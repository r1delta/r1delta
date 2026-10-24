// Passive instrumentation for the exact R1 Client2015 DX11 material system.
// Provenance: timestamp 54874238, SizeOfImage 3E0000, SHA256
// 0d2337844afb0e6e3c2079638c798101bf25910d1360dd0f64bf4e1d9c6b78c8.
// RSDS 4791CBE9-9035-4CB1-939D-E2B283F8FBE4 age 17:
// c:\depot\r1pcstaging\game\bin\x64_retail\materialsystem_dx11.pdb.
// Installation is optional: unsupported bytes disable only the affected
// diagnostics, never native rendering. No COM calls or payload walks occur here.

#include "graphics_flight_recorder.h"

#include "core.h"
#include "logging.h"

#include <MinHook.h>
#include <Windows.h>
#include <intrin.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
namespace recorder = r1delta::graphics_flight_recorder;
using recorder::EventKind;
using recorder::ResourceDescription;

constexpr std::uint32_t kNativeTimestamp = 0x54874238;
constexpr std::uint32_t kNativeImageSize = 0x3E0000;
constexpr std::uintptr_t kCodeViewRva = 0x196A08;
constexpr std::uintptr_t kTextureBucketsRva = 0x2983F8;
constexpr std::uintptr_t kTextureBucketCountRva = 0x2983D4;
constexpr std::uint32_t kMaxTextureBuckets = 256;
constexpr std::uintptr_t kTextureRecordBytes = 72;
constexpr std::uintptr_t kTextureBucketHeaderBytes = 16;

// RecorderStarted coverage bits are stable even when an individual hook fails.
constexpr std::uint32_t kCreate2DCoverage = 1u << 0;
constexpr std::uint32_t kReleaseCoverage = 1u << 1;
constexpr std::uint32_t kQueueCoverage = 1u << 2;
constexpr std::uint32_t kExecuteCoverage = 1u << 3;
constexpr std::uint32_t kLevelShutdownCoverage = 1u << 4;
constexpr std::uint32_t kCreate1DCoverage = 1u << 5;
constexpr std::uint32_t kCreate3DCoverage = 1u << 6;
constexpr std::uint32_t kCreationCoverage =
    kCreate1DCoverage | kCreate2DCoverage | kCreate3DCoverage;
constexpr std::uint32_t kExpectedCoverage = kCreationCoverage | kReleaseCoverage
    | kQueueCoverage | kExecuteCoverage | kLevelShutdownCoverage;

constexpr unsigned char kCodeViewIdentity[] = {
    0x52, 0x53, 0x44, 0x53, 0xE9, 0xCB, 0x91, 0x47,
    0x35, 0x90, 0xB1, 0x4C, 0x93, 0x9D, 0xE2, 0xB2,
    0x83, 0xF8, 0xFB, 0xE4, 0x11, 0x00, 0x00, 0x00,
};

// Each sequence ends at an instruction boundary and covers the instructions
// MinHook relocates, plus additional entry instructions from this exact image.
constexpr unsigned char kCreate2DEntry[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x44, 0x89, 0x44, 0x24, 0x18,
    0x89, 0x54, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41,
    0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xF0,
    0x48, 0x81, 0xEC, 0x10, 0x01, 0x00, 0x00,
};
constexpr unsigned char kReleaseEntry[] = {
    0x40, 0x55, 0x56, 0x57, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
    0x48, 0x83, 0xEC, 0x28, 0xFF, 0x0D, 0x7C, 0x62, 0x28, 0x00,
    0x48, 0x8B, 0x05, 0x9D, 0x62, 0x28, 0x00, 0x44, 0x0F, 0xBF,
    0xF1, 0x41, 0xFF, 0xCE, 0x45, 0x8B, 0xFE, 0x41, 0x83, 0xE6,
    0x7F, 0x41, 0xC1, 0xEF, 0x07,
};
constexpr unsigned char kQueueEntry[] = {
    0x40, 0x55, 0x57, 0x41, 0x54, 0x48, 0x83, 0xEC, 0x20,
    0x41, 0x8B, 0xE8, 0x4C, 0x8B, 0xE2, 0x48, 0x8B, 0xF9,
    0xE8, 0xA9, 0xC6, 0xFA, 0xFF, 0x84, 0xC0,
};
constexpr unsigned char kExecuteEntry[] = {
    0x48, 0x8B, 0xC4, 0x48, 0x89, 0x50, 0x10, 0x57, 0x41, 0x54,
    0x48, 0x81, 0xEC, 0x08, 0x01, 0x00, 0x00, 0x48, 0x89, 0x70,
    0x20, 0x4C, 0x89, 0x68, 0xE8, 0x4C, 0x89, 0x70, 0xE0,
    0x4C, 0x89, 0x78, 0xD8, 0x48, 0x8B, 0xF9,
};
constexpr unsigned char kLevelShutdownEntry[] = {
    0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20,
    0x48, 0x63, 0xB9, 0x10, 0x09, 0x00, 0x00, 0x48, 0x8B, 0xF1,
};
constexpr unsigned char kCreate1DEntry[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10,
    0x48, 0x89, 0x7C, 0x24, 0x18, 0x4C, 0x89, 0x64, 0x24, 0x20,
    0x55, 0x41, 0x55, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xD1,
    0x48, 0x81, 0xEC, 0xB0, 0x00, 0x00, 0x00,
};
constexpr unsigned char kCreate3DEntry[] = {
    0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x68,
    0x10, 0x48, 0x89, 0x70, 0x18, 0x48, 0x89, 0x78, 0x20,
    0x41, 0x54, 0x48, 0x83, 0xEC, 0x70,
};

// All ABIs are Windows x64. The 1D function has mixed pointer/HRESULT return
// bits in RAX; retaining its native pointer-sized return does not interpret it.
using Create2DFunction = std::int64_t(__fastcall*)(std::int64_t* record,
    int width, int height, int format, int mipCount, std::uint32_t arraySize,
    int nativeFlags, std::int64_t initialData);
using Create1DFunction = char*(__fastcall*)(std::int64_t* record, int width,
    int format, int mipCount, std::uint32_t arraySize, int nativeFlags,
    std::int64_t initialData);
using Create3DFunction = std::int64_t(__fastcall*)(std::int64_t* record,
    int width, int height, int format, int mipCount, int nativeArraySize,
    std::uint32_t nativeFlags);
using ReleaseFunction = std::uint64_t*(__fastcall*)(std::int16_t handle);
using QueueFunction = char(__fastcall*)(std::int64_t texture,
    std::int64_t descriptorArray, int imageFormat);
using ExecuteFunction = std::int64_t(__fastcall*)(std::int64_t texture,
    std::int64_t descriptorArray, int format, int depth, int count,
    int nativeFlags);
using LevelShutdownFunction = std::int64_t(__fastcall*)(std::int64_t object);

Create2DFunction s_originalCreate2D;
Create1DFunction s_originalCreate1D;
Create3DFunction s_originalCreate3D;
ReleaseFunction s_originalRelease;
QueueFunction s_originalQueue;
ExecuteFunction s_originalExecute;
LevelShutdownFunction s_originalLevelShutdown;
std::atomic<std::uintptr_t> s_materialSystemBase{0};
std::atomic<std::uintptr_t> s_textureLookupBase{0};
std::atomic<bool> s_installAttempted{false};
static_assert(std::atomic<std::uintptr_t>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);

struct UploadContext
{
    std::uintptr_t texture;
    std::uintptr_t descriptorArray;
    std::uint64_t formatAndDepth;
    std::uint64_t countAndFlags;
};
thread_local const UploadContext* s_uploadContext = nullptr;

std::uint64_t Pack32(std::uint32_t low, std::uint32_t high) noexcept
{
    return low | (static_cast<std::uint64_t>(high) << 32);
}

std::uintptr_t CallerRva(void* address) noexcept
{
    const std::uintptr_t base = s_materialSystemBase.load(std::memory_order_relaxed);
    const std::uintptr_t caller = reinterpret_cast<std::uintptr_t>(address);
    return caller >= base && caller - base < kNativeImageSize ? caller - base : 0;
}

int DiagnosticReadExceptionFilter(unsigned long code) noexcept
{
    return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR
        ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

std::uintptr_t ResolveTextureKey(std::uintptr_t base, std::int16_t handle) noexcept
{
    // Native +1215B sign-extends CX before subtracting one. Nonpositive handles
    // therefore do NOT index the 256-bucket native pool. Never dereference them.
    if (!base || handle <= 0)
        return 0;
    const std::uint32_t index = static_cast<std::uint32_t>(
        static_cast<std::int32_t>(handle) - 1);
    const std::uint32_t bucketIndex = index >> 7;
    const std::uint32_t slot = index & 127;

    // +13CC0 grows/replaces Src in groups of 32. This is one bounded, best-effort
    // native snapshot, not a lock or retry: changed/unreadable state has no key.
    // Only diagnostic reads are protected; the original owner still receives
    // every handle unchanged, including one which will fault natively.
    __try
    {
        const auto* const countAddress = reinterpret_cast<const volatile std::uint32_t*>(
            base + kTextureBucketCountRva);
        const auto* const tableAddress = reinterpret_cast<const volatile std::uintptr_t*>(
            base + kTextureBucketsRva);
        const std::uint32_t count = *countAddress;
        const std::uintptr_t table = *tableAddress;
        if (!table || count > kMaxTextureBuckets || bucketIndex >= count)
            return 0;
        const auto* const buckets = reinterpret_cast<const volatile std::uintptr_t*>(table);
        const std::uintptr_t bucket = buckets[bucketIndex];
        if (!bucket || *tableAddress != table || *countAddress != count
            || buckets[bucketIndex] != bucket)
            return 0;
        // Do not test the allocation bit: a repeated retirement of an already
        // free slot must retain the last identity rather than hide that evidence.
        return bucket + kTextureBucketHeaderBytes + kTextureRecordBytes * slot;
    }
    __except (DiagnosticReadExceptionFilter(GetExceptionCode()))
    {
        return 0;
    }
}

std::uint32_t Texture2DViewLayout(std::uint32_t flags,
    std::uint32_t arraySize) noexcept
{
    // Match native branch precedence, including cube SRVs (a single COM view)
    // and the fact that a Texture2DArray can still have a single COM SRV.
    const bool shaderView = (flags & 0x400002u) != 0
        || (flags & 0x800008u) == 0 || (flags & 0x80000u) != 0;
    std::uint32_t layout = shaderView && (flags & 1u) == 0
        && (arraySize != 1 || (flags & 0x4000000u) != 0) ? 1u : 0u;
    if ((flags & 2u) != 0)
        return layout;
    if ((flags & 0x400000u) != 0)
        return layout | 2u;
    if ((flags & 8u) != 0)
        return layout | 4u;
    if ((flags & 0x800000u) != 0)
        return layout | 2u | 4u;
    return layout;
}

void RecordUploadBegin(const UploadContext& context, std::uintptr_t key = 0,
    std::uint64_t generation = 0) noexcept
{
    // key==0 is the execution boundary. A nonzero key is an actual creation
    // inside that execution, linking texture/queue arguments to a generation.
    recorder::Record(EventKind::TextureUploadBegin, key, generation,
        context.texture, context.descriptorArray,
        context.formatAndDepth, context.countAndFlags);
}

std::uint64_t BeginCreation(std::int64_t* record,
    const ResourceDescription& description, void* caller) noexcept
{
    const auto key = reinterpret_cast<std::uintptr_t>(record);
    const std::uint64_t generation = recorder::ResourceCreateBegin(
        key, description, CallerRva(caller));
    if (s_uploadContext)
        RecordUploadBegin(*s_uploadContext, key, generation);
    return generation;
}

void EndCreation(std::int64_t* record, std::uint64_t generation,
    ResourceDescription& description, std::int64_t result) noexcept
{
    // These scalar fields belong to the caller's live 72-byte record, including
    // the full-screen owner's stack-local record. Never follow COM/view arrays.
    description.resource = static_cast<std::uintptr_t>(record[0]);
    description.shaderView = static_cast<std::uintptr_t>(record[1]);
    description.targetView = static_cast<std::uintptr_t>(record[4]);
    recorder::ResourceCreateEnd(reinterpret_cast<std::uintptr_t>(record),
        generation, description, result);
}

std::int64_t __fastcall CreateTexture2D(std::int64_t* record, int width,
    int height, int format, int mipCount, std::uint32_t arraySize,
    int nativeFlags, std::int64_t initialData)
{
    ResourceDescription description;
    description.width = static_cast<std::uint32_t>(width);
    description.height = static_cast<std::uint32_t>(height);
    description.depth = 1;
    description.format = static_cast<std::uint32_t>(format);
    description.mipCount = static_cast<std::uint32_t>(mipCount);
    description.arraySize = arraySize;
    description.flags = static_cast<std::uint32_t>(nativeFlags);
    description.viewLayout = Texture2DViewLayout(description.flags, arraySize);
    description.dimension = 2;
    const std::uint64_t generation = BeginCreation(record, description, _ReturnAddress());
    const std::int64_t result = s_originalCreate2D(record, width, height, format,
        mipCount, arraySize, nativeFlags, initialData);
    EndCreation(record, generation, description, result);
    return result;
}

char* __fastcall CreateTexture1D(std::int64_t* record, int width, int format,
    int mipCount, std::uint32_t arraySize, int nativeFlags,
    std::int64_t initialData)
{
    ResourceDescription description;
    description.width = static_cast<std::uint32_t>(width);
    description.height = 1;
    description.depth = 1;
    description.format = static_cast<std::uint32_t>(format);
    description.mipCount = static_cast<std::uint32_t>(mipCount);
    description.arraySize = arraySize;
    description.flags = static_cast<std::uint32_t>(nativeFlags);
    description.dimension = 1;
    // Unlike +132B0, +13030 always writes a single SRV at record+8, even
    // for a 1D array. Its optional record+32 RTV is also a single COM view.
    const std::uint64_t generation = BeginCreation(record, description, _ReturnAddress());
    char* const result = s_originalCreate1D(record, width, format, mipCount,
        arraySize, nativeFlags, initialData);
    EndCreation(record, generation, description, reinterpret_cast<std::intptr_t>(result));
    return result;
}

std::int64_t __fastcall CreateTexture3D(std::int64_t* record, int width,
    int height, int format, int mipCount, int nativeArraySize,
    std::uint32_t nativeFlags)
{
    ResourceDescription description;
    description.width = static_cast<std::uint32_t>(width);
    description.height = static_cast<std::uint32_t>(height);
    // +1397B/+1397F write R8D to both Height and Depth. The sixth argument
    // is native accounting input, not a D3D11 Texture3D array dimension.
    description.depth = static_cast<std::uint32_t>(height);
    description.format = static_cast<std::uint32_t>(format);
    description.mipCount = static_cast<std::uint32_t>(mipCount);
    description.arraySize = static_cast<std::uint32_t>(nativeArraySize);
    description.flags = nativeFlags;
    description.dimension = 3;
    const std::uint64_t generation = BeginCreation(record, description, _ReturnAddress());
    const std::int64_t result = s_originalCreate3D(record, width, height, format,
        mipCount, nativeArraySize, nativeFlags);
    EndCreation(record, generation, description, result);
    return result;
}

std::uint64_t* __fastcall ReleaseTexture(std::int16_t handle)
{
    const std::uintptr_t key = ResolveTextureKey(
        s_materialSystemBase.load(std::memory_order_relaxed), handle);
    const std::uint64_t generation = recorder::ResourceGeneration(key);
    const std::uintptr_t caller = CallerRva(_ReturnAddress());
    if (key)
        recorder::ResourceReleaseBegin(key, generation, caller);
    else
        recorder::Record(EventKind::TextureReleaseBegin, 0, 0, caller,
            static_cast<std::uint16_t>(handle));
    std::uint64_t* const result = s_originalRelease(handle);
    if (key)
        recorder::ResourceReleaseEnd(key, generation);
    else
        recorder::Record(EventKind::TextureReleaseEnd, 0, 0,
            static_cast<std::uint16_t>(handle), reinterpret_cast<std::uintptr_t>(result));
    return result;
}

char __fastcall QueueTextureUpload(std::int64_t texture,
    std::int64_t descriptorArray, int imageFormat)
{
    const auto caller = static_cast<std::uint32_t>(CallerRva(_ReturnAddress()));
    // +68440 can decline without touching the texture. Do not read its fields
    // here: +660B0/+16820 have not allocated/written its +72 handle array yet.
    // value4: high32 caller RVA, low32 UINT32_MAX while pending, then native AL.
    recorder::Record(EventKind::TextureUploadQueued, 0, 0,
        static_cast<std::uintptr_t>(texture), static_cast<std::uintptr_t>(descriptorArray),
        static_cast<std::uint32_t>(imageFormat), Pack32(UINT32_MAX, caller));
    const char result = s_originalQueue(texture, descriptorArray, imageFormat);
    recorder::Record(EventKind::TextureUploadQueued, 0, 0,
        static_cast<std::uintptr_t>(texture), static_cast<std::uintptr_t>(descriptorArray),
        static_cast<std::uint32_t>(imageFormat), Pack32(static_cast<unsigned char>(result), caller));
    return result;
}

std::int64_t __fastcall ExecuteTextureUpload(std::int64_t texture,
    std::int64_t descriptorArray, int format, int depth, int count,
    int nativeFlags)
{
    const UploadContext context{
        static_cast<std::uintptr_t>(texture), static_cast<std::uintptr_t>(descriptorArray),
        Pack32(static_cast<std::uint32_t>(format), static_cast<std::uint32_t>(depth)),
        Pack32(static_cast<std::uint32_t>(count), static_cast<std::uint32_t>(nativeFlags)),
    };
    const UploadContext* const previous = s_uploadContext;
    RecordUploadBegin(context);
    s_uploadContext = &context;
    std::int64_t result;
    // Restore nested TLS correlation even if native code unwinds. This does not
    // catch/suppress an exception, skip work, or emit a fabricated end event.
    __try
    {
        result = s_originalExecute(texture, descriptorArray, format, depth, count, nativeFlags);
    }
    __finally
    {
        s_uploadContext = previous;
    }
    recorder::Record(EventKind::TextureUploadEnd, 0, 0,
        context.texture, context.descriptorArray, CallerRva(_ReturnAddress()),
        static_cast<std::uint64_t>(result));
    return result;
}

std::int64_t __fastcall LevelShutdown(std::int64_t object)
{
    recorder::ResetBoundary(EventKind::LevelShutdownBegin, static_cast<std::uintptr_t>(object));
    const std::int64_t result = s_originalLevelShutdown(object);
    recorder::ResetBoundary(EventKind::LevelShutdownEnd, static_cast<std::uintptr_t>(object));
    return result;
}

struct HookDefinition
{
    const char* name;
    std::uintptr_t rva;
    const unsigned char* expected;
    std::size_t expectedSize;
    void* detour;
    void** original;
    std::uint32_t coverage;
};

bool IsExpectedModule(std::uintptr_t base, std::uint32_t& timestamp,
    std::uint32_t& imageSize) noexcept
{
    if (!base)
        return false;
    __try
    {
        const auto* const dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0
            || dos->e_lfanew > 0x1000 - sizeof(IMAGE_NT_HEADERS64))
            return false;
        const auto* const nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        timestamp = nt->FileHeader.TimeDateStamp;
        imageSize = nt->OptionalHeader.SizeOfImage;
        return nt->Signature == IMAGE_NT_SIGNATURE
            && nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64
            && nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC
            && timestamp == kNativeTimestamp && imageSize == kNativeImageSize
            && std::memcmp(reinterpret_cast<const void*>(base + kCodeViewRva),
                kCodeViewIdentity, sizeof(kCodeViewIdentity)) == 0;
    }
    __except (DiagnosticReadExceptionFilter(GetExceptionCode()))
    {
        return false;
    }
}

bool MatchesEntry(std::uintptr_t base, const HookDefinition& hook) noexcept
{
    __try
    {
        return std::memcmp(reinterpret_cast<const void*>(base + hook.rva),
            hook.expected, hook.expectedSize) == 0;
    }
    __except (DiagnosticReadExceptionFilter(GetExceptionCode()))
    {
        return false;
    }
}

void LogHookResult(const HookDefinition& hook, std::uintptr_t base,
    const char* result, int created = -1, int enabled = -1, int removed = -1)
{
    char buffer[384]{};
    _snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
        "R1Delta: graphics flight recorder %s RVA=0x%llX result=%s "
        "create=%d enable=%d remove=%d target=%p original=%p\n",
        hook.name, static_cast<unsigned long long>(hook.rva), result,
        created, enabled, removed,
        base ? reinterpret_cast<void*>(base + hook.rva) : nullptr, *hook.original);
    // Installation only. The detours and identity helper never print/log.
    OutputDebugStringA(buffer);
    Msg("%s", buffer);
}

bool InstallOptionalHook(std::uintptr_t base, const HookDefinition& hook)
{
    if (!MatchesEntry(base, hook))
    {
        LogHookResult(hook, base, "entry-mismatch");
        return false;
    }
    void* const target = reinterpret_cast<void*>(base + hook.rva);
    const MH_STATUS created = MH_CreateHook(target, hook.detour, hook.original);
    if (created != MH_OK)
    {
        // An existing hook is not ours; do not enable, remove or adopt it.
        LogHookResult(hook, base, "create-failed", static_cast<int>(created));
        return false;
    }
    const MH_STATUS enabled = *hook.original ? MH_EnableHook(target) : MH_ERROR_NOT_CREATED;
    if (*hook.original && (enabled == MH_OK || enabled == MH_ERROR_ENABLED))
    {
        LogHookResult(hook, base, "installed", static_cast<int>(created), static_cast<int>(enabled));
        return true;
    }
    // Do not leave a disabled optional hook for a later MH_ALL_HOOKS call to
    // activate after we have reported it missing. Remove only our own hook.
    const MH_STATUS removed = MH_RemoveHook(target);
    LogHookResult(hook, base, "enable-failed", static_cast<int>(created),
        static_cast<int>(enabled), static_cast<int>(removed));
    return false;
}
} // namespace

std::uintptr_t GraphicsFlightRecorderTextureKey(std::int16_t handle) noexcept
{
    return ResolveTextureKey(s_textureLookupBase.load(std::memory_order_acquire), handle);
}

void InstallGraphicsFlightRecorder(std::uintptr_t materialSystemBase)
{
    if (GetR1DeltaEngineMode() != R1DeltaEngineMode::Client2015)
        return;
    bool expected = false;
    if (!s_installAttempted.compare_exchange_strong(expected, true, std::memory_order_relaxed))
        return;

    const HookDefinition hooks[] = {
        {"texture2D-create", 0x132B0, kCreate2DEntry, sizeof(kCreate2DEntry),
            reinterpret_cast<void*>(&CreateTexture2D), reinterpret_cast<void**>(&s_originalCreate2D), kCreate2DCoverage},
        {"texture-owner-release", 0x12140, kReleaseEntry, sizeof(kReleaseEntry),
            reinterpret_cast<void*>(&ReleaseTexture), reinterpret_cast<void**>(&s_originalRelease), kReleaseCoverage},
        {"texture-upload-queue", 0x68440, kQueueEntry, sizeof(kQueueEntry),
            reinterpret_cast<void*>(&QueueTextureUpload), reinterpret_cast<void**>(&s_originalQueue), kQueueCoverage},
        {"texture-upload-execute", 0x660B0, kExecuteEntry, sizeof(kExecuteEntry),
            reinterpret_cast<void*>(&ExecuteTextureUpload), reinterpret_cast<void**>(&s_originalExecute), kExecuteCoverage},
        {"level-shutdown", 0x4BE50, kLevelShutdownEntry, sizeof(kLevelShutdownEntry),
            reinterpret_cast<void*>(&LevelShutdown), reinterpret_cast<void**>(&s_originalLevelShutdown), kLevelShutdownCoverage},
        {"texture1D-create", 0x13030, kCreate1DEntry, sizeof(kCreate1DEntry),
            reinterpret_cast<void*>(&CreateTexture1D), reinterpret_cast<void**>(&s_originalCreate1D), kCreate1DCoverage},
        {"texture3D-create", 0x13950, kCreate3DEntry, sizeof(kCreate3DEntry),
            reinterpret_cast<void*>(&CreateTexture3D), reinterpret_cast<void**>(&s_originalCreate3D), kCreate3DCoverage},
    };

    const bool recorderReady = recorder::Initialize();
    std::uint32_t timestamp = 0;
    std::uint32_t imageSize = 0;
    const bool supported = IsExpectedModule(materialSystemBase, timestamp, imageSize);
    std::uint32_t installed = 0;
    if (supported && recorderReady)
    {
        s_materialSystemBase.store(materialSystemBase, std::memory_order_relaxed);
        for (const HookDefinition& hook : hooks)
        {
            // Each valid hook is created AND immediately enabled independently.
            if (InstallOptionalHook(materialSystemBase, hook))
                installed |= hook.coverage;
        }
        if ((installed & kReleaseCoverage) != 0 && (installed & kCreationCoverage) != 0)
            s_textureLookupBase.store(materialSystemBase, std::memory_order_release);
    }
    else
    {
        for (const HookDefinition& hook : hooks)
            LogHookResult(hook, materialSystemBase,
                supported ? "recorder-unavailable" : "unsupported-image");
    }
    // value4 high32 timestamp / low32 SizeOfImage. All seven expected bits are
    // recorded even for an unsupported image; missing coverage is not success.
    recorder::Record(EventKind::RecorderStarted, 0, 0, installed,
        kExpectedCoverage, materialSystemBase, Pack32(imageSize, timestamp));
}
