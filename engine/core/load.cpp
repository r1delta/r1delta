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
#define DELAYIMP_INSECURE_WRITABLE_HOOKS
#include "core.h"
#include "arena.h"
#include "tctx.h"

#include "load.h"
#include "player_resource_18.h"
#include "ffa_targeting.h"
#include "client_port_override.h"
#include "r1o_runtime_paths.h"
#include <cstdlib>
#include <cmath>
#include <new>
#include "windows.h"
#include <delayimp.h>

#include <iostream>
#include "cvar.h"
#include <winternl.h> // For UNICODE_STRING.
#include <fstream>
#include <filesystem>
#include <array>
#include <cstdarg>
#include <intrin.h>
#include "memory.h"
#include "filesystem.h"
#include "defs.h"
#include "factory.h"
#include "core.h"
#include "load.h"
#include "patcher.h"
#include "MinHook.h"
#include "TableDestroyer.h"
#include "bitbuf.h"
#include "in6addr.h"
#include <fcntl.h>
#include <io.h>
#include <streambuf>
#include "navmesh.h"
#include <psapi.h>
#include "logging.h"
#include "squirrel.h"
#include "dedicated.h"
#include "predictionerror.h"
#include "netadr.h"
#include "sendmoveclampfix.h"
#include "dedicated.h"
#include "client.h"
#include "compression.h"
#include "cvar.h"
#include "persistentdata.h"
#include "weaponxdebug.h"
#include "netchanwarnings.h"
#include "engine_vtable.h"
#include "security_fixes.h"
#include "r1o_vpk.h"
#include "vpk_async_precache_fix.h"
#include "server_usercmd.h"
#include "commands.h"
// steam.h removed - unused
#include "persistentdata.h"
#include "netadr.h"
#include <httplib.h>
#include "audio.h"
#include "audio_device.h"
#include "audio_cache.h"
#include "reflex.h"
#include <nlohmann/json.hpp>
#include "shellapi.h"
//#define JWT
#ifdef JWT
#include <l8w8jwt/decode.h>
#include "l8w8jwt/encode.h"
#include "jwt_compact.h"
#endif
#include "vector.h"
#include "hudwarp.h"
#include "hudwarp_convars.h"
#include "hudwarp_hooks.h"
#include "surfacerender.h"
#include "mcp_server.h"
#include "localchatwriter.h"
#include "discord.h"
#include "eos_network.h"
#include "net_hooks.h"
#define DISCORDPP_IMPLEMENTATION
#ifdef DISCORD
#include <discordpp.h>
#endif
#include "sv_filter.h"
#include <discord-game-sdk/discord.h>
#include <Mmdeviceapi.h>

#include "r1d_version.h"

// Refactored module headers
#include "misc.h"
#include "usermessages.h"
#include "precache.h"
#include "auth.h"
#include "bot.h"
#include "physics_hooks.h"
#include "vphysics_shutdown_guard.h"
#include "chat.h"
#include "localize.h"
#include "networking.h"

// Define and initialize the static member for the ConVar
ConVarR1 *CBanSystem::m_pSvBanlistAutosave = nullptr;

std::atomic<bool> running = true;

// Signal handler to stop the application

//
// auto client = std::make_shared<discordpp::Client>();

#pragma intrinsic(_ReturnAddress)

extern "C"
{
	uintptr_t CNetChan__ProcessSubChannelData_ret0 = 0;
	uintptr_t CNetChan__ProcessSubChannelData_Asm_continue = 0;
	extern uintptr_t CNetChan__ProcessSubChannelData_AsmConductBufferSizeCheck;
}
void *dll_notification_cookie_;

struct SavedCall {
	__int64 a1;
	std::string a2;
	int a3;
};
LDR_DLL_LOADED_NOTIFICATION_DATA* GetModuleNotificationData(const wchar_t* moduleName)
{
	HMODULE hMods[1024];
	DWORD cbNeeded;
	MODULEINFO modInfo;

	if (EnumProcessModules(GetCurrentProcess(), hMods, sizeof(hMods), &cbNeeded))
	{
		for (DWORD i = 0; i < (cbNeeded / sizeof(HMODULE)); i++)
		{
			wchar_t szModName[MAX_PATH];
			if (GetModuleFileNameEx(GetCurrentProcess(), hMods[i], szModName, sizeof(szModName) / sizeof(wchar_t)))
			{
				if (wcsstr(szModName, moduleName) != 0)
				{
					if (GetModuleInformation(GetCurrentProcess(), hMods[i], &modInfo, sizeof(modInfo)))
					{
						LDR_DLL_LOADED_NOTIFICATION_DATA* notificationData = new LDR_DLL_LOADED_NOTIFICATION_DATA();
						notificationData->Flags = 0;

						UNICODE_STRING* fullDllName = new UNICODE_STRING();
						fullDllName->Buffer = new wchar_t[MAX_PATH];
						wcscpy_s(fullDllName->Buffer, MAX_PATH, szModName);
						fullDllName->Length = (USHORT)wcslen(szModName) * sizeof(wchar_t);
						fullDllName->MaximumLength = MAX_PATH * sizeof(wchar_t);
						notificationData->FullDllName = fullDllName;

						UNICODE_STRING* baseDllName = new UNICODE_STRING();
						baseDllName->Buffer = new wchar_t[MAX_PATH];
						_wsplitpath_s(szModName, NULL, 0, NULL, 0, baseDllName->Buffer, MAX_PATH, NULL, 0);
						lstrcatW(baseDllName->Buffer, L".dll");
						baseDllName->Length = (USHORT)wcslen(baseDllName->Buffer) * sizeof(wchar_t);
						baseDllName->MaximumLength = MAX_PATH * sizeof(wchar_t);
						notificationData->BaseDllName = baseDllName;

						notificationData->DllBase = modInfo.lpBaseOfDll;
						notificationData->SizeOfImage = modInfo.SizeOfImage;
						return notificationData;
					}
				}
			}
		}
	}

	return nullptr;
}

void FreeModuleNotificationData(LDR_DLL_LOADED_NOTIFICATION_DATA* data) {
	delete[] data->BaseDllName->Buffer;
	delete data->BaseDllName;
	delete[] data->FullDllName->Buffer;
	delete data->FullDllName;
	delete data;
}

uintptr_t G_launcher;
uintptr_t G_vscript;
uintptr_t G_filesystem_stdio;
uintptr_t G_server;
uintptr_t G_engine;
uintptr_t G_engine_ds;
uintptr_t G_engine_r1o;
uintptr_t G_client;
uintptr_t G_matsystem;
uintptr_t G_localize;

using DedicatedLoadLibraryA_t = HMODULE(WINAPI*)(LPCSTR);
using DedicatedLoadLibraryW_t = HMODULE(WINAPI*)(LPCWSTR);
using DedicatedLoadLibraryExA_t = HMODULE(WINAPI*)(LPCSTR, HANDLE, DWORD);
using DedicatedLoadLibraryExW_t = HMODULE(WINAPI*)(LPCWSTR, HANDLE, DWORD);

static DedicatedLoadLibraryA_t DedicatedLoadLibraryAOriginal;
static DedicatedLoadLibraryW_t DedicatedLoadLibraryWOriginal;
static DedicatedLoadLibraryExA_t DedicatedLoadLibraryExAOriginal;
static DedicatedLoadLibraryExW_t DedicatedLoadLibraryExWOriginal;
static CreateInterfaceFn DedicatedCreateInterfaceOriginal;
static CreateInterfaceFn StudioRenderCreateInterfaceOriginal;
static CreateInterfaceFn VPhysicsCreateInterfaceOriginal;
static CreateInterfaceFn DataCacheCreateInterfaceOriginal;
static CreateInterfaceFn VGui2CreateInterfaceOriginal;
static CreateInterfaceFn InputSystemCreateInterfaceOriginal;
using R1OHeadlessAppSystemInitFn = int(__fastcall*)(void* thisptr);
static R1OHeadlessAppSystemInitFn StudioRenderAppSystemInitOriginal;
static void** StudioRenderAppSystemVTable;
static R1OHeadlessAppSystemInitFn VPhysicsAppSystemInitOriginal;
static void** VPhysicsAppSystemVTable;
static R1OHeadlessAppSystemInitFn DataCacheAppSystemInitOriginal;
static void** DataCacheAppSystemVTable;
static R1OHeadlessAppSystemInitFn MDLCacheAppSystemInitOriginal;
static void** MDLCacheAppSystemVTable;
static R1OHeadlessAppSystemInitFn StudioDataCacheAppSystemInitOriginal;
static void** StudioDataCacheAppSystemVTable;
static R1OHeadlessAppSystemInitFn InputSystemAppSystemInitOriginal;
static void** InputSystemAppSystemVTable;
using DedicatedSetupSteamSearchPathsFn = __int64(__fastcall*)(__int64 setupInfo);
static DedicatedSetupSteamSearchPathsFn DedicatedSetupSteamSearchPathsOriginal;
using DedicatedTierAppStartupFn = bool(__fastcall*)(const char* appName);
static DedicatedTierAppStartupFn DedicatedTierAppStartupOriginal;
using DedicatedVguiSteamAppPreInitFn = bool(__fastcall*)();
static DedicatedVguiSteamAppPreInitFn DedicatedVguiSteamAppPreInitOriginal;
using DedicatedInitConsoleAndFilesystemFn = bool(__fastcall*)();
static DedicatedInitConsoleAndFilesystemFn DedicatedInitConsoleAndFilesystemOriginal;
using DedicatedSharedServerVPKSetupFn = __int64(__fastcall*)();
static DedicatedSharedServerVPKSetupFn DedicatedSharedServerVPKSetupOriginal;
using DedicatedSteamApplicationStartupFn = __int64(__fastcall*)(__int64 app);
using DedicatedConsoleOutputFn = int(__fastcall*)(void* sys, const char* text);
static DedicatedConsoleOutputFn DedicatedConsoleOutputOriginal;

using CStaticPropMgrLoadStaticPropsFn = __int64(__fastcall*)(__int64 staticPropMgr);
using CStaticPropMgrStaticPropDistanceTestFn = bool(__fastcall*)(__int64 staticPropMgr, int staticPropIndex, float* origin, float maxDistanceSquared);
using CollisionBSPDataBuildBrushContentsFn = __int64(__fastcall*)(__int64 collisionBspData);
static CStaticPropMgrLoadStaticPropsFn CStaticPropMgrLoadStaticPropsOriginal;
static CStaticPropMgrStaticPropDistanceTestFn CStaticPropMgrStaticPropDistanceTestOriginal;
static CollisionBSPDataBuildBrushContentsFn CollisionBSPDataBuildBrushContentsOriginal;
static bool s_NoStaticPropsHookInstalled;
static bool s_NoClipBrushesHookInstalled;
static int s_NoStaticPropsLogBudget = 4;
static int s_NoClipBrushesLogBudget = 4;

static bool ShouldDisableStaticProps()
{
	static bool parsed = false;
	static bool enabled = false;
	if (!parsed) {
		parsed = true;
		enabled = HasEngineCommandLineFlag("-nostaticprops");
	}
	return enabled;
}

static bool ShouldDisableClipBrushes()
{
	static bool parsed = false;
	static bool enabled = false;
	if (!parsed) {
		parsed = true;
		enabled = HasEngineCommandLineFlag("-noclipbrushes");
	}
	return enabled;
}

static __int64 __fastcall CStaticPropMgrLoadStaticPropsNoop(__int64 staticPropMgr)
{
	if (staticPropMgr) {
		__try {
			*reinterpret_cast<unsigned char*>(staticPropMgr + 0x60) = 1;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
		}
	}

	if (s_NoStaticPropsLogBudget > 0) {
		--s_NoStaticPropsLogBudget;
		OutputDebugStringA("R1Delta: -nostaticprops skipped CStaticPropMgr vtable slot 2 static-prop load\n");
	}

	return 0;
}

static bool __fastcall CStaticPropMgrStaticPropDistanceTestGuard(
	__int64 staticPropMgr,
	int staticPropIndex,
	float* origin,
	float maxDistanceSquared)
{
	if (!*reinterpret_cast<void**>(staticPropMgr + 0x38))
		return false;
	return CStaticPropMgrStaticPropDistanceTestOriginal(
		staticPropMgr,
		staticPropIndex,
		origin,
		maxDistanceSquared);
}

static bool InstallEngineCommandLineCheckedHook(
	uintptr_t engineBase,
	uintptr_t rva,
	const unsigned char* expectedPrologue,
	size_t expectedPrologueSize,
	void* detour,
	void** original,
	const char* featureName,
	const char* name)
{
	void* target = reinterpret_cast<void*>(engineBase + rva);
	if (memcmp(target, expectedPrologue, expectedPrologueSize) != 0) {
		char buffer[256];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: %s skipped %s hook because bytes did not match\n",
			featureName,
			name);
		OutputDebugStringA(buffer);
		return false;
	}

	const MH_STATUS createStatus = MH_CreateHook(target, detour, original);
	const MH_STATUS enableStatus = (createStatus == MH_OK || createStatus == MH_ERROR_ALREADY_CREATED)
		? MH_EnableHook(target)
		: createStatus;
	const bool installed = enableStatus == MH_OK || enableStatus == MH_ERROR_ENABLED;

	char buffer[256];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: %s %s hook create=%d enable=%d target=%p original=%p\n",
		featureName,
		name,
		static_cast<int>(createStatus),
		static_cast<int>(enableStatus),
		target,
		original ? *original : nullptr);
	OutputDebugStringA(buffer);
	return installed;
}

static void InstallNoStaticPropsHook(uintptr_t engineBase)
{
	if (s_NoStaticPropsHookInstalled || !engineBase)
		return;

	const unsigned char expectedLevelInitPrologue[] = {
		0x48, 0x83, 0xEC, 0x28,
		0x80, 0x79, 0x60, 0x00,
		0x75, 0x09,
		0xC6, 0x41, 0x60, 0x01,
		0xE8, 0xDD, 0xFE, 0xFF, 0xFF,
		0x48, 0x83, 0xC4, 0x28,
		0xC3
	};
	const unsigned char expectedDistanceTestPrologue[] = {
		0x48, 0x83, 0xEC, 0x38,
		0xF3, 0x41, 0x0F, 0x10, 0x60, 0x04,
		0xF3, 0x41, 0x0F, 0x10, 0x48, 0x08,
		0xF3, 0x0F, 0x10, 0x2D, 0xA0, 0xCB, 0xFF, 0x02
	};

	bool levelInitInstalled = true;
	if (ShouldDisableStaticProps()) {
		levelInitInstalled = InstallEngineCommandLineCheckedHook(
			engineBase,
			0x18FFD0,
			expectedLevelInitPrologue,
			sizeof(expectedLevelInitPrologue),
			&CStaticPropMgrLoadStaticPropsNoop,
			reinterpret_cast<void**>(&CStaticPropMgrLoadStaticPropsOriginal),
			"-nostaticprops",
			"CStaticPropMgr::LevelInit");
	}
	const bool distanceTestInstalled = InstallEngineCommandLineCheckedHook(
		engineBase,
		0x18DC10,
		expectedDistanceTestPrologue,
		sizeof(expectedDistanceTestPrologue),
		&CStaticPropMgrStaticPropDistanceTestGuard,
		reinterpret_cast<void**>(&CStaticPropMgrStaticPropDistanceTestOriginal),
		"static-prop bounds",
		"CStaticPropMgr::DistanceTest");

	if (!distanceTestInstalled
		|| !CStaticPropMgrStaticPropDistanceTestOriginal) {
		constexpr DWORD kStaticPropBoundsInvariantFailure = 0xE042101E;
		const ULONG_PTR arguments[] = {
			static_cast<ULONG_PTR>(distanceTestInstalled),
			reinterpret_cast<ULONG_PTR>(
				CStaticPropMgrStaticPropDistanceTestOriginal),
		};
		OutputDebugStringA(
			"R1Delta: fatal static-prop distance guard installation failure\n");
		RaiseException(
			kStaticPropBoundsInvariantFailure,
			EXCEPTION_NONCONTINUABLE,
			static_cast<DWORD>(std::size(arguments)),
			arguments);
		TerminateProcess(
			GetCurrentProcess(),
			kStaticPropBoundsInvariantFailure);
		__assume(0);
	}

	s_NoStaticPropsHookInstalled =
		distanceTestInstalled && levelInitInstalled;
}

static void StripCollisionBSPClipBrushContents(__int64 collisionBspData)
{
	constexpr unsigned int kR1ClipBrushContentsMask = 0x230000;
	if (!collisionBspData)
		return;

	__try {
		auto* uniqueContents = *reinterpret_cast<unsigned int**>(collisionBspData + 0xB0);
		auto* brushContentIndices = *reinterpret_cast<unsigned char**>(collisionBspData + 0xC8);
		const int brushCount = *reinterpret_cast<int*>(collisionBspData + 0x100);
		if (!uniqueContents || !brushContentIndices || brushCount <= 0 || brushCount > 0x100000)
			return;

		bool touched[256] = {};
		int stripped = 0;
		int referenced = 0;
		for (int i = 0; i < brushCount; ++i) {
			const unsigned int contentIndex = brushContentIndices[i];
			if (touched[contentIndex])
				continue;

			touched[contentIndex] = true;
			++referenced;

			const unsigned int before = uniqueContents[contentIndex];
			const unsigned int after = before & ~kR1ClipBrushContentsMask;
			if (after != before) {
				uniqueContents[contentIndex] = after;
				++stripped;
			}
		}

		if (s_NoClipBrushesLogBudget > 0) {
			--s_NoClipBrushesLogBudget;
			char buffer[256];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: -noclipbrushes stripped clip bits from %d/%d brush-referenced collision content entries\n",
				stripped,
				referenced);
			OutputDebugStringA(buffer);
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		if (s_NoClipBrushesLogBudget > 0) {
			--s_NoClipBrushesLogBudget;
			OutputDebugStringA("R1Delta: -noclipbrushes failed while stripping collision content entries\n");
		}
	}
}

static __int64 __fastcall CollisionBSPDataBuildBrushContentsNoClipBrushes(__int64 collisionBspData)
{
	const __int64 result = CollisionBSPDataBuildBrushContentsOriginal
		? CollisionBSPDataBuildBrushContentsOriginal(collisionBspData)
		: 0;
	StripCollisionBSPClipBrushContents(collisionBspData);
	return result;
}

static void InstallNoClipBrushesHook(uintptr_t engineBase)
{
	if (s_NoClipBrushesHookInstalled || !engineBase || !ShouldDisableClipBrushes())
		return;

	const unsigned char expectedBuildBrushContentsPrologue[] = {
		0x48, 0x89, 0x5C, 0x24, 0x08,
		0x48, 0x89, 0x7C, 0x24, 0x10,
		0x4C, 0x8B, 0x91, 0x90, 0x00, 0x00, 0x00,
		0x33, 0xFF,
		0x4C, 0x8B, 0xD9,
		0x8B, 0xDF
	};

	s_NoClipBrushesHookInstalled = InstallEngineCommandLineCheckedHook(
		engineBase,
		0x109C10,
		expectedBuildBrushContentsPrologue,
		sizeof(expectedBuildBrushContentsPrologue),
		&CollisionBSPDataBuildBrushContentsNoClipBrushes,
		reinterpret_cast<void**>(&CollisionBSPDataBuildBrushContentsOriginal),
		"-noclipbrushes",
		"CollisionBSPData_BuildBrushContents");
}
static DedicatedSteamApplicationStartupFn DedicatedSteamApplicationStartupOriginal;

static void* LogCreateInterfaceCall(const char* moduleName, CreateInterfaceFn original, const char* name, int* returnCode)
{
	void* result = original ? original(name, returnCode) : nullptr;
	if (IsR1ODedicatedServer()) {
		char buffer[512];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: %s CreateInterface(%s) -> %p rc=%d\n",
			moduleName ? moduleName : "<module>",
			name ? name : "<null>",
			result,
			returnCode ? *returnCode : 0);
		OutputDebugStringA(buffer);
	}
	return result;
}

static int __fastcall StudioRenderAppSystemInitNoDx(void* thisptr)
{
	if (AreR1OFakeDediVerboseLogsEnabled())
		OutputDebugStringA("R1Delta: suppressed TFO studiorender IAppSystem::Init with INIT_OK for R1O fake dedi\n");
	return 1;
}

static int __fastcall VPhysicsAppSystemInitNoDx(void* thisptr)
{
	if (AreR1OFakeDediVerboseLogsEnabled())
		OutputDebugStringA("R1Delta: suppressed TFO vphysics IAppSystem::Init with INIT_OK for R1O fake dedi\n");
	return 1;
}

static int __fastcall DataCacheAppSystemInitNoDx(void* thisptr)
{
	if (AreR1OFakeDediVerboseLogsEnabled())
		OutputDebugStringA("R1Delta: suppressed TFO datacache IAppSystem::Init with INIT_OK for R1O fake dedi\n");
	return 1;
}

static int __fastcall InputSystemAppSystemInitNoDx(void* thisptr)
{
	if (AreR1OFakeDediVerboseLogsEnabled())
		OutputDebugStringA("R1Delta: suppressed TFO inputsystem IAppSystem::Init with INIT_OK for R1O fake dedi\n");
	return 1;
}

static bool PatchR1OHeadlessAppSystemInit(
	void* appSystem,
	void*** patchedVTable,
	R1OHeadlessAppSystemInitFn* original,
	void* replacement,
	const char* moduleName)
{
	if (!IsR1ODedicatedServer() || !appSystem || !patchedVTable || !original || !replacement)
		return false;

	auto** vtable = *reinterpret_cast<void***>(appSystem);
	if (!vtable)
		return false;
	if (*patchedVTable == vtable)
		return true;

	DWORD oldProtect = 0;
	void** const slot = &vtable[3]; // IAppSystem::Init
	if (!VirtualProtect(slot, sizeof(*slot), PAGE_EXECUTE_READWRITE, &oldProtect)) {
		char buffer[256];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: failed to patch %s IAppSystem::Init for headless R1O fake dedi, gle=%lu\n",
			moduleName ? moduleName : "<module>",
			GetLastError());
		OutputDebugStringA(buffer);
		return false;
	}

	*original = reinterpret_cast<R1OHeadlessAppSystemInitFn>(*slot);
	*slot = replacement;
	FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));
	DWORD ignored = 0;
	VirtualProtect(slot, sizeof(*slot), oldProtect, &ignored);
	*patchedVTable = vtable;
	return true;
}

static void* __cdecl StudioRenderCreateInterfaceHook(const char* name, int* returnCode)
{
	void* result = LogCreateInterfaceCall("studiorender", StudioRenderCreateInterfaceOriginal, name, returnCode);
	if (result && name && _stricmp(name, "VStudioRender026") == 0) {
		PatchR1OHeadlessAppSystemInit(
			result,
			&StudioRenderAppSystemVTable,
			&StudioRenderAppSystemInitOriginal,
			reinterpret_cast<void*>(&StudioRenderAppSystemInitNoDx),
			"studiorender");
	}
	return result;
}

static void* __cdecl VPhysicsCreateInterfaceHook(const char* name, int* returnCode)
{
	void* result = LogCreateInterfaceCall("vphysics", VPhysicsCreateInterfaceOriginal, name, returnCode);
	if (result && name && _stricmp(name, "VPhysics031") == 0) {
		PatchR1OHeadlessAppSystemInit(
			result,
			&VPhysicsAppSystemVTable,
			&VPhysicsAppSystemInitOriginal,
			reinterpret_cast<void*>(&VPhysicsAppSystemInitNoDx),
			"vphysics");
	}
	return result;
}

static void* __cdecl DataCacheCreateInterfaceHook(const char* name, int* returnCode)
{
	void* result = LogCreateInterfaceCall("datacache", DataCacheCreateInterfaceOriginal, name, returnCode);
	if (!result || !name)
		return result;

	if (_stricmp(name, "VDataCache003") == 0) {
		PatchR1OHeadlessAppSystemInit(
			result,
			&DataCacheAppSystemVTable,
			&DataCacheAppSystemInitOriginal,
			reinterpret_cast<void*>(&DataCacheAppSystemInitNoDx),
			"datacache");
	}
	else if (_stricmp(name, "MDLCache004") == 0) {
		PatchR1OHeadlessAppSystemInit(
			result,
			&MDLCacheAppSystemVTable,
			&MDLCacheAppSystemInitOriginal,
			reinterpret_cast<void*>(&DataCacheAppSystemInitNoDx),
			"mdlcache");
	}
	else if (_stricmp(name, "VStudioDataCache005") == 0) {
		PatchR1OHeadlessAppSystemInit(
			result,
			&StudioDataCacheAppSystemVTable,
			&StudioDataCacheAppSystemInitOriginal,
			reinterpret_cast<void*>(&DataCacheAppSystemInitNoDx),
			"studiodatacache");
	}
	return result;
}

static void* __cdecl VGui2CreateInterfaceHook(const char* name, int* returnCode)
{
	return LogCreateInterfaceCall("vgui2", VGui2CreateInterfaceOriginal, name, returnCode);
}

static void* __cdecl InputSystemCreateInterfaceHook(const char* name, int* returnCode)
{
	void* result = LogCreateInterfaceCall("inputsystem", InputSystemCreateInterfaceOriginal, name, returnCode);
	if (result && name && _stricmp(name, "InputSystemVersion001") == 0) {
		PatchR1OHeadlessAppSystemInit(
			result,
			&InputSystemAppSystemVTable,
			&InputSystemAppSystemInitOriginal,
			reinterpret_cast<void*>(&InputSystemAppSystemInitNoDx),
			"inputsystem");
	}
	return result;
}

static void HookCreateInterfaceExport(uintptr_t moduleBase, const char* moduleName, CreateInterfaceFn hook, CreateInterfaceFn* original)
{
	if (!IsR1ODedicatedServer() || !moduleBase || !hook || !original || *original)
		return;

	auto target = reinterpret_cast<CreateInterfaceFn>(GetProcAddress(reinterpret_cast<HMODULE>(moduleBase), "CreateInterface"));
	if (!target)
		return;

	MH_STATUS status = MH_CreateHook(
		reinterpret_cast<LPVOID>(target),
		hook,
		reinterpret_cast<LPVOID*>(original));

	char buffer[384];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: %s CreateInterface hook status=%d target=%p orig=%p\n",
		moduleName ? moduleName : "<module>",
		static_cast<int>(status),
		reinterpret_cast<void*>(target),
		reinterpret_cast<void*>(*original));
	OutputDebugStringA(buffer);
	MH_EnableHook(MH_ALL_HOOKS);
}

static void* __cdecl DedicatedCreateInterfaceHook(const char* name, int* returnCode)
{
	void* result = DedicatedCreateInterfaceOriginal
		? DedicatedCreateInterfaceOriginal(name, returnCode)
		: nullptr;

	if (IsR1ODedicatedServer()) {
		char buffer[512];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: dedicated CreateInterface(%s) -> %p rc=%d\n",
			name ? name : "<null>",
			result,
			returnCode ? *returnCode : 0);
		OutputDebugStringA(buffer);
	}

	return result;
}

static __int64 __fastcall DedicatedSetupSteamSearchPathsHook(__int64 setupInfo)
{
	if (!IsR1ODedicatedServer())
		return DedicatedSetupSteamSearchPathsOriginal
			? DedicatedSetupSteamSearchPathsOriginal(setupInfo)
			: 0;

	char buffer[192];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: skipped R1 dedicated Steam filesystem mount setup for R1O dedi, setup=%p\n",
		reinterpret_cast<void*>(setupInfo));
	OutputDebugStringA(buffer);
	return 0;
}

static uintptr_t ReadDedicatedGlobalPtr(uintptr_t imageRva)
{
	const uintptr_t dedicatedBase = reinterpret_cast<uintptr_t>(GetModuleHandleA("dedicated.dll"));
	if (!dedicatedBase)
		return 0;
	return *reinterpret_cast<uintptr_t*>(dedicatedBase + imageRva);
}

static bool __fastcall DedicatedTierAppStartupHook(const char* appName)
{
	const bool result = DedicatedTierAppStartupOriginal
		? DedicatedTierAppStartupOriginal(appName)
		: true;

	if (!IsR1ODedicatedServer())
		return result;

	const uintptr_t tier0 = ReadDedicatedGlobalPtr(0x2956D8);
	const uintptr_t tier1a = ReadDedicatedGlobalPtr(0x2956C0);
	const uintptr_t tier1b = ReadDedicatedGlobalPtr(0x2956D0);
	const uintptr_t tier1c = ReadDedicatedGlobalPtr(0x2956C8);
	const uintptr_t tier2a = ReadDedicatedGlobalPtr(0x2956E8);
	const uintptr_t tier2b = ReadDedicatedGlobalPtr(0x2956F0);

	char buffer[512];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: dedicated Tier app startup app=%s result=%d globals=%p,%p,%p,%p,%p,%p%s\n",
		appName ? appName : "<null>",
		result ? 1 : 0,
		reinterpret_cast<void*>(tier0),
		reinterpret_cast<void*>(tier1a),
		reinterpret_cast<void*>(tier1b),
		reinterpret_cast<void*>(tier1c),
		reinterpret_cast<void*>(tier2a),
		reinterpret_cast<void*>(tier2b),
		result ? "" : " forcing success for R1O fake dedi");
	OutputDebugStringA(buffer);

	return true;
}

static bool __fastcall DedicatedVguiSteamAppPreInitHook()
{
	const bool result = DedicatedVguiSteamAppPreInitOriginal
		? DedicatedVguiSteamAppPreInitOriginal()
		: false;

	if (!IsR1ODedicatedServer())
		return result;

	char buffer[192];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: CVguiSteamApp::PreInit result=%d for R1O fake dedi\n",
		result ? 1 : 0);
	OutputDebugStringA(buffer);
	return result;
}

static bool RawProcessCommandLineHasFlag(const wchar_t* flag)
{
	int argumentCount = 0;
	LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
	if (!arguments)
		return false;

	bool found = false;
	for (int i = 1; i < argumentCount; ++i) {
		if (_wcsicmp(arguments[i], flag) == 0) {
			found = true;
			break;
		}
	}

	LocalFree(arguments);
	return found;
}

static unsigned char ApplyDedicatedUIModeByte(uintptr_t dedicatedBase)
{
	// The dedicated bootstrap appends -console to tier0's parsed command
	// line. Use the immutable OS process arguments so an actual launch
	// without -console still selects the original VGUI dedicated mode.
	const bool consoleMode = RawProcessCommandLineHasFlag(L"-console");
	const unsigned char dedicatedVGUIMode = consoleMode ? 0 : 1;
	if (dedicatedBase)
		*reinterpret_cast<unsigned char*>(dedicatedBase + 0x293288) = dedicatedVGUIMode;
	return dedicatedVGUIMode;
}

static int __fastcall DedicatedConsoleOutputHook(void* sys, const char* text)
{
	const uintptr_t dedicatedBase =
		reinterpret_cast<uintptr_t>(GetModuleHandleA("dedicated.dll"));
	if (IsR1ODedicatedServer()
		&& dedicatedBase
		&& *reinterpret_cast<unsigned char*>(dedicatedBase + 0x293288) != 0) {
		const uintptr_t adminDialog =
			*reinterpret_cast<uintptr_t*>(dedicatedBase + 0x295270);
		const bool routesToStartupMessageBox =
			!adminDialog
			|| *reinterpret_cast<unsigned char*>(adminDialog + 634) != 0;
		if (routesToStartupMessageBox && text && *text) {
			const void* const caller = _ReturnAddress();
			HMODULE callerModule = nullptr;
			char callerModulePath[MAX_PATH]{};
			if (GetModuleHandleExA(
					GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
						| GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCSTR>(caller),
					&callerModule)) {
				GetModuleFileNameA(
					callerModule,
					callerModulePath,
					static_cast<DWORD>(std::size(callerModulePath)));
			}

			char prefix[2 * MAX_PATH]{};
			_snprintf_s(
				prefix,
				sizeof(prefix),
				_TRUNCATE,
				"R1Delta: early dedicated VGUI console output caller=%p "
				"module=%s+0x%llX text=",
				caller,
				callerModulePath[0] ? callerModulePath : "<unknown>",
				callerModule
					? static_cast<unsigned long long>(
						reinterpret_cast<uintptr_t>(caller)
						- reinterpret_cast<uintptr_t>(callerModule))
					: 0ULL);
			OutputDebugStringA(prefix);
			OutputDebugStringA(text);
			if (text[strlen(text) - 1] != '\n')
				OutputDebugStringA("\n");
		}
	}

	return DedicatedConsoleOutputOriginal
		? DedicatedConsoleOutputOriginal(sys, text)
		: 0;
}

static bool __fastcall DedicatedInitConsoleAndFilesystemHook()
{
	if (IsR1ODedicatedServer()) {
		// CDedicatedAppSystemGroup::PreInit normally calls the base VGUI app
		// PreInit before its R1-only filesystem and NET_Init work. We replace
		// the enclosing function for R1O, so explicitly preserve that base
		// call; otherwise the dedicated VGUI globals remain null.
		const bool vguiPreInitResult = DedicatedVguiSteamAppPreInitHook();
		if (!vguiPreInitResult) {
			OutputDebugStringA(
				"R1Delta: CVguiSteamApp::PreInit failed while skipping R1 dedicated "
				"console/filesystem pre-init for R1O fake dedi\n");
			return false;
		}

		const uintptr_t dedicatedBase = reinterpret_cast<uintptr_t>(GetModuleHandleA("dedicated.dll"));
		// DedicatedInitConsoleAndFilesystem normally maintains this byte.
		// R1O fake-dedi skips the rest of that R1-only initialization, but
		// still needs the mode selection consumed by the dedicated UI path.
		const unsigned char dedicatedVGUIMode = ApplyDedicatedUIModeByte(dedicatedBase);

		char buffer[256];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: skipped R1 dedicated console/filesystem pre-init for R1O fake dedi; "
			"CVguiSteamApp::PreInit=%d dedicated VGUI mode=%d byte=%p\n",
			vguiPreInitResult ? 1 : 0,
			static_cast<int>(dedicatedVGUIMode),
			reinterpret_cast<void*>(dedicatedBase ? dedicatedBase + 0x293288 : 0));
		OutputDebugStringA(buffer);
		return true;
	}

	const bool result = DedicatedInitConsoleAndFilesystemOriginal
		? DedicatedInitConsoleAndFilesystemOriginal()
		: true;

	return result;
}

static __int64 __fastcall DedicatedSharedServerVPKSetupHook()
{
	if (!IsR1ODedicatedServer())
		return DedicatedSharedServerVPKSetupOriginal
			? DedicatedSharedServerVPKSetupOriginal()
			: 0;

	OutputDebugStringA("R1Delta: skipped R1 dedicated shared-server-VPK setup for R1O fake dedi\n");
	return 0;
}

static __int64 __fastcall DedicatedSteamApplicationStartupHook(__int64 app)
{
	const __int64 result = DedicatedSteamApplicationStartupOriginal
		? DedicatedSteamApplicationStartupOriginal(app)
		: 1;

	if (!IsR1ODedicatedServer())
		return result;

	int stage = -1;
	if (app)
		stage = *reinterpret_cast<int*>(app + 160);

	// CSteamApplication::Startup stopped in the app-system Connect phase for
	// the mixed R1/TFO fake-dedi graph. The old fallback jumped the startup
	// state straight from stage 2 to stage 6, which also skipped the app
	// group's PreInit virtual. Preserve the required CVguiSteamApp base
	// PreInit here before bypassing the incompatible R1-only remainder.
	bool vguiPreInitResult = true;
	if (result != 1 && stage == 2)
		vguiPreInitResult = DedicatedVguiSteamAppPreInitHook();

	char buffer[256];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: dedicated steam application startup result=%lld stage=%d "
		"CVguiSteamApp::PreInit=%d%s\n",
		static_cast<long long>(result),
		stage,
		vguiPreInitResult ? 1 : 0,
		result == 1
			? ""
			: (vguiPreInitResult
				? " forcing stage 6 for R1O fake dedi"
				: " leaving startup failed"));
	OutputDebugStringA(buffer);

	if (result != 1 && !vguiPreInitResult)
		return result;

	if (result != 1 && app)
		*reinterpret_cast<int*>(app + 160) = 6;
	return result == 1 ? result : 1;
}

static bool IsEngineDllRequest(const char* path)
{
	if (!path)
		return false;

	const char* base = path;
	for (const char* scan = path; *scan; ++scan) {
		if (*scan == '\\' || *scan == '/')
			base = scan + 1;
	}

	return _stricmp(base, "engine.dll") == 0 || _stricmp(base, "engine_ds.dll") == 0;
}

static bool IsEngineDllRequest(const wchar_t* path)
{
	if (!path)
		return false;

	const wchar_t* base = path;
	for (const wchar_t* scan = path; *scan; ++scan) {
		if (*scan == L'\\' || *scan == L'/')
			base = scan + 1;
	}

	return _wcsicmp(base, L"engine.dll") == 0 || _wcsicmp(base, L"engine_ds.dll") == 0;
}

static bool IsMaterialSystemNoDxRequest(const char* path)
{
	if (!path)
		return false;

	const char* base = path;
	for (const char* scan = path; *scan; ++scan) {
		if (*scan == '\\' || *scan == '/')
			base = scan + 1;
	}

	return _stricmp(base, "materialsystem_nodx.dll") == 0;
}

static bool IsMaterialSystemNoDxRequest(const wchar_t* path)
{
	if (!path)
		return false;

	const wchar_t* base = path;
	for (const wchar_t* scan = path; *scan; ++scan) {
		if (*scan == L'\\' || *scan == L'/')
			base = scan + 1;
	}

	return _wcsicmp(base, L"materialsystem_nodx.dll") == 0;
}

static std::string GetBinDeltaModulePathA(const char* moduleName)
{
	char tier0Path[MAX_PATH]{};
	HMODULE tier0 = GetModuleHandleA("tier0.dll");
	if (!tier0 || !GetModuleFileNameA(tier0, tier0Path, sizeof(tier0Path)))
		return moduleName;

	char* lastSlash = strrchr(tier0Path, '\\');
	if (!lastSlash)
		return moduleName;

	lastSlash[1] = '\0';
	return std::string(tier0Path) + moduleName;
}

static std::wstring GetBinDeltaModulePathW(const wchar_t* moduleName)
{
	wchar_t tier0Path[MAX_PATH]{};
	HMODULE tier0 = GetModuleHandleW(L"tier0.dll");
	if (!tier0 || !GetModuleFileNameW(tier0, tier0Path, std::size(tier0Path)))
		return moduleName;

	wchar_t* lastSlash = wcsrchr(tier0Path, L'\\');
	if (!lastSlash)
		return moduleName;

	lastSlash[1] = L'\0';
	return std::wstring(tier0Path) + moduleName;
}

static const char* BaseNameA(const char* path)
{
	if (!path)
		return "";

	const char* base = path;
	for (const char* scan = path; *scan; ++scan) {
		if (*scan == '\\' || *scan == '/')
			base = scan + 1;
	}
	return base;
}

static const wchar_t* BaseNameW(const wchar_t* path)
{
	if (!path)
		return L"";

	const wchar_t* base = path;
	for (const wchar_t* scan = path; *scan; ++scan) {
		if (*scan == L'\\' || *scan == L'/')
			base = scan + 1;
	}
	return base;
}

static bool IsR1OTFODllRequest(const char* path)
{
	const char* base = BaseNameA(path);
	static const char* const modules[] = {
		"datacache.dll",
		"filesystem_stdio.dll",
		"launcher.dll",
		"studiorender.dll",
		"vguimatsurface.dll",
		"vphysics.dll",
	};

	for (const char* module : modules) {
		if (_stricmp(base, module) == 0)
			return true;
	}
	return false;
}

static bool IsR1OTFODllRequest(const wchar_t* path)
{
	const wchar_t* base = BaseNameW(path);
	static const wchar_t* const modules[] = {
		L"datacache.dll",
		L"filesystem_stdio.dll",
		L"launcher.dll",
		L"studiorender.dll",
		L"vguimatsurface.dll",
		L"vphysics.dll",
	};

	for (const wchar_t* module : modules) {
		if (_wcsicmp(base, module) == 0)
			return true;
	}
	return false;
}

static HMODULE LoadR1OTFODllA(LPCSTR path, HANDLE file, DWORD flags)
{
	const char* base = BaseNameA(path);
	HMODULE existing = GetModuleHandleA(base);
	if (existing)
		return existing;

	const std::string redirected = r1delta::r1o::ResolveTFOModulePathA(base);

	HMODULE loaded = nullptr;
	if (!redirected.empty() && DedicatedLoadLibraryExAOriginal)
		loaded = DedicatedLoadLibraryExAOriginal(redirected.c_str(), file, flags | LOAD_WITH_ALTERED_SEARCH_PATH);
	else if (!redirected.empty() && DedicatedLoadLibraryAOriginal)
		loaded = DedicatedLoadLibraryAOriginal(redirected.c_str());

	const std::string validationError = r1delta::r1o::TFORuntimeValidationErrorA();
	char buffer[1024];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: redirected R1O fake-dedi TFO DLL load request=%s redirected=%s result=%p gle=%lu validation=%s\n",
		path ? path : "<null>",
		redirected.empty() ? "<unresolved>" : redirected.c_str(),
		loaded,
		loaded ? 0 : GetLastError(),
		validationError.empty() ? "<ok>" : validationError.c_str());
	OutputDebugStringA(buffer);
	return loaded;
}

static HMODULE LoadR1OTFODllW(LPCWSTR path, HANDLE file, DWORD flags)
{
	const wchar_t* base = BaseNameW(path);
	HMODULE existing = GetModuleHandleW(base);
	if (existing)
		return existing;

	const std::wstring redirected = r1delta::r1o::ResolveTFOModulePathW(base);

	HMODULE loaded = nullptr;
	if (!redirected.empty() && DedicatedLoadLibraryExWOriginal)
		loaded = DedicatedLoadLibraryExWOriginal(redirected.c_str(), file, flags | LOAD_WITH_ALTERED_SEARCH_PATH);
	else if (!redirected.empty() && DedicatedLoadLibraryWOriginal)
		loaded = DedicatedLoadLibraryWOriginal(redirected.c_str());

	char requestUtf8[MAX_PATH * 2]{};
	char redirectedUtf8[MAX_PATH * 2]{};
	WideCharToMultiByte(CP_UTF8, 0, path ? path : L"<null>", -1, requestUtf8, sizeof(requestUtf8), nullptr, nullptr);
	WideCharToMultiByte(CP_UTF8, 0, redirected.c_str(), -1, redirectedUtf8, sizeof(redirectedUtf8), nullptr, nullptr);

	const std::string validationError = r1delta::r1o::TFORuntimeValidationErrorA();
	char buffer[1024];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: redirected R1O fake-dedi TFO DLL load request=%s redirected=%s result=%p gle=%lu validation=%s\n",
		requestUtf8,
		redirected.empty() ? "<unresolved>" : redirectedUtf8,
		loaded,
		loaded ? 0 : GetLastError(),
		validationError.empty() ? "<ok>" : validationError.c_str());
	OutputDebugStringA(buffer);
	return loaded;
}

static HMODULE LoadR1ODedicatedEngineA(HANDLE file, DWORD flags)
{
	HMODULE existing = GetModuleHandleA("engine_r1o.dll");
	if (existing) {
		G_engine_r1o = (uintptr_t)existing;
		G_engine = G_engine_r1o;
		return existing;
	}

	HMODULE loaded = DedicatedLoadLibraryExAOriginal
		? DedicatedLoadLibraryExAOriginal("engine_r1o.dll", file, flags)
		: DedicatedLoadLibraryAOriginal("engine_r1o.dll");
	if (loaded) {
		G_engine_r1o = (uintptr_t)loaded;
		G_engine = G_engine_r1o;
	}
	else {
		OutputDebugStringA("R1Delta: failed to redirect dedicated engine load to engine_r1o.dll\n");
	}
	return loaded;
}

static HMODULE LoadR1ODedicatedEngineW(HANDLE file, DWORD flags)
{
	HMODULE existing = GetModuleHandleA("engine_r1o.dll");
	if (existing) {
		G_engine_r1o = (uintptr_t)existing;
		G_engine = G_engine_r1o;
		return existing;
	}

	HMODULE loaded = DedicatedLoadLibraryExWOriginal
		? DedicatedLoadLibraryExWOriginal(L"engine_r1o.dll", file, flags)
		: DedicatedLoadLibraryWOriginal(L"engine_r1o.dll");
	if (loaded) {
		G_engine_r1o = (uintptr_t)loaded;
		G_engine = G_engine_r1o;
	}
	else {
		OutputDebugStringA("R1Delta: failed to redirect dedicated engine load to engine_r1o.dll\n");
	}
	return loaded;
}

static HMODULE LoadR1ODedicatedMaterialSystemNoDxA(HANDLE file, DWORD flags)
{
	HMODULE existing = GetModuleHandleA("materialsystem_nodx.dll");
	if (existing)
		return existing;

	const std::string proxyPath = GetBinDeltaModulePathA("materialsystem_nodx.dll");
	HMODULE loaded = DedicatedLoadLibraryExAOriginal
		? DedicatedLoadLibraryExAOriginal(proxyPath.c_str(), file, flags)
		: DedicatedLoadLibraryAOriginal(proxyPath.c_str());
	if (!loaded) {
		OutputDebugStringA("R1Delta: failed to redirect materialsystem_nodx.dll load to bin_delta proxy\n");
	}
	return loaded;
}

static HMODULE LoadR1ODedicatedMaterialSystemNoDxW(HANDLE file, DWORD flags)
{
	HMODULE existing = GetModuleHandleA("materialsystem_nodx.dll");
	if (existing)
		return existing;

	const std::wstring proxyPath = GetBinDeltaModulePathW(L"materialsystem_nodx.dll");
	HMODULE loaded = DedicatedLoadLibraryExWOriginal
		? DedicatedLoadLibraryExWOriginal(proxyPath.c_str(), file, flags)
		: DedicatedLoadLibraryWOriginal(proxyPath.c_str());
	if (!loaded) {
		OutputDebugStringA("R1Delta: failed to redirect materialsystem_nodx.dll load to bin_delta proxy\n");
	}
	return loaded;
}

static HMODULE WINAPI DedicatedLoadLibraryA(LPCSTR path)
{
	if (IsR1ODedicatedServer() && IsEngineDllRequest(path))
		return LoadR1ODedicatedEngineA(nullptr, 0);
	if (IsR1ODedicatedServer() && IsMaterialSystemNoDxRequest(path))
		return LoadR1ODedicatedMaterialSystemNoDxA(nullptr, 0);
	if (IsR1ODedicatedServer() && IsR1OTFODllRequest(path))
		return LoadR1OTFODllA(path, nullptr, 0);
	HMODULE result = DedicatedLoadLibraryAOriginal(path);
	if (IsR1ODedicatedServer()) {
		char buffer[512];
		_snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "R1Delta: LoadLibraryA(%s) -> %p gle=%lu\n", path ? path : "<null>", result, result ? 0 : GetLastError());
		OutputDebugStringA(buffer);
	}
	return result;
}

static HMODULE WINAPI DedicatedLoadLibraryW(LPCWSTR path)
{
	if (IsR1ODedicatedServer() && IsEngineDllRequest(path))
		return LoadR1ODedicatedEngineW(nullptr, 0);
	if (IsR1ODedicatedServer() && IsMaterialSystemNoDxRequest(path))
		return LoadR1ODedicatedMaterialSystemNoDxW(nullptr, 0);
	if (IsR1ODedicatedServer() && IsR1OTFODllRequest(path))
		return LoadR1OTFODllW(path, nullptr, 0);
	HMODULE result = DedicatedLoadLibraryWOriginal(path);
	if (IsR1ODedicatedServer()) {
		char buffer[512];
		_snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "R1Delta: LoadLibraryW(%ls) -> %p gle=%lu\n", path ? path : L"<null>", result, result ? 0 : GetLastError());
		OutputDebugStringA(buffer);
	}
	return result;
}

static HMODULE WINAPI DedicatedLoadLibraryExA(LPCSTR path, HANDLE file, DWORD flags)
{
	if (IsR1ODedicatedServer() && IsEngineDllRequest(path))
		return LoadR1ODedicatedEngineA(file, flags);
	if (IsR1ODedicatedServer() && IsMaterialSystemNoDxRequest(path))
		return LoadR1ODedicatedMaterialSystemNoDxA(file, flags);
	if (IsR1ODedicatedServer() && IsR1OTFODllRequest(path))
		return LoadR1OTFODllA(path, file, flags);
	HMODULE result = DedicatedLoadLibraryExAOriginal(path, file, flags);
	if (IsR1ODedicatedServer()) {
		char buffer[512];
		_snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "R1Delta: LoadLibraryExA(%s, flags=0x%lx) -> %p gle=%lu\n", path ? path : "<null>", flags, result, result ? 0 : GetLastError());
		OutputDebugStringA(buffer);
	}
	return result;
}

static HMODULE WINAPI DedicatedLoadLibraryExW(LPCWSTR path, HANDLE file, DWORD flags)
{
	if (IsR1ODedicatedServer() && IsEngineDllRequest(path))
		return LoadR1ODedicatedEngineW(file, flags);
	if (IsR1ODedicatedServer() && IsMaterialSystemNoDxRequest(path))
		return LoadR1ODedicatedMaterialSystemNoDxW(file, flags);
	if (IsR1ODedicatedServer() && IsR1OTFODllRequest(path))
		return LoadR1OTFODllW(path, file, flags);
	HMODULE result = DedicatedLoadLibraryExWOriginal(path, file, flags);
	if (IsR1ODedicatedServer()) {
		char buffer[512];
		_snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "R1Delta: LoadLibraryExW(%ls, flags=0x%lx) -> %p gle=%lu\n", path ? path : L"<null>", flags, result, result ? 0 : GetLastError());
		OutputDebugStringA(buffer);
	}
	return result;
}

template <typename Fn>
static bool PatchImportedFunction(uintptr_t moduleBase, const char* functionName, Fn hook, Fn* original)
{
	auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(moduleBase);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE)
		return false;

	auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(moduleBase + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE)
		return false;

	const auto& importDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (!importDir.VirtualAddress || !importDir.Size)
		return false;

	bool patched = false;
	auto imports = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(moduleBase + importDir.VirtualAddress);
	for (; imports->Name; ++imports) {
		auto thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(moduleBase +
			(imports->OriginalFirstThunk ? imports->OriginalFirstThunk : imports->FirstThunk));
		auto iat = reinterpret_cast<IMAGE_THUNK_DATA*>(moduleBase + imports->FirstThunk);

		for (; thunk->u1.AddressOfData; ++thunk, ++iat) {
			if (IMAGE_SNAP_BY_ORDINAL(thunk->u1.Ordinal))
				continue;

			auto importByName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(moduleBase + thunk->u1.AddressOfData);
			if (strcmp(reinterpret_cast<const char*>(importByName->Name), functionName) != 0)
				continue;

			DWORD oldProtect = 0;
			if (!VirtualProtect(&iat->u1.Function, sizeof(iat->u1.Function), PAGE_READWRITE, &oldProtect))
				continue;

			if (!*original)
				*original = reinterpret_cast<Fn>(iat->u1.Function);
			iat->u1.Function = reinterpret_cast<ULONG_PTR>(hook);
			VirtualProtect(&iat->u1.Function, sizeof(iat->u1.Function), oldProtect, &oldProtect);
			patched = true;
		}
	}

	return patched;
}

void HookDedicatedEngineLoader(uintptr_t dedicatedBase)
{
	if (!IsR1ODedicatedServer())
		return;
	if (!dedicatedBase)
		return;

	// The R1O mixed app-system startup can stop before the dedicated app-group
	// PreInit virtual. Apply the mode side effect as soon as dedicated.dll is
	// available so either startup branch sees the raw process selection.
	const unsigned char dedicatedVGUIMode = ApplyDedicatedUIModeByte(dedicatedBase);
	char uiModeBuffer[224];
	_snprintf_s(
		uiModeBuffer,
		sizeof(uiModeBuffer),
		_TRUNCATE,
		"R1Delta: initialized R1O fake-dedi UI mode=%d byte=%p from raw process arguments\n",
		static_cast<int>(dedicatedVGUIMode),
		reinterpret_cast<void*>(dedicatedBase + 0x293288));
	OutputDebugStringA(uiModeBuffer);

	if (!DedicatedConsoleOutputOriginal) {
		const MH_STATUS consoleOutputStatus = MH_CreateHook(
			reinterpret_cast<LPVOID>(dedicatedBase + 0x6B800),
			&DedicatedConsoleOutputHook,
			reinterpret_cast<LPVOID*>(&DedicatedConsoleOutputOriginal));
		char buffer[256];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: dedicated early VGUI console-output hook status=%d "
			"target=%p orig=%p\n",
			static_cast<int>(consoleOutputStatus),
			reinterpret_cast<void*>(dedicatedBase + 0x6B800),
			reinterpret_cast<void*>(DedicatedConsoleOutputOriginal));
		OutputDebugStringA(buffer);
	}

	PatchImportedFunction(dedicatedBase, "LoadLibraryA", &DedicatedLoadLibraryA, &DedicatedLoadLibraryAOriginal);
	PatchImportedFunction(dedicatedBase, "LoadLibraryW", &DedicatedLoadLibraryW, &DedicatedLoadLibraryWOriginal);
	PatchImportedFunction(dedicatedBase, "LoadLibraryExA", &DedicatedLoadLibraryExA, &DedicatedLoadLibraryExAOriginal);
	PatchImportedFunction(dedicatedBase, "LoadLibraryExW", &DedicatedLoadLibraryExW, &DedicatedLoadLibraryExWOriginal);
	if (!DedicatedSetupSteamSearchPathsOriginal) {
		MH_STATUS setupSearchPathsStatus = MH_CreateHook(
			reinterpret_cast<LPVOID>(dedicatedBase + 0x669A0),
			&DedicatedSetupSteamSearchPathsHook,
			reinterpret_cast<LPVOID*>(&DedicatedSetupSteamSearchPathsOriginal));
		char buffer[256];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: dedicated Steam filesystem setup hook status=%d target=%p orig=%p\n",
			static_cast<int>(setupSearchPathsStatus),
			reinterpret_cast<void*>(dedicatedBase + 0x669A0),
			reinterpret_cast<void*>(DedicatedSetupSteamSearchPathsOriginal));
		OutputDebugStringA(buffer);
	}
	if (!DedicatedTierAppStartupOriginal) {
		MH_STATUS tierAppStartupStatus = MH_CreateHook(
			reinterpret_cast<LPVOID>(dedicatedBase + 0xDB300),
			&DedicatedTierAppStartupHook,
			reinterpret_cast<LPVOID*>(&DedicatedTierAppStartupOriginal));
		char buffer[256];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: dedicated Tier app startup hook status=%d target=%p orig=%p\n",
			static_cast<int>(tierAppStartupStatus),
			reinterpret_cast<void*>(dedicatedBase + 0xDB300),
			reinterpret_cast<void*>(DedicatedTierAppStartupOriginal));
		OutputDebugStringA(buffer);
	}
	if (!DedicatedVguiSteamAppPreInitOriginal) {
		MH_STATUS vguiSteamAppPreInitStatus = MH_CreateHook(
			reinterpret_cast<LPVOID>(dedicatedBase + 0x69260),
			&DedicatedVguiSteamAppPreInitHook,
			reinterpret_cast<LPVOID*>(&DedicatedVguiSteamAppPreInitOriginal));
		char buffer[256];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: CVguiSteamApp::PreInit hook status=%d target=%p orig=%p\n",
			static_cast<int>(vguiSteamAppPreInitStatus),
			reinterpret_cast<void*>(dedicatedBase + 0x69260),
			reinterpret_cast<void*>(DedicatedVguiSteamAppPreInitOriginal));
		OutputDebugStringA(buffer);
	}
	if (!DedicatedInitConsoleAndFilesystemOriginal) {
		MH_STATUS initConsoleAndFilesystemStatus = MH_CreateHook(
			reinterpret_cast<LPVOID>(dedicatedBase + 0x69780),
			&DedicatedInitConsoleAndFilesystemHook,
			reinterpret_cast<LPVOID*>(&DedicatedInitConsoleAndFilesystemOriginal));
		char buffer[256];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: dedicated init console/filesystem hook status=%d target=%p orig=%p\n",
			static_cast<int>(initConsoleAndFilesystemStatus),
			reinterpret_cast<void*>(dedicatedBase + 0x69780),
			reinterpret_cast<void*>(DedicatedInitConsoleAndFilesystemOriginal));
		OutputDebugStringA(buffer);
	}
	if (!DedicatedSharedServerVPKSetupOriginal) {
		MH_STATUS sharedServerVPKStatus = MH_CreateHook(
			reinterpret_cast<LPVOID>(dedicatedBase + 0x69870),
			&DedicatedSharedServerVPKSetupHook,
			reinterpret_cast<LPVOID*>(&DedicatedSharedServerVPKSetupOriginal));
		char buffer[256];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: dedicated shared-server-VPK setup hook status=%d target=%p orig=%p\n",
			static_cast<int>(sharedServerVPKStatus),
			reinterpret_cast<void*>(dedicatedBase + 0x69870),
			reinterpret_cast<void*>(DedicatedSharedServerVPKSetupOriginal));
		OutputDebugStringA(buffer);
	}
	if (!DedicatedSteamApplicationStartupOriginal) {
		MH_STATUS steamApplicationStartupStatus = MH_CreateHook(
			reinterpret_cast<LPVOID>(dedicatedBase + 0xAB160),
			&DedicatedSteamApplicationStartupHook,
			reinterpret_cast<LPVOID*>(&DedicatedSteamApplicationStartupOriginal));
		char buffer[256];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: dedicated steam application startup hook status=%d target=%p orig=%p\n",
			static_cast<int>(steamApplicationStartupStatus),
			reinterpret_cast<void*>(dedicatedBase + 0xAB160),
			reinterpret_cast<void*>(DedicatedSteamApplicationStartupOriginal));
		OutputDebugStringA(buffer);
	}
	if (!DedicatedCreateInterfaceOriginal) {
		if (auto createInterface = reinterpret_cast<CreateInterfaceFn>(GetProcAddress(reinterpret_cast<HMODULE>(dedicatedBase), "CreateInterface"))) {
			MH_STATUS createInterfaceStatus = MH_CreateHook(
				reinterpret_cast<LPVOID>(createInterface),
				&DedicatedCreateInterfaceHook,
				reinterpret_cast<LPVOID*>(&DedicatedCreateInterfaceOriginal));
			char buffer[256];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: dedicated CreateInterface hook status=%d target=%p orig=%p\n",
				static_cast<int>(createInterfaceStatus),
				reinterpret_cast<void*>(createInterface),
				reinterpret_cast<void*>(DedicatedCreateInterfaceOriginal));
			OutputDebugStringA(buffer);
		}
	}
	OutputDebugStringA("R1Delta: installed R1O dedicated engine load redirection\n");
}

__int64 (*oFileSystem_AddLoadedSearchPath)(
	__int64 a1,
	unsigned __int8* a2,
	_BYTE* a3,
	char* a4,
	char* Source,
	char a6);
__int64 FileSystem_AddLoadedSearchPath(
	__int64 a1,
	unsigned __int8* a2, // Often 'byte*' or 'unsigned char*'
	_BYTE* a3,         // Often 'byte*' or 'unsigned char*'
	char* a4,          // The path string we are interested in
	char* Source,
	char a6)
{
	const char* suffix = "r1delta";
	const size_t suffix_len = 7; // strlen("r1delta")

	// Store the original value of a4, as we might need it later.
	char* original_a4 = a4;
	// Prepare the value to be passed to the original function.
	// Default to nullifying a4, unless the specific conditions are met.
	char* result_a4 = nullptr; // Use nullptr for modern C++, or 0 for C/older C++

	// --- Start checking the conditions under which a4 should *NOT* be nullified ---
	bool keep_original_path = false;
	if (original_a4) // Check if original_a4 is not NULL
	{
		size_t path_len = strlen(original_a4);
		if (path_len >= suffix_len)
		{
			// Point to the potential start of the suffix within original_a4
			const char* end_of_path = original_a4 + (path_len - suffix_len);

			// Case-insensitive comparison of the last 'suffix_len' bytes
			if (_strnicmp(end_of_path, suffix, suffix_len) == 0)
			{
				// It ends with "r1delta". Now check for "gameinfo.txt" in that directory.
				char gameinfo_path[MAX_PATH];

				// Construct the full path: original_a4 + "\" + "gameinfo.txt"
				int chars_written = sprintf_s(gameinfo_path, MAX_PATH, "%s\\gameinfo.txt", original_a4);

				// Check if path construction was successful and if the file exists
				if (chars_written > 0 && GetFileAttributesA(gameinfo_path) != INVALID_FILE_ATTRIBUTES)
				{
					// "gameinfo.txt" exists in the directory specified by original_a4.
					// This is the *only* condition where we want to keep the original path.
					keep_original_path = true;
				}
				// else: gameinfo.txt doesn't exist or path construction failed.
			}
			// else: original_a4 does not end with "r1delta".
		}
		// else: original_a4 is shorter than the suffix.
	}
	// else: original_a4 was already NULL.
	// --- End checking the conditions ---

	// Decide the final value for a4 based on whether the specific conditions were met
	if (keep_original_path)
	{
		result_a4 = original_a4; // Keep the original path
	}
	// else: result_a4 remains nullptr (the default action is to nullify)

	// Call the original function with the final result_a4 value
	return oFileSystem_AddLoadedSearchPath(a1, a2, a3, result_a4, Source, a6);
}

void InitAddons() {
	static bool done = false;
	if (done) return;
	done = true;
	auto engine_base_spec = ENGINE_DLL_BASE;
	auto filesystem_stdio = IsDedicatedServer() ? G_vscript : G_filesystem_stdio;
	if (!IsR1ODedicatedServer()) {
		MH_CreateHook((LPVOID)(engine_base_spec + (IsDedicatedServer() ? 0x95AA0 : 0x127C70)), &FileSystem_UpdateAddonSearchPaths, reinterpret_cast<LPVOID*>(&FileSystem_UpdateAddonSearchPathsTypeOriginal));
		MH_CreateHook((LPVOID)(engine_base_spec + (IsDedicatedServer() ? 0x950E0 : 0x1272B0)), &ReconcileAddonListFile, reinterpret_cast<LPVOID*>(&oReconcileAddonListFile));
	}
	MH_CreateHook((LPVOID)(filesystem_stdio + (IsDedicatedServer() ? 0x1752B0 : 0x6A420)), &ReadFileFromVPKHook, reinterpret_cast<LPVOID*>(&readFileFromVPK));
	MH_CreateHook((LPVOID)(filesystem_stdio + (IsDedicatedServer() ? 0x750F0 : 0x9C20)), &ReadFromCacheHook, reinterpret_cast<LPVOID*>(&readFromCache));
	MH_CreateHook((LPVOID)(filesystem_stdio + (IsDedicatedServer() ? 0x80BB0 : 0x16250)), &AddVPKFile, reinterpret_cast<LPVOID*>(&AddVPKFileOriginal));
	if (!HasEngineCommandLineFlag("-r1delta_disable_vpk_directory_repair"))
		InstallVPKDirectoryLoadFlagRepair(filesystem_stdio);
	if (!HasEngineCommandLineFlag("-r1delta_disable_vpk_async_precache_fix"))
		InstallR1ClientVPKAsyncPrecacheFix(filesystem_stdio);
	MH_CreateHook((LPVOID)(filesystem_stdio + (IsDedicatedServer() ? 0x1A1514 : 0x9AB70)), &fs_sprintf_hook, reinterpret_cast<LPVOID*>(NULL));
	MH_CreateHook((LPVOID)(filesystem_stdio + (IsDedicatedServer() ? 0x6EE10 : 0x02C30)), &CBaseFileSystem__FindFirst, reinterpret_cast<LPVOID*>(&oCBaseFileSystem__FindFirst));
	MH_CreateHook((LPVOID)(filesystem_stdio + (IsDedicatedServer() ? 0x86E00 : 0x1C4A0)), &CBaseFileSystem__FindNext, reinterpret_cast<LPVOID*>(&oCBaseFileSystem__FindNext));
	MH_CreateHook((LPVOID)(filesystem_stdio + (IsDedicatedServer() ? 0x7F180 : 0x14780)), &HookedHandleOpenRegularFile, reinterpret_cast<LPVOID*>(&HandleOpenRegularFileOriginal));
	if (!IsR1ODedicatedServer()) {
		MH_CreateHook((LPVOID)(engine_base_spec + (IsDedicatedServer() ? 0x96980 : 0x128C80)), &FileSystem_AddLoadedSearchPath, reinterpret_cast<LPVOID*>(&oFileSystem_AddLoadedSearchPath));
	}

	//client = std::make_shared<discordpp::Client>();
	MH_EnableHook(MH_ALL_HOOKS);
}

std::unordered_map<std::string, std::string, HashStrings, std::equal_to<>> g_LastEntCreateKeyValues;
void (*oCC_Ent_Create)(const CCommand* args);
bool g_bIsEntCreateCommand = false;

void CC_Ent_Create(const CCommand* args)
{
	g_LastEntCreateKeyValues.clear();

	int numPairs = (args->ArgC() - 2) / 2;
	g_LastEntCreateKeyValues.reserve(numPairs);

	for (int i = 2; i + 1 < args->ArgC(); i += 2)
	{
		const char* const pKeyName = (*args)[i];
		const char* const pValue = (*args)[i + 1];

		if (pKeyName && pValue) {
			g_LastEntCreateKeyValues[pKeyName] = pValue;
		}
	}

	g_bIsEntCreateCommand = true;
	oCC_Ent_Create(args);
	g_bIsEntCreateCommand = false;

	g_LastEntCreateKeyValues.clear();
}
__int64 (*oDispatchSpawn)(__int64 a1, char a2);
__int64 __fastcall DispatchSpawn(__int64 a1, char a2) {
	static auto target = G_server + 0x3BE267;
	if (uintptr_t(_ReturnAddress()) == target && g_bIsEntCreateCommand) {
		auto entityVtable = *(_QWORD*)a1;
		auto setKeyValueFunction = (void(__fastcall**)(__int64, const char*, const char*))(entityVtable + 288LL);

		for (const auto& pair : g_LastEntCreateKeyValues) {
			(*setKeyValueFunction)(a1, pair.first.c_str(), pair.second.c_str());
		}
	}
	return oDispatchSpawn(a1, a2);
}
typedef void (*SetRankFunctionType)(__int64, int);
typedef int (*GetRankFunctionType)(__int64);


SetConvarString_t SetConvarStringOriginal;
static bool ResolveSetConvarString()
{
	if (SetConvarStringOriginal)
		return true;

	const uintptr_t vstdlibBase = reinterpret_cast<uintptr_t>(GetModuleHandleA("vstdlib.dll"));
	if (!vstdlibBase)
		return false;

	SetConvarStringOriginal = reinterpret_cast<SetConvarString_t>(vstdlibBase + 0x24DE0);
	return true;
}


bool ShouldEnableMCP() {
	static bool parsed = false;
	static bool useMcp = false;

	if (!parsed) {
		parsed = true;
		useMcp = HasEngineCommandLineFlag("-usemcp");
	}

	return useMcp;
}

static bool ShouldInstallR1OClientDebugHooks()
{
	static bool parsed = false;
	static bool enabled = false;
	if (!parsed) {
		parsed = true;
		enabled = HasEngineCommandLineFlag("-r1o_client_debug_hooks");
	}
	return enabled;
}

typedef __int64(*Host_InitType)(bool a1);
Host_InitType Host_InitOriginal;

using NetMessageWriteToBufferType = bool(__fastcall*)(__int64 message, __int64 bitBuffer);
using NetMessageReadFromBufferType = bool(__fastcall*)(__int64 message, __int64 bitBuffer);
using NetMessageProcessType = bool(__fastcall*)(__int64 message);
using COMExplainDisconnectionType = __int64(*)(__int64 eventKind, const char* format, ...);
using RecvTableMergeDeltasType = __int64(__fastcall*)(__int64 recvTable, __int64 oldState, __int64 newState, __int64 outState, int a5, int a6, signed int* outProps);
using RecvTableMergePropType = __int64(__fastcall*)(__int64 recvProp, unsigned int propIndex, __int64 sourceState, __int64 outState);
using DeltaPropIndexReadType = __int64(__fastcall*)(__int64* reader);
using DataTableSetupReceiveType = char(__fastcall*)(__int64 sendTable, char needsDecoder);
using FindRecvTableByNameType = __int64(__fastcall*)(const char* name);
using ClientCopyNewEntityType = char(__fastcall*)(__int64 parseInfo, int classIndex, unsigned int serial);
using ClientSnapshotEntityReadType = char(__fastcall*)(__int64 a1, __int64 a2, __int64 oldState, __int64 newState, __int64 stack1, __int64 stack2, __int64 stack3, __int64 stack4, __int64 stack5);
using ClientSetUpViewType = __int64(__fastcall*)(__int64 viewRender);
using ClientPlayerCalcViewType = void(__fastcall*)(void* player, float* eyeOrigin, float* eyeAngles, float* fov);
using ClientNormalCalcViewType = __int64(__fastcall*)(void* player, float* eyeOrigin, float* eyeAngles, float* fov);
using ClientPostCalcViewType = __int64(__fastcall*)(void* player, float* eyeOrigin, float* eyeAngles);
using ClientViewStage3Type = __int64(__fastcall*)(void* player, float* eyeOrigin, float* eyeAngles);
using ClientViewStage4FloatType = __int64(__fastcall*)(void* player, float* eyeOrigin, float* eyeAngles, float frameTime);
using ClientViewStage4FloatVoidType = void(__fastcall*)(void* player, float* eyeOrigin, float* eyeAngles, float frameTime);
using ClientSpringOriginStageType = void(__fastcall*)(float* spring, float* eyeOrigin);
using ClientSpringAnglesStageType = __int64(__fastcall*)(float* spring, float* eyeAngles);
using ClientViewModelCalcViewModelViewType = __int64(__fastcall*)(void* viewModel, void* player, float* eyeOrigin, float* eyeAngles);
using ClientAngleOffset03BA40Type = float* (__fastcall*)(void* player, float* outAngles);
using ClientAngleOffset0376F0Type = float* (__fastcall*)(float* outAngles, void* player);
using ClientAngleStage03C9F0Type = __int64(__fastcall*)(void* player, float* eyeAngles, float frameTime);
using ClientRollReset08E6E0Type = __int64(__fastcall*)(void* player);
using ClientRollLifecycle34CEF0Type = __int64(__fastcall*)(void* player);
using ClientApplyPlayerClassMods03FBB0Type = __int64(__fastcall*)(void* player, char force);
using ClientSetClassVar041830Type = __int64(__fastcall*)(__int64 args);
using ClientLoadPlayerClasses109E80Type = __int64(__fastcall*)();
using ClientLoadDataList45BD10Type = __int64(__fastcall*)(const char* listName, const char* directory, const char* fileName, void(__fastcall* callback)(__int64, unsigned char*));
using ClientKeyValuesLoadFromFile65F980Type = unsigned char(__fastcall*)(void* keyValues, void* fileSystem, const char* resourceName, const char* pathId, void* unknown);

static int ClientLocalSignonState();
static NetMessageWriteToBufferType s_ClientNetMessageWriteToBufferOriginal[sizeof(netMessages) / sizeof(netMessages[0])];
static NetMessageReadFromBufferType s_ClientNetMessageReadFromBufferOriginal[sizeof(netMessages) / sizeof(netMessages[0])];
static NetMessageProcessType s_ClientNetMessageProcessOriginal[sizeof(netMessages) / sizeof(netMessages[0])];
static NetMessageReadFromBufferType s_ClientSVCServerInfoReadOriginal;
static NetMessageProcessType s_ClientSVCServerInfoProcessOriginal;
static COMExplainDisconnectionType s_COMExplainDisconnectionOriginal;
static RecvTableMergeDeltasType s_ClientRecvTableMergeDeltasOriginal;
static RecvTableMergePropType s_ClientRecvTableMergePropOriginal;
static DeltaPropIndexReadType s_ClientDeltaPropIndexReadOriginal;
static DataTableSetupReceiveType s_ClientDataTableSetupReceiveOriginal;
static FindRecvTableByNameType s_ClientFindRecvTableByName;
static ClientCopyNewEntityType s_ClientCopyNewEntityOriginal;
static ClientSnapshotEntityReadType s_ClientSnapshotEntityReadOriginal;
static ClientSetUpViewType s_ClientSetUpViewOriginal;
static ClientPlayerCalcViewType s_ClientPlayerCalcViewOriginal;
static ClientNormalCalcViewType s_ClientNormalCalcViewOriginal;
static ClientPostCalcViewType s_ClientPostCalcViewOriginal;
static ClientViewStage3Type s_ClientCameraStage03F170Original;
static ClientViewStage3Type s_ClientCameraStage32ECC0Original;
static ClientViewStage4FloatType s_ClientCameraStage089B60Original;
static ClientViewStage3Type s_ClientCameraStage037D10Original;
static ClientViewStage4FloatVoidType s_ClientCameraStage090160Original;
static ClientViewStage3Type s_ClientCameraStage035450Original;
static ClientSpringOriginStageType s_ClientSpringOriginStageOriginal;
static ClientSpringAnglesStageType s_ClientSpringAnglesStageOriginal;
static ClientViewModelCalcViewModelViewType s_ClientViewModelCalcViewModelViewOriginal;
static ClientAngleOffset03BA40Type s_ClientAngleOffset03BA40Original;
static ClientAngleOffset0376F0Type s_ClientAngleOffset0376F0Original;
static ClientAngleStage03C9F0Type s_ClientAngleStage03C9F0Original;
static ClientRollReset08E6E0Type s_ClientRollReset08E6E0Original;
static ClientRollLifecycle34CEF0Type s_ClientRollLifecycle34CEF0Original;
static ClientApplyPlayerClassMods03FBB0Type s_ClientApplyPlayerClassMods03FBB0Original;
static ClientSetClassVar041830Type s_ClientSetClassVar041830Original;
static ClientLoadPlayerClasses109E80Type s_ClientLoadPlayerClasses109E80Original;
static ClientLoadDataList45BD10Type s_ClientLoadDataList45BD10Original;
static ClientKeyValuesLoadFromFile65F980Type s_ClientKeyValuesLoadFromFile65F980Original;
static uintptr_t s_ClientPlayerCalcViewTarget;
static int s_ClientNetMessageWriteLogBudget = 1024;
static int s_ClientNetMessageReadLogBudget = 256;
static int s_ClientSnapshotReadLogBudget = 64;
static int s_ClientNetMessageProcessLogBudget = 128;
static int s_ClientClassInfoProcessLogBudget = 96;
static int s_ClientSVCServerInfoReadLogBudget = 16;
static int s_ClientSVCServerInfoProcessLogBudget = 16;
static int s_ClientRecvTableMergeLogBudget = 96;
static int s_ClientRecvTableMergeValidLogBudget = 1024;
static int s_ClientCellDecodeTraceLogBudget = 256;
static int s_ClientRecvTableMergePreCrashLogBudget = 256;
static int s_ClientDeltaPropIndexReadLogBudget = 2048;
static int s_ClientDataTableSetupLogBudget = 1024;
static int s_ClientCopyNewEntityLogBudget = 96;
static int s_ClientRecvTableMergeExceptionLogBudget = 16;
static int s_ClientSnapshotEntityReadLogBudget = 32;
static int s_ClientPlayerFlatPropLogBudget = 512;
static int s_ClientRecvDecoderAuditLogBudget = 1024;
static int s_ClientLocalPlayerStateLogBudget = 24;
static int s_ClientRecvProxyTraceLogBudget = 512;
static int s_ClientSetUpViewLogBudget = 96;
static int s_ClientPlayerCalcViewLogBudget = 96;
static int s_ClientNormalCalcViewLogBudget = 64;
static int s_ClientPostCalcViewLogBudget = 64;
static int s_ClientCameraStage03F170LogBudget = 64;
static int s_ClientCameraStageLogBudget = 160;
static int s_ClientViewModelCalcViewModelViewLogBudget = 64;
static int s_ClientAngleStageLogBudget = 96;
static int s_ClientRollLifecycleLogBudget = 160;
static int s_ClientClassApplyLogBudget = 128;
static int s_ClientSetClassVarLogBudget = 48;
static int s_ClientPlayerClassLoadLogBudget = 24;
static int s_ClientPlayerClassDataListLogBudget = 48;
static int s_ClientPlayerClassKVLogBudget = 96;
static int s_ClientPlayerClassLoadEnsureAttempts = 0;
static bool s_ClientNetMessageWriteHooksInstalled;
static bool s_ClientNetMessageReadHooksInstalled;
static bool s_ClientNetMessageProcessHooksInstalled;
static bool s_ClientSVCServerInfoReadHookInstalled;
static bool s_ClientSVCServerInfoProcessHookInstalled;
static bool s_COMExplainDisconnectionHookInstalled;
static bool s_ClientRecvTableMergeHooksInstalled;
static bool s_ClientDataTableSetupHookInstalled;
static bool s_ClientCopyNewEntityHookInstalled;
static bool s_ClientRecvProxyTraceInstalled;
static __int64 s_ClientAuditedRecvDecoders[512];
static int s_ClientAuditedRecvDecoderCount;
static bool s_ClientSetUpViewHookInstalled;
static bool s_ClientPlayerCalcViewHookInstalled;
static bool s_ClientNormalCalcViewHookInstalled;
static bool s_ClientPostCalcViewHookInstalled;
static bool s_ClientCameraStage03F170HookInstalled;
static bool s_ClientCameraStageHooksInstalled;
static bool s_ClientViewModelCalcViewModelViewHookInstalled;
static bool s_ClientAngleStageHooksInstalled;
static bool s_ClientRollLifecycleHooksInstalled;
static bool s_ClientApplyPlayerClassModsHookInstalled;
static bool s_ClientSetClassVarHookInstalled;
static bool s_ClientPlayerClassLoadHookInstalled;
static bool s_ClientLoadDataListHookInstalled;
static bool s_ClientKeyValuesLoadFromFileHookInstalled;
static thread_local bool s_ClientInsideNormalCalcView;
static thread_local bool s_ClientInsidePlayerClassLoad;
static thread_local bool s_ClientInsidePlayerClassKVFallback;
static thread_local void* s_ClientCurrentNormalCalcViewPlayer;
static thread_local __int64 s_ClientRecvTableMergeCurrentTable;
static thread_local const char* s_ClientRecvTableMergeCurrentName;
static thread_local __int64 s_ClientRecvTableMergeCurrentOldState;
static thread_local __int64 s_ClientRecvTableMergeCurrentNewState;
static thread_local __int64 s_ClientRecvTableMergeCurrentOutState;
static thread_local int s_ClientRecvTableMergeCurrentEntity = -1;
static thread_local int s_ClientRecvTableMergeCurrentA6 = -1;

static int ClientNetMessageIndex(__int64 message)
{
	if (!message)
		return -1;

	uintptr_t vtable = 0;
	__try {
		vtable = *reinterpret_cast<uintptr_t*>(message);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return -1;
	}

	if (!G_engine || vtable < G_engine)
		return -1;

	const uintptr_t rva = vtable - G_engine;
	for (size_t i = 0; i < sizeof(netMessages) / sizeof(netMessages[0]); ++i) {
		if (netMessages[i].offset_engine == rva)
			return static_cast<int>(i);
	}

	return -1;
}

static int ClientNetMessageIntVFunc(__int64 message, size_t index, int fallback = -1)
{
	if (!message)
		return fallback;

	__try {
		uintptr_t* vtable = *reinterpret_cast<uintptr_t**>(message);
		if (!vtable || !vtable[index])
			return fallback;
		return reinterpret_cast<int(__fastcall*)(__int64)>(vtable[index])(message);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return fallback;
	}
}

static const char* ClientNetMessageName(__int64 message)
{
	if (!message)
		return "<null>";

	__try {
		uintptr_t* vtable = *reinterpret_cast<uintptr_t**>(message);
		if (!vtable || !vtable[10])
			return "<no-name-fn>";
		const char* name = reinterpret_cast<const char*(__fastcall*)(__int64)>(vtable[10])(message);
		return name ? name : "<null-name>";
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return "<name-av>";
	}
}

static const char* ClientSafeCString(const char* value, char* scratch, size_t scratchSize)
{
	if (!scratch || !scratchSize)
		return "<no-scratch>";

	scratch[0] = '\0';
	if (!value)
		return "<null>";

	__try {
		size_t i = 0;
		for (; i + 1 < scratchSize && i < 255; ++i) {
			const char ch = value[i];
			scratch[i] = ch;
			if (!ch)
				return scratch;
		}
		scratch[i] = '\0';
		return scratch;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return "<av>";
	}
}

static bool ClientIsReadableCString(const char* value)
{
	if (!value)
		return false;

	__try {
		for (size_t i = 0; i < 256; ++i) {
			if (value[i] == '\0')
				return true;
		}
		return false;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

static bool ClientLooksLikeUserRange(uintptr_t address, size_t size)
{
	constexpr uintptr_t kMinUserAddress = 0x10000;
	constexpr uintptr_t kMaxUserAddress = 0x00007FFFFFFFFFFF;
	if (!size || address < kMinUserAddress || address > kMaxUserAddress)
		return false;
	return size - 1 <= kMaxUserAddress - address;
}

static bool ClientIsReadableRange(const void* value, size_t size)
{
	if (!size)
		return true;
	return ClientLooksLikeUserRange(reinterpret_cast<uintptr_t>(value), size);
}

// Use this only when the caller is about to copy from storage whose lifetime or
// allocation owner is not trusted.  Most client compatibility probes operate on
// engine-owned objects and only need the cheap canonical-range/overflow check
// above; putting VirtualQuery in that shared path makes class and netprop setup
// issue thousands of kernel queries while a client joins.
static bool ClientIsCommittedReadableRange(const void* value, size_t size)
{
	if (!size)
		return true;

	uintptr_t current = reinterpret_cast<uintptr_t>(value);
	if (!ClientLooksLikeUserRange(current, size))
		return false;

	const uintptr_t end = current + size - 1;
	while (current <= end) {
		MEMORY_BASIC_INFORMATION mbi{};
		if (!VirtualQuery(reinterpret_cast<const void*>(current), &mbi, sizeof(mbi)))
			return false;

		const DWORD protect = mbi.Protect & 0xff;
		if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD) || protect == PAGE_NOACCESS)
			return false;

		const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
		if (regionEnd <= current)
			return false;

		current = regionEnd;
	}

	return true;
}

static const char* ClientNetStringCommandText(__int64 message)
{
	if (!message || !ClientIsReadableRange(reinterpret_cast<void*>(message + 32), sizeof(const char*)))
		return "<unreadable-message>";

	const char* command = *reinterpret_cast<const char**>(message + 32);
	if (!ClientIsReadableCString(command))
		return "<unreadable-command>";

	return command;
}

static int ClientCBitReadTell(__int64 bitBuffer)
{
	if (!bitBuffer)
		return -1;

	__try {
		const int dataBits = *reinterpret_cast<int*>(bitBuffer + 16);
		const int dataBytes = *reinterpret_cast<int*>(bitBuffer + 24);
		const int bitsAvail = *reinterpret_cast<int*>(bitBuffer + 36);
		const auto dataIn = *reinterpret_cast<uint32_t const**>(bitBuffer + 40);
		const auto data = *reinterpret_cast<uint32_t const**>(bitBuffer + 56);
		if (!dataIn || !data)
			return 0;
		int bit = static_cast<int>((dataIn - data) - 1) * 32;
		bit += 32 - bitsAvail;
		bit += 8 * (dataBytes & 3);
		return bit < dataBits ? bit : dataBits;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return -1;
	}
}

static void ClientDumpBitWindow(char* out, size_t outSize, __int64 bitBuffer, int bit)
{
	if (!out || !outSize)
		return;
	out[0] = '\0';
	if (!bitBuffer || bit < 0)
		return;

	__try {
		const auto data = *reinterpret_cast<unsigned char const**>(bitBuffer + 56);
		const int dataBytes = *reinterpret_cast<int*>(bitBuffer + 24);
		const int byteOffset = bit >> 3;
		if (!data || byteOffset < 0 || byteOffset >= dataBytes)
			return;
		if (!ClientIsReadableRange(data + byteOffset, 1))
			return;

		size_t written = 0;
		const int limit = dataBytes - byteOffset < 24 ? dataBytes - byteOffset : 24;
		for (int i = 0; i < limit && written + 4 < outSize; ++i) {
			if (!ClientIsReadableRange(data + byteOffset + i, 1))
				break;
			written += static_cast<size_t>(_snprintf_s(out + written, outSize - written, _TRUNCATE, "%02X%s", data[byteOffset + i], i + 1 < limit ? " " : ""));
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		strncpy_s(out, outSize, "<av>", _TRUNCATE);
	}
}

static void ClientDumpBitRange(char* out, size_t outSize, __int64 bitBuffer, int startBit, int endBit)
{
	if (!out || !outSize)
		return;

	out[0] = '\0';
	if (!bitBuffer || startBit < 0 || endBit < startBit)
		return;

	__try {
		const auto data = *reinterpret_cast<unsigned char const**>(bitBuffer + 56);
		const int dataBits = *reinterpret_cast<int*>(bitBuffer + 16);
		if (!data || dataBits <= 0 || startBit >= dataBits)
			return;

		if (endBit > dataBits)
			endBit = dataBits;

		size_t used = 0;
		const int limit = endBit - startBit > 96 ? startBit + 96 : endBit;
		for (int bit = startBit; bit < limit && used + 2 < outSize; ++bit) {
			if (!ClientIsReadableRange(data + (bit >> 3), 1))
				break;
			out[used++] = (data[bit >> 3] & (1u << (bit & 7))) ? '1' : '0';
		}
		out[used] = '\0';
		if (limit < endBit && used + 4 < outSize)
			strncpy_s(out + used, outSize - used, "...", _TRUNCATE);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		strncpy_s(out, outSize, "<av>", _TRUNCATE);
	}
}

static unsigned int ClientReadBitRangeUnsigned(__int64 bitBuffer, int startBit, int endBit, bool* ok = nullptr)
{
	if (ok)
		*ok = false;
	if (!bitBuffer || startBit < 0 || endBit < startBit || endBit - startBit > 32)
		return 0;

	__try {
		const auto data = *reinterpret_cast<unsigned char const**>(bitBuffer + 56);
		const int dataBits = *reinterpret_cast<int*>(bitBuffer + 16);
		if (!data || dataBits <= 0 || endBit > dataBits)
			return 0;

		unsigned int value = 0;
		for (int bit = startBit; bit < endBit; ++bit) {
			if (!ClientIsReadableRange(data + (bit >> 3), 1))
				return 0;
			if (data[bit >> 3] & (1u << (bit & 7)))
				value |= 1u << (bit - startBit);
		}
		if (ok)
			*ok = true;
		return value;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return 0;
	}
}

static bool ClientIsDeltaTraceTable(const char* tableName)
{
	if (!tableName)
		return false;

	return _stricmp(tableName, "DT_World") == 0
		|| _stricmp(tableName, "DT_WORLD") == 0
		|| _stricmp(tableName, "DT_Team") == 0
		|| _stricmp(tableName, "DT_PlayerResource") == 0
		|| _stricmp(tableName, "DT_PhysicsProp") == 0
		|| _stricmp(tableName, "DT_HL2_Player") == 0
		|| _stricmp(tableName, "DT_InfoPlacementHelper") == 0
		|| _stricmp(tableName, "DT_EnvTonemapController") == 0;
}

static const char* ClientRecvTableName(__int64 recvTable)
{
	if (!recvTable)
		return "<null-table>";

	__try {
		const char* name = *reinterpret_cast<const char**>(recvTable + 24);
		return ClientIsReadableCString(name) ? name : "<bad-table-name>";
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return "<av-table-name>";
	}
}

static int ClientRecvTablePropCount(__int64 recvTable)
{
	if (!recvTable)
		return -1;

	__try {
		return *reinterpret_cast<int*>(recvTable + 8);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return -2;
	}
}

static bool ClientReadUtlVector(__int64 vectorBase, __int64* data, int* count)
{
	if (data)
		*data = 0;
	if (count)
		*count = -1;

	if (!vectorBase)
		return false;

	__try {
		const __int64 vectorData = *reinterpret_cast<__int64*>(vectorBase);
		const int vectorCount = *reinterpret_cast<int*>(vectorBase + 16);
		if (data)
			*data = vectorData;
		if (count)
			*count = vectorCount;
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

static const char* ClientSendPropName(__int64 sendProp)
{
	if (!sendProp)
		return "<null-sendprop>";

	__try {
		const char* name = *reinterpret_cast<const char**>(sendProp + 72);
		return ClientIsReadableCString(name) ? name : "<bad-sendprop-name>";
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return "<av-sendprop-name>";
	}
}

static int ClientSendPropType(__int64 sendProp)
{
	if (!sendProp)
		return -1;

	__try {
		return *reinterpret_cast<int*>(sendProp + 16);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return -2;
	}
}

static int ClientSendPropFlags(__int64 sendProp)
{
	if (!sendProp)
		return -1;

	__try {
		return *reinterpret_cast<int*>(sendProp + 88);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return -2;
	}
}

static int ClientSendPropOffset(__int64 sendProp)
{
	if (!sendProp)
		return -1;

	__try {
		return *reinterpret_cast<int*>(sendProp + 120);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return -2;
	}
}

static int ClientSendPropNumBits(__int64 sendProp)
{
	if (!sendProp)
		return -1;

	__try {
		return *reinterpret_cast<int*>(sendProp + 20);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return -2;
	}
}

static bool ClientSendPropLooksValid(__int64 sendProp)
{
	if (!sendProp)
		return false;

	if (!ClientIsReadableRange(reinterpret_cast<const void*>(sendProp + 16), sizeof(int))
		|| !ClientIsReadableRange(reinterpret_cast<const void*>(sendProp + 72), sizeof(const char*)))
		return false;

	const int type = *reinterpret_cast<int*>(sendProp + 16);
	const char* name = *reinterpret_cast<const char**>(sendProp + 72);
	if (type < 0 || type > 10)
		return false;
	return ClientIsReadableCString(name);
}

static __int64 ClientDecoderForRecvTable(__int64 recvTable)
{
	if (!recvTable)
		return 0;

	__try {
		return *reinterpret_cast<__int64*>(recvTable + 16);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return 0;
	}
}

static __int64 ClientDecoderVectorEntry(__int64 vectorBase, unsigned int index)
{
	__int64 data = 0;
	int count = -1;
	if (!ClientReadUtlVector(vectorBase, &data, &count) || !data || index >= static_cast<unsigned int>(count))
		return 0;

	__try {
		return *reinterpret_cast<__int64*>(data + 8LL * index);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return 0;
	}
}

static int ClientRecvDecoderFlattenedPropCount(__int64 decoder)
{
	if (!decoder)
		return -1;

	__try {
		return *reinterpret_cast<int*>(decoder + 536);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return -2;
	}
}

static __int64 ClientSendPropDataTable(__int64 sendProp)
{
	if (!sendProp)
		return 0;

	__try {
		return *reinterpret_cast<__int64*>(sendProp + 112);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return 0;
	}
}

static int ClientRecvDecoderSendPropCount(__int64 decoder)
{
	if (!decoder)
		return -1;

	__try {
		// The decoded remote SendProp list is a CUtlVector at +0x60.
		// Its count is independent of the flattened local RecvProp vector.
		return *reinterpret_cast<int*>(decoder + 120);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return -2;
	}
}

static __int64 ClientRecvDecoderRecvProp(__int64 decoder, unsigned int index)
{
	if (!decoder)
		return 0;

	__try {
		const int count = ClientRecvDecoderFlattenedPropCount(decoder);
		if (count < 0 || index >= static_cast<unsigned int>(count))
			return 0;
		const __int64 data = *reinterpret_cast<__int64*>(decoder + 512);
		return data ? *reinterpret_cast<__int64*>(data + 8LL * index) : 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return 0;
	}
}

static __int64 ClientRecvDecoderSendProp(__int64 decoder, unsigned int index)
{
	if (!decoder)
		return 0;

	__try {
		const int count = ClientRecvDecoderSendPropCount(decoder);
		if (count < 0 || index >= static_cast<unsigned int>(count))
			return 0;
		const __int64 data = *reinterpret_cast<__int64*>(decoder + 96);
		return data ? *reinterpret_cast<__int64*>(data + 8LL * index) : 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return 0;
	}
}

static const char* ClientRecvPropName(__int64 recvProp)
{
	if (!recvProp)
		return "<null-recvprop>";

	__try {
		const char* name = *reinterpret_cast<const char**>(recvProp + 0);
		return ClientIsReadableCString(name) ? name : "<bad-recvprop-name>";
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return "<av-recvprop-name>";
	}
}

static int ClientRecvPropType(__int64 recvProp)
{
	if (!recvProp)
		return -1;

	__try {
		return *reinterpret_cast<int*>(recvProp + 8);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return -2;
	}
}

static int ClientRecvPropFlags(__int64 recvProp)
{
	if (!recvProp)
		return -1;

	__try {
		return *reinterpret_cast<int*>(recvProp + 12);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return -2;
	}
}

static int ClientRecvPropOffset(__int64 recvProp)
{
	if (!recvProp)
		return -1;

	__try {
		return *reinterpret_cast<int*>(recvProp + 72);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return -2;
	}
}

static int ClientRecvPropElementStride(__int64 recvProp)
{
	if (!recvProp)
		return -1;

	__try {
		return *reinterpret_cast<int*>(recvProp + 76);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return -2;
	}
}

static int ClientRecvPropNumElements(__int64 recvProp)
{
	if (!recvProp)
		return -1;

	__try {
		return *reinterpret_cast<int*>(recvProp + 80);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return -2;
	}
}

using ClientRecvVarProxyFn = void(__fastcall*)(const void* data, void* pStruct, void* pOut);

struct ClientDVariantTrace
{
	union
	{
		float m_Float;
		int m_Int;
		char* m_pString;
		void* m_pData;
		float m_Vector[3];
		long long m_Int64;
	};
	int m_Type;
};

struct ClientRecvProxyDataTrace
{
	__int64 m_pRecvProp;
	ClientDVariantTrace m_Value;
	int m_iElement;
	int m_ObjectID;
};

struct ClientRecvProxyTraceEntry
{
	__int64 recvProp;
	ClientRecvVarProxyFn original;
	char name[96];
	int type;
	int offset;
};

static ClientRecvProxyTraceEntry s_ClientRecvProxyTraceEntries[1024];
static int s_ClientRecvProxyTraceEntryCount;

static ClientRecvVarProxyFn ClientRecvPropProxyFn(__int64 recvProp)
{
	if (!recvProp)
		return nullptr;

	__try {
		return *reinterpret_cast<ClientRecvVarProxyFn*>(recvProp + 48);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return nullptr;
	}
}

static bool ClientSetRecvPropProxyFn(__int64 recvProp, ClientRecvVarProxyFn proxy)
{
	if (!recvProp || !proxy)
		return false;

	void* const slot = reinterpret_cast<void*>(recvProp + 48);
	DWORD oldProtect = 0;
	if (!VirtualProtect(slot, sizeof(proxy), PAGE_EXECUTE_READWRITE, &oldProtect))
		return false;
	*reinterpret_cast<ClientRecvVarProxyFn*>(slot) = proxy;
	DWORD unused = 0;
	VirtualProtect(slot, sizeof(proxy), oldProtect, &unused);
	FlushInstructionCache(GetCurrentProcess(), slot, sizeof(proxy));
	return true;
}

static ClientRecvProxyTraceEntry* ClientFindRecvProxyTraceEntry(__int64 recvProp)
{
	if (!recvProp)
		return nullptr;
	for (int i = 0; i < s_ClientRecvProxyTraceEntryCount; ++i) {
		if (s_ClientRecvProxyTraceEntries[i].recvProp == recvProp)
			return &s_ClientRecvProxyTraceEntries[i];
	}
	return nullptr;
}

static void* ClientGetLocalPlayerEntityForTrace()
{
	if (!G_client)
		return nullptr;

	using GetClientEntityByIndexType = void* (*)(int);
	using GetClientEntitySelfType = void* (*)(int);
	__try {
		void* const entSelf = reinterpret_cast<GetClientEntitySelfType>(G_client + 0x7B1B0)(-1);
		if (entSelf)
			return entSelf;
		void* const ent1 = reinterpret_cast<GetClientEntityByIndexType>(G_client + 0x280FE0)(1);
		if (ent1)
			return ent1;
		return reinterpret_cast<GetClientEntityByIndexType>(G_client + 0x280FE0)(0);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return nullptr;
	}
}

static bool ClientIsInterestingRecvProxyName(const char* name)
{
	if (!name)
		return false;

	return _stricmp(name, "m_dodging") == 0
		|| _stricmp(name, "m_dodgingInAir") == 0
		|| _stricmp(name, "m_airSpeed") == 0
		|| _stricmp(name, "m_airAcceleration") == 0
		|| _stricmp(name, "m_stepSmoothingOffset") == 0
		|| _stricmp(name, "m_flAirInputScale") == 0
		|| _stricmp(name, "m_flCurrentStickTransitionDelay") == 0
		|| _stricmp(name, "m_nStickCameraState") == 0
		|| _stricmp(name, "m_InAirState") == 0
		|| _stricmp(name, "m_bDoneStickInterp") == 0
		|| _stricmp(name, "m_bDoneCorrectPitch") == 0
		|| _stricmp(name, "m_Up") == 0
		|| _stricmp(name, "m_vLocalUp") == 0
		|| _stricmp(name, "m_upDir") == 0
		|| _stricmp(name, "m_upDirPredicted") == 0
		|| _stricmp(name, "m_vecVelocity[0]") == 0
		|| _stricmp(name, "m_vecVelocity[1]") == 0
		|| _stricmp(name, "m_vecVelocity[2]") == 0;
}

static void ClientFormatProxyValue(char* out, size_t outSize, const ClientRecvProxyDataTrace* data, int fallbackType)
{
	if (!out || !outSize)
		return;
	out[0] = '\0';
	if (!data) {
		strncpy_s(out, outSize, "<null>", _TRUNCATE);
		return;
	}

	const int type = data->m_Value.m_Type >= 0 && data->m_Value.m_Type <= 7 ? data->m_Value.m_Type : fallbackType;
	if (type == 0) {
		_snprintf_s(out, outSize, _TRUNCATE, "i=%d", data->m_Value.m_Int);
	}
	else if (type == 1) {
		_snprintf_s(out, outSize, _TRUNCATE, "f=%.9g", data->m_Value.m_Float);
	}
	else if (type == 2 || type == 3) {
		_snprintf_s(out, outSize, _TRUNCATE, "v=(%.9g,%.9g,%.9g)", data->m_Value.m_Vector[0], data->m_Value.m_Vector[1], data->m_Value.m_Vector[2]);
	}
	else if (type == 7) {
		_snprintf_s(out, outSize, _TRUNCATE, "i64=%lld", data->m_Value.m_Int64);
	}
	else {
		_snprintf_s(out, outSize, _TRUNCATE, "type=%d raw=(0x%08x,0x%08x,0x%08x)", type, reinterpret_cast<const unsigned int*>(&data->m_Value)[0], reinterpret_cast<const unsigned int*>(&data->m_Value)[1], reinterpret_cast<const unsigned int*>(&data->m_Value)[2]);
	}
}

static void ClientReadProxyOutFloats(void* pOut, int propType, float out[3])
{
	out[0] = NAN;
	out[1] = NAN;
	out[2] = NAN;
	if (!pOut)
		return;

	__try {
		if (propType == 1) {
			if (ClientIsReadableRange(pOut, sizeof(float)))
				out[0] = *reinterpret_cast<float*>(pOut);
		}
		else if ((propType == 2 || propType == 3) && ClientIsReadableRange(pOut, sizeof(float) * 3)) {
			out[0] = reinterpret_cast<float*>(pOut)[0];
			out[1] = reinterpret_cast<float*>(pOut)[1];
			out[2] = reinterpret_cast<float*>(pOut)[2];
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		out[0] = NAN;
		out[1] = NAN;
		out[2] = NAN;
	}
}

static bool ClientProxyValueHasNaN(const ClientRecvProxyDataTrace* data, int fallbackType)
{
	if (!data)
		return false;
	const int type = data->m_Value.m_Type >= 0 && data->m_Value.m_Type <= 7 ? data->m_Value.m_Type : fallbackType;
	if (type == 1)
		return !std::isfinite(data->m_Value.m_Float);
	if (type == 2 || type == 3)
		return !std::isfinite(data->m_Value.m_Vector[0]) || !std::isfinite(data->m_Value.m_Vector[1]) || !std::isfinite(data->m_Value.m_Vector[2]);
	return false;
}

static void __fastcall ClientRecvProxyTrace(const void* rawData, void* pStruct, void* pOut)
{
	const auto* data = reinterpret_cast<const ClientRecvProxyDataTrace*>(rawData);
	const __int64 recvProp = data ? data->m_pRecvProp : 0;
	ClientRecvProxyTraceEntry* const entry = ClientFindRecvProxyTraceEntry(recvProp);
	ClientRecvVarProxyFn original = entry ? entry->original : nullptr;

	float before[3];
	ClientReadProxyOutFloats(pOut, entry ? entry->type : -1, before);
	if (original)
		original(rawData, pStruct, pOut);

	if (!entry || s_ClientRecvProxyTraceLogBudget <= 0)
		return;

	void* const local = ClientGetLocalPlayerEntityForTrace();
	const long long relOut = (local && pOut) ? static_cast<long long>(reinterpret_cast<char*>(pOut) - reinterpret_cast<char*>(local)) : 0x7fffffffffffffffLL;
	const long long relStruct = (local && pStruct) ? static_cast<long long>(reinterpret_cast<char*>(pStruct) - reinterpret_cast<char*>(local)) : 0x7fffffffffffffffLL;
	const bool nearRoll = relOut >= 13680 && relOut <= 13776;
	const bool nearClassMods = relOut >= 11248 && relOut <= 11320;
	const bool objectOne = data && data->m_ObjectID == 1;
	const bool interestingName = ClientIsInterestingRecvProxyName(entry->name);
	const bool valueHasNaN = ClientProxyValueHasNaN(data, entry->type);
	float after[3];
	ClientReadProxyOutFloats(pOut, entry->type, after);
	const bool afterHasNaN = (entry->type == 1 && !std::isfinite(after[0]))
		|| ((entry->type == 2 || entry->type == 3) && (!std::isfinite(after[0]) || !std::isfinite(after[1]) || !std::isfinite(after[2])));
	if (!interestingName && !nearRoll && !nearClassMods && !valueHasNaN && !afterHasNaN)
		return;

	--s_ClientRecvProxyTraceLogBudget;
	char valueText[128];
	ClientFormatProxyValue(valueText, sizeof(valueText), data, entry->type);
	char buffer[1536];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: recvproxy-trace table=DT_HL2_Player prop=\"%s\" recv=%p type=%d recvOff=%d obj=%d elem=%d valueType=%d value=%s pStruct=%p pOut=%p local=%p relStruct=%lld relOut=%lld nearRoll=%d nearClassMods=%d before=(%.9g,%.9g,%.9g) after=(%.9g,%.9g,%.9g) valueNaN=%d afterNaN=%d original=%p budget=%d\n",
		entry->name,
		reinterpret_cast<void*>(entry->recvProp),
		entry->type,
		entry->offset,
		data ? data->m_ObjectID : -1,
		data ? data->m_iElement : -1,
		data ? data->m_Value.m_Type : -1,
		valueText,
		pStruct,
		pOut,
		local,
		relStruct,
		relOut,
		nearRoll ? 1 : 0,
		nearClassMods ? 1 : 0,
		before[0], before[1], before[2],
		after[0], after[1], after[2],
		valueHasNaN ? 1 : 0,
		afterHasNaN ? 1 : 0,
		reinterpret_cast<void*>(original),
		s_ClientRecvProxyTraceLogBudget);
	OutputDebugStringA(buffer);
	Warning("%s", buffer);
}

static void ClientMaybeInstallHL2PlayerRecvProxyTrace(__int64 decoder)
{
	if (!ShouldInstallR1OClientDebugHooks() || s_ClientRecvProxyTraceInstalled || !decoder || IsDedicatedServer())
		return;

	const int flatCount = ClientRecvDecoderFlattenedPropCount(decoder);
	int installed = 0;
	int skipped = 0;
	for (int i = 0; i < flatCount && i < 4096 && s_ClientRecvProxyTraceEntryCount < static_cast<int>(sizeof(s_ClientRecvProxyTraceEntries) / sizeof(s_ClientRecvProxyTraceEntries[0])); ++i) {
		const __int64 recvProp = ClientRecvDecoderRecvProp(decoder, static_cast<unsigned int>(i));
		if (!recvProp || ClientFindRecvProxyTraceEntry(recvProp))
			continue;
		const int type = ClientRecvPropType(recvProp);
		if (type == 5 || type == 6) {
			++skipped;
			continue;
		}
		ClientRecvVarProxyFn const original = ClientRecvPropProxyFn(recvProp);
		if (!original || original == &ClientRecvProxyTrace) {
			++skipped;
			continue;
		}

		ClientRecvProxyTraceEntry& entry = s_ClientRecvProxyTraceEntries[s_ClientRecvProxyTraceEntryCount];
		entry.recvProp = recvProp;
		entry.original = original;
		entry.type = type;
		entry.offset = ClientRecvPropOffset(recvProp);
		const char* name = ClientRecvPropName(recvProp);
		strncpy_s(entry.name, sizeof(entry.name), name ? name : "<null>", _TRUNCATE);
		if (ClientSetRecvPropProxyFn(recvProp, &ClientRecvProxyTrace)) {
			++s_ClientRecvProxyTraceEntryCount;
			++installed;
		}
		else {
			entry = {};
			++skipped;
		}
	}

	s_ClientRecvProxyTraceInstalled = installed > 0;
	char buffer[512];
	_snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
		"R1Delta: recvproxy-trace install DT_HL2_Player flat=%d installed=%d skipped=%d entries=%d budget=%d\n",
		flatCount,
		installed,
		skipped,
		s_ClientRecvProxyTraceEntryCount,
		s_ClientRecvProxyTraceLogBudget);
	OutputDebugStringA(buffer);
	Warning("%s", buffer);
}

static __int64 ClientFindRecvTableSafe(const char* name)
{
	if (!s_ClientFindRecvTableByName || !name)
		return 0;

	__try {
		return s_ClientFindRecvTableByName(name);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return 0;
	}
}

static __int64 ClientFindFlatRecvProp(__int64 decoder, const char* name, int occurrence = 0)
{
	if (!decoder || !name || occurrence < 0)
		return 0;

	const int flatCount = ClientRecvDecoderFlattenedPropCount(decoder);
	int seen = 0;
	for (int i = 0; i < flatCount && i < 4096; ++i) {
		const __int64 prop = ClientRecvDecoderRecvProp(decoder, static_cast<unsigned int>(i));
		if (!prop)
			continue;
		const char* propName = ClientRecvPropName(prop);
		if (propName && !_stricmp(propName, name)) {
			if (seen == occurrence)
				return prop;
			++seen;
		}
	}
	return 0;
}

static int ClientSafeReadIntField(__int64 object, int offset, int defaultValue = -999999)
{
	if (!object || offset < 0 || offset > 0x2000000)
		return defaultValue;

	__try {
		const void* ptr = reinterpret_cast<const void*>(object + offset);
		if (!ClientIsReadableRange(ptr, sizeof(int)))
			return defaultValue;
		return *reinterpret_cast<const int*>(ptr);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return defaultValue;
	}
}

static unsigned char ClientSafeReadByteField(__int64 object, int offset, unsigned char defaultValue = 0xff)
{
	if (!object || offset < 0 || offset > 0x2000000)
		return defaultValue;

	__try {
		const void* ptr = reinterpret_cast<const void*>(object + offset);
		if (!ClientIsReadableRange(ptr, sizeof(unsigned char)))
			return defaultValue;
		return *reinterpret_cast<const unsigned char*>(ptr);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return defaultValue;
	}
}

static float ClientSafeReadFloatField(__int64 object, int offset)
{
	if (!object || offset < 0 || offset > 0x2000000)
		return NAN;

	__try {
		const void* ptr = reinterpret_cast<const void*>(object + offset);
		if (!ClientIsReadableRange(ptr, sizeof(float)))
			return NAN;
		return *reinterpret_cast<const float*>(ptr);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return NAN;
	}
}

static int ClientSafeCallBoolVFunc(void* ent, size_t vtableOffset)
{
	if (!ent)
		return -1;

	__try {
		if (!ClientIsReadableRange(ent, sizeof(void*)))
			return -1;
		void* const vtable = *reinterpret_cast<void**>(ent);
		if (!ClientIsReadableRange(reinterpret_cast<const char*>(vtable) + vtableOffset, sizeof(void*)))
			return -1;
		void* const target = *reinterpret_cast<void**>(reinterpret_cast<char*>(vtable) + vtableOffset);
		if (!target)
			return -1;
		return reinterpret_cast<unsigned char(__fastcall*)(void*)>(target)(ent) ? 1 : 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return -2;
	}
}

static bool ClientReadVectorVFunc(void* ent, size_t vtableOffset, float out[3])
{
	if (out) {
		out[0] = NAN;
		out[1] = NAN;
		out[2] = NAN;
	}
	if (!ent || !out)
		return false;

	__try {
		if (!ClientIsReadableRange(ent, sizeof(void*)))
			return false;
		void* const vtable = *reinterpret_cast<void**>(ent);
		if (!ClientIsReadableRange(reinterpret_cast<const char*>(vtable) + vtableOffset, sizeof(void*)))
			return false;

		using GetVectorPtrType = const float* (__fastcall*)(void*);
		const auto fn = *reinterpret_cast<GetVectorPtrType*>(reinterpret_cast<char*>(vtable) + vtableOffset);
		if (!fn)
			return false;
		const float* value = fn(ent);
		if (!ClientIsReadableRange(value, sizeof(float) * 3))
			return false;
		out[0] = value[0];
		out[1] = value[1];
		out[2] = value[2];
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		out[0] = NAN;
		out[1] = NAN;
		out[2] = NAN;
		return false;
	}
}

static bool ClientCallVectorOutVFunc(void* ent, size_t vtableOffset, float out[3], void** targetOut = nullptr)
{
	if (out) {
		out[0] = NAN;
		out[1] = NAN;
		out[2] = NAN;
	}
	if (targetOut)
		*targetOut = nullptr;
	if (!ent || !out)
		return false;

	__try {
		if (!ClientIsReadableRange(ent, sizeof(void*)))
			return false;
		void* const vtable = *reinterpret_cast<void**>(ent);
		if (!ClientIsReadableRange(reinterpret_cast<const char*>(vtable) + vtableOffset, sizeof(void*)))
			return false;

		using GetVectorOutType = float* (__fastcall*)(void*, float*);
		const auto fn = *reinterpret_cast<GetVectorOutType*>(reinterpret_cast<char*>(vtable) + vtableOffset);
		if (targetOut)
			*targetOut = reinterpret_cast<void*>(fn);
		if (!fn)
			return false;
		const float* value = fn(ent, out);
		if (value && value != out && ClientIsReadableRange(value, sizeof(float) * 3)) {
			out[0] = value[0];
			out[1] = value[1];
			out[2] = value[2];
		}
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		out[0] = NAN;
		out[1] = NAN;
		out[2] = NAN;
		return false;
	}
}

static __int64 ClientClientClassDataForIndex(int classIndex)
{
	if (!G_client || classIndex < 0 || classIndex >= 30)
		return 0;
	return static_cast<__int64>(G_client + 0xBF6EB0) + 11584LL * classIndex;
}

static int ClientEffectivePlayerClassIndex(void* player)
{
	const __int64 base = reinterpret_cast<__int64>(player);
	int classIndex = ClientSafeReadIntField(base, 0x2AD0);
	if (classIndex < 0)
		classIndex = ClientSafeReadIntField(base, 0x2AC0);
	return classIndex;
}

static const char* ClientSafeClassName(__int64 classData)
{
	const char* const name = reinterpret_cast<const char*>(classData);
	return ClientIsReadableCString(name) ? name : "<bad-class>";
}

static void ClientReadClassModSettings(__int64 base, int offset, float out[8])
{
	for (int i = 0; i < 8; ++i)
		out[i] = ClientSafeReadFloatField(base, offset + i * 4);
}

static int ClientPlayerClassCount()
{
	return G_client ? ClientSafeReadIntField(static_cast<__int64>(G_client), 0xBF6E54, -1) : -1;
}

static void ClientLogPlayerClassTable(const char* reason, __int64 result)
{
	if (!ShouldInstallR1OClientDebugHooks() || s_ClientPlayerClassLoadLogBudget <= 0)
		return;

	--s_ClientPlayerClassLoadLogBudget;
	const int count = ClientPlayerClassCount();
	const int currentLoading = G_client ? ClientSafeReadIntField(static_cast<__int64>(G_client), 0xBF6E58, -1) : -1;
	char samples[512] = {};
	char* cursor = samples;
	size_t remaining = sizeof(samples);
	const int sampleCount = count > 6 ? 6 : count;
	for (int i = 0; i < sampleCount && remaining > 1; ++i) {
		const __int64 classData = ClientClientClassDataForIndex(i);
		float classMods[8];
		ClientReadClassModSettings(classData, 0xC2C, classMods);
		const int written = _snprintf_s(
			cursor,
			remaining,
			_TRUNCATE,
			" [%d]=\"%s\" dodgeSpeed=%.9g health=%.9g",
			i,
			classData ? ClientSafeClassName(classData) : "<null>",
			classMods[3],
			classMods[0]);
		if (written <= 0)
			break;
		const size_t used = static_cast<size_t>(written) < remaining ? static_cast<size_t>(written) : remaining - 1;
		cursor += used;
		remaining -= used;
	}

	char buffer[900];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: client player-class-load reason=%s result=%lld count=%d current=%d samples:%s budget=%d\n",
		reason ? reason : "<null>",
		static_cast<long long>(result),
		count,
		currentLoading,
		samples[0] ? samples : " <none>",
		s_ClientPlayerClassLoadLogBudget);
	OutputDebugStringA(buffer);
	Warning("%s", buffer);
}

static bool ClientNormalizePlayerClassResourcePath(const char* resourceName, char* outPath, size_t outPathSize)
{
	if (!ClientIsReadableCString(resourceName) || !outPath || outPathSize == 0)
		return false;

	char scratch[MAX_PATH * 2];
	_snprintf_s(scratch, sizeof(scratch), _TRUNCATE, "%s", resourceName);
	for (char* it = scratch; *it; ++it) {
		if (*it == '\\')
			*it = '/';
		else
			*it = static_cast<char>(tolower(static_cast<unsigned char>(*it)));
	}

	const char* source = scratch;
	const char* cacheMarker = strstr(source, "r1delta_r1o_vpk_cache/");
	if (cacheMarker)
		source = cacheMarker + strlen("r1delta_r1o_vpk_cache/");
	else {
		const char* modMarker = strstr(source, "/r1delta/");
		if (modMarker)
			source = modMarker + strlen("/r1delta/");
	}

	while (*source == '/' || *source == '\\')
		++source;

	if (source[0] && source[1] == ':')
		return false;

	_snprintf_s(outPath, outPathSize, _TRUNCATE, "%s", source);
	for (char* it = outPath; *it; ++it) {
		if (*it == '\\')
			*it = '/';
		else
			*it = static_cast<char>(tolower(static_cast<unsigned char>(*it)));
	}

	return outPath[0] != '\0';
}

static bool ClientIsPlayerClassKVResource(const char* resourceName)
{
	char relative[MAX_PATH * 2];
	if (!ClientNormalizePlayerClassResourcePath(resourceName, relative, sizeof(relative)))
		return false;

	// The R1 2015 client should load multiplayer player-class KeyValues from the
	// active game content.  In the R1O/TFO fake-dedi client path those files can be
	// absent from the legacy R1 search path, so the fallback is intentionally
	// limited to the MP player-class manifest and its imported .set files.
	if (_strnicmp(relative, "scripts/players/mp/", strlen("scripts/players/mp/")) != 0)
		return false;

	const char* ext = strrchr(relative, '.');
	return ext && (!_stricmp(ext, ".txt") || !_stricmp(ext, ".set"));
}

static bool ClientBuildR1DeltaLoosePath(const char* relativeResourcePath, char* outPath, size_t outPathSize)
{
	if (!relativeResourcePath || !relativeResourcePath[0] || !outPath || !outPathSize)
		return false;

	char exePath[MAX_PATH];
	const DWORD len = GetModuleFileNameA(nullptr, exePath, sizeof(exePath));
	if (!len || len >= sizeof(exePath))
		return false;

	char* slash = strrchr(exePath, '\\');
	if (!slash)
		return false;
	*slash = '\0';

	char relativeBackslash[MAX_PATH * 2];
	_snprintf_s(relativeBackslash, sizeof(relativeBackslash), _TRUNCATE, "%s", relativeResourcePath);
	for (char* it = relativeBackslash; *it; ++it) {
		if (*it == '/')
			*it = '\\';
	}

	_snprintf_s(outPath, outPathSize, _TRUNCATE, "%s\\r1delta\\%s", exePath, relativeBackslash);
	const DWORD attributes = GetFileAttributesA(outPath);
	return outPath[0] != '\0' && attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static void ClientLogPlayerClassKVLoad(
	const char* phase,
	const char* resourceName,
	const char* pathId,
	const char* fallbackPath,
	unsigned char result)
{
	if (!ShouldInstallR1OClientDebugHooks() || s_ClientPlayerClassKVLogBudget <= 0)
		return;

	--s_ClientPlayerClassKVLogBudget;
	char buffer[1024];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: client player-class KeyValues %s resource=\"%s\" pathId=\"%s\" fallback=\"%s\" result=%u count=%d budget=%d\n",
		phase ? phase : "<null>",
		ClientIsReadableCString(resourceName) ? resourceName : "<invalid>",
		ClientIsReadableCString(pathId) ? pathId : "<null>",
		fallbackPath ? fallbackPath : "<none>",
		static_cast<unsigned int>(result),
		ClientPlayerClassCount(),
		s_ClientPlayerClassKVLogBudget);
	OutputDebugStringA(buffer);
	Warning("%s", buffer);
}

static unsigned char __fastcall ClientKeyValuesLoadFromFile65F980(
	void* keyValues,
	void* fileSystem,
	const char* resourceName,
	const char* pathId,
	void* unknown)
{
	if (!s_ClientKeyValuesLoadFromFile65F980Original)
		return 0;

	const unsigned char originalResult = s_ClientKeyValuesLoadFromFile65F980Original(keyValues, fileSystem, resourceName, pathId, unknown);
	if (!ClientIsPlayerClassKVResource(resourceName) || s_ClientInsidePlayerClassKVFallback) {
		return originalResult;
	}

	ClientLogPlayerClassKVLoad("original", resourceName, pathId, nullptr, originalResult);
	if (originalResult)
		return originalResult;

	char relative[MAX_PATH * 2];
	if (!ClientNormalizePlayerClassResourcePath(resourceName, relative, sizeof(relative)))
		return originalResult;

	s_ClientInsidePlayerClassKVFallback = true;
	unsigned char fallbackResult = 0;

	char loosePath[MAX_PATH * 2];
	if (ClientBuildR1DeltaLoosePath(relative, loosePath, sizeof(loosePath))) {
		fallbackResult = s_ClientKeyValuesLoadFromFile65F980Original(keyValues, fileSystem, loosePath, nullptr, unknown);
		ClientLogPlayerClassKVLoad("loose-r1delta", resourceName, pathId, loosePath, fallbackResult);
	}

	if (!fallbackResult && fileSystem) {
		InstallR1OVPKFileSystemHooks(fileSystem);
		SetR1OVPKClientFallbackActive(true);
		fallbackResult = s_ClientKeyValuesLoadFromFile65F980Original(keyValues, fileSystem, relative, "GAME", unknown);
		SetR1OVPKClientFallbackActive(false);
		ClientLogPlayerClassKVLoad(
			fallbackResult ? "r1o-vpk-memory" : "r1o-vpk-miss",
			resourceName,
			pathId,
			relative,
			fallbackResult);
	}

	s_ClientInsidePlayerClassKVFallback = false;
	return fallbackResult;
}

static __int64 __fastcall ClientLoadDataList45BD10(
	const char* listName,
	const char* directory,
	const char* fileName,
	void(__fastcall* callback)(__int64, unsigned char*))
{
	const int beforeCount = ClientPlayerClassCount();
	__int64 result = s_ClientLoadDataList45BD10Original
		? s_ClientLoadDataList45BD10Original(listName, directory, fileName, callback)
		: 0;
	const int afterCount = ClientPlayerClassCount();

	const bool isPlayerClassManifest = ClientIsReadableCString(listName)
		&& ClientIsReadableCString(fileName)
		&& !_stricmp(listName, "player class")
		&& !_stricmp(fileName, "classes.txt");
	if (ShouldInstallR1OClientDebugHooks() && isPlayerClassManifest && s_ClientPlayerClassDataListLogBudget > 0) {
		--s_ClientPlayerClassDataListLogBudget;
		char buffer[1024];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: client data-list 45BD10 list=\"%s\" dir=\"%s\" file=\"%s\" result=%lld count=%d->%d budget=%d\n",
			ClientIsReadableCString(listName) ? listName : "<invalid>",
			ClientIsReadableCString(directory) ? directory : "<invalid>",
			ClientIsReadableCString(fileName) ? fileName : "<invalid>",
			static_cast<long long>(result),
			beforeCount,
			afterCount,
			s_ClientPlayerClassDataListLogBudget);
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}

	if (isPlayerClassManifest
		&& afterCount == 0
		&& ClientIsReadableCString(directory)
		&& _stricmp(directory, "scripts/players/mp")
		&& s_ClientLoadDataList45BD10Original) {
		const int fallbackBeforeCount = ClientPlayerClassCount();
		result = s_ClientLoadDataList45BD10Original(listName, "scripts/players/mp", fileName, callback);
		const int fallbackAfterCount = ClientPlayerClassCount();
		if (ShouldInstallR1OClientDebugHooks() && s_ClientPlayerClassDataListLogBudget > 0) {
			--s_ClientPlayerClassDataListLogBudget;
			char buffer[1024];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: client data-list 45BD10 mp-fallback originalDir=\"%s\" file=\"%s\" result=%lld count=%d->%d budget=%d\n",
				ClientIsReadableCString(directory) ? directory : "<invalid>",
				ClientIsReadableCString(fileName) ? fileName : "<invalid>",
				static_cast<long long>(result),
				fallbackBeforeCount,
				fallbackAfterCount,
				s_ClientPlayerClassDataListLogBudget);
			OutputDebugStringA(buffer);
			Warning("%s", buffer);
		}
	}

	return result;
}

static void ClientEnsurePlayerClassesLoaded(const char* reason)
{
	if (!G_client || s_ClientInsidePlayerClassLoad || ClientPlayerClassCount() > 0 || s_ClientPlayerClassLoadEnsureAttempts >= 3)
		return;

	++s_ClientPlayerClassLoadEnsureAttempts;
	ClientLogPlayerClassTable(reason ? reason : "ensure-before", 0);
	const auto loader = s_ClientLoadPlayerClasses109E80Original
		? s_ClientLoadPlayerClasses109E80Original
		: reinterpret_cast<ClientLoadPlayerClasses109E80Type>(G_client + 0x109E80);
	__int64 result = 0;
	if (loader) {
		s_ClientInsidePlayerClassLoad = true;
		__try {
			result = loader();
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			result = -1;
		}
		s_ClientInsidePlayerClassLoad = false;
	}
	ClientLogPlayerClassTable(reason ? reason : "ensure-after", result);
}

static __int64 __fastcall ClientLoadPlayerClasses109E80()
{
	ClientLogPlayerClassTable("109E80-before", 0);
	s_ClientInsidePlayerClassLoad = true;
	const __int64 result = s_ClientLoadPlayerClasses109E80Original ? s_ClientLoadPlayerClasses109E80Original() : 0;
	s_ClientInsidePlayerClassLoad = false;
	ClientLogPlayerClassTable("109E80-after", result);
	return result;
}

static void ClientReadRollLifecycleFields(void* player, float up[3], float roll[3]);
static bool ClientRollLifecycleHasNaN(const float up[3], const float roll[3]);

static void ClientLogClassApply03FBB0(const char* phase, void* player, char force, __int64 result)
{
	if (!ShouldInstallR1OClientDebugHooks() || s_ClientClassApplyLogBudget <= 0)
		return;

	const __int64 base = reinterpret_cast<__int64>(player);
	const int activeClass = ClientSafeReadIntField(base, 0x2AD0);
	const int requestedClass = ClientSafeReadIntField(base, 0x2AC0);
	const int lastClass = ClientSafeReadIntField(base, 0x2ACC);
	const int effectiveClass = activeClass >= 0 ? activeClass : requestedClass;
	const int activeMods = ClientSafeReadIntField(base, 0x2C00);
	const int lastMods = ClientSafeReadIntField(base, 0x2C04);
	const __int64 classData = ClientClientClassDataForIndex(effectiveClass);
	float playerMods[8];
	float classMods[8];
	ClientReadClassModSettings(base, 0x2C08, playerMods);
	ClientReadClassModSettings(classData, 0xC2C, classMods);
	float up[3];
	float roll[3];
	ClientReadRollLifecycleFields(player, up, roll);
	void* const local = ClientGetLocalPlayerEntityForTrace();
	const bool isLocal = player && local && player == local;
	const bool suspicious = !std::isfinite(playerMods[3]) || playerMods[3] <= 0.0f
		|| !std::isfinite(classMods[3]) || classMods[3] <= 0.0f
		|| ClientRollLifecycleHasNaN(up, roll);
	if (!isLocal && !suspicious && ClientLocalSignonState() != 8)
		return;

	--s_ClientClassApplyLogBudget;
	char buffer[1600];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: client class-apply %s player=%p local=%p isLocal=%d signon=%d force=%d result=%lld class active/request/last/effective=%d/%d/%d/%d classData=%p className=\"%s\" mods=%d/%d playerMods[health,powerRegen,dodgeDur,dodgeSpeed,dodgeDrain,smartAmmo,wallrun,wallhang]=(%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g) classMods=(%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g) up=(%.9g,%.9g,%.9g) roll=(%.9g,%.9g,%.9g) suspicious=%d budget=%d\n",
		phase ? phase : "<null>",
		player,
		local,
		isLocal ? 1 : 0,
		ClientLocalSignonState(),
		static_cast<int>(force),
		static_cast<long long>(result),
		activeClass,
		requestedClass,
		lastClass,
		effectiveClass,
		reinterpret_cast<void*>(classData),
		classData ? ClientSafeClassName(classData) : "<null-class>",
		activeMods,
		lastMods,
		playerMods[0], playerMods[1], playerMods[2], playerMods[3], playerMods[4], playerMods[5], playerMods[6], playerMods[7],
		classMods[0], classMods[1], classMods[2], classMods[3], classMods[4], classMods[5], classMods[6], classMods[7],
		up[0], up[1], up[2],
		roll[0], roll[1], roll[2],
		suspicious ? 1 : 0,
		s_ClientClassApplyLogBudget);
	OutputDebugStringA(buffer);
	Warning("%s", buffer);
}

static void ClientReadRollLifecycleFields(void* player, float up[3], float roll[3])
{
	up[0] = up[1] = up[2] = NAN;
	roll[0] = roll[1] = roll[2] = NAN;
	const __int64 base = reinterpret_cast<__int64>(player);
	up[0] = ClientSafeReadFloatField(base, 3393 * 4);
	up[1] = ClientSafeReadFloatField(base, 3394 * 4);
	up[2] = ClientSafeReadFloatField(base, 3395 * 4);
	roll[0] = ClientSafeReadFloatField(base, 3433 * 4);
	roll[1] = ClientSafeReadFloatField(base, 3434 * 4);
	roll[2] = ClientSafeReadFloatField(base, 3435 * 4);
}

static bool ClientRollLifecycleHasNaN(const float up[3], const float roll[3])
{
	return !std::isfinite(up[0]) || !std::isfinite(up[1]) || !std::isfinite(up[2])
		|| !std::isfinite(roll[0]) || !std::isfinite(roll[1]) || !std::isfinite(roll[2]);
}

static void ClientLogRollLifecycle(const char* stage, void* player, const float beforeUp[3], const float beforeRoll[3], __int64 result, int vfunc1312Before, int vfunc1312After)
{
	if (s_ClientRollLifecycleLogBudget <= 0)
		return;

	float afterUp[3];
	float afterRoll[3];
	ClientReadRollLifecycleFields(player, afterUp, afterRoll);
	void* const local = ClientGetLocalPlayerEntityForTrace();
	const bool isLocal = player && local && player == local;
	const bool nanBefore = ClientRollLifecycleHasNaN(beforeUp, beforeRoll);
	const bool nanAfter = ClientRollLifecycleHasNaN(afterUp, afterRoll);
	if (!isLocal && !nanBefore && !nanAfter && ClientLocalSignonState() != 8 && s_ClientRollLifecycleLogBudget < 144)
		return;

	--s_ClientRollLifecycleLogBudget;
	const __int64 base = reinterpret_cast<__int64>(player);
	const int team = ClientSafeReadIntField(base, 464);
	const int health = ClientSafeReadIntField(base, 440);
	const int life = ClientSafeReadByteField(base, 1160, 0xff);
	const int flags = ClientSafeReadIntField(base, 480);
	const int observerMode = ClientSafeReadIntField(base, 13260);
	const int splitPlayer = ClientSafeReadIntField(base, 14856);
	const int splitActive = ClientSafeReadByteField(base, 14864, 0xff);
	char buffer[1400];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: client roll-lifecycle stage=%s player=%p local=%p isLocal=%d signon=%d vfunc1312=%d->%d team=%d health=%d life=%d flags=0x%x observer=%d splitPlayer=%d splitActive=%d beforeUp=(%.9g,%.9g,%.9g) beforeRoll=(%.9g,%.9g,%.9g) afterUp=(%.9g,%.9g,%.9g) afterRoll=(%.9g,%.9g,%.9g) nan=%d->%d result=%lld budget=%d\n",
		stage ? stage : "<null>",
		player,
		local,
		isLocal ? 1 : 0,
		ClientLocalSignonState(),
		vfunc1312Before,
		vfunc1312After,
		team,
		health,
		life,
		flags,
		observerMode,
		splitPlayer,
		splitActive,
		beforeUp[0], beforeUp[1], beforeUp[2],
		beforeRoll[0], beforeRoll[1], beforeRoll[2],
		afterUp[0], afterUp[1], afterUp[2],
		afterRoll[0], afterRoll[1], afterRoll[2],
		nanBefore ? 1 : 0,
		nanAfter ? 1 : 0,
		static_cast<long long>(result),
		s_ClientRollLifecycleLogBudget);
	OutputDebugStringA(buffer);
	Warning("%s", buffer);
}

static __int64 __fastcall ClientRollReset08E6E0(void* player)
{
	float beforeUp[3];
	float beforeRoll[3];
	ClientReadRollLifecycleFields(player, beforeUp, beforeRoll);
	const int vfuncBefore = ClientSafeCallBoolVFunc(player, 1312);
	const __int64 result = s_ClientRollReset08E6E0Original ? s_ClientRollReset08E6E0Original(player) : 0;
	const int vfuncAfter = ClientSafeCallBoolVFunc(player, 1312);
	ClientLogRollLifecycle("08E6E0", player, beforeUp, beforeRoll, result, vfuncBefore, vfuncAfter);
	return result;
}

static __int64 __fastcall ClientRollLifecycle34CEF0(void* player)
{
	float beforeUp[3];
	float beforeRoll[3];
	ClientReadRollLifecycleFields(player, beforeUp, beforeRoll);
	const int vfuncBefore = ClientSafeCallBoolVFunc(player, 1312);
	const __int64 result = s_ClientRollLifecycle34CEF0Original ? s_ClientRollLifecycle34CEF0Original(player) : 0;
	const int vfuncAfter = ClientSafeCallBoolVFunc(player, 1312);
	ClientLogRollLifecycle("34CEF0", player, beforeUp, beforeRoll, result, vfuncBefore, vfuncAfter);
	return result;
}

static __int64 __fastcall ClientApplyPlayerClassMods03FBB0(void* player, char force)
{
	ClientEnsurePlayerClassesLoaded("class-apply-03FBB0");
	ClientLogClassApply03FBB0("before03FBB0", player, force, 0);
	const __int64 result = s_ClientApplyPlayerClassMods03FBB0Original
		? s_ClientApplyPlayerClassMods03FBB0Original(player, force)
		: 0;
	ClientLogClassApply03FBB0("after03FBB0", player, force, result);
	return result;
}

static void ClientLogSetClassVar041830(const char* phase, __int64 args, __int64 result)
{
	if (s_ClientSetClassVarLogBudget <= 0)
		return;

	void* const player = ClientGetLocalPlayerEntityForTrace();
	const __int64 base = reinterpret_cast<__int64>(player);
	const int argc = ClientSafeReadIntField(args, 0);
	const char* key = "<none>";
	const char* value = "<none>";
	__try {
		if (argc > 1) {
			const char* candidate = *reinterpret_cast<const char**>(args + 1048);
			if (ClientIsReadableCString(candidate))
				key = candidate;
		}
		if (argc > 2) {
			const char* candidate = *reinterpret_cast<const char**>(args + 1056);
			if (ClientIsReadableCString(candidate))
				value = candidate;
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		key = "<av>";
		value = "<av>";
	}

	float playerMods[8];
	ClientReadClassModSettings(base, 0x2C08, playerMods);
	--s_ClientSetClassVarLogBudget;
	char buffer[1024];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: client set-class-var %s args=%p argc=%d key=\"%s\" value=\"%s\" player=%p signon=%d class=%d mods=%d dodgeSpeed=%.9g result=%lld budget=%d\n",
		phase ? phase : "<null>",
		reinterpret_cast<void*>(args),
		argc,
		key,
		value,
		player,
		ClientLocalSignonState(),
		ClientEffectivePlayerClassIndex(player),
		ClientSafeReadIntField(base, 0x2C00),
		playerMods[3],
		static_cast<long long>(result),
		s_ClientSetClassVarLogBudget);
	OutputDebugStringA(buffer);
	Warning("%s", buffer);
}

static __int64 __fastcall ClientSetClassVar041830(__int64 args)
{
	ClientLogSetClassVar041830("before041830", args, 0);
	const __int64 result = s_ClientSetClassVar041830Original ? s_ClientSetClassVar041830Original(args) : 0;
	ClientLogSetClassVar041830("after041830", args, result);
	return result;
}

static bool ClientReadMainViewSlot(int slot, float origin[3], float angles[3])
{
	if (origin) {
		origin[0] = NAN;
		origin[1] = NAN;
		origin[2] = NAN;
	}
	if (angles) {
		angles[0] = NAN;
		angles[1] = NAN;
		angles[2] = NAN;
	}
	if (!G_client || slot < 0 || slot >= 4 || !origin || !angles)
		return false;

	// R1 2015 client.dll: MainViewOrigin(slot) returns &client.dll+0x1508650[3*slot],
	// and MainViewAngles(slot) returns &client.dll+0x1508E30[3*slot].
	const float* originPtr = reinterpret_cast<const float*>(G_client + 0x1508650 + sizeof(float) * 3 * slot);
	const float* anglesPtr = reinterpret_cast<const float*>(G_client + 0x1508E30 + sizeof(float) * 3 * slot);
	__try {
		if (!ClientIsReadableRange(originPtr, sizeof(float) * 3) || !ClientIsReadableRange(anglesPtr, sizeof(float) * 3))
			return false;
		origin[0] = originPtr[0];
		origin[1] = originPtr[1];
		origin[2] = originPtr[2];
		angles[0] = anglesPtr[0];
		angles[1] = anglesPtr[1];
		angles[2] = anglesPtr[2];
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		origin[0] = NAN;
		origin[1] = NAN;
		origin[2] = NAN;
		angles[0] = NAN;
		angles[1] = NAN;
		angles[2] = NAN;
		return false;
	}
}

static void* ClientLocalPlayerEntity()
{
	if (!G_client)
		return nullptr;

	__try {
		using GetClientEntitySelfType = void* (*)(int);
		return reinterpret_cast<GetClientEntitySelfType>(G_client + 0x7B1B0)(-1);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return nullptr;
	}
}

static bool ClientIsFiniteVector(const float v[3])
{
	return v && isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]);
}

static bool ClientCopyVectorPointer(const float* ptr, float out[3])
{
	if (out) {
		out[0] = NAN;
		out[1] = NAN;
		out[2] = NAN;
	}
	if (!ptr || !out)
		return false;

	__try {
		if (!ClientIsReadableRange(ptr, sizeof(float) * 3))
			return false;
		out[0] = ptr[0];
		out[1] = ptr[1];
		out[2] = ptr[2];
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		out[0] = NAN;
		out[1] = NAN;
		out[2] = NAN;
		return false;
	}
}

static __int64 __fastcall ClientNormalCalcView(void* player, float* eyeOrigin, float* eyeAngles, float* fov)
{
	float beforeOrigin[3];
	float beforeAngles[3];
	const bool beforeOriginOk = ClientCopyVectorPointer(eyeOrigin, beforeOrigin);
	const bool beforeAnglesOk = ClientCopyVectorPointer(eyeAngles, beforeAngles);
	float beforeFov = NAN;
	__try {
		if (fov && ClientIsReadableRange(fov, sizeof(float)))
			beforeFov = *fov;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		beforeFov = NAN;
	}

	float vfuncEyeBefore[3];
	float vfuncAngBefore[3];
	void* eyeTarget = nullptr;
	void* angTarget = nullptr;
	const bool vfuncEyeBeforeOk = ClientCallVectorOutVFunc(player, 1464, vfuncEyeBefore, &eyeTarget);
	const bool vfuncAngBeforeOk = ClientCallVectorOutVFunc(player, 1472, vfuncAngBefore, &angTarget);
	int predErrorActive = -1;
	int cameraOverrideActive = -1;
	unsigned int cameraHandle11944 = 0;
	unsigned int cameraHandle13792 = 0;
	unsigned int cameraHandle13796 = 0;
	float cameraBlend11948 = NAN;
	float cameraBlend11952 = NAN;
	float cameraOverrideOrigin[3] = { NAN, NAN, NAN };
	float cameraOverrideAngles[3] = { NAN, NAN, NAN };
	float originSpringOffset[3] = { NAN, NAN, NAN };
	float angleSpringOffset[3] = { NAN, NAN, NAN };
	float predOriginOfs[3] = { NAN, NAN, NAN };
	float predAnglesOfs[3] = { NAN, NAN, NAN };
	__try {
		if (player && ClientIsReadableRange(player, 13800)) {
			const auto base = reinterpret_cast<char*>(player);
			cameraOverrideActive = *reinterpret_cast<unsigned char*>(base + 12932) ? 1 : 0;
			cameraHandle11944 = *reinterpret_cast<unsigned int*>(base + 11944);
			cameraBlend11948 = *reinterpret_cast<float*>(base + 11948);
			cameraBlend11952 = *reinterpret_cast<float*>(base + 11952);
			cameraHandle13792 = *reinterpret_cast<unsigned int*>(base + 13792);
			cameraHandle13796 = *reinterpret_cast<unsigned int*>(base + 13796);
			cameraOverrideOrigin[0] = *reinterpret_cast<float*>(base + 12940);
			cameraOverrideOrigin[1] = *reinterpret_cast<float*>(base + 12944);
			cameraOverrideOrigin[2] = *reinterpret_cast<float*>(base + 12948);
			cameraOverrideAngles[0] = *reinterpret_cast<float*>(base + 12952);
			cameraOverrideAngles[1] = *reinterpret_cast<float*>(base + 12956);
			cameraOverrideAngles[2] = *reinterpret_cast<float*>(base + 12960);
			originSpringOffset[0] = *reinterpret_cast<float*>(base + 13008);
			originSpringOffset[1] = *reinterpret_cast<float*>(base + 13012);
			originSpringOffset[2] = *reinterpret_cast<float*>(base + 13016);
			angleSpringOffset[0] = *reinterpret_cast<float*>(base + 1644);
			angleSpringOffset[1] = *reinterpret_cast<float*>(base + 1648);
			angleSpringOffset[2] = *reinterpret_cast<float*>(base + 1652);
		}
		if (G_client) {
			using PredErrorActiveType = unsigned char(__fastcall*)();
			auto predErrorActiveFn = reinterpret_cast<PredErrorActiveType>(G_client + 0xF6F60);
			predErrorActive = predErrorActiveFn ? (predErrorActiveFn() ? 1 : 0) : -1;
			float* predOriginPtr = reinterpret_cast<float*>(G_client + 0x17F92D8);
			float* predAnglesPtr = reinterpret_cast<float*>(G_client + 0x17F92E8);
			if (ClientIsReadableRange(predOriginPtr, sizeof(float) * 3)) {
				predOriginOfs[0] = predOriginPtr[0];
				predOriginOfs[1] = predOriginPtr[1];
				predOriginOfs[2] = predOriginPtr[2];
			}
			if (ClientIsReadableRange(predAnglesPtr, sizeof(float) * 3)) {
				predAnglesOfs[0] = predAnglesPtr[0];
				predAnglesOfs[1] = predAnglesPtr[1];
				predAnglesOfs[2] = predAnglesPtr[2];
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		predErrorActive = -2;
	}

	__int64 result = 0;
	if (s_ClientNormalCalcViewOriginal) {
		s_ClientInsideNormalCalcView = true;
		s_ClientCurrentNormalCalcViewPlayer = player;
		result = s_ClientNormalCalcViewOriginal(player, eyeOrigin, eyeAngles, fov);
		s_ClientCurrentNormalCalcViewPlayer = nullptr;
		s_ClientInsideNormalCalcView = false;
	}

	if (s_ClientNormalCalcViewLogBudget > 0 && ClientLocalSignonState() == 8) {
		--s_ClientNormalCalcViewLogBudget;
		float afterOrigin[3];
		float afterAngles[3];
		const bool afterOriginOk = ClientCopyVectorPointer(eyeOrigin, afterOrigin);
		const bool afterAnglesOk = ClientCopyVectorPointer(eyeAngles, afterAngles);
		float afterFov = NAN;
		__try {
			if (fov && ClientIsReadableRange(fov, sizeof(float)))
				afterFov = *fov;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			afterFov = NAN;
		}
		float vfuncEyeAfter[3];
		float vfuncAngAfter[3];
		const bool vfuncEyeAfterOk = ClientCallVectorOutVFunc(player, 1464, vfuncEyeAfter, nullptr);
		const bool vfuncAngAfterOk = ClientCallVectorOutVFunc(player, 1472, vfuncAngAfter, nullptr);
		char buffer[1536];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: client NormalCalcView player=%p predError=%d predOfs=(%.3f,%.3f,%.3f)/(%.3f,%.3f,%.3f) camOverride=%d h11944=0x%08x h13792=0x%08x h13796=0x%08x blends=(%.3f,%.3f) camCache=(%.3f,%.3f,%.3f)/(%.3f,%.3f,%.3f) springs=(%.3f,%.3f,%.3f)/(%.3f,%.3f,%.3f) beforeOk=%d/%d beforeOrigin=(%.3f,%.3f,%.3f) beforeAngles=(%.3f,%.3f,%.3f) beforeFov=%.3f vfuncBeforeOk=%d/%d eyeTarget=%p angTarget=%p eyeBefore=(%.3f,%.3f,%.3f) angBefore=(%.3f,%.3f,%.3f) afterOk=%d/%d afterOrigin=(%.3f,%.3f,%.3f) afterAngles=(%.3f,%.3f,%.3f) afterFov=%.3f vfuncAfterOk=%d/%d eyeAfter=(%.3f,%.3f,%.3f) angAfter=(%.3f,%.3f,%.3f) finiteAfter=%d result=%lld budget=%d\n",
			player,
			predErrorActive,
			predOriginOfs[0], predOriginOfs[1], predOriginOfs[2],
			predAnglesOfs[0], predAnglesOfs[1], predAnglesOfs[2],
			cameraOverrideActive,
			cameraHandle11944,
			cameraHandle13792,
			cameraHandle13796,
			cameraBlend11948,
			cameraBlend11952,
			cameraOverrideOrigin[0], cameraOverrideOrigin[1], cameraOverrideOrigin[2],
			cameraOverrideAngles[0], cameraOverrideAngles[1], cameraOverrideAngles[2],
			originSpringOffset[0], originSpringOffset[1], originSpringOffset[2],
			angleSpringOffset[0], angleSpringOffset[1], angleSpringOffset[2],
			beforeOriginOk ? 1 : 0,
			beforeAnglesOk ? 1 : 0,
			beforeOrigin[0], beforeOrigin[1], beforeOrigin[2],
			beforeAngles[0], beforeAngles[1], beforeAngles[2],
			beforeFov,
			vfuncEyeBeforeOk ? 1 : 0,
			vfuncAngBeforeOk ? 1 : 0,
			eyeTarget,
			angTarget,
			vfuncEyeBefore[0], vfuncEyeBefore[1], vfuncEyeBefore[2],
			vfuncAngBefore[0], vfuncAngBefore[1], vfuncAngBefore[2],
			afterOriginOk ? 1 : 0,
			afterAnglesOk ? 1 : 0,
			afterOrigin[0], afterOrigin[1], afterOrigin[2],
			afterAngles[0], afterAngles[1], afterAngles[2],
			afterFov,
			vfuncEyeAfterOk ? 1 : 0,
			vfuncAngAfterOk ? 1 : 0,
			vfuncEyeAfter[0], vfuncEyeAfter[1], vfuncEyeAfter[2],
			vfuncAngAfter[0], vfuncAngAfter[1], vfuncAngAfter[2],
			ClientIsFiniteVector(afterOrigin) ? 1 : 0,
			static_cast<long long>(result),
			s_ClientNormalCalcViewLogBudget);
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}

	return result;
}

static __int64 __fastcall ClientCameraStage03F170(void* player, float* eyeOrigin, float* eyeAngles)
{
	float beforeOrigin[3];
	float beforeAngles[3];
	const bool beforeOriginOk = ClientCopyVectorPointer(eyeOrigin, beforeOrigin);
	const bool beforeAnglesOk = ClientCopyVectorPointer(eyeAngles, beforeAngles);
	const __int64 result = s_ClientCameraStage03F170Original
		? s_ClientCameraStage03F170Original(player, eyeOrigin, eyeAngles)
		: 0;
	if (s_ClientInsideNormalCalcView && s_ClientCameraStage03F170LogBudget > 0 && ClientLocalSignonState() == 8) {
		--s_ClientCameraStage03F170LogBudget;
		float afterOrigin[3];
		float afterAngles[3];
		const bool afterOriginOk = ClientCopyVectorPointer(eyeOrigin, afterOrigin);
		const bool afterAnglesOk = ClientCopyVectorPointer(eyeAngles, afterAngles);
		char buffer[640];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: client view-stage 03F170 player=%p beforeOk=%d/%d before=(%.3f,%.3f,%.3f)/(%.3f,%.3f,%.3f) afterOk=%d/%d after=(%.3f,%.3f,%.3f)/(%.3f,%.3f,%.3f) finiteAfter=%d result=%lld budget=%d\n",
			player,
			beforeOriginOk ? 1 : 0,
			beforeAnglesOk ? 1 : 0,
			beforeOrigin[0], beforeOrigin[1], beforeOrigin[2],
			beforeAngles[0], beforeAngles[1], beforeAngles[2],
			afterOriginOk ? 1 : 0,
			afterAnglesOk ? 1 : 0,
			afterOrigin[0], afterOrigin[1], afterOrigin[2],
			afterAngles[0], afterAngles[1], afterAngles[2],
			ClientIsFiniteVector(afterOrigin) ? 1 : 0,
			static_cast<long long>(result),
			s_ClientCameraStage03F170LogBudget);
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}
	return result;
}

static void ClientLogViewStage(const char* name, void* player, float* eyeOrigin, float* eyeAngles, const float beforeOrigin[3], const float beforeAngles[3], bool beforeOriginOk, bool beforeAnglesOk, __int64 result)
{
	if (!s_ClientInsideNormalCalcView || s_ClientCameraStageLogBudget <= 0 || ClientLocalSignonState() != 8)
		return;
	--s_ClientCameraStageLogBudget;
	float afterOrigin[3];
	float afterAngles[3];
	const bool afterOriginOk = ClientCopyVectorPointer(eyeOrigin, afterOrigin);
	const bool afterAnglesOk = ClientCopyVectorPointer(eyeAngles, afterAngles);
	char buffer[768];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: client view-stage %s player=%p beforeOk=%d/%d before=(%.3f,%.3f,%.3f)/(%.3f,%.3f,%.3f) afterOk=%d/%d after=(%.3f,%.3f,%.3f)/(%.3f,%.3f,%.3f) finiteAfter=%d result=%lld budget=%d\n",
		name,
		player,
		beforeOriginOk ? 1 : 0,
		beforeAnglesOk ? 1 : 0,
		beforeOrigin[0], beforeOrigin[1], beforeOrigin[2],
		beforeAngles[0], beforeAngles[1], beforeAngles[2],
		afterOriginOk ? 1 : 0,
		afterAnglesOk ? 1 : 0,
		afterOrigin[0], afterOrigin[1], afterOrigin[2],
		afterAngles[0], afterAngles[1], afterAngles[2],
		ClientIsFiniteVector(afterOrigin) ? 1 : 0,
		static_cast<long long>(result),
		s_ClientCameraStageLogBudget);
	OutputDebugStringA(buffer);
	Warning("%s", buffer);
}

static __int64 __fastcall ClientCameraStage32ECC0(void* player, float* eyeOrigin, float* eyeAngles)
{
	float beforeOrigin[3];
	float beforeAngles[3];
	const bool beforeOriginOk = ClientCopyVectorPointer(eyeOrigin, beforeOrigin);
	const bool beforeAnglesOk = ClientCopyVectorPointer(eyeAngles, beforeAngles);
	const __int64 result = s_ClientCameraStage32ECC0Original ? s_ClientCameraStage32ECC0Original(player, eyeOrigin, eyeAngles) : 0;
	ClientLogViewStage("32ECC0", player, eyeOrigin, eyeAngles, beforeOrigin, beforeAngles, beforeOriginOk, beforeAnglesOk, result);
	return result;
}

static __int64 __fastcall ClientCameraStage089B60(void* player, float* eyeOrigin, float* eyeAngles, float frameTime)
{
	float beforeOrigin[3];
	float beforeAngles[3];
	const bool beforeOriginOk = ClientCopyVectorPointer(eyeOrigin, beforeOrigin);
	const bool beforeAnglesOk = ClientCopyVectorPointer(eyeAngles, beforeAngles);
	const __int64 result = s_ClientCameraStage089B60Original ? s_ClientCameraStage089B60Original(player, eyeOrigin, eyeAngles, frameTime) : 0;
	ClientLogViewStage("089B60", player, eyeOrigin, eyeAngles, beforeOrigin, beforeAngles, beforeOriginOk, beforeAnglesOk, result);
	return result;
}

static void ClientLogAngleOffset(const char* name, void* player, const float out[3], const float beforeAngles[3], bool beforeAnglesOk, __int64 result, float frameTime = NAN)
{
	if (!s_ClientInsideNormalCalcView || s_ClientAngleStageLogBudget <= 0 || ClientLocalSignonState() != 8)
		return;
	--s_ClientAngleStageLogBudget;
	const bool finiteOut = out && std::isfinite(out[0]) && std::isfinite(out[1]) && std::isfinite(out[2]);
	const float punch[6] = {
		ClientSafeReadFloatField(reinterpret_cast<__int64>(player), 2484 * 4),
		ClientSafeReadFloatField(reinterpret_cast<__int64>(player), 2485 * 4),
		ClientSafeReadFloatField(reinterpret_cast<__int64>(player), 2486 * 4),
		ClientSafeReadFloatField(reinterpret_cast<__int64>(player), 2490 * 4),
		ClientSafeReadFloatField(reinterpret_cast<__int64>(player), 2491 * 4),
		ClientSafeReadFloatField(reinterpret_cast<__int64>(player), 2492 * 4),
	};
	const float rollState[9] = {
		ClientSafeReadFloatField(reinterpret_cast<__int64>(player), 2912 * 4),
		ClientSafeReadFloatField(reinterpret_cast<__int64>(player), 2913 * 4),
		ClientSafeReadFloatField(reinterpret_cast<__int64>(player), 2914 * 4),
		ClientSafeReadFloatField(reinterpret_cast<__int64>(player), 3393 * 4),
		ClientSafeReadFloatField(reinterpret_cast<__int64>(player), 3394 * 4),
		ClientSafeReadFloatField(reinterpret_cast<__int64>(player), 3395 * 4),
		ClientSafeReadFloatField(reinterpret_cast<__int64>(player), 3433 * 4),
		ClientSafeReadFloatField(reinterpret_cast<__int64>(player), 3434 * 4),
		ClientSafeReadFloatField(reinterpret_cast<__int64>(player), 3435 * 4),
	};
	const float rollInputs[5] = {
		ClientSafeReadFloatField(reinterpret_cast<__int64>(player), 90 * 4),
		ClientSafeReadFloatField(reinterpret_cast<__int64>(player), 91 * 4),
		ClientSafeReadFloatField(reinterpret_cast<__int64>(player), 2821 * 4),
		ClientSafeReadFloatField(reinterpret_cast<__int64>(player), 2953 * 4),
		ClientSafeReadFloatField(reinterpret_cast<__int64>(player), 3420 * 4),
	};
	char buffer[1200];
	_snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
		"R1Delta: client angle-stage %s player=%p out=(%.3f,%.3f,%.3f) finiteOut=%d beforeAnglesOk=%d beforeAngles=(%.3f,%.3f,%.3f) punch2484=(%.3f,%.3f,%.3f) punch2490=(%.3f,%.3f,%.3f) base2912=(%.3f,%.3f,%.3f) rollN3393=(%.3f,%.3f,%.3f) rollOfs3433=(%.3f,%.3f,%.3f) inp90_91_2821_2953_3420=(%.3f,%.3f,%.3f,%.3f,%.3f) frameTime=%.6f result=%lld budget=%d\n",
		name,
		player,
		out ? out[0] : NAN, out ? out[1] : NAN, out ? out[2] : NAN,
		finiteOut ? 1 : 0,
		beforeAnglesOk ? 1 : 0,
		beforeAngles ? beforeAngles[0] : NAN, beforeAngles ? beforeAngles[1] : NAN, beforeAngles ? beforeAngles[2] : NAN,
		punch[0], punch[1], punch[2],
		punch[3], punch[4], punch[5],
		rollState[0], rollState[1], rollState[2],
		rollState[3], rollState[4], rollState[5],
		rollState[6], rollState[7], rollState[8],
		rollInputs[0], rollInputs[1], rollInputs[2], rollInputs[3], rollInputs[4],
		frameTime,
		static_cast<long long>(result),
		s_ClientAngleStageLogBudget);
	OutputDebugStringA(buffer);
	Warning("%s", buffer);
}

static float* __fastcall ClientAngleOffset03BA40(void* player, float* outAngles)
{
	float beforeAngles[3] = { NAN, NAN, NAN };
	const bool beforeAnglesOk = ClientCopyVectorPointer(outAngles, beforeAngles);
	float* const result = s_ClientAngleOffset03BA40Original ? s_ClientAngleOffset03BA40Original(player, outAngles) : outAngles;
	float out[3] = { NAN, NAN, NAN };
	ClientCopyVectorPointer(result ? result : outAngles, out);
	ClientLogAngleOffset("03BA40-viewPunch", player, out, beforeAngles, beforeAnglesOk, reinterpret_cast<__int64>(result));
	return result;
}

static float* __fastcall ClientAngleOffset0376F0(float* outAngles, void* player)
{
	float beforeAngles[3] = { NAN, NAN, NAN };
	const bool beforeAnglesOk = ClientCopyVectorPointer(outAngles, beforeAngles);
	float* const result = s_ClientAngleOffset0376F0Original ? s_ClientAngleOffset0376F0Original(outAngles, player) : outAngles;
	float out[3] = { NAN, NAN, NAN };
	ClientCopyVectorPointer(result ? result : outAngles, out);
	ClientLogAngleOffset("0376F0-predAngleOffset", player, out, beforeAngles, beforeAnglesOk, reinterpret_cast<__int64>(result));
	return result;
}

static __int64 __fastcall ClientAngleStage03C9F0(void* player, float* eyeAngles, float frameTime)
{
	float beforeAngles[3] = { NAN, NAN, NAN };
	const bool beforeAnglesOk = ClientCopyVectorPointer(eyeAngles, beforeAngles);
	const __int64 result = s_ClientAngleStage03C9F0Original ? s_ClientAngleStage03C9F0Original(player, eyeAngles, frameTime) : 0;
	float afterAngles[3] = { NAN, NAN, NAN };
	ClientCopyVectorPointer(eyeAngles, afterAngles);
	ClientLogAngleOffset("03C9F0-rollBob", player, afterAngles, beforeAngles, beforeAnglesOk, result, frameTime);
	return result;
}

static __int64 __fastcall ClientCameraStage037D10(void* player, float* eyeOrigin, float* eyeAngles)
{
	float beforeOrigin[3];
	float beforeAngles[3];
	const bool beforeOriginOk = ClientCopyVectorPointer(eyeOrigin, beforeOrigin);
	const bool beforeAnglesOk = ClientCopyVectorPointer(eyeAngles, beforeAngles);
	const __int64 result = s_ClientCameraStage037D10Original ? s_ClientCameraStage037D10Original(player, eyeOrigin, eyeAngles) : 0;
	ClientLogViewStage("037D10", player, eyeOrigin, eyeAngles, beforeOrigin, beforeAngles, beforeOriginOk, beforeAnglesOk, result);
	return result;
}

static void __fastcall ClientCameraStage090160(void* player, float* eyeOrigin, float* eyeAngles, float frameTime)
{
	float beforeOrigin[3];
	float beforeAngles[3];
	const bool beforeOriginOk = ClientCopyVectorPointer(eyeOrigin, beforeOrigin);
	const bool beforeAnglesOk = ClientCopyVectorPointer(eyeAngles, beforeAngles);
	if (s_ClientCameraStage090160Original)
		s_ClientCameraStage090160Original(player, eyeOrigin, eyeAngles, frameTime);
	ClientLogViewStage("090160", player, eyeOrigin, eyeAngles, beforeOrigin, beforeAngles, beforeOriginOk, beforeAnglesOk, 0);
}

static __int64 __fastcall ClientCameraStage035450(void* player, float* eyeOrigin, float* eyeAngles)
{
	float beforeOrigin[3];
	float beforeAngles[3];
	const bool beforeOriginOk = ClientCopyVectorPointer(eyeOrigin, beforeOrigin);
	const bool beforeAnglesOk = ClientCopyVectorPointer(eyeAngles, beforeAngles);
	const __int64 result = s_ClientCameraStage035450Original ? s_ClientCameraStage035450Original(player, eyeOrigin, eyeAngles) : 0;
	ClientLogViewStage("035450", player, eyeOrigin, eyeAngles, beforeOrigin, beforeAngles, beforeOriginOk, beforeAnglesOk, result);
	return result;
}

static void __fastcall ClientSpringOriginStage(float* spring, float* eyeOrigin)
{
	float beforeOrigin[3];
	float beforeAngles[3] = { NAN, NAN, NAN };
	const bool beforeOriginOk = ClientCopyVectorPointer(eyeOrigin, beforeOrigin);
	if (s_ClientSpringOriginStageOriginal)
		s_ClientSpringOriginStageOriginal(spring, eyeOrigin);
	if (s_ClientCurrentNormalCalcViewPlayer && spring == reinterpret_cast<float*>(reinterpret_cast<char*>(s_ClientCurrentNormalCalcViewPlayer) + 12996))
		ClientLogViewStage("3F6360-originSpring", s_ClientCurrentNormalCalcViewPlayer, eyeOrigin, nullptr, beforeOrigin, beforeAngles, beforeOriginOk, false, 0);
}

static __int64 __fastcall ClientSpringAnglesStage(float* spring, float* eyeAngles)
{
	float beforeAngles[3];
	float beforeOrigin[3] = { NAN, NAN, NAN };
	const bool beforeAnglesOk = ClientCopyVectorPointer(eyeAngles, beforeAngles);
	const __int64 result = s_ClientSpringAnglesStageOriginal ? s_ClientSpringAnglesStageOriginal(spring, eyeAngles) : 0;
	if (s_ClientCurrentNormalCalcViewPlayer && spring == reinterpret_cast<float*>(reinterpret_cast<char*>(s_ClientCurrentNormalCalcViewPlayer) + 1632))
		ClientLogViewStage("3F6480-angleSpring", s_ClientCurrentNormalCalcViewPlayer, nullptr, eyeAngles, beforeOrigin, beforeAngles, false, beforeAnglesOk, result);
	return result;
}

static __int64 __fastcall ClientViewModelCalcViewModelView(void* viewModel, void* player, float* eyeOrigin, float* eyeAngles)
{
	float beforeOrigin[3] = { NAN, NAN, NAN };
	float beforeAngles[3] = { NAN, NAN, NAN };
	const bool beforeOriginOk = ClientCopyVectorPointer(eyeOrigin, beforeOrigin);
	const bool beforeAnglesOk = ClientCopyVectorPointer(eyeAngles, beforeAngles);
	const __int64 result = s_ClientViewModelCalcViewModelViewOriginal
		? s_ClientViewModelCalcViewModelViewOriginal(viewModel, player, eyeOrigin, eyeAngles)
		: 0;

	if (s_ClientInsideNormalCalcView && s_ClientViewModelCalcViewModelViewLogBudget > 0 && ClientLocalSignonState() == 8) {
		--s_ClientViewModelCalcViewModelViewLogBudget;
		float afterOrigin[3] = { NAN, NAN, NAN };
		float afterAngles[3] = { NAN, NAN, NAN };
		const bool afterOriginOk = ClientCopyVectorPointer(eyeOrigin, afterOrigin);
		const bool afterAnglesOk = ClientCopyVectorPointer(eyeAngles, afterAngles);
		const bool finiteAfter = std::isfinite(afterOrigin[0]) && std::isfinite(afterOrigin[1]) && std::isfinite(afterOrigin[2])
			&& std::isfinite(afterAngles[0]) && std::isfinite(afterAngles[1]) && std::isfinite(afterAngles[2]);
		const int vmH2274 = ClientSafeReadIntField(reinterpret_cast<__int64>(viewModel), 2274 * 4, -999999);
		const int vmH2275 = ClientSafeReadIntField(reinterpret_cast<__int64>(viewModel), 2275 * 4, -999999);
		char buffer[768];
		_snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
			"R1Delta: client ViewModelCalcViewModelView vm=%p player=%p vmH2274=0x%08x vmH2275=0x%08x beforeOk=%d/%d before=(%.3f,%.3f,%.3f)/(%.3f,%.3f,%.3f) afterOk=%d/%d after=(%.3f,%.3f,%.3f)/(%.3f,%.3f,%.3f) finiteAfter=%d result=%lld budget=%d\n",
			viewModel,
			player,
			vmH2274,
			vmH2275,
			beforeOriginOk ? 1 : 0,
			beforeAnglesOk ? 1 : 0,
			beforeOrigin[0], beforeOrigin[1], beforeOrigin[2],
			beforeAngles[0], beforeAngles[1], beforeAngles[2],
			afterOriginOk ? 1 : 0,
			afterAnglesOk ? 1 : 0,
			afterOrigin[0], afterOrigin[1], afterOrigin[2],
			afterAngles[0], afterAngles[1], afterAngles[2],
			finiteAfter ? 1 : 0,
			static_cast<long long>(result),
			s_ClientViewModelCalcViewModelViewLogBudget);
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}

	return result;
}

static __int64 __fastcall ClientPostCalcView(void* player, float* eyeOrigin, float* eyeAngles)
{
	float beforeOrigin[3];
	float beforeAngles[3];
	const bool beforeOriginOk = ClientCopyVectorPointer(eyeOrigin, beforeOrigin);
	const bool beforeAnglesOk = ClientCopyVectorPointer(eyeAngles, beforeAngles);
	int whooshMode = -1;
	int whooshParity = -1;
	float cachedOrigin[3] = { NAN, NAN, NAN };
	float cachedAngles[3] = { NAN, NAN, NAN };
	__try {
		if (player && ClientIsReadableRange(player, 13548)) {
			whooshMode = *reinterpret_cast<int*>(reinterpret_cast<char*>(player) + 13248);
			whooshParity = *reinterpret_cast<int*>(reinterpret_cast<char*>(player) + 13252);
			cachedOrigin[0] = *reinterpret_cast<float*>(reinterpret_cast<char*>(player) + 13524);
			cachedOrigin[1] = *reinterpret_cast<float*>(reinterpret_cast<char*>(player) + 13528);
			cachedOrigin[2] = *reinterpret_cast<float*>(reinterpret_cast<char*>(player) + 13532);
			cachedAngles[0] = *reinterpret_cast<float*>(reinterpret_cast<char*>(player) + 13536);
			cachedAngles[1] = *reinterpret_cast<float*>(reinterpret_cast<char*>(player) + 13540);
			cachedAngles[2] = *reinterpret_cast<float*>(reinterpret_cast<char*>(player) + 13544);
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		whooshMode = -2;
	}

	const __int64 result = s_ClientPostCalcViewOriginal
		? s_ClientPostCalcViewOriginal(player, eyeOrigin, eyeAngles)
		: 0;

	if (s_ClientPostCalcViewLogBudget > 0 && ClientLocalSignonState() == 8) {
		--s_ClientPostCalcViewLogBudget;
		float afterOrigin[3];
		float afterAngles[3];
		const bool afterOriginOk = ClientCopyVectorPointer(eyeOrigin, afterOrigin);
		const bool afterAnglesOk = ClientCopyVectorPointer(eyeAngles, afterAngles);
		char buffer[1024];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: client PostCalcView player=%p whooshMode=%d whooshParity=%d beforeOk=%d/%d beforeOrigin=(%.3f,%.3f,%.3f) beforeAngles=(%.3f,%.3f,%.3f) cachedBefore=(%.3f,%.3f,%.3f)/(%.3f,%.3f,%.3f) afterOk=%d/%d afterOrigin=(%.3f,%.3f,%.3f) afterAngles=(%.3f,%.3f,%.3f) finiteAfter=%d result=%lld budget=%d\n",
			player,
			whooshMode,
			whooshParity,
			beforeOriginOk ? 1 : 0,
			beforeAnglesOk ? 1 : 0,
			beforeOrigin[0], beforeOrigin[1], beforeOrigin[2],
			beforeAngles[0], beforeAngles[1], beforeAngles[2],
			cachedOrigin[0], cachedOrigin[1], cachedOrigin[2],
			cachedAngles[0], cachedAngles[1], cachedAngles[2],
			afterOriginOk ? 1 : 0,
			afterAnglesOk ? 1 : 0,
			afterOrigin[0], afterOrigin[1], afterOrigin[2],
			afterAngles[0], afterAngles[1], afterAngles[2],
			ClientIsFiniteVector(afterOrigin) ? 1 : 0,
			static_cast<long long>(result),
			s_ClientPostCalcViewLogBudget);
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}

	return result;
}

static bool InstallClientPlayerClassHook(
	uintptr_t clientBase,
	uintptr_t rva,
	void* hook,
	void** original,
	bool& installed)
{
	if (installed)
		return true;

	void* const target = reinterpret_cast<void*>(clientBase + rva);
	const MH_STATUS createStatus = MH_CreateHook(target, hook, reinterpret_cast<LPVOID*>(original));
	const MH_STATUS enableStatus = (createStatus == MH_OK || createStatus == MH_ERROR_ALREADY_CREATED)
		? MH_EnableHook(target)
		: createStatus;
	installed = enableStatus == MH_OK || enableStatus == MH_ERROR_ENABLED;
	if (!installed) {
		char buffer[384];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: failed to install player-class compatibility hook rva=0x%llx create=%d enable=%d\n",
			static_cast<unsigned long long>(rva),
			static_cast<int>(createStatus),
			static_cast<int>(enableStatus));
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}
	return installed;
}

static __int64 ClientRecvTableProp(__int64 recvTable, unsigned int index)
{
	if (!recvTable)
		return 0;

	__try {
		const __int64 props = *reinterpret_cast<__int64*>(recvTable);
		const int count = *reinterpret_cast<int*>(recvTable + 8);
		if (!props || index >= static_cast<unsigned int>(count))
			return 0;
		return props + 96LL * index;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return 0;
	}
}

static __int64 ClientRecvPropDataTable(__int64 recvProp)
{
	if (!recvProp)
		return 0;

	__try {
		return *reinterpret_cast<__int64*>(recvProp + 64);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return 0;
	}
}

static void InstallClientPlayerClassCompatibilityHooks(uintptr_t clientBase)
{
	if (!clientBase || IsDedicatedServer())
		return;

	InstallClientPlayerClassHook(
		clientBase,
		0x3FBB0,
		reinterpret_cast<void*>(&ClientApplyPlayerClassMods03FBB0),
		reinterpret_cast<void**>(&s_ClientApplyPlayerClassMods03FBB0Original),
		s_ClientApplyPlayerClassModsHookInstalled);
	InstallClientPlayerClassHook(
		clientBase,
		0x109E80,
		reinterpret_cast<void*>(&ClientLoadPlayerClasses109E80),
		reinterpret_cast<void**>(&s_ClientLoadPlayerClasses109E80Original),
		s_ClientPlayerClassLoadHookInstalled);
	InstallClientPlayerClassHook(
		clientBase,
		0x45BD10,
		reinterpret_cast<void*>(&ClientLoadDataList45BD10),
		reinterpret_cast<void**>(&s_ClientLoadDataList45BD10Original),
		s_ClientLoadDataListHookInstalled);
	InstallClientPlayerClassHook(
		clientBase,
		0x65F980,
		reinterpret_cast<void*>(&ClientKeyValuesLoadFromFile65F980),
		reinterpret_cast<void**>(&s_ClientKeyValuesLoadFromFile65F980Original),
		s_ClientKeyValuesLoadFromFileHookInstalled);
}

static void InstallClientRollLifecycleHooks(uintptr_t clientBase)
{
	if (!ShouldInstallR1OClientDebugHooks() || s_ClientRollLifecycleHooksInstalled || !clientBase || IsDedicatedServer())
		return;

	struct RollInstall {
		uintptr_t rva;
		void* hook;
		void** original;
		const char* name;
	};
	RollInstall hooks[] = {
		{ 0x8E6E0, reinterpret_cast<void*>(&ClientRollReset08E6E0), reinterpret_cast<void**>(&s_ClientRollReset08E6E0Original), "08E6E0" },
		{ 0x34CEF0, reinterpret_cast<void*>(&ClientRollLifecycle34CEF0), reinterpret_cast<void**>(&s_ClientRollLifecycle34CEF0Original), "34CEF0" },
	};

	bool allInstalled = true;
	for (const RollInstall& hook : hooks) {
		void* const target = reinterpret_cast<void*>(clientBase + hook.rva);
		const MH_STATUS createStatus = MH_CreateHook(target, hook.hook, reinterpret_cast<LPVOID*>(hook.original));
		const MH_STATUS enableStatus = (createStatus == MH_OK || createStatus == MH_ERROR_ALREADY_CREATED) ? MH_EnableHook(target) : createStatus;
		const bool installed = enableStatus == MH_OK || enableStatus == MH_ERROR_ENABLED;
		allInstalled = allInstalled && installed;
		char buffer[384];
		_snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
			"R1Delta: client roll-lifecycle hook %s create=%d enable=%d target=%p original=%p installed=%d\n",
			hook.name,
			static_cast<int>(createStatus),
			static_cast<int>(enableStatus),
			target,
			*hook.original,
			installed ? 1 : 0);
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}

	if (!s_ClientSetClassVarHookInstalled) {
		void* const target = reinterpret_cast<void*>(clientBase + 0x41830);
		const MH_STATUS createStatus = MH_CreateHook(target, &ClientSetClassVar041830, reinterpret_cast<LPVOID*>(&s_ClientSetClassVar041830Original));
		const MH_STATUS enableStatus = (createStatus == MH_OK || createStatus == MH_ERROR_ALREADY_CREATED) ? MH_EnableHook(target) : createStatus;
		s_ClientSetClassVarHookInstalled = enableStatus == MH_OK || enableStatus == MH_ERROR_ENABLED;
		char buffer[384];
		_snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
			"R1Delta: client set-class-var hook 041830 create=%d enable=%d target=%p original=%p installed=%d\n",
			static_cast<int>(createStatus),
			static_cast<int>(enableStatus),
			target,
			reinterpret_cast<void*>(s_ClientSetClassVar041830Original),
			s_ClientSetClassVarHookInstalled ? 1 : 0);
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}

	s_ClientRollLifecycleHooksInstalled = allInstalled;
}

static void ClientMaybeInstallViewSubHooks()
{
	if (!ShouldInstallR1OClientDebugHooks() || !G_client || IsDedicatedServer())
		return;

	if (!s_ClientNormalCalcViewHookInstalled) {
		void* const target = reinterpret_cast<void*>(G_client + 0x32FF20);
		const MH_STATUS createStatus = MH_CreateHook(target, &ClientNormalCalcView, reinterpret_cast<LPVOID*>(&s_ClientNormalCalcViewOriginal));
		const MH_STATUS enableStatus = (createStatus == MH_OK || createStatus == MH_ERROR_ALREADY_CREATED) ? MH_EnableHook(target) : createStatus;
		s_ClientNormalCalcViewHookInstalled = enableStatus == MH_OK || enableStatus == MH_ERROR_ENABLED;
		char buffer[320];
		_snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "R1Delta: client NormalCalcView hook create=%d enable=%d target=%p original=%p installed=%d\n", static_cast<int>(createStatus), static_cast<int>(enableStatus), target, reinterpret_cast<void*>(s_ClientNormalCalcViewOriginal), s_ClientNormalCalcViewHookInstalled ? 1 : 0);
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}

	if (!s_ClientPostCalcViewHookInstalled) {
		void* const target = reinterpret_cast<void*>(G_client + 0x8EE70);
		const MH_STATUS createStatus = MH_CreateHook(target, &ClientPostCalcView, reinterpret_cast<LPVOID*>(&s_ClientPostCalcViewOriginal));
		const MH_STATUS enableStatus = (createStatus == MH_OK || createStatus == MH_ERROR_ALREADY_CREATED) ? MH_EnableHook(target) : createStatus;
		s_ClientPostCalcViewHookInstalled = enableStatus == MH_OK || enableStatus == MH_ERROR_ENABLED;
		char buffer[320];
		_snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "R1Delta: client PostCalcView hook create=%d enable=%d target=%p original=%p installed=%d\n", static_cast<int>(createStatus), static_cast<int>(enableStatus), target, reinterpret_cast<void*>(s_ClientPostCalcViewOriginal), s_ClientPostCalcViewHookInstalled ? 1 : 0);
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}

	if (!s_ClientCameraStage03F170HookInstalled) {
		void* const target = reinterpret_cast<void*>(G_client + 0x3F170);
		const MH_STATUS createStatus = MH_CreateHook(target, &ClientCameraStage03F170, reinterpret_cast<LPVOID*>(&s_ClientCameraStage03F170Original));
		const MH_STATUS enableStatus = (createStatus == MH_OK || createStatus == MH_ERROR_ALREADY_CREATED) ? MH_EnableHook(target) : createStatus;
		s_ClientCameraStage03F170HookInstalled = enableStatus == MH_OK || enableStatus == MH_ERROR_ENABLED;
		char buffer[320];
		_snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "R1Delta: client view-stage 03F170 hook create=%d enable=%d target=%p original=%p installed=%d\n", static_cast<int>(createStatus), static_cast<int>(enableStatus), target, reinterpret_cast<void*>(s_ClientCameraStage03F170Original), s_ClientCameraStage03F170HookInstalled ? 1 : 0);
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}

	if (!s_ClientCameraStageHooksInstalled) {
		struct StageInstall {
			uintptr_t rva;
			void* hook;
			void** original;
			const char* name;
		};
		StageInstall stages[] = {
			{ 0x32ECC0, reinterpret_cast<void*>(&ClientCameraStage32ECC0), reinterpret_cast<void**>(&s_ClientCameraStage32ECC0Original), "32ECC0" },
			{ 0x89B60, reinterpret_cast<void*>(&ClientCameraStage089B60), reinterpret_cast<void**>(&s_ClientCameraStage089B60Original), "089B60" },
			{ 0x37D10, reinterpret_cast<void*>(&ClientCameraStage037D10), reinterpret_cast<void**>(&s_ClientCameraStage037D10Original), "037D10" },
			{ 0x90160, reinterpret_cast<void*>(&ClientCameraStage090160), reinterpret_cast<void**>(&s_ClientCameraStage090160Original), "090160" },
			{ 0x35450, reinterpret_cast<void*>(&ClientCameraStage035450), reinterpret_cast<void**>(&s_ClientCameraStage035450Original), "035450" },
			{ 0x3F6360, reinterpret_cast<void*>(&ClientSpringOriginStage), reinterpret_cast<void**>(&s_ClientSpringOriginStageOriginal), "3F6360" },
			{ 0x3F6480, reinterpret_cast<void*>(&ClientSpringAnglesStage), reinterpret_cast<void**>(&s_ClientSpringAnglesStageOriginal), "3F6480" },
		};
		bool allInstalled = true;
		for (const StageInstall& stage : stages) {
			void* const target = reinterpret_cast<void*>(G_client + stage.rva);
			const MH_STATUS createStatus = MH_CreateHook(target, stage.hook, reinterpret_cast<LPVOID*>(stage.original));
			const MH_STATUS enableStatus = (createStatus == MH_OK || createStatus == MH_ERROR_ALREADY_CREATED) ? MH_EnableHook(target) : createStatus;
			const bool installed = enableStatus == MH_OK || enableStatus == MH_ERROR_ENABLED;
			allInstalled = allInstalled && installed;
			char buffer[320];
			_snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "R1Delta: client view-stage %s hook create=%d enable=%d target=%p original=%p installed=%d\n", stage.name, static_cast<int>(createStatus), static_cast<int>(enableStatus), target, *stage.original, installed ? 1 : 0);
			OutputDebugStringA(buffer);
			Warning("%s", buffer);
		}
		s_ClientCameraStageHooksInstalled = allInstalled;
	}

	if (!s_ClientViewModelCalcViewModelViewHookInstalled) {
		void* const target = reinterpret_cast<void*>(G_client + 0x44680);
		const MH_STATUS createStatus = MH_CreateHook(target, &ClientViewModelCalcViewModelView, reinterpret_cast<LPVOID*>(&s_ClientViewModelCalcViewModelViewOriginal));
		const MH_STATUS enableStatus = (createStatus == MH_OK || createStatus == MH_ERROR_ALREADY_CREATED) ? MH_EnableHook(target) : createStatus;
		s_ClientViewModelCalcViewModelViewHookInstalled = enableStatus == MH_OK || enableStatus == MH_ERROR_ENABLED;
		char buffer[320];
		_snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "R1Delta: client ViewModelCalcViewModelView hook create=%d enable=%d target=%p original=%p installed=%d\n", static_cast<int>(createStatus), static_cast<int>(enableStatus), target, reinterpret_cast<void*>(s_ClientViewModelCalcViewModelViewOriginal), s_ClientViewModelCalcViewModelViewHookInstalled ? 1 : 0);
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}

	if (!s_ClientAngleStageHooksInstalled) {
		struct AngleInstall {
			uintptr_t rva;
			void* hook;
			void** original;
			const char* name;
		};
		AngleInstall angleStages[] = {
			{ 0x3BA40, reinterpret_cast<void*>(&ClientAngleOffset03BA40), reinterpret_cast<void**>(&s_ClientAngleOffset03BA40Original), "03BA40" },
			{ 0x376F0, reinterpret_cast<void*>(&ClientAngleOffset0376F0), reinterpret_cast<void**>(&s_ClientAngleOffset0376F0Original), "0376F0" },
			{ 0x3C9F0, reinterpret_cast<void*>(&ClientAngleStage03C9F0), reinterpret_cast<void**>(&s_ClientAngleStage03C9F0Original), "03C9F0" },
		};
		bool allInstalled = true;
		for (const AngleInstall& stage : angleStages) {
			void* const target = reinterpret_cast<void*>(G_client + stage.rva);
			const MH_STATUS createStatus = MH_CreateHook(target, stage.hook, reinterpret_cast<LPVOID*>(stage.original));
			const MH_STATUS enableStatus = (createStatus == MH_OK || createStatus == MH_ERROR_ALREADY_CREATED) ? MH_EnableHook(target) : createStatus;
			const bool installed = enableStatus == MH_OK || enableStatus == MH_ERROR_ENABLED;
			allInstalled = allInstalled && installed;
			char buffer[320];
			_snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "R1Delta: client angle-stage %s hook create=%d enable=%d target=%p original=%p installed=%d\n", stage.name, static_cast<int>(createStatus), static_cast<int>(enableStatus), target, *stage.original, installed ? 1 : 0);
			OutputDebugStringA(buffer);
			Warning("%s", buffer);
		}
		s_ClientAngleStageHooksInstalled = allInstalled;
	}
}

static void __fastcall ClientPlayerCalcView(void* player, float* eyeOrigin, float* eyeAngles, float* fov)
{
	float beforeOrigin[3];
	float beforeAngles[3];
	const bool beforeOriginOk = ClientCopyVectorPointer(eyeOrigin, beforeOrigin);
	const bool beforeAnglesOk = ClientCopyVectorPointer(eyeAngles, beforeAngles);
	float beforeFov = NAN;
	__try {
		if (fov && ClientIsReadableRange(fov, sizeof(float)))
			beforeFov = *fov;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		beforeFov = NAN;
	}

	if (s_ClientPlayerCalcViewOriginal)
		s_ClientPlayerCalcViewOriginal(player, eyeOrigin, eyeAngles, fov);

	if (s_ClientPlayerCalcViewLogBudget <= 0 || ClientLocalSignonState() != 8)
		return;

	float afterOrigin[3];
	float afterAngles[3];
	const bool afterOriginOk = ClientCopyVectorPointer(eyeOrigin, afterOrigin);
	const bool afterAnglesOk = ClientCopyVectorPointer(eyeAngles, afterAngles);
	float afterFov = NAN;
	__try {
		if (fov && ClientIsReadableRange(fov, sizeof(float)))
			afterFov = *fov;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		afterFov = NAN;
	}

	float absOrigin[3];
	float absAngles[3];
	float eyePosVFunc[3];
	float eyeAngVFunc[3];
	void* eyePosTarget = nullptr;
	void* eyeAngTarget = nullptr;
	const bool absOriginOk = ClientReadVectorVFunc(player, 72, absOrigin);
	const bool absAnglesOk = ClientReadVectorVFunc(player, 80, absAngles);
	const bool eyePosOk = ClientCallVectorOutVFunc(player, 1464, eyePosVFunc, &eyePosTarget);
	const bool eyeAngOk = ClientCallVectorOutVFunc(player, 1472, eyeAngVFunc, &eyeAngTarget);
	int alive = -1;
	int vehicleHandle11928 = 0;
	int viewOffsetHandle11944 = 0;
	int splitViewPlayer14856 = 0;
	int splitViewActive14864 = -1;
	int calcViewSpectatorBranch = -1;
	int observerModeBranch = -1;
	float cachedViewOffsetOrigin[3] = { NAN, NAN, NAN };
	float cachedEyeAngles[3] = { NAN, NAN, NAN };
	__try {
		if (player && ClientIsReadableRange(player, 17948)) {
			alive = *reinterpret_cast<unsigned char*>(reinterpret_cast<char*>(player) + 1160) == 0 ? 1 : 0;
			vehicleHandle11928 = *reinterpret_cast<int*>(reinterpret_cast<char*>(player) + 11928);
			viewOffsetHandle11944 = *reinterpret_cast<int*>(reinterpret_cast<char*>(player) + 11944);
			splitViewPlayer14856 = *reinterpret_cast<int*>(reinterpret_cast<char*>(player) + 14856);
			splitViewActive14864 = *reinterpret_cast<unsigned char*>(reinterpret_cast<char*>(player) + 14864);
			cachedViewOffsetOrigin[0] = *reinterpret_cast<float*>(reinterpret_cast<char*>(player) + 14248);
			cachedViewOffsetOrigin[1] = *reinterpret_cast<float*>(reinterpret_cast<char*>(player) + 14252);
			cachedViewOffsetOrigin[2] = *reinterpret_cast<float*>(reinterpret_cast<char*>(player) + 14256);
			cachedEyeAngles[0] = *reinterpret_cast<float*>(reinterpret_cast<char*>(player) + 17936);
			cachedEyeAngles[1] = *reinterpret_cast<float*>(reinterpret_cast<char*>(player) + 17940);
			cachedEyeAngles[2] = *reinterpret_cast<float*>(reinterpret_cast<char*>(player) + 17944);
		}
		if (G_client && player) {
			calcViewSpectatorBranch = reinterpret_cast<bool(__fastcall*)(void*)>(G_client + 0x84EB0)(player) ? 1 : 0;
			observerModeBranch = static_cast<int>(reinterpret_cast<__int64(__fastcall*)(void*)>(G_client + 0x84F70)(player));
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		alive = -2;
	}
	--s_ClientPlayerCalcViewLogBudget;
	char buffer[1536];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: client CalcView player=%p target=%p beforeOk=%d/%d beforeOrigin=(%.3f,%.3f,%.3f) beforeAngles=(%.3f,%.3f,%.3f) beforeFov=%.3f afterOk=%d/%d afterOrigin=(%.3f,%.3f,%.3f) afterAngles=(%.3f,%.3f,%.3f) afterFov=%.3f absOk=%d/%d absOrigin=(%.3f,%.3f,%.3f) absAngles=(%.3f,%.3f,%.3f) eyeVFuncOk=%d/%d eyePosTarget=%p eyeAngTarget=%p eyePos=(%.3f,%.3f,%.3f) eyeAng=(%.3f,%.3f,%.3f) alive=%d spectatorBranch=%d observerModeBranch=%d vehicleH11928=0x%x viewOffH11944=0x%x splitViewPlayer14856=%d splitViewActive14864=%d cachedViewOffOrigin=(%.3f,%.3f,%.3f) cachedEyeAngles=(%.3f,%.3f,%.3f) finiteAfter=%d budget=%d\n",
		player,
		reinterpret_cast<void*>(s_ClientPlayerCalcViewTarget),
		beforeOriginOk ? 1 : 0,
		beforeAnglesOk ? 1 : 0,
		beforeOrigin[0],
		beforeOrigin[1],
		beforeOrigin[2],
		beforeAngles[0],
		beforeAngles[1],
		beforeAngles[2],
		beforeFov,
		afterOriginOk ? 1 : 0,
		afterAnglesOk ? 1 : 0,
		afterOrigin[0],
		afterOrigin[1],
		afterOrigin[2],
		afterAngles[0],
		afterAngles[1],
		afterAngles[2],
		afterFov,
		absOriginOk ? 1 : 0,
		absAnglesOk ? 1 : 0,
		absOrigin[0],
		absOrigin[1],
		absOrigin[2],
		absAngles[0],
		absAngles[1],
		absAngles[2],
		eyePosOk ? 1 : 0,
		eyeAngOk ? 1 : 0,
		eyePosTarget,
		eyeAngTarget,
		eyePosVFunc[0],
		eyePosVFunc[1],
		eyePosVFunc[2],
		eyeAngVFunc[0],
		eyeAngVFunc[1],
		eyeAngVFunc[2],
		alive,
		calcViewSpectatorBranch,
		observerModeBranch,
		vehicleHandle11928,
		viewOffsetHandle11944,
		splitViewPlayer14856,
		splitViewActive14864,
		cachedViewOffsetOrigin[0],
		cachedViewOffsetOrigin[1],
		cachedViewOffsetOrigin[2],
		cachedEyeAngles[0],
		cachedEyeAngles[1],
		cachedEyeAngles[2],
		ClientIsFiniteVector(afterOrigin) ? 1 : 0,
		s_ClientPlayerCalcViewLogBudget);
	OutputDebugStringA(buffer);
	Warning("%s", buffer);
}

static void ClientMaybeInstallPlayerCalcViewHook(void* ent)
{
	if (!ShouldInstallR1OClientDebugHooks())
		return;
	ClientMaybeInstallViewSubHooks();
	if (s_ClientPlayerCalcViewHookInstalled || !ent || !G_client || IsDedicatedServer())
		return;

	void* target = nullptr;
	__try {
		if (!ClientIsReadableRange(ent, sizeof(void*)))
			return;
		void* const vtable = *reinterpret_cast<void**>(ent);
		if (!ClientIsReadableRange(reinterpret_cast<const char*>(vtable) + 2272, sizeof(void*)))
			return;
		target = *reinterpret_cast<void**>(reinterpret_cast<char*>(vtable) + 2272);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return;
	}

	if (!target)
		return;
	const uintptr_t targetAddress = reinterpret_cast<uintptr_t>(target);
	if (targetAddress < G_client || targetAddress >= G_client + 0x4000000)
		return;

	const MH_STATUS createStatus = MH_CreateHook(target, &ClientPlayerCalcView, reinterpret_cast<LPVOID*>(&s_ClientPlayerCalcViewOriginal));
	if (createStatus != MH_OK && createStatus != MH_ERROR_ALREADY_CREATED) {
		char buffer[320];
		_snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "R1Delta: client CalcView hook create failed status=%d target=%p\n", static_cast<int>(createStatus), target);
		OutputDebugStringA(buffer);
		return;
	}
	const MH_STATUS enableStatus = MH_EnableHook(target);
	s_ClientPlayerCalcViewTarget = targetAddress;
	s_ClientPlayerCalcViewHookInstalled = enableStatus == MH_OK || enableStatus == MH_ERROR_ENABLED;
	char buffer[384];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: client CalcView hook create=%d enable=%d target=%p original=%p installed=%d\n",
		static_cast<int>(createStatus),
		static_cast<int>(enableStatus),
		target,
		reinterpret_cast<void*>(s_ClientPlayerCalcViewOriginal),
		s_ClientPlayerCalcViewHookInstalled ? 1 : 0);
	OutputDebugStringA(buffer);
	Warning("%s", buffer);
}

static void ClientLogHL2PlayerFlatPropsOnce(__int64 decoder)
{
	if (!decoder)
		return;

	ClientMaybeInstallHL2PlayerRecvProxyTrace(decoder);

	if (s_ClientPlayerFlatPropLogBudget <= 0)
		return;

	const int flatCount = ClientRecvDecoderFlattenedPropCount(decoder);
	for (int i = 0; i < flatCount && i < 4096 && s_ClientPlayerFlatPropLogBudget > 0; ++i) {
		--s_ClientPlayerFlatPropLogBudget;
		const __int64 recvProp = ClientRecvDecoderRecvProp(decoder, static_cast<unsigned int>(i));
		const __int64 sendProp = ClientRecvDecoderSendProp(decoder, static_cast<unsigned int>(i));
		char buffer[1024];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: client DT_HL2_Player flat prop[%d/%d] recv=%p rName=\"%s\" rType=%d rFlags=0x%x rOffset=%d rStride=%d rElems=%d send=%p sName=\"%s\" sType=%d sBits=%d sFlags=0x%x sOffset=%d budget=%d\n",
			i,
			flatCount,
			reinterpret_cast<void*>(recvProp),
			ClientRecvPropName(recvProp),
			ClientRecvPropType(recvProp),
			ClientRecvPropFlags(recvProp),
			ClientRecvPropOffset(recvProp),
			ClientRecvPropElementStride(recvProp),
			ClientRecvPropNumElements(recvProp),
			reinterpret_cast<void*>(sendProp),
			ClientSendPropName(sendProp),
			ClientSendPropType(sendProp),
			ClientSendPropNumBits(sendProp),
			ClientSendPropFlags(sendProp),
			ClientSendPropOffset(sendProp),
			s_ClientPlayerFlatPropLogBudget);
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}
}

static void ClientLogLocalPlayerState(const char* reason)
{
	if (!G_client || s_ClientLocalPlayerStateLogBudget <= 0)
		return;

	const __int64 recvTable = ClientFindRecvTableSafe("DT_HL2_Player");
	const __int64 decoder = ClientDecoderForRecvTable(recvTable);
	ClientLogHL2PlayerFlatPropsOnce(decoder);

	using GetClientEntityByIndexType = void* (*)(int);
	using GetClientEntitySelfType = void* (*)(int);
	void* entSelf = nullptr;
	void* ent1 = nullptr;
	void* ent0 = nullptr;
	__try {
		entSelf = reinterpret_cast<GetClientEntitySelfType>(G_client + 0x7B1B0)(-1);
		ent0 = reinterpret_cast<GetClientEntityByIndexType>(G_client + 0x280FE0)(0);
		ent1 = reinterpret_cast<GetClientEntityByIndexType>(G_client + 0x280FE0)(1);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
	}

	void* ent = entSelf ? entSelf : (ent1 ? ent1 : ent0);
	ClientMaybeInstallPlayerCalcViewHook(ent);
	const __int64 object = reinterpret_cast<__int64>(ent);

	const __int64 propTeam = ClientFindFlatRecvProp(decoder, "m_iTeamNum");
	const __int64 propHealth = ClientFindFlatRecvProp(decoder, "m_iHealth");
	const __int64 propLife = ClientFindFlatRecvProp(decoder, "m_lifeState");
	const __int64 propFlags = ClientFindFlatRecvProp(decoder, "m_fFlags");
	const __int64 propCellBits = ClientFindFlatRecvProp(decoder, "m_cellbits");
	const __int64 propCellX = ClientFindFlatRecvProp(decoder, "m_cellX");
	const __int64 propCellY = ClientFindFlatRecvProp(decoder, "m_cellY");
	const __int64 propCellZ = ClientFindFlatRecvProp(decoder, "m_cellZ");
	const __int64 propOrigin = ClientFindFlatRecvProp(decoder, "m_localOrigin", 0);
	const __int64 propOriginZ = ClientFindFlatRecvProp(decoder, "m_localOrigin[2]", 0);
	const __int64 propViewX = ClientFindFlatRecvProp(decoder, "m_vecViewOffset[0]", 0);
	const __int64 propViewY = ClientFindFlatRecvProp(decoder, "m_vecViewOffset[1]", 0);
	const __int64 propViewZ = ClientFindFlatRecvProp(decoder, "m_vecViewOffset[2]", 0);
	const __int64 propEyePitch = ClientFindFlatRecvProp(decoder, "m_angEyeAngles[0]", 0);
	const __int64 propEyeYaw = ClientFindFlatRecvProp(decoder, "m_angEyeAngles[1]", 0);
	const __int64 propPlayerCond = ClientFindFlatRecvProp(decoder, "m_nPlayerCond", 0);
	const __int64 propEyeOffset = ClientFindFlatRecvProp(decoder, "m_vEyeOffset", 0);
	const __int64 propLocalUp = ClientFindFlatRecvProp(decoder, "m_vLocalUp", 0);
	const __int64 propUp = ClientFindFlatRecvProp(decoder, "m_Up", 0);
	const __int64 propStickCameraState = ClientFindFlatRecvProp(decoder, "m_nStickCameraState", 0);
	const __int64 propInAirState = ClientFindFlatRecvProp(decoder, "m_InAirState", 0);
	const __int64 propCurrentStickTransitionDelay = ClientFindFlatRecvProp(decoder, "m_flCurrentStickTransitionDelay", 0);
	const __int64 propAirInputScale = ClientFindFlatRecvProp(decoder, "m_flAirInputScale", 0);
	const __int64 propDoneStickInterp = ClientFindFlatRecvProp(decoder, "m_bDoneStickInterp", 0);
	const __int64 propDoneCorrectPitch = ClientFindFlatRecvProp(decoder, "m_bDoneCorrectPitch", 0);

	const int offTeam = ClientRecvPropOffset(propTeam);
	const int offHealth = ClientRecvPropOffset(propHealth);
	const int offLife = ClientRecvPropOffset(propLife);
	const int offFlags = ClientRecvPropOffset(propFlags);
	const int offCellBits = ClientRecvPropOffset(propCellBits);
	const int offCellX = ClientRecvPropOffset(propCellX);
	const int offCellY = ClientRecvPropOffset(propCellY);
	const int offCellZ = ClientRecvPropOffset(propCellZ);
	const int offOrigin = ClientRecvPropOffset(propOrigin);
	const int offOriginZ = ClientRecvPropOffset(propOriginZ);
	const int offViewX = ClientRecvPropOffset(propViewX);
	const int offViewY = ClientRecvPropOffset(propViewY);
	const int offViewZ = ClientRecvPropOffset(propViewZ);
	const int offEyePitch = ClientRecvPropOffset(propEyePitch);
	const int offEyeYaw = ClientRecvPropOffset(propEyeYaw);
	const int offPlayerCond = ClientRecvPropOffset(propPlayerCond);
	const int offEyeOffset = ClientRecvPropOffset(propEyeOffset);
	const int offLocalUp = ClientRecvPropOffset(propLocalUp);
	const int offUp = ClientRecvPropOffset(propUp);
	const int offStickCameraState = ClientRecvPropOffset(propStickCameraState);
	const int offInAirState = ClientRecvPropOffset(propInAirState);
	const int offCurrentStickTransitionDelay = ClientRecvPropOffset(propCurrentStickTransitionDelay);
	const int offAirInputScale = ClientRecvPropOffset(propAirInputScale);
	const int offDoneStickInterp = ClientRecvPropOffset(propDoneStickInterp);
	const int offDoneCorrectPitch = ClientRecvPropOffset(propDoneCorrectPitch);

	int vfuncTeam = -999999;
	__try {
		if (ent && ClientIsReadableRange(ent, sizeof(void*))) {
			void* const vtable = *reinterpret_cast<void**>(ent);
			if (ClientIsReadableRange(reinterpret_cast<const char*>(vtable) + 768, sizeof(void*))) {
				using GetTeamType = int(__fastcall*)(void*);
				vfuncTeam = (*reinterpret_cast<GetTeamType*>(reinterpret_cast<char*>(vtable) + 768))(ent);
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		vfuncTeam = -888888;
	}

	const float netOriginX = ClientSafeReadFloatField(object, offOrigin);
	const float netOriginY = ClientSafeReadFloatField(object, offOrigin + 4);
	const float netOriginZ = offOriginZ >= 0 ? ClientSafeReadFloatField(object, offOriginZ) : ClientSafeReadFloatField(object, offOrigin + 8);
	const float viewX = ClientSafeReadFloatField(object, offViewX);
	const float viewY = ClientSafeReadFloatField(object, offViewY);
	const float viewZ = ClientSafeReadFloatField(object, offViewZ);
	float absOrigin[3];
	float absAngles[3];
	const bool absOriginOk = ClientReadVectorVFunc(ent, 72, absOrigin);
	const bool absAnglesOk = ClientReadVectorVFunc(ent, 80, absAngles);
	float mainViewOrigin[3];
	float mainViewAngles[3];
	const bool mainViewOk = ClientReadMainViewSlot(0, mainViewOrigin, mainViewAngles);
	const float eyeX = absOriginOk ? absOrigin[0] + viewX : netOriginX + viewX;
	const float eyeY = absOriginOk ? absOrigin[1] + viewY : netOriginY + viewY;
	const float eyeZ = absOriginOk ? absOrigin[2] + viewZ : netOriginZ + viewZ;

	--s_ClientLocalPlayerStateLogBudget;
	char buffer[2048];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: client local-player state reason=%s localState=%d entSelf=%p ent0=%p ent1=%p chosen=%p recvTable=%p decoder=%p flat=%d teamV=%d health[%d]=%d life[%d]=%d flags[%d]=0x%x cellbits[%d]=%d cell=(%d:%d,%d:%d,%d:%d) netOrigin[%d,%d]=(%.3f,%.3f,%.3f) viewOffset=(%d:%.3f,%d:%.3f,%d:%.3f) absOriginOk=%d absOrigin=(%.3f,%.3f,%.3f) absAnglesOk=%d absAngles=(%.3f,%.3f,%.3f) calcEye=(%.3f,%.3f,%.3f) mainViewOk=%d mainView=(%.3f,%.3f,%.3f) mainAngles=(%.3f,%.3f,%.3f) mainMinusEye=(%.3f,%.3f,%.3f) eye=(%d:%.3f,%d:%.3f) cond[%d]=0x%x budget=%d\n",
		reason ? reason : "<null>",
		ClientLocalSignonState(),
		entSelf,
		ent0,
		ent1,
		ent,
		reinterpret_cast<void*>(recvTable),
		reinterpret_cast<void*>(decoder),
		ClientRecvDecoderFlattenedPropCount(decoder),
		vfuncTeam,
		offHealth,
		ClientSafeReadIntField(object, offHealth),
		offLife,
		static_cast<int>(ClientSafeReadByteField(object, offLife)),
		offFlags,
		ClientSafeReadIntField(object, offFlags),
		offCellBits,
		ClientSafeReadIntField(object, offCellBits),
		offCellX,
		ClientSafeReadIntField(object, offCellX),
		offCellY,
		ClientSafeReadIntField(object, offCellY),
		offCellZ,
		ClientSafeReadIntField(object, offCellZ),
		offOrigin,
		offOriginZ,
		netOriginX,
		netOriginY,
		netOriginZ,
		offViewX,
		viewX,
		offViewY,
		viewY,
		offViewZ,
		viewZ,
		absOriginOk ? 1 : 0,
		absOrigin[0],
		absOrigin[1],
		absOrigin[2],
		absAnglesOk ? 1 : 0,
		absAngles[0],
		absAngles[1],
		absAngles[2],
		eyeX,
		eyeY,
		eyeZ,
		mainViewOk ? 1 : 0,
		mainViewOrigin[0],
		mainViewOrigin[1],
		mainViewOrigin[2],
		mainViewAngles[0],
		mainViewAngles[1],
		mainViewAngles[2],
		mainViewOrigin[0] - eyeX,
		mainViewOrigin[1] - eyeY,
		mainViewOrigin[2] - eyeZ,
		offEyePitch,
		ClientSafeReadFloatField(object, offEyePitch),
		offEyeYaw,
		ClientSafeReadFloatField(object, offEyeYaw),
		offPlayerCond,
		ClientSafeReadIntField(object, offPlayerCond),
		s_ClientLocalPlayerStateLogBudget);
	OutputDebugStringA(buffer);
	Warning("%s", buffer);

	const __int64 propObserverMode = ClientFindFlatRecvProp(decoder, "m_iObserverMode", 0);
	const __int64 propObserverTarget = ClientFindFlatRecvProp(decoder, "m_hObserverTarget", 0);
	const __int64 propViewEntity = ClientFindFlatRecvProp(decoder, "m_hViewEntity", 0);
	const __int64 propViewOffsetEntity = ClientFindFlatRecvProp(decoder, "m_hViewOffsetEntity", 0);
	const __int64 propThirdPersonEnt = ClientFindFlatRecvProp(decoder, "m_hThirdPersonEnt", 0);
	const __int64 propPostProcessCtrl = ClientFindFlatRecvProp(decoder, "m_hPostProcessCtrl", 0);
	const __int64 propColorCorrectionCtrl = ClientFindFlatRecvProp(decoder, "m_hColorCorrectionCtrl", 0);
	const __int64 propFogCtrl = ClientFindFlatRecvProp(decoder, "m_PlayerFog.m_hCtrl", 0);
	const __int64 propFov = ClientFindFlatRecvProp(decoder, "m_iFOV", 0);
	const __int64 propDefaultFov = ClientFindFlatRecvProp(decoder, "m_iDefaultFOV", 0);

	const int offObserverMode = ClientRecvPropOffset(propObserverMode);
	const int offObserverTarget = ClientRecvPropOffset(propObserverTarget);
	const int offViewEntity = ClientRecvPropOffset(propViewEntity);
	const int offViewOffsetEntity = ClientRecvPropOffset(propViewOffsetEntity);
	const int offThirdPersonEnt = ClientRecvPropOffset(propThirdPersonEnt);
	const int offPostProcessCtrl = ClientRecvPropOffset(propPostProcessCtrl);
	const int offColorCorrectionCtrl = ClientRecvPropOffset(propColorCorrectionCtrl);
	const int offFogCtrl = ClientRecvPropOffset(propFogCtrl);
	const int offFov = ClientRecvPropOffset(propFov);
	const int offDefaultFov = ClientRecvPropOffset(propDefaultFov);

	char viewBuffer[1024];
	_snprintf_s(
		viewBuffer,
		sizeof(viewBuffer),
		_TRUNCATE,
		"R1Delta: client local-player view state reason=%s observerMode[%d]=%d observerTarget[%d]=0x%x viewEnt[%d]=0x%x viewOffsetEnt[%d]=0x%x thirdPersonEnt[%d]=0x%x postProcess[%d]=0x%x colorCorrection[%d]=0x%x fogCtrl[%d]=0x%x fov[%d]=%d defaultFov[%d]=%d budget=%d\n",
		reason ? reason : "<null>",
		offObserverMode,
		ClientSafeReadIntField(object, offObserverMode),
		offObserverTarget,
		ClientSafeReadIntField(object, offObserverTarget),
		offViewEntity,
		ClientSafeReadIntField(object, offViewEntity),
		offViewOffsetEntity,
		ClientSafeReadIntField(object, offViewOffsetEntity),
		offThirdPersonEnt,
		ClientSafeReadIntField(object, offThirdPersonEnt),
		offPostProcessCtrl,
		ClientSafeReadIntField(object, offPostProcessCtrl),
		offColorCorrectionCtrl,
		ClientSafeReadIntField(object, offColorCorrectionCtrl),
		offFogCtrl,
		ClientSafeReadIntField(object, offFogCtrl),
		offFov,
		ClientSafeReadIntField(object, offFov),
		offDefaultFov,
		ClientSafeReadIntField(object, offDefaultFov),
		s_ClientLocalPlayerStateLogBudget);
	OutputDebugStringA(viewBuffer);
	Warning("%s", viewBuffer);

	char cameraBuffer[1536];
	_snprintf_s(
		cameraBuffer,
		sizeof(cameraBuffer),
		_TRUNCATE,
		"R1Delta: client camera-net state reason=%s eyeOffset[%d]=(%.3f,%.3f,%.3f) localUp[%d]=(%.3f,%.3f,%.3f) up[%d]=(%.3f,%.3f,%.3f) stickState[%d]=%d inAir[%d]=%d stickDelay[%d]=%.3f airInput[%d]=%.3f doneStick[%d]=%d donePitch[%d]=%d mainView=(%.3f,%.3f,%.3f) calcEye=(%.3f,%.3f,%.3f) budget=%d\n",
		reason ? reason : "<null>",
		offEyeOffset,
		ClientSafeReadFloatField(object, offEyeOffset),
		ClientSafeReadFloatField(object, offEyeOffset + 4),
		ClientSafeReadFloatField(object, offEyeOffset + 8),
		offLocalUp,
		ClientSafeReadFloatField(object, offLocalUp),
		ClientSafeReadFloatField(object, offLocalUp + 4),
		ClientSafeReadFloatField(object, offLocalUp + 8),
		offUp,
		ClientSafeReadFloatField(object, offUp),
		ClientSafeReadFloatField(object, offUp + 4),
		ClientSafeReadFloatField(object, offUp + 8),
		offStickCameraState,
		ClientSafeReadIntField(object, offStickCameraState),
		offInAirState,
		ClientSafeReadIntField(object, offInAirState),
		offCurrentStickTransitionDelay,
		ClientSafeReadFloatField(object, offCurrentStickTransitionDelay),
		offAirInputScale,
		ClientSafeReadFloatField(object, offAirInputScale),
		offDoneStickInterp,
		static_cast<int>(ClientSafeReadByteField(object, offDoneStickInterp)),
		offDoneCorrectPitch,
		static_cast<int>(ClientSafeReadByteField(object, offDoneCorrectPitch)),
		mainViewOrigin[0],
		mainViewOrigin[1],
		mainViewOrigin[2],
		eyeX,
		eyeY,
		eyeZ,
		s_ClientLocalPlayerStateLogBudget);
	OutputDebugStringA(cameraBuffer);
	Warning("%s", cameraBuffer);

	char compareBuffer[3072];
	_snprintf_s(
		compareBuffer,
		sizeof(compareBuffer),
		_TRUNCATE,
		"R1Delta: netprop-compare client tick=%d reason=%s localState=%d ent=%p flat=%d teamV=%d team[%d]=%d health[%d]=%d life[%d]=%d flags[%d]=0x%x cellbits[%d]=%d cell=(%d:%d,%d:%d,%d:%d) origin[%d,%d]=(%.3f,%.3f,%.3f) viewOffset=(%d:%.3f,%d:%.3f,%d:%.3f) absOriginOk=%d absOrigin=(%.3f,%.3f,%.3f) absAnglesOk=%d absAngles=(%.3f,%.3f,%.3f) calcEye=(%.3f,%.3f,%.3f) mainViewOk=%d mainView=(%.3f,%.3f,%.3f) mainAngles=(%.3f,%.3f,%.3f) mainMinusEye=(%.3f,%.3f,%.3f) eye=(%d:%.3f,%d:%.3f) observerMode[%d]=%d observerTarget[%d]=0x%x viewEnt[%d]=0x%x viewOffsetEnt[%d]=0x%x thirdPersonEnt[%d]=0x%x postProcess[%d]=0x%x colorCorrection[%d]=0x%x fogCtrl[%d]=0x%x budget=%d\n",
		static_cast<int>(GetTickCount()),
		reason ? reason : "<null>",
		ClientLocalSignonState(),
		ent,
		ClientRecvDecoderFlattenedPropCount(decoder),
		vfuncTeam,
		offTeam,
		ClientSafeReadIntField(object, offTeam),
		offHealth,
		ClientSafeReadIntField(object, offHealth),
		offLife,
		static_cast<int>(ClientSafeReadByteField(object, offLife)),
		offFlags,
		ClientSafeReadIntField(object, offFlags),
		offCellBits,
		ClientSafeReadIntField(object, offCellBits),
		offCellX,
		ClientSafeReadIntField(object, offCellX),
		offCellY,
		ClientSafeReadIntField(object, offCellY),
		offCellZ,
		ClientSafeReadIntField(object, offCellZ),
		offOrigin,
		offOriginZ,
		netOriginX,
		netOriginY,
		netOriginZ,
		offViewX,
		viewX,
		offViewY,
		viewY,
		offViewZ,
		viewZ,
		absOriginOk ? 1 : 0,
		absOrigin[0],
		absOrigin[1],
		absOrigin[2],
		absAnglesOk ? 1 : 0,
		absAngles[0],
		absAngles[1],
		absAngles[2],
		eyeX,
		eyeY,
		eyeZ,
		mainViewOk ? 1 : 0,
		mainViewOrigin[0],
		mainViewOrigin[1],
		mainViewOrigin[2],
		mainViewAngles[0],
		mainViewAngles[1],
		mainViewAngles[2],
		mainViewOrigin[0] - eyeX,
		mainViewOrigin[1] - eyeY,
		mainViewOrigin[2] - eyeZ,
		offEyePitch,
		ClientSafeReadFloatField(object, offEyePitch),
		offEyeYaw,
		ClientSafeReadFloatField(object, offEyeYaw),
		offObserverMode,
		ClientSafeReadIntField(object, offObserverMode),
		offObserverTarget,
		ClientSafeReadIntField(object, offObserverTarget),
		offViewEntity,
		ClientSafeReadIntField(object, offViewEntity),
		offViewOffsetEntity,
		ClientSafeReadIntField(object, offViewOffsetEntity),
		offThirdPersonEnt,
		ClientSafeReadIntField(object, offThirdPersonEnt),
		offPostProcessCtrl,
		ClientSafeReadIntField(object, offPostProcessCtrl),
		offColorCorrectionCtrl,
		ClientSafeReadIntField(object, offColorCorrectionCtrl),
		offFogCtrl,
		ClientSafeReadIntField(object, offFogCtrl),
		s_ClientLocalPlayerStateLogBudget);
	OutputDebugStringA(compareBuffer);
	Warning("%s", compareBuffer);
}

static const char* ClientSendTableName(__int64 sendTable)
{
	if (!sendTable)
		return "<null-sendtable>";

	__try {
		const char* name = *reinterpret_cast<const char**>(sendTable + 16);
		return ClientIsReadableCString(name) ? name : "<bad-sendtable-name>";
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return "<av-sendtable-name>";
	}
}

static int ClientSendTablePropCount(__int64 sendTable)
{
	if (!sendTable)
		return -1;

	__try {
		return *reinterpret_cast<int*>(sendTable + 8);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return -2;
	}
}

static __int64 ClientSendTableProp(__int64 sendTable, unsigned int index)
{
	if (!sendTable)
		return 0;

	__try {
		const __int64 props = *reinterpret_cast<__int64*>(sendTable);
		const int count = *reinterpret_cast<int*>(sendTable + 8);
		if (!props || index >= static_cast<unsigned int>(count))
			return 0;
		return props + 136LL * index;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return 0;
	}
}

static bool ClientIsWorldTableName(const char* name)
{
	return name
		&& (strcmp_static(name, "DT_World") == 0 || strcmp_static(name, "DT_WORLD") == 0);
}

static bool ClientIsPlayerResourceArrayTableName(const char* name)
{
	if (!name)
		return false;

	static const char* const names[] = {
		"m_iPing",
		"m_score",
		"m_kills",
		"m_deaths",
		"m_bConnected",
		"m_iTeam",
		"m_bAlive",
		"m_iPRHealth",
		"m_titanKills",
		"m_npcKills",
		"m_assists",
		"m_assaultScore",
		"m_defenseScore"
	};
	for (const char* candidate : names) {
		if (_stricmp(name, candidate) == 0)
			return true;
	}
	return false;
}

static bool ClientIsDataTableSetupTraceName(const char* name)
{
	return name
		&& (ClientIsWorldTableName(name)
			|| strcmp_static(name, "DT_PlayerResource") == 0
			|| ClientIsPlayerResourceArrayTableName(name)
			|| strcmp_static(name, "DT_PhysicsProp") == 0
			|| strcmp_static(name, "DT_EnvTonemapController") == 0);
}

static void ClientLogSendPropLine(const char* prefix, const char* tableName, unsigned int index, __int64 prop)
{
	if (s_ClientDataTableSetupLogBudget <= 0)
		return;

	--s_ClientDataTableSetupLogBudget;
	char buffer[768];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: client %s table=\"%s\" [%u]=%p name=\"%s\" type=%d flags=0x%x offset=%d valid=%d budget=%d\n",
		prefix,
		tableName ? tableName : "<null>",
		index,
		reinterpret_cast<void*>(prop),
		ClientSendPropName(prop),
		ClientSendPropType(prop),
		ClientSendPropFlags(prop),
		ClientSendPropOffset(prop),
		ClientSendPropLooksValid(prop) ? 1 : 0,
		s_ClientDataTableSetupLogBudget);
	OutputDebugStringA(buffer);
	Warning("%s", buffer);
}

static void ClientLogPlayerResourceChildTables(__int64 sendTable, __int64 recvTable)
{
	if (s_ClientDataTableSetupLogBudget <= 0)
		return;

	const int sendParentCount = ClientSendTablePropCount(sendTable);
	const int recvParentCount = ClientRecvTablePropCount(recvTable);
	const int parentCount = sendParentCount < recvParentCount ? sendParentCount : recvParentCount;
	for (int parentIndex = 0;
		parentIndex < parentCount && s_ClientDataTableSetupLogBudget > 0;
		++parentIndex)
	{
		const __int64 sendParent = ClientSendTableProp(sendTable, static_cast<unsigned int>(parentIndex));
		const __int64 recvParent = ClientRecvTableProp(recvTable, static_cast<unsigned int>(parentIndex));
		const __int64 sendChildTable = ClientSendPropDataTable(sendParent);
		const __int64 recvChildTable = ClientRecvPropDataTable(recvParent);

		--s_ClientDataTableSetupLogBudget;
		char header[1024];
		_snprintf_s(
			header,
			sizeof(header),
			_TRUNCATE,
			"R1Delta: PlayerResource hierarchy parent=%d "
			"sendParent=%p sName=\"%s\" sType=%d sFlags=0x%x sChild=%p "
			"sChildName=\"%s\" sChildCount=%d "
			"recvParent=%p rName=\"%s\" rType=%d rFlags=0x%x rChild=%p "
			"rChildName=\"%s\" rChildCount=%d budget=%d\n",
			parentIndex,
			reinterpret_cast<void*>(sendParent),
			ClientSendPropName(sendParent),
			ClientSendPropType(sendParent),
			ClientSendPropFlags(sendParent),
			reinterpret_cast<void*>(sendChildTable),
			ClientSendTableName(sendChildTable),
			ClientSendTablePropCount(sendChildTable),
			reinterpret_cast<void*>(recvParent),
			ClientRecvPropName(recvParent),
			ClientRecvPropType(recvParent),
			ClientRecvPropFlags(recvParent),
			reinterpret_cast<void*>(recvChildTable),
			ClientRecvTableName(recvChildTable),
			ClientRecvTablePropCount(recvChildTable),
			s_ClientDataTableSetupLogBudget);
		OutputDebugStringA(header);
		Warning("%s", header);

		const int sendChildCount = ClientSendTablePropCount(sendChildTable);
		const int recvChildCount = ClientRecvTablePropCount(recvChildTable);
		const int firstChild = 15;
		const int childCount = sendChildCount > recvChildCount ? sendChildCount : recvChildCount;
		for (int childIndex = firstChild;
			childIndex < childCount && childIndex < 20 && s_ClientDataTableSetupLogBudget > 0;
			++childIndex)
		{
			const __int64 sendChild = ClientSendTableProp(sendChildTable, static_cast<unsigned int>(childIndex));
			const __int64 recvChild = ClientRecvTableProp(recvChildTable, static_cast<unsigned int>(childIndex));
			--s_ClientDataTableSetupLogBudget;
			char child[1024];
			_snprintf_s(
				child,
				sizeof(child),
				_TRUNCATE,
				"R1Delta: PlayerResource child parent=%d index=%d "
				"send=%p sName=\"%s\" sType=%d sBits=%d sFlags=0x%x sOffset=%d "
				"recv=%p rName=\"%s\" rType=%d rFlags=0x%x rOffset=%d "
				"rStride=%d rElements=%d budget=%d\n",
				parentIndex,
				childIndex,
				reinterpret_cast<void*>(sendChild),
				ClientSendPropName(sendChild),
				ClientSendPropType(sendChild),
				ClientSendPropNumBits(sendChild),
				ClientSendPropFlags(sendChild),
				ClientSendPropOffset(sendChild),
				reinterpret_cast<void*>(recvChild),
				ClientRecvPropName(recvChild),
				ClientRecvPropType(recvChild),
				ClientRecvPropFlags(recvChild),
				ClientRecvPropOffset(recvChild),
				ClientRecvPropElementStride(recvChild),
				ClientRecvPropNumElements(recvChild),
				s_ClientDataTableSetupLogBudget);
			OutputDebugStringA(child);
			Warning("%s", child);
		}
	}
}

static void ClientLogRecvDecoder(const char* phase, __int64 recvTable)
{
	if (s_ClientDataTableSetupLogBudget <= 0 || !recvTable)
		return;

	const char* recvName = ClientRecvTableName(recvTable);
	if (!ClientIsDataTableSetupTraceName(recvName))
		return;

	const __int64 decoder = ClientDecoderForRecvTable(recvTable);
	const int flatCount = ClientRecvDecoderFlattenedPropCount(decoder);
	const int sendCount = ClientRecvDecoderSendPropCount(decoder);
	--s_ClientDataTableSetupLogBudget;
	char header[512];
	_snprintf_s(
		header,
		sizeof(header),
		_TRUNCATE,
		"R1Delta: client DataTable decoder %s recvTable=%p name=\"%s\" direct=%d decoder=%p recvFlatCount=%d sendFlatCount=%d budget=%d\n",
		phase,
		reinterpret_cast<void*>(recvTable),
		recvName,
		ClientRecvTablePropCount(recvTable),
		reinterpret_cast<void*>(decoder),
		flatCount,
		sendCount,
		s_ClientDataTableSetupLogBudget);
	OutputDebugStringA(header);
	Warning("%s", header);

	const int count = flatCount > 140 ? 140 : flatCount;
	for (int i = 0; i < count && s_ClientDataTableSetupLogBudget > 0; ++i) {
		const __int64 sendProp = ClientRecvDecoderSendProp(decoder, static_cast<unsigned int>(i));
		ClientLogSendPropLine("decoder-sendprop", recvName, static_cast<unsigned int>(i), sendProp);
	}
}

static char __fastcall ClientDataTableSetupReceive(__int64 sendTable, char needsDecoder)
{
	const char* sendName = ClientSendTableName(sendTable);
	const bool traceTable = ClientIsDataTableSetupTraceName(sendName);
	if (traceTable && s_ClientDataTableSetupLogBudget > 0) {
		--s_ClientDataTableSetupLogBudget;
		char buffer[512];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: client DataTable setup enter sendTable=%p name=\"%s\" direct=%d needsDecoder=%d budget=%d\n",
			reinterpret_cast<void*>(sendTable),
			sendName,
			ClientSendTablePropCount(sendTable),
			static_cast<int>(needsDecoder),
			s_ClientDataTableSetupLogBudget);
		OutputDebugStringA(buffer);
		Warning("%s", buffer);

		const int directCount = ClientSendTablePropCount(sendTable);
		for (int i = 0; i < directCount && s_ClientDataTableSetupLogBudget > 0; ++i)
			ClientLogSendPropLine("parsed-sendprop", sendName, static_cast<unsigned int>(i), ClientSendTableProp(sendTable, static_cast<unsigned int>(i)));
	}

	const char result = s_ClientDataTableSetupReceiveOriginal
		? s_ClientDataTableSetupReceiveOriginal(sendTable, needsDecoder)
		: 0;

	if (s_ClientFindRecvTableByName && sendName && _stricmp(sendName, "DT_HL2_Player") == 0) {
		__int64 recvTable = 0;
		__try {
			recvTable = s_ClientFindRecvTableByName(sendName);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			recvTable = 0;
		}
		ClientMaybeInstallHL2PlayerRecvProxyTrace(ClientDecoderForRecvTable(recvTable));
	}

	if (traceTable && s_ClientFindRecvTableByName) {
		__int64 recvTable = 0;
		__try {
			recvTable = s_ClientFindRecvTableByName(sendName);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			recvTable = 0;
		}
		if (sendName && _stricmp(sendName, "DT_PlayerResource") == 0)
			ClientLogPlayerResourceChildTables(sendTable, recvTable);
		ClientLogRecvDecoder(result ? "leave-ok" : "leave-fail", recvTable);
	}

	return result;
}

static void ClientDescribeDecoderPath(char* out, size_t outSize, __int64 decoder, unsigned int path)
{
	if (!out || !outSize)
		return;
	out[0] = '\0';

	const __int64 recvProp = ClientRecvDecoderRecvProp(decoder, path);
	const __int64 sendProp = ClientRecvDecoderSendProp(decoder, path);
	_snprintf_s(
		out,
		outSize,
		_TRUNCATE,
		"%u:%s/%s:t%d:f%x",
		path,
		recvProp ? "recv" : "null",
		ClientSendPropName(sendProp),
		ClientSendPropType(sendProp),
		ClientSendPropFlags(sendProp));
}

static void ClientAppendText(char* out, size_t outSize, size_t* used, const char* text)
{
	if (!out || !outSize || !used || !text || *used >= outSize)
		return;

	const int written = _snprintf_s(out + *used, outSize - *used, _TRUNCATE, "%s", text);
	if (written > 0)
		*used += static_cast<size_t>(written);
	else
		*used = outSize;
}

static void ClientBytesHex(char* out, size_t outSize, const unsigned char* data, int count)
{
	if (!out || !outSize)
		return;

	out[0] = '\0';
	if (!data || count <= 0)
		return;

	size_t used = 0;
	__try {
		for (int i = 0; i < count && used + 4 < outSize; ++i) {
			const int written = _snprintf_s(
				out + used,
				outSize - used,
				_TRUNCATE,
				"%02X%s",
				data[i],
				i + 1 < count ? " " : "");
			if (written <= 0)
				break;
			used += static_cast<size_t>(written);
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		strncpy_s(out, outSize, "<av>", _TRUNCATE);
	}
}

static void ClientAppendSerializedEntityPaths(char* out, size_t outSize, size_t* used, const char* label, __int64 entity, __int64 decoder)
{
	ClientAppendText(out, outSize, used, label);
	if (!entity) {
		ClientAppendText(out, outSize, used, "<null>");
		return;
	}

	__try {
		if (!G_engine || !ClientIsReadableRange(reinterpret_cast<const void*>(entity), 0x60)) {
			ClientAppendText(out, outSize, used, "<unreadable-bfread>");
			return;
		}

		using DeltaBitsReaderInitType = __int64(__fastcall*)(void* reader, __int64 bitbuf);
		using DeltaBitsReaderNextType = __int64(__fastcall*)(void* reader);
		auto initReader = reinterpret_cast<DeltaBitsReaderInitType>(G_engine + 0x1BED50);
		auto nextProp = reinterpret_cast<DeltaBitsReaderNextType>(G_engine + 0x1BED80);

		alignas(16) unsigned char bitbufCopy[0x80] = {};
		alignas(16) unsigned char deltaReader[0x20] = {};
		memcpy(bitbufCopy, reinterpret_cast<const void*>(entity), 0x60);
		initReader(deltaReader, reinterpret_cast<__int64>(bitbufCopy));

		const int startBit = ClientCBitReadTell(reinterpret_cast<__int64>(bitbufCopy));
		char header[128];
		_snprintf_s(
			header,
			sizeof(header),
			_TRUNCATE,
			"bf=%p bit=%d:",
			reinterpret_cast<void*>(entity),
			startBit);
		ClientAppendText(out, outSize, used, header);

		for (unsigned int i = 0; i < 32; ++i) {
			const unsigned int path = static_cast<unsigned int>(nextProp(deltaReader));
			if (path == 0x7FFFFFFF)
				break;
			char pathText[192];
			const int flatCount = ClientRecvDecoderFlattenedPropCount(decoder);
			if (flatCount >= 0 && path >= static_cast<unsigned int>(flatCount)) {
				_snprintf_s(
					pathText,
					sizeof(pathText),
					_TRUNCATE,
					"%u:<invalid flatCount=%d>",
					path,
					flatCount);
			}
			else {
				ClientDescribeDecoderPath(pathText, sizeof(pathText), decoder, path);
			}
			ClientAppendText(out, outSize, used, i ? "," : "");
			ClientAppendText(out, outSize, used, pathText);
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		ClientAppendText(out, outSize, used, "<av>");
	}
}

static void ClientDescribeBitRead(char* out, size_t outSize, __int64 bitBuffer)
{
	if (!out || !outSize)
		return;

	out[0] = '\0';
	if (!bitBuffer) {
		strncpy_s(out, outSize, "<null>", _TRUNCATE);
		return;
	}

	__try {
		const int dataBits = *reinterpret_cast<int*>(bitBuffer + 16);
		const int dataBytes = *reinterpret_cast<int*>(bitBuffer + 24);
		const uint32_t scratch = *reinterpret_cast<uint32_t*>(bitBuffer + 32);
		const int bitsAvail = *reinterpret_cast<int*>(bitBuffer + 36);
		const void* dataIn = *reinterpret_cast<void**>(bitBuffer + 40);
		const bool overflow = *reinterpret_cast<unsigned char*>(bitBuffer + 48) != 0;
		const void* data = *reinterpret_cast<void**>(bitBuffer + 56);
		const int tell = ClientCBitReadTell(bitBuffer);
		char window[128];
		ClientDumpBitWindow(window, sizeof(window), bitBuffer, tell);
		_snprintf_s(
			out,
			outSize,
			_TRUNCATE,
			"ptr=%p bits=%d bytes=%d scratch=0x%08x avail=%d dataIn=%p data=%p tell=%d overflow=%d bytes=[%s]",
			reinterpret_cast<void*>(bitBuffer),
			dataBits,
			dataBytes,
			scratch,
			bitsAvail,
			dataIn,
			data,
			tell,
			overflow ? 1 : 0,
			window);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		strncpy_s(out, outSize, "<av>", _TRUNCATE);
	}
}

static const char* ClientRecvTableMergeSourceKind(__int64 sourceState)
{
	if (sourceState == s_ClientRecvTableMergeCurrentOldState)
		return "old";
	if (sourceState == s_ClientRecvTableMergeCurrentNewState)
		return "new";
	if (sourceState == s_ClientRecvTableMergeCurrentOutState)
		return "out";
	return "other";
}

static bool ClientSerializedEntityHasInvalidPath(__int64 entity, int flatCount)
{
	if (!entity || flatCount <= 0)
		return false;

	__try {
		if (!G_engine || !ClientIsReadableRange(reinterpret_cast<const void*>(entity), 0x60))
			return true;

		using DeltaBitsReaderInitType = __int64(__fastcall*)(void* reader, __int64 bitbuf);
		using DeltaBitsReaderNextType = __int64(__fastcall*)(void* reader);
		auto initReader = reinterpret_cast<DeltaBitsReaderInitType>(G_engine + 0x1BED50);
		auto nextProp = reinterpret_cast<DeltaBitsReaderNextType>(G_engine + 0x1BED80);

		alignas(16) unsigned char bitbufCopy[0x80] = {};
		alignas(16) unsigned char deltaReader[0x20] = {};
		memcpy(bitbufCopy, reinterpret_cast<const void*>(entity), 0x60);
		initReader(deltaReader, reinterpret_cast<__int64>(bitbufCopy));

		for (unsigned int i = 0; i < 4096; ++i) {
			const unsigned int path = static_cast<unsigned int>(nextProp(deltaReader));
			if (path == 0x7FFFFFFF)
				return false;
			if (path >= static_cast<unsigned int>(flatCount))
				return true;
		}
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return true;
	}
}

static __int64 ClientEngineClientState()
{
	if (!G_engine)
		return 0;

	__try {
		return reinterpret_cast<__int64(__fastcall*)()>(G_engine + 0x5F4B0)();
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return 0;
	}
}

static const char* ClientClassNameFromIndex(__int64 clientState, int classIndex)
{
	if (!clientState || classIndex < 0)
		return "<bad-class-index>";

	__try {
		const int classCount = *reinterpret_cast<int*>(clientState + 66464);
		const __int64 classes = *reinterpret_cast<__int64*>(clientState + 66456);
		if (!classes || classIndex >= classCount)
			return "<out-of-range-class>";
		const __int64 clientClass = *reinterpret_cast<__int64*>(classes + 32LL * classIndex);
		if (!clientClass)
			return "<null-clientclass>";
		const char* name = *reinterpret_cast<const char**>(clientClass + 16);
		return ClientIsReadableCString(name) ? name : "<bad-class-name>";
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return "<av-class-name>";
	}
}

static bool ClientClassBaseline(__int64 clientState, int classIndex, __int64* data, int* bytes)
{
	if (data)
		*data = 0;
	if (bytes)
		*bytes = 0;
	if (!G_engine || !clientState || classIndex < 0)
		return false;

	__try {
		using GetClassBaselineType = unsigned char(__fastcall*)(__int64 clientState, int classIndex, __int64* data, int* bytes);
		return reinterpret_cast<GetClassBaselineType>(G_engine + 0x23670)(clientState, classIndex, data, bytes) != 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

static __int64 ClientRecvTableFromClassIndex(__int64 clientState, int classIndex)
{
	if (!G_engine || !clientState || classIndex < 0)
		return 0;

	__try {
		using GetClientClassByIndexType = __int64(__fastcall*)(__int64 clientState, int classIndex);
		const __int64 clientClass = reinterpret_cast<GetClientClassByIndexType>(G_engine + 0x23650)(clientState, classIndex);
		return clientClass ? *reinterpret_cast<__int64*>(clientClass + 24) : 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return 0;
	}
}

static void ClientDescribeBaselinePaths(char* out, size_t outSize, __int64 data, int bits, __int64 decoder)
{
	if (!out || !outSize)
		return;
	out[0] = '\0';
	if (!data || bits <= 0) {
		strncpy_s(out, outSize, "<empty>", _TRUNCATE);
		return;
	}

	__try {
		if (!G_engine)
			return;

		using DeltaBitsReaderInitType = __int64(__fastcall*)(void* reader, __int64 bitbuf);
		using DeltaBitsReaderNextType = __int64(__fastcall*)(void* reader);
		using CBitReadInitType = void(__fastcall*)(void* bitbuf, __int64 data, int bytes, int startBit, int bits);
		auto initReader = reinterpret_cast<DeltaBitsReaderInitType>(G_engine + 0x1BED50);
		auto nextProp = reinterpret_cast<DeltaBitsReaderNextType>(G_engine + 0x1BED80);
		auto initBitRead = reinterpret_cast<CBitReadInitType>(G_engine + 0x48EF80);

		alignas(16) unsigned char bitbuf[0x80] = {};
		alignas(16) unsigned char deltaReader[0x20] = {};
		initBitRead(bitbuf, data, (bits + 7) >> 3, 0, bits);

		initReader(deltaReader, reinterpret_cast<__int64>(bitbuf));

		size_t used = 0;
		const int flatCount = ClientRecvDecoderFlattenedPropCount(decoder);
		for (unsigned int i = 0; i < 24; ++i) {
			const unsigned int path = static_cast<unsigned int>(nextProp(deltaReader));
			if (path == 0x7FFFFFFF) {
				ClientAppendText(out, outSize, &used, i ? ",END" : "END");
				break;
			}

			char entry[128];
			if (flatCount >= 0 && path >= static_cast<unsigned int>(flatCount)) {
				_snprintf_s(entry, sizeof(entry), _TRUNCATE, "%s%u:<invalid flatCount=%d>", i ? "," : "", path, flatCount);
			}
			else {
				char pathText[96];
				ClientDescribeDecoderPath(pathText, sizeof(pathText), decoder, path);
				_snprintf_s(entry, sizeof(entry), _TRUNCATE, "%s%s", i ? "," : "", pathText);
			}
			ClientAppendText(out, outSize, &used, entry);
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		strncpy_s(out, outSize, "<av>", _TRUNCATE);
	}
}

static void ClientAuditRecvDecoderMapping(__int64 recvTable, __int64 decoder)
{
	if (!ShouldInstallR1OClientDebugHooks() || !recvTable || !decoder || IsDedicatedServer())
		return;

	for (int i = 0; i < s_ClientAuditedRecvDecoderCount; ++i) {
		if (s_ClientAuditedRecvDecoders[i] == decoder)
			return;
	}
	if (s_ClientAuditedRecvDecoderCount >= static_cast<int>(sizeof(s_ClientAuditedRecvDecoders) / sizeof(s_ClientAuditedRecvDecoders[0])))
		return;
	s_ClientAuditedRecvDecoders[s_ClientAuditedRecvDecoderCount++] = decoder;

	const char* const tableName = ClientRecvTableName(recvTable);
	const bool dumpAll = tableName
		&& (_stricmp(tableName, "DT_NPC_CombineDropship") == 0
			|| _stricmp(tableName, "DT_PlayerResource") == 0);
	const int flatCount = ClientRecvDecoderFlattenedPropCount(decoder);
	const int sendCount = ClientRecvDecoderSendPropCount(decoder);
	int nullRecv = 0;
	int nullSend = 0;
	int nameMismatch = 0;
	int typeMismatch = 0;

	const int auditCount = flatCount > sendCount ? flatCount : sendCount;
	for (int i = 0; i < auditCount && i < 4096; ++i) {
		const __int64 recvProp = ClientRecvDecoderRecvProp(decoder, static_cast<unsigned int>(i));
		const __int64 sendProp = ClientRecvDecoderSendProp(decoder, static_cast<unsigned int>(i));
		const char* const recvName = ClientRecvPropName(recvProp);
		const char* const sendName = ClientSendPropName(sendProp);
		const int recvType = ClientRecvPropType(recvProp);
		const int sendType = ClientSendPropType(sendProp);
		const bool namesDiffer = !recvProp || !sendProp || _stricmp(recvName, sendName) != 0;
		const bool typesDiffer = recvProp && sendProp && recvType != sendType;
		if (!recvProp)
			++nullRecv;
		if (!sendProp)
			++nullSend;
		if (namesDiffer)
			++nameMismatch;
		if (typesDiffer)
			++typeMismatch;

		if (s_ClientRecvDecoderAuditLogBudget <= 0 || (!dumpAll && !namesDiffer && !typesDiffer))
			continue;

		--s_ClientRecvDecoderAuditLogBudget;
		char buffer[1024];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: recvdecoder-audit table=\"%s\" decoder=%p flat=%d recvCount=%d sendCount=%d recv=%p rName=\"%s\" rType=%d rFlags=0x%x rOffset=%d send=%p sName=\"%s\" sType=%d sBits=%d sFlags=0x%x nameMismatch=%d typeMismatch=%d budget=%d\n",
			tableName,
			reinterpret_cast<void*>(decoder),
			i,
			flatCount,
			sendCount,
			reinterpret_cast<void*>(recvProp),
			recvName,
			recvType,
			ClientRecvPropFlags(recvProp),
			ClientRecvPropOffset(recvProp),
			reinterpret_cast<void*>(sendProp),
			sendName,
			sendType,
			ClientSendPropNumBits(sendProp),
			ClientSendPropFlags(sendProp),
			namesDiffer ? 1 : 0,
			typesDiffer ? 1 : 0,
			s_ClientRecvDecoderAuditLogBudget);
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}

	if (s_ClientRecvDecoderAuditLogBudget > 0) {
		--s_ClientRecvDecoderAuditLogBudget;
		char buffer[640];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: recvdecoder-audit summary table=\"%s\" decoder=%p recvCount=%d sendCount=%d nullRecv=%d nullSend=%d nameMismatch=%d typeMismatch=%d dumpAll=%d audited=%d budget=%d\n",
			tableName,
			reinterpret_cast<void*>(decoder),
			flatCount,
			sendCount,
			nullRecv,
			nullSend,
			nameMismatch,
			typeMismatch,
			dumpAll ? 1 : 0,
			s_ClientAuditedRecvDecoderCount,
			s_ClientRecvDecoderAuditLogBudget);
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}
}

static char __fastcall ClientCopyNewEntity(__int64 parseInfo, int classIndex, unsigned int serial)
{
	const __int64 clientState = ClientEngineClientState();
	const __int64 recvTable = ClientRecvTableFromClassIndex(clientState, classIndex);
	const __int64 decoder = ClientDecoderForRecvTable(recvTable);
	ClientAuditRecvDecoderMapping(recvTable, decoder);

	if (s_ClientCopyNewEntityLogBudget > 0) {
		--s_ClientCopyNewEntityLogBudget;
		const char* className = ClientClassNameFromIndex(clientState, classIndex);
		int entIndex = -1;
		int classCount = -1;
		int baselineBytes = 0;
		__int64 baselineData = 0;
		const int flatCount = ClientRecvDecoderFlattenedPropCount(decoder);
		bool baselineOk = false;
		__try {
			entIndex = parseInfo ? *reinterpret_cast<int*>(parseInfo + 40) : -1;
			classCount = clientState ? *reinterpret_cast<int*>(clientState + 66464) : -1;
			baselineOk = ClientClassBaseline(clientState, classIndex, &baselineData, &baselineBytes);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			entIndex = -2;
			classCount = -2;
		}

		char bytes[128];
		bytes[0] = '\0';
		if (baselineData && baselineBytes > 0)
			ClientBytesHex(bytes, sizeof(bytes), reinterpret_cast<const unsigned char*>(baselineData), baselineBytes < 24 ? baselineBytes : 24);

		char buffer[1536];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: client CL_CopyNewEntity enter parse=%p ent=%d class=%d/%d serial=%u className=\"%s\" recvTable=%p recvName=\"%s\" decoder=%p flatCount=%d baselineOk=%d baseline=%p bytes=%d first=[%s] budget=%d\n",
			reinterpret_cast<void*>(parseInfo),
			entIndex,
			classIndex,
			classCount,
			serial,
			className,
			reinterpret_cast<void*>(recvTable),
			ClientRecvTableName(recvTable),
			reinterpret_cast<void*>(decoder),
			flatCount,
			baselineOk ? 1 : 0,
			reinterpret_cast<void*>(baselineData),
			baselineBytes,
			bytes,
			s_ClientCopyNewEntityLogBudget);
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}

	return s_ClientCopyNewEntityOriginal
		? s_ClientCopyNewEntityOriginal(parseInfo, classIndex, serial)
		: 0;
}

static __int64 __fastcall ClientRecvTableMergeProp(__int64 recvProp, unsigned int propIndex, __int64 sourceState, __int64 outState)
{
	if (recvProp && ClientSendPropLooksValid(recvProp)) {
		char beforeBits[512] = {};
		const char* propName = ClientSendPropName(recvProp);
		const bool traceCameraPlayerProp = propName
			&& s_ClientRecvTableMergeCurrentName
			&& strcmp(s_ClientRecvTableMergeCurrentName, "DT_HL2_Player") == 0
			&& s_ClientRecvTableMergeCurrentEntity == 1
			&& (_stricmp(propName, "m_vEyeOffset") == 0
				|| _stricmp(propName, "m_vLocalUp") == 0
				|| _stricmp(propName, "m_Up") == 0
				|| _stricmp(propName, "m_nStickCameraState") == 0
				|| _stricmp(propName, "m_InAirState") == 0
				|| _stricmp(propName, "m_flCurrentStickTransitionDelay") == 0
				|| _stricmp(propName, "m_flAirInputScale") == 0
				|| _stricmp(propName, "m_bDoneStickInterp") == 0
				|| _stricmp(propName, "m_bDoneCorrectPitch") == 0
				|| _stricmp(propName, "m_hWhooshTargetEntity") == 0
				|| _stricmp(propName, "m_iWhooshCameraMode") == 0
				|| _stricmp(propName, "m_iWhooshCameraParity") == 0);
		const bool traceValidPlayerProp = s_ClientRecvTableMergeValidLogBudget > 0
			&& s_ClientRecvTableMergeCurrentName
			&& (ClientIsDeltaTraceTable(s_ClientRecvTableMergeCurrentName)
				|| strcmp(s_ClientRecvTableMergeCurrentName, "DT_ParticleSystem") == 0)
			&& (sourceState == s_ClientRecvTableMergeCurrentOldState
				|| (sourceState == s_ClientRecvTableMergeCurrentNewState
					&& (strcmp(s_ClientRecvTableMergeCurrentName, "DT_PhysicsProp") == 0
						|| strcmp(s_ClientRecvTableMergeCurrentName, "DT_ParticleSystem") == 0
						|| (strcmp(s_ClientRecvTableMergeCurrentName, "DT_HL2_Player") == 0 && (propIndex <= 64 || traceCameraPlayerProp)))));
		const bool tracePlayerCellPayload = s_ClientCellDecodeTraceLogBudget > 0
			&& s_ClientRecvTableMergeCurrentName
			&& strcmp(s_ClientRecvTableMergeCurrentName, "DT_HL2_Player") == 0
			&& s_ClientRecvTableMergeCurrentEntity == 1
			&& propName
			&& (_stricmp(propName, "m_cellX") == 0
				|| _stricmp(propName, "m_cellY") == 0
				|| _stricmp(propName, "m_cellZ") == 0
				|| _stricmp(propName, "m_cellbits") == 0
				|| _stricmp(propName, "m_hWhooshTargetEntity") == 0
				|| _stricmp(propName, "m_iWhooshCameraMode") == 0
				|| _stricmp(propName, "m_iWhooshCameraParity") == 0);
		const int beforeValueBit = tracePlayerCellPayload ? ClientCBitReadTell(sourceState) : -1;
		if (traceValidPlayerProp)
			ClientDescribeBitRead(beforeBits, sizeof(beforeBits), sourceState);
		else if (s_ClientRecvTableMergeCurrentName && strcmp(s_ClientRecvTableMergeCurrentName, "DT_ParticleSystem") == 0)
			ClientDescribeBitRead(beforeBits, sizeof(beforeBits), sourceState);

		if (s_ClientRecvTableMergePreCrashLogBudget > 0
			&& s_ClientRecvTableMergeCurrentName
			&& strcmp(s_ClientRecvTableMergeCurrentName, "DT_ParticleSystem") == 0) {
			--s_ClientRecvTableMergePreCrashLogBudget;
			char preBuffer[1536];
			_snprintf_s(
				preBuffer,
				sizeof(preBuffer),
				_TRUNCATE,
				"R1Delta: client RecvTable_MergeDeltas pre-prop table=\"%s\" ent=%d a6=%d sourceKind=%s propIndex=%u prop=%p name=\"%s\" type=%d bits=%d flags=0x%x offset=%d source={%s} out=%p budget=%d\n",
				s_ClientRecvTableMergeCurrentName,
				s_ClientRecvTableMergeCurrentEntity,
				s_ClientRecvTableMergeCurrentA6,
				ClientRecvTableMergeSourceKind(sourceState),
				propIndex,
				reinterpret_cast<void*>(recvProp),
				propName,
				ClientSendPropType(recvProp),
				ClientSendPropNumBits(recvProp),
				ClientSendPropFlags(recvProp),
				ClientSendPropOffset(recvProp),
				beforeBits,
				reinterpret_cast<void*>(outState),
				s_ClientRecvTableMergePreCrashLogBudget);
			OutputDebugStringA(preBuffer);
			Warning("%s", preBuffer);
		}

		const __int64 result = s_ClientRecvTableMergePropOriginal
			? s_ClientRecvTableMergePropOriginal(recvProp, propIndex, sourceState, outState)
			: 0;

		if (traceValidPlayerProp) {
			--s_ClientRecvTableMergeValidLogBudget;
			char afterBits[512];
			ClientDescribeBitRead(afterBits, sizeof(afterBits), sourceState);
			char buffer[1536];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: client RecvTable_MergeDeltas valid prop table=\"%s\" ent=%d a6=%d sourceKind=%s propIndex=%u prop=%p name=\"%s\" type=%d bits=%d flags=0x%x offset=%d before={%s} after={%s} result=%lld budget=%d\n",
				s_ClientRecvTableMergeCurrentName,
				s_ClientRecvTableMergeCurrentEntity,
				s_ClientRecvTableMergeCurrentA6,
				ClientRecvTableMergeSourceKind(sourceState),
				propIndex,
				reinterpret_cast<void*>(recvProp),
				propName,
				ClientSendPropType(recvProp),
				ClientSendPropNumBits(recvProp),
				ClientSendPropFlags(recvProp),
				ClientSendPropOffset(recvProp),
				beforeBits,
				afterBits,
				static_cast<long long>(result),
				s_ClientRecvTableMergeValidLogBudget);
			OutputDebugStringA(buffer);
			Warning("%s", buffer);
		}

		if (tracePlayerCellPayload) {
			--s_ClientCellDecodeTraceLogBudget;
			const int afterValueBit = ClientCBitReadTell(sourceState);
			char payloadBits[128];
			ClientDumpBitRange(payloadBits, sizeof(payloadBits), sourceState, beforeValueBit, afterValueBit);
			bool payloadOk = false;
			const unsigned int payloadValue = ClientReadBitRangeUnsigned(sourceState, beforeValueBit, afterValueBit, &payloadOk);
			char buffer[1024];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: cell-decode r1 client table=\"%s\" ent=%d a6=%d sourceKind=%s propIndex=%u prop=%p name=\"%s\" type=%d numBits=%d flags=0x%x offset=%d bits=%d->%d payload=\"%s\" payloadU=%u payloadOk=%d result=%lld budget=%d\n",
				s_ClientRecvTableMergeCurrentName,
				s_ClientRecvTableMergeCurrentEntity,
				s_ClientRecvTableMergeCurrentA6,
				ClientRecvTableMergeSourceKind(sourceState),
				propIndex,
				reinterpret_cast<void*>(recvProp),
				propName,
				ClientSendPropType(recvProp),
				ClientSendPropNumBits(recvProp),
				ClientSendPropFlags(recvProp),
				ClientSendPropOffset(recvProp),
				beforeValueBit,
				afterValueBit,
				payloadBits,
				payloadValue,
				payloadOk ? 1 : 0,
				static_cast<long long>(result),
				s_ClientCellDecodeTraceLogBudget);
			OutputDebugStringA(buffer);
			Warning("%s", buffer);
		}

		return result;
	}

	if (!ClientIsDeltaTraceTable(s_ClientRecvTableMergeCurrentName))
		return s_ClientRecvTableMergePropOriginal
			? s_ClientRecvTableMergePropOriginal(recvProp, propIndex, sourceState, outState)
			: 0;

	if (s_ClientRecvTableMergeLogBudget > 0) {
		--s_ClientRecvTableMergeLogBudget;
		const __int64 decoder = ClientDecoderForRecvTable(s_ClientRecvTableMergeCurrentTable);
		const int flattenedPropCount = ClientRecvDecoderFlattenedPropCount(decoder);
		__int64 recvVectorData = 0;
		__int64 sendVectorData = 0;
		__try {
			sendVectorData = decoder ? *reinterpret_cast<__int64*>(decoder + 96) : 0;
			recvVectorData = decoder ? *reinterpret_cast<__int64*>(decoder + 512) : 0;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			recvVectorData = 0;
			sendVectorData = 0;
		}
		const bool propInRange = flattenedPropCount >= 0 && propIndex < static_cast<unsigned int>(flattenedPropCount);
		const __int64 sendProp = propInRange ? ClientRecvDecoderSendProp(decoder, propIndex) : 0;
		char sourceBits[512];
		ClientDescribeBitRead(sourceBits, sizeof(sourceBits), sourceState);
		char buffer[1024];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: client RecvTable_MergeDeltas invalid prop table=%p name=\"%s\" propIndex=%u propCount=%d decoder=%p flatCount=%d recvVec=%p sendVec=%p propInRange=%d argProp=%p decoderProp=%p decoderName=\"%s\" decoderType=%d decoderFlags=0x%x sourceKind=%s source=%s out=%p return=%p original=%p budget=%d\n",
			reinterpret_cast<void*>(s_ClientRecvTableMergeCurrentTable),
			s_ClientRecvTableMergeCurrentName ? s_ClientRecvTableMergeCurrentName : "<unknown>",
			propIndex,
			ClientRecvTablePropCount(s_ClientRecvTableMergeCurrentTable),
			reinterpret_cast<void*>(decoder),
			flattenedPropCount,
			reinterpret_cast<void*>(recvVectorData),
			reinterpret_cast<void*>(sendVectorData),
			propInRange ? 1 : 0,
			reinterpret_cast<void*>(recvProp),
			reinterpret_cast<void*>(sendProp),
			propInRange ? ClientSendPropName(sendProp) : "<out-of-range>",
			propInRange ? ClientSendPropType(sendProp) : -1,
			propInRange ? ClientSendPropFlags(sendProp) : -1,
			ClientRecvTableMergeSourceKind(sourceState),
			sourceBits,
			reinterpret_cast<void*>(outState),
			_ReturnAddress(),
			reinterpret_cast<void*>(s_ClientRecvTableMergePropOriginal),
			s_ClientRecvTableMergeLogBudget);
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}

	return s_ClientRecvTableMergePropOriginal
		? s_ClientRecvTableMergePropOriginal(recvProp, propIndex, sourceState, outState)
		: 0;
}

static __int64 __fastcall ClientDeltaPropIndexRead(__int64* reader)
{
	const bool trace = s_ClientDeltaPropIndexReadLogBudget > 0
		&& s_ClientRecvTableMergeCurrentName
		&& ClientIsDeltaTraceTable(s_ClientRecvTableMergeCurrentName)
		&& reader
		&& (reader[0] == s_ClientRecvTableMergeCurrentOldState
			|| reader[0] == s_ClientRecvTableMergeCurrentNewState
			|| reader[0] == s_ClientRecvTableMergeCurrentOutState);
	char beforeBits[512] = {};
	const int beforeBit = trace ? ClientCBitReadTell(reader[0]) : -1;
	const int previousIndex = trace ? *reinterpret_cast<int*>(reinterpret_cast<char*>(reader) + 12) : -1;
	if (trace)
		ClientDescribeBitRead(beforeBits, sizeof(beforeBits), reader[0]);

	const __int64 result = s_ClientDeltaPropIndexReadOriginal
		? s_ClientDeltaPropIndexReadOriginal(reader)
		: 0x7fffffff;

	if (trace) {
		--s_ClientDeltaPropIndexReadLogBudget;
		const int afterBit = ClientCBitReadTell(reader[0]);
		char afterBits[512];
		ClientDescribeBitRead(afterBits, sizeof(afterBits), reader[0]);
		char wireBits[128];
		ClientDumpBitRange(wireBits, sizeof(wireBits), reader[0], beforeBit, afterBit);
		const __int64 decoder = ClientDecoderForRecvTable(s_ClientRecvTableMergeCurrentTable);
		const int flatCount = ClientRecvDecoderFlattenedPropCount(decoder);
		const __int64 prop = result >= 0 && result < flatCount
			? ClientRecvDecoderSendProp(decoder, static_cast<unsigned int>(result))
			: 0;
		const bool outOfRange = result != 0x7fffffff && (result < 0 || result >= flatCount);
		const int oldTell = ClientCBitReadTell(s_ClientRecvTableMergeCurrentOldState);
		const int newTell = ClientCBitReadTell(s_ClientRecvTableMergeCurrentNewState);
		const int outTell = ClientCBitReadTell(s_ClientRecvTableMergeCurrentOutState);
		char buffer[1792];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: client delta prop-read table=\"%s\" ent=%d a6=%d sourceKind=%s reader=%p result=%lld prev=%d flatCount=%d outOfRange=%d prop=%p name=\"%s\" type=%d numBits=%d flags=0x%x offset=%d bits=%d->%d oldTell=%d newTell=%d outTell=%d wire=\"%s\" before={%s} after={%s} budget=%d\n",
			s_ClientRecvTableMergeCurrentName,
			s_ClientRecvTableMergeCurrentEntity,
			s_ClientRecvTableMergeCurrentA6,
			ClientRecvTableMergeSourceKind(reader[0]),
			reinterpret_cast<void*>(reader),
			static_cast<long long>(result),
			previousIndex,
			flatCount,
			outOfRange ? 1 : 0,
			reinterpret_cast<void*>(prop),
			prop ? ClientSendPropName(prop) : (result == 0x7fffffff ? "<sentinel>" : "<out-of-range>"),
			prop ? ClientSendPropType(prop) : -1,
			prop ? ClientSendPropNumBits(prop) : -1,
			prop ? ClientSendPropFlags(prop) : -1,
			prop ? ClientSendPropOffset(prop) : -1,
			beforeBit,
			afterBit,
			oldTell,
			newTell,
			outTell,
			wireBits,
			beforeBits,
			afterBits,
			s_ClientDeltaPropIndexReadLogBudget);
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}
	return result;
}

static __int64 __fastcall ClientRecvTableMergeDeltas(__int64 recvTable, __int64 oldState, __int64 newState, __int64 outState, int a5, int a6, signed int* outProps)
{
	const __int64 previousTable = s_ClientRecvTableMergeCurrentTable;
	const char* previousName = s_ClientRecvTableMergeCurrentName;
	const __int64 previousOldState = s_ClientRecvTableMergeCurrentOldState;
	const __int64 previousNewState = s_ClientRecvTableMergeCurrentNewState;
	const __int64 previousOutState = s_ClientRecvTableMergeCurrentOutState;
	const int previousEntity = s_ClientRecvTableMergeCurrentEntity;
	const int previousA6 = s_ClientRecvTableMergeCurrentA6;
	s_ClientRecvTableMergeCurrentTable = recvTable;
	s_ClientRecvTableMergeCurrentName = ClientRecvTableName(recvTable);
	s_ClientRecvTableMergeCurrentOldState = oldState;
	s_ClientRecvTableMergeCurrentNewState = newState;
	s_ClientRecvTableMergeCurrentOutState = outState;
	s_ClientRecvTableMergeCurrentEntity = a5;
	s_ClientRecvTableMergeCurrentA6 = a6;
	const __int64 decoder = ClientDecoderForRecvTable(recvTable);
	const int flattenedPropCount = ClientRecvDecoderFlattenedPropCount(decoder);
	const int flattenedSendPropCount = ClientRecvDecoderSendPropCount(decoder);
	const bool isWorldTable = s_ClientRecvTableMergeCurrentName
		&& strcmp(s_ClientRecvTableMergeCurrentName, "DT_World") == 0;
	const bool ignoreInvalidBaseline = false;
	const bool hasInvalidBaseline = false;

	if (s_ClientRecvTableMergeLogBudget > 0 && ClientIsDeltaTraceTable(s_ClientRecvTableMergeCurrentName)) {
		--s_ClientRecvTableMergeLogBudget;
		__int64 recvVectorData = 0;
		__int64 sendVectorData = 0;
		__try {
			sendVectorData = decoder ? *reinterpret_cast<__int64*>(decoder + 96) : 0;
			recvVectorData = decoder ? *reinterpret_cast<__int64*>(decoder + 512) : 0;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			recvVectorData = 0;
			sendVectorData = 0;
		}
		char oldBits[512];
		char newBits[512];
		char outBits[512];
		ClientDescribeBitRead(oldBits, sizeof(oldBits), oldState);
		ClientDescribeBitRead(newBits, sizeof(newBits), newState);
		ClientDescribeBitRead(outBits, sizeof(outBits), outState);

		char buffer[4096];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: client RecvTable_MergeDeltas enter table=%p name=\"%s\" propCount=%d decoder=%p recvFlatCount=%d sendFlatCount=%d recvVec=%p sendVec=%p old={%s} new={%s} out={%s} a5=%d a6=%d return=%p ignoreInvalidBaseline=%d hasInvalidBaseline=%d budget=%d\n",
			reinterpret_cast<void*>(recvTable),
			s_ClientRecvTableMergeCurrentName ? s_ClientRecvTableMergeCurrentName : "<unknown>",
			ClientRecvTablePropCount(recvTable),
			reinterpret_cast<void*>(decoder),
			flattenedPropCount,
			flattenedSendPropCount,
			reinterpret_cast<void*>(recvVectorData),
			reinterpret_cast<void*>(sendVectorData),
			oldBits,
			newBits,
			outBits,
			a5,
			a6,
			_ReturnAddress(),
			ignoreInvalidBaseline ? 1 : 0,
			hasInvalidBaseline ? 1 : 0,
			s_ClientRecvTableMergeLogBudget);
		OutputDebugStringA(buffer);
		Warning("%s", buffer);

	}

	__int64 result = 0;
	__try {
		result = s_ClientRecvTableMergeDeltasOriginal
			? s_ClientRecvTableMergeDeltasOriginal(recvTable, ignoreInvalidBaseline ? 0 : oldState, newState, outState, a5, a6, outProps)
			: 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		if (s_ClientRecvTableMergeExceptionLogBudget > 0) {
			--s_ClientRecvTableMergeExceptionLogBudget;
			char oldBits[512];
			char newBits[512];
			char outBits[512];
			ClientDescribeBitRead(oldBits, sizeof(oldBits), oldState);
			ClientDescribeBitRead(newBits, sizeof(newBits), newState);
			ClientDescribeBitRead(outBits, sizeof(outBits), outState);
			char buffer[2048];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: client RecvTable_MergeDeltas caught exception table=\"%s\" ent=%d a6=%d old={%s} new={%s} out={%s} outProps=%p original=%p budget=%d\n",
				s_ClientRecvTableMergeCurrentName ? s_ClientRecvTableMergeCurrentName : "<unknown>",
				a5,
				a6,
				oldBits,
				newBits,
				outBits,
				reinterpret_cast<void*>(outProps),
				reinterpret_cast<void*>(s_ClientRecvTableMergeDeltasOriginal),
				s_ClientRecvTableMergeExceptionLogBudget);
			OutputDebugStringA(buffer);
			Warning("%s", buffer);
		}
		result = 0;
	}

	s_ClientRecvTableMergeCurrentTable = previousTable;
	s_ClientRecvTableMergeCurrentName = previousName;
	s_ClientRecvTableMergeCurrentOldState = previousOldState;
	s_ClientRecvTableMergeCurrentNewState = previousNewState;
	s_ClientRecvTableMergeCurrentOutState = previousOutState;
	s_ClientRecvTableMergeCurrentEntity = previousEntity;
	s_ClientRecvTableMergeCurrentA6 = previousA6;
	return result;
}

static void LogClientSVCServerInfoFields(const char* phase, __int64 message, __int64 bitBuffer, bool result, int startBitsLeft, int endBitsLeft, int startBit, int endBit)
{
	if (s_ClientSVCServerInfoReadLogBudget <= 0)
		return;

	int protocol = -1;
	int serverCount = -1;
	int unknownLong = -1;
	int mapCrc = -1;
	int clientCrc = -1;
	int maxClients = -1;
	int maxClasses = -1;
	int playerSlot = -1;
	float tickInterval = -1.0f;
	char os = '?';
	const char* gameDir = nullptr;
	const char* mapName = nullptr;
	const char* skyName = nullptr;
	const char* hostName = nullptr;
	const char* loadingUrl = nullptr;
	const char* extraString = nullptr;
	int dataBits = -1;
	bool overflow = false;

	__try {
		protocol = *reinterpret_cast<int*>(message + 32);
		serverCount = *reinterpret_cast<int*>(message + 36);
		unknownLong = *reinterpret_cast<int*>(message + 44);
		mapCrc = *reinterpret_cast<int*>(message + 48);
		clientCrc = *reinterpret_cast<int*>(message + 52);
		maxClients = *reinterpret_cast<int*>(message + 56);
		maxClasses = *reinterpret_cast<int*>(message + 60);
		playerSlot = *reinterpret_cast<int*>(message + 64);
		tickInterval = *reinterpret_cast<float*>(message + 68);
		os = *reinterpret_cast<char*>(message + 43);
		gameDir = *reinterpret_cast<const char**>(message + 72);
		mapName = *reinterpret_cast<const char**>(message + 80);
		skyName = *reinterpret_cast<const char**>(message + 88);
		hostName = *reinterpret_cast<const char**>(message + 96);
		loadingUrl = *reinterpret_cast<const char**>(message + 104);
		extraString = *reinterpret_cast<const char**>(message + 112);
		if (bitBuffer) {
			dataBits = *reinterpret_cast<int*>(bitBuffer + 16);
			overflow = *reinterpret_cast<unsigned char*>(bitBuffer + 8) != 0;
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		protocol = -2;
	}

	char gameDirBuffer[256];
	char mapNameBuffer[256];
	char skyNameBuffer[256];
	char hostNameBuffer[256];
	char loadingUrlBuffer[256];
	char extraStringBuffer[256];
	char startBytes[128];
	ClientDumpBitWindow(startBytes, sizeof(startBytes), bitBuffer, startBit);
	char buffer[1536];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: client SVC_ServerInfo read %s msg=%p bitbuf=%p result=%d startBit=%d endBit=%d startBitsAvail=%d endBitsAvail=%d dataBits=%d overflow=%d bytes=[%s] protocol=%d serverCount=%d unknown=%d mapCrc=%d clientCrc=%d max=%d maxClasses=%d slot=%d tick=%f os=%c game=\"%s\" map=\"%s\" sky=\"%s\" host=\"%s\" loading=\"%s\" extra=\"%s\" budget=%d\n",
		phase ? phase : "<null>",
		reinterpret_cast<void*>(message),
		reinterpret_cast<void*>(bitBuffer),
		static_cast<int>(result),
		startBit,
		endBit,
		startBitsLeft,
		endBitsLeft,
		dataBits,
		static_cast<int>(overflow),
		startBytes,
		protocol,
		serverCount,
		unknownLong,
		mapCrc,
		clientCrc,
		maxClients,
		maxClasses,
		playerSlot,
		tickInterval,
		os ? os : '?',
		ClientSafeCString(gameDir, gameDirBuffer, sizeof(gameDirBuffer)),
		ClientSafeCString(mapName, mapNameBuffer, sizeof(mapNameBuffer)),
		ClientSafeCString(skyName, skyNameBuffer, sizeof(skyNameBuffer)),
		ClientSafeCString(hostName, hostNameBuffer, sizeof(hostNameBuffer)),
		ClientSafeCString(loadingUrl, loadingUrlBuffer, sizeof(loadingUrlBuffer)),
		ClientSafeCString(extraString, extraStringBuffer, sizeof(extraStringBuffer)),
		s_ClientSVCServerInfoReadLogBudget);
	OutputDebugStringA(buffer);
}

static bool __fastcall ClientSVCServerInfoReadFromBuffer(__int64 message, __int64 bitBuffer)
{
	const int startBitsLeft = bitBuffer ? *reinterpret_cast<int*>(bitBuffer + 36) : -1;
	const int startBit = ClientCBitReadTell(bitBuffer);
	const bool result = s_ClientSVCServerInfoReadOriginal
		? s_ClientSVCServerInfoReadOriginal(message, bitBuffer)
		: false;
	const int endBitsLeft = bitBuffer ? *reinterpret_cast<int*>(bitBuffer + 36) : -1;
	const int endBit = ClientCBitReadTell(bitBuffer);

	LogClientSVCServerInfoFields("leave", message, bitBuffer, result, startBitsLeft, endBitsLeft, startBit, endBit);
	if (s_ClientSVCServerInfoReadLogBudget > 0)
		--s_ClientSVCServerInfoReadLogBudget;

	return result;
}

static int ClientMessageIntField(__int64 message, size_t offset, int fallback = -1)
{
	if (!message)
		return fallback;

	__try {
		return *reinterpret_cast<int*>(message + offset);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return fallback;
	}
}

static unsigned char ClientMessageByteField(__int64 message, size_t offset, unsigned char fallback = 0xff)
{
	if (!message)
		return fallback;

	__try {
		return *reinterpret_cast<unsigned char*>(message + offset);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return fallback;
	}
}

static void ClientLogSVCSnapshotRead(
	const char* phase,
	__int64 message,
	__int64 bitBuffer,
	const char* tableName,
	int startBit,
	int endBit,
	int startBitsLeft,
	int endBitsLeft,
	bool result)
{
	if (s_ClientSnapshotReadLogBudget <= 0 || !tableName || strcmp_static(tableName, "SVC_Snapshot"))
		return;

	--s_ClientSnapshotReadLogBudget;
	char bytes[128];
	ClientDumpBitWindow(bytes, sizeof(bytes), bitBuffer, startBit);
	char buffer[1024];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: client SVC_Snapshot read %s msg=%p bitbuf=%p bits=%d->%d bitsLeft=%d->%d result=%d fields i32={32:%d 36:%d 44:%d 48:%d 52:%d 56:%d 60:%d 68:%d 72:%d} bytes={40:%u 41:%u 42:%u 43:%u 64:%u} window=[%s] budget=%d\n",
		phase ? phase : "?",
		reinterpret_cast<void*>(message),
		reinterpret_cast<void*>(bitBuffer),
		startBit,
		endBit,
		startBitsLeft,
		endBitsLeft,
		static_cast<int>(result),
		ClientMessageIntField(message, 32),
		ClientMessageIntField(message, 36),
		ClientMessageIntField(message, 44),
		ClientMessageIntField(message, 48),
		ClientMessageIntField(message, 52),
		ClientMessageIntField(message, 56),
		ClientMessageIntField(message, 60),
		ClientMessageIntField(message, 68),
		ClientMessageIntField(message, 72),
		static_cast<unsigned int>(ClientMessageByteField(message, 40)),
		static_cast<unsigned int>(ClientMessageByteField(message, 41)),
		static_cast<unsigned int>(ClientMessageByteField(message, 42)),
		static_cast<unsigned int>(ClientMessageByteField(message, 43)),
		static_cast<unsigned int>(ClientMessageByteField(message, 64)),
		bytes,
		s_ClientSnapshotReadLogBudget);
	OutputDebugStringA(buffer);
	Warning("%s", buffer);
}
static bool __fastcall ClientNetMessageWriteToBuffer(__int64 message, __int64 bitBuffer)
{
	const int index = ClientNetMessageIndex(message);
	NetMessageWriteToBufferType original = index >= 0 ? s_ClientNetMessageWriteToBufferOriginal[index] : nullptr;
	const int startBit = bitBuffer ? *reinterpret_cast<int*>(bitBuffer + 16) : -1;
	const bool result = original ? original(message, bitBuffer) : false;
	const int endBit = bitBuffer ? *reinterpret_cast<int*>(bitBuffer + 16) : -1;
	const int dataBits = bitBuffer ? *reinterpret_cast<int*>(bitBuffer + 12) : -1;

	if (s_ClientNetMessageWriteLogBudget > 0) {
		--s_ClientNetMessageWriteLogBudget;
		const bool stringCmd = index >= 0 && !_stricmp(netMessages[index].name, "NET_StringCmd");
		char buffer[768];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: client netmsg write msg=%p bitbuf=%p tableName=%s getName=%s type=%d localState=%d startBit=%d endBit=%d dataBits=%d result=%d%s%s%s budget=%d\n",
			reinterpret_cast<void*>(message),
			reinterpret_cast<void*>(bitBuffer),
			index >= 0 ? netMessages[index].name : "<unknown-vtable>",
			ClientNetMessageName(message),
			ClientNetMessageIntVFunc(message, 8),
			ClientLocalSignonState(),
			startBit,
			endBit,
			dataBits,
			static_cast<int>(result),
			stringCmd ? " stringCmd=\"" : "",
			stringCmd ? ClientNetStringCommandText(message) : "",
			stringCmd ? "\"" : "",
			s_ClientNetMessageWriteLogBudget);
		OutputDebugStringA(buffer);
	}

	if (!result) {
		char buffer[768];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: CLIENT NETMESSAGE WRITE FAILED msg=%p bitbuf=%p tableName=%s getName=%s type=%d startBit=%d endBit=%d dataBits=%d original=%p\n",
			reinterpret_cast<void*>(message),
			reinterpret_cast<void*>(bitBuffer),
			index >= 0 ? netMessages[index].name : "<unknown-vtable>",
			ClientNetMessageName(message),
			ClientNetMessageIntVFunc(message, 8),
			startBit,
			endBit,
			dataBits,
			reinterpret_cast<void*>(original));
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}

	return result;
}

static bool __fastcall ClientNetMessageReadFromBuffer(__int64 message, __int64 bitBuffer)
{
	const int index = ClientNetMessageIndex(message);
	NetMessageReadFromBufferType original = index >= 0 ? s_ClientNetMessageReadFromBufferOriginal[index] : nullptr;
	const char* tableName = index >= 0 ? netMessages[index].name : "<unknown-vtable>";
	const int startBit = bitBuffer ? *reinterpret_cast<int*>(bitBuffer + 36) : -1;
	const int startTell = ClientCBitReadTell(bitBuffer);
	const bool result = original ? original(message, bitBuffer) : false;
	const int endBit = bitBuffer ? *reinterpret_cast<int*>(bitBuffer + 36) : -1;
	const int endTell = ClientCBitReadTell(bitBuffer);
	const int dataBits = bitBuffer ? *reinterpret_cast<int*>(bitBuffer + 16) : -1;
	ClientLogSVCSnapshotRead("leave", message, bitBuffer, tableName, startTell, endTell, startBit, endBit, result);

	const char* readName = ClientNetMessageName(message);
	const bool forceReadLog = readName && (!_stricmp(readName, "svc_PlaylistOverrides") || !_stricmp(readName, "svc_UpdateStringTable"));
	if (forceReadLog || s_ClientNetMessageReadLogBudget > 0) {
		if (!forceReadLog)
			--s_ClientNetMessageReadLogBudget;
		char startBytes[128];
		ClientDumpBitWindow(startBytes, sizeof(startBytes), bitBuffer, startTell);
		char buffer[512];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: client netmsg read msg=%p bitbuf=%p tableName=%s getName=%s type=%d startBit=%d endBit=%d startBitsLeft=%d endBitsLeft=%d dataBits=%d result=%d bytes=[%s] budget=%d\n",
			reinterpret_cast<void*>(message),
			reinterpret_cast<void*>(bitBuffer),
			tableName,
			ClientNetMessageName(message),
			ClientNetMessageIntVFunc(message, 8),
			startTell,
			endTell,
			startBit,
			endBit,
			dataBits,
			static_cast<int>(result),
			startBytes,
			s_ClientNetMessageReadLogBudget);
		OutputDebugStringA(buffer);
	}

	if (!result) {
		char startBytes[128];
		ClientDumpBitWindow(startBytes, sizeof(startBytes), bitBuffer, startTell);
		char buffer[768];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: CLIENT NETMESSAGE READ FAILED msg=%p bitbuf=%p tableName=%s getName=%s type=%d startBit=%d endBit=%d startBitsLeft=%d endBitsLeft=%d dataBits=%d overflow=%d bytes=[%s] original=%p\n",
			reinterpret_cast<void*>(message),
			reinterpret_cast<void*>(bitBuffer),
			tableName,
			ClientNetMessageName(message),
			ClientNetMessageIntVFunc(message, 8),
			startTell,
			endTell,
			startBit,
			endBit,
			dataBits,
			bitBuffer ? static_cast<int>(*reinterpret_cast<unsigned char*>(bitBuffer + 8) != 0) : -1,
			startBytes,
			reinterpret_cast<void*>(original));
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}

	return result;
}

static int ClientLocalSignonState()
{
	if (!G_engine)
		return -1;

	__try {
		const auto cl = reinterpret_cast<unsigned char*>(G_engine + 0x797070);
		return *reinterpret_cast<int*>(cl + 29 * sizeof(int));
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return -2;
	}
}

static void ClientAppendDisconnectDiagnostic(const char* text)
{
	if (!text || !*text)
		return;

	HANDLE file = CreateFileA(
		"r1delta_com_disconnect.log",
		FILE_APPEND_DATA,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr,
		OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return;

	SYSTEMTIME time;
	GetLocalTime(&time);
	char line[1024];
	const int lineLength = _snprintf_s(
		line,
		sizeof(line),
		_TRUNCATE,
		"%04u-%02u-%02u %02u:%02u:%02u.%03u %s",
		time.wYear,
		time.wMonth,
		time.wDay,
		time.wHour,
		time.wMinute,
		time.wSecond,
		time.wMilliseconds,
		text);
	if (lineLength > 0) {
		DWORD written = 0;
		WriteFile(file, line, static_cast<DWORD>(lineLength), &written, nullptr);
	}

	CloseHandle(file);
}

static __int64 COMExplainDisconnectionHook(__int64 eventKind, const char* format, ...)
{
	char reason[1024] = {};
	va_list args;
	va_start(args, format);
	vsnprintf_s(reason, sizeof(reason), _TRUNCATE, format ? format : "<null-format>", args);
	va_end(args);

	char buffer[768];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: COM_ExplainDisconnection event=%lld ret=%p localSignon=%d reason=\"%s\" format=\"%s\" original=%p\n",
		static_cast<long long>(eventKind),
		_ReturnAddress(),
		ClientLocalSignonState(),
		reason,
		format ? format : "<null>",
		reinterpret_cast<void*>(s_COMExplainDisconnectionOriginal));
	OutputDebugStringA(buffer);
	ClientAppendDisconnectDiagnostic(buffer);

	return s_COMExplainDisconnectionOriginal
		? s_COMExplainDisconnectionOriginal(eventKind, "%s", reason)
		: 0;
}

static void InstallCOMExplainDisconnectionHook(uintptr_t engineBase)
{
	if (!ShouldInstallR1OClientDebugHooks() || s_COMExplainDisconnectionHookInstalled || !engineBase || IsDedicatedServer())
		return;

	void* target = reinterpret_cast<void*>(engineBase + 0x117240);
	MH_STATUS status = MH_CreateHook(
		target,
		&COMExplainDisconnectionHook,
		reinterpret_cast<LPVOID*>(&s_COMExplainDisconnectionOriginal));

	s_COMExplainDisconnectionHookInstalled = status == MH_OK || status == MH_ERROR_ALREADY_CREATED;
	char buffer[256];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: COM_ExplainDisconnection hook status=%d target=%p original=%p\n",
		static_cast<int>(status),
		target,
		reinterpret_cast<void*>(s_COMExplainDisconnectionOriginal));
	OutputDebugStringA(buffer);
}

static const char* ClientSignonStateBufferString(__int64 message, ptrdiff_t bufferOffset, ptrdiff_t lengthOffset, char* out, size_t outSize)
{
	if (!out || outSize == 0)
		return "<bad-out>";

	out[0] = '\0';
	if (!message)
		return "";

	__try {
		const int length = *reinterpret_cast<int*>(message + lengthOffset);
		const char* data = *reinterpret_cast<const char**>(message + bufferOffset);
		if (length <= 0 || !data)
			return "";

		const size_t copyLen = static_cast<size_t>(length) < outSize - 1
			? static_cast<size_t>(length)
			: outSize - 1;
		memcpy(out, data, copyLen);
		out[copyLen] = '\0';
		return out;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		strncpy_s(out, outSize, "<av>", _TRUNCATE);
		return out;
	}
}

static bool __fastcall ClientNetMessageProcess(__int64 message)
{
	const int index = ClientNetMessageIndex(message);
	NetMessageProcessType original = index >= 0 ? s_ClientNetMessageProcessOriginal[index] : nullptr;
	const char* tableName = index >= 0 ? netMessages[index].name : "<unknown-vtable>";
	const char* getName = ClientNetMessageName(message);
	const bool signonState = getName && !_stricmp(getName, "net_SignonState");
	const bool classInfo = getName && !_stricmp(getName, "svc_ClassInfo");
	const bool sendTable = getName && !_stricmp(getName, "svc_SendTable");
	const int localStateBefore = signonState ? ClientLocalSignonState() : -1;

	int preFields[24] = {};
	unsigned char preBytes[64] = {};
	if ((classInfo || sendTable) && s_ClientClassInfoProcessLogBudget > 0) {
		__try {
			for (int i = 0; i < 24; ++i)
				preFields[i] = *reinterpret_cast<int*>(message + 32 + sizeof(int) * i);
			memcpy(preBytes, reinterpret_cast<const void*>(message + 32), sizeof(preBytes));
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			preFields[0] = -0x7ffffffe;
		}
	}

	int messageState = -1;
	int spawnCount = -1;
	int serverCount = -1;
	int mapLength = -1;
	int modeLength = -1;
	int loading = -1;
	int token = -1;
	int extra = -1;
	char mapName[64] = {};
	char modeName[64] = {};
	char levelName[64] = {};
	char parentMap[160] = {};

	const bool playlistOverrides = getName && !_stricmp(getName, "svc_PlaylistOverrides");
	if (playlistOverrides) {
		char payloadBytes[128] = {};
		int bitCount = -1;
		int copiedTell = -1;
		int copiedLeft = -1;
		__try {
			bitCount = *reinterpret_cast<int*>(message + 32);
			__int64 copiedBits = message + 40;
			copiedTell = ClientCBitReadTell(copiedBits);
			copiedLeft = *reinterpret_cast<int*>(copiedBits + 36);
			ClientDumpBitWindow(payloadBytes, sizeof(payloadBytes), copiedBits, copiedTell);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			strncpy_s(payloadBytes, "<av>", _TRUNCATE);
		}
		char buffer[512];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: client svc_PlaylistOverrides process-enter msg=%p bitCount=%d copiedTell=%d copiedBitsLeft=%d payload=[%s] localState=%d original=%p\n",
			reinterpret_cast<void*>(message),
			bitCount,
			copiedTell,
			copiedLeft,
			payloadBytes,
			ClientLocalSignonState(),
			reinterpret_cast<void*>(original));
		OutputDebugStringA(buffer);
	}

	if (signonState) {
		__try {
			messageState = *reinterpret_cast<int*>(message + 32);
			spawnCount = *reinterpret_cast<int*>(message + 36);
			serverCount = *reinterpret_cast<int*>(message + 40);
			mapLength = *reinterpret_cast<int*>(message + 104);
			modeLength = *reinterpret_cast<int*>(message + 136);
			loading = *reinterpret_cast<unsigned char*>(message + 180);
			token = *reinterpret_cast<int*>(message + 176);
			extra = *reinterpret_cast<int*>(message + 184);
			strncpy_s(levelName, reinterpret_cast<const char*>(message + 144), _TRUNCATE);
			strncpy_s(parentMap, reinterpret_cast<const char*>(message + 188), _TRUNCATE);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			messageState = -2;
		}

		ClientSignonStateBufferString(message, 80, 104, mapName, sizeof(mapName));
		ClientSignonStateBufferString(message, 112, 136, modeName, sizeof(modeName));
	}

	bool result = false;
	__try {
		result = original ? original(message) : false;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		if (s_ClientNetMessageProcessLogBudget > 0) {
			--s_ClientNetMessageProcessLogBudget;
			char buffer[1024];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: client netmsg process caught exception msg=%p tableName=%s getName=%s type=%d original=%p budget=%d\n",
				reinterpret_cast<void*>(message),
				tableName,
				getName ? getName : "<null>",
				ClientNetMessageIntVFunc(message, 8),
				reinterpret_cast<void*>(original),
				s_ClientNetMessageProcessLogBudget);
			OutputDebugStringA(buffer);
			Warning("%s", buffer);
		}
		result = false;
	}
	const int localStateAfter = signonState ? ClientLocalSignonState() : -1;

	if ((classInfo || sendTable) && s_ClientClassInfoProcessLogBudget > 0) {
		--s_ClientClassInfoProcessLogBudget;
		int postFields[24] = {};
		__try {
			for (int i = 0; i < 24; ++i)
				postFields[i] = *reinterpret_cast<int*>(message + 32 + sizeof(int) * i);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			postFields[0] = -0x7ffffffe;
		}

		char byteDump[256] = {};
		size_t used = 0;
		for (int i = 0; i < 32 && used + 4 < sizeof(byteDump); ++i) {
			used += static_cast<size_t>(_snprintf_s(
				byteDump + used,
				sizeof(byteDump) - used,
				_TRUNCATE,
				"%02X%s",
				static_cast<unsigned int>(preBytes[i]),
				i + 1 < 32 ? " " : ""));
		}

		char buffer[1600];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: client netmsg process class/send msg=%p tableName=%s getName=%s type=%d result=%d pre=[%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d] post=[%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d] bytes=[%s] budget=%d\n",
			reinterpret_cast<void*>(message),
			tableName,
			getName ? getName : "<null>",
			ClientNetMessageIntVFunc(message, 8),
			static_cast<int>(result),
			preFields[0], preFields[1], preFields[2], preFields[3],
			preFields[4], preFields[5], preFields[6], preFields[7],
			preFields[8], preFields[9], preFields[10], preFields[11],
			postFields[0], postFields[1], postFields[2], postFields[3],
			postFields[4], postFields[5], postFields[6], postFields[7],
			postFields[8], postFields[9], postFields[10], postFields[11],
			byteDump,
			s_ClientClassInfoProcessLogBudget);
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}

	if ((signonState || (s_ClientNetMessageProcessLogBudget > 0 && getName && (!_stricmp(getName, "svc_CreateStringTable") || !_stricmp(getName, "svc_SetTeam"))))
		&& s_ClientNetMessageProcessLogBudget > 0) {
		--s_ClientNetMessageProcessLogBudget;
		char buffer[1024];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: client netmsg process msg=%p tableName=%s getName=%s type=%d result=%d localState=%d->%d signonState=%d spawn=%d serverCount=%d mapLen=%d map=\"%s\" modeLen=%d mode=\"%s\" loading=%d level=\"%s\" token=%d parent=\"%s\" extra=%d budget=%d\n",
			reinterpret_cast<void*>(message),
			tableName,
			getName ? getName : "<null>",
			ClientNetMessageIntVFunc(message, 8),
			static_cast<int>(result),
			localStateBefore,
			localStateAfter,
			messageState,
			spawnCount,
			serverCount,
			mapLength,
			mapName,
			modeLength,
			modeName,
			loading,
			levelName,
			token,
			parentMap,
			extra,
			s_ClientNetMessageProcessLogBudget);
		OutputDebugStringA(buffer);
	}

	if (!result) {
		char buffer[1024];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: CLIENT NETMESSAGE PROCESS FAILED msg=%p tableName=%s getName=%s type=%d localState=%d->%d signonState=%d spawn=%d serverCount=%d mapLen=%d map=\"%s\" modeLen=%d mode=\"%s\" loading=%d level=\"%s\" token=%d parent=\"%s\" extra=%d original=%p\n",
			reinterpret_cast<void*>(message),
			tableName,
			getName ? getName : "<null>",
			ClientNetMessageIntVFunc(message, 8),
			localStateBefore,
			localStateAfter,
			messageState,
			spawnCount,
			serverCount,
			mapLength,
			mapName,
			modeLength,
			modeName,
			loading,
			levelName,
			token,
			parentMap,
			extra,
			reinterpret_cast<void*>(original));
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}

	return result;
}

static bool __fastcall ClientSVCServerInfoProcess(__int64 message)
{
	void* handler = nullptr;
	void* handlerProcess = nullptr;
	int protocol = -1;
	int expectedProtocol = -1;
	int maxClients = -1;
	int maxClasses = -1;
	float tickInterval = -1.0f;
	float expectedTickInterval = -1.0f;
	int hostState = -1;
	const char* gameDir = nullptr;
	const char* mapName = nullptr;
	const char* currentGameDir = nullptr;
	bool gameDirOk = false;
	bool mapPathOk = false;
	char mapPath[64] = {};
	__try {
		handler = message ? *reinterpret_cast<void**>(message + 0x18) : nullptr;
		if (handler) {
			void** handlerVtable = *reinterpret_cast<void***>(handler);
			handlerProcess = handlerVtable ? handlerVtable[6] : nullptr;
		}
		if (message) {
			protocol = *reinterpret_cast<int*>(message + 32);
			maxClients = *reinterpret_cast<int*>(message + 56);
			maxClasses = *reinterpret_cast<int*>(message + 60);
			tickInterval = *reinterpret_cast<float*>(message + 68);
			gameDir = *reinterpret_cast<const char**>(message + 72);
			mapName = *reinterpret_cast<const char**>(message + 80);
		}
		if (G_engine) {
			expectedProtocol = *reinterpret_cast<int*>(G_engine + 0x30EB448);
			hostState = *reinterpret_cast<int*>(G_engine + 0x2966168);
			expectedTickInterval = *reinterpret_cast<float*>(G_engine + 0x7C0B08);
			currentGameDir = reinterpret_cast<const char*>(G_engine + 0x2EBD5F0);
		}
		if (!gameDir || !gameDir[0]) {
			gameDirOk = true;
		}
		else if (ClientIsReadableCString(currentGameDir) && _stricmp(currentGameDir, gameDir) == 0) {
			gameDirOk = true;
		}
		else if ((!_stricmp(gameDir, "portal2") || !_stricmp(gameDir, "portal2_sixense"))
			&& ClientIsReadableCString(currentGameDir)
			&& (!_stricmp(currentGameDir, "portal2") || !_stricmp(currentGameDir, "portal2_sixense"))) {
			gameDirOk = true;
		}
		if (ClientIsReadableCString(mapName)) {
			const int mapPathResult = _snprintf_s(mapPath, sizeof(mapPath), _TRUNCATE, "maps/%s%s.bsp", mapName, "");
			mapPathOk = mapPathResult >= 0 && mapPathResult < static_cast<int>(sizeof(mapPath));
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		handler = reinterpret_cast<void*>(static_cast<uintptr_t>(-1));
		handlerProcess = reinterpret_cast<void*>(static_cast<uintptr_t>(-1));
	}

	if (s_ClientSVCServerInfoProcessLogBudget > 0) {
		char gameDirBuffer[256];
		char mapNameBuffer[256];
		char currentGameDirBuffer[256];
		char buffer[1536];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: client SVC_ServerInfo process enter msg=%p handler=%p handlerProcess=%p protocol=%d expectedProtocol=%d max=%d maxClasses=%d tick=%f expectedTick=%f hostState=%d game=\"%s\" currentGame=\"%s\" gameOk=%d map=\"%s\" mapPath=\"%s\" mapPathOk=%d earlyPredicatesOk=%d budget=%d\n",
			reinterpret_cast<void*>(message),
			handler,
			handlerProcess,
			protocol,
			expectedProtocol,
			maxClients,
			maxClasses,
			tickInterval,
			expectedTickInterval,
			hostState,
			ClientSafeCString(gameDir, gameDirBuffer, sizeof(gameDirBuffer)),
			ClientSafeCString(currentGameDir, currentGameDirBuffer, sizeof(currentGameDirBuffer)),
			static_cast<int>(gameDirOk),
			ClientSafeCString(mapName, mapNameBuffer, sizeof(mapNameBuffer)),
			mapPath,
			static_cast<int>(mapPathOk),
			static_cast<int>(protocol == expectedProtocol
				&& maxClients >= 1
				&& maxClients <= 19
				&& maxClasses >= 1
				&& maxClasses <= 512
				&& tickInterval >= 0.001f
				&& tickInterval <= 0.1f
				&& gameDirOk
				&& mapPathOk
				&& (hostState != 3 || tickInterval == expectedTickInterval)),
			s_ClientSVCServerInfoProcessLogBudget);
		OutputDebugStringA(buffer);
	}

	const bool result = s_ClientSVCServerInfoProcessOriginal
		? s_ClientSVCServerInfoProcessOriginal(message)
		: false;

	if (s_ClientSVCServerInfoProcessLogBudget > 0) {
		--s_ClientSVCServerInfoProcessLogBudget;
		char buffer[512];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: client SVC_ServerInfo process leave msg=%p handler=%p handlerProcess=%p result=%d budget=%d\n",
			reinterpret_cast<void*>(message),
			handler,
			handlerProcess,
			static_cast<int>(result),
			s_ClientSVCServerInfoProcessLogBudget);
		OutputDebugStringA(buffer);
	}

	return result;
}

static void InstallClientNetMessageWriteHooks(uintptr_t engineBase)
{
	if (!ShouldInstallR1OClientDebugHooks() || s_ClientNetMessageWriteHooksInstalled || !engineBase || IsDedicatedServer())
		return;

	int patched = 0;
	for (size_t i = 0; i < sizeof(netMessages) / sizeof(netMessages[0]); ++i) {
		if (!netMessages[i].offset_engine)
			continue;

		uintptr_t* vtable = reinterpret_cast<uintptr_t*>(engineBase + netMessages[i].offset_engine);
		void* target = reinterpret_cast<void*>(vtable[5]);
		if (!target)
			continue;

		MH_STATUS status = MH_CreateHook(
			target,
			&ClientNetMessageWriteToBuffer,
			reinterpret_cast<LPVOID*>(&s_ClientNetMessageWriteToBufferOriginal[i]));
		if (status == MH_OK)
			++patched;
	}

	s_ClientNetMessageWriteHooksInstalled = patched > 0;
	char buffer[256];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: client netmessage WriteToBuffer hooks patched=%d total=%zu\n",
		patched,
		sizeof(netMessages) / sizeof(netMessages[0]));
	OutputDebugStringA(buffer);
}

static void InstallClientNetMessageReadHooks(uintptr_t engineBase)
{
	if (!ShouldInstallR1OClientDebugHooks() || s_ClientNetMessageReadHooksInstalled || !engineBase || IsDedicatedServer())
		return;

	int patched = 0;
	for (size_t i = 0; i < sizeof(netMessages) / sizeof(netMessages[0]); ++i) {
		if (!netMessages[i].offset_engine)
			continue;

		uintptr_t* vtable = reinterpret_cast<uintptr_t*>(engineBase + netMessages[i].offset_engine);
		void* target = reinterpret_cast<void*>(vtable[4]);
		if (!target)
			continue;

		MH_STATUS status = MH_CreateHook(
			target,
			&ClientNetMessageReadFromBuffer,
			reinterpret_cast<LPVOID*>(&s_ClientNetMessageReadFromBufferOriginal[i]));
		if (status == MH_OK)
			++patched;
	}

	s_ClientNetMessageReadHooksInstalled = patched > 0;
	char buffer[256];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: client netmessage ReadFromBuffer hooks patched=%d total=%zu\n",
		patched,
		sizeof(netMessages) / sizeof(netMessages[0]));
	OutputDebugStringA(buffer);
}

static void InstallClientNetMessageProcessHooks(uintptr_t engineBase)
{
	if (!ShouldInstallR1OClientDebugHooks() || s_ClientNetMessageProcessHooksInstalled || !engineBase || IsDedicatedServer())
		return;

	int patched = 0;
	for (size_t i = 0; i < sizeof(netMessages) / sizeof(netMessages[0]); ++i) {
		if (!netMessages[i].offset_engine || !strcmp_static(netMessages[i].name, "SVC_ServerInfo"))
			continue;

		uintptr_t* vtable = reinterpret_cast<uintptr_t*>(engineBase + netMessages[i].offset_engine);
		void* target = reinterpret_cast<void*>(vtable[3]);
		if (!target)
			continue;

		MH_STATUS status = MH_CreateHook(
			target,
			&ClientNetMessageProcess,
			reinterpret_cast<LPVOID*>(&s_ClientNetMessageProcessOriginal[i]));
		if (status == MH_OK)
			++patched;
	}

	s_ClientNetMessageProcessHooksInstalled = patched > 0;
	char buffer[256];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: client netmessage Process hooks patched=%d total=%zu\n",
		patched,
		sizeof(netMessages) / sizeof(netMessages[0]));
	OutputDebugStringA(buffer);
}

static void InstallClientSVCServerInfoProcessHook(uintptr_t engineBase)
{
	if (!ShouldInstallR1OClientDebugHooks() || s_ClientSVCServerInfoProcessHookInstalled || !engineBase || IsDedicatedServer())
		return;

	uintptr_t* vtable = nullptr;
	for (size_t i = 0; i < sizeof(netMessages) / sizeof(netMessages[0]); ++i) {
		if (!strcmp_static(netMessages[i].name, "SVC_ServerInfo")) {
			vtable = reinterpret_cast<uintptr_t*>(engineBase + netMessages[i].offset_engine);
			break;
		}
	}

	void* target = vtable ? reinterpret_cast<void*>(vtable[3]) : nullptr;
	MH_STATUS status = target
		? MH_CreateHook(target, &ClientSVCServerInfoProcess, reinterpret_cast<LPVOID*>(&s_ClientSVCServerInfoProcessOriginal))
		: MH_ERROR_FUNCTION_NOT_FOUND;

	s_ClientSVCServerInfoProcessHookInstalled = status == MH_OK || status == MH_ERROR_ALREADY_CREATED;
	char buffer[256];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: client SVC_ServerInfo process hook status=%d target=%p original=%p\n",
		static_cast<int>(status),
		target,
		reinterpret_cast<void*>(s_ClientSVCServerInfoProcessOriginal));
	OutputDebugStringA(buffer);
}

static void InstallClientSVCServerInfoReadHook(uintptr_t engineBase)
{
	if (!ShouldInstallR1OClientDebugHooks() || s_ClientSVCServerInfoReadHookInstalled || !engineBase || IsDedicatedServer())
		return;

	uintptr_t* vtable = nullptr;
	for (size_t i = 0; i < sizeof(netMessages) / sizeof(netMessages[0]); ++i) {
		if (!strcmp_static(netMessages[i].name, "SVC_ServerInfo")) {
			vtable = reinterpret_cast<uintptr_t*>(engineBase + netMessages[i].offset_engine);
			break;
		}
	}

	void* target = vtable ? reinterpret_cast<void*>(vtable[4]) : nullptr;
	MH_STATUS status = target
		? MH_CreateHook(target, &ClientSVCServerInfoReadFromBuffer, reinterpret_cast<LPVOID*>(&s_ClientSVCServerInfoReadOriginal))
		: MH_ERROR_FUNCTION_NOT_FOUND;

	s_ClientSVCServerInfoReadHookInstalled = status == MH_OK || status == MH_ERROR_ALREADY_CREATED;
	char buffer[256];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: client SVC_ServerInfo read hook status=%d target=%p original=%p\n",
		static_cast<int>(status),
		target,
		reinterpret_cast<void*>(s_ClientSVCServerInfoReadOriginal));
	OutputDebugStringA(buffer);
}

static void InstallClientRecvTableMergeHooks(uintptr_t engineBase)
{
	if (!ShouldInstallR1OClientDebugHooks() || s_ClientRecvTableMergeHooksInstalled || !engineBase || IsDedicatedServer())
		return;

	void* mergeTarget = reinterpret_cast<void*>(engineBase + 0x1D7520);
	void* propTarget = reinterpret_cast<void*>(engineBase + 0x1D5DE0);
	void* propIndexReadTarget = reinterpret_cast<void*>(engineBase + 0x1BED80);
	const MH_STATUS mergeStatus = MH_CreateHook(
		mergeTarget,
		&ClientRecvTableMergeDeltas,
		reinterpret_cast<LPVOID*>(&s_ClientRecvTableMergeDeltasOriginal));
	const MH_STATUS propStatus = MH_CreateHook(
		propTarget,
		&ClientRecvTableMergeProp,
		reinterpret_cast<LPVOID*>(&s_ClientRecvTableMergePropOriginal));
	const MH_STATUS propIndexReadStatus = MH_CreateHook(
		propIndexReadTarget,
		&ClientDeltaPropIndexRead,
		reinterpret_cast<LPVOID*>(&s_ClientDeltaPropIndexReadOriginal));

	s_ClientRecvTableMergeHooksInstalled =
		(mergeStatus == MH_OK || mergeStatus == MH_ERROR_ALREADY_CREATED)
		&& (propStatus == MH_OK || propStatus == MH_ERROR_ALREADY_CREATED)
		&& (propIndexReadStatus == MH_OK || propIndexReadStatus == MH_ERROR_ALREADY_CREATED);

	char buffer[512];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: client RecvTable_MergeDeltas hooks mergeStatus=%d propStatus=%d propIndexReadStatus=%d mergeTarget=%p propTarget=%p propIndexReadTarget=%p mergeOriginal=%p propOriginal=%p propIndexReadOriginal=%p\n",
		static_cast<int>(mergeStatus),
		static_cast<int>(propStatus),
		static_cast<int>(propIndexReadStatus),
		mergeTarget,
		propTarget,
		propIndexReadTarget,
		reinterpret_cast<void*>(s_ClientRecvTableMergeDeltasOriginal),
		reinterpret_cast<void*>(s_ClientRecvTableMergePropOriginal),
		reinterpret_cast<void*>(s_ClientDeltaPropIndexReadOriginal));
	OutputDebugStringA(buffer);
}

static void InstallClientDataTableSetupHook(uintptr_t engineBase)
{
	if (!ShouldInstallR1OClientDebugHooks()
		|| s_ClientDataTableSetupHookInstalled
		|| !engineBase
		|| IsDedicatedServer())
		return;

	s_ClientFindRecvTableByName = reinterpret_cast<FindRecvTableByNameType>(engineBase + 0x1D86D0);
	void* target = reinterpret_cast<void*>(engineBase + 0x1C79A0);
	const MH_STATUS status = MH_CreateHook(
		target,
		&ClientDataTableSetupReceive,
		reinterpret_cast<LPVOID*>(&s_ClientDataTableSetupReceiveOriginal));

	s_ClientDataTableSetupHookInstalled = status == MH_OK || status == MH_ERROR_ALREADY_CREATED;
	char buffer[384];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: client DataTable setup hook status=%d target=%p original=%p findRecv=%p\n",
		static_cast<int>(status),
		target,
		reinterpret_cast<void*>(s_ClientDataTableSetupReceiveOriginal),
		reinterpret_cast<void*>(s_ClientFindRecvTableByName));
	OutputDebugStringA(buffer);
}

static void InstallClientCopyNewEntityHook(uintptr_t engineBase)
{
	if (!ShouldInstallR1OClientDebugHooks() || s_ClientCopyNewEntityHookInstalled || !engineBase || IsDedicatedServer())
		return;

	void* target = reinterpret_cast<void*>(engineBase + 0x53010);
	const MH_STATUS status = MH_CreateHook(
		target,
		&ClientCopyNewEntity,
		reinterpret_cast<LPVOID*>(&s_ClientCopyNewEntityOriginal));

	s_ClientCopyNewEntityHookInstalled = status == MH_OK || status == MH_ERROR_ALREADY_CREATED;
	char buffer[384];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: client CL_CopyNewEntity hook status=%d target=%p original=%p\n",
		static_cast<int>(status),
		target,
		reinterpret_cast<void*>(s_ClientCopyNewEntityOriginal));
	OutputDebugStringA(buffer);
}

using ClientNetReceivePacketType = __int64(__fastcall*)(unsigned int frameTime, __int64 packet, char encrypted);
static ClientNetReceivePacketType s_ClientNetReceivePacketOriginal = nullptr;
static bool s_ClientNetReceivePacketHookInstalled = false;

static __int64 __fastcall ClientNetReceivePacket(unsigned int frameTime, __int64 packet, char encrypted)
{
	struct PacketLogSnapshot {
		int source = 0;
		int length = 0;
		unsigned char firstBytes[16]{};
		bool valid = false;
	};

	static int logBudget = 128;
	PacketLogSnapshot snapshot{};
	if (logBudget > 0 && packet && ClientIsCommittedReadableRange(reinterpret_cast<const void*>(packet), 116)) {
		// NET_ReceivePacket owns the packet and may release or recycle its
		// payload before returning. Capture diagnostics while the input lifetime
		// is still valid; never inspect packet storage after the original call.
		__try {
			snapshot.source = *reinterpret_cast<int*>(packet + 24);
			snapshot.length = *reinterpret_cast<int*>(packet + 112);
			const unsigned char* payload = *reinterpret_cast<unsigned char**>(packet + 40);
			if (payload && snapshot.length > 0 && snapshot.length <= 4096) {
				const size_t bytesToCopy = static_cast<size_t>(snapshot.length < 16 ? snapshot.length : 16);
				if (ClientIsCommittedReadableRange(payload, bytesToCopy)) {
					memcpy(snapshot.firstBytes, payload, bytesToCopy);
					snapshot.valid = true;
				}
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			snapshot.valid = false;
		}
	}

	const __int64 result = s_ClientNetReceivePacketOriginal
		? s_ClientNetReceivePacketOriginal(frameTime, packet, encrypted)
		: 0;

	if (logBudget > 0 && snapshot.valid) {
		--logBudget;
		char buffer[768];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: client NET_ReceivePacket result=%lld frame=%u packet=%p source=%d length=%d encrypted=%d first=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
			static_cast<long long>(result),
			frameTime,
			reinterpret_cast<void*>(packet),
			snapshot.source,
			snapshot.length,
			static_cast<int>(encrypted),
			snapshot.length > 0 ? snapshot.firstBytes[0] : 0,
			snapshot.length > 1 ? snapshot.firstBytes[1] : 0,
			snapshot.length > 2 ? snapshot.firstBytes[2] : 0,
			snapshot.length > 3 ? snapshot.firstBytes[3] : 0,
			snapshot.length > 4 ? snapshot.firstBytes[4] : 0,
			snapshot.length > 5 ? snapshot.firstBytes[5] : 0,
			snapshot.length > 6 ? snapshot.firstBytes[6] : 0,
			snapshot.length > 7 ? snapshot.firstBytes[7] : 0,
			snapshot.length > 8 ? snapshot.firstBytes[8] : 0,
			snapshot.length > 9 ? snapshot.firstBytes[9] : 0,
			snapshot.length > 10 ? snapshot.firstBytes[10] : 0,
			snapshot.length > 11 ? snapshot.firstBytes[11] : 0,
			snapshot.length > 12 ? snapshot.firstBytes[12] : 0,
			snapshot.length > 13 ? snapshot.firstBytes[13] : 0,
			snapshot.length > 14 ? snapshot.firstBytes[14] : 0,
			snapshot.length > 15 ? snapshot.firstBytes[15] : 0);
		OutputDebugStringA(buffer);
	}

	return result;
}

static void InstallClientNetReceivePacketHook(uintptr_t engineBase)
{
	if (!ShouldInstallR1OClientDebugHooks() || s_ClientNetReceivePacketHookInstalled || !engineBase || IsDedicatedServer())
		return;

	void* target = reinterpret_cast<void*>(engineBase + 0x1F2A20);
	const MH_STATUS status = MH_CreateHook(
		target,
		&ClientNetReceivePacket,
		reinterpret_cast<LPVOID*>(&s_ClientNetReceivePacketOriginal));

	s_ClientNetReceivePacketHookInstalled = status == MH_OK || status == MH_ERROR_ALREADY_CREATED;
	char buffer[384];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: client NET_ReceivePacket hook status=%d target=%p original=%p\n",
		static_cast<int>(status),
		target,
		reinterpret_cast<void*>(s_ClientNetReceivePacketOriginal));
	OutputDebugStringA(buffer);
}

using ClientNetConfigType = __int64(__fastcall*)(char enableNetworking);
static ClientNetConfigType s_ClientNetConfigOriginal = nullptr;
static bool s_ClientNetConfigHookInstalled = false;
static uintptr_t s_ClientNetConfigEngineBase = 0;

static int ParseCommandLinePort(const char* token)
{
	const char* cmd = GetCommandLineA();
	if (!cmd || !token)
		return 0;

	const char* match = strstr(cmd, token);
	if (!match)
		return 0;

	match += strlen(token);
	while (*match == ' ' || *match == '\t' || *match == '"')
		++match;

	char* end = nullptr;
	const long value = strtol(match, &end, 10);
	if (end == match || value <= 0 || value > 65535)
		return 0;
	return static_cast<int>(value);
}

static void ApplyClientPortOverride()
{
	if (!s_ClientNetConfigEngineBase || IsDedicatedServer() || !SetConvarStringOriginal)
		return;

	int port = ParseCommandLinePort("+clientport");
	if (!port)
		port = ParseCommandLinePort("-clientport");
	if (!port)
		return;

	// R1 2015 engine.dll: static ConVar clientport at RVA 0x30EF4E0.
	ConVarR1* clientPort = reinterpret_cast<ConVarR1*>(s_ClientNetConfigEngineBase + 0x30EF4E0);
	if (!r1delta::client_port::ApplyOverride(clientPort, port, SetConvarStringOriginal))
		return;

	static int logBudget = 8;
	if (logBudget > 0) {
		--logBudget;
		char buffer[256];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: client NET_Config forced clientport=%d cvar=%p commandLine=%s\n",
			port,
			reinterpret_cast<void*>(clientPort),
			GetCommandLineA());
		OutputDebugStringA(buffer);
	}
}

static __int64 __fastcall ClientNetConfig(char enableNetworking)
{
	ApplyClientPortOverride();
	return s_ClientNetConfigOriginal ? s_ClientNetConfigOriginal(enableNetworking) : 0;
}

static void InstallClientNetConfigPortHook(uintptr_t engineBase)
{
	if (s_ClientNetConfigHookInstalled || !engineBase || IsDedicatedServer())
		return;
	if (!ResolveSetConvarString()) {
		OutputDebugStringA("R1Delta: refused client NET_Config port hook because the vstdlib ConVar setter is unavailable\n");
		return;
	}


	s_ClientNetConfigEngineBase = engineBase;
	void* target = reinterpret_cast<void*>(engineBase + 0x1F5220);
	const MH_STATUS status = MH_CreateHook(
		target,
		&ClientNetConfig,
		reinterpret_cast<LPVOID*>(&s_ClientNetConfigOriginal));

	s_ClientNetConfigHookInstalled = status == MH_OK || status == MH_ERROR_ALREADY_CREATED;
	char buffer[384];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: client NET_Config port hook status=%d target=%p original=%p\n",
		static_cast<int>(status),
		target,
		reinterpret_cast<void*>(s_ClientNetConfigOriginal));
	OutputDebugStringA(buffer);
}

static bool IsClientRenderPointerPoisoned(uintptr_t value)
{
	return value == 0 || value < 0x10000 || value == 0xababababababababull;
}

static bool IsClientRenderRangeReadable(const void* pointer, size_t size)
{
	if (!size)
		return true;

	uintptr_t current = reinterpret_cast<uintptr_t>(pointer);
	if (IsClientRenderPointerPoisoned(current))
		return false;

	const uintptr_t end = current + size - 1;
	if (end < current)
		return false;

	while (current <= end) {
		MEMORY_BASIC_INFORMATION mbi{};
		if (!VirtualQuery(reinterpret_cast<const void*>(current), &mbi, sizeof(mbi)))
			return false;

		const DWORD protect = mbi.Protect & 0xff;
		if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD) || protect == PAGE_NOACCESS)
			return false;

		const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
		if (regionEnd <= current)
			return false;

		current = regionEnd;
	}

	return true;
}

using ClientStudioRenderMaterialListType = unsigned int(__fastcall*)(void* studioRender, void* studioHdr, void* hardwareData, int skin, int lod, unsigned int materialFlags, void* outEntries, int maxEntries);
static ClientStudioRenderMaterialListType s_ClientStudioRenderMaterialListOriginal = nullptr;
static bool s_ClientStudioRenderMaterialListHookInstalled = false;
static int s_ClientStudioRenderInvalidMaterialListBudget = 24;

static unsigned int __fastcall ClientStudioRenderMaterialList(
	void* studioRender,
	void* studioHdr,
	void* hardwareData,
	int skin,
	int lod,
	unsigned int materialFlags,
	void* outEntries,
	int maxEntries)
{
	constexpr uint32_t kStudioHdrMagic = 0x54534449u; // 'IDST'
	const char* rejectReason = nullptr;
	char modelName[80] = "<unknown>";
	int numLods = 0;
	int numMeshes = 0;
	int numTextures = 0;
	uintptr_t lods = 0;
	uintptr_t meshData = 0;
	uintptr_t materials = 0;

	__try {
		if (!studioHdr || !hardwareData) {
			rejectReason = "null-arg";
		}
		else {
			const uintptr_t studio = reinterpret_cast<uintptr_t>(studioHdr);
			const uintptr_t hw = reinterpret_cast<uintptr_t>(hardwareData);
			const uint32_t magic = *reinterpret_cast<const uint32_t*>(studio + 0x00);
			const int version = *reinterpret_cast<const int*>(studio + 0x04);
			const int length = *reinterpret_cast<const int*>(studio + 0x50);
			numTextures = *reinterpret_cast<const int*>(studio + 0xCC);
			numLods = *reinterpret_cast<const int*>(hw + 0x04);
			lods = *reinterpret_cast<const uintptr_t*>(hw + 0x08);
			numMeshes = *reinterpret_cast<const int*>(hw + 0x10);

			const char* name = reinterpret_cast<const char*>(studio + 0x0C);
			_snprintf_s(modelName, sizeof(modelName), _TRUNCATE, "%.*s", 63, name ? name : "<null>");

			if (magic != kStudioHdrMagic || version <= 0 || length < 0x100) {
				rejectReason = "bad-studiohdr";
			}
			else if (maxEntries <= 0 || !outEntries) {
				return 0;
			}
			else if (numLods <= 0 || numLods > 16 || lod < 0 || lod >= numLods) {
				rejectReason = "bad-lod";
			}
			else if (numMeshes <= 0 || numMeshes > 32768 || numTextures < 0 || numTextures > 4096) {
				rejectReason = "bad-counts";
			}
			else if (IsClientRenderPointerPoisoned(lods) || !IsClientRenderRangeReadable(reinterpret_cast<const void*>(lods), static_cast<size_t>(numLods) * 48u)) {
				rejectReason = "bad-lod-array";
			}
			else {
				const uintptr_t lodBase = lods + static_cast<uintptr_t>(lod) * 48u;
				meshData = *reinterpret_cast<const uintptr_t*>(lodBase + 0x00);
				materials = *reinterpret_cast<const uintptr_t*>(lodBase + 0x10);
				if (numTextures == 0) {
					return 0;
				}
				else if (IsClientRenderPointerPoisoned(meshData) || !IsClientRenderRangeReadable(reinterpret_cast<const void*>(meshData), static_cast<size_t>(numMeshes) * 16u)) {
					rejectReason = "bad-mesh-data";
				}
				else if (IsClientRenderPointerPoisoned(materials) || !IsClientRenderRangeReadable(reinterpret_cast<const void*>(materials), static_cast<size_t>(numTextures) * sizeof(void*))) {
					rejectReason = "bad-material-list";
				}
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		rejectReason = "probe-exception";
	}

	if (rejectReason) {
		if (s_ClientStudioRenderInvalidMaterialListBudget > 0) {
			--s_ClientStudioRenderInvalidMaterialListBudget;
			char buffer[640];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: client studiorender material-list skipped invalid hardware data reason=%s studio=%p hw=%p model=\"%s\" skin=%d lod=%d numLods=%d numMeshes=%d numTextures=%d lods=%p meshData=%p materials=%p flags=0x%x max=%d budget=%d\n",
				rejectReason,
				studioHdr,
				hardwareData,
				modelName,
				skin,
				lod,
				numLods,
				numMeshes,
				numTextures,
				reinterpret_cast<void*>(lods),
				reinterpret_cast<void*>(meshData),
				reinterpret_cast<void*>(materials),
				materialFlags,
				maxEntries,
				s_ClientStudioRenderInvalidMaterialListBudget);
			OutputDebugStringA(buffer);
		}
		return 0;
	}

	return s_ClientStudioRenderMaterialListOriginal
		? s_ClientStudioRenderMaterialListOriginal(studioRender, studioHdr, hardwareData, skin, lod, materialFlags, outEntries, maxEntries)
		: 0;
}

static void InstallClientStudioRenderMaterialListHook(uintptr_t studioRenderBase)
{
	if (!ShouldInstallR1OClientDebugHooks() || s_ClientStudioRenderMaterialListHookInstalled || !studioRenderBase || IsDedicatedServer())
		return;
	if (!HasEngineCommandLineFlag("-r1o_debug_matguard"))
		return;

	void* target = reinterpret_cast<void*>(studioRenderBase + 0x17F30);
	const MH_STATUS status = MH_CreateHook(
		target,
		&ClientStudioRenderMaterialList,
		reinterpret_cast<LPVOID*>(&s_ClientStudioRenderMaterialListOriginal));

	s_ClientStudioRenderMaterialListHookInstalled = status == MH_OK || status == MH_ERROR_ALREADY_CREATED;
	char buffer[384];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: client studiorender material-list hook status=%d target=%p original=%p\n",
		static_cast<int>(status),
		target,
		reinterpret_cast<void*>(s_ClientStudioRenderMaterialListOriginal));
	OutputDebugStringA(buffer);
}

static bool PatchClientBytesIfMatch(uintptr_t moduleBase, uintptr_t rva, const unsigned char* expected, const unsigned char* replacement, size_t size, const char* reason)
{
	if (!moduleBase || !expected || !replacement || !size)
		return false;

	unsigned char* address = reinterpret_cast<unsigned char*>(moduleBase + rva);
	if (memcmp(address, replacement, size) == 0) {
		char buffer[384];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: client patch already applied %s at rva=0x%llx address=%p\n",
			reason,
			static_cast<unsigned long long>(rva),
			address);
		OutputDebugStringA(buffer);
		return true;
	}

	if (memcmp(address, expected, size) != 0) {
		char buffer[512];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: client patch skipped %s at rva=0x%llx address=%p bytes=%02X %02X %02X %02X %02X\n",
			reason,
			static_cast<unsigned long long>(rva),
			address,
			size > 0 ? address[0] : 0,
			size > 1 ? address[1] : 0,
			size > 2 ? address[2] : 0,
			size > 3 ? address[3] : 0,
			size > 4 ? address[4] : 0);
		OutputDebugStringA(buffer);
		return false;
	}

	DWORD oldProtect = 0;
	if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
		char buffer[384];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: client patch failed %s at rva=0x%llx VirtualProtect gle=%lu\n",
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

	char buffer[384];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: client patch applied %s at rva=0x%llx address=%p\n",
		reason,
		static_cast<unsigned long long>(rva),
		address);
	OutputDebugStringA(buffer);
	return true;
}

// client.dll+0xF74F0 is PrecacheParticleSystem(const char* pParticleSystemName).
// It looks up the particle system in g_pStringTableParticleEffectNames
// (client.dll+0xBF52C8) by calling vtable[8] on that string table. The
// datacache fires this as a release callback during shutdown, but the particle
// effect name string table is already destroyed by then, so the global is NULL
// and the vtable call AVs at client.dll+0xF7518. Guard: if the string table
// global is NULL, skip the precache entirely (return 0 like the function's own
// shutdown path does).
typedef int(__cdecl* PrecacheParticleSystemFn)(const char* pParticleSystemName);
static PrecacheParticleSystemFn oPrecacheParticleSystem = nullptr;
static int __cdecl PrecacheParticleSystemGuard(const char* pParticleSystemName)
{
	if (!G_client)
		return 0;
	const __int64* stringTable = reinterpret_cast<const __int64*>(G_client + 0xBF52C8);
	if (!*stringTable)
		return 0;
	return oPrecacheParticleSystem ? oPrecacheParticleSystem(pParticleSystemName) : 0;
}

static void InstallClientDatacacheCallbackGuard(uintptr_t clientBase)
{
	if (!clientBase || IsDedicatedServer())
		return;

	// Verify the expected prologue: push rbx; sub rsp,30h; mov rbx,rcx
	constexpr unsigned char expectedPrologue[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x30 };
	if (memcmp(reinterpret_cast<void*>(clientBase + 0xF74F0), expectedPrologue, sizeof(expectedPrologue)) != 0)
		return;

	const MH_STATUS status = MH_CreateHook(
		reinterpret_cast<void*>(clientBase + 0xF74F0),
		&PrecacheParticleSystemGuard,
		reinterpret_cast<LPVOID*>(&oPrecacheParticleSystem));
	if (status != MH_OK && status != MH_ERROR_ALREADY_CREATED) {
		Warning("R1Delta: failed to create PrecacheParticleSystem string-table guard (%d)\n", static_cast<int>(status));
		return;
	}
	const MH_STATUS enableStatus = MH_EnableHook(reinterpret_cast<void*>(clientBase + 0xF74F0));
	if (enableStatus != MH_OK) {
		Warning("R1Delta: failed to enable PrecacheParticleSystem string-table guard (%d)\n", static_cast<int>(enableStatus));
	}
}

static void PatchClientDynamicLodNoMatchSentinel(uintptr_t clientBase)
{
	if (!clientBase || IsDedicatedServer())
		return;

	// R1 2015 client CModelRenderSystem::ComputeModelLODs uses BSF over the
	// model LOD threshold mask. When no threshold bit matches it writes the x86
	// BSF sentinel value 32 into the packed model LOD field, which is later
	// expanded into StudioArrayInstanceData_t::m_nLOD and used as an unchecked
	// studiohwdata_t::m_pLODs index by studiorender.dll. Keep the original
	// nonzero-mask path, but make the zero-mask default the model's root LOD
	// instead of the out-of-range sentinel.
	const unsigned char expected[] = {
		0xB9, 0x20, 0x00, 0x00, 0x00 // mov ecx, 20h
	};
	const unsigned char replacement[] = {
		0x48, 0x8B, 0x0B,             // mov rcx, [rbx]    ; studiohwdata_t*
		0x8B, 0x09                    // mov ecx, [rcx]    ; m_RootLOD
	};
	PatchClientBytesIfMatch(
		clientBase,
		0x1BABEC,
		expected,
		replacement,
		sizeof(expected),
		"dynamic model LOD no-match root fallback");
}

static void PatchClientStaticPropLodNoMatchSentinel(uintptr_t engineBase)
{
	if (!ShouldInstallR1OClientDebugHooks() || !engineBase || IsDedicatedServer())
		return;

	// R1 2015 engine static-prop fast-pipeline LOD bucketing builds a bitmask of
	// matching LOD thresholds, isolates the low bit, and then BSRs it into the
	// per-static-prop LOD byte at draw-list offset 0xC03A. When no threshold
	// matches, the original default remains 0xFF; downstream studiorender paths
	// use that byte as an unchecked LOD index. Keep the matched-threshold path,
	// but make the no-match sentinel the root/high-detail LOD instead of 255.
	const unsigned char expected[] = {
		0xB9, 0xFF, 0x00, 0x00, 0x00 // mov ecx, 0FFh
	};
	const unsigned char replacement[] = {
		0x33, 0xC9,                   // xor ecx, ecx
		0x90, 0x90, 0x90
	};
	PatchClientBytesIfMatch(
		engineBase,
		0x18CC83,
		expected,
		replacement,
		sizeof(expected),
		"static prop LOD no-match root fallback");
}

using ClientLightProbeBlendType = __int64(__fastcall*)(unsigned int currentProbe, unsigned int targetProbe, float interpolation, float scale, void* outLightingState);
static ClientLightProbeBlendType s_ClientLightProbeBlendOriginal = nullptr;
static bool s_ClientLightProbeBlendHookInstalled = false;
static int s_ClientLightProbeInvalidLogBudget = 16;
using ClientModelLightingType = void(__fastcall*)(__int64 modelRender, const float* transformedOrigin, unsigned char useLightingCube, __int64 lightingState);
static ClientModelLightingType s_ClientModelLightingOriginal = nullptr;
static bool s_ClientModelLightingHookInstalled = false;
using ClientRenderableDrawModelType = __int64(__fastcall*)(__int64 entity, __int64 drawState, unsigned int flags, __int64 instance);
static ClientRenderableDrawModelType s_ClientRenderableDrawModelOriginal = nullptr;
static bool s_ClientRenderableDrawModelHookInstalled = false;
static thread_local __int64 s_ClientCurrentRenderingEntity = 0;
static int s_ClientRenderableTransformLogBudget = 16;

static bool ClientVectorIsFinite(const float* value)
{
	return value
		&& _finite(value[0])
		&& _finite(value[1])
		&& _finite(value[2]);
}

static const char* ClientRenderingEntityModelName(__int64 entity, void** outModel)
{
	if (outModel)
		*outModel = nullptr;
	if (!entity || !G_client)
		return "<none>";

	__try {
		auto* renderable = reinterpret_cast<void*>(entity + 8);
		auto** renderableVtable = *reinterpret_cast<void***>(renderable);
		if (!renderableVtable || !renderableVtable[8])
			return "<no-renderable-vtable>";

		using GetModelType = void* (__fastcall*)(void*);
		void* model = reinterpret_cast<GetModelType>(renderableVtable[8])(renderable);
		if (outModel)
			*outModel = model;
		if (!model)
			return "<no-model>";

		void* modelInfo = *reinterpret_cast<void**>(G_client + 0xBF5220);
		if (!modelInfo)
			return "<no-model-info>";
		auto** modelInfoVtable = *reinterpret_cast<void***>(modelInfo);
		if (!modelInfoVtable || !modelInfoVtable[3])
			return "<no-model-name-vfunc>";

		using GetModelNameType = const char* (__fastcall*)(void*, const void*);
		const char* name = reinterpret_cast<GetModelNameType>(modelInfoVtable[3])(modelInfo, model);
		return name ? name : "<null-model-name>";
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return "<model-query-exception>";
	}
}

static __int64 __fastcall ClientRenderableDrawModel(__int64 entity, __int64 drawState, unsigned int flags, __int64 instance)
{
	const __int64 previousEntity = s_ClientCurrentRenderingEntity;
	s_ClientCurrentRenderingEntity = entity;
	if (entity && s_ClientRenderableTransformLogBudget > 0) {
		__try {
			auto** entityVtable = *reinterpret_cast<void***>(entity);
			using GetVectorType = const float* (__fastcall*)(__int64);
			const float* origin = entityVtable && entityVtable[9]
				? reinterpret_cast<GetVectorType>(entityVtable[9])(entity)
				: nullptr;
			const float* angles = entityVtable && entityVtable[10]
				? reinterpret_cast<GetVectorType>(entityVtable[10])(entity)
				: nullptr;
			if (!ClientVectorIsFinite(origin) || !ClientVectorIsFinite(angles)) {
				--s_ClientRenderableTransformLogBudget;
				void* model = nullptr;
				const char* modelName = ClientRenderingEntityModelName(entity, &model);
				__int64 clientClass = 0;
				__int64 recvTable = 0;
				__int64 decoder = 0;
				const char* className = "<no-client-class>";
				int classId = -1;
				int entIndex = -1;
				auto* networkable = reinterpret_cast<void*>(entity + 16);
				auto** networkableVtable = *reinterpret_cast<void***>(networkable);
				if (networkableVtable) {
					if (networkableVtable[2]) {
						using GetClientClassType = __int64(__fastcall*)(void*);
						clientClass = reinterpret_cast<GetClientClassType>(networkableVtable[2])(networkable);
					}
					if (networkableVtable[9]) {
						using EntIndexType = int(__fastcall*)(void*);
						entIndex = reinterpret_cast<EntIndexType>(networkableVtable[9])(networkable);
					}
				}
				if (clientClass) {
					const char* candidate = *reinterpret_cast<const char**>(clientClass + 16);
					className = ClientIsReadableCString(candidate) ? candidate : "<bad-client-class-name>";
					recvTable = *reinterpret_cast<__int64*>(clientClass + 24);
					classId = *reinterpret_cast<int*>(clientClass + 40);
					decoder = ClientDecoderForRecvTable(recvTable);
				}

				char nearbyProps[1024] = {};
				size_t nearbyUsed = 0;
				const int flatCount = ClientRecvDecoderFlattenedPropCount(decoder);
				for (int i = 0; i < flatCount && i < 4096; ++i) {
					const __int64 recvProp = ClientRecvDecoderRecvProp(decoder, static_cast<unsigned int>(i));
					const int recvOffset = ClientRecvPropOffset(recvProp);
					if (recvOffset < 0x150 || recvOffset > 0x180)
						continue;
					const __int64 sendProp = ClientRecvDecoderSendProp(decoder, static_cast<unsigned int>(i));
					char propText[256];
					_snprintf_s(
						propText,
						sizeof(propText),
						_TRUNCATE,
						"%s%d:r=\"%s\"/t%d/o0x%x s=\"%s\"/t%d/o0x%x",
						nearbyUsed ? ", " : "",
						i,
						ClientRecvPropName(recvProp),
						ClientRecvPropType(recvProp),
						recvOffset,
						ClientSendPropName(sendProp),
						ClientSendPropType(sendProp),
						ClientSendPropOffset(sendProp));
					const size_t remaining = sizeof(nearbyProps) - nearbyUsed;
					if (remaining <= 1)
						break;
					strncpy_s(nearbyProps + nearbyUsed, remaining, propText, _TRUNCATE);
					nearbyUsed = strlen(nearbyProps);
				}

				char buffer[2048];
				_snprintf_s(
					buffer,
					sizeof(buffer),
					_TRUNCATE,
					"R1Delta: client renderable has invalid transform entity=%p entIndex=%d class=%p classId=%d className=\"%s\" recvTable=%p recvName=\"%s\" decoder=%p flat=%d model=%p name=\"%s\" originPtr=%p origin=(%.9g,%.9g,%.9g) anglesPtr=%p angles=(%.9g,%.9g,%.9g) flags=0x%x instance=%p nearbyProps=[%s] budget=%d\n",
					reinterpret_cast<void*>(entity),
					entIndex,
					reinterpret_cast<void*>(clientClass),
					classId,
					className,
					reinterpret_cast<void*>(recvTable),
					ClientRecvTableName(recvTable),
					reinterpret_cast<void*>(decoder),
					flatCount,
					model,
					modelName,
					origin,
					origin ? origin[0] : 0.0f,
					origin ? origin[1] : 0.0f,
					origin ? origin[2] : 0.0f,
					angles,
					angles ? angles[0] : 0.0f,
					angles ? angles[1] : 0.0f,
					angles ? angles[2] : 0.0f,
					flags,
					reinterpret_cast<void*>(instance),
					nearbyProps,
					s_ClientRenderableTransformLogBudget);
				OutputDebugStringA(buffer);
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
		}
	}
	const __int64 result = s_ClientRenderableDrawModelOriginal
		? s_ClientRenderableDrawModelOriginal(entity, drawState, flags, instance)
		: 0;
	s_ClientCurrentRenderingEntity = previousEntity;
	return result;
}

static void InstallClientRenderableDrawModelHook(uintptr_t clientBase)
{
	if (!ShouldInstallR1OClientDebugHooks() || s_ClientRenderableDrawModelHookInstalled || !clientBase || IsDedicatedServer())
		return;

	void* target = reinterpret_cast<void*>(clientBase + 0x678F0);
	const MH_STATUS status = MH_CreateHook(
		target,
		&ClientRenderableDrawModel,
		reinterpret_cast<LPVOID*>(&s_ClientRenderableDrawModelOriginal));
	s_ClientRenderableDrawModelHookInstalled = status == MH_OK || status == MH_ERROR_ALREADY_CREATED;

	char buffer[384];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: client renderable DrawModel hook status=%d target=%p original=%p\n",
		static_cast<int>(status),
		target,
		reinterpret_cast<void*>(s_ClientRenderableDrawModelOriginal));
	OutputDebugStringA(buffer);
}

static void __fastcall ClientModelLighting(__int64 modelRender, const float* transformedOrigin, unsigned char useLightingCube, __int64 lightingState)
{
	float rawOrigin[3] = {};
	float effectiveOrigin[3] = {};
	float* explicitOrigin = nullptr;
	float* transform = nullptr;
	bool readable = false;

	__try {
		if (transformedOrigin) {
			memcpy(rawOrigin, transformedOrigin, sizeof(rawOrigin));
			memcpy(effectiveOrigin, transformedOrigin, sizeof(effectiveOrigin));
		}
		if (lightingState) {
			explicitOrigin = *reinterpret_cast<float**>(lightingState + 56);
			transform = *reinterpret_cast<float**>(lightingState + 48);
		}
		if (explicitOrigin) {
			memcpy(effectiveOrigin, explicitOrigin, sizeof(effectiveOrigin));
		}
		else if (transform) {
			for (int row = 0; row < 3; ++row) {
				effectiveOrigin[row] =
					rawOrigin[0] * transform[row * 4 + 0]
					+ rawOrigin[1] * transform[row * 4 + 1]
					+ rawOrigin[2] * transform[row * 4 + 2]
					+ transform[row * 4 + 3];
			}
		}
		readable = true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		readable = false;
	}

	if (!readable || !ClientVectorIsFinite(effectiveOrigin)) {
		void* model = nullptr;
		const char* modelName = ClientRenderingEntityModelName(s_ClientCurrentRenderingEntity, &model);
		char buffer[768];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: client model lighting received invalid origin readable=%d entity=%p model=%p name=\"%s\" raw=(%.9g,%.9g,%.9g) effective=(%.9g,%.9g,%.9g) explicit=%p transform=%p lightingState=%p useCube=%u\n",
			readable ? 1 : 0,
			reinterpret_cast<void*>(s_ClientCurrentRenderingEntity),
			model,
			modelName,
			rawOrigin[0],
			rawOrigin[1],
			rawOrigin[2],
			effectiveOrigin[0],
			effectiveOrigin[1],
			effectiveOrigin[2],
			explicitOrigin,
			transform,
			reinterpret_cast<void*>(lightingState),
			static_cast<unsigned int>(useLightingCube));
		OutputDebugStringA(buffer);
	}

	if (s_ClientModelLightingOriginal)
		s_ClientModelLightingOriginal(modelRender, transformedOrigin, useLightingCube, lightingState);
}

static void InstallClientModelLightingHook(uintptr_t engineBase)
{
	if (!ShouldInstallR1OClientDebugHooks() || s_ClientModelLightingHookInstalled || !engineBase || IsDedicatedServer())
		return;

	void* target = reinterpret_cast<void*>(engineBase + 0xA72A0);
	const MH_STATUS status = MH_CreateHook(
		target,
		&ClientModelLighting,
		reinterpret_cast<LPVOID*>(&s_ClientModelLightingOriginal));
	s_ClientModelLightingHookInstalled = status == MH_OK || status == MH_ERROR_ALREADY_CREATED;

	char buffer[384];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: client model lighting hook status=%d target=%p original=%p\n",
		static_cast<int>(status),
		target,
		reinterpret_cast<void*>(s_ClientModelLightingOriginal));
	OutputDebugStringA(buffer);
}

static __int64 __fastcall ClientLightProbeBlend(unsigned int currentProbe, unsigned int targetProbe, float interpolation, float scale, void* outLightingState)
{
	constexpr unsigned int kInvalidProbe = 0xFFFFFFFFu;
	const unsigned int originalCurrent = currentProbe;
	const unsigned int originalTarget = targetProbe;
	float cachedOrigin[3] = {};
	float cachedFraction = 0.0f;
	float cachedRate = 0.0f;
	int cachedProbeQuery = -1;
	if (outLightingState) {
		const auto* cacheBytes = static_cast<const unsigned char*>(outLightingState);
		memcpy(cachedOrigin, cacheBytes + 84, sizeof(cachedOrigin));
		memcpy(&cachedProbeQuery, cacheBytes + 100, sizeof(cachedProbeQuery));
		memcpy(&cachedFraction, cacheBytes + 112, sizeof(cachedFraction));
		memcpy(&cachedRate, cacheBytes + 116, sizeof(cachedRate));
	}

	if (currentProbe == kInvalidProbe || targetProbe == kInvalidProbe) {
		if (currentProbe == kInvalidProbe && targetProbe != kInvalidProbe) {
			// The model-lighting cache can be born with no previous probe but a
			// valid target probe while its transition fraction is still below 1.0.
			// Blending target->target is equivalent to snapping to the first valid
			// probe and keeps the light-probe table index in range.
			currentProbe = targetProbe;
			interpolation = 1.0f;
		}
		else if (targetProbe == kInvalidProbe && currentProbe != kInvalidProbe) {
			targetProbe = currentProbe;
			interpolation = 0.0f;
		}
		else {
			if (outLightingState)
				memset(outLightingState, 0, 80);
			if (s_ClientLightProbeInvalidLogBudget > 0) {
				--s_ClientLightProbeInvalidLogBudget;
				char buffer[512];
				_snprintf_s(
					buffer,
					sizeof(buffer),
					_TRUNCATE,
					"R1Delta: client light-probe blend had no valid probes current=%u target=%u out=%p origin=(%.3f,%.3f,%.3f) query=%d fraction=%.6f rate=%.6f interp=%.6f scale=%.6f budget=%d\n",
					originalCurrent,
					originalTarget,
					outLightingState,
					cachedOrigin[0],
					cachedOrigin[1],
					cachedOrigin[2],
					cachedProbeQuery,
					cachedFraction,
					cachedRate,
					interpolation,
					scale,
					s_ClientLightProbeInvalidLogBudget);
				OutputDebugStringA(buffer);
			}
			return 0;
		}

		if (s_ClientLightProbeInvalidLogBudget > 0) {
			--s_ClientLightProbeInvalidLogBudget;
			char buffer[512];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: client light-probe blend normalized invalid probe current=%u target=%u -> current=%u target=%u interp=%.3f out=%p origin=(%.3f,%.3f,%.3f) query=%d fraction=%.6f rate=%.6f scale=%.6f budget=%d\n",
				originalCurrent,
				originalTarget,
				currentProbe,
				targetProbe,
				interpolation,
				outLightingState,
				cachedOrigin[0],
				cachedOrigin[1],
				cachedOrigin[2],
				cachedProbeQuery,
				cachedFraction,
				cachedRate,
				scale,
				s_ClientLightProbeInvalidLogBudget);
			OutputDebugStringA(buffer);
		}
	}

	return s_ClientLightProbeBlendOriginal
		? s_ClientLightProbeBlendOriginal(currentProbe, targetProbe, interpolation, scale, outLightingState)
		: 0;
}

static void InstallClientLightProbeBlendHook(uintptr_t engineBase)
{
	if (!ShouldInstallR1OClientDebugHooks() || s_ClientLightProbeBlendHookInstalled || !engineBase || IsDedicatedServer())
		return;

	void* target = reinterpret_cast<void*>(engineBase + 0xAE540);
	const MH_STATUS status = MH_CreateHook(
		target,
		&ClientLightProbeBlend,
		reinterpret_cast<LPVOID*>(&s_ClientLightProbeBlendOriginal));

	s_ClientLightProbeBlendHookInstalled = status == MH_OK || status == MH_ERROR_ALREADY_CREATED;
	char buffer[384];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: client light-probe blend hook status=%d target=%p original=%p\n",
		static_cast<int>(status),
		target,
		reinterpret_cast<void*>(s_ClientLightProbeBlendOriginal));
	OutputDebugStringA(buffer);
}

void Host_InitHook(bool a1) {
	Host_InitOriginal(a1);
	OriginalCCVar_FindVar(cvarinterface, "sv_alltalk")->m_nFlags |= FCVAR_REPLICATED;
	auto user_id = OriginalCCVar_FindVar(cvarinterface, "platform_user_id");
	EnsurePlatformUserIdString(user_id);
	user_id->m_nFlags |= FCVAR_DEVELOPMENTONLY;

	MCPServer::InstallEchoCommandFix();

	// Initialize MCP server only if -usemcp argument is present
	if (ShouldEnableMCP())
	{
		MCPServer::InitializeMCP();
	}

	if (!eos::InitializeNetworking())
	{
		Msg("EOS: Initialization skipped or failed\n");
	}
	net_hooks::Initialize();

	return;
}

static FORCEINLINE void
do_engine(const LDR_DLL_NOTIFICATION_DATA* notification_data)
{
	G_engine = (uintptr_t)notification_data->Loaded.DllBase;
	auto engine_base = G_engine;
	SetupReflexEngineHooks(engine_base);
	if (!IsDedicatedServer())
		InstallNoStaticPropsHook(engine_base);
	if (!IsDedicatedServer())
		InstallNoClipBrushesHook(engine_base);
	if (!IsDedicatedServer() && !Cbuf_AddTextOriginal)
		Cbuf_AddTextOriginal = reinterpret_cast<Cbuf_AddTextType>(engine_base + 0x102D50);
	const bool installClientDebugHooks = !IsDedicatedServer() && ShouldInstallR1OClientDebugHooks();
	MH_CreateHook((LPVOID)(engine_base + 0x1FDA50), &CLC_Move__ReadFromBuffer, reinterpret_cast<LPVOID*>(&CLC_Move__ReadFromBufferOriginal));
	MH_CreateHook((LPVOID)(engine_base + 0x1F6F10), &CLC_Move__WriteToBuffer, reinterpret_cast<LPVOID*>(&CLC_Move__WriteToBufferOriginal));
	MH_CreateHook((LPVOID)(engine_base + 0x203C20), &NET_SetConVar__ReadFromBuffer, NULL);
	MH_CreateHook((LPVOID)(engine_base + 0x202F80), &NET_SetConVar__WriteToBuffer, NULL);
	MH_CreateHook((LPVOID)(engine_base + 0x1FE3F0), &SVC_ServerInfo__WriteToBuffer, reinterpret_cast<LPVOID*>(&SVC_ServerInfo__WriteToBufferOriginal));
	MH_CreateHook((LPVOID)(engine_base + 0x19CBC0), &GetBuildNo, NULL);
	MH_CreateHook((LPVOID)(engine_base + 0x1E0C10), &CNetChan__GetAddress, reinterpret_cast<LPVOID*>(&oCNetChan__GetAddress));
	MH_CreateHook((void*)(engine_base + 0x133AA0), &Host_InitHook, (void**)&Host_InitOriginal);
	if (installClientDebugHooks) {
		InstallClientSVCServerInfoReadHook(engine_base);
		InstallClientSVCServerInfoProcessHook(engine_base);
		InstallClientNetMessageReadHooks(engine_base);
		InstallClientNetMessageWriteHooks(engine_base);
		InstallClientNetMessageProcessHooks(engine_base);
		InstallCOMExplainDisconnectionHook(engine_base);
		InstallClientDataTableSetupHook(engine_base);
		InstallClientRecvTableMergeHooks(engine_base);
		InstallClientCopyNewEntityHook(engine_base);
		InstallClientNetReceivePacketHook(engine_base);
		InstallClientLightProbeBlendHook(engine_base);
		InstallClientModelLightingHook(engine_base);
	}
	if (!IsDedicatedServer())
		InstallClientNetConfigPortHook(engine_base);
	if (installClientDebugHooks)
		PatchClientStaticPropLodNoMatchSentinel(engine_base);

	// Client connectionless traffic uses the stock R1 handler. Query limiting is
	// disabled by default and must not be toggled around unrelated packet types.

	if (!IsDedicatedServer()) {
		MH_CreateHook((LPVOID)(G_engine + 0x1305E0), &ExecuteConfigFile, NULL);
		InstallPersistentProfileWriterHook(G_engine);
		MH_CreateHook((LPVOID)(engine_base + 0x21F9C0), &CEngineVGui__Init, reinterpret_cast<LPVOID*>(&CEngineVGui__InitOriginal));
		MH_CreateHook((LPVOID)(engine_base + 0x21EB70), &CEngineVGui__HideGameUI, reinterpret_cast<LPVOID*>(&CEngineVGui__HideGameUIOriginal));
		RegisterConCommand("toggleconsole", ToggleConsoleCommand, "Toggles the console", (1 << 17));
		RegisterConCommand("clear", ClearConsoleCommand, "Clears the console", (1 << 17));
		RegisterConCommand("delta_start_discord_auth", DiscordAuthCommand, "Starts the discord auth process", 0);
		RegisterConCommand(PERSIST_COMMAND, setinfopersist_cmd, "Set persistent variable", FCVAR_SERVER_CAN_EXECUTE);

		// Register slot commands
		RegisterConCommand("slot1", Slot1Command, "Select menu slot 1", 0);
		RegisterConCommand("slot2", Slot2Command, "Select menu slot 2", 0);
		RegisterConCommand("slot3", Slot3Command, "Select menu slot 3", 0);
		RegisterConCommand("slot4", Slot4Command, "Select menu slot 4", 0);
		RegisterConCommand("slot5", Slot5Command, "Select menu slot 5", 0);
		RegisterConCommand("slot6", Slot6Command, "Select menu slot 6", 0);
		RegisterConCommand("slot7", Slot7Command, "Select menu slot 7", 0);
		RegisterConCommand("slot8", Slot8Command, "Select menu slot 8", 0);
		RegisterConCommand("slot9", Slot9Command, "Select menu slot 9", 0);
		RegisterConCommand("slot10", Slot10Command, "Select menu slot 10", 0);
		MH_CreateHook((LPVOID)(G_engine + 0x2A200), &CBaseClientState_SendConnectPacket, reinterpret_cast<LPVOID*>(&CBaseClientState_SendConnectPacket_Original));
		//g_pLogAudio = RegisterConVar("fs_log_audio", "0", FCVAR_NONE, "Log audio file reads");
		MH_CreateHook((LPVOID)(G_engine + 0xAE00), &GetAcacheHk, reinterpret_cast<LPVOID*>(&GetAcacheOriginal));
		// InitSteamHooks(); // Removed - steam.cpp was unused
		InitAddons();

	}

	// TODO(mrsteyk): nice-ify.
	extern void DeltaMemoryStats(const CCommand & c);
	extern void DeltaMemoryTraceStats(const CCommand& c);
	RegisterConCommand("delta_memory_stats", DeltaMemoryStats, "Dump memory stats", 0);
	RegisterConCommand(
		"delta_memory_trace_stats",
		DeltaMemoryTraceStats,
		"Dump allocation provenance collected by -r1delta_trace_allocations",
		0);

    // TODO(mrsteyk): fuck Windows for not abiding by stack reserve rules.
    security_fixes_engine(engine_base);

    R1DAssert(MH_EnableHook(MH_ALL_HOOKS) == MH_OK);

	//// Fix stack smash in CNetChan::ProcessSubChannelData
	CNetChan__ProcessSubChannelData_Asm_continue = (uintptr_t)(engine_base + 0x1E8DDA);
	CNetChan__ProcessSubChannelData_ret0 = (uintptr_t)(engine_base + 0x1E8F26);
	void* allign = (void*)(engine_base + 0x1EA961);

	auto* jmp_pos = (void*)(engine_base + 0x1E8DD5); // `call nullsub_87` offset
	// 0xE9, 0x87, 0x1B, 0x00, 0x00 // jmp 0x1b8c (algn_1801EA961)  (0x1EA961 - 0x1E8DD5)
	DWORD old_protect;
	VirtualProtect(jmp_pos, 5, PAGE_EXECUTE_READWRITE, &old_protect);
	*((unsigned char*)jmp_pos) = 0xE9;
	*(unsigned char*)((uint64_t)jmp_pos + 1) = 0x87;
	*(unsigned char*)((uint64_t)jmp_pos + 2) = 0x1B;
	*(unsigned char*)((uint64_t)jmp_pos + 3) = 0x00;
	*(unsigned char*)((uint64_t)jmp_pos + 4) = 0x00;
	VirtualProtect(jmp_pos, 5, old_protect, &old_protect);

	VirtualProtect(allign, 6, PAGE_EXECUTE_READWRITE, &old_protect);
	*((unsigned char*)allign) = 0xFF;
	*(unsigned char*)((uint64_t)allign + 1) = 0x25;
	*(unsigned char*)((uint64_t)allign + 2) = 0x00;
	*(unsigned char*)((uint64_t)allign + 3) = 0x00;
	*(unsigned char*)((uint64_t)allign + 4) = 0x00;
	*(unsigned char*)((uint64_t)allign + 5) = 0x00;
	*(uintptr_t**)((uint64_t)allign + 6) = &CNetChan__ProcessSubChannelData_AsmConductBufferSizeCheck;
	VirtualProtect(allign, 6, old_protect, &old_protect);
}

static FORCEINLINE void
HookMsvcAllocator(uintptr_t moduleBase, uintptr_t callocRva, uintptr_t mallocRva, uintptr_t reallocRva, uintptr_t recallocRva, uintptr_t freeRva)
{
	MH_CreateHook((LPVOID)(moduleBase + callocRva), &hkcalloc_base, NULL);
	MH_CreateHook((LPVOID)(moduleBase + mallocRva), &hkmalloc_base, NULL);
	MH_CreateHook((LPVOID)(moduleBase + reallocRva), &hkrealloc_base, NULL);
	MH_CreateHook((LPVOID)(moduleBase + recallocRva), &hkrecalloc_base, NULL);
	MH_CreateHook((LPVOID)(moduleBase + freeRva), &hkfree_base, NULL);
}

struct MsvcAllocatorRvas {
	uintptr_t callocRva;
	uintptr_t mallocRva;
	uintptr_t reallocRva;
	uintptr_t recallocRva;
	uintptr_t freeRva;
};

static SRWLOCK s_r1oAllocatorHookLock = SRWLOCK_INIT;
static uintptr_t s_r1oAllocatorHookedModules[64] = {};

static bool MarkR1OAllocatorModuleHooked(uintptr_t moduleBase)
{
	AcquireSRWLockExclusive(&s_r1oAllocatorHookLock);
	for (uintptr_t hooked : s_r1oAllocatorHookedModules) {
		if (hooked == moduleBase) {
			ReleaseSRWLockExclusive(&s_r1oAllocatorHookLock);
			return false;
		}
	}

	for (uintptr_t& hooked : s_r1oAllocatorHookedModules) {
		if (!hooked) {
			hooked = moduleBase;
			ReleaseSRWLockExclusive(&s_r1oAllocatorHookLock);
			return true;
		}
	}
	ReleaseSRWLockExclusive(&s_r1oAllocatorHookLock);
	return false;
}

static bool ModuleHasImageRva(uintptr_t moduleBase, uintptr_t rva, size_t size)
{
	MODULEINFO info = {};
	if (!GetModuleInformation(GetCurrentProcess(), reinterpret_cast<HMODULE>(moduleBase), &info, sizeof(info)))
		return false;
	return rva < info.SizeOfImage && size <= info.SizeOfImage - rva;
}

static MH_STATUS EnableCreatedHook(uintptr_t target, MH_STATUS createStatus)
{
	if (createStatus != MH_OK && createStatus != MH_ERROR_ALREADY_CREATED)
		return createStatus;

	const MH_STATUS enableStatus = MH_EnableHook(reinterpret_cast<LPVOID>(target));
	return enableStatus == MH_ERROR_ENABLED ? MH_OK : enableStatus;
}

using ServerGetModelPtrType = uintptr_t(__fastcall*)(uintptr_t entity);
using ServerSetupBonesType = void**(__fastcall*)(uintptr_t entity, uintptr_t boneToWorldOut, unsigned int maxBones, unsigned int boneMask);
using ServerBoneCacheType = __int64(__fastcall*)(uintptr_t entity, char forceRebuild, unsigned int boneMask);
static ServerGetModelPtrType ServerGetModelPtr;
static ServerSetupBonesType ServerSetupBonesOriginal;
static ServerBoneCacheType ServerBoneCacheOriginal;
static uintptr_t s_ServerSetupBonesTarget;
static uintptr_t s_ServerBoneCacheTarget;
using ServerScannerMoveToDivebombType = void(__fastcall*)(uintptr_t scanner, float frameTime);
static ServerScannerMoveToDivebombType ServerScannerMoveToDivebombOriginal;
static uintptr_t s_ServerScannerMoveToDivebombTarget;


static bool EntityHasServerStudioHdr(uintptr_t entity, const char* context)
{
	if (!entity || !ServerGetModelPtr)
		return entity != 0;

	if (ServerGetModelPtr(entity))
		return true;

	static int s_logBudget = 64;
	if (s_logBudget > 0)
	{
		--s_logBudget;
		char buffer[256];
		_snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
			"R1Delta: server model bone guard skipped %s ent=%p with no studio hdr\n",
			context ? context : "<unknown>",
			reinterpret_cast<void*>(entity));
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}
	return false;
}

static void** __fastcall ServerSetupBones(uintptr_t entity, uintptr_t boneToWorldOut, unsigned int maxBones, unsigned int boneMask)
{
	if (!EntityHasServerStudioHdr(entity, "SetupBones"))
		return nullptr;

	return ServerSetupBonesOriginal
		? ServerSetupBonesOriginal(entity, boneToWorldOut, maxBones, boneMask)
		: nullptr;
}

static __int64 __fastcall ServerBoneCache(uintptr_t entity, char forceRebuild, unsigned int boneMask)
{
	if (!EntityHasServerStudioHdr(entity, "BoneCache"))
		return 0;

	return ServerBoneCacheOriginal
		? ServerBoneCacheOriginal(entity, forceRebuild, boneMask)
		: 0;
}

static void __fastcall ServerScannerMoveToDivebomb(uintptr_t scanner, float frameTime)
{
	if (!scanner)
		return;

	const uintptr_t physicsObject = *reinterpret_cast<uintptr_t*>(scanner + 600);
	if (!physicsObject)
	{
		static int s_logBudget = 64;
		if (s_logBudget > 0)
		{
			--s_logBudget;
			char buffer[256];
			_snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
				"R1Delta: scanner divebomb skipped ent=%p frame=%.4f with no physics object\n",
				reinterpret_cast<void*>(scanner),
				frameTime);
			OutputDebugStringA(buffer);
			Warning("%s", buffer);
		}
		return;
	}

	if (ServerScannerMoveToDivebombOriginal)
		ServerScannerMoveToDivebombOriginal(scanner, frameTime);
}


static void InstallServerModelBoneGuards(uintptr_t serverBase)
{
	constexpr uintptr_t kGetModelPtrRva = 0x81F40;
	constexpr uintptr_t kSetupBonesRva = 0x8A120;
	constexpr uintptr_t kBoneCacheRva = 0x8D000;
	constexpr uintptr_t kScannerMoveToDivebombRva = 0x441880;
	if (!serverBase
		|| !ModuleHasImageRva(serverBase, kGetModelPtrRva, 0x80)
		|| !ModuleHasImageRva(serverBase, kSetupBonesRva, 0x620)
		|| !ModuleHasImageRva(serverBase, kBoneCacheRva, 0x320)
		|| !ModuleHasImageRva(serverBase, kScannerMoveToDivebombRva, 0x190))
		return;

	ServerGetModelPtr = reinterpret_cast<ServerGetModelPtrType>(serverBase + kGetModelPtrRva);
	const uintptr_t setupTarget = serverBase + kSetupBonesRva;
	const uintptr_t boneCacheTarget = serverBase + kBoneCacheRva;
	const uintptr_t scannerDivebombTarget = serverBase + kScannerMoveToDivebombRva;
	if (s_ServerSetupBonesTarget == setupTarget
		&& s_ServerBoneCacheTarget == boneCacheTarget
		&& s_ServerScannerMoveToDivebombTarget == scannerDivebombTarget)
		return;

	const MH_STATUS setupCreateStatus = MH_CreateHook(
		reinterpret_cast<LPVOID>(setupTarget),
		&ServerSetupBones,
		reinterpret_cast<LPVOID*>(&ServerSetupBonesOriginal));
	const MH_STATUS setupEnableStatus = EnableCreatedHook(setupTarget, setupCreateStatus);
	if (setupEnableStatus == MH_OK || setupEnableStatus == MH_ERROR_ENABLED)
		s_ServerSetupBonesTarget = setupTarget;

	const MH_STATUS boneCacheCreateStatus = MH_CreateHook(
		reinterpret_cast<LPVOID>(boneCacheTarget),
		&ServerBoneCache,
		reinterpret_cast<LPVOID*>(&ServerBoneCacheOriginal));
	const MH_STATUS boneCacheEnableStatus = EnableCreatedHook(boneCacheTarget, boneCacheCreateStatus);
	if (boneCacheEnableStatus == MH_OK || boneCacheEnableStatus == MH_ERROR_ENABLED)
		s_ServerBoneCacheTarget = boneCacheTarget;

	const MH_STATUS scannerDivebombCreateStatus = MH_CreateHook(
		reinterpret_cast<LPVOID>(scannerDivebombTarget),
		&ServerScannerMoveToDivebomb,
		reinterpret_cast<LPVOID*>(&ServerScannerMoveToDivebombOriginal));
	const MH_STATUS scannerDivebombEnableStatus = EnableCreatedHook(scannerDivebombTarget, scannerDivebombCreateStatus);
	if (scannerDivebombEnableStatus == MH_OK || scannerDivebombEnableStatus == MH_ERROR_ENABLED)
		s_ServerScannerMoveToDivebombTarget = scannerDivebombTarget;

	char buffer[512];
	_snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
		"R1Delta: server model guards setup create=%d enable=%d target=%p original=%p cache create=%d enable=%d target=%p original=%p scanner dive create=%d enable=%d target=%p original=%p\n",
		static_cast<int>(setupCreateStatus),
		static_cast<int>(setupEnableStatus),
		reinterpret_cast<void*>(setupTarget),
		reinterpret_cast<void*>(ServerSetupBonesOriginal),
		static_cast<int>(boneCacheCreateStatus),
		static_cast<int>(boneCacheEnableStatus),
		reinterpret_cast<void*>(boneCacheTarget),
		reinterpret_cast<void*>(ServerBoneCacheOriginal),
		static_cast<int>(scannerDivebombCreateStatus),
		static_cast<int>(scannerDivebombEnableStatus),
		reinterpret_cast<void*>(scannerDivebombTarget),
		reinterpret_cast<void*>(ServerScannerMoveToDivebombOriginal));
	OutputDebugStringA(buffer);
	if ((setupCreateStatus != MH_OK
			&& setupCreateStatus != MH_ERROR_ALREADY_CREATED)
		|| setupEnableStatus != MH_OK
		|| (boneCacheCreateStatus != MH_OK
			&& boneCacheCreateStatus != MH_ERROR_ALREADY_CREATED)
		|| boneCacheEnableStatus != MH_OK
		|| (scannerDivebombCreateStatus != MH_OK
			&& scannerDivebombCreateStatus != MH_ERROR_ALREADY_CREATED)
		|| scannerDivebombEnableStatus != MH_OK) {
		Warning("%s", buffer);
	}
}


using CPropVehicleDriveableSetVehicleEntryAnimType = void(__fastcall*)(uintptr_t vehicleSubobject, bool enabled);
using CPropVehicleDriveableTurnOnType = void(__fastcall*)(uintptr_t vehicle);
using CPropVehicleDriveableDriveVehicleType = void(__fastcall*)(uintptr_t vehicle, float frameTime, uintptr_t userCmd, int buttonsDown, int buttonsReleased);
static CPropVehicleDriveableSetVehicleEntryAnimType CPropVehicleDriveable_SetVehicleEntryAnimOriginal;
static CPropVehicleDriveableTurnOnType CPropVehicleDriveable_TurnOn;
static CPropVehicleDriveableDriveVehicleType CPropVehicleDriveable_DriveVehicleOriginal;
static uintptr_t s_CPropVehicleDriveableSetVehicleEntryAnimTarget;
static uintptr_t s_CPropVehicleDriveableDriveVehicleTarget;
using CFourWheelVehiclePhysicsCalcWheelDataType = __int64(__fastcall*)(uintptr_t vehiclePhysics, uintptr_t vehicleParams);
static CFourWheelVehiclePhysicsCalcWheelDataType CFourWheelVehiclePhysics_CalcWheelDataOriginal;
static uintptr_t s_CFourWheelVehiclePhysicsCalcWheelDataTarget;

static void __fastcall CPropVehicleDriveable_SetVehicleEntryAnim(uintptr_t vehicleSubobject, bool enabled)
{
	// R1/TFO can leave prop_vehicle_driveable stuck entering when the model has no HL2 vehicle entry sequence.
	if (CPropVehicleDriveable_SetVehicleEntryAnimOriginal)
		CPropVehicleDriveable_SetVehicleEntryAnimOriginal(vehicleSubobject, false);

	if (CPropVehicleDriveable_TurnOn)
		CPropVehicleDriveable_TurnOn(vehicleSubobject - 3064);
}

static float ReadFloat(uintptr_t address)
{
	return *reinterpret_cast<float*>(address);
}

static void WriteFloat(uintptr_t address, float value)
{
	*reinterpret_cast<float*>(address) = value;
}

static bool IsReasonableSuspensionTravel(float value)
{
	return std::isfinite(value) && value >= 4.0f && value <= 96.0f;
}

static float DeriveSuspensionTravel(float raytraceZ, float wheelZ)
{
	const float travel = fabsf(raytraceZ - wheelZ);
	return IsReasonableSuspensionTravel(travel) ? travel : 38.0f;
}

static void __fastcall CPropVehicleDriveable_DriveVehicle(uintptr_t vehicle, float frameTime, uintptr_t userCmd, int buttonsDown, int buttonsReleased)
{
	float forwardMove = userCmd ? ReadFloat(userCmd + 52) : 0.0f;
	float sideMove = userCmd ? ReadFloat(userCmd + 56) : 0.0f;
	int buttons = userCmd ? *reinterpret_cast<int*>(userCmd + 64) : 0;
	const float preThrottle = ReadFloat(vehicle + 2704);
	const int preSpeed = *reinterpret_cast<int*>(vehicle + 2736);
	const bool preEngineOn = *reinterpret_cast<unsigned char*>(vehicle + 3020) != 0;
	const uintptr_t controller = *reinterpret_cast<uintptr_t*>(vehicle + 2728);

	if (userCmd && !IsR1ODedicatedServer() && buttons == 0 && forwardMove == 0.0f && sideMove == 0.0f)
	{
		int fallbackButtons = 0;
		float fallbackForward = 0.0f;
		float fallbackSide = 0.0f;

		if ((GetAsyncKeyState('W') & 0x8000) != 0)
		{
			fallbackButtons |= 0x8;
			fallbackForward += 1.0f;
		}
		if ((GetAsyncKeyState('S') & 0x8000) != 0)
		{
			fallbackButtons |= 0x10;
			fallbackForward -= 1.0f;
		}
		if ((GetAsyncKeyState('A') & 0x8000) != 0)
		{
			fallbackButtons |= 0x200;
			fallbackSide -= 1.0f;
		}
		if ((GetAsyncKeyState('D') & 0x8000) != 0)
		{
			fallbackButtons |= 0x400;
			fallbackSide += 1.0f;
		}
		if ((GetAsyncKeyState(VK_SPACE) & 0x8000) != 0)
			fallbackButtons |= 0x2;
		if ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0)
			fallbackButtons |= 0x20000;

		if (fallbackButtons)
		{
			*reinterpret_cast<float*>(userCmd + 52) = fallbackForward;
			*reinterpret_cast<float*>(userCmd + 56) = fallbackSide;
			*reinterpret_cast<int*>(userCmd + 64) = fallbackButtons;
			forwardMove = fallbackForward;
			sideMove = fallbackSide;
			buttons = fallbackButtons;
		}
	}

	if (CPropVehicleDriveable_DriveVehicleOriginal)
		CPropVehicleDriveable_DriveVehicleOriginal(vehicle, frameTime, userCmd, buttonsDown, buttonsReleased);

	const float postThrottle = ReadFloat(vehicle + 2704);
	const int postSpeed = *reinterpret_cast<int*>(vehicle + 2736);

	static ULONGLONG s_lastLogMs;
	static int s_logBudget = 120;
	const ULONGLONG nowMs = GetTickCount64();
	const bool hasInput = buttons || buttonsDown || buttonsReleased || forwardMove != 0.0f || sideMove != 0.0f;
	if (s_logBudget > 0 && (hasInput || nowMs - s_lastLogMs >= 1000))
	{
		s_lastLogMs = nowMs;
		--s_logBudget;

		char buffer[512];
		_snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
			"R1Delta: vehicle DriveVehicle veh=%p ctrl=%p frame=%.4f cmd=%p fwd=%.3f side=%.3f btn=0x%08X down=0x%08X rel=0x%08X engine=%d throttle %.3f->%.3f speed %d->%d\n",
			reinterpret_cast<void*>(vehicle),
			reinterpret_cast<void*>(controller),
			frameTime,
			reinterpret_cast<void*>(userCmd),
			forwardMove,
			sideMove,
			buttons,
			buttonsDown,
			buttonsReleased,
			preEngineOn ? 1 : 0,
			preThrottle,
			postThrottle,
			preSpeed,
			postSpeed);
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}
}

static __int64 __fastcall CFourWheelVehiclePhysics_CalcWheelData(uintptr_t vehiclePhysics, uintptr_t vehicleParams)
{
	const __int64 result = CFourWheelVehiclePhysics_CalcWheelDataOriginal
		? CFourWheelVehiclePhysics_CalcWheelDataOriginal(vehiclePhysics, vehicleParams)
		: 0;
	if (!vehiclePhysics || !vehicleParams)
		return result;

	float* params = reinterpret_cast<float*>(vehicleParams);
	bool patched = false;
	float frontTravel = 0.0f;
	float rearTravel = 0.0f;

	if (ReadFloat(vehiclePhysics + 244) <= 1.01f || ReadFloat(vehiclePhysics + 248) <= 1.01f)
	{
		const float frontWheelZ = (ReadFloat(vehiclePhysics + 228) + ReadFloat(vehiclePhysics + 232)) * 0.5f;
		frontTravel = DeriveSuspensionTravel(params[20], frontWheelZ);
		WriteFloat(vehiclePhysics + 244, frontTravel);
		WriteFloat(vehiclePhysics + 248, frontTravel);
		params[33] = frontTravel;
		patched = true;
	}

	if (ReadFloat(vehiclePhysics + 252) <= 1.01f || ReadFloat(vehiclePhysics + 256) <= 1.01f)
	{
		const float rearWheelZ = (ReadFloat(vehiclePhysics + 236) + ReadFloat(vehiclePhysics + 240)) * 0.5f;
		rearTravel = DeriveSuspensionTravel(params[49], rearWheelZ);
		WriteFloat(vehiclePhysics + 252, rearTravel);
		WriteFloat(vehiclePhysics + 256, rearTravel);
		params[62] = rearTravel;
		patched = true;
	}

	static int s_logBudget = 16;
	if (patched && s_logBudget > 0)
	{
		--s_logBudget;
		char buffer[256];
		_snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
			"R1Delta: vehicle patched collapsed suspension phys=%p front=%.3f rear=%.3f\n",
			reinterpret_cast<void*>(vehiclePhysics),
			frontTravel,
			rearTravel);
		OutputDebugStringA(buffer);
		Warning("%s", buffer);
	}

	return result;
}


static void InstallDriveableVehicleTransitionHooks(uintptr_t serverBase)
{
	constexpr uintptr_t kSetVehicleEntryAnimRva = 0x266270;
	constexpr uintptr_t kTurnOnRva = 0x269E50;
	constexpr uintptr_t kDriveVehicleRva = 0x269580;
	constexpr uintptr_t kCalcWheelDataRva = 0x13AF40;
	if (!serverBase
		|| !ModuleHasImageRva(serverBase, kSetVehicleEntryAnimRva, 0x40)
		|| !ModuleHasImageRva(serverBase, kTurnOnRva, 0x80)
		|| !ModuleHasImageRva(serverBase, kDriveVehicleRva, 0x380)
		|| !ModuleHasImageRva(serverBase, kCalcWheelDataRva, 0xD40))
		return;

	CPropVehicleDriveable_TurnOn = reinterpret_cast<CPropVehicleDriveableTurnOnType>(serverBase + kTurnOnRva);
	const uintptr_t target = serverBase + kSetVehicleEntryAnimRva;
	const uintptr_t driveTarget = serverBase + kDriveVehicleRva;
	const uintptr_t calcWheelDataTarget = serverBase + kCalcWheelDataRva;
	if (s_CPropVehicleDriveableSetVehicleEntryAnimTarget == target
		&& s_CPropVehicleDriveableDriveVehicleTarget == driveTarget
		&& s_CFourWheelVehiclePhysicsCalcWheelDataTarget == calcWheelDataTarget)
		return;

	const MH_STATUS createStatus = MH_CreateHook(
		reinterpret_cast<LPVOID>(target),
		&CPropVehicleDriveable_SetVehicleEntryAnim,
		reinterpret_cast<LPVOID*>(&CPropVehicleDriveable_SetVehicleEntryAnimOriginal));
	const MH_STATUS enableStatus = EnableCreatedHook(target, createStatus);
	if (enableStatus == MH_OK || enableStatus == MH_ERROR_ENABLED)
		s_CPropVehicleDriveableSetVehicleEntryAnimTarget = target;

	const MH_STATUS driveCreateStatus = MH_CreateHook(
		reinterpret_cast<LPVOID>(driveTarget),
		&CPropVehicleDriveable_DriveVehicle,
		reinterpret_cast<LPVOID*>(&CPropVehicleDriveable_DriveVehicleOriginal));
	const MH_STATUS driveEnableStatus = EnableCreatedHook(driveTarget, driveCreateStatus);
	if (driveEnableStatus == MH_OK || driveEnableStatus == MH_ERROR_ENABLED)
		s_CPropVehicleDriveableDriveVehicleTarget = driveTarget;

	const MH_STATUS calcWheelDataCreateStatus = MH_CreateHook(
		reinterpret_cast<LPVOID>(calcWheelDataTarget),
		&CFourWheelVehiclePhysics_CalcWheelData,
		reinterpret_cast<LPVOID*>(&CFourWheelVehiclePhysics_CalcWheelDataOriginal));
	const MH_STATUS calcWheelDataEnableStatus = EnableCreatedHook(calcWheelDataTarget, calcWheelDataCreateStatus);
	if (calcWheelDataEnableStatus == MH_OK || calcWheelDataEnableStatus == MH_ERROR_ENABLED)
		s_CFourWheelVehiclePhysicsCalcWheelDataTarget = calcWheelDataTarget;
	char buffer[384];
	_snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
		"R1Delta: vehicle hooks entry create=%d enable=%d target=%p original=%p drive create=%d enable=%d target=%p original=%p calcwheel create=%d enable=%d target=%p original=%p\n",
		static_cast<int>(createStatus),
		static_cast<int>(enableStatus),
		reinterpret_cast<void*>(target),
		reinterpret_cast<void*>(CPropVehicleDriveable_SetVehicleEntryAnimOriginal),
		static_cast<int>(driveCreateStatus),
		static_cast<int>(driveEnableStatus),
		reinterpret_cast<void*>(driveTarget),
		reinterpret_cast<void*>(CPropVehicleDriveable_DriveVehicleOriginal),
		static_cast<int>(calcWheelDataCreateStatus),
		static_cast<int>(calcWheelDataEnableStatus),
		reinterpret_cast<void*>(calcWheelDataTarget),
		reinterpret_cast<void*>(CFourWheelVehiclePhysics_CalcWheelDataOriginal));
	OutputDebugStringA(buffer);
	if ((createStatus != MH_OK && createStatus != MH_ERROR_ALREADY_CREATED)
		|| enableStatus != MH_OK
		|| (driveCreateStatus != MH_OK
			&& driveCreateStatus != MH_ERROR_ALREADY_CREATED)
		|| driveEnableStatus != MH_OK
		|| (calcWheelDataCreateStatus != MH_OK
			&& calcWheelDataCreateStatus != MH_ERROR_ALREADY_CREATED)
		|| calcWheelDataEnableStatus != MH_OK) {
		Warning("%s", buffer);
	}
}


static void HookR1ODedicatedMsvcAllocator(
	uintptr_t moduleBase,
	const char* moduleName,
	const MsvcAllocatorRvas& rvas)
{
	if (!IsR1ODedicatedServer() || !moduleBase || !MarkR1OAllocatorModuleHooked(moduleBase))
		return;

	if (!ModuleHasImageRva(moduleBase, rvas.freeRva, 0x40) ||
		!ModuleHasImageRva(moduleBase, rvas.mallocRva, 0x60) ||
		!ModuleHasImageRva(moduleBase, rvas.reallocRva, 0x90) ||
		!ModuleHasImageRva(moduleBase, rvas.callocRva, 0x80) ||
		!ModuleHasImageRva(moduleBase, rvas.recallocRva, 0xA0)) {
		char buffer[256];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: skipped R1O allocator hook for %s base=%p; RVA set does not fit image\n",
			moduleName ? moduleName : "<unknown>",
			reinterpret_cast<void*>(moduleBase));
		OutputDebugStringA(buffer);
		return;
	}

	MsvcCallocBaseFn originalCalloc = nullptr;
	MsvcMallocBaseFn originalMalloc = nullptr;
	MsvcReallocBaseFn originalRealloc = nullptr;
	MsvcRecallocBaseFn originalRecalloc = nullptr;
	MsvcFreeBaseFn originalFree = nullptr;

	const MH_STATUS callocStatus = MH_CreateHook(reinterpret_cast<LPVOID>(moduleBase + rvas.callocRva), &hkcalloc_base, reinterpret_cast<LPVOID*>(&originalCalloc));
	const MH_STATUS mallocStatus = MH_CreateHook(reinterpret_cast<LPVOID>(moduleBase + rvas.mallocRva), &hkmalloc_base, reinterpret_cast<LPVOID*>(&originalMalloc));
	const MH_STATUS reallocStatus = MH_CreateHook(reinterpret_cast<LPVOID>(moduleBase + rvas.reallocRva), &hkrealloc_base, reinterpret_cast<LPVOID*>(&originalRealloc));
	const MH_STATUS recallocStatus = MH_CreateHook(reinterpret_cast<LPVOID>(moduleBase + rvas.recallocRva), &hkrecalloc_base, reinterpret_cast<LPVOID*>(&originalRecalloc));
	const MH_STATUS freeStatus = MH_CreateHook(reinterpret_cast<LPVOID>(moduleBase + rvas.freeRva), &hkfree_base, reinterpret_cast<LPVOID*>(&originalFree));
	const MH_STATUS callocEnableStatus = EnableCreatedHook(moduleBase + rvas.callocRva, callocStatus);
	const MH_STATUS mallocEnableStatus = EnableCreatedHook(moduleBase + rvas.mallocRva, mallocStatus);
	const MH_STATUS reallocEnableStatus = EnableCreatedHook(moduleBase + rvas.reallocRva, reallocStatus);
	const MH_STATUS recallocEnableStatus = EnableCreatedHook(moduleBase + rvas.recallocRva, recallocStatus);
	const MH_STATUS freeEnableStatus = EnableCreatedHook(moduleBase + rvas.freeRva, freeStatus);

	MODULEINFO info = {};
	if (GetModuleInformation(GetCurrentProcess(), reinterpret_cast<HMODULE>(moduleBase), &info, sizeof(info))) {
		RegisterMsvcAllocatorFallbacks(
			moduleBase,
			info.SizeOfImage,
			moduleName,
			originalCalloc,
			originalMalloc,
			originalRealloc,
			originalRecalloc,
			originalFree);
	}

	char buffer[512];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: R1O allocator hooks %s base=%p create=[%d,%d,%d,%d,%d] enable=[%d,%d,%d,%d,%d] rvas=[%llx,%llx,%llx,%llx,%llx]\n",
		moduleName ? moduleName : "<unknown>",
		reinterpret_cast<void*>(moduleBase),
		static_cast<int>(callocStatus),
		static_cast<int>(mallocStatus),
		static_cast<int>(reallocStatus),
		static_cast<int>(recallocStatus),
		static_cast<int>(freeStatus),
		static_cast<int>(callocEnableStatus),
		static_cast<int>(mallocEnableStatus),
		static_cast<int>(reallocEnableStatus),
		static_cast<int>(recallocEnableStatus),
		static_cast<int>(freeEnableStatus),
		static_cast<unsigned long long>(rvas.callocRva),
		static_cast<unsigned long long>(rvas.mallocRva),
		static_cast<unsigned long long>(rvas.reallocRva),
		static_cast<unsigned long long>(rvas.recallocRva),
		static_cast<unsigned long long>(rvas.freeRva));
	OutputDebugStringA(buffer);
}

static void HookR1ODedicatedTfoAllocatorForLoadedModule(const wchar_t* name, USHORT nameLen, uintptr_t moduleBase)
{
	if (!IsR1ODedicatedServer())
		return;

	static constexpr MsvcAllocatorRvas kR1OEngineAllocator = {
		0x487868, 0x48509C, 0x4850FC, 0x496CC4, 0x48AA78
	};
	static constexpr MsvcAllocatorRvas kTfoFileSystemAllocator = {
		0x0E11C8, 0x0DE9FC, 0x0DEA5C, 0x0EC7A8, 0x0E1344
	};
	static constexpr MsvcAllocatorRvas kTfoDatacacheAllocator = {
		0x03AE48, 0x03871C, 0x03877C, 0x041274, 0x03AF94
	};
	static constexpr MsvcAllocatorRvas kTfoMaterialSystemAllocator = {
		0x12CDA4, 0x12D654, 0x12D6B4, 0x13A048, 0x12FCC0
	};
	static constexpr MsvcAllocatorRvas kTfoStudioRenderAllocator = {
		0x02C49C, 0x02C43C, 0x02C514, 0x030360, 0x02C3FC
	};
	static constexpr MsvcAllocatorRvas kTfoVPhysicsAllocator = {
		0x10DBAC, 0x10DB4C, 0x10DC24, 0x1139F8, 0x10DB0C
	};
	static constexpr MsvcAllocatorRvas kTfoLauncherAllocator = {
		0x093C78, 0x091620, 0x091680, 0x09B208, 0x093DC4
	};
	static constexpr MsvcAllocatorRvas kTfoServerLocalAllocator = {
		0x71E0BC, 0x71E99C, 0x71E9FC, 0x72B480, 0x721000
	};

	if (string_equal_size(name, nameLen, L"engine_r1o.dll")) {
		HookR1ODedicatedMsvcAllocator(moduleBase, "engine_r1o.dll", kR1OEngineAllocator);
	}
	else if (string_equal_size(name, nameLen, L"filesystem_stdio.dll")) {
		HookR1ODedicatedMsvcAllocator(moduleBase, "tfo-filesystem", kTfoFileSystemAllocator);
	}
	else if (string_equal_size(name, nameLen, L"datacache.dll")) {
		HookR1ODedicatedMsvcAllocator(moduleBase, "tfo-datacache", kTfoDatacacheAllocator);
	}
	else if (string_equal_size(name, nameLen, L"materialsystem_dx11.dll")) {
		HookR1ODedicatedMsvcAllocator(moduleBase, "tfo-materialsystem", kTfoMaterialSystemAllocator);
	}
	else if (string_equal_size(name, nameLen, L"studiorender.dll")) {
		HookR1ODedicatedMsvcAllocator(moduleBase, "tfo-studiorender", kTfoStudioRenderAllocator);
	}
	else if (string_equal_size(name, nameLen, L"vphysics.dll")) {
		HookR1ODedicatedMsvcAllocator(moduleBase, "tfo-vphysics", kTfoVPhysicsAllocator);
	}
	else if (string_equal_size(name, nameLen, L"launcher.dll")) {
		HookR1ODedicatedMsvcAllocator(moduleBase, "tfo-launcher", kTfoLauncherAllocator);
	}
	else if (string_equal_size(name, nameLen, L"server_local.dll") ||
		string_equal_size(name, nameLen, L"server.dll")) {
		HookR1ODedicatedMsvcAllocator(moduleBase, "tfo-server-local", kTfoServerLocalAllocator);
	}
}

static FORCEINLINE void
do_server(const LDR_DLL_NOTIFICATION_DATA* notification_data)
{
	auto server_base = (uintptr_t)notification_data->Loaded.DllBase;
	G_server = server_base;
	auto vscript_base = G_vscript;

	const bool legacyDedicated = UsesLegacyDedicatedEngine();
	const uintptr_t engine_base_spec = MainEngineBase();

	LDR_DLL_LOADED_NOTIFICATION_DATA* ndata = GetModuleNotificationData(L"vstdlib");
	doBinaryPatchForFile(*ndata);
	FreeModuleNotificationData(ndata);
	ResolveSetConvarString();
	uintptr_t vTableAddr = server_base + 0x807220;

	if (!IsR1ODedicatedServer()) {
		RemoveItemsFromVTable(vTableAddr, 35, 2);
		if (IsDedicatedServer())
			RemoveItemsFromVTable(vTableAddr, 61, 1);
	}
	if (!IsR1ODedicatedServer())
		MH_CreateHook((LPVOID)(server_base + 0x143A10), &CServerGameDLL__DLLInit, (LPVOID*)&CServerGameDLL__DLLInitOriginal);
	if (!IsR1ODedicatedServer())
		HookMsvcAllocator(server_base, 0x71E0BC, 0x71E99C, 0x71E9FC, 0x72B480, 0x721000);
	InstallServerModelBoneGuards(server_base);
	InstallDriveableVehicleTransitionHooks(server_base);
	InstallServerUserCmdHooks(server_base);
	//MH_CreateHook((LPVOID)(server_base + 0x364D00), &CAI_NetworkManager__LoadNavMesh, reinterpret_cast<LPVOID*>(&CAI_NetworkManager__LoadNavMeshOriginal));
	if (!IsR1ODedicatedServer()) {
		MH_CreateHook((LPVOID)(vscript_base + (legacyDedicated ? 0x1210 : 0x1210)), &CScriptManager__CreateNewVM, reinterpret_cast<LPVOID*>(&CScriptManager__CreateNewVMOriginal));
		MH_CreateHook((LPVOID)(vscript_base + (legacyDedicated ? 0x1640 : 0x1630)), &CScriptVM__GetUnknownVMPtr, reinterpret_cast<LPVOID*>(&CScriptVM__GetUnknownVMPtrOriginal));
		MH_CreateHook((LPVOID)(vscript_base + (legacyDedicated ? 0x1600 : 0x15F0)), &CScriptManager__DestroyVM, reinterpret_cast<LPVOID*>(&CScriptManager__DestroyVMOriginal));
	}
	InitVStdLibICVarFactoryHook();
	// Listen, legacy dedicated, and R1O fake-dedicated modes all load the same
	// TFO server/server_local binary. The R1 client RecvTables are expanded in
	// every client process, so keep the shared server SendTables and storage in
	// lockstep in every hosting mode as well.
	InstallTFOServerPlayerResource18(server_base);
	if (IsR1ODedicatedServer()) {
		// R1O fake-dedi uses the TFO server_local.dll, so the server-local hook
		// set below still applies. Keep engine_ds-specific hooks guarded.
		OutputDebugStringA("R1Delta: installing R1O fake-dedi server-local hooks\n");
	}
	MH_CreateHook((LPVOID)(server_base + 0x507560), &ServerClassInit_DT_BasePlayer, reinterpret_cast<LPVOID*>(&ServerClassInit_DT_BasePlayerOriginal));
	MH_CreateHook((LPVOID)(server_base + 0x51DFE0), &ServerClassInit_DT_Local, reinterpret_cast<LPVOID*>(&ServerClassInit_DT_LocalOriginal));
	MH_CreateHook((LPVOID)(server_base + 0x5064F0), &ServerClassInit_DT_LocalPlayerExclusive, reinterpret_cast<LPVOID*>(&ServerClassInit_DT_LocalPlayerExclusiveOriginal));
	MH_CreateHook((LPVOID)(server_base + 0x593270), &ServerClassInit_DT_TitanSoul, reinterpret_cast<LPVOID*>(&ServerClassInit_DT_TitanSoulOriginal));
	MH_CreateHook((LPVOID)(server_base + 0x629740), &UserMessage_ReorderHook, reinterpret_cast<LPVOID*>(&UserMessage_ReorderHook_Original));
	MH_CreateHook((LPVOID)(server_base + 0x3A1EC0), &CBaseEntity__SendProxy_CellOrigin, reinterpret_cast<LPVOID*>(NULL));
	MH_CreateHook((LPVOID)(server_base + 0x3A2020), &CBaseEntity__SendProxy_CellOriginXY, reinterpret_cast<LPVOID*>(NULL));
	MH_CreateHook((LPVOID)(server_base + 0x3A2130), &CBaseEntity__SendProxy_CellOriginZ, reinterpret_cast<LPVOID*>(NULL));
	MH_CreateHook((LPVOID)(server_base + 0x4E2F30), &CPlayer_GetLevel, reinterpret_cast<LPVOID*>(NULL));
	MH_CreateHook((LPVOID)(server_base + 0x1442D0), &CServerGameDLL_DLLShutdown, reinterpret_cast<LPVOID*>(NULL));
	MH_CreateHook((LPVOID)(server_base + 0x1532A0), &WallrunMove_BlockForTitans, reinterpret_cast<LPVOID*>(&WallrunMove_BlockForTitans_Original));
	MH_CreateHook((LPVOID)(server_base + 0x21B6B0), &HookedGetRankFunction, NULL);
	MH_CreateHook((LPVOID)(server_base + 0x50B8B0), &HookedSetRankFunction, NULL);

	// do_server runs from the DLL notification after the earlier global
	// MH_EnableHook call. Hooks created here must be enabled immediately.
	// server.dll and the R1O server_local.dll are the same supported binary,
	// but keep exact prologue checks so an unsupported revision fails closed.
	const unsigned char expectedUTILLogPrintfPrologue[] = {
		0x48, 0x8B, 0xC4, 0x48, 0x89, 0x48, 0x08, 0x48,
		0x89, 0x50, 0x10, 0x4C, 0x89, 0x40, 0x18, 0x4C
	};
	InstallEngineCommandLineCheckedHook(
		server_base,
		0x25E290,
		expectedUTILLogPrintfPrologue,
		sizeof(expectedUTILLogPrintfPrologue),
		reinterpret_cast<void*>(&UTIL_LogPrintf),
		reinterpret_cast<void**>(&oUTIL_LogPrintf),
		"server logging",
		"UTIL_LogPrintf");

	const unsigned char expectedOnSayTextMsgPrologue[] = {
		0x85, 0xD2, 0x0F, 0x8E, 0x56, 0x03, 0x00, 0x00,
		0x48, 0x8B, 0xC4, 0x4C, 0x89, 0x40, 0x18, 0x57
	};
	InstallEngineCommandLineCheckedHook(
		server_base,
		0x148730,
		expectedOnSayTextMsgPrologue,
		sizeof(expectedOnSayTextMsgPrologue),
		reinterpret_cast<void*>(&CServerGameDLL_OnSayTextMsg),
		reinterpret_cast<void**>(&oCServerGameDLL_OnSayTextMsg),
		"server chat",
		"CServerGameDLL_OnSayTextMsg");
	
	if (IsR1ODedicatedServer()) {
		const unsigned char expectedR1OGetNetworkIDStringPrologue[] = {
			0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
			0xEC, 0x40, 0x48, 0x8B, 0x01, 0x48, 0x8D, 0x3D,
			0x24, 0xBB, 0x41, 0x00, 0x48, 0x8B, 0xD9, 0x48
		};
		InstallEngineCommandLineCheckedHook(
			G_engine_r1o,
			0x140CA0,
			expectedR1OGetNetworkIDStringPrologue,
			sizeof(expectedR1OGetNetworkIDStringPrologue),
			reinterpret_cast<void*>(&R1OGetNetworkIDStringHook),
			reinterpret_cast<void**>(&R1OGetNetworkIDStringOriginal),
			"TFO authentication",
			"CBaseClient::GetNetworkIDString");
	}
	else if (IsDedicatedServer()) {
		MH_CreateHook((LPVOID)(G_engine_ds + 0x45EB0), &GetUserIDStringHook, reinterpret_cast<LPVOID*>(&GetUserIDStringOriginal));
		MH_CreateHook((LPVOID)(G_engine_ds + 0x46080), &GetUserIDHook, reinterpret_cast<LPVOID*>(&GetUserIDOriginal));
	}
	else {
		MH_CreateHook((LPVOID)(engine_base_spec + 0xD5260), &GetUserIDStringHook, reinterpret_cast<LPVOID*>(&GetUserIDStringOriginal));
		MH_CreateHook((LPVOID)(engine_base_spec + 0xD5430), &GetUserIDHook, reinterpret_cast<LPVOID*>(&GetUserIDOriginal));
	}
	if (!IsDedicatedServer()) {
		MH_CreateHook((LPVOID)(engine_base_spec + 0x1E2930), &CNetChan__SendDatagramLISTEN_Part2_Hook, reinterpret_cast<LPVOID*>(&oCNetChan__SendDatagramLISTEN_Part2));
	}
	
	MH_CreateHook((LPVOID)(server_base + 0x18760), &dynamic_initializer_for__prop_dynamic__, reinterpret_cast<LPVOID*>(&odynamic_initializer_for__prop_dynamic__));

	//MH_CreateHook((LPVOID)(server_base + 0x7F7E0), &HookedServerClassRegister, reinterpret_cast<LPVOID*>(&ServerClassRegister_7F7E0));
	//MH_CreateHook((LPVOID)(server_base + 0x25A8E0), &CEntityFactoryDictionary__Create, reinterpret_cast<LPVOID*>(&CEntityFactoryDictionary__CreateOriginal));

	const bool ainDumpLoaded = HasEngineCommandLineFlag("-r1delta_dump_loaded_ain");
	const bool ainDumpRebuilt = HasEngineCommandLineFlag("-r1delta_dump_rebuilt_ain");
	const bool ainRebuildOnLoad = HasEngineCommandLineFlag("-r1delta_rebuild_ain_on_load");
	const bool ainNoExisting = HasEngineCommandLineFlag("-r1delta_build_ain_no_existing")
		|| HasEngineCommandLineFlag("-r1delta_no_existing_ain");

	if (ainDumpLoaded || ainDumpRebuilt || ainRebuildOnLoad || ainNoExisting) {
		char flagBuffer[256];
		_snprintf_s(flagBuffer, sizeof(flagBuffer), _TRUNCATE,
			"buildain: flags dumpLoaded=%d dumpRebuilt=%d rebuild=%d noExisting=%d\n",
			ainDumpLoaded ? 1 : 0,
			ainDumpRebuilt ? 1 : 0,
			ainRebuildOnLoad ? 1 : 0,
			ainNoExisting ? 1 : 0);
		OutputDebugStringA(flagBuffer);
		Warning("%s", flagBuffer);

		uintptr_t target = server_base + 0x3667D0;
		const MH_STATUS createStatus = MH_CreateHook((LPVOID)target, &CAI_NetworkManager__DelayedInit, reinterpret_cast<LPVOID*>(&CAI_NetworkManager__DelayedInitOriginal));
		const MH_STATUS enableStatus = EnableCreatedHook(target, createStatus);
		char buffer[256];
		_snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
			"buildain: delayed-init hook create=%d enable=%d target=%p original=%p\n",
			static_cast<int>(createStatus),
			static_cast<int>(enableStatus),
			reinterpret_cast<void*>(target),
			reinterpret_cast<void*>(CAI_NetworkManager__DelayedInitOriginal));
		OutputDebugStringA(buffer);
		Warning("%s", buffer);

		uintptr_t dynamicLinkTarget = server_base + 0x337E80;
		const MH_STATUS dynamicLinkCreateStatus = MH_CreateHook(
			(LPVOID)dynamicLinkTarget,
			&CAI_DynamicLink__InitDynamicLinks,
			reinterpret_cast<LPVOID*>(&CAI_DynamicLink__InitDynamicLinksOriginal));
		const MH_STATUS dynamicLinkEnableStatus = EnableCreatedHook(dynamicLinkTarget, dynamicLinkCreateStatus);
		char dynamicLinkBuffer[256];
		_snprintf_s(dynamicLinkBuffer, sizeof(dynamicLinkBuffer), _TRUNCATE,
			"buildain: dynamic-link-init hook create=%d enable=%d target=%p original=%p\n",
			static_cast<int>(dynamicLinkCreateStatus),
			static_cast<int>(dynamicLinkEnableStatus),
			reinterpret_cast<void*>(dynamicLinkTarget),
			reinterpret_cast<void*>(CAI_DynamicLink__InitDynamicLinksOriginal));
		OutputDebugStringA(dynamicLinkBuffer);
		Warning("%s", dynamicLinkBuffer);
	}
	if (ainNoExisting) {
		struct AINHookInstall {
			uintptr_t rva;
			void* hook;
			void** original;
			const char* name;
		};
		AINHookInstall hooks[] = {
			{ 0x364A70, reinterpret_cast<void*>(&CAI_NetworkManager__OpenAINFile), reinterpret_cast<void**>(&CAI_NetworkManager__OpenAINFileOriginal), "open-existing" },
			{ 0x364D00, reinterpret_cast<void*>(&CAI_NetworkManager__LoadNavMesh), reinterpret_cast<void**>(&CAI_NetworkManager__LoadNavMeshOriginal), "load-existing" },
		};

		for (const AINHookInstall& hook : hooks) {
			uintptr_t target = server_base + hook.rva;
			const MH_STATUS createStatus = MH_CreateHook((LPVOID)target, hook.hook, reinterpret_cast<LPVOID*>(hook.original));
			const MH_STATUS enableStatus = EnableCreatedHook(target, createStatus);
			char buffer[256];
			_snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
				"buildain: %s hook create=%d enable=%d target=%p original=%p\n",
				hook.name,
				static_cast<int>(createStatus),
				static_cast<int>(enableStatus),
				reinterpret_cast<void*>(target),
				*hook.original);
			OutputDebugStringA(buffer);
			Warning("%s", buffer);
		}
	}
	//MH_CreateHook((LPVOID)(server_base + 0x36BC30), &sub_36BC30, reinterpret_cast<LPVOID*>(&sub_36BC30Original));
	//MH_CreateHook((LPVOID)(server_base + 0x36C150), &sub_36C150, reinterpret_cast<LPVOID*>(&sub_36C150Original));
	//MH_CreateHook((LPVOID)(server_base + 0x3669C0), &CAI_NetworkManager__FixupHints, reinterpret_cast<LPVOID*>(&CAI_NetworkManager__FixupHintsOriginal));
	//MH_CreateHook((LPVOID)(server_base + 0x31CE90), &unkallocfunc, reinterpret_cast<LPVOID*>(&unkallocfuncoriginal));
	//MH_CreateHook((LPVOID)(server_base + 0x25A8E0), &CEntityFactoryDictionary__Create, reinterpret_cast<LPVOID*>(&CEntityFactoryDictionary__CreateOriginal));
	//MH_CreateHook((LPVOID)(server_base + 0x363A50), &sub_363A50, reinterpret_cast<LPVOID*>(&sub_363A50Original));
	auto engine_base = G_engine;
	MH_CreateHook((LPVOID)(server_base + 0x3BE1A0), &CC_Ent_Create, reinterpret_cast<LPVOID*>(&oCC_Ent_Create));
	MH_CreateHook((LPVOID)(server_base + 0x25E340), &DispatchSpawn, reinterpret_cast<LPVOID*>(&oDispatchSpawn));
	MH_CreateHook((LPVOID)(server_base + 0x2820A0), &HandleSquirrelClientCommand, reinterpret_cast<LPVOID*>(&oHandleSquirrelClientCommand));
	//MH_CreateHook((LPVOID)(server_base + 0x364140), &DebugConnectMsg, reinterpret_cast<LPVOID*>(0));
	if (IsR1ODedicatedServer()) {
		OutputDebugStringA("R1Delta: deferring R1O fake-dedi cvar/concommand registration until wrapped cvar is ready\n");
	}
	else {
		RegisterServerUserCmdConVars();
		RegisterConCommand("updatescriptdata", updatescriptdata_cmd, "Dumps the script data in the AI node graph to disk", FCVAR_CHEAT);
		RegisterConCommand("verifyain", verifyain_cmd, "Reads the .ain file from disk, compares its nodes & links to in-memory data, logs differences.", FCVAR_CHEAT);
		RegisterConCommand("updateain", updateain_cmd, "Runs the reconstructed AIN full build, then writes a generated .ain dump without replacing the source file.", FCVAR_CHEAT);
		RegisterConCommand("bot_dummy", AddBotDummyConCommand, "Adds a bot.", FCVAR_GAMEDLL | FCVAR_CHEAT);
		if (IsDedicatedServer())
			RegisterConCommand("find", Find, "Find a command or convar.", FCVAR_NONE);
		RegisterConVar("delta_ms_url", "ms.r1delta.net", FCVAR_CLIENTDLL, "Url for r1d masterserver");
		RegisterConVar("delta_server_auth_token", "", FCVAR_USERINFO | FCVAR_SERVER_CANNOT_QUERY | FCVAR_DONTRECORD | FCVAR_PROTECTED | FCVAR_HIDDEN, "Per-server auth token");
		RegisterConVar("delta_version", R1D_VERSION, FCVAR_USERINFO | FCVAR_DONTRECORD, "R1Delta version number");
		RegisterConVar("delta_skip_version_check", "0", FCVAR_GAMEDLL, "Skip version check for connecting clients (sets server to dev mode)");
		RegisterConVar("delta_persistent_master_auth_token", "DEFAULT", FCVAR_ARCHIVE | FCVAR_SERVER_CANNOT_QUERY | FCVAR_DONTRECORD | FCVAR_PROTECTED | FCVAR_HIDDEN, "Persistent master server authentication token");
		RegisterConVar("delta_persistent_master_auth_token_failed_reason", "", FCVAR_ARCHIVE | FCVAR_SERVER_CANNOT_QUERY | FCVAR_DONTRECORD | FCVAR_PROTECTED | FCVAR_HIDDEN, "Persistent master server authentication token");
		RegisterConVar("delta_online_auth_enable", "0", FCVAR_GAMEDLL, "Whether to use master server auth");
		RegisterConVar("delta_discord_username_sync", "0", FCVAR_GAMEDLL, "Controls if player names are synced with Discord: 0=Off,1=Norm,2=Pomelo");
		RegisterConVar("riff_floorislava", "0", FCVAR_HIDDEN, "Enable floor is lava mode");
		const auto hudwarpConVars = r1delta::hudwarp::RegisterRuntimeConVars<ConVarR1>(
			[](const char* name, const char* value, int flags, const char* helpString) {
				return RegisterConVar(name, value, flags, helpString);
			},
			FCVAR_ARCHIVE);
		R1DAssert(hudwarpConVars.useGpu);
		R1DAssert(hudwarpConVars.disable);
		RegisterConVar("hide_server", "0", FCVAR_NONE, "Whether the server should be hidden from the master server");
		RegisterConVar("server_description", "", FCVAR_NONE, "Server description");
		RegisterConVar("delta_ui_server_filter", "0", FCVAR_NONE, "Script managed vgui filter convar");
		RegisterConVar("delta_autoBalanceTeams", "1", FCVAR_NONE, "Whether to autobalance teams on death/private match/lobby start. Managed by script");
		RegisterConVar("delta_useLegacyProgressBar", "0", FCVAR_ARCHIVE, "Whether or not to use the old loading bar");
		RegisterConVar("delta_return_to_lobby", "1", FCVAR_NONE, "Return to lobby after a game");
		RegisterConVar("delta_allow_empty_server", "1", FCVAR_NONE, "Allow matches with no players.");
		RegisterConVar("delta_skip_waiting_for_players", "0", FCVAR_NONE, "Skip waiting for players.");
		CBanSystem::m_pSvBanlistAutosave = RegisterConVar("sv_banlist_autosave", "1", FCVAR_ARCHIVE, "Automatically save ban lists after modification commands.");
		RegisterConCommand("script", script_cmd, "Execute Squirrel code in server context", FCVAR_GAMEDLL | FCVAR_CHEAT);
		if (!IsDedicatedServer()) {
			RegisterConCommand("script_client", script_client_cmd, "Execute Squirrel code in client context", FCVAR_NONE);
			RegisterConCommand("script_ui", script_ui_cmd, "Execute Squirrel code in UI context", FCVAR_NONE);
			RegisterConCommand("noclip", noclip_cmd, "Toggles NOCLIP mode.", FCVAR_GAMEDLL | FCVAR_CHEAT);
		}

		RegisterConVar("bot_kick_on_death", "1", FCVAR_GAMEDLL | FCVAR_CHEAT, "Enable/disable bots getting kicked on death.");
		RegisterConVar("delta_vote_allowed", "1", FCVAR_GAMEDLL | FCVAR_REPLICATED, "Allow voting?"); //sv_allow_votes
		RegisterConVar("delta_vote_timer_duration", "12.0", FCVAR_GAMEDLL | FCVAR_REPLICATED, "How long to allow voting on an issue");
		RegisterConVar("delta_vote_failure_timer", "300.0", FCVAR_GAMEDLL | FCVAR_REPLICATED, "A vote that fails cannot be re-submitted for this long");
		RegisterConVar("delta_vote_creation_timer", "150.0", FCVAR_GAMEDLL | FCVAR_REPLICATED, "How long before a player can attempt to call another vote (in seconds).");
		RegisterConVar("delta_vote_holder_may_vote_no", "1", FCVAR_GAMEDLL | FCVAR_REPLICATED, "1 = Vote caller is not forced to vote yes on yes/no votes.");
		RegisterConVar("delta_vote_next_map", "", FCVAR_GAMEDLL | FCVAR_REPLICATED, "Next voted map.");
		RegisterConVar("delta_vote_next_mode", "", FCVAR_GAMEDLL | FCVAR_REPLICATED, "Next voted gamemode.");
	}
			

	//auto allTalk = OriginalCCVar_FindVar(cvarinterface, "sv_alltalk");

	//allTalk->m_nFlags |= FCVAR_REPLICATED;

	//0x0000415198 on dedicated
	// 0x0620818 on client

	if (IsDedicatedServer() && !IsR1ODedicatedServer()) {
		MH_CreateHook((LPVOID)(G_engine_ds + 0x45530), &HookedCBaseClientSetName, reinterpret_cast<LPVOID*>(&CBaseClientSetNameOriginal));
		MH_CreateHook((LPVOID)(G_engine_ds + 0x491A0), &HookedCBaseClientConnect, reinterpret_cast<LPVOID*>(&oCBaseClientConnect));
	}
	else if (!IsDedicatedServer()) {
		MH_CreateHook((LPVOID)(G_engine + 0xD4840), &HookedCBaseClientSetName, reinterpret_cast<LPVOID*>(&CBaseClientSetNameOriginal));
		MH_CreateHook((LPVOID)(G_engine + 0xD7DC0), &HookedCBaseClientConnect, reinterpret_cast<LPVOID*>(&oCBaseClientConnect));
		MH_CreateHook((LPVOID)(G_engine + 0x2AA90), &HookedCBaseStateClientConnect, reinterpret_cast<LPVOID*>(&oCBaseStateClientConnect));
	}

	//MH_CreateHook((LPVOID)(server_base + 0x364140), &sub_364140, reinterpret_cast<LPVOID*>(NULL));
	//MH_CreateHook((LPVOID)(server_base + 0xED7A0), &WeaponXRegisterServer, reinterpret_cast<LPVOID*>(&oWeaponXRegisterServer));

	//MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("vphysics.dll") + 0x257E0), &sub_1800257E0, reinterpret_cast<LPVOID*>(&sub_1800257E0Original));
	//MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("vphysics.dll") + 0xE77F0), &IVP_Environment__set_delta_PSI_time, reinterpret_cast<LPVOID*>(&IVP_Environment__set_delta_PSI_timeOriginal));
	//MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("vphysics.dll") + 0x31610), &sub_180031610, reinterpret_cast<LPVOID*>(&sub_180031610Original));
	MH_CreateHook((LPVOID)(server_base + 0x554660), &CPortal_Player__ChangeTeam, reinterpret_cast<LPVOID*>(&oCPortal_Player__ChangeTeam));
	//MH_CreateHook((LPVOID)(engine_base + 0x0284C0), &SVC_UserMessage__Process, reinterpret_cast<LPVOID*>(&oSVC_UserMessage__Process));
	//MH_CreateHook((LPVOID)(engine_base + 0x1FFA20), &SVC_UserMessage__ReadFromBuffer, reinterpret_cast<LPVOID*>(&oSVC_UserMessage__ReadFromBuffer));
	//MH_CreateHook((LPVOID)(engine_base + 0x1FBF70), &SVC_UserMessage__WriteToBuffer, reinterpret_cast<LPVOID*>(&oSVC_UserMessage__WriteToBuffer));

	//MH_CreateHook((LPVOID)(server_base + 0x5FC370), &mp_weapon_wingman_ctor_hk, reinterpret_cast<LPVOID*>(&mp_weapon_wingman_ctor_orig));
	//MH_CreateHook((LPVOID)(server_base + 0x605570), &mp_weapon_wingman_dtor_hk, reinterpret_cast<LPVOID*>(&mp_weapon_wingman_dtor_orig));

	if (!IsDedicatedServer()) {
		auto launcher = G_launcher;
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("engine.dll") + 0x471980), &StringCompare_AllTalkHookDedi, reinterpret_cast<LPVOID*>(&oStringCompare_AllTalkHookDedi));

		MH_CreateHook((LPVOID)(engine_base_spec + 0x136860), &Status_ConMsg, NULL);
		MH_CreateHook((LPVOID)(engine_base_spec + 0x1BF500), &Status_ConMsg, NULL);
		//MH_CreateHook((LPVOID)(engine_base_spec + 0x4735A0), &sub_1804735A0, NULL);
		MH_CreateHook((LPVOID)(engine_base_spec + 0x8E6D0), &Status_ConMsg, NULL);
		MH_CreateHook((LPVOID)(engine_base_spec + 0x22610), &Status_ConMsg, NULL);
		MH_CreateHook((LPVOID)(engine_base_spec + 0x55C00), &CL_Retry_f, reinterpret_cast<LPVOID*>(&CL_Retry_fOriginal));
		MH_CreateHook((LPVOID)(engine_base_spec + 0x8EAF0), &Con_ColorPrintf, NULL);
		MH_CreateHook((LPVOID)(launcher + 0xB6F0), &CSquirrelVM__PrintFunc2, NULL);
		MH_CreateHook((LPVOID)(launcher + 0xB7A0), &CSquirrelVM__PrintFunc3, NULL);
		MH_CreateHook((LPVOID)(engine_base + 0x23E20), &SVC_Print_Process_Hook, NULL);
		MH_CreateHook((LPVOID)(engine_base + 0x22DD0), &CBaseClientState__InternalProcessStringCmd, reinterpret_cast<LPVOID*>(&CBaseClientState__InternalProcessStringCmdOriginal));
		MH_CreateHook((LPVOID)(engine_base + 0x72360), &cl_DumpPrecacheStats, NULL);

		//MH_CreateHook((LPVOID)(engine_base_spec + 0x473550), &sub_180473550, NULL);

		//MH_CreateHook((LPVOID)(engine_base_spec + 0x1168B0), &COM_StringCopy, reinterpret_cast<LPVOID*>(&COM_StringCopyOriginal));
		//MH_CreateHook((LPVOID)(engine_base_spec + 0x1C79A0), &DataTable_SetupReceiveTableFromSendTable, reinterpret_cast<LPVOID*>(&DataTable_SetupReceiveTableFromSendTableOriginal));
	}
	// R1O fake-dedi's G_vscript is the TFO launcher, whose server print
	// function is launcher+0x254F0 and is installed by
	// InstallR1OTFOSquirrelHooks. The legacy R1 dedicated RVA below lands in
	// the middle of an unrelated TFO instruction, so it must never be applied
	// to the fake-dedi launcher.
	if (!IsR1ODedicatedServer())
		MH_CreateHook((LPVOID)(G_vscript + (IsDedicatedServer() ? 0x0B660 : 0xB640)), &CSquirrelVM__PrintFunc1, NULL);
	if (!IsR1ODedicatedServer()) {
		void* ret = reinterpret_cast<void*>((reinterpret_cast<CreateInterfaceFn>(GetProcAddress(GetModuleHandleA("vstdlib.dll"), "CreateInterface"))("VEngineCvar007", 0)));
		auto v = (decltype(&OriginalCCVar_FindCommand)((*(void***)ret))); // Assuming OriginalCCVar_FindVar is defined elsewhere
		auto findcmdptr = v[17];
		MH_CreateHook(findcmdptr((uintptr_t)ret, "banip")->m_pCommandCallback, &CBanSystem::addip, NULL);
		MH_CreateHook(findcmdptr((uintptr_t)ret, "addip")->m_pCommandCallback, &CBanSystem::addip, NULL);
		MH_CreateHook(findcmdptr((uintptr_t)ret, "removeip")->m_pCommandCallback, &CBanSystem::removeip, NULL);
		MH_CreateHook(findcmdptr((uintptr_t)ret, "listip")->m_pCommandCallback, &CBanSystem::listip, NULL);
		MH_CreateHook(findcmdptr((uintptr_t)ret, "writeip")->m_pCommandCallback, &CBanSystem::writeip, NULL);
		MH_CreateHook(findcmdptr((uintptr_t)ret, "writeid")->m_pCommandCallback, &CBanSystem::writeid, NULL);
		RegisterConCommand("removeallids", &CBanSystem::removeallids, "Remove all user IDs from the ban list.", 0);
		RegisterConCommand("removeallips", &CBanSystem::removeallips, "Remove all IPs from the ban list.", 0);
		MH_CreateHook(findcmdptr((uintptr_t)ret, "removeid")->m_pCommandCallback, &CBanSystem::removeid, NULL);
		MH_CreateHook(findcmdptr((uintptr_t)ret, "listid")->m_pCommandCallback, &CBanSystem::listid, NULL);
		MH_CreateHook(findcmdptr((uintptr_t)ret, "banid")->m_pCommandCallback, &CBanSystem::banid, NULL);
		MH_CreateHook(findcmdptr((uintptr_t)ret, "kickid")->m_pCommandCallback, &CBanSystem::kickid, NULL);
		MH_CreateHook(findcmdptr((uintptr_t)ret, "kick")->m_pCommandCallback, &CBanSystem::kick, NULL);
	}
	if (IsDedicatedServer() && !IsR1ODedicatedServer()) {
		MH_CreateHook((LPVOID)(G_engine_ds + 0x6ABF0), &CBanSystem::RemoteAccess_GetUserBanList, NULL);
	}
	else if (!IsDedicatedServer()) {
		MH_CreateHook((LPVOID)(G_engine + 0xF9BB0), &CBanSystem::RemoteAccess_GetUserBanList, NULL);
	}

	//MH_CreateHook((LPVOID)(engine_base_spec + 0x1C79A0), &sub_1801C79A0, reinterpret_cast<LPVOID*>(&sub_1801C79A0Original));
	//
	//
	//diMH_CreateHook((LPVOID)(engine_base_spec + 0x1D9E70), &MatchRecvPropsToSendProps_R, reinterpret_cast<LPVOID*>(NULL));
	//MH_CreateHook((LPVOID)(engine_base_spec + 0x217C30), &sub_180217C30, NULL);
	// Cast the function pointer to the function at 0x4E80

	// Fix precache start
	// Rebuild CHL2_Player's precache to take our stuff into account
	MH_CreateHook(LPVOID(server_base + 0x41E070), &CHL2_Player_Precache, 0);

	if (!IsR1ODedicatedServer())
		security_fixes_server(engine_base, server_base);
	R1DAssert(MH_EnableHook(MH_ALL_HOOKS) == MH_OK);
	//std::cout << "did hooks" << std::endl;
}


struct ShaderParamInfo
{
	const char* m_name;
	int dword_8;
	int m_nFlags;
	const char* m_defaultVal;
	const char* m_helpText;
	uint64_t qword_20;
};

struct WaterShader
{
	struct WaterShaderVtbl
	{
		const char* (__fastcall* GetName)(WaterShader*);
		int(__fastcall* GetParamCount)(WaterShader*);
		ShaderParamInfo* (__fastcall* GetParamInfo)(WaterShader*, int);
	};
	WaterShaderVtbl* vtbl;
};

struct CTexture
{
	struct CTexture_vtbl
	{
		const char* (__fastcall* GetName)(CTexture* thisptr);
	}*vtbl;
};


struct waterShaderData
{
	_DWORD dword_0;
	_DWORD dword_4;
	_BYTE gap_8[16];
	unsigned int uint_18;
	_BYTE gap_1C[12];
	CTexture* qword_28;
	_BYTE gap_30[24];
	CTexture* pqword_48;
	CTexture* qword_50;
	float reflectRefractScale1;
	float float_5C;
	float float_60;
	float float_64;
	float reflectRefractScale0;
	float reflectTint[3];
	CTexture* qword_78;
	_QWORD qword_80;
	unsigned int uint_88;
	float bumpTexCoordTransformRotateX[2];
	float bumpTexCoordTransformRotateY[2];
	float bumpTexCoordTransformTranslate[2];
	_BYTE gap_A4[8];
	float waterFogColor[3];
	_BYTE gap_B8[20];
	_DWORD dword_CC;
	float float_D0;
	float mulitTexScrollRate0[2];
	float mulitTexScrollRate1[2];
	_DWORD dword_E4;
	CTexture* qword_E8;
	CTexture* qword_F0;
	unsigned int uint_F8;
	float float_FC;
	float float_100;
	float float_104;
	float float_108;
	float float_10C;
	float float_110;
	float colorFlowTexCoordScaleInverse;
	float colorFlowCyclePeriod;
	float float_11C;
	float float_120;
	float float_124;
	float float_128;
	float float_12C;
	float float_130;
	_DWORD dword_134;
};

struct struct_a3_water
{
	_BYTE gap0[8];
	_QWORD qword8;
	_BYTE gap10[104];
	_QWORD qword78;
};

struct shaderInitData
{
	_DWORD dword0;
	_DWORD dword4;
	_QWORD qword8;
	_QWORD qword10;
	const char* shaderName[6];
	_DWORD staticComboIds[6];
};


using InitWaterShader_t = int64_t(__fastcall*)(WaterShader* a1, waterShaderData** a2, struct_a3_water* a3, shaderInitData* a4);
static auto oInitWaterShader = (InitWaterShader_t)(G_matsystem + 0x32490);

int64_t __fastcall InitWaterShader_41B50(WaterShader* a1, waterShaderData** a2, struct_a3_water* a3, shaderInitData* a4) {

	//for (int i = 0; i < a1->vtbl->GetParamCount(a1); i++)
	//{
	//	ShaderParamInfo* info = a1->vtbl->GetParamInfo(a1, i);
	//	//spdlog::info("Param index \"{}\" name \"{}\" type \"{}\"", i, info->m_name, info->dword_8);
	//	Msg("Param index \"%d\" name \"%s\" type \"%d\", flags: %d, defaultValue \"%s\", helpText \"%s\"\n", i, info->m_name, info->dword_8,info->m_nFlags,info->m_defaultVal,info->m_helpText);
	//}

	auto res = oInitWaterShader(a1, a2, a3, a4);
	Msg("--------------------------------\n");
	Msg("Water vertex combo: %d\n", a4->staticComboIds[1]);
	Msg("Water pixel combo: %d \n", a4->staticComboIds[0]);
	return res;
}


static bool should_init_security_fixes = false;
void __stdcall LoaderNotificationCallback(
	unsigned long notification_reason,
	const LDR_DLL_NOTIFICATION_DATA* notification_data,
	void* context) {
	if (notification_reason != LDR_DLL_NOTIFICATION_REASON_LOADED)
		return;

	ZoneScoped;
#if BUILD_PROFILE
	if (ZoneIsActive)
	{
		extern char* WideToStringArena(Arena * arena, const std::wstring_view & wide);
		auto arena = tctx.get_arena_for_scratch();
		auto temp = TempArena(arena);

		auto s = WideToStringArena(arena, std::wstring_view(notification_data->Loaded.BaseDllName->Buffer, notification_data->Loaded.BaseDllName->Length));
		ZoneTextF(s, strlen(s));
	}
#endif

	doBinaryPatchForFile(notification_data->Loaded);
	static bool bDone = false;	
	if (GetModuleHandleA("dedicated.dll") && !bDone) {
		if (!IsR1ODedicatedServer())
			InitCompressionHooks();
		auto dedicated_base = (uintptr_t)GetModuleHandleA("dedicated.dll");
		HookDedicatedEngineLoader(dedicated_base);
		MH_CreateHook((LPVOID)(dedicated_base + 0x84000), &AddSearchPathDedi, reinterpret_cast<LPVOID*>(&oAddSearchPathDedi));
		if (IsR1ODedicatedServer())
			OutputDebugStringA("R1Delta: skipped legacy R1 filesystem/addon hooks for R1O fake dedi\n");
		MH_EnableHook(MH_ALL_HOOKS);
		bDone = true;
	}
	auto name = notification_data->Loaded.BaseDllName->Buffer;
	auto name_len = notification_data->Loaded.BaseDllName->Length;
	HookR1ODedicatedTfoAllocatorForLoadedModule(
		name,
		name_len,
		reinterpret_cast<uintptr_t>(notification_data->Loaded.DllBase));
	if (!G_is_dedi && string_equal_size(name, name_len, L"launcher.dll")) {
		G_launcher = (uintptr_t)GetModuleHandleW(L"launcher.dll");
		G_vscript = G_launcher;
	}
	else if (IsR1ODedicatedServer() && string_equal_size(name, name_len, L"launcher.dll")) {
		G_launcher = reinterpret_cast<uintptr_t>(notification_data->Loaded.DllBase);
		G_vscript = G_launcher;
		InstallR1OTFOSquirrelHooks(G_launcher);
	}
	if (string_equal_size(name, name_len, L"filesystem_stdio.dll")) {
		G_filesystem_stdio = (uintptr_t)notification_data->Loaded.DllBase;
		// Map directory archives can be mounted before engine.dll reaches
		// InitAddons, so install the in-memory directory repair at the same
		// early boundary as the filesystem compression hooks.
		if (!HasEngineCommandLineFlag("-r1delta_disable_vpk_directory_repair"))
			InstallVPKDirectoryLoadFlagRepair(G_filesystem_stdio);
		if (!HasEngineCommandLineFlag("-r1delta_disable_vpk_async_precache_fix"))
			InstallR1ClientVPKAsyncPrecacheFix(G_filesystem_stdio);
		if (!IsR1ODedicatedServer())
			InitCompressionHooks();
		else
			OutputDebugStringA("R1Delta: loaded filesystem_stdio.dll in R1O fake dedi; legacy compression hooks skipped\n");
	}
	else if (string_equal_size(name, name_len, L"studiorender.dll")) {
		bool enableHooks = false;
		if (IsR1ODedicatedServer()) {
			HookCreateInterfaceExport((uintptr_t)notification_data->Loaded.DllBase, "studiorender", &StudioRenderCreateInterfaceHook, &StudioRenderCreateInterfaceOriginal);
			enableHooks = true;
		}
		if (ShouldInstallR1OClientDebugHooks()) {
			InstallClientStudioRenderMaterialListHook((uintptr_t)notification_data->Loaded.DllBase);
			enableHooks = true;
		}
		if (enableHooks)
			MH_EnableHook(MH_ALL_HOOKS);
	}
	else if (string_equal_size(name, name_len, L"datacache.dll")) {
		if (IsR1ODedicatedServer()) {
			HookCreateInterfaceExport((uintptr_t)notification_data->Loaded.DllBase, "datacache", &DataCacheCreateInterfaceHook, &DataCacheCreateInterfaceOriginal);
			MH_EnableHook(MH_ALL_HOOKS);
		}
	}
	else if (string_equal_size(name, name_len, L"inputsystem.dll")) {
		const std::wstring fullPath(
			notification_data->Loaded.FullDllName->Buffer,
			notification_data->Loaded.FullDllName->Length / sizeof(wchar_t));
		if (IsR1ODedicatedServer()
			&& r1delta::r1o::IsTFORuntimeModulePathW(fullPath.c_str())) {
			HookCreateInterfaceExport((uintptr_t)notification_data->Loaded.DllBase, "inputsystem", &InputSystemCreateInterfaceHook, &InputSystemCreateInterfaceOriginal);
			MH_EnableHook(MH_ALL_HOOKS);
		}
	}
	else if (string_equal_size(name, name_len, L"localize.dll")) {
		const std::wstring fullPath(
			notification_data->Loaded.FullDllName->Buffer,
			notification_data->Loaded.FullDllName->Length / sizeof(wchar_t));
		if (!r1delta::r1o::IsTFORuntimeModulePathW(fullPath.c_str()))
			G_localize = (uintptr_t)notification_data->Loaded.DllBase;
	}
	else if (string_equal_size(name, name_len, L"vstdlib.dll")) {
		if (IsR1ODedicatedServer()) {
			InitVStdLibICVarFactoryHook();
			MH_EnableHook(MH_ALL_HOOKS);
		}
	}
	else if (string_equal_size(name, name_len, L"engine.dll")) {
		do_engine(notification_data);
		should_init_security_fixes = true;
		//client = std::make_shared<discordpp::Client>();
	}
	else if (string_equal_size(name, name_len, L"engine_ds.dll")) {
		G_engine_ds = (uintptr_t)notification_data->Loaded.DllBase;
		if (IsR1ODedicatedServer())
			return;
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("engine_ds.dll") + 0x433C0), &ProcessConnectionlessPacketDedi, reinterpret_cast<LPVOID*>(&ProcessConnectionlessPacketOriginal));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("engine_ds.dll") + 0x30FE20), &StringCompare_AllTalkHookDedi, reinterpret_cast<LPVOID*>(&oStringCompare_AllTalkHookDedi));
		MH_EnableHook(MH_ALL_HOOKS);
		InitAddons();
		InitDedicated();
		constexpr auto a = (1 << 2);
		should_init_security_fixes = true;
	}
	else if (string_equal_size(name, name_len, L"vphysics.dll")) {
		HookCreateInterfaceExport((uintptr_t)notification_data->Loaded.DllBase, "vphysics", &VPhysicsCreateInterfaceHook, &VPhysicsCreateInterfaceOriginal);
		InstallVPhysicsStaticBVHProbe(reinterpret_cast<uintptr_t>(notification_data->Loaded.DllBase));
		if (IsR1ODedicatedServer())
			InstallR1OVPhysicsDeferredReleaseGuard(reinterpret_cast<uintptr_t>(notification_data->Loaded.DllBase));
		else {
			InitPhysicsHooks();
			if (GetR1DeltaEngineMode() == R1DeltaEngineMode::Client2015) {
				const std::wstring fullPath(
					notification_data->Loaded.FullDllName->Buffer,
					notification_data->Loaded.FullDllName->Length / sizeof(wchar_t));
				if (r1delta::vphysics::IsExpectedR1VPhysicsModulePath(fullPath.c_str())) {
					InstallR1VPhysicsSequentialDispatcherGuard(
						reinterpret_cast<uintptr_t>(
							notification_data->Loaded.DllBase));
					InstallR1VPhysicsShutdownGuard(
						reinterpret_cast<uintptr_t>(
							notification_data->Loaded.DllBase));
				}
				else
					Warning(
						"R1Delta: required R1 VPhysics guards refused module path '%ls'\n",
						fullPath.c_str());
			}
		}

		//MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("vphysics.dll") + 0xFFFF), &sub_FFFF, reinterpret_cast<LPVOID*>(&ovphys_sub_FFFF));
		MH_EnableHook(MH_ALL_HOOKS);
	}
	else if (string_equal_size(name, name_len, L"materialsystem_dx11.dll")) {
		G_matsystem = (uintptr_t)notification_data->Loaded.DllBase;
		if (GetR1DeltaEngineMode() == R1DeltaEngineMode::Client2015) {
			InstallMaterialSystemDx11RenderThreadGuards(G_matsystem);
			InstallMaterialSystemDx11InputLayoutCacheGuard(G_matsystem);
		}
		if (!HasEngineCommandLineFlag("-r1delta_disable_material_guards")) {
			InstallMaterialSystemDx11NullShaderResourceGuard(G_matsystem);
			if (GetR1DeltaEngineMode() == R1DeltaEngineMode::Client2015) {
				InstallMaterialSystemDx11TxaaLifetimeGuard(G_matsystem);
			}
		}
		SetupReflexMaterialSystemHooks(G_matsystem);
		if (!IsR1ODedicatedServer()) {
			SetupHudWarpMatSystemHooks();
			MH_EnableHook(MH_ALL_HOOKS);
		}
	}
	else if (string_equal_size(name, name_len, L"vguimatsurface.dll")) {
		if (!IsR1ODedicatedServer()) {
			SetupHudWarpVguiHooks();
			MH_EnableHook(MH_ALL_HOOKS);
		}
	}
	else if (string_equal_size(name, name_len, L"vgui2.dll")) {
		if (IsR1ODedicatedServer() && ShouldInstallR1OClientDebugHooks()) {
			HookCreateInterfaceExport((uintptr_t)notification_data->Loaded.DllBase, "vgui2", &VGui2CreateInterfaceHook, &VGui2CreateInterfaceOriginal);
			MH_EnableHook(MH_ALL_HOOKS);
		}
	}
	else if ((string_equal_size(name, name_len, L"adminserver.dll")) || ((string_equal_size(name, name_len, L"AdminServer.dll")))) {
		InstallAdminServerHooks(
			reinterpret_cast<uintptr_t>(notification_data->Loaded.DllBase));
	}
	else if (string_equal_size(name, name_len, L"engine_r1o.dll")) {
		G_engine_r1o = (uintptr_t)notification_data->Loaded.DllBase;
		if (IsR1ODedicatedServer()) {
			G_engine = G_engine_r1o;
			InitVStdLibICVarFactoryHook();
			InitR1ODedicatedServerAPIHook(G_engine_r1o);
			InstallR1ODedicatedSecurityHooks(G_engine_r1o);
			InstallR1ORemoteAccessHooks(G_engine_r1o);
			MH_EnableHook(MH_ALL_HOOKS);
		}
	}
	else {
		bool is_client = string_equal_size(name, name_len, L"client.dll");
		bool is_r1o_server_local = !is_client
			&& IsR1ODedicatedServer()
			&& (string_equal_size(name, name_len, L"server_local.dll")
				|| string_equal_size(name, name_len, L"server.dll"));
		bool is_server = !is_client
			&& !is_r1o_server_local
			&& string_equal_size(name, name_len, L"server.dll");

		if (is_client) {
			G_client = (uintptr_t)notification_data->Loaded.DllBase;
			const std::wstring clientFullPath(
				notification_data->Loaded.FullDllName->Buffer,
				notification_data->Loaded.FullDllName->Length / sizeof(wchar_t));
			InstallClientStudioHeaderLookupGuard(G_client, clientFullPath.c_str());
			PatchClientDynamicLodNoMatchSentinel(G_client);
			InstallR1ClientPlayerResource18(G_client);
			InstallClientDatacacheCallbackGuard(G_client);
			InstallClientPlayerClassCompatibilityHooks(G_client);
			r1delta::ffa_targeting::InstallClientHooks(G_client);
			if (ShouldInstallR1OClientDebugHooks()) {
				InstallClientRenderableDrawModelHook(G_client);
				ClientMaybeInstallViewSubHooks();
				InstallClientRollLifecycleHooks(G_client);
				if (HMODULE studioRender = GetModuleHandleA("studiorender.dll")) {
					InstallClientStudioRenderMaterialListHook(reinterpret_cast<uintptr_t>(studioRender));
				}
			}
			InitClient();
			SetupHudWarpHooks();
			Setup_MMNotificationClient();
			SetupLocalizeIface();
			typedef bool(__fastcall* o_pCLocalise__AddFile_t)(void*, const char*, const char*, bool);
			o_pCLocalise__AddFile = (o_pCLocalise__AddFile_t)(G_localize + 0x7760);
			SetupSurfaceRenderHooks();
			SetupSquirrelErrorNotificationHooks();
			SetupChatWriter();
			RegisterConVar("delta_enable_ads_sway", "1", FCVAR_CLIENTDLL | FCVAR_ARCHIVE_PLAYERPROFILE, "Enable/disable viewmodel ads sway.");
			RegisterConCommand("+toggleFullscreenMap", toggleFullscreenMap_cmd, "Toggles the fullscreen map.", FCVAR_CLIENTDLL);
			RegisterConVar("cl_hold_to_rodeo_enable", "0", FCVAR_CLIENTDLL | FCVAR_ARCHIVE_PLAYERPROFILE, "0: Automatic rodeo. 1: Hold to rodeo ALL titans. 2: Hold to rodeo friendlies, automatically rodeo hostile titans.");
			RegisterConVar("delta_improved_colorblind", "0", FCVAR_CLIENTDLL | FCVAR_ARCHIVE_PLAYERPROFILE, "Allows certain other things to change color depending on your colorblind setting.");
			RegisterConVar("delta_hud_grenade_style", "0", FCVAR_CLIENTDLL | FCVAR_ARCHIVE_PLAYERPROFILE, "Grenade indicator style. 0: 2D Texture. 1: 3D Model.");
			RegisterConVar("delta_hud_objective_opacity", "255", FCVAR_CLIENTDLL | FCVAR_ARCHIVE_PLAYERPROFILE, "Opacity for objective markers on the hud (CTF Flag, Hardpoint control points, etc).");
			RegisterConVar("delta_hud_show_AT_hint", "1", FCVAR_CLIENTDLL | FCVAR_ARCHIVE_PLAYERPROFILE, "Show anti-titan weapon hint.");
			RegisterConVar("delta_hud_show_challenge_completed", "1", FCVAR_CLIENTDLL | FCVAR_ARCHIVE_PLAYERPROFILE, "Show challenge completed notification.");
			RegisterConVar("delta_hud_show_chat", "1", FCVAR_CLIENTDLL | FCVAR_ARCHIVE_PLAYERPROFILE, "Show chat.");
			RegisterConVar("delta_hud_show_flyout", "1", FCVAR_CLIENTDLL | FCVAR_ARCHIVE_PLAYERPROFILE, "Show information about your weapons when you switch between them.");
			RegisterConVar("delta_hud_show_hitmarkers", "1", FCVAR_CLIENTDLL | FCVAR_ARCHIVE_PLAYERPROFILE, "Show hitmarkers. 0: No hitmarkers. 1: Show all hitmarkers. 2: Only normal hitmarker. 3: Only weak/critical hitmarker.");
			RegisterConVar("delta_hud_show_keybind_icons", "1", FCVAR_CLIENTDLL | FCVAR_ARCHIVE_PLAYERPROFILE, "Show persistent button prompts for your weapons and abilities on the HUD.");
			RegisterConVar("delta_hud_show_levelup", "1", FCVAR_CLIENTDLL | FCVAR_ARCHIVE_PLAYERPROFILE, "Show level-up notification.");
			RegisterConVar("delta_hud_show_obituaries", "1", FCVAR_CLIENTDLL | FCVAR_ARCHIVE_PLAYERPROFILE, "Show obituaries for player kills and deaths.");
			RegisterConVar("delta_hud_show_titan_earnings", "1", FCVAR_CLIENTDLL | FCVAR_ARCHIVE_PLAYERPROFILE, "Show titan timer earnings below the crosshair.");
			RegisterConVar("delta_hud_show_vdu", "1", FCVAR_CLIENTDLL | FCVAR_ARCHIVE_PLAYERPROFILE, "Show character VDUs on the top right.");
			RegisterConVar("delta_hud_show_xpbar", "1", FCVAR_CLIENTDLL | FCVAR_ARCHIVE_PLAYERPROFILE, "Show XP bar.");
			RegisterConCommand("+voteYes", toggleFullscreenMap_cmd, "Vote yes.", FCVAR_CLIENTDLL);
			RegisterConCommand("+voteNo", toggleFullscreenMap_cmd, "Vote no.", FCVAR_CLIENTDLL);
			RegisterConVar("delta_hud_misc_changes", "1", FCVAR_CLIENTDLL | FCVAR_ARCHIVE_PLAYERPROFILE, "Enable some small miscellaneous r1delta hud changes.");
			RegisterConVar("delta_play_hitsounds", "1", FCVAR_CLIENTDLL | FCVAR_ARCHIVE_PLAYERPROFILE, "Play hitsounds.");
			RegisterConVar("delta_play_killsounds", "1", FCVAR_CLIENTDLL | FCVAR_ARCHIVE_PLAYERPROFILE, "Play killsounds.");
			RegisterConVar("delta_hud_show_xpsplash", "1", FCVAR_CLIENTDLL | FCVAR_ARCHIVE_PLAYERPROFILE, "Show XP splash.");
			RegisterConVar("delta_hud_xpsplash_stack", "1", FCVAR_CLIENTDLL | FCVAR_ARCHIVE_PLAYERPROFILE, "Stack XP splash lines of the same type.");
			RegisterConVar("delta_old_reticles", "0", FCVAR_CLIENTDLL | FCVAR_ARCHIVE_PLAYERPROFILE, "Do vanilla reticle behavior, so that theyre kinda broken above 90 fov.");

				MH_CreateHook((LPVOID)(G_localize + 0x3A40), &h_CLocalize__ReloadLocalizationFiles, (LPVOID*)&o_pCLocalize__ReloadLocalizationFiles);
				MH_EnableHook(MH_ALL_HOOKS);
				std::thread(DiscordThread).detach();
		}
		if (is_server || is_r1o_server_local) {
			r1delta::ffa_targeting::InstallServerHooks(
				reinterpret_cast<std::uintptr_t>(notification_data->Loaded.DllBase));
		}
		if (is_server || is_r1o_server_local) do_server(notification_data);
		if (should_init_security_fixes && (is_client || is_server || is_r1o_server_local)) {
			security_fixes_init();
			should_init_security_fixes = false;
		}
	}

}

