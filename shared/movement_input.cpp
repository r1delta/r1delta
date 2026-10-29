#include "movement_input.h"

#include <Windows.h>
#include <MinHook.h>
#include <intrin.h>
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>

namespace r1delta::movement
{
// Shared exact-file guard owned by predicted_movement.cpp.
bool VerifyMovementModuleDigest(std::uintptr_t module, const char* expected);

namespace
{
constexpr char kClientDigest[] = "5b3f00805568eeaa6ee1cf889f12dc4570eb28a3fad2626ec0578eae3e7afd5e";
constexpr char kServerDigest[] = "50faad928e0715a51ad624d0a3eb8f00f7ffad9e66258dc95c960f9d11ff41e4";
constexpr std::uintptr_t kBuildTail = 0x1A5350;
constexpr std::uintptr_t kBuildReturn = 0x1A87C8;
constexpr std::uintptr_t kWriteCommand = 0x2791B0;
constexpr std::uintptr_t kFloatDelta = 0x278E20;
constexpr std::uintptr_t kIntegerDelta = 0x278CE0;
constexpr std::uintptr_t kResolvePlayer = 0x7B1B0;
constexpr std::array<std::uintptr_t, 6> kCameraReturns = {
    0x279524, 0x27953D, 0x279556, 0x27956F, 0x279588, 0x2795A1
};

using BuildTailFn = std::int64_t (__fastcall*)(std::uintptr_t, int, std::uintptr_t, std::uintptr_t);
using WriteCommandFn = std::int64_t (__fastcall*)(std::uintptr_t, std::uintptr_t, std::uintptr_t);
using FloatDeltaFn = char (__fastcall*)(std::uintptr_t, float, float);
using IntegerDeltaFn = char (__fastcall*)(std::uintptr_t, std::uint32_t, std::uint32_t, int);
using ResolvePlayerFn = std::uintptr_t (__fastcall*)(int);

std::mutex s_installLock;
std::atomic<bool> s_clientReady{false};
std::atomic<bool> s_serverReady{false};
std::uintptr_t s_clientModule{};
std::uintptr_t s_serverModule{};
MovementInputCallbacks s_callbacks{};
BuildTailFn s_buildTail{};
WriteCommandFn s_writeCommand{};
FloatDeltaFn s_floatDelta{};
IntegerDeltaFn s_integerDelta{};
ResolvePlayerFn s_resolvePlayer{};
thread_local bool s_writingCustomInput{};

void InputFailure(const char* reason)
{
    OutputDebugStringA("[r1delta_movement_input] ");
    OutputDebugStringA(reason);
    OutputDebugStringA("\n");
}

template <std::size_t N>
bool Matches(std::uintptr_t module, std::uintptr_t rva, const unsigned char (&expected)[N])
{
    return std::memcmp(reinterpret_cast<const void*>(module + rva), expected, N) == 0;
}

bool CheckClient(std::uintptr_t module)
{
    if (!module || !VerifyMovementModuleDigest(module, kClientDigest)) return false;
    constexpr unsigned char build[] = {0x4C,0x8B,0xDC,0x55,0x53,0x57,0x49,0x8D,0x6B,0xA1,0x48,0x81,0xEC,0xA0,0x00,0x00};
    constexpr unsigned char writer[] = {0x4C,0x89,0x44,0x24,0x18,0x48,0x89,0x54,0x24,0x10,0x53,0x55,0x56,0x57,0x41,0x54};
    constexpr unsigned char floating[] = {0x48,0x83,0xEC,0x28,0x0F,0x2E,0xCA,0x4C,0x8B,0xC1,0x48,0x63,0x49,0x10,0x8D,0x41};
    constexpr unsigned char integer[] = {0x40,0x53,0x48,0x83,0xEC,0x30,0x4C,0x8B,0xD1,0x48,0x63,0x49,0x10,0x41,0x8B,0xD9};
    constexpr unsigned char player[] = {0x48,0x83,0xEC,0x28,0x83,0xF9,0xFF,0x75,0x6C,0x48,0x8B,0x0D,0xB8,0x0C,0x64,0x01};
    constexpr unsigned char buildCall[] = {0xE8,0x88,0xCB,0xFF,0xFF};
    if (!Matches(module, kBuildTail, build) || !Matches(module, kWriteCommand, writer)
        || !Matches(module, kFloatDelta, floating) || !Matches(module, kIntegerDelta, integer)
        || !Matches(module, kResolvePlayer, player) || !Matches(module, kBuildReturn - 5, buildCall)) return false;
    for (const auto returnRva : kCameraReturns)
    {
        const auto* call = reinterpret_cast<const unsigned char*>(module + returnRva - 5);
        std::int32_t displacement{};
        std::memcpy(&displacement, call + 1, sizeof(displacement));
        if (*call != 0xE8 || module + returnRva + displacement != module + kFloatDelta) return false;
    }
    return true;
}

std::int64_t __fastcall BuildTailHook(std::uintptr_t input, int slot,
    std::uintptr_t command, std::uintptr_t previous)
{
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto result = s_buildTail(input, slot, command, previous);
    if (!s_clientReady.load(std::memory_order_acquire) || caller != s_clientModule + kBuildReturn
        || !s_callbacks.sessionCapable()) return result;

    // Native split-screen mirroring can copy an earlier command at this seam.
    // Presence belongs to THIS build, not to the copied baseline.
    const MovementCommandInput empty{};
    std::memcpy(reinterpret_cast<void*>(command + kMovementInputOffset), &empty, sizeof(empty));
    *reinterpret_cast<unsigned char*>(command + kMovementInputPresenceOffset) = 0;
    const auto player = s_resolvePlayer(slot);
    if (!player || !s_callbacks.playerEnabled(player)) return result;

    std::uint32_t buttons{};
    if (!s_callbacks.build(player, command, buttons))
    {
        s_callbacks.failed(player, command);
        return result;
    }
    const MovementCommandInput payload{buttons, {}};
    std::memcpy(reinterpret_cast<void*>(command + kMovementInputOffset), &payload, sizeof(payload));
    *reinterpret_cast<unsigned char*>(command + kMovementInputPresenceOffset) = 1;
    return result;
}

std::int64_t __fastcall WriteCommandHook(std::uintptr_t writer, std::uintptr_t to, std::uintptr_t from)
{
    const bool previous = s_writingCustomInput;
    s_writingCustomInput = s_clientReady.load(std::memory_order_acquire)
        && s_callbacks.sessionCapable()
        && *reinterpret_cast<const unsigned char*>(to + kMovementInputPresenceOffset) != 0;
    const auto result = s_writeCommand(writer, to, from);
    s_writingCustomInput = previous;
    return result;
}

char __fastcall FloatDeltaHook(std::uintptr_t writer, float from, float to)
{
    if (s_writingCustomInput)
    {
        const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        for (const auto returnRva : kCameraReturns)
        {
            if (caller != s_clientModule + returnRva) continue;
            std::uint32_t fromBits{}, toBits{};
            std::memcpy(&fromBits, &from, sizeof(fromBits));
            std::memcpy(&toBits, &to, sizeof(toBits));
            return s_integerDelta(writer, fromBits, toBits, 32);
        }
    }
    return s_floatDelta(writer, from, to);
}

struct Hook
{
    void* target;
    void* detour;
    void** original;
};

bool InstallTransaction(const std::array<Hook, 3>& hooks)
{
    unsigned created = 0;
    bool ok = true;
    for (const auto& hook : hooks)
    {
        if (MH_CreateHook(hook.target, hook.detour, hook.original) != MH_OK)
        {
            ok = false;
            break;
        }
        ++created;
        if (!*hook.original) { ok = false; break; }
    }
    if (ok)
    {
        for (const auto& hook : hooks)
        {
            if (MH_QueueEnableHook(hook.target) != MH_OK) { ok = false; break; }
        }
        if (ok) ok = MH_ApplyQueued() == MH_OK;
    }
    if (ok) return true;
    // ApplyQueued may have enabled a prefix. Remove every target we created,
    // never a pre-existing hook owned by another component.
    for (unsigned i = 0; i < created; ++i)
    {
        MH_QueueDisableHook(hooks[i].target);
        MH_DisableHook(hooks[i].target);
        MH_RemoveHook(hooks[i].target);
        *hooks[i].original = nullptr;
    }
    return false;
}
}

bool InstallMovementInputClient(std::uintptr_t clientModule, const MovementInputCallbacks& callbacks)
{
    std::lock_guard<std::mutex> lock(s_installLock);
    if (s_clientReady.load(std::memory_order_acquire)) return s_clientModule == clientModule
        && s_callbacks.sessionCapable == callbacks.sessionCapable
        && s_callbacks.playerEnabled == callbacks.playerEnabled
        && s_callbacks.build == callbacks.build && s_callbacks.failed == callbacks.failed;
    if (!callbacks.sessionCapable || !callbacks.playerEnabled || !callbacks.build || !callbacks.failed
        || !CheckClient(clientModule))
    {
        InputFailure("client image/sites or callbacks rejected; custom input unavailable");
        return false;
    }
    s_clientModule = clientModule;
    s_callbacks = callbacks;
    s_integerDelta = reinterpret_cast<IntegerDeltaFn>(clientModule + kIntegerDelta);
    s_resolvePlayer = reinterpret_cast<ResolvePlayerFn>(clientModule + kResolvePlayer);
    const std::array<Hook, 3> hooks = {{
        {reinterpret_cast<void*>(clientModule + kBuildTail), reinterpret_cast<void*>(&BuildTailHook), reinterpret_cast<void**>(&s_buildTail)},
        {reinterpret_cast<void*>(clientModule + kWriteCommand), reinterpret_cast<void*>(&WriteCommandHook), reinterpret_cast<void**>(&s_writeCommand)},
        {reinterpret_cast<void*>(clientModule + kFloatDelta), reinterpret_cast<void*>(&FloatDeltaHook), reinterpret_cast<void**>(&s_floatDelta)}
    }};
    if (!InstallTransaction(hooks))
    {
        InputFailure("client hook transaction failed; custom input unavailable");
        return false;
    }
    s_clientReady.store(true, std::memory_order_release);
    return true;
}

bool InstallMovementInputServer(std::uintptr_t serverModule)
{
    std::lock_guard<std::mutex> lock(s_installLock);
    if (s_serverReady.load(std::memory_order_acquire)) return s_serverModule == serverModule;
    constexpr unsigned char reader[] = {0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x54,0x24,0x10,0x55,0x56,0x57,0x41,0x54,0x41};
    constexpr unsigned char presence[] = {0x41,0x88,0x95,0x94,0x00,0x00,0x00,0x84,0xD2,0x0F,0x84,0xD7,0x07,0x00,0x00};
    constexpr unsigned char rawPosition[] = {0x44,0x89,0x5C,0x24,0x60,0xF3,0x0F,0x10,0x44,0x24,0x60,0xF3,0x41,0x0F,0x11,0x45,0x7C};
    constexpr unsigned char rawAngles[] = {0x44,0x89,0x5C,0x24,0x60,0xF3,0x0F,0x10,0x44,0x24,0x60,0xF3,0x41,0x0F,0x11,0x85,0x88,0x00,0x00,0x00};
    if (!serverModule || !VerifyMovementModuleDigest(serverModule, kServerDigest)
        || !Matches(serverModule, 0x2576F0, reader) || !Matches(serverModule, 0x259355, presence)
        || !Matches(serverModule, 0x25949F, rawPosition) || !Matches(serverModule, 0x259889, rawAngles))
    {
        InputFailure("server image/decoder rejected; custom input unavailable");
        return false;
    }
    s_serverModule = serverModule;
    s_serverReady.store(true, std::memory_order_release);
    return true;
}

bool MovementInputClientReady() { return s_clientReady.load(std::memory_order_acquire); }
bool MovementInputServerReady() { return s_serverReady.load(std::memory_order_acquire); }

bool ReadMovementCommandInput(std::uintptr_t command, bool enabled, std::uint32_t& buttons)
{
    buttons = 0;
    if (!enabled || !command
        || *reinterpret_cast<const unsigned char*>(command + kMovementInputPresenceOffset) == 0) return false;
    std::memcpy(&buttons, reinterpret_cast<const void*>(command + kMovementInputOffset), sizeof(buttons));
    return true;
}
}
