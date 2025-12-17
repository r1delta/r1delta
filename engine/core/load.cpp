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
#include <cstdlib>
#include <crtdbg.h>
#include <new>
#include "windows.h"
#include <delayimp.h>

#include <iostream>
#include "cvar.h"
#include <winternl.h> // For UNICODE_STRING.
#include <fstream>
#include <filesystem>
#include <array>
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
// steam.h removed - unused
#include "persistentdata.h"
#include "netadr.h"
#include <httplib.h>
#include "audio.h"
#include "audio_device.h"
#include "audio_cache.h"
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
typedef __int64 (*sub_136E70Type)(char* pPath);
sub_136E70Type sub_136E70Original;
__int64 __fastcall sub_136E70(char* pPath)
{
	auto ret = sub_136E70Original(pPath);
	reinterpret_cast<__int64(*)()>(G_engine + 0x19D730)();
	return ret;
}


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
uintptr_t G_client;
uintptr_t G_matsystem;
uintptr_t G_localize;

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
	MH_CreateHook((LPVOID)(engine_base_spec + (IsDedicatedServer() ? 0x95AA0 : 0x127C70)), &FileSystem_UpdateAddonSearchPaths, reinterpret_cast<LPVOID*>(&FileSystem_UpdateAddonSearchPathsTypeOriginal));
	MH_CreateHook((LPVOID)(engine_base_spec + (IsDedicatedServer() ? 0x950E0 : 0x1272B0)), &ReconcileAddonListFile, reinterpret_cast<LPVOID*>(&oReconcileAddonListFile));
	MH_CreateHook((LPVOID)(filesystem_stdio + (IsDedicatedServer() ? 0x1752B0 : 0x6A420)), &ReadFileFromVPKHook, reinterpret_cast<LPVOID*>(&readFileFromVPK));
	MH_CreateHook((LPVOID)(filesystem_stdio + (IsDedicatedServer() ? 0x750F0 : 0x9C20)), &ReadFromCacheHook, reinterpret_cast<LPVOID*>(&readFromCache));
	MH_CreateHook((LPVOID)(filesystem_stdio + (IsDedicatedServer() ? 0x80BB0 : 0x16250)), &AddVPKFile, reinterpret_cast<LPVOID*>(&AddVPKFileOriginal));
	MH_CreateHook((LPVOID)(filesystem_stdio + (IsDedicatedServer() ? 0x1A1514 : 0x9AB70)), &fs_sprintf_hook, reinterpret_cast<LPVOID*>(NULL));
	MH_CreateHook((LPVOID)(filesystem_stdio + (IsDedicatedServer() ? 0x6EE10 : 0x02C30)), &CBaseFileSystem__FindFirst, reinterpret_cast<LPVOID*>(&oCBaseFileSystem__FindFirst));
	MH_CreateHook((LPVOID)(filesystem_stdio + (IsDedicatedServer() ? 0x86E00 : 0x1C4A0)), &CBaseFileSystem__FindNext, reinterpret_cast<LPVOID*>(&oCBaseFileSystem__FindNext));
	MH_CreateHook((LPVOID)(filesystem_stdio + (IsDedicatedServer() ? 0x7F180 : 0x14780)), &HookedHandleOpenRegularFile, reinterpret_cast<LPVOID*>(&HandleOpenRegularFileOriginal));
	MH_CreateHook((LPVOID)(engine_base_spec + (IsDedicatedServer() ? 0x96980 : 0x128C80)), &FileSystem_AddLoadedSearchPath, reinterpret_cast<LPVOID*>(&oFileSystem_AddLoadedSearchPath));

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

bool ShouldEnableMCP() {
	static bool parsed = false;
	static bool useMcp = false;

	if (!parsed) {
		parsed = true;
		useMcp = HasEngineCommandLineFlag("-usemcp");
	}

	return useMcp;
}

typedef __int64(*Host_InitType)(bool a1);
Host_InitType Host_InitOriginal;

void Host_InitHook(bool a1) {
	Host_InitOriginal(a1);
	OriginalCCVar_FindVar(cvarinterface, "sv_alltalk")->m_nFlags |= FCVAR_REPLICATED;
	auto user_id = OriginalCCVar_FindVar(cvarinterface, "platform_user_id");
	if (user_id->m_Value.m_StringLength < 10) {
		std::srand(std::time(0));
		SetConvarStringOriginal(OriginalCCVar_FindVar(cvarinterface, "platform_user_id"), std::to_string(std::rand()).c_str());
	}
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
	MH_CreateHook((LPVOID)(engine_base + 0x1FDA50), &CLC_Move__ReadFromBuffer, reinterpret_cast<LPVOID*>(&CLC_Move__ReadFromBufferOriginal));
	MH_CreateHook((LPVOID)(engine_base + 0x1F6F10), &CLC_Move__WriteToBuffer, reinterpret_cast<LPVOID*>(&CLC_Move__WriteToBufferOriginal));
	MH_CreateHook((LPVOID)(engine_base + 0x203C20), &NET_SetConVar__ReadFromBuffer, NULL);
	MH_CreateHook((LPVOID)(engine_base + 0x202F80), &NET_SetConVar__WriteToBuffer, NULL);
	MH_CreateHook((LPVOID)(engine_base + 0x1FE3F0), &SVC_ServerInfo__WriteToBuffer, reinterpret_cast<LPVOID*>(&SVC_ServerInfo__WriteToBufferOriginal));
	MH_CreateHook((LPVOID)(engine_base + 0x19CBC0), &GetBuildNo, NULL);
	MH_CreateHook((LPVOID)(engine_base + 0x1E0C10), &CNetChan__GetAddress, reinterpret_cast<LPVOID*>(&oCNetChan__GetAddress));
	MH_CreateHook((void*)(engine_base + 0x133AA0), &Host_InitHook, (void**)&Host_InitOriginal);

	//MH_CreateHook((LPVOID)(engine_base + 0x0D2490), &ProcessConnectionlessPacketClient, reinterpret_cast<LPVOID*>(&ProcessConnectionlessPacketOriginalClient));

	if (!IsDedicatedServer()) {
		MH_CreateHook((LPVOID)(G_engine + 0x1305E0), &ExecuteConfigFile, NULL);
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
	RegisterConCommand("delta_memory_stats", DeltaMemoryStats, "Dump memory stats", 0);

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
do_server(const LDR_DLL_NOTIFICATION_DATA* notification_data)
{
	auto server_base = (uintptr_t)notification_data->Loaded.DllBase;
	G_server = server_base;
	auto vscript_base = G_vscript;

	auto dedi = G_is_dedi;

	auto engine_base_spec = ENGINE_DLL_BASE_(dedi);

	LDR_DLL_LOADED_NOTIFICATION_DATA* ndata = GetModuleNotificationData(L"vstdlib");
	doBinaryPatchForFile(*ndata);
	FreeModuleNotificationData(ndata);
	auto stb_lib = (uintptr_t)GetModuleHandleA("vstdlib.dll");
	SetConvarStringOriginal = (SetConvarString_t)(stb_lib + 0x24DE0);
	uintptr_t vTableAddr = server_base + 0x807220;

	RemoveItemsFromVTable(vTableAddr, 35, 2);
	if (IsDedicatedServer())
		RemoveItemsFromVTable(vTableAddr, 61, 1);
	MH_CreateHook((LPVOID)(server_base + 0x143A10), &CServerGameDLL__DLLInit, (LPVOID*)&CServerGameDLL__DLLInitOriginal);
	MH_CreateHook((LPVOID)(server_base + 0x71E0BC), &hkcalloc_base, NULL);
	MH_CreateHook((LPVOID)(server_base + 0x71E99C), &hkmalloc_base, NULL);
	MH_CreateHook((LPVOID)(server_base + 0x71E9FC), &hkrealloc_base, NULL);
	MH_CreateHook((LPVOID)(server_base + 0x72B480), &hkrecalloc_base, NULL);
	MH_CreateHook((LPVOID)(server_base + 0x721000), &hkfree_base, NULL);
	//MH_CreateHook((LPVOID)(server_base + 0x364D00), &CAI_NetworkManager__LoadNavMesh, reinterpret_cast<LPVOID*>(&CAI_NetworkManager__LoadNavMeshOriginal));
	MH_CreateHook((LPVOID)(vscript_base + (dedi ? 0x6A80 : 0x6A60)), &CScriptVM__ctor, reinterpret_cast<LPVOID*>(&CScriptVM__ctororiginal));
	MH_CreateHook((LPVOID)(vscript_base + (dedi ? 0x1210 : 0x1210)), &CScriptManager__CreateNewVM, reinterpret_cast<LPVOID*>(&CScriptManager__CreateNewVMOriginal));
	MH_CreateHook((LPVOID)(vscript_base + (dedi ? 0x1640 : 0x1630)), &CScriptVM__GetUnknownVMPtr, reinterpret_cast<LPVOID*>(&CScriptVM__GetUnknownVMPtrOriginal));
	MH_CreateHook((LPVOID)(vscript_base + (dedi ? 0x1600 : 0x15F0)), &CScriptManager__DestroyVM, reinterpret_cast<LPVOID*>(&CScriptManager__DestroyVMOriginal));
	MH_CreateHook((LPVOID)(vscript_base + (dedi ? 0xCDD0 : 0xCDB0)), &CSquirrelVM__RegisterFunctionGuts, reinterpret_cast<LPVOID*>(&CSquirrelVM__RegisterFunctionGutsOriginal));
	MH_CreateHook((LPVOID)(vscript_base + (dedi ? 0xD670 : 0xD650)), &CSquirrelVM__PushVariant, reinterpret_cast<LPVOID*>(&CSquirrelVM__PushVariantOriginal));
	MH_CreateHook((LPVOID)(vscript_base + (dedi ? 0xD7D0 : 0xD7B0)), &CSquirrelVM__ConvertToVariant, reinterpret_cast<LPVOID*>(&CSquirrelVM__ConvertToVariantOriginal));
	MH_CreateHook((LPVOID)(vscript_base + (dedi ? 0xB130 : 0xB110)), &CSquirrelVM__ReleaseValue, reinterpret_cast<LPVOID*>(&CSquirrelVM__ReleaseValueOriginal));
	MH_CreateHook((LPVOID)(vscript_base + (dedi ? 0xA210 : 0xA1F0)), &CSquirrelVM__SetValue, reinterpret_cast<LPVOID*>(&CSquirrelVM__SetValueOriginal));
	MH_CreateHook((LPVOID)(vscript_base + (dedi ? 0x9C60 : 0x9C40)), &CSquirrelVM__SetValueEx, reinterpret_cast<LPVOID*>(&CSquirrelVM__SetValueExOriginal));
	MH_CreateHook((LPVOID)(vscript_base + (dedi ? 0xBE80 : 0xBE60)), &CSquirrelVM__TranslateCall, reinterpret_cast<LPVOID*>(&CSquirrelVM__TranslateCallOriginal));
	MH_CreateHook((LPVOID)(server_base + 0x507560), &ServerClassInit_DT_BasePlayer, reinterpret_cast<LPVOID*>(&ServerClassInit_DT_BasePlayerOriginal));
	MH_CreateHook((LPVOID)(server_base + 0x51DFE0), &ServerClassInit_DT_Local, reinterpret_cast<LPVOID*>(&ServerClassInit_DT_LocalOriginal));
	MH_CreateHook((LPVOID)(server_base + 0x5064F0), &ServerClassInit_DT_LocalPlayerExclusive, reinterpret_cast<LPVOID*>(&ServerClassInit_DT_LocalPlayerExclusiveOriginal));
	MH_CreateHook((LPVOID)(server_base + 0x593270), &ServerClassInit_DT_TitanSoul, reinterpret_cast<LPVOID*>(&ServerClassInit_DT_TitanSoulOriginal));
	MH_CreateHook((LPVOID)(server_base + 0x629740), &UserMessage_ReorderHook, reinterpret_cast<LPVOID*>(&UserMessage_ReorderHook_Original));
	MH_CreateHook((LPVOID)(server_base + 0x3A1EC0), &CBaseEntity__SendProxy_CellOrigin, reinterpret_cast<LPVOID*>(NULL));
	MH_CreateHook((LPVOID)(server_base + 0x3A2020), &CBaseEntity__SendProxy_CellOriginXY, reinterpret_cast<LPVOID*>(NULL));
	MH_CreateHook((LPVOID)(server_base + 0x3A2130), &CBaseEntity__SendProxy_CellOriginZ, reinterpret_cast<LPVOID*>(NULL));
	MH_CreateHook((LPVOID)(server_base + 0x3C8B70), &CBaseEntity__VPhysicsInitNormal, reinterpret_cast<LPVOID*>(&oCBaseEntity__VPhysicsInitNormal));
	MH_CreateHook((LPVOID)(server_base + 0x3B3200), &CBaseEntity__SetMoveType, reinterpret_cast<LPVOID*>(&oCBaseEntity__SetMoveType));
	MH_CreateHook((LPVOID)(server_base + 0x4E2F30), &CPlayer_GetLevel, reinterpret_cast<LPVOID*>(NULL));
	MH_CreateHook((LPVOID)(server_base + 0x1442D0), &CServerGameDLL_DLLShutdown, reinterpret_cast<LPVOID*>(NULL));
	MH_CreateHook((LPVOID)(server_base + 0x1532A0), &WallrunMove_BlockForTitans, reinterpret_cast<LPVOID*>(&WallrunMove_BlockForTitans_Original));
	MH_CreateHook((LPVOID)(server_base + 0x21B6B0), &HookedGetRankFunction, NULL);
	MH_CreateHook((LPVOID)(server_base + 0x50B8B0), &HookedSetRankFunction, NULL);
	MH_CreateHook((LPVOID)(server_base + 0x410F60), &CGrenadeFrag__ResolveFlyCollisionCustom, reinterpret_cast<LPVOID*>(&oCGrenadeFrag__ResolveFlyCollisionCustom));
	MH_CreateHook((LPVOID)(server_base + 0x25E290), &UTIL_LogPrintf, reinterpret_cast<LPVOID*>(&oUTIL_LogPrintf));
	//if (IsDedicatedServer())
		MH_CreateHook((LPVOID)(server_base + 0x148730), &CServerGameDLL_OnSayTextMsg, reinterpret_cast<LPVOID*>(&oCServerGameDLL_OnSayTextMsg));
	
	if (IsDedicatedServer()) {
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

	//MH_CreateHook((LPVOID)(server_base + 0x3667D0), &CAI_NetworkManager__DelayedInit, reinterpret_cast<LPVOID*>(&CAI_NetworkManager__DelayedInitOriginal));
	//MH_CreateHook((LPVOID)(server_base + 0x36BC30), &sub_36BC30, reinterpret_cast<LPVOID*>(&sub_36BC30Original));
	//MH_CreateHook((LPVOID)(server_base + 0x36C150), &sub_36C150, reinterpret_cast<LPVOID*>(&sub_36C150Original));
	//MH_CreateHook((LPVOID)(server_base + 0x3669C0), &CAI_NetworkManager__FixupHints, reinterpret_cast<LPVOID*>(&CAI_NetworkManager__FixupHintsOriginal));
	//MH_CreateHook((LPVOID)(server_base + 0x31CE90), &unkallocfunc, reinterpret_cast<LPVOID*>(&unkallocfuncoriginal));
	//MH_CreateHook((LPVOID)(server_base + 0x25A8E0), &CEntityFactoryDictionary__Create, reinterpret_cast<LPVOID*>(&CEntityFactoryDictionary__CreateOriginal));
	//MH_CreateHook((LPVOID)(server_base + 0x363A50), &sub_363A50, reinterpret_cast<LPVOID*>(&sub_363A50Original));
	auto engine_base = G_engine;
	MH_CreateHook((LPVOID)(server_base + 0x3BE1A0), &CC_Ent_Create, reinterpret_cast<LPVOID*>(&oCC_Ent_Create));
	MH_CreateHook((LPVOID)(server_base + 0x25E340), &DispatchSpawn, reinterpret_cast<LPVOID*>(&oDispatchSpawn));
	MH_CreateHook((LPVOID)(server_base + 0x369E00), &InitTableHook, reinterpret_cast<LPVOID*>(&original_init_table));
	MH_CreateHook((LPVOID)(server_base + 0x2820A0), &HandleSquirrelClientCommand, reinterpret_cast<LPVOID*>(&oHandleSquirrelClientCommand));
	MH_CreateHook((LPVOID)(server_base + 0x59F380), &CProjectile__PhysicsSimulate, reinterpret_cast<LPVOID*>(&oCProjectile__PhysicsSimulate));
	//MH_CreateHook((LPVOID)(server_base + 0x364140), &DebugConnectMsg, reinterpret_cast<LPVOID*>(0));
	RegisterConCommand("updatescriptdata", updatescriptdata_cmd, "Dumps the script data in the AI node graph to disk", FCVAR_CHEAT);
	RegisterConCommand("verifyain", verifyain_cmd, "Reads the .ain file from disk, compares its nodes & links to in-memory data, logs differences.", FCVAR_CHEAT);
	RegisterConCommand("updateain", updateain_cmd, "Calls StartRebuild, then overwrites node/link data in the .ain file.", FCVAR_CHEAT);
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
	RegisterConVar("hudwarp_use_gpu", "1", FCVAR_ARCHIVE,"Use GPU for HUD warp");
	RegisterConVar("hudwarp_disable", "0", FCVAR_ARCHIVE, "GPU device to use for HUD warp");
	RegisterConVar("hide_server", "0", FCVAR_NONE, "Whether the server should be hidden from the master server");
	RegisterConVar("server_description", "", FCVAR_NONE, "Server description");
	RegisterConVar("delta_ui_server_filter", "0", FCVAR_NONE, "Script managed vgui filter convar");
	RegisterConVar("delta_autoBalanceTeams", "1", FCVAR_NONE, "Whether to autobalance teams on death/private match/lobby start. Managed by script");
	RegisterConVar("delta_useLegacyProgressBar", "0", FCVAR_ARCHIVE, "Whether or not to use the old loading bar");
	RegisterConVar("delta_return_to_lobby", "1", FCVAR_NONE, "Return to lobby after a game");
	CBanSystem::m_pSvBanlistAutosave = RegisterConVar("sv_banlist_autosave", "1", FCVAR_ARCHIVE, "Automatically save ban lists after modification commands.");
	RegisterConCommand("script", script_cmd, "Execute Squirrel code in server context", FCVAR_GAMEDLL | FCVAR_CHEAT);
	if (!IsDedicatedServer()) {
		RegisterConCommand("script_client", script_client_cmd, "Execute Squirrel code in client context", FCVAR_NONE);
		RegisterConCommand("script_ui", script_ui_cmd, "Execute Squirrel code in UI context", FCVAR_NONE);
		RegisterConCommand("noclip", noclip_cmd, "Toggles NOCLIP mode.", FCVAR_GAMEDLL | FCVAR_CHEAT);
	}

	//auto allTalk = OriginalCCVar_FindVar(cvarinterface, "sv_alltalk");

	//allTalk->m_nFlags |= FCVAR_REPLICATED;

	//0x0000415198 on dedicated
	// 0x0620818 on client

	if (IsDedicatedServer()) {
		MH_CreateHook((LPVOID)(G_engine_ds + 0x45530), &HookedCBaseClientSetName, reinterpret_cast<LPVOID*>(&CBaseClientSetNameOriginal));
		MH_CreateHook((LPVOID)(G_engine_ds + 0x491A0), &HookedCBaseClientConnect, reinterpret_cast<LPVOID*>(&oCBaseClientConnect));
	}
	else {
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


	MH_CreateHook((LPVOID)GetProcAddress(GetModuleHandleA("vstdlib.dll"), "VStdLib_GetICVarFactory"), &VStdLib_GetICVarFactory, NULL);
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
		MH_CreateHook((LPVOID)(engine_base + 0x136E70), &sub_136E70, reinterpret_cast<LPVOID*>(&sub_136E70Original)); // fixes some vpk issue
		MH_CreateHook((LPVOID)(engine_base + 0x72360), &cl_DumpPrecacheStats, NULL);

		//MH_CreateHook((LPVOID)(engine_base_spec + 0x473550), &sub_180473550, NULL);

		//MH_CreateHook((LPVOID)(engine_base_spec + 0x1168B0), &COM_StringCopy, reinterpret_cast<LPVOID*>(&COM_StringCopyOriginal));
		//MH_CreateHook((LPVOID)(engine_base_spec + 0x1C79A0), &DataTable_SetupReceiveTableFromSendTable, reinterpret_cast<LPVOID*>(&DataTable_SetupReceiveTableFromSendTableOriginal));
	}
	MH_CreateHook((LPVOID)(G_vscript + (IsDedicatedServer() ? 0x0B660 : 0xB640)), &CSquirrelVM__PrintFunc1, NULL);
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
	if (IsDedicatedServer()) {
		MH_CreateHook((LPVOID)(G_engine_ds + 0x6ABF0), &CBanSystem::RemoteAccess_GetUserBanList, NULL);
	}
	else {
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

	security_fixes_server(engine_base, server_base);
	R1DAssert(MH_EnableHook(MH_ALL_HOOKS) == MH_OK);
	//std::cout << "did hooks" << std::endl;
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
		InitCompressionHooks();
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("dedicated.dll") + 0x84000), &AddSearchPathDedi, reinterpret_cast<LPVOID*>(&oAddSearchPathDedi));
		MH_EnableHook(MH_ALL_HOOKS);
		bDone = true;
	}
	auto name = notification_data->Loaded.BaseDllName->Buffer;
	auto name_len = notification_data->Loaded.BaseDllName->Length;
	if (!G_is_dedi && string_equal_size(name, name_len, L"launcher.dll")) {
		G_launcher = (uintptr_t)GetModuleHandleW(L"launcher.dll");
		G_vscript = G_launcher;
	}
	if (string_equal_size(name, name_len, L"filesystem_stdio.dll")) {
		G_filesystem_stdio = (uintptr_t)notification_data->Loaded.DllBase;
		InitCompressionHooks();
	}
	else if (string_equal_size(name, name_len, L"engine.dll")) {
		do_engine(notification_data);
		should_init_security_fixes = true;
		//client = std::make_shared<discordpp::Client>();
	}
	else if (string_equal_size(name, name_len, L"engine_ds.dll")) {
		G_engine_ds = (uintptr_t)notification_data->Loaded.DllBase;
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("engine_ds.dll") + 0x433C0), &ProcessConnectionlessPacketDedi, reinterpret_cast<LPVOID*>(&ProcessConnectionlessPacketOriginal));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("engine_ds.dll") + 0x30FE20), &StringCompare_AllTalkHookDedi, reinterpret_cast<LPVOID*>(&oStringCompare_AllTalkHookDedi));
		MH_EnableHook(MH_ALL_HOOKS);
		InitAddons();
		InitDedicated();
		constexpr auto a = (1 << 2);
		should_init_security_fixes = true;
	}
	else if (string_equal_size(name, name_len, L"vphysics.dll")) {
		InitPhysicsHooks();
		//MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("vphysics.dll") + 0x1032C0), &sub_1032C0_hook, reinterpret_cast<LPVOID*>(&o_sub_1032C0));
		//MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("vphysics.dll") + 0x103120), &sub_103120_hook, reinterpret_cast<LPVOID*>(&o_sub_103120));
		//CreateMiscHook(vphysicsdllBaseAddress, 0x100880, &sub_180100880, reinterpret_cast<LPVOID*>(&sub_180100880_org));
		//MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("vphysics.dll") + 0x100880), &sub_100880_hook, reinterpret_cast<LPVOID*>(&o_sub_100880));

		//MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("vphysics.dll") + 0xFFFF), &sub_FFFF, reinterpret_cast<LPVOID*>(&ovphys_sub_FFFF));
		MH_EnableHook(MH_ALL_HOOKS);
	}
	else if (string_equal_size(name, name_len, L"materialsystem_dx11.dll")) {
		G_matsystem = (uintptr_t)notification_data->Loaded.DllBase;
		SetupHudWarpMatSystemHooks();
		MH_EnableHook(MH_ALL_HOOKS);
	}
	else if (string_equal_size(name, name_len, L"vguimatsurface.dll")) {
		SetupHudWarpVguiHooks();
		MH_EnableHook(MH_ALL_HOOKS);
	}
	else if ((string_equal_size(name, name_len, L"adminserver.dll")) || ((string_equal_size(name, name_len, L"AdminServer.dll")))) {
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("AdminServer.dll") + 0x14730), &CServerInfoPanel__OnServerDataResponse_14730, reinterpret_cast<LPVOID*>(&oCServerInfoPanel__OnServerDataResponse_14730));
		MH_EnableHook(MH_ALL_HOOKS);
	}
	else if ((string_equal_size(name, name_len, "localize.dll"))) {
		G_localize = (uintptr_t)notification_data->Loaded.DllBase;
	}
	else {
		bool is_client = string_equal_size(name, name_len, L"client.dll");
		bool is_server = !is_client && string_equal_size(name, name_len, L"server.dll");

		if (is_client) {
			G_client = (uintptr_t)notification_data->Loaded.DllBase;
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
			RegisterConVar("bot_kick_on_death", "1", FCVAR_GAMEDLL | FCVAR_CHEAT, "Enable/disable bots getting kicked on death.");
			RegisterConVar("delta_improved_colorblind", "0", FCVAR_CLIENTDLL | FCVAR_ARCHIVE_PLAYERPROFILE, "Allows certain other things to change color depending on your colorblind setting.");
			MH_CreateHook((LPVOID)(G_localize + 0x3A40), &h_CLocalize__ReloadLocalizationFiles, (LPVOID*)&o_pCLocalize__ReloadLocalizationFiles);
			MH_EnableHook(MH_ALL_HOOKS);
			std::thread(DiscordThread).detach();
		}
		if (is_server) do_server(notification_data);
		if (should_init_security_fixes && (is_client || is_server)) {
			security_fixes_init();
			should_init_security_fixes = false;
		}
	}

}

