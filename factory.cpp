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
#include <cstdlib>
#include <crtdbg.h>
#include <new>
#include "windows.h"

#include <iostream>
#include "cvar.h"
#include <winternl.h>  // For UNICODE_STRING.
#include <fstream>
#include <filesystem>
#include <intrin.h>
#include "memory.h"
#include "filesystem.h"
#include "defs.h"
#include "factory.h"
#include "core.h"
#include "TableDestroyer.h"
#include <DbgHelp.h>
#include <string.h>
#include <set>
#include <map>
#include "engine.h"
#include "model_info.h"
//#include "thirdparty/silver-bun/silver-bun.h"
#include "load.h"

#pragma comment(lib, "Dbghelp.lib")

CServerGameDLL__DLLInitType CServerGameDLL__DLLInitOriginal;
CreateInterfaceFn oAppSystemFactory;
CreateInterfaceFn oFileSystemFactory;
CreateInterfaceFn oPhysicsFactory;

void CNetworkStringTableContainer__SetTickCount(__int64 a1, char a2) {
	*(char*)(a1 + 8) = a2;
}

void CCVar__SetSomeFlag_Corrupt(int64_t a1, int64_t a2) {
	return;
}

int64_t CCVar__GetSomeFlag(int64_t thisptr) {
	return 0;
}

uintptr_t modelinterface;
uintptr_t stringtableinterface;
uintptr_t fsintfakeptr = 0;

HMODULE engineR1O;
CreateInterfaceFn R1OCreateInterface;

void* R1OFactory(const char* pName, int* pReturnCode) {
	//	std::cout << "looking for " << pName << std::endl;

	if (!strcmp_static(pName, "VEngineServer022")) {
		//std::cout << "wrapping VEngineServer022" << std::endl;

		uintptr_t* r1vtable = *(uintptr_t**)oAppSystemFactory(pName, pReturnCode);
		g_CVEngineServerInterface = (uintptr_t)oFileSystemFactory(pName, pReturnCode);
		if (!g_CVEngineServer) {
			g_CVEngineServer = new CVEngineServer(r1vtable);
		}

		static void* whatever2 = &g_r1oCVEngineServerInterface; // double ref return
		return &whatever2;
	}
	if (!strcmp_static(pName, "VFileSystem017")) {
		//	std::cout << "wrapping VFileSystem017" << std::endl;

		uintptr_t* r1vtable = *(uintptr_t**)oFileSystemFactory(pName, pReturnCode);
		g_CVFileSystemInterface = (uintptr_t)oFileSystemFactory(pName, pReturnCode);

		g_CBaseFileSystemInterface = (IFileSystem*)(((uintptr_t)oFileSystemFactory(pName, pReturnCode)) + 8);
		g_CVFileSystem = new CVFileSystem((*(uintptr_t**)(g_CVFileSystemInterface)));

		g_CBaseFileSystem = new CBaseFileSystem((*(uintptr_t**)(g_CVFileSystemInterface + 8)));

		struct fsptr {
			void* ptr1;
			void* ptr2;
			void* ptr3;
			void* ptr4;
			void* ptr5;
		};
		void* whatever3 = &g_r1oCVFileSystemInterface;
		void* whatever4 = &g_r1oCBaseFileSystemInterface;
		static fsptr a; // ref return;

		a.ptr1 = whatever3;
		a.ptr2 = whatever4;
		a.ptr3 = whatever4;
		a.ptr4 = whatever4;
		a.ptr5 = whatever4;
		fsintfakeptr = (uintptr_t)(&a.ptr1);
		return &a.ptr1;
	}
	if (!strcmp_static(pName, "VModelInfoServer002")) {
		//std::cout << "wrapping VModelInfoServer002" << std::endl;
		g_CVModelInfoServerInterface = (uintptr_t)oAppSystemFactory(pName, pReturnCode);
		uintptr_t* r1vtable = *(uintptr_t**)oAppSystemFactory(pName, pReturnCode);

		g_CVModelInfoServer = new CVModelInfoServer(r1vtable);

		static void* whatever4 = &g_r1oCVModelInfoServerInterface; // double ref return
		return &whatever4;
	}
	if (!strcmp_static(pName, "VEngineServerStringTable001")) {
		//std::cout << "wrapping VEngineServerStringTable001" << std::endl;
		stringtableinterface = (uintptr_t)oAppSystemFactory(pName, pReturnCode);
		uintptr_t* r1vtable = *(uintptr_t**)stringtableinterface;

		uintptr_t oCNetworkStringTableContainer_dtor = r1vtable[0];
		uintptr_t oCNetworkStringTableContainer__CreateStringTable = r1vtable[1];
		uintptr_t oCNetworkStringTableContainer__RemoveAllTables = r1vtable[2];
		uintptr_t oCNetworkStringTableContainer__FindTable = r1vtable[3];
		uintptr_t oCNetworkStringTableContainer__GetTable = r1vtable[4];
		uintptr_t oCNetworkStringTableContainer__GetNumTables = r1vtable[5];
		uintptr_t oCNetworkStringTableContainer__SetAllowClientSideAddString = r1vtable[6];
		uintptr_t oCNetworkStringTableContainer__CreateDictionary = r1vtable[7];
		static uintptr_t r1ovtable[] = {
			CreateFunction((void*)oCNetworkStringTableContainer_dtor, (void*)stringtableinterface),
			CreateFunction((void*)oCNetworkStringTableContainer__CreateStringTable, (void*)stringtableinterface),
			CreateFunction((void*)oCNetworkStringTableContainer__RemoveAllTables, (void*)stringtableinterface),
			CreateFunction((void*)oCNetworkStringTableContainer__FindTable, (void*)stringtableinterface),
			CreateFunction((void*)oCNetworkStringTableContainer__GetTable, (void*)stringtableinterface),
			CreateFunction((void*)oCNetworkStringTableContainer__GetNumTables, (void*)stringtableinterface),
			CreateFunction((void*)oCNetworkStringTableContainer__SetAllowClientSideAddString, (void*)stringtableinterface),
			CreateFunction(CNetworkStringTableContainer__SetTickCount, (void*)stringtableinterface),
			CreateFunction((void*)oCNetworkStringTableContainer__CreateDictionary, (void*)stringtableinterface)
		};
		static void* whatever5 = &r1ovtable; // double ref return
		return &whatever5;
	}
	if (!strcmp_static(pName, "VEngineCvar007")) {
		//std::cout << "wrapping VEngineCvar007" << std::endl;
		cvarinterface = (uintptr_t)oAppSystemFactory(pName, pReturnCode);
		uintptr_t* r1vtable = *(uintptr_t**)cvarinterface;

		uintptr_t oCCvar__Connect = r1vtable[0];
		uintptr_t oCCvar__Disconnect = r1vtable[1];
		uintptr_t oCCvar__QueryInterface = r1vtable[2];
		uintptr_t oCCVar__Init = r1vtable[3];
		uintptr_t oCCVar__Shutdown = r1vtable[4];
		uintptr_t oCCvar__GetDependencies = r1vtable[5];
		uintptr_t oCCVar__GetTier = r1vtable[6];
		uintptr_t oCCVar__Reconnect = r1vtable[7];
		uintptr_t oCCvar__AllocateDLLIdentifier = r1vtable[8];
		OriginalCCVar_RegisterConCommand = reinterpret_cast<decltype(OriginalCCVar_RegisterConCommand)>(r1vtable[9]);
		OriginalCCVar_UnregisterConCommand = reinterpret_cast<decltype(OriginalCCVar_UnregisterConCommand)>(r1vtable[10]);
		uintptr_t oCCvar__UnregisterConCommands = r1vtable[11];
		uintptr_t oCCvar__GetCommandLineValue = r1vtable[12];
		//uintptr_t CCvar__FindCommandBase = r1vtable[13];
		//uintptr_t CCvar__FindCommandBase2 = r1vtable[14];
		//uintptr_t CCvar__FindVar = r1vtable[15];
		//uintptr_t CCvar__FindVar2 = r1vtable[16];
		//uintptr_t CCvar__FindCommand = r1vtable[17];
		//uintptr_t CCvar__FindCommand2 = r1vtable[18];
		OriginalCCVar_FindCommandBase = reinterpret_cast<decltype(OriginalCCVar_FindCommandBase)>(r1vtable[13]);
		OriginalCCVar_FindCommandBase2 = reinterpret_cast<decltype(OriginalCCVar_FindCommandBase2)>(r1vtable[14]);
		OriginalCCVar_FindVar = reinterpret_cast<decltype(OriginalCCVar_FindVar)>(r1vtable[15]);
		OriginalCCVar_FindVar2 = reinterpret_cast<decltype(OriginalCCVar_FindVar2)>(r1vtable[16]);
		OriginalCCVar_FindCommand = reinterpret_cast<decltype(OriginalCCVar_FindCommand)>(r1vtable[17]);
		OriginalCCVar_FindCommand2 = reinterpret_cast<decltype(OriginalCCVar_FindCommand2)>(r1vtable[18]);

		uintptr_t oCCVar__Find = r1vtable[19];
		OriginalCCvar__InstallGlobalChangeCallback = reinterpret_cast<decltype(OriginalCCvar__InstallGlobalChangeCallback)>(r1vtable[20]);
		OriginalCCvar__RemoveGlobalChangeCallback = reinterpret_cast<decltype(OriginalCCvar__RemoveGlobalChangeCallback)>(r1vtable[21]);
		OriginalCCVar_CallGlobalChangeCallbacks = reinterpret_cast<decltype(OriginalCCVar_CallGlobalChangeCallbacks)>(r1vtable[22]);
		uintptr_t oCCvar__InstallConsoleDisplayFunc = r1vtable[23];
		uintptr_t oCCvar__RemoveConsoleDisplayFunc = r1vtable[24];
		uintptr_t oCCvar__ConsoleColorPrintf = r1vtable[25];
		uintptr_t oCCvar__ConsolePrintf = r1vtable[26];
		uintptr_t oCCvar__ConsoleDPrintf = r1vtable[27];
		uintptr_t oCCVar__RevertFlaggedConVars = r1vtable[28];
		uintptr_t oCCvar__InstallCVarQuery = r1vtable[29];
		uintptr_t oCCvar__SetMaxSplitScreenSlots = r1vtable[30];
		uintptr_t oCCvar__GetMaxSplitScreenSlots = r1vtable[31];
		uintptr_t oCCvar__GetConsoleDisplayFuncCount = r1vtable[32];
		uintptr_t oCCvar__GetConsoleText = r1vtable[33];
		uintptr_t oCCvar__IsMaterialThreadSetAllowed = r1vtable[34];
		OriginalCCVar_QueueMaterialThreadSetValue1 = reinterpret_cast<decltype(OriginalCCVar_QueueMaterialThreadSetValue1)>(r1vtable[35]);
		OriginalCCVar_QueueMaterialThreadSetValue2 = reinterpret_cast<decltype(OriginalCCVar_QueueMaterialThreadSetValue2)>(r1vtable[36]);
		OriginalCCVar_QueueMaterialThreadSetValue3 = reinterpret_cast<decltype(OriginalCCVar_QueueMaterialThreadSetValue3)>(r1vtable[37]);
		uintptr_t oCCvar__HasQueuedMaterialThreadConVarSets = r1vtable[38];
		//uintptr_t CCvar__ProcessQueuedMaterialThreadConVarSets = r1vtable[39];
		uintptr_t oCCvar__FactoryInternalIterator = r1vtable[40];

		static uintptr_t r1ovtable[] = {
			CreateFunction((void*)oCCvar__Connect, (void*)cvarinterface),
			CreateFunction((void*)oCCvar__Disconnect, (void*)cvarinterface),
			CreateFunction((void*)oCCvar__QueryInterface, (void*)cvarinterface),
			CreateFunction((void*)oCCVar__Init, (void*)cvarinterface),
			CreateFunction((void*)oCCVar__Shutdown, (void*)cvarinterface),
			CreateFunction((void*)oCCvar__GetDependencies, (void*)cvarinterface),
			CreateFunction((void*)oCCVar__GetTier, (void*)cvarinterface),
			CreateFunction((void*)oCCVar__Reconnect, (void*)cvarinterface),
			CreateFunction((void*)oCCvar__AllocateDLLIdentifier, (void*)cvarinterface),
			CreateFunction(CCVar__SetSomeFlag_Corrupt, (void*)cvarinterface),
			CreateFunction(CCVar__GetSomeFlag, (void*)cvarinterface),
			CreateFunction(CCVar_RegisterConCommand, (void*)cvarinterface),
			CreateFunction(CCVar_UnregisterConCommand, (void*)cvarinterface),
			CreateFunction((void*)oCCvar__UnregisterConCommands, (void*)cvarinterface),
			CreateFunction((void*)oCCvar__GetCommandLineValue, (void*)cvarinterface),
			CreateFunction(CCVar_FindCommandBase, (void*)cvarinterface),
			CreateFunction(CCVar_FindCommandBase2, (void*)cvarinterface),
			CreateFunction(CCVar_FindVar, (void*)cvarinterface),
			CreateFunction(CCVar_FindVar2, (void*)cvarinterface),
			CreateFunction(CCVar_FindCommand, (void*)cvarinterface),
			CreateFunction(CCVar_FindCommand2, (void*)cvarinterface),
			CreateFunction((void*)oCCVar__Find, (void*)cvarinterface),
			CreateFunction(CCvar__InstallGlobalChangeCallback, (void*)cvarinterface),
			CreateFunction(CCvar__RemoveGlobalChangeCallback, (void*)cvarinterface),
			CreateFunction(CCVar_CallGlobalChangeCallbacks, (void*)cvarinterface),
			CreateFunction((void*)oCCvar__InstallConsoleDisplayFunc, (void*)cvarinterface),
			CreateFunction((void*)oCCvar__RemoveConsoleDisplayFunc, (void*)cvarinterface),
			CreateFunction((void*)oCCvar__ConsoleColorPrintf, (void*)cvarinterface),
			CreateFunction((void*)oCCvar__ConsolePrintf, (void*)cvarinterface),
			CreateFunction((void*)oCCvar__ConsoleDPrintf, (void*)cvarinterface),
			CreateFunction((void*)oCCVar__RevertFlaggedConVars, (void*)cvarinterface),
			CreateFunction((void*)oCCvar__InstallCVarQuery, (void*)cvarinterface),
			CreateFunction((void*)oCCvar__SetMaxSplitScreenSlots, (void*)cvarinterface),
			CreateFunction((void*)oCCvar__GetMaxSplitScreenSlots, (void*)cvarinterface),
			CreateFunction((void*)oCCvar__GetConsoleDisplayFuncCount, (void*)cvarinterface),
			CreateFunction((void*)oCCvar__GetConsoleText, (void*)cvarinterface),
			CreateFunction((void*)oCCvar__IsMaterialThreadSetAllowed, (void*)cvarinterface),
			CreateFunction(CCVar_QueueMaterialThreadSetValue1, (void*)cvarinterface),
			CreateFunction(CCVar_QueueMaterialThreadSetValue2, (void*)cvarinterface),
			CreateFunction(CCVar_QueueMaterialThreadSetValue3, (void*)cvarinterface),
			CreateFunction((void*)oCCvar__HasQueuedMaterialThreadConVarSets, (void*)cvarinterface),
			CreateFunction(CCvar__ProcessQueuedMaterialThreadConVarSets, (void*)cvarinterface),
			CreateFunction((void*)oCCvar__FactoryInternalIterator, r1vtable)
		};
		static void* whatever6 = &r1ovtable; // double ref return
		return &whatever6;
	}
	auto result = oAppSystemFactory(pName, pReturnCode);
	if (!result && !strcmp_static(pName, "VENGINE_DEDICATEDEXPORTS_API_VERSION003")) {
		//std::cout << "forging dediexports" << std::endl;
		return (void*)1;
	}
	if (result) {
		//std::cout << "found " << pName << "  in appsystem factory" << std::endl;
		return result;
	}

	//std::cout << "engine is set up, looking for " << pName << std::endl;

	return R1OCreateInterface(pName, pReturnCode);
}

class SomeNexonBullshit {
public:
	virtual void whatever() = 0;
	virtual void Init() = 0;
};
CGlobalVarsServer2015* pGlobalVarsServer;
char __fastcall CServerGameDLL__DLLInit(void* thisptr, CreateInterfaceFn appSystemFactory,
	CreateInterfaceFn physicsFactory, CreateInterfaceFn fileSystemFactory,
	CGlobalVarsServer2015* pGlobals) {
	if (IsDedicatedServer()) {
		pGlobals = (CGlobalVarsServer2015*)((uintptr_t)pGlobals - 4); // Don't ask. If you DO ask, you will die a violent, painful death - wndrr
		pGlobals->nTimestampNetworkingBase = 100;
		pGlobals->nTimestampRandomizeWindow = 32;
	}
	pGlobalVarsServer = pGlobals;
	void* serverPtr = (void*)G_server;
	SendProp* DT_BasePlayer = (SendProp*)(((uintptr_t)serverPtr) + 0xE9A800);
	int* DT_BasePlayerLen = (int*)(((uintptr_t)serverPtr) + 0xE04768);

	// Move m_titanRespawnTime from DT_Local to the end of DT_BasePlayer and rename it
	SendProp* DT_Local = (SendProp*)(((uintptr_t)serverPtr) + 0xE9E340);
	int* DT_LocalLen = (int*)(((uintptr_t)serverPtr) + 0xE04B48);

	for (int i = 0; i < *DT_LocalLen; ++i) {
		if (strcmp_static(DT_Local[i].name, "m_titanRespawnTime") == 0) {
			DT_BasePlayer[*DT_BasePlayerLen] = DT_Local[i];
			DT_BasePlayer[*DT_BasePlayerLen].name = "m_nextTitanRespawnAvailable";
			++(*DT_BasePlayerLen);

			DestroySendProp(DT_Local, DT_LocalLen, "m_titanRespawnTime");
			break;
		}
	}
	oAppSystemFactory = appSystemFactory;
	oFileSystemFactory = fileSystemFactory;
	oPhysicsFactory = physicsFactory;
	engineR1O = LoadLibraryA("engine_r1o.dll");
	R1OCreateInterface = reinterpret_cast<CreateInterfaceFn>(GetProcAddress(engineR1O, "CreateInterface"));

	extern void* SetPreCache_o;
	__int64 __fastcall SetPreCache(__int64 a1, __int64 a2, char a3);
	MH_CreateHook(LPVOID(uintptr_t(engineR1O) + 0xF5790), &SetPreCache, &SetPreCache_o);
	MH_EnableHook(MH_ALL_HOOKS);
	CreateInterfaceFn fnptr = (CreateInterfaceFn)(&R1OFactory);
	reinterpret_cast<char(__fastcall*)(__int64, CreateInterfaceFn)>((uintptr_t)(engineR1O)+0x1C6B30)(0, fnptr); // call is to CDedicatedServerAPI::Connect
	void* whatev = fnptr;
	//reinterpret_cast<void(__fastcall*)()>((uintptr_t)(engineR1O)+0x2742A0)(); // register engine convars
	//*(__int64*)((uintptr_t)(GetModuleHandleA("launcher_r1o.dll")) + 0xECBE0) = fsintfakeptr;
	reinterpret_cast<__int64(__fastcall*)(int a1)>(GetProcAddress(GetModuleHandleA("tier0_orig.dll"), "SetTFOFileLogLevel"))(999);
	SomeNexonBullshit* tfotableversion = (SomeNexonBullshit*)R1OCreateInterface("TFOTableVersion", 0);
	SomeNexonBullshit* tfoitems = (SomeNexonBullshit*)R1OCreateInterface("TFOItemSystem", 0);
	SomeNexonBullshit* tfoinventory = (SomeNexonBullshit*)R1OCreateInterface("TFOInentorySystem", 0);
	SomeNexonBullshit* tfomsghandler = (SomeNexonBullshit*)R1OCreateInterface("TFOMsgHandler001", 0);
	SomeNexonBullshit* tfogamemanager = (SomeNexonBullshit*)R1OCreateInterface("TFOGameManager", 0);
	SomeNexonBullshit* staticclasssystem = (SomeNexonBullshit*)R1OCreateInterface("StaticClassSystem001", 0);
	tfotableversion->Init();
	tfoitems->Init();
	tfoinventory->Init();
	tfomsghandler->Init();
	tfogamemanager->Init();
	staticclasssystem->Init();
	return CServerGameDLL__DLLInitOriginal(thisptr, fnptr, fnptr, fnptr, pGlobals);
}

extern "C" __declspec(dllexport) void StackToolsNotify_LoadedLibrary(char* pModuleName) {
	//printf("loaded %s\n", pModuleName);
}
typedef char(*MatchRecvPropsToSendProps_RType)(__int64 a1, __int64 a2, __int64 a3, __int64 a4);
MatchRecvPropsToSendProps_RType MatchRecvPropsToSendProps_ROriginal;

// #STR: "CompareRecvPropToSendProp: missing a property."
char __fastcall CompareRecvPropToSendProp(__int64 a1, __int64 a2) {
	int v4; // ecx

	while (1) {
		if (!a1 || !a2)
			std::cout << "CompareRecvPropToSendProp: missing a property." << std::endl;
		v4 = *(_DWORD*)(a1 + 8);
		if (v4 != *(_DWORD*)(a2 + 16) || *(_BYTE*)(a1 + 20) != (BYTE1(*(_DWORD*)(a2 + 88)) & 1))
			break;
		if (v4 != 5)
			return 1;
		if (*(_DWORD*)(a1 + 80) != *(_DWORD*)(a2 + 48))
			break;
		a1 = *(_QWORD*)(a1 + 32);
		a2 = *(_QWORD*)(a2 + 32);
	}
	return 0;
}
__int64 FindRecvProp(__int64 a4, const char* v9) {
	const char* v16 = 0; // [rsp+30h] [rbp-38h]

	__int64 RecvProp = 0; // rbp
	int v10 = 0; // ebx
	__int64 j = 0; // rdi

	v10 = 0;
	v16 = v9;
	if (*(int*)(a4 + 8) <= 0)
		return 0;
	for (j = 0i64; ; j += 96i64) {
		RecvProp = j + *(_QWORD*)a4;
		if (_stricmp(*(const char**)RecvProp, v9) == 0)
			break;
		if (++v10 >= *(_DWORD*)(a4 + 8))
			return 0;
		v9 = v16;
	}
	return RecvProp;
}
char __fastcall MatchRecvPropsToSendProps_R(__int64 a1, __int64 a2, __int64 pSendTable, __int64 a4) {
	_QWORD* v5; // rax
	__int64 v6; // rcx
	int v8; // ecx
	__int64 RecvProp; // rbp
	int v14; // [rsp+20h] [rbp-48h]
	__int64 i; // [rsp+28h] [rbp-40h]
	__int64 v17; // [rsp+38h] [rbp-30h]
	__int64 v18[5]; // [rsp+40h] [rbp-28h] BYREF
	__int64 v7; // rdx
	unsigned __int8* v9; // rcx

	auto sub_1801D9D00 = (__int64(__fastcall*)(__int64 a1, _QWORD * a2))(ENGINE_DLL_BASE + 0x1D9D00);
	v5 = (_QWORD*)pSendTable;
	v5 = (_QWORD*)pSendTable;
	v14 = 0;
	if (*(int*)(pSendTable + 8) <= 0)
		return 1;
	v6 = 0i64;
	for (i = 0i64; ; i += 136i64) {
		v7 = v6 + *v5;
		v17 = v7;
		v8 = *(_DWORD*)(v7 + 88);
		if ((v8 & 0x40) == 0 && (v8 & 0x100) == 0) {
			if (!a4)
				break;

			RecvProp = FindRecvProp(a4, *(const char**)(v7 + 72));
			if (RecvProp) {
				if (!CompareRecvPropToSendProp(RecvProp, v17))
					break;
				v18[0] = v17;
				v18[1] = RecvProp;
				sub_1801D9D00(a1, (uint64*)v18);
			}
			else {
				std::cout << "Missing RecvProp for " << *(const char**)(v7 + 72) << std::endl;
				MessageBoxA(NULL, *(const char**)(v7 + 72), "SendProp Error", 16);
			}
			if (*(_DWORD*)(v17 + 16) == 6
				&& !MatchRecvPropsToSendProps_R(a1, a2, *(_QWORD*)(v17 + 112), *(_QWORD*)(RecvProp + 64))) {
				break;
			}
		}
		v6 = i + 136;
		if (++v14 >= *(_DWORD*)(pSendTable + 8))
			return 1;
		v5 = (_QWORD*)pSendTable;
	}
	return 0;
}

typedef char(*sub_1801C79A0Type)(__int64 a1, __int64 a2);
sub_1801C79A0Type sub_1801C79A0Original;
char __fastcall sub_1801C79A0(__int64 a1, __int64 a2) {
	auto dtname = *(const char**)(a1 + 16);
	auto dtname_len = strlen(dtname);
	if (string_equal_size(dtname, dtname_len, "DT_BigBrotherPanelEntity") || string_equal_size(dtname, dtname_len, "DT_ControlPanelEntity") || string_equal_size(dtname, dtname_len, "DT_RushPointEntity") || string_equal_size(dtname, dtname_len, "DT_SpawnItemEntity")) {
		std::cout << "blocking st " << *(const char**)(a1 + 16) << std::endl;
		return false;
	}
	return sub_1801C79A0Original(a1, a2);
}

char __fastcall sub_180217C30(char* a1, __int64 size, _QWORD* a3, __int64 a4) {
	return true;
}
const char* scripterr() {
	return "scripterror.log";
}

__forceinline BOOL CheckIfCallingDLLContainsR1o() {
	// Get the return address of the calling function
	PVOID retAddress = _ReturnAddress();

	// Get a handle to the module (DLL) based on the return address
	HMODULE hModule;
	if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)retAddress, &hModule)) {
		return FALSE; // Failed to get module handle
	}

	// Retrieve the module (DLL) name
	char szModuleName[MAX_PATH];
	if (GetModuleFileNameA(hModule, szModuleName, MAX_PATH) == 0) {
		return FALSE; // Failed to get module name
	}

	// Convert module name to uppercase for case-insensitive comparison
	_strupr_s(szModuleName, MAX_PATH);

	// Check if "R1O" is in the module name
	return strstr(szModuleName, "R1O") != NULL;
}
__int64 VStdLib_GetICVarFactory() {
	return CheckIfCallingDLLContainsR1o() ? (__int64)R1OFactory : (__int64)(((uintptr_t)(GetModuleHandleA("vstdlib.dll")) + 0x023DD0));
}

uintptr_t CreateTrampolineFunction(void* vftable, uintptr_t engineDsStart, uintptr_t engineDsEnd, int original_offset, int new_offset) {
	void* execMem = VirtualAlloc(NULL, 128, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (!execMem) return 0;

	uint8_t* bytes = static_cast<uint8_t*>(execMem);

	// Load the return address into rax
	*bytes++ = 0x48; *bytes++ = 0x8B; *bytes++ = 0x04; *bytes++ = 0x24; // mov rax, [rsp]

	// Move engineDsStart to r10 for comparison
	*bytes++ = 0x49; *bytes++ = 0xBA; // mov r10, engineDsStart
	*(uintptr_t*)bytes = engineDsStart;
	bytes += sizeof(uintptr_t);

	// cmp rax, r10
	*bytes++ = 0x4C; *bytes++ = 0x39; *bytes++ = 0xD0;

	// jb originalFunction (Offset will be filled later)
	*bytes++ = 0x0F; *bytes++ = 0x82;
	int32_t* jbOffsetLoc = reinterpret_cast<int32_t*>(bytes);
	bytes += sizeof(int32_t);

	// Move engineDsEnd to r10 for comparison
	*bytes++ = 0x49; *bytes++ = 0xBA; // mov r10, engineDsEnd
	*(uintptr_t*)bytes = engineDsEnd;
	bytes += sizeof(uintptr_t);

	// cmp rax, r10
	*bytes++ = 0x4C; *bytes++ = 0x39; *bytes++ = 0xD0;

	// ja originalFunction (Offset will be filled later)
	*bytes++ = 0x0F; *bytes++ = 0x87;
	int32_t* jaOffsetLoc = reinterpret_cast<int32_t*>(bytes);
	bytes += sizeof(int32_t);

	// Redirect to new function
	uintptr_t newFunctionAddress = ((uintptr_t*)vftable)[new_offset];
	*bytes++ = 0x48; *bytes++ = 0xB8; // mov rax, newFunctionAddress
	*(uintptr_t*)bytes = newFunctionAddress;
	bytes += sizeof(uintptr_t);
	*bytes++ = 0xFF; *bytes++ = 0xE0; // jmp rax

	uintptr_t originalFunctionLoc = reinterpret_cast<uintptr_t>(bytes);

	// Fill in the offsets for jb and ja
	*jbOffsetLoc = static_cast<int32_t>(originalFunctionLoc - (reinterpret_cast<uintptr_t>(jbOffsetLoc) + sizeof(int32_t)));
	*jaOffsetLoc = static_cast<int32_t>(originalFunctionLoc - (reinterpret_cast<uintptr_t>(jaOffsetLoc) + sizeof(int32_t)));

	// Original function pointer
	uintptr_t originalFunctionAddress = ((uintptr_t*)vftable)[original_offset];

	// mov rax, originalFunctionAddress
	*bytes++ = 0x48; *bytes++ = 0xB8;
	*(uintptr_t*)bytes = originalFunctionAddress;
	bytes += sizeof(uintptr_t);

	// jmp rax
	*bytes++ = 0xFF; *bytes++ = 0xE0;

	return reinterpret_cast<uintptr_t>(execMem);
}

const int indexMapping[] = {
		0,  // CNetChan__GetName (original index 0, new index 0)
		1,  // CNetChan__GetAddress (original index 1, new index 1)
		2,  // CNetChan__GetTime (original index 2, new index 2)
		3,  // CNetChan__GetTimeConnected (original index 3, new index 3)
		4,  // CNetChan__GetBufferSize (original index 4, new index 4)
		5,  // CNetChan__GetDataRate (original index 5, new index 5)
		6,  // CNetChan__IsLoopback (original index 6, new index 6)
		7,  // CNetChan__IsTimingOut (original index 7, new index 7)
		8,  // CNetChan__IsPlayback (original index 8, new index 8)
		9,  // CNetChan__GetLatency (original index 9, new index 9)
		10,  // CNetChan__GetAvgLatency (original index 10, new index 10)
		11,  // CNetChan__GetAvgLoss (original index 11, new index 11)
		12,  // CNetChan__GetAvgChoke (original index 12, new index 12)
		13,  // CNetChan__GetAvgData (original index 13, new index 13)
		14,  // CNetChan__GetAvgPackets (original index 14, new index 14)
		15,  // CNetChan__GetTotalData (original index 15, new index 15)
		16,  // CNetChan__GetTotalPackets (original index 16, new index 16)
		17,  // CNetChan__GetSequenceNr (original index 17, new index 17)
		18,  // CNetChan__IsValidPacket (original index 18, new index 18)
		19,  // CNetChan__GetPacketTime (original index 19, new index 19)
		20,  // CNetChan__GetPacketBytes (original index 20, new index 20)
		21,  // CNetChan__GetStreamProgress (original index 21, new index 21)
		22,  // CNetCHan__GetTimeSinceLastReceived (original index 22, new index 22)
		23,  // CNetChan__GetPacketResponseLatency (original index 23, new index 23)
		24,  // CNetChan__GetRemoteFramerate (original index 24, new index 24)
		25,  // CNetChan__GetTimeoutSeconds (original index 25, new index 25)
		26,  // CNetCHan____DESTROY (original index 26, new index 26)
		27,  // CNetChan__SetDataRate (original index 27, new index 27)
		28,  // CNetChan__RegisterMessage (original index 28, new index 28)
		29,  // CNetCHan__StartStreaming (original index 29, new index 29)
		30,  // CNetChan__ResetStreaming (original index 30, new index 30)
		31,  // CNetChan__SetTimeout (original index 31, new index 31)
		32,  // CNetChan__SetDemoRecorder (original index 32, new index 32)
		33,  // CNetChan__SetChallengeNr (original index 33, new index 33)
		34,  // CNetChan__Reset (original index 34, new index 34)
		35,  // CNetChan__Clear (original index 35, new index 35)
		36,  // CNetChan__Shutdown (original index 36, new index 36)
		45,  // CNetChan__RequestFile_OLD (original index 37, new index 45)
		38,  // CNetChan__ProcessStream (original index 38, new index 38)
		39,  // CNetChan__ProcessPacket (original index 39, new index 39)
		41,  // CNetChan__SendNetMsg (original index 40, new index 41)
		42,  // CNetChan__SendData (original index 41, new index 42)
		43,  // CNetChan__SendFile (original index 42, new index 43)
		44,  // CNetChan__DenyFile (original index 43, new index 44)
		45,  // CNetChan__RequestFile_OLD (original index 44, new index 45)
		46,  // CNetChan__SetChoked (original index 45, new index 46)
		52,  // CNetChan__SendDatagram (original index 46, new index 52)
		53,  // CNetChan__PostSendDatagram (original index 47, new index 53)
		54,  // CNetChan__Transmit (original index 48, new index 54)
		55,  // CNetChan__GetRemoteAddress (original index 49, new index 55)
		56,  // CNetChan__GetMsgHandler (original index 50, new index 56)
		57,  // CNetChan__GetDropNumber (original index 51, new index 57)
		58,  // CNetChan__GetSocket (original index 52, new index 58)
		59,  // CNetChan__GetChallengeNr (original index 53, new index 59)
		60,  // CNetChan__GetSequenceData (original index 54, new index 60)
		61,  // CNetChan__SetSequenceData (original index 55, new index 61)
		62,  // CNetChan__UpdateMessageStats (original index 56, new index 62)
		63,  // CNetChan__CanPacket (original index 57, new index 63)
		64,  // CNetChan__IsOverflowed (original index 58, new index 64)
		65,  // CNetChan__IsTimedOut (original index 59, new index 65)
		66,  // CNetChan__HasPendingReliableData (original index 60, new index 66)
		67,  // CNetChan__SetFileTransmissionMode (original index 61, new index 67)
		68,  // CNetChan__SetCompressionMode (original index 62, new index 68)
		69,  // CNetChan__RequestFile (original index 63, new index 69)
		70,  // CNetChan__SetMaxBufferSize (original index 64, new index 70)
		71,  // CNetChan__IsNull (original index 65, new index 71)
		72,  // CNetChan__GetNumBitsWritten (original index 66, new index 72)
		73,  // sub_1801E1F60 (original index 67, new index 73)
		74,  // CNetChan__SetInterpolationAmount (original index 68, new index 74)
		75,  // CNetChan____UnkGet (original index 69, new index 75)
		76,  // CNetChan__SetActiveChannel (original index 70, new index 76)
		77,  // CNetChan__AttachSplitPlayer (original index 71, new index 77)
		78,  // CNetChan__DetachSplitPlayer (original index 72, new index 78)
		79,  // CNetChan____WhateverFunc (original index 73, new index 79)
		80,  // CNetChan__GetStreamByIndex (original index 74, new index 80)
		81,  // CNetChan____SetUnknownString (original index 75, new index 81)
};

void* modifiedNetCHANVTable = nullptr;

void InitializeModifiedNetCHANVTable(void* netChan) {
	if (modifiedNetCHANVTable == nullptr) {
		//CModule engineDS("engine_ds.dll");
		auto engineDS = G_engine_ds;
		uintptr_t engineDS_size = 0;
		{
			auto mz = (PIMAGE_DOS_HEADER)engineDS;
			auto pe = (PIMAGE_NT_HEADERS64)((uint8_t*)mz + mz->e_lfanew);
			engineDS_size = pe->OptionalHeader.SizeOfImage;
		}
		auto start = engineDS;
		auto end = start + engineDS_size;
		// Get the original vtable from the provided CNetChan object
		uintptr_t* originalVTable = *(uintptr_t**)netChan;

		size_t vtableSize = 83 * sizeof(uintptr_t);  // Adjust the size according to the number of virtual functions

		// Allocate memory for the modified vtable and the flag
		modifiedNetCHANVTable = malloc(vtableSize);

		if (modifiedNetCHANVTable) {
			// Copy the original vtable to the modified vtable
			memcpy(modifiedNetCHANVTable, originalVTable, vtableSize);

			// Create trampolines for each remapped virtual function and update the modified vtable
			for (int i = 0; i < sizeof(indexMapping) / sizeof(indexMapping[0]); i++) {
				int originalIndex = i;
				int newIndex = indexMapping[i];
				if (originalIndex != newIndex) {
					((uintptr_t*)modifiedNetCHANVTable)[originalIndex] = CreateTrampolineFunction(originalVTable, start, end, originalIndex, newIndex); // originalIndex will be jumped to if from engine.dll, newIndex otherwise
				}
			}
		}
	}
}

using NET_CreateNetChannelType = void* (__fastcall*)(int a1, unsigned int* a2, const char* a3, __int64 a4, char a5, char a6);
NET_CreateNetChannelType NET_CreateNetChannelOriginal;

void* __fastcall NET_CreateNetChannel(int a1, unsigned int* a2, const char* a3, __int64 a4, char a5, char a6) {
	void* netChan = NET_CreateNetChannelOriginal(a1, a2, a3, a4, a5, a6);
	InitializeModifiedNetCHANVTable(netChan);
	*(uintptr_t**)netChan = static_cast<uintptr_t*>(modifiedNetCHANVTable);
	return netChan;
}