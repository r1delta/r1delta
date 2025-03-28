// ogg_decoder.cpp
// Drop–in replacement for the original Opus–based decoder.
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
// Critical section helpers (unchanged)
// --------------------------------------------------------------------
static void EnterCriticalSection_(LPCRITICAL_SECTION cs) {
    ::EnterCriticalSection(cs);
}
static void LeaveCriticalSection_(LPCRITICAL_SECTION cs) {
    ::LeaveCriticalSection(cs);
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
static bool IsOpusFile(const std::vector<unsigned char>& fileData)
{
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

// This structure holds the progressively–decoded PCM data (in multiple small chunks),
// along with atomic counters and synchronization primitives. Importantly, it now also
// owns a persistent MemoryBuffer (pMemBuffer) that remains valid until decoding finishes.
struct ProgressiveOpusTemplate {
    std::vector<std::vector<int16_t>> pcmChunks; // multiple smaller buffers
    std::atomic<size_t> decodedChunks; // number of chunks decoded so far
    int channels;
    bool fullyDecoded;
    std::mutex chunkMutex;              // protects pcmChunks
    std::condition_variable chunkCV;    // signals when new chunks are added
    std::vector<unsigned char> fileData;  // persistent copy of the original file data
    std::shared_ptr<MemoryBuffer> pMemBuffer; // persistent memory buffer for ov_open_callbacks

    // Shared decoder pointer and a mutex to protect it.
    OggVorbis_File* vf;
    std::mutex decodeMutex;

    ProgressiveOpusTemplate() : decodedChunks(0), channels(1), fullyDecoded(false), vf(nullptr) {}
    static constexpr size_t CHUNK_SIZE = 32768; // number of int16_t samples per channel per chunk
};

// Global cache for progressive Ogg Vorbis file data.
static std::unordered_map<std::string, std::shared_ptr<ProgressiveOpusTemplate>> g_progressiveCache;
static std::mutex g_progressiveCacheMutex;

// --------------------------------------------------------------------
// Global map for per–request playback instances.
// --------------------------------------------------------------------
struct OpusPlaybackInstance {
    std::shared_ptr<ProgressiveOpusTemplate> progTemplate;
    size_t readSampleIndex; // absolute sample index (across chunks)
    int channels;
    std::unique_ptr<std::recursive_mutex> bufferMutex;
    OpusPlaybackInstance() : readSampleIndex(0), channels(1), bufferMutex(std::make_unique<std::recursive_mutex>()) {}
};

static std::unordered_map<FileAsyncRequest_t*, OpusPlaybackInstance*> g_playbackInstances;
static std::mutex g_playbackInstancesMutex;

// --------------------------------------------------------------------
// Helper: Read entire file via FSOperations (unchanged)
// --------------------------------------------------------------------
static std::vector<unsigned char> ReadEntireFileIntoMem(
    CBaseFileSystem* fs,
    const char* path,
    const char* pathID)
{
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
    const size_t maxSamples = ProgressiveOpusTemplate::CHUNK_SIZE * channels;
    std::vector<int16_t> chunk;
    chunk.reserve(maxSamples);
    const int DECODE_BUFFER_SIZE = 65536; // in bytes
    char decodeBuffer[DECODE_BUFFER_SIZE];
    int bitStream = 0;
    size_t totalSamples = 0;
    while (totalSamples < maxSamples) {
        size_t bytesToRead = std::min((size_t)DECODE_BUFFER_SIZE, (maxSamples - totalSamples) * sizeof(int16_t));
        long bytesDecoded = ov_read(vf, decodeBuffer, (int)bytesToRead, 0, 2, 1, &bitStream);
        if (bytesDecoded <= 0)
            break; // End of file or error.
        size_t samplesDecoded = bytesDecoded / sizeof(int16_t);
        size_t oldSize = chunk.size();
        chunk.resize(oldSize + samplesDecoded);
        memcpy(&chunk[oldSize], decodeBuffer, samplesDecoded * sizeof(int16_t));
        totalSamples += samplesDecoded;
    }
    return chunk;
}

// --------------------------------------------------------------------
// Background decoder: continuously decode remaining chunks and add them to the template.
// Runs in its own thread.
// --------------------------------------------------------------------
static void BackgroundDecodeThread(std::shared_ptr<ProgressiveOpusTemplate> progTemplate, int channels) {
    while (true) {
        std::vector<int16_t> chunk;
        {
            std::lock_guard<std::mutex> decodeLock(progTemplate->decodeMutex);
            // If vf is already null, break out.
            if (progTemplate->vf == nullptr) {
                break;
            }
            chunk = DecodeChunk(progTemplate->vf, channels);
        }
        {
            std::lock_guard<std::mutex> lock(progTemplate->chunkMutex);
            if (chunk.empty()) {
                progTemplate->fullyDecoded = true;
                progTemplate->chunkCV.notify_all();
                break;
            }
            progTemplate->pcmChunks.push_back(std::move(chunk));
            progTemplate->decodedChunks.fetch_add(1, std::memory_order_relaxed);
            progTemplate->chunkCV.notify_all();
        }
    }
    // Cleanup the shared decoder under the same mutex to prevent races.
    {
        std::lock_guard<std::mutex> decodeLock(progTemplate->decodeMutex);
        if (progTemplate->vf) {
            ov_clear(progTemplate->vf);
            delete progTemplate->vf;
            progTemplate->vf = nullptr;
        }
    }
}


// --------------------------------------------------------------------
// Progressive decode: open an Ogg Vorbis file, decode a larger number of initial chunks,
// and then launch a background thread to decode the remainder.
// --------------------------------------------------------------------
bool OpenOpusContext(const std::string& wavName, CBaseFileSystem* filesystem, const char* pathID) {
    std::lock_guard<std::mutex> lock(g_progressiveCacheMutex);
    if (g_progressiveCache.find(wavName) != g_progressiveCache.end()) {
        return true; // Already opened/cached.
    }

    std::vector<unsigned char> fileData = ReadEntireFileIntoMem(filesystem, wavName.c_str(), pathID);
    if (fileData.empty())
        return false;

    // Check header ("OggS"). Both Opus and Vorbis in Ogg containers use this.
    if (!(fileData.size() >= 4 && std::memcmp(fileData.data(), "OggS", 4) == 0))
        return false;

    // Create a progressive template.
    auto progTemplate = std::make_shared<ProgressiveOpusTemplate>();
    progTemplate->fileData = std::move(fileData);

    // Create a persistent MemoryBuffer and store it in the template.
    progTemplate->pMemBuffer = std::make_shared<MemoryBuffer>();
    progTemplate->pMemBuffer->data = progTemplate->fileData.data();
    progTemplate->pMemBuffer->size = progTemplate->fileData.size();
    progTemplate->pMemBuffer->pos = 0;

    // Prepare the callbacks.
    ov_callbacks callbacks;
    callbacks.read_func = MemoryReadCallback;
    callbacks.seek_func = MemorySeekCallback;
    callbacks.close_func = MemoryCloseCallback;
    callbacks.tell_func = MemoryTellCallback;

    // Allocate an OggVorbis_File on the heap so that it can be shared.
    OggVorbis_File* vf = new OggVorbis_File;
    int result = ov_open_callbacks(progTemplate->pMemBuffer.get(), vf, nullptr, progTemplate->fileData.size(), callbacks);
    if (result < 0) {
        Warning("[Ogg] ov_open_callbacks failed with error %d for file %s\n", result, wavName.c_str());
        delete vf;
        return false;
    }

    vorbis_info* vi = ov_info(vf, -1);
    if (!vi) {
        ov_clear(vf);
        delete vf;
        Warning("[Ogg] ov_info failed for file %s\n", wavName.c_str());
        return false;
    }
    int channels = vi->channels;
    //Msg("[Ogg] Opened ogg file '%s': channels=%d\n", wavName.c_str(), channels);

    progTemplate->channels = channels;
    // Store the shared decoder in the template.
    progTemplate->vf = vf;

    // Increase the initial buffer by decoding more chunks synchronously.
    const size_t INITIAL_CHUNKS = 8; // increased from 4
    for (size_t i = 0; i < INITIAL_CHUNKS; i++) {
        std::vector<int16_t> chunk;
        {
            std::lock_guard<std::mutex> decodeLock(progTemplate->decodeMutex);
            chunk = DecodeChunk(progTemplate->vf, channels);
        }
        if (chunk.empty()) {
            progTemplate->fullyDecoded = true;
            break;
        }
        progTemplate->pcmChunks.push_back(std::move(chunk));
        progTemplate->decodedChunks.fetch_add(1, std::memory_order_relaxed);
    }

    // Start an asynchronous worker thread to decode the remaining chunks.
    std::thread bgThread(BackgroundDecodeThread, progTemplate, channels);
    bgThread.detach();

    // Save the progressive template in the global cache.
    g_progressiveCache[wavName] = progTemplate;
    return true;
}

// --------------------------------------------------------------------
// Create a new playback instance from the cached progressive data.
// Each instance maintains its own read pointer (in absolute sample index) into the decoded PCM.
// --------------------------------------------------------------------
static OpusPlaybackInstance* CreateOpusTrackInstance(const std::string& filename,
    CBaseFileSystem* filesystem,
    const char* pathID)
{
    std::shared_ptr<ProgressiveOpusTemplate> progTemplate;
    {
        std::lock_guard<std::mutex> lock(g_progressiveCacheMutex);
        auto it = g_progressiveCache.find(filename);
        if (it == g_progressiveCache.end()) {
            if (!OpenOpusContext(filename, filesystem, pathID))
                return nullptr;
            it = g_progressiveCache.find(filename);
            if (it == g_progressiveCache.end())
                return nullptr;
        }
        progTemplate = it->second;
    }

    OpusPlaybackInstance* instance = new OpusPlaybackInstance();
    instance->progTemplate = progTemplate;
    instance->channels = progTemplate->channels;
    instance->readSampleIndex = 0;
    return instance;
}

// --------------------------------------------------------------------
// Helper: Compute total number of decoded samples available so far.
// --------------------------------------------------------------------
static size_t GetTotalAvailableSamples(std::shared_ptr<ProgressiveOpusTemplate> progTemplate) {
    size_t total = 0;
    std::lock_guard<std::mutex> lock(progTemplate->chunkMutex);
    for (const auto& chunk : progTemplate->pcmChunks) {
        total += chunk.size();
    }
    return total;
}

// --------------------------------------------------------------------
// Helper: Copy samples from the progressive template to the destination buffer,
// starting at the given absolute sample index.
// --------------------------------------------------------------------
static void CopySamples(std::shared_ptr<ProgressiveOpusTemplate> progTemplate, size_t startSample, int16_t* dest, size_t samplesToCopy) {
    size_t copied = 0;
    std::lock_guard<std::mutex> lock(progTemplate->chunkMutex);
    for (const auto& chunk : progTemplate->pcmChunks) {
        if (startSample >= chunk.size()) {
            startSample -= chunk.size();
            continue;
        }
        // Determine how many samples we can copy from this chunk.
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
__int64 HandleOpusRead(CBaseFileSystem* filesystem, FileAsyncRequest_t* request)
{
    OpusPlaybackInstance* instance = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_playbackInstancesMutex);
        auto it = g_playbackInstances.find(request);
        if (it != g_playbackInstances.end())
            instance = it->second;
    }
    // If no instance exists for this request, create one.
    if (!instance) {
        instance = CreateOpusTrackInstance(request->pszFilename, filesystem, request->pszPathID);
        if (!instance) {
            Warning("[Ogg] Failed to create playback instance for '%s'\n", request->pszFilename);
            return FSASYNC_ERR_FILEOPEN;
        }
        {
            std::lock_guard<std::mutex> lock(g_playbackInstancesMutex);
            g_playbackInstances[request] = instance;
        }
    }

    int channels = instance->channels;
    size_t bytesPerSample = sizeof(int16_t);
    size_t bytesPerFrame = bytesPerSample * channels;
    size_t framesRequested = static_cast<size_t>(request->nBytes) / bytesPerFrame;
    size_t samplesRequested = framesRequested * channels;

    // Preserve offset skipping logic.
    {
        std::lock_guard<std::recursive_mutex> lock(*instance->bufferMutex);
        if (request->nOffset > 0) {
            size_t skipBytes = static_cast<size_t>(request->nOffset);
            size_t skipFrames = skipBytes / bytesPerFrame;
            instance->readSampleIndex = skipFrames * channels;
            size_t totalSamples = GetTotalAvailableSamples(instance->progTemplate);
            if (instance->readSampleIndex > totalSamples) {
                instance->readSampleIndex = totalSamples;
            }
        }
    }

    // Allocate (or use) the destination buffer.
    void* pDest = request->pData;
    if (!pDest) {
        if (request->pfnAlloc)
            pDest = request->pfnAlloc(request->pszFilename, request->nBytes);
        else
            pDest = malloc(request->nBytes);
    }
    // Zero–initialize destination buffer so missing samples are silent.
    memset(pDest, 0, request->nBytes);

    // Ensure that the progressive template has decoded enough samples.
    {
        auto progTemplate = instance->progTemplate;
        size_t availableSamples = GetTotalAvailableSamples(progTemplate);
        // If insufficient samples are buffered, synchronously decode additional chunks immediately.
        while (availableSamples < instance->readSampleIndex + samplesRequested && !progTemplate->fullyDecoded) {
            std::vector<int16_t> newChunk;
            {
                std::lock_guard<std::mutex> decodeLock(progTemplate->decodeMutex);
                // Check if the decoder has already been cleaned up.
                if (progTemplate->vf == nullptr) {
                    progTemplate->fullyDecoded = true;
                    break;
                }
                newChunk = DecodeChunk(progTemplate->vf, progTemplate->channels);
            }
            if (!newChunk.empty()) {
                std::lock_guard<std::mutex> lock(progTemplate->chunkMutex);
                progTemplate->pcmChunks.push_back(std::move(newChunk));
                progTemplate->decodedChunks.fetch_add(1, std::memory_order_relaxed);
                progTemplate->chunkCV.notify_all();
            }
            else {
                progTemplate->fullyDecoded = true;
                break;
            }
            availableSamples = GetTotalAvailableSamples(progTemplate);
        }
    }


    // Copy the available samples into the destination buffer.
    {
        std::lock_guard<std::recursive_mutex> lock(*instance->bufferMutex);
        size_t availableSamples = GetTotalAvailableSamples(instance->progTemplate);
        size_t samplesToCopy = std::min(samplesRequested,
            availableSamples > instance->readSampleIndex ? availableSamples - instance->readSampleIndex : 0);
        if (samplesToCopy > 0) {
            CopySamples(instance->progTemplate, instance->readSampleIndex, reinterpret_cast<int16_t*>(pDest), samplesToCopy);
            instance->readSampleIndex += samplesToCopy;
        }
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
    if (!filename) return false;
    std::string fn(filename);
    if (fn.size() < 4) return false;
    auto ext = fn.substr(fn.size() - 4);
    for (auto& c : ext)
        c = tolower((unsigned char)c);
    return (ext == ".wav");
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
        // Save the original pointer.
        const char* originalFilename = request->pszFilename;

        // Create a modifiable copy.
        std::string modifiedFilename(request->pszFilename);
        modifiedFilename.replace(modifiedFilename.size() - 4, 4, ".ogg");

        // Rewrite the filename pointer.
        request->pszFilename = modifiedFilename.c_str();

        __int64 result = FSASYNC_ERR_FILEOPEN;
        if (OpenOpusContext(request->pszFilename, filesystem, request->pszPathID)) {
            result = HandleOpusRead(filesystem, request);
        }

        // Restore the original filename pointer before returning.
        request->pszFilename = originalFilename;

        // If our custom handler did not return an error, return its result.
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
        std::lock_guard<std::mutex> lock(g_playbackInstancesMutex);
        auto it = g_playbackInstances.find((FileAsyncRequest_t*)(((__int64)thisptr) + 0x70));
        if (it != g_playbackInstances.end()) {
            delete it->second;
            g_playbackInstances.erase(it);
        }
    }
    return Original_CFileAsyncReadJob_dtor(thisptr);
}
