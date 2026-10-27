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
#include <fstream>
#include <cstring>
#include <deque>
#include <list>
#include <memory>
#include <mutex>
#include <shared_mutex>
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
std::atomic_flag g_loggedAmbientOcclusionVmt = ATOMIC_FLAG_INIT;

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

bool IsMaterialOrTextureRequestPath(std::string_view path)
{
    constexpr std::string_view prefix = "materials/";
    if (path.size() <= prefix.size() + 4 || path.substr(0, prefix.size()) != prefix)
        return false;
    const std::string_view extension = path.substr(path.size() - 4);
    return extension == ".vmt" || extension == ".vtf";
}

// Bump when the r2tex library or the VMT/VTF output it produces changes: converted files are
// persisted per R2 install fingerprint and revision.
constexpr std::string_view kConvertedCacheRevision = "3";

bool ReadWholeFile(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        return false;
    const std::streamoff size = file.tellg();
    if (size < 0 || size > (1ll << 30))
        return false;
    bytes.resize(static_cast<std::size_t>(size));
    file.seekg(0);
    if (size > 0 && !file.read(reinterpret_cast<char*>(bytes.data()), size))
        return false;
    return true;
}

// Writes through a unique temporary name and renames it into place, so a reader or a crash
// never observes a partial file.
bool WriteFileAtomic(const std::filesystem::path& target, const void* data, std::size_t size)
{
    std::filesystem::path temporary = target;
    temporary += ".tmp" + std::to_string(GetCurrentProcessId()) + "." + std::to_string(GetCurrentThreadId());
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file)
            return false;
        file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        file.close();
        if (!file) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return false;
        }
    }
    if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    return true;
}

std::uint64_t KnownPathHash(std::string_view path)
{
    std::uint64_t hash = 14695981039346656037ull;
    for (const unsigned char character : path) {
        hash ^= character;
        hash *= 1099511628211ull;
    }
    return hash;
}

using KnownPathBuckets = std::unordered_map<std::uint64_t, std::vector<std::string>>;

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
        Stop();
    }

    void StartIndex()
    {
        bool failed = false;
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            if (m_state != State::Cold)
                return;
            m_state = State::Opening;
            try {
                m_indexWorker = std::thread([this]() { Initialize(); });
            }
            catch (...) {
                m_state = State::Failed;
                failed = true;
            }
        }
        if (failed) {
            LogR2TextureMessage("R1Delta: R2 texture provider could not start its index worker\n");
        }
    }

    void Activate()
    {
        m_activated.store(true, std::memory_order_release);
        StartIndex();
    }

    void PrefetchMapMaterials(std::string_view mapName, FileBytes bspData)
    {
        Activate();
        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(m_metricsMutex);
            changed = m_activeMap != mapName;
            if (changed) {
                m_activeMap.assign(mapName);
                m_mapMaterialsQueued = false;
            }
            if (m_mapMaterialsQueued)
                return;
            m_mapMaterialsQueued = true;
        }
        if (changed)
            ClearKnownPaths();
        if (QueueMapMaterials(std::string(mapName), std::move(bspData)))
            m_prefetchedMapCount.fetch_add(1, std::memory_order_relaxed);
        else {
            std::lock_guard<std::mutex> lock(m_metricsMutex);
            m_mapMaterialsQueued = false;
        }
    }

    // Marks the materials of a served model as known and converts them in the background, so
    // they are ready before R1 resolves the model's textures.
    void PrefetchModelMaterials(const std::vector<std::string>& materialNames)
    {
        Activate();
        for (const std::string& name : materialNames) {
            std::string path = "materials/";
            path.append(name);
            path.append(".vmt");
            QueuePrefetchPath(std::move(path));
        }
    }

    bool MayContain(std::string_view normalizedPath) const override
    {
        if (!m_activated.load(std::memory_order_acquire)
            || m_state.load(std::memory_order_acquire) != State::Ready
            || !IsMaterialOrTextureRequestPath(normalizedPath))
            return false;
        std::shared_lock<std::shared_mutex> lock(m_knownPathsMutex);
        return ContainsKnownPath(m_knownPaths, normalizedPath);
    }

    bool CanCacheMisses() const override
    {
        return m_activated.load(std::memory_order_acquire)
            && m_state.load(std::memory_order_acquire) == State::Ready;
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
        if (!m_activated.load(std::memory_order_acquire)
            || m_state.load(std::memory_order_acquire) != State::Ready)
            return {};

        std::string assetName;
        bool materialFile{};
        if (!ParseMaterialPath(normalizedPath, assetName, materialFile))
            return {};

        const auto started = std::chrono::steady_clock::now();
        FileBytes data = ReadOrSchedule(std::string(normalizedPath), assetName, materialFile);
        if (data
            && FilesystemTracePathMatches(normalizedPath)
            && normalizedPath == "materials/world/atmosphere/ambient_occlusion.vmt"
            && !g_loggedAmbientOcclusionVmt.test_and_set(std::memory_order_relaxed)) {
            std::string message = "R1Delta: R2 served material VMT path=";
            message.append(normalizedPath.data(), normalizedPath.size());
            message.append(" bytes=");
            message.append(std::to_string(data->size()));
            message.push_back('\n');
            message.append(reinterpret_cast<const char*>(data->data()), data->size());
            if (message.empty() || message.back() != '\n')
                message.push_back('\n');
            LogR2TextureMessage(message.c_str());
        }
        RecordProviderRead(normalizedPath, materialFile, static_cast<bool>(data), started);
        return data;
    }


    void LogMetrics(const char* phase)
    {
        std::string mapName;
        {
            std::lock_guard<std::mutex> lock(m_metricsMutex);
            mapName = m_activeMap;
        }

        std::size_t queued{};
        std::size_t active{};
        {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            queued = m_workQueue.size();
            active = m_activeWork;
        }

        State state;
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            state = m_state;
        }
        const char* stateName = state == State::Ready ? "ready"
            : state == State::Failed ? "failed"
            : state == State::Opening ? "opening" : "cold";
        const auto qpcMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        char message[1536]{};
        _snprintf_s(message, sizeof(message), _TRUNCATE,
            "R1Delta: R2 texture timing phase=%s map=%s steady-ms=%lld index-state=%s index-cache=%s index-ms=%llu reads=%llu materials=%llu textures=%llu material-served=%llu material-misses=%llu texture-served=%llu texture-misses=%llu prefetch-bsp=%llu prefetch-vmt=%llu prefetch-vtf=%llu conversions=%llu conversion-total-ms=%llu conversion-max-ms=%llu callbacks-total-ms=%llu callbacks-max-ms=%llu queued=%zu active=%zu workers=%u queue-full=%llu disk-hits=%llu disk-load-ms=%llu disk-stores=%llu\n",
            phase ? phase : "<unknown>", mapName.empty() ? "<none>" : mapName.c_str(),
            static_cast<long long>(qpcMilliseconds), stateName,
            m_indexCacheHit.load(std::memory_order_relaxed) ? "warm" : "cold",
            static_cast<unsigned long long>(m_indexElapsedMs.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(m_providerReadCount.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(m_materialReadCount.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(m_textureReadCount.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(m_materialServedCount.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(m_materialMissCount.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(m_textureServedCount.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(m_textureMissCount.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(m_prefetchedMapCount.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(m_prefetchedMaterialCount.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(m_prefetchedTextureCount.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(m_conversionCount.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(m_conversionTotalUs.load(std::memory_order_relaxed) / 1000),
            static_cast<unsigned long long>(m_conversionMaxUs.load(std::memory_order_relaxed) / 1000),
            static_cast<unsigned long long>(m_providerReadTotalUs.load(std::memory_order_relaxed) / 1000),
            static_cast<unsigned long long>(m_providerReadMaxUs.load(std::memory_order_relaxed) / 1000),
            queued, active, m_workerCount.load(std::memory_order_relaxed),
            static_cast<unsigned long long>(m_queueFullCount.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(m_diskHitCount.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(m_diskLoadTotalUs.load(std::memory_order_relaxed) / 1000),
            static_cast<unsigned long long>(m_diskStoreCount.load(std::memory_order_relaxed)));
        LogR2TextureMessage(message);
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

    struct WorkItem {
        std::string path;
        std::string assetName;
        bool materialFile{};
        bool prefetch{};
        std::shared_ptr<PendingRead> pending;
        std::string mapName;
        FileBytes bspData;
        bool mapMaterials{};
    };

    static bool ContainsKnownPath(const KnownPathBuckets& knownPaths, std::string_view path)
    {
        const auto found = knownPaths.find(KnownPathHash(path));
        if (found == knownPaths.end())
            return false;
        return std::find_if(found->second.begin(), found->second.end(),
            [path](const std::string& candidate) {
                return std::string_view(candidate) == path;
            }) != found->second.end();
    }

    void AddKnownPath(std::string_view path)
    {
        std::unique_lock<std::shared_mutex> lock(m_knownPathsMutex);
        auto& bucket = m_knownPaths[KnownPathHash(path)];
        if (!ContainsKnownPath(m_knownPaths, path)) {
            bucket.emplace_back(path);
            r2port::InvalidateMissCache();
        }
    }

    void RemoveKnownPath(std::string_view path)
    {
        std::unique_lock<std::shared_mutex> lock(m_knownPathsMutex);
        const auto bucket = m_knownPaths.find(KnownPathHash(path));
        if (bucket == m_knownPaths.end())
            return;
        auto& names = bucket->second;
        names.erase(std::remove_if(names.begin(), names.end(),
            [path](const std::string& candidate) {
                return std::string_view(candidate) == path;
            }), names.end());
        if (names.empty())
            m_knownPaths.erase(bucket);
    }

    void ClearKnownPaths()
    {
        std::unique_lock<std::shared_mutex> lock(m_knownPathsMutex);
        m_knownPaths.clear();
        r2port::InvalidateMissCache();
    }

    static constexpr std::size_t kMaximumQueuedAssets = 8192;
    // A known path is always queued on the worker pool, and a missed open leaves R1 with its
    // error texture for the rest of the map, so a read waits for the conversion to finish.
    static constexpr std::chrono::milliseconds kConversionWait{ 60000 };

    void Initialize() noexcept
    {
        const auto start = std::chrono::steady_clock::now();
        std::string errorMessage;
        bool opened{};
        bool cacheHit{};
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
                    m_convertedRoot = cacheDirectory / "converted" / std::string(kConvertedCacheRevision);
                    std::error_code cacheError;
                    const bool cacheExists = std::filesystem::is_regular_file(
                        cacheDirectory / "r2tex.index", cacheError);
                    if (!cacheExists) {
                        LogR2TextureMessage(
                            "R1Delta: R2 texture index cache is cold; building it in the background, first use may take several seconds\n");
                    }
                    r2tex::SetFamilyCacheLimitMiB(g_cacheLimitMiB.load(std::memory_order_relaxed));
                    opened = r2tex::Open(r2Install, cacheDirectory, &errorMessage, &cacheHit);
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
        m_indexCacheHit.store(cacheHit, std::memory_order_relaxed);
        m_indexElapsedMs.store(static_cast<std::uint64_t>(std::max<std::int64_t>(elapsed, 0)),
            std::memory_order_relaxed);
        char message[1536]{};
        if (opened) {
            _snprintf_s(message, sizeof(message), _TRUNCATE,
                "R1Delta: R2 texture provider index ready install=%s cache=%s source=%s time=%lld ms family-cache=%u MiB\n",
                r2Install.string().c_str(), cacheDirectory.string().c_str(),
                cacheHit ? "warm" : "built",
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
        if (opened)
            StartWorkers();
    }

    void StartWorkers() noexcept
    {
        const unsigned int cores = std::thread::hardware_concurrency();
        const unsigned int workerLimit = cores > 2 ? cores - 2 : 1;
        // A cold map load is bound by conversion throughput (hundreds of files on the first load of a map)
        // and the loading screen absorbs it, so use most cores; r2tex loads each rpak family under its own lock.
        const unsigned int workerCount = std::max(1u, std::min(12u, workerLimit));
        {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            if (m_stopping)
                return;
        }

        try {
            m_workers.reserve(workerCount);
            for (unsigned int i = 0; i < workerCount; ++i)
                m_workers.emplace_back([this]() { WorkerLoop(); });
        }
        catch (...) {
        }
        m_workerCount.store(static_cast<unsigned int>(m_workers.size()), std::memory_order_relaxed);
        if (m_workers.empty())
            LogR2TextureMessage("R1Delta: R2 texture provider could not create a conversion worker\n");
    }


    FileBytes ReadOrSchedule(std::string path, const std::string& assetName, bool materialFile)
    {
        std::unique_lock<std::mutex> lock(m_cacheMutex);
        const auto cached = m_cache.find(path);
        if (cached != m_cache.end()) {
            m_lru.splice(m_lru.begin(), m_lru, cached->second.lruPosition);
            return cached->second.bytes;
        }

        std::shared_ptr<PendingRead> pending;
        const auto existing = m_pendingReads.find(path);
        if (existing != m_pendingReads.end()) {
            pending = existing->second;
            PromoteQueuedPathLocked(path);
        }
        else {
            if (m_stopping || m_workerCount.load(std::memory_order_relaxed) == 0
                || m_workQueue.size() >= kMaximumQueuedAssets) {
                m_queueFullCount.fetch_add(1, std::memory_order_relaxed);
                return {};
            }
            pending = std::make_shared<PendingRead>();
            m_pendingReads.emplace(path, pending);
            m_workQueue.push_front(WorkItem{ std::move(path), assetName, materialFile, false, pending });
            m_workAvailable.notify_one();
        }

        if (!pending->complete)
            pending->ready.wait_for(lock, kConversionWait, [&pending]() { return pending->complete; });
        return pending->complete ? pending->bytes : FileBytes{};
    }

    bool QueuePrefetchPath(std::string path)
    {
        const auto normalized = NormalizeProviderPath(path);
        if (!normalized)
            return false;
        std::string assetName;
        bool materialFile{};
        if (!ParseMaterialPath(*normalized, assetName, materialFile))
            return false;

        // A path that is already cached or in flight still has to be known again after a map
        // change cleared the known set, together with the textures its VMT refers to.
        std::vector<std::string> knownDependencies;
        {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            if (m_stopping)
                return false;
            if (m_cache.find(*normalized) != m_cache.end()
                || m_pendingReads.find(*normalized) != m_pendingReads.end()) {
                AddKnownPath(*normalized);
                const auto dependencies = m_materialDependencies.find(*normalized);
                if (dependencies != m_materialDependencies.end())
                    knownDependencies = dependencies->second;
            }
            else if (m_workQueue.size() >= kMaximumQueuedAssets) {
                return false;
            }
            else {
                auto pending = std::make_shared<PendingRead>();
                AddKnownPath(*normalized);
                m_pendingReads.emplace(*normalized, pending);
                m_workQueue.push_back(WorkItem{
                    *normalized, std::move(assetName), materialFile, true, std::move(pending) });
                if (materialFile)
                    m_prefetchedMaterialCount.fetch_add(1, std::memory_order_relaxed);
                else
                    m_prefetchedTextureCount.fetch_add(1, std::memory_order_relaxed);
                m_workAvailable.notify_one();
                return true;
            }
        }
        for (std::string& dependency : knownDependencies)
            QueuePrefetchPath(std::move(dependency));
        return false;
    }

    bool QueueMapMaterials(std::string mapName, FileBytes bspData)
    {
        if (!bspData)
            return false;
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        if (m_stopping || m_workQueue.size() >= kMaximumQueuedAssets) {
            m_queueFullCount.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        WorkItem item;
        item.mapName = std::move(mapName);
        item.bspData = std::move(bspData);
        item.mapMaterials = true;
        item.prefetch = true;
        m_workQueue.push_front(std::move(item));
        m_workAvailable.notify_one();
        return true;
    }

    void PrefetchMaterialFilesFromBsp(const WorkItem& item)
    {
        try {
            std::vector<std::string> materials;
            std::string error;
            if (!r2tex::ListMapMaterials(
                    item.bspData->data(), item.bspData->size(), materials, &error)) {
                char message[768]{};
                _snprintf_s(message, sizeof(message), _TRUNCATE,
                    "R1Delta: R2 texture prefetch could not read map materials map=%s error=%s\n",
                    item.mapName.c_str(), error.c_str());
                LogR2TextureMessage(message);
                return;
            }

            std::size_t queued = 0;
            for (const std::string& material : materials) {
                std::string path = "materials/";
                path.append(material);
                path.append(".vmt");
                if (QueuePrefetchPath(std::move(path)))
                    ++queued;
            }
            char message[768]{};
            _snprintf_s(message, sizeof(message), _TRUNCATE,
                "R1Delta: R2 texture prefetch queued map=%s material-names=%zu queued-vmt=%zu\n",
                item.mapName.c_str(), materials.size(), queued);
            LogR2TextureMessage(message);
        }
        catch (...) {
            LogR2TextureMessage(
                "R1Delta: R2 texture prefetch failed while parsing BSP materials\n");
        }
    }

    void PromoteQueuedPathLocked(const std::string& path)
    {
        const auto queued = std::find_if(m_workQueue.begin(), m_workQueue.end(),
            [&path](const WorkItem& item) { return item.path == path; });
        if (queued == m_workQueue.end() || queued == m_workQueue.begin())
            return;
        WorkItem item = std::move(*queued);
        m_workQueue.erase(queued);
        m_workQueue.push_front(std::move(item));
        m_workAvailable.notify_one();
    }

    FileBytes LoadConvertedFile(const WorkItem& item, std::vector<std::string>& dependencies) const
    {
        const std::filesystem::path file = ConvertedFilePath(item.path);
        if (file.empty())
            return {};
        try {
            std::vector<std::uint8_t> bytes;
            if (!ReadWholeFile(file, bytes) || bytes.empty())
                return {};
            if (item.materialFile) {
                std::filesystem::path dependencyFile = file;
                dependencyFile += ".deps";
                std::vector<std::uint8_t> dependencyBytes;
                if (!ReadWholeFile(dependencyFile, dependencyBytes))
                    return {};
                std::size_t begin = 0;
                while (begin < dependencyBytes.size()) {
                    std::size_t end = begin;
                    while (end < dependencyBytes.size() && dependencyBytes[end] != '\n')
                        ++end;
                    if (end > begin)
                        dependencies.emplace_back(
                            reinterpret_cast<const char*>(dependencyBytes.data()) + begin, end - begin);
                    begin = end + 1;
                }
            }
            else if (bytes.size() < 4 || std::memcmp(bytes.data(), "VTF\0", 4) != 0) {
                return {};
            }
            return std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes));
        }
        catch (...) {
            dependencies.clear();
            return {};
        }
    }

    void StoreConvertedFile(
        const WorkItem& item, const FileBytes& data, const std::vector<std::string>& dependencies)
    {
        const std::filesystem::path file = ConvertedFilePath(item.path);
        if (file.empty() || !data || data->empty())
            return;
        try {
            std::error_code error;
            std::filesystem::create_directories(file.parent_path(), error);
            if (error)
                return;
            if (item.materialFile) {
                std::string dependencyText;
                for (const std::string& dependency : dependencies) {
                    dependencyText.append(dependency);
                    dependencyText.push_back('\n');
                }
                std::filesystem::path dependencyFile = file;
                dependencyFile += ".deps";
                if (!WriteFileAtomic(dependencyFile, dependencyText.data(), dependencyText.size()))
                    return;
            }
            if (WriteFileAtomic(file, data->data(), data->size()))
                m_diskStoreCount.fetch_add(1, std::memory_order_relaxed);
        }
        catch (...) {
        }
    }

    std::filesystem::path ConvertedFilePath(std::string_view path) const
    {
        if (m_convertedRoot.empty() || path.empty() || path.front() == '/'
            || path.find("..") != std::string_view::npos
            || path.find(':') != std::string_view::npos
            || path.find('\\') != std::string_view::npos)
            return {};
        return m_convertedRoot / std::filesystem::path(std::string(path));
    }

    FileBytes ConvertAsset(const WorkItem& item, std::vector<std::string>& dependencies)
    {
        try {
            if (item.materialFile) {
                // Converted models name their materials "r2m/<R2 material>" (see modelconv): the same
                // R2 material also exists as a world material, and R1 crashes in studiorender/d3d11 when
                // a studio mesh is drawn with a world shader (LightmappedGeneric). Convert the R2
                // material and force the model shader.
                constexpr std::string_view modelPrefix = "r2m/";
                const bool forModel = item.assetName.compare(0, modelPrefix.size(), modelPrefix) == 0;
                const std::string sourceName = forModel ? item.assetName.substr(modelPrefix.size()) : item.assetName;
                r2tex::Material material;
                if (!r2tex::ConvertMaterial(sourceName, material) || material.vmt.empty())
                    return {};
                if (forModel) {
                    static constexpr std::string_view world = "\"LightmappedGeneric\"";
                    const std::size_t at = material.vmt.find(world);
                    if (at != std::string::npos && material.vmt.find('{') > at)
                        material.vmt.replace(at, world.size(), "\"VertexLitGeneric\"");
                }
                std::vector<std::uint8_t> bytes(material.vmt.begin(), material.vmt.end());
                dependencies.reserve(material.textures.size());
                for (const r2tex::TextureRef& texture : material.textures) {
                    if (texture.name.empty())
                        continue;
                    std::string path = "materials/";
                    path.append(texture.name);
                    path.append(".vtf");
                    dependencies.push_back(std::move(path));
                }
                return std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes));
            }

            std::vector<std::uint8_t> bytes;
            if (r2tex::ConvertTexture(item.assetName, bytes) && !bytes.empty())
                return std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes));
        }
        catch (...) {
        }
        return {};
    }

    void WorkerLoop()
    {
        for (;;) {
            WorkItem item;
            {
                std::unique_lock<std::mutex> lock(m_cacheMutex);
                m_workAvailable.wait(lock, [this]() {
                    return m_stopping || !m_workQueue.empty();
                });
                if (m_stopping)
                    return;
                item = std::move(m_workQueue.front());
                m_workQueue.pop_front();
                ++m_activeWork;
            }

            if (item.mapMaterials) {
                PrefetchMaterialFilesFromBsp(item);
                std::lock_guard<std::mutex> lock(m_cacheMutex);
                if (m_activeWork)
                    --m_activeWork;
                continue;
            }

            const auto conversionStart = std::chrono::steady_clock::now();
            std::vector<std::string> dependencies;
            bool fromDisk = false;
            FileBytes data = LoadConvertedFile(item, dependencies);
            if (data)
                fromDisk = true;
            else
                data = ConvertAsset(item, dependencies);
            const auto conversionUs = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - conversionStart).count());
            if (fromDisk) {
                m_diskHitCount.fetch_add(1, std::memory_order_relaxed);
                m_diskLoadTotalUs.fetch_add(conversionUs, std::memory_order_relaxed);
            }
            else {
                m_conversionCount.fetch_add(1, std::memory_order_relaxed);
                m_conversionTotalUs.fetch_add(conversionUs, std::memory_order_relaxed);
                std::uint64_t previousMax = m_conversionMaxUs.load(std::memory_order_relaxed);
                while (conversionUs > previousMax
                    && !m_conversionMaxUs.compare_exchange_weak(
                        previousMax, conversionUs, std::memory_order_relaxed)) {
                }
            }

            // Register the VMT's textures as known before its reader resumes: R1 opens them
            // as soon as the VMT parses, and an unknown path is a cached miss.
            if (data) {
                for (const std::string& dependency : dependencies)
                    QueuePrefetchPath(dependency);
            }
            {
                std::lock_guard<std::mutex> lock(m_cacheMutex);
                if (data) {
                    if (item.materialFile && !dependencies.empty())
                        m_materialDependencies[item.path] = dependencies;
                    try {
                        CacheLocked(item.path, data);
                    }
                    catch (...) {
                    }
                }
                const auto pendingRead = m_pendingReads.find(item.path);
                if (pendingRead != m_pendingReads.end() && pendingRead->second == item.pending)
                    m_pendingReads.erase(pendingRead);
                item.pending->bytes = data;
                item.pending->complete = true;
                if (m_activeWork)
                    --m_activeWork;
            }
            item.pending->ready.notify_all();
            if (!data)
                RemoveKnownPath(item.path);
            if (data && !fromDisk)
                StoreConvertedFile(item, data, dependencies);

            if (data) {
                std::atomic<bool>& logged = item.materialFile ? m_loggedMaterial : m_loggedTexture;
                bool expected = false;
                if (logged.compare_exchange_strong(expected, true, std::memory_order_relaxed)) {
                    char message[1024]{};
                    _snprintf_s(message, sizeof(message), _TRUNCATE,
                        "R1Delta: R2 texture provider served first %s path=%s bytes=%zu\n",
                        item.materialFile ? "material" : "texture",
                        item.path.c_str(), data->size());
                    LogR2TextureMessage(message);
                }
            }
        }
    }

    void RecordProviderRead(
        std::string_view path,
        bool materialFile,
        bool served,
        std::chrono::steady_clock::time_point started)
    {
        const auto signedElapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count();
        const auto elapsedUs = static_cast<std::uint64_t>(std::max<std::int64_t>(signedElapsed, 0));
        m_providerReadCount.fetch_add(1, std::memory_order_relaxed);
        (materialFile ? m_materialReadCount : m_textureReadCount).fetch_add(
            1, std::memory_order_relaxed);
        if (materialFile)
            (served ? m_materialServedCount : m_materialMissCount).fetch_add(
                1, std::memory_order_relaxed);
        else
            (served ? m_textureServedCount : m_textureMissCount).fetch_add(
                1, std::memory_order_relaxed);
        m_providerReadTotalUs.fetch_add(elapsedUs, std::memory_order_relaxed);
        std::uint64_t previousMax = m_providerReadMaxUs.load(std::memory_order_relaxed);
        while (elapsedUs > previousMax
            && !m_providerReadMaxUs.compare_exchange_weak(
                previousMax, elapsedUs, std::memory_order_relaxed)) {
        }
        if (elapsedUs <= 250000)
            return;

        char message[1024]{};
        _snprintf_s(message, sizeof(message), _TRUNCATE,
            "R1Delta: WARNING slow R2 texture provider callback path=%.*s elapsed=%llu ms\n",
            static_cast<int>(std::min<std::size_t>(path.size(), 700)), path.data(),
            static_cast<unsigned long long>(elapsedUs / 1000));
        LogR2TextureMessage(message);
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

    void Stop()
    {
        std::deque<WorkItem> cancelled;
        {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            if (m_stopping)
                return;
            m_stopping = true;
            cancelled.swap(m_workQueue);
            for (WorkItem& item : cancelled) {
                if (!item.pending)
                    continue;
                const auto pendingRead = m_pendingReads.find(item.path);
                if (pendingRead != m_pendingReads.end() && pendingRead->second == item.pending)
                    m_pendingReads.erase(pendingRead);
                item.pending->bytes.reset();
                item.pending->complete = true;
            }
        }
        for (WorkItem& item : cancelled) {
            if (item.pending)
                item.pending->ready.notify_all();
        }
        m_workAvailable.notify_all();
        if (m_indexWorker.joinable())
            m_indexWorker.join();
        for (std::thread& worker : m_workers) {
            if (worker.joinable())
                worker.join();
        }
    }

    std::atomic<bool> m_activated{};
    mutable std::shared_mutex m_knownPathsMutex;
    KnownPathBuckets m_knownPaths;
    std::mutex m_stateMutex;
    std::atomic<State> m_state{ State::Cold };
    std::thread m_indexWorker;
    std::vector<std::thread> m_workers;
    std::atomic<unsigned int> m_workerCount{};
    std::mutex m_cacheMutex;
    std::condition_variable m_workAvailable;
    std::unordered_map<std::string, CachedFile> m_cache;
    std::unordered_map<std::string, std::vector<std::string>> m_materialDependencies;
    std::unordered_map<std::string, std::shared_ptr<PendingRead>> m_pendingReads;
    std::list<std::string> m_lru;
    std::deque<WorkItem> m_workQueue;
    std::size_t m_cachedBytes{};
    std::size_t m_activeWork{};
    bool m_stopping{};
    std::mutex m_metricsMutex;
    std::string m_activeMap;
    bool m_mapMaterialsQueued{};
    std::atomic<bool> m_indexCacheHit{};
    std::atomic<std::uint64_t> m_indexElapsedMs{};
    std::atomic<std::uint64_t> m_providerReadCount{};
    std::atomic<std::uint64_t> m_materialReadCount{};
    std::atomic<std::uint64_t> m_textureReadCount{};
    std::atomic<std::uint64_t> m_materialServedCount{};
    std::atomic<std::uint64_t> m_materialMissCount{};
    std::atomic<std::uint64_t> m_textureServedCount{};
    std::atomic<std::uint64_t> m_textureMissCount{};
    std::atomic<std::uint64_t> m_providerReadTotalUs{};
    std::atomic<std::uint64_t> m_providerReadMaxUs{};
    std::atomic<std::uint64_t> m_conversionCount{};
    std::atomic<std::uint64_t> m_conversionTotalUs{};
    std::atomic<std::uint64_t> m_conversionMaxUs{};
    std::atomic<std::uint64_t> m_prefetchedMaterialCount{};
    std::atomic<std::uint64_t> m_prefetchedTextureCount{};
    std::atomic<std::uint64_t> m_prefetchedMapCount{};
    std::atomic<std::uint64_t> m_queueFullCount{};
    std::atomic<std::uint64_t> m_diskHitCount{};
    std::atomic<std::uint64_t> m_diskLoadTotalUs{};
    std::atomic<std::uint64_t> m_diskStoreCount{};
    std::filesystem::path m_convertedRoot;
    std::atomic<bool> m_loggedMaterial{};
    std::atomic<bool> m_loggedTexture{};
};


} // namespace

void RegisterR2MaterialProvider()
{
    std::call_once(g_providerRegistration, []() {
        auto provider = std::make_shared<R2MaterialProvider>();
        if (RegisterProvider("materials", provider)) {
            g_materialProvider = std::move(provider);
            g_materialProvider->StartIndex();
        }
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
        g_materialProvider->Activate();
}

void PrefetchR2MapMaterials(std::string_view mapName, FileBytes bspData)
{
    if (g_materialProvider)
        g_materialProvider->PrefetchMapMaterials(mapName, std::move(bspData));
}

void PrefetchR2ModelMaterials(const std::vector<std::string>& materialNames)
{
    if (g_materialProvider)
        g_materialProvider->PrefetchModelMaterials(materialNames);
}

void LogR2TextureProviderMetrics(const char* phase)
{
    if (g_materialProvider)
        g_materialProvider->LogMetrics(phase);
}

} // namespace r2port
