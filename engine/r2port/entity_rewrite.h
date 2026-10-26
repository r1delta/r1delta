#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace r2port::entity_rewrite {

// Bump whenever RewriteEntityLump can produce different output for the same input. The R2 map
// provider folds it into its on-disk cache fingerprint so cached entity files and BSPs are rebuilt.
inline constexpr std::uint32_t kRewriteVersion = 1;

enum class SpawnMode : std::size_t {
    CaptureTheFlag,
    AttritionTitanMode,
    FreeForAll,
    TeamDeathmatch,
    PilotHunter,
    Count
};

struct SpawnModeRule {
    std::string_view name;
    std::string_view gameModeKey;
    bool countHuman;
    bool countHumanStart;
    bool countTitan;
    bool countTitanStart;
    bool countFlag;
};

struct SpawnModeCounts {
    std::uint64_t activeSpawnEntities = 0;
    std::uint64_t human = 0;
    std::uint64_t humanStart = 0;
    std::uint64_t titan = 0;
    std::uint64_t titanStart = 0;
    std::uint64_t flag = 0;
};

const SpawnModeRule& GetSpawnModeRule(SpawnMode mode) noexcept;

struct RewriteOptions {
    std::string_view mapName;
    bool strict = false;
};

struct RewriteReport {
    std::uint64_t entitiesIn = 0;
    std::uint64_t entitiesOut = 0;
    std::uint64_t renamedClassnames = 0;
    std::uint64_t rewrittenValues = 0;
    std::uint64_t preservedZiplineMaterials = 0;
    std::uint64_t renamedEdenSpaceNodes = 0;
    std::uint64_t renamedCtfTeamKeys = 0;
    std::uint64_t droppedScriptMoverLightweight = 0;
    std::uint64_t droppedLightEnvironmentVolume = 0;
    std::uint64_t unknownConstructs = 0;
    std::array<SpawnModeCounts, static_cast<std::size_t>(SpawnMode::Count)> spawnModes{};
};

struct HistogramEntry {
    std::string name;
    std::uint64_t occurrences = 0;
};

struct MapFeatures {
    std::uint64_t worldspawn = 0;
    std::uint64_t worldspawnSkyKeys = 0;
    std::uint64_t worldspawnFogKeys = 0;
    std::uint64_t fogControllers = 0;
    std::uint64_t lightEnvironments = 0;
    std::uint64_t lightEnvironmentVolumes = 0;
    std::uint64_t skyCameras = 0;
    std::uint64_t spawnZones = 0;
    std::uint64_t menuCameraTargets = 0;
    std::uint64_t menuSceneRefs = 0;
    std::uint64_t skyboxIntroCameras = 0;
    std::uint64_t skyboxLevelCameras = 0;
    std::uint64_t skyboxDayCameras = 0;
    std::uint64_t infoPlayerStarts = 0;
    std::uint64_t infoHardpoints = 0;
    std::uint64_t assaultPoints = 0;
    std::uint64_t triggerEntities = 0;
    std::uint64_t introCameraTargets = 0;
    std::uint64_t spaceNodeTargets = 0;
    std::uint64_t evacuationNodeTargets = 0;
};

struct EntityLumpAnalysis {
    std::uint64_t entities = 0;
    std::uint64_t keyValuePairs = 0;
    std::vector<HistogramEntry> classnames;
    std::vector<HistogramEntry> keys;
    std::array<SpawnModeCounts, static_cast<std::size_t>(SpawnMode::Count)> spawnModes{};
    MapFeatures features;
};

struct Error : std::runtime_error {
    Error(std::size_t offset, const std::string& message);

    std::size_t offset() const noexcept;

private:
    std::size_t m_offset;
};

// Parses the Valve entity-lump text used by both R2 ENTITIES01 files and R1 .ent files.
// Unknown keys and classname values are opaque and preserved. Strict mode rejects
// non-quoted tokens, duplicate rewrite-sensitive keys, and malformed block structure.
std::string RewriteEntityLump(std::string_view input,
                              const RewriteOptions& options,
                              RewriteReport* report = nullptr);

bool ValidateEntityLump(std::string_view input,
                        bool strict = true,
                        std::string* error = nullptr);

// Offline analysis helper for the CLI. Histograms contain one entry per distinct
// classname/key, sorted lexicographically; no per-key data is exposed to runtime callers.
bool AnalyzeEntityLump(std::string_view input,
                       EntityLumpAnalysis& analysis,
                       bool strict = true,
                       std::string* error = nullptr);

} // namespace r2port::entity_rewrite
