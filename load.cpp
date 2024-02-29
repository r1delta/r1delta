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
#include <intrin.h>
#include "memory.h"
#include "filesystem.h"
#include "defs.h"
#include "factory.h"
#include "core.h"
#include "load.h"
#include "patcher.h"
#include "MinHook.h"
#pragma intrinsic(_ReturnAddress)


wchar_t kNtDll[] = L"ntdll.dll";
char kLdrRegisterDllNotification[] = "LdrRegisterDllNotification";
char kLdrUnregisterDllNotification[] = "LdrUnregisterDllNotification";
void* dll_notification_cookie_;

void Status_ConMsg(const char* text, ...)
// clang-format on
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
	static AddMapVPKFileFunc addMapVPKFile = (AddMapVPKFileFunc)((((uintptr_t)GetModuleHandleA("filesystem_stdio.dll"))) + 0x4E80);
	if (pPath != NULL && strstr(pPath, "common") == NULL && strstr(pPath, "lobby") == NULL) {
		if (strncmp(pPath, "maps/", 5) == 0)
		{
			char newPath[256];
			_snprintf_s(newPath, sizeof(newPath), "vpk/client_%s", pPath + 5);
			addMapVPKFile(fileSystem, newPath);
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



void __stdcall LoaderNotificationCallback(
	unsigned long notification_reason,
	const LDR_DLL_NOTIFICATION_DATA* notification_data,
	void* context) {
	if (notification_reason != LDR_DLL_NOTIFICATION_REASON_LOADED)
		return;
	doBinaryPatchForFile(notification_data->Loaded);
	if (std::wstring((wchar_t*)notification_data->Loaded.BaseDllName->Buffer, notification_data->Loaded.BaseDllName->Length).find(SERVER_DLLWIDE) != std::string::npos) {
		uintptr_t vTableAddr = reinterpret_cast<uintptr_t>(GetModuleHandleA("server.dll")) + 0x807220;
		RemoveItemsFromVTable(vTableAddr, 35, 2);
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(SERVER_DLL) + 0x143A10), &CServerGameDLL__DLLInit, (LPVOID*)&CServerGameDLL__DLLInitOriginal);
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(SERVER_DLL) + 0x71E0BC), &hkcalloc_base, NULL);
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(SERVER_DLL) + 0x71E99C), &hkmalloc_base, NULL);
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(SERVER_DLL) + 0x71E9FC), &hkrealloc_base, NULL);
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(SERVER_DLL) + 0x72B480), &hkrecalloc_base, NULL);
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(SERVER_DLL) + 0x721000), &hkfree_base, NULL);

		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("launcher.dll") + 0x6A60), &CScriptVM__ctor, reinterpret_cast<LPVOID*>(&CScriptVM__ctororiginal));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("launcher.dll") + 0x1210), &CScriptManager__CreateNewVM, reinterpret_cast<LPVOID*>(&CScriptManager__CreateNewVMOriginal));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("launcher.dll") + 0x1630), &CScriptVM__GetUnknownVMPtr, reinterpret_cast<LPVOID*>(&CScriptVM__GetUnknownVMPtrOriginal));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("launcher.dll") + 0x15F0), &CScriptManager__DestroyVM, reinterpret_cast<LPVOID*>(&CScriptManager__DestroyVMOriginal));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("launcher.dll") + 0xCDB0), &CSquirrelVM__RegisterFunctionGuts, reinterpret_cast<LPVOID*>(&CSquirrelVM__RegisterFunctionGutsOriginal));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("launcher.dll") + 0xD650), &CSquirrelVM__PushVariant, reinterpret_cast<LPVOID*>(&CSquirrelVM__PushVariantOriginal));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("launcher.dll") + 0xD7B0), &CSquirrelVM__ConvertToVariant, reinterpret_cast<LPVOID*>(&CSquirrelVM__ConvertToVariantOriginal));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("launcher.dll") + 0xB110), &CSquirrelVM__ReleaseValue, reinterpret_cast<LPVOID*>(&CSquirrelVM__ReleaseValueOriginal));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("launcher.dll") + 0xA1F0), &CSquirrelVM__SetValue, reinterpret_cast<LPVOID*>(&CSquirrelVM__SetValueOriginal));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("launcher.dll") + 0x9C40), &CSquirrelVM__SetValueEx, reinterpret_cast<LPVOID*>(&CSquirrelVM__SetValueExOriginal));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("launcher.dll") + 0xBE60), &CSquirrelVM__TranslateCall, reinterpret_cast<LPVOID*>(&CSquirrelVM__TranslateCallOriginal));
		
#ifdef DEDICATED
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("engine_ds.dll") + 0x1693D0), &ParsePDATA, reinterpret_cast<LPVOID*>(&ParsePDATAOriginal));
#endif
		//MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("filesystem_stdio.dll") + 0x6A420), &ReadFileFromVPKHook, reinterpret_cast<LPVOID*>(&readFileFromVPK));
		//MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("filesystem_stdio.dll") + 0x9C20), &ReadFromCacheHook, reinterpret_cast<LPVOID*>(&readFromCache));
		//MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("filesystem_stdio.dll") + 0x15F20), &ReadFileFromFilesystemHook, reinterpret_cast<LPVOID*>(&readFileFromFilesystem));
		MH_CreateHook((LPVOID)GetProcAddress(GetModuleHandleA("vstdlib.dll"), "VStdLib_GetICVarFactory"), &VStdLib_GetICVarFactory, NULL);
#ifndef DEDICATED
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(ENGINE_DLL) + 0x136860), &Status_ConMsg, NULL);
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(ENGINE_DLL) + 0x1BF500), &Status_ConMsg, NULL);
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("filesystem_stdio.dll") + 0x196A0), &AddSearchPathHook, reinterpret_cast<LPVOID*>(&addSearchPathOriginal));
		//MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(ENGINE_DLL) + 0x1C79A0), &sub_1801C79A0, reinterpret_cast<LPVOID*>(&sub_1801C79A0Original));
		//
		//
		//diMH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(ENGINE_DLL) + 0x1D9E70), &MatchRecvPropsToSendProps_R, reinterpret_cast<LPVOID*>(NULL));
#endif
		//MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(ENGINE_DLL) + 0x217C30), &sub_180217C30, NULL);
		// Cast the function pointer to the function at 0x4E80
		MH_EnableHook(MH_ALL_HOOKS);
		std::cout << "did hooks" << std::endl;

	}
	if (std::wstring((wchar_t*)notification_data->Loaded.BaseDllName->Buffer, notification_data->Loaded.BaseDllName->Length).find(L"client.dll") != std::string::npos) {
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("client.dll") + 0x21FE50), &sub_18021FE50, reinterpret_cast<LPVOID*>(NULL));
		MH_EnableHook(MH_ALL_HOOKS);
	}

}