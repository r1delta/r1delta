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


#include <MinHook.h>
#include "load.h"
#include "core.h"
#include "patcher.h"
#include "filesystem.h"
#include "logging.h"
#include <tier0/platform.h>
#include "thirdparty/mimalloc-2.1.7/include/mimalloc-new-delete.h"


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

int G_is_dedi;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved)
{
	// make sure we're game and not tools
	char path[MAX_PATH];
	if (GetModuleFileNameA(NULL, path, MAX_PATH)) {
		char* exeName = strrchr(path, '\\') ? strrchr(path, '\\') + 1 : path;
		if (_stricmp(exeName, "r1ds.exe") != 0 && 
			_stricmp(exeName, "titanfall.exe") != 0 && 
			_stricmp(exeName, "r1delta.exe") != 0) { // TODO
			return TRUE; 
		}
	}
	switch (fdwReason)
	{
	case DLL_PROCESS_ATTACH: {
		if (!IsDedicatedServer() && !IsNoConsole())
		{
			AllocConsole();
			SetConsoleTitleW(L"R1Delta");
			freopen("CONOUT$", "wt", stdout);
		}

		VirtualAlloc((void*)0xFFEEFFEE, 1, MEM_RESERVE, PAGE_NOACCESS);

		{
			char path[MAX_PATH];
			if (GetModuleFileNameA(NULL, path, MAX_PATH)) {
				// Extract the executable name from the path
				char* exeName = strrchr(path, '\\') ? strrchr(path, '\\') + 1 : path;

				// Compare the executable name with "r1ds.exe"
				if (_stricmp(exeName, "r1ds.exe") == 0) {
					G_is_dedi = 1;
				}
				else {
					G_is_dedi = 0;
				}
			}
			else {
				// If GetModuleFileNameA fails, assume it's not a dedicated server.
				G_is_dedi = 0;
			}
		}

		LoadLibraryW(L"OnDemandConnRouteHelper"); // stop fucking reloading this thing
		LoadLibraryA("TextShaping.dll"); // fix "Patcher Error" dialogs having no text
		SetDllDirectory(L"r1delta\\bin\\x64_delta");

		MH_Initialize();
		MH_CreateHook((LPVOID)GetProcAddress(GetModuleHandleA("tier0_orig.dll"), "CreateGlobalMemAlloc"), &GetCPUInformationDet, reinterpret_cast<LPVOID*>(&GetCPUInformationOriginal));
		MH_CreateHook((LPVOID)GetProcAddress(GetModuleHandleA("tier0_orig.dll"), "GetCPUInformation"), &GetCPUInformationDet, reinterpret_cast<LPVOID*>(&GetCPUInformationOriginal));
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
			LDR_DLL_LOADED_NOTIFICATION_DATA* ndata = GetModuleNotificationData(L"launcher.dll");
			doBinaryPatchForFile(*ndata);
			FreeModuleNotificationData(ndata);
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
