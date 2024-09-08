#include "dedicated.h"
#include <intrin.h>

#include "load.h"

#pragma intrinsic(_ReturnAddress)

#define VTABLE_UPDATE_FORCE 1

struct vtableRef2Engines {
#if defined(_DEBUG)
	const char* name;
#endif
	uintptr_t offset_engine;
	uintptr_t offset_engine_ds;
};

#if defined(_DEBUG)
#define VTABLEREF2ENGINES(N, E, EDS) { (N), (E), (EDS) }

// NOTE(mrsteyk): this is intentionally slow to make you aware of what you are doing.
//                you must atone for your sins.
static uintptr_t
FindVTable(uintptr_t base, const char* name)
{
	auto mz = (PIMAGE_DOS_HEADER)base;
	auto pe = (PIMAGE_NT_HEADERS64)((uint8_t*)base + mz->e_lfanew);
	auto sections_num = pe->FileHeader.NumberOfSections;
	auto sections = IMAGE_FIRST_SECTION(pe);

	uintptr_t data_start = 0;
	uintptr_t data_end = 0;
	uintptr_t rdata_start = 0;
	uintptr_t rdata_end = 0;

	for (size_t i = 0; i < sections_num; i++)
	{
		auto section = sections + i;
		if (strcmp_static(section->Name, ".data") == 0)
		{
			data_start = base + section->VirtualAddress;
			data_end = data_start + section->Misc.VirtualSize;
		}
		else if (strcmp_static(section->Name, ".rdata") == 0)
		{
			rdata_start = base + section->VirtualAddress;
			rdata_end = rdata_start + section->Misc.VirtualSize;
		}

		if (data_start && data_end && rdata_start && rdata_end)
		{
			break;
		}
	}

	if (data_start && data_end && rdata_start && rdata_end)
	{
		auto name_len = strlen(name);
		uint32_t name_rva = 0;

		// NOTE(mrsteyk): strings are usually 16 byte aligned, but this is a part of a struct with 8 align.
		for (size_t pos = data_start; pos < data_end; pos += 8)
		{
			// ".?AV" name "@@";
			if (memcmp((void*)pos, ".?AV", 4))
				continue;
			if (memcmp((void*)(pos + 4), name, name_len))
				continue;
			if (memcmp((void*)(pos + 4 + name_len), "@@", 3))
				continue;

			name_rva = (pos - 0x10) - base;
			break;
		}

		if (name_rva) {
			for (size_t pos = rdata_start; pos < rdata_end; pos += 4)
			{
				if (*(uint32_t*)pos == name_rva)
				{
					if (*(uint32_t*)(pos - 0x8) == 0 && *(uint32_t*)(pos - 0xC) == 1)
					{
						uintptr_t rtti_ref = pos - 0xC;
						for (size_t posr = rdata_start; posr < rdata_end; posr += 8)
						{
							if (*(uintptr_t*)posr == rtti_ref)
							{
								return (posr + 8) - base;
							}
						}
					}
				}
			}
		}
	}

	return 0;
}

static int
FindVTables(uintptr_t engine, uintptr_t engine_ds, vtableRef2Engines* ref) {
	int result = 1;

	auto name = ref->name;
	ref->offset_engine = FindVTable(engine, name);
	ref->offset_engine_ds = FindVTable(engine_ds, name);

	if (!ref->offset_engine || !ref->offset_engine_ds) {
		result = 0;
	}

	return result;
}

#else
#define VTABLEREF2ENGINES(N, E, EDS) { (E), (EDS) }
#endif

#if !defined(_DEBUG)
const
#endif
vtableRef2Engines netMessages[] = {
	VTABLEREF2ENGINES("Base_CmdKeyValues", 0x63E5D8, 0x4324B8),
	VTABLEREF2ENGINES("CLC_BaselineAck", 0x607898, 0x4162C8),
	VTABLEREF2ENGINES("CLC_ClientInfo", 0x60FD48, 0x4164E8),
	VTABLEREF2ENGINES("CLC_ClientSayText", 0x5F9B38, 0x416738),
	VTABLEREF2ENGINES("CLC_ClientTick", 0x608668, 0x416088),
	VTABLEREF2ENGINES("CLC_CmdKeyValues", 0x63EAF8, 0x4329E8),
	VTABLEREF2ENGINES("CLC_DurangoVoiceData", 0x621258, 0x416238),
	VTABLEREF2ENGINES("CLC_FileCRCCheck", 0x60FF58, 0x416578),
	VTABLEREF2ENGINES("CLC_ListenEvents", 0x5F6E48, 0x40F758),
	VTABLEREF2ENGINES("CLC_LoadingProgress", 0x60FDD8, 0x416608),
	VTABLEREF2ENGINES("CLC_Move", 0x6086F8, 0x416118),
	VTABLEREF2ENGINES("CLC_PersistenceClientToken", 0x621378, 0x4163E8),
	VTABLEREF2ENGINES("CLC_PersistenceRequestSave", 0x60FE68, 0x416698),
	VTABLEREF2ENGINES("CLC_RespondCvarValue", 0x5F6928, 0x40F0F8),
	VTABLEREF2ENGINES("CLC_SaveReplay", 0x6212E8, 0x416358),
	VTABLEREF2ENGINES("CLC_SplitPlayerConnect", 0x5F6DB8, 0x40F6C8),
	VTABLEREF2ENGINES("CLC_VoiceData", 0x6211C8, 0x4161A8),
	VTABLEREF2ENGINES("CNetMessage", 0x5F35F8, 0x40AA58),
	VTABLEREF2ENGINES("NET_SetConVar", 0x5F6C98, 0x40F5A8),
	VTABLEREF2ENGINES("NET_SignonState", 0x5F6ED8, 0x40F7E8),
	VTABLEREF2ENGINES("NET_SplitScreenUser", 0x63ED38, 0x432CD8),
	VTABLEREF2ENGINES("NET_StringCmd", 0x5F55F8, 0x40B648),
	VTABLEREF2ENGINES("SVC_BSPDecal", 0x5F6508, 0x40EC48),
	VTABLEREF2ENGINES("SVC_ClassInfo", 0x5F6D28, 0x40F638),
	VTABLEREF2ENGINES("SVC_CmdKeyValues", 0x63EB88, 0x432A78),
	VTABLEREF2ENGINES("SVC_CreateStringTable", 0x63EDC8, 0x432D68),
	VTABLEREF2ENGINES("SVC_CrosshairAngle", 0x5F5F38, 0x40E728),
	VTABLEREF2ENGINES("SVC_DurangoVoiceData", 0x5F5E18, 0x40E608),
	VTABLEREF2ENGINES("SVC_EntityMessage", 0x5F5FC8, 0x40E7B8),
	VTABLEREF2ENGINES("SVC_FixAngle", 0x5F5EA8, 0x40E698),
	VTABLEREF2ENGINES("SVC_GameEvent", 0x5F6598, 0x40ECD8),
	VTABLEREF2ENGINES("SVC_GameEventList", 0x5F66B8, 0x40EDF8),
	VTABLEREF2ENGINES("SVC_GetCvarValue", 0x5F6748, 0x40EE88),
	VTABLEREF2ENGINES("SVC_Menu", 0x5F60E8, 0x40E8D8),
	VTABLEREF2ENGINES("SVC_PersistenceBaseline", 0x5F58D8, 0x40E288),
	VTABLEREF2ENGINES("SVC_PersistenceDefFile", 0x5F57A8, 0x40E1F8),
	VTABLEREF2ENGINES("SVC_PersistenceNotifySaved", 0x5F5A08, 0x40E3B8),
	VTABLEREF2ENGINES("SVC_PersistenceUpdateVar", 0x5F5968, 0x40E318),
	VTABLEREF2ENGINES("SVC_PlaylistChange", 0x63EC18, 0x432B08),
	VTABLEREF2ENGINES("SVC_Playlists", 0x5F5CF8, 0x40E4E8),
	VTABLEREF2ENGINES("SVC_Print", 0x5F5718, 0x40E168),
	VTABLEREF2ENGINES("SVC_SendTable", 0x5F63E8, 0x40EB28),
	VTABLEREF2ENGINES("SVC_ServerInfo", 0x5F6358, 0x40EA98),
	VTABLEREF2ENGINES("SVC_ServerTick", 0x5F5688, 0x40E0D8),
	VTABLEREF2ENGINES("SVC_SetPause", 0x5F5C68, 0x40E458),
	VTABLEREF2ENGINES("SVC_SetTeam", 0x63ECA8, 0x432B98),
	VTABLEREF2ENGINES("SVC_Snapshot", 0x5F6628, 0x40ED68),
	VTABLEREF2ENGINES("SVC_Sounds", 0x5F36C8, 0x40AB28),
	VTABLEREF2ENGINES("SVC_SplitScreen", 0x5F67D8, 0x40EF18),
	VTABLEREF2ENGINES("SVC_TempEntities", 0x5F6058, 0x40E848),
	VTABLEREF2ENGINES("SVC_UpdateStringTable", 0x5F6478, 0x40EBB8),
	VTABLEREF2ENGINES("SVC_UserMessage", 0x5F6BD8, 0x40F518),
	VTABLEREF2ENGINES("SVC_VoiceData", 0x5F5D88, 0x40E578),
};

#if defined(_DEBUG)
void
InitDedicatedVtables() {
	uintptr_t engine = (uintptr_t)LoadLibraryW(L"engine.dll");
	uintptr_t engineDS = (uintptr_t)LoadLibraryW(L"engine_ds.dll");

	OutputDebugStringA("vtableRef2Engines netMessages[] = {\n");

	for (size_t i = 0; i < (sizeof(netMessages) / sizeof(netMessages[0])); i++) {
		auto msg = &netMessages[i];
		static char buf[512];
		if (FindVTables(engine, engineDS, msg)) {
			sprintf_s(buf, "    VTABLEREF2ENGINES(\"%s\", 0x%04llX, 0x%04llX),\n", msg->name, msg->offset_engine, msg->offset_engine_ds);
			OutputDebugStringA(buf);
		}
		else {
			sprintf_s(buf, "    VTABLEREF2ENGINES(\"%s\", 0x%04llX, 0x%04llX), // !!! ERROR !!!\n", msg->name, msg->offset_engine, msg->offset_engine_ds);
			OutputDebugStringA(buf);
		}

		auto dsVtable = engineDS + msg->offset_engine_ds;
		auto vtable = engine + msg->offset_engine;

		if (dsVtable && vtable) {
			for (int i = 0; i < 14; ++i) {
				LPVOID pTarget = ((LPVOID*)dsVtable)[i];
				LPVOID pDetour = ((LPVOID*)vtable)[i];
				MH_CreateHook(pTarget, pDetour, NULL);
			}
		}
	}

	OutputDebugStringA("};\n");
}
#endif

typedef void (*CUtlBuffer__SetExternalBufferType)(__int64 a1, __int64 a2, __int64 a3, __int64 a4, char a5);
CUtlBuffer__SetExternalBufferType CUtlBuffer__SetExternalBufferOriginal;
char* textbuf = 0;
__int64 pdefsize = 0;
void __fastcall CUtlBuffer__SetExternalBuffer(__int64 a1, __int64 a2, __int64 a3, __int64 a4, char a5)
{
	uintptr_t engineDS = G_engine_ds;
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
	auto whatthefuck = (__int64 (*)(__int64 a1, int* a2, __int64 a3, int a4, unsigned int a5, unsigned int a6, unsigned int a7))(G_engine + 0x42A450);
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
	if (strcmp_static(fmt, "%c00000000000000") == 0) {
		const char** gamemode = reinterpret_cast<const char**>(G_server + 0xB68520);
		const char* mapname = reinterpret_cast<const char*>(G_engine_ds + 0x1C89A84);
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
	uintptr_t engineDS = G_engine_ds;
	auto ret = bf_write__WriteUBitLongOriginal(a1, a2, a3);
	if (uintptr_t(_ReturnAddress()) == (engineDS + 0x51018) || uintptr_t(_ReturnAddress()) == (engineDS + 0x5101D))
		bf_write__WriteUBitLongOriginal(a1, 0, 14);
	return ret;
}


typedef __int64(*Host_InitDedicatedType)(__int64 a1, __int64 a2, __int64 a3);
Host_InitDedicatedType Host_InitDedicatedOriginal;
__int64 Host_InitDedicated(__int64 a1, __int64 a2, __int64 a3)
{
	//CModule engine("engine.dll", (uintptr_t)LoadLibraryA("engine.dll"));
	//CModule engineDS("engine_ds.dll");
	uintptr_t engine = (uintptr_t)LoadLibraryW(L"engine.dll");
	uintptr_t engineDS = G_engine_ds;

	NET_CreateNetChannelOriginal = NET_CreateNetChannelType(engine + 0x1F1B10);
	MH_CreateHook(LPVOID(engineDS + 0x13CA10), LPVOID(engine + 0x1EC7B0), NULL); // NET_BufferToBufferCompress
	MH_CreateHook(LPVOID(engineDS + 0x13DD90), LPVOID(engine + 0x1EDB40), NULL); // NET_BufferToBufferDecompress
	MH_CreateHook(LPVOID(engineDS + 0x144C60), LPVOID(engine + 0x1F4AC0), NULL); // NET_ClearQueuedPacketsForChannel
	MH_CreateHook(LPVOID(engineDS + 0x141D60), &NET_CreateNetChannel, NULL); // NET_CreateNetChannel LPVOID(engine + 0x1F1B10)
	MH_CreateHook(LPVOID(engineDS + 0x13F7A0), LPVOID(engine + 0x1EF550), NULL); // NET_GetUDPPort
	MH_CreateHook(LPVOID(engineDS + 0x145440), LPVOID(engine + 0x1F5220), NULL); // NET_Init
	MH_CreateHook(LPVOID(engineDS + 0x13C9D0), LPVOID(engine + 0x1EC770), NULL); // NET_InitPostFork
	MH_CreateHook(LPVOID(engineDS + 0x13C8D0), LPVOID(engine + 0x1EC660), NULL); // NET_IsDedicated
	MH_CreateHook(LPVOID(engineDS + 0x13C8C0), LPVOID(engine + 0x1EC650), NULL); // NET_IsMultiplayer
	MH_CreateHook(LPVOID(engineDS + 0x13F7D0), LPVOID(engine + 0x1EF580), NULL); // NET_ListenSocket
	Original_NET_OutOfBandPrintf = NET_OutOfBandPrintf_t(engine + 0x1F47C0);
	MH_CreateHook(LPVOID(engineDS + 0x144960), LPVOID(Detour_NET_OutOfBandPrintf), NULL); // NET_OutOfBandPrintf
	MH_CreateHook(LPVOID(engineDS + 0x142ED0), LPVOID(engine + 0x1F2CF0), NULL); // NET_ProcessSocket
	MH_CreateHook(LPVOID(engineDS + 0x1458A0), LPVOID(engine + 0x1F5650), NULL); // NET_RemoveNetChannel
	MH_CreateHook(LPVOID(engineDS + 0x143390), LPVOID(engine + 0x1F31B0), NULL); // NET_RunFrame
	NET_SendPacketOriginal = NET_SendPacketType(engine + 0x1F4130);
	MH_CreateHook(LPVOID(engineDS + 0x1442D0), LPVOID(engine + 0x1F4130), NULL); // NET_SendPacket
	MH_CreateHook(LPVOID(engineDS + 0x144D10), LPVOID(engine + 0x1F4B70), NULL); // NET_SendQueuedPackets
	MH_CreateHook(LPVOID(engineDS + 0x1453D0), LPVOID(engine + 0x1F51B0), NULL); // NET_SetMultiplayer
	MH_CreateHook(LPVOID(engineDS + 0x1434C0), LPVOID(engine + 0x1F32E0), NULL); // NET_Shutdown
	MH_CreateHook(LPVOID(engineDS + 0x13FAB0), LPVOID(engine + 0x1EF860), NULL); // NET_SleepUntilMessages
	MH_CreateHook(LPVOID(engineDS + 0x13C3E0), LPVOID(engine + 0x1EC1B0), NULL); // NET_StringToAdr
	MH_CreateHook(LPVOID(engineDS + 0x13C100), LPVOID(engine + 0x1EBED0), NULL); // NET_StringToSockaddr
	MH_CreateHook(LPVOID(engineDS + 0x146D50), LPVOID(engine + 0x1F6B90), NULL); // INetMessage__WriteToBuffer
	MH_CreateHook(LPVOID(engineDS + 0x13B000), LPVOID(engine + 0x1E9EA0), NULL); // CNetChan__CNetChan__dtor
	MH_CreateHook(LPVOID(engineDS + 0x017940), LPVOID(engine + 0x028BC0), NULL); // CLC_SplitPlayerConnect__dtor
	MH_CreateHook(LPVOID(engineDS + 0x12F140), LPVOID(engine + 0x1DC830), NULL); // SendTable_WriteInfos
	MH_CreateHook(LPVOID(engineDS + 0x71C0), &bf_write__WriteUBitLong, reinterpret_cast<LPVOID*>(&bf_write__WriteUBitLongOriginal)); // bf_write__WriteUBitLong
	MH_CreateHook(LPVOID(engineDS + 0x497F0), LPVOID(engine + 0xD8420), NULL); // CBaseClient::ConnectionStart

#if defined(_DEBUG) && VTABLE_UPDATE_FORCE
	OutputDebugStringA("const vtableRef2Engines netMessages[] = {\n");
#endif

	for (size_t i = 0; i < (sizeof(netMessages) / sizeof(netMessages[0])); i++) {
		auto msg = &netMessages[i];
#if defined(_DEBUG)
		if (VTABLE_UPDATE_FORCE || !msg->offset_engine || !msg->offset_engine_ds) {
			static char buf[512];
			if (FindVTables(engine, engineDS, msg)) {
				sprintf_s(buf, "    VTABLEREF2ENGINES(\"%s\", 0x%04llX, 0x%04llX),\n", msg->name, msg->offset_engine, msg->offset_engine_ds);
				OutputDebugStringA(buf);
			}
			else {
				sprintf_s(buf, "    VTABLEREF2ENGINES(\"%s\", 0x%04llX, 0x%04llX), // !!! ERROR !!!\n", msg->name, msg->offset_engine, msg->offset_engine_ds);
				OutputDebugStringA(buf);
			}
		}
#endif

		auto dsVtable = engineDS + msg->offset_engine_ds;
		auto vtable = engine + msg->offset_engine;

		if (dsVtable && vtable) {
			for (int i = 0; i < 14; ++i) {
				LPVOID pTarget = ((LPVOID*)dsVtable)[i];
				LPVOID pDetour = ((LPVOID*)vtable)[i];
				MH_CreateHook(pTarget, pDetour, NULL);
			}
		}
	}

#if defined(_DEBUG) && VTABLE_UPDATE_FORCE
	OutputDebugStringA("};\n");
#endif

	MH_EnableHook(MH_ALL_HOOKS);
	reinterpret_cast<char(__fastcall*)(__int64, CreateInterfaceFn)>((uintptr_t)(engine) + 0x01A04A0)(0, (CreateInterfaceFn)(engineDS + 0xE9000)); // connect nondedi engine
	reinterpret_cast<void(__fastcall*)(int, void*)>((uintptr_t)(engine) + 0x47F580)(0, 0); // register nondedi engine cvars
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

	auto engine_ds = G_engine_ds;

	for (auto offset : offsets) {
		int* address = reinterpret_cast<int*>(engine_ds + offset);

		DWORD oldProtect;
		if (!VirtualProtect(address, sizeof(uintptr_t), PAGE_EXECUTE_READWRITE, &oldProtect)) {
			continue;
		}

		*address += 4;

		DWORD temp;
		VirtualProtect(address, sizeof(uintptr_t), oldProtect, &temp);

		FlushInstructionCache(GetCurrentProcess(), address, sizeof(uintptr_t));
	}

	MH_CreateHook((LPVOID)(engine_ds + 0x1693D0), &ParsePDEF, reinterpret_cast<LPVOID*>(&ParsePDATAOriginal));
	MH_CreateHook((LPVOID)(engine_ds + 0x3259C0), &CUtlBuffer__SetExternalBuffer, reinterpret_cast<LPVOID*>(&CUtlBuffer__SetExternalBufferOriginal));
	MH_CreateHook((LPVOID)(engine_ds + 0x160130), &sub_160130, reinterpret_cast<LPVOID*>(NULL));
	MH_CreateHook((LPVOID)(engine_ds + 0xA1B90), &Host_InitDedicated, reinterpret_cast<LPVOID*>(&Host_InitDedicatedOriginal));

	MH_EnableHook(MH_ALL_HOOKS);
}