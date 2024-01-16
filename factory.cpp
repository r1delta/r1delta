#include <MinHook.h>
#include <cstdlib>
#include <crtdbg.h>	
#include <new>
#include "windows.h"
#include "tier0_orig.h"
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
CServerGameDLL__DLLInitType CServerGameDLL__DLLInitOriginal;
CreateInterfaceFn oAppSystemFactory;
CreateInterfaceFn oFileSystemFactory;
CreateInterfaceFn oPhysicsFactory;

__int64 NULLNET_Config()
{
	return 0;
}
char* __fastcall NULLNET_GetPacket(int a1, __int64 a2, unsigned __int8 a3)
{
	return 0;
}
__int64(__fastcall* __fastcall CVEngineServer__GetNETConfig_TFO(__int64 a1, __int64 (**a2)()))()
{
	__int64(__fastcall* result)(); // rax

	result = NULLNET_Config;
	*a2 = NULLNET_Config;
	return result;
}
char* (__fastcall* __fastcall CVEngineServer__GetNETGetPacket_TFO(
	__int64 a1,
	char* (__fastcall** a2)(int a1, __int64 a2, unsigned __int8 a3)))(int a1, __int64 a2, unsigned __int8 a3)
{
	char* (__fastcall* result)(int, __int64, unsigned __int8); // rax

	result = NULLNET_GetPacket;
	*a2 = NULLNET_GetPacket;
	return result;
}
__int64(__fastcall* __fastcall CVEngineServer__GetLocalNETConfig_TFO(__int64 a1, __int64 (**a2)()))()
{
	__int64(__fastcall* result)(); // rax

	result = NULLNET_Config;
	*a2 = NULLNET_Config;
	return result;
}
char* (__fastcall* __fastcall CVEngineServer__GetLocalNETGetPacket_TFO(
	__int64 a1,
	char* (__fastcall** a2)(int a1, __int64 a2, unsigned __int8 a3)))(int a1, __int64 a2, unsigned __int8 a3)
{
	char* (__fastcall* result)(int, __int64, unsigned __int8); // rax

	result = NULLNET_GetPacket;
	*a2 = NULLNET_GetPacket;
	return result;
}
char CFileSystem_Stdio__NullSub3()
{
	return 0;
}
__int64 CFileSystem_Stdio__NullSub4()
{
	return 0i64;
}
__int64 CBaseFileSystem__GetTFOFileSystemThing()
{
	return 0;
}
__int64 __fastcall CFileSystem_Stdio__DoTFOFilesystemOp(__int64 a1, char* a2, rsize_t a3)
{
	return false;
}
void __fastcall CModelInfo__UnkTFOVoid(__int64 a1, int* a2, __int64 a3)
{

}
void __fastcall CModelInfo__UnkTFOVoid2(__int64 a1, __int64 a2)
{
}
void __fastcall CModelInfo__UnkTFOVoid3(__int64 a1, __int64 a2, unsigned __int64 a3)
{
	return;
}
void __fastcall CCVar__SetSomeFlag_Corrupt(__int64 a1, __int64 a2)
{
	return;
}
__int64 __fastcall CCVar__GetSomeFlag(__int64 thisptr)
{
	return 0;
}
char __fastcall CModelInfo__UnkTFOShouldRet0_2(__int64 a1, __int64 a2)
{
	if (a2)
		return *(char*)(a2 + 264) & 1;
	else
		return 0;
}
__int64 CModelInfo__ShouldRet0()
{
	return 0;
}
bool CModelInfo__ClientFullyConnected()
{
	return false;
}
bool byte_1824D16C0 = false;
void CModelInfo__UnkSetFlag()
{
	byte_1824D16C0 = 1;
}
void CModelInfo__UnkClearFlag()
{
	byte_1824D16C0 = 0;
}
__int64 CModelInfo__GetFlag()
{
	return (unsigned __int8)byte_1824D16C0;
}
void __fastcall CNetworkStringTableContainer__SetTickCount(__int64 a1, char a2)
{
	*(char*)(a1 + 8) = a2;
}

uintptr_t fsinterface;


//char sub_180009C20(__int64 a1, char* a2, __int64 a3) {
//	// Check if the path starts with "scripts/vscripts"
//	std::string path = a2;
//	if (path.rfind("scripts/vscripts", 0) == 0) {
//		// Replace "scripts/vscripts" with "scripts/vscripts/server"
//		path = "scripts/vscripts/server" + path.substr(16);
//	}
//
//	std::cout << "Server FS: " << path << std::endl;
//	return reinterpret_cast<char(*)(__int64, char*, __int64)>(osub_180009C20)(fsinterface, (char*)(path.c_str()), a3);
//}



uintptr_t modelinterface;
uintptr_t stringtableinterface;
uintptr_t fsinterfaceoffset;
uintptr_t fsintfakeptr = 0;

uintptr_t CreateFunction(void* func, void* real) {
	// allocate executable memory.
	void* execMem = VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (!execMem) return 0;

	uint8_t* bytes = static_cast<uint8_t*>(execMem);

	// mov rcx, real
	*bytes++ = 0x48;
	*bytes++ = 0xB9;
	*reinterpret_cast<uintptr_t*>(bytes) = (uintptr_t)real;
	bytes += sizeof(uintptr_t);

	// jmp func (RIP-relative indirect)
	*bytes++ = 0xFF;
	*bytes++ = 0x25;
	*reinterpret_cast<int32_t*>(bytes) = 0; // offset = 0 (next instruction)
	bytes += sizeof(int32_t);

	*reinterpret_cast<uintptr_t*>(bytes) = (uintptr_t)func;
	bytes += sizeof(uintptr_t);

	// return the function pointer.
	return reinterpret_cast<uintptr_t>(execMem);
}


__int64 CVEngineServer__FuncThatReturnsFF_Stub()
{
	return 0xFFFFFFFFi64;
}
static HMODULE engineR1O;
static HMODULE launcherR1O;
static CreateInterfaceFn R1OCreateInterface;
static CreateInterfaceFn R1OLauncherCreateInterface;
void* R1OFactory(const char* pName, int* pReturnCode) {
	std::cout << "looking for " << pName << std::endl;
	if (!strcmp(pName, "VScriptManager009")) {
		return R1OLauncherCreateInterface(pName, pReturnCode);
	}
	if (!strcmp(pName, "VEngineServer022")) {
		std::cout << "wrapping VEngineServer022" << std::endl;

		uintptr_t* r1vtable = *(uintptr_t**)oAppSystemFactory(pName, pReturnCode);
#ifdef DEDICATED
		uintptr_t CVEngineServer__ChangeLevel = r1vtable[0];
		uintptr_t CVEngineServer__IsMapValid = r1vtable[1];
		uintptr_t CVEngineServer__GetMapCRC = r1vtable[2];
		uintptr_t CVEngineServer__IsDedicatedServer = r1vtable[3];
		uintptr_t CVEngineServer__IsInEditMode = r1vtable[4];
		uintptr_t CVEngineServer__GetLaunchOptions = r1vtable[5];
		uintptr_t CVEngineServer__PrecacheModel = r1vtable[6];
		uintptr_t CVEngineServer__PrecacheDecal = r1vtable[7];
		uintptr_t CVEngineServer__PrecacheGeneric = r1vtable[8];
		uintptr_t CVEngineServer__IsModelPrecached = r1vtable[9];
		uintptr_t CVEngineServer__IsDecalPrecached = r1vtable[10];
		uintptr_t CVEngineServer__IsGenericPrecached = r1vtable[11];
		uintptr_t CVEngineServer__IsSimulating = r1vtable[12];
		uintptr_t CVEngineServer__GetPlayerUserId = r1vtable[13];
		uintptr_t CVEngineServer__GetPlayerNetworkIDString = r1vtable[14];
		uintptr_t CVEngineServer__IsUserIDInUse = r1vtable[15];
		uintptr_t CVEngineServer__GetLoadingProgressForUserID = r1vtable[16];
		uintptr_t CVEngineServer__GetEntityCount = r1vtable[17];
		uintptr_t CVEngineServer__GetPlayerNetInfo = r1vtable[18];
		uintptr_t CVEngineServer__IsClientValid = r1vtable[19];
		uintptr_t CVEngineServer__PlayerChangesTeams = r1vtable[20];
		uintptr_t CVEngineServer__CreateEdict = r1vtable[21];
		uintptr_t CVEngineServer__RemoveEdict = r1vtable[22];
		uintptr_t CVEngineServer__PvAllocEntPrivateData = r1vtable[23];
		uintptr_t CVEngineServer__FreeEntPrivateData = r1vtable[24];
		uintptr_t CVEngineServer__SaveAllocMemory = r1vtable[25];
		uintptr_t CVEngineServer__SaveFreeMemory = r1vtable[26];
		uintptr_t CVEngineServer__FadeClientVolume = r1vtable[27];
		uintptr_t CVEngineServer__ServerCommand = r1vtable[28];
		uintptr_t CVEngineServer__CBuf_Execute = r1vtable[29];
		uintptr_t CVEngineServer__ClientCommand = r1vtable[30];
		uintptr_t CVEngineServer__LightStyle = r1vtable[31];
		uintptr_t CVEngineServer__StaticDecal = r1vtable[32];
		uintptr_t CVEngineServer__EntityMessageBeginEx = r1vtable[33];
		uintptr_t CVEngineServer__EntityMessageBegin = r1vtable[34];
		uintptr_t CVEngineServer__UserMessageBegin = r1vtable[35];
		uintptr_t CVEngineServer__MessageEnd = r1vtable[36];
		uintptr_t CVEngineServer__ClientPrintf = r1vtable[37];
		uintptr_t CVEngineServer__Con_NPrintf = r1vtable[38];
		uintptr_t CVEngineServer__Con_NXPrintf = r1vtable[39];
		uintptr_t CVEngineServer__UnkFunc1 = r1vtable[40];
		uintptr_t CVEngineServer__NumForEdictinfo = r1vtable[41];
		uintptr_t CVEngineServer__UnkFunc2 = r1vtable[42];
		uintptr_t CVEngineServer__CrosshairAngle = r1vtable[43];
		uintptr_t CVEngineServer__GetGameDir = r1vtable[44];
		uintptr_t CVEngineServer__CompareFileTime = r1vtable[45];
		uintptr_t CVEngineServer__LockNetworkStringTables = r1vtable[46];
		uintptr_t CVEngineServer__CreateFakeClient = r1vtable[47];
		uintptr_t CVEngineServer__GetClientConVarValue = r1vtable[48];
		uintptr_t CVEngineServer__ReplayReady = r1vtable[49];
		uintptr_t CVEngineServer__GetReplayFrame = r1vtable[50];
		uintptr_t CVEngineServer__UnkFunc4 = r1vtable[51];
		uintptr_t CVEngineServer__UnkFunc5 = r1vtable[52];
		uintptr_t CVEngineServer__UnkFunc6 = r1vtable[53];
		uintptr_t CVEngineServer__UnkFunc7 = r1vtable[54];
		uintptr_t CEngineClient__ParseFile = r1vtable[55];
		uintptr_t CEngineClient__CopyLocalFile = r1vtable[56];
		uintptr_t CVEngineServer__PlaybackTempEntity = r1vtable[57];
		uintptr_t CVEngineServer__UnkFunc9 = r1vtable[58];
		uintptr_t CVEngineServer__LoadGameState = r1vtable[59];
		uintptr_t CVEngineServer__LoadAdjacentEnts = r1vtable[60];
		uintptr_t CVEngineServer__ClearSaveDir = r1vtable[61];
		uintptr_t CVEngineServer__GetMapEntitiesString = r1vtable[62];
		uintptr_t CVEngineServer__GetPlaylistCount = r1vtable[63];
		uintptr_t CVEngineServer__GetUnknownPlaylistKV = r1vtable[64];
		uintptr_t CVEngineServer__GetPlaylistValPossible = r1vtable[65];
		uintptr_t CVEngineServer__GetPlaylistValPossibleAlt = r1vtable[66];
		uintptr_t CVEngineServer__GetUnknownPlaylistKV2 = r1vtable[67];
		uintptr_t CVEngineServer__GetUnknownPlaylistKV3 = r1vtable[68];
		uintptr_t CVEngineServer__UnknownMapSetup = r1vtable[69];
		uintptr_t CVEngineServer__UnknownMapSetup2 = r1vtable[70];
		uintptr_t CVEngineServer__UnknownMapSetup3 = r1vtable[71];
		uintptr_t CVEngineServer__UnknownMapSetup4 = r1vtable[72];
		uintptr_t CVEngineServer__UnknownMapSetup3_2 = r1vtable[73];
		uintptr_t CVEngineServer__UnknownGamemodeSetup2 = r1vtable[74];
		uintptr_t CVEngineServer__IsMatchmakingDedi = r1vtable[75];
		uintptr_t CVEngineServer__UnusedTimeFunc = r1vtable[76];
		uintptr_t CVEngineServer__UnkFunc11 = r1vtable[77];
		uintptr_t CVEngineServer__UnusedTimeFunc_2 = r1vtable[78];
		uintptr_t CVEngineServer__IsClientSearching = r1vtable[79];


		uintptr_t CVEngineServer__IsPrivateMatch = r1vtable[80];
		//uintptr_t CVEngineServer__IsCoop = r1vtable[80];
		uintptr_t CVEngineServer__TextMessageGet = r1vtable[81];
		uintptr_t CVEngineServer__UnkFunc12 = r1vtable[82];
		uintptr_t CVEngineServer__LogPrint = r1vtable[83];
		uintptr_t CVEngineServer__IsLogEnabled = r1vtable[84];
		uintptr_t CVEngineServer__IsWorldBrushValid = r1vtable[85];
		uintptr_t CVEngineServer__SolidMoved = r1vtable[86];
		uintptr_t CVEngineServer__TriggerMoved = r1vtable[87];
		uintptr_t CVEngineServer__CreateSpatialPartition = r1vtable[88];
		uintptr_t CVEngineServer__DestroySpatialPartition = r1vtable[89];
		uintptr_t CVEngineServer__UnkFunc13 = r1vtable[90];
		uintptr_t CVEngineServer__IsPaused = r1vtable[91];
		uintptr_t CVEngineServer__UnkFunc14 = r1vtable[92];
		uintptr_t CVEngineServer__UnkFunc15 = r1vtable[93];
		uintptr_t CVEngineServer__UnkFunc16 = r1vtable[94];
		uintptr_t CVEngineServer__UnkFunc17 = r1vtable[95];
		uintptr_t CVEngineServer__UnkFunc18 = r1vtable[96];
		uintptr_t CVEngineServer__UnkFunc19 = r1vtable[97];
		uintptr_t CVEngineServer__UnkFunc20 = r1vtable[98];
		uintptr_t CVEngineServer__UnkFunc21 = r1vtable[99];
		uintptr_t CVEngineServer__UnkFunc22 = r1vtable[100];
		uintptr_t CVEngineServer__UnkFunc23 = r1vtable[101];
		uintptr_t CVEngineServer__UnkFunc24 = r1vtable[102];
		uintptr_t CVEngineServer__UnkFunc25 = r1vtable[103];
		uintptr_t CVEngineServer__UnkFunc26 = r1vtable[104];
		uintptr_t CVEngineServer__UnkFunc27 = r1vtable[105];
		uintptr_t CVEngineServer__UnkFunc28 = r1vtable[106];
		uintptr_t CVEngineServer__UnkFunc29 = r1vtable[107];
		uintptr_t CVEngineServer__UnkFunc30 = r1vtable[108];
		uintptr_t CVEngineServer__UnkFunc31 = r1vtable[109];
		uintptr_t CVEngineServer__UnkFunc32 = r1vtable[110];
		uintptr_t CVEngineServer__UnkFunc33 = r1vtable[111];
		uintptr_t CVEngineServer__InsertServerCommand = r1vtable[113];
		uintptr_t CVEngineServer__UnkFunc35 = r1vtable[114];
		uintptr_t CVEngineServer__UnkFunc36 = r1vtable[115];
		uintptr_t CVEngineServer__UnkFunc37 = r1vtable[116];
		uintptr_t CVEngineServer__UnkFunc38 = r1vtable[117];
		uintptr_t CVEngineServer__UnkFunc39 = r1vtable[118];
		uintptr_t CVEngineServer__UnkFunc40 = r1vtable[119];
		uintptr_t CVEngineServer__UnkFunc41 = r1vtable[120];
		uintptr_t CVEngineServer__UnkFunc42 = r1vtable[121];
		uintptr_t CVEngineServer__UnkFunc43 = r1vtable[122];
		uintptr_t CVEngineServer__UnkFunc44 = r1vtable[123];
		uintptr_t CVEngineServer__AllocLevelStaticData = r1vtable[124];
		uintptr_t CVEngineServer__UnkFunc45 = r1vtable[125];
		uintptr_t CVEngineServer__UnkFunc46 = r1vtable[126];
		uintptr_t CVEngineServer__UnkFunc47 = r1vtable[127];
		uintptr_t CVEngineServer__Pause = r1vtable[128];
		uintptr_t CVEngineServer__UnkFunc48 = r1vtable[129];
		uintptr_t CVEngineServer__UnkFunc49 = r1vtable[130];
		uintptr_t CVEngineServer__UnkFunc50 = r1vtable[131];
		uintptr_t CVEngineServer__NullSub1 = r1vtable[132];
		uintptr_t CVEngineServer__UnkFunc51 = r1vtable[133];
		uintptr_t CVEngineServer__UnkFunc52 = r1vtable[134];
		uintptr_t CVEngineServer__MarkTeamsAsBalanced_On = r1vtable[135];
		uintptr_t CVEngineServer__MarkTeamsAsBalanced_Off = r1vtable[136];
		uintptr_t CVEngineServer__UnkFunc54 = r1vtable[137];
		uintptr_t CVEngineServer__UnkFunc55 = r1vtable[138];
		uintptr_t CVEngineServer__UnkFunc56 = r1vtable[139];
		uintptr_t CVEngineServer__UnkFunc57 = r1vtable[140];
		uintptr_t CVEngineServer__UnkFunc58 = r1vtable[141];
		uintptr_t CVEngineServer__UnkFunc59 = r1vtable[142];
		uintptr_t CVEngineServer__UnkFunc60 = r1vtable[143];
		uintptr_t CVEngineServer__UnkFunc61 = r1vtable[144];
		uintptr_t CVEngineServer__UnkFunc62 = r1vtable[145];
		uintptr_t CVEngineServer__UnkFunc = r1vtable[146];
		uintptr_t CVEngineServer__UnkFunc63 = r1vtable[147];
		uintptr_t CVEngineServer__UnkFunc64 = r1vtable[148];
		uintptr_t CVEngineServer__GetClientXUID = r1vtable[149];
		uintptr_t CVEngineServer__IsActiveApp = r1vtable[150];
		uintptr_t CVEngineServer__UnkFunc70 = r1vtable[151];
		uintptr_t CVEngineServer__Bandwidth_WriteBandwidthStatsToFile = r1vtable[152];
		uintptr_t CVEngineServer__UnkFunc71 = r1vtable[153];
		uintptr_t CVEngineServer__IsCheckpointMapLoad = r1vtable[154];
		uintptr_t CVEngineServer__IsDurangoDedi = r1vtable[155];
		uintptr_t CVEngineServer__UnkFunc53 = r1vtable[156];
		uintptr_t sub_18006E790 = r1vtable[157];
		uintptr_t sub_18006ECB0 = r1vtable[158];
		uintptr_t nullsub_285 = r1vtable[159];
		uintptr_t sub_18006ED00 = r1vtable[160];
		uintptr_t sub_18006ED90 = r1vtable[161];
		uintptr_t sub_18006EDA0 = r1vtable[162];
		uintptr_t sub_18006ED10 = r1vtable[163];
		uintptr_t sub_18006ED20 = r1vtable[164];
		uintptr_t sub_18006ED30 = r1vtable[165];
		uintptr_t sub_18006ED50 = r1vtable[166];
		uintptr_t sub_18006ED60 = r1vtable[167];
		uintptr_t sub_18006ED70 = r1vtable[168];
		uintptr_t sub_18006EDB0 = r1vtable[169];
		uintptr_t sub_18006EDC0 = r1vtable[170];
		uintptr_t sub_18006EDD0 = r1vtable[171];
		uintptr_t sub_18006EDE0 = r1vtable[172];
		uintptr_t sub_18006EDF0 = r1vtable[173];
		uintptr_t sub_18006EE00 = r1vtable[174];
		uintptr_t sub_18006EE10 = r1vtable[175];
		uintptr_t sub_18006EE20 = r1vtable[176];
		uintptr_t sub_18006EE30 = r1vtable[177];
		uintptr_t sub_18006EE40 = r1vtable[178];
		uintptr_t sub_18006EE50 = r1vtable[179];
		uintptr_t sub_18006EE60 = r1vtable[180];
		uintptr_t sub_18006EE80 = r1vtable[181];
		uintptr_t sub_18006EE90 = r1vtable[182];
		uintptr_t sub_18006EEA0 = r1vtable[183];
		uintptr_t sub_18006EEB0 = r1vtable[184];
		uintptr_t sub_18006E750 = r1vtable[185];
		uintptr_t CVEngineServer__UnkFunc77 = r1vtable[186];

#else
		uintptr_t CVEngineServer__ChangeLevel = r1vtable[0];
		uintptr_t CVEngineServer__IsMapValid = r1vtable[1];
		uintptr_t CVEngineServer__GetMapCRC = r1vtable[2];
		uintptr_t CVEngineServer__IsDedicatedServer = r1vtable[3];
		uintptr_t CVEngineServer__IsInEditMode = r1vtable[4];
		uintptr_t CVEngineServer__GetLaunchOptions = r1vtable[5];
		uintptr_t CVEngineServer__PrecacheModel = r1vtable[6];
		uintptr_t CVEngineServer__PrecacheDecal = r1vtable[7];
		uintptr_t CVEngineServer__PrecacheGeneric = r1vtable[8];
		uintptr_t CVEngineServer__IsModelPrecached = r1vtable[9];
		uintptr_t CVEngineServer__IsDecalPrecached = r1vtable[10];
		uintptr_t CVEngineServer__IsGenericPrecached = r1vtable[11];
		uintptr_t CVEngineServer__IsSimulating = r1vtable[12];
		uintptr_t CVEngineServer__GetPlayerUserId = r1vtable[13];
		uintptr_t CVEngineServer__GetPlayerNetworkIDString = r1vtable[14];
		uintptr_t CVEngineServer__IsUserIDInUse = r1vtable[15];
		uintptr_t CVEngineServer__GetLoadingProgressForUserID = r1vtable[16];
		uintptr_t CVEngineServer__GetEntityCount = r1vtable[17];
		uintptr_t CVEngineServer__GetPlayerNetInfo = r1vtable[18];
		uintptr_t CVEngineServer__IsClientValid = r1vtable[19];
		uintptr_t CVEngineServer__PlayerChangesTeams = r1vtable[20];
		uintptr_t CVEngineServer__RequestClientScreenshot = r1vtable[21];
		uintptr_t CVEngineServer__CreateEdict = r1vtable[22];
		uintptr_t CVEngineServer__RemoveEdict = r1vtable[23];
		uintptr_t CVEngineServer__PvAllocEntPrivateData = r1vtable[24];
		uintptr_t CVEngineServer__FreeEntPrivateData = r1vtable[25];
		uintptr_t CVEngineServer__SaveAllocMemory = r1vtable[26];
		uintptr_t CVEngineServer__SaveFreeMemory = r1vtable[27];
		uintptr_t CVEngineServer__FadeClientVolume = r1vtable[28];
		uintptr_t CVEngineServer__ServerCommand = r1vtable[29];
		uintptr_t CVEngineServer__CBuf_Execute = r1vtable[30];
		uintptr_t CVEngineServer__ClientCommand = r1vtable[31];
		uintptr_t CVEngineServer__LightStyle = r1vtable[32];
		uintptr_t CVEngineServer__StaticDecal = r1vtable[33];
		uintptr_t CVEngineServer__EntityMessageBeginEx = r1vtable[34];
		uintptr_t CVEngineServer__EntityMessageBegin = r1vtable[35];
		uintptr_t CVEngineServer__UserMessageBegin = r1vtable[36];
		uintptr_t CVEngineServer__MessageEnd = r1vtable[37];
		uintptr_t CVEngineServer__ClientPrintf = r1vtable[38];
		uintptr_t CVEngineServer__Con_NPrintf = r1vtable[39];
		uintptr_t CVEngineServer__Con_NXPrintf = r1vtable[40];
		uintptr_t CVEngineServer__UnkFunc1 = r1vtable[41];
		uintptr_t CVEngineServer__NumForEdictinfo = r1vtable[42];
		uintptr_t CVEngineServer__UnkFunc2 = r1vtable[43];
		uintptr_t CVEngineServer__UnkFunc3 = r1vtable[44];
		uintptr_t CVEngineServer__CrosshairAngle = r1vtable[45];
		uintptr_t CVEngineServer__GetGameDir = r1vtable[46];
		uintptr_t CVEngineServer__CompareFileTime = r1vtable[47];
		uintptr_t CVEngineServer__LockNetworkStringTables = r1vtable[48];
		uintptr_t CVEngineServer__CreateFakeClient = r1vtable[49];
		uintptr_t CVEngineServer__GetClientConVarValue = r1vtable[50];
		uintptr_t CVEngineServer__ReplayReady = r1vtable[51];
		uintptr_t CVEngineServer__GetReplayFrame = r1vtable[52];
		uintptr_t CVEngineServer__UnkFunc4 = r1vtable[53];
		uintptr_t CVEngineServer__UnkFunc5 = r1vtable[54];
		uintptr_t CVEngineServer__UnkFunc6 = r1vtable[55];
		uintptr_t CVEngineServer__UnkFunc7 = r1vtable[56];
		uintptr_t CVEngineServer__UnkFunc8 = r1vtable[57];
		uintptr_t CEngineClient__ParseFile = r1vtable[58];
		uintptr_t CEngineClient__CopyLocalFile = r1vtable[59];
		uintptr_t CVEngineServer__PlaybackTempEntity = r1vtable[60];
		uintptr_t CVEngineServer__UnkFunc9 = r1vtable[61];
		uintptr_t CVEngineServer__LoadGameState = r1vtable[62];
		uintptr_t CVEngineServer__LoadAdjacentEnts = r1vtable[63];
		uintptr_t CVEngineServer__ClearSaveDir = r1vtable[64];
		uintptr_t CVEngineServer__GetMapEntitiesString = r1vtable[65];
		uintptr_t CVEngineServer__GetPlaylistCount = r1vtable[66];
		uintptr_t CVEngineServer__GetUnknownPlaylistKV = r1vtable[67];
		uintptr_t CVEngineServer__GetPlaylistValPossible = r1vtable[68];
		uintptr_t CVEngineServer__GetPlaylistValPossibleAlt = r1vtable[69];
		uintptr_t CVEngineServer__AddPlaylistOverride = r1vtable[70];
		uintptr_t CVEngineServer__MarkPlaylistReadyForOverride = r1vtable[71];
		uintptr_t CVEngineServer__UnknownPlaylistSetup = r1vtable[72];
		uintptr_t CVEngineServer__GetUnknownPlaylistKV2 = r1vtable[73];
		uintptr_t CVEngineServer__GetUnknownPlaylistKV3 = r1vtable[74];
		uintptr_t CVEngineServer__GetUnknownPlaylistKV4 = r1vtable[75];
		uintptr_t CVEngineServer__UnknownMapSetup = r1vtable[76];
		uintptr_t CVEngineServer__UnknownMapSetup2 = r1vtable[77];
		uintptr_t CVEngineServer__UnknownMapSetup3 = r1vtable[78];
		uintptr_t CVEngineServer__UnknownMapSetup4 = r1vtable[79];
		uintptr_t CVEngineServer__UnknownGamemodeSetup = r1vtable[80];
		uintptr_t CVEngineServer__UnknownGamemodeSetup2 = r1vtable[81];
		uintptr_t CVEngineServer__IsMatchmakingDedi = r1vtable[82];
		uintptr_t CVEngineServer__UnusedTimeFunc = r1vtable[83];
		uintptr_t CVEngineServer__IsClientSearching = r1vtable[84];
		uintptr_t CVEngineServer__UnkFunc11 = r1vtable[85];
		uintptr_t CVEngineServer__IsPrivateMatch = r1vtable[86];
		uintptr_t CVEngineServer__IsCoop = r1vtable[87];
		uintptr_t CVEngineServer__GetSkillFlag_Unused = r1vtable[88];
		uintptr_t CVEngineServer__TextMessageGet = r1vtable[89];
		uintptr_t CVEngineServer__UnkFunc12 = r1vtable[90];
		uintptr_t CVEngineServer__LogPrint = r1vtable[91];
		uintptr_t CVEngineServer__IsLogEnabled = r1vtable[92];
		uintptr_t CVEngineServer__IsWorldBrushValid = r1vtable[93];
		uintptr_t CVEngineServer__SolidMoved = r1vtable[94];
		uintptr_t CVEngineServer__TriggerMoved = r1vtable[95];
		uintptr_t CVEngineServer__CreateSpatialPartition = r1vtable[96];
		uintptr_t CVEngineServer__DestroySpatialPartition = r1vtable[97];
		uintptr_t CVEngineServer__UnkFunc13 = r1vtable[98];
		uintptr_t CVEngineServer__IsPaused = r1vtable[99];
		uintptr_t CVEngineServer__UnkFunc14 = r1vtable[100];
		uintptr_t CVEngineServer__UnkFunc15 = r1vtable[101];
		uintptr_t CVEngineServer__UnkFunc16 = r1vtable[102];
		uintptr_t CVEngineServer__UnkFunc17 = r1vtable[103];
		uintptr_t CVEngineServer__UnkFunc18 = r1vtable[104];
		uintptr_t CVEngineServer__UnkFunc19 = r1vtable[105];
		uintptr_t CVEngineServer__UnkFunc20 = r1vtable[106];
		uintptr_t CVEngineServer__UnkFunc21 = r1vtable[107];
		uintptr_t CVEngineServer__UnkFunc22 = r1vtable[108];
		uintptr_t CVEngineServer__UnkFunc23 = r1vtable[109];
		uintptr_t CVEngineServer__UnkFunc24 = r1vtable[110];
		uintptr_t CVEngineServer__UnkFunc25 = r1vtable[111];
		uintptr_t CVEngineServer__UnkFunc26 = r1vtable[112];
		uintptr_t CVEngineServer__UnkFunc27 = r1vtable[113];
		uintptr_t CVEngineServer__UnkFunc28 = r1vtable[114];
		uintptr_t CVEngineServer__UnkFunc29 = r1vtable[115];
		uintptr_t CVEngineServer__UnkFunc30 = r1vtable[116];
		uintptr_t CVEngineServer__UnkFunc31 = r1vtable[117];
		uintptr_t CVEngineServer__UnkFunc32 = r1vtable[118];
		uintptr_t CVEngineServer__UnkFunc33 = r1vtable[119];
		uintptr_t CVEngineServer__UnkFunc34 = r1vtable[120];
		uintptr_t CVEngineServer__InsertServerCommand = r1vtable[121];
		uintptr_t CVEngineServer__UnkFunc35 = r1vtable[122];
		uintptr_t CVEngineServer__UnkFunc36 = r1vtable[123];
		uintptr_t CVEngineServer__UnkFunc37 = r1vtable[124];
		uintptr_t CVEngineServer__UnkFunc38 = r1vtable[125];
		uintptr_t CVEngineServer__UnkFunc39 = r1vtable[126];
		uintptr_t CVEngineServer__UnkFunc40 = r1vtable[127];
		uintptr_t CVEngineServer__UnkFunc41 = r1vtable[128];
		uintptr_t CVEngineServer__UnkFunc42 = r1vtable[129];
		uintptr_t CVEngineServer__UnkFunc43 = r1vtable[130];
		uintptr_t CVEngineServer__UnkFunc44 = r1vtable[131];
		uintptr_t CVEngineServer__AllocLevelStaticData = r1vtable[132];
		uintptr_t CVEngineServer__UnkFunc45 = r1vtable[133];
		uintptr_t CVEngineServer__UnkFunc46 = r1vtable[134];
		uintptr_t CVEngineServer__UnkFunc47 = r1vtable[135];
		uintptr_t CVEngineServer__Pause = r1vtable[136];
		uintptr_t CVEngineServer__UnkFunc48 = r1vtable[137];
		uintptr_t CVEngineServer__UnkFunc49 = r1vtable[138];
		uintptr_t CVEngineServer__UnkFunc50 = r1vtable[139];
		uintptr_t CVEngineServer__NullSub1 = r1vtable[140];
		uintptr_t CVEngineServer__UnkFunc51 = r1vtable[141];
		uintptr_t CVEngineServer__UnkFunc52 = r1vtable[142];
		uintptr_t CVEngineServer__MarkTeamsAsBalanced_On = r1vtable[143];
		uintptr_t CVEngineServer__MarkTeamsAsBalanced_Off = r1vtable[144];
		uintptr_t CVEngineServer__UnkFunc53 = r1vtable[145];
		uintptr_t CVEngineServer__UnkFunc54 = r1vtable[146];
		uintptr_t CVEngineServer__UnkFunc55 = r1vtable[147];
		uintptr_t CVEngineServer__UnkFunc56 = r1vtable[148];
		uintptr_t CVEngineServer__UnkFunc57 = r1vtable[149];
		uintptr_t CVEngineServer__UnkFunc58 = r1vtable[150];
		uintptr_t CVEngineServer__UnkFunc59 = r1vtable[151];
		uintptr_t CVEngineServer__UnkFunc60 = r1vtable[152];
		uintptr_t CVEngineServer__UnkFunc61 = r1vtable[153];
		uintptr_t CVEngineServer__UnkFunc62 = r1vtable[154];
		uintptr_t CVEngineServer__UnkFunc63 = r1vtable[155];
		uintptr_t CVEngineServer__NullSub2 = r1vtable[156];
		uintptr_t CVEngineServer__UnkFunc64 = r1vtable[157];
		uintptr_t CVEngineServer__NullSub3 = r1vtable[158];
		uintptr_t CVEngineServer__NullSub4 = r1vtable[159];
		uintptr_t CVEngineServer__UnkFunc65 = r1vtable[160];
		uintptr_t CVEngineServer__UnkFunc66 = r1vtable[161];
		uintptr_t CVEngineServer__UnkFunc67 = r1vtable[162];
		uintptr_t CVEngineServer__UnkFunc68 = r1vtable[163];
		uintptr_t CVEngineServer__UnkFunc69 = r1vtable[164];
		uintptr_t CVEngineServer__UnkFunc_R1EXCLUSIVE = r1vtable[165];
		uintptr_t CVEngineServer__FuncThatReturnsFF_12 = r1vtable[166];
		uintptr_t CVEngineServer__FuncThatReturnsFF_11 = r1vtable[167];
		uintptr_t CVEngineServer__FuncThatReturnsFF_10 = r1vtable[168];
		uintptr_t CVEngineServer__FuncThatReturnsFF_9 = r1vtable[169];
		uintptr_t CVEngineServer__FuncThatReturnsFF_8 = r1vtable[170];
		uintptr_t CVEngineServer__FuncThatReturnsFF_7 = r1vtable[171];
		uintptr_t CVEngineServer__FuncThatReturnsFF_6 = r1vtable[172];
		uintptr_t CVEngineServer__FuncThatReturnsFF_5 = r1vtable[173];
		uintptr_t CVEngineServer__FuncThatReturnsFF_4 = r1vtable[174];
		uintptr_t CVEngineServer__FuncThatReturnsFF_3 = r1vtable[175];
		uintptr_t CVEngineServer__FuncThatReturnsFF_2 = r1vtable[176];
		uintptr_t CVEngineServer__FuncThatReturnsFF_1 = r1vtable[177];
		uintptr_t CVEngineServer__GetClientXUID = r1vtable[178];
		uintptr_t CVEngineServer__IsActiveApp = r1vtable[179];
		uintptr_t CVEngineServer__UnkFunc70 = r1vtable[180];
		uintptr_t CVEngineServer__Bandwidth_WriteBandwidthStatsToFile = r1vtable[181];
		uintptr_t CVEngineServer__UnkFunc71 = r1vtable[182];
		uintptr_t CVEngineServer__IsCheckpointMapLoad = r1vtable[183];
		uintptr_t CVEngineServer__UnkFunc72 = r1vtable[184];
		uintptr_t CVEngineServer__UnkFunc73 = r1vtable[185];
		uintptr_t CVEngineServer__UnkFunc74 = r1vtable[186];
		uintptr_t CVEngineServer__UnkFunc75 = r1vtable[187];
		uintptr_t CVEngineServer__UnkFunc76 = r1vtable[188];
		uintptr_t CVEngineServer__NullSub5 = r1vtable[189];
		uintptr_t CVEngineServer__NullSub6 = r1vtable[190];
		uintptr_t sub_1800FE440 = r1vtable[191];
		uintptr_t sub_1800FE450 = r1vtable[192];
		uintptr_t sub_1800FE3C0 = r1vtable[193];
		uintptr_t sub_1800FE3D0 = r1vtable[194];
		uintptr_t sub_1800FE3E0 = r1vtable[195];
		uintptr_t sub_1800FE400 = r1vtable[196];
		uintptr_t sub_1800FE410 = r1vtable[197];
		uintptr_t sub_1800FE420 = r1vtable[198];
		uintptr_t sub_1800FE460 = r1vtable[199];
		uintptr_t sub_1800FE470 = r1vtable[200];
		uintptr_t sub_1800FE480 = r1vtable[201];
		uintptr_t sub_1800FE490 = r1vtable[202];
		uintptr_t sub_1800FE4A0 = r1vtable[203];
		uintptr_t sub_1800FE4B0 = r1vtable[204];
		uintptr_t sub_1800FE4C0 = r1vtable[205];
		uintptr_t sub_1800FE4D0 = r1vtable[206];
		uintptr_t sub_1800FE4E0 = r1vtable[207];
		uintptr_t sub_1800FE4F0 = r1vtable[208];
		uintptr_t sub_1800FE500 = r1vtable[209];
		uintptr_t sub_1800FE510 = r1vtable[210];
		uintptr_t sub_1800FE530 = r1vtable[211];
		uintptr_t sub_1800FE540 = r1vtable[212];
		uintptr_t sub_1800FE550 = r1vtable[213];
		uintptr_t sub_1800FE560 = r1vtable[214];
		uintptr_t CVEngineServer__UpdateClientHashtag = r1vtable[215];
		uintptr_t CVEngineServer__UnkFunc77 = r1vtable[216];
		uintptr_t CVEngineServer__UnkFunc78 = r1vtable[217];
		uintptr_t CVEngineServer__UnkFunc79 = r1vtable[218];
		uintptr_t CVEngineServer__UnkFunc80 = r1vtable[219];
		uintptr_t CVEngineServer__UnkFunc81 = r1vtable[220];
		uintptr_t CVEngineServer__UnkFunc82 = r1vtable[221];
#endif
		static uintptr_t r1ovtable[] = {
			CVEngineServer__NullSub1,
			CVEngineServer__ChangeLevel,
			CVEngineServer__IsMapValid,
			CVEngineServer__GetMapCRC,
			CVEngineServer__IsDedicatedServer,
			CVEngineServer__IsInEditMode,
			CVEngineServer__GetLaunchOptions,
			(uintptr_t)(&CVEngineServer__GetLocalNETConfig_TFO),
			(uintptr_t)(&CVEngineServer__GetNETConfig_TFO),
			(uintptr_t)(&CVEngineServer__GetLocalNETGetPacket_TFO),
			(uintptr_t)(&CVEngineServer__GetNETGetPacket_TFO),
			CVEngineServer__PrecacheModel,
			CVEngineServer__PrecacheDecal,
			CVEngineServer__PrecacheGeneric,
			CVEngineServer__IsModelPrecached,
			CVEngineServer__IsDecalPrecached,
			CVEngineServer__IsGenericPrecached,
			CVEngineServer__IsSimulating,
			CVEngineServer__GetPlayerUserId,
			CVEngineServer__GetPlayerUserId,
			CVEngineServer__GetPlayerNetworkIDString,
			CVEngineServer__IsUserIDInUse,
			CVEngineServer__GetLoadingProgressForUserID,
			CVEngineServer__GetEntityCount,
			CVEngineServer__GetPlayerNetInfo,
			CVEngineServer__IsClientValid,
			CVEngineServer__PlayerChangesTeams,
#ifdef DEDICATED
			CVEngineServer__NullSub1,
#else
			CVEngineServer__RequestClientScreenshot,
#endif
			CVEngineServer__CreateEdict,
			CVEngineServer__RemoveEdict,
			CVEngineServer__PvAllocEntPrivateData,
			CVEngineServer__FreeEntPrivateData,
			CVEngineServer__SaveAllocMemory,
			CVEngineServer__SaveFreeMemory,
			CVEngineServer__FadeClientVolume,
			CVEngineServer__ServerCommand,
			CVEngineServer__CBuf_Execute,
			CVEngineServer__ClientCommand,
			CVEngineServer__LightStyle,
			CVEngineServer__StaticDecal,
			CVEngineServer__EntityMessageBeginEx,
			CVEngineServer__EntityMessageBegin,
			CVEngineServer__UserMessageBegin,
			CVEngineServer__MessageEnd,
			CVEngineServer__ClientPrintf,
			CVEngineServer__Con_NPrintf,
			CVEngineServer__Con_NXPrintf,
			CVEngineServer__UnkFunc1,
			CVEngineServer__NumForEdictinfo,
			CVEngineServer__UnkFunc2,
#ifdef DEDICATED
			CVEngineServer__NullSub1,
#else
			CVEngineServer__UnkFunc3,
#endif
			CVEngineServer__CrosshairAngle,
			CVEngineServer__GetGameDir,
			CVEngineServer__CompareFileTime,
			CVEngineServer__LockNetworkStringTables,
			CVEngineServer__CreateFakeClient,
			CVEngineServer__GetClientConVarValue,
			CVEngineServer__ReplayReady,
			CVEngineServer__GetReplayFrame,
			CVEngineServer__UnkFunc4,
			CVEngineServer__UnkFunc5,
			CVEngineServer__UnkFunc6,
			CVEngineServer__UnkFunc7,
#ifdef DEDICATED
			CVEngineServer__NullSub1,
#else
			CVEngineServer__UnkFunc8,
#endif
			CEngineClient__ParseFile,
			CEngineClient__CopyLocalFile,
			CVEngineServer__PlaybackTempEntity,
			CVEngineServer__UnkFunc9,
			CVEngineServer__LoadGameState,
			CVEngineServer__LoadAdjacentEnts,
			CVEngineServer__ClearSaveDir,
			CVEngineServer__GetMapEntitiesString,
			CVEngineServer__GetPlaylistCount,
			CVEngineServer__GetUnknownPlaylistKV,
			CVEngineServer__GetPlaylistValPossible,
			CVEngineServer__GetPlaylistValPossibleAlt,
#ifdef DEDICATED
			CVEngineServer__NullSub1,
			CVEngineServer__NullSub1,
			CVEngineServer__NullSub1,
#else
			CVEngineServer__AddPlaylistOverride,
			CVEngineServer__MarkPlaylistReadyForOverride,
			CVEngineServer__UnknownPlaylistSetup,
#endif

			CVEngineServer__GetUnknownPlaylistKV2,
			CVEngineServer__GetUnknownPlaylistKV3,
#ifdef DEDICATED
			CVEngineServer__NullSub1,
#else
			CVEngineServer__GetUnknownPlaylistKV4,
#endif
			CVEngineServer__UnknownMapSetup,
			CVEngineServer__UnknownMapSetup2,
			CVEngineServer__UnknownMapSetup3,
			CVEngineServer__UnknownMapSetup4,
#ifdef DEDICATED
			CVEngineServer__NullSub1,
#else
			CVEngineServer__UnknownGamemodeSetup,
#endif
			CVEngineServer__UnknownGamemodeSetup2,
			CVEngineServer__IsMatchmakingDedi,
			CVEngineServer__UnusedTimeFunc,
			CVEngineServer__IsClientSearching,
			CVEngineServer__UnkFunc11,
			CVEngineServer__IsPrivateMatch,
#ifdef DEDICATED
			CVEngineServer__NullSub1,
			CVEngineServer__NullSub1,
#else
			CVEngineServer__IsCoop,
			CVEngineServer__GetSkillFlag_Unused,
#endif
			CVEngineServer__TextMessageGet,
			CVEngineServer__UnkFunc12,
			CVEngineServer__LogPrint,
			CVEngineServer__IsLogEnabled,
			CVEngineServer__IsWorldBrushValid,
			CVEngineServer__SolidMoved,
			CVEngineServer__TriggerMoved,
			CVEngineServer__CreateSpatialPartition,
			CVEngineServer__DestroySpatialPartition,
			CVEngineServer__UnkFunc13,
			CVEngineServer__IsPaused,
			CVEngineServer__UnkFunc14,
			CVEngineServer__UnkFunc15,
			CVEngineServer__UnkFunc16,
			CVEngineServer__UnkFunc17,
			CVEngineServer__UnkFunc18,
			CVEngineServer__UnkFunc19,
			CVEngineServer__UnkFunc20,
			CVEngineServer__UnkFunc21,
			CVEngineServer__UnkFunc22,
			CVEngineServer__UnkFunc23,
			CVEngineServer__UnkFunc24,
			CVEngineServer__UnkFunc25,
			CVEngineServer__UnkFunc26,
			CVEngineServer__UnkFunc27,
			CVEngineServer__UnkFunc28,
			CVEngineServer__UnkFunc29,
			CVEngineServer__UnkFunc30,
			CVEngineServer__UnkFunc31,
			CVEngineServer__UnkFunc32,
			CVEngineServer__UnkFunc33,
#ifdef DEDICATED
			CVEngineServer__NullSub1,
#else
			CVEngineServer__UnkFunc34,
#endif
			CVEngineServer__InsertServerCommand,
			CVEngineServer__UnkFunc35,
			CVEngineServer__UnkFunc36,
			CVEngineServer__UnkFunc37,
			CVEngineServer__UnkFunc38,
			CVEngineServer__UnkFunc39,
			CVEngineServer__UnkFunc40,
			CVEngineServer__UnkFunc41,
			CVEngineServer__UnkFunc42,
			CVEngineServer__UnkFunc43,
			CVEngineServer__UnkFunc44,
			CVEngineServer__AllocLevelStaticData,
			CVEngineServer__UnkFunc45,
			CVEngineServer__UnkFunc46,
			CVEngineServer__UnkFunc47,
			CVEngineServer__Pause,
			CVEngineServer__UnkFunc48,
			CVEngineServer__UnkFunc49,
			CVEngineServer__UnkFunc50,
			CVEngineServer__NullSub1,
			CVEngineServer__UnkFunc51,
			CVEngineServer__UnkFunc52,
			CVEngineServer__MarkTeamsAsBalanced_On,
			CVEngineServer__MarkTeamsAsBalanced_Off,
			CVEngineServer__UnkFunc53,
			CVEngineServer__UnkFunc54,
			CVEngineServer__UnkFunc55,
			CVEngineServer__UnkFunc56,
			CVEngineServer__UnkFunc57,
			CVEngineServer__UnkFunc58,
			CVEngineServer__UnkFunc59,
			CVEngineServer__UnkFunc60,
			CVEngineServer__UnkFunc61,
			CVEngineServer__UnkFunc62,
			CVEngineServer__UnkFunc63,
#ifdef DEDICATED
			CVEngineServer__NullSub1,
			CVEngineServer__UnkFunc64,
			CVEngineServer__NullSub1,
			CVEngineServer__NullSub1,
			CVEngineServer__NullSub1,
			CVEngineServer__NullSub1,
			CVEngineServer__NullSub1,
			CVEngineServer__NullSub1,
			CVEngineServer__NullSub1,
			(uintptr_t)(&CVEngineServer__FuncThatReturnsFF_Stub),
			(uintptr_t)(&CVEngineServer__FuncThatReturnsFF_Stub),
			(uintptr_t)(&CVEngineServer__FuncThatReturnsFF_Stub),
			(uintptr_t)(&CVEngineServer__FuncThatReturnsFF_Stub),
			(uintptr_t)(&CVEngineServer__FuncThatReturnsFF_Stub),
			(uintptr_t)(&CVEngineServer__FuncThatReturnsFF_Stub),
			(uintptr_t)(&CVEngineServer__FuncThatReturnsFF_Stub),
			(uintptr_t)(&CVEngineServer__FuncThatReturnsFF_Stub),
			(uintptr_t)(&CVEngineServer__FuncThatReturnsFF_Stub),
			(uintptr_t)(&CVEngineServer__FuncThatReturnsFF_Stub),
			(uintptr_t)(&CVEngineServer__FuncThatReturnsFF_Stub),
			(uintptr_t)(&CVEngineServer__FuncThatReturnsFF_Stub),
#else
			CVEngineServer__NullSub2,
			CVEngineServer__UnkFunc64,
			CVEngineServer__NullSub3,
			CVEngineServer__NullSub4,
			CVEngineServer__UnkFunc65,
			CVEngineServer__UnkFunc66,
			CVEngineServer__UnkFunc67,
			CVEngineServer__UnkFunc68,
			CVEngineServer__UnkFunc69,
			CVEngineServer__FuncThatReturnsFF_12,
			CVEngineServer__FuncThatReturnsFF_11,
			CVEngineServer__FuncThatReturnsFF_10,
			CVEngineServer__FuncThatReturnsFF_9,
			CVEngineServer__FuncThatReturnsFF_8,
			CVEngineServer__FuncThatReturnsFF_7,
			CVEngineServer__FuncThatReturnsFF_6,
			CVEngineServer__FuncThatReturnsFF_5,
			CVEngineServer__FuncThatReturnsFF_4,
			CVEngineServer__FuncThatReturnsFF_3,
			CVEngineServer__FuncThatReturnsFF_2,
			CVEngineServer__FuncThatReturnsFF_1,
#endif
			CVEngineServer__GetClientXUID,
			CVEngineServer__IsActiveApp,
			CVEngineServer__UnkFunc70,
			CVEngineServer__Bandwidth_WriteBandwidthStatsToFile,
			CVEngineServer__UnkFunc71,
			CVEngineServer__IsCheckpointMapLoad,
#ifdef DEDICATED
			CVEngineServer__NullSub1,
			CVEngineServer__NullSub1,
			CVEngineServer__NullSub1,
			CVEngineServer__NullSub1,
			CVEngineServer__NullSub1,
			CVEngineServer__NullSub1,
			CVEngineServer__NullSub1,
			CVEngineServer__NullSub1,
			CVEngineServer__UnkFunc77,
			CVEngineServer__NullSub1,
			CVEngineServer__NullSub1,
			CVEngineServer__NullSub1,
			CVEngineServer__NullSub1,
			CVEngineServer__NullSub1
#else
			CVEngineServer__UnkFunc72,
			CVEngineServer__UnkFunc73,
			CVEngineServer__UnkFunc74,
			CVEngineServer__UnkFunc75,
			CVEngineServer__UnkFunc76,
			CVEngineServer__NullSub5,
			CVEngineServer__NullSub6,
			CVEngineServer__UpdateClientHashtag,
			CVEngineServer__UnkFunc77,
			CVEngineServer__UnkFunc78,
			CVEngineServer__UnkFunc79,
			CVEngineServer__UnkFunc80,
			CVEngineServer__UnkFunc81,
			CVEngineServer__UnkFunc82
#endif
		};

		static void* whatever2 = &r1ovtable;
		return &whatever2;
	}
	if (!strcmp(pName, "VFileSystem017")) {
		std::cout << "wrapping VFileSystem017" << std::endl;

		uintptr_t* r1vtable = *(uintptr_t**)oFileSystemFactory(pName, pReturnCode);
		fsinterface = (uintptr_t)oFileSystemFactory(pName, pReturnCode);
		uintptr_t oCBaseFileSystem__Connect = r1vtable[0];
		uintptr_t oCBaseFileSystem__Disconnect = r1vtable[1];
		uintptr_t oCFileSystem_Stdio__QueryInterface = r1vtable[2];
		uintptr_t oCBaseFileSystem__Init = r1vtable[3];
		uintptr_t oCBaseFileSystem__Shutdown = r1vtable[4];
		uintptr_t oCBaseFileSystem__GetDependencies = r1vtable[5];
		uintptr_t oCBaseFileSystem__GetTier = r1vtable[6];
		uintptr_t oCBaseFileSystem__Reconnect = r1vtable[7];
		uintptr_t osub_180023F80 = r1vtable[8];
		uintptr_t osub_180023F90 = r1vtable[9];
		uintptr_t oCFileSystem_Stdio__AddSearchPath = r1vtable[10];
		uintptr_t oCBaseFileSystem__RemoveSearchPath = r1vtable[11];
		uintptr_t oCBaseFileSystem__RemoveAllSearchPaths = r1vtable[12];
		uintptr_t oCBaseFileSystem__RemoveSearchPaths = r1vtable[13];
		uintptr_t oCBaseFileSystem__MarkPathIDByRequestOnly = r1vtable[14];
		uintptr_t oCBaseFileSystem__RelativePathToFullPath = r1vtable[15];
		uintptr_t oCBaseFileSystem__GetSearchPath = r1vtable[16];
		uintptr_t oCBaseFileSystem__AddPackFile = r1vtable[17];
		uintptr_t oCBaseFileSystem__RemoveFile = r1vtable[18];
		uintptr_t oCBaseFileSystem__RenameFile = r1vtable[19];
		uintptr_t oCBaseFileSystem__CreateDirHierarchy = r1vtable[20];
		uintptr_t oCBaseFileSystem__IsDirectory = r1vtable[21];
		uintptr_t oCBaseFileSystem__FileTimeToString = r1vtable[22];
		uintptr_t oCFileSystem_Stdio__SetBufferSize = r1vtable[23];
		uintptr_t oCFileSystem_Stdio__IsOK = r1vtable[24];
		uintptr_t oCFileSystem_Stdio__EndOfLine = r1vtable[25];
		uintptr_t oCFileSystem_Stdio__ReadLine = r1vtable[26];
		uintptr_t oCBaseFileSystem__FPrintf = r1vtable[27];
		uintptr_t oCBaseFileSystem__LoadModule = r1vtable[28];
		uintptr_t oCBaseFileSystem__UnloadModule = r1vtable[29];
		uintptr_t oCBaseFileSystem__FindFirst = r1vtable[30];
		uintptr_t oCBaseFileSystem__FindNext = r1vtable[31];
		uintptr_t oCBaseFileSystem__FindIsDirectory = r1vtable[32];
		uintptr_t oCBaseFileSystem__FindClose = r1vtable[33];
		uintptr_t oCBaseFileSystem__FindFirstEx = r1vtable[34];
		uintptr_t oCBaseFileSystem__FindFileAbsoluteList = r1vtable[35];
		uintptr_t oCBaseFileSystem__GetLocalPath = r1vtable[36];
		uintptr_t oCBaseFileSystem__FullPathToRelativePath = r1vtable[37];
		uintptr_t oCBaseFileSystem__GetCurrentDirectory = r1vtable[38];
		uintptr_t oCBaseFileSystem__FindOrAddFileName = r1vtable[39];
		uintptr_t oCBaseFileSystem__String = r1vtable[40];
		uintptr_t oCBaseFileSystem__AsyncReadMultiple = r1vtable[41];
		uintptr_t oCBaseFileSystem__AsyncAppend = r1vtable[42];
		uintptr_t oCBaseFileSystem__AsyncAppendFile = r1vtable[43];
		uintptr_t oCBaseFileSystem__AsyncFinishAll = r1vtable[44];
		uintptr_t oCBaseFileSystem__AsyncFinishAllWrites = r1vtable[45];
		uintptr_t oCBaseFileSystem__AsyncFlush = r1vtable[46];
		uintptr_t oCBaseFileSystem__AsyncSuspend = r1vtable[47];
		uintptr_t oCBaseFileSystem__AsyncResume = r1vtable[48];
		uintptr_t oCBaseFileSystem__AsyncBeginRead = r1vtable[49];
		uintptr_t oCBaseFileSystem__AsyncEndRead = r1vtable[50];
		uintptr_t oCBaseFileSystem__AsyncFinish = r1vtable[51];
		uintptr_t oCBaseFileSystem__AsyncGetResult = r1vtable[52];
		uintptr_t oCBaseFileSystem__AsyncAbort = r1vtable[53];
		uintptr_t oCBaseFileSystem__AsyncStatus = r1vtable[54];
		uintptr_t oCBaseFileSystem__AsyncSetPriority = r1vtable[55];
		uintptr_t oCBaseFileSystem__AsyncAddRef = r1vtable[56];
		uintptr_t oCBaseFileSystem__AsyncRelease = r1vtable[57];
		uintptr_t osub_180024450 = r1vtable[58];
		uintptr_t osub_180024460 = r1vtable[59];
		uintptr_t onullsub_96 = r1vtable[60];
		uintptr_t osub_180024490 = r1vtable[61];
		uintptr_t osub_180024440 = r1vtable[62];
		uintptr_t onullsub_97 = r1vtable[63];
		uintptr_t osub_180009BE0 = r1vtable[64];
		uintptr_t osub_18000F6A0 = r1vtable[65];
		uintptr_t osub_180002CA0 = r1vtable[66];
		uintptr_t osub_180002CB0 = r1vtable[67];
		uintptr_t osub_1800154F0 = r1vtable[68];
		uintptr_t osub_180015550 = r1vtable[69];
		uintptr_t osub_180015420 = r1vtable[70];
		uintptr_t osub_180015480 = r1vtable[71];
		uintptr_t oCBaseFileSystem__RemoveLoggingFunc = r1vtable[72];
		uintptr_t oCBaseFileSystem__GetFilesystemStatistics = r1vtable[73];
		uintptr_t oCFileSystem_Stdio__OpenEx = r1vtable[74];
		uintptr_t osub_18000A5D0 = r1vtable[75];
		uintptr_t osub_1800052A0 = r1vtable[76];
		uintptr_t osub_180002F10 = r1vtable[77];
		uintptr_t osub_18000A690 = r1vtable[78];
		uintptr_t osub_18000A6F0 = r1vtable[79];
		uintptr_t osub_1800057A0 = r1vtable[80];
		uintptr_t osub_180002960 = r1vtable[81];
		uintptr_t osub_180020110 = r1vtable[82];
		uintptr_t osub_180020230 = r1vtable[83];
		uintptr_t osub_180023660 = r1vtable[84];
		uintptr_t osub_1800204A0 = r1vtable[85];
		uintptr_t osub_180002F40 = r1vtable[86];
		uintptr_t osub_180004F00 = r1vtable[87];
		uintptr_t osub_180024020 = r1vtable[88];
		uintptr_t osub_180024AF0 = r1vtable[89];
		uintptr_t osub_180024110 = r1vtable[90];
		uintptr_t osub_180002580 = r1vtable[91];
		uintptr_t osub_180002560 = r1vtable[92];
		uintptr_t osub_18000A070 = r1vtable[93];
		uintptr_t osub_180009E80 = r1vtable[94];
		uintptr_t osub_180009C20 = r1vtable[95];
		uintptr_t osub_1800022F0 = r1vtable[96];
		uintptr_t osub_180002330 = r1vtable[97];
		uintptr_t osub_180009CF0 = r1vtable[98];
		uintptr_t osub_180002340 = r1vtable[99];
		uintptr_t osub_180002320 = r1vtable[100];
		uintptr_t osub_180009E00 = r1vtable[101];
		uintptr_t osub_180009F20 = r1vtable[102];
		uintptr_t osub_180009EA0 = r1vtable[103];
		uintptr_t osub_180009E50 = r1vtable[104];
		uintptr_t osub_180009FC0 = r1vtable[105];
		uintptr_t osub_180004E80 = r1vtable[106];
		uintptr_t osub_18000A000 = r1vtable[107];
		uintptr_t osub_180014350 = r1vtable[108];
		uintptr_t osub_18000F5B0 = r1vtable[109];
		uintptr_t osub_180002590 = r1vtable[110];
		uintptr_t osub_1800025D0 = r1vtable[111];
		uintptr_t osub_1800025E0 = r1vtable[112];
		uintptr_t oCFileSystem_Stdio__LoadVPKForMap = r1vtable[113];
		uintptr_t oCFileSystem_Stdio__UnkFunc1 = r1vtable[114];
		uintptr_t oCFileSystem_Stdio__WeirdFuncThatJustDerefsRDX = r1vtable[115];
		uintptr_t oCFileSystem_Stdio__GetPathTime = r1vtable[116];
		uintptr_t oCFileSystem_Stdio__GetFSConstructedFlag = r1vtable[117];
		uintptr_t oCFileSystem_Stdio__EnableWhitelistFileTracking = r1vtable[118];
		uintptr_t osub_18000A750 = r1vtable[119];
		uintptr_t osub_180002B20 = r1vtable[120];
		uintptr_t osub_18001DC30 = r1vtable[121];
		uintptr_t osub_180002B30 = r1vtable[122];
		uintptr_t osub_180002BA0 = r1vtable[123];
		uintptr_t osub_180002BB0 = r1vtable[124];
		uintptr_t osub_180002BC0 = r1vtable[125];
		uintptr_t osub_180002290 = r1vtable[126];
		uintptr_t osub_18001CCD0 = r1vtable[127];
		uintptr_t osub_18001CCE0 = r1vtable[128];
		uintptr_t osub_18001CCF0 = r1vtable[129];
		uintptr_t osub_18001CD00 = r1vtable[130];
		uintptr_t osub_180014520 = r1vtable[131];
		uintptr_t osub_180002650 = r1vtable[132];
		uintptr_t osub_18001CD10 = r1vtable[133];
		uintptr_t osub_180016250 = r1vtable[134];
		uintptr_t osub_18000F0D0 = r1vtable[135];
		uintptr_t osub_1800139F0 = r1vtable[136];
		uintptr_t osub_180016570 = r1vtable[137];
		uintptr_t onullsub_86 = r1vtable[138];
		uintptr_t osub_18000AEC0 = r1vtable[139];
		uintptr_t osub_180003320 = r1vtable[140];
		uintptr_t osub_18000AF50 = r1vtable[141];
		uintptr_t osub_18000AF60 = r1vtable[142];
		uintptr_t osub_180005D00 = r1vtable[143];
		uintptr_t osub_18000AF70 = r1vtable[144];
		uintptr_t osub_18001B130 = r1vtable[145];
		uintptr_t osub_18000AF80 = r1vtable[146];
		uintptr_t osub_1800034D0 = r1vtable[147];
		uintptr_t osub_180017180 = r1vtable[148];
		uintptr_t osub_180003550 = r1vtable[149];
		uintptr_t osub_1800250D0 = r1vtable[150];
		uintptr_t osub_1800241B0 = r1vtable[151];
		uintptr_t osub_1800241C0 = r1vtable[152];
		uintptr_t osub_1800241F0 = r1vtable[153];
		uintptr_t osub_180024240 = r1vtable[154];
		uintptr_t osub_180024250 = r1vtable[155];
		uintptr_t osub_180024260 = r1vtable[156];
		uintptr_t osub_180024300 = r1vtable[157];
		uintptr_t osub_180024310 = r1vtable[158];
		uintptr_t osub_180024320 = r1vtable[159];
		uintptr_t osub_180024340 = r1vtable[160];
		uintptr_t osub_180024350 = r1vtable[161];
		uintptr_t osub_180024360 = r1vtable[162];
		uintptr_t osub_180024390 = r1vtable[163];
		uintptr_t osub_180024370 = r1vtable[164];
		uintptr_t osub_1800243C0 = r1vtable[165];
		uintptr_t osub_1800243F0 = r1vtable[166];
		uintptr_t osub_180024410 = r1vtable[167];
		uintptr_t osub_180024430 = r1vtable[168];

		static uintptr_t r1ovtable[] = {
			CreateFunction((void*)oCBaseFileSystem__Connect, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__Disconnect, (void*)fsinterface),
			CreateFunction((void*)oCFileSystem_Stdio__QueryInterface, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__Init, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__Shutdown, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__GetDependencies, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__GetTier, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__Reconnect, (void*)fsinterface),
			CreateFunction((void*)CFileSystem_Stdio__NullSub3, (void*)fsinterface),
			CreateFunction((void*)CBaseFileSystem__GetTFOFileSystemThing, (void*)fsinterface),
			CreateFunction((void*)CFileSystem_Stdio__DoTFOFilesystemOp, (void*)fsinterface),
			CreateFunction((void*)CFileSystem_Stdio__NullSub4, (void*)fsinterface),
			CreateFunction((void*)oCFileSystem_Stdio__AddSearchPath, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__RemoveSearchPath, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__RemoveAllSearchPaths, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__RemoveSearchPaths, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__MarkPathIDByRequestOnly, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__RelativePathToFullPath, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__GetSearchPath, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__AddPackFile, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__RemoveFile, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__RenameFile, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__CreateDirHierarchy, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__IsDirectory, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__FileTimeToString, (void*)fsinterface),
			CreateFunction((void*)oCFileSystem_Stdio__SetBufferSize, (void*)fsinterface),
			CreateFunction((void*)oCFileSystem_Stdio__IsOK, (void*)fsinterface),
			CreateFunction((void*)oCFileSystem_Stdio__EndOfLine, (void*)fsinterface),
			CreateFunction((void*)oCFileSystem_Stdio__ReadLine, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__FPrintf, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__LoadModule, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__UnloadModule, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__FindFirst, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__FindNext, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__FindIsDirectory, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__FindClose, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__FindFirstEx, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__FindFileAbsoluteList, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__GetLocalPath, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__FullPathToRelativePath, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__GetCurrentDirectory, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__FindOrAddFileName, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__String, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__AsyncReadMultiple, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__AsyncAppend, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__AsyncAppendFile, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__AsyncFinishAll, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__AsyncFinishAllWrites, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__AsyncFlush, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__AsyncSuspend, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__AsyncResume, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__AsyncBeginRead, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__AsyncEndRead, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__AsyncFinish, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__AsyncGetResult, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__AsyncAbort, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__AsyncStatus, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__AsyncSetPriority, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__AsyncAddRef, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__AsyncRelease, (void*)fsinterface),
			CreateFunction((void*)osub_180024450, (void*)fsinterface),
			CreateFunction((void*)osub_180024460, (void*)fsinterface),
			CreateFunction((void*)onullsub_96, (void*)fsinterface),
			CreateFunction((void*)osub_180024490, (void*)fsinterface),
			CreateFunction((void*)osub_180024440, (void*)fsinterface),
			CreateFunction((void*)onullsub_97, (void*)fsinterface),
			CreateFunction((void*)osub_180009BE0, (void*)fsinterface),
			CreateFunction((void*)osub_18000F6A0, (void*)fsinterface),
			CreateFunction((void*)osub_180002CA0, (void*)fsinterface),
			CreateFunction((void*)osub_180002CB0, (void*)fsinterface),
			CreateFunction((void*)osub_1800154F0, (void*)fsinterface),
			CreateFunction((void*)osub_180015550, (void*)fsinterface),
			CreateFunction((void*)osub_180015420, (void*)fsinterface),
			CreateFunction((void*)osub_180015480, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__RemoveLoggingFunc, (void*)fsinterface),
			CreateFunction((void*)oCBaseFileSystem__GetFilesystemStatistics, (void*)fsinterface),
			CreateFunction((void*)oCFileSystem_Stdio__OpenEx, (void*)fsinterface),
			CreateFunction((void*)osub_18000A5D0, (void*)fsinterface),
			CreateFunction((void*)osub_1800052A0, (void*)fsinterface),
			CreateFunction((void*)osub_180002F10, (void*)fsinterface),
			CreateFunction((void*)osub_18000A690, (void*)fsinterface),
			CreateFunction((void*)osub_18000A6F0, (void*)fsinterface),
			CreateFunction((void*)osub_1800057A0, (void*)fsinterface),
			CreateFunction((void*)osub_180002960, (void*)fsinterface),
			CreateFunction((void*)osub_180020110, (void*)fsinterface),
			CreateFunction((void*)osub_180020230, (void*)fsinterface),
			CreateFunction((void*)osub_180023660, (void*)fsinterface),
			CreateFunction((void*)osub_1800204A0, (void*)fsinterface),
			CreateFunction((void*)osub_180002F40, (void*)fsinterface),
			CreateFunction((void*)osub_180004F00, (void*)fsinterface),
			CreateFunction((void*)osub_180024020, (void*)fsinterface),
			CreateFunction((void*)osub_180024AF0, (void*)fsinterface),
			CreateFunction((void*)osub_180024110, (void*)fsinterface),
			CreateFunction((void*)osub_180002580, (void*)fsinterface),
			CreateFunction((void*)osub_180002560, (void*)fsinterface),
			CreateFunction((void*)osub_18000A070, (void*)fsinterface),
			CreateFunction((void*)CFileSystem_Stdio__NullSub4, (void*)fsinterface),
			CreateFunction((void*)osub_180009C20, (void*)fsinterface),
			CreateFunction((void*)osub_1800022F0, (void*)fsinterface),
			CreateFunction((void*)osub_180002330, (void*)fsinterface),
			CreateFunction((void*)osub_180009CF0, (void*)fsinterface),
			CreateFunction((void*)osub_180002340, (void*)fsinterface),
			CreateFunction((void*)osub_180002320, (void*)fsinterface),
			CreateFunction((void*)osub_180009E00, (void*)fsinterface),
			CreateFunction((void*)osub_180009F20, (void*)fsinterface),
			CreateFunction((void*)osub_180009EA0, (void*)fsinterface),
			CreateFunction((void*)osub_180009E50, (void*)fsinterface),
			CreateFunction((void*)osub_180009FC0, (void*)fsinterface),
			CreateFunction((void*)osub_180004E80, (void*)fsinterface),
			CreateFunction((void*)osub_18000A000, (void*)fsinterface),
			CreateFunction((void*)osub_180014350, (void*)fsinterface),
			CreateFunction((void*)osub_18000F5B0, (void*)fsinterface),
			CreateFunction((void*)osub_180002590, (void*)fsinterface),
			CreateFunction((void*)osub_1800025D0, (void*)fsinterface),
			CreateFunction((void*)osub_1800025E0, (void*)fsinterface),
			CreateFunction((void*)oCFileSystem_Stdio__LoadVPKForMap, (void*)fsinterface),
			CreateFunction((void*)oCFileSystem_Stdio__UnkFunc1, (void*)fsinterface),
			CreateFunction((void*)oCFileSystem_Stdio__WeirdFuncThatJustDerefsRDX, (void*)fsinterface),
			CreateFunction((void*)oCFileSystem_Stdio__GetPathTime, (void*)fsinterface),
			CreateFunction((void*)oCFileSystem_Stdio__GetFSConstructedFlag, (void*)fsinterface),
			CreateFunction((void*)oCFileSystem_Stdio__EnableWhitelistFileTracking, (void*)fsinterface),
			CreateFunction((void*)osub_18000A750, (void*)fsinterface),
			CreateFunction((void*)osub_180002B20, (void*)fsinterface),
			CreateFunction((void*)osub_18001DC30, (void*)fsinterface),
			CreateFunction((void*)osub_180002B30, (void*)fsinterface),
			CreateFunction((void*)osub_180002BA0, (void*)fsinterface),
			CreateFunction((void*)osub_180002BB0, (void*)fsinterface),
			CreateFunction((void*)osub_180002BC0, (void*)fsinterface),
			CreateFunction((void*)osub_180002290, (void*)fsinterface),
			CreateFunction((void*)osub_18001CCD0, (void*)fsinterface),
			CreateFunction((void*)osub_18001CCE0, (void*)fsinterface),
			CreateFunction((void*)osub_18001CCF0, (void*)fsinterface),
			CreateFunction((void*)osub_18001CD00, (void*)fsinterface),
			CreateFunction((void*)osub_180014520, (void*)fsinterface),
			CreateFunction((void*)osub_180002650, (void*)fsinterface),
			CreateFunction((void*)osub_18001CD10, (void*)fsinterface),
			CreateFunction((void*)osub_180016250, (void*)fsinterface),
			CreateFunction((void*)osub_18000F0D0, (void*)fsinterface),
			CreateFunction((void*)osub_1800139F0, (void*)fsinterface),
			CreateFunction((void*)osub_180016570, (void*)fsinterface),
			CreateFunction((void*)onullsub_86, (void*)fsinterface),
			CreateFunction((void*)osub_18000AEC0, (void*)fsinterface),
			CreateFunction((void*)osub_180003320, (void*)fsinterface),
			CreateFunction((void*)osub_18000AF50, (void*)fsinterface),
			CreateFunction((void*)osub_18000AF60, (void*)fsinterface),
			CreateFunction((void*)osub_180005D00, (void*)fsinterface),
			CreateFunction((void*)osub_18000AF70, (void*)fsinterface),
			CreateFunction((void*)osub_18001B130, (void*)fsinterface),
			CreateFunction((void*)osub_18000AF80, (void*)fsinterface),
			CreateFunction((void*)osub_1800034D0, (void*)fsinterface),
			CreateFunction((void*)osub_180017180, (void*)fsinterface),
			CreateFunction((void*)osub_180003550, (void*)fsinterface),
			CreateFunction((void*)osub_1800250D0, (void*)fsinterface),
			CreateFunction((void*)osub_1800241B0, (void*)fsinterface),
			CreateFunction((void*)osub_1800241C0, (void*)fsinterface),
			CreateFunction((void*)osub_1800241F0, (void*)fsinterface),
			CreateFunction((void*)osub_180024240, (void*)fsinterface),
			CreateFunction((void*)osub_180024250, (void*)fsinterface),
			CreateFunction((void*)osub_180024260, (void*)fsinterface),
			CreateFunction((void*)osub_180024300, (void*)fsinterface),
			CreateFunction((void*)osub_180024310, (void*)fsinterface),
			CreateFunction((void*)osub_180024320, (void*)fsinterface),
			CreateFunction((void*)osub_180024340, (void*)fsinterface),
			CreateFunction((void*)osub_180024350, (void*)fsinterface),
			CreateFunction((void*)osub_180024360, (void*)fsinterface),
			CreateFunction((void*)osub_180024390, (void*)fsinterface),
			CreateFunction((void*)osub_180024370, (void*)fsinterface),
			CreateFunction((void*)osub_1800243C0, (void*)fsinterface),
			CreateFunction((void*)osub_1800243F0, (void*)fsinterface),
			CreateFunction((void*)osub_180024410, (void*)fsinterface),
			CreateFunction((void*)osub_180024430, (void*)fsinterface),
			CreateFunction((void*)CFileSystem_Stdio__NullSub4, r1vtable)
		};

		fsinterfaceoffset = fsinterface + 8;
		uintptr_t* origsimplefsvtable = (*(uintptr_t**)fsinterfaceoffset);


		uintptr_t oCBaseFileSystem_Read = origsimplefsvtable[0];
		uintptr_t oCBaseFileSystem_Write = origsimplefsvtable[1];
		uintptr_t oCBaseFileSystem_Open = origsimplefsvtable[2];
		uintptr_t oCBaseFileSystem_Close = origsimplefsvtable[3];
		uintptr_t oCBaseFileSystem_Seek = origsimplefsvtable[4];
		uintptr_t oCBaseFileSystem_Tell = origsimplefsvtable[5];
		uintptr_t oCBaseFileSystem_Size = origsimplefsvtable[6];
		uintptr_t oCBaseFileSystem_Size2 = origsimplefsvtable[7];
		uintptr_t oCBaseFileSystem_Flush = origsimplefsvtable[8];
		uintptr_t oCBaseFileSystem_Precache = origsimplefsvtable[9];
		uintptr_t oCBaseFileSystem_FileExists = origsimplefsvtable[10];
		uintptr_t oCBaseFileSystem_IsFileWritable = origsimplefsvtable[11];
		uintptr_t oCBaseFileSystem_SetFileWritable = origsimplefsvtable[12];
		uintptr_t oCBaseFileSystem_GetFileTime = origsimplefsvtable[13];
		uintptr_t oCBaseFileSystem_ReadFile = origsimplefsvtable[14];
		uintptr_t oCBaseFileSystem_WriteFile = origsimplefsvtable[15];
		uintptr_t oCBaseFileSystem_UnzipFile = origsimplefsvtable[16];
		static uintptr_t simplefsvtable[] = {
			CreateFunction((void*)oCBaseFileSystem_Read, (void*)fsinterfaceoffset),
			CreateFunction((void*)oCBaseFileSystem_Write, (void*)fsinterfaceoffset),
			CreateFunction((void*)oCBaseFileSystem_Open, (void*)fsinterfaceoffset),
			CreateFunction((void*)oCBaseFileSystem_Close, (void*)fsinterfaceoffset),
			CreateFunction((void*)oCBaseFileSystem_Seek, (void*)fsinterfaceoffset),
			CreateFunction((void*)oCBaseFileSystem_Tell, (void*)fsinterfaceoffset),
			CreateFunction((void*)oCBaseFileSystem_Size, (void*)fsinterfaceoffset),
			CreateFunction((void*)oCBaseFileSystem_Size2, (void*)fsinterfaceoffset),
			CreateFunction((void*)oCBaseFileSystem_Flush, (void*)fsinterfaceoffset),
			CreateFunction((void*)oCBaseFileSystem_Precache, (void*)fsinterfaceoffset),
			CreateFunction((void*)oCBaseFileSystem_FileExists, (void*)fsinterfaceoffset),
			CreateFunction((void*)oCBaseFileSystem_IsFileWritable, (void*)fsinterfaceoffset),
			CreateFunction((void*)oCBaseFileSystem_SetFileWritable, (void*)fsinterfaceoffset),
			CreateFunction((void*)oCBaseFileSystem_GetFileTime, (void*)fsinterfaceoffset),
			CreateFunction((void*)oCBaseFileSystem_ReadFile, (void*)fsinterfaceoffset),
			CreateFunction((void*)oCBaseFileSystem_WriteFile, (void*)fsinterfaceoffset),
			CreateFunction((void*)oCBaseFileSystem_UnzipFile, (void*)fsinterfaceoffset)

		};
		struct fsptr {
			void* ptr1;
			void* ptr2;
			void* ptr3;
			void* ptr4;
			void* ptr5;
		};
		static void* whatever3 = &r1ovtable;
		static void* whatever4 = &simplefsvtable;
		static fsptr a;

		a.ptr1 = whatever3;
		a.ptr2 = whatever4;
		a.ptr3 = whatever4;
		a.ptr4 = whatever4;
		a.ptr5 = whatever4;
		fsintfakeptr = (uintptr_t)(&a.ptr1);
		return &a.ptr1;
	}
	if (!strcmp(pName, "VModelInfoServer002")) {
		std::cout << "wrapping VModelInfoServer002" << std::endl;
		modelinterface = (uintptr_t)oAppSystemFactory(pName, pReturnCode);

		uintptr_t* r1vtable = *(uintptr_t**)oAppSystemFactory(pName, pReturnCode);
		uintptr_t oCModelInfo_dtor_0 = r1vtable[0];
		uintptr_t oCModelInfoServer__GetModel = r1vtable[1];
		uintptr_t oCModelInfoServer__GetModelIndex = r1vtable[2];
		uintptr_t oCModelInfo__GetModelName = r1vtable[3];
		uintptr_t oCModelInfo__GetVCollide = r1vtable[4];
		uintptr_t oCModelInfo__GetVCollideEx = r1vtable[5];
		uintptr_t oCModelInfo__GetVCollideEx2 = r1vtable[6];
		uintptr_t oCModelInfo__GetModelRenderBounds = r1vtable[7];
		uintptr_t oCModelInfo__GetModelFrameCount = r1vtable[8];
		uintptr_t oCModelInfo__GetModelType = r1vtable[9];
		uintptr_t oCModelInfo__GetModelExtraData = r1vtable[10];
		uintptr_t oCModelInfo__IsTranslucentTwoPass = r1vtable[11];
		uintptr_t oCModelInfo__ModelHasMaterialProxy = r1vtable[12];
		uintptr_t oCModelInfo__IsTranslucent = r1vtable[13];
		uintptr_t oCModelInfo__NullSub1 = r1vtable[14];
		uintptr_t oCModelInfo__UnkFunc1 = r1vtable[15];
		uintptr_t oCModelInfo__UnkFunc2 = r1vtable[16];
		uintptr_t oCModelInfo__UnkFunc3 = r1vtable[17];
		uintptr_t oCModelInfo__UnkFunc4 = r1vtable[18];
		uintptr_t oCModelInfo__UnkFunc5 = r1vtable[19];
		uintptr_t oCModelInfo__UnkFunc6 = r1vtable[20];
		uintptr_t oCModelInfo__UnkFunc7 = r1vtable[21];
		uintptr_t oCModelInfo__UnkFunc8 = r1vtable[22];
		uintptr_t oCModelInfo__UnkFunc9 = r1vtable[23];
		uintptr_t oCModelInfo__UnkFunc10 = r1vtable[24];
		uintptr_t oCModelInfo__UnkFunc11 = r1vtable[25];
		uintptr_t oCModelInfo__UnkFunc12 = r1vtable[26];
		uintptr_t oCModelInfo__UnkFunc13 = r1vtable[27];
		uintptr_t oCModelInfo__UnkFunc14 = r1vtable[28];
		uintptr_t oCModelInfo__UnkFunc15 = r1vtable[29];
		uintptr_t oCModelInfo__UnkFunc16 = r1vtable[30];
		uintptr_t oCModelInfo__UnkFunc17 = r1vtable[31];
		uintptr_t oCModelInfo__UnkFunc18 = r1vtable[32];
		uintptr_t oCModelInfo__UnkFunc19 = r1vtable[33];
		uintptr_t oCModelInfo__UnkFunc20 = r1vtable[34];
		uintptr_t oCModelInfo__UnkFunc21 = r1vtable[35];
		uintptr_t oCModelInfo__UnkFunc22 = r1vtable[36];
		uintptr_t oCModelInfo__UnkFunc23 = r1vtable[37];
		uintptr_t oCModelInfo__NullSub2 = r1vtable[38];
		uintptr_t oCModelInfo__UnkFunc24 = r1vtable[39];
		uintptr_t oCModelInfo__UnkFunc25 = r1vtable[40];
		static uintptr_t r1ovtable[] = {
			CreateFunction((void*)oCModelInfo_dtor_0, (void*)modelinterface),
			CreateFunction((void*)oCModelInfoServer__GetModel, (void*)modelinterface),
			CreateFunction((void*)oCModelInfoServer__GetModelIndex, (void*)modelinterface),
			CreateFunction((void*)oCModelInfo__GetModelName, (void*)modelinterface),
			CreateFunction((void*)oCModelInfo__GetVCollide, (void*)modelinterface),
			CreateFunction((void*)oCModelInfo__GetVCollideEx, (void*)modelinterface),
			CreateFunction((void*)oCModelInfo__GetVCollideEx2, (void*)modelinterface),
			CreateFunction((void*)oCModelInfo__GetModelRenderBounds, (void*)modelinterface),
			CreateFunction(CModelInfo__UnkSetFlag, (void*)modelinterface),
			CreateFunction(CModelInfo__UnkClearFlag, (void*)modelinterface),
			CreateFunction(CModelInfo__GetFlag, (void*)modelinterface),
			CreateFunction(CModelInfo__UnkTFOVoid, (void*)modelinterface),
			CreateFunction(CModelInfo__UnkTFOVoid2, (void*)modelinterface),
			CreateFunction(CModelInfo__ShouldRet0, (void*)modelinterface),
			CreateFunction(CModelInfo__UnkTFOVoid3, (void*)modelinterface),
			CreateFunction(CModelInfo__ClientFullyConnected, (void*)modelinterface),
			CreateFunction((void*)oCModelInfo__GetModelFrameCount, (void*)modelinterface),
			CreateFunction((void*)oCModelInfo__GetModelType, (void*)modelinterface),
			CreateFunction(CModelInfo__UnkTFOShouldRet0_2, (void*)modelinterface),
			CreateFunction((void*)oCModelInfo__GetModelExtraData, (void*)modelinterface),
			CreateFunction((void*)oCModelInfo__IsTranslucentTwoPass, (void*)modelinterface),
			CreateFunction((void*)oCModelInfo__ModelHasMaterialProxy, (void*)modelinterface),
			CreateFunction((void*)oCModelInfo__IsTranslucent, (void*)modelinterface),
			CreateFunction((void*)oCModelInfo__NullSub1, (void*)modelinterface),
			CreateFunction((void*)oCModelInfo__UnkFunc1, (void*)modelinterface),
			CreateFunction((void*)oCModelInfo__UnkFunc2, (void*)modelinterface),
			CreateFunction((void*)oCModelInfo__UnkFunc3, (void*)modelinterface),
			CreateFunction((void*)oCModelInfo__UnkFunc4, (void*)modelinterface),
			CreateFunction((void*)oCModelInfo__UnkFunc5, (void*)modelinterface),
			CreateFunction((void*)oCModelInfo__UnkFunc6, (void*)modelinterface),
			CreateFunction((void*)oCModelInfo__UnkFunc7, (void*)modelinterface),
			CreateFunction((void*)oCModelInfo__UnkFunc8, (void*)modelinterface),
			CreateFunction((void*)oCModelInfo__UnkFunc9, (void*)modelinterface),
			CreateFunction((void*)oCModelInfo__UnkFunc10, (void*)modelinterface),
			CreateFunction((void*)oCModelInfo__UnkFunc11, (void*)modelinterface),
			CreateFunction((void*)oCModelInfo__UnkFunc12, (void*)modelinterface),
			CreateFunction((void*)oCModelInfo__UnkFunc13, (void*)modelinterface),
			CreateFunction((void*)oCModelInfo__UnkFunc14, (void*)modelinterface),
			CreateFunction((void*)oCModelInfo__UnkFunc15, (void*)modelinterface),
			CreateFunction((void*)oCModelInfo__UnkFunc16, (void*)modelinterface),
			CreateFunction((void*)oCModelInfo__UnkFunc17, (void*)modelinterface),
			CreateFunction((void*)oCModelInfo__UnkFunc18, (void*)modelinterface),
			CreateFunction((void*)oCModelInfo__UnkFunc19, (void*)modelinterface),
			CreateFunction((void*)oCModelInfo__UnkFunc20, (void*)modelinterface),
			CreateFunction((void*)oCModelInfo__UnkFunc21, (void*)modelinterface),
			CreateFunction((void*)oCModelInfo__UnkFunc22, (void*)modelinterface),
			CreateFunction((void*)oCModelInfo__UnkFunc23, (void*)modelinterface),
			CreateFunction((void*)oCModelInfo__NullSub2, (void*)modelinterface),
			CreateFunction((void*)oCModelInfo__UnkFunc24, (void*)modelinterface),
			CreateFunction((void*)oCModelInfo__UnkFunc25, (void*)modelinterface)
		};
		static void* whatever4 = &r1ovtable;
		return &whatever4;
	}
	if (!strcmp(pName, "VEngineServerStringTable001")) {
		std::cout << "wrapping VEngineServerStringTable001" << std::endl;
		stringtableinterface = (uintptr_t)oAppSystemFactory(pName, pReturnCode);
		static uintptr_t* r1vtable = *(uintptr_t**)oAppSystemFactory(pName, pReturnCode);

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
		static void* whatever5 = &r1ovtable;
		return &whatever5;
	}
	if (!strcmp(pName, "VEngineCvar007")) {
		std::cout << "wrapping VEngineCvar007" << std::endl;
		static uintptr_t* original_this = *(uintptr_t**)oAppSystemFactory(pName, pReturnCode);
		static uintptr_t* r1vtable = *(uintptr_t**)oAppSystemFactory(pName, pReturnCode);
		cvarinterface = (uintptr_t)oAppSystemFactory(pName, pReturnCode);

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
		static void* whatever6 = &r1ovtable;
		return &whatever6;
	}
	if (!oAppSystemFactory(pName, pReturnCode) && !strcmp(pName, "VENGINE_DEDICATEDEXPORTS_API_VERSION003")) {
		std::cout << "forging dediexports" << std::endl;
		return (void*)1;
	}
	void* result = oAppSystemFactory(pName, pReturnCode);
	if (result) {
		std::cout << "found " << pName << "  in appsystem factory" << std::endl;
		return result;
	}

	std::cout << "engine is set up, looking for " << pName << std::endl;

	return R1OCreateInterface(pName, pReturnCode);
}


class SomeNexonBullshit {
public:
	virtual void whatever() = 0;
	virtual void Init() = 0;
};
char __fastcall CServerGameDLL__DLLInit(void* thisptr, CreateInterfaceFn appSystemFactory,
	CreateInterfaceFn physicsFactory, CreateInterfaceFn fileSystemFactory,
	void* pGlobals)
{
#ifdef DEDICATED
	pGlobals = (void*)((uintptr_t)pGlobals - 8); // Don't ask. If you DO ask, you will die a violent, painful death - wndrr
#endif
	oAppSystemFactory = appSystemFactory;
	oFileSystemFactory = fileSystemFactory;
	oPhysicsFactory = physicsFactory;
	engineR1O = LoadLibraryA("engine_r1o.dll");
	launcherR1O = LoadLibraryA("launcher_r1o.dll");
	R1OCreateInterface = reinterpret_cast<CreateInterfaceFn>(GetProcAddress(engineR1O, "CreateInterface"));
	R1OLauncherCreateInterface = reinterpret_cast<CreateInterfaceFn>(GetProcAddress(launcherR1O, "CreateInterface"));

	reinterpret_cast<char(__fastcall*)(__int64, CreateInterfaceFn)>((uintptr_t)(engineR1O)+0x1C6B30)(0, R1OFactory); // call is to CDedicatedServerAPI::Connect
	void* whatev = R1OFactory;
	reinterpret_cast<char(__fastcall*)(CreateInterfaceFn*)>((uintptr_t)(launcherR1O)+0x13930)((CreateInterfaceFn*)(&whatev)); // call is to ConnectTier1Interfaces

	reinterpret_cast<void(__fastcall*)()>((uintptr_t)(engineR1O)+0x2742A0)(); // register engine convars
	reinterpret_cast<void(__fastcall*)()>((uintptr_t)(launcherR1O)+0x018990)(); // register launcher convars
	//*(__int64*)((uintptr_t)(GetModuleHandleA("launcher_r1o.dll")) + 0xECBE0) = fsintfakeptr; 
	*(__int64*)((uintptr_t)(GetModuleHandleA("launcher_r1o.dll")) + 0xF4228) = (__int64)(appSystemFactory("VENGINE_LAUNCHER_API_VERSION004", 0));
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
	size_t bytes = 0;
	//unsigned char noparray = { 0x90 };
	//WriteProcessMemory(GetCurrentProcess(), (void*)((uintptr_t)(GetModuleHandleA("filesystem_stdio.dll")) + 0x74E26), &noparray, sizeof(noparray), &bytes);
	//WriteProcessMemory(GetCurrentProcess(), (void*)((uintptr_t)(GetModuleHandleA("filesystem_stdio.dll")) + 0x74E27), &noparray, sizeof(noparray), &bytes);
	//WriteProcessMemory(GetCurrentProcess(), (void*)((uintptr_t)(GetModuleHandleA("filesystem_stdio.dll")) + 0x74E28), &noparray, sizeof(noparray), &bytes);

	return CServerGameDLL__DLLInitOriginal(thisptr, R1OFactory, R1OFactory, R1OFactory, pGlobals);
}

extern "C" __declspec(dllexport) void StackToolsNotify_LoadedLibrary(char* pModuleName)
{
	std::cout << "loaded " << pModuleName << std::endl;
}
typedef char(*MatchRecvPropsToSendProps_RType)(__int64 a1, __int64 a2, __int64 a3, __int64 a4);
MatchRecvPropsToSendProps_RType MatchRecvPropsToSendProps_ROriginal;
__int64 __fastcall sub_1804719D0(const char* a1, const char* a2)
{
	if (!a2 || !a1)
		return 0;
	//std::cout << "a1: " << (char*)a1 << " a2: " << (char*)a2 << std::endl;

	unsigned int v4; // r8d
	int v5; // r9d
	unsigned int v7; // ecx
	int v8; // edx

	while (1)
	{
		v4 = *a1;
		v5 = *a2;
		if (v4 != v5)
			break;
		if (!*a1)
			return 0i64;
	LABEL_10:
		v7 = a1[1];
		v8 = a2[1];
		a1 += 2;
		a2 += 2;
		if (v7 == v8)
		{
			if (!v7)
				return 0i64;
		}
		else
		{
			if (!v8)
				return v7;
			if (v7 - 65 <= 0x19)
				v7 += 32;
			if ((unsigned int)(v8 - 65) <= 0x19)
				v8 += 32;
			if (v7 != v8)
			{
				v7 -= v8;
				return v7;
			}
		}
	}
	if (!*a2)
		return v4;
	if (v4 - 65 <= 0x19)
		v4 += 32;
	if ((unsigned int)(v5 - 65) <= 0x19)
		v5 += 32;
	if (v4 == v5)
		goto LABEL_10;
	v4 -= v5;
	return v4;
}

// #STR: "CompareRecvPropToSendProp: missing a property."
char __fastcall CompareRecvPropToSendProp(__int64 a1, __int64 a2)
{
	int v4; // ecx

	while (1)
	{
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
__int64 FindRecvProp(__int64 a4, const char* v9)
{
	const char* v16 = 0; // [rsp+30h] [rbp-38h]

	__int64 RecvProp = 0; // rbp
	int v10 = 0; // ebx
	__int64 j = 0; // rdi

	v10 = 0;
	v16 = v9;
	if (*(int*)(a4 + 8) <= 0)
		return 0;
	for (j = 0i64; ; j += 96i64)
	{
		RecvProp = j + *(_QWORD*)a4;
		if (!(unsigned int)sub_1804719D0(*(const char**)RecvProp, v9))
			break;
		if (++v10 >= *(_DWORD*)(a4 + 8))
			return 0;
		v9 = v16;
	}
	return RecvProp;
}
char __fastcall MatchRecvPropsToSendProps_R(__int64 a1, __int64 a2, __int64 pSendTable, __int64 a4)
{
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

	auto sub_1801D9D00 = (__int64(__fastcall*)(__int64 a1, _QWORD * a2))(((uintptr_t)GetModuleHandleA(ENGINE_DLL)) + 0x1D9D00);
	v5 = (_QWORD*)pSendTable;
	v5 = (_QWORD*)pSendTable;
	v14 = 0;
	if (*(int*)(pSendTable + 8) <= 0)
		return 1;
	v6 = 0i64;
	for (i = 0i64; ; i += 136i64)
	{
		v7 = v6 + *v5;
		v17 = v7;
		v8 = *(_DWORD*)(v7 + 88);
		if ((v8 & 0x40) == 0 && (v8 & 0x100) == 0)
		{
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
			}
			if (*(_DWORD*)(v17 + 16) == 6
				&& !MatchRecvPropsToSendProps_R(a1, a2, *(_QWORD*)(v17 + 112), *(_QWORD*)(RecvProp + 64)))
			{
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
char __fastcall sub_1801C79A0(__int64 a1, __int64 a2)
{
	if (!strcmp(*(const char**)(a1 + 16), "DT_BigBrotherPanelEntity") || !strcmp(*(const char**)(a1 + 16), "DT_ControlPanelEntity") || !strcmp(*(const char**)(a1 + 16), "DT_RushPointEntity") || !strcmp(*(const char**)(a1 + 16), "DT_SpawnItemEntity")) {
		std::cout << "blocking st " << *(const char**)(a1 + 16) << std::endl;
		return false;
	}
	return sub_1801C79A0Original(a1, a2);
}

char __fastcall sub_180217C30(char* a1, __int64 size, _QWORD* a3, __int64 a4)
{
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