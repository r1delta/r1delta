#pragma once

#include "r2port_provider.h"

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace r2port {

using BspSha256 = std::array<uint8_t, 32>;

struct MapPatch {
    BspSha256 sourceBspSha256{};
    std::unordered_map<uint32_t, FileBytes> lumpOverrides;
    // Keys are normalized game-search paths such as maps/graphs/<map>.ain.
    std::unordered_map<std::string, FileBytes> fileOverrides;
};

class MapPatchSource {
public:
    virtual ~MapPatchSource() = default;
    virtual std::shared_ptr<const MapPatch> LoadPatch(std::string_view mapName) = 0;
};

// Patch adapters read <gamedir>\\r2port\\<map>.patch once its data format is defined.
bool RegisterMapPatchSource(std::shared_ptr<MapPatchSource> source);
std::filesystem::path GetR2MapPatchDirectory();
void ObserveMapFilePath(std::string_view path);
// Materializes one allowlisted BSP or entity into the keyed disk fallback.
// Returns the search-path root on success; empty means the file was unavailable.
std::filesystem::path MaterializeMapCacheFile(std::string_view path);
void ClearMapCache();

void InstallR2MapProvider();

} // namespace r2port
