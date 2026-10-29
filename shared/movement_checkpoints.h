#pragma once

#include "movement_runtime.h"

namespace r1delta::movement
{
enum class CheckpointStage : unsigned
{
    Friction,
    GroundPublish,
    JumpHeight,
    JumpComplete,
    StartGravity,
    FinishGravity,
    DuckRequest,
    Walk
};

// m_nDuckTransitionTimeMsecs has a native unsigned 10-bit SendProp.
inline constexpr int kMaximumDuckTransitionMilliseconds = 1023;

// Command-local controls, read by reference at each native checkpoint. The base
// acceleration/deceleration replaces the native selection, BEFORE damage-grace
// and player acceleration modifiers. Surface friction remains a separate factor.
struct CheckpointOverrides
{
    std::uint32_t checkpointMask{};
    float acceleration{}, deceleration{};
    float frictionScale{1.0f}, moveScale{1.0f};
    int duckMilliseconds{}; // 0 = stock; otherwise down duration, 1..1023 ms.
    bool accelerationSet{}, decelerationSet{};
    bool retainDuck{}, allowToggleJump{};
    bool sprintAllowed{true};
};

struct CheckpointPlayerContext
{
    MoveVector wallNormal, gravityUp, groundNormal;
    // Raw native moveScale: excludes the command's multiplier above.
    float surfaceFriction{}, moveScale{}, waterJumpTime{};
    int duckState{}, duckMillisecondsRemaining{}, stance{}, groundSurface{-1};
    bool grounded{}, crouchToggled{}, worldUp{}, groundNormalKnown{};
};

struct CheckpointContext : CheckpointPlayerContext
{
    MovementDomain domain{};
    CheckpointStage stage{};
    std::uintptr_t player{}, movement{}, moveData{};
    void* user{};
    CheckpointOverrides* overrides{};

    // Only these two vectors, selectedJumpHeight at JumpHeight, and
    // duckRequested at DuckRequest are committed when the callback returns true.
    MoveVector velocity, outputWishVelocity;
    float selectedJumpHeight{};
    bool duckRequested{};

    MoveVector viewForward, inputDirection, wishDirection;
    float wishSpeed{}, nativeDeceleration{}, gravityContribution{};
    int waterLevel{};
    bool wishKnown{}, nativeDecelerationKnown{};
    bool groundCandidate{}, jumpSucceeded{}, jumpHeightReached{};
};

// Friction: true replaces the ENTIRE routine (except native waterjump); false
// runs it unchanged apart from explicitly selected base scalars/frictionScale.
// Other stages: true commits the documented fields; native code ALWAYS resumes.
// Walk runs before the native walk routine, including its deceleration getter.
// GroundPublish precedes SetGroundEntity: grounded is the previous state,
// groundCandidate is the new validated candidate, and velocity is the incoming
// native post-clip velocity. A real trace plane is supplied only for a candidate.
// JumpHeight is reached only after native jump validation. JumpComplete reports
// the native bool, including paths which did not select a regular jump height.
// Gravity checkpoints precede native velocity loads. gravityContribution is
// entityGravity * sv_gravity * dt/2 * native wallrun modifier, positive for normal
// world-up gravity, NOT a signed Z delta. Native applies its own direction next.
using CheckpointCallback = bool (*)(CheckpointContext&);

// Call only while the relevant module's movement is quiescent. Install verifies
// the exact on-disk image and every entry/mid-function site, then enables the
// required set transactionally. MinHook must already be initialized. These hooks
// own the friction targets; do not install another friction detour alongside.
// Authority uses the same exact server.dll in listen, legacy and R1O hosts.
// On removal failure, executable storage is retained rather than freed live.
bool InstallMovementCheckpoints(MovementDomain domain, std::uintptr_t module) noexcept;
bool UninstallMovementCheckpoints(MovementDomain domain) noexcept;

// The caller supplies an already validated native player. No trace is invented:
// groundNormalKnown is true only after a valid publication in the active scope.
// Outside that scope the native gravity-up vector is NOT exposed as a ground
// plane. Persist an observed plane in the movement program's predicted slots.
bool ReadCheckpointPlayerContext(MovementDomain domain, std::uintptr_t player,
    CheckpointPlayerContext& context) noexcept;
// Canonical R1 physical surface name. Negative contact indices select native
// surface zero, matching the native default-contact convention.
const char* ReadMovementSurfaceName(MovementDomain domain, int index) noexcept;

// Bind only around a current MovementEvaluation, with overrides owned by that
// evaluation. No scope means original native behavior. This object and overrides
// must outlive synchronous native movement; callbacks may update overrides.
// m_duckState, m_nDuckTransitionTimeMsecs and m_duckToggleOn already have native
// prediction descriptors and SendProps. Client offsets: 2D6C/26B4/2718; server:
// 1A10/D9C/D90. Native code still writes/marks state and countdown; toggle-jump
// only hides and restores the exact latch during CheckJumpButton.
// Native does not store custom down duration: programs must retain that duration
// in predicted slots through a partial down transition and its reversal. This
// adapter has no persistent per-player or solo-game state.
class MovementCheckpointScope
{
public:
    MovementCheckpointScope(MovementDomain domain, std::uintptr_t player,
        std::uintptr_t movement, std::uintptr_t moveData, CheckpointOverrides& overrides,
        CheckpointCallback callback, void* user = nullptr) noexcept;
    ~MovementCheckpointScope();
    MovementCheckpointScope(const MovementCheckpointScope&) = delete;
    MovementCheckpointScope& operator=(const MovementCheckpointScope&) = delete;

    bool Ready() const noexcept { return active_; }
    bool Failed() const noexcept { return failed_; }

private:
    friend struct CheckpointDispatch;
    MovementCheckpointScope* previous_{};
    MovementDomain domain_{};
    std::uintptr_t player_{}, movement_{}, moveData_{}, trace_{};
    CheckpointOverrides* overrides_{};
    CheckpointCallback callback_{};
    void* user_{};
    MoveVector groundNormal_;
    int groundSurface_{-1};
    bool groundNormalKnown_{}, ducking_{}, jumping_{}, jumpHeightReached_{};
    bool active_{}, failed_{}, inCallback_{};
};
}
