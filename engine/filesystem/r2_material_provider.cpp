#include "r2_material_provider.h"

#include "engine/cvar.h"
#include "engine/filesystem/r2_map_provider.h"
#include "engine/filesystem/r2port_provider.h"
#include "engine/logging/logging.h"
#include "engine/r2port/r2tex/r2tex.hpp"
#include "p2p/p2p_identity.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace r2port {
namespace {

constexpr std::size_t kMaterialOutputCacheLimit = 16ull * 1024 * 1024;
constexpr std::uint32_t kDefaultR2TextureCacheMiB = 256;
std::atomic<std::uint32_t> g_cacheLimitMiB{kDefaultR2TextureCacheMiB};

std::mutex g_cacheCvarMutex;
bool g_cacheCvarRegistered{};
ConVarR1* g_nativeCacheCvar{};
ConVarR1O* g_r1oCacheCvar{};
std::shared_ptr<class R2MaterialProvider> g_materialProvider;
std::once_flag g_providerRegistration;

void LogR2TextureMessage(const char* message)
{
    Msg("%s", message);
    OutputDebugStringA(message);
}

std::string LowerAscii(std::string value)
{
    for (char& character : value)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    return value;
}

std::string R2PakFingerprint(const std::filesystem::path& r2Install)
{
    const std::filesystem::path pakDirectory = r2Install / "paks" / "Win64";
    std::error_code error;
    std::filesystem::directory_iterator iterator(pakDirectory, error);
    if (error)
        return {};

    struct PakMetadata {
        std::string name;
        std::filesystem::path path;
    };
    std::vector<PakMetadata> paks;
    const std::filesystem::directory_iterator end;
    for (; iterator != end; iterator.increment(error)) {
        if (error)
            return {};
        std::error_code itemError;
        if (!iterator->is_regular_file(itemError)) {
            if (itemError)
                return {};
            continue;
        }
        if (LowerAscii(iterator->path().extension().string()) != ".rpak")
            continue;
        paks.push_back({ LowerAscii(iterator->path().filename().string()), iterator->path() });
    }
    if (error || paks.empty())
        return {};

    std::sort(paks.begin(), paks.end(), [](const PakMetadata& left, const PakMetadata& right) {
        return left.name < right.name;
    });

    std::string metadata = LowerAscii(r2Install.lexically_normal().string());
    metadata.push_back('\n');
    for (const PakMetadata& pak : paks) {
        error.clear();
        const auto size = std::filesystem::file_size(pak.path, error);
        if (error)
            return {};
        const auto modified = std::filesystem::last_write_time(pak.path, error);
        if (error)
            return {};
        metadata.append(pak.name);
        metadata.push_back('|');
        metadata.append(std::to_string(size));
        metadata.push_back('|');
        metadata.append(std::to_string(modified.time_since_epoch().count()));
        metadata.push_back('\n');
    }

    const auto digest = p2p::Sha256(
        reinterpret_cast<const std::uint8_t*>(metadata.data()), metadata.size());
    constexpr char hex[] = "0123456789abcdef";
    std::string fingerprint;
    fingerprint.reserve(digest.size() * 2);
    for (const std::uint8_t byte : digest) {
        fingerprint.push_back(hex[byte >> 4]);
        fingerprint.push_back(hex[byte & 0x0f]);
    }
    return fingerprint;
}

std::filesystem::path R2TextureCacheDirectory(
    const std::filesystem::path& r2Install, std::string& errorMessage)
{
    const std::string fingerprint = R2PakFingerprint(r2Install);
    if (fingerprint.empty()) {
        errorMessage = "R2 RPak inventory could not be fingerprinted";
        return {};
    }

    const std::filesystem::path r2PortDirectory = GetR2MapPatchDirectory();
    if (r2PortDirectory.empty()) {
        errorMessage = "R1Delta game content directory could not be found";
        return {};
    }

    const std::filesystem::path cacheDirectory = r2PortDirectory / "cache" / fingerprint;
    std::error_code error;
    std::filesystem::create_directories(cacheDirectory, error);
    if (error) {
        errorMessage = "cannot create R2 texture cache directory: " + error.message();
        return {};
    }
    return cacheDirectory;
}

bool ParseMaterialPath(std::string_view path, std::string& assetName, bool& materialFile)
{
    constexpr std::string_view prefix = "materials/";
    if (path.substr(0, prefix.size()) != prefix)
        return false;

    const std::string_view relative = path.substr(prefix.size());
    if (relative.size() > 4 && relative.substr(relative.size() - 4) == ".vmt") {
        materialFile = true;
        assetName.assign(relative.substr(0, relative.size() - 4));
        return !assetName.empty();
    }
    if (relative.size() > 4 && relative.substr(relative.size() - 4) == ".vtf") {
        materialFile = false;
        assetName.assign(relative.substr(0, relative.size() - 4));
        return !assetName.empty();
    }
    return false;
}

void RefreshR2TextureCacheLimit(void*, const char*, float)
{
    int megabytes = static_cast<int>(kDefaultR2TextureCacheMiB);
    if (g_r1oCacheCvar)
        megabytes = g_r1oCacheCvar->m_Value.m_nValue;
    else if (g_nativeCacheCvar)
        megabytes = g_nativeCacheCvar->m_Value.m_nValue;

    const std::uint32_t configuredMiB =
        megabytes < 0 ? 0u : static_cast<std::uint32_t>(megabytes);
    g_cacheLimitMiB.store(configuredMiB, std::memory_order_relaxed);
    r2tex::SetFamilyCacheLimitMiB(configuredMiB);
}

class R2MaterialProvider final : public FileProvider {
public:
    ~R2MaterialProvider() override
    {
        if (m_worker.joinable())
            m_worker.join();
    }

    void Start()
    {
        m_activated.store(true, std::memory_order_release);
        std::unique_lock<std::mutex> lock(m_stateMutex);
        if (m_state != State::Cold)
            return;

        m_state = State::Opening;
        try {
            m_worker = std::thread([this]() { Initialize(); });
        }
        catch (...) {
            m_state = State::Failed;
            lock.unlock();
            m_stateChanged.notify_all();
        }
    }

    bool Exists(std::string_view normalizedPath) override
    {
        return static_cast<bool>(Read(normalizedPath));
    }

    std::optional<std::uint64_t> Size(std::string_view normalizedPath) override
    {
        const FileBytes data = Read(normalizedPath);
        return data ? std::optional<std::uint64_t>(data->size()) : std::nullopt;
    }

    FileBytes Read(std::string_view normalizedPath) override
    {
        if (!m_activated.load(std::memory_order_acquire))
            return {};
        std::string assetName;
        bool materialFile{};
        if (!ParseMaterialPath(normalizedPath, assetName, materialFile) || !WaitUntilReady())
            return {};

        return ReadOrConvert(std::string(normalizedPath), assetName, materialFile);
    }

private:
    enum class State { Cold, Opening, Ready, Failed };

    struct CachedFile {
        FileBytes bytes;
        std::size_t size{};
        std::list<std::string>::iterator lruPosition;
    };

    struct PendingRead {
        std::condition_variable ready;
        bool complete{};
        FileBytes bytes;
    };

    void Initialize() noexcept
    {
        const auto start = std::chrono::steady_clock::now();
        std::string errorMessage;
        bool opened{};
        std::filesystem::path r2Install;
        std::filesystem::path cacheDirectory;
        try {
            const std::filesystem::path installRoot = GetR2InstallDirectory();
            if (installRoot.empty()) {
                errorMessage = "R2 install was not found";
            }
            else {
                r2Install = installRoot / "r2";
                cacheDirectory = R2TextureCacheDirectory(r2Install, errorMessage);
                if (!cacheDirectory.empty()) {
                    r2tex::SetFamilyCacheLimitMiB(g_cacheLimitMiB.load(std::memory_order_relaxed));
                    opened = r2tex::Open(r2Install, cacheDirectory, &errorMessage);
                }
            }
        }
        catch (const std::exception& exception) {
            errorMessage = exception.what();
        }
        catch (...) {
            errorMessage = "unexpected R2 texture provider initialization failure";
        }

        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        char message[1536]{};
        if (opened) {
            _snprintf_s(message, sizeof(message), _TRUNCATE,
                "R1Delta: R2 texture provider index ready install=%s cache=%s time=%lld ms family-cache=%u MiB\n",
                r2Install.string().c_str(), cacheDirectory.string().c_str(),
                static_cast<long long>(elapsed),
                g_cacheLimitMiB.load(std::memory_order_relaxed));
        }
        else {
            _snprintf_s(message, sizeof(message), _TRUNCATE,
                "R1Delta: R2 texture provider unavailable: %s\n",
                errorMessage.empty() ? "index initialization failed" : errorMessage.c_str());
        }
        LogR2TextureMessage(message);

        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            m_state = opened ? State::Ready : State::Failed;
        }
        m_stateChanged.notify_all();
    }

    bool WaitUntilReady()
    {
        std::unique_lock<std::mutex> lock(m_stateMutex);
        m_stateChanged.wait(lock, [this]() {
            return m_state == State::Ready || m_state == State::Failed;
        });
        return m_state == State::Ready;
    }

    FileBytes ReadOrConvert(
        const std::string& path, const std::string& assetName, bool materialFile)
    {
        std::shared_ptr<PendingRead> pending;
        {
            std::unique_lock<std::mutex> lock(m_cacheMutex);
            const auto cached = m_cache.find(path);
            if (cached != m_cache.end()) {
                m_lru.splice(m_lru.begin(), m_lru, cached->second.lruPosition);
                return cached->second.bytes;
            }

            const auto pendingRead = m_pendingReads.find(path);
            if (pendingRead != m_pendingReads.end()) {
                pending = pendingRead->second;
                pending->ready.wait(lock, [&pending]() { return pending->complete; });
                return pending->bytes;
            }

            pending = std::make_shared<PendingRead>();
            m_pendingReads.emplace(path, pending);
        }

        FileBytes data;
        try {
            if (materialFile) {
                r2tex::Material material;
                if (r2tex::ConvertMaterial(assetName, material) && !material.vmt.empty()) {
                    std::vector<std::uint8_t> bytes(material.vmt.begin(), material.vmt.end());
                    data = std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes));
                }
            }
            else {
                std::vector<std::uint8_t> bytes;
                if (r2tex::ConvertTexture(assetName, bytes) && !bytes.empty())
                    data = std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes));
            }
        }
        catch (...) {
        }

        {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            if (data) {
                try {
                    CacheLocked(path, data);
                }
                catch (...) {
                }
            }
            pending->bytes = data;
            pending->complete = true;
            m_pendingReads.erase(path);
        }
        pending->ready.notify_all();

        if (data) {
            std::atomic<bool>& logged = materialFile ? m_loggedMaterial : m_loggedTexture;
            bool expected = false;
            if (logged.compare_exchange_strong(expected, true, std::memory_order_relaxed)) {
                char message[1024]{};
                _snprintf_s(message, sizeof(message), _TRUNCATE,
                    "R1Delta: R2 texture provider served first %s path=%s bytes=%zu\n",
                    materialFile ? "material" : "texture", path.c_str(), data->size());
                LogR2TextureMessage(message);
            }
        }
        return data;
    }

    void CacheLocked(const std::string& path, const FileBytes& data)
    {
        if (data->size() > kMaterialOutputCacheLimit)
            return;

        while (m_cachedBytes + data->size() > kMaterialOutputCacheLimit && !m_lru.empty()) {
            const auto oldest = m_cache.find(m_lru.back());
            if (oldest == m_cache.end()) {
                m_lru.pop_back();
                continue;
            }
            m_cachedBytes -= oldest->second.size;
            m_cache.erase(oldest);
            m_lru.pop_back();
        }

        m_lru.push_front(path);
        const auto inserted = m_cache.emplace(path, CachedFile{ data, data->size(), m_lru.begin() });
        if (inserted.second)
            m_cachedBytes += data->size();
        else
            m_lru.pop_front();
    }

    std::atomic<bool> m_activated{};
    std::mutex m_stateMutex;
    std::condition_variable m_stateChanged;
    State m_state{State::Cold};
    std::thread m_worker;
    std::mutex m_cacheMutex;
    std::unordered_map<std::string, CachedFile> m_cache;
    std::unordered_map<std::string, std::shared_ptr<PendingRead>> m_pendingReads;
    std::list<std::string> m_lru;
    std::size_t m_cachedBytes{};
    std::atomic<bool> m_loggedMaterial{};
    std::atomic<bool> m_loggedTexture{};
};


} // namespace

void RegisterR2MaterialProvider()
{
    std::call_once(g_providerRegistration, []() {
        auto provider = std::make_shared<R2MaterialProvider>();
        if (RegisterProvider("materials", provider))
            g_materialProvider = std::move(provider);
    });
}

void RegisterR2TextureCacheConVar()
{
    std::lock_guard<std::mutex> lock(g_cacheCvarMutex);
    if (g_cacheCvarRegistered)
        return;

    constexpr const char* name = "r2port_tex_cache_mb";
    constexpr const char* help = "Maximum decoded R2 RPak family cache in MiB.";
    if (IsR1ODedicatedServer()) {
        g_r1oCacheCvar = RegisterR1ODediConVar(name, "256", FCVAR_NONE, help);
        if (!g_r1oCacheCvar)
            return;
        g_r1oCacheCvar->m_fnChangeCallbacks.AddToTail(RefreshR2TextureCacheLimit);
    }
    else {
        g_nativeCacheCvar = RegisterConVar(name, "256", FCVAR_NONE, help);
        if (!g_nativeCacheCvar)
            return;
        g_nativeCacheCvar->m_fnChangeCallbacks.AddToTail(RefreshR2TextureCacheLimit);
    }

    g_cacheCvarRegistered = true;
    RefreshR2TextureCacheLimit(nullptr, nullptr, 0.0f);
}

void StartR2MaterialProvider()
{
    if (g_materialProvider)
        g_materialProvider->Start();
}

} // namespace r2port
