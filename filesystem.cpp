#include "filesystem.h"
#include <iostream>
#include <unordered_set>
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
    std::unordered_set<std::size_t> cache;
    std::unordered_set<std::string> addonsFolderCache;
    std::mutex cacheMutex;
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

        std::size_t hashedFilePath = hashFilePath("r1delta\\" + std::string(svFilePath));

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
                hashedAddonFilePath = hashFilePath(fs::path(addonPath).string() + "\\" + std::string(svFilePath));
                if (cache.count(hashedAddonFilePath) > 0) {
                    return true;
                }
            }
        }

        return false;
    }

    void UpdateCache() {
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

    void ScanDirectory(const fs::path& directory, std::unordered_set<std::size_t>& cache, std::unordered_set<std::string>* addonsFolderCache = nullptr) {
        try {
            for (const auto& entry : fs::directory_iterator(directory)) {
                if (entry.is_regular_file()) {
                    std::size_t hashedPath = hashFilePath(entry.path().string());
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

std::atomic_flag threadStarted = ATOMIC_FLAG_INIT;

void StartFileCacheThread() {
    if (!threadStarted.test_and_set()) {
        std::thread([]() { fileCache.UpdateCache(); }).detach();
    }
}

FileSystem_UpdateAddonSearchPathsType FileSystem_UpdateAddonSearchPathsTypeOriginal;

__int64 __fastcall FileSystem_UpdateAddonSearchPaths(void* a1) {
    fileCache.RequestManualRescan();
    return FileSystem_UpdateAddonSearchPathsTypeOriginal(a1);
}