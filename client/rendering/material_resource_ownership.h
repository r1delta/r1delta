#pragma once

#include <cstdint>

// Exact R1 Client2015 materialsystem_dx11 only. The dedicated engine variants
// never reach the DX11 device paths this module owns.
void InstallMaterialResourceOwnership(std::uintptr_t materialSystemBase);
