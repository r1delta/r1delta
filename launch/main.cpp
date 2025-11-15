// Originally based on SpectreLauncher by Barnaby https://github.com/R1Spectre/SpectreLauncher
// Mitigations from Titanfall Black Market Edition mod by p0358 https://github.com/p0358/black_market_edition

#include <windows.h>
#include <tlhelp32.h>
#include <filesystem>
#include <sstream>
#include <fstream>
#include <shlwapi.h>
#include <iostream>
#include <unordered_map>
#include <psapi.h>
#include <winternl.h>
#include <immintrin.h>
#ifndef _MSC_VER && !(__MINGW32__ || __MINGW64__)
// provides __cpuidex on mingw
#include <intrin.h>
#endif
#pragma comment(lib, "Imm32.lib")

//SKIBIDI TOILET
//67

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

DWORD GetProcessByName(const std::wstring &processName)
{
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

	PROCESSENTRY32W processSnapshotEntry = {0};
	processSnapshotEntry.dwSize = sizeof(PROCESSENTRY32);

	if (snapshot == INVALID_HANDLE_VALUE)
		return 0;

	if (!Process32FirstW(snapshot, &processSnapshotEntry))
		return 0;

	while (Process32NextW(snapshot, &processSnapshotEntry))
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

bool GetExePathWide(wchar_t *dest, DWORD destSize)
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

void LibraryLoadError(DWORD dwMessageId, const wchar_t *libName, const wchar_t *location)
{
	char text[8192];
	std::string message = std::system_category().message(dwMessageId);
	printf("%s", message.c_str());
	sprintf_s(
		text,
		"Failed to load the %ls at \"%ls\" (%lu):\n\n%hs\n\nMake sure you followed the R1Delta installation instructions carefully "
		"before reaching out for help.",
		libName,
		location,
		dwMessageId,
		message.c_str());
	printf("%s", text);
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
	/*else if (fs::exists("Titanfall.exe"))
	{
		sprintf_s(
			text,
			"%s\n\nTitanfall.exe has been found in the current directory: is the game installation corrupted or did you not unpack all "
			"R1Delta files here?",
			text);
	}*/

	MessageBoxA(GetForegroundWindow(), text, "R1Delta Launcher Error", 0);
}

void PrependPath()
{
	DWORD pathSize = 0;
	DWORD result = GetEnvironmentVariableW(L"PATH", NULL, 0);
	if (result == 0 && GetLastError() != ERROR_ENVVAR_NOT_FOUND)
	{
		MessageBoxW(
			GetForegroundWindow(),
			L"Warning: could not get current PATH environment variable. Something may break because of that.",
			L"R1Delta Launcher Warning",
			0);
		return;
	}

	std::wstring newPath = std::wstring(exePath) + L"\\r1delta\\bin_delta\\;" +
		std::wstring(exePath) + L"\\r1delta\\bin\\;" +
		std::wstring(exePath) + L"\\bin\\x64_retail\\;.";

	if (result > 0) {
		std::wstring currentPath(result, L'\0');
		GetEnvironmentVariableW(L"PATH", &currentPath[0], result);

		// Remove null terminator if present
		if (!currentPath.empty() && currentPath.back() == L'\0') {
			currentPath.pop_back();
		}

		newPath += L";" + currentPath;
	}

	if (!SetEnvironmentVariableW(L"PATH", newPath.c_str()))
	{
		MessageBoxW(
			GetForegroundWindow(),
			L"Warning: could not set PATH environment variable. Something may break because of that.",
			L"R1Delta Launcher Warning",
			0);
	}
}

bool IsAnyIMEInstalled()
{
	auto count = GetKeyboardLayoutList(0, nullptr);
	if (count == 0)
		return false;
	auto *list = reinterpret_cast<HKL *>(_alloca(count * sizeof(HKL)));
	GetKeyboardLayoutList(count, list);
	for (int i = 0; i < count; i++)
		if (ImmIsIME(list[i]))
			return true;
	return false;
}

bool DoesCpuSupportCetShadowStack()
{
	int cpuInfo[4] = {0, 0, 0, 0};
	__cpuidex(cpuInfo, 7, 0);
	return (cpuInfo[2] & (1 << 7)) != 0; // Check bit 7 in ECX (cpuInfo[2])
}

std::unordered_map<PROCESS_MITIGATION_POLICY, const char *> g_mitigationPolicyNames = {
	{ProcessASLRPolicy, "ProcessASLRPolicy"},
	{ProcessDynamicCodePolicy, "ProcessDynamicCodePolicy"},
	{ProcessExtensionPointDisablePolicy, "ProcessExtensionPointDisablePolicy"},
	{ProcessControlFlowGuardPolicy, "ProcessControlFlowGuardPolicy"},
	{ProcessSignaturePolicy, "ProcessSignaturePolicy"},
	{ProcessImageLoadPolicy, "ProcessImageLoadPolicy"},
	{ProcessUserShadowStackPolicy, "ProcessUserShadowStackPolicy"},
};

void SetMitigationPolicies()
{
	auto kernel32 = GetModuleHandleW(L"kernel32.dll");
	if (!kernel32)
		return;
	auto SetProcessMitigationPolicy = (decltype(&::SetProcessMitigationPolicy))GetProcAddress(kernel32, "SetProcessMitigationPolicy");
	auto GetProcessMitigationPolicy = (decltype(&::GetProcessMitigationPolicy))GetProcAddress(kernel32, "GetProcessMitigationPolicy");
	if (!SetProcessMitigationPolicy || !GetProcessMitigationPolicy)
		return;

	auto SetProcessMitigationPolicyEnsureOK = [SetProcessMitigationPolicy](PROCESS_MITIGATION_POLICY MitigationPolicy, PVOID lpBuffer, SIZE_T dwLength)
	{
		bool result = SetProcessMitigationPolicy(MitigationPolicy, lpBuffer, dwLength);
		if (!result)
		{
			auto lastError = GetLastError();
			if (MitigationPolicy == ProcessUserShadowStackPolicy && !DoesCpuSupportCetShadowStack())
				return;
			MessageBoxA(0, ("Failed mitigation: " + (g_mitigationPolicyNames.find(MitigationPolicy) != g_mitigationPolicyNames.end() ? g_mitigationPolicyNames[MitigationPolicy] : std::to_string(MitigationPolicy)) + ", error: " + std::to_string(lastError) + "\n\nThis is a non-fatal error.").c_str(),
						"SetProcessMitigationPolicy failed", 0);
		}
	};

	PROCESS_MITIGATION_ASLR_POLICY ap;
	GetProcessMitigationPolicy(::GetCurrentProcess(), ProcessASLRPolicy, &ap, sizeof(ap));
	ap.EnableBottomUpRandomization = true;
	ap.EnableForceRelocateImages = true;
	ap.EnableHighEntropy = true;
	ap.DisallowStrippedImages = true; // Images that have not been built with /DYNAMICBASE and do not have relocation information will fail to load if this flag and EnableForceRelocateImages are set.
	SetProcessMitigationPolicyEnsureOK(ProcessASLRPolicy, &ap, sizeof(ap));

	/*PROCESS_MITIGATION_DYNAMIC_CODE_POLICY dcp;
	GetProcessMitigationPolicy(::GetCurrentProcess(), ProcessDynamicCodePolicy, &dcp, sizeof(dcp));
	dcp.ProhibitDynamicCode = true;
	SetProcessMitigationPolicyEnsureOK(ProcessDynamicCodePolicy, &dcp, sizeof(dcp));*/

	if (!IsAnyIMEInstalled()) // this breaks IME apparently (https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-updateprocthreadattribute)
	{
		PROCESS_MITIGATION_EXTENSION_POINT_DISABLE_POLICY epdp;
		GetProcessMitigationPolicy(::GetCurrentProcess(), ProcessExtensionPointDisablePolicy, &epdp, sizeof(epdp));
		epdp.DisableExtensionPoints = true;
		SetProcessMitigationPolicyEnsureOK(ProcessExtensionPointDisablePolicy, &epdp, sizeof(epdp));
	}

	/*PROCESS_MITIGATION_CONTROL_FLOW_GUARD_POLICY cfgp;
	GetProcessMitigationPolicy(::GetCurrentProcess(), ProcessControlFlowGuardPolicy, &cfgp, sizeof(cfgp));
	cfgp.EnableControlFlowGuard = true; // This field cannot be changed via SetProcessMitigationPolicy.
	//cfgp.StrictMode = true; // this needs to be disabled to load stubs with no CRT
	SetProcessMitigationPolicyEnsureOK(ProcessControlFlowGuardPolicy, &cfgp, sizeof(cfgp));*/

	PROCESS_MITIGATION_IMAGE_LOAD_POLICY ilp;
	GetProcessMitigationPolicy(::GetCurrentProcess(), ProcessImageLoadPolicy, &ilp, sizeof(ilp));
	ilp.PreferSystem32Images = true;
	ilp.NoRemoteImages = true;
	SetProcessMitigationPolicyEnsureOK(ProcessImageLoadPolicy, &ilp, sizeof(ilp));

	PROCESS_MITIGATION_USER_SHADOW_STACK_POLICY usspp;
	GetProcessMitigationPolicy(::GetCurrentProcess(), ProcessUserShadowStackPolicy, &usspp, sizeof(usspp));
	if (usspp.EnableUserShadowStack)
	{
		// MessageBoxA(0, "Enabling shadow stack strict mode", "", 0);
		usspp.EnableUserShadowStackStrictMode = true;
		SetProcessMitigationPolicyEnsureOK(ProcessUserShadowStackPolicy, &usspp, sizeof(usspp));
	} // else MessageBoxA(0, "Shadow stack is not enabled!!!", "", 0);
}

bool ScanFileForOggS(const char *filepath, const __m128i &pattern, char *buffer, size_t bufferSize)
{
	HANDLE hFile = CreateFileA(
		filepath,
		GENERIC_READ,
		FILE_SHARE_READ,
		nullptr,
		OPEN_EXISTING,
		FILE_FLAG_SEQUENTIAL_SCAN,
		nullptr);

	if (hFile == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	size_t carryLen = 0;
	bool foundOggS = false;
	static auto containsOggS = [&](const char *buf, size_t size) -> bool
	{
		if (size < 4)
			return false;

		const size_t lastPossible = size - 4;

		for (size_t i = 0; i + 16 <= size; i++)
		{
			__m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i *>(buf + i));
			__m128i eq = _mm_cmpeq_epi32(chunk, pattern);
			int mask = _mm_movemask_epi8(eq);
			if (mask)
			{
				for (int offset = 0; offset < 13; offset++)
				{
					if (i + offset + 4 > size)
						break;
					if (memcmp(buf + i + offset, "OggS", 4) == 0)
					{
						return true;
					}
				}
			}
		}

		size_t start = (size < 16) ? 0 : (size - 16);
		for (size_t i = start; i + 4 <= size; i++)
		{
			if (memcmp(buf + i, "OggS", 4) == 0)
			{
				return true;
			}
		}

		return false;
	};

	while (true)
	{
		DWORD actual = 0;
		if (!ReadFile(hFile, buffer + carryLen, bufferSize, &actual, nullptr) || actual == 0)
		{
			break;
		}

		size_t blockSize = carryLen + actual;
		if (containsOggS(buffer, blockSize))
		{
			foundOggS = true;
			break;
		}

		if (blockSize >= 3)
		{
			memcpy(buffer, buffer + blockSize - 3, 3);
			carryLen = 3;
		}
		else
		{
			memcpy(buffer, buffer + 0, blockSize);
			carryLen = blockSize;
		}
	}

	CloseHandle(hFile);
	return foundOggS;
}

int RunAudioInstallerIfNecessary()
{
	return false;
	if (std::filesystem::exists("vpk/audio_done.txt"))
	{
		return 0;
	}

	std::ifstream camList("vpk/cam_list.txt");
	if (!camList)
	{
		return 1;
	}

	std::string line;
	std::getline(camList, line);
	if (line != "CAMLIST")
	{
		return 1;
	}

	const __m128i pattern = _mm_set1_epi32(0x5367674F);
	constexpr size_t BUFFER_SIZE = 64 * 1024;
	alignas(16) char buffer[BUFFER_SIZE + 16];

	const char *targetVpk = "vpk/client_mp_common.bsp.pak000_040.vpk";
	if (!ScanFileForOggS(targetVpk, pattern, buffer, BUFFER_SIZE))
	{
		std::ofstream doneFile("vpk/audio_done.txt");
		doneFile << "1";
		return 0;
	}

	// Run the installer
	std::string cmd = R"(bin\x64\audio_installer.exe vpk/cam_list.txt)";
	STARTUPINFOA si = {sizeof(STARTUPINFOA)};
	PROCESS_INFORMATION pi = {};

	if (!CreateProcessA(nullptr, const_cast<LPSTR>(cmd.c_str()), nullptr, nullptr,
						FALSE, 0, nullptr, nullptr, &si, &pi))
	{
		return 1;
	}

	WaitForSingleObject(pi.hProcess, INFINITE);
	DWORD exitCode = 0;
	GetExitCodeProcess(pi.hProcess, &exitCode);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	if (exitCode == 0 && !ScanFileForOggS(targetVpk, pattern, buffer, BUFFER_SIZE))
	{
		std::ofstream doneFile("vpk/audio_done.txt");
		doneFile << "1";
	}

	return static_cast<int>(exitCode);
}

std::wstring GetSteamDllPath()
{
	wchar_t pathString[512];
	DWORD pathStringSize = sizeof(pathString);

	if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", L"SteamClientDll64", RRF_RT_REG_SZ, nullptr, pathString, &pathStringSize) == ERROR_SUCCESS)
	{
		return pathString;
	}

	return L"";
}
#define VFUNC_OF(a, off) (*reinterpret_cast<void ***>(a))[off]

class CGameID
{
public:
	CGameID(int appid, int type, int modid) : m_nAppID(appid), m_nType(type), m_nModID(modid) {};
	unsigned int m_nAppID : 24;
	unsigned int m_nType : 8;
	unsigned int m_nModID : 32;
};

bool EnsureStartedFromLauncher()
{
	//auto delta_env = getenv("FROM_DELTA_LAUNCHER");
	wchar_t delta_env[2];
	auto delta_env_size = GetEnvironmentVariableW(L"FROM_DELTA_LAUNCHER", delta_env, 1);
	if (!delta_env_size)
	{
		return false;
	}
	return true;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, int nCmdShow)
{
	 //AllocConsole();
	 //SetConsoleTitle(TEXT("R1Delta Launcher"));
	 //FILE *pCout, *pCerr;
	 //freopen_s(&pCout, "conout$", "w", stdout);
	 //freopen_s(&pCerr, "conout$", "w", stderr);
	 // Get the current working directory
	 wchar_t currentDir[MAX_PATH];
	 GetCurrentDirectoryW(MAX_PATH, currentDir);
	 
	 DWORD result = GetEnvironmentVariableW(L"PATH", NULL, 0);
	 if (result == 0 && GetLastError() != ERROR_ENVVAR_NOT_FOUND)
	 {
		 MessageBoxW(
			 GetForegroundWindow(),
			 L"Warning: could not get current PATH environment variable. Something may break because of that.",
			 L"R1Delta Launcher Warning",
			 0);
		 return 1;
	 }
	 std::wstring currentPath(result, L'\0');
	 GetEnvironmentVariableW(L"PATH", &currentPath[0], result);

	 // Get the current exe path
	 if (!GetExePathWide(exePath, sizeof(exePath) / sizeof(wchar_t)))
	 {
		 MessageBoxW(
			 GetForegroundWindow(),
			 L"Warning: could not get current exe path. Something may break because of that.",
			 L"R1Delta Launcher Warning",
			 0);
		 return 1;
	 }

	if (!EnsureStartedFromLauncher())
	{
		PROCESS_INFORMATION pi;
		memset(&pi, 0, sizeof(pi));
		STARTUPINFOA si;
		memset(&si, 0, sizeof(si));
		si.cb = sizeof(STARTUPINFOA);
		si.dwFlags = STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_SHOWDEFAULT;
		if (!CreateProcessA(
			"r1delta.exe",
			nullptr,
			NULL,
			NULL,
			false,
			CREATE_DEFAULT_ERROR_MODE | CREATE_NEW_PROCESS_GROUP,
			NULL,
			NULL,
			(LPSTARTUPINFOA)&si,
			&pi))
		{
			MessageBoxA(0, "Failed to start the launcher!", "R1Delta Launcher Error", MB_OK);
			return 1;
		}
		return 1;
	}

	if (RunAudioInstallerIfNecessary())
	{
		MessageBoxA(
			GetForegroundWindow(),
			"Failed setting up game audio.\nThe game cannot continue and has to exit.",
			"R1Delta Launcher Error",
			0);
		return 1;
	}
	{
		swprintf_s(buffer, L"%s\\r1delta\\bin\\\launcher.dll", exePath);
		hLauncherModule = LoadLibraryExW(buffer, 0, LOAD_WITH_ALTERED_SEARCH_PATH);
		if (!hLauncherModule)
		{
			LibraryLoadError(GetLastError(), L"launcher.dll", buffer);
			return 1;
		}
		swprintf_s(buffer, L"%s\\r1delta\\bin_delta\\tier0.dll", exePath);
		auto hCoreModule = LoadLibraryExW(buffer, 0, LOAD_WITH_ALTERED_SEARCH_PATH);
		if (!hCoreModule)
		{
			LibraryLoadError(GetLastError(), L"tier0.dll", buffer);
			return 1;
		}
		using SetR1DeltaLaunchArgs_t = void(__stdcall*)(const char*);
		auto SetArgs = reinterpret_cast<SetR1DeltaLaunchArgs_t>(GetProcAddress(hCoreModule, "SetR1DeltaLaunchArgs"));

		if (!SetArgs)
		{
			MessageBoxA(
				GetForegroundWindow(),
				"Failed to load SetR1DeltaLaunchArgs function.\nThe game cannot continue and has to exit.",
				"R1Delta Launcher Error",
				0);
			return 1;
		}

		SetArgs(lpCmdLine);

		SetMitigationPolicies();
	}

	printf("[*] Launching the game...\n");

	auto LauncherMain = GetLauncherMain();
	if (!LauncherMain)
	{
		MessageBoxA(
			GetForegroundWindow(),
			"Failed loading launcher.dll.\nThe game cannot continue and has to exit.",
			"R1Delta Launcher Error",
			0);
		return 1;
	}


	return ((int(/*__fastcall*/ *)(HINSTANCE, HINSTANCE, LPSTR, int))LauncherMain)(
		0, 0, nullptr, 1); // the parameters aren't really used anyways
}
