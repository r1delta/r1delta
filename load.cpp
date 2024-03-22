#include "load.h"
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
#include "thirdparty/silver-bun/module.h"
#pragma intrinsic(_ReturnAddress)

wchar_t kNtDll[] = L"ntdll.dll";
char kLdrRegisterDllNotification[] = "LdrRegisterDllNotification";
char kLdrUnregisterDllNotification[] = "LdrUnregisterDllNotification";
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

	std::cout << formatted << std::endl;
}


__declspec(dllexport) void whatever2() { Error(); };

typedef char(__fastcall* ParsePDATAType)(int a1, int a2, const char* a3, const char* a4);
ParsePDATAType ParsePDATAOriginal;

char __fastcall ParsePDATA(int a1, int a2, const char* a3, const char* a4)
{
	return true;
	static char* newbuf = (char*)malloc(0x65536);
	a3 = newbuf;
	return ParsePDATAOriginal(a1, a2, a3, a4);
}
typedef void (*AddSearchPathType)(IFileSystem* fileSystem, const char* pPath, const char* pathID, unsigned int addType);
AddSearchPathType addSearchPathOriginal;
typedef __int64(__fastcall* AddMapVPKFileFunc)(IFileSystem* fileSystem, const char* pPath);
void AddSearchPathHook(IFileSystem* fileSystem, const char* pPath, const char* pathID, unsigned int addType)
{
	//static AddMapVPKFileFunc addMapVPKFile = (AddMapVPKFileFunc)((((uintptr_t)GetModuleHandleA("filesystem_stdio.dll"))) + 0x4E80);
	
	// this function is used in debugprecachevpk concommand
	// alternatively, real game has that in call stack when I bp the above funciton
	using debug_precache_t = void(__fastcall*)(const char*, unsigned int, char);
	static auto debug_precache = debug_precache_t(uintptr_t(GetModuleHandleA("engine.dll")) + 0x19FB30);
	if (pPath != NULL && strstr(pPath, "common") == NULL && strstr(pPath, "lobby") == NULL) {
		if (strncmp(pPath, "maps/", 5) == 0)
		{
			char newPath[256];
			_snprintf_s(newPath, sizeof(newPath), "vpk/client_%s", pPath + 5);
			//addMapVPKFile(fileSystem, newPath);
			debug_precache(newPath, 2, 0);
		}
	}

	// Call the original function
	addSearchPathOriginal(fileSystem, pPath, pathID, addType);
}
signed __int64 __fastcall sub_1806580E0(char* a1, signed __int64 a2, const char* a3, va_list a4)
{
	signed __int64 result; // rax

	if (a2 <= 0)
		return 0i64;
	result = vsnprintf(a1, a2, a3, a4);
	if ((int)result < 0i64 || (int)result >= a2)
	{
		result = a2 - 1;
		a1[a2 - 1] = 0;
	}
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

enum
{
	TD_OFFSET_NORMAL = 0,
	TD_OFFSET_PACKED = 1,

	// Must be last
	TD_OFFSET_COUNT,
};
typedef enum _fieldtypes
{
	FIELD_VOID = 0, // No type or value
	FIELD_FLOAT, // Any floating point value
	FIELD_STRING, // A string ID (return from ALLOC_STRING)
	FIELD_VECTOR, // Any vector, QAngle, or AngularImpulse
	FIELD_QUATERNION, // A quaternion
	FIELD_INTEGER, // Any integer or enum
	FIELD_BOOLEAN, // boolean, implemented as an int, I may use this as a hint for compression
	FIELD_SHORT, // 2 byte integer
	FIELD_CHARACTER, // a byte
	FIELD_COLOR32, // 8-bit per channel r,g,b,a (32bit color)
	FIELD_EMBEDDED, // an embedded object with a datadesc, recursively traverse and embedded class/structure based on an additional
	// typedescription
	FIELD_CUSTOM, // special type that contains function pointers to it's read/write/parse functions

	FIELD_CLASSPTR, // CBaseEntity *
	FIELD_EHANDLE, // Entity handle
	FIELD_EDICT, // edict_t *

	FIELD_POSITION_VECTOR, // A world coordinate (these are fixed up across level transitions automagically)
	FIELD_TIME, // a floating point time (these are fixed up automatically too!)
	FIELD_TICK, // an integer tick count( fixed up similarly to time)
	FIELD_MODELNAME, // Engine string that is a model name (needs precache)
	FIELD_SOUNDNAME, // Engine string that is a sound name (needs precache)

	FIELD_INPUT, // a list of inputed data fields (all derived from CMultiInputVar)
	FIELD_FUNCTION, // A class function pointer (Think, Use, etc)

	FIELD_VMATRIX, // a vmatrix (output coords are NOT worldspace)

	// NOTE: Use float arrays for local transformations that don't need to be fixed up.
	FIELD_VMATRIX_WORLDSPACE, // A VMatrix that maps some local space to world space (translation is fixed up on level transitions)
	FIELD_MATRIX3X4_WORLDSPACE, // matrix3x4_t that maps some local space to world space (translation is fixed up on level transitions)

	FIELD_INTERVAL, // a start and range floating point interval ( e.g., 3.2->3.6 == 3.2 and 0.4 )
	FIELD_MODELINDEX, // a model index
	FIELD_MATERIALINDEX, // a material index (using the material precache string table)

	FIELD_VECTOR2D, // 2 floats

	FIELD_TYPECOUNT, // MUST BE LAST
} fieldtype_t;
struct typedescription_t;
struct datamap_t
{
	typedescription_t* dataDesc;
	int dataNumFields;
	char const* dataClassName;
	datamap_t* baseMap;

	bool chains_validated;
	// Have the "packed" offsets been computed
	bool packed_offsets_computed;
	int packed_size;
};
struct typedescription_t
{
	fieldtype_t fieldType;
	const char* fieldName;
	int fieldOffset[TD_OFFSET_COUNT]; // 0 == normal, 1 == packed offset
	unsigned short fieldSize;
	short flags;
	// the name of the variable in the map/fgd data, or the name of the action
	const char* externalName;
	// pointer to the function set for save/restoring of custom data types
	void* pSaveRestoreOps;
	// for associating function with string names
	void* inputFunc;
	// For embedding additional datatables inside this one
	datamap_t* td;

	// Stores the actual member variable size in bytes
	int fieldSizeInBytes;

	// FTYPEDESC_OVERRIDE point to first baseclass instance if chains_validated has occurred
	struct typedescription_t* override_field;

	// Used to track exclusion of baseclass fields
	int override_count;

	// Tolerance for field errors for float fields
	float fieldTolerance;
};
// write access to const memory has been detected, the output may be wrong!
// write access to const memory has been detected, the output may be wrong!
const char* sub_18021FE50(__int64 thisptr, const datamap_t* pCurrentMap, const typedescription_t* pField, const char* fmt, ...)
{
	++* (DWORD*)(thisptr + 32);
	const char* fieldname = "empty";
	const char* classname = "empty";
	int flags = 0;

	if (pField)
	{
		flags = pField->flags;
		fieldname = pField->fieldName ? pField->fieldName : "NULL";
		classname = pCurrentMap->dataClassName;
	}

	va_list argptr;
	char data[4096];
	int len;
	va_start(argptr, fmt);
	len = sub_1806580E0(data, sizeof(data), fmt, argptr);
	va_end(argptr);
	data[strcspn(data, "\n")] = 0;

	int v6 = 0; // edi
	__int64 v7 = 0; // rbx
	bool bUseLongName = true;
	std::string longName;
	if (*(int*)(thisptr + 80) > 0)
	{
		do
		{
			const char* result = *(const char**)(v7 + *(unsigned __int64*)(thisptr + 56));
			if (result)
			{
				const char* v8 = (const char*)*((unsigned __int64*)result + 1);
				const char* v9 = "NULL";
				if (v8)
					v9 = v8;
				longName += v9;
				longName += "/";
			}
		} while (v6 < *(DWORD*)(thisptr + 80));
	}

	printf("%2d (%d)%s%s::%s - %s\n", *(DWORD*)(thisptr + 32), *(DWORD*)(thisptr + 36), longName.c_str(), classname, fieldname, data);
	return 0;
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
	const char* commandsToSave[] = {
		"InitHUD",
		"FullyConnected",
		"MatchIsOver",
		"ChangeTitanBuildPoint",
		"ChangeCurrentTitanBuildPoint",
		"TutorialStatus",
		"ChangeCurrentTitanOID"
	};
	for (auto& command : commandsToSave) {
		if (strcmp(a2, command) == 0) {
			return true;
		}
	}
	return false;
}

typedef __int64 (*sub_629740Type)(__int64 a1, const char* a2, int a3);
sub_629740Type sub_629740Original;

__int64 __fastcall sub_629740(__int64 a1, const char* a2, int a3) {
	if (shouldSave(a2)) {
		savedCalls.push_back({ a1, a2, a3 });
		return -1;
	}
	else if (strcmp(a2, "RemoteWeaponReload") == 0) {
		for (auto& call : savedCalls) {
			sub_629740Original(call.a1, call.a2.c_str(), call.a3);
		}
		savedCalls.clear(); // Clear after processing
	}
	return sub_629740Original(a1, a2, a3);
}

// TODO(mrsteyk): REMOVE
void* CVEngineServer_PrecacheModel_o = nullptr;
uintptr_t CVEngineServer_PrecacheModel(uintptr_t a1, const char* a2, char a3) {
	auto ret = reinterpret_cast<decltype(&CVEngineServer_PrecacheModel)>(CVEngineServer_PrecacheModel_o)(a1, a2, a3);

	// ты хуесос полнейший, вондерер, где логгер сука нормальный
	// this print is so aids
	//printf("[STK] CVEngineServer_PrecacheModel('%s', %d) = %p\n", a2, +a3, LPVOID(a1));
	//std::cout << "[STK] CVEngineServer_PrecacheModel('" << a2 << "', " << +a3 << ") = " << LPVOID(a1) << std::endl;

	return ret;
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

	if (!strcmp(name, "modelprecache")) {
		items = CClientState + 0x19EF8;
	}
	else if (!strcmp(name, "genericprecache")) {
		items = CClientState + 0x1DEF8;
	}
	else if (!strcmp(name, "decalprecache")) {
		items = CClientState + 0x1FEF8;
	}

	using GetStringTable_t = uintptr_t(__fastcall*)(uintptr_t, const char*);
	static auto GetStringTable = GetStringTable_t(uintptr_t(GetModuleHandleA("engine.dll")) + 0x22B60);
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

	for (int i = 0; i < count; i++) {
		auto slot = items + (i * 16);

		auto name = (*(const char* (__fastcall**)(__int64, _QWORD))(*(_QWORD*)table + 72i64))(table, i);

		using CL_GetPrecacheUserData_t = uintptr_t(__fastcall*)(uintptr_t, uintptr_t);
		auto CL_GetPrecacheUserData = CL_GetPrecacheUserData_t(uintptr_t(GetModuleHandleA("engine.dll")) + 0x558C0);

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
	static auto server_mod = uintptr_t(GetModuleHandleA("server.dll"));
	static auto byte_180C318A6 = (uint8_t*)(server_mod + 0xC318A6);

	if (*byte_180C318A6) {
		using sub_1804FE8B0_t = uintptr_t(__fastcall*)(uintptr_t, uintptr_t);
		auto sub_1804FE8B0 = sub_1804FE8B0_t(server_mod + 0x4FE8B0);

		static auto EffectDispatch_ptr = (uintptr_t*)(server_mod + 0xC310C8);
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

		static auto StaticClassSystem001_ptr = (uintptr_t*)(server_mod + 0xC31000);
		auto StaticClassSystem001 = *StaticClassSystem001_ptr;
		for (size_t i = 0; i < 100; i++) {
			auto v5 = (*(__int64(__fastcall**)(__int64, _QWORD))(*(_QWORD*)StaticClassSystem001 + 24i64))(StaticClassSystem001, i);
			auto elem = g_militia_bodies[i];

			// if (*(_QWORD*)(v5 + 448))
			{
				using PrecacheModel_t = uintptr_t(__fastcall*)(const void*);
				static auto PrecacheModel = PrecacheModel_t(server_mod + 0x3B6A40);

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
bool isProcessingSendTables = false;
typedef char* (__cdecl* COM_StringCopyType)(char* in);
COM_StringCopyType COM_StringCopyOriginal;
char* __cdecl COM_StringCopy(char* in)
{
	if (isProcessingSendTables) {
		std::ofstream file("test.txt", std::ios::app);
		file << in << "\n";
		file.close();
	}
	return COM_StringCopyOriginal(in);
}
typedef char(__fastcall* DataTable_SetupReceiveTableFromSendTableType)(__int64, __int64);
DataTable_SetupReceiveTableFromSendTableType DataTable_SetupReceiveTableFromSendTableOriginal;
char __fastcall DataTable_SetupReceiveTableFromSendTable(__int64 a1, __int64 a2)
{
	isProcessingSendTables = true;
	char ret = DataTable_SetupReceiveTableFromSendTableOriginal(a1, a2);
	isProcessingSendTables = false;
	return ret;
}

enum class netadrtype_t
{
	NA_NULL = 0,
	NA_LOOPBACK,
	NA_IP,
};

class CNetAdr
{
public:
	CNetAdr(void) { Clear(); }
	CNetAdr(const char* pch) { SetFromString(pch); }
	void	Clear(void);

	inline void	SetPort(uint16_t newport) { port = newport; }
	inline void	SetType(netadrtype_t newtype) { type = newtype; }

	bool	SetFromSockadr(struct sockaddr_storage* s);
	bool	SetFromString(const char* pch, bool bUseDNS = false);

	inline netadrtype_t	GetType(void) const { return type; }
	inline uint16_t		GetPort(void) const { return port; }

	bool		CompareAdr(const CNetAdr& other) const;
	inline bool	ComparePort(const CNetAdr& other) const { return port == other.port; }
	inline bool	IsLoopback(void) const { return type == netadrtype_t::NA_LOOPBACK; } // true if engine loopback buffers are used.

	const char* ToString(bool onlyBase = false) const;
	void		ToString(char* pchBuffer, size_t unBufferSize, bool onlyBase = false) const;
	void		ToSockadr(struct sockaddr_storage* s) const;

private:
	netadrtype_t type;
	IN6_ADDR adr;
	uint16_t port;
	bool field_16;
	bool reliable;
};
typedef struct netpacket_s
{
	CNetAdr from;
	int source;
	double received;
	uint8_t* pData;
	BFRead message;
	int size;
	int wiresize;
	char stream;
	netpacket_s* pNext;
} netpacket_t;
bool ReadConnectPacket2015AndWriteConnectPacket2014(BFRead& msg, BFWrite& buffer)
{
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
char __fastcall ProcessConnectionlessPacket(unsigned int* a1, netpacket_s* a2)
{
	char buffer[1200] = { 0 };
	BFWrite writer(reinterpret_cast<uptr>(buffer), sizeof(buffer));

	if (((char*)a2->pData + 4)[0] == 'A' && ReadConnectPacket2015AndWriteConnectPacket2014(a2->message, writer) != -1)
	{
		if (lastReceived == a2->received)
			return false;
		lastReceived = a2->received;
		BFRead converted(reinterpret_cast<uptr>(buffer), writer.GetNumBytesWritten());
		a2->message = converted;
		return ProcessConnectionlessPacketOriginal(a1, a2);
	}
	else
	{
		return ProcessConnectionlessPacketOriginal(a1, a2);
	}
}
typedef void (*CAI_NetworkManager__BuildStubType)(__int64 a1);
typedef void (*CAI_NetworkManager__LoadNavMeshType)(__int64 a1, __int64 a2, const char* a3);
CAI_NetworkManager__LoadNavMeshType CAI_NetworkManager__LoadNavMeshOriginal;
void __fastcall CAI_NetworkManager__LoadNavMesh(__int64 a1, __int64 a2, const char* a3)
{
	CAI_NetworkManager__LoadNavMeshOriginal(a1, a2, a3);
	*(int*)((uintptr_t)(GetModuleHandleA("server.dll")) + 0xC317F0) &= ~1;
	((CAI_NetworkManager__BuildStubType)(((uintptr_t)(GetModuleHandleA("server.dll"))) + 0x3664C0))(a1);
	((CAI_NetworkManager__BuildStubType)(((uintptr_t)(GetModuleHandleA("server.dll"))) + 0x3645f0))(a1);
}

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
	MH_CreateHook(LPVOID(engineDS.GetModuleBase() + 0x144960), LPVOID(engine.GetModuleBase() + 0x1F47C0), NULL); // NET_OutOfBandPrintf
	MH_CreateHook(LPVOID(engineDS.GetModuleBase() + 0x142ED0), LPVOID(engine.GetModuleBase() + 0x1F2CF0), NULL); // NET_ProcessSocket
	MH_CreateHook(LPVOID(engineDS.GetModuleBase() + 0x1458A0), LPVOID(engine.GetModuleBase() + 0x1F5650), NULL); // NET_RemoveNetChannel
	MH_CreateHook(LPVOID(engineDS.GetModuleBase() + 0x143390), LPVOID(engine.GetModuleBase() + 0x1F31B0), NULL); // NET_RunFrame
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
	reinterpret_cast<char(__fastcall*)(__int64, CreateInterfaceFn)>((uintptr_t)(engine.GetModuleBase())+0x01A04A0)(0, (CreateInterfaceFn)(engineDS.GetModuleBase() + 0xE9000)); // connect nondedi engine
	reinterpret_cast<void(__fastcall*)(int, void*)>((uintptr_t)(engine.GetModuleBase())+0x47F580)(0, 0); // register nondedi engine cvars
	return Host_InitDedicatedOriginal(a1, a2, a3);
}
void __stdcall LoaderNotificationCallback(
	unsigned long notification_reason,
	const LDR_DLL_NOTIFICATION_DATA* notification_data,
	void* context) {
	if (notification_reason != LDR_DLL_NOTIFICATION_REASON_LOADED)
		return;
	doBinaryPatchForFile(notification_data->Loaded);
	if (std::wstring((wchar_t*)notification_data->Loaded.BaseDllName->Buffer, notification_data->Loaded.BaseDllName->Length).find(L"server.dll") != std::string::npos) {
		uintptr_t vTableAddr = reinterpret_cast<uintptr_t>(GetModuleHandleA("server.dll")) + 0x807220;
		RemoveItemsFromVTable(vTableAddr, 35, 2);
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("server.dll") + 0x143A10), &CServerGameDLL__DLLInit, (LPVOID*)&CServerGameDLL__DLLInitOriginal);
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("server.dll") + 0x71E0BC), &hkcalloc_base, NULL);
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("server.dll") + 0x71E99C), &hkmalloc_base, NULL);
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("server.dll") + 0x71E9FC), &hkrealloc_base, NULL);
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("server.dll") + 0x72B480), &hkrecalloc_base, NULL);
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("server.dll") + 0x721000), &hkfree_base, NULL);
		//MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("server.dll") + 0x364D00), &CAI_NetworkManager__LoadNavMesh, reinterpret_cast<LPVOID*>(&CAI_NetworkManager__LoadNavMeshOriginal));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(VSCRIPT_DLL) + (IsDedicatedServer() ? 0x6A80 : 0x6A60)), &CScriptVM__ctor, reinterpret_cast<LPVOID*>(&CScriptVM__ctororiginal));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(VSCRIPT_DLL) + (IsDedicatedServer() ? 0x1210 : 0x1210)), &CScriptManager__CreateNewVM, reinterpret_cast<LPVOID*>(&CScriptManager__CreateNewVMOriginal));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(VSCRIPT_DLL) + (IsDedicatedServer() ? 0x1640 : 0x1630)), &CScriptVM__GetUnknownVMPtr, reinterpret_cast<LPVOID*>(&CScriptVM__GetUnknownVMPtrOriginal));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(VSCRIPT_DLL) + (IsDedicatedServer() ? 0x1600 : 0x15F0)), &CScriptManager__DestroyVM, reinterpret_cast<LPVOID*>(&CScriptManager__DestroyVMOriginal));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(VSCRIPT_DLL) + (IsDedicatedServer() ? 0xCDD0 : 0xCDB0)), &CSquirrelVM__RegisterFunctionGuts, reinterpret_cast<LPVOID*>(&CSquirrelVM__RegisterFunctionGutsOriginal));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(VSCRIPT_DLL) + (IsDedicatedServer() ? 0xD670 : 0xD650)), &CSquirrelVM__PushVariant, reinterpret_cast<LPVOID*>(&CSquirrelVM__PushVariantOriginal));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(VSCRIPT_DLL) + (IsDedicatedServer() ? 0xD7D0 : 0xD7B0)), &CSquirrelVM__ConvertToVariant, reinterpret_cast<LPVOID*>(&CSquirrelVM__ConvertToVariantOriginal));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(VSCRIPT_DLL) + (IsDedicatedServer() ? 0xB130 : 0xB110)), &CSquirrelVM__ReleaseValue, reinterpret_cast<LPVOID*>(&CSquirrelVM__ReleaseValueOriginal));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(VSCRIPT_DLL) + (IsDedicatedServer() ? 0xA210 : 0xA1F0)), &CSquirrelVM__SetValue, reinterpret_cast<LPVOID*>(&CSquirrelVM__SetValueOriginal));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(VSCRIPT_DLL) + (IsDedicatedServer() ? 0x9C60 : 0x9C40)), &CSquirrelVM__SetValueEx, reinterpret_cast<LPVOID*>(&CSquirrelVM__SetValueExOriginal));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(VSCRIPT_DLL) + (IsDedicatedServer() ? 0xBE80 : 0xBE60)), &CSquirrelVM__TranslateCall, reinterpret_cast<LPVOID*>(&CSquirrelVM__TranslateCallOriginal));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("server.dll") + 0x507560), &ServerClassInit_DT_BasePlayer, reinterpret_cast<LPVOID*>(&ServerClassInit_DT_BasePlayerOriginal));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("server.dll") + 0x51DFE0), &ServerClassInit_DT_Local, reinterpret_cast<LPVOID*>(&ServerClassInit_DT_LocalOriginal));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("server.dll") + 0x5064F0), &ServerClassInit_DT_LocalPlayerExclusive, reinterpret_cast<LPVOID*>(&ServerClassInit_DT_LocalPlayerExclusiveOriginal));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("server.dll") + 0x593270), &ServerClassInit_DT_TitanSoul, reinterpret_cast<LPVOID*>(&ServerClassInit_DT_TitanSoulOriginal));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("server.dll") + 0x629740), &sub_629740, reinterpret_cast<LPVOID*>(&sub_629740Original));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("server.dll") + 0x3A1EC0), &CBaseEntity__SendProxy_CellOrigin, reinterpret_cast<LPVOID*>(NULL));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("server.dll") + 0x3A2020), &CBaseEntity__SendProxy_CellOriginXY, reinterpret_cast<LPVOID*>(NULL));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("server.dll") + 0x3A2130), &CBaseEntity__SendProxy_CellOriginZ, reinterpret_cast<LPVOID*>(NULL));

		MH_CreateHook((LPVOID)GetProcAddress(GetModuleHandleA("vstdlib.dll"), "VStdLib_GetICVarFactory"), &VStdLib_GetICVarFactory, NULL);
		if (!IsDedicatedServer()) {
			MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(ENGINE_DLL) + 0x136860), &Status_ConMsg, NULL);
			MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(ENGINE_DLL) + 0x1BF500), &Status_ConMsg, NULL);
			MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(ENGINE_DLL) + 0x1168B0), &COM_StringCopy, reinterpret_cast<LPVOID*>(&COM_StringCopyOriginal));
			MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(ENGINE_DLL) + 0x1C79A0), &DataTable_SetupReceiveTableFromSendTable, reinterpret_cast<LPVOID*>(&DataTable_SetupReceiveTableFromSendTableOriginal));
		}
		//MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("filesystem_stdio.dll") + 0x196A0), &AddSearchPathHook, reinterpret_cast<LPVOID*>(&addSearchPathOriginal));
		//MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(ENGINE_DLL) + 0x1C79A0), &sub_1801C79A0, reinterpret_cast<LPVOID*>(&sub_1801C79A0Original));
		//
		//
		//diMH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(ENGINE_DLL) + 0x1D9E70), &MatchRecvPropsToSendProps_R, reinterpret_cast<LPVOID*>(NULL));
		//MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(ENGINE_DLL) + 0x217C30), &sub_180217C30, NULL);
		// Cast the function pointer to the function at 0x4E80

		// TODO(mrsteyk): REMOVE
		if (!IsDedicatedServer())
		{
			auto engine_mod = GetModuleHandleA("engine.dll");
			auto CVEngineServer_PrecacheModel_ptr = uintptr_t(engine_mod) + 0x0FC4D0;
			MH_CreateHook(LPVOID(CVEngineServer_PrecacheModel_ptr), &CVEngineServer_PrecacheModel, &CVEngineServer_PrecacheModel_o);
		}

		// Fix precache start
		{
			LoadLibraryA("engine_r1o.dll");
			auto r1oe_mod = uintptr_t(GetModuleHandleA("engine_r1o.dll"));
			auto server_mod = uintptr_t(GetModuleHandleA("server.dll"));
			// Cache bodymodel_militia to our own array...
			//MH_CreateHook(LPVOID(r1oe_mod + 0xF5790), &SetPreCache, &SetPreCache_o);
			// Rebuild CHL2_Player's precache to take our stuff into account
			MH_CreateHook(LPVOID(server_mod + 0x41E070), &CHL2_Player_Precache, 0);
		}
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("engine.dll") + 0x72360), &cl_DumpPrecacheStats, NULL);
		if (IsDedicatedServer()) {
			MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("filesystem_stdio.dll") + 0x6A420), &ReadFileFromVPKHook, reinterpret_cast<LPVOID*>(&readFileFromVPK));
			MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("filesystem_stdio.dll") + 0x9C20), &ReadFromCacheHook, reinterpret_cast<LPVOID*>(&readFromCache));
			MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("filesystem_stdio.dll") + 0x15F20), &ReadFileFromFilesystemHook, reinterpret_cast<LPVOID*>(&readFileFromFilesystem));
		}
		MH_EnableHook(MH_ALL_HOOKS);
		std::cout << "did hooks" << std::endl;

	}

	if (IsDedicatedServer() && (std::wstring((wchar_t*)notification_data->Loaded.BaseDllName->Buffer, notification_data->Loaded.BaseDllName->Length).find(L"engine_ds.dll") != std::string::npos)) {
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

		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("engine_ds.dll") + 0x1693D0), &ParsePDATA, reinterpret_cast<LPVOID*>(&ParsePDATAOriginal));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("engine_ds.dll") + 0x433C0), &ProcessConnectionlessPacket, reinterpret_cast<LPVOID*>(&ProcessConnectionlessPacketOriginal));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("engine_ds.dll") + 0xA1B90), &Host_InitDedicated, reinterpret_cast<LPVOID*>(&Host_InitDedicatedOriginal));

		MH_EnableHook(MH_ALL_HOOKS);

	}

	if (std::wstring((wchar_t*)notification_data->Loaded.BaseDllName->Buffer, notification_data->Loaded.BaseDllName->Length).find(L"client.dll") != std::string::npos) {
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("client.dll") + 0x21FE50), &sub_18021FE50, reinterpret_cast<LPVOID*>(NULL));
		MH_EnableHook(MH_ALL_HOOKS);
	}

}