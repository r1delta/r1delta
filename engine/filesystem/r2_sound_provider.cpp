#include "r2_sound_provider.h"

#include "engine/filesystem/r2_map_provider.h"
#include "engine/logging/logging.h"
#include "engine/r2port/audio/r2_audio_source.h"
#include "engine/r2port/audio/r2_sound_bank.h"
#include "engine/r2port/audio/r2_sound_scripts.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace r2port {
namespace {

constexpr std::string_view kAliasPath = "scripts/r2port_sounds.txt";
constexpr std::string_view kSoundscapePrefix = "scripts/soundscapes_";
constexpr std::string_view kSoundscapeSuffix = ".txt";

class R2SoundProvider final : public FileProvider {
public:
    bool MayContain(std::string_view path) const override { return Kind(path) != RequestKind::None; }

    // The answer depends on the path and on the R2 install, neither of which changes in a session.
    bool CanCacheMisses() const override { return true; }

    bool Exists(std::string_view path) override { return static_cast<bool>(Read(path)); }

    std::optional<uint64_t> Size(std::string_view path) override
    {
        const FileBytes data = Read(path);
        return data ? std::optional<uint64_t>(data->size()) : std::nullopt;
    }

    FileBytes Read(std::string_view path) override
    {
        const RequestKind kind = Kind(path);
        if (kind == RequestKind::None)
            return {};
        const std::string key(path);
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto found = m_files.find(key);
        if (found != m_files.end())
            return found->second;

        std::string error;
        const r2audio::SoundBank* bank = r2audio::SharedBank(&error);
        std::string text;
        if (bank) {
            if (kind == RequestKind::Aliases) {
                r2audio::AliasReport report;
                text = r2audio::BuildAliasScript(*bank, r2audio::EnabledSoundMaps(), &report);
                char message[512]{};
                _snprintf_s(message, sizeof(message), _TRUNCATE,
                    "R1Delta: R2 sound aliases generated aliases=%u waves=%u skipped-r1=%u unresolved=%u bytes=%zu\n",
                    report.aliases, report.waves, report.skippedR1, report.unresolved, text.size());
                Msg("%s", message);
                OutputDebugStringA(message);
                for (const std::string& name : report.unresolvedNames) {
                    const std::string line = "R1Delta: R2 sound event not resolved: " + name + "\n";
                    Msg("%s", line.c_str());
                }
            }
            else {
                const std::string map(path.substr(kSoundscapePrefix.size(), path.size() - kSoundscapePrefix.size() - kSoundscapeSuffix.size()));
                text = r2audio::BuildSoundscapeScript(*bank, map);
            }
        }
        else if (kind == RequestKind::Aliases && !m_loggedMissing) {
            m_loggedMissing = true;
            const std::string line = "R1Delta: R2 sound scripts not served: " + error + "\n";
            Msg("%s", line.c_str());
        }
        FileBytes data;
        if (!text.empty())
            data = std::make_shared<const std::vector<uint8_t>>(text.begin(), text.end());
        m_files.emplace(key, data);
        return data;
    }

private:
    enum class RequestKind { None, Aliases, Soundscape };

    static RequestKind Kind(std::string_view path)
    {
        if (path == kAliasPath)
            return RequestKind::Aliases;
        if (path.size() > kSoundscapePrefix.size() + kSoundscapeSuffix.size()
            && path.compare(0, kSoundscapePrefix.size(), kSoundscapePrefix) == 0
            && path.compare(path.size() - kSoundscapeSuffix.size(), kSoundscapeSuffix.size(), kSoundscapeSuffix) == 0) {
            const std::string_view map = path.substr(kSoundscapePrefix.size(), path.size() - kSoundscapePrefix.size() - kSoundscapeSuffix.size());
            if (IsR2PortMap(map))
                return RequestKind::Soundscape;
        }
        return RequestKind::None;
    }

    std::mutex m_mutex;
    std::unordered_map<std::string, FileBytes> m_files;
    bool m_loggedMissing = false;
};

std::once_flag g_registration;

} // namespace

void RegisterR2SoundProvider()
{
    std::call_once(g_registration, []() {
        r2audio::SetInstallDirectory(GetR2InstallDirectory());
        RegisterProvider("scripts", std::make_shared<R2SoundProvider>());
    });
}

} // namespace r2port
