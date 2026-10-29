#include "r2port_provider.h"

#include <algorithm>
#include <array>
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
#include <functional>
#include <unordered_set>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "logging.h"
#include <filesystem>
#include "r2_map_provider.h"
#include "r2_model_provider.h"
#include "engine/r2port/r2_profile.h"
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

std::mutex g_providerMutex;
using ProviderList = std::vector<Registration>;
std::shared_ptr<const ProviderList> g_initialProviders = std::make_shared<const ProviderList>();
std::vector<std::shared_ptr<const ProviderList>> g_providerSnapshots{ g_initialProviders };
std::atomic<const ProviderList*> g_providers{ g_initialProviders.get() };
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
struct TransparentStringHash {
    using is_transparent = void;
    size_t operator()(std::string_view value) const noexcept
    {
        return std::hash<std::string_view>{}(value);
    }
};

struct TransparentStringEqual {
    using is_transparent = void;
    bool operator()(std::string_view left, std::string_view right) const noexcept
    {
        return left == right;
    }
};

constexpr size_t kMaximumTrackedMaterialMisses = 4096;
constexpr size_t kMaximumLoggedMaterialMisses = 100;
constexpr size_t kMaximumTrackedMaterialRequests = 8192;
std::shared_mutex g_materialMissMutex;
std::unordered_set<std::string, TransparentStringHash, TransparentStringEqual>
    g_uniqueMaterialMisses;
std::vector<std::string> g_loggedMaterialMisses;
std::unordered_set<std::string, TransparentStringHash, TransparentStringEqual>
    g_uniqueMaterialRequests;
std::atomic<std::uint64_t> g_uniqueMaterialRequestCount{};
std::atomic<bool> g_materialRequestTraceTruncated{};
std::atomic<std::uint64_t> g_uniqueMaterialMissCount{};
std::atomic<bool> g_materialMissTraceTruncated{};
std::atomic<std::uint64_t> g_materialRequestCallCount{};
std::atomic<std::uint64_t> g_materialRequestTotalUs{};
std::atomic<std::uint64_t> g_materialRequestMaxUs{};
constexpr size_t kMaximumProviderMisses = 16384;
std::shared_mutex g_providerMissMutex;
std::unordered_set<std::string, TransparentStringHash, TransparentStringEqual> g_providerMisses;
uint64_t g_providerMissGeneration{};
std::atomic<std::uint64_t> g_materialRequestTimedCallCount{};

constexpr size_t kFilesystemTraceCapacity = 4096;
struct FilesystemTraceRecord {
    std::uint64_t sequence{};
    int64_t flags{};
    char functionName[64]{};
    char path[260]{};
    char pathId[32]{};
    char result[32]{};
    char detail[160]{};
};
std::mutex g_filesystemTraceMutex;
std::array<FilesystemTraceRecord, kFilesystemTraceCapacity> g_filesystemTrace{};
std::uint64_t g_filesystemTraceNext{};
std::size_t g_filesystemTraceCount{};

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
bool RegistrationMayContain(
    const Registration& registration,
    std::string_view normalizedPath)
{
    if (!MatchesPrefix(normalizedPath, registration.prefix))
        return false;
    try {
        return registration.provider->MayContain(normalizedPath);
    }
    catch (...) {
        return false;
    }
}
bool RegistrationCanCacheMisses(const Registration& registration)
{
    try {
        return registration.provider->CanCacheMisses();
    }
    catch (...) {
        return false;
    }
}

bool NormalizeProviderPathInto(
    std::string_view path,
    char* output,
    size_t outputCapacity,
    std::string_view& normalized)
{
    if (path.empty() || path.size() >= outputCapacity
        || path.front() == '/' || path.front() == '\\')
        return false;

    size_t written = 0;
    size_t segmentStart = 0;
    for (size_t i = 0; i <= path.size(); ++i) {
        if (i != path.size() && path[i] != '/' && path[i] != '\\') {
            if (path[i] == ':')
                return false;
            continue;
        }

        const std::string_view segment = path.substr(segmentStart, i - segmentStart);
        segmentStart = i + 1;
        if (segment.empty() || segment == ".")
            continue;
        if (segment == "..")
            return false;
        if (written)
            output[written++] = '/';
        for (const unsigned char c : segment)
            output[written++] = static_cast<char>(std::tolower(c));
    }
    if (!written)
        return false;
    normalized = std::string_view(output, written);
    return true;
}

const ProviderList* ProviderSnapshot()
{
    return g_providers.load(std::memory_order_acquire);
}

bool FindCachedProviderMiss(std::string_view normalizedPath, uint64_t& generation)
{
    std::shared_lock<std::shared_mutex> lock(g_providerMissMutex);
    generation = g_providerMissGeneration;
    return g_providerMisses.find(normalizedPath) != g_providerMisses.end();
}

void CacheProviderMiss(std::string_view normalizedPath, uint64_t generation)
{
    std::unique_lock<std::shared_mutex> lock(g_providerMissMutex);
    if (generation != g_providerMissGeneration)
        return;
    if (g_providerMisses.size() >= kMaximumProviderMisses) {
        g_providerMisses.clear();
        ++g_providerMissGeneration;
    }
    g_providerMisses.emplace(normalizedPath);
}

bool AnyProviderMayContain(
    const ProviderList& providers,
    std::string_view normalizedPath,
    bool* cacheableMiss = nullptr)
{
    bool matchedProvider = false;
    bool allMissesCacheable = true;
    for (const Registration& registration : providers) {
        if (!MatchesPrefix(normalizedPath, registration.prefix))
            continue;
        matchedProvider = true;
        try {
            if (registration.provider->MayContain(normalizedPath))
                return true;
        }
        catch (...) {
            allMissesCacheable = false;
            continue;
        }
        if (!RegistrationCanCacheMisses(registration))
            allMissesCacheable = false;
    }
    if (cacheableMiss)
        *cacheableMiss = matchedProvider && allMissesCacheable;
    return false;
}

bool IsTraceFilterSeparator(char value)
{
    return value == ',' || value == ';' || value == '|';
}

unsigned char NormalizeTraceFilterChar(unsigned char value)
{
    if (value == '\\')
        return '/';
    if (value >= 'A' && value <= 'Z')
        return static_cast<unsigned char>(value + ('a' - 'A'));
    return value;
}

bool IsBroadTraceFilterToken(std::string_view token)
{
    const auto equals = [token](std::string_view expected) {
        if (token.size() != expected.size())
            return false;
        for (std::size_t i = 0; i < token.size(); ++i) {
            if (NormalizeTraceFilterChar(static_cast<unsigned char>(token[i]))
                != NormalizeTraceFilterChar(static_cast<unsigned char>(expected[i])))
                return false;
        }
        return true;
    };
    return equals("1") || equals("0") || equals("true") || equals("false")
        || equals("all") || equals("*");
}

bool HasSpecificTraceFilter(const char* filters)
{
    if (!filters)
        return false;
    const std::string_view value(filters);
    std::size_t begin = 0;
    while (begin < value.size()) {
        while (begin < value.size()
            && (IsTraceFilterSeparator(value[begin])
                || std::isspace(static_cast<unsigned char>(value[begin]))))
            ++begin;
        std::size_t end = begin;
        while (end < value.size() && !IsTraceFilterSeparator(value[end]))
            ++end;
        while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])))
            --end;
        const std::string_view token = value.substr(begin, end - begin);
        if (!token.empty() && !IsBroadTraceFilterToken(token))
            return true;
        begin = end;
        while (begin < value.size() && !IsTraceFilterSeparator(value[begin]))
            ++begin;
        if (begin < value.size())
            ++begin;
    }
    return false;
}

bool ContainsTraceFilterToken(std::string_view path, std::string_view token)
{
    if (token.empty() || token.size() > path.size())
        return false;
    for (std::size_t start = 0; start + token.size() <= path.size(); ++start) {
        std::size_t i = 0;
        for (; i < token.size(); ++i) {
            if (NormalizeTraceFilterChar(static_cast<unsigned char>(path[start + i]))
                != NormalizeTraceFilterChar(static_cast<unsigned char>(token[i])))
                break;
        }
        if (i == token.size())
            return true;
    }
    return false;
}

bool TraceFilterMatches(const char* filter, std::string_view path)
{
    if (!filter || path.empty())
        return false;
    const std::string_view filters(filter);
    std::size_t begin = 0;
    while (begin < filters.size()) {
        while (begin < filters.size()
            && (IsTraceFilterSeparator(filters[begin])
                || std::isspace(static_cast<unsigned char>(filters[begin]))))
            ++begin;
        std::size_t end = begin;
        while (end < filters.size() && !IsTraceFilterSeparator(filters[end]))
            ++end;
        while (end > begin && std::isspace(static_cast<unsigned char>(filters[end - 1])))
            --end;
        const std::string_view token = filters.substr(begin, end - begin);
        if (!IsBroadTraceFilterToken(token) && ContainsTraceFilterToken(path, token))
            return true;
        begin = end;
        while (begin < filters.size() && !IsTraceFilterSeparator(filters[begin]))
            ++begin;
        if (begin < filters.size())
            ++begin;
    }
    return false;
}

const char* GetFilesystemTraceFilter()
{
    static const char* filter = []() {
        const char* value = std::getenv("R1DELTA_R2PORT_TRACE_FS");
        return HasSpecificTraceFilter(value) ? value : nullptr;
    }();
    return filter;
}

const char* GetMaterialMissTraceFilter()
{
    static const char* filter = []() {
        const char* value = std::getenv("R1DELTA_R2PORT_TRACE_MISSES");
        return HasSpecificTraceFilter(value) ? value : nullptr;
    }();
    return filter;
}

bool MaterialMissTracePathMatches(std::string_view path)
{
    return TraceFilterMatches(GetMaterialMissTraceFilter(), path);
}
bool MaterialMissTraceEnabled()
{
    return GetMaterialMissTraceFilter() != nullptr;
}

bool IsMaterialRequestPath(std::string_view normalizedPath)
{
    if (!MatchesPrefix(normalizedPath, "materials/"))
        return false;
    return normalizedPath.size() > 4
        && (normalizedPath.substr(normalizedPath.size() - 4) == ".vmt"
            || normalizedPath.substr(normalizedPath.size() - 4) == ".vtf");
}

class MaterialRequestTimer {
public:
    explicit MaterialRequestTimer(std::string_view path)
        : m_enabled(IsMaterialRequestPath(path) && MaterialMissTracePathMatches(path))
        , m_started(m_enabled ? std::chrono::steady_clock::now()
                              : std::chrono::steady_clock::time_point{})
    {
    }

    ~MaterialRequestTimer()
    {
        if (!m_enabled)
            return;
        const auto signedElapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - m_started).count();
        const auto elapsedUs = static_cast<std::uint64_t>(
            std::max<std::int64_t>(signedElapsed, 0));
        g_materialRequestTimedCallCount.fetch_add(1, std::memory_order_relaxed);
        g_materialRequestTotalUs.fetch_add(elapsedUs, std::memory_order_relaxed);
        std::uint64_t previousMax = g_materialRequestMaxUs.load(std::memory_order_relaxed);
        while (elapsedUs > previousMax
            && !g_materialRequestMaxUs.compare_exchange_weak(
                previousMax, elapsedUs, std::memory_order_relaxed)) {
        }
    }

private:
    bool m_enabled{};
    std::chrono::steady_clock::time_point m_started{};
};

void RecordUniqueMaterialRequest(std::string_view normalizedPath)
{
    if (!IsMaterialRequestPath(normalizedPath))
        return;
    g_materialRequestCallCount.fetch_add(1, std::memory_order_relaxed);
    if (!MaterialMissTracePathMatches(normalizedPath))
        return;
    {
        std::shared_lock<std::shared_mutex> lock(g_materialMissMutex);
        if (g_uniqueMaterialRequests.find(normalizedPath) != g_uniqueMaterialRequests.end())
            return;
    }

    std::unique_lock<std::shared_mutex> lock(g_materialMissMutex);
    if (g_uniqueMaterialRequests.find(normalizedPath) != g_uniqueMaterialRequests.end())
        return;
    const std::uint64_t count = g_uniqueMaterialRequestCount.load(std::memory_order_relaxed);
    if (count >= kMaximumTrackedMaterialRequests) {
        g_materialRequestTraceTruncated.store(true, std::memory_order_relaxed);
        return;
    }
    g_uniqueMaterialRequests.emplace(normalizedPath);
    g_uniqueMaterialRequestCount.store(count + 1, std::memory_order_relaxed);
}

void RecordUniqueMaterialMiss(std::string_view rawPath, std::string_view normalizedPath)
{
    if (!MaterialMissTracePathMatches(normalizedPath)
        || !IsMaterialRequestPath(normalizedPath)
        || rawPath.empty() || rawPath.size() > 1024)
        return;

    {
        std::shared_lock<std::shared_mutex> lock(g_materialMissMutex);
        if (g_uniqueMaterialMisses.find(normalizedPath) != g_uniqueMaterialMisses.end()
            || g_uniqueMaterialMissCount.load(std::memory_order_relaxed)
                >= kMaximumTrackedMaterialMisses)
            return;
    }

    std::unique_lock<std::shared_mutex> lock(g_materialMissMutex);
    if (g_uniqueMaterialMisses.find(normalizedPath) != g_uniqueMaterialMisses.end())
        return;
    const std::uint64_t count = g_uniqueMaterialMissCount.load(std::memory_order_relaxed);
    if (count >= kMaximumTrackedMaterialMisses) {
        g_materialMissTraceTruncated.store(true, std::memory_order_relaxed);
        return;
    }
    g_uniqueMaterialMisses.emplace(normalizedPath);
    if (g_loggedMaterialMisses.size() < kMaximumLoggedMaterialMisses)
        g_loggedMaterialMisses.emplace_back(rawPath);
    g_uniqueMaterialMissCount.store(count + 1, std::memory_order_relaxed);
}

void LogUniqueMaterialMisses()
{
    if (!MaterialMissTraceEnabled())
        return;

    std::vector<std::string> names;
    {
        std::shared_lock<std::shared_mutex> lock(g_materialMissMutex);
        names = g_loggedMaterialMisses;
    }
    const std::uint64_t requestCalls =
        g_materialRequestCallCount.load(std::memory_order_relaxed);
    const std::uint64_t timedCalls =
        g_materialRequestTimedCallCount.load(std::memory_order_relaxed);
    const std::uint64_t requestTotalUs =
        g_materialRequestTotalUs.load(std::memory_order_relaxed);
    char summary[320]{};
    _snprintf_s(summary, sizeof(summary), _TRUNCATE,
        "R1Delta: R2 material requests calls=%llu traced-calls=%llu avg-us=%llu max-us=%llu unique=%llu misses=%llu logged-misses=%zu requests-truncated=%d misses-truncated=%d\n",
        static_cast<unsigned long long>(requestCalls),
        static_cast<unsigned long long>(timedCalls),
        static_cast<unsigned long long>(timedCalls ? requestTotalUs / timedCalls : 0),
        static_cast<unsigned long long>(g_materialRequestMaxUs.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            g_uniqueMaterialRequestCount.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            g_uniqueMaterialMissCount.load(std::memory_order_relaxed)),
        names.size(),
        g_materialRequestTraceTruncated.load(std::memory_order_relaxed) ? 1 : 0,
        g_materialMissTraceTruncated.load(std::memory_order_relaxed) ? 1 : 0);
    LogProviderMessage(summary);
    for (size_t i = 0; i < names.size(); ++i) {
        char message[1200]{};
        _snprintf_s(message, sizeof(message), _TRUNCATE,
            "R1Delta: R2 unique material miss index=%zu path=%s\n", i, names[i].c_str());
        LogProviderMessage(message);
    }
}

void ClearUniqueMaterialMisses()
{
    g_materialRequestCallCount.store(0, std::memory_order_relaxed);
    g_materialRequestTotalUs.store(0, std::memory_order_relaxed);
    g_materialRequestTimedCallCount.store(0, std::memory_order_relaxed);
    g_materialRequestMaxUs.store(0, std::memory_order_relaxed);
    if (!MaterialMissTraceEnabled())
        return;
    std::unique_lock<std::shared_mutex> lock(g_materialMissMutex);
    g_uniqueMaterialMisses.clear();
    g_uniqueMaterialRequests.clear();
    g_loggedMaterialMisses.clear();
    g_uniqueMaterialMissCount.store(0, std::memory_order_relaxed);
    g_uniqueMaterialRequestCount.store(0, std::memory_order_relaxed);
    g_materialRequestTraceTruncated.store(false, std::memory_order_relaxed);
    g_materialMissTraceTruncated.store(false, std::memory_order_relaxed);
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

FileBytes ReadFromProviders(
    std::string_view normalizedPath,
    const ProviderList& providers)
{
    for (const Registration& registration : providers) {
        if (!RegistrationMayContain(registration, normalizedPath))
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
    char buffer[1024]{};
    std::string_view normalized;
    if (!NormalizeProviderPathInto(path, buffer, sizeof(buffer), normalized))
        return std::nullopt;
    return std::string(normalized);
}

bool RegisterProvider(std::string_view prefix, std::shared_ptr<FileProvider> provider)
{
    if (!provider)
        return false;
    auto normalized = NormalizeProviderPath(prefix);
    if (!normalized)
        return false;
    normalized->push_back('/');

    {
        std::lock_guard<std::mutex> lock(g_providerMutex);
        auto updated = std::make_shared<ProviderList>(*ProviderSnapshot());
        updated->push_back({ std::move(*normalized), std::move(provider) });
        std::shared_ptr<const ProviderList> published = std::move(updated);
        const ProviderList* snapshot = published.get();
        g_providerSnapshots.push_back(std::move(published));
        g_providers.store(snapshot, std::memory_order_release);
    }
    InvalidateMissCache();
    return true;
}

void InvalidateMissCache()
{
    std::unique_lock<std::shared_mutex> lock(g_providerMissMutex);
    g_providerMisses.clear();
    ++g_providerMissGeneration;
}

bool MayContain(std::string_view path)
{
    char normalizedBuffer[1024];
    std::string_view normalized;
    if (!NormalizeProviderPathInto(path, normalizedBuffer, sizeof(normalizedBuffer), normalized))
        return false;

    MaterialRequestTimer requestTimer(normalized);
    RecordUniqueMaterialRequest(normalized);
    uint64_t missGeneration{};
    if (FindCachedProviderMiss(normalized, missGeneration)) {
        RecordUniqueMaterialMiss(path, normalized);
        return false;
    }

    bool cacheableMiss = false;
    const auto providers = ProviderSnapshot();
    if (AnyProviderMayContain(*providers, normalized, &cacheableMiss))
        return true;
    if (cacheableMiss)
        CacheProviderMiss(normalized, missGeneration);
    RecordUniqueMaterialMiss(path, normalized);
    return false;
}

bool Exists(std::string_view path)
{
    char normalizedBuffer[1024];
    std::string_view normalized;
    if (!NormalizeProviderPathInto(path, normalizedBuffer, sizeof(normalizedBuffer), normalized))
        return false;
    MaterialRequestTimer requestTimer(normalized);
    RecordUniqueMaterialRequest(normalized);
    uint64_t missGeneration{};
    if (FindCachedProviderMiss(normalized, missGeneration)) {
        RecordUniqueMaterialMiss(path, normalized);
        return false;
    }

    bool matchedProvider = false;
    bool cacheableMiss = true;
    const auto providers = ProviderSnapshot();
    for (const Registration& registration : *providers) {
        if (!MatchesPrefix(normalized, registration.prefix))
            continue;
        matchedProvider = true;
        bool mayContain = false;
        try {
            mayContain = registration.provider->MayContain(normalized);
        }
        catch (...) {
            cacheableMiss = false;
            continue;
        }
        if (!mayContain) {
            if (!RegistrationCanCacheMisses(registration))
                cacheableMiss = false;
            continue;
        }

        const auto started = std::chrono::steady_clock::now();
        bool exists{};
        try {
            exists = registration.provider->Exists(normalized);
        }
        catch (...) {
            cacheableMiss = false;
        }
        RecordProviderCallback("exists", normalized, started, exists);
        if (exists)
            return true;
        cacheableMiss = false;
    }
    if (matchedProvider && cacheableMiss)
        CacheProviderMiss(normalized, missGeneration);
    RecordUniqueMaterialMiss(path, normalized);
    return false;
}

std::optional<uint64_t> Size(std::string_view path)
{
    char normalizedBuffer[1024];
    std::string_view normalized;
    if (!NormalizeProviderPathInto(path, normalizedBuffer, sizeof(normalizedBuffer), normalized))
        return std::nullopt;
    MaterialRequestTimer requestTimer(normalized);
    RecordUniqueMaterialRequest(normalized);
    uint64_t missGeneration{};
    if (FindCachedProviderMiss(normalized, missGeneration)) {
        RecordUniqueMaterialMiss(path, normalized);
        return std::nullopt;
    }

    bool matchedProvider = false;
    bool cacheableMiss = true;
    bool queriedProvider = false;
    const auto providers = ProviderSnapshot();
    for (const Registration& registration : *providers) {
        if (!MatchesPrefix(normalized, registration.prefix))
            continue;
        matchedProvider = true;
        bool mayContain = false;
        try {
            mayContain = registration.provider->MayContain(normalized);
        }
        catch (...) {
            cacheableMiss = false;
            continue;
        }
        if (!mayContain) {
            if (!RegistrationCanCacheMisses(registration))
                cacheableMiss = false;
            continue;
        }

        queriedProvider = true;
        const auto started = std::chrono::steady_clock::now();
        std::optional<uint64_t> size;
        try {
            size = registration.provider->Size(normalized);
        }
        catch (...) {
            cacheableMiss = false;
        }
        RecordProviderCallback("size", normalized, started, size.has_value());
        if (size)
            return size;
        cacheableMiss = false;
    }
    if (queriedProvider) {
        if (const FileBytes data = ReadAll(path))
            return static_cast<uint64_t>(data->size());
    }
    if (matchedProvider && cacheableMiss)
        CacheProviderMiss(normalized, missGeneration);
    RecordUniqueMaterialMiss(path, normalized);
    return std::nullopt;
}


FileBytes ReadAll(std::string_view path)
{
    R2P_SCOPE_B(profileReadAll, "m.fs.readall", path);
    char normalizedBuffer[1024];
    std::string_view normalized;
    if (!NormalizeProviderPathInto(path, normalizedBuffer, sizeof(normalizedBuffer), normalized))
        return {};
    MaterialRequestTimer requestTimer(normalized);
    RecordUniqueMaterialRequest(normalized);
    uint64_t missGeneration{};
    if (FindCachedProviderMiss(normalized, missGeneration)) {
        RecordUniqueMaterialMiss(path, normalized);
        return {};
    }

    bool cacheableMiss = false;
    const auto providers = ProviderSnapshot();
    if (!AnyProviderMayContain(*providers, normalized, &cacheableMiss)) {
        if (cacheableMiss)
            CacheProviderMiss(normalized, missGeneration);
        RecordUniqueMaterialMiss(path, normalized);
        return {};
    }

    const std::string cacheKey(normalized);
    const auto now = std::chrono::steady_clock::now();
    std::shared_ptr<PendingRead> pending;
    bool isLoader = false;
    {
        std::unique_lock<std::mutex> lock(g_cacheMutex);
        PruneCache(now);
        const auto cached = g_cache.find(cacheKey);
        if (cached != g_cache.end()) {
            cached->second.lastUse = now;
            g_lru.splice(g_lru.end(), g_lru, cached->second.lruPosition);
            return cached->second.data;
        }

        const auto active = g_pendingReads.find(cacheKey);
        if (active != g_pendingReads.end()) {
            pending = active->second;
        }
        else {
            pending = std::make_shared<PendingRead>();
            pending->generation = g_cacheGeneration;
            g_pendingReads.emplace(cacheKey, pending);
            isLoader = true;
        }

        if (!isLoader) {
            pending->ready.wait(lock, [&pending]() { return pending->complete; });
            return pending->data;
        }
    }

    FileBytes data = ReadFromProviders(normalized, *providers);
    if (!data)
        RecordUniqueMaterialMiss(path, normalized);
    {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        pending->data = data;
        pending->complete = true;
        const auto active = g_pendingReads.find(cacheKey);
        if (active != g_pendingReads.end() && active->second == pending)
            g_pendingReads.erase(active);

        if (data && data->size() <= kMaximumCachedBytes
            && (pending->generation == g_cacheGeneration || BelongsToActiveMap(normalized))) {
            g_lru.push_back(cacheKey);
            const auto lruPosition = std::prev(g_lru.end());
            const auto [entry, inserted] = g_cache.emplace(
                cacheKey,
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
    if (data)
        profileReadAll.SetBytes(data->size());
    return data;
}

std::vector<std::string> Enumerate(std::string_view pattern)
{
    char normalizedBuffer[1024];
    std::string_view normalized;
    if (!NormalizeProviderPathInto(pattern, normalizedBuffer, sizeof(normalizedBuffer), normalized))
        return {};

    std::vector<std::string> paths;
    std::unordered_set<std::string> seen;
    const auto providers = ProviderSnapshot();
    for (const Registration& registration : *providers) {
        if (!MatchesPrefix(normalized, registration.prefix))
            continue;
        try {
            for (std::string& candidate : registration.provider->Enumerate(normalized)) {
                auto candidatePath = NormalizeProviderPath(candidate);
                if (!candidatePath || !MatchesPrefix(*candidatePath, registration.prefix)
                    || !seen.insert(*candidatePath).second)
                    continue;
                paths.push_back(std::move(*candidatePath));
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
    InvalidateMissCache();
    ClearUniqueMaterialMisses();

    const auto qpcMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        startedAt.time_since_epoch()).count();
    char message[512]{};
    _snprintf_s(message, sizeof(message), _TRUNCATE,
        "R1Delta: R2 map start map=%s steady-ms=%lld\n",
        activeMapForLog.c_str(), static_cast<long long>(qpcMilliseconds));
    LogProviderMessage(message);
    r2port::prof::MarkMapStart(activeMapForLog);
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
        "R1Delta: R2 provider timing phase=%s map=%s steady-ms=%lld map-elapsed-ms=%llu material-registry-calls=%llu callbacks=%llu total-ms=%llu max-ms=%llu slow-over-250ms=%llu\n",
        phase ? phase : "<unknown>", mapName.c_str(),
        static_cast<long long>(qpcMilliseconds),
        static_cast<unsigned long long>(mapElapsedMs),
        static_cast<unsigned long long>(g_materialRequestCallCount.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(g_providerCallbackCount.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(g_providerCallbackTotalUs.load(std::memory_order_relaxed) / 1000),
        static_cast<unsigned long long>(g_providerCallbackMaxUs.load(std::memory_order_relaxed) / 1000),
        static_cast<unsigned long long>(g_providerSlowCallbackCount.load(std::memory_order_relaxed)));
    LogProviderMessage(message);
    if (phase && std::strcmp(phase, "manual") == 0) {
        LogUniqueMaterialMisses();
        LogR2ModelProviderMetrics(phase);
    }
}

bool FilesystemTraceEnabled()
{
    return GetFilesystemTraceFilter() != nullptr;
}

bool FilesystemTracePathMatches(std::string_view path)
{
    return TraceFilterMatches(GetFilesystemTraceFilter(), path);
}

bool FilesystemTracePathMatches(const char* path)
{
    return path && TraceFilterMatches(GetFilesystemTraceFilter(), path);
}

void TraceFilesystemCall(
    const char* functionName,
    const char* path,
    const char* pathId,
    int64_t flags,
    const char* result,
    const char* detail)
{
    if (!FilesystemTracePathMatches(path))
        return;

    std::lock_guard<std::mutex> lock(g_filesystemTraceMutex);
    const std::uint64_t sequence = g_filesystemTraceNext++;
    FilesystemTraceRecord& record =
        g_filesystemTrace[sequence % kFilesystemTraceCapacity];
    record = {};
    record.sequence = sequence;
    record.flags = flags;
    strncpy_s(record.functionName, functionName ? functionName : "<unknown>", _TRUNCATE);
    strncpy_s(record.path, path, _TRUNCATE);
    strncpy_s(record.pathId, pathId ? pathId : "<none>", _TRUNCATE);
    strncpy_s(record.result, result ? result : "<none>", _TRUNCATE);
    strncpy_s(record.detail, detail ? detail : "<none>", _TRUNCATE);
    g_filesystemTraceCount = std::min(g_filesystemTraceCount + 1, kFilesystemTraceCapacity);
}

void DumpFilesystemTrace()
{
    std::vector<FilesystemTraceRecord> records(kFilesystemTraceCapacity);
    std::size_t count{};
    std::uint64_t nextSequence{};
    std::uint64_t firstSequence{};
    {
        std::lock_guard<std::mutex> lock(g_filesystemTraceMutex);
        count = g_filesystemTraceCount;
        nextSequence = g_filesystemTraceNext;
        firstSequence = g_filesystemTraceNext - count;
        for (std::size_t i = 0; i < count; ++i)
            records[i] = g_filesystemTrace[(firstSequence + i) % kFilesystemTraceCapacity];
    }
    char summary[192]{};
    _snprintf_s(summary, sizeof(summary), _TRUNCATE,
        "R1Delta: R2Port FS trace entries=%zu capacity=%zu truncated=%d\n",
        count, kFilesystemTraceCapacity,
        nextSequence > kFilesystemTraceCapacity ? 1 : 0);
    LogProviderMessage(summary);
    if (count == 0) {
        LogProviderMessage("R1Delta: R2Port FS trace buffer is empty; set a path filter to capture calls.\n");
        return;
    }

    // The ring can hold thousands of calls, more than the console keeps, so the full dump goes
    // to a file next to the other r2port runtime data.
    const std::filesystem::path traceFile = GetR2MapPatchDirectory() / "fs-trace.txt";
    std::error_code fileError;
    std::filesystem::create_directories(traceFile.parent_path(), fileError);
    FILE* output = nullptr;
    if (!fileError)
        _wfopen_s(&output, traceFile.c_str(), L"wb");
    if (!output) {
        LogProviderMessage("R1Delta: R2Port FS trace file could not be opened\n");
        return;
    }
    for (std::size_t i = 0; i < count; ++i) {
        const FilesystemTraceRecord& record = records[i];
        fprintf(output,
            "seq=%llu fn=%s path=%s pathId=%s flags=0x%llX detail=%s result=%s\n",
            static_cast<unsigned long long>(record.sequence),
            record.functionName, record.path, record.pathId,
            static_cast<unsigned long long>(record.flags),
            record.detail, record.result);
    }
    fclose(output);
    char written[640]{};
    _snprintf_s(written, sizeof(written), _TRUNCATE,
        "R1Delta: R2Port FS trace written to %s\n", traceFile.string().c_str());
    LogProviderMessage(written);
}

} // namespace r2port
