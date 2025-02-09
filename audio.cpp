// ogg_decoder.cpp
// Drop–in replacement for the Opus–based decoder that now decodes Ogg Vorbis files,
// with minimal cold–start improvements (larger decode buffer + pre-allocation).

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
struct MemoryBuffer {
    const unsigned char* data;
    size_t size;
    size_t pos;
};

static size_t MemoryReadCallback(void* ptr, size_t size, size_t nmemb, void* datasource) {
    MemoryBuffer* mem = (MemoryBuffer*)datasource;
    size_t bytesRequested = size * nmemb;
    size_t bytesAvailable = (mem->size - mem->pos);
    size_t bytesToRead = (bytesRequested < bytesAvailable) ? bytesRequested : bytesAvailable;
    memcpy(ptr, mem->data + mem->pos, bytesToRead);
    mem->pos += bytesToRead;
    // Return number of items read, in "chunks" of `size`.
    return bytesToRead / size;
}

static int MemorySeekCallback(void* datasource, ogg_int64_t offset, int whence) {
    MemoryBuffer* mem = (MemoryBuffer*)datasource;
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
    MemoryBuffer* mem = (MemoryBuffer*)datasource;
    return static_cast<long>(mem->pos);
}

// --------------------------------------------------------------------
// Global cache for fully decoded Ogg Vorbis file data ("template" track).
// (The names remain unchanged for drop–in compatibility.)
// --------------------------------------------------------------------
struct OpusTemplate {
    std::shared_ptr<std::vector<int16_t>> pcmBuffer; // Fully decoded PCM data
    int channels = 1;
};

static std::unordered_map<std::string, OpusTemplate> g_opusCache;
static std::mutex g_opusCacheMutex;

// --------------------------------------------------------------------
// Global map for per–request playback instances.
// --------------------------------------------------------------------
struct OpusPlaybackInstance {
    std::shared_ptr<std::vector<int16_t>> pcmBuffer;
    size_t readFrameIndex = 0;
    int channels = 1;
    std::unique_ptr<std::recursive_mutex> bufferMutex;
    OpusPlaybackInstance() : bufferMutex(std::make_unique<std::recursive_mutex>()) {}
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
// Fully decode an Ogg Vorbis file from memory into PCM data.
// This function uses ov_read() to decode directly to 16–bit PCM,
// with a larger decode buffer and pre–allocation to reduce overhead.
// --------------------------------------------------------------------
static bool DecodeOggFileFull(OggVorbis_File* vf, int channels, std::vector<int16_t>& outPCM)
{
    // Attempt to retrieve total PCM sample count so we can reserve upfront.
    // If it fails (some streams won't allow it), totalSamples < 0 => fallback.
    ogg_int64_t totalSamples = ov_pcm_total(vf, -1);
    if (totalSamples > 0) {
        // Reserve enough space so we rarely (or never) have to grow.
        outPCM.reserve(static_cast<size_t>(totalSamples) * channels);
    }

    // Use a larger decode buffer to reduce overhead from repeated small reads.
    const int DECODE_BUFFER_SIZE = 65536; // 64 KB
    char decodeBuffer[DECODE_BUFFER_SIZE];
    int bitStream = 0;

    // We'll decode until ov_read reports 0 or negative (end or error).
    while (true)
    {
        long bytesDecoded = ov_read(vf, decodeBuffer, DECODE_BUFFER_SIZE, 0, 2, 1, &bitStream);
        if (bytesDecoded <= 0)
            break;  // 0 => end of file, <0 => Vorbis error

        size_t samplesDecoded = static_cast<size_t>(bytesDecoded / sizeof(int16_t));
        size_t oldSize = outPCM.size();
        outPCM.resize(oldSize + samplesDecoded);
        std::memcpy(&outPCM[oldSize], decodeBuffer, bytesDecoded);
    }

    return true;
}

// --------------------------------------------------------------------
// Load an Ogg Vorbis file into the global cache (drop–in replacement for OpenOpusContext)
// Fully decode the file into PCM data, with larger decode buffer.
// --------------------------------------------------------------------
bool OpenOpusContext(const std::string& wavName, CBaseFileSystem* filesystem, const char* pathID)
{
    std::lock_guard<std::mutex> lock(g_opusCacheMutex);
    if (g_opusCache.find(wavName) != g_opusCache.end()) {
        return true; // Already opened/cached.
    }

    std::vector<unsigned char> fileData = ReadEntireFileIntoMem(filesystem, wavName.c_str(), pathID);
    if (fileData.empty())
        return false;

    // Check file header ("OggS"). Both Opus and Vorbis in Ogg containers use this.
    if (!IsOpusFile(fileData))
        return false;

    // Prepare a memory data source for the Vorbis decoder.
    MemoryBuffer memBuffer;
    memBuffer.data = fileData.data();
    memBuffer.size = fileData.size();
    memBuffer.pos = 0;

    ov_callbacks callbacks;
    callbacks.read_func = MemoryReadCallback;
    callbacks.seek_func = MemorySeekCallback;
    callbacks.close_func = MemoryCloseCallback;
    callbacks.tell_func = MemoryTellCallback;

    OggVorbis_File vf;
    int result = ov_open_callbacks(&memBuffer, &vf, nullptr, fileData.size(), callbacks);
    if (result < 0) {
        Warning("[Ogg] ov_open_callbacks failed with error %d for file %s\n", result, wavName.c_str());
        return false;
    }

    vorbis_info* vi = ov_info(&vf, -1);
    if (!vi) {
        ov_clear(&vf);
        Warning("[Ogg] ov_info failed for file %s\n", wavName.c_str());
        return false;
    }
    int channels = vi->channels;
    //Msg("[Ogg] Opened ogg file '%s': channels=%d\n", wavName.c_str(), channels);

    // Decode the entire file into a temporary buffer.
    std::vector<int16_t> decodedPCM;
    if (!DecodeOggFileFull(&vf, channels, decodedPCM)) {
        ov_clear(&vf);
        Warning("[Ogg] Failed to fully decode file %s\n", wavName.c_str());
        return false;
    }
    ov_clear(&vf);

    // Save the decoded PCM in the cache.
    OpusTemplate tmpl;
    tmpl.pcmBuffer = std::make_shared<std::vector<int16_t>>(std::move(decodedPCM));
    tmpl.channels = channels;
    g_opusCache[wavName] = std::move(tmpl);
    return true;
}

// --------------------------------------------------------------------
// Create a new playback instance from the cached data.
// Each instance maintains its own read pointer into the fully decoded PCM.
// --------------------------------------------------------------------
static OpusPlaybackInstance* CreateOpusTrackInstance(const std::string& filename,
    CBaseFileSystem* filesystem,
    const char* pathID)
{
    {
        std::lock_guard<std::mutex> lock(g_opusCacheMutex);
        if (g_opusCache.find(filename) == g_opusCache.end()) {
            if (!OpenOpusContext(filename, filesystem, pathID))
                return nullptr;
        }
    }

    OpusTemplate& tmpl = g_opusCache[filename];
    OpusPlaybackInstance* instance = new OpusPlaybackInstance();
    instance->pcmBuffer = tmpl.pcmBuffer; // Share the fully decoded PCM data.
    instance->channels = tmpl.channels;
    instance->readFrameIndex = 0;
    return instance;
}

// --------------------------------------------------------------------
// Free a playback instance.
// --------------------------------------------------------------------
static void ReleaseOpusPlaybackInstance(OpusPlaybackInstance* instance) {
    if (instance) {
        delete instance;
    }
}

// --------------------------------------------------------------------
// The main read handler (no sample–rate conversion).
// Uses the fully decoded PCM data from the cache.
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
    size_t bytesPerFrame = sizeof(int16_t) * channels;
    size_t framesRequested = static_cast<size_t>(request->nBytes) / bytesPerFrame;

    {
        // Preserve offset skipping logic.
        std::lock_guard<std::recursive_mutex> lock(*instance->bufferMutex);
        if (request->nOffset > 0) {
            size_t skipBytes = static_cast<size_t>(request->nOffset);
            size_t skipFrames = skipBytes / bytesPerFrame;
            instance->readFrameIndex = skipFrames;
            size_t totalFrames = instance->pcmBuffer->size() / channels;
            if (instance->readFrameIndex > totalFrames) {
                instance->readFrameIndex = totalFrames;
            }
        }
    }

    // Prepare a temporary buffer for the requested frames.
    std::vector<int16_t> finalPCM(framesRequested * channels, 0);
    size_t framesAvailable = 0;
    {
        std::lock_guard<std::recursive_mutex> lock(*instance->bufferMutex);
        size_t totalFrames = instance->pcmBuffer->size() / channels;
        if (instance->readFrameIndex < totalFrames) {
            framesAvailable = totalFrames - instance->readFrameIndex;
        }
        size_t framesToCopy = std::min(framesRequested, framesAvailable);

        // Copy from the fully decoded PCM buffer.
        const int16_t* src = instance->pcmBuffer->data() + instance->readFrameIndex * channels;
        memcpy(finalPCM.data(), src, framesToCopy * bytesPerFrame);

        // Advance the read pointer.
        instance->readFrameIndex += framesToCopy;
    }

    // Allocate or use the existing request->pData.
    void* pDest = request->pData;
    if (!pDest) {
        if (request->pfnAlloc)
            pDest = request->pfnAlloc(request->pszFilename, request->nBytes);
        else
            pDest = malloc(request->nBytes);
    }

    size_t bytesToCopy = finalPCM.size() * sizeof(int16_t);
    bytesToCopy = std::min(bytesToCopy, static_cast<size_t>(request->nBytes));
    memcpy(pDest, finalPCM.data(), bytesToCopy);

    // Zero–fill any remaining bytes.
    if (bytesToCopy < static_cast<size_t>(request->nBytes)) {
        memset((char*)pDest + bytesToCopy, 0, request->nBytes - bytesToCopy);
    }

    // Null–terminate if requested.
    if (request->flags & FSASYNC_FLAGS_NULLTERMINATE) {
        if (bytesToCopy < static_cast<size_t>(request->nBytes)) {
            reinterpret_cast<char*>(pDest)[bytesToCopy] = '\0';
        }
    }

    pPerformAsyncCallback(filesystem, request, pDest, bytesToCopy, FSASYNC_OK);
    return FSASYNC_OK;
}

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
// If the filename ends with ".wav", we rewrite it (e.g. to ".ogg")
// and use our custom handler. In all cases, we restore request->pszFilename
// before returning.
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
        // The offset (0x70) detail depends on how your original code tracks the request pointer.
        // Adjust if necessary:
        auto it = g_playbackInstances.find((FileAsyncRequest_t*)(((__int64)thisptr) + 0x70));
        if (it != g_playbackInstances.end()) {
            ReleaseOpusPlaybackInstance(it->second);
            g_playbackInstances.erase(it);
        }
    }
    return Original_CFileAsyncReadJob_dtor(thisptr);
}
