#include "filesystem.h"
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

bool TryReplaceFile(const char* pszFilePath)
{
	//std::cout << "FS: " << pszFilePath << std::endl;
	std::string svFilePath = ConvertToWinPath(pszFilePath);
	if (svFilePath.find("\\*\\") != std::string::npos)
	{
		// Erase '//*/'.
		svFilePath.erase(0, 4);
	}

	if (V_IsAbsolutePath(pszFilePath))
		return false;

	// TODO: obtain 'mod' SearchPath's instead.
	svFilePath.insert(0, "platform\\");

	if (::FileExists(svFilePath.c_str()) /*|| ::FileExists(pszFilePath)*/)
	{
		return true;
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
