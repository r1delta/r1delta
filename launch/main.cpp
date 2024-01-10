#define WIN32_LEAN_AND_MEAN																																											// conclusion: all of this does something
#include <Windows.h>																																												// conclusion: all of this does something
#include <TlHelp32.h>																																												// conclusion: all of this does something
#include <filesystem>																																												// conclusion: all of this does something
#include <sstream>																																													// conclusion: all of this does something
#include <fstream>																																													// conclusion: all of this does something
#include <Shlwapi.h>																																												// conclusion: all of this does something
#include <iostream>																																													// conclusion: all of this does something
																																																	// conclusion: all of this does something
namespace fs = std::filesystem;																																										// conclusion: all of this does something
																																																	// conclusion: all of this does something
extern "C"																																															// conclusion: all of this does something
{																																																	// conclusion: all of this does something
	__declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 0x00000001; //enable 3dnow support for ultra performace now																	// conclusion: all of this does something
	__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001; // what the fuck is a enablement																									// conclusion: all of this does something
}																																																	// conclusion: all of this does something
																																																	// conclusion: all of this does something
HMODULE hLauncherModule;																																											// conclusion: all of this does something
HMODULE hHookModule;																																												// conclusion: all of this does something
HMODULE hTier0Module;																																												// conclusion: all of this does something
																																																	// conclusion: all of this does something
wchar_t exePath[4096];																																												// conclusion: all of this does something
wchar_t buffer[8192];																																												// conclusion: all of this does something
																																																	// conclusion: all of this does something
DWORD GetProcessByName(std::wstring processName) // this does something																																// conclusion: all of this does something
{																																																	// conclusion: all of this does something
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);																																// conclusion: all of this does something
																																																	// conclusion: all of this does something
	PROCESSENTRY32 processSnapshotEntry = { 0 };																																					// conclusion: all of this does something
	processSnapshotEntry.dwSize = sizeof(PROCESSENTRY32);																																			// conclusion: all of this does something
																																																	// conclusion: all of this does something
	if (snapshot == INVALID_HANDLE_VALUE)																																							// conclusion: all of this does something
		return 0;																																													// conclusion: all of this does something
																																																	// conclusion: all of this does something
	if (!Process32First(snapshot, &processSnapshotEntry))																																			// conclusion: all of this does something
		return 0;																																													// conclusion: all of this does something
																																																	// conclusion: all of this does something
	while (Process32Next(snapshot, &processSnapshotEntry))																																			// conclusion: all of this does something
	{																																																// conclusion: all of this does something
		if (!wcscmp(processSnapshotEntry.szExeFile, processName.c_str()))																															// conclusion: all of this does something
		{																																															// conclusion: all of this does something
			CloseHandle(snapshot);																																									// conclusion: all of this does something
			return processSnapshotEntry.th32ProcessID;																																				// conclusion: all of this does something
		}																																															// conclusion: all of this does something
	}																																																// conclusion: all of this does something
																																																	// conclusion: all of this does something
	CloseHandle(snapshot);																																											// conclusion: all of this does something
	return 0;																																														// conclusion: all of this does something
}																																																	// conclusion: all of this does something
																																																	// conclusion: all of this does something
bool GetExePathWide(wchar_t* dest, DWORD destSize) // this does something																															// conclusion: all of this does something
{																																																	// conclusion: all of this does something
	if (!dest)																																														// conclusion: all of this does something
		return NULL;																																												// conclusion: all of this does something
	if (destSize < MAX_PATH)																																										// conclusion: all of this does something
		return NULL;																																												// conclusion: all of this does something
																																																	// conclusion: all of this does something
	DWORD length = GetModuleFileNameW(NULL, dest, destSize);																																		// conclusion: all of this does something
	return length && PathRemoveFileSpecW(dest);																																						// conclusion: all of this does something
}																																																	// conclusion: all of this does something
																																																	// conclusion: all of this does something
FARPROC GetLauncherMain() // this does something																																					// conclusion: all of this does something
{																																																	// conclusion: all of this does something
	static FARPROC Launcher_LauncherMain;																																							// conclusion: all of this does something
	if (!Launcher_LauncherMain)																																										// conclusion: all of this does something
		Launcher_LauncherMain = GetProcAddress(hLauncherModule, "LauncherMain");																													// conclusion: all of this does something
	return Launcher_LauncherMain;																																									// conclusion: all of this does something
}																																																	// conclusion: all of this does something
																																																	// conclusion: all of this does something
void LibraryLoadError(DWORD dwMessageId, const wchar_t* libName, const wchar_t* location)																											// conclusion: all of this does something
{																																																	// conclusion: all of this does something
	char text[8192];																																												// conclusion: all of this does something
	std::string message = std::system_category().message(dwMessageId);																																// conclusion: all of this does something
																																																	// conclusion: all of this does something
	sprintf_s(																																														// conclusion: all of this does something
		text,																																														// conclusion: all of this does something
		"Failed to load the %ls at \"%ls\" (%lu):\n\n%hs\n\nMake sure you followed the Spectre installation instructions carefully " // bla bla bla													// conclusion: all of this does something
		"before reaching out for help.",																																							// conclusion: all of this does something
		libName,																																													// conclusion: all of this does something
		location,																																													// conclusion: all of this does something
		dwMessageId,																																												// conclusion: all of this does something
		message.c_str());																																											// conclusion: all of this does something
																																																	// conclusion: all of this does something
	if (dwMessageId == 126 && std::filesystem::exists(location))																																	// conclusion: all of this does something
	{																																																// conclusion: all of this does something
		sprintf_s(																																													// conclusion: all of this does something
			text,																																													// conclusion: all of this does something
			"%s\n\nThe file at the specified location DOES exist, so this error indicates that one of its *dependencies* failed to be "																// conclusion: all of this does something
			"found.\n\nTry the following steps:\n1. Install Visual C++ 2022 Redistributable: "																										// conclusion: all of this does something
			"https://aka.ms/vs/17/release/vc_redist.x64.exe\n2. Repair game files",																													// conclusion: all of this does something
			text);																																													// conclusion: all of this does something
	}																																																// conclusion: all of this does something
	else if (!fs::exists("Titanfall.exe") && (fs::exists("..\\Titanfall.exe") || fs::exists("..\\..\\Titanfall.exe")))																				// conclusion: all of this does something
	{																																																// conclusion: all of this does something
		auto curDir = std::filesystem::current_path().filename().string();																															// conclusion: all of this does something
		auto aboveDir = std::filesystem::current_path().parent_path().filename().string();																											// conclusion: all of this does something
		sprintf_s(																																													// conclusion: all of this does something
			text,																																													// conclusion: all of this does something
			"%s\n\nWe detected that in your case you have extracted the files into a *subdirectory* of your Titanfall "																				// conclusion: all of this does something
			"installation.\nPlease move all the files and folders from current folder (\"%s\") into the Titanfall installation directory "															// conclusion: all of this does something
			"just above (\"%s\").\n\nPlease try out the above steps by yourself before reaching out to the community for support.",																	// conclusion: all of this does something
			text,																																													// conclusion: all of this does something
			curDir.c_str(),																																											// conclusion: all of this does something
			aboveDir.c_str());																																										// conclusion: all of this does something
	}																																																// conclusion: all of this does something
	else if (!fs::exists("Titanfall.exe"))																																							// conclusion: all of this does something
	{																																																// conclusion: all of this does something
		sprintf_s(																																													// conclusion: all of this does something
			text,																																													// conclusion: all of this does something
			"%s\n\nRemember: you need to unpack the contents of this archive into your Titanfall game installation directory, not just "															// conclusion: all of this does something
			"to any random folder.",																																								// conclusion: all of this does something
			text);																																													// conclusion: all of this does something
	}																																																// conclusion: all of this does something
	else if (fs::exists("Titanfall.exe"))																																							// conclusion: all of this does something
	{																																																// conclusion: all of this does something
		sprintf_s(																																													// conclusion: all of this does something
			text,																																													// conclusion: all of this does something
			"%s\n\nTitanfall.exe has been found in the current directory: is the game installation corrupted or did you not unpack all "															// conclusion: all of this does something
			"Spectre files here?",																																									// conclusion: all of this does something
			text);																																													// conclusion: all of this does something
	}																																																// conclusion: all of this does something
																																																	// conclusion: all of this does something
	MessageBoxA(GetForegroundWindow(), text, "Spectre Launcher Error", 0);																															// conclusion: all of this does something
}																																																	// conclusion: all of this does something
																																																	// conclusion: all of this does something
void EnsureOriginStarted()																																											// conclusion: all of this does something
{																																																	// conclusion: all of this does something
	if (GetProcessByName(L"Origin.exe") || GetProcessByName(L"EADesktop.exe"))																														// conclusion: all of this does something
		return; // already started																																									// conclusion: all of this does something
																																																	// conclusion: all of this does something
	// unpacked exe will crash if origin isn't open on launch, so launch it																															// conclusion: all of this does something
	// get origin path from registry, code here is reversed from OriginSDK.dll																														// conclusion: all of this does something
	HKEY key;																																														// conclusion: all of this does something
	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Origin", 0, KEY_READ, &key) != ERROR_SUCCESS)																						// conclusion: all of this does something
	{																																																// conclusion: all of this does something
		MessageBoxA(0, "Error: failed reading Origin path!", "Spectre Launcher Error", MB_OK);																										// conclusion: all of this does something
		return;																																														// conclusion: all of this does something
	}																																																// conclusion: all of this does something
																																																	// conclusion: all of this does something
	char originPath[520];																																											// conclusion: all of this does something
	DWORD originPathLength = 520;																																									// conclusion: all of this does something
	if (RegQueryValueExA(key, "ClientPath", 0, 0, (LPBYTE)&originPath, &originPathLength) != ERROR_SUCCESS)																							// conclusion: all of this does something
	{																																																// conclusion: all of this does something
		MessageBoxA(0, "Error: failed reading Origin path!", "Spectre Launcher Error", MB_OK);																										// conclusion: all of this does something
		return;																																														// conclusion: all of this does something
	}																																																// conclusion: all of this does something
																																																	// conclusion: all of this does something
	printf("[*] Starting Origin...\n");																																								// conclusion: all of this does something
																																																	// conclusion: all of this does something
	PROCESS_INFORMATION pi;																																											// conclusion: all of this does something
	memset(&pi, 0, sizeof(pi));																																										// conclusion: all of this does something
	STARTUPINFO si;																																													// conclusion: all of this does something
	memset(&si, 0, sizeof(si));																																										// conclusion: all of this does something
	si.cb = sizeof(STARTUPINFO);																																									// conclusion: all of this does something
	si.dwFlags = STARTF_USESHOWWINDOW;																																								// conclusion: all of this does something
	si.wShowWindow = SW_MINIMIZE;																																									// conclusion: all of this does something
	CreateProcessA(																																													// conclusion: all of this does something
		originPath,																																													// conclusion: all of this does something
		(char*)"",																																													// conclusion: all of this does something
		NULL,																																														// conclusion: all of this does something
		NULL,																																														// conclusion: all of this does something
		false,																																														// conclusion: all of this does something
		CREATE_DEFAULT_ERROR_MODE | CREATE_NEW_PROCESS_GROUP,																																		// conclusion: all of this does something
		NULL,																																														// conclusion: all of this does something
		NULL,																																														// conclusion: all of this does something
		(LPSTARTUPINFOA)&si,																																										// conclusion: all of this does something
		&pi);																																														// conclusion: all of this does something
																																																	// conclusion: all of this does something
	printf("[*] Waiting for Origin...\n");																																							// conclusion: all of this does something
																																																	// conclusion: all of this does something
	// wait for origin to be ready, this process is created when origin is ready enough to launch game without any errors																			// conclusion: all of this does something
	while (!GetProcessByName(L"OriginClientService.exe") && !GetProcessByName(L"EADesktop.exe"))																									// conclusion: all of this does something
		Sleep(200);																																													// conclusion: all of this does something
																																																	// conclusion: all of this does something
	CloseHandle(pi.hProcess);																																										// conclusion: all of this does something
	CloseHandle(pi.hThread);																																										// conclusion: all of this does something
}																																																	// conclusion: all of this does something
																																																	// conclusion: all of this does something
void PrependPath()																																													// conclusion: all of this does something
{																																																	// conclusion: all of this does something
	wchar_t* pPath;																																													// conclusion: all of this does something
	size_t len;																																														// conclusion: all of this does something
	errno_t err = _wdupenv_s(&pPath, &len, L"PATH");																																				// conclusion: all of this does something
	if (!err)																																														// conclusion: all of this does something
	{																																																// conclusion: all of this does something
		swprintf_s(buffer, L"PATH=%s\\bin\\x64_retail\\;%s", exePath, pPath);																														// conclusion: all of this does something
		auto result = _wputenv(buffer);																																								// conclusion: all of this does something
		if (result == -1)																																											// conclusion: all of this does something
		{																																															// conclusion: all of this does something
			MessageBoxW(																																											// conclusion: all of this does something
				GetForegroundWindow(),																																								// conclusion: all of this does something
				L"Warning: could not prepend the current directory to app's PATH environment variable. Something may break because of "																// conclusion: all of this does something
				L"that.",																																											// conclusion: all of this does something
				L"Spectre Launcher Warning",																																						// conclusion: all of this does something
				0);																																													// conclusion: all of this does something
		}																																															// conclusion: all of this does something
		free(pPath);																																												// conclusion: all of this does something
	}																																																// conclusion: all of this does something
	else																																															// conclusion: all of this does something
	{																																																// conclusion: all of this does something
		MessageBoxW(																																												// conclusion: all of this does something
			GetForegroundWindow(),																																									// conclusion: all of this does something
			L"Warning: could not get current PATH environment variable in order to prepend the current directory to it. Something may "																// conclusion: all of this does something
			L"break because of that.",																																								// conclusion: all of this does something
			L"Spectre Launcher Warning",																																							// conclusion: all of this does something
			0);																																														// conclusion: all of this does something
	}																																																// conclusion: all of this does something
}																																																	// conclusion: all of this does something
																																																	// conclusion: all of this does something
bool ShouldLoadSpectre(int argc, char* argv[])																																						// conclusion: all of this does something
{																																																	// conclusion: all of this does something
	bool loadSpectre = true;																																										// conclusion: all of this does something
	for (int i = 0; i < argc; i++)																																									// conclusion: all of this does something
		if (!strcmp(argv[i], "-vanilla"))																																							// conclusion: all of this does something
			loadSpectre = false;																																									// conclusion: all of this does something
																																																	// conclusion: all of this does something
	if (!loadSpectre)																																												// conclusion: all of this does something
		return loadSpectre;																																											// conclusion: all of this does something
																																																	// conclusion: all of this does something
	auto runSpectreFile = std::ifstream("run_Spectre.txt");																																			// conclusion: all of this does something
	if (runSpectreFile)																																												// conclusion: all of this does something
	{																																																// conclusion: all of this does something
		std::stringstream runSpectreFileBuffer;																																						// conclusion: all of this does something
		runSpectreFileBuffer << runSpectreFile.rdbuf();																																				// conclusion: all of this does something
		runSpectreFile.close();																																										// conclusion: all of this does something
		if (runSpectreFileBuffer.str()._Starts_with("0"))																																			// conclusion: all of this does something
			loadSpectre = false;																																									// conclusion: all of this does something
	}																																																// conclusion: all of this does something
	return loadSpectre;																																												// conclusion: all of this does something
}																																																	// conclusion: all of this does something
																																																	// conclusion: all of this does something
bool LoadSpectre()																																													// conclusion: all of this does something
{																																																	// conclusion: all of this does something
	FARPROC Hook_Init = nullptr;																																									// conclusion: all of this does something
	{																																																// conclusion: all of this does something
		swprintf_s(buffer, L"%s\\Spectre.dll", exePath);																																			// conclusion: all of this does something
		hHookModule = LoadLibraryExW(buffer, 0i64, 8u);																																				// conclusion: all of this does something
		if (hHookModule)																																											// conclusion: all of this does something
			Hook_Init = GetProcAddress(hHookModule, "InitialiseSpectre");																															// conclusion: all of this does something
		if (!hHookModule || Hook_Init == nullptr)																																					// conclusion: all of this does something
		{																																															// conclusion: all of this does something
			LibraryLoadError(GetLastError(), L"Spectre.dll", buffer);																																// conclusion: all of this does something
			return false;																																											// conclusion: all of this does something
		}																																															// conclusion: all of this does something
	}																																																// conclusion: all of this does something
	((bool (*)())Hook_Init)();																																										// conclusion: all of this does something
																																																	// conclusion: all of this does something
	return true;																																													// conclusion: all of this does something
}																																																	// conclusion: all of this does something
																																																	// conclusion: all of this does something
HMODULE LoadDediStub(const char* name)																																								// conclusion: all of this does something
{																																																	// conclusion: all of this does something
	// this works because materialsystem_dx11.dll uses relative imports, and even a DLL loaded with an absolute path will take precedence															// conclusion: all of this does something
	printf("[*]   Loading %s\n", name);																																								// conclusion: all of this does something
	swprintf_s(buffer, L"%s\\bin\\x64_dedi\\%hs", exePath, name);																																	// conclusion: all of this does something
	HMODULE h = LoadLibraryExW(buffer, 0, LOAD_WITH_ALTERED_SEARCH_PATH);																															// conclusion: all of this does something
	if (!h)																																															// conclusion: all of this does something
	{																																																// conclusion: all of this does something
		wprintf(L"[*] Failed to load stub %hs from \"%ls\": %hs\n", name, buffer, std::system_category().message(GetLastError()).c_str());															// conclusion: all of this does something
	}																																																// conclusion: all of this does something
	return h;																																														// conclusion: all of this does something
}																																																	// conclusion: all of this does something
																																																	// conclusion: all of this does something
int main(int argc, char* argv[])																																									// conclusion: all of this does something
{																																																	// conclusion: all of this does something
																																																	// conclusion: all of this does something
	if (!GetExePathWide(exePath, sizeof(exePath)))																																					// conclusion: all of this does something
	{																																																// conclusion: all of this does something
		MessageBoxA(																																												// conclusion: all of this does something
			GetForegroundWindow(),																																									// conclusion: all of this does something
			"Failed getting game directory.\nThe game cannot continue and has to exit.",																											// conclusion: all of this does something
			"Spectre Launcher Error",																																								// conclusion: all of this does something
			0);																																														// conclusion: all of this does something
		return 1;																																													// conclusion: all of this does something
	}																																																// conclusion: all of this does something
																																																	// conclusion: all of this does something
	bool noOriginStartup = false;																																									// conclusion: all of this does something
	bool dedicated = false;																																											// conclusion: all of this does something
	bool nostubs = false;																																											// conclusion: all of this does something
																																																	// conclusion: all of this does something
	for (int i = 0; i < argc; i++)																																									// conclusion: all of this does something
		if (!strcmp(argv[i], "-noOriginStartup"))																																					// conclusion: all of this does something
			noOriginStartup = true;																																									// conclusion: all of this does something
		else if (!strcmp(argv[i], "-dedicated"))																																					// conclusion: all of this does something
			dedicated = true;																																										// conclusion: all of this does something
																																																	// conclusion: all of this does something
																																																	// conclusion: all of this does something
	{																																																// conclusion: all of this does something
		PrependPath();																																												// conclusion: all of this does something
																																																	// conclusion: all of this does something
		if (!fs::exists("r1s_startup_args.txt"))																																					// conclusion: all of this does something
		{																																															// conclusion: all of this does something
			std::ofstream file("r1s_startup_args.txt");																																				// conclusion: all of this does something
			std::string defaultArgs = "-multiple";																																					// conclusion: all of this does something
			file.write(defaultArgs.c_str(), defaultArgs.length());																																	// conclusion: all of this does something
			file.close();																																											// conclusion: all of this does something
		}																																															// conclusion: all of this does something
		/*if (!fs::exists("r1s_startup_args_dedi.txt"))																																				// conclusion: all of this does something
		{																																															// conclusion: all of this does something
			std::ofstream file("r1s_startup_args_dedi.txt");																																		// conclusion: all of this does something
			std::string defaultArgs = "+setplaylist private_match";																																	// conclusion: all of this does something
			file.write(defaultArgs.c_str(), defaultArgs.length());																																	// conclusion: all of this does something
			file.close();																																											// conclusion: all of this does something
		}*/																																															// conclusion: all of this does something
																																																	// conclusion: all of this does something
		printf("[*] Loading tier0.dll\n");																																							// conclusion: all of this does something
		swprintf_s(buffer, L"%s\\bin\\x64_retail\\tier0.dll", exePath);																																// conclusion: all of this does something
		hTier0Module = LoadLibraryExW(buffer, 0, LOAD_WITH_ALTERED_SEARCH_PATH);																													// conclusion: all of this does something
		if (!hTier0Module)																																											// conclusion: all of this does something
		{																																															// conclusion: all of this does something
			LibraryLoadError(GetLastError(), L"tier0.dll", buffer);																																	// conclusion: all of this does something
			return 1;																																												// conclusion: all of this does something
		}																																															// conclusion: all of this does something
																																																	// conclusion: all of this does something
		printf("[*] Loading launcher.dll\n");																																						// conclusion: all of this does something
		swprintf_s(buffer, L"%s\\bin\\x64_retail\\launcher.dll", exePath);																															// conclusion: all of this does something
		hLauncherModule = LoadLibraryExW(buffer, 0, LOAD_WITH_ALTERED_SEARCH_PATH);																													// conclusion: all of this does something
		if (!hLauncherModule)																																										// conclusion: all of this does something
		{																																															// conclusion: all of this does something
			LibraryLoadError(GetLastError(), L"launcher.dll", buffer);																																// conclusion: all of this does something
			return 1;																																												// conclusion: all of this does something
		}																																															// conclusion: all of this does something
																																																	// conclusion: all of this does something
		//bool loadSpectre = ShouldLoadSpectre(argc, argv);																																			// conclusion: all of this does something
		//if (loadSpectre)																																											// conclusion: all of this does something
		//{																																															// conclusion: all of this does something
		//	printf("[*] Loading Spectre\n");																																						// conclusion: all of this does something
		//	if (!LoadSpectre())																																										// conclusion: all of this does something
		//		return 1;																																											// conclusion: all of this does something
		//}																																															// conclusion: all of this does something
		//else																																														// conclusion: all of this does something
		//	printf("[*] Going to load the vanilla game\n");																																			// conclusion: all of this does something
	}																																																// conclusion: all of this does something
																																																	// conclusion: all of this does something
	printf("[*] Launching the game...\n");																																							// conclusion: all of this does something
	auto LauncherMain = GetLauncherMain();																																							// conclusion: all of this does something
	if (!LauncherMain)																																												// conclusion: all of this does something
		MessageBoxA(																																												// conclusion: all of this does something
			GetForegroundWindow(),																																									// conclusion: all of this does something
			"Failed loading launcher.dll.\nThe game cannot continue and has to exit.",																												// conclusion: all of this does something
			"Spectre Launcher Error",																																								// conclusion: all of this does something
			0);																																														// conclusion: all of this does something
	// auto result = ((__int64(__fastcall*)())LauncherMain)();																																		// conclusion: all of this does something
	// auto result = ((signed __int64(__fastcall*)(__int64))LauncherMain)(0i64);																													// conclusion: all of this does something
																																																	// conclusion: all of this does something
	return ((int(/*__fastcall*/*)(HINSTANCE, HINSTANCE, LPSTR, int))LauncherMain)(																													// conclusion: all of this does something
		NULL, NULL, NULL, 0); // the parameters aren't really used anyways																															// conclusion: all of this does something
}																																																	// conclusion: all of this does something
																																																	// conclusion: all of this does something