#include "r2_material_provider.h"

#include "engine/cvar.h"
#include "engine/filesystem/r2_map_provider.h"
#include "engine/filesystem/r2_work_queue.h"
#include "engine/filesystem/r2_cache_store.h"
#include "engine/filesystem/r2_model_provider.h"
#include "engine/filesystem/r2_material_names.h"
#include "engine/filesystem/r2port_provider.h"
#include "engine/logging/logging.h"
#include "engine/r2port/r2_profile.h"
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

// In-process LRU of converted files (VMT/VTF): R2PORT_MEM_CACHE_MB, default 1536 MiB (r2_cache_store.h).
std::size_t MaterialOutputCacheLimit() { return cache::MemoryCacheBytes(); }
constexpr std::uint32_t kDefaultR2TextureCacheMiB = 4096;
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
constexpr std::string_view kConvertedCacheRevision = "36";  // 36: non-square R2 overview images padded to a square (mp_lf_meadow); 35: world env materials default to gloss (DXT5 spec+gloss alpha) and blur (shininess lod), probe lod base 4; 34: opaque model materials with Vcola drop $vertexalpha and alias the albedo opaque (TreeTrunks); 33: env defaults ($envmaplightscale 1, $fresnelamount 0.35 on world env materials); 32: every source texture a VMT names lives under r2t/ (r2_material_names.h), world materials under r2w/

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

    // The first launch (or the first after the R2 install changed) builds the rpak index, which takes
    // tens of seconds. A material or texture request that arrives before it is ready is a miss, and R1
    // keeps the error material/texture it got for the rest of the map (the whole world turned into the
    // magenta checker on the first load of mp_lf_deck). So once an R2 map is being loaded, block every
    // provider question until the index is ready or has failed.
    void WaitForIndex() const
    {
        if (m_state.load(std::memory_order_acquire) != State::Opening)
            return;
        std::unique_lock<std::mutex> lock(m_stateMutex);
        m_stateChanged.wait_for(lock, kIndexWait, [this]() {
            return m_state.load(std::memory_order_acquire) != State::Opening;
        });
    }

    // Queues the VMT/VTF conversion of a map's materials. Called with the R2 source BSP as soon as the VPK read is done
    // (fromSource: before bspdown has run, so the conversions overlap the downgrade) and again with the converted BSP.
    // Only queues work: the workers start when the index is ready, nothing here blocks the engine thread.
    void PrefetchMapMaterials(std::string_view mapName, FileBytes bspData, bool fromSource)
    {
        Activate();
        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(m_metricsMutex);
            changed = m_activeMap != mapName;
            if (changed) {
                m_activeMap.assign(mapName);
                m_mapMaterialsQueued = false;
                m_sourceMaterialsQueued = false;
            }
            bool& queued = fromSource ? m_sourceMaterialsQueued : m_mapMaterialsQueued;
            if (queued)
                return;
            queued = true;
        }
        if (changed) {
            ClearKnownPaths();
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            MarkAllConsumedLocked();   // the previous map's files are the first to go
        }
        if (QueueMapMaterials(std::string(mapName), std::move(bspData), fromSource))
            m_prefetchedMapCount.fetch_add(1, std::memory_order_relaxed);
        else {
            std::lock_guard<std::mutex> lock(m_metricsMutex);
            (fromSource ? m_sourceMaterialsQueued : m_mapMaterialsQueued) = false;
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
            QueuePrefetchPath(std::move(path), Placement{ 0, kModelOrderBase + m_modelOrder.fetch_add(1, std::memory_order_relaxed) });
        }
    }

    void PrefetchUiTextures(const std::vector<std::string>& texturePaths)
    {
        Activate();
        for (const std::string& path : texturePaths)
            QueuePrefetchPath(path, Placement{ 1, kModelOrderBase + m_modelOrder.fetch_add(1, std::memory_order_relaxed) });
    }

    bool MayContain(std::string_view normalizedPath) const override
    {
        if (!m_activated.load(std::memory_order_acquire)
            || !IsMaterialOrTextureRequestPath(normalizedPath))
            return false;
        WaitForIndex();
        if (m_state.load(std::memory_order_acquire) != State::Ready)
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
        if (!m_activated.load(std::memory_order_acquire))
            return {};
        {
            R2P_SCOPE("m.index.wait");
            WaitForIndex();
        }
        if (m_state.load(std::memory_order_acquire) != State::Ready)
            return {};

        std::string assetName;
        bool materialFile{};
        if (!ParseMaterialPath(normalizedPath, assetName, materialFile))
            return {};

        const auto started = std::chrono::steady_clock::now();
        R2P_SCOPE_B(profileRead, materialFile ? "m.read.vmt" : "m.read.vtf", normalizedPath);
        FileBytes data = ReadOrSchedule(std::string(normalizedPath), assetName, materialFile);
        if (data)
            profileRead.SetBytes(data->size());
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
        std::size_t activeFamilyClaims{};
        {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            queued = m_workQueue.Size();
            active = m_activeWork;
            activeFamilyClaims = m_claims.size();
        }
        std::uint64_t familyLoads{};
        std::uint64_t familyLoadMicroseconds{};
        r2tex::GetFamilyLoadStats(familyLoads, familyLoadMicroseconds);

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
        char message[2048]{};
        _snprintf_s(message, sizeof(message), _TRUNCATE,
            "R1Delta: R2 texture timing phase=%s map=%s steady-ms=%lld index-state=%s index-cache=%s index-ms=%llu reads=%llu materials=%llu textures=%llu material-served=%llu material-misses=%llu texture-served=%llu texture-misses=%llu prefetch-bsp=%llu prefetch-vmt=%llu prefetch-vtf=%llu conversions=%llu conversion-total-ms=%llu conversion-max-ms=%llu callbacks-total-ms=%llu callbacks-max-ms=%llu queued=%zu active=%zu workers=%u queue-full=%llu disk-hits=%llu disk-load-ms=%llu disk-stores=%llu family-claims=%llu family-loads=%llu family-load-ms=%llu family-claims-now=%zu\n",
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
            static_cast<unsigned long long>(m_diskStoreCount.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(m_familyClaims.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(familyLoads), static_cast<unsigned long long>(familyLoadMicroseconds / 1000),
            activeFamilyClaims);
        LogR2TextureMessage(message);
    }

private:
    enum class State { Cold, Opening, Ready, Failed };

    struct CachedFile {
        FileBytes bytes;
        std::size_t size{};
        std::list<std::string>::iterator lruPosition;
        bool consumed{};   // R1 has read it: evicted first, and no longer counted against the prefetch window
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
        bool fromSource{};           // mapMaterials: bspData is the R2 source BSP, whose texdata names are not namespaced yet
        std::int64_t enqueuedUs{};   // R2PORT_PROFILE only: when the item entered the queue
        std::uint32_t family = 0xFFFFFFFFu;   // package family pinned in r2tex while the item is queued or running (kNoFamily: none)
    };

    static WorkItem Stamped(WorkItem item)
    {
        if (r2port::prof::Enabled())
            item.enqueuedUs = r2port::prof::NowUs();
        return item;
    }

    using WorkQueue = r2port::WorkQueue<WorkItem>;
    static constexpr std::uint32_t kNoFamily = WorkQueue::kNoFamily;

    // Where an item sorts: tier 0 = VMTs (R1 reads every VMT of a map before its textures), tier 1 = textures; `order` is
    // the position in the BSP material list (textures inherit their VMT's position, 64 slots per VMT).
    struct Placement {
        std::uint32_t tier{};
        std::uint64_t order{};
    };
    static constexpr std::uint64_t kTextureSlots = 64;
    static constexpr std::uint64_t kModelOrderBase = 1ull << 40;   // model materials come after every world material

    // The package family a conversion of this item has to decode, or kNoFamily when it needs none (already in
    // the disk cache) or the name does not resolve (the conversion fails fast). Called without the queue lock:
    // it reads the asset index and stats the disk cache.
    std::uint32_t FamilyFor(const std::string& path, const std::string& assetName, bool materialFile) const
    {
        if (!m_convertedRoot.empty()) {
            const std::filesystem::path file = ConvertedFilePath(path);
            if (!file.empty()) {
                std::error_code error;
                bool cached = std::filesystem::is_regular_file(file, error);
                if (cached && materialFile) {
                    std::filesystem::path dependencyFile = file;
                    dependencyFile += ".deps";
                    cached = std::filesystem::is_regular_file(dependencyFile, error);
                }
                if (cached)
                    return kNoFamily;
            }
        }
        std::uint32_t family{};
        bool resolved{};
        if (materialFile) {
            resolved = r2tex::FamilyOfMaterial(MaterialSourceName(assetName), family);
        }
        else {
            std::string_view textureSource;
            resolved = r2tex::FamilyOfTexture(
                StripTextureNamespace(assetName, textureSource) ? textureSource : std::string_view(assetName), family);
        }
        return resolved ? family : kNoFamily;
    }

    // Pushes an item (m_cacheMutex held). The item's family stays pinned in r2tex until the item has run.
    void PushLocked(WorkItem item, bool urgent, Placement placement)
    {
        WorkQueue::Meta meta;
        meta.family = item.family;
        meta.tier = placement.tier;
        meta.order = placement.order;
        meta.urgent = urgent;
        if (item.family != kNoFamily)
            r2tex::PinFamily(item.family);
        m_workQueue.Push(Stamped(std::move(item)), meta);
    }

    // Whether a worker may start an item now (m_cacheMutex held). With `force` the memory budget and the prefetch window
    // are ignored: used when every worker is idle, so a full budget can never stall the queue.
    bool Admit(const WorkQueue::Meta& meta, bool force) const
    {
        if (!force && !meta.urgent && meta.tier != 0 && m_unconsumedBytes >= PrefetchWindowBytes())
            return false;   // the converted textures R1 has not read yet already fill the window
        if (meta.family == kNoFamily || r2tex::IsFamilyResident(meta.family))
            return true;
        if (m_claims.count(meta.family) != 0 || r2tex::IsFamilyLoading(meta.family))
            return false;   // another worker is decoding it: take work for a different family instead
        if (force || meta.urgent)
            return true;
        std::size_t claimed = 0;
        for (const auto& claim : m_claims)
            claimed += claim.second;
        const std::size_t limit = static_cast<std::size_t>(g_cacheLimitMiB.load(std::memory_order_relaxed)) << 20;
        // Resident families nobody has queued work for are evicted when the new one lands, so they do not count.
        return r2tex::ResidentFamilyBytes(true) + claimed + r2tex::FamilyDecodedBytes(meta.family) <= limit;
    }

    static std::size_t PrefetchWindowBytes()
    {
        return MaterialOutputCacheLimit() / 4 * 3;
    }

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
    // Longest a request waits for a cold index build (first launch); a build takes tens of seconds.
    static constexpr std::chrono::milliseconds kIndexWait{ 300000 };

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
                    // r2tex::EnvCacheTag(): "" for the defaults, "-env1-<tokens>" while R2TEX_ENV is set (A/B runs never reuse each other's conversions).
                    if (cache::DiskCacheEnabled())
                        m_convertedRoot = cacheDirectory / "converted" / (std::string(kConvertedCacheRevision) + r2tex::EnvCacheTag());
                    if (!m_convertedRoot.empty())
                        m_convertedRoot += r2tex::UberPackCacheTag();   // R2_UBER=...pack: converted VMT/VTF set with AO/cavity carriers
                    std::error_code cacheError;
                    const bool cacheExists = std::filesystem::is_regular_file(
                        cacheDirectory / "r2tex.index", cacheError);
                    if (!cacheExists) {
                        LogR2TextureMessage(
                            "R1Delta: R2 texture index cache is cold; building it in the background, first use may take several seconds\n");
                    }
                    r2tex::SetFamilyCacheLimitMiB(g_cacheLimitMiB.load(std::memory_order_relaxed));
                    R2P_SCOPE_B(profileOpen, "idx.open", "r2tex::Open");
                    opened = r2tex::Open(r2Install, cacheDirectory, &errorMessage, &cacheHit);
                    profileOpen.SetLabel(cacheHit ? "warm index" : "built index");
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
        if (opened) {
            StartCacheMaintenance(cacheDirectory);
            StartWorkers();
        }
        m_stateChanged.notify_all();
    }

    void StartWorkers() noexcept
    {
        const unsigned int cores = std::thread::hardware_concurrency();
        const unsigned int workerLimit = cores > 2 ? cores - 2 : 1;
        // A cold map load is bound by conversion throughput (hundreds of files on the first load of a map)
        // and the loading screen absorbs it, so use most cores; r2tex loads each rpak family under its own lock.
        unsigned int workerCount = std::max(1u, std::min(24u, workerLimit));
        char workerOverride[16]{};
        if (GetEnvironmentVariableA("R2PORT_CONV_WORKERS", workerOverride, sizeof(workerOverride)) > 0 && std::atoi(workerOverride) > 0)
            workerCount = static_cast<unsigned int>(std::min(std::atoi(workerOverride), 64));
        {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            if (m_stopping)
                return;
        }

        try {
            m_workers.reserve(workerCount);
            for (unsigned int i = 0; i < workerCount; ++i)
                m_workers.emplace_back([this]() { WorkerLoop(false); });
            // Two more threads that only take items a reader is blocked on: when every normal worker is busy with a
            // 100+ ms conversion an urgent item would otherwise wait for one of them to finish.
            for (unsigned int i = 0; i < 2; ++i)
                m_workers.emplace_back([this]() { WorkerLoop(true); });
        }
        catch (...) {
        }
        m_workerCount.store(static_cast<unsigned int>(m_workers.size()), std::memory_order_relaxed);
        if (m_workers.empty())
            LogR2TextureMessage("R1Delta: R2 texture provider could not create a conversion worker\n");
    }


    FileBytes ReadOrSchedule(std::string path, const std::string& assetName, bool materialFile)
    {
        std::uint32_t family = kNoFamily;
        bool familyResolved = false;
        std::unique_lock<std::mutex> lock(m_cacheMutex);
        std::shared_ptr<PendingRead> pending;
        for (;;) {
            const auto cached = m_cache.find(path);
            if (cached != m_cache.end()) {
                ConsumeLocked(cached->second);
                return cached->second.bytes;
            }

            const auto existing = m_pendingReads.find(path);
            if (existing != m_pendingReads.end()) {
                pending = existing->second;
                PromoteQueuedPathLocked(path);
                break;
            }
            if (!familyResolved) {
                lock.unlock();
                family = FamilyFor(path, assetName, materialFile);
                familyResolved = true;
                lock.lock();
                continue;   // the state may have changed while unlocked
            }
            if (m_stopping || m_workerCount.load(std::memory_order_relaxed) == 0
                || m_workQueue.Size() >= kMaximumQueuedAssets) {
                m_queueFullCount.fetch_add(1, std::memory_order_relaxed);
                return {};
            }
            pending = std::make_shared<PendingRead>();
            m_pendingReads.emplace(path, pending);
            // A reader is blocked on this item: urgent.
            WorkItem item;
            item.path = path;
            item.assetName = assetName;
            item.materialFile = materialFile;
            item.pending = pending;
            item.family = family;
            PushLocked(std::move(item), true, Placement{ materialFile ? 0u : 1u, 0 });
            m_workAvailable.notify_all();
            break;
        }

        if (!pending->complete)
            pending->ready.wait_for(lock, kConversionWait, [&pending]() { return pending->complete; });
        if (pending->complete && pending->bytes) {
            const auto cached = m_cache.find(path);
            if (cached != m_cache.end())
                ConsumeLocked(cached->second);
        }
        return pending->complete ? pending->bytes : FileBytes{};
    }

    bool QueuePrefetchPath(std::string path, Placement placement)
    {
        const auto normalized = NormalizeProviderPath(path);
        if (!normalized)
            return false;
        std::string assetName;
        bool materialFile{};
        if (!ParseMaterialPath(*normalized, assetName, materialFile))
            return false;
        const std::uint32_t family = FamilyFor(*normalized, assetName, materialFile);

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
            else if (m_workQueue.Size() >= kMaximumQueuedAssets) {
                return false;
            }
            else {
                auto pending = std::make_shared<PendingRead>();
                AddKnownPath(*normalized);
                m_pendingReads.emplace(*normalized, pending);
                WorkItem item;
                item.path = *normalized;
                item.assetName = std::move(assetName);
                item.materialFile = materialFile;
                item.prefetch = true;
                item.pending = std::move(pending);
                item.family = family;
                PushLocked(std::move(item), false, placement);
                if (materialFile)
                    m_prefetchedMaterialCount.fetch_add(1, std::memory_order_relaxed);
                else
                    m_prefetchedTextureCount.fetch_add(1, std::memory_order_relaxed);
                m_workAvailable.notify_one();
                return true;
            }
        }
        std::uint64_t slot = 0;
        for (std::string& dependency : knownDependencies)
            QueuePrefetchPath(std::move(dependency), Placement{ 1, placement.order * kTextureSlots + std::min(slot++, kTextureSlots - 1) });
        return false;
    }

    bool QueueMapMaterials(std::string mapName, FileBytes bspData, bool fromSource)
    {
        if (!bspData)
            return false;
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        if (m_stopping || m_workQueue.Size() >= kMaximumQueuedAssets) {
            m_queueFullCount.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        WorkItem item;
        item.mapName = std::move(mapName);
        item.bspData = std::move(bspData);
        item.mapMaterials = true;
        item.fromSource = fromSource;
        item.prefetch = true;
        item.family = kNoFamily;
        PushLocked(std::move(item), true, Placement{});
        m_workAvailable.notify_all();
        return true;
    }

    // The texdata name the converted BSP carries for a name of the R2 source BSP (bspdown rule 25): lower case, forward
    // slashes, behind r2w/ except under tools/.
    static std::string ConvertedWorldMaterialName(std::string_view sourceName)
    {
        std::string name(sourceName);
        for (char& character : name) {
            if (character == '\\')
                character = '/';
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        }
        if (name.compare(0, 6, "tools/") == 0 || name.compare(0, kWorldMaterialNamespace.size(), kWorldMaterialNamespace) == 0)
            return name;
        name.insert(0, kWorldMaterialNamespace);
        return name;
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
                    "R1Delta: R2 texture prefetch could not read map materials map=%s source=%d error=%s\n",
                    item.mapName.c_str(), item.fromSource ? 1 : 0, error.c_str());
                LogR2TextureMessage(message);
                return;
            }

            // R1 reads the VMTs in the order of the BSP's texdata name table.
            std::size_t queued = 0;
            std::uint64_t order = 0;
            for (const std::string& material : materials) {
                std::string path = "materials/";
                path.append(item.fromSource ? ConvertedWorldMaterialName(material) : material);
                path.append(".vmt");
                if (QueuePrefetchPath(std::move(path), Placement{ 0, order++ }))
                    ++queued;
            }
            // The minimap image R1 draws (materials/overviews/<map>.vtf, named by the overview provider's
            // materials/vgui/minimaps/<map>.vmt) is the R2 rpak texture "overviews\<map>", not a BSP material.
            std::string overview = "materials/overviews/";
            overview.append(item.mapName);
            overview.append(".vtf");
            if (QueuePrefetchPath(std::move(overview), Placement{ 1, kModelOrderBase }))
                ++queued;
            char message[768]{};
            _snprintf_s(message, sizeof(message), _TRUNCATE,
                "R1Delta: R2 texture prefetch queued map=%s source=%d material-names=%zu queued-vmt=%zu\n",
                item.mapName.c_str(), item.fromSource ? 1 : 0, materials.size(), queued);
            LogR2TextureMessage(message);
        }
        catch (...) {
            LogR2TextureMessage(
                "R1Delta: R2 texture prefetch failed while parsing BSP materials\n");
        }
    }

    void PromoteQueuedPathLocked(const std::string& path)
    {
        if (m_workQueue.Promote([&path](const WorkItem& item) { return item.path == path; }))
            m_workAvailable.notify_all();
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
            if (!cache::Unpack(bytes) || bytes.empty())
                return {};
            cache::TouchIfOld(file);
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

    // Returns the bytes written to disk (the packed size for a compressed VTF, the VMT plus its dependency list otherwise).
    std::uint64_t StoreConvertedFile(
        const WorkItem& item, const FileBytes& data, const std::vector<std::string>& dependencies)
    {
        const std::filesystem::path file = ConvertedFilePath(item.path);
        if (file.empty() || !data || data->empty())
            return 0;
        std::uint64_t written = 0;
        try {
            std::error_code error;
            std::filesystem::create_directories(file.parent_path(), error);
            if (error)
                return 0;
            if (item.materialFile) {
                std::string dependencyText;
                for (const std::string& dependency : dependencies) {
                    dependencyText.append(dependency);
                    dependencyText.push_back('\n');
                }
                std::filesystem::path dependencyFile = file;
                dependencyFile += ".deps";
                if (!WriteFileAtomic(dependencyFile, dependencyText.data(), dependencyText.size()))
                    return 0;
                written += dependencyText.size();
            }
            std::vector<std::uint8_t> packed;
            const bool isPacked = !item.materialFile && cache::Pack(data->data(), data->size(), packed);
            const std::uint8_t* bytes = isPacked ? packed.data() : data->data();
            const std::size_t size = isPacked ? packed.size() : data->size();
            if (WriteFileAtomic(file, bytes, size)) {
                m_diskStoreCount.fetch_add(1, std::memory_order_relaxed);
                written += size;
            }
        }
        catch (...) {
        }
        return written;
    }

    // One low priority pass per process: deletes stale revision and fingerprint directories and enforces the size cap (r2_cache_store.h).
    void StartCacheMaintenance(const std::filesystem::path& cacheDirectory)
    {
        static std::atomic<bool> started{ false };
        // DELETES cache directories: opt-in until the owner agrees (R2PORT_CACHE_PRUNE=1). Compression is independent of this switch.
        char pruneFlag[8]{};
        if (GetEnvironmentVariableA("R2PORT_CACHE_PRUNE", pruneFlag, sizeof(pruneFlag)) == 0 || pruneFlag[0] == '0')
            return;
        if (started.exchange(true) || m_convertedRoot.empty())
            return;
        cache::MaintenancePlan plan;
        plan.r2portDirectory = cacheDirectory.parent_path().parent_path();
        plan.currentFingerprint = cacheDirectory.filename().string();
        plan.currentConvertedName = m_convertedRoot.filename().string();
        plan.currentModelRevision = std::string(ModelCacheRevision());
        plan.capBytes = cache::ConfiguredCapBytes();
        try {
            std::thread([plan]() {
                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
                r2port::prof::SetThreadRole("cache-maintenance");
                cache::MaintenanceResult result;
                {
                    R2P_SCOPE_B(profileMaintenance, "wt.cache.maintenance", plan.currentConvertedName);
                    result = cache::RunMaintenance(plan);
                    profileMaintenance.SetBytes(result.removedBytes + result.evictedBytes);
                }
                char message[512]{};
                _snprintf_s(message, sizeof(message), _TRUNCATE,
                    "R1Delta: R2 cache maintenance removed %llu stale directories (%llu files, %llu MiB), evicted %llu least recently used files (%llu MiB), cache now %llu MiB (cap %llu MiB)\n",
                    static_cast<unsigned long long>(result.removedDirectories), static_cast<unsigned long long>(result.removedFiles),
                    static_cast<unsigned long long>(result.removedBytes >> 20), static_cast<unsigned long long>(result.evictedFiles),
                    static_cast<unsigned long long>(result.evictedBytes >> 20), static_cast<unsigned long long>(result.keptBytes >> 20),
                    static_cast<unsigned long long>(plan.capBytes >> 20));
                LogR2TextureMessage(message);
            }).detach();
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

    // Logs one line per failed conversion (the r2tex error string, or the exception text), at most
    // kMaxFailureLogs per session so a bad pak cannot flood the console.
    static void LogConvertFailure(const WorkItem& item, const char* what)
    {
        constexpr unsigned kMaxFailureLogs = 200;
        static std::atomic<unsigned> logged{0};
        const unsigned index = logged.fetch_add(1, std::memory_order_relaxed);
        if (index >= kMaxFailureLogs)
            return;
        char message[1024]{};
        _snprintf_s(message, sizeof(message), _TRUNCATE,
            "R1Delta: R2 texture provider conversion failed %s asset=%s error=%s%s\n",
            item.materialFile ? "material" : "texture", item.assetName.c_str(),
            what && *what ? what : "<no error string>",
            index + 1 == kMaxFailureLogs ? " (further failures not logged)" : "");
        LogR2TextureMessage(message);
    }

    // One line per world material the R2 packages do not carry (about ten per map), at most kMaxFallbackLogs.
    static void LogWorldMaterialFallback(const WorkItem& item, const char* what)
    {
        constexpr unsigned kMaxFallbackLogs = 200;
        static std::atomic<unsigned> logged{0};
        if (logged.fetch_add(1, std::memory_order_relaxed) >= kMaxFallbackLogs)
            return;
        char message[1024]{};
        _snprintf_s(message, sizeof(message), _TRUNCATE,
            "R1Delta: R2 world material %s is not an R2 package material (%s); served as an include of materials/%s.vmt\n",
            item.assetName.c_str(), what && *what ? what : "<no error string>",
            std::string(MaterialSourceName(item.assetName)).c_str());
        LogR2TextureMessage(message);
    }

    // A texture the converted VMT lists but does not name literally cannot move into the r2t/ namespace; R1 then asks
    // for the plain name and any R1 pack that is mounted may answer it. At most kMaxLogs lines.
    static void LogUnnamespacedTexture(const WorkItem& item, const std::string& texture)
    {
        constexpr unsigned kMaxLogs = 50;
        static std::atomic<unsigned> logged{0};
        if (logged.fetch_add(1, std::memory_order_relaxed) >= kMaxLogs)
            return;
        char message[1024]{};
        _snprintf_s(message, sizeof(message), _TRUNCATE,
            "R1Delta: WARNING R2 material %s lists texture %s but its VMT does not name it literally; it stays outside the r2t/ namespace\n",
            item.assetName.c_str(), texture.c_str());
        LogR2TextureMessage(message);
    }

    FileBytes ConvertAsset(const WorkItem& item, std::vector<std::string>& dependencies)
    {
        std::string error;
        try {
            if (item.materialFile) {
                // Converted models name their materials "r2m/<R2 material>" (see modelconv): the same
                // R2 material also exists as a world material, and R1 crashes in studiorender/d3d11 when
                // a studio mesh is drawn with a world shader (LightmappedGeneric). Convert the R2
                // material and force the model shader.
                const bool forModel = item.assetName.compare(0, kModelMaterialNamespace.size(), kModelMaterialNamespace) == 0;
                // World materials of a converted BSP are named "r2w/<R2 material>" (bspdown rule 25) so that no
                // file of a stock R1 map pack answers them; the R2 packages know the name without the prefix.
                std::string_view worldSource;
                const bool forWorld = !forModel && StripWorldMaterialNamespace(item.assetName, worldSource);
                const std::string sourceName(forModel || forWorld ? MaterialSourceName(item.assetName) : std::string_view(item.assetName));
                r2tex::Material material;
                r2tex::MaterialOptions materialOptions;
                materialOptions.forModel = forModel;
                materialOptions.uberPack = r2tex::UberPackRequested();
                bool vmtConverted;
                {
                    R2P_SCOPE_L("d.vmt.emit", sourceName);
                    vmtConverted = r2tex::ConvertMaterial(sourceName, material, materialOptions, &error);
                }
                if (!vmtConverted || material.vmt.empty()) {
                    if (forWorld) {
                        // Not an R2 package material (debug/debugempty, world/dev/edge_detection_*: map pack or R1
                        // files): answer with an include of the plain name, which is what the BSP asked for before the
                        // namespace existed.
                        LogWorldMaterialFallback(item, error.c_str());
                        const std::string fallback = WorldMaterialFallbackVmt(sourceName);
                        return std::make_shared<const std::vector<std::uint8_t>>(fallback.begin(), fallback.end());
                    }
                    LogConvertFailure(item, error.c_str());
                    return {};
                }
                if (forModel) {
                    static constexpr std::string_view world = "\"LightmappedGeneric\"";
                    const std::size_t at = material.vmt.find(world);
                    if (at != std::string::npos && material.vmt.find('{') > at)
                        material.vmt.replace(at, world.size(), "\"VertexLitGeneric\"");
                }
                // Every source texture the VMT names moves into the r2t/ namespace (r2_material_names.h): a stock R1
                // map pack that is still mounted has textures of the same names with other data (specular colour,
                // detail normal maps) and would answer them.
                {
                R2P_SCOPE_L("d.vmt.namespace", item.assetName);
                for (const r2tex::TextureRef& texture : material.textures) {
                    if (texture.name.empty())
                        continue;
                    if (NamespaceVmtTexture(material.vmt, texture.name) == 0
                        && texture.name.compare(0, kSyntheticTexturePrefix.size(), kSyntheticTexturePrefix) != 0)
                        LogUnnamespacedTexture(item, texture.name);
                }
                }
                std::vector<std::uint8_t> bytes(material.vmt.begin(), material.vmt.end());
                dependencies.reserve(material.textures.size());
                for (const r2tex::TextureRef& texture : material.textures) {
                    if (texture.name.empty())
                        continue;
                    std::string path = "materials/";
                    path.append(NamespacedTextureName(texture.name));
                    path.append(".vtf");
                    dependencies.push_back(std::move(path));
                }
                return std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes));
            }

            std::vector<std::uint8_t> bytes;
            std::string_view textureSource;
            const std::string_view r2Name = StripTextureNamespace(item.assetName, textureSource)
                ? textureSource : std::string_view(item.assetName);
            if (r2tex::ConvertTexture(r2Name, bytes, &error) && !bytes.empty())
                return std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes));
            LogConvertFailure(item, error.c_str());
        }
        catch (const std::exception& exception) {
            LogConvertFailure(item, exception.what());
        }
        catch (...) {
            LogConvertFailure(item, "unknown exception");
        }
        return {};
    }

    // R2PORT_LOG_HASHES=1: one line per converted file (FNV-1a 64 of its bytes) in <gamedir>/r2port/profile/filehashes.txt, to
    // compare two runs or two builds byte for byte.
    static void LogServedFileHash(const std::string& path, const std::vector<std::uint8_t>& bytes)
    {
        static const bool enabled = []() {
            char flag[8]{};
            return GetEnvironmentVariableA("R2PORT_LOG_HASHES", flag, sizeof(flag)) > 0 && flag[0] != '0';
        }();
        if (!enabled)
            return;
        std::uint64_t hash = 14695981039346656037ull;
        for (const std::uint8_t byte : bytes) {
            hash ^= byte;
            hash *= 1099511628211ull;
        }
        static std::mutex mutex;
        std::lock_guard<std::mutex> lock(mutex);
        const std::filesystem::path directory = GetR2MapPatchDirectory() / "profile";
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (FILE* file = _wfopen((directory / L"filehashes.txt").c_str(), L"ab")) {
            std::fprintf(file, "%s %016llx %zu\n", path.c_str(), static_cast<unsigned long long>(hash), bytes.size());
            std::fclose(file);
        }
    }

    // A worker is done with `item` (m_cacheMutex held): the family it was loading is no longer claimed.
    void FinishItemLocked(const WorkItem& item)
    {
        if (m_activeWork)
            --m_activeWork;
        if (item.family != kNoFamily)
            m_claims.erase(item.family);
    }

    void WorkerLoop(bool urgentOnly)
    {
        r2port::prof::SetThreadRole(urgentOnly ? "conv-urgent" : "conv-worker");
        for (;;) {
            WorkItem item;
            WorkQueue::Meta meta;
            {
                std::unique_lock<std::mutex> lock(m_cacheMutex);
                for (;;) {
                    if (m_stopping)
                        return;
                    bool taken = m_workQueue.Take(item, meta, [this, urgentOnly](const WorkQueue::Meta& candidate) { return (!urgentOnly || candidate.urgent) && Admit(candidate, false); });
                    if (urgentOnly) {
                        if (taken)
                            break;
                        m_workAvailable.wait_for(lock, std::chrono::milliseconds(50));
                        continue;
                    }
                    // Every worker is idle but work is queued: the budget or the prefetch window is full of things nobody
                    // is waiting for, so run the first item anyway.
                    if (!taken && m_activeWork == 0 && !m_workQueue.Empty())
                        taken = m_workQueue.Take(item, meta, [this](const WorkQueue::Meta& candidate) { return Admit(candidate, true); });
                    if (taken)
                        break;
                    m_workAvailable.wait_for(lock, std::chrono::milliseconds(50));
                }
                ++m_activeWork;
                if (item.family != kNoFamily && !r2tex::IsFamilyResident(item.family)) {
                    m_claims[item.family] = r2tex::FamilyDecodedBytes(item.family);
                    m_familyClaims.fetch_add(1, std::memory_order_relaxed);
                }
            }
            if (item.enqueuedUs)
                r2port::prof::Record("q.wait", item.path, item.enqueuedUs, r2port::prof::NowUs() - item.enqueuedUs);

            if (item.mapMaterials) {
                {
                    R2P_SCOPE_L("wt.prefetch.bsp", item.mapName);
                    PrefetchMaterialFilesFromBsp(item);
                }
                {
                    std::lock_guard<std::mutex> lock(m_cacheMutex);
                    FinishItemLocked(item);
                }
                m_workAvailable.notify_all();
                continue;
            }

            const auto conversionStart = std::chrono::steady_clock::now();
            std::vector<std::string> dependencies;
            bool fromDisk = false;
            const std::int64_t profileDiskStart = r2port::prof::Enabled() ? r2port::prof::NowUs() : 0;
            FileBytes data = LoadConvertedFile(item, dependencies);
            if (data) {
                fromDisk = true;
                if (profileDiskStart)
                    r2port::prof::Record("wt.disk.load", item.path, profileDiskStart, r2port::prof::NowUs() - profileDiskStart, data->size());
            }
            else {
                R2P_SCOPE_B(profileConvert, item.materialFile ? "wt.convert.material" : "wt.convert.texture", item.path);
                data = ConvertAsset(item, dependencies);
                if (data)
                    profileConvert.SetBytes(data->size());
            }
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

            if (data && !fromDisk)
                LogServedFileHash(item.path, *data);
            // Register the VMT's textures as known before its reader resumes: R1 opens them
            // as soon as the VMT parses, and an unknown path is a cached miss. They sort behind
            // every VMT, in the order of their material.
            if (data) {
                std::uint64_t slot = 0;
                for (const std::string& dependency : dependencies)
                    QueuePrefetchPath(dependency, Placement{ 1, meta.order * kTextureSlots + std::min(slot++, kTextureSlots - 1) });
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
                FinishItemLocked(item);
            }
            item.pending->ready.notify_all();
            m_workAvailable.notify_all();
            if (item.family != kNoFamily)
                r2tex::UnpinFamily(item.family);
            if (!data)
                RemoveKnownPath(item.path);
            if (data && !fromDisk) {
                R2P_SCOPE_B(profileStore, "wt.disk.store", item.path);
                profileStore.SetBytes(StoreConvertedFile(item, data, dependencies));
            }

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

    // R1 has read the entry: it keeps its place in the cache but is evicted first and stops counting against the
    // prefetch window (m_cacheMutex held).
    void ConsumeLocked(CachedFile& file)
    {
        if (!file.consumed) {
            file.consumed = true;
            m_unconsumedBytes -= file.size;
            m_lruConsumed.splice(m_lruConsumed.begin(), m_lru, file.lruPosition);
            m_workAvailable.notify_all();   // the window has room again
        }
        else {
            m_lruConsumed.splice(m_lruConsumed.begin(), m_lruConsumed, file.lruPosition);
        }
    }

    void MarkAllConsumedLocked()
    {
        for (auto& entry : m_cache) {
            if (entry.second.consumed)
                continue;
            entry.second.consumed = true;
            m_unconsumedBytes -= entry.second.size;
            m_lruConsumed.splice(m_lruConsumed.begin(), m_lru, entry.second.lruPosition);
        }
    }

    void CacheLocked(const std::string& path, const FileBytes& data)
    {
        if (data->size() > MaterialOutputCacheLimit())
            return;

        // Evict what R1 has already read first; only a full cache of unread files drops one of those.
        while (m_cachedBytes + data->size() > MaterialOutputCacheLimit() && !(m_lruConsumed.empty() && m_lru.empty())) {
            std::list<std::string>& list = m_lruConsumed.empty() ? m_lru : m_lruConsumed;
            const auto oldest = m_cache.find(list.back());
            if (oldest == m_cache.end()) {
                list.pop_back();
                continue;
            }
            m_cachedBytes -= oldest->second.size;
            if (!oldest->second.consumed)
                m_unconsumedBytes -= oldest->second.size;
            m_cache.erase(oldest);
            list.pop_back();
        }

        m_lru.push_front(path);
        const auto inserted = m_cache.emplace(path, CachedFile{ data, data->size(), m_lru.begin(), false });
        if (inserted.second) {
            m_cachedBytes += data->size();
            m_unconsumedBytes += data->size();
        }
        else
            m_lru.pop_front();
    }

    void Stop()
    {
        std::vector<std::pair<WorkItem, WorkQueue::Meta>> cancelled;
        {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            if (m_stopping)
                return;
            m_stopping = true;
            cancelled = m_workQueue.Drain();
            for (auto& [item, meta] : cancelled) {
                if (!item.pending)
                    continue;
                const auto pendingRead = m_pendingReads.find(item.path);
                if (pendingRead != m_pendingReads.end() && pendingRead->second == item.pending)
                    m_pendingReads.erase(pendingRead);
                item.pending->bytes.reset();
                item.pending->complete = true;
            }
        }
        for (auto& [item, meta] : cancelled) {
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
    mutable std::mutex m_stateMutex;
    mutable std::condition_variable m_stateChanged;
    std::atomic<State> m_state{ State::Cold };
    std::thread m_indexWorker;
    std::vector<std::thread> m_workers;
    std::atomic<unsigned int> m_workerCount{};
    std::mutex m_cacheMutex;
    std::condition_variable m_workAvailable;
    std::unordered_map<std::string, CachedFile> m_cache;
    std::unordered_map<std::string, std::vector<std::string>> m_materialDependencies;
    std::unordered_map<std::string, std::shared_ptr<PendingRead>> m_pendingReads;
    std::list<std::string> m_lru;           // unconsumed entries, most recently inserted first
    std::list<std::string> m_lruConsumed;   // entries R1 has read, most recently read first: evicted before m_lru
    WorkQueue m_workQueue;
    std::unordered_map<std::uint32_t, std::size_t> m_claims;   // families a worker is decoding for its item -> decoded bytes
    std::atomic<std::uint64_t> m_familyClaims{};
    std::atomic<std::uint64_t> m_modelOrder{};
    std::size_t m_cachedBytes{};
    std::size_t m_unconsumedBytes{};
    std::size_t m_activeWork{};
    bool m_stopping{};
    std::mutex m_metricsMutex;
    std::string m_activeMap;
    bool m_mapMaterialsQueued{};
    bool m_sourceMaterialsQueued{};
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
        g_r1oCacheCvar = RegisterR1ODediConVar(name, "4096", FCVAR_NONE, help);
        if (!g_r1oCacheCvar)
            return;
        g_r1oCacheCvar->m_fnChangeCallbacks.AddToTail(RefreshR2TextureCacheLimit);
    }
    else {
        g_nativeCacheCvar = RegisterConVar(name, "4096", FCVAR_NONE, help);
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

void PrefetchR2MapMaterials(std::string_view mapName, FileBytes bspData, bool fromSource)
{
    if (g_materialProvider)
        g_materialProvider->PrefetchMapMaterials(mapName, std::move(bspData), fromSource);
}

void PrefetchR2ModelMaterials(const std::vector<std::string>& materialNames)
{
    if (g_materialProvider)
        g_materialProvider->PrefetchModelMaterials(materialNames);
}

void PrefetchR2UiTextures(const std::vector<std::string>& texturePaths)
{
    if (g_materialProvider)
        g_materialProvider->PrefetchUiTextures(texturePaths);
}

void LogR2TextureProviderMetrics(const char* phase)
{
    if (g_materialProvider)
        g_materialProvider->LogMetrics(phase);
}

} // namespace r2port
