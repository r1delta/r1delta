#pragma once

#include <cstdint>

// Replaces only the exact R1 retail client filesystem async-precache worker.
bool InstallR1ClientVPKAsyncPrecacheFix(std::uintptr_t filesystemBase);
