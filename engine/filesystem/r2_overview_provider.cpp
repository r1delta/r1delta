#include "r2_overview_provider.h"

#include "engine/filesystem/r1o_vpk.h"
#include "engine/filesystem/r2_map_provider.h"
#include "engine/filesystem/r2_material_provider.h"
#include "engine/logging/logging.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace r2port {
namespace {

// R1's CMapOverview (client.dll sub_180313DA0, on game_newmap) loads resource/overviews/<map>.txt from the GAME path and
// reads material, material_alt, pos_x, pos_y, scale and displayDist (plus optional full_* and zoomOverride_* keys). A map
// without that file gets material index -1 and the HUD draws an empty panel with the entity dots. R2 keeps the same
// world transform (pos_x, pos_y: world position of the image's top left corner, scale: world units per 1/1024 of the
// image width, the same values R1's mp_angel_city file has) but names the image in an assets block ("minimap
// overviews\<map>") instead of material/material_alt.
enum class Kind { Txt, HudMaterial, FullscreenMaterial, LoadScreenMaterial };

struct Request {
    Kind kind{};
    std::string map;
};

constexpr std::string_view kTxtPrefix = "resource/overviews/";
constexpr std::string_view kHudPrefix = "materials/vgui/minimaps/";
constexpr std::string_view kFullscreenPrefix = "materials/vgui/minimaps/fullscreen/";
// The loading screen picture as a UI image ("r2/loadscreens/<map>"): the provider path rules reject "..", so the UI image path
// "../loadscreens/<map>_widescreen" (materials/vgui/../loadscreens/...) is never served for an R2 map.
constexpr std::string_view kUiLoadScreenPrefix = "materials/vgui/r2/loadscreens/";
constexpr std::string_view kUiLoadScreenSuffix = ".vmt";
constexpr std::string_view kLoadScreenPrefix = "materials/loadscreens/";
constexpr std::string_view kLoadScreenSuffix = "_widescreen.vmt";

bool EndsWith(std::string_view text, std::string_view suffix)
{
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::optional<Request> ParseRequest(std::string_view path)
{
    Kind kind{};
    std::string_view prefix;
    std::string_view extension;
    if (path.compare(0, kTxtPrefix.size(), kTxtPrefix) == 0) {
        kind = Kind::Txt;
        prefix = kTxtPrefix;
        extension = ".txt";
    }
    else if (path.compare(0, kFullscreenPrefix.size(), kFullscreenPrefix) == 0) {
        kind = Kind::FullscreenMaterial;
        prefix = kFullscreenPrefix;
        extension = ".vmt";
    }
    else if (path.compare(0, kHudPrefix.size(), kHudPrefix) == 0) {
        kind = Kind::HudMaterial;
        prefix = kHudPrefix;
        extension = ".vmt";
    }
    else if (path.compare(0, kUiLoadScreenPrefix.size(), kUiLoadScreenPrefix) == 0) {
        kind = Kind::LoadScreenMaterial;
        prefix = kUiLoadScreenPrefix;
        extension = kUiLoadScreenSuffix;
    }
    else if (path.compare(0, kLoadScreenPrefix.size(), kLoadScreenPrefix) == 0) {
        kind = Kind::LoadScreenMaterial;
        prefix = kLoadScreenPrefix;
        extension = EndsWith(path, kLoadScreenSuffix) ? kLoadScreenSuffix : std::string_view(".vmt");
    }
    else {
        return std::nullopt;
    }
    if (path.size() <= prefix.size() + extension.size() || !EndsWith(path, extension))
        return std::nullopt;
    const std::string_view map = path.substr(prefix.size(), path.size() - prefix.size() - extension.size());
    if (map.find('/') != std::string_view::npos || !IsR2PortMap(map))
        return std::nullopt;
    return Request{ kind, std::string(map) };
}

std::string LowerAscii(std::string_view value)
{
    std::string out(value);
    for (char& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// KeyValues-style tokens: quoted strings, braces and bare words ("//" comments end at the line break).
std::vector<std::string> Tokenize(std::string_view text)
{
    std::vector<std::string> tokens;
    size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
        }
        else if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            while (i < text.size() && text[i] != '\n')
                ++i;
        }
        else if (c == '{' || c == '}') {
            tokens.emplace_back(1, c);
            ++i;
        }
        else if (c == '"') {
            const size_t end = text.find('"', i + 1);
            const size_t stop = end == std::string_view::npos ? text.size() : end;
            tokens.emplace_back(text.substr(i + 1, stop - i - 1));
            i = stop + 1;
        }
        else {
            size_t end = i;
            while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end]))
                && text[end] != '{' && text[end] != '}' && text[end] != '"')
                ++end;
            tokens.emplace_back(text.substr(i, end - i));
            i = end;
        }
    }
    return tokens;
}

// The value token that follows `key` anywhere in the file (case-insensitive); empty when absent.
std::string FindValue(const std::vector<std::string>& tokens, std::string_view key)
{
    for (size_t i = 0; i + 1 < tokens.size(); ++i) {
        if (LowerAscii(tokens[i]) == LowerAscii(key) && tokens[i + 1] != "{" && tokens[i + 1] != "}")
            return tokens[i + 1];
    }
    return {};
}

// Keeps only characters a number can have, so the generated file cannot be broken by odd R2 text.
bool IsNumber(const std::string& value)
{
    if (value.empty())
        return false;
    for (const char c : value) {
        if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+' || c == '.'))
            return false;
    }
    return true;
}

std::string HudMaterialText(const std::string& map)
{
    return "\"UnlitTwoTexture\"\r\n{\r\n"
        "\t\"$basetexture\" \"vgui\\hud\\threathud_minimap_alpha\"\r\n"
        "\t\"$Texture2\" \"overviews/" + map + "\"\r\n\r\n"
        "\t\"$translucent\" \"1\"\r\n"
        "\t\"$vertexcolor\" \"1\"\r\n"
        "\t\"$vertexalpha\" \"1\"\r\n"
        "\t\"$ignorez\"     \"1\"\r\n\r\n"
        "\t\"Proxies\"\r\n\t{\r\n\t\t\"MiniMap\"\r\n\t\t{\r\n\t\t\t\"resultVar\" \"$texture2transform\"\r\n\t\t}\r\n\t}\r\n}\r\n";
}

// The loading screen picture R1 draws while a map loads (materials/loadscreens/<map>_widescreen, 1920x1080; stock R1 keeps the files in
// mp_common), also served as the UI image "r2/loadscreens/<map>" (materials/vgui/r2/loadscreens/<map>.vmt) that the map picker, the
// lobby, the server browser and the end of game card show for an R2 map. The R2 rpak texture "loadscreens\<map>_widescreen"
// (<map>_loadscreen*.rpak, the latest patch wins) is converted by the R2 material provider; the VMT is the one of the stock R1 loadscreens.
std::string LoadScreenMaterialText(const std::string& map)
{
    return "\"Basic\"\r\n{\r\n"
        "\t\"$basetexture\" \"loadscreens/" + map + "_widescreen\"\r\n"
        "\t\"$vertexcolor\" 1\r\n"
        "\t\"$vertexalpha\" 1\r\n"
        "\t\"$ignorez\" 1\r\n"
        "\t\"$no_fullbright\" \"1\"\r\n"
        "\t\"$nolod\" \"1\"\r\n"
        "\t\"$SHADERSRGBREAD360\" 1\r\n}\r\n";
}

std::string FullscreenMaterialText(const std::string& map)
{
    return "\"UnlitTwoTexture\"\r\n{\r\n"
        "\t\"$basetexture\" \"ui\\menu\\spectator\\spectatorHUDAssets\\spectator_map_mask\"\r\n"
        "\t\"$Texture2\" \"overviews/" + map + "\"\r\n\r\n"
        "\t\"$translucent\" \"1\"\r\n"
        "\t\"$vertexcolor\" \"1\"\r\n"
        "\t\"$vertexalpha\" \"1\"\r\n"
        "\t\"$ignorez\"     \"1\"\r\n\r\n"
        "\t\"Proxies\"\r\n\t{\r\n\t\t\"MiniMap\"\r\n\t\t{\r\n\t\t\t\"resultVar\" \"$texture2transform\"\r\n\t\t}\r\n\t}\r\n}\r\n";
}

class R2OverviewProvider final : public FileProvider {
public:
    bool MayContain(std::string_view normalizedPath) const override
    {
        return ParseRequest(normalizedPath).has_value();
    }

    // The answer depends on the path alone (the allowlist) and the R2 install, which does not change in a session.
    bool CanCacheMisses() const override { return true; }

    bool Exists(std::string_view normalizedPath) override { return static_cast<bool>(Read(normalizedPath)); }

    std::optional<uint64_t> Size(std::string_view normalizedPath) override
    {
        const FileBytes data = Read(normalizedPath);
        return data ? std::optional<uint64_t>(data->size()) : std::nullopt;
    }

    FileBytes Read(std::string_view normalizedPath) override
    {
        const auto request = ParseRequest(normalizedPath);
        if (!request)
            return {};
        const std::string path(normalizedPath);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const auto found = m_files.find(path);
            if (found != m_files.end())
                return found->second;
        }

        std::string text;
        switch (request->kind) {
        case Kind::Txt:
            text = BuildOverviewText(request->map);
            break;
        case Kind::HudMaterial:
            text = HasOverviewData(request->map) ? HudMaterialText(request->map) : std::string();
            break;
        case Kind::FullscreenMaterial:
            text = HasOverviewData(request->map) ? FullscreenMaterialText(request->map) : std::string();
            break;
        case Kind::LoadScreenMaterial:
            text = HasLoadScreenPak(request->map) ? LoadScreenMaterialText(request->map) : std::string();
            break;
        }
        if (text.empty())
            return {};

        const FileBytes data = std::make_shared<const std::vector<uint8_t>>(text.begin(), text.end());
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_files.emplace(path, data);
        }
        if (request->kind == Kind::LoadScreenMaterial) {
            // R1 asks for the VMT first (the loading screen, or the UI image in the map picker/lobby before any R2 map was
            // loaded); the VTF is a known path of the R2 material provider from here on and converted in the background.
            PrefetchR2UiTextures({ "materials/loadscreens/" + request->map + "_widescreen.vtf" });
        }
        if (!m_logged.exchange(true, std::memory_order_relaxed)) {
            char message[512]{};
            _snprintf_s(message, sizeof(message), _TRUNCATE,
                "R1Delta: R2 overview served first path=%s bytes=%zu\n", path.c_str(), data->size());
            Msg("%s", message);
            OutputDebugStringA(message);
        }
        return data;
    }

private:
    // The install has a loading screen package of the map (<map>_loadscreen.rpak, or <map>_loadscreen(NN).rpak once patched).
    static bool HasLoadScreenPak(const std::string& map)
    {
        const std::filesystem::path install = GetR2InstallDirectory();
        if (install.empty())
            return false;
        const std::string prefix = LowerAscii(map + "_loadscreen");
        std::error_code error;
        for (std::filesystem::directory_iterator it(install / "r2" / "paks" / "Win64", error), end; !error && it != end; it.increment(error)) {
            const std::string name = LowerAscii(it->path().filename().string());
            if (name.compare(0, prefix.size(), prefix) == 0 && EndsWith(name, ".rpak"))
                return true;
        }
        return false;
    }

    static std::string PackagePath(const std::string& map)
    {
        const std::filesystem::path install = GetR2InstallDirectory();
        if (install.empty())
            return {};
        const std::filesystem::path package = install / "vpk" / ("englishclient_" + map + ".bsp.pak000_dir.vpk");
        std::error_code error;
        return std::filesystem::is_regular_file(package, error) ? package.string() : std::string();
    }

    static FileBytes ReadR2OverviewFile(const std::string& map)
    {
        const std::string package = PackagePath(map);
        if (package.empty())
            return {};
        const std::string path = "resource/overviews/" + map + ".txt";
        return R1OVPK_ReadDirectoryVPKFile(package.c_str(), path.c_str());
    }

    bool HasOverviewData(const std::string& map)
    {
        return static_cast<bool>(Read(std::string(kTxtPrefix) + map + ".txt"));
    }

    static std::string BuildOverviewText(const std::string& map)
    {
        const FileBytes source = ReadR2OverviewFile(map);
        if (!source)
            return {};
        const std::vector<std::string> tokens = Tokenize(std::string_view(
            reinterpret_cast<const char*>(source->data()), source->size()));
        const std::string posX = FindValue(tokens, "pos_x");
        const std::string posY = FindValue(tokens, "pos_y");
        const std::string scale = FindValue(tokens, "scale");
        std::string displayDist = FindValue(tokens, "displayDist");
        if (!IsNumber(posX) || !IsNumber(posY) || !IsNumber(scale))
            return {};
        // Every R2 file but mp_coliseum's carries displayDist; 2048 is the value of the other 19 and of R1's own maps.
        if (!IsNumber(displayDist))
            displayDist = "2048";

        std::string text = "\"" + map + "\"\r\n{\r\n";
        text += "\tmaterial\t\tvgui/minimaps/" + map + "\r\n";
        text += "\tmaterial_alt\tvgui/minimaps/fullscreen/" + map + "\r\n";
        text += "\tpos_x\t\t" + posX + "\r\n";
        text += "\tpos_y\t\t" + posY + "\r\n";
        text += "\tscale\t\t" + scale + "\r\n";
        text += "\tdisplayDist\t" + displayDist + "\r\n";
        text += "}\r\n";
        return text;
    }

    std::mutex m_mutex;
    std::unordered_map<std::string, FileBytes> m_files;
    std::atomic<bool> m_logged{};
};

std::once_flag g_registration;

} // namespace

void RegisterR2OverviewProvider()
{
    std::call_once(g_registration, []() {
        auto provider = std::make_shared<R2OverviewProvider>();
        RegisterProvider("resource", provider);
        RegisterProvider("materials", std::move(provider));
    });
}

} // namespace r2port
