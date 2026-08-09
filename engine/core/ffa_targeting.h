#pragma once
#include "ffa_targeting_logic.h"

#include <cstdint>

namespace r1delta::ffa_targeting
{
bool InstallClientHooks(std::uintptr_t clientBase);
bool InstallServerHooks(std::uintptr_t serverBase);

void SetFfaBased(bool enabled) noexcept;
bool IsFfaBased() noexcept;
}
