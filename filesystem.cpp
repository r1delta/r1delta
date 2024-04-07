#include "filesystem.h"
#include <iostream>
#include <unordered_map>
#include <string>
#include <filesystem>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <Windows.h>

namespace fs = std::filesystem;

ReadFileFromFilesystemType readFileFromFilesystem;
ReadFromCacheType readFromCache;
ReadFileFromVPKType readFileFromVPK;

class FileCache {
private:
    std::unordered_map<std::size_t, bool> cache;
    std::list<std::string> addonsFolderCache;
    std::mutex cacheMutex;
    std::mutex addonsFolderCacheMutex;
    std::atomic<bool> initialized{ false };
    std::atomic<bool> manualRescanRequested{ false };
    std::condition_variable cacheCondition;

public:
    bool FileExists(const std::string& filePath) {
        std::size_t hashedPath = hashFilePath(filePath);
        std::unique_lock<std::mutex> lock(cacheMutex);
        cacheCondition.wait(lock, [this]() { return initialized.load(); });
        return cache.count(hashedPath) > 0;
    }

    bool TryReplaceFile(const char* pszFilePath) {
        std::string svFilePath = ConvertToWinPath(pszFilePath);

        if (svFilePath.find("\\*\\") != std::string::npos)
        {
            // Erase '//*/'.
            svFilePath.erase(0, 4);
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

        std::size_t hashedFilePath = hashFilePath("r1delta\\" + svFilePath);

        {
            std::lock_guard<std::mutex> lock(cacheMutex);
            if (cache.count(hashedFilePath) > 0) {
                return true;
            }
        }

        {
            std::lock_guard<std::mutex> lock(addonsFolderCacheMutex);
            for (const auto& addonPath : addonsFolderCache) {
                std::size_t hashedAddonFilePath = hashFilePath(addonPath + "/" + svFilePath);

                {
                    std::lock_guard<std::mutex> lock(cacheMutex);
                    if (cache.count(hashedAddonFilePath) > 0) {
                        return true;
                    }
                }
            }
        }

        return false;
    }

    void UpdateCache() {
        while (true) {
            std::unordered_map<std::size_t, bool> newCache;
            std::list<std::string> newAddonsFolderCache;

            ScanDirectory("r1delta", newCache);
            ScanDirectory("r1delta/addons", newCache, &newAddonsFolderCache);

            {
                std::lock_guard<std::mutex> lock(cacheMutex);
                cache = std::move(newCache);
            }

            {
                std::lock_guard<std::mutex> lock(addonsFolderCacheMutex);
                addonsFolderCache = std::move(newAddonsFolderCache);
            }

            initialized.store(true);
            cacheCondition.notify_all();

            std::unique_lock<std::mutex> lock(cacheMutex);
            cacheCondition.wait_for(lock, std::chrono::milliseconds(500), [this]() { return manualRescanRequested.load(); });

            if (manualRescanRequested.load()) {
                manualRescanRequested.store(false);
            }
        }
    }

    void RequestManualRescan() {
        manualRescanRequested.store(true);
        cacheCondition.notify_all();
    }

private:
    std::size_t hashFilePath(const std::string& filePath) {
        return std::hash<std::string>{}(filePath);
    }

    void ScanDirectory(const std::string& directory, std::unordered_map<std::size_t, bool>& cache, std::list<std::string>* addonsFolderCache = nullptr) {
        try {
            for (const auto& entry : fs::recursive_directory_iterator(directory)) {
                if (entry.is_regular_file()) {
                    std::size_t hashedPath = hashFilePath(entry.path().string());
                    cache[hashedPath] = true;
                }
                else if (entry.is_directory() && addonsFolderCache) {
                    addonsFolderCache->push_back(entry.path().string());
                    ScanDirectory(entry.path().string(), cache);
                }
            }
        }
        catch (const std::exception& e) {
            // Log the error
            std::cerr << "Error scanning directory: " << directory << ": " << e.what() << std::endl;
        }
    }
};

FileCache fileCache;

bool V_IsAbsolutePath(const char* pStr) {
    if (!(pStr[0] && pStr[1]))
        return false;

    bool bIsAbsolute = (pStr[0] && pStr[1] == ':') ||
        ((pStr[0] == '/' || pStr[0] == '\\') && (pStr[1] == '/' || pStr[1] == '\\'));

    return bIsAbsolute;
}

bool ReadFromCacheHook(IFileSystem* filesystem, char* path, void* result) {
    if (fileCache.TryReplaceFile(path))
        return false;

    return readFromCache(filesystem, path, result);
}

FileHandle_t ReadFileFromVPKHook(VPKData* vpkInfo, __int64* b, char* filename) {
    if (fileCache.TryReplaceFile(filename)) {
        *b = -1;
        return b;
    }

    return readFileFromVPK(vpkInfo, b, filename);
}

FileHandle_t ReadFileFromFilesystemHook(IFileSystem* filesystem, const char* pPath, const char* pOptions, int64_t a4, uint32_t a5, void* a6) {
    return readFileFromFilesystem(filesystem, pPath, pOptions, a4, a5, a6);
}

void StartFileCacheThread() {
    static bool threadStarted = false;

    if (!threadStarted) {
        std::thread([]() { fileCache.UpdateCache(); }).detach();
        threadStarted = true;
    }
}

FileSystem_UpdateAddonSearchPathsType FileSystem_UpdateAddonSearchPathsTypeOriginal;

__int64 __fastcall FileSystem_UpdateAddonSearchPaths(void* a1) {
    fileCache.RequestManualRescan();
    return FileSystem_UpdateAddonSearchPathsTypeOriginal(a1);
}