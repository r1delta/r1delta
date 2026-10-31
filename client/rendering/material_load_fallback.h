// Initializes a material whose VMT cannot be read as the material system's error material.
#pragma once

#include <cstdint>

// Exact R1 Client2015 materialsystem_dx11 only.
void InstallMaterialLoadFallback(std::uintptr_t materialSystemBase);
