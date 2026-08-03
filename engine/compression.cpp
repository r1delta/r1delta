// r1dc_hooks.cpp
//
// R1Delta ZSTD or Titanfall LZHAM Alpha 8 streaming decompression.
//
// Native Alpha 8 state handles remain unchanged. A sidecar registry supplies
// the locking and ZSTD state, including for handles created before the hooks
// were installed. Marked ZSTD frames stream directly into the caller's output
// buffer; unmarked streams are passed through to the original LZHAM decoder.
//

#include "core.h"
#include "compression_stream.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

#include "MinHook.h"
#include <zstd.h>
#include "audio.h"
#include "load.h"
#include "logging.h"

// --------------------------------------------------------------------------
// Original function pointer types for fallback to LZHAM
// --------------------------------------------------------------------------
using r1dc_init_t = void* (*)(const void* params);
using r1dc_reinit_t = void* (*)(void* p, const void* params);
using r1dc_deinit_t = uint32_t (*)(void* p);
using r1dc_decompress_t = int (*)(
    void* p,
    const void* pIn_buf,
    size_t* pIn_buf_size,
    void* pOut_buf,
    size_t* pOut_buf_size,
    int no_more_input_bytes_flag);

// Global pointers to the original LZHAM routines (set by MinHook)
static r1dc_init_t       original_lzham_decompressor_init = nullptr;
static r1dc_reinit_t     original_lzham_decompressor_reinit = nullptr;
static r1dc_deinit_t     original_lzham_decompressor_deinit = nullptr;
static r1dc_decompress_t original_lzham_decompressor_decompress = nullptr;

namespace
{

constexpr uint32_t kAlpha8OutputUnbuffered = 1;

bool RangesOverlap(const void* first, size_t firstSize, const void* second, size_t secondSize) noexcept
{
    if (!first || !second || !firstSize || !secondSize)
        return false;

    const uintptr_t firstBegin = reinterpret_cast<uintptr_t>(first);
    const uintptr_t secondBegin = reinterpret_cast<uintptr_t>(second);
    if (firstSize > UINTPTR_MAX - firstBegin || secondSize > UINTPTR_MAX - secondBegin)
        return true;

    return firstBegin < secondBegin + secondSize
        && secondBegin < firstBegin + firstSize;
}

uint32_t Alpha8DecompressFlags(const void* params) noexcept
{
    uint32_t flags = 0;
    memcpy(&flags, static_cast<const uint8_t*>(params) + 8, sizeof(flags));
    return flags;
}

} // namespace

// --------------------------------------------------------------------------
// r1dc context struct
// --------------------------------------------------------------------------
struct r1dc_context_t
{
    r1dc_context_t(void* nativeState, bool outputUnbuffered) noexcept
        : lzham_ctx(nativeState)
    {
        InitializeSRWLock(&mtx);
        zstd.Reset(outputUnbuffered);
    }

    SRWLOCK mtx;
    void* lzham_ctx{};
    r1delta::compression::R1DZstdStream zstd;
    bool errorReported{};
    bool retired{};
};

namespace
{

using R1DCContextPtr = std::shared_ptr<r1dc_context_t>;

SRWLOCK s_contextRegistryLock = SRWLOCK_INIT;
std::unordered_map<void*, R1DCContextPtr> s_contextRegistry;

R1DCContextPtr MakeContext(void* nativeState, bool outputUnbuffered) noexcept
{
    try
    {
        return std::make_shared<r1dc_context_t>(
            nativeState,
            outputUnbuffered);
    }
    catch (...)
    {
        return {};
    }
}

bool RegisterContext(void* nativeState, const R1DCContextPtr& ctx) noexcept
{
    SRWGuard lock(&s_contextRegistryLock);
    if (s_contextRegistry.find(nativeState) != s_contextRegistry.end())
        return false;

    try
    {
        return s_contextRegistry.emplace(nativeState, ctx).second;
    }
    catch (...)
    {
        return false;
    }
}

R1DCContextPtr FindOrAdoptContext(
    void* nativeState,
    bool outputUnbuffered) noexcept
{
    {
        SRWGuardShared lock(&s_contextRegistryLock);
        const auto it = s_contextRegistry.find(nativeState);
        if (it != s_contextRegistry.end())
            return it->second;
    }

    R1DCContextPtr candidate = MakeContext(nativeState, outputUnbuffered);
    if (!candidate)
        return {};

    SRWGuard lock(&s_contextRegistryLock);
    const auto existing = s_contextRegistry.find(nativeState);
    if (existing != s_contextRegistry.end())
        return existing->second;

    try
    {
        s_contextRegistry.emplace(nativeState, candidate);
        return candidate;
    }
    catch (...)
    {
        return {};
    }
}

R1DCContextPtr RemoveContext(void* nativeState) noexcept
{
    SRWGuard lock(&s_contextRegistryLock);
    const auto it = s_contextRegistry.find(nativeState);
    if (it == s_contextRegistry.end())
        return {};

    R1DCContextPtr ctx = it->second;
    s_contextRegistry.erase(it);
    return ctx;
}

bool RekeyContext(
    void* oldNativeState,
    void* newNativeState,
    const R1DCContextPtr& ctx) noexcept
{
    if (oldNativeState == newNativeState)
        return true;

    SRWGuard lock(&s_contextRegistryLock);
    const auto oldIt = s_contextRegistry.find(oldNativeState);
    if (oldIt == s_contextRegistry.end() || oldIt->second != ctx)
        return false;

    const auto newIt = s_contextRegistry.find(newNativeState);
    if (newIt != s_contextRegistry.end() && newIt->second != ctx)
        return false;

    try
    {
        if (newIt == s_contextRegistry.end())
            s_contextRegistry.emplace(newNativeState, ctx);
        s_contextRegistry.erase(oldNativeState);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

} // namespace


// --------------------------------------------------------------------------
// r1dc_init
// --------------------------------------------------------------------------
void* r1dc_init(const void* params)
{
    ZoneScoped;

    if (!original_lzham_decompressor_init || !params)
        return nullptr;

    void* nativeState = original_lzham_decompressor_init(params);
    if (!nativeState)
        return nullptr;

    const uint32_t flags = Alpha8DecompressFlags(params);
    R1DCContextPtr ctx = MakeContext(
        nativeState,
        (flags & kAlpha8OutputUnbuffered) != 0);
    if (!ctx || !RegisterContext(nativeState, ctx))
    {
        if (original_lzham_decompressor_deinit)
            original_lzham_decompressor_deinit(nativeState);
        return nullptr;
    }

    return nativeState;
}


// --------------------------------------------------------------------------
// r1dc_reinit: Called each time the engine wants to start a new chunk
// --------------------------------------------------------------------------
void* r1dc_reinit(void* p, const void* params)
{
    ZoneScoped;

    if (!p || !params || !original_lzham_decompressor_reinit)
        return nullptr;

    const uint32_t flags = Alpha8DecompressFlags(params);
    R1DCContextPtr ctx = FindOrAdoptContext(
        p,
        (flags & kAlpha8OutputUnbuffered) != 0);
    if (!ctx)
        return nullptr;

    SRWGuard lock(&ctx->mtx);
    if (ctx->retired || !ctx->lzham_ctx)
        return nullptr;

    void* reinitialized = original_lzham_decompressor_reinit(ctx->lzham_ctx, params);
    if (!reinitialized)
        return nullptr;

    void* oldNativeState = ctx->lzham_ctx;
    ctx->lzham_ctx = reinitialized;
    ctx->zstd.Reset((flags & kAlpha8OutputUnbuffered) != 0);
    ctx->errorReported = false;

    if (!RekeyContext(oldNativeState, reinitialized, ctx))
    {
        Warning(
            "[r1dc] LZHAM reinit changed state from %p to %p, but the sidecar "
            "registry could not be updated\n",
            oldNativeState,
            reinitialized);
        return nullptr;
    }

    return reinitialized;
}

// --------------------------------------------------------------------------
// r1dc_deinit
// --------------------------------------------------------------------------
uint32_t r1dc_deinit(void* p)
{
    ZoneScoped;

    if (!p)
        return 0;

    R1DCContextPtr ctx = RemoveContext(p);
    if (!ctx)
        return original_lzham_decompressor_deinit
            ? original_lzham_decompressor_deinit(p)
            : 0;

    SRWGuard lock(&ctx->mtx);
    if (ctx->retired)
        return 0;

    ctx->retired = true;
    uint32_t ret = 0;
    if (ctx->lzham_ctx && original_lzham_decompressor_deinit)
        ret = original_lzham_decompressor_deinit(ctx->lzham_ctx);
    ctx->lzham_ctx = nullptr;
    return ret;
}


// --------------------------------------------------------------------------
// r1dc_decompress: preserve Alpha 8's byte-count/status contract while
// dispatching marked streams to ZSTD and everything else to stock LZHAM.
//
int r1dc_decompress(
    void* p,
    const void* pIn_buf,
    size_t* pIn_buf_size,
    void* pOut_buf,
    size_t* pOut_buf_size,
    int no_more_input_bytes_flag)
{
    ZoneScoped;

    using Status = r1delta::compression::Alpha8DecompressStatus;
    if (!p)
        return static_cast<int>(Status::InvalidParameter);

    if (HasEngineCommandLineFlag("-r1delta_compression_passthrough"))
        return original_lzham_decompressor_decompress(
            p,
            pIn_buf,
            pIn_buf_size,
            pOut_buf,
            pOut_buf_size,
            no_more_input_bytes_flag);

    // Titanfall's VPK reader submits complete compressed chunks in a single
    // call. Decode those without attaching ZSTD stream state to the shared
    // native LZHAM handle: filesystem jobs may reuse the same handle for
    // independent chunks, so persistent per-handle stream state can cross
    // logical job boundaries.
    if (pIn_buf_size
        && pOut_buf_size
        && pIn_buf
        && *pIn_buf_size >= sizeof(r1delta::compression::kR1DZstdMarker)
        && no_more_input_bytes_flag)
    {
        uint64_t marker = 0;
        memcpy(&marker, pIn_buf, sizeof(marker));
        if (marker == r1delta::compression::kR1DZstdMarker)
        {
            const size_t inputAvailable = *pIn_buf_size;
            const size_t outputCapacity = *pOut_buf_size;
            if ((inputAvailable && !pIn_buf) || (outputCapacity && !pOut_buf))
                return static_cast<int>(Status::InvalidParameter);

            const void* compressed = static_cast<const uint8_t*>(pIn_buf) + sizeof(marker);
            const size_t compressedSize = inputAvailable - sizeof(marker);
            std::vector<uint8_t> stagedOutput;
            try
            {
                stagedOutput.resize(outputCapacity);
            }
            catch (...)
            {
                *pIn_buf_size = 0;
                *pOut_buf_size = 0;
                return static_cast<int>(Status::FailedInitializing);
            }

            const size_t result = ZSTD_decompress(
                stagedOutput.data(),
                outputCapacity,
                compressed,
                compressedSize);

            *pIn_buf_size = inputAvailable;
            if (ZSTD_isError(result))
            {
                *pOut_buf_size = 0;
                return static_cast<int>(
                    ZSTD_getFrameContentSize(compressed, compressedSize) > outputCapacity
                        ? Status::FailedDestBufferTooSmall
                        : Status::FailedBadCode);
            }

            if (result)
                memcpy(pOut_buf, stagedOutput.data(), result);
            *pOut_buf_size = result;
            return static_cast<int>(Status::Success);
        }
    }

    // Dedicated.dll initializes a decompressor before this DLL can install
    // the hook. Alpha 8's Titanfall call sites use OUTPUT_UNBUFFERED, so a
    // first-seen native handle can be adopted without changing its identity.
    R1DCContextPtr ctx = FindOrAdoptContext(p, true);
    if (!ctx)
    {
        if (pIn_buf_size)
            *pIn_buf_size = 0;
        if (pOut_buf_size)
            *pOut_buf_size = 0;
        return static_cast<int>(Status::FailedInitializing);
    }

    SRWGuard lock(&ctx->mtx);
    if (ctx->retired)
        return static_cast<int>(Status::InvalidParameter);

    const size_t inputBefore = pIn_buf_size ? *pIn_buf_size : 0;
    const size_t outputBefore = pOut_buf_size ? *pOut_buf_size : 0;
    uint64_t prefix = 0;
    if (pIn_buf && inputBefore >= sizeof(prefix))
        memcpy(&prefix, pIn_buf, sizeof(prefix));

    const auto dispatch = ctx->zstd.Decompress(
        pIn_buf,
        pIn_buf_size,
        pOut_buf,
        pOut_buf_size,
        no_more_input_bytes_flag != 0);

    // The stock Alpha 8 decoder mutates its state too. Keep the same
    // per-context lock held across the fallback call so decompress/reinit
    // cannot race one another.
    if (dispatch.useLzham)
    {
        if (!ctx->lzham_ctx || !original_lzham_decompressor_decompress)
        {
            if (pIn_buf_size)
                *pIn_buf_size = 0;
            if (pOut_buf_size)
                *pOut_buf_size = 0;
            return static_cast<int>(Status::FailedInitializing);
        }
        return original_lzham_decompressor_decompress(
            ctx->lzham_ctx,
            pIn_buf,
            pIn_buf_size,
            pOut_buf,
            pOut_buf_size,
            no_more_input_bytes_flag);
    }

    const int status = static_cast<int>(dispatch.status);
    static volatile LONG zstdLogBudget = 1024;
    if (InterlockedDecrement(&zstdLogBudget) >= 0)
    {
        char buffer[384];
        _snprintf_s(
            buffer,
            sizeof(buffer),
            _TRUNCATE,
            "[r1dc] ZSTD call state=%p input=%zu outputCapacity=%zu eof=%d "
            "prefix=%016llX consumed=%zu produced=%zu status=%d\n",
            p,
            inputBefore,
            outputBefore,
            no_more_input_bytes_flag,
            static_cast<unsigned long long>(prefix),
            pIn_buf_size ? *pIn_buf_size : 0,
            pOut_buf_size ? *pOut_buf_size : 0,
            status);
        OutputDebugStringA(buffer);
    }
    if (status >= static_cast<int>(Status::FailedInitializing) && !ctx->errorReported)
    {
        const char* reason = status == static_cast<int>(Status::InvalidParameter)
            ? "invalid Alpha 8 decompression arguments or EOF sequence"
            : ctx->zstd.LastErrorName();
        Warning(
            "[r1dc] ZSTD decompression failed (status=%d): %s\n",
            status,
            reason);
        ctx->errorReported = true;
    }

    return status;
}
// --------------------------------------------------------------------------
// Hook setup
// --------------------------------------------------------------------------
namespace
{

struct CompressionHookSpec
{
    uintptr_t rva;
    void* detour;
    void** original;
    const uint8_t* expected;
    size_t expectedSize;
    const char* name;
};

bool s_compressionHooksInstalled = false;
bool s_clientFileHooksCreated = false;

bool IsSuccessfulMinHookStatus(MH_STATUS status) noexcept
{
    return status == MH_OK
        || status == MH_ERROR_ALREADY_CREATED
        || status == MH_ERROR_ENABLED;
}

void LogCompressionHookStatus(
    const char* operation,
    const CompressionHookSpec& hook,
    void* target,
    MH_STATUS status)
{
    char buffer[320];
    _snprintf_s(
        buffer,
        sizeof(buffer),
        _TRUNCATE,
        "[r1delta_compression] %s %s target=%p status=%d original=%p\n",
        operation,
        hook.name,
        target,
        static_cast<int>(status),
        hook.original ? *hook.original : nullptr);
    OutputDebugStringA(buffer);
}

bool InstallCompressionHookSet(uintptr_t module, uintptr_t baseRva)
{
    static constexpr uint8_t kInitBytes[] = { 0xE9, 0xFB, 0xFE, 0x00, 0x00 };
    static constexpr uint8_t kReinitBytes[] = { 0xE9, 0x2B, 0x00, 0x01, 0x00 };
    static constexpr uint8_t kDeinitBytes[] = { 0xE9, 0x6B, 0x01, 0x01, 0x00 };
    static constexpr uint8_t kDecompressBytes[] = { 0x48, 0x83, 0xEC, 0x38 };

    CompressionHookSpec hooks[] = {
        {
            baseRva,
            reinterpret_cast<void*>(&r1dc_init),
            reinterpret_cast<void**>(&original_lzham_decompressor_init),
            kInitBytes,
            sizeof(kInitBytes),
            "lzham_decompress_init",
        },
        {
            baseRva + 0x10,
            reinterpret_cast<void*>(&r1dc_reinit),
            reinterpret_cast<void**>(&original_lzham_decompressor_reinit),
            kReinitBytes,
            sizeof(kReinitBytes),
            "lzham_decompress_reinit",
        },
        {
            baseRva + 0x20,
            reinterpret_cast<void*>(&r1dc_deinit),
            reinterpret_cast<void**>(&original_lzham_decompressor_deinit),
            kDeinitBytes,
            sizeof(kDeinitBytes),
            "lzham_decompress_deinit",
        },
        {
            baseRva + 0x30,
            reinterpret_cast<void*>(&r1dc_decompress),
            reinterpret_cast<void**>(&original_lzham_decompressor_decompress),
            kDecompressBytes,
            sizeof(kDecompressBytes),
            "lzham_decompress",
        },
    };

    for (const CompressionHookSpec& hook : hooks)
    {
        const void* target = reinterpret_cast<const void*>(module + hook.rva);
        if (memcmp(target, hook.expected, hook.expectedSize) != 0)
        {
            char buffer[320];
            _snprintf_s(
                buffer,
                sizeof(buffer),
                _TRUNCATE,
                "[r1delta_compression] skipped %s at module=%p rva=%llX because bytes did not match\n",
                hook.name,
                reinterpret_cast<void*>(module),
                static_cast<unsigned long long>(hook.rva));
            OutputDebugStringA(buffer);
            return false;
        }
    }

    bool created = true;
    for (const CompressionHookSpec& hook : hooks)
    {
        void* target = reinterpret_cast<void*>(module + hook.rva);
        const MH_STATUS status = MH_CreateHook(target, hook.detour, hook.original);
        LogCompressionHookStatus("create", hook, target, status);
        created = created
            && IsSuccessfulMinHookStatus(status)
            && hook.original
            && *hook.original;
    }
    if (!created)
        return false;

    bool enabled = true;
    for (const CompressionHookSpec& hook : hooks)
    {
        void* target = reinterpret_cast<void*>(module + hook.rva);
        const MH_STATUS status = MH_EnableHook(target);
        LogCompressionHookStatus("enable", hook, target, status);
        enabled = enabled && IsSuccessfulMinHookStatus(status);
    }
    return enabled;
}

} // namespace

void InitCompressionHooks()
{
    if (IsR1ODedicatedServer()
        || HasEngineCommandLineFlag("-r1delta_disable_compression_hooks")
        || s_compressionHooksInstalled)
        return;

    const bool dedicated = IsDedicatedServer();
    const uintptr_t module = dedicated ? G_vscript : G_filesystem_stdio;
    if (!module)
        return;

    const uintptr_t baseRva = dedicated ? 0x180090 : 0x75380;
    if (!InstallCompressionHookSet(module, baseRva))
        return;

    s_compressionHooksInstalled = true;

    if (!dedicated && !s_clientFileHooksCreated)
    {
        MH_CreateHook(LPVOID(module + 0x23860),
            Hooked_CBaseFileSystem__SyncRead,
            reinterpret_cast<LPVOID*>(&Original_CBaseFileSystem__SyncRead));
        MH_CreateHook(LPVOID(module + 0x23490),
            CFileAsyncReadJob_dtor,
            reinterpret_cast<LPVOID*>(&Original_CFileAsyncReadJob_dtor));
        s_clientFileHooksCreated = true;
    }

    // Preserve the original initialization point's behavior for unrelated
    // hooks that may have been queued immediately before this call.
    MH_EnableHook(MH_ALL_HOOKS);
}
