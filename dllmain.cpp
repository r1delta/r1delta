// Happiness has to be fought for.

// %*++***###*##**##++**+++*++*%%%%%%%+*%+#*+%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%#=%%%#**#+#%
// ==----------------------------------------------------------------------=================+
// =------------------------------------::----------------------------------===---==========+
// ---------------------------------:-:--::::-::::-------------------=======================+
// =-------------------------------::::::::-::::-:::----------==============+===+++=========+
// ----------------------------::::::--:---=====----------===========++==++++++++++++++++++++
// ----------------------------:-----:---==++++++====-==========++++++++++++++++++++++++++++*
// -------------------------------------=+++++++=============++++++++++++++++++++++++++++++**
// -------------------------------------=++++*+========++++++++++++++++++++++++++++++++++++**
// ----------------------------:::::::--=+++++=======+++++++++++++++++++++++************++++*
// ---------------------::::::::::::::::-==+++===++++++++++++++++++++++++********###%%%##*++*
// -------:::::::::::::::::::::::::::::::-=====+####**+++++++++++++++++*********#%%%@@@@%%#**
// ------:-:::::::::::::::::::::::::::::::-====*%%%%#*++++++++++++++++++********##%@@@@@%%#**
// ----------::::::::::::::::::::::::::-=--====+#%%%*++++++++++++++++++++*********##%%%%%#***
// -------------=*=-:::::::::::::::::-=++======++***+++++++++++++++++++**************###*****
// -------------=*#=-------======++++*###*+=+=++=++++++++++*+++******************************
// =-----=======+*#*+++++++*****##########+=++++++++++***************************************
// +++++++++++****#################*****#*+=+++++++++****************************************
// ++**+++++++++++++======+++++++++++++****+=+++***################**************************
// *****+=--------::-::::::::::::::::::------=*#%%%%%%%%%%%%%%%%%%%#####*********************
// ******=-----------:::::::::::---:::::::::-=#%%%%@@@@@@@@@@@@@@%%%%###********************#
// ******=---------------:::::::::::-:::::::-*%%%@@@@@@@@@@@@@@@@@%%%%##********************#
// ****#*=-----------------:::::::::::::::::-=*%%@@@@@@@@@@@@@@@@@@%%##*********************#
// ******+===-------------::::::::::::---:::--=*#%%%@@@@@@@@@@@@@%%######**#**************###
// ==++==------------------:::::::::::::-------=+**##%%%@%%%%%%%%##########*****************#
// ==--------------------------::-:::::::::::---=++**##%%%%%%%%%%%##########*************####
// =--------------------------------:---::::--:--==+**###%#%%%%%%%%%%%#####**************####
// ====--------------------------:-------::-------==+++****###########******************#####
// ===--==------------------------------------::---==+++++******************************#####
// ===-------------------------------------:::-:----=+++********************************####%
// =====---------------------------------------------=++++******************************####%
// ======------------------==------------------------==+++***************************######%%
// =========-----===--------==------------------------==++********#*#####**#######*########%%

#include "core.h"

#include <MinHook.h>
#include "load.h"
#include "core.h"
#include "patcher.h"
#include "filesystem.h"
#include "logging.h"
#include <tier0/platform.h>
#include "mimalloc-new-delete.h"
#include "crashhandler.h"
uint64_t g_PerformanceFrequency;
int G_is_dedi;
typedef const char* (__cdecl* wine_get_version_func)();
CPUInformation* (__fastcall* GetCPUInformationOriginal)();

const CPUInformation* GetCPUInformationDet()
{
	CPUInformation* result = GetCPUInformationOriginal();

	// NOTE(mrsteyk): blame wanderer for SINGLEPLAYER SUPPORT WOWOWOWOWOW SO USEFUL
	// NOTE(mrsteyk): <=20 global thread pool "IOJob" for filesystem_stdio.dll
	//                 5 WT simple threads in vstdlib.dll WT_Init (25)
	//                 1 thread in filesystem_stdio.dll (26) ???
	//                 1 "RenderThread" simple thread in materialsystem_dx11.dll (27)
	//                 3 thread pool "GlobPool" in engine.dll due to g_pThreadPool->[4]() (30)
	//                 1 thread pool "SaveJob"? in engine.dll due to SaveRestore_Init (31)
	//                 1 simple thread in engine.dll due to QueuedPacketThread (32) (it disappeared?)
	//                 CRASH

	// NOTE(mrsteyk): Some threads want logical core count, wanderer want's singleplayer
	if (result->m_nLogicalProcessors >= 20) {
		result->m_nLogicalProcessors = 19;
	}

	return result;
}

static void
nvapi_stuff()
{
	auto nv = LoadLibraryExW(L"nvapi64.dll", 0, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (!nv)
	{
		return;
	}

	using nvapi_queryiface_t = void*(__fastcall*)(uintptr_t);
	auto nvapi_queryiface = (nvapi_queryiface_t)GetProcAddress(nv, "nvapi_QueryInterface");

	using nvapi_start_call_profile_t = int32_t(__fastcall*)(uintptr_t, uintptr_t*);
	using nvapi_end_call_profile_t = int32_t(__fastcall*)(uintptr_t, uintptr_t, uint64_t);
	auto nvapi_start_call = (nvapi_start_call_profile_t)nvapi_queryiface(0x33C7358C);
	auto nvapi_end_call = (nvapi_end_call_profile_t)nvapi_queryiface(0x593E8644);

	if (!nvapi_start_call || !nvapi_end_call)
	{
		nvapi_start_call = nullptr;
		nvapi_end_call = nullptr;
	}

	int32_t nvret = 0;
#define NVAPI_CALL(id, X) { uintptr_t hprofile; if (nvapi_start_call) nvapi_start_call(id, &hprofile); nvret = X; if (nvapi_end_call) nvapi_end_call(id, hprofile, nvret); }

	constexpr uintptr_t nvapi_get_session_id = 0x694D52E;
	using nvapi_get_session_t = int32_t(__fastcall*)(uintptr_t*);
	auto nvapi_get_session = (nvapi_get_session_t)nvapi_queryiface(nvapi_get_session_id);

	constexpr uintptr_t nvapi_end_session_id = 0xDAD9CFF8;
	using nvapi_end_session_t = int32_t(__fastcall*)(uintptr_t);
	auto nvapi_end_session = (nvapi_end_session_t)nvapi_queryiface(nvapi_end_session_id);

	constexpr uintptr_t nvapi_load_settings_id = 0x375DBD6B;
	using nvapi_load_settings_t = int32_t(__fastcall*)(uintptr_t);
	auto nvapi_load_settings = (nvapi_load_settings_t)nvapi_queryiface(nvapi_load_settings_id);

	constexpr uintptr_t nvapi_profile_by_name_id = 0x7E4A9A0B;
	using nvapi_profile_by_name_t = int32_t(__fastcall*)(uintptr_t, const wchar_t*, uintptr_t*);
	auto nvapi_profile_by_name = (nvapi_profile_by_name_t)nvapi_queryiface(nvapi_profile_by_name_id);

	constexpr uintptr_t nvapi_create_app_id = 0x4347A9DE;
	using nvapi_create_app_t = int32_t(__fastcall*)(uintptr_t, uintptr_t, void*);
	auto nvapi_create_app = (nvapi_create_app_t)nvapi_queryiface(nvapi_create_app_id);

	if (!nvapi_get_session || !nvapi_end_session || !nvapi_load_settings || !nvapi_profile_by_name_id || !nvapi_create_app)
	{
		return;
	}

	uintptr_t handle = 0;
	NVAPI_CALL(nvapi_get_session_id, nvapi_get_session(&handle));
	if (nvret)
	{
		printf("NVAPI: nvapi_get_session fail 0x%X (%d)", nvret, nvret);
		return;
	}

	NVAPI_CALL(nvapi_load_settings_id, nvapi_load_settings(handle));

	uintptr_t profile = 0;
	NVAPI_CALL(nvapi_profile_by_name_id, nvapi_profile_by_name(handle, L"TitanFall", &profile));
	if (nvret)
	{
		printf("NVAPI: nvapi_profile_by_name fail 0x%X (%d)", nvret, nvret);
		return;
	}
	else {
		struct app_info_t
		{
			uint32_t struct_ver;
			uint32_t blergh;
			wchar_t app_name[2048];
			wchar_t app_name2[2048];

			wchar_t string1[2048];
			wchar_t string2[2048];

			uint32_t flags;

			wchar_t string3[2048];
		};
		static_assert(sizeof(app_info_t) == 20492);

		app_info_t ai;
		memset(&ai, 0, sizeof(ai));
		ai.struct_ver = 0x4500C;

		memcpy(ai.app_name, L"r1delta.exe", sizeof(L"r1delta.exe"));
		memcpy(ai.app_name2, L"r1delta.exe", sizeof(L"r1delta.exe"));
		
		NVAPI_CALL(nvapi_create_app_id, nvapi_create_app(handle, profile, &ai));
		if (nvret)
		{
			// NOTE(mrsteyk): this can fail if we are already added to "TitanFall" profile.
			//printf("NVAPI: nvapi_create_app fail 0x%X (%d)", nvret, nvret);
			return;
		} else {
			NVAPI_CALL(nvapi_load_settings_id, nvapi_load_settings(handle));
			if (nvret)
			{
				printf("NVAPI: nvapi_load_settings fail 0x%X (%d)", nvret, nvret);
				return;
			}
			else
			{
				// ergh, idk?
			}
		}
	}

	NVAPI_CALL(nvapi_end_session_id, nvapi_end_session(handle));
}

#if BUILD_PROFILE
#include "tracy-0.11.1/public/TracyClient.cpp"
#endif

static int skip_dllmain = -1;
bool Plat_IsInToolMode() {
		static HMODULE matsystem = 0;
		if (!matsystem) {
			matsystem = GetModuleHandleA("materialsystem_dx11.dll");
			return false;
		}
		HMODULE module2;
		BOOL check1 = GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCTSTR)_ReturnAddress(), &module2);
		BOOL check2 = module2 == (HMODULE)matsystem;
		return check1 && check2;
}
void (*CCommandLine__CreateCmdLineOriginal)(void* thisptr, char* commandline);
#include <Windows.h>
#include <string>
#include <vector>
#include <filesystem> // Requires C++17
#include <cstdlib> // For std::getenv
#include <stdexcept> // For exception handling (optional but good)
#include <iostream> // For debug output (alternative to OutputDebugStringA)

// Assuming this is defined elsewhere
extern void (*CCommandLine__CreateCmdLineOriginal)(void* thisptr, char* commandline);

void CCommandLine__CreateCmdLine(void* thisptr, char* commandline) {
	std::string finalCmdLineStr;

	// 1. Start with the original command line content, if any.
	if (commandline != nullptr && commandline[0] != '\0') {
		finalCmdLineStr = commandline;
	}

	// 2. Try to determine the path and construct the -game argument.
	std::string gameArg; // Holds the "-game ..." part
	try {
		std::string exePathStr;
		DWORD pathLen = 0;
		std::vector<char> buffer(MAX_PATH);

		do {
			pathLen = GetModuleFileNameA(NULL, buffer.data(), static_cast<DWORD>(buffer.size()));
			if (pathLen == 0) {
				OutputDebugStringA("Warning: GetModuleFileNameA failed in CCommandLine__CreateCmdLine. Cannot add -game argument.\n");
				exePathStr.clear();
				break;
			}
			if (pathLen < buffer.size()) {
				exePathStr.assign(buffer.data(), pathLen);
				break;
			}
			if (buffer.size() > MAX_PATH * 16) { // Add a safety limit
				OutputDebugStringA("Warning: GetModuleFileNameA buffer size exceeded limit. Cannot add -game argument.\n");
				exePathStr.clear();
				break;
			}
			buffer.resize(buffer.size() * 2);
		} while (true);

		if (!exePathStr.empty()) {
			std::filesystem::path exePath = exePathStr;
			std::filesystem::path exeDir = exePath.parent_path();
			std::filesystem::path r1deltaPath = exeDir / "r1delta";
			// Ensure the path string is properly quoted for the command line
			gameArg = "-game \"" + r1deltaPath.string() + "\"";
		}
	}
	catch (const std::filesystem::filesystem_error& e) {
		OutputDebugStringA("Warning: Filesystem error constructing r1delta path: ");
		OutputDebugStringA(e.what());
		OutputDebugStringA(". Cannot add -game argument.\n");
		gameArg.clear(); // Ensure it's empty on error
	}
	catch (const std::bad_alloc& e) {
		OutputDebugStringA("Warning: Memory allocation error during path finding: ");
		OutputDebugStringA(e.what());
		OutputDebugStringA(". Cannot add -game argument.\n");
		gameArg.clear();
	}
	catch (...) { // Catch any other unexpected exceptions
		OutputDebugStringA("Warning: Unknown error during path finding. Cannot add -game argument.\n");
		gameArg.clear();
	}


	// 3. Append the "-game" argument if constructed successfully.
	// Check if "-game" is already present to avoid duplicates (optional but good practice)
	// A simple check: does finalCmdLineStr contain "-game "?
	bool gameArgPresent = (finalCmdLineStr.find("-game ") != std::string::npos);

	if (!gameArg.empty() && !gameArgPresent) {
		if (!finalCmdLineStr.empty()) {
			finalCmdLineStr += " "; // Add separator
		}
		finalCmdLineStr += gameArg;
	}


	// 4. Append arguments from the environment variable.
	const char* envArgs = std::getenv("R1DELTA_LAUNCH_ARGS");
	if (envArgs != nullptr && envArgs[0] != '\0') {
		if (!finalCmdLineStr.empty()) {
			finalCmdLineStr += " "; // Add separator
		}
		finalCmdLineStr += envArgs;
	}

	// 5. Prepare the final buffer for the original function.
	// The original function expects a 'char*' and might modify it.
	// Copy to a mutable buffer.
	std::vector<char> finalCmdLineBuffer(finalCmdLineStr.begin(), finalCmdLineStr.end());
	finalCmdLineBuffer.push_back('\0'); // Ensure null-termination

	// 6. Call the original function.
	CCommandLine__CreateCmdLineOriginal(thisptr, finalCmdLineBuffer.data());
}
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved)
{
	// make sure we're game and not tools
	if (skip_dllmain == 1)
	{
		return TRUE;
	}
	else if (skip_dllmain == -1)
	{
		char path[MAX_PATH];
		if (GetModuleFileNameA(NULL, path, MAX_PATH)) {
			char* exeName = strrchr(path, '\\') ? strrchr(path, '\\') + 1 : path;
			if (_stricmp(exeName, "r1delta_ds.exe") != 0 &&
				_stricmp(exeName, "titanfall.exe") != 0 &&
				_stricmp(exeName, "r1delta.exe") != 0) { // TODO
				skip_dllmain = 1;
				return TRUE;
			}
		}
	}
	switch (fdwReason)
	{
	case DLL_PROCESS_ATTACH: {
		skip_dllmain = 0;

		if (!IsDedicatedServer() && !IsNoConsole())
		{
			AllocConsole();
			SetConsoleTitleW(L"R1Delta");
			freopen("CONOUT$", "wt", stdout);
		}

#if BUILD_PROFILE
		TracyPlotConfig("r1dc_decompress_memory_in", tracy::PlotFormatType::Memory, false, true, 0);
		TracyPlotConfig("r1dc_decompress_memory_out", tracy::PlotFormatType::Memory, false, true, 0);
#endif

		LARGE_INTEGER freq;
		QueryPerformanceFrequency(&freq);
		g_PerformanceFrequency = freq.QuadPart;

		VirtualAlloc((void*)0xFFEEFFEE, 1, MEM_RESERVE, PAGE_NOACCESS);

		{
			char path[MAX_PATH];
			if (GetModuleFileNameA(NULL, path, MAX_PATH)) {
				// Extract the executable name from the path
				char* exeName = strrchr(path, '\\') ? strrchr(path, '\\') + 1 : path;

				// Compare the executable name with "r1delta_ds.exe"
				if (_stricmp(exeName, "r1delta_ds.exe") == 0) {
					G_is_dedi = 1;
				}
				else {
					G_is_dedi = 0;

					if (_stricmp(exeName, "r1delta.exe") == 0)
					{
						nvapi_stuff();
					}
				}
			}
			else {
				// If GetModuleFileNameA fails, assume it's not a dedicated server.
				G_is_dedi = 0;
			}
		}


		SetDllDirectoryW(L"r1delta\\bin_delta");
		SetDllDirectoryW(L"r1delta\\bin");
		InstallExceptionHandler();
		MH_Initialize();
		MH_CreateHook((LPVOID)GetProcAddress(GetModuleHandleA("tier0_orig.dll"), "GetCPUInformation"), &GetCPUInformationDet, reinterpret_cast<LPVOID*>(&GetCPUInformationOriginal));
		MH_CreateHook((LPVOID)(((uintptr_t)GetModuleHandleA("tier0_orig.dll"))+0xbf60), &CCommandLine__CreateCmdLine, reinterpret_cast<LPVOID*>(&CCommandLine__CreateCmdLineOriginal));
//		if (!IsDedicatedServer())
//			MH_CreateHook((LPVOID)GetProcAddress(GetModuleHandleA("tier0_orig.dll"), "Plat_IsInToolMode"), &GetCPUInformationDet, reinterpret_cast<LPVOID*>(NULL));

		MH_EnableHook(MH_ALL_HOOKS);

		initialisePatchInstructions();


		if(!IsNoConsole() && !IsDedicatedServer())
			InitLoggingHooks();
		StartFileCacheThread();

		// Chromium
		LdrRegisterDllNotificationFunc reg_fn =
			reinterpret_cast<LdrRegisterDllNotificationFunc>(::GetProcAddress(
				::GetModuleHandleW(L"ntdll.dll"), "LdrRegisterDllNotification"));
		reg_fn(0, &LoaderNotificationCallback, 0, &dll_notification_cookie_);

		if (!G_is_dedi) {
			G_launcher = (uintptr_t)GetModuleHandleW(L"launcher.dll");
			G_vscript = G_launcher;

			HMODULE hNTDLL = GetModuleHandleA("ntdll.dll");
			auto wine_get_version = (wine_get_version_func)GetProcAddress(hNTDLL, "wine_get_version");
			if (!wine_get_version)
			{
				LDR_DLL_LOADED_NOTIFICATION_DATA* ndata = GetModuleNotificationData(L"launcher.dll");
				doBinaryPatchForFile(*ndata);
				FreeModuleNotificationData(ndata);
			}
		}
		else {
			G_launcher = (uintptr_t)GetModuleHandleW(L"dedicated.dll");
			G_vscript = G_launcher;
			LDR_DLL_LOADED_NOTIFICATION_DATA* ndata = GetModuleNotificationData(L"dedicated.dll");
			doBinaryPatchForFile(*ndata);
			FreeModuleNotificationData(ndata);
		}

		break; 
	}
	case DLL_THREAD_ATTACH:
		break;
	case DLL_THREAD_DETACH:
		break;
	case DLL_PROCESS_DETACH:
		MH_DisableHook(MH_ALL_HOOKS);
		MH_Uninitialize();
		break;
	}
	return TRUE;
}
