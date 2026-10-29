#include "predicted_movement.h"
#include "movement_runtime.h"
#include "movement_input.h"
#include "movement_checkpoints.h"
#include "../client/movement_presentation_hooks.h"
#include "../client/movement_rope_presentation.h"
#include "../engine/net/movement_snapshot_transport.h"
#include "core.h"
#include "load.h"
#include "logging.h"
#include "cvar.h"
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <shared_mutex>
#include <mutex>
#include <string_view>

#pragma comment(lib, "bcrypt.lib")

namespace r1delta::movement
{
namespace
{
constexpr unsigned kPlayerSlots = 128;
constexpr unsigned kEntityEntryBits = 16;
constexpr unsigned kNetworkSerialBits = 10;
constexpr std::uint32_t kNetworkSerialMask = (1u << kNetworkSerialBits) - 1;
constexpr std::uint32_t kNetworkHandleMask = (kNetworkSerialMask << kEntityEntryBits) | 0xFFFFu;
constexpr unsigned kNetworkEntityLimit = 4096;
constexpr unsigned kPresentationQueue = kMovementEvents * kCommandWindow + 2;
constexpr ULONGLONG kAuthorityWaitMilliseconds = 5000;
// These client/server offsets match the digest-admitted modules. Permission is read-only;
// every state change below goes through the engine's native player transition vfunc.
constexpr std::uint32_t kWeaponPermissionLockBit = 1u;
constexpr std::uint32_t kUnavailableWeaponLock = 0xFFFFFFFFu;
constexpr std::size_t kClientWeaponPermissionOffset = 9240;
constexpr std::size_t kServerWeaponPermissionOffset = 2988;
constexpr std::size_t kClientActiveWeaponVtableOffset = 2160;
constexpr std::size_t kServerActiveWeaponVtableOffset = 2640;
constexpr std::size_t kClientHolsterWeaponVtableOffset = 2096;
constexpr std::size_t kServerHolsterWeaponVtableOffset = 2520;
constexpr std::size_t kClientDeployWeaponVtableOffset = 2104;
constexpr std::size_t kServerDeployWeaponVtableOffset = 2528;
constexpr std::size_t kClientEnableWeaponVtableOffset = 2088;
constexpr std::size_t kServerEnableWeaponVtableOffset = 2512;
std::atomic<unsigned> pendingWeaponReleaseCount{};
using PlayerWeaponTransition = void(__fastcall*)(std::uintptr_t);
using PlayerActiveWeapon = std::uintptr_t(__fastcall*)(std::uintptr_t);
constexpr char kClientDigest[] = "5b3f00805568eeaa6ee1cf889f12dc4570eb28a3fad2626ec0578eae3e7afd5e";
constexpr char kServerDigest[] = "50faad928e0715a51ad624d0a3eb8f00f7ffad9e66258dc95c960f9d11ff41e4";
constexpr char kClientEngineDigest[] = "0214a40436eca77f160d12763ab8f392a8ce2268bfe0a973188e426ae4c0263c";
using SolveMovement = std::uintptr_t(__fastcall*)(std::uintptr_t);
using UpdateNativePrediction = std::uintptr_t(__fastcall*)(std::uintptr_t, int, unsigned char, unsigned char, std::uint32_t, int);
using RestoreNativePrediction = std::uintptr_t(__fastcall*)(std::uintptr_t, const char*, int, int);
using RunNativeCommand = std::uintptr_t(__fastcall*)(std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t);
using HullTrace = std::uintptr_t(__fastcall*)(const MoveVector*, const MoveVector*, const MoveVector*, const MoveVector*, std::uint32_t, std::uintptr_t, int, void*);
using PrepareNativeMovement = void(__fastcall*)(std::uintptr_t);
using CompleteNativePredictionPacket = std::uintptr_t(__fastcall*)(std::uintptr_t, int, unsigned char);

struct NativeMovement
{
    std::uintptr_t module{};
    SolveMovement solve{};
    UpdateNativePrediction update{};
    RestoreNativePrediction restore{};
    RunNativeCommand command{};
    HullTrace trace{};
    PrepareNativeMovement prepare{};
    PrepareNativeMovement levelShutdown{};
    CompleteNativePredictionPacket completePacket{};
    const void* gravity{};
    std::array<void*, 7> hooks{};
    unsigned hookCount{};
    bool ready{};
    bool checkpointsReady{};
};
NativeMovement nativeClient, nativeServer;
std::uintptr_t clientEngine{};
thread_local MovementEvaluation* solvingMovement{};

template<class T> T& Field(std::uintptr_t base, std::size_t offset)
{
    return *reinterpret_cast<T*>(base + offset);
}
template<class F> F VirtualFunction(std::uintptr_t object, std::size_t offset)
{
    return reinterpret_cast<F>(Field<std::uintptr_t>(Field<std::uintptr_t>(object, 0), offset));
}
NativeMovement& NativeFor(MovementDomain side)
{
    return side == MovementDomain::Client ? nativeClient : nativeServer;
}
std::uintptr_t MovementGlobals(MovementDomain side)
{
    const auto& native = NativeFor(side);
    return native.module ? Field<std::uintptr_t>(native.module, side == MovementDomain::Client ? 0xAEA640 : 0xC310C0) : 0;
}
std::uint32_t NativeEntityHandle(std::uintptr_t entity, MovementDomain side)
{
    return entity ? Field<std::uint32_t>(entity, side == MovementDomain::Client ? 0x4CC : 0x454) : 0xFFFFFFFFu;
}
std::uint32_t NetworkEntityHandle(std::uint32_t nativeHandle);
bool HasMovementWeaponLockHandle(std::uint32_t handle)
{
    const auto index = handle & 0xFFFFu;
    return handle && handle != kUnavailableWeaponLock && index && index < kNetworkEntityLimit;
}
// Native IsWeaponDisabled also checks independent player/weapon flags. The
// lock owns permission bit 0 only; never consume unrelated permission bits.
std::uint32_t NativeWeaponPermissionValue(std::uintptr_t player, MovementDomain side)
{
    return player ? Field<std::uint32_t>(player,
        side == MovementDomain::Client ? kClientWeaponPermissionOffset : kServerWeaponPermissionOffset) : 0;
}
bool NativeWeaponPermission(std::uintptr_t player, MovementDomain side)
{
    return (NativeWeaponPermissionValue(player, side) & kWeaponPermissionLockBit) != 0;
}
std::uintptr_t NativeActiveWeapon(std::uintptr_t player, MovementDomain side)
{
    const auto offset = side == MovementDomain::Client
        ? kClientActiveWeaponVtableOffset : kServerActiveWeaponVtableOffset;
    const auto getActiveWeapon = VirtualFunction<PlayerActiveWeapon>(player, offset);
    return getActiveWeapon ? getActiveWeapon(player) : 0;
}
enum class NativeWeaponTransition { Holster, Deploy, Enable };
bool CallNativeWeaponTransition(std::uintptr_t player, MovementDomain side, NativeWeaponTransition transition)
{
    const bool holster = transition == NativeWeaponTransition::Holster;
    const auto offset = side == MovementDomain::Client
        ? (holster ? kClientHolsterWeaponVtableOffset
            : transition == NativeWeaponTransition::Deploy ? kClientDeployWeaponVtableOffset : kClientEnableWeaponVtableOffset)
        : (holster ? kServerHolsterWeaponVtableOffset
            : transition == NativeWeaponTransition::Deploy ? kServerDeployWeaponVtableOffset : kServerEnableWeaponVtableOffset);
    const auto nativeTransition = VirtualFunction<PlayerWeaponTransition>(player, offset);
    if (!nativeTransition) return false;
    nativeTransition(player);
    return NativeWeaponPermission(player, side) == holster;
}
bool ReleaseNativeWeaponLock(std::uintptr_t player, MovementDomain side, std::uint32_t ownedHandle, bool allowSlowDeploy = true)
{
    if (!player || !HasMovementWeaponLockHandle(ownedHandle)) return true;
    const auto permission = NativeWeaponPermissionValue(player, side);
    if (!(permission & kWeaponPermissionLockBit)) return true;
    if (permission & ~kWeaponPermissionLockBit) return false;
    const auto activeWeapon = NativeActiveWeapon(player, side);
    const auto activeHandle = NetworkEntityHandle(NativeEntityHandle(activeWeapon, side));
    // DeployWeapon is the slow-animation path. If the owner is no longer
    // active, use EnableWeapon rather than DeployWeapon on the replacement.
    const auto transition = activeHandle == ownedHandle && allowSlowDeploy
        ? NativeWeaponTransition::Deploy : NativeWeaponTransition::Enable;
    return CallNativeWeaponTransition(player, side, transition);
}
bool ApplyMovementWeaponLock(MovementEvaluation& sample, bool enabled)
{
    auto& ownedHandle = sample.state[kMovementWeaponLockStateSlot];
    if (!enabled)
    {
        if (HasMovementWeaponLockHandle(ownedHandle)
            && !ReleaseNativeWeaponLock(sample.player, sample.domain, ownedHandle, sample.alive))
        {
            sample.failed = true;
            return false;
        }
        ownedHandle = 0;
        return true;
    }
    if (!sample.player || !sample.alive)
    {
        if (HasMovementWeaponLockHandle(ownedHandle)
            && !ReleaseNativeWeaponLock(sample.player, sample.domain, ownedHandle, false))
        {
            sample.failed = true;
            return false;
        }
        ownedHandle = 0;
        return false;
    }
    if (ownedHandle == kUnavailableWeaponLock) return false;

    const auto activeWeapon = NativeActiveWeapon(sample.player, sample.domain);
    const auto currentHandle = NetworkEntityHandle(NativeEntityHandle(activeWeapon, sample.domain));
    if (!HasMovementWeaponLockHandle(currentHandle))
    {
        if (HasMovementWeaponLockHandle(ownedHandle)
            && !ReleaseNativeWeaponLock(sample.player, sample.domain, ownedHandle, false))
        {
            sample.failed = true;
            return false;
        }
        ownedHandle = kUnavailableWeaponLock;
        return false;
    }
    if (HasMovementWeaponLockHandle(ownedHandle))
    {
        if (ownedHandle != currentHandle)
        {
            if (!ReleaseNativeWeaponLock(sample.player, sample.domain, ownedHandle, false))
            {
                sample.failed = true;
                return false;
            }
            ownedHandle = kUnavailableWeaponLock;
            return false;
        }
        if (!NativeWeaponPermission(sample.player, sample.domain))
        {
            ownedHandle = kUnavailableWeaponLock;
            return false;
        }
        return true;
    }
    if (ownedHandle != 0 || NativeWeaponPermissionValue(sample.player, sample.domain) != 0)
    {
        ownedHandle = kUnavailableWeaponLock;
        return false;
    }
    if (!CallNativeWeaponTransition(sample.player, sample.domain, NativeWeaponTransition::Holster))
    {
        sample.failed = true;
        return false;
    }
    ownedHandle = currentHandle;
    return true;
}
std::uint32_t NetworkEntityHandle(std::uint32_t nativeHandle)
{
    // Native entity creation transmits ten serial bits. Keep the full native
    // serial for server ownership, but expose the same identity in both VMs.
    if (nativeHandle == 0xFFFFFFFFu || (nativeHandle & 0xFFFFu) >= kNetworkEntityLimit) return 0xFFFFFFFFu;
    return nativeHandle & kNetworkHandleMask;
}
int AuthorityPlayerSlot(std::uintptr_t player)
{
    if (!nativeServer.ready || !player) return -1;
    const auto globals = MovementGlobals(MovementDomain::Authority);
    if (!globals) return -1;
    const auto edicts = Field<std::uintptr_t>(globals, 0x78);
    const auto edict = Field<std::uintptr_t>(player, 0x40);
    const int clients = Field<int>(globals, 0x20);
    if (!edicts || edict <= edicts || (edict - edicts) % 56 || clients < 1 || clients > kPlayerSlots) return -1;
    const auto index = (edict - edicts) / 56;
    return index <= static_cast<unsigned>(clients) ? static_cast<int>(index) - 1 : -1;
}
bool CommandAfter(std::uint32_t a, std::uint32_t b)
{
    return static_cast<std::int32_t>(a - b) > 0;
}
bool FiniteVector(const MoveVector& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
struct NativeJumpState
{
    int remaining{};
    float power{};
    bool operator==(const NativeJumpState& other) const { return remaining == other.remaining && power == other.power; }
};
NativeJumpState ReadNativeJumpState(std::uintptr_t player, MovementDomain side)
{
    const bool client = side == MovementDomain::Client;
    return {Field<int>(player, client ? 0x26BC : 0xDA4), Field<float>(player, client ? 0x26C4 : 0xDAC)};
}
void RestoreClientJumpState(std::uintptr_t player, const NativeJumpState& jump)
{
    Field<int>(player, 0x26BC) = jump.remaining;
    Field<float>(player, 0x26C4) = jump.power;
}
struct NativeMovementBaseline
{
    std::uint32_t frame{}, acknowledged{};
    bool valid{};
};
NativeMovementBaseline committedMovementBaseline;
struct CommandState
{
    MovementState state{};
    NativeJumpState jump;
    std::uint32_t command{};
    float time{};
    bool valid{};
};
struct FrozenAuthorityFrame
{
    char command[kMovementAuthorityBytes]{};
    std::uint32_t tick{};
    std::uint16_t length{};
    bool valid{};
};
using AuthorityFrameHistory = std::array<FrozenAuthorityFrame, kCommandStorage>;
struct AuthorityPlayer
{
    std::uintptr_t player{};
    const MovementProgram* program{};
    MovementState state{};
    std::uint32_t handle{0xFFFFFFFFu}, generation{}, command{};
    std::uint32_t publishedCommand{}, pendingWeaponHandle{}, nativeWeaponLockHandle{};
    float time{}, publishedTime{};
    MovementState publishedState{};
    std::unique_ptr<AuthorityFrameHistory> frames;
    bool pendingWeaponRelease{}, statePending{}, everEnabled{}, hasPublishedState{};
};
std::array<AuthorityPlayer, kPlayerSlots> authorityPlayers;
std::atomic<unsigned> enabledAuthorityPlayers{};
std::atomic<unsigned> authoritySessionCount{};
std::shared_mutex authorityFrameMutex;
std::uint32_t nextGeneration{};

struct AuthorityMessage
{
    MovementState state{};
    NativeJumpState jump;
    char name[33]{}, fingerprint[65]{};
    std::uint32_t generation{}, handle{}, command{}, frame{};
    float time{};
    unsigned slots{};
    bool enabled{}, valid{};
};
struct PresentedCommand
{
    std::uint32_t command{}, mask{};
    bool valid{};
};
struct PresentationEvent
{
    const MovementProgram* program{};
    std::uint32_t command{};
    int event{};
    MoveVector value;
};
struct ClientMovement
{
    const MovementProgram* program{};
    std::uint32_t generation{}, handle{0xFFFFFFFFu}, baselineCommand{}, previousAck{};
    MovementState baseline{};
    NativeJumpState baselineJump;
    AuthorityMessage acceptedAuthority;
    std::array<CommandState, kCommandStorage> predictions{};
    std::array<AuthorityMessage, kCommandStorage> authorities{};
    std::array<PresentedCommand, kCommandStorage> presented{};
    std::array<PresentationEvent, kPresentationQueue> events{};
    unsigned eventCount{};
    bool receivedAuthority{};
    bool baselineValid{}, ackValid{}, inUpdate{}, needsRestore{}, restored{}, failed{}, disconnectQueued{};
    bool inputCapable{};
    const ConVarR1* predictionControl{};
    bool predicting{true}, pendingWeaponRelease{};
    std::uint32_t pendingWeaponHandle{}, pendingWeaponPlayerHandle{0xFFFFFFFFu};
    std::uintptr_t player{};
    ULONGLONG waitingSince{};
    char failure[192]{};
};
ClientMovement clientMovement;
MovementPresentationSnapshot committedPresentation;
struct PendingAuthority
{
    std::mutex mutex;
    std::array<AuthorityMessage, kCommandStorage> entries{};
    unsigned count{};
    bool malformed{};
    std::atomic<bool> available{};
};
PendingAuthority pendingAuthority;
void QueueClientWeaponRelease(std::uint32_t handle)
{
    if (!HasMovementWeaponLockHandle(handle) || !HasMovementWeaponLockHandle(clientMovement.handle)) return;
    clientMovement.pendingWeaponHandle = handle;
    clientMovement.pendingWeaponPlayerHandle = clientMovement.handle;
    clientMovement.pendingWeaponRelease = true;
}
void ClearClientWeaponRelease()
{
    clientMovement.pendingWeaponHandle = 0;
    clientMovement.pendingWeaponPlayerHandle = 0xFFFFFFFFu;
    clientMovement.pendingWeaponRelease = false;
}

void QueueClientReset(const MovementProgram* program, std::uint32_t command)
{
    if (!program || !program->presentation[0]) return;
    if (clientMovement.eventCount < kPresentationQueue)
        clientMovement.events[clientMovement.eventCount++] = {program, command, -1, {}};
}
void FailClientMovement(const char* reason)
{
    if (clientMovement.failed) return;
    clientMovement.failed = true;
    strncpy_s(clientMovement.failure, reason, _TRUNCATE);
    Warning("R1Delta movement: %s\n", reason);
    QueueClientReset(clientMovement.program, clientMovement.baselineCommand);
}
void DispatchClientPresentation()
{
    const auto count = clientMovement.eventCount;
    clientMovement.eventCount = 0;
    for (unsigned index = 0; index < count; ++index)
    {
        const auto event = clientMovement.events[index];
        DeliverMovementPresentation(*event.program, event.command, event.event, event.value);
    }
}
void QueueMovementDisconnect()
{
    if (!clientMovement.failed || clientMovement.disconnectQueued) return;
    clientMovement.disconnectQueued = true;
    DispatchClientPresentation();
    // Prediction's callers still hold native frame/entity pointers. Let the
    // normal command-buffer boundary perform teardown after they have unwound.
    // Failure reasons are internal fixed messages, never script/network text.
    char command[sizeof(clientMovement.failure) + 16];
    sprintf_s(command, "disconnect \"%s\"\n", clientMovement.failure);
    Cbuf_AddText(0, command, 0);
}
void StopAuthorityPlayer(AuthorityPlayer& entry)
{
    if (!entry.pendingWeaponRelease && entry.player)
    {
        const auto ownedHandle = HasMovementWeaponLockHandle(entry.nativeWeaponLockHandle)
            ? entry.nativeWeaponLockHandle : entry.state[kMovementWeaponLockStateSlot];
        if (HasMovementWeaponLockHandle(ownedHandle))
        {
            entry.pendingWeaponHandle = ownedHandle;
            entry.pendingWeaponRelease = true;
            ++pendingWeaponReleaseCount;
        }
    }
    entry.nativeWeaponLockHandle = 0;
    if (!entry.pendingWeaponRelease) entry.pendingWeaponHandle = 0;
    if (entry.program)
    {
        entry.program = nullptr;
        --enabledAuthorityPlayers;
    }
    entry.state = {};
    entry.publishedState = {};
    entry.publishedCommand = entry.command;
    entry.statePending = false;
    entry.hasPublishedState = false;
}
void AbortAuthorityProgram(const MovementProgram& program)
{
    Warning("R1Delta movement: program '%s' failed; disabling its authoritative session for every opted-in player.\n", program.name);
    std::unique_lock<std::shared_mutex> lock(authorityFrameMutex);
    for (auto& entry : authorityPlayers)
        if (entry.program == &program) StopAuthorityPlayer(entry);
}

void ReadMovementValues(MovementEvaluation& sample)
{
    const auto move = sample.moveData;
    sample.values.origin = Field<MoveVector>(move, 0xC0);
    sample.values.velocity = Field<MoveVector>(move, 0x80);
    sample.values.angles = Field<MoveVector>(move, 0x10);
    sample.values.buttons = Field<std::uint32_t>(move, 0x1C);
    sample.values.oldButtons = Field<std::uint32_t>(move, 0x20);
    sample.values.forwardSpeed = Field<float>(move, 0x24);
    sample.values.sideSpeed = Field<float>(move, 0x28);
    sample.values.upSpeed = Field<float>(move, 0x2C);
    sample.values.maxSpeed = Field<float>(move, 0x78);
    sample.values.clientMaxSpeed = Field<float>(move, 0x7C);
    const bool client = sample.domain == MovementDomain::Client;
    sample.values.remainingAirJumps = Field<int>(sample.player, client ? 0x26BC : 0xDA4);
    sample.values.jumpPower = Field<float>(sample.player, client ? 0x26C4 : 0xDAC);
}
void WriteMovementValues(const MovementEvaluation& sample)
{
    const auto move = sample.moveData;
    const auto& value = sample.values;
    const bool changedWish = value.forwardSpeed != Field<float>(move, 0x24) || value.sideSpeed != Field<float>(move, 0x28);
    Field<MoveVector>(move, 0xC0) = value.origin;
    Field<MoveVector>(move, 0x80) = value.velocity;
    Field<std::uint32_t>(move, 0x1C) = value.buttons;
    Field<std::uint32_t>(move, 0x20) = value.oldButtons;
    Field<float>(move, 0x24) = value.forwardSpeed;
    Field<float>(move, 0x28) = value.sideSpeed;
    Field<float>(move, 0x2C) = value.upSpeed;
    if (changedWish)
    {
        // Keep the native planar wish direction coherent with edited inputs.
        MoveVector wish{
            value.forwardSpeed * Field<float>(move, 0x54) + value.sideSpeed * Field<float>(move, 0x60),
            value.forwardSpeed * Field<float>(move, 0x58) + value.sideSpeed * Field<float>(move, 0x64), 0.0f};
        const float squared = wish.x * wish.x + wish.y * wish.y;
        if (squared > 1.0f)
        {
            const float inverse = 1.0f / (std::sqrt(squared) + 1.1754944e-38f);
            wish.x *= inverse; wish.y *= inverse;
        }
        Field<MoveVector>(move, 0x6C) = wish;
    }
    const bool client = sample.domain == MovementDomain::Client;
    const auto countOffset = client ? 0x26BCu : 0xDA4u;
    const auto powerOffset = client ? 0x26C4u : 0xDACu;
    if (Field<int>(sample.player, countOffset) != value.remainingAirJumps)
    {
        if (!client)
        {
            const auto local = sample.player + 0xD78;
            VirtualFunction<void(__fastcall*)(std::uintptr_t, void*)>(local, 0x10)(local, reinterpret_cast<void*>(sample.player + countOffset));
        }
        Field<int>(sample.player, countOffset) = value.remainingAirJumps;
    }
    if (Field<float>(sample.player, powerOffset) != value.jumpPower)
    {
        if (!client)
        {
            const auto local = sample.player + 0xD78;
            VirtualFunction<void(__fastcall*)(std::uintptr_t, void*)>(local, 0)(local, reinterpret_cast<void*>(sample.player + powerOffset));
        }
        Field<float>(sample.player, powerOffset) = value.jumpPower;
    }
}
void ReadMovementContext(MovementEvaluation& sample)
{
    using PlayerBoolean = bool(__fastcall*)(std::uintptr_t);
    using PlayerVector = const MoveVector*(__fastcall*)(std::uintptr_t);
    using EyeVector = MoveVector*(__fastcall*)(std::uintptr_t, MoveVector*);
    const auto& native = NativeFor(sample.domain);
    const bool client = sample.domain == MovementDomain::Client;
    sample.flags = Field<std::uint32_t>(sample.player, client ? 0x1E0 : 0x168);
    sample.moveType = Field<unsigned char>(sample.player, client ? 0x24A : 0x1BC);
    sample.alive = client
        ? reinterpret_cast<PlayerBoolean>(native.module + 0x27F70)(sample.player)
        : VirtualFunction<PlayerBoolean>(sample.player, 0x228)(sample.player);
    sample.titan = VirtualFunction<PlayerBoolean>(sample.player, client ? 0x540 : 0x2B8)(sample.player);
    sample.grounded = reinterpret_cast<PlayerBoolean>(native.module + (client ? 0x28330 : 0x3CC950))(sample.player);
    sample.wallRunning = reinterpret_cast<PlayerBoolean>(native.module + (client ? 0x39030 : 0x4DFF80))(sample.player);
    sample.traversing = sample.moveType == 13;
    sample.ziplining = reinterpret_cast<PlayerBoolean>(native.module + (client ? 0x39600 : 0x4E1C50))(sample.player);
    if (client)
    {
        sample.hullMin = *reinterpret_cast<PlayerVector>(native.module + 0x28560)(sample.player);
        sample.hullMax = *reinterpret_cast<PlayerVector>(native.module + 0x28570)(sample.player);
    }
    else
    {
        sample.hullMin = *reinterpret_cast<PlayerVector>(native.module + 0x5338C0)(sample.movement);
        sample.hullMax = *reinterpret_cast<PlayerVector>(native.module + 0x533920)(sample.movement);
    }
    MoveVector eye;
    VirtualFunction<EyeVector>(sample.player, client ? 0x5B8 : 0x448)(sample.player, &eye);
    const auto* origin = client
        ? VirtualFunction<PlayerVector>(sample.player, 0x48)(sample.player)
        : reinterpret_cast<PlayerVector>(native.module + 0x7DFA0)(sample.player);
    sample.eyeOffset = {eye.x - origin->x, eye.y - origin->y, eye.z - origin->z};
}
bool ReadCommandGravity(NativeMovement& native, float& value)
{
    if (!native.gravity)
    {
        if (!IsCvarInterfacePublished() || !cvarinterface || !OriginalCCVar_FindVar2) return false;
        native.gravity = IsR1ODedicatedServer()
            ? static_cast<const void*>(CCVar_FindVar2(cvarinterface, "sv_gravity"))
            : static_cast<const void*>(OriginalCCVar_FindVar2(cvarinterface, "sv_gravity"));
        if (!native.gravity) return false;
    }
    if (IsR1ODedicatedServer())
    {
        const auto* parent = static_cast<const ConVarR1O*>(native.gravity)->m_pParent;
        if (!parent) return false;
        value = parent->m_Value.m_fValue;
    }
    else
    {
        const auto* parent = static_cast<const ConVarR1*>(native.gravity)->m_pParent;
        if (!parent) return false;
        value = parent->m_Value.m_fValue;
    }
    return std::isfinite(value);
}
bool FillMovementEvaluation(MovementEvaluation& sample, MovementDomain side, std::uintptr_t movement,
    std::uintptr_t player, std::uintptr_t command, const MovementProgram& program, const MovementState& before)
{
    sample.domain = side;
    sample.program = &program;
    sample.movement = movement;
    sample.player = player;
    sample.playerHandle = NetworkEntityHandle(NativeEntityHandle(player, side));
    sample.moveData = Field<std::uintptr_t>(movement, 0x10);
    sample.command = Field<std::uint32_t>(command, 0);
    sample.tick = Field<std::uint32_t>(command, 4);
    ReadMovementCommandInput(command, true, sample.customButtons);
    sample.state = before;
    const auto globals = MovementGlobals(side);
    if (!globals || !sample.moveData) return false;
    sample.time = Field<float>(globals, 0x10);
    sample.frameTime = Field<float>(globals, 0x1C);
    if (!ReadCommandGravity(NativeFor(side), sample.worldGravity))
    {
        Warning("R1Delta movement: native replicated sv_gravity is unavailable or non-finite.\n");
        return false;
    }
    ReadMovementValues(sample);
    ReadMovementContext(sample);
    return std::isfinite(sample.time) && std::isfinite(sample.frameTime) && sample.frameTime >= 0.0f
        && FiniteVector(sample.values.origin) && FiniteVector(sample.values.velocity)
        && FiniteVector(sample.values.angles) && FiniteVector(sample.hullMin) && FiniteVector(sample.hullMax)
        && FiniteVector(sample.eyeOffset);
}
void RecordClientEvents(const MovementEvaluation& sample, bool firstPrediction)
{
    if (!firstPrediction || !sample.eventMask || !sample.program->presentation[0]) return;
    auto& ledger = clientMovement.presented[sample.command % kCommandStorage];
    if (!ledger.valid || ledger.command != sample.command) ledger = {sample.command, 0, true};
    const auto fresh = sample.eventMask & ~ledger.mask;
    for (unsigned event = 0; event < kMovementEvents; ++event)
    {
        if (!(fresh & (1u << event))) continue;
        if (clientMovement.eventCount == kPresentationQueue)
        {
            FailClientMovement("Movement presentation queue overflow");
            return;
        }
        clientMovement.events[clientMovement.eventCount++] = {sample.program, sample.command, static_cast<int>(event), sample.events[event]};
    }
    ledger.mask |= fresh;
}
bool FillCommandCheckpointContext(MovementEvaluation& sample, CheckpointContext& context)
{
    if (!ReadCheckpointPlayerContext(sample.domain, sample.player, context)) return false;
    context.domain = sample.domain;
    context.player = sample.player;
    context.movement = sample.movement;
    context.moveData = sample.moveData;
    context.overrides = sample.overrides;
    context.viewForward = Field<MoveVector>(sample.moveData, 0x54);
    context.inputDirection = Field<MoveVector>(sample.moveData, 0x6C);
    context.waterLevel = Field<int>(sample.movement, 0x18);
    context.duckRequested = (sample.values.buttons & 4u) != 0 || context.crouchToggled;
    return true;
}
bool EvaluateMovementCheckpoint(CheckpointContext& context)
{
    auto& sample = *static_cast<MovementEvaluation*>(context.user);
    if (sample.failed || !(sample.overrides->checkpointMask & (1u << static_cast<unsigned>(context.stage)))) return false;
    ReadMovementValues(sample);
    ReadMovementContext(sample);
    sample.grounded = context.grounded;
    sample.values.velocity = context.velocity;
    auto* previous = sample.checkpoint;
    sample.checkpoint = &context;
    sample.phase = 3 + static_cast<int>(context.stage);
    sample.replaceFriction = false;
    const bool ok = ExecuteMovementSource(sample);
    sample.checkpoint = previous;
    if (!ok)
    {
        sample.failed = true;
        *sample.overrides = {};
        return false;
    }
    WriteMovementValues(sample);
    context.velocity = sample.values.velocity;
    return context.stage != CheckpointStage::Friction || sample.replaceFriction;
}
std::uintptr_t EvaluateNativeMovement(MovementDomain side, std::uintptr_t movement)
{
    auto& native = NativeFor(side);
    if (solvingMovement || (side == MovementDomain::Client
        ? (!clientMovement.program && !clientMovement.pendingWeaponRelease)
        : (!enabledAuthorityPlayers && !pendingWeaponReleaseCount.load(std::memory_order_relaxed))))
        return native.solve(movement);
    const auto player = Field<std::uintptr_t>(movement, 8);
    if (!player) return native.solve(movement);
    const auto command = Field<std::uintptr_t>(player, side == MovementDomain::Client ? 0x3238 : 0x1998);
    if (!command) return native.solve(movement);
    if (side == MovementDomain::Client && clientMovement.pendingWeaponRelease)
    {
        if (!clientMovement.predicting) return native.solve(movement);
        const auto playerHandle = NetworkEntityHandle(NativeEntityHandle(player, side));
        if (playerHandle != clientMovement.pendingWeaponPlayerHandle)
            ClearClientWeaponRelease();
        else if (!ReleaseNativeWeaponLock(player, side, clientMovement.pendingWeaponHandle, false))
            return native.solve(movement);
        else
            ClearClientWeaponRelease();
    }
    if (side == MovementDomain::Client && !clientMovement.program)
        return native.solve(movement);
    const auto number = Field<std::uint32_t>(command, 0);
    const MovementProgram* program{};
    const MovementState* before{};
    AuthorityPlayer* authority{};
    if (side == MovementDomain::Client)
    {
        if (player != clientMovement.player || !clientMovement.inUpdate || clientMovement.failed)
            return native.solve(movement);
        if (!clientMovement.baselineValid || (clientMovement.needsRestore && !clientMovement.restored))
        {
            FailClientMovement("Movement native prediction baseline was not restored");
            return native.solve(movement);
        }
        if (!CommandAfter(number, clientMovement.baselineCommand)
            || number - clientMovement.baselineCommand > kCommandWindow)
        {
            FailClientMovement("Movement command is outside the native prediction window");
            return native.solve(movement);
        }
        if (number == clientMovement.baselineCommand + 1) before = &clientMovement.baseline;
        else
        {
            const auto& preceding = clientMovement.predictions[(number - 1) % kCommandStorage];
            if (!preceding.valid || preceding.command != number - 1)
            {
                FailClientMovement("Movement predecessor snapshot is unavailable");
                return native.solve(movement);
            }
            before = &preceding.state;
        }
        program = clientMovement.program;
    }
    else
    {
        const int slot = AuthorityPlayerSlot(player);
        if (slot < 0) return native.solve(movement);
        authority = &authorityPlayers[slot];
        if (authority->player != player || authority->handle != NativeEntityHandle(player, side))
        {
            if (authority->pendingWeaponRelease)
            {
                authority->pendingWeaponRelease = false;
                authority->pendingWeaponHandle = 0;
                --pendingWeaponReleaseCount;
            }
            return native.solve(movement);
        }
        if (authority->pendingWeaponRelease)
        {
            if (!ReleaseNativeWeaponLock(player, side, authority->pendingWeaponHandle, false))
                return native.solve(movement);
            authority->pendingWeaponRelease = false;
            authority->pendingWeaponHandle = 0;
            --pendingWeaponReleaseCount;
        }
        if (!authority->program) return native.solve(movement);
        // Native loss recovery can synthesize steps by advancing only tick_count
        // in its last usercmd. Follow every real authority solve, including those
        // repeated command numbers; the entity-frame snapshot owns reconciliation.
        program = authority->program;
        before = &authority->state;
    }
    const auto session = authority ? authority->generation : clientMovement.generation;
    MovementEvaluation sample;
    CheckpointOverrides overrides;
    CheckpointContext commandContext;
    sample.overrides = &overrides;
    sample.checkpoint = &commandContext;
    if (!FillMovementEvaluation(sample, side, movement, player, command, *program, *before)
        || !FillCommandCheckpointContext(sample, commandContext) || !ExecuteMovementSource(sample))
    {
        if (authority) AbortAuthorityProgram(*program);
        else FailClientMovement("Movement setup callback failed; inspect the script console");
        return native.solve(movement);
    }
    // Keep both pre-solver phases transactional: neither writes native data until
    // both callbacks have completed. Only native-owned evaluation data crosses phases.
    sample.phase = 1;
    if (!ExecuteMovementSource(sample))
    {
        if (authority) AbortAuthorityProgram(*program);
        else FailClientMovement("Movement simulation callback failed; inspect the script console");
        return native.solve(movement);
    }
    WriteMovementValues(sample);
    overrides.frictionScale = sample.frictionScale;
    MovementCheckpointScope checkpoints(side, player, movement, sample.moveData,
        overrides, &EvaluateMovementCheckpoint, &sample);
    if (!checkpoints.Ready())
    {
        if (authority) AbortAuthorityProgram(*program);
        else FailClientMovement("Movement native checkpoint scope is unavailable");
        return native.solve(movement);
    }
    solvingMovement = &sample;
    const auto result = native.solve(movement);
    solvingMovement = nullptr;
    if (sample.failed || checkpoints.Failed())
    {
        if (authority) AbortAuthorityProgram(*program);
        else FailClientMovement("Movement native checkpoint callback failed; inspect the script console");
        return result;
    }
    // Ordinary native touch/think callbacks may explicitly change the opt-in.
    // Never finish or publish an old program into a replacement session.
    if (authority ? (authority->program != program || authority->generation != session)
        : (clientMovement.program != program || clientMovement.generation != session))
        return result;
    ReadMovementValues(sample);
    ReadMovementContext(sample);
    FillCommandCheckpointContext(sample, commandContext);
    sample.phase = 2;
    if (!ExecuteMovementSource(sample))
    {
        if (authority) AbortAuthorityProgram(*program);
        else FailClientMovement("Movement finish callback failed; inspect the script console");
        return result;
    }
    WriteMovementValues(sample);
    if (authority)
    {
        authority->state = sample.state;
        authority->command = number;
        authority->time = sample.time;
        authority->statePending = true;
    }
    else
    {
        auto& saved = clientMovement.predictions[number % kCommandStorage];
        saved.state = sample.state;
        saved.command = number;
        saved.time = sample.time;
        saved.valid = true;
        RecordClientEvents(sample, Field<unsigned char>(command, 84) == 0);
    }
    return result;
}
std::uintptr_t __fastcall SolveClientMovement(std::uintptr_t movement)
{
    return EvaluateNativeMovement(MovementDomain::Client, movement);
}
std::uintptr_t __fastcall SolveAuthorityMovement(std::uintptr_t movement)
{
    return EvaluateNativeMovement(MovementDomain::Authority, movement);
}
void ApplyPreparedMovementCaps(NativeMovement& native, std::uintptr_t movement)
{
    native.prepare(movement);
    auto* sample = solvingMovement;
    if (!sample || sample->movement != movement || sample->capsApplied) return;
    sample->capsApplied = true;
    // The native dispatcher refreshes both caps after its early preparation.
    // Preserve those fresh values unless the corresponding setter was used.
    auto& maximum = Field<float>(sample->moveData, 0x78);
    auto& clientMaximum = Field<float>(sample->moveData, 0x7C);
    if (sample->maximumEdited || sample->speedScale != 1.0f)
        maximum = (sample->maximumEdited ? sample->values.maxSpeed : maximum) * sample->speedScale;
    if (sample->clientMaximumEdited || sample->speedScale != 1.0f)
        clientMaximum = (sample->clientMaximumEdited ? sample->values.clientMaxSpeed : clientMaximum) * sample->speedScale;
}
void __fastcall ApplyClientMovementCaps(std::uintptr_t movement)
{
    ApplyPreparedMovementCaps(nativeClient, movement);
}
void __fastcall ApplyAuthorityMovementCaps(std::uintptr_t movement)
{
    ApplyPreparedMovementCaps(nativeServer, movement);
}
void __fastcall ReleaseLevelMovementState(std::uintptr_t gameServer)
{
    nativeServer.levelShutdown(gameServer);
    ResetMovementLevel();
}

void DrainAuthorityMessages()
{
    if (!pendingAuthority.available.load(std::memory_order_acquire)) return;
    std::lock_guard<std::mutex> lock(pendingAuthority.mutex);
    if (pendingAuthority.malformed) FailClientMovement("Invalid movement authority snapshot");
    for (unsigned index = 0; index < pendingAuthority.count; ++index)
    {
        const auto& incoming = pendingAuthority.entries[index];
        clientMovement.receivedAuthority = true;
        clientMovement.authorities[incoming.frame % kCommandStorage] = incoming;
    }
    pendingAuthority.count = 0;
    pendingAuthority.malformed = false;
    pendingAuthority.available.store(false, std::memory_order_release);
}
bool WaitForAuthority(const char* reason)
{
    if (!clientMovement.waitingSince) clientMovement.waitingSince = GetTickCount64();
    if (GetTickCount64() - clientMovement.waitingSince <= kAuthorityWaitMilliseconds) return true;
    FailClientMovement(reason);
    return false;
}
std::uintptr_t __fastcall CommitClientMovementBaseline(std::uintptr_t prediction, int acknowledgedCommands,
    unsigned char changedEntities)
{
    const auto result = nativeClient.completePacket(prediction, acknowledgedCommands, changedEntities);
    if (!clientEngine) return result;
    // Selection precedes the engine's final old-world prediction pass. Only
    // this completed post-packet callback follows entity application, original
    // prediction-buffer publication, and native acknowledgement promotion.
    const auto state = clientEngine + 0x797070;
    const auto frame = Field<std::uintptr_t>(state, 104536);
    committedMovementBaseline = frame
        ? NativeMovementBaseline{Field<std::uint32_t>(frame, 24), Field<std::uint32_t>(state, 104576), true}
        : NativeMovementBaseline{};
    return result;
}
std::uintptr_t __fastcall RestoreClientMovement(std::uintptr_t player, const char* label, int relativeCommand, int kind)
{
    const auto result = nativeClient.restore(player, label, relativeCommand, kind);
    if (clientMovement.inUpdate && player == clientMovement.player && relativeCommand == -1 && kind == 2)
    {
        if (!committedMovementBaseline.valid
            || committedMovementBaseline.frame != clientMovement.acceptedAuthority.frame
            || committedMovementBaseline.acknowledged != clientMovement.baselineCommand)
            FailClientMovement("Movement native restore has a different completed entity baseline");
        else
        {
            RestoreClientJumpState(player, clientMovement.baselineJump);
            clientMovement.restored = true;
        }
    }
    return result;
}
std::uintptr_t __fastcall RunClientMovementCommand(std::uintptr_t prediction, std::uintptr_t player,
    std::uintptr_t command, std::uintptr_t helper)
{
    if (!clientMovement.program || !clientMovement.inUpdate || clientMovement.failed || player != clientMovement.player)
        return nativeClient.command(prediction, player, command, helper);
    const auto number = Field<std::uint32_t>(command, 0);
    const auto& preceding = clientMovement.predictions[(number - 1) % kCommandStorage];
    if (number == clientMovement.baselineCommand + 1)
        RestoreClientJumpState(player, clientMovement.baselineJump);
    else if (preceding.valid && preceding.command == number - 1)
        RestoreClientJumpState(player, preceding.jump);
    else
        FailClientMovement("Movement native-input predecessor snapshot is unavailable");
    const auto result = nativeClient.command(prediction, player, command, helper);
    auto& saved = clientMovement.predictions[number % kCommandStorage];
    if (saved.valid && saved.command == number)
        saved.jump = ReadNativeJumpState(player, MovementDomain::Client);
    return result;
}
std::uintptr_t __fastcall UpdateClientMovement(std::uintptr_t prediction, int slot, unsigned char newWorld,
    unsigned char validFrame, std::uint32_t acknowledged, int outgoing)
{
    if (slot != 0 || clientMovement.inUpdate)
        return nativeClient.update(prediction, slot, newWorld, validFrame, acknowledged, outgoing);
    DrainAuthorityMessages();
    if (clientMovement.failed) { QueueMovementDisconnect(); return 0; }
    if (!clientMovement.program && !clientMovement.receivedAuthority)
        return nativeClient.update(prediction, slot, newWorld, validFrame, acknowledged, outgoing);
    if (!validFrame)
        return nativeClient.update(prediction, slot, newWorld, validFrame, acknowledged, outgoing);
    // The live selected-frame pointer can already name the next world while
    // this Update still predicts the previous one. Admit only the completed
    // original-entity baseline, paired with its actual native acknowledgement.
    const auto baseline = committedMovementBaseline;
    if (!baseline.valid || baseline.acknowledged != acknowledged)
    {
        if (!WaitForAuthority("Movement native entity baseline is not committed"))
        {
            QueueMovementDisconnect(); return 0;
        }
        return nativeClient.update(prediction, slot, newWorld, 0, acknowledged, outgoing);
    }
    const auto frame = baseline.frame;
    const auto& staged = clientMovement.authorities[frame % kCommandStorage];
    const auto& message = staged.valid && staged.frame == frame ? staged : clientMovement.acceptedAuthority;
    const bool exact = message.valid && message.frame == frame;
    if (!exact)
    {
        if (!WaitForAuthority("Movement authority did not match the accepted native entity frame"))
        {
            QueueMovementDisconnect(); return 0;
        }
        return nativeClient.update(prediction, slot, newWorld, 0, acknowledged, outgoing);
    }
    if (!clientMovement.predictionControl && IsCvarInterfacePublished() && cvarinterface && OriginalCCVar_FindVar2)
        clientMovement.predictionControl = OriginalCCVar_FindVar2(cvarinterface, "cl_predict");
    const auto* predictionControl = clientMovement.predictionControl;
    if (!predictionControl || !predictionControl->m_pParent)
    {
        FailClientMovement("The native cl_predict control is unavailable");
        QueueMovementDisconnect(); return 0;
    }
    const bool predicting = predictionControl->m_pParent->m_Value.m_nValue != 0;
    const bool changedPrediction = clientMovement.predicting != predicting;
    using LocalPlayer = std::uintptr_t(__fastcall*)(int);
    const auto player = reinterpret_cast<LocalPlayer>(nativeClient.module + 0x7B1B0)(slot);
    if (!player || NetworkEntityHandle(NativeEntityHandle(player, MovementDomain::Client)) != message.handle
        || (predicting && !Field<unsigned char>(player, 1384)))
    {
        if (!WaitForAuthority("Movement player identity or native predictable state is not ready"))
        {
            QueueMovementDisconnect(); return 0;
        }
        return nativeClient.update(prediction, slot, newWorld, 0, acknowledged, outgoing);
    }
    clientMovement.waitingSince = 0;
    clientMovement.player = player;
    // A completed baseline may legitimately revisit an older cached frame.
    // A changed world invalidates script history in either direction; retain
    // only the presentation ledger when the program/session remains the same.
    const bool changedBaselineFrame = clientMovement.acceptedAuthority.valid
        && clientMovement.acceptedAuthority.frame != frame;
    if (clientMovement.ackValid && CommandAfter(clientMovement.previousAck, acknowledged))
    {
        FailClientMovement("Movement native command acknowledgement moved backwards");
        QueueMovementDisconnect(); return 0;
    }
    const MovementProgram* program = message.enabled ? FindMovementProgram(MovementDomain::Client, message.name) : nullptr;
    if (message.enabled && (!program || program->slots != message.slots || std::strcmp(program->fingerprint, message.fingerprint)))
    {
        FailClientMovement("Movement program is missing or differs from the authoritative server");
        QueueMovementDisconnect(); return 0;
    }
    if (program) clientMovement.inputCapable = true;
    const bool changedSession = clientMovement.generation != message.generation || clientMovement.handle != message.handle
        || clientMovement.program != program;
    if (changedSession)
    {
        if (program && HasMovementWeaponLockHandle(message.state[kMovementWeaponLockStateSlot]))
            ClearClientWeaponRelease();
        else
        {
            const auto owned = committedPresentation.valid
                && committedPresentation.session == clientMovement.generation
                ? committedPresentation.state[kMovementWeaponLockStateSlot]
                : clientMovement.baseline[kMovementWeaponLockStateSlot];
            QueueClientWeaponRelease(owned);
        }
        if (clientMovement.generation) presentation::ResetSession(clientMovement.generation);
        QueueClientReset(clientMovement.program, acknowledged);
        clientMovement.program = program;
        clientMovement.generation = message.generation;
        clientMovement.handle = message.handle;
        committedPresentation = {};
        clientMovement.baselineValid = false;
        for (auto& state : clientMovement.predictions) state.valid = false;
        for (auto& event : clientMovement.presented) event.valid = false;
    }
    clientMovement.acceptedAuthority = message;
    const auto& predicted = clientMovement.predictions[acknowledged % kCommandStorage];
    const bool matchingPrediction = predicted.valid && predicted.command == acknowledged
        && predicted.state == message.state && predicted.jump == message.jump;
    const bool repeatedBaseline = clientMovement.baselineValid && clientMovement.baselineCommand == acknowledged
        && clientMovement.baseline == message.state && clientMovement.baselineJump == message.jump;
    clientMovement.needsRestore = predicting && (changedSession || changedPrediction
        || (program && (changedBaselineFrame || (!matchingPrediction && !repeatedBaseline))));
    clientMovement.restored = false;
    clientMovement.baseline = message.state;
    clientMovement.baselineJump = message.jump;
    clientMovement.baselineCommand = acknowledged;
    clientMovement.baselineValid = true;
    clientMovement.previousAck = acknowledged;
    clientMovement.ackValid = true;
    if (changedPrediction)
        for (auto& state : clientMovement.predictions) state.valid = false;
    clientMovement.predicting = predicting;
    if (clientMovement.needsRestore)
    {
        // Both native command-skip paths must be invalidated. The original
        // Update restores this accepted FRAME's entity state, then replays from
        // its own ACK index. A retransmitted full frame may have an older
        // simulated command; its frozen custom state must travel with its pose.
        Field<int>(prediction, 208 + 88 * slot) = 1;
        Field<unsigned char>(prediction, 256 + 88 * slot) = 1;
        newWorld = 1;
        for (auto& state : clientMovement.predictions) state.valid = false;
    }
    clientMovement.inUpdate = predicting;
    const auto result = nativeClient.update(prediction, slot, newWorld, validFrame, acknowledged, outgoing);
    clientMovement.inUpdate = false;
    if (clientMovement.needsRestore && !clientMovement.restored)
        FailClientMovement("Movement native entity rewind was not observed");
    if (!clientMovement.failed)
    {
        committedPresentation.valid = false;
        const auto finalCommand = predicting ? static_cast<std::uint32_t>(outgoing) : acknowledged;
        const auto& final = clientMovement.predictions[finalCommand % kCommandStorage];
        if (program && (finalCommand == acknowledged || (final.valid && final.command == finalCommand)))
        {
            committedPresentation.program = program;
            committedPresentation.session = message.generation;
            committedPresentation.command = finalCommand;
            committedPresentation.player = player;
            committedPresentation.time = finalCommand == acknowledged ? message.time : final.time;
            committedPresentation.state = finalCommand == acknowledged ? message.state : final.state;
            committedPresentation.valid = true;
        }
    }
    DispatchClientPresentation();
    if (clientMovement.failed) QueueMovementDisconnect();
    return result;
}

struct HookDefinition
{
    std::uintptr_t rva;
    const unsigned char* prefix;
    std::size_t prefixSize;
    void* detour;
    void** original;
};
bool InstallMovementHooks(NativeMovement& native, std::uintptr_t module, const HookDefinition* definitions, unsigned count)
{
    for (unsigned index = 0; index < count; ++index)
        if (std::memcmp(reinterpret_cast<const void*>(module + definitions[index].rva), definitions[index].prefix, definitions[index].prefixSize)) return false;
    for (unsigned index = 0; index < count; ++index)
    {
        void* address = reinterpret_cast<void*>(module + definitions[index].rva);
        if (MH_CreateHook(address, definitions[index].detour, definitions[index].original) != MH_OK) break;
        native.hooks[native.hookCount++] = address;
    }
    bool ok = native.hookCount == count;
    if (ok)
    {
        for (unsigned index = 0; index < count; ++index)
            if (MH_EnableHook(native.hooks[index]) != MH_OK) { ok = false; break; }
    }
    if (!ok)
    {
        for (unsigned index = 0; index < native.hookCount; ++index)
        {
            MH_DisableHook(native.hooks[index]);
            MH_RemoveHook(native.hooks[index]);
        }
        native = {};
        return false;
    }
    native.module = module;
    native.ready = true;
    return true;
}
int HexValue(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
bool ParseHexWord(std::string_view text, std::uint32_t& value)
{
    if (text.size() != 8) return false;
    value = 0;
    for (char c : text)
    {
        const int digit = HexValue(c);
        if (digit < 0) return false;
        value = (value << 4) | static_cast<unsigned>(digit);
    }
    return true;
}
bool ParseAuthorityMessage(const char* command, AuthorityMessage& parsed)
{
    const auto length = strnlen_s(command, kMovementAuthorityBytes);
    if (!length || length == kMovementAuthorityBytes) return false;
    std::array<std::string_view, 12> words;
    unsigned count{};
    const char* cursor = command;
    while (*cursor)
    {
        if (count == words.size() || *cursor == ' ') return false;
        const char* begin = cursor;
        while (*cursor && *cursor != ' ') ++cursor;
        words[count++] = std::string_view(begin, cursor - begin);
        if (*cursor && !*++cursor) return false;
    }
    std::uint32_t remaining{}, powerBits{}, timeBits{};
    if ((count != 9 && count != 12) || words[0] != kAuthorityCommand || (words[1] != "0" && words[1] != "1")
        || !ParseHexWord(words[2], parsed.generation) || !parsed.generation
        || !ParseHexWord(words[3], parsed.handle) || parsed.handle == 0xFFFFFFFFu
        || parsed.handle != NetworkEntityHandle(parsed.handle)
        || !ParseHexWord(words[4], parsed.command) || !ParseHexWord(words[5], parsed.frame)
        || !ParseHexWord(words[6], remaining) || !ParseHexWord(words[7], powerBits)
        || !ParseHexWord(words[8], timeBits)) return false;
    parsed.jump.remaining = static_cast<std::int32_t>(remaining);
    std::memcpy(&parsed.jump.power, &powerBits, sizeof(powerBits));
    std::memcpy(&parsed.time, &timeBits, sizeof(timeBits));
    if (!std::isfinite(parsed.jump.power) || !std::isfinite(parsed.time)) return false;
    parsed.enabled = words[1] == "1";
    if (parsed.enabled)
    {
        if (count != 12 || words[9].empty() || words[9].size() > 32 || words[10].size() != 64) return false;
        for (char c : words[9])
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-')) return false;
        for (char c : words[10]) if (HexValue(c) < 0) return false;
        std::memcpy(parsed.name, words[9].data(), words[9].size());
        std::memcpy(parsed.fingerprint, words[10].data(), words[10].size());
        if (words[11].empty() || words[11].size() % 8
            || words[11].size() > kMovementStateWords * 8
            || words[11].size() < kMovementNativeStateCount * 8) return false;
        const auto serializedWords = static_cast<unsigned>(words[11].size() / 8);
        parsed.slots = serializedWords - kMovementNativeStateCount;
        if (parsed.slots > kMovementSlots) return false;
        for (unsigned index = 0; index < parsed.slots; ++index)
            if (!ParseHexWord(words[11].substr(index * 8, 8), parsed.state[index])) return false;
        for (unsigned index = 0; index < kMovementNativeStateCount; ++index)
            if (!ParseHexWord(words[11].substr((parsed.slots + index) * 8, 8),
                parsed.state[kMovementNativeStateFirst + index])) return false;
    }
    else if (count != 9) return false;
    parsed.valid = true;
    return true;
}
}
bool SetMovementWeaponLock(MovementEvaluation& evaluation, bool enabled)
{
    const bool acquired = ApplyMovementWeaponLock(evaluation, enabled);
    if (acquired && evaluation.domain == MovementDomain::Authority)
    {
        const int slot = AuthorityPlayerSlot(evaluation.player);
        if (slot >= 0)
        {
            auto& entry = authorityPlayers[slot];
            if (entry.player == evaluation.player && entry.program == evaluation.program
                && entry.handle == NativeEntityHandle(evaluation.player, MovementDomain::Authority))
            {
                const auto owner = evaluation.state[kMovementWeaponLockStateSlot];
                entry.nativeWeaponLockHandle = HasMovementWeaponLockHandle(owner) ? owner : 0;
            }
        }
    }
    return acquired;
}

bool VerifyMovementModuleDigest(std::uintptr_t module, const char* expected)
{
    if (!module) return false;
    wchar_t path[32768];
    const auto length = GetModuleFileNameW(reinterpret_cast<HMODULE>(module), path, static_cast<DWORD>(std::size(path)));
    if (!length || length == std::size(path)) return false;
    const auto file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    unsigned char buffer[65536], digest[32];
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (ok) ok = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    while (ok)
    {
        DWORD read{};
        if (!ReadFile(file, buffer, sizeof(buffer), &read, nullptr)) { ok = false; break; }
        if (!read) break;
        ok = BCryptHashData(hash, buffer, read, 0) >= 0;
    }
    if (ok) ok = BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    if (!ok) return false;
    constexpr char digits[] = "0123456789abcdef";
    for (unsigned index = 0; index < std::size(digest); ++index)
        if (expected[index * 2] != digits[digest[index] >> 4] || expected[index * 2 + 1] != digits[digest[index] & 15]) return false;
    return expected[64] == 0;
}
namespace
{
bool ClientInputCapable() { return clientMovement.inputCapable; }
bool ClientInputEnabled(std::uintptr_t player)
{
    const auto* program = ActiveClientMovementProgram(player);
    return program && program->inputCallback[0];
}
void ClientInputFailed(std::uintptr_t, std::uintptr_t)
{
    FailMovementInput("Movement input callback failed; inspect the script console");
}
}
bool AttachClientMovementModule(std::uintptr_t module)
{
    if (nativeClient.ready) return nativeClient.module == module && MovementDomainReady(MovementDomain::Client);
    if (!VerifyMovementModuleDigest(module, kClientDigest) || !VerifyMovementModuleDigest(G_engine, kClientEngineDigest)) return false;
    constexpr unsigned char solve[] = {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x48,0x89,0x7C,0x24,0x18,0x55};
    constexpr unsigned char update[] = {0x44,0x88,0x44,0x24,0x18,0x53,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x68};
    constexpr unsigned char restore[] = {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48};
    constexpr unsigned char prepare[] = {0x40,0x53,0x48,0x83,0xEC,0x20,0x80,0xB9,0x48,0x03,0x00,0x00,0x00,0x48,0x8B,0xD9};
    constexpr unsigned char completePacket[] = {0x44,0x88,0x44,0x24,0x18,0x89,0x54,0x24,0x10,0x48,0x89,0x4C,0x24,0x08,0x55,0x53};
    const HookDefinition definitions[] = {
        {0x3485D0, solve, sizeof(solve), reinterpret_cast<void*>(&SolveClientMovement), reinterpret_cast<void**>(&nativeClient.solve)},
        {0x21C6A0, update, sizeof(update), reinterpret_cast<void*>(&UpdateClientMovement), reinterpret_cast<void**>(&nativeClient.update)},
        {0x2FF940, restore, sizeof(restore), reinterpret_cast<void*>(&RestoreClientMovement), reinterpret_cast<void**>(&nativeClient.restore)},
        {0x21A400, restore, sizeof(restore), reinterpret_cast<void*>(&RunClientMovementCommand), reinterpret_cast<void**>(&nativeClient.command)},
        {0x167400, prepare, sizeof(prepare), reinterpret_cast<void*>(&ApplyClientMovementCaps), reinterpret_cast<void**>(&nativeClient.prepare)},
        {0x21CF60, completePacket, sizeof(completePacket), reinterpret_cast<void*>(&CommitClientMovementBaseline), reinterpret_cast<void**>(&nativeClient.completePacket)}
    };
    if (!InstallMovementHooks(nativeClient, module, definitions, static_cast<unsigned>(std::size(definitions)))) return false;
    const MovementInputCallbacks inputCallbacks{
        &ClientInputCapable, &ClientInputEnabled, &DispatchMovementInput, &ClientInputFailed};
    if (!InstallMovementInputClient(module, inputCallbacks)) return false;
    nativeClient.checkpointsReady = InstallMovementCheckpoints(MovementDomain::Client, module);
    if (!nativeClient.checkpointsReady) return false;
    if (!InstallMovementClientViewSinks(module) || !presentation::InstallClientHooks(module)
        || !presentation::InstallRopeOwnerVisibility(module))
    {
        Warning("R1Delta movement: required client view/activity/rope hooks are unavailable.\n");
        return false;
    }
    clientEngine = G_engine;
    nativeClient.trace = reinterpret_cast<HullTrace>(module + 0x7E6B0);
    return true;
}
bool AttachServerMovementModule(std::uintptr_t module)
{
    if (nativeServer.ready) return nativeServer.module == module && MovementDomainReady(MovementDomain::Authority);
    if (!VerifyMovementModuleDigest(module, kServerDigest)) return false;
    constexpr unsigned char solve[] = {0x48,0x8B,0xC4,0x55,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8D,0x68,0xA1};
    constexpr unsigned char prepare[] = {0x40,0x53,0x48,0x83,0xEC,0x20,0x80,0xB9,0x48,0x03,0x00,0x00,0x00,0x48,0x8B,0xD9};
    constexpr unsigned char levelShutdown[] = {0x40,0x53,0x48,0x83,0xEC,0x20,0x8B,0x05,0x5C,0xEF,0x9E,0x00,0x83,0xE8,0x01,0x48,0x63,0xD8,0x78,0x22,0x0F,0x1F,0x40,0x00};
    const HookDefinition definitions[] = {
        {0x53A880, solve, sizeof(solve), reinterpret_cast<void*>(&SolveAuthorityMovement), reinterpret_cast<void**>(&nativeServer.solve)},
        {0x1584C0, prepare, sizeof(prepare), reinterpret_cast<void*>(&ApplyAuthorityMovementCaps), reinterpret_cast<void**>(&nativeServer.prepare)},
        {0x1468C0, levelShutdown, sizeof(levelShutdown), reinterpret_cast<void*>(&ReleaseLevelMovementState), reinterpret_cast<void**>(&nativeServer.levelShutdown)}
    };
    if (!InstallMovementHooks(nativeServer, module, definitions, static_cast<unsigned>(std::size(definitions)))) return false;
    if (!InstallMovementInputServer(module)) return false;
    nativeServer.checkpointsReady = InstallMovementCheckpoints(MovementDomain::Authority, module);
    if (!nativeServer.checkpointsReady) return false;
    if (!presentation::InstallServerHooks(module))
    {
        Warning("R1Delta movement: required server activity/pose hooks are unavailable.\n");
        return false;
    }
    nativeServer.trace = reinterpret_cast<HullTrace>(module + 0xB4D90);
    return true;
}
bool MovementDomainReady(MovementDomain side)
{
    return NativeFor(side).ready && NativeFor(side).checkpointsReady && (side == MovementDomain::Client
        ? (MovementInputClientReady() && presentation::ClientHooksReady()
            && presentation::RopeOwnerVisibilityReady(nativeClient.module))
        : (MovementInputServerReady() && presentation::ServerHooksReady()));
}
std::uintptr_t ResolveMovementEntity(const MovementEvaluation& sample, std::uint32_t handle)
{
    const auto& native = NativeFor(sample.domain);
    const auto index = handle & 0xFFFFu;
    if (!native.ready || handle == 0xFFFFFFFFu || handle != NetworkEntityHandle(handle)) return 0;
    const bool client = sample.domain == MovementDomain::Client;
    const auto table = client ? Field<std::uintptr_t>(native.module, 0xAEED60) : native.module + 0xD425C8;
    if (!table) return 0;
    const auto entry = table + index * (client ? 0x20u : 0x30u);
    if ((Field<std::uint32_t>(entry, client ? 0x10 : 8) & kNetworkSerialMask) != (handle >> kEntityEntryBits)) return 0;
    return Field<std::uintptr_t>(entry, client ? 8 : 0);
}
bool ReadMovementEntityOrigin(const MovementEvaluation& sample, std::uint32_t handle, MoveVector& origin)
{
    const auto entity = ResolveMovementEntity(sample, handle);
    if (!entity) return false;
    using EntityOrigin = const MoveVector*(__fastcall*)(std::uintptr_t);
    const auto* value = sample.domain == MovementDomain::Client
        ? VirtualFunction<EntityOrigin>(entity, 0x48)(entity)
        : reinterpret_cast<EntityOrigin>(nativeServer.module + 0x7DFA0)(entity);
    if (!value || !FiniteVector(*value)) return false;
    origin = *value;
    return true;
}
bool TraceMovementGeometry(MovementEvaluation& sample, const MoveVector& start, const MoveVector& end,
    const MoveVector& mins, const MoveVector& maxs, std::uint32_t mask, int collisionGroup, MovementRayResult& result)
{
    auto& native = NativeFor(sample.domain);
    if (!native.ready) return false;
    alignas(16) unsigned char trace[160]{};
    native.trace(&start, &end, &mins, &maxs, mask, sample.player, collisionGroup, trace);
    const auto address = reinterpret_cast<std::uintptr_t>(trace);
    result.end = Field<MoveVector>(address, 0x10);
    result.normal = Field<MoveVector>(address, 0x20);
    result.fraction = Field<float>(address, 0x30);
    result.allSolid = Field<unsigned char>(address, 0x3A) != 0;
    result.startSolid = Field<unsigned char>(address, 0x3B) != 0;
    result.hitSky = (Field<unsigned char>(address, 0x54) & 4) != 0;
    result.entity = NetworkEntityHandle(NativeEntityHandle(Field<std::uintptr_t>(address, 0x60), sample.domain));
    const unsigned surfaceIndex = (Field<std::uint16_t>(address, 0x38) >> 4) & 1;
    result.surface = result.fraction < 1.0f
        ? Field<std::int16_t>(address, 0x50 + 2 * surfaceIndex) : -1;
    return FiniteVector(result.end) && FiniteVector(result.normal) && std::isfinite(result.fraction)
        && result.fraction >= 0.0f && result.fraction <= 1.0f;
}
const MovementPresentationSnapshot* ReadMovementPresentation()
{
    return !clientMovement.inUpdate && !clientMovement.failed && committedPresentation.valid
        ? &committedPresentation : nullptr;
}
bool IsMovementEnabledForPlayer(std::uintptr_t player, const char* name)
{
    const int slot = AuthorityPlayerSlot(player);
    if (slot < 0 || !name) return false;
    std::shared_lock<std::shared_mutex> lock(authorityFrameMutex);
    const auto& entry = authorityPlayers[slot];
    return entry.player == player && entry.handle == NativeEntityHandle(player, MovementDomain::Authority)
        && entry.program && (!*name || std::strcmp(entry.program->name, name) == 0);
}
bool ReadMovementAuthorityState(std::uintptr_t player, const char* name, int index,
    std::uint32_t& word, std::uint32_t& command, std::uint32_t& session, float& time)
{
    const int slot = AuthorityPlayerSlot(player);
    if (slot < 0 || !name || index < -1) return false;
    std::shared_lock<std::shared_mutex> lock(authorityFrameMutex);
    const auto& entry = authorityPlayers[slot];
    if (entry.player != player || entry.handle != NativeEntityHandle(player, MovementDomain::Authority)
        || !entry.program || !entry.hasPublishedState || std::strcmp(entry.program->name, name)
        || (index >= 0 && static_cast<unsigned>(index) >= entry.program->slots)) return false;
    if (index >= 0) word = entry.publishedState[static_cast<unsigned>(index)];
    command = entry.publishedCommand;
    session = entry.generation;
    time = entry.publishedTime;
    return true;
}
const MovementProgram* ActiveClientMovementProgram(std::uintptr_t player)
{
    return !clientMovement.failed && clientMovement.player == player ? clientMovement.program : nullptr;
}
std::uint32_t ClientMovementViewSession(std::uintptr_t player)
{
    return ActiveClientMovementProgram(player) ? clientMovement.generation : 0;
}
void FailMovementInput(const char* reason)
{
    FailClientMovement(reason);
    QueueMovementDisconnect();
}
bool EnableMovementForPlayer(std::uintptr_t player, const MovementProgram& program)
{
    if (!MovementDomainReady(MovementDomain::Authority) || !IsMovementSnapshotTransportReady()) return false;
    const int slot = AuthorityPlayerSlot(player);
    if (slot < 0) return false;
    std::unique_lock<std::shared_mutex> lock(authorityFrameMutex);
    auto& entry = authorityPlayers[slot];
    const auto handle = NativeEntityHandle(player, MovementDomain::Authority);
    if (handle == 0xFFFFFFFFu) return false;
    if (entry.player == player && entry.handle == handle && entry.program == &program) return true;
    std::unique_ptr<AuthorityFrameHistory> frames(new (std::nothrow) AuthorityFrameHistory);
    if (!frames) return false;
    const bool hadPendingWeaponRelease = entry.pendingWeaponRelease;
    bool pendingWeaponRelease = hadPendingWeaponRelease && entry.player == player && entry.handle == handle
        && HasMovementWeaponLockHandle(entry.pendingWeaponHandle);
    std::uint32_t pendingWeaponHandle = pendingWeaponRelease ? entry.pendingWeaponHandle : 0;
    if (hadPendingWeaponRelease && !pendingWeaponRelease) --pendingWeaponReleaseCount;
    if (!pendingWeaponRelease && entry.player == player && entry.handle == handle)
    {
        const auto ownedHandle = HasMovementWeaponLockHandle(entry.nativeWeaponLockHandle)
            ? entry.nativeWeaponLockHandle : entry.state[kMovementWeaponLockStateSlot];
        if (HasMovementWeaponLockHandle(ownedHandle))
        {
            pendingWeaponRelease = true;
            pendingWeaponHandle = ownedHandle;
            ++pendingWeaponReleaseCount;
        }
    }
    if (!entry.everEnabled) ++authoritySessionCount;
    if (entry.program) --enabledAuthorityPlayers;
    entry = {};
    entry.frames = std::move(frames);
    entry.player = player;
    entry.handle = handle;
    entry.generation = ++nextGeneration;
    if (!entry.generation) entry.generation = ++nextGeneration;
    entry.program = &program;
    entry.pendingWeaponRelease = pendingWeaponRelease;
    entry.pendingWeaponHandle = pendingWeaponHandle;
    entry.everEnabled = true;
    ++enabledAuthorityPlayers;
    return true;
}
bool DisableMovementForPlayer(std::uintptr_t player)
{
    const int slot = AuthorityPlayerSlot(player);
    if (slot < 0) return false;
    std::unique_lock<std::shared_mutex> lock(authorityFrameMutex);
    auto& entry = authorityPlayers[slot];
    if (!entry.everEnabled || entry.player != player || entry.handle != NativeEntityHandle(player, MovementDomain::Authority)) return true;
    StopAuthorityPlayer(entry);
    return true;
}
void FlushMovementAuthorityForPlayer(std::uintptr_t player)
{
    if (!enabledAuthorityPlayers) return;
    const int slot = AuthorityPlayerSlot(player);
    if (slot < 0) return;
    std::unique_lock<std::shared_mutex> lock(authorityFrameMutex);
    auto& entry = authorityPlayers[slot];
    if (!entry.program || !entry.statePending || entry.player != player || entry.handle != NativeEntityHandle(player, MovementDomain::Authority)) return;
    entry.publishedCommand = entry.command;
    entry.publishedState = entry.state;
    entry.publishedTime = entry.time;
    entry.statePending = false;
    entry.hasPublishedState = true;
}
void CaptureMovementAuthorityForSnapshot(int playerSlot, std::uint32_t nativeFrameTick)
{
    if (!authoritySessionCount || playerSlot < 0 || playerSlot >= kPlayerSlots) return;
    std::unique_lock<std::shared_mutex> lock(authorityFrameMutex);
    auto& entry = authorityPlayers[playerSlot];
    if (!entry.everEnabled) return;
    // Validate the serial in the native entity registry before dereferencing an
    // old player pointer. A recycled connection slot never inherits an opt-in.
    const auto index = entry.handle & 0xFFFFu;
    const auto registry = nativeServer.module + 0xD425C8 + index * 0x30;
    if (index >= 0x4000 || Field<std::uint32_t>(registry, 8) != (entry.handle >> 16)
        || Field<std::uintptr_t>(registry, 0) != entry.player)
    {
        if (entry.program) --enabledAuthorityPlayers;
        --authoritySessionCount;
        entry = {};
        return;
    }
    auto& frame = (*entry.frames)[nativeFrameTick % kCommandStorage];
    if (frame.valid && frame.tick == nativeFrameTick) return;
    frame.valid = false;
    frame.tick = nativeFrameTick;
    const auto jump = ReadNativeJumpState(entry.player, MovementDomain::Authority);
    if (!std::isfinite(jump.power)) return;
    std::uint32_t powerBits{}, timeBits{};
    std::memcpy(&powerBits, &jump.power, sizeof(powerBits));
    std::memcpy(&timeBits, &entry.publishedTime, sizeof(timeBits));
    const int prefix = _snprintf_s(frame.command, sizeof(frame.command), _TRUNCATE, "%s %d %08x %08x %08x %08x %08x %08x %08x", kAuthorityCommand,
        entry.program ? 1 : 0, entry.generation, NetworkEntityHandle(entry.handle), entry.publishedCommand, nativeFrameTick,
        static_cast<std::uint32_t>(jump.remaining), powerBits, timeBits);
    if (prefix < 0) return;
    std::size_t length = prefix;
    if (entry.program)
    {
        const int written = _snprintf_s(frame.command + length, sizeof(frame.command) - length, _TRUNCATE,
            " %s %s ", entry.program->name, entry.program->fingerprint);
        if (written < 0) return;
        length += written;
        constexpr char digits[] = "0123456789abcdef";
        for (unsigned index = 0; index < entry.program->slots; ++index)
            for (unsigned nibble = 0; nibble != 8; ++nibble)
                frame.command[length++] = digits[(entry.publishedState[index] >> (28 - nibble * 4)) & 15];
        for (unsigned index = 0; index < kMovementNativeStateCount; ++index)
            for (unsigned nibble = 0; nibble != 8; ++nibble)
                frame.command[length++] = digits[(entry.publishedState[kMovementNativeStateFirst + index] >> (28 - nibble * 4)) & 15];
    }
    frame.command[length] = 0;
    frame.length = static_cast<std::uint16_t>(length);
    frame.valid = true;
}
std::size_t CopyMovementAuthorityCommand(int playerSlot, std::uint32_t nativeFrameTick, char* output, std::size_t capacity)
{
    if (playerSlot < 0 || playerSlot >= kPlayerSlots || !authoritySessionCount) return 0;
    if (!output || capacity < kMovementAuthorityBytes) return SIZE_MAX;
    std::shared_lock<std::shared_mutex> lock(authorityFrameMutex);
    const auto& entry = authorityPlayers[playerSlot];
    if (!entry.everEnabled) return 0;
    const auto& frame = (*entry.frames)[nativeFrameTick % kCommandStorage];
    if (!frame.valid || frame.tick != nativeFrameTick) return SIZE_MAX;
    std::memcpy(output, frame.command, frame.length + 1);
    return frame.length;
}
bool ConsumeMovementAuthorityCommand(const char* command)
{
    if (!command || std::strncmp(command, kAuthorityCommand, sizeof(kAuthorityCommand) - 1)
        || (command[sizeof(kAuthorityCommand) - 1] && command[sizeof(kAuthorityCommand) - 1] != ' ')) return false;
    AuthorityMessage parsed;
    const bool valid = ParseAuthorityMessage(command, parsed);
    std::lock_guard<std::mutex> lock(pendingAuthority.mutex);
    if (!valid || pendingAuthority.count == pendingAuthority.entries.size()) pendingAuthority.malformed = true;
    else pendingAuthority.entries[pendingAuthority.count++] = parsed;
    pendingAuthority.available.store(true, std::memory_order_release);
    return true;
}
void ResetMovementConnection()
{
    presentation::Reset();
    committedPresentation = {};
    clientMovement.inputCapable = false;
    QueueClientReset(clientMovement.program, clientMovement.baselineCommand);
    DispatchClientPresentation();
    committedMovementBaseline = {};
    clientMovement.program = nullptr;
    ClearClientWeaponRelease();
    clientMovement.generation = 0;
    clientMovement.handle = 0xFFFFFFFFu;
    clientMovement.baselineCommand = clientMovement.previousAck = 0;
    clientMovement.baseline = {};
    clientMovement.baselineJump = {};
    clientMovement.acceptedAuthority = {};
    for (auto& value : clientMovement.predictions) value.valid = false;
    for (auto& value : clientMovement.authorities) value.valid = false;
    for (auto& value : clientMovement.presented) value.valid = false;
    clientMovement.eventCount = 0;
    clientMovement.receivedAuthority = clientMovement.baselineValid = clientMovement.ackValid = false;
    clientMovement.inUpdate = clientMovement.needsRestore = clientMovement.restored = false;
    clientMovement.failed = clientMovement.disconnectQueued = false;
    clientMovement.player = 0;
    clientMovement.waitingSince = 0;
    clientMovement.failure[0] = 0;
    std::lock_guard<std::mutex> lock(pendingAuthority.mutex);
    pendingAuthority.count = 0;
    pendingAuthority.malformed = false;
    pendingAuthority.available.store(false, std::memory_order_release);
}
void ResetMovementLevel()
{
    ResetMovementConnection();
    std::unique_lock<std::shared_mutex> lock(authorityFrameMutex);
    for (auto& entry : authorityPlayers) entry = {};
    enabledAuthorityPlayers = 0;
    authoritySessionCount = 0;
    pendingWeaponReleaseCount = 0;
}
void InvalidateMovementDomain(MovementDomain side)
{
    if (side == MovementDomain::Client) ResetMovementConnection();
    else
    {
        presentation::Reset();
        std::unique_lock<std::shared_mutex> lock(authorityFrameMutex);
        for (auto& entry : authorityPlayers) StopAuthorityPlayer(entry);
        enabledAuthorityPlayers = 0;
    }
}
}
