// filecache.h
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <unordered_set>
#include <filesystem>
#include <shared_mutex>
#include <string>       // Include string
#include <string_view> // Include string_view
#include <vector>
#include "addonlist_state.h"
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
    std::vector<std::string> addonsFolderCache; // Stores enabled absolute addon directory paths in addonlist order
    std::vector<std::size_t> addonsFolderCacheHashes;
    mutable SRWLOCK cacheMutex; // Mutable for const methods like TryReplaceFile if needed

    // State variables
    std::atomic<bool> initialized{ false };
    AddonListState::RescanState rescanState;
    uint64_t publishedGeneration = 0;
    bool scanUnavailable = false;
    CONDITION_VARIABLE cacheCondition;

    // Paths
    std::filesystem::path executableDirectory; // Store the base path
    std::filesystem::path r1deltaBasePath;     // Store the r1delta path
    std::filesystem::path addonRoot;
    std::size_t r1deltaBasePathHash;

    static constexpr std::size_t FNV1A_HASH_INIT = 14695981039346656037ULL;
    // FNV-1a hash function implementation
    static constexpr std::size_t fnv1a_hash(std::string_view sv, std::size_t hash = FNV1A_HASH_INIT) {
        for (unsigned char c : sv) { // Use unsigned char for hashing bytes correctly
            hash ^= static_cast<size_t>(c);
            hash *= 1099511628211ULL; // FNV prime
        }
        return hash;
    }
    static constexpr std::size_t fnv1a_hash(const std::wstring_view& s, std::size_t hash = FNV1A_HASH_INIT) {
        for (uint16_t c : s) {
            hash ^= static_cast<size_t>(c & 0xFF);
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
            hash ^= static_cast<size_t>(c & 0xFF);
            hash *= 1099511628211ULL; // FNV prime
        }
        return hash;
    }

    // Internal scanning methods
    void ScanDirectory(const std::filesystem::path& directory,
        std::unordered_set<std::size_t>& currentCache);
    void BuildSnapshot(
        const std::filesystem::path& scanAddonRoot,
        std::unordered_set<std::size_t>& newCache,
        std::vector<std::string>& newAddonsFolderCache);
    void CommitSnapshot(
        uint64_t generation,
        std::unordered_set<std::size_t>&& newCache,
        std::vector<std::string>&& newAddonsFolderCache);
    bool WaitForInitialPublication();

public:
    class ReadLease {
        friend class FileCache;
        const FileCache* owner = nullptr;
        SRWLOCK* lock = nullptr;

        ReadLease() = default;
        ReadLease(const FileCache* owner, SRWLOCK* lock)
            : owner(owner), lock(lock) {
            AcquireSRWLockShared(lock);
        }

    public:
        ReadLease(const ReadLease&) = delete;
        ReadLease& operator=(const ReadLease&) = delete;
        ReadLease(ReadLease&& other) noexcept
            : owner(other.owner), lock(other.lock) {
            other.owner = nullptr;
            other.lock = nullptr;
        }
        ReadLease& operator=(ReadLease&& other) noexcept {
            if (this != &other) {
                if (lock)
                    ReleaseSRWLockShared(lock);
                owner = other.owner;
                lock = other.lock;
                other.owner = nullptr;
                other.lock = nullptr;
            }
            return *this;
        }
        ~ReadLease() {
            if (lock)
                ReleaseSRWLockShared(lock);
        }
        explicit operator bool() const { return lock != nullptr; }
    };

    // Singleton specific: Public static access method
    static FileCache& GetInstance() {
        static FileCache instance; // Thread-safe initialization in C++11+
        return instance;
    }

    // Public interface methods (remain instance methods)
    //bool FileExists(const std::string& filePath); // Checks exact absolute path match
    bool TryReplaceFile(const char* pszRelativeFilePath); // Checks relative paths in specific locations
    ReadLease AcquireReadLease();
    bool ResolveReplacementFile(
        const ReadLease& lease,
        const char* pszRelativeFilePath,
        char* pszResolvedPath,
        size_t resolvedPathSize);
    std::vector<std::string> FindReplacementFileNames(
        const char* pszRelativeDirectory,
        const char* pszPattern);
    void UpdateCache(); // Starts the background update loop (or performs initial scan)

    void RequestManualRescan();
    void BeginAddonSearchPathUpdate();
    bool PublishAddonSearchPathSnapshot(const std::filesystem::path& newAddonRoot);
    bool EndAddonSearchPathUpdate();
};
