#include "script_modding.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace ScriptModding
{
namespace
{

constexpr std::string_view kScriptsPrefix = "scripts/vscripts/";
constexpr std::string_view kResourcePrefix = "resource/";
constexpr std::string_view kModulePrefixes[] = { "bin/", "bin_nexon/", "bin_delta/" };
constexpr std::string_view kR1DeltaDirectory = "/r1delta/";

// Files retail Titanfall installs next to the game (bin/x64_retail and r1/bin/x64_retail).
constexpr std::array<std::string_view, 23> kRetailModules = { {
    "adminserver.dll", "bink2w64.dll", "bsppack.dll", "client.dll", "datacache.dll",
    "dedicated.dll", "engine.dll", "filesystem_stdio.dll", "inputsystem.dll", "launcher.dll",
    "localize.dll", "materialsystem_dx11.dll", "materialsystem_nodx.dll", "scenefilecache.dll",
    "server.dll", "studiorender.dll", "tier0.dll", "valve_avi.dll", "vaudio_speex.dll",
    "vgui2.dll", "vguimatsurface.dll", "vphysics.dll", "vstdlib.dll",
} };

char ToLower(char ch)
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
}

bool StartsWith(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

bool EndsWith(std::string_view text, std::string_view suffix)
{
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

bool IsLowerHexDigest(std::string_view text)
{
    return text.size() == kSha256HexLength
        && std::all_of(text.begin(), text.end(), [](char ch) {
               return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
           });
}

bool IsCleanRelativePath(std::string_view path)
{
    if (path.empty() || path.front() == '/' || path.find('\\') != std::string_view::npos
        || path.find(':') != std::string_view::npos) {
        return false;
    }
    std::size_t start = 0;
    while (start <= path.size()) {
        std::size_t end = path.find('/', start);
        if (end == std::string_view::npos)
            end = path.size();
        const std::string_view part = path.substr(start, end - start);
        if (part.empty() || part == "." || part == "..")
            return false;
        start = end + 1;
    }
    return std::none_of(path.begin(), path.end(), [](char ch) {
        return ch >= 'A' && ch <= 'Z';
    });
}

std::string_view StripCarriageReturn(std::string_view line)
{
    if (!line.empty() && line.back() == '\r')
        line.remove_suffix(1);
    return line;
}

} // namespace

std::vector<std::string_view> ReasonNames(ReasonMask reasons)
{
    std::vector<std::string_view> names;
    if (reasons & kReasonAddons)
        names.push_back("addons");
    if (reasons & kReasonCore)
        names.push_back("core");
    if (reasons & kReasonNative)
        names.push_back("native");
    return names;
}

std::string ReasonText(ReasonMask reasons)
{
    std::string text;
    for (const std::string_view name : ReasonNames(reasons)) {
        if (!text.empty())
            text += ',';
        text += name;
    }
    return text.empty() ? std::string("none") : text;
}

std::string NormalizePath(std::string_view path)
{
    std::string normalized;
    normalized.reserve(path.size());
    for (const char ch : path)
        normalized.push_back(ch == '\\' ? '/' : ToLower(ch));
    while (StartsWith(normalized, "./"))
        normalized.erase(0, 2);
    while (!normalized.empty() && normalized.front() == '/')
        normalized.erase(0, 1);
    return normalized;
}

bool IsTrackedContentPath(std::string_view normalizedPath)
{
    if (StartsWith(normalizedPath, kScriptsPrefix))
        return normalizedPath.size() > kScriptsPrefix.size();
    return StartsWith(normalizedPath, kResourcePrefix)
        && (EndsWith(normalizedPath, ".res") || EndsWith(normalizedPath, ".menu"));
}

bool IsTrackedModulePath(std::string_view normalizedPath)
{
    if (!EndsWith(normalizedPath, ".dll"))
        return false;
    return std::any_of(std::begin(kModulePrefixes), std::end(kModulePrefixes),
        [&](std::string_view prefix) { return StartsWith(normalizedPath, prefix); });
}

bool ParseManifest(std::string_view text, Manifest& manifest, std::string* error)
{
    const auto fail = [&](std::size_t lineNumber, const char* reason) {
        if (error)
            *error = "line " + std::to_string(lineNumber) + ": " + reason;
        return false;
    };

    Manifest parsed;
    bool sawHeader = false;
    std::size_t lineNumber = 0;
    while (!text.empty()) {
        ++lineNumber;
        const std::size_t newline = text.find('\n');
        const std::string_view line = StripCarriageReturn(text.substr(0, newline));
        text = newline == std::string_view::npos ? std::string_view() : text.substr(newline + 1);
        if (line.empty())
            continue;

        if (!sawHeader) {
            if (line != kManifestHeader)
                return fail(lineNumber, "missing or unsupported header");
            sawHeader = true;
            continue;
        }

        // "<S|N> <64 hex> <path>"
        if (line.size() < 3 + kSha256HexLength + 1 || line[1] != ' '
            || line[2 + kSha256HexLength] != ' ') {
            return fail(lineNumber, "malformed entry");
        }
        const char kind = line[0];
        const std::string_view hash = line.substr(2, kSha256HexLength);
        const std::string_view path = line.substr(3 + kSha256HexLength);
        if (!IsLowerHexDigest(hash))
            return fail(lineNumber, "hash is not 64 lowercase hex digits");
        if (!IsCleanRelativePath(path))
            return fail(lineNumber, "path is not a clean lowercase relative path");

        if (kind == 'S') {
            if (!IsTrackedContentPath(path))
                return fail(lineNumber, "content path outside scripts/vscripts and resource");
            if (!parsed.content.emplace(std::string(path), std::string(hash)).second)
                return fail(lineNumber, "duplicate content path");
        }
        else if (kind == 'N') {
            if (!IsTrackedModulePath(path))
                return fail(lineNumber, "module path outside bin, bin_nexon and bin_delta");
            if (!parsed.modules.emplace(std::string(path), std::string(hash)).second)
                return fail(lineNumber, "duplicate module path");
        }
        else {
            return fail(lineNumber, "unknown entry kind");
        }
    }

    if (!sawHeader) {
        if (error)
            *error = "empty manifest";
        return false;
    }
    if (parsed.content.empty()) {
        if (error)
            *error = "manifest has no content entries";
        return false;
    }
    manifest = std::move(parsed);
    return true;
}

std::string FrameContentPath(std::string_view frameSource)
{
    const std::string normalized = NormalizePath(frameSource);
    if (normalized.empty() || normalized.find("..") != std::string::npos
        || normalized.find(':') != std::string::npos
        || normalized.find('.') == std::string::npos) {
        return {};
    }
    return std::string(kScriptsPrefix) + normalized;
}

FileState CompareContentFile(const Manifest& manifest, std::string_view normalizedPath,
    std::string_view sha256)
{
    const auto entry = manifest.content.find(std::string(normalizedPath));
    if (sha256.empty())
        return FileState::Missing;
    if (entry == manifest.content.end())
        return FileState::Added;
    return entry->second == sha256 ? FileState::Stock : FileState::Modified;
}

FrameVerdict ClassifyFrame(const Manifest& manifest, std::string_view frameSource,
    const FrameFacts& facts)
{
    FrameVerdict verdict;
    const std::string path = FrameContentPath(frameSource);
    if (path.empty())
        return verdict;

    if (facts.addonProvides && facts.addonProvides(path))
        verdict.reasons |= kReasonAddons;

    std::string hash;
    if (facts.hashCoreFile && facts.hashCoreFile(path, hash)) {
        const FileState state = CompareContentFile(manifest, path, hash);
        if (state == FileState::Modified || state == FileState::Added)
            verdict.reasons |= kReasonCore;
    }

    verdict.modded = verdict.reasons != 0;
    return verdict;
}

bool IsRetailModuleName(std::string_view lowerName)
{
    return std::find(kRetailModules.begin(), kRetailModules.end(), lowerName) != kRetailModules.end();
}

ModuleKind ClassifyModule(const ModuleInfo& module, const ModuleContext& context)
{
    const std::string path = NormalizePath(module.path);
    const std::string name = NormalizePath(module.name);

    if (!context.systemRoot.empty() && StartsWith(path, context.systemRoot + "/"))
        return ModuleKind::System;

    const std::size_t r1deltaPosition = path.rfind(kR1DeltaDirectory);
    const bool inR1DeltaTree = r1deltaPosition != std::string::npos;

    if (!context.manifest)
        return !inR1DeltaTree && IsRetailModuleName(name) ? ModuleKind::Retail : ModuleKind::Unverifiable;

    if (inR1DeltaTree) {
        const std::string relative = path.substr(r1deltaPosition + kR1DeltaDirectory.size());
        const auto entry = context.manifest->modules.find(relative);
        if (entry != context.manifest->modules.end()) {
            if (!context.hashFile)
                return ModuleKind::Shipped;
            std::string hash;
            if (!context.hashFile(module.path, hash))
                return ModuleKind::Unverifiable;
            return hash == entry->second ? ModuleKind::Shipped : ModuleKind::Modified;
        }
        return ModuleKind::ForeignGame;
    }

    if (IsRetailModuleName(name))
        return ModuleKind::Retail;

    const bool inGameTree = !context.gameRoot.empty() && StartsWith(path, context.gameRoot + "/");
    return inGameTree ? ModuleKind::ForeignGame : ModuleKind::ForeignOther;
}

bool IsNativeModdingKind(ModuleKind kind)
{
    return kind == ModuleKind::Modified || kind == ModuleKind::ForeignGame;
}

} // namespace ScriptModding
