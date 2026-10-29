#pragma once

#include <cstddef>
#include <cstdint>
#include "squirrel.h"

namespace r1delta::movement
{
inline constexpr char kAuthorityCommand[] = "_delta_movement_native2";

// Installation is transactional and image-specific. A failed installation keeps
// ordinary movement intact and makes script opt-in fail with a useful error.
bool AttachClientMovementModule(std::uintptr_t module);
bool AttachServerMovementModule(std::uintptr_t module);

// Call after native VM globals are registered, before addon autorun. Destruction
// notification MUST precede native VM destruction. UI VMs are deliberately ignored.
bool AdmitMovementVm(R1SquirrelVM* owner, ScriptContext context);
void ForgetMovementVm(R1SquirrelVM* owner);

// Client signon/disconnect and server level shutdown are separate ownership
// boundaries: a listen server must not erase authority when its client signs on.
void ResetMovementConnection();
void ResetMovementLevel();

// Shared owners for the existing load.cpp view wrappers. Installation follows
// the movement module's exact image validation, before debug subhook setup.
bool InstallMovementClientViewSinks(std::uintptr_t clientModule);
std::uint32_t ClientMovementViewSession(std::uintptr_t player);

// Immediately after the existing per-player PhysicsSimulate original returns.
// Publishes only the final command of that batch to the packet integration.
void FlushMovementAuthorityForPlayer(std::uintptr_t player);

// Called at native per-client entity-frame capture, before asynchronous encoding.
// This freezes custom state under that frame tick; it never guesses packet ACKs.
void CaptureMovementAuthorityForSnapshot(int playerSlot, std::uint32_t nativeFrameTick);

// Append only to the SAME assembled snapshot bytes (delta or full/reliable).
// Zero means no opt-in for this identity; SIZE_MAX means required state missing:
// the transport MUST NOT send the native snapshot without its custom state.
// Capacity >= kMovementAuthorityBytes (movement_runtime.h) is sufficient. There
// is no latest-state fallback.
std::size_t CopyMovementAuthorityCommand(int playerSlot, std::uint32_t nativeFrameTick,
    char* output, std::size_t capacity);

// Stage synchronously from the trusted server's StringCmd handler, not Cbuf.
// Staging does NOT admit authority: native accepted-frame/prediction restoration
// must subsequently select this exact frame's state.
bool ConsumeMovementAuthorityCommand(const char* command);
}
