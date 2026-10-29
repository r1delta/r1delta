#pragma once

#include <string_view>

#include "engine/filesystem/r2port_provider.h"

namespace r2port {

// Serves models/**.mdl, .vvd, .dx11.vtx, .vtx, .phy and .vvc for the allowlisted R2 maps by
// converting the R2 MDL v53 files in the R2 install's map and common VPKs to R1 files.
void RegisterR2ModelProvider();
// Converts the models the converted BSP's static-prop lump and the map's entity files refer to
// on a background thread and registers their materials with the material provider.
void PrefetchR2MapModels(std::string_view mapName, FileBytes convertedBsp);
void LogR2ModelProviderMetrics(const char* phase);

} // namespace r2port
