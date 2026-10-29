#include "movement_snapshot_transport.h"

#include "core.h"
#include "load.h"
#include "logging.h"
#include "bitbuf.h"
#include "../../shared/movement_runtime.h"
#include "../../shared/predicted_movement.h"

#include <atomic>
#include <cstddef>
#include <cstring>

namespace r1delta::movement
{
namespace
{
using CaptureFrame = std::int64_t(__fastcall*)(std::uintptr_t, std::uintptr_t);
using WriteSnapshot = std::int64_t(__fastcall*)(std::uintptr_t, std::uintptr_t,
    std::uintptr_t, std::uintptr_t, bf_write*, int, int);

struct SnapshotProfile
{
    const char* name;
    const char* digest;
    std::uintptr_t captureRva;
    std::uintptr_t writerRva;
    std::size_t slotOffset;
    std::size_t frameOffset;
    const unsigned char* capturePrefix;
    std::size_t capturePrefixSize;
    const unsigned char* writerPrefix;
    std::size_t writerPrefixSize;
};

constexpr unsigned char kCaptureListen[] = {
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20
};
constexpr unsigned char kCaptureDedicated[] = {
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x30
};
constexpr unsigned char kCaptureAlternate[] = {
    0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20
};
constexpr unsigned char kWriterLegacy[] = {
    0x4C,0x89,0x44,0x24,0x18,0x48,0x89,0x54,0x24,0x10,0x55,0x53,0x56,0x57,
    0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57
};
constexpr unsigned char kWriterAlternate[] = {
    0x48,0x8B,0xC4,0x4C,0x89,0x40,0x18,0x48,0x89,0x50,0x10,0x55,0x53,0x56,0x57,
    0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57
};
constexpr SnapshotProfile kListen = {
    "listen", "0214a40436eca77f160d12763ab8f392a8ce2268bfe0a973188e426ae4c0263c",
    0xDA900, 0xDF020, 132, 284840,
    kCaptureListen, sizeof(kCaptureListen), kWriterLegacy, sizeof(kWriterLegacy)
};
constexpr SnapshotProfile kDedicated = {
    "legacy dedicated", "bc27e3ef243e87bfcf9a0b4d4059ff9dcaeec4a7bc1dcd653255a0b9ad34699a",
    0x4CD00, 0x50870, 68, 216032,
    kCaptureDedicated, sizeof(kCaptureDedicated), kWriterLegacy, sizeof(kWriterLegacy)
};
constexpr SnapshotProfile kAlternate = {
    "alternate dedicated", "7d85f8201f2a951a35ddb57dc8162169a1b3fa730b7a94b0caec002dcc569999",
    0x142EB0, 0x146B00, 132, 178496,
    kCaptureAlternate, sizeof(kCaptureAlternate), kWriterAlternate, sizeof(kWriterAlternate)
};

CaptureFrame captureFrameOriginal{};
WriteSnapshot writeSnapshotOriginal{};
const SnapshotProfile* activeProfile{};
std::uintptr_t activeModule{};
std::atomic<bool> transportReady{};

bool CorrectSnapshotRole(std::uintptr_t module, const SnapshotProfile& profile)
{
    if (IsR1ODedicatedServer()) return &profile == &kAlternate && module == G_engine_r1o;
    if (IsDedicatedServer()) return &profile == &kDedicated && module == G_engine_ds;
    return &profile == &kListen && module == G_engine;
}

int SnapshotRecipient(std::uintptr_t client)
{
    return *reinterpret_cast<const int*>(client + activeProfile->slotOffset);
}

std::int64_t __fastcall CaptureSnapshotMovement(std::uintptr_t client, std::uintptr_t snapshot)
{
    const auto result = captureFrameOriginal(client, snapshot);
    if (!transportReady.load(std::memory_order_acquire)
        || !CorrectSnapshotRole(activeModule, *activeProfile)) return result;
    const auto frame = *reinterpret_cast<const std::uintptr_t*>(client + activeProfile->frameOffset);
    if (frame && *reinterpret_cast<const std::uintptr_t*>(frame + 536) == snapshot)
        CaptureMovementAuthorityForSnapshot(SnapshotRecipient(client),
            *reinterpret_cast<const std::uint32_t*>(frame + 8));
    return result;
}

std::int64_t __fastcall WriteSnapshotMovement(std::uintptr_t server, std::uintptr_t client,
    std::uintptr_t frame, std::uintptr_t baseline, bf_write* buffer, int currentTick, int deltaTick)
{
    const auto result = writeSnapshotOriginal(server, client, frame, baseline, buffer, currentTick, deltaTick);
    if (!transportReady.load(std::memory_order_acquire)
        || !CorrectSnapshotRole(activeModule, *activeProfile) || buffer->IsOverflowed()) return result;

    char command[kMovementAuthorityBytes];
    const auto length = CopyMovementAuthorityCommand(SnapshotRecipient(client),
        *reinterpret_cast<const std::uint32_t*>(frame + 8), command, sizeof(command));
    if (!length) return result;

    // This is the actual entity-snapshot buffer, before the caller's common
    // overflow decision and either unreliable send or reliable full-block copy.
    // A missing frozen frame must never emit native state without its slots.
    if (length == SIZE_MAX || length >= sizeof(command)
        || command[length] != '\0' || buffer->GetNumBitsLeft() < 6 + static_cast<int>((length + 1) * 8))
    {
        buffer->SetOverflowFlag();
        return result;
    }
    buffer->WriteUBitLong(4, 6);
    buffer->WriteString(command);
    return result;
}
}

bool IsMovementSnapshotTransportReady()
{
    return transportReady.load(std::memory_order_acquire);
}

bool AttachMovementSnapshotTransport(std::uintptr_t module)
{
    if (IsMovementSnapshotTransportReady())
        return activeModule == module && CorrectSnapshotRole(module, *activeProfile);
    const auto& profile = IsR1ODedicatedServer() ? kAlternate : IsDedicatedServer() ? kDedicated : kListen;
    if (!module || !CorrectSnapshotRole(module, profile) || !VerifyMovementModuleDigest(module, profile.digest))
    {
        Warning("R1Delta: movement snapshot transport refused %s engine image/role; script movement is unavailable.\n", profile.name);
        return false;
    }
    auto* captureTarget = reinterpret_cast<void*>(module + profile.captureRva);
    auto* writerTarget = reinterpret_cast<void*>(module + profile.writerRva);
    if (std::memcmp(captureTarget, profile.capturePrefix, profile.capturePrefixSize)
        || std::memcmp(writerTarget, profile.writerPrefix, profile.writerPrefixSize))
    {
        Warning("R1Delta: movement snapshot transport refused %s engine prologue; script movement is unavailable.\n", profile.name);
        return false;
    }

    activeProfile = &profile;
    activeModule = module;
    const auto captureStatus = MH_CreateHook(captureTarget, reinterpret_cast<void*>(&CaptureSnapshotMovement),
        reinterpret_cast<void**>(&captureFrameOriginal));
    if (captureStatus != MH_OK)
    {
        Warning("R1Delta: movement snapshot capture hook failed (%s, %d).\n", profile.name, static_cast<int>(captureStatus));
        return false;
    }
    const auto writerStatus = MH_CreateHook(writerTarget, reinterpret_cast<void*>(&WriteSnapshotMovement),
        reinterpret_cast<void**>(&writeSnapshotOriginal));
    if (writerStatus != MH_OK)
    {
        MH_RemoveHook(captureTarget);
        Warning("R1Delta: movement snapshot writer hook failed (%s, %d).\n", profile.name, static_cast<int>(writerStatus));
        return false;
    }
    const auto captureEnable = MH_EnableHook(captureTarget);
    const auto writerEnable = captureEnable == MH_OK ? MH_EnableHook(writerTarget) : captureEnable;
    if (captureEnable != MH_OK || writerEnable != MH_OK)
    {
        if (captureEnable == MH_OK) MH_DisableHook(captureTarget);
        MH_RemoveHook(writerTarget);
        MH_RemoveHook(captureTarget);
        Warning("R1Delta: movement snapshot hook enable failed (%s, %d/%d).\n", profile.name,
            static_cast<int>(captureEnable), static_cast<int>(writerEnable));
        return false;
    }
    transportReady.store(true, std::memory_order_release);
    return true;
}
}
