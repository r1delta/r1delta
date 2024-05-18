#include "dedicated.h"
#include <intrin.h>

#pragma intrinsic(_ReturnAddress)

 std::vector<std::string> netMessages = {
	"Base_CmdKeyValues",
	"CLC_BaselineAck",
	"CLC_ClientInfo",
	"CLC_ClientSayText",
	"CLC_ClientTick",
	"CLC_CmdKeyValues",
	"CLC_DurangoVoiceData",
	"CLC_FileCRCCheck",
	"CLC_ListenEvents",
	"CLC_LoadingProgress",
	"CLC_Move",
	"CLC_PersistenceClientToken",
	"CLC_PersistenceRequestSave",
	"CLC_RespondCvarValue",
	"CLC_SaveReplay",
	"CLC_SplitPlayerConnect",
	"CLC_VoiceData",
	"CNetMessage",
	"NET_SetConVar",
	"NET_SignonState",
	"NET_SplitScreenUser",
	"NET_StringCmd",
	"SVC_BSPDecal",
	"SVC_ClassInfo",
	"SVC_CmdKeyValues",
	"SVC_CreateStringTable",
	"SVC_CrosshairAngle",
	"SVC_DurangoVoiceData",
	"SVC_EntityMessage",
	"SVC_FixAngle",
	"SVC_GameEvent",
	"SVC_GameEventList",
	"SVC_GetCvarValue",
	"SVC_Menu",
	"SVC_PersistenceBaseline",
	"SVC_PersistenceDefFile",
	"SVC_PersistenceNotifySaved",
	"SVC_PersistenceUpdateVar",
	"SVC_PlaylistChange",
	"SVC_Playlists",
	"SVC_Print",
	"SVC_SendTable",
	"SVC_ServerInfo",
	"SVC_ServerTick",
	"SVC_SetPause",
	"SVC_SetTeam",
	"SVC_Snapshot",
	"SVC_Sounds",
	"SVC_SplitScreen",
	"SVC_TempEntities",
	"SVC_UpdateStringTable",
	"SVC_UserMessage",
	"SVC_VoiceData"
};

typedef void (*CUtlBuffer__SetExternalBufferType)(__int64 a1, __int64 a2, __int64 a3, __int64 a4, char a5);
CUtlBuffer__SetExternalBufferType CUtlBuffer__SetExternalBufferOriginal;
char* textbuf = 0;
__int64 pdefsize = 0;
void __fastcall CUtlBuffer__SetExternalBuffer(__int64 a1, __int64 a2, __int64 a3, __int64 a4, char a5)
{
	static uintptr_t engineDS = uintptr_t(GetModuleHandleA("engine_ds.dll"));
	if (uintptr_t(_ReturnAddress()) == (engineDS + 0x169F03)) {
		if (!textbuf)
			textbuf = (char*)malloc(0x50238);
		a2 = (__int64)(textbuf);
		a3 = 0x50238;
	}
	return CUtlBuffer__SetExternalBufferOriginal(a1, a2, a3, a4, a5);
}

__int64 __fastcall sub_160130(__int64* a1, __int64* a2)
{
	__int64 result; // rax

	*a1 = (__int64)textbuf;
	result = pdefsize;
	*a2 = pdefsize;
	return result;
}

long long* __fastcall unkfunc(long long* a1)
{
	*a1 = 0i64;
	a1[1] = 0i64;
	a1[2] = 0i64;
	a1[3] = 0i64;
	return a1;
}

typedef char(__fastcall* ParsePDATAType)(__int64 a1, __int64 a2, __int64 a3, const char* a4);
ParsePDATAType ParsePDATAOriginal;
char __fastcall ParsePDEF(__int64 a1, __int64 a2, __int64 a3, const char* a4)
{
	char v10[53264];
	static auto whatthefuck = (__int64 (*)(__int64 a1, int* a2, __int64 a3, int a4, unsigned int a5, unsigned int a6, unsigned int a7))(uintptr_t(GetModuleHandleA("engine.dll")) + 0x42A450);
	a1 = (__int64)(textbuf);
	pdefsize = a2;
	auto ret = ParsePDATAOriginal(a1, a2, a3, a4);
	int whatever = a2;
	memcpy(v10, (void*)a1, sizeof(v10));
	int v4 = whatthefuck(a1, &whatever, (__int64)v10, a2, 1u, 0, 0);
	return ret;
}

typedef __int64(*NET_OutOfBandPrintf_t)(int, void*, const char*, ...);
NET_OutOfBandPrintf_t Original_NET_OutOfBandPrintf = NULL;
typedef __int64 (*NET_SendPacketType)(
	__int64 a1,
	int a2,
	void* a3,
	_DWORD* a4,
	int a5,
	__int64 a6,
	char a7,
	int a8,
	char a9,
	__int64 a10,
	char a11);
NET_SendPacketType NET_SendPacketOriginal;

__int64 Detour_NET_OutOfBandPrintf(int sock, void* adr, const char* fmt, ...) {
	if (strcmp(fmt, "%c00000000000000") == 0) {
		static const char** gamemode = reinterpret_cast<const char**>((uintptr_t)(GetModuleHandleA("server.dll")) + 0xB68520);
		static const char* mapname = reinterpret_cast<const char*>((uintptr_t)(GetModuleHandleA("engine_ds.dll")) + 0x1C89A84);
		//MessageBoxA(NULL, *gamemode, mapname, 16);
		char msg[1200] = { 0 };
		bf_write startup(msg, 1200);
		startup.WriteLong(0xFFFFFFFFi64);
		startup.WriteByte(0x4Au);
		startup.WriteString(mapname);
		startup.WriteString(*gamemode);
		return NET_SendPacketOriginal(0, sock, adr, (uint32*)(startup.GetBasePointer()), startup.GetNumBytesWritten(), 0i64, 0, 0, 0, 0i64, 1);
	}

	va_list args;
	va_start(args, fmt);
	__int64 result = Original_NET_OutOfBandPrintf(sock, adr, fmt, args);
	va_end(args);
	return result;
}

typedef __int64 (*bf_write__WriteUBitLongType)(bf_write* a1, unsigned int a2, signed int a3);
bf_write__WriteUBitLongType bf_write__WriteUBitLongOriginal;
__int64 __fastcall bf_write__WriteUBitLong(bf_write* a1, unsigned int a2, signed int a3)
{
	static uintptr_t engineDS = uintptr_t(GetModuleHandleA("engine_ds.dll"));
	auto ret = bf_write__WriteUBitLongOriginal(a1, a2, a3);
	if (uintptr_t(_ReturnAddress()) == (engineDS + 0x51018) || uintptr_t(_ReturnAddress()) == (engineDS + 0x5101D))
		bf_write__WriteUBitLongOriginal(a1, 0, 14);
	return ret;
}


typedef __int64(*Host_InitDedicatedType)(__int64 a1, __int64 a2, __int64 a3);
Host_InitDedicatedType Host_InitDedicatedOriginal;
__int64 Host_InitDedicated(__int64 a1, __int64 a2, __int64 a3)
{
	CModule engine("engine.dll", (uintptr_t)LoadLibraryA("engine.dll"));
	CModule engineDS("engine_ds.dll");

	NET_CreateNetChannelOriginal = NET_CreateNetChannelType(engine.GetModuleBase() + 0x1F1B10);
	MH_CreateHook(LPVOID(engineDS.GetModuleBase() + 0x13CA10), LPVOID(engine.GetModuleBase() + 0x1EC7B0), NULL); // NET_BufferToBufferCompress
	MH_CreateHook(LPVOID(engineDS.GetModuleBase() + 0x13DD90), LPVOID(engine.GetModuleBase() + 0x1EDB40), NULL); // NET_BufferToBufferDecompress
	MH_CreateHook(LPVOID(engineDS.GetModuleBase() + 0x144C60), LPVOID(engine.GetModuleBase() + 0x1F4AC0), NULL); // NET_ClearQueuedPacketsForChannel
	MH_CreateHook(LPVOID(engineDS.GetModuleBase() + 0x141D60), &NET_CreateNetChannel, NULL); // NET_CreateNetChannel LPVOID(engine.GetModuleBase() + 0x1F1B10)
	MH_CreateHook(LPVOID(engineDS.GetModuleBase() + 0x13F7A0), LPVOID(engine.GetModuleBase() + 0x1EF550), NULL); // NET_GetUDPPort
	MH_CreateHook(LPVOID(engineDS.GetModuleBase() + 0x145440), LPVOID(engine.GetModuleBase() + 0x1F5220), NULL); // NET_Init
	MH_CreateHook(LPVOID(engineDS.GetModuleBase() + 0x13C9D0), LPVOID(engine.GetModuleBase() + 0x1EC770), NULL); // NET_InitPostFork
	MH_CreateHook(LPVOID(engineDS.GetModuleBase() + 0x13C8D0), LPVOID(engine.GetModuleBase() + 0x1EC660), NULL); // NET_IsDedicated
	MH_CreateHook(LPVOID(engineDS.GetModuleBase() + 0x13C8C0), LPVOID(engine.GetModuleBase() + 0x1EC650), NULL); // NET_IsMultiplayer
	MH_CreateHook(LPVOID(engineDS.GetModuleBase() + 0x13F7D0), LPVOID(engine.GetModuleBase() + 0x1EF580), NULL); // NET_ListenSocket
	Original_NET_OutOfBandPrintf = NET_OutOfBandPrintf_t(engine.GetModuleBase() + 0x1F47C0);
	MH_CreateHook(LPVOID(engineDS.GetModuleBase() + 0x144960), LPVOID(Detour_NET_OutOfBandPrintf), NULL); // NET_OutOfBandPrintf
	MH_CreateHook(LPVOID(engineDS.GetModuleBase() + 0x142ED0), LPVOID(engine.GetModuleBase() + 0x1F2CF0), NULL); // NET_ProcessSocket
	MH_CreateHook(LPVOID(engineDS.GetModuleBase() + 0x1458A0), LPVOID(engine.GetModuleBase() + 0x1F5650), NULL); // NET_RemoveNetChannel
	MH_CreateHook(LPVOID(engineDS.GetModuleBase() + 0x143390), LPVOID(engine.GetModuleBase() + 0x1F31B0), NULL); // NET_RunFrame
	NET_SendPacketOriginal = NET_SendPacketType(engine.GetModuleBase() + 0x1F4130);
	MH_CreateHook(LPVOID(engineDS.GetModuleBase() + 0x1442D0), LPVOID(engine.GetModuleBase() + 0x1F4130), NULL); // NET_SendPacket
	MH_CreateHook(LPVOID(engineDS.GetModuleBase() + 0x144D10), LPVOID(engine.GetModuleBase() + 0x1F4B70), NULL); // NET_SendQueuedPackets
	MH_CreateHook(LPVOID(engineDS.GetModuleBase() + 0x1453D0), LPVOID(engine.GetModuleBase() + 0x1F51B0), NULL); // NET_SetMultiplayer
	MH_CreateHook(LPVOID(engineDS.GetModuleBase() + 0x1434C0), LPVOID(engine.GetModuleBase() + 0x1F32E0), NULL); // NET_Shutdown
	MH_CreateHook(LPVOID(engineDS.GetModuleBase() + 0x13FAB0), LPVOID(engine.GetModuleBase() + 0x1EF860), NULL); // NET_SleepUntilMessages
	MH_CreateHook(LPVOID(engineDS.GetModuleBase() + 0x13C3E0), LPVOID(engine.GetModuleBase() + 0x1EC1B0), NULL); // NET_StringToAdr
	MH_CreateHook(LPVOID(engineDS.GetModuleBase() + 0x13C100), LPVOID(engine.GetModuleBase() + 0x1EBED0), NULL); // NET_StringToSockaddr
	MH_CreateHook(LPVOID(engineDS.GetModuleBase() + 0x146D50), LPVOID(engine.GetModuleBase() + 0x1F6B90), NULL); // INetMessage__WriteToBuffer
	MH_CreateHook(LPVOID(engineDS.GetModuleBase() + 0x13B000), LPVOID(engine.GetModuleBase() + 0x1E9EA0), NULL); // CNetChan__CNetChan__dtor
	MH_CreateHook(LPVOID(engineDS.GetModuleBase() + 0x017940), LPVOID(engine.GetModuleBase() + 0x028BC0), NULL); // CLC_SplitPlayerConnect__dtor
	MH_CreateHook(LPVOID(engineDS.GetModuleBase() + 0x12F140), LPVOID(engine.GetModuleBase() + 0x1DC830), NULL); // SendTable_WriteInfos
	MH_CreateHook(LPVOID(engineDS.GetModuleBase() + 0x71C0), &bf_write__WriteUBitLong, reinterpret_cast<LPVOID*>(&bf_write__WriteUBitLongOriginal)); // bf_write__WriteUBitLong
	MH_CreateHook(LPVOID(engineDS.GetModuleBase() + 0x497F0), LPVOID(engine.GetModuleBase() + 0xD8420), NULL); // CBaseClient::ConnectionStart

	for (const auto& msg : netMessages) {
		std::string mangledName = ".?AV" + msg + "@@";
		LPVOID dsVtable = engineDS.GetVirtualMethodTable(mangledName.c_str());
		LPVOID vtable = engine.GetVirtualMethodTable(mangledName.c_str());

		if (dsVtable && vtable) {
			for (int i = 0; i < 14; ++i) {
				LPVOID pTarget = static_cast<LPVOID*>(dsVtable)[i];
				LPVOID pDetour = static_cast<LPVOID*>(vtable)[i];
				MH_CreateHook(pTarget, pDetour, NULL);
			}
		}
	}
	MH_EnableHook(MH_ALL_HOOKS);
	reinterpret_cast<char(__fastcall*)(__int64, CreateInterfaceFn)>((uintptr_t)(engine.GetModuleBase()) + 0x01A04A0)(0, (CreateInterfaceFn)(engineDS.GetModuleBase() + 0xE9000)); // connect nondedi engine
	reinterpret_cast<void(__fastcall*)(int, void*)>((uintptr_t)(engine.GetModuleBase()) + 0x47F580)(0, 0); // register nondedi engine cvars
	return Host_InitDedicatedOriginal(a1, a2, a3);
}

void InitDedicated()
{
	uintptr_t offsets[] = {
		// move g_ServerGlobalVariables_nTimestampNetworkingBase + 4
		0x552fc,
		// move g_ServerGlobalVariables_nTimestampRandomizeWindow + 4
		0x55303,
		// move g_ServerGlobalVariables_mapname_pszValue + 4
		0x5ec5f,
		// move g_ServerGlobalVariables_mapversion + 4
		0x286ba,
		0x286cb,
		0x2898c,
		0x5e7aa,
		// move g_ServerGlobalVariables_startspot + 4
		0x5ec81,
		// move g_ServerGlobalVariables_bMapLoadFailed + 4
		0xaa088,
		// move g_ServerGlobalVariables_deathmatch + 4
		0x5ec41,
		0x5ec4b,
		// move g_ServerGlobalVariables_coop + 4
		0x5ec2c,
		// move g_ServerGlobalVariables_maxEntities + 4
		0x5e823,
		0x61C10,
		// move g_ServerGlobalVariables_serverCount + 4
		0xa05d2,
		0xa178e,
		// move g_ServerGlobalVariables_pEdicts + 4
		0x5DBC7,
		0x5dc0c,
		0x5e89d,
		0x61c86
	};

	HMODULE hModule = GetModuleHandleA("engine_ds.dll");

	for (auto offset : offsets) {
		int* address = reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(hModule) + offset);

		DWORD oldProtect;
		if (!VirtualProtect(address, sizeof(uintptr_t), PAGE_EXECUTE_READWRITE, &oldProtect)) {
			continue;
		}

		*address += 4;

		DWORD temp;
		VirtualProtect(address, sizeof(uintptr_t), oldProtect, &temp);

		FlushInstructionCache(GetCurrentProcess(), address, sizeof(uintptr_t));
	}

	MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("engine_ds.dll") + 0x1693D0), &ParsePDEF, reinterpret_cast<LPVOID*>(&ParsePDATAOriginal));
	MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("engine_ds.dll") + 0x3259C0), &CUtlBuffer__SetExternalBuffer, reinterpret_cast<LPVOID*>(&CUtlBuffer__SetExternalBufferOriginal));
	MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("engine_ds.dll") + 0x160130), &sub_160130, reinterpret_cast<LPVOID*>(NULL));
	MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("engine_ds.dll") + 0xA1B90), &Host_InitDedicated, reinterpret_cast<LPVOID*>(&Host_InitDedicatedOriginal));
	uintptr_t tier0_base = reinterpret_cast<uintptr_t>(GetModuleHandleA("tier0_r1.dll"));


	MH_EnableHook(MH_ALL_HOOKS);
}