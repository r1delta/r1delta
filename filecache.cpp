#include "filecache.h"
#include "filesystem.h"

bool FileCache::FileExists(const std::string& filePath) {
    std::size_t hashedPath = HashFilePath(filePath);
    std::unique_lock<std::mutex> lock(cacheMutex);
    cacheCondition.wait(lock, [this]() { return initialized.load(); });
    return cache.count(hashedPath) > 0;
}

bool FileCache::TryReplaceFile(const char* pszFilePath) {
    std::string modifiedFilePath;
    modifiedFilePath.reserve(strlen(pszFilePath));

    // Copy pszFilePath to modifiedFilePath while replacing '/' with '\\'
    for (const char* p = pszFilePath; *p; ++p) {
        if (*p == '/') {
            modifiedFilePath.push_back('\\');
        }
        else {
            modifiedFilePath.push_back(*p);
        }
    }

    std::string_view svFilePath(modifiedFilePath);

    if (svFilePath.find("\\*\\") != std::string_view::npos)
    {
        // Erase '//*/'.
        svFilePath.remove_prefix(4);
    }

    auto check = pszFilePath;
    while (*check) {
        if (*check < ' ' || *check > 128)
            return false;
        check++;
    }

    if (V_IsAbsolutePath(pszFilePath)) {
        return false;
    }

    std::size_t hashedFilePath = HashFilePath("r1delta\\" + std::string(svFilePath));

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        if (cache.count(hashedFilePath) > 0) {
            return true;
        }
    }

    std::size_t hashedAddonFilePath;
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        for (const auto& addonPath : addonsFolderCache) {
            hashedAddonFilePath = HashFilePath(std::filesystem::path(addonPath).string() + "\\" + std::string(svFilePath));
            if (cache.count(hashedAddonFilePath) > 0) {
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

        ScanDirectory("r1delta", newCache);
        ScanDirectory("r1delta/addons", newCache, &newAddonsFolderCache);

        {
            std::lock_guard<std::mutex> lock(cacheMutex);
            cache = std::move(newCache);
            addonsFolderCache = std::move(newAddonsFolderCache);
        }

        initialized.store(true);
        cacheCondition.notify_all();

        std::unique_lock<std::mutex> lock(cacheMutex);
        cacheCondition.wait_for(lock, std::chrono::milliseconds(5000), [this]() { return manualRescanRequested.load(); });

        if (manualRescanRequested.load()) {
            manualRescanRequested.store(false);
        }
    }
}

void FileCache::ScanDirectory(const std::filesystem::path& directory, std::unordered_set<std::size_t>& cache, std::unordered_set<std::string>* addonsFolderCache) {
    try {
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.is_regular_file()) {
                std::size_t hashedPath = HashFilePath(entry.path().string());
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