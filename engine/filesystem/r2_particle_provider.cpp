#include "r2_particle_provider.h"

#include "engine/filesystem/r1o_vpk.h"
#include "engine/filesystem/r2_map_provider.h"
#include "engine/logging/logging.h"
#include "engine/r2port/particles/pcfconv.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace r2port {
namespace {

#include "engine/r2port/particles/r2_particle_requests.inc"

constexpr std::string_view kPcfPath = kR2ParticlePcfPath;
constexpr std::string_view kParticleMaterialPrefix = "materials/particle/";
constexpr const char* kFrontendPack = "englishclient_frontend.bsp.pak000_dir.vpk";
constexpr const char* kCommonPack = "englishclient_mp_common.bsp.pak000_dir.vpk";

void LogParticleMessage(const char* message)
{
    Msg("%s", message);
    OutputDebugStringA(message);
}

std::string LowerAscii(std::string_view value)
{
    std::string out(value);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return out;
}
// Differential switch for A/B checks: R1DELTA_R2PORT_NO_PARTICLES=1 converts and renames nothing.
bool ParticlesDisabled()
{
    static const bool disabled = []() {
        char value[8]{};
        return GetEnvironmentVariableA("R1DELTA_R2PORT_NO_PARTICLES", value, sizeof(value)) && value[0] == '1';
    }();
    return disabled;
}


const r2particles::Options& Options()
{
    static const r2particles::Options options;
    return options;
}

const std::unordered_map<std::string, std::string>& TargetNames()
{
    static const std::unordered_map<std::string, std::string> names = []() {
        std::unordered_map<std::string, std::string> map;
        for (const ParticleRequest& request : kParticleRequests)
            map.emplace(LowerAscii(request.system), r2particles::TargetName(Options(), request.system));
        return map;
    }();
    return names;
}

std::string PackPath(const char* name)
{
    const std::filesystem::path install = GetR2InstallDirectory();
    if (install.empty())
        return {};
    const std::filesystem::path pack = install / "vpk" / name;
    std::error_code error;
    return std::filesystem::is_regular_file(pack, error) ? pack.string() : std::string();
}

// Builds particles/r2port.pcf once; later calls return the same bytes (or the same failure).
class R2ParticleProvider final : public FileProvider {
public:
    bool MayContain(std::string_view normalizedPath) const override
    {
        return normalizedPath == kPcfPath && static_cast<bool>(Pcf());
    }

    bool Exists(std::string_view normalizedPath) override { return MayContain(normalizedPath); }

    std::optional<std::uint64_t> Size(std::string_view normalizedPath) override
    {
        const FileBytes file = normalizedPath == kPcfPath ? Pcf() : FileBytes{};
        return file ? std::optional<std::uint64_t>(file->size()) : std::nullopt;
    }

    FileBytes Read(std::string_view normalizedPath) override
    {
        return normalizedPath == kPcfPath ? Pcf() : FileBytes{};
    }

    FileBytes Pcf() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_built) {
            m_built = true;
            m_pcf = Build();
        }
        return m_pcf;
    }

private:
    static FileBytes Build()
    {
        const auto started = std::chrono::steady_clock::now();
        const std::string pack = PackPath(kFrontendPack);
        if (pack.empty())
            return {};

        std::vector<r2particles::SourcePcf> sources;
        std::vector<r2particles::Request> requests;
        std::unordered_set<std::string> seen;
        for (const ParticleRequest& request : kParticleRequests) {
            requests.push_back({ request.system, request.file });
            if (!seen.insert(request.file).second)
                continue;
            const std::string path = std::string("particles/") + request.file;
            const FileBytes bytes = R1OVPK_ReadDirectoryVPKFile(pack.c_str(), path.c_str());
            if (!bytes)
                continue;
            r2particles::SourcePcf source;
            source.name = request.file;
            source.bytes = *bytes;
            sources.push_back(std::move(source));
        }

        std::vector<std::uint8_t> output;
        r2particles::Report report;
        std::string error;
        if (!r2particles::ConvertPcf(sources, requests, Options(), output, report, error)) {
            char message[512]{};
            _snprintf_s(message, sizeof(message), _TRUNCATE,
                "R1Delta: R2 particle conversion failed: %s\n", error.c_str());
            LogParticleMessage(message);
            return {};
        }

        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        char message[512]{};
        _snprintf_s(message, sizeof(message), _TRUNCATE,
            "R1Delta: R2 particles converted files=%zu systems=%zu bytes=%zu dropped-functions=%zu approximated=%zu dropped-attributes=%zu missing=%zu time=%lld ms\n",
            sources.size(), report.systems.size(), output.size(), report.droppedFunctions,
            report.approximatedFunctions, report.droppedAttributes, report.missingSystems,
            static_cast<long long>(elapsed));
        LogParticleMessage(message);
        for (const std::string& note : report.notes) {
            char line[512]{};
            _snprintf_s(line, sizeof(line), _TRUNCATE, "R1Delta: R2 particle note: %s\n", note.c_str());
            LogParticleMessage(line);
        }
        return std::make_shared<const std::vector<std::uint8_t>>(std::move(output));
    }

    mutable std::mutex m_mutex;
    mutable bool m_built = false;
    mutable FileBytes m_pcf;
};

// materials/particle/** files that R1 lacks and the R2 mp_common VPK carries. (Map-specific ones are
// answered by R2MapPackFileProvider.) R1 files win because the registry is only consulted on a miss.
class R2ParticleMaterialProvider final : public FileProvider {
public:
    bool MayContain(std::string_view normalizedPath) const override
    {
        if (normalizedPath.substr(0, kParticleMaterialPrefix.size()) != kParticleMaterialPrefix)
            return false;
        const auto index = Index();
        return index && index->find(std::string(normalizedPath)) != index->end();
    }

    bool CanCacheMisses() const override { return static_cast<bool>(Index()); }

    bool Exists(std::string_view normalizedPath) override { return MayContain(normalizedPath); }

    std::optional<std::uint64_t> Size(std::string_view normalizedPath) override
    {
        const FileBytes data = Read(normalizedPath);
        return data ? std::optional<std::uint64_t>(data->size()) : std::nullopt;
    }

    FileBytes Read(std::string_view normalizedPath) override
    {
        if (!MayContain(normalizedPath))
            return {};
        const std::string path(normalizedPath);
        const FileBytes data = R1OVPK_ReadDirectoryVPKFile(m_pack.c_str(), path.c_str());
        if (data && !m_logged.exchange(true, std::memory_order_relaxed)) {
            char message[512]{};
            _snprintf_s(message, sizeof(message), _TRUNCATE,
                "R1Delta: R2 particle material served first path=%s bytes=%zu\n", path.c_str(), data->size());
            LogParticleMessage(message);
        }
        return data;
    }

private:
    using PathSet = std::unordered_set<std::string>;

    std::shared_ptr<const PathSet> Index() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_built) {
            m_built = true;
            m_pack = PackPath(kCommonPack);
            std::vector<std::string> paths;
            if (!m_pack.empty() && R1OVPK_EnumerateDirectoryVPKFiles(m_pack.c_str(), &paths)) {
                auto set = std::make_shared<PathSet>();
                for (std::string& path : paths) {
                    path = LowerAscii(path);
                    if (path.compare(0, kParticleMaterialPrefix.size(), kParticleMaterialPrefix) == 0)
                        set->insert(std::move(path));
                }
                m_index = std::move(set);
            }
        }
        return m_index;
    }

    mutable std::mutex m_mutex;
    mutable bool m_built = false;
    mutable std::string m_pack;
    mutable std::shared_ptr<const PathSet> m_index;
    std::atomic<bool> m_logged{};
};

std::shared_ptr<R2ParticleProvider> g_provider;
std::once_flag g_registration;

} // namespace

void RegisterR2ParticleProvider()
{
    std::call_once(g_registration, []() {
        if (ParticlesDisabled())
            return;
        auto provider = std::make_shared<R2ParticleProvider>();
        if (RegisterProvider("particles", provider))
            g_provider = std::move(provider);
        RegisterProvider("materials", std::make_shared<R2ParticleMaterialProvider>());
    });
}

bool R2ParticlesAvailable()
{
    return g_provider && static_cast<bool>(g_provider->Pcf());
}

bool R2ParticleTargetName(std::string_view r2System, std::string& target)
{
    if (ParticlesDisabled())
        return false;
    const auto& names = TargetNames();
    const auto it = names.find(LowerAscii(r2System));
    if (it == names.end())
        return false;
    target = it->second;
    return true;
}

} // namespace r2port
