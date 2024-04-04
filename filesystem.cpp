#include "filesystem.h"
#include <iostream>
#include <unordered_map>
#include <string>
#include <filesystem>
namespace fs = std::filesystem;

std::unordered_map<std::string, bool> fileCache;
std::unordered_map<std::string, bool> folderCache;

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
bool folderExists(const std::string& folderPath) {
    auto it = folderCache.find(folderPath);
    if (it != folderCache.end()) {
        return it->second;
    }

    bool exists = fs::exists(fs::path(folderPath)) && fs::is_directory(fs::path(folderPath));
    folderCache[folderPath] = exists;
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


    // Check if the file exists in the cache
    auto fileIt = fileCache.find(svFilePath);
    if (fileIt != fileCache.end()) {
        return fileIt->second;
    }

    if (V_IsAbsolutePath(pszFilePath)) {
        fileCache[svFilePath] = false;
        return false;
    }

    // Search for the file in "r1delta\\addons\\" subfolders
    std::string addonsPath = "r1delta\\addons\\";

    // Check if the "addons" folder exists in the cache
    if (!folderExists(addonsPath)) {
        fileCache[svFilePath] = false;
        return false;
    }

    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA((addonsPath + "*").c_str(), &findData);

    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                strcmp(findData.cFileName, ".") != 0 &&
                strcmp(findData.cFileName, "..") != 0) {

                std::string subfolderPath = addonsPath + findData.cFileName + "\\" + svFilePath;

                // Check if the subfolder exists in the cache
                std::string folderPath = addonsPath + findData.cFileName;
                if (!folderExists(folderPath)) {
                    continue;
                }

                if (::FileExists(subfolderPath.c_str())) {
                    FindClose(hFind);
                    fileCache[svFilePath] = true;
                    return true;
                }
            }
        } while (FindNextFileA(hFind, &findData));
        FindClose(hFind);
    }

    // Check if the file exists in the "r1" directory
    std::string r1Path = "r1delta\\" + svFilePath;

    if (::FileExists(r1Path.c_str())) {
        fileCache[svFilePath] = true;
        return true;
    }

    fileCache[svFilePath] = false;
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
}
FileSystem_UpdateAddonSearchPathsType FileSystem_UpdateAddonSearchPathsTypeOriginal;
__int64 __fastcall FileSystem_UpdateAddonSearchPaths(void* a1)
{
    clearFileCache();
    return FileSystem_UpdateAddonSearchPathsTypeOriginal(a1);
}