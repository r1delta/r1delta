#include "filecache.h"
#include "filesystem.h"
#include <shared_mutex>

bool FileCache::FileExists(const std::string& filePath) {
    std::size_t hashedPath = fnv1a_hash(filePath);

    // Wait for initialization using a shared lock
    {
        std::shared_lock<std::shared_mutex> sharedLock(cacheMutex);
        cacheCondition.wait(sharedLock, [this]() { return initialized.load(std::memory_order_acquire); });
    }

    // Use a shared lock for reading from the cache
    std::shared_lock<std::shared_mutex> sharedLock(cacheMutex);
    return cache.contains(hashedPath);
}
bool FileCache::TryReplaceFile(const char* pszFilePath) {
    std::string_view svFilePath(pszFilePath);

    if (V_IsAbsolutePath(pszFilePath)) {
        return false;
    }

    // Remove "/*/" prefix if present
    if (svFilePath.starts_with("/*/")) {
        svFilePath.remove_prefix(3);
    }

    // Replace '/' with '\\'
    std::string normalizedPath(svFilePath);
    std::replace(normalizedPath.begin(), normalizedPath.end(), '/', '\\');

    // Single cache lookup
    std::vector<std::string_view> paths_to_check = { "r1delta\\" };
    {
        std::shared_lock<std::shared_mutex> lock(cacheMutex); // Use shared lock for reading
        paths_to_check.insert(paths_to_check.end(), addonsFolderCache.begin(), addonsFolderCache.end());
    }

    for (const auto& prefix : paths_to_check) {
        std::string fullPath = std::string(prefix) + normalizedPath;
        std::size_t hashedPath = fnv1a_hash(fullPath);

        {
            std::shared_lock<std::shared_mutex> lock(cacheMutex); // Use shared lock for reading
            if (cache.count(hashedPath) > 0) {
                return true;
            }
        }
    }

    return false;
}


void FileCache::UpdateCache() {
    while (true) {
        std::unordered_set<std::size_t> newCache;
        std::unordered_set<std::string> newAddonsFolderCache;

        std::filesystem::create_directories("r1delta/addons");
        ScanDirectory("r1delta", newCache);
        ScanDirectory("r1delta/addons", newCache, &newAddonsFolderCache);

        {
            std::unique_lock<std::shared_mutex> lock(cacheMutex);
            cache = std::move(newCache);
            addonsFolderCache = std::move(newAddonsFolderCache);
        }

        initialized.store(true, std::memory_order_release);
        cacheCondition.notify_all();

        std::unique_lock<std::shared_mutex> lock(cacheMutex);
        cacheCondition.wait(lock, [this]() {
            return manualRescanRequested.load(std::memory_order_acquire);
            });

        manualRescanRequested.store(false, std::memory_order_release);
    }
}

void FileCache::ScanDirectory(const std::filesystem::path& directory, std::unordered_set<std::size_t>& cache, std::unordered_set<std::string>* addonsFolderCache) {
    try {
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.is_regular_file()) {
                std::size_t hashedPath = fnv1a_hash(entry.path().string());
                cache.insert(hashedPath);
            }
            else if (entry.is_directory()) {
                if (addonsFolderCache) {
                    addonsFolderCache->insert(entry.path().string());
                }
                ScanDirectory(entry.path(), cache);
            }
        }
    }
    catch (const std::exception& e) {
        // Log the error
        std::cerr << "Error scanning directory: " << directory << ": " << e.what() << std::endl;
    }
}