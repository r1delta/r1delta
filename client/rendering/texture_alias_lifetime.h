// Lifetime of the DX11 material system's reserved texture-alias records.
#pragma once

#include <cstdint>

// Exact R1 Client2015 materialsystem_dx11 only. The dedicated engine variants
// never create the DX11 texture records this module protects.
void InstallTextureAliasLifetime(std::uintptr_t materialSystemBase);
