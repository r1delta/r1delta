// ogg_decoder.cpp
// Drop–in replacement for the original WAV–based decoder.
// Implements progressive decoding (with an enlarged initial decode buffer + background decoding),
// asynchronous read with synchronous “just–in–time” decoding when needed,
// and memory–allocation optimizations.
//
// FIXES:
// 1. MemoryBuffer lifetime issues are fixed by storing a persistent copy in the progressive template.
// 2. A single shared OggVorbis_File is used for both background and synchronous decoding,
//    preventing sample misalignment.
// 3. A larger initial synchronous decode buffer is filled so that the handoff to the async thread
//    doesn’t cause underruns.
// 4. In the synchronous branch, if more samples are needed the code immediately decodes additional
//    chunks rather than waiting, reducing the chance of buffer underruns.
//
// Note: This file is self–contained and must be compiled with the required libraries (e.g. vorbis).

#include <windows.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <filesystem>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <atomic>
#include <condition_variable>
#include <thread>
#include <memory>
#include <cstdio>
#include <emmintrin.h>    // SSE2
#include <xmmintrin.h>    // SSE

// Replace Opus headers with Vorbis headers.
#include <vorbis/vorbisfile.h>
#include <vorbis/codec.h>

#include "load.h"
#include "logging.h"
#include "audio.h"
#include "tctx.hh"

// --------------------------------------------------------------------
// Core filesystem enums/structs (unchanged)
// --------------------------------------------------------------------
enum FSAsyncStatus_t {
    FSASYNC_ERR_FILEOPEN = -1,
    FSASYNC_OK = 0,
    FSASYNC_ERR_READING = -4
};

enum FSAsyncFlags_t {
    FSASYNC_FLAGS_NULLTERMINATE = (1 << 3)
};

struct FileAsyncRequest_t // x64 version
{
    const char* pszFilename;
    void* pData;
    __int64 nOffset;
    __int64 nBytes;
    void(__fastcall* pfnCallback)(const FileAsyncRequest_t*, __int64, int);
    void* pContext; // Used by the game – do not use.
    int priority;
    int flags;
    const char* pszPathID;
    void* hSpecificAsyncFile;
    void* (__fastcall* pfnAlloc)(const char*, __int64);
};
static_assert(offsetof(FileAsyncRequest_t, pszPathID) == 56);

struct AsyncOpenedFile_t {
    char pad[16];
    __int64 hFile;
};

class CBaseFileSystem {
public:
    std::uint64_t vtable;
    std::uint64_t innerVTable;  // at offset 8
    char padding[0x260];
    int fwLevel;
};

// --------------------------------------------------------------------
// Tier0 thread priority stubs (unchanged)
// --------------------------------------------------------------------
typedef int(__fastcall* ThreadGetPriorityFn)(std::uint64_t);
typedef void(__fastcall* ThreadSetPriorityFn)(std::uint64_t, int);
ThreadGetPriorityFn OriginalThreadGetPriority = nullptr;
ThreadSetPriorityFn OriginalThreadSetPriority = nullptr;

void LoadTier0Functions() {
    HMODULE tier0Module = GetModuleHandleA("tier0_orig.dll");
    OriginalThreadGetPriority = reinterpret_cast<ThreadGetPriorityFn>(
        GetProcAddress(tier0Module, "ThreadGetPriority"));
    OriginalThreadSetPriority = reinterpret_cast<ThreadSetPriorityFn>(
        GetProcAddress(tier0Module, "ThreadSetPriority"));
}

extern "C" {
    int __fastcall ThreadGetPriority(std::uint64_t threadId) {
        if (!OriginalThreadGetPriority) {
            LoadTier0Functions();
        }
        return OriginalThreadGetPriority(threadId);
    }

    void __fastcall ThreadSetPriority(std::uint64_t threadId, int priority) {
        if (!OriginalThreadSetPriority) {
            LoadTier0Functions();
        }
        OriginalThreadSetPriority(threadId, priority);
    }
}

// --------------------------------------------------------------------
// Core filesystem vtable–based function pointers (unchanged)
// --------------------------------------------------------------------
constexpr std::uint64_t FS_INVALID_ASYNC_FILE = 0xFFFFULL;
typedef void* FileHandle_t;

namespace FSOperations {
    static void* OpenFile(CBaseFileSystem* fs, const char* filename, const char* mode,
        int flags, const char* pathID, std::uint64_t resolvedFilename)
    {
        return reinterpret_cast<void* (__fastcall*)(CBaseFileSystem*, const char*, const char*, int, const char*, std::uint64_t)>(
            *reinterpret_cast<std::uint64_t*>(fs->vtable + 592LL)
            )(fs, filename, mode, flags, pathID, resolvedFilename);
    }

    static __int64 GetFileSize(CBaseFileSystem* fs, FileHandle_t file) {
        std::uint64_t innerThis = reinterpret_cast<std::uint64_t>(fs) + 8;
        auto vtable = *reinterpret_cast<std::uint64_t*>(innerThis);
        return reinterpret_cast<__int64(__fastcall*)(std::uint64_t, void*)>(
            *reinterpret_cast<std::uint64_t*>(vtable + 56LL)
            )(innerThis, file);
    }

    static void SeekFile(CBaseFileSystem* fs, FileHandle_t file, __int64 offset, int origin) {
        std::uint64_t innerThis = reinterpret_cast<std::uint64_t>(fs) + 8;
        auto vtable = *reinterpret_cast<std::uint64_t*>(innerThis);
        reinterpret_cast<void(__fastcall*)(std::uint64_t, void*, __int64, int)>(
            *reinterpret_cast<std::uint64_t*>(vtable + 32LL)
            )(innerThis, file, offset, origin);
    }

    static bool GetOptimalIO(CBaseFileSystem* fs, FileHandle_t file,
        std::uint64_t* pAlign, std::uint64_t zero1, std::uint64_t zero2)
    {
        return reinterpret_cast<bool(__fastcall*)(CBaseFileSystem*, void*, std::uint64_t*, std::uint64_t, std::uint64_t)>(
            *reinterpret_cast<std::uint64_t*>(fs->vtable + 704LL)
            )(fs, file, pAlign, zero1, zero2);
    }

    static void* AllocOptimalReadBuffer(CBaseFileSystem* fs, FileHandle_t file,
        __int64 size, std::uint64_t offset)
    {
        return reinterpret_cast<void* (__fastcall*)(CBaseFileSystem*, void*, __int64, std::uint64_t)>(
            *reinterpret_cast<std::uint64_t*>(fs->vtable + 712LL)
            )(fs, file, size, offset);
    }

    static __int64 ReadFile(CBaseFileSystem* fs, void* dest, std::uint64_t destSize,
        std::uint64_t size, FileHandle_t file)
    {
        return reinterpret_cast<__int64(__fastcall*)(CBaseFileSystem*, void*, std::uint64_t, std::uint64_t, void*)>(
            *reinterpret_cast<std::uint64_t*>(fs->vtable + 600LL)
            )(fs, dest, destSize, size, file);
    }

    static void CloseFile(CBaseFileSystem* fs, FileHandle_t file) {
        std::uint64_t innerThis = reinterpret_cast<std::uint64_t>(fs) + 8;
        auto vtable = *reinterpret_cast<std::uint64_t*>(innerThis);
        reinterpret_cast<void(__fastcall*)(std::uint64_t, void*)>(
            *reinterpret_cast<std::uint64_t*>(vtable + 24LL)
            )(innerThis, file);
    }

    static void SetBufferSize(CBaseFileSystem* fs, FileHandle_t file, std::uint64_t size) {
        return reinterpret_cast<void(__fastcall*)(CBaseFileSystem*, void*, std::uint64_t)>(
            *reinterpret_cast<std::uint64_t*>(fs->vtable + 184LL)
            )(fs, file, size);
    }

    static bool FileExists(CBaseFileSystem* fs, const char* filename, const char* pathID) {
        std::uint64_t innerThis = reinterpret_cast<std::uint64_t>(fs) + 8;
        auto vtable = *reinterpret_cast<std::uint64_t*>(innerThis);
        return reinterpret_cast<__int64(__fastcall*)(std::uint64_t, const char*, const char*)>(
            *reinterpret_cast<std::uint64_t*>(vtable + 104LL)
            )(innerThis, filename, pathID);
    }
}

static void(__fastcall* pFreeAsyncFileHandle)(std::uint64_t);
static std::uint64_t(__fastcall* pGetOptimalReadSize)(CBaseFileSystem*, void*, std::uint64_t);
static void(__fastcall* pPerformAsyncCallback)(CBaseFileSystem*, FileAsyncRequest_t*, void*, std::uint64_t, std::uint64_t);
static void(__fastcall* pReleaseAsyncOpenedFiles)(CRITICAL_SECTION*, void*);
static void(__fastcall* pLogFileAccess)(CBaseFileSystem*, const char*, const char*, void*);

static CRITICAL_SECTION* pAsyncOpenedFilesCriticalSection;
static std::uint64_t pAsyncOpenedFilesTable;
static void* pFileAccessLoggingPointer;

// --------------------------------------------------------------------
// Ogg Vorbis – specific definitions and callbacks
// --------------------------------------------------------------------

// Although both Opus and Vorbis in Ogg containers start with "OggS", we
// leave this helper unchanged.
static bool IsOpusFile(const std::vector<unsigned char>& fileData) {
    return (fileData.size() >= 4 && std::memcmp(fileData.data(), "OggS", 4) == 0);
}

// A simple memory–based data source for ov_open_callbacks.
// (Note: We now use persistent MemoryBuffer instances.)
struct MemoryBuffer {
    const unsigned char* data;
    size_t size;
    size_t pos;
};

static size_t MemoryReadCallback(void* ptr, size_t size, size_t nmemb, void* datasource) {
    MemoryBuffer* mem = reinterpret_cast<MemoryBuffer*>(datasource);
    size_t bytesRequested = size * nmemb;
    size_t bytesAvailable = (mem->size > mem->pos) ? (mem->size - mem->pos) : 0;
    size_t bytesToRead = (bytesRequested < bytesAvailable) ? bytesRequested : bytesAvailable;
    memcpy(ptr, mem->data + mem->pos, bytesToRead);
    mem->pos += bytesToRead;
    // Return number of items read, in "chunks" of `size`.
    return bytesToRead / size;
}

static int MemorySeekCallback(void* datasource, ogg_int64_t offset, int whence) {
    MemoryBuffer* mem = reinterpret_cast<MemoryBuffer*>(datasource);
    size_t newpos;
    switch (whence) {
    case SEEK_SET: newpos = static_cast<size_t>(offset); break;
    case SEEK_CUR: newpos = mem->pos + static_cast<size_t>(offset); break;
    case SEEK_END: newpos = mem->size + static_cast<size_t>(offset); break;
    default: return -1;
    }
    if (newpos > mem->size)
        return -1;
    mem->pos = newpos;
    return 0;
}

static int MemoryCloseCallback(void* datasource) {
    // Nothing to free here; the caller manages the memory.
    return 0;
}

static long MemoryTellCallback(void* datasource) {
    MemoryBuffer* mem = reinterpret_cast<MemoryBuffer*>(datasource);
    return static_cast<long>(mem->pos);
}

// --------------------------------------------------------------------
// Progressive Ogg Vorbis decoding definitions
// --------------------------------------------------------------------
//
// This structure holds the progressively–decoded PCM data (in multiple small chunks),
// along with atomic counters and synchronization primitives. Importantly, it now also
// owns a persistent MemoryBuffer (pMemBuffer) that remains valid until decoding finishes.
struct ProgressiveOpusTemplate {
    std::vector<std::vector<int16_t>> pcmChunks; // multiple smaller buffers
    std::atomic<size_t> decodedChunks;          // number of chunks decoded so far
    int channels;
    bool fullyDecoded;
    SRWLOCK chunkMutex;                 // protects pcmChunks
    //CONDITION_VARIABLE chunkCV;    // signals when new chunks are added
    std::vector<unsigned char> fileData;  // persistent copy of the original file data
    MemoryBuffer pMemBuffer; // persistent memory buffer for ov_open_callbacks

    // Shared decoder pointer and a mutex to protect it.
    OggVorbis_File* vf;
    SRWLOCK decodeMutex;

    ProgressiveOpusTemplate() : decodedChunks(0), channels(1), fullyDecoded(false), vf(nullptr), chunkMutex(SRWLOCK_INIT), decodeMutex(SRWLOCK_INIT) {}
    ~ProgressiveOpusTemplate() {}

    static constexpr size_t CHUNK_SIZE = 32768; // number of int16_t samples per channel per chunk
};

static std::unordered_map<std::string, ProgressiveOpusTemplate*, HashStrings, std::equal_to<>> g_progressiveCache;
static SRWLOCK g_progressiveCacheMutex = SRWLOCK_INIT;

// --------------------------------------------------------------------
// Global map for per–request playback instances.
// --------------------------------------------------------------------
struct OpusPlaybackInstance {
    ProgressiveOpusTemplate* progTemplate;
    size_t readSampleIndex; // absolute sample index (across chunks)
    int channels;
    SRWLOCK bufferMutex;

    OpusPlaybackInstance() : readSampleIndex(0), channels(1), bufferMutex(SRWLOCK_INIT) {}
    ~OpusPlaybackInstance() {}
};

static std::unordered_map<FileAsyncRequest_t*, OpusPlaybackInstance*> g_playbackInstances;
static SRWLOCK g_playbackInstancesMutex = SRWLOCK_INIT;

// --------------------------------------------------------------------
// Helper: Read entire file via FSOperations (unchanged)
// --------------------------------------------------------------------
static std::vector<unsigned char> ReadEntireFileIntoMem(
    CBaseFileSystem* fs,
    const char* path,
    const char* pathID)
{
    ZoneScoped;

    std::vector<unsigned char> fileData;
    void* file = FSOperations::OpenFile(fs, path, "rb", 0, pathID, 0);
    if (!file) {
        return fileData; // empty
    }
    __int64 size = FSOperations::GetFileSize(fs, file);
    if (size <= 0) {
        FSOperations::CloseFile(fs, file);
        return fileData;
    }
    fileData.resize(static_cast<size_t>(size));
    FSOperations::SeekFile(fs, file, 0, 0);
    __int64 bytesRead = FSOperations::ReadFile(fs, &fileData[0], size, size, file);
    FSOperations::CloseFile(fs, file);
    if (bytesRead < size) {
        fileData.clear();
    }
    return fileData;
}

// --------------------------------------------------------------------
// Decode one chunk of PCM samples from the Ogg Vorbis stream.
// Returns a vector of int16_t samples (up to CHUNK_SIZE * channels in length).
// --------------------------------------------------------------------
static std::vector<int16_t> DecodeChunk(OggVorbis_File* vf, int channels) {
    ZoneScoped;

    const size_t maxSamples = ProgressiveOpusTemplate::CHUNK_SIZE * channels;
    std::vector<int16_t> chunk(maxSamples, 0);
    //const int DECODE_BUFFER_SIZE = 65536; // in bytes
    //char decodeBuffer[DECODE_BUFFER_SIZE];
    int bitStream = 0;
    size_t totalSamples = 0;
    while (totalSamples < maxSamples) {
        size_t bytesToRead = (maxSamples - totalSamples) * sizeof(int16_t);
#if 1
        long bytesDecoded = ov_read(vf, (char*)&chunk[totalSamples], (int)bytesToRead, 0, 2, 1, &bitStream);
        if (bytesDecoded <= 0)
            break; // End of file or error.
        size_t samplesDecoded = bytesDecoded / sizeof(int16_t);
        totalSamples += samplesDecoded;
#else
        long bytesDecoded = ov_read(vf, decodeBuffer, (int)bytesToRead, 0, 2, 1, &bitStream);
        if (bytesDecoded <= 0)
            break; // End of file or error.
        size_t samplesDecoded = bytesDecoded / sizeof(int16_t);
        size_t oldSize = chunk.size();
        chunk.resize(oldSize + samplesDecoded);
        memcpy(&chunk[oldSize], decodeBuffer, samplesDecoded * sizeof(int16_t));
        totalSamples += samplesDecoded;
#endif
    }
    chunk.resize(totalSamples);
    return chunk;
}

// --------------------------------------------------------------------
// Background decoder: continuously decode remaining chunks and add them to the template.
// Runs in its own thread.
// --------------------------------------------------------------------
static void BackgroundDecodeThread(ProgressiveOpusTemplate* progTemplate, int channels) {
    while (true) {
        std::vector<int16_t> chunk;
        {
            SRWGuard decodeLock(&progTemplate->decodeMutex);
            if (progTemplate->vf == nullptr) {
                break;
            }
            chunk = DecodeChunk(progTemplate->vf, channels);
        }
        {
            SRWGuard lock(&progTemplate->chunkMutex);
            if (chunk.empty()) {
                progTemplate->fullyDecoded = true;
                //WakeAllConditionVariable(&progTemplate->chunkCV);
                break;
            }
            progTemplate->pcmChunks.push_back(std::move(chunk));
            progTemplate->decodedChunks.fetch_add(1, std::memory_order_relaxed);
            //WakeAllConditionVariable(&progTemplate->chunkCV);
        }
    }
    {
        SRWGuard decodeLock(&progTemplate->decodeMutex);
        if (progTemplate->vf) {
            ov_clear(progTemplate->vf);
            GlobalAllocator()->mi_free(progTemplate->vf, TAG_AUDIO, HEAP_DELTA);
            progTemplate->vf = nullptr;
        }
    }
}

// --------------------------------------------------------------------
// Background decoder entry-point wrapper (new).
// Bumps its own thread priority one notch to reduce underruns.
// --------------------------------------------------------------------
static void BackgroundDecodeWrapper(ProgressiveOpusTemplate* progTemplate, int channels) {
    DWORD tid = GetCurrentThreadId();
    int basePrio = ThreadGetPriority(tid);
    ThreadSetPriority(tid, basePrio + 1);
    BackgroundDecodeThread(progTemplate, channels);
}

// --------------------------------------------------------------------
// Progressive decode: open an Ogg Vorbis file, decode a larger number of initial chunks,
// and then launch a background thread to decode the remainder.
// --------------------------------------------------------------------
bool OpenOpusContext(const char* wavName, CBaseFileSystem* filesystem, const char* pathID) {
    ZoneScoped;

    SRWGuard lock(&g_progressiveCacheMutex);
    if (g_progressiveCache.find(wavName) != g_progressiveCache.end()) {
        return true; // Already opened/cached.
    }

    std::vector<unsigned char> fileData = ReadEntireFileIntoMem(filesystem, wavName, pathID);
    if (fileData.empty())
        return false;

    // Check header ("OggS"). Both Opus and Vorbis in Ogg containers use this.
    if (!(fileData.size() >= 4 && std::memcmp(fileData.data(), "OggS", 4) == 0))
        return false;

    // Create a progressive template.
    auto progTemplate = new (GlobalAllocator()->mi_malloc(sizeof(ProgressiveOpusTemplate), TAG_AUDIO, HEAP_DELTA)) ProgressiveOpusTemplate();
    progTemplate->fileData = std::move(fileData);

    // Create a persistent MemoryBuffer and store it in the template.
    progTemplate->pMemBuffer.data = progTemplate->fileData.data();
    progTemplate->pMemBuffer.size = progTemplate->fileData.size();
    progTemplate->pMemBuffer.pos = 0;

    // Prepare the callbacks.
    ov_callbacks callbacks;
    callbacks.read_func = MemoryReadCallback;
    callbacks.seek_func = MemorySeekCallback;
    callbacks.close_func = MemoryCloseCallback;
    callbacks.tell_func = MemoryTellCallback;

    // Allocate an OggVorbis_File on the heap so that it can be shared.
    OggVorbis_File* vf = (OggVorbis_File*)GlobalAllocator()->mi_malloc(sizeof(OggVorbis_File), TAG_AUDIO, HEAP_DELTA);
    int result = ov_open_callbacks(&progTemplate->pMemBuffer, vf, nullptr, (long)progTemplate->fileData.size(), callbacks);
    if (result < 0) {
        R1DAssert(!"Unreachable");
        Warning("[Ogg] ov_open_callbacks failed with error %d for file %s\n", result, wavName);
        GlobalAllocator()->mi_free(vf, TAG_AUDIO, HEAP_DELTA);
        return false;
    }

    vorbis_info* vi = ov_info(vf, -1);
    if (!vi) {
        R1DAssert(!"Unreachable");
        ov_clear(vf);
        GlobalAllocator()->mi_free(vf, TAG_AUDIO, HEAP_DELTA);
        Warning("[Ogg] ov_info failed for file %s\n", wavName);
        return false;
    }
    int channels = vi->channels;

    progTemplate->channels = channels;
    progTemplate->vf = vf;

    // Increase the initial buffer by decoding more chunks synchronously.
    {
        SRWGuard decodeLock(&progTemplate->decodeMutex);

        const size_t INITIAL_CHUNKS = 16; // increased from 8
        for (size_t i = 0; i < INITIAL_CHUNKS; i++) {
            std::vector<int16_t> chunk;
            {
                chunk = DecodeChunk(progTemplate->vf, channels);
            }
            if (chunk.empty()) {
                progTemplate->fullyDecoded = true;
                break;
            }
            progTemplate->pcmChunks.push_back(std::move(chunk));
            progTemplate->decodedChunks.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // Start an asynchronous worker thread with bumped priority.
    std::thread bgThread(BackgroundDecodeWrapper, progTemplate, channels);
    bgThread.detach();

    // Save the progressive template in the global cache.
    g_progressiveCache[wavName] = progTemplate;
    return true;
}

// --------------------------------------------------------------------
// Create a new playback instance from the cached progressive data.
// Each instance maintains its own read pointer (in absolute sample index) into the decoded PCM.
// --------------------------------------------------------------------
static OpusPlaybackInstance* CreateOpusTrackInstance(const char* filename,
    CBaseFileSystem* filesystem,
    const char* pathID)
{
    ZoneScoped;
    
    ProgressiveOpusTemplate* progTemplate;
    {
        SRWGuard lock(&g_progressiveCacheMutex);
        auto it = g_progressiveCache.find(filename);
        if (it == g_progressiveCache.end()) {
            R1DAssert(!"Will deadlock due to double g_progressiveCacheMutex lock. Blame wanderer for using Gemini.");
            if (!OpenOpusContext(filename, filesystem, pathID))
                return nullptr;
            it = g_progressiveCache.find(filename);
            if (it == g_progressiveCache.end())
                return nullptr;
        }
        progTemplate = it->second;
    }

    OpusPlaybackInstance* instance = new (GlobalAllocator()->mi_malloc(sizeof(*instance), TAG_AUDIO, HEAP_DELTA)) OpusPlaybackInstance();
    instance->progTemplate = progTemplate;
    instance->channels = progTemplate->channels;
    instance->readSampleIndex = 0;
    return instance;
}

// --------------------------------------------------------------------
// Helper: Compute total number of decoded samples available so far.
// --------------------------------------------------------------------
static size_t GetTotalAvailableSamples(ProgressiveOpusTemplate* progTemplate) {
    ZoneScoped;
    
    size_t total = 0;
    SRWGuard lock(&progTemplate->chunkMutex);
    for (const auto& chunk : progTemplate->pcmChunks) {
        total += chunk.size();
    }
    return total;
}
static size_t GetTotalAvailableSamplesNoLock(ProgressiveOpusTemplate* progTemplate) {
    ZoneScoped;

    size_t total = 0;
    for (const auto& chunk : progTemplate->pcmChunks) {
        total += chunk.size();
    }
    return total;
}

// --------------------------------------------------------------------
// Helper: Copy samples from the progressive template to the destination buffer,
// starting at the given absolute sample index.
// --------------------------------------------------------------------
static void CopySamples(ProgressiveOpusTemplate* progTemplate, size_t startSample, int16_t* dest, size_t samplesToCopy)
{
    ZoneScoped;

    size_t copied = 0;
    SRWGuard lock(&progTemplate->chunkMutex);
    for (const auto& chunk : progTemplate->pcmChunks) {
        if (startSample >= chunk.size()) {
            startSample -= chunk.size();
            continue;
        }
        size_t availableInChunk = chunk.size() - startSample;
        size_t copyNow = std::min(availableInChunk, samplesToCopy - copied);
        memcpy(dest + copied, chunk.data() + startSample, copyNow * sizeof(int16_t));
        copied += copyNow;
        startSample = 0;
        if (copied >= samplesToCopy)
            break;
    }
}

// --------------------------------------------------------------------
// The main read handler.
// Reads from the progressive template; if the requested samples extend beyond
// the currently decoded data, it synchronously decodes the missing chunks.
// Uses SSE2/AVX for fast copies (via memcpy) when possible.
// --------------------------------------------------------------------
__int64 HandleOpusRead(CBaseFileSystem* filesystem, FileAsyncRequest_t* request) {
    ZoneScoped;

    OpusPlaybackInstance* instance = nullptr;
    {
        SRWGuard lock(&g_playbackInstancesMutex);
        auto it = g_playbackInstances.find(request);
        if (it != g_playbackInstances.end())
            instance = it->second;
    }
    if (!instance) {
        instance = CreateOpusTrackInstance(request->pszFilename, filesystem, request->pszPathID);
        if (!instance) {
            Warning("[Ogg] Failed to create playback instance for '%s'\n", request->pszFilename);
            return FSASYNC_ERR_FILEOPEN;
        }
        {
            SRWGuard lock(&g_playbackInstancesMutex);
            g_playbackInstances[request] = instance;
        }
    }

    int channels = instance->channels;
    size_t bytesPerSample = sizeof(int16_t);
    size_t bytesPerFrame = bytesPerSample * channels;
    size_t framesRequested = static_cast<size_t>(request->nBytes) / bytesPerFrame;
    size_t samplesRequested = framesRequested * channels;

    // Handle skipping
    if (request->nOffset > 0)
    {
        SRWGuard lock(&instance->bufferMutex);
        size_t skipBytes = static_cast<size_t>(request->nOffset);
        size_t skipFrames = skipBytes / bytesPerFrame;
        instance->readSampleIndex = skipFrames * channels;
        size_t totalSamples = GetTotalAvailableSamples(instance->progTemplate);
        if (instance->readSampleIndex > totalSamples) {
            instance->readSampleIndex = totalSamples;
        }
    }

    // Allocate (or use) the destination buffer.
    void* pDest = request->pData;
    if (!pDest) {
        R1DAssert(!"Unreachable?");
        if (request->pfnAlloc)
            pDest = request->pfnAlloc(request->pszFilename, request->nBytes);
        else
            pDest = GlobalAllocator()->mi_malloc(request->nBytes, TAG_AUDIO, HEAP_DELTA);
    }

    // Ensure that the progressive template has decoded enough samples.
    {
        auto progTemplate = instance->progTemplate;
        size_t availableSamples = GetTotalAvailableSamples(progTemplate);
        // If insufficient samples are buffered, synchronously decode additional chunks immediately.
        SRWGuard decodeLock(&progTemplate->decodeMutex);
        SRWGuard lock(&progTemplate->chunkMutex);
        while (availableSamples < instance->readSampleIndex + samplesRequested && !progTemplate->fullyDecoded) {
            std::vector<int16_t> newChunk;
            {
                if (progTemplate->vf == nullptr) {
                    progTemplate->fullyDecoded = true;
                    break;
                }
                newChunk = DecodeChunk(progTemplate->vf, progTemplate->channels);
            }
            if (!newChunk.empty()) {
                progTemplate->pcmChunks.push_back(std::move(newChunk));
                progTemplate->decodedChunks.fetch_add(1, std::memory_order_relaxed);
                //WakeAllConditionVariable(&progTemplate->chunkCV);
            }
            else {
                progTemplate->fullyDecoded = true;
                break;
            }
            availableSamples = GetTotalAvailableSamplesNoLock(progTemplate);
        }
    }

    // Copy the available samples into the destination buffer, then zero the remainder.
    size_t copiedSamples = 0;
    {
        SRWGuard lock(&instance->bufferMutex);
        size_t availableSamples = GetTotalAvailableSamples(instance->progTemplate);
        size_t toCopy = std::min(samplesRequested,
            availableSamples > instance->readSampleIndex ? availableSamples - instance->readSampleIndex : 0);
        if (toCopy > 0) {
            CopySamples(instance->progTemplate, instance->readSampleIndex, reinterpret_cast<int16_t*>(pDest), toCopy);
            instance->readSampleIndex += toCopy;
            copiedSamples = toCopy;
        }
    }
    // Zero any remaining bytes (silence).
    __int64 bytesCopied = copiedSamples * sizeof(int16_t);
    if (bytesCopied < request->nBytes) {
        memset((char*)pDest + bytesCopied, 0, request->nBytes - bytesCopied);
    }

    // IMPORTANT: Call the async callback with the full buffer size,
    // so that if fewer samples were available, the remaining data (zeros) is sent.
    pPerformAsyncCallback(filesystem, request, pDest, request->nBytes, FSASYNC_OK);
    return FSASYNC_OK;
}

// --------------------------------------------------------------------
// Helper: Check if filename ends with ".wav"
// --------------------------------------------------------------------
static bool EndsWithWav(const char* filename) {
    ZoneScoped;

    if (!filename) return false;
    std::string_view fn(filename);
    if (fn.size() < 4) return false;
    auto ext = fn.substr(fn.size() - 4);
    return ext[0] == '.' && tolower(ext[1]) == 'w' && tolower(ext[2]) == 'a' && tolower(ext[3]) == 'v';
}

// --------------------------------------------------------------------
// The main filesystem read hook.
// If the filename ends with ".wav", we temporarily rewrite it to ".ogg"
// and use our custom handler. Otherwise, we fall back to the original read.
// --------------------------------------------------------------------
__int64 (*Original_CBaseFileSystem__SyncRead)(
    CBaseFileSystem* filesystem,
    FileAsyncRequest_t* request);

__int64 __fastcall Hooked_CBaseFileSystem__SyncRead(
    CBaseFileSystem* filesystem,
    FileAsyncRequest_t* request)
{
    ZoneScoped;

    static bool bInited = false;
    if (!bInited) {
        bInited = true;
        // Initialize the various function pointers and globals.
        pFreeAsyncFileHandle = reinterpret_cast<void(__fastcall*)(std::uint64_t)>(G_filesystem_stdio + 0x38610);
        pGetOptimalReadSize = reinterpret_cast<std::uint64_t(__fastcall*)(CBaseFileSystem*, void*, std::uint64_t)>(G_filesystem_stdio + 0x4B70);
        pPerformAsyncCallback = reinterpret_cast<void(__fastcall*)(CBaseFileSystem*, FileAsyncRequest_t*, void*, std::uint64_t, std::uint64_t)>(G_filesystem_stdio + 0x1F200);
        pReleaseAsyncOpenedFiles = reinterpret_cast<void(__fastcall*)(CRITICAL_SECTION*, void*)>(G_filesystem_stdio + 0x233C0);
        pLogFileAccess = reinterpret_cast<void(__fastcall*)(CBaseFileSystem*, const char*, const char*, void*)>(G_filesystem_stdio + 0x9A40);

        pAsyncOpenedFilesCriticalSection = reinterpret_cast<CRITICAL_SECTION*>(G_filesystem_stdio + 0xF90A0);
        pAsyncOpenedFilesTable = G_filesystem_stdio + 0xF90D0;
        pFileAccessLoggingPointer = reinterpret_cast<void*>(G_filesystem_stdio + 0xCDBC0);
    }

    // Basic sanity check on the request.
    if (request->nBytes < 0 || request->nOffset < 0) {
        if (request->pfnCallback) {
            request->pfnCallback(request, 0, FSASYNC_ERR_FILEOPEN);
        }
        return FSASYNC_ERR_FILEOPEN;
    }

    // If the filename ends with ".wav", rewrite it temporarily.
    if (EndsWithWav(request->pszFilename)) {
        auto arena = tctx.get_arena_for_scratch();
        auto temp = TempArena(arena);

        const char* originalFilename = request->pszFilename;
        const size_t originalFilename_len = strlen(request->pszFilename);
        
        // Why was this done in the first place?
        auto modifiedFileName = (char*)arena_push(arena, originalFilename_len + 1);
        memcpy(modifiedFileName, originalFilename, originalFilename_len);
        memcpy(modifiedFileName + originalFilename_len - 4, ".ogg", 4);

        request->pszFilename = modifiedFileName;

        __int64 result = FSASYNC_ERR_FILEOPEN;
        if (OpenOpusContext(request->pszFilename, filesystem, request->pszPathID)) {
            result = HandleOpusRead(filesystem, request);
        }

        request->pszFilename = originalFilename;
        if (result != FSASYNC_ERR_FILEOPEN)
            return result;
    }

    // Fall through to the original read if no custom handling occurred.
    return Original_CBaseFileSystem__SyncRead(filesystem, request);
}

// --------------------------------------------------------------------
// Async read job destructor hook: free our playback instance.
// --------------------------------------------------------------------
void (*Original_CFileAsyncReadJob_dtor)(FileAsyncRequest_t* thisptr);

void CFileAsyncReadJob_dtor(FileAsyncRequest_t* thisptr) {
    {
        SRWGuard lock(&g_playbackInstancesMutex);
        auto it = g_playbackInstances.find((FileAsyncRequest_t*)(((__int64)thisptr) + 0x70));
        if (it != g_playbackInstances.end()) {
            OpusPlaybackInstance* instance = it->second;
            instance->~OpusPlaybackInstance();
            GlobalAllocator()->mi_free(instance, TAG_AUDIO, HEAP_DELTA);
            g_playbackInstances.erase(it);
        }
    }
    return Original_CFileAsyncReadJob_dtor(thisptr);
}

// NOTE(mrsteyk): I'm not too fond of how this shit works, so this function is to track my suspicions.
void AudioReportMemory()
{
    ZoneScoped;

    {
        SRWGuard lock(&g_progressiveCacheMutex);
        size_t total_untracked_mem = 0;
        for (auto& e : g_progressiveCache)
        {
            ProgressiveOpusTemplate* pt = e.second;

            SRWGuard lock(&pt->chunkMutex);
            for (auto& ee : pt->pcmChunks)
            {
                total_untracked_mem += ee.size() * sizeof(ee[0]);
            }
        }
        Msg("* Delta Audio: num entries in progressive cache: %zu, untracked sample mem: %.02f MB\n", g_progressiveCache.size(), (float)total_untracked_mem / (1 << 20));
    }
}