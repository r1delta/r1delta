#include "movement_checkpoints.h"
#include <Windows.h>
#include <MinHook.h>
#include <intrin.h>
#include <xmmintrin.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace r1delta::movement
{
namespace
{
constexpr unsigned kClient = 0, kAuthority = 1;
constexpr char kImageDigest[2][65] = {
    "5b3f00805568eeaa6ee1cf889f12dc4570eb28a3fad2626ec0578eae3e7afd5e",
    "50faad928e0715a51ad624d0a3eb8f00f7ffad9e66258dc95c960f9d11ff41e4"
};

// Recovered from the exact R1 client and shared server images above. These are
// binary ABI facts, not C++ object definitions usable with another engine build.
struct PlayerLayout
{
    std::size_t duckState, duckTimer, crouchToggle, stance, gravityUp;
    std::size_t surfaceFriction, wallNormal, waterJump;
    std::uintptr_t groundGetter, decelerationGetter;
};
constexpr PlayerLayout kLayout[2] = {
    {0x2D6C, 0x26B4, 0x2718, 0x2714, 0x2D74, 0x3968, 0x3564, 0x2CEC,
        0x1F31C0, 0x1655B0},
    {0x1A10, 0xD9C, 0xD90, 0xD94, 0x1A24, 0x1E40, 0x1B2C, 0x1B50,
        0x4AAA70, 0x14CFE0}
};
struct Site
{
    std::uintptr_t rva;
    unsigned char bytes[16];
};
enum FunctionIndex : unsigned { FrictionFunction, DuckFunction, JumpFunction, GroundFunction, WalkFunction,
    FractionFunction, PlayerScaleFunction, CanSprintFunction, FunctionCount };
enum PointIndex : unsigned { DuckRequestPoint, DuckDurationPoint, DuckReverseUpPoint, DuckReverseDownPoint, JumpHeightPoint,
    AccelerationPoint, DecelerationPoint, GroundPublishPoint, StartGravityPoint, FinishGravityPoint, DuckFractionPoint, PointCount };

constexpr Site kFunctions[2][FunctionCount] = {
    {
        {0x339E20, {0x48,0x8B,0xC4,0x53,0x48,0x81,0xEC,0xB0,0x00,0x00,0x00,0x48,0x8B,0xD9,0x48,0x8B}},
        {0x342ED0, {0x40,0x53,0x56,0x57,0x48,0x81,0xEC,0x90,0x00,0x00,0x00,0x48,0x8B,0x41,0x10,0x48}},
        {0x3455E0, {0x40,0x55,0x53,0x57,0x48,0x8D,0xAC,0x24,0x50,0xFF,0xFF,0xFF,0x48,0x81,0xEC,0xB0}},
        {0x33B830, {0x48,0x8B,0xC4,0x48,0x89,0x58,0x18,0x56,0x48,0x81,0xEC,0x90,0x00,0x00,0x00,0x48}},
        {0x341240, {0x48,0x8B,0xC4,0x48,0x89,0x58,0x10,0x48,0x89,0x70,0x18,0x55,0x57,0x41,0x55,0x48}},
        {0x037200, {0x0F,0x57,0xC0,0x66,0x0F,0x6E,0xC9,0x0F,0x5B,0xC9,0xF3,0x0F,0x59,0x0D,0xA6,0x26}},
        {0x33A920, {0x48,0x83,0xEC,0x38,0x48,0x8B,0x05,0x15,0xFD,0x7A,0x00,0xF3,0x0F,0x10,0x81,0x50}},
        {0x03D370, {0x40,0x53,0x48,0x83,0xEC,0x40,0x80,0xB9,0x2B,0x2C,0x00,0x00,0x00,0x48,0x8B,0xD9}}
    },
    {
        {0x5355E0, {0x48,0x8B,0xC4,0x53,0x48,0x81,0xEC,0xB0,0x00,0x00,0x00,0x48,0x8B,0xD9,0x44,0x0F}},
        {0x53A110, {0x40,0x53,0x56,0x57,0x48,0x81,0xEC,0x30,0x01,0x00,0x00,0x48,0x8B,0x41,0x10,0x48}},
        {0x52FE30, {0x40,0x55,0x53,0x48,0x8D,0xAC,0x24,0x28,0xFF,0xFF,0xFF,0x48,0x81,0xEC,0xD8,0x01}},
        {0x532660, {0x48,0x8B,0xC4,0x48,0x89,0x58,0x18,0x55,0x48,0x81,0xEC,0x90,0x00,0x00,0x00,0x48}},
        {0x5363A0, {0x48,0x8B,0xC4,0x48,0x89,0x58,0x18,0x48,0x89,0x70,0x20,0x55,0x57,0x41,0x54,0x48}},
        {},
        {0x52D5F0, {0x48,0x83,0xEC,0x38,0x48,0x8B,0x05,0xC5,0x3A,0x70,0x00,0xF3,0x0F,0x10,0x81,0xA0}},
        {0x4DC3C0, {0x40,0x53,0x48,0x83,0xEC,0x50,0x80,0xB9,0x83,0x14,0x00,0x00,0x00,0x48,0x8B,0xD9}}
    }
};
constexpr Site kPoints[2][PointCount] = {
    {
        {0x34302E, {0x40,0x84,0xFF,0x0F,0x85,0x7A,0x01,0x00,0x00,0x40,0x84,0xF6,0x0F,0x85,0x7A,0x01}},
        {0x343250, {0x8B,0x88,0x6C,0x2D,0x00,0x00,0x85,0xC9,0x75,0x43,0xC7,0x80,0x6C,0x2D,0x00,0x00}},
        {0x343106, {0x2B,0x88,0xB4,0x26,0x00,0x00,0xE8,0xEF,0x40,0xCF,0xFF,0xF3,0x0F,0x59,0x05,0xB7}},
        {0x3432CC, {0xF3,0x0F,0x2C,0xC8,0x89,0x88,0xB4,0x26,0x00,0x00,0x48,0x8B,0x43,0x08,0x83,0xB8}},
        {0x345ED5, {0x48,0x39,0xB0,0x60,0x39,0x00,0x00,0x0F,0x84,0x85,0x00,0x00,0x00,0x8B,0x88,0xD0}},
        {0x165577, {0x48,0x8B,0x05,0xC2,0x50,0x98,0x00,0x48,0x8B,0x4B,0x08,0xF3,0x0F,0x10,0x48,0x10}},
        {0x16564B, {0x48,0x8B,0x05,0xEE,0x4F,0x98,0x00,0x48,0x8B,0x4F,0x08,0xF3,0x0F,0x10,0x48,0x10}},
        {0x33BAFC, {0xE8,0xBF,0xB4,0xEB,0xFF,0x44,0x0F,0x28,0x5C,0x24,0x30,0x44,0x0F,0x28,0x54,0x24}},
        {0x3398BF, {0x0F,0x28,0xC6,0x0F,0x28,0xCE,0xF3,0x0F,0x59,0xB3,0xFC,0x03,0x00,0x00,0xF3,0x0F}},
        {0x339CDC, {0x0F,0x28,0xC6,0x0F,0x28,0xCE,0xF3,0x0F,0x59,0xB3,0xFC,0x03,0x00,0x00,0xF3,0x0F}},
        {}
    },
    {
        {0x53A33E, {0x40,0x84,0xF6,0x0F,0x85,0x3F,0x02,0x00,0x00,0x40,0x84,0xED,0x0F,0x85,0x3F,0x02}},
        {0x53A63C, {0x0F,0x57,0xF6,0x85,0xC0,0x75,0x74,0x83,0xF8,0x01,0x74,0x2F,0x38,0x47,0x70,0x74}},
        {0x53A4B7, {0x0F,0x2F,0xC6,0x73,0x05,0x0F,0x28,0xC6,0xEB,0x04,0xF3,0x0F,0x5D,0xC7,0xF3,0x0F}},
        {0x53A729, {0xF3,0x0F,0x2C,0xE8,0x39,0xAF,0x9C,0x0D,0x00,0x00,0x74,0x1A,0x48,0x8D,0x8F,0x78}},
        {0x5306E6, {0x48,0x85,0xC0,0x0F,0x84,0x00,0x01,0x00,0x00,0x41,0x8B,0x90,0x80,0x13,0x00,0x00}},
        {0x14CFB2, {0x48,0x8B,0x5B,0x08,0x48,0x8B,0x05,0x03,0x41,0xAE,0x00,0x48,0x8B,0xCB,0xF3,0x0F}},
        {0x14D096, {0x48,0x8B,0x5B,0x08,0x48,0x8B,0x05,0x1F,0x40,0xAE,0x00,0x48,0x8B,0xCB,0xF3,0x0F}},
        {0x5329FE, {0x48,0x8B,0x4B,0x08,0x48,0x8B,0xD7,0xE8,0xD6,0x7E,0xF7,0xFF,0x44,0x0F,0x28,0x44}},
        {0x5341C8, {0x0F,0x28,0xC5,0x0F,0x28,0xCD,0xF3,0x0F,0x59,0x87,0xF4,0x03,0x00,0x00,0xF3,0x0F}},
        {0x5345E5, {0x0F,0x28,0xC5,0x0F,0x28,0xCD,0xF3,0x0F,0x59,0x82,0xF4,0x03,0x00,0x00,0xF3,0x0F}},
        {0x53A781, {0x0F,0x2F,0xC6,0x72,0x07,0x0F,0x28,0xF0,0xF3,0x0F,0x5D,0xF7,0xF3,0x0F,0x10,0x0D}}
    }
};
constexpr Site kReaders[2][3] = {
    {
        {0x1F31C0, {0x8B,0x81,0xB4,0x01,0x00,0x00,0x83,0xF8,0xFF,0x74,0x27,0x0F,0xB7,0xC8,0x81,0xF9}},
        {0x1655B0, {0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xF9,0x48,0x8B,0x49}},
        {0x3432FB, {0x2B,0xF9,0x8B,0xCF,0xE8,0xFC,0x3E,0xCF,0xFF,0xF3,0x0F,0x10,0x0D,0xB8,0x1D,0x4B}}
    },
    {
        {0x4AAA70, {0x8B,0x91,0x68,0x02,0x00,0x00,0x83,0xFA,0xFF,0x74,0x26,0x0F,0xB7,0xC2,0x3D,0x00}},
        {0x14CFE0, {0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0x41,0x08,0x48,0x8B,0xD9,0x83,0xB8,0x34}},
        {}
    }
};

template<class T> T& Field(std::uintptr_t base, std::size_t offset) noexcept
{
    return *reinterpret_cast<T*>(base + offset);
}
bool Finite(const MoveVector& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
bool ValidOverrides(const CheckpointOverrides& value) noexcept
{
    return (!value.accelerationSet || (std::isfinite(value.acceleration) && value.acceleration >= 0.0f))
        && (!value.decelerationSet || (std::isfinite(value.deceleration) && value.deceleration >= 0.0f))
        && std::isfinite(value.frictionScale) && value.frictionScale >= 0.0f
        && std::isfinite(value.moveScale) && value.moveScale >= 0.0f
        && value.duckMilliseconds >= 0 && value.duckMilliseconds <= kMaximumDuckTransitionMilliseconds;
}
bool Reject(unsigned side, std::uintptr_t rva, const char* operation, int status = 0) noexcept
{
    char message[192];
    std::snprintf(message, sizeof(message), "[movement checkpoints] %s RVA 0x%llX: %s (%d).\n",
        side == kClient ? "client" : "authority", static_cast<unsigned long long>(rva), operation, status);
    OutputDebugStringA(message);
    return false;
}

struct Hook
{
    void* target{};
    void* original{};
    void* gateway{};
};
struct Adapter
{
    std::uintptr_t module{};
    std::array<Hook, FunctionCount> functions;
    std::array<Hook, PointCount> points;
    std::atomic<unsigned> scopes{};
    bool ready{};
};
Adapter adapters[2];
std::mutex installMutex;
thread_local MovementCheckpointScope* currentScope{};

// Instruction-boundary context, deliberately distinct from a function ABI.
// All guarded sites have a 16-byte-aligned RSP. The gateway preserves all 15
// non-RSP GPRs, RFLAGS and XMM0..15. No overwritten instruction is handwritten:
// MinHook decodes and relocates whole instructions into its trampoline.
struct alignas(16) CpuFrame
{
    float xmm[16][4];
    std::uint64_t rcx, rdx, rbx, rbp, rsi, rdi;
    std::uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    std::uint64_t rax, flags;
};
static_assert(sizeof(CpuFrame) == 384 && offsetof(CpuFrame, rax) == 368);
using PointCallback = void (__fastcall*)(CpuFrame*, unsigned) noexcept;

bool PrepareGateway(Hook& hook, std::uintptr_t address, PointCallback callback, unsigned id) noexcept
{
    constexpr std::size_t allocationSize = 4096;
    auto* code = static_cast<unsigned char*>(VirtualAlloc(nullptr, allocationSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!code) return false;
    hook.gateway = code;
    std::size_t length{};
    auto emit = [&](unsigned byte) { code[length++] = static_cast<unsigned char>(byte); };
    auto immediate32 = [&](std::uint32_t value) { std::memcpy(code + length, &value, sizeof(value)); length += sizeof(value); };
    auto immediate64 = [&](std::uint64_t value) { std::memcpy(code + length, &value, sizeof(value)); length += sizeof(value); };
    // RAX/RFLAGS occupy the final two frame slots. Reserve shadow space and the
    // remaining frame below them; 16+400 bytes preserves the site's alignment.
    emit(0x9C); emit(0x50);
    emit(0x48); emit(0x81); emit(0xEC); immediate32(400);
    constexpr unsigned registers[] = {1,2,3,5,6,7,8,9,10,11,12,13,14,15};
    auto general = [&](unsigned slot, bool restore)
    {
        const unsigned reg = registers[slot];
        emit(reg < 8 ? 0x48 : 0x4C);
        emit(restore ? 0x8B : 0x89);
        emit(0x84 | ((reg & 7) << 3)); emit(0x24);
        immediate32(32 + 256 + slot * 8);
    };
    auto vector = [&](unsigned reg, bool restore)
    {
        emit(0xF3);
        if (reg >= 8) emit(0x44);
        emit(0x0F); emit(restore ? 0x6F : 0x7F);
        emit(0x84 | ((reg & 7) << 3)); emit(0x24);
        immediate32(32 + reg * 16);
    };
    for (unsigned slot = 0; slot != std::size(registers); ++slot) general(slot, false);
    for (unsigned reg = 0; reg != 16; ++reg) vector(reg, false);
    emit(0x48); emit(0x8D); emit(0x4C); emit(0x24); emit(0x20); // RCX = frame
    emit(0xBA); immediate32(id);
    emit(0x48); emit(0xB8); immediate64(reinterpret_cast<std::uintptr_t>(callback));
    emit(0xFF); emit(0xD0);
    for (unsigned reg = 0; reg != 16; ++reg) vector(reg, true);
    for (unsigned slot = 0; slot != std::size(registers); ++slot) general(slot, true);
    emit(0x48); emit(0x81); emit(0xC4); immediate32(400);
    emit(0x58); emit(0x9D);
    emit(0xFF); emit(0x25); immediate32(0);
    const auto destination = length;
    immediate64(0);
    DWORD protection{};
    // MinHook requires an executable destination at creation. The disabled
    // gateway is reopened RW only to fill its trampoline, then published RX.
    if (!VirtualProtect(code, allocationSize, PAGE_EXECUTE_READ, &protection)) return false;
    if (MH_CreateHook(reinterpret_cast<void*>(address), code, &hook.original) != MH_OK) return false;
    hook.target = reinterpret_cast<void*>(address);
    if (!VirtualProtect(code, allocationSize, PAGE_READWRITE, &protection)) return false;
    std::memcpy(code + destination, &hook.original, sizeof(hook.original));
    return VirtualProtect(code, allocationSize, PAGE_EXECUTE_READ, &protection)
        && FlushInstructionCache(GetCurrentProcess(), code, length);
}

template<class Visitor> void ForEachHook(Adapter& adapter, Visitor visit)
{
    for (auto& hook : adapter.functions) visit(hook);
    for (auto& hook : adapter.points) visit(hook);
}
bool RemoveHooks(unsigned side, Adapter& adapter) noexcept
{
    adapter.ready = false;
    bool disabled = true;
    ForEachHook(adapter, [&](Hook& hook)
    {
        if (!hook.target) return;
        const auto status = MH_DisableHook(hook.target);
        if (status != MH_OK && status != MH_ERROR_DISABLED && status != MH_ERROR_NOT_CREATED)
        {
            Reject(side, reinterpret_cast<std::uintptr_t>(hook.target) - adapter.module, "disable failed; retaining gateway", status);
            disabled = false;
        }
    });
    if (!disabled) return false;
    bool removed = true;
    ForEachHook(adapter, [&](Hook& hook)
    {
        if (hook.target)
        {
            const auto status = MH_RemoveHook(hook.target);
            if (status != MH_OK && status != MH_ERROR_NOT_CREATED)
            {
                Reject(side, reinterpret_cast<std::uintptr_t>(hook.target) - adapter.module, "remove failed; retaining gateway", status);
                removed = false;
                return;
            }
            hook.target = nullptr;
            hook.original = nullptr;
        }
        if (hook.gateway && !VirtualFree(hook.gateway, 0, MEM_RELEASE))
        {
            Reject(side, 0, "gateway release failed", static_cast<int>(GetLastError()));
            removed = false;
            return;
        }
        hook = {};
    });
    if (removed) adapter.module = 0;
    return removed;
}
bool MatchSite(unsigned side, std::uintptr_t module, std::size_t imageSize, const Site& site) noexcept
{
    if (!site.rva) return true;
    if (site.rva > imageSize || sizeof(site.bytes) > imageSize - site.rva
        || std::memcmp(reinterpret_cast<const void*>(module + site.rva), site.bytes, sizeof(site.bytes)))
        return Reject(side, site.rva, "native bytes differ");
    return true;
}
bool MatchImage(unsigned side, std::uintptr_t module) noexcept
{
    HMODULE loaded{};
    if (!module || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(module), &loaded) || reinterpret_cast<std::uintptr_t>(loaded) != module)
        return Reject(side, 0, "not a loaded module base");
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 4096)
        return Reject(side, 0, "invalid PE header");
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64
        || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC
        || nt->FileHeader.TimeDateStamp != (side == kClient ? 0x55038DB8u : 0x58E93EDFu)
        || !VerifyMovementModuleDigest(module, kImageDigest[side]))
        return Reject(side, 0, "unsupported native image");
    for (const auto& site : kFunctions[side]) if (!MatchSite(side, module, nt->OptionalHeader.SizeOfImage, site)) return false;
    for (const auto& site : kPoints[side]) if (!MatchSite(side, module, nt->OptionalHeader.SizeOfImage, site)) return false;
    for (const auto& site : kReaders[side]) if (!MatchSite(side, module, nt->OptionalHeader.SizeOfImage, site)) return false;
    return true;
}
}

struct CheckpointDispatch
{
    static MovementCheckpointScope* Active(unsigned side, std::uintptr_t movement) noexcept
    {
        auto* scope = currentScope;
        if (!scope || !scope->active_ || scope->failed_ || scope->inCallback_
            || static_cast<unsigned>(scope->domain_) != side || scope->movement_ != movement)
            return nullptr;
        if (Field<std::uintptr_t>(movement, 8) != scope->player_ || Field<std::uintptr_t>(movement, 16) != scope->moveData_
            || !ValidOverrides(*scope->overrides_))
        {
            scope->failed_ = true;
            return nullptr;
        }
        return scope;
    }

    static bool ReadPlayer(MovementDomain domain, std::uintptr_t player, CheckpointPlayerContext& result) noexcept
    {
        const auto side = static_cast<unsigned>(domain);
        if (side > kAuthority || !adapters[side].ready || !player) return false;
        const auto& layout = kLayout[side];
        result.wallNormal = Field<MoveVector>(player, layout.wallNormal);
        result.gravityUp = Field<MoveVector>(player, layout.gravityUp);
        result.surfaceFriction = Field<float>(player, layout.surfaceFriction);
        result.waterJumpTime = Field<float>(player, layout.waterJump);
        result.duckState = Field<int>(player, layout.duckState);
        result.duckMillisecondsRemaining = Field<int>(player, layout.duckTimer);
        result.stance = Field<int>(player, layout.stance);
        result.crouchToggled = Field<unsigned char>(player, layout.crouchToggle) != 0;
        result.worldUp = result.gravityUp.x == 0.0f && result.gravityUp.y == 0.0f && result.gravityUp.z == 1.0f;
        result.grounded = reinterpret_cast<std::uintptr_t(__fastcall*)(std::uintptr_t)>(
            adapters[side].module + layout.groundGetter)(player) != 0;
        result.moveScale = reinterpret_cast<float(__fastcall*)(std::uintptr_t)>(
            adapters[side].functions[PlayerScaleFunction].original)(player);
        const auto* scope = currentScope;
        result.groundNormalKnown = scope && scope->active_ && scope->domain_ == domain && scope->player_ == player
            && scope->groundNormalKnown_;
        result.groundNormal = result.groundNormalKnown ? scope->groundNormal_ : MoveVector{};
        result.groundSurface = result.groundNormalKnown ? scope->groundSurface_ : -1;
        return Finite(result.wallNormal) && Finite(result.gravityUp) && std::isfinite(result.surfaceFriction)
            && std::isfinite(result.moveScale) && std::isfinite(result.waterJumpTime);
    }

    static bool Context(MovementCheckpointScope& scope, CheckpointStage stage, CheckpointContext& context) noexcept
    {
        if (!(scope.overrides_->checkpointMask & (1u << static_cast<unsigned>(stage)))) return false;
        if (!ReadPlayer(scope.domain_, scope.player_, context))
        {
            scope.failed_ = true;
            return false;
        }
        context.domain = scope.domain_;
        context.stage = stage;
        context.player = scope.player_;
        context.movement = scope.movement_;
        context.moveData = scope.moveData_;
        context.user = scope.user_;
        context.overrides = scope.overrides_;
        context.velocity = Field<MoveVector>(scope.moveData_, 0x80);
        context.outputWishVelocity = Field<MoveVector>(scope.moveData_, 0x98);
        context.viewForward = Field<MoveVector>(scope.moveData_, 0x54);
        context.inputDirection = Field<MoveVector>(scope.moveData_, 0x6C);
        context.waterLevel = Field<int>(scope.movement_, 0x18);
        context.duckRequested = (Field<unsigned>(scope.moveData_, 0x1C) & 4) != 0 || context.crouchToggled;
        return true;
    }

    static bool Call(MovementCheckpointScope& scope, CheckpointContext& context) noexcept
    {
        scope.inCallback_ = true;
        const bool accepted = scope.callback_(context);
        scope.inCallback_ = false;
        if (!ValidOverrides(*scope.overrides_)
            || (accepted && (!Finite(context.velocity) || !Finite(context.outputWishVelocity)
                || (context.stage == CheckpointStage::JumpHeight
                    && (!std::isfinite(context.selectedJumpHeight) || context.selectedJumpHeight < 0.0f)))))
        {
            scope.failed_ = true;
            return false;
        }
        if (accepted)
        {
            Field<MoveVector>(scope.moveData_, 0x80) = context.velocity;
            Field<MoveVector>(scope.moveData_, 0x98) = context.outputWishVelocity;
        }
        return accepted;
    }

    template<unsigned Side> static std::uintptr_t __fastcall Friction(std::uintptr_t movement, const MoveVector* wish, float speed)
    {
        const auto original = reinterpret_cast<std::uintptr_t(__fastcall*)(std::uintptr_t, const MoveVector*, float)>(
            adapters[Side].functions[FrictionFunction].original);
        auto* scope = Active(Side, movement);
        if (!scope || Field<float>(scope->player_, kLayout[Side].waterJump) != 0.0f)
            return original(movement, wish, speed);
        CheckpointContext context;
        if (Context(*scope, CheckpointStage::Friction, context))
        {
            context.wishDirection = *wish;
            context.wishSpeed = speed;
            context.wishKnown = true;
            // Read the real final scalar only when a friction callback requested it.
            context.nativeDeceleration = reinterpret_cast<float(__fastcall*)(std::uintptr_t)>(
                adapters[Side].module + kLayout[Side].decelerationGetter)(movement);
            context.nativeDecelerationKnown = true;
            if (Call(*scope, context)) return scope->moveData_;
        }
        if (scope->failed_ || scope->overrides_->frictionScale == 1.0f) return original(movement, wish, speed);
        const float scale = scope->overrides_->frictionScale;
        const auto before = Field<MoveVector>(scope->moveData_, 0x80);
        const auto beforeOutput = Field<MoveVector>(scope->moveData_, 0x98);
        const auto result = original(movement, wish, speed);
        auto& after = Field<MoveVector>(scope->moveData_, 0x80);
        auto& afterOutput = Field<MoveVector>(scope->moveData_, 0x98);
        for (unsigned axis = 0; axis != 3; ++axis)
        {
            after.Component(axis) = before.Component(axis) + (after.Component(axis) - before.Component(axis)) * scale;
            afterOutput.Component(axis) = beforeOutput.Component(axis) + (afterOutput.Component(axis) - beforeOutput.Component(axis)) * scale;
        }
        return result;
    }

    template<unsigned Side> static std::uintptr_t __fastcall Duck(std::uintptr_t movement)
    {
        const auto original = reinterpret_cast<std::uintptr_t(__fastcall*)(std::uintptr_t)>(adapters[Side].functions[DuckFunction].original);
        auto* scope = Active(Side, movement);
        if (!scope) return original(movement);
        const bool previous = scope->ducking_;
        scope->ducking_ = true;
        const auto result = original(movement);
        scope->ducking_ = previous;
        return result;
    }

    template<unsigned Side> static bool __fastcall Jump(std::uintptr_t movement)
    {
        const auto original = reinterpret_cast<bool(__fastcall*)(std::uintptr_t)>(adapters[Side].functions[JumpFunction].original);
        auto* scope = Active(Side, movement);
        if (!scope) return original(movement);
        const bool previousJumping = scope->jumping_, previousHeight = scope->jumpHeightReached_;
        scope->jumping_ = true;
        scope->jumpHeightReached_ = false;
        auto& toggle = Field<unsigned char>(scope->player_, kLayout[Side].crouchToggle);
        const auto savedToggle = toggle;
        const bool hideToggle = scope->overrides_->allowToggleJump && savedToggle != 0;
        if (hideToggle) toggle = 0;
        const bool result = original(movement);
        if (hideToggle) toggle = savedToggle;
        if (!scope->failed_)
        {
            CheckpointContext context;
            if (Context(*scope, CheckpointStage::JumpComplete, context))
            {
                context.jumpSucceeded = result;
                context.jumpHeightReached = scope->jumpHeightReached_;
                Call(*scope, context);
            }
        }
        scope->jumping_ = previousJumping;
        scope->jumpHeightReached_ = previousHeight;
        return result;
    }

    template<unsigned Side> static std::uintptr_t __fastcall Ground(std::uintptr_t movement, std::uintptr_t trace)
    {
        const auto original = reinterpret_cast<std::uintptr_t(__fastcall*)(std::uintptr_t, std::uintptr_t)>(
            adapters[Side].functions[GroundFunction].original);
        auto* scope = Active(Side, movement);
        if (!scope) return original(movement, trace);
        const auto previous = scope->trace_;
        scope->trace_ = trace;
        const auto result = original(movement, trace);
        scope->trace_ = previous;
        return result;
    }

    template<unsigned Side> static std::uintptr_t __fastcall Walk(std::uintptr_t movement)
    {
        const auto original = reinterpret_cast<std::uintptr_t(__fastcall*)(std::uintptr_t)>(adapters[Side].functions[WalkFunction].original);
        if (auto* scope = Active(Side, movement))
        {
            CheckpointContext context;
            if (Context(*scope, CheckpointStage::Walk, context)) Call(*scope, context);
        }
        return original(movement);
    }

    template<unsigned Side> static float __fastcall PlayerScale(std::uintptr_t player)
    {
        const float native = reinterpret_cast<float(__fastcall*)(std::uintptr_t)>(
            adapters[Side].functions[PlayerScaleFunction].original)(player);
        auto* scope = currentScope && currentScope->player_ == player ? Active(Side, currentScope->movement_) : nullptr;
        return scope && scope->overrides_->moveScale != 1.0f ? native * scope->overrides_->moveScale : native;
    }

    template<unsigned Side> static bool __fastcall CanSprint(std::uintptr_t player)
    {
        const bool native = reinterpret_cast<bool(__fastcall*)(std::uintptr_t)>(
            adapters[Side].functions[CanSprintFunction].original)(player);
        auto* scope = currentScope && currentScope->player_ == player ? Active(Side, currentScope->movement_) : nullptr;
        return native && (!scope || scope->overrides_->sprintAllowed);
    }

    static __m128 __fastcall ClientFraction(int elapsed)
    {
        const auto original = reinterpret_cast<__m128(__fastcall*)(int)>(adapters[kClient].functions[FractionFunction].original);
        const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        auto* scope = currentScope ? Active(kClient, currentScope->movement_) : nullptr;
        if (scope && scope->ducking_ && scope->overrides_->duckMilliseconds
            && (caller == adapters[kClient].module + 0x343111 || caller == adapters[kClient].module + 0x343304))
            return _mm_set_ss(std::clamp(static_cast<float>(elapsed) / scope->overrides_->duckMilliseconds, 0.0f, 1.0f));
        return original(elapsed);
    }

    static void __fastcall Point(CpuFrame* saved, unsigned id) noexcept
    {
        const unsigned side = id >> 8;
        const auto point = static_cast<PointIndex>(id & 255);
        auto& cpu = *saved;
        std::uintptr_t movement = cpu.rbx;
        if (side == kClient && (point == DecelerationPoint || point == JumpHeightPoint)) movement = cpu.rdi;
        if (side == kAuthority && point == StartGravityPoint) movement = cpu.rdi;
        if (side == kAuthority && point == FinishGravityPoint) movement = cpu.rdx;
        auto* scope = Active(side, movement);
        if (!scope) return;
        auto& controls = *scope->overrides_;
        const auto& layout = kLayout[side];
        const int duration = controls.duckMilliseconds;
        switch (point)
        {
        case AccelerationPoint:
            if (controls.accelerationSet) cpu.xmm[2][0] = controls.acceleration;
            return;
        case DecelerationPoint:
            if (controls.decelerationSet) cpu.xmm[2][0] = controls.deceleration;
            return;
        case DuckDurationPoint:
            if (scope->ducking_ && duration) (side == kClient ? cpu.rdi : cpu.rsi) = duration;
            return;
        case DuckReverseUpPoint:
            if (scope->ducking_ && duration)
            {
                if (side == kClient) cpu.rcx = duration;
                else cpu.xmm[0][0] = static_cast<float>(duration - Field<int>(scope->player_, layout.duckTimer)) / duration;
            }
            return;
        case DuckReverseDownPoint:
            if (scope->ducking_ && duration) cpu.xmm[0][0] *= static_cast<float>(duration) / 400.0f;
            return;
        case DuckFractionPoint:
            if (scope->ducking_ && duration)
                cpu.xmm[0][0] = static_cast<float>(duration - Field<int>(scope->player_, layout.duckTimer)) / duration;
            return;
        default:
            break;
        }
        CheckpointContext context;
        if (point == DuckRequestPoint)
        {
            if (!scope->ducking_) return;
            auto& request = side == kClient ? cpu.rdi : cpu.rsi;
            bool accepted = false;
            if (Context(*scope, CheckpointStage::DuckRequest, context))
            {
                context.duckRequested = (request & 255) != 0;
                accepted = Call(*scope, context);
            }
            if (!scope->failed_ && (accepted || controls.retainDuck))
                request = (request & ~std::uint64_t{255}) | ((accepted && context.duckRequested) || controls.retainDuck ? 1 : 0);
        }
        else if (point == JumpHeightPoint)
        {
            if (!scope->jumping_) return;
            scope->jumpHeightReached_ = true;
            if (!Context(*scope, CheckpointStage::JumpHeight, context)) return;
            context.jumpHeightReached = true;
            context.selectedJumpHeight = cpu.xmm[7][0];
            if (Call(*scope, context)) cpu.xmm[7][0] = context.selectedJumpHeight;
        }
        else if (point == GroundPublishPoint)
        {
            // Keep a genuine observed plane available to other opted-in stages,
            // even when the program did not request a ground callback itself.
            scope->groundNormalKnown_ = cpu.rdi != 0 && scope->trace_ != 0;
            scope->groundNormal_ = scope->groundNormalKnown_ ? Field<MoveVector>(scope->trace_, 0x20) : MoveVector{};
            scope->groundSurface_ = -1;
            if (scope->groundNormalKnown_)
            {
                const unsigned selected = (Field<std::uint16_t>(scope->trace_, 0x38) >> 4) & 1u;
                scope->groundSurface_ = Field<std::int16_t>(scope->trace_, 0x50 + 2 * selected);
            }
            if (!Context(*scope, CheckpointStage::GroundPublish, context)) return;
            context.groundCandidate = cpu.rdi != 0;
            Call(*scope, context);
        }
        else if (point == StartGravityPoint || point == FinishGravityPoint)
        {
            if (!Context(*scope, point == StartGravityPoint ? CheckpointStage::StartGravity : CheckpointStage::FinishGravity, context)) return;
            context.gravityContribution = cpu.xmm[side == kClient ? 6 : 5][0];
            Call(*scope, context);
        }
    }
};

bool InstallMovementCheckpoints(MovementDomain domain, std::uintptr_t module) noexcept
{
    const auto side = static_cast<unsigned>(domain);
    if (side > kAuthority) return false;
    std::lock_guard<std::mutex> lock(installMutex);
    auto& adapter = adapters[side];
    if (adapter.ready) return adapter.module == module;
    if (adapter.module) return Reject(side, 0, "previous hook set still needs removal");
    if (!MatchImage(side, module)) return false;
    adapter.module = module;
    void* detours[FunctionCount] = {
        side == kClient ? reinterpret_cast<void*>(&CheckpointDispatch::Friction<kClient>) : reinterpret_cast<void*>(&CheckpointDispatch::Friction<kAuthority>),
        side == kClient ? reinterpret_cast<void*>(&CheckpointDispatch::Duck<kClient>) : reinterpret_cast<void*>(&CheckpointDispatch::Duck<kAuthority>),
        side == kClient ? reinterpret_cast<void*>(&CheckpointDispatch::Jump<kClient>) : reinterpret_cast<void*>(&CheckpointDispatch::Jump<kAuthority>),
        side == kClient ? reinterpret_cast<void*>(&CheckpointDispatch::Ground<kClient>) : reinterpret_cast<void*>(&CheckpointDispatch::Ground<kAuthority>),
        side == kClient ? reinterpret_cast<void*>(&CheckpointDispatch::Walk<kClient>) : reinterpret_cast<void*>(&CheckpointDispatch::Walk<kAuthority>),
        reinterpret_cast<void*>(&CheckpointDispatch::ClientFraction),
        side == kClient ? reinterpret_cast<void*>(&CheckpointDispatch::PlayerScale<kClient>) : reinterpret_cast<void*>(&CheckpointDispatch::PlayerScale<kAuthority>),
        side == kClient ? reinterpret_cast<void*>(&CheckpointDispatch::CanSprint<kClient>) : reinterpret_cast<void*>(&CheckpointDispatch::CanSprint<kAuthority>)
    };
    bool ok = true;
    for (unsigned index = 0; index != FunctionCount && ok; ++index)
    {
        const auto& site = kFunctions[side][index];
        if (!site.rva) continue;
        auto& hook = adapter.functions[index];
        void* target = reinterpret_cast<void*>(module + site.rva);
        const auto status = MH_CreateHook(target, detours[index], &hook.original);
        if (status == MH_OK) hook.target = target;
        else ok = Reject(side, site.rva, "function hook creation failed", status);
    }
    for (unsigned index = 0; index != PointCount && ok; ++index)
    {
        const auto& site = kPoints[side][index];
        if (site.rva && !PrepareGateway(adapter.points[index], module + site.rva, &CheckpointDispatch::Point, (side << 8) | index))
            ok = Reject(side, site.rva, "instruction gateway creation failed");
    }
    if (ok) ForEachHook(adapter, [&](Hook& hook)
    {
        if (!ok || !hook.target) return;
        const auto status = MH_EnableHook(hook.target);
        if (status != MH_OK) ok = Reject(side, reinterpret_cast<std::uintptr_t>(hook.target) - module, "hook enable failed", status);
    });
    if (!ok)
    {
        RemoveHooks(side, adapter);
        return false;
    }
    adapter.ready = true;
    return true;
}

bool UninstallMovementCheckpoints(MovementDomain domain) noexcept
{
    const auto side = static_cast<unsigned>(domain);
    if (side > kAuthority) return false;
    std::lock_guard<std::mutex> lock(installMutex);
    auto& adapter = adapters[side];
    if (adapter.scopes.load(std::memory_order_acquire)) return Reject(side, 0, "active movement scope prevents uninstall");
    return RemoveHooks(side, adapter);
}

bool ReadCheckpointPlayerContext(MovementDomain domain, std::uintptr_t player, CheckpointPlayerContext& context) noexcept
{
    return CheckpointDispatch::ReadPlayer(domain, player, context);
}

const char* ReadMovementSurfaceName(MovementDomain domain, int index) noexcept
{
    const auto side = static_cast<unsigned>(domain);
    if (side > kAuthority || !adapters[side].ready) return nullptr;
    const auto properties = Field<std::uintptr_t>(adapters[side].module,
        side == kClient ? 0xF8D7A8 : 0xC31A38);
    if (!properties) return nullptr;
    const auto table = Field<std::uintptr_t>(properties, 0);
    using Count = int(__fastcall*)(std::uintptr_t);
    using Name = const char*(__fastcall*)(std::uintptr_t, int);
    const int count = reinterpret_cast<Count>(Field<std::uintptr_t>(table, 2 * 8))(properties);
    if (index < 0) index = 0;
    if (index >= count) return nullptr;
    return reinterpret_cast<Name>(Field<std::uintptr_t>(table, 7 * 8))(properties, index);
}

MovementCheckpointScope::MovementCheckpointScope(MovementDomain domain, std::uintptr_t player,
    std::uintptr_t movement, std::uintptr_t moveData, CheckpointOverrides& overrides,
    CheckpointCallback callback, void* user) noexcept
    : previous_(currentScope), domain_(domain), player_(player), movement_(movement), moveData_(moveData),
      overrides_(&overrides), callback_(callback), user_(user)
{
    const auto side = static_cast<unsigned>(domain);
    if (side > kAuthority || !adapters[side].ready || !player || !movement || !moveData || !callback
        || Field<std::uintptr_t>(movement, 8) != player || Field<std::uintptr_t>(movement, 16) != moveData
        || !ValidOverrides(overrides))
    {
        failed_ = true;
        return;
    }
    adapters[side].scopes.fetch_add(1, std::memory_order_relaxed);
    active_ = true;
    currentScope = this;
}

MovementCheckpointScope::~MovementCheckpointScope()
{
    if (!active_) return;
    currentScope = previous_;
    adapters[static_cast<unsigned>(domain_)].scopes.fetch_sub(1, std::memory_order_release);
}
}
