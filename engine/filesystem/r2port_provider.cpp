#include "r2port_provider.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <list>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "logging.h"
namespace r2port {
namespace {

constexpr size_t kMaximumCachedBytes = 220ull * 1024 * 1024;
constexpr auto kCacheIdleLifetime = std::chrono::minutes(30);

struct Registration {
    std::string prefix;
    std::shared_ptr<FileProvider> provider;
};

struct CacheRecord {
    FileBytes data;
    size_t bytes{};
    std::chrono::steady_clock::time_point lastUse;
    std::list<std::string>::iterator lruPosition;
};

struct PendingRead {
    std::condition_variable ready;
    bool complete{};
    FileBytes data;
    uint64_t generation{};
};

std::shared_mutex g_providerMutex;
std::vector<Registration> g_providers;
std::mutex g_cacheMutex;
std::unordered_map<std::string, CacheRecord> g_cache;
std::unordered_map<std::string, std::shared_ptr<PendingRead>> g_pendingReads;
std::list<std::string> g_lru;
size_t g_cachedBytes{};
uint64_t g_cacheGeneration{};
std::string g_activeMap;

std::chrono::steady_clock::time_point g_activeMapStartedAt{};
std::atomic<std::uint64_t> g_providerCallbackCount{};
std::atomic<std::uint64_t> g_providerCallbackTotalUs{};
std::atomic<std::uint64_t> g_providerCallbackMaxUs{};
std::atomic<std::uint64_t> g_providerSlowCallbackCount{};

void LogProviderMessage(const char* message)
{
    static std::mutex outputMutex;
    std::lock_guard<std::mutex> lock(outputMutex);
    OutputDebugStringA(message);
    Msg("%s", message);
}

void RecordProviderCallback(
    const char* operation,
    std::string_view path,
    std::chrono::steady_clock::time_point started,
    bool found)
{
    const auto signedElapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - started).count();
    const auto elapsedUs = static_cast<std::uint64_t>(std::max<std::int64_t>(signedElapsed, 0));
    g_providerCallbackCount.fetch_add(1, std::memory_order_relaxed);
    g_providerCallbackTotalUs.fetch_add(elapsedUs, std::memory_order_relaxed);
    std::uint64_t previousMax = g_providerCallbackMaxUs.load(std::memory_order_relaxed);
    while (elapsedUs > previousMax
        && !g_providerCallbackMaxUs.compare_exchange_weak(
            previousMax, elapsedUs, std::memory_order_relaxed)) {
    }
    if (elapsedUs <= 250000)
        return;

    g_providerSlowCallbackCount.fetch_add(1, std::memory_order_relaxed);
    char message[1024]{};
    _snprintf_s(message, sizeof(message), _TRUNCATE,
        "R1Delta: WARNING slow R2 provider callback op=%s path=%.*s elapsed=%llu ms result=%s\n",
        operation ? operation : "<unknown>",
        static_cast<int>(std::min<std::size_t>(path.size(), 700)), path.data(),
        static_cast<unsigned long long>(elapsedUs / 1000),
        found ? "hit" : "miss");
    LogProviderMessage(message);
}

bool MatchesPrefix(std::string_view path, std::string_view prefix)
{
    return path.size() >= prefix.size() && path.substr(0, prefix.size()) == prefix;
}
bool BelongsToActiveMap(std::string_view path)
{
	if (g_activeMap.empty())
		return false;
	const std::string mapPrefix = "maps/" + g_activeMap;
	if (path.size() > mapPrefix.size() && path.substr(0, mapPrefix.size()) == mapPrefix) {
		const char boundary = path[mapPrefix.size()];
		if (boundary == '.' || boundary == '_' || boundary == '/')
			return true;
	}
	const std::string graphPrefix = "maps/graphs/" + g_activeMap + ".ain";
	if (path == graphPrefix)
		return true;
	const std::string materialPrefix = "materials/maps/" + g_activeMap + "/";
	return path.size() >= materialPrefix.size()
		&& path.substr(0, materialPrefix.size()) == materialPrefix;
}


void RemoveCachedFile(const std::unordered_map<std::string, CacheRecord>::iterator& found)
{
    g_cachedBytes -= found->second.bytes;
    g_lru.erase(found->second.lruPosition);
    g_cache.erase(found);
}

void PruneCache(std::chrono::steady_clock::time_point now)
{
    while (!g_lru.empty()) {
        const auto found = g_cache.find(g_lru.front());
        if (found == g_cache.end()) {
            g_lru.pop_front();
            continue;
        }
        const bool expired = now - found->second.lastUse >= kCacheIdleLifetime;
        if (!expired && g_cachedBytes <= kMaximumCachedBytes)
            break;
        RemoveCachedFile(found);
    }
}

FileBytes ReadFromProviders(std::string_view normalizedPath)
{
    std::shared_lock<std::shared_mutex> lock(g_providerMutex);
    for (const Registration& registration : g_providers) {
        if (!MatchesPrefix(normalizedPath, registration.prefix))
            continue;
        const auto started = std::chrono::steady_clock::now();
        FileBytes data;
        try {
            data = registration.provider->Read(normalizedPath);
        }
        catch (...) {
            // A provider failure behaves like a miss so other providers can answer.
        }
        RecordProviderCallback("read", normalizedPath, started, static_cast<bool>(data));
        if (data)
            return data;
    }
    return {};
}

} // namespace

std::optional<std::string> NormalizeProviderPath(std::string_view path)
{
    if (path.empty() || path.front() == '/' || path.front() == '\\')
        return std::nullopt;

    std::string normalized;
    normalized.reserve(path.size());
    size_t segmentStart = 0;
    for (size_t i = 0; i <= path.size(); ++i) {
        if (i != path.size() && path[i] != '/' && path[i] != '\\') {
            if (path[i] == ':')
                return std::nullopt;
            continue;
        }

        const std::string_view segment = path.substr(segmentStart, i - segmentStart);
        segmentStart = i + 1;
        if (segment.empty() || segment == ".")
            continue;
        if (segment == "..")
            return std::nullopt;
        if (!normalized.empty())
            normalized.push_back('/');
        for (const unsigned char c : segment)
            normalized.push_back(static_cast<char>(std::tolower(c)));
    }
    if (normalized.empty())
        return std::nullopt;
    return normalized;
}

bool RegisterProvider(std::string_view prefix, std::shared_ptr<FileProvider> provider)
{
    if (!provider)
        return false;
    auto normalized = NormalizeProviderPath(prefix);
    if (!normalized)
        return false;
    normalized->push_back('/');

    std::unique_lock<std::shared_mutex> lock(g_providerMutex);
    g_providers.push_back({ std::move(*normalized), std::move(provider) });
    return true;
}

bool Exists(std::string_view path)
{
    const auto normalized = NormalizeProviderPath(path);
    if (!normalized)
        return false;

    std::shared_lock<std::shared_mutex> lock(g_providerMutex);
    for (const Registration& registration : g_providers) {
        if (!MatchesPrefix(*normalized, registration.prefix))
            continue;
        const auto started = std::chrono::steady_clock::now();
        bool exists{};
        try {
            exists = registration.provider->Exists(*normalized);
        }
        catch (...) {
        }
        RecordProviderCallback("exists", *normalized, started, exists);
        if (exists)
            return true;
    }
    return false;
}

std::optional<uint64_t> Size(std::string_view path)
{
    const auto normalized = NormalizeProviderPath(path);
    if (!normalized)
        return std::nullopt;

    bool hasMatchingProvider = false;
    {
        std::shared_lock<std::shared_mutex> lock(g_providerMutex);
        for (const Registration& registration : g_providers) {
            if (!MatchesPrefix(*normalized, registration.prefix))
                continue;
            hasMatchingProvider = true;
            const auto started = std::chrono::steady_clock::now();
            std::optional<uint64_t> size;
            try {
                size = registration.provider->Size(*normalized);
            }
            catch (...) {
            }
            RecordProviderCallback("size", *normalized, started, size.has_value());
            if (size)
                return size;
        }
    }
    if (hasMatchingProvider) {
        if (const FileBytes data = ReadAll(*normalized))
            return static_cast<uint64_t>(data->size());
    }
    return std::nullopt;
}


FileBytes ReadAll(std::string_view path)
{
    auto normalized = NormalizeProviderPath(path);
    if (!normalized)
        return {};

    const auto now = std::chrono::steady_clock::now();
    std::shared_ptr<PendingRead> pending;
    bool isLoader = false;
    {
        std::unique_lock<std::mutex> lock(g_cacheMutex);
        PruneCache(now);
        const auto cached = g_cache.find(*normalized);
        if (cached != g_cache.end()) {
            cached->second.lastUse = now;
            g_lru.splice(g_lru.end(), g_lru, cached->second.lruPosition);
            return cached->second.data;
        }

        const auto active = g_pendingReads.find(*normalized);
        if (active != g_pendingReads.end()) {
            pending = active->second;
        }
        else {
            pending = std::make_shared<PendingRead>();
            pending->generation = g_cacheGeneration;
            g_pendingReads.emplace(*normalized, pending);
            isLoader = true;
        }

        if (!isLoader) {
            pending->ready.wait(lock, [&pending]() { return pending->complete; });
            return pending->data;
        }
    }

    FileBytes data = ReadFromProviders(*normalized);
    {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        pending->data = data;
        pending->complete = true;
        const auto active = g_pendingReads.find(*normalized);
        if (active != g_pendingReads.end() && active->second == pending)
            g_pendingReads.erase(active);

        if (data && data->size() <= kMaximumCachedBytes
            && (pending->generation == g_cacheGeneration || BelongsToActiveMap(*normalized))) {
            g_lru.push_back(*normalized);
            const auto lruPosition = std::prev(g_lru.end());
            const auto [entry, inserted] = g_cache.emplace(
                *normalized,
                CacheRecord{ data, data->size(), std::chrono::steady_clock::now(), lruPosition });
            if (inserted) {
                g_cachedBytes += entry->second.bytes;
                PruneCache(std::chrono::steady_clock::now());
            }
            else {
                g_lru.erase(lruPosition);
            }
        }
    }
    pending->ready.notify_all();
    return data;
}
std::vector<std::string> Enumerate(std::string_view pattern)
{
    const auto normalized = NormalizeProviderPath(pattern);
    if (!normalized)
        return {};

    std::vector<std::string> paths;
    std::unordered_set<std::string> seen;
    std::shared_lock<std::shared_mutex> lock(g_providerMutex);
    for (const Registration& registration : g_providers) {
        if (!MatchesPrefix(*normalized, registration.prefix))
            continue;
        try {
            for (std::string& candidate : registration.provider->Enumerate(*normalized)) {
                auto path = NormalizeProviderPath(candidate);
                if (!path || !MatchesPrefix(*path, registration.prefix) || !seen.insert(*path).second)
                    continue;
                paths.push_back(std::move(*path));
            }
        }
        catch (...) {
        }
    }
    return paths;
}

void SetActiveMap(std::string_view mapName)
{
    std::string normalized;
    if (const auto path = NormalizeProviderPath(mapName))
        normalized = *path;

    const auto startedAt = std::chrono::steady_clock::now();
    std::string activeMapForLog;
    {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        if (normalized == g_activeMap)
            return;
        g_activeMap = std::move(normalized);
        activeMapForLog = g_activeMap;
        g_activeMapStartedAt = startedAt;
        ++g_cacheGeneration;
        g_cache.clear();
        g_lru.clear();
        g_cachedBytes = 0;
        g_providerCallbackCount.store(0, std::memory_order_relaxed);
        g_providerCallbackTotalUs.store(0, std::memory_order_relaxed);
        g_providerCallbackMaxUs.store(0, std::memory_order_relaxed);
        g_providerSlowCallbackCount.store(0, std::memory_order_relaxed);
    }

    const auto qpcMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        startedAt.time_since_epoch()).count();
    char message[512]{};
    _snprintf_s(message, sizeof(message), _TRUNCATE,
        "R1Delta: R2 map start map=%s steady-ms=%lld\n",
        activeMapForLog.c_str(), static_cast<long long>(qpcMilliseconds));
    LogProviderMessage(message);
}

std::string ActiveMapName()
{
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    return g_activeMap;
}

std::uint64_t ActiveMapElapsedMilliseconds()
{
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    if (g_activeMapStartedAt == std::chrono::steady_clock::time_point{})
        return 0;
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - g_activeMapStartedAt).count());
}

void LogProviderReadMetrics(const char* phase)
{
    std::string mapName;
    std::uint64_t mapElapsedMs{};
    const auto now = std::chrono::steady_clock::now();
    const auto qpcMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        mapName = g_activeMap;
        if (g_activeMapStartedAt != std::chrono::steady_clock::time_point{})
            mapElapsedMs = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(now - g_activeMapStartedAt).count());
    }

    char message[768]{};
    _snprintf_s(message, sizeof(message), _TRUNCATE,
        "R1Delta: R2 provider timing phase=%s map=%s steady-ms=%lld map-elapsed-ms=%llu callbacks=%llu total-ms=%llu max-ms=%llu slow-over-250ms=%llu\n",
        phase ? phase : "<unknown>", mapName.c_str(),
        static_cast<long long>(qpcMilliseconds),
        static_cast<unsigned long long>(mapElapsedMs),
        static_cast<unsigned long long>(g_providerCallbackCount.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(g_providerCallbackTotalUs.load(std::memory_order_relaxed) / 1000),
        static_cast<unsigned long long>(g_providerCallbackMaxUs.load(std::memory_order_relaxed) / 1000),
        static_cast<unsigned long long>(g_providerSlowCallbackCount.load(std::memory_order_relaxed)));
    LogProviderMessage(message);
}

bool FilesystemTraceEnabled()
{
    static const bool enabled = []() {
        const char* value = std::getenv("R1DELTA_R2PORT_TRACE_FS");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return enabled;
}

void TraceFilesystemCall(
    const char* functionName,
    const char* path,
    const char* pathId,
    int64_t flags,
    const char* result,
    const char* detail)
{
    if (!FilesystemTraceEnabled() || !path)
        return;

    const std::string_view pathView(path);
    const auto contains = [pathView](std::string_view needle) {
        if (needle.size() > pathView.size())
            return false;
        for (size_t start = 0; start + needle.size() <= pathView.size(); ++start) {
            size_t i = 0;
            for (; i < needle.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(pathView[start + i]))
                    != std::tolower(static_cast<unsigned char>(needle[i])))
                    break;
            }
            if (i == needle.size())
                return true;
        }
        return false;
    };
    if (!contains("mp_lf_stacks") && !contains("mp_glitch")
        && !contains(".bsp") && !contains(".ent") && !contains(".ain")
        && !contains("maps/") && !contains("maps\\") && !contains("graphs"))
        return;

    char buffer[2048]{};
    _snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
        "R1Delta: R2Port FS trace fn=%s path=%s pathId=%s flags=0x%llX detail=%s result=%s\n",
        functionName ? functionName : "<unknown>",
        path,
        pathId ? pathId : "<none>",
        static_cast<unsigned long long>(flags),
        detail ? detail : "<none>",
        result ? result : "<none>");
    static std::mutex outputMutex;
    std::lock_guard<std::mutex> lock(outputMutex);
    OutputDebugStringA(buffer);
    Msg("%s", buffer);
}

} // namespace r2port
