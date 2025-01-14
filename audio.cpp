#include <windows.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <filesystem>
#include <chrono>

#include <opus/opus.h>
#include <opus/opusfile.h>
#include <samplerate.h>

#include "load.h"
#include "logging.h"

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
        auto result = reinterpret_cast<__int64(__fastcall*)(CBaseFileSystem*, void*, std::uint64_t, std::uint64_t, void*)>(
            *reinterpret_cast<std::uint64_t*>(fs->vtable + 600LL)
            )(fs, dest, destSize, size, file);
        return result;
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
    Msg("SyncRead: file='%s' offset=%lld bytes=%lld status=%s\n",
        request->pszFilename, request->nOffset, request->nBytes, statusStr);

    return originalResult;
}

// --------------------------------------------------------------------
// Now, implement .opus streaming decode
// --------------------------------------------------------------------

struct OpusContext {
    std::vector<int16_t> pcmData;
    bool skipDiscovered = false;
    int64_t skipOffset = 0;

    // Streaming state
    std::vector<unsigned char> opusFileData;  // Keep the file data
    OggOpusFile* opusFile = nullptr;
    SRC_STATE* srcState = nullptr;
    int64_t currentDecodePos = 0;  // How many bytes of PCM we've decoded
    bool reachedEnd = false;
};

// We'll keep a global map from WAV filename => decoded opus context
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
//#define USE_LIBSAMPLERATE
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
    // Process 8 output frames at a time with AVX2
    const __m256 vStep = _mm256_set1_ps(step);
    const __m256 vOne = _mm256_set1_ps(1.0f);
    __m256 vPos = _mm256_set_ps(7.0f, 6.0f, 5.0f, 4.0f, 3.0f, 2.0f, 1.0f, 0.0f);
    vPos = _mm256_mul_ps(vPos, vStep);

    int i = 0;
    for (; i < outputFrames - 7; i += 8) {
        __m256i vIdx = _mm256_cvttps_epi32(vPos);
        __m256 vFrac = _mm256_sub_ps(vPos, _mm256_cvtepi32_ps(vIdx));

        __m128i vIdxLo = _mm256_extractf128_si256(vIdx, 0);
        __m128i vIdxHi = _mm256_extractf128_si256(vIdx, 1);
        int indices[8];
        indices[0] = _mm_extract_epi32(vIdxLo, 0);
        indices[1] = _mm_extract_epi32(vIdxLo, 1);
        indices[2] = _mm_extract_epi32(vIdxLo, 2);
        indices[3] = _mm_extract_epi32(vIdxLo, 3);
        indices[4] = _mm_extract_epi32(vIdxHi, 0);
        indices[5] = _mm_extract_epi32(vIdxHi, 1);
        indices[6] = _mm_extract_epi32(vIdxHi, 2);
        indices[7] = _mm_extract_epi32(vIdxHi, 3);

        float tempL[8], tempR[8], tempNextL[8], tempNextR[8];
        for (int j = 0; j < 8; j++) {
            tempL[j] = input[indices[j] * 2];
            tempR[j] = input[indices[j] * 2 + 1];
            tempNextL[j] = input[indices[j] * 2 + 2];
            tempNextR[j] = input[indices[j] * 2 + 3];
        }

        __m256 vL = _mm256_load_ps(tempL);
        __m256 vR = _mm256_load_ps(tempR);
        __m256 vNextL = _mm256_load_ps(tempNextL);
        __m256 vNextR = _mm256_load_ps(tempNextR);

        __m256 vOneMinusFrac = _mm256_sub_ps(vOne, vFrac);
        __m256 vOutL = _mm256_add_ps(
            _mm256_mul_ps(vL, vOneMinusFrac),
            _mm256_mul_ps(vNextL, vFrac)
        );
        __m256 vOutR = _mm256_add_ps(
            _mm256_mul_ps(vR, vOneMinusFrac),
            _mm256_mul_ps(vNextR, vFrac)
        );

        for (int j = 0; j < 8; j++) {
            output[i * 2 + j * 2] = _mm256_cvtss_f32(_mm256_permutevar8x32_ps(vOutL, _mm256_set1_epi32(j)));
            output[i * 2 + j * 2 + 1] = _mm256_cvtss_f32(_mm256_permutevar8x32_ps(vOutR, _mm256_set1_epi32(j)));
        }

        vPos = _mm256_add_ps(vPos, _mm256_set1_ps(step * 8));
    }

    float pos = _mm256_cvtss_f32(vPos);
    for (; i < outputFrames; i++) {
        int idx = (int)pos;
        float frac = pos - idx;

        output[i * 2] = input[idx * 2] * (1 - frac) + input[idx * 2 + 2] * frac;
        output[i * 2 + 1] = input[idx * 2 + 1] * (1 - frac) + input[idx * 2 + 3] * frac;

        pos += step;
    }
#else
    // Process 4 output frames at a time with SSE3
    const __m128 vStep = _mm_set1_ps(step);
    const __m128 vOne = _mm_set1_ps(1.0f);
    __m128 vPos = _mm_set_ps(3.0f, 2.0f, 1.0f, 0.0f);
    vPos = _mm_mul_ps(vPos, vStep);

    int i = 0;
    for (; i < outputFrames - 3; i += 4) {
        __m128i vIdx = _mm_cvttps_epi32(vPos);
        __m128 vFrac = _mm_sub_ps(vPos, _mm_cvtepi32_ps(vIdx));

        int indices[4];
        _mm_store_si128((__m128i*)indices, vIdx);

        // Load input samples
        float tempL[4], tempR[4], tempNextL[4], tempNextR[4];
        for (int j = 0; j < 4; j++) {
            tempL[j] = input[indices[j] * 2];
            tempR[j] = input[indices[j] * 2 + 1];
            tempNextL[j] = input[indices[j] * 2 + 2];
            tempNextR[j] = input[indices[j] * 2 + 3];
        }

        __m128 vL = _mm_load_ps(tempL);
        __m128 vR = _mm_load_ps(tempR);
        __m128 vNextL = _mm_load_ps(tempNextL);
        __m128 vNextR = _mm_load_ps(tempNextR);

        __m128 vOneMinusFrac = _mm_sub_ps(vOne, vFrac);

        // Linear interpolation
        __m128 vOutL = _mm_add_ps(
            _mm_mul_ps(vL, vOneMinusFrac),
            _mm_mul_ps(vNextL, vFrac)
        );
        __m128 vOutR = _mm_add_ps(
            _mm_mul_ps(vR, vOneMinusFrac),
            _mm_mul_ps(vNextR, vFrac)
        );

        // Store results
        float outL[4], outR[4];
        _mm_store_ps(outL, vOutL);
        _mm_store_ps(outR, vOutR);

        for (int j = 0; j < 4; j++) {
            output[i * 2 + j * 2] = outL[j];
            output[i * 2 + j * 2 + 1] = outR[j];
        }

        vPos = _mm_add_ps(vPos, _mm_mul_ps(vStep, _mm_set1_ps(4.0f)));
    }

    // Handle remaining frames
    float pos = _mm_cvtss_f32(vPos);
    for (; i < outputFrames; i++) {
        int idx = (int)pos;
        float frac = pos - idx;

        output[i * 2] = input[idx * 2] * (1 - frac) + input[idx * 2 + 2] * frac;
        output[i * 2 + 1] = input[idx * 2 + 1] * (1 - frac) + input[idx * 2 + 3] * frac;

        pos += step;
    }
#endif
}
#endif

static bool DecodeOpusChunk(OpusContext& ctx, int64_t offset, size_t bytesNeeded) {
    using clock = std::chrono::high_resolution_clock;
    auto totalStart = clock::now();

    if (ctx.reachedEnd || (offset + bytesNeeded <= ctx.currentDecodePos)) {
        return true;
    }

    const size_t DECODE_CHUNK = ctx.pcmData.empty() ? 32768 : 5760;

    auto allocStart = clock::now();
    std::vector<float> decodeBuf(DECODE_CHUNK * 2);
    std::vector<float> resampleBuf((DECODE_CHUNK * 441) / 480 * 2); // pre-calculate output size
    auto allocEnd = clock::now();

    int64_t totalOpusTime = 0;
    int64_t totalResampleTime = 0;
    int64_t totalConvertTime = 0;
    int totalFrames = 0;

    while (!ctx.reachedEnd && ctx.pcmData.size() * sizeof(int16_t) < offset + bytesNeeded) {
        auto opusStart = clock::now();
        int framesDecoded = op_read_float_stereo(ctx.opusFile, decodeBuf.data(), DECODE_CHUNK);
        auto opusEnd = clock::now();
        totalOpusTime += std::chrono::duration_cast<std::chrono::microseconds>(opusEnd - opusStart).count();

        if (framesDecoded <= 0) {
            ctx.reachedEnd = true;
            if (framesDecoded < 0) return false;
            break;
        }
        totalFrames += framesDecoded;

        auto resampleStart = clock::now();
        // Use our fixed resampler instead of libsamplerate
        FixedResample48to441(decodeBuf.data(), resampleBuf.data(), framesDecoded);
        int outputFrames = int(framesDecoded * 0.91875f);
        auto resampleEnd = clock::now();
        totalResampleTime += std::chrono::duration_cast<std::chrono::microseconds>(resampleEnd - resampleStart).count();

        auto convertStart = clock::now();
        size_t oldSize = ctx.pcmData.size();
        size_t newSamples = outputFrames * 2;
        ctx.pcmData.resize(oldSize + newSamples);

#ifdef __AVX2__
        for (size_t i = 0; i < newSamples; i += 8) {
            __m256 floats = _mm256_load_ps(&resampleBuf[i]);
            floats = _mm256_mul_ps(floats, _mm256_set1_ps(32767.0f));
            __m256i ints = _mm256_cvtps_epi32(floats);
            __m128i shorts = _mm_packs_epi32(_mm256_extractf128_si256(ints, 0),
                _mm256_extractf128_si256(ints, 1));
            _mm_store_si128((__m128i*) & ctx.pcmData[oldSize + i], shorts);
        }
#else
        for (size_t i = 0; i < newSamples; i += 4) {
            __m128 floats = _mm_load_ps(&resampleBuf[i]);
            floats = _mm_mul_ps(floats, _mm_set1_ps(32767.0f));
            __m128i ints = _mm_cvtps_epi32(floats);
            __m128i shorts = _mm_packs_epi32(ints, ints);
            _mm_store_si128((__m128i*) & ctx.pcmData[oldSize + i], shorts);
        }
#endif

        auto convertEnd = clock::now();
        totalConvertTime += std::chrono::duration_cast<std::chrono::microseconds>(convertEnd - convertStart).count();

        ctx.currentDecodePos = ctx.pcmData.size() * sizeof(int16_t);
    }

    auto totalEnd = clock::now();
    auto totalMicros = std::chrono::duration_cast<std::chrono::microseconds>(totalEnd - totalStart).count();
    auto allocMicros = std::chrono::duration_cast<std::chrono::microseconds>(allocEnd - allocStart).count();

    Msg("Opus decode timing:\n"
        "  Total time: %.2fms\n"
        "  Buffer alloc: %.2fms\n"
        "  Opus decode: %.2fms (%d frames)\n"
        "  Resampling: %.2fms\n"
        "  Float conversion: %.2fms\n",
        totalMicros / 1000.0f,
        allocMicros / 1000.0f,
        totalOpusTime / 1000.0f, totalFrames,
        totalResampleTime / 1000.0f,
        totalConvertTime / 1000.0f);

    return true;
}

static bool OpenOpusContext(
    const std::string& wavName,
    const std::string& opusName,
    CBaseFileSystem* filesystem,
    const char* pathID)
{
    std::lock_guard<std::mutex> lock(g_opusCacheMutex);

    if (g_opusCache.find(wavName) != g_opusCache.end()) {
        return true;
    }

    // Read the opus file
    auto fileData = ReadEntireFileIntoMem(filesystem, opusName.c_str(), pathID);
    if (fileData.empty()) {
        return false;
    }

    // Create context
    OpusContext ctx;
    ctx.opusFileData = std::move(fileData);

    // Open opus file
    int error = 0;
    ctx.opusFile = op_open_memory(
        ctx.opusFileData.data(),
        ctx.opusFileData.size(),
        &error);
    if (!ctx.opusFile || error != 0) {
        return false;
    }

    // Create resampler
    ctx.srcState = src_new(SRC_SINC_FASTEST, 2, &error);
    if (!ctx.srcState || error != 0) {
        op_free(ctx.opusFile);
        return false;
    }

    // Pre-allocate some space
    ctx.pcmData.reserve(1024 * 1024);  // 1MB starting point

    g_opusCache[wavName] = std::move(ctx);
    return true;
}

static __int64 HandleOpusRead(CBaseFileSystem* fs, FileAsyncRequest_t* request) {
    std::unique_lock<std::mutex> lock(g_opusCacheMutex);
    auto it = g_opusCache.find(request->pszFilename);
    if (it == g_opusCache.end()) {
        lock.unlock();
        return HandleOriginalRead(fs, request);
    }
    OpusContext& ctx = it->second;

    // Handle skip detection
    if (!ctx.skipDiscovered && request->nOffset > 0) {
        ctx.skipDiscovered = true;
        ctx.skipOffset = request->nOffset;
        Msg("[Opus] Discovered skip=%lld for file='%s'\n",
            (long long)ctx.skipOffset, request->pszFilename);
    }

    // Calculate real file offset
    int64_t fileOffset = request->nOffset - ctx.skipOffset;
    if (fileOffset < 0) {
        int64_t overhang = -fileOffset;
        fileOffset = 0;
        request->nBytes -= overhang;
        if (request->nBytes < 0) {
            request->nBytes = 0;
        }
    }

    // Decode if needed
    if (!DecodeOpusChunk(ctx, fileOffset, request->nBytes)) {
        lock.unlock();
        if (request->pfnCallback) {
            request->pfnCallback(request, 0, FSASYNC_ERR_READING);
        }
        return FSASYNC_ERR_READING;
    }

    // Copy data to output
    void* pDest = request->pData ? request->pData :
        (request->pfnAlloc ?
            request->pfnAlloc(request->pszFilename, request->nBytes +
                ((request->flags & FSASYNC_FLAGS_NULLTERMINATE) ? 1 : 0)) :
            malloc(request->nBytes +
                ((request->flags & FSASYNC_FLAGS_NULLTERMINATE) ? 1 : 0)));

    int actualRead = 0;
    if (request->nBytes > 0 && fileOffset < ctx.currentDecodePos) {
        size_t bytesToCopy = request->nBytes;
        if (fileOffset + bytesToCopy > ctx.currentDecodePos) {
            bytesToCopy = ctx.currentDecodePos - fileOffset;
        }
        memcpy(pDest, &ctx.pcmData[fileOffset / sizeof(int16_t)], bytesToCopy);
        actualRead = (int)bytesToCopy;
    }

    if (request->flags & FSASYNC_FLAGS_NULLTERMINATE) {
        reinterpret_cast<char*>(pDest)[actualRead] = '\0';
    }

    lock.unlock();

    // Callback
    pPerformAsyncCallback(fs, request, pDest, actualRead,
        (actualRead < request->nBytes) ? FSASYNC_ERR_READING : FSASYNC_OK);

    Msg("SyncRead (opus): file='%s' offset=%lld bytes=%lld skip=%lld [actual %d]\n",
        request->pszFilename, (long long)request->nOffset, (long long)request->nBytes,
        (long long)ctx.skipOffset, actualRead);

    return (actualRead == 0 && request->nBytes != 0) ?
        FSASYNC_ERR_READING : FSASYNC_OK;
}

// --------------------------------------------------------------------
// The main hooked function
// --------------------------------------------------------------------
__int64 __fastcall Hooked_CBaseFileSystem__SyncRead(
    CBaseFileSystem* filesystem,
    FileAsyncRequest_t* request)
{
    static bool bInited = false;
    if (!bInited) {
        bInited = true;
        // Fill in function pointers
        // (Example addresses; yours may differ)
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

    // If it's a .wav...
    if (EndsWithWav(request->pszFilename)) {
        // Check for matching .opus
        std::string opusName = MakeOpusFilename(request->pszFilename);
        if (FSOperations::FileExists(filesystem, opusName.c_str(), request->pszPathID)) {
            // Attempt to open & decode
            if (OpenOpusContext(request->pszFilename, opusName, filesystem, request->pszPathID)) {
                // If successful, handle the read from our decoded buffer
                return HandleOpusRead(filesystem, request);
            }
        }
    }

    // Fallback to original
    return HandleOriginalRead(filesystem, request);
}
