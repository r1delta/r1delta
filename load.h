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

#pragma once
#include "windows.h"
#include <winternl.h>  // For UNICODE_STRING.
#include "core.h"
#include "physics.h"
#include "vsdk/public/tier1/utlmap.h"

extern IBaseClientDLL* g_ClientDLL;

enum {
	// The DLL was loaded. The NotificationData parameter points to a
	// LDR_DLL_LOADED_NOTIFICATION_DATA structure.
	LDR_DLL_NOTIFICATION_REASON_LOADED = 1,
	// The DLL was unloaded. The NotificationData parameter points to a
	// LDR_DLL_UNLOADED_NOTIFICATION_DATA structure.
	LDR_DLL_NOTIFICATION_REASON_UNLOADED = 2,
};
// Structure that is used for module load notifications.
struct LDR_DLL_LOADED_NOTIFICATION_DATA {
	// Reserved.
	ULONG Flags;
	// The full path name of the DLL module.
	PCUNICODE_STRING FullDllName;
	// The base file name of the DLL module.
	PCUNICODE_STRING BaseDllName;
	// A pointer to the base address for the DLL in memory.
	PVOID DllBase;
	// The size of the DLL image, in bytes.
	ULONG SizeOfImage;
};
using PLDR_DLL_LOADED_NOTIFICATION_DATA = LDR_DLL_LOADED_NOTIFICATION_DATA*;
// Structure that is used for module unload notifications.
struct LDR_DLL_UNLOADED_NOTIFICATION_DATA {
	// Reserved.
	ULONG Flags;
	// The full path name of the DLL module.
	PCUNICODE_STRING FullDllName;
	// The base file name of the DLL module.
	PCUNICODE_STRING BaseDllName;
	// A pointer to the base address for the DLL in memory.
	PVOID DllBase;
	// The size of the DLL image, in bytes.
	ULONG SizeOfImage;
};
using PLDR_DLL_UNLOADED_NOTIFICATION_DATA = LDR_DLL_UNLOADED_NOTIFICATION_DATA*;
// Union that is used for notifications.
union LDR_DLL_NOTIFICATION_DATA {
	LDR_DLL_LOADED_NOTIFICATION_DATA Loaded;
	LDR_DLL_UNLOADED_NOTIFICATION_DATA Unloaded;
};
using PLDR_DLL_NOTIFICATION_DATA = LDR_DLL_NOTIFICATION_DATA*;
// Signature of the notification callback function.
using PLDR_DLL_NOTIFICATION_FUNCTION =
VOID(CALLBACK*)(ULONG notification_reason,
	const LDR_DLL_NOTIFICATION_DATA* notification_data,
	PVOID context);
// Signatures of the functions used for registering DLL notification callbacks.
using LdrRegisterDllNotificationFunc =
NTSTATUS(NTAPI*)(ULONG flags,
	PLDR_DLL_NOTIFICATION_FUNCTION notification_function,
	PVOID context,
	PVOID* cookie);
using LdrUnregisterDllNotificationFunc = NTSTATUS(NTAPI*)(PVOID cookie);
extern void* dll_notification_cookie_;
void __stdcall LoaderNotificationCallback(
	unsigned long notification_reason,
	const LDR_DLL_NOTIFICATION_DATA* notification_data,
	void* context);

extern int* g_nServerThread;
extern uintptr_t G_launcher;
extern uintptr_t G_vscript;
extern uintptr_t G_filesystem_stdio;
extern uintptr_t G_server;
extern uintptr_t G_engine;
extern uintptr_t G_engine_ds;
extern uintptr_t G_client;

inline bool IsInServerThread() {
	return GetCurrentThreadId() == *g_nServerThread;
}

LDR_DLL_LOADED_NOTIFICATION_DATA* GetModuleNotificationData(const wchar_t* moduleName);
void FreeModuleNotificationData(LDR_DLL_LOADED_NOTIFICATION_DATA*);
