#pragma once

#include <cstdint>

// Replaces the R1 client filesystem's VPK async-precache worker. It repairs the
// retail miss-path stack slot, validates existing entry types before readiness
// dispatch, and keeps pack-store teardown serialized against active workers.
bool InstallR1ClientVPKAsyncPrecacheFix(uintptr_t filesystemBase);
