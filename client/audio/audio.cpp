#include "audio.h"
#include "audio_source.h"
#include "audio_hook_profile.h"
#include "load.h"
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

namespace {
struct FileAsyncRequest {
    const char* filename;
    void* data;
    std::int64_t offset;
    std::int64_t bytes;
    void (*callback)(const FileAsyncRequest*, std::int64_t, int);
    void* context;
    int priority;
    int flags;
    const char* pathID;
    void* specificFile;
    void* (*allocate)(const char*, std::int64_t);
};
static_assert(sizeof(FileAsyncRequest) == 80);
static_assert(offsetof(FileAsyncRequest, callback) == 32);
static_assert(offsetof(FileAsyncRequest, pathID) == 56);
using SyncReadFn = std::int64_t (*)(void*, FileAsyncRequest*);
using CallbackFn = void (*)(void*, FileAsyncRequest*, void*, std::int64_t, int);
SyncReadFn s_originalRead = nullptr;
CallbackFn s_callback = nullptr;
std::uintptr_t s_filesystem = 0;
bool s_installed = false;

bool IsAudioRequest(const FileAsyncRequest& request) {
    auto callback = reinterpret_cast<std::uintptr_t>(request.callback);
    if (callback == s_filesystem + 0x1E230) {
        // CAsyncIOJob replaces the request callback/context before dispatching
        // SyncRead. Its verified completion wrapper restores the original
        // context from +D0 and callback from +D8 before invoking the consumer.
        // Inspect that owner without changing the job or bypassing completion.
        if (!request.context) return false;
        std::memcpy(&callback, static_cast<const unsigned char*>(request.context) + 0xD8, sizeof(callback));
    }
    return callback == G_engine + 0xB530;
}

std::int64_t SyncRead(void* filesystem, FileAsyncRequest* request) {
    // +B5F0's stream scheduler is the sole owner of virtual PCM offsets. Other
    // filesystem reads of a WAV (tools, scripts, UI) still receive the real file,
    // even when an audio metadata record for the same name exists.
    if (!request || !IsAudioRequest(*request))
        return s_originalRead(filesystem, request);
    if (request->offset < 0 || request->bytes < 0) return s_originalRead(filesystem, request);
    std::size_t got = 0;
    std::string error;
    const auto result = ReadR1AudioSource(request->filename, request->pathID,
        static_cast<std::uint64_t>(request->offset), request->data,
        static_cast<std::size_t>(request->bytes), got, error);
    if (result == R1AudioReadResult::NotManaged)
        error = "native audio stream has no generated source record; refusing to interpret physical-file bytes as canonical PCM";
    const int status = result == R1AudioReadResult::Success ? 0 : -4; // FSASYNC_ERR_READING.
    if (status) {
        // The stream scheduler retries failing reads; keep logging off the hot path.
        static std::atomic<unsigned> s_readFailureBudget{ 8 };
        if (s_readFailureBudget.fetch_sub(1, std::memory_order_relaxed) > 0)
            Warning("R1Delta audio read '%s' at %lld: %s\n", request->filename, request->offset, error.c_str());
    }
    // Exact +1F200 helper owns callback locking, request-copy semantics, and
    // native buffer release flags. Never substitute padded silence/full success.
    s_callback(filesystem, request, request->data, static_cast<std::int64_t>(got), status);
    return status;
}
}

bool InstallR1AudioReadHooks(std::uintptr_t filesystemBase) {
    if (GetR1DeltaEngineMode() != R1DeltaEngineMode::Client2015) return false;
    if (s_installed) return s_filesystem == filesystemBase;
    static constexpr std::array<unsigned char, 32> digest = {0x82,0x00,0x54,0x3c,0x98,0x5b,0x34,0xb5,0x92,0xd3,0x0c,0x49,0xa9,0x7e,0x45,0xf0,0x0d,0xb4,0x85,0x38,0xba,0xcc,0xf2,0x9e,0xee,0xd6,0x71,0xd0,0xdb,0x87,0x36,0xa2};
    if (!r1audio::VerifyModule(filesystemBase, 0x54874230, 0x2116000, digest, "R1 filesystem_stdio.dll")) return false;
    static constexpr unsigned char readBytes[] = {0x40,0x53,0x55,0x56,0x57,0x48,0x83,0xEC,0x48,0x48,0x83,0x7A,0x18,0x00};
    static constexpr unsigned char callbackBytes[] = {0x48,0x8B,0xC4,0x48,0x89,0x68,0x10,0x48,0x89,0x70,0x18,0x48,0x89,0x78,0x20,0x41,0x54};
    static constexpr unsigned char asyncCallbackBytes[] = {
        0x48,0x83,0xEC,0x28,0x4C,0x8B,0x49,0x28,0x45,0x85,0xC0,0x75,0x18,0xF6,0x41,0x34,
        0x02,0x75,0x12,0x48,0x8B,0x41,0x08,0x49,0x89,0x91,0xC8,0x00,0x00,0x00,0x49,0x89,
        0x81,0xC0,0x00,0x00,0x00,0x49,0x8B,0x81,0xD8,0x00,0x00,0x00,0x48,0x85,0xC0,0x74,
        0x16,0x48,0x89,0x41,0x20,0x49,0x8B,0x81,0xD0,0x00,0x00,0x00,0x48,0x89,0x41,0x28,
        0x41,0xFF,0x91,0xD8,0x00,0x00,0x00,0x48,0x83,0xC4,0x28,0xC3
    };
    if (std::memcmp(reinterpret_cast<const void*>(filesystemBase + 0x23860), readBytes, sizeof(readBytes)) ||
        std::memcmp(reinterpret_cast<const void*>(filesystemBase + 0x1F200), callbackBytes, sizeof(callbackBytes)) ||
        std::memcmp(reinterpret_cast<const void*>(filesystemBase + 0x1E230), asyncCallbackBytes, sizeof(asyncCallbackBytes))) {
        Warning("R1Delta audio: refusing mismatched filesystem SyncRead/completion/async-wrapper bytes\n");
        return false;
    }
    s_callback = reinterpret_cast<CallbackFn>(filesystemBase + 0x1F200);
    void* target = reinterpret_cast<void*>(filesystemBase + 0x23860);
    const auto create = MH_CreateHook(target, reinterpret_cast<void*>(SyncRead), reinterpret_cast<void**>(&s_originalRead));
    if (create != MH_OK) return false;
    s_filesystem = filesystemBase;
    const auto enable = MH_EnableHook(target);
    if (enable != MH_OK) {
        MH_RemoveHook(target);
        s_originalRead = nullptr;
        s_callback = nullptr;
        s_filesystem = 0;
        return false;
    }
    s_installed = true;
    Msg("R1Delta audio: installed exact native streaming PCM read adapter\n");
    return true;
}
bool R1AudioReadHooksInstalled() { return s_installed; }
void AudioReportMemory() { ReportR1AudioSources(); }
