#include "r1_map_pack_material_provider.h"

#include "engine/filesystem/r1o_vpk.h"
#include "engine/filesystem/r2port_provider.h"
#include "engine/logging/crash_report_context.h"
#include "engine/logging/logging.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace r2port {
namespace {

constexpr std::string_view kMaterialsPrefix = "materials/";
constexpr std::string_view kSkyboxMaterial = "materials/tools/toolsskybox.vmt";
constexpr std::string_view kZiplineMaterial = "materials/cable/zipline.vmt";

void LogMapPackMessage(const char* message)
{
    Msg("%s", message);
    OutputDebugStringA(message);
}

std::uint64_t HashPath(std::string_view path)
{
    std::uint64_t hash = 14695981039346656037ull;
    for (const unsigned char c : path) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash;
}

bool EqualPath(const std::string& stored, std::string_view requested)
{
    return stored.size() == requested.size()
        && std::equal(stored.begin(), stored.end(), requested.begin());
}

struct AssetEntry {
    std::uint64_t hash{};
    std::size_t packIndex{};
    std::string path;
};

class MaterialPackIndex {
public:
    std::size_t AddPack(std::string path)
    {
        m_packs.push_back(std::move(path));
        return m_packs.size() - 1;
    }

    void Insert(std::string path, std::size_t packIndex)
    {
        if (path.size() <= kMaterialsPrefix.size()
            || path.compare(0, kMaterialsPrefix.size(), kMaterialsPrefix) != 0)
            return;

        if (m_entries.empty())
            Rehash(64);
        else if ((m_size + 1) * 2 >= m_entries.size())
            Rehash(m_entries.size() * 2);

        const std::uint64_t hash = HashPath(path);
        const std::size_t mask = m_entries.size() - 1;
        std::size_t slotIndex = static_cast<std::size_t>(hash) & mask;
        for (;;) {
            AssetEntry& entry = m_entries[slotIndex];
            if (entry.path.empty()) {
                entry.hash = hash;
                entry.packIndex = packIndex;
                entry.path = std::move(path);
                ++m_size;
                return;
            }
            if (entry.hash == hash && EqualPath(entry.path, path))
                return;
            slotIndex = (slotIndex + 1) & mask;
        }
    }

    const AssetEntry* Find(std::string_view path) const
    {
        if (m_entries.empty() || path.size() <= kMaterialsPrefix.size()
            || path.compare(0, kMaterialsPrefix.size(), kMaterialsPrefix) != 0)
            return nullptr;

        const std::uint64_t hash = HashPath(path);
        const std::size_t mask = m_entries.size() - 1;
        std::size_t slotIndex = static_cast<std::size_t>(hash) & mask;
        for (std::size_t probes = 0; probes < m_entries.size(); ++probes) {
            const AssetEntry& entry = m_entries[slotIndex];
            if (entry.path.empty())
                return nullptr;
            if (entry.hash == hash && EqualPath(entry.path, path))
                return &entry;
            slotIndex = (slotIndex + 1) & mask;
        }
        return nullptr;
    }

    const std::string* PackPath(const AssetEntry& entry) const
    {
        return entry.packIndex < m_packs.size() ? &m_packs[entry.packIndex] : nullptr;
    }

    std::size_t AssetCount() const { return m_size; }

private:
    void Rehash(std::size_t capacity)
    {
        std::vector<AssetEntry> previous = std::move(m_entries);
        m_entries.clear();
        m_entries.resize(capacity);
        m_size = 0;
        for (AssetEntry& entry : previous) {
            if (entry.path.empty())
                continue;
            const std::size_t mask = m_entries.size() - 1;
            std::size_t slotIndex = static_cast<std::size_t>(entry.hash) & mask;
            while (!m_entries[slotIndex].path.empty())
                slotIndex = (slotIndex + 1) & mask;
            m_entries[slotIndex] = std::move(entry);
            ++m_size;
        }
    }

    std::vector<AssetEntry> m_entries;
    std::vector<std::string> m_packs;
    std::size_t m_size{};
};

struct PackFile {
    std::string sortName;
    std::string path;
};

std::string LowerAscii(std::string value)
{
    for (char& c : value) {
        const unsigned char valueByte = static_cast<unsigned char>(c);
        if (valueByte >= 'A' && valueByte <= 'Z')
            c = static_cast<char>(valueByte + ('a' - 'A'));
    }
    return value;
}

class R1MapPackMaterialsProvider final : public FileProvider {
public:
    ~R1MapPackMaterialsProvider() override
    {
        if (m_indexThread.joinable())
            m_indexThread.join();
    }

    void StartIndex() noexcept
    {
        try {
            m_indexThread = std::thread([this]() {
                RegisterR1DeltaThread("R1MapPackMaterialIndex");
                BuildIndex();
            });
        }
        catch (...) {
            LogMapPackMessage(
                "R1Delta: R1 map-pack material index could not start its background worker\n");
        }
    }

    bool MayContain(std::string_view normalizedPath) const override
    {
        const auto index = m_index.load(std::memory_order_acquire);
        return index && index->Find(normalizedPath);
    }

    bool CanCacheMisses() const override
    {
        return m_index.load(std::memory_order_acquire) != nullptr;
    }

    bool Exists(std::string_view normalizedPath) override
    {
        const auto index = m_index.load(std::memory_order_acquire);
        if (!index)
            return false;
        const AssetEntry* entry = index->Find(normalizedPath);
        const std::string* pack = entry ? index->PackPath(*entry) : nullptr;
        return pack && R1OVPK_HasDirectoryVPKFile(pack->c_str(), std::string(normalizedPath).c_str());
    }

    std::optional<std::uint64_t> Size(std::string_view normalizedPath) override
    {
        const auto index = m_index.load(std::memory_order_acquire);
        if (!index)
            return std::nullopt;
        const AssetEntry* entry = index->Find(normalizedPath);
        const std::string* pack = entry ? index->PackPath(*entry) : nullptr;
        if (!pack)
            return std::nullopt;
        std::uint64_t size{};
        return R1OVPK_GetDirectoryVPKFileSize(
            pack->c_str(), std::string(normalizedPath).c_str(), &size)
            ? std::optional<std::uint64_t>(size) : std::nullopt;
    }

    FileBytes Read(std::string_view normalizedPath) override
    {
        const auto index = m_index.load(std::memory_order_acquire);
        if (!index)
            return {};
        const AssetEntry* entry = index->Find(normalizedPath);
        const std::string* pack = entry ? index->PackPath(*entry) : nullptr;
        if (!pack)
            return {};

        FileBytes data = R1OVPK_ReadDirectoryVPKFile(
            pack->c_str(), std::string(normalizedPath).c_str());
        if (data)
            LogServedAsset(normalizedPath, *pack, data->size());
        return data;
    }

private:
    void BuildIndex() noexcept
    {
        const auto started = std::chrono::steady_clock::now();
        try {
            std::error_code error;
            const std::filesystem::path vpkDirectory = std::filesystem::current_path(error) / "vpk";
            if (error || !std::filesystem::is_directory(vpkDirectory, error) || error) {
                LogMapPackMessage(
                    "R1Delta: R1 map-pack material index unavailable; current vpk directory was not found\n");
                return;
            }

            std::vector<PackFile> packages;
            for (std::filesystem::directory_iterator it(vpkDirectory, error), end;
                !error && it != end; it.increment(error)) {
                if (!it->is_regular_file(error) || error)
                    continue;
                std::string name = LowerAscii(it->path().filename().string());
                if (name.find("client_mp_") == std::string::npos
                    || name.find(".bsp.pak000_dir.vpk") == std::string::npos
                    || name.find("client_mp_common") != std::string::npos)
                    continue;
                packages.push_back({ std::move(name), it->path().string() });
            }
            std::sort(packages.begin(), packages.end(), [](const PackFile& left, const PackFile& right) {
                return left.sortName < right.sortName;
            });

            auto built = std::make_shared<MaterialPackIndex>();
            std::size_t parsedPacks{};
            std::size_t failedPacks{};
            for (const PackFile& package : packages) {
                std::vector<std::string> paths;
                if (!R1OVPK_EnumerateDirectoryVPKFiles(package.path.c_str(), &paths)) {
                    ++failedPacks;
                    continue;
                }
                const std::size_t packIndex = built->AddPack(package.path);
                ++parsedPacks;
                for (std::string& path : paths)
                    built->Insert(std::move(path), packIndex);
            }

            const std::size_t assetCount = built->AssetCount();
            std::shared_ptr<const MaterialPackIndex> published = std::move(built);
            m_index.store(std::move(published), std::memory_order_release);

            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            char message[512]{};
            _snprintf_s(message, sizeof(message), _TRUNCATE,
                "R1Delta: R1 map-pack material index ready directory=%s packs=%zu parsed=%zu failed=%zu materials=%zu time=%lld ms\n",
                vpkDirectory.string().c_str(), packages.size(), parsedPacks, failedPacks,
                assetCount, static_cast<long long>(elapsed));
            LogMapPackMessage(message);
        }
        catch (...) {
            LogMapPackMessage(
                "R1Delta: R1 map-pack material index failed while scanning R1 VPKs\n");
        }
    }

    void LogServedAsset(std::string_view path, const std::string& pack, std::size_t bytes)
    {
        std::atomic<bool>* logged{};
        const char* name{};
        if (path == kSkyboxMaterial) {
            logged = &m_loggedSkybox;
            name = "toolsskybox";
        }
        else if (path == kZiplineMaterial) {
            logged = &m_loggedZipline;
            name = "zipline";
        }
        if (!logged)
            return;
        bool expected = false;
        if (!logged->compare_exchange_strong(expected, true, std::memory_order_relaxed))
            return;

        char message[1024]{};
        _snprintf_s(message, sizeof(message), _TRUNCATE,
            "R1Delta: R1 map-pack material served asset=%s path=%.*s pack=%s bytes=%zu\n",
            name, static_cast<int>(path.size()), path.data(), pack.c_str(), bytes);
        LogMapPackMessage(message);
    }

    std::atomic<std::shared_ptr<const MaterialPackIndex>> m_index;
    std::thread m_indexThread;
    std::atomic<bool> m_loggedSkybox{};
    std::atomic<bool> m_loggedZipline{};
};

std::shared_ptr<R1MapPackMaterialsProvider> g_provider;
std::once_flag g_providerRegistration;

} // namespace

void RegisterR1MapPackMaterialProvider()
{
    std::call_once(g_providerRegistration, []() {
        auto provider = std::make_shared<R1MapPackMaterialsProvider>();
        if (!RegisterProvider("materials", provider)) {
            LogMapPackMessage("R1Delta: R1 map-pack material provider could not register\n");
            return;
        }
        g_provider = provider;
        provider->StartIndex();
    });
}

} // namespace r2port
