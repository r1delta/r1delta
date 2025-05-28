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

#include "core.h"
#include "arena.hh"
#include "tctx.hh"

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
#include "security_fixes.hh"
#include "steam.h"
#include "persistentdata.h"
#include "netadr.h"
#include <httplib.h>
#include "audio.h"
#include <nlohmann/json.hpp>
#include "shellapi.h"
#ifdef JWT
#include <jwt-cpp/jwt.h>
#include "jwt_compact.h"
#endif
#include "vector.h"
#include "hudwarp.h"
#include "hudwarp_hooks.h"
#include "surfacerender.h"
#include "localchatwriter.h"
#include "discord.h"
//#define DISCORD
#define DISCORDPP_IMPLEMENTATION
#ifdef DISCORD
#include <discordpp.h>
#endif
#include "sv_filter.h"
#include <discord-game-sdk/discord.h>  
#include "thread.h"
#include <Mmdeviceapi.h>

#include "r1d_version.h"

// Define and initialize the static member for the ConVar
ConVarR1* CBanSystem::m_pSvBanlistAutosave = nullptr;

std::atomic<bool> running = true;

// Signal handler to stop the application

//
//auto client = std::make_shared<discordpp::Client>();



#pragma intrinsic(_ReturnAddress)


extern "C"
{
	uintptr_t CNetChan__ProcessSubChannelData_ret0 = 0;
	uintptr_t CNetChan__ProcessSubChannelData_Asm_continue = 0;
	extern uintptr_t CNetChan__ProcessSubChannelData_AsmConductBufferSizeCheck;
}
void* dll_notification_cookie_;

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

signed __int64 __fastcall sub_1804735A0(char* a1, signed __int64 a2, const char* a3, va_list a4)
{
	static bool recursive2 = false;
	signed __int64 result; // rax

	if (a2 <= 0)
		return 0i64;
	result = vsnprintf(a1, a2, a3, a4);
	if ((int)result < 0i64 || (int)result >= a2)
	{
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
void sub_180473550(char* a1, signed __int64 a2, const char* a3, ...)
{
	int v5; // eax
	va_list ArgList; // [rsp+58h] [rbp+20h] BYREF

	va_start(ArgList, a3);
	if (a2 > 0)
	{
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
	// TODO(mrsteyk): move this out of here...
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
		char inl[16]{ 0 };
		const char* ptr;
	};
	// ?????
	//uint32_t size = 0;
	//uint32_t capacity = 0;
	size_t size = 0;
	size_t big_size = 0;
};
static_assert(sizeof(string_nodebug) == 4*8, "AAA");
std::array<string_nodebug[2], 100> g_militia_bodies;
// #STR: "%sserver_%s%s", "-sharedservervpk"
typedef __int64 (*sub_136E70Type)(char* pPath);
sub_136E70Type sub_136E70Original;
__int64 __fastcall sub_136E70(char* pPath)
{
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
					if (*(_QWORD*)(v5 + 448))
					{
						auto v7 = (_BYTE*)(v5 + 432);
						if (*(_QWORD*)(v5 + 456) >= 0x10ui64)
							v7 = *(_BYTE**)v7;
						PrecacheModel(v7);
					}

					// MCOR
					if (elem[0].size)
					{
						const char* v7 = elem[0].inl;
						if (elem[0].big_size >= 0x10)
							v7 = elem[0].ptr;
						PrecacheModel(v7);
					}

					// armsmodel IMC
					if (*(_QWORD*)(v5 + 488))
					{
						auto v8 = (_BYTE*)(v5 + 472);
						if (*(_QWORD*)(v5 + 496) >= 0x10ui64)
							v8 = *(_BYTE**)v8;
						PrecacheModel(v8);
					}

					// armsmodel MCOR
					if (elem[1].size)
					{
						const char* v7 = elem[1].inl;
						if (elem[1].big_size >= 0x10)
							v7 = elem[1].ptr;
						PrecacheModel(v7);
					}
				}

				// cockpitmodel
				if (*(_QWORD*)(v5 + 528))
				{
					auto v9 = (_BYTE*)(v5 + 512);
					if (*(_QWORD*)(v5 + 536) >= 0x10ui64)
						v9 = *(_BYTE**)v9;
					PrecacheModel(v9);
				}
			}
		}
	}
}

int32_t ReadConnectPacket2015AndWriteConnectPacket2014(bf_read& msg, bf_write& buffer)
{
	ZoneScoped;

	char type = msg.ReadByte();
	if (type != 'A')
	{
		return -1;
	}

	//buffer.WriteLong(-1);
	buffer.WriteByte('A');

	int version = msg.ReadLong();
	if (version != 1040)
	{
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
	for (int i = 0; i < unknownCount; i++)
	{
		buffer.WriteLongLong(msg.ReadLongLong()); // unknown3
	}

	msg.ReadString(tempStr, sizeof(tempStr));
	buffer.WriteString(tempStr); // serverFilter

	msg.ReadLong(); // skip playlistVersionNumber
	msg.ReadLong(); // skip persistenceVersionNumber
	msg.ReadLongLong(); // skip persistenceHash

	int numberOfPlayers = msg.ReadByte();
	buffer.WriteByte(numberOfPlayers);
	for (int i = 0; i < numberOfPlayers; i++)
	{
		// Read SplitPlayerConnect from 2015 packet
		int type = msg.ReadUBitLong(6);
		int count = msg.ReadByte();

		// Write SplitPlayerConnect to 2014 packet
		buffer.WriteUBitLong(type, 6);
		buffer.WriteByte(count);

		for (int j = 0; j < count; j++)
		{
			msg.ReadString(tempStr, sizeof(tempStr));
			buffer.WriteString(tempStr); // key

			msg.ReadString(tempStr, sizeof(tempStr));
			buffer.WriteString(tempStr); // value
		}
	}

	buffer.WriteByte(msg.ReadByte()); // lowViolence 

	buffer.WriteByte(1);

	return !buffer.IsOverflowed() ? lastChallenge : -1;
}

typedef char (*ProcessConnectionlessPacketType)(unsigned int* a1, netpacket_s* a2);
ProcessConnectionlessPacketType ProcessConnectionlessPacketOriginal;
double lastReceived = 0.f;
char __fastcall ProcessConnectionlessPacketDedi(unsigned int* a1, netpacket_s* a2)
{
	ZoneScoped;

	char buffer[1200] = { 0 };
	bf_write writer(reinterpret_cast<char*>(buffer), sizeof(buffer));

	if (((char*)a2->pData + 4)[0] == 'A' && ReadConnectPacket2015AndWriteConnectPacket2014(a2->message, writer) != -1)
	{
		if (lastReceived == a2->received)
			return false;
		lastReceived = a2->received;
		bf_read converted(reinterpret_cast<char*>(buffer), writer.GetNumBytesWritten());
		a2->message = converted;
		return ProcessConnectionlessPacketOriginal(a1, a2);
	}
	else
	{
		return ProcessConnectionlessPacketOriginal(a1, a2);
	}
}

ProcessConnectionlessPacketType ProcessConnectionlessPacketOriginalClient;


char __fastcall ProcessConnectionlessPacketClient(unsigned int* a1, netpacket_s* a2)
{
	static auto sv_limit_quires = CCVar_FindVar(cvarinterface,"sv_limit_queries");
	static auto& sv_limit_queries_var = sv_limit_quires->m_Value.m_nValue;
	if (sv_limit_queries_var == 1 && a2->pData[4] == 'N') {
		sv_limit_queries_var = 0;
	}
	else if(sv_limit_queries_var == 0 && a2->pData[4] != 'N') {
		sv_limit_queries_var = 1;
	}
	return ProcessConnectionlessPacketOriginalClient(a1, a2);
}




typedef void (*CAI_NetworkManager__BuildStubType)(__int64 a1);
typedef void (*CAI_NetworkManager__LoadNavMeshType)(__int64 a1, __int64 a2, const char* a3);
CAI_NetworkManager__LoadNavMeshType CAI_NetworkManager__LoadNavMeshOriginal;
void __fastcall CAI_NetworkManager__LoadNavMesh(__int64 a1, __int64 a2, const char* a3)
{
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
__int64 __fastcall CBaseServer__WriteDeltaEntities(__int64 a1, _DWORD* a2, __int64 a3, __int64 a4, __int64 a5, int a6, unsigned int a7)
{
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
	int a9)
{
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
	__int64 a8)
{
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

char __fastcall sub_4C460(__int64 a1)
{
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
	if (true)
	{
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
void COM_Init()
{
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
char __fastcall SVC_ServerInfo__WriteToBuffer(__int64 a1, __int64 a2)
{
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

ConCommandR1* RegisterConCommand(const char* commandName, void (*callback)(const CCommand&), const char* helpString, int flags) {
	ConCommandConstructorType conCommandConstructor = (ConCommandConstructorType)(IsDedicatedServer() ? (G_engine_ds + 0x31F260) : (G_engine + 0x4808F0));
	ConCommandR1* newCommand = new ConCommandR1;
	
	conCommandConstructor(newCommand, commandName, callback, helpString, flags, nullptr);

	return newCommand;
}

ConVarR1* RegisterConVar(const char* name, const char* value, int flags, const char* helpString) {
	typedef void (*ConVarConstructorType)(ConVarR1* newVar, const char* name, const char* value, int flags, const char* helpString);
	ConVarConstructorType conVarConstructor = (ConVarConstructorType)(IsDedicatedServer() ? (G_engine_ds + 0x320460) : (G_engine + 0x481AF0));
	ConVarR1* newVar = new ConVarR1;


	conVarConstructor(newVar, name, value, flags, helpString);

	return newVar;
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
uintptr_t G_localize;
ILocalize* G_localizeIface;

class CPluginBotManager
{
public:
	virtual void* GetBotController(uint16_t* pEdict);
	virtual __int64 CreateBot(const char* botname);
};
bool isCreatingBot = false;
int botTeamIndex = 0;
__int64 (*oCPortal_Player__ChangeTeam)(__int64 thisptr, unsigned int index);
__int64 __fastcall CPortal_Player__ChangeTeam(__int64 thisptr, unsigned int index)
{
	if (isCreatingBot)
		index = botTeamIndex;
	return oCPortal_Player__ChangeTeam(thisptr, index);
}

static int g_botCounter = 1; // Counter for sequential bot names

void AddBotDummyConCommand(const CCommand& args)
{
	// Expected usage: bot_dummy -team <team index>
	if (args.ArgC() != 3)
	{
		Warning("Usage: bot_dummy -team <team index>\n");
		return;
	}

	// Check for the '-team' flag
	if (strcmp_static(args.Arg(1), "-team") != 0)
	{
		Warning("Usage: bot_dummy -team <team index>\n");
		return;
	}

	// Parse the team index
	int teamIndex = 0;
	try
	{
		teamIndex = std::stoi(args.Arg(2));
	}
	catch (const std::invalid_argument&)
	{
		Warning("Invalid team index: %s\n", args.Arg(2));
		return;
	}
	catch (const std::out_of_range&)
	{
		Warning("Team index out of range: %s\n", args.Arg(2));
		return;
	}

	botTeamIndex = teamIndex;

	// Generate sequential bot name
	char botName[16]; // Buffer for "BotXX" + null terminator
	snprintf(botName, sizeof(botName), "Bot%02d", g_botCounter);

	HMODULE serverModule = GetModuleHandleA("server.dll");
	if (!serverModule)
	{
		Warning("Failed to get handle for server.dll\n");
		return;
	}

	typedef CPluginBotManager* (*CreateInterfaceFn)(const char* name, int* returnCode);
	CreateInterfaceFn CreateInterface = reinterpret_cast<CreateInterfaceFn>(GetProcAddress(serverModule, "CreateInterface"));
	if (!CreateInterface)
	{
		Warning("Failed to get CreateInterface function from server.dll\n");
		return;
	}

	int returnCode = 0;
	CPluginBotManager* pBotManager = CreateInterface("BotManager001", &returnCode);
	if (!pBotManager)
	{
		Warning("Failed to retrieve BotManager001 interface\n");
		return;
	}

	isCreatingBot = true;
	__int64 pBot = pBotManager->CreateBot(botName);
	isCreatingBot = false;

	if (!pBot)
	{
		Warning("Failed to create dummy bot with name: %s\n", botName);
		return;
	}
	else
	{
		// Increment counter only if bot creation was successful
		g_botCounter++;
	}

	typedef void (*ClientFullyConnectedFn)(__int64 thisptr, __int64 entity);

	ClientFullyConnectedFn CServerGameClients_ClientFullyConnected = (ClientFullyConnectedFn)(G_server + 0x1499E0);

	CServerGameClients_ClientFullyConnected(0, pBot);

	Msg("Dummy bot '%s' has been successfully created and assigned to team %d.\n", botName, teamIndex);
}

//0x4E2F30

typedef int (*CPlayer_GetLevel_t)(__int64 thisptr);
int __fastcall CPlayer_GetLevel(__int64 thisptr)
{
	int xp = *(int*)(thisptr + 0x1834);
	typedef int (*GetLevelFromXP_t)(int xp);
	GetLevelFromXP_t GetLevelFromXP = (GetLevelFromXP_t)(G_server + 0x28E740);
	return GetLevelFromXP(xp);
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

std::unordered_map<std::string, std::string, HashStrings> g_LastEntCreateKeyValues;
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
__int64 (*oCBaseEntity__VPhysicsInitNormal)(void* a1, unsigned int a2, unsigned int a3, char a4, __int64 a5);
void (*oCBaseEntity__SetMoveType)(void* a1, __int64 a2, __int64 a3);

__int64 CBaseEntity__VPhysicsInitNormal(void* a1, unsigned int a2, unsigned int a3, char a4, __int64 a5)
{
	if (uintptr_t(_ReturnAddress()) == (G_server + 0xB63FD))
		return 0;
	else
		return oCBaseEntity__VPhysicsInitNormal(a1, a2, a3, a4, a5);
}
void CBaseEntity__SetMoveType(void* a1, __int64 a2, __int64 a3)
{
	if (uintptr_t(_ReturnAddress()) == (G_server + 0xB64EC))
	{
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
	if (strcmp_static(a2, "CDynamicProp") == 0) {
		// Store the dereferenced value and original pointer
		originalPointerValue = *(__int64*)a3;
		originalA3 = a3;
	}
	// Check if it's CControlPanelProp
	else if (strcmp_static(a2, "CControlPanelProp") == 0) {
		// Redirect the pointer
		*(__int64*)a3 = originalPointerValue;
		a3 = originalA3;
	}

	// Call original function
	return ServerClassRegister_7F7E0(a1, a2, a3);
}

// SetrankFunction (__int64 player, int rank)
typedef void (*SetRankFunctionType)(__int64, int);

void __fastcall HookedSetRankFunction(__int64 player, int rank)
{
	if (rank > 100) return;
	*(int*)(player + 0x15A0) = rank;
}

typedef int (*GetRankFunctionType)(__int64);

int __fastcall HookedGetRankFunction(__int64 player)
{
	return *(int*)(player + 0x15A0);
}

typedef void (*CBaseClientSetNameType)(__int64 thisptr, const char* name);
CBaseClientSetNameType CBaseClientSetNameOriginal;

void __fastcall HookedCBaseClientSetName(__int64 thisptr, const char* name)
{
	/*
	* Restrict client names to valid characters.
	* Enforce a maximum length of 32 characters.
	*/

	// Handle null or empty name
	if (!name || !*name) {
		CBaseClientSetNameOriginal(thisptr, "unnamed");
		return;
	}

	size_t nameSize = strlen(name);

	// Check if name is too short
	if (nameSize < 2) {
		CBaseClientSetNameOriginal(thisptr, "unnamed");
		return;
	}

	// Check if the name is too long
	if (nameSize > 32) {
		nameSize = 32; // Truncate to max length
	}

	// Create a sanitized name buffer
	char sanitizedName[256];
	size_t sanitizedIndex = 0;

	// Process each character using the IsValidUserInfo validation logic
	for (size_t i = 0; i < nameSize && sanitizedIndex < 32; i++) {
		char c = name[i];

		// Basic ASCII printable range check
		if (c < 32 || c > 126) {
			continue; // Skip non-printable characters
		}

		// Check for explicitly denied characters
		bool isInvalid = false;
		switch (c) {
		case '"':  // String termination
		case '\\': // Escapes
		case '{':  // Code blocks/JSON
		case '}':
		case '\'': // String delimiters
		case '`':
		case ';':  // Command separators
		case '/':
		case '*':
		case '<':  // XML/HTML
		case '>':
		case '&':  // Shell
		case '|':
		case '$':
		case '!':
		case '?':
		case '+':  // URL encoding
		case '%':
		case '\n': // Any whitespace except regular space
		case '\r':
		case '\t':
		case '\v':
		case '\f':
			isInvalid = true;
			break;
		}

		if (!isInvalid) {
			sanitizedName[sanitizedIndex++] = c;
		}
	}

	// Add null terminator
	sanitizedName[sanitizedIndex] = '\0';

	// If no valid characters were found, use default name
	if (sanitizedIndex == 0) {
		CBaseClientSetNameOriginal(thisptr, "unnamed");
		return;
	}

	// Call original function with sanitized name
	//Msg("Updated client name: %s to: %s\n", name, sanitizedName);
	CBaseClientSetNameOriginal(thisptr, sanitizedName);
}
struct AuthResponse {
	bool success = false;
	char failureReason[256];
	char discordName[32];
	char pomeloName[32];
	int64 discordId = 0;
};


static const std::string ecdsa_pub_key = "-----BEGIN PUBLIC KEY-----\n"
"MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEPc1fjPhVLc/prdKT5ku5xNQ9mM3v\n"
"9FHTsnwhx2tPbmVNB0TAJyKNnWVf993HPtb5+W/JAJtJCFg0txDyBBHONg==\n"
"-----END PUBLIC KEY-----\n";

namespace {
	std::string trim(const std::string& s) {
		size_t start = s.find_first_not_of(" \t\n\r");
		return (start == std::string::npos) ? "" : s.substr(start, s.find_last_not_of(" \t\n\r") - start + 1);
	}

	bool is_valid_ipv4(const std::string& ip) {
		std::istringstream iss(trim(ip));
		std::string part;
		int count = 0;
		while (std::getline(iss, part, '.')) {
			if (++count > 4 || part.empty() || part.size() > 3) return false;
			for (char c : part) if (!std::isdigit(c)) return false;
			int num = std::stoi(part);
			if (num < 0 || num > 255) return false;
		}
		return count == 4;
	}
}


void Shared_OnLocalAuthFailure() {
	Warning("%s", "Invalid auth token!\n");
	Cbuf_AddText(0, "disconnect \"Invalid auth token\";delta_start_discord_auth", 0);
}



typedef void (*SetConvarString_t)(ConVarR1* var, const char* value);

SetConvarString_t SetConvarStringOriginal;

// Helper function to get the server�s public IP.
std::string get_public_ip() {
	static std::string cached_ip = []() -> std::string {
		const char* hosts[] = { "checkip.amazonaws.com", "eth0.me", "api.ipify.org" };
		for (const char* host : hosts) {
			httplib::Client client(host);
			client.set_read_timeout(1, 0);
			if (auto res = client.Get("/")) {
				std::string ip = res->body;
				// Trim whitespace.
				ip.erase(0, ip.find_first_not_of(" \t\n\r"));
				ip.erase(ip.find_last_not_of(" \t\n\r") + 1);
				if (res->status == 200 && !ip.empty())
					return ip;
			}
		}
		return std::string("0.0.0.0");
	}();
	return cached_ip;
}

// Server_AuthCallback verifies the server auth token.
AuthResponse Server_AuthCallback(bool loopback, const char* serverIP, const char* token) {
#ifdef JWT
	AuthResponse response;
	if (token == nullptr) {
		response.success = false;
		strncpy(response.failureReason, "No auth token provided", sizeof(response.failureReason));
		return response;
	}
	if (strlen(token) < 10) {
		response.success = false;
		strncpy(response.failureReason, "Invalid auth token", sizeof(response.failureReason));
		return response;
	}
	try {
		auto decoded = jwt::decode(token);

		// Verify expiration.
		auto exp = decoded.get_payload_claim("e").as_int();
		auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
		if (exp < now) {
			response.success = false;
			strncpy(response.failureReason, "Token is expired", sizeof(response.failureReason));
			return response;
		}

		// Create a verifier that checks the ES256 signature using the public key,
		// and also ensures the token�s "server_ip" claim matches the serverIP.
		auto verifier = jwt::verify()
			.allow_algorithm(jwt::algorithm::es256(ecdsa_pub_key, "", "", ""));

		verifier.verify(decoded); // Will throw if verification fails.

		// Extract minimal claims.
		std::string displayName = decoded.get_payload_claim("dn").as_string();
		std::string pomeloName = decoded.get_payload_claim("p").as_string();
		std::string id = decoded.get_payload_claim("di").as_string();
		// Extra check: the token�s server_ip must match exactly.
		std::string tokenServerIP = decoded.get_payload_claim("s").as_string();
		/*if (tokenServerIP != serverIP && !loopback) {
			response.success = false;
			strncpy(response.failureReason, "Token server IP mismatch", sizeof(response.failureReason));
			return response;
		}*/
		response.success = true;
		strncpy(response.discordName, displayName.c_str(), sizeof(response.discordName) - 1);
		response.discordName[sizeof(response.discordName) - 1] = '\0';
		strncpy(response.pomeloName, pomeloName.c_str(), sizeof(response.pomeloName) - 1);
		response.pomeloName[sizeof(response.pomeloName) - 1] = '\0';
		response.discordId = std::stoll(id);
		return response;
	}
	catch (const std::exception& e) {
		response.success = false;
		strncpy(response.failureReason, e.what(), sizeof(response.failureReason) - 1);
		response.failureReason[sizeof(response.failureReason) - 1] = '\0';
		return response;
	}
#else
	AuthResponse response;
	response.success = false;
	strncpy(response.failureReason, "Discord support not enabled", sizeof(response.failureReason));
	return response;
#endif
}



// --- Hook functions for in�game connection ---

// Original function pointer for client connection.
bool (*oCBaseClientConnect)(
	__int64 a1,
	_BYTE* a2,
	int a3,
	__int64 a4,
	char a5,
	int a6,
	CUtlVector<NetMessageCvar_t>* conVars,
	char* a8,
	int a9);

// Hooked client connection function.
bool __fastcall HookedCBaseClientConnect(
	__int64 a1,
	_BYTE* a2,
	int a3,
	__int64 a4,
	char bFakePlayer,
	int a6,
	CUtlVector<NetMessageCvar_t>* conVars,
	char* a8,
	int a9)
{
	if (bFakePlayer)
		return oCBaseClientConnect(a1, a2, a3, a4, bFakePlayer, a6, conVars, a8, a9);

	static auto bUseOnlineAuth = OriginalCCVar_FindVar(cvarinterface, "delta_online_auth_enable");
	static auto iSyncUsernameWithDiscord = OriginalCCVar_FindVar(cvarinterface, "delta_discord_username_sync");
	static auto iHostPort = OriginalCCVar_FindVar(cvarinterface, "hostport");
	if (bUseOnlineAuth->m_Value.m_nValue != 1)
		return oCBaseClientConnect(a1, a2, a3, a4, bFakePlayer, a6, conVars, a8, a9);

	// Determine if this is a loopback connection.
	bool bIsLoopback = false;
	typedef const char* (__fastcall* GetClientIPFn)(__int64);
	GetClientIPFn GetClientIP = reinterpret_cast<GetClientIPFn>((*((uintptr_t**)a4))[1]);
	const char* clientIP = GetClientIP(a4);
	bIsLoopback = (std::string(clientIP) == std::string("loopback") || std::string(clientIP).starts_with("[::1]")) && !IsDedicatedServer();
	bool allow = false;
	if (conVars) {
		for (int i = 0; i < conVars->Count(); i++) {
			NetMessageCvar_t& var = conVars->Element(i);
			if (strcmp(var.name, "delta_server_auth_token") == 0) {
				allow = true;

				std::string actualServerIP = get_public_ip() + ":" + std::to_string(iHostPort->m_Value.m_nValue); // if we're not using ports comment this out later

				#ifdef JWT
				AuthResponse res = Server_AuthCallback(bIsLoopback, actualServerIP.c_str(), jwt_compact::expandJWT(var.value).c_str());
				#else
				AuthResponse res = Server_AuthCallback(bIsLoopback, actualServerIP.c_str(), var.value);
				#endif
				if (CBanSystem::Filter_IsUserBanned(res.discordId)) {
					V_snprintf(a8, a9, "%s", "Discord UserID not allowed to join this server.");
					return false;
				}

				if (!res.success && !bIsLoopback) {
					V_snprintf(a8, a9, "%s", res.failureReason);
					return false;
				}
				if (IsDedicatedServer()) {
					// a1 is 8 bytes ahead on dedicated servers
					*(int64*)(a1 + 0x284) = res.discordId;
				}
				else
					*(int64*)(a1 + 0x2fc) = res.discordId;

			
				if (res.success && iSyncUsernameWithDiscord->m_Value.m_nValue != 0) {
					const char* newName = (iSyncUsernameWithDiscord->m_Value.m_nValue == 1) ? res.discordName : res.pomeloName;
					// Update the client's "name" convar.
					a2 = (_BYTE*)(newName);
					for (int j = 0; j < conVars->Count(); j++) {
						NetMessageCvar_t& innerVar = conVars->Element(j);
						if (strcmp(innerVar.name, "name") == 0) {
							conVars->Remove(j);
							break;
						}
					}
					NetMessageCvar_t newNameVar;
					strncpy(newNameVar.name, "name", sizeof(newNameVar.name));
					strncpy(newNameVar.value, newName, sizeof(newNameVar.value));
					conVars->AddToTail(newNameVar);
				}
			}
		}
	}
	if (!allow && !bIsLoopback) {
		V_snprintf(a8, a9, "%s", "Invalid ConVars in CBaseClient::Connect.");
		return false;
	}
	return oCBaseClientConnect(a1, a2, a3, a4, bFakePlayer, a6, conVars, a8, a9);
}

// Original function pointer for state client connection.
__int64 (*oCBaseStateClientConnect)(
	__int64 a1,
	const char* public_ip,
	const char* private_ip,
	int num_players,
	char a5,
	int a6,
	_BYTE* a7,
	int a8,
	const char* a9,
	__int64* a10,
	int a11);

// Hooked state client connection function.
__int64 __fastcall HookedCBaseStateClientConnect(
	__int64 a1,
	const char* public_ip,
	const char* private_ip,
	int num_players,
	char a5,
	int a6,
	_BYTE* a7,
	int a8,
	const char* a9,
	__int64* a10,
	int a11)
{
	static auto bUseOnlineAuth = OriginalCCVar_FindVar(cvarinterface, "delta_online_auth_enable");
	if (bUseOnlineAuth->m_Value.m_nValue != 1)
		return oCBaseStateClientConnect(a1, public_ip, private_ip, num_players, a5, a6, a7, a8, a9, a10, a11);

	// Obtain the master server URL from the engine convars.
	auto ms_url = OriginalCCVar_FindVar(cvarinterface, "delta_ms_url")->m_Value.m_pszString;
	httplib::Client cli(ms_url);
	cli.set_connection_timeout(2, 0);
	cli.set_follow_location(true);

	// Use the persistent master server auth token.
	auto persistentToken = OriginalCCVar_FindVar(cvarinterface, "delta_persistent_master_auth_token")->m_Value.m_pszString;
	cli.set_bearer_token_auth(persistentToken);

	// Prepare a JSON payload with the server's public IP.
	nlohmann::json j;
	j["ip"] = public_ip;

	auto result = cli.Post("/server-token", j.dump(), "application/json");
	auto var = OriginalCCVar_FindVar(cvarinterface, "delta_server_auth_token");
	if (result && result->status == 200) {
		try {
			#ifdef JWT
			auto jsonResponse = nlohmann::json::parse(result->body);
			std::string token = jsonResponse["token"].get<std::string>();
			// Update the server auth token convar.
			SetConvarStringOriginal(var, jwt_compact::compactJWT(token).c_str());
			Msg("Server Auth Token Length: %d\n", var->m_Value.m_StringLength);
			auto decoded = jwt::decode(token);
			std::string pomeloName = decoded.get_payload_claim("p").as_string();
			std::string id = decoded.get_payload_claim("di").as_string();
			SetConvarStringOriginal(OriginalCCVar_FindVar(cvarinterface, "name"), pomeloName.c_str());
			SetConvarStringOriginal(OriginalCCVar_FindVar(cvarinterface, "hostname"), (pomeloName+"'s R1Delta Server").c_str());
			#endif // JWT
		}
		catch (const std::exception& e) {
			Warning("Failed to parse token from master server: %s\n", e.what());
		}
	}
	else {
		if (result) {
			Warning("Failed to get token from master server: %s\n", result->body.c_str());
		}
		else {
			Warning("Failed to get token from master server. Error: %d\n", static_cast<int>(result.error()));
		}
	}
	cli.stop();
	return oCBaseStateClientConnect(a1, public_ip, private_ip, num_players, a5, a6, a7, a8, a9, a10, a11);
}

/**
 * Validates and processes the sign-on state from a network buffer.
 *
 * This function prevents exploitation of duplicate SIGNONSTATE_FULL messages
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
struct alignas(8) NET_SignOnState : INetMessage
{
	bool m_bReliable;
	void* m_NetChannel;
	void* m_pMessageHandler;
	SIGNONSTATE m_nSignonState;
	int m_nSpawnCount;
	int m_numServerPlayers;
};
static_assert(offsetof(NET_SignOnState, m_nSignonState) == 32);
bool (*oNET_SignOnState__ReadFromBuffer)(NET_SignOnState* thisptr, bf_read& buffer);
bool NET_SignOnState__ReadFromBuffer(NET_SignOnState* thisptr, bf_read& buffer)
{
	// Process the original buffer read
	oNET_SignOnState__ReadFromBuffer(thisptr, buffer);

	// Reject duplicate SIGNONSTATE_FULL messages when file transmission is active
	if (thisptr->GetNetChannel()->m_bConnectionComplete_OrPreSignon && thisptr->m_nSignonState == SIGNONSTATE_FULL) {
		Warning("NET_SignOnState::ReadFromBuffer: blocked attempt at re-ACKing SIGNONSTATE_FULL\n");
		return false;
	}

	return true;
}
struct alignas(8) NET_StringCmd : INetMessage
{
	bool m_bReliable;
	void* m_NetChannel;
	void* m_pMessageHandler;
	char* m_szCommand; // should be const but we need to modify [0]
	char m_szCommandBuffer[1024];
};
static_assert(offsetof(NET_SignOnState, m_nSignonState) == 32);
bool (*oNET_StringCmd__ReadFromBuffer)(NET_StringCmd* thisptr, bf_read& buffer);
bool NET_StringCmd__ReadFromBuffer(NET_StringCmd* thisptr, bf_read& buffer)
{
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

//1F7F0
__int64 (*oXmaCallback)();
__int64 XmaCallback()
{

	// if ( *(_DWORD *)(qword_2018C88 + 92) )
	ConVarR1* xma_useaudio = OriginalCCVar_FindVar(cvarinterface, "sound_useXMA");
	// sub_114B0();
	//call that sub

	if (xma_useaudio->m_Value.m_nValue == 1) {
		typedef void (*XmaCallback_t)();
		typedef __int64 (*XmaCallback_t2)();
		XmaCallback_t sub_114B0 = (XmaCallback_t)(G_engine + 0x114B0);
		sub_114B0();
		auto val = *(int*)(G_engine + 0x443648);
		if (val)
			return (uintptr_t)xma_useaudio;

		//set dword_7931F8
		*(int*)(G_engine + 0x7931FC) = 0xC000;
		*(const char**)(G_engine + 0x793208) = "sound/xma.acache";
		*(int*)(G_engine + 0x44364C) = 1024;
	
		XmaCallback_t sub_EA00 = (XmaCallback_t)(G_engine + 0xEA00);
		XmaCallback_t sub_ADA0 = (XmaCallback_t)(G_engine + 0xADA0);
		XmaCallback_t sub_B160 = (XmaCallback_t)(G_engine + 0xB160);
		XmaCallback_t2 sub_A880 = (XmaCallback_t2)(G_engine + 0xA880);
		sub_EA00();
		sub_ADA0();
		sub_B160();
		*(int*)(G_engine + 0x443648) = 1;
		return sub_A880();

	}


	return oXmaCallback();
}

typedef char* (*GetUserIDString_t)(void* id);
GetUserIDString_t GetUserIDStringOriginal;


typedef struct USERID_s
{
	int			idtype;
	uintptr_t snowflake;
} USERID_t;

typedef USERID_s*(*GetUserID_t)(__int64 base_client, USERID_s* id);

GetUserID_t GetUserIDOriginal;


const char* GetUserIDStringHook(USERID_s* id) {
	char buffer[256];
	if (id->snowflake == 1) {
		sprintf(buffer, "%s", "UNKNOWN");
		return buffer;
	}
	sprintf(buffer, "%lld", id->snowflake); 
	return buffer;
	
}

void StartDiscordAuth(const CCommand& ccargs) {
#ifndef DISCORD
	Warning("Build was compiled without DISCORD defined.\n");
#else
	if (ccargs.ArgC() != 1) {
		Warning("Usage: delta_start_discord_auth\n");
		return;
	}
	if (IsDedicatedServer()) {
		Warning("This command is not available on dedicated servers.\n");
		return;
	}


		discordpp::AuthorizationArgs args{};
		args.SetClientId(1304910395013595176);
		std::string scopes = discordpp::Client::GetDefaultPresenceScopes() + " identify";
		args.SetScopes(scopes);
		if (client) {
			if (client->IsAuthenticated()) {
				Msg("Already authenticated with Discord.\n");
				client->Connect();
				return;
			}
			auto clientPtr = client.get();
			client->Authorize(args, [clientPtr](auto dis_result, auto code, auto redirectUri) {
				if (!dis_result.Successful()) {
					std::cerr << "Authentication Error: " << dis_result.Error() << std::endl;
					Msg("Doing Stuff");
					return;
				}
				else {
					Msg("Got code: %s\n", code.c_str());
				}

				auto ms_url = CCVar_FindVar(cvarinterface, "delta_ms_url")->m_Value.m_pszString;
				httplib::Client cli(ms_url);
				cli.set_connection_timeout(2);
				cli.set_address_family(AF_INET);
				cli.set_follow_location(true);
				auto result = cli.Get(std::format("/discord-auth?code={}", code));
				nlohmann::json j;
				try {
					j = nlohmann::json::parse(result->body);
				}
				catch (const std::exception& e) {
					return;
				}
				auto errorVar = OriginalCCVar_FindVar(cvarinterface, "delta_persistent_master_auth_token_failed_reason");

				if (j.contains("error")) {
					SetConvarStringOriginal(errorVar, j["error"].get<std::string>().c_str());
					return;
				}
				auto token_j = j["token"].get<std::string>(); 
				auto access_token = j["access_token"].get<std::string>();
				auto v = OriginalCCVar_FindVar(cvarinterface, "delta_persistent_master_auth_token");
				SetConvarStringOriginal(v, token_j.c_str());
				SetConvarStringOriginal(errorVar, "");
				
				// Next Step: Update the token and connect
				clientPtr->UpdateToken(discordpp::AuthorizationTokenType::Bearer, access_token, [](discordpp::ClientResult result) {
				
					if (result.Successful()) {
						Msg("Successfully updated token\n");
						//client->Connect();
					}
					else {
						Warning("Failed to update token: %s\n", result.Error().c_str());
					}
					
					});
				});
				
		}
#endif
	return;
}
const char* GetBuildNo() {
	static char buffer[64] = {};

	if (buffer[0] == '\0') {
		std::tm epoch_tm = {};
		epoch_tm.tm_year = 2023 - 1900;
		epoch_tm.tm_mon = 11;  // December
		epoch_tm.tm_mday = 1;
		epoch_tm.tm_hour = 0;
		epoch_tm.tm_min = 0;
		epoch_tm.tm_sec = 0;
		std::time_t epoch_time = _mkgmtime(&epoch_tm);
		std::time_t current_time = std::time(nullptr);
		double seconds_since = std::difftime(current_time, epoch_time);
		int beat = static_cast<int>(seconds_since / 86400);
#ifdef _DEBUG
		std::snprintf(buffer, sizeof(buffer), "R1DMP_PC_BUILD_%d (DEBUG)", beat);
#else
		std::snprintf(buffer, sizeof(buffer), "R1DMP_PC_BUILD_%d", beat);
#endif
	}

	return buffer;
}
const char* (*oCNetChan__GetAddress)(CNetChan* thisptr);
const char* CNetChan__GetAddress(CNetChan* thisptr) {
	return (netadr_t(std::string(oCNetChan__GetAddress(thisptr)).c_str())).GetAddressString();
}

//2A200

typedef __int64(__fastcall* sub_2A200_t)(__int64 a1, __int64 a2, unsigned int a3);
sub_2A200_t sub_2A200Original;
int64 sub_2A200(__int64 a1, __int64 a2, unsigned int a3) {
	static auto id = OriginalCCVar_FindVar(cvarinterface, "platform_user_id");
	if (id->m_Value.m_nValue == 0) {
		static std::random_device rd;                          
		static std::mt19937       gen(rd());     
		std::uniform_int_distribution<> dist(0, 99999);        
		auto randNum = dist(gen);
		id->m_Value.m_nValue = randNum;
	}
	return sub_2A200Original(a1, a2, a3);
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

	//MH_CreateHook((LPVOID)(engine_base + 0x0D2490), &ProcessConnectionlessPacketClient, reinterpret_cast<LPVOID*>(&ProcessConnectionlessPacketOriginalClient));

	if (!IsDedicatedServer()) {
		MH_CreateHook((LPVOID)(G_engine + 0x1305E0), &ExecuteConfigFile, NULL);
		MH_CreateHook((LPVOID)(engine_base + 0x21F9C0), &CEngineVGui__Init, reinterpret_cast<LPVOID*>(&CEngineVGui__InitOriginal));
		MH_CreateHook((LPVOID)(engine_base + 0x21EB70), &CEngineVGui__HideGameUI, reinterpret_cast<LPVOID*>(&CEngineVGui__HideGameUIOriginal));
		RegisterConCommand("toggleconsole", ToggleConsoleCommand, "Toggles the console", (1 << 17));
		RegisterConCommand("delta_start_discord_auth", StartDiscordAuth, "Starts the discord auth process", 0);
		RegisterConCommand(PERSIST_COMMAND, setinfopersist_cmd, "Set persistent variable", FCVAR_SERVER_CAN_EXECUTE);
		MH_CreateHook((LPVOID)(G_engine + 0x2A200), &sub_2A200, reinterpret_cast<LPVOID*>(&sub_2A200Original));
		//g_pLogAudio = RegisterConVar("fs_log_audio", "0", FCVAR_NONE, "Log audio file reads");
		MH_CreateHook((LPVOID)(engine_base + 0x11DB0), &XmaCallback, reinterpret_cast<LPVOID*>(&oXmaCallback));
		InitSteamHooks();
		InitAddons();

	}

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
__forceinline void DebugConnectMsg(int node1, int node2, const char* pszFormat, ...)
{
	ZoneScoped;

	// Stack-allocated buffer for the complete format string
	char finalFormat[512];  // Adjust size as needed
	snprintf(finalFormat, sizeof(finalFormat), "node 1: %%d node 2: %%d: %s", pszFormat);

	va_list args;
	va_start(args, pszFormat);
	Msg(finalFormat, node1, node2, args);
	va_end(args);
}
bool (*oHandleSquirrelClientCommand)(__int64 player, CCommand* args);
bool HandleSquirrelClientCommand(__int64 player, CCommand* args) {
	static auto HandlePlayerClientCommand = reinterpret_cast<decltype(oHandleSquirrelClientCommand)>(G_server + 0x5014F0);
	static auto g_pVoiceManager = G_server + 0xB3B8B0;
	static auto HandleVoiceClientCommand = reinterpret_cast<bool (*)(__int64 voice, __int64 player, CCommand* args)>(G_server + 0x275C20);

	if (oHandleSquirrelClientCommand(player, args)) return true;
	if (HandlePlayerClientCommand(player, args)) return true;
	if (HandleVoiceClientCommand(g_pVoiceManager, player, args)) return true;
	if (!strcmp_static(args->Arg(0), "resetidletimer")) { reinterpret_cast<void(*)()>((G_server + 0xDB210))(); return true; }

	return false;
}
__int64 __fastcall CServerGameDLL_DLLShutdown(__int64 a1, void*** a2, __int64 a3)
{
	Msg("Shutting down\n");
	TerminateProcess(GetCurrentProcess(), 0); // GAME OVER YEAHHHHHH
	return 0;
}

__int64 (*odynamic_initializer_for__prop_dynamic__)();
__int64 dynamic_initializer_for__prop_dynamic__() {
	// Call original initializer
	__int64 ret = odynamic_initializer_for__prop_dynamic__();

	// Get function pointer from offset
	using ServerFunc = __int64(*)();
	ServerFunc serverFunction = reinterpret_cast<ServerFunc>(G_server + 0x25A6C0);
	__int64 v0 = serverFunction();

	// Cast and call final function
	using FinalFunc = __int64(__fastcall*)(__int64*, void***, const char*);
	FinalFunc finalFunction = **reinterpret_cast<FinalFunc**>(v0);

	return finalFunction(reinterpret_cast<__int64*>(v0),
		reinterpret_cast<void***>(G_server + 0xB2F278),
		"prop_control_panel");
}
// Global variables to track state
static int g_consecutive_packets = 0;
static const int PACKET_THRESHOLD = 1500;
static const int PACKET_SIZE = 16;
static double g_last_retry_time = 0.0;
static const double RETRY_DELAY = 15.0; // 15 second delay
__int64 (*oCNetChan__SendDatagramLISTEN_Part2)(__int64 thisptr, unsigned int length, int SendToResult);

__int64 CNetChan__SendDatagramLISTEN_Part2_Hook(__int64 thisptr, unsigned int length, int SendToResult) {
	// Get original function pointer
	static auto original_fn = oCNetChan__SendDatagramLISTEN_Part2;
	if ((*(uint8_t*)(((uintptr_t)thisptr) + 216) > 0)) // make sure client netchan
		return original_fn(thisptr, length, SendToResult);
	// Check packet size
	if (length == PACKET_SIZE) {
		g_consecutive_packets++;
	}
	else {
		g_consecutive_packets = 0;
	}

	// Check conditions for retry
	bool should_retry = false;

	// Check packet threshold
	if (g_consecutive_packets > PACKET_THRESHOLD) {
		should_retry = true;
		Warning("Circuit breaker tripped due to packets!\n");
	}

	//void** vtable = *(void***)thisptr;  // Get vtable pointer
	//using LastReceivedFn = double(__fastcall*)(__int64);
	//LastReceivedFn LastReceived = (LastReceivedFn)vtable[22];  // Assuming IsTimingOut is at index 7
	//if (LastReceived(thisptr) > 5.f) {
	//	should_retry = true;
	//	Warning("Circuit breaker tripped due to timeout, lastreceived was %f\n", LastReceived(thisptr));
	//}

	// Handle retry if needed
	if (should_retry) {
		double current_time = Plat_FloatTime();
		if (current_time - g_last_retry_time >= RETRY_DELAY) {
			Cbuf_AddText(0, "retry\n", 0);
			g_consecutive_packets = 0;
			g_last_retry_time = current_time;
		}
	}

	// Call original function
	return original_fn(thisptr, length, SendToResult);
}
__int64 (*oStringCompare_AllTalkHookDedi)(__int64 a1, __int64 a2);
__int64 StringCompare_AllTalkHookDedi(__int64 a1, __int64 a2) {
	static const ConVarR1* var = 0;
	if (!var && OriginalCCVar_FindVar2)
		var = OriginalCCVar_FindVar2(cvarinterface, "sv_alltalk");
	if (((((uintptr_t)(_ReturnAddress())) == G_engine_ds + 0x05FCD3) || (((uintptr_t)(_ReturnAddress())) == G_engine + 0x0EE5EC)) && var->m_Value.m_nValue == 1)
		return false;
	return strcmp((const char*)a1, (const char*)a2);
}
#define MOVETYPE_NOCLIP 9
void DisableNoClip(void* pPlayer) {
	auto sub_03B3200 = reinterpret_cast<void(*)(void*, int, int)>(G_server + 0x3B3200);
	auto sub_025D680 = reinterpret_cast<void(*)(void*, int, const char*)>(G_server + 0x25D680);

	sub_03B3200(pPlayer, 2, 0);
	sub_025D680(pPlayer, 0, "noclip OFF\n");
}
void noclip_cmd(const CCommand& args)
{
	auto UTIL_GetCommandClient = reinterpret_cast<void* (*)()>(G_server + 0x01438F0);
	auto EnableNoClip = reinterpret_cast<void (*)(void*)>(G_server + 0xDAC70);
	void* pPlayer = UTIL_GetCommandClient();
	if (!pPlayer)
		return;
	int pPlayer_GetMoveType = *(_BYTE*)(((uintptr_t)pPlayer) + 444);
	if (args.ArgC() >= 2)
	{
		bool bEnable = atoi(args.Arg(1)) ? true : false;
		if (bEnable && pPlayer_GetMoveType != MOVETYPE_NOCLIP)
		{
			EnableNoClip(pPlayer);
		}
		else if (!bEnable && pPlayer_GetMoveType == MOVETYPE_NOCLIP)
		{
			DisableNoClip(pPlayer);
		}
	}
	else
	{
		// Toggle the noclip state if there aren't any arguments.
		if (pPlayer_GetMoveType != MOVETYPE_NOCLIP)
		{
			EnableNoClip(pPlayer);
		}
		else
		{
			DisableNoClip(pPlayer);
		}
	}
}
bool (*osub_1801532A0)(__int64 a1, __int64 a2, __int64 a3);
bool sub_1801532A0(__int64 a1, __int64 a2, __int64 a3)
{
	auto ent = *(_QWORD*)(a1 + 8);
	if (ent && *(char**)(ent + 0xd8) && V_stristr(*(char**)(ent + 0xd8), "titan"))
		return 0;
	return osub_1801532A0(a1, a2, a3);
}
void (*oCProjectile__PhysicsSimulate)(__int64 thisptr);
void CProjectile__PhysicsSimulate(__int64 thisptr)
{
	oCProjectile__PhysicsSimulate(thisptr);
	if (*(uintptr_t*)thisptr == (G_server + 0x8DA9D0)) {
		// Get current velocity
		Vector vecVelocity;
		memcpy(&vecVelocity, reinterpret_cast<Vector * (*)(uintptr_t)>(G_server + 0x91B10)(thisptr), sizeof(Vector));

		float flSpeed = vecVelocity.Length();

		// Stop when velocity is very low
		if (flSpeed < 5.f && vecVelocity.z) {
			reinterpret_cast<Vector* (*)(uintptr_t)>(G_server + 0x3BB300)(thisptr)->Zero();
			//*(int*)(thisptr + 360) |= 32;
			//*(_BYTE*)(thisptr + 444) = 0;
			return;
		}

		// Calculate direction vector
		Vector vecDirection;
		if (flSpeed > 0.1f) {
			vecDirection.x = vecVelocity.x / flSpeed;
			vecDirection.y = vecVelocity.y / flSpeed;
			vecDirection.z = vecVelocity.z / flSpeed;
		}
		else {
			vecDirection.x = 0;
			vecDirection.y = 0;
			vecDirection.z = 1;
		}

		// Base rotation speed
		float baseSpinRate = 8.0f;

		// Make spin rate somewhat proportional to speed
		// Use non-linear scaling so it still spins at lower speeds
		float speedFactor = 500.f * (0.3f + 0.7f * (flSpeed / 1300.0f));
		float spinRate = baseSpinRate * speedFactor;

		// Set rotation primarily along the direction of travel
		// This makes it look like a bullet/arrow spinning as it flies
		Vector angVelocity;
		angVelocity.x = vecDirection.x * spinRate;
		angVelocity.y = vecDirection.y * spinRate;
		angVelocity.z = vecDirection.z * spinRate;

		// Apply the new angular velocity each frame
		reinterpret_cast<void(*)(__int64 thisptr, float, float, float)>(G_server + 0x3BB2A0)(thisptr,
			angVelocity.x, angVelocity.y, angVelocity.z);
	}
}
CRITICAL_SECTION g_cs;
typedef __int64(__fastcall* t_sub_1032C0)(__int64 a1, char a2);
typedef __int64(__fastcall* t_sub_103120)(__int64 a1, __int64 a2, __int64 a3, int a4);
t_sub_1032C0 original_sub_1032C0 = nullptr;
t_sub_103120 original_sub_103120 = nullptr;
void (*oCGrenadeFrag__ResolveFlyCollisionCustom)(__int64 a1, float* a2, float* a3);
__int64 __fastcall UTIL_GetEntityByIndex(int iIndex)
{
	__int64 result; // rax
	char* pEdicts; // rdx
	char* pEnt; // rcx
	__int64 v1; // rcx
	__int64 (*v2)(void); // rdx

	if (iIndex <= 0)
		return 0LL;

	pEdicts = (char*)pGlobalVarsServer->pEdicts;
	if (!pEdicts)
		return 0LL;

	pEnt = &pEdicts[56 * iIndex];
	result = *(_DWORD*)pEnt >> 1;

	if ((*(_DWORD*)pEnt & 2) == 0)
	{
		// Inline of IServerUnknown::GetBaseEntity
		v1 = *(_QWORD*)(pEnt + 48);
		if (!v1)
			return 0LL;

		return v1;
	}

	return 0LL;
}
void CGrenadeFrag__ResolveFlyCollisionCustom(__int64 a1, float* a2, float* a3) // void* thisptr, CGameTrace* trace, Vector* move
{
	float x = *(float*)(a1 + 636);
	float y = *((float*)(a1 + 636) + 1);
	float z = *(float*)(a1 + 644);

	//Msg("CGrenadeFrag::ResolveFlyCollisionCustom - a1: %p, vel: [%.2f, %.2f, %.2f], normal: [%.2f, %.2f, %.2f]\n",
	//	a1, x, y, z, a2[8], a2[9], a2[10]);

	char trace[104];
	Vector vel;
	memcpy(trace, a2, sizeof(trace));
	memcpy(&vel, a3, sizeof(vel));
	static auto sub_4AA8E0 = reinterpret_cast<void(*)(__int64, __int64)>(G_server + 0x4AA8E0);
	// Calculate speed squared
	float speedSqr = x * x + y * y + z * z;

	// If speed is below threshold (30*30), zero out velocity
	if (speedSqr < 900.0f)  // 30*30 = 900
	{
		Vector zeroVel = Vector(0, 0, 0);
		static auto CBaseEntity__SetAbsVelocity = reinterpret_cast<void(*)(__int64, Vector*)>(G_server + 0x3BA970);
		CBaseEntity__SetAbsVelocity(a1, &zeroVel);
		oCBaseEntity__SetMoveType((void*)a1, 0, 0);
		return;
	}

	oCGrenadeFrag__ResolveFlyCollisionCustom(a1, (float*)&trace, (float*)&vel); // original stomps the params or some shit

	// Stop if on ground.
	if (a2[10] > 0.9)  // Floor
	{
		static auto sub_1803B9B30 = reinterpret_cast<void(*)(__int64)>(G_server + 0x3b9b30);
		if ((*(_DWORD*)(a1 + 352) & 0x1000LL) != 0)
			sub_1803B9B30(a1); // Update abs velocity
	
		// Get current velocity after bounce
		Vector currVel = *(Vector*)(a1 + 636);
		//if (currVel.z < 30.f)
		//{
		//	Msg("Post-bounce vel: %f, %f, %f\n", currVel.x, currVel.x, currVel.z);
		//	// Your existing code to handle z velocity
		//	currVel = Vector(0, 0, 0);
		//	static auto CBaseEntity__SetAbsVelocity = reinterpret_cast<void(*)(__int64, Vector*)>(G_server + 0x3BA970);
		//	CBaseEntity__SetAbsVelocity(a1, &currVel);
		//}
		// Calculate speed squared
		float speedSqr = currVel.x * currVel.x + currVel.y * currVel.y + currVel.z * currVel.z;
	
		// If speed is below threshold (30*30), zero out velocity
		if (speedSqr < 900.0f)  // 30*30 = 900
		{
			currVel = Vector(0, 0, 0);
			static auto CBaseEntity__SetAbsVelocity = reinterpret_cast<void(*)(__int64, Vector*)>(G_server + 0x3BA970);
			CBaseEntity__SetAbsVelocity(a1, &currVel);
		}
	
	
		// Set ground entity
		auto ent = *(_QWORD*)((__int64)(a2)+96);
		if ((*(unsigned __int8(__fastcall**)(__int64, __int64))(*(_QWORD*)a1 + 824LL))(a1, ent) && a2[10] == 1.0f)
			sub_4AA8E0(a1, ent);

		//if ((*(unsigned __int8(__fastcall**)(__int64, __int64))(*(_QWORD*)a1 + 824LL))(a1, ent))
		//	sub_4AA8E0(a1, ent);
	}

}

__int64 (*o_sub_1032C0)(__int64, char);
__int64 __fastcall sub_1032C0_hook(__int64 a1, char a2)
{
	EnterCriticalSection(&g_cs);
	__int64 ret = o_sub_1032C0(a1, a2);
	LeaveCriticalSection(&g_cs);
	return ret;
}

__int64 (*o_sub_103120)(__int64, __int64, __int64, int);
__int64 __fastcall sub_103120_hook(__int64 a1, __int64 a2, __int64 a3, int a4)
{
	EnterCriticalSection(&g_cs);

	// Get base address of vphysics.dll
	static uintptr_t base = (uintptr_t)GetModuleHandleA("vphysics.dll");

	// Calculate addresses of key memory locations
	uintptr_t physics_pp_mindists_addr = base + 0x1EF1D0; // Adjust this offset to match your binary
	int* physics_pp_mindists = (int*)(physics_pp_mindists_addr + 0x5C); // +0x5C to get the flag
	uintptr_t qword_1801EF258_addr = base + 0x1EF258;
	void** qword_1801EF258 = (void**)qword_1801EF258_addr;

	// Check if we need to manually set the pointer (non-parallel path)
	int needsManualSet = (*physics_pp_mindists == 0);

	// Save original value and set our value if needed
	void* original_value = NULL;
	if (needsManualSet) {
		original_value = *qword_1801EF258;
		*qword_1801EF258 = (void*)(a1 + 0x100078); // a1 + 1048696
	}

	// Call original function
	__int64 ret = o_sub_103120(a1, a2, a3, a4);

	// Restore original value if we changed it
	if (needsManualSet) {
		//static auto sub_1801032C0 = (__int64(*)(__int64, char))(base + 0x1032C0);;
		//sub_1801032C0(a1, 0xFF);
		*qword_1801EF258 = original_value;
	}

	LeaveCriticalSection(&g_cs);
	return ret;
}
//		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("vphysics.dll") + 0x100880), &sub_100880_hook, reinterpret_cast<LPVOID*>(&o_sub_100880));

inline bool IsMemoryReadable(void* ptr, size_t size, DWORD protect_required_flags_oneof)
{
	static SYSTEM_INFO sysInfo;
	if (!sysInfo.dwPageSize)
		GetSystemInfo(&sysInfo);

	MEMORY_BASIC_INFORMATION memInfo;

	if (!VirtualQuery(ptr, &memInfo, sizeof(memInfo)))
		return false;

	if (memInfo.RegionSize < size)
		return false;

	return (memInfo.State & MEM_COMMIT) && !(memInfo.Protect & PAGE_NOACCESS) && (memInfo.Protect & protect_required_flags_oneof) != 0;
}

typedef void(__fastcall* sub_180100880_type)(uintptr_t);
sub_180100880_type o_sub_100880 = nullptr;
void __fastcall sub_100880_hook(uintptr_t a1) // we fix seldom crash in vphysics on level shutdown (but most frequent crash to be reported)
{
	auto cl = reinterpret_cast<void*>(G_engine + 0x5F4B0);
	unsigned int& baseLocalClient__m_nSignonState = *((_DWORD*)cl + 29);
	bool cl_isActive = baseLocalClient__m_nSignonState == 8;
	uintptr_t vPhysicsBase = (uintptr_t)GetModuleHandleA("vphysics.dll");
	static auto* sub_1800FFB50 = reinterpret_cast<__int64(*)(uintptr_t)>(vPhysicsBase + 0xFFB50);
	static auto* sub_1800FF010 = reinterpret_cast<__int64(*)(uintptr_t)>(vPhysicsBase + 0xFF010);
	static auto* sub_1800CA0B0 = reinterpret_cast<__int64(*)(uintptr_t)>(vPhysicsBase + 0xCA0B0);
	bool do_check = !cl_isActive; 
	void(__fastcall * **v2)(_QWORD, __int64); 
	__int64 v3;
	sub_1800FFB50(a1);
	sub_1800FF010(a1);
	int i = 0;
	while (*(__int16*)(a1 + 1310866))
	{
		i++;
		v2 = **(void(__fastcall*****)(_QWORD, __int64))(a1 + 1310872);
		if (v2)
		{
			if (*v2 && **v2) 
			{
				if (do_check)
				{
					if (!IsMemoryReadable(**v2, 8, PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))
					{
						break;
					}
				}
				(**v2)((_QWORD)v2, 1i64);
			}
		}
	}
	v3 = *(_QWORD*)(a1 + 1310872);
	if (v3 != a1 + 1310880)
	{
		if (v3)
			sub_1800CA0B0(v3);
		*(_QWORD*)(a1 + 1310872) = 0i64;
		*(__int16*)(a1 + 1310864) = 0;
	}
	*(__int16*)(a1 + 1310866) = 0;
	DeleteCriticalSection((LPCRITICAL_SECTION)(a1 + 8));
}



void (*oUTIL_LogPrintf)(const char* fmt, ...);
void UTIL_LogPrintf(char* fmt, ...)
{
	char tempString[1024]; // [esp+0h] [ebp-400h] BYREF
	va_list params; // [esp+40Ch] [ebp+Ch] BYREF

	va_start(params, fmt);
	V_vsnprintf(tempString, 1024, fmt, params);
	oUTIL_LogPrintf("%s", tempString);
	if (IsDedicatedServer())
		Msg("%s", tempString);
	static auto UTIL_ClientPrintAll = (void(*)(unsigned int, char*))(G_server + 0x25D5B0);
	UTIL_ClientPrintAll(2, tempString);
}
void (*oCServerGameDLL_OnSayTextMsg)(void* pThis, int clientIndex, char* text, char isTeamChat);
// Simplified IServerUnknown and CBaseEntity/CBasePlayer interfaces
class CBasePlayer {
public:
	uint8_t GetLifeState() const {
		// Offset 865 from the start of the CBaseEntity/CBasePlayer object
		return *(uint8_t*)((uintptr_t)this + 865);
	}

	// Check if alive (assuming LIFE_ALIVE == 0)
	bool IsAlive() const {
		return GetLifeState() == 0; // LIFE_ALIVE is typically 0
	}
};

void __fastcall CServerGameDLL_OnSayTextMsg(void* pThis, int clientIndex, char* text, char isTeamChat) {
	auto r1sqvm = GetServerVMPtr();
	auto v = r1sqvm->sqvm;

	base_getroottable(v);
	sq_pushstring(v, "CodeCallback_OnClientChatMsg", -1);
	if (SQ_SUCCEEDED(sq_get_noerr(v, -2)) && sq_gettype(v, -1) == OT_CLOSURE) {
		base_getroottable(v);
		sq_pushinteger(r1sqvm, v, clientIndex);
		sq_pushstring(v, text, -1);
		sq_pushbool(r1sqvm, v, isTeamChat);
		if (SQ_SUCCEEDED(sq_call(v, 4, SQTrue, SQTrue))) {
			if (sq_gettype(v, -1) == OT_STRING) sq_getstring(v, -1, (const SQChar**)&text);
			sq_pop(v, 1);
		}
		sq_pop(v, 1);
	}
	sq_pop(v, 1);

	if (!text || !text[0]) return;

	oCServerGameDLL_OnSayTextMsg(pThis, clientIndex, text, isTeamChat);

	if (IsDedicatedServer()) {
		// 1. Get Player Entity
		CBasePlayer* pSenderEntity = (CBasePlayer*)UTIL_GetEntityByIndex(clientIndex);

		if (!pSenderEntity) {
			return;
		}

		// 2. Get Player Name (using vtable index 43 / offset 344)
		// We need the vtable pointer first (usually the first member of the object)
		uintptr_t* vtable = *(uintptr_t**)pSenderEntity;
		// Get the function pointer from the vtable
		auto getPlayerNameFunc = (const char* (*)(const CBasePlayer*)) vtable[43]; // 344 / 8 = 43

		const char* playerName = nullptr;
		if (getPlayerNameFunc) {
			playerName = getPlayerNameFunc(pSenderEntity);
		}

		if (!playerName) {
			playerName = "<Unknown>"; // Fallback name
		}

		// 3. Check if Player is Dead (using offset 865 / m_lifeState)
		bool isSenderDead = !pSenderEntity->IsAlive(); // IsAlive checks m_lifeState at 865

		// 4. Format the Output String
		char formattedMsg[1024]; // Adjust size as needed
		const char* deadPrefix = isSenderDead ? "[DEAD]" : "";
		const char* teamPrefix = isTeamChat ? "[TEAM]" : "";

		snprintf(formattedMsg, sizeof(formattedMsg), "*** CHAT *** %s%s%s: %s",
			deadPrefix,
			teamPrefix,
			playerName,
			text ? text : "<Empty Message>"); // Handle null text just in case

		// 5. Print the Formatted Message
		Msg("%s\n", formattedMsg);
	}
}

ConVarR1* host_mostRecentMapCvar = nullptr;
ConVarR1* host_mostRecentGamemodeCvar = nullptr;
void GamemodeChangeCallback(IConVar* var_iconvar, const char* pOldValue, float flOldValue)
{
	auto Cbuf_AddText2 = (Cbuf_AddTextType)(IsDedicatedServer() ? (G_engine_ds + 0x72d70) : (G_engine + 0x102D50));

	ConVarR1* gamemodeCvar = OriginalCCVar_FindVar(cvarinterface, "mp_gamemode");
	const char* newValue = pOldValue;
	char command[256];
	snprintf(command, sizeof(command), "host_mostRecentGamemode \"%s\"\n", newValue ? newValue : "");
	Cbuf_AddText2(0, command, 0);
}

void HostMapChangeCallback(IConVar* var_iconvar, const char* pOldValue, float flOldValue)
{
	auto Cbuf_AddText2 = (Cbuf_AddTextType)(IsDedicatedServer() ? (G_engine_ds + 0x72d70) : (G_engine + 0x102D50));

	const char* newValue = pOldValue;

	// Check if the value is valid and not the lobby map
	if (newValue && newValue[0] != '\0' && strcmp_static(newValue, "mp_lobby.bsp") != 0)
	{
		char mapNameToStore[256]; // Buffer to hold the potentially trimmed map name
		char command[256 + 30];   // Command buffer (make slightly larger for prefix/quotes/suffix)

		strncpy(mapNameToStore, newValue, sizeof(mapNameToStore) - 1);
		mapNameToStore[sizeof(mapNameToStore) - 1] = '\0';

		// --- Trim ".bsp" suffix if present ---
		size_t len = strlen(mapNameToStore);
		const char* suffix = ".bsp";
		size_t suffixLen = strlen(suffix); // length is 4

		// Check if string is long enough and ends with the suffix
		if (len >= suffixLen && strcmp(mapNameToStore + len - suffixLen, suffix) == 0)
		{
			// Null-terminate the string before the suffix starts
			mapNameToStore[len - suffixLen] = '\0';
		}
		// --- End trimming logic ---

		// Construct the command using the (potentially) trimmed map name
		snprintf(command, sizeof(command), "host_mostRecentMap \"%s\"\n", mapNameToStore);
		Cbuf_AddText2(0, command, 0);
	}
}
void InitializeRecentHostVars()
{
	host_mostRecentGamemodeCvar = RegisterConVar(
		"host_mostRecentGamemode",
		"",
		FCVAR_HIDDEN,
		"Stores the last gamemode set via mp_gamemode."
	);

	host_mostRecentMapCvar = RegisterConVar(
		"host_mostRecentMap",
		"",
		FCVAR_HIDDEN,
		"Stores the last map set via host_map, excluding mp_lobby."
	);

	ConVarR1* mp_gamemode = OriginalCCVar_FindVar(cvarinterface, "mp_gamemode");
	ConVarR1* host_map = OriginalCCVar_FindVar(cvarinterface, "host_map");
	{
		mp_gamemode->m_fnChangeCallbacks.AddToTail((FnChangeCallback_t)GamemodeChangeCallback);
		GamemodeChangeCallback(nullptr, "", 0.0f);
	}
	{
		host_map->m_fnChangeCallbacks.AddToTail((FnChangeCallback_t)HostMapChangeCallback);
		HostMapChangeCallback(nullptr, "", 0.0f);
	}

	auto m_sensitivity = OriginalCCVar_FindVar(cvarinterface, "m_sensitivity");
	if (m_sensitivity) {
		m_sensitivity->m_fMinVal = 0.01f;
	}

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
	MH_CreateHook((LPVOID)(server_base + 0x629740), &sub_629740, reinterpret_cast<LPVOID*>(&sub_629740Original));
	MH_CreateHook((LPVOID)(server_base + 0x3A1EC0), &CBaseEntity__SendProxy_CellOrigin, reinterpret_cast<LPVOID*>(NULL));
	MH_CreateHook((LPVOID)(server_base + 0x3A2020), &CBaseEntity__SendProxy_CellOriginXY, reinterpret_cast<LPVOID*>(NULL));
	MH_CreateHook((LPVOID)(server_base + 0x3A2130), &CBaseEntity__SendProxy_CellOriginZ, reinterpret_cast<LPVOID*>(NULL));
	MH_CreateHook((LPVOID)(server_base + 0x3C8B70), &CBaseEntity__VPhysicsInitNormal, reinterpret_cast<LPVOID*>(&oCBaseEntity__VPhysicsInitNormal));
	MH_CreateHook((LPVOID)(server_base + 0x3B3200), &CBaseEntity__SetMoveType, reinterpret_cast<LPVOID*>(&oCBaseEntity__SetMoveType));
	MH_CreateHook((LPVOID)(server_base + 0x4E2F30), &CPlayer_GetLevel, reinterpret_cast<LPVOID*>(NULL));
	MH_CreateHook((LPVOID)(server_base + 0x1442D0), &CServerGameDLL_DLLShutdown, reinterpret_cast<LPVOID*>(NULL));
	MH_CreateHook((LPVOID)(server_base + 0x1532A0), &sub_1801532A0, reinterpret_cast<LPVOID*>(&osub_1801532A0));
	MH_CreateHook((LPVOID)(server_base + 0x21B6B0), &HookedGetRankFunction, NULL);
	MH_CreateHook((LPVOID)(server_base + 0x50B8B0), &HookedSetRankFunction, NULL);
	MH_CreateHook((LPVOID)(server_base + 0x410F60), &CGrenadeFrag__ResolveFlyCollisionCustom, reinterpret_cast<LPVOID*>(&oCGrenadeFrag__ResolveFlyCollisionCustom));
	MH_CreateHook((LPVOID)(server_base + 0x25E290), &UTIL_LogPrintf, reinterpret_cast<LPVOID*>(&oUTIL_LogPrintf));
	//if (IsDedicatedServer())
		MH_CreateHook((LPVOID)(server_base + 0x148730), &CServerGameDLL_OnSayTextMsg, reinterpret_cast<LPVOID*>(&oCServerGameDLL_OnSayTextMsg));
	
	if (IsDedicatedServer()) {
		MH_CreateHook((LPVOID)(G_engine_ds + 0x45EB0), &GetUserIDStringHook, reinterpret_cast<LPVOID*>(&GetUserIDStringOriginal));
	}
	else {
		MH_CreateHook((LPVOID)(engine_base_spec + 0xD5260), &GetUserIDStringHook, reinterpret_cast<LPVOID*>(&GetUserIDStringOriginal));

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
	RegisterConVar("delta_ms_url", "ms.r1delta.net", FCVAR_CLIENTDLL, "Url for r1d masterserver");
	RegisterConVar("delta_server_auth_token", "", FCVAR_USERINFO | FCVAR_SERVER_CANNOT_QUERY | FCVAR_DONTRECORD | FCVAR_PROTECTED | FCVAR_HIDDEN, "Per-server auth token");
	RegisterConVar("delta_persistent_master_auth_token", "DEFAULT", FCVAR_ARCHIVE | FCVAR_SERVER_CANNOT_QUERY | FCVAR_DONTRECORD | FCVAR_PROTECTED | FCVAR_HIDDEN, "Persistent master server authentication token");
	RegisterConVar("delta_persistent_master_auth_token_failed_reason", "", FCVAR_ARCHIVE | FCVAR_SERVER_CANNOT_QUERY | FCVAR_DONTRECORD | FCVAR_PROTECTED | FCVAR_HIDDEN, "Persistent master server authentication token");
	RegisterConVar("delta_online_auth_enable", "0", FCVAR_GAMEDLL, "Whether to use master server auth");
	RegisterConVar("delta_discord_username_sync", "1", FCVAR_GAMEDLL, "Controls if player names are synced with Discord: 0=Off,1=Norm,2=Pomelo");
	RegisterConVar("riff_floorislava", "0", FCVAR_HIDDEN, "Enable floor is lava mode");
	RegisterConVar("hudwarp_use_gpu", "1", FCVAR_ARCHIVE,"Use GPU for HUD warp");
	RegisterConVar("hudwarp_disable", "0", FCVAR_ARCHIVE, "GPU device to use for HUD warp");
	RegisterConVar("hide_server", "0", FCVAR_NONE, "Whether the server should be hidden from the master server");
	RegisterConVar("server_description", "", FCVAR_NONE, "Server description");
	RegisterConVar("delta_ui_server_filter", "0", FCVAR_NONE, "Script managed vgui filter convar");
	RegisterConVar("delta_autoBalanceTeams", "1", FCVAR_NONE, "Whether to autobalance teams on death/private match/lobby start. Managed by script");
	RegisterConVar("delta_useLegacyProgressBar", "0", FCVAR_ARCHIVE, "Whether or not to use the old loading bar");
	CBanSystem::m_pSvBanlistAutosave = RegisterConVar("sv_banlist_autosave", "1", FCVAR_ARCHIVE, "Automatically save ban lists after modification commands.");
	RegisterConCommand("script", script_cmd, "Execute Squirrel code in server context", FCVAR_GAMEDLL | FCVAR_CHEAT);
	if (!IsDedicatedServer()) {
		RegisterConCommand("script_client", script_client_cmd, "Execute Squirrel code in client context", FCVAR_NONE);
		RegisterConCommand("script_ui", script_ui_cmd, "Execute Squirrel code in UI context", FCVAR_NONE);
		RegisterConCommand("noclip", noclip_cmd, "Toggles NOCLIP mode.", FCVAR_GAMEDLL | FCVAR_CHEAT);
	}


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

//
//void DiscordThread() {
//	
//#ifdef DISCORD
//	client = std::make_unique<discordpp::Client>();
//	client->AddLogCallback([](auto message, auto severity) {
//		Msg("[Discord::%s] %s\n", EnumToString(severity), message.c_str());
//	//	std::cout << "[" << EnumToString(severity) << "] " << message << std::endl;
//		}, discordpp::LoggingSeverity::Info);
//
//	client->SetStatusChangedCallback([](auto status, auto error, auto details) {
//		Msg("Status has changed to %s\n", discordpp::Client::StatusToString(status).c_str());
//		if (status == discordpp::Client::Status::Ready) {
//			Msg("Client is ready, you can now call SDK functions. For example:\n");
//			discordpp::Activity activity;
//			activity.SetType(discordpp::ActivityTypes::Playing);
//			activity.SetDetails("Battle Creek");
//			activity.SetState("In Competitive Match");
//			client->UpdateRichPresence(activity, [](discordpp::ClientResult result) {
//				if (result.Successful()) {
//					std::cout << "Rich presence updated!\n";
//				}
//				else {
//					std::cout << "Failed to update rich presence: " << result.Error() << "\n";
//				}
//				});
//
//		}
//		else if (error != discordpp::Client::Error::None) {
//			Msg("Error connecting: %s %d\n", discordpp::Client::ErrorToString(error).c_str(),
//				details);
//		}
//		});
//
//
//
//		while (running) {
//			discordpp::RunCallbacks();
//			std::this_thread::sleep_for(std::chrono::milliseconds(16));
//
//		}
//#endif
//}

__int64 (*oAddSearchPathDedi)(__int64 a1, const char* a2, __int64 a3, unsigned int a4);
__int64 __fastcall AddSearchPathDedi(__int64 a1, const char* a2, __int64 a3, unsigned int a4) {
	if (!strcmp_static(a2, "r1delta")) {
		auto a = _strdup((std::filesystem::path(GetExecutableDirectory()) / "r1delta").string().c_str());
		a2 = a;
		auto ret = oAddSearchPathDedi(a1, a2, a3, a4);
		return ret;
	}
	return oAddSearchPathDedi(a1, a2, a3, a4);
}
void (*oCServerInfoPanel__OnServerDataResponse_14730)(__int64 a1, const char* a2, const char* a3);
void CServerInfoPanel__OnServerDataResponse_14730(__int64 a1, const char* a2, const char* a3) {
	if (strcmp_static(a2, "maplist") == 0) {
		static bool bDone = false;
		if (!bDone) {
			bDone = true;
			reinterpret_cast<void(*)(__int64, const char*, const char*)>((uintptr_t(GetModuleHandleA("AdminServer.dll")) + 0xB310))((a1 - 704), "map", a3);
		}
	}
	//if (strcmp_static(a2, "map") == 0) {

	if (strcmp_static(a2, "map") == 0 && strlen(a3) > 3) {
		static bool bDone = false;
		if (!bDone) {
			bDone = true;
			// Get the engine interface
			void* ret = reinterpret_cast<void*>((reinterpret_cast<CreateInterfaceFn>(GetProcAddress(GetModuleHandleA("engine_ds.dll"), "CreateInterface"))("VENGINE_HLDS_API_VERSION002", 0)));

			// Prepare a string to hold all playlist names
			std::string combined_playlists;
			int v7 = 0;
			// Get the total number of playlists
			int playlist_count = (*(int(__fastcall**)(void*))(*(_QWORD*)ret + 152LL))(ret);

			// Check if there are any playlists to process to avoid issues with do-while on count 0
			if (playlist_count > 0)
			{
				// Loop through all playlists
				do
				{
					// Get the name of the current playlist
					char* playlist = (*(char* (__fastcall**)(void*, _QWORD))(*(_QWORD*)ret + 160LL))(ret, (unsigned int)v7);
					// Append the playlist name to the combined string
					combined_playlists += playlist;
					// Append a newline character
					combined_playlists += '\n';
					// Increment index
					++v7;
				} while (v7 < playlist_count); // Continue until all playlists are processed
			}

			// Call the target function once with the combined, newline-separated string
			reinterpret_cast<void(*)(__int64, const char*, const char*)>((uintptr_t(GetModuleHandleA("AdminServer.dll")) + 0xB310))((a1 - 704), "launchplaylist", combined_playlists.c_str());

		}
		char* playlist = reinterpret_cast<char* (*)()>(G_engine_ds + 0xB8C40)();
		if (playlist) {
			reinterpret_cast<void(*)(__int64, const char*, const char*)>((uintptr_t(GetModuleHandleA("AdminServer.dll")) + 0xB200))(a1 - 704, "launchplaylist", playlist);
		}
	}

	//}
	oCServerInfoPanel__OnServerDataResponse_14730(a1, a2, a3);

}

//


class CDevicesManager;
class MMNotificationClient : public IMMNotificationClient
{
public:
	MMNotificationClient() {};
	virtual ~MMNotificationClient() {};
	ULONG STDMETHODCALLTYPE AddRef() { return 1; };
	ULONG STDMETHODCALLTYPE Release() { return 1; };
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, VOID** ppvInterface);
	HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDeviceId);
	HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR pwstrDeviceId) { return S_OK; };
	HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR pwstrDeviceId) { return S_OK; };
	HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) { return S_OK; };
	HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key) { return S_OK; };
};


HRESULT STDMETHODCALLTYPE MMNotificationClient::QueryInterface(REFIID riid, VOID** ppvInterface)
{
	if (IID_IUnknown == riid)
	{
		AddRef();
		*ppvInterface = (IUnknown*)this;
	}
	else if (__uuidof(IMMNotificationClient) == riid)
	{
		AddRef();
		*ppvInterface = (IMMNotificationClient*)this;
	}
	else
	{
		*ppvInterface = NULL;
		return E_NOINTERFACE;
	}
	return S_OK;
}

HRESULT STDMETHODCALLTYPE MMNotificationClient::OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDeviceId)
{
	
	if (role == eMultimedia)
	{
		Msg("Default device changed to %ls\n", pwstrDeviceId);
		int refresh_rate = *reinterpret_cast<int*>(G_engine + 0x7CAEA4);
		if (G_client) {
			Cbuf_AddText(0, "sound_reboot_xaudio",0);
		}
	}
	return S_OK;
}

MMNotificationClient g_mmNotificationClient{};

IMMDeviceEnumerator* g_mmDeviceEnumerator = nullptr;

void Init_MMNotificationClient()
{
	HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&g_mmDeviceEnumerator);
	if (SUCCEEDED(hr))
	{
		g_mmDeviceEnumerator->RegisterEndpointNotificationCallback(&g_mmNotificationClient);
	}
}

void Deinit_MMNotificationClient()
{
	if (g_mmDeviceEnumerator)
	{
		g_mmDeviceEnumerator->UnregisterEndpointNotificationCallback(&g_mmNotificationClient);
		g_mmDeviceEnumerator->Release();
		g_mmDeviceEnumerator = nullptr;
	}
}

typedef void(__cdecl* Snd_Restart_DirectSound_t)();
Snd_Restart_DirectSound_t Snd_Restart_DirectSound = nullptr;

void ConCommand_sound_reboot_xaudio(const CCommand& args)
{
	Msg("Restarting XAudio...\n");
	Snd_Restart_DirectSound();
	Msg("Restarted XAudio...\n");
}
typedef void(__cdecl* S_Init_t)();
typedef void(__cdecl* S_Shutdown_t)();
S_Init_t oS_Init = nullptr;
S_Shutdown_t oS_Shutdown = nullptr;

void S_Init_Hook()
{
	oS_Init();
	bool g_bNoSound = *reinterpret_cast<bool*>(G_engine + 0x20144E4);
	if (!g_bNoSound)
		Init_MMNotificationClient();
}

void S_Shutdown_Hook()
{
	oS_Shutdown();
	bool g_bNoSound = *reinterpret_cast<bool*>(G_engine + 0x20144E4);
	if (!g_bNoSound)
		Deinit_MMNotificationClient();
}
void toggleFullscreenMap_cmd(const CCommand& ccargs) {
	return;
}


bool __fastcall pCLocalise__AddFile(void* pVguiLocalize, const char* path, const char* pathId, bool bIncludeFallbackSearchPaths) {
	
	auto ret = o_pCLocalise__AddFile(pVguiLocalize, path, pathId, bIncludeFallbackSearchPaths);
	return true;
}
static void(__fastcall* o_pCLocalize__ReloadLocalizationFiles)(void* pVguiLocalize) = nullptr;

static void __fastcall h_CLocalize__ReloadLocalizationFiles(void* pVguiLocalize)
{
	for (const auto& file : modLocalization_files) {
		o_pCLocalise__AddFile(G_localizeIface, file.c_str(), "GAME", false);
	}
	o_pCLocalize__ReloadLocalizationFiles(pVguiLocalize);
}

void Setup_MMNotificationClient()
{
	MH_CreateHook((LPVOID)(G_engine + 0xEA00), &S_Init_Hook, reinterpret_cast<LPVOID*>(&oS_Init));
	MH_CreateHook((LPVOID)(G_engine + 0x114B0), &S_Shutdown_Hook, reinterpret_cast<LPVOID*>(&oS_Shutdown));
	Snd_Restart_DirectSound = reinterpret_cast<Snd_Restart_DirectSound_t>(G_engine + 0x15AF0);
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
		bDone = true;
	}
	auto name = notification_data->Loaded.BaseDllName->Buffer;
	if (strcmp_static(name, L"filesystem_stdio.dll") == 0) {
		G_filesystem_stdio = (uintptr_t)notification_data->Loaded.DllBase;
		InitCompressionHooks();
	}
	else if (strcmp_static(name, L"engine.dll") == 0) {
		do_engine(notification_data);
		should_init_security_fixes = true;
		//client = std::make_shared<discordpp::Client>();
	}
	else if (strcmp_static(name, L"engine_ds.dll") == 0) {
		G_engine_ds = (uintptr_t)notification_data->Loaded.DllBase;
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("engine_ds.dll") + 0x433C0), &ProcessConnectionlessPacketDedi, reinterpret_cast<LPVOID*>(&ProcessConnectionlessPacketOriginal));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("engine_ds.dll") + 0x30FE20), &StringCompare_AllTalkHookDedi, reinterpret_cast<LPVOID*>(&oStringCompare_AllTalkHookDedi));
		InitAddons();
		InitDedicated();
		constexpr auto a = (1 << 2);
		should_init_security_fixes = true;
	}
	else if (strcmp_static(name, L"vphysics.dll") == 0) {
		InitializeCriticalSectionAndSpinCount(&g_cs, 4000);
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("vphysics.dll") + 0x1032C0), &sub_1032C0_hook, reinterpret_cast<LPVOID*>(&o_sub_1032C0));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("vphysics.dll") + 0x103120), &sub_103120_hook, reinterpret_cast<LPVOID*>(&o_sub_103120));
		//CreateMiscHook(vphysicsdllBaseAddress, 0x100880, &sub_180100880, reinterpret_cast<LPVOID*>(&sub_180100880_org));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("vphysics.dll") + 0x100880), &sub_100880_hook, reinterpret_cast<LPVOID*>(&o_sub_100880));

		//MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("vphysics.dll") + 0xFFFF), &sub_FFFF, reinterpret_cast<LPVOID*>(&ovphys_sub_FFFF));
		//MH_EnableHook(MH_ALL_HOOKS);
	}
	else if (strcmp_static(name, L"materialsystem_dx11.dll") == 0) {
		SetupHudWarpMatSystemHooks();
		MH_EnableHook(MH_ALL_HOOKS);
	}
	else if (strcmp_static(name, L"vguimatsurface.dll") == 0) {
		SetupHudWarpVguiHooks();
		MH_EnableHook(MH_ALL_HOOKS);
	}
	else if ((strcmp_static(name, L"adminserver.dll") == 0) || ((strcmp_static(name, L"AdminServer.dll")) == 0)) {
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("AdminServer.dll") + 0x14730), &CServerInfoPanel__OnServerDataResponse_14730, reinterpret_cast<LPVOID*>(&oCServerInfoPanel__OnServerDataResponse_14730));
		MH_EnableHook(MH_ALL_HOOKS);
	}
	else if ((strcmp_static(name, "localize.dll") == 0)) {
		
	}
	else {
		bool is_client = !strcmp_static(name, L"client.dll");
		bool is_server = !is_client && !strcmp_static(name, L"server.dll");

		if (is_client) {
			G_client = (uintptr_t)notification_data->Loaded.DllBase;
			InitClient();
			SetupHudWarpHooks();
			Setup_MMNotificationClient();
			SetupLocalizeIface();
			SetupSurfaceRenderHooks();
			SetupSquirrelErrorNotificationHooks();
			SetupChatWriter();
			RegisterConCommand("+toggleFullscreenMap", toggleFullscreenMap_cmd, "Toggles the fullscreen map.", FCVAR_CLIENTDLL);
			RegisterConVar("cl_hold_to_rodeo_enable", "0", FCVAR_CLIENTDLL | FCVAR_ARCHIVE_PLAYERPROFILE, "0: Automatic rodeo. 1: Hold to rodeo ALL titans. 2: Hold to rodeo friendlies, automatically rodeo hostile titans.");
			RegisterConVar("bot_kick_on_death", "1", FCVAR_GAMEDLL | FCVAR_CHEAT, "Enable/disable bots getting kicked on death.");
			typedef bool(__fastcall* o_pCLocalise__AddFile_t)(void*, const char*, const char*, bool);
			o_pCLocalise__AddFile = (o_pCLocalise__AddFile_t)(G_localize + 0x7760);
			MH_CreateHook((LPVOID)(G_localize + 0x3A40), &h_CLocalize__ReloadLocalizationFiles, (LPVOID*)&o_pCLocalize__ReloadLocalizationFiles);
			MH_EnableHook(MH_ALL_HOOKS);
			CThread(DiscordThread).detach();
			
		}
		if (is_server) do_server(notification_data);
		if (should_init_security_fixes && (is_client || is_server)) {
			security_fixes_init();
			should_init_security_fixes = false;
		}
	}

}

