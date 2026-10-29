#pragma once

#include <cstdint>

namespace r1delta::movement::slide_model
{
// R1 retail client.dll only (SHA-256 5b3f0080...). Installs the exact client pose consumers that
// retarget the authored 190-bone slide source rig onto the nine transferred MP pilot roots
// (223-bone male, 228-bone female). It never hooks CalcMainActivity and never writes prediction or
// simulation state. Idempotent for the same module; returns false for an unrecognized image, entry
// prologue or callsite, with all hooks rolled back. Call after MinHook init.
bool InstallClient(std::uintptr_t clientModule) noexcept;

// True only when the client module is installed and the player's current native model is one of the
// nine supported pilot roots whose pinned GAME assets, include graph and sequence/pose closure are
// admitted for the current model generation. Client thread only; the first call may read the assets.
bool SupportsPlayer(std::uintptr_t player) noexcept;

// R1 retail server.dll only (SHA-256 50faad92...). Independent of client installation.
// Validates the exact server pose profile before installing three authoritative consumers.
// Call after MinHook init.
bool InstallServer(std::uintptr_t serverModule) noexcept;

// Server-side counterpart: true only for an admitted supported pilot player in the installed server
// module. Server thread only; the first call for a model may read the pinned GAME assets.
bool SupportsServerPlayer(std::uintptr_t player) noexcept;

// Call separately for each side only when its callbacks are quiescent and before MinHook teardown.
// Each drops only its own model/pose state, releases its native source header and removes its hooks.
void ShutdownClient() noexcept;
void ShutdownServer() noexcept;
}
