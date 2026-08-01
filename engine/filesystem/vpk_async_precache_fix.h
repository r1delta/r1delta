#pragma once

#include <cstdint>

// Replaces the R1 client filesystem's VPK async-precache worker. The shipped
// worker uses an uninitialized stack slot when its initial pack lookup misses.
bool InstallR1ClientVPKAsyncPrecacheFix(uintptr_t filesystemBase);
