#pragma once
#include <cstdint>

namespace r1delta::movement::presentation
{
// Ropes have a reduced receive table without owner/visibility fields. Respect
// the replicated terminal anchor's owner-exclusion declaration for an active
// local predicted presentation, leaving every other native rope unchanged.
bool InstallRopeOwnerVisibility(std::uintptr_t clientModule) noexcept;
bool RopeOwnerVisibilityReady(std::uintptr_t clientModule) noexcept;
bool UninstallRopeOwnerVisibility() noexcept;
}
