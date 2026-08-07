#pragma once

#include <cstdint>

// Verifies the R1 client filesystem image and intentionally preserves the
// retail VPK async-precache worker. Reconstructing that worker outside the
// filesystem module violates its pack-entry lifetime invariants.
bool InstallR1ClientVPKAsyncPrecacheFix(uintptr_t filesystemBase);
