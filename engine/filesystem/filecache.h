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
    std::vector<std::size_t> addonsFolderCacheHashes;
    mutable SRWLOCK cacheMutex; // Mutable for const methods like TryReplaceFile if needed

    // State variables
    std::atomic<bool> initialized{ false };
    std::atomic<bool> manualRescanRequested{ false };
    CONDITION_VARIABLE cacheCondition;

    // Paths
    std::filesystem::path executableDirectory; // Store the base path
    std::filesystem::path r1deltaBasePath;     // Store the r1delta path
    std::size_t r1deltaBasePathHash;
    std::filesystem::path r1deltaAddonsPath;   // Store the addons path

    static constexpr std::size_t FNV1A_HASH_INIT = 14695981039346656037ULL;

    // Case-insensitive lowercase helper (ASCII only, sufficient for file paths)
    static constexpr unsigned char to_lower(unsigned char c) {
        return (c >= 'A' && c <= 'Z') ? (c + ('a' - 'A')) : c;
    }

    // FNV-1a hash function implementation (case-insensitive)
    static constexpr std::size_t fnv1a_hash(std::string_view sv, std::size_t hash = FNV1A_HASH_INIT) {
        for (unsigned char c : sv) { // Use unsigned char for hashing bytes correctly
            hash ^= static_cast<size_t>(to_lower(c));
            hash *= 1099511628211ULL; // FNV prime
        }
        return hash;
    }
    static constexpr std::size_t fnv1a_hash(const std::wstring_view& s, std::size_t hash = FNV1A_HASH_INIT) {
        for (uint16_t c : s) {
            hash ^= static_cast<size_t>(to_lower(static_cast<unsigned char>(c & 0xFF)));
            hash *= 1099511628211ULL; // FNV prime
        }
        return hash;
    }
    // Overload for convenience if needed (e.g. std::string)
    static std::size_t fnv1a_hash(const std::string& s, std::size_t hash = FNV1A_HASH_INIT) {
        return fnv1a_hash(std::string_view(s), hash);
    }
    static std::size_t fnv1a_hash(const std::wstring& s, std::size_t hash = FNV1A_HASH_INIT) {
        for (uint16_t c : s) {
            R1DAssert(c <= 0xFF);
            hash ^= static_cast<size_t>(to_lower(static_cast<unsigned char>(c & 0xFF)));
            hash *= 1099511628211ULL; // FNV prime
        }
        return hash;
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
    //bool FileExists(const std::string& filePath); // Checks exact absolute path match
    bool TryReplaceFile(const char* pszRelativeFilePath); // Checks relative paths in specific locations
    void UpdateCache(); // Starts the background update loop (or performs initial scan)

    void RequestManualRescan() {
        manualRescanRequested.store(true, std::memory_order_release);
        WakeAllConditionVariable(&cacheCondition); // Notify the waiting UpdateCache loop
    }
};
