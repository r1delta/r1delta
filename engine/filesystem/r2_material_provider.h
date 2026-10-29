#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "engine/filesystem/r2port_provider.h"

namespace r2port {

void RegisterR2MaterialProvider();
void RegisterR2TextureCacheConVar();
void StartR2MaterialProvider();
void PrefetchR2MapMaterials(std::string_view mapName, FileBytes bspData);
// Names are material paths without the materials/ prefix and .vmt extension.
void PrefetchR2ModelMaterials(const std::vector<std::string>& materialNames);
void LogR2TextureProviderMetrics(const char* phase);

} // namespace r2port
