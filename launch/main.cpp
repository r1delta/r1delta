#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <TlHelp32.h>
#include <filesystem>
#include <sstream>
#include <fstream>
#include <Shlwapi.h>
#include <iostream>

namespace fs = std::filesystem;

extern "C"
{
	__declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 0x00000001;
	__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
}

HMODULE hLauncherModule;
HMODULE hHookModule;
HMODULE hTier0Module;

wchar_t exePath[4096];
wchar_t buffer[8192];

DWORD GetProcessByName(std::wstring processName)
{
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

	PROCESSENTRY32 processSnapshotEntry = { 0 };
	processSnapshotEntry.dwSize = sizeof(PROCESSENTRY32);

	if (snapshot == INVALID_HANDLE_VALUE)
		return 0;

	if (!Process32First(snapshot, &processSnapshotEntry))
		return 0;

	while (Process32Next(snapshot, &processSnapshotEntry))
	{
		if (!wcscmp(processSnapshotEntry.szExeFile, processName.c_str()))
		{
			CloseHandle(snapshot);
			return processSnapshotEntry.th32ProcessID;
		}
	}

	CloseHandle(snapshot);
	return 0;
}

bool GetExePathWide(wchar_t* dest, DWORD destSize)
{
	if (!dest)
		return NULL;
	if (destSize < MAX_PATH)
		return NULL;

	DWORD length = GetModuleFileNameW(NULL, dest, destSize);
	return length && PathRemoveFileSpecW(dest);
}

FARPROC GetLauncherMain()
{
	static FARPROC Launcher_LauncherMain;
	if (!Launcher_LauncherMain)
		Launcher_LauncherMain = GetProcAddress(hLauncherModule, "LauncherMain");
	return Launcher_LauncherMain;
}

void LibraryLoadError(DWORD dwMessageId, const wchar_t* libName, const wchar_t* location)
{
	char text[8192];
	std::string message = std::system_category().message(dwMessageId);

	sprintf_s(
		text,
		"Failed to load the %ls at \"%ls\" (%lu):\n\n%hs\n\nMake sure you followed the Spectre installation instructions carefully "
		"before reaching out for help.",
		libName,
		location,
		dwMessageId,
		message.c_str());

	if (dwMessageId == 126 && std::filesystem::exists(location))
	{
		sprintf_s(
			text,
			"%s\n\nThe file at the specified location DOES exist, so this error indicates that one of its *dependencies* failed to be "
			"found.\n\nTry the following steps:\n1. Install Visual C++ 2022 Redistributable: "
			"https://aka.ms/vs/17/release/vc_redist.x64.exe\n2. Repair game files",
			text);
	}
	else if (!fs::exists("Titanfall.exe") && (fs::exists("..\\Titanfall.exe") || fs::exists("..\\..\\Titanfall.exe")))
	{
		auto curDir = std::filesystem::current_path().filename().string();
		auto aboveDir = std::filesystem::current_path().parent_path().filename().string();
		sprintf_s(
			text,
			"%s\n\nWe detected that in your case you have extracted the files into a *subdirectory* of your Titanfall "
			"installation.\nPlease move all the files and folders from current folder (\"%s\") into the Titanfall installation directory "
			"just above (\"%s\").\n\nPlease try out the above steps by yourself before reaching out to the community for support.",
			text,
			curDir.c_str(),
			aboveDir.c_str());
	}
	else if (!fs::exists("Titanfall.exe"))
	{
		sprintf_s(
			text,
			"%s\n\nRemember: you need to unpack the contents of this archive into your Titanfall game installation directory, not just "
			"to any random folder.",
			text);
	}
	else if (fs::exists("Titanfall.exe"))
	{
		sprintf_s(
			text,
			"%s\n\nTitanfall.exe has been found in the current directory: is the game installation corrupted or did you not unpack all "
			"Spectre files here?",
			text);
	}

	MessageBoxA(GetForegroundWindow(), text, "Spectre Launcher Error", 0);
}

void EnsureOriginStarted()
{
	if (GetProcessByName(L"Origin.exe") || GetProcessByName(L"EADesktop.exe"))
		return; // already started

	// unpacked exe will crash if origin isn't open on launch, so launch it
	// get origin path from registry, code here is reversed from OriginSDK.dll
	HKEY key;
	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Origin", 0, KEY_READ, &key) != ERROR_SUCCESS)
	{
		MessageBoxA(0, "Error: failed reading Origin path!", "Spectre Launcher Error", MB_OK);
		return;
	}

	char originPath[520];
	DWORD originPathLength = 520;
	if (RegQueryValueExA(key, "ClientPath", 0, 0, (LPBYTE)&originPath, &originPathLength) != ERROR_SUCCESS)
	{
		MessageBoxA(0, "Error: failed reading Origin path!", "Spectre Launcher Error", MB_OK);
		return;
	}

	printf("[*] Starting Origin...\n");

	PROCESS_INFORMATION pi;
	memset(&pi, 0, sizeof(pi));
	STARTUPINFO si;
	memset(&si, 0, sizeof(si));
	si.cb = sizeof(STARTUPINFO);
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_MINIMIZE;
	CreateProcessA(
		originPath,
		(char*)"",
		NULL,
		NULL,
		false,
		CREATE_DEFAULT_ERROR_MODE | CREATE_NEW_PROCESS_GROUP,
		NULL,
		NULL,
		(LPSTARTUPINFOA)&si,
		&pi);

	printf("[*] Waiting for Origin...\n");

	// wait for origin to be ready, this process is created when origin is ready enough to launch game without any errors
	while (!GetProcessByName(L"OriginClientService.exe") && !GetProcessByName(L"EADesktop.exe"))
		Sleep(200);

	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
}

void PrependPath()
{
	wchar_t* pPath;
	size_t len;
	errno_t err = _wdupenv_s(&pPath, &len, L"PATH");
	if (!err)
	{
		swprintf_s(buffer, L"PATH=%s\\bin\\x64_retail\\;%s", exePath, pPath);
		auto result = _wputenv(buffer);
		if (result == -1)
		{
			MessageBoxW(
				GetForegroundWindow(),
				L"Warning: could not prepend the current directory to app's PATH environment variable. Something may break because of "
				L"that.",
				L"Spectre Launcher Warning",
				0);
		}
		free(pPath);
	}
	else
	{
		MessageBoxW(
			GetForegroundWindow(),
			L"Warning: could not get current PATH environment variable in order to prepend the current directory to it. Something may "
			L"break because of that.",
			L"Spectre Launcher Warning",
			0);
	}
}

bool ShouldLoadSpectre(int argc, char* argv[])
{
	bool loadSpectre = true;
	for (int i = 0; i < argc; i++)
		if (!strcmp(argv[i], "-vanilla"))
			loadSpectre = false;

	if (!loadSpectre)
		return loadSpectre;

	auto runSpectreFile = std::ifstream("run_Spectre.txt");
	if (runSpectreFile)
	{
		std::stringstream runSpectreFileBuffer;
		runSpectreFileBuffer << runSpectreFile.rdbuf();
		runSpectreFile.close();
		if (runSpectreFileBuffer.str()._Starts_with("0"))
			loadSpectre = false;
	}
	return loadSpectre;
}

bool LoadSpectre()
{
	FARPROC Hook_Init = nullptr;
	{
		swprintf_s(buffer, L"%s\\Spectre.dll", exePath);
		hHookModule = LoadLibraryExW(buffer, 0i64, 8u);
		if (hHookModule)
			Hook_Init = GetProcAddress(hHookModule, "InitialiseSpectre");
		if (!hHookModule || Hook_Init == nullptr)
		{
			LibraryLoadError(GetLastError(), L"Spectre.dll", buffer);
			return false;
		}
	}
	((bool (*)())Hook_Init)();

	return true;
}

HMODULE LoadDediStub(const char* name)
{
	// this works because materialsystem_dx11.dll uses relative imports, and even a DLL loaded with an absolute path will take precedence
	printf("[*]   Loading %s\n", name);
	swprintf_s(buffer, L"%s\\bin\\x64_dedi\\%hs", exePath, name);
	HMODULE h = LoadLibraryExW(buffer, 0, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!h)
	{
		wprintf(L"[*] Failed to load stub %hs from \"%ls\": %hs\n", name, buffer, std::system_category().message(GetLastError()).c_str());
	}
	return h;
}

int main(int argc, char* argv[])
{

	if (!GetExePathWide(exePath, sizeof(exePath)))
	{
		MessageBoxA(
			GetForegroundWindow(),
			"Failed getting game directory.\nThe game cannot continue and has to exit.",
			"Spectre Launcher Error",
			0);
		return 1;
	}

	bool noOriginStartup = false;
	bool dedicated = false;
	bool nostubs = false;

	for (int i = 0; i < argc; i++)
		if (!strcmp(argv[i], "-noOriginStartup"))
			noOriginStartup = true;
		else if (!strcmp(argv[i], "-dedicated"))
			dedicated = true;


	{
		PrependPath();

		if (!fs::exists("r1s_startup_args.txt"))
		{
			std::ofstream file("r1s_startup_args.txt");
			std::string defaultArgs = "-multiple";
			file.write(defaultArgs.c_str(), defaultArgs.length());
			file.close();
		}
		/*if (!fs::exists("r1s_startup_args_dedi.txt"))
		{
			std::ofstream file("r1s_startup_args_dedi.txt");
			std::string defaultArgs = "+setplaylist private_match";
			file.write(defaultArgs.c_str(), defaultArgs.length());
			file.close();
		}*/

		printf("[*] Loading tier0.dll\n");
		swprintf_s(buffer, L"%s\\bin\\x64_retail\\tier0.dll", exePath);
		hTier0Module = LoadLibraryExW(buffer, 0, LOAD_WITH_ALTERED_SEARCH_PATH);
		if (!hTier0Module)
		{
			LibraryLoadError(GetLastError(), L"tier0.dll", buffer);
			return 1;
		}

		printf("[*] Loading launcher.dll\n");
		swprintf_s(buffer, L"%s\\bin\\x64_retail\\launcher.dll", exePath);
		hLauncherModule = LoadLibraryExW(buffer, 0, LOAD_WITH_ALTERED_SEARCH_PATH);
		if (!hLauncherModule)
		{
			LibraryLoadError(GetLastError(), L"launcher.dll", buffer);
			return 1;
		}

		//bool loadSpectre = ShouldLoadSpectre(argc, argv);
		//if (loadSpectre)
		//{
		//	printf("[*] Loading Spectre\n");
		//	if (!LoadSpectre())
		//		return 1;
		//}
		//else
		//	printf("[*] Going to load the vanilla game\n");
	}

	printf("[*] Launching the game...\n");
	auto LauncherMain = GetLauncherMain();
	if (!LauncherMain)
		MessageBoxA(
			GetForegroundWindow(),
			"Failed loading launcher.dll.\nThe game cannot continue and has to exit.",
			"Spectre Launcher Error",
			0);
	// auto result = ((__int64(__fastcall*)())LauncherMain)();
	// auto result = ((signed __int64(__fastcall*)(__int64))LauncherMain)(0i64);

	return ((int(/*__fastcall*/*)(HINSTANCE, HINSTANCE, LPSTR, int))LauncherMain)(
		NULL, NULL, NULL, 0); // the parameters aren't really used anyways
}
