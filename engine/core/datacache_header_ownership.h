#pragma once

#include <cstdint>

// Exact R1 Client2015 datacache only; dedicated variants have different layouts.
void InstallClientDataCacheHeaderOwnership(std::uintptr_t moduleBase);
