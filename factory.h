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
typedef void* (*CScriptManager__CreateNewVMType)(__int64 a1, int a2, unsigned int a3);
extern CScriptManager__CreateNewVMType CScriptManager__CreateNewVMOriginal;
void* CScriptManager__CreateNewVM(__int64 a1, int a2, unsigned int a3);
typedef __int64 (*CScriptVM__ctortype)(void* thisptr);
extern CScriptVM__ctortype CScriptVM__ctororiginal;
__int64 __fastcall CScriptVM__ctor(void* thisptr);
typedef void* (*CScriptVM__GetUnknownVMPtrType)();
extern CScriptVM__GetUnknownVMPtrType CScriptVM__GetUnknownVMPtrOriginal;
void* CScriptVM__GetUnknownVMPtr();

typedef __int64 (*sub_180008AB0Type)(__int64 a1, void* a2, unsigned int* a3, unsigned int a4, __int64 a5, int* a6);
extern sub_180008AB0Type sub_180008AB0Original;
__int64 __fastcall sub_180008AB0(__int64 a1, void* a2, unsigned int* a3, unsigned int a4, __int64 a5, int* a6);
typedef void (*CScriptManager__DestroyVMType)(void* a1, void* vmptr);
extern CScriptManager__DestroyVMType CScriptManager__DestroyVMOriginal;
void __fastcall CScriptManager__DestroyVM(void* a1, void* vmptr);

struct ScriptVariant_t;
typedef void (*CSquirrelVM__RegisterFunctionGutsType)(__int64* a1, __int64 a2, const char** a3);
extern CSquirrelVM__RegisterFunctionGutsType CSquirrelVM__RegisterFunctionGutsOriginal;
typedef __int64 (*CSquirrelVM__PushVariantType)(__int64* a1, ScriptVariant_t* a2);
extern CSquirrelVM__PushVariantType CSquirrelVM__PushVariantOriginal;
typedef char (*CSquirrelVM__ConvertToVariantType)(__int64* a1, __int64 a2, ScriptVariant_t* a3);
extern CSquirrelVM__ConvertToVariantType CSquirrelVM__ConvertToVariantOriginal;
typedef __int64 (*CSquirrelVM__ReleaseValueType)(__int64* a1, ScriptVariant_t* a2);
extern CSquirrelVM__ReleaseValueType CSquirrelVM__ReleaseValueOriginal;
typedef bool (*CSquirrelVM__SetValueType)(__int64* a1, void* a2, unsigned int a3, ScriptVariant_t* a4);
extern CSquirrelVM__SetValueType CSquirrelVM__SetValueOriginal;
typedef bool (*CSquirrelVM__SetValueExType)(__int64* a1, __int64 a2, const char* a3, ScriptVariant_t* a4);
extern CSquirrelVM__SetValueExType CSquirrelVM__SetValueExOriginal;
typedef __int64 (*CSquirrelVM__TranslateCallType)(__int64* a1);
extern CSquirrelVM__TranslateCallType CSquirrelVM__TranslateCallOriginal;
void __fastcall CSquirrelVM__TranslateCall(__int64* a1);
void __fastcall CSquirrelVM__RegisterFunctionGuts(__int64* a1, __int64 a2, const char** a3);
__int64 __fastcall CSquirrelVM__PushVariant(__int64* a1, ScriptVariant_t* a2);
char __fastcall CSquirrelVM__ConvertToVariant(__int64* a1, __int64 a2, ScriptVariant_t* a3);
__int64 __fastcall CSquirrelVM__ReleaseValue(__int64* a1, ScriptVariant_t* a2);
bool __fastcall CSquirrelVM__SetValue(__int64* a1, void* a2, unsigned int a3, ScriptVariant_t* a4);
bool __fastcall CSquirrelVM__SetValueEx(__int64* a1, __int64 a2, const char* a3, ScriptVariant_t* a4);
extern CreateInterfaceFn oAppSystemFactory;
extern CreateInterfaceFn oFileSystemFactory;
extern CreateInterfaceFn oPhysicsFactory;
typedef void* (*NET_CreateNetChannelType)(int a1, _DWORD* a2, const char* a3, __int64 a4, char a5, char a6);
extern NET_CreateNetChannelType NET_CreateNetChannelOriginal;
void* __fastcall NET_CreateNetChannel(int a1, _DWORD* a2, const char* a3, __int64 a4, char a5, char a6);
uintptr_t CreateFunction(void* func, void* real);