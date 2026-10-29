#include "movement_runtime.h"
#include "movement_checkpoints.h"
#include "../engine/filesystem/r2_map_provider.h"
#include "../client/movement_presentation_hooks.h"
#include "core.h"
#include "load.h"
#include "logging.h"
#include <Windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string_view>
#include <vector>
#include <limits>
#pragma comment(lib, "bcrypt.lib")

namespace r1delta::movement
{
namespace
{
using RemoteCompile = SQRESULT(*)(HSQUIRRELVM, void*, BufState*, const char*, SQBool);
using PushScriptObject = std::intptr_t(__fastcall*)(R1SquirrelVM*, HSQUIRRELVM, const SQObject*);
struct ScriptPrimitives
{
    sq_get_t get{};
    sq_newslot_t newSlot{};
    base_getroottable_t root{};
    sq_call_t call{};
    RemoteCompile remoteCompile{};
    std::size_t entityInstanceOffset{};
    PushScriptObject pushObject{};
};
struct MovementVm
{
    R1SquirrelVM* owner{};
    DWORD thread{};
    ScriptPrimitives api;
    std::array<MovementProgram, kMovementPrograms> programs{};
    bool prepared{};
    char lastError[256]{};
};
MovementVm movementVms[2];
thread_local MovementEvaluation* evaluating{};
struct MovementInputEvaluation
{
    std::uint32_t command{}, buttons{};
    bool failed{};
};
thread_local MovementInputEvaluation* inputEvaluation{};

MovementVm& DomainVm(MovementDomain domain) { return movementVms[static_cast<unsigned>(domain)]; }
MovementVm* OwningMovementVm(HSQUIRRELVM vm)
{
    if (!vm) return nullptr;
    // The three admitted runtimes independently share this SQVM shared-state
    // field. Coroutines may register, but evaluations always use the owner VM.
    for (auto& binding : movementVms)
        if (binding.owner && binding.owner->sqvm
            && vm->_sharedstate == binding.owner->sqvm->_sharedstate)
            return &binding;
    return nullptr;
}
MovementDomain VmDomain(const MovementVm& vm)
{
    return &vm == &movementVms[0] ? MovementDomain::Client : MovementDomain::Authority;
}
SQInteger MovementScriptError(HSQUIRRELVM vm, const char* reason)
{
    if (evaluating) evaluating->failed = true;
    if (inputEvaluation) inputEvaluation->failed = true;
    if (auto* binding = OwningMovementVm(vm))
        std::snprintf(binding->lastError, sizeof(binding->lastError), "%s", reason);
    return sq_throwerror(vm, reason);
}
bool ReadIntegerArgument(HSQUIRRELVM vm, int at, SQInteger& value)
{
    SQObject object{};
    auto* owner = OwningMovementVm(vm);
    if (!owner || SQ_FAILED(sq_getstackobj(owner->owner, vm, at, &object))
        || object._type != OT_INTEGER) return false;
    value = object._unVal.nInteger;
    return true;
}
bool ReadNumberArgument(HSQUIRRELVM vm, int at, float& value)
{
    SQObject object{};
    auto* owner = OwningMovementVm(vm);
    if (!owner || SQ_FAILED(sq_getstackobj(owner->owner, vm, at, &object))) return false;
    if (object._type == OT_FLOAT) value = object._unVal.fFloat;
    else if (object._type == OT_INTEGER) value = static_cast<float>(object._unVal.nInteger);
    else return false;
    return std::isfinite(value);
}
MovementEvaluation* CurrentMovementEvaluation(HSQUIRRELVM vm)
{
    if (!evaluating) return nullptr;
    auto& binding = DomainVm(evaluating->domain);
    return binding.owner && binding.owner->sqvm == vm
        && binding.thread == GetCurrentThreadId() ? evaluating : nullptr;
}
SQInteger PushMovementInteger(HSQUIRRELVM vm, std::int32_t value)
{
    sq_pushinteger(OwningMovementVm(vm)->owner, vm, value);
    return 1;
}
SQInteger PushMovementNumber(HSQUIRRELVM vm, float value)
{
    sq_pushfloat(OwningMovementVm(vm)->owner, vm, value);
    return 1;
}
SQInteger PushMovementBoolean(HSQUIRRELVM vm, bool value)
{
    sq_pushbool(OwningMovementVm(vm)->owner, vm, value ? SQTrue : SQFalse);
    return 1;
}
#define MOVE_CONTEXT() auto* sample = CurrentMovementEvaluation(vm); if (!sample) return MovementScriptError(vm, "Movement access is only valid in a synchronous movement evaluation")
#define MOVE_INT_GETTER(NAME, VALUE) SQInteger NAME(HSQUIRRELVM vm) { MOVE_CONTEXT(); return PushMovementInteger(vm, static_cast<std::int32_t>(VALUE)); }
#define MOVE_FLOAT_GETTER(NAME, VALUE) SQInteger NAME(HSQUIRRELVM vm) { MOVE_CONTEXT(); return PushMovementNumber(vm, (VALUE)); }
#define MOVE_BOOL_GETTER(NAME, VALUE) SQInteger NAME(HSQUIRRELVM vm) { MOVE_CONTEXT(); return PushMovementBoolean(vm, (VALUE)); }
MOVE_INT_GETTER(ReadMovementPhase, sample->phase)
MOVE_INT_GETTER(ReadMovementCommand, sample->command)
MOVE_INT_GETTER(ReadMovementTick, sample->tick)
MOVE_INT_GETTER(ReadMovementPlayerHandle, sample->playerHandle)
MOVE_INT_GETTER(ReadMovementButtons, sample->values.buttons)
MOVE_INT_GETTER(ReadMovementOldButtons, sample->values.oldButtons)
MOVE_INT_GETTER(ReadMovementFlags, sample->flags)
MOVE_INT_GETTER(ReadMovementType, sample->moveType)
MOVE_INT_GETTER(ReadMovementCustomButtons, sample->customButtons)
MOVE_INT_GETTER(ReadMovementJumpCount, sample->values.remainingAirJumps)
MOVE_INT_GETTER(ReadMovementTraceEntity, sample->trace.entity)
MOVE_FLOAT_GETTER(ReadMovementTime, sample->time)
MOVE_FLOAT_GETTER(ReadMovementFrameTime, sample->frameTime)
MOVE_FLOAT_GETTER(ReadMovementGravity, sample->worldGravity)
MOVE_FLOAT_GETTER(ReadMovementForward, sample->values.forwardSpeed)
MOVE_FLOAT_GETTER(ReadMovementSide, sample->values.sideSpeed)
MOVE_FLOAT_GETTER(ReadMovementUp, sample->values.upSpeed)
MOVE_FLOAT_GETTER(ReadMovementMaximum, sample->values.maxSpeed)
MOVE_FLOAT_GETTER(ReadMovementClientMaximum, sample->values.clientMaxSpeed)
MOVE_FLOAT_GETTER(ReadMovementJumpPower, sample->values.jumpPower)
MOVE_FLOAT_GETTER(ReadMovementTraceFraction, sample->trace.fraction)
MOVE_BOOL_GETTER(ReadMovementGrounded, sample->grounded)
MOVE_FLOAT_GETTER(ReadMovementSurfaceFriction, sample->checkpoint->surfaceFriction)
MOVE_FLOAT_GETTER(ReadMovementMoveScale, sample->checkpoint->moveScale)
MOVE_BOOL_GETTER(ReadMovementWorldUp, sample->checkpoint->worldUp)
MOVE_BOOL_GETTER(ReadMovementWaterJump, sample->checkpoint->waterJumpTime != 0.0f)
MOVE_BOOL_GETTER(ReadMovementGroundNormalKnown, sample->checkpoint->groundNormalKnown && (sample->phase != 4 || sample->checkpoint->groundCandidate))
MOVE_BOOL_GETTER(ReadMovementDidJump, sample->phase == 6 && sample->checkpoint->jumpSucceeded)
MOVE_BOOL_GETTER(ReadMovementDuckRequested, sample->phase == 9 ? sample->checkpoint->duckRequested : ((sample->values.buttons & 4u) != 0 || sample->checkpoint->crouchToggled))
MOVE_INT_GETTER(ReadMovementDuckState, sample->checkpoint->duckState)
MOVE_INT_GETTER(ReadMovementStance, sample->checkpoint->stance)
MOVE_INT_GETTER(ReadMovementWaterLevel, sample->checkpoint->waterLevel)
MOVE_INT_GETTER(ReadMovementTraceSurface, sample->trace.surface)
MOVE_INT_GETTER(ReadMovementGroundSurface, sample->checkpoint->groundSurface)
MOVE_BOOL_GETTER(ReadMovementAlive, sample->alive)
MOVE_BOOL_GETTER(ReadMovementTitan, sample->titan)
MOVE_BOOL_GETTER(ReadMovementWallRun, sample->wallRunning)
MOVE_BOOL_GETTER(ReadMovementTraversal, sample->traversing)
MOVE_BOOL_GETTER(ReadMovementZipline, sample->ziplining)
MOVE_BOOL_GETTER(ReadMovementTraceStart, sample->trace.startSolid)
MOVE_BOOL_GETTER(ReadMovementTraceSolid, sample->trace.allSolid)
MOVE_BOOL_GETTER(ReadMovementTraceSky, sample->trace.hitSky)
#undef MOVE_INT_GETTER
#undef MOVE_FLOAT_GETTER
#undef MOVE_BOOL_GETTER
SQInteger ReadMovementPlayer(HSQUIRRELVM vm)
{
    MOVE_CONTEXT();
    auto& binding = DomainVm(sample->domain);
    // Borrow the native GetScriptInstance cache; never run its allocating lazy
    // creation branch during prediction. HSCRIPT is an SQObject reference, not
    // the network identity exposed by Move_GetPlayerHandle.
    const auto* instance = sample->player ? *reinterpret_cast<const SQObject* const*>(
        sample->player + binding.api.entityInstanceOffset) : nullptr;
    if (!instance || instance->_type != OT_INSTANCE)
        return MovementScriptError(vm, "The evaluated player has no native script entity");
    binding.api.pushObject(binding.owner, vm, instance);
    return 1;
}

#define MOVE_VECTOR_GETTER(NAME, VALUE) SQInteger NAME(HSQUIRRELVM vm) { MOVE_CONTEXT(); SQInteger axis{}; if (!ReadIntegerArgument(vm, 2, axis) || axis < 0 || axis > 2) return MovementScriptError(vm, "Movement axis must be MOVE_X, MOVE_Y, or MOVE_Z"); return PushMovementNumber(vm, (VALUE).Component(static_cast<unsigned>(axis))); }
MOVE_VECTOR_GETTER(ReadMovementOrigin, sample->values.origin)
MOVE_VECTOR_GETTER(ReadMovementVelocity, sample->values.velocity)
MOVE_VECTOR_GETTER(ReadMovementAngles, sample->values.angles)
MOVE_VECTOR_GETTER(ReadMovementHullMinimum, sample->hullMin)
MOVE_VECTOR_GETTER(ReadMovementHullMaximum, sample->hullMax)
MOVE_VECTOR_GETTER(ReadMovementEyeOffset, sample->eyeOffset)
MOVE_VECTOR_GETTER(ReadMovementTraceEnd, sample->trace.end)
MOVE_VECTOR_GETTER(ReadMovementTraceNormal, sample->trace.normal)
MOVE_VECTOR_GETTER(ReadMovementWallNormal, sample->checkpoint->wallNormal)
MOVE_VECTOR_GETTER(ReadMovementViewForward, sample->checkpoint->viewForward)
MOVE_VECTOR_GETTER(ReadMovementInputDirection, sample->checkpoint->inputDirection)
#undef MOVE_VECTOR_GETTER
SQInteger ReadMovementGroundNormal(HSQUIRRELVM vm)
{
    MOVE_CONTEXT(); SQInteger axis{};
    if (!sample->checkpoint->groundNormalKnown
        || (sample->phase == 4 && !sample->checkpoint->groundCandidate)
        || !ReadIntegerArgument(vm, 2, axis) || axis < 0 || axis > 2)
        return MovementScriptError(vm, "Ground normal requires a published native ground plane and axis 0..2");
    return PushMovementNumber(vm, sample->checkpoint->groundNormal.Component(static_cast<unsigned>(axis)));
}

SQInteger QueryMovementEntity(HSQUIRRELVM vm)
{
    MOVE_CONTEXT(); SQInteger handle{};
    if (!ReadIntegerArgument(vm, 2, handle)) return MovementScriptError(vm, "Entity handle must be an integer");
    return PushMovementBoolean(vm, ResolveMovementEntity(*sample, static_cast<std::uint32_t>(handle)) != 0);
}
SQInteger QueryMovementEntityOrigin(HSQUIRRELVM vm)
{
    MOVE_CONTEXT(); SQInteger handle{}, axis{}; MoveVector origin;
    if (!ReadIntegerArgument(vm, 2, handle) || !ReadIntegerArgument(vm, 3, axis) || axis < 0 || axis > 2
        || !ReadMovementEntityOrigin(*sample, static_cast<std::uint32_t>(handle), origin))
        return MovementScriptError(vm, "Entity origin requires a currently valid handle and MOVE_X, MOVE_Y, or MOVE_Z");
    return PushMovementNumber(vm, origin.Component(static_cast<unsigned>(axis)));
}
SQInteger ChangeMovementButtons(HSQUIRRELVM vm)
{
    MOVE_CONTEXT(); SQInteger value{};
    if (sample->phase != 0 || !ReadIntegerArgument(vm, 2, value))
        return MovementScriptError(vm, "Buttons require an integer during MOVE_SETUP");
    sample->values.buttons = static_cast<std::uint32_t>(value);
    return 0;
}
SQInteger ChangeMovementOldButtons(HSQUIRRELVM vm)
{
    MOVE_CONTEXT(); SQInteger value{};
    if (sample->phase != 0 || !ReadIntegerArgument(vm, 2, value))
        return MovementScriptError(vm, "Old buttons require an integer during MOVE_SETUP");
    sample->values.oldButtons = static_cast<std::uint32_t>(value);
    return 0;
}
#define MOVE_NUMBER_SETTER(NAME, FIELD, LOW, HIGH, SETUP) SQInteger NAME(HSQUIRRELVM vm) { MOVE_CONTEXT(); float value{}; if (((SETUP) && sample->phase != 0) || !ReadNumberArgument(vm, 2, value) || value < (LOW) || value > (HIGH)) return MovementScriptError(vm, "Movement value is out of range or not writable in this phase"); sample->FIELD = value; return 0; }
MOVE_NUMBER_SETTER(ChangeMovementForward, values.forwardSpeed, -100000.0f, 100000.0f, true)
MOVE_NUMBER_SETTER(ChangeMovementSide, values.sideSpeed, -100000.0f, 100000.0f, true)
MOVE_NUMBER_SETTER(ChangeMovementUp, values.upSpeed, -100000.0f, 100000.0f, true)
SQInteger ChangeMovementMaximum(HSQUIRRELVM vm)
{
    MOVE_CONTEXT(); float value{};
    if (sample->phase != 0 || !ReadNumberArgument(vm, 2, value) || value < 0.0f || value > 100000.0f)
        return MovementScriptError(vm, "Maximum speed requires MOVE_SETUP and a finite value in 0..100000");
    sample->values.maxSpeed = value;
    sample->maximumEdited = true;
    return 0;
}
SQInteger ChangeMovementClientMaximum(HSQUIRRELVM vm)
{
    MOVE_CONTEXT(); float value{};
    if (sample->phase != 0 || !ReadNumberArgument(vm, 2, value) || value < 0.0f || value > 100000.0f)
        return MovementScriptError(vm, "Client maximum speed requires MOVE_SETUP and a finite value in 0..100000");
    sample->values.clientMaxSpeed = value;
    sample->clientMaximumEdited = true;
    return 0;
}
MOVE_NUMBER_SETTER(ChangeMovementSpeedScale, speedScale, 0.0f, 16.0f, true)
MOVE_NUMBER_SETTER(ChangeMovementFrictionScale, frictionScale, 0.0f, 1.0f, true)
MOVE_NUMBER_SETTER(ChangeMovementJumpPower, values.jumpPower, 0.0f, 100.0f, false)
#undef MOVE_NUMBER_SETTER
SQInteger ChangeMovementJumpCount(HSQUIRRELVM vm)
{
    MOVE_CONTEXT(); SQInteger value{};
    if (!ReadIntegerArgument(vm, 2, value) || value < 0 || value > 16)
        return MovementScriptError(vm, "Remaining air jumps must be an integer from 0 through 16");
    sample->values.remainingAirJumps = value;
    return 0;
}
bool ReadMovementVector(HSQUIRRELVM vm, int at, MoveVector& value, float maximum)
{
    return ReadNumberArgument(vm, at, value.x) && ReadNumberArgument(vm, at + 1, value.y)
        && ReadNumberArgument(vm, at + 2, value.z)
        && std::fabs(value.x) <= maximum && std::fabs(value.y) <= maximum && std::fabs(value.z) <= maximum;
}
SQInteger ChangeMovementVelocity(HSQUIRRELVM vm)
{
    MOVE_CONTEXT(); MoveVector value;
    if (!ReadMovementVector(vm, 2, value, 100000.0f))
        return MovementScriptError(vm, "Velocity requires three finite components within +/-100000");
    sample->values.velocity = value;
    return 0;
}
SQInteger ChangeMovementOrigin(HSQUIRRELVM vm)
{
    MOVE_CONTEXT(); MoveVector value; MovementRayResult contact;
    if (!ReadMovementVector(vm, 2, value, 1048576.0f))
        return MovementScriptError(vm, "Origin requires three finite world coordinates");
    // MASK_PLAYERSOLID: the existing movement collision class remains native.
    if (!TraceMovementGeometry(*sample, sample->values.origin, value, sample->hullMin,
            sample->hullMax, 0x0201400Bu, 8, contact))
        return MovementScriptError(vm, "Movement origin sweep failed");
    sample->trace = contact;
    if (!contact.startSolid && !contact.allSolid) sample->values.origin = contact.end;
    return PushMovementBoolean(vm, !contact.startSolid && !contact.allSolid && contact.fraction == 1.0f);
}
SQInteger QueryMovementTrace(HSQUIRRELVM vm)
{
    MOVE_CONTEXT(); MoveVector start, end, mins, maxs; SQInteger mask{}, group{};
    const int count = sq_gettop(OwningMovementVm(vm)->owner, vm);
    if (count < 14 || count > 15 || (count == 15 && (!ReadIntegerArgument(vm, 15, group) || group < 0 || group > 32)))
        return MovementScriptError(vm, "TraceHull accepts thirteen values and an optional collision group in 0..32");
    if (!ReadMovementVector(vm, 2, start, 1048576.0f)
        || !ReadMovementVector(vm, 5, end, 1048576.0f)
        || !ReadMovementVector(vm, 8, mins, 4096.0f)
        || !ReadMovementVector(vm, 11, maxs, 4096.0f)
        || !ReadIntegerArgument(vm, 14, mask)
        || mins.x > maxs.x || mins.y > maxs.y || mins.z > maxs.z
        || mins.x != mins.y || maxs.x != maxs.y)
        return MovementScriptError(vm, "TraceHull requires finite endpoints, ordered square XY hull bounds, and an integer mask");
    if (!TraceMovementGeometry(*sample, start, end, mins, maxs,
            static_cast<std::uint32_t>(mask), static_cast<int>(group), sample->trace))
        return MovementScriptError(vm, "Movement trace failed");
    return 0;
}
SQInteger ReadMovementCheckpointValue(HSQUIRRELVM vm, unsigned kind)
{
    MOVE_CONTEXT();
    const auto& context = *sample->checkpoint;
    if (kind == 0 && (sample->phase != 3 || !context.wishKnown))
        return MovementScriptError(vm, "Wish speed requires MOVE_FRICTION");
    if (kind == 1 && (sample->phase != 3 || !context.nativeDecelerationKnown))
        return MovementScriptError(vm, "Native deceleration requires MOVE_FRICTION");
    if (kind == 2 && sample->phase != 5)
        return MovementScriptError(vm, "Selected jump height requires MOVE_JUMP");
    if (kind == 3 && sample->phase != 7 && sample->phase != 8)
        return MovementScriptError(vm, "Gravity contribution requires a native gravity checkpoint");
    return PushMovementNumber(vm, kind == 0 ? context.wishSpeed : kind == 1 ? context.nativeDeceleration
        : kind == 2 ? context.selectedJumpHeight : context.gravityContribution);
}
SQInteger ReadMovementWishSpeed(HSQUIRRELVM vm) { return ReadMovementCheckpointValue(vm, 0); }
SQInteger ReadMovementNativeDeceleration(HSQUIRRELVM vm) { return ReadMovementCheckpointValue(vm, 1); }
SQInteger ReadMovementJumpHeight(HSQUIRRELVM vm) { return ReadMovementCheckpointValue(vm, 2); }
SQInteger ReadMovementGravityContribution(HSQUIRRELVM vm) { return ReadMovementCheckpointValue(vm, 3); }
SQInteger ReadMovementFrictionVector(HSQUIRRELVM vm, bool output)
{
    MOVE_CONTEXT(); SQInteger axis{};
    if (sample->phase != 3 || !sample->checkpoint->wishKnown
        || !ReadIntegerArgument(vm, 2, axis) || axis < 0 || axis > 2)
        return MovementScriptError(vm, "Friction vectors require MOVE_FRICTION and axis 0..2");
    return PushMovementNumber(vm, (output ? sample->checkpoint->outputWishVelocity
        : sample->checkpoint->wishDirection).Component(static_cast<unsigned>(axis)));
}
SQInteger ReadMovementWishDirection(HSQUIRRELVM vm) { return ReadMovementFrictionVector(vm, false); }
SQInteger ReadMovementOutputWish(HSQUIRRELVM vm) { return ReadMovementFrictionVector(vm, true); }
SQInteger ChangeMovementOutputWish(HSQUIRRELVM vm)
{
    MOVE_CONTEXT(); MoveVector value;
    if (sample->phase != 3 || !ReadMovementVector(vm, 2, value, 100000.0f))
        return MovementScriptError(vm, "Output wish velocity requires MOVE_FRICTION and three finite components within +/-100000");
    sample->checkpoint->outputWishVelocity = value;
    return 0;
}
SQInteger ChangeMovementScalar(HSQUIRRELVM vm, unsigned kind)
{
    MOVE_CONTEXT(); float value{};
    if (!ReadNumberArgument(vm, 2, value) || value < 0.0f || value > (kind == 2 ? 16.0f : 100000.0f))
        return MovementScriptError(vm, "Native movement scalar is outside its finite range");
    if (kind == 0) { sample->overrides->acceleration = value; sample->overrides->accelerationSet = true; }
    else if (kind == 1) { sample->overrides->deceleration = value; sample->overrides->decelerationSet = true; }
    else if (kind == 2) sample->overrides->moveScale = value;
    else
    {
        if (sample->phase != 5) return MovementScriptError(vm, "Jump height is writable only during MOVE_JUMP");
        sample->checkpoint->selectedJumpHeight = value;
    }
    return 0;
}
SQInteger ChangeMovementAcceleration(HSQUIRRELVM vm) { return ChangeMovementScalar(vm, 0); }
SQInteger ChangeMovementDeceleration(HSQUIRRELVM vm) { return ChangeMovementScalar(vm, 1); }
SQInteger ChangeMovementMoveScale(HSQUIRRELVM vm) { return ChangeMovementScalar(vm, 2); }
SQInteger ChangeMovementJumpHeight(HSQUIRRELVM vm) { return ChangeMovementScalar(vm, 3); }
SQInteger ClearMovementAcceleration(HSQUIRRELVM vm) { MOVE_CONTEXT(); sample->overrides->accelerationSet = false; return 0; }
SQInteger ClearMovementDeceleration(HSQUIRRELVM vm) { MOVE_CONTEXT(); sample->overrides->decelerationSet = false; return 0; }
SQInteger ChangeMovementDuckDuration(HSQUIRRELVM vm)
{
    MOVE_CONTEXT(); SQInteger duration{};
    if (!ReadIntegerArgument(vm, 2, duration) || duration < 1 || duration > kMaximumDuckTransitionMilliseconds)
        return MovementScriptError(vm, "Duck duration must be an integer in 1..1023 milliseconds");
    sample->overrides->duckMilliseconds = duration;
    return 0;
}
SQInteger ChangeMovementCheckpointBoolean(HSQUIRRELVM vm, unsigned kind)
{
    MOVE_CONTEXT(); SQObject object{};
    auto* owner = OwningMovementVm(vm);
    if (SQ_FAILED(sq_getstackobj(owner->owner, vm, 2, &object)) || object._type != OT_BOOL)
        return MovementScriptError(vm, "Native movement switch requires a bool");
    const bool value = object._unVal.nInteger != 0;
    if (kind == 0)
    {
        if (sample->phase != 3) return MovementScriptError(vm, "Friction replacement requires MOVE_FRICTION");
        sample->replaceFriction = value;
    }
    else if (kind == 1) sample->overrides->retainDuck = value;
    else if (kind == 2) sample->overrides->allowToggleJump = value;
    else sample->overrides->sprintAllowed = value;
    return 0;
}
SQInteger ChangeMovementFrictionReplacement(HSQUIRRELVM vm) { return ChangeMovementCheckpointBoolean(vm, 0); }
SQInteger ChangeMovementDuckRetention(HSQUIRRELVM vm) { return ChangeMovementCheckpointBoolean(vm, 1); }
SQInteger ChangeMovementCrouchJump(HSQUIRRELVM vm) { return ChangeMovementCheckpointBoolean(vm, 2); }
SQInteger ChangeMovementSprintAllowed(HSQUIRRELVM vm) { return ChangeMovementCheckpointBoolean(vm, 3); }
SQInteger ChangeMovementCheckpointMask(HSQUIRRELVM vm)
{
    MOVE_CONTEXT(); SQInteger mask{};
    if (sample->phase != 0 || !ReadIntegerArgument(vm, 2, mask) || mask < 0 || mask > 255)
        return MovementScriptError(vm, "Checkpoint mask requires MOVE_SETUP and integer bits 0..255");
    sample->overrides->checkpointMask = static_cast<std::uint32_t>(mask);
    return 0;
}
SQInteger ChangeMovementWeaponLock(HSQUIRRELVM vm)
{
    MOVE_CONTEXT();
    SQObject object{};
    auto* owner = OwningMovementVm(vm);
    if (sample->phase != 1 || !owner
        || SQ_FAILED(sq_getstackobj(owner->owner, vm, 2, &object)) || object._type != OT_BOOL)
        return MovementScriptError(vm, "Move_SetWeaponLock requires MOVE_SIMULATE and a bool");
    return PushMovementBoolean(vm, SetMovementWeaponLock(*sample, object._unVal.nInteger != 0));
}
bool ScriptMovementStateSlot(const MovementEvaluation& sample, SQInteger slot)
{
    return slot >= 0 && static_cast<unsigned>(slot) < sample.program->slots;
}
SQInteger QueryMovementStateFloat(HSQUIRRELVM vm)
{
    MOVE_CONTEXT(); SQInteger slot{};
    if (!ReadIntegerArgument(vm, 2, slot) || !ScriptMovementStateSlot(*sample, slot))
        return MovementScriptError(vm, "Movement state slot is outside the registered script range");
    float value; std::memcpy(&value, &sample->state[slot], sizeof(value));
    if (!std::isfinite(value)) return MovementScriptError(vm, "This state slot does not contain a finite float");
    return PushMovementNumber(vm, value);
}
SQInteger QueryMovementStateInteger(HSQUIRRELVM vm)
{
    MOVE_CONTEXT(); SQInteger slot{};
    if (!ReadIntegerArgument(vm, 2, slot) || !ScriptMovementStateSlot(*sample, slot))
        return MovementScriptError(vm, "Movement state slot is outside the registered script range");
    return PushMovementInteger(vm, static_cast<std::int32_t>(sample->state[slot]));
}
SQInteger ChangeMovementStateFloat(HSQUIRRELVM vm)
{
    MOVE_CONTEXT(); SQInteger slot{}; float value{};
    if (!ReadIntegerArgument(vm, 2, slot) || !ScriptMovementStateSlot(*sample, slot)
        || !ReadNumberArgument(vm, 3, value))
        return MovementScriptError(vm, "Float state requires a registered slot and finite number");
    std::memcpy(&sample->state[slot], &value, sizeof(value));
    return 0;
}
SQInteger ChangeMovementStateInteger(HSQUIRRELVM vm)
{
    MOVE_CONTEXT(); SQInteger slot{}, value{};
    if (!ReadIntegerArgument(vm, 2, slot) || !ScriptMovementStateSlot(*sample, slot)
        || !ReadIntegerArgument(vm, 3, value))
        return MovementScriptError(vm, "Integer state requires a registered slot and integer");
    sample->state[slot] = static_cast<std::uint32_t>(value);
    return 0;
}
SQInteger EvaluateMovementMath(HSQUIRRELVM vm)
{
    MOVE_CONTEXT(); SQInteger operation{}; float a{}, b{}, value{};
    if (!ReadIntegerArgument(vm, 2, operation) || !ReadNumberArgument(vm, 3, a) || !ReadNumberArgument(vm, 4, b))
        return MovementScriptError(vm, "Movement math requires an operation and two finite numbers");
    switch (operation)
    {
    case 0: value = std::sqrt(a); break;
    case 1: value = std::sin(a); break;
    case 2: value = std::cos(a); break;
    case 3: value = std::atan2(a, b); break;
    case 4: value = std::pow(a, b); break;
    case 5: value = std::fabs(a); break;
    case 6: value = (std::min)(a, b); break;
    case 7: value = (std::max)(a, b); break;
    default: return MovementScriptError(vm, "Unknown movement math operation");
    }
    if (!std::isfinite(value)) return MovementScriptError(vm, "Movement math produced a non-finite result");
    return PushMovementNumber(vm, value);
}
SQInteger QueueMovementEvent(HSQUIRRELVM vm)
{
    MOVE_CONTEXT(); SQInteger event{}; MoveVector value;
    if (!ReadIntegerArgument(vm, 2, event) || event < 0 || event >= kMovementEvents
        || !ReadMovementVector(vm, 3, value, 1048576.0f))
        return MovementScriptError(vm, "Movement events require ID 0..31 and three finite payload values");
    sample->eventMask |= 1u << event;
    sample->events[event] = value;
    return 0;
}
#undef MOVE_CONTEXT

bool ValidMovementName(const char* name)
{
    const auto length = name ? std::strlen(name) : 0;
    if (!length || length > 32) return false;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(name); *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')
            || (*p >= '0' && *p <= '9') || *p == '_' || *p == '.' || *p == '-')) return false;
    return true;
}
bool SynchronousMovementSource(const char* source)
{
    // Native movement cannot suspend. Ordinary globals/helpers are available,
    // but their side effects and mutable state are not restored during replay.
    constexpr std::string_view prohibited[] = {"yield", "thread", "wait", "waitthread", "waittill"};
    for (const char* p = source; *p;)
    {
        if (p[0] == '/' && p[1] == '/') { while (*p && *p != '\n') ++p; continue; }
        if (p[0] == '/' && p[1] == '*')
        {
            p += 2; while (*p && !(p[0] == '*' && p[1] == '/')) ++p;
            if (*p) p += 2;
            continue;
        }
        if (*p == '\"' || *p == '\'')
        {
            const char quote = *p++;
            while (*p && *p != quote) { if (*p == '\\' && p[1]) ++p; ++p; }
            if (*p) ++p;
            continue;
        }
        const auto identifier = [](char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_'; };
        if (identifier(*p))
        {
            const char* begin = p++; while (identifier(*p)) ++p;
            const std::string_view token(begin, p - begin);
            for (auto denied : prohibited) if (token == denied) return false;
        }
        else ++p;
    }
    return true;
}
bool FingerprintMovementSource(const char* name, const char* source, unsigned slots, char (&text)[65])
{
    BCRYPT_ALG_HANDLE algorithm{}; BCRYPT_HASH_HANDLE hash{};
    unsigned char digest[32]{};
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (ok) ok = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    constexpr char version[] = "R1Delta movement API 3";
    const auto append = [&](const void* bytes, std::size_t length)
    {
        return BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<void*>(bytes)), static_cast<ULONG>(length), 0) >= 0;
    };
    if (ok) ok = append(version, sizeof(version))
        && append(kMovementNativeStateFingerprintTag, sizeof(kMovementNativeStateFingerprintTag))
        && append(name, std::strlen(name) + 1) && append(&slots, sizeof(slots)) && append(source, std::strlen(source));
    if (ok) ok = BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok) return false;
    constexpr char digits[] = "0123456789abcdef";
    for (unsigned i = 0; i < sizeof(digest); ++i) { text[i * 2] = digits[digest[i] >> 4]; text[i * 2 + 1] = digits[digest[i] & 15]; }
    text[64] = 0;
    return true;
}
SQRESULT CompileMovementChunk(MovementVm& binding, HSQUIRRELVM vm, const char* source, const char* name)
{
    const auto size = static_cast<SQInteger>(std::strlen(source));
    if (!binding.api.remoteCompile) return sq_compilebuffer(vm, source, size, name, SQFalse);
    BufState buffer{source, 0, size};
    return binding.api.remoteCompile(vm, nullptr, &buffer, name, SQFalse);
}
bool PutMovementChunk(MovementVm& binding, HSQUIRRELVM vm, const char* key, const char* source)
{
    const auto top = sq_gettop(binding.owner, vm);
    binding.api.root(vm);
    sq_pushstring(vm, key, -1);
    bool ok = SQ_SUCCEEDED(CompileMovementChunk(binding, vm, source, key));
    if (ok) ok = SQ_SUCCEEDED(binding.api.newSlot(vm, -3, SQFalse));
    sq_settop(vm, top);
    return ok;
}
void LogMovementVmFailure(MovementVm& binding, const char* operation, const char* name, HSQUIRRELVM vm = nullptr);
SQInteger RegisterMovementProgram(HSQUIRRELVM vm)
{
    auto* binding = OwningMovementVm(vm);
    if (!binding || !binding->prepared || evaluating || binding->thread != GetCurrentThreadId())
        return MovementScriptError(vm, "Movement registration requires the live owning VM thread outside prediction");
    const char *name{}, *source{}, *presentation{}; SQInteger slots{};
    if (SQ_FAILED(sq_getstring(vm, 2, &name)) || !ValidMovementName(name)
        || static_cast<std::size_t>(sq_getsize(binding->owner, vm, 2)) != std::strlen(name)
        || SQ_FAILED(sq_getstring(vm, 3, &source)) || !source || !*source || std::strlen(source) > 65536
        || static_cast<std::size_t>(sq_getsize(binding->owner, vm, 3)) != std::strlen(source)
        || !ReadIntegerArgument(vm, 4, slots) || slots < 0 || slots > kMovementSlots
        || SQ_FAILED(sq_getstring(vm, 5, &presentation)) || !presentation || std::strlen(presentation) > 64
        || static_cast<std::size_t>(sq_getsize(binding->owner, vm, 5)) != std::strlen(presentation))
        return MovementScriptError(vm, "Movement_Register(name[1..32], source[1..65536], slots[0..64], presentationName[0..64])");
    if (!SynchronousMovementSource(source))
        return MovementScriptError(vm, "Movement source must be synchronous; yielding, threads and waits are not supported");
    MovementProgram candidate{};
    if (!FingerprintMovementSource(name, source, static_cast<unsigned>(slots), candidate.fingerprint))
        return MovementScriptError(vm, "Cannot fingerprint movement source");
    for (const auto& existing : binding->programs)
        if (existing.registered && std::strcmp(existing.name, name) == 0)
        {
            if (std::strcmp(existing.fingerprint, candidate.fingerprint) || std::strcmp(existing.presentation, presentation))
                return MovementScriptError(vm, "A movement program cannot be replaced during a VM lifetime");
            return 0;
        }
    unsigned index = 0;
    while (index < binding->programs.size() && binding->programs[index].registered) ++index;
    if (index == binding->programs.size()) return MovementScriptError(vm, "Movement program capacity (16) exhausted");
    std::snprintf(candidate.registryKey, sizeof(candidate.registryKey), "__delta_movement_program_%u", index);
    std::strcpy(candidate.name, name);
    std::strcpy(candidate.presentation, presentation);
    candidate.slots = static_cast<unsigned>(slots);
    if (!PutMovementChunk(*binding, vm, candidate.registryKey, source))
    {
        LogMovementVmFailure(*binding, "compilation failed", name, vm);
        return sq_throwerror(vm, binding->lastError);
    }
    candidate.registered = true;
    binding->programs[index] = candidate;
    Warning("R1Delta movement: '%s' runs in the normal script environment and may replay. "
        "Side effects and ordinary script state are not rolled back; use Move_* state/edits "
        "and Move_EmitEvent for prediction-safe simulation and presentation.\n", name);
    return 0;
}
SQInteger EnableScriptMovement(HSQUIRRELVM vm)
{
    auto* binding = OwningMovementVm(vm); const char* name{};
    if (!binding || VmDomain(*binding) != MovementDomain::Authority || evaluating
        || binding->thread != GetCurrentThreadId() || SQ_FAILED(sq_getstring(vm, 3, &name)))
        return MovementScriptError(vm, "Movement_Enable requires SERVER VM, player, and registered program name");
    const auto* program = FindMovementProgram(MovementDomain::Authority, name);
    const auto player = reinterpret_cast<std::uintptr_t>(sq_getentity(vm, 2));
    if (!program) return MovementScriptError(vm, "Movement_Enable: program is not registered in SERVER");
    if (!MovementDomainReady(MovementDomain::Authority))
        return MovementScriptError(vm, "Movement_Enable: native v3 movement/input hooks are unavailable");
    if (!player) return MovementScriptError(vm, "Movement_Enable: argument is not a native player entity");
    if (!EnableMovementForPlayer(player, *program))
        return MovementScriptError(vm, "Movement_Enable: snapshot transport, player identity, or session allocation unavailable");
    binding->lastError[0] = 0;
    return 0;
}
SQInteger DisableScriptMovement(HSQUIRRELVM vm)
{
    auto* binding = OwningMovementVm(vm);
    if (!binding || VmDomain(*binding) != MovementDomain::Authority || evaluating || binding->thread != GetCurrentThreadId())
        return MovementScriptError(vm, "Movement_Disable requires the owning SERVER VM outside prediction");
    const auto player = reinterpret_cast<std::uintptr_t>(sq_getentity(vm, 2));
    if (!player || !DisableMovementForPlayer(player)) return MovementScriptError(vm, "Movement_Disable requires a native player");
    return 0;
}
SQInteger GetMovementApiVersion(HSQUIRRELVM vm)
{
    return PushMovementInteger(vm, kMovementApiVersion);
}
SQInteger QueryMovementAvailable(HSQUIRRELVM vm)
{
    auto* binding = OwningMovementVm(vm);
    return PushMovementBoolean(vm, binding && binding->prepared && MovementDomainReady(VmDomain(*binding)));
}
SQInteger QueryMovementEnabled(HSQUIRRELVM vm)
{
    auto* binding = OwningMovementVm(vm);
    const char* name{};
    if (!binding || VmDomain(*binding) != MovementDomain::Authority
        || SQ_FAILED(sq_getstring(vm, 3, &name)))
        return MovementScriptError(vm, "Movement_IsEnabled requires SERVER, player, and program name");
    return PushMovementBoolean(vm, IsMovementEnabledForPlayer(
        reinterpret_cast<std::uintptr_t>(sq_getentity(vm, 2)), name));
}
// True when the named map is one of the converted Titanfall 2 maps the R2 map provider serves (r2port::IsR2PortMap).
SQInteger QueryR2PortMap(HSQUIRRELVM vm)
{
    const char* name{};
    if (SQ_FAILED(sq_getstring(vm, 2, &name)) || !name)
        return MovementScriptError(vm, "Movement_IsR2PortMap requires a map name");
    return PushMovementBoolean(vm, r2port::IsR2PortMap(name));
}
SQInteger QueryMovementLastError(HSQUIRRELVM vm)
{
    sq_pushstring(vm, OwningMovementVm(vm)->lastError, -1);
    return 1;
}
SQInteger ReadAuthorityState(HSQUIRRELVM vm, unsigned kind)
{
    auto* binding = OwningMovementVm(vm);
    const char* name{}; SQInteger index{-1};
    if (!binding || VmDomain(*binding) != MovementDomain::Authority || evaluating || inputEvaluation
        || binding->thread != GetCurrentThreadId() || SQ_FAILED(sq_getstring(vm, 3, &name))
        || ((kind == 1 || kind == 2) && !ReadIntegerArgument(vm, 4, index)))
        return MovementScriptError(vm, "Authority state requires SERVER outside prediction, player, program name, and a valid slot");
    std::uint32_t word{}, command{}, session{};
    float time{};
    const bool available = ReadMovementAuthorityState(reinterpret_cast<std::uintptr_t>(sq_getentity(vm, 2)),
        name, index, word, command, session, time);
    if (kind == 0) return PushMovementBoolean(vm, available);
    if (!available) return MovementScriptError(vm, "No published authoritative movement state for this player/program/slot");
    if (kind == 1) return PushMovementInteger(vm, static_cast<std::int32_t>(word));
    if (kind == 3) return PushMovementInteger(vm, command);
    if (kind == 4) return PushMovementInteger(vm, session);
    if (kind == 5) return PushMovementNumber(vm, time);
    float value{}; std::memcpy(&value, &word, sizeof(value));
    if (!std::isfinite(value)) return MovementScriptError(vm, "Authority state slot is not a finite float");
    return PushMovementNumber(vm, value);
}
SQInteger QueryAuthorityState(HSQUIRRELVM vm) { return ReadAuthorityState(vm, 0); }
SQInteger ReadAuthorityInteger(HSQUIRRELVM vm) { return ReadAuthorityState(vm, 1); }
SQInteger ReadAuthorityFloat(HSQUIRRELVM vm) { return ReadAuthorityState(vm, 2); }
SQInteger ReadAuthorityCommand(HSQUIRRELVM vm) { return ReadAuthorityState(vm, 3); }
SQInteger ReadAuthoritySession(HSQUIRRELVM vm) { return ReadAuthorityState(vm, 4); }
SQInteger ReadAuthorityTime(HSQUIRRELVM vm) { return ReadAuthorityState(vm, 5); }
const MovementPresentationSnapshot* PresentationForVm(HSQUIRRELVM vm)
{
    const auto* binding = OwningMovementVm(vm);
    const char* name{};
    if (!binding || VmDomain(*binding) != MovementDomain::Client || evaluating || inputEvaluation
        || binding->thread != GetCurrentThreadId() || SQ_FAILED(sq_getstring(vm, 2, &name))) return nullptr;
    const auto* snapshot = ReadMovementPresentation();
    return snapshot && std::strcmp(snapshot->program->name, name) == 0 ? snapshot : nullptr;
}
SQInteger QueryMovementPresentation(HSQUIRRELVM vm)
{
    return PushMovementBoolean(vm, PresentationForVm(vm) != nullptr);
}
SQInteger ReadPresentationState(HSQUIRRELVM vm, bool asFloat)
{
    const auto* snapshot = PresentationForVm(vm);
    SQInteger index{};
    if (!snapshot)
        return MovementScriptError(vm, "No committed client presentation for this movement program");
    if (!ReadIntegerArgument(vm, 3, index) || index < 0 || index >= snapshot->program->slots)
        return MovementScriptError(vm, "Presentation state slot is outside this program's declared slots");
    const auto word = snapshot->state[static_cast<unsigned>(index)];
    if (!asFloat) return PushMovementInteger(vm, static_cast<std::int32_t>(word));
    float value{};
    std::memcpy(&value, &word, sizeof(value));
    if (!std::isfinite(value)) return MovementScriptError(vm, "Presentation slot is not a finite float");
    return PushMovementNumber(vm, value);
}
SQInteger ReadPresentationInteger(HSQUIRRELVM vm) { return ReadPresentationState(vm, false); }
SQInteger ReadPresentationFloat(HSQUIRRELVM vm) { return ReadPresentationState(vm, true); }
SQInteger ReadPresentationIdentity(HSQUIRRELVM vm, bool session)
{
    const auto* snapshot = PresentationForVm(vm);
    if (!snapshot)
        return MovementScriptError(vm, "No committed client presentation for this movement program");
    return PushMovementInteger(vm, session ? snapshot->session : snapshot->command);
}
SQInteger ReadPresentationCommand(HSQUIRRELVM vm) { return ReadPresentationIdentity(vm, false); }
SQInteger ReadPresentationSession(HSQUIRRELVM vm) { return ReadPresentationIdentity(vm, true); }
SQInteger ReadPresentationTime(HSQUIRRELVM vm)
{
    const auto* snapshot = PresentationForVm(vm);
    if (!snapshot) return MovementScriptError(vm, "No committed client presentation for this movement program");
    return PushMovementNumber(vm, snapshot->time);
}
SQInteger SetMovementView(HSQUIRRELVM vm)
{
    const auto* snapshot = PresentationForVm(vm);
    if (!snapshot) return MovementScriptError(vm, "Movement_SetView requires committed CLIENT state outside prediction");
    float fov{}, transform[6]{};
    if (!ReadNumberArgument(vm, 3, fov) || fov < 0.1f || fov > 4.0f)
        return MovementScriptError(vm, "View FOV scale must be finite in 0.1..4");
    for (unsigned index = 0; index != 6; ++index)
        if (!ReadNumberArgument(vm, 4 + index, transform[index]) || std::fabs(transform[index]) > 360.0f)
            return MovementScriptError(vm, "Viewmodel local offsets/angles must be finite within +/-360");
    if (!presentation::SetViewModelTransform(snapshot->player, snapshot->session, transform)
        || !presentation::SetFovScale(snapshot->player, snapshot->session, fov))
    {
        presentation::ClearViewModelTransform(snapshot->player, snapshot->session);
        presentation::ClearFovScale(snapshot->player, snapshot->session);
        return MovementScriptError(vm, "Client movement view hooks or per-player presentation capacity unavailable");
    }
    return 0;
}
SQInteger ClearMovementView(HSQUIRRELVM vm)
{
    auto* binding = OwningMovementVm(vm);
    if (!binding || VmDomain(*binding) != MovementDomain::Client || evaluating || inputEvaluation
        || binding->thread != GetCurrentThreadId())
        return MovementScriptError(vm, "Movement_ClearView requires CLIENT outside prediction");
    if (const auto* snapshot = PresentationForVm(vm))
    {
        presentation::ClearViewModelTransform(snapshot->player, snapshot->session);
        presentation::ClearFovScale(snapshot->player, snapshot->session);
    }
    return 0;
}
SQInteger ChangeMovementActivity(HSQUIRRELVM vm, bool clear)
{
    auto* binding = OwningMovementVm(vm); SQInteger session{}, activity{};
    if (!binding || VmDomain(*binding) != MovementDomain::Client || evaluating || inputEvaluation
        || binding->thread != GetCurrentThreadId() || !ReadIntegerArgument(vm, 3, session) || !session
        || (!clear && (!ReadIntegerArgument(vm, 4, activity) || activity < 0)))
        return MovementScriptError(vm, "Movement activity requires CLIENT outside prediction, player, nonzero session, and valid activity");
    const auto player = reinterpret_cast<std::uintptr_t>(sq_getcliententity(vm, 2));
    if (!player) return MovementScriptError(vm, "Movement activity requires a valid player entity");
    if (clear) { presentation::ClearMainActivityOverride(player, session); return 0; }
    return PushMovementBoolean(vm, presentation::SetMainActivityOverride(player, session, activity));
}
SQInteger SetMovementActivity(HSQUIRRELVM vm) { return ChangeMovementActivity(vm, false); }
SQInteger ClearMovementActivity(HSQUIRRELVM vm) { return ChangeMovementActivity(vm, true); }
SQInteger ReadMovementSurfaceNameScript(HSQUIRRELVM vm)
{
    auto* binding = OwningMovementVm(vm);
    SQInteger index{};
    if (!binding || evaluating || inputEvaluation || binding->thread != GetCurrentThreadId()
        || !ReadIntegerArgument(vm, 2, index)
        || index < (std::numeric_limits<int>::min)() || index > (std::numeric_limits<int>::max)())
        return MovementScriptError(vm, "Movement_GetSurfaceName requires an owning VM outside movement/input callbacks and an int index");
    const auto* name = ReadMovementSurfaceName(VmDomain(*binding), static_cast<int>(index));
    if (!name) return MovementScriptError(vm, "Movement surface registry or index unavailable");
    sq_pushstring(vm, name, -1);
    return 1;
}
SQInteger SetMovementSlideActivity(HSQUIRRELVM vm)
{
    auto* binding = OwningMovementVm(vm);
    SQInteger session{};
    SQObject active{};
    if (!binding || evaluating || inputEvaluation
        || binding->thread != GetCurrentThreadId() || !ReadIntegerArgument(vm, 3, session)
        || session < (std::numeric_limits<int>::min)() || session > (std::numeric_limits<int>::max)()
        || SQ_FAILED(sq_getstackobj(binding->owner, vm, 4, &active)) || active._type != OT_BOOL)
        return MovementScriptError(vm, "Movement_SetSlideActivity requires an owning VM outside movement/input callbacks, player, int session, and bool");
    const auto side = VmDomain(*binding);
    const auto player = reinterpret_cast<CBaseEntity*>(side == MovementDomain::Client
        ? sq_getcliententity(vm, 2) : sq_getentity(vm, 2));
    if (!player) return MovementScriptError(vm, "Movement_SetSlideActivity requires a valid player entity");
    const auto changed = side == MovementDomain::Client
        ? presentation::SetSlideActivityOverride(player, static_cast<int>(session), active._unVal.nInteger != 0)
        : presentation::SetServerSlideActivityOverride(reinterpret_cast<std::uintptr_t>(player),
            static_cast<int>(session), active._unVal.nInteger != 0);
    return PushMovementBoolean(vm, changed);
}
SQInteger SetMovementInputCallback(HSQUIRRELVM vm)
{
    auto* binding = OwningMovementVm(vm);
    const char *name{}, *callback{};
    if (!binding || VmDomain(*binding) != MovementDomain::Client || evaluating || inputEvaluation
        || binding->thread != GetCurrentThreadId()
        || SQ_FAILED(sq_getstring(vm, 2, &name)) || SQ_FAILED(sq_getstring(vm, 3, &callback))
        || !callback || std::strlen(callback) > 64)
        return MovementScriptError(vm, "Movement_SetInputCallback requires CLIENT, program name, synchronous callback name[0..64]");
    auto* program = const_cast<MovementProgram*>(FindMovementProgram(MovementDomain::Client, name));
    if (!program) return MovementScriptError(vm, "Movement input program is not registered");
    std::strcpy(program->inputCallback, callback);
    return 0;
}
SQInteger ReadMovementInputButtons(HSQUIRRELVM vm)
{
    if (!inputEvaluation || VmDomain(*OwningMovementVm(vm)) != MovementDomain::Client)
        return MovementScriptError(vm, "Move_InputGetButtons requires the client command input callback");
    return PushMovementInteger(vm, inputEvaluation->buttons);
}
SQInteger ReadMovementInputCommand(HSQUIRRELVM vm)
{
    if (!inputEvaluation || VmDomain(*OwningMovementVm(vm)) != MovementDomain::Client)
        return MovementScriptError(vm, "Move_InputGetCommandNumber requires the client command input callback");
    return PushMovementInteger(vm, inputEvaluation->command);
}
SQInteger ChangeMovementInputButtons(HSQUIRRELVM vm)
{
    SQInteger buttons{};
    if (!inputEvaluation || VmDomain(*OwningMovementVm(vm)) != MovementDomain::Client
        || !ReadIntegerArgument(vm, 2, buttons))
        return MovementScriptError(vm, "Move_InputSetButtons requires the client command input callback and integer buttons");
    inputEvaluation->buttons = static_cast<std::uint32_t>(buttons);
    return 0;
}
struct MovementBinding { const char* name; SQFUNCTION function; const char* mask; int arguments; const char* result; };
#define MREAD(NAME, FN, TYPE) {NAME, FN, ".", 1, TYPE}
#define MAXIS(NAME, FN) {NAME, FN, ".i", 2, "float"}
#define MWRITE(NAME, FN) {NAME, FN, ".n", 2, "void"}
const MovementBinding movementBindings[] = {
    {"Movement_Register", RegisterMovementProgram, ".ssis", 5, "void"},
    {"Movement_Enable", EnableScriptMovement, ".Is", 3, "void"},
    {"Movement_Disable", DisableScriptMovement, ".I", 2, "void"},
    MREAD("Movement_GetApiVersion", GetMovementApiVersion, "int"),
    MREAD("Movement_IsAvailable", QueryMovementAvailable, "bool"),
    MREAD("Movement_GetLastError", QueryMovementLastError, "string"),
    {"Movement_IsEnabled", QueryMovementEnabled, ".Is", 3, "bool"},
    {"Movement_IsR2PortMap", QueryR2PortMap, ".s", 2, "bool"},
    {"Movement_HasPresentation", QueryMovementPresentation, ".s", 2, "bool"},
    {"Movement_GetPresentationInt", ReadPresentationInteger, ".si", 3, "int"},
    {"Movement_GetPresentationFloat", ReadPresentationFloat, ".si", 3, "float"},
    {"Movement_GetPresentationCommand", ReadPresentationCommand, ".s", 2, "int"},
    {"Movement_GetPresentationSession", ReadPresentationSession, ".s", 2, "int"},
    {"Movement_GetPresentationTime", ReadPresentationTime, ".s", 2, "float"},
    {"Movement_GetSurfaceName", ReadMovementSurfaceNameScript, ".i", 2, "string"},
    {"Movement_SetView", SetMovementView, ".snnnnnnn", 9, "void"},
    {"Movement_ClearView", ClearMovementView, ".s", 2, "void"},
    {"Movement_SetActivity", SetMovementActivity, ".Iii", 4, "bool"},
    {"Movement_ClearActivity", ClearMovementActivity, ".Ii", 3, "void"},
    {"Movement_SetSlideActivity", SetMovementSlideActivity, ".Iib", 4, "bool"},
    {"Movement_HasPlayerState", QueryAuthorityState, ".Is", 3, "bool"},
    {"Movement_GetPlayerStateInt", ReadAuthorityInteger, ".Isi", 4, "int"},
    {"Movement_GetPlayerStateFloat", ReadAuthorityFloat, ".Isi", 4, "float"},
    {"Movement_GetPlayerStateCommand", ReadAuthorityCommand, ".Is", 3, "int"},
    {"Movement_GetPlayerStateSession", ReadAuthoritySession, ".Is", 3, "int"},
    {"Movement_GetPlayerStateTime", ReadAuthorityTime, ".Is", 3, "float"},
    {"Movement_SetInputCallback", SetMovementInputCallback, ".ss", 3, "void"},
    MREAD("Move_InputGetButtons", ReadMovementInputButtons, "int"),
    MREAD("Move_InputGetCommandNumber", ReadMovementInputCommand, "int"),
    {"Move_InputSetButtons", ChangeMovementInputButtons, ".i", 2, "void"},
    MREAD("Move_GetCustomButtons", ReadMovementCustomButtons, "int"),
    MREAD("Move_IsWorldUp", ReadMovementWorldUp, "bool"),
    MREAD("Move_IsWaterJumping", ReadMovementWaterJump, "bool"),
    MREAD("Move_GetWaterLevel", ReadMovementWaterLevel, "int"),
    MREAD("Move_GetDuckState", ReadMovementDuckState, "int"),
    MREAD("Move_GetStance", ReadMovementStance, "int"),
    MREAD("Move_GetDuckRequested", ReadMovementDuckRequested, "bool"),
    MREAD("Move_GetMoveScale", ReadMovementMoveScale, "float"),
    MREAD("Move_GetSurfaceFriction", ReadMovementSurfaceFriction, "float"),
    MREAD("Move_HasGroundNormal", ReadMovementGroundNormalKnown, "bool"),
    MAXIS("Move_GetGroundNormal", ReadMovementGroundNormal),
    MAXIS("Move_GetWallNormal", ReadMovementWallNormal),
    MAXIS("Move_GetViewForward", ReadMovementViewForward),
    MAXIS("Move_GetInputDirection", ReadMovementInputDirection),
    MREAD("Move_GetWishSpeed", ReadMovementWishSpeed, "float"),
    MREAD("Move_GetNativeDeceleration", ReadMovementNativeDeceleration, "float"),
    MREAD("Move_GetSelectedJumpHeight", ReadMovementJumpHeight, "float"),
    MREAD("Move_GetGravityContribution", ReadMovementGravityContribution, "float"),
    MREAD("Move_DidJump", ReadMovementDidJump, "bool"),
    MAXIS("Move_GetWishDirection", ReadMovementWishDirection),
    MAXIS("Move_GetOutputWishVelocity", ReadMovementOutputWish),
    {"Move_SetOutputWishVelocity", ChangeMovementOutputWish, ".nnn", 4, "void"},
    MWRITE("Move_SetAcceleration", ChangeMovementAcceleration),
    MWRITE("Move_SetDeceleration", ChangeMovementDeceleration),
    MWRITE("Move_SetMoveScale", ChangeMovementMoveScale),
    MWRITE("Move_SetSelectedJumpHeight", ChangeMovementJumpHeight),
    MREAD("Move_ClearAcceleration", ClearMovementAcceleration, "void"),
    MREAD("Move_ClearDeceleration", ClearMovementDeceleration, "void"),
    {"Move_SetDuckDuration", ChangeMovementDuckDuration, ".i", 2, "void"},
    {"Move_ReplaceFriction", ChangeMovementFrictionReplacement, ".b", 2, "void"},
    {"Move_SetDuckRetention", ChangeMovementDuckRetention, ".b", 2, "void"},
    {"Move_SetCrouchJumpEnabled", ChangeMovementCrouchJump, ".b", 2, "void"},
    {"Move_SetSprintAllowed", ChangeMovementSprintAllowed, ".b", 2, "void"},
    MREAD("Move_GetTraceSurface", ReadMovementTraceSurface, "int"),
    MREAD("Move_GetGroundSurface", ReadMovementGroundSurface, "int"),
    {"Move_SetCheckpointMask", ChangeMovementCheckpointMask, ".i", 2, "void"},
    MREAD("Move_GetPhase", ReadMovementPhase, "int"),
    MREAD("Move_GetCommandNumber", ReadMovementCommand, "int"),
    MREAD("Move_GetTick", ReadMovementTick, "int"),
    MREAD("Move_GetWorldGravity", ReadMovementGravity, "float"),
    MREAD("Move_GetTime", ReadMovementTime, "float"),
    MREAD("Move_GetFrameTime", ReadMovementFrameTime, "float"),
    MREAD("Move_GetButtons", ReadMovementButtons, "int"),
    MREAD("Move_GetOldButtons", ReadMovementOldButtons, "int"),
    MREAD("Move_GetFlags", ReadMovementFlags, "int"),
    MREAD("Move_GetMoveType", ReadMovementType, "int"),
    MREAD("Move_IsOnGround", ReadMovementGrounded, "bool"),
    MREAD("Move_IsAlive", ReadMovementAlive, "bool"),
    MREAD("Move_IsTitan", ReadMovementTitan, "bool"),
    MREAD("Move_IsWallRunning", ReadMovementWallRun, "bool"),
    MREAD("Move_IsMantling", ReadMovementTraversal, "bool"),
    MREAD("Move_IsZiplining", ReadMovementZipline, "bool"),
    MAXIS("Move_GetOrigin", ReadMovementOrigin),
    MAXIS("Move_GetVelocity", ReadMovementVelocity),
    MAXIS("Move_GetViewAngles", ReadMovementAngles),
    MAXIS("Move_GetHullMins", ReadMovementHullMinimum),
    MAXIS("Move_GetHullMaxs", ReadMovementHullMaximum),
    MAXIS("Move_GetEyeOffset", ReadMovementEyeOffset),
    MREAD("Move_GetPlayerHandle", ReadMovementPlayerHandle, "int"),
    MREAD("Move_GetPlayer", ReadMovementPlayer, "entity"),
    {"Move_SetWeaponLock", ChangeMovementWeaponLock, ".b", 2, "bool"},
    {"Move_IsEntityValid", QueryMovementEntity, ".i", 2, "bool"},
    {"Move_GetEntityOrigin", QueryMovementEntityOrigin, ".ii", 3, "float"},
    {"Move_SetOrigin", ChangeMovementOrigin, ".nnn", 4, "bool"},
    {"Move_SetVelocity", ChangeMovementVelocity, ".nnn", 4, "void"},
    {"Move_SetButtons", ChangeMovementButtons, ".i", 2, "void"},
    {"Move_SetOldButtons", ChangeMovementOldButtons, ".i", 2, "void"},
    MREAD("Move_GetForwardSpeed", ReadMovementForward, "float"),
    MREAD("Move_GetSideSpeed", ReadMovementSide, "float"),
    MREAD("Move_GetUpSpeed", ReadMovementUp, "float"),
    MREAD("Move_GetMaxSpeed", ReadMovementMaximum, "float"),
    MREAD("Move_GetClientMaxSpeed", ReadMovementClientMaximum, "float"),
    MWRITE("Move_SetForwardSpeed", ChangeMovementForward),
    MWRITE("Move_SetSideSpeed", ChangeMovementSide),
    MWRITE("Move_SetUpSpeed", ChangeMovementUp),
    MWRITE("Move_SetMaxSpeed", ChangeMovementMaximum),
    MWRITE("Move_SetClientMaxSpeed", ChangeMovementClientMaximum),
    MWRITE("Move_SetSpeedScale", ChangeMovementSpeedScale),
    MWRITE("Move_SetFrictionScale", ChangeMovementFrictionScale),
    MREAD("Move_GetRemainingAirJumps", ReadMovementJumpCount, "int"),
    {"Move_SetRemainingAirJumps", ChangeMovementJumpCount, ".i", 2, "void"},
    MREAD("Move_GetJumpPower", ReadMovementJumpPower, "float"),
    MWRITE("Move_SetJumpPower", ChangeMovementJumpPower),
    {"Move_GetFloatState", QueryMovementStateFloat, ".i", 2, "float"},
    {"Move_GetIntState", QueryMovementStateInteger, ".i", 2, "int"},
    {"Move_SetFloatState", ChangeMovementStateFloat, ".in", 3, "void"},
    {"Move_SetIntState", ChangeMovementStateInteger, ".ii", 3, "void"},
    {"Move_TraceHull", QueryMovementTrace, ".nnnnnnnnnnnni", -14, "void"},
    MREAD("Move_GetTraceFraction", ReadMovementTraceFraction, "float"),
    MAXIS("Move_GetTraceEnd", ReadMovementTraceEnd),
    MAXIS("Move_GetTraceNormal", ReadMovementTraceNormal),
    MREAD("Move_TraceStartedSolid", ReadMovementTraceStart, "bool"),
    MREAD("Move_TraceAllSolid", ReadMovementTraceSolid, "bool"),
    MREAD("Move_TraceHitSky", ReadMovementTraceSky, "bool"),
    MREAD("Move_GetTraceEntityHandle", ReadMovementTraceEntity, "int"),
    {"Move_Math", EvaluateMovementMath, ".inn", 4, "float"},
    {"Move_EmitEvent", QueueMovementEvent, ".innn", 5, "void"}
};
#undef MREAD
#undef MAXIS
#undef MWRITE
struct MovementConstant { const char* name; int value; };
constexpr MovementConstant movementConstants[] = {
    {"MOVE_FRICTION", 3}, {"MOVE_GROUND_CONTACT", 4}, {"MOVE_JUMP", 5}, {"MOVE_JUMP_FINISH", 6},
    {"MOVE_START_GRAVITY", 7}, {"MOVE_FINISH_GRAVITY", 8}, {"MOVE_DUCK", 9}, {"MOVE_WALK", 10},
    {"MOVE_CHECKPOINT_FRICTION", 1}, {"MOVE_CHECKPOINT_GROUND_CONTACT", 2},
    {"MOVE_CHECKPOINT_JUMP", 4}, {"MOVE_CHECKPOINT_JUMP_FINISH", 8},
    {"MOVE_CHECKPOINT_START_GRAVITY", 16}, {"MOVE_CHECKPOINT_FINISH_GRAVITY", 32},
    {"MOVE_CHECKPOINT_DUCK", 64}, {"MOVE_CHECKPOINT_WALK", 128}, {"MOVE_CHECKPOINT_ALL", 255},
    {"MOVE_SETUP", 0}, {"MOVE_SIMULATE", 1}, {"MOVE_FINISH", 2}, {"MOVE_X", 0}, {"MOVE_Y", 1}, {"MOVE_Z", 2},
    {"MOVE_MATH_SQRT", 0}, {"MOVE_MATH_SIN", 1}, {"MOVE_MATH_COS", 2}, {"MOVE_MATH_ATAN2", 3},
    {"MOVE_MATH_POW", 4}, {"MOVE_MATH_ABS", 5}, {"MOVE_MATH_MIN", 6}, {"MOVE_MATH_MAX", 7},
    {"MOVE_MASK_PLAYERSOLID", 0x0201400B}, {"MOVE_MASK_SOLID", 0x0200400B},
    {"MOVE_COLLISION_GROUP_NONE", 0}, {"MOVE_COLLISION_GROUP_PLAYER_MOVEMENT", 8},
    {"MOVE_MAX_STATE_SLOTS", kMovementSlots}, {"MOVE_MAX_EVENTS", kMovementEvents},
    {"MOVE_BUTTON_ATTACK", 1}, {"MOVE_BUTTON_JUMP", 2}, {"MOVE_BUTTON_DUCK", 4},
    {"MOVE_BUTTON_FORWARD", 8}, {"MOVE_BUTTON_BACK", 16}, {"MOVE_BUTTON_USE", 32},
    {"MOVE_BUTTON_LEFT", 128}, {"MOVE_BUTTON_RIGHT", 256}, {"MOVE_BUTTON_MOVELEFT", 512},
    {"MOVE_BUTTON_MOVERIGHT", 1024}, {"MOVE_BUTTON_ATTACK2", 2048}, {"MOVE_BUTTON_RELOAD", 8192},
    {"MOVE_EVENT_RESET", -1}
};
std::vector<std::unique_ptr<SQFuncRegistration>> registrations[2];

bool SelectMovementPrimitives(MovementVm& binding)
{
    if (!G_vscript || !sq_gettop || !sq_getsize || !sq_settop || !sq_pushstring || !sq_getstackobj || !AddSquirrelReg) return false;
    const char* digest = IsR1ODedicatedServer()
        ? "10b75abc67eadc02cf0bdcebab8bca1eb76fd241aca4d5a2a69a125a1b4aa0f6"
        : IsDedicatedServer()
        ? "c779b517716cfcf767edf968b0176d1dc1ee776532303483c732dac862563c45"
        : "c0320bbe75b59ce221e3fb1dbdcdf8857e9305845ff9b0f3f3bfb4897d4b7a38";
    if (!VerifyMovementModuleDigest(G_vscript, digest)) return false;
    auto& api = binding.api;
    const bool client = VmDomain(binding) == MovementDomain::Client;
    const auto entityModule = client ? G_client : G_server;
    if (!entityModule || !VerifyMovementModuleDigest(entityModule, client
        ? "5b3f00805568eeaa6ee1cf889f12dc4570eb28a3fad2626ec0578eae3e7afd5e"
        : "50faad928e0715a51ad624d0a3eb8f00f7ffad9e66258dc95c960f9d11ff41e4")) return false;
    constexpr unsigned char clientInstancePrefix[] = {0x40,0x57,0x48,0x83,0xEC,0x20,0x48,0x83,0x79,0x50,0x00,0x48,0x8B,0xF9};
    constexpr unsigned char serverInstancePrefix[] = {0x40,0x57,0x48,0x83,0xEC,0x20,0x48,0x83,0xB9,0x50,0x05,0x00,0x00,0x00,0x48,0x8B,0xF9};
    const auto entityInstance = entityModule + (client ? 0x2A6B0 : 0x3CC080);
    if (client
        ? std::memcmp(reinterpret_cast<void*>(entityInstance), clientInstancePrefix, sizeof(clientInstancePrefix)) != 0
        : std::memcmp(reinterpret_cast<void*>(entityInstance), serverInstancePrefix, sizeof(serverInstancePrefix)) != 0) return false;
    // CSquirrelVM's native object push is slot 111 in R1/R1D and 113 in
    // TFO. These exact implementations own all stack reference accounting.
    const auto pushObject = G_vscript + (IsR1ODedicatedServer() ? 0x28F10 : IsDedicatedServer() ? 0xEAF0 : 0xEAD0);
    constexpr unsigned char r1PushPrefix[] = {0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x30,0x41,0x0F,0x10,0x00};
    constexpr unsigned char tfoPushPrefix[] = {0x40,0x57,0x48,0x83,0xEC,0x40,0x48,0xC7,0x44,0x24,0x20,0xFE,0xFF,0xFF,0xFF};
    if (IsR1ODedicatedServer()
        ? std::memcmp(reinterpret_cast<void*>(pushObject), tfoPushPrefix, sizeof(tfoPushPrefix)) != 0
        : std::memcmp(reinterpret_cast<void*>(pushObject), r1PushPrefix, sizeof(r1PushPrefix)) != 0) return false;
    api.entityInstanceOffset = client ? 0x50 : 0x550;
    api.pushObject = reinterpret_cast<PushScriptObject>(pushObject);
    if (IsR1ODedicatedServer())
    {
        constexpr unsigned char compilePrefix[] = {0x48,0x8B,0xC4,0x41,0x56,0x48,0x81,0xEC,0x40,0x03,0x00,0x00};
        constexpr unsigned char rootPrefix[] = {0x8B,0x51,0x50,0x4C,0x8B,0xC1,0x44,0x8B,0xCA,0x49,0xC1,0xE1,0x04};
        if (std::memcmp(reinterpret_cast<void*>(G_vscript + 0x2BBE0), compilePrefix, sizeof(compilePrefix))
            || std::memcmp(reinterpret_cast<void*>(G_vscript + 0x2CE00), rootPrefix, sizeof(rootPrefix))) return false;
        api.get = reinterpret_cast<sq_get_t>(G_vscript + 0x2DCC0);
        api.newSlot = reinterpret_cast<sq_newslot_t>(G_vscript + 0x2D370);
        api.root = reinterpret_cast<base_getroottable_t>(G_vscript + 0x2CE00);
        api.call = reinterpret_cast<sq_call_t>(G_vscript + 0x2E7B0);
        api.remoteCompile = reinterpret_cast<RemoteCompile>(G_vscript + 0x2BBE0);
    }
    else
    {
        if (!sq_compilebuffer || !sq_get || !sq_newslot || !base_getroottable || !sq_call) return false;
        api.get = sq_get; api.newSlot = sq_newslot; api.root = base_getroottable; api.call = sq_call;
    }
    return true;
}
bool GetMovementRootValue(MovementVm& binding, HSQUIRRELVM vm, int rootIndex, const char* key)
{
    sq_pushstring(vm, key, -1);
    return SQ_SUCCEEDED(binding.api.get(vm, rootIndex));
}
void LogMovementVmFailure(MovementVm& binding, const char* operation, const char* name, HSQUIRRELVM vm)
{
    const auto* error = reinterpret_cast<const SQObject*>(reinterpret_cast<std::uintptr_t>(vm ? vm : binding.owner->sqvm) + 0x68);
    const char* message = error->_type == OT_STRING && error->_unVal.pString
        ? reinterpret_cast<const char*>(error->_unVal.pString) + 0x38 : "script execution failed";
    char text[1280];
    std::snprintf(text, sizeof(text), "R1Delta movement: %s '%s': %.1024s\n", operation, name, message);
    std::snprintf(binding.lastError, sizeof(binding.lastError), "%s", text);
    Warning("%s", text);
    OutputDebugStringA(text);
}
}
bool DispatchMovementInput(std::uintptr_t player, std::uintptr_t command, std::uint32_t& buttons)
{
    auto& binding = DomainVm(MovementDomain::Client);
    const auto* program = ActiveClientMovementProgram(player);
    if (!program || !program->inputCallback[0] || !binding.prepared || !binding.owner
        || evaluating || inputEvaluation || binding.thread != GetCurrentThreadId()) return false;
    auto vm = binding.owner->sqvm;
    const int top = sq_gettop(binding.owner, vm);
    binding.api.root(vm);
    bool ok = GetMovementRootValue(binding, vm, top + 1, program->inputCallback);
    MovementInputEvaluation input{*reinterpret_cast<const std::uint32_t*>(command), 0};
    if (ok)
    {
        binding.api.root(vm);
        inputEvaluation = &input;
        ok = SQ_SUCCEEDED(binding.api.call(vm, 1, SQFalse, SQFalse)) && !input.failed;
        inputEvaluation = nullptr;
    }
    if (!ok) LogMovementVmFailure(binding, "input callback failed", program->inputCallback);
    sq_settop(vm, top);
    if (ok) buttons = input.buttons;
    return ok;
}

bool AdmitMovementVm(R1SquirrelVM* owner, ScriptContext context)
{
    if (context == SCRIPT_CONTEXT_UI) return true;
    if (!owner || !owner->sqvm) return false;
    const auto domain = context == SCRIPT_CONTEXT_CLIENT ? MovementDomain::Client : MovementDomain::Authority;
    auto& binding = DomainVm(domain);
    if (binding.owner == owner && binding.prepared) return true;
    if (binding.owner) ForgetMovementVm(binding.owner);
    binding.owner = owner;
    binding.thread = GetCurrentThreadId();
    if (!SelectMovementPrimitives(binding))
    {
        Warning("R1Delta movement: unsupported script runtime; opt-in is unavailable.\n");
        binding = {};
        return false;
    }
    auto& declarations = registrations[static_cast<unsigned>(domain)];
    if (declarations.empty())
        for (const auto& definition : movementBindings)
            declarations.push_back(std::make_unique<SQFuncRegistration>(context, definition.name,
                definition.function, definition.mask, definition.arguments, definition.result, "",
                "Opt-in command movement API; movement accessors require a synchronous evaluation."));
    for (const auto& declaration : declarations) AddSquirrelReg(owner, declaration->GetInternalReg());
    auto vm = owner->sqvm;
    const int top = sq_gettop(owner, vm);
    binding.api.root(vm);
    bool ok = true;
    for (const auto& constant : movementConstants)
    {
        sq_pushstring(vm, constant.name, -1);
        sq_pushinteger(owner, vm, constant.value);
        if (SQ_FAILED(binding.api.newSlot(vm, top + 1, SQFalse))) { ok = false; break; }
    }
    sq_settop(vm, top);
    binding.prepared = ok;
    if (!ok) Warning("R1Delta movement: cannot register movement constants.\n");
    return ok;
}
const MovementProgram* FindMovementProgram(MovementDomain domain, const char* name)
{
    if (!name) return nullptr;
    const auto& binding = DomainVm(domain);
    if (!binding.prepared) return nullptr;
    for (const auto& program : binding.programs)
        if (program.registered && std::strcmp(program.name, name) == 0) return &program;
    return nullptr;
}
bool ExecuteMovementSource(MovementEvaluation& sample)
{
    auto& binding = DomainVm(sample.domain);
    if (!binding.prepared || !sample.program || !binding.owner || evaluating
        || binding.thread != GetCurrentThreadId())
    {
        Warning("R1Delta movement: rejected VM thread/reentrancy/lifetime violation.\n");
        return false;
    }
    auto vm = binding.owner->sqvm;
    const int top = sq_gettop(binding.owner, vm);
    binding.api.root(vm);
    bool ok = GetMovementRootValue(binding, vm, top + 1, sample.program->registryKey);
    if (ok)
    {
        binding.api.root(vm);
        evaluating = &sample;
        ok = SQ_SUCCEEDED(binding.api.call(vm, 1, SQFalse, SQFalse)) && !sample.failed;
        evaluating = nullptr;
    }
    if (!ok) LogMovementVmFailure(binding, "evaluation failed", sample.program->name);
    sq_settop(vm, top);
    return ok;
}
void DeliverMovementPresentation(const MovementProgram& program, std::uint32_t command, int event, const MoveVector& value)
{
    auto& binding = DomainVm(MovementDomain::Client);
    if (!binding.prepared || !binding.owner || !*program.presentation || (program.presentationFailed && event != -1)) return;
    if (evaluating || binding.thread != GetCurrentThreadId())
    {
        Warning("R1Delta movement: presentation called outside the owning VM thread.\n");
        return;
    }
    auto vm = binding.owner->sqvm;
    const int top = sq_gettop(binding.owner, vm);
    binding.api.root(vm);
    bool ok = GetMovementRootValue(binding, vm, top + 1, program.presentation);
    if (ok)
    {
        binding.api.root(vm);
        sq_pushinteger(binding.owner, vm, static_cast<SQInteger>(command));
        sq_pushinteger(binding.owner, vm, event);
        sq_pushfloat(binding.owner, vm, value.x);
        sq_pushfloat(binding.owner, vm, value.y);
        sq_pushfloat(binding.owner, vm, value.z);
        ok = SQ_SUCCEEDED(binding.api.call(vm, 6, SQFalse, SQFalse));
    }
    if (!ok)
    {
        LogMovementVmFailure(binding, "presentation disabled", program.presentation);
        program.presentationFailed = true;
    }
    sq_settop(vm, top);
}
void ClearMovementVmPrograms(MovementDomain domain)
{
    auto& binding = DomainVm(domain);
    if (!binding.owner) return;
    if (evaluating || binding.thread != GetCurrentThreadId())
    {
        Warning("R1Delta movement: unsafe VM teardown thread/reentrancy.\n");
        RaiseFailFastException(nullptr, nullptr, 0);
        return;
    }
    if (binding.owner->sqvm && binding.api.root && binding.api.newSlot)
    {
        auto vm = binding.owner->sqvm;
        const int top = sq_gettop(binding.owner, vm);
        binding.api.root(vm);
        for (auto& program : binding.programs) if (program.registered)
        {
            sq_pushstring(vm, program.registryKey, -1); sq_pushnull(vm);
            binding.api.newSlot(vm, top + 1, SQFalse);
        }
        sq_settop(vm, top);
    }
    binding = {};
}
void ForgetMovementVm(R1SquirrelVM* owner)
{
    for (unsigned i = 0; i < 2; ++i)
        if (movementVms[i].owner == owner)
        {
            const auto domain = static_cast<MovementDomain>(i);
            InvalidateMovementDomain(domain);
            ClearMovementVmPrograms(domain);
        }
}
}
