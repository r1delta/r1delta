#pragma once

#include <cstdint>

namespace r1delta::movement
{
// Call after the owning engine globals and runtime role have been established.
// A failed installation leaves native snapshot serialization unchanged; script
// movement opt-in must remain unavailable until this returns true.
bool AttachMovementSnapshotTransport(std::uintptr_t engineModule);
bool IsMovementSnapshotTransportReady();
}
