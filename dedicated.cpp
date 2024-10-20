#include "dedicated.h"
#include <intrin.h>
#include "logging.h"
#include "load.h"
#include "dedicated.h"
#include "netchanwarnings.h"
#pragma intrinsic(_ReturnAddress)

#define VTABLE_UPDATE_FORCE 1

#if defined(_DEBUG)

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
	if (uintptr_t(_ReturnAddress()) == (engineDS + 0x51018) || uintptr_t(_ReturnAddress()) == (engineDS + 0x5101D)) {
		a1->WriteOneBit(0);
		a1->WriteOneBit(0);
		a1->WriteUBitLong(0, 12);
	}
	return ret;
}


typedef __int64(*Host_InitDedicatedType)(__int64 a1, __int64 a2, __int64 a3);
Host_InitDedicatedType Host_InitDedicatedOriginal;
__int64 Host_InitDedicated(__int64 a1, __int64 a2, __int64 a3)
{
	//CModule engine("engine.dll", (uintptr_t)LoadLibraryA("engine.dll"));
	//CModule engineDS("engine_ds.dll");
	//InitDedicatedVtables();
	uintptr_t engine = (uintptr_t)LoadLibraryA("engine.dll");
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
	// copy sendtable funcs
	DWORD oldProtect;
	VirtualProtect((LPVOID)(G_engine_ds + 0x550760), 173 * sizeof(uintptr_t), PAGE_READWRITE, &oldProtect);
	memcpy((void*)(G_engine_ds + 0x550760), (void*)(G_engine + 0x7CB3F0), 173 * sizeof(uintptr_t));
	VirtualProtect((LPVOID)(G_engine_ds + 0x550760), 173 * sizeof(uintptr_t), oldProtect, &oldProtect);
	MH_EnableHook(MH_ALL_HOOKS);
	reinterpret_cast<char(__fastcall*)(__int64, CreateInterfaceFn)>((uintptr_t)(engine) + 0x01A04A0)(0, (CreateInterfaceFn)(engineDS + 0xE9000)); // connect nondedi engine
	reinterpret_cast<void(__fastcall*)(int, void*)>((uintptr_t)(engine) + 0x47F580)(0, 0); // register nondedi engine cvars
#ifdef _DEBUG
	if (!InitNetChanWarningHooks())
		MessageBoxA(NULL, "Failed to initialize warning hooks", "ERROR", 16);
#endif
	return Host_InitDedicatedOriginal(a1, a2, a3);
}
char* __fastcall sub_311910(char* a1, const char* a2, signed __int64 a3)
{
	char* result; // rax

	result = strncpy(a1, a2, a3);
	if (a3 > 0)
		a1[a3 - 1] = 0;
	return result;
}
char __fastcall CBaseClient__ProcessClientInfo(__int64 a1, __int64 a2)
{
	char v4; // al
	int v5; // eax
	int v6; // eax
	int v7; // eax

	*(_DWORD*)(a1 + 940) = *(_DWORD*)(a2 + 32);
	v4 = *(_BYTE*)(a2 + 44);
	*(_DWORD*)(a1 + 976) = 0;
	*(_BYTE*)(a1 + 928) = v4;
	sub_311910((char*)(a1 + 648), (const char*)(a2 + 52), 256i64);
	*(_QWORD*)(a1 + 944) = *(unsigned int*)(a2 + 308);
	v5 = *(_DWORD*)(a2 + 312);
	*(_DWORD*)(a1 + 956) = 0;
	*(_DWORD*)(a1 + 952) = v5;
	v6 = *(_DWORD*)(a2 + 316);
	*(_DWORD*)(a1 + 964) = 0;
	*(_DWORD*)(a1 + 960) = v6;
	v7 = *(_DWORD*)(a2 + 320);
	*(_DWORD*)(a1 + 972) = 0;
	*(_DWORD*)(a1 + 968) = v7;
	if (*(_DWORD*)(a2 + 40) != (*(unsigned int(__fastcall**)(_QWORD))(**(_QWORD**)(a1 + 920) + 120i64))(*(_QWORD*)(a1 + 920)))
		(*(void(__fastcall**)(__int64))(*(_QWORD*)(a1 - 8) + 96i64))(a1 - 8);
	return 1;
}
static float		host_nexttick = 0;		// next server tick in this many ms
static float* host_state_interval_per_tick;
static float* host_remainder;
static void (*oCbuf_Execute)(void);
static void Cbuf_Execute() {
	static uintptr_t ret_from_host_runframe = G_engine_ds + 0xA181E;
	if (uintptr_t(_ReturnAddress()) == ret_from_host_runframe) {
		if (!host_state_interval_per_tick)
			host_state_interval_per_tick = (float*)(G_engine_ds + 0x547300);
		if (!host_remainder)
			host_remainder = (float*)(G_engine_ds + 0x20408C0);
		host_nexttick = *host_state_interval_per_tick - *host_remainder;
	}
	oCbuf_Execute();
}
static bool CEngine__FilterTime(void* thisptr, float dt, float* flMinFrameTime)
{
	*flMinFrameTime = host_nexttick;
	return (dt >= host_nexttick);
}
void* (*oKeyValues__SetString__Dedi)(__int64 a1, char* a2, const char* a3);
void* KeyValues__SetString__Dedi(__int64 a1, char* a2, const char* a3)
{
	static auto target = G_engine_ds + 0xC36AD;
	if (uintptr_t(_ReturnAddress()) == target)
		a3 = "30"; // force replay updaterate to 60
	return oKeyValues__SetString__Dedi(a1, a2, a3);
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
	MH_CreateHook((LPVOID)(engine_ds + 0xA1B90), &Host_InitDedicated, reinterpret_cast<LPVOID*>(&Host_InitDedicatedOriginal));
	MH_CreateHook((LPVOID)(engine_ds + 0x31EB20), &ConVar_PrintDescription, reinterpret_cast<LPVOID*>(&ConVar_PrintDescriptionOriginal));
	MH_CreateHook((LPVOID)(engine_ds + 0x310780), &sub_1804722E0, 0);
	MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("engine_ds.dll") + 0x45C00), &CBaseClient__ProcessClientInfo, reinterpret_cast<LPVOID*>(NULL));
	MH_CreateHook((LPVOID)(engine_ds + 0x756E0), &Cbuf_Execute, reinterpret_cast<LPVOID*>(&oCbuf_Execute));
	MH_CreateHook((LPVOID)(engine_ds + 0xEF480), &CEngine__FilterTime, reinterpret_cast<LPVOID*>(NULL));
	MH_CreateHook((LPVOID)(engine_ds + 0x318D60), &KeyValues__SetString__Dedi, reinterpret_cast<LPVOID*>(&oKeyValues__SetString__Dedi));

	//MH_CreateHook((LPVOID)(engine_ds + 0x360230), &vsnprintf_l_hk, NULL);
	MH_EnableHook(MH_ALL_HOOKS);
}