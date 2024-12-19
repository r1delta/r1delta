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

#include "load.h"
#include <cstdlib>
#include <crtdbg.h>
#include <new>
#include "windows.h"

#include <iostream>
#include "cvar.h"
#include <winternl.h>  // For UNICODE_STRING.
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
#include "engine.h"
#include "voice.h"
#include <tier1/utlbuffer.h>

#pragma intrinsic(_ReturnAddress)

extern "C"
{
	uintptr_t CNetChan__ProcessSubChannelData_ret0 = 0;
	uintptr_t CNetChan__ProcessSubChannelData_Asm_continue = 0;
	extern uintptr_t CNetChan__ProcessSubChannelData_AsmConductBufferSizeCheck;
}
void* dll_notification_cookie_;
int* g_nServerThread;
IBaseClientDLL* g_ClientDLL = nullptr;

void Status_ConMsg(const char* text, ...)
// clang-format off
{
	char formatted[2048];
	va_list list;

	va_start(list, text);
	vsprintf_s(formatted, text, list);
	va_end(list);

	auto endpos = strlen(formatted);
	if (formatted[endpos - 1] == '\n')
		formatted[endpos - 1] = '\0'; // cut off repeated newline

	Msg("%s\n", formatted);
}

signed __int64 __fastcall sub_1804735A0(char* a1, signed __int64 a2, const char* a3, va_list a4) {
	static bool recursive2 = false;
	signed __int64 result; // rax

	if (a2 <= 0)
		return 0i64;
	result = vsnprintf(a1, a2, a3, a4);
	if ((int)result < 0i64 || (int)result >= a2) {
		result = a2 - 1;
		a1[a2 - 1] = 0;
	}
	if (!recursive2) {
		recursive2 = true;
		Msg("%s\n", a1);
		recursive2 = false;
	}
	return result;
}
void sub_180473550(char* a1, signed __int64 a2, const char* a3, ...) {
	int v5; // eax
	va_list ArgList; // [rsp+58h] [rbp+20h] BYREF

	va_start(ArgList, a3);
	if (a2 > 0) {
		v5 = vsnprintf(a1, a2, a3, ArgList);
		if (v5 < 0i64 || v5 >= a2)
			a1[a2 - 1] = 0;
	}
	std::cout << a1 << std::endl;
}

BOOL RemoveItemsFromVTable(uintptr_t vTableAddr, size_t indexStart, size_t itemCount) {
	if (vTableAddr == 0) return FALSE;

	uintptr_t* vTable = reinterpret_cast<uintptr_t*>(vTableAddr);
	size_t vTableSize = 77;

	if (indexStart + itemCount > vTableSize) return FALSE;

	size_t elementsToShift = vTableSize - (indexStart + itemCount);

	DWORD oldProtect;
	if (!VirtualProtect(reinterpret_cast<LPVOID>(vTableAddr), vTableSize * sizeof(uintptr_t), PAGE_EXECUTE_READWRITE, &oldProtect)) {
		std::cerr << "Failed to change memory protection." << std::endl;
		return FALSE;
	}

	for (size_t i = 0; i < elementsToShift; ++i) {
		vTable[indexStart + i] = vTable[indexStart + itemCount + i];
	}

	memset(&vTable[vTableSize - itemCount], 0, itemCount * sizeof(uintptr_t));

	// Restore the original memory protection
	if (!VirtualProtect(reinterpret_cast<LPVOID>(vTableAddr), vTableSize * sizeof(uintptr_t), oldProtect, &oldProtect)) {
		std::cerr << "Failed to restore memory protection." << std::endl;
		return FALSE;
	}

	return TRUE;
}
struct SavedCall {
	__int64 a1;
	std::string a2;
	int a3;
};

std::vector<SavedCall> savedCalls;

bool shouldSave(const char* a2) {
	// TODO(mrsteyk): move thisptr out of here...
	struct str8 {
		char* p;
		size_t l;
	};
#define str8_lit_(S) { (char*)S, sizeof(S) - 1 }
	const str8 commandsToSave[] = {
		str8_lit_("InitHUD"),
		str8_lit_("FullyConnected"),
		str8_lit_("MatchIsOver"),
		str8_lit_("ChangeTitanBuildPoint"),
		str8_lit_("ChangeCurrentTitanBuildPoint"),
		str8_lit_("TutorialStatus"),
		str8_lit_("ChangeCurrentTitanOID")
	};
#undef str8_lit_
	for (auto& command : commandsToSave) {
		if (memcmp(a2, command.p, command.l + 1) == 0) {
			return true;
		}
	}
	return false;
}

typedef __int64 (*sub_629740Type)(__int64 a1, const char* a2, int a3);
sub_629740Type sub_629740Original;

__int64 __fastcall sub_629740(__int64 a1, const char* a2, int a3) {
	if (shouldSave(a2)) {
		//savedCalls.push_back({ a1, a2, a3 });
		return -1;
	}
	else if (strcmp_static(a2, "RemoteWeaponReload") == 0) {
		//for (auto& call : savedCalls) {
		//	sub_629740Original(call.a1, call.a2.c_str(), call.a3);
		//}
		//savedCalls.clear(); // Clear after processing
	}
	return sub_629740Original(a1, a2, a3);
}

void __fastcall cl_DumpPrecacheStats(__int64 CClientState, const char* name) {
	/*
	freopen("CONIN$", "r", stdin);
	freopen("CONOUT$", "w", stdout);
	freopen("CONOUT$", "w", stderr);
	*/
	auto outhandle = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);

	if (!name || !name[0]) {
		//std::cout << "Can only dump stats when active in a level" << std::endl;
		const char error[] = "Can only dump stats when active in a level\r\n";
		WriteFile(outhandle, error, sizeof(error) - 1, 0, 0);
		return;
	}

	uintptr_t items = 0;

	if (!strcmp_static(name, "modelprecache")) {
		items = CClientState + 0x19EF8;
	}
	else if (!strcmp_static(name, "genericprecache")) {
		items = CClientState + 0x1DEF8;
	}
	else if (!strcmp_static(name, "decalprecache")) {
		items = CClientState + 0x1FEF8;
	}

	using GetStringTable_t = uintptr_t(__fastcall*)(uintptr_t, const char*);
	auto GetStringTable = GetStringTable_t(G_engine + 0x22B60);
	auto table = GetStringTable(CClientState, name);

	if (!items || !table) {
		//std::cout << "!items || !table" << std::endl;
		const char error[] = "!items || !table\r\n";
		WriteFile(outhandle, error, sizeof(error) - 1, 0, 0);
		return;
	}

	auto count = (*(int64_t(__fastcall**)(uintptr_t))(*(uintptr_t*)table + 24i64))(table);

	//std::cout << std::endl << "Precache table " << name << ":  " << count << " out of UNK slots used" << std::endl;
	char buf[1024];
	auto towrite = sprintf_s(buf, "Precache table %s:  %d out of UNK slots used\n", name, int(count));
	WriteFile(outhandle, buf, towrite, 0, 0);

	using CL_GetPrecacheUserData_t = uintptr_t(__fastcall*)(uintptr_t, uintptr_t);
	auto CL_GetPrecacheUserData = CL_GetPrecacheUserData_t(G_engine + 0x558C0);

	for (int i = 0; i < count; i++) {
		auto slot = items + (i * 16);

		auto name = (*(const char* (__fastcall**)(__int64, _QWORD))(*(_QWORD*)table + 72i64))(table, i);

		auto p = CL_GetPrecacheUserData(table, i);

		auto refcount = (*(uint32_t*)slot) >> 3;

		if (!name || !p) {
			continue;
		}

		//Status_ConMsg("%03i:  %s (%s):   ", i, name, "");
		/*
		if (i < 10) {
			std::cout << "00";
		}
		else if (i < 100) {
			std::cout << "0";
		}
		std::cout << i << ":  " << name << " ():    ";
		*/
		towrite = sprintf_s(buf, "%03i:  %s (%s):   ", i, name, "");
		WriteFile(outhandle, buf, towrite, 0, 0);
		if (refcount == 0) {
			//std::cout << "never used" << std::endl;
			const char msg[] = "never used\r\n";
			WriteFile(outhandle, msg, sizeof(msg) - 1, 0, 0);
		}
		else {
			//std::cout << refcount << " refs," << std::endl;
			towrite = sprintf_s(buf, "%i refcounts\r\n", refcount);
			WriteFile(outhandle, buf, towrite, 0, 0);
		}
	}

	FlushFileBuffers(outhandle);
}

struct string_nodebug {
	union {
		char inl[16]{0};
		const char* ptr;
	};
	// ?????
	//uint32_t size = 0;
	//uint32_t capacity = 0;
	size_t size = 0;
	size_t big_size = 0;
};
static_assert(sizeof(string_nodebug) == 4 * 8, "AAA");
std::array<string_nodebug[2], 100> g_militia_bodies;
// #STR: "%sserver_%s%s", "-sharedservervpk"
typedef __int64 (*sub_136E70Type)(char* pPath);
sub_136E70Type sub_136E70Original;
__int64 __fastcall sub_136E70(char* pPath) {
	auto ret = sub_136E70Original(pPath);
	reinterpret_cast<__int64(*)()>(G_engine + 0x19D730)();
	return ret;
}
void* SetPreCache_o = nullptr;
__int64 __fastcall SetPreCache(__int64 a1, __int64 a2, char a3) {
	auto ret = reinterpret_cast<decltype(&SetPreCache)>(SetPreCache_o)(a1, a2, a3);

	using sub_1800F5680_t = char(__fastcall*)(const char* a1, __int64 a2, void* a3, void* a4);
	static auto sub_1800F5680 = sub_1800F5680_t(uintptr_t(GetModuleHandleA("engine_r1o.dll")) + 0xF5680);

	static auto array_start = uintptr_t(GetModuleHandleA("engine_r1o.dll")) + 0x2555C00;

	auto idx = (a1 - array_start) / 10064;
	// assert that no mod and no more than 100...
	auto elem = &g_militia_bodies[idx];
	auto militia_exists = sub_1800F5680("bodymodel_militia", a2, &elem[0]->ptr, &elem[0]->big_size);

	if (!*(_QWORD*)(a1 + 488))
		sub_1800F5680("armsmodel_imc", a2, PVOID(a1 + 472), PVOID(a1 + 488));

	auto armsmodel_militia_exists = sub_1800F5680("armsmodel_militia", a2, &elem[1]->ptr, &elem[1]->big_size);

	return ret;
}

void CHL2_Player_Precache(uintptr_t a1, uintptr_t a2) {
	auto server_mod = G_server;
	auto byte_180C318A6 = (uint8_t*)(server_mod + 0xC318A6);

	if (*byte_180C318A6) {
		using sub_1804FE8B0_t = uintptr_t(__fastcall*)(uintptr_t, uintptr_t);
		auto sub_1804FE8B0 = sub_1804FE8B0_t(server_mod + 0x4FE8B0);

		auto EffectDispatch_ptr = (uintptr_t*)(server_mod + 0xC310C8);
		auto EffectDispatch = *EffectDispatch_ptr;

		sub_1804FE8B0(a1, a2);

		(*(void(__fastcall**)(__int64, __int64, const char*, __int64, _QWORD))(*(_QWORD*)EffectDispatch + 64i64))(
			EffectDispatch,
			1,
			"waterripple",
			0xFFFFFFFFi64,
			0i64);
		(*(void(__fastcall**)(__int64, __int64, const char*, __int64, _QWORD))(*(_QWORD*)EffectDispatch + 64i64))(
			EffectDispatch,
			1,
			"watersplash",
			0xFFFFFFFFi64,
			0i64);

		auto StaticClassSystem001_ptr = (uintptr_t*)(server_mod + 0xC31000);
		auto StaticClassSystem001 = *StaticClassSystem001_ptr;

		using PrecacheModel_t = uintptr_t(__fastcall*)(const void*);
		auto PrecacheModel = PrecacheModel_t(server_mod + 0x3B6A40);

		PrecacheModel("models/weapons/arms/atlaspov.mdl");
		PrecacheModel("models/weapons/arms/atlaspov_cockpit2.mdl");
		PrecacheModel("models/weapons/arms/human_pov_cockpit.mdl");
		PrecacheModel("models/weapons/arms/ogrepov.mdl");
		PrecacheModel("models/weapons/arms/ogrepov_cockpit.mdl");
		PrecacheModel("models/weapons/arms/petepov_workspace.mdl");
		PrecacheModel("models/weapons/arms/pov_imc_pilot_male_br.mdl");
		PrecacheModel("models/weapons/arms/pov_imc_pilot_male_cq.mdl");
		PrecacheModel("models/weapons/arms/pov_imc_pilot_male_dm.mdl");
		PrecacheModel("models/weapons/arms/pov_imc_spectre.mdl");
		PrecacheModel("models/weapons/arms/pov_male_anims.mdl");
		PrecacheModel("models/weapons/arms/pov_mcor_pilot_male_br.mdl");
		PrecacheModel("models/weapons/arms/pov_mcor_pilot_male_cq.mdl");
		PrecacheModel("models/weapons/arms/pov_mcor_pilot_male_dm.mdl");
		PrecacheModel("models/weapons/arms/pov_mcor_spectre_assault.mdl");
		PrecacheModel("models/weapons/arms/pov_pete_core.mdl");
		PrecacheModel("models/weapons/arms/pov_pilot_female_br.mdl");
		PrecacheModel("models/weapons/arms/pov_pilot_female_cq.mdl");
		PrecacheModel("models/weapons/arms/pov_pilot_female_dm.mdl");
		PrecacheModel("models/weapons/arms/stryderpov.mdl");
		PrecacheModel("models/weapons/arms/stryderpov_cockpit.mdl");

		for (size_t i = 0; i < 100; i++) {
			auto v5 = (*(__int64(__fastcall**)(__int64, _QWORD))(*(_QWORD*)StaticClassSystem001 + 24i64))(StaticClassSystem001, i);
			auto elem = g_militia_bodies[i];

			// if (*(_QWORD*)(v5 + 448))
			{
				for (size_t i_ = 0; i_ < 16; i_++) {
					// IMC
					if (*(_QWORD*)(v5 + 448)) {
						auto v7 = (_BYTE*)(v5 + 432);
						if (*(_QWORD*)(v5 + 456) >= 0x10ui64)
							v7 = *(_BYTE**)v7;
						PrecacheModel(v7);
					}

					// MCOR
					if (elem[0].size) {
						const char* v7 = elem[0].inl;
						if (elem[0].big_size >= 0x10)
							v7 = elem[0].ptr;
						PrecacheModel(v7);
					}

					// armsmodel IMC
					if (*(_QWORD*)(v5 + 488)) {
						auto v8 = (_BYTE*)(v5 + 472);
						if (*(_QWORD*)(v5 + 496) >= 0x10ui64)
							v8 = *(_BYTE**)v8;
						PrecacheModel(v8);
					}

					// armsmodel MCOR
					if (elem[1].size) {
						const char* v7 = elem[1].inl;
						if (elem[1].big_size >= 0x10)
							v7 = elem[1].ptr;
						PrecacheModel(v7);
					}
				}

				// cockpitmodel
				if (*(_QWORD*)(v5 + 528)) {
					auto v9 = (_BYTE*)(v5 + 512);
					if (*(_QWORD*)(v5 + 536) >= 0x10ui64)
						v9 = *(_BYTE**)v9;
					PrecacheModel(v9);
				}
			}
		}
	}
}

bool isProcessingSendTables = false;
typedef char* (__cdecl* COM_StringCopyType)(char* in);
COM_StringCopyType COM_StringCopyOriginal;
char* __cdecl COM_StringCopy(char* in) {
	if (isProcessingSendTables) {
		std::ofstream file("test.txt", std::ios::app);
		file << in << "\n";
		file.close();
	}
	return COM_StringCopyOriginal(in);
}

typedef char(__fastcall* DataTable_SetupReceiveTableFromSendTableType)(__int64, __int64);
DataTable_SetupReceiveTableFromSendTableType DataTable_SetupReceiveTableFromSendTableOriginal;
char __fastcall DataTable_SetupReceiveTableFromSendTable(__int64 a1, __int64 a2) {
	isProcessingSendTables = true;
	char ret = DataTable_SetupReceiveTableFromSendTableOriginal(a1, a2);
	isProcessingSendTables = false;
	return ret;
}

bool ReadConnectPacket2015AndWriteConnectPacket2014(bf_read& msg, bf_write& buffer) {
	char type = msg.ReadByte();
	if (type != 'A') {
		return -1;
	}

	//buffer.WriteLong(-1);
	buffer.WriteByte('A');

	int version = msg.ReadLong();
	if (version != 1040) {
		return -1;
	}
	buffer.WriteLong(1036); // 2014 version

	buffer.WriteLong(msg.ReadLong()); // hostVersion
	int32_t lastChallenge = msg.ReadLong();
	buffer.WriteLong(lastChallenge); // challengeNr
	buffer.WriteLong(msg.ReadLong()); // unknown
	buffer.WriteLong(msg.ReadLong()); // unknown1
	msg.ReadLongLong(); // skip platformUserId

	char tempStr[256];
	msg.ReadString(tempStr, sizeof(tempStr));
	buffer.WriteString(tempStr); // name

	msg.ReadString(tempStr, sizeof(tempStr));
	buffer.WriteString(tempStr); // password

	msg.ReadString(tempStr, sizeof(tempStr));
	buffer.WriteString(tempStr); // unknown2

	int unknownCount = msg.ReadByte();
	buffer.WriteByte(unknownCount);
	for (int i = 0; i < unknownCount; i++) {
		buffer.WriteLongLong(msg.ReadLongLong()); // unknown3
	}

	msg.ReadString(tempStr, sizeof(tempStr));
	buffer.WriteString(tempStr); // serverFilter

	msg.ReadLong(); // skip playlistVersionNumber
	msg.ReadLong(); // skip persistenceVersionNumber
	msg.ReadLongLong(); // skip persistenceHash

	int numberOfPlayers = msg.ReadByte();
	buffer.WriteByte(numberOfPlayers);
	for (int i = 0; i < numberOfPlayers; i++) {
		// Read SplitPlayerConnect from 2015 packet
		int type = msg.ReadUBitLong(6);
		int count = msg.ReadByte();

		// Write SplitPlayerConnect to 2014 packet
		buffer.WriteUBitLong(type, 6);
		buffer.WriteByte(count);

		for (int j = 0; j < count; j++) {
			msg.ReadString(tempStr, sizeof(tempStr));
			buffer.WriteString(tempStr); // key

			msg.ReadString(tempStr, sizeof(tempStr));
			buffer.WriteString(tempStr); // value
		}
	}

	buffer.WriteByte(msg.ReadByte()); // lowViolence

	//if (msg.ReadByte() != 1)
	//{
	//	return false;
	//}
	buffer.WriteByte(1);

	return !buffer.IsOverflowed() ? lastChallenge : -1;
}

typedef char (*ProcessConnectionlessPacketType)(unsigned int* a1, netpacket_s* a2);
ProcessConnectionlessPacketType ProcessConnectionlessPacketOriginal;
double lastReceived = 0.f;

char __fastcall ProcessConnectionlessPacketDedi(unsigned int* a1, netpacket_s* a2) {
	char buffer[1200] = {0};
	bf_write writer(reinterpret_cast<char*>(buffer), sizeof(buffer));

	if (((char*)a2->pData + 4)[0] == 'A' && ReadConnectPacket2015AndWriteConnectPacket2014(a2->message, writer) != -1) {
		if (lastReceived == a2->received)
			return false;
		lastReceived = a2->received;
		bf_read converted(reinterpret_cast<char*>(buffer), writer.GetNumBytesWritten());
		a2->message = converted;
		return ProcessConnectionlessPacketOriginal(a1, a2);
	}
	else {
		return ProcessConnectionlessPacketOriginal(a1, a2);
	}
}

ProcessConnectionlessPacketType ProcessConnectionlessPacketOriginalClient;

char __fastcall ProcessConnectionlessPacketClient(unsigned int* a1, netpacket_s* a2) {
	static auto sv_limit_quires = CCVar_FindVar(cvarinterface, "sv_limit_queries");
	static auto& sv_limit_queries_var = sv_limit_quires->m_Value.m_nValue;
	if (sv_limit_queries_var == 1 && a2->pData[4] == 'N') {
		sv_limit_queries_var = 0;
	}
	else if (sv_limit_queries_var == 0 && a2->pData[4] != 'N') {
		sv_limit_queries_var = 1;
	}
	return ProcessConnectionlessPacketOriginalClient(a1, a2);
}

typedef void (*CAI_NetworkManager__BuildStubType)(__int64 a1);
typedef void (*CAI_NetworkManager__LoadNavMeshType)(__int64 a1, __int64 a2, const char* a3);
CAI_NetworkManager__LoadNavMeshType CAI_NetworkManager__LoadNavMeshOriginal;
void __fastcall CAI_NetworkManager__LoadNavMesh(__int64 a1, __int64 a2, const char* a3) {
	CAI_NetworkManager__LoadNavMeshOriginal(a1, a2, a3);
	auto server = G_server;
	*(int*)(server + 0xC317F0) &= ~1;
	((CAI_NetworkManager__BuildStubType)(server + 0x3664C0))(a1);
	((CAI_NetworkManager__BuildStubType)(server + 0x3645f0))(a1);
}

typedef __int64 (*SendTable_CalcDeltaType)(
	__int64 a1,
	__int64 a2,
	__int64 a3,
	__int64 a4,
	int a5,
	__int64 a6,
	int a7,
	int a8,
	int a9);
SendTable_CalcDeltaType SendTable_CalcDeltaOriginal;
typedef __int64 (*CBaseServer__WriteDeltaEntitiesType)(__int64 a1, _DWORD* a2, __int64 a3, __int64 a4, __int64 a5, int a6, unsigned int a7);
CBaseServer__WriteDeltaEntitiesType CBaseServer__WriteDeltaEntitiesOriginal;
__int64 __fastcall CBaseServer__WriteDeltaEntities(__int64 a1, _DWORD* a2, __int64 a3, __int64 a4, __int64 a5, int a6, unsigned int a7) {
	uintptr_t engine = G_engine;
	uintptr_t engine_ds = G_engine_ds;
	*(int*)(engine + 0x2966168) = 2; // force active
	memcpy((void*)(engine + 0x2E9253C), (void*)(engine_ds + 0x200D00C), static_cast<size_t>(static_cast<size_t>(0x2E9BA40) - static_cast<size_t>(0x2E9253C))); // prevent overflow
	memcpy((void*)(engine + 0x2965048), (void*)(engine_ds + 0x1C836A8), 32);
	return CBaseServer__WriteDeltaEntitiesOriginal(a1, a2, a3, a4, a5, a6, a7);
}
__int64 SendTable_CalcDelta(
	__int64 a1,
	__int64 a2,
	__int64 a3,
	__int64 a4,
	int a5,
	__int64 a6,
	int a7,
	int a8,
	int a9) {
	if (a7 < 600)
		a7 = 600;
	if (a8 < 600)
		a8 = 600;
	return SendTable_CalcDeltaOriginal(a1, a2, a3, a4, a5, a6, a7, a8, a9);
}
typedef bool (*SendTable_EncodeType)(
	__int64 a1,
	__int64 a2,
	__int64 a3,
	int a4,
	__int64 pRecipients,
	char a6,
	__int64 a7,
	__int64 a8);
SendTable_EncodeType SendTable_EncodeOriginal;
bool __fastcall SendTable_Encode(
	__int64 a1,
	__int64 a2,
	__int64 a3,
	int a4,
	__int64 pRecipients,
	char a6,
	__int64 a7,
	__int64 a8) {
	return SendTable_EncodeOriginal(a1, a2, a3, a4, pRecipients, 0, a7, a8);
}

typedef void (*COM_InitType)();
COM_InitType COM_InitOriginal;
typedef void (*SaveRestore_InitType)(__int64);
SaveRestore_InitType SaveRestore_Init;
void* pdataempty = 0;

// Function pointer types
using sub_1601A0_t = __int64(__fastcall*)(__int64);
using sub_14EF30_t = void(__fastcall*)(__int64, __int64, DWORD);
using sub_45420_t = void(__fastcall*)(__int64, int);
using sub_4AAD0_t = void(__fastcall*)(__int64);
using sub_16B0F0_t = char(__fastcall*)(__int64);

char __fastcall sub_4C460(__int64 a1) {
	char result; // al
	__int64 v3; // rax
	const char* v4; // rax
	__int64 v5; // r9
	char v6; // [rsp+20h] [rbp-3048h]
	char v7[34872]; // [rsp+30h] [rbp-3038h] BYREF
	if (!pdataempty) {
		pdataempty = calloc(34872, 1);
	}
	// Set up function pointers
	auto engine_mod = G_engine_ds;
	auto enginenotdedi_mod = G_engine;
	auto sub_1601A0 = sub_1601A0_t(engine_mod + 0x1601A0);
	auto sub_14EF30 = sub_14EF30_t(enginenotdedi_mod + 0x1FEDB0);
	auto sub_45420 = sub_45420_t(engine_mod + 0x45420);
	auto sub_4AAD0 = sub_4AAD0_t(engine_mod + 0x4AAD0);
	auto qword_2097510 = (QWORD*)(engine_mod + 0x2097510);
	auto sub_16B0F0 = sub_16B0F0_t(engine_mod + 0x16B0F0);
	sub_16B0F0(a1);
	if (true) {
		v3 = (__int64)pdataempty;
		sub_14EF30((__int64)v7, v3, 0);
		v4 = (const char*)(*(__int64(__fastcall**)(__int64))(*(_QWORD*)(a1 + 8) + 136i64))(a1 + 8);
		Msg("Sending persistence baseline to %s\n", v4);
		v5 = 1;
		v6 = 0;
		(*(void(__fastcall**)(__int64, char*, _QWORD, __int64, char))(*(_QWORD*)(a1 + 8) + 224i64))(
			a1 + 8,
			v7,
			0i64,
			v5,
			v6);
		sub_45420(a1, 6);
		sub_4AAD0(a1);
		if (qword_2097510)
			*(_BYTE*)((*(int(__fastcall**)(__int64))(*(_QWORD*)(a1 + 8) + 112i64))(a1 + 8) + qword_2097510 + 4032) = 0;
		return 1;
	}

	return result;
}
void COM_Init() {
	COM_InitOriginal();
	// SaveRestore__Init();
	SaveRestore_Init = SaveRestore_InitType(G_engine + 0x1410E0);
	SaveRestore_Init(G_engine + 0x2EC8590);
}
typedef void (*CL_Retry_fType)();
CL_Retry_fType CL_Retry_fOriginal;
void CL_Retry_f() {
	uintptr_t engine = G_engine;
	typedef void (*Cbuf_AddTextType)(int a1, const char* a2, unsigned int a3);
	Cbuf_AddTextType Cbuf_AddText = (Cbuf_AddTextType)(engine + 0x102D50);
	if (*(int*)(engine + 0x2966168) == 2) {
		Cbuf_AddText(0, "connect localhost\n", 0);
	}
	else {
		return CL_Retry_fOriginal();
	}
}

typedef char (*SVC_ServerInfo__WriteToBufferType)(__int64 a1, __int64 a2);
SVC_ServerInfo__WriteToBufferType SVC_ServerInfo__WriteToBufferOriginal;
char __fastcall SVC_ServerInfo__WriteToBuffer(__int64 a1, __int64 a2) {
	static const char* real_gamedir = "r1_dlc1";
	*(const char**)(a1 + 72) = real_gamedir;
	if (IsDedicatedServer()) {
		const char** gamemode = reinterpret_cast<const char**>(G_server + 0xB68520);
		*(const char**)(a1 + 88) = *gamemode;
	}
	auto ret = SVC_ServerInfo__WriteToBufferOriginal(a1, a2);
	return ret;
}

typedef void (*ConCommandConstructorType)(
	ConCommandR1* newCommand, const char* name, void (*callback)(const CCommand&), const char* helpString, int flags, void* parent);

void RegisterConCommand(const char* commandName, void (*callback)(const CCommand&), const char* helpString, int flags) {
	ConCommandConstructorType conCommandConstructor = (ConCommandConstructorType)(IsDedicatedServer() ? (G_engine_ds + 0x31F260) : (G_engine + 0x4808F0));
	ConCommandR1* newCommand = new ConCommandR1;

	conCommandConstructor(newCommand, commandName, callback, helpString, flags, nullptr);
}

void RegisterConVar(const char* name, const char* value, int flags, const char* helpString) {
	typedef void (*ConVarConstructorType)(ConVarR1* newVar, const char* name, const char* value, int flags, const char* helpString);
	ConVarConstructorType conVarConstructor = (ConVarConstructorType)(IsDedicatedServer() ? (G_engine_ds + 0x320460) : (G_engine + 0x481AF0));
	ConVarR1* newVar = new ConVarR1;

	conVarConstructor(newVar, name, value, flags, helpString);
}

LDR_DLL_LOADED_NOTIFICATION_DATA* GetModuleNotificationData(const wchar_t* moduleName) {
	HMODULE hMods[1024];
	DWORD cbNeeded;
	MODULEINFO modInfo;

	if (EnumProcessModules(GetCurrentProcess(), hMods, sizeof(hMods), &cbNeeded)) {
		for (DWORD i = 0; i < (cbNeeded / sizeof(HMODULE)); i++) {
			wchar_t szModName[MAX_PATH];
			if (GetModuleFileNameEx(GetCurrentProcess(), hMods[i], szModName, sizeof(szModName) / sizeof(wchar_t))) {
				if (wcsstr(szModName, moduleName) != 0) {
					if (GetModuleInformation(GetCurrentProcess(), hMods[i], &modInfo, sizeof(modInfo))) {
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

class CPluginBotManager {
public:
	virtual void* GetBotController(uint16_t* pEdict);
	virtual __int64 CreateBot(const char* botname);
};

bool isCreatingBot = false;
int botTeamIndex = 0;

__int64 (*oCPortal_Player__ChangeTeam)(__int64 thisptr, unsigned int index);

__int64 __fastcall CPortal_Player__ChangeTeam(__int64 thisptr, unsigned int index) {
	if (isCreatingBot)
		index = botTeamIndex;
	return oCPortal_Player__ChangeTeam(thisptr, index);
}

void AddBotDummyConCommand(const CCommand& args) {
	// Expected usage: bot_dummy -team <team index>
	if (args.ArgC() != 3) {
		Warning("Usage: bot_dummy -team <team index>\n");
		return;
	}

	// Check for the '-team' flag
	if (strcmp(args.Arg(1), "-team") != 0) {
		Warning("Usage: bot_dummy -team <team index>\n");
		return;
	}

	// Parse the team index
	int teamIndex = 0;
	try {
		teamIndex = std::stoi(args.Arg(2));
	}
	catch (const std::invalid_argument&) {
		Warning("Invalid team index: %s\n", args.Arg(2));
		return;
	}
	catch (const std::out_of_range&) {
		Warning("Team index out of range: %s\n", args.Arg(2));
		return;
	}

	botTeamIndex = teamIndex;

	// Use a default bot name for dummy bots
	const char* dummyBotName = "DummyBot";

	HMODULE serverModule = GetModuleHandleA("server.dll");
	if (!serverModule) {
		Warning("Failed to get handle for server.dll\n");
		return;
	}

	typedef CPluginBotManager* (*CreateInterfaceFn)(const char* name, int* returnCode);
	CreateInterfaceFn CreateInterface = reinterpret_cast<CreateInterfaceFn>(GetProcAddress(serverModule, "CreateInterface"));
	if (!CreateInterface) {
		Warning("Failed to get CreateInterface function from server.dll\n");
		return;
	}

	int returnCode = 0;
	CPluginBotManager* pBotManager = CreateInterface("BotManager001", &returnCode);
	if (!pBotManager) {
		Warning("Failed to retrieve BotManager001 interface\n");
		return;
	}

	isCreatingBot = true;
	__int64 pBot = pBotManager->CreateBot(dummyBotName);
	isCreatingBot = false;

	if (!pBot) {
		Warning("Failed to create dummy bot with name: %s\n", dummyBotName);
		return;
	}

	typedef void (*ClientFullyConnectedFn)(__int64 thisptr, __int64 entity);

	ClientFullyConnectedFn CServerGameClients_ClientFullyConnected = (ClientFullyConnectedFn)(G_server + 0x1499E0);

	CServerGameClients_ClientFullyConnected(0, pBot);

	Msg("Dummy bot '%s' has been successfully created and assigned to team %d.\n", dummyBotName, teamIndex);
}

//0x4E2F30

typedef int (*CPlayer_GetLevel_t)(__int64 thisptr);
int __fastcall CPlayer_GetLevel(__int64 thisptr) {
	int xp = *(int*)(thisptr + 0x1834);
	typedef int (*GetLevelFromXP_t)(int xp);
	GetLevelFromXP_t GetLevelFromXP = (GetLevelFromXP_t)(G_server + 0x28E740);
	return GetLevelFromXP(xp);
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

	MH_EnableHook(MH_ALL_HOOKS);
}

std::unordered_map<std::string, std::string> g_LastEntCreateKeyValues;
void (*oCC_Ent_Create)(const CCommand* args);
bool g_bIsEntCreateCommand = false;

void CC_Ent_Create(const CCommand* args) {
	g_LastEntCreateKeyValues.clear();

	int numPairs = (args->ArgC() - 2) / 2;
	g_LastEntCreateKeyValues.reserve(numPairs);

	for (int i = 2; i + 1 < args->ArgC(); i += 2) {
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

__int64 (*oCBaseEntity__VPhysicsInitNormal)(void* a1, unsigned int a2, unsigned int a3, char a4, __int64 a5);
void (*oCBaseEntity__SetMoveType)(void* a1, __int64 a2, __int64 a3);

__int64 CBaseEntity__VPhysicsInitNormal(void* a1, unsigned int a2, unsigned int a3, char a4, __int64 a5) {
	if (uintptr_t(_ReturnAddress()) == (G_server + 0xB63FD))
		return 0;
	else
		return oCBaseEntity__VPhysicsInitNormal(a1, a2, a3, a4, a5);
}
void CBaseEntity__SetMoveType(void* a1, __int64 a2, __int64 a3) {
	if (uintptr_t(_ReturnAddress()) == (G_server + 0xB64EC)) {
		a2 = 5;
		a3 = 1;
	}
	return oCBaseEntity__SetMoveType(a1, a2, a3);
}

__int64 (*ServerClassRegister_7F7E0)(__int64 a1, char* a2, __int64 a3);

__int64 __fastcall HookedServerClassRegister(__int64 a1, char* a2, __int64 a3) {
	static __int64 originalPointerValue = 0;  // Stores where qword_C4A8D0 points to
	static __int64 originalA3 = 0;           // Stores original a3 value

	// Check if it's CDynamicProp
	if (strcmp(a2, "CDynamicProp") == 0) {
		// Store the dereferenced value and original pointer
		originalPointerValue = *(__int64*)a3;
		originalA3 = a3;
	}
	// Check if it's CControlPanelProp
	else if (strcmp(a2, "CControlPanelProp") == 0) {
		// Redirect the pointer
		*(__int64*)a3 = originalPointerValue;
		a3 = originalA3;
	}

	// Call original function
	return ServerClassRegister_7F7E0(a1, a2, a3);
}

typedef void (*CBaseClientSetNameType)(__int64 thisptr, const char* name);
CBaseClientSetNameType CBaseClientSetNameOriginal;

void __fastcall HookedCBaseClientSetName(__int64 thisptr, const char* name) {
	/*
	* Restrict client names to printable ASCII characters.
Enforce a maximum length of 32 characters.
	*/

	const char* nameBuffer = name;

	// Check if the name is too long
	if (strlen(name) > 32) {
		// Truncate the name
		char truncatedName[256];
		strncpy_s(truncatedName, name, 32);
		truncatedName[32] = '\0';

		nameBuffer = truncatedName;
	}

	// Check if the name contains any non-printable ASCII characters
	for (size_t i = 0; i < strlen(name); i++) {
		if (name[i] < 32 || name[i] > 126) {
			// Remove the non-printable character
			char printableName[256];
			size_t j = 0;
			for (size_t i = 0; i < strlen(name); i++) {
				if (name[i] >= 32 && name[i] <= 126) {
					printableName[j] = name[i];
					j++;
				}
			}
			printableName[j] = '\0';
			nameBuffer = printableName;
			break;
		}
	}

	Msg("Updated client name: %s to: %s\n", name, nameBuffer);
	CBaseClientSetNameOriginal(thisptr, nameBuffer);
}

typedef void* (*CEntityFactoryDictionary__CreateType)(void* thisptr, const char* pClassName);
CEntityFactoryDictionary__CreateType CEntityFactoryDictionary__CreateOriginal;
void* CEntityFactoryDictionary__Create(void* thisptr, const char* pClassName) {
	if (strstr(pClassName, "prop_physics") != NULL) {// && uintptr_t(_ReturnAddress()) != mapload) {
		pClassName = "prop_dynamic_override";
	}
	/*if(strstr(pClassName, "prop_control_panel") != NULL) {
		pClassName = "prop_dynamic";
	}*/
	return CEntityFactoryDictionary__CreateOriginal(thisptr, pClassName);
}

void(__fastcall* oCNetChan__ProcessPacket)(CNetChan*, netpacket_s*, bool);
bool(__fastcall* oCNetChan___ProcessMessages)(CNetChan*, bf_read*);

float m_flLastProcessingTime = 0.0f;
float m_flFinalProcessingTime = 0.0f;

bool __fastcall CNetChan___ProcessMessages(CNetChan* thisptr, bf_read* buf) {
	if (!thisptr || !IsInServerThread() || !buf)
		return oCNetChan___ProcessMessages(thisptr, buf);

	if (buf->GetNumBitsRead() < 6 || buf->IsOverflowed()) // idfk tbh just move thisptr to whenever a net message processes.
		return oCNetChan___ProcessMessages(thisptr, buf);

	static auto net_chan_limit_msec_ptr = (ConVarR1*)OriginalCCVar_FindVar2(cvarinterface, "net_chan_limit_msec");
	auto net_chan_limit_msec = net_chan_limit_msec_ptr->m_Value.m_fValue;

	if (net_chan_limit_msec == 0.0f)
		return oCNetChan___ProcessMessages(thisptr, buf);

	float flStartTime = Plat_FloatTime();

	if ((flStartTime - m_flLastProcessingTime) > 1.0f) {
		m_flLastProcessingTime = flStartTime;
		m_flFinalProcessingTime = 0.0f;
	}

	BeginProfiling(flStartTime);

	const auto original = oCNetChan___ProcessMessages(thisptr, buf);

	m_flFinalProcessingTime = EndProfiling(flStartTime);

	bool bIsProcessingTimeReached = (m_flFinalProcessingTime >= 0.001f);

	const auto pMessageHandler = *reinterpret_cast<INetChannelHandler**>(reinterpret_cast<uintptr_t>(thisptr) + 0x3ED0);

	if (pMessageHandler && bIsProcessingTimeReached && m_flFinalProcessingTime >= net_chan_limit_msec) {
#ifdef _DEBUG
		Msg("CNetChan::_ProcessMessages: Max processing time reached for client \"%s\" (%.2fms)\n", thisptr->GetName(), m_flFinalProcessingTime);
#endif
		pMessageHandler->ConnectionCrashed("Max processing time reached.");
		return false;
	}

	return original;
}

void __fastcall CNetChan__ProcessPacket(CNetChan* thisptr, netpacket_s* packet, bool bHasHeader) {
	if (!thisptr || !IsInServerThread())
		return oCNetChan__ProcessPacket(thisptr, packet, bHasHeader);

	bool bReceivingPacket = *(bool*)((uintptr_t)thisptr + 0x3F80) && !*(int*)((uintptr_t)thisptr + 0xD8);

	if (!bReceivingPacket)
		return oCNetChan__ProcessPacket(thisptr, packet, bHasHeader);

	static auto net_chan_limit_msec_ptr = (ConVarR1*)OriginalCCVar_FindVar2(cvarinterface, "net_chan_limit_msec");
	auto net_chan_limit_msec = net_chan_limit_msec_ptr->m_Value.m_fValue;

	if (net_chan_limit_msec == 0.0f)
		return oCNetChan__ProcessPacket(thisptr, packet, bHasHeader);

	float flStartTime = Plat_FloatTime();

	if ((flStartTime - m_flLastProcessingTime) > 1.0f) {
		m_flLastProcessingTime = flStartTime;
		m_flFinalProcessingTime = 0.0f;
	}

	BeginProfiling(flStartTime);

	oCNetChan__ProcessPacket(thisptr, packet, bHasHeader);

	const auto pMessageHandler = *reinterpret_cast<INetChannelHandler**>(reinterpret_cast<uintptr_t>(thisptr) + 0x3ED0);

	m_flFinalProcessingTime = EndProfiling(flStartTime);

	bool bIsProcessingTimeReached = (m_flFinalProcessingTime >= 0.001f);

	if (pMessageHandler && bIsProcessingTimeReached && m_flFinalProcessingTime >= net_chan_limit_msec) {
#ifdef _DEBUG
		Msg("CNetChan::ProcessPacket: Max processing time reached for client \"%s\" (%.2fms)\n", thisptr->GetName(), m_flFinalProcessingTime);
#endif
		pMessageHandler->ConnectionCrashed("Max processing time reached.");
	}
}

bool(__fastcall* oCGameClient__ProcessVoiceData)(void*, CLC_VoiceData*);

bool __fastcall CGameClient__ProcessVoiceData(void* thisptr, CLC_VoiceData* msg) {
	char voiceDataBuffer[4096];

	void* thisptr_shifted = reinterpret_cast<uintptr_t>(thisptr) == 16 ? nullptr : reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(thisptr) - 8);
	int bitsRead = msg->m_DataIn.ReadBitsClamped(voiceDataBuffer, msg->m_nLength);

	if (msg->m_DataIn.IsOverflowed())
		return false;

	auto SV_BroadcastVoiceData = reinterpret_cast<void(__cdecl*)(void*, int, char*, uint64)>(G_engine + 0xEE4D0);

	if (thisptr_shifted)
		SV_BroadcastVoiceData(thisptr_shifted, Bits2Bytes(bitsRead), voiceDataBuffer, *reinterpret_cast<uint64*>(reinterpret_cast<uintptr_t>(msg) + 0x88));

	return true;
}

#define MAX_USER_MSG_DATA 255
#define MAX_ENTITY_MSG_DATA 255

bool(__fastcall* oCClientState__ProcessUserMessage)(void*, SVC_UserMessage*);

bool __fastcall CClientState__ProcessUserMessage(void* thisptr, SVC_UserMessage* msg) {
	ALIGN4 byte userdata[MAX_USER_MSG_DATA] = {0};

	bf_read userMsg("UserMessage(read)", userdata, sizeof(userdata));

	int bitsRead = msg->m_DataIn.ReadBitsClamped(userdata, msg->m_nLength);
	userMsg.StartReading(userdata, Bits2Bytes(bitsRead));

	const auto& res = g_ClientDLL->DispatchUserMessage(msg->m_nMsgType, &userMsg);

	if (!res) {
		Msg("Couldn't dispatch user message (%i)\n", msg->m_nMsgType);

		return res;
	}

	return res;
}

bool(__fastcall* oCNetChan__ProcessControlMessage)(CNetChan*, int, CBitRead*);

bool __fastcall CNetChan__ProcessControlMessage(CNetChan* thisptr, int cmd, CBitRead* buf) {
	if (buf) {
		unsigned char _cmd = buf->ReadUBitLong(6);

		if (_cmd >= net_File)
			return false;
	}

	return oCNetChan__ProcessControlMessage(thisptr, cmd, buf);
}

/**
 * Validates and processes the sign-on state from a network buffer.
 *
 * thisptr function prevents exploitation of duplicate SIGNONSTATE_FULL messages
 * that could be used maliciously. If the file background transmission is already
 * active and we receive another SIGNONSTATE_FULL, the message is rejected.
 *
 * @param thisptr Pointer to the NET_SignOnState object
 * @param buffer Network buffer containing the sign-on state
 * @return bool Returns false if message is potentially malicious, true otherwise
 */
enum SIGNONSTATE : int {
	SIGNONSTATE_NONE = 0, // no state yet; about to connect.
	SIGNONSTATE_CHALLENGE = 1, // client challenging server; all OOB packets.
	SIGNONSTATE_CONNECTED = 2, // client is connected to server; netchans ready.
	SIGNONSTATE_NEW = 3, // just got serverinfo and string tables.
	SIGNONSTATE_PRESPAWN = 4, // received signon buffers.
	SIGNONSTATE_GETTING_DATA = 5, // getting persistence data.
	SIGNONSTATE_SPAWN = 6, // ready to receive entity packets.
	SIGNONSTATE_FIRST_SNAP = 7, // received baseline snapshot.
	SIGNONSTATE_FULL = 8, // we are fully connected; first non-delta packet received.
	SIGNONSTATE_CHANGELEVEL = 9, // server is changing level; please wait.
};

struct alignas(8) NET_SignOnState : INetMessage {
	bool m_bReliable;
	void* m_NetChannel;
	void* m_pMessageHandler;
	SIGNONSTATE m_nSignonState;
	int m_nSpawnCount;
	int m_numServerPlayers;
};

static_assert(offsetof(NET_SignOnState, m_nSignonState) == 32);
bool (*oNET_SignOnState__ReadFromBuffer)(NET_SignOnState* thisptr, bf_read& buffer);

bool NET_SignOnState__ReadFromBuffer(NET_SignOnState* thisptr, bf_read& buffer) {
	// Process the original buffer read
	oNET_SignOnState__ReadFromBuffer(thisptr, buffer);

	// Reject duplicate SIGNONSTATE_FULL messages when file transmission is active
	if (thisptr->GetNetChannel()->m_bConnectionComplete_OrPreSignon && thisptr->m_nSignonState == SIGNONSTATE_FULL) {
		Warning("NET_SignOnState::ReadFromBuffer: blocked attempt at re-ACKing SIGNONSTATE_FULL\n");
		return false;
	}

	return true;
}

bool(__fastcall* oCClientState__ProcessVoiceData)(void*, SVC_VoiceMessage*);

bool __fastcall CClientState__ProcessVoiceData(void* thisptr, SVC_VoiceMessage* msg) {
	//char chReceived[4104];

	//unsigned int dwLength = msg->m_nLength;

	//if (dwLength >= 0x1000)
	//	dwLength = 4096;

	//int bitsRead = msg->m_DataIn.ReadBitsClamped(chReceived, msg->m_nLength);

	//if (msg->m_bFromClient)
	//	return false;

	//int iEntity = (msg->m_bFromClient + 1);
	//int nChannel = Voice_GetChannel(iEntity);

	//if (nChannel != -1)
	//	return Voice_AddIncomingData(nChannel, chReceived, *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(thisptr) + 0x84), true);

	//nChannel = Voice_AssignChannel(iEntity, false);

	//if (nChannel != -1)
	//	return Voice_AddIncomingData(nChannel, chReceived, *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(thisptr) + 0x84), true);

	//return nChannel;
	return oCClientState__ProcessVoiceData(thisptr, msg);
}

void(__cdecl* oCL_CopyExistingEntity)(CEntityReadInfo&);

void CL_CopyExistingEntity(CEntityReadInfo& u) {
	if (u.m_nNewEntity >= 2048) {
#ifdef _DEBUG
		Warning("CL_CopyExistingEntity: m_nNewEntity (%d) out of bounds!! (exceeds >= 2048)\n", u.m_nNewEntity);
#endif
		Host_Error("CL_CopyExistingEntity: u.m_nNewEntity out of bounds.");
		return;
	}

	return oCL_CopyExistingEntity(u);
}

bool __fastcall CBaseClientState__ProcessGetCvarValue(void* thisptr, void* msg) {
	return true;
}

bool __fastcall CBaseClientState__ProcessSplitScreenUser(void* thisptr, SVC_SplitScreen* msg) {
	static auto splitscreen = (ISplitScreen*)G_engine + 0x797060;

	if (msg->m_nSlot < 0 || msg->m_nSlot > 1) {
#ifdef _DEBUG
		Warning("CBaseClientState::ProcessSplitScreenUser: failed to process \"(msg->m_nSlot < 0 || msg->m_nSlot > 1)\".\n");
#endif
		return false;
	}

	if (splitscreen->RemoveSplitScreenPlayer(msg->m_Type))
		splitscreen->AddSplitScreenPlayer(msg->m_Type);

	return true;
}

bool __fastcall CBaseClient__IsSplitScreenUser(void* thisptr) {
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: flows a new packet
// Input  : *pChan   -
//          outSeqNr -
//          acknr    -
//          inSeqNr  -
//          nChoked  -
//          nDropped -
//          nSize    -
//-----------------------------------------------------------------------------
void __fastcall CNetChan__FlowNewPacket(CNetChan* pChan, int flow, int outSeqNr, int inSeqNr, int nChoked, int nDropped, int nSize) {
	double netTime;
	int v8;
	int v9;
	int v12;
	int currentindex;
	int nextIndex;
	int v17;
	int v18;
	unsigned int v19;
	int v20;
	int v21;
	__int64 v22;
	float time;
	__int64 v24;
	__int64 v25;
	__int64 v26;
	__int64 v27;
	__int64 v28;
	__int64 v29;
	int v30;
	int v31;
	float v32;
	__int64 v33;
	__int64 v34;
	__int64 v35;
	int v36;
	float v37;
	__int64 result;
	float v39;
	float v40;
	float v41;
	netframe_header_t* v42;
	float v43;
	float v44;
	float v45;

	netTime = *reinterpret_cast<double*>(G_engine + 0x30EF1C0);
	v8 = flow;
	v9 = inSeqNr;
	netflow_t* pFlow = &pChan->m_DataFlow[flow];
	v12 = outSeqNr;

	netframe_header_t* pFrameHeader = nullptr;
	netframe_t* pFrame = nullptr;

	currentindex = pFlow->currentindex;
	if (outSeqNr > currentindex) {
		nextIndex = currentindex + 1;
		if (currentindex + 1 <= outSeqNr) {
			// thisptr variable makes sure the loops below do not execute more
			// than NET_FRAMES_BACKUP times. thisptr has to be done as the
			// headers and frame arrays in the netflow_t structure is as
			// large as NET_FRAMES_BACKUP. Any execution past it is futile
			// and only wastes CPU time. Sending an outSeqNr that is higher
			// than the current index by something like a million or more will
			// hang the engine for several milliseconds to several seconds.
			int numPacketFrames = 0;

			v17 = outSeqNr - nextIndex;

			if (v17 + 1 >= 4) {
				v18 = nChoked + nDropped;
				v19 = ((unsigned int)(v12 - nextIndex - 3) >> 2) + 1;
				v20 = nextIndex + 2;
				v21 = v17 - 2;
				v22 = v19;
				time = netTime;
				nextIndex += 4 * v19;

				do {
					v24 = (v20 - 2) & NET_FRAMES_MASK;
					v25 = v24;
					pFlow->frame_headers[v25].time = time;
					pFlow->frame_headers[v25].valid = 0;
					pFlow->frame_headers[v25].size = 0;
					pFlow->frame_headers[v25].latency = -1.0;
					pFlow->frames[v24].avg_latency = pChan->m_DataFlow[FLOW_OUTGOING].avglatency;
					pFlow->frame_headers[v25].choked = 0;
					pFlow->frames[v24].dropped = 0;
					if (v21 + 2 < v18) {
						if (v21 + 2 >= nChoked)
							pFlow->frames[v24].dropped = 1;
						else
							pFlow->frame_headers[(v20 - 2) & NET_FRAMES_MASK].choked = 1;
					}
					v26 = (v20 - 1) & NET_FRAMES_MASK;
					v27 = v26;
					pFlow->frame_headers[v27].time = time;
					pFlow->frame_headers[v27].valid = 0;
					pFlow->frame_headers[v27].size = 0;
					pFlow->frame_headers[v27].latency = -1.0;
					pFlow->frames[v26].avg_latency = pChan->m_DataFlow[FLOW_OUTGOING].avglatency;
					pFlow->frame_headers[v27].choked = 0;
					pFlow->frames[v26].dropped = 0;
					if (v21 + 1 < v18) {
						if (v21 + 1 >= nChoked)
							pFlow->frames[v26].dropped = 1;
						else
							pFlow->frame_headers[(v20 - 1) & NET_FRAMES_MASK].choked = 1;
					}
					v28 = v20 & NET_FRAMES_MASK;
					v29 = v28;
					pFlow->frame_headers[v29].time = time;
					pFlow->frame_headers[v29].valid = 0;
					pFlow->frame_headers[v29].size = 0;
					pFlow->frame_headers[v29].latency = -1.0;
					pFlow->frames[v28].avg_latency = pChan->m_DataFlow[FLOW_OUTGOING].avglatency;
					pFlow->frame_headers[v29].choked = 0;
					pFlow->frames[v28].dropped = 0;
					if (v21 < v18) {
						if (v21 >= nChoked)
							pFlow->frames[v28].dropped = 1;
						else
							pFlow->frame_headers[v20 & NET_FRAMES_MASK].choked = 1;
					}
					pFrame = &pFlow->frames[(v20 + 1) & NET_FRAMES_MASK];
					pFrameHeader = &pFlow->frame_headers[(v20 + 1) & NET_FRAMES_MASK];
					pFrameHeader->time = time;
					pFrameHeader->valid = 0;
					pFrameHeader->size = 0;
					pFrameHeader->latency = -1.0;
					pFrame->avg_latency = pChan->m_DataFlow[FLOW_OUTGOING].avglatency;
					pFrameHeader->choked = 0;
					pFrame->dropped = 0;
					if (v21 - 1 < v18) {
						if (v21 - 1 >= nChoked)
							pFrame->dropped = 1;
						else
							pFrameHeader->choked = 1;
					}

					numPacketFrames += 4;
					v21 -= 4;
					v20 += 4;
					--v22;
				} while (v22 && numPacketFrames < NET_FRAMES_BACKUP);
				v12 = outSeqNr;
				v8 = flow;
				v9 = inSeqNr;
			}

			if (numPacketFrames < NET_FRAMES_BACKUP && nextIndex <= v12) {
				v30 = v12 - nextIndex;
				v31 = nextIndex;
				v33 = v12 - nextIndex + 1;
				do {
					pFrame = &pFlow->frames[v31 & NET_FRAMES_MASK];
					pFrameHeader = &pFlow->frame_headers[v31 & NET_FRAMES_MASK];
					v32 = netTime;
					pFrameHeader->time = v32;
					pFrameHeader->valid = 0;
					pFrameHeader->size = 0;
					pFrameHeader->latency = -1.0;
					pFrame->avg_latency = pChan->m_DataFlow[FLOW_OUTGOING].avglatency;
					pFrameHeader->choked = 0;
					pFrame->dropped = 0;
					if (v30 < nChoked + nDropped) {
						if (v30 >= nChoked)
							pFrame->dropped = 1;
						else
							pFrameHeader->choked = 1;
					}
					--v30;
					++v31;
					--v33;
					++numPacketFrames;
				} while (v33 && numPacketFrames < NET_FRAMES_BACKUP);
				v9 = inSeqNr;
			}
		}
		pFrame->dropped = nDropped;
		pFrameHeader->choked = (short)nChoked;
		pFrameHeader->size = nSize;
		pFrameHeader->valid = 1;
		pFrame->avg_latency = pChan->m_DataFlow[FLOW_OUTGOING].avglatency;
	}
	++pFlow->totalpackets;
	pFlow->currentindex = v12;
	v34 = 544i64;

	if (!v8)
		v34 = 3688i64;

	pFlow->current_frame = pFrame;
	v35 = 548i64;
	v36 = *(_DWORD*)(&pChan->m_bProcessingMessages + v34);
	if (v9 > v36 - NET_FRAMES_BACKUP) {
		if (!v8)
			v35 = 3692i64;
		result = (__int64)pChan + 16 * (v9 & NET_FRAMES_MASK);
		v42 = (netframe_header_t*)(result + v35);
		if (v42->valid && v42->latency == -1.0) {
			v43 = 0.0;
			v44 = fmax(0.0f, netTime - v42->time);
			v42->latency = v44;
			if (v44 >= 0.0)
				v43 = v44;
			else
				v42->latency = 0.0;
			v45 = v43 + pFlow->latency;
			++pFlow->totalupdates;
			pFlow->latency = v45;
			pFlow->maxlatency = fmaxf(pFlow->maxlatency, v42->latency);
		}
	}
	else {
		if (!v8)
			v35 = 3692i64;

		v37 = *(float*)(&pChan->m_bProcessingMessages + 16 * (v36 & NET_FRAMES_MASK) + v35);
		result = v35 + 16i64 * (((_BYTE)v36 + 1) & NET_FRAMES_MASK);
		v39 = v37 - *(float*)(&pChan->m_bProcessingMessages + result);
		++pFlow->totalupdates;
		v40 = (float)((float)(v39 / 127.0) * (float)(v36 - v9)) + netTime - v37;
		v41 = fmaxf(pFlow->maxlatency, v40);
		pFlow->latency = v40 + pFlow->latency;
		pFlow->maxlatency = v41;
	}
}

struct alignas(8) NET_StringCmd : INetMessage {
	bool m_bReliable;
	void* m_NetChannel;
	void* m_pMessageHandler;
	char* m_szCommand; // should be const but we need to modify [0]
	char m_szCommandBuffer[1024];
};

static_assert(offsetof(NET_SignOnState, m_nSignonState) == 32);

bool (*oNET_StringCmd__ReadFromBuffer)(NET_StringCmd* thisptr, bf_read& buffer);

bool NET_StringCmd__ReadFromBuffer(NET_StringCmd* thisptr, bf_read& buffer) {
	// Process the original buffer read
	oNET_StringCmd__ReadFromBuffer(thisptr, buffer);

	// Get the network channel and check if file transmission is active
	if (!thisptr->GetNetChannel()->m_bConnectionComplete_OrPreSignon) {
		Warning("NET_StringCmd::ReadFromBuffer: blocked stringcmd from inactive client\n");
		if (thisptr->m_szCommand)
			thisptr->m_szCommand[0] = 0;
		thisptr->m_szCommandBuffer[0] = 0;
		return true;
	}

	return true;
}

void __stdcall LoaderNotificationCallback(
	unsigned long notification_reason,
	const LDR_DLL_NOTIFICATION_DATA* notification_data,
	void* context) {
	if (notification_reason != LDR_DLL_NOTIFICATION_REASON_LOADED)
		return;

	doBinaryPatchForFile(notification_data->Loaded);

	if (strcmp_static(notification_data->Loaded.BaseDllName->Buffer, L"filesystem_stdio.dll") == 0) {
		G_filesystem_stdio = (uintptr_t)notification_data->Loaded.DllBase;
		InitCompressionHooks();
	}

	if (strcmp_static(notification_data->Loaded.BaseDllName->Buffer, L"server.dll") == 0) {
		auto server_base = (uintptr_t)notification_data->Loaded.DllBase;
		G_server = server_base;
		auto vscript_base = G_vscript;

		auto dedi = G_is_dedi;

		auto engine_base_spec = ENGINE_DLL_BASE_(dedi);

		LDR_DLL_LOADED_NOTIFICATION_DATA* ndata = GetModuleNotificationData(L"vstdlib");
		doBinaryPatchForFile(*ndata);
		FreeModuleNotificationData(ndata);
		uintptr_t vTableAddr = server_base + 0x807220;
		RemoveItemsFromVTable(vTableAddr, 35, 2);
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
		MH_CreateHook((LPVOID)(server_base + 0x629740), &sub_629740, reinterpret_cast<LPVOID*>(&sub_629740Original));
		MH_CreateHook((LPVOID)(server_base + 0x3A1EC0), &CBaseEntity__SendProxy_CellOrigin, reinterpret_cast<LPVOID*>(NULL));
		MH_CreateHook((LPVOID)(server_base + 0x3A2020), &CBaseEntity__SendProxy_CellOriginXY, reinterpret_cast<LPVOID*>(NULL));
		MH_CreateHook((LPVOID)(server_base + 0x3A2130), &CBaseEntity__SendProxy_CellOriginZ, reinterpret_cast<LPVOID*>(NULL));
		MH_CreateHook((LPVOID)(server_base + 0x3C8B70), &CBaseEntity__VPhysicsInitNormal, reinterpret_cast<LPVOID*>(&oCBaseEntity__VPhysicsInitNormal));
		MH_CreateHook((LPVOID)(server_base + 0x3B3200), &CBaseEntity__SetMoveType, reinterpret_cast<LPVOID*>(&oCBaseEntity__SetMoveType));
		MH_CreateHook((LPVOID)(server_base + 0x7F7E0), &HookedServerClassRegister, reinterpret_cast<LPVOID*>(&ServerClassRegister_7F7E0));
		MH_CreateHook((LPVOID)(server_base + 0x4E2F30), &CPlayer_GetLevel, reinterpret_cast<LPVOID*>(NULL));
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
		MH_CreateHook((LPVOID)(engine_base + 0xD4E30), &CBaseClient__ProcessSignonState, reinterpret_cast<LPVOID*>(&oCBaseClient__ProcessSignonState));
		MH_CreateHook((LPVOID)(engine_base + 0xD1EC0), &CBaseClient__IsSplitScreenUser, reinterpret_cast<LPVOID*>(NULL));

		RegisterConVar("net_chan_limit_msec", "225", FCVAR_GAMEDLL | FCVAR_CHEAT, "Netchannel processing is limited to so many milliseconds, abort connection if exceeding budget");

		if (!IsDedicatedServer()) {
			MH_CreateHook((LPVOID)(engine_base + 0x21F9C0), &CEngineVGui__Init, reinterpret_cast<LPVOID*>(&CEngineVGui__InitOriginal));
			MH_CreateHook((LPVOID)(engine_base + 0x21EB70), &CEngineVGui__HideGameUI, reinterpret_cast<LPVOID*>(&CEngineVGui__HideGameUIOriginal));
			RegisterConCommand("toggleconsole", ToggleConsoleCommand, "Toggles the console", (1 << 17));
		}

		RegisterConCommand("updatescriptdata", updatescriptdata_cmd, "Dumps the script data in the AI node graph to disk", FCVAR_CHEAT);
		RegisterConCommand("bot_dummy", AddBotDummyConCommand, "Adds a bot.", FCVAR_GAMEDLL | FCVAR_CHEAT);
		RegisterConVar("r1d_ms", "ms.r1delta.net", FCVAR_CLIENTDLL, "Url for r1d masterserver");
		RegisterConCommand("script", script_cmd, "Execute Squirrel code in server context", FCVAR_GAMEDLL | FCVAR_CHEAT);

		if (!IsDedicatedServer()) {
			RegisterConCommand("script_client", script_client_cmd, "Execute Squirrel code in client context", FCVAR_NONE);
			RegisterConCommand("script_ui", script_ui_cmd, "Execute Squirrel code in UI context", FCVAR_NONE);
		}

		//0x0000415198 on dedicated
		// 0x0620818 on client

		if (IsDedicatedServer()) {
			MH_CreateHook((LPVOID)(G_engine_ds + 0x45530), &HookedCBaseClientSetName, reinterpret_cast<LPVOID*>(&CBaseClientSetNameOriginal));
		}
		else {
			MH_CreateHook((LPVOID)(G_engine + 0xD4840), &HookedCBaseClientSetName, reinterpret_cast<LPVOID*>(&CBaseClientSetNameOriginal));
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

			MH_CreateHook((LPVOID)(engine_base_spec + 0x136860), &Status_ConMsg, NULL);
			MH_CreateHook((LPVOID)(engine_base_spec + 0x1BF500), &Status_ConMsg, NULL);
			//MH_CreateHook((LPVOID)(engine_base_spec + 0x4735A0), &sub_1804735A0, NULL);
			MH_CreateHook((LPVOID)(engine_base_spec + 0x8E6D0), &Status_ConMsg, NULL);
			MH_CreateHook((LPVOID)(engine_base_spec + 0x22610), &Status_ConMsg, NULL);
			MH_CreateHook((LPVOID)(engine_base_spec + 0x1170A0), &COM_Init, reinterpret_cast<LPVOID*>(&COM_InitOriginal));
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

		//MH_CreateHook((LPVOID)(engine_base_spec + 0x1C79A0), &sub_1801C79A0, reinterpret_cast<LPVOID*>(&sub_1801C79A0Original));
		//
		//
		//diMH_CreateHook((LPVOID)(engine_base_spec + 0x1D9E70), &MatchRecvPropsToSendProps_R, reinterpret_cast<LPVOID*>(NULL));
		//MH_CreateHook((LPVOID)(engine_base_spec + 0x217C30), &sub_180217C30, NULL);
		// Cast the function pointer to the function at 0x4E80

		// Fix precache start
		// Rebuild CHL2_Player's precache to take our stuff into account
		MH_CreateHook(LPVOID(server_base + 0x41E070), &CHL2_Player_Precache, 0);

		MH_EnableHook(MH_ALL_HOOKS);
		//std::cout << "did hooks" << std::endl;
	}

	if (strcmp_static(notification_data->Loaded.BaseDllName->Buffer, L"engine_ds.dll") == 0) {
		G_engine_ds = (uintptr_t)notification_data->Loaded.DllBase;

		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("engine_ds.dll") + 0x433C0), &ProcessConnectionlessPacketDedi, reinterpret_cast<LPVOID*>(&ProcessConnectionlessPacketOriginal));
		InitAddons();
		InitDedicated();
		InitDedicated();
	}

	if (strcmp_static(notification_data->Loaded.BaseDllName->Buffer, L"engine.dll") == 0) {
		G_engine = (uintptr_t)notification_data->Loaded.DllBase;

		auto engine_base = G_engine;

		g_nServerThread = (int*)engine_base + 0x7BF1F8;

		MH_CreateHook((LPVOID)(engine_base + 0x1E1230), &CNetChan__ProcessControlMessage, reinterpret_cast<LPVOID*>(&oCNetChan__ProcessControlMessage));
		MH_CreateHook((LPVOID)(engine_base + 0x27EA0), &CBaseClientState__ProcessGetCvarValue, NULL);
		MH_CreateHook((LPVOID)(engine_base + 0x536F0), &CL_CopyExistingEntity, reinterpret_cast<LPVOID*>(&oCL_CopyExistingEntity));
		MH_CreateHook((LPVOID)(engine_base + 0x1FDA50), &CLC_Move__ReadFromBuffer, reinterpret_cast<LPVOID*>(&CLC_Move__ReadFromBufferOriginal));
		MH_CreateHook((LPVOID)(engine_base + 0x1F6F10), &CLC_Move__WriteToBuffer, reinterpret_cast<LPVOID*>(&CLC_Move__WriteToBufferOriginal));
		MH_CreateHook((LPVOID)(engine_base + 0x203C20), &NET_SetConVar__ReadFromBuffer, NULL);
		MH_CreateHook((LPVOID)(engine_base + 0x202F80), &NET_SetConVar__WriteToBuffer, NULL);
		MH_CreateHook((LPVOID)(engine_base + 0x1FE3F0), &SVC_ServerInfo__WriteToBuffer, reinterpret_cast<LPVOID*>(&SVC_ServerInfo__WriteToBufferOriginal));
		MH_CreateHook((LPVOID)(engine_base + 0x1E96E0), &CNetChan__ProcessPacket, reinterpret_cast<LPVOID*>(&oCNetChan__ProcessPacket));
		MH_CreateHook((LPVOID)(engine_base + 0x1E51D0), &CNetChan___ProcessMessages, reinterpret_cast<LPVOID*>(&oCNetChan___ProcessMessages));
		MH_CreateHook((LPVOID)(engine_base + 0xDA330), &CGameClient__ProcessVoiceData, reinterpret_cast<LPVOID*>(&oCGameClient__ProcessVoiceData));
		MH_CreateHook((LPVOID)(engine_base + 0x17D400), &CClientState__ProcessUserMessage, reinterpret_cast<LPVOID*>(&oCClientState__ProcessUserMessage));
		MH_CreateHook((LPVOID)(engine_base + 0x17D600), &CClientState__ProcessVoiceData, reinterpret_cast<LPVOID*>(&oCClientState__ProcessVoiceData));
		MH_CreateHook((LPVOID)(engine_base + 0x1E0C80), &CNetChan__FlowNewPacket, NULL);
		MH_CreateHook((LPVOID)(engine_base + 0x237F0), &CBaseClientState__ProcessSplitScreenUser, NULL);

		MH_CreateHook((LPVOID)(engine_base + 0x0D2490), &ProcessConnectionlessPacketClient, reinterpret_cast<LPVOID*>(&ProcessConnectionlessPacketOriginalClient));
		if (!IsDedicatedServer()) {
			MH_CreateHook((LPVOID)(G_engine + 0x1305E0), &ExecuteConfigFile, NULL);
			// RegisterConVar("r1d_ms", "localhost:3000", FCVAR_CLIENTDLL, "Url for r1d masterserver");
			// @hypnotic: whoops do not put that thing here causes stack overflow :steamhappy
			RegisterConCommand(PERSIST_COMMAND, setinfopersist_cmd, "Set persistent variable", FCVAR_SERVER_CAN_EXECUTE);
			InitAddons();
		}

		//// Fix stack smash in CNetChan::ProcessSubChannelData
		CNetChan__ProcessSubChannelData_Asm_continue = (uintptr_t)(engine_base + 0x1E8DDA);
		CNetChan__ProcessSubChannelData_ret0 = (uintptr_t)(engine_base + 0x1E8F26);
		void* allign = (void*)(engine_base + 0x1EA961);

		auto* jmp_pos = (void*)(((uintptr_t)GetModuleHandle(L"engine.dll")) + 0x1E8DD5); // `call nullsub_87` offset
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

	if (strcmp_static(notification_data->Loaded.BaseDllName->Buffer, L"client.dll") == 0) {
		G_client = (uintptr_t)notification_data->Loaded.DllBase;

		InitClient();
	}
}