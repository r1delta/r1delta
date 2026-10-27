#include "r2_shader_cache.h"

#include "r2_shader_compiler.h"
#include "r2_shader_sources.h"

#include "engine/filesystem/r2_map_provider.h"
#include "engine/logging/logging.h"

#include <Windows.h>

#include <cstdarg>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace r2port::shaders {
namespace {

// Bump when the compiler flags, combo layout or VCS container change: it is part of every source hash.
constexpr std::string_view kCacheRevision = "r2shader-2";

struct Entry {
    std::string name;
    bool fromDisk{};
    bool hasPixel{};
    bool hasVertex{};
    bool compiled{};      // compiled in this process (false: cache hit)
    std::string error;
    std::filesystem::path pixelPath;
    std::filesystem::path vertexPath;
    std::filesystem::path vertexColorPath;
};

std::mutex g_mutex;
bool g_built = false;
bool g_unavailableLogged = false;
std::map<std::string, Entry> g_entries;
std::filesystem::path g_cacheDirectory;

bool ResolveInclude(std::string_view name, std::string& text)
{
    if (name == "r2_common.hlsli") {
        text = kCommonInclude;
        return true;
    }
    return false;
}

void Log(const char* format, ...)
{
    char text[2048];
    va_list args;
    va_start(args, format);
    _vsnprintf_s(text, sizeof(text), _TRUNCATE, format, args);
    va_end(args);
    Msg("%s", text);
    OutputDebugStringA(text);
}

void LogLoud(const char* name, const std::string& error)
{
    char head[256];
    _snprintf_s(head, sizeof(head), _TRUNCATE,
        "R1Delta: R2 SHADER BUILD FAILED name=%s (materials that use it will not draw)\n", name);
    Warning("%s", head);
    Warning("%s\n", error.c_str());
    OutputDebugStringA(head);
    OutputDebugStringA(error.c_str());
    OutputDebugStringA("\n");
}

std::string ReadText(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool WriteBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
{
    std::error_code ec;
    std::filesystem::path temp = path;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out.good())
            return false;
    }
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(temp, path, ec);
    return !ec;
}

std::string HashText(std::uint64_t hash)
{
    char text[24];
    _snprintf_s(text, sizeof(text), _TRUNCATE, "%016llx", static_cast<unsigned long long>(hash));
    return text;
}

std::uint64_t SourceHash(const std::string& source)
{
    std::uint64_t hash = HashBytes(kCacheRevision);
    hash = HashBytes(kCommonInclude, hash);
    return HashBytes(source, hash);
}

struct Source {
    std::string name;
    std::string text;
    bool fromDisk{};
};

std::vector<Source> CollectSources(const std::filesystem::path& r2portDirectory)
{
    std::map<std::string, Source> byName;
    for (const EmbeddedShader& shader : kEmbeddedShaders)
        byName[shader.name] = { shader.name, shader.source, false };

    std::error_code ec;
    const std::filesystem::path loose = r2portDirectory / "shaders";
    if (std::filesystem::is_directory(loose, ec)) {
        for (const auto& item : std::filesystem::directory_iterator(loose, ec)) {
            if (!item.is_regular_file(ec) || item.path().extension() != ".hlsl")
                continue;
            std::string name = item.path().stem().string();
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            byName[name] = { name, ReadText(item.path()), true };
        }
    }
    std::vector<Source> sources;
    for (auto& item : byName)
        sources.push_back(std::move(item.second));
    return sources;
}

void BuildOne(const Source& source, bool force)
{
    Entry entry;
    entry.name = source.name;
    entry.fromDisk = source.fromDisk;
    entry.pixelPath = g_cacheDirectory / (source.name + "_ps40.vcs");
    entry.vertexPath = g_cacheDirectory / (source.name + "_vs40.vcs");
    entry.vertexColorPath = g_cacheDirectory / (source.name + "_vcol_vs40.vcs");
    const std::filesystem::path hashPath = g_cacheDirectory / (source.name + ".hash");
    const std::string hash = HashText(SourceHash(source.text));

    std::error_code ec;
    if (!force && ReadText(hashPath) == hash) {
        entry.hasPixel = std::filesystem::is_regular_file(entry.pixelPath, ec);
        entry.hasVertex = std::filesystem::is_regular_file(entry.vertexPath, ec);
        if (entry.hasPixel || entry.hasVertex) {
            g_entries[source.name] = std::move(entry);
            return;
        }
    }

    // Remove stale output first so a failed build can never leave an old shader behind.
    std::filesystem::remove(entry.pixelPath, ec);
    std::filesystem::remove(entry.vertexPath, ec);
    std::filesystem::remove(entry.vertexColorPath, ec);
    std::filesystem::remove(hashPath, ec);

    const BuiltShader built = BuildShader(source.name, source.text, &ResolveInclude);
    entry.compiled = true;
    if (!built.error.empty()) {
        entry.error = built.error;
        LogLoud(source.name.c_str(), built.error);
        g_entries[source.name] = std::move(entry);
        return;
    }
    if (built.hasPixel) {
        const std::string problem = ValidateVcs(built.pixelVcs, 1);
        if (!problem.empty() || !WriteBytes(entry.pixelPath, built.pixelVcs))
            entry.error = problem.empty() ? "could not write the pixel shader file" : "pixel VCS invalid: " + problem;
        entry.hasPixel = entry.error.empty();
    }
    if (entry.error.empty() && built.hasVertex) {
        std::string problem = ValidateVcs(built.vertexVcs, 1);
        if (problem.empty())
            problem = ValidateVcs(built.vertexColorVcs, 1);
        if (!problem.empty() || !WriteBytes(entry.vertexPath, built.vertexVcs)
            || !WriteBytes(entry.vertexColorPath, built.vertexColorVcs))
            entry.error = problem.empty() ? "could not write the vertex shader file" : "vertex VCS invalid: " + problem;
        entry.hasVertex = entry.error.empty();
    }
    if (!entry.error.empty()) {
        entry.hasPixel = false;
        entry.hasVertex = false;
        std::filesystem::remove(entry.pixelPath, ec);
        std::filesystem::remove(entry.vertexPath, ec);
        std::filesystem::remove(entry.vertexColorPath, ec);
        LogLoud(source.name.c_str(), entry.error);
    }
    else {
        std::ofstream(hashPath, std::ios::binary | std::ios::trunc) << hash;
        Log("R1Delta: R2 shader '%s' built (%s%s%s)\n", source.name.c_str(),
            built.hasPixel ? "ps40" : "", built.hasPixel && built.hasVertex ? "+" : "", built.hasVertex ? "vs40" : "");
    }
    g_entries[source.name] = std::move(entry);
}

// Caller holds g_mutex.
void BuildAllLocked(bool force)
{
    g_entries.clear();
    const std::filesystem::path r2portDirectory = GetR2MapPatchDirectory();
    if (r2portDirectory.empty()) {
        if (!g_unavailableLogged) {
            g_unavailableLogged = true;
            Warning("R1Delta: R2 shaders unavailable: game directory not found, custom shader names will not resolve\n");
        }
        g_built = true;
        return;
    }
    g_cacheDirectory = r2portDirectory / "shadercache";
    std::error_code ec;
    std::filesystem::create_directories(g_cacheDirectory, ec);
    if (ec) {
        Warning("R1Delta: R2 shaders unavailable: cannot create %s\n", g_cacheDirectory.string().c_str());
        g_built = true;
        return;
    }
    for (const Source& source : CollectSources(r2portDirectory))
        BuildOne(source, force);
    g_built = true;
}

std::string NormalizeRequest(const char* path)
{
    std::string normalized;
    for (const char* p = path; *p; ++p) {
        char c = *p == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
        normalized.push_back(c);
    }
    return normalized;
}

} // namespace

bool ResolveShaderFile(const char* requestedPath, std::string& absolutePath)
{
    if (!requestedPath)
        return false;
    constexpr std::string_view prefix = "shaders/fxc/";
    constexpr std::string_view extension = ".vcs";
    const std::string normalized = NormalizeRequest(requestedPath);
    if (normalized.size() <= prefix.size() + extension.size()
        || normalized.compare(0, prefix.size(), prefix) != 0
        || normalized.compare(normalized.size() - extension.size(), extension.size(), extension) != 0)
        return false;

    std::string stem = normalized.substr(prefix.size(), normalized.size() - prefix.size() - extension.size());
    bool pixel;
    if (stem.size() > 5 && stem.compare(stem.size() - 5, 5, "_ps40") == 0)
        pixel = true;
    else if (stem.size() > 5 && stem.compare(stem.size() - 5, 5, "_vs40") == 0)
        pixel = false;
    else
        return false;
    stem.resize(stem.size() - 5);
    // `<name>_vcol` is the vertex-colour variant of the vertex shader `<name>`.
    bool vertexColor = false;
    if (!pixel && stem.size() > 5 && stem.compare(stem.size() - 5, 5, "_vcol") == 0) {
        vertexColor = true;
        stem.resize(stem.size() - 5);
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_built)
        BuildAllLocked(false);
    const auto found = g_entries.find(stem);
    if (found == g_entries.end())
        return false;
    const Entry& entry = found->second;
    if (!(pixel ? entry.hasPixel : entry.hasVertex))
        return false;
    absolutePath = (pixel ? entry.pixelPath : vertexColor ? entry.vertexColorPath : entry.vertexPath).string();
    return true;
}

void RebuildAllShaders()
{
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        BuildAllLocked(true);
    }
    PrintShaderStatus();
}

void PrintShaderStatus()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_built)
        BuildAllLocked(false);
    Log("R1Delta: R2 shader cache %s, %zu shaders\n", g_cacheDirectory.string().c_str(), g_entries.size());
    for (const auto& item : g_entries) {
        const Entry& entry = item.second;
        if (!entry.error.empty()) {
            Log("  %-24s FAILED (%s)\n", entry.name.c_str(), entry.fromDisk ? "loose" : "embedded");
            Log("    %s\n", entry.error.c_str());
        }
        else {
            Log("  %-24s ok %s%s %s%s\n", entry.name.c_str(), entry.hasPixel ? "ps40 " : "", entry.hasVertex ? "vs40 " : "",
                entry.fromDisk ? "(loose)" : "(embedded)", entry.compiled ? " (compiled)" : " (cached)");
        }
    }
}

} // namespace r2port::shaders
