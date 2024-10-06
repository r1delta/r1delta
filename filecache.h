#pragma once

#include <atomic>
#include <condition_variable>
#include <unordered_set>
#include <filesystem>
#include <shared_mutex>
class FileCache {
private:
    std::unordered_set<std::size_t> cache;
    std::unordered_set<std::string> addonsFolderCache;
    std::shared_mutex cacheMutex;
    std::atomic<bool> initialized{ false };
    std::atomic<bool> manualRescanRequested{ false };
    std::condition_variable_any cacheCondition;

public:
    bool FileExists(const std::string& filePath);
    bool TryReplaceFile(const char* pszFilePath);
    void UpdateCache();

    void RequestManualRescan() {
        manualRescanRequested.store(true, std::memory_order_release);
        cacheCondition.notify_all();
    }

private:
    // Use a faster hash function (e.g., FNV-1a)
    static constexpr std::size_t fnv1a_hash(std::string_view sv) {
        std::size_t hash = 14695981039346656037ULL;
        for (char c : sv) {
            hash ^= static_cast<size_t>(c);
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    void ScanDirectory(const std::filesystem::path& directory, std::unordered_set<std::size_t>& cache, std::unordered_set<std::string>* addonsFolderCache = nullptr);
};