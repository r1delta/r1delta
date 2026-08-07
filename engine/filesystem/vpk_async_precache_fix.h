#pragma once

#include <cstdint>

// Replaces the R1 client filesystem's VPK async-precache worker. The shipped
// worker uses an uninitialized stack slot when its initial pack lookup misses;
// pack-store teardown is deferred when it re-enters the replacement inline.
bool InstallR1ClientVPKAsyncPrecacheFix(uintptr_t filesystemBase);
