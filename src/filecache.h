// filecache.h
#pragma once

#include <atomic>
#include <condition_variable>
#include <unordered_set>
#include <filesystem>
#include <shared_mutex>
#include <string>       // Include string
#include <string_view> // Include string_view
#include "core.h"       // For HashStrings, Msg, Warning, ZoneScoped etc.
#include "filesystem.h" // For GetExecutableDirectory, V_IsAbsolutePath

class FileCache {
private:
    // Singleton specific: Private constructor and deleted copy/move operations
    FileCache(); // Constructor is now private
    FileCache(const FileCache&) = delete;
    FileCache& operator=(const FileCache&) = delete;
    FileCache(FileCache&&) = delete;
    FileCache& operator=(FileCache&&) = delete;

    // Cache data structures
    std::unordered_set<std::size_t> cache; // Stores hashes of absolute, normalized file paths
    std::unordered_set<std::string, HashStrings, std::equal_to<>> addonsFolderCache; // Stores absolute, normalized addon directory paths
    mutable std::shared_mutex cacheMutex; // Mutable for const methods like TryReplaceFile if needed

    // State variables
    std::atomic<bool> initialized{ false };
    std::atomic<bool> manualRescanRequested{ false };
    std::condition_variable_any cacheCondition;

    // Paths
    std::filesystem::path executableDirectory; // Store the base path
    std::filesystem::path r1deltaBasePath;     // Store the r1delta path
    std::filesystem::path r1deltaAddonsPath;   // Store the addons path

    // FNV-1a hash function implementation
    static constexpr std::size_t fnv1a_hash(std::string_view sv, std::size_t hash = 14695981039346656037ULL) {
        for (unsigned char c : sv) { // Use unsigned char for hashing bytes correctly
            hash ^= static_cast<size_t>(c);
            hash *= 1099511628211ULL; // FNV prime
        }
        return hash;
    }
    // Overload for convenience if needed (e.g. std::string)
    static std::size_t fnv1a_hash(const std::string& s) {
        return fnv1a_hash(std::string_view(s));
    }

    // Internal scanning method
    void ScanDirectory(const std::filesystem::path& directory,
        std::unordered_set<std::size_t>& currentCache,
        std::unordered_set<std::string, HashStrings, std::equal_to<>>* currentAddonsFolders = nullptr);

public:
    // Singleton specific: Public static access method
    static FileCache& GetInstance() {
        static FileCache instance; // Thread-safe initialization in C++11+
        return instance;
    }

    // Public interface methods (remain instance methods)
    bool FileExists(const std::string& filePath); // Checks exact absolute path match
    bool TryReplaceFile(const char* pszRelativeFilePath); // Checks relative paths in specific locations
    void UpdateCache(); // Starts the background update loop (or performs initial scan)

    void RequestManualRescan() {
        manualRescanRequested.store(true, std::memory_order_release);
        cacheCondition.notify_all(); // Notify the waiting UpdateCache loop
    }
};
