#pragma once

#include <cstdint>

class CBaseEntity;

namespace r1delta::movement::presentation
{
// R1 retail client.dll only. Installs the CalcMainActivity adapter at client+0x333880;
// the existing CalcViewModelView and NormalCalcView detours must call the sinks below.
bool InstallClientHooks(std::uintptr_t clientModule) noexcept;
bool ClientHooksReady() noexcept;
bool ShutdownClientHooks() noexcept; // Call only after client callbacks are quiescent.
bool InstallServerHooks(std::uintptr_t serverModule) noexcept;
bool ServerHooksReady() noexcept;
bool ShutdownServerHooks() noexcept; // Call only after server callbacks are quiescent.

// Each record is keyed by player, native entity handle, and session. Clear the
// departing session explicitly; Reset clears all per-player presentation state.
void ResetSession(std::uint32_t session) noexcept;
void Reset() noexcept;

// Values are local Source viewmodel offsets: forward, right, up, pitch, yaw, roll.
// Native weapon bob/lag is composed after this sink, so call it on copied input vectors
// immediately before the existing CalcViewModelView trampoline.
bool SetViewModelTransform(std::uintptr_t player, std::uint32_t session,
    const float localTransform[6]) noexcept;
void ClearViewModelTransform(std::uintptr_t player, std::uint32_t session) noexcept;
bool ApplyViewModelInputTransform(std::uintptr_t player, std::uint32_t session,
    float origin[3], float angles[3]) noexcept;

// The scale is multiplied into the output FOV only after native NormalCalcView returns,
// preserving all native zoom and scripted camera calculations.
bool SetFovScale(std::uintptr_t player, std::uint32_t session, float scale) noexcept;
void ClearFovScale(std::uintptr_t player, std::uint32_t session) noexcept;
bool ApplyFovScale(std::uintptr_t player, std::uint32_t session, float* fov) noexcept;

// The override is consumed after exclusive native death/traversal exits and after
// jump/duck/zipline/landing bookkeeping; it precedes vehicle/ordinary/falling selection.
// No activity is synthesized. Set returns false unless the adapter is ready and the
// current model round-trips the requested activity through a sequence.
bool SetMainActivityOverride(std::uintptr_t player, std::uint32_t session, int activity) noexcept;
void ClearMainActivityOverride(std::uintptr_t player, std::uint32_t session) noexcept;

// The slide bit is presentation-only; session zero clears the entity's local
// presentation record. Retargeting and native activity mapping gate the hook.
bool SetSlideActivityOverride(CBaseEntity* player, int session, bool active) noexcept;
bool SetServerSlideActivityOverride(std::uintptr_t player, int session, bool active) noexcept;

// Native activity names are process-local IDs. Each admitted client/server
// module is initialized independently; zero means that side is not loaded.
bool RegisterSlideActivities(std::uintptr_t clientModule, std::uintptr_t serverModule) noexcept;
void ClearPlayer(std::uintptr_t player, std::uint32_t session) noexcept;
}