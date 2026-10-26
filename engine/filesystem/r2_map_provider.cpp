#include "engine/logging/logging.h"
#include "r2_map_provider.h"
#include "r2_material_provider.h"
#include "r1_map_pack_material_provider.h"
#include "engine/core/factory.h"
#include "engine/core/core.h"
#include "engine/cvar.h"
#include "r2_map_patch_source.h"

#include "engine/filesystem/r1o_vpk.h"
#include "engine/r2port/bspdown/bspdown.hpp"
#include "p2p/p2p_identity.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <ShlObj.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <limits>
#include <chrono>
#include <fstream>
#include <iterator>
#include <condition_variable>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace r2port {
namespace {

constexpr std::array<const char*, 20> kR2MapAllowlist = {
    "mp_black_water_canal",
    "mp_coliseum",
    "mp_coliseum_column",
    "mp_colony02",
    "mp_complex3",
    "mp_crashsite3",
    "mp_drydock",
    "mp_eden",
    "mp_forwardbase_kodai",
    "mp_glitch",
    "mp_grave",
    "mp_homestead",
    "mp_lf_deck",
    "mp_lf_meadow",
    "mp_lf_stacks",
    "mp_lf_township",
    "mp_lf_traffic",
    "mp_lf_uma",
    "mp_relic02",
    "mp_thaw",
};

constexpr std::array<const char*, 5> kEntitySuffixes = {
    "_env.ent",
    "_fx.ent",
    "_script.ent",
    "_snd.ent",
    "_spawn.ent",
};

constexpr size_t kBspHeaderSize = 0x810;
constexpr uint32_t kBspLumpCount = 128;
constexpr uint32_t kGameLump = 0x23;

struct BspLumpHeader {
    uint32_t offset;
    uint32_t length;
    uint32_t version;
    uint32_t fourCC;
};
static_assert(sizeof(BspLumpHeader) == 16);

struct MapState {
    explicit MapState(std::string name, std::filesystem::path package)
        : mapName(std::move(name)), directoryVpk(std::move(package))
    {
    }

    std::string mapName;
    std::filesystem::path directoryVpk;
    std::mutex sourceMutex;
    std::condition_variable sourceReady;
    bool sourceLoading{};
    bool sourceAttempted{};
    FileBytes sourceBsp;
    bool hashReady{};
    BspSha256 sourceHash{};
    std::mutex conversionMutex;
    std::condition_variable conversionReady;
    bool conversionLoading{};
    bool conversionFailed{};
    std::weak_ptr<const std::vector<uint8_t>> convertedBsp;
    std::mutex patchMutex;
    uint64_t patchGeneration{};
    BspSha256 patchSourceHash{};
    bool patchLoaded{};
    std::shared_ptr<const MapPatch> patch;
    std::mutex filePatchMutex;
    bool filePatchLoaded{};
    uint64_t filePatchGeneration{};
    BspSha256 filePatchSourceHash{};
    BspSha256 filePatchConvertedHash{};
    std::int32_t filePatchMapVersion{};
    std::int32_t filePatchDeveloperValue{};
    FileBytes aiGraphFile;
};

std::once_flag g_installDiscoveryOnce;
std::filesystem::path g_r2InstallDirectory;
std::mutex g_mapStatesMutex;
std::unordered_map<std::string, std::shared_ptr<MapState>> g_mapStates;
std::mutex g_patchSourcesMutex;
std::vector<std::shared_ptr<MapPatchSource>> g_patchSources;
uint64_t g_patchSourcesGeneration{};
std::mutex g_loggedMapsMutex;
std::unordered_set<std::string> g_loggedMaps;
std::once_flag g_providerInstallOnce;

std::string LowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool IsRegularFile(const std::filesystem::path& path)
{
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && !error;
}

std::string ReadRegistryString(HKEY root, const char* keyPath, const char* valueName, REGSAM view)
{
    HKEY key{};
    if (RegOpenKeyExA(root, keyPath, 0, KEY_READ | view, &key) != ERROR_SUCCESS)
        return {};

    DWORD type{};
    DWORD byteCount{};
    LONG status = RegQueryValueExA(key, valueName, nullptr, &type, nullptr, &byteCount);
    if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || byteCount < 2) {
        RegCloseKey(key);
        return {};
    }

    std::vector<char> value(byteCount + 1, '\0');
    status = RegQueryValueExA(key, valueName, nullptr, &type,
        reinterpret_cast<BYTE*>(value.data()), &byteCount);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS)
        return {};

    std::string result(value.data());
    if (type == REG_EXPAND_SZ) {
        std::vector<char> expanded(32768, '\0');
        const DWORD written = ExpandEnvironmentStringsA(result.c_str(), expanded.data(),
            static_cast<DWORD>(expanded.size()));
        if (!written || written >= expanded.size())
            return {};
        result.assign(expanded.data());
    }
    return result;
}

std::vector<std::string> ReadVdfTokens(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return {};
    const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    std::vector<std::string> tokens;
    for (size_t i = 0; i < contents.size();) {
        if (contents[i] != '"') {
            ++i;
            continue;
        }
        ++i;
        std::string token;
        while (i < contents.size() && contents[i] != '"') {
            if (contents[i] == '\\' && i + 1 < contents.size()) {
                const char next = contents[i + 1];
                if (next == '\\' || next == '"') {
                    token.push_back(next);
                    i += 2;
                    continue;
                }
            }
            token.push_back(contents[i++]);
        }
        if (i < contents.size())
            ++i;
        tokens.push_back(std::move(token));
    }
    return tokens;
}

void AddCandidate(std::vector<std::filesystem::path>& candidates,
    std::unordered_set<std::string>& seen, const std::filesystem::path& path)
{
    if (path.empty())
        return;
    std::error_code error;
    const std::filesystem::path normalized = std::filesystem::absolute(path, error).lexically_normal();
    const std::string value = LowerAscii((error ? path : normalized).string());
    if (seen.insert(value).second)
        candidates.emplace_back(error ? path : normalized);
}

bool HasCommonClientVpkDirectory(const std::filesystem::path& vpkDirectory)
{
    constexpr std::string_view suffix = "client_mp_common.bsp.pak000_dir.vpk";
    std::error_code error;
    std::filesystem::directory_iterator item(vpkDirectory, error);
    const std::filesystem::directory_iterator end;
    for (; !error && item != end; item.increment(error)) {
        if (!IsRegularFile(item->path()))
            continue;
        const std::string filename = LowerAscii(item->path().filename().string());
        if (filename.size() >= suffix.size()
            && filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) == 0)
            return true;
    }
    return false;
}
bool IsValidR2Install(const std::filesystem::path& root)
{
    if (root.empty()
        || !IsRegularFile(root / "bin" / "x64_retail" / "rtech_game.dll")
        || !HasCommonClientVpkDirectory(root / "vpk")
        || !IsRegularFile(root / "vpk" / "client_mp_common.bsp.pak000_000.vpk")
        || IsRegularFile(root / "r1" / "gameinfo.txt"))
        return false;

    std::error_code error;
    const std::filesystem::path paks = root / "r2" / "paks" / "Win64";
    if (!std::filesystem::is_directory(paks, error) || error)
        return false;
    std::filesystem::directory_iterator item(paks, error);
    const std::filesystem::directory_iterator end;
    for (; !error && item != end; item.increment(error)) {
        if (item->is_regular_file(error) && !error
            && LowerAscii(item->path().extension().string()) == ".rpak")
            return true;
        error.clear();
    }
    return false;
}

std::filesystem::path DiscoverR2Install()
{
    char environmentPath[MAX_PATH]{};
    const DWORD environmentLength = GetEnvironmentVariableA(
        "R1DELTA_TITANFALL2_PATH", environmentPath, static_cast<DWORD>(std::size(environmentPath)));
    if (environmentLength && environmentLength < std::size(environmentPath)
        && IsValidR2Install(environmentPath))
        return std::filesystem::path(environmentPath);

    std::vector<std::filesystem::path> candidates;
    std::unordered_set<std::string> seen;
    for (const char* keyPath : {
        "SOFTWARE\\Respawn\\Titanfall2",
        "SOFTWARE\\WOW6432Node\\Respawn\\Titanfall2",
    }) {
        for (REGSAM view : { KEY_WOW64_64KEY, KEY_WOW64_32KEY }) {
            const std::string install = ReadRegistryString(HKEY_LOCAL_MACHINE, keyPath, "Install Dir", view);
            if (!install.empty())
                AddCandidate(candidates, seen, install);
        }
    }

    std::vector<std::filesystem::path> steamRoots;
    for (const auto& registryLocation : {
        std::pair{ HKEY_LOCAL_MACHINE, "SOFTWARE\\Valve\\Steam" },
        std::pair{ HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Valve\\Steam" },
        std::pair{ HKEY_CURRENT_USER, "Software\\Valve\\Steam" },
    }) {
        for (const char* valueName : { "InstallPath", "SteamPath" }) {
            for (REGSAM view : { KEY_WOW64_64KEY, KEY_WOW64_32KEY }) {
                const std::string steam = ReadRegistryString(
                    registryLocation.first, registryLocation.second, valueName, view);
                if (!steam.empty()) {
                    steamRoots.emplace_back(steam);
                    AddCandidate(candidates, seen,
                        std::filesystem::path(steam) / "steamapps" / "common" / "Titanfall2");
                }
            }
        }
    }

    std::vector<std::filesystem::path> steamLibraries;
    for (const auto& steam : steamRoots) {
        steamLibraries.push_back(steam);
        const auto libraries = ReadVdfTokens(steam / "steamapps" / "libraryfolders.vdf");
        for (size_t i = 0; i + 1 < libraries.size(); ++i) {
            if (libraries[i] == "path") {
                steamLibraries.emplace_back(libraries[i + 1]);
                ++i;
            }
            else if (!libraries[i].empty() && std::all_of(libraries[i].begin(), libraries[i].end(),
                [](unsigned char c) { return std::isdigit(c) != 0; })
                && libraries[i + 1].size() > 2 && libraries[i + 1][1] == ':') {
                steamLibraries.emplace_back(libraries[i + 1]);
                ++i;
            }
        }
    }

    for (const auto& library : steamLibraries) {
        const std::filesystem::path steamApps = library / "steamapps";
        AddCandidate(candidates, seen, steamApps / "common" / "Titanfall2");
        const auto manifest = ReadVdfTokens(steamApps / "appmanifest_1237970.acf");
        for (size_t i = 0; i + 1 < manifest.size(); ++i) {
            if (manifest[i] == "installdir") {
                AddCandidate(candidates, seen, steamApps / "common" / manifest[i + 1]);
                break;
            }
        }
    }

    const DWORD drives = GetLogicalDrives();
    constexpr std::array<const char*, 8> knownParents = {
        "",
        "Games",
        "Games\\EA",
        "EA Games",
        "Program Files\\EA Games",
        "Program Files (x86)\\Origin Games",
        "Program Files (x86)\\Steam\\steamapps\\common",
        "SteamLibrary\\steamapps\\common",
    };
    constexpr std::array<const char*, 2> installFolders = {
        "Titanfall2",
        "Titanfall 2",
    };
    for (unsigned int index = 0; index < 26; ++index) {
        if ((drives & (1u << index)) == 0)
            continue;
        char rootName[] = { static_cast<char>('A' + index), ':', '\\', '\0' };
        const UINT driveType = GetDriveTypeA(rootName);
        if (driveType != DRIVE_FIXED && driveType != DRIVE_REMOVABLE)
            continue;
        if (GetFileAttributesA(rootName) == INVALID_FILE_ATTRIBUTES)
            continue;

        const std::filesystem::path drive(rootName);
        for (const char* parent : knownParents) {
            for (const char* folder : installFolders)
                AddCandidate(candidates, seen, drive / parent / folder);
        }
    }

    for (const auto& candidate : candidates) {
        if (IsValidR2Install(candidate))
            return candidate;
    }
    return {};
}

const std::filesystem::path& R2InstallDirectory()
{
    std::call_once(g_installDiscoveryOnce, []() {
        g_r2InstallDirectory = DiscoverR2Install();
        char buffer[MAX_PATH * 2]{};
        _snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
            "R1Delta: R2 map provider install discovery %s%s\n",
            g_r2InstallDirectory.empty() ? "found no valid install" : "found ",
            g_r2InstallDirectory.empty() ? "" : g_r2InstallDirectory.string().c_str());
        Msg("%s", buffer);
        OutputDebugStringA(buffer);
    });
    return g_r2InstallDirectory;
}

bool IsAllowlistedMap(std::string_view mapName)
{
    return std::any_of(kR2MapAllowlist.begin(), kR2MapAllowlist.end(), [mapName](const char* allowed) {
        return mapName == allowed;
    });
}

struct MapRequest {
    std::string_view mapName;
    enum class Kind { Bsp, Entity, AiGraph } kind;
    std::string_view packagePath;
};

std::optional<MapRequest> ParseMapRequest(std::string_view path)
{
    constexpr std::string_view mapsPrefix = "maps/";
    constexpr std::string_view graphsPrefix = "maps/graphs/";
    if (path.substr(0, graphsPrefix.size()) == graphsPrefix && path.size() > graphsPrefix.size()
        && path.substr(path.size() - 4) == ".ain") {
        const std::string_view mapName = path.substr(graphsPrefix.size(), path.size() - graphsPrefix.size() - 4);
        if (IsAllowlistedMap(mapName))
            return MapRequest{ mapName, MapRequest::Kind::AiGraph, path };
        return std::nullopt;
    }

    if (path.substr(0, mapsPrefix.size()) != mapsPrefix)
        return std::nullopt;
    const std::string_view fileName = path.substr(mapsPrefix.size());
    for (const char* mapName : kR2MapAllowlist) {
        const std::string_view map(mapName);
        if (fileName.size() == map.size() + 4
            && fileName.substr(0, map.size()) == map
            && fileName.substr(map.size()) == ".bsp")
            return MapRequest{ map, MapRequest::Kind::Bsp, path };
        for (const char* suffix : kEntitySuffixes) {
            const std::string_view suffixView(suffix);
            if (fileName.size() == map.size() + suffixView.size()
                && fileName.substr(0, map.size()) == map
                && fileName.substr(map.size()) == suffixView)
                return MapRequest{ map, MapRequest::Kind::Entity, path };
        }
    }
    return std::nullopt;
}

std::filesystem::path DirectoryVpkForMap(std::string_view mapName)
{
    const auto& install = R2InstallDirectory();
    if (install.empty())
        return {};
    return install / "vpk" / ("englishclient_" + std::string(mapName) + ".bsp.pak000_dir.vpk");
}

std::shared_ptr<MapState> GetMapState(std::string_view mapName)
{
    const std::string key(mapName);
    std::lock_guard<std::mutex> lock(g_mapStatesMutex);
    const auto found = g_mapStates.find(key);
    if (found != g_mapStates.end())
        return found->second;
    auto state = std::make_shared<MapState>(key, DirectoryVpkForMap(key));
    g_mapStates.emplace(key, state);
    return state;
}

bool EnsureSourceBsp(const std::shared_ptr<MapState>& state)
{
    std::unique_lock<std::mutex> lock(state->sourceMutex);
    while (state->sourceLoading)
        state->sourceReady.wait(lock);
    if (state->sourceBsp)
        return true;
    if (state->sourceAttempted && !state->hashReady)
        return false;

    const bool hadHash = state->hashReady;
    state->sourceLoading = true;
    lock.unlock();

    FileBytes source;
    try {
        if (!state->directoryVpk.empty() && IsRegularFile(state->directoryVpk)) {
            const std::string package = state->directoryVpk.string();
            const std::string path = "maps/" + state->mapName + ".bsp";
            source = R1OVPK_ReadDirectoryVPKFile(package.c_str(), path.c_str());
        }
    }
    catch (...) {
    }

    BspSha256 hash{};
    if (source)
        hash = p2p::Sha256(source->data(), source->size());

    lock.lock();
    if (source) {
        state->sourceBsp = std::move(source);
        state->sourceHash = hash;
        state->hashReady = true;
    }
    else if (!hadHash) {
        state->hashReady = false;
    }
    state->sourceAttempted = true;
    state->sourceLoading = false;
    const bool loaded = static_cast<bool>(state->sourceBsp);
    lock.unlock();
    state->sourceReady.notify_all();
    return loaded;
}

bool EnsureSourceHash(const std::shared_ptr<MapState>& state)
{
    {
        std::unique_lock<std::mutex> lock(state->sourceMutex);
        while (state->sourceLoading)
            state->sourceReady.wait(lock);
        if (state->hashReady)
            return true;
    }
    return EnsureSourceBsp(state);
}
bool HavePatchSources()
{
    std::lock_guard<std::mutex> lock(g_patchSourcesMutex);
    return !g_patchSources.empty();
}


std::pair<std::vector<std::shared_ptr<MapPatchSource>>, uint64_t> PatchSourcesSnapshot()
{
    std::lock_guard<std::mutex> lock(g_patchSourcesMutex);
    return { g_patchSources, g_patchSourcesGeneration };
}

std::shared_ptr<const MapPatch> GetMatchingPatch(
    const std::shared_ptr<MapState>& state, const BspSha256& sourceHash)
{
    auto [sources, generation] = PatchSourcesSnapshot();
    std::lock_guard<std::mutex> lock(state->patchMutex);
    if (state->patchLoaded && state->patchGeneration == generation
        && state->patchSourceHash == sourceHash)
        return state->patch;

    state->patchLoaded = true;
    state->patchGeneration = generation;
    state->patchSourceHash = sourceHash;
    state->patch.reset();
    for (const auto& source : sources) {
        try {
            auto patch = source->LoadPatch(state->mapName, sourceHash);
            if (patch && patch->sourceBspSha256 == sourceHash) {
                state->patch = std::move(patch);
                break;
            }
        }
        catch (...) {
        }
    }
    return state->patch;
}

bool ReplacePatchedLumps(
    const std::vector<uint8_t>& input,
    const MapPatch& patch,
    std::vector<uint8_t>& output)
{
    if (input.size() < kBspHeaderSize)
        return false;

    std::array<const FileBytes*, kBspLumpCount> replacements{};
    bool anyReplacement = false;
    for (const auto& [lumpId, data] : patch.lumpOverrides) {
        if ((lumpId == 0x1D || lumpId == 0x46) && data && lumpId < replacements.size()) {
            replacements[lumpId] = &data;
            anyReplacement = true;
        }
    }
    if (!anyReplacement)
        return false;

    std::array<BspLumpHeader, kBspLumpCount> oldHeaders{};
    std::array<BspLumpHeader, kBspLumpCount> newHeaders{};
    for (uint32_t lump = 0; lump < kBspLumpCount; ++lump) {
        std::memcpy(&oldHeaders[lump], input.data() + 16 + lump * sizeof(BspLumpHeader), sizeof(BspLumpHeader));
        const auto& header = oldHeaders[lump];
        if (header.offset > input.size() || header.length > input.size() - header.offset)
            return false;
    }

    output.assign(input.begin(), input.begin() + kBspHeaderSize);
    for (uint32_t lump = 0; lump < kBspLumpCount; ++lump) {
        const BspLumpHeader& oldHeader = oldHeaders[lump];
        const FileBytes* replacement = replacements[lump];
        const bool present = replacement || oldHeader.offset || oldHeader.length;
        if (!present) {
            newHeaders[lump] = {};
            continue;
        }

        while ((output.size() & 3u) != 0)
            output.push_back(0);
        if (output.size() > std::numeric_limits<uint32_t>::max())
            return false;
        const uint64_t newLength = replacement ? (*replacement)->size() : oldHeader.length;
        if (newLength > std::numeric_limits<uint32_t>::max()
            || newLength > std::numeric_limits<size_t>::max() - output.size())
            return false;

        BspLumpHeader& newHeader = newHeaders[lump];
        newHeader.offset = static_cast<uint32_t>(output.size());
        newHeader.length = static_cast<uint32_t>(newLength);
        newHeader.version = oldHeader.version;
        newHeader.fourCC = oldHeader.fourCC;
        if (replacement) {
            output.insert(output.end(), (*replacement)->begin(), (*replacement)->end());
        }
        else if (oldHeader.length) {
            output.insert(output.end(), input.begin() + oldHeader.offset,
                input.begin() + oldHeader.offset + oldHeader.length);
        }
    }

    if (output.size() > std::numeric_limits<uint32_t>::max())
        return false;

    for (uint32_t lump = 0; lump < kBspLumpCount; ++lump)
        std::memcpy(output.data() + 16 + lump * sizeof(BspLumpHeader), &newHeaders[lump], sizeof(BspLumpHeader));

    const BspLumpHeader& oldGame = oldHeaders[kGameLump];
    const BspLumpHeader& newGame = newHeaders[kGameLump];
    if (oldGame.length && newGame.length && oldGame.offset != newGame.offset) {
        if (newGame.length < sizeof(uint32_t))
            return false;
        uint32_t count{};
        std::memcpy(&count, output.data() + newGame.offset, sizeof(count));
        if (count > (newGame.length - sizeof(uint32_t)) / 16)
            return false;
        const int64_t offsetDelta = static_cast<int64_t>(newGame.offset) - oldGame.offset;
        for (uint32_t index = 0; index < count; ++index) {
            const size_t offsetField = static_cast<size_t>(newGame.offset) + 4 + 16ull * index + 8;
            uint32_t oldOffset{};
            std::memcpy(&oldOffset, output.data() + offsetField, sizeof(oldOffset));
            const int64_t rebased = static_cast<int64_t>(oldOffset) + offsetDelta;
            if (rebased < 0 || rebased > std::numeric_limits<uint32_t>::max())
                return false;
            const uint32_t newOffset = static_cast<uint32_t>(rebased);
            std::memcpy(output.data() + offsetField, &newOffset, sizeof(newOffset));
        }
    }
    return true;
}

void LogServedMap(const std::shared_ptr<MapState>& state, size_t bspSize, uint64_t conversionMs)
{
    {
        std::lock_guard<std::mutex> lock(g_loggedMapsMutex);
        if (!g_loggedMaps.insert(state->mapName).second)
            return;
    }

    std::string entities;
    if (!state->directoryVpk.empty() && IsRegularFile(state->directoryVpk)) {
        const std::string package = state->directoryVpk.string();
        for (size_t i = 0; i < kEntitySuffixes.size(); ++i) {
            const std::string path = "maps/" + state->mapName + kEntitySuffixes[i];
            if (!R1OVPK_HasDirectoryVPKFile(package.c_str(), path.c_str()))
                continue;
            if (!entities.empty())
                entities.push_back(',');
            entities.append(kEntitySuffixes[i] + 1, std::strlen(kEntitySuffixes[i]) - 1);
        }
    }

    const auto steadyMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    char buffer[768]{};
    _snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
        "R1Delta: R2 map provider served map=%s bsp=maps/%s.bsp entities=%s converted=%llu ms map-elapsed-ms=%llu steady-ms=%lld bytes=%zu\n",
        state->mapName.c_str(), state->mapName.c_str(), entities.empty() ? "none" : entities.c_str(),
        static_cast<unsigned long long>(conversionMs),
        static_cast<unsigned long long>(ActiveMapElapsedMilliseconds()),
        static_cast<long long>(steadyMilliseconds), bspSize);
    Msg("%s", buffer);
    OutputDebugStringA(buffer);
    LogProviderReadMetrics("map-bsp-served");
    LogR2TextureProviderMetrics("map-bsp-served");
}

FileBytes ReadConvertedBsp(const MapRequest& request);

int CurrentDeveloperValue()
{
    if (!IsCvarInterfacePublished() || !cvarinterface)
        return -1;
    if (IsR1ODedicatedServer()) {
        if (ConVarR1O* developer = CCVar_FindVar(cvarinterface, "developer"))
            return developer->m_Value.m_nValue;
    }
    else if (OriginalCCVar_FindVar) {
        if (ConVarR1* developer = OriginalCCVar_FindVar(cvarinterface, "developer"))
            return developer->m_Value.m_nValue;
    }
    return -1;
}

FileBytes ReadMapPatchFile(const MapRequest& request)
{
    const auto state = GetMapState(request.mapName);
    if (request.kind != MapRequest::Kind::AiGraph
        || !HavePatchSources() || !EnsureSourceHash(state))
        return {};

    BspSha256 sourceHash{};
    {
        std::lock_guard<std::mutex> lock(state->sourceMutex);
        if (!state->hashReady)
            return {};
        sourceHash = state->sourceHash;
    }

    const std::string bspPath = "maps/" + state->mapName + ".bsp";
    const MapRequest bspRequest{ request.mapName, MapRequest::Kind::Bsp, bspPath };
    const FileBytes converted = ReadConvertedBsp(bspRequest);
    if (!converted)
        return {};
    const BspSha256 convertedHash = p2p::Sha256(converted->data(), converted->size());
    const std::int32_t mapVersion = pGlobalVarsServer ? pGlobalVarsServer->mapversion : 0;
    const std::int32_t developerValue = CurrentDeveloperValue();
    if (mapVersion <= 0 || developerValue < 0)
        return {};

    auto [sources, generation] = PatchSourcesSnapshot();
    std::lock_guard<std::mutex> lock(state->filePatchMutex);
    if (state->filePatchLoaded && state->filePatchGeneration == generation
        && state->filePatchSourceHash == sourceHash
        && state->filePatchConvertedHash == convertedHash
        && state->filePatchMapVersion == mapVersion
        && state->filePatchDeveloperValue == developerValue)
        return state->aiGraphFile;

    state->filePatchLoaded = true;
    state->filePatchGeneration = generation;
    state->filePatchSourceHash = sourceHash;
    state->filePatchConvertedHash = convertedHash;
    state->filePatchMapVersion = mapVersion;
    state->filePatchDeveloperValue = developerValue;
    state->aiGraphFile.reset();
    for (const auto& source : sources) {
        try {
            state->aiGraphFile = source->LoadFileOverride(request.mapName, request.packagePath,
                sourceHash, convertedHash, mapVersion, developerValue);
            if (state->aiGraphFile)
                break;
        }
        catch (...) {
        }
    }
    return state->aiGraphFile;
}

FileBytes ReadConvertedBsp(const MapRequest& request)
{
    const auto state = GetMapState(request.mapName);
    SetActiveMap(state->mapName);
    std::unique_lock<std::mutex> lock(state->conversionMutex);
    while (state->conversionLoading)
        state->conversionReady.wait(lock);
    if (FileBytes cached = state->convertedBsp.lock())
        return cached;
    if (state->conversionFailed)
        return {};
    state->conversionLoading = true;
    lock.unlock();

    std::vector<uint8_t> converted;
    uint64_t conversionMs = 0;
    try {
        if (EnsureSourceBsp(state)) {
            FileBytes source;
            BspSha256 sourceHash{};
            {
                std::lock_guard<std::mutex> sourceLock(state->sourceMutex);
                source = state->sourceBsp;
                sourceHash = state->sourceHash;
            }
            if (source) {
                const auto started = std::chrono::steady_clock::now();
                bspdown::Options options;
                bspdown::Report report;
                converted = bspdown::DowngradeBsp(source->data(), source->size(), options, &report);
                conversionMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count());

                if (const auto patch = GetMatchingPatch(state, sourceHash)) {
                    std::vector<uint8_t> patched;
                    if (ReplacePatchedLumps(converted, *patch, patched)) {
                        converted = std::move(patched);
                        const auto physics = patch->lumpOverrides.find(0x1D);
                        if (physics != patch->lumpOverrides.end() && physics->second) {
                            char message[256]{};
                            _snprintf_s(message, sizeof(message), _TRUNCATE,
                                "R1Delta: R2 map physics patch applied map=%s lump=0x1D bytes=%zu\n",
                                state->mapName.c_str(), physics->second->size());
                            Msg("%s", message);
                            OutputDebugStringA(message);
                        }
                    }
                }
            }
        }
    }
    catch (const std::exception& error) {
        char buffer[768]{};
        _snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
            "R1Delta: R2 map provider conversion failed map=%s error=%s\n",
            state->mapName.c_str(), error.what());
        Msg("%s", buffer);
        OutputDebugStringA(buffer);
    }
    catch (...) {
        char buffer[256]{};
        _snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
            "R1Delta: R2 map provider conversion failed map=%s error=unknown\n",
            state->mapName.c_str());
        Msg("%s", buffer);
        OutputDebugStringA(buffer);
    }

    FileBytes result;
    if (!converted.empty())
        result = std::make_shared<const std::vector<uint8_t>>(std::move(converted));
    {
        std::lock_guard<std::mutex> sourceLock(state->sourceMutex);
        state->sourceBsp.reset();
    }
    lock.lock();
    if (result)
        state->convertedBsp = result;
    else
        state->conversionFailed = true;
    state->conversionLoading = false;
    lock.unlock();
    state->conversionReady.notify_all();

    if (result) {
        LogServedMap(state, result->size(), conversionMs);
    }
    return result;
}

class R2MapProvider final : public FileProvider {
public:
    bool MayContain(std::string_view normalizedPath) const override
    {
        return ParseMapRequest(normalizedPath).has_value();
    }

    bool Exists(std::string_view normalizedPath) override
    {
        const auto request = ParseMapRequest(normalizedPath);
        if (!request)
            return false;
        if (request->kind == MapRequest::Kind::AiGraph)
            return static_cast<bool>(ReadMapPatchFile(*request));

        const auto state = GetMapState(request->mapName);
        if (state->directoryVpk.empty() || !IsRegularFile(state->directoryVpk))
            return false;
        const std::string package = state->directoryVpk.string();
        const std::string path(request->packagePath);
        return R1OVPK_HasDirectoryVPKFile(package.c_str(), path.c_str());
    }

    std::optional<uint64_t> Size(std::string_view normalizedPath) override
    {
        const auto request = ParseMapRequest(normalizedPath);
        if (!request)
            return std::nullopt;
        if (request->kind == MapRequest::Kind::Bsp) {
            const FileBytes bsp = ReadConvertedBsp(*request);
            return bsp ? std::optional<uint64_t>(bsp->size()) : std::nullopt;
        }
        if (request->kind == MapRequest::Kind::AiGraph)
            return std::nullopt;

        const auto state = GetMapState(request->mapName);
        if (state->directoryVpk.empty() || !IsRegularFile(state->directoryVpk))
            return std::nullopt;
        const std::string package = state->directoryVpk.string();
        const std::string path(request->packagePath);
        uint64_t size{};
        return R1OVPK_GetDirectoryVPKFileSize(package.c_str(), path.c_str(), &size)
            ? std::optional<uint64_t>(size) : std::nullopt;
    }

    FileBytes Read(std::string_view normalizedPath) override
    {
        const auto request = ParseMapRequest(normalizedPath);
        if (!request)
            return {};
        if (request->kind == MapRequest::Kind::Bsp) {
            FileBytes data = ReadConvertedBsp(*request);
            if (data && !IsDedicatedServer())
                PrefetchR2MapMaterials(request->mapName, data);
            return data;
        }
        if (request->kind == MapRequest::Kind::AiGraph) {
            FileBytes data = ReadMapPatchFile(*request);
            if (data && !IsDedicatedServer())
                StartR2MaterialProvider();
            return data;
        }

        const auto state = GetMapState(request->mapName);
        if (state->directoryVpk.empty() || !IsRegularFile(state->directoryVpk))
            return {};
        const std::string package = state->directoryVpk.string();
        const std::string path(request->packagePath);
        FileBytes data = R1OVPK_ReadDirectoryVPKFile(package.c_str(), path.c_str());
        if (data && !IsDedicatedServer())
            StartR2MaterialProvider();
        return data;
    }
    std::vector<std::string> Enumerate(std::string_view normalizedPattern) override
    {
        const bool mapPattern = normalizedPattern == "maps/*.bsp";
        const bool fakeDediPackagePattern = normalizedPattern == "vpk/e*.bsp*";
        if (!mapPattern && !fakeDediPackagePattern)
            return {};

        std::vector<std::string> paths;
        for (const char* mapName : kR2MapAllowlist) {
            const auto state = GetMapState(mapName);
            if (state->directoryVpk.empty() || !IsRegularFile(state->directoryVpk))
                continue;

            const std::string package = state->directoryVpk.string();
            const std::string bspPath = "maps/" + state->mapName + ".bsp";
            if (!R1OVPK_HasDirectoryVPKFile(package.c_str(), bspPath.c_str()))
                continue;
            if (mapPattern)
                paths.push_back(bspPath);
            else
                paths.push_back("vpk/" + state->directoryVpk.filename().string());
        }
        return paths;
    }
};

std::filesystem::path FindGameContentDirectoryFrom(std::filesystem::path path)
{
    for (size_t depth = 0; depth < 8 && !path.empty(); ++depth) {
        const std::string leaf = LowerAscii(path.filename().string());
        if (leaf == "r1delta" && IsRegularFile(path / "gameinfo.txt"))
            return path;
        const std::filesystem::path child = path / "r1delta";
        if (IsRegularFile(child / "gameinfo.txt"))
            return child;
        path = path.parent_path();
    }
    return {};
}

std::filesystem::path FindGameContentDirectory()
{
    char executable[MAX_PATH]{};
    const DWORD executableLength = GetModuleFileNameA(nullptr, executable, static_cast<DWORD>(std::size(executable)));
    if (executableLength && executableLength < std::size(executable)) {
        std::filesystem::path directory(executable);
        directory = directory.parent_path();
        if (auto found = FindGameContentDirectoryFrom(directory); !found.empty())
            return found;
    }

    char currentDirectory[MAX_PATH]{};
    const DWORD currentLength = GetCurrentDirectoryA(static_cast<DWORD>(std::size(currentDirectory)), currentDirectory);
    if (currentLength && currentLength < std::size(currentDirectory))
        return FindGameContentDirectoryFrom(currentDirectory);
    return {};
}

std::string MapPackageFingerprint(const std::shared_ptr<MapState>& state)
{
    if (!state || state->directoryVpk.empty() || !IsRegularFile(state->directoryVpk))
        return {};

    std::vector<std::filesystem::path> packages{ state->directoryVpk };
    const std::string chunkPrefix = "client_" + state->mapName + ".bsp.pak000_";
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(state->directoryVpk.parent_path(), error)) {
        if (error)
            break;
        const std::string name = entry.path().filename().string();
        if (name.compare(0, chunkPrefix.size(), chunkPrefix) == 0
            && name.size() >= 4 && name.compare(name.size() - 4, 4, ".vpk") == 0)
            packages.push_back(entry.path());
    }
    std::sort(packages.begin(), packages.end());

    std::string metadata;
    for (const auto& package : packages) {
        error.clear();
        const auto size = std::filesystem::file_size(package, error);
        if (error)
            return {};
        const auto modified = std::filesystem::last_write_time(package, error);
        if (error)
            return {};
        metadata.append(package.filename().string());
        metadata.push_back('|');
        metadata.append(std::to_string(size));
        metadata.push_back('|');
        metadata.append(std::to_string(modified.time_since_epoch().count()));
        metadata.push_back('\n');
    }

    const auto digest = p2p::Sha256(
        reinterpret_cast<const uint8_t*>(metadata.data()), metadata.size());
    constexpr char hex[] = "0123456789abcdef";
    std::string fingerprint;
    fingerprint.reserve(digest.size() * 2);
    for (const uint8_t byte : digest) {
        fingerprint.push_back(hex[byte >> 4]);
        fingerprint.push_back(hex[byte & 0x0f]);
    }
    return fingerprint;
}

} // namespace

bool RegisterMapPatchSource(std::shared_ptr<MapPatchSource> source)
{
    if (!source)
        return false;
    std::lock_guard<std::mutex> lock(g_patchSourcesMutex);
    g_patchSources.push_back(std::move(source));
    ++g_patchSourcesGeneration;
    return true;
}

std::filesystem::path GetR2MapPatchDirectory()
{
    const auto gameDirectory = FindGameContentDirectory();
    return gameDirectory.empty() ? std::filesystem::path{} : gameDirectory / "r2port";
}
std::filesystem::path GetR2InstallDirectory()
{
    return R2InstallDirectory();
}

std::filesystem::path MaterializeMapCacheFile(std::string_view path)
{
    const auto normalized = NormalizeProviderPath(path);
    if (!normalized)
        return {};
    const auto request = ParseMapRequest(*normalized);
    if (!request || request->kind == MapRequest::Kind::AiGraph)
        return {};

    const auto state = GetMapState(request->mapName);
    const std::string fingerprint = MapPackageFingerprint(state);
    if (fingerprint.empty())
        return {};
    const auto gameDirectory = FindGameContentDirectory();
    if (gameDirectory.empty())
        return {};

    const FileBytes data = ReadAll(*normalized);
    if (!data)
        return {};

    const std::filesystem::path root = gameDirectory / "r2port_cache"
        / (state->mapName + "-" + fingerprint);
    const std::filesystem::path destination = root / std::filesystem::path(*normalized);
    std::error_code error;
    std::filesystem::create_directories(destination.parent_path(), error);
    if (error)
        return {};

    const std::filesystem::path temporary = destination.string()
        + ".tmp." + std::to_string(GetCurrentThreadId());
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output || data->size() > static_cast<size_t>(std::numeric_limits<std::streamsize>::max()))
            return {};
        output.write(reinterpret_cast<const char*>(data->data()),
            static_cast<std::streamsize>(data->size()));
        output.close();
        if (!output) {
            std::filesystem::remove(temporary, error);
            return {};
        }
    }

    std::filesystem::remove(destination, error);
    error.clear();
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return {};
    }
    return root;
}

void ClearMapCache()
{
    const auto gameDirectory = FindGameContentDirectory();
    if (gameDirectory.empty())
        return;
    std::error_code error;
    std::filesystem::remove_all(gameDirectory / "r2port_cache", error);
}

void ObserveMapFilePath(std::string_view path)
{
    const auto normalized = NormalizeProviderPath(path);
    if (!normalized || normalized->size() <= 9
        || normalized->compare(0, 5, "maps/") != 0
        || normalized->compare(normalized->size() - 4, 4, ".bsp") != 0)
        return;

    const std::string_view normalizedPath(*normalized);
    const std::string_view mapName = normalizedPath.substr(5, normalizedPath.size() - 9);
    if (!mapName.empty() && mapName.find('/') == std::string_view::npos)
        SetActiveMap(mapName);
}

void InstallR2MapProvider()
{
    std::call_once(g_providerInstallOnce, []() {
        RegisterR2MaterialProvider();
        RegisterR1MapPackMaterialProvider();
        InstallR2MapPatchSource();
        auto provider = std::make_shared<R2MapProvider>();
        RegisterProvider("maps", provider);
        RegisterProvider("vpk", std::move(provider));
    });
}
} // namespace r2port
