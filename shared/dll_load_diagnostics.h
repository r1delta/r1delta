// Explains why the R1Delta launcher could not load one of its DLLs.
//
// LoadLibrary reports ERROR_MOD_NOT_FOUND (126) both when the DLL itself is absent and when one of its imports is
// absent. launcher.dll imports the retail engine libraries (vstdlib.dll, ...) from <game>\bin\x64_retail, so a game
// folder that lost that directory fails with 126 even though launcher.dll is present. The old message sent every such
// user to reinstall the Visual C++ runtime. Diagnose() walks the static import tables to name the file that is
// actually missing, and FormatMessageText() turns the result into advice that matches the cause.
//
// Header-only and free of launcher state so the wording is unit tested (tests/dll_load_diagnostics_tests.cpp).
#pragma once

#include <windows.h>

#include <algorithm>
#include <cwchar>
#include <string>
#include <vector>

namespace r1d_dllload {

constexpr DWORD kErrorModNotFound = 126;                  // ERROR_MOD_NOT_FOUND
constexpr DWORD kErrorAccessDisabledByPolicy = 1260;      // ERROR_ACCESS_DISABLED_BY_POLICY
constexpr DWORD kErrorSystemIntegrityPolicyViolation = 4551; // ERROR_SYSTEM_INTEGRITY_POLICY_VIOLATION

// Windows returns these when Smart App Control, WDAC, AppLocker or a group policy refuses to load a file.
inline bool IsApplicationControlBlock(DWORD errorCode)
{
	return errorCode == kErrorSystemIntegrityPolicyViolation || errorCode == kErrorAccessDisabledByPolicy;
}

inline std::wstring ToLowerAscii(std::wstring s)
{
	for (wchar_t& c : s)
		if (c >= L'A' && c <= L'Z')
			c = static_cast<wchar_t>(c - L'A' + L'a');
	return s;
}

inline bool PathExists(const std::wstring& path)
{
	return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

inline std::wstring JoinPath(const std::wstring& dir, const std::wstring& name)
{
	if (dir.empty() || dir.back() == L'\\' || dir.back() == L'/')
		return dir + name;
	return dir + L"\\" + name;
}

inline std::wstring ParentDirectory(const std::wstring& path)
{
	const size_t slash = path.find_last_of(L"\\/");
	return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

inline std::wstring FileName(const std::wstring& path)
{
	const size_t slash = path.find_last_of(L"\\/");
	return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

// Returns the DLL names in the static import directory of a 64-bit PE image. False when the file can not be read
// or is not a well-formed 64-bit PE image.
inline bool ReadPeImportNames(const std::wstring& path, std::vector<std::wstring>& names)
{
	constexpr LONGLONG kMaxImageBytes = 256LL << 20;
	HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return false;
	LARGE_INTEGER size{};
	if (!GetFileSizeEx(file, &size) || size.QuadPart < static_cast<LONGLONG>(sizeof(IMAGE_DOS_HEADER)) || size.QuadPart > kMaxImageBytes)
	{
		CloseHandle(file);
		return false;
	}
	std::vector<unsigned char> data(static_cast<size_t>(size.QuadPart));
	DWORD got = 0;
	const BOOL ok = ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &got, nullptr);
	CloseHandle(file);
	if (!ok || got != data.size())
		return false;

	const size_t fileSize = data.size();
	auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(data.data());
	if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
		static_cast<size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > fileSize)
		return false;
	auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(data.data() + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
		return false;

	const IMAGE_DATA_DIRECTORY& importDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (importDir.VirtualAddress == 0)
		return true;

	auto sections = IMAGE_FIRST_SECTION(nt);
	const WORD sectionCount = nt->FileHeader.NumberOfSections;
	if (reinterpret_cast<const unsigned char*>(sections + sectionCount) > data.data() + fileSize)
		return false;

	auto rvaToOffset = [&](DWORD rva) -> size_t {
		for (WORD i = 0; i < sectionCount; ++i)
		{
			const DWORD span = sections[i].Misc.VirtualSize > sections[i].SizeOfRawData ? sections[i].Misc.VirtualSize : sections[i].SizeOfRawData;
			if (rva >= sections[i].VirtualAddress && rva < sections[i].VirtualAddress + span)
			{
				const size_t off = static_cast<size_t>(sections[i].PointerToRawData) + (rva - sections[i].VirtualAddress);
				return off < fileSize ? off : static_cast<size_t>(-1);
			}
		}
		return rva < fileSize ? static_cast<size_t>(rva) : static_cast<size_t>(-1); // header RVAs
	};

	size_t off = rvaToOffset(importDir.VirtualAddress);
	if (off == static_cast<size_t>(-1))
		return false;
	for (;; off += sizeof(IMAGE_IMPORT_DESCRIPTOR))
	{
		if (off + sizeof(IMAGE_IMPORT_DESCRIPTOR) > fileSize)
			return false;
		auto desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(data.data() + off);
		if (desc->Name == 0)
			break;
		const size_t nameOff = rvaToOffset(desc->Name);
		if (nameOff == static_cast<size_t>(-1))
			return false;
		const size_t maxLen = fileSize - nameOff;
		size_t len = 0;
		while (len < maxLen && data[nameOff + len] != 0)
			++len;
		if (len == maxLen || len == 0)
			return false;
		names.emplace_back(data.begin() + nameOff, data.begin() + nameOff + len); // import names are ASCII
	}
	return true;
}

// Directories the loader uses for the imports of a DLL loaded with LOAD_WITH_ALTERED_SEARCH_PATH, after the DLL's
// own directory: the application directory, the system directory, the Windows directory, the current directory,
// then PATH.
inline std::vector<std::wstring> LoaderSearchDirectories()
{
	std::vector<std::wstring> dirs;
	wchar_t buf[MAX_PATH];

	if (GetModuleFileNameW(nullptr, buf, MAX_PATH) > 0)
	{
		const std::wstring appDir = ParentDirectory(buf);
		if (!appDir.empty())
			dirs.push_back(appDir);
	}
	if (GetSystemDirectoryW(buf, MAX_PATH) > 0)
		dirs.push_back(buf);
	if (GetWindowsDirectoryW(buf, MAX_PATH) > 0)
		dirs.push_back(buf);
	if (GetCurrentDirectoryW(MAX_PATH, buf) > 0)
		dirs.push_back(buf);

	const DWORD pathLen = GetEnvironmentVariableW(L"PATH", nullptr, 0);
	if (pathLen > 0)
	{
		std::wstring path(pathLen, L'\0');
		const DWORD copied = GetEnvironmentVariableW(L"PATH", path.data(), pathLen);
		path.resize(copied);
		size_t start = 0;
		while (start <= path.size())
		{
			size_t end = path.find(L';', start);
			if (end == std::wstring::npos)
				end = path.size();
			if (end > start)
				dirs.push_back(path.substr(start, end - start));
			start = end + 1;
		}
	}
	return dirs;
}

inline std::wstring FindInDirectories(const std::vector<std::wstring>& dirs, const std::wstring& name)
{
	for (const std::wstring& dir : dirs)
	{
		const std::wstring candidate = JoinPath(dir, name);
		if (PathExists(candidate))
			return candidate;
	}
	return std::wstring();
}

// Depth-first walk of the static imports of 'modulePath'. Reports the first import that is not already loaded in
// this process and can not be found next to the importer or in 'dirs'. 'chain' receives the "A -> B" path of
// importers that led to it. Returns true when such an import was found.
inline bool FindFirstMissingImport(
	const std::wstring& modulePath,
	const std::vector<std::wstring>& dirs,
	std::vector<std::wstring>& visited,
	std::wstring& chain,
	std::wstring& missingName)
{
	std::vector<std::wstring> imports;
	if (!ReadPeImportNames(modulePath, imports))
		return false;

	const std::wstring self = ToLowerAscii(FileName(modulePath));
	const std::wstring chainHere = chain.empty() ? self : chain + L" -> " + self;
	std::vector<std::wstring> searchDirs{ ParentDirectory(modulePath) };
	searchDirs.insert(searchDirs.end(), dirs.begin(), dirs.end());

	// Check every direct import before descending, so the shallowest missing DLL is reported.
	std::vector<std::wstring> toDescend;
	for (const std::wstring& imp : imports)
	{
		const std::wstring lower = ToLowerAscii(imp);
		if (lower.compare(0, 4, L"api-") == 0 || lower.compare(0, 4, L"ext-") == 0)
			continue; // API set contracts are resolved by the loader, not by file name.
		if (std::find(visited.begin(), visited.end(), lower) != visited.end())
			continue;
		visited.push_back(lower);

		const std::wstring found = FindInDirectories(searchDirs, imp);
		if (found.empty())
		{
			if (GetModuleHandleW(imp.c_str()) != nullptr)
				continue; // already mapped (KnownDLL / loaded earlier)
			chain = chainHere;
			missingName = imp;
			return true;
		}
		toDescend.push_back(found);
	}
	for (const std::wstring& next : toDescend)
	{
		chain = chainHere;
		if (FindFirstMissingImport(next, dirs, visited, chain, missingName))
			return true;
	}
	return false;
}

enum class ImportKind
{
	GameFile,        // not part of Windows or a redistributable: a Titanfall (or R1Delta) file
	VcRuntime,       // Visual C++ / Universal CRT
	DirectXRuntime,  // DirectX June 2010 runtime
};

inline ImportKind ClassifyImport(const std::wstring& name)
{
	const std::wstring lower = ToLowerAscii(name);
	auto startsWith = [&](const wchar_t* prefix) { return lower.compare(0, wcslen(prefix), prefix) == 0; };
	if (startsWith(L"vcruntime") || startsWith(L"msvcp") || startsWith(L"msvcr") || startsWith(L"concrt") ||
		startsWith(L"vcomp") || startsWith(L"vccorlib") || startsWith(L"ucrtbase"))
		return ImportKind::VcRuntime;
	if (startsWith(L"d3dx9_") || startsWith(L"d3dx10_") || startsWith(L"d3dx11_") || startsWith(L"d3dcompiler_4") ||
		startsWith(L"xinput") || startsWith(L"x3daudio") || startsWith(L"xaudio2_") || startsWith(L"xapofx"))
		return ImportKind::DirectXRuntime;
	return ImportKind::GameFile;
}

struct Diagnosis
{
	DWORD errorCode = 0;
	std::wstring libName;       // "launcher.dll"
	std::wstring location;      // full path LoadLibrary was given
	std::wstring systemMessage; // FormatMessage text of errorCode
	bool libraryExists = false;
	std::wstring gameDir;       // the Titanfall install the launcher runs against (working directory)
	// The first static import of 'location' that is not found. Empty when the walk found none.
	std::wstring missingImport;
	std::wstring importChain;
	bool importExistsInGameBin = false; // <game>\bin\x64_retail\<missingImport> exists but is not on the search path
	bool gameBinExists = false;         // <game>\bin\x64_retail exists
	// Where the launcher was extracted, for the "extracted into the wrong folder" hints.
	bool titanfallExeInWorkingDir = false;
	bool titanfallExeInParentDir = false;
	std::wstring workingDirName;
	std::wstring parentDirName;
};

inline std::wstring GameBinDir(const std::wstring& gameDir)
{
	return JoinPath(JoinPath(gameDir, L"bin"), L"x64_retail");
}

inline Diagnosis Diagnose(DWORD errorCode, const std::wstring& libName, const std::wstring& location, const std::wstring& gameDir)
{
	Diagnosis d;
	d.errorCode = errorCode;
	d.libName = libName;
	d.location = location;
	d.gameDir = gameDir;
	d.libraryExists = PathExists(location);
	d.gameBinExists = PathExists(GameBinDir(gameDir));

	if (errorCode == kErrorModNotFound && d.libraryExists)
	{
		std::vector<std::wstring> visited;
		FindFirstMissingImport(location, LoaderSearchDirectories(), visited, d.importChain, d.missingImport);
		if (!d.missingImport.empty())
			d.importExistsInGameBin = PathExists(JoinPath(GameBinDir(gameDir), d.missingImport));
	}
	return d;
}

inline std::wstring FormatMessageText(const Diagnosis& d)
{
	std::wstring text = L"Failed to load " + d.libName + L" (error " + std::to_wstring(d.errorCode) + L").\n\n";
	text += L"Windows reported: " + d.systemMessage + L"\n\n";
	text += L"File: " + d.location + L"\n\n";

	if (IsApplicationControlBlock(d.errorCode))
	{
		text += L"Windows blocked this file (or a file it depends on) from loading. An application control policy refused it: "
			L"Smart App Control, Windows Defender Application Control (WDAC), AppLocker, or a policy set by your organization. "
			L"Those allow or block programs by signature and reputation, so the file is present but not allowed to run.\n\n"
			L"To play, allow the file in your policy, or on a personal PC turn Smart App Control off (Windows Security > "
			L"App & browser control > Smart App Control settings; read Microsoft's note there first), then start R1Delta again.";
		return text;
	}

	if (d.errorCode == kErrorModNotFound && d.libraryExists)
	{
		text += d.libName + L" is there, but it could not load a file it depends on.\n\n";
		if (!d.missingImport.empty())
		{
			text += L"Missing file: " + d.missingImport + L" (needed by " + d.importChain + L").\n\n";
			switch (ClassifyImport(d.missingImport))
			{
			case ImportKind::GameFile:
				if (d.importExistsInGameBin)
				{
					text += d.missingImport + L" exists in your Titanfall folder (" + JoinPath(GameBinDir(d.gameDir), d.missingImport) +
						L") but Windows was not told to look there. Start R1Delta through its launcher (r1delta.exe), not by running "
						L"Titanfall.exe directly, so it can set up the search path.";
				}
				else
				{
					text += d.missingImport + L" is a Titanfall game file. It should be here:\n" +
						JoinPath(GameBinDir(d.gameDir), d.missingImport) + L"\n\n";
					text += d.gameBinExists
						? L"That folder exists, but the file is gone from it.\n\n"
						: L"The whole bin\\x64_retail folder is missing from your Titanfall install.\n\n";
					text += L"Verify or repair your Titanfall install (Steam or EA app: verify the game files). If you installed with the "
						L"R1Delta setup, start R1Delta while holding F4 to open setup, which verifies the install and re-downloads "
						L"what is missing. Installing the Visual C++ runtime will not fix this.";
				}
				break;
			case ImportKind::VcRuntime:
				text += d.missingImport + L" belongs to the Microsoft Visual C++ Redistributable. Install the latest x64 version: "
					L"https://aka.ms/vs/17/release/vc_redist.x64.exe";
				break;
			case ImportKind::DirectXRuntime:
				text += d.missingImport + L" belongs to the Microsoft DirectX End-User Runtime (June 2010): "
					L"https://www.microsoft.com/en-us/download/details.aspx?id=8109";
				break;
			}
		}
		else
		{
			text += L"Every file " + d.libName + L" imports directly was found, so one of those files failed to load because of a file "
				L"it needs, or is not a valid 64-bit library. Verify or repair your Titanfall install and reinstall the latest x64 "
				L"Microsoft Visual C++ Redistributable (https://aka.ms/vs/17/release/vc_redist.x64.exe).";
		}
		return text;
	}

	if (d.titanfallExeInParentDir && !d.titanfallExeInWorkingDir)
	{
		text += L"R1Delta was extracted into a subdirectory of your Titanfall installation. Move all files and folders from the current "
			L"folder (\"" + d.workingDirName + L"\") into the Titanfall installation directory just above (\"" + d.parentDirName +
			L"\").\n\nTry this yourself before asking the community for help.";
		return text;
	}
	if (!d.titanfallExeInWorkingDir)
	{
		text += L"Remember: you need to unpack the contents of the R1Delta archive into your Titanfall game installation directory, "
			L"not just to any random folder.";
		return text;
	}
	text += L"Make sure you followed the R1Delta installation instructions carefully before reaching out for help.";
	return text;
}

// FormatMessage text of a Win32 error code, trimmed.
inline std::wstring FormatSystemMessage(DWORD errorCode)
{
	wchar_t* buffer = nullptr;
	const DWORD length = FormatMessageW(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr, errorCode, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
	std::wstring text = length > 0 && buffer ? std::wstring(buffer, length) : L"(no system message)";
	if (buffer)
		LocalFree(buffer);
	while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' '))
		text.pop_back();
	return text;
}

} // namespace r1d_dllload
