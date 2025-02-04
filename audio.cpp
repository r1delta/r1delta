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
#include <emmintrin.h>    // SSE2
#include <xmmintrin.h>    // SSE

#include <opus/opus.h>
#include <opus/opusfile.h>
#include <samplerate.h>

#include "load.h"
#include "logging.h"
#include "audio.h"

// --------------------------------------------------------------------
// Core filesystem enums/structs (from your snippet)
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
    void* pContext;
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

// Thread priority stubs
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

// Critical section helpers
static void EnterCriticalSection_(LPCRITICAL_SECTION cs) {
    ::EnterCriticalSection(cs);
}

static void LeaveCriticalSection_(LPCRITICAL_SECTION cs) {
    ::LeaveCriticalSection(cs);
}

// --------------------------------------------------------------------
// Core filesystem vtable-based function pointers (from your snippet)
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

// --------------------------------------------------------------------
// Helpers from your snippet
// --------------------------------------------------------------------
static bool EndsWithWav(const char* filename) {
    if (!filename) return false;
    std::string fn(filename);
    if (fn.size() < 4) return false;
    auto ext = fn.substr(fn.size() - 4);
    for (auto& c : ext) c = tolower((unsigned char)c);
    return (ext == ".wav");
}

// This version of MakeOpusFilename returns a single opus filename (without a track index)
static std::string MakeOpusFilename(const char* wavFile) {
    std::string fn(wavFile);
    return fn.substr(0, fn.size() - 4) + ".opus";
}

// --------------------------------------------------------------------
// Function pointers (from your snippet)
// --------------------------------------------------------------------
static void(__fastcall* pFreeAsyncFileHandle)(std::uint64_t);
static std::uint64_t(__fastcall* pGetOptimalReadSize)(CBaseFileSystem*, void*, std::uint64_t);
static void(__fastcall* pPerformAsyncCallback)(CBaseFileSystem*, FileAsyncRequest_t*, void*, std::uint64_t, std::uint64_t);
static void(__fastcall* pReleaseAsyncOpenedFiles)(CRITICAL_SECTION*, void*);
static void(__fastcall* pLogFileAccess)(CBaseFileSystem*, const char*, const char*, void*);

static CRITICAL_SECTION* pAsyncOpenedFilesCriticalSection;
static std::uint64_t pAsyncOpenedFilesTable;
static void* pFileAccessLoggingPointer;

// --------------------------------------------------------------------
// Original read handler (unchanged from your snippet)
// --------------------------------------------------------------------
__int64 HandleOriginalRead(CBaseFileSystem* filesystem, FileAsyncRequest_t* request)
{
    AsyncOpenedFile_t* pHeldFile = nullptr;
    if (request->hSpecificAsyncFile != (void*)FS_INVALID_ASYNC_FILE) {
        EnterCriticalSection_(pAsyncOpenedFilesCriticalSection);
        std::uint64_t entryBase = pAsyncOpenedFilesTable + (24ULL * ((__int64)request->hSpecificAsyncFile & 0xFFFF));
        std::uint64_t ptrStruct = *reinterpret_cast<std::uint64_t*>(entryBase + 16ULL);
        if (ptrStruct) {
            _InterlockedIncrement(reinterpret_cast<volatile long*>(ptrStruct + 8ULL));
            pHeldFile = reinterpret_cast<AsyncOpenedFile_t*>(ptrStruct);
        }
        LeaveCriticalSection_(pAsyncOpenedFilesCriticalSection);
    }

    FileHandle_t hFile = pHeldFile ? *reinterpret_cast<FileHandle_t*>(reinterpret_cast<std::uint64_t>(pHeldFile) + 16) : nullptr;
    if (!pHeldFile || !hFile) {
        hFile = FSOperations::OpenFile(filesystem, request->pszFilename, "rb", 0, request->pszPathID, 0);
        if (pHeldFile) {
            *reinterpret_cast<FileHandle_t*>(reinterpret_cast<std::uint64_t>(pHeldFile) + 16) = hFile;
        }
    }

    __int64 originalResult;
    if (!hFile) {
        // File open error
        if (request->pfnCallback) {
            auto criticalSection = reinterpret_cast<LPCRITICAL_SECTION>(reinterpret_cast<std::uint64_t>(filesystem) + 408);
            EnterCriticalSection_(criticalSection);
            request->pfnCallback(request, 0, 0xFFFFFFFF);
            LeaveCriticalSection_(criticalSection);
        }
        originalResult = FSASYNC_ERR_FILEOPEN;
    }
    else {
        // Calculate read size
        int nBytesToRead = request->nBytes
            ? static_cast<int>(request->nBytes)
            : static_cast<int>(FSOperations::GetFileSize(filesystem, hFile) - request->nOffset);
        if (nBytesToRead < 0) nBytesToRead = 0;

        // Prepare buffer
        void* pDest;
        int nBytesBuffer;
        if (request->pData) {
            pDest = request->pData;
            nBytesBuffer = nBytesToRead;
        }
        else {
            nBytesBuffer = nBytesToRead + ((request->flags & FSASYNC_FLAGS_NULLTERMINATE) ? 1 : 0);
            std::uint64_t alignment = 0;
            if (FSOperations::GetOptimalIO(filesystem, hFile, &alignment, 0, 0)
                && (request->nOffset % alignment == 0))
            {
                nBytesBuffer = static_cast<int>(pGetOptimalReadSize(filesystem, hFile, nBytesBuffer));
            }
            if (request->pfnAlloc) {
                pDest = request->pfnAlloc(request->pszFilename, nBytesBuffer);
            }
            else {
                pDest = FSOperations::AllocOptimalReadBuffer(filesystem, hFile, nBytesBuffer, request->nOffset);
            }
        }

        if (request->nOffset > 0) {
            FSOperations::SeekFile(filesystem, hFile, request->nOffset, 0);
        }

        // Bump thread priority
        int oldPriority = ThreadGetPriority(0);
        if (oldPriority < 2)
            ThreadSetPriority(0, 2);

        __int64 nBytesRead = FSOperations::ReadFile(filesystem, pDest, nBytesToRead, nBytesToRead, hFile);

        // Restore thread priority
        if (oldPriority < 2)
            ThreadSetPriority(0, oldPriority);

        if (!pHeldFile) {
            FSOperations::CloseFile(filesystem, hFile);
        }

        if (request->flags & FSASYNC_FLAGS_NULLTERMINATE) {
            reinterpret_cast<char*>(pDest)[nBytesRead] = '\0';
        }

        if ((nBytesRead == 0) && (nBytesToRead != 0)) {
            Warning("SyncRead failed: file='%s' nBytesRead=0 nBytesToRead=%lld\n",
                request->pszFilename, nBytesToRead);
            originalResult = FSASYNC_ERR_READING;
        }
        else {
            originalResult = FSASYNC_OK;
        }

        int finalBytes = (nBytesRead < nBytesToRead) ? static_cast<int>(nBytesRead) : nBytesToRead;
        pPerformAsyncCallback(filesystem, request, pDest, finalBytes, originalResult);
    }

    if (pHeldFile) {
        pReleaseAsyncOpenedFiles(pAsyncOpenedFilesCriticalSection, request->hSpecificAsyncFile);
    }

    const char* statusStr = "UNKNOWN";
    switch (originalResult) {
    case FSASYNC_OK:           statusStr = "OK";            break;
    case FSASYNC_ERR_FILEOPEN: statusStr = "ERR_FILEOPEN";  break;
    case FSASYNC_ERR_READING:  statusStr = "ERR_READING";   break;
    }
    /* if (g_pLogAudio->m_Value.m_nValue == 1) {
         Msg("SyncRead: file='%s' offset=%lld bytes=%lld status=%s\n",
             request->pszFilename, request->nOffset, request->nBytes, statusStr);
     }*/
    return originalResult;
}

// --------------------------------------------------------------------
// Now, implement .opus streaming decode (single opus file with multi‐channels)
// --------------------------------------------------------------------

struct OpusContext {
    // In this single‐file version we use only one track.
    struct Track {
        std::vector<int16_t> pcmData;           // Decoded PCM data (already interleaved)
        std::vector<unsigned char> opusFileData;  // The complete opus file in memory
        OggOpusFile* opusFile = nullptr;          // Handle returned by op_open_memory()
        int64_t currentDecodePos = 0;             // Bytes decoded so far (pcmData.size()*sizeof(int16_t))
        bool reachedEnd = false;
        int channels = 1;
    };

    std::vector<Track> tracks;
    bool skipDiscovered = false;
    int64_t skipOffset = 0;
};

static std::unordered_map<std::string, OpusContext> g_opusCache;
static std::mutex g_opusCacheMutex;

// Helper: read entire file via our FSOperations
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
        // partial read or error
        fileData.clear();
    }
    return fileData;
}

// Simple fixed-ratio 48kHz -> 44.1kHz resampler
#ifdef USE_LIBSAMPLERATE
static bool FixedResample48to441(const float* input, float* output, int inputFrames) {
    SRC_DATA srcData = {};
    srcData.data_in = input;
    srcData.data_out = output;
    srcData.input_frames = inputFrames;
    srcData.output_frames = int(inputFrames * 0.91875f);
    srcData.src_ratio = 0.91875; // 44100/48000

    int error = src_simple(&srcData, SRC_ZERO_ORDER_HOLD, 2); // 2 channels
    return (error == 0);
}
#else
static void FixedResample48to441(const float* input, float* output, int inputFrames) {
    const float ratio = 0.91875f;  // 44100/48000
    const float step = 1.0f / ratio;
    const int outputFrames = int(inputFrames * ratio);

#if __AVX2__
    // Process 8 output frames at a time with AVX2 (omitted for brevity)
#else
    // Process 4 output frames at a time with SSE3 (omitted for brevity)
    float pos = 0.0f;
    for (int i = 0; i < outputFrames; i++) {
        int idx = (int)pos;
        float frac = pos - idx;
        output[i * 2] = input[idx * 2] * (1 - frac) + input[(idx + 1) * 2] * frac;
        output[i * 2 + 1] = input[idx * 2 + 1] * (1 - frac) + input[(idx + 1) * 2 + 1] * frac;
        pos += step;
    }
#endif
}
#endif

// SSE2 resampler for any number of channels
static void FixedResample48to441MultiChannel(const float* input, float* output,
    int inputFrames, int channels) {
    const float ratio = 0.91875f;  // 44100/48000
    const float step = 1.0f / ratio;
    const int outputFrames = int(inputFrames * ratio);

    // Process 4 samples at a time with SSE2
    const __m128 vStep = _mm_set1_ps(step);
    const __m128 vOne = _mm_set1_ps(1.0f);
    __m128 vPos = _mm_set_ps(3.0f, 2.0f, 1.0f, 0.0f);
    vPos = _mm_mul_ps(vPos, vStep);

    int i = 0;
    for (; i < outputFrames - 3; i += 4) {
        __m128i vIdx = _mm_cvttps_epi32(vPos);
        __m128 vFrac = _mm_sub_ps(vPos, _mm_cvtepi32_ps(vIdx));

        // Store indices
        int indices[4];
        _mm_store_si128((__m128i*)indices, vIdx);

        // Process each channel
        for (int ch = 0; ch < channels; ch++) {
            // Load samples for this channel
            float temp[4], tempNext[4];
            for (int j = 0; j < 4; j++) {
                temp[j] = input[indices[j] * channels + ch];
                tempNext[j] = input[(indices[j] + 1) * channels + ch];
            }

            __m128 vSamp = _mm_load_ps(temp);
            __m128 vNext = _mm_load_ps(tempNext);

            // Linear interpolation
            __m128 vOneMinusFrac = _mm_sub_ps(vOne, vFrac);
            __m128 vOut = _mm_add_ps(
                _mm_mul_ps(vSamp, vOneMinusFrac),
                _mm_mul_ps(vNext, vFrac)
            );

            // Store results
            float result[4];
            _mm_store_ps(result, vOut);
            for (int j = 0; j < 4; j++) {
                output[i * channels + j * channels + ch] = result[j];
            }
        }

        vPos = _mm_add_ps(vPos, _mm_mul_ps(vStep, _mm_set1_ps(4.0f)));
    }

    // Handle remaining frames
    float pos = _mm_cvtss_f32(vPos);
    for (; i < outputFrames; i++) {
        int idx = (int)pos;
        float frac = pos - idx;
        for (int ch = 0; ch < channels; ch++) {
            float samp0 = input[idx * channels + ch];
            float samp1 = input[(idx + 1) * channels + ch];
            output[i * channels + ch] = samp0 * (1 - frac) + samp1 * frac;
        }
        pos += step;
    }
}

// SSE2 float to int16 converter for any number of channels
static void FloatToInt16MultiChannel(const float* in, int16_t* out,
    size_t frames, int channels) {
    const __m128 scale = _mm_set1_ps(32767.0f);
    const size_t totalSamples = frames * channels;

    size_t i = 0;
    for (; i < totalSamples - 7; i += 8) {
        // Load 8 floats (2 registers worth)
        __m128 in1 = _mm_load_ps(&in[i]);
        __m128 in2 = _mm_load_ps(&in[i + 4]);

        // Scale to int16 range
        in1 = _mm_mul_ps(in1, scale);
        in2 = _mm_mul_ps(in2, scale);

        // Convert to int32
        __m128i i1 = _mm_cvtps_epi32(in1);
        __m128i i2 = _mm_cvtps_epi32(in2);

        // Pack to int16
        __m128i shorts = _mm_packs_epi32(i1, i2);

        // Store result
        _mm_store_si128((__m128i*) & out[i], shorts);
    }

    // Handle remaining samples
    for (; i < totalSamples; i++) {
        float sample = in[i] * 32767.0f;
        if (sample > 32767.f) sample = 32767.f;
        if (sample < -32768.f) sample = -32768.f;
        out[i] = (int16_t)sample;
    }
}

// Decode enough data so that the decoded PCM buffer holds at least the requested range.
// In this single-file version we decode from the single track and use its 'channels' field.
static bool DecodeOpusChunk(OpusContext& ctx, int64_t offset, size_t bytesNeeded) {
    if (ctx.tracks.empty()) return false;
    auto& track = ctx.tracks[0];
    const size_t bytesPerSample = sizeof(int16_t) * track.channels;
    const size_t samplesNeeded = bytesNeeded / bytesPerSample;
    size_t trackOffset = offset / bytesPerSample;
    size_t trackBytesNeeded = samplesNeeded * sizeof(int16_t);

    // If we already have enough decoded data, nothing more to do.
    if (track.pcmData.size() * sizeof(int16_t) >= trackOffset * sizeof(int16_t) + trackBytesNeeded)
        return true;

    const size_t DECODE_CHUNK = 5760;
    while (!track.reachedEnd &&
        (track.pcmData.size() * sizeof(int16_t) < trackOffset * sizeof(int16_t) + trackBytesNeeded))
    {
        std::vector<float> decodeBuf(DECODE_CHUNK * track.channels);
        int framesDecoded = op_read_float(track.opusFile,
            decodeBuf.data(),
            (int)decodeBuf.size(),
            nullptr);

        if (framesDecoded <= 0) {
            track.reachedEnd = true;
            if (framesDecoded < 0) {
                Msg("[Opus] decode error: %d\n", framesDecoded);
                return false;
            }
            break;
        }

        size_t oldSize = track.pcmData.size();
        track.pcmData.resize(oldSize + framesDecoded * track.channels);

        FloatToInt16MultiChannel(decodeBuf.data(),
            &track.pcmData[oldSize],
            framesDecoded,
            track.channels);

        track.currentDecodePos = track.pcmData.size() * sizeof(int16_t);
    }
    return true;
}

// Open the opus context for the given wav file by looking for a single matching .opus file.
static bool OpenOpusContext(
    const std::string& wavName,
    CBaseFileSystem* filesystem,
    const char* pathID)
{
    std::lock_guard<std::mutex> lock(g_opusCacheMutex);
    if (g_opusCache.find(wavName) != g_opusCache.end()) {
        return true;
    }

    OpusContext ctx;

    std::string opusName = MakeOpusFilename(wavName.c_str());
    auto fileData = ReadEntireFileIntoMem(filesystem, opusName.c_str(), pathID);
    if (fileData.empty()) {
        return false;
    }

    OpusContext::Track newTrack;
    newTrack.opusFileData = std::move(fileData);

    int error = 0;
    newTrack.opusFile = op_open_memory(
        newTrack.opusFileData.data(),
        newTrack.opusFileData.size(),
        &error);

    if (!newTrack.opusFile || error != 0) {
        op_free(newTrack.opusFile);
        return false;
    }

    const OpusHead* head = op_head(newTrack.opusFile, -1);
    if (!head) {
        op_free(newTrack.opusFile);
        return false;
    }

    newTrack.channels = head->channel_count;
    Msg("[Opus] Opened opus file '%s': channels=%d\n", opusName.c_str(), head->channel_count);

    ctx.tracks.push_back(std::move(newTrack));
    // Reserve some space (adjust as needed)
    ctx.tracks[0].pcmData.reserve(1024 * ctx.tracks[0].channels);

    g_opusCache[wavName] = std::move(ctx);
    return true;
}

// This function handles the read request by decoding data from the single opus file.
__int64 HandleOpusRead(CBaseFileSystem* filesystem, FileAsyncRequest_t* request)
{
    // --------------------------------------------------
    // 1. Acquire pHeldFile if the engine gave us hSpecificAsyncFile
    //    (so partial reads reuse the same handle)
    // --------------------------------------------------
    AsyncOpenedFile_t* pHeldFile = nullptr;
    if (request->hSpecificAsyncFile != (void*)FS_INVALID_ASYNC_FILE)
    {
        EnterCriticalSection_(pAsyncOpenedFilesCriticalSection);
        std::uint64_t entryBase = pAsyncOpenedFilesTable
            + (24ULL * (static_cast<std::uint64_t>((__int64)request->hSpecificAsyncFile) & 0xFFFF));
        std::uint64_t ptrStruct = *reinterpret_cast<std::uint64_t*>(entryBase + 16ULL);
        if (ptrStruct)
        {
            _InterlockedIncrement(reinterpret_cast<volatile long*>(ptrStruct + 8ULL));
            pHeldFile = reinterpret_cast<AsyncOpenedFile_t*>(ptrStruct);
        }
        LeaveCriticalSection_(pAsyncOpenedFilesCriticalSection);
    }

    // We won't actually do file I/O from pHeldFile, but some
    // games rely on it to track partial offsets. So let's at least
    // store and restore it if needed:
    FileHandle_t hFile = pHeldFile
        ? *reinterpret_cast<FileHandle_t*>(reinterpret_cast<std::uint64_t>(pHeldFile) + 16)
        : nullptr;

    // --------------------------------------------------
    // 2. Bump thread priority (like the original read does)
    // --------------------------------------------------
    int oldPriority = ThreadGetPriority(0);
    if (oldPriority < 2) {
        ThreadSetPriority(0, 2);
    }

    // --------------------------------------------------
    // 3. Decode from our global opus cache
    // --------------------------------------------------
    std::unique_lock<std::mutex> lock(g_opusCacheMutex);

    // Make sure we have an OpusContext
    auto it = g_opusCache.find(request->pszFilename);
    if (it == g_opusCache.end()) {
        // Must open it first
        if (!OpenOpusContext(request->pszFilename, filesystem, request->pszPathID)) {
            lock.unlock();
            // Failed to open .opus? Fall back to original read:
            // We'll do that by returning a sentinel, so the caller can call HandleOriginalRead.
            return -99999; // Some sentinel that we'll check for.
        }
        it = g_opusCache.find(request->pszFilename);
        if (it == g_opusCache.end() || it->second.tracks.empty()) {
            lock.unlock();
            // No valid track after opening => fallback
            return -99999;
        }
    }

    OpusContext& ctx = it->second;
    if (ctx.tracks.empty()) {
        lock.unlock();
        return -99999; // fallback
    }
    auto& track = ctx.tracks[0];

    // We do not do special skipOffset logic here unless you need it.
    // Just treat nOffset as the position in the decoded PCM data:
    const size_t bytesPerSample = sizeof(int16_t) * track.channels;
    int64_t fileOffset = request->nOffset; // simplest approach
    if (fileOffset < 0) {
        fileOffset = 0; // can't do negative
    }

    // The game wants nBytes from offset => decode enough to fill that range.
    // E.g. if offset=10000, nBytes=4096, we want samples covering [10000..(10000+4096)).
    if (!DecodeOpusChunk(ctx, fileOffset, request->nBytes)) {
        lock.unlock();
        if (request->pfnCallback) {
            // Error
            request->pfnCallback(request, 0, FSASYNC_ERR_READING);
        }
      //  goto OpusReadDoneErr;
    }

    // How many samples are actually available from offset -> end of decode?
    size_t trackOffsetSamples = static_cast<size_t>(fileOffset / bytesPerSample);
    size_t totalDecodedSamples = track.pcmData.size() / track.channels; // track is interleaved
    size_t availableSamples = (totalDecodedSamples > trackOffsetSamples)
        ? (totalDecodedSamples - trackOffsetSamples)
        : 0;

    // The user wants this many *bytes*, i.e. (samplesNeeded = nBytes / bytesPerSample)
    size_t samplesNeeded = static_cast<size_t>(request->nBytes / bytesPerSample);
    size_t samplesToCopy = std::min(availableSamples, samplesNeeded);
    size_t bytesToCopy = samplesToCopy * bytesPerSample;

    // --------------------------------------------------
    // 4. Prepare output buffer (like original code)
    // --------------------------------------------------
    void* pDest = nullptr;
    if (request->pData) {
        pDest = request->pData;
    }
    else {
        // We might want to respect alignment. For brevity, skip that:
        if (request->pfnAlloc) {
            pDest = request->pfnAlloc(request->pszFilename, request->nBytes);
        }
        else {
            pDest = malloc(request->nBytes);
        }
    }

    // Copy the data
    if (bytesToCopy > 0) {
        const int16_t* srcPtr = &track.pcmData[trackOffsetSamples * track.channels];
        memcpy(pDest, srcPtr, bytesToCopy);
    }
    // If we have fewer samples than requested => zero out the remainder
    if (bytesToCopy < static_cast<size_t>(request->nBytes)) {
        memset((char*)pDest + bytesToCopy, 0, request->nBytes - bytesToCopy);
    }

    // Null‐terminate if flags say so
    if (request->flags & FSASYNC_FLAGS_NULLTERMINATE) {
        // The original code sets: dest[nBytesRead] = '\0'
        // We'll do the same. Here finalBytes = bytesToCopy
        reinterpret_cast<char*>(pDest)[bytesToCopy] = '\0';
    }

    lock.unlock(); // done with the opus context

    // --------------------------------------------------
    // 5. Check finalBytes vs. requested
    // --------------------------------------------------
    __int64 finalBytes = static_cast<__int64>(bytesToCopy);
    __int64 resultStatus = FSASYNC_OK;
    if ((finalBytes == 0) && (request->nBytes != 0)) {
        // The original read logic calls FSASYNC_ERR_READING if read=0 but we wanted >0
        resultStatus = FSASYNC_ERR_READING;
        Warning("[Opus] decode returned 0 bytes for '%s'\n", request->pszFilename);
    }

    // --------------------------------------------------
    // 6. Call the async callback with the actual finalBytes
    // --------------------------------------------------
    pPerformAsyncCallback(filesystem, request, pDest, finalBytes, resultStatus);

    // Thread priority restore
    if (oldPriority < 2) {
        ThreadSetPriority(0, oldPriority);
    }

    // Release hold on the file if we had one
    if (pHeldFile) {
        pReleaseAsyncOpenedFiles(pAsyncOpenedFilesCriticalSection, request->hSpecificAsyncFile);
    }

    return resultStatus;

//OpusReadDoneErr:
    // Something went wrong, but still do the priority restore + release
    if (oldPriority < 2) {
        ThreadSetPriority(0, oldPriority);
    }
    if (pHeldFile) {
        pReleaseAsyncOpenedFiles(pAsyncOpenedFilesCriticalSection, request->hSpecificAsyncFile);
    }
    return FSASYNC_ERR_READING;
}

// --------------------------------------------------------------------
// The main hooked function
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
        // Fill in function pointers (example addresses; yours may differ)
        pFreeAsyncFileHandle = reinterpret_cast<void(__fastcall*)(std::uint64_t)>(G_filesystem_stdio + 0x38610);
        pGetOptimalReadSize = reinterpret_cast<std::uint64_t(__fastcall*)(CBaseFileSystem*, void*, std::uint64_t)>(G_filesystem_stdio + 0x4B70);
        pPerformAsyncCallback = reinterpret_cast<void(__fastcall*)(CBaseFileSystem*, FileAsyncRequest_t*, void*, std::uint64_t, std::uint64_t)>(G_filesystem_stdio + 0x1F200);
        pReleaseAsyncOpenedFiles = reinterpret_cast<void(__fastcall*)(CRITICAL_SECTION*, void*)>(G_filesystem_stdio + 0x233C0);
        pLogFileAccess = reinterpret_cast<void(__fastcall*)(CBaseFileSystem*, const char*, const char*, void*)>(G_filesystem_stdio + 0x9A40);
        pAsyncOpenedFilesCriticalSection = reinterpret_cast<CRITICAL_SECTION*>(G_filesystem_stdio + 0xF90A0);
        pAsyncOpenedFilesTable = G_filesystem_stdio + 0xF90D0;
        pFileAccessLoggingPointer = reinterpret_cast<void*>(G_filesystem_stdio + 0xCDBC0);
    }

    // Validate offsets
    if (request->nBytes < 0 || request->nOffset < 0) {
        if (request->pfnCallback) {
            request->pfnCallback(request, 0, FSASYNC_ERR_FILEOPEN);
        }
        return FSASYNC_ERR_FILEOPEN;
    }
    // If ends with .wav => check for .opus
    if (EndsWithWav(request->pszFilename)) {
        std::string opusName = MakeOpusFilename(request->pszFilename);
        if (FSOperations::FileExists(filesystem, opusName.c_str(), request->pszPathID)) {
            // Attempt to do an Opus read:
            __int64 result = HandleOpusRead(filesystem, request);
            if (result != -99999) {
                // That means we handled it or failed inside
                return result;
            }
            // If result == -99999 => fallback to original
        }
    }


    // Fallback to original read (or simply call it for testing)
    return Original_CBaseFileSystem__SyncRead(filesystem, request);
}
