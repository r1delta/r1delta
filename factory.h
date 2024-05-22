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
#include "cvar.h"
#include "squirrel.h"
typedef void* (*CreateInterfaceFn)(const char* pName, int* pReturnCode);
typedef char(__fastcall* CServerGameDLL__DLLInitType)(void* thisptr, CreateInterfaceFn appSystemFactory,
	CreateInterfaceFn physicsFactory, CreateInterfaceFn fileSystemFactory,
	void* pGlobals);
extern CServerGameDLL__DLLInitType CServerGameDLL__DLLInitOriginal;
struct CGlobalVarsServer2015 {
	double realtime;                                       // 0x0000
	int framecount;                                        // 0x0008
	float absoluteframetime;                               // 0x000C
	float curtime;                                         // 0x0010
	char pad0[4];                                      // 0x0014, padding to align next member at 8-byte boundary
	float curtimeUnkSeemsLikeCopy;                         // 0x0018
	int frametime;                                         // 0x001C
	int maxClients;                                        // 0x0020
	int tickcount;                                         // 0x0024
	float interval_per_tick;                               // 0x0028
	float interpolation_amount;                            // 0x002C
	int simTicksThisFrame;                                 // 0x0030
	char pad1[4];                                      // 0x0034, padding to align next member at 8-byte boundary
	void* pSaveData;                                       // 0x0038, assuming pointer for pSaveData
	char pad2[4];                                      // 0x0040, additional padding if necessary
	int nTimestampNetworkingBase;                          // 0x0044
	int nTimestampRandomizeWindow;                         // 0x0048
	char pad3[4];                                      // 0x004C, padding to align next member at 16-byte boundary
	void* mapname_pszValue;                                // 0x0050, assuming pointer for mapname_pszValue
	int mapversion;                                        // 0x0058
	char pad4[4];                                      // 0x005C, padding to align next member at 32-byte boundary
	void* startspot;                                       // 0x0060, assuming pointer for startspot
	int eLoadType;                                         // 0x0068
	char bMapLoadFailed;                                   // 0x006C
	char deathmatch;                                       // 0x006D
	char coop;                                             // 0x006E
	char pad5[1];                                      // 0x006F, padding to align next member at 16-byte boundary
	int maxEntities;                                       // 0x0070
	int serverCount;                                       // 0x0074
	void* pEdicts;                                         // 0x0078, assuming pointer for pEdicts
};

char __fastcall CServerGameDLL__DLLInit(void* thisptr, CreateInterfaceFn appSystemFactory,
	CreateInterfaceFn physicsFactory, CreateInterfaceFn fileSystemFactory,
	CGlobalVarsServer2015* pGlobals);
__int64 VStdLib_GetICVarFactory();
char __fastcall MatchRecvPropsToSendProps_R(__int64 a1, __int64 a2, __int64 pSendTable, __int64 a4);
typedef char(*sub_1801C79A0Type)(__int64 a1, __int64 a2);
extern sub_1801C79A0Type sub_1801C79A0Original;
char __fastcall sub_1801C79A0(__int64 a1, __int64 a2);
extern CreateInterfaceFn oAppSystemFactory;
extern CreateInterfaceFn oFileSystemFactory;
extern CreateInterfaceFn oPhysicsFactory;
typedef void* (*NET_CreateNetChannelType)(int a1, _DWORD* a2, const char* a3, __int64 a4, char a5, char a6);
extern NET_CreateNetChannelType NET_CreateNetChannelOriginal;
void* __fastcall NET_CreateNetChannel(int a1, _DWORD* a2, const char* a3, __int64 a4, char a5, char a6);