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

#include <WinSock2.h>
#include <ws2tcpip.h>
#include "core.h"
#include "materialsystem_dx11_texture_load_gate.h"
#include "r1o_runtime_paths.h"

#include <MinHook.h>
#include <cstdlib>
#include <new>
#include <cctype>
#include "windows.h"

#include <iostream>
#include "cvar.h"
#include <winternl.h>  // For UNICODE_STRING.
#include <fstream>
#include <filesystem>
#include <intrin.h>
#include "memory.h"
#include "filesystem.h"
#include "r1o_vpk.h"
#include "script_error_telemetry.h"
#include "bitbuf.h"
#include "defs.h"
#include "factory.h"
#include "TableDestroyer.h"
#include <DbgHelp.h>
#include <string.h>
#include <stdio.h>
#include <set>
#include <mutex>
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <cmath>
#include <climits>
#include "engine_vtable.h"
#include "model_info_vtable.h"
//#include "thirdparty/silver-bun/silver-bun.h"
#include "load.h"
#include "logging.h"
#include "vtable.h"
#include "persistentdata.h"
#include "persistentdata_slots.h"
#include "precache.h"
#include "squirrel.h"
#include "commands.h"
#include "../dedicated.h"

#pragma comment(lib, "Dbghelp.lib")

CServerGameDLL__DLLInitType CServerGameDLL__DLLInitOriginal;
CreateInterfaceFn oAppSystemFactory;
CreateInterfaceFn oFileSystemFactory;
CreateInterfaceFn oPhysicsFactory;
static void* s_R1ONativeEngineServer022;

void* GetR1ONativeEngineServer022()
{
	return s_R1ONativeEngineServer022;
}

static void* RememberR1ONativeEngineServer022(const char* name, void* interfacePointer)
{
	if (interfacePointer && name && !strcmp_static(name, "VEngineServer022"))
		s_R1ONativeEngineServer022 = interfacePointer;
	return interfacePointer;
}

void CNetworkStringTableContainer__SetTickCount(__int64 a1, char a2)
{
	*(char*)(a1 + 8) = a2;
}

void CCVar__SetSomeFlag_Corrupt(int64_t a1, int64_t a2)
{
	return;
}

int64_t CCVar__GetSomeFlag(int64_t thisptr)
{
	return 0;
}

uintptr_t modelinterface;
uintptr_t stringtableinterface;
uintptr_t fsintfakeptr = 0;

HMODULE engineR1O;
CreateInterfaceFn R1OCreateInterface;
void* R1OFactory(const char* pName, int* pReturnCode);
static bool IsR1OOrTFOModuleAddress(PVOID retAddress);
static CreateInterfaceFn R1OCreateInterfaceOriginal;
static CDedicatedServerAPI_ConnectType CDedicatedServerAPI_ConnectOriginal;
static CDedicatedServerAPI_InitType CDedicatedServerAPI_InitOriginal;
static CDedicatedServerAPI_ModInitType CDedicatedServerAPI_ModInitOriginal;
using R1OFileSystem_LoadSearchPathsType = __int64(__fastcall*)(__int64 params);
static R1OFileSystem_LoadSearchPathsType R1OFileSystem_LoadSearchPathsOriginal;
static uintptr_t s_R1OFileSystem_LoadSearchPathsTarget;
using R1OFileSystem_ParseGameInfoType = __int64(__fastcall*)(void* fileSystem, const char* gameInfoPath, void* outGameInfo, void* outSearchPaths, void* outSearchPathRoot);
static R1OFileSystem_ParseGameInfoType R1OFileSystem_ParseGameInfoOriginal;
using R1OFileSystem_SetGameInfoNameType = char*(__fastcall*)(void* fileSystem);
static R1OFileSystem_SetGameInfoNameType R1OFileSystem_SetGameInfoNameOriginal;
using R1OFileSystem_LoadKeyValuesFileType = __int64(__fastcall*)(void* fileSystem, const char* path);
static R1OFileSystem_LoadKeyValuesFileType R1OFileSystem_LoadKeyValuesFileOriginal;
using R1OFileSystem_RefreshSearchPathsType = void(__fastcall*)(void* fileSystem);
static R1OFileSystem_RefreshSearchPathsType R1OFileSystem_RefreshSearchPathsOriginal;
using R1OKeyValues_LoadFromFileType = unsigned char(__fastcall*)(void* keyValues, void* fileSystem, const char* resourceName, const char* pathId, void* unknown);
static R1OKeyValues_LoadFromFileType R1OKeyValues_LoadFromFileOriginal;
using R1OFileSystemFatalType = __int64(__cdecl*)(char showVConfig, unsigned int code, const char* fmt, ...);
static R1OFileSystemFatalType R1OFileSystemFatalOriginal;
using R1OCOM_InitFilesystemType = __int64(__fastcall*)(const char* initialMod);
static R1OCOM_InitFilesystemType R1OCOM_InitFilesystemOriginal;
using R1OEngine_PostFilesystemInitType = __int64(__fastcall*)();
static R1OEngine_PostFilesystemInitType R1OEngine_PostFilesystemInitOriginal;
using R1OLoadGameSoundManifestType = __int64(__fastcall*)();
static R1OLoadGameSoundManifestType R1OLoadGameSoundManifest;
using R1OEngine_InitServerSystemsType = char(__fastcall*)(__int64 unknown);
static R1OEngine_InitServerSystemsType R1OEngine_InitServerSystemsOriginal;
using R1OEngineFatalType = void(__cdecl*)(const char* fmt, ...);
static R1OEngineFatalType R1OEngineFatalOriginal;
using R1OAppSystemGroupConstructorType = __int64(__fastcall*)(__int64 thisptr, __int64 parent);
static R1OAppSystemGroupConstructorType R1OAppSystemGroupConstructorOriginal;
using R1OAppSystemGroupRunType = __int64(__fastcall*)(__int64 thisptr);
static R1OAppSystemGroupRunType R1OAppSystemGroupRunOriginal;
using R1OAppSystemGroupStageType = __int64(__fastcall*)(__int64 thisptr);
static R1OAppSystemGroupStageType R1OAppSystemGroupStartupOriginal;
static R1OAppSystemGroupStageType R1OAppSystemGroupAddSystemsOriginal;
static R1OAppSystemGroupStageType R1OAppSystemGroupInitSystemsOriginal;
using R1OLoadServerLocalGameDLLType = char(__fastcall*)(const char* dllName);
static R1OLoadServerLocalGameDLLType R1OLoadServerLocalGameDLLOriginal;
using R1OInitializeGameDLLsType = __int64(__fastcall*)();
static R1OInitializeGameDLLsType R1OInitializeGameDLLsOriginal;
static bool s_R1OSetPreCacheHookInstalled;
static CServerGameDLL__DLLInitType R1OTFOServerGameDLL_DllInitOriginal;
static bool s_R1OTFOServerGameDLL_DllInitHooked;
using R1OTFOCBaseAnimatingGetModelPtrType = __int64(__fastcall*)(__int64 thisptr);
static R1OTFOCBaseAnimatingGetModelPtrType R1OTFOCBaseAnimatingGetModelPtr;
using R1OTFOCBaseAnimatingSetSequenceType = void(__fastcall*)(__int64 thisptr, int sequence);
static R1OTFOCBaseAnimatingSetSequenceType R1OTFOCBaseAnimatingSetSequenceOriginal;
static bool s_R1OTFOCBaseAnimatingSetSequenceHooked;
static int s_R1OTFOCBaseAnimatingNullStudioLogBudget = 8;
using R1OCVar_SetDLLIdentifierType = void(__fastcall*)(void* thisptr, HMODULE module);
static R1OCVar_SetDLLIdentifierType R1OCVar_SetDLLIdentifierOriginal;
static bool s_R1OCVar_SetDLLIdentifierHooked;
using R1OLoadModuleType = HMODULE(__fastcall*)(char* source);
static R1OLoadModuleType R1OLoadModuleOriginal;
using R1OLoadExternalServerInfoType = char(__fastcall*)(__int64 thisptr);
static R1OLoadExternalServerInfoType R1OLoadExternalServerInfoOriginal;
using R1OCbuf_AddTextType = char(__fastcall*)(void* commandBuffer, const char* text);
static R1OCbuf_AddTextType R1OCbuf_AddTextOriginal;
using R1OCbuf_ExecuteType = void(__fastcall*)();
static R1OCbuf_ExecuteType R1OCbuf_ExecuteOriginal;
using R1OCbuf_DispatchType = __int64(__fastcall*)(__int64 context, __int64 command, int source, int flags);
static R1OCbuf_DispatchType R1OCbuf_DispatchOriginal;
using R1ONativeIpFilterType = bool(__fastcall*)(const void* address, int exemptAddress);
static R1ONativeIpFilterType R1ONativeIpFilterOriginal;
using R1OCEngineFilterTimeType = bool(__fastcall*)(void* thisptr, float dt, float* flMinFrameTime);
static R1OCEngineFilterTimeType R1OCEngineFilterTimeOriginal;
using R1OSleepType = VOID(WINAPI*)(DWORD milliseconds);
static R1OSleepType R1OSleepOriginal;
static bool s_R1OIdleFrameSleepPatched;
using R1OCDedicatedServerAPI_RunFrameType = char(__fastcall*)(__int64 thisptr);
static R1OCDedicatedServerAPI_RunFrameType R1OCDedicatedServerAPI_RunFrameOriginal;
using R1OCDedicatedServerAPI_AddConsoleTextType = void(__fastcall*)(__int64 thisptr, char* text);
static R1OCDedicatedServerAPI_AddConsoleTextType R1OCDedicatedServerAPI_AddConsoleTextOriginal;
using R1OSysInitType = __int64(__fastcall*)(__int64 thisptr, const char* baseDirectory, int dedicated);
static R1OSysInitType R1OSysInitOriginal;
using R1OUpdateMapType = void(__fastcall*)(unsigned char updateType, const char* mapName, const char* gameMode);
static R1OUpdateMapType R1OUpdateMapOriginal;
static bool s_R1OUpdateMapHooked;
static int s_R1OUpdateMapNullVideoModeLogBudget = 4;
using R1OVPhysicsDeferredReleaseType = void(__fastcall*)();
using R1OVPhysicsThreadLocalGetType = void*(__fastcall*)(void* thisptr);
using R1OVPhysicsThreadLocalSetType = void(__fastcall*)(void* thisptr, void* value);
using R1OVPhysicsReleaseCachedCollisionType = __int64(__fastcall*)(void* owner, unsigned int handle);
static R1OVPhysicsDeferredReleaseType R1OVPhysicsDeferredReleaseOriginal;
static R1OVPhysicsThreadLocalGetType R1OVPhysicsThreadLocalGet;
static R1OVPhysicsThreadLocalSetType R1OVPhysicsThreadLocalSet;
static R1OVPhysicsReleaseCachedCollisionType R1OVPhysicsReleaseCachedCollision;
static uintptr_t s_R1OVPhysicsBase;
static bool s_R1OVPhysicsDeferredReleaseGuardHooked;
static int s_R1OVPhysicsDeferredReleaseCrashLogBudget = 8;
using R1ONetListenSocketType = void(__fastcall*)(int socketIndex);
static R1ONetListenSocketType R1ONetListenSocketOriginal;
using R1ONetGetPacketType = char*(__fastcall*)(int socketIndex, __int64 data, char encrypted);
static R1ONetGetPacketType R1ONetGetPacketOriginal;
using R1ONetGetLoopbackPacketType = char*(__fastcall*)(int socketIndex, __int64 data);
static R1ONetGetLoopbackPacketType R1ONetGetLoopbackPacketOriginal;
using R1ONetReceivePacketType = char(__fastcall*)(unsigned int frameTime, __int64 packet, char encrypted);
static R1ONetReceivePacketType R1ONetReceivePacketOriginal;
using R1ORecvFromType = int (WSAAPI*)(SOCKET, char*, int, int, sockaddr*, int*);
static R1ORecvFromType R1ORecvFromOriginal;
static thread_local unsigned int s_R1ORecvFromGuardDepth;
static thread_local unsigned int s_R1ORecvFromCalls;
using R1ONetChanLookupType = __int64(__fastcall*)(int socketIndex, int* packet);
static R1ONetChanLookupType R1ONetChanLookupOriginal;
using R1ONetChanProcessPacketType = __int64(__fastcall*)(__int64 netChan, char* packet, __int64 allowConnectionless);
static R1ONetChanProcessPacketType R1ONetChanProcessPacketOriginal;
using R1ONetMessageLookupType = __int64(__fastcall*)(__int64 registry, int id);
static R1ONetMessageLookupType R1ONetMessageLookupOriginal;
using R1ONetMessageDecodeType = char(__fastcall*)(__int64 message, __int64 bitBuffer);
static R1ONetMessageDecodeType R1ONetMessageDecodeOriginal;
using R1ONetMessageReadFromBufferType = bool(__fastcall*)(__int64 message, __int64 bitBuffer);
using R1ONETSetConVarReadFromBufferType = bool(__fastcall*)(__int64 message, __int64 bitBuffer);
static R1ONETSetConVarReadFromBufferType R1ONETSetConVarReadFromBufferOriginal;
using R1ONetMessageProcessType = char(__fastcall*)(__int64 message);
using R1OCGameClientProcessSignonStateType = char(__fastcall*)(__int64 client, __int64 message);
static R1OCGameClientProcessSignonStateType R1OCGameClientProcessSignonStateOriginal;
using R1OCGameClientProcessClientInfoType = char(__fastcall*)(__int64 client, __int64 message);
static R1OCGameClientProcessClientInfoType R1OCGameClientProcessClientInfoOriginal;
using R1ONetChanProcessMessagesType = char(__fastcall*)(__int64 netChan, __int64 bitBuffer);
static R1ONetChanProcessMessagesType R1ONetChanProcessMessagesOriginal;
using R1ONetChanProcessSpecialMessageType = char(__fastcall*)(__int64 netChan, int id, __int64 bitBuffer);
static R1ONetChanProcessSpecialMessageType R1ONetChanProcessSpecialMessageOriginal;
using R1ONetChanSendNetMsgType = bool(__fastcall*)(__int64 netChan, __int64 message, bool forceReliable, bool voice);
static R1ONetChanSendNetMsgType R1ONetChanSendNetMsgOriginal;
using R1ONetChanSendDataType = __int64(__fastcall*)(__int64 netChanOrClient, bf_write* bitBuffer);
static R1ONetChanSendDataType R1ONetChanSendDataOriginal;
using R1OSendTableEncodeType = bool(__fastcall*)(__int64 sendTable, __int64 object, __int64 bitBuffer, int objectId, __int64 recipients, char delta, __int64 oldState, __int64 fieldBitCounts);
static R1OSendTableEncodeType R1OSendTableEncodeOriginal;
using R1ODeltaPropIndexWriteType = int(__fastcall*)(__int64 writer, int propIndex);
static R1ODeltaPropIndexWriteType R1ODeltaPropIndexWriteOriginal;
using R1OEncodePropType = __int64(__fastcall*)(__int64 encodeInfo, int propIndex);
static R1OEncodePropType R1OEncodePropOriginal;
using R1OWritePropListType = __int64(__fastcall*)(__int64 sendTable, __int64 newState, __int64 oldState, __int64 output, __int64 recipients, unsigned int* changedProps, int changedPropCount);
static R1OWritePropListType R1OWritePropListOriginal;
using R1OBuildPropListType = __int64(__fastcall*)(__int64 sendTable, __int64 oldState, __int64 newState, __int64 baselineState, int maxProp, __int64 output);
static R1OBuildPropListType R1OBuildPropListOriginal;
using R1OBuildChangedPropListType = int(__fastcall*)(
	__int64 sendTable,
	__int64 oldData,
	int oldBits,
	__int64 newData,
	int newBits,
	unsigned int* changedProps,
	__int64 profileData,
	int maxProp,
	int objectId);
static R1OBuildChangedPropListType R1OBuildChangedPropListOriginal;
using R1ODeltaCalculatorAdvanceType = __int64(__fastcall*)(__int64 calculator);
static R1ODeltaCalculatorAdvanceType R1ODeltaCalculatorAdvanceOriginal;
using R1OSnapshotEntityWriteType = __int64(__fastcall*)(__int64 context, unsigned int* changedProps, unsigned int changedPropCount);
static R1OSnapshotEntityWriteType R1OSnapshotEntityWriteOriginal;
using R1OCullChangedPropsType = __int64(__fastcall*)(__int64 cullStack, unsigned int* changedProps, int changedPropCount, unsigned int* outputProps);
static R1OCullChangedPropsType R1OCullChangedPropsOriginal;
using R1OSVEnsureInstanceBaselineType = void(__fastcall*)(__int64 unused, int entIndex, __int64 data, unsigned int bytes);
static R1OSVEnsureInstanceBaselineType R1OSVEnsureInstanceBaselineOriginal;
using R1OBFReadSeekType = char(__fastcall*)(void* bitBuffer, __int64 bitPosition);
static R1OBFReadSeekType R1OBFReadSeek;
using R1OBFReadStringType = char(__fastcall*)(__int64 bitBuffer, char* output, int outputSize, char stopAtNewline, int* outChars);
static R1OBFReadStringType R1OBFReadString;
using R1ONETSetConVarAddToTailType = __int64(__fastcall*)(__int64 vector, const NetMessageCvar_t* value);
static R1ONETSetConVarAddToTailType R1ONETSetConVarAddToTail;
using R1OProcessConnectionlessPacketType = char(__fastcall*)(void* thisptr, __int64 packet);
static R1OProcessConnectionlessPacketType R1OProcessConnectionlessPacketOriginal;
using R1OReplyChallengeType = __int64(__fastcall*)(__int64 server, int socketIndex, __int64 address, __int64 bitBuffer, char useCompression);
static R1OReplyChallengeType R1OReplyChallengeOriginal;
using R1ONetSendPacketType = __int64(__fastcall*)(__int64 netChan, unsigned int socketIndex, __int64 address, const void* data, unsigned int length, __int64 bitBuffer, char compressed, int sequenceOverride, char unknown9, __int64 unknown10, char unknown11);
static R1ONetSendPacketType R1ONetSendPacket;
using R1OGetChallengeNrType = int(__fastcall*)(__int64 server, __int64 address);
using R1OGetPasswordType = const char*(__fastcall*)(__int64 server);
using R1OStringTableContainsType = bool(__fastcall*)(__int64 thisptr, const char* name);
static R1OStringTableContainsType R1OStringTableContainsOriginal;
using R1OStringTableLookupType = char*(__fastcall*)(const char* name);
static R1OStringTableLookupType R1OStringTableLookupOriginal;
using R1OSVCServerInfoWriteToBufferType = char(__fastcall*)(__int64 message, __int64 bitBuffer);
static R1OSVCServerInfoWriteToBufferType R1OSVCServerInfoWriteToBufferOriginal;
using R1ONetMessageWriteToBufferType = char(__fastcall*)(__int64 message, __int64 bitBuffer);
static R1ONetMessageWriteToBufferType R1ONetMessageWriteToBufferOriginal;
using R1ONetMessageWriteHeaderType = bool(__fastcall*)(unsigned int id, __int64 bitBuffer);
static R1ONetMessageWriteHeaderType R1ONetMessageWriteHeaderOriginal;
using R1ONetMessageWritePreludeType = char(__fastcall*)(unsigned int id, const char* name, __int64 bitBuffer);
static R1ONetMessageWritePreludeType R1ONetMessageWritePreludeOriginal;
using R1ONetMessageWriteTrailerType = __int64(__fastcall*)(__int64 bitBuffer);
static R1ONetMessageWriteTrailerType R1ONetMessageWriteTrailerOriginal;
using R1OHostStateFrameType = __int64(__fastcall*)();
static R1OHostStateFrameType R1OHostStateFrameOriginal;
using R1OHostSetActiveClientListType = __int64(__fastcall*)(__int64 list, unsigned char active);
static R1OHostSetActiveClientListType R1OHostSetActiveClientListOriginal;
using R1OHostShutdownClientListType = __int64(__fastcall*)(__int64 list);
static R1OHostShutdownClientListType R1OHostShutdownClientListOriginal;
static bool s_R1ODedicatedServerAPIHooked;
static bool s_VStdLibICVarFactoryHooked;
static bool s_R1ODedicatedServerAPIConnected;
static bool s_R1OStartupCommandsQueued;
static bool s_R1OLoggedFirstRunFrame;
static bool s_R1OCommandBuffersDirty;
static bool s_R1OLoggedMapDispatch;
static float s_R1OHostNextTick;
static int s_R1OCommandBufferExecuteLogBudget = 8;
static bool s_R1ONetChanProcessPacketHooked;
static bool s_R1ONetChanSendNetMsgHooked;
static bool s_R1ONetMessageRegistryDumped;
static bool s_R1ONETSetConVarReadHooked;
static bool s_R1OServerNetMessageGetIdOverridesInstalled;
// Keep the R1O fake-dedi path playable by default. These diagnostics are useful
// for bringup, but the hot-path formatting/log I/O is too expensive for gameplay.
// Fatal script/error reporting remains separately enabled below.
static int s_R1ONetChanProcessPacketLogBudget = 0;
static int s_R1ONetChanSendNetMsgLogBudget = 0;
static int s_R1ONetMessageLogBudget = 0;
static int s_R1ONetMessageEmptyDispatchLogBudget = 0;
static int s_R1ONetMessageProcessLogBudget = 0;
static int s_R1OStringCmdContentLogBudget = 0;
static int s_R1ONETSetConVarReadLogBudget = 0;
static int s_R1OServerNetMessageGetTypeLogBudget = 0;
static int s_R1ONetMessageWriteHeaderLogBudget = 0;
static int s_R1OSnapshotWriteLogBudget = 0;
static int s_R1ONetMessageWritePreludeLogBudget = 0;
static int s_R1ONetMessageWriteTrailerLogBudget = 0;
static int s_R1OServerInfoForceLogBudget = 0;
static int s_R1OSVCServerInfoWriteLogBudget = 0;
static int s_R1ONetChanSendDataLogBudget = 0;
static int s_R1OSignonStateTraceLogBudget = 0;
static int s_R1OSignonStateHandlerLogBudget = 0;
static int s_R1OClientInfoHandlerLogBudget = 0;
static int s_R1OCLCMoveLegacyReadLogBudget = 0;
static int s_R1OSendTableEncodeLogBudget = 0;
static int s_R1OSendTableEncodeHeaderLogBudget = 0;
static int s_R1ODeltaPropIndexWriteLogBudget = 0;
static int s_R1ODeltaPropIndexWritePhysicsWindowBudget = 0;
static int s_R1ODeltaPropIndexWritePlayerWindowBudget = 0;
static int s_R1ODeltaPropIndexWriteUnknownLogBudget = 0;
static int s_R1OEncodePropLogBudget = 0;
static int s_R1OCellEncodeTraceLogBudget = 0;
static int s_R1OWritePropListPlayerLogBudget = 0;
static int s_R1ODeltaCalculatorPlayerLogBudget = 0;
static int s_R1OPropCullPlayerLogBudget = 0;
static int s_R1OInstanceBaselineLogBudget = 0;
static int s_R1OPlayerNetpropCompareLogBudget = 0;
static int s_R1OPlayerNetpropCompareSeen;
static bool s_R1OLoggedHL2PlayerFlatProps;
static thread_local __int64 s_R1OSendTableEncodeCurrentTable;
static thread_local __int64 s_R1OSendTableEncodeCurrentObject;
static thread_local __int64 s_R1OSendTableEncodeCurrentBitBuffer;
static thread_local __int64 s_R1OSendTableEncodeCurrentOldState;
static thread_local int s_R1OSendTableEncodeCurrentObjectId = -1;
static thread_local int s_R1OSendTableEncodeCurrentStartBit = -1;
static thread_local char s_R1OSendTableEncodeCurrentDelta;
static thread_local __int64 s_R1OBuildPropListCurrentTable;
static thread_local int s_R1OBuildChangedPropListCurrentObjectId = -1;
static thread_local __int64 s_R1OPropCullCurrentTable;
static thread_local int s_R1ONetChanSendNetMsgDepth;
static thread_local __int64 s_R1ONetChanSendNetMsgMessage;
static thread_local int s_R1ONetMessageWriteToBufferDepth;
static thread_local __int64 s_R1ONetMessageWriteToBufferMessage;
static thread_local int s_R1ONetMessageDispatchDepth;
static thread_local __int64 s_R1ONetMessageActiveBitBuffer;
static thread_local int s_R1OConvertedConnectionlessConnectDepth;
static int s_R1OHostStateFrameLogBudget = 0;
static int s_R1OClientSlotLogBudget = 0;
static int s_R1OHibernateListGuardLogBudget = 8;
static bool s_R1OStaticConCommandsRegistered;
static bool s_R1OFakeMaterialDefaultInstalled;
static bool s_R1OMaterialLoadHooked;
static bool s_R1OWorldModelReleaseCallbackInstalled;
static bool s_R1OClientDllModelCacheDisabled;
static bool s_R1ODataCacheFileSystemGlobalInstalled;
static bool s_R1ODataCachePhysicsSurfacePropsGlobalInstalled;
static bool s_R1ODataCachePhysicsCollisionGlobalInstalled;
static bool s_R1ODataCacheStudioRenderGlobalInstalled;
static bool s_R1OLauncherFileSystemGlobalInstalled;
static bool s_R1OLauncherScriptFatalHooksInstalled;
static int s_R1OLauncherScriptFatalLogBudget = 32;
using R1OLauncherScriptFatalDispatchType = __int64(__fastcall*)();
static R1OLauncherScriptFatalDispatchType R1OLauncherScriptFatalDispatchOriginal;
static bool s_R1OStudioRenderStudioDataCacheGlobalInstalled;
static bool s_R1OStudioRenderMaterialSystemGlobalInstalled;
static bool s_R1OStudioRenderMaterialSystemHardwareConfigGlobalInstalled;
static bool s_R1ODedicatedNetworkModeLogged;
static HMODULE s_R1OTFOFileSystem;
static CreateInterfaceFn s_R1OTFOFileSystemFactory;
static HMODULE s_R1OTFOLauncher;
static CreateInterfaceFn s_R1OTFOLauncherFactory;
static void* s_R1OTFOFileSystem017;
static void* s_R1OTFOPhysicsSurfaceProps001;
static void* s_R1OTFOPhysicsCollision007;
static void* s_R1OTFOStudioRender026;
static void* s_R1OTFOStudioDataCache005;
static void* s_R1ODedicatedMaterialSystem083;
static void* s_R1ODedicatedMaterialSystemHardwareConfig015;
struct R1OTFOSupportModule {
	const char* moduleName;
	HMODULE module;
	CreateInterfaceFn factory;
};
static R1OTFOSupportModule s_R1OTFOSupportModules[] = {
	{ "datacache.dll", nullptr, nullptr },
	{ "studiorender.dll", nullptr, nullptr },
	{ "vphysics.dll", nullptr, nullptr },
	{ "vguimatsurface.dll", nullptr, nullptr },
};
static HMODULE s_R1OTFOLocalize;
static CreateInterfaceFn s_R1OTFOLocalizeFactory;
static void* s_R1OTFOLocalizeInterface;
static bool s_R1OTFOLocalizeConnected;
static bool s_R1OTFOLocalizeConnecting;
static std::recursive_mutex s_R1OTFOLocalizeMutex;
static HMODULE s_R1OTFOInputSystem;
static CreateInterfaceFn s_R1OTFOInputSystemFactory;
static void* s_R1OTFOInputSystemInterface;
static bool s_R1OTFOInputSystemConnected;
static bool s_R1OTFOInputSystemConnecting;
static std::recursive_mutex s_R1OTFOInputSystemMutex;
using R1OBaseFileSystem_ReadType = int(__fastcall*)(void* thisptr, void* output, int bytesToRead, void* handle);
using R1OBaseFileSystem_OpenType = void*(__fastcall*)(void* thisptr, const char* fileName, const char* options, const char* pathId, int flags);
using R1OBaseFileSystem_CloseType = void(__fastcall*)(void* thisptr, void* handle);
using R1OBaseFileSystem_SeekType = void(__fastcall*)(void* thisptr, void* handle, int offset, int origin);
using R1OBaseFileSystem_TellType = unsigned int(__fastcall*)(void* thisptr, void* handle);
using R1OBaseFileSystem_SizeType = unsigned int(__fastcall*)(void* thisptr, void* handle);
using R1OBaseFileSystem_SizeByNameType = unsigned int(__fastcall*)(void* thisptr, const char* fileName, const char* pathId);
using R1OBaseFileSystem_FileExistsType = bool(__fastcall*)(void* thisptr, const char* fileName, const char* pathId);
static R1OBaseFileSystem_ReadType R1OBaseFileSystem_ReadOriginal;
static R1OBaseFileSystem_OpenType R1OBaseFileSystem_OpenOriginal;
static R1OBaseFileSystem_CloseType R1OBaseFileSystem_CloseOriginal;
static R1OBaseFileSystem_SeekType R1OBaseFileSystem_SeekOriginal;
static R1OBaseFileSystem_TellType R1OBaseFileSystem_TellOriginal;
static R1OBaseFileSystem_SizeType R1OBaseFileSystem_SizeOriginal;
static R1OBaseFileSystem_SizeByNameType R1OBaseFileSystem_SizeByNameOriginal;
static R1OBaseFileSystem_FileExistsType R1OBaseFileSystem_FileExistsOriginal;
static bool s_R1OBaseFileSystemHooksInstalled;
using R1OFileSystem_OpenType = void*(__fastcall*)(void* thisptr, const char* fileName, const char* options, int flags, const char* pathId, __int64 unknown);
using R1OFileSystem_ReadExType = int(__fastcall*)(void* thisptr, void* output, int destinationSize, int bytesToRead, void* handle);
using R1OFileSystem_SetBufferSizeType = void(__fastcall*)(void* thisptr, void* handle, unsigned int bytes);
using R1OFileSystem_IsOkType = bool(__fastcall*)(void* thisptr, void* handle);
using R1OFileSystem_EndOfFileType = bool(__fastcall*)(void* thisptr, void* handle);
using R1OFileSystem_ReadLineType = char*(__fastcall*)(void* thisptr, char* output, int maxChars, void* handle);
static R1OFileSystem_OpenType R1OFileSystem_OpenOriginal;
static R1OFileSystem_ReadExType R1OFileSystem_ReadExOriginal;
static R1OFileSystem_SetBufferSizeType R1OFileSystem_SetBufferSizeOriginal;
static R1OFileSystem_IsOkType R1OFileSystem_IsOkOriginal;
static R1OFileSystem_EndOfFileType R1OFileSystem_EndOfFileOriginal;
static R1OFileSystem_ReadLineType R1OFileSystem_ReadLineOriginal;
static bool s_R1OFileSystemHooksInstalled;
static bool s_R1OVPKMemoryFilesEnabled;
static thread_local unsigned int s_R1OVPKClientFallbackDepth;
static bool s_R1OTFOFileSystemReplacementHooksInstalled;
using R1OMaterialSystem_FindOrLoadMaterialType = void*(__fastcall*)(void* thisptr, const char* materialName, const char* pathId, __int64 arg3, __int64 arg4, __int64 arg5, __int64 arg6);
static R1OMaterialSystem_FindOrLoadMaterialType R1OMaterialSystem_FindOrLoadMaterialSlot117Original;
static R1OMaterialSystem_FindOrLoadMaterialType R1OMaterialSystem_FindOrLoadMaterialSlot118Original;
static bool IsReadableRange(const void* ptr, size_t size);
static bool IsReadableProtect(DWORD protect);
static bool IsReadableCString(const char* ptr);
static void CopyReadableStringForDebug(const char* source, char* dest, size_t destSize);
static void EnsureR1OClientOnlyGlobalsForDedi();
static void EnsureR1ODedicatedWorldModelFallbacks();
static void EnsureR1ODedicatedClientDllModelFallback();
static void EnsureR1ODataCacheFileSystemGlobal();
static void EnsureR1ODataCachePhysicsSurfacePropsGlobal();
static void EnsureR1ODataCachePhysicsCollisionGlobal();
static void EnsureR1ODataCacheStudioRenderGlobal();
static void EnsureR1OLauncherFileSystemGlobal();
static void EnsureR1OLauncherScriptFatalHooks();
static void EnsureR1OStudioRenderStudioDataCacheGlobal();
static void EnsureR1OStudioRenderMaterialSystemGlobal();
static void EnsureR1OStudioRenderMaterialSystemHardwareConfigGlobal();
static void* EnsureR1OWrappedCVarInterfaceReady();
static void EnsureR1ODedicatedMaterialFallbacks();
static void EnsureR1ONetChanSendNetMsgHook(__int64 netChan);
static void* R1OTFOFileSystemInterface(const char* name, int* returnCode);
static void* R1OTFOSupportModuleInterface(const char* name, int* returnCode);
static void* R1OQueryLoadedModuleFactories(const char* name, int* returnCode);
static HMODULE LoadR1OTFOSupportModule(const char* moduleName);
static HMODULE LoadR1ODedicatedMaterialSystemProxy();
static void DebugR1ODediFactoryResult(const char* source, const char* name, void* result, int* returnCode);

constexpr uintptr_t kR1OClientArrayRva = 0x2659738;
constexpr size_t kR1OClientStride = 22387ull * sizeof(uintptr_t);
constexpr uintptr_t kR1OCGameClientVtableRva = 0x55C7D8;
constexpr ptrdiff_t kR1OClientSubobjectOffset = 8;
constexpr ptrdiff_t kR1OClientSignonStateOffset = 1144;
constexpr ptrdiff_t kR1OClientPendingServerInfoOffset = 1041;
constexpr ptrdiff_t kR1OClientNetChanOffset = 1128;
constexpr ptrdiff_t kR1OClientSpawnedOffset = 18176;

static bool PatchR1OBytesIfMatch(uintptr_t engineBase, uintptr_t rva, const unsigned char* expected, const unsigned char* replacement, size_t size, const char* reason)
{
	if (!IsR1ODedicatedServer() || !engineBase || !expected || !replacement || !size)
		return false;

	void* const address = reinterpret_cast<void*>(engineBase + rva);
	if (!IsReadableRange(address, size) || memcmp(address, expected, size) != 0) {
		char buffer[256];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: skipped R1O patch %s at rva=0x%llx because bytes did not match\n",
			reason,
			static_cast<unsigned long long>(rva));
		OutputDebugStringA(buffer);
		return false;
	}

	DWORD oldProtect = 0;
	if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
		char buffer[256];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: failed R1O patch %s at rva=0x%llx VirtualProtect gle=%lu\n",
			reason,
			static_cast<unsigned long long>(rva),
			GetLastError());
		OutputDebugStringA(buffer);
		return false;
	}

	memcpy(address, replacement, size);
	FlushInstructionCache(GetCurrentProcess(), address, size);

	DWORD ignoredProtect = 0;
	VirtualProtect(address, size, oldProtect, &ignoredProtect);

	char buffer[256];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: patched R1O %s at rva=0x%llx\n",
		reason,
		static_cast<unsigned long long>(rva));
	OutputDebugStringA(buffer);
	return true;
}

static bool PatchModuleBytesIfMatch(uintptr_t moduleBase, uintptr_t rva, const unsigned char* expected, const unsigned char* replacement, size_t size, const char* reason)
{
	if (!moduleBase || !expected || !replacement || !size)
		return false;

	void* const address = reinterpret_cast<void*>(moduleBase + rva);
	if (!IsReadableRange(address, size))
		return false;

	if (memcmp(address, replacement, size) == 0)
		return true;

	if (memcmp(address, expected, size) != 0) {
		char buffer[256];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: skipped patch %s at rva=0x%llx because bytes did not match\n",
			reason,
			static_cast<unsigned long long>(rva));
		OutputDebugStringA(buffer);
		return false;
	}

	DWORD oldProtect = 0;
	if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
		char buffer[256];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: failed patch %s at rva=0x%llx VirtualProtect gle=%lu\n",
			reason,
			static_cast<unsigned long long>(rva),
			GetLastError());
		OutputDebugStringA(buffer);
		return false;
	}

	memcpy(address, replacement, size);
	FlushInstructionCache(GetCurrentProcess(), address, size);

	DWORD ignoredProtect = 0;
	VirtualProtect(address, size, oldProtect, &ignoredProtect);
	return true;
}

using MaterialSystemDx11SelectShaderResourceType = void(__fastcall*)(int slot);
using MaterialSystemDx11SelectShaderStageResourceType = char(__fastcall*)(unsigned int stage, unsigned int slot, unsigned char* shaderType);
using MaterialSystemDx11CreateInputLayoutType = __int64(__fastcall*)(unsigned __int64 vertexFormat, __int64 streamFormat, __int64 shaderBytecodeProvider);
using MaterialSystemDx11CreateTexture2DResourceType = __int64(__fastcall*)(__int64* texture, int width, int height, int format, int mipLevels, unsigned int arraySize, int flags, __int64 initialData);
using MaterialSystemDx11LoadTextureType = __int64(__fastcall*)(__int64 texture, __int64* textureData);
using MaterialSystemDx11ResetD3DResourcePointersType = void(__fastcall*)();
using MaterialSystemDx11FlushConstantBufferUpdatesType = __int64(__fastcall*)();
using MaterialSystemDx11InitializeMaterialType = __int64(__fastcall*)(__int64 material, __int64 vmt, __int64 vmtPatches, __int64 context);
using MaterialSystemDx11IsErrorMaterialType = __int64(__fastcall*)(__int64 material);
static MaterialSystemDx11SelectShaderResourceType MaterialSystemDx11SelectShaderResourceOriginal;
static MaterialSystemDx11SelectShaderStageResourceType MaterialSystemDx11SelectShaderStageResourceOriginal;
static MaterialSystemDx11CreateInputLayoutType MaterialSystemDx11CreateInputLayoutOriginal;
static MaterialSystemDx11CreateTexture2DResourceType MaterialSystemDx11CreateTexture2DResourceOriginal;
static MaterialSystemDx11LoadTextureType MaterialSystemDx11LoadTextureOriginal;
static MaterialSystemDx11ResetD3DResourcePointersType MaterialSystemDx11ResetD3DResourcePointersOriginal;
static MaterialSystemDx11FlushConstantBufferUpdatesType MaterialSystemDx11FlushConstantBufferUpdatesOriginal;
static MaterialSystemDx11InitializeMaterialType MaterialSystemDx11InitializeMaterialOriginal;
static MaterialSystemDx11IsErrorMaterialType MaterialSystemDx11IsErrorMaterialOriginal;
using MaterialSystemDx11BoolPropertyType = __int64(__fastcall*)(__int64 material);
static MaterialSystemDx11BoolPropertyType MaterialSystemDx11PropertyGetterOriginal[5]{};
static bool s_MaterialSystemDx11NullResourceGuardInstalled;
static uintptr_t s_MaterialSystemDx11Base;
static int s_MaterialSystemDx11NullResourceLogBudget = 8;
static int s_MaterialSystemDx11TextureInitialDataLogBudget = 8;
static int s_MaterialSystemDx11ResetResourceLogBudget = 8;
static int s_MaterialSystemDx11ConstantBufferLogBudget = 8;
static int s_MaterialSystemDx11InputLayoutLogBudget = 8;
static int s_MaterialSystemDx11MaterialInitLogBudget = 8;
static std::recursive_mutex s_MaterialSystemDx11MaterialInitMutex;
static r1delta::materialsystem_dx11::TextureLoadScratchBufferGate s_MaterialSystemDx11TextureLoadGate;
static bool MaterialSystemDx11LooksLikeUserRange(uintptr_t address, size_t size);

static void MaterialSystemDx11LogMaterialInitGuard(const char* reason, __int64 material)
{
	if (s_MaterialSystemDx11MaterialInitLogBudget <= 0)
		return;

	--s_MaterialSystemDx11MaterialInitLogBudget;
	char buffer[256];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: materialsystem_dx11 material-init guard reason=%s material=%p\n",
		reason,
		reinterpret_cast<void*>(material));
	OutputDebugStringA(buffer);
}

static __int64 __fastcall MaterialSystemDx11InitializeMaterialGuard(
	__int64 material,
	__int64 vmt,
	__int64 vmtPatches,
	__int64 context)
{
	if (!MaterialSystemDx11InitializeMaterialOriginal || !material)
		return 0;

	std::lock_guard<std::recursive_mutex> lock(s_MaterialSystemDx11MaterialInitMutex);
	const __int64 result = MaterialSystemDx11InitializeMaterialOriginal(material, vmt, vmtPatches, context);
	if (result)
		return result;

	const bool markedInitialized = (*reinterpret_cast<unsigned short*>(material + 0x26) & 0x4u) != 0;
	const bool hasShader = *reinterpret_cast<uintptr_t*>(material + 0x10) != 0;
	if (markedInitialized && hasShader) {
		MaterialSystemDx11LogMaterialInitGuard("initialize-partial-proxies", material);
		return 1;
	}

	*reinterpret_cast<unsigned short*>(material + 0x26) &= static_cast<unsigned short>(~0x4u);
	MaterialSystemDx11LogMaterialInitGuard("initialize-failed", material);
	return 0;
}

static __int64 __fastcall MaterialSystemDx11IsErrorMaterialGuard(__int64 material)
{
	if (!MaterialSystemDx11IsErrorMaterialOriginal || !MaterialSystemDx11InitializeMaterialOriginal || !material)
		return 1;

	std::lock_guard<std::recursive_mutex> lock(s_MaterialSystemDx11MaterialInitMutex);
	if ((*reinterpret_cast<unsigned short*>(material + 0x26) & 0x4u) == 0
		&& !MaterialSystemDx11InitializeMaterialGuard(material, 0, 0, 0)) {
		MaterialSystemDx11LogMaterialInitGuard("error-query-init-failed", material);
		return 1;
	}
	return MaterialSystemDx11IsErrorMaterialOriginal(material);
}

template <size_t Index>
static __int64 __fastcall MaterialSystemDx11BoolPropertyGuard(__int64 material)
{
	static_assert(Index < std::size(MaterialSystemDx11PropertyGetterOriginal));
	MaterialSystemDx11BoolPropertyType const original = MaterialSystemDx11PropertyGetterOriginal[Index];
	if (!original || !MaterialSystemDx11InitializeMaterialOriginal || !material)
		return 0;

	std::lock_guard<std::recursive_mutex> lock(s_MaterialSystemDx11MaterialInitMutex);
	if ((*reinterpret_cast<unsigned short*>(material + 0x26) & 0x4u) == 0
		&& !MaterialSystemDx11InitializeMaterialGuard(material, 0, 0, 0)) {
		MaterialSystemDx11LogMaterialInitGuard("property-query-init-failed", material);
		return 0;
	}
	return original(material);
}

static int MaterialSystemDx11NullResourceExceptionFilter(EXCEPTION_POINTERS* info)
{
	if (!info || !info->ExceptionRecord || info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
		return EXCEPTION_CONTINUE_SEARCH;

	if (info->ExceptionRecord->NumberParameters >= 2) {
		const ULONG_PTR faultAddress = info->ExceptionRecord->ExceptionInformation[1];
		if (faultAddress >= 0x10000)
			return EXCEPTION_CONTINUE_SEARCH;
	}

	return EXCEPTION_EXECUTE_HANDLER;
}

static bool MaterialSystemDx11LooksLikeUserRange(uintptr_t address, size_t size)
{
	constexpr uintptr_t kMinUserAddress = 0x10000;
	constexpr uintptr_t kMaxUserAddress = 0x00007FFFFFFFFFFF;
	if (!size || address < kMinUserAddress || address > kMaxUserAddress)
		return false;
	return size - 1 <= kMaxUserAddress - address;
}

static bool MaterialSystemDx11LooksLikeUserPointer(uintptr_t address)
{
	return MaterialSystemDx11LooksLikeUserRange(address, sizeof(void*));
}

static bool MaterialSystemDx11ClearShaderStageCache(unsigned int stage)
{
	if (!s_MaterialSystemDx11Base || stage > 5)
		return false;

	auto* currentShader = reinterpret_cast<void**>(s_MaterialSystemDx11Base + 0x2A9810 + sizeof(void*) * stage);
	const bool changed = *currentShader != nullptr;
	*currentShader = nullptr;
	return changed;
}

static void MaterialSystemDx11LogNullResourceGuard(const char* helper, unsigned int stage, unsigned int slot, bool changed)
{
	if (s_MaterialSystemDx11NullResourceLogBudget <= 0)
		return;

	--s_MaterialSystemDx11NullResourceLogBudget;
	char buffer[320];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: materialsystem_dx11 shader-resource guard helper=%s stage=%u slot=%u changed=%d\n",
		helper,
		stage,
		slot,
		changed ? 1 : 0);
	OutputDebugStringA(buffer);
}

static bool MaterialSystemDx11SetShaderStageCache(unsigned int stage, void* shader, bool* changed)
{
	if (changed)
		*changed = false;
	if (!s_MaterialSystemDx11Base || stage > 5)
		return false;

	auto* currentShader = reinterpret_cast<void**>(s_MaterialSystemDx11Base + 0x2A9810 + sizeof(void*) * stage);
	if (changed)
		*changed = *currentShader != shader;
	*currentShader = shader;
	return true;
}

static unsigned short MaterialSystemDx11CurrentShaderHandle(unsigned int stage)
{
	if (!s_MaterialSystemDx11Base)
		return 0xFFFF;

	const auto* frameIndex = reinterpret_cast<const unsigned short*>(s_MaterialSystemDx11Base + 0x37A898);
	const size_t handleIndex = static_cast<size_t>(*frameIndex) * 24 + stage;
	const auto* handles = reinterpret_cast<const unsigned short*>(s_MaterialSystemDx11Base + 0x2AA3EC);
	return handles[handleIndex];
}

static bool MaterialSystemDx11ResolveShaderStageResource(unsigned int stage, unsigned int slot, void** shader, unsigned char* shaderType, bool* changed)
{
	if (shader)
		*shader = nullptr;
	if (shaderType)
		*shaderType = 0xFF;
	if (changed)
		*changed = false;
	if (!s_MaterialSystemDx11Base || stage > 5 || !shader)
		return false;

	const unsigned short handle = MaterialSystemDx11CurrentShaderHandle(stage);
	if (handle == 0xFFFF)
		return MaterialSystemDx11SetShaderStageCache(stage, nullptr, changed);

	auto* tablePtrSlot = reinterpret_cast<uintptr_t*>(s_MaterialSystemDx11Base + 0x2A9840 + 0x28 * stage);
	auto* hashValuePtr = reinterpret_cast<unsigned int*>(s_MaterialSystemDx11Base + 0x2A984C + 0x28 * stage);

	const uintptr_t table = *tablePtrSlot;
	const unsigned int hashValue = *hashValuePtr;
	const int shift = static_cast<int>(static_cast<int32_t>(hashValue) >> 27);
	if (!MaterialSystemDx11LooksLikeUserPointer(table) || shift < 0 || shift > 63) {
		const bool ok = MaterialSystemDx11SetShaderStageCache(stage, nullptr, changed);
		MaterialSystemDx11LogNullResourceGuard("table-invalid", stage, slot, changed ? *changed : false);
		return ok;
	}

	const size_t bucketIndex = static_cast<size_t>(handle) >> shift;
	const auto* bucketPtr = reinterpret_cast<const uintptr_t*>(table + sizeof(uintptr_t) * bucketIndex);
	if (!MaterialSystemDx11LooksLikeUserRange(reinterpret_cast<uintptr_t>(bucketPtr), sizeof(*bucketPtr)) || !*bucketPtr) {
		const bool ok = MaterialSystemDx11SetShaderStageCache(stage, nullptr, changed);
		MaterialSystemDx11LogNullResourceGuard("bucket-invalid", stage, slot, changed ? *changed : false);
		return ok;
	}

	const int64_t mask = (static_cast<int64_t>(static_cast<int32_t>(hashValue)) * 32) >> 5;
	if (mask < 0) {
		const bool ok = MaterialSystemDx11SetShaderStageCache(stage, nullptr, changed);
		MaterialSystemDx11LogNullResourceGuard("mask-invalid", stage, slot, changed ? *changed : false);
		return ok;
	}

	const uintptr_t entry = *bucketPtr + 32 * (static_cast<size_t>(handle) & static_cast<size_t>(mask));
	if (!MaterialSystemDx11LooksLikeUserRange(entry, 32)) {
		const bool ok = MaterialSystemDx11SetShaderStageCache(stage, nullptr, changed);
		MaterialSystemDx11LogNullResourceGuard("entry-invalid", stage, slot, changed ? *changed : false);
		return ok;
	}

	if ((*reinterpret_cast<unsigned char*>(entry + 16) & 2) != 0)
		return MaterialSystemDx11SetShaderStageCache(stage, nullptr, changed);

	const uintptr_t resourceArray = *reinterpret_cast<uintptr_t*>(entry + 8);
	const unsigned short shaderCount = *reinterpret_cast<unsigned short*>(entry + 2);
	const uintptr_t shaderSlot = resourceArray + sizeof(uintptr_t) * static_cast<size_t>(slot);
	if (!MaterialSystemDx11LooksLikeUserRange(shaderSlot, sizeof(uintptr_t))) {
		const bool ok = MaterialSystemDx11SetShaderStageCache(stage, nullptr, changed);
		MaterialSystemDx11LogNullResourceGuard("resource-invalid", stage, slot, changed ? *changed : false);
		return ok;
	}

	*shader = *reinterpret_cast<void**>(shaderSlot);
	if (stage == 1 && shaderType) {
		const uintptr_t typeSlot = resourceArray + sizeof(uintptr_t) * shaderCount + slot;
		if (MaterialSystemDx11LooksLikeUserRange(typeSlot, sizeof(unsigned char)))
			*shaderType = *reinterpret_cast<unsigned char*>(typeSlot);
	}

	return MaterialSystemDx11SetShaderStageCache(stage, *shader, changed);
}


static void MaterialSystemDx11BindVertexShaderIfChanged(void* shader, unsigned char shaderType, bool changed)
{
	if (!s_MaterialSystemDx11Base)
		return;

	auto* currentType = reinterpret_cast<unsigned char*>(s_MaterialSystemDx11Base + 0x2A99C0);
	if (changed) {
		auto* contextSlot = reinterpret_cast<void**>(s_MaterialSystemDx11Base + 0x290D90);
		void* context = *contextSlot;
		if (MaterialSystemDx11LooksLikeUserRange(reinterpret_cast<uintptr_t>(context), sizeof(void*))) {
			void** vtable = *reinterpret_cast<void***>(context);
			if (MaterialSystemDx11LooksLikeUserRange(reinterpret_cast<uintptr_t>(vtable + 11), sizeof(void*))) {
				using SetShaderFn = void(__fastcall*)(void*, void*, void*, unsigned int);
				reinterpret_cast<SetShaderFn>(vtable[11])(context, shader, nullptr, 0);
			}
		}
	}

	if (*currentType != shaderType) {
		*currentType = shaderType;
		auto* shaderTypeCookie = reinterpret_cast<uintptr_t*>(s_MaterialSystemDx11Base + 0x2820D0);
		*shaderTypeCookie = static_cast<uintptr_t>(-1);
	}
}

static void __fastcall MaterialSystemDx11SelectShaderResourceGuard(int slot)
{
	void* shader = nullptr;
	unsigned char shaderType = 0xFF;
	bool changed = false;
	if (MaterialSystemDx11ResolveShaderStageResource(1, static_cast<unsigned int>(slot), &shader, &shaderType, &changed))
		MaterialSystemDx11BindVertexShaderIfChanged(shader, shaderType, changed);
}

static char __fastcall MaterialSystemDx11SelectShaderStageResourceGuard(unsigned int stage, unsigned int slot, unsigned char* shaderType)
{
	void* shader = nullptr;
	unsigned char resolvedType = 0xFF;
	bool changed = false;
	if (MaterialSystemDx11ResolveShaderStageResource(stage, slot, &shader, &resolvedType, &changed)) {
		if (stage == 1 && shaderType)
			*shaderType = resolvedType;
		return changed ? 1 : 0;
	}

	return 0;
}

static void MaterialSystemDx11LogResetResourceGuard(const char* reason, uintptr_t pointerSlot, uintptr_t pointerToObject, uintptr_t object)
{
	if (s_MaterialSystemDx11ResetResourceLogBudget <= 0)
		return;

	--s_MaterialSystemDx11ResetResourceLogBudget;
	char buffer[320];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: materialsystem_dx11 reset-resource guard reason=%s slot=%p ptr=%p object=%p\n",
		reason,
		reinterpret_cast<void*>(pointerSlot),
		reinterpret_cast<void*>(pointerToObject),
		reinterpret_cast<void*>(object));
	OutputDebugStringA(buffer);
}

static void MaterialSystemDx11LogInputLayoutGuard(const char* reason, unsigned __int64 vertexFormat, __int64 streamFormat, __int64 shaderBytecodeProvider)
{
	if (s_MaterialSystemDx11InputLayoutLogBudget <= 0)
		return;

	--s_MaterialSystemDx11InputLayoutLogBudget;
	char buffer[320];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: materialsystem_dx11 input-layout guard reason=%s vertex=0x%llx stream=0x%llx shaderProvider=%p\n",
		reason,
		static_cast<unsigned long long>(vertexFormat),
		static_cast<unsigned long long>(streamFormat),
		reinterpret_cast<void*>(shaderBytecodeProvider));
	OutputDebugStringA(buffer);
}

static __int64 __fastcall MaterialSystemDx11CreateInputLayoutGuard(unsigned __int64 vertexFormat, __int64 streamFormat, __int64 shaderBytecodeProvider)
{
	if (!MaterialSystemDx11LooksLikeUserRange(static_cast<uintptr_t>(shaderBytecodeProvider), sizeof(void*))) {
		MaterialSystemDx11LogInputLayoutGuard("shader-provider-invalid", vertexFormat, streamFormat, shaderBytecodeProvider);
		return 0;
	}

	const auto* vtable = *reinterpret_cast<void***>(shaderBytecodeProvider);
	if (!MaterialSystemDx11LooksLikeUserRange(reinterpret_cast<uintptr_t>(vtable + 4), sizeof(void*))) {
		MaterialSystemDx11LogInputLayoutGuard("shader-provider-vtable-invalid", vertexFormat, streamFormat, shaderBytecodeProvider);
		return 0;
	}

	if (!MaterialSystemDx11CreateInputLayoutOriginal)
		return 0;

	return MaterialSystemDx11CreateInputLayoutOriginal(vertexFormat, streamFormat, shaderBytecodeProvider);
}

static void __fastcall MaterialSystemDx11ResetD3DResourcePointersGuard()
{
	if (!s_MaterialSystemDx11Base)
		return;

	const uintptr_t pointerSlot = s_MaterialSystemDx11Base + 0x2983F8;
	if (!IsReadableRange(reinterpret_cast<void*>(pointerSlot), sizeof(uintptr_t))) {
		MaterialSystemDx11LogResetResourceGuard("slot-unreadable", pointerSlot, 0, 0);
		return;
	}

	const uintptr_t pointerToObject = *reinterpret_cast<uintptr_t*>(pointerSlot);
	if (!pointerToObject || !IsReadableRange(reinterpret_cast<void*>(pointerToObject), sizeof(uintptr_t))) {
		MaterialSystemDx11LogResetResourceGuard("ptr-unreadable", pointerSlot, pointerToObject, 0);
		return;
	}

	const uintptr_t object = *reinterpret_cast<uintptr_t*>(pointerToObject);
	if (!object || !IsReadableRange(reinterpret_cast<void*>(object + 0x618), sizeof(uintptr_t))) {
		MaterialSystemDx11LogResetResourceGuard("object-unreadable", pointerSlot, pointerToObject, object);
		return;
	}

	__try {
		*reinterpret_cast<uintptr_t*>(object + 0x5D0) = 0;
		*reinterpret_cast<uintptr_t*>(object + 0x5B8) = 0;
		*reinterpret_cast<uintptr_t*>(object + 0x618) = 0;
		*reinterpret_cast<uintptr_t*>(object + 0x600) = 0;
	}
	__except (MaterialSystemDx11NullResourceExceptionFilter(GetExceptionInformation())) {
		MaterialSystemDx11LogResetResourceGuard("write-av", pointerSlot, pointerToObject, object);
	}
}

static int MaterialSystemDx11ConstantBufferExceptionFilter(EXCEPTION_POINTERS* info)
{
	if (!info || !info->ExceptionRecord || info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
		return EXCEPTION_CONTINUE_SEARCH;

	if (info->ExceptionRecord->NumberParameters >= 2) {
		const ULONG_PTR faultAddress = info->ExceptionRecord->ExceptionInformation[1];
		if (faultAddress < 0x10000)
			return EXCEPTION_EXECUTE_HANDLER;
	}

	if (s_MaterialSystemDx11Base) {
		const uintptr_t exceptionAddress = reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress);
		if (exceptionAddress >= s_MaterialSystemDx11Base + 0x1364E0
			&& exceptionAddress < s_MaterialSystemDx11Base + 0x136814)
			return EXCEPTION_EXECUTE_HANDLER;
	}

	return EXCEPTION_CONTINUE_SEARCH;
}

static void MaterialSystemDx11ClearConstantBufferUpdateState()
{
	if (!s_MaterialSystemDx11Base)
		return;

	for (unsigned int i = 0; i < 2; ++i) {
		auto* updateCount = reinterpret_cast<unsigned int*>(s_MaterialSystemDx11Base + 0x2A3860 + sizeof(unsigned int) * i);
		auto* commandCount = reinterpret_cast<unsigned int*>(s_MaterialSystemDx11Base + 0x2A3868 + sizeof(unsigned int) * i);
		if (IsReadableRange(updateCount, sizeof(*updateCount)))
			*updateCount = 0;
		if (IsReadableRange(commandCount, sizeof(*commandCount)))
			*commandCount = 0;
	}
}

static void MaterialSystemDx11LogConstantBufferGuard()
{
	if (s_MaterialSystemDx11ConstantBufferLogBudget <= 0)
		return;

	--s_MaterialSystemDx11ConstantBufferLogBudget;
	OutputDebugStringA("R1Delta: materialsystem_dx11 constant-buffer upload AV guarded and pending updates cleared\n");
}

static __int64 __fastcall MaterialSystemDx11FlushConstantBufferUpdatesGuard()
{
	if (!MaterialSystemDx11FlushConstantBufferUpdatesOriginal)
		return 0;

	__try {
		return MaterialSystemDx11FlushConstantBufferUpdatesOriginal();
	}
	__except (MaterialSystemDx11ConstantBufferExceptionFilter(GetExceptionInformation())) {
		MaterialSystemDx11ClearConstantBufferUpdateState();
		MaterialSystemDx11LogConstantBufferGuard();
		return 0;
	}
}

struct MaterialSystemDx11TextureInitialDataEntry {
	const void* sysMem;
	unsigned int sysMemPitch;
	unsigned int sysMemSlicePitch;
};

static bool MaterialSystemDx11MulSize(size_t a, size_t b, size_t* out)
{
	if (!out)
		return false;
	if (a && b > static_cast<size_t>(-1) / a)
		return false;
	*out = a * b;
	return true;
}

static constexpr size_t MaterialSystemDx11TextureArraySize(unsigned int arraySize, int flags)
{
	// D3D11 cube textures use exactly six array slices. The original
	// materialsystem replaces ArraySize with 6 for this flag; it does not
	// multiply the caller's ArraySize by 6.
	return (flags & 1) != 0 ? 6u : static_cast<size_t>(arraySize);
}

static_assert(MaterialSystemDx11TextureArraySize(1, 1) == 6);
static_assert(MaterialSystemDx11TextureArraySize(6, 1) == 6);
static_assert(MaterialSystemDx11TextureArraySize(4, 0) == 4);

static size_t MaterialSystemDx11TextureSubresourceCount(int mipLevels, unsigned int arraySize, int flags)
{
	if (mipLevels <= 0)
		return 0;

	const size_t effectiveArraySize = MaterialSystemDx11TextureArraySize(arraySize, flags);
	if (!effectiveArraySize)
		return 0;

	size_t count = 0;
	if (!MaterialSystemDx11MulSize(static_cast<size_t>(mipLevels), effectiveArraySize, &count))
		return 0;
	return count;
}

static size_t MaterialSystemDx11RequiredInitialDataBytes(const MaterialSystemDx11TextureInitialDataEntry& entry)
{
	size_t required = entry.sysMemSlicePitch;
	if (entry.sysMemPitch > required)
		required = entry.sysMemPitch;
	return required ? required : 1;
}

static void MaterialSystemDx11LogTextureInitialDataGuard(
	const char* reason,
	int width,
	int height,
	int format,
	int mipLevels,
	unsigned int arraySize,
	int flags,
	__int64 initialData,
	size_t subresourceCount,
	size_t zeroBytes)
{
	if (s_MaterialSystemDx11TextureInitialDataLogBudget <= 0)
		return;

	--s_MaterialSystemDx11TextureInitialDataLogBudget;
	char buffer[384];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: materialsystem_dx11 texture initial-data guard reason=%s %dx%d fmt=%d mips=%d array=%u flags=0x%x data=%p subresources=%llu zeroBytes=%llu\n",
		reason,
		width,
		height,
		format,
		mipLevels,
		arraySize,
		flags,
		reinterpret_cast<void*>(initialData),
		static_cast<unsigned long long>(subresourceCount),
		static_cast<unsigned long long>(zeroBytes));
	OutputDebugStringA(buffer);
}

static bool MaterialSystemDx11PrepareSafeTextureInitialData(
	int width,
	int height,
	int format,
	int mipLevels,
	unsigned int arraySize,
	int flags,
	__int64 initialData,
	std::vector<MaterialSystemDx11TextureInitialDataEntry>& safeEntries,
	std::vector<unsigned char>& zeroData,
	__int64* safeInitialData)
{
	if (safeInitialData)
		*safeInitialData = initialData;
	if (!safeInitialData || !initialData)
		return false;

	const size_t subresourceCount = MaterialSystemDx11TextureSubresourceCount(mipLevels, arraySize, flags);
	if (!subresourceCount || subresourceCount > 16384)
		return false;

	size_t entriesBytes = 0;
	if (!MaterialSystemDx11MulSize(subresourceCount, sizeof(MaterialSystemDx11TextureInitialDataEntry), &entriesBytes))
		return false;

	auto* initialEntries = reinterpret_cast<const MaterialSystemDx11TextureInitialDataEntry*>(initialData);
	if (!IsReadableRange(initialEntries, entriesBytes)) {
		*safeInitialData = 0;
		MaterialSystemDx11LogTextureInitialDataGuard("array-unreadable", width, height, format, mipLevels, arraySize, flags, initialData, subresourceCount, 0);
		return true;
	}

	safeEntries.assign(initialEntries, initialEntries + subresourceCount);
	std::vector<unsigned char> invalidEntries(subresourceCount, 0);
	bool changed = false;
	size_t maxZeroBytes = 0;
	for (size_t i = 0; i < subresourceCount; ++i) {
		const size_t required = MaterialSystemDx11RequiredInitialDataBytes(safeEntries[i]);
		if (!safeEntries[i].sysMem || !IsReadableRange(safeEntries[i].sysMem, required)) {
			invalidEntries[i] = 1;
			changed = true;
			if (required > maxZeroBytes)
				maxZeroBytes = required;
		}
	}

	if (!changed)
		return false;

	constexpr size_t kMaxZeroTextureInitialDataBytes = 64 * 1024 * 1024;
	if (!maxZeroBytes || maxZeroBytes > kMaxZeroTextureInitialDataBytes) {
		*safeInitialData = 0;
		MaterialSystemDx11LogTextureInitialDataGuard("entry-too-large", width, height, format, mipLevels, arraySize, flags, initialData, subresourceCount, maxZeroBytes);
		return true;
	}

	zeroData.assign(maxZeroBytes, 0);
	for (size_t i = 0; i < subresourceCount; ++i) {
		if (invalidEntries[i])
			safeEntries[i].sysMem = zeroData.data();
	}

	*safeInitialData = reinterpret_cast<__int64>(safeEntries.data());
	MaterialSystemDx11LogTextureInitialDataGuard("entry-unreadable", width, height, format, mipLevels, arraySize, flags, initialData, subresourceCount, maxZeroBytes);
	return true;
}

static __int64 __fastcall MaterialSystemDx11CreateTexture2DResourceGuard(
	__int64* texture,
	int width,
	int height,
	int format,
	int mipLevels,
	unsigned int arraySize,
	int flags,
	__int64 initialData)
{
	if (!MaterialSystemDx11CreateTexture2DResourceOriginal)
		return 0;

	std::vector<MaterialSystemDx11TextureInitialDataEntry> safeEntries;
	std::vector<unsigned char> zeroData;
	__int64 safeInitialData = initialData;
	MaterialSystemDx11PrepareSafeTextureInitialData(width, height, format, mipLevels, arraySize, flags, initialData, safeEntries, zeroData, &safeInitialData);

	return MaterialSystemDx11CreateTexture2DResourceOriginal(texture, width, height, format, mipLevels, arraySize, flags, safeInitialData);
}

static __int64 __fastcall MaterialSystemDx11LoadTextureGuard(__int64 texture, __int64* textureData)
{
	if (!MaterialSystemDx11LoadTextureOriginal)
		return 0;

	std::unique_lock<std::recursive_mutex> scratchBufferLock;
	try
	{
		scratchBufferLock = s_MaterialSystemDx11TextureLoadGate.Acquire();
	}
	catch (const std::system_error&)
	{
		// The gate is a scratch-buffer lifetime safety net. Never let a lock
		// failure close the game; proceed unlocked (pre-gate behavior).
		OutputDebugStringA("R1Delta: texture load gate lock failed; proceeding unlocked\n");
	}
	return MaterialSystemDx11LoadTextureOriginal(texture, textureData);
}

static bool InstallMaterialSystemDx11CheckedHook(
	uintptr_t materialSystemBase,
	uintptr_t rva,
	const unsigned char* expectedPrologue,
	size_t expectedPrologueSize,
	void* detour,
	void** original,
	const char* name)
{
	void* target = reinterpret_cast<void*>(materialSystemBase + rva);
	if (!IsReadableRange(target, expectedPrologueSize)
		|| memcmp(target, expectedPrologue, expectedPrologueSize) != 0) {
		char buffer[256];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: skipped materialsystem_dx11 %s guard because bytes did not match\n",
			name);
		OutputDebugStringA(buffer);
		return false;
	}

	const MH_STATUS createStatus = MH_CreateHook(target, detour, original);
	const bool hasOriginal = original && *original;
	const bool canEnable = createStatus == MH_OK
		|| (createStatus == MH_ERROR_ALREADY_CREATED && hasOriginal);
	const MH_STATUS enableStatus = canEnable ? MH_EnableHook(target) : createStatus;

	char buffer[256];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: materialsystem_dx11 %s guard create=%d enable=%d target=%p original=%p\n",
		name,
		static_cast<int>(createStatus),
		static_cast<int>(enableStatus),
		target,
		original ? *original : nullptr);
	OutputDebugStringA(buffer);

	return hasOriginal && (enableStatus == MH_OK || enableStatus == MH_ERROR_ENABLED);
}

void InstallMaterialSystemDx11NullShaderResourceGuard(uintptr_t materialSystemBase)
{
	if (s_MaterialSystemDx11NullResourceGuardInstalled || !materialSystemBase)
		return;

	s_MaterialSystemDx11Base = materialSystemBase;

	const unsigned char expectedMaterialInitPrologue[] = {
		0x40, 0x57,
		0x41, 0x55,
		0x48, 0x83, 0xEC, 0x48,
		0x48, 0x89, 0x5C, 0x24, 0x60,
		0x48, 0x89, 0x6C, 0x24, 0x68,
		0x48, 0x89, 0x74, 0x24, 0x70,
		0x4C, 0x89, 0x64, 0x24, 0x40
	};
	const unsigned char expectedIsErrorMaterialPrologue[] = {
		0x40, 0x53,
		0x48, 0x83, 0xEC, 0x20,
		0x0F, 0xB6, 0x41, 0x26,
		0x48, 0x8B, 0xD9,
		0xC0, 0xE8, 0x02,
		0xA8, 0x01
	};
	const unsigned char expectedResetPrologue[] = {
		0x48, 0x8B, 0x05, 0x81, 0x38, 0x28, 0x00,
		0x33, 0xD2,
		0x48, 0x8B, 0x08,
		0x48, 0x89, 0x91, 0xD0, 0x05, 0x00, 0x00
	};
	const unsigned char expectedConstantFlushPrologue[] = {
		0x48, 0x89, 0x5C, 0x24, 0x08,
		0x48, 0x89, 0x6C, 0x24, 0x10,
		0x48, 0x89, 0x74, 0x24, 0x18,
		0x48, 0x89, 0x7C, 0x24, 0x20,
		0x41, 0x54,
		0x41, 0x55,
		0x41, 0x56,
		0x48, 0x83, 0xEC, 0x40,
		0xB8, 0x01, 0x00, 0x00, 0x00
	};
	const unsigned char expectedInputLayoutPrologue[] = {
		0x48, 0x8B, 0xC4,
		0x4C, 0x89, 0x40, 0x18,
		0x55,
		0x53,
		0x56,
		0x57,
		0x41, 0x55,
		0x48, 0x8D, 0xA8, 0x98, 0xFC, 0xFF, 0xFF,
		0x48, 0x81, 0xEC, 0x40, 0x04, 0x00, 0x00,
		0x4C, 0x89, 0x60, 0x10
	};
	const unsigned char expectedTexturePrologue[] = {
		0x48, 0x89, 0x5C, 0x24, 0x08,
		0x44, 0x89, 0x44, 0x24, 0x18,
		0x89, 0x54, 0x24, 0x10,
		0x55,
		0x56,
		0x57,
		0x41, 0x54,
		0x41, 0x55,
		0x41, 0x56,
		0x41, 0x57,
		0x48, 0x8D, 0x6C, 0x24, 0xF0,
		0x48, 0x81, 0xEC, 0x10, 0x01, 0x00, 0x00
	};
	const unsigned char expectedTextureLoadPrologue[] = {
		0x40, 0x53,
		0x55,
		0x56,
		0x41, 0x54,
		0x48, 0x81, 0xEC, 0x38, 0x09, 0x00, 0x00,
		0x48, 0x8B, 0xF1,
		0x48, 0x8B, 0xDA,
		0x33, 0xED,
		0x48, 0x8D, 0x4C, 0x24, 0x21
	};
	const unsigned char expectedStagePrologue[] = {
		0x48, 0x89, 0x5C, 0x24, 0x08,
		0x48, 0x89, 0x6C, 0x24, 0x10,
		0x48, 0x89, 0x74, 0x24, 0x18,
		0x57,
		0x48, 0x83, 0xEC, 0x20,
		0x48, 0x63, 0xD9,
		0x49, 0x8B, 0xF8,
		0x8B, 0xF2,
		0x8B, 0xCB
	};
	const unsigned char expectedVertexPrologue[] = {
		0x48, 0x89, 0x5C, 0x24, 0x08,
		0x57,
		0x48, 0x83, 0xEC, 0x20,
		0x8B, 0xF9,
		0xB9, 0x01, 0x00, 0x00, 0x00,
		0xB3, 0xFF
	};
	const bool materialInitInstalled = InstallMaterialSystemDx11CheckedHook(
		materialSystemBase,
		0x3AB60,
		expectedMaterialInitPrologue,
		sizeof(expectedMaterialInitPrologue),
		reinterpret_cast<void*>(&MaterialSystemDx11InitializeMaterialGuard),
		reinterpret_cast<void**>(&MaterialSystemDx11InitializeMaterialOriginal),
		"material-initialize");
	const bool isErrorMaterialInstalled = materialInitInstalled && InstallMaterialSystemDx11CheckedHook(
		materialSystemBase,
		0x3B010,
		expectedIsErrorMaterialPrologue,
		sizeof(expectedIsErrorMaterialPrologue),
		reinterpret_cast<void*>(&MaterialSystemDx11IsErrorMaterialGuard),
		reinterpret_cast<void**>(&MaterialSystemDx11IsErrorMaterialOriginal),
		"material-is-error");
	const uintptr_t materialPropertyRvas[] = { 0x3AFB0, 0x3B070, 0x3B0D0, 0x3B120 };
	void* const materialPropertyGuards[] = {
		reinterpret_cast<void*>(&MaterialSystemDx11BoolPropertyGuard<0>),
		reinterpret_cast<void*>(&MaterialSystemDx11BoolPropertyGuard<1>),
		reinterpret_cast<void*>(&MaterialSystemDx11BoolPropertyGuard<2>),
		reinterpret_cast<void*>(&MaterialSystemDx11BoolPropertyGuard<3>)
	};
	bool materialPropertyGuardsInstalled = materialInitInstalled;
	for (size_t index = 0; index < std::size(materialPropertyRvas); ++index) {
		materialPropertyGuardsInstalled = InstallMaterialSystemDx11CheckedHook(
			materialSystemBase,
			materialPropertyRvas[index],
			expectedIsErrorMaterialPrologue,
			sizeof(expectedIsErrorMaterialPrologue),
			materialPropertyGuards[index],
			reinterpret_cast<void**>(&MaterialSystemDx11PropertyGetterOriginal[index]),
			"material-property") && materialPropertyGuardsInstalled;
	}
	const bool resetInstalled = InstallMaterialSystemDx11CheckedHook(
		materialSystemBase,
		0x14B70,
		expectedResetPrologue,
		sizeof(expectedResetPrologue),
		reinterpret_cast<void*>(&MaterialSystemDx11ResetD3DResourcePointersGuard),
		reinterpret_cast<void**>(&MaterialSystemDx11ResetD3DResourcePointersOriginal),
		"reset-resource");
	const bool constantFlushInstalled = InstallMaterialSystemDx11CheckedHook(
		materialSystemBase,
		0x184F0,
		expectedConstantFlushPrologue,
		sizeof(expectedConstantFlushPrologue),
		reinterpret_cast<void*>(&MaterialSystemDx11FlushConstantBufferUpdatesGuard),
		reinterpret_cast<void**>(&MaterialSystemDx11FlushConstantBufferUpdatesOriginal),
		"constant-buffer");
	const bool textureInstalled = InstallMaterialSystemDx11CheckedHook(
		materialSystemBase,
		0x132B0,
		expectedTexturePrologue,
		sizeof(expectedTexturePrologue),
		reinterpret_cast<void*>(&MaterialSystemDx11CreateTexture2DResourceGuard),
		reinterpret_cast<void**>(&MaterialSystemDx11CreateTexture2DResourceOriginal),
		"texture-initial-data");
	const bool textureLoadInstalled = InstallMaterialSystemDx11CheckedHook(
		materialSystemBase,
		0x6C340,
		expectedTextureLoadPrologue,
		sizeof(expectedTextureLoadPrologue),
		reinterpret_cast<void*>(&MaterialSystemDx11LoadTextureGuard),
		reinterpret_cast<void**>(&MaterialSystemDx11LoadTextureOriginal),
		"texture-load-lifetime");
	const bool inputLayoutInstalled = InstallMaterialSystemDx11CheckedHook(
		materialSystemBase,
		0x1B020,
		expectedInputLayoutPrologue,
		sizeof(expectedInputLayoutPrologue),
		reinterpret_cast<void*>(&MaterialSystemDx11CreateInputLayoutGuard),
		reinterpret_cast<void**>(&MaterialSystemDx11CreateInputLayoutOriginal),
		"input-layout");
	const bool stageInstalled = InstallMaterialSystemDx11CheckedHook(
		materialSystemBase,
		0x1DAF0,
		expectedStagePrologue,
		sizeof(expectedStagePrologue),
		reinterpret_cast<void*>(&MaterialSystemDx11SelectShaderStageResourceGuard),
		reinterpret_cast<void**>(&MaterialSystemDx11SelectShaderStageResourceOriginal),
		"stage");
	const bool vertexInstalled = InstallMaterialSystemDx11CheckedHook(
		materialSystemBase,
		0x1DBC0,
		expectedVertexPrologue,
		sizeof(expectedVertexPrologue),
		reinterpret_cast<void*>(&MaterialSystemDx11SelectShaderResourceGuard),
		reinterpret_cast<void**>(&MaterialSystemDx11SelectShaderResourceOriginal),
		"vertex");
	s_MaterialSystemDx11NullResourceGuardInstalled = materialInitInstalled || isErrorMaterialInstalled || materialPropertyGuardsInstalled || resetInstalled || constantFlushInstalled || textureInstalled || textureLoadInstalled || inputLayoutInstalled || stageInstalled || vertexInstalled;
}

static bool IsVPhysicsGuardNegativeStackIndexDisabled()
{
	static const bool disabled = []() {
		if (HasEngineCommandLineFlag("-r1delta_vphysics_no_negative_stack_guard"))
			return true;

		const char* commandLine = GetCommandLineA();
		return commandLine && strstr(commandLine, "-r1delta_vphysics_no_negative_stack_guard") != nullptr;
	}();
	return disabled;
}

static bool IsVPhysicsDiagnosticsEnabled()
{
	static const bool enabled = []() {
		static const char* const flags[] = {
			"-r1delta_vphysics_bvh_probe",
			"-r1delta_vphysics_bvh_patch_clear",
			"-r1delta_vphysics_bvh_include_not_sim",
			"-r1delta_vphysics_revive_on_collision_enable",
			"-r1delta_vphysics_force_selector_tick",
			"-r1delta_vphysics_guard_page_trace",
			"-r1delta_vphysics_defer_logs",
		};

		const char* commandLine = GetCommandLineA();
		for (const char* flag : flags) {
			if (HasEngineCommandLineFlag(flag) || (commandLine && strstr(commandLine, flag)))
				return true;
		}
		return false;
	}();
	return enabled;
}

static bool IsVPhysicsStaticBVHProbeEnabled()
{
	return !IsVPhysicsGuardNegativeStackIndexDisabled() || IsVPhysicsDiagnosticsEnabled();
}

static bool IsVPhysicsStaticBVHClearPatchEnabled()
{
	static const bool enabled = []() {
		if (HasEngineCommandLineFlag("-r1delta_vphysics_bvh_patch_clear"))
			return true;

		const char* commandLine = GetCommandLineA();
		return commandLine && strstr(commandLine, "-r1delta_vphysics_bvh_patch_clear") != nullptr;
	}();
	return enabled;
}

static bool IsVPhysicsStaticBVHIncludeNotSimPatchEnabled()
{
	static const bool enabled = []() {
		if (HasEngineCommandLineFlag("-r1delta_vphysics_bvh_include_not_sim"))
			return true;

		const char* commandLine = GetCommandLineA();
		return commandLine && strstr(commandLine, "-r1delta_vphysics_bvh_include_not_sim") != nullptr;
	}();
	return enabled;
}

static bool IsVPhysicsReviveOnCollisionEnablePatchEnabled()
{
	static const bool enabled = []() {
		if (HasEngineCommandLineFlag("-r1delta_vphysics_revive_on_collision_enable"))
			return true;

		const char* commandLine = GetCommandLineA();
		return commandLine && strstr(commandLine, "-r1delta_vphysics_revive_on_collision_enable") != nullptr;
	}();
	return enabled;
}

static bool IsVPhysicsForceSelectorTickEnabled()
{
	if (!IsR1ODedicatedServer())
		return false;

	static const bool enabled = []() {
		if (HasEngineCommandLineFlag("-r1delta_vphysics_force_selector_tick"))
			return true;

		const char* commandLine = GetCommandLineA();
		return commandLine && strstr(commandLine, "-r1delta_vphysics_force_selector_tick") != nullptr;
	}();
	return enabled;
}

static bool IsVPhysicsGuardNegativeStackIndexEnabled()
{
	return !IsVPhysicsGuardNegativeStackIndexDisabled();
}

static bool IsVPhysicsGuardPageTraceEnabled()
{
	if (!IsR1ODedicatedServer())
		return false;

	static const bool enabled = []() {
		if (HasEngineCommandLineFlag("-r1delta_vphysics_guard_page_trace"))
			return true;

		const char* commandLine = GetCommandLineA();
		return commandLine && strstr(commandLine, "-r1delta_vphysics_guard_page_trace") != nullptr;
	}();
	return enabled;
}

static int VPhysicsGuardPageTraceLogBudget()
{
	const char* commandLine = GetCommandLineA();
	if (!commandLine)
		return 2048;

	const char* token = strstr(commandLine, "-r1delta_vphysics_guard_page_trace_budget");
	if (!token)
		token = strstr(commandLine, "+r1delta_vphysics_guard_page_trace_budget");
	if (!token)
		return 2048;

	token += strlen("-r1delta_vphysics_guard_page_trace_budget");
	while (*token == ' ' || *token == '\t' || *token == '=')
		++token;

	const int budget = atoi(token);
	return budget > 0 ? budget : 2048;
}

static bool IsVPhysicsDeferredProbeLogEnabled()
{
	if (!IsR1ODedicatedServer())
		return false;

	static const bool enabled = []() {
		if (HasEngineCommandLineFlag("-r1delta_vphysics_defer_logs"))
			return true;

		const char* commandLine = GetCommandLineA();
		return commandLine && strstr(commandLine, "-r1delta_vphysics_defer_logs") != nullptr;
	}();
	return enabled;
}

static int VPhysicsDeferredProbeLogFlushFrame()
{
	const char* commandLine = GetCommandLineA();
	if (!commandLine)
		return 36000;

	const char* token = strstr(commandLine, "-r1delta_vphysics_defer_flush_frame");
	if (!token)
		token = strstr(commandLine, "+r1delta_vphysics_defer_flush_frame");
	if (!token)
		return 36000;

	token += strlen("-r1delta_vphysics_defer_flush_frame");
	while (*token == ' ' || *token == '\t' || *token == '=')
		++token;

	const int frame = atoi(token);
	return frame > 0 ? frame : 36000;
}

using VPhysicsStaticBVHHelperType = int(__fastcall*)(__int64 rangeData, __int64 object, float* center, float radius, __int64 ledges);
using VPhysicsObjectPairUpdateType = void(__fastcall*)(__int64 rangeData, __int64 object, __int64 outputPairs);
using VPhysicsStaticBVHFilterType = void(__fastcall*)(__int64 rangeData, __int64 object, __int64 staticCandidates, __int64 outputCandidates, int* outputStartIndex);
using VPhysicsExistingPairType = __int64(__fastcall*)(__int64* hashState, __int64 candidate, __int64 object);
using VPhysicsCollisionFilterShouldCollideType = bool(__fastcall*)(__int64 filter, __int64 object, __int64 candidate);
using VPhysicsStaticBVHRebuildCallbackType = __int64(__fastcall*)(__int64 callback);
using VPhysicsSimUnitReviveType = __int16(__fastcall*)(__int64 simUnit, __int64 environment);
using VPhysicsCoreStopMovementType = __int64(__fastcall*)(__int64 core);
using VPhysicsCoreInitSimulationType = __int64(__fastcall*)(__int64 core);
using VPhysicsCoreReviveSimulationType = __int64(__fastcall*)(__int64 core);
using VPhysicsObjectRefreshMindistsType = __int16(__fastcall*)(__int64 object);
using VPhysicsCollisionEnableType = __int64(__fastcall*)(__int64 ovElement, char enable);
using VPhysicsCollisionRecheckType = __int64(__fastcall*)(__int64 ovElement);
using VPhysicsNewPairInitType = __int64(__fastcall*)(__int64 rangeData, __int64 pair, __int64 objectA, __int64 objectB);
using VPhysicsUpdateExactMindistEventsType = void(__fastcall*)(__int64 mindist, int allowHullConversion, int eventHint);
using VPhysicsUpdateExactMindistDeferredType = __int64(__fastcall*)(__int64 manager, int mode, __int64 context);
using VPhysicsDeferredFlushType = __int64(__fastcall*)(__int64 manager, int mask);
using VPhysicsDoImpactType = __int64(__fastcall*)(__int64 mindist);
using VPhysicsExactToHullType = void(__fastcall*)(__int64 mindist, float hullTime0, float hullTime1);
using VPhysicsMindistEventType = void(__fastcall*)(__int64 mindist, float eventTime);
static VPhysicsStaticBVHHelperType VPhysicsStaticBVHHelperOriginal;
static VPhysicsObjectPairUpdateType VPhysicsObjectPairUpdateOriginal;
static VPhysicsStaticBVHFilterType VPhysicsStaticBVHFilterOriginal;
static VPhysicsExistingPairType VPhysicsExistingPairOriginal;
static VPhysicsCollisionFilterShouldCollideType VPhysicsCollisionFilterShouldCollideOriginal;
static VPhysicsStaticBVHRebuildCallbackType VPhysicsStaticBVHRebuildCallbackOriginal;
static VPhysicsSimUnitReviveType VPhysicsSimUnitReviveOriginal;
static VPhysicsCoreStopMovementType VPhysicsCoreStopMovementOriginal;
static VPhysicsCoreInitSimulationType VPhysicsCoreInitSimulationOriginal;
static VPhysicsCoreReviveSimulationType VPhysicsCoreReviveSimulationOriginal;
static VPhysicsObjectRefreshMindistsType VPhysicsObjectRefreshMindistsOriginal;
static VPhysicsCollisionEnableType VPhysicsCollisionEnableOriginal;
static VPhysicsCollisionRecheckType VPhysicsCollisionRecheckOriginal;
static VPhysicsNewPairInitType VPhysicsNewPairInitOriginal;
static VPhysicsUpdateExactMindistEventsType VPhysicsUpdateExactMindistEventsOriginal;
static VPhysicsUpdateExactMindistDeferredType VPhysicsUpdateExactMindistDeferredOriginal;
static VPhysicsDeferredFlushType VPhysicsDeferredFlushOriginal;
static VPhysicsDoImpactType VPhysicsDoImpactOriginal;
static VPhysicsExactToHullType VPhysicsExactToHullOriginal;
static VPhysicsMindistEventType VPhysicsMindistEventOriginal;
static const char* s_VPhysicsStaticBVHProbeFlavor;
static bool s_VPhysicsStaticBVHProbePointerVector;
static bool s_VPhysicsStaticBVHProbeInstalled;
static bool s_VPhysicsCollisionFilterHookInstalled;
static uintptr_t s_VPhysicsCollisionFilterShouldCollideTarget;
static unsigned long long s_VPhysicsObjectPairUpdateCalls;
static unsigned long long s_VPhysicsStaticBVHHelperCalls;
static unsigned long long s_VPhysicsStaticBVHFilterCalls;
static unsigned long long s_VPhysicsStaticBVHRebuildCalls;
static unsigned long long s_VPhysicsNewPairInitCalls;
static unsigned long long s_VPhysicsUpdateExactMindistEventsCalls;
static unsigned long long s_VPhysicsUpdateExactMindistDeferredCalls;
static unsigned long long s_VPhysicsNegativeStackGuardCalls;
static unsigned long long s_VPhysicsGuardPageTraceFaults;
static unsigned long long s_VPhysicsDeferredFlushCalls;
static unsigned long long s_VPhysicsDoImpactCalls;
static unsigned long long s_VPhysicsExactToHullCalls;
static unsigned long long s_VPhysicsMindistEventCalls;
static DWORD s_VPhysicsStaticBVHActiveThreadId;
static __int64 s_VPhysicsStaticBVHActiveObject;
static unsigned long long s_VPhysicsStaticBVHActiveCall;
static bool s_VPhysicsStaticBVHActiveTarget;
static thread_local const char* s_VPhysicsActiveMovementPhase;
static std::set<unsigned long long> s_VPhysicsStaticBVHLoggedSelfOnlyKeys;
static std::set<unsigned long long> s_VPhysicsStaticBVHTrackedObjects;
static std::set<unsigned long long> s_VPhysicsStaticBVHTrackedHelperKeys;
static std::set<unsigned long long> s_VPhysicsStaticBVHTrackedPairKeys;
static std::set<unsigned long long> s_VPhysicsMovementLoggedKeys;
static std::set<unsigned long long> s_VPhysicsNewPairInitLoggedKeys;
static std::set<unsigned long long> s_VPhysicsUpdateExactMindistLoggedKeys;
static PVOID s_VPhysicsGuardPageTraceHandler;
static uintptr_t s_VPhysicsGuardPageTraceManager;
static uintptr_t s_VPhysicsGuardPageTraceTarget;
static uintptr_t s_VPhysicsGuardPageTracePage;
static size_t s_VPhysicsGuardPageTracePageSize;
static DWORD s_VPhysicsGuardPageTraceProtect;
static DWORD s_VPhysicsGuardPageTraceRearmThreadId;
static volatile LONG s_VPhysicsGuardPageTraceArmed;
static volatile LONG s_VPhysicsGuardPageTraceNeedsRearm;
static volatile LONG s_VPhysicsGuardPageTraceLogBudget;
static volatile LONG s_VPhysicsGuardPageTraceObserved;
static volatile LONG s_VPhysicsGuardPageTraceArmLogWritten;
static volatile LONG s_VPhysicsGuardPageTraceLastUnderflow;
static volatile LONG s_VPhysicsGuardPageTraceLastStackIndex;
static volatile LONG s_VPhysicsGuardPageTraceLastBadEmpty;
static uintptr_t s_VPhysicsMindistEnvironmentOffset = 0x3E;
static uintptr_t s_VPhysicsMindistFlagsOffset = 0x4E;
static uintptr_t s_VPhysicsMindistObjectBaseOffset = 0x72;
static uintptr_t s_VPhysicsMindistObjectStride = 0x34;
static uintptr_t s_VPhysicsEnvironmentManagerOffset = 0x20;
static uintptr_t s_VPhysicsObjectCoreOffset = 0x1B0;
static uintptr_t s_VPhysicsManagerStackBaseOffset = 0x140080;
static uintptr_t s_VPhysicsManagerStackIndexOffset = 0x140100;
static uintptr_t s_VPhysicsManagerCriticalSectionOffset = 0x140108;
static bool s_VPhysicsManagerCriticalSectionIsPointer = true;
static unsigned int s_VPhysicsExactMindistDeferBit = 8;

struct VPhysicsStaticBVHTreeState {
	uintptr_t tree;
	uintptr_t nodes;
	uintptr_t objects;
	uintptr_t bounds;
	unsigned int valid;
	unsigned int nodeCount;
	unsigned int objectCount;
	float minX;
	float minY;
	float minZ;
	float maxX;
	float maxY;
	float maxZ;
	bool envReadable;
	bool treeHeaderReadable;
	bool rootReadable;
	bool countsReadable;
};

struct VPhysicsTfoTlsScratchSnapshot {
	uintptr_t tlsArray;
	uintptr_t tlsBase;
	uintptr_t slotAddress;
	uintptr_t scratch;
	uintptr_t rawBlock;
	uintptr_t freeList;
	uintptr_t begin;
	uintptr_t end;
	unsigned int tlsIndex;
	unsigned int refCount;
	unsigned int capacity;
	bool tlsIndexReadable;
	bool tlsArrayReadable;
	bool tlsBaseReadable;
	bool slotReadable;
	bool scratchReadable;
};

static unsigned long long VPhysicsSelfOnlyLogKey(
	__int64 object,
	unsigned int moveFlags,
	unsigned int surfaceFlags,
	unsigned int syncStamp,
	unsigned int active,
	int staticCandidateCount,
	int pairCount,
	int startBefore,
	int startAfter)
{
	unsigned long long key = static_cast<unsigned long long>(object);
	key ^= (static_cast<unsigned long long>(moveFlags) << 17) | (static_cast<unsigned long long>(moveFlags) >> 15);
	key ^= (static_cast<unsigned long long>(surfaceFlags) << 33) | (static_cast<unsigned long long>(surfaceFlags) >> 7);
	key ^= (static_cast<unsigned long long>(syncStamp) << 21) | (static_cast<unsigned long long>(syncStamp) >> 11);
	key ^= static_cast<unsigned long long>(active & 0xff) << 40;
	key ^= static_cast<unsigned long long>(staticCandidateCount & 0xff) << 8;
	key ^= static_cast<unsigned long long>(pairCount & 0xff) << 48;
	key ^= static_cast<unsigned long long>(startBefore & 0xff) << 56;
	key ^= static_cast<unsigned long long>(startAfter & 0xff);
	return key;
}

static unsigned long long VPhysicsTrackedStateLogKey(
	__int64 object,
	unsigned int moveFlags,
	unsigned int surfaceFlags,
	unsigned int syncStamp,
	unsigned int active,
	int pairCountA,
	int pairCountB)
{
	unsigned long long key = static_cast<unsigned long long>(object);
	key ^= (static_cast<unsigned long long>(moveFlags) << 19) | (static_cast<unsigned long long>(moveFlags) >> 13);
	key ^= (static_cast<unsigned long long>(surfaceFlags) << 37) | (static_cast<unsigned long long>(surfaceFlags) >> 5);
	key ^= (static_cast<unsigned long long>(syncStamp) << 23) | (static_cast<unsigned long long>(syncStamp) >> 9);
	key ^= static_cast<unsigned long long>(active & 0xff) << 44;
	key ^= static_cast<unsigned long long>(pairCountA & 0xffff) << 48;
	key ^= static_cast<unsigned long long>(pairCountB & 0xffff);
	return key;
}

static bool VPhysicsTrackSurfaceMissObject(__int64 object)
{
	if (!object)
		return false;

	return s_VPhysicsStaticBVHTrackedObjects.insert(static_cast<unsigned long long>(object)).second;
}

static bool VPhysicsIsTrackedSurfaceMissObject(__int64 object)
{
	if (!object)
		return false;

	return s_VPhysicsStaticBVHTrackedObjects.find(static_cast<unsigned long long>(object)) != s_VPhysicsStaticBVHTrackedObjects.end();
}

static bool VPhysicsReadPointer(uintptr_t address, uintptr_t* value)
{
	if (value)
		*value = 0;
	if (!value || !IsReadableRange(reinterpret_cast<void*>(address), sizeof(uintptr_t)))
		return false;

	__try {
		*value = *reinterpret_cast<uintptr_t*>(address);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		*value = 0;
		return false;
	}
}

static bool VPhysicsReadByte(uintptr_t address, unsigned int* value)
{
	if (value)
		*value = 0;
	if (!value || !IsReadableRange(reinterpret_cast<void*>(address), sizeof(unsigned char)))
		return false;

	__try {
		*value = *reinterpret_cast<unsigned char*>(address);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		*value = 0;
		return false;
	}
}

static bool VPhysicsReadDword(uintptr_t address, unsigned int* value)
{
	if (value)
		*value = 0;
	if (!value || !IsReadableRange(reinterpret_cast<void*>(address), sizeof(unsigned int)))
		return false;

	__try {
		*value = *reinterpret_cast<unsigned int*>(address);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		*value = 0;
		return false;
	}
}

static bool VPhysicsWriteDword(uintptr_t address, unsigned int value)
{
	if (!IsReadableRange(reinterpret_cast<void*>(address), sizeof(unsigned int)))
		return false;

	__try {
		*reinterpret_cast<unsigned int*>(address) = value;
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

static bool VPhysicsReadFloat(uintptr_t address, float* value)
{
	if (value)
		*value = 0.0f;
	if (!value || !IsReadableRange(reinterpret_cast<void*>(address), sizeof(float)))
		return false;

	__try {
		*value = *reinterpret_cast<float*>(address);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		*value = 0.0f;
		return false;
	}
}

static bool VPhysicsReadDouble(uintptr_t address, double* value)
{
	if (value)
		*value = 0.0;
	if (!value || !IsReadableRange(reinterpret_cast<void*>(address), sizeof(double)))
		return false;

	__try {
		*value = *reinterpret_cast<double*>(address);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		*value = 0.0;
		return false;
	}
}

static VPhysicsStaticBVHTreeState VPhysicsReadStaticBVHTreeState(__int64 rangeData)
{
	VPhysicsStaticBVHTreeState state = {};
	const uintptr_t environment = static_cast<uintptr_t>(rangeData);
	if (!environment)
		return state;

	state.envReadable = IsReadableRange(reinterpret_cast<void*>(environment + 0x118), sizeof(uintptr_t) + sizeof(unsigned char));
	if (!state.envReadable)
		return state;

	VPhysicsReadPointer(environment + 0x118, &state.tree);
	VPhysicsReadByte(environment + 0x120, &state.valid);
	if (!state.tree)
		return state;

	state.treeHeaderReadable = IsReadableRange(reinterpret_cast<void*>(state.tree), 0x38);
	if (!state.treeHeaderReadable)
		return state;

	VPhysicsReadPointer(state.tree + 0x00, &state.nodes);
	VPhysicsReadPointer(state.tree + 0x08, &state.objects);
	VPhysicsReadPointer(state.tree + 0x10, &state.bounds);
	state.countsReadable =
		VPhysicsReadDword(state.tree + 0x30, &state.nodeCount)
		&& VPhysicsReadDword(state.tree + 0x34, &state.objectCount);
	state.rootReadable = state.nodes && IsReadableRange(reinterpret_cast<void*>(state.nodes), 0x20)
		&& VPhysicsReadFloat(state.nodes + 0x00, &state.minX)
		&& VPhysicsReadFloat(state.nodes + 0x04, &state.minY)
		&& VPhysicsReadFloat(state.nodes + 0x08, &state.minZ)
		&& VPhysicsReadFloat(state.nodes + 0x10, &state.maxX)
		&& VPhysicsReadFloat(state.nodes + 0x14, &state.maxY)
		&& VPhysicsReadFloat(state.nodes + 0x18, &state.maxZ);
	return state;
}

static VPhysicsTfoTlsScratchSnapshot VPhysicsReadTfoTlsScratchSnapshot()
{
	VPhysicsTfoTlsScratchSnapshot snapshot = {};
	if (!s_R1OVPhysicsBase)
		return snapshot;

	snapshot.tlsIndexReadable = VPhysicsReadDword(s_R1OVPhysicsBase + 0x17DF8C, &snapshot.tlsIndex);
	if (!snapshot.tlsIndexReadable)
		return snapshot;

	snapshot.tlsArray = static_cast<uintptr_t>(__readgsqword(0x58));
	snapshot.tlsArrayReadable = snapshot.tlsArray
		&& IsReadableRange(reinterpret_cast<void*>(snapshot.tlsArray + static_cast<uintptr_t>(snapshot.tlsIndex) * sizeof(uintptr_t)), sizeof(uintptr_t));
	if (!snapshot.tlsArrayReadable)
		return snapshot;

	VPhysicsReadPointer(
		snapshot.tlsArray + static_cast<uintptr_t>(snapshot.tlsIndex) * sizeof(uintptr_t),
		&snapshot.tlsBase);
	snapshot.tlsBaseReadable = snapshot.tlsBase && IsReadableRange(reinterpret_cast<void*>(snapshot.tlsBase + 8), sizeof(uintptr_t));
	if (!snapshot.tlsBaseReadable)
		return snapshot;

	snapshot.slotAddress = snapshot.tlsBase + 8;
	snapshot.slotReadable = VPhysicsReadPointer(snapshot.slotAddress, &snapshot.scratch);
	if (!snapshot.slotReadable || !snapshot.scratch)
		return snapshot;

	snapshot.scratchReadable = IsReadableRange(reinterpret_cast<void*>(snapshot.scratch), 0x28);
	if (!snapshot.scratchReadable)
		return snapshot;

	VPhysicsReadPointer(snapshot.scratch + 0x00, &snapshot.rawBlock);
	VPhysicsReadPointer(snapshot.scratch + 0x08, &snapshot.freeList);
	VPhysicsReadPointer(snapshot.scratch + 0x10, &snapshot.begin);
	VPhysicsReadPointer(snapshot.scratch + 0x18, &snapshot.end);
	VPhysicsReadDword(snapshot.scratch + 0x20, &snapshot.refCount);
	VPhysicsReadDword(snapshot.scratch + 0x24, &snapshot.capacity);
	return snapshot;
}

static float VPhysicsAxisDistanceSq(float value, float minValue, float maxValue)
{
	float delta = 0.0f;
	if (value < minValue)
		delta = minValue - value;
	else if (value > maxValue)
		delta = value - maxValue;

	return delta * delta;
}

static float VPhysicsRootDistanceSq(const VPhysicsStaticBVHTreeState& state, float x, float y, float z)
{
	if (!state.rootReadable)
		return 0.0f;

	return VPhysicsAxisDistanceSq(x, state.minX, state.maxX)
		+ VPhysicsAxisDistanceSq(y, state.minY, state.maxY)
		+ VPhysicsAxisDistanceSq(z, state.minZ, state.maxZ);
}

static std::mutex s_VPhysicsDeferredProbeLogMutex;
static std::vector<std::string> s_VPhysicsDeferredProbeLogs;
static unsigned int s_VPhysicsDeferredProbeDroppedLogs;
static bool s_VPhysicsDeferredProbeLogsFlushed;

static bool AreVPhysicsProbePrintsEnabled()
{
	return false;
}

static bool VPhysicsShouldDeferProbeLog(const char* text)
{
	if (!IsVPhysicsDeferredProbeLogEnabled() || !text)
		return false;

	if (strncmp(text, "VPHYSICS:", 9) != 0)
		return false;

	return strstr(text, "probe hook") == nullptr
		&& strstr(text, "probe installed") == nullptr
		&& strstr(text, "requested but no known") == nullptr;
}

static void VPhysicsFlushDeferredProbeLogs(const char* reason)
{
	if (!AreVPhysicsProbePrintsEnabled()) {
		std::lock_guard<std::mutex> lock(s_VPhysicsDeferredProbeLogMutex);
		s_VPhysicsDeferredProbeLogsFlushed = true;
		s_VPhysicsDeferredProbeLogs.clear();
		s_VPhysicsDeferredProbeDroppedLogs = 0;
		return;
	}

	std::vector<std::string> logs;
	unsigned int dropped = 0;
	{
		std::lock_guard<std::mutex> lock(s_VPhysicsDeferredProbeLogMutex);
		if (s_VPhysicsDeferredProbeLogsFlushed)
			return;

		s_VPhysicsDeferredProbeLogsFlushed = true;
		logs.swap(s_VPhysicsDeferredProbeLogs);
		dropped = s_VPhysicsDeferredProbeDroppedLogs;
	}

	char header[256];
	_snprintf_s(
		header,
		sizeof(header),
		_TRUNCATE,
		"VPHYSICS: deferredLogFlush reason=%s lines=%llu dropped=%u\n",
		reason ? reason : "<unknown>",
		static_cast<unsigned long long>(logs.size()),
		dropped);
	OutputDebugStringA(header);
	Warning("%s", header);

	for (const std::string& line : logs) {
		OutputDebugStringA(line.c_str());
		Warning("%s", line.c_str());
	}
}

static void VPhysicsStaticBVHProbeLog(const char* text)
{
	if (!AreVPhysicsProbePrintsEnabled())
		return;

	if (!text || !*text)
		return;

	if (VPhysicsShouldDeferProbeLog(text)) {
		std::lock_guard<std::mutex> lock(s_VPhysicsDeferredProbeLogMutex);
		if (!s_VPhysicsDeferredProbeLogsFlushed && s_VPhysicsDeferredProbeLogs.size() < 12000) {
			s_VPhysicsDeferredProbeLogs.emplace_back(text);
		} else {
			++s_VPhysicsDeferredProbeDroppedLogs;
		}
		return;
	}

	OutputDebugStringA(text);
	Warning("%s", text);
}

static int VPhysicsPointerVectorCount(__int64 vector)
{
	if (!vector)
		return -1;

	uintptr_t begin = 0;
	uintptr_t end = 0;
	if (!VPhysicsReadPointer(static_cast<uintptr_t>(vector), &begin)
		|| !VPhysicsReadPointer(static_cast<uintptr_t>(vector) + sizeof(uintptr_t), &end))
		return -1;

	if (!begin && !end)
		return 0;
	if (!begin || end < begin || ((end - begin) % sizeof(uintptr_t)) != 0)
		return -1;

	const uintptr_t count = (end - begin) / sizeof(uintptr_t);
	return count <= 0x7fffffff ? static_cast<int>(count) : -1;
}

static bool VPhysicsPointerVectorElement(__int64 vector, int index, uintptr_t* value)
{
	if (value)
		*value = 0;
	if (!value || index < 0 || !vector)
		return false;

	uintptr_t begin = 0;
	uintptr_t end = 0;
	if (!VPhysicsReadPointer(static_cast<uintptr_t>(vector), &begin)
		|| !VPhysicsReadPointer(static_cast<uintptr_t>(vector) + sizeof(uintptr_t), &end))
		return false;

	if (!begin || end < begin || ((end - begin) % sizeof(uintptr_t)) != 0)
		return false;

	const uintptr_t count = (end - begin) / sizeof(uintptr_t);
	if (static_cast<uintptr_t>(index) >= count)
		return false;

	const uintptr_t slot = begin + (static_cast<uintptr_t>(index) * sizeof(uintptr_t));
	return VPhysicsReadPointer(slot, value);
}

static int VPhysicsStaticBVHProbeVectorCount(__int64 ledges)
{
	if (!ledges)
		return -1;

	if (s_VPhysicsStaticBVHProbePointerVector)
		return VPhysicsPointerVectorCount(ledges);

	if (!IsReadableRange(reinterpret_cast<void*>(ledges + 2), sizeof(unsigned short)))
		return -1;

	__try {
		return static_cast<int>(*reinterpret_cast<unsigned short*>(ledges + 2));
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return -1;
	}
}

static int VPhysicsObjectPairCount(__int64 object)
{
	return object ? VPhysicsPointerVectorCount(object + 0x108) : -1;
}

static unsigned int VPhysicsObjectDword(__int64 object, uintptr_t offset)
{
	unsigned int value = 0;
	return object && VPhysicsReadDword(static_cast<uintptr_t>(object) + offset, &value) ? value : 0;
}

static unsigned int VPhysicsObjectByte(__int64 object, uintptr_t offset)
{
	unsigned int value = 0;
	return object && VPhysicsReadByte(static_cast<uintptr_t>(object) + offset, &value) ? value : 0;
}

static uintptr_t VPhysicsObjectQword(__int64 object, uintptr_t offset)
{
	uintptr_t value = 0;
	return object && VPhysicsReadPointer(static_cast<uintptr_t>(object) + offset, &value) ? value : 0;
}

static uintptr_t VPhysicsReadQwordOrDefault(uintptr_t address, uintptr_t fallback)
{
	uintptr_t value = 0;
	return VPhysicsReadPointer(address, &value) ? value : fallback;
}

static int VPhysicsReadIntOrDefault(uintptr_t address, int fallback)
{
	unsigned int value = 0;
	return VPhysicsReadDword(address, &value) ? static_cast<int>(value) : fallback;
}

static unsigned int VPhysicsReadDwordOrDefault(uintptr_t address, unsigned int fallback)
{
	unsigned int value = 0;
	return VPhysicsReadDword(address, &value) ? value : fallback;
}

static uintptr_t VPhysicsManagerStackValueAddress(uintptr_t manager, int stackIndex)
{
	if (!manager)
		return 0;

	const uintptr_t stackBase = manager + s_VPhysicsManagerStackBaseOffset;
	const intptr_t signedAddress =
		static_cast<intptr_t>(stackBase)
		+ static_cast<intptr_t>(stackIndex) * static_cast<intptr_t>(sizeof(unsigned int));
	return static_cast<uintptr_t>(signedAddress);
}

static unsigned int VPhysicsReadManagerStackValueRaw(uintptr_t manager, int stackIndex)
{
	return VPhysicsReadDwordOrDefault(VPhysicsManagerStackValueAddress(manager, stackIndex), 0);
}

static uintptr_t VPhysicsManagerStackIndexAddress(uintptr_t manager)
{
	return manager ? manager + s_VPhysicsManagerStackIndexOffset : 0;
}

static int VPhysicsReadManagerStackIndex(uintptr_t manager)
{
	return VPhysicsReadIntOrDefault(VPhysicsManagerStackIndexAddress(manager), -1);
}

static LPCRITICAL_SECTION VPhysicsManagerCriticalSection(uintptr_t manager)
{
	if (!manager)
		return nullptr;

	if (s_VPhysicsManagerCriticalSectionIsPointer) {
		return reinterpret_cast<LPCRITICAL_SECTION>(
			VPhysicsReadQwordOrDefault(manager + s_VPhysicsManagerCriticalSectionOffset, 0));
	}

	return reinterpret_cast<LPCRITICAL_SECTION>(manager + s_VPhysicsManagerCriticalSectionOffset);
}

static unsigned int VPhysicsGuardPageTraceReadDword(uintptr_t address, unsigned int fallback)
{
	if (!address)
		return fallback;

	__try {
		return *reinterpret_cast<volatile unsigned int*>(address);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return fallback;
	}
}

static int VPhysicsGuardPageTraceReadInt(uintptr_t address, int fallback)
{
	if (!address)
		return fallback;

	__try {
		return *reinterpret_cast<volatile int*>(address);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return fallback;
	}
}

static uintptr_t VPhysicsGuardPageTraceReadQword(uintptr_t address, uintptr_t fallback)
{
	if (!address)
		return fallback;

	__try {
		return *reinterpret_cast<volatile uintptr_t*>(address);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return fallback;
	}
}

static bool VPhysicsGuardPageTraceAddressInPage(uintptr_t address)
{
	return s_VPhysicsGuardPageTracePage
		&& s_VPhysicsGuardPageTracePageSize
		&& address >= s_VPhysicsGuardPageTracePage
		&& address < s_VPhysicsGuardPageTracePage + s_VPhysicsGuardPageTracePageSize;
}

static bool VPhysicsArmGuardPageTracePage()
{
	if (!s_VPhysicsGuardPageTracePage || !s_VPhysicsGuardPageTracePageSize || !s_VPhysicsGuardPageTraceProtect)
		return false;

	DWORD oldProtect = 0;
	if (VirtualProtect(
			reinterpret_cast<void*>(s_VPhysicsGuardPageTracePage),
			s_VPhysicsGuardPageTracePageSize,
			s_VPhysicsGuardPageTraceProtect | PAGE_GUARD,
			&oldProtect)) {
		InterlockedExchange(&s_VPhysicsGuardPageTraceArmed, 1);
		InterlockedExchange(&s_VPhysicsGuardPageTraceNeedsRearm, 0);
		return true;
	}

	InterlockedExchange(&s_VPhysicsGuardPageTraceArmed, 0);
	return false;
}

static LONG CALLBACK VPhysicsGuardPageTraceVectoredHandler(EXCEPTION_POINTERS* exceptionInfo)
{
	if (!exceptionInfo || !exceptionInfo->ExceptionRecord || !exceptionInfo->ContextRecord)
		return EXCEPTION_CONTINUE_SEARCH;

	const DWORD code = exceptionInfo->ExceptionRecord->ExceptionCode;
	if (code == STATUS_GUARD_PAGE_VIOLATION) {
		const ULONG_PTR* info = exceptionInfo->ExceptionRecord->ExceptionInformation;
		const DWORD count = exceptionInfo->ExceptionRecord->NumberParameters;
		const uintptr_t accessAddress = count > 1 ? static_cast<uintptr_t>(info[1]) : 0;
		if (accessAddress && !VPhysicsGuardPageTraceAddressInPage(accessAddress))
			return EXCEPTION_CONTINUE_SEARCH;
		if (!accessAddress && !s_VPhysicsGuardPageTracePage)
			return EXCEPTION_CONTINUE_SEARCH;

		DWORD oldProtect = 0;
		VirtualProtect(
			reinterpret_cast<void*>(s_VPhysicsGuardPageTracePage),
			s_VPhysicsGuardPageTracePageSize,
			s_VPhysicsGuardPageTraceProtect,
			&oldProtect);
		InterlockedExchange(&s_VPhysicsGuardPageTraceArmed, 0);
		InterlockedExchange(&s_VPhysicsGuardPageTraceNeedsRearm, 1);
		s_VPhysicsGuardPageTraceRearmThreadId = GetCurrentThreadId();

		const uintptr_t manager = s_VPhysicsGuardPageTraceManager;
		const int stackIndex = VPhysicsGuardPageTraceReadInt(manager + 0x140100, -9999);
		const unsigned int underflow = VPhysicsGuardPageTraceReadDword(manager + 0x14007C, 0xffffffffu);
		const unsigned int stack0 = VPhysicsGuardPageTraceReadDword(manager + 0x140080, 0xffffffffu);
		const unsigned int stack1 = VPhysicsGuardPageTraceReadDword(manager + 0x140084, 0xffffffffu);
		const uintptr_t criticalSection = VPhysicsGuardPageTraceReadQword(manager + 0x140108, 0);

#ifdef _M_X64
		const uintptr_t ip = static_cast<uintptr_t>(exceptionInfo->ContextRecord->Rip);
#else
		const uintptr_t ip = 0;
#endif
		const uintptr_t rva = s_R1OVPhysicsBase && ip >= s_R1OVPhysicsBase
			? ip - s_R1OVPhysicsBase
			: 0;
		const unsigned long long fault = ++s_VPhysicsGuardPageTraceFaults;
		const int previousStackIndex = InterlockedCompareExchange(&s_VPhysicsGuardPageTraceLastStackIndex, 0, 0);
		const unsigned int previousUnderflow = static_cast<unsigned int>(
			InterlockedCompareExchange(&s_VPhysicsGuardPageTraceLastUnderflow, 0, 0));
		const int previousBadEmpty = InterlockedCompareExchange(&s_VPhysicsGuardPageTraceLastBadEmpty, 0, 0);
		const bool observed = InterlockedCompareExchange(&s_VPhysicsGuardPageTraceObserved, 1, 1) != 0;
		const bool badEmpty = stackIndex < 0 && ((underflow >> 3) & 1u) != 0;
		const bool underflowChanged = !observed || underflow != previousUnderflow;
		const bool badEmptyTransition = badEmpty && !previousBadEmpty;
		const bool stackChanged = stackIndex != previousStackIndex;
		if (underflowChanged || badEmptyTransition) {
			InterlockedExchange(&s_VPhysicsGuardPageTraceObserved, 1);
			InterlockedExchange(&s_VPhysicsGuardPageTraceLastUnderflow, static_cast<LONG>(underflow));
			InterlockedExchange(&s_VPhysicsGuardPageTraceLastStackIndex, stackIndex);
			InterlockedExchange(&s_VPhysicsGuardPageTraceLastBadEmpty, badEmpty ? 1 : 0);
		}

		if (AreVPhysicsProbePrintsEnabled() && (underflowChanged || badEmptyTransition)) {
			const long budgetAfter = InterlockedDecrement(&s_VPhysicsGuardPageTraceLogBudget);
			if (budgetAfter >= 0) {
			char buffer[1024];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"VPHYSICS: guardPage desync fault=%llu accessKind=%llu access=%p rel=0x%llx ip=%p rva=0x%llx tid=%lu manager=%p stack=%d->%d under=0x%08x->0x%08x stack0=0x%08x stack1=0x%08x badEmpty=%d->%d stackChanged=%d crit=%p page=%p protect=0x%lx budget=%ld\n",
				fault,
				count > 0 ? static_cast<unsigned long long>(info[0]) : 0ull,
				reinterpret_cast<void*>(accessAddress),
				accessAddress && manager ? static_cast<unsigned long long>(accessAddress - manager) : 0ull,
				reinterpret_cast<void*>(ip),
				static_cast<unsigned long long>(rva),
				static_cast<unsigned long>(s_VPhysicsGuardPageTraceRearmThreadId),
				reinterpret_cast<void*>(manager),
				previousStackIndex,
				stackIndex,
				previousUnderflow,
				underflow,
				stack0,
				stack1,
				previousBadEmpty,
				badEmpty ? 1 : 0,
				stackChanged ? 1 : 0,
				reinterpret_cast<void*>(criticalSection),
				reinterpret_cast<void*>(s_VPhysicsGuardPageTracePage),
				static_cast<unsigned long>(s_VPhysicsGuardPageTraceProtect),
				budgetAfter);
			OutputDebugStringA(buffer);
			}
		}
		return EXCEPTION_CONTINUE_EXECUTION;
	}

	if (code == EXCEPTION_SINGLE_STEP
		&& InterlockedCompareExchange(&s_VPhysicsGuardPageTraceNeedsRearm, 0, 0) != 0
		&& GetCurrentThreadId() == s_VPhysicsGuardPageTraceRearmThreadId) {
		VPhysicsArmGuardPageTracePage();
		return EXCEPTION_CONTINUE_EXECUTION;
	}

	return EXCEPTION_CONTINUE_SEARCH;
}

static void VPhysicsMaybeArmGuardPageTrace(uintptr_t manager, const char* reason)
{
	if (!IsVPhysicsGuardPageTraceEnabled() || !manager)
		return;

	SYSTEM_INFO systemInfo{};
	GetSystemInfo(&systemInfo);
	const size_t pageSize = systemInfo.dwPageSize ? systemInfo.dwPageSize : 0x1000;
	const uintptr_t target = manager + 0x14007C;
	const uintptr_t page = target & ~(static_cast<uintptr_t>(pageSize) - 1ull);
	if (!page || !VPhysicsGuardPageTraceAddressInPage(target) && s_VPhysicsGuardPageTracePage)
	{
		if (s_VPhysicsGuardPageTracePage && InterlockedCompareExchange(&s_VPhysicsGuardPageTraceArmed, 0, 0) != 0) {
			DWORD ignoredProtect = 0;
			VirtualProtect(
				reinterpret_cast<void*>(s_VPhysicsGuardPageTracePage),
				s_VPhysicsGuardPageTracePageSize,
				s_VPhysicsGuardPageTraceProtect,
				&ignoredProtect);
			InterlockedExchange(&s_VPhysicsGuardPageTraceArmed, 0);
			InterlockedExchange(&s_VPhysicsGuardPageTraceNeedsRearm, 0);
		}
	}
	if (InterlockedCompareExchange(&s_VPhysicsGuardPageTraceArmed, 0, 0) != 0
		&& s_VPhysicsGuardPageTraceManager == manager
		&& s_VPhysicsGuardPageTraceTarget == target)
		return;
	if (s_VPhysicsGuardPageTraceManager == manager
		&& s_VPhysicsGuardPageTraceTarget == target
		&& s_VPhysicsGuardPageTracePage
		&& InterlockedCompareExchange(&s_VPhysicsGuardPageTraceArmed, 0, 0) == 0) {
		VPhysicsArmGuardPageTracePage();
		return;
	}

	MEMORY_BASIC_INFORMATION mbi{};
	if (!VirtualQuery(reinterpret_cast<void*>(page), &mbi, sizeof(mbi)))
		return;
	if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_NOACCESS))
		return;

	const DWORD baseProtect = mbi.Protect & 0xff;
	if (!IsReadableProtect(baseProtect))
		return;

	if (!s_VPhysicsGuardPageTraceHandler) {
		s_VPhysicsGuardPageTraceHandler = AddVectoredExceptionHandler(1, VPhysicsGuardPageTraceVectoredHandler);
		if (!s_VPhysicsGuardPageTraceHandler)
			return;
	}

	s_VPhysicsGuardPageTraceManager = manager;
	s_VPhysicsGuardPageTraceTarget = target;
	s_VPhysicsGuardPageTracePage = page;
	s_VPhysicsGuardPageTracePageSize = pageSize;
	s_VPhysicsGuardPageTraceProtect = baseProtect;
	if (InterlockedCompareExchange(&s_VPhysicsGuardPageTraceLogBudget, 0, 0) == 0)
		InterlockedExchange(&s_VPhysicsGuardPageTraceLogBudget, VPhysicsGuardPageTraceLogBudget());

	const int initialStackIndex = VPhysicsGuardPageTraceReadInt(manager + 0x140100, -9999);
	const unsigned int initialUnderflow = VPhysicsGuardPageTraceReadDword(manager + 0x14007C, 0xffffffffu);
	const unsigned int initialStack0 = VPhysicsGuardPageTraceReadDword(manager + 0x140080, 0xffffffffu);
	const bool initialBadEmpty = initialStackIndex < 0 && ((initialUnderflow >> 3) & 1u) != 0;
	InterlockedExchange(&s_VPhysicsGuardPageTraceObserved, 1);
	InterlockedExchange(&s_VPhysicsGuardPageTraceLastUnderflow, static_cast<LONG>(initialUnderflow));
	InterlockedExchange(&s_VPhysicsGuardPageTraceLastStackIndex, initialStackIndex);
	InterlockedExchange(&s_VPhysicsGuardPageTraceLastBadEmpty, initialBadEmpty ? 1 : 0);
	if (InterlockedCompareExchange(&s_VPhysicsGuardPageTraceArmLogWritten, 1, 0) == 0) {
		char buffer[768];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"VPHYSICS: guardPage armed reason=%s manager=%p target=%p page=%p pageSize=0x%Ix protect=0x%lx stack=%d under=0x%08x stack0=0x%08x badEmpty=%d budget=%ld\n",
			reason ? reason : "<unknown>",
			reinterpret_cast<void*>(manager),
			reinterpret_cast<void*>(target),
			reinterpret_cast<void*>(page),
			pageSize,
			static_cast<unsigned long>(baseProtect),
			initialStackIndex,
			initialUnderflow,
			initialStack0,
			initialBadEmpty ? 1 : 0,
			InterlockedCompareExchange(&s_VPhysicsGuardPageTraceLogBudget, 0, 0));
		VPhysicsStaticBVHProbeLog(buffer);
	}

	if (!VPhysicsArmGuardPageTracePage())
		return;
}

static unsigned int VPhysicsReadWordOrDefault(uintptr_t address, unsigned int fallback)
{
	if (!IsReadableRange(reinterpret_cast<void*>(address), sizeof(unsigned short)))
		return fallback;

	__try {
		return *reinterpret_cast<unsigned short*>(address);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return fallback;
	}
}

static float VPhysicsReadFloatOrDefault(uintptr_t address, float fallback)
{
	float value = 0.0f;
	return VPhysicsReadFloat(address, &value) ? value : fallback;
}

static void VPhysicsReadVector3(uintptr_t address, float out[3])
{
	if (!out)
		return;

	out[0] = 0.0f;
	out[1] = 0.0f;
	out[2] = 0.0f;
	VPhysicsReadFloat(address + 0, &out[0]);
	VPhysicsReadFloat(address + 4, &out[1]);
	VPhysicsReadFloat(address + 8, &out[2]);
}

static unsigned int VPhysicsObjectSurfaceFlags(__int64 object)
{
	uintptr_t surface = 0;
	if (!object || !VPhysicsReadPointer(static_cast<uintptr_t>(object) + 0x1B0, &surface))
		return 0;

	unsigned int flags = 0;
	return VPhysicsReadDword(surface, &flags) ? flags : 0;
}

static bool VPhysicsShouldLogSurfaceMissObject(unsigned int moveFlags, unsigned int surfaceFlags, int pairCount)
{
	if (moveFlags == 0x108 && surfaceFlags == 0x100c)
		return true;

	return moveFlags == 0x101 && surfaceFlags == 0x208 && pairCount <= 0;
}

static bool VPhysicsLooksLikeSurfaceMissObject(__int64 object)
{
	if (!object)
		return false;

	if (VPhysicsIsTrackedSurfaceMissObject(object))
		return true;

	const unsigned int moveFlags = VPhysicsObjectDword(object, 0xE0);
	const unsigned int surfaceFlags = VPhysicsObjectSurfaceFlags(object);
	const int pairCount = VPhysicsObjectPairCount(object);
	if (VPhysicsShouldLogSurfaceMissObject(moveFlags, surfaceFlags, pairCount))
		return true;

	const unsigned int movementState = moveFlags & 0xff;
	return (surfaceFlags == 0x100c && (movementState == 1 || movementState == 8 || movementState == 0x21))
		|| (surfaceFlags == 0x208 && (movementState == 1 || movementState == 8 || movementState == 0x21));
}

static unsigned long long VPhysicsMovementLogKey(
	const char* phase,
	__int64 owner,
	__int64 object,
	unsigned int moveFlags,
	unsigned int surfaceFlags,
	unsigned int syncStamp,
	unsigned int active,
	int pairCount)
{
	unsigned long long phaseHash = 1469598103934665603ull;
	if (phase) {
		for (const unsigned char* cursor = reinterpret_cast<const unsigned char*>(phase); *cursor; ++cursor) {
			phaseHash ^= *cursor;
			phaseHash *= 1099511628211ull;
		}
	}

	unsigned long long key = phaseHash;
	key ^= static_cast<unsigned long long>(owner) + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2);
	key ^= static_cast<unsigned long long>(object) + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2);
	key ^= (static_cast<unsigned long long>(moveFlags) << 17) | (static_cast<unsigned long long>(moveFlags) >> 15);
	key ^= (static_cast<unsigned long long>(surfaceFlags) << 31) | (static_cast<unsigned long long>(surfaceFlags) >> 9);
	key ^= (static_cast<unsigned long long>(syncStamp) << 21) | (static_cast<unsigned long long>(syncStamp) >> 11);
	key ^= static_cast<unsigned long long>(active & 0xff) << 48;
	key ^= static_cast<unsigned long long>(pairCount & 0xffff);
	return key;
}

static void VPhysicsLogMovementObject(const char* phase, __int64 owner, __int64 object)
{
	if (!object || !VPhysicsLooksLikeSurfaceMissObject(object))
		return;

	const unsigned int moveFlags = VPhysicsObjectDword(object, 0xE0);
	const unsigned int surfaceFlags = VPhysicsObjectSurfaceFlags(object);
	const unsigned int syncStamp = VPhysicsObjectDword(object, 0x100);
	const unsigned int active = VPhysicsObjectByte(object, 0x104);
	const int pairCount = VPhysicsObjectPairCount(object);
	const uintptr_t core = VPhysicsObjectQword(object, 0x1B0);
	const uintptr_t group = VPhysicsObjectQword(object, 0x1B8);
	const bool tracked = VPhysicsIsTrackedSurfaceMissObject(object);
	const unsigned long long key = VPhysicsMovementLogKey(phase, owner, object, moveFlags, surfaceFlags, syncStamp, active, pairCount);
	if (!s_VPhysicsMovementLoggedKeys.insert(key).second)
		return;
	char buffer[1408];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"VPHYSICS: movement phase=%s owner=%p object=%p tracked=%d active=%u moveFlags=0x%08x movementState=0x%02x surfaceFlags=0x%08x sync=%u pairs=%d core=%p group=%p key=0x%016llx\n",
		phase ? phase : "<unknown>",
		reinterpret_cast<void*>(owner),
		reinterpret_cast<void*>(object),
		tracked ? 1 : 0,
		active,
		moveFlags,
		moveFlags & 0xff,
		surfaceFlags,
		syncStamp,
		pairCount,
		reinterpret_cast<void*>(core),
		reinterpret_cast<void*>(group),
		key);
	VPhysicsStaticBVHProbeLog(buffer);
}

static int VPhysicsCoreObjectCount(__int64 core)
{
	return core && IsReadableRange(reinterpret_cast<void*>(core + 0x68), sizeof(uintptr_t) * 2)
		? VPhysicsPointerVectorCount(core + 0x68)
		: -1;
}

static bool VPhysicsCoreObjectElement(__int64 core, int index, uintptr_t* value)
{
	return core
		&& IsReadableRange(reinterpret_cast<void*>(core + 0x68), sizeof(uintptr_t) * 2)
		&& VPhysicsPointerVectorElement(core + 0x68, index, value);
}

static void VPhysicsLogCoreMovementObjects(const char* phase, __int64 core)
{
	const int objectCount = VPhysicsCoreObjectCount(core);
	if (objectCount <= 0 || objectCount > 64)
		return;

	for (int i = 0; i < objectCount; ++i) {
		uintptr_t object = 0;
		if (VPhysicsCoreObjectElement(core, i, &object))
			VPhysicsLogMovementObject(phase, core, static_cast<__int64>(object));
	}
}

struct VPhysicsObjectSnapshot {
	unsigned int moveFlags;
	unsigned int surfaceFlags;
	unsigned int syncStamp;
	unsigned int active;
	int pairCount;
	uintptr_t core;
	uintptr_t group;
};

struct VPhysicsPairSnapshot {
	uintptr_t objectA;
	uintptr_t objectB;
	uintptr_t pairContext;
	unsigned int indexA;
	unsigned int indexB;
	float distanceA;
	float distanceB;
	float positionA[3];
	float positionB[3];
	bool readable;
};

struct VPhysicsMindistSnapshot {
	uintptr_t objectA;
	uintptr_t objectB;
	uintptr_t environment;
	uintptr_t coreA;
	uintptr_t coreB;
	unsigned int environmentSelectorCount;
	unsigned int flags;
	unsigned int eventIndex;
	unsigned int collType;
	unsigned int synapseSort;
	unsigned int mindistFunction;
	unsigned int recalcResult;
	unsigned int mindistStatus;
	unsigned int collDistSelector;
	uintptr_t eventQueue;
	uintptr_t eventHeap;
	float environmentDeltaPsi;
	double eventQueueBaseTime;
	double environmentNow;
	double environmentNext;
	float collDist;
	float worstCaseSpeed;
	float hullCheckLen;
	float hullTimeFromLength;
	float eventDueRelative;
	double eventDueAbsolute;
	float length;
	float contactPlane[3];
	float coreASpeed[3];
	float coreBSpeed[3];
	float coreARotationAxis[3];
	float coreBRotationAxis[3];
	float coreACurrentSpeed;
	float coreBCurrentSpeed;
	float coreAMaxSurfaceRotSpeed;
	float coreBMaxSurfaceRotSpeed;
	bool readable;
	bool environmentSelectorCountReadable;
	bool collDistReadable;
	bool eventDueReadable;
};

struct VPhysicsCE300GateSnapshot {
	uintptr_t objectA;
	uintptr_t objectB;
	uintptr_t coreA;
	uintptr_t coreB;
	uintptr_t manager;
	unsigned int slotA;
	unsigned int slotB;
	unsigned int flags;
	int managerStackIndex;
	unsigned int managerStackValue;
	unsigned int failCode;
	float length;
	float collDist;
	float hullThreshold;
	float projectedSpeed;
	float eventWindow;
	float relativeNormalSpeed;
	float rotationContributionA;
	float rotationContributionB;
	float rotationDotA;
	float rotationDotB;
	float normalSpeedA;
	float normalSpeedB;
	float deltaTime;
	float envDeltaPsi;
	float worstCaseSpeed;
	float hullScale;
	float projectedGateA;
	float projectedGateB;
	unsigned int lengthBits;
	unsigned int collDistBits;
	unsigned int projectedSpeedBits;
	unsigned int eventWindowBits;
	bool readable;
	bool statusGate;
	bool managerGate;
	bool hullGate;
	bool projectedGate;
	bool windowGate;
	bool recalcGate;
	bool selectorGate;
	bool reachesCounter;
};

static VPhysicsObjectSnapshot VPhysicsReadObjectSnapshot(__int64 object)
{
	VPhysicsObjectSnapshot snapshot = {};
	snapshot.moveFlags = VPhysicsObjectDword(object, 0xE0);
	snapshot.surfaceFlags = VPhysicsObjectSurfaceFlags(object);
	snapshot.syncStamp = VPhysicsObjectDword(object, 0x100);
	snapshot.active = VPhysicsObjectByte(object, 0x104);
	snapshot.pairCount = VPhysicsObjectPairCount(object);
	snapshot.core = VPhysicsObjectQword(object, 0x1B0);
	snapshot.group = VPhysicsObjectQword(object, 0x1B8);
	return snapshot;
}

static VPhysicsMindistSnapshot VPhysicsReadMindistSnapshot(__int64 mindist)
{
	VPhysicsMindistSnapshot snapshot = {};
	if (!mindist || !IsReadableRange(reinterpret_cast<void*>(mindist), 0xDC))
		return snapshot;

	const uintptr_t mindistAddress = static_cast<uintptr_t>(mindist);
	snapshot.readable = true;
	snapshot.environment = VPhysicsReadQwordOrDefault(mindistAddress + s_VPhysicsMindistEnvironmentOffset, 0);
	snapshot.environmentSelectorCountReadable = VPhysicsReadDword(snapshot.environment + 0x114, &snapshot.environmentSelectorCount);
	VPhysicsReadDword(mindistAddress + s_VPhysicsMindistFlagsOffset, &snapshot.flags);
	VPhysicsReadDword(mindistAddress + 0x08, &snapshot.eventIndex);
	snapshot.collType = snapshot.flags & 0xffu;
	snapshot.synapseSort = (snapshot.flags >> 8) & 0x3u;
	snapshot.mindistFunction = (snapshot.flags >> 10) & 0x3u;
	snapshot.recalcResult = (snapshot.flags >> 12) & 0x3u;
	snapshot.mindistStatus = (snapshot.flags >> 18) & 0xfu;
	snapshot.collDistSelector = (snapshot.flags >> 22) & 0xffu;
	snapshot.objectA = VPhysicsReadQwordOrDefault(mindistAddress + s_VPhysicsMindistObjectBaseOffset, 0);
	snapshot.objectB = VPhysicsReadQwordOrDefault(mindistAddress + s_VPhysicsMindistObjectBaseOffset + s_VPhysicsMindistObjectStride, 0);
	snapshot.coreA = VPhysicsObjectQword(static_cast<__int64>(snapshot.objectA), s_VPhysicsObjectCoreOffset);
	snapshot.coreB = VPhysicsObjectQword(static_cast<__int64>(snapshot.objectB), s_VPhysicsObjectCoreOffset);
	VPhysicsReadFloat(snapshot.environment + 0xF4, &snapshot.environmentDeltaPsi);
	VPhysicsReadDouble(snapshot.environment + 0x1A8, &snapshot.environmentNow);
	VPhysicsReadDouble(snapshot.environment + 0x1B0, &snapshot.environmentNext);
	const uintptr_t timeManager = VPhysicsReadQwordOrDefault(snapshot.environment + 0x08, 0);
	snapshot.eventQueue = VPhysicsReadQwordOrDefault(timeManager + 0x10, 0);
	VPhysicsReadDouble(timeManager + 0x28, &snapshot.eventQueueBaseTime);
	snapshot.eventHeap = VPhysicsReadQwordOrDefault(snapshot.eventQueue + 0x08, 0);
	const uintptr_t eventItems = snapshot.eventHeap;
	const unsigned int eventIndex = snapshot.eventIndex & 0xffffu;
	if (eventIndex != 0xffffu && eventItems && eventIndex < 0x10000u) {
		snapshot.eventDueReadable = VPhysicsReadFloat(eventItems + 24ull * eventIndex + 0x08, &snapshot.eventDueRelative);
		if (snapshot.eventDueReadable)
			snapshot.eventDueAbsolute = snapshot.eventQueueBaseTime + snapshot.eventDueRelative;
	}
	VPhysicsReadFloat(mindistAddress + 0xC6, &snapshot.length);
	VPhysicsReadVector3(mindistAddress + 0xD0, snapshot.contactPlane);
	VPhysicsReadVector3(snapshot.coreA + 0x100, snapshot.coreASpeed);
	VPhysicsReadVector3(snapshot.coreB + 0x100, snapshot.coreBSpeed);
	VPhysicsReadVector3(snapshot.coreA + 0x150, snapshot.coreARotationAxis);
	VPhysicsReadVector3(snapshot.coreB + 0x150, snapshot.coreBRotationAxis);
	VPhysicsReadFloat(snapshot.coreA + 0x16C, &snapshot.coreACurrentSpeed);
	VPhysicsReadFloat(snapshot.coreB + 0x16C, &snapshot.coreBCurrentSpeed);
	VPhysicsReadFloat(snapshot.coreA + 0x1F4, &snapshot.coreAMaxSurfaceRotSpeed);
	VPhysicsReadFloat(snapshot.coreB + 0x1F4, &snapshot.coreBMaxSurfaceRotSpeed);
	snapshot.worstCaseSpeed =
		snapshot.coreACurrentSpeed
		+ snapshot.coreBCurrentSpeed
		+ snapshot.coreAMaxSurfaceRotSpeed
		+ snapshot.coreBMaxSurfaceRotSpeed;
	if (s_R1OVPhysicsBase && snapshot.collDistSelector < 256u) {
		snapshot.collDistReadable = VPhysicsReadFloat(
			s_R1OVPhysicsBase + 0x173C08 + static_cast<uintptr_t>(snapshot.collDistSelector) * sizeof(float),
			&snapshot.collDist);
		if (snapshot.collDistReadable) {
			snapshot.hullTimeFromLength = snapshot.length - snapshot.collDist;
			snapshot.hullCheckLen = snapshot.collDist + snapshot.environmentDeltaPsi * snapshot.worstCaseSpeed * 2.1f;
		}
	}
	return snapshot;
}

static VPhysicsPairSnapshot VPhysicsReadPairSnapshot(__int64 pair)
{
	VPhysicsPairSnapshot snapshot = {};
	if (!pair || !IsReadableRange(reinterpret_cast<void*>(pair), 0x80))
		return snapshot;

	const uintptr_t pairAddress = static_cast<uintptr_t>(pair);
	snapshot.readable = true;
	snapshot.indexA = VPhysicsReadWordOrDefault(pairAddress + 48, 0xffffu);
	snapshot.indexB = VPhysicsReadWordOrDefault(pairAddress + 50, 0xffffu);
	snapshot.pairContext = VPhysicsReadQwordOrDefault(pairAddress + 62, 0);
	snapshot.objectA = VPhysicsReadQwordOrDefault(pairAddress + 78, 0);
	snapshot.objectB = VPhysicsReadQwordOrDefault(pairAddress + 86, 0);
	VPhysicsReadFloat(pairAddress + 106, &snapshot.distanceA);
	VPhysicsReadFloat(pairAddress + 122, &snapshot.distanceB);
	VPhysicsReadVector3(pairAddress + 94, snapshot.positionA);
	VPhysicsReadVector3(pairAddress + 110, snapshot.positionB);
	return snapshot;
}

static unsigned int VPhysicsFloatBits(float value)
{
	unsigned int bits = 0;
	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static float VPhysicsDot3(const float* lhs, const float* rhs)
{
	return lhs[0] * rhs[0] + lhs[1] * rhs[1] + lhs[2] * rhs[2];
}

static float VPhysicsSqrtClamp(float value)
{
	return value > 0.0f ? sqrtf(value) : 0.0f;
}

static VPhysicsCE300GateSnapshot VPhysicsBuildCE300GateSnapshot(__int64 mindist, int allowHullConversion)
{
	VPhysicsCE300GateSnapshot gate = {};
	if (!mindist || !IsReadableRange(reinterpret_cast<void*>(mindist), 0xDC)) {
		gate.failCode = 1;
		return gate;
	}

	const uintptr_t mindistAddress = static_cast<uintptr_t>(mindist);
	gate.readable = true;
	VPhysicsReadDword(mindistAddress + s_VPhysicsMindistFlagsOffset, &gate.flags);
	const uintptr_t environment = VPhysicsReadQwordOrDefault(mindistAddress + s_VPhysicsMindistEnvironmentOffset, 0);
	gate.statusGate = (gate.flags & 0x3C0000u) == 0x0C0000u;
	if (!gate.statusGate) {
		gate.failCode = 8;
		return gate;
	}

	gate.manager = VPhysicsReadQwordOrDefault(environment + s_VPhysicsEnvironmentManagerOffset, 0);
	gate.managerStackIndex = VPhysicsReadManagerStackIndex(gate.manager);
	gate.managerStackValue = VPhysicsReadManagerStackValueRaw(gate.manager, gate.managerStackIndex);
	gate.managerGate = (gate.managerStackValue & s_VPhysicsExactMindistDeferBit) == 0;
	if (!gate.managerGate) {
		gate.failCode = 9;
		return gate;
	}

	gate.slotA = (gate.flags >> 8) & 3u;
	gate.slotB = ((gate.flags ^ 0x100u) >> 8) & 3u;
	gate.objectA = VPhysicsReadQwordOrDefault(
		mindistAddress + s_VPhysicsMindistObjectBaseOffset + static_cast<uintptr_t>(gate.slotA) * s_VPhysicsMindistObjectStride,
		0);
	gate.objectB = VPhysicsReadQwordOrDefault(
		mindistAddress + s_VPhysicsMindistObjectBaseOffset + static_cast<uintptr_t>(gate.slotB) * s_VPhysicsMindistObjectStride,
		0);
	gate.coreA = VPhysicsObjectQword(static_cast<__int64>(gate.objectA), s_VPhysicsObjectCoreOffset);
	gate.coreB = VPhysicsObjectQword(static_cast<__int64>(gate.objectB), s_VPhysicsObjectCoreOffset);
	if (!environment || !gate.objectA || !gate.objectB || !gate.coreA || !gate.coreB || !s_R1OVPhysicsBase) {
		gate.failCode = 1;
		return gate;
	}

	VPhysicsReadFloat(mindistAddress + 0xC6, &gate.length);
	VPhysicsReadFloat(environment + 0xF4, &gate.envDeltaPsi);
	double environmentNow = 0.0;
	double environmentNext = 0.0;
	VPhysicsReadDouble(environment + 0x1A8, &environmentNow);
	VPhysicsReadDouble(environment + 0x1B0, &environmentNext);
	gate.deltaTime = static_cast<float>(environmentNext - environmentNow);

	const unsigned int collDistSelector = (gate.flags >> 22) & 0xffu;
	VPhysicsReadFloat(s_R1OVPhysicsBase + 0x173C08 + static_cast<uintptr_t>(collDistSelector) * sizeof(float), &gate.collDist);
	VPhysicsReadFloat(s_R1OVPhysicsBase + 0x146E30, &gate.hullScale);
	VPhysicsReadFloat(s_R1OVPhysicsBase + 0x173D14, &gate.projectedGateA);
	VPhysicsReadFloat(s_R1OVPhysicsBase + 0x146B84, &gate.projectedGateB);
	if (gate.hullScale == 0.0f)
		gate.hullScale = 2.1f;

	float normal[3] = {};
	float speedA[3] = {};
	float speedB[3] = {};
	float rotationAxisA[3] = {};
	float rotationAxisB[3] = {};
	VPhysicsReadVector3(mindistAddress + 0xD0, normal);
	VPhysicsReadVector3(gate.coreA + 0x100, speedA);
	VPhysicsReadVector3(gate.coreB + 0x100, speedB);
	VPhysicsReadVector3(gate.coreA + 0x150, rotationAxisA);
	VPhysicsReadVector3(gate.coreB + 0x150, rotationAxisB);
	gate.normalSpeedA = VPhysicsDot3(normal, speedA);
	gate.normalSpeedB = VPhysicsDot3(normal, speedB);
	gate.relativeNormalSpeed = gate.normalSpeedB - gate.normalSpeedA;
	gate.rotationDotA = VPhysicsDot3(normal, rotationAxisA);
	gate.rotationDotB = VPhysicsDot3(normal, rotationAxisB);

	const float maxRotA = VPhysicsReadFloatOrDefault(gate.coreA + 0x1F4, 0.0f);
	const float maxRotB = VPhysicsReadFloatOrDefault(gate.coreB + 0x1F4, 0.0f);
	const float currentSpeedA = VPhysicsReadFloatOrDefault(gate.coreA + 0x16C, 0.0f);
	const float currentSpeedB = VPhysicsReadFloatOrDefault(gate.coreB + 0x16C, 0.0f);
	gate.rotationContributionA = VPhysicsSqrtClamp(1.0f - gate.rotationDotA * gate.rotationDotA) * maxRotA;
	gate.rotationContributionB = VPhysicsSqrtClamp(1.0f - gate.rotationDotB * gate.rotationDotB) * maxRotB;
	gate.projectedSpeed = gate.relativeNormalSpeed + gate.rotationContributionA + gate.rotationContributionB;
	gate.worstCaseSpeed = currentSpeedA + currentSpeedB + maxRotA + maxRotB;
	gate.hullThreshold = gate.collDist + gate.envDeltaPsi * gate.worstCaseSpeed * gate.hullScale;
	gate.eventWindow = gate.collDist + gate.deltaTime * gate.projectedSpeed;

	gate.lengthBits = VPhysicsFloatBits(gate.length);
	gate.collDistBits = VPhysicsFloatBits(gate.collDist);
	gate.projectedSpeedBits = VPhysicsFloatBits(gate.projectedSpeed);
	gate.eventWindowBits = VPhysicsFloatBits(gate.eventWindow);

	gate.hullGate = gate.length <= gate.hullThreshold;
	if (!gate.hullGate) {
		gate.failCode = allowHullConversion ? 2u : 3u;
		return gate;
	}

	gate.projectedGate = gate.projectedSpeed >= gate.projectedGateA || gate.projectedSpeed >= gate.projectedGateB;
	if (!gate.projectedGate) {
		gate.failCode = 4;
		return gate;
	}

	gate.windowGate = gate.length < gate.eventWindow;
	if (!gate.windowGate) {
		gate.failCode = 5;
		return gate;
	}

	gate.recalcGate = (gate.flags & 0x3000u) != 0x1000u;
	if (!gate.recalcGate) {
		gate.failCode = 6;
		return gate;
	}

	gate.selectorGate = (gate.flags & 0x3FC00000u) != 0;
	if (!gate.selectorGate) {
		gate.failCode = 7;
		return gate;
	}

	gate.reachesCounter = true;
	return gate;
}

static bool VPhysicsShouldLogPairInit(__int64 objectA, __int64 objectB)
{
	if (VPhysicsIsTrackedSurfaceMissObject(objectA) || VPhysicsIsTrackedSurfaceMissObject(objectB))
		return true;

	const VPhysicsObjectSnapshot a = VPhysicsReadObjectSnapshot(objectA);
	const VPhysicsObjectSnapshot b = VPhysicsReadObjectSnapshot(objectB);
	return VPhysicsShouldLogSurfaceMissObject(a.moveFlags, a.surfaceFlags, a.pairCount)
		|| VPhysicsShouldLogSurfaceMissObject(b.moveFlags, b.surfaceFlags, b.pairCount);
}

static bool VPhysicsShouldLogMindist(const VPhysicsMindistSnapshot& snapshot)
{
	if (!snapshot.readable)
		return false;

	return VPhysicsShouldLogPairInit(static_cast<__int64>(snapshot.objectA), static_cast<__int64>(snapshot.objectB));
}

struct VPhysicsDeferredQueueSnapshot {
	unsigned int count;
	unsigned int firstAllow;
	unsigned int firstHint;
	unsigned int lastAllow;
	unsigned int lastHint;
	unsigned int stackValue;
	int stackIndex;
	uintptr_t firstMindist;
	uintptr_t lastMindist;
	bool readable;
	bool firstReadable;
	bool lastReadable;
};

static uintptr_t VPhysicsDeferredQueueBase(uintptr_t manager, int mode)
{
	if (!manager || mode < 0 || mode > 3)
		return 0;

	return manager + static_cast<uintptr_t>(mode) * 0x40008ull;
}

static unsigned int VPhysicsReadDeferredMindistQueueIndex(__int64 mindist, int mode)
{
	if (!mindist || mode < 0 || mode > 3)
		return 0xffffu;

	return VPhysicsReadWordOrDefault(static_cast<uintptr_t>(mindist) + 0x34 + static_cast<uintptr_t>(mode) * sizeof(unsigned short), 0xffffu);
}

static void VPhysicsReadDeferredQueueEntry(uintptr_t entry, uintptr_t* mindist, unsigned int* allow, unsigned int* hint, bool* readable)
{
	if (mindist)
		*mindist = 0;
	if (allow)
		*allow = 0;
	if (hint)
		*hint = 0;
	if (readable)
		*readable = false;

	if (!entry || !IsReadableRange(reinterpret_cast<void*>(entry), 0x14))
		return;

	if (mindist)
		*mindist = VPhysicsReadQwordOrDefault(entry, 0);
	if (allow)
		VPhysicsReadByte(entry + 0x08, allow);
	if (hint)
		VPhysicsReadDword(entry + 0x10, hint);
	if (readable)
		*readable = true;
}

static VPhysicsDeferredQueueSnapshot VPhysicsReadDeferredQueueSnapshot(__int64 manager, int mode)
{
	VPhysicsDeferredQueueSnapshot snapshot = {};
	const uintptr_t managerAddress = static_cast<uintptr_t>(manager);
	const uintptr_t queueBase = VPhysicsDeferredQueueBase(managerAddress, mode);
	if (!queueBase)
		return snapshot;

	snapshot.stackIndex = VPhysicsReadManagerStackIndex(managerAddress);
	snapshot.stackValue = VPhysicsReadManagerStackValueRaw(managerAddress, snapshot.stackIndex);
	snapshot.readable = VPhysicsReadDword(queueBase + 0x40010, &snapshot.count);
	if (!snapshot.readable || snapshot.count == 0 || snapshot.count > 0x1fffu)
		return snapshot;

	VPhysicsReadDeferredQueueEntry(
		queueBase + 0x10,
		&snapshot.firstMindist,
		&snapshot.firstAllow,
		&snapshot.firstHint,
		&snapshot.firstReadable);
	VPhysicsReadDeferredQueueEntry(
		queueBase + 0x10 + static_cast<uintptr_t>(snapshot.count - 1u) * 0x20ull,
		&snapshot.lastMindist,
		&snapshot.lastAllow,
		&snapshot.lastHint,
		&snapshot.lastReadable);
	return snapshot;
}

static bool VPhysicsDeferredQueueLooksRelevant(const VPhysicsDeferredQueueSnapshot& snapshot)
{
	if (!snapshot.count)
		return false;

	if (snapshot.firstMindist && VPhysicsShouldLogMindist(VPhysicsReadMindistSnapshot(static_cast<__int64>(snapshot.firstMindist))))
		return true;
	if (snapshot.lastMindist && VPhysicsShouldLogMindist(VPhysicsReadMindistSnapshot(static_cast<__int64>(snapshot.lastMindist))))
		return true;
	return false;
}

static unsigned long long VPhysicsPairInitLogKey(
	__int64 pair,
	__int64 objectA,
	__int64 objectB,
	const VPhysicsPairSnapshot& before,
	const VPhysicsPairSnapshot& after)
{
	unsigned long long key = static_cast<unsigned long long>(pair);
	key ^= static_cast<unsigned long long>(objectA) + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2);
	key ^= static_cast<unsigned long long>(objectB) + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2);
	key ^= static_cast<unsigned long long>(before.indexA) << 16;
	key ^= static_cast<unsigned long long>(before.indexB) << 32;
	key ^= static_cast<unsigned long long>(after.indexA) << 40;
	key ^= static_cast<unsigned long long>(after.indexB) << 52;
	key ^= static_cast<unsigned long long>(*reinterpret_cast<const unsigned int*>(&after.distanceA)) << 7;
	key ^= static_cast<unsigned long long>(*reinterpret_cast<const unsigned int*>(&after.distanceB)) << 29;
	return key;
}

static unsigned long long VPhysicsUpdateExactMindistLogKey(
	__int64 mindist,
	const VPhysicsMindistSnapshot& before,
	const VPhysicsMindistSnapshot& after,
	int allowHullConversion,
	int eventHint)
{
	unsigned long long key = static_cast<unsigned long long>(mindist);
	key ^= static_cast<unsigned long long>(before.objectA) + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2);
	key ^= static_cast<unsigned long long>(before.objectB) + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2);
	key ^= static_cast<unsigned long long>(before.flags) << 7;
	key ^= static_cast<unsigned long long>(after.flags) << 29;
	key ^= static_cast<unsigned long long>(before.eventIndex) << 11;
	key ^= static_cast<unsigned long long>(after.eventIndex) << 33;
	key ^= static_cast<unsigned long long>(VPhysicsFloatBits(before.length)) << 17;
	key ^= static_cast<unsigned long long>(VPhysicsFloatBits(after.length)) << 41;
	key ^= static_cast<unsigned long long>(before.environmentSelectorCount) << 5;
	key ^= static_cast<unsigned long long>(after.environmentSelectorCount) << 37;
	key ^= static_cast<unsigned long long>(allowHullConversion & 0xff) << 53;
	key ^= static_cast<unsigned long long>(eventHint & 0xff) << 57;
	return key;
}

static void VPhysicsFormatPairObjectContext(__int64 object, char* out, size_t outSize)
{
	if (!out || !outSize)
		return;

	const VPhysicsObjectSnapshot snapshot = VPhysicsReadObjectSnapshot(object);
	_snprintf_s(
		out,
		outSize,
		_TRUNCATE,
		"obj=%p active=%u moveFlags=0x%08x state=0x%02x surfaceFlags=0x%08x sync=%u pairs=%d core=%p group=%p",
		reinterpret_cast<void*>(object),
		snapshot.active,
		snapshot.moveFlags,
		snapshot.moveFlags & 0xff,
		snapshot.surfaceFlags,
		snapshot.syncStamp,
		snapshot.pairCount,
		reinterpret_cast<void*>(snapshot.core),
		reinterpret_cast<void*>(snapshot.group));
}

static __int64 __fastcall VPhysicsNewPairInitProbe(__int64 rangeData, __int64 pair, __int64 objectA, __int64 objectB)
{
	const unsigned long long call = ++s_VPhysicsNewPairInitCalls;
	const bool shouldLog = VPhysicsShouldLogPairInit(objectA, objectB);
	const VPhysicsPairSnapshot before = shouldLog ? VPhysicsReadPairSnapshot(pair) : VPhysicsPairSnapshot{};
	__int64 result = 0;
	if (VPhysicsNewPairInitOriginal)
		result = VPhysicsNewPairInitOriginal(rangeData, pair, objectA, objectB);

	if (!shouldLog)
		return result;

	const VPhysicsPairSnapshot after = VPhysicsReadPairSnapshot(pair);
	const unsigned long long key = VPhysicsPairInitLogKey(pair, objectA, objectB, before, after);
	if (!s_VPhysicsNewPairInitLoggedKeys.insert(key).second)
		return result;
	char objectContextA[384];
	char objectContextB[384];
	VPhysicsFormatPairObjectContext(objectA, objectContextA, sizeof(objectContextA));
	VPhysicsFormatPairObjectContext(objectB, objectContextB, sizeof(objectContextB));

	char buffer[3072];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"VPHYSICS: newPairInit call=%llu range=%p pair=%p result=%p pairObj=%p/%p->%p/%p idx=%u/%u->%u/%u ctx=%p->%p distA=%.6f->%.6f distB=%.6f->%.6f posA=(%.3f %.3f %.3f)->(%.3f %.3f %.3f) posB=(%.3f %.3f %.3f)->(%.3f %.3f %.3f) key=0x%016llx A{%s} B{%s}\n",
		call,
		reinterpret_cast<void*>(rangeData),
		reinterpret_cast<void*>(pair),
		reinterpret_cast<void*>(result),
		reinterpret_cast<void*>(before.objectA),
		reinterpret_cast<void*>(before.objectB),
		reinterpret_cast<void*>(after.objectA),
		reinterpret_cast<void*>(after.objectB),
		before.indexA,
		before.indexB,
		after.indexA,
		after.indexB,
		reinterpret_cast<void*>(before.pairContext),
		reinterpret_cast<void*>(after.pairContext),
		before.distanceA,
		after.distanceA,
		before.distanceB,
		after.distanceB,
		before.positionA[0],
		before.positionA[1],
		before.positionA[2],
		after.positionA[0],
		after.positionA[1],
		after.positionA[2],
		before.positionB[0],
		before.positionB[1],
		before.positionB[2],
		after.positionB[0],
		after.positionB[1],
		after.positionB[2],
		key,
		objectContextA,
		objectContextB);
	VPhysicsStaticBVHProbeLog(buffer);
	return result;
}

static void __fastcall VPhysicsUpdateExactMindistEventsProbe(__int64 mindist, int allowHullConversion, int eventHint)
{
	const unsigned long long call = ++s_VPhysicsUpdateExactMindistEventsCalls;
	const bool diagnosticsEnabled = IsVPhysicsDiagnosticsEnabled();
	const VPhysicsCE300GateSnapshot gateBefore = VPhysicsBuildCE300GateSnapshot(mindist, allowHullConversion);

	const bool guardNegativeStack =
		IsVPhysicsGuardNegativeStackIndexEnabled()
		&& gateBefore.manager
		&& gateBefore.managerStackIndex < 0;
	LPCRITICAL_SECTION guardCriticalSection = nullptr;
	int guardSavedStackIndex = -1;
	unsigned int guardSavedStack0 = 0;
	bool guardApplied = false;
	if (guardNegativeStack) {
		const uintptr_t stackIndexAddress = VPhysicsManagerStackIndexAddress(gateBefore.manager);
		const uintptr_t stack0Address = VPhysicsManagerStackValueAddress(gateBefore.manager, 0);
		guardCriticalSection = VPhysicsManagerCriticalSection(gateBefore.manager);
		if (guardCriticalSection
			&& IsReadableRange(guardCriticalSection, sizeof(CRITICAL_SECTION))
			&& IsReadableRange(reinterpret_cast<void*>(stackIndexAddress), sizeof(unsigned int))
			&& IsReadableRange(reinterpret_cast<void*>(stack0Address), sizeof(unsigned int))) {
			EnterCriticalSection(guardCriticalSection);
			guardSavedStackIndex = VPhysicsReadIntOrDefault(stackIndexAddress, -1);
			guardSavedStack0 = VPhysicsReadDwordOrDefault(stack0Address, 0);
			// Empty phase stack means CE300 should behave as if no defer-phase bits are active.
			VPhysicsWriteDword(stackIndexAddress, 0);
			VPhysicsWriteDword(stack0Address, guardSavedStack0 & ~s_VPhysicsExactMindistDeferBit);
			guardApplied = true;
			++s_VPhysicsNegativeStackGuardCalls;
		}
	}

	if (!diagnosticsEnabled) {
		if (VPhysicsUpdateExactMindistEventsOriginal)
			VPhysicsUpdateExactMindistEventsOriginal(mindist, allowHullConversion, eventHint);
		if (guardApplied) {
			VPhysicsWriteDword(VPhysicsManagerStackValueAddress(gateBefore.manager, 0), guardSavedStack0);
			VPhysicsWriteDword(VPhysicsManagerStackIndexAddress(gateBefore.manager), static_cast<unsigned int>(guardSavedStackIndex));
			LeaveCriticalSection(guardCriticalSection);
		}
		return;
	}

	const uintptr_t returnAddress = reinterpret_cast<uintptr_t>(_ReturnAddress());
	const uintptr_t returnRva = s_R1OVPhysicsBase && returnAddress >= s_R1OVPhysicsBase
		? returnAddress - s_R1OVPhysicsBase
		: 0;
	const DWORD threadId = GetCurrentThreadId();
	const VPhysicsMindistSnapshot before = VPhysicsReadMindistSnapshot(mindist);
	VPhysicsMaybeArmGuardPageTrace(gateBefore.manager, "updateExactMindist");
	const bool shouldLog = VPhysicsShouldLogMindist(before);
	const bool forceSelectorTick = shouldLog
		&& IsVPhysicsForceSelectorTickEnabled()
		&& before.environmentSelectorCountReadable;
	if (forceSelectorTick)
		VPhysicsWriteDword(before.environment + 0x114, 3);

	if (VPhysicsUpdateExactMindistEventsOriginal)
		VPhysicsUpdateExactMindistEventsOriginal(mindist, allowHullConversion, eventHint);

	if (guardApplied) {
		VPhysicsWriteDword(VPhysicsManagerStackValueAddress(gateBefore.manager, 0), guardSavedStack0);
		VPhysicsWriteDword(VPhysicsManagerStackIndexAddress(gateBefore.manager), static_cast<unsigned int>(guardSavedStackIndex));
		LeaveCriticalSection(guardCriticalSection);
	}

	if (!shouldLog)
		return;

	const VPhysicsMindistSnapshot after = VPhysicsReadMindistSnapshot(mindist);
	const bool counterDirectPath = before.readable
		&& before.environmentSelectorCountReadable
		&& ((before.flags & 0x3000u) != 0x1000u)
		&& ((before.flags & 0x3FC00000u) != 0);
	const unsigned int counterExpectedDirect = counterDirectPath
		? (before.environmentSelectorCount > 2 ? 0 : before.environmentSelectorCount + 1)
		: 0xffffffffu;
	const bool counterUnexpected = counterDirectPath
		&& after.environmentSelectorCountReadable
		&& after.environmentSelectorCount != counterExpectedDirect;
	const unsigned long long key = VPhysicsUpdateExactMindistLogKey(mindist, before, after, allowHullConversion, eventHint);
	if (!s_VPhysicsUpdateExactMindistLoggedKeys.insert(key).second)
		return;
	char objectContextA[384];
	char objectContextB[384];
	VPhysicsFormatPairObjectContext(static_cast<__int64>(before.objectA), objectContextA, sizeof(objectContextA));
	VPhysicsFormatPairObjectContext(static_cast<__int64>(before.objectB), objectContextB, sizeof(objectContextB));

	char buffer[8192];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"VPHYSICS: updateExactMindist call=%llu mindist=%p retRva=0x%llx tid=%lu allowHull=%d eventHint=%d forceSelectorTick=%d negStackGuard=%d/%llu savedStack=%d/0x%08x counterDirect=%d counterExpected=%u counterUnexpected=%d ceGate=%d ceFail=%u ceSlots=%u/%u ceObj=%p/%p ceCore=%p/%p ceMgr=%p ceStack=%d/0x%08x ceGates=%d/%d/%d/%d/%d/%d/%d ceLen=%.9g/0x%08x ceColl=%.9g/0x%08x ceHull=%.9g ceProj=%.9g/0x%08x ceWindow=%.9g/0x%08x ceRel=%.9g ceNorm=%.9g/%.9g ceRot=%.9g/%.9g ceRotDot=%.9g/%.9g ceDt=%.9g ceEnvStep=%.9g ceWorst=%.9g ceConst=%.9g/%.9g/%.9g readable=%d->%d env=%p->%p envCount=%u/%d->%u/%d envStep=%.6f now=%.6f next=%.6f eventQueue=%p->%p eventHeap=%p->%p eventDueRel=%.6f/%d->%.6f/%d eventDueAbs=%.6f->%.6f eventDueDelta=%.6f->%.6f obj=%p/%p->%p/%p flags=0x%08x->0x%08x collType=0x%02x->0x%02x sort=%u->%u func=%u->%u recalc=%u->%u status=%u->%u selector=%u->%u eventIndex=0x%x->0x%x len=%.6f->%.6f normal=(%.3f %.3f %.3f)->(%.3f %.3f %.3f) coreA=%p speedA=(%.3f %.3f %.3f) curA=%.3f maxRotA=%.3f rotA=(%.3f %.3f %.3f) coreB=%p speedB=(%.3f %.3f %.3f) curB=%.3f maxRotB=%.3f rotB=(%.3f %.3f %.3f) key=0x%016llx A{%s} B{%s}\n",
		call,
		reinterpret_cast<void*>(mindist),
		static_cast<unsigned long long>(returnRva),
		static_cast<unsigned long>(threadId),
		allowHullConversion,
		eventHint,
		forceSelectorTick ? 1 : 0,
		guardApplied ? 1 : 0,
		static_cast<unsigned long long>(s_VPhysicsNegativeStackGuardCalls),
		guardSavedStackIndex,
		guardSavedStack0,
		counterDirectPath ? 1 : 0,
		counterExpectedDirect,
		counterUnexpected ? 1 : 0,
		gateBefore.reachesCounter ? 1 : 0,
		gateBefore.failCode,
		gateBefore.slotA,
		gateBefore.slotB,
		reinterpret_cast<void*>(gateBefore.objectA),
		reinterpret_cast<void*>(gateBefore.objectB),
		reinterpret_cast<void*>(gateBefore.coreA),
		reinterpret_cast<void*>(gateBefore.coreB),
		reinterpret_cast<void*>(gateBefore.manager),
		gateBefore.managerStackIndex,
		gateBefore.managerStackValue,
		gateBefore.statusGate ? 1 : 0,
		gateBefore.managerGate ? 1 : 0,
		gateBefore.hullGate ? 1 : 0,
		gateBefore.projectedGate ? 1 : 0,
		gateBefore.windowGate ? 1 : 0,
		gateBefore.recalcGate ? 1 : 0,
		gateBefore.selectorGate ? 1 : 0,
		gateBefore.length,
		gateBefore.lengthBits,
		gateBefore.collDist,
		gateBefore.collDistBits,
		gateBefore.hullThreshold,
		gateBefore.projectedSpeed,
		gateBefore.projectedSpeedBits,
		gateBefore.eventWindow,
		gateBefore.eventWindowBits,
		gateBefore.relativeNormalSpeed,
		gateBefore.normalSpeedA,
		gateBefore.normalSpeedB,
		gateBefore.rotationContributionA,
		gateBefore.rotationContributionB,
		gateBefore.rotationDotA,
		gateBefore.rotationDotB,
		gateBefore.deltaTime,
		gateBefore.envDeltaPsi,
		gateBefore.worstCaseSpeed,
		gateBefore.hullScale,
		gateBefore.projectedGateA,
		gateBefore.projectedGateB,
		before.readable ? 1 : 0,
		after.readable ? 1 : 0,
		reinterpret_cast<void*>(before.environment),
		reinterpret_cast<void*>(after.environment),
		before.environmentSelectorCount,
		before.environmentSelectorCountReadable ? 1 : 0,
		after.environmentSelectorCount,
		after.environmentSelectorCountReadable ? 1 : 0,
		before.environmentDeltaPsi,
		before.environmentNow,
		before.environmentNext,
		reinterpret_cast<void*>(before.eventQueue),
		reinterpret_cast<void*>(after.eventQueue),
		reinterpret_cast<void*>(before.eventHeap),
		reinterpret_cast<void*>(after.eventHeap),
		before.eventDueRelative,
		before.eventDueReadable ? 1 : 0,
		after.eventDueRelative,
		after.eventDueReadable ? 1 : 0,
		before.eventDueAbsolute,
		after.eventDueAbsolute,
		before.eventDueReadable ? before.eventDueAbsolute - before.environmentNow : 0.0,
		after.eventDueReadable ? after.eventDueAbsolute - after.environmentNow : 0.0,
		reinterpret_cast<void*>(before.objectA),
		reinterpret_cast<void*>(before.objectB),
		reinterpret_cast<void*>(after.objectA),
		reinterpret_cast<void*>(after.objectB),
		before.flags,
		after.flags,
		before.collType,
		after.collType,
		before.synapseSort,
		after.synapseSort,
		before.mindistFunction,
		after.mindistFunction,
		before.recalcResult,
		after.recalcResult,
		before.mindistStatus,
		after.mindistStatus,
		before.collDistSelector,
		after.collDistSelector,
		before.eventIndex,
		after.eventIndex,
		before.length,
		after.length,
		before.contactPlane[0],
		before.contactPlane[1],
		before.contactPlane[2],
		after.contactPlane[0],
		after.contactPlane[1],
		after.contactPlane[2],
		reinterpret_cast<void*>(before.coreA),
		before.coreASpeed[0],
		before.coreASpeed[1],
		before.coreASpeed[2],
		before.coreACurrentSpeed,
		before.coreAMaxSurfaceRotSpeed,
		before.coreARotationAxis[0],
		before.coreARotationAxis[1],
		before.coreARotationAxis[2],
		reinterpret_cast<void*>(before.coreB),
		before.coreBSpeed[0],
		before.coreBSpeed[1],
		before.coreBSpeed[2],
		before.coreBCurrentSpeed,
		before.coreBMaxSurfaceRotSpeed,
		before.coreBRotationAxis[0],
		before.coreBRotationAxis[1],
		before.coreBRotationAxis[2],
		key,
		objectContextA,
		objectContextB);
	VPhysicsStaticBVHProbeLog(buffer);
}

static __int64 __fastcall VPhysicsUpdateExactMindistDeferredProbe(__int64 manager, int mode, __int64 context)
{
	const unsigned long long call = ++s_VPhysicsUpdateExactMindistDeferredCalls;
	const uintptr_t returnAddress = reinterpret_cast<uintptr_t>(_ReturnAddress());
	const uintptr_t returnRva = s_R1OVPhysicsBase && returnAddress >= s_R1OVPhysicsBase
		? returnAddress - s_R1OVPhysicsBase
		: 0;
	const DWORD threadId = GetCurrentThreadId();
	VPhysicsMaybeArmGuardPageTrace(static_cast<uintptr_t>(manager), "updateExactDeferred");

	__int64 mindist = 0;
	unsigned int contextAllow = 0;
	unsigned int contextHint = 0;
	if (context && IsReadableRange(reinterpret_cast<void*>(context), 0x14)) {
		mindist = static_cast<__int64>(VPhysicsReadQwordOrDefault(static_cast<uintptr_t>(context), 0));
		VPhysicsReadByte(static_cast<uintptr_t>(context) + 0x08, &contextAllow);
		VPhysicsReadDword(static_cast<uintptr_t>(context) + 0x10, &contextHint);
	}

	const VPhysicsDeferredQueueSnapshot queueBefore = VPhysicsReadDeferredQueueSnapshot(manager, mode);
	const unsigned int queueIndexBefore = VPhysicsReadDeferredMindistQueueIndex(mindist, mode);
	const VPhysicsMindistSnapshot before = VPhysicsReadMindistSnapshot(mindist);
	__int64 result = VPhysicsUpdateExactMindistDeferredOriginal
		? VPhysicsUpdateExactMindistDeferredOriginal(manager, mode, context)
		: 0;
	const VPhysicsMindistSnapshot after = VPhysicsReadMindistSnapshot(mindist);
	const VPhysicsDeferredQueueSnapshot queueAfter = VPhysicsReadDeferredQueueSnapshot(manager, mode);
	const unsigned int queueIndexAfter = VPhysicsReadDeferredMindistQueueIndex(mindist, mode);
	const bool shouldLog = VPhysicsShouldLogMindist(before) || VPhysicsShouldLogMindist(after);
	if (!shouldLog)
		return result;
	char objectContextA[384];
	char objectContextB[384];
	VPhysicsFormatPairObjectContext(static_cast<__int64>(before.objectA ? before.objectA : after.objectA), objectContextA, sizeof(objectContextA));
	VPhysicsFormatPairObjectContext(static_cast<__int64>(before.objectB ? before.objectB : after.objectB), objectContextB, sizeof(objectContextB));

	char buffer[4096];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"VPHYSICS: updateExactDeferred call=%llu retRva=0x%llx tid=%lu manager=%p mode=%d context=%p contextAllow=%u contextHint=%u result=%p queueCount=%u/%d->%u/%d queueIndex=0x%x->0x%x queueStack=%d/0x%08x->%d/0x%08x queueFirst=%p[%u/%u]->%p[%u/%u] queueLast=%p[%u/%u]->%p[%u/%u] mindist=%p readable=%d->%d env=%p->%p envCount=%u/%d->%u/%d flags=0x%08x->0x%08x status=%u->%u selector=%u->%u eventIndex=0x%x->0x%x len=%.9g->%.9g normal=(%.3f %.3f %.3f)->(%.3f %.3f %.3f) obj=%p/%p->%p/%p A{%s} B{%s}\n",
		call,
		static_cast<unsigned long long>(returnRva),
		static_cast<unsigned long>(threadId),
		reinterpret_cast<void*>(manager),
		mode,
		reinterpret_cast<void*>(context),
		contextAllow,
		contextHint,
		reinterpret_cast<void*>(result),
		queueBefore.count,
		queueBefore.readable ? 1 : 0,
		queueAfter.count,
		queueAfter.readable ? 1 : 0,
		queueIndexBefore,
		queueIndexAfter,
		queueBefore.stackIndex,
		queueBefore.stackValue,
		queueAfter.stackIndex,
		queueAfter.stackValue,
		reinterpret_cast<void*>(queueBefore.firstMindist),
		queueBefore.firstAllow,
		queueBefore.firstHint,
		reinterpret_cast<void*>(queueAfter.firstMindist),
		queueAfter.firstAllow,
		queueAfter.firstHint,
		reinterpret_cast<void*>(queueBefore.lastMindist),
		queueBefore.lastAllow,
		queueBefore.lastHint,
		reinterpret_cast<void*>(queueAfter.lastMindist),
		queueAfter.lastAllow,
		queueAfter.lastHint,
		reinterpret_cast<void*>(mindist),
		before.readable ? 1 : 0,
		after.readable ? 1 : 0,
		reinterpret_cast<void*>(before.environment),
		reinterpret_cast<void*>(after.environment),
		before.environmentSelectorCount,
		before.environmentSelectorCountReadable ? 1 : 0,
		after.environmentSelectorCount,
		after.environmentSelectorCountReadable ? 1 : 0,
		before.flags,
		after.flags,
		before.mindistStatus,
		after.mindistStatus,
		before.collDistSelector,
		after.collDistSelector,
		before.eventIndex,
		after.eventIndex,
		before.length,
		after.length,
		before.contactPlane[0],
		before.contactPlane[1],
		before.contactPlane[2],
		after.contactPlane[0],
		after.contactPlane[1],
		after.contactPlane[2],
		reinterpret_cast<void*>(before.objectA),
		reinterpret_cast<void*>(before.objectB),
		reinterpret_cast<void*>(after.objectA),
		reinterpret_cast<void*>(after.objectB),
		objectContextA,
		objectContextB);
	VPhysicsStaticBVHProbeLog(buffer);
	return result;
}

static __int64 __fastcall VPhysicsDeferredFlushProbe(__int64 manager, int mask)
{
	VPhysicsMaybeArmGuardPageTrace(static_cast<uintptr_t>(manager), "deferredFlush");

	if (!IsVPhysicsDeferredProbeLogEnabled()) {
		return VPhysicsDeferredFlushOriginal
			? VPhysicsDeferredFlushOriginal(manager, mask)
			: 0;
	}

	const unsigned long long call = ++s_VPhysicsDeferredFlushCalls;
	const uintptr_t returnAddress = reinterpret_cast<uintptr_t>(_ReturnAddress());
	const uintptr_t returnRva = s_R1OVPhysicsBase && returnAddress >= s_R1OVPhysicsBase
		? returnAddress - s_R1OVPhysicsBase
		: 0;

	VPhysicsDeferredQueueSnapshot before[4] = {};
	for (int mode = 0; mode < 4; ++mode)
		before[mode] = VPhysicsReadDeferredQueueSnapshot(manager, mode);

	const bool relevantBefore =
		VPhysicsDeferredQueueLooksRelevant(before[0])
		|| VPhysicsDeferredQueueLooksRelevant(before[1])
		|| VPhysicsDeferredQueueLooksRelevant(before[2])
		|| VPhysicsDeferredQueueLooksRelevant(before[3]);

	__int64 result = VPhysicsDeferredFlushOriginal
		? VPhysicsDeferredFlushOriginal(manager, mask)
		: 0;

	VPhysicsDeferredQueueSnapshot after[4] = {};
	for (int mode = 0; mode < 4; ++mode)
		after[mode] = VPhysicsReadDeferredQueueSnapshot(manager, mode);

	const bool relevantAfter =
		VPhysicsDeferredQueueLooksRelevant(after[0])
		|| VPhysicsDeferredQueueLooksRelevant(after[1])
		|| VPhysicsDeferredQueueLooksRelevant(after[2])
		|| VPhysicsDeferredQueueLooksRelevant(after[3]);
	const bool mode3Changed = before[3].count || after[3].count || ((mask & 8) != 0);
	if (!mode3Changed && !relevantBefore && !relevantAfter)
		return result;
	if (!relevantBefore && !relevantAfter && before[3].count == 0 && after[3].count == 0)
		return result;

	char buffer[2048];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"VPHYSICS: deferredFlush call=%llu retRva=0x%llx manager=%p mask=0x%08x result=%p stack=%d/0x%08x->%d/0x%08x q0=%u/%d->%u/%d q1=%u/%d->%u/%d q2=%u/%d->%u/%d q3=%u/%d->%u/%d q3First=%p[%u/%u]->%p[%u/%u] q3Last=%p[%u/%u]->%p[%u/%u] relevant=%d->%d\n",
		call,
		static_cast<unsigned long long>(returnRva),
		reinterpret_cast<void*>(manager),
		mask,
		reinterpret_cast<void*>(result),
		before[3].stackIndex,
		before[3].stackValue,
		after[3].stackIndex,
		after[3].stackValue,
		before[0].count,
		before[0].readable ? 1 : 0,
		after[0].count,
		after[0].readable ? 1 : 0,
		before[1].count,
		before[1].readable ? 1 : 0,
		after[1].count,
		after[1].readable ? 1 : 0,
		before[2].count,
		before[2].readable ? 1 : 0,
		after[2].count,
		after[2].readable ? 1 : 0,
		before[3].count,
		before[3].readable ? 1 : 0,
		after[3].count,
		after[3].readable ? 1 : 0,
		reinterpret_cast<void*>(before[3].firstMindist),
		before[3].firstAllow,
		before[3].firstHint,
		reinterpret_cast<void*>(after[3].firstMindist),
		after[3].firstAllow,
		after[3].firstHint,
		reinterpret_cast<void*>(before[3].lastMindist),
		before[3].lastAllow,
		before[3].lastHint,
		reinterpret_cast<void*>(after[3].lastMindist),
		after[3].lastAllow,
		after[3].lastHint,
		relevantBefore ? 1 : 0,
		relevantAfter ? 1 : 0);
	VPhysicsStaticBVHProbeLog(buffer);
	return result;
}

static __int64 __fastcall VPhysicsDoImpactProbe(__int64 mindist)
{
	const unsigned long long call = ++s_VPhysicsDoImpactCalls;
	const uintptr_t returnAddress = reinterpret_cast<uintptr_t>(_ReturnAddress());
	const uintptr_t returnRva = s_R1OVPhysicsBase && returnAddress >= s_R1OVPhysicsBase
		? returnAddress - s_R1OVPhysicsBase
		: 0;

	const VPhysicsMindistSnapshot before = VPhysicsReadMindistSnapshot(mindist);
	const bool shouldLogBefore = VPhysicsShouldLogMindist(before);
	const VPhysicsObjectSnapshot objectABefore = shouldLogBefore
		? VPhysicsReadObjectSnapshot(static_cast<__int64>(before.objectA))
		: VPhysicsObjectSnapshot{};
	const VPhysicsObjectSnapshot objectBBefore = shouldLogBefore
		? VPhysicsReadObjectSnapshot(static_cast<__int64>(before.objectB))
		: VPhysicsObjectSnapshot{};
	const VPhysicsTfoTlsScratchSnapshot tlsBefore = shouldLogBefore
		? VPhysicsReadTfoTlsScratchSnapshot()
		: VPhysicsTfoTlsScratchSnapshot{};

	__int64 result = 0;
	if (VPhysicsDoImpactOriginal)
		result = VPhysicsDoImpactOriginal(mindist);

	const VPhysicsMindistSnapshot after = VPhysicsReadMindistSnapshot(mindist);
	const bool shouldLog = shouldLogBefore || VPhysicsShouldLogMindist(after);
	if (!shouldLog)
		return result;

	const VPhysicsTfoTlsScratchSnapshot tlsAfter = VPhysicsReadTfoTlsScratchSnapshot();
	const VPhysicsObjectSnapshot objectAAfter = VPhysicsReadObjectSnapshot(static_cast<__int64>(after.objectA ? after.objectA : before.objectA));
	const VPhysicsObjectSnapshot objectBAfter = VPhysicsReadObjectSnapshot(static_cast<__int64>(after.objectB ? after.objectB : before.objectB));
	char objectContextA[384];
	char objectContextB[384];
	const __int64 objectA = static_cast<__int64>(before.objectA ? before.objectA : after.objectA);
	const __int64 objectB = static_cast<__int64>(before.objectB ? before.objectB : after.objectB);
	VPhysicsFormatPairObjectContext(objectA, objectContextA, sizeof(objectContextA));
	VPhysicsFormatPairObjectContext(objectB, objectContextB, sizeof(objectContextB));

	char buffer[8192];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"VPHYSICS: eventCallback call=%llu mindist=%p retRva=0x%llx result=%p tlsIdx=%u/%d tlsArray=%p/%d tlsBase=%p/%d slot=%p/%d scratch=%p/%d->%p/%d ref=%u->%u raw=%p->%p free=%p->%p begin=%p->%p end=%p->%p cap=%u->%u readable=%d->%d env=%p->%p envCount=%u/%d->%u/%d envStep=%.6f now=%.6f next=%.6f eventQueue=%p->%p eventHeap=%p->%p eventDueRel=%.6f/%d->%.6f/%d eventDueAbs=%.6f->%.6f eventDueDelta=%.6f->%.6f obj=%p/%p->%p/%p objAState=0x%02x->0x%02x objBState=0x%02x->0x%02x objAMove=0x%08x->0x%08x objBMove=0x%08x->0x%08x objASurface=0x%08x->0x%08x objBSurface=0x%08x->0x%08x objASync=%u->%u objBSync=%u->%u objAPairs=%d->%d objBPairs=%d->%d flags=0x%08x->0x%08x collType=0x%02x->0x%02x sort=%u->%u func=%u->%u recalc=%u->%u status=%u->%u selector=%u->%u eventIndex=0x%x->0x%x len=%.6f->%.6f normal=(%.3f %.3f %.3f)->(%.3f %.3f %.3f) coreA=%p->%p speedA=(%.3f %.3f %.3f)->(%.3f %.3f %.3f) curA=%.3f->%.3f maxRotA=%.3f->%.3f coreB=%p->%p speedB=(%.3f %.3f %.3f)->(%.3f %.3f %.3f) curB=%.3f->%.3f maxRotB=%.3f->%.3f A{%s} B{%s}\n",
		call,
		reinterpret_cast<void*>(mindist),
		static_cast<unsigned long long>(returnRva),
		reinterpret_cast<void*>(result),
		tlsAfter.tlsIndex,
		tlsAfter.tlsIndexReadable ? 1 : 0,
		reinterpret_cast<void*>(tlsAfter.tlsArray),
		tlsAfter.tlsArrayReadable ? 1 : 0,
		reinterpret_cast<void*>(tlsAfter.tlsBase),
		tlsAfter.tlsBaseReadable ? 1 : 0,
		reinterpret_cast<void*>(tlsAfter.slotAddress),
		tlsAfter.slotReadable ? 1 : 0,
		reinterpret_cast<void*>(tlsBefore.scratch),
		tlsBefore.scratchReadable ? 1 : 0,
		reinterpret_cast<void*>(tlsAfter.scratch),
		tlsAfter.scratchReadable ? 1 : 0,
		tlsBefore.refCount,
		tlsAfter.refCount,
		reinterpret_cast<void*>(tlsBefore.rawBlock),
		reinterpret_cast<void*>(tlsAfter.rawBlock),
		reinterpret_cast<void*>(tlsBefore.freeList),
		reinterpret_cast<void*>(tlsAfter.freeList),
		reinterpret_cast<void*>(tlsBefore.begin),
		reinterpret_cast<void*>(tlsAfter.begin),
		reinterpret_cast<void*>(tlsBefore.end),
		reinterpret_cast<void*>(tlsAfter.end),
		tlsBefore.capacity,
		tlsAfter.capacity,
		before.readable ? 1 : 0,
		after.readable ? 1 : 0,
		reinterpret_cast<void*>(before.environment),
		reinterpret_cast<void*>(after.environment),
		before.environmentSelectorCount,
		before.environmentSelectorCountReadable ? 1 : 0,
		after.environmentSelectorCount,
		after.environmentSelectorCountReadable ? 1 : 0,
		before.environmentDeltaPsi,
		before.environmentNow,
		before.environmentNext,
		reinterpret_cast<void*>(before.eventQueue),
		reinterpret_cast<void*>(after.eventQueue),
		reinterpret_cast<void*>(before.eventHeap),
		reinterpret_cast<void*>(after.eventHeap),
		before.eventDueRelative,
		before.eventDueReadable ? 1 : 0,
		after.eventDueRelative,
		after.eventDueReadable ? 1 : 0,
		before.eventDueAbsolute,
		after.eventDueAbsolute,
		before.eventDueReadable ? before.eventDueAbsolute - before.environmentNow : 0.0,
		after.eventDueReadable ? after.eventDueAbsolute - after.environmentNow : 0.0,
		reinterpret_cast<void*>(before.objectA),
		reinterpret_cast<void*>(before.objectB),
		reinterpret_cast<void*>(after.objectA),
		reinterpret_cast<void*>(after.objectB),
		objectABefore.moveFlags & 0xff,
		objectAAfter.moveFlags & 0xff,
		objectBBefore.moveFlags & 0xff,
		objectBAfter.moveFlags & 0xff,
		objectABefore.moveFlags,
		objectAAfter.moveFlags,
		objectBBefore.moveFlags,
		objectBAfter.moveFlags,
		objectABefore.surfaceFlags,
		objectAAfter.surfaceFlags,
		objectBBefore.surfaceFlags,
		objectBAfter.surfaceFlags,
		objectABefore.syncStamp,
		objectAAfter.syncStamp,
		objectBBefore.syncStamp,
		objectBAfter.syncStamp,
		objectABefore.pairCount,
		objectAAfter.pairCount,
		objectBBefore.pairCount,
		objectBAfter.pairCount,
		before.flags,
		after.flags,
		before.collType,
		after.collType,
		before.synapseSort,
		after.synapseSort,
		before.mindistFunction,
		after.mindistFunction,
		before.recalcResult,
		after.recalcResult,
		before.mindistStatus,
		after.mindistStatus,
		before.collDistSelector,
		after.collDistSelector,
		before.eventIndex,
		after.eventIndex,
		before.length,
		after.length,
		before.contactPlane[0],
		before.contactPlane[1],
		before.contactPlane[2],
		after.contactPlane[0],
		after.contactPlane[1],
		after.contactPlane[2],
		reinterpret_cast<void*>(before.coreA),
		reinterpret_cast<void*>(after.coreA),
		before.coreASpeed[0],
		before.coreASpeed[1],
		before.coreASpeed[2],
		after.coreASpeed[0],
		after.coreASpeed[1],
		after.coreASpeed[2],
		before.coreACurrentSpeed,
		after.coreACurrentSpeed,
		before.coreAMaxSurfaceRotSpeed,
		after.coreAMaxSurfaceRotSpeed,
		reinterpret_cast<void*>(before.coreB),
		reinterpret_cast<void*>(after.coreB),
		before.coreBSpeed[0],
		before.coreBSpeed[1],
		before.coreBSpeed[2],
		after.coreBSpeed[0],
		after.coreBSpeed[1],
		after.coreBSpeed[2],
		before.coreBCurrentSpeed,
		after.coreBCurrentSpeed,
		before.coreBMaxSurfaceRotSpeed,
		after.coreBMaxSurfaceRotSpeed,
		objectContextA,
		objectContextB);
	VPhysicsStaticBVHProbeLog(buffer);
	return result;
}

static void __fastcall VPhysicsExactToHullProbe(__int64 mindist, float hullTime0, float hullTime1)
{
	const unsigned long long call = ++s_VPhysicsExactToHullCalls;
	const uintptr_t returnAddress = reinterpret_cast<uintptr_t>(_ReturnAddress());
	const uintptr_t returnRva = s_R1OVPhysicsBase && returnAddress >= s_R1OVPhysicsBase
		? returnAddress - s_R1OVPhysicsBase
		: 0;

	const VPhysicsMindistSnapshot before = VPhysicsReadMindistSnapshot(mindist);
	const bool shouldLogBefore = VPhysicsShouldLogMindist(before);

	if (VPhysicsExactToHullOriginal)
		VPhysicsExactToHullOriginal(mindist, hullTime0, hullTime1);

	const VPhysicsMindistSnapshot after = VPhysicsReadMindistSnapshot(mindist);
	const bool shouldLog = shouldLogBefore || VPhysicsShouldLogMindist(after);
	if (!shouldLog)
		return;

	const __int64 objectA = static_cast<__int64>(before.objectA ? before.objectA : after.objectA);
	const __int64 objectB = static_cast<__int64>(before.objectB ? before.objectB : after.objectB);
	char objectContextA[384];
	char objectContextB[384];
	VPhysicsFormatPairObjectContext(objectA, objectContextA, sizeof(objectContextA));
	VPhysicsFormatPairObjectContext(objectB, objectContextB, sizeof(objectContextB));

	char buffer[4096];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"VPHYSICS: exactToHull call=%llu mindist=%p retRva=0x%llx hullIn=(%.6f %.6f) readable=%d->%d env=%p->%p envCount=%u/%d->%u/%d envStep=%.6f now=%.6f next=%.6f eventQueue=%p->%p eventHeap=%p->%p eventDueRel=%.6f/%d->%.6f/%d eventDueAbs=%.6f->%.6f eventDueDelta=%.6f->%.6f obj=%p/%p->%p/%p flags=0x%08x->0x%08x collType=0x%02x->0x%02x sort=%u->%u func=%u->%u recalc=%u->%u status=%u->%u selector=%u->%u collDist=%.6f/%d hullCheckLen=%.6f hullTimeLen=%.6f worstSpeed=%.6f eventIndex=0x%x->0x%x len=%.6f->%.6f normal=(%.3f %.3f %.3f)->(%.3f %.3f %.3f) A{%s} B{%s}\n",
		call,
		reinterpret_cast<void*>(mindist),
		static_cast<unsigned long long>(returnRva),
		hullTime0,
		hullTime1,
		before.readable ? 1 : 0,
		after.readable ? 1 : 0,
		reinterpret_cast<void*>(before.environment),
		reinterpret_cast<void*>(after.environment),
		before.environmentSelectorCount,
		before.environmentSelectorCountReadable ? 1 : 0,
		after.environmentSelectorCount,
		after.environmentSelectorCountReadable ? 1 : 0,
		before.environmentDeltaPsi,
		before.environmentNow,
		before.environmentNext,
		reinterpret_cast<void*>(before.eventQueue),
		reinterpret_cast<void*>(after.eventQueue),
		reinterpret_cast<void*>(before.eventHeap),
		reinterpret_cast<void*>(after.eventHeap),
		before.eventDueRelative,
		before.eventDueReadable ? 1 : 0,
		after.eventDueRelative,
		after.eventDueReadable ? 1 : 0,
		before.eventDueAbsolute,
		after.eventDueAbsolute,
		before.eventDueReadable ? before.eventDueAbsolute - before.environmentNow : 0.0,
		after.eventDueReadable ? after.eventDueAbsolute - after.environmentNow : 0.0,
		reinterpret_cast<void*>(before.objectA),
		reinterpret_cast<void*>(before.objectB),
		reinterpret_cast<void*>(after.objectA),
		reinterpret_cast<void*>(after.objectB),
		before.flags,
		after.flags,
		before.collType,
		after.collType,
		before.synapseSort,
		after.synapseSort,
		before.mindistFunction,
		after.mindistFunction,
		before.recalcResult,
		after.recalcResult,
		before.mindistStatus,
		after.mindistStatus,
		before.collDistSelector,
		after.collDistSelector,
		before.collDist,
		before.collDistReadable ? 1 : 0,
		before.hullCheckLen,
		before.hullTimeFromLength,
		before.worstCaseSpeed,
		before.eventIndex,
		after.eventIndex,
		before.length,
		after.length,
		before.contactPlane[0],
		before.contactPlane[1],
		before.contactPlane[2],
		after.contactPlane[0],
		after.contactPlane[1],
		after.contactPlane[2],
		objectContextA,
		objectContextB);
	VPhysicsStaticBVHProbeLog(buffer);
}

static void __fastcall VPhysicsMindistEventProbe(__int64 mindist, float eventTime)
{
	const unsigned long long call = ++s_VPhysicsMindistEventCalls;
	const uintptr_t returnAddress = reinterpret_cast<uintptr_t>(_ReturnAddress());
	const uintptr_t returnRva = s_R1OVPhysicsBase && returnAddress >= s_R1OVPhysicsBase
		? returnAddress - s_R1OVPhysicsBase
		: 0;

	const VPhysicsMindistSnapshot before = VPhysicsReadMindistSnapshot(mindist);
	const bool shouldLogBefore = VPhysicsShouldLogMindist(before);

	if (VPhysicsMindistEventOriginal)
		VPhysicsMindistEventOriginal(mindist, eventTime);

	const VPhysicsMindistSnapshot after = VPhysicsReadMindistSnapshot(mindist);
	const bool shouldLog = shouldLogBefore || VPhysicsShouldLogMindist(after);
	if (!shouldLog)
		return;

	const __int64 objectA = static_cast<__int64>(before.objectA ? before.objectA : after.objectA);
	const __int64 objectB = static_cast<__int64>(before.objectB ? before.objectB : after.objectB);
	char objectContextA[384];
	char objectContextB[384];
	VPhysicsFormatPairObjectContext(objectA, objectContextA, sizeof(objectContextA));
	VPhysicsFormatPairObjectContext(objectB, objectContextB, sizeof(objectContextB));

	char buffer[4096];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"VPHYSICS: mindistEvent call=%llu mindist=%p retRva=0x%llx eventTime=%.6f readable=%d->%d env=%p->%p envCount=%u/%d->%u/%d envStep=%.6f now=%.6f next=%.6f eventQueue=%p->%p eventHeap=%p->%p eventDueRel=%.6f/%d->%.6f/%d eventDueAbs=%.6f->%.6f eventDueDelta=%.6f->%.6f obj=%p/%p->%p/%p flags=0x%08x->0x%08x collType=0x%02x->0x%02x sort=%u->%u func=%u->%u recalc=%u->%u status=%u->%u selector=%u->%u eventIndex=0x%x->0x%x len=%.6f->%.6f normal=(%.3f %.3f %.3f)->(%.3f %.3f %.3f) A{%s} B{%s}\n",
		call,
		reinterpret_cast<void*>(mindist),
		static_cast<unsigned long long>(returnRva),
		eventTime,
		before.readable ? 1 : 0,
		after.readable ? 1 : 0,
		reinterpret_cast<void*>(before.environment),
		reinterpret_cast<void*>(after.environment),
		before.environmentSelectorCount,
		before.environmentSelectorCountReadable ? 1 : 0,
		after.environmentSelectorCount,
		after.environmentSelectorCountReadable ? 1 : 0,
		before.environmentDeltaPsi,
		before.environmentNow,
		before.environmentNext,
		reinterpret_cast<void*>(before.eventQueue),
		reinterpret_cast<void*>(after.eventQueue),
		reinterpret_cast<void*>(before.eventHeap),
		reinterpret_cast<void*>(after.eventHeap),
		before.eventDueRelative,
		before.eventDueReadable ? 1 : 0,
		after.eventDueRelative,
		after.eventDueReadable ? 1 : 0,
		before.eventDueAbsolute,
		after.eventDueAbsolute,
		before.eventDueReadable ? before.eventDueAbsolute - before.environmentNow : 0.0,
		after.eventDueReadable ? after.eventDueAbsolute - after.environmentNow : 0.0,
		reinterpret_cast<void*>(before.objectA),
		reinterpret_cast<void*>(before.objectB),
		reinterpret_cast<void*>(after.objectA),
		reinterpret_cast<void*>(after.objectB),
		before.flags,
		after.flags,
		before.collType,
		after.collType,
		before.synapseSort,
		after.synapseSort,
		before.mindistFunction,
		after.mindistFunction,
		before.recalcResult,
		after.recalcResult,
		before.mindistStatus,
		after.mindistStatus,
		before.collDistSelector,
		after.collDistSelector,
		before.eventIndex,
		after.eventIndex,
		before.length,
		after.length,
		before.contactPlane[0],
		before.contactPlane[1],
		before.contactPlane[2],
		after.contactPlane[0],
		after.contactPlane[1],
		after.contactPlane[2],
		objectContextA,
		objectContextB);
	VPhysicsStaticBVHProbeLog(buffer);
}

static bool VPhysicsTryReviveBeforeCollisionRecheck(const char* phase, __int64 ovElement)
{
	if (!IsVPhysicsReviveOnCollisionEnablePatchEnabled() || !ovElement || !VPhysicsCoreReviveSimulationOriginal)
		return false;
	if (!IsReadableRange(reinterpret_cast<void*>(ovElement + 0x10), sizeof(uintptr_t)))
		return false;

	const __int64 object = *reinterpret_cast<__int64*>(ovElement + 0x10);
	if (!object || !VPhysicsLooksLikeSurfaceMissObject(object))
		return false;

	const unsigned int moveFlags = VPhysicsObjectDword(object, 0xE0);
	if ((moveFlags & 0xff) != 8)
		return false;

	const __int64 core = static_cast<__int64>(VPhysicsObjectQword(object, 0x1B0));
	if (!core || !IsReadableRange(reinterpret_cast<void*>(core), 0x78))
		return false;

	VPhysicsLogMovementObject(phase ? phase : "reviveOnCollision.before", core, object);
	const char* previousPhase = s_VPhysicsActiveMovementPhase;
	s_VPhysicsActiveMovementPhase = "reviveOnCollision";
	VPhysicsCoreReviveSimulationOriginal(core);
	s_VPhysicsActiveMovementPhase = previousPhase;
	VPhysicsLogMovementObject("reviveOnCollision.after", core, object);
	return true;
}

static void VPhysicsLogCollisionApiCaller(const char* apiName, __int64 ovElement, void* retAddress)
{
	if (!ovElement || !IsReadableRange(reinterpret_cast<void*>(ovElement + 0x10), sizeof(uintptr_t)))
		return;

	const __int64 object = *reinterpret_cast<__int64*>(ovElement + 0x10);
	if (!object || !VPhysicsLooksLikeSurfaceMissObject(object))
		return;

	const unsigned int moveFlags = VPhysicsObjectDword(object, 0xE0);
	const unsigned int surfaceFlags = VPhysicsObjectSurfaceFlags(object);
	const unsigned int syncStamp = VPhysicsObjectDword(object, 0x100);
	const unsigned int active = VPhysicsObjectByte(object, 0x104);
	const int pairCount = VPhysicsObjectPairCount(object);
	const __int64 core = static_cast<__int64>(VPhysicsObjectQword(object, 0x1B0));
	if ((moveFlags & 0xff) != 8)
		return;

	HMODULE callerModule = nullptr;
	uintptr_t callerRva = 0;
	char callerPath[MAX_PATH] = {};
	if (retAddress
		&& GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCSTR>(retAddress), &callerModule)
		&& callerModule) {
		callerRva = reinterpret_cast<uintptr_t>(retAddress) - reinterpret_cast<uintptr_t>(callerModule);
		GetModuleFileNameA(callerModule, callerPath, sizeof(callerPath));
	}
	char buffer[1536];
	sprintf_s(
		buffer,
		"VPHYSICS: collisionApiCaller api=%s ov=%p object=%p ret=%p module=%p rva=0x%llx path=%s moveFlags=0x%08x surfaceFlags=0x%08x sync=%u pairs=%d key=0x%016llx\n",
		apiName ? apiName : "<unknown>",
		reinterpret_cast<void*>(ovElement),
		reinterpret_cast<void*>(object),
		retAddress,
		callerModule,
		static_cast<unsigned long long>(callerRva),
		callerPath[0] ? callerPath : "<unknown>",
		moveFlags,
		surfaceFlags,
		syncStamp,
		pairCount,
		static_cast<unsigned long long>(VPhysicsMovementLogKey(apiName, core, object, moveFlags, surfaceFlags, syncStamp, active, pairCount)));
	VPhysicsStaticBVHProbeLog(buffer);
}

static bool VPhysicsStaticCandidatesHaveNonSelf(__int64 staticCandidates, __int64 object)
{
	const int candidateCount = VPhysicsPointerVectorCount(staticCandidates);
	if (candidateCount <= 0)
		return false;

	const int limit = candidateCount < 16 ? candidateCount : 16;
	for (int i = 0; i < limit; ++i) {
		uintptr_t candidatePtr = 0;
		if (VPhysicsPointerVectorElement(staticCandidates, i, &candidatePtr)
			&& candidatePtr
			&& static_cast<__int64>(candidatePtr) != object) {
			return true;
		}
	}

	return false;
}

static bool VPhysicsIsSurfaceMissTargetShape(
	unsigned int moveFlags,
	unsigned int surfaceFlags,
	int staticCandidateCount,
	int pairCount,
	bool hasNonSelfCandidate)
{
	return staticCandidateCount > 0
		&& staticCandidateCount <= 4
		&& hasNonSelfCandidate
		&& VPhysicsShouldLogSurfaceMissObject(moveFlags, surfaceFlags, pairCount);
}

static bool VPhysicsHookOne(uintptr_t base, uintptr_t rva, void* detour, void** original, const char* name)
{
	if (!base || !rva || !detour || !original)
		return false;

	void* target = reinterpret_cast<void*>(base + rva);
	if (!IsReadableRange(target, 16))
		return false;

	const MH_STATUS status = MH_CreateHook(target, detour, reinterpret_cast<LPVOID*>(original));
	const MH_STATUS enableStatus = (status == MH_OK || status == MH_ERROR_ALREADY_CREATED)
		? MH_EnableHook(target)
		: status;
	const bool installed = enableStatus == MH_OK || enableStatus == MH_ERROR_ENABLED || enableStatus == MH_ERROR_ALREADY_CREATED;

	char buffer[320];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"VPHYSICS: probe hook name=%s rva=0x%llx status=%d enable=%d installed=%d target=%p original=%p\n",
		name ? name : "<unknown>",
		static_cast<unsigned long long>(rva),
		static_cast<int>(status),
		static_cast<int>(enableStatus),
		installed ? 1 : 0,
		target,
		*original);
	VPhysicsStaticBVHProbeLog(buffer);
	return installed;
}

static bool __fastcall VPhysicsCollisionFilterShouldCollideProbe(__int64 filter, __int64 object, __int64 candidate)
{
	bool result = false;
	if (VPhysicsCollisionFilterShouldCollideOriginal)
		result = VPhysicsCollisionFilterShouldCollideOriginal(filter, object, candidate);

	const DWORD threadId = GetCurrentThreadId();
	const bool activeTarget = s_VPhysicsStaticBVHActiveThreadId == threadId
		&& s_VPhysicsStaticBVHActiveObject == object
		&& s_VPhysicsStaticBVHActiveTarget;
	if (activeTarget && candidate != object) {
		const unsigned int objectMoveFlags = VPhysicsObjectDword(object, 0xE0);
		const unsigned int candidateMoveFlags = VPhysicsObjectDword(candidate, 0xE0);
		const unsigned int objectSurfaceFlags = VPhysicsObjectSurfaceFlags(object);
		const unsigned int candidateSurfaceFlags = VPhysicsObjectSurfaceFlags(candidate);
		char buffer[1408];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"VPHYSICS: staticFilterShouldCollide call=%llu filter=%p object=%p candidate=%p result=%d objFlags=0x%08x candFlags=0x%08x objSurf=0x%08x candSurf=0x%08x objPairs=%d candPairs=%d\n",
			s_VPhysicsStaticBVHActiveCall,
			reinterpret_cast<void*>(filter),
			reinterpret_cast<void*>(object),
			reinterpret_cast<void*>(candidate),
			result ? 1 : 0,
			objectMoveFlags,
			candidateMoveFlags,
			objectSurfaceFlags,
			candidateSurfaceFlags,
			VPhysicsObjectPairCount(object),
			VPhysicsObjectPairCount(candidate));
		VPhysicsStaticBVHProbeLog(buffer);
	}

	return result;
}

static void VPhysicsTryInstallCollisionFilterHook(__int64 rangeData)
{
	if (s_VPhysicsCollisionFilterHookInstalled || !rangeData)
		return;
	if (!IsReadableRange(reinterpret_cast<void*>(rangeData + 0x30), sizeof(uintptr_t)))
		return;

	const uintptr_t filter = *reinterpret_cast<uintptr_t*>(rangeData + 0x30);
	if (!filter || !IsReadableRange(reinterpret_cast<void*>(filter), sizeof(uintptr_t)))
		return;

	const uintptr_t vtable = *reinterpret_cast<uintptr_t*>(filter);
	if (!vtable || !IsReadableRange(reinterpret_cast<void*>(vtable), sizeof(uintptr_t)))
		return;

	const uintptr_t target = *reinterpret_cast<uintptr_t*>(vtable);
	if (!target || !IsReadableRange(reinterpret_cast<void*>(target), 16))
		return;

	const MH_STATUS status = MH_CreateHook(
		reinterpret_cast<void*>(target),
		reinterpret_cast<void*>(&VPhysicsCollisionFilterShouldCollideProbe),
		reinterpret_cast<LPVOID*>(&VPhysicsCollisionFilterShouldCollideOriginal));
	const MH_STATUS enableStatus = (status == MH_OK || status == MH_ERROR_ALREADY_CREATED)
		? MH_EnableHook(reinterpret_cast<void*>(target))
		: status;
	const bool installed = enableStatus == MH_OK || enableStatus == MH_ERROR_ENABLED || enableStatus == MH_ERROR_ALREADY_CREATED;
	if (installed) {
		s_VPhysicsCollisionFilterHookInstalled = true;
		s_VPhysicsCollisionFilterShouldCollideTarget = target;
	}

	char buffer[384];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"VPHYSICS: probe hook name=static-bvh-should-collide target=%p filter=%p status=%d enable=%d installed=%d original=%p\n",
		reinterpret_cast<void*>(target),
		reinterpret_cast<void*>(filter),
		static_cast<int>(status),
		static_cast<int>(enableStatus),
		installed ? 1 : 0,
		reinterpret_cast<void*>(VPhysicsCollisionFilterShouldCollideOriginal));
	VPhysicsStaticBVHProbeLog(buffer);
}

static void VPhysicsLogStaticBVHRejectDetails(
	unsigned long long call,
	__int64 rangeData,
	__int64 object,
	__int64 staticCandidates,
	int outputBefore,
	int outputAfter,
	int appended,
	int startBefore,
	int startAfter)
{
	const int candidateCount = VPhysicsPointerVectorCount(staticCandidates);
	if (candidateCount <= 0)
		return;

	const unsigned int objectMoveFlags = VPhysicsObjectDword(object, 0xE0);
	const unsigned int objectSurfaceFlags = VPhysicsObjectSurfaceFlags(object);
	const uintptr_t objectGroup = VPhysicsObjectQword(object, 0x1B8);
	const int objectPairCount = VPhysicsObjectPairCount(object);
	const bool hasNonSelfCandidate = VPhysicsStaticCandidatesHaveNonSelf(staticCandidates, object);
	const bool targetShape = VPhysicsIsSurfaceMissTargetShape(objectMoveFlags, objectSurfaceFlags, candidateCount, objectPairCount, hasNonSelfCandidate);
	if (!targetShape)
		return;

	const bool objectHas0200 = (objectMoveFlags & 0x200) != 0;
	const bool objectHas0400Without0200 = !objectHas0200 && (objectMoveFlags & 0x400) != 0;
	const unsigned int objectSurfaceMask = objectSurfaceFlags & 0x12;
	const int limit = candidateCount < 8 ? candidateCount : 8;

	for (int i = 0; i < limit; ++i) {
		uintptr_t candidatePtr = 0;
		const bool haveCandidate = VPhysicsPointerVectorElement(staticCandidates, i, &candidatePtr);
		const __int64 candidate = static_cast<__int64>(candidatePtr);
		if (haveCandidate && candidate == object)
			continue;

		const unsigned int candidateMoveFlags = haveCandidate ? VPhysicsObjectDword(candidate, 0xE0) : 0;
		const unsigned int candidateSurfaceFlags = haveCandidate ? VPhysicsObjectSurfaceFlags(candidate) : 0;
		const uintptr_t candidateGroup = haveCandidate ? VPhysicsObjectQword(candidate, 0x1B8) : 0;
		const int candidatePairCount = haveCandidate ? VPhysicsObjectPairCount(candidate) : -1;

		unsigned int rejectMask = 0;
		if (!haveCandidate || !candidate)
			rejectMask |= 0x01;
		if (haveCandidate && objectGroup == candidateGroup)
			rejectMask |= 0x02;
		if (haveCandidate && (objectMoveFlags & 7) == 0 && (candidateMoveFlags & 7) == 0)
			rejectMask |= 0x04;
		if (haveCandidate && objectSurfaceMask && (candidateSurfaceFlags & 0x12) != 0)
			rejectMask |= 0x08;
		if (haveCandidate && objectHas0200 && (candidateMoveFlags & 0x200) != 0)
			rejectMask |= 0x10;

		const bool cheapPass = haveCandidate && rejectMask == 0;
		const bool fastAllow = cheapPass && (
			(objectHas0400Without0200 && (candidateMoveFlags & 0x200) != 0)
			|| (objectHas0200 && (candidateMoveFlags & 0x400) != 0));
		const bool needsCallback = cheapPass && !fastAllow;

		char buffer[768];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"VPHYSICS: staticFilterCandidate call=%llu target=1 idx=%d/%d range=%p object=%p candidate=%p output=%d->%d appended=%d start=%d->%d objFlags=0x%08x candFlags=0x%08x objSurf=0x%08x candSurf=0x%08x objGroup=%p candGroup=%p objPairs=%d candPairs=%d cheapPass=%d fastAllow=%d needsCallback=%d rejectMask=0x%02x\n",
			call,
			i,
			candidateCount,
			reinterpret_cast<void*>(rangeData),
			reinterpret_cast<void*>(object),
			reinterpret_cast<void*>(candidatePtr),
			outputBefore,
			outputAfter,
			appended,
			startBefore,
			startAfter,
			objectMoveFlags,
			candidateMoveFlags,
			objectSurfaceFlags,
			candidateSurfaceFlags,
			reinterpret_cast<void*>(objectGroup),
			reinterpret_cast<void*>(candidateGroup),
			objectPairCount,
			candidatePairCount,
			cheapPass ? 1 : 0,
			fastAllow ? 1 : 0,
			needsCallback ? 1 : 0,
			rejectMask);
		VPhysicsStaticBVHProbeLog(buffer);
	}
}

static void VPhysicsLogStaticBVHSelfOnlyMiss(
	unsigned long long call,
	__int64 rangeData,
	__int64 object,
	__int64 staticCandidates,
	int staticBefore,
	int staticAfter,
	int outputBefore,
	int outputAfter,
	int appended,
	int startBefore,
	int startAfter,
	int pairCountBefore)
{
	if (staticAfter <= 0 || staticAfter > 16 || appended > 0)
		return;
	if (VPhysicsStaticCandidatesHaveNonSelf(staticCandidates, object))
		return;

	const unsigned int moveFlags = VPhysicsObjectDword(object, 0xE0);
	const unsigned int surfaceFlags = VPhysicsObjectSurfaceFlags(object);
	if (!VPhysicsShouldLogSurfaceMissObject(moveFlags, surfaceFlags, pairCountBefore))
		return;

	const unsigned int syncStamp = VPhysicsObjectDword(object, 0x100);
	const unsigned int active = VPhysicsObjectByte(object, 0x104);
	const bool trackedNew = VPhysicsTrackSurfaceMissObject(object);
	const unsigned long long key = VPhysicsSelfOnlyLogKey(
		object,
		moveFlags,
		surfaceFlags,
		syncStamp,
		active,
		staticAfter,
		pairCountBefore,
		startBefore,
		startAfter);
	if (!s_VPhysicsStaticBVHLoggedSelfOnlyKeys.insert(key).second)
		return;

	uintptr_t firstCandidate = 0;
	const bool haveFirstCandidate = VPhysicsPointerVectorElement(staticCandidates, 0, &firstCandidate);
	const int pairCountAfter = VPhysicsObjectPairCount(object);
	char buffer[1408];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"VPHYSICS: staticFilterSelfOnly call=%llu range=%p object=%p trackedNew=%d firstCandidate=%p firstIsSelf=%d static=%d->%d output=%d->%d appended=%d start=%d->%d active=%u moveFlags=0x%08x surfaceFlags=0x%08x sync=%u pairs=%d->%d key=0x%016llx\n",
		call,
		reinterpret_cast<void*>(rangeData),
		reinterpret_cast<void*>(object),
		trackedNew ? 1 : 0,
		reinterpret_cast<void*>(haveFirstCandidate ? firstCandidate : 0),
		haveFirstCandidate && static_cast<__int64>(firstCandidate) == object ? 1 : 0,
		staticBefore,
		staticAfter,
		outputBefore,
		outputAfter,
		appended,
		startBefore,
		startAfter,
		active,
		moveFlags,
		surfaceFlags,
		syncStamp,
		pairCountBefore,
		pairCountAfter,
		key);
	VPhysicsStaticBVHProbeLog(buffer);
}

static __int64 __fastcall VPhysicsExistingPairProbe(__int64* hashState, __int64 candidate, __int64 object)
{
	__int64 result = 0;
	if (VPhysicsExistingPairOriginal)
		result = VPhysicsExistingPairOriginal(hashState, candidate, object);

	const DWORD threadId = GetCurrentThreadId();
	const bool activeFilter = s_VPhysicsStaticBVHActiveThreadId == threadId && s_VPhysicsStaticBVHActiveObject == object;
	if (activeFilter && s_VPhysicsStaticBVHActiveTarget && candidate != object) {
		const unsigned int objectMoveFlags = VPhysicsObjectDword(object, 0xE0);
		const unsigned int candidateMoveFlags = VPhysicsObjectDword(candidate, 0xE0);
		const unsigned int objectSurfaceFlags = VPhysicsObjectSurfaceFlags(object);
		const unsigned int candidateSurfaceFlags = VPhysicsObjectSurfaceFlags(candidate);
		char buffer[1408];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"VPHYSICS: staticFilterExisting call=%llu object=%p candidate=%p result=%p duplicate=%d objFlags=0x%08x candFlags=0x%08x objSurf=0x%08x candSurf=0x%08x objPairs=%d candPairs=%d\n",
			s_VPhysicsStaticBVHActiveCall,
			reinterpret_cast<void*>(object),
			reinterpret_cast<void*>(candidate),
			reinterpret_cast<void*>(result),
			result ? 1 : 0,
			objectMoveFlags,
			candidateMoveFlags,
			objectSurfaceFlags,
			candidateSurfaceFlags,
			VPhysicsObjectPairCount(object),
			VPhysicsObjectPairCount(candidate));
		VPhysicsStaticBVHProbeLog(buffer);
	}

	return result;
}

static void __fastcall VPhysicsObjectPairUpdateProbe(__int64 rangeData, __int64 object, __int64 outputPairs)
{
	const unsigned long long call = ++s_VPhysicsObjectPairUpdateCalls;
	void* const returnAddress = _ReturnAddress();
	const uintptr_t returnAddressValue = reinterpret_cast<uintptr_t>(returnAddress);
	const uintptr_t returnRva = s_R1OVPhysicsBase && returnAddressValue >= s_R1OVPhysicsBase
		? returnAddressValue - s_R1OVPhysicsBase
		: 0;
	const int pairCountBefore = VPhysicsObjectPairCount(object);
	const unsigned int moveFlags = VPhysicsObjectDword(object, 0xE0);
	const unsigned int syncStamp = VPhysicsObjectDword(object, 0x100);
	const unsigned int active = VPhysicsObjectByte(object, 0x104);
	const unsigned int surfaceFlags = VPhysicsObjectSurfaceFlags(object);
	const unsigned long long helperCallsBefore = s_VPhysicsStaticBVHHelperCalls;
	const unsigned long long filterCallsBefore = s_VPhysicsStaticBVHFilterCalls;

	if (VPhysicsObjectPairUpdateOriginal)
		VPhysicsObjectPairUpdateOriginal(rangeData, object, outputPairs);

	const int pairCountAfter = VPhysicsObjectPairCount(object);
	const unsigned int moveFlagsAfter = VPhysicsObjectDword(object, 0xE0);
	const unsigned int syncStampAfter = VPhysicsObjectDword(object, 0x100);
	const unsigned int activeAfter = VPhysicsObjectByte(object, 0x104);
	const unsigned int surfaceFlagsAfter = VPhysicsObjectSurfaceFlags(object);
	const bool interesting = pairCountBefore != pairCountAfter
		&& VPhysicsShouldLogSurfaceMissObject(moveFlags, surfaceFlags, pairCountBefore);
	const bool targetShape = VPhysicsShouldLogSurfaceMissObject(moveFlags, surfaceFlags, pairCountBefore)
		|| VPhysicsShouldLogSurfaceMissObject(moveFlagsAfter, surfaceFlagsAfter, pairCountAfter);
	const bool helperInvokedForTarget = targetShape && s_VPhysicsStaticBVHHelperCalls != helperCallsBefore;
	const bool tracked = VPhysicsIsTrackedSurfaceMissObject(object)
		&& targetShape;
	const unsigned long long trackedKey = VPhysicsTrackedStateLogKey(
		object,
		moveFlags,
		surfaceFlags,
		syncStamp,
		active,
		pairCountBefore,
		pairCountAfter)
		^ ((s_VPhysicsStaticBVHHelperCalls & 0xfffffllu) << 20)
		^ (s_VPhysicsStaticBVHFilterCalls & 0xfffffllu);
	const bool trackedNew = tracked && s_VPhysicsStaticBVHTrackedPairKeys.insert(trackedKey).second;
	if (interesting || trackedNew || helperInvokedForTarget) {
		char buffer[1536];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"VPHYSICS: %s call=%llu phase=%s ret=%p retRva=0x%llx interesting=%d tracked=%d helperInvoked=%d range=%p object=%p output=%p active=%u->%u moveFlags=0x%08x->0x%08x surfaceFlags=0x%08x->0x%08x sync=%u->%u pairs=%d->%d helperCalls=%llu->%llu filterCalls=%llu->%llu key=0x%016llx\n",
			trackedNew && !interesting ? "pairUpdateTracked" : (helperInvokedForTarget && !interesting ? "pairUpdateHelper" : "pairUpdate"),
			call,
			s_VPhysicsActiveMovementPhase ? s_VPhysicsActiveMovementPhase : "<none>",
			returnAddress,
			static_cast<unsigned long long>(returnRva),
			interesting ? 1 : 0,
			tracked ? 1 : 0,
			helperInvokedForTarget ? 1 : 0,
			reinterpret_cast<void*>(rangeData),
			reinterpret_cast<void*>(object),
			reinterpret_cast<void*>(outputPairs),
			active,
			activeAfter,
			moveFlags,
			moveFlagsAfter,
			surfaceFlags,
			surfaceFlagsAfter,
			syncStamp,
			syncStampAfter,
			pairCountBefore,
			pairCountAfter,
			helperCallsBefore,
			s_VPhysicsStaticBVHHelperCalls,
			filterCallsBefore,
			s_VPhysicsStaticBVHFilterCalls,
			trackedKey);
		VPhysicsStaticBVHProbeLog(buffer);
	}
}

static void __fastcall VPhysicsStaticBVHFilterProbe(__int64 rangeData, __int64 object, __int64 staticCandidates, __int64 outputCandidates, int* outputStartIndex)
{
	const unsigned long long call = ++s_VPhysicsStaticBVHFilterCalls;
	const int staticBefore = VPhysicsPointerVectorCount(staticCandidates);
	const int outputBefore = VPhysicsPointerVectorCount(outputCandidates);
	const int startBefore = outputStartIndex && IsReadableRange(outputStartIndex, sizeof(*outputStartIndex)) ? *outputStartIndex : -1;
	const unsigned int moveFlagsBefore = VPhysicsObjectDword(object, 0xE0);
	const unsigned int surfaceFlagsBefore = VPhysicsObjectSurfaceFlags(object);
	const int pairCountBefore = VPhysicsObjectPairCount(object);
	const bool hasNonSelfCandidate = VPhysicsStaticCandidatesHaveNonSelf(staticCandidates, object);
	const bool activeTarget = VPhysicsIsSurfaceMissTargetShape(moveFlagsBefore, surfaceFlagsBefore, staticBefore, pairCountBefore, hasNonSelfCandidate);

	const DWORD previousThreadId = s_VPhysicsStaticBVHActiveThreadId;
	const __int64 previousObject = s_VPhysicsStaticBVHActiveObject;
	const unsigned long long previousCall = s_VPhysicsStaticBVHActiveCall;
	const bool previousTarget = s_VPhysicsStaticBVHActiveTarget;
	VPhysicsTryInstallCollisionFilterHook(rangeData);
	s_VPhysicsStaticBVHActiveThreadId = GetCurrentThreadId();
	s_VPhysicsStaticBVHActiveObject = object;
	s_VPhysicsStaticBVHActiveCall = call;
	s_VPhysicsStaticBVHActiveTarget = activeTarget;
	if (VPhysicsStaticBVHFilterOriginal)
		VPhysicsStaticBVHFilterOriginal(rangeData, object, staticCandidates, outputCandidates, outputStartIndex);
	s_VPhysicsStaticBVHActiveThreadId = previousThreadId;
	s_VPhysicsStaticBVHActiveObject = previousObject;
	s_VPhysicsStaticBVHActiveCall = previousCall;
	s_VPhysicsStaticBVHActiveTarget = previousTarget;

	const int staticAfter = VPhysicsPointerVectorCount(staticCandidates);
	const int outputAfter = VPhysicsPointerVectorCount(outputCandidates);
	const int startAfter = outputStartIndex && IsReadableRange(outputStartIndex, sizeof(*outputStartIndex)) ? *outputStartIndex : -1;
	const int appended = outputBefore >= 0 && outputAfter >= outputBefore ? outputAfter - outputBefore : -1;
	const unsigned int moveFlags = VPhysicsObjectDword(object, 0xE0);
	const unsigned int surfaceFlags = VPhysicsObjectSurfaceFlags(object);
	const bool familiarStaticStartup = moveFlags == 0x108 && (surfaceFlags == 0x1000 || surfaceFlags == 0x1008 || surfaceFlags == 0x10000);
	const bool notable = activeTarget && staticAfter > 0 && appended <= 0 && (staticAfter >= 8 || !familiarStaticStartup);
	if (notable) {
		char buffer[1280];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"VPHYSICS: staticFilter call=%llu notable=1 range=%p object=%p static=%d->%d output=%d->%d appended=%d start=%d->%d moveFlags=0x%08x surfaceFlags=0x%08x\n",
			call,
			reinterpret_cast<void*>(rangeData),
			reinterpret_cast<void*>(object),
			staticBefore,
			staticAfter,
			outputBefore,
			outputAfter,
			appended,
			startBefore,
			startAfter,
			moveFlags,
			surfaceFlags);
		VPhysicsStaticBVHProbeLog(buffer);
	}
	VPhysicsLogStaticBVHSelfOnlyMiss(
		call,
		rangeData,
		object,
		staticCandidates,
		staticBefore,
		staticAfter,
		outputBefore,
		outputAfter,
		appended,
		startBefore,
		startAfter,
		pairCountBefore);
	if (notable)
		VPhysicsLogStaticBVHRejectDetails(call, rangeData, object, staticCandidates, outputBefore, outputAfter, appended, startBefore, startAfter);
}

static int __fastcall VPhysicsStaticBVHHelperProbe(__int64 rangeData, __int64 object, float* center, float radius, __int64 ledges)
{
	const unsigned long long call = ++s_VPhysicsStaticBVHHelperCalls;
	const int before = VPhysicsStaticBVHProbeVectorCount(ledges);
	const unsigned int moveFlags = VPhysicsObjectDword(object, 0xE0);
	const unsigned int surfaceFlags = VPhysicsObjectSurfaceFlags(object);
	const unsigned int syncStamp = VPhysicsObjectDword(object, 0x100);
	const unsigned int active = VPhysicsObjectByte(object, 0x104);
	const int pairCount = VPhysicsObjectPairCount(object);
	const float x = center && IsReadableRange(center, sizeof(float) * 3) ? center[0] : 0.0f;
	const float y = center && IsReadableRange(center, sizeof(float) * 3) ? center[1] : 0.0f;
	const float z = center && IsReadableRange(center, sizeof(float) * 3) ? center[2] : 0.0f;
	const VPhysicsStaticBVHTreeState treeState = VPhysicsReadStaticBVHTreeState(rangeData);

	int result = 0;
	if (VPhysicsStaticBVHHelperOriginal)
		result = VPhysicsStaticBVHHelperOriginal(rangeData, object, center, radius, ledges);

	const int after = VPhysicsStaticBVHProbeVectorCount(ledges);
	const int appended = before >= 0 && after >= before ? after - before : -1;
	const bool cleared = result > 120 || (before >= 0 && after >= 0 && after < before);
	const float radiusSq = radius * radius;
	const float rootDistanceSq = VPhysicsRootDistanceSq(treeState, x, y, z);
	const bool rootHit = treeState.rootReadable && rootDistanceSq < radiusSq;
	const bool tracked = VPhysicsIsTrackedSurfaceMissObject(object)
		&& VPhysicsShouldLogSurfaceMissObject(moveFlags, surfaceFlags, pairCount);
	const bool trackedNew = tracked && s_VPhysicsStaticBVHTrackedHelperKeys.insert(VPhysicsTrackedStateLogKey(
		object,
		moveFlags,
		surfaceFlags,
		syncStamp,
		active,
		before,
		after)).second;
	const bool notable = VPhysicsShouldLogSurfaceMissObject(moveFlags, surfaceFlags, pairCount)
		&& (result <= 0 || appended <= 0 || cleared || radius > 2048.0f || (treeState.rootReadable && !rootHit));
	if (notable || trackedNew) {
		char buffer[2048];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"VPHYSICS: %s call=%llu flavor=%s notable=%d tracked=%d result=%d appended=%d before=%d after=%d cleared=%d valid=%u tree=%p nodes=%p objects=%p bounds=%p counts=%u/%u rootReadable=%d rootHit=%d rootDistSq=%.3f radiusSq=%.3f rootMin=(%.3f %.3f %.3f) rootMax=(%.3f %.3f %.3f) radius=%.3f center=(%.3f %.3f %.3f) range=%p object=%p ledges=%p active=%u moveFlags=0x%08x surfaceFlags=0x%08x sync=%u pairs=%d envReadable=%d treeReadable=%d countsReadable=%d\n",
			trackedNew && !notable ? "staticHelperTracked" : "staticHelper",
			call,
			s_VPhysicsStaticBVHProbeFlavor ? s_VPhysicsStaticBVHProbeFlavor : "<unknown>",
			notable ? 1 : 0,
			tracked ? 1 : 0,
			result,
			appended,
			before,
			after,
			cleared ? 1 : 0,
			treeState.valid,
			reinterpret_cast<void*>(treeState.tree),
			reinterpret_cast<void*>(treeState.nodes),
			reinterpret_cast<void*>(treeState.objects),
			reinterpret_cast<void*>(treeState.bounds),
			treeState.nodeCount,
			treeState.objectCount,
			treeState.rootReadable ? 1 : 0,
			rootHit ? 1 : 0,
			rootDistanceSq,
			radiusSq,
			treeState.minX,
			treeState.minY,
			treeState.minZ,
			treeState.maxX,
			treeState.maxY,
			treeState.maxZ,
			radius,
			x,
			y,
			z,
			reinterpret_cast<void*>(rangeData),
			reinterpret_cast<void*>(object),
			reinterpret_cast<void*>(ledges),
			active,
			moveFlags,
			surfaceFlags,
			syncStamp,
			pairCount,
			treeState.envReadable ? 1 : 0,
			treeState.treeHeaderReadable ? 1 : 0,
			treeState.countsReadable ? 1 : 0);
		VPhysicsStaticBVHProbeLog(buffer);
	}

	return result;
}

static void VPhysicsLogStaticBVHRebuildState(
	unsigned long long call,
	const char* phase,
	__int64 callback,
	__int64 environment,
	__int64 result,
	const VPhysicsStaticBVHTreeState& state)
{
	char buffer[960];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"VPHYSICS: staticRebuild call=%llu phase=%s this=%p env=%p result=%p valid=%u arrays=%d tree=%p nodes=%p objects=%p bounds=%p counts=%u/%u rootMin=(%.3f %.3f %.3f) rootMax=(%.3f %.3f %.3f) envReadable=%d treeReadable=%d countsReadable=%d rootReadable=%d\n",
		call,
		phase ? phase : "<unknown>",
		reinterpret_cast<void*>(callback),
		reinterpret_cast<void*>(environment),
		reinterpret_cast<void*>(result),
		state.valid,
		(state.nodes || state.objects || state.bounds) ? 1 : 0,
		reinterpret_cast<void*>(state.tree),
		reinterpret_cast<void*>(state.nodes),
		reinterpret_cast<void*>(state.objects),
		reinterpret_cast<void*>(state.bounds),
		state.nodeCount,
		state.objectCount,
		state.minX,
		state.minY,
		state.minZ,
		state.maxX,
		state.maxY,
		state.maxZ,
		state.envReadable ? 1 : 0,
		state.treeHeaderReadable ? 1 : 0,
		state.countsReadable ? 1 : 0,
		state.rootReadable ? 1 : 0);
	VPhysicsStaticBVHProbeLog(buffer);
}

static __int64 __fastcall VPhysicsStaticBVHRebuildCallbackProbe(__int64 callback)
{
	const unsigned long long call = ++s_VPhysicsStaticBVHRebuildCalls;
	uintptr_t environment = 0;
	if (callback && IsReadableRange(reinterpret_cast<void*>(callback + sizeof(uintptr_t)), sizeof(uintptr_t)))
		environment = *reinterpret_cast<uintptr_t*>(callback + sizeof(uintptr_t));

	const VPhysicsStaticBVHTreeState before = VPhysicsReadStaticBVHTreeState(static_cast<__int64>(environment));
	VPhysicsLogStaticBVHRebuildState(call, "before", callback, static_cast<__int64>(environment), 0, before);

	__int64 result = 0;
	if (VPhysicsStaticBVHRebuildCallbackOriginal)
		result = VPhysicsStaticBVHRebuildCallbackOriginal(callback);

	const VPhysicsStaticBVHTreeState after = VPhysicsReadStaticBVHTreeState(static_cast<__int64>(environment));
	VPhysicsLogStaticBVHRebuildState(call, "after", callback, static_cast<__int64>(environment), result, after);
	return result;
}

static __int16 __fastcall VPhysicsSimUnitReviveProbe(__int64 simUnit, __int64 environment)
{
	__int16 result = 0;
	const char* previousPhase = s_VPhysicsActiveMovementPhase;
	s_VPhysicsActiveMovementPhase = "simUnitRevive";
	if (VPhysicsSimUnitReviveOriginal)
		result = VPhysicsSimUnitReviveOriginal(simUnit, environment);
	s_VPhysicsActiveMovementPhase = previousPhase;

	if (simUnit && IsReadableRange(reinterpret_cast<void*>(simUnit + 0x18), sizeof(uintptr_t) * 2)) {
		const int coreCount = VPhysicsPointerVectorCount(simUnit + 0x18);
		if (coreCount > 0 && coreCount <= 64) {
			for (int i = 0; i < coreCount; ++i) {
				uintptr_t core = 0;
				if (VPhysicsPointerVectorElement(simUnit + 0x18, i, &core))
					VPhysicsLogCoreMovementObjects("simUnitRevive.after", static_cast<__int64>(core));
			}
		}
	}

	return result;
}

static __int64 __fastcall VPhysicsCoreStopMovementProbe(__int64 core)
{
	VPhysicsLogCoreMovementObjects("coreStop.before", core);

	__int64 result = 0;
	const char* previousPhase = s_VPhysicsActiveMovementPhase;
	s_VPhysicsActiveMovementPhase = "coreStop";
	if (VPhysicsCoreStopMovementOriginal)
		result = VPhysicsCoreStopMovementOriginal(core);
	s_VPhysicsActiveMovementPhase = previousPhase;

	VPhysicsLogCoreMovementObjects("coreStop.after", core);
	return result;
}

static __int64 __fastcall VPhysicsCoreInitSimulationProbe(__int64 core)
{
	VPhysicsLogCoreMovementObjects("coreInit.before", core);

	__int64 result = 0;
	const char* previousPhase = s_VPhysicsActiveMovementPhase;
	s_VPhysicsActiveMovementPhase = "coreInit";
	if (VPhysicsCoreInitSimulationOriginal)
		result = VPhysicsCoreInitSimulationOriginal(core);
	s_VPhysicsActiveMovementPhase = previousPhase;

	VPhysicsLogCoreMovementObjects("coreInit.after", core);
	return result;
}

static __int64 __fastcall VPhysicsCoreReviveSimulationProbe(__int64 core)
{
	VPhysicsLogCoreMovementObjects("coreRevive.before", core);

	__int64 result = 0;
	const char* previousPhase = s_VPhysicsActiveMovementPhase;
	s_VPhysicsActiveMovementPhase = "coreRevive";
	if (VPhysicsCoreReviveSimulationOriginal)
		result = VPhysicsCoreReviveSimulationOriginal(core);
	s_VPhysicsActiveMovementPhase = previousPhase;

	VPhysicsLogCoreMovementObjects("coreRevive.after", core);
	return result;
}

static __int16 __fastcall VPhysicsObjectRefreshMindistsProbe(__int64 object)
{
	VPhysicsLogMovementObject("refreshMindists.before", object ? static_cast<__int64>(VPhysicsObjectQword(object, 0x1B0)) : 0, object);

	__int16 result = 0;
	const char* previousPhase = s_VPhysicsActiveMovementPhase;
	s_VPhysicsActiveMovementPhase = "refreshMindists";
	if (VPhysicsObjectRefreshMindistsOriginal)
		result = VPhysicsObjectRefreshMindistsOriginal(object);
	s_VPhysicsActiveMovementPhase = previousPhase;

	VPhysicsLogMovementObject("refreshMindists.after", object ? static_cast<__int64>(VPhysicsObjectQword(object, 0x1B0)) : 0, object);
	return result;
}

static __int64 __fastcall VPhysicsCollisionEnableProbe(__int64 ovElement, char enable)
{
	VPhysicsLogCollisionApiCaller(enable ? "EnableCollisions(true)" : "EnableCollisions(false)", ovElement, _ReturnAddress());

	if (enable)
		VPhysicsTryReviveBeforeCollisionRecheck("reviveOnCollision.enable", ovElement);

	return VPhysicsCollisionEnableOriginal
		? VPhysicsCollisionEnableOriginal(ovElement, enable)
		: 0;
}

static __int64 __fastcall VPhysicsCollisionRecheckProbe(__int64 ovElement)
{
	VPhysicsLogCollisionApiCaller("RecheckCollisionFilter", ovElement, _ReturnAddress());
	VPhysicsTryReviveBeforeCollisionRecheck("reviveOnCollision.recheck", ovElement);

	return VPhysicsCollisionRecheckOriginal
		? VPhysicsCollisionRecheckOriginal(ovElement)
		: 0;
}

void InstallVPhysicsStaticBVHProbe(uintptr_t vphysicsBase)
{
	if (s_VPhysicsStaticBVHProbeInstalled || !IsVPhysicsStaticBVHProbeEnabled())
		return;

	if (!vphysicsBase) {
		HMODULE module = GetModuleHandleA("vphysics.dll");
		vphysicsBase = reinterpret_cast<uintptr_t>(module);
	}
	if (!vphysicsBase)
		return;
	s_R1OVPhysicsBase = vphysicsBase;

	struct Candidate {
		const char* flavor;
		uintptr_t helperRva;
		uintptr_t objectPairUpdateRva;
		uintptr_t staticFilterRva;
		uintptr_t existingPairRva;
		uintptr_t rebuildCallbackRva;
		uintptr_t simUnitReviveRva;
		uintptr_t coreStopMovementRva;
		uintptr_t coreInitSimulationRva;
		uintptr_t coreReviveSimulationRva;
		uintptr_t objectRefreshMindistsRva;
		uintptr_t collisionEnableRva;
		uintptr_t collisionRecheckRva;
		uintptr_t newPairInitRva;
		uintptr_t updateExactMindistEventsRva;
		uintptr_t updateExactMindistDeferredRva;
		uintptr_t deferredFlushRva;
		uintptr_t doImpactRva;
		uintptr_t exactToHullRva;
		uintptr_t mindistEventRva;
		uintptr_t emptyStackIndexInitRva;
		unsigned char expectedEmptyStackIndexInit[10];
		unsigned char replacementEmptyStackIndexInit[10];
		uintptr_t branchRva;
		unsigned char expectedBranch[2];
		unsigned char replacementBranch[2];
		uintptr_t includeNotSimBranchRva;
		unsigned char expectedIncludeNotSimBranch[2];
		unsigned char replacementIncludeNotSimBranch[2];
		uintptr_t mindistEnvironmentOffset;
		uintptr_t mindistFlagsOffset;
		uintptr_t mindistObjectBaseOffset;
		uintptr_t mindistObjectStride;
		uintptr_t environmentManagerOffset;
		uintptr_t objectCoreOffset;
		uintptr_t managerStackBaseOffset;
		uintptr_t managerStackIndexOffset;
		uintptr_t managerCriticalSectionOffset;
		bool managerCriticalSectionIsPointer;
		unsigned int exactMindistDeferBit;
		bool pointerVector;
	};
	const Candidate candidates[] = {
		{
			"tfo-r1o",
			0x0BD770, 0x0CDA40, 0x0CD4A0, 0x0D0230, 0x01E670,
			0x0A29E0, 0x0A5D20, 0x0A5E10, 0x0BF040, 0x09BFF0,
			0x026230, 0x026490, 0x0CD710, 0x0CE300, 0x0CF450,
			0x0CFEA0, 0x0C3B80, 0x0CF0F0, 0x0CE980,
			0x0CF976,
			{ 0xC7, 0x86, 0x00, 0x01, 0x14, 0x00, 0xFF, 0xFF, 0xFF, 0xFF },
			{ 0xC7, 0x86, 0x00, 0x01, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00 },
			0x0BD8A4, { 0x7E, 0x04 }, { 0xEB, 0x04 },
			0x0CDC6A, { 0x74, 0x54 }, { 0x90, 0x90 },
			0x3E, 0x4E, 0x72, 0x34, 0x20, 0x1B0,
			0x140080, 0x140100, 0x140108, true, 8, true
		},
		{
			"r1",
			0x0EF790, 0, 0, 0, 0,
			0, 0, 0, 0, 0,
			0, 0, 0, 0x100DB0, 0,
			0x1032C0, 0, 0, 0,
			0x0FFAC1,
			{ 0xC7, 0x86, 0x20, 0x01, 0x14, 0x00, 0xFF, 0xFF, 0xFF, 0xFF },
			{ 0xC7, 0x86, 0x20, 0x01, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00 },
			0x0EF904, { 0x7E, 0x24 }, { 0xEB, 0x24 },
			0, { 0, 0 }, { 0, 0 },
			0x7E, 0x86, 0xAA, 0x34, 0x20, 0x308,
			0x1400A0, 0x140120, 0x8, false, 8, false
		},
	};

	for (const Candidate& candidate : candidates) {
		void* const helper = reinterpret_cast<void*>(vphysicsBase + candidate.helperRva);
		void* const branch = reinterpret_cast<void*>(vphysicsBase + candidate.branchRva);
		if (!IsReadableRange(helper, 16) || !IsReadableRange(branch, sizeof(candidate.expectedBranch)))
			continue;
		if (memcmp(branch, candidate.expectedBranch, sizeof(candidate.expectedBranch)) != 0
			&& memcmp(branch, candidate.replacementBranch, sizeof(candidate.replacementBranch)) != 0)
			continue;

		s_VPhysicsStaticBVHProbeFlavor = candidate.flavor;
		s_VPhysicsStaticBVHProbePointerVector = candidate.pointerVector;
		s_VPhysicsMindistEnvironmentOffset = candidate.mindistEnvironmentOffset;
		s_VPhysicsMindistFlagsOffset = candidate.mindistFlagsOffset;
		s_VPhysicsMindistObjectBaseOffset = candidate.mindistObjectBaseOffset;
		s_VPhysicsMindistObjectStride = candidate.mindistObjectStride;
		s_VPhysicsEnvironmentManagerOffset = candidate.environmentManagerOffset;
		s_VPhysicsObjectCoreOffset = candidate.objectCoreOffset;
		s_VPhysicsManagerStackBaseOffset = candidate.managerStackBaseOffset;
		s_VPhysicsManagerStackIndexOffset = candidate.managerStackIndexOffset;
		s_VPhysicsManagerCriticalSectionOffset = candidate.managerCriticalSectionOffset;
		s_VPhysicsManagerCriticalSectionIsPointer = candidate.managerCriticalSectionIsPointer;
		s_VPhysicsExactMindistDeferBit = candidate.exactMindistDeferBit;

		// The Respawn IVP manager uses -1 for an empty phase stack, but several
		// inline readers (and all five CFEA0/1032C0 flush-bit clears) index the
		// array without checking for -1. That aliases stack[-1] with the high
		// dword of the adjacent heap pointer, making behavior depend on ASLR.
		//
		// Reserve zeroed slot 0 as the empty sentinel instead. Every push in
		// these builds pre-increments the index and every matching pop
		// decrements it, so active phase entries move to slots 1..31 while all
		// empty-stack reads and writes remain inside the phase array.
		const bool patchedEmptyStackSentinel =
			IsVPhysicsGuardNegativeStackIndexEnabled()
			&& candidate.emptyStackIndexInitRva
			&& PatchModuleBytesIfMatch(
				vphysicsBase,
				candidate.emptyStackIndexInitRva,
				candidate.expectedEmptyStackIndexInit,
				candidate.replacementEmptyStackIndexInit,
				sizeof(candidate.expectedEmptyStackIndexInit),
				"vphysics empty phase-stack sentinel");

		const bool diagnosticsEnabled = IsVPhysicsDiagnosticsEnabled();
		const bool helperInstalled = diagnosticsEnabled && VPhysicsHookOne(
			vphysicsBase,
			candidate.helperRva,
			reinterpret_cast<void*>(&VPhysicsStaticBVHHelperProbe),
			reinterpret_cast<void**>(&VPhysicsStaticBVHHelperOriginal),
			"static-bvh-helper");
		bool objectPairUpdateInstalled = false;
		bool staticFilterInstalled = false;
		bool existingPairInstalled = false;
		bool rebuildCallbackInstalled = false;
		bool simUnitReviveInstalled = false;
		bool coreStopMovementInstalled = false;
		bool coreInitSimulationInstalled = false;
		bool coreReviveSimulationInstalled = false;
		bool objectRefreshMindistsInstalled = false;
		bool collisionEnableInstalled = false;
		bool collisionRecheckInstalled = false;
		bool newPairInitInstalled = false;
		bool updateExactMindistEventsInstalled = false;
		bool updateExactMindistDeferredInstalled = false;
		bool deferredFlushInstalled = false;
		bool doImpactInstalled = false;
		bool exactToHullInstalled = false;
		bool mindistEventInstalled = false;
		if (diagnosticsEnabled && candidate.objectPairUpdateRva) {
			objectPairUpdateInstalled = VPhysicsHookOne(
				vphysicsBase,
				candidate.objectPairUpdateRva,
				reinterpret_cast<void*>(&VPhysicsObjectPairUpdateProbe),
				reinterpret_cast<void**>(&VPhysicsObjectPairUpdateOriginal),
				"object-pair-update");
		}
		if (diagnosticsEnabled && candidate.staticFilterRva) {
			staticFilterInstalled = VPhysicsHookOne(
				vphysicsBase,
				candidate.staticFilterRva,
				reinterpret_cast<void*>(&VPhysicsStaticBVHFilterProbe),
				reinterpret_cast<void**>(&VPhysicsStaticBVHFilterOriginal),
				"static-bvh-filter");
		}
		if (diagnosticsEnabled && candidate.existingPairRva) {
			existingPairInstalled = VPhysicsHookOne(
				vphysicsBase,
				candidate.existingPairRva,
				reinterpret_cast<void*>(&VPhysicsExistingPairProbe),
				reinterpret_cast<void**>(&VPhysicsExistingPairOriginal),
				"static-bvh-existing-pair");
		}
		if (diagnosticsEnabled && candidate.rebuildCallbackRva) {
			rebuildCallbackInstalled = VPhysicsHookOne(
				vphysicsBase,
				candidate.rebuildCallbackRva,
				reinterpret_cast<void*>(&VPhysicsStaticBVHRebuildCallbackProbe),
				reinterpret_cast<void**>(&VPhysicsStaticBVHRebuildCallbackOriginal),
				"static-bvh-rebuild");
		}
		if (diagnosticsEnabled && candidate.simUnitReviveRva) {
			simUnitReviveInstalled = VPhysicsHookOne(
				vphysicsBase,
				candidate.simUnitReviveRva,
				reinterpret_cast<void*>(&VPhysicsSimUnitReviveProbe),
				reinterpret_cast<void**>(&VPhysicsSimUnitReviveOriginal),
				"sim-unit-revive");
		}
		if (diagnosticsEnabled && candidate.coreStopMovementRva) {
			coreStopMovementInstalled = VPhysicsHookOne(
				vphysicsBase,
				candidate.coreStopMovementRva,
				reinterpret_cast<void*>(&VPhysicsCoreStopMovementProbe),
				reinterpret_cast<void**>(&VPhysicsCoreStopMovementOriginal),
				"core-stop-movement");
		}
		if (diagnosticsEnabled && candidate.coreInitSimulationRva) {
			coreInitSimulationInstalled = VPhysicsHookOne(
				vphysicsBase,
				candidate.coreInitSimulationRva,
				reinterpret_cast<void*>(&VPhysicsCoreInitSimulationProbe),
				reinterpret_cast<void**>(&VPhysicsCoreInitSimulationOriginal),
				"core-init-simulation");
		}
		if (diagnosticsEnabled && candidate.coreReviveSimulationRva) {
			coreReviveSimulationInstalled = VPhysicsHookOne(
				vphysicsBase,
				candidate.coreReviveSimulationRva,
				reinterpret_cast<void*>(&VPhysicsCoreReviveSimulationProbe),
				reinterpret_cast<void**>(&VPhysicsCoreReviveSimulationOriginal),
				"core-revive-simulation");
		}
		if (diagnosticsEnabled && candidate.objectRefreshMindistsRva) {
			objectRefreshMindistsInstalled = VPhysicsHookOne(
				vphysicsBase,
				candidate.objectRefreshMindistsRva,
				reinterpret_cast<void*>(&VPhysicsObjectRefreshMindistsProbe),
				reinterpret_cast<void**>(&VPhysicsObjectRefreshMindistsOriginal),
				"object-refresh-mindists");
		}
		if (diagnosticsEnabled && candidate.collisionEnableRva) {
			collisionEnableInstalled = VPhysicsHookOne(
				vphysicsBase,
				candidate.collisionEnableRva,
				reinterpret_cast<void*>(&VPhysicsCollisionEnableProbe),
				reinterpret_cast<void**>(&VPhysicsCollisionEnableOriginal),
				"collision-enable");
		}
		if (diagnosticsEnabled && candidate.collisionRecheckRva) {
			collisionRecheckInstalled = VPhysicsHookOne(
				vphysicsBase,
				candidate.collisionRecheckRva,
				reinterpret_cast<void*>(&VPhysicsCollisionRecheckProbe),
				reinterpret_cast<void**>(&VPhysicsCollisionRecheckOriginal),
				"collision-recheck");
		}
		if (diagnosticsEnabled && candidate.newPairInitRva) {
			newPairInitInstalled = VPhysicsHookOne(
				vphysicsBase,
				candidate.newPairInitRva,
				reinterpret_cast<void*>(&VPhysicsNewPairInitProbe),
				reinterpret_cast<void**>(&VPhysicsNewPairInitOriginal),
				"new-pair-init");
		}
		// The constructor sentinel patch fixes the invariant for every phase-stack
		// consumer. Keep the CE300 detour strictly as diagnostic instrumentation;
		// the old production guard only protected this one reader and sampled the
		// stack index before taking the manager lock.
		if (diagnosticsEnabled && candidate.updateExactMindistEventsRva) {
			updateExactMindistEventsInstalled = VPhysicsHookOne(
				vphysicsBase,
				candidate.updateExactMindistEventsRva,
				reinterpret_cast<void*>(&VPhysicsUpdateExactMindistEventsProbe),
				reinterpret_cast<void**>(&VPhysicsUpdateExactMindistEventsOriginal),
				"update-exact-mindist-events");
		}
		if (diagnosticsEnabled && candidate.updateExactMindistDeferredRva) {
			updateExactMindistDeferredInstalled = VPhysicsHookOne(
				vphysicsBase,
				candidate.updateExactMindistDeferredRva,
				reinterpret_cast<void*>(&VPhysicsUpdateExactMindistDeferredProbe),
				reinterpret_cast<void**>(&VPhysicsUpdateExactMindistDeferredOriginal),
				"update-exact-mindist-deferred");
		}
		if (diagnosticsEnabled && candidate.deferredFlushRva) {
			deferredFlushInstalled = VPhysicsHookOne(
				vphysicsBase,
				candidate.deferredFlushRva,
				reinterpret_cast<void*>(&VPhysicsDeferredFlushProbe),
				reinterpret_cast<void**>(&VPhysicsDeferredFlushOriginal),
				"deferred-flush");
		}
		if (diagnosticsEnabled && candidate.doImpactRva) {
			doImpactInstalled = VPhysicsHookOne(
				vphysicsBase,
				candidate.doImpactRva,
				reinterpret_cast<void*>(&VPhysicsDoImpactProbe),
				reinterpret_cast<void**>(&VPhysicsDoImpactOriginal),
				"event-callback");
		}
		if (diagnosticsEnabled && candidate.exactToHullRva) {
			exactToHullInstalled = VPhysicsHookOne(
				vphysicsBase,
				candidate.exactToHullRva,
				reinterpret_cast<void*>(&VPhysicsExactToHullProbe),
				reinterpret_cast<void**>(&VPhysicsExactToHullOriginal),
				"exact-to-hull");
		}
		if (diagnosticsEnabled && candidate.mindistEventRva) {
			mindistEventInstalled = VPhysicsHookOne(
				vphysicsBase,
				candidate.mindistEventRva,
				reinterpret_cast<void*>(&VPhysicsMindistEventProbe),
				reinterpret_cast<void**>(&VPhysicsMindistEventOriginal),
				"mindist-event");
		}

		const bool patched = IsVPhysicsStaticBVHClearPatchEnabled()
			&& PatchModuleBytesIfMatch(
				vphysicsBase,
				candidate.branchRva,
				candidate.expectedBranch,
				candidate.replacementBranch,
				sizeof(candidate.expectedBranch),
				"vphysics static BVH overflow clear probe skip");
		const bool patchedIncludeNotSim = IsVPhysicsStaticBVHIncludeNotSimPatchEnabled()
			&& candidate.includeNotSimBranchRva
			&& PatchModuleBytesIfMatch(
				vphysicsBase,
				candidate.includeNotSimBranchRva,
				candidate.expectedIncludeNotSimBranch,
				candidate.replacementIncludeNotSimBranch,
				sizeof(candidate.expectedIncludeNotSimBranch),
				"vphysics static BVH include IVP_MT_NOT_SIM objects");
		s_VPhysicsStaticBVHProbeInstalled =
			helperInstalled
			|| objectPairUpdateInstalled
			|| staticFilterInstalled
			|| existingPairInstalled
			|| rebuildCallbackInstalled
			|| simUnitReviveInstalled
			|| coreStopMovementInstalled
			|| coreInitSimulationInstalled
			|| coreReviveSimulationInstalled
			|| objectRefreshMindistsInstalled
			|| collisionEnableInstalled
			|| collisionRecheckInstalled
			|| newPairInitInstalled
			|| updateExactMindistEventsInstalled
			|| updateExactMindistDeferredInstalled
			|| deferredFlushInstalled
			|| doImpactInstalled
			|| exactToHullInstalled
			|| mindistEventInstalled
			|| patchedEmptyStackSentinel
			|| patched
			|| patchedIncludeNotSim;

		char buffer[896];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"VPHYSICS: probe installed flavor=%s helper=%d pairUpdate=%d staticFilter=%d existingPair=%d rebuild=%d simUnitRevive=%d coreStop=%d coreInit=%d coreRevive=%d refreshMindists=%d collisionEnable=%d collisionRecheck=%d newPairInit=%d updateExactMindist=%d updateExactDeferred=%d deferredFlush=%d eventCallback=%d exactToHull=%d mindistEvent=%d reviveOnCollision=%d negStackGuard=%d emptyStackSentinel=%d guardPageTrace=%d patchedClear=%d patchedIncludeNotSim=%d helperTarget=%p branch=%p includeNotSimBranch=%p\n",
			candidate.flavor,
			helperInstalled ? 1 : 0,
			objectPairUpdateInstalled ? 1 : 0,
			staticFilterInstalled ? 1 : 0,
			existingPairInstalled ? 1 : 0,
			rebuildCallbackInstalled ? 1 : 0,
			simUnitReviveInstalled ? 1 : 0,
			coreStopMovementInstalled ? 1 : 0,
			coreInitSimulationInstalled ? 1 : 0,
			coreReviveSimulationInstalled ? 1 : 0,
			objectRefreshMindistsInstalled ? 1 : 0,
			collisionEnableInstalled ? 1 : 0,
			collisionRecheckInstalled ? 1 : 0,
			newPairInitInstalled ? 1 : 0,
			updateExactMindistEventsInstalled ? 1 : 0,
			updateExactMindistDeferredInstalled ? 1 : 0,
			deferredFlushInstalled ? 1 : 0,
			doImpactInstalled ? 1 : 0,
			exactToHullInstalled ? 1 : 0,
			mindistEventInstalled ? 1 : 0,
			IsVPhysicsReviveOnCollisionEnablePatchEnabled() ? 1 : 0,
			IsVPhysicsGuardNegativeStackIndexEnabled() ? 1 : 0,
			patchedEmptyStackSentinel ? 1 : 0,
			IsVPhysicsGuardPageTraceEnabled() ? 1 : 0,
			patched ? 1 : 0,
			patchedIncludeNotSim ? 1 : 0,
			helper,
			branch,
			candidate.includeNotSimBranchRva ? reinterpret_cast<void*>(vphysicsBase + candidate.includeNotSimBranchRva) : nullptr);
		VPhysicsStaticBVHProbeLog(buffer);
		return;
	}

	VPhysicsStaticBVHProbeLog("VPHYSICS: requested but no known R1/TFO helper bytes matched\n");
}

static void PatchR1ORenderOnlyDedicatedHelpers(uintptr_t engineBase)
{
	const unsigned char prolog[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x30 };
	const unsigned char returnZero[] = { 0x33, 0xC0, 0xC3, 0x90, 0x90, 0x90 };

	PatchR1OBytesIfMatch(
		engineBase,
		0x0DDE50,
		prolog,
		returnZero,
		sizeof(returnZero),
		"fake-dedi FullFrameFB render-target helper");
	PatchR1OBytesIfMatch(
		engineBase,
		0x0DDF00,
		prolog,
		returnZero,
		sizeof(returnZero),
		"fake-dedi FullFrameDepth render-target helper");
}

static void PatchR1OStaticPropRenderInventory(uintptr_t engineBase)
{
	// R1O CStaticProp::Init performs an additional studio render-data walk after
	// the static prop itself is fully initialized. The real R1 dedicated build
	// returns at this point instead. Server VPKs intentionally omit the render
	// data queried by the R1O model-loader vfunc, which makes that extra walk
	// return null and dereference it at engine_r1o+0x1BC296.
	//
	// Jump from the beginning of the R1O-only walk to the existing success
	// epilogue. This preserves every preceding static-prop field, bounds, and
	// model initialization operation and changes only fake-dedicated behavior.
	const unsigned char expected[] = {
		0x48, 0x8B, 0x0D, 0x12, 0xF4, 0x13, 0x02 // mov rcx, [engine_r1o+0x22FB690]
	};
	const unsigned char skipRenderInventory[] = {
		0xE9, 0xAE, 0x00, 0x00, 0x00, // jmp engine_r1o+0x1BC32A
		0x90, 0x90
	};

	static_assert(sizeof(expected) == sizeof(skipRenderInventory), "R1O static-prop render-inventory patch size mismatch");
	PatchR1OBytesIfMatch(
		engineBase,
		0x1BC277,
		expected,
		skipRenderInventory,
		sizeof(skipRenderInventory),
		"fake-dedi skip CStaticProp render-only studio inventory");
}

static void PatchR1OConnectPlatformValidation(uintptr_t engineBase)
{
	const unsigned char expected[] = {
		0x45, 0x85, 0xFF,                         // test r15d, r15d
		0x0F, 0x84, 0x29, 0x01, 0x00, 0x00,       // jz invalid platform
		0x41, 0x83, 0xFF, 0x03,                   // cmp r15d, 3
		0x0F, 0x8F, 0x1F, 0x01, 0x00, 0x00        // jg invalid platform
	};
	const unsigned char forcePcPlatform[] = {
		0x41, 0xBF, 0x01, 0x00, 0x00, 0x00,       // mov r15d, 1
		0x90, 0x90, 0x90, 0x90, 0x90,
		0x90, 0x90, 0x90, 0x90, 0x90,
		0x90, 0x90, 0x90
	};

	PatchR1OBytesIfMatch(
		engineBase,
		0x136552,
		expected,
		forcePcPlatform,
		sizeof(forcePcPlatform),
		"fake-dedi C2S_CONNECT cross-play platform normalization");
}

static void PatchR1OConnectUidValidation(uintptr_t engineBase)
{
	const unsigned char expected[] = { 0x74 };
	const unsigned char forceAccept[] = { 0xEB };

	PatchR1OBytesIfMatch(
		engineBase,
		0x1347F6,
		expected,
		forceAccept,
		sizeof(forceAccept),
		"fake-dedi C2S_CONNECT UID validation bypass");
}

static void PatchR1ONetChanPacketEndHandlerGuard(uintptr_t engineBase)
{
	const unsigned char expected[] = {
		0x48, 0x8B, 0x84, 0x24, 0x20, 0x01, 0x00, 0x00,       // mov rax, [rsp+120h]
		0x48, 0x8B, 0x80, 0xD0, 0x3E, 0x00, 0x00,             // mov rax, [rax+3ED0h]
		0x48, 0x8B, 0x8C, 0x24, 0x20, 0x01, 0x00, 0x00,       // mov rcx, [rsp+120h]
		0x48, 0x8B, 0x89, 0xD0, 0x3E, 0x00, 0x00,             // mov rcx, [rcx+3ED0h]
		0x48, 0x8B, 0x00,                                     // mov rax, [rax]
		0xFF, 0x50, 0x28                                      // call qword ptr [rax+28h]
	};
	const unsigned char guarded[] = {
		0x48, 0x8B, 0x8C, 0x24, 0x20, 0x01, 0x00, 0x00,       // mov rcx, [rsp+120h]
		0x48, 0x8B, 0x89, 0xD0, 0x3E, 0x00, 0x00,             // mov rcx, [rcx+3ED0h]
		0x48, 0x85, 0xC9,                                     // test rcx, rcx
		0x74, 0x10,                                           // jz after PacketEnd call
		0x48, 0x8B, 0x01,                                     // mov rax, [rcx]
		0xFF, 0x50, 0x28,                                     // call qword ptr [rax+28h]
		0x90, 0x90, 0x90, 0x90, 0x90,
		0x90, 0x90, 0x90, 0x90, 0x90
	};

	static_assert(sizeof(expected) == sizeof(guarded), "R1O PacketEnd null guard patch size mismatch");
	PatchR1OBytesIfMatch(
		engineBase,
		0x1F902F,
		expected,
		guarded,
		sizeof(guarded),
		"fake-dedi CNetChan PacketEnd handler null guard");
}


static void PatchR1OReconnectClientsInvalidSignonState(uintptr_t engineBase)
{
	const unsigned char connectedState[] = {
		0xBA, 0x02, 0x00, 0x00, 0x00 // mov edx, SIGNONSTATE_CONNECTED
	};
	const unsigned char invalidRetryState[] = {
		0xBA, 0x06, 0x66, 0x00, 0x00 // mov edx, 0x6606
	};

	PatchR1OBytesIfMatch(
		engineBase,
		0x137C50,
		connectedState,
		invalidRetryState,
		sizeof(invalidRetryState),
		"fake-dedi ReconnectClients retry signon-state invalidation");
}

static void PatchR1OMapChangeStudioRenderTeardown(uintptr_t engineBase)
{
	const unsigned char expected[] = {
		0x48, 0x8B, 0x0D, 0xBB, 0x3D, 0x21, 0x02,             // mov rcx, qword_1822FB6B0
		0x48, 0x8B, 0x01,                                     // mov rax, [rcx]
		0xFF, 0x90, 0x18, 0x01, 0x00, 0x00                    // call qword ptr [rax+118h]
	};
	const unsigned char noOp[] = {
		0x90, 0x90, 0x90, 0x90,
		0x90, 0x90, 0x90, 0x90,
		0x90, 0x90, 0x90, 0x90,
		0x90, 0x90, 0x90, 0x90
	};

	static_assert(sizeof(expected) == sizeof(noOp), "R1O map-change studiorender teardown patch size mismatch");
	PatchR1OBytesIfMatch(
		engineBase,
		0x0E78EE,
		expected,
		noOp,
		sizeof(noOp),
		"fake-dedi map-change studiorender teardown no-op");
}


static void PatchR1OCoordinateEncodingForR1Client(uintptr_t engineBase)
{
	const unsigned char r1oFullCoordBits[] = {
		0x41, 0xB8, 0x0F, 0x00, 0x00, 0x00 // mov r8d, 0Fh
	};
	const unsigned char r1FullCoordBits[] = {
		0x41, 0xB8, 0x0E, 0x00, 0x00, 0x00 // mov r8d, 0Eh
	};

	PatchR1OBytesIfMatch(
		engineBase,
		0x27839B,
		r1oFullCoordBits,
		r1FullCoordBits,
		sizeof(r1FullCoordBits),
		"fake-dedi WriteBitCoord full integer bit count");
	PatchR1OBytesIfMatch(
		engineBase,
		0x27826B,
		r1oFullCoordBits,
		r1FullCoordBits,
		sizeof(r1FullCoordBits),
		"fake-dedi WriteBitCoordMP out-of-bounds integer bit count");

	// The sparse snapshot writer does not always re-encode properties; it advances
	// through serialized source buffers with SendProp skip functions, then copies
	// the skipped payload bits. R1O's SPROP_COORD float skip still accounts for
	// sign + 15 integer bits (16 total) while the R1 client expects sign + 14.
	// Leaving that unpatched shifts the serialized-field cursor after coord props
	// and can make later listed props (for example DT_PhysicsProp::m_vecMaxs) copy
	// zero payload bits.
	const unsigned char coordSkipSignPlus15Integer[] = {
		0xB8, 0x10, 0x00, 0x00, 0x00 // mov eax, 10h
	};
	const unsigned char coordSkipSignPlus14Integer[] = {
		0xB8, 0x0F, 0x00, 0x00, 0x00 // mov eax, 0Fh
	};
	PatchR1OBytesIfMatch(
		engineBase,
		0x1E0D1E,
		coordSkipSignPlus15Integer,
		coordSkipSignPlus14Integer,
		sizeof(coordSkipSignPlus14Integer),
		"fake-dedi SendProp SPROP_COORD skip integer payload bit count");

	// R1O re-reads some server-encoded delta buffers internally before they hit the
	// 2015 client, so the reader side must accept the same 14-bit coord format.
	const unsigned char cmp15[] = { 0x83, 0xF8, 0x0F };
	const unsigned char cmp14[] = { 0x83, 0xF8, 0x0E };
	const unsigned char and15[] = { 0x41, 0x81, 0xE3, 0xFF, 0x7F, 0x00, 0x00 };
	const unsigned char and14[] = { 0x41, 0x81, 0xE3, 0xFF, 0x3F, 0x00, 0x00 };
	const unsigned char addNeg15[] = { 0x83, 0xC0, 0xF1 };
	const unsigned char addNeg14[] = { 0x83, 0xC0, 0xF2 };
	const unsigned char shr15[] = { 0xC1, 0xE9, 0x0F };
	const unsigned char shr14[] = { 0xC1, 0xE9, 0x0E };
	const unsigned char movEbx15[] = { 0xBB, 0x0F, 0x00, 0x00, 0x00 };
	const unsigned char movEbx14[] = { 0xBB, 0x0E, 0x00, 0x00, 0x00 };
	const unsigned char mpAnd15[] = { 0x41, 0x81, 0xE2, 0xFF, 0x7F, 0x00, 0x00 };
	const unsigned char mpAnd14[] = { 0x41, 0x81, 0xE2, 0xFF, 0x3F, 0x00, 0x00 };
	const unsigned char mpShr15[] = { 0xC1, 0xE9, 0x0F };
	const unsigned char mpShr14[] = { 0xC1, 0xE9, 0x0E };

	PatchR1OBytesIfMatch(engineBase, 0x27BF64, cmp15, cmp14, sizeof(cmp14), "fake-dedi ReadBitCoord full integer available-bit compare");
	PatchR1OBytesIfMatch(engineBase, 0x27BF70, and15, and14, sizeof(and14), "fake-dedi ReadBitCoord full integer mask");
	PatchR1OBytesIfMatch(engineBase, 0x27BF77, addNeg15, addNeg14, sizeof(addNeg14), "fake-dedi ReadBitCoord full integer remaining-bit subtract");
	PatchR1OBytesIfMatch(engineBase, 0x27BF80, shr15, shr14, sizeof(shr14), "fake-dedi ReadBitCoord full integer shift");
	PatchR1OBytesIfMatch(engineBase, 0x27BFDA, movEbx15, movEbx14, sizeof(movEbx14), "fake-dedi ReadBitCoord split full integer bit count");
	PatchR1OBytesIfMatch(engineBase, 0x27C2F2, cmp15, cmp14, sizeof(cmp14), "fake-dedi ReadBitCoordMP full integer available-bit compare");
	PatchR1OBytesIfMatch(engineBase, 0x27C2FE, mpAnd15, mpAnd14, sizeof(mpAnd14), "fake-dedi ReadBitCoordMP full integer mask");
	PatchR1OBytesIfMatch(engineBase, 0x27C305, addNeg15, addNeg14, sizeof(addNeg14), "fake-dedi ReadBitCoordMP full integer remaining-bit subtract");
	PatchR1OBytesIfMatch(engineBase, 0x27C30E, mpShr15, mpShr14, sizeof(mpShr14), "fake-dedi ReadBitCoordMP full integer shift");
	PatchR1OBytesIfMatch(engineBase, 0x27C364, movEbx15, movEbx14, sizeof(movEbx14), "fake-dedi ReadBitCoordMP split full integer bit count");
}

static __int64 __fastcall R1ONullClientOnlyInterfaceCall()
{
	return 0;
}

static void* s_R1ONullClientOnlyInterfaceVTable[128];

static void* GetR1ONullClientOnlyInterface()
{
	static struct NullClientOnlyInterfaceObject {
		void** vtable;
	} object{ s_R1ONullClientOnlyInterfaceVTable };

	static bool initialized = false;
	if (!initialized) {
		for (void*& slot : s_R1ONullClientOnlyInterfaceVTable)
			slot = reinterpret_cast<void*>(&R1ONullClientOnlyInterfaceCall);
		initialized = true;
	}

	return &object;
}

static __int64 __fastcall R1OFakeMaterialNoOp()
{
	return 0;
}

static const char* __fastcall R1OFakeMaterialGetName()
{
	return "r1delta/fake_dedicated_material";
}

static unsigned short __fastcall R1OFakeMaterialFindVar()
{
	return 0xFFFF;
}

static const char* __fastcall R1OFakeMaterialGetString()
{
	return "";
}

static __int64 __fastcall R1OWorldModelReleaseNoOp()
{
	return 0;
}

static const char* R1OLauncherGlobalCString(HMODULE launcher, uintptr_t rva, const char* fallback = "")
{
	if (!launcher)
		return fallback;

	const char* const* slot = reinterpret_cast<const char* const*>(reinterpret_cast<uintptr_t>(launcher) + rva);
	if (!IsReadableRange(slot, sizeof(*slot)) || !*slot || !IsReadableCString(*slot))
		return fallback;
	return *slot;
}

static int R1OLauncherGlobalInt(HMODULE launcher, uintptr_t rva, int fallback = 0)
{
	if (!launcher)
		return fallback;

	const int* slot = reinterpret_cast<const int*>(reinterpret_cast<uintptr_t>(launcher) + rva);
	return IsReadableRange(slot, sizeof(*slot)) ? *slot : fallback;
}

static bool TryGetR1OFatalScriptPolicyValue(const char* name, int& value)
{
	if (!name || !cvarinterface)
		return false;

	// These policy ConVars are owned by TFO launcher.dll. Engine/RCON setters
	// operate the native object returned by the R1O ICvar adapter; its R1 mirror
	// can lag behind and is not authoritative for launcher fatal dispatch.
	ConVarR1O* var = CCVar_FindVar(cvarinterface, name);
	if (!var || !IsReadableRange(var, sizeof(*var)))
		return false;

	ConVarR1O* parent = var->m_pParent ? var->m_pParent : var;
	if (!IsReadableRange(parent, sizeof(*parent)))
		return false;

	value = parent->m_Value.m_nValue;
	return true;
}

static __int64 __fastcall R1OLauncherScriptFatalReporterCanSuppress(void* thisptr)
{
	int fatalScriptErrors = 0;
	int fatalScriptErrorsServer = -1;
	const bool hasFatalScriptErrors =
		TryGetR1OFatalScriptPolicyValue("fatal_script_errors", fatalScriptErrors);
	const bool hasFatalScriptErrorsServer =
		TryGetR1OFatalScriptPolicyValue("fatal_script_errors_server", fatalScriptErrorsServer);

	// The context-specific ConVar is tri-state: -1 inherits the master, 0
	// disables server fatals, and 1 enables them. The master enable always wins,
	// so the effective server policy is:
	//     fatal_script_errors == 1 || fatal_script_errors_server == 1
	// The reporter may suppress only when that effective policy is false.
	// If registration has not completed, preserve the launcher's fatal behavior
	// instead of accidentally hiding an error.
	const bool suppress = hasFatalScriptErrors
		&& hasFatalScriptErrorsServer
		&& fatalScriptErrors != 1
		&& fatalScriptErrorsServer != 1;

	if (s_R1OLauncherScriptFatalLogBudget > 0) {
		char buffer[384];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O launcher fatal reporter suppress query this=%p fatal_script_errors=%d resolved=%d fatal_script_errors_server=%d resolved=%d returning=%d\n",
			thisptr,
			static_cast<int>(fatalScriptErrors),
			static_cast<int>(hasFatalScriptErrors),
			static_cast<int>(fatalScriptErrorsServer),
			static_cast<int>(hasFatalScriptErrorsServer),
			static_cast<int>(suppress));
		OutputDebugStringA(buffer);
	}
	return suppress ? 1 : 0;
}

static __int64 __fastcall R1OLauncherScriptFatalReporterNotified(void* thisptr)
{
	if (s_R1OLauncherScriptFatalLogBudget > 0) {
		char buffer[256];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O launcher fatal reporter notified this=%p\n",
			thisptr);
		OutputDebugStringA(buffer);
	}
	return 0;
}

static void* GetR1OLauncherScriptFatalReporter()
{
	static void* reporterVTable[32];
	static struct LauncherScriptFatalReporter {
		void** vtable;
	} reporter{ reporterVTable };

	static bool initialized = false;
	if (!initialized) {
		for (void*& slot : reporterVTable)
			slot = reinterpret_cast<void*>(&R1OLauncherScriptFatalReporterNotified);
		reporterVTable[15] = reinterpret_cast<void*>(&R1OLauncherScriptFatalReporterCanSuppress);
		reporterVTable[16] = reinterpret_cast<void*>(&R1OLauncherScriptFatalReporterNotified);
		initialized = true;
	}

	return &reporter;
}

static __int64 __fastcall R1OLauncherScriptFatalDispatch()
{
	ScriptErrorTelemetry::BeginErrorBlock(ScriptErrorTelemetry::VmContext::Server);
	HMODULE launcher = GetModuleHandleA("launcher.dll");
	const char* source = R1OLauncherGlobalCString(launcher, 0xF2DB8, "");
	const char* callstack = R1OLauncherGlobalCString(launcher, 0xF2DA8, "");

	// TFO formats and emits its script-error text before entering this fatal
	// dispatch function. BeginErrorBlock therefore cannot observe those print
	// calls. Feed the launcher's authoritative error/callstack globals into the
	// normal parser here while they are still valid; the rest of the telemetry
	// pipeline (sanitization, opt-out, queueing, and upload) remains shared.
	if (source && *source) {
		char errorLine[2048];
		_snprintf_s(
			errorLine,
			sizeof(errorLine),
			_TRUNCATE,
			"SCRIPT ERROR: %s\n",
			source);
		ScriptErrorTelemetry::CapturePrint(ScriptErrorTelemetry::VmContext::Server, errorLine);
		if (callstack && *callstack) {
			ScriptErrorTelemetry::CapturePrint(ScriptErrorTelemetry::VmContext::Server, "CALLSTACK\n");
			ScriptErrorTelemetry::CapturePrint(ScriptErrorTelemetry::VmContext::Server, callstack);
			const size_t callstackLength = strlen(callstack);
			if (callstackLength && callstack[callstackLength - 1] != '\n')
				ScriptErrorTelemetry::CapturePrint(ScriptErrorTelemetry::VmContext::Server, "\n");
		}
	}

	if (launcher && s_R1OLauncherScriptFatalLogBudget > 0) {
		--s_R1OLauncherScriptFatalLogBudget;

		const int errorKind = R1OLauncherGlobalInt(launcher, 0xF1D9C, -1);
		const int line = R1OLauncherGlobalInt(launcher, 0xF2DC8, -1);
		const unsigned char pending = IsReadableRange(reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(launcher) + 0xF1D99), 1)
			? *reinterpret_cast<unsigned char*>(reinterpret_cast<uintptr_t>(launcher) + 0xF1D99)
			: 0;

		const char* summary = R1OLauncherGlobalCString(launcher, 0xF2DB0, "<missing>");
		const char* diag = R1OLauncherGlobalCString(launcher, 0xF2DC0, "");
		const char* lineText = R1OLauncherGlobalCString(launcher, 0xF2DA0, "");

		char buffer[2048];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O launcher fatal script dispatch pending=%u kind=%d source=\"%s\" line=%d summary=\"%s\" lineText=\"%s\" callstack=\"%s\" diag=\"%s\" budget=%d\n",
			static_cast<unsigned int>(pending),
			errorKind,
			source,
			line,
			summary,
			lineText,
			callstack,
			diag,
			s_R1OLauncherScriptFatalLogBudget);
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}

	const __int64 result = R1OLauncherScriptFatalDispatchOriginal ? R1OLauncherScriptFatalDispatchOriginal() : 0;
	ScriptErrorTelemetry::EndErrorBlock(ScriptErrorTelemetry::VmContext::Server);
	return result;
}


static void* GetR1OFakeDedicatedMaterial()
{
	static void* fakeMaterialVTable[128];
	static struct FakeMaterialObject {
		void** vtable;
		const char* name;
		int refCount;
	} fakeMaterial{ fakeMaterialVTable, "r1delta/fake_dedicated_material", 1 };

	static bool initialized = false;
	if (!initialized) {
		for (void*& slot : fakeMaterialVTable)
			slot = reinterpret_cast<void*>(&R1OFakeMaterialNoOp);

		fakeMaterialVTable[0] = reinterpret_cast<void*>(&R1OFakeMaterialGetName);
		fakeMaterialVTable[55] = reinterpret_cast<void*>(&R1OFakeMaterialFindVar);
		fakeMaterialVTable[57] = reinterpret_cast<void*>(&R1OFakeMaterialFindVar);
		fakeMaterialVTable[70] = reinterpret_cast<void*>(&R1OFakeMaterialGetString);
		initialized = true;
	}

	return &fakeMaterial;
}

struct DedicatedServerModInfo2015 {
	void* m_pInstance;
	const char* m_pBaseDirectory;
	const char* m_pInitialMod;
	const char* m_pInitialGame;
	void* m_pParentAppSystemGroup;
	bool m_bTextMode;
};

static bool IsReadableProtect(DWORD protect)
{
	if (protect & (PAGE_GUARD | PAGE_NOACCESS))
		return false;

	protect &= 0xff;
	return protect == PAGE_READONLY
		|| protect == PAGE_READWRITE
		|| protect == PAGE_WRITECOPY
		|| protect == PAGE_EXECUTE_READ
		|| protect == PAGE_EXECUTE_READWRITE
		|| protect == PAGE_EXECUTE_WRITECOPY;
}

static bool IsReadableRange(const void* ptr, size_t size)
{
	return MaterialSystemDx11LooksLikeUserRange(reinterpret_cast<uintptr_t>(ptr), size);
}

static bool IsReadableCString(const char* ptr)
{
	return !!ptr;
}

static const char* R1OSafeCString(const char* text, const char* fallback = "<invalid>")
{
	return IsReadableCString(text) ? text : fallback;
}

static bool ResolveR1OVPhysicsDeferredRelease(uintptr_t vphysicsBase)
{
	if (!IsR1ODedicatedServer())
		return false;

	if (!vphysicsBase) {
		HMODULE module = GetModuleHandleA("vphysics.dll");
		vphysicsBase = reinterpret_cast<uintptr_t>(module);
	}
	if (!vphysicsBase)
		return false;

	s_R1OVPhysicsBase = vphysicsBase;

	if (!R1OVPhysicsThreadLocalGet) {
		auto slot = reinterpret_cast<R1OVPhysicsThreadLocalGetType*>(vphysicsBase + 0x1272E0);
		if (IsReadableRange(slot, sizeof(*slot)))
			R1OVPhysicsThreadLocalGet = *slot;
	}
	if (!R1OVPhysicsThreadLocalSet) {
		auto slot = reinterpret_cast<R1OVPhysicsThreadLocalSetType*>(vphysicsBase + 0x1272E8);
		if (IsReadableRange(slot, sizeof(*slot)))
			R1OVPhysicsThreadLocalSet = *slot;
	}
	if (!R1OVPhysicsReleaseCachedCollision)
		R1OVPhysicsReleaseCachedCollision = reinterpret_cast<R1OVPhysicsReleaseCachedCollisionType>(vphysicsBase + 0x0FB5F0);

	return R1OVPhysicsThreadLocalGet
		&& R1OVPhysicsThreadLocalSet
		&& IsReadableRange(reinterpret_cast<void*>(vphysicsBase + 0x18666C), 4)
		&& IsReadableRange(reinterpret_cast<void*>(vphysicsBase + 0x186670), sizeof(SLIST_HEADER))
		&& IsReadableRange(reinterpret_cast<void*>(R1OVPhysicsReleaseCachedCollision), 16);
}

static void DrainR1OVPhysicsDeferredReleases(const char* reason)
{
	if (!ResolveR1OVPhysicsDeferredRelease(s_R1OVPhysicsBase)) {
		if (R1OVPhysicsDeferredReleaseOriginal)
			R1OVPhysicsDeferredReleaseOriginal();
		return;
	}

	void* tlsBase = reinterpret_cast<void*>(s_R1OVPhysicsBase + 0x18666C);
	auto* listHead = reinterpret_cast<PSLIST_HEADER>(s_R1OVPhysicsBase + 0x186670);
	auto* state = reinterpret_cast<unsigned char*>(R1OVPhysicsThreadLocalGet(tlsBase));
	if (!state)
		return;
	if (!IsReadableRange(state, 0x20)) {
		if (R1OVPhysicsDeferredReleaseOriginal)
			R1OVPhysicsDeferredReleaseOriginal();
		return;
	}

	void** entries = *reinterpret_cast<void***>(state);
	const __int64 capacity = *reinterpret_cast<__int64*>(state + 0x08);
	int count = *reinterpret_cast<int*>(state + 0x18);
	const bool saneCount = count >= 0 && count <= 65536;
	const bool saneEntries = !count || (entries && IsReadableRange(entries, sizeof(void*) * static_cast<size_t>(count)));
	bool hasBadEntry = !saneCount || !saneEntries;

	if (!hasBadEntry) {
		for (int i = 0; i < count; ++i) {
			void* object = entries[i];
			if (!IsReadableRange(reinterpret_cast<unsigned char*>(object) + 0x28, sizeof(unsigned int))) {
				hasBadEntry = true;
				break;
			}
		}
	}

	if (!hasBadEntry && R1OVPhysicsDeferredReleaseOriginal) {
		R1OVPhysicsDeferredReleaseOriginal();
		return;
	}

	int released = 0;
	int skipped = 0;
	void* firstBadObject = nullptr;
	unsigned int firstBadIndex = 0;
	if (saneCount && saneEntries) {
		for (int i = 0; i < count; ++i) {
			void* object = entries[i];
			if (!IsReadableRange(reinterpret_cast<unsigned char*>(object) + 0x28, sizeof(unsigned int))) {
				if (!firstBadObject) {
					firstBadObject = object;
					firstBadIndex = static_cast<unsigned int>(i);
				}
				++skipped;
				continue;
			}

			const unsigned int handle = *reinterpret_cast<unsigned int*>(reinterpret_cast<unsigned char*>(object) + 0x28);
			R1OVPhysicsReleaseCachedCollision(entries, handle);
			++released;
		}
	}
	else {
		skipped = count > 0 ? count : 1;
		*reinterpret_cast<void***>(state) = nullptr;
		*reinterpret_cast<__int64*>(state + 0x08) = 0;
		*reinterpret_cast<void**>(state + 0x10) = nullptr;
	}

	*reinterpret_cast<int*>(state + 0x18) = 0;
	void* currentState = R1OVPhysicsThreadLocalGet(tlsBase);
	if (currentState && IsReadableRange(reinterpret_cast<unsigned char*>(currentState) - 0x10, sizeof(SLIST_ENTRY)))
		InterlockedPushEntrySList(listHead, reinterpret_cast<PSLIST_ENTRY>(reinterpret_cast<unsigned char*>(currentState) - 0x10));
	R1OVPhysicsThreadLocalSet(tlsBase, nullptr);

	if ((hasBadEntry || AreR1OFakeDediVerboseLogsEnabled()) && s_R1OVPhysicsDeferredReleaseCrashLogBudget > 0) {
		--s_R1OVPhysicsDeferredReleaseCrashLogBudget;
		Warning(
			"R1Delta: R1O blocked crash: drained TFO vphysics deferred-release queue reason=%s count=%d capacity=%lld released=%d skipped=%d firstBadIndex=%u firstBadObject=%p budget=%d\n",
			reason ? reason : "<unknown>",
			count,
			static_cast<long long>(capacity),
			released,
			skipped,
			firstBadIndex,
			firstBadObject,
			s_R1OVPhysicsDeferredReleaseCrashLogBudget);
	}
}

static void __fastcall R1OVPhysicsDeferredRelease()
{
	if (!IsR1ODedicatedServer()) {
		if (R1OVPhysicsDeferredReleaseOriginal)
			R1OVPhysicsDeferredReleaseOriginal();
		return;
	}

	DrainR1OVPhysicsDeferredReleases("vphysics simulate");
}

void InstallR1OVPhysicsDeferredReleaseGuard(uintptr_t vphysicsBase)
{
	if (!IsR1ODedicatedServer() || s_R1OVPhysicsDeferredReleaseGuardHooked)
		return;

	if (!ResolveR1OVPhysicsDeferredRelease(vphysicsBase))
		return;

	void* target = reinterpret_cast<void*>(s_R1OVPhysicsBase + 0x03C8D0);
	if (!IsReadableRange(target, 16))
		return;

	const MH_STATUS status = MH_CreateHook(
		target,
		&R1OVPhysicsDeferredRelease,
		reinterpret_cast<LPVOID*>(&R1OVPhysicsDeferredReleaseOriginal));
	const MH_STATUS enableStatus = (status == MH_OK || status == MH_ERROR_ALREADY_CREATED)
		? MH_EnableHook(target)
		: status;
	s_R1OVPhysicsDeferredReleaseGuardHooked = status == MH_OK || status == MH_ERROR_ALREADY_CREATED;

	if (AreR1OFakeDediVerboseLogsEnabled()) {
		char buffer[320];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O TFO vphysics deferred-release guard status=%d enable=%d target=%p original=%p base=%p\n",
			static_cast<int>(status),
			static_cast<int>(enableStatus),
			target,
			reinterpret_cast<void*>(R1OVPhysicsDeferredReleaseOriginal),
			reinterpret_cast<void*>(s_R1OVPhysicsBase));
		OutputDebugStringA(buffer);
	}
}

static const char* R1OSendPropName(__int64 sendProp)
{
	if (!sendProp || !IsReadableRange(reinterpret_cast<const void*>(sendProp + 0x48), sizeof(const char*)))
		return "<null>";

	const char* name = *reinterpret_cast<const char**>(sendProp + 0x48);
	return R1OSafeCString(name, "<bad-name>");
}

static int R1OSendPropType(__int64 sendProp)
{
	return sendProp && IsReadableRange(reinterpret_cast<const void*>(sendProp + 0x10), sizeof(int))
		? *reinterpret_cast<int*>(sendProp + 0x10)
		: -1;
}

static int R1OSendPropFlags(__int64 sendProp)
{
	return sendProp && IsReadableRange(reinterpret_cast<const void*>(sendProp + 0x58), sizeof(int))
		? *reinterpret_cast<int*>(sendProp + 0x58)
		: -1;
}

static int R1OSendPropOffset(__int64 sendProp)
{
	return sendProp && IsReadableRange(reinterpret_cast<const void*>(sendProp + 0x78), sizeof(int))
		? *reinterpret_cast<int*>(sendProp + 0x78)
		: -1;
}

static int R1OSendPropNumBits(__int64 sendProp)
{
	return sendProp && IsReadableRange(reinterpret_cast<const void*>(sendProp + 0x14), sizeof(int))
		? *reinterpret_cast<int*>(sendProp + 0x14)
		: -1;
}

static const char* R1OSendTableName(__int64 sendTable)
{
	if (!sendTable || !IsReadableRange(reinterpret_cast<const void*>(sendTable + 0x10), sizeof(const char*)))
		return "<null>";

	return R1OSafeCString(*reinterpret_cast<const char**>(sendTable + 0x10), "<bad-table>");
}

static int R1OSendTableFlatCount(__int64 sendTable)
{
	const __int64 precalc = sendTable && IsReadableRange(reinterpret_cast<const void*>(sendTable + 0x18), sizeof(__int64))
		? *reinterpret_cast<__int64*>(sendTable + 0x18)
		: 0;
	return precalc && IsReadableRange(reinterpret_cast<const void*>(precalc + 0x68), sizeof(int))
		? *reinterpret_cast<int*>(precalc + 0x68)
		: -1;
}

static __int64 R1OSendTableFlatProp(__int64 sendTable, int propIndex)
{
	const __int64 precalc = sendTable && IsReadableRange(reinterpret_cast<const void*>(sendTable + 0x18), sizeof(__int64))
		? *reinterpret_cast<__int64*>(sendTable + 0x18)
		: 0;
	const int flatCount = precalc && IsReadableRange(reinterpret_cast<const void*>(precalc + 0x68), sizeof(int))
		? *reinterpret_cast<int*>(precalc + 0x68)
		: -1;
	const __int64 flatProps = precalc && IsReadableRange(reinterpret_cast<const void*>(precalc + 0x50), sizeof(__int64))
		? *reinterpret_cast<__int64*>(precalc + 0x50)
		: 0;
	if (!flatProps || propIndex < 0 || propIndex >= flatCount)
		return 0;
	const __int64 slot = flatProps + sizeof(__int64) * static_cast<size_t>(propIndex);
	return IsReadableRange(reinterpret_cast<const void*>(slot), sizeof(__int64))
		? *reinterpret_cast<__int64*>(slot)
		: 0;
}

static __int64 R1OFindFlatSendProp(__int64 sendTable, const char* name, int occurrence = 0, int* outIndex = nullptr)
{
	if (outIndex)
		*outIndex = -1;
	if (!sendTable || !name || occurrence < 0)
		return 0;

	const int flatCount = R1OSendTableFlatCount(sendTable);
	int seen = 0;
	for (int i = 0; i < flatCount && i < 4096; ++i) {
		const __int64 prop = R1OSendTableFlatProp(sendTable, i);
		if (!prop)
			continue;
		const char* propName = R1OSendPropName(prop);
		if (propName && !_stricmp(propName, name)) {
			if (seen == occurrence) {
				if (outIndex)
					*outIndex = i;
				return prop;
			}
			++seen;
		}
	}
	return 0;
}

static int R1OSendPropStorageOffset(__int64 sendProp)
{
	const int offset = R1OSendPropOffset(sendProp);
	if (offset < 0)
		return -1;

	// R1O/TFO SendProp offsets for some flattened player props carry high-bit storage
	// tags (for example 0xD0043C for m_localOrigin). The low 20 bits are the
	// in-object storage offset used by the encoder-side datamap/proxy path.
	return offset >= 0x100000 ? (offset & 0xFFFFF) : offset;
}

static int R1OSafeReadIntField(__int64 object, int offset, int defaultValue = -999999)
{
	if (!object || offset < 0 || offset > 0x2000000)
		return defaultValue;

	__try {
		const void* ptr = reinterpret_cast<const void*>(object + offset);
		if (!IsReadableRange(ptr, sizeof(int)))
			return defaultValue;
		return *reinterpret_cast<const int*>(ptr);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return defaultValue;
	}
}

static unsigned char R1OSafeReadByteField(__int64 object, int offset, unsigned char defaultValue = 0xff)
{
	if (!object || offset < 0 || offset > 0x2000000)
		return defaultValue;

	__try {
		const void* ptr = reinterpret_cast<const void*>(object + offset);
		if (!IsReadableRange(ptr, sizeof(unsigned char)))
			return defaultValue;
		return *reinterpret_cast<const unsigned char*>(ptr);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return defaultValue;
	}
}

static float R1OSafeReadFloatField(__int64 object, int offset)
{
	if (!object || offset < 0 || offset > 0x2000000)
		return NAN;

	__try {
		const void* ptr = reinterpret_cast<const void*>(object + offset);
		if (!IsReadableRange(ptr, sizeof(float)))
			return NAN;
		return *reinterpret_cast<const float*>(ptr);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return NAN;
	}
}

static void R1OLogHL2PlayerNetpropValues(__int64 sendTable, __int64 object, int objectId, char delta)
{
	if (!IsR1ODedicatedServer() || s_R1OPlayerNetpropCompareLogBudget <= 0 || !sendTable || !object)
		return;

	const char* tableName = R1OSendTableName(sendTable);
	if (_stricmp(R1OSafeCString(tableName), "DT_HL2_Player") != 0)
		return;

	if (objectId != 1)
		return;

	const int seq = ++s_R1OPlayerNetpropCompareSeen;
	if (seq > 8 && (seq % 60) != 0)
		return;

	--s_R1OPlayerNetpropCompareLogBudget;

	int idxTeam = -1;
	int idxHealth = -1;
	int idxLife = -1;
	int idxFlags = -1;
	int idxCellBits = -1;
	int idxCellX = -1;
	int idxCellY = -1;
	int idxCellZ = -1;
	int idxOrigin = -1;
	int idxOriginZ = -1;
	int idxViewZ = -1;
	int idxEyePitch = -1;
	int idxEyeYaw = -1;
	int idxObserverMode = -1;
	int idxObserverTarget = -1;
	int idxViewEntity = -1;
	int idxViewOffsetEntity = -1;
	int idxThirdPersonEnt = -1;
	int idxPostProcessCtrl = -1;
	int idxColorCorrectionCtrl = -1;
	int idxFogCtrl = -1;

	const __int64 propTeam = R1OFindFlatSendProp(sendTable, "m_iTeamNum", 0, &idxTeam);
	const __int64 propHealth = R1OFindFlatSendProp(sendTable, "m_iHealth", 0, &idxHealth);
	const __int64 propLife = R1OFindFlatSendProp(sendTable, "m_lifeState", 0, &idxLife);
	const __int64 propFlags = R1OFindFlatSendProp(sendTable, "m_fFlags", 0, &idxFlags);
	const __int64 propCellBits = R1OFindFlatSendProp(sendTable, "m_cellbits", 0, &idxCellBits);
	const __int64 propCellX = R1OFindFlatSendProp(sendTable, "m_cellX", 0, &idxCellX);
	const __int64 propCellY = R1OFindFlatSendProp(sendTable, "m_cellY", 0, &idxCellY);
	const __int64 propCellZ = R1OFindFlatSendProp(sendTable, "m_cellZ", 0, &idxCellZ);
	const __int64 propOrigin = R1OFindFlatSendProp(sendTable, "m_localOrigin", 0, &idxOrigin);
	const __int64 propOriginZ = R1OFindFlatSendProp(sendTable, "m_localOrigin[2]", 0, &idxOriginZ);
	const __int64 propViewZ = R1OFindFlatSendProp(sendTable, "m_vecViewOffset[2]", 0, &idxViewZ);
	const __int64 propEyePitch = R1OFindFlatSendProp(sendTable, "m_angEyeAngles[0]", 0, &idxEyePitch);
	const __int64 propEyeYaw = R1OFindFlatSendProp(sendTable, "m_angEyeAngles[1]", 0, &idxEyeYaw);
	const __int64 propObserverMode = R1OFindFlatSendProp(sendTable, "m_iObserverMode", 0, &idxObserverMode);
	const __int64 propObserverTarget = R1OFindFlatSendProp(sendTable, "m_hObserverTarget", 0, &idxObserverTarget);
	const __int64 propViewEntity = R1OFindFlatSendProp(sendTable, "m_hViewEntity", 0, &idxViewEntity);
	const __int64 propViewOffsetEntity = R1OFindFlatSendProp(sendTable, "m_hViewOffsetEntity", 0, &idxViewOffsetEntity);
	const __int64 propThirdPersonEnt = R1OFindFlatSendProp(sendTable, "m_hThirdPersonEnt", 0, &idxThirdPersonEnt);
	const __int64 propPostProcessCtrl = R1OFindFlatSendProp(sendTable, "m_hPostProcessCtrl", 0, &idxPostProcessCtrl);
	const __int64 propColorCorrectionCtrl = R1OFindFlatSendProp(sendTable, "m_hColorCorrectionCtrl", 0, &idxColorCorrectionCtrl);
	const __int64 propFogCtrl = R1OFindFlatSendProp(sendTable, "m_PlayerFog.m_hCtrl", 0, &idxFogCtrl);

	const int offTeam = R1OSendPropStorageOffset(propTeam);
	const int offHealth = R1OSendPropStorageOffset(propHealth);
	const int offLife = R1OSendPropStorageOffset(propLife);
	const int offFlags = R1OSendPropStorageOffset(propFlags);
	const int offCellBits = R1OSendPropStorageOffset(propCellBits);
	const int offCellX = R1OSendPropStorageOffset(propCellX);
	const int offCellY = R1OSendPropStorageOffset(propCellY);
	const int offCellZ = R1OSendPropStorageOffset(propCellZ);
	const int offOrigin = R1OSendPropStorageOffset(propOrigin);
	const int offOriginZ = R1OSendPropStorageOffset(propOriginZ);
	const int offViewZ = R1OSendPropStorageOffset(propViewZ);
	const int offEyePitch = R1OSendPropStorageOffset(propEyePitch);
	const int offEyeYaw = R1OSendPropStorageOffset(propEyeYaw);
	const int offObserverMode = R1OSendPropStorageOffset(propObserverMode);
	const int offObserverTarget = R1OSendPropStorageOffset(propObserverTarget);
	const int offViewEntity = R1OSendPropStorageOffset(propViewEntity);
	const int offViewOffsetEntity = R1OSendPropStorageOffset(propViewOffsetEntity);
	const int offThirdPersonEnt = R1OSendPropStorageOffset(propThirdPersonEnt);
	const int offPostProcessCtrl = R1OSendPropStorageOffset(propPostProcessCtrl);
	const int offColorCorrectionCtrl = R1OSendPropStorageOffset(propColorCorrectionCtrl);
	const int offFogCtrl = R1OSendPropStorageOffset(propFogCtrl);

	char buffer[2048];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: netprop-compare server seq=%d objectId=%d object=%p delta=%d flat=%d team[%d:%d/%d]=%d health[%d:%d/%d]=%d life[%d:%d/%d]=%d flags[%d:%d/%d]=0x%x cellbits[%d:%d/%d]=%d cell=(%d:%d/%d:%d,%d:%d/%d:%d,%d:%d/%d:%d) origin[%d:%d/%d,%d:%d/%d]=(%.3f,%.3f,%.3f) viewZ[%d:%d/%d]=%.3f eye=(%d:%d/%d:%.3f,%d:%d/%d:%.3f) observerMode[%d:%d/%d]=%d observerTarget[%d:%d/%d]=0x%x viewEnt[%d:%d/%d]=0x%x viewOffsetEnt[%d:%d/%d]=0x%x thirdPersonEnt[%d:%d/%d]=0x%x postProcess[%d:%d/%d]=0x%x colorCorrection[%d:%d/%d]=0x%x fogCtrl[%d:%d/%d]=0x%x budget=%d\n",
		seq,
		objectId,
		reinterpret_cast<void*>(object),
		static_cast<int>(delta),
		R1OSendTableFlatCount(sendTable),
		idxTeam,
		R1OSendPropOffset(propTeam),
		offTeam,
		R1OSafeReadIntField(object, offTeam),
		idxHealth,
		R1OSendPropOffset(propHealth),
		offHealth,
		R1OSafeReadIntField(object, offHealth),
		idxLife,
		R1OSendPropOffset(propLife),
		offLife,
		static_cast<int>(R1OSafeReadByteField(object, offLife)),
		idxFlags,
		R1OSendPropOffset(propFlags),
		offFlags,
		R1OSafeReadIntField(object, offFlags),
		idxCellBits,
		R1OSendPropOffset(propCellBits),
		offCellBits,
		R1OSafeReadIntField(object, offCellBits),
		idxCellX,
		R1OSendPropOffset(propCellX),
		offCellX,
		R1OSafeReadIntField(object, offCellX),
		idxCellY,
		R1OSendPropOffset(propCellY),
		offCellY,
		R1OSafeReadIntField(object, offCellY),
		idxCellZ,
		R1OSendPropOffset(propCellZ),
		offCellZ,
		R1OSafeReadIntField(object, offCellZ),
		idxOrigin,
		R1OSendPropOffset(propOrigin),
		offOrigin,
		idxOriginZ,
		R1OSendPropOffset(propOriginZ),
		offOriginZ,
		R1OSafeReadFloatField(object, offOrigin),
		R1OSafeReadFloatField(object, offOrigin + 4),
		offOriginZ >= 0 ? R1OSafeReadFloatField(object, offOriginZ) : R1OSafeReadFloatField(object, offOrigin + 8),
		idxViewZ,
		R1OSendPropOffset(propViewZ),
		offViewZ,
		R1OSafeReadFloatField(object, offViewZ),
		idxEyePitch,
		R1OSendPropOffset(propEyePitch),
		offEyePitch,
		R1OSafeReadFloatField(object, offEyePitch),
		idxEyeYaw,
		R1OSendPropOffset(propEyeYaw),
		offEyeYaw,
		R1OSafeReadFloatField(object, offEyeYaw),
		idxObserverMode,
		R1OSendPropOffset(propObserverMode),
		offObserverMode,
		R1OSafeReadIntField(object, offObserverMode),
		idxObserverTarget,
		R1OSendPropOffset(propObserverTarget),
		offObserverTarget,
		R1OSafeReadIntField(object, offObserverTarget),
		idxViewEntity,
		R1OSendPropOffset(propViewEntity),
		offViewEntity,
		R1OSafeReadIntField(object, offViewEntity),
		idxViewOffsetEntity,
		R1OSendPropOffset(propViewOffsetEntity),
		offViewOffsetEntity,
		R1OSafeReadIntField(object, offViewOffsetEntity),
		idxThirdPersonEnt,
		R1OSendPropOffset(propThirdPersonEnt),
		offThirdPersonEnt,
		R1OSafeReadIntField(object, offThirdPersonEnt),
		idxPostProcessCtrl,
		R1OSendPropOffset(propPostProcessCtrl),
		offPostProcessCtrl,
		R1OSafeReadIntField(object, offPostProcessCtrl),
		idxColorCorrectionCtrl,
		R1OSendPropOffset(propColorCorrectionCtrl),
		offColorCorrectionCtrl,
		R1OSafeReadIntField(object, offColorCorrectionCtrl),
		idxFogCtrl,
		R1OSendPropOffset(propFogCtrl),
		offFogCtrl,
		R1OSafeReadIntField(object, offFogCtrl),
		s_R1OPlayerNetpropCompareLogBudget);
	OutputDebugStringA(buffer);
	Warning("%s", buffer);
}

static void R1OBytesHex(char* out, size_t outSize, const unsigned char* data, unsigned int bytes)
{
	if (!out || !outSize)
		return;

	out[0] = '\0';
	if (!data || !bytes || !IsReadableRange(data, 1))
		return;

	size_t used = 0;
	const unsigned int limit = bytes < 24 ? bytes : 24;
	for (unsigned int i = 0; i < limit; ++i) {
		if (!IsReadableRange(data + i, 1))
			break;
		const int written = _snprintf_s(out + used, outSize - used, _TRUNCATE, "%s%02X", i ? " " : "", data[i]);
		if (written <= 0)
			break;
		used += static_cast<size_t>(written);
		if (used >= outSize)
			break;
	}
}

static int R1ODeltaWriterBit(__int64 writer)
{
	const __int64 bitBuffer = writer && IsReadableRange(reinterpret_cast<const void*>(writer + 8), sizeof(__int64))
		? *reinterpret_cast<__int64*>(writer + 8)
		: 0;
	return bitBuffer && IsReadableRange(reinterpret_cast<const void*>(bitBuffer + 16), sizeof(int))
		? *reinterpret_cast<int*>(bitBuffer + 16)
		: -1;
}

static __int64 R1ODeltaWriterBitBuffer(__int64 writer)
{
	return writer && IsReadableRange(reinterpret_cast<const void*>(writer + 8), sizeof(__int64))
		? *reinterpret_cast<__int64*>(writer + 8)
		: 0;
}

static bool R1OIsDeltaTraceTable(const char* tableName)
{
	if (!tableName)
		return false;

	tableName = R1OSafeCString(tableName, "<bad-table>");
	return _stricmp(tableName, "DT_WORLD") == 0
		|| _stricmp(tableName, "DT_World") == 0
		|| _stricmp(tableName, "DT_Team") == 0
		|| _stricmp(tableName, "DT_PhysicsProp") == 0
		|| _stricmp(tableName, "DT_ParticleSystem") == 0
		|| _stricmp(tableName, "DT_InfoPlacementHelper") == 0
		|| _stricmp(tableName, "DT_EnvTonemapController") == 0;
}

static void R1ODumpWriteBits(char* out, size_t outSize, __int64 bitBuffer, int startBit, int endBit)
{
	if (!out || !outSize)
		return;

	out[0] = '\0';
	if (!bitBuffer || startBit < 0 || endBit < startBit)
		return;

	const unsigned char* data = IsReadableRange(reinterpret_cast<const void*>(bitBuffer), sizeof(unsigned char*))
		? *reinterpret_cast<unsigned char const**>(bitBuffer)
		: nullptr;
	const int dataBits = IsReadableRange(reinterpret_cast<const void*>(bitBuffer + 12), sizeof(int))
		? *reinterpret_cast<int*>(bitBuffer + 12)
		: -1;
	if (!data || dataBits <= 0 || startBit >= dataBits)
		return;

	if (endBit > dataBits)
		endBit = dataBits;

	size_t used = 0;
	const int limit = endBit - startBit > 96 ? startBit + 96 : endBit;
	for (int bit = startBit; bit < limit && used + 2 < outSize; ++bit) {
		const int byteIndex = bit >> 3;
		if (!IsReadableRange(data + byteIndex, 1))
			break;
		out[used++] = (data[byteIndex] & (1u << (bit & 7))) ? '1' : '0';
	}
	out[used] = '\0';
	if (limit < endBit && used + 4 < outSize)
		strncpy_s(out + used, outSize - used, "...", _TRUNCATE);
}

static unsigned int R1OReadWriteBitsUnsigned(__int64 bitBuffer, int startBit, int endBit, bool* ok = nullptr)
{
	if (ok)
		*ok = false;
	if (!bitBuffer || startBit < 0 || endBit < startBit || endBit - startBit > 32)
		return 0;

	const unsigned char* data = IsReadableRange(reinterpret_cast<const void*>(bitBuffer), sizeof(unsigned char*))
		? *reinterpret_cast<unsigned char const**>(bitBuffer)
		: nullptr;
	const int dataBits = IsReadableRange(reinterpret_cast<const void*>(bitBuffer + 12), sizeof(int))
		? *reinterpret_cast<int*>(bitBuffer + 12)
		: -1;
	if (!data || dataBits <= 0 || endBit > dataBits)
		return 0;

	unsigned int value = 0;
	for (int bit = startBit; bit < endBit; ++bit) {
		const int byteIndex = bit >> 3;
		if (!IsReadableRange(data + byteIndex, 1))
			return 0;
		if (data[byteIndex] & (1u << (bit & 7)))
			value |= 1u << (bit - startBit);
	}
	if (ok)
		*ok = true;
	return value;
}

static __int64 __fastcall R1OWritePropList(
	__int64 sendTable,
	__int64 newState,
	__int64 oldState,
	__int64 output,
	__int64 recipients,
	unsigned int* changedProps,
	int changedPropCount)
{
	const char* tableName = R1OSendTableName(sendTable);
	const bool tracePlayer = IsR1ODedicatedServer()
		&& s_R1OWritePropListPlayerLogBudget > 0
		&& tableName
		&& _stricmp(R1OSafeCString(tableName), "DT_HL2_Player") == 0;
	const int beforeBit = tracePlayer
		&& output
		&& IsReadableRange(reinterpret_cast<const void*>(output + 16), sizeof(int))
		? *reinterpret_cast<int*>(output + 16)
		: -1;

	char changedList[2048] = {};
	bool hasCellX = false;
	bool hasCellY = false;
	bool hasCellZ = false;
	bool hasCellBits = false;
	if (tracePlayer && changedProps && changedPropCount > 0) {
		size_t used = 0;
		const int limit = changedPropCount < 256 ? changedPropCount : 256;
		for (int i = 0; i < limit; ++i) {
			if (!IsReadableRange(changedProps + i, sizeof(*changedProps)))
				break;

			const unsigned int propIndex = changedProps[i];
			const __int64 prop = propIndex < static_cast<unsigned int>(R1OSendTableFlatCount(sendTable))
				? R1OSendTableFlatProp(sendTable, static_cast<int>(propIndex))
				: 0;
			const char* propName = R1OSendPropName(prop);
			hasCellX = hasCellX || _stricmp(R1OSafeCString(propName), "m_cellX") == 0;
			hasCellY = hasCellY || _stricmp(R1OSafeCString(propName), "m_cellY") == 0;
			hasCellZ = hasCellZ || _stricmp(R1OSafeCString(propName), "m_cellZ") == 0;
			hasCellBits = hasCellBits || _stricmp(R1OSafeCString(propName), "m_cellbits") == 0;

			if (used + 32 >= sizeof(changedList))
				break;
			const int written = _snprintf_s(
				changedList + used,
				sizeof(changedList) - used,
				_TRUNCATE,
				"%s%u:%s",
				used ? "," : "",
				propIndex,
				R1OSafeCString(propName, "<bad-prop>"));
			if (written <= 0)
				break;
			used += static_cast<size_t>(written);
		}
	}

	const __int64 result = R1OWritePropListOriginal
		? R1OWritePropListOriginal(sendTable, newState, oldState, output, recipients, changedProps, changedPropCount)
		: 0;

	if (tracePlayer) {
		--s_R1OWritePropListPlayerLogBudget;
		const int afterBit = output
			&& IsReadableRange(reinterpret_cast<const void*>(output + 16), sizeof(int))
			? *reinterpret_cast<int*>(output + 16)
			: -1;
		const int newBits = newState
			&& IsReadableRange(reinterpret_cast<const void*>(newState + 8), sizeof(int))
			? *reinterpret_cast<int*>(newState + 8)
			: -1;
		const int oldBits = oldState
			&& IsReadableRange(reinterpret_cast<const void*>(oldState + 8), sizeof(int))
			? *reinterpret_cast<int*>(oldState + 8)
			: -1;
		char buffer[3072];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O WritePropList table=\"%s\" sendTable=%p new=%p newBits=%d old=%p oldBits=%d output=%p bits=%d->%d recipients=%p changedCount=%d cells={bits:%d,x:%d,y:%d,z:%d} changed=[%s] result=%lld budget=%d\n",
			R1OSafeCString(tableName),
			reinterpret_cast<void*>(sendTable),
			reinterpret_cast<void*>(newState),
			newBits,
			reinterpret_cast<void*>(oldState),
			oldBits,
			reinterpret_cast<void*>(output),
			beforeBit,
			afterBit,
			reinterpret_cast<void*>(recipients),
			changedPropCount,
			hasCellBits ? 1 : 0,
			hasCellX ? 1 : 0,
			hasCellY ? 1 : 0,
			hasCellZ ? 1 : 0,
			changedList,
			static_cast<long long>(result),
			s_R1OWritePropListPlayerLogBudget);
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}

	return result;
}

static __int64 __fastcall R1ODeltaCalculatorAdvance(__int64 calculator)
{
	const __int64 table = s_R1OBuildPropListCurrentTable;
	const char* tableName = R1OSendTableName(table);
	const int targetProp = calculator
		&& IsReadableRange(reinterpret_cast<const void*>(calculator + 192), sizeof(int))
		? *reinterpret_cast<int*>(calculator + 192)
		: -1;
	const bool traceCell = IsR1ODedicatedServer()
		&& s_R1ODeltaCalculatorPlayerLogBudget > 0
		&& table
		&& _stricmp(R1OSafeCString(tableName), "DT_HL2_Player") == 0
		&& targetProp >= 23
		&& targetProp <= 26;
	const int fromPropBefore = traceCell
		&& IsReadableRange(reinterpret_cast<const void*>(calculator + 188), sizeof(int))
		? *reinterpret_cast<int*>(calculator + 188)
		: -1;
	const int outputCountBefore = traceCell
		&& IsReadableRange(reinterpret_cast<const void*>(calculator + 212), sizeof(int))
		? *reinterpret_cast<int*>(calculator + 212)
		: -1;

	const __int64 result = R1ODeltaCalculatorAdvanceOriginal
		? R1ODeltaCalculatorAdvanceOriginal(calculator)
		: 0;

	if (traceCell) {
		--s_R1ODeltaCalculatorPlayerLogBudget;
		const int fromPropAfter = IsReadableRange(reinterpret_cast<const void*>(calculator + 188), sizeof(int))
			? *reinterpret_cast<int*>(calculator + 188)
			: -1;
		const int outputCountAfter = IsReadableRange(reinterpret_cast<const void*>(calculator + 212), sizeof(int))
			? *reinterpret_cast<int*>(calculator + 212)
			: -1;
		const __int64 prop = R1OSendTableFlatProp(table, targetProp);
		char buffer[1024];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O delta compare table=\"%s\" target=%d:%s from=%d->%d outputCount=%d->%d changed=%d calculator=%p result=%lld budget=%d\n",
			R1OSafeCString(tableName),
			targetProp,
			R1OSafeCString(R1OSendPropName(prop), "<bad-prop>"),
			fromPropBefore,
			fromPropAfter,
			outputCountBefore,
			outputCountAfter,
			outputCountAfter > outputCountBefore ? 1 : 0,
			reinterpret_cast<void*>(calculator),
			static_cast<long long>(result),
			s_R1ODeltaCalculatorPlayerLogBudget);
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}

	return result;
}

static __int64 __fastcall R1OBuildPropList(
	__int64 sendTable,
	__int64 oldState,
	__int64 newState,
	__int64 baselineState,
	int maxProp,
	__int64 output)
{
	const __int64 previousTable = s_R1OBuildPropListCurrentTable;
	s_R1OBuildPropListCurrentTable = sendTable;
	const __int64 result = R1OBuildPropListOriginal
		? R1OBuildPropListOriginal(sendTable, oldState, newState, baselineState, maxProp, output)
		: 0;
	s_R1OBuildPropListCurrentTable = previousTable;
	return result;
}

static int __fastcall R1OBuildChangedPropList(
	__int64 sendTable,
	__int64 oldData,
	int oldBits,
	__int64 newData,
	int newBits,
	unsigned int* changedProps,
	__int64 profileData,
	int maxProp,
	int objectId)
{
	const __int64 previousTable = s_R1OBuildPropListCurrentTable;
	const int previousObjectId = s_R1OBuildChangedPropListCurrentObjectId;
	s_R1OBuildPropListCurrentTable = sendTable;
	s_R1OBuildChangedPropListCurrentObjectId = objectId;
	const int result = R1OBuildChangedPropListOriginal
		? R1OBuildChangedPropListOriginal(
			sendTable,
			oldData,
			oldBits,
			newData,
			newBits,
			changedProps,
			profileData,
			maxProp,
			objectId)
		: 0;
	s_R1OBuildChangedPropListCurrentObjectId = previousObjectId;
	s_R1OBuildPropListCurrentTable = previousTable;

	const char* tableName = R1OSendTableName(sendTable);
	if (IsR1ODedicatedServer()
		&& s_R1ODeltaCalculatorPlayerLogBudget > 0
		&& tableName
		&& _stricmp(R1OSafeCString(tableName), "DT_HL2_Player") == 0) {
		bool hasCellX = false;
		bool hasCellY = false;
		bool hasCellZ = false;
		bool hasCellBits = false;
		for (int i = 0; changedProps && i < result; ++i) {
			if (!IsReadableRange(changedProps + i, sizeof(*changedProps)))
				break;
			const __int64 prop = R1OSendTableFlatProp(sendTable, static_cast<int>(changedProps[i]));
			const char* propName = R1OSendPropName(prop);
			hasCellX = hasCellX || _stricmp(R1OSafeCString(propName), "m_cellX") == 0;
			hasCellY = hasCellY || _stricmp(R1OSafeCString(propName), "m_cellY") == 0;
			hasCellZ = hasCellZ || _stricmp(R1OSafeCString(propName), "m_cellZ") == 0;
			hasCellBits = hasCellBits || _stricmp(R1OSafeCString(propName), "m_cellbits") == 0;
		}

		char buffer[768];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O changed-prop build table=\"%s\" objectId=%d old=%p/%d new=%p/%d maxProp=%d result=%d cells={bits:%d,x:%d,y:%d,z:%d}\n",
			R1OSafeCString(tableName),
			objectId,
			reinterpret_cast<void*>(oldData),
			oldBits,
			reinterpret_cast<void*>(newData),
			newBits,
			maxProp,
			result,
			hasCellBits ? 1 : 0,
			hasCellX ? 1 : 0,
			hasCellY ? 1 : 0,
			hasCellZ ? 1 : 0);
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}

	return result;
}

static bool R1OChangedPropListContains(
	__int64 sendTable,
	const unsigned int* changedProps,
	int changedPropCount,
	const char* targetName)
{
	if (!sendTable || !changedProps || changedPropCount <= 0 || !targetName)
		return false;

	for (int i = 0; i < changedPropCount; ++i) {
		if (!IsReadableRange(changedProps + i, sizeof(*changedProps)))
			break;
		const __int64 prop = R1OSendTableFlatProp(sendTable, static_cast<int>(changedProps[i]));
		if (_stricmp(R1OSafeCString(R1OSendPropName(prop)), targetName) == 0)
			return true;
	}
	return false;
}

static __int64 R1OFindCullProxyNode(__int64 node, unsigned short proxyIndex, int depth = 0)
{
	if (!node || depth > 64 || !IsReadableRange(reinterpret_cast<const void*>(node), 56))
		return 0;

	if (*reinterpret_cast<unsigned short*>(node + 54) == proxyIndex)
		return node;

	const __int64 children = *reinterpret_cast<__int64*>(node);
	const int childCount = *reinterpret_cast<int*>(node + 24);
	if (!children || childCount < 0 || childCount > 1024
		|| !IsReadableRange(reinterpret_cast<const void*>(children), sizeof(__int64) * static_cast<size_t>(childCount)))
		return 0;

	for (int i = 0; i < childCount; ++i) {
		const __int64 found = R1OFindCullProxyNode(
			*reinterpret_cast<__int64*>(children + sizeof(__int64) * static_cast<size_t>(i)),
			proxyIndex,
			depth + 1);
		if (found)
			return found;
	}
	return 0;
}

static bool R1OChangedPropListContainsIndex(
	const unsigned int* changedProps,
	int changedPropCount,
	unsigned int targetIndex)
{
	if (!changedProps || changedPropCount <= 0)
		return false;

	for (int i = 0; i < changedPropCount; ++i) {
		if (!IsReadableRange(changedProps + i, sizeof(*changedProps)))
			break;
		if (changedProps[i] == targetIndex)
			return true;
	}
	return false;
}

static void R1OLogCullProxyNodes(
	__int64 node,
	unsigned short proxyIndex,
	__int64 precalc,
	int depth = 0)
{
	if (!node || depth > 64 || !IsReadableRange(reinterpret_cast<const void*>(node), 56))
		return;

	if (*reinterpret_cast<unsigned short*>(node + 54) == proxyIndex) {
		const short proxyPropIndex = *reinterpret_cast<short*>(node + 32);
		const __int64 proxyProps = precalc
			&& IsReadableRange(reinterpret_cast<const void*>(precalc + 144), sizeof(__int64))
			? *reinterpret_cast<__int64*>(precalc + 144)
			: 0;
		const __int64 proxyPropSlot = proxyProps && proxyPropIndex >= 0
			? proxyProps + sizeof(__int64) * static_cast<size_t>(proxyPropIndex)
			: 0;
		const __int64 proxyProp = proxyPropSlot
			&& IsReadableRange(reinterpret_cast<const void*>(proxyPropSlot), sizeof(__int64))
			? *reinterpret_cast<__int64*>(proxyPropSlot)
			: 0;
		const __int64 proxyDataTable = proxyProp
			&& IsReadableRange(reinterpret_cast<const void*>(proxyProp + 96), sizeof(__int64))
			? *reinterpret_cast<__int64*>(proxyProp + 96)
			: 0;
		const __int64 proxyFunction = proxyProp
			&& IsReadableRange(reinterpret_cast<const void*>(proxyProp + 104), sizeof(__int64))
			? *reinterpret_cast<__int64*>(proxyProp + 104)
			: 0;
		const __int64 nodeTable = *reinterpret_cast<__int64*>(node + 40);
		char buffer[1536];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O prop cull proxy-node index=%u depth=%d node=%p children=%p/%d raw={q8:%p,q16:%p,q32:%p,q40:%p,w48:%u,w50:%u,w52:%u,w54:%u} nodeTable=%p/\"%s\" proxyPropIndex=%d proxyProp=%p/\"%s\" proxyDataTable=%p/\"%s\" proxyFn=%p\n",
			static_cast<unsigned int>(proxyIndex),
			depth,
			reinterpret_cast<void*>(node),
			reinterpret_cast<void*>(*reinterpret_cast<__int64*>(node)),
			*reinterpret_cast<int*>(node + 24),
			reinterpret_cast<void*>(*reinterpret_cast<__int64*>(node + 8)),
			reinterpret_cast<void*>(*reinterpret_cast<__int64*>(node + 16)),
			reinterpret_cast<void*>(*reinterpret_cast<__int64*>(node + 32)),
			reinterpret_cast<void*>(*reinterpret_cast<__int64*>(node + 40)),
			static_cast<unsigned int>(*reinterpret_cast<unsigned short*>(node + 48)),
			static_cast<unsigned int>(*reinterpret_cast<unsigned short*>(node + 50)),
			static_cast<unsigned int>(*reinterpret_cast<unsigned short*>(node + 52)),
			static_cast<unsigned int>(*reinterpret_cast<unsigned short*>(node + 54)),
			reinterpret_cast<void*>(nodeTable),
			R1OSendTableName(nodeTable),
			static_cast<int>(proxyPropIndex),
			reinterpret_cast<void*>(proxyProp),
			R1OSafeCString(R1OSendPropName(proxyProp)),
			reinterpret_cast<void*>(proxyDataTable),
			R1OSendTableName(proxyDataTable),
			reinterpret_cast<void*>(proxyFunction));
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}

	const __int64 children = *reinterpret_cast<__int64*>(node);
	const int childCount = *reinterpret_cast<int*>(node + 24);
	if (!children || childCount < 0 || childCount > 1024
		|| !IsReadableRange(reinterpret_cast<const void*>(children), sizeof(__int64) * static_cast<size_t>(childCount)))
		return;

	for (int i = 0; i < childCount; ++i) {
		R1OLogCullProxyNodes(
			*reinterpret_cast<__int64*>(children + sizeof(__int64) * static_cast<size_t>(i)),
			proxyIndex,
			precalc,
			depth + 1);
	}
}

static __int64 __fastcall R1OCullChangedProps(
	__int64 cullStack,
	unsigned int* changedProps,
	int changedPropCount,
	unsigned int* outputProps)
{
	const __int64 sendTable = s_R1OPropCullCurrentTable;
	const char* tableName = R1OSendTableName(sendTable);
	const bool tracePlayer = IsR1ODedicatedServer()
		&& s_R1OPropCullPlayerLogBudget > 0
		&& sendTable
		&& _stricmp(R1OSafeCString(tableName), "DT_HL2_Player") == 0;

	const bool inputCellX = tracePlayer && R1OChangedPropListContains(sendTable, changedProps, changedPropCount, "m_cellX");
	const bool inputCellY = tracePlayer && R1OChangedPropListContains(sendTable, changedProps, changedPropCount, "m_cellY");
	const bool inputCellZ = tracePlayer && R1OChangedPropListContains(sendTable, changedProps, changedPropCount, "m_cellZ");

	const __int64 result = R1OCullChangedPropsOriginal
		? R1OCullChangedPropsOriginal(cullStack, changedProps, changedPropCount, outputProps)
		: 0;

	if (tracePlayer && (inputCellX || inputCellY || inputCellZ)) {
		--s_R1OPropCullPlayerLogBudget;
		const int outputCount = cullStack
			&& IsReadableRange(reinterpret_cast<const void*>(cullStack + 620), sizeof(int))
			? *reinterpret_cast<int*>(cullStack + 620)
			: -1;
		const bool outputCellX = R1OChangedPropListContains(sendTable, outputProps, outputCount, "m_cellX");
		const bool outputCellY = R1OChangedPropListContains(sendTable, outputProps, outputCount, "m_cellY");
		const bool outputCellZ = R1OChangedPropListContains(sendTable, outputProps, outputCount, "m_cellZ");

		const __int64 precalc = cullStack
			&& IsReadableRange(reinterpret_cast<const void*>(cullStack + 8), sizeof(__int64))
			? *reinterpret_cast<__int64*>(cullStack + 8)
			: 0;
		const __int64 propToProxy = precalc
			&& IsReadableRange(reinterpret_cast<const void*>(precalc + 112), sizeof(__int64))
			? *reinterpret_cast<__int64*>(precalc + 112)
			: 0;

		int cellIndices[3] = {-1, -1, -1};
		R1OFindFlatSendProp(sendTable, "m_cellX", 0, &cellIndices[0]);
		R1OFindFlatSendProp(sendTable, "m_cellY", 0, &cellIndices[1]);
		R1OFindFlatSendProp(sendTable, "m_cellZ", 0, &cellIndices[2]);
		unsigned int proxyIndices[3] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
		void* proxyRecipients[3] = {};
		for (int i = 0; i < 3; ++i) {
			if (cellIndices[i] < 0
				|| !propToProxy
				|| !IsReadableRange(reinterpret_cast<const void*>(propToProxy + cellIndices[i]), 1))
				continue;
			proxyIndices[i] = *reinterpret_cast<unsigned char*>(propToProxy + cellIndices[i]);
			const __int64 recipientSlot = precalc + 16 + 8LL * proxyIndices[i];
			if (IsReadableRange(reinterpret_cast<const void*>(recipientSlot), sizeof(__int64)))
				proxyRecipients[i] = reinterpret_cast<void*>(*reinterpret_cast<__int64*>(recipientSlot));
		}

		const __int64 proxyRoot = precalc
			&& IsReadableRange(reinterpret_cast<const void*>(precalc + 176), 56)
			? precalc + 176
			: 0;
		const __int64 cellProxyNode = proxyIndices[0] <= 0xFFFFu
			? R1OFindCullProxyNode(proxyRoot, static_cast<unsigned short>(proxyIndices[0]))
			: 0;
		if (proxyIndices[0] <= 0xFFFFu) {
			const int flatCount = R1OSendTableFlatCount(sendTable);
			for (int propIndex = 0; propIndex < flatCount && propIndex < 4096; ++propIndex) {
				if (!propToProxy
					|| !IsReadableRange(reinterpret_cast<const void*>(propToProxy + propIndex), 1))
					break;

				const unsigned int proxyIndex =
					*reinterpret_cast<unsigned char*>(propToProxy + propIndex);
				const __int64 prop = R1OSendTableFlatProp(sendTable, propIndex);
				const char* propName = R1OSafeCString(R1OSendPropName(prop), "<bad-prop>");
				const bool isCell =
					_stricmp(propName, "m_cellX") == 0
					|| _stricmp(propName, "m_cellY") == 0
					|| _stricmp(propName, "m_cellZ") == 0
					|| _stricmp(propName, "m_cellbits") == 0;
				if (!isCell && proxyIndex != proxyIndices[0])
					continue;

				char detail[1024];
				_snprintf_s(
					detail,
					sizeof(detail),
					_TRUNCATE,
					"R1Delta: R1O prop cull flat index=%d name=\"%s\" prop=%p offset=%d proxy=%u input=%d output=%d\n",
					propIndex,
					propName,
					reinterpret_cast<void*>(prop),
					R1OSendPropStorageOffset(prop),
					proxyIndex,
					R1OChangedPropListContainsIndex(
						changedProps,
						changedPropCount,
						static_cast<unsigned int>(propIndex)) ? 1 : 0,
					R1OChangedPropListContainsIndex(
						outputProps,
						outputCount,
						static_cast<unsigned int>(propIndex)) ? 1 : 0);
				OutputDebugStringA(detail);
				Warning("%s", detail);
			}
			R1OLogCullProxyNodes(
				proxyRoot,
				static_cast<unsigned short>(proxyIndices[0]),
				precalc);
		}
		const unsigned int recipientProxyIndex = cellProxyNode
			&& IsReadableRange(reinterpret_cast<const void*>(cellProxyNode + 52), sizeof(unsigned short))
			? *reinterpret_cast<unsigned short*>(cellProxyNode + 52)
			: 0xFFFFFFFFu;
		const unsigned int firstProxyProp = cellProxyNode
			&& IsReadableRange(reinterpret_cast<const void*>(cellProxyNode + 48), sizeof(unsigned short))
			? *reinterpret_cast<unsigned short*>(cellProxyNode + 48)
			: 0xFFFFFFFFu;
		const unsigned int proxyPropCount = cellProxyNode
			&& IsReadableRange(reinterpret_cast<const void*>(cellProxyNode + 50), sizeof(unsigned short))
			? *reinterpret_cast<unsigned short*>(cellProxyNode + 50)
			: 0xFFFFFFFFu;
		const int clientSlot = cullStack
			&& IsReadableRange(reinterpret_cast<const void*>(cullStack + 568), sizeof(int))
			? *reinterpret_cast<int*>(cullStack + 568)
			: -1;
		const __int64 oldRecipientMasks = cullStack
			&& IsReadableRange(reinterpret_cast<const void*>(cullStack + 576), sizeof(__int64))
			? *reinterpret_cast<__int64*>(cullStack + 576)
			: 0;
		const __int64 newRecipientMasks = cullStack
			&& IsReadableRange(reinterpret_cast<const void*>(cullStack + 592), sizeof(__int64))
			? *reinterpret_cast<__int64*>(cullStack + 592)
			: 0;
		bool oldRecipient = false;
		bool newRecipient = false;
		unsigned int oldRecipientMask = 0;
		unsigned int newRecipientMask = 0;
		if (recipientProxyIndex < 255 && clientSlot >= 0) {
			const unsigned int mask = 1u << (clientSlot & 31);
			const __int64 oldSlot = oldRecipientMasks + sizeof(unsigned int) * recipientProxyIndex;
			const __int64 newSlot = newRecipientMasks + sizeof(unsigned int) * recipientProxyIndex;
			if (oldRecipientMasks
				&& IsReadableRange(reinterpret_cast<const void*>(oldSlot), sizeof(unsigned int))) {
				oldRecipientMask = *reinterpret_cast<unsigned int*>(oldSlot);
				oldRecipient = (oldRecipientMask & mask) != 0;
			}
			if (newRecipientMasks
				&& IsReadableRange(reinterpret_cast<const void*>(newSlot), sizeof(unsigned int))) {
				newRecipientMask = *reinterpret_cast<unsigned int*>(newSlot);
				newRecipient = (newRecipientMask & mask) != 0;
			}
		}
		else if (recipientProxyIndex == 255) {
			oldRecipient = true;
			newRecipient = true;
		}

		char buffer[1024];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O prop cull table=\"%s\" input=%d cells={x:%d,y:%d,z:%d} output=%d cells={x:%d,y:%d,z:%d} proxy={x:%u/%p,y:%u/%p,z:%u/%p} node=%p props=%u+%u recipientProxy=%u clientSlot=%d oldRecipient=%d/0x%08X newRecipient=%d/0x%08X masks=%p/%p stack=%p precalc=%p result=%lld budget=%d\n",
			R1OSafeCString(tableName),
			changedPropCount,
			inputCellX ? 1 : 0,
			inputCellY ? 1 : 0,
			inputCellZ ? 1 : 0,
			outputCount,
			outputCellX ? 1 : 0,
			outputCellY ? 1 : 0,
			outputCellZ ? 1 : 0,
			proxyIndices[0],
			proxyRecipients[0],
			proxyIndices[1],
			proxyRecipients[1],
			proxyIndices[2],
			proxyRecipients[2],
			reinterpret_cast<void*>(cellProxyNode),
			firstProxyProp,
			proxyPropCount,
			recipientProxyIndex,
			clientSlot,
			oldRecipient ? 1 : 0,
			oldRecipientMask,
			newRecipient ? 1 : 0,
			newRecipientMask,
			reinterpret_cast<void*>(oldRecipientMasks),
			reinterpret_cast<void*>(newRecipientMasks),
			reinterpret_cast<void*>(cullStack),
			reinterpret_cast<void*>(precalc),
			static_cast<long long>(result),
			s_R1OPropCullPlayerLogBudget);
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}

	return result;
}

static __int64 __fastcall R1OSnapshotEntityWrite(
	__int64 context,
	unsigned int* changedProps,
	unsigned int changedPropCount)
{
	__int64 sendTable = 0;
	if (context && IsReadableRange(reinterpret_cast<const void*>(context + 1104), sizeof(__int64))) {
		const __int64 packedEntity = *reinterpret_cast<__int64*>(context + 1104);
		if (packedEntity && IsReadableRange(reinterpret_cast<const void*>(packedEntity), sizeof(__int64))) {
			const __int64 packedEntityInfo = *reinterpret_cast<__int64*>(packedEntity);
			if (packedEntityInfo && IsReadableRange(reinterpret_cast<const void*>(packedEntityInfo + 8), sizeof(__int64)))
				sendTable = *reinterpret_cast<__int64*>(packedEntityInfo + 8);
		}
	}

	const __int64 previousTable = s_R1OPropCullCurrentTable;
	s_R1OPropCullCurrentTable = sendTable;

	unsigned char* cullEnabled = context
		&& IsReadableRange(reinterpret_cast<const void*>(context + 1724), 1)
		? reinterpret_cast<unsigned char*>(context + 1724)
		: nullptr;
	const unsigned char savedCullEnabled = cullEnabled ? *cullEnabled : 0;
	if (cullEnabled && HasEngineCommandLineFlag("-r1o_disable_prop_cull_test"))
		*cullEnabled = 0;

	const __int64 result = R1OSnapshotEntityWriteOriginal
		? R1OSnapshotEntityWriteOriginal(context, changedProps, changedPropCount)
		: 0;

	if (cullEnabled)
		*cullEnabled = savedCullEnabled;
	s_R1OPropCullCurrentTable = previousTable;
	return result;
}

static int __fastcall R1ODeltaPropIndexWrite(__int64 writer, int propIndex)
{
	const __int64 table = s_R1OSendTableEncodeCurrentTable;
	const char* tableName = R1OSendTableName(table);
	const bool trace = IsR1ODedicatedServer()
		&& s_R1ODeltaPropIndexWriteLogBudget > 0
		&& table
		&& R1OIsDeltaTraceTable(tableName);
	const bool tracePhysicsWindow = IsR1ODedicatedServer()
		&& s_R1ODeltaPropIndexWritePhysicsWindowBudget > 0
		&& table
		&& _stricmp(R1OSafeCString(tableName), "DT_PhysicsProp") == 0
		&& propIndex >= 80
		&& propIndex <= 100;
	const bool tracePlayerWindow = IsR1ODedicatedServer()
		&& s_R1ODeltaPropIndexWritePlayerWindowBudget > 0
		&& table
		&& _stricmp(R1OSafeCString(tableName), "DT_HL2_Player") == 0
		&& propIndex >= 0
		&& propIndex <= 64;
	const bool traceUnknownSnapshot = IsR1ODedicatedServer()
		&& !table
		&& s_R1ODeltaPropIndexWriteUnknownLogBudget > 0;
	const int beforeBit = trace ? R1ODeltaWriterBit(writer) : -1;
	const int beforeWindowBit = tracePhysicsWindow ? R1ODeltaWriterBit(writer) : -1;
	const int beforeUnknownBit = traceUnknownSnapshot ? R1ODeltaWriterBit(writer) : -1;
	const int beforePlayerBit = tracePlayerWindow ? R1ODeltaWriterBit(writer) : -1;
	const __int64 bitBuffer = (trace || tracePhysicsWindow || tracePlayerWindow || traceUnknownSnapshot) ? R1ODeltaWriterBitBuffer(writer) : 0;
	const int previousIndex = (trace || tracePhysicsWindow || tracePlayerWindow || traceUnknownSnapshot) && writer && IsReadableRange(reinterpret_cast<const void*>(writer + 16), sizeof(int))
		? *reinterpret_cast<int*>(writer + 16)
		: -1;

	const int result = R1ODeltaPropIndexWriteOriginal
		? R1ODeltaPropIndexWriteOriginal(writer, propIndex)
		: 0;

	if (trace) {
		--s_R1ODeltaPropIndexWriteLogBudget;
		const int afterBit = R1ODeltaWriterBit(writer);
		char bits[128];
		R1ODumpWriteBits(bits, sizeof(bits), bitBuffer, beforeBit, afterBit);
		const int flatCount = R1OSendTableFlatCount(table);
		const __int64 prop = propIndex >= 0 && propIndex != 0x7fffffff
			? R1OSendTableFlatProp(table, propIndex)
			: 0;
		const int relativeBeforeBit = s_R1OSendTableEncodeCurrentStartBit >= 0 && beforeBit >= 0
			? beforeBit - s_R1OSendTableEncodeCurrentStartBit
			: -1;
		const int relativeAfterBit = s_R1OSendTableEncodeCurrentStartBit >= 0 && afterBit >= 0
			? afterBit - s_R1OSendTableEncodeCurrentStartBit
			: -1;
		char buffer[1408];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O delta prop-write table=\"%s\" objectId=%d object=%p encodeBitbuf=%p encodeStart=%d relBits=%d->%d delta=%d old=%p writer=%p bitbuf=%p propIndex=%d prev=%d flatCount=%d prop=%p name=\"%s\" type=%d numBits=%d flags=0x%x offset=%d bits=%d->%d wire=\"%s\" result=%d budget=%d\n",
			tableName,
			s_R1OSendTableEncodeCurrentObjectId,
			reinterpret_cast<void*>(s_R1OSendTableEncodeCurrentObject),
			reinterpret_cast<void*>(s_R1OSendTableEncodeCurrentBitBuffer),
			s_R1OSendTableEncodeCurrentStartBit,
			relativeBeforeBit,
			relativeAfterBit,
			static_cast<int>(s_R1OSendTableEncodeCurrentDelta),
			reinterpret_cast<void*>(s_R1OSendTableEncodeCurrentOldState),
			reinterpret_cast<void*>(writer),
			reinterpret_cast<void*>(bitBuffer),
			propIndex,
			previousIndex,
			flatCount,
			reinterpret_cast<void*>(prop),
			prop ? R1OSendPropName(prop) : (propIndex == 0x7fffffff ? "<sentinel>" : "<out-of-range>"),
			prop ? R1OSendPropType(prop) : -1,
			prop ? R1OSendPropNumBits(prop) : -1,
			prop ? R1OSendPropFlags(prop) : -1,
			prop ? R1OSendPropOffset(prop) : -1,
			beforeBit,
			afterBit,
			bits,
			result,
			s_R1ODeltaPropIndexWriteLogBudget);
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}

	if (traceUnknownSnapshot && beforeUnknownBit >= 8000 && beforeUnknownBit <= 13000) {
		--s_R1ODeltaPropIndexWriteUnknownLogBudget;
		const int afterBit = R1ODeltaWriterBit(writer);
		char bits[128];
		R1ODumpWriteBits(bits, sizeof(bits), bitBuffer, beforeUnknownBit, afterBit);
		const int dataBits = bitBuffer && IsReadableRange(reinterpret_cast<const void*>(bitBuffer + 12), sizeof(int))
			? *reinterpret_cast<int*>(bitBuffer + 12)
			: -1;
		char buffer[768];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O delta prop-write unknown-context writer=%p bitbuf=%p dataBits=%d bits=%d->%d propIndex=%d prev=%d wire=\"%s\" result=%d ret=%p budget=%d\n",
			reinterpret_cast<void*>(writer),
			reinterpret_cast<void*>(bitBuffer),
			dataBits,
			beforeUnknownBit,
			afterBit,
			propIndex,
			previousIndex,
			bits,
			result,
			_ReturnAddress(),
			s_R1ODeltaPropIndexWriteUnknownLogBudget);
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}

	if (tracePhysicsWindow || tracePlayerWindow) {
		int& budget = tracePlayerWindow ? s_R1ODeltaPropIndexWritePlayerWindowBudget : s_R1ODeltaPropIndexWritePhysicsWindowBudget;
		--budget;
		const int afterBit = R1ODeltaWriterBit(writer);
		const int beforeSpecialBit = tracePlayerWindow ? beforePlayerBit : beforeWindowBit;
		char bits[128];
		R1ODumpWriteBits(bits, sizeof(bits), bitBuffer, beforeSpecialBit, afterBit);
		const int flatCount = R1OSendTableFlatCount(table);
		const __int64 prop = R1OSendTableFlatProp(table, propIndex);
		const int relativeBeforeBit = s_R1OSendTableEncodeCurrentStartBit >= 0 && beforeSpecialBit >= 0
			? beforeSpecialBit - s_R1OSendTableEncodeCurrentStartBit
			: -1;
		const int relativeAfterBit = s_R1OSendTableEncodeCurrentStartBit >= 0 && afterBit >= 0
			? afterBit - s_R1OSendTableEncodeCurrentStartBit
			: -1;
		char buffer[1408];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O delta prop-write %s objectId=%d object=%p encodeBitbuf=%p encodeStart=%d relBits=%d->%d delta=%d old=%p writer=%p bitbuf=%p propIndex=%d prev=%d flatCount=%d prop=%p name=\"%s\" type=%d numBits=%d flags=0x%x offset=%d bits=%d->%d wire=\"%s\" result=%d budget=%d\n",
			tracePlayerWindow ? "player-window" : "physics-window",
			s_R1OSendTableEncodeCurrentObjectId,
			reinterpret_cast<void*>(s_R1OSendTableEncodeCurrentObject),
			reinterpret_cast<void*>(s_R1OSendTableEncodeCurrentBitBuffer),
			s_R1OSendTableEncodeCurrentStartBit,
			relativeBeforeBit,
			relativeAfterBit,
			static_cast<int>(s_R1OSendTableEncodeCurrentDelta),
			reinterpret_cast<void*>(s_R1OSendTableEncodeCurrentOldState),
			reinterpret_cast<void*>(writer),
			reinterpret_cast<void*>(bitBuffer),
			propIndex,
			previousIndex,
			flatCount,
			reinterpret_cast<void*>(prop),
			R1OSendPropName(prop),
			R1OSendPropType(prop),
			R1OSendPropNumBits(prop),
			R1OSendPropFlags(prop),
			R1OSendPropOffset(prop),
			beforeSpecialBit,
			afterBit,
			bits,
			result,
			budget);
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}

	return result;
}

static void R1OLogSendTableFlatProps(const char* phase, __int64 sendTable)
{
	if (!IsR1ODedicatedServer())
		return;

	if (!sendTable || !IsReadableRange(reinterpret_cast<const void*>(sendTable), 0x20))
		return;

	const char* tableName = *reinterpret_cast<const char**>(sendTable + 0x10);
	const int directCount = IsReadableRange(reinterpret_cast<const void*>(sendTable + 0x8), sizeof(int))
		? *reinterpret_cast<int*>(sendTable + 0x8)
		: -1;
	const __int64 precalc = IsReadableRange(reinterpret_cast<const void*>(sendTable + 0x18), sizeof(__int64))
		? *reinterpret_cast<__int64*>(sendTable + 0x18)
		: 0;
	const int flatCount = precalc && IsReadableRange(reinterpret_cast<const void*>(precalc + 0x68), sizeof(int))
		? *reinterpret_cast<int*>(precalc + 0x68)
		: -1;
	const __int64 flatProps = precalc && IsReadableRange(reinterpret_cast<const void*>(precalc + 0x50), sizeof(__int64))
		? *reinterpret_cast<__int64*>(precalc + 0x50)
		: 0;

	const bool interestingWorld = tableName
		&& (_stricmp(R1OSafeCString(tableName), "DT_WORLD") == 0 || _stricmp(R1OSafeCString(tableName), "DT_World") == 0);
	const bool interestingTeam = tableName
		&& _stricmp(R1OSafeCString(tableName), "DT_Team") == 0;
	const bool interestingTrace = tableName
		&& R1OIsDeltaTraceTable(tableName);
	const bool interestingPlayer = tableName
		&& _stricmp(R1OSafeCString(tableName), "DT_HL2_Player") == 0;
	const bool interestingBaseline = interestingTrace || interestingPlayer;
	if (!interestingBaseline)
		return;
	const bool forcePlayerFlatLog = AreR1OFakeDediVerboseLogsEnabled() && interestingPlayer && !s_R1OLoggedHL2PlayerFlatProps;
	if (s_R1OSendTableEncodeLogBudget <= 0 && !forcePlayerFlatLog)
		return;
	if (forcePlayerFlatLog)
		s_R1OLoggedHL2PlayerFlatProps = true;

	if (!forcePlayerFlatLog && s_R1OSendTableEncodeLogBudget > 0)
		--s_R1OSendTableEncodeLogBudget;
	char header[512];
	_snprintf_s(
		header,
		sizeof(header),
		_TRUNCATE,
		"R1Delta: R1O SendTable_Encode %s table=%p name=\"%s\" direct=%d precalc=%p flatCount=%d flatProps=%p budget=%d\n",
		phase,
		reinterpret_cast<void*>(sendTable),
		R1OSafeCString(tableName, "<null>"),
		directCount,
		reinterpret_cast<void*>(precalc),
		flatCount,
		reinterpret_cast<void*>(flatProps),
		s_R1OSendTableEncodeLogBudget);
	OutputDebugStringA(header);
	Warning("%s", header);

	if (!flatProps || flatCount <= 0 || !IsReadableRange(reinterpret_cast<const void*>(flatProps), sizeof(__int64) * static_cast<size_t>(flatCount)))
		return;

	const int first = interestingTrace || interestingPlayer ? 0 : 0;
	const int last = interestingTrace || interestingPlayer ? (flatCount - 1) : (flatCount - 1);
	int forcedLineBudget = forcePlayerFlatLog ? 96 : 0;
	for (int base = first; base <= last && (forcePlayerFlatLog ? forcedLineBudget > 0 : s_R1OSendTableEncodeLogBudget > 0); base += 4) {
		if (forcePlayerFlatLog)
			--forcedLineBudget;
		else if (s_R1OSendTableEncodeLogBudget > 0)
			--s_R1OSendTableEncodeLogBudget;
		char line[1024];
		size_t used = 0;
		used += _snprintf_s(
			line + used,
			sizeof(line) - used,
			_TRUNCATE,
			"R1Delta: R1O SendTable_Encode flat table=\"%s\"",
			R1OSafeCString(tableName, "<null>"));

		for (int i = 0; i < 4 && base + i <= last; ++i) {
			const int index = base + i;
			__int64 prop = 0;
			if (IsReadableRange(reinterpret_cast<const void*>(flatProps + sizeof(__int64) * static_cast<size_t>(index)), sizeof(__int64)))
				prop = *reinterpret_cast<__int64*>(flatProps + sizeof(__int64) * static_cast<size_t>(index));
			used += _snprintf_s(
				line + used,
				sizeof(line) - used,
				_TRUNCATE,
				" [%d]=%p/%s/t%d/f0x%x/o%d",
				index,
				reinterpret_cast<void*>(prop),
				R1OSendPropName(prop),
				R1OSendPropType(prop),
				R1OSendPropFlags(prop),
				R1OSendPropOffset(prop));
			if (used >= sizeof(line))
				break;
		}

		_snprintf_s(line + used, sizeof(line) - used, _TRUNCATE, "\n");
		OutputDebugStringA(line);
		Warning("%s", line);
	}
}

static __int64 __fastcall R1OEncodeProp(__int64 encodeInfo, int propIndex)
{
	const __int64 table = s_R1OSendTableEncodeCurrentTable;
	const char* tableName = R1OSendTableName(table);
	const __int64 prop = R1OSendTableFlatProp(table, propIndex);
	const char* propName = R1OSendPropName(prop);
	const bool tracePlayerCell = IsR1ODedicatedServer()
		&& s_R1OCellEncodeTraceLogBudget > 0
		&& s_R1OSendTableEncodeCurrentObjectId == 1
		&& tableName
		&& _stricmp(R1OSafeCString(tableName), "DT_HL2_Player") == 0
		&& propName
		&& (_stricmp(propName, "m_cellX") == 0
			|| _stricmp(propName, "m_cellY") == 0
			|| _stricmp(propName, "m_cellZ") == 0
			|| _stricmp(propName, "m_cellbits") == 0
			|| _stricmp(propName, "m_hWhooshTargetEntity") == 0
			|| _stricmp(propName, "m_iWhooshCameraMode") == 0
			|| _stricmp(propName, "m_iWhooshCameraParity") == 0);
	const int beforeBit = tracePlayerCell && s_R1OSendTableEncodeCurrentBitBuffer && IsReadableRange(reinterpret_cast<void*>(s_R1OSendTableEncodeCurrentBitBuffer + 16), sizeof(int))
		? *reinterpret_cast<int*>(s_R1OSendTableEncodeCurrentBitBuffer + 16)
		: -1;
	const int storageOffset = tracePlayerCell ? R1OSendPropStorageOffset(prop) : -1;
	const int objectValue = tracePlayerCell ? R1OSafeReadIntField(s_R1OSendTableEncodeCurrentObject, storageOffset) : 0;

	if (IsR1ODedicatedServer() && s_R1OEncodePropLogBudget > 0) {
		if (tableName && _stricmp(R1OSafeCString(tableName), "DT_PhysicsProp") == 0 && propIndex == 84) {
			--s_R1OEncodePropLogBudget;
			char buffer[512];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: R1O encoding DT_PhysicsProp propIndex=84 encodeInfo=%p objectId=%d table=%p budget=%d\n",
				reinterpret_cast<void*>(encodeInfo),
				s_R1OSendTableEncodeCurrentObjectId,
				reinterpret_cast<void*>(table),
				s_R1OEncodePropLogBudget);
			OutputDebugStringA(buffer);
		}
	}

	const __int64 result = R1OEncodePropOriginal ? R1OEncodePropOriginal(encodeInfo, propIndex) : 0;

	if (tracePlayerCell) {
		--s_R1OCellEncodeTraceLogBudget;
		const int afterBit = s_R1OSendTableEncodeCurrentBitBuffer && IsReadableRange(reinterpret_cast<void*>(s_R1OSendTableEncodeCurrentBitBuffer + 16), sizeof(int))
			? *reinterpret_cast<int*>(s_R1OSendTableEncodeCurrentBitBuffer + 16)
			: -1;
		char bits[128];
		R1ODumpWriteBits(bits, sizeof(bits), s_R1OSendTableEncodeCurrentBitBuffer, beforeBit, afterBit);
		bool bitsOk = false;
		const unsigned int payloadValue = R1OReadWriteBitsUnsigned(s_R1OSendTableEncodeCurrentBitBuffer, beforeBit, afterBit, &bitsOk);
		char buffer[1024];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: cell-encode r1o table=\"%s\" objectId=%d object=%p delta=%d old=%p propIndex=%d prop=%p name=\"%s\" type=%d numBits=%d flags=0x%x offset=%d storage=%d objectValue=%d bits=%d->%d payload=\"%s\" payloadU=%u payloadOk=%d result=%lld budget=%d\n",
			R1OSafeCString(tableName, "<null>"),
			s_R1OSendTableEncodeCurrentObjectId,
			reinterpret_cast<void*>(s_R1OSendTableEncodeCurrentObject),
			static_cast<int>(s_R1OSendTableEncodeCurrentDelta),
			reinterpret_cast<void*>(s_R1OSendTableEncodeCurrentOldState),
			propIndex,
			reinterpret_cast<void*>(prop),
			propName,
			R1OSendPropType(prop),
			R1OSendPropNumBits(prop),
			R1OSendPropFlags(prop),
			R1OSendPropOffset(prop),
			storageOffset,
			objectValue,
			beforeBit,
			afterBit,
			bits,
			payloadValue,
			bitsOk ? 1 : 0,
			static_cast<long long>(result),
			s_R1OCellEncodeTraceLogBudget);
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}

	return result;
}

static bool __fastcall R1OSendTableEncode(__int64 sendTable, __int64 object, __int64 bitBuffer, int objectId, __int64 recipients, char delta, __int64 oldState, __int64 fieldBitCounts)
{
	const int startBit = bitBuffer && IsReadableRange(reinterpret_cast<void*>(bitBuffer + 16), sizeof(int))
		? *reinterpret_cast<int*>(bitBuffer + 16)
		: -1;
	R1OLogSendTableFlatProps("enter", sendTable);

	const __int64 previousEncodeTable = s_R1OSendTableEncodeCurrentTable;
	const __int64 previousEncodeObject = s_R1OSendTableEncodeCurrentObject;
	const __int64 previousEncodeBitBuffer = s_R1OSendTableEncodeCurrentBitBuffer;
	const __int64 previousEncodeOldState = s_R1OSendTableEncodeCurrentOldState;
	const int previousEncodeObjectId = s_R1OSendTableEncodeCurrentObjectId;
	const int previousEncodeStartBit = s_R1OSendTableEncodeCurrentStartBit;
	const char previousEncodeDelta = s_R1OSendTableEncodeCurrentDelta;
	s_R1OSendTableEncodeCurrentTable = sendTable;
	s_R1OSendTableEncodeCurrentObject = object;
	s_R1OSendTableEncodeCurrentBitBuffer = bitBuffer;
	s_R1OSendTableEncodeCurrentOldState = oldState;
	s_R1OSendTableEncodeCurrentObjectId = objectId;
	s_R1OSendTableEncodeCurrentStartBit = startBit;
	s_R1OSendTableEncodeCurrentDelta = delta;
	bool result = R1OSendTableEncodeOriginal
		? R1OSendTableEncodeOriginal(sendTable, object, bitBuffer, objectId, recipients, delta, oldState, fieldBitCounts)
		: false;
	s_R1OSendTableEncodeCurrentTable = previousEncodeTable;
	s_R1OSendTableEncodeCurrentObject = previousEncodeObject;
	s_R1OSendTableEncodeCurrentBitBuffer = previousEncodeBitBuffer;
	s_R1OSendTableEncodeCurrentOldState = previousEncodeOldState;
	s_R1OSendTableEncodeCurrentObjectId = previousEncodeObjectId;
	s_R1OSendTableEncodeCurrentStartBit = previousEncodeStartBit;
	s_R1OSendTableEncodeCurrentDelta = previousEncodeDelta;

	R1OLogHL2PlayerNetpropValues(sendTable, object, objectId, delta);

	if (s_R1OSendTableEncodeHeaderLogBudget > 0 && sendTable && IsReadableRange(reinterpret_cast<void*>(sendTable + 0x10), sizeof(const char*))) {
		const char* tableName = *reinterpret_cast<const char**>(sendTable + 0x10);
		const bool interestingTable = tableName
			&& (R1OIsDeltaTraceTable(tableName)
				|| _stricmp(R1OSafeCString(tableName), "DT_HL2_Player") == 0);
		if (interestingTable) {
			--s_R1OSendTableEncodeHeaderLogBudget;
		const int endBit = bitBuffer && IsReadableRange(reinterpret_cast<void*>(bitBuffer + 16), sizeof(int))
			? *reinterpret_cast<int*>(bitBuffer + 16)
			: -1;
		const __int64 precalc = IsReadableRange(reinterpret_cast<void*>(sendTable + 0x18), sizeof(__int64))
			? *reinterpret_cast<__int64*>(sendTable + 0x18)
			: 0;
		const int flatCount = precalc && IsReadableRange(reinterpret_cast<void*>(precalc + 0x68), sizeof(int))
			? *reinterpret_cast<int*>(precalc + 0x68)
			: -1;
		char buffer[512];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O SendTable_Encode header name=\"%s\" objectId=%d delta=%d old=%p result=%d bits=%d->%d flatCount=%d recipients=%p fieldBits=%p budget=%d\n",
			R1OSafeCString(tableName, "<null>"),
			objectId,
			static_cast<int>(delta),
			reinterpret_cast<void*>(oldState),
			static_cast<int>(result),
			startBit,
			endBit,
			flatCount,
			reinterpret_cast<void*>(recipients),
			reinterpret_cast<void*>(fieldBitCounts),
			s_R1OSendTableEncodeHeaderLogBudget);
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
		}
	}

	if (s_R1OSendTableEncodeLogBudget > 0 && sendTable && IsReadableRange(reinterpret_cast<void*>(sendTable + 0x10), sizeof(const char*))) {
		const char* tableName = *reinterpret_cast<const char**>(sendTable + 0x10);
		const int endBit = bitBuffer && IsReadableRange(reinterpret_cast<void*>(bitBuffer + 16), sizeof(int))
			? *reinterpret_cast<int*>(bitBuffer + 16)
			: -1;
		const __int64 precalc = IsReadableRange(reinterpret_cast<void*>(sendTable + 0x18), sizeof(__int64))
			? *reinterpret_cast<__int64*>(sendTable + 0x18)
			: 0;
		const int flatCount = precalc && IsReadableRange(reinterpret_cast<void*>(precalc + 0x68), sizeof(int))
			? *reinterpret_cast<int*>(precalc + 0x68)
			: -1;
		const bool interestingTable = tableName
			&& (R1OIsDeltaTraceTable(tableName)
				|| _stricmp(R1OSafeCString(tableName), "DT_HL2_Player") == 0);
		if (interestingTable) {
			--s_R1OSendTableEncodeLogBudget;
			char tailBits[128];
			const int tailStart = endBit > 96 ? endBit - 96 : startBit;
			R1ODumpWriteBits(tailBits, sizeof(tailBits), bitBuffer, tailStart, endBit);
			char buffer[768];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: R1O SendTable_Encode leave table=%p name=\"%s\" object=%p objectId=%d delta=%d old=%p result=%d bits=%d->%d flatCount=%d recipients=%p fieldBits=%p tailStart=%d tail=\"%s\" budget=%d\n",
				reinterpret_cast<void*>(sendTable),
				R1OSafeCString(tableName, "<null>"),
				reinterpret_cast<void*>(object),
				objectId,
				static_cast<int>(delta),
				reinterpret_cast<void*>(oldState),
				static_cast<int>(result),
				startBit,
				endBit,
				flatCount,
				reinterpret_cast<void*>(recipients),
				reinterpret_cast<void*>(fieldBitCounts),
				tailStart,
				tailBits,
				s_R1OSendTableEncodeLogBudget);
			OutputDebugStringA(buffer);
			Warning("%s", buffer);
		}
	}

	return result;
}

static void __fastcall R1OSVEnsureInstanceBaseline(__int64 unused, int entIndex, __int64 data, unsigned int bytes)
{
	if (s_R1OInstanceBaselineLogBudget > 0 && IsR1ODedicatedServer() && engineR1O) {
		--s_R1OInstanceBaselineLogBudget;
		const uintptr_t engineBase = reinterpret_cast<uintptr_t>(engineR1O);
		const __int64 edicts = IsReadableRange(reinterpret_cast<void*>(engineBase + 0x2998480), sizeof(__int64))
			? *reinterpret_cast<__int64*>(engineBase + 0x2998480)
			: 0;
		__int64 ent = 0;
		__int64 serverClass = 0;
		__int64 sendTable = 0;
		const char* className = "<null>";
		int classId = -1;
		int baselineId = -1;
		int flatCount = -1;
		__try {
			const __int64 edict = edicts && entIndex >= 0 ? edicts + 56LL * entIndex : 0;
			ent = edict && IsReadableRange(reinterpret_cast<void*>(edict + 40), sizeof(__int64))
				? *reinterpret_cast<__int64*>(edict + 40)
				: 0;
			if (ent && IsReadableRange(reinterpret_cast<void*>(ent), sizeof(__int64))) {
				const __int64 vtable = *reinterpret_cast<__int64*>(ent);
				if (vtable && IsReadableRange(reinterpret_cast<void*>(vtable + 8), sizeof(__int64))) {
					auto getServerClass = *reinterpret_cast<__int64(__fastcall**)(__int64)>(vtable + 8);
					serverClass = getServerClass(ent);
				}
			}
			if (serverClass && IsReadableRange(reinterpret_cast<void*>(serverClass), 0x20)) {
				className = R1OSafeCString(*reinterpret_cast<const char**>(serverClass), "<bad-class>");
				sendTable = *reinterpret_cast<__int64*>(serverClass + 8);
				classId = *reinterpret_cast<int*>(serverClass + 24);
				baselineId = *reinterpret_cast<int*>(serverClass + 28);
				flatCount = R1OSendTableFlatCount(sendTable);
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			className = "<av>";
		}

		char firstBytes[128];
		R1OBytesHex(firstBytes, sizeof(firstBytes), reinterpret_cast<const unsigned char*>(data), bytes);

		const char* tableName = R1OSendTableName(sendTable);
		if (_stricmp(tableName, "DT_HL2_Player") == 0
			|| _stricmp(tableName, "DT_World") == 0
			|| _stricmp(tableName, "DT_WORLD") == 0
			|| s_R1OInstanceBaselineLogBudget > 80) {
			char buffer[1024];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: R1O SV_EnsureInstanceBaseline ent=%d entptr=%p class=%p className=\"%s\" classId=%d baselineId=%d sendTable=%p table=\"%s\" flatCount=%d bytes=%u first=[%s] budget=%d\n",
				entIndex,
				reinterpret_cast<void*>(ent),
				reinterpret_cast<void*>(serverClass),
				className,
				classId,
				baselineId,
				reinterpret_cast<void*>(sendTable),
				tableName,
				flatCount,
				bytes,
				firstBytes,
				s_R1OInstanceBaselineLogBudget);
			OutputDebugStringA(buffer);
			Warning("%s", buffer);
		}
	}

	if (R1OSVEnsureInstanceBaselineOriginal)
		R1OSVEnsureInstanceBaselineOriginal(unused, entIndex, data, bytes);
}

static bool IsR1OInterestingCommandText(const char* text)
{
	if (!IsReadableCString(text))
		return false;

	return strstr(text, "startup") != nullptr
		|| strstr(text, "stuffcmds") != nullptr
		|| strstr(text, "map ") != nullptr
		|| strstr(text, "+map") != nullptr
		|| strstr(text, "exec ") != nullptr
		|| strstr(text, "maxplayers") != nullptr
		|| strstr(text, "status") != nullptr
		|| strstr(text, "echo") != nullptr
		|| strstr(text, "help ") != nullptr
		|| strstr(text, "ent_fire") != nullptr
		|| strstr(text, "kick") != nullptr
		|| strstr(text, "banid") != nullptr
		|| strstr(text, "removeid") != nullptr
		|| strstr(text, "removeallids") != nullptr
		|| strstr(text, "listid") != nullptr
		|| strstr(text, "writeid") != nullptr
		|| strstr(text, "banip") != nullptr
		|| strstr(text, "addip") != nullptr
		|| strstr(text, "removeip") != nullptr
		|| strstr(text, "removeallips") != nullptr
		|| strstr(text, "listip") != nullptr
		|| strstr(text, "writeip") != nullptr
		|| strstr(text, "bot_dummy") != nullptr
		|| strstr(text, "R1Delta_SetDummy") != nullptr
		|| strstr(text, "R1Delta_Throw") != nullptr
		|| strstr(text, "R1Delta_SpawnDummy") != nullptr;
}

static const char* GetR1OCCommandArg(__int64 command, int index)
{
	if (!command || index < 0)
		return nullptr;

	if (!IsReadableRange(reinterpret_cast<const void*>(command), 0x610))
		return nullptr;

	const int argc = *reinterpret_cast<int*>(command);
	if (index >= argc || argc < 0 || argc > 64)
		return nullptr;

	const char* const* argv = reinterpret_cast<const char* const*>(command + 1040);
	if (!IsReadableRange(argv, sizeof(const char*) * (static_cast<size_t>(index) + 1)))
		return nullptr;

	const char* value = argv[index];
	return IsReadableCString(value) ? value : nullptr;
}

static const char* GetR1OCCommandString(__int64 command)
{
	if (!command || !IsReadableRange(reinterpret_cast<const void*>(command + 16), 1))
		return "";

	const char* commandString = reinterpret_cast<const char*>(command + 16);
	return IsReadableCString(commandString) ? commandString : "";
}

static const char* GetR1OCCommandArgS(__int64 command)
{
	if (!command || !IsReadableRange(reinterpret_cast<const void*>(command + 8), sizeof(__int64)))
		return "";

	const __int64 argv0Size = *reinterpret_cast<const __int64*>(command + 8);
	if (argv0Size <= 0 || argv0Size >= 512)
		return "";

	const char* argString = reinterpret_cast<const char*>(command + 16 + argv0Size);
	return IsReadableCString(argString) ? argString : "";
}

struct R1OClientCommandIdentity
{
	bool connected;
	int userId;
	char name[128];
	char networkId[128];
};

static bool ReadR1OClientCommandIdentity(int index, R1OClientCommandIdentity* identity)
{
	if (!identity || !engineR1O || index < 0 || index >= 256)
		return false;

	memset(identity, 0, sizeof(*identity));
	__try {
		const uintptr_t client =
			reinterpret_cast<uintptr_t>(engineR1O)
			+ 0x2659738
			+ static_cast<uintptr_t>(index) * 22387 * sizeof(__int64);
		void** vtable = *reinterpret_cast<void***>(client);
		if (!vtable)
			return false;

		using IsConnectedType = bool(__fastcall*)(uintptr_t);
		using GetUserIdType = int(__fastcall*)(uintptr_t);
		using GetStringType = const char*(__fastcall*)(uintptr_t);
		auto isConnected = reinterpret_cast<IsConnectedType>(vtable[30]);
		auto getUserId = reinterpret_cast<GetUserIdType>(vtable[15]);
		auto getName = reinterpret_cast<GetStringType>(vtable[17]);
		auto getNetworkId = reinterpret_cast<GetStringType>(vtable[21]);
		if (!isConnected || !getUserId || !getName || !getNetworkId || !isConnected(client))
			return false;

		identity->connected = true;
		identity->userId = getUserId(client);
		const char* name = getName(client);
		const char* networkId = getNetworkId(client);
		if (name)
			_snprintf_s(identity->name, sizeof(identity->name), _TRUNCATE, "%s", name);
		if (networkId)
			_snprintf_s(identity->networkId, sizeof(identity->networkId), _TRUNCATE, "%s", networkId);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		memset(identity, 0, sizeof(*identity));
		return false;
	}
}

static int GetR1OClientCommandCount()
{
	if (!engineR1O)
		return 0;

	__try {
		const int count = *reinterpret_cast<const int*>(
			reinterpret_cast<uintptr_t>(engineR1O) + 0x265971C);
		return count >= 0 && count <= 256 ? count : 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return 0;
	}
}

static bool FindR1OClientByName(const char* name, R1OClientCommandIdentity* identity)
{
	if (!name || !name[0] || !identity)
		return false;

	for (int index = 0, count = GetR1OClientCommandCount(); index < count; ++index) {
		R1OClientCommandIdentity candidate = {};
		if (ReadR1OClientCommandIdentity(index, &candidate)
			&& !_stricmp(candidate.name, name)) {
			*identity = candidate;
			return true;
		}
	}
	return false;
}

static bool FindR1OClientById(const char* value, R1OClientCommandIdentity* identity)
{
	if (!value || !value[0] || !identity)
		return false;

	char* end = nullptr;
	const long parsedUserId = strtol(value, &end, 10);
	const bool isUserId = end && *end == '\0' && parsedUserId >= 0 && parsedUserId <= INT_MAX;
	for (int index = 0, count = GetR1OClientCommandCount(); index < count; ++index) {
		R1OClientCommandIdentity candidate = {};
		if (!ReadR1OClientCommandIdentity(index, &candidate))
			continue;
		if ((isUserId && candidate.userId == static_cast<int>(parsedUserId))
			|| (!isUserId && !_stricmp(candidate.networkId, value))) {
			*identity = candidate;
			return true;
		}
	}
	return false;
}

static void CopyR1ONormalizedKickName(__int64 command, char* output, size_t outputSize)
{
	if (!output || outputSize == 0)
		return;

	output[0] = '\0';
	const char* argString = GetR1OCCommandArgS(command);
	if (!IsReadableCString(argString))
		return;

	_snprintf_s(output, outputSize, _TRUNCATE, "%s", argString);
	const size_t length = strlen(output);
	if (length >= 2 && output[0] == '"' && output[length - 1] == '"') {
		memmove(output, output + 1, length - 2);
		output[length - 2] = '\0';
	}
}

static void BuildR1OSteamIdArgument(__int64 command, int firstArgument, char* output, size_t outputSize)
{
	if (!output || outputSize == 0)
		return;

	output[0] = '\0';
	const char* first = GetR1OCCommandArg(command, firstArgument);
	if (!first)
		return;

	const int argc = IsReadableRange(reinterpret_cast<const void*>(command), sizeof(int))
		? *reinterpret_cast<const int*>(command)
		: 0;
	if (!_strnicmp(first, "STEAM_", 6) && argc > firstArgument + 4) {
		const char* second = GetR1OCCommandArg(command, firstArgument + 2);
		const char* third = GetR1OCCommandArg(command, firstArgument + 4);
		if (second && third) {
			_snprintf_s(output, outputSize, _TRUNCATE, "%s:%s:%s", first, second, third);
			return;
		}
	}
	_snprintf_s(output, outputSize, _TRUNCATE, "%s", first);
}

struct R1ONativeBanState
{
	bool valid;
	int count;
	int permanentCount;
	unsigned __int64 hash;
};

static R1ONativeBanState CaptureR1ONativeBanState(
	uintptr_t entriesRva,
	uintptr_t countRva,
	size_t entrySize,
	size_t durationOffset)
{
	R1ONativeBanState state = {};
	if (!engineR1O || !entrySize || durationOffset + sizeof(float) > entrySize)
		return state;

	__try {
		const uintptr_t base = reinterpret_cast<uintptr_t>(engineR1O);
		const int count = *reinterpret_cast<const int*>(base + countRva);
		const unsigned char* entries = *reinterpret_cast<unsigned char* const*>(base + entriesRva);
		if (count < 0 || count > 0x8000 || (count > 0 && !entries))
			return state;

		unsigned __int64 hash = 1469598103934665603ULL;
		int permanentCount = 0;
		for (int index = 0; index < count; ++index) {
			const unsigned char* entry = entries + static_cast<size_t>(index) * entrySize;
			for (size_t byte = 0; byte < entrySize; ++byte) {
				hash ^= entry[byte];
				hash *= 1099511628211ULL;
			}
			if (*reinterpret_cast<const float*>(entry + durationOffset) == 0.0f)
				++permanentCount;
		}

		state.valid = true;
		state.count = count;
		state.permanentCount = permanentCount;
		state.hash = hash;
		return state;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return state;
	}
}

struct R1ONativeUserBanEntry
{
	unsigned char networkId[24];
	float expiresAt;
	float durationMinutes;
};

struct R1ONativeIpBanEntry
{
	unsigned int mask;
	unsigned int address;
	float expiresAt;
	float durationMinutes;
};

struct R1ONativeFilterAddress
{
	int type;
	unsigned char address[16];
	unsigned short port;
	unsigned char field16;
	unsigned char reliable;
};

struct R1OPackedIpAndPort
{
	unsigned char address[16];
	unsigned int scope;
	unsigned short type;
	unsigned short port;
};

static_assert(sizeof(R1ONativeFilterAddress) == 24, "R1O network address layout changed");
static_assert(offsetof(R1ONativeFilterAddress, address) == 4, "R1O network address offset changed");
static_assert(sizeof(R1OPackedIpAndPort) == 24, "R1O packed IP-and-port layout changed");
static_assert(offsetof(R1OPackedIpAndPort, type) == 20, "R1O packed IP type offset changed");

static bool ExtractR1OLegacyIpv4(
	const unsigned char* ipv6Bytes,
	unsigned char* ipv4)
{
	if (!ipv6Bytes || !ipv4)
		return false;

	const IN6_ADDR* ipv6 = reinterpret_cast<const IN6_ADDR*>(ipv6Bytes);
	if (IN6_IS_ADDR_V4MAPPED(ipv6)) {
		memcpy(ipv4, ipv6Bytes + 12, 4);
		return true;
	}
	if (IN6_IS_ADDR_LOOPBACK(ipv6)) {
		ipv4[0] = 127;
		ipv4[1] = 0;
		ipv4[2] = 0;
		ipv4[3] = 1;
		return true;
	}
	return false;
}

static bool NormalizeR1ONativeFilterAddress(
	const void* address,
	R1ONativeFilterAddress* normalized)
{
	if (!address || !normalized || !IsReadableRange(address, sizeof(*normalized)))
		return false;

	memcpy(normalized, address, sizeof(*normalized));
	unsigned char ipv4[4] = {};

	// Connection admission passes the legacy address wrapper with the IPv6
	// bytes at +4. The post-ban active-client scan instead passes the packed
	// CIPAndPort returned by INetChannel::GetRemoteAddress, where the address
	// starts at +0 and the type is at +20. Normalize either representation
	// into the +4 DWORD consumed by the native IPv4 filter.
	const R1OPackedIpAndPort* packed =
		reinterpret_cast<const R1OPackedIpAndPort*>(address);
	const bool packedIpv6 =
		packed->type == 4
		&& ExtractR1OLegacyIpv4(packed->address, ipv4);
	const bool legacyIpv6 =
		!packedIpv6
		&& ExtractR1OLegacyIpv4(normalized->address, ipv4);
	if (!packedIpv6 && !legacyIpv6)
		return false;

	memset(normalized->address, 0, sizeof(normalized->address));
	memcpy(normalized->address, ipv4, sizeof(ipv4));
	return true;
}

static bool __fastcall R1ONativeIpFilter(const void* address, int exemptAddress)
{
	if (!R1ONativeIpFilterOriginal)
		return false;

	R1ONativeFilterAddress normalized = {};
	const bool didNormalize = NormalizeR1ONativeFilterAddress(address, &normalized);
	return R1ONativeIpFilterOriginal(
		didNormalize ? &normalized : address,
		exemptAddress);
}

static bool NormalizeR1ONativeIpBanArgument(
	const char* input,
	char* output,
	size_t outputSize)
{
	if (!input || !output || outputSize == 0 || !IsReadableCString(input))
		return false;

	output[0] = '\0';
	char addressText[128] = {};
	_snprintf_s(addressText, sizeof(addressText), _TRUNCATE, "%s", input);

	char* first = addressText;
	while (*first && isspace(static_cast<unsigned char>(*first)))
		++first;
	char* end = first + strlen(first);
	while (end > first && isspace(static_cast<unsigned char>(end[-1])))
		*--end = '\0';
	if (end - first >= 2 && first[0] == '"' && end[-1] == '"') {
		++first;
		*--end = '\0';
	}
	if (first[0] == '[') {
		char* bracket = strchr(first + 1, ']');
		if (!bracket)
			return false;
		*bracket = '\0';
		first += 1;
	}

	IN_ADDR ipv4 = {};
	if (InetPtonA(AF_INET, first, &ipv4) == 1)
		return InetNtopA(AF_INET, &ipv4, output, static_cast<DWORD>(outputSize)) != nullptr;

	IN6_ADDR ipv6 = {};
	if (InetPtonA(AF_INET6, first, &ipv6) != 1)
		return false;
	if (IN6_IS_ADDR_V4MAPPED(&ipv6))
		return InetNtopA(AF_INET, &ipv6.s6_addr[12], output, static_cast<DWORD>(outputSize)) != nullptr;
	if (IN6_IS_ADDR_LOOPBACK(&ipv6)) {
		_snprintf_s(output, outputSize, _TRUNCATE, "127.0.0.1");
		return true;
	}
	return false;
}

static bool FormatR1OSteamNetworkId(
	const void* networkId,
	char* output,
	size_t outputSize)
{
	if (!networkId || !output || outputSize == 0
		|| !IsReadableRange(networkId, sizeof(R1ONativeUserBanEntry{}.networkId)))
		return false;

	const unsigned int* words = reinterpret_cast<const unsigned int*>(networkId);
	if (words[0] != 1)
		return false;

	_snprintf_s(
		output,
		outputSize,
		_TRUNCATE,
		"STEAM_%u:%u:%u",
		words[2],
		words[3],
		words[4]);
	return true;
}

static bool ReadR1ONativeUserBanEntry(int index, R1ONativeUserBanEntry* entry)
{
	if (!entry || !engineR1O || index < 0)
		return false;

	__try {
		const uintptr_t base = reinterpret_cast<uintptr_t>(engineR1O);
		const int count = *reinterpret_cast<const int*>(base + 0x264D1C8);
		const unsigned char* entries = *reinterpret_cast<unsigned char* const*>(base + 0x264D1B0);
		if (index >= count || !entries)
			return false;
		memcpy(entry, entries + static_cast<size_t>(index) * sizeof(*entry), sizeof(*entry));
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

static bool ReadR1ONativeIpBanEntry(int index, R1ONativeIpBanEntry* entry)
{
	if (!entry || !engineR1O || index < 0)
		return false;

	__try {
		const uintptr_t base = reinterpret_cast<uintptr_t>(engineR1O);
		const int count = *reinterpret_cast<const int*>(base + 0x264D1F0);
		const unsigned char* entries = *reinterpret_cast<unsigned char* const*>(base + 0x264D1D8);
		if (index >= count || !entries)
			return false;
		memcpy(entry, entries + static_cast<size_t>(index) * sizeof(*entry), sizeof(*entry));
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

static bool FormatR1ONativeNetworkId(
	const R1ONativeUserBanEntry* entry,
	char* output,
	size_t outputSize)
{
	if (!output || outputSize == 0)
		return false;
	output[0] = '\0';
	if (!entry || !engineR1O)
		return false;

	if (FormatR1OSteamNetworkId(entry->networkId, output, outputSize))
		return true;

	__try {
		using R1ONetworkIdToStringType = const char*(__fastcall*)(const void* networkId);
		auto toString = reinterpret_cast<R1ONetworkIdToStringType>(
			reinterpret_cast<uintptr_t>(engineR1O) + 0x140C20);
		const char* value = toString(entry->networkId);
		if (value)
			_snprintf_s(output, outputSize, _TRUNCATE, "%s", value);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		output[0] = '\0';
	}

	if (!output[0] || !_stricmp(output, "UNKNOWN")) {
		const unsigned int* words = reinterpret_cast<const unsigned int*>(entry->networkId);
		_snprintf_s(
			output,
			outputSize,
			_TRUNCATE,
			"UNKNOWN[type=%u raw=%08X%08X%08X%08X%08X%08X]",
			words[0],
			words[0],
			words[1],
			words[2],
			words[3],
			words[4],
			words[5]);
		return false;
	}
	return true;
}

static bool TryRemoveR1ONativeUserBan(const char* target)
{
	if (!target || !target[0] || !engineR1O)
		return false;

	const R1ONativeBanState state =
		CaptureR1ONativeBanState(0x264D1B0, 0x264D1C8, sizeof(R1ONativeUserBanEntry), 28);
	if (!state.valid || state.count == 0)
		return false;

	int removeIndex = -1;
	char* end = nullptr;
	const long numericIndex = strtol(target, &end, 10);
	if (end && *end == '\0' && numericIndex > 0 && numericIndex <= state.count) {
		removeIndex = static_cast<int>(numericIndex - 1);
	}
	else {
		for (int index = 0; index < state.count; ++index) {
			R1ONativeUserBanEntry entry = {};
			char networkId[192];
			if (ReadR1ONativeUserBanEntry(index, &entry)
				&& FormatR1ONativeNetworkId(&entry, networkId, sizeof(networkId))
				&& !_stricmp(networkId, target)) {
				removeIndex = index;
				break;
			}
		}
	}

	if (removeIndex < 0)
		return false;

	__try {
		using RemoveUserBanType = unsigned __int64(__fastcall*)(__int64 unused, int index);
		auto removeUserBan = reinterpret_cast<RemoveUserBanType>(
			reinterpret_cast<uintptr_t>(engineR1O) + 0x14A070);
		removeUserBan(0, removeIndex);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

static bool TryClearR1ONativeBanList(uintptr_t countRva, uintptr_t removeRva)
{
	if (!engineR1O)
		return false;

	__try {
		const uintptr_t base = reinterpret_cast<uintptr_t>(engineR1O);
		int* count = reinterpret_cast<int*>(base + countRva);
		if (*count < 0 || *count > 0x8000)
			return false;

		using RemoveBanType = unsigned __int64(__fastcall*)(__int64 unused, int index);
		auto removeBan = reinterpret_cast<RemoveBanType>(base + removeRva);
		while (*count > 0) {
			const int previousCount = *count;
			removeBan(0, previousCount - 1);
			if (*count != previousCount - 1)
				return false;
		}
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

static void PrintR1ONativeUserBanList()
{
	const R1ONativeBanState state =
		CaptureR1ONativeBanState(0x264D1B0, 0x264D1C8, sizeof(R1ONativeUserBanEntry), 28);
	if (!state.valid) {
		Status_ConMsg("Unable to read the R1O user ban list.\n");
		return;
	}
	if (state.count == 0) {
		Status_ConMsg("User ID ban list is empty.\n");
		return;
	}

	Status_ConMsg("User ID ban list (%d entr%s):\n", state.count, state.count == 1 ? "y" : "ies");
	for (int index = 0; index < state.count; ++index) {
		R1ONativeUserBanEntry entry = {};
		if (!ReadR1ONativeUserBanEntry(index, &entry))
			continue;
		char networkId[192];
		FormatR1ONativeNetworkId(&entry, networkId, sizeof(networkId));
		if (entry.durationMinutes == 0.0f)
			Status_ConMsg("  %d: %s (permanent)\n", index + 1, networkId);
		else
			Status_ConMsg("  %d: %s (%.2f minutes)\n", index + 1, networkId, entry.durationMinutes);
	}
}

static void PrintR1ONativeIpBanList()
{
	const R1ONativeBanState state =
		CaptureR1ONativeBanState(0x264D1D8, 0x264D1F0, sizeof(R1ONativeIpBanEntry), 12);
	if (!state.valid) {
		Status_ConMsg("Unable to read the R1O IP ban list.\n");
		return;
	}
	if (state.count == 0) {
		Status_ConMsg("IP ban list is empty.\n");
		return;
	}

	Status_ConMsg("IP ban list (%d entr%s):\n", state.count, state.count == 1 ? "y" : "ies");
	for (int index = 0; index < state.count; ++index) {
		R1ONativeIpBanEntry entry = {};
		if (!ReadR1ONativeIpBanEntry(index, &entry))
			continue;
		const unsigned char* address = reinterpret_cast<const unsigned char*>(&entry.address);
		const unsigned char* mask = reinterpret_cast<const unsigned char*>(&entry.mask);
		if (entry.durationMinutes == 0.0f) {
			Status_ConMsg(
				"  %d: %u.%u.%u.%u mask %u.%u.%u.%u (permanent)\n",
				index + 1,
				address[0], address[1], address[2], address[3],
				mask[0], mask[1], mask[2], mask[3]);
		}
		else {
			Status_ConMsg(
				"  %d: %u.%u.%u.%u mask %u.%u.%u.%u (%.2f minutes)\n",
				index + 1,
				address[0], address[1], address[2], address[3],
				mask[0], mask[1], mask[2], mask[3],
				entry.durationMinutes);
		}
	}
}

static bool IsR1OInterestingCommandName(const char* name)
{
	if (!IsReadableCString(name))
		return false;

	return !_stricmp(name, "exec")
		|| !_stricmp(name, "stuffcmds")
		|| !_stricmp(name, "maxplayers")
		|| !_stricmp(name, "status")
		|| !_stricmp(name, "echo")
		|| !_stricmp(name, "help")
		|| !_stricmp(name, "ent_fire")
		|| !_stricmp(name, "kick")
		|| !_stricmp(name, "kickid")
		|| !_stricmp(name, "banid")
		|| !_stricmp(name, "removeid")
		|| !_stricmp(name, "removeallids")
		|| !_stricmp(name, "listid")
		|| !_stricmp(name, "writeid")
		|| !_stricmp(name, "banip")
		|| !_stricmp(name, "addip")
		|| !_stricmp(name, "removeip")
		|| !_stricmp(name, "removeallips")
		|| !_stricmp(name, "listip")
		|| !_stricmp(name, "writeip")
		|| !_stricmp(name, "script")
		|| !_stricmp(name, "find")
		|| !_stricmp(name, "bot_dummy")
		|| !_stricmp(name, "map")
		|| !_stricmp(name, "host_map")
		|| !_stricmp(name, "ss_map")
		|| !_stricmp(name, "hostport")
		|| !_stricmp(name, "net_start")
		|| !_stricmp(name, "changelevel")
		|| !_stricmp(name, "changelevel2")
		|| !_stricmp(name, "disconnect");
}

static void CopyReadableStringForDebug(const char* source, char* dest, size_t destSize)
{
	if (!dest || destSize == 0)
		return;

	dest[0] = '\0';
	if (!IsReadableCString(source))
		return;

	_snprintf_s(dest, destSize, _TRUNCATE, "%s", source);
}

static int ResolveR1ODediServerPort(int currentPort);
static int OpenR1ODediSocket(int port, bool stream);
static void __fastcall R1ONetListenSocket(int socketIndex);
static unsigned int s_R1ONetListenInvocationCount;
static int s_R1ODediServerSocketIndex = 1;
static std::atomic<int> s_R1ODediBoundServerPort{0};

int GetR1ODedicatedBoundServerPort()
{
	return s_R1ODediBoundServerPort.load(std::memory_order_acquire);
}

template <typename T>
static T* R1OHookGlobal(uintptr_t rva)
{
	const uintptr_t base = reinterpret_cast<uintptr_t>(engineR1O);
	return base ? reinterpret_cast<T*>(base + rva) : nullptr;
}

template <typename T>
static T R1OHookGlobalValue(uintptr_t rva, T fallback)
{
	T* value = R1OHookGlobal<T>(rva);
	return value && IsReadableRange(value, sizeof(T)) ? *value : fallback;
}

template <typename T>
static bool WriteR1OHookGlobalValue(uintptr_t rva, T value)
{
	T* target = R1OHookGlobal<T>(rva);
	if (!target || !IsReadableRange(target, sizeof(T)))
		return false;

	DWORD oldProtect = 0;
	if (!VirtualProtect(target, sizeof(T), PAGE_READWRITE, &oldProtect))
		return false;

	*target = value;

	DWORD ignoredProtect = 0;
	VirtualProtect(target, sizeof(T), oldProtect, &ignoredProtect);
	return true;
}

static void __fastcall R1OUpdateMap(unsigned char updateType, const char* mapName, const char* gameMode)
{
	if (!IsR1ODedicatedServer()) {
		if (R1OUpdateMapOriginal)
			R1OUpdateMapOriginal(updateType, mapName, gameMode);
		return;
	}

	void* const fakeVideoMode = GetR1ONullClientOnlyInterface();
	void* const previousVideoMode = R1OHookGlobalValue<void*>(0x22FB430, nullptr);
	const bool installedFakeVideoMode = !previousVideoMode
		&& fakeVideoMode
		&& WriteR1OHookGlobalValue<void*>(0x22FB430, fakeVideoMode);

	if (installedFakeVideoMode && s_R1OUpdateMapNullVideoModeLogBudget > 0) {
		--s_R1OUpdateMapNullVideoModeLogBudget;
		char buffer[384];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O UpdateMap using temporary no-op CVideoMode global map=%s mode=%s budget=%d\n",
			mapName ? mapName : "<null>",
			gameMode ? gameMode : "<null>",
			s_R1OUpdateMapNullVideoModeLogBudget);
		OutputDebugStringA(buffer);
	}

	DrainR1OVPhysicsDeferredReleases("before R1O UpdateMap");

	if (R1OUpdateMapOriginal)
		R1OUpdateMapOriginal(updateType, mapName, gameMode);

	DrainR1OVPhysicsDeferredReleases("after R1O UpdateMap");

	if (installedFakeVideoMode && R1OHookGlobalValue<void*>(0x22FB430, nullptr) == fakeVideoMode)
		WriteR1OHookGlobalValue<void*>(0x22FB430, nullptr);
}

static void InstallR1OUpdateMapHook(uintptr_t engineBase)
{
	if (!IsR1ODedicatedServer() || !engineBase || s_R1OUpdateMapHooked)
		return;

	void* target = reinterpret_cast<void*>(engineBase + 0x183A20);
	const MH_STATUS status = MH_CreateHook(target, &R1OUpdateMap, reinterpret_cast<LPVOID*>(&R1OUpdateMapOriginal));
	const MH_STATUS enableStatus = (status == MH_OK || status == MH_ERROR_ALREADY_CREATED) ? MH_EnableHook(target) : status;
	s_R1OUpdateMapHooked = status == MH_OK || status == MH_ERROR_ALREADY_CREATED;

	char buffer[384];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O UpdateMap hook status=%d enable=%d target=%p original=%p\n",
		static_cast<int>(status),
		static_cast<int>(enableStatus),
		target,
		reinterpret_cast<void*>(R1OUpdateMapOriginal));
	OutputDebugStringA(buffer);
}

static void* R1OMaterialSystem_SubstituteNullMaterial(
	void* result,
	void* thisptr,
	const char* materialName,
	const char* pathId,
	const char* slotName)
{
	if (IsR1ODedicatedServer() && !result) {
		static int logBudget = 0;
		if (logBudget > 0) {
			--logBudget;
			char buffer[512];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: R1O material-system %s returned null, substituting fake material name=%s pathId=%s this=%p\n",
				slotName ? slotName : "<unknown>",
				IsReadableCString(materialName) ? materialName : "<unreadable>",
				IsReadableCString(pathId) ? pathId : "<unreadable>",
				thisptr);
			OutputDebugStringA(buffer);
		}
		result = GetR1OFakeDedicatedMaterial();
	}

	return result;
}

static void* __fastcall R1OMaterialSystem_FindOrLoadMaterialSlot117(
	void* thisptr,
	const char* materialName,
	const char* pathId,
	__int64 arg3,
	__int64 arg4,
	__int64 arg5,
	__int64 arg6)
{
	void* result = R1OMaterialSystem_FindOrLoadMaterialSlot117Original
		? R1OMaterialSystem_FindOrLoadMaterialSlot117Original(thisptr, materialName, pathId, arg3, arg4, arg5, arg6)
		: nullptr;

	return R1OMaterialSystem_SubstituteNullMaterial(result, thisptr, materialName, pathId, "slot117");
}

static void* __fastcall R1OMaterialSystem_FindOrLoadMaterialSlot118(
	void* thisptr,
	const char* materialName,
	const char* pathId,
	__int64 arg3,
	__int64 arg4,
	__int64 arg5,
	__int64 arg6)
{
	void* result = R1OMaterialSystem_FindOrLoadMaterialSlot118Original
		? R1OMaterialSystem_FindOrLoadMaterialSlot118Original(thisptr, materialName, pathId, arg3, arg4, arg5, arg6)
		: nullptr;

	return R1OMaterialSystem_SubstituteNullMaterial(result, thisptr, materialName, pathId, "slot118");
}

static void EnsureR1ODedicatedMaterialFallbacks()
{
	if (!IsR1ODedicatedServer() || !engineR1O)
		return;

	void* fakeMaterial = GetR1OFakeDedicatedMaterial();
	void* defaultMaterial = R1OHookGlobalValue<void*>(0x22ED080, nullptr);
	if (defaultMaterial != fakeMaterial) {
		if (WriteR1OHookGlobalValue<void*>(0x22ED080, fakeMaterial)) {
			if (!s_R1OFakeMaterialDefaultInstalled) {
				s_R1OFakeMaterialDefaultInstalled = true;
				char buffer[256];
				_snprintf_s(
					buffer,
					sizeof(buffer),
					_TRUNCATE,
					"R1Delta: installed R1O fake dedicated default material global=%p previous=%p\n",
					fakeMaterial,
					defaultMaterial);
				OutputDebugStringA(buffer);
			}
		}
	}

	if (s_R1OMaterialLoadHooked)
		return;

	void* materialSystem = R1OHookGlobalValue<void*>(0x22FB6A8, nullptr);
	if (!materialSystem || !IsReadableRange(materialSystem, sizeof(void*)))
		return;

	void** vtable = *reinterpret_cast<void***>(materialSystem);
	if (!vtable || !IsReadableRange(vtable, sizeof(void*) * 119) || !vtable[117] || !vtable[118])
		return;

	const MH_STATUS status117 = MH_CreateHook(
		vtable[117],
		&R1OMaterialSystem_FindOrLoadMaterialSlot117,
		reinterpret_cast<LPVOID*>(&R1OMaterialSystem_FindOrLoadMaterialSlot117Original));
	const MH_STATUS enable117 = (status117 == MH_OK || status117 == MH_ERROR_ALREADY_CREATED)
		? MH_EnableHook(vtable[117])
		: status117;

	const MH_STATUS status118 = MH_CreateHook(
		vtable[118],
		&R1OMaterialSystem_FindOrLoadMaterialSlot118,
		reinterpret_cast<LPVOID*>(&R1OMaterialSystem_FindOrLoadMaterialSlot118Original));
	const MH_STATUS enable118 = (status118 == MH_OK || status118 == MH_ERROR_ALREADY_CREATED)
		? MH_EnableHook(vtable[118])
		: status118;

	if ((enable117 == MH_OK || enable117 == MH_ERROR_ENABLED)
		&& (enable118 == MH_OK || enable118 == MH_ERROR_ENABLED)) {
		s_R1OMaterialLoadHooked = true;
		char buffer[384];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: hooked R1O material-system load fallbacks materialSystem=%p vt=%p slot117=%p status117=%d enable117=%d original117=%p slot118=%p status118=%d enable118=%d original118=%p\n",
			materialSystem,
			vtable,
			vtable[117],
			static_cast<int>(status117),
			static_cast<int>(enable117),
			reinterpret_cast<void*>(R1OMaterialSystem_FindOrLoadMaterialSlot117Original),
			vtable[118],
			static_cast<int>(status118),
			static_cast<int>(enable118),
			reinterpret_cast<void*>(R1OMaterialSystem_FindOrLoadMaterialSlot118Original));
		OutputDebugStringA(buffer);
	}
}

static void EnsureR1ODedicatedWorldModelFallbacks()
{
	if (!IsR1ODedicatedServer() || !engineR1O)
		return;

	void* callback = R1OHookGlobalValue<void*>(0x22FAE10, nullptr);
	if (callback)
		return;

	if (WriteR1OHookGlobalValue<void*>(0x22FAE10, reinterpret_cast<void*>(&R1OWorldModelReleaseNoOp))) {
		if (!s_R1OWorldModelReleaseCallbackInstalled) {
			s_R1OWorldModelReleaseCallbackInstalled = true;
			OutputDebugStringA("R1Delta: installed R1O fake-dedi world-model release callback fallback\n");
		}
	}
}

static void EnsureR1ODedicatedClientDllModelFallback()
{
	if (!IsR1ODedicatedServer() || !engineR1O)
		return;

	const int current = R1OHookGlobalValue<int>(0x22EFDB0, 0);
	if (current == -1)
		return;

	if (WriteR1OHookGlobalValue<int>(0x22EFDB0, -1)) {
		if (!s_R1OClientDllModelCacheDisabled) {
			s_R1OClientDllModelCacheDisabled = true;
			OutputDebugStringA("R1Delta: disabled R1O fake-dedi client.dll model cache lookup\n");
		}
	}
}

static bool WriteModuleGlobalValue(HMODULE module, uintptr_t rva, void* value)
{
	if (!module)
		return false;

	void** target = reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(module) + rva);
	if (!IsReadableRange(target, sizeof(void*)))
		return false;

	DWORD oldProtect = 0;
	if (!VirtualProtect(target, sizeof(void*), PAGE_READWRITE, &oldProtect))
		return false;

	*target = value;

	DWORD ignoredProtect = 0;
	VirtualProtect(target, sizeof(void*), oldProtect, &ignoredProtect);
	return true;
}

static void EnsureR1ODataCacheFileSystemGlobal()
{
	if (!IsR1ODedicatedServer())
		return;

	HMODULE datacache = GetModuleHandleA("datacache.dll");
	if (!datacache)
		datacache = LoadR1OTFOSupportModule("datacache.dll");
	if (!datacache)
		return;

	void* fileSystem = s_R1OTFOFileSystem017;
	if (!fileSystem) {
		fileSystem = R1OTFOFileSystemInterface("VFileSystem017", nullptr);
		s_R1OTFOFileSystem017 = fileSystem;
	}
	if (!fileSystem)
		return;

	void* current = nullptr;
	void** target = reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(datacache) + 0x7D3C8);
	if (IsReadableRange(target, sizeof(void*)))
		current = *target;

	if (current == fileSystem)
		return;

	if (WriteModuleGlobalValue(datacache, 0x7D3C8, fileSystem)) {
		if (!s_R1ODataCacheFileSystemGlobalInstalled) {
			s_R1ODataCacheFileSystemGlobalInstalled = true;
			char buffer[384];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: wired TFO datacache filesystem global datacache=%p fs=%p previous=%p\n",
				datacache,
				fileSystem,
				current);
			OutputDebugStringA(buffer);
		}
	}
}

static void EnsureR1ODataCachePhysicsSurfacePropsGlobal()
{
	if (!IsR1ODedicatedServer())
		return;

	HMODULE datacache = GetModuleHandleA("datacache.dll");
	if (!datacache)
		datacache = LoadR1OTFOSupportModule("datacache.dll");
	if (!datacache)
		return;

	void* physicsSurfaceProps = s_R1OTFOPhysicsSurfaceProps001;
	if (!physicsSurfaceProps) {
		physicsSurfaceProps = R1OTFOSupportModuleInterface("VPhysicsSurfaceProps001", nullptr);
		if (!physicsSurfaceProps)
			physicsSurfaceProps = R1OQueryLoadedModuleFactories("VPhysicsSurfaceProps001", nullptr);
		s_R1OTFOPhysicsSurfaceProps001 = physicsSurfaceProps;
	}
	if (!physicsSurfaceProps)
		return;

	void* current = nullptr;
	void** target = reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(datacache) + 0x7D1C0);
	if (IsReadableRange(target, sizeof(void*)))
		current = *target;

	if (current == physicsSurfaceProps)
		return;

	if (WriteModuleGlobalValue(datacache, 0x7D1C0, physicsSurfaceProps)) {
		if (!s_R1ODataCachePhysicsSurfacePropsGlobalInstalled) {
			s_R1ODataCachePhysicsSurfacePropsGlobalInstalled = true;
			char buffer[384];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: wired TFO datacache physics surface props global datacache=%p physprops=%p previous=%p\n",
				datacache,
				physicsSurfaceProps,
				current);
			OutputDebugStringA(buffer);
		}
	}
}

static void EnsureR1ODataCachePhysicsCollisionGlobal()
{
	if (!IsR1ODedicatedServer())
		return;

	HMODULE datacache = GetModuleHandleA("datacache.dll");
	if (!datacache)
		datacache = LoadR1OTFOSupportModule("datacache.dll");
	if (!datacache)
		return;

	void* physicsCollision = s_R1OTFOPhysicsCollision007;
	if (!physicsCollision) {
		physicsCollision = R1OTFOSupportModuleInterface("VPhysicsCollision007", nullptr);
		if (!physicsCollision)
			physicsCollision = R1OQueryLoadedModuleFactories("VPhysicsCollision007", nullptr);
		s_R1OTFOPhysicsCollision007 = physicsCollision;
	}
	if (!physicsCollision)
		return;

	void* current = nullptr;
	void** target = reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(datacache) + 0x7D3D8);
	if (IsReadableRange(target, sizeof(void*)))
		current = *target;

	if (current == physicsCollision)
		return;

	if (WriteModuleGlobalValue(datacache, 0x7D3D8, physicsCollision)) {
		if (!s_R1ODataCachePhysicsCollisionGlobalInstalled) {
			s_R1ODataCachePhysicsCollisionGlobalInstalled = true;
			char buffer[384];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: wired TFO datacache physics collision global datacache=%p collision=%p previous=%p\n",
				datacache,
				physicsCollision,
				current);
			OutputDebugStringA(buffer);
		}
	}
}

static void EnsureR1ODataCacheStudioRenderGlobal()
{
	if (!IsR1ODedicatedServer())
		return;

	HMODULE datacache = GetModuleHandleA("datacache.dll");
	if (!datacache)
		datacache = LoadR1OTFOSupportModule("datacache.dll");
	if (!datacache)
		return;

	void* studioRender = s_R1OTFOStudioRender026;
	if (!studioRender) {
		studioRender = R1OTFOSupportModuleInterface("VStudioRender026", nullptr);
		if (!studioRender)
			studioRender = R1OQueryLoadedModuleFactories("VStudioRender026", nullptr);
		s_R1OTFOStudioRender026 = studioRender;
	}
	if (!studioRender)
		return;

	void* current = nullptr;
	void** target = reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(datacache) + 0x7D3B8);
	if (IsReadableRange(target, sizeof(void*)))
		current = *target;

	if (current == studioRender)
		return;

	if (WriteModuleGlobalValue(datacache, 0x7D3B8, studioRender)) {
		if (!s_R1ODataCacheStudioRenderGlobalInstalled) {
			s_R1ODataCacheStudioRenderGlobalInstalled = true;
			char buffer[384];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: wired TFO datacache studiorender global datacache=%p studiorender=%p previous=%p\n",
				datacache,
				studioRender,
				current);
			OutputDebugStringA(buffer);
		}
	}
}

static void EnsureR1OLauncherFileSystemGlobal()
{
	if (!IsR1ODedicatedServer())
		return;

	HMODULE launcher = GetModuleHandleA("launcher.dll");
	if (!launcher)
		return;

	void* fileSystem = s_R1OTFOFileSystem017;
	if (!fileSystem) {
		fileSystem = R1OTFOFileSystemInterface("VFileSystem017", nullptr);
		s_R1OTFOFileSystem017 = fileSystem;
	}
	if (!fileSystem)
		return;

	void* current = nullptr;
	void** target = reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(launcher) + 0xECBE0);
	if (IsReadableRange(target, sizeof(void*)))
		current = *target;

	if (current == fileSystem)
		return;

	if (WriteModuleGlobalValue(launcher, 0xECBE0, fileSystem)) {
		if (!s_R1OLauncherFileSystemGlobalInstalled) {
			s_R1OLauncherFileSystemGlobalInstalled = true;
			char buffer[384];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: wired TFO launcher filesystem global launcher=%p fs=%p previous=%p\n",
				launcher,
				fileSystem,
				current);
			OutputDebugStringA(buffer);
		}
	}
}

static void EnsureR1OLauncherScriptFatalHooks()
{
	if (!IsR1ODedicatedServer() || s_R1OLauncherScriptFatalHooksInstalled)
		return;

	HMODULE launcher = GetModuleHandleA("launcher.dll");
	if (!launcher)
		return;

	InstallR1OTFOSquirrelHooks(reinterpret_cast<uintptr_t>(launcher));

	void* reporter = GetR1OLauncherScriptFatalReporter();
	void** reporterTarget = reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(launcher) + 0xF4228);
	void* previousReporter = nullptr;
	if (IsReadableRange(reporterTarget, sizeof(void*)))
		previousReporter = *reporterTarget;

	bool reporterInstalled = previousReporter == reporter;
	if (!previousReporter || previousReporter == reporter)
		reporterInstalled = WriteModuleGlobalValue(launcher, 0xF4228, reporter);

	void* target = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(launcher) + 0x40100);
	const MH_STATUS status = MH_CreateHook(
		target,
		&R1OLauncherScriptFatalDispatch,
		reinterpret_cast<LPVOID*>(&R1OLauncherScriptFatalDispatchOriginal));
	const MH_STATUS enableStatus = (status == MH_OK || status == MH_ERROR_ALREADY_CREATED)
		? MH_EnableHook(target)
		: status;
	s_R1OLauncherScriptFatalHooksInstalled = reporterInstalled
		&& (enableStatus == MH_OK || enableStatus == MH_ERROR_ENABLED || status == MH_ERROR_ALREADY_CREATED);

	if (AreR1OFakeDediVerboseLogsEnabled()) {
		char buffer[512];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O launcher fatal-script hooks reporterInstalled=%d previousReporter=%p reporter=%p dispatchStatus=%d enable=%d launcher=%p target=%p original=%p installed=%d\n",
			static_cast<int>(reporterInstalled),
			previousReporter,
			reporter,
			static_cast<int>(status),
			static_cast<int>(enableStatus),
			launcher,
			target,
			reinterpret_cast<void*>(R1OLauncherScriptFatalDispatchOriginal),
			static_cast<int>(s_R1OLauncherScriptFatalHooksInstalled));
		OutputDebugStringA(buffer);
	}
}


static void EnsureR1OStudioRenderStudioDataCacheGlobal()
{
	if (!IsR1ODedicatedServer())
		return;

	HMODULE studioRender = GetModuleHandleA("studiorender.dll");
	if (!studioRender)
		studioRender = LoadR1OTFOSupportModule("studiorender.dll");
	if (!studioRender)
		return;

	void* studioDataCache = s_R1OTFOStudioDataCache005;
	if (!studioDataCache) {
		studioDataCache = R1OTFOSupportModuleInterface("VStudioDataCache005", nullptr);
		s_R1OTFOStudioDataCache005 = studioDataCache;
	}
	if (!studioDataCache)
		return;

	void* current = nullptr;
	void** target = reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(studioRender) + 0x530D8);
	if (IsReadableRange(target, sizeof(void*)))
		current = *target;

	if (current == studioDataCache)
		return;

	if (WriteModuleGlobalValue(studioRender, 0x530D8, studioDataCache)) {
		if (!s_R1OStudioRenderStudioDataCacheGlobalInstalled) {
			s_R1OStudioRenderStudioDataCacheGlobalInstalled = true;
			char buffer[384];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: wired TFO studiorender studio-data-cache global studiorender=%p studioDataCache=%p previous=%p\n",
				studioRender,
				studioDataCache,
				current);
			OutputDebugStringA(buffer);
		}
	}
}

static void* ResolveR1ODedicatedMaterialSystemInterface()
{
	if (!IsR1ODedicatedServer())
		return nullptr;

	if (s_R1ODedicatedMaterialSystem083)
		return s_R1ODedicatedMaterialSystem083;

	void* materialSystem = nullptr;
	HMODULE module = LoadR1ODedicatedMaterialSystemProxy();
	if (module) {
		auto factory = reinterpret_cast<CreateInterfaceFn>(GetProcAddress(module, "CreateInterface"));
		if (factory) {
			int returnCode = 0;
			materialSystem = factory("VMaterialSystem083", &returnCode);
			DebugR1ODediFactoryResult("materialsystem-nodx-proxy", "VMaterialSystem083", materialSystem, &returnCode);
			if (materialSystem) {
				OutputDebugStringA("R1Delta: using proxy-backed R1O dedicated VMaterialSystem083\n");
				s_R1ODedicatedMaterialSystem083 = materialSystem;
				return materialSystem;
			}
		}
	}

	materialSystem = R1OHookGlobalValue<void*>(0x22FB6A8, nullptr);
	if (materialSystem && IsReadableRange(materialSystem, sizeof(void*))) {
		OutputDebugStringA("R1Delta: falling back to R1O dedicated VMaterialSystem083 global\n");
		s_R1ODedicatedMaterialSystem083 = materialSystem;
		return materialSystem;
	}

	materialSystem = R1OQueryLoadedModuleFactories("VMaterialSystem083", nullptr);
	if (materialSystem)
		OutputDebugStringA("R1Delta: using queried loaded-module R1O dedicated VMaterialSystem083\n");
	s_R1ODedicatedMaterialSystem083 = materialSystem;
	return materialSystem;
}

static void EnsureR1OStudioRenderMaterialSystemGlobal()
{
	if (!IsR1ODedicatedServer())
		return;

	HMODULE studioRender = GetModuleHandleA("studiorender.dll");
	if (!studioRender)
		studioRender = LoadR1OTFOSupportModule("studiorender.dll");
	if (!studioRender)
		return;

	void* materialSystem = ResolveR1ODedicatedMaterialSystemInterface();
	if (!materialSystem)
		return;

	void* current = nullptr;
	void** target = reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(studioRender) + 0x530E0);
	if (IsReadableRange(target, sizeof(void*)))
		current = *target;

	if (current == materialSystem)
		return;

	if (WriteModuleGlobalValue(studioRender, 0x530E0, materialSystem)) {
		if (!s_R1OStudioRenderMaterialSystemGlobalInstalled) {
			s_R1OStudioRenderMaterialSystemGlobalInstalled = true;
			char buffer[384];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: wired TFO studiorender material-system global studiorender=%p materials=%p previous=%p\n",
				studioRender,
				materialSystem,
				current);
			OutputDebugStringA(buffer);
		}
	}
}

static void* ResolveR1ODedicatedMaterialSystemHardwareConfigInterface()
{
	if (!IsR1ODedicatedServer())
		return nullptr;

	if (s_R1ODedicatedMaterialSystemHardwareConfig015)
		return s_R1ODedicatedMaterialSystemHardwareConfig015;

	HMODULE module = LoadR1ODedicatedMaterialSystemProxy();
	if (module) {
		auto factory = reinterpret_cast<CreateInterfaceFn>(GetProcAddress(module, "CreateInterface"));
		if (factory) {
			int returnCode = 0;
			void* hardwareConfig = factory("MaterialSystemHardwareConfig015", &returnCode);
			DebugR1ODediFactoryResult("materialsystem-nodx-proxy", "MaterialSystemHardwareConfig015", hardwareConfig, &returnCode);
			if (hardwareConfig) {
				s_R1ODedicatedMaterialSystemHardwareConfig015 = hardwareConfig;
				return hardwareConfig;
			}

			returnCode = 0;
			hardwareConfig = factory("RenderHardwareConfig001", &returnCode);
			DebugR1ODediFactoryResult("materialsystem-nodx-proxy", "RenderHardwareConfig001", hardwareConfig, &returnCode);
			if (hardwareConfig) {
				s_R1ODedicatedMaterialSystemHardwareConfig015 = hardwareConfig;
				return hardwareConfig;
			}
		}
	}

	void* hardwareConfig = R1OQueryLoadedModuleFactories("MaterialSystemHardwareConfig015", nullptr);
	if (hardwareConfig) {
		s_R1ODedicatedMaterialSystemHardwareConfig015 = hardwareConfig;
		return hardwareConfig;
	}

	hardwareConfig = R1OQueryLoadedModuleFactories("RenderHardwareConfig001", nullptr);
	s_R1ODedicatedMaterialSystemHardwareConfig015 = hardwareConfig;
	return hardwareConfig;
}

static void EnsureR1OStudioRenderMaterialSystemHardwareConfigGlobal()
{
	if (!IsR1ODedicatedServer())
		return;

	HMODULE studioRender = GetModuleHandleA("studiorender.dll");
	if (!studioRender)
		studioRender = LoadR1OTFOSupportModule("studiorender.dll");
	if (!studioRender)
		return;

	void* hardwareConfig = ResolveR1ODedicatedMaterialSystemHardwareConfigInterface();
	if (!hardwareConfig)
		return;

	void* current = nullptr;
	void** target = reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(studioRender) + 0x53230);
	if (IsReadableRange(target, sizeof(void*)))
		current = *target;

	if (current == hardwareConfig)
		return;

	if (WriteModuleGlobalValue(studioRender, 0x53230, hardwareConfig)) {
		if (!s_R1OStudioRenderMaterialSystemHardwareConfigGlobalInstalled) {
			s_R1OStudioRenderMaterialSystemHardwareConfigGlobalInstalled = true;
			char buffer[384];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: wired TFO studiorender material hardware-config global studiorender=%p hardwareConfig=%p previous=%p\n",
				studioRender,
				hardwareConfig,
				current);
			OutputDebugStringA(buffer);
		}
	}
}

static void CopyR1OGlobalString(uintptr_t rva, char* dest, size_t destSize)
{
	if (!dest || !destSize)
		return;

	dest[0] = '\0';
	const char* source = R1OHookGlobal<const char>(rva);
	if (!source || !IsReadableRange(source, 1))
		return;

	size_t copied = 0;
	while (copied + 1 < destSize && IsReadableRange(source + copied, 1) && source[copied]) {
		dest[copied] = source[copied];
		++copied;
	}
	dest[copied] = '\0';
}

static void LogR1OHostStateSnapshot(const char* reason, bool force)
{
	if (!IsR1ODedicatedServer())
		return;

	if (!force) {
		if (s_R1OHostStateFrameLogBudget <= 0)
			return;
		--s_R1OHostStateFrameLogBudget;
	}

	char mapName[272];
	CopyR1OGlobalString(0x6DADE0, mapName, sizeof(mapName));

	const uintptr_t appFrameState = reinterpret_cast<uintptr_t>(engineR1O) + 0x6DED30;
	const int appUnknown8 = IsReadableRange(reinterpret_cast<void*>(appFrameState + 8), sizeof(int))
		? *reinterpret_cast<int*>(appFrameState + 8)
		: -1;
	const int appCurrent = IsReadableRange(reinterpret_cast<void*>(appFrameState + 12), sizeof(int))
		? *reinterpret_cast<int*>(appFrameState + 12)
		: -1;
	const int appTarget = IsReadableRange(reinterpret_cast<void*>(appFrameState + 16), sizeof(int))
		? *reinterpret_cast<int*>(appFrameState + 16)
		: -1;

	void* materialGate = R1OHookGlobalValue<void*>(0x6CD400, nullptr);
	const int materialGateValue = materialGate && IsReadableRange(reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(materialGate) + 100), sizeof(int))
		? *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(materialGate) + 100)
		: -1;

	char buffer[1600];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O host-state reason=%s host[current=%d next=%d map=%s flags=%d loadFailed=%d] appState[ptr=%p unknown8=%d current=%d target=%d] dedicated[byte=%d word=%d] net[clients=%p signon=%d serverActive=%d multiplayer=%d maxClients=%d socketIndex=%d socketTable=%p socketCount=%d socketPort=%d socketListening=%d socketFd=%d listenSuppressed=%d wsa=%d mapList=%p] materials[system=%p default=%p gate=%p gateValue=%d fake=%p hooked=%d]\n",
		reason ? reason : "<none>",
		R1OHookGlobalValue<int>(0x6DADC0, -1),
		R1OHookGlobalValue<int>(0x6DADC4, -1),
		mapName[0] ? mapName : "<empty>",
		R1OHookGlobalValue<int>(0x6DADC8, -1),
		R1OHookGlobalValue<unsigned char>(0x6DADC6, static_cast<unsigned char>(0)),
		reinterpret_cast<void*>(appFrameState),
		appUnknown8,
		appCurrent,
		appTarget,
		R1OHookGlobalValue<unsigned char>(0x22FAE8A, static_cast<unsigned char>(0)),
		static_cast<int>(R1OHookGlobalValue<unsigned short>(0x2998378, static_cast<unsigned short>(0))),
		R1OHookGlobalValue<void*>(0x22FB238, nullptr),
		R1OHookGlobalValue<int>(0x2659558, -1),
		R1OHookGlobalValue<int>(0x2497BD4, -1),
		static_cast<int>(R1OHookGlobalValue<unsigned char>(0x22FB57A, static_cast<unsigned char>(0))),
		R1OHookGlobalValue<int>(0x265971C, -1),
		R1OHookGlobalValue<int>(0x265955C, -1),
		R1OHookGlobalValue<void*>(0x2A73EB8, nullptr),
		R1OHookGlobalValue<int>(0x2A73ED0, -1),
		[]() -> int {
			void* table = R1OHookGlobalValue<void*>(0x2A73EB8, nullptr);
			const int index = R1OHookGlobalValue<int>(0x265955C, -1);
			const auto entry = reinterpret_cast<unsigned char*>(table) + 16LL * index;
			return (table && index >= 0 && IsReadableRange(entry, 16)) ? *reinterpret_cast<unsigned short*>(entry) : -1;
		}(),
		[]() -> int {
			void* table = R1OHookGlobalValue<void*>(0x2A73EB8, nullptr);
			const int index = R1OHookGlobalValue<int>(0x265955C, -1);
			const auto entry = reinterpret_cast<unsigned char*>(table) + 16LL * index;
			return (table && index >= 0 && IsReadableRange(entry, 16)) ? static_cast<int>(entry[4]) : -1;
		}(),
		[]() -> int {
			void* table = R1OHookGlobalValue<void*>(0x2A73EB8, nullptr);
			const int index = R1OHookGlobalValue<int>(0x265955C, -1);
			const auto entry = reinterpret_cast<unsigned char*>(table) + 16LL * index;
			return (table && index >= 0 && IsReadableRange(entry, 16)) ? *reinterpret_cast<int*>(entry + 12) : -1;
		}(),
		static_cast<int>(R1OHookGlobalValue<unsigned char>(0x6B908F, static_cast<unsigned char>(0))),
		R1OHookGlobalValue<int>(0x22FB5A4, 0),
		R1OHookGlobalValue<void*>(0x24D1680, nullptr),
		R1OHookGlobalValue<void*>(0x22FB6A8, nullptr),
		R1OHookGlobalValue<void*>(0x22ED080, nullptr),
		materialGate,
		materialGateValue,
		GetR1OFakeDedicatedMaterial(),
		static_cast<int>(s_R1OMaterialLoadHooked));
	OutputDebugStringA(buffer);
}

static void LogR1OClientSlotSnapshot(const char* reason, bool force)
{
	if (!IsR1ODedicatedServer())
		return;

	if (!force) {
		if (s_R1OClientSlotLogBudget <= 0)
			return;
		--s_R1OClientSlotLogBudget;
	}

	const int maxClients = R1OHookGlobalValue<int>(0x265971C, 0);
	const uintptr_t clientArray = reinterpret_cast<uintptr_t>(engineR1O) + kR1OClientArrayRva;

	int activeSlots = 0;
	int connectedSlots = 0;
	int newSlots = 0;
	int prespawnSlots = 0;
	int spawnedSlots = 0;
	int fullSlots = 0;
	char slots[1024] = {};
	size_t used = 0;

	const int limit = maxClients > 0 && maxClients < 64 ? maxClients : 0;
	for (int i = 0; i < limit; ++i) {
		const uintptr_t client = clientArray + kR1OClientStride * static_cast<size_t>(i);
		if (!IsReadableRange(reinterpret_cast<void*>(client), 2048))
			continue;

		const uintptr_t vtable = *reinterpret_cast<uintptr_t*>(client);
		const int state = *reinterpret_cast<int*>(client + kR1OClientSignonStateOffset);
		const bool isKnownClient = vtable == reinterpret_cast<uintptr_t>(engineR1O) + kR1OCGameClientVtableRva;
		const bool spawned = IsReadableRange(reinterpret_cast<void*>(client + kR1OClientSpawnedOffset), 1)
			&& *reinterpret_cast<unsigned char*>(client + kR1OClientSpawnedOffset) != 0;
		const bool hasNetChan = IsReadableRange(reinterpret_cast<void*>(client + kR1OClientNetChanOffset), sizeof(void*))
			&& *reinterpret_cast<void**>(client + kR1OClientNetChanOffset) != nullptr;
		const bool pendingServerInfo = IsReadableRange(reinterpret_cast<void*>(client + kR1OClientPendingServerInfoOffset), 1)
			&& *reinterpret_cast<unsigned char*>(client + kR1OClientPendingServerInfoOffset) != 0;
		const char* name = reinterpret_cast<const char*>(client + 148);
		if (!IsReadableCString(name))
			name = "<bad>";

		if (state > 0 || spawned || hasNetChan) {
			++activeSlots;
			if (state >= 2)
				++connectedSlots;
			if (state == 3)
				++newSlots;
			if (state == 4)
				++prespawnSlots;
			if (state >= 5 || spawned)
				++spawnedSlots;
			if (state >= 6)
				++fullSlots;

			const int written = _snprintf_s(
				slots + used,
				sizeof(slots) - used,
				_TRUNCATE,
				"%s#%d{state=%d net=%d pendingInfo=%d spawned=%d known=%d name=%s}",
				used ? " " : "",
				i,
				state,
				static_cast<int>(hasNetChan),
				static_cast<int>(pendingServerInfo),
				static_cast<int>(spawned),
				static_cast<int>(isKnownClient),
				name);
			if (written < 0)
				break;
			used += static_cast<size_t>(written);
			if (used >= sizeof(slots) - 1)
				break;
		}
	}

	char buffer[1400];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O client-slots reason=%s max=%d active=%d connected=%d new=%d prespawn=%d spawned=%d full=%d array=%p slots=%s\n",
		reason ? reason : "<none>",
		maxClients,
		activeSlots,
		connectedSlots,
		newSlots,
		prespawnSlots,
		spawnedSlots,
		fullSlots,
		reinterpret_cast<void*>(clientArray),
		slots[0] ? slots : "<none>");
	OutputDebugStringA(buffer);
}

static bool HasR1OFullNetworkClient()
{
	if (!IsR1ODedicatedServer() || !engineR1O)
		return false;

	const int maxClients = R1OHookGlobalValue<int>(0x265971C, 0);
	const int limit = maxClients > 0 && maxClients < 64 ? maxClients : 0;
	if (limit <= 0)
		return false;

	const uintptr_t clientArray = reinterpret_cast<uintptr_t>(engineR1O) + kR1OClientArrayRva;
	for (int i = 0; i < limit; ++i) {
		const uintptr_t client = clientArray + kR1OClientStride * static_cast<size_t>(i);
		if (!IsReadableRange(reinterpret_cast<void*>(client), 2048))
			continue;

		const uintptr_t vtable = *reinterpret_cast<uintptr_t*>(client);
		if (vtable != reinterpret_cast<uintptr_t>(engineR1O) + kR1OCGameClientVtableRva)
			continue;

		const int state = *reinterpret_cast<int*>(client + kR1OClientSignonStateOffset);
		void* netChan = IsReadableRange(reinterpret_cast<void*>(client + kR1OClientNetChanOffset), sizeof(void*))
			? *reinterpret_cast<void**>(client + kR1OClientNetChanOffset)
			: nullptr;
		if (state >= 8 && netChan)
			return true;
	}

	return false;
}

static void MarkR1OServerInfoPendingForConnectedClients(const char* reason)
{
	if (!IsR1ODedicatedServer() || !engineR1O)
		return;

	const int maxClients = R1OHookGlobalValue<int>(0x265971C, 0);
	const int limit = maxClients > 0 && maxClients < 64 ? maxClients : 0;
	if (limit <= 0)
		return;

	const uintptr_t clientArray = reinterpret_cast<uintptr_t>(engineR1O) + kR1OClientArrayRva;
	for (int i = 0; i < limit; ++i) {
		const uintptr_t client = clientArray + kR1OClientStride * static_cast<size_t>(i);
		if (!IsReadableRange(reinterpret_cast<void*>(client), 2048))
			continue;

		const uintptr_t vtable = *reinterpret_cast<uintptr_t*>(client);
		if (vtable != reinterpret_cast<uintptr_t>(engineR1O) + kR1OCGameClientVtableRva)
			continue;

		const int state = *reinterpret_cast<int*>(client + kR1OClientSignonStateOffset);
		void* netChan = IsReadableRange(reinterpret_cast<void*>(client + kR1OClientNetChanOffset), sizeof(void*))
			? *reinterpret_cast<void**>(client + kR1OClientNetChanOffset)
			: nullptr;
		if (state != 2 || !netChan)
			continue;
		EnsureR1ONetChanSendNetMsgHook(reinterpret_cast<__int64>(netChan));

		unsigned char* pendingServerInfo = reinterpret_cast<unsigned char*>(client + kR1OClientPendingServerInfoOffset);
		if (!IsReadableRange(pendingServerInfo, sizeof(*pendingServerInfo)))
			continue;

		const unsigned char previousPending = *pendingServerInfo;
		*pendingServerInfo = 1;

		if (s_R1OServerInfoForceLogBudget > 0) {
			--s_R1OServerInfoForceLogBudget;
			char buffer[512];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: R1O marked SendServerInfo pending reason=%s slot=%d client=%p fullClient=%p state=%d netchan=%p pendingWas=%u pendingNow=%u budget=%d\n",
				reason ? reason : "<none>",
				i,
				reinterpret_cast<void*>(client),
				reinterpret_cast<void*>(static_cast<__int64>(client) - kR1OClientSubobjectOffset),
				state,
				netChan,
				static_cast<unsigned int>(previousPending),
				static_cast<unsigned int>(*pendingServerInfo),
				s_R1OServerInfoForceLogBudget);
			OutputDebugStringA(buffer);
		}
	}
}

static __int64 __fastcall R1OHostSetActiveClientList(__int64 list, unsigned char active)
{
	if (!IsR1ODedicatedServer())
		return R1OHostSetActiveClientListOriginal ? R1OHostSetActiveClientListOriginal(list, active) : 0;

	int count = 0;
	void** entries = nullptr;
	int skipped = 0;
	int notified = 0;
	int clientNotified = 0;

	if (list && IsReadableRange(reinterpret_cast<void*>(list + 32), sizeof(int)))
		count = *reinterpret_cast<int*>(list + 32);
	if (list && IsReadableRange(reinterpret_cast<void*>(list + 8), sizeof(void*)))
		entries = *reinterpret_cast<void***>(list + 8);

	if (count > 0 && count < 4096 && entries && IsReadableRange(entries, sizeof(void*) * count)) {
		for (int i = 0; i < count; ++i) {
			void* entry = entries[i];
			if (!entry || !IsReadableRange(entry, 144)) {
				++skipped;
				continue;
			}

			auto* disabled = reinterpret_cast<unsigned char*>(reinterpret_cast<uintptr_t>(entry) + 128);
			if (!IsReadableRange(disabled, sizeof(*disabled))) {
				++skipped;
				continue;
			}
			if (*disabled)
				continue;

			auto* sinkSlot = reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(entry) + 136);
			if (!IsReadableRange(sinkSlot, sizeof(*sinkSlot))) {
				++skipped;
				continue;
			}

			void* sink = *sinkSlot;
			if (!sink || !IsReadableRange(sink, sizeof(void*))) {
				++skipped;
				continue;
			}

			void** vtable = *reinterpret_cast<void***>(sink);
			if (!vtable || !IsReadableRange(vtable, sizeof(void*) * 8) || !vtable[7]) {
				++skipped;
				continue;
			}

			using NotifyActiveType = void(__fastcall*)(void* thisptr, unsigned char active);
			reinterpret_cast<NotifyActiveType>(vtable[7])(sink, active);
			++notified;
		}
	}
	else if (count > 0) {
		skipped = count;
	}

	if (engineR1O) {
		const int maxClients = R1OHookGlobalValue<int>(0x265971C, 0);
		const int limit = maxClients > 0 && maxClients < 64 ? maxClients : 0;
		const uintptr_t clientArray = reinterpret_cast<uintptr_t>(engineR1O) + kR1OClientArrayRva;
		for (int i = 0; i < limit; ++i) {
			const uintptr_t client = clientArray + kR1OClientStride * static_cast<size_t>(i);
			if (!IsReadableRange(reinterpret_cast<void*>(client), 2048)) {
				++skipped;
				continue;
			}

			const uintptr_t vtable = *reinterpret_cast<uintptr_t*>(client);
			const bool knownClient = vtable == reinterpret_cast<uintptr_t>(engineR1O) + kR1OCGameClientVtableRva;
			if (!knownClient)
				continue;

			const int state = IsReadableRange(reinterpret_cast<void*>(client + kR1OClientSignonStateOffset), sizeof(int))
				? *reinterpret_cast<int*>(client + kR1OClientSignonStateOffset)
				: 0;
			if (state != 5)
				continue;

			const uintptr_t fullClient = client - kR1OClientSubobjectOffset;
			if (!IsReadableRange(reinterpret_cast<void*>(fullClient), sizeof(void*))) {
				++skipped;
				continue;
			}

			void** fullVtable = *reinterpret_cast<void***>(fullClient);
			if (!fullVtable || !IsReadableRange(fullVtable, sizeof(void*) * 4) || !fullVtable[3]) {
				++skipped;
				continue;
			}

			using NotifyClientType = void(__fastcall*)(void* thisptr);
			reinterpret_cast<NotifyClientType>(fullVtable[3])(reinterpret_cast<void*>(fullClient));
			++clientNotified;
		}
	}

	void* target = R1OHookGlobalValue<void*>(0x22FB0F0, nullptr);
	__int64 result = 0;
	if (target && IsReadableRange(target, sizeof(void*))) {
		void** vtable = *reinterpret_cast<void***>(target);
		if (vtable && IsReadableRange(vtable, sizeof(void*) * 5) && vtable[4]) {
			using FinalActiveType = __int64(__fastcall*)(void* thisptr, unsigned char active);
			result = reinterpret_cast<FinalActiveType>(vtable[4])(target, active);
		}
	}

	if ((skipped > 0 || AreR1OFakeDediVerboseLogsEnabled()) && s_R1OHibernateListGuardLogBudget > 0) {
		--s_R1OHibernateListGuardLogBudget;
		char buffer[512];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: guarded R1O host active client list list=%p active=%u count=%d entries=%p notified=%d clientNotified=%d skipped=%d target=%p result=%lld budget=%d\n",
			reinterpret_cast<void*>(list),
			static_cast<unsigned int>(active),
			count,
			entries,
			notified,
			clientNotified,
			skipped,
			target,
			static_cast<long long>(result),
			s_R1OHibernateListGuardLogBudget);
		OutputDebugStringA(buffer);
	}

	return result;
}

static __int64 __fastcall R1OHostShutdownClientList(__int64 list)
{
	if (!IsR1ODedicatedServer())
		return R1OHostShutdownClientListOriginal ? R1OHostShutdownClientListOriginal(list) : 0;

	int count = 0;
	void** entries = nullptr;
	int skipped = 0;
	int notified = 0;

	if (list && IsReadableRange(reinterpret_cast<void*>(list + 32), sizeof(int)))
		count = *reinterpret_cast<int*>(list + 32);
	if (list && IsReadableRange(reinterpret_cast<void*>(list + 8), sizeof(void*)))
		entries = *reinterpret_cast<void***>(list + 8);

	if (count > 0 && count < 4096 && entries && IsReadableRange(entries, sizeof(void*) * count)) {
		for (int i = 0; i < count; ++i) {
			void* entry = entries[i];
			if (!entry || !IsReadableRange(entry, 144)) {
				++skipped;
				continue;
			}

			if (*reinterpret_cast<unsigned char*>(reinterpret_cast<uintptr_t>(entry) + 128))
				continue;

			void* sink = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(entry) + 136);
			if (!sink || !IsReadableRange(sink, sizeof(void*))) {
				++skipped;
				continue;
			}

			void** vtable = *reinterpret_cast<void***>(sink);
			if (!vtable || !IsReadableRange(vtable, sizeof(void*) * 9) || !vtable[8]) {
				++skipped;
				continue;
			}

			using NotifyShutdownType = void(__fastcall*)(void* thisptr);
			reinterpret_cast<NotifyShutdownType>(vtable[8])(sink);
			++notified;
		}
	}
	else if (count > 0) {
		skipped = count;
	}

	void* target = R1OHookGlobalValue<void*>(0x22FB0F0, nullptr);
	__int64 result = 0;
	if (target && IsReadableRange(target, sizeof(void*))) {
		void** vtable = *reinterpret_cast<void***>(target);
		if (vtable && IsReadableRange(vtable, sizeof(void*) * 7) && vtable[6]) {
			using FinalShutdownType = __int64(__fastcall*)(void* thisptr);
			result = reinterpret_cast<FinalShutdownType>(vtable[6])(target);
		}
	}

	char buffer[384];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: guarded R1O host shutdown client list list=%p count=%d entries=%p notified=%d skipped=%d target=%p result=%lld\n",
		reinterpret_cast<void*>(list),
		count,
		entries,
		notified,
		skipped,
		target,
		static_cast<long long>(result));
	OutputDebugStringA(buffer);
	return result;
}

static bool R1OCommandBaseIsCommand(__int64 commandBase);

static __int64 LookupR1OCommandBaseForDebug(const char* name)
{
	if (!IsReadableCString(name))
		return 0;

	void** cvarGlobal = R1OHookGlobal<void*>(0x22FB648);
	void* cvar = cvarGlobal && IsReadableRange(cvarGlobal, sizeof(void*)) ? *cvarGlobal : nullptr;
	if (!cvar || !IsReadableRange(cvar, sizeof(void*)))
		return 0;

	void** vtable = *reinterpret_cast<void***>(cvar);
	if (!IsReadableRange(vtable, sizeof(void*) * 17))
		return 0;

	using FindCommandBaseType = __int64(__fastcall*)(void* thisptr, const char* name, const char* breakSet);
	auto findCommandBase = reinterpret_cast<FindCommandBaseType>(vtable[16]);
	return findCommandBase ? findCommandBase(cvar, name, "[$&*,`]") : 0;
}

static void* GetR1OCommandCallbackForDebug(__int64 commandBase)
{
	if (!commandBase || !IsReadableRange(reinterpret_cast<void*>(commandBase), sizeof(ConCommandR1)))
		return nullptr;

	if (!R1OCommandBaseIsCommand(commandBase))
		return nullptr;

	return reinterpret_cast<ConCommandR1*>(commandBase)->m_pCommandCallback;
}

static bool R1OCommandBaseIsCommand(__int64 commandBase)
{
	if (!commandBase || !IsReadableRange(reinterpret_cast<void*>(commandBase), sizeof(void*)))
		return false;

	void** vtable = *reinterpret_cast<void***>(commandBase);
	if (!IsReadableRange(vtable, sizeof(void*) * 3))
		return false;

	using IsCommandType = unsigned char(__fastcall*)(__int64 thisptr);
	auto isCommand = reinterpret_cast<IsCommandType>(vtable[1]);
	return isCommand && isCommand(commandBase) != 0;
}

static bool R1OCommandBaseHasFlag(__int64 commandBase, __int64 flag)
{
	if (!commandBase || !IsReadableRange(reinterpret_cast<void*>(commandBase), sizeof(void*)))
		return false;

	void** vtable = *reinterpret_cast<void***>(commandBase);
	if (!IsReadableRange(vtable, sizeof(void*) * 3))
		return false;

	using IsFlagSetType = unsigned char(__fastcall*)(__int64 thisptr, __int64 flag);
	auto isFlagSet = reinterpret_cast<IsFlagSetType>(vtable[2]);
	return isFlagSet && isFlagSet(commandBase, flag) != 0;
}

static void RegisterR1OStaticConCommandsForDedi()
{
	if (!IsR1ODedicatedServer() || s_R1OStaticConCommandsRegistered || !cvarinterface || !OriginalCCVar_RegisterConCommand)
		return;

	auto head = R1OHookGlobal<ConCommandBaseR1O*>(0x23007D0);
	ConCommandBaseR1O* node = head && IsReadableRange(head, sizeof(*head)) ? *head : nullptr;
	int registered = 0;
	int skipped = 0;
	int interesting = 0;

	for (int guard = 0; node && guard < 20000; ++guard) {
		if (!IsReadableRange(node, sizeof(ConCommandBaseR1O))) {
			++skipped;
			break;
		}

		ConCommandBaseR1O* next = node->m_pNext;
		if (IsReadableCString(node->m_pszName)) {
			if (IsR1OInterestingCommandName(node->m_pszName) || !_stricmp(node->m_pszName, "exec"))
				++interesting;
			CCVar_RegisterConCommand(cvarinterface, node);
			++registered;
		}
		else {
			++skipped;
		}

		node = next;
	}

	s_R1OStaticConCommandsRegistered = true;
	RegisterR1ODediDeltaConVars();

	char buffer[384];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: registered R1O static ConCommandBase list into wrapped cvar registered=%d skipped=%d interesting=%d head=%p\n",
		registered,
		skipped,
		interesting,
		head ? *head : nullptr);
	OutputDebugStringA(buffer);
}

static const char* GetR1ODediFallbackBaseDirectory();
static bool BuildR1ODediLooseModPath(const char* relativePath, char* outPath, size_t outPathSize);
static bool QueueR1OCommandBufferText(int index, const char* text);
static void ExecuteR1OCommandBuffers(const char* reason);
static void QueueR1ODediRequestedDummyBot(int frameCount);
static std::set<std::string> s_R1ODediExecFallbackQueued;

static bool EnsureDirectoryExists(const char* path)
{
	if (!path || !path[0])
		return false;

	const DWORD attributes = GetFileAttributesA(path);
	if (attributes != INVALID_FILE_ATTRIBUTES)
		return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

	if (CreateDirectoryA(path, nullptr))
		return true;
	return GetLastError() == ERROR_ALREADY_EXISTS;
}

static bool WriteR1OBanConfigAtomic(
	const char* fileName,
	const std::string& contents,
	char* outputPath,
	size_t outputPathSize,
	DWORD* errorCode)
{
	if (errorCode)
		*errorCode = ERROR_SUCCESS;
	if (!fileName || !fileName[0] || !outputPath || outputPathSize == 0) {
		if (errorCode)
			*errorCode = ERROR_INVALID_PARAMETER;
		return false;
	}

	char modDirectory[MAX_PATH];
	char cfgDirectory[MAX_PATH];
	_snprintf_s(
		modDirectory,
		sizeof(modDirectory),
		_TRUNCATE,
		"%s\\r1delta",
		GetR1ODediFallbackBaseDirectory());
	_snprintf_s(
		cfgDirectory,
		sizeof(cfgDirectory),
		_TRUNCATE,
		"%s\\cfg",
		modDirectory);
	_snprintf_s(
		outputPath,
		outputPathSize,
		_TRUNCATE,
		"%s\\%s",
		cfgDirectory,
		fileName);

	if (!EnsureDirectoryExists(modDirectory) || !EnsureDirectoryExists(cfgDirectory)) {
		if (errorCode)
			*errorCode = GetLastError();
		return false;
	}

	char temporaryPath[MAX_PATH];
	_snprintf_s(
		temporaryPath,
		sizeof(temporaryPath),
		_TRUNCATE,
		"%s.tmp.%lu",
		outputPath,
		static_cast<unsigned long>(GetCurrentProcessId()));

	std::ofstream file(temporaryPath, std::ios::out | std::ios::binary | std::ios::trunc);
	if (!file) {
		if (errorCode)
			*errorCode = GetLastError();
		return false;
	}
	file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
	file.close();
	if (file.fail()) {
		if (errorCode)
			*errorCode = ERROR_WRITE_FAULT;
		DeleteFileA(temporaryPath);
		return false;
	}

	if (!MoveFileExA(
			temporaryPath,
			outputPath,
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		if (errorCode)
			*errorCode = GetLastError();
		DeleteFileA(temporaryPath);
		return false;
	}
	return true;
}

static bool ShouldAutosaveR1ONativeBans()
{
	ConVarR1O* autosave = cvarinterface
		? CCVar_FindVar(cvarinterface, "sv_banlist_autosave")
		: nullptr;
	return autosave && autosave->m_Value.m_nValue != 0;
}

static bool WriteR1ONativeUserBans(bool announce = true)
{
	const R1ONativeBanState state =
		CaptureR1ONativeBanState(0x264D1B0, 0x264D1C8, sizeof(R1ONativeUserBanEntry), 28);
	if (!state.valid) {
		Status_ConMsg("writeid: native R1O user ban list could not be inspected.\n");
		return false;
	}

	std::string contents = "sv_banlist_autosave 0\r\n";
	int written = 0;
	int skipped = 0;
	for (int index = 0; index < state.count; ++index) {
		R1ONativeUserBanEntry entry = {};
		if (!ReadR1ONativeUserBanEntry(index, &entry)
			|| entry.durationMinutes != 0.0f)
			continue;

		char networkId[192];
		if (!FormatR1ONativeNetworkId(&entry, networkId, sizeof(networkId))) {
			++skipped;
			continue;
		}
		contents += "banid 0 ";
		contents += networkId;
		contents += "\r\n";
		++written;
	}
	contents += "sv_banlist_autosave 1\r\n";

	char path[MAX_PATH];
	DWORD errorCode = ERROR_SUCCESS;
	if (!WriteR1OBanConfigAtomic(
			"banned_user.cfg",
			contents,
			path,
			sizeof(path),
			&errorCode)) {
		Status_ConMsg("writeid: failed to write cfg/banned_user.cfg (Win32 error %lu).\n", errorCode);
		return false;
	}

	if (announce) {
		Status_ConMsg(
			"writeid: wrote %d permanent user ban%s to %s%s.\n",
			written,
			written == 1 ? "" : "s",
			path,
			skipped ? " (unsupported IDs skipped)" : "");
	}
	return true;
}

static bool WriteR1ONativeIpBans(bool announce = true)
{
	const R1ONativeBanState state =
		CaptureR1ONativeBanState(0x264D1D8, 0x264D1F0, sizeof(R1ONativeIpBanEntry), 12);
	if (!state.valid) {
		Status_ConMsg("writeip: native R1O IP ban list could not be inspected.\n");
		return false;
	}

	std::string contents = "sv_banlist_autosave 0\r\n";
	int written = 0;
	for (int index = 0; index < state.count; ++index) {
		R1ONativeIpBanEntry entry = {};
		if (!ReadR1ONativeIpBanEntry(index, &entry)
			|| entry.durationMinutes != 0.0f)
			continue;

		const unsigned char* address = reinterpret_cast<const unsigned char*>(&entry.address);
		char line[64];
		_snprintf_s(
			line,
			sizeof(line),
			_TRUNCATE,
			"addip 0 %u.%u.%u.%u\r\n",
			address[0],
			address[1],
			address[2],
			address[3]);
		contents += line;
		++written;
	}
	contents += "sv_banlist_autosave 1\r\n";

	char path[MAX_PATH];
	DWORD errorCode = ERROR_SUCCESS;
	if (!WriteR1OBanConfigAtomic(
			"banned_ip.cfg",
			contents,
			path,
			sizeof(path),
			&errorCode)) {
		Status_ConMsg("writeip: failed to write cfg/banned_ip.cfg (Win32 error %lu).\n", errorCode);
		return false;
	}

	if (announce) {
		Status_ConMsg(
			"writeip: wrote %d permanent IP ban%s to %s.\n",
			written,
			written == 1 ? "" : "s",
			path);
	}
	return true;
}

static bool IsSafeR1OCfgExecName(const char* name)
{
	if (!IsReadableCString(name) || !name[0])
		return false;
	if (strstr(name, "..") || strchr(name, ':') || name[0] == '/' || name[0] == '\\')
		return false;
	return true;
}

static bool IsR1ODediOwnedBanConfigExec(const char* cfgName)
{
	if (!IsSafeR1OCfgExecName(cfgName)
		|| strchr(cfgName, '/')
		|| strchr(cfgName, '\\'))
		return false;

	char baseName[MAX_PATH];
	_snprintf_s(baseName, sizeof(baseName), _TRUNCATE, "%s", cfgName);
	const size_t length = strlen(baseName);
	if (length > 4 && !_stricmp(baseName + length - 4, ".cfg"))
		baseName[length - 4] = '\0';

	return !_stricmp(baseName, "banned_user")
		|| !_stricmp(baseName, "banned_ip");
}

static void QueueR1ODediExecFallback(const char* cfgName)
{
	if (!IsR1ODedicatedServer() || !IsSafeR1OCfgExecName(cfgName))
		return;

	const char* lastSlash = strrchr(cfgName, '/');
	const char* lastBackslash = strrchr(cfgName, '\\');
	const char* lastSeparator = lastSlash;
	if (!lastSeparator || (lastBackslash && lastBackslash > lastSeparator))
		lastSeparator = lastBackslash;
	const char* lastDot = strrchr(cfgName, '.');
	const bool hasExtension = lastDot && (!lastSeparator || lastDot > lastSeparator);

	char relativePath[MAX_PATH];
	_snprintf_s(
		relativePath,
		sizeof(relativePath),
		_TRUNCATE,
		"cfg\\%s%s",
		cfgName,
		hasExtension ? "" : ".cfg");

	char loosePath[MAX_PATH];
	if (!BuildR1ODediLooseModPath(relativePath, loosePath, sizeof(loosePath)))
		return;

	std::ifstream file(loosePath, std::ios::in | std::ios::binary);
	if (!file)
		return;

	std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	if (content.empty())
		return;

	std::string key = loosePath;
	for (char& ch : key)
		ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
	if (!s_R1ODediExecFallbackQueued.insert(key).second)
		return;

	if (content.back() != '\n')
		content.push_back('\n');

	char buffer[512];
	_snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "R1Delta: R1O exec loose cfg fallback queue cfg=%s path=%s bytes=%zu\n", cfgName, loosePath, content.size());
	OutputDebugStringA(buffer);
	QueueR1OCommandBufferText(1, content.c_str());
}

static __int64 __fastcall R1OCbuf_Dispatch(__int64 context, __int64 command, int source, int flags)
{
	if (!IsR1ODedicatedServer())
		return R1OCbuf_DispatchOriginal
			? R1OCbuf_DispatchOriginal(context, command, source, flags)
			: 0;

	const char* name = GetR1OCCommandArg(command, 0);
	const int argc = IsReadableRange(reinterpret_cast<const void*>(command), sizeof(int))
		? *reinterpret_cast<const int*>(command)
		: 0;
	const bool interesting = IsR1OInterestingCommandName(name);
	const __int64 commandBase = interesting ? LookupR1OCommandBaseForDebug(name) : 0;
	char nameCopy[128] = {};
	char arg1Copy[128] = {};
	char textCopy[512] = {};
	if (interesting) {
		CopyReadableStringForDebug(name, nameCopy, sizeof(nameCopy));
		CopyReadableStringForDebug(GetR1OCCommandArg(command, 1), arg1Copy, sizeof(arg1Copy));
		CopyReadableStringForDebug(GetR1OCCommandString(command), textCopy, sizeof(textCopy));
	}
	const bool isDedicatedNetStart =
		interesting
		&& nameCopy[0]
		&& !_stricmp(nameCopy, "net_start");
	const unsigned int netListenInvocationBefore =
		s_R1ONetListenInvocationCount;
	if (interesting) {
		void* callback = GetR1OCommandCallbackForDebug(commandBase);
		const uintptr_t engineBase = reinterpret_cast<uintptr_t>(engineR1O);
		const uintptr_t callbackRva = callback && engineBase
			? reinterpret_cast<uintptr_t>(callback) - engineBase
			: 0;
		char buffer[1024];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O command dispatch enter ctx=%p command=%p source=%d flags=%d argc=%d name=%s arg1=%s text=%s cmdBase=%p isCmd=%d callback=%p callbackRva=%p flags[cheat=%d client=%d serverCan=%d material=%d restricted=%d] state[mode=%d sp=%d splits=%lld wait=%d defer=%d serverClients=%p]\n",
			reinterpret_cast<void*>(context),
			reinterpret_cast<void*>(command),
			source,
			flags,
			argc,
			nameCopy[0] ? nameCopy : "<null>",
			arg1Copy,
			textCopy,
			reinterpret_cast<void*>(commandBase),
			static_cast<int>(R1OCommandBaseIsCommand(commandBase)),
			callback,
			reinterpret_cast<void*>(callbackRva),
			static_cast<int>(R1OCommandBaseHasFlag(commandBase, 2)),
			static_cast<int>(R1OCommandBaseHasFlag(commandBase, 4)),
			static_cast<int>(R1OCommandBaseHasFlag(commandBase, 64)),
			static_cast<int>(R1OCommandBaseHasFlag(commandBase, 0x4000)),
			static_cast<int>(R1OCommandBaseHasFlag(commandBase, 0x10000000)),
			R1OHookGlobalValue<int>(0x2659558, -1),
			R1OHookGlobalValue<int>(0x2497BD4, -1),
			static_cast<long long>(R1OHookGlobalValue<__int64>(0x265971C, -1)),
			R1OHookGlobalValue<int>(0x22EF51C, -1),
			R1OHookGlobalValue<int>(0x22EF4FC, -1),
			R1OHookGlobalValue<void*>(0x22FB238, nullptr));
		OutputDebugStringA(buffer);
	}

	const bool handledDedicatedEntFire =
		interesting
		&& name
		&& !_stricmp(name, "ent_fire")
		&& TryHandleR1ODedicatedConsoleEntFire(
			source,
			argc,
			reinterpret_cast<const char* const*>(command + 1040));
	const bool handledDedicatedHelp =
		interesting
		&& name
		&& !_stricmp(name, "help")
		&& argc == 2
		&& PrintR1OConVarDescriptionByName(GetR1OCCommandArg(command, 1));
	const bool handledDedicatedBanConfigExec =
		interesting
		&& name
		&& !_stricmp(name, "exec")
		&& argc == 2
		&& IsR1ODediOwnedBanConfigExec(GetR1OCCommandArg(command, 1));
	const bool handledDedicatedFind =
		interesting
		&& name
		&& !_stricmp(name, "find");
	const bool handledDedicatedEcho =
		interesting
		&& name
		&& !_stricmp(name, "echo");
	const bool handledDedicatedListId =
		interesting
		&& name
		&& !_stricmp(name, "listid");
	const bool handledDedicatedListIp =
		interesting
		&& name
		&& !_stricmp(name, "listip");
	const bool handledDedicatedWriteId =
		interesting
		&& name
		&& !_stricmp(name, "writeid");
	const bool handledDedicatedWriteIp =
		interesting
		&& name
		&& !_stricmp(name, "writeip");
	const bool handledDedicatedRemoveId =
		interesting
		&& name
		&& !_stricmp(name, "removeid");
	const bool handledDedicatedRemoveAllIds =
		interesting
		&& name
		&& !_stricmp(name, "removeallids");
	const bool handledDedicatedRemoveAllIps =
		interesting
		&& name
		&& !_stricmp(name, "removeallips");

	char kickTarget[128] = {};
	char idTarget[128] = {};
	char ipTarget[128] = {};
	char normalizedIpTarget[INET_ADDRSTRLEN] = {};
	int ipArgumentIndex = -1;
	R1OClientCommandIdentity kickIdentity = {};
	bool kickIdentityFound = false;
	if (name && !_stricmp(name, "kick") && argc > 1) {
		CopyR1ONormalizedKickName(command, kickTarget, sizeof(kickTarget));
		if (!kickTarget[0])
			CopyReadableStringForDebug(GetR1OCCommandArg(command, 1), kickTarget, sizeof(kickTarget));
		kickIdentityFound = FindR1OClientByName(kickTarget, &kickIdentity);
	}
	else if (name && !_stricmp(name, "kickid") && argc > 1) {
		BuildR1OSteamIdArgument(command, 1, idTarget, sizeof(idTarget));
		kickIdentityFound = FindR1OClientById(idTarget, &kickIdentity);
	}
	else if (name && !_stricmp(name, "banid") && argc > 2) {
		BuildR1OSteamIdArgument(command, 2, idTarget, sizeof(idTarget));
	}
	else if (name && !_stricmp(name, "removeid") && argc > 1) {
		BuildR1OSteamIdArgument(command, 1, idTarget, sizeof(idTarget));
	}
	else if (name && (!_stricmp(name, "banip") || !_stricmp(name, "addip")) && argc > 2) {
		CopyReadableStringForDebug(GetR1OCCommandArg(command, 2), ipTarget, sizeof(ipTarget));
		ipArgumentIndex = 2;
	}
	else if (name && !_stricmp(name, "removeip") && argc > 1) {
		CopyReadableStringForDebug(GetR1OCCommandArg(command, 1), ipTarget, sizeof(ipTarget));
		ipArgumentIndex = 1;
	}
	const bool hasNormalizedIpTarget =
		ipArgumentIndex >= 0
		&& NormalizeR1ONativeIpBanArgument(ipTarget, normalizedIpTarget, sizeof(normalizedIpTarget));
	if (hasNormalizedIpTarget)
		_snprintf_s(ipTarget, sizeof(ipTarget), _TRUNCATE, "%s", normalizedIpTarget);

	const bool mutatesUserBans =
		name && (!_stricmp(name, "banid")
			|| !_stricmp(name, "removeid")
			|| !_stricmp(name, "removeallids"));
	const bool mutatesIpBans =
		name && (!_stricmp(name, "banip")
			|| !_stricmp(name, "addip")
			|| !_stricmp(name, "removeip")
			|| !_stricmp(name, "removeallips"));
	const R1ONativeBanState userBansBefore = mutatesUserBans
		? CaptureR1ONativeBanState(0x264D1B0, 0x264D1C8, sizeof(R1ONativeUserBanEntry), 28)
		: R1ONativeBanState{};
	const R1ONativeBanState ipBansBefore = mutatesIpBans
		? CaptureR1ONativeBanState(0x264D1D8, 0x264D1F0, sizeof(R1ONativeIpBanEntry), 12)
		: R1ONativeBanState{};
	if (handledDedicatedRemoveId && argc > 1)
		TryRemoveR1ONativeUserBan(idTarget);
	if (handledDedicatedRemoveAllIds)
		TryClearR1ONativeBanList(0x264D1C8, 0x14A070);
	if (handledDedicatedRemoveAllIps)
		TryClearR1ONativeBanList(0x264D1F0, 0x14A010);

	if (handledDedicatedFind) {
		if (argc != 2)
			Status_ConMsg("Usage: find <string>\n");
		else if (!PrintR1ODediFindResults(GetR1OCCommandArg(command, 1)))
			Status_ConMsg("find: unable to enumerate commands and convars.\n");
	}
	if (handledDedicatedEcho)
		Status_ConMsg("%s\n", GetR1OCCommandArgS(command));
	if (handledDedicatedListId)
		PrintR1ONativeUserBanList();
	if (handledDedicatedListIp)
		PrintR1ONativeIpBanList();
	if (handledDedicatedWriteId)
		WriteR1ONativeUserBans();
	if (handledDedicatedWriteIp)
		WriteR1ONativeIpBans();
	if (handledDedicatedBanConfigExec)
		QueueR1ODediExecFallback(GetR1OCCommandArg(command, 1));

	const bool handledWithoutNativeDispatch =
		handledDedicatedEntFire
		|| handledDedicatedHelp
		|| handledDedicatedBanConfigExec
		|| handledDedicatedFind
		|| handledDedicatedEcho
		|| handledDedicatedListId
		|| handledDedicatedListIp
		|| handledDedicatedWriteId
		|| handledDedicatedWriteIp
		|| handledDedicatedRemoveId
		|| handledDedicatedRemoveAllIds
		|| handledDedicatedRemoveAllIps;
	const char** overriddenIpArgument = nullptr;
	const char* originalIpArgument = nullptr;
	if (!handledWithoutNativeDispatch
		&& hasNormalizedIpTarget
		&& IsReadableRange(
			reinterpret_cast<const void*>(command + 1040 + sizeof(const char*) * ipArgumentIndex),
			sizeof(const char*))) {
		overriddenIpArgument = reinterpret_cast<const char**>(
			command + 1040 + sizeof(const char*) * ipArgumentIndex);
		__try {
			originalIpArgument = *overriddenIpArgument;
			*overriddenIpArgument = normalizedIpTarget;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			overriddenIpArgument = nullptr;
			originalIpArgument = nullptr;
		}
	}

	const __int64 result = handledWithoutNativeDispatch
		? commandBase
		: (R1OCbuf_DispatchOriginal
			? R1OCbuf_DispatchOriginal(context, command, source, flags)
			: 0);

	// TFO creates the server VM while its initial script file-scope traversal
	// is still active.  The native map bootstrap executes server.cfg after that
	// traversal is complete.  Mark autorun ready only after the exec callback
	// returns so addon scripts can safely use nested IncludeFile calls.
	if (nameCopy[0]
		&& !_stricmp(nameCopy, "exec")
		&& arg1Copy[0]
		&& (!_stricmp(arg1Copy, "server.cfg") || !_stricmp(arg1Copy, "server"))) {
		MarkR1OServerAutorunBootstrapComplete();
	}

	if (overriddenIpArgument) {
		__try {
			*overriddenIpArgument = originalIpArgument;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
		}
	}

	// Native net_start normally reaches NET_ListenSocket through NET_Config.
	// Some TFO startup states short-circuit that path, so guarantee one reopen
	// after the native command while avoiding a duplicate when it did run.
	if (isDedicatedNetStart
		&& s_R1ONetListenInvocationCount == netListenInvocationBefore) {
		R1ONetListenSocket(s_R1ODediServerSocketIndex);
	}

	if (nameCopy[0] && !_stricmp(nameCopy, "kick")) {
		if (argc <= 1)
			Status_ConMsg("Usage: kick <name>\n");
		else if (kickIdentityFound)
			Status_ConMsg("Kick issued for \"%s\" (userid %d, %s).\n", kickIdentity.name, kickIdentity.userId, kickIdentity.networkId);
		else
			Status_ConMsg("kick: player \"%s\" was not found.\n", kickTarget);
	}
	else if (nameCopy[0] && !_stricmp(nameCopy, "kickid")) {
		if (argc <= 1)
			Status_ConMsg("Usage: kickid <userid | STEAM_x:y:z> [reason]\n");
		else if (kickIdentityFound)
			Status_ConMsg("Kick issued for \"%s\" (userid %d, %s).\n", kickIdentity.name, kickIdentity.userId, kickIdentity.networkId);
		else
			Status_ConMsg("kickid: player \"%s\" was not found.\n", idTarget);
	}
	else if (nameCopy[0] && !_stricmp(nameCopy, "banid")) {
		if (argc < 3 || argc > 8) {
			Status_ConMsg("Usage: banid <minutes> <userid | STEAM_x:y:z> [kick]\n");
		}
		else {
			const R1ONativeBanState after =
				CaptureR1ONativeBanState(0x264D1B0, 0x264D1C8, sizeof(R1ONativeUserBanEntry), 28);
			if (userBansBefore.valid && after.valid
				&& (userBansBefore.count != after.count || userBansBefore.hash != after.hash)) {
				Status_ConMsg("banid: native R1O user ban list updated for \"%s\" (%d total).\n", idTarget, after.count);
			}
			else {
				Status_ConMsg("banid: native R1O user ban list was unchanged for \"%s\".\n", idTarget);
			}
		}
	}
	else if (nameCopy[0] && !_stricmp(nameCopy, "removeid")) {
		if (argc <= 1) {
			Status_ConMsg("Usage: removeid <userid | STEAM_x:y:z>\n");
		}
		else {
			const R1ONativeBanState after =
				CaptureR1ONativeBanState(0x264D1B0, 0x264D1C8, sizeof(R1ONativeUserBanEntry), 28);
			if (userBansBefore.valid && after.valid && after.count < userBansBefore.count)
				Status_ConMsg("removeid: removed \"%s\" (%d remaining).\n", idTarget, after.count);
			else
				Status_ConMsg("removeid: no matching native R1O user ban for \"%s\".\n", idTarget);
		}
	}
	else if (nameCopy[0] && !_stricmp(nameCopy, "removeallids")) {
		const R1ONativeBanState after =
			CaptureR1ONativeBanState(0x264D1B0, 0x264D1C8, sizeof(R1ONativeUserBanEntry), 28);
		if (userBansBefore.valid && after.valid && after.count == 0)
			Status_ConMsg("removeallids: removed all %d user ban entr%s.\n", userBansBefore.count, userBansBefore.count == 1 ? "y" : "ies");
		else
			Status_ConMsg("removeallids: native R1O user ban list could not be cleared.\n");
	}
	else if (nameCopy[0] && (!_stricmp(nameCopy, "banip") || !_stricmp(nameCopy, "addip"))) {
		if (argc < 3) {
			Status_ConMsg("Usage: %s <minutes> <ip-address>\n", nameCopy);
		}
		else {
			const R1ONativeBanState after =
				CaptureR1ONativeBanState(0x264D1D8, 0x264D1F0, sizeof(R1ONativeIpBanEntry), 12);
			if (ipBansBefore.valid && after.valid
				&& (ipBansBefore.count != after.count || ipBansBefore.hash != after.hash)) {
				Status_ConMsg("%s: native R1O IP ban list updated for \"%s\" (%d total).\n", nameCopy, ipTarget, after.count);
			}
			else {
				Status_ConMsg("%s: native R1O IP ban list was unchanged for \"%s\".\n", nameCopy, ipTarget);
			}
		}
	}
	else if (nameCopy[0] && !_stricmp(nameCopy, "removeip")) {
		if (argc <= 1) {
			Status_ConMsg("Usage: removeip <ip-address | entry-number>\n");
		}
		else {
			const R1ONativeBanState after =
				CaptureR1ONativeBanState(0x264D1D8, 0x264D1F0, sizeof(R1ONativeIpBanEntry), 12);
			if (ipBansBefore.valid && after.valid && after.count < ipBansBefore.count)
				Status_ConMsg("removeip: removed \"%s\" (%d remaining).\n", ipTarget, after.count);
			else
				Status_ConMsg("removeip: no matching native R1O IP ban for \"%s\".\n", ipTarget);
		}
	}
	else if (nameCopy[0] && !_stricmp(nameCopy, "removeallips")) {
		const R1ONativeBanState after =
			CaptureR1ONativeBanState(0x264D1D8, 0x264D1F0, sizeof(R1ONativeIpBanEntry), 12);
		if (ipBansBefore.valid && after.valid && after.count == 0)
			Status_ConMsg("removeallips: removed all %d IP ban entr%s.\n", ipBansBefore.count, ipBansBefore.count == 1 ? "y" : "ies");
		else
			Status_ConMsg("removeallips: native R1O IP ban list could not be cleared.\n");
	}

	const R1ONativeBanState userBansAfter = mutatesUserBans
		? CaptureR1ONativeBanState(0x264D1B0, 0x264D1C8, sizeof(R1ONativeUserBanEntry), 28)
		: R1ONativeBanState{};
	const R1ONativeBanState ipBansAfter = mutatesIpBans
		? CaptureR1ONativeBanState(0x264D1D8, 0x264D1F0, sizeof(R1ONativeIpBanEntry), 12)
		: R1ONativeBanState{};
	const bool userBansChanged = userBansBefore.valid && userBansAfter.valid
		&& (userBansBefore.count != userBansAfter.count || userBansBefore.hash != userBansAfter.hash);
	const bool ipBansChanged = ipBansBefore.valid && ipBansAfter.valid
		&& (ipBansBefore.count != ipBansAfter.count || ipBansBefore.hash != ipBansAfter.hash);
	if (ShouldAutosaveR1ONativeBans()) {
		if (userBansChanged)
			WriteR1ONativeUserBans(false);
		if (ipBansChanged)
			WriteR1ONativeIpBans(false);
	}
	if (interesting) {
		char buffer[384];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O command dispatch return name=%s result=%p\n",
			nameCopy[0] ? nameCopy : "<null>",
			reinterpret_cast<void*>(result));
		OutputDebugStringA(buffer);

		if (!handledDedicatedBanConfigExec
			&& nameCopy[0]
			&& !_stricmp(nameCopy, "exec"))
			QueueR1ODediExecFallback(arg1Copy);

		if (nameCopy[0] && !_stricmp(nameCopy, "map")) {
			LogR1OHostStateSnapshot("after map dispatch", true);
			s_R1OLoggedMapDispatch = true;
		}
	}

	return result;
}

static char __fastcall R1OCbuf_AddText(void* commandBuffer, const char* text)
{
	if (IsR1ODedicatedServer() && IsR1OInterestingCommandText(text)) {
		char buffer[768];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O Cbuf_AddText buffer=%p text=%s\n",
			commandBuffer,
			text);
		OutputDebugStringA(buffer);
	}

	const char result = R1OCbuf_AddTextOriginal
		? R1OCbuf_AddTextOriginal(commandBuffer, text)
		: 0;
	if (result && text && *text)
		s_R1OCommandBuffersDirty = true;
	return result;
}

static void UpdateR1OHostNextTickFromRunFrame()
{
	if (!IsR1ODedicatedServer() || !engineR1O)
		return;

	const uintptr_t base = reinterpret_cast<uintptr_t>(engineR1O);
	const float intervalPerTick = *reinterpret_cast<float*>(base + 0x6D8C08);
	const float remainder = *reinterpret_cast<float*>(base + 0x2E17CC0);
	s_R1OHostNextTick = intervalPerTick - remainder;
}

static void __fastcall R1OCbuf_Execute()
{
	const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
	if (caller == reinterpret_cast<uintptr_t>(engineR1O) + 0x181970)
		UpdateR1OHostNextTickFromRunFrame();

	if (R1OCbuf_ExecuteOriginal)
		R1OCbuf_ExecuteOriginal();
}

static bool __fastcall R1OCEngineFilterTime(void* thisptr, float dt, float* flMinFrameTime)
{
	if (!IsR1ODedicatedServer())
		return R1OCEngineFilterTimeOriginal
			? R1OCEngineFilterTimeOriginal(thisptr, dt, flMinFrameTime)
			: true;

	if (flMinFrameTime)
		*flMinFrameTime = s_R1OHostNextTick;
	return dt >= s_R1OHostNextTick;
}

static VOID WINAPI R1OFramePacingSleep(DWORD milliseconds)
{
	// R1 engine_ds sleeps for one millisecond when FilterTime rejects a frame.
	// TFO replaced that wait with WT_DoStuff + Sleep(0) in a one-millisecond
	// polling loop. Preserve every other engine Sleep call, but restore the R1
	// wait at the exact return site of engine_r1o's rejected-frame loop.
	if (IsR1ODedicatedServer()
		&& milliseconds == 0
		&& engineR1O
		&& reinterpret_cast<uintptr_t>(_ReturnAddress())
			== reinterpret_cast<uintptr_t>(engineR1O) + 0x1C8597) {
		milliseconds = 1;
	}

	if (R1OSleepOriginal)
		R1OSleepOriginal(milliseconds);
}

static bool InstallR1OIdleFrameSleepCompatibility()
{
	if (!IsR1ODedicatedServer() || !engineR1O)
		return false;
	if (s_R1OIdleFrameSleepPatched)
		return true;

	// engine_r1o.dll imports Sleep through this IAT slot. Patching the slot is
	// ASLR-safe and lets the wrapper distinguish the sole frame-pacing call by
	// return address; no other Sleep(0) behavior is changed.
	R1OSleepType* const slot = R1OHookGlobal<R1OSleepType>(0x4D5540);
	if (!slot || !IsReadableRange(slot, sizeof(*slot)) || !*slot) {
		Warning("R1Delta: failed to locate R1O frame-pacing Sleep import\n");
		return false;
	}

	if (*slot == &R1OFramePacingSleep) {
		s_R1OIdleFrameSleepPatched = R1OSleepOriginal != nullptr;
		return s_R1OIdleFrameSleepPatched;
	}

	R1OSleepOriginal = *slot;
	if (!WriteR1OHookGlobalValue<R1OSleepType>(0x4D5540, &R1OFramePacingSleep)) {
		R1OSleepOriginal = nullptr;
		Warning("R1Delta: failed to patch R1O frame-pacing Sleep import\n");
		return false;
	}

	s_R1OIdleFrameSleepPatched = true;
	OutputDebugStringA("R1Delta: restored R1 dedicated one-millisecond rejected-frame sleep for R1O fake dedi\n");
	return true;
}

static void __fastcall R1ONetListenSocket(int socketIndex)
{
	if (!IsR1ODedicatedServer()) {
		if (R1ONetListenSocketOriginal)
			R1ONetListenSocketOriginal(socketIndex);
		return;
	}

	++s_R1ONetListenInvocationCount;
	s_R1ODediServerSocketIndex = socketIndex;

	void* tableBefore = R1OHookGlobalValue<void*>(0x2A73EB8, nullptr);
	unsigned char* entryBefore = tableBefore && socketIndex >= 0
		? reinterpret_cast<unsigned char*>(tableBefore) + 16LL * socketIndex
		: nullptr;
	const bool readableBefore = tableBefore && socketIndex >= 0 && IsReadableRange(entryBefore, 16);
	const int portBefore = readableBefore ? *reinterpret_cast<unsigned short*>(entryBefore) : -1;
	const int activeBefore = readableBefore ? static_cast<int>(entryBefore[4]) : -1;
	const int udpBefore = readableBefore ? *reinterpret_cast<int*>(entryBefore + 8) : -1;
	const int fdBefore = readableBefore ? *reinterpret_cast<int*>(entryBefore + 12) : -1;

	if (readableBefore) {
		const int oldTcp = *reinterpret_cast<int*>(entryBefore + 12);
		if (oldTcp) {
			closesocket(static_cast<SOCKET>(oldTcp));
			*reinterpret_cast<int*>(entryBefore + 12) = 0;
			entryBefore[4] = 0;
		}

		const int requestedPort =
			ResolveR1ODediServerPort(*reinterpret_cast<unsigned short*>(entryBefore));
		int udpSocket = *reinterpret_cast<int*>(entryBefore + 8);
		const int existingPort =
			*reinterpret_cast<unsigned short*>(entryBefore);
		int selectedPort = 0;
		int lastSocketError = 0;
		constexpr int kPortTryMax = 10;

		if (udpSocket
			&& existingPort == requestedPort
			&& EnsureR1ORconListenerForPort(
				reinterpret_cast<uintptr_t>(engineR1O),
				requestedPort)) {
			selectedPort = requestedPort;
		}
		else {
			if (udpSocket) {
				closesocket(static_cast<SOCKET>(udpSocket));
				udpSocket = 0;
			}

			for (int offset = 0; offset < kPortTryMax; ++offset) {
				const int candidatePort = requestedPort + offset;
				if (candidatePort > 0xFFFF)
					break;

				const int candidateSocket =
					OpenR1ODediSocket(candidatePort, false);
				if (!candidateSocket) {
					lastSocketError = WSAGetLastError();
					continue;
				}

				if (!EnsureR1ORconListenerForPort(
						reinterpret_cast<uintptr_t>(engineR1O),
						candidatePort)) {
					lastSocketError = WSAGetLastError();
					closesocket(static_cast<SOCKET>(candidateSocket));
					continue;
				}

				udpSocket = candidateSocket;
				selectedPort = candidatePort;
				break;
			}
		}

		if (selectedPort) {
			s_R1ODediBoundServerPort.store(selectedPort, std::memory_order_release);
			*reinterpret_cast<unsigned short*>(entryBefore) =
				static_cast<unsigned short>(selectedPort);
			*reinterpret_cast<int*>(entryBefore + 8) = udpSocket;

			if (cvarinterface && OriginalCCVar_FindVar) {
				if (ConVarR1* hostPort =
						OriginalCCVar_FindVar(cvarinterface, "hostport")) {
					if (hostPort->m_Value.m_nValue != selectedPort) {
						IConVar* const conVarInterface =
							static_cast<IConVar*>(hostPort);
						auto** const vtable = reinterpret_cast<void**>(
							conVarInterface->__vftable);
						if (vtable && IsReadableRange(vtable, sizeof(void*) * 3)) {
							using SetIntFn =
								void(__fastcall*)(IConVar*, int);
							reinterpret_cast<SetIntFn>(vtable[2])(
								conVarInterface,
								selectedPort);
						}
					}
				}
			}

			if (selectedPort != requestedPort) {
				Status_ConMsg(
					"Port %d was unavailable; server is listening on port %d instead.\n",
					requestedPort,
					selectedPort);
			}
		}
		else {
			s_R1ODediBoundServerPort.store(0, std::memory_order_release);
			*reinterpret_cast<unsigned short*>(entryBefore) =
				static_cast<unsigned short>(requestedPort);
			*reinterpret_cast<int*>(entryBefore + 8) = 0;
			WriteR1OHookGlobalValue<int>(0x22FB5A4, lastSocketError);
			Warning(
				"R1Delta: unable to bind R1O game and RCON sockets in port range %d-%d "
				"(WSA error %d)\n",
				requestedPort,
				requestedPort + kPortTryMax - 1 > 0xFFFF
					? 0xFFFF
					: requestedPort + kPortTryMax - 1,
				lastSocketError);
		}

		// TFO's second socket is the RPT transport, not Source RCON. R1
		// clients use the UDP game socket, while the native CRConServer owns
		// its own TCP listener on hostport. Leaving RPT bound here prevents
		// CRConServer from binding and makes standard Source RCON clients hit
		// the wrong protocol parser.
		*reinterpret_cast<int*>(entryBefore + 12) = 0;
		entryBefore[4] = 0;
	} else if (R1ONetListenSocketOriginal) {
		R1ONetListenSocketOriginal(socketIndex);
	}

	void* tableAfter = R1OHookGlobalValue<void*>(0x2A73EB8, nullptr);
	unsigned char* entryAfter = tableAfter && socketIndex >= 0
		? reinterpret_cast<unsigned char*>(tableAfter) + 16LL * socketIndex
		: nullptr;
	const bool readableAfter = tableAfter && socketIndex >= 0 && IsReadableRange(entryAfter, 16);
	const int portAfter = readableAfter ? *reinterpret_cast<unsigned short*>(entryAfter) : -1;
	const int activeAfter = readableAfter ? static_cast<int>(entryAfter[4]) : -1;
	const int udpAfter = readableAfter ? *reinterpret_cast<int*>(entryAfter + 8) : -1;
	const int fdAfter = readableAfter ? *reinterpret_cast<int*>(entryAfter + 12) : -1;

	char buffer[512];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O NET_ListenSocket index=%d before[table=%p port=%d active=%d udp=%d tcp=%d] after[table=%p port=%d active=%d udp=%d tcp=%d] multiplayer=%d listenSuppressed=%d wsa=%d\n",
		socketIndex,
		tableBefore,
		portBefore,
		activeBefore,
		udpBefore,
		fdBefore,
		tableAfter,
		portAfter,
		activeAfter,
		udpAfter,
		fdAfter,
		static_cast<int>(R1OHookGlobalValue<unsigned char>(0x22FB57A, static_cast<unsigned char>(0))),
		static_cast<int>(R1OHookGlobalValue<unsigned char>(0x6B908F, static_cast<unsigned char>(0))),
		R1OHookGlobalValue<int>(0x22FB5A4, 0));
	OutputDebugStringA(buffer);

	static int s_lastReportedPort = -1;
	if (readableAfter && activeAfter == 1 && portAfter > 0 && portAfter != s_lastReportedPort) {
		s_lastReportedPort = portAfter;
		printf("[R1O dedicated] listening on port %d\n", portAfter);
		fflush(stdout);
	}
}

static char __fastcall R1ONetReceivePacket(unsigned int frameTime, __int64 packet, char encrypted)
{
	if (IsR1ODedicatedServer() && packet
		&& IsReadableRange(reinterpret_cast<void*>(packet + 24), sizeof(int))) {
		const int socketIndex = *reinterpret_cast<int*>(packet + 24);
		void* table = R1OHookGlobalValue<void*>(0x2A73EB8, nullptr);
		unsigned char* entry = table && socketIndex >= 0
			? reinterpret_cast<unsigned char*>(table) + 16LL * socketIndex
			: nullptr;
		if (entry && IsReadableRange(entry, 16)
			&& *reinterpret_cast<int*>(entry + 8) == 0)
			return 0;
	}

	int socketIndexBefore = -1;
	int lengthBefore = 0;
	int udpBefore = -1;
	int tcpBefore = -1;
	int portBefore = -1;
	int wsaBefore = 0;

	if (IsR1ODedicatedServer() && packet) {
		socketIndexBefore = IsReadableRange(reinterpret_cast<void*>(packet + 24), sizeof(int))
			? *reinterpret_cast<int*>(packet + 24)
			: -1;
		lengthBefore = IsReadableRange(reinterpret_cast<void*>(packet + 112), sizeof(int))
			? *reinterpret_cast<int*>(packet + 112)
			: 0;
		void* table = R1OHookGlobalValue<void*>(0x2A73EB8, nullptr);
		unsigned char* entry = table && socketIndexBefore >= 0
			? reinterpret_cast<unsigned char*>(table) + 16LL * socketIndexBefore
			: nullptr;
		if (entry && IsReadableRange(entry, 16)) {
			portBefore = *reinterpret_cast<unsigned short*>(entry);
			udpBefore = *reinterpret_cast<int*>(entry + 8);
			tcpBefore = *reinterpret_cast<int*>(entry + 12);
		}
		wsaBefore = R1OHookGlobalValue<int>(0x22FB5A4, 0);

		static int enterLogBudget = 0;
		if (enterLogBudget > 0) {
			--enterLogBudget;
			char buffer[512];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: R1O NET_ReceivePacket enter frame=%u packet=%p socket=%d port=%d udp=%d tcp=%d length=%d encrypted=%d wsa=%d original=%p\n",
				frameTime,
				reinterpret_cast<void*>(packet),
				socketIndexBefore,
				portBefore,
				udpBefore,
				tcpBefore,
				lengthBefore,
				static_cast<int>(encrypted),
				wsaBefore,
				reinterpret_cast<void*>(R1ONetReceivePacketOriginal));
			OutputDebugStringA(buffer);
		}
	}

	const bool guardRecvLoop = IsR1ODedicatedServer();
	if (guardRecvLoop) {
		if (s_R1ORecvFromGuardDepth++ == 0)
			s_R1ORecvFromCalls = 0;
	}

	const char result = R1ONetReceivePacketOriginal
		? R1ONetReceivePacketOriginal(frameTime, packet, encrypted)
		: 0;

	if (guardRecvLoop)
		--s_R1ORecvFromGuardDepth;

	if (IsR1ODedicatedServer() && packet) {
		static int logBudget = 0;
		const int socketIndex = IsReadableRange(reinterpret_cast<void*>(packet + 24), sizeof(int))
			? *reinterpret_cast<int*>(packet + 24)
			: -1;
		const int length = IsReadableRange(reinterpret_cast<void*>(packet + 112), sizeof(int))
			? *reinterpret_cast<int*>(packet + 112)
			: 0;
		void* table = R1OHookGlobalValue<void*>(0x2A73EB8, nullptr);
		unsigned char* entry = table && socketIndex >= 0
			? reinterpret_cast<unsigned char*>(table) + 16LL * socketIndex
			: nullptr;
		const int port = entry && IsReadableRange(entry, 16) ? *reinterpret_cast<unsigned short*>(entry) : -1;
		const int udp = entry && IsReadableRange(entry, 16) ? *reinterpret_cast<int*>(entry + 8) : -1;
		const int tcp = entry && IsReadableRange(entry, 16) ? *reinterpret_cast<int*>(entry + 12) : -1;
		const int wsa = R1OHookGlobalValue<int>(0x22FB5A4, 0);
		if (logBudget > 0 && (result || length != lengthBefore || wsa != wsaBefore || (length > 0 && length <= 4096))) {
			--logBudget;
			char buffer[512];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: R1O NET_ReceivePacket return result=%d packet=%p socket=%d port=%d udp=%d tcp=%d length=%d->%d encrypted=%d wsa=%d->%d\n",
				static_cast<int>(result),
				reinterpret_cast<void*>(packet),
				socketIndex,
				port,
				udp,
				tcp,
				lengthBefore,
				length,
				static_cast<int>(encrypted),
				wsaBefore,
				wsa);
			OutputDebugStringA(buffer);
		}

		static int datagramLogBudget = 0;
		void* payload = IsReadableRange(reinterpret_cast<void*>(packet + 40), sizeof(void*))
			? *reinterpret_cast<void**>(packet + 40)
			: nullptr;
		if (datagramLogBudget > 0 && payload && IsReadableRange(payload, static_cast<size_t>(length > 32 ? 32 : length))
			&& (length >= 1024 || result)) {
			--datagramLogBudget;
			const unsigned char* bytes = reinterpret_cast<const unsigned char*>(payload);
			const bool splitPacket = length >= 12 && *reinterpret_cast<const int*>(bytes) == -2;
			int splitSequence = -1;
			int splitNumber = -1;
			int splitCount = -1;
			int splitSize = -1;
			if (splitPacket) {
				splitSequence = *reinterpret_cast<const int*>(bytes + 4);
				const unsigned short packetId = *reinterpret_cast<const unsigned short*>(bytes + 8);
				splitNumber = packetId >> 8;
				splitCount = packetId & 0xff;
				splitSize = *reinterpret_cast<const short*>(bytes + 10);
			}
			char buffer[1024];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: R1O NET_ReceivePacket payload result=%d socket=%d length=%d encrypted=%d split=%d seq=%d part=%d/%d splitSize=%d first=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
				static_cast<int>(result),
				socketIndex,
				length,
				static_cast<int>(encrypted),
				splitPacket ? 1 : 0,
				splitSequence,
				splitNumber,
				splitCount,
				splitSize,
				length > 0 ? bytes[0] : 0,
				length > 1 ? bytes[1] : 0,
				length > 2 ? bytes[2] : 0,
				length > 3 ? bytes[3] : 0,
				length > 4 ? bytes[4] : 0,
				length > 5 ? bytes[5] : 0,
				length > 6 ? bytes[6] : 0,
				length > 7 ? bytes[7] : 0,
				length > 8 ? bytes[8] : 0,
				length > 9 ? bytes[9] : 0,
				length > 10 ? bytes[10] : 0,
				length > 11 ? bytes[11] : 0,
				length > 12 ? bytes[12] : 0,
				length > 13 ? bytes[13] : 0,
				length > 14 ? bytes[14] : 0,
				length > 15 ? bytes[15] : 0);
			OutputDebugStringA(buffer);
		}

		if (result && length >= 1024)
			LogR1OClientSlotSnapshot("after accepted large datagram", true);
	}

	return result;
}

static void LogR1OPacketSummary(const char* prefix, __int64 packet, __int64 netChan, int socketIndex, int lookupResult)
{
	if (!IsR1ODedicatedServer() || !packet)
		return;

	const int length = IsReadableRange(reinterpret_cast<void*>(packet + 112), sizeof(int))
		? *reinterpret_cast<int*>(packet + 112)
		: 0;
	void* payload = IsReadableRange(reinterpret_cast<void*>(packet + 40), sizeof(void*))
		? *reinterpret_cast<void**>(packet + 40)
		: nullptr;
	const unsigned char* bytes = reinterpret_cast<const unsigned char*>(payload);
	const bool readablePayload = payload && IsReadableRange(payload, static_cast<size_t>(length > 16 ? 16 : length));
	const int addressType = IsReadableRange(reinterpret_cast<void*>(packet), sizeof(int))
		? *reinterpret_cast<int*>(packet)
		: -1;
	const unsigned short port = IsReadableRange(reinterpret_cast<void*>(packet + 20), sizeof(unsigned short))
		? *reinterpret_cast<unsigned short*>(packet + 20)
		: 0;
	void* processPacket = nullptr;
	if (netChan && IsReadableRange(reinterpret_cast<void*>(netChan), sizeof(void*))) {
		uintptr_t* vtable = *reinterpret_cast<uintptr_t**>(netChan);
		if (vtable && IsReadableRange(vtable, sizeof(uintptr_t) * 40))
			processPacket = reinterpret_cast<void*>(vtable[39]);
	}

	char buffer[768];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O %s socket=%d packet=%p addrType=%d port=%hu length=%d payload=%p netchan=%p lookup=%d processPacket=%p first=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
		prefix ? prefix : "packet",
		socketIndex,
		reinterpret_cast<void*>(packet),
		addressType,
		port,
		length,
		payload,
		reinterpret_cast<void*>(netChan),
		lookupResult,
		processPacket,
		readablePayload && length > 0 ? bytes[0] : 0,
		readablePayload && length > 1 ? bytes[1] : 0,
		readablePayload && length > 2 ? bytes[2] : 0,
		readablePayload && length > 3 ? bytes[3] : 0,
		readablePayload && length > 4 ? bytes[4] : 0,
		readablePayload && length > 5 ? bytes[5] : 0,
		readablePayload && length > 6 ? bytes[6] : 0,
		readablePayload && length > 7 ? bytes[7] : 0,
		readablePayload && length > 8 ? bytes[8] : 0,
		readablePayload && length > 9 ? bytes[9] : 0,
		readablePayload && length > 10 ? bytes[10] : 0,
		readablePayload && length > 11 ? bytes[11] : 0,
		readablePayload && length > 12 ? bytes[12] : 0,
		readablePayload && length > 13 ? bytes[13] : 0,
		readablePayload && length > 14 ? bytes[14] : 0,
		readablePayload && length > 15 ? bytes[15] : 0);
	OutputDebugStringA(buffer);
}

static int R1OBitBufferIntField(__int64 bitBuffer, size_t offset, int fallback = -1)
{
	return bitBuffer && IsReadableRange(reinterpret_cast<void*>(bitBuffer + offset), sizeof(int))
		? *reinterpret_cast<int*>(bitBuffer + offset)
		: fallback;
}

static unsigned char R1OBitBufferByteField(__int64 bitBuffer, size_t offset, unsigned char fallback = 0xff)
{
	return bitBuffer && IsReadableRange(reinterpret_cast<void*>(bitBuffer + offset), sizeof(unsigned char))
		? *reinterpret_cast<unsigned char*>(bitBuffer + offset)
		: fallback;
}

static __int64 R1OBitBufferQwordField(__int64 bitBuffer, size_t offset, __int64 fallback = -1)
{
	return bitBuffer && IsReadableRange(reinterpret_cast<void*>(bitBuffer + offset), sizeof(__int64))
		? *reinterpret_cast<__int64*>(bitBuffer + offset)
		: fallback;
}

static __int64 R1OBitBufferCurrentBit(__int64 bitBuffer)
{
	if (!bitBuffer || !IsReadableRange(reinterpret_cast<void*>(bitBuffer), 64))
		return -1;

	const __int64 dataBits = R1OBitBufferQwordField(bitBuffer, 16);
	const __int64 dataByteOffset = R1OBitBufferQwordField(bitBuffer, 24);
	const int cachedBits = R1OBitBufferIntField(bitBuffer, 36);
	const __int64 current = R1OBitBufferQwordField(bitBuffer, 40);
	const __int64 base = R1OBitBufferQwordField(bitBuffer, 56);
	if (dataBits < 0 || !base)
		return -1;

	const __int64 currentBit =
		8 * (dataByteOffset & 3) +
		32 * (((current - base) >> 2) - 1) +
		32 - cachedBits;
	if (currentBit < 0)
		return 0;
	if (currentBit > dataBits)
		return dataBits;
	return currentBit;
}

static void R1OBitBufferSetOverflow(__int64 bitBuffer)
{
	if (bitBuffer && IsReadableRange(reinterpret_cast<void*>(bitBuffer + 8), sizeof(unsigned char)))
		*reinterpret_cast<unsigned char*>(bitBuffer + 8) = 1;
}

static bool R1OBitBufferIsOverflowed(__int64 bitBuffer)
{
	return R1OBitBufferByteField(bitBuffer, 8, 1) != 0;
}

static unsigned int R1OBitMask(int numBits)
{
	if (numBits <= 0)
		return 0;
	if (numBits >= 32)
		return 0xFFFFFFFFu;
	return (1u << numBits) - 1u;
}

static void R1OBitBufferGrabNextDWord(__int64 bitBuffer, bool overflowImmediately)
{
	if (!bitBuffer || !IsReadableRange(reinterpret_cast<void*>(bitBuffer), 64))
		return;

	unsigned int** dataIn = reinterpret_cast<unsigned int**>(bitBuffer + 40);
	unsigned int** bufferEnd = reinterpret_cast<unsigned int**>(bitBuffer + 48);
	unsigned int* inBufWord = reinterpret_cast<unsigned int*>(bitBuffer + 32);
	int* bitsAvail = reinterpret_cast<int*>(bitBuffer + 36);
	if (!IsReadableRange(dataIn, sizeof(*dataIn)) ||
		!IsReadableRange(bufferEnd, sizeof(*bufferEnd)) ||
		!IsReadableRange(inBufWord, sizeof(*inBufWord)) ||
		!IsReadableRange(bitsAvail, sizeof(*bitsAvail))) {
		R1OBitBufferSetOverflow(bitBuffer);
		return;
	}

	if (*dataIn == *bufferEnd) {
		*bitsAvail = 1;
		*inBufWord = 0;
		++(*dataIn);
		if (overflowImmediately)
			R1OBitBufferSetOverflow(bitBuffer);
	}
	else if (*dataIn <= *bufferEnd && IsReadableRange(*dataIn, sizeof(unsigned int))) {
		*inBufWord = **dataIn;
		++(*dataIn);
	}
	else {
		R1OBitBufferSetOverflow(bitBuffer);
		*inBufWord = 0;
	}
}

static unsigned int R1OBitBufferReadUBitLong(__int64 bitBuffer, int numBits)
{
	if (numBits <= 0)
		return 0;
	if (numBits > 32) {
		R1OBitBufferSetOverflow(bitBuffer);
		return 0;
	}
	if (!bitBuffer || !IsReadableRange(reinterpret_cast<void*>(bitBuffer), 64)) {
		R1OBitBufferSetOverflow(bitBuffer);
		return 0;
	}

	unsigned int* inBufWord = reinterpret_cast<unsigned int*>(bitBuffer + 32);
	int* bitsAvail = reinterpret_cast<int*>(bitBuffer + 36);
	if (!IsReadableRange(inBufWord, sizeof(*inBufWord)) || !IsReadableRange(bitsAvail, sizeof(*bitsAvail))) {
		R1OBitBufferSetOverflow(bitBuffer);
		return 0;
	}

	if (*bitsAvail >= numBits) {
		const unsigned int result = *inBufWord & R1OBitMask(numBits);
		*bitsAvail -= numBits;
		if (*bitsAvail) {
			*inBufWord >>= numBits;
		}
		else {
			*bitsAvail = 32;
			R1OBitBufferGrabNextDWord(bitBuffer, false);
		}
		return result;
	}

	unsigned int result = *inBufWord;
	const int remainingBits = numBits - *bitsAvail;
	const int consumedBits = *bitsAvail;
	R1OBitBufferGrabNextDWord(bitBuffer, true);
	if (R1OBitBufferIsOverflowed(bitBuffer))
		return 0;

	result |= ((*inBufWord & R1OBitMask(remainingBits)) << consumedBits);
	*bitsAvail = 32 - remainingBits;
	*inBufWord >>= remainingBits;
	return result;
}

static unsigned int R1OBitBufferPeekUBitLong(__int64 bitBuffer, int numBits)
{
	if (!bitBuffer || !IsReadableRange(reinterpret_cast<void*>(bitBuffer), 64))
		return 0;

	unsigned int* inBufWord = reinterpret_cast<unsigned int*>(bitBuffer + 32);
	int* bitsAvail = reinterpret_cast<int*>(bitBuffer + 36);
	unsigned int** dataIn = reinterpret_cast<unsigned int**>(bitBuffer + 40);
	unsigned char* overflow = reinterpret_cast<unsigned char*>(bitBuffer + 8);
	if (!IsReadableRange(inBufWord, sizeof(*inBufWord)) ||
		!IsReadableRange(bitsAvail, sizeof(*bitsAvail)) ||
		!IsReadableRange(dataIn, sizeof(*dataIn)) ||
		!IsReadableRange(overflow, sizeof(*overflow))) {
		return 0;
	}

	const unsigned int savedInBufWord = *inBufWord;
	const int savedBitsAvail = *bitsAvail;
	unsigned int* const savedDataIn = *dataIn;
	const unsigned char savedOverflow = *overflow;
	const unsigned int result = R1OBitBufferReadUBitLong(bitBuffer, numBits);
	*inBufWord = savedInBufWord;
	*bitsAvail = savedBitsAvail;
	*dataIn = savedDataIn;
	*overflow = savedOverflow;
	return result;
}

static unsigned int R1OBitBufferReadUBitVar(__int64 bitBuffer)
{
	unsigned int value = R1OBitBufferReadUBitLong(bitBuffer, 6);
	switch (value & (16 | 32)) {
	case 16:
		value = (value & 15) | (R1OBitBufferReadUBitLong(bitBuffer, 4) << 4);
		break;
	case 32:
		value = (value & 15) | (R1OBitBufferReadUBitLong(bitBuffer, 8) << 4);
		break;
	case 48:
		value = (value & 15) | (R1OBitBufferReadUBitLong(bitBuffer, 32 - 4) << 4);
		break;
	default:
		break;
	}
	return value;
}

static int R1OBitBufferGetNumBitsLeft(__int64 bitBuffer)
{
	const int dataBits = R1OBitBufferIntField(bitBuffer, 16);
	const __int64 currentBit = R1OBitBufferCurrentBit(bitBuffer);
	if (dataBits < 0 || currentBit < 0 || currentBit > dataBits)
		return 0;
	return static_cast<int>(dataBits - currentBit);
}

static int R1ONetMessageIntVFunc(__int64 message, size_t vtableIndex, int fallback = -1)
{
	if (!message || !IsReadableRange(reinterpret_cast<void*>(message), sizeof(void*)))
		return fallback;

	uintptr_t* vtable = *reinterpret_cast<uintptr_t**>(message);
	if (!vtable || !IsReadableRange(vtable, sizeof(uintptr_t) * (vtableIndex + 1)))
		return fallback;

	using IntVFunc = int(__fastcall*)(__int64);
	IntVFunc fn = reinterpret_cast<IntVFunc>(vtable[vtableIndex]);
	if (!fn)
		return fallback;

	__try {
		return fn(message);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return fallback;
	}
}

static const char* R1ONetMessageName(__int64 message)
{
	if (!message || !IsReadableRange(reinterpret_cast<void*>(message), sizeof(void*)))
		return "<null>";

	uintptr_t* vtable = *reinterpret_cast<uintptr_t**>(message);
	if (!vtable || !IsReadableRange(vtable, sizeof(uintptr_t) * 11))
		return "<bad-vtable>";

	using NameVFunc = const char*(__fastcall*)(__int64);
	NameVFunc fn = reinterpret_cast<NameVFunc>(vtable[10]);
	if (!fn)
		return "<no-name-fn>";

	const char* name = nullptr;
	__try {
		name = fn(message);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return "<name-av>";
	}

	return IsReadableCString(name) ? name : "<bad-name>";
}

static uintptr_t R1ONetMessageVtableRva(__int64 message)
{
	if (!message || !engineR1O || !IsReadableRange(reinterpret_cast<void*>(message), sizeof(uintptr_t)))
		return 0;

	const uintptr_t vtable = *reinterpret_cast<uintptr_t*>(message);
	const uintptr_t base = reinterpret_cast<uintptr_t>(engineR1O);
	if (vtable < base)
		return 0;

	return vtable - base;
}

static int R1ONetMessageLegacyIdFromVtable(__int64 message, int fallback = -1);
static const char* R1ONetMessageNameFromVtable(__int64 message, const char* fallback = "<unknown>");

static void LogR1ONetMessageEvent(const char* eventName, __int64 netChan, __int64 bitBuffer, __int64 message, int id, __int64 result)
{
	if (!IsR1ODedicatedServer())
		return;

	const bool emptyDispatch = !message
		&& id < 0
		&& eventName
		&& (!strcmp_static(eventName, "dispatch-enter") || !strcmp_static(eventName, "dispatch-leave"));
	if (emptyDispatch) {
		if (s_R1ONetMessageEmptyDispatchLogBudget <= 0)
			return;
		--s_R1ONetMessageEmptyDispatchLogBudget;
	} else {
		if (s_R1ONetMessageLogBudget <= 0)
			return;
		--s_R1ONetMessageLogBudget;
	}

	const int idSlot8 = R1ONetMessageLegacyIdFromVtable(message);
	const int idSlot9 = -1;
	const int dataBits = R1OBitBufferIntField(bitBuffer, 16);
	const int bitsAvail = R1OBitBufferIntField(bitBuffer, 36);
	const unsigned char overflow = R1OBitBufferByteField(bitBuffer, 8);

	char buffer[768];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O netmsg %s netchan=%p bitbuf=%p id=%d msg=%p name=%s vtableRva=0x%llX id8=%d id9=%d result=%lld dataBits=%d bitsAvail=%d overflow=%u budget=%d\n",
		eventName ? eventName : "event",
		reinterpret_cast<void*>(netChan),
		reinterpret_cast<void*>(bitBuffer),
		id,
		reinterpret_cast<void*>(message),
		R1ONetMessageNameFromVtable(message),
		static_cast<unsigned long long>(R1ONetMessageVtableRva(message)),
		idSlot8,
		idSlot9,
		static_cast<long long>(result),
		dataBits,
		bitsAvail,
		static_cast<unsigned int>(overflow),
		emptyDispatch ? s_R1ONetMessageEmptyDispatchLogBudget : s_R1ONetMessageLogBudget);
	OutputDebugStringA(buffer);
}

static void LogR1ONetMessageFailure(const char* operation, __int64 netChan, __int64 bitBuffer, __int64 message, int id, __int64 result, const char* extra = nullptr)
{
	if (!IsR1ODedicatedServer())
		return;

	const int dataBits = R1OBitBufferIntField(bitBuffer, 16);
	const int bitsAvail = R1OBitBufferIntField(bitBuffer, 36);
	const unsigned char overflow = R1OBitBufferByteField(bitBuffer, 8);
	char buffer[1024];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O NETMESSAGE %s FAILED netchan=%p bitbuf=%p id=%d msg=%p name=%s vtableRva=0x%llX id8=%d id9=%d result=%lld dataBits=%d bitsAvail=%d overflow=%u currentBit=%lld%s%s\n",
		operation ? operation : "operation",
		reinterpret_cast<void*>(netChan),
		reinterpret_cast<void*>(bitBuffer),
		id,
		reinterpret_cast<void*>(message),
		R1ONetMessageNameFromVtable(message),
		static_cast<unsigned long long>(R1ONetMessageVtableRva(message)),
		R1ONetMessageLegacyIdFromVtable(message),
		-1,
		static_cast<long long>(result),
		dataBits,
		bitsAvail,
		static_cast<unsigned int>(overflow),
		static_cast<long long>(R1OBitBufferCurrentBit(bitBuffer)),
		extra && *extra ? " " : "",
		extra && *extra ? extra : "");
	OutputDebugStringA(buffer);
	Warning("%s", buffer);
}

struct R1LegacyNetMessageId {
	int id;
	const char* name;
};

using R1ONetMessageGetTypeFunc = int(__fastcall*)(__int64 message);

struct R1ONetMessageGetIdOverride {
	const char* name;
	uintptr_t vtableRva;
	int r1oId;
	int r1Id;
	uintptr_t originalGetId;
};

static R1ONetMessageGetIdOverride s_R1ONetMessageGetIdOverrides[] = {
	{ "svc_Sounds", 0x52FEB0, 17, 17, 0 },
	{ "svc_SendTable", 0x531740, 8, 8, 0 },
	{ "svc_SetPause", 0x531810, 10, 10, 0 },
	{ "svc_DurangoVoiceData", 0x531888, 15, 15, 0 },
	{ "svc_SplitScreen", 0x531978, 21, 21, 0 },
	{ "svc_Playlists", 0x531A08, 11, 11, 0 },
	{ "svc_PlaylistPlayerCounts", 0x531AF8, 27, 32, 0 },
	{ "svc_ServerInfo", 0x531A80, 7, 7, 0 },
	{ "svc_CrosshairAngle", 0x531C78, 19, 19, 0 },
	{ "svc_ClassInfo", 0x531CF0, 9, 9, 0 },
	{ "svc_UserMessage", 0x532148, 28, 33, 0 },
	{ "svc_Print", 0x532B10, 16, 16, 0 },
	{ "svc_DLCNotifyOwnership", 0x532B88, 23, 28, 0 },
	{ "svc_PlaylistOverrides", 0x532C00, 26, 31, 0 },
	{ "svc_FixAngle", 0x532C78, 18, 18, 0 },
	{ "svc_BSPDecal", 0x532D68, 20, 20, 0 },
	{ "svc_VoiceData", 0x532F08, 14, 14, 0 },
	{ "svc_EntityMessage", 0x531650, 29, 34, 0 },
	{ "svc_GameEvent", 0x532F80, 30, 35, 0 },
	{ "svc_Snapshot", 0x5316C8, 31, 36, 0 },
	{ "svc_TempEntities", 0x531D68, 32, 37, 0 },
	{ "svc_Menu", 0x532CF0, 34, 39, 0 },
	{ "svc_GameEventList", 0x531C00, 35, 40, 0 },
	{ "svc_GetCvarValue", 0x5315D8, 36, 41, 0 },
	{ "svc_RequestScreenshot", 0x532FF8, 38, 43, 0 },
	{ "svc_ServerTick", 0x533070, 22, 22, 0 },
	{ "svc_UpdateStringTable", 0x533160, 13, 13, 0 },
	{ "svc_CmdKeyValues", 0x571AC8, 37, 42, 0 },
	{ "svc_CreateStringTable", 0x571A50, 12, 12, 0 },
	{ "svc_PlaylistChange", 0x5718E8, 24, 29, 0 },
	{ "svc_SetTeam", 0x571BB8, 25, 30, 0 },
	{ "clc_ClientInfo", 0x545FF0, 39, 44, 0 },
	{ "clc_Move", 0x53E5A0, 40, 45, 0 },
	{ "clc_VoiceData", 0x55D6A8, 41, 46, 0 },
	{ "clc_DurangoVoiceData", 0x55D720, 42, 47, 0 },
	{ "clc_BaselineAck", 0x53DDF0, 43, 48, 0 },
	{ "clc_ListenEvents", 0x532A20, 44, 49, 0 },
	{ "clc_RespondCvarValue", 0x531B88, 45, 50, 0 },
	{ "clc_FileCRCCheck", 0x546180, 46, 51, 0 },
	{ "clc_SaveReplay", 0x55D818, 47, 52, 0 },
	{ "clc_LoadingProgress", 0x5461F8, 48, 53, 0 },
	{ "clc_SplitPlayerConnect", 0x5330E8, 49, 56, 0 },
	{ "clc_CmdKeyValues", 0x5719D8, 50, 57, 0 },
	{ "clc_ClientTick", 0x53E510, 51, 58, 0 },
	{ "clc_ClientSayText", 0x533970, 52, 59, 0 },
	{ "clc_Screenshot", 0x55D890, 53, 60, 0 },
};

static const R1LegacyNetMessageId s_R1LegacyNetMessageIds[] = {
	{ 3, "net_SplitScreenUser" },
	{ 4, "net_StringCmd" },
	{ 5, "net_SetConVar" },
	{ 6, "net_SignonState" },
	{ 44, "clc_ClientInfo" },
	{ 45, "clc_Move" },
	{ 46, "clc_VoiceData" },
	{ 47, "clc_DurangoVoiceData" },
	{ 48, "clc_BaselineAck" },
	{ 49, "clc_ListenEvents" },
	{ 50, "clc_RespondCvarValue" },
	{ 51, "clc_FileCRCCheck" },
	{ 52, "clc_SaveReplay" },
	{ 53, "clc_LoadingProgress" },
	{ 54, "clc_PersistenceRequestSave" },
	{ 55, "clc_PersistenceClientToken" },
	{ 56, "clc_SplitPlayerConnect" },
	{ 57, "clc_CmdKeyValues" },
	{ 58, "clc_ClientTick" },
	{ 59, "clc_ClientSayText" },
	{ 60, "clc_Screenshot" },
};

static const char* R1LegacyNetMessageNameForId(int id)
{
	for (const R1LegacyNetMessageId& entry : s_R1LegacyNetMessageIds) {
		if (entry.id == id)
			return entry.name;
	}

	return nullptr;
}

static const char* R1OLegacyServerMessageNameForId(int id)
{
	for (const R1ONetMessageGetIdOverride& overrideEntry : s_R1ONetMessageGetIdOverrides) {
		if (overrideEntry.r1Id == id)
			return overrideEntry.name;
	}

	return nullptr;
}

static int __fastcall R1ONetMessageGetIdOverrideThunk(__int64 message)
{
	const uintptr_t vtableRva = R1ONetMessageVtableRva(message);

	for (const R1ONetMessageGetIdOverride& overrideEntry : s_R1ONetMessageGetIdOverrides) {
		if (overrideEntry.vtableRva == vtableRva) {
			if (IsR1ODedicatedServer() && s_R1OServerNetMessageGetTypeLogBudget > 0) {
				--s_R1OServerNetMessageGetTypeLogBudget;
				char buffer[512];
				_snprintf_s(
					buffer,
					sizeof(buffer),
					_TRUNCATE,
					"R1Delta: R1O netmsg GetId override msg=%p name=%s vtableRva=0x%llX r1oId=%d r1Id=%d budget=%d\n",
					reinterpret_cast<void*>(message),
					R1ONetMessageNameFromVtable(message),
					static_cast<unsigned long long>(vtableRva),
					overrideEntry.r1oId,
					overrideEntry.r1Id,
					s_R1OServerNetMessageGetTypeLogBudget);
				OutputDebugStringA(buffer);
			}
			return overrideEntry.r1Id;
		}
	}

	return -1;
}

static int R1ONetMessageLegacyIdFromVtable(__int64 message, int fallback)
{
	const uintptr_t vtableRva = R1ONetMessageVtableRva(message);
	for (const R1ONetMessageGetIdOverride& overrideEntry : s_R1ONetMessageGetIdOverrides) {
		if (overrideEntry.vtableRva == vtableRva)
			return overrideEntry.r1Id;
	}
	return fallback;
}

static const char* R1ONetMessageNameFromVtable(__int64 message, const char* fallback)
{
	const uintptr_t vtableRva = R1ONetMessageVtableRva(message);
	for (const R1ONetMessageGetIdOverride& overrideEntry : s_R1ONetMessageGetIdOverrides) {
		if (overrideEntry.vtableRva == vtableRva)
			return overrideEntry.name;
	}
	return fallback;
}

static int R1ONetMessageOriginalType(__int64 message)
{
	const uintptr_t vtableRva = R1ONetMessageVtableRva(message);
	for (const R1ONetMessageGetIdOverride& overrideEntry : s_R1ONetMessageGetIdOverrides) {
		if (overrideEntry.vtableRva != vtableRva || !overrideEntry.originalGetId)
			continue;

		__try {
			return reinterpret_cast<R1ONetMessageGetTypeFunc>(overrideEntry.originalGetId)(message);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return -1;
		}
	}

	return R1ONetMessageIntVFunc(message, 8);
}

struct R1OLegacySignonStateFixup
{
	bool playlistNameChanged = false;
	char oldPlaylistName[128] = {};
};

static int s_R1OSignonStateLegacyPayloadLogBudget = 0;

static const char* R1OSignonStateStringBuffer(__int64 message, ptrdiff_t bufferOffset, ptrdiff_t lengthOffset, int maxLen)
{
	if (!message)
		return nullptr;

	__try {
		const int length = *reinterpret_cast<int*>(message + lengthOffset);
		const char* data = *reinterpret_cast<const char**>(message + bufferOffset);
		if (length <= 0 || length > maxLen || !data)
			return nullptr;
		if (!IsReadableRange(const_cast<char*>(data), static_cast<size_t>(length)))
			return nullptr;
		return data;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return nullptr;
	}
}

static void LogR1OSignonStateTrace(const char* phase, __int64 message, __int64 bitBuffer, int result)
{
	if (!IsR1ODedicatedServer() || s_R1OSignonStateTraceLogBudget <= 0)
		return;
	--s_R1OSignonStateTraceLogBudget;

	int state = -999;
	int spawnCount = -999;
	int serverCount = -999;
	int mapLen = -1;
	int modeLen = -1;
	int initialLen = -1;
	int token = -999;
	int extra = -999;
	int loading = -1;
	__int64 handler = 0;
	uintptr_t handlerVtable = 0;
	uintptr_t handlerTarget = 0;
	int handlerSignonState = -999;
	int predictedStateMismatch = 0;
	const char* initial = nullptr;
	const char* map = nullptr;
	const char* mode = nullptr;
	char levelName[33] = {};
	char parentName[129] = {};

	__try {
		if (message && IsReadableRange(reinterpret_cast<void*>(message + 32), 192)) {
			state = *reinterpret_cast<int*>(message + 32);
			spawnCount = *reinterpret_cast<int*>(message + 36);
			serverCount = *reinterpret_cast<int*>(message + 40);
			initialLen = *reinterpret_cast<int*>(message + 72);
			mapLen = *reinterpret_cast<int*>(message + 104);
			modeLen = *reinterpret_cast<int*>(message + 136);
			initial = R1OSignonStateStringBuffer(message, 48, 72, 1280);
			map = R1OSignonStateStringBuffer(message, 80, 104, 32);
			mode = R1OSignonStateStringBuffer(message, 112, 136, 32);
			loading = static_cast<int>(*reinterpret_cast<unsigned char*>(message + 180));
			token = *reinterpret_cast<int*>(message + 176);
			extra = *reinterpret_cast<int*>(message + 184);
			handler = *reinterpret_cast<__int64*>(message + 24);
			if (handler && IsReadableRange(reinterpret_cast<void*>(handler), 2048)) {
				handlerVtable = *reinterpret_cast<uintptr_t*>(handler);
				if (handlerVtable && IsReadableRange(reinterpret_cast<void*>(handlerVtable), sizeof(uintptr_t) * 4))
					handlerTarget = reinterpret_cast<uintptr_t*>(handlerVtable)[3];
				if (IsReadableRange(reinterpret_cast<void*>(handler + kR1OClientSignonStateOffset), sizeof(int))) {
					handlerSignonState = *reinterpret_cast<int*>(handler + kR1OClientSignonStateOffset);
					predictedStateMismatch = state != handlerSignonState;
				}
			}
			strncpy_s(levelName, reinterpret_cast<const char*>(message + 144), _TRUNCATE);
			strncpy_s(parentName, reinterpret_cast<const char*>(message + 188), _TRUNCATE);
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
	}

	char buffer[1400];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O NET_SignonState %s msg=%p handler=%p handlerVtable=%p handlerTarget=%p result=%d state=%d handlerState=%d stateMismatch=%d spawn=%d server=%d initialLen=%d initial=\"%.*s\" mapLen=%d map=\"%.*s\" modeLen=%d mode=\"%.*s\" loading=%d level=\"%s\" token=%d parent=\"%s\" extra=%d bitbuf=%p currentBit=%lld bitsLeft=%d overflow=%d budget=%d\n",
		phase ? phase : "<phase>",
		reinterpret_cast<void*>(message),
		reinterpret_cast<void*>(handler),
		reinterpret_cast<void*>(handlerVtable),
		reinterpret_cast<void*>(handlerTarget),
		result,
		state,
		handlerSignonState,
		predictedStateMismatch,
		spawnCount,
		serverCount,
		initialLen,
		initial ? (initialLen > 80 ? 80 : initialLen) : 0,
		initial ? initial : "",
		mapLen,
		map ? (mapLen > 32 ? 32 : mapLen) : 0,
		map ? map : "",
		modeLen,
		mode ? (modeLen > 32 ? 32 : modeLen) : 0,
		mode ? mode : "",
		loading,
		levelName,
		token,
		parentName,
		extra,
		reinterpret_cast<void*>(bitBuffer),
		static_cast<long long>(R1OBitBufferCurrentBit(bitBuffer)),
		R1OBitBufferGetNumBitsLeft(bitBuffer),
		static_cast<int>(R1OBitBufferIsOverflowed(bitBuffer)),
		s_R1OSignonStateTraceLogBudget);
	OutputDebugStringA(buffer);
	Warning("%s", buffer);
}

static int R1OServerSpawnCountFromClient(__int64 client)
{
	if (!client)
		return -1;

	__try {
		const __int64 server = *reinterpret_cast<__int64*>(client + 1040);
		if (!server || !IsReadableRange(reinterpret_cast<void*>(server + 464), sizeof(int)))
			return -1;
		return *reinterpret_cast<int*>(server + 464);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return -1;
	}
}

static char __fastcall R1OCGameClientProcessSignonState(__int64 client, __int64 message)
{
	int msgState = -999;
	int msgSpawn = -999;
	int clientState = -999;
	int serverSpawn = -999;
	int reconnectBySpawn = 0;
	int reconnectByState = 0;

	__try {
		if (message && IsReadableRange(reinterpret_cast<void*>(message + 36), sizeof(int) * 2)) {
			msgState = *reinterpret_cast<int*>(message + 32);
			msgSpawn = *reinterpret_cast<int*>(message + 36);
		}
		if (client && IsReadableRange(reinterpret_cast<void*>(client + 1136), sizeof(int)))
			clientState = *reinterpret_cast<int*>(client + 1136);
		serverSpawn = R1OServerSpawnCountFromClient(client);
		reconnectBySpawn = msgState > 2 && serverSpawn != -1 && msgSpawn != serverSpawn;
		reconnectByState = msgState != clientState && !(msgState == 7 && clientState == 8);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
	}

	if (IsR1ODedicatedServer() && s_R1OSignonStateHandlerLogBudget > 0) {
		--s_R1OSignonStateHandlerLogBudget;
		char buffer[512];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O CGameClient::ProcessSignonState enter client=%p msg=%p msgState=%d msgSpawn=%d clientState=%d serverSpawn=%d reconnectBySpawn=%d reconnectByState=%d budget=%d\n",
			reinterpret_cast<void*>(client),
			reinterpret_cast<void*>(message),
			msgState,
			msgSpawn,
			clientState,
			serverSpawn,
			reconnectBySpawn,
			reconnectByState,
			s_R1OSignonStateHandlerLogBudget);
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}

	const char result = R1OCGameClientProcessSignonStateOriginal
		? R1OCGameClientProcessSignonStateOriginal(client, message)
		: 0;

	if (IsR1ODedicatedServer() && s_R1OSignonStateHandlerLogBudget > 0) {
		--s_R1OSignonStateHandlerLogBudget;
		int clientStateAfter = -999;
		__try {
			if (client && IsReadableRange(reinterpret_cast<void*>(client + 1136), sizeof(int)))
				clientStateAfter = *reinterpret_cast<int*>(client + 1136);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
		}
		char buffer[512];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O CGameClient::ProcessSignonState leave client=%p msg=%p result=%d msgState=%d msgSpawn=%d clientStateAfter=%d serverSpawn=%d budget=%d\n",
			reinterpret_cast<void*>(client),
			reinterpret_cast<void*>(message),
			static_cast<int>(result),
			msgState,
			msgSpawn,
			clientStateAfter,
			serverSpawn,
			s_R1OSignonStateHandlerLogBudget);
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}

	return result;
}

static char __fastcall R1OCGameClientProcessClientInfo(__int64 client, __int64 message)
{
	int sendTableCrc = -1;
	int serverCount = -1;
	int clientProtocol = -1;
	int replay = -1;
	int serverSpawn = R1OServerSpawnCountFromClient(client);
	int clientState = -999;

	__try {
		if (message && IsReadableRange(reinterpret_cast<void*>(message + 52), 260)) {
			sendTableCrc = *reinterpret_cast<int*>(message + 32);
			clientProtocol = *reinterpret_cast<int*>(message + 36);
			serverCount = *reinterpret_cast<int*>(message + 40);
			replay = static_cast<int>(*reinterpret_cast<unsigned char*>(message + 44));
		}
		if (client && IsReadableRange(reinterpret_cast<void*>(client + 1136), sizeof(int)))
			clientState = *reinterpret_cast<int*>(client + 1136);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
	}

	if (IsR1ODedicatedServer() && s_R1OClientInfoHandlerLogBudget > 0) {
		--s_R1OClientInfoHandlerLogBudget;
		char buffer[768];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O CGameClient::ProcessClientInfo enter client=%p msg=%p sendTableCrc=%d clientProtocol=%d serverCount=%d serverSpawn=%d replay=%d clientState=%d spawnMismatch=%d budget=%d\n",
			reinterpret_cast<void*>(client),
			reinterpret_cast<void*>(message),
			sendTableCrc,
			clientProtocol,
			serverCount,
			serverSpawn,
			replay,
			clientState,
			serverSpawn != -1 && serverCount != serverSpawn,
			s_R1OClientInfoHandlerLogBudget);
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}

	const char result = R1OCGameClientProcessClientInfoOriginal
		? R1OCGameClientProcessClientInfoOriginal(client, message)
		: 0;

	if (IsR1ODedicatedServer() && s_R1OClientInfoHandlerLogBudget > 0) {
		--s_R1OClientInfoHandlerLogBudget;
		int clientStateAfter = -999;
		__try {
			if (client && IsReadableRange(reinterpret_cast<void*>(client + 1136), sizeof(int)))
				clientStateAfter = *reinterpret_cast<int*>(client + 1136);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
		}
		char buffer[512];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O CGameClient::ProcessClientInfo leave client=%p msg=%p result=%d serverCount=%d serverSpawn=%d clientStateAfter=%d budget=%d\n",
			reinterpret_cast<void*>(client),
			reinterpret_cast<void*>(message),
			static_cast<int>(result),
			serverCount,
			serverSpawn,
			clientStateAfter,
			s_R1OClientInfoHandlerLogBudget);
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}

	return result;
}

static void R1OPrepareSignonStateForLegacyClient(__int64 message, R1OLegacySignonStateFixup* fixup)
{
	if (!message || !fixup || !IsReadableRange(reinterpret_cast<void*>(message + 32), 284))
		return;

	__try {
		const int state = *reinterpret_cast<int*>(message + 32);
		char* legacyPlaylistName = reinterpret_cast<char*>(message + 188);
		if (state != 3 || legacyPlaylistName[0])
			return;

		const char* gameMode = R1OSignonStateStringBuffer(message, 112, 136, 32);
		if (!gameMode || !gameMode[0])
			return;

		memcpy(fixup->oldPlaylistName, legacyPlaylistName, sizeof(fixup->oldPlaylistName));
		strncpy_s(legacyPlaylistName, 128, gameMode, _TRUNCATE);
		fixup->playlistNameChanged = true;

		if (s_R1OSignonStateLegacyPayloadLogBudget > 0) {
			--s_R1OSignonStateLegacyPayloadLogBudget;
			char buffer[512];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: R1O legacy NET_SignonState payload fix msg=%p state=%d gameMode=\"%.*s\" playlistName=\"%s\" loading=%d soundChecksum=%d budget=%d\n",
				reinterpret_cast<void*>(message),
				state,
				32,
				gameMode,
				legacyPlaylistName,
				static_cast<int>(*reinterpret_cast<unsigned char*>(message + 180)),
				*reinterpret_cast<int*>(message + 184),
				s_R1OSignonStateLegacyPayloadLogBudget);
			OutputDebugStringA(buffer);
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
	}
}

static void R1ORestoreSignonStateForLegacyClient(__int64 message, const R1OLegacySignonStateFixup& fixup)
{
	if (!message || !fixup.playlistNameChanged)
		return;

	__try {
		memcpy(reinterpret_cast<char*>(message + 188), fixup.oldPlaylistName, sizeof(fixup.oldPlaylistName));
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
	}
}

static bool R1OTranslateSignonStateForLegacyClient(__int64 message, int* originalSignonState, int* translatedSignonState)
{
	if (originalSignonState)
		*originalSignonState = -1;
	if (translatedSignonState)
		*translatedSignonState = -1;

	const char* messageName = R1ONetMessageName(message);
	if (!message
		|| !messageName
		|| strcmp_static(messageName, "net_SignonState")
		|| !IsReadableRange(reinterpret_cast<void*>(message + 32), sizeof(int)))
		return false;

	const int original = *reinterpret_cast<int*>(message + 32);
	const int translated = original;

	if (originalSignonState)
		*originalSignonState = original;
	if (translatedSignonState)
		*translatedSignonState = translated;

	return true;
}

static __int64 R1OActiveOutgoingNetMessage()
{
	if (s_R1ONetMessageWriteToBufferDepth > 0 && s_R1ONetMessageWriteToBufferMessage)
		return s_R1ONetMessageWriteToBufferMessage;
	if (s_R1ONetChanSendNetMsgDepth > 0 && s_R1ONetChanSendNetMsgMessage)
		return s_R1ONetChanSendNetMsgMessage;
	return 0;
}

static int R1OMessageIntField(__int64 message, size_t offset, int fallback = -1)
{
	return message && IsReadableRange(reinterpret_cast<void*>(message + offset), sizeof(int))
		? *reinterpret_cast<int*>(message + offset)
		: fallback;
}

static unsigned char R1OMessageByteField(__int64 message, size_t offset, unsigned char fallback = 0xff)
{
	return message && IsReadableRange(reinterpret_cast<void*>(message + offset), sizeof(unsigned char))
		? *reinterpret_cast<unsigned char*>(message + offset)
		: fallback;
}

static void R1OLogSnapshotWrite(const char* phase, __int64 message, __int64 bitBuffer, int startBit, int endBit, int result)
{
	if (!IsR1ODedicatedServer() || s_R1OSnapshotWriteLogBudget <= 0)
		return;

	const char* name = R1ONetMessageName(message);
	if (!name || strcmp_static(name, "svc_Snapshot"))
		return;

	--s_R1OSnapshotWriteLogBudget;
	char buffer[1024];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O svc_Snapshot write %s msg=%p bitbuf=%p bits=%d->%d result=%d fields i32={32:%d 36:%d 44:%d 48:%d 52:%d 56:%d 60:%d 68:%d 72:%d} bytes={40:%u 41:%u 42:%u 43:%u 64:%u} vtableRva=0x%llX budget=%d\n",
		phase ? phase : "?",
		reinterpret_cast<void*>(message),
		reinterpret_cast<void*>(bitBuffer),
		startBit,
		endBit,
		result,
		R1OMessageIntField(message, 32),
		R1OMessageIntField(message, 36),
		R1OMessageIntField(message, 44),
		R1OMessageIntField(message, 48),
		R1OMessageIntField(message, 52),
		R1OMessageIntField(message, 56),
		R1OMessageIntField(message, 60),
		R1OMessageIntField(message, 68),
		R1OMessageIntField(message, 72),
		static_cast<unsigned int>(R1OMessageByteField(message, 40)),
		static_cast<unsigned int>(R1OMessageByteField(message, 41)),
		static_cast<unsigned int>(R1OMessageByteField(message, 42)),
		static_cast<unsigned int>(R1OMessageByteField(message, 43)),
		static_cast<unsigned int>(R1OMessageByteField(message, 64)),
		static_cast<unsigned long long>(R1ONetMessageVtableRva(message)),
		s_R1OSnapshotWriteLogBudget);
	OutputDebugStringA(buffer);
	Warning("%s", buffer);
}

static char __fastcall R1ONetMessageWriteToBuffer(__int64 message, __int64 bitBuffer)
{
	const __int64 previousMessage = s_R1ONetMessageWriteToBufferMessage;
	++s_R1ONetMessageWriteToBufferDepth;
	s_R1ONetMessageWriteToBufferMessage = message;
	const int snapshotStartBit = static_cast<int>(R1OBitBufferCurrentBit(bitBuffer));
	R1OLogSnapshotWrite("enter", message, bitBuffer, snapshotStartBit, snapshotStartBit, -1);

	int originalSignonState = -1;
	int translatedSignonState = -1;
	R1OLegacySignonStateFixup signonFixup;
	const bool translatedSignon = IsR1ODedicatedServer()
		&& s_R1ONetChanSendNetMsgDepth == 0
		&& R1OTranslateSignonStateForLegacyClient(message, &originalSignonState, &translatedSignonState);
	if (translatedSignon)
		R1OPrepareSignonStateForLegacyClient(message, &signonFixup);

	char result = 0;
	if (R1ONetMessageWriteToBufferOriginal)
		result = R1ONetMessageWriteToBufferOriginal(message, bitBuffer);
	R1OLogSnapshotWrite("leave", message, bitBuffer, snapshotStartBit, static_cast<int>(R1OBitBufferCurrentBit(bitBuffer)), static_cast<int>(result));

	if (translatedSignon)
		R1ORestoreSignonStateForLegacyClient(message, signonFixup);
	if (translatedSignon && translatedSignonState != originalSignonState)
		*reinterpret_cast<int*>(message + 32) = originalSignonState;

	if (translatedSignon && s_R1ONetMessageWriteHeaderLogBudget > 0) {
		--s_R1ONetMessageWriteHeaderLogBudget;
		char buffer[512];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O direct NET_SignonState write msg=%p bitbuf=%p state=%d->%d result=%d sendNetMsgDepth=%d budget=%d\n",
			reinterpret_cast<void*>(message),
			reinterpret_cast<void*>(bitBuffer),
			originalSignonState,
			translatedSignonState,
			static_cast<int>(result),
			s_R1ONetChanSendNetMsgDepth,
			s_R1ONetMessageWriteHeaderLogBudget);
		OutputDebugStringA(buffer);
	}

	if (!result) {
		char extra[128];
		_snprintf_s(
			extra,
			sizeof(extra),
			_TRUNCATE,
			"sendNetMsgDepth=%d writeDepth=%d original=%p",
			s_R1ONetChanSendNetMsgDepth,
			s_R1ONetMessageWriteToBufferDepth,
			reinterpret_cast<void*>(R1ONetMessageWriteToBufferOriginal));
		LogR1ONetMessageFailure("WRITE", 0, bitBuffer, message, R1ONetMessageLegacyIdFromVtable(message), result, extra);
	}

	s_R1ONetMessageWriteToBufferMessage = previousMessage;
	--s_R1ONetMessageWriteToBufferDepth;
	return result;
}

static unsigned int R1OEffectiveOutgoingNetMessageId(unsigned int id, __int64 activeMessage, const char* directName = nullptr)
{
	if (!IsR1ODedicatedServer())
		return id;

	if (activeMessage) {
		const int virtualId = R1ONetMessageIntVFunc(activeMessage, 8);
		if (virtualId >= 0 && virtualId <= 63)
			return static_cast<unsigned int>(virtualId);
	}

	if (IsReadableCString(directName)) {
		for (const R1ONetMessageGetIdOverride& overrideEntry : s_R1ONetMessageGetIdOverrides) {
			if (!_stricmp(overrideEntry.name, directName))
				return static_cast<unsigned int>(overrideEntry.r1Id);
		}
	}

	return id;
}

static bool __fastcall R1ONetMessageWriteHeader(unsigned int id, __int64 bitBuffer)
{
	const __int64 activeMessage = R1OActiveOutgoingNetMessage();
	const unsigned int effectiveId = R1OEffectiveOutgoingNetMessageId(id, activeMessage);
	if (IsR1ODedicatedServer() && effectiveId != 8 && s_R1ONetMessageWriteHeaderLogBudget > 0) {
		--s_R1ONetMessageWriteHeaderLogBudget;
		char buffer[640];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O netmsg header write id=%u effectiveId=%u bitbuf=%p sendNetMsgDepth=%d writeDepth=%d activeMsg=%p activeName=%s budget=%d\n",
			id,
			effectiveId,
			reinterpret_cast<void*>(bitBuffer),
			s_R1ONetChanSendNetMsgDepth,
			s_R1ONetMessageWriteToBufferDepth,
			reinterpret_cast<void*>(activeMessage),
			activeMessage ? R1ONetMessageNameFromVtable(activeMessage) : "(null)",
			s_R1ONetMessageWriteHeaderLogBudget);
		OutputDebugStringA(buffer);
	}

	return R1ONetMessageWriteHeaderOriginal
		? R1ONetMessageWriteHeaderOriginal(effectiveId, bitBuffer)
		: false;
}

static bool IsLegacyEngineNetMessageName(const char* name)
{
	if (!IsReadableCString(name))
		return false;

	return _strnicmp(name, "svc_", 4) == 0
		|| _strnicmp(name, "net_", 4) == 0
		|| _strnicmp(name, "clc_", 4) == 0;
}

static char __fastcall R1ONetMessageWritePrelude(unsigned int id, const char* name, __int64 bitBuffer)
{
	if (!IsR1ODedicatedServer()) {
		return R1ONetMessageWritePreludeOriginal
			? R1ONetMessageWritePreludeOriginal(id, name, bitBuffer)
			: 0;
	}

	const bool writeLegacyIdOnly = IsLegacyEngineNetMessageName(name);
	if (!writeLegacyIdOnly) {
		return R1ONetMessageWritePreludeOriginal
			? R1ONetMessageWritePreludeOriginal(id, name, bitBuffer)
			: 0;
	}

	const __int64 activeMessage = R1OActiveOutgoingNetMessage();
	const int startBit = bitBuffer && IsReadableRange(reinterpret_cast<void*>(bitBuffer), sizeof(bf_write))
		? reinterpret_cast<bf_write*>(bitBuffer)->GetNumBitsWritten()
		: -1;
	const unsigned int effectiveId = R1OEffectiveOutgoingNetMessageId(id, activeMessage, name);
	const bool result = R1ONetMessageWriteHeaderOriginal
		? R1ONetMessageWriteHeaderOriginal(effectiveId, bitBuffer)
		: false;
	const int endBit = bitBuffer && IsReadableRange(reinterpret_cast<void*>(bitBuffer), sizeof(bf_write))
		? reinterpret_cast<bf_write*>(bitBuffer)->GetNumBitsWritten()
		: -1;

	const bool forcePreludeLog = AreR1OFakeDediVerboseLogsEnabled() && (effectiveId != id || effectiveId == 31 || effectiveId == 36 || id == 31 || id == 36);
	if ((forcePreludeLog || s_R1ONetMessageWritePreludeLogBudget > 0) && effectiveId != 8) {
		if (!forcePreludeLog)
			--s_R1ONetMessageWritePreludeLogBudget;
		char buffer[640];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O direct netmsg prelude id=%u effectiveId=%u name=%s bitbuf=%p bits=%d->%d result=%d activeMsg=%p activeName=%s budget=%d\n",
			id,
			effectiveId,
			IsReadableCString(name) ? name : "<invalid>",
			reinterpret_cast<void*>(bitBuffer),
			startBit,
			endBit,
			static_cast<int>(result),
			reinterpret_cast<void*>(activeMessage),
			activeMessage ? R1ONetMessageNameFromVtable(activeMessage) : "(null)",
			s_R1ONetMessageWritePreludeLogBudget);
		OutputDebugStringA(buffer);
	}

	return result ? 1 : 0;
}

static __int64 __fastcall R1ONetMessageWriteTrailer(__int64 bitBuffer)
{
	if (!IsR1ODedicatedServer()) {
		return R1ONetMessageWriteTrailerOriginal
			? R1ONetMessageWriteTrailerOriginal(bitBuffer)
			: 0;
	}

	if (s_R1ONetMessageWriteTrailerLogBudget > 0) {
		--s_R1ONetMessageWriteTrailerLogBudget;
		const int bit = bitBuffer && IsReadableRange(reinterpret_cast<void*>(bitBuffer), sizeof(bf_write))
			? reinterpret_cast<bf_write*>(bitBuffer)->GetNumBitsWritten()
			: -1;
		char buffer[256];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O omitted direct netmsg 31-bit trailer for legacy client bitbuf=%p bit=%d budget=%d\n",
			reinterpret_cast<void*>(bitBuffer),
			bit,
			s_R1ONetMessageWriteTrailerLogBudget);
		OutputDebugStringA(buffer);
	}

	return 1;
}

static void InstallR1OServerNetMessageGetIdOverrides()
{
	if (!IsR1ODedicatedServer() || !engineR1O || s_R1OServerNetMessageGetIdOverridesInstalled)
		return;

	int patchedCount = 0;
	for (R1ONetMessageGetIdOverride& overrideEntry : s_R1ONetMessageGetIdOverrides) {
		const uintptr_t slotRva = overrideEntry.vtableRva + 8 * sizeof(uintptr_t);
		overrideEntry.originalGetId = R1OHookGlobalValue<uintptr_t>(slotRva, 0);
		if (!overrideEntry.originalGetId)
			continue;

		if (WriteR1OHookGlobalValue<uintptr_t>(slotRva, reinterpret_cast<uintptr_t>(&R1ONetMessageGetIdOverrideThunk)))
			++patchedCount;
	}

	s_R1OServerNetMessageGetIdOverridesInstalled = patchedCount > 0;
	char buffer[512];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O installed per-vtable legacy GetId overrides patched=%d total=%zu\n",
		patchedCount,
		sizeof(s_R1ONetMessageGetIdOverrides) / sizeof(s_R1ONetMessageGetIdOverrides[0]));
	OutputDebugStringA(buffer);
}

static __int64 R1ONetMessageAt(__int64 registry, int index)
{
	if (!registry || index < 0)
		return 0;

	const int count = IsReadableRange(reinterpret_cast<void*>(registry + 16112), sizeof(int))
		? *reinterpret_cast<int*>(registry + 16112)
		: -1;
	if (index >= count)
		return 0;

	const __int64 array = IsReadableRange(reinterpret_cast<void*>(registry + 16088), sizeof(__int64))
		? *reinterpret_cast<__int64*>(registry + 16088)
		: 0;
	if (!array || !IsReadableRange(reinterpret_cast<void*>(array + 8LL * index), sizeof(__int64)))
		return 0;

	return *reinterpret_cast<__int64*>(array + 8LL * index);
}

static __int64 FindR1ONetMessageByName(__int64 registry, const char* name)
{
	if (!registry || !name)
		return 0;

	const int count = IsReadableRange(reinterpret_cast<void*>(registry + 16112), sizeof(int))
		? *reinterpret_cast<int*>(registry + 16112)
		: -1;
	for (int i = 0; i < count; ++i) {
		const __int64 message = R1ONetMessageAt(registry, i);
		const char* messageName = R1ONetMessageName(message);
		if (message && messageName && _stricmp(messageName, name) == 0)
			return message;
	}

	return 0;
}

struct R1ONetMessageProcessHook {
	uintptr_t target;
	uintptr_t vtableRva;
	R1ONetMessageProcessType original;
};

static R1ONetMessageProcessHook s_R1ONetMessageProcessHooks[64];
static int s_R1ONetMessageProcessHookCount;

static R1ONetMessageProcessHook* FindR1ONetMessageProcessHook(__int64 message)
{
	const uintptr_t vtableRva = R1ONetMessageVtableRva(message);
	for (int i = 0; i < s_R1ONetMessageProcessHookCount; ++i) {
		if (s_R1ONetMessageProcessHooks[i].vtableRva == vtableRva)
			return &s_R1ONetMessageProcessHooks[i];
	}

	return nullptr;
}

static char __fastcall R1ONetMessageProcess(__int64 message)
{
	R1ONetMessageProcessHook* hook = FindR1ONetMessageProcessHook(message);
	const char* messageName = R1ONetMessageName(message);
	const bool clientMove = IsR1ODedicatedServer()
		&& messageName
		&& !strcmp_static(messageName, "clc_Move");
	const uintptr_t processVtable = message && IsReadableRange(reinterpret_cast<void*>(message), sizeof(uintptr_t))
		? *reinterpret_cast<uintptr_t*>(message)
		: 0;
	const int moveBackupCommands = clientMove && IsReadableRange(reinterpret_cast<void*>(message + 32), sizeof(int))
		? *reinterpret_cast<int*>(message + 32)
		: -1;
	const int moveNewCommands = clientMove && IsReadableRange(reinterpret_cast<void*>(message + 36), sizeof(int))
		? *reinterpret_cast<int*>(message + 36)
		: -1;
	const int movePayloadBits = clientMove && IsReadableRange(reinterpret_cast<void*>(message + 40), sizeof(int))
		? *reinterpret_cast<int*>(message + 40)
		: -1;
	const __int64 moveReader = clientMove ? message + 48 : 0;
	const __int64 moveStartBit = moveReader ? R1OBitBufferCurrentBit(moveReader) : -1;
	const int moveStartBitsLeft = moveReader ? R1OBitBufferGetNumBitsLeft(moveReader) : -1;
	const int moveStartDataBits = moveReader ? R1OBitBufferIntField(moveReader, 16, -1) : -1;
	const unsigned int moveStartOverflow = moveReader
		? static_cast<unsigned int>(R1OBitBufferByteField(moveReader, 8, 0xff))
		: 0xff;
	const bool signonState = IsR1ODedicatedServer()
		&& messageName
		&& !strcmp_static(messageName, "net_SignonState");
	if (signonState) {
		LogR1OSignonStateTrace("process-enter", message, 0, -1);
		LogR1OClientSlotSnapshot("before R1O net_SignonState process", true);
	}
	char result = hook && hook->original ? hook->original(message) : 0;
	if (clientMove && !result) {
		const __int64 moveEndBit = moveReader ? R1OBitBufferCurrentBit(moveReader) : -1;
		const int moveEndBitsLeft = moveReader ? R1OBitBufferGetNumBitsLeft(moveReader) : -1;
		const int moveEndDataBits = moveReader ? R1OBitBufferIntField(moveReader, 16, -1) : -1;
		const unsigned int moveEndOverflow = moveReader
			? static_cast<unsigned int>(R1OBitBufferByteField(moveReader, 8, 0xff))
			: 0xff;
		char moveFailureBuffer[768];
		_snprintf_s(
			moveFailureBuffer,
			sizeof(moveFailureBuffer),
			_TRUNCATE,
			"R1Delta: R1O CLC_Move process failed msg=%p vtable=%p backup=%d new=%d total=%d payloadBits=%d reader=%p startBit=%lld endBit=%lld consumed=%lld startBitsLeft=%d endBitsLeft=%d startDataBits=%d endDataBits=%d startOverflow=%u endOverflow=%u\n",
			reinterpret_cast<void*>(message),
			reinterpret_cast<void*>(processVtable),
			moveBackupCommands,
			moveNewCommands,
			moveBackupCommands >= 0 && moveNewCommands >= 0 ? moveBackupCommands + moveNewCommands : -1,
			movePayloadBits,
			reinterpret_cast<void*>(moveReader),
			static_cast<long long>(moveStartBit),
			static_cast<long long>(moveEndBit),
			moveStartBit >= 0 && moveEndBit >= 0 ? static_cast<long long>(moveEndBit - moveStartBit) : -1LL,
			moveStartBitsLeft,
			moveEndBitsLeft,
			moveStartDataBits,
			moveEndDataBits,
			moveStartOverflow,
			moveEndOverflow);
		OutputDebugStringA(moveFailureBuffer);
		Warning("%s", moveFailureBuffer);
	}
	if (signonState) {
		LogR1OSignonStateTrace("process-leave", message, 0, result);
		LogR1OClientSlotSnapshot("after R1O net_SignonState process", true);
	}

	if (IsR1ODedicatedServer()
		&& result
		&& messageName
		&& !strcmp_static(messageName, "net_SignonState")
		&& IsReadableRange(reinterpret_cast<void*>(message + 32), sizeof(int))
		&& *reinterpret_cast<int*>(message + 32) == 2) {
		MarkR1OServerInfoPendingForConnectedClients("after processing client net_SignonState(2)");
	}

	if (IsR1ODedicatedServer() && s_R1ONetMessageProcessLogBudget > 0) {
		--s_R1ONetMessageProcessLogBudget;
		char buffer[512];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O netmsg process msg=%p name=%s vtableRva=0x%llX id8=%d result=%d budget=%d\n",
			reinterpret_cast<void*>(message),
			messageName,
			static_cast<unsigned long long>(R1ONetMessageVtableRva(message)),
			R1ONetMessageLegacyIdFromVtable(message),
			static_cast<int>(result),
			s_R1ONetMessageProcessLogBudget);
		OutputDebugStringA(buffer);
	}

	if (!result) {
		char extra[160];
		_snprintf_s(
			extra,
			sizeof(extra),
			_TRUNCATE,
			"processOriginal=%p hookTarget=%p",
			hook ? reinterpret_cast<void*>(hook->original) : nullptr,
			hook ? reinterpret_cast<void*>(hook->target) : nullptr);
		LogR1ONetMessageFailure("PROCESS", 0, 0, message, R1ONetMessageLegacyIdFromVtable(message), result, extra);
	}

	return result;
}

static void EnsureR1ONetMessageProcessHook(__int64 message)
{
	if (!IsR1ODedicatedServer() || !message || s_R1ONetMessageProcessHookCount >= static_cast<int>(sizeof(s_R1ONetMessageProcessHooks) / sizeof(s_R1ONetMessageProcessHooks[0])))
		return;

	if (!IsReadableRange(reinterpret_cast<void*>(message), sizeof(void*)))
		return;

	const uintptr_t vtableRva = R1ONetMessageVtableRva(message);
	if (!vtableRva || FindR1ONetMessageProcessHook(message))
		return;

	uintptr_t* vtable = *reinterpret_cast<uintptr_t**>(message);
	if (!vtable || !IsReadableRange(vtable, sizeof(uintptr_t) * 4) || !vtable[3])
		return;

	R1ONetMessageProcessHook& hook = s_R1ONetMessageProcessHooks[s_R1ONetMessageProcessHookCount];
	hook.target = vtable[3];
	hook.vtableRva = vtableRva;
	hook.original = nullptr;

	const MH_STATUS status = MH_CreateHook(
		reinterpret_cast<void*>(hook.target),
		&R1ONetMessageProcess,
		reinterpret_cast<LPVOID*>(&hook.original));
	const MH_STATUS enableStatus = (status == MH_OK || status == MH_ERROR_ALREADY_CREATED)
		? MH_EnableHook(reinterpret_cast<void*>(hook.target))
		: status;

	if (status == MH_OK) {
		++s_R1ONetMessageProcessHookCount;
	}

	char buffer[512];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O netmsg process hook status=%d enable=%d msg=%p name=%s vtableRva=0x%llX target=%p original=%p count=%d\n",
		static_cast<int>(status),
		static_cast<int>(enableStatus),
		reinterpret_cast<void*>(message),
		R1ONetMessageNameFromVtable(message),
		static_cast<unsigned long long>(vtableRva),
		reinterpret_cast<void*>(hook.target),
		reinterpret_cast<void*>(hook.original),
		s_R1ONetMessageProcessHookCount);
	OutputDebugStringA(buffer);
}

struct R1OPersistenceOwner
{
	int playerSlot = -1;
	PersistentDataState::SessionKey session;
};

static int R1OPersistencePlayerSlotFromNetChannel(__int64 netChannel)
{
	if (!netChannel || !engineR1O)
		return -1;

	const int maxClients = R1OHookGlobalValue<int>(0x265971C, 0);
	if (maxClients <= 0 || maxClients > PersistentDataSlots::kMaximumSupportedClients)
		return -1;

	const uintptr_t clientArray = reinterpret_cast<uintptr_t>(engineR1O) + kR1OClientArrayRva;
	for (int i = 0; i < maxClients; ++i) {
		const uintptr_t client = clientArray + kR1OClientStride * static_cast<size_t>(i);
		if (!IsReadableRange(reinterpret_cast<void*>(client + kR1OClientNetChanOffset), sizeof(void*)))
			continue;
		if (*reinterpret_cast<__int64*>(client + kR1OClientNetChanOffset) == netChannel)
			return i;
	}
	return -1;
}

static bool R1OResolvePersistenceOwner(__int64 message, R1OPersistenceOwner& owner)
{
	if (!message || !engineR1O
		|| !IsReadableRange(reinterpret_cast<void*>(message + 24), sizeof(void*)))
		return false;

	const uintptr_t handler = reinterpret_cast<uintptr_t>(*reinterpret_cast<void**>(message + 24));
	const int maxClients = R1OHookGlobalValue<int>(0x265971C, 0);
	if (!handler || maxClients <= 0 || maxClients > PersistentDataSlots::kMaximumSupportedClients)
		return false;

	const uintptr_t clientArray = reinterpret_cast<uintptr_t>(engineR1O) + kR1OClientArrayRva;
	for (int i = 0; i < maxClients; ++i) {
		const uintptr_t client = clientArray + kR1OClientStride * static_cast<size_t>(i);
		const uintptr_t fullClient = client - kR1OClientSubobjectOffset;
		if (handler < fullClient || handler >= fullClient + kR1OClientStride)
			continue;

		if (!IsReadableRange(reinterpret_cast<void*>(client + kR1OClientNetChanOffset), sizeof(void*)))
			return false;
		const uintptr_t netChannel = reinterpret_cast<uintptr_t>(
			*reinterpret_cast<void**>(client + kR1OClientNetChanOffset));
		if (!netChannel)
			return false;

		int userId = -1;
		R1OClientCommandIdentity identity = {};
		if (ReadR1OClientCommandIdentity(i, &identity))
			userId = identity.userId;

		owner.playerSlot = i;
		owner.session.netChannel = netChannel;
		owner.session.userId = userId;
		return true;
	}
	return false;
}

static bool __fastcall R1ONETSetConVarReadFromBuffer(__int64 message, __int64 bitBuffer)
{
	const __int64 startBit = R1OBitBufferCurrentBit(bitBuffer);
	const uint8_t byteCount = static_cast<uint8_t>(R1OBitBufferReadUBitLong(bitBuffer, 8));
	uint32_t count = byteCount;
	if (byteCount == static_cast<uint8_t>(-1))
		count = R1OBitBufferReadUBitVar(bitBuffer);

	if (count > 4096 * 4) {
		char buffer[256];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O NET_SetConVar custom read rejected count=%u startBit=%lld bitsRead=%lld\n",
			count,
			static_cast<long long>(startBit),
			static_cast<long long>(R1OBitBufferCurrentBit(bitBuffer)));
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
		return false;
	}

	__int64 vector = message + 32;
	std::vector<NetMessageCvar_t> staged;
	staged.reserve(count);
	std::string packedPData;
	bool sawPackedPData = false;
	bool sawLegacyPData = false;
	for (uint32_t i = 0; i < count; ++i) {
		NetMessageCvar_t var = {};
		const bool readName = R1OBFReadString
			? R1OBFReadString(bitBuffer, var.name, sizeof(var.name), 0, nullptr) != 0
			: false;
		const bool readValue = R1OBFReadString
			? R1OBFReadString(bitBuffer, var.value, sizeof(var.value), 0, nullptr) != 0
			: false;
		if (!readName || !readValue) {
			char buffer[256];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: R1O NET_SetConVar custom read failed string index=%u/%u startBit=%lld bitsRead=%lld overflow=%d\n",
				i,
				count,
				static_cast<long long>(startBit),
				static_cast<long long>(R1OBitBufferCurrentBit(bitBuffer)),
				static_cast<int>(R1OBitBufferIsOverflowed(bitBuffer)));
			OutputDebugStringA(buffer);
			Warning("%s", buffer);
			return false;
		}

		if (IsPackedPDataWireName(var.name)) {
			constexpr size_t maxPackedPDataSize = 4 * 1024 * 1024;
			const size_t chunkLength = strlen(var.value);
			if (!chunkLength || packedPData.size() > maxPackedPDataSize
				|| maxPackedPDataSize - packedPData.size() < chunkLength) {
				Warning("Invalid packed persistent data chunk\n");
				return false;
			}
			sawPackedPData = true;
			packedPData.append(var.value, chunkLength);
			continue;
		}

		if (static_cast<unsigned char>(var.name[0]) & 0x80) {
			sawLegacyPData = true;
			var.name[0] = static_cast<char>(static_cast<unsigned char>(var.name[0]) & 0x7F);

			std::string nameStr(var.name);
			std::string valueStr(var.value);
			if (!PDef::IsValidKeyAndValue(nameStr, valueStr)) {
				Warning("Invalid persistent data convar: key=%s value=%s\n", var.name, var.value);
				return false;
			}

			if (!SafePrefixConVarName(var.name, sizeof(var.name), PERSIST_COMMAND" ")) {
				Warning("Failed to prefix persistent data convar\n");
				return false;
			}
		}
		else {
			if (::_stricmp(var.name, "networkid_force") == 0)
				continue;

			int flags = 0;
			if (OriginalCCVar_FindVar && cvarinterface) {
				if (auto* cvar = OriginalCCVar_FindVar(cvarinterface, var.name))
					flags = cvar->m_nFlags;
			}
			if (!(flags & (FCVAR_USERINFO | FCVAR_REPLICATED))) {
				Warning("Invalid userinfo convar (doesn't exist or missing FCVAR_USERINFO or FCVAR_REPLICATED flag): %s\n", var.name);
				continue;
			}
		}

		staged.push_back(var);
	}

	if (sawPackedPData) {
		if (sawLegacyPData) {
			Warning("Mixed packed and legacy persistent data payload\n");
			return false;
		}
		std::vector<NetMessageCvar_t> decoded;
		if (!DecodePackedPDataWire(packedPData, decoded)) {
			Warning("Failed to decode packed persistent data payload\n");
			return false;
		}
		staged.insert(staged.end(), decoded.begin(), decoded.end());
	}

	const bool result = !R1OBitBufferIsOverflowed(bitBuffer);
	if (result) {
		if (!R1ONETSetConVarAddToTail
			|| !IsReadableRange(reinterpret_cast<void*>(vector + 24), sizeof(int))) {
			Warning("R1Delta: R1O NET_SetConVar custom read missing vector commit support\n");
			return false;
		}

		const bool hasPersistentData = sawPackedPData || sawLegacyPData;
		R1OPersistenceOwner persistenceOwner;
		if (hasPersistentData && !R1OResolvePersistenceOwner(message, persistenceOwner)) {
			Warning("R1Delta: R1O NET_SetConVar could not resolve persistence owner\n");
			return false;
		}

		*reinterpret_cast<int*>(vector + 24) = 0;
		for (NetMessageCvar_t& var : staged)
			R1ONETSetConVarAddToTail(vector, &var);

		if (hasPersistentData) {
			const bool stored = sawPackedPData
				? R1OReplacePersistentUserDataForPlayer(
					persistenceOwner.playerSlot, persistenceOwner.session, staged)
				: R1OMergePersistentUserDataForPlayer(
					persistenceOwner.playerSlot, persistenceOwner.session, staged);
			if (!stored) {
				Warning(
					"R1Delta: R1O NET_SetConVar failed to %s persistence for player slot %d\n",
					sawPackedPData ? "replace" : "merge",
					persistenceOwner.playerSlot);
				return false;
			}
		}

		if (sawPackedPData) {
			static int packedPersistenceCommitLogBudget = 32;
			if (packedPersistenceCommitLogBudget-- > 0) {
				char messageBuffer[256];
				_snprintf_s(
					messageBuffer,
					sizeof(messageBuffer),
					_TRUNCATE,
					"R1Delta: R1O NET_SetConVar committed playerSlot=%d total=%zu encodedBytes=%zu\n",
					persistenceOwner.playerSlot,
					staged.size(),
					packedPData.size());
				OutputDebugStringA(messageBuffer);
			}
		}
	}
	if (IsR1ODedicatedServer() && s_R1ONETSetConVarReadLogBudget > 0) {
		--s_R1ONETSetConVarReadLogBudget;
		char buffer[512];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O NET_SetConVar custom read msg=%p count=%u startBit=%lld endBit=%lld bitsLeft=%d result=%d first=%s budget=%d\n",
			reinterpret_cast<void*>(message),
			count,
			static_cast<long long>(startBit),
			static_cast<long long>(R1OBitBufferCurrentBit(bitBuffer)),
			R1OBitBufferGetNumBitsLeft(bitBuffer),
			static_cast<int>(result),
			count > 0 && IsReadableRange(reinterpret_cast<void*>(vector), sizeof(void*)) && *reinterpret_cast<void**>(vector)
				? reinterpret_cast<NetMessageCvar_t*>(*reinterpret_cast<void**>(vector))[0].name
				: "",
			s_R1ONETSetConVarReadLogBudget);
		OutputDebugStringA(buffer);
	}

	if (!result)
		LogR1ONetMessageFailure("READ", 0, bitBuffer, message, R1ONetMessageLegacyIdFromVtable(message), 0, "NET_SetConVar overflow");

	return result;
}

static bool R1OCLCClientInfoReadFromLegacyBuffer(__int64 message, __int64 bitBuffer)
{
	if (!message || !bitBuffer || !IsReadableRange(reinterpret_cast<void*>(message + 320), sizeof(int)))
		return false;

	const __int64 startBit = R1OBitBufferCurrentBit(bitBuffer);
	const unsigned int serverCount = R1OBitBufferReadUBitLong(bitBuffer, 32);
	const unsigned int sendTableCrc = R1OBitBufferReadUBitLong(bitBuffer, 32);
	const unsigned int clientProtocol = R1OBitBufferReadUBitLong(bitBuffer, 32);
	const bool isReplay = R1OBitBufferReadUBitLong(bitBuffer, 1) != 0;
	const unsigned int friendsId = R1OBitBufferReadUBitLong(bitBuffer, 32);

	*reinterpret_cast<int*>(message + 32) = static_cast<int>(sendTableCrc);
	*reinterpret_cast<int*>(message + 36) = static_cast<int>(clientProtocol);
	*reinterpret_cast<int*>(message + 40) = static_cast<int>(serverCount);
	*reinterpret_cast<unsigned char*>(message + 44) = isReplay ? 1 : 0;
	*reinterpret_cast<int*>(message + 48) = static_cast<int>(friendsId);
	memset(reinterpret_cast<void*>(message + 52), 0, 256);

	const bool readName = R1OBFReadString
		? R1OBFReadString(bitBuffer, reinterpret_cast<char*>(message + 52), 256, 0, nullptr) != 0
		: false;
	if (!readName)
		return false;

	for (int i = 0; i < 4; ++i) {
		const bool hasCustomFile = R1OBitBufferReadUBitLong(bitBuffer, 1) != 0;
		*reinterpret_cast<int*>(message + 308 + 4LL * i) = hasCustomFile
			? static_cast<int>(R1OBitBufferReadUBitLong(bitBuffer, 32))
			: 0;
	}

	const bool result = !R1OBitBufferIsOverflowed(bitBuffer);
	if (IsR1ODedicatedServer() && s_R1OClientInfoHandlerLogBudget > 0) {
		--s_R1OClientInfoHandlerLogBudget;
		char buffer[768];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O CLC_ClientInfo legacy read msg=%p result=%d serverCount=%u sendTableCrc=%u clientProtocol=%u replay=%d friendsId=%u name=\"%s\" startBit=%lld endBit=%lld bitsLeft=%d budget=%d\n",
			reinterpret_cast<void*>(message),
			static_cast<int>(result),
			serverCount,
			sendTableCrc,
			clientProtocol,
			static_cast<int>(isReplay),
			friendsId,
			reinterpret_cast<const char*>(message + 52),
			static_cast<long long>(startBit),
			static_cast<long long>(R1OBitBufferCurrentBit(bitBuffer)),
			R1OBitBufferGetNumBitsLeft(bitBuffer),
			s_R1OClientInfoHandlerLogBudget);
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}

	if (!result)
		LogR1ONetMessageFailure("READ", 0, bitBuffer, message, R1ONetMessageLegacyIdFromVtable(message), 0, "CLC_ClientInfo legacy overflow");

	return result;
}

static bool R1ONetMessageReadPayloadDirect(__int64 message, __int64 bitBuffer);

static bool R1OCLCMoveReadFromLegacyBuffer(__int64 message, __int64 bitBuffer)
{
	if (!message || !bitBuffer || !R1OBFReadSeek || !IsReadableRange(reinterpret_cast<void*>(message + 112), 64))
		return false;

	const __int64 startBit = R1OBitBufferCurrentBit(bitBuffer);
	const unsigned int legacyMarker = R1OBitBufferPeekUBitLong(bitBuffer, 7);
	if (legacyMarker != 0)
		return R1ONetMessageReadPayloadDirect(message, bitBuffer);

	R1OBitBufferReadUBitLong(bitBuffer, 7);
	const unsigned int legacyNewCommands = R1OBitBufferReadUBitLong(bitBuffer, 6);
	const unsigned int legacyBackupCommands = R1OBitBufferReadUBitLong(bitBuffer, 4);
	const unsigned int bitLength = R1OBitBufferReadUBitLong(bitBuffer, 16);
	const __int64 payloadStartBit = R1OBitBufferCurrentBit(bitBuffer);

	// R1 2015 writes an unencrypted legacy CLC_Move payload as: 7-bit zero marker,
	// 6-bit new command count, 4-bit backup count, 16-bit payload length, then raw usercmd bits.
	// Both R1 2015 and R1O pass dword[9] to ServerGameClients::ProcessUsercmds as
	// the new-command count and dword[8] only participates in the total
	// backup+new count. Keep the expanded 6-bit legacy new count intact here;
	// narrowing it would drop valid low-FPS/hitch commands before server_local
	// can process Titanfall's usercmd-per-frame movement.
	*reinterpret_cast<int*>(message + 36) = static_cast<int>(legacyNewCommands);
	*reinterpret_cast<int*>(message + 32) = static_cast<int>(legacyBackupCommands & 0xF);
	*reinterpret_cast<int*>(message + 40) = static_cast<int>(bitLength);
	memcpy(reinterpret_cast<void*>(message + 48), reinterpret_cast<void*>(bitBuffer), 64);

	bool result = !R1OBitBufferIsOverflowed(bitBuffer) && bitLength <= static_cast<unsigned int>(R1OBitBufferGetNumBitsLeft(bitBuffer));
	if (result)
		result = R1OBFReadSeek(reinterpret_cast<void*>(bitBuffer), payloadStartBit + bitLength) != 0;
	else
		R1OBitBufferSetOverflow(bitBuffer);

	if (IsR1ODedicatedServer() && s_R1OCLCMoveLegacyReadLogBudget > 0) {
		--s_R1OCLCMoveLegacyReadLogBudget;
		char buffer[768];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O CLC_Move legacy read msg=%p result=%d marker=%u new=%u storedNew=%d backup=%u storedBackup=%d length=%u startBit=%lld payloadStart=%lld endBit=%lld bitsLeft=%d overflow=%d budget=%d\n",
			reinterpret_cast<void*>(message),
			static_cast<int>(result),
			legacyMarker,
			legacyNewCommands,
			*reinterpret_cast<int*>(message + 36),
			legacyBackupCommands,
			*reinterpret_cast<int*>(message + 32),
			bitLength,
			static_cast<long long>(startBit),
			static_cast<long long>(payloadStartBit),
			static_cast<long long>(R1OBitBufferCurrentBit(bitBuffer)),
			R1OBitBufferGetNumBitsLeft(bitBuffer),
			static_cast<int>(R1OBitBufferIsOverflowed(bitBuffer)),
			s_R1OCLCMoveLegacyReadLogBudget);
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}

	if (!result)
		LogR1ONetMessageFailure("READ", 0, bitBuffer, message, R1ONetMessageLegacyIdFromVtable(message), 0, "CLC_Move legacy overflow");

	return result;
}

static void InstallR1ONETSetConVarReadHook(uintptr_t engineBase)
{
	if (!IsR1ODedicatedServer() || !engineBase || s_R1ONETSetConVarReadHooked)
		return;

	uintptr_t* vtable = reinterpret_cast<uintptr_t*>(engineBase + 0x532A98);
	if (!IsReadableRange(vtable, sizeof(uintptr_t) * 6) || !vtable[4])
		return;

	const MH_STATUS status = MH_CreateHook(
		reinterpret_cast<void*>(vtable[4]),
		&R1ONETSetConVarReadFromBuffer,
		reinterpret_cast<LPVOID*>(&R1ONETSetConVarReadFromBufferOriginal));
	const MH_STATUS enableStatus = (status == MH_OK || status == MH_ERROR_ALREADY_CREATED)
		? MH_EnableHook(reinterpret_cast<void*>(vtable[4]))
		: status;
	s_R1ONETSetConVarReadHooked = status == MH_OK || status == MH_ERROR_ALREADY_CREATED;

	char buffer[384];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O NET_SetConVar::ReadFromBuffer custom hook status=%d enable=%d target=%p original=%p vtable=%p\n",
		static_cast<int>(status),
		static_cast<int>(enableStatus),
		reinterpret_cast<void*>(vtable[4]),
		reinterpret_cast<void*>(R1ONETSetConVarReadFromBufferOriginal),
		reinterpret_cast<void*>(vtable));
	OutputDebugStringA(buffer);
}

static bool R1ONetMessageReadPayloadDirect(__int64 message, __int64 bitBuffer)
{
	if (!message || !IsReadableRange(reinterpret_cast<void*>(message), sizeof(uintptr_t)))
		return false;

	uintptr_t* vtable = *reinterpret_cast<uintptr_t**>(message);
	if (!vtable || !IsReadableRange(vtable, sizeof(uintptr_t) * 5) || !vtable[4])
		return false;

	return reinterpret_cast<R1ONetMessageReadFromBufferType>(vtable[4])(message, bitBuffer);
}

static void R1OLogStringCmdReadProbe(__int64 message, __int64 bitBuffer, const unsigned char* stateBeforeRead, const unsigned char* stateAfterRead, __int64 startBit)
{
	if (!IsR1ODedicatedServer()
		|| !message
		|| !bitBuffer
		|| !stateBeforeRead
		|| !stateAfterRead
		|| !R1OBFReadSeek
		|| !R1OBFReadString)
		return;

	const char* name = R1ONetMessageName(message);
	if (!name || _stricmp(name, "net_StringCmd") != 0)
		return;

	char probeSummary[1024] = {};
	size_t used = 0;
	for (int delta = -4; delta <= 4; ++delta) {
		const __int64 seekBit = startBit + delta;
		if (seekBit < 0)
			continue;

		memcpy(reinterpret_cast<void*>(bitBuffer), stateBeforeRead, 64);
		char command[160] = {};
		int chars = -1;
		const char seekOk = R1OBFReadSeek(reinterpret_cast<void*>(bitBuffer), seekBit);
		const char readOk = seekOk ? R1OBFReadString(bitBuffer, command, sizeof(command), 0, &chars) : 0;
		const int currentBit = static_cast<int>(R1OBitBufferCurrentBit(bitBuffer));
		const int overflow = static_cast<int>(R1OBitBufferByteField(bitBuffer, 8, 0xff));
		const int written = _snprintf_s(
			probeSummary + used,
			sizeof(probeSummary) - used,
			_TRUNCATE,
			"%s%+d:seek=%d read=%d chars=%d end=%d ov=%d cmd=\"%s\"",
			used ? " | " : "",
			delta,
			static_cast<int>(seekOk),
			static_cast<int>(readOk),
			chars,
			currentBit,
			overflow,
			command);
		if (written < 0)
			break;
		used += static_cast<size_t>(written);
		if (used >= sizeof(probeSummary) - 1)
			break;
	}

	memcpy(reinterpret_cast<void*>(bitBuffer), stateAfterRead, 64);

	char buffer[1280];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O net_StringCmd read probes msg=%p bitbuf=%p startBit=%lld dataBits=%d probes=[%s]\n",
		reinterpret_cast<void*>(message),
		reinterpret_cast<void*>(bitBuffer),
		static_cast<long long>(startBit),
		R1OBitBufferIntField(bitBuffer, 16),
		probeSummary);
	OutputDebugStringA(buffer);
	Warning("%s", buffer);
}

static void DumpR1ONetMessageRegistry(__int64 registry)
{
	if (s_R1ONetMessageRegistryDumped || !IsR1ODedicatedServer() || !registry)
		return;

	const int count = IsReadableRange(reinterpret_cast<void*>(registry + 16112), sizeof(int))
		? *reinterpret_cast<int*>(registry + 16112)
		: -1;
	if (count <= 0)
		return;

	s_R1ONetMessageRegistryDumped = true;
	for (int i = 0; i < count; ++i) {
		const __int64 message = R1ONetMessageAt(registry, i);
		char buffer[384];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O netmsg registry[%d/%d] msg=%p name=%s id8=%d id9=%d\n",
			i,
			count,
			reinterpret_cast<void*>(message),
			R1ONetMessageNameFromVtable(message),
			R1ONetMessageLegacyIdFromVtable(message),
			-1);
		OutputDebugStringA(buffer);
	}
}

static __int64 __fastcall R1ONetMessageLookup(__int64 registry, int id)
{
	__int64 result = R1ONetMessageLookupOriginal
		? R1ONetMessageLookupOriginal(registry, id)
		: 0;

	const int registeredCount = registry && IsReadableRange(reinterpret_cast<void*>(registry + 16112), sizeof(int))
		? *reinterpret_cast<int*>(registry + 16112)
		: -1;
	const bool dispatchLookup = s_R1ONetMessageDispatchDepth > 0;
	if (dispatchLookup && registeredCount > 0)
		DumpR1ONetMessageRegistry(registry);

	const char* legacyName = dispatchLookup && registeredCount > 0 ? R1LegacyNetMessageNameForId(id) : nullptr;

	if (!result && dispatchLookup) {
		const int cachedBits = R1OBitBufferIntField(s_R1ONetMessageActiveBitBuffer, 36);
		char unknownBuffer[768];
		_snprintf_s(
			unknownBuffer,
			sizeof(unknownBuffer),
			_TRUNCATE,
			"R1Delta: R1O unknown netmsg id=%d legacyName=%s bitbuf=%p cachedBits=%d q00=%llx q08=%llx q10=%llx q18=%llx q20=%llx q28=%llx q30=%llx q38=%llx i08=%d i10=%d i14=%d i20=%d i24=%d currentBit=%lld\n",
			id,
			legacyName ? legacyName : "",
			reinterpret_cast<void*>(s_R1ONetMessageActiveBitBuffer),
			cachedBits,
			static_cast<unsigned long long>(R1OBitBufferQwordField(s_R1ONetMessageActiveBitBuffer, 0, 0)),
			static_cast<unsigned long long>(R1OBitBufferQwordField(s_R1ONetMessageActiveBitBuffer, 8, 0)),
			static_cast<unsigned long long>(R1OBitBufferQwordField(s_R1ONetMessageActiveBitBuffer, 16, 0)),
			static_cast<unsigned long long>(R1OBitBufferQwordField(s_R1ONetMessageActiveBitBuffer, 24, 0)),
			static_cast<unsigned long long>(R1OBitBufferQwordField(s_R1ONetMessageActiveBitBuffer, 32, 0)),
			static_cast<unsigned long long>(R1OBitBufferQwordField(s_R1ONetMessageActiveBitBuffer, 40, 0)),
			static_cast<unsigned long long>(R1OBitBufferQwordField(s_R1ONetMessageActiveBitBuffer, 48, 0)),
			static_cast<unsigned long long>(R1OBitBufferQwordField(s_R1ONetMessageActiveBitBuffer, 56, 0)),
			R1OBitBufferIntField(s_R1ONetMessageActiveBitBuffer, 8),
			R1OBitBufferIntField(s_R1ONetMessageActiveBitBuffer, 16),
			R1OBitBufferIntField(s_R1ONetMessageActiveBitBuffer, 20),
			R1OBitBufferIntField(s_R1ONetMessageActiveBitBuffer, 32),
			R1OBitBufferIntField(s_R1ONetMessageActiveBitBuffer, 36),
			static_cast<long long>(R1OBitBufferCurrentBit(s_R1ONetMessageActiveBitBuffer)));
		OutputDebugStringA(unknownBuffer);
		Warning("%s", unknownBuffer);
	}

	if (dispatchLookup)
		EnsureR1ONetMessageProcessHook(result);

	if (IsR1ODedicatedServer() && s_R1ONetMessageLogBudget > 0 && dispatchLookup) {
		char buffer[512];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O netmsg lookup registry=%p id=%d legacyName=%s result=%p name=%s registered=%d budget=%d\n",
			reinterpret_cast<void*>(registry),
			id,
			legacyName ? legacyName : "",
			reinterpret_cast<void*>(result),
			R1ONetMessageNameFromVtable(result),
			registeredCount,
			s_R1ONetMessageLogBudget - 1);
		OutputDebugStringA(buffer);
		--s_R1ONetMessageLogBudget;
	}

	return result;
}

static char __fastcall R1ONetMessageDecode(__int64 message, __int64 bitBuffer)
{
	LogR1ONetMessageEvent("decode-enter", 0, bitBuffer, message, R1ONetMessageLegacyIdFromVtable(message), -1);
	if (IsR1ODedicatedServer() && message && s_R1OConvertedConnectionlessConnectDepth == 0) {
		const char* name = R1ONetMessageName(message);
		const bool setConVar = name && _stricmp(name, "net_SetConVar") == 0;
		const bool signonState = name && _stricmp(name, "net_SignonState") == 0;
		const bool loadingProgress = name && _stricmp(name, "clc_LoadingProgress") == 0;
		const bool clientInfo = name && _stricmp(name, "clc_ClientInfo") == 0;
		const bool clientMove = name && _stricmp(name, "clc_Move") == 0;
		const int valueBefore = !setConVar && IsReadableRange(reinterpret_cast<void*>(message + 32), sizeof(int))
			? *reinterpret_cast<int*>(message + 32)
			: -1;
		const int spawnCountBefore = signonState && IsReadableRange(reinterpret_cast<void*>(message + 36), sizeof(int))
			? *reinterpret_cast<int*>(message + 36)
			: -1;
		const bool result = setConVar
			? R1ONETSetConVarReadFromBuffer(message, bitBuffer)
			: (clientInfo
				? R1OCLCClientInfoReadFromLegacyBuffer(message, bitBuffer)
				: (clientMove ? R1OCLCMoveReadFromLegacyBuffer(message, bitBuffer) : R1ONetMessageReadPayloadDirect(message, bitBuffer)));
		if (signonState)
			LogR1OSignonStateTrace("decode-direct-leave", message, bitBuffer, result ? 1 : 0);
		if (name && _stricmp(name, "net_StringCmd") == 0 && s_R1OStringCmdContentLogBudget > 0) {
			--s_R1OStringCmdContentLogBudget;
			const char* command = nullptr;
			__try {
				command = IsReadableRange(reinterpret_cast<void*>(message + 32), sizeof(const char*))
					? *reinterpret_cast<const char**>(message + 32)
					: nullptr;
			}
			__except (EXCEPTION_EXECUTE_HANDLER) {
				command = nullptr;
			}
			char stringCmdBuffer[512];
			_snprintf_s(
				stringCmdBuffer,
				sizeof(stringCmdBuffer),
				_TRUNCATE,
				"R1Delta: R1O net_StringCmd direct read result=%d msg=%p command=\"%s\" budget=%d\n",
				static_cast<int>(result),
				reinterpret_cast<void*>(message),
				R1OSafeCString(command, "<unreadable>"),
				s_R1OStringCmdContentLogBudget);
			OutputDebugStringA(stringCmdBuffer);
		}
		if (signonState || loadingProgress || clientInfo || !result) {
			const int valueAfter = IsReadableRange(reinterpret_cast<void*>(message + 32), sizeof(int))
				? *reinterpret_cast<int*>(message + 32)
				: -1;
			const int spawnCountAfter = signonState && IsReadableRange(reinterpret_cast<void*>(message + 36), sizeof(int))
				? *reinterpret_cast<int*>(message + 36)
				: -1;
			char directBuffer[384];
			_snprintf_s(
				directBuffer,
				sizeof(directBuffer),
				_TRUNCATE,
				"R1Delta: R1O %s direct read msg=%p result=%d value=%d->%d spawn=%d->%d bitbuf=%p\n",
				name ? name : "<unknown>",
				reinterpret_cast<void*>(message),
				static_cast<int>(result),
				valueBefore,
				valueAfter,
				spawnCountBefore,
				spawnCountAfter,
				reinterpret_cast<void*>(bitBuffer));
			OutputDebugStringA(directBuffer);
		}
		LogR1ONetMessageEvent("decode-leave", 0, bitBuffer, message, R1ONetMessageLegacyIdFromVtable(message), result ? 1 : 0);
		if (!result)
			LogR1ONetMessageFailure("READ", 0, bitBuffer, message, R1ONetMessageLegacyIdFromVtable(message), 0, "direct legacy payload");
		return result ? 1 : 0;
	}

	unsigned char stringCmdStateBefore[64] = {};
	unsigned char stringCmdStateAfter[64] = {};
	const bool stringCmdDecode = IsR1ODedicatedServer()
		&& message
		&& R1ONetMessageName(message)
		&& _stricmp(R1ONetMessageName(message), "net_StringCmd") == 0
		&& bitBuffer
		&& IsReadableRange(reinterpret_cast<void*>(bitBuffer), sizeof(stringCmdStateBefore));
	const __int64 stringCmdStartBit = stringCmdDecode ? R1OBitBufferCurrentBit(bitBuffer) : -1;
	if (stringCmdDecode)
		memcpy(stringCmdStateBefore, reinterpret_cast<void*>(bitBuffer), sizeof(stringCmdStateBefore));

	const char result = R1ONetMessageDecodeOriginal
		? R1ONetMessageDecodeOriginal(message, bitBuffer)
		: 0;
	if (stringCmdDecode)
		memcpy(stringCmdStateAfter, reinterpret_cast<void*>(bitBuffer), sizeof(stringCmdStateAfter));
	LogR1ONetMessageEvent("decode-leave", 0, bitBuffer, message, R1ONetMessageLegacyIdFromVtable(message), result);
	if (!result) {
		if (stringCmdDecode)
			R1OLogStringCmdReadProbe(message, bitBuffer, stringCmdStateBefore, stringCmdStateAfter, stringCmdStartBit);
		LogR1ONetMessageFailure("READ", 0, bitBuffer, message, R1ONetMessageLegacyIdFromVtable(message), result, "registry decode");
	}
	return result;
}

static char __fastcall R1ONetChanProcessSpecialMessage(__int64 netChan, int id, __int64 bitBuffer)
{
	LogR1ONetMessageEvent("special-enter", netChan, bitBuffer, 0, id, -1);
	if (IsR1ODedicatedServer() && id == 0) {
		LogR1ONetMessageEvent("special-leave", netChan, bitBuffer, 0, id, 1);
		return 1;
	}
	if (IsR1ODedicatedServer() && id == 1 && bitBuffer && R1OBFReadString && R1OBFReadSeek) {
		const int startBit = static_cast<int>(R1OBitBufferCurrentBit(bitBuffer));
		char reason[1024] = {};
		int chars = 0;
		const char readOk = R1OBFReadString(bitBuffer, reason, sizeof(reason), 0, &chars);
		R1OBFReadSeek(reinterpret_cast<void*>(bitBuffer), startBit);
		char buffer[1400];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O received net_Disconnect reason readOk=%d chars=%d bit=%d reason=\"%s\"\n",
			static_cast<int>(readOk),
			chars,
			startBit,
			reason);
		OutputDebugStringA(buffer);
		const int playerSlot = R1OPersistencePlayerSlotFromNetChannel(netChan);
		if (playerSlot >= 0)
			R1OClearPersistentUserDataForPlayer(playerSlot);
	}
	const char result = R1ONetChanProcessSpecialMessageOriginal
		? R1ONetChanProcessSpecialMessageOriginal(netChan, id, bitBuffer)
		: 0;
	LogR1ONetMessageEvent("special-leave", netChan, bitBuffer, 0, id, result);
	if (!result)
		LogR1ONetMessageFailure("SPECIAL", netChan, bitBuffer, 0, id, result);
	return result;
}

static char __fastcall R1ONetChanProcessMessages(__int64 netChan, __int64 bitBuffer)
{
	++s_R1ONetMessageDispatchDepth;
	const __int64 previousBitBuffer = s_R1ONetMessageActiveBitBuffer;
	s_R1ONetMessageActiveBitBuffer = bitBuffer;
	LogR1ONetMessageEvent("dispatch-enter", netChan, bitBuffer, 0, -1, -1);
	const char result = R1ONetChanProcessMessagesOriginal
		? R1ONetChanProcessMessagesOriginal(netChan, bitBuffer)
		: 0;
	char finalResult = result;
	int consumedTrailingNops = 0;
	if (!finalResult && IsR1ODedicatedServer() && !R1OBitBufferIsOverflowed(bitBuffer)) {
		while (R1OBitBufferGetNumBitsLeft(bitBuffer) >= 6 && R1OBitBufferPeekUBitLong(bitBuffer, 6) == 0) {
			R1OBitBufferReadUBitLong(bitBuffer, 6);
			++consumedTrailingNops;
		}
	}
	const int trailingBits = R1OBitBufferGetNumBitsLeft(bitBuffer);
	const bool trailingNopBits = trailingBits >= 0
		&& trailingBits < 6
		&& R1OBitBufferPeekUBitLong(bitBuffer, trailingBits) == 0;
	if (!finalResult && IsR1ODedicatedServer() && !R1OBitBufferIsOverflowed(bitBuffer) && trailingNopBits) {
		finalResult = 1;
		static int trailingPaddingLogBudget = 0;
		if (trailingPaddingLogBudget > 0) {
			--trailingPaddingLogBudget;
			char buffer[256];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: R1O accepted trailing net_NOPs count=%d residualBits=%d netchan=%p bitbuf=%p budget=%d\n",
				consumedTrailingNops,
				trailingBits,
				reinterpret_cast<void*>(netChan),
				reinterpret_cast<void*>(bitBuffer),
				trailingPaddingLogBudget);
			OutputDebugStringA(buffer);
		}
	}
	LogR1ONetMessageEvent("dispatch-leave", netChan, bitBuffer, 0, -1, finalResult);
	if (!finalResult) {
		const int nextId = R1OBitBufferGetNumBitsLeft(bitBuffer) >= 6
			? static_cast<int>(R1OBitBufferPeekUBitLong(bitBuffer, 6))
			: -1;
		char extra[160];
		_snprintf_s(
			extra,
			sizeof(extra),
			_TRUNCATE,
			"nextId=%d nextLegacyName=%s bitsLeft=%d",
			nextId,
			nextId >= 0 ? R1LegacyNetMessageNameForId(nextId) : "",
			R1OBitBufferGetNumBitsLeft(bitBuffer));
		LogR1ONetMessageFailure("DISPATCH", netChan, bitBuffer, 0, -1, finalResult, extra);
	}
	s_R1ONetMessageActiveBitBuffer = previousBitBuffer;
	--s_R1ONetMessageDispatchDepth;
	return finalResult;
}

static __int64 __fastcall R1ONetChanProcessPacket(__int64 netChan, char* packet, __int64 allowConnectionless)
{
	const bool shouldLog = IsR1ODedicatedServer() && s_R1ONetChanProcessPacketLogBudget > 0;
	if (shouldLog) {
		--s_R1ONetChanProcessPacketLogBudget;
		LogR1OPacketSummary("CNetChan::ProcessPacket enter", reinterpret_cast<__int64>(packet), netChan, -1, 1);
	}

	const __int64 result = R1ONetChanProcessPacketOriginal
		? R1ONetChanProcessPacketOriginal(netChan, packet, allowConnectionless)
		: 0;

	if (shouldLog) {
		char buffer[256];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O CNetChan::ProcessPacket result=%lld netchan=%p packet=%p allowConnectionless=%lld budget=%d\n",
			static_cast<long long>(result),
			reinterpret_cast<void*>(netChan),
			packet,
			static_cast<long long>(allowConnectionless),
			s_R1ONetChanProcessPacketLogBudget);
		OutputDebugStringA(buffer);
		LogR1OPacketSummary("CNetChan::ProcessPacket leave", reinterpret_cast<__int64>(packet), netChan, -1, 1);
		LogR1OClientSlotSnapshot("after CNetChan::ProcessPacket", true);
	}

	return result;
}

static bool __fastcall R1ONetChanSendNetMsg(__int64 netChan, __int64 message, bool forceReliable, bool voice)
{
	const bool shouldLog = IsR1ODedicatedServer() && s_R1ONetChanSendNetMsgLogBudget > 0;
	const char* messageName = R1ONetMessageName(message);
	int originalSignonState = -1;
	int translatedSignonState = -1;
	R1OLegacySignonStateFixup signonFixup;
	const bool translateSignonState = IsR1ODedicatedServer()
		&& R1OTranslateSignonStateForLegacyClient(message, &originalSignonState, &translatedSignonState);
	if (translateSignonState)
		R1OPrepareSignonStateForLegacyClient(message, &signonFixup);

	if (shouldLog) {
		--s_R1ONetChanSendNetMsgLogBudget;
		char buffer[512];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O CNetChan::SendNetMsg enter netchan=%p msg=%p name=%s vtableRva=0x%llX id=%d originalId=%d signon=%d->%d forceReliable=%d voice=%d budget=%d\n",
			reinterpret_cast<void*>(netChan),
			reinterpret_cast<void*>(message),
			messageName,
			static_cast<unsigned long long>(R1ONetMessageVtableRva(message)),
			R1ONetMessageLegacyIdFromVtable(message),
			R1ONetMessageOriginalType(message),
			originalSignonState,
			translatedSignonState,
			static_cast<int>(forceReliable),
			static_cast<int>(voice),
			s_R1ONetChanSendNetMsgLogBudget);
		OutputDebugStringA(buffer);
	}

	const __int64 previousSendNetMsgMessage = s_R1ONetChanSendNetMsgMessage;
	++s_R1ONetChanSendNetMsgDepth;
	s_R1ONetChanSendNetMsgMessage = message;
	const bool result = R1ONetChanSendNetMsgOriginal
		? R1ONetChanSendNetMsgOriginal(netChan, message, forceReliable, voice)
		: false;
	s_R1ONetChanSendNetMsgMessage = previousSendNetMsgMessage;
	--s_R1ONetChanSendNetMsgDepth;
	if (translateSignonState)
		R1ORestoreSignonStateForLegacyClient(message, signonFixup);
	if (translateSignonState && translatedSignonState != originalSignonState)
		*reinterpret_cast<int*>(message + 32) = originalSignonState;

	if (shouldLog) {
		char buffer[384];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O CNetChan::SendNetMsg leave netchan=%p msg=%p name=%s result=%d budget=%d\n",
			reinterpret_cast<void*>(netChan),
			reinterpret_cast<void*>(message),
			R1ONetMessageNameFromVtable(message),
			static_cast<int>(result),
			s_R1ONetChanSendNetMsgLogBudget);
		OutputDebugStringA(buffer);
	}

	if (!result) {
		char extra[160];
		_snprintf_s(
			extra,
			sizeof(extra),
			_TRUNCATE,
			"forceReliable=%d voice=%d original=%p",
			static_cast<int>(forceReliable),
			static_cast<int>(voice),
			reinterpret_cast<void*>(R1ONetChanSendNetMsgOriginal));
		LogR1ONetMessageFailure("SEND", netChan, 0, message, R1ONetMessageLegacyIdFromVtable(message), result, extra);
	}

	return result;
}

static void LogR1OSendDataMessageIds(__int64 owner, bf_write* bitBuffer, const char* phase, __int64 result = -1)
{
	if (!IsR1ODedicatedServer() || s_R1ONetChanSendDataLogBudget <= 0)
		return;

	if (!bitBuffer || !IsReadableRange(bitBuffer, sizeof(*bitBuffer))) {
		--s_R1ONetChanSendDataLogBudget;
		char buffer[256];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O CNetChan::SendData %s owner=%p bitbuf=%p invalid result=%lld budget=%d\n",
			phase ? phase : "event",
			reinterpret_cast<void*>(owner),
			bitBuffer,
			static_cast<long long>(result),
			s_R1ONetChanSendDataLogBudget);
		OutputDebugStringA(buffer);
		return;
	}

	const int bitCount = bitBuffer->GetNumBitsWritten();
	const int byteCount = bitBuffer->GetNumBytesWritten();
	const unsigned char* data = bitBuffer->GetData();
	const int readableCheckBytes = byteCount < 64 ? byteCount : 64;
	if (bitCount < 0 || byteCount < 0 || !data || !IsReadableRange(data, readableCheckBytes)) {
		--s_R1ONetChanSendDataLogBudget;
		char buffer[256];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O CNetChan::SendData %s owner=%p bitbuf=%p data=%p bits=%d bytes=%d overflow=%d unreadable result=%lld budget=%d\n",
			phase ? phase : "event",
			reinterpret_cast<void*>(owner),
			bitBuffer,
			data,
			bitCount,
			byteCount,
			static_cast<int>(bitBuffer->IsOverflowed()),
			static_cast<long long>(result),
			s_R1ONetChanSendDataLogBudget);
		OutputDebugStringA(buffer);
		return;
	}

	old_bf_read reader(data, byteCount, bitCount);
	char ids[512] = {};
	size_t offset = 0;
	for (int i = 0; i < 24 && reader.GetNumBitsLeft() >= 6; ++i) {
		const int id = static_cast<int>(reader.ReadUBitLong(6));
		const char* name = R1OLegacyServerMessageNameForId(id);
		char item[48];
		_snprintf_s(
			item,
			sizeof(item),
			_TRUNCATE,
			"%s%d:%s",
			i ? "," : "",
			id,
			name ? name : "?");
		const size_t itemLength = strlen(item);
		if (offset + itemLength + 1 >= sizeof(ids))
			break;
		memcpy(ids + offset, item, itemLength);
		offset += itemLength;
	}

	char bytes[256] = {};
	size_t byteOffset = 0;
	const int dumpBytes = byteCount < 24 ? byteCount : 24;
	for (int i = 0; i < dumpBytes; ++i) {
		char item[8];
		_snprintf_s(item, sizeof(item), _TRUNCATE, "%s%02X", i ? " " : "", data[i]);
		const size_t itemLength = strlen(item);
		if (byteOffset + itemLength + 1 >= sizeof(bytes))
			break;
		memcpy(bytes + byteOffset, item, itemLength);
		byteOffset += itemLength;
	}

	--s_R1ONetChanSendDataLogBudget;
	char buffer[1024];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O CNetChan::SendData %s owner=%p bitbuf=%p data=%p bits=%d bytes=%d overflow=%d ids=[%s] firstBytes=[%s] result=%lld budget=%d\n",
		phase ? phase : "event",
		reinterpret_cast<void*>(owner),
		bitBuffer,
		data,
		bitCount,
		byteCount,
		static_cast<int>(bitBuffer->IsOverflowed()),
		ids,
		bytes,
		static_cast<long long>(result),
		s_R1ONetChanSendDataLogBudget);
	OutputDebugStringA(buffer);
}

static __int64 __fastcall R1ONetChanSendData(__int64 netChanOrClient, bf_write* bitBuffer)
{
	LogR1OSendDataMessageIds(netChanOrClient, bitBuffer, "enter");
	const __int64 result = R1ONetChanSendDataOriginal
		? R1ONetChanSendDataOriginal(netChanOrClient, bitBuffer)
		: 0;
	LogR1OSendDataMessageIds(netChanOrClient, bitBuffer, "leave", result);
	return result;
}

static void EnsureR1ONetChanSendNetMsgHook(__int64 netChan)
{
	if (!IsR1ODedicatedServer() || s_R1ONetChanSendNetMsgHooked || !netChan)
		return;

	if (!IsReadableRange(reinterpret_cast<void*>(netChan), sizeof(void*)))
		return;

	uintptr_t* vtable = *reinterpret_cast<uintptr_t**>(netChan);
	if (!vtable || !IsReadableRange(vtable, sizeof(uintptr_t) * 42))
		return;

	void* target = reinterpret_cast<void*>(vtable[41]);
	const MH_STATUS status = MH_CreateHook(
		target,
		&R1ONetChanSendNetMsg,
		reinterpret_cast<LPVOID*>(&R1ONetChanSendNetMsgOriginal));
	const MH_STATUS enableStatus = (status == MH_OK || status == MH_ERROR_ALREADY_CREATED)
		? MH_EnableHook(target)
		: status;
	s_R1ONetChanSendNetMsgHooked = status == MH_OK || status == MH_ERROR_ALREADY_CREATED;

	char buffer[384];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O CNetChan::SendNetMsg hook status=%d enable=%d netchan=%p vtable=%p target=%p original=%p\n",
		static_cast<int>(status),
		static_cast<int>(enableStatus),
		reinterpret_cast<void*>(netChan),
		vtable,
		target,
		reinterpret_cast<void*>(R1ONetChanSendNetMsgOriginal));
	OutputDebugStringA(buffer);
}

static void EnsureR1ONetChanProcessPacketHook(__int64 netChan)
{
	if (!IsR1ODedicatedServer() || s_R1ONetChanProcessPacketHooked || !netChan)
		return;

	if (!IsReadableRange(reinterpret_cast<void*>(netChan), sizeof(void*)))
		return;

	uintptr_t* vtable = *reinterpret_cast<uintptr_t**>(netChan);
	if (!vtable || !IsReadableRange(vtable, sizeof(uintptr_t) * 40))
		return;

	void* target = reinterpret_cast<void*>(vtable[39]);
	const MH_STATUS status = MH_CreateHook(
		target,
		&R1ONetChanProcessPacket,
		reinterpret_cast<LPVOID*>(&R1ONetChanProcessPacketOriginal));
	const MH_STATUS enableStatus = (status == MH_OK || status == MH_ERROR_ALREADY_CREATED)
		? MH_EnableHook(target)
		: status;
	s_R1ONetChanProcessPacketHooked = status == MH_OK || status == MH_ERROR_ALREADY_CREATED;

	char buffer[384];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O CNetChan::ProcessPacket hook status=%d enable=%d netchan=%p vtable=%p target=%p original=%p\n",
		static_cast<int>(status),
		static_cast<int>(enableStatus),
		reinterpret_cast<void*>(netChan),
		vtable,
		target,
		reinterpret_cast<void*>(R1ONetChanProcessPacketOriginal));
	OutputDebugStringA(buffer);
}

static __int64 __fastcall R1ONetChanLookup(int socketIndex, int* packet)
{
	const __int64 result = R1ONetChanLookupOriginal
		? R1ONetChanLookupOriginal(socketIndex, packet)
		: 0;

	if (IsR1ODedicatedServer()) {
		const __int64 packetAddress = reinterpret_cast<__int64>(packet);
		const int length = packet && IsReadableRange(reinterpret_cast<void*>(packetAddress + 112), sizeof(int))
			? *reinterpret_cast<int*>(packetAddress + 112)
			: 0;
		static int logBudget = 0;
		if (logBudget > 0 && (length >= 1024 || result == 0)) {
			--logBudget;
			LogR1OPacketSummary("netchan lookup", packetAddress, result, socketIndex, result ? 1 : 0);
		}

		if (result)
		{
			EnsureR1ONetChanProcessPacketHook(result);
			EnsureR1ONetChanSendNetMsgHook(result);
		}
	}

	return result;
}

static char* ForceR1ODediUdpReceivePacket(int socketIndex, char encrypted, const char* source)
{
	const int packetCount = R1OHookGlobalValue<int>(0x2A75F50, 0);
	void* packetTable = R1OHookGlobalValue<void*>(0x2A75F38, nullptr);
	if (socketIndex < 0 || socketIndex >= packetCount || !packetTable)
		return nullptr;

	void* socketTable = R1OHookGlobalValue<void*>(0x2A73EB8, nullptr);
	unsigned char* socketEntry = socketTable && socketIndex >= 0
		? reinterpret_cast<unsigned char*>(socketTable) + 16LL * socketIndex
		: nullptr;
	if (!socketEntry || !IsReadableRange(socketEntry, 16))
		return nullptr;

	const int udpSocket = *reinterpret_cast<int*>(socketEntry + 8);
	if (!udpSocket)
		return nullptr;

	char* packet = reinterpret_cast<char*>(packetTable) + 136LL * socketIndex;
	if (!IsReadableRange(packet, 136))
		return nullptr;

	const char received = R1ONetReceivePacketOriginal
		? R1ONetReceivePacketOriginal(static_cast<unsigned int>(socketIndex), reinterpret_cast<__int64>(packet), encrypted)
		: 0;
	if (!received)
		return nullptr;

	const __int64 packetData = *reinterpret_cast<__int64*>(packet + 0x28);
	const int packetLength = *reinterpret_cast<int*>(packet + 0x70);
	*reinterpret_cast<__int64*>(packet + 0x68) = packetData;
	*reinterpret_cast<__int64*>(packet + 0x58) = packetData;
	*reinterpret_cast<__int64*>(packet + 0x48) = packetLength;
	*reinterpret_cast<__int64*>(packet + 0x40) = 8LL * packetLength;
	*reinterpret_cast<__int64*>(packet + 0x60) = packetData + packetLength;
	packet[0x38] = 0;
	if (packetData && R1OBFReadSeek)
		R1OBFReadSeek(packet + 0x30, 0);

	char buffer[512];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O %s forced UDP receive packet=%p socket=%d udp=%d length=%d data=%p encrypted=%d wsa=%d\n",
		source ? source : "NET_GetPacket",
		packet,
		socketIndex,
		udpSocket,
		packetLength,
		reinterpret_cast<void*>(packetData),
		static_cast<int>(encrypted),
		R1OHookGlobalValue<int>(0x22FB5A4, 0));
	OutputDebugStringA(buffer);
	return packet;
}

static void ResetR1OPacketMessage(__int64 packet, char* messageData, int messageLength)
{
	if (!packet || !messageData || messageLength <= 0)
		return;

	*reinterpret_cast<__int64*>(packet + 0x68) = reinterpret_cast<__int64>(messageData);
	*reinterpret_cast<__int64*>(packet + 0x58) = reinterpret_cast<__int64>(messageData);
	*reinterpret_cast<__int64*>(packet + 0x48) = messageLength;
	*reinterpret_cast<__int64*>(packet + 0x40) = 8LL * messageLength;
	*reinterpret_cast<__int64*>(packet + 0x60) = reinterpret_cast<__int64>(messageData + messageLength);
	*reinterpret_cast<unsigned char*>(packet + 0x38) = 0;
	if (R1OBFReadSeek)
		R1OBFReadSeek(reinterpret_cast<void*>(packet + 0x30), 0);

	static int logBudget = 0;
	if (logBudget > 0) {
		--logBudget;
		char buffer[768];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O packet message reset packet=%p data=%p len=%d field30=%p overflow=%u maxBits=%lld bytes=%lld cache=%08X bits=%d cur=%p end=%p base=%p rawLen=%d first=%02X %02X %02X %02X %02X %02X %02X %02X\n",
			reinterpret_cast<void*>(packet),
			reinterpret_cast<void*>(messageData),
			messageLength,
			reinterpret_cast<void*>(*reinterpret_cast<__int64*>(packet + 0x30)),
			static_cast<unsigned int>(*reinterpret_cast<unsigned char*>(packet + 0x38)),
			*reinterpret_cast<__int64*>(packet + 0x40),
			*reinterpret_cast<__int64*>(packet + 0x48),
			*reinterpret_cast<unsigned int*>(packet + 0x50),
			*reinterpret_cast<int*>(packet + 0x54),
			reinterpret_cast<void*>(*reinterpret_cast<__int64*>(packet + 0x58)),
			reinterpret_cast<void*>(*reinterpret_cast<__int64*>(packet + 0x60)),
			reinterpret_cast<void*>(*reinterpret_cast<__int64*>(packet + 0x68)),
			*reinterpret_cast<int*>(packet + 0x70),
			static_cast<unsigned char>(messageData[0]),
			static_cast<unsigned char>(messageData[1]),
			static_cast<unsigned char>(messageData[2]),
			static_cast<unsigned char>(messageData[3]),
			static_cast<unsigned char>(messageData[4]),
			static_cast<unsigned char>(messageData[5]),
			static_cast<unsigned char>(messageData[6]),
			static_cast<unsigned char>(messageData[7]));
		OutputDebugStringA(buffer);
	}
}

static bool CopyConnectStringForR1O(bf_read& reader, bf_write& writer, char* scratch, size_t scratchSize)
{
	if (!scratch || scratchSize == 0)
		return false;

	scratch[0] = 0;
	if (!reader.ReadString(scratch, static_cast<int>(scratchSize)))
		return false;

	writer.WriteString(scratch);
	return !writer.IsOverflowed();
}

static bool ConvertR1DConnectPacketToR1O(char* data, int length, int capacity, int* outLength, int* outChallenge)
{
	if (!data || length < 9 || capacity <= length || !outLength)
		return false;
	if (*reinterpret_cast<unsigned int*>(data) != 0xFFFFFFFFu || data[4] != 'A')
		return false;

	char converted[1240] = {};
	if (capacity > static_cast<int>(sizeof(converted)))
		capacity = static_cast<int>(sizeof(converted));

	*reinterpret_cast<unsigned int*>(converted) = 0xFFFFFFFFu;
	bf_read reader(data + 4, length - 4);
	bf_write writer(converted + 4, capacity - 4);

	const int type = reader.ReadByte();
	if (type != 'A')
		return false;
	writer.WriteByte('A');

	const int version = reader.ReadLong();
	if (version != 1040)
		return false;
	writer.WriteLong(1040);

	writer.WriteLong(reader.ReadLong()); // hostVersion
	const int challenge = reader.ReadLong();
	writer.WriteLong(challenge);
	writer.WriteLong(reader.ReadLong()); // unknown
	writer.WriteLong(reader.ReadLong()); // unknown1
	const long long platformUserId = reader.ReadLongLong();
	writer.WriteLongLong(platformUserId);

	char temp[256];
	if (!CopyConnectStringForR1O(reader, writer, temp, sizeof(temp)) ||
		!CopyConnectStringForR1O(reader, writer, temp, sizeof(temp))) {
		return false;
	}

	temp[0] = 0;
	if (!reader.ReadString(temp, sizeof(temp)))
		return false;
	bool numericString = temp[0] != 0;
	for (const char* p = temp; numericString && *p; ++p)
		numericString = (*p >= '0' && *p <= '9');
	if (!numericString)
		_snprintf_s(temp, sizeof(temp), _TRUNCATE, "%llu", static_cast<unsigned long long>(platformUserId));
	writer.WriteString(temp);
	writer.WriteString("");

	char platformUserIdString[32];
	_snprintf_s(
		platformUserIdString,
		sizeof(platformUserIdString),
		_TRUNCATE,
		"%llu",
		static_cast<unsigned long long>(platformUserId));

	const int unknownCount = reader.ReadByte();
	if (unknownCount < 0 || unknownCount > 32)
		return false;
	writer.WriteByte(static_cast<unsigned int>(unknownCount));
	for (int i = 0; i < unknownCount; ++i)
		writer.WriteLongLong(reader.ReadLongLong());

	if (!CopyConnectStringForR1O(reader, writer, temp, sizeof(temp)))
		return false;

	writer.WriteLong(reader.ReadLong()); // playlistVersionNumber
	writer.WriteLong(reader.ReadLong()); // persistenceVersionNumber
	reader.ReadLongLong(); // R1 2015 persistenceHash
	// TFO/R1O reads a 32-bit persistence/hash sentinel here and rejects the
	// connect if it is not the delta VPK magic used by the shipped TFO data.
	writer.WriteLong(0x12345);

	const int numberOfPlayers = reader.ReadByte();
	if (numberOfPlayers < 0 || numberOfPlayers > 32)
		return false;
	writer.WriteByte(static_cast<unsigned int>(numberOfPlayers));
	static int splitKvRewriteLogBudget = 0;
	for (int i = 0; i < numberOfPlayers; ++i) {
		const unsigned int msgType = reader.ReadUBitLong(6);
		const int kvCount = reader.ReadByte();
		if (kvCount < 0 || kvCount > 64)
			return false;

		// R1O assigns CLC_SplitPlayerConnect a different netmessage id than R1
		// 2015 and wraps the inherited convar block with two TFO-only fields.
		// sub_180206260 reads a string, then CLC_SplitPlayerConnect::ReadFromBuffer,
		// then 31 bits of extra state.
		writer.WriteUBitLong(49, 6);
		writer.WriteString("");
		writer.WriteByte(static_cast<unsigned int>(kvCount));
		for (int j = 0; j < kvCount; ++j) {
			char key[256] = {};
			char value[256] = {};
			if (!reader.ReadString(key, sizeof(key)) || !reader.ReadString(value, sizeof(value))) {
				return false;
			}
			const bool isPlatformUserId = _stricmp(key, "platform_user_id") == 0;
			const bool replacedPlatformUserId = isPlatformUserId && value[0] == 0;
			if (replacedPlatformUserId)
				strncpy_s(value, sizeof(value), platformUserIdString, _TRUNCATE);
			if (splitKvRewriteLogBudget > 0 && (isPlatformUserId || strstr(key, "platform") || strstr(key, "user"))) {
				--splitKvRewriteLogBudget;
				char buffer[512];
				_snprintf_s(
					buffer,
					sizeof(buffer),
					_TRUNCATE,
					"R1Delta: R1O split connect kv player=%d index=%d key=%s value=%s replaced=%d fallback=%s\n",
					i,
					j,
					key,
					value,
					static_cast<int>(replacedPlatformUserId),
					platformUserIdString);
				OutputDebugStringA(buffer);
			}
			writer.WriteString(key);
			writer.WriteString(value);
		}
		writer.WriteUBitLong(0, 31);
	}

	const int lowViolence = reader.ReadByte();
	writer.WriteOneBit(lowViolence ? 1 : 0);
	writer.WriteByte(1); // cross-play platform id: PC

	if (reader.IsOverflowed() || writer.IsOverflowed())
		return false;

	const int convertedLength = 4 + writer.GetNumBytesWritten();
	if (convertedLength <= 4 || convertedLength > capacity)
		return false;

	memcpy(data, converted, convertedLength);
	*outLength = convertedLength;
	if (outChallenge)
		*outChallenge = challenge;

	static int verifyLogBudget = 0;
	if (verifyLogBudget > 0) {
		--verifyLogBudget;
		bf_read verify(converted + 4, convertedLength - 4);
		char vName[256] = {};
		char vPassword[256] = {};
		char vIdString[256] = {};
		char vExtra[64] = {};
		char vServerFilter[256] = {};
		const int vType = verify.ReadByte();
		const int vVersion = verify.ReadLong();
		const int vHost = verify.ReadLong();
		const int vChallenge = verify.ReadLong();
		const int vUnknown0 = verify.ReadLong();
		const int vUnknown1 = verify.ReadLong();
		const long long vPlatform = verify.ReadLongLong();
		verify.ReadString(vName, sizeof(vName));
		verify.ReadString(vPassword, sizeof(vPassword));
		verify.ReadString(vIdString, sizeof(vIdString));
		verify.ReadString(vExtra, sizeof(vExtra));
		const int vUnknownCount = verify.ReadByte();
		for (int i = 0; i < vUnknownCount && i < 32; ++i)
			verify.ReadLongLong();
		verify.ReadString(vServerFilter, sizeof(vServerFilter));
		const int vPlaylist = verify.ReadLong();
		const int vPersistenceVersion = verify.ReadLong();
		const int vPersistenceHash = verify.ReadLong();
		const int vPlayers = verify.ReadByte();
		unsigned int vFirstSplitType = 0;
		int vFirstSplitKvCount = 0;
		char vFirstSplitExtra[256] = {};
		unsigned int vFirstSplitTail = 0;
		char vSplitPlatformUserId[256] = {};
		for (int i = 0; i < vPlayers && i < 32; ++i) {
			const unsigned int splitType = verify.ReadUBitLong(6);
			char vSplitExtra[256] = {};
			verify.ReadString(vSplitExtra, sizeof(vSplitExtra));
			const int kvCount = verify.ReadByte();
			if (i == 0) {
				vFirstSplitType = splitType;
				vFirstSplitKvCount = kvCount;
				strncpy_s(vFirstSplitExtra, sizeof(vFirstSplitExtra), vSplitExtra, _TRUNCATE);
			}
			for (int j = 0; j < kvCount && j < 64; ++j) {
				char vKey[256] = {};
				char vValue[256] = {};
				verify.ReadString(vKey, sizeof(vKey));
				verify.ReadString(vValue, sizeof(vValue));
				if (i == 0 && _stricmp(vKey, "platform_user_id") == 0)
					strncpy_s(vSplitPlatformUserId, sizeof(vSplitPlatformUserId), vValue, _TRUNCATE);
			}
			const unsigned int splitTail = verify.ReadUBitLong(31);
			if (i == 0)
				vFirstSplitTail = splitTail;
		}
		const int vLowViolence = verify.ReadOneBit();
		const int vCrossplay = verify.ReadByte();
		char buffer[768];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O converted C2S_CONNECT verify len=%d type=%d version=%d host=%d challenge=%d unk0=%d unk1=%d platform=%llu id=%s extra=%s unknownCount=%d filter=%s playlist=%d persistenceVersion=%d persistenceHash=%X players=%d splitType=%u splitExtra=%s splitKv=%d splitPlatformUserId=%s splitTail=%u lowViolence=%d crossplay=%d overflow=%d bitsRead=%d bitsLeft=%d\n",
			convertedLength,
			vType,
			vVersion,
			vHost,
			vChallenge,
			vUnknown0,
			vUnknown1,
			static_cast<unsigned long long>(vPlatform),
			vIdString,
			vExtra,
			vUnknownCount,
			vServerFilter,
			vPlaylist,
			vPersistenceVersion,
			vPersistenceHash,
			vPlayers,
			vFirstSplitType,
			vFirstSplitExtra,
			vFirstSplitKvCount,
			vSplitPlatformUserId,
			vFirstSplitTail,
			vLowViolence,
			vCrossplay,
			static_cast<int>(verify.IsOverflowed()),
			verify.GetNumBitsRead(),
			verify.GetNumBitsLeft());
		OutputDebugStringA(buffer);
	}
	return true;
}

static __int64 __fastcall R1OReplyChallenge(__int64 server, int socketIndex, __int64 address, __int64 bitBuffer, char useCompression)
{
	if (!IsR1ODedicatedServer() || !server || !address || !bitBuffer || !R1OBFReadString || !R1ONetSendPacket) {
		return R1OReplyChallengeOriginal
			? R1OReplyChallengeOriginal(server, socketIndex, address, bitBuffer, useCompression)
			: 0;
	}

	char context[256] = {};
	int charsRead = 0;
	const char readOk = R1OBFReadString(bitBuffer, context, sizeof(context), 0, &charsRead);
	if (!readOk) {
		char buffer[384];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O legacy challenge response failed to read context server=%p socket=%d address=%p bitbuf=%p\n",
			reinterpret_cast<void*>(server),
			socketIndex,
			reinterpret_cast<void*>(address),
			reinterpret_cast<void*>(bitBuffer));
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
		return 1;
	}

	R1OGetChallengeNrType getChallengeNr = nullptr;
	R1OGetPasswordType getPassword = nullptr;
	if (IsReadableRange(reinterpret_cast<void*>(server), sizeof(void*))) {
		auto vtable = *reinterpret_cast<uintptr_t**>(server);
		if (vtable && IsReadableRange(vtable, sizeof(uintptr_t) * 60)) {
			getChallengeNr = reinterpret_cast<R1OGetChallengeNrType>(vtable[53]);
			getPassword = reinterpret_cast<R1OGetPasswordType>(vtable[29]);
		}
	}

	const int challenge = getChallengeNr ? getChallengeNr(server, address) : 0;
	const int hostVersion = R1OHookGlobalValue<int>(0x2E08CD8, 0);
	const bool requiresPassword = getPassword && getPassword(server) && getPassword(server)[0];

	char response[512] = {};
	bf_write writer(response, sizeof(response));
	writer.WriteLong(-1);
	writer.WriteByte('I');
	writer.WriteLong(challenge);
	writer.WriteString(context);
	writer.WriteLong(hostVersion);
	writer.WriteString("");
	writer.WriteByte(requiresPassword ? 1 : 0);
	writer.WriteLongLong(-1);

	const int bytes = writer.GetNumBytesWritten();
	static int logBudget = 0;
	if (logBudget > 0) {
		--logBudget;
		char buffer[768];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O legacy challenge response socket=%d address=%p context=%s readOk=%d chars=%d challenge=%d hostVersion=%d password=%d bytes=%d overflow=%d budget=%d\n",
			socketIndex,
			reinterpret_cast<void*>(address),
			context,
			static_cast<int>(readOk),
			charsRead,
			challenge,
			hostVersion,
			static_cast<int>(requiresPassword),
			bytes,
			static_cast<int>(writer.IsOverflowed()),
			logBudget);
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}

	if (writer.IsOverflowed() || bytes <= 0)
		return 1;

	const __int64 sendResult = R1ONetSendPacket(0, static_cast<unsigned int>(socketIndex), address, response, static_cast<unsigned int>(bytes), 0, 0, 0, 0, 0, useCompression);
	if (logBudget >= 0) {
		char buffer[384];
		const unsigned char* addressBytes = reinterpret_cast<const unsigned char*>(address);
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O legacy challenge send result=%lld socket=%d address=%p bytes=%d wsa=%d netReady=%d addrType=%d addrBytes=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
			static_cast<long long>(sendResult),
			socketIndex,
			reinterpret_cast<void*>(address),
			bytes,
			R1OHookGlobalValue<int>(0x22FB5A4, 0),
			static_cast<int>(R1OHookGlobalValue<unsigned char>(0x22FB57A, static_cast<unsigned char>(0))),
			address ? *reinterpret_cast<const int*>(address) : -1,
			addressBytes ? addressBytes[0] : 0,
			addressBytes ? addressBytes[1] : 0,
			addressBytes ? addressBytes[2] : 0,
			addressBytes ? addressBytes[3] : 0,
			addressBytes ? addressBytes[4] : 0,
			addressBytes ? addressBytes[5] : 0,
			addressBytes ? addressBytes[6] : 0,
			addressBytes ? addressBytes[7] : 0,
			addressBytes ? addressBytes[8] : 0,
			addressBytes ? addressBytes[9] : 0,
			addressBytes ? addressBytes[10] : 0,
			addressBytes ? addressBytes[11] : 0,
			addressBytes ? addressBytes[12] : 0,
			addressBytes ? addressBytes[13] : 0,
			addressBytes ? addressBytes[14] : 0,
			addressBytes ? addressBytes[15] : 0,
			addressBytes ? addressBytes[16] : 0,
			addressBytes ? addressBytes[17] : 0,
			addressBytes ? addressBytes[18] : 0,
			addressBytes ? addressBytes[19] : 0,
			addressBytes ? addressBytes[20] : 0,
			addressBytes ? addressBytes[21] : 0,
			addressBytes ? addressBytes[22] : 0,
			addressBytes ? addressBytes[23] : 0);
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}
	return sendResult;
}

static char __fastcall R1OProcessConnectionlessPacket(void* thisptr, __int64 packet)
{
	bool wasConnectPacket = false;
	bool convertedConnectPacket = false;
	int originalLength = 0;
	int finalLength = 0;
	int connectChallenge = 0;

	if (IsR1ODedicatedServer() && packet) {
		char* data = IsReadableRange(reinterpret_cast<void*>(packet + 0x28), sizeof(char*))
			? *reinterpret_cast<char**>(packet + 0x28)
			: nullptr;
		int length = IsReadableRange(reinterpret_cast<void*>(packet + 0x70), sizeof(int))
			? *reinterpret_cast<int*>(packet + 0x70)
			: 0;
		if (data && length >= 5 && IsReadableRange(data, static_cast<size_t>(length)) && data[4] == 'A') {
			wasConnectPacket = true;
			originalLength = length;
			int convertedLength = 0;
			int challenge = 0;
			const bool converted = ConvertR1DConnectPacketToR1O(data, length, 1240, &convertedLength, &challenge);
			convertedConnectPacket = converted;
			connectChallenge = challenge;
			finalLength = converted ? convertedLength : length;
			if (converted) {
				*reinterpret_cast<int*>(packet + 0x70) = convertedLength;
				ResetR1OPacketMessage(packet, data + 4, convertedLength - 4);
			}

			static int logBudget = 0;
			if (logBudget > 0) {
				--logBudget;
				char buffer[512];
				_snprintf_s(
					buffer,
					sizeof(buffer),
					_TRUNCATE,
					"R1Delta: R1O ProcessConnectionlessPacket connect this=%p packet=%p converted=%d length=%d->%d challenge=%d protocol=%d host=%d first=%02X %02X %02X %02X %02X\n",
					thisptr,
					reinterpret_cast<void*>(packet),
					static_cast<int>(converted),
					length,
					converted ? convertedLength : length,
					challenge,
					length >= 9 ? *reinterpret_cast<int*>(data + 5) : -1,
					length >= 13 ? *reinterpret_cast<int*>(data + 9) : -1,
					static_cast<unsigned char>(data[0]),
					static_cast<unsigned char>(data[1]),
					static_cast<unsigned char>(data[2]),
					static_cast<unsigned char>(data[3]),
					static_cast<unsigned char>(data[4]));
				OutputDebugStringA(buffer);
			}
		}
	}

	if (convertedConnectPacket)
		++s_R1OConvertedConnectionlessConnectDepth;
	const char result = R1OProcessConnectionlessPacketOriginal
		? R1OProcessConnectionlessPacketOriginal(thisptr, packet)
		: 0;
	if (convertedConnectPacket)
		--s_R1OConvertedConnectionlessConnectDepth;
	if (IsR1ODedicatedServer()) {
		if (wasConnectPacket) {
			char buffer[384];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: R1O ProcessConnectionlessPacket connect result=%d converted=%d length=%d->%d challenge=%d this=%p packet=%p\n",
				static_cast<int>(result),
				static_cast<int>(convertedConnectPacket),
				originalLength,
				finalLength,
				connectChallenge,
				thisptr,
				reinterpret_cast<void*>(packet));
			OutputDebugStringA(buffer);
		}
		LogR1OClientSlotSnapshot("after connectionless packet", wasConnectPacket);
	}
	return result;
}

static bool __fastcall R1OStringTableContains(__int64 thisptr, const char* name)
{
	if (IsR1ODedicatedServer() && !name) {
		static int logBudget = 0;
		if (logBudget > 0) {
			--logBudget;
			OutputDebugStringA("R1Delta: R1O string-table contains received null name; returning false\n");
		}
		return false;
	}

	return R1OStringTableContainsOriginal
		? R1OStringTableContainsOriginal(thisptr, name)
		: false;
}

static char* __fastcall R1OStringTableLookup(const char* name)
{
	if (!IsR1ODedicatedServer()) {
		return R1OStringTableLookupOriginal
			? R1OStringTableLookupOriginal(name)
			: nullptr;
	}

	if (!IsReadableCString(name)) {
		static int logBudget = 0;
		if (logBudget > 0) {
			--logBudget;
			OutputDebugStringA("R1Delta: R1O sound-alias lookup received invalid name; returning null\n");
		}
		return nullptr;
	}

	HMODULE module = engineR1O ? engineR1O : GetModuleHandleA("engine_r1o.dll");
	const uintptr_t base = reinterpret_cast<uintptr_t>(module);
	if (!base)
		return nullptr;

	int* const countPtr = reinterpret_cast<int*>(base + 0x18836F4);
	uintptr_t* const entries = reinterpret_cast<uintptr_t*>(base + 0x1883710);
	if (!IsReadableRange(countPtr, sizeof(*countPtr))) {
		return nullptr;
	}

	const int count = *countPtr;
	if (count <= 0 || count > 65536 || !IsReadableRange(entries, sizeof(uintptr_t) * static_cast<size_t>(count) * 2)) {
		static int logBudget = 0;
		if (logBudget > 0) {
			--logBudget;
			char buffer[256];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: R1O sound-alias lookup has invalid table count=%d entries=%p\n",
				count,
				entries);
			OutputDebugStringA(buffer);
		}
		return nullptr;
	}

	char lowered[64];
	size_t i = 0;
	for (; i + 1 < sizeof(lowered) && name[i]; ++i) {
		lowered[i] = static_cast<char>(tolower(static_cast<unsigned char>(name[i])));
	}
	lowered[i] = '\0';

	int nullEntries = 0;
	for (int index = 0; index < count; ++index) {
		const char* const entryName = reinterpret_cast<const char*>(entries[static_cast<size_t>(index) * 2]);
		if (!IsReadableCString(entryName)) {
			++nullEntries;
			continue;
		}

		if (_stricmp(entryName, lowered) != 0)
			continue;

		const int resultIndex = static_cast<int>(entries[static_cast<size_t>(index) * 2 + 1]);
		if (resultIndex < 0 || resultIndex > 65536)
			return nullptr;

		char* const result = reinterpret_cast<char*>(base + 0x6F9110 + static_cast<uintptr_t>(resultIndex) * 512);
		return IsReadableRange(result, 1) ? result : nullptr;
	}

	static int missLogBudget = 0;
	if (missLogBudget > 0 && nullEntries > 0) {
		--missLogBudget;
		char buffer[256];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O sound-alias lookup missed '%s' count=%d nullEntries=%d\n",
			lowered,
			count,
			nullEntries);
		OutputDebugStringA(buffer);
	}
	return nullptr;
}

static char* __fastcall R1ONetGetPacket(int socketIndex, __int64 data, char encrypted)
{
	char* result = R1ONetGetPacketOriginal
		? R1ONetGetPacketOriginal(socketIndex, data, encrypted)
		: nullptr;

	if (!IsR1ODedicatedServer())
		return result;

	static int enterLogBudget = 0;
	if (enterLogBudget > 0) {
		--enterLogBudget;
		char buffer[512];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O NET_GetPacket original result=%p socket=%d data=%p encrypted=%d mode=%d splits=%lld multiplayer=%d wsa=%d\n",
			result,
			socketIndex,
			reinterpret_cast<void*>(data),
			static_cast<int>(encrypted),
			R1OHookGlobalValue<int>(0x2659558, -1),
			static_cast<long long>(R1OHookGlobalValue<__int64>(0x265971C, -1)),
			static_cast<int>(R1OHookGlobalValue<unsigned char>(0x22FB57A, static_cast<unsigned char>(0))),
			R1OHookGlobalValue<int>(0x22FB5A4, 0));
		OutputDebugStringA(buffer);
	}

	if (result)
		return result;

	return ForceR1ODediUdpReceivePacket(socketIndex, encrypted, "NET_GetPacket");
}

static char* __fastcall R1ONetGetLoopbackPacket(int socketIndex, __int64 data)
{
	const uintptr_t returnAddress = reinterpret_cast<uintptr_t>(_ReturnAddress());
	char* result = R1ONetGetLoopbackPacketOriginal
		? R1ONetGetLoopbackPacketOriginal(socketIndex, data)
		: nullptr;

	if (!IsR1ODedicatedServer())
		return result;

	static int enterLogBudget = 0;
	if (enterLogBudget > 0) {
		--enterLogBudget;
		char buffer[512];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O NET_GetLoopbackPacket original result=%p socket=%d data=%p mode=%d splits=%lld multiplayer=%d wsa=%d\n",
			result,
			socketIndex,
			reinterpret_cast<void*>(data),
			R1OHookGlobalValue<int>(0x2659558, -1),
			static_cast<long long>(R1OHookGlobalValue<__int64>(0x265971C, -1)),
			static_cast<int>(R1OHookGlobalValue<unsigned char>(0x22FB57A, static_cast<unsigned char>(0))),
			R1OHookGlobalValue<int>(0x22FB5A4, 0));
		OutputDebugStringA(buffer);
	}

	if (result)
		return result;

	const uintptr_t base = reinterpret_cast<uintptr_t>(engineR1O);
	const uintptr_t callerRva = base && returnAddress >= base ? returnAddress - base : 0;
	if (callerRva >= 0x201050 && callerRva < 0x2012EA) {
		char* packet = R1ONetGetPacketOriginal
			? R1ONetGetPacketOriginal(socketIndex, data, 0)
			: nullptr;

		static int datagramFallbackLogBudget = 0;
		if (datagramFallbackLogBudget > 0) {
			--datagramFallbackLogBudget;
			char buffer[512];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: R1O NET_ProcessSocket loopback selector fallback packet=%p socket=%d data=%p callerRva=0x%llx wsa=%d\n",
				packet,
				socketIndex,
				reinterpret_cast<void*>(data),
				static_cast<unsigned long long>(callerRva),
				R1OHookGlobalValue<int>(0x22FB5A4, 0));
			OutputDebugStringA(buffer);
		}
		return packet;
	}

	return nullptr;
}

static char __fastcall R1OCDedicatedServerAPI_RunFrame(__int64 thisptr)
{
	if (!IsR1ODedicatedServer())
		return R1OCDedicatedServerAPI_RunFrameOriginal
			? R1OCDedicatedServerAPI_RunFrameOriginal(thisptr)
			: 0;

	static int s_R1ODedicatedRunFrameCount = 0;
	++s_R1ODedicatedRunFrameCount;
	if (IsVPhysicsDeferredProbeLogEnabled()
		&& s_R1ODedicatedRunFrameCount >= VPhysicsDeferredProbeLogFlushFrame()) {
		VPhysicsFlushDeferredProbeLogs("dedicated RunFrame");
	}

	if (!s_R1OLoggedFirstRunFrame) {
		s_R1OLoggedFirstRunFrame = true;
		OutputDebugStringA("R1Delta: R1O CDedicatedServerAPI::RunFrame entered\n");
		LogR1OHostStateSnapshot("first dedicated RunFrame", true);
	}
	const uintptr_t r1oEngineBase = G_engine_r1o
		? G_engine_r1o
		: reinterpret_cast<uintptr_t>(GetModuleHandleA("engine_r1o.dll"));
	InstallR1ORemoteAccessHooks(r1oEngineBase);
	InstallR1OPlaylistCompatibilityHooks(r1oEngineBase);
	EnsureR1ONetConsoleInitialized(r1oEngineBase);

	constexpr int runtimeMaintenanceIntervalFrames = 64;
	if (s_R1ODedicatedRunFrameCount == 1
		|| (s_R1ODedicatedRunFrameCount % runtimeMaintenanceIntervalFrames) == 0) {
		EnsureR1ODedicatedMaterialFallbacks();
		EnsureR1ODedicatedWorldModelFallbacks();
		EnsureR1ODedicatedClientDllModelFallback();
		EnsureR1ODataCacheFileSystemGlobal();
		EnsureR1ODataCachePhysicsSurfacePropsGlobal();
		EnsureR1ODataCachePhysicsCollisionGlobal();
		EnsureR1ODataCacheStudioRenderGlobal();
		EnsureR1OLauncherFileSystemGlobal();
		EnsureR1OLauncherScriptFatalHooks();
		EnsureR1OStudioRenderStudioDataCacheGlobal();
		EnsureR1OStudioRenderMaterialSystemGlobal();
		EnsureR1OStudioRenderMaterialSystemHardwareConfigGlobal();
	}

	// R1O engine.dll is not a real SWDS build. In fake-dedi mode the 2015
	// dedicated frontend enters the RunFrame loop, but the R1O frame path can
	// leave startup commands pending forever. Flush commands from the real
	// dedicated frame boundary, matching where a normal engine frame would run
	// Cbuf_Execute.
	ExecuteR1OCommandBuffers("dedicated RunFrame pre");
	LogR1OHostStateSnapshot("dedicated RunFrame after pre Cbuf_Execute", false);

	const char result = R1OCDedicatedServerAPI_RunFrameOriginal
		? R1OCDedicatedServerAPI_RunFrameOriginal(thisptr)
		: 1;
	LogR1OHostStateSnapshot("dedicated RunFrame after engine frame", false);
	LogR1OClientSlotSnapshot("dedicated RunFrame after engine frame", false);

	if (RunR1OServerAutorunScriptsIfPending())
		ApplyR1OPlaylistAfterServerAutorun(r1oEngineBase);
	ExecuteR1OCommandBuffers("dedicated RunFrame post");
	LogR1OHostStateSnapshot("dedicated RunFrame after post Cbuf_Execute", false);
	LogR1OClientSlotSnapshot("dedicated RunFrame after post Cbuf_Execute", false);
	QueueR1ODediRequestedDummyBot(s_R1ODedicatedRunFrameCount);
	return result;
}

static void __fastcall R1OCDedicatedServerAPI_AddConsoleText(__int64 thisptr, char* text)
{
	if (!IsR1ODedicatedServer()) {
		if (R1OCDedicatedServerAPI_AddConsoleTextOriginal)
			R1OCDedicatedServerAPI_AddConsoleTextOriginal(thisptr, text);
		return;
	}

	if (IsR1OInterestingCommandText(text)) {
		char buffer[768];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O CDedicatedServerAPI::AddConsoleText text=%s\n",
			text);
		OutputDebugStringA(buffer);
	}

	if (R1OCDedicatedServerAPI_AddConsoleTextOriginal) {
		R1OCDedicatedServerAPI_AddConsoleTextOriginal(thisptr, text);
		return;
	}

	QueueR1OCommandBufferText(1, text);
}

static __int64 __fastcall R1OSysInit(__int64 thisptr, const char* baseDirectory, int dedicated)
{
	if (!IsR1ODedicatedServer())
		return R1OSysInitOriginal
			? R1OSysInitOriginal(thisptr, baseDirectory, dedicated)
			: 0;

	char buffer[512];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O Sys_Init forcing fake-dedi dedicated flag this=%p basedir=%p(%s) incomingDedicated=%d\n",
		reinterpret_cast<void*>(thisptr),
		baseDirectory,
		IsReadableCString(baseDirectory) ? baseDirectory : "<invalid>",
		dedicated);
	OutputDebugStringA(buffer);

	EnsureR1OWrappedCVarInterfaceReady();
	const __int64 result = R1OSysInitOriginal
		? R1OSysInitOriginal(thisptr, baseDirectory, 1)
		: 0;
	if (result)
		RegisterR1ODediDeltaConVars();
	return result;
}

static const char* FindCommandLineTokenValue(const char* token)
{
	const char* cmdLine = GetCommandLineA();
	if (!cmdLine || !token || !token[0])
		return nullptr;

	const size_t tokenLength = strlen(token);
	for (const char* scan = cmdLine; *scan; ++scan) {
		if ((scan == cmdLine || scan[-1] == ' ' || scan[-1] == '\t')
			&& !_strnicmp(scan, token, tokenLength)
			&& (scan[tokenLength] == ' ' || scan[tokenLength] == '\t')) {
			const char* value = scan + tokenLength;
			while (*value == ' ' || *value == '\t')
				++value;
			return *value ? value : nullptr;
		}
	}

	return nullptr;
}

static bool CopyCommandLineTokenValue(const char* token, char* out, size_t outSize)
{
	if (!out || !outSize)
		return false;
	out[0] = '\0';

	const char* value = FindCommandLineTokenValue(token);
	if (!value)
		return false;

	if (*value == '"') {
		++value;
		size_t copied = 0;
		while (value[copied] && value[copied] != '"' && copied + 1 < outSize) {
			out[copied] = value[copied];
			++copied;
		}
		out[copied] = '\0';
		return out[0] != '\0';
	}

	size_t copied = 0;
	while (value[copied] && value[copied] != ' ' && value[copied] != '\t' && copied + 1 < outSize) {
		out[copied] = value[copied];
		++copied;
	}
	out[copied] = '\0';
	return out[0] != '\0';
}

static int ResolveR1ODediServerPort(int currentPort)
{
	if (cvarinterface && OriginalCCVar_FindVar) {
		if (ConVarR1* hostPort =
				OriginalCCVar_FindVar(cvarinterface, "hostport")) {
			const int value = hostPort->m_Value.m_nValue;
			if (value > 0 && value <= 0xFFFF)
				return value;
		}
	}

	if (currentPort > 0 && currentPort <= 0xFFFF)
		return currentPort;

	char portText[32];
	const char* tokens[] = { "-port", "+hostport", "+port" };
	for (const char* token : tokens) {
		if (!CopyCommandLineTokenValue(token, portText, sizeof(portText)))
			continue;

		const int parsed = atoi(portText);
		if (parsed > 0 && parsed <= 0xFFFF)
			return parsed;
	}

	return 27015;
}

static bool EnsureR1ODediWinsock()
{
	static bool initialized = false;
	static bool attempted = false;
	if (initialized)
		return true;
	if (attempted)
		return false;

	attempted = true;
	WSADATA data = {};
	const int result = WSAStartup(MAKEWORD(2, 2), &data);
	initialized = result == 0;
	if (!initialized) {
		char buffer[160];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O fake-dedi WSAStartup failed result=%d wsa=%d\n",
			result,
			WSAGetLastError());
		OutputDebugStringA(buffer);
	}
	return initialized;
}

static void ConfigureR1ODediSocket(SOCKET socketHandle)
{
	u_long nonBlocking = 1;
	if (ioctlsocket(socketHandle, FIONBIO, &nonBlocking) == SOCKET_ERROR)
		WriteR1OHookGlobalValue<int>(0x22FB5A4, WSAGetLastError());

	int bufferSize = 0x100000;
	setsockopt(socketHandle, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&bufferSize), sizeof(bufferSize));
	setsockopt(socketHandle, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&bufferSize), sizeof(bufferSize));
}

static int OpenR1ODediSocket(int port, bool stream)
{
	if (!EnsureR1ODediWinsock())
		return 0;

	const SOCKET socketHandle = socket(AF_INET6, stream ? SOCK_STREAM : SOCK_DGRAM, stream ? IPPROTO_TCP : IPPROTO_UDP);
	if (socketHandle == INVALID_SOCKET) {
		WriteR1OHookGlobalValue<int>(0x22FB5A4, WSAGetLastError());
		return 0;
	}

	ConfigureR1ODediSocket(socketHandle);

	DWORD ipv6Only = 0;
	setsockopt(socketHandle, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&ipv6Only), sizeof(ipv6Only));

	if (!stream) {
		BOOL broadcast = TRUE;
		setsockopt(socketHandle, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&broadcast), sizeof(broadcast));
	} else {
		BOOL reuseAddress = TRUE;
		setsockopt(socketHandle, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuseAddress), sizeof(reuseAddress));
	}

	sockaddr_in6 address = {};
	address.sin6_family = AF_INET6;
	address.sin6_addr = in6addr_any;
	address.sin6_port = htons(static_cast<u_short>(port));

	if (bind(socketHandle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
		WriteR1OHookGlobalValue<int>(0x22FB5A4, WSAGetLastError());
		closesocket(socketHandle);
		return 0;
	}

	if (stream && listen(socketHandle, 8) == SOCKET_ERROR) {
		WriteR1OHookGlobalValue<int>(0x22FB5A4, WSAGetLastError());
		closesocket(socketHandle);
		return 0;
	}

	WriteR1OHookGlobalValue<int>(0x22FB5A4, 0);
	return static_cast<int>(socketHandle);
}

static void* GetR1OCurrentCommandBuffer()
{
	if (!engineR1O)
		return nullptr;

	using R1OGetCurrentCommandBufferIndexType = int(__fastcall*)();
	auto getCurrentCommandBufferIndex = reinterpret_cast<R1OGetCurrentCommandBufferIndexType>(
		reinterpret_cast<uintptr_t>(engineR1O) + 0x1620E0);
	const int index = getCurrentCommandBufferIndex ? getCurrentCommandBufferIndex() : 0;
	if (index < 0 || index > 4)
		return nullptr;

	return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(engineR1O) + 0x29A2100 + (static_cast<uintptr_t>(index) * 9888));
}

static void* GetR1OCommandBufferByIndex(int index)
{
	if (!engineR1O || index < 0 || index > 1)
		return nullptr;

	return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(engineR1O) + 0x29A2100 + (static_cast<uintptr_t>(index) * 9888));
}

static bool QueueR1OCommandBufferText(int index, const char* text)
{
	if (!R1OCbuf_AddTextOriginal || !text)
		return false;

	void* commandBuffer = GetR1OCommandBufferByIndex(index);
	if (!commandBuffer)
		return false;

	const char result = R1OCbuf_AddTextOriginal(commandBuffer, text);
	if (result && text && *text)
		s_R1OCommandBuffersDirty = true;
	if (IsR1ODedicatedServer() && IsR1OInterestingCommandText(text)) {
		char buffer[768];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: queued R1O command buffer=%d ptr=%p result=%d text=%s\n",
			index,
			commandBuffer,
			static_cast<int>(result),
			text);
		OutputDebugStringA(buffer);
	}

	return result != 0;
}

static void ExecuteR1OCommandBuffers(const char* reason)
{
	if (!R1OCbuf_ExecuteOriginal || !s_R1OCommandBuffersDirty)
		return;

	bool loggedExecute = false;
	if (IsR1ODedicatedServer() && AreR1OFakeDediVerboseLogsEnabled() && s_R1OCommandBufferExecuteLogBudget > 0) {
		--s_R1OCommandBufferExecuteLogBudget;
		loggedExecute = true;
		char buffer[256];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: executing R1O command buffers reason=%s budget=%d\n",
			reason ? reason : "<none>",
			s_R1OCommandBufferExecuteLogBudget);
		OutputDebugStringA(buffer);
	}

	s_R1OCommandBuffersDirty = false;
	R1OCbuf_ExecuteOriginal();

	if (loggedExecute) {
		char buffer[256];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: executed R1O command buffers reason=%s dirty=%d\n",
			reason ? reason : "<none>",
			s_R1OCommandBuffersDirty ? 1 : 0);
		OutputDebugStringA(buffer);
	}
}

static void QueueR1ODediStartupCommands()
{
	if (!IsR1ODedicatedServer() || s_R1OStartupCommandsQueued || !R1OCbuf_AddTextOriginal)
		return;

	void* commandBuffer = GetR1OCommandBufferByIndex(1);
	if (!commandBuffer)
		return;

	s_R1OStartupCommandsQueued = true;
	QueueR1OCommandBufferText(1, "developer 1\n");
	QueueR1OCommandBufferText(1, "exec startup_dedi_retail.cfg\n");
	QueueR1OCommandBufferText(1, "net_data_block_enabled 1\n");
	QueueR1OCommandBufferText(1, "maxplayers 16\n");

	char spawnTitanClass[128];
	if (!CopyCommandLineTokenValue("+info_spawnpoint_titan_classname", spawnTitanClass, sizeof(spawnTitanClass))
		&& !CopyCommandLineTokenValue("-info_spawnpoint_titan_classname", spawnTitanClass, sizeof(spawnTitanClass))) {
		// TFO server.dll defaults this to titan_atlas_tier0, but the R1Delta
		// class manifest loaded in fake-dedi mode provides titan_atlas.
		QueueR1OCommandBufferText(1, "info_spawnpoint_titan_classname titan_atlas\n");
	}

	char mapName[128] = {};
	if (CopyCommandLineTokenValue("+map", mapName, sizeof(mapName))) {
		char command[192];
		_snprintf_s(command, sizeof(command), _TRUNCATE, "map %s\n", mapName);
		QueueR1OCommandBufferText(1, command);
	}

	char buffer[384];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: queued R1O fake-dedi startup commands buffer=%p map=%s\n",
		commandBuffer,
		mapName[0] ? mapName : "<none>");
	OutputDebugStringA(buffer);
}

static const char* GetR1ODediFallbackBaseDirectory()
{
	static char s_baseDirectory[MAX_PATH];

	if (s_baseDirectory[0])
		return s_baseDirectory;

	DWORD len = GetModuleFileNameA(NULL, s_baseDirectory, sizeof(s_baseDirectory));
	if (!len || len >= sizeof(s_baseDirectory)) {
		strcpy_s(s_baseDirectory, ".");
		return s_baseDirectory;
	}

	char* lastBackslash = strrchr(s_baseDirectory, '\\');
	char* lastSlash = strrchr(s_baseDirectory, '/');
	char* lastSeparator = lastBackslash;
	if (!lastSeparator || (lastSlash && lastSlash > lastSeparator))
		lastSeparator = lastSlash;
	if (lastSeparator)
		*lastSeparator = '\0';

	return s_baseDirectory;
}

static const char* GetR1ODediInstallBaseDirectory()
{
	static char s_installDirectory[MAX_PATH];
	if (s_installDirectory[0])
		return s_installDirectory;

	const DWORD len = GetCurrentDirectoryA(sizeof(s_installDirectory), s_installDirectory);
	if (!len || len >= sizeof(s_installDirectory))
		return GetR1ODediFallbackBaseDirectory();

	return s_installDirectory;
}

static bool BuildR1ODediLooseModPath(const char* relativePath, char* outPath, size_t outPathSize)
{
	if (!relativePath || !relativePath[0] || !outPath || !outPathSize)
		return false;
	if ((relativePath[1] == ':') || relativePath[0] == '/' || relativePath[0] == '\\')
		return false;

	_snprintf_s(outPath, outPathSize, _TRUNCATE, "%s\\r1delta\\%s", GetR1ODediFallbackBaseDirectory(), relativePath);
	for (char* it = outPath; *it; ++it) {
		if (*it == '/')
			*it = '\\';
	}

	const DWORD attributes = GetFileAttributesA(outPath);
	return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static bool BuildR1ODediRelativeModPath(const char* absolutePath, char* outPath, size_t outPathSize)
{
	if (!absolutePath || !absolutePath[0] || !outPath || !outPathSize)
		return false;

	char normalizedPath[MAX_PATH];
	char normalizedRoot[MAX_PATH];
	_snprintf_s(normalizedPath, sizeof(normalizedPath), _TRUNCATE, "%s", absolutePath);
	_snprintf_s(
		normalizedRoot,
		sizeof(normalizedRoot),
		_TRUNCATE,
		"%s\\r1delta\\",
		GetR1ODediFallbackBaseDirectory());

	for (char* it = normalizedPath; *it; ++it) {
		if (*it == '/')
			*it = '\\';
	}
	for (char* it = normalizedRoot; *it; ++it) {
		if (*it == '/')
			*it = '\\';
	}

	const size_t rootLength = strlen(normalizedRoot);
	if (_strnicmp(normalizedPath, normalizedRoot, rootLength) != 0)
		return false;

	const DWORD attributes = GetFileAttributesA(normalizedPath);
	if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY))
		return false;

	_snprintf_s(outPath, outPathSize, _TRUNCATE, "%s", normalizedPath + rootLength);
	return outPath[0] != '\0';
}

static void DebugR1ODediModInfo(const char* prefix, const DedicatedServerModInfo2015* info)
{
	char buffer[1024];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: %s ModInfo=%p instance=%p basedir=%p(%s) initialmod=%p(%s) initialgame=%p(%s) parent=%p textmode=%u\n",
		prefix,
		info,
		info ? info->m_pInstance : nullptr,
		info ? info->m_pBaseDirectory : nullptr,
		info && IsReadableCString(info->m_pBaseDirectory) ? info->m_pBaseDirectory : "<invalid>",
		info ? info->m_pInitialMod : nullptr,
		info && IsReadableCString(info->m_pInitialMod) ? info->m_pInitialMod : "<invalid>",
		info ? info->m_pInitialGame : nullptr,
		info && IsReadableCString(info->m_pInitialGame) ? info->m_pInitialGame : "<invalid>",
		info ? info->m_pParentAppSystemGroup : nullptr,
		info ? static_cast<unsigned int>(info->m_bTextMode) : 0);
	OutputDebugStringA(buffer);
}

static void DebugR1ODediFactoryResult(const char* source, const char* name, void* result, int* returnCode)
{
	if (!IsR1ODedicatedServer())
		return;

	char buffer[512];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O factory %s request=%s result=%p rc=%d\n",
		source ? source : "<unknown>",
		name ? name : "<null>",
		result,
		returnCode ? *returnCode : 0);
	OutputDebugStringA(buffer);
}

static CreateInterfaceFn GetDedicatedCreateInterfaceForR1O()
{
	if (oAppSystemFactory)
		return oAppSystemFactory;

	HMODULE dedicated = GetModuleHandleA("dedicated.dll");
	if (!dedicated)
		return nullptr;

	auto factory = reinterpret_cast<CreateInterfaceFn>(GetProcAddress(dedicated, "CreateInterface"));
	if (factory) {
		oAppSystemFactory = factory;
		oFileSystemFactory = factory;
		oPhysicsFactory = factory;
	}

	return factory;
}

static bool EnsureR1ODedicatedServerAPIConnected(__int64 thisptr)
{
	if (!IsR1ODedicatedServer())
		return true;

	if (s_R1ODedicatedServerAPIConnected)
		return true;

	if (!CDedicatedServerAPI_ConnectOriginal) {
		OutputDebugStringA("R1Delta: cannot connect R1O CDedicatedServerAPI; original Connect is missing\n");
		return false;
	}

	CreateInterfaceFn dedicatedFactory = GetDedicatedCreateInterfaceForR1O();
	if (!dedicatedFactory) {
		OutputDebugStringA("R1Delta: cannot connect R1O CDedicatedServerAPI; dedicated CreateInterface is missing\n");
		return false;
	}

	char buffer[256];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: connecting R1O CDedicatedServerAPI before ModInit this=%p dedicatedFactory=%p\n",
		reinterpret_cast<void*>(thisptr),
		reinterpret_cast<void*>(dedicatedFactory));
	OutputDebugStringA(buffer);

	oAppSystemFactory = dedicatedFactory;
	oFileSystemFactory = dedicatedFactory;
	oPhysicsFactory = dedicatedFactory;

	const char result = CDedicatedServerAPI_ConnectOriginal(thisptr, &R1OFactory);
	s_R1ODedicatedServerAPIConnected = result != 0;

	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O CDedicatedServerAPI pre-ModInit Connect result=%d\n",
		static_cast<int>(result));
	OutputDebugStringA(buffer);

	return s_R1ODedicatedServerAPIConnected;
}

static bool IsAbsoluteModulePath(const char* path)
{
	if (!path || !path[0])
		return false;

	return (path[1] == ':') || path[0] == '/' || path[0] == '\\';
}

static bool PathBasenameEquals(const char* path, const char* expected)
{
	if (!path || !expected)
		return false;

	const char* base = path;
	for (const char* it = path; *it; ++it) {
		if (*it == '\\' || *it == '/')
			base = it + 1;
	}

	return _stricmp(base, expected) == 0;
}

static bool IsReadOnlyFileOpen(const char* options)
{
	if (!options || !options[0])
		return false;

	bool reads = false;
	for (const char* it = options; *it; ++it) {
		const char c = static_cast<char>(tolower(static_cast<unsigned char>(*it)));
		if (c == 'r')
			reads = true;
		if (c == 'w' || c == 'a' || c == '+')
			return false;
	}

	return reads;
}

static HMODULE LoadR1ODedicatedModuleFromPath(const char* path)
{
	if (!path || !path[0])
		return nullptr;

	HMODULE module = GetModuleHandleA(path);
	if (module)
		return module;

	module = LoadLibraryExA(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!module)
		module = LoadLibraryA(path);
	return module;
}

static bool IsR1OTFOSupportModuleName(const char* requested)
{
	for (const R1OTFOSupportModule& module : s_R1OTFOSupportModules) {
		if (PathBasenameEquals(requested, module.moduleName))
			return true;
	}
	return false;
}

static HMODULE LoadR1OTFOSupportModule(const char* moduleName)
{
	if (!moduleName || !moduleName[0])
		return nullptr;

	for (R1OTFOSupportModule& module : s_R1OTFOSupportModules) {
		if (!PathBasenameEquals(moduleName, module.moduleName))
			continue;

		if (module.module)
			return module.module;

		const std::string path = r1delta::r1o::ResolveTFOModulePathA(module.moduleName);
		if (!path.empty())
			module.module = LoadLibraryExA(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
		module.factory = module.module
			? reinterpret_cast<CreateInterfaceFn>(GetProcAddress(module.module, "CreateInterface"))
			: nullptr;

		const std::string validationError = r1delta::r1o::TFORuntimeValidationErrorA();
		char buffer[1024];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O TFO support module load name=%s path=%s module=%p factory=%p gle=%lu validation=%s\n",
			module.moduleName,
			path.empty() ? "<unresolved>" : path.c_str(),
			module.module,
			reinterpret_cast<void*>(module.factory),
			module.module ? 0 : GetLastError(),
			validationError.empty() ? "<ok>" : validationError.c_str());
		OutputDebugStringA(buffer);
		return module.module;
	}

	return nullptr;
}

static HMODULE LoadR1ODedicatedMaterialSystemProxy()
{
	HMODULE module = GetModuleHandleA("materialsystem_nodx.dll");
	if (module)
		return module;

	char tier0Path[MAX_PATH];
	DWORD len = GetModuleFileNameA(GetModuleHandleA("tier0.dll"), tier0Path, sizeof(tier0Path));
	if (len && len < sizeof(tier0Path)) {
		char* slash = strrchr(tier0Path, '\\');
		if (slash) {
			*(slash + 1) = 0;
			strncat_s(tier0Path, sizeof(tier0Path), "materialsystem_nodx.dll", _TRUNCATE);
			module = LoadR1ODedicatedModuleFromPath(tier0Path);
			if (module)
				return module;
		}
	}

	return LoadR1ODedicatedModuleFromPath("materialsystem_nodx.dll");
}

static HMODULE __fastcall R1OLoadModule(char* source)
{
	if (!IsR1ODedicatedServer())
		return R1OLoadModuleOriginal(source);

	const char* requested = source ? source : "";
	HMODULE module = nullptr;

	if (PathBasenameEquals(requested, "engine.dll") || PathBasenameEquals(requested, "engine_ds.dll") || PathBasenameEquals(requested, "engine_r1o.dll")) {
		module = GetModuleHandleA("engine_r1o.dll");
	} else if (PathBasenameEquals(requested, "materialsystem.dll")
		|| PathBasenameEquals(requested, "materialsystem_dx11.dll")
		|| PathBasenameEquals(requested, "materialsystem_nodx.dll")) {
		module = LoadR1ODedicatedMaterialSystemProxy();
	} else if (IsR1OTFOSupportModuleName(requested)) {
		module = LoadR1OTFOSupportModule(requested);
	} else if (IsAbsoluteModulePath(requested)) {
		module = R1OLoadModuleOriginal ? R1OLoadModuleOriginal(source) : LoadR1ODedicatedModuleFromPath(requested);
	} else {
		const std::string path = r1delta::r1o::ResolveTFOModulePathA(requested);
		module = LoadR1ODedicatedModuleFromPath(path.c_str());
	}

	char buffer[512];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O LoadModule request=%s result=%p gle=%lu\n",
		requested[0] ? requested : "<null>",
		module,
		module ? 0 : GetLastError());
	OutputDebugStringA(buffer);

	return module;
}

static char __fastcall R1OLoadExternalServerInfo(__int64 thisptr)
{
	return R1OLoadExternalServerInfoOriginal
		? R1OLoadExternalServerInfoOriginal(thisptr)
		: 0;
}

static char __fastcall R1OSVCServerInfoWriteToBuffer(__int64 message, __int64 bitBuffer)
{
	if (!IsR1ODedicatedServer())
		return R1OSVCServerInfoWriteToBufferOriginal
			? R1OSVCServerInfoWriteToBufferOriginal(message, bitBuffer)
			: 0;

	const char* previousGameDir = nullptr;
	const char* previousMapName = nullptr;
	const int previousProtocol = IsReadableRange(reinterpret_cast<void*>(message + 32), sizeof(int))
		? *reinterpret_cast<int*>(message + 32)
		: -1;
	const int previousMaxClients = IsReadableRange(reinterpret_cast<void*>(message + 56), sizeof(int))
		? *reinterpret_cast<int*>(message + 56)
		: -1;
	const int previousMaxClasses = IsReadableRange(reinterpret_cast<void*>(message + 60), sizeof(int))
		? *reinterpret_cast<int*>(message + 60)
		: -1;
	const int previousPlayerSlot = IsReadableRange(reinterpret_cast<void*>(message + 64), sizeof(int))
		? *reinterpret_cast<int*>(message + 64)
		: -1;
	if (IsReadableRange(reinterpret_cast<void*>(message + 72), sizeof(previousGameDir)))
		previousGameDir = *reinterpret_cast<const char**>(message + 72);
	if (IsReadableRange(reinterpret_cast<void*>(message + 80), sizeof(previousMapName)))
		previousMapName = *reinterpret_cast<const char**>(message + 80);
	if (!IsReadableRange(reinterpret_cast<void*>(message + 72), sizeof(const char*))
		|| !IsReadableRange(reinterpret_cast<void*>(message + 80), sizeof(const char*))
		|| !IsReadableRange(reinterpret_cast<void*>(message + 32), sizeof(int))
		|| !IsReadableRange(reinterpret_cast<void*>(message + 36), sizeof(int))
		|| !IsReadableRange(reinterpret_cast<void*>(message + 40), sizeof(unsigned char) * 4)
		|| !IsReadableRange(reinterpret_cast<void*>(message + 44), sizeof(int))
		|| !IsReadableRange(reinterpret_cast<void*>(message + 48), sizeof(int))
		|| !IsReadableRange(reinterpret_cast<void*>(message + 52), sizeof(int))
		|| !IsReadableRange(reinterpret_cast<void*>(message + 56), sizeof(int))
		|| !IsReadableRange(reinterpret_cast<void*>(message + 60), sizeof(int))
		|| !IsReadableRange(reinterpret_cast<void*>(message + 64), sizeof(int))
		|| !IsReadableRange(reinterpret_cast<void*>(message + 68), sizeof(float))
		|| !IsReadableRange(reinterpret_cast<void*>(message + 88), sizeof(const char*))
		|| !IsReadableRange(reinterpret_cast<void*>(message + 96), sizeof(const char*))
		|| !IsReadableRange(reinterpret_cast<void*>(message + 104), sizeof(const char*))
		|| !IsReadableRange(reinterpret_cast<void*>(bitBuffer), sizeof(bf_write))) {
		return R1OSVCServerInfoWriteToBufferOriginal
			? R1OSVCServerInfoWriteToBufferOriginal(message, bitBuffer)
			: 0;
	}

	static const char* legacyGameDir = "r1";
	*reinterpret_cast<const char**>(message + 72) = legacyGameDir;

	static char commandLineMapName[128];
	const char* replacementMapName = previousMapName;
	if (!IsReadableCString(previousMapName) || !previousMapName[0]) {
		const char* commandLine = GetCommandLineA();
		const char* mapArg = commandLine ? strstr(commandLine, "+map") : nullptr;
		if (mapArg) {
			mapArg += 4;
			while (*mapArg == ' ' || *mapArg == '\t')
				++mapArg;
			size_t length = 0;
			while (mapArg[length] && mapArg[length] != ' ' && mapArg[length] != '\t' && mapArg[length] != '"' && length + 1 < sizeof(commandLineMapName))
				++length;
			if (length > 0) {
				memcpy(commandLineMapName, mapArg, length);
				commandLineMapName[length] = '\0';
				replacementMapName = commandLineMapName;
				*reinterpret_cast<const char**>(message + 80) = replacementMapName;
			}
		}
	}

	const char* skyName = *reinterpret_cast<const char**>(message + 88);
	const char* hostName = *reinterpret_cast<const char**>(message + 96);
	const char* loadingUrl = *reinterpret_cast<const char**>(message + 104);
	if (!IsReadableCString(skyName))
		skyName = "";
	if (!IsReadableCString(hostName))
		hostName = "";
	if (!IsReadableCString(loadingUrl))
		loadingUrl = "";

	int serverCount = *reinterpret_cast<int*>(message + 36);
	// The native TFO signon validator (engine_r1o+0x13F2ED) compares every client
	// ACK against CGameServer.m_nSpawnCount (server+0x1D0), but TFO's ServerInfo
	// fill falls back to CGameServer.m_nMaxclients (+0x1CC) when its mode object
	// is unavailable. If the two diverge, clients latch the wrong server count
	// from this message, echo it in every signon ACK, and get force-retried
	// forever. Publish the authoritative value the validator actually uses.
	constexpr uintptr_t kR1OServerSpawnCountRva = 0x2659720; // qword_18265971C + 4
	if (engineR1O) {
		const int* authoritativeSpawnCount = reinterpret_cast<const int*>(
			reinterpret_cast<uintptr_t>(engineR1O) + kR1OServerSpawnCountRva);
		if (IsReadableRange(authoritativeSpawnCount, sizeof(int))
			&& *authoritativeSpawnCount != serverCount) {
			if (s_R1OServerInfoForceLogBudget > 0) {
				--s_R1OServerInfoForceLogBudget;
				char buffer[256];
				_snprintf_s(
					buffer,
					sizeof(buffer),
					_TRUNCATE,
					"R1Delta: R1O SVC_ServerInfo corrected divergent server count fill=%d authoritative=%d\n",
					serverCount,
					*authoritativeSpawnCount);
				OutputDebugStringA(buffer);
				Warning("%s", buffer);
			}
			serverCount = *authoritativeSpawnCount;
		}
	}
	const int unknownLong = *reinterpret_cast<int*>(message + 44);
	const int mapCrc = *reinterpret_cast<int*>(message + 48);
	const int clientCrc = *reinterpret_cast<int*>(message + 52);
	const int maxClients = previousMaxClients >= 1 && previousMaxClients <= 19 ? previousMaxClients : 16;
	const int maxClasses = previousMaxClasses >= 1 && previousMaxClasses <= 512 ? previousMaxClasses : 512;
	const int playerSlot = previousPlayerSlot >= 0 && previousPlayerSlot <= 18 ? previousPlayerSlot : 0;
	float tickInterval = *reinterpret_cast<float*>(message + 68);
	if (tickInterval < 0.001f || tickInterval > 0.1f)
		tickInterval = 0.05f;
	const char os = *reinterpret_cast<char*>(message + 43) ? *reinterpret_cast<char*>(message + 43) : 'w';

	bf_write* outerWriter = reinterpret_cast<bf_write*>(bitBuffer);
	const int startBit = outerWriter->GetNumBitsWritten();
	unsigned char* writerData = outerWriter->GetData();
	const int maxBits = outerWriter->GetMaxNumBits();
	for (int bit = startBit; writerData && bit < maxBits; ++bit)
		writerData[bit >> 3] &= static_cast<unsigned char>(~(1u << (bit & 7)));

	CBitWrite writer(writerData, (maxBits + 7) >> 3, maxBits);
	writer.SeekToBit(startBit);
	writer.WriteWord(2001);
	writer.WriteLong(serverCount);
	writer.WriteOneBit(*reinterpret_cast<unsigned char*>(message + 41) != 0);
	writer.WriteOneBit(*reinterpret_cast<unsigned char*>(message + 42) != 0);
	writer.WriteOneBit(*reinterpret_cast<unsigned char*>(message + 40) != 0);
	writer.WriteLong(mapCrc);
	writer.WriteLong(clientCrc);
	writer.WriteWord(maxClasses);
	writer.WriteLong(unknownLong);
	writer.WriteByte(playerSlot);
	writer.WriteByte(maxClients);
	writer.WriteBitFloat(tickInterval);
	writer.WriteChar(os);
	writer.WriteString(legacyGameDir);
	writer.WriteString(IsReadableCString(replacementMapName) ? replacementMapName : "");
	writer.WriteString(skyName);
	writer.WriteString(hostName);
	writer.WriteString(loadingUrl);
	writer.WriteString("");
	writer.TempFlush();

	const char result = writer.IsOverflowed() ? 0 : 1;
	const int endBit = writer.GetNumBitsWritten();
	if (writer.IsOverflowed())
		outerWriter->SetOverflowFlag();
	else
		outerWriter->SeekToBit(endBit);

	if (s_R1OSVCServerInfoWriteLogBudget > 0) {
		--s_R1OSVCServerInfoWriteLogBudget;
		int decodedProtocol = -1;
		int decodedServerCount = -1;
		int decodedMaxClients = -1;
		int decodedMaxClasses = -1;
		int decodedPlayerSlot = -1;
		float decodedTickInterval = -1.0f;
		char decodedGameDir[128] = {};
		char decodedMapName[128] = {};
		char encodedBytes[128] = {};
		bool decodedOk = false;
		if (outerWriter->GetData() && endBit >= startBit) {
			const unsigned char* encodedData = outerWriter->GetData();
			const int byteOffset = startBit >> 3;
			const int maxBytes = outerWriter->GetNumBytesWritten();
			size_t written = 0;
			const int limit = maxBytes - byteOffset < 24 ? maxBytes - byteOffset : 24;
			for (int i = 0; i < limit && written + 4 < sizeof(encodedBytes); ++i) {
				written += static_cast<size_t>(_snprintf_s(
					encodedBytes + written,
					sizeof(encodedBytes) - written,
					_TRUNCATE,
					"%02X%s",
					encodedData[byteOffset + i],
					i + 1 < limit ? " " : ""));
			}
			CBitRead verify(outerWriter->GetData(), (outerWriter->GetMaxNumBits() + 7) >> 3, endBit);
			verify.Seek(startBit);
			decodedProtocol = verify.ReadWord();
			decodedServerCount = verify.ReadLong();
			verify.ReadOneBit();
			verify.ReadOneBit();
			verify.ReadOneBit();
			verify.ReadLong();
			verify.ReadLong();
			decodedMaxClasses = verify.ReadWord();
			verify.ReadLong();
			decodedPlayerSlot = verify.ReadByte();
			decodedMaxClients = verify.ReadByte();
			decodedTickInterval = verify.ReadBitFloat();
			verify.ReadChar();
			verify.ReadString(decodedGameDir, sizeof(decodedGameDir));
			verify.ReadString(decodedMapName, sizeof(decodedMapName));
			decodedOk = !verify.IsOverflowed();
		}
		char buffer[1024];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O SVC_ServerInfo::WriteToBuffer msg=%p bitbuf=%p result=%d bits=%d->%d bytes=[%s] protocol=%d->2001 gamedir=%s->%s map=%s->%s maxclients=%d->%d maxclasses=%d->%d playerslot=%d->%d tick=%f os=%c verifyOk=%d verifyProtocol=%d verifyGame=%s verifyMap=%s verifyMax=%d verifyClasses=%d verifySlot=%d verifyTick=%f budget=%d\n",
			reinterpret_cast<void*>(message),
			reinterpret_cast<void*>(bitBuffer),
			static_cast<int>(result),
			startBit,
			endBit,
			encodedBytes,
			previousProtocol,
			IsReadableCString(previousGameDir) ? previousGameDir : "<invalid>",
			legacyGameDir,
			IsReadableCString(previousMapName) ? previousMapName : "<invalid>",
			IsReadableCString(replacementMapName) ? replacementMapName : "<invalid>",
			previousMaxClients,
			maxClients,
			previousMaxClasses,
			maxClasses,
			previousPlayerSlot,
			playerSlot,
			tickInterval,
			os,
			static_cast<int>(decodedOk),
			decodedProtocol,
			decodedGameDir,
			decodedMapName,
			decodedMaxClients,
			decodedMaxClasses,
			decodedPlayerSlot,
			decodedTickInterval,
			s_R1OSVCServerInfoWriteLogBudget);
		OutputDebugStringA(buffer);
	}

	return result;
}

static void HookR1OTFOFileSystemReplacementHooks(HMODULE filesystemModule)
{
	if (!IsR1ODedicatedServer() || !filesystemModule || s_R1OTFOFileSystemReplacementHooksInstalled)
		return;

	void* readVpkTarget = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(filesystemModule) + 0x6A420);
	void* readCacheTarget = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(filesystemModule) + 0x9C20);
	MH_STATUS readVpkStatus = MH_CreateHook(readVpkTarget, &ReadFileFromVPKHook, reinterpret_cast<LPVOID*>(&readFileFromVPK));
	MH_STATUS readCacheStatus = MH_CreateHook(readCacheTarget, &ReadFromCacheHook, reinterpret_cast<LPVOID*>(&readFromCache));
	MH_STATUS readVpkEnable = (readVpkStatus == MH_OK || readVpkStatus == MH_ERROR_ALREADY_CREATED) ? MH_EnableHook(readVpkTarget) : readVpkStatus;
	MH_STATUS readCacheEnable = (readCacheStatus == MH_OK || readCacheStatus == MH_ERROR_ALREADY_CREATED) ? MH_EnableHook(readCacheTarget) : readCacheStatus;
	s_R1OTFOFileSystemReplacementHooksInstalled =
		(readVpkEnable == MH_OK || readVpkEnable == MH_ERROR_ENABLED || readVpkStatus == MH_ERROR_ALREADY_CREATED)
		&& (readCacheEnable == MH_OK || readCacheEnable == MH_ERROR_ENABLED || readCacheStatus == MH_ERROR_ALREADY_CREATED);

	char buffer[512];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O TFO filesystem TryReplaceFile hooks module=%p readVpkStatus=%d readVpkEnable=%d readVpkTarget=%p readCacheStatus=%d readCacheEnable=%d readCacheTarget=%p installed=%d\n",
		filesystemModule,
		static_cast<int>(readVpkStatus),
		static_cast<int>(readVpkEnable),
		readVpkTarget,
		static_cast<int>(readCacheStatus),
		static_cast<int>(readCacheEnable),
		readCacheTarget,
		static_cast<int>(s_R1OTFOFileSystemReplacementHooksInstalled));
	OutputDebugStringA(buffer);
}

static CreateInterfaceFn GetR1OTFOFileSystemFactory()
{
	if (s_R1OTFOFileSystemFactory)
		return s_R1OTFOFileSystemFactory;

	const std::string path = r1delta::r1o::ResolveTFOModulePathA("filesystem_stdio.dll");

	if (!path.empty())
		s_R1OTFOFileSystem = LoadLibraryExA(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);

	s_R1OTFOFileSystemFactory = s_R1OTFOFileSystem
		? reinterpret_cast<CreateInterfaceFn>(GetProcAddress(s_R1OTFOFileSystem, "CreateInterface"))
		: nullptr;

	const std::string validationError = r1delta::r1o::TFORuntimeValidationErrorA();
	char buffer[1024];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O TFO filesystem load path=%s module=%p factory=%p gle=%lu validation=%s\n",
		path.empty() ? "<unresolved>" : path.c_str(),
		s_R1OTFOFileSystem,
		reinterpret_cast<void*>(s_R1OTFOFileSystemFactory),
		GetLastError(),
		validationError.empty() ? "<ok>" : validationError.c_str());
	OutputDebugStringA(buffer);
	if (s_R1OTFOFileSystem)
		HookR1OTFOFileSystemReplacementHooks(s_R1OTFOFileSystem);

	return s_R1OTFOFileSystemFactory;
}

static bool TryResolveR1ODediLooseReplacementPath(
	const FileCache::ReadLease& lease,
	const char* fileName,
	char* outPath,
	size_t outPathSize)
{
	if (!fileName || !fileName[0] || !outPath || !outPathSize)
		return false;

	char normalized[MAX_PATH];
	const char* source = fileName;
	while ((source[0] == '/' || source[0] == '\\') && (source[1] == '/' || source[1] == '\\') && source[2] && source[3] == ':')
		++source;
	_snprintf_s(normalized, sizeof(normalized), _TRUNCATE, "%s", source);
	for (char* it = normalized; *it; ++it) {
		if (*it == '/')
			*it = '\\';
	}

	const char* candidates[10] = {};
	char cacheRelative[MAX_PATH] = {};
	char rootRelative[MAX_PATH] = {};
	char vscriptRelative[MAX_PATH] = {};
	char vscriptNutRelative[MAX_PATH] = {};
	char normalizedNutRelative[MAX_PATH] = {};
	char playlistAliasRelative[MAX_PATH] = {};
	size_t candidateCount = 0;
	const auto addCandidate = [&](const char* candidate) {
		if (!candidate || !candidate[0] || candidateCount >= std::size(candidates))
			return;
		for (size_t i = 0; i < candidateCount; ++i) {
			if (!_stricmp(candidates[i], candidate))
				return;
		}
		candidates[candidateCount++] = candidate;
	};

	const char* cacheMarker = strstr(normalized, "\\r1delta_r1o_vpk_cache\\");
	if (cacheMarker) {
		_snprintf_s(cacheRelative, sizeof(cacheRelative), _TRUNCATE, "%s", cacheMarker + strlen("\\r1delta_r1o_vpk_cache\\"));
		addCandidate(cacheRelative);
	}

	if (normalized[0] && normalized[1] == ':') {
		if (BuildR1ODediRelativeModPath(normalized, rootRelative, sizeof(rootRelative)))
			addCandidate(rootRelative);
	}
	else {
		addCandidate(normalized);
	}

	const char* normalizedExtension = strrchr(normalized, '.');
	const char* normalizedSeparator = strrchr(normalized, '\\');
	if (!normalizedExtension
		|| (normalizedSeparator && normalizedExtension < normalizedSeparator)) {
		_snprintf_s(
			normalizedNutRelative,
			sizeof(normalizedNutRelative),
			_TRUNCATE,
			"%s.nut",
			normalized);
		addCandidate(normalizedNutRelative);
	}

	if (_strnicmp(normalized, "scripts\\vscripts\\", strlen("scripts\\vscripts\\")) != 0) {
		_snprintf_s(vscriptRelative, sizeof(vscriptRelative), _TRUNCATE, "scripts\\vscripts\\%s", normalized);
		addCandidate(vscriptRelative);
		if (!normalizedExtension
			|| (normalizedSeparator && normalizedExtension < normalizedSeparator)) {
			_snprintf_s(
				vscriptNutRelative,
				sizeof(vscriptNutRelative),
				_TRUNCATE,
				"scripts\\vscripts\\%s.nut",
				normalized);
			addCandidate(vscriptNutRelative);
		}
	}

	if (!_stricmp(normalized, "tfoplaylists.txt")) {
		_snprintf_s(playlistAliasRelative, sizeof(playlistAliasRelative), _TRUNCATE, "playlists.txt");
		addCandidate(playlistAliasRelative);
	}

	for (size_t i = 0; i < candidateCount; ++i) {
		const char* relative = candidates[i];
		if (!relative || !relative[0])
			continue;
		if (!_stricmp(relative, "playlists.txt")) {
			if (FileCache::GetInstance().ResolveReplacementFile(
					lease,
					relative,
					outPath,
					outPathSize,
					FileCache::ResolveOrder::AddonsFirst))
				return true;

			// Keep the loose mod file available during early startup even if the
			// addon cache could not publish a read lease.
			if (BuildR1ODediLooseModPath(relative, outPath, outPathSize))
				return true;
			continue;
		}
		if (FileCache::GetInstance().ResolveReplacementFile(lease, relative, outPath, outPathSize))
			return true;
	}

	return false;
}

static bool IsAllowedR1ODediLooseVScriptReplacement(const char* fileName)
{
	if (!fileName)
		return false;
	char normalized[MAX_PATH];
	_snprintf_s(normalized, sizeof(normalized), _TRUNCATE, "%s", fileName);
	for (char* it = normalized; *it; ++it) {
		if (*it == '\\')
			*it = '/';
		else
			*it = static_cast<char>(tolower(static_cast<unsigned char>(*it)));
	}
	const char* marker = strstr(normalized, "scripts/vscripts/");
	if (!marker)
		return true;
	return true;
}

static bool NormalizeR1ODediLooseFileName(
	const FileCache::ReadLease& lease,
	const char* fileName,
	char* outPath,
	size_t outPathSize)
{
	if (!IsAllowedR1ODediLooseVScriptReplacement(fileName))
		return false;

	if (TryResolveR1ODediLooseReplacementPath(lease, fileName, outPath, outPathSize))
		return true;

	if (!fileName || !outPath || !outPathSize)
		return false;

	const char* source = fileName;
	while ((source[0] == '/' || source[0] == '\\') && (source[1] == '/' || source[1] == '\\') && source[2] && source[3] == ':')
		++source;

	_snprintf_s(outPath, outPathSize, _TRUNCATE, "%s", source);
	for (char* it = outPath; *it; ++it) {
		if (*it == '/')
			*it = '\\';
	}

	if (outPath[0] && outPath[1] == ':') {
		char normalizedAbs[MAX_PATH];
		_snprintf_s(normalizedAbs, sizeof(normalizedAbs), _TRUNCATE, "%s", outPath);
		for (char* it = normalizedAbs; *it; ++it) {
			if (*it == '/')
				*it = '\\';
			else
				*it = static_cast<char>(tolower(static_cast<unsigned char>(*it)));
		}
		char* modRoot = strstr(normalizedAbs, "\\r1delta\\");
		if (modRoot) {
			const char* relative = modRoot + strlen("\\r1delta\\");
			return FileCache::GetInstance().ResolveReplacementFile(
				lease, relative, outPath, outPathSize);
		}
		const DWORD attributes = GetFileAttributesA(outPath);
		return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
	}

	return false;
}

static bool IsR1ODediLooseOverridePath(const char* fileName)
{
	if (!IsReadableCString(fileName))
		return false;
	return strstr(fileName, "cfg/") || strstr(fileName, "cfg\\")
		|| strstr(fileName, "resource/") || strstr(fileName, "resource\\")
		|| strstr(fileName, "scripts/") || strstr(fileName, "scripts\\");
}


static void* OpenR1OVPKMemoryFile(const char* fileName)
{
	char relativeModPath[MAX_PATH];
	if (BuildR1ODediRelativeModPath(fileName, relativeModPath, sizeof(relativeModPath))) {
		if (void* handle = R1OVPK_OpenFile(relativeModPath))
			return handle;
	}

	if (!fileName[0] || fileName[1] == ':' || fileName[0] == '/' || fileName[0] == '\\')
		return nullptr;
	return R1OVPK_OpenFile(fileName);
}

static bool GetR1OVPKMemoryFileSize(const char* fileName, uint64_t* size)
{
	char relativeModPath[MAX_PATH];
	if (BuildR1ODediRelativeModPath(fileName, relativeModPath, sizeof(relativeModPath))) {
		if (R1OVPK_GetFileSize(relativeModPath, size))
			return true;
	}

	if (!fileName[0] || fileName[1] == ':' || fileName[0] == '/' || fileName[0] == '\\')
		return false;
	return R1OVPK_GetFileSize(fileName, size);
}

static bool ShouldUseR1OVPKMemoryFileFallback()
{
	return s_R1OVPKMemoryFilesEnabled
		&& (IsR1ODedicatedServer() || s_R1OVPKClientFallbackDepth > 0);
}

static void* OpenR1OVPKFallback(
	R1OBaseFileSystem_OpenType originalOpen,
	void* thisptr,
	const char* fileName,
	const char* options,
	const char* pathId,
	int flags)
{
	if (!originalOpen)
		return nullptr;

	if (!ShouldUseR1OVPKMemoryFileFallback() || !IsReadOnlyFileOpen(options) || !IsReadableCString(fileName))
		return originalOpen(thisptr, fileName, options, pathId, flags);

	char loosePath[MAX_PATH];
	{
		auto lease = FileCache::GetInstance().AcquireReadLease();
		if (NormalizeR1ODediLooseFileName(lease, fileName, loosePath, sizeof(loosePath))) {
			void* looseHandle = originalOpen(thisptr, loosePath, options, nullptr, flags);
			if (AreR1OFakeDediVerboseLogsEnabled() && IsR1ODediLooseOverridePath(fileName)) {
				char buffer[512];
				_snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "R1Delta: R1O filesystem loose open override file=%s normalized=%s handle=%p\n", fileName, loosePath, looseHandle);
				OutputDebugStringA(buffer);
			}
			if (looseHandle)
				return looseHandle;
		}
	}

	if (void* handle = originalOpen(thisptr, fileName, options, pathId, flags)) {
		return handle;
	}
	void* handle = OpenR1OVPKMemoryFile(fileName);
	return handle;
}

static int __fastcall R1OBaseFileSystem_Read(void* thisptr, void* output, int bytesToRead, void* handle)
{
	int result = 0;
	if (R1OVPK_ReadFile(handle, output, bytesToRead, &result))
		return result;
	result = R1OBaseFileSystem_ReadOriginal
		? R1OBaseFileSystem_ReadOriginal(thisptr, output, bytesToRead, handle)
		: 0;
	return result;
}

static void* __fastcall R1OBaseFileSystem_Open(void* thisptr, const char* fileName, const char* options, const char* pathId, int flags)
{
	return OpenR1OVPKFallback(R1OBaseFileSystem_OpenOriginal, thisptr, fileName, options, pathId, flags);
}

static void __fastcall R1OBaseFileSystem_Close(void* thisptr, void* handle)
{
	if (R1OVPK_CloseFile(handle))
		return;
	if (R1OBaseFileSystem_CloseOriginal)
		R1OBaseFileSystem_CloseOriginal(thisptr, handle);
}

static void __fastcall R1OBaseFileSystem_Seek(void* thisptr, void* handle, int offset, int origin)
{
	if (R1OVPK_SeekFile(handle, offset, origin))
		return;
	if (R1OBaseFileSystem_SeekOriginal)
		R1OBaseFileSystem_SeekOriginal(thisptr, handle, offset, origin);
}

static unsigned int __fastcall R1OBaseFileSystem_Tell(void* thisptr, void* handle)
{
	uint64_t position = 0;
	if (R1OVPK_TellFile(handle, &position))
		return static_cast<unsigned int>(position);
	const unsigned int result = R1OBaseFileSystem_TellOriginal
		? R1OBaseFileSystem_TellOriginal(thisptr, handle)
		: 0;
	return result;
}

static unsigned int __fastcall R1OBaseFileSystem_Size(void* thisptr, void* handle)
{
	uint64_t size = 0;
	if (R1OVPK_SizeFile(handle, &size))
		return static_cast<unsigned int>(size);
	const unsigned int result = R1OBaseFileSystem_SizeOriginal
		? R1OBaseFileSystem_SizeOriginal(thisptr, handle)
		: 0;
	return result;
}

static unsigned int __fastcall R1OBaseFileSystem_SizeByName(void* thisptr, const char* fileName, const char* pathId)
{
	const unsigned int invalidSize = static_cast<unsigned int>(-1);
	if (!ShouldUseR1OVPKMemoryFileFallback() || !IsReadableCString(fileName)) {
		return R1OBaseFileSystem_SizeByNameOriginal
			? R1OBaseFileSystem_SizeByNameOriginal(thisptr, fileName, pathId)
			: invalidSize;
	}

	char loosePath[MAX_PATH];
	WIN32_FILE_ATTRIBUTE_DATA data;
	{
		auto lease = FileCache::GetInstance().AcquireReadLease();
		if (NormalizeR1ODediLooseFileName(lease, fileName, loosePath, sizeof(loosePath))
			&& GetFileAttributesExA(loosePath, GetFileExInfoStandard, &data)) {
			return data.nFileSizeHigh == 0 ? data.nFileSizeLow : invalidSize;
		}
	}

	const unsigned int nativeSize = R1OBaseFileSystem_SizeByNameOriginal
		? R1OBaseFileSystem_SizeByNameOriginal(thisptr, fileName, pathId)
		: invalidSize;
	if (nativeSize != invalidSize)
		return nativeSize;

	uint64_t size = 0;
	return GetR1OVPKMemoryFileSize(fileName, &size)
		? static_cast<unsigned int>(size)
		: nativeSize;
}

static bool __fastcall R1OBaseFileSystem_FileExists(void* thisptr, const char* fileName, const char* pathId)
{
	bool result = R1OBaseFileSystem_FileExistsOriginal ? R1OBaseFileSystem_FileExistsOriginal(thisptr, fileName, pathId) : false;
	if (result || !ShouldUseR1OVPKMemoryFileFallback() || !IsReadableCString(fileName))
		return result;

	char loosePath[MAX_PATH];
	{
		auto lease = FileCache::GetInstance().AcquireReadLease();
		result = NormalizeR1ODediLooseFileName(lease, fileName, loosePath, sizeof(loosePath));
		if (result && AreR1OFakeDediVerboseLogsEnabled() && IsR1ODediLooseOverridePath(fileName)) {
			char buffer[512];
			_snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "R1Delta: R1O filesystem loose exists fallback file=%s normalized=%s\n", fileName, loosePath);
			OutputDebugStringA(buffer);
		}
		if (result)
			return true;
	}

	uint64_t size = 0;
	return GetR1OVPKMemoryFileSize(fileName, &size);
}

static void* __fastcall R1OFileSystem_Open(void* thisptr, const char* fileName, const char* options, int flags, const char* pathId, __int64 unknown)
{
	if (!R1OFileSystem_OpenOriginal)
		return nullptr;

	if (!ShouldUseR1OVPKMemoryFileFallback() || !IsReadOnlyFileOpen(options) || !IsReadableCString(fileName))
		return R1OFileSystem_OpenOriginal(thisptr, fileName, options, flags, pathId, unknown);

	char loosePath[MAX_PATH];
	{
		auto lease = FileCache::GetInstance().AcquireReadLease();
		if (NormalizeR1ODediLooseFileName(lease, fileName, loosePath, sizeof(loosePath))) {
			void* looseHandle = R1OFileSystem_OpenOriginal(thisptr, loosePath, options, flags, nullptr, unknown);
			if (AreR1OFakeDediVerboseLogsEnabled() && IsR1ODediLooseOverridePath(fileName)) {
				char buffer[512];
				_snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "R1Delta: R1O CVFileSystem loose open override file=%s normalized=%s handle=%p\n", fileName, loosePath, looseHandle);
				OutputDebugStringA(buffer);
			}
			if (looseHandle)
				return looseHandle;
		}
	}

	if (void* handle = R1OFileSystem_OpenOriginal(thisptr, fileName, options, flags, pathId, unknown)) {
		return handle;
	}
	void* handle = OpenR1OVPKMemoryFile(fileName);
	return handle;
}

static int __fastcall R1OFileSystem_ReadEx(
	void* thisptr,
	void* output,
	int destinationSize,
	int bytesToRead,
	void* handle)
{
	int result = 0;
	const int requestSize = std::min(destinationSize, bytesToRead);
	if (R1OVPK_ReadFile(handle, output, requestSize, &result))
		return result;
	result = R1OFileSystem_ReadExOriginal
		? R1OFileSystem_ReadExOriginal(thisptr, output, destinationSize, bytesToRead, handle)
		: 0;
	return result;
}

static void __fastcall R1OFileSystem_SetBufferSize(void* thisptr, void* handle, unsigned int bytes)
{
	bool isOk = false;
	if (R1OVPK_IsFileOk(handle, &isOk))
		return;
	if (R1OFileSystem_SetBufferSizeOriginal)
		R1OFileSystem_SetBufferSizeOriginal(thisptr, handle, bytes);
}

static bool __fastcall R1OFileSystem_IsOk(void* thisptr, void* handle)
{
	bool result = false;
	if (R1OVPK_IsFileOk(handle, &result))
		return result;
	result = R1OFileSystem_IsOkOriginal
		? R1OFileSystem_IsOkOriginal(thisptr, handle)
		: false;
	return result;
}

static bool __fastcall R1OFileSystem_EndOfFile(void* thisptr, void* handle)
{
	bool result = true;
	if (R1OVPK_IsEndOfFile(handle, &result))
		return result;
	result = R1OFileSystem_EndOfFileOriginal
		? R1OFileSystem_EndOfFileOriginal(thisptr, handle)
		: true;
	return result;
}

static char* __fastcall R1OFileSystem_ReadLine(void* thisptr, char* output, int maxChars, void* handle)
{
	char* result = nullptr;
	if (R1OVPK_ReadLine(handle, output, maxChars, &result))
		return result;
	result = R1OFileSystem_ReadLineOriginal
		? R1OFileSystem_ReadLineOriginal(thisptr, output, maxChars, handle)
		: nullptr;
	return result;
}

static bool HookR1OTFOFileSystemMethod(void* target, void* detour, LPVOID* original)
{
	const MH_STATUS createStatus = MH_CreateHook(target, detour, original);
	if (createStatus != MH_OK && createStatus != MH_ERROR_ALREADY_CREATED)
		return false;
	if (!*original)
		return false;

	const MH_STATUS enableStatus = MH_EnableHook(target);
	return enableStatus == MH_OK || enableStatus == MH_ERROR_ENABLED;
}

static void HookR1OTFOBaseFileSystemOpen(void* baseFileSystem)
{
	if (s_R1OBaseFileSystemHooksInstalled || !baseFileSystem || !IsReadableRange(baseFileSystem, sizeof(void*)))
		return;

	auto vtable = *reinterpret_cast<void***>(baseFileSystem);
	if (!IsReadableRange(vtable, sizeof(void*) * 11))
		return;

	const bool read = HookR1OTFOFileSystemMethod(vtable[0], reinterpret_cast<void*>(&R1OBaseFileSystem_Read), reinterpret_cast<LPVOID*>(&R1OBaseFileSystem_ReadOriginal));
	const bool open = HookR1OTFOFileSystemMethod(vtable[2], reinterpret_cast<void*>(&R1OBaseFileSystem_Open), reinterpret_cast<LPVOID*>(&R1OBaseFileSystem_OpenOriginal));
	const bool close = HookR1OTFOFileSystemMethod(vtable[3], reinterpret_cast<void*>(&R1OBaseFileSystem_Close), reinterpret_cast<LPVOID*>(&R1OBaseFileSystem_CloseOriginal));
	const bool seek = HookR1OTFOFileSystemMethod(vtable[4], reinterpret_cast<void*>(&R1OBaseFileSystem_Seek), reinterpret_cast<LPVOID*>(&R1OBaseFileSystem_SeekOriginal));
	const bool tell = HookR1OTFOFileSystemMethod(vtable[5], reinterpret_cast<void*>(&R1OBaseFileSystem_Tell), reinterpret_cast<LPVOID*>(&R1OBaseFileSystem_TellOriginal));
	const bool size = HookR1OTFOFileSystemMethod(vtable[7], reinterpret_cast<void*>(&R1OBaseFileSystem_Size), reinterpret_cast<LPVOID*>(&R1OBaseFileSystem_SizeOriginal));
	const bool sizeByName = HookR1OTFOFileSystemMethod(vtable[6], reinterpret_cast<void*>(&R1OBaseFileSystem_SizeByName), reinterpret_cast<LPVOID*>(&R1OBaseFileSystem_SizeByNameOriginal));
	const bool exists = HookR1OTFOFileSystemMethod(vtable[10], reinterpret_cast<void*>(&R1OBaseFileSystem_FileExists), reinterpret_cast<LPVOID*>(&R1OBaseFileSystem_FileExistsOriginal));
	s_R1OBaseFileSystemHooksInstalled = read && open && close && seek && tell && size && sizeByName && exists;

	char buffer[512];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O TFO IBaseFileSystem memory VPK hooks basefs=%p vtable=%p read=%d open=%d close=%d seek=%d tell=%d size=%d/%p sizeByName=%d/%p exists=%d installed=%d\n",
		baseFileSystem,
		vtable,
		read ? 1 : 0,
		open ? 1 : 0,
		close ? 1 : 0,
		seek ? 1 : 0,
		tell ? 1 : 0,
		size ? 1 : 0,
		reinterpret_cast<void*>(R1OBaseFileSystem_SizeOriginal),
		sizeByName ? 1 : 0,
		reinterpret_cast<void*>(R1OBaseFileSystem_SizeByNameOriginal),
		exists ? 1 : 0,
		s_R1OBaseFileSystemHooksInstalled ? 1 : 0);
	OutputDebugStringA(buffer);
}

static void HookR1OTFOFileSystemOpen(void* fileSystem)
{
	if (s_R1OFileSystemHooksInstalled || !fileSystem || !IsReadableRange(fileSystem, sizeof(void*)))
		return;

	auto vtable = *reinterpret_cast<void***>(fileSystem);
	if (!IsReadableRange(vtable, sizeof(void*) * 78))
		return;

	const bool setBufferSize = HookR1OTFOFileSystemMethod(vtable[25], reinterpret_cast<void*>(&R1OFileSystem_SetBufferSize), reinterpret_cast<LPVOID*>(&R1OFileSystem_SetBufferSizeOriginal));
	const bool isOk = HookR1OTFOFileSystemMethod(vtable[26], reinterpret_cast<void*>(&R1OFileSystem_IsOk), reinterpret_cast<LPVOID*>(&R1OFileSystem_IsOkOriginal));
	const bool endOfFile = HookR1OTFOFileSystemMethod(vtable[27], reinterpret_cast<void*>(&R1OFileSystem_EndOfFile), reinterpret_cast<LPVOID*>(&R1OFileSystem_EndOfFileOriginal));
	const bool readLine = HookR1OTFOFileSystemMethod(vtable[28], reinterpret_cast<void*>(&R1OFileSystem_ReadLine), reinterpret_cast<LPVOID*>(&R1OFileSystem_ReadLineOriginal));
	const bool open = HookR1OTFOFileSystemMethod(vtable[76], reinterpret_cast<void*>(&R1OFileSystem_Open), reinterpret_cast<LPVOID*>(&R1OFileSystem_OpenOriginal));
	const bool readEx = HookR1OTFOFileSystemMethod(vtable[77], reinterpret_cast<void*>(&R1OFileSystem_ReadEx), reinterpret_cast<LPVOID*>(&R1OFileSystem_ReadExOriginal));
	s_R1OFileSystemHooksInstalled = setBufferSize && isOk && endOfFile && readLine && open && readEx;

	char buffer[384];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O TFO VFileSystem017 memory VPK hooks fs=%p vtable=%p setBufferSize=%d isOk=%d eof=%d readLine=%d open=%d readEx=%d installed=%d\n",
		fileSystem,
		vtable,
		setBufferSize ? 1 : 0,
		isOk ? 1 : 0,
		endOfFile ? 1 : 0,
		readLine ? 1 : 0,
		open ? 1 : 0,
		readEx ? 1 : 0,
		s_R1OFileSystemHooksInstalled ? 1 : 0);
	OutputDebugStringA(buffer);
}

void SetR1OVPKClientFallbackActive(bool active)
{
	if (active) {
		++s_R1OVPKClientFallbackDepth;
	} else if (s_R1OVPKClientFallbackDepth > 0) {
		--s_R1OVPKClientFallbackDepth;
	}
}

void InstallR1OVPKFileSystemHooks(void* fileSystem)
{
	if (!fileSystem)
		return;

	HookR1OTFOFileSystemOpen(fileSystem);
	HookR1OTFOBaseFileSystemOpen(reinterpret_cast<char*>(fileSystem) + sizeof(void*));
	s_R1OVPKMemoryFilesEnabled = s_R1OFileSystemHooksInstalled && s_R1OBaseFileSystemHooksInstalled;
}

static void* R1OTFOFileSystemInterface(const char* name, int* returnCode)
{
	CreateInterfaceFn factory = GetR1OTFOFileSystemFactory();
	if (!factory) {
		DebugR1ODediFactoryResult("tfo-filesystem-missing-factory", name, nullptr, returnCode);
		return nullptr;
	}

	void* result = factory(name, returnCode);
	if (result) {
		if (!strcmp_static(name, "VFileSystem017")) {
			s_R1OTFOFileSystem017 = result;
			InstallR1OVPKFileSystemHooks(result);
		}
		else if (!strcmp_static(name, "VBaseFileSystem012")) {
			HookR1OTFOBaseFileSystemOpen(result);
			s_R1OVPKMemoryFilesEnabled = s_R1OFileSystemHooksInstalled && s_R1OBaseFileSystemHooksInstalled;
		}
	}
	DebugR1ODediFactoryResult("tfo-filesystem", name, result, returnCode);
	return result;
}

static CreateInterfaceFn GetR1OTFOLauncherFactory()
{
	if (s_R1OTFOLauncherFactory)
		return s_R1OTFOLauncherFactory;

	const std::string path = r1delta::r1o::ResolveTFOModulePathA("launcher.dll");

	if (!path.empty())
		s_R1OTFOLauncher = LoadLibraryExA(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (s_R1OTFOLauncher)
		InstallR1OTFOSquirrelHooks(reinterpret_cast<uintptr_t>(s_R1OTFOLauncher));

	s_R1OTFOLauncherFactory = s_R1OTFOLauncher
		? reinterpret_cast<CreateInterfaceFn>(GetProcAddress(s_R1OTFOLauncher, "CreateInterface"))
		: nullptr;

	const std::string validationError = r1delta::r1o::TFORuntimeValidationErrorA();
	char buffer[1024];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O TFO launcher load path=%s module=%p factory=%p gle=%lu validation=%s\n",
		path.empty() ? "<unresolved>" : path.c_str(),
		s_R1OTFOLauncher,
		reinterpret_cast<void*>(s_R1OTFOLauncherFactory),
		GetLastError(),
		validationError.empty() ? "<ok>" : validationError.c_str());
	OutputDebugStringA(buffer);

	return s_R1OTFOLauncherFactory;
}

static void* R1OTFOLauncherInterface(const char* name, int* returnCode)
{
	CreateInterfaceFn factory = GetR1OTFOLauncherFactory();
	if (!factory) {
		DebugR1ODediFactoryResult("tfo-launcher-missing-factory", name, nullptr, returnCode);
		return nullptr;
	}

	void* result = factory(name, returnCode);
	EnsureR1OLauncherFileSystemGlobal();
	EnsureR1OLauncherScriptFatalHooks();
	DebugR1ODediFactoryResult("tfo-launcher", name, result, returnCode);
	return result;
}

static void* R1OTFOSupportModuleInterface(const char* name, int* returnCode)
{
	for (R1OTFOSupportModule& module : s_R1OTFOSupportModules) {
		if (!module.factory)
			LoadR1OTFOSupportModule(module.moduleName);
		if (!module.factory)
			continue;

		int localReturnCode = 0;
		void* result = module.factory(name, returnCode ? returnCode : &localReturnCode);
		if (!result)
			continue;

		char source[128];
		_snprintf_s(source, sizeof(source), _TRUNCATE, "tfo-support:%s", module.moduleName);
		DebugR1ODediFactoryResult(source, name, result, returnCode ? returnCode : &localReturnCode);
		return result;
	}

	DebugR1ODediFactoryResult("tfo-support", name, nullptr, returnCode);
	return nullptr;
}

static void* R1OTFOLocalizeInterfaceForEngine(int* returnCode)
{
	std::lock_guard<std::recursive_mutex> lock(s_R1OTFOLocalizeMutex);
	if (s_R1OTFOLocalizeConnected && s_R1OTFOLocalizeInterface) {
		if (returnCode)
			*returnCode = 0;
		return s_R1OTFOLocalizeInterface;
	}
	if (s_R1OTFOLocalizeConnecting && s_R1OTFOLocalizeInterface) {
		if (returnCode)
			*returnCode = 0;
		return s_R1OTFOLocalizeInterface;
	}

	const std::string path = r1delta::r1o::ResolveTFOModulePathA("localize.dll");
	if (path.empty()) {
		OutputDebugStringA("R1Delta: cannot resolve TFO localize.dll for engine_r1o\n");
		if (returnCode)
			*returnCode = 1;
		return nullptr;
	}

	s_R1OTFOLocalize = LoadLibraryExA(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!s_R1OTFOLocalize) {
		char buffer[1024];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: cannot load TFO localize.dll for engine_r1o path=%s gle=%lu\n",
			path.c_str(),
			GetLastError());
		OutputDebugStringA(buffer);
		if (returnCode)
			*returnCode = 1;
		return nullptr;
	}

	char loadedPath[MAX_PATH]{};
	GetModuleFileNameA(s_R1OTFOLocalize, loadedPath, sizeof(loadedPath));
	if (!r1delta::r1o::IsTFORuntimeModulePathA(loadedPath)) {
		char buffer[1024];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: TFO localize load resolved to the wrong module path=%s expected=%s\n",
			loadedPath[0] ? loadedPath : "<unknown>",
			path.c_str());
		OutputDebugStringA(buffer);
		if (returnCode)
			*returnCode = 1;
		return nullptr;
	}

	s_R1OTFOLocalizeFactory = reinterpret_cast<CreateInterfaceFn>(
		GetProcAddress(s_R1OTFOLocalize, "CreateInterface"));
	if (!s_R1OTFOLocalizeFactory) {
		OutputDebugStringA("R1Delta: TFO localize.dll has no CreateInterface export\n");
		if (returnCode)
			*returnCode = 1;
		return nullptr;
	}

	int localReturnCode = 0;
	s_R1OTFOLocalizeInterface = s_R1OTFOLocalizeFactory("Localize_001", &localReturnCode);
	if (!s_R1OTFOLocalizeInterface) {
		OutputDebugStringA("R1Delta: TFO localize.dll did not expose Localize_001\n");
		if (returnCode)
			*returnCode = localReturnCode ? localReturnCode : 1;
		return nullptr;
	}

	auto** vtable = *reinterpret_cast<void***>(s_R1OTFOLocalizeInterface);
	using ConnectFn = bool(__fastcall*)(void*, CreateInterfaceFn);
	s_R1OTFOLocalizeConnecting = true;
	const bool connected = vtable && vtable[0]
		&& reinterpret_cast<ConnectFn>(vtable[0])(s_R1OTFOLocalizeInterface, &R1OFactory);
	s_R1OTFOLocalizeConnecting = false;
	if (!connected) {
		OutputDebugStringA("R1Delta: TFO Localize_001 failed its private engine_r1o Connect\n");
		s_R1OTFOLocalizeInterface = nullptr;
		if (returnCode)
			*returnCode = 1;
		return nullptr;
	}

	s_R1OTFOLocalizeConnected = true;
	if (returnCode)
		*returnCode = 0;

	char buffer[1024];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: connected private TFO Localize_001 for engine_r1o path=%s object=%p; R1 VGUI keeps the stock R1 localizer\n",
		loadedPath,
		s_R1OTFOLocalizeInterface);
	OutputDebugStringA(buffer);
	return s_R1OTFOLocalizeInterface;
}

static void* R1OTFOInputSystemInterfaceForEngine(const char* name, int* returnCode)
{
	std::lock_guard<std::recursive_mutex> lock(s_R1OTFOInputSystemMutex);
	if (!name) {
		if (returnCode)
			*returnCode = 1;
		return nullptr;
	}

	if (!s_R1OTFOInputSystemFactory) {
		const std::string path = r1delta::r1o::ResolveTFOModulePathA("inputsystem.dll");
		if (path.empty()) {
			OutputDebugStringA("R1Delta: cannot resolve TFO inputsystem.dll for engine_r1o\n");
			if (returnCode)
				*returnCode = 1;
			return nullptr;
		}

		s_R1OTFOInputSystem = LoadLibraryExA(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
		if (!s_R1OTFOInputSystem) {
			char buffer[1024];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: cannot load TFO inputsystem.dll for engine_r1o path=%s gle=%lu\n",
				path.c_str(),
				GetLastError());
			OutputDebugStringA(buffer);
			if (returnCode)
				*returnCode = 1;
			return nullptr;
		}

		char loadedPath[MAX_PATH]{};
		GetModuleFileNameA(s_R1OTFOInputSystem, loadedPath, sizeof(loadedPath));
		if (!r1delta::r1o::IsTFORuntimeModulePathA(loadedPath)) {
			char buffer[1024];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: TFO inputsystem load resolved to the wrong module path=%s expected=%s\n",
				loadedPath[0] ? loadedPath : "<unknown>",
				path.c_str());
			OutputDebugStringA(buffer);
			if (returnCode)
				*returnCode = 1;
			return nullptr;
		}

		s_R1OTFOInputSystemFactory = reinterpret_cast<CreateInterfaceFn>(
			GetProcAddress(s_R1OTFOInputSystem, "CreateInterface"));
		if (!s_R1OTFOInputSystemFactory) {
			OutputDebugStringA("R1Delta: TFO inputsystem.dll has no CreateInterface export\n");
			if (returnCode)
				*returnCode = 1;
			return nullptr;
		}
	}

	if (!s_R1OTFOInputSystemInterface) {
		int localReturnCode = 0;
		s_R1OTFOInputSystemInterface =
			s_R1OTFOInputSystemFactory("InputSystemVersion001", &localReturnCode);
		if (!s_R1OTFOInputSystemInterface) {
			OutputDebugStringA("R1Delta: TFO inputsystem.dll did not expose InputSystemVersion001\n");
			if (returnCode)
				*returnCode = localReturnCode ? localReturnCode : 1;
			return nullptr;
		}
	}

	if (!s_R1OTFOInputSystemConnected && !s_R1OTFOInputSystemConnecting) {
		auto** vtable = *reinterpret_cast<void***>(s_R1OTFOInputSystemInterface);
		using ConnectFn = bool(__fastcall*)(void*, CreateInterfaceFn);
		s_R1OTFOInputSystemConnecting = true;
		const bool connected = vtable && vtable[0]
			&& reinterpret_cast<ConnectFn>(vtable[0])(s_R1OTFOInputSystemInterface, &R1OFactory);
		s_R1OTFOInputSystemConnecting = false;
		if (!connected) {
			OutputDebugStringA("R1Delta: TFO InputSystemVersion001 failed its private engine_r1o Connect\n");
			if (returnCode)
				*returnCode = 1;
			return nullptr;
		}
		s_R1OTFOInputSystemConnected = true;

		char loadedPath[MAX_PATH]{};
		GetModuleFileNameA(s_R1OTFOInputSystem, loadedPath, sizeof(loadedPath));
		char buffer[1024];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: connected private TFO InputSystemVersion001 for engine_r1o path=%s object=%p; R1 VGUI keeps the initialized stock R1 input system\n",
			loadedPath,
			s_R1OTFOInputSystemInterface);
		OutputDebugStringA(buffer);
	}

	int localReturnCode = 0;
	void* result = s_R1OTFOInputSystemFactory(
		name,
		returnCode ? returnCode : &localReturnCode);
	if (!result && returnCode && *returnCode == 0)
		*returnCode = 1;
	return result;
}

void* GetR1ONativeFileSystem()
{
	return IsR1ODedicatedServer() ? s_R1OTFOFileSystem017 : nullptr;
}

static int WSAAPI R1ORecvFromGuard(
	SOCKET socket,
	char* buffer,
	int length,
	int flags,
	sockaddr* from,
	int* fromLength)
{
	if (s_R1ORecvFromGuardDepth != 0 && ++s_R1ORecvFromCalls > 1000) {
		WSASetLastError(WSAEWOULDBLOCK);
		return SOCKET_ERROR;
	}

	if (!R1ORecvFromOriginal) {
		WSASetLastError(WSAEOPNOTSUPP);
		return SOCKET_ERROR;
	}

	return R1ORecvFromOriginal(socket, buffer, length, flags, from, fromLength);
}

static void* R1OQueryLoadedModuleFactories(const char* name, int* returnCode)
{
	static const char* kModules[] = {
		"materialsystem_nodx.dll",
		"materialsystem_dx11.dll",
		"studiorender.dll",
		"vphysics.dll",
		"datacache.dll",
		"vgui2.dll",
		"localize.dll",
		"filesystem_stdio.dll",
		"dedicated.dll",
	};

	for (const char* moduleName : kModules) {
		HMODULE module = GetModuleHandleA(moduleName);
		if (!module)
			continue;

		auto factory = reinterpret_cast<CreateInterfaceFn>(GetProcAddress(module, "CreateInterface"));
		if (!factory)
			continue;

		int localReturnCode = 0;
		void* result = factory(name, returnCode ? returnCode : &localReturnCode);
		if (!result)
			continue;

		char source[128];
		_snprintf_s(source, sizeof(source), _TRUNCATE, "loaded-module:%s", moduleName);
		DebugR1ODediFactoryResult(source, name, result, returnCode ? returnCode : &localReturnCode);
		return result;
	}

	DebugR1ODediFactoryResult("loaded-modules", name, nullptr, returnCode);
	return nullptr;
}

static void* ResolveR1OCVarBackingInterface(const char* name, int* returnCode)
{
	if (oAppSystemFactory) {
		int localReturnCode = 0;
		void* result = oAppSystemFactory(name, returnCode ? returnCode : &localReturnCode);
		DebugR1ODediFactoryResult("cvar-backing-appsystem", name, result, returnCode ? returnCode : &localReturnCode);
		if (result)
			return result;
	}

	HMODULE vstdlib = GetModuleHandleA("vstdlib.dll");
	if (vstdlib) {
		auto factory = reinterpret_cast<CreateInterfaceFn>(reinterpret_cast<uintptr_t>(vstdlib) + 0x023DD0);
		int localReturnCode = 0;
		void* result = factory(name, returnCode ? returnCode : &localReturnCode);
		DebugR1ODediFactoryResult("cvar-backing-vstdlib", name, result, returnCode ? returnCode : &localReturnCode);
		if (result)
			return result;
	}

	return nullptr;
}

static void* R1OWrappedCVarInterface(const char* name, int* returnCode)
{
	void* backingInterface = ResolveR1OCVarBackingInterface(name, returnCode);
	if (!backingInterface)
		return nullptr;

	cvarinterface = reinterpret_cast<uintptr_t>(backingInterface);
	uintptr_t* r1vtable = *reinterpret_cast<uintptr_t**>(cvarinterface);
	if (!r1vtable)
		return nullptr;

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
	OriginalCCvar__ProcessQueuedMaterialThreadConVarSets = reinterpret_cast<decltype(OriginalCCvar__ProcessQueuedMaterialThreadConVarSets)>(r1vtable[39]);
	OriginalCCvar__FactoryInternalIterator = reinterpret_cast<decltype(OriginalCCvar__FactoryInternalIterator)>(r1vtable[40]);

	static uintptr_t r1ovtable[43]{};
	static void* r1ovtablePtr = r1ovtable;
	static bool initialized = false;
	if (!initialized) {
		r1ovtable[0] = CreateFunction(reinterpret_cast<void*>(oCCvar__Connect), reinterpret_cast<void*>(cvarinterface));
		r1ovtable[1] = CreateFunction(reinterpret_cast<void*>(oCCvar__Disconnect), reinterpret_cast<void*>(cvarinterface));
		r1ovtable[2] = CreateFunction(reinterpret_cast<void*>(oCCvar__QueryInterface), reinterpret_cast<void*>(cvarinterface));
		r1ovtable[3] = CreateFunction(reinterpret_cast<void*>(oCCVar__Init), reinterpret_cast<void*>(cvarinterface));
		r1ovtable[4] = CreateFunction(reinterpret_cast<void*>(oCCVar__Shutdown), reinterpret_cast<void*>(cvarinterface));
		r1ovtable[5] = CreateFunction(reinterpret_cast<void*>(oCCvar__GetDependencies), reinterpret_cast<void*>(cvarinterface));
		r1ovtable[6] = CreateFunction(reinterpret_cast<void*>(oCCVar__GetTier), reinterpret_cast<void*>(cvarinterface));
		r1ovtable[7] = CreateFunction(reinterpret_cast<void*>(oCCVar__Reconnect), reinterpret_cast<void*>(cvarinterface));
		r1ovtable[8] = CreateFunction(reinterpret_cast<void*>(oCCvar__AllocateDLLIdentifier), reinterpret_cast<void*>(cvarinterface));
		r1ovtable[9] = CreateFunction(CCVar__SetSomeFlag_Corrupt, reinterpret_cast<void*>(cvarinterface));
		r1ovtable[10] = CreateFunction(CCVar__GetSomeFlag, reinterpret_cast<void*>(cvarinterface));
		r1ovtable[11] = CreateFunction(CCVar_RegisterConCommand, reinterpret_cast<void*>(cvarinterface));
		r1ovtable[12] = CreateFunction(CCVar_UnregisterConCommand, reinterpret_cast<void*>(cvarinterface));
		r1ovtable[13] = CreateFunction(reinterpret_cast<void*>(oCCvar__UnregisterConCommands), reinterpret_cast<void*>(cvarinterface));
		r1ovtable[14] = CreateFunction(reinterpret_cast<void*>(oCCvar__GetCommandLineValue), reinterpret_cast<void*>(cvarinterface));
		r1ovtable[15] = CreateFunction(CCVar_FindCommandBase, reinterpret_cast<void*>(cvarinterface));
		r1ovtable[16] = CreateFunction(CCVar_FindCommandBase2, reinterpret_cast<void*>(cvarinterface));
		r1ovtable[17] = CreateFunction(CCVar_FindVar, reinterpret_cast<void*>(cvarinterface));
		r1ovtable[18] = CreateFunction(CCVar_FindVar2, reinterpret_cast<void*>(cvarinterface));
		r1ovtable[19] = CreateFunction(CCVar_FindCommand, reinterpret_cast<void*>(cvarinterface));
		r1ovtable[20] = CreateFunction(CCVar_FindCommand2, reinterpret_cast<void*>(cvarinterface));
		r1ovtable[21] = CreateFunction(reinterpret_cast<void*>(oCCVar__Find), reinterpret_cast<void*>(cvarinterface));
		r1ovtable[22] = CreateFunction(CCvar__InstallGlobalChangeCallback, reinterpret_cast<void*>(cvarinterface));
		r1ovtable[23] = CreateFunction(CCvar__RemoveGlobalChangeCallback, reinterpret_cast<void*>(cvarinterface));
		r1ovtable[24] = CreateFunction(CCVar_CallGlobalChangeCallbacks, reinterpret_cast<void*>(cvarinterface));
		r1ovtable[25] = CreateFunction(reinterpret_cast<void*>(oCCvar__InstallConsoleDisplayFunc), reinterpret_cast<void*>(cvarinterface));
		r1ovtable[26] = CreateFunction(reinterpret_cast<void*>(oCCvar__RemoveConsoleDisplayFunc), reinterpret_cast<void*>(cvarinterface));
		r1ovtable[27] = CreateFunction(reinterpret_cast<void*>(oCCvar__ConsoleColorPrintf), reinterpret_cast<void*>(cvarinterface));
		r1ovtable[28] = CreateFunction(reinterpret_cast<void*>(oCCvar__ConsolePrintf), reinterpret_cast<void*>(cvarinterface));
		r1ovtable[29] = CreateFunction(reinterpret_cast<void*>(oCCvar__ConsoleDPrintf), reinterpret_cast<void*>(cvarinterface));
		r1ovtable[30] = CreateFunction(reinterpret_cast<void*>(oCCVar__RevertFlaggedConVars), reinterpret_cast<void*>(cvarinterface));
		r1ovtable[31] = CreateFunction(reinterpret_cast<void*>(oCCvar__InstallCVarQuery), reinterpret_cast<void*>(cvarinterface));
		r1ovtable[32] = CreateFunction(reinterpret_cast<void*>(oCCvar__SetMaxSplitScreenSlots), reinterpret_cast<void*>(cvarinterface));
		r1ovtable[33] = CreateFunction(reinterpret_cast<void*>(oCCvar__GetMaxSplitScreenSlots), reinterpret_cast<void*>(cvarinterface));
		r1ovtable[34] = CreateFunction(reinterpret_cast<void*>(oCCvar__GetConsoleDisplayFuncCount), reinterpret_cast<void*>(cvarinterface));
		r1ovtable[35] = CreateFunction(reinterpret_cast<void*>(oCCvar__GetConsoleText), reinterpret_cast<void*>(cvarinterface));
		r1ovtable[36] = CreateFunction(reinterpret_cast<void*>(oCCvar__IsMaterialThreadSetAllowed), reinterpret_cast<void*>(cvarinterface));
		r1ovtable[37] = CreateFunction(CCVar_QueueMaterialThreadSetValue1, reinterpret_cast<void*>(cvarinterface));
		r1ovtable[38] = CreateFunction(CCVar_QueueMaterialThreadSetValue2, reinterpret_cast<void*>(cvarinterface));
		r1ovtable[39] = CreateFunction(CCVar_QueueMaterialThreadSetValue3, reinterpret_cast<void*>(cvarinterface));
		r1ovtable[40] = CreateFunction(reinterpret_cast<void*>(oCCvar__HasQueuedMaterialThreadConVarSets), reinterpret_cast<void*>(cvarinterface));
		r1ovtable[41] = CreateFunction(CCvar__ProcessQueuedMaterialThreadConVarSets, reinterpret_cast<void*>(cvarinterface));
		r1ovtable[42] = CreateFunction(CCvar__FactoryInternalIterator, reinterpret_cast<void*>(cvarinterface));
		initialized = true;
	}

	DebugR1ODediFactoryResult("cvar-wrapper", name, &r1ovtablePtr, returnCode);
	if (IsR1ODedicatedServer())
		WriteR1OHookGlobalValue<void*>(0x22FB648, &r1ovtablePtr);
	RegisterR1OStaticConCommandsForDedi();
	return &r1ovtablePtr;
}

static void* EnsureR1OWrappedCVarInterfaceReady()
{
	if (!IsR1ODedicatedServer())
		return nullptr;

	int returnCode = 0;
	void* result = R1OWrappedCVarInterface("VEngineCvar007", &returnCode);
	if (result)
		return result;

	OutputDebugStringA("R1Delta: failed to resolve wrapped R1O VEngineCvar007 before Sys_Init\n");
	return nullptr;
}

void* __cdecl EngineR1OCreateInterface(const char* name, int* returnCode)
{
	void* result = R1OCreateInterfaceOriginal
		? R1OCreateInterfaceOriginal(name, returnCode)
		: nullptr;

	DebugR1ODediFactoryResult("engine_r1o_export", name, result, returnCode);

	if (IsR1ODedicatedServer()
		&& name
		&& (strstr(name, "VENGINE_") == name)
		&& result
		&& IsReadableRange(result, sizeof(void*))) {
		auto vtable = *reinterpret_cast<uintptr_t**>(result);
		if (IsReadableRange(vtable, sizeof(uintptr_t) * 12)) {
			char buffer[1024];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: R1O dedicated exports vtable object=%p vt=%p [0]=%p [1]=%p [2]=%p [3]=%p [4]=%p [5]=%p [6]=%p [7]=%p [8]=%p [9]=%p [10]=%p [11]=%p\n",
				result,
				vtable,
				reinterpret_cast<void*>(vtable[0]),
				reinterpret_cast<void*>(vtable[1]),
				reinterpret_cast<void*>(vtable[2]),
				reinterpret_cast<void*>(vtable[3]),
				reinterpret_cast<void*>(vtable[4]),
				reinterpret_cast<void*>(vtable[5]),
				reinterpret_cast<void*>(vtable[6]),
				reinterpret_cast<void*>(vtable[7]),
				reinterpret_cast<void*>(vtable[8]),
				reinterpret_cast<void*>(vtable[9]),
				reinterpret_cast<void*>(vtable[10]),
				reinterpret_cast<void*>(vtable[11]));
			OutputDebugStringA(buffer);
		}
	}

	return result;
}

char __fastcall CDedicatedServerAPI_Connect(__int64 thisptr, CreateInterfaceFn factory) {
	if (!IsR1ODedicatedServer())
		return CDedicatedServerAPI_ConnectOriginal(thisptr, factory);

	oAppSystemFactory = factory;
	oFileSystemFactory = factory;
	oPhysicsFactory = factory;

	engineR1O = (HMODULE)(G_engine_r1o ? G_engine_r1o : (uintptr_t)GetModuleHandleA("engine_r1o.dll"));
	if (!engineR1O)
		engineR1O = GetModuleHandleA("engine.dll");
	R1OCreateInterface = engineR1O
		? reinterpret_cast<CreateInterfaceFn>(GetProcAddress(engineR1O, "CreateInterface"))
		: nullptr;

	char buffer[256];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O CDedicatedServerAPI::Connect this=%p incomingFactory=%p engine=%p r1oCreateInterface=%p\n",
		reinterpret_cast<void*>(thisptr),
		reinterpret_cast<void*>(factory),
		engineR1O,
		reinterpret_cast<void*>(R1OCreateInterface));
	OutputDebugStringA(buffer);

	const char result = CDedicatedServerAPI_ConnectOriginal(thisptr, &R1OFactory);
	s_R1ODedicatedServerAPIConnected = result != 0;
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O CDedicatedServerAPI::Connect result=%d\n",
		static_cast<int>(result));
	OutputDebugStringA(buffer);
	return result;
}

int __fastcall CDedicatedServerAPI_Init(__int64 thisptr)
{
	if (!IsR1ODedicatedServer())
		return CDedicatedServerAPI_InitOriginal(thisptr);

	char buffer[192];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O CDedicatedServerAPI::Init this=%p\n",
		reinterpret_cast<void*>(thisptr));
	OutputDebugStringA(buffer);

	const int result = CDedicatedServerAPI_InitOriginal(thisptr);
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O CDedicatedServerAPI::Init result=%d\n",
		result);
	OutputDebugStringA(buffer);
	return result;
}

char __fastcall CDedicatedServerAPI_ModInit(__int64 thisptr, void* modInfo)
{
	if (!IsR1ODedicatedServer())
		return CDedicatedServerAPI_ModInitOriginal(thisptr, modInfo);

	if (!EnsureR1ODedicatedServerAPIConnected(thisptr))
		return 0;
	EnsureR1OClientOnlyGlobalsForDedi();

	if (!IsReadableRange(modInfo, sizeof(DedicatedServerModInfo2015))) {
		char buffer[256];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O ModInit got unreadable ModInfo pointer %p\n",
			modInfo);
		OutputDebugStringA(buffer);

		DedicatedServerModInfo2015 fallback{};
		fallback.m_pInstance = GetModuleHandleA(NULL);
		fallback.m_pBaseDirectory = GetR1ODediFallbackBaseDirectory();
		fallback.m_pInitialMod = "r1";
		fallback.m_pInitialGame = "r1";
		fallback.m_bTextMode = false;
		DebugR1ODediModInfo("fixed unreadable", &fallback);
		const char result = CDedicatedServerAPI_ModInitOriginal(thisptr, &fallback);
		char resultBuffer[128];
		_snprintf_s(resultBuffer, sizeof(resultBuffer), _TRUNCATE, "R1Delta: R1O CDedicatedServerAPI::ModInit result=%d\n", static_cast<int>(result));
		OutputDebugStringA(resultBuffer);
		return result;
	}

	DedicatedServerModInfo2015 fixed = *reinterpret_cast<DedicatedServerModInfo2015*>(modInfo);
	DebugR1ODediModInfo("incoming", &fixed);

	bool changed = false;
	if (!IsReadableCString(fixed.m_pBaseDirectory)) {
		fixed.m_pBaseDirectory = GetR1ODediFallbackBaseDirectory();
		changed = true;
	}
	if (!IsReadableCString(fixed.m_pInitialMod)) {
		fixed.m_pInitialMod = "r1";
		changed = true;
	}
	if (!IsReadableCString(fixed.m_pInitialGame)) {
		fixed.m_pInitialGame = "r1";
		changed = true;
	}

	if (changed) {
		DebugR1ODediModInfo("fixed", &fixed);
		const char result = CDedicatedServerAPI_ModInitOriginal(thisptr, &fixed);
		char buffer[128];
		_snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "R1Delta: R1O CDedicatedServerAPI::ModInit result=%d\n", static_cast<int>(result));
		OutputDebugStringA(buffer);
		return result;
	}

	const char result = CDedicatedServerAPI_ModInitOriginal(thisptr, modInfo);
	char buffer[128];
	_snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "R1Delta: R1O CDedicatedServerAPI::ModInit result=%d\n", static_cast<int>(result));
	OutputDebugStringA(buffer);
	return result;
}

__int64 __fastcall R1OFileSystem_LoadSearchPaths(__int64 params)
{
	if (!IsR1ODedicatedServer())
		return R1OFileSystem_LoadSearchPathsOriginal(params);

	const char* gameInfoPath = nullptr;
	const char* baseDirectory = nullptr;
	void* fileSystem = nullptr;
	if (IsReadableRange(reinterpret_cast<void*>(params), 0x28)) {
		gameInfoPath = *reinterpret_cast<const char**>(params);
		auto baseDirectoryField = reinterpret_cast<const char**>(params + 8);
		fileSystem = *reinterpret_cast<void**>(params + 24);
		baseDirectory = *baseDirectoryField;
		if (!baseDirectory) {
			*baseDirectoryField = GetR1ODediInstallBaseDirectory();
			baseDirectory = *baseDirectoryField;

			char buffer[384];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: filled null R1O FileSystem_LoadSearchPaths basedir with %s for params=%p gameinfo=%p\n",
				baseDirectory,
				reinterpret_cast<void*>(params),
				*reinterpret_cast<void**>(params));
			OutputDebugStringA(buffer);
		}

		char buffer[1024];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O FileSystem_LoadSearchPaths enter params=%p gameinfo=%p(%s) basedir=%p(%s) fs=%p writepath=%u\n",
			reinterpret_cast<void*>(params),
			gameInfoPath,
			IsReadableCString(gameInfoPath) ? gameInfoPath : "<invalid>",
			baseDirectory,
			IsReadableCString(baseDirectory) ? baseDirectory : "<invalid>",
			fileSystem,
			static_cast<unsigned int>(*reinterpret_cast<unsigned char*>(params + 32)));
		OutputDebugStringA(buffer);
	}

	if (!R1OFileSystem_LoadSearchPathsOriginal) {
		OutputDebugStringA("R1Delta: R1O FileSystem_LoadSearchPaths original trampoline is missing\n");
		return 3;
	}

	const __int64 result = R1OFileSystem_LoadSearchPathsOriginal(params);

	char resultBuffer[256];
	_snprintf_s(
		resultBuffer,
		sizeof(resultBuffer),
		_TRUNCATE,
		"R1Delta: R1O FileSystem_LoadSearchPaths params=%p result=%lld\n",
		reinterpret_cast<void*>(params),
		static_cast<long long>(result));
	OutputDebugStringA(resultBuffer);

	return result;
}

__int64 __fastcall R1OFileSystem_ParseGameInfo(
	void* fileSystem,
	const char* gameInfoPath,
	void* outGameInfo,
	void* outSearchPaths,
	void* outSearchPathRoot)
{
	if (!IsR1ODedicatedServer())
		return R1OFileSystem_ParseGameInfoOriginal(fileSystem, gameInfoPath, outGameInfo, outSearchPaths, outSearchPathRoot);

	char buffer[1400];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O ParseGameInfo enter fs=%p gameinfo=%p(%s) outGameInfo=%p outSearchPaths=%p outRoot=%p\n",
		fileSystem,
		gameInfoPath,
		IsReadableCString(gameInfoPath) ? gameInfoPath : "<invalid>",
		outGameInfo,
		outSearchPaths,
		outSearchPathRoot);
	OutputDebugStringA(buffer);

	const __int64 result = R1OFileSystem_ParseGameInfoOriginal
		? R1OFileSystem_ParseGameInfoOriginal(fileSystem, gameInfoPath, outGameInfo, outSearchPaths, outSearchPathRoot)
		: 3;

	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O ParseGameInfo result=%lld outGameInfoValue=%p outSearchPathsByte=%u outRootValue=%p\n",
		static_cast<long long>(result),
		IsReadableRange(outGameInfo, sizeof(void*)) ? *reinterpret_cast<void**>(outGameInfo) : nullptr,
		IsReadableRange(outSearchPaths, sizeof(unsigned char)) ? static_cast<unsigned int>(*reinterpret_cast<unsigned char*>(outSearchPaths)) : 0,
		IsReadableRange(outSearchPathRoot, sizeof(void*)) ? *reinterpret_cast<void**>(outSearchPathRoot) : nullptr);
	OutputDebugStringA(buffer);

	return result;
}

char* __fastcall R1OFileSystem_SetGameInfoName(void* fileSystem)
{
	if (!IsR1ODedicatedServer())
		return R1OFileSystem_SetGameInfoNameOriginal(fileSystem);

	char buffer[256];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O SetGameInfoName enter fs=%p\n",
		fileSystem);
	OutputDebugStringA(buffer);

	char* result = R1OFileSystem_SetGameInfoNameOriginal
		? R1OFileSystem_SetGameInfoNameOriginal(fileSystem)
		: nullptr;

	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O SetGameInfoName return result=%p(%s)\n",
		result,
		IsReadableCString(result) ? result : "<invalid>");
	OutputDebugStringA(buffer);
	return result;
}

__int64 __fastcall R1OFileSystem_LoadKeyValuesFile(void* fileSystem, const char* path)
{
	if (!IsR1ODedicatedServer())
		return R1OFileSystem_LoadKeyValuesFileOriginal(fileSystem, path);

	char buffer[1536];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O LoadKeyValuesFile enter fs=%p path=%p(%s)\n",
		fileSystem,
		path,
		IsReadableCString(path) ? path : "<invalid>");
	OutputDebugStringA(buffer);


	const __int64 result = R1OFileSystem_LoadKeyValuesFileOriginal
		? R1OFileSystem_LoadKeyValuesFileOriginal(fileSystem, path)
		: 0;

	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O LoadKeyValuesFile return result=%p\n",
		reinterpret_cast<void*>(result));
	OutputDebugStringA(buffer);
	return result;
}

void __fastcall R1OFileSystem_RefreshSearchPaths(void* fileSystem)
{
	if (IsR1ODedicatedServer())
		BeginAddonSearchCacheUpdate();

	if (R1OFileSystem_RefreshSearchPathsOriginal)
		R1OFileSystem_RefreshSearchPathsOriginal(fileSystem);

	if (IsR1ODedicatedServer() && EndAddonSearchCacheUpdate()
		&& AreR1OFakeDediVerboseLogsEnabled()) {
		OutputDebugStringA("R1Delta: refreshed native R1O addon search paths\n");
	}
}

unsigned char __fastcall R1OKeyValues_LoadFromFile(
	void* keyValues,
	void* fileSystem,
	const char* resourceName,
	const char* pathId,
	void* unknown)
{
	if (!IsR1ODedicatedServer())
		return R1OKeyValues_LoadFromFileOriginal(keyValues, fileSystem, resourceName, pathId, unknown);

	if (AreR1OFakeDediVerboseLogsEnabled()) {
		char buffer[1536];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O KeyValues_LoadFromFile kv=%p fs=%p resource=%p(%s) pathId=%p(%s)\n",
			keyValues,
			fileSystem,
			resourceName,
			IsReadableCString(resourceName) ? resourceName : "<invalid>",
			pathId,
			IsReadableCString(pathId) ? pathId : "<null>");
		OutputDebugStringA(buffer);
	}

	if (fileSystem && IsReadableCString(resourceName) && _stricmp(resourceName, "gameinfo.txt") == 0) {
		if (AreR1OFakeDediVerboseLogsEnabled())
			OutputDebugStringA("R1Delta: R1O KeyValues_LoadFromFile suppressed optional ModInfo gameinfo.txt probe for fake dedi\n");
		return 0;
	}

	if (fileSystem && IsReadableCString(resourceName)) {
		char loosePath[MAX_PATH];
		if (BuildR1ODediLooseModPath(resourceName, loosePath, sizeof(loosePath))) {
			if (AreR1OFakeDediVerboseLogsEnabled()) {
				char buffer[512];
				_snprintf_s(
					buffer,
					sizeof(buffer),
					_TRUNCATE,
					"R1Delta: R1O KeyValues_LoadFromFile using loose mod file %s\n",
					loosePath);
				OutputDebugStringA(buffer);
			}
			return R1OKeyValues_LoadFromFileOriginal
				? R1OKeyValues_LoadFromFileOriginal(keyValues, fileSystem, loosePath, nullptr, unknown)
				: 0;
		}
	}

	if (fileSystem && IsReadableCString(resourceName)) {
		char relativeModPath[MAX_PATH];
		if (BuildR1ODediRelativeModPath(resourceName, relativeModPath, sizeof(relativeModPath))) {
			if (AreR1OFakeDediVerboseLogsEnabled()) {
				char buffer[512];
				_snprintf_s(
					buffer,
					sizeof(buffer),
					_TRUNCATE,
					"R1Delta: R1O KeyValues_LoadFromFile retrying missing absolute mod file as GAME-relative %s\n",
					relativeModPath);
				OutputDebugStringA(buffer);
			}
			const unsigned char gameResult = R1OKeyValues_LoadFromFileOriginal
				? R1OKeyValues_LoadFromFileOriginal(keyValues, fileSystem, relativeModPath, "GAME", unknown)
				: 0;
			if (gameResult)
				return gameResult;

			return 0;
		}
	}


	return R1OKeyValues_LoadFromFileOriginal
		? R1OKeyValues_LoadFromFileOriginal(keyValues, fileSystem, resourceName, pathId, unknown)
		: 0;
}

__int64 __cdecl R1OFileSystemFatal(char showVConfig, unsigned int code, const char* fmt, ...)
{
	if (!IsR1ODedicatedServer() && R1OFileSystemFatalOriginal)
		return R1OFileSystemFatalOriginal(showVConfig, code, fmt);

	char message[512];
	va_list args;
	va_start(args, fmt);
	_vsnprintf_s(message, sizeof(message), _TRUNCATE, fmt ? fmt : "<null>", args);
	va_end(args);

	char buffer[768];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O filesystem fatal showVConfig=%d code=%u message=%s\n",
		static_cast<int>(showVConfig),
		code,
		message);
	OutputDebugStringA(buffer);

	if (R1OFileSystemFatalOriginal)
		return R1OFileSystemFatalOriginal(showVConfig, code, "%s", message);
	return code;
}

__int64 __fastcall R1OCOM_InitFilesystem(const char* initialMod)
{
	if (!IsR1ODedicatedServer())
		return R1OCOM_InitFilesystemOriginal(initialMod);

	char buffer[512];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O COM_InitFilesystem enter initialMod=%p(%s)\n",
		initialMod,
		IsReadableCString(initialMod) ? initialMod : "<invalid>");
	OutputDebugStringA(buffer);

	const __int64 result = R1OCOM_InitFilesystemOriginal
		? R1OCOM_InitFilesystemOriginal(initialMod)
		: 0;

	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O COM_InitFilesystem return result=%lld\n",
		static_cast<long long>(result));
	OutputDebugStringA(buffer);

	return result;
}

__int64 __fastcall R1OEngine_PostFilesystemInit()
{
	if (!IsR1ODedicatedServer())
		return R1OEngine_PostFilesystemInitOriginal();

	OutputDebugStringA("R1Delta: R1O post-filesystem init enter\n");
	const __int64 result = R1OEngine_PostFilesystemInitOriginal
		? R1OEngine_PostFilesystemInitOriginal()
		: 0;

	HMODULE module = engineR1O ? engineR1O : GetModuleHandleA("engine_r1o.dll");
	const uintptr_t base = reinterpret_cast<uintptr_t>(module);
	int aliasesBefore = -1;
	int aliasesAfter = -1;
	if (base) {
		int* const aliasCount = reinterpret_cast<int*>(base + 0x18836F4);
		if (IsReadableRange(aliasCount, sizeof(*aliasCount))) {
			aliasesBefore = *aliasCount;
			if (aliasesBefore == 0 && R1OLoadGameSoundManifest) {
				// The R1O client normally reaches the game-sound manifest loader
				// through its audio-cache initializer at +0x4A830. That wrapper
				// also allocates the client OGG cache (196608 KiB) even under
				// -nosound. A dedicated server only needs the manifest aliases,
				// matching the old engine_ds behavior, so call the manifest-only
				// function at +0x46230 instead.
				R1OLoadGameSoundManifest();
				aliasesAfter = *aliasCount;
			}
			else {
				aliasesAfter = aliasesBefore;
			}
		}
	}

	char buffer[192];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O post-filesystem init return result=%lld soundAliases=%d->%d\n",
		static_cast<long long>(result),
		aliasesBefore,
		aliasesAfter);
	OutputDebugStringA(buffer);

	if (aliasesBefore == 0 && aliasesAfter <= 0) {
		Warning(
			"R1Delta: R1O fake dedicated server failed to load sound aliases from "
			"scripts/game_sounds_manifest.txt; server-emitted scripted sounds will be missing\n");
	}

	return result;
}

char __fastcall R1OEngine_InitServerSystems(__int64 unknown)
{
	if (!IsR1ODedicatedServer())
		return R1OEngine_InitServerSystemsOriginal(unknown);

	char buffer[192];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O InitServerSystems enter arg=%p\n",
		reinterpret_cast<void*>(unknown));
	OutputDebugStringA(buffer);

	OutputDebugStringA("R1Delta: R1O InitServerSystems suppressed CGame::Init window/input setup for fake dedi\n");
	return 1;
}

void __cdecl R1OEngineFatal(const char* fmt, ...)
{
	char message[512];
	va_list args;
	va_start(args, fmt);
	_vsnprintf_s(message, sizeof(message), _TRUNCATE, fmt ? fmt : "<null>", args);
	va_end(args);

	char buffer[768];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O engine fatal message=%s\n",
		message);
	OutputDebugStringA(buffer);

	if (R1OEngineFatalOriginal)
		R1OEngineFatalOriginal("%s", message);
}

static void LogR1OAppSystemGroupState(const char* prefix, __int64 thisptr, __int64 result)
{
	char buffer[384];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O %s appSystemGroup=%p state=%d systems=%d factories=%d result=%lld\n",
		prefix ? prefix : "<unknown>",
		reinterpret_cast<void*>(thisptr),
		IsReadableRange(reinterpret_cast<void*>(thisptr + 160), sizeof(int)) ? *reinterpret_cast<int*>(thisptr + 160) : -999,
		IsReadableRange(reinterpret_cast<void*>(thisptr + 64), sizeof(int)) ? *reinterpret_cast<int*>(thisptr + 64) : -999,
		IsReadableRange(reinterpret_cast<void*>(thisptr + 96), sizeof(int)) ? *reinterpret_cast<int*>(thisptr + 96) : -999,
		static_cast<long long>(result));
	OutputDebugStringA(buffer);
}

template <typename T>
static T* R1OEngineGlobal(uintptr_t rva)
{
	const uintptr_t base = G_engine_r1o ? G_engine_r1o : reinterpret_cast<uintptr_t>(engineR1O);
	return base ? reinterpret_cast<T*>(base + rva) : nullptr;
}

static void ClearR1OLocalServerGlobals()
{
	if (auto global = R1OEngineGlobal<CreateInterfaceFn>(0x22FB200))
		*global = nullptr;
	if (auto global = R1OEngineGlobal<void*>(0x22FB340))
		*global = nullptr;
	if (auto global = R1OEngineGlobal<void*>(0x22FB0E8))
		*global = nullptr;
	if (auto global = R1OEngineGlobal<void*>(0x22FB218))
		*global = nullptr;
	if (auto global = R1OEngineGlobal<int>(0x22FAE8C))
		*global = 0;
	if (auto global = R1OEngineGlobal<void*>(0x22FB0F8))
		*global = nullptr;
	if (auto global = R1OEngineGlobal<HMODULE>(0x22FB210))
		*global = nullptr;
}

static void EnsureR1OClientOnlyGlobalsForDedi()
{
	if (!IsR1ODedicatedServer())
		return;

	if (auto multiplayer = R1OEngineGlobal<unsigned char>(0x22FB57A))
		*multiplayer = 1;
	if (auto listenSuppressed = R1OEngineGlobal<unsigned char>(0x6B908F))
		*listenSuppressed = 0;
	if (auto maxClients = R1OEngineGlobal<int>(0x265971C)) {
		if (*maxClients < 2)
			*maxClients = 16;
	}

	if (!s_R1ODedicatedNetworkModeLogged) {
		s_R1ODedicatedNetworkModeLogged = true;
		char buffer[384];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: forced R1O fake-dedi multiplayer network globals multiplayer=%d maxClients=%d socketIndex=%d listenSuppressed=%d\n",
			R1OHookGlobalValue<unsigned char>(0x22FB57A, static_cast<unsigned char>(0)),
			R1OHookGlobalValue<int>(0x265971C, -1),
			R1OHookGlobalValue<int>(0x265955C, -1),
			R1OHookGlobalValue<unsigned char>(0x6B908F, static_cast<unsigned char>(0)));
		OutputDebugStringA(buffer);
	}

	auto lobbySystem = R1OEngineGlobal<void*>(0x1ED9160);
	if (lobbySystem && !*lobbySystem) {
		*lobbySystem = GetR1ONullClientOnlyInterface();
		OutputDebugStringA("R1Delta: filled null R1O LobbySystem001 client-only global with no-op fake for dedicated mode\n");
	}
}

static void InstallR1OSetPreCacheHook()
{
	if (s_R1OSetPreCacheHookInstalled)
		return;

	if (!engineR1O)
		engineR1O = GetModuleHandleA("engine_r1o.dll");
	if (!engineR1O)
		return;

	void* target = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(engineR1O) + 0xF5790);
	if (!IsReadableRange(target, 16))
		return;

	const MH_STATUS status = MH_CreateHook(target, &SetPreCache, reinterpret_cast<LPVOID*>(&SetPreCache_o));
	if (status == MH_OK || status == MH_ERROR_ALREADY_CREATED) {
		s_R1OSetPreCacheHookInstalled = true;
		MH_EnableHook(target);
	}

	if (AreR1OFakeDediVerboseLogsEnabled()) {
		char buffer[256];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O SetPreCache hook target=%p status=%d original=%p\n",
			target,
			static_cast<int>(status),
			SetPreCache_o);
		OutputDebugStringA(buffer);
	}
}

static void QueueR1ODediRequestedDummyBot(int frameCount)
{
	if (!IsR1ODedicatedServer() || !R1OCbuf_AddTextOriginal)
		return;

	static bool s_dummyBotSpawnQueued = false;
	static bool s_dummyBotThrowQueued = false;
	static bool s_dummyBotSamplingDone = false;
	static bool s_dummyBotConfigParsed = false;
	static long s_dummyBotTeam = 0;
	static long s_dummyBotStartFrame = 120;
	static bool s_dummyBotWaitForFullNetworkClient = false;
	static int s_dummyBotSpawnFrame = 0;
	static int s_dummyBotThrowFrame = 0;
	static int s_dummyBotLastSampleFrame = 0;
	if (s_dummyBotSamplingDone)
		return;

	if (!s_dummyBotConfigParsed) {
		s_dummyBotConfigParsed = true;

		char teamToken[16] = {};
		if (!CopyCommandLineTokenValue("-r1o_dummy_bot_team", teamToken, sizeof(teamToken))
			&& !CopyCommandLineTokenValue("+r1o_dummy_bot_team", teamToken, sizeof(teamToken))) {
			s_dummyBotSamplingDone = true;
			return;
		}

		char* end = nullptr;
		s_dummyBotTeam = strtol(teamToken, &end, 10);
		if (!end || *end != '\0' || s_dummyBotTeam <= 0 || s_dummyBotTeam > 255) {
			char buffer[192];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: invalid -r1o_dummy_bot_team value=%s\n",
				teamToken);
			OutputDebugStringA(buffer);
			s_dummyBotSamplingDone = true;
			return;
		}

		char startFrameToken[16] = {};
		if (CopyCommandLineTokenValue("-r1o_dummy_bot_start_frame", startFrameToken, sizeof(startFrameToken))
			|| CopyCommandLineTokenValue("+r1o_dummy_bot_start_frame", startFrameToken, sizeof(startFrameToken))) {
			char* startFrameEnd = nullptr;
			const long parsedStartFrame = strtol(startFrameToken, &startFrameEnd, 10);
			if (startFrameEnd && *startFrameEnd == '\0' && parsedStartFrame >= 0 && parsedStartFrame <= 100000)
				s_dummyBotStartFrame = parsedStartFrame;
		}

		char waitClientToken[16] = {};
		if (CopyCommandLineTokenValue("-r1o_dummy_bot_wait_full_client", waitClientToken, sizeof(waitClientToken))
			|| CopyCommandLineTokenValue("+r1o_dummy_bot_wait_full_client", waitClientToken, sizeof(waitClientToken))) {
			s_dummyBotWaitForFullNetworkClient = waitClientToken[0] != '\0' && waitClientToken[0] != '0';
		}
	}

	const long team = s_dummyBotTeam;
	const long startFrame = s_dummyBotStartFrame;
	const bool waitForFullNetworkClient = s_dummyBotWaitForFullNetworkClient;

	if (frameCount < startFrame || !GetServerVMPtr())
		return;
	if (waitForFullNetworkClient && !HasR1OFullNetworkClient())
		return;

	if (!s_dummyBotSpawnQueued) {
		char command[96];
		const bool queuedFlag = QueueR1OCommandBufferText(1, "script R1Delta_SetDummyPilotMode()\n");
		_snprintf_s(command, sizeof(command), _TRUNCATE, "bot_dummy -team %ld\n", team);
		const bool queuedBot = QueueR1OCommandBufferText(1, command);
		if (queuedFlag && queuedBot) {
			s_dummyBotSpawnQueued = true;
			s_dummyBotSpawnFrame = frameCount;
		}

		if (AreR1OFakeDediVerboseLogsEnabled()) {
			char buffer[256];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: R1O dummy bot spawn request team=%ld frame=%d queuedFlag=%d queuedBot=%d\n",
				team,
				frameCount,
				queuedFlag ? 1 : 0,
				queuedBot ? 1 : 0);
			OutputDebugStringA(buffer);
		}
		return;
	}

	if (frameCount < s_dummyBotSpawnFrame + 240)
		return;

	if (!s_dummyBotThrowQueued) {
		const bool queuedThrow = QueueR1OCommandBufferText(1, "script R1Delta_ThrowFirstDummyFragNow()\n");
		if (queuedThrow) {
			s_dummyBotThrowQueued = true;
			s_dummyBotThrowFrame = frameCount;
		}

		if (AreR1OFakeDediVerboseLogsEnabled()) {
			char buffer[256];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: R1O dummy bot throw request team=%ld frame=%d spawnFrame=%d queuedThrow=%d\n",
				team,
				frameCount,
				s_dummyBotSpawnFrame,
				queuedThrow ? 1 : 0);
			OutputDebugStringA(buffer);
		}
		return;
	}

	if (frameCount < s_dummyBotThrowFrame + 2)
		return;

	// Let the script-side duration own the classification. This is only a
	// failsafe for a stopped VM or frozen Time(); 900 RunFrame calls can be less
	// than one simulated second in server-only fake-dedi.
	if (frameCount >= s_dummyBotThrowFrame + 36000) {
		QueueR1OCommandBufferText(1, "script R1Delta_DebugGrenadeTraceClassifierForceFinish()\n");
		s_dummyBotSamplingDone = true;
		if (AreR1OFakeDediVerboseLogsEnabled()) {
			char buffer[192];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: R1O dummy bot classifier stop frame=%d throwFrame=%d\n",
				frameCount,
				s_dummyBotThrowFrame);
			OutputDebugStringA(buffer);
		}
		return;
	}

	if (frameCount - s_dummyBotLastSampleFrame >= 2) {
		QueueR1OCommandBufferText(1, "script R1Delta_DebugGrenadeTraceClassifierSample()\n");
		s_dummyBotLastSampleFrame = frameCount;
	}
}

static char __fastcall R1OTFOServerGameDLL_DllInit(
	void* thisptr,
	CreateInterfaceFn appSystemFactory,
	CreateInterfaceFn physicsFactory,
	CreateInterfaceFn fileSystemFactory,
	void* pGlobals)
{
	if (!IsR1ODedicatedServer())
		return R1OTFOServerGameDLL_DllInitOriginal(thisptr, appSystemFactory, physicsFactory, fileSystemFactory, pGlobals);

	pGlobalVarsServer = reinterpret_cast<CGlobalVarsServer2015*>(pGlobals);

	char buffer[384];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O TFO ServerGameDLL::DLLInit this=%p incomingFactories=%p/%p/%p globals=%p; substituting R1OFactory\n",
		thisptr,
		reinterpret_cast<void*>(appSystemFactory),
		reinterpret_cast<void*>(physicsFactory),
		reinterpret_cast<void*>(fileSystemFactory),
		pGlobals);
	OutputDebugStringA(buffer);

	// Port the October 2024 non-R1O fake-dedi class precache fix: TFO class
	// records may carry Delta-only body/arms keys (armsmodel_imc,
	// bodymodel_militia, armsmodel_militia) that the stock R1O SetPreCache
	// ignores. The hook captures those keys before CHL2_Player::Precache builds
	// the model table used by mantle animations.
	InstallR1OSetPreCacheHook();

	const char result = R1OTFOServerGameDLL_DllInitOriginal(thisptr, &R1OFactory, &R1OFactory, &R1OFactory, pGlobals);
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O TFO ServerGameDLL::DLLInit result=%d\n",
		static_cast<int>(result));
	OutputDebugStringA(buffer);
	return result;
}

static void HookR1OTFOServerGameDLLInit(void* gameDll)
{
	if (s_R1OTFOServerGameDLL_DllInitHooked || !gameDll || !IsReadableRange(gameDll, sizeof(void*)))
		return;

	auto vtable = *reinterpret_cast<void***>(gameDll);
	if (!IsReadableRange(vtable, sizeof(void*)))
		return;

	void* target = vtable[0];
	const MH_STATUS status = MH_CreateHook(
		target,
		&R1OTFOServerGameDLL_DllInit,
		reinterpret_cast<LPVOID*>(&R1OTFOServerGameDLL_DllInitOriginal));
	if (status == MH_OK || status == MH_ERROR_ALREADY_CREATED) {
		s_R1OTFOServerGameDLL_DllInitHooked = true;
		MH_EnableHook(target);
	}

	char buffer[256];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O TFO ServerGameDLL::DLLInit hook target=%p status=%d original=%p\n",
		target,
		static_cast<int>(status),
		reinterpret_cast<void*>(R1OTFOServerGameDLL_DllInitOriginal));
		OutputDebugStringA(buffer);
}

static void __fastcall R1OTFOCBaseAnimatingSetSequence(__int64 thisptr, int sequence)
{
	if (!IsR1ODedicatedServer()) {
		R1OTFOCBaseAnimatingSetSequenceOriginal(thisptr, sequence);
		return;
	}

	// TFO's CBaseAnimating::SetSequence assumes GetModelPtr() returned a valid
	// CStudioHdr and immediately dereferences it. In fake-dedi the wallrun/ledge
	// animation helper can legitimately have no loaded studio header on the
	// server side. Do not fabricate a sequence or model; just fail the optional
	// animation update closed and let movement continue.
	const bool readableEntity = thisptr && IsReadableRange(reinterpret_cast<void*>(thisptr), 0xA20);
	if (!readableEntity || !R1OTFOCBaseAnimatingGetModelPtr) {
		R1OTFOCBaseAnimatingSetSequenceOriginal(thisptr, sequence);
		return;
	}

	const __int64 studioHdr = R1OTFOCBaseAnimatingGetModelPtr(thisptr);
	if (!studioHdr) {
		if (s_R1OTFOCBaseAnimatingNullStudioLogBudget-- > 0) {
			Warning(
				"R1Delta: R1O blocked crash: skipped TFO CBaseAnimating::SetSequence on readable entity without studio hdr ent=%p seq=%d\n",
				reinterpret_cast<void*>(thisptr),
				sequence);
		}
		return;
	}

	R1OTFOCBaseAnimatingSetSequenceOriginal(thisptr, sequence);
}

static void HookR1OTFOCBaseAnimatingSetSequence(HMODULE serverLocal)
{
	if (s_R1OTFOCBaseAnimatingSetSequenceHooked || !serverLocal || !IsR1ODedicatedServer())
		return;

	const uintptr_t base = reinterpret_cast<uintptr_t>(serverLocal);
	R1OTFOCBaseAnimatingGetModelPtr = reinterpret_cast<R1OTFOCBaseAnimatingGetModelPtrType>(base + 0x81F40);
	void* target = reinterpret_cast<void*>(base + 0x94BA0);
	if (!IsReadableRange(target, 16) || !IsReadableRange(reinterpret_cast<void*>(R1OTFOCBaseAnimatingGetModelPtr), 16))
		return;

	const MH_STATUS status = MH_CreateHook(
		target,
		&R1OTFOCBaseAnimatingSetSequence,
		reinterpret_cast<LPVOID*>(&R1OTFOCBaseAnimatingSetSequenceOriginal));
	if (status == MH_OK || status == MH_ERROR_ALREADY_CREATED) {
		s_R1OTFOCBaseAnimatingSetSequenceHooked = true;
		MH_EnableHook(target);
	}

	if (AreR1OFakeDediVerboseLogsEnabled()) {
		char buffer[256];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O TFO CBaseAnimating::SetSequence hook target=%p status=%d original=%p getModelPtr=%p\n",
			target,
			static_cast<int>(status),
			reinterpret_cast<void*>(R1OTFOCBaseAnimatingSetSequenceOriginal),
			reinterpret_cast<void*>(R1OTFOCBaseAnimatingGetModelPtr));
		OutputDebugStringA(buffer);
	}
}

static void __fastcall R1OCVar_SetDLLIdentifier(void* thisptr, HMODULE module)
{
	if (!IsR1ODedicatedServer()) {
		R1OCVar_SetDLLIdentifierOriginal(thisptr, module);
		return;
	}

	char buffer[192];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O cvar SetDLLIdentifier suppressed this=%p module=%p\n",
		thisptr,
		module);
	OutputDebugStringA(buffer);
}

static void HookR1OCVarSetDLLIdentifier()
{
	if (s_R1OCVar_SetDLLIdentifierHooked)
		return;

	auto cvarGlobal = R1OEngineGlobal<void*>(0x22FB648);
	void* cvar = cvarGlobal ? *cvarGlobal : nullptr;
	if (!cvar || !IsReadableRange(cvar, sizeof(void*)))
		return;

	auto vtable = *reinterpret_cast<void***>(cvar);
	if (!IsReadableRange(vtable, sizeof(void*) * 10))
		return;

	void* target = vtable[9];
	const MH_STATUS status = MH_CreateHook(
		target,
		&R1OCVar_SetDLLIdentifier,
		reinterpret_cast<LPVOID*>(&R1OCVar_SetDLLIdentifierOriginal));
	if (status == MH_OK || status == MH_ERROR_ALREADY_CREATED) {
		s_R1OCVar_SetDLLIdentifierHooked = true;
		MH_EnableHook(target);
	}

	char buffer[256];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O cvar SetDLLIdentifier hook target=%p status=%d original=%p cvar=%p\n",
		target,
		static_cast<int>(status),
		reinterpret_cast<void*>(R1OCVar_SetDLLIdentifierOriginal),
		cvar);
	OutputDebugStringA(buffer);
}

__int64 __fastcall R1OAppSystemGroupConstructor(__int64 thisptr, __int64 parent)
{
	if (!IsR1ODedicatedServer())
		return R1OAppSystemGroupConstructorOriginal(thisptr, parent);

	char buffer[256];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O CModAppSystemGroup ctor enter this=%p parent=%p\n",
		reinterpret_cast<void*>(thisptr),
		reinterpret_cast<void*>(parent));
	OutputDebugStringA(buffer);

	const __int64 result = R1OAppSystemGroupConstructorOriginal
		? R1OAppSystemGroupConstructorOriginal(thisptr, parent)
		: thisptr;
	LogR1OAppSystemGroupState("CModAppSystemGroup ctor return", thisptr, result);
	return result;
}

char __fastcall R1OLoadServerLocalGameDLL(const char* dllName)
{
	if (!IsR1ODedicatedServer())
		return R1OLoadServerLocalGameDLLOriginal(dllName);

	char buffer[768];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O LoadServerLocalGameDLL enter dll=%p(%s)\n",
		dllName,
		IsReadableCString(dllName) ? dllName : "<invalid>");
	OutputDebugStringA(buffer);

	const std::string path = r1delta::r1o::ResolveTFOModulePathA("server_local.dll");

	HMODULE serverLocal = path.empty()
		? nullptr
		: LoadLibraryExA(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);

	auto factory = serverLocal
		? reinterpret_cast<CreateInterfaceFn>(GetProcAddress(serverLocal, "CreateInterface"))
		: nullptr;

	void* gameDll = factory ? factory("ServerGameDLL005", nullptr) : nullptr;
	void* gameEnts = factory ? factory("ServerGameEnts002", nullptr) : nullptr;
	void* gameClients = factory ? factory("ServerGameClients004", nullptr) : nullptr;
	int clientsVersion = 4;
	if (!gameClients) {
		gameClients = factory ? factory("ServerGameClients003", nullptr) : nullptr;
		clientsVersion = 3;
	}
	void* gameTags = factory ? factory("ServerGameTags001", nullptr) : nullptr;

	void* localGameDll = factory ? factory("LocalServerGameDLL005", nullptr) : nullptr;
	void* localGameEnts = factory ? factory("LocalServerGameEnts002", nullptr) : nullptr;
	void* localGameClients = factory ? factory("LocalServerGameClients004", nullptr) : nullptr;
	int localClientsVersion = 4;
	if (!localGameClients) {
		localGameClients = factory ? factory("LocalServerGameClients003", nullptr) : nullptr;
		localClientsVersion = 3;
	}
	void* localGameTags = factory ? factory("LocalServerGameTags001", nullptr) : nullptr;

	const std::string validationError = r1delta::r1o::TFORuntimeValidationErrorA();
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O TFO server load path=%s module=%p factory=%p gameDll=%p gameEnts=%p gameClients=%p clientVersion=%d gameTags=%p localDll=%p localEnts=%p localClients=%p localClientVersion=%d localTags=%p gle=%lu validation=%s\n",
		path.empty() ? "<unresolved>" : path.c_str(),
		serverLocal,
		reinterpret_cast<void*>(factory),
		gameDll,
		gameEnts,
		gameClients,
		clientsVersion,
		gameTags,
		localGameDll,
		localGameEnts,
		localGameClients,
		localClientsVersion,
		localGameTags,
		GetLastError(),
		validationError.empty() ? "<ok>" : validationError.c_str());
	OutputDebugStringA(buffer);

	if (!serverLocal || !factory || !gameDll || !gameEnts || !gameClients)
		return 0;

	HookR1OTFOServerGameDLLInit(gameDll);
	HookR1OTFOCBaseAnimatingSetSequence(serverLocal);

	// CModAppSystemGroup's early startup path is wired around the LocalServer*
	// globals even for the R1O client engine. Keep them populated until the
	// actual game-DLL init hook below, where fake-dedi switches to ServerGame*
	// only to avoid the unsafe LocalServerGameDLL::DLLInit ABI path.
	if (auto global = R1OEngineGlobal<CreateInterfaceFn>(0x22FB200))
		*global = factory;
	if (auto global = R1OEngineGlobal<void*>(0x22FB340))
		*global = localGameDll ? localGameDll : gameDll;
	if (auto global = R1OEngineGlobal<void*>(0x22FB0E8))
		*global = localGameEnts ? localGameEnts : gameEnts;
	if (auto global = R1OEngineGlobal<void*>(0x22FB218))
		*global = localGameClients ? localGameClients : gameClients;
	if (auto global = R1OEngineGlobal<int>(0x22FAE8C))
		*global = localGameClients ? localClientsVersion : clientsVersion;
	if (auto global = R1OEngineGlobal<void*>(0x22FB0F8))
		*global = localGameTags ? localGameTags : gameTags;
	if (auto global = R1OEngineGlobal<HMODULE>(0x22FB210))
		*global = serverLocal;
	if (auto global = R1OEngineGlobal<unsigned char>(0x22FB0DD))
		*global = 1;

	if (auto global = R1OEngineGlobal<CreateInterfaceFn>(0x22FB230))
		*global = factory;
	if (auto global = R1OEngineGlobal<unsigned char>(0x22FAE8B))
		*global = 1;
	if (auto global = R1OEngineGlobal<void*>(0x22FB228))
		*global = gameDll;
	if (auto global = R1OEngineGlobal<void*>(0x22FAEA8))
		*global = gameEnts;
	if (auto global = R1OEngineGlobal<void*>(0x22FB348))
		*global = gameClients;
	if (auto global = R1OEngineGlobal<int>(0x22FAEA0))
		*global = clientsVersion;
	if (auto global = R1OEngineGlobal<int>(0x22FAEA4))
		*global = clientsVersion;
	if (auto global = R1OEngineGlobal<void*>(0x22FB0E0))
		*global = gameTags;
	if (auto global = R1OEngineGlobal<HMODULE>(0x22FB220))
		*global = serverLocal;

	// The non-dedicated R1O engine normally only aliases ServerGame* into the
	// active globals under the -tools/listen-server path. Fake-dedi follows the
	// server path explicitly, so install the aliases here and avoid the LocalServer
	// globals that would run the wrong DLLInit path.
	if (auto global = R1OEngineGlobal<void*>(0x22FB0F0))
		*global = gameDll;
	if (auto global = R1OEngineGlobal<void*>(0x22FB350))
		*global = gameEnts;
	if (auto global = R1OEngineGlobal<void*>(0x22FB238))
		*global = gameClients;
	if (auto global = R1OEngineGlobal<void*>(0x22FB368))
		*global = gameTags;
	if (auto global = R1OEngineGlobal<CreateInterfaceFn>(0x22FB358))
		*global = factory;
	if (auto global = R1OEngineGlobal<void*>(0x22EF500))
		*global = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(engineR1O) + 0x6B9768);

	return 1;
}

__int64 __fastcall R1OInitializeGameDLLs()
{
	if (!IsR1ODedicatedServer())
		return R1OInitializeGameDLLsOriginal();

	char buffer[384];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O InitializeGameDLLs enter localDll=%p multiDll=%p localModule=%p multiModule=%p\n",
		R1OEngineGlobal<void*>(0x22FB340) ? *R1OEngineGlobal<void*>(0x22FB340) : nullptr,
		R1OEngineGlobal<void*>(0x22FB228) ? *R1OEngineGlobal<void*>(0x22FB228) : nullptr,
		R1OEngineGlobal<HMODULE>(0x22FB210) ? *R1OEngineGlobal<HMODULE>(0x22FB210) : nullptr,
		R1OEngineGlobal<HMODULE>(0x22FB220) ? *R1OEngineGlobal<HMODULE>(0x22FB220) : nullptr);
	OutputDebugStringA(buffer);

	ClearR1OLocalServerGlobals();
	HookR1OCVarSetDLLIdentifier();

	const __int64 result = R1OInitializeGameDLLsOriginal
		? R1OInitializeGameDLLsOriginal()
		: 0;

	ClearR1OLocalServerGlobals();

	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O InitializeGameDLLs return result=%lld localDll=%p multiDll=%p\n",
		static_cast<long long>(result),
		R1OEngineGlobal<void*>(0x22FB340) ? *R1OEngineGlobal<void*>(0x22FB340) : nullptr,
		R1OEngineGlobal<void*>(0x22FB228) ? *R1OEngineGlobal<void*>(0x22FB228) : nullptr);
	OutputDebugStringA(buffer);

	QueueR1ODediStartupCommands();

	return result;
}

__int64 __fastcall R1OAppSystemGroupStartup(__int64 thisptr)
{
	if (!IsR1ODedicatedServer())
		return R1OAppSystemGroupStartupOriginal(thisptr);

	LogR1OAppSystemGroupState("AppSystem startup enter", thisptr, 0);
	const __int64 result = R1OAppSystemGroupStartupOriginal
		? R1OAppSystemGroupStartupOriginal(thisptr)
		: 0;
	LogR1OAppSystemGroupState("AppSystem startup return", thisptr, result);
	return result;
}

__int64 __fastcall R1OAppSystemGroupAddSystems(__int64 thisptr)
{
	if (!IsR1ODedicatedServer())
		return R1OAppSystemGroupAddSystemsOriginal(thisptr);

	LogR1OAppSystemGroupState("AppSystem add-systems enter", thisptr, 0);
	const __int64 result = R1OAppSystemGroupAddSystemsOriginal
		? R1OAppSystemGroupAddSystemsOriginal(thisptr)
		: 0;
	LogR1OAppSystemGroupState("AppSystem add-systems return", thisptr, result);
	return result;
}

__int64 __fastcall R1OAppSystemGroupInitSystems(__int64 thisptr)
{
	if (!IsR1ODedicatedServer())
		return R1OAppSystemGroupInitSystemsOriginal(thisptr);

	LogR1OAppSystemGroupState("AppSystem init-systems enter", thisptr, 0);
	const __int64 result = R1OAppSystemGroupInitSystemsOriginal
		? R1OAppSystemGroupInitSystemsOriginal(thisptr)
		: 0;
	LogR1OAppSystemGroupState("AppSystem init-systems return", thisptr, result);
	return result;
}

__int64 __fastcall R1OAppSystemGroupRun(__int64 thisptr)
{
	if (!IsR1ODedicatedServer())
		return R1OAppSystemGroupRunOriginal(thisptr);

	EnsureR1OClientOnlyGlobalsForDedi();
	LogR1OAppSystemGroupState("AppSystem run enter", thisptr, 0);
	const __int64 result = R1OAppSystemGroupRunOriginal
		? R1OAppSystemGroupRunOriginal(thisptr)
		: 0xFFFFFFFFLL;
	LogR1OAppSystemGroupState("AppSystem run return", thisptr, result);
	return result;
}

void InitR1ODedicatedServerAPIHook(uintptr_t engineR1OBase) {
	if (!IsR1ODedicatedServer() || !engineR1OBase || s_R1ODedicatedServerAPIHooked)
		return;

	R1OLoadGameSoundManifest =
		reinterpret_cast<R1OLoadGameSoundManifestType>(engineR1OBase + 0x46230);

	if (HasEngineCommandLineFlag("-r1o_netprop_compare")) {
		s_R1OSendTableEncodeLogBudget = 128;
		s_R1OSendTableEncodeHeaderLogBudget = 64;
		s_R1ODeltaPropIndexWriteLogBudget = 512;
		s_R1ODeltaPropIndexWritePlayerWindowBudget = 256;
		s_R1OEncodePropLogBudget = 128;
		s_R1OCellEncodeTraceLogBudget = 128;
		s_R1OWritePropListPlayerLogBudget = 128;
		s_R1ODeltaCalculatorPlayerLogBudget = 256;
		s_R1OPropCullPlayerLogBudget = 128;
		s_R1OPlayerNetpropCompareLogBudget = 24;
	}

	engineR1O = (HMODULE)engineR1OBase;
	InstallR1OIdleFrameSleepCompatibility();
	InstallR1OConsoleLoggingHooks(engineR1OBase);
	InstallR1OSetPreCacheHook();
	PatchR1ORenderOnlyDedicatedHelpers(engineR1OBase);
	PatchR1OStaticPropRenderInventory(engineR1OBase);
	PatchR1OConnectPlatformValidation(engineR1OBase);
	PatchR1OConnectUidValidation(engineR1OBase);
	PatchR1ONetChanPacketEndHandlerGuard(engineR1OBase);
	PatchR1OReconnectClientsInvalidSignonState(engineR1OBase);
	PatchR1OMapChangeStudioRenderTeardown(engineR1OBase);
	PatchR1OCoordinateEncodingForR1Client(engineR1OBase);
	{
		const unsigned char expected[] = { 0x0F, 0x84, 0xAA, 0x00, 0x00, 0x00 };
		const unsigned char replacement[] = { 0xE9, 0xAB, 0x00, 0x00, 0x00, 0x90 };
		PatchR1OBytesIfMatch(
			engineR1OBase,
			0x14467B,
			expected,
			replacement,
			sizeof(expected),
			"fake-dedi route client string commands only through ServerGameDLL ClientCommand");
	}
	{
		// TFO's map-list provider searches a loose maps/ directory. R1 fake-dedi
		// content is mounted as VPK BSP entries, matching the old engine_ds
		// dedicated-server compatibility patches.
		const unsigned char expected[] = {
			'm', 'a', 'p', 's', '/', '*', '.', 'b', 's', 'p', '\0', '\0'
		};
		const unsigned char replacement[] = {
			'v', 'p', 'k', '/', 'e', '*', '.', 'b', 's', 'p', '*', '\0'
		};
		PatchR1OBytesIfMatch(
			engineR1OBase,
			0x5618F8,
			expected,
			replacement,
			sizeof(expected),
			"fake-dedi enumerate BSPs from mounted VPK entries");
	}
	{
		const unsigned char expected[] = {
			'm', 'a', 'p', 's', '/', '%', 's', '\0'
		};
		const unsigned char replacement[] = {
			'v', 'p', 'k', '/', '/', '%', 's', '\0'
		};
		PatchR1OBytesIfMatch(
			engineR1OBase,
			0x561840,
			expected,
			replacement,
			sizeof(expected),
			"fake-dedi format mounted VPK BSP paths");
	}
	{
		const unsigned char expected[] = { 0x74, 0x5D };
		const unsigned char replacement[] = { 0x90, 0x90 };
		PatchR1OBytesIfMatch(
			engineR1OBase,
			0x15BA2A,
			expected,
			replacement,
			sizeof(expected),
			"fake-dedi accept mounted VPK BSP map-list entries");
	}
	{
		const unsigned char expected[] = { 0xA0 };
		const unsigned char replacement[] = { 0xAE };
		PatchR1OBytesIfMatch(
			engineR1OBase,
			0x15BA6E,
			expected,
			replacement,
			sizeof(expected),
			"fake-dedi strip mounted VPK prefix from map-list names");
	}
	{
		// TFO's ExecGameTypeCfg reads members/numSlots, an unrelated
		// matchmaking property which is absent from R1 playlists. Preserve the
		// existing fake-dedi behavior of suppressing that invalid max-client
		// override; Playlist_SetPlaylist applies the compatible vars/max players
		// value through R1OGetPlaylistMaxPlayers instead.
		const unsigned char expected[] = { 0x40 };
		const unsigned char replacement[] = { 0xC3 };
		PatchR1OBytesIfMatch(
			engineR1OBase,
			0x153260,
			expected,
			replacement,
			sizeof(expected),
			"fake-dedi suppress unrelated members/numSlots max-client override");
	}
	InstallR1OUpdateMapHook(engineR1OBase);
	EnsureR1ODedicatedWorldModelFallbacks();
	EnsureR1ODedicatedClientDllModelFallback();
	EnsureR1ODataCacheFileSystemGlobal();
	EnsureR1ODataCachePhysicsSurfacePropsGlobal();
	EnsureR1ODataCachePhysicsCollisionGlobal();
	EnsureR1ODataCacheStudioRenderGlobal();
	EnsureR1OLauncherFileSystemGlobal();
	EnsureR1OLauncherScriptFatalHooks();
	EnsureR1OStudioRenderStudioDataCacheGlobal();
	EnsureR1OStudioRenderMaterialSystemGlobal();
	EnsureR1OStudioRenderMaterialSystemHardwareConfigGlobal();

	R1OCreateInterface = reinterpret_cast<CreateInterfaceFn>(GetProcAddress(engineR1O, "CreateInterface"));
	MH_STATUS createInterfaceStatus = R1OCreateInterface
		? MH_CreateHook(
			reinterpret_cast<LPVOID>(R1OCreateInterface),
			&EngineR1OCreateInterface,
			reinterpret_cast<LPVOID*>(&R1OCreateInterfaceOriginal))
		: MH_ERROR_FUNCTION_NOT_FOUND;
	MH_STATUS connectStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x1C6B30),
		&CDedicatedServerAPI_Connect,
		reinterpret_cast<LPVOID*>(&CDedicatedServerAPI_ConnectOriginal));
	MH_STATUS initStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x1C74C0),
		&CDedicatedServerAPI_Init,
		reinterpret_cast<LPVOID*>(&CDedicatedServerAPI_InitOriginal));
	MH_STATUS modInitStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x1C6CF0),
		&CDedicatedServerAPI_ModInit,
		reinterpret_cast<LPVOID*>(&CDedicatedServerAPI_ModInitOriginal));
	s_R1OFileSystem_LoadSearchPathsTarget = engineR1OBase + 0x17A240;
	MH_STATUS loadSearchPathsStatus = MH_CreateHook(
		(LPVOID)s_R1OFileSystem_LoadSearchPathsTarget,
		&R1OFileSystem_LoadSearchPaths,
		reinterpret_cast<LPVOID*>(&R1OFileSystem_LoadSearchPathsOriginal));
	MH_STATUS parseGameInfoStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x179CE0),
		&R1OFileSystem_ParseGameInfo,
		reinterpret_cast<LPVOID*>(&R1OFileSystem_ParseGameInfoOriginal));
	MH_STATUS setGameInfoNameStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x179C50),
		&R1OFileSystem_SetGameInfoName,
		reinterpret_cast<LPVOID*>(&R1OFileSystem_SetGameInfoNameOriginal));
	MH_STATUS loadKeyValuesFileStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x1797C0),
		&R1OFileSystem_LoadKeyValuesFile,
		reinterpret_cast<LPVOID*>(&R1OFileSystem_LoadKeyValuesFileOriginal));
	MH_STATUS refreshSearchPathsStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x178AD0),
		&R1OFileSystem_RefreshSearchPaths,
		reinterpret_cast<LPVOID*>(&R1OFileSystem_RefreshSearchPathsOriginal));
	MH_STATUS keyValuesLoadFromFileStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x270720),
		&R1OKeyValues_LoadFromFile,
		reinterpret_cast<LPVOID*>(&R1OKeyValues_LoadFromFileOriginal));
	MH_STATUS filesystemFatalStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x179B90),
		&R1OFileSystemFatal,
		reinterpret_cast<LPVOID*>(&R1OFileSystemFatalOriginal));
	MH_STATUS comInitFilesystemStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x16D300),
		&R1OCOM_InitFilesystem,
		reinterpret_cast<LPVOID*>(&R1OCOM_InitFilesystemOriginal));
	MH_STATUS postFilesystemInitStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x4A8A0),
		&R1OEngine_PostFilesystemInit,
		reinterpret_cast<LPVOID*>(&R1OEngine_PostFilesystemInitOriginal));
	MH_STATUS initServerSystemsStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x1CD680),
		&R1OEngine_InitServerSystems,
		reinterpret_cast<LPVOID*>(&R1OEngine_InitServerSystemsOriginal));
	MH_STATUS engineFatalStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x1C1940),
		&R1OEngineFatal,
		reinterpret_cast<LPVOID*>(&R1OEngineFatalOriginal));
	MH_STATUS appSystemGroupConstructorStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x23B490),
		&R1OAppSystemGroupConstructor,
		reinterpret_cast<LPVOID*>(&R1OAppSystemGroupConstructorOriginal));
	MH_STATUS appSystemGroupRunStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x23CB50),
		&R1OAppSystemGroupRun,
		reinterpret_cast<LPVOID*>(&R1OAppSystemGroupRunOriginal));
	MH_STATUS appSystemGroupStartupStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x23CBF0),
		&R1OAppSystemGroupStartup,
		reinterpret_cast<LPVOID*>(&R1OAppSystemGroupStartupOriginal));
	MH_STATUS appSystemGroupAddSystemsStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x23C480),
		&R1OAppSystemGroupAddSystems,
		reinterpret_cast<LPVOID*>(&R1OAppSystemGroupAddSystemsOriginal));
	MH_STATUS appSystemGroupInitSystemsStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x23C850),
		&R1OAppSystemGroupInitSystems,
		reinterpret_cast<LPVOID*>(&R1OAppSystemGroupInitSystemsOriginal));
	MH_STATUS loadServerLocalStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x1C23D0),
		&R1OLoadServerLocalGameDLL,
		reinterpret_cast<LPVOID*>(&R1OLoadServerLocalGameDLLOriginal));
	MH_STATUS initializeGameDLLsStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x150960),
		&R1OInitializeGameDLLs,
		reinterpret_cast<LPVOID*>(&R1OInitializeGameDLLsOriginal));
	MH_STATUS loadModuleStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x277700),
		&R1OLoadModule,
		reinterpret_cast<LPVOID*>(&R1OLoadModuleOriginal));
	MH_STATUS loadExternalServerInfoStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x100D60),
		&R1OLoadExternalServerInfo,
		reinterpret_cast<LPVOID*>(&R1OLoadExternalServerInfoOriginal));
	MH_STATUS svcServerInfoWriteStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x20D010),
		&R1OSVCServerInfoWriteToBuffer,
		reinterpret_cast<LPVOID*>(&R1OSVCServerInfoWriteToBufferOriginal));
	MH_STATUS netMessageWriteToBufferStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x205ED0),
		&R1ONetMessageWriteToBuffer,
		reinterpret_cast<LPVOID*>(&R1ONetMessageWriteToBufferOriginal));
	MH_STATUS netMessageWriteHeaderStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x205E00),
		&R1ONetMessageWriteHeader,
		reinterpret_cast<LPVOID*>(&R1ONetMessageWriteHeaderOriginal));
	MH_STATUS netMessageWritePreludeStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x205E70),
		&R1ONetMessageWritePrelude,
		reinterpret_cast<LPVOID*>(&R1ONetMessageWritePreludeOriginal));
	MH_STATUS netMessageWriteTrailerStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x205D00),
		&R1ONetMessageWriteTrailer,
		reinterpret_cast<LPVOID*>(&R1ONetMessageWriteTrailerOriginal));
	MH_STATUS netChanSendDataStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x141CB0),
		&R1ONetChanSendData,
		reinterpret_cast<LPVOID*>(&R1ONetChanSendDataOriginal));
	MH_STATUS sendTableEncodeStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x1EC3E0),
		&R1OSendTableEncode,
		reinterpret_cast<LPVOID*>(&R1OSendTableEncodeOriginal));
	MH_STATUS writePropListStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x1EC8A0),
		&R1OWritePropList,
		reinterpret_cast<LPVOID*>(&R1OWritePropListOriginal));
	MH_STATUS buildPropListStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x1EC220),
		&R1OBuildPropList,
		reinterpret_cast<LPVOID*>(&R1OBuildPropListOriginal));
	MH_STATUS buildChangedPropListStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x1ECED0),
		&R1OBuildChangedPropList,
		reinterpret_cast<LPVOID*>(&R1OBuildChangedPropListOriginal));
	MH_STATUS deltaCalculatorAdvanceStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x1EBF00),
		&R1ODeltaCalculatorAdvance,
		reinterpret_cast<LPVOID*>(&R1ODeltaCalculatorAdvanceOriginal));
	MH_STATUS snapshotEntityWriteStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x1452B0),
		&R1OSnapshotEntityWrite,
		reinterpret_cast<LPVOID*>(&R1OSnapshotEntityWriteOriginal));
	MH_STATUS cullChangedPropsStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x1EBB20),
		&R1OCullChangedProps,
		reinterpret_cast<LPVOID*>(&R1OCullChangedPropsOriginal));
	MH_STATUS deltaPropIndexWriteStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x1D9AC0),
		&R1ODeltaPropIndexWrite,
		reinterpret_cast<LPVOID*>(&R1ODeltaPropIndexWriteOriginal));
	void* encodePropTarget = reinterpret_cast<void*>(engineR1OBase + 0x1EC010);
	MH_STATUS encodePropStatus = MH_CreateHook(
		encodePropTarget,
		&R1OEncodeProp,
		reinterpret_cast<LPVOID*>(&R1OEncodePropOriginal));
	MH_STATUS encodePropEnableStatus = (encodePropStatus == MH_OK || encodePropStatus == MH_ERROR_ALREADY_CREATED)
		? MH_EnableHook(encodePropTarget)
		: encodePropStatus;
	MH_STATUS ensureInstanceBaselineStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x154A30),
		&R1OSVEnsureInstanceBaseline,
		reinterpret_cast<LPVOID*>(&R1OSVEnsureInstanceBaselineOriginal));
	MH_STATUS cbufAddTextStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x286330),
		&R1OCbuf_AddText,
		reinterpret_cast<LPVOID*>(&R1OCbuf_AddTextOriginal));
	MH_STATUS cbufDispatchStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x1638D0),
		&R1OCbuf_Dispatch,
		reinterpret_cast<LPVOID*>(&R1OCbuf_DispatchOriginal));
	MH_STATUS cbufExecuteStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x162250),
		&R1OCbuf_Execute,
		reinterpret_cast<LPVOID*>(&R1OCbuf_ExecuteOriginal));
	if (!R1OCbuf_ExecuteOriginal)
		R1OCbuf_ExecuteOriginal = reinterpret_cast<R1OCbuf_ExecuteType>(engineR1OBase + 0x162250);
	void* nativeIpFilterTarget = reinterpret_cast<void*>(engineR1OBase + 0x148260);
	MH_STATUS nativeIpFilterStatus = MH_CreateHook(
		nativeIpFilterTarget,
		&R1ONativeIpFilter,
		reinterpret_cast<LPVOID*>(&R1ONativeIpFilterOriginal));
	MH_STATUS nativeIpFilterEnableStatus =
		(nativeIpFilterStatus == MH_OK || nativeIpFilterStatus == MH_ERROR_ALREADY_CREATED)
		? MH_EnableHook(nativeIpFilterTarget)
		: nativeIpFilterStatus;
	MH_STATUS engineFilterTimeStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x1C80C0),
		&R1OCEngineFilterTime,
		reinterpret_cast<LPVOID*>(&R1OCEngineFilterTimeOriginal));
	MH_STATUS dedicatedRunFrameStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x1C6F70),
		&R1OCDedicatedServerAPI_RunFrame,
		reinterpret_cast<LPVOID*>(&R1OCDedicatedServerAPI_RunFrameOriginal));
	MH_STATUS dedicatedAddConsoleStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x1C7080),
		&R1OCDedicatedServerAPI_AddConsoleText,
		reinterpret_cast<LPVOID*>(&R1OCDedicatedServerAPI_AddConsoleTextOriginal));
	MH_STATUS sysInitStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x1C1FC0),
		&R1OSysInit,
		reinterpret_cast<LPVOID*>(&R1OSysInitOriginal));
	MH_STATUS netListenSocketStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x203000),
		&R1ONetListenSocket,
		reinterpret_cast<LPVOID*>(&R1ONetListenSocketOriginal));
	MH_STATUS netGetLoopbackPacketStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x200820),
		&R1ONetGetLoopbackPacket,
		reinterpret_cast<LPVOID*>(&R1ONetGetLoopbackPacketOriginal));
	MH_STATUS netGetPacketStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x200920),
		&R1ONetGetPacket,
		reinterpret_cast<LPVOID*>(&R1ONetGetPacketOriginal));
	MH_STATUS netReceivePacketStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x200430),
		&R1ONetReceivePacket,
		reinterpret_cast<LPVOID*>(&R1ONetReceivePacketOriginal));
	void* recvFromTarget = nullptr;
	if (HMODULE ws2 = GetModuleHandleA("ws2_32.dll"))
		recvFromTarget = reinterpret_cast<void*>(GetProcAddress(ws2, "recvfrom"));
	MH_STATUS recvFromCreateStatus = recvFromTarget
		? MH_CreateHook(
			recvFromTarget,
			&R1ORecvFromGuard,
			reinterpret_cast<LPVOID*>(&R1ORecvFromOriginal))
		: MH_ERROR_FUNCTION_NOT_FOUND;
	MH_STATUS recvFromEnableStatus =
		(recvFromCreateStatus == MH_OK || recvFromCreateStatus == MH_ERROR_ALREADY_CREATED)
			? MH_EnableHook(recvFromTarget)
			: recvFromCreateStatus;
	if (recvFromEnableStatus != MH_OK && recvFromEnableStatus != MH_ERROR_ENABLED) {
		Warning(
			"R1Delta: failed to install R1O dedicated recvfrom loop guard "
			"(create=%d enable=%d target=%p)\n",
			static_cast<int>(recvFromCreateStatus),
			static_cast<int>(recvFromEnableStatus),
			recvFromTarget);
	}
	MH_STATUS netChanLookupStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x1FF0E0),
		&R1ONetChanLookup,
		reinterpret_cast<LPVOID*>(&R1ONetChanLookupOriginal));
	MH_STATUS netMessageLookupStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x1F92A0),
		&R1ONetMessageLookup,
		reinterpret_cast<LPVOID*>(&R1ONetMessageLookupOriginal));
	MH_STATUS netMessageDecodeStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x206260),
		&R1ONetMessageDecode,
		reinterpret_cast<LPVOID*>(&R1ONetMessageDecodeOriginal));
	MH_STATUS netProcessMessagesStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x1F5920),
		&R1ONetChanProcessMessages,
		reinterpret_cast<LPVOID*>(&R1ONetChanProcessMessagesOriginal));
	MH_STATUS netSpecialMessageStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x1F50F0),
		&R1ONetChanProcessSpecialMessage,
		reinterpret_cast<LPVOID*>(&R1ONetChanProcessSpecialMessageOriginal));
	MH_STATUS gameClientSignonStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x13F2B0),
		&R1OCGameClientProcessSignonState,
		reinterpret_cast<LPVOID*>(&R1OCGameClientProcessSignonStateOriginal));
	MH_STATUS gameClientInfoStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x142460),
		&R1OCGameClientProcessClientInfo,
		reinterpret_cast<LPVOID*>(&R1OCGameClientProcessClientInfoOriginal));
	if (IsR1ODedicatedServer() && AreR1OFakeDediVerboseLogsEnabled()) {
		s_R1OSignonStateTraceLogBudget = 4096;
		s_R1OSignonStateHandlerLogBudget = 4096;
		s_R1OClientInfoHandlerLogBudget = 1024;
		s_R1OServerInfoForceLogBudget = 256;
	}
	R1OBFReadSeek = reinterpret_cast<R1OBFReadSeekType>(engineR1OBase + 0x27B260);
	R1OBFReadString = reinterpret_cast<R1OBFReadStringType>(engineR1OBase + 0x27B3B0);

	R1ONetSendPacket = reinterpret_cast<R1ONetSendPacketType>(engineR1OBase + 0x201D70);
	R1ONETSetConVarAddToTail = reinterpret_cast<R1ONETSetConVarAddToTailType>(engineR1OBase + 0x90040);
	InstallR1ONETSetConVarReadHook(engineR1OBase);
	MH_STATUS processConnectionlessStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x134F30),
		&R1OProcessConnectionlessPacket,
		reinterpret_cast<LPVOID*>(&R1OProcessConnectionlessPacketOriginal));
	MH_STATUS replyChallengeStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x137190),
		&R1OReplyChallenge,
		reinterpret_cast<LPVOID*>(&R1OReplyChallengeOriginal));
	MH_STATUS stringTableContainsStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x54130),
		&R1OStringTableContains,
		reinterpret_cast<LPVOID*>(&R1OStringTableContainsOriginal));
	MH_STATUS stringTableLookupStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x468A0),
		&R1OStringTableLookup,
		reinterpret_cast<LPVOID*>(&R1OStringTableLookupOriginal));
	MH_STATUS hostSetActiveClientListStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x156630),
		&R1OHostSetActiveClientList,
		reinterpret_cast<LPVOID*>(&R1OHostSetActiveClientListOriginal));
	MH_STATUS hostShutdownClientListStatus = MH_CreateHook(
		(LPVOID)(engineR1OBase + 0x156720),
		&R1OHostShutdownClientList,
		reinterpret_cast<LPVOID*>(&R1OHostShutdownClientListOriginal));
	InstallR1OServerNetMessageGetIdOverrides();

	if (AreR1OFakeDediVerboseLogsEnabled()) {
	char hostShutdownBuffer[256];
	_snprintf_s(
		hostShutdownBuffer,
		sizeof(hostShutdownBuffer),
		_TRUNCATE,
		"R1Delta: R1O guarded host client-list hooks activeStatus=%d activeTarget=%p activeOriginal=%p shutdownStatus=%d shutdownTarget=%p shutdownOriginal=%p\n",
		static_cast<int>(hostSetActiveClientListStatus),
		reinterpret_cast<void*>(engineR1OBase + 0x156630),
		reinterpret_cast<void*>(R1OHostSetActiveClientListOriginal),
		static_cast<int>(hostShutdownClientListStatus),
		reinterpret_cast<void*>(engineR1OBase + 0x156720),
		reinterpret_cast<void*>(R1OHostShutdownClientListOriginal));
	OutputDebugStringA(hostShutdownBuffer);

	char netMessageHookBuffer[512];
	_snprintf_s(
		netMessageHookBuffer,
		sizeof(netMessageHookBuffer),
		_TRUNCATE,
		"R1Delta: R1O netmessage hook install lookup=%d decode=%d dispatch=%d special=%d signonHandler=%d clientInfoHandler=%d lookupOrig=%p decodeOrig=%p dispatchOrig=%p specialOrig=%p signonHandlerOrig=%p clientInfoHandlerOrig=%p\n",
		static_cast<int>(netMessageLookupStatus),
		static_cast<int>(netMessageDecodeStatus),
		static_cast<int>(netProcessMessagesStatus),
		static_cast<int>(netSpecialMessageStatus),
		static_cast<int>(gameClientSignonStatus),
		static_cast<int>(gameClientInfoStatus),
		reinterpret_cast<void*>(R1ONetMessageLookupOriginal),
		reinterpret_cast<void*>(R1ONetMessageDecodeOriginal),
		reinterpret_cast<void*>(R1ONetChanProcessMessagesOriginal),
		reinterpret_cast<void*>(R1ONetChanProcessSpecialMessageOriginal),
		reinterpret_cast<void*>(R1OCGameClientProcessSignonStateOriginal),
		reinterpret_cast<void*>(R1OCGameClientProcessClientInfoOriginal));
	OutputDebugStringA(netMessageHookBuffer);

	char svcServerInfoHookBuffer[256];
	_snprintf_s(
		svcServerInfoHookBuffer,
		sizeof(svcServerInfoHookBuffer),
		_TRUNCATE,
		"R1Delta: R1O SVC_ServerInfo::WriteToBuffer hook status=%d target=%p original=%p\n",
		static_cast<int>(svcServerInfoWriteStatus),
		reinterpret_cast<void*>(engineR1OBase + 0x20D010),
		reinterpret_cast<void*>(R1OSVCServerInfoWriteToBufferOriginal));
	OutputDebugStringA(svcServerInfoHookBuffer);

	char netMessageWriteHeaderHookBuffer[256];
	_snprintf_s(
		netMessageWriteHeaderHookBuffer,
		sizeof(netMessageWriteHeaderHookBuffer),
		_TRUNCATE,
		"R1Delta: R1O netmsg direct write hook status=%d target=%p original=%p headerStatus=%d headerTarget=%p headerOriginal=%p trailerStatus=%d trailerTarget=%p trailerOriginal=%p\n",
		static_cast<int>(netMessageWriteToBufferStatus),
		reinterpret_cast<void*>(engineR1OBase + 0x205ED0),
		reinterpret_cast<void*>(R1ONetMessageWriteToBufferOriginal),
		static_cast<int>(netMessageWriteHeaderStatus),
		reinterpret_cast<void*>(engineR1OBase + 0x205E00),
		reinterpret_cast<void*>(R1ONetMessageWriteHeaderOriginal),
		static_cast<int>(netMessageWriteTrailerStatus),
		reinterpret_cast<void*>(engineR1OBase + 0x205D00),
		reinterpret_cast<void*>(R1ONetMessageWriteTrailerOriginal));
	OutputDebugStringA(netMessageWriteHeaderHookBuffer);

	char netChanSendDataHookBuffer[256];
	_snprintf_s(
		netChanSendDataHookBuffer,
		sizeof(netChanSendDataHookBuffer),
		_TRUNCATE,
		"R1Delta: R1O CNetChan::SendData direct hook status=%d target=%p original=%p sendTableEncode=%d sendTableTarget=%p sendTableOriginal=%p\n",
		static_cast<int>(netChanSendDataStatus),
		reinterpret_cast<void*>(engineR1OBase + 0x141CB0),
		reinterpret_cast<void*>(R1ONetChanSendDataOriginal),
		static_cast<int>(sendTableEncodeStatus),
		reinterpret_cast<void*>(engineR1OBase + 0x1EC3E0),
		reinterpret_cast<void*>(R1OSendTableEncodeOriginal));
	OutputDebugStringA(netChanSendDataHookBuffer);

	char writePropListHookBuffer[256];
	_snprintf_s(
		writePropListHookBuffer,
		sizeof(writePropListHookBuffer),
		_TRUNCATE,
		"R1Delta: R1O WritePropList hook status=%d target=%p original=%p\n",
		static_cast<int>(writePropListStatus),
		reinterpret_cast<void*>(engineR1OBase + 0x1EC8A0),
		reinterpret_cast<void*>(R1OWritePropListOriginal));
	OutputDebugStringA(writePropListHookBuffer);

	char buildPropListHookBuffer[512];
	_snprintf_s(
		buildPropListHookBuffer,
		sizeof(buildPropListHookBuffer),
		_TRUNCATE,
		"R1Delta: R1O BuildPropList hooks buildStatus=%d buildTarget=%p buildOriginal=%p changedStatus=%d changedTarget=%p changedOriginal=%p compareStatus=%d compareTarget=%p compareOriginal=%p\n",
		static_cast<int>(buildPropListStatus),
		reinterpret_cast<void*>(engineR1OBase + 0x1EC220),
		reinterpret_cast<void*>(R1OBuildPropListOriginal),
		static_cast<int>(buildChangedPropListStatus),
		reinterpret_cast<void*>(engineR1OBase + 0x1ECED0),
		reinterpret_cast<void*>(R1OBuildChangedPropListOriginal),
		static_cast<int>(deltaCalculatorAdvanceStatus),
		reinterpret_cast<void*>(engineR1OBase + 0x1EBF00),
		reinterpret_cast<void*>(R1ODeltaCalculatorAdvanceOriginal));
	OutputDebugStringA(buildPropListHookBuffer);

	char propCullHookBuffer[384];
	_snprintf_s(
		propCullHookBuffer,
		sizeof(propCullHookBuffer),
		_TRUNCATE,
		"R1Delta: R1O snapshot prop-cull hooks snapshotStatus=%d snapshotTarget=%p snapshotOriginal=%p cullStatus=%d cullTarget=%p cullOriginal=%p\n",
		static_cast<int>(snapshotEntityWriteStatus),
		reinterpret_cast<void*>(engineR1OBase + 0x1452B0),
		reinterpret_cast<void*>(R1OSnapshotEntityWriteOriginal),
		static_cast<int>(cullChangedPropsStatus),
		reinterpret_cast<void*>(engineR1OBase + 0x1EBB20),
		reinterpret_cast<void*>(R1OCullChangedPropsOriginal));
	OutputDebugStringA(propCullHookBuffer);

	char deltaPropIndexHookBuffer[256];
	_snprintf_s(
		deltaPropIndexHookBuffer,
		sizeof(deltaPropIndexHookBuffer),
		_TRUNCATE,
		"R1Delta: R1O delta prop-index write hook status=%d target=%p original=%p\n",
		static_cast<int>(deltaPropIndexWriteStatus),
		reinterpret_cast<void*>(engineR1OBase + 0x1D9AC0),
		reinterpret_cast<void*>(R1ODeltaPropIndexWriteOriginal));
	OutputDebugStringA(deltaPropIndexHookBuffer);
	char encodePropHookBuffer[256];
	_snprintf_s(
		encodePropHookBuffer,
		sizeof(encodePropHookBuffer),
		_TRUNCATE,
		"R1Delta: R1O encode-prop hook status=%d enable=%d target=%p original=%p\n",
		static_cast<int>(encodePropStatus),
		static_cast<int>(encodePropEnableStatus),
		encodePropTarget,
		reinterpret_cast<void*>(R1OEncodePropOriginal));
	OutputDebugStringA(encodePropHookBuffer);

	char instanceBaselineHookBuffer[256];
	_snprintf_s(
		instanceBaselineHookBuffer,
		sizeof(instanceBaselineHookBuffer),
		_TRUNCATE,
		"R1Delta: R1O SV_EnsureInstanceBaseline hook status=%d target=%p original=%p\n",
		static_cast<int>(ensureInstanceBaselineStatus),
		reinterpret_cast<void*>(engineR1OBase + 0x154A30),
		reinterpret_cast<void*>(R1OSVEnsureInstanceBaselineOriginal));
	OutputDebugStringA(instanceBaselineHookBuffer);

	char nativeIpFilterHookBuffer[320];
	_snprintf_s(
		nativeIpFilterHookBuffer,
		sizeof(nativeIpFilterHookBuffer),
		_TRUNCATE,
		"R1Delta: R1O native IP filter hook create=%d enable=%d target=%p original=%p\n",
		static_cast<int>(nativeIpFilterStatus),
		static_cast<int>(nativeIpFilterEnableStatus),
		nativeIpFilterTarget,
		reinterpret_cast<void*>(R1ONativeIpFilterOriginal));
	OutputDebugStringA(nativeIpFilterHookBuffer);

	char buffer[768];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O hook install engine=%p createInterface=%p createInterfaceHook=%d connect=%d init=%d modinit=%d loadsearchpaths=%d parseGameInfo=%d setGameInfoName=%d loadKeyValues=%d refreshSearchPaths=%d keyValuesLoad=%d fsfatal=%d comInitFs=%d postFs=%d initServerSystems=%d engineFatal=%d appCtor=%d appRun=%d appStartup=%d appAdd=%d appInit=%d loadServerLocal=%d initGameDLLs=%d loadModule=%d serverInfo=%d cbufAdd=%d cbufDispatch=%d cbufExec=%d dedicatedRunFrame=%d dedicatedAddConsole=%d sysInit=%d netListen=%d netGetLoopback=%d netGetPacket=%d netReceive=%d netChanLookup=%d processConnectionless=%d replyChallenge=%d stringTableContains=%d stringTableLookup=%d createInterfaceOrig=%p connectOrig=%p initOrig=%p modinitOrig=%p loadsearchOrig=%p parseOrig=%p setGameInfoOrig=%p loadKVOrig=%p refreshOrig=%p keyValuesLoadOrig=%p comInitFsOrig=%p postFsOrig=%p initServerSystemsOrig=%p engineFatalOrig=%p appRunOrig=%p appStartupOrig=%p appAddOrig=%p appInitOrig=%p loadServerLocalOrig=%p initGameDLLsOrig=%p loadModuleOrig=%p serverInfoOrig=%p cbufAddOrig=%p cbufDispatchOrig=%p cbufExecOrig=%p dedicatedRunFrameOrig=%p dedicatedAddConsoleOrig=%p sysInitOrig=%p netGetLoopbackOrig=%p netGetPacketOrig=%p netReceiveOrig=%p netChanLookupOrig=%p processConnectionlessOrig=%p stringTableLookupOrig=%p\n",
		reinterpret_cast<void*>(engineR1OBase),
		reinterpret_cast<void*>(R1OCreateInterface),
		static_cast<int>(createInterfaceStatus),
		static_cast<int>(connectStatus),
		static_cast<int>(initStatus),
		static_cast<int>(modInitStatus),
		static_cast<int>(loadSearchPathsStatus),
		static_cast<int>(parseGameInfoStatus),
		static_cast<int>(setGameInfoNameStatus),
		static_cast<int>(loadKeyValuesFileStatus),
		static_cast<int>(refreshSearchPathsStatus),
		static_cast<int>(keyValuesLoadFromFileStatus),
		static_cast<int>(filesystemFatalStatus),
		static_cast<int>(comInitFilesystemStatus),
		static_cast<int>(postFilesystemInitStatus),
		static_cast<int>(initServerSystemsStatus),
		static_cast<int>(engineFatalStatus),
		static_cast<int>(appSystemGroupConstructorStatus),
		static_cast<int>(appSystemGroupRunStatus),
		static_cast<int>(appSystemGroupStartupStatus),
		static_cast<int>(appSystemGroupAddSystemsStatus),
		static_cast<int>(appSystemGroupInitSystemsStatus),
		static_cast<int>(loadServerLocalStatus),
		static_cast<int>(initializeGameDLLsStatus),
		static_cast<int>(loadModuleStatus),
		static_cast<int>(loadExternalServerInfoStatus),
		static_cast<int>(cbufAddTextStatus),
		static_cast<int>(cbufDispatchStatus),
		static_cast<int>(cbufExecuteStatus),
		static_cast<int>(dedicatedRunFrameStatus),
		static_cast<int>(dedicatedAddConsoleStatus),
		static_cast<int>(sysInitStatus),
		static_cast<int>(netListenSocketStatus),
		static_cast<int>(netGetLoopbackPacketStatus),
		static_cast<int>(netGetPacketStatus),
		static_cast<int>(netReceivePacketStatus),
		static_cast<int>(netChanLookupStatus),
		static_cast<int>(processConnectionlessStatus),
		static_cast<int>(replyChallengeStatus),
		static_cast<int>(stringTableContainsStatus),
		static_cast<int>(stringTableLookupStatus),
		reinterpret_cast<void*>(R1OCreateInterfaceOriginal),
		reinterpret_cast<void*>(CDedicatedServerAPI_ConnectOriginal),
		reinterpret_cast<void*>(CDedicatedServerAPI_InitOriginal),
		reinterpret_cast<void*>(CDedicatedServerAPI_ModInitOriginal),
		reinterpret_cast<void*>(R1OFileSystem_LoadSearchPathsOriginal),
		reinterpret_cast<void*>(R1OFileSystem_ParseGameInfoOriginal),
		reinterpret_cast<void*>(R1OFileSystem_SetGameInfoNameOriginal),
		reinterpret_cast<void*>(R1OFileSystem_LoadKeyValuesFileOriginal),
		reinterpret_cast<void*>(R1OFileSystem_RefreshSearchPathsOriginal),
		reinterpret_cast<void*>(R1OKeyValues_LoadFromFileOriginal),
		reinterpret_cast<void*>(R1OCOM_InitFilesystemOriginal),
		reinterpret_cast<void*>(R1OEngine_PostFilesystemInitOriginal),
		reinterpret_cast<void*>(R1OEngine_InitServerSystemsOriginal),
		reinterpret_cast<void*>(R1OEngineFatalOriginal),
		reinterpret_cast<void*>(R1OAppSystemGroupRunOriginal),
		reinterpret_cast<void*>(R1OAppSystemGroupStartupOriginal),
		reinterpret_cast<void*>(R1OAppSystemGroupAddSystemsOriginal),
		reinterpret_cast<void*>(R1OAppSystemGroupInitSystemsOriginal),
		reinterpret_cast<void*>(R1OLoadServerLocalGameDLLOriginal),
		reinterpret_cast<void*>(R1OInitializeGameDLLsOriginal),
		reinterpret_cast<void*>(R1OLoadModuleOriginal),
		reinterpret_cast<void*>(R1OLoadExternalServerInfoOriginal),
		reinterpret_cast<void*>(R1OCbuf_AddTextOriginal),
		reinterpret_cast<void*>(R1OCbuf_DispatchOriginal),
		reinterpret_cast<void*>(R1OCbuf_ExecuteOriginal),
		reinterpret_cast<void*>(R1OCDedicatedServerAPI_RunFrameOriginal),
		reinterpret_cast<void*>(R1OCDedicatedServerAPI_AddConsoleTextOriginal),
		reinterpret_cast<void*>(R1OSysInitOriginal),
		reinterpret_cast<void*>(R1ONetGetLoopbackPacketOriginal),
		reinterpret_cast<void*>(R1ONetGetPacketOriginal),
		reinterpret_cast<void*>(R1ONetReceivePacketOriginal),
		reinterpret_cast<void*>(R1ONetChanLookupOriginal),
		reinterpret_cast<void*>(R1OProcessConnectionlessPacketOriginal),
		reinterpret_cast<void*>(R1OStringTableLookupOriginal));
	OutputDebugStringA(buffer);
	}

	s_R1ODedicatedServerAPIHooked = true;
}

void InitVStdLibICVarFactoryHook() {
	if (s_VStdLibICVarFactoryHooked)
		return;

	HMODULE vstdlib = GetModuleHandleA("vstdlib.dll");
	if (!vstdlib)
		return;

	auto factory = GetProcAddress(vstdlib, "VStdLib_GetICVarFactory");
	if (!factory)
		return;

	MH_CreateHook((LPVOID)factory, &VStdLib_GetICVarFactory, NULL);
	s_VStdLibICVarFactoryHooked = true;
}

void* R1OFactory(const char* pName, int* pReturnCode) {
//	std::cout << "looking for " << pName << std::endl;

	if (!pName)
		return nullptr;

	if (IsR1ODedicatedServer() && !strcmp_static(pName, "VEngineCvar007"))
		return R1OWrappedCVarInterface(pName, pReturnCode);

	if (IsR1ODedicatedServer()
		&& (!strcmp_static(pName, "VFileSystem017")
			|| !strcmp_static(pName, "VBaseFileSystem012")
			|| !strcmp_static(pName, "VNewAsyncFileSystem001"))) {
		return R1OTFOFileSystemInterface(pName, pReturnCode);
	}

	if (IsR1ODedicatedServer() && !strcmp_static(pName, "VScriptManager009"))
		return R1OTFOLauncherInterface(pName, pReturnCode);

	if (IsR1ODedicatedServer()
		&& !strcmp_static(pName, "Localize_001")
		&& IsR1OOrTFOModuleAddress(_ReturnAddress())) {
		return R1OTFOLocalizeInterfaceForEngine(pReturnCode);
	}

	if (IsR1ODedicatedServer()
		&& (!strcmp_static(pName, "InputSystemVersion001")
			|| !strcmp_static(pName, "InputStackSystemVersion001"))
		&& IsR1OOrTFOModuleAddress(_ReturnAddress())) {
		return R1OTFOInputSystemInterfaceForEngine(pName, pReturnCode);
	}

	if (IsR1ODedicatedServer()
		&& strcmp_static(pName, "VFileSystem017") != 0) {
		void* result = oAppSystemFactory(pName, pReturnCode);
		DebugR1ODediFactoryResult("appsystem", pName, result, pReturnCode);
		if (result)
			return RememberR1ONativeEngineServer022(pName, result);

		CreateInterfaceFn engineCreateInterface = R1OCreateInterfaceOriginal ? R1OCreateInterfaceOriginal : R1OCreateInterface;
		if (!engineCreateInterface) {
			DebugR1ODediFactoryResult("r1o-missing-createinterface", pName, nullptr, pReturnCode);
			return nullptr;
		}

		result = engineCreateInterface(pName, pReturnCode);
		DebugR1ODediFactoryResult("engine_r1o", pName, result, pReturnCode);
		if (result)
			return RememberR1ONativeEngineServer022(pName, result);

		result = R1OTFOSupportModuleInterface(pName, pReturnCode);
		if (result)
			return RememberR1ONativeEngineServer022(pName, result);

		return RememberR1ONativeEngineServer022(
			pName,
			R1OQueryLoadedModuleFactories(pName, pReturnCode));
	}

	if (!strcmp_static(pName, "VEngineServer022")) {
		//std::cout << "wrapping VEngineServer022" << std::endl;

		uintptr_t* r1vtable = *(uintptr_t**)oAppSystemFactory(pName, pReturnCode);
		g_CVEngineServerInterface = (uintptr_t)oAppSystemFactory(pName, pReturnCode);
		if (!g_CVEngineServer)
		{
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
		OriginalCCvar__ProcessQueuedMaterialThreadConVarSets = reinterpret_cast<decltype(OriginalCCvar__ProcessQueuedMaterialThreadConVarSets)>(r1vtable[39]);
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
	CGlobalVarsServer2015* pGlobals)
{
	if (IsDedicatedServer() && !IsR1ODedicatedServer()) {
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
	engineR1O = IsR1ODedicatedServer() ? GetModuleHandleA("engine_r1o.dll") : LoadLibraryA("engine_r1o.dll");
	if (!engineR1O)
		engineR1O = LoadLibraryA("engine_r1o.dll");
	R1OCreateInterface = reinterpret_cast<CreateInterfaceFn>(GetProcAddress(engineR1O, "CreateInterface"));

	InstallR1OSetPreCacheHook();
	CreateInterfaceFn fnptr = (CreateInterfaceFn)(&R1OFactory);
	if (!IsR1ODedicatedServer()) {
		reinterpret_cast<char(__fastcall*)(__int64, CreateInterfaceFn)>((uintptr_t)(engineR1O)+0x1C6B30)(0, fnptr); // call is to CDedicatedServerAPI::Connect
	}
	void* whatev = fnptr;
	//reinterpret_cast<void(__fastcall*)()>((uintptr_t)(engineR1O)+0x2742A0)(); // register engine convars
	//*(__int64*)((uintptr_t)(GetModuleHandleA("launcher_r1o.dll")) + 0xECBE0) = fsintfakeptr; 
//	reinterpret_cast<__int64(__fastcall*)(int a1)>(GetProcAddress(GetModuleHandleA("tier0_orig.dll"), "SetTFOFileLogLevel"))(999);
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
	auto ret = CServerGameDLL__DLLInitOriginal(thisptr, fnptr, fnptr, fnptr, pGlobals);
	if (!IsR1ODedicatedServer()) {
		InitializeRecentHostVars();
		OriginalCCVar_FindVar(cvarinterface, "sv_pausable_dev")->m_nFlags &= ~(FCVAR_HIDDEN | FCVAR_DEVELOPMENTONLY);
	}
	return ret;
}

extern "C" __declspec(dllexport) void StackToolsNotify_LoadedLibrary(char* pModuleName)
{
	//printf("loaded %s\n", pModuleName);
}
typedef char(*MatchRecvPropsToSendProps_RType)(__int64 a1, __int64 a2, __int64 a3, __int64 a4);
MatchRecvPropsToSendProps_RType MatchRecvPropsToSendProps_ROriginal;

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
		if (_stricmp(*(const char**)RecvProp, v9) == 0)
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

	auto sub_1801D9D00 = (__int64(__fastcall*)(__int64 a1, _QWORD * a2))(ENGINE_DLL_BASE + 0x1D9D00);
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
				MessageBoxA(NULL, *(const char**)(v7 + 72), "SendProp Error", 16);
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
	auto dtname = *(const char**)(a1 + 16);
	auto dtname_len = strlen(dtname);
	if (string_equal_size(dtname, dtname_len, "DT_BigBrotherPanelEntity") || string_equal_size(dtname, dtname_len, "DT_ControlPanelEntity") || string_equal_size(dtname, dtname_len, "DT_RushPointEntity") || string_equal_size(dtname, dtname_len, "DT_SpawnItemEntity")) {
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

static bool IsR1OOrTFOModuleAddress(PVOID retAddress)
{
	HMODULE hModule;
	if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCSTR>(retAddress), &hModule))
		return false;

	char modulePath[MAX_PATH];
	if (GetModuleFileNameA(hModule, modulePath, sizeof(modulePath)) == 0)
		return false;

	char upperPath[MAX_PATH];
	strcpy_s(upperPath, modulePath);
	_strupr_s(upperPath, MAX_PATH);

	if (strstr(upperPath, "R1O") != nullptr)
		return true;

	if (!IsR1ODedicatedServer())
		return false;

	return r1delta::r1o::IsTFORuntimeModulePathA(modulePath);
}

__int64 VStdLib_GetICVarFactory() {
	return IsR1OOrTFOModuleAddress(_ReturnAddress()) ? (__int64)R1OFactory : (__int64)(((uintptr_t)(GetModuleHandleA("vstdlib.dll")) + 0x023DD0));
}


// CreateTrampolineFunction is now CreateCallgate in utils.cpp
// Alias for backwards compatibility
#define CreateTrampolineFunction(vftable, start, end, orig, new_idx) \
	CreateCallgate(vftable, start, end, orig, new_idx)

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

void* __fastcall NET_CreateNetChannel(int a1, unsigned int* a2, const char* a3, __int64 a4, char a5, char a6)
{
    void* netChan = NET_CreateNetChannelOriginal(a1, a2, a3, a4, a5, a6);
    InitializeModifiedNetCHANVTable(netChan);
    *(uintptr_t**)netChan = static_cast<uintptr_t*>(modifiedNetCHANVTable);
    return netChan;
}
