#include "engine/logging/logging.h"
#include "r2_map_provider.h"
#include "r2_material_provider.h"
#include "r2_model_provider.h"
#include "r1_map_pack_material_provider.h"
#include "engine/r2port/postfx/r2_postfx.h"
#include "engine/core/factory.h"
#include "engine/core/core.h"
#include "engine/cvar.h"
#include "r2_map_patch_source.h"

#include "engine/filesystem/r1o_vpk.h"
#include "engine/r2port/bspdown/bspdown.hpp"
#include "engine/r2port/entity_rewrite.h"
#include "engine/r2port/trigger_brush.h"
#include "engine/filesystem/r2_particle_provider.h"
#include "p2p/p2p_identity.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <ShlObj.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
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

// Folded into the on-disk cache fingerprint together with entity_rewrite::kRewriteVersion.
// Bump when the vendored bspdown converter or the provider's BSP post-processing changes output.
constexpr uint32_t kConvertedBspCacheVersion = 26;  // 26: rule 24 emitter glow vertex alpha (vr_light_trim_01_totrans, mp_glitch); 25: mp_coliseum and mp_coliseum_column keep the last CSM environment (csmLastEnv); 24: rule 17 real CSM environment chosen by content (BWC, angel_city, coliseum, coliseum_column); 23: rule 23 far-origin prop fade distance

// Raised onto the z of every info_spawnpoint_flag origin in lump 0 (see entity_rewrite::RaiseFlagSpawnOrigins).
// The R1 server drops the spawnpoint back to the floor; one that starts inside solid is removed.
constexpr double kFlagSpawnLift = 8.0;

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
    std::int32_t filePatchMapVersion{};
    FileBytes aiGraphFile;
    std::mutex entityMutex;
    std::unordered_map<std::string, FileBytes> entityFiles;
    // Trigger volumes baked into the converted BSP (trigger_brush.h) and the entity files that reference them.
    std::mutex triggerMutex;
    bool triggerPlanReady{};
    std::unordered_map<std::string, FileBytes> triggerEntityFiles;
    // Converted-BSP disk cache (see ConvertedBspCacheFile): key file path, resolved once.
    std::mutex bspCacheMutex;
    bool bspCacheKeyTried{};
    std::filesystem::path bspCacheFile;
    const char* bspCacheStatus{ "off" };
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

std::string TrimInstallText(std::string value)
{
    const auto isTrim = [](unsigned char c) { return std::isspace(c) != 0 || c == '"'; };
    while (!value.empty() && isTrim(static_cast<unsigned char>(value.back())))
        value.pop_back();
    size_t first = 0;
    while (first < value.size() && isTrim(static_cast<unsigned char>(value[first])))
        ++first;
    return value.substr(first);
}

// Turns a directory, or a file inside the install, into a trimmed absolute directory path.
std::filesystem::path NormalizeInstallRoot(const std::string& text)
{
    const std::string trimmed = TrimInstallText(text);
    if (trimmed.empty())
        return {};
    std::error_code error;
    std::filesystem::path full = std::filesystem::absolute(trimmed, error).lexically_normal();
    if (error)
        return {};
    if (IsRegularFile(full))
        full = full.parent_path();
    while (full.has_relative_path() && (full.native().back() == L'\\' || full.native().back() == L'/'))
        full = full.native().substr(0, full.native().size() - 1);
    return full;
}

// Mirrors the launcher's ^Titanfall\S{0,3}\s*2\b (case-insensitive). The EA installer writes a mangled
// trademark sign, so the display name can be "TitanfallT 2".
bool IsTitanfall2DisplayName(const std::string& name)
{
    constexpr std::string_view prefix = "titanfall";
    if (name.size() < prefix.size() + 1 || LowerAscii(name.substr(0, prefix.size())) != prefix)
        return false;
    const auto isSpace = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
    for (size_t skipped = 0; skipped <= 3; ++skipped) {
        size_t position = prefix.size();
        bool ok = true;
        for (size_t i = 0; i < skipped; ++i, ++position) {
            if (position >= name.size() || isSpace(name[position])) {
                ok = false;
                break;
            }
        }
        if (!ok)
            continue;
        while (position < name.size() && isSpace(name[position]))
            ++position;
        if (position >= name.size() || name[position] != '2')
            continue;
        ++position;
        if (position == name.size()
            || !(std::isalnum(static_cast<unsigned char>(name[position])) || name[position] == '_'))
            return true;
    }
    return false;
}

std::vector<std::string> ReadInstallStringsFromUninstallEntries()
{
    constexpr const char* uninstallPath = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
    constexpr std::string_view eaProductGuid = "{4bd80373-fee7-45b6-8249-6e8e98717405}";
    constexpr std::string_view steamKeyName = "steam app 1237970";

    std::vector<std::string> locations;
    for (REGSAM view : { KEY_WOW64_64KEY, KEY_WOW64_32KEY }) {
        HKEY uninstall{};
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, uninstallPath, 0, KEY_READ | view, &uninstall) != ERROR_SUCCESS)
            continue;
        for (DWORD index = 0;; ++index) {
            char name[512]{};
            DWORD nameLength = static_cast<DWORD>(std::size(name));
            const LONG status = RegEnumKeyExA(uninstall, index, name, &nameLength, nullptr, nullptr, nullptr, nullptr);
            if (status == ERROR_NO_MORE_ITEMS)
                break;
            if (status != ERROR_SUCCESS)
                continue;

            const std::string subKey = std::string(uninstallPath) + "\\" + name;
            const std::string lowerName = LowerAscii(name);
            const bool known = lowerName == eaProductGuid || lowerName == steamKeyName;
            if (!known && !IsTitanfall2DisplayName(ReadRegistryString(HKEY_LOCAL_MACHINE, subKey.c_str(), "DisplayName", view)))
                continue;
            std::string location = ReadRegistryString(HKEY_LOCAL_MACHINE, subKey.c_str(), "InstallLocation", view);
            if (!TrimInstallText(location).empty())
                locations.push_back(std::move(location));
        }
        RegCloseKey(uninstall);
    }
    return locations;
}

std::vector<std::filesystem::path> ReadSteamRoots()
{
    std::vector<std::filesystem::path> roots;
    for (const auto& registryLocation : {
        std::pair{ HKEY_LOCAL_MACHINE, "SOFTWARE\\Valve\\Steam" },
        std::pair{ HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Valve\\Steam" },
        std::pair{ HKEY_CURRENT_USER, "Software\\Valve\\Steam" },
    }) {
        for (const char* valueName : { "InstallPath", "SteamPath" }) {
            for (REGSAM view : { KEY_WOW64_64KEY, KEY_WOW64_32KEY }) {
                const std::string steam = ReadRegistryString(
                    registryLocation.first, registryLocation.second, valueName, view);
                if (!TrimInstallText(steam).empty())
                    roots.emplace_back(steam);
            }
        }
    }
    return roots;
}

// Both libraryfolders.vdf layouts: "path" "D:\\Lib" entries and the older "1" "D:\\Lib" entries.
std::vector<std::filesystem::path> ReadSteamLibraryPaths(const std::filesystem::path& libraryFolders)
{
    std::vector<std::filesystem::path> libraries;
    const auto tokens = ReadVdfTokens(libraryFolders);
    for (size_t i = 0; i + 1 < tokens.size(); ++i) {
        const bool legacyNumericEntry = !tokens[i].empty()
            && std::all_of(tokens[i].begin(), tokens[i].end(), [](unsigned char c) { return std::isdigit(c) != 0; })
            && tokens[i + 1].size() > 2 && tokens[i + 1][1] == ':';
        if (tokens[i] == "path" || legacyNumericEntry) {
            libraries.emplace_back(tokens[i + 1]);
            ++i;
        }
    }
    return libraries;
}

std::vector<std::string> SteamInstallCandidates()
{
    std::vector<std::filesystem::path> libraries;
    for (const auto& steamRoot : ReadSteamRoots()) {
        libraries.push_back(steamRoot);
        for (auto& library : ReadSteamLibraryPaths(steamRoot / "steamapps" / "libraryfolders.vdf"))
            libraries.push_back(std::move(library));
    }

    std::vector<std::string> candidates;
    for (const auto& library : libraries) {
        const std::filesystem::path steamApps = library / "steamapps";
        const std::filesystem::path common = steamApps / "common";
        // The manifest names the real folder; the default names cover a deleted manifest.
        const auto manifest = ReadVdfTokens(steamApps / "appmanifest_1237970.acf");
        for (size_t i = 0; i + 1 < manifest.size(); ++i) {
            if (manifest[i] == "installdir") {
                candidates.push_back((common / manifest[i + 1]).string());
                break;
            }
        }
        candidates.push_back((common / "Titanfall2").string());
        candidates.push_back((common / "Titanfall 2").string());
    }
    return candidates;
}

std::vector<std::string> KnownFolderCandidates()
{
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

    std::vector<std::string> candidates;
    const DWORD drives = GetLogicalDrives();
    for (unsigned int index = 0; index < 26; ++index) {
        if ((drives & (1u << index)) == 0)
            continue;
        char rootName[] = { static_cast<char>('A' + index), ':', '\\', '\0' };
        // Skip network and optical drives so a dead share cannot stall the search.
        const UINT driveType = GetDriveTypeA(rootName);
        if (driveType != DRIVE_FIXED && driveType != DRIVE_REMOVABLE)
            continue;
        if (GetFileAttributesA(rootName) == INVALID_FILE_ATTRIBUTES)
            continue;

        const std::filesystem::path drive(rootName);
        for (const char* parent : knownParents) {
            for (const char* folder : installFolders)
                candidates.push_back((drive / parent / folder).string());
        }
    }
    return candidates;
}

std::vector<std::string> RespawnRegistryCandidates()
{
    std::vector<std::string> installs;
    for (REGSAM view : { KEY_WOW64_64KEY, KEY_WOW64_32KEY }) {
        std::string install = ReadRegistryString(
            HKEY_LOCAL_MACHINE, "SOFTWARE\\Respawn\\Titanfall2", "Install Dir", view);
        if (!TrimInstallText(install).empty())
            installs.push_back(std::move(install));
    }
    return installs;
}

// Finds the player's own Titanfall 2 install. The search order is shared with the launcher
// (TitanfallLocator.FindTitanfall2 in launcher_ex/TitanfallFinder.cs) and documented in
// docs/r2port.md; change all three together:
//   1. R1DELTA_TITANFALL2_PATH. Explicit: when set and not a valid install the search stops,
//      it does not fall back to another install. (The launcher also takes a manual path first.)
//   2. HKLM\SOFTWARE\Respawn\Titanfall2 "Install Dir" (64-bit then 32-bit view)
//   3. Uninstall entries: the EA product GUID, "Steam App 1237970", or a display name that reads Titanfall 2
//   4. Steam: root from the registry, libraries from libraryfolders.vdf, appmanifest_1237970.acf
//      installdir, then common\Titanfall2 and common\Titanfall 2
//   5. Titanfall2 / "Titanfall 2" under well-known parent folders on every fixed or removable drive
// Every candidate must pass IsValidR2Install. foundVia names the source of the result, or the
// environment variable when it was set and rejected.
std::filesystem::path DiscoverR2Install(std::string& foundVia)
{
    char environmentPath[32768]{};
    const DWORD environmentLength = GetEnvironmentVariableA(
        "R1DELTA_TITANFALL2_PATH", environmentPath, static_cast<DWORD>(std::size(environmentPath)));
    if (environmentLength) {
        foundVia = "R1DELTA_TITANFALL2_PATH";
        if (environmentLength >= std::size(environmentPath))
            return {};
        const auto root = NormalizeInstallRoot(environmentPath);
        return IsValidR2Install(root) ? root : std::filesystem::path{};
    }

    struct Source {
        const char* name;
        std::vector<std::string> (*candidates)();
    };
    const std::array<Source, 4> sources = { {
        { "HKLM Respawn\\Titanfall2", RespawnRegistryCandidates },
        { "Uninstall entries", ReadInstallStringsFromUninstallEntries },
        { "Steam libraries", SteamInstallCandidates },
        { "known folders", KnownFolderCandidates },
    } };

    std::unordered_set<std::string> tested;
    for (const Source& source : sources) {
        for (const std::string& candidate : source.candidates()) {
            const auto root = NormalizeInstallRoot(candidate);
            if (root.empty() || !tested.insert(LowerAscii(root.string())).second)
                continue;
            if (IsValidR2Install(root)) {
                foundVia = source.name;
                return root;
            }
        }
    }
    foundVia.clear();
    return {};
}

const std::filesystem::path& R2InstallDirectory()
{
    std::call_once(g_installDiscoveryOnce, []() {
        std::string foundVia;
        g_r2InstallDirectory = DiscoverR2Install(foundVia);
        char buffer[MAX_PATH * 2]{};
        if (g_r2InstallDirectory.empty() && !foundVia.empty())
            _snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
                "R1Delta: R2 map provider install discovery: %s is set but is not a valid Titanfall 2 install\n",
                foundVia.c_str());
        else if (g_r2InstallDirectory.empty())
            _snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
                "R1Delta: R2 map provider install discovery found no valid install\n");
        else
            _snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
                "R1Delta: R2 map provider install discovery found %s via %s\n",
                g_r2InstallDirectory.string().c_str(), foundVia.c_str());
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

std::string MapPackageFingerprint(const std::shared_ptr<MapState>& state);

// SHA-256 through CNG (SHA-NI where the CPU has it): about 70 ms for a 150 MB BSP against 520 ms for
// p2p::Sha256, which copies the whole image into a std::vector and hashes it in scalar code. Same digest.
BspSha256 Sha256Fast(const uint8_t* data, size_t size)
{
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    BspSha256 digest{};
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0
        && BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) == 0;
    size_t offset = 0;
    while (ok && offset < size) {
        const ULONG chunk = static_cast<ULONG>(std::min<size_t>(size - offset, 1u << 30));
        ok = BCryptHashData(hash, const_cast<PUCHAR>(data + offset), chunk, 0) == 0;
        offset += chunk;
    }
    ok = ok && BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) == 0;
    if (hash)
        BCryptDestroyHash(hash);
    if (algorithm)
        BCryptCloseAlgorithmProvider(algorithm, 0);
    return ok ? digest : p2p::Sha256(data, size);
}

// ---- converted-BSP disk cache -----------------------------------------------------------------------
// DowngradeBsp output (before the lump-0 entity rewrite and the physics patch, which depend on runtime
// state and run on every load) plus the SHA-256 of the R2 source BSP the patch sources match against.
// The file name carries MapPackageFingerprint: the R2 directory VPK and chunk names, sizes and mtimes,
// entity_rewrite::kRewriteVersion and kConvertedBspCacheVersion. Nothing is rebuilt unless one changes.
// A hit skips the VPK read (LZHAM), the SHA-256 and bspdown.
constexpr char kBspCacheMagic[8] = { 'R', '2', 'B', 'S', 'P', 'C', 'H', '1' };

#pragma pack(push, 1)
struct BspCacheHeader {
    char magic[8];
    uint32_t cacheVersion;
    uint32_t entityRewriteVersion;
    uint64_t convertedSize;
    uint8_t sourceSha256[32];
};
#pragma pack(pop)

std::filesystem::path ConvertedBspCacheFile(const std::shared_ptr<MapState>& state)
{
    std::lock_guard<std::mutex> lock(state->bspCacheMutex);
    char disabled[8]{};
    if (GetEnvironmentVariableA("R2PORT_BSP_CACHE", disabled, sizeof(disabled)) && disabled[0] == '0')
        return {};   // measurement switch: R2PORT_BSP_CACHE=0 turns the converted-BSP cache off
    if (!state->bspCacheKeyTried) {
        state->bspCacheKeyTried = true;
        const std::string fingerprint = MapPackageFingerprint(state);
        const std::filesystem::path root = GetR2MapPatchDirectory();
        if (!fingerprint.empty() && !root.empty())
            state->bspCacheFile = root / "bsp_cache" / (state->mapName + "-" + fingerprint + ".r2bsp");
    }
    return state->bspCacheFile;
}

// Reads the cache file. With `converted` null only the header is read (the source hash for patch lookup).
bool LoadConvertedBspCache(const std::shared_ptr<MapState>& state, BspSha256& sourceHash,
    std::vector<uint8_t>* converted)
{
    const std::filesystem::path file = ConvertedBspCacheFile(state);
    if (file.empty())
        return false;
    try {
        std::ifstream input(file, std::ios::binary);
        if (!input)
            return false;
        BspCacheHeader header{};
        input.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (!input || std::memcmp(header.magic, kBspCacheMagic, sizeof(header.magic)) != 0
            || header.cacheVersion != kConvertedBspCacheVersion
            || header.entityRewriteVersion != entity_rewrite::kRewriteVersion
            || header.convertedSize < kBspHeaderSize || header.convertedSize > (1ull << 31))
            return false;
        std::error_code error;
        const auto fileSize = std::filesystem::file_size(file, error);
        if (error || fileSize != sizeof(header) + header.convertedSize)
            return false;
        if (converted) {
            converted->resize(static_cast<size_t>(header.convertedSize));
            input.read(reinterpret_cast<char*>(converted->data()), static_cast<std::streamsize>(converted->size()));
            if (!input) {
                converted->clear();
                return false;
            }
        }
        std::memcpy(sourceHash.data(), header.sourceSha256, sizeof(header.sourceSha256));
        return true;
    }
    catch (...) {
        if (converted)
            converted->clear();
        return false;
    }
}

void StoreConvertedBspCache(const std::shared_ptr<MapState>& state, const BspSha256& sourceHash,
    const std::vector<uint8_t>& converted)
{
    const std::filesystem::path file = ConvertedBspCacheFile(state);
    if (file.empty())
        return;
    try {
        std::error_code error;
        std::filesystem::create_directories(file.parent_path(), error);
        if (error)
            return;
        BspCacheHeader header{};
        std::memcpy(header.magic, kBspCacheMagic, sizeof(header.magic));
        header.cacheVersion = kConvertedBspCacheVersion;
        header.entityRewriteVersion = entity_rewrite::kRewriteVersion;
        header.convertedSize = converted.size();
        std::memcpy(header.sourceSha256, sourceHash.data(), sizeof(header.sourceSha256));

        std::filesystem::path temporary = file;
        temporary += ".tmp" + std::to_string(GetCurrentProcessId()) + "." + std::to_string(GetCurrentThreadId());
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output)
                return;
            output.write(reinterpret_cast<const char*>(&header), sizeof(header));
            output.write(reinterpret_cast<const char*>(converted.data()), static_cast<std::streamsize>(converted.size()));
            output.close();
            if (!output) {
                std::filesystem::remove(temporary, error);
                return;
            }
        }
        if (!MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            std::filesystem::remove(temporary, error);
            return;
        }
        state->bspCacheStatus = "stored";

        // Fingerprints of older VPKs / converter versions are never read again.
        const std::string prefix = state->mapName + "-";
        const std::string keep = file.filename().string();
        for (const auto& entry : std::filesystem::directory_iterator(file.parent_path(), error)) {
            if (error)
                break;
            const std::string name = entry.path().filename().string();
            if (name != keep && name.compare(0, prefix.size(), prefix) == 0
                && name.size() > 6 && name.compare(name.size() - 6, 6, ".r2bsp") == 0
                && name.size() == prefix.size() + 64 + 6) {
                std::error_code removeError;
                std::filesystem::remove(entry.path(), removeError);
            }
        }
    }
    catch (...) {
    }
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
        hash = Sha256Fast(source->data(), source->size());

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
    // The converted-BSP cache carries the source hash: no VPK read and no SHA-256 on a warm launch.
    BspSha256 cachedHash{};
    if (LoadConvertedBspCache(state, cachedHash, nullptr)) {
        std::lock_guard<std::mutex> lock(state->sourceMutex);
        if (!state->hashReady) {
            state->sourceHash = cachedHash;
            state->hashReady = true;
        }
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

using LumpReplacements = std::array<const FileBytes*, kBspLumpCount>;

bool ReplaceLumps(
    const std::vector<uint8_t>& input,
    const LumpReplacements& replacements,
    std::vector<uint8_t>& output)
{
    if (input.size() < kBspHeaderSize)
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

bool ReplacePatchedLumps(
    const std::vector<uint8_t>& input,
    const MapPatch& patch,
    std::vector<uint8_t>& output)
{
    LumpReplacements replacements{};
    bool anyReplacement = false;
    for (const auto& [lumpId, data] : patch.lumpOverrides) {
        if ((lumpId == 0x1D || lumpId == 0x46) && data && lumpId < replacements.size()) {
            replacements[lumpId] = &data;
            anyReplacement = true;
        }
    }
    return anyReplacement && ReplaceLumps(input, replacements, output);
}

void LogEntityRewrite(const char* what, std::string_view mapName,
    const entity_rewrite::RewriteReport& report, size_t bytesIn, size_t bytesOut, uint64_t flagOriginsRaised = 0)
{
    char buffer[640]{};
    _snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
        "R1Delta: R2 entity rewrite map=%.*s %s entities=%llu->%llu renamed-classnames=%llu eden-space-nodes=%llu "
        "ctf-team-keys=%llu dropped-script-mover-lightweight=%llu dropped-light-environment-volume=%llu "
        "npc-keys-lowered=%llu flag-origins-raised=%llu bytes=%zu->%zu version=%u\n",
        static_cast<int>(mapName.size()), mapName.data(), what,
        static_cast<unsigned long long>(report.entitiesIn),
        static_cast<unsigned long long>(report.entitiesOut),
        static_cast<unsigned long long>(report.renamedClassnames),
        static_cast<unsigned long long>(report.renamedEdenSpaceNodes),
        static_cast<unsigned long long>(report.renamedCtfTeamKeys),
        static_cast<unsigned long long>(report.droppedScriptMoverLightweight),
        static_cast<unsigned long long>(report.droppedLightEnvironmentVolume),
        static_cast<unsigned long long>(report.loweredNpcKeys),
        static_cast<unsigned long long>(flagOriginsRaised),
        bytesIn, bytesOut, entity_rewrite::kRewriteVersion);
    Msg("%s", buffer);
    OutputDebugStringA(buffer);
}

// Reads one partition .ent file from the R2 package and applies the entity text rules. Returns false when
// the package has no such file; text that cannot be parsed is returned unchanged.
bool LoadRewrittenPartition(const std::shared_ptr<MapState>& state, const std::string& path, std::string& text,
    std::map<std::string, std::uint64_t>& unsupported)
{
    if (state->directoryVpk.empty() || !IsRegularFile(state->directoryVpk))
        return false;
    const std::string package = state->directoryVpk.string();
    const FileBytes data = R1OVPK_ReadDirectoryVPKFile(package.c_str(), path.c_str());
    if (!data)
        return false;
    text.assign(reinterpret_cast<const char*>(data->data()), data->size());
    try {
        entity_rewrite::RewriteReport report;
        std::string rewritten = entity_rewrite::RewriteEntityLump(text, { state->mapName, false, &R2ParticleTargetName }, &report);
        if (!report.unknownConstructs)
            text = std::move(rewritten);
        for (const auto& entry : report.unsupportedClasses)
            unsupported[entry.name] += entry.occurrences;
        if (report.droppedScriptMoverLightweight)
            unsupported["#script_mover_lightweight"] += report.droppedScriptMoverLightweight;
        if (report.droppedLightEnvironmentVolume)
            unsupported["#light_environment_volume"] += report.droppedLightEnvironmentVolume;
    }
    catch (const std::exception&) {
    }
    return true;
}

void LogTriggerBrushes(std::string_view mapName, const trigger_brush::Report& report)
{
    char buffer[512]{};
    _snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
        "R1Delta: R2 trigger brushes map=%.*s %s entities=%llu brushes=%llu non-axial-planes=%llu models=%u..%u "
        "skipped-no-brush=%llu skipped-has-model=%llu%s%s version=%u\n",
        static_cast<int>(mapName.size()), mapName.data(), report.failure.empty() ? "baked" : "NOT-BAKED",
        static_cast<unsigned long long>(report.entitiesBaked), static_cast<unsigned long long>(report.brushes),
        static_cast<unsigned long long>(report.nonAxialPlanes), report.firstModel,
        report.firstModel + report.modelsAdded, static_cast<unsigned long long>(report.entitiesSkippedNoBrush),
        static_cast<unsigned long long>(report.entitiesSkippedHasModel), report.failure.empty() ? "" : " reason=",
        report.failure.c_str(), trigger_brush::kBakeVersion);
    Msg("%s", buffer);
    OutputDebugStringA(buffer);
}

// Applies the R2 -> R1 entity text rules to the converted BSP's entity lump (lump 0), where the
// CTF flag spawnpoints live, and gives every R2 trigger volume an R1 brush model (see trigger_brush.h:
// R2 triggers carry planes in their entity text and have no model, so R1 would spawn them without a volume).
// The entity files that reference the new models are kept in the map state for ReadEntityFile.
// Leaves the BSP untouched when no rule matches.
void RewriteConvertedBspEntities(const std::shared_ptr<MapState>& state, std::vector<uint8_t>& bsp)
{
    const std::string_view mapName = state->mapName;
    const auto markPlanReady = [&] {
        std::lock_guard<std::mutex> lock(state->triggerMutex);
        state->triggerPlanReady = true;
    };
    if (bsp.size() < kBspHeaderSize)
        return markPlanReady();
    BspLumpHeader header{};
    std::memcpy(&header, bsp.data() + 16, sizeof(header));
    if (!header.length || header.offset > bsp.size() || header.length > bsp.size() - header.offset)
        return markPlanReady();

    const std::string_view text(reinterpret_cast<const char*>(bsp.data()) + header.offset, header.length);
    entity_rewrite::RewriteReport report;
    std::string rewritten = entity_rewrite::RewriteEntityLump(text, { mapName, false, &R2ParticleTargetName }, &report);
    std::uint64_t raised = 0;
    rewritten = entity_rewrite::RaiseFlagSpawnOrigins(rewritten, kFlagSpawnLift, &raised);
    std::map<std::string, std::uint64_t> unsupported;
    for (const auto& entry : report.unsupportedClasses)
        unsupported[entry.name] += entry.occurrences;
    if (report.droppedScriptMoverLightweight)
        unsupported["#script_mover_lightweight"] += report.droppedScriptMoverLightweight;
    if (report.droppedLightEnvironmentVolume)
        unsupported["#light_environment_volume"] += report.droppedLightEnvironmentVolume;

    // Trigger volumes: partition 0 is lump 0, then the five .ent files in kEntitySuffixes order.
    std::array<std::string, kEntitySuffixes.size()> partitionText;
    std::array<bool, kEntitySuffixes.size()> partitionPresent{};
    std::vector<std::string_view> partitions;
    partitions.push_back(rewritten);
    for (size_t i = 0; i < kEntitySuffixes.size(); ++i) {
        const std::string path = "maps/" + state->mapName + kEntitySuffixes[i];
        partitionPresent[i] = LoadRewrittenPartition(state, path, partitionText[i], unsupported);
        partitions.push_back(partitionText[i]);
    }
    {
        std::string line, dropped;
        for (const auto& entry : unsupported) {
            std::string& target = entry.first[0] == '#' ? dropped : line;
            target += ' ';
            target += (entry.first[0] == '#' ? entry.first.substr(1) : entry.first) + "=" + std::to_string(entry.second);
        }
        char buffer[1024]{};
        _snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
            "R1Delta: R2 entity classes R1 does not register map=%.*s skipped-by-R1:%s dropped-by-rewrite:%s\n",
            static_cast<int>(mapName.size()), mapName.data(), line.empty() ? " none" : line.c_str(),
            dropped.empty() ? " none" : dropped.c_str());
        Msg("%s", buffer);
        OutputDebugStringA(buffer);
    }
    trigger_brush::Result triggers;
    try {
        triggers = trigger_brush::Bake(bsp, partitions);
    }
    catch (const std::exception& error) {
        triggers = {};
        triggers.report.failure = error.what();
    }
    if (triggers.baked || !triggers.report.failure.empty())
        LogTriggerBrushes(mapName, triggers.report);

    std::array<FileBytes, kBspLumpCount> owned{};
    LumpReplacements replacements{};
    if (triggers.baked) {
        for (size_t lump = 0; lump < kBspLumpCount; ++lump) {
            if (triggers.lumps[lump].empty())
                continue;
            owned[lump] = std::make_shared<const std::vector<uint8_t>>(std::move(triggers.lumps[lump]));
            replacements[lump] = &owned[lump];
        }
    }
    else if (rewritten != text) {
        owned[0] = std::make_shared<const std::vector<uint8_t>>(rewritten.begin(), rewritten.end());
        replacements[0] = &owned[0];
    }
    else
        return markPlanReady();

    std::vector<uint8_t> output;
    if (!ReplaceLumps(bsp, replacements, output))
        return markPlanReady();
    LogEntityRewrite("bsp-lump=0", mapName, report, header.length, owned[0]->size(), raised);
    bsp = std::move(output);

    std::lock_guard<std::mutex> lock(state->triggerMutex);
    if (triggers.baked) {
        for (size_t i = 0; i < kEntitySuffixes.size(); ++i) {
            if (!partitionPresent[i] || triggers.texts[i + 1] == partitionText[i])
                continue;
            const std::string path = "maps/" + state->mapName + kEntitySuffixes[i];
            state->triggerEntityFiles[path] = std::make_shared<const std::vector<uint8_t>>(
                triggers.texts[i + 1].begin(), triggers.texts[i + 1].end());
        }
    }
    state->triggerPlanReady = true;
}

// The entity files that reference baked trigger models only exist after the BSP was converted.
FileBytes ReadConvertedBsp(const MapRequest& request);

FileBytes BakedEntityFile(const std::shared_ptr<MapState>& state, const std::string& path)
{
    bool ready;
    {
        std::lock_guard<std::mutex> lock(state->triggerMutex);
        ready = state->triggerPlanReady;
    }
    if (!ready) {
        const std::string bspPath = "maps/" + state->mapName + ".bsp";
        ReadConvertedBsp(MapRequest{ state->mapName, MapRequest::Kind::Bsp, bspPath });
    }
    std::lock_guard<std::mutex> lock(state->triggerMutex);
    const auto found = state->triggerEntityFiles.find(path);
    return found != state->triggerEntityFiles.end() ? found->second : FileBytes{};
}

// Reads one of the map's five partition entity files from the R2 package and applies the same
// rules. The result is cached per map so Size() and Read() agree and the rewrite runs once.
FileBytes ReadEntityFile(const MapRequest& request, const std::shared_ptr<MapState>& state)
{
    const std::string path(request.packagePath);
    if (FileBytes baked = BakedEntityFile(state, path))
        return baked;
    std::lock_guard<std::mutex> lock(state->entityMutex);
    if (const auto found = state->entityFiles.find(path); found != state->entityFiles.end())
        return found->second;
    if (state->directoryVpk.empty() || !IsRegularFile(state->directoryVpk))
        return {};

    const std::string package = state->directoryVpk.string();
    FileBytes data = R1OVPK_ReadDirectoryVPKFile(package.c_str(), path.c_str());
    if (!data)
        return {};

    try {
        const std::string_view text(reinterpret_cast<const char*>(data->data()), data->size());
        entity_rewrite::RewriteReport report;
        std::string rewritten = entity_rewrite::RewriteEntityLump(text, { request.mapName, false, &R2ParticleTargetName }, &report);
        if (report.unknownConstructs) {
            char buffer[384]{};
            _snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
                "R1Delta: R2 entity rewrite map=%s file=%s could not parse the entity text; serving it unchanged\n",
                state->mapName.c_str(), path.c_str());
            Msg("%s", buffer);
            OutputDebugStringA(buffer);
        }
        else if (rewritten != text) {
            const size_t bytesIn = data->size();
            data = std::make_shared<const std::vector<uint8_t>>(rewritten.begin(), rewritten.end());
            LogEntityRewrite(path.c_str(), request.mapName, report, bytesIn, data->size());
        }
    }
    catch (const std::exception& error) {
        char buffer[512]{};
        _snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
            "R1Delta: R2 entity rewrite map=%s file=%s failed (%s); serving it unchanged\n",
            state->mapName.c_str(), path.c_str(), error.what());
        Msg("%s", buffer);
        OutputDebugStringA(buffer);
    }
    state->entityFiles.emplace(path, data);
    return data;
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
        "R1Delta: R2 map provider served map=%s bsp=maps/%s.bsp entities=%s converted=%llu ms map-elapsed-ms=%llu steady-ms=%lld bytes=%zu bsp-cache=%s\n",
        state->mapName.c_str(), state->mapName.c_str(), entities.empty() ? "none" : entities.c_str(),
        static_cast<unsigned long long>(conversionMs),
        static_cast<unsigned long long>(ActiveMapElapsedMilliseconds()),
        static_cast<long long>(steadyMilliseconds), bspSize, state->bspCacheStatus);
    Msg("%s", buffer);
    OutputDebugStringA(buffer);
    LogProviderReadMetrics("map-bsp-served");
    LogR2TextureProviderMetrics("map-bsp-served");
    LogR2ModelProviderMetrics("map-bsp-served");
}

FileBytes ReadConvertedBsp(const MapRequest& request);

std::atomic<bool> g_ainPackageServed{ false };

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

    const std::int32_t mapVersion = pGlobalVarsServer ? pGlobalVarsServer->mapversion : 0;
    if (mapVersion <= 0)
        return {};

    auto [sources, generation] = PatchSourcesSnapshot();
    std::lock_guard<std::mutex> lock(state->filePatchMutex);
    if (state->filePatchLoaded && state->filePatchGeneration == generation
        && state->filePatchSourceHash == sourceHash
        && state->filePatchMapVersion == mapVersion)
        return state->aiGraphFile;

    state->filePatchLoaded = true;
    state->filePatchGeneration = generation;
    state->filePatchSourceHash = sourceHash;
    state->filePatchMapVersion = mapVersion;
    state->aiGraphFile.reset();
    for (const auto& source : sources) {
        try {
            state->aiGraphFile = source->LoadFileOverride(request.mapName, request.packagePath,
                sourceHash, mapVersion);
            if (state->aiGraphFile)
                break;
        }
        catch (...) {
        }
    }
    return state->aiGraphFile;
}

// The options the provider converts with. Everything not set here (notably the cubemap scale, rule 13)
// is bspdown's own default, which is also what the bspdown.exe command line uses without flags, so the
// provider and the CLI produce the same BSP. The converter owns the scale; the provider does not override it.
// The other options are spelled out so a change of a library default cannot silently change what the
// game loads.
// Diagnostic converter variants: R1DELTA_R2PORT_BSPDOWN is a comma separated token list that flips one option each
// (A/B runs without a rebuild). Unknown tokens are logged and ignored. The raw string is part of the converted-BSP
// cache key (MapPackageFingerprint) so variants never share a cache file.
const std::string& R2ConverterVariantString()
{
    static const std::string variant = [] {
        char buffer[512]{};
        const DWORD length = GetEnvironmentVariableA("R1DELTA_R2PORT_BSPDOWN", buffer, sizeof(buffer));
        return length && length < sizeof(buffer) ? std::string(buffer, length) : std::string();
    }();
    return variant;
}

// key=value tokens: sky-scale, sun-scale, pbr-scale (rule 20 light scales), cube-scale (rule 13). The value must be a finite
// number >= 0.
bool ApplyConverterScaleToken(bspdown::Options& options, const std::string& token)
{
    const size_t equals = token.find('=');
    if (equals == std::string::npos || equals + 1 >= token.size())
        return false;
    const std::string key = token.substr(0, equals);
    const std::string text = token.substr(equals + 1);
    char* parsedEnd = nullptr;
    const double value = std::strtod(text.c_str(), &parsedEnd);
    if (parsedEnd != text.c_str() + text.size() || !std::isfinite(value) || value < 0.0)
        return false;
    const float scale = static_cast<float>(value);
    if (key == "sky-scale") options.skyAmbientScale = scale;
    else if (key == "sun-scale") options.sunScale = scale;
    else if (key == "pbr-scale") options.pbrScale = scale;
    else if (key == "cube-scale") options.cubemapScale = scale;
    else if (key == "cube-diag") options.cubemapDiag = static_cast<int>(value);   // diagnostic: 1 = flat colour per cube frame, 2 = all neutral
    else return false;
    return true;
}

void ApplyConverterVariant(bspdown::Options& options, const std::string& variant)
{
    size_t begin = 0;
    while (begin <= variant.size()) {
        size_t end = variant.find(',', begin);
        if (end == std::string::npos)
            end = variant.size();
        std::string token = variant.substr(begin, end - begin);
        begin = end + 1;
        token.erase(std::remove_if(token.begin(), token.end(), [](unsigned char c) { return std::isspace(c) != 0; }), token.end());
        if (token.empty())
            continue;
        if (token == "csm-rebase") options.csmRebase = true;
        else if (token == "csm-no-rebase") options.csmRebase = false;
        else if (token == "csm-last-env") options.csmLastEnv = true;   // rule 17: keep the last shadow environment record (mp_coliseum and mp_coliseum_column, which have no cloudmask)
        else if (token == "empty-shadow-meshes") options.emptyShadowMeshes = true;
        else if (token == "no-sun-alpha") options.lightmapSunAlpha = false;
        else if (token == "no-rtl") options.rtlConvert = false;
        else if (token == "rtl-bake-edge") options.rtlBake = 1;   // rule 20f (default): bake spot/point RTL light of lights with shadow edges into lightmap plane A (smooth penumbra)
        else if (token == "rtl-bake-all") options.rtlBake = 2;    // rule 20f: bake every eligible real-time light
        else if (token == "rtl-bake-none") options.rtlBake = 0;   // opt-out: R1 real-time light list only, no baked penumbra
        else if (token == "cubemaps-split") options.cubemaps = bspdown::CubemapMode::Split;
        else if (token == "cubemaps-single") options.cubemaps = bspdown::CubemapMode::None;   // pre-split behaviour: every world sort samples the nearest cube
        else if (token == "no-collapse-envs") options.collapseEnvs = false;
        else if (token == "pbr-keep") options.pbrLights = false;
        else if (token == "flag2") options.worldLightFlagBit2 = true;
        else if (token == "props-keep10") options.keepPropFlag10 = true;
        else if (token == "props-keepfade") options.keepPropFade = true;   // rule 23 opt-out: copy the R2 prop fade distance for far-origin props too
        else if (ApplyConverterScaleToken(options, token)) {}
        else {
            char message[256]{};
            _snprintf_s(message, sizeof(message), _TRUNCATE,
                "R1Delta: R2 converter variant token '%s' ignored (unknown or bad value)\n", token.c_str());
            Msg("%s", message);
            OutputDebugStringA(message);
        }
    }
}

bspdown::Options R2ConverterOptions()
{
    bspdown::Options options;
    options.worldLightFlagBit2 = false;
    options.keepPropFlag10 = false;     // rule 16: clear static prop flag 0x10 (R1 reads it as "shadow caster only, not drawn")
    options.keepPropFade = false;       // rule 23: raise the fade distance of props whose origin is far from their geometry (R1 culls by view-to-origin distance); opt-out token props-keepfade
    options.csmRebase = true;           // rule 17: drop R2's empty dummy CSM environment (the real one is chosen by content, not by position: on four maps the dummy is the last record) so R1's tree root is the real one (tree/prop sun shadows); opt-out token csm-no-rebase
    options.emptyShadowMeshes = false;
    options.cubemaps = bspdown::CubemapMode::Split;   // rule 13d: per-mesh R2 cubemaps via patch materials; opt-out token cubemaps-single
    options.rtlBake = 1;                // rule 20f: bake spot/point real-time lights with shadow edges into lightmap plane A; opt-out tokens rtl-bake-none, no-rtl
    ApplyConverterVariant(options, R2ConverterVariantString());
    return options;
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
        BspSha256 sourceHash{};
        bool haveConverted = false;
        const auto started = std::chrono::steady_clock::now();
        if (LoadConvertedBspCache(state, sourceHash, &converted)) {
            {
                std::lock_guard<std::mutex> sourceLock(state->sourceMutex);
                state->sourceHash = sourceHash;
                state->hashReady = true;
            }
            RewriteConvertedBspEntities(state, converted);
            state->bspCacheStatus = "hit";
            haveConverted = true;
        }
        else if (EnsureSourceBsp(state)) {
            FileBytes source;
            {
                std::lock_guard<std::mutex> sourceLock(state->sourceMutex);
                source = state->sourceBsp;
                sourceHash = state->sourceHash;
            }
            if (source) {
                state->bspCacheStatus = "miss";
                bspdown::Options options = R2ConverterOptions();
                // No cloudmask on these two maps: the entity rewrite leaves the dummy light_environment last, so the CSM environment must be the last record too.
                if (state->mapName == "mp_coliseum" || state->mapName == "mp_coliseum_column")
                    options.csmLastEnv = true;
                bspdown::Report report;
                converted = bspdown::DowngradeBsp(source->data(), source->size(), options, &report);
                StoreConvertedBspCache(state, sourceHash, converted);
                RewriteConvertedBspEntities(state, converted);
                haveConverted = true;
            }
        }
        conversionMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count());

        if (haveConverted) {
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
        char hashFlag[8]{};
        if (GetEnvironmentVariableA("R2PORT_LOG_HASHES", hashFlag, sizeof(hashFlag)) && hashFlag[0] != '0') {
            const BspSha256 digest = Sha256Fast(result->data(), result->size());
            char text[160]{};
            int length = _snprintf_s(text, sizeof(text), _TRUNCATE, "R1Delta: R2 map provider served-hash map=%s sha256=", state->mapName.c_str());
            for (const uint8_t byte : digest)
                length += _snprintf_s(text + length, sizeof(text) - length, _TRUNCATE, "%02x", byte);
            _snprintf_s(text + length, sizeof(text) - length, _TRUNCATE, " bytes=%zu\n", result->size());
            Msg("%s", text);
            OutputDebugStringA(text);
        }
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

        const FileBytes entities = ReadEntityFile(*request, GetMapState(request->mapName));
        return entities ? std::optional<uint64_t>(entities->size()) : std::nullopt;
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
            if (data)
                PrefetchR2MapModels(request->mapName, data);
            return data;
        }
        if (request->kind == MapRequest::Kind::AiGraph) {
            FileBytes data = ReadMapPatchFile(*request);
            if (data)
                g_ainPackageServed = true;
            if (data && !IsDedicatedServer())
                StartR2MaterialProvider();
            return data;
        }

        const auto state = GetMapState(request->mapName);
        FileBytes data = ReadEntityFile(*request, state);
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

// Serves the material files the active R2 map's own directory VPK carries: assets that only
// entity keys name (cloudmask textures, color-correction .raw files) and a few map-specific VMT
// and VTF files. R1 content and the RPak material provider are consulted first.
// Converted models name every material "r2m/<R2 material>" (modelconv). Materials that exist only as a
// loose VMT in the map pack (not as an rpak matl), e.g. models/holo_screens/hammond_marvins_01 on
// mp_complex3, never resolve in the RPak provider, so the model drew R1's magenta error checker. For a
// "materials/r2m/<x>.vmt" request the pack's "materials/<x>.vmt" is served instead; a world shader in it
// is relabelled VertexLitGeneric, as the RPak provider does for models.
class R2MapPackFileProvider final : public FileProvider {
public:
    bool MayContain(std::string_view normalizedPath) const override
    {
        return !PackageFor(normalizedPath).empty();
    }

    // The answer only changes when the active map does, and that invalidates the miss cache.
    bool CanCacheMisses() const override { return true; }

    bool Exists(std::string_view normalizedPath) override { return MayContain(normalizedPath); }

    std::optional<uint64_t> Size(std::string_view normalizedPath) override
    {
        std::string path;
        const std::string package = PackageFor(normalizedPath, path);
        if (package.empty())
            return std::nullopt;
        if (IsModelNamespaceMaterial(normalizedPath)) {
            const FileBytes data = Read(normalizedPath);
            return data ? std::optional<uint64_t>(data->size()) : std::nullopt;
        }
        uint64_t size{};
        return R1OVPK_GetDirectoryVPKFileSize(package.c_str(), path.c_str(), &size)
            ? std::optional<uint64_t>(size) : std::nullopt;
    }

    FileBytes Read(std::string_view normalizedPath) override
    {
        std::string path;
        const std::string package = PackageFor(normalizedPath, path);
        if (package.empty())
            return {};
        FileBytes data = R1OVPK_ReadDirectoryVPKFile(package.c_str(), path.c_str());
        if (data && IsModelNamespaceMaterial(normalizedPath)) {
            std::string text(reinterpret_cast<const char*>(data->data()), data->size());
            static constexpr std::string_view world = "\"LightmappedGeneric\"";
            const std::size_t at = text.find(world);
            if (at != std::string::npos && text.find('{') > at) {
                text.replace(at, world.size(), "\"VertexLitGeneric\"");
                data = std::make_shared<const std::vector<uint8_t>>(text.begin(), text.end());
            }
        }
        if (data && !m_loggedFirst.test_and_set(std::memory_order_relaxed)) {
            char message[512]{};
            _snprintf_s(message, sizeof(message), _TRUNCATE,
                "R1Delta: R2 map pack served first material file path=%s pack=%s bytes=%zu\n",
                std::string(normalizedPath).c_str(), path.c_str(), data->size());
            Msg("%s", message);
            OutputDebugStringA(message);
        }
        return data;
    }

private:
    static bool IsModelNamespaceMaterial(std::string_view path)
    {
        constexpr std::string_view prefix = "materials/r2m/";
        return path.size() > prefix.size() + 4 && path.compare(0, prefix.size(), prefix) == 0
            && path.substr(path.size() - 4) == ".vmt";
    }

    // Returns the active map's directory VPK when it holds the file, otherwise empty. packPath receives
    // the path to read inside the pack (the request itself, or for an r2m/ model material the same
    // name without the namespace).
    std::string PackageFor(std::string_view path, std::string& packPath) const
    {
        const bool wanted = path.size() > 4
            && (path.substr(path.size() - 4) == ".vtf" || path.substr(path.size() - 4) == ".vmt"
                || path.substr(path.size() - 4) == ".raw");
        if (!wanted)
            return {};
        const std::string map = ActiveMapName();
        if (!IsAllowlistedMap(map))
            return {};
        const std::filesystem::path package = DirectoryVpkForMap(map);
        if (package.empty() || !IsRegularFile(package))
            return {};
        std::string packageName = package.string();
        packPath.assign(path);
        if (IsModelNamespaceMaterial(path))
            packPath = "materials/" + std::string(path.substr(std::string_view("materials/r2m/").size()));
        return R1OVPK_HasDirectoryVPKFile(packageName.c_str(), packPath.c_str())
            ? packageName : std::string{};
    }

    std::string PackageFor(std::string_view path) const
    {
        std::string packPath;
        return PackageFor(path, packPath);
    }

    mutable std::atomic_flag m_loggedFirst = ATOMIC_FLAG_INIT;
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
    metadata.append("entity-rewrite|");
    metadata.append(std::to_string(entity_rewrite::kRewriteVersion));
    metadata.append("\ntrigger-brush|");
    metadata.append(std::to_string(trigger_brush::kBakeVersion));
    metadata.append("\nconverted-bsp|");
    metadata.append(std::to_string(kConvertedBspCacheVersion));
    metadata.push_back('\n');
    metadata.append("bspdown-variant|");
    metadata.append(R2ConverterVariantString());
    metadata.push_back('\n');

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

// The r1delta game directory that holds cfg/ (the CORE autoexec_delta.cfg lives in <dir>\cfg), or empty when it cannot be found.
std::filesystem::path GetR2GameContentDirectory()
{
    return FindGameContentDirectory();
}

bool ConsumeServedAinPackage()
{
    return g_ainPackageServed.exchange(false);
}

bool IsR2PortMap(std::string_view mapName)
{
    return IsAllowlistedMap(mapName);
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

    // A different fingerprint (new VPK, entity rewrite version or converter version) means the
    // older generated files for this map are stale and are never mounted again; drop them.
    {
        const std::string mapPrefix = state->mapName + "-";
        std::error_code cleanupError;
        for (const auto& entry : std::filesystem::directory_iterator(root.parent_path(), cleanupError)) {
            if (cleanupError)
                break;
            const std::string name = entry.path().filename().string();
            if (name.size() == mapPrefix.size() + fingerprint.size()
                && name.compare(0, mapPrefix.size(), mapPrefix) == 0
                && name != root.filename().string()) {
                std::error_code removeError;
                std::filesystem::remove_all(entry.path(), removeError);
            }
        }
    }
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

bool IsMaterializedMapCopy(std::string_view path)
{
    const auto normalized = NormalizeProviderPath(path);
    if (!normalized)
        return false;
    const auto request = ParseMapRequest(*normalized);
    if (!request || request->kind == MapRequest::Kind::AiGraph)
        return false;
    const auto state = GetMapState(request->mapName);
    const std::string fingerprint = MapPackageFingerprint(state);
    const auto gameDirectory = FindGameContentDirectory();
    if (fingerprint.empty() || gameDirectory.empty())
        return false;
    std::error_code error;
    return std::filesystem::is_regular_file(
        gameDirectory / "r2port_cache" / (state->mapName + "-" + fingerprint)
            / std::filesystem::path(*normalized),
        error);
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
        RegisterR2ModelProvider();
        RegisterR2ParticleProvider();
        InstallR2MapPatchSource();
        postfx::RegisterMaterialProvider();
        auto provider = std::make_shared<R2MapProvider>();
        RegisterProvider("maps", provider);
        RegisterProvider("materials", std::make_shared<R2MapPackFileProvider>());
        RegisterProvider("vpk", std::move(provider));
    });
}
} // namespace r2port
