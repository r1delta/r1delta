#pragma once
#include "ffa_targeting_logic.h"

#include <cstdint>

namespace r1delta::ffa_targeting
{
bool InstallClientHooks(std::uintptr_t clientBase);
bool InstallServerHooks(std::uintptr_t serverBase);

void SetClientFfaBased(bool enabled) noexcept;
void SetServerFfaBased(bool enabled) noexcept;
bool IsClientFfaBased() noexcept;
bool IsServerFfaBased() noexcept;
// Client: the player an entity belongs to (itself, its boss player or its owner chain). Without the
// validated client.dll getters (InstallClientHooks) only a player entity resolves, to itself.
void* ResolveClientPlayerOwner(void* entity);
}
