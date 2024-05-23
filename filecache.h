#pragma once

#include <atomic>
#include <condition_variable>
#include <unordered_set>
#include <filesystem>

class FileCache {
private:
    std::unordered_set<std::size_t> cache;
    std::unordered_set<std::string> addonsFolderCache;
    std::mutex cacheMutex;
    std::atomic<bool> initialized{ false };
    std::atomic<bool> manualRescanRequested{ false };
    std::condition_variable cacheCondition;

public:
    bool FileExists(const std::string& filePath);
    bool TryReplaceFile(const char* pszFilePath);
    void UpdateCache();

    void RequestManualRescan() {
        manualRescanRequested.store(true);
        cacheCondition.notify_all();
    }

private:
    std::size_t HashFilePath(const std::string& filePath) {
        return std::hash<std::string>{}(filePath);
    }

    void ScanDirectory(const std::filesystem::path& directory, std::unordered_set<std::size_t>& cache, std::unordered_set<std::string>* addonsFolderCache = nullptr);
};