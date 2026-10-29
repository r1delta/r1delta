#pragma once

#include "predicted_movement.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace r1delta::movement
{
enum class MovementDomain : unsigned { Client, Authority };
inline constexpr unsigned kMovementSlots = 64;
inline constexpr unsigned kMovementApiVersion = 3;
inline constexpr unsigned kMovementNativeStateFirst = kMovementSlots;
inline constexpr unsigned kMovementNativeStateCount = 1;
inline constexpr unsigned kMovementWeaponLockStateSlot = kMovementNativeStateFirst;
inline constexpr unsigned kMovementStateWords = kMovementSlots + kMovementNativeStateCount;
// Native rollback state trails, and never consumes, the public 64 script slots.
inline constexpr unsigned kMovementNativeStateFormat = 2;
inline constexpr char kMovementNativeStateFingerprintTag[] = "R1Delta movement native state format 2";
// Fixed header/name/fingerprint plus eight hexadecimal characters per state word.
inline constexpr std::size_t kMovementAuthorityBytes = 256 + kMovementStateWords * 8;
inline constexpr unsigned kMovementPrograms = 16;
inline constexpr unsigned kCommandWindow = 300;
inline constexpr unsigned kCommandStorage = 512;
inline constexpr unsigned kMovementEvents = 32;

struct MoveVector
{
    float x{}, y{}, z{};
    float& Component(unsigned axis) { return axis == 0 ? x : axis == 1 ? y : z; }
    float Component(unsigned axis) const { return axis == 0 ? x : axis == 1 ? y : z; }
};

using MovementState = std::array<std::uint32_t, kMovementStateWords>;
struct MovementProgram
{
    char name[33]{};
    char fingerprint[65]{};
    char presentation[65]{};
    char inputCallback[65]{};
    char registryKey[40]{};
    unsigned slots{};
    bool registered{};
    mutable bool presentationFailed{};
};

struct MovementValues
{
    MoveVector origin, velocity, angles;
    float forwardSpeed{}, sideSpeed{}, upSpeed{}, maxSpeed{}, clientMaxSpeed{};
    int remainingAirJumps{};
    float jumpPower{};
    std::uint32_t buttons{}, oldButtons{};
};
struct MovementRayResult
{
    MoveVector end, normal;
    float fraction{1.0f};
    int surface{-1};
    std::uint32_t entity{0xFFFFFFFFu};
    bool startSolid{}, allSolid{}, hitSky{};
};
struct CheckpointOverrides;
struct CheckpointContext;
struct MovementEvaluation
{
    MovementDomain domain{};
    const MovementProgram* program{};
    std::uintptr_t player{}, movement{}, moveData{};
    std::uint32_t command{}, tick{}, flags{}, playerHandle{};
    std::uint32_t customButtons{};
    int moveType{}, phase{};
    float time{}, frameTime{}, worldGravity{}, speedScale{1.0f}, frictionScale{1.0f};
    bool alive{}, grounded{}, titan{}, wallRunning{}, traversing{}, ziplining{};
    MoveVector hullMin, hullMax, eyeOffset;
    MovementValues values;
    MovementState state{};
    MovementRayResult trace;
    CheckpointOverrides* overrides{};
    CheckpointContext* checkpoint{};
    bool replaceFriction{};
    std::uint32_t eventMask{};
    std::array<MoveVector, kMovementEvents> events{};
    bool maximumEdited{}, clientMaximumEdited{}, capsApplied{}, failed{};
};

bool VerifyMovementModuleDigest(std::uintptr_t module, const char* expected);
bool MovementDomainReady(MovementDomain domain);
const MovementProgram* FindMovementProgram(MovementDomain domain, const char* name);
bool ExecuteMovementSource(MovementEvaluation& evaluation);
void DeliverMovementPresentation(const MovementProgram& program, std::uint32_t command,
    int event, const MoveVector& value);
void ClearMovementVmPrograms(MovementDomain domain);
bool DispatchMovementInput(std::uintptr_t player, std::uintptr_t command, std::uint32_t& buttons);

bool IsMovementEnabledForPlayer(std::uintptr_t player, const char* name);
bool ReadMovementAuthorityState(std::uintptr_t player, const char* name, int index,
    std::uint32_t& word, std::uint32_t& command, std::uint32_t& session, float& time);

struct MovementPresentationSnapshot
{
    const MovementProgram* program{};
    MovementState state{};
    std::uint32_t session{}, command{};
    std::uintptr_t player{};
    float time{};
    bool valid{};
};
// Published only after native prediction unwinds; never exposes replay scratch.
const MovementPresentationSnapshot* ReadMovementPresentation();
const MovementProgram* ActiveClientMovementProgram(std::uintptr_t player);
void FailMovementInput(const char* reason);

bool EnableMovementForPlayer(std::uintptr_t player, const MovementProgram& program);
bool DisableMovementForPlayer(std::uintptr_t player);
void InvalidateMovementDomain(MovementDomain domain);
bool TraceMovementGeometry(MovementEvaluation& evaluation, const MoveVector& start,
    const MoveVector& end, const MoveVector& mins, const MoveVector& maxs,
    std::uint32_t mask, int collisionGroup, MovementRayResult& result);
std::uintptr_t ResolveMovementEntity(const MovementEvaluation& evaluation, std::uint32_t handle);
bool ReadMovementEntityOrigin(const MovementEvaluation& evaluation, std::uint32_t handle, MoveVector& origin);
// Call only in MOVE_SIMULATE; hidden native state word 64 tracks weapon-lock ownership.
bool SetMovementWeaponLock(MovementEvaluation& evaluation, bool enabled);

// All helper calls have native bounds and finite-value checks. Persistent script
// state is exclusively these slots; no script object survives an evaluation.
// Register identical source/name/slot count in both VMs, then opt individual
// server players in. No registration changes stock movement or native schemas.
// SETUP prepares movement inputs. SIMULATE applies forces before the existing
// collision solver. FINISH observes/adjusts its result before publication.
// Native movement always runs; there is no skip-native return value.
// Every replay executes all phases from its preceding command snapshot. Only
// successful first-prediction events are presented, once per command/event ID.
// Presentation event -1 requests teardown on disable, reconnect, or VM teardown.
}
