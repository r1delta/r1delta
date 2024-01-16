#include "windowsutil.h"

std::string ConvertToWinPath(const std::string& svInput)
{
	std::string result = svInput;

	// Flip forward slashes in file path to windows-style backslash
	for (size_t i = 0; i < result.size(); i++)
	{
		if (result[i] == '/')
		{
			result[i] = '\\';
		}
	}
	return result;
}

BOOL FileExists(LPCSTR szPath)
{
	DWORD dwAttrib = GetFileAttributesA(szPath);

	return (dwAttrib != INVALID_FILE_ATTRIBUTES &&
		!(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}