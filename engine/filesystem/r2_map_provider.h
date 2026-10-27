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
};

class MapPatchSource {
public:
    virtual ~MapPatchSource() = default;
    virtual std::shared_ptr<const MapPatch> LoadPatch(
        std::string_view mapName, const BspSha256& sourceBspSha256) = 0;
    // Optional file override for formats bound to the source R2 BSP hash or live map state.
    virtual FileBytes LoadFileOverride(
        std::string_view, std::string_view,
        const BspSha256&, std::int32_t, std::int32_t)
    {
        return {};
    }
};

// Patch files live under <gamedir>\\r2port\\patches.
bool RegisterMapPatchSource(std::shared_ptr<MapPatchSource> source);
std::filesystem::path GetR2MapPatchDirectory();
std::filesystem::path GetR2InstallDirectory();
// True for the allowlisted R2-only maps this provider serves.
bool IsR2PortMap(std::string_view mapName);
void ObserveMapFilePath(std::string_view path);
// Materializes one allowlisted BSP or entity into the keyed disk fallback.
// Returns the search-path root on success; empty means the file was unavailable.
std::filesystem::path MaterializeMapCacheFile(std::string_view path);
void ClearMapCache();

void InstallR2MapProvider();

} // namespace r2port
