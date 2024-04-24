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
#include <intrin.h>

#pragma intrinsic(_ReturnAddress)

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
            cacheCondition.wait_for(lock, std::chrono::milliseconds(5000), [this]() { return manualRescanRequested.load(); });

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

std::atomic_flag threadStarted = ATOMIC_FLAG_INIT;

void StartFileCacheThread() {
    if (!threadStarted.test_and_set()) {
        std::thread([]() { fileCache.UpdateCache(); }).detach();
    }
}

FileSystem_UpdateAddonSearchPathsType FileSystem_UpdateAddonSearchPathsTypeOriginal;
bool done = false;
__int64 __fastcall FileSystem_UpdateAddonSearchPaths(void* a1) {
    fileCache.RequestManualRescan();
    return FileSystem_UpdateAddonSearchPathsTypeOriginal(a1);
}


void replace_underscore(char* str) {
    char* pos = strstr(str, "_dir");
    if (pos) *pos = '\0';
}
IFileSystem* fsInterface;
const char* lastZipPackFilePath = "";
typedef char (*CZipPackFile__PrepareType)(__int64* a1, unsigned __int64 a2, __int64 a3);
CZipPackFile__PrepareType CZipPackFile__PrepareOriginal;
char __fastcall CZipPackFile__Prepare(__int64* a1, unsigned __int64 a2, __int64 a3) {
    static void* rettozipsearchpath = (void*)(uintptr_t(GetModuleHandleA("filesystem_stdio.dll")) + 0x17A43);
    auto ret = CZipPackFile__PrepareOriginal(a1, a2, a3);
    if (_ReturnAddress() == rettozipsearchpath) {
        reinterpret_cast<__int64(*)(__int64 a1, const char* a2)>(uintptr_t(GetModuleHandleA("filesystem_stdio.dll")) + 0x51880)((uintptr_t(a1) + 72), lastZipPackFilePath);
    }
    return ret;
}
typedef void (*CBaseFileSystem__CSearchPath__SetPathType)(void* thisptr, __int16* id);
CBaseFileSystem__CSearchPath__SetPathType CBaseFileSystem__CSearchPath__SetPathOriginal;
void __fastcall CBaseFileSystem__CSearchPath__SetPath(void* thisptr, __int16* id) 
{
    CBaseFileSystem__CSearchPath__SetPathOriginal(thisptr, id);
    static void* rettozipsearchpath = (void*)(uintptr_t(GetModuleHandleA("filesystem_stdio.dll")) + 0x017AFC);
    static auto CBaseFileSystem__FindOrAddPathIDInfo = reinterpret_cast<void* (*)(IFileSystem * fileSystem, __int16* id, int byRequestOnly)>(uintptr_t(GetModuleHandleA("filesystem_stdio.dll")) + 0x155C0);
    if (_ReturnAddress() == rettozipsearchpath) {
        *(__int64*)(uintptr_t(thisptr)+8) = __int64(CBaseFileSystem__FindOrAddPathIDInfo(fsInterface, id, -1));
        //*(__int64*)(uintptr_t(thisptr) + 32) = 0x13371337;
    }
}
typedef __int64 (*AddVPKFileType)(IFileSystem* fileSystem, char* a2, char** a3, char a4, int a5, char a6);
AddVPKFileType AddVPKFileOriginal;
__int64 __fastcall AddVPKFile(IFileSystem* fileSystem, char* a2, char** a3, char a4, int a5, char a6)
{
    replace_underscore(a2);
    //fsInterface = fileSystem;
    //static void* rettoaddonsystem = (void*)(uintptr_t(GetModuleHandleA("engine.dll")) + 0x127EF4);
    //static auto AddPackFile = reinterpret_cast<bool (*)(IFileSystem * fileSystem, const char* pPath, const char* pathID)>(uintptr_t(GetModuleHandleA("filesystem_stdio.dll")) + 0x18030);
    //
    //if (_ReturnAddress() == rettoaddonsystem) {
    //    lastZipPackFilePath = a2;
    //    return AddPackFile(fileSystem, a2, "GAME");
    //}

    return AddVPKFileOriginal(fileSystem, a2, a3, a4, a5, a6);
}