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
// What is known to be wrong with the Titanfall 2 texture files on this machine, as a sentence a player can act on; empty when nothing is,
// also while the index is still building. servesNoTextures: the texture files could not be opened, so every converted map would be drawn
// in the error texture. Otherwise some rpaks were unreadable while indexing and only the textures they hold are missing.
struct R2TextureProviderProblem {
    std::string text;
    bool servesNoTextures = false;
};
R2TextureProviderProblem GetR2TextureProviderProblem();
// fromSource: bspData is the R2 source BSP (called before the downgrade); otherwise the converted BSP.
void PrefetchR2MapMaterials(std::string_view mapName, FileBytes bspData, bool fromSource);
// Names are material paths without the materials/ prefix and .vmt extension.
void PrefetchR2ModelMaterials(const std::vector<std::string>& materialNames);
// Paths (materials/<name>.vmt or .vtf) of UI materials served outside any map (loading screens, the overview images the map picker shows in
// the lobby): starts the provider, marks the paths known for the rest of the session (map changes do not forget them) and converts them in
// the background.
void PrefetchR2UiTextures(const std::vector<std::string>& texturePaths);
// A stock R1 map started: drops the converted-file LRU, queued prefetch work and the decoded package families of the previous R2
// map (index, workers and disk cache stay). No-op while the provider has never been started.
void ReleaseR2MaterialProviderMapState();
void LogR2TextureProviderMetrics(const char* phase);

} // namespace r2port
