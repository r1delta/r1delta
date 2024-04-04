#include "filesystem.h"
#include <iostream>
#include <unordered_map>
#include <string>
#include <filesystem>
namespace fs = std::filesystem;

std::unordered_map<std::string, bool> fileCache;
std::unordered_map<std::string, bool> folderCache;
std::list<std::string> addonsFoldersCache;

ReadFileFromFilesystemType readFileFromFilesystem;
ReadFromCacheType readFromCache;
ReadFileFromVPKType readFileFromVPK;

bool V_IsAbsolutePath(const char* pStr)
{
	if (!(pStr[0] && pStr[1]))
		return false;

	bool bIsAbsolute = (pStr[0] && pStr[1] == ':') ||
		((pStr[0] == '/' || pStr[0] == '\\') && (pStr[1] == '/' || pStr[1] == '\\'));


	return bIsAbsolute;
}

bool folderExistsCheckCache(const std::string& folderPath) {
    auto it = folderCache.find(folderPath);
    if (it != folderCache.end()) {
        return it->second;
    }
    else {
        bool exists = fs::exists(folderPath);
        folderCache[folderPath] = exists;
        return exists;
    }
}
bool fileExistsCheckCache(const std::string& filePath) {
    auto it = fileCache.find(filePath);
    if (it != fileCache.end()) {
        return it->second;
    }

    // Check if every parent directory exists
    std::filesystem::path path(filePath);
    std::filesystem::path parentPath;
    for (const auto& element : path) {
        parentPath /= element;
        if (!folderExistsCheckCache(parentPath.string())) {
            fileCache[filePath] = false;
            return false;
        }
    }

    // Check if the file exists
    bool exists = std::filesystem::exists(filePath);
    fileCache[filePath] = exists;
    return exists;
}

bool TryReplaceFile(const char* pszFilePath) {

    std::string svFilePath = ConvertToWinPath(pszFilePath);

    if (svFilePath.find("\\\\*\\\\") != std::string::npos) {
        // Erase '//*/'.
        svFilePath.erase(0, 4);
    }
    auto check = pszFilePath;
    while (*check) {
        if (*check < ' ')
            return false;
        if (*check > 128)
            return false;
        check++;
    }
    if (V_IsAbsolutePath(pszFilePath)) {
        fileCache[svFilePath] = false;
        return false;
    }
    std::string folderPath = "r1delta";
    std::string filePath = folderPath + "/" + svFilePath;
    if (fileExistsCheckCache(filePath)) {
        return true;
    }
    // Check for addons/*/ folder
    for (const auto& addonPath : addonsFoldersCache) {
        std::string addonFilePath = addonPath + "/" + svFilePath;
        if (fileExistsCheckCache(addonFilePath)) {
            return true;
        }
    }

    return false;
}


bool ReadFromCacheHook(IFileSystem* filesystem, char* path, void* result)
{
	// move this to a convar at some point when we can read them in native
	//Log::Info("ReadFromCache %s", path);

	if (TryReplaceFile(path))
		return false;

	return readFromCache(filesystem, path, result);
}

FileHandle_t ReadFileFromVPKHook(VPKData* vpkInfo, __int64* b, char* filename)
{
	// move this to a convar at some point when we can read them in native
	//Log::Info("ReadFileFromVPK %s %s", filename, vpkInfo->path);

	// there is literally never any reason to compile here, since we'll always compile in ReadFileFromFilesystemHook in the same codepath
	// this is called
	if (TryReplaceFile(filename))
	{
		*b = -1;
		return b;
	}

	return readFileFromVPK(vpkInfo, b, filename);
}

FileHandle_t ReadFileFromFilesystemHook(IFileSystem* filesystem, const char* pPath, const char* pOptions, int64_t a4, uint32_t a5, void* a6)
{
	// this isn't super efficient, but it's necessary, since calling addsearchpath in readfilefromvpk doesn't work, possibly refactor later
	// it also might be possible to hook functions that are called later, idk look into callstack for ReadFileFromVPK
	//if (true)
	//	TryReplaceFile((char*)pPath);

	return readFileFromFilesystem(filesystem, pPath, pOptions, a4, a5, a6);
}

void clearFileCache() {
    fileCache.clear();
    folderCache.clear();
    addonsFoldersCache.clear();
    for (const auto& entry : std::filesystem::directory_iterator("r1delta/addons")) {
        if (entry.is_directory()) {
            addonsFoldersCache.push_back(entry.path().string());
        }
    }
}
FileSystem_UpdateAddonSearchPathsType FileSystem_UpdateAddonSearchPathsTypeOriginal;
__int64 __fastcall FileSystem_UpdateAddonSearchPaths(void* a1)
{
    clearFileCache();
    return FileSystem_UpdateAddonSearchPathsTypeOriginal(a1);
}