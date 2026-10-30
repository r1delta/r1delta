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
    // Optional file override for formats bound to the source R2 BSP hash or live map state (last argument: map version).
    virtual FileBytes LoadFileOverride(
        std::string_view, std::string_view,
        const BspSha256&, std::int32_t)
    {
        return {};
    }
};

// Patch files live under <gamedir>\\r2port\\patches.
bool RegisterMapPatchSource(std::shared_ptr<MapPatchSource> source);
std::filesystem::path GetR2MapPatchDirectory();
std::filesystem::path GetR2InstallDirectory();
// The r1delta game directory that holds cfg/ (empty when it cannot be found).
std::filesystem::path GetR2GameContentDirectory();
// True for the allowlisted R2-only maps this provider serves.
bool IsR2PortMap(std::string_view mapName);
// Legacy dedicated engine only: writes the generated scripts/r2port_sounds.txt as a loose file in the game directory (or removes a
// stale one when none is generated) before the engine loads it. True when the file is in place.
bool PlaceGeneratedSoundScript();
// Engines whose file lookup reaches the provider fallback (R1O fake dedicated): the alias script is served from memory, so a loose
// scripts/r2port_sounds.txt left in the game directory by an earlier run (a legacy dedicated server places one) is deleted before the
// engine loads the script; a loose file would win over the provider and inject whatever it holds as ordinary aliases. True when no
// loose file remains.
bool RemoveStaleGeneratedSoundScript();
// True once after the provider has served an AI graph package to the engine; cleared by the call. The server's AIN
// validation hook uses it to accept a packaged graph whose entity CRC the engine cannot match (developer > 0).
bool ConsumeServedAinPackage();
void ObserveMapFilePath(std::string_view path);
// Materializes one allowlisted BSP or entity into the keyed disk fallback.
// Returns the search-path root on success; empty means the file was unavailable.
std::filesystem::path MaterializeMapCacheFile(std::string_view path);
// True when `path` names the copy MaterializeMapCacheFile already wrote for the current converter fingerprint. That copy is
// provider output, not a native game file: it must never suppress the provider Read that starts the map's material and
// model prefetch.
bool IsMaterializedMapCopy(std::string_view path);
void ClearMapCache();

void InstallR2MapProvider();

} // namespace r2port
