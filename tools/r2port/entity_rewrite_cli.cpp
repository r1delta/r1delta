#include "engine/r2port/entity_rewrite.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;
using r2port::entity_rewrite::AnalyzeEntityLump;
using r2port::entity_rewrite::EntityLumpAnalysis;
using r2port::entity_rewrite::RewriteReport;
using r2port::entity_rewrite::HistogramEntry;
using r2port::entity_rewrite::RewriteEntityLump;
using r2port::entity_rewrite::RewriteOptions;
using r2port::entity_rewrite::SpawnMode;
using r2port::entity_rewrite::SpawnModeCounts;
using r2port::entity_rewrite::SpawnModeRule;
using r2port::entity_rewrite::ValidateEntityLump;

namespace {

constexpr std::array<std::string_view, 20> kR2Maps = {{
    "mp_black_water_canal", "mp_coliseum", "mp_coliseum_column", "mp_colony02",
    "mp_complex3", "mp_crashsite3", "mp_drydock", "mp_eden", "mp_forwardbase_kodai",
    "mp_glitch", "mp_grave", "mp_homestead", "mp_lf_deck", "mp_lf_meadow",
    "mp_lf_stacks", "mp_lf_township", "mp_lf_traffic", "mp_lf_uma", "mp_relic02",
    "mp_thaw"
}};

constexpr std::array<std::string_view, 5> kEntitySuffixes = {{
    "_env.ent", "_fx.ent", "_script.ent", "_snd.ent", "_spawn.ent"
}};

struct MapData {
    std::map<std::string, std::uint64_t> classnames;
    std::map<std::string, std::uint64_t> keys;
    RewriteReport rewrites;
    std::array<std::uint64_t, kEntitySuffixes.size()> filesBySuffix{};
    std::uint64_t entities = 0;
    std::uint64_t keyValuePairs = 0;
    std::uint64_t inputBytes = 0;
    std::uint64_t outputBytes = 0;
    std::uint64_t worldspawn = 0;
    std::uint64_t worldspawnSkyKeys = 0;
    std::uint64_t worldspawnFogKeys = 0;
    std::uint64_t fogControllers = 0;
    std::uint64_t lightEnvironments = 0;
    std::uint64_t lightEnvironmentVolumes = 0;
    std::uint64_t skyCameras = 0;
    std::uint64_t infoPlayerStarts = 0;
    std::uint64_t infoHardpoints = 0;
    std::uint64_t assaultPoints = 0;
    std::uint64_t triggerEntities = 0;
    std::uint64_t spawnZones = 0;
    std::uint64_t menuCameraTargets = 0;
    std::uint64_t menuSceneRefs = 0;
    std::uint64_t skyboxIntroCameras = 0;
    std::uint64_t skyboxLevelCameras = 0;
    std::uint64_t skyboxDayCameras = 0;
    std::uint64_t introCameraTargets = 0;
    std::uint64_t spaceNodeTargets = 0;
    std::uint64_t evacuationNodeTargets = 0;
    std::array<SpawnModeCounts, static_cast<std::size_t>(SpawnMode::Count)> spawnModes{};
};

struct BatchTotals {
    std::map<std::string, std::uint64_t> classnames;
    std::map<std::string, std::uint64_t> keys;
    std::uint64_t files = 0;
    std::uint64_t entities = 0;
    std::uint64_t keyValuePairs = 0;
    std::uint64_t inputBytes = 0;
    std::uint64_t outputBytes = 0;
    RewriteReport rewrites;
};

bool ReadFile(const fs::path& path, std::string& contents, std::string& error)
{
    std::error_code ec;
    const std::uintmax_t size = fs::file_size(path, ec);
    if (ec) {
        error = "cannot get file size: " + ec.message();
        return false;
    }
    if (size > static_cast<std::uintmax_t>(contents.max_size())) {
        error = "file is too large";
        return false;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "cannot open input file";
        return false;
    }
    contents.clear();
    contents.reserve(static_cast<std::size_t>(size));
    contents.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    if (!input.eof() && input.fail()) {
        error = "failed while reading input file";
        return false;
    }
    return true;
}

bool ReadInput(const std::string& path, std::string& contents, std::string& error)
{
    if (path != "-")
        return ReadFile(path, contents, error);
    contents.assign(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
    if (!std::cin.eof() && std::cin.fail()) {
        error = "failed while reading standard input";
        return false;
    }
    return true;
}

bool WriteFile(const fs::path& path, std::string_view contents, std::string& error)
{
    std::error_code ec;
    if (!path.parent_path().empty())
        fs::create_directories(path.parent_path(), ec);
    if (ec) {
        error = "cannot create output directory: " + ec.message();
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "cannot open output file";
        return false;
    }
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!output) {
        error = "failed while writing output file";
        return false;
    }
    return true;
}


void AddHistogram(std::map<std::string, std::uint64_t>& destination,
                  const std::vector<HistogramEntry>& source)
{
    for (const HistogramEntry& entry : source)
        destination[entry.name] += entry.occurrences;
}

void AddSpawnModes(std::array<SpawnModeCounts, static_cast<std::size_t>(SpawnMode::Count)>& destination,
                   const std::array<SpawnModeCounts, static_cast<std::size_t>(SpawnMode::Count)>& source)
{
    for (std::size_t index = 0; index < destination.size(); ++index) {
        destination[index].activeSpawnEntities += source[index].activeSpawnEntities;
        destination[index].human += source[index].human;
        destination[index].humanStart += source[index].humanStart;
        destination[index].titan += source[index].titan;
        destination[index].titanStart += source[index].titanStart;
        destination[index].flag += source[index].flag;
    }
}

void AddRewriteReport(RewriteReport& destination, const RewriteReport& source)
{
    destination.entitiesIn += source.entitiesIn;
    destination.entitiesOut += source.entitiesOut;
    destination.rewrittenValues += source.rewrittenValues;
    destination.renamedClassnames += source.renamedClassnames;
    destination.preservedZiplineMaterials += source.preservedZiplineMaterials;
    destination.renamedEdenSpaceNodes += source.renamedEdenSpaceNodes;
    destination.droppedScriptMoverLightweight += source.droppedScriptMoverLightweight;
    destination.droppedLightEnvironmentVolume += source.droppedLightEnvironmentVolume;
    destination.unknownConstructs += source.unknownConstructs;
    destination.renamedCtfTeamKeys += source.renamedCtfTeamKeys;
    AddSpawnModes(destination.spawnModes, source.spawnModes);
}

void AddAnalysis(MapData& destination,
                 BatchTotals& totals,
                 const EntityLumpAnalysis& source,
                 const RewriteReport& rewrites,
                 std::size_t suffixIndex,
                 std::size_t inputBytes,
                 std::size_t outputBytes)
{
    AddHistogram(destination.classnames, source.classnames);
    AddHistogram(destination.keys, source.keys);
    AddHistogram(totals.classnames, source.classnames);
    AddHistogram(totals.keys, source.keys);

    destination.entities += source.entities;
    destination.keyValuePairs += source.keyValuePairs;
    destination.inputBytes += inputBytes;
    destination.outputBytes += outputBytes;
    ++destination.filesBySuffix[suffixIndex];
    destination.worldspawn += source.features.worldspawn;
    destination.worldspawnSkyKeys += source.features.worldspawnSkyKeys;
    destination.worldspawnFogKeys += source.features.worldspawnFogKeys;
    destination.fogControllers += source.features.fogControllers;
    destination.lightEnvironments += source.features.lightEnvironments;
    destination.lightEnvironmentVolumes += source.features.lightEnvironmentVolumes;
    destination.skyCameras += source.features.skyCameras;
    destination.infoPlayerStarts += source.features.infoPlayerStarts;
    destination.infoHardpoints += source.features.infoHardpoints;
    destination.assaultPoints += source.features.assaultPoints;
    destination.triggerEntities += source.features.triggerEntities;
    destination.introCameraTargets += source.features.introCameraTargets;
    destination.spaceNodeTargets += source.features.spaceNodeTargets;
    destination.evacuationNodeTargets += source.features.evacuationNodeTargets;
    destination.spawnZones += source.features.spawnZones;
    destination.menuCameraTargets += source.features.menuCameraTargets;
    destination.menuSceneRefs += source.features.menuSceneRefs;
    destination.skyboxIntroCameras += source.features.skyboxIntroCameras;
    destination.skyboxLevelCameras += source.features.skyboxLevelCameras;
    destination.skyboxDayCameras += source.features.skyboxDayCameras;
    AddSpawnModes(destination.spawnModes, source.spawnModes);
    AddRewriteReport(destination.rewrites, rewrites);

    ++totals.files;
    totals.entities += source.entities;
    totals.keyValuePairs += source.keyValuePairs;
    totals.inputBytes += inputBytes;
    totals.outputBytes += outputBytes;
    AddRewriteReport(totals.rewrites, rewrites);
}

bool ParseEntityPath(const fs::path& path,
                     std::string& mapName,
                     std::size_t& suffixIndex)
{
    const std::string filename = path.filename().string();
    for (std::size_t index = 0; index < kEntitySuffixes.size(); ++index) {
        const std::string_view suffix = kEntitySuffixes[index];
        if (filename.size() <= suffix.size()
            || std::string_view(filename).substr(filename.size() - suffix.size()) != suffix)
            continue;
        mapName.assign(filename.data(), filename.size() - suffix.size());
        suffixIndex = index;
        return true;
    }
    return false;
}

bool IsAllowlistedMap(std::string_view mapName)
{
    for (std::string_view candidate : kR2Maps) {
        if (candidate == mapName)
            return true;
    }
    return false;
}

std::string HistogramText(const std::map<std::string, std::uint64_t>& histogram)
{
    std::ostringstream text;
    bool first = true;
    for (const auto& entry : histogram) {
        if (!first)
            text << ", ";
        first = false;
        text << '`' << entry.first << '`' << '=' << entry.second;
    }
    return first ? "(none)" : text.str();
}


std::string BuildBatchReport(const std::map<std::string, MapData>& maps,
                             const BatchTotals& totals)
{
    std::ostringstream text;
    text << "# R2 entity rewrite batch report\n\n";
    text << "Strict parser and rewrite passed for " << totals.files << " entity files across "
         << maps.size() << " maps. Entities: " << totals.entities << "; key/value pairs: "
         << totals.keyValuePairs << "; unique classnames: " << totals.classnames.size()
         << "; unique keys: " << totals.keys.size() << ". Each rewritten output was parsed again and a second rewrite was byte-identical.\n\n";
    text << "| Map | Files | Entities | Unique classes | Unique keys | Spawn zones | Rope material kept | Eden target | Dynamic downgrade | Dropped movers | Dropped light volumes | TDM human/start | FFA human/start | CTF titan-start | AT-tagged human/titans | PH humans | env fog | light env | sky cameras | menu camera/scene refs | spec cameras | skybox intro/level/day | evac nodes | space nodes |\n";
    text << "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n";
    for (std::string_view mapName : kR2Maps) {
        const auto found = maps.find(std::string(mapName));
        if (found == maps.end())
            continue;
        const MapData& map = found->second;
        const SpawnModeCounts& ctf = map.spawnModes[static_cast<std::size_t>(SpawnMode::CaptureTheFlag)];
        const SpawnModeCounts& at = map.spawnModes[static_cast<std::size_t>(SpawnMode::AttritionTitanMode)];
        const SpawnModeCounts& tdm = map.spawnModes[static_cast<std::size_t>(SpawnMode::TeamDeathmatch)];
        const SpawnModeCounts& ffa = map.spawnModes[static_cast<std::size_t>(SpawnMode::FreeForAll)];
        const SpawnModeCounts& ph = map.spawnModes[static_cast<std::size_t>(SpawnMode::PilotHunter)];
        text << "| " << mapName << " | "
             << map.filesBySuffix[0] + map.filesBySuffix[1] + map.filesBySuffix[2] + map.filesBySuffix[3] + map.filesBySuffix[4]
             << " | " << map.entities << " | " << map.classnames.size() << " | " << map.keys.size()
             << " | " << map.spawnZones
             << " | " << map.rewrites.preservedZiplineMaterials << " | " << map.rewrites.renamedEdenSpaceNodes
             << " | " << map.rewrites.renamedClassnames << " | " << map.rewrites.droppedScriptMoverLightweight
             << " | " << map.rewrites.droppedLightEnvironmentVolume
             << " | " << tdm.human << '/' << tdm.humanStart << " | " << ffa.human << '/' << ffa.humanStart << " | " << ctf.titanStart
             << " | " << at.human << '/' << at.titan << " | " << ph.human
             << " | " << map.fogControllers << " | " << map.lightEnvironments << " | " << map.skyCameras
             << " | " << map.menuCameraTargets << '/' << map.menuSceneRefs
             << " | " << map.introCameraTargets
             << " | " << map.skyboxIntroCameras << '/' << map.skyboxLevelCameras << '/' << map.skyboxDayCameras
             << " | " << map.evacuationNodeTargets << " | " << map.spaceNodeTargets << " |\n";
    }
    text << "\nThe 100 files are the five additive suffix entity lumps per map; they do not include BSP lump 0, so worldspawn, info_intermission and base-lump flag spawns are not counted here.\n";

    text << "\n## Aggregate classname histogram\n\n| Classname | Count |\n|---|---:|\n";
    for (const auto& entry : totals.classnames)
        text << "| `" << entry.first << "` | " << entry.second << " |\n";
    text << "\n## Aggregate key histogram\n\n| Key | Count |\n|---|---:|\n";
    for (const auto& entry : totals.keys)
        text << "| `" << entry.first << "` | " << entry.second << " |\n";

    text << "\n## Per-map histograms\n";
    for (std::string_view mapName : kR2Maps) {
        const auto found = maps.find(std::string(mapName));
        if (found == maps.end())
            continue;
        text << "\n### " << mapName << "\n\nEntities: " << found->second.entities << "; key/value pairs: " << found->second.keyValuePairs << ".\n\n";
        text << "Classnames: " << HistogramText(found->second.classnames) << "\n\n";
        text << "Keys: " << HistogramText(found->second.keys) << "\n";
    }

    text << "\n## Rewrite totals\n\n";
    text << "| Rule | Count |\n|---|---:|\n";
    text << "| `move_rope.RopeMaterial` kept as `cable/zipline.vmt` | " << totals.rewrites.preservedZiplineMaterials << " |\n";
    text << "| `info_spawnpoint_flag.teamnumber` -> `TeamNum` when missing | " << totals.rewrites.renamedCtfTeamKeys << " |\n";
    text << "| mp_eden info_target targetname spacenode -> intro_SpaceNode | " << totals.rewrites.renamedEdenSpaceNodes << " |\n";
    text << "| `prop_dynamic_lightweight` -> `prop_dynamic` | " << totals.rewrites.renamedClassnames << " |\n";
    text << "| dropped `script_mover_lightweight` records | " << totals.rewrites.droppedScriptMoverLightweight << " |\n";
    text << "| dropped `light_environment_volume` records | " << totals.rewrites.droppedLightEnvironmentVolume << " |\n";
    text << "| unknown syntax constructs | " << totals.rewrites.unknownConstructs << " |\n";
    text << "\nSpawn counting follows pinned R1 `GameModeRemove`: a spawn is excluded only when a recognized mode key is present with an empty value or `0`; missing mode keys remain active. Counts use CTF=`gamemode_ctf`, AT=`gamemode_at`, FFA=`gamemode_ffa`, TDM=`gamemode_tdm`, PH=`gamemode_tdm`. R1 asks for `gamemode_atdm`, so AT counts are R2-tagged entities, not R1 AT pruning. The five suffix files omit BSP lump 0; base-lump CTF flags are not included. When passed BSP lump 0, the library renames `teamnumber` to `TeamNum` only on `info_spawnpoint_flag` records lacking `TeamNum`. Counts are static, not runtime spawn tests. Zipline keys are preserved because R1 zipline clients force `cable/zipline.vmt` for Zipline-flagged ropes.\n";
    return text.str();
}

int RunValidate(const std::vector<std::string>& args)
{
    if (args.size() < 2) {
        std::cerr << "usage: entity_rewrite_cli --validate <input-file|-> [--strict]\n";
        return 2;
    }
    const std::string inputPath = args[1];
    bool strict = false;
    for (std::size_t index = 2; index < args.size(); ++index) {
        if (args[index] == "--strict")
            strict = true;
        else {
            std::cerr << "unknown option: " << args[index] << '\n';
            return 2;
        }
    }
    std::string contents;
    std::string error;
    if (!ReadInput(inputPath, contents, error)) {
        std::cerr << inputPath << ": " << error << '\n';
        return 1;
    }
    if (!ValidateEntityLump(contents, strict, &error)) {
        std::cerr << inputPath << ": " << error << '\n';
        return 1;
    }
    EntityLumpAnalysis analysis;
    if (!AnalyzeEntityLump(contents, analysis, strict, &error)) {
        std::cerr << inputPath << ": " << error << '\n';
        return 1;
    }
    std::cout << "valid entities=" << analysis.entities
              << " key-value-pairs=" << analysis.keyValuePairs
              << " classnames=" << analysis.classnames.size()
              << " keys=" << analysis.keys.size() << '\n';
    return 0;
}

int RunRewrite(const std::vector<std::string>& args)
{
    if (args.size() < 3) {
        std::cerr << "usage: entity_rewrite_cli --rewrite <map-name> <input-file|-> [output-file] [--strict]\n";
        return 2;
    }
    const std::string mapName = args[1];
    const std::string inputPath = args[2];
    std::string outputPath;
    bool strict = false;
    for (std::size_t index = 3; index < args.size(); ++index) {
        if (args[index] == "--strict")
            strict = true;
        else if (outputPath.empty())
            outputPath = args[index];
        else {
            std::cerr << "unknown option: " << args[index] << '\n';
            return 2;
        }
    }

    std::string input;
    std::string error;
    if (!ReadInput(inputPath, input, error)) {
        std::cerr << inputPath << ": " << error << '\n';
        return 1;
    }
    RewriteReport report;
    std::string output;
    try {
        output = RewriteEntityLump(input, {mapName, strict}, &report);
    } catch (const r2port::entity_rewrite::Error& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
    if (!ValidateEntityLump(output, strict, &error)) {
        std::cerr << "rewritten output: " << error << '\n';
        return 1;
    }
    if (outputPath.empty()) {
        std::cout.write(output.data(), static_cast<std::streamsize>(output.size()));
    } else if (!WriteFile(outputPath, output, error)) {
        std::cerr << outputPath << ": " << error << '\n';
        return 1;
    }
    std::cerr << "entities=" << report.entitiesIn << "->" << report.entitiesOut
              << " class-renames=" << report.renamedClassnames
              << " value-rewrites=" << report.rewrittenValues
              << " zipline-materials-preserved=" << report.preservedZiplineMaterials
              << " eden-targets=" << report.renamedEdenSpaceNodes
              << " dropped-movers=" << report.droppedScriptMoverLightweight
              << " ctf-team-keys=" << report.renamedCtfTeamKeys
              << " dropped-light-volumes=" << report.droppedLightEnvironmentVolume
              << " unknown-constructs=" << report.unknownConstructs << '\n';
    return 0;
}

int RunBatch(const std::vector<std::string>& args)
{
    bool strict = false;
    std::string reportPath;
    std::vector<fs::path> roots;
    for (std::size_t index = 1; index < args.size(); ++index) {
        if (args[index] == "--strict") {
            strict = true;
        } else if (args[index] == "--report") {
            if (index + 1 >= args.size()) {
                std::cerr << "--report requires a file path\n";
                return 2;
            }
            reportPath = args[++index];
        } else if (std::string_view(args[index]).substr(0, 2) == "--") {
            std::cerr << "unknown option: " << args[index] << '\n';
            return 2;
        } else {
            roots.emplace_back(args[index]);
        }
    }
    if (roots.empty()) {
        std::cerr << "usage: entity_rewrite_cli --batch [--strict] [--report file] <root> [root ...]\n";
        return 2;
    }

    std::map<std::string, MapData> maps;
    for (std::string_view mapName : kR2Maps)
        maps.emplace(std::string(mapName), MapData{});
    BatchTotals totals;
    std::uint64_t foundEntityFiles = 0;

    for (const fs::path& root : roots) {
        std::error_code ec;
        if (!fs::exists(root, ec) || ec) {
            std::cerr << root.string() << ": input root does not exist\n";
            return 1;
        }
        fs::recursive_directory_iterator iterator(root, fs::directory_options::skip_permission_denied, ec);
        const fs::recursive_directory_iterator end;
        if (ec) {
            std::cerr << root.string() << ": cannot enumerate input root: " << ec.message() << '\n';
            return 1;
        }
        for (; iterator != end; iterator.increment(ec)) {
            if (ec) {
                std::cerr << root.string() << ": directory enumeration failed: " << ec.message() << '\n';
                return 1;
            }
            if (!iterator->is_regular_file(ec) || ec)
                continue;
            const fs::path path = iterator->path();
            if (path.extension() != ".ent")
                continue;
            std::string mapName;
            std::size_t suffixIndex = 0;
            if (!ParseEntityPath(path, mapName, suffixIndex) || !IsAllowlistedMap(mapName)) {
                std::cerr << path.string() << ": entity file is not an allowlisted <map>_<suffix>.ent\n";
                return 1;
            }

            std::string input;
            std::string error;
            if (!ReadFile(path, input, error)) {
                std::cerr << path.string() << ": " << error << '\n';
                return 1;
            }
            EntityLumpAnalysis analysis;
            if (!AnalyzeEntityLump(input, analysis, strict, &error)) {
                std::cerr << path.string() << ": " << error << '\n';
                return 1;
            }

            RewriteReport rewriteReport;
            std::string output;
            try {
                output = RewriteEntityLump(input, {mapName, strict}, &rewriteReport);
            } catch (const r2port::entity_rewrite::Error& exception) {
                std::cerr << path.string() << ": " << exception.what() << '\n';
                return 1;
            }
            if (!ValidateEntityLump(output, strict, &error)) {
                std::cerr << path.string() << ": rewritten output: " << error << '\n';
                return 1;
            }
            const std::string secondOutput = RewriteEntityLump(output, {mapName, strict}, nullptr);
            if (secondOutput != output) {
                std::cerr << path.string() << ": rewrite is not idempotent\n";
                return 1;
            }

            MapData& map = maps.at(mapName);
            if (map.filesBySuffix[suffixIndex] != 0) {
                std::cerr << path.string() << ": duplicate entity suffix for " << mapName << '\n';
                return 1;
            }
            AddAnalysis(map, totals, analysis, rewriteReport, suffixIndex, input.size(), output.size());
            ++foundEntityFiles;
        }
    }

    for (const auto& entry : maps) {
        for (std::size_t suffixIndex = 0; suffixIndex < kEntitySuffixes.size(); ++suffixIndex) {
            if (entry.second.filesBySuffix[suffixIndex] != 1) {
                std::cerr << entry.first << ": expected exactly one file ending in "
                          << kEntitySuffixes[suffixIndex] << ", found "
                          << entry.second.filesBySuffix[suffixIndex] << '\n';
                return 1;
            }
        }
    }
    if (foundEntityFiles != static_cast<std::uint64_t>(kR2Maps.size() * kEntitySuffixes.size())) {
        std::cerr << "expected " << kR2Maps.size() * kEntitySuffixes.size()
                  << " files, found " << foundEntityFiles << '\n';
        return 1;
    }

    const std::string report = BuildBatchReport(maps, totals);
    if (reportPath.empty()) {
        std::cout << report;
    } else {
        std::string error;
        if (!WriteFile(reportPath, report, error)) {
            std::cerr << reportPath << ": " << error << '\n';
            return 1;
        }
        std::cout << "wrote " << reportPath << '\n';
    }
    return 0;
}

void PrintUsage()
{
    std::cerr << "usage:\n"
              << "  entity_rewrite_cli --validate <input-file|-> [--strict]\n"
              << "  entity_rewrite_cli --rewrite <map-name> <input-file|-> [output-file] [--strict]\n"
              << "  entity_rewrite_cli --batch [--strict] [--report file] <root> [root ...]\n";
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        PrintUsage();
        return 2;
    }
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc));
    for (int index = 1; index < argc; ++index)
        args.emplace_back(argv[index]);

    if (args[0] == "--validate")
        return RunValidate(args);
    if (args[0] == "--rewrite")
        return RunRewrite(args);
    if (args[0] == "--batch")
        return RunBatch(args);

    PrintUsage();
    return 2;
}
