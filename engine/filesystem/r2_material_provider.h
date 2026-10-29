#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "engine/filesystem/r2port_provider.h"

namespace r2port {

void RegisterR2MaterialProvider();
void RegisterR2TextureCacheConVar();
void StartR2MaterialProvider();
// fromSource: bspData is the R2 source BSP (called before the downgrade); otherwise the converted BSP.
void PrefetchR2MapMaterials(std::string_view mapName, FileBytes bspData, bool fromSource);
// Names are material paths without the materials/ prefix and .vmt extension.
void PrefetchR2ModelMaterials(const std::vector<std::string>& materialNames);
// Paths (materials/<name>.vmt or .vtf) of UI materials served outside any map (the overview images the map picker shows in the lobby):
// starts the provider, marks the paths known and converts them in the background.
void PrefetchR2UiTextures(const std::vector<std::string>& texturePaths);
void LogR2TextureProviderMetrics(const char* phase);

} // namespace r2port
