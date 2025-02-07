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
    void* pContext; // This field is used by the game – we must not use it.
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

// Thread priority stubs (unchanged)
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

// A simple helper to check the header.
static bool IsOpusFile(const std::vector<unsigned char>& fileData)
{
    // An Opus (Ogg) file always begins with "OggS"
    return (fileData.size() >= 4 && std::memcmp(fileData.data(), "OggS", 4) == 0);
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
// Global cache for Opus file data (the "template" track)
// --------------------------------------------------------------------
struct OpusContext {
    struct Track {
        // Entire file data in memory.
        std::vector<unsigned char> opusFileData;
        // Pointer returned by op_open_memory.
        OggOpusFile* opusFile = nullptr;
        // Ring buffer holding decoded PCM data (48 kHz, interleaved int16_t).
        std::vector<int16_t> pcmRingBuffer;
        // Count of frames (each frame = one sample per channel) that have been consumed.
        size_t readFrameIndex = 0;
        // Persistent fractional offset (in frames) in the source 48 kHz data.
        double resamplePos = 0.0;
        // Flag indicating the end of the Opus file.
        bool reachedEnd = false;
        // Number of channels.
        int channels = 1;
        // Use a unique_ptr so that the mutex is movable.
        std::unique_ptr<std::recursive_mutex> bufferMutex;

        // Default constructor: allocate a new mutex.
        Track() : bufferMutex(std::make_unique<std::recursive_mutex>()) { }

        // Delete copy constructor/assignment.
        Track(const Track&) = delete;
        Track& operator=(const Track&) = delete;

        // Move constructor.
        Track(Track&& other) noexcept :
            opusFileData(std::move(other.opusFileData)),
            opusFile(other.opusFile),
            pcmRingBuffer(std::move(other.pcmRingBuffer)),
            readFrameIndex(other.readFrameIndex),
            resamplePos(other.resamplePos),
            reachedEnd(other.reachedEnd),
            channels(other.channels),
            bufferMutex(std::move(other.bufferMutex))
        {
            if (!bufferMutex) {
                bufferMutex = std::make_unique<std::recursive_mutex>();
            }
            other.opusFile = nullptr;
            other.readFrameIndex = 0;
            other.resamplePos = 0.0;
            other.reachedEnd = false;
        }

        // Move assignment operator.
        Track& operator=(Track&& other) noexcept {
            if (this != &other) {
                opusFileData = std::move(other.opusFileData);
                opusFile = other.opusFile;
                pcmRingBuffer = std::move(other.pcmRingBuffer);
                readFrameIndex = other.readFrameIndex;
                resamplePos = other.resamplePos;
                reachedEnd = other.reachedEnd;
                channels = other.channels;
                bufferMutex = std::move(other.bufferMutex);
                if (!bufferMutex) {
                    bufferMutex = std::make_unique<std::recursive_mutex>();
                }
                other.opusFile = nullptr;
                other.readFrameIndex = 0;
                other.resamplePos = 0.0;
                other.reachedEnd = false;
            }
            return *this;
        }

        ~Track() {
            if (opusFile)
                op_free(opusFile);
        }
    };

    std::vector<Track> tracks;
};

static std::unordered_map<std::string, OpusContext> g_opusCache;
static std::mutex g_opusCacheMutex;

// --------------------------------------------------------------------
// Global map for per-request playback instances.
// We key this map by the FileAsyncRequest_t pointer.
// --------------------------------------------------------------------
static std::unordered_map<FileAsyncRequest_t*, OpusContext::Track*> g_playbackInstances;
static std::mutex g_playbackInstancesMutex;

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
        fileData.clear();
    }
    return fileData;
}

// --------------------------------------------------------------------
// Resampling and conversion helpers (unchanged)
// --------------------------------------------------------------------
static void FixedResample48to441MultiChannel_Persistent(const float* input, int availableFrames,
    float* output, int outputFrameCount, int channels, double& inPos)
{
    const double step = 48000.0 / 44100.0;
    for (int i = 0; i < outputFrameCount; i++) {
        if (inPos >= availableFrames - 1) {
            for (int j = i; j < outputFrameCount; j++) {
                for (int ch = 0; ch < channels; ch++) {
                    output[j * channels + ch] = 0.0f;
                }
            }
            inPos = availableFrames - 1;
            break;
        }
        int idx = (int)inPos;
        double frac = inPos - idx;
        int idxNext = idx + 1;
        for (int ch = 0; ch < channels; ch++) {
            float s0 = input[idx * channels + ch];
            float s1 = input[idxNext * channels + ch];
            output[i * channels + ch] = s0 * (1.0f - (float)frac) + s1 * (float)frac;
        }
        inPos += step;
    }
}

static void ConsumeFramesFromRingBuffer(OpusContext::Track& track) {
    std::lock_guard<std::recursive_mutex> lock(*track.bufferMutex);
    size_t wholeFramesToConsume = (size_t)std::floor(track.resamplePos);
    size_t availableFrames = track.pcmRingBuffer.size() / track.channels;
    if (wholeFramesToConsume > availableFrames) {
        wholeFramesToConsume = availableFrames;
    }
    if (wholeFramesToConsume > 0) {
        size_t samplesToRemove = wholeFramesToConsume * track.channels;
        track.pcmRingBuffer.erase(track.pcmRingBuffer.begin(),
            track.pcmRingBuffer.begin() + samplesToRemove);
        track.readFrameIndex = 0;
        track.resamplePos -= wholeFramesToConsume;
    }
}

static void FloatToInt16MultiChannel(const float* in, int16_t* out, size_t frames, int channels) {
    size_t totalSamples = frames * channels;
    size_t i = 0;
    const __m128 scale = _mm_set1_ps(32767.0f);
    for (; i + 7 < totalSamples; i += 8) {
        __m128 f0 = _mm_loadu_ps(in + i);
        __m128 f1 = _mm_loadu_ps(in + i + 4);
        f0 = _mm_mul_ps(f0, scale);
        f1 = _mm_mul_ps(f1, scale);
        __m128i i0 = _mm_cvtps_epi32(f0);
        __m128i i1 = _mm_cvtps_epi32(f1);
        __m128i packed = _mm_packs_epi32(i0, i1);
        _mm_storeu_si128((__m128i*)(out + i), packed);
    }
    for (; i < totalSamples; ++i) {
        float scaled = in[i] * 32767.0f;
        int32_t sample = (int32_t)(scaled + (scaled >= 0.f ? 0.5f : -0.5f));
        if (sample > 32767)
            sample = 32767;
        else if (sample < -32768)
            sample = -32768;
        out[i] = (int16_t)sample;
    }
}

// --------------------------------------------------------------------
// New helper: decode a chunk for a given playback instance (Track)
// This is a refactored version of DecodeOpusChunk that works directly on a Track.
// --------------------------------------------------------------------
static bool DecodeOpusChunkInstance(OpusContext::Track* track, size_t framesNeeded)
{
    std::lock_guard<std::recursive_mutex> lock(*track->bufferMutex);
    size_t currentFrames = track->pcmRingBuffer.size() / track->channels;
    if (currentFrames >= track->readFrameIndex + framesNeeded)
        return true;

    const size_t DECODE_CHUNK = 23040;
    while (!track->reachedEnd &&
        ((track->pcmRingBuffer.size() / track->channels) < (track->readFrameIndex + framesNeeded)))
    {
        std::vector<float> decodeBuf(DECODE_CHUNK * track->channels);
        int framesDecoded = op_read_float(track->opusFile, decodeBuf.data(), (int)decodeBuf.size(), nullptr);
        if (framesDecoded <= 0)
        {
            track->reachedEnd = true;
            if (framesDecoded < 0)
            {
                Warning("[Opus] decode error: %d\n", framesDecoded);
                return false;
            }
            break;
        }
        size_t prevFrameCount = track->pcmRingBuffer.size() / track->channels;
        track->pcmRingBuffer.resize((prevFrameCount + framesDecoded) * track->channels);
        FloatToInt16MultiChannel(decodeBuf.data(),
            &track->pcmRingBuffer[prevFrameCount * track->channels],
            framesDecoded,
            track->channels);
    }
    return true;
}

// --------------------------------------------------------------------
// Original OpenOpusContext remains as a cache loader (stores one template Track)
// --------------------------------------------------------------------
bool OpenOpusContext(const std::string& wavName, CBaseFileSystem* filesystem, const char* pathID)
{
    std::lock_guard<std::mutex> lock(g_opusCacheMutex);
    if (g_opusCache.find(wavName) != g_opusCache.end())
        return true; // Already opened

    OpusContext ctx;
    std::vector<unsigned char> fileData = ReadEntireFileIntoMem(filesystem, wavName.c_str(), pathID);
    if (fileData.empty())
        return false;
    if (!IsOpusFile(fileData))
        return false;

    OpusContext::Track newTrack;
    newTrack.opusFileData = std::move(fileData);
    int error = 0;
    newTrack.opusFile = op_open_memory(newTrack.opusFileData.data(),
        newTrack.opusFileData.size(),
        &error);
    if (!newTrack.opusFile || error != 0) {
        if (newTrack.opusFile)
            op_free(newTrack.opusFile);
        Warning("[Opus] op_open_memory failed with error %d for file %s\n", error, wavName.c_str());
        return false;
    }
    const OpusHead* head = op_head(newTrack.opusFile, -1);
    if (!head) {
        op_free(newTrack.opusFile);
        Warning("[Opus] op_head failed for file %s\n", wavName.c_str());
        return false;
    }
    newTrack.channels = head->channel_count;
    Msg("[Opus] Opened opus file '%s': channels=%d\n", wavName.c_str(), newTrack.channels);

    // Pre-decode a few seconds.
    const size_t INITIAL_DECODE_FRAMES = 48000 * 2;
    if (!DecodeOpusChunkInstance(&newTrack, INITIAL_DECODE_FRAMES)) {
        Warning("[Opus] Initial decode failed for '%s'\n", wavName.c_str());
    }
    ctx.tracks.push_back(std::move(newTrack));
    g_opusCache[wavName] = std::move(ctx);
    return true;
}

// --------------------------------------------------------------------
// New helper: Create a new playback instance (a Track) from the cached file data.
// Each instance has its own decoding state.
// --------------------------------------------------------------------
static OpusContext::Track* CreateOpusTrackInstance(const std::string& filename, CBaseFileSystem* filesystem, const char* pathID) {
    // Ensure the file is cached.
    {
        std::lock_guard<std::mutex> lock(g_opusCacheMutex);
        if (g_opusCache.find(filename) == g_opusCache.end()) {
            if (!OpenOpusContext(filename, filesystem, pathID))
                return nullptr;
        }
    }
    // Get the cached file data from the template Track.
    std::vector<unsigned char> cachedData;
    {
        std::lock_guard<std::mutex> lock(g_opusCacheMutex);
        if (!g_opusCache[filename].tracks.empty())
            cachedData = g_opusCache[filename].tracks[0].opusFileData;
        else
            return nullptr;
    }
    // Create a temporary track locally.
    OpusContext::Track tempTrack;
    tempTrack.opusFileData = cachedData;
    int error = 0;
    tempTrack.opusFile = op_open_memory(tempTrack.opusFileData.data(), tempTrack.opusFileData.size(), &error);
    if (!tempTrack.opusFile || error != 0) {
        if (tempTrack.opusFile)
            op_free(tempTrack.opusFile);
        Warning("[Opus] op_open_memory failed with error %d for instance creation\n", error);
        return nullptr;
    }
    const OpusHead* head = op_head(tempTrack.opusFile, -1);
    if (!head) {
        op_free(tempTrack.opusFile);
        Warning("[Opus] op_head failed during instance creation\n");
        return nullptr;
    }
    tempTrack.channels = head->channel_count;
    Msg("[Opus] Created new playback instance for '%s': channels=%d\n", filename.c_str(), tempTrack.channels);
    // Optionally, pre-decode some initial frames.
    const size_t INITIAL_DECODE_FRAMES = 48000 * 2;
    DecodeOpusChunkInstance(&tempTrack, INITIAL_DECODE_FRAMES);
    // Allocate a new Track on the heap and move the temporary track into it.
    OpusContext::Track* instance = new OpusContext::Track(std::move(tempTrack));
    return instance;
}

// --------------------------------------------------------------------
// New helper: Free a playback instance created by CreateOpusTrackInstance.
// (Call this when the playback is done.)
// --------------------------------------------------------------------
static void ReleaseOpusPlaybackInstance(OpusContext::Track* track) {
    if (track) {
        delete track;
    }
}

// --------------------------------------------------------------------
// This function handles the read request by decoding data from the instance.
// We no longer use request->pContext; instead we look up our instance in a global map.
// --------------------------------------------------------------------
__int64 HandleOpusRead(CBaseFileSystem* filesystem, FileAsyncRequest_t* request)
{
    // Look up the playback instance from our global map (keyed by the request pointer).
    OpusContext::Track* track = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_playbackInstancesMutex);
        auto it = g_playbackInstances.find(request);
        if (it != g_playbackInstances.end())
            track = it->second;
    }
    if (!track) {
        track = CreateOpusTrackInstance(request->pszFilename, filesystem, request->pszPathID);
        if (!track)
        {
            Warning("[Opus] Failed to create playback instance for '%s'\n", request->pszFilename);
            return -99999; // error fallback
        }
        {
            std::lock_guard<std::mutex> lock(g_playbackInstancesMutex);
            g_playbackInstances[request] = track;
        }
    }

    int channels = track->channels;
    // Adjust starting position if needed.
    {
        std::lock_guard<std::recursive_mutex> lock(*track->bufferMutex);
        if (track->resamplePos < 1e-6 && request->nOffset > 0) {
            int64_t effectiveOffset = request->nOffset;
            if (effectiveOffset < 0)
                effectiveOffset = 0;
            size_t skipFrames44 = effectiveOffset / (sizeof(int16_t) * channels);
            const double conversionRatio = 48000.0 / 44100.0;
            double skipFrames48 = skipFrames44 * conversionRatio;
            track->resamplePos = skipFrames48;
            Msg("[Opus] Adjusted starting position for instance: skipped %f frames (48kHz)\n", skipFrames48);
        }
    }

    size_t bytesPerFrame = sizeof(int16_t) * channels;
    size_t framesRequested44 = request->nBytes / bytesPerFrame;
    double currentOffset = 0.0;
    {
        std::lock_guard<std::recursive_mutex> lock(*track->bufferMutex);
        currentOffset = track->resamplePos;
    }

    const double step = 48000.0 / 44100.0;
    double inPosNeeded = currentOffset + framesRequested44 * step;
    size_t inputFramesNeeded = (size_t)std::ceil(inPosNeeded);

    {
        std::lock_guard<std::recursive_mutex> lock(*track->bufferMutex);
        size_t availableFrames = track->pcmRingBuffer.size() / channels;
        if (availableFrames < track->readFrameIndex + inputFramesNeeded) {
            if (!DecodeOpusChunkInstance(track, inputFramesNeeded)) {
                Warning("[Opus] Not enough decoded frames available for instance '%s'\n", request->pszFilename);
                // Optionally fill with silence.
            }
        }
    }

    size_t framesAvailable;
    {
        std::lock_guard<std::recursive_mutex> lock(*track->bufferMutex);
        framesAvailable = (track->pcmRingBuffer.size() / channels) - track->readFrameIndex;
    }
    size_t framesForResample = std::min(inputFramesNeeded, framesAvailable);

    std::vector<float> inputFloat(framesForResample * channels);
    {
        std::lock_guard<std::recursive_mutex> lock(*track->bufferMutex);
        const int16_t* src = &track->pcmRingBuffer[track->readFrameIndex * channels];
        for (size_t i = 0; i < framesForResample * channels; i++) {
            inputFloat[i] = src[i] / 32767.0f;
        }
    }

    std::vector<float> resampleOut(framesRequested44 * channels, 0.0f);
    double inPos = currentOffset;
    FixedResample48to441MultiChannel_Persistent(inputFloat.data(), (int)framesForResample,
        resampleOut.data(), (int)framesRequested44,
        channels, inPos);

    {
        std::lock_guard<std::recursive_mutex> lock(*track->bufferMutex);
        track->resamplePos = inPos;
        ConsumeFramesFromRingBuffer(*track);
    }

    std::vector<int16_t> finalPCM(framesRequested44 * channels);
    FloatToInt16MultiChannel(resampleOut.data(), finalPCM.data(), framesRequested44, channels);

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
    if (bytesToCopy < static_cast<size_t>(request->nBytes))
        memset((char*)pDest + bytesToCopy, 0, request->nBytes - bytesToCopy);
    if (request->flags & FSASYNC_FLAGS_NULLTERMINATE)
        reinterpret_cast<char*>(pDest)[bytesToCopy] = '\0';

    pPerformAsyncCallback(filesystem, request, pDest, bytesToCopy, FSASYNC_OK);
    return FSASYNC_OK;
}

// --------------------------------------------------------------------
// The main hooked function (unchanged except for using our new HandleOpusRead)
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
        pFreeAsyncFileHandle = reinterpret_cast<void(__fastcall*)(std::uint64_t)>(G_filesystem_stdio + 0x38610);
        pGetOptimalReadSize = reinterpret_cast<std::uint64_t(__fastcall*)(CBaseFileSystem*, void*, std::uint64_t)>(G_filesystem_stdio + 0x4B70);
        pPerformAsyncCallback = reinterpret_cast<void(__fastcall*)(CBaseFileSystem*, FileAsyncRequest_t*, void*, std::uint64_t, std::uint64_t)>(G_filesystem_stdio + 0x1F200);
        pReleaseAsyncOpenedFiles = reinterpret_cast<void(__fastcall*)(CRITICAL_SECTION*, void*)>(G_filesystem_stdio + 0x233C0);
        pLogFileAccess = reinterpret_cast<void(__fastcall*)(CBaseFileSystem*, const char*, const char*, void*)>(G_filesystem_stdio + 0x9A40);
        pAsyncOpenedFilesCriticalSection = reinterpret_cast<CRITICAL_SECTION*>(G_filesystem_stdio + 0xF90A0);
        pAsyncOpenedFilesTable = G_filesystem_stdio + 0xF90D0;
        pFileAccessLoggingPointer = reinterpret_cast<void*>(G_filesystem_stdio + 0xCDBC0);
    }

    if (request->nBytes < 0 || request->nOffset < 0) {
        if (request->pfnCallback) {
            request->pfnCallback(request, 0, FSASYNC_ERR_FILEOPEN);
        }
        return FSASYNC_ERR_FILEOPEN;
    }
    if (EndsWithWav(request->pszFilename)) {
        if (OpenOpusContext(request->pszFilename, filesystem, request->pszPathID)) {
            __int64 result = HandleOpusRead(filesystem, request);
            if (result != -99999) {
                return result;
            }
        }
    }
    return Original_CBaseFileSystem__SyncRead(filesystem, request);
}
void (*Original_CFileAsyncReadJob_dtor)(FileAsyncRequest_t* thisptr);
// --------------------------------------------------------------------
// In the asynchronous job destructor, remove our playback instance from the global map.
// --------------------------------------------------------------------
void CFileAsyncReadJob_dtor(FileAsyncRequest_t* thisptr) {
    {
        std::lock_guard<std::mutex> lock(g_playbackInstancesMutex);
        auto it = g_playbackInstances.find((FileAsyncRequest_t*)(((__int64)thisptr) + 0x70));
        if (it != g_playbackInstances.end()) {
            ReleaseOpusPlaybackInstance(it->second);
            g_playbackInstances.erase(it);
        }
    }
    return Original_CFileAsyncReadJob_dtor(thisptr);
}
