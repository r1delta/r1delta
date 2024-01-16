#include "filesystem.h"

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

FileHandle_t ReadFileFromFilesystemHook(IFileSystem* filesystem, const char* pPath, const char* pOptions, int64_t a4, uint32_t a5)
{
	// this isn't super efficient, but it's necessary, since calling addsearchpath in readfilefromvpk doesn't work, possibly refactor later
	// it also might be possible to hook functions that are called later, idk look into callstack for ReadFileFromVPK
	if (true)
		TryReplaceFile((char*)pPath);

	return readFileFromFilesystem(filesystem, pPath, pOptions, a4, a5);
}
