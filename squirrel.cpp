#pragma once

#include "load.h"
#include <cstdlib>
#include <crtdbg.h>
#include <new>
#include "windows.h"
#include <iostream>
#include "cvar.h"
#include <random>

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
#include "logging.h"
#include <map>
#include "keyvalues.h"
#include "persistentdata.h"
#include "load.h"

//#define USE_CURL

#ifdef USE_CURL
#define CURL_STATICLIB
#include <curl/curl.h>
#pragma comment(lib, "libcurl.lib")
#endif // USE_CURL

#include <random>
#include "masterserver.h"

#pragma intrinsic(_ReturnAddress)

class ScriptFunctionRegistry {
public:
	static ScriptFunctionRegistry& getInstance() {
		static ScriptFunctionRegistry instance;
		return instance;
	}

	void addFunction(std::unique_ptr<SQFuncRegistration> func) {
		m_functions.push_back(std::move(func));
	}

	void registerFunctions(void* vmPtr, ScriptContext context) {
		typedef int64_t(*AddSquirrelRegType)(void*, SQFuncRegistrationInternal*);
		AddSquirrelRegType AddSquirrelReg = reinterpret_cast<AddSquirrelRegType>(G_vscript + (IsDedicatedServer() ? 0x8E70 : 0x8E50));

		for (const auto& func : m_functions) {
			if (func->GetContext() == context) {
				AddSquirrelReg(vmPtr, func->GetInternalReg());
			}
		}
	}

private:
	ScriptFunctionRegistry() = default;
	std::vector<std::unique_ptr<SQFuncRegistration>> m_functions;
};

typedef SQRESULT(*sq_compile_t)(HSQUIRRELVM, SQLEXREADFUNC, SQUserPointer, const SQChar*, SQBool);
typedef SQRESULT(*sq_compilebuffer_t)(HSQUIRRELVM, const SQChar*, SQInteger, const SQChar*, SQBool);
typedef __int64(__fastcall* base_getroottable_t)(HSQUIRRELVM);
typedef SQRESULT(*sq_call_t)(HSQUIRRELVM, SQInteger, SQBool, SQBool);
typedef SQRESULT(*sq_newslot_t)(HSQUIRRELVM, SQInteger, SQBool);
typedef void (*SQVM_Pop_t)(HSQUIRRELVM, SQInteger);
typedef void (*sq_push_t)(HSQUIRRELVM, SQInteger);
typedef void (*SQVM_Raise_Error_t)(HSQUIRRELVM, const SQChar*, ...);
typedef SQChar* (__fastcall* IdType2Name_t)(SQObjectType);
typedef SQRESULT(*sq_getstring_t)(HSQUIRRELVM, SQInteger, const SQChar**);
typedef SQRESULT(*sq_getinteger_t)(R1SquirrelVM*, HSQUIRRELVM, SQInteger, SQInteger*);
typedef SQRESULT(*sq_getfloat_t)(R1SquirrelVM*, HSQUIRRELVM, SQInteger, SQFloat*);
typedef SQRESULT(*sq_getbool_t)(R1SquirrelVM*, HSQUIRRELVM, SQInteger, SQBool*);
typedef void (*sq_pushnull_t)(HSQUIRRELVM);
typedef void (*sq_pushstring_t)(HSQUIRRELVM, const SQChar*, SQInteger);
typedef void (*sq_pushinteger_t)(R1SquirrelVM*, HSQUIRRELVM, SQInteger);
typedef void (*sq_pushfloat_t)(R1SquirrelVM*, HSQUIRRELVM, SQFloat);
typedef void (*sq_pushbool_t)(R1SquirrelVM*, HSQUIRRELVM, SQBool);
typedef void (*sq_tostring_t)(HSQUIRRELVM, SQInteger);
typedef SQInteger(*sq_getsize_t)(R1SquirrelVM*, HSQUIRRELVM, SQInteger);
typedef SQObjectType(*sq_gettype_t)(HSQUIRRELVM, SQInteger);
typedef SQRESULT(*sq_getstackobj_t)(R1SquirrelVM*, HSQUIRRELVM, SQInteger, SQObject*);
typedef SQRESULT(*sq_get_t)(HSQUIRRELVM, SQInteger);
typedef SQRESULT(*sq_get_noerr_t)(HSQUIRRELVM, SQInteger);
typedef SQInteger(*sq_gettop_t)(R1SquirrelVM*, HSQUIRRELVM);
typedef void (*sq_newtable_t)(HSQUIRRELVM);
typedef SQRESULT(*sq_next_t)(HSQUIRRELVM, SQInteger);
typedef SQRESULT(*sq_getinstanceup_t)(HSQUIRRELVM, SQInteger, SQUserPointer*, SQUserPointer);
typedef void (*sq_newarray_t)(HSQUIRRELVM, SQInteger);
typedef SQRESULT(*sq_arrayappend_t)(HSQUIRRELVM, SQInteger);
typedef SQRESULT(*sq_throwerror_t)(HSQUIRRELVM, const char* err);
typedef bool (*RunCallback_t)(R1SquirrelVM*, const char*);
typedef __int64 (*CSquirrelVM__RegisterGlobalConstantInt_t)(R1SquirrelVM*, const char*, signed int);
typedef void* (*CSquirrelVM__GetEntityFromInstance_t)(R1SquirrelVM*, SQObject*, char**);
typedef char** (*sq_GetEntityConstant_CBaseEntity_t)();
typedef int64_t(*AddSquirrelReg_t)(R1SquirrelVM*, SQFuncRegistrationInternal*);

// Global variables for all the functions
sq_compile_t sq_compile;
sq_compilebuffer_t sq_compilebuffer;
base_getroottable_t base_getroottable;
sq_call_t sq_call;
sq_newslot_t sq_newslot;
SQVM_Pop_t SQVM_Pop;
sq_push_t sq_push;
SQVM_Raise_Error_t SQVM_Raise_Error;
IdType2Name_t IdType2Name;
sq_getstring_t sq_getstring;
sq_getinteger_t sq_getinteger;
sq_getfloat_t sq_getfloat;
sq_getbool_t sq_getbool;
sq_pushnull_t sq_pushnull;
sq_pushstring_t sq_pushstring;
sq_pushinteger_t sq_pushinteger;
sq_pushfloat_t sq_pushfloat;
sq_pushbool_t sq_pushbool;
sq_tostring_t sq_tostring;
sq_getsize_t sq_getsize;
sq_gettype_t sq_gettype;
sq_getstackobj_t sq_getstackobj;
sq_get_t sq_get;
sq_get_noerr_t sq_get_noerr;
sq_gettop_t sq_gettop;
sq_newtable_t sq_newtable;
sq_get_table_t sq_gettable;
sq_next_t sq_next;
sq_getinstanceup_t sq_getinstanceup;
sq_newarray_t sq_newarray;
sq_arrayappend_t sq_arrayappend;
sq_throwerror_t sq_throwerror;
RunCallback_t RunCallback;
CSquirrelVM__RegisterGlobalConstantInt_t CSquirrelVM__RegisterGlobalConstantInt;
CSquirrelVM__GetEntityFromInstance_t CSquirrelVM__GetEntityFromInstance;
sq_GetEntityConstant_CBaseEntity_t sq_GetEntityConstant_CBaseEntity; // CLIENT
AddSquirrelReg_t AddSquirrelReg;
//
//const char* __fastcall Script_GetConVarString(const char* a1, __int64 a2, __int64 a3)
//{
//	_BYTE v5[8]; // [rsp+20h] [rbp-18h] BYREF
//	__int64 v6; // [rsp+28h] [rbp-10h]
//
//	LOBYTE(a3) = 1;
//	sub_180665BA0(v5, a1, a3);
//	if ((unsigned __int8)sub_180664D70(v5))
//		return *(const char**)(v6 + 72);
//	sub_1802C4FE0("ConVar %s is not valid", a1);
//	return Locale;
//}
SQInteger SquirrelNativeFunctionTest(HSQUIRRELVM v, __int64 a2, __int64 a3) {
	const SQChar* str;
	sq_getstring(v, 2, &str);
	SQInteger integer;
	sq_getinteger(nullptr, v, 3, &integer);
	SQFloat fl;
	sq_getfloat(nullptr, v, 4, &fl);
	SQBool bo;
	sq_getbool(nullptr, v, 5, &bo);

	Msg("[sq] SquirrelNativeFunctionTest native: %s %i %f %d", str, integer, fl, bo);

	sq_pushstring(v, "from native", -1);
	return 1;
}

typedef void (*CPlayer__Script_XP_Changed)(__int64 at);
CPlayer__Script_XP_Changed CPlayer__Script_XP_ChangedOrig;

void __fastcall CPlayer__Script_XP_ChangedHook(__int64 a1) {
	Msg("CPlayer__Script_XP_ChangedHook %x\n", a1);
	CPlayer__Script_XP_ChangedOrig(a1);
}

typedef void (*CPlayer__Script_XP_Changed)(__int64 at);
CPlayer__Script_XP_Changed CPlayer__Script_Gen_Changed_Orig;

void __fastcall CPlayer__Script_Gen_Changed(__int64 a1) {
	Msg("Gen Changed Hook\n");
	CPlayer__Script_Gen_Changed_Orig(a1);
}

typedef __int64 (*CPlayer__SetXP)(__int64 a1, int a2);

CPlayer__SetXP CPlayer__SetXPRebuildOrig;
void __fastcall CPlayer__SetXPRebuild(__int64 a1, int a2) {
	//Msg("CPlayer__SetXPRebuild\n");
	*(int*)(a1 + 0x1834) = a2;
	return;
}

typedef __int64 (*CPlayer__SetGen)(__int64 a1, int a2);

CPlayer__SetGen CPlayer__SetGenOrig;

void __fastcall CPlayer__SetGenHook(__int64 a1, int a2) {
	Msg("CPlayer__SetGenHook %d\n", a2);
	*(int*)(a1 + 0x183C) = a2;
}

void __fastcall SetGen(__int64 a1, int a2) {
	if (a2 < 0) {
		*(int*)(a1 + 0x183C) = 0;
	}
	else if (a2 > 9) {
		*(int*)(a1 + 0x183C) = 9;
	}
	else {
		*(int*)(a1 + 0x183C) = a2;
	}
}

void* __fastcall CSquirrelVM__GetEntityFromInstance_Rebuild(__int64 a2, __int64 a3) {
	__int64 result; // rax
	__int64 v4; // rax
	__int64 v5; // rcx
	__int64 v6; // rax
	unsigned __int64 v7; // rax

	if (!a2) {
		return 0LL;
	}
	if (*(_DWORD*)a2 != 0xA008000)
		return 0LL;
	v4 = *(_QWORD*)(a2 + 8);
	v5 = *(_QWORD*)(v4 + 64);
	if (!v5)
		return 0LL;
	v6 = *(_QWORD*)(v4 + 56);
	if (a3) {
		v7 = *(_QWORD*)(v6 + 128);
		if (v7 != a3) {
			if (v7 < 2)
				return 0LL;
			result = *(_QWORD*)(v7 + 24);
			if (!result)
				return 0LL;
			while (result != a3) {
				result = *(_QWORD*)(result + 24);
				if (!result)
					return (void*)result;
			}
		}
	}
	return (void*)(*(_QWORD*)v5);
}

void* sq_getentity(HSQUIRRELVM v, SQInteger iStackPos) {
	SQObject obj;
	sq_getstackobj(nullptr, v, iStackPos, &obj);
	auto constant = (G_server + 0xD42040);
	return CSquirrelVM__GetEntityFromInstance_Rebuild((__int64)(&obj), (__int64)((char**)constant));
}

template <typename Return, typename ... Arguments>
constexpr Return Call(void* vmt, const std::uint32_t index, Arguments ... args) noexcept {
	using Function = Return(__thiscall*)(void*, decltype(args)...);
	return (*static_cast<Function**>(vmt))[index](vmt, args...);
}

struct AddonInfo {
	const char* name;
	const char* author;
	const char* version;
	const char* description;
	const char* enabled;
};

int UpdateAddons(HSQUIRRELVM v, SQInteger index, SQBool enabled) {
	const char* str = "thread void function() { wait 1 while(true) {  wait 1 } }";
	auto result = sq_compilebuffer(v, str, strlen(str), "console", SQTrue);
	if (result != -1) {
		base_getroottable(v);
		SQRESULT callResult = sq_call(v, 1, false, true);
		Msg("Call Result: %d\n", callResult);
	}
	auto func_addr = g_CVFileSystem->GetSearchPath;
	auto kv_load_file = G_client + 0x65F980;
	auto kv_write_file = G_client + 0x65DB30;
	void* file_system = *(void**)(G_client + 0x380E678);
	auto base_file_system = (uintptr_t)file_system + 0x8;
	auto kv_load_file_addr = (int(__fastcall*)(KeyValues*, int64, char*, const char*, int))kv_load_file;
	auto kv_write_file_addr = (int(__fastcall*)(KeyValues*, int64, char*))kv_write_file;
	auto load_addon_info_addr = G_client + 0x65F980;
	auto load_addon_info_file = (int(__fastcall*)(void*, KeyValues*, const char*, bool))load_addon_info_addr;
	auto func = (int(__fastcall*)(void*, const char*, int64, char*, int64))func_addr;
	char szModPath[260];
	char szAddOnListPath[260];
	char szAddonDirName[60];
	auto ret = func(file_system, "MOD", 0, szModPath, 260);
	snprintf(szAddOnListPath, 260, "%s%s", szModPath, "addonlist.txt");
	KeyValues* kv = new KeyValues("AddonList");
	kv_load_file_addr(kv, base_file_system, szAddOnListPath, nullptr, 0);
	auto vm = GetServerVMPtr();
	std::cout << kv->GetString() << std::endl;
	int i = 0; // Declare the iterator before the loop
	for (KeyValues* subkey = kv->GetFirstValue(); subkey; subkey = subkey->GetNextValue(), ++i) {
		const char* name = subkey->GetName();
		bool value = subkey->GetInt(NULL, 0) != 0;
		sq_getinteger(vm, v, i + 2, &index);
		sq_getbool(vm, v, i + 3, &enabled);
		if (i == index) {
			subkey->SetInt(NULL, enabled);
			std::cout << "Updated: " << name << " to " << enabled << std::endl;
			break;
		}
		// find the index of the addon
	}
	sq_pushinteger(vm, v, 1);
	kv_write_file_addr(kv, base_file_system, szAddOnListPath);
	// Save the keyvalues to the file
	return 1;
}

int GetMods(HSQUIRRELVM v) {
	sq_compilebuffer(v, "printt(\"Hello, World!\")", -1, "test", SQTrue);

	auto func_addr = g_CVFileSystem->GetSearchPath;
	auto remove_file_addr = g_CVFileSystem->RemoveFile;
	auto kv_load_file = G_client + 0x65F980;
	void* file_system = *(void**)(G_client + 0x380E678);
	auto base_file_system = (uintptr_t)file_system + 0x8;
	auto kv_load_file_addr = (int(__fastcall*)(KeyValues*, int64, char*, const char*, int))kv_load_file;
	auto load_addon_info_addr = G_client + 0x3D82F0;
	auto get_addon_image_addr = G_client + 0x3DAB30;
	auto load_addon_info_file = (int(__fastcall*)(void*, KeyValues**, const char*, bool))load_addon_info_addr;
	auto get_addon_image = (int(__fastcall*)(void*, const char*, char*, int, bool))get_addon_image_addr;
	auto func = (int(__fastcall*)(void*, const char*, int64, char*, int64))func_addr;
	auto remove_file = (int(__fastcall*)(void*, char*, int))remove_file_addr;
	auto extract_addon_info_addr = G_client + 0x3D9910;
	auto extract_addon_info = (int(__fastcall*)(void*, char*))extract_addon_info_addr;
	auto strip_extention_addr = G_client + 0x658700;
	auto strip_extention = (int(__fastcall*)(const char*, char*, int))strip_extention_addr;
	auto create_vpk_addr = G_client + 0x511E30;
	auto create_vpk = (int(__fastcall*)(char*, char*, void*, int, int, int64))create_vpk_addr;
	auto vpk_open_file_addr = G_client + 0x50C250;
	auto vpk_open_file = (int(__fastcall*)(char*, int*, const char*))vpk_open_file_addr;
	char szModPath[260];
	char szAddOnListPath[260];
	char szAddonDirName[64];
	auto vm = GetServerVMPtr();
	auto ret = func(file_system, "MOD", 0, szModPath, 260);
	snprintf(szAddOnListPath, 260, "%s%s", szModPath, "addonlist.txt");
	KeyValues* kv = new KeyValues("AddonList");
	printf("AddonList Path: %s\n", szAddOnListPath);
	char addoninfoFilename[260];
	kv_load_file_addr(kv, base_file_system, szAddOnListPath, nullptr, 0);
	sq_newarray(v, 0);
	bool bIsVPK = false;

	for (KeyValues* subkey = kv->GetFirstValue(); subkey; subkey = subkey->GetNextValue()) {
		const char* name = subkey->GetName();
		if (V_stristr(name, ".vpk")) {
			const char* firstValue = subkey->GetName();
			strip_extention(firstValue, szAddonDirName, 60);
			extract_addon_info(nullptr, szAddonDirName);
			bIsVPK = 1;
			char szAddonVPKFullPath[260];
			char szAddonInfoFullPath[260];
			char vpk_file[33680];
			snprintf(szAddonVPKFullPath, 260, "%s%s%c%s.vpk", szModPath, "addons", '\\', szAddonDirName);
			create_vpk(vpk_file, szAddonDirName, file_system, 0, 1, 0);
			snprintf(szAddonInfoFullPath, 260, "%s%s%c%s", szModPath, "addons", '\\', "addoninfo.txt");
			int result;
			vpk_open_file(vpk_file, &result, "addoninfo.txt");
			if (result != -1) {
				std::printf("Addon: %s\n", szAddonDirName);
			}
		}
		else {
			bIsVPK = 0;
			V_strncpy(szAddonDirName, name, 60);
		}
		printf("Addon: %s\n", szAddonDirName);
		if (load_addon_info_file(nullptr, &kv, name, bIsVPK)) {
			char image[260];
			printf("Addon After load: %s\n", szAddonDirName);
			get_addon_image(nullptr, name, image, 260, bIsVPK);
			snprintf(addoninfoFilename, 260, "%s%s%c%s%c%s", szModPath, "addons", '\\', szAddonDirName, '\\', "addoninfo.txt");
			KeyValues* addoninfo = new KeyValues("AddonInfo");
			kv_load_file_addr(addoninfo, base_file_system, addoninfoFilename, nullptr, 0);
			bool enabled = subkey->GetInt(NULL, 0) != 0;
			auto author = addoninfo->GetWString("addonauthor");
			auto addon_name = addoninfo->GetWString("addontitle");
			auto version = addoninfo->GetWString("addonversion");
			auto description = addoninfo->GetWString("addondescription");
			char author_str[260];
			char addon_name_str[260];
			char description_str[260];
			char version_str[260];
			wcstombs(addon_name_str, addon_name, 260);
			wcstombs(author_str, author, 260);
			wcstombs(description_str, description, 260);
			wcstombs(version_str, version, 260);
			sq_newtable(v);
			sq_pushstring(v, "name", -1);
			sq_pushstring(v, name, -1);
			sq_newslot(v, -3, 0);
			sq_pushstring(v, "author", -1);
			sq_pushstring(v, author_str, strlen(author_str));
			sq_newslot(v, -3, 0);
			sq_pushstring(v, "version", -1);
			sq_pushstring(v, version_str, strlen(version_str));
			sq_newslot(v, -3, 0);
			sq_pushstring(v, "description", -1);
			sq_pushstring(v, description_str, strlen(description_str));
			sq_newslot(v, -3, 0);
			sq_pushstring(v, "enabled", -1);
			sq_pushbool(vm, v, enabled);
			sq_newslot(v, -3, 0);
			sq_pushstring(v, "image", -1);
			sq_pushstring(v, image, strlen(image));
			sq_newslot(v, -3, 0);
			sq_arrayappend(v, -2);
			if (bIsVPK) {
				char* pSemi = strrchr(szModPath, ';');
				if (pSemi)
					V_strncpy(szModPath, pSemi + 1, 260);
				char tempFilename[260];
				snprintf(tempFilename, 260, "%s%s%c%s", szModPath, "addons", 92, "addoninfo.txt");
				// remove file
				remove_file(file_system, tempFilename, 0);
				snprintf(tempFilename, 260, "%s%s%c%s", szModPath, "addons", 92, "addonimage.jpg");
				// remove file
				remove_file(file_system, tempFilename, 0);
			}
		}
	}
	return 1;
}

void AddXp(HSQUIRRELVM v) {
	auto r1sqvm = GetServerVMPtr();
	SQInteger xp;
	sq_getinteger(r1sqvm, v, 1, &xp);
	auto player = sq_getentity(v, 2);

	if (player) {
		auto player_ptr = reinterpret_cast<__int64>(player);
		CPlayer__SetXPRebuild(player_ptr, xp);
	}
}

void SetGenSQ(HSQUIRRELVM v) {
	auto r1sqvm = GetServerVMPtr();
	SQInteger gen;
	sq_getinteger(r1sqvm, v, 1, &gen);
	auto player = sq_getentity(v, 2);

	if (player) {
		auto player_ptr = reinterpret_cast<__int64>(player);
		SetGen(player_ptr, gen);
	}
}

SQInteger Script_Server_SetActiveBurnCardIndex(HSQUIRRELVM v) {
	const void* player = sq_getentity(v, 2);
	if (!player) {
		return sq_throwerror(v, "player is null");
	}
	auto r1_vm = GetServerVMPtr();
	SQInteger index;
	sq_getinteger(r1_vm, v, -1, &index);

	if (player) {
		auto player_ptr = reinterpret_cast<__int64>(player);
		*(int*)(player_ptr + 0x1A14) = index;
	}

	return 1;
}

SQInteger Script_Server_GetActiveBurnCardIndex(HSQUIRRELVM v) {
	const void* player = sq_getentity(v, 2);
	if (!player) {
		return sq_throwerror(v, "player is null");
	}
	auto r1_vm = GetServerVMPtr();
	auto player_ptr = reinterpret_cast<__int64>(player);
	int value = *(int*)(player_ptr + 0x1A14);

	sq_pushinteger(r1_vm, v, value);

	return 1;
}

// Function to initialize all SQVM functions
bool GetSQVMFuncs() {
	static bool initialized = false;
	if (initialized) return true;
	auto engine = IsDedicatedServer() ? G_engine_ds : G_engine;
	g_pClientArray = (CBaseClient*)(engine + 0x2966340);
	g_pClientArrayDS = (CBaseClientDS*)(engine + 0x1C89C48);

#if BUILD_DEBUG
	if (!G_vscript) MessageBoxW(0, L"G_launcher is null in GetSQVMFuncs", L"ASSERT!!!", MB_ICONERROR | MB_OK);
#endif

	if (MH_CreateHook(reinterpret_cast<void*>((engine + (IsDedicatedServer() ? 0xAA4A0 : 0x14BB10))), &Hk_CHostState__State_GameShutdown, reinterpret_cast<void**>(&oGameShutDown)) != MH_OK) {
		Msg("Failed to hook CHostState__State_GameShutdown\n");
	}

	uintptr_t baseAddress = G_vscript;
	if (G_server) {
		if (MH_CreateHook(reinterpret_cast<void*>(G_server + 0x0050EA30), &CPlayer__SetXPRebuild, reinterpret_cast<void**>(&CPlayer__SetXPRebuildOrig)) != MH_OK) {
			Msg("Failed to hook CPlayer__SetXPRebuild\n");
		}
		if (MH_CreateHook(reinterpret_cast<void*>(G_server + 0x50E740), &Script_XPChanged_Rebuild, reinterpret_cast<void**>(&CPlayer__Script_XP_ChangedOrig)) != MH_OK) {
			Msg("Failed to hook CPlayer__Script_XP_ChangedHook\n");
		}
		if (MH_CreateHook(reinterpret_cast<void*>(G_server + 0x50E7A0), &Script_GenChanged_Rebuild, reinterpret_cast<void**>(&CPlayer__Script_Gen_Changed_Orig)) != MH_OK) {
			Msg("Failed to hook CPlayer__Script_XP_ChangedHook\n");
		}
		MH_EnableHook(MH_ALL_HOOKS);
	}
	sq_compile = reinterpret_cast<sq_compile_t>(baseAddress + (IsDedicatedServer() ? 0x14A50 : 0x14970));
	sq_compilebuffer = reinterpret_cast<sq_compilebuffer_t>(baseAddress + (IsDedicatedServer() ? 0x1A6C0 : 0x1A5E0));
	base_getroottable = reinterpret_cast<base_getroottable_t>(baseAddress + (IsDedicatedServer() ? 0x56520 : 0x56440));
	sq_call = reinterpret_cast<sq_call_t>(baseAddress + (IsDedicatedServer() ? 0x18D20 : 0x18C40));
	sq_newslot = reinterpret_cast<sq_newslot_t>(baseAddress + (IsDedicatedServer() ? 0x17340 : 0x17260));
	SQVM_Pop = reinterpret_cast<SQVM_Pop_t>(baseAddress + (IsDedicatedServer() ? 0x2BD40 : 0x2BC60));
	sq_push = reinterpret_cast<sq_push_t>(baseAddress + (IsDedicatedServer() ? 0x166C0 : 0x165E0));
	SQVM_Raise_Error = reinterpret_cast<SQVM_Raise_Error_t>(baseAddress + (IsDedicatedServer() ? 0x41290 : 0x411B0));
	IdType2Name = reinterpret_cast<IdType2Name_t>(baseAddress + (IsDedicatedServer() ? 0x3C740 : 0x3C660));
	sq_getstring = reinterpret_cast<sq_getstring_t>(baseAddress + (IsDedicatedServer() ? 0x16960 : 0x16880));
	sq_getinteger = reinterpret_cast<sq_getinteger_t>(baseAddress + (IsDedicatedServer() ? 0xE760 : 0xE740));
	sq_getfloat = reinterpret_cast<sq_getfloat_t>(baseAddress + (IsDedicatedServer() ? 0xE710 : 0xE6F0));
	sq_getbool = reinterpret_cast<sq_getbool_t>(baseAddress + (IsDedicatedServer() ? 0xE6D0 : 0xE6B0));
	sq_pushnull = reinterpret_cast<sq_pushnull_t>(baseAddress + (IsDedicatedServer() ? 0x14CB0 : 0x14BD0));
	sq_pushstring = reinterpret_cast<sq_pushstring_t>(baseAddress + (IsDedicatedServer() ? 0x14D10 : 0x14C30));
	sq_pushinteger = reinterpret_cast<sq_pushinteger_t>(baseAddress + (IsDedicatedServer() ? 0xEA30 : 0xEA10));
	sq_pushfloat = reinterpret_cast<sq_pushfloat_t>(baseAddress + (IsDedicatedServer() ? 0xE9D0 : 0xE9B0));
	sq_pushbool = reinterpret_cast<sq_pushbool_t>(baseAddress + (IsDedicatedServer() ? 0xE950 : 0xE930));
	sq_tostring = reinterpret_cast<sq_tostring_t>(baseAddress + (IsDedicatedServer() ? 0x16770 : 0x16690));
	sq_getsize = reinterpret_cast<sq_getsize_t>(baseAddress + (IsDedicatedServer() ? 0xE7B0 : 0xE790));
	sq_gettype = reinterpret_cast<sq_gettype_t>(baseAddress + (IsDedicatedServer() ? 0x16740 : 0x16660));
	sq_getstackobj = reinterpret_cast<sq_getstackobj_t>(baseAddress + (IsDedicatedServer() ? 0xE810 : 0xE7F0));
	sq_get = reinterpret_cast<sq_get_t>(baseAddress + (IsDedicatedServer() ? 0x18230 : 0x17F10));
	sq_get_noerr = reinterpret_cast<sq_get_noerr_t>(baseAddress + (IsDedicatedServer() ? 0x17FF0 : 0x18150));
	sq_gettop = reinterpret_cast<sq_gettop_t>(baseAddress + (IsDedicatedServer() ? 0xE850 : 0xE830));
	sq_newtable = reinterpret_cast<sq_newtable_t>(baseAddress + (IsDedicatedServer() ? 0x15010 : 0x14F30));
	//0x26550
	sq_next = reinterpret_cast<sq_next_t>(baseAddress + (IsDedicatedServer() ? 0x1A290 : 0x1A1B0));
	sq_getinstanceup = reinterpret_cast<sq_getinstanceup_t>(baseAddress + (IsDedicatedServer() ? 0x6770 : 0x6750));
	sq_newarray = reinterpret_cast<sq_newarray_t>(baseAddress + (IsDedicatedServer() ? 0x15090 : 0x14FB0));
	sq_arrayappend = reinterpret_cast<sq_arrayappend_t>(baseAddress + (IsDedicatedServer() ? 0x15380 : 0x152A0));
	sq_throwerror = reinterpret_cast<sq_throwerror_t>(baseAddress + (IsDedicatedServer() ? 0x18A10 : 0x18930));
	RunCallback = reinterpret_cast<RunCallback_t>(baseAddress + (IsDedicatedServer() ? 0x89C0 : 0x89A0));
	CSquirrelVM__RegisterGlobalConstantInt = reinterpret_cast<CSquirrelVM__RegisterGlobalConstantInt_t>(baseAddress + (IsDedicatedServer() ? 0xA6A0 : 0xA680));
	CSquirrelVM__GetEntityFromInstance = reinterpret_cast<CSquirrelVM__GetEntityFromInstance_t>(baseAddress + (IsDedicatedServer() ? 0x9950 : 0x9930));
	AddSquirrelReg = reinterpret_cast<AddSquirrelReg_t>(baseAddress + (IsDedicatedServer() ? 0x8E70 : 0x8E50));
	sq_GetEntityConstant_CBaseEntity = reinterpret_cast<sq_GetEntityConstant_CBaseEntity_t>(G_client + 0x2EF850);

	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_CLIENT,
		"GetPersistentString",
		(SQFUNCTION)Script_ClientGetPersistentData,
		".ss", // String
		3,      // Expects 2 parameters
		"string",    // Returns a string
		"str",
		"Get a persistent data value"
	);

	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_UI,
		"GetModPath",
		(SQFUNCTION)GetMods,
		".ss", // String
		1,      // Expects 2 parameters
		"string",    // Returns a string
		"str",
		"Get a persistent data value"
	);

	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_UI,
		"GetPersistentString",
		(SQFUNCTION)Script_ClientGetPersistentData,
		".ss", // String
		3,      // Expects 2 parameters
		"string",    // Returns a string
		"str",
		"Get a persistent data value"
	);

	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_SERVER,
		"XPChanged",
		(SQFUNCTION)Script_XPChanged_Rebuild,
		"", // String
		2,      // Expects 2 parameters
		"void",    // Returns a string
		"",
		"Updates xp value from persistent vars"
	);

	//REGISTER_SCRIPT_FUNCTION(
	//	SCRIPT_CONTEXT_SERVER,
	//	"GenChanged",
	//	(SQFUNCTION)Script_GenChanged_Rebuild,
	//	"I", // String
	//	3,      // Expects 2 parameters
	//	"void",    // Returns a string
	//	"",
	//	"Updates gen value from persistent vars"
	//);

	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_UI,
		"UpdateAddons",
		(SQFUNCTION)UpdateAddons,
		".ib", // String
		3,      // Expects 2 parameters
		"int",    // Returns a string
		"int index,bool enabled",
		"Updates the selected addons"
	);

	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_UI,
		"GetServerList",
		(SQFUNCTION)GetServerList,
		".ib", // String
		1,      // Expects 2 parameters
		"void",    // Returns a string
		"str",
		"Gets server list"
	);

	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_SERVER,
		"SendDataToCppServer",
		(SQFUNCTION)GetServerHeartbeat,
		".t",
		2,
		"void",
		"str",
		"Send data to the cpp server"
	);

	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_SERVER,
		"AddXpServer",
		(SQFUNCTION)AddXp,
		".Ii",
		3,
		"void",
		"int",
		"Add XP"
	);

	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_SERVER,
		"SetGen",
		(SQFUNCTION)SetGenSQ,
		".Ii",
		3,
		"void",
		"int",
		"Set player gen"
	);

	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_SERVER, // Available in client script contexts
		"GetPersistentStringForClient",
		Script_ServerGetPersistentUserDataKVString,
		"..ss", // String
		4,      // Expects 3 parameters
		"string",    // Returns an int (idk if i is the right char for this lmao)
		"str",
		"Get a persistent userinfo value"
	);

	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_SERVER, // Available in client script contexts
		"SetActiveBurnCardIndexForPlayer",
		Script_Server_SetActiveBurnCardIndex,
		".Ii", // String
		3,      // Expects 2 parameters
		"void",    // Returns an int (idk if i is the right char for this lmao)
		"str",
		"Get a persistent userinfo value"
	);

	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_SERVER, // Available in client script contexts
		"GetActiveBurnCardIndex",
		Script_Server_GetActiveBurnCardIndex,
		".I", // String
		2,      // Expects 2 parameters
		"int",    // Returns an int (idk if i is the right char for this lmao)
		"str",
		"Get a persistent userinfo value"
	);

	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_SERVER, // Available in client script contexts
		"SetPersistentStringForClient",
		Script_ServerSetPersistentUserDataKVString,
		"..ss", // String
		4,      // Expects 3 parameters
		"string",    // Returns an int (idk if i is the right char for this lmao)
		"str",
		"Set a persistent userinfo value (this does NOT replicate you will need to send the replication command)"
	);
	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_UI, // Available in client script contexts
		"SquirrelNativeFunctionTest", (SQFUNCTION)SquirrelNativeFunctionTest, ".sifb", 0, "string", "string text, int a2, float a3, bool a4", "Test registering and calling native function in Squirrel.");

	initialized = true;
	return true;
}

typedef __int64 (*CScriptVM__ctortype)(void* thisptr);
CScriptVM__ctortype CScriptVM__ctororiginal;
bool scriptflag = false;
void __fastcall CScriptVM__SetTFOFlag(__int64 a1, char a2) {
	scriptflag = a2;
}
char __fastcall CScriptVM__GetTFOFlag(__int64 a1) {
	return scriptflag;
}
// Prototype for the function to update the vtable pointer of a CScriptVM object.
void SetNewVTable(void* thisptr, uintptr_t* newVTable);

constexpr size_t SQUIRREL_VM_VMT_SIZE = 122;

// Implementation for creating a new vtable and inserting functions.
void* CreateNewVTable(void* thisptr) {
	// Original vtable can be obtained by dereferencing the this pointer.
	uintptr_t* originalVTable = *(uintptr_t**)thisptr;

	// Allocate memory for the new vtable with 122 entries (original 120 + 2 new).
	uintptr_t* newVTable = new uintptr_t[SQUIRREL_VM_VMT_SIZE];

	// Copy the original vtable entries into the new vtable.
	for (int i = 0; i < 120; ++i) {
		newVTable[i] = CreateFunction((void*)originalVTable[i], thisptr);
	}

	// Insert CScriptVM__SetTFOFlag and CScriptVM__GetTFOFlag between the 4th and 5th functions.
	// This involves shifting functions starting from the 4th position (index 3) to the right by 2 positions.
	for (int i = 119; i >= 3; --i) {
		newVTable[i + 2] = newVTable[i];
	}

	// Now, insert the new functions into the shifted positions.
	newVTable[3] = CreateFunction((void*)CScriptVM__SetTFOFlag, thisptr);
	newVTable[4] = CreateFunction((void*)CScriptVM__GetTFOFlag, thisptr);

	// Update the vtable pointer of the CScriptVM object to the new vtable.
	return newVTable;
}

void* lastvmptr = 0;
void* fakevmptr;
void* realvmptr = 0;
typedef void* (*CScriptManager__CreateNewVMType)(__int64 a1, int a2, unsigned int a3);
CScriptManager__CreateNewVMType CScriptManager__CreateNewVMOriginal;
bool isServerScriptVM = false;

void* CScriptManager__CreateNewVM(__int64 a1, int a2, unsigned int a3) {
	// Call the original function to maintain existing functionality
	void* vmPtr = CScriptManager__CreateNewVMOriginal(a1, a2, a3);

	// Check if VM creation was successful
	if (vmPtr == nullptr) {
		// Handle error: VM creation failed
		return nullptr;
	}

	// Determine the script context
	ScriptContext context;
	switch (a3) {
	case 0:
		context = SCRIPT_CONTEXT_SERVER;
		break;
	case 1:
		context = SCRIPT_CONTEXT_CLIENT;
		break;
	case 2:
		context = SCRIPT_CONTEXT_UI;
		break;
	default:
		// Handle unknown context
		return vmPtr;
	}
	GetSQVMFuncs();

	// Register our custom functions for the appropriate context
	if (AddSquirrelReg != nullptr) {
		ScriptFunctionRegistry::getInstance().registerFunctions(vmPtr, context);
	}
	else {
		// Handle error: AddSquirrelReg function not found
		// You might want to log this error
	}

	// Original functionality for server VM
	if (context == SCRIPT_CONTEXT_SERVER) {
		realvmptr = vmPtr;
		if (!lastvmptr) {
			fakevmptr = CreateNewVTable(vmPtr);
			lastvmptr = vmPtr;
		}
		else if (vmPtr != lastvmptr) {
			lastvmptr = vmPtr;
			for (size_t i = 0; i < SQUIRREL_VM_VMT_SIZE; i++) {
				UpdateRWXFunction(((void**)fakevmptr)[i], vmPtr);
			}
		}

		// Return the fake VM pointer for server context
		return &fakevmptr;
	}

	return vmPtr;
}

typedef void* (*CScriptVM__GetUnknownVMPtrType)();
CScriptVM__GetUnknownVMPtrType CScriptVM__GetUnknownVMPtrOriginal;
BOOL IsReturnAddressInServerDll(void* returnAddress) {
	HMODULE module2;
	BOOL check1 = GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCTSTR)returnAddress, &module2);
	BOOL check2 = module2 == (HMODULE)G_server;
	return check1 && check2;
}

void* CScriptVM__GetUnknownVMPtr() {
	if (IsReturnAddressInServerDll(_ReturnAddress())) {
		///std::cout << "returning addr to Server SCRIPT VM" << std::endl;
		return &fakevmptr;
	}
	return CScriptVM__GetUnknownVMPtrOriginal();
}
__int64 __fastcall CScriptVM__ctor(void* thisptr) {
	__int64 ret = CScriptVM__ctororiginal(thisptr);
	//if (isServerScriptVM)
	//	realvmptr = thisptr;
	return ret;
}

void ConvertScriptVariant(ScriptVariant_t* variant, ConversionDirection direction) {
	// Check if attempting to convert in the opposite direction of a previous conversion
	if ((direction == R1_TO_R1O && (variant->m_flags & SV_CONVERTED_TO_R1)) ||
		(direction == R1O_TO_R1 && (variant->m_flags & SV_CONVERTED_TO_R1O))) {
		// Clear the opposite flag
		variant->m_flags &= ~(direction == R1_TO_R1O ? SV_CONVERTED_TO_R1 : SV_CONVERTED_TO_R1O);
	}
	else if ((direction == R1_TO_R1O && (variant->m_flags & SV_CONVERTED_TO_R1O)) ||
		(direction == R1O_TO_R1 && (variant->m_flags & SV_CONVERTED_TO_R1))) {
		// If already converted in this direction, exit to prevent double conversion
		return;
	}

	if (variant->m_type > 5) {
		if (direction == R1_TO_R1O) {
			variant->m_type += 1;
			variant->m_flags |= SV_CONVERTED_TO_R1O; // Set the flag for R1 to R1O conversion
			//std::cout << "ConvertScriptVariant: converted variant " << static_cast<void*>(variant) << " to SV_CONVERTED_TO_R1O" << std::endl;
		}
		else {
			variant->m_type -= 1;
			variant->m_flags |= SV_CONVERTED_TO_R1; // Set the flag for R1O to R1 conversion
			//std::cout << "ConvertScriptVariant: converted variant " << static_cast<void*>(variant) << " to SV_CONVERTED_TO_R1" << std::endl;
		}
	}
}

// Function to check if server.dll is in the call stack
// TODO(mrsteyk): performance
__forceinline bool serverRunning(void* a1) {
	//return isServerScriptVM || a1 == realvmptr || a1 == fakevmptr || (realvmptr && a1 == *(void**)(((uintptr_t)realvmptr + 8)));
	if (isServerScriptVM || a1 == realvmptr || a1 == fakevmptr || (realvmptr && a1 == *(void**)(((uintptr_t)realvmptr + 8))))
		return true; // SQVM handle check

	// NOTE(mrsteyk): Server VM exists? If no, we can't be running server.
	if (!realvmptr)
		return false;

	const HMODULE serverDllBase = (HMODULE)G_server;
	const SIZE_T serverDllSize = 0xFB5000; // no comment
	constexpr size_t stack_size = 72;
	void* stack[stack_size];

	USHORT frames = CaptureStackBackTrace(0, stack_size, stack, NULL);

	for (USHORT i = 0; i < frames; i++) {
		if ((stack[i] >= serverDllBase) && ((ULONG_PTR)stack[i] < (ULONG_PTR)serverDllBase + serverDllSize)) {
			return TRUE;
		}
	}

	return FALSE;
}

const char* FieldTypeToString(int fieldType) {
	static const std::map<int, const char*> typeMapServerRunning = {
		{0, "void"}, {1, "float"}, {3, "Vector"}, {5, "int"},
		{7, "bool"}, {9, "char"}, {33, "string"}, {34, "handle"}
	};
	static const std::map<int, const char*> typeMapServerNotRunning = {
		{0, "void"}, {1, "float"}, {3, "Vector"}, {5, "int"},
		{6, "bool"}, {8, "char"}, {32, "string"}, {33, "handle"}
	};

	const auto& typeMap = serverRunning(NULL) ? typeMapServerRunning : typeMapServerNotRunning;
	auto it = typeMap.find(fieldType);
	if (it != typeMap.end()) {
		return it->second;
	}
	else {
		return "<unknown>";
	}
}
typedef void (*CSquirrelVM__RegisterFunctionGutsType)(__int64* a1, __int64 a2, const char** a3);
CSquirrelVM__RegisterFunctionGutsType CSquirrelVM__RegisterFunctionGutsOriginal;
typedef __int64 (*CSquirrelVM__PushVariantType)(__int64* a1, ScriptVariant_t* a2);
CSquirrelVM__PushVariantType CSquirrelVM__PushVariantOriginal;
typedef char (*CSquirrelVM__ConvertToVariantType)(__int64* a1, __int64 a2, ScriptVariant_t* a3);
CSquirrelVM__ConvertToVariantType CSquirrelVM__ConvertToVariantOriginal;
typedef __int64 (*CSquirrelVM__ReleaseValueType)(__int64* a1, ScriptVariant_t* a2);
CSquirrelVM__ReleaseValueType CSquirrelVM__ReleaseValueOriginal;
typedef bool (*CSquirrelVM__SetValueType)(__int64* a1, void* a2, unsigned int a3, ScriptVariant_t* a4);
CSquirrelVM__SetValueType CSquirrelVM__SetValueOriginal;
typedef bool (*CSquirrelVM__SetValueExType)(__int64* a1, __int64 a2, const char* a3, ScriptVariant_t* a4);
CSquirrelVM__SetValueExType CSquirrelVM__SetValueExOriginal;
typedef __int64 (*CSquirrelVM__TranslateCallType)(__int64* a1);
CSquirrelVM__TranslateCallType CSquirrelVM__TranslateCallOriginal;
bool IsPointerFromServerDll(void* pointer) {
	// Get the base address of "server.dll"
	HMODULE hModule = (HMODULE)G_server;
	if (!hModule) {
		std::cerr << "Failed to get handle of server.dll\n";
		return false;
	}

	// Convert the HMODULE to a pointer for comparison
	uintptr_t baseAddress = reinterpret_cast<uintptr_t>(hModule);
	uintptr_t ptrAddress = reinterpret_cast<uintptr_t>(pointer);

	// Size of "server.dll" is 0xFB5000
	const uintptr_t moduleSize = 0xFB5000;

	// Check if the pointer is within the range of "server.dll"
	return ptrAddress >= baseAddress && ptrAddress < (baseAddress + moduleSize);
}
bool hasRegisteredServerFuncs = false;
void __fastcall CSquirrelVM__RegisterFunctionGuts(__int64* a1, __int64 a2, const char** a3) {
	//std::cout << "RegisterFunctionGuts called, server: " << (serverRunning ? "TRUE" : "FALSE") << std::endl;

	if (serverRunning(a1) && (*(_DWORD*)(a2 + 112) & 2) == 0 && (*(_DWORD*)(a2 + 112) & 16) == 0) { // Check if server is running
		int argCount = *(_DWORD*)(a2 + 88); // Get the argument count
		_DWORD* args = *(_DWORD**)(a2 + 64); // Get the pointer to arguments
		*(_DWORD*)(a2 + 112) |= 16;
		for (int i = 0; i < argCount; ++i) {
			if (args[i] > 5) {
				args[i] -= 1; // Subtract 1 from argument values above 5
				//std::cout << "subtracted 1" << std::endl;
			}
		}
	}
	/*
	LPCVOID baseAddressDll = (LPCVOID)G_vscript;
	LPCVOID address1 = (LPCVOID)((uintptr_t)(baseAddressDll)+0xCE27);
	LPCVOID address2 = (LPCVOID)((uintptr_t)(baseAddressDll)+0xD3C0);
	char value1 = 0x22;
	char data1[] = { 0x00, 0x05, 0x01, 0x05, 0x00, 0x05, 0x02, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x03, 0x04, 0x04 };
	WriteProcessMemory(GetCurrentProcess(), (LPVOID)address1, &value1, 1, NULL);
	WriteProcessMemory(GetCurrentProcess(), (LPVOID)address2, data1, sizeof(data1), NULL);
	//std::cout << __FUNCTION__ ": translated call" << std::endl;
	CSquirrelVM__RegisterFunctionGutsOriginal(a1, a2, a3);
	char value2 = 0x21;
	char data2[] = { 0x00, 0x05, 0x01, 0x05, 0x00, 0x02, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x03, 0x04, 0x04 };
	WriteProcessMemory(GetCurrentProcess(), (LPVOID)address1, &value2, 1, NULL);
	WriteProcessMemory(GetCurrentProcess(), (LPVOID)address2, data2, sizeof(data2), NULL);
	*/
	//
	CSquirrelVM__RegisterFunctionGutsOriginal(a1, a2, a3);
}
void __fastcall CSquirrelVM__TranslateCall(__int64* a1) {
	auto vscript = G_vscript;
	LPVOID address1 = (LPVOID)(vscript + (IsDedicatedServer() ? 0xc3cf : 0xC3AF));
	LPVOID address2 = (LPVOID)(vscript + (IsDedicatedServer() ? 0xc7e4 : 0xC7C4));

	char value1 = 0x21;
	char data1[] = {0x00, 0x07, 0x01, 0x07, 0x02, 0x07, 0x03, 0x07, 0x04, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x05, 0x06};
	char value2 = 0x20;
	char data2[] = {0x00, 0x07, 0x01, 0x07, 0x02, 0x03, 0x07, 0x04, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x05, 0x06};

	DWORD oldProtect;
	static bool done = false;
	if (!done) {
		done = true;
		VirtualProtect(address1, 1, PAGE_EXECUTE_READWRITE, &oldProtect);
		VirtualProtect(address2, sizeof(data1), PAGE_EXECUTE_READWRITE, &oldProtect);
	}

	if (!serverRunning(a1)) {
		*(char*)address1 = value2;
		memcpy(address2, data2, sizeof(data2));
		CSquirrelVM__TranslateCallOriginal(a1);
		return;
	}

	*(char*)address1 = value1;
	memcpy(address2, data1, sizeof(data1));
	CSquirrelVM__TranslateCallOriginal(a1);
}

__int64 __fastcall CSquirrelVM__PushVariant(__int64* a1, ScriptVariant_t* a2) {
	if (serverRunning(a1)) {
		ConvertScriptVariant(a2, R1O_TO_R1);
		//std::cout << __FUNCTION__ ": converted variant" << std::endl;
	}
	else {
		//std::cout << __FUNCTION__ ": did not convert variant" << std::endl;
	}
	return CSquirrelVM__PushVariantOriginal(a1, a2);
}

char __fastcall CSquirrelVM__ConvertToVariant(__int64* a1, __int64 a2, ScriptVariant_t* a3) {
	bool ret = CSquirrelVM__ConvertToVariantOriginal(a1, a2, a3);
	if (serverRunning(a1))
		ConvertScriptVariant(a3, R1_TO_R1O);
	return ret;
}
__int64 __fastcall CSquirrelVM__ReleaseValue(__int64* a1, ScriptVariant_t* a2) {
	if (serverRunning(a1))
		ConvertScriptVariant(a2, R1O_TO_R1);
	return CSquirrelVM__ReleaseValueOriginal(a1, a2);
}
bool __fastcall CSquirrelVM__SetValue(__int64* a1, void* a2, unsigned int a3, ScriptVariant_t* a4) {
	if (serverRunning(a1))
		ConvertScriptVariant(a4, R1O_TO_R1);
	return CSquirrelVM__SetValueOriginal(a1, a2, a3, a4);
}

bool __fastcall CSquirrelVM__SetValueEx(__int64* a1, __int64 a2, const char* a3, ScriptVariant_t* a4) {
	if (serverRunning(a1))
		ConvertScriptVariant(a4, R1O_TO_R1);
	return CSquirrelVM__SetValueExOriginal(a1, a2, a3, a4);
}
typedef void (*CScriptManager__DestroyVMType)(void* a1, void* vmptr);
CScriptManager__DestroyVMType CScriptManager__DestroyVMOriginal;

__declspec(dllexport) R1SquirrelVM* GetServerVMPtr() {
	return (R1SquirrelVM*)(realvmptr);
}
void __fastcall CScriptManager__DestroyVM(void* a1, void* vmptr) {
	//if (serverRunning(a1) || serverRunning(vmptr) || serverRunning(*(void**)vmptr) || serverRunning(*(void**)a1)) {
	//	vmptr = realvmptr;
	//	//std::cout << "set vm ptr" << std::endl;
	//}
	//else {
	//	//std::cout << "did NOT set vm ptr" << std::endl;
	//}
	if (*((void**)vmptr) == fakevmptr) {
		vmptr = realvmptr;
		realvmptr = 0;
		hasRegisteredServerFuncs = true;
	}
	return CScriptManager__DestroyVMOriginal(a1, vmptr);
}
// Track incomplete lines for each VM type
static bool g_serverLineIncomplete = false;
static bool g_clientLineIncomplete = false;
static bool g_uiLineIncomplete = false;

void CSquirrelVM__PrintFunc1(void* m_hVM, const char* s, ...) {
	char string[2048];
	va_list params;

	va_start(params, s);
	vsnprintf(string, 2048, s, params);

	// Check if string ends with newline
	size_t len = strlen(string);
	bool endsWithNewline = (len > 0 && string[len - 1] == '\n');

	if (!g_serverLineIncomplete) {
		Msg("[SERVER SCRIPT] %s", string);
	}
	else {
		Msg("%s", string);
	}

	g_serverLineIncomplete = !endsWithNewline;
	va_end(params);
}

void CSquirrelVM__PrintFunc2(void* m_hVM, const char* s, ...) {
	char string[2048];
	va_list params;

	va_start(params, s);
	vsnprintf(string, 2048, s, params);

	size_t len = strlen(string);
	bool endsWithNewline = (len > 0 && string[len - 1] == '\n');

	if (!g_clientLineIncomplete) {
		Msg("[CLIENT SCRIPT] %s", string);
	}
	else {
		Msg("%s", string);
	}

	g_clientLineIncomplete = !endsWithNewline;
	va_end(params);
}

void CSquirrelVM__PrintFunc3(void* m_hVM, const char* s, ...) {
	char string[2048];
	va_list params;

	va_start(params, s);
	vsnprintf(string, 2048, s, params);

	size_t len = strlen(string);
	bool endsWithNewline = (len > 0 && string[len - 1] == '\n');

	if (!g_uiLineIncomplete) {
		Msg("[UI SCRIPT] %s", string);
	}
	else {
		Msg("%s", string);
	}

	g_uiLineIncomplete = !endsWithNewline;
	va_end(params);
}
R1SquirrelVM* GetClientVMPtr() {
	return *(R1SquirrelVM**)(G_client + 0x16BBE78);
}
R1SquirrelVM* GetUIVMPtr() {
	return *(R1SquirrelVM**)(G_client + 0x16C1FA8);
}
using SQCompileBufferFn = SQRESULT(*)(HSQUIRRELVM, const SQChar*, SQInteger, const SQChar*, SQBool);
using BaseGetRootTableFn = __int64(*)(HSQUIRRELVM);
using SQCallFn = SQRESULT(*)(HSQUIRRELVM, SQInteger, SQBool, SQBool);

void run_script(const CCommand& args, R1SquirrelVM* (*GetVMPtr)()) {
	auto launcher = G_launcher;
	SQCompileBufferFn sq_compilebuffer = reinterpret_cast<SQCompileBufferFn>(launcher + (IsDedicatedServer() ? 0x1A6C0 : 0x1A5E0));
	BaseGetRootTableFn base_getroottable = reinterpret_cast<BaseGetRootTableFn>(launcher + (IsDedicatedServer() ? 0x56520 : 0x56440));
	SQCallFn sq_call = reinterpret_cast<SQCallFn>(launcher + (IsDedicatedServer() ? 0x18D20 : 0x18C40));

	std::string code = args.ArgS();
	R1SquirrelVM* vm = GetVMPtr();
	if (!vm) {
		Warning("Can't run script code on a VM when that VM is not present.");
		return;
	}

	SQRESULT compileRes = sq_compilebuffer(vm->sqvm, code.c_str(), static_cast<SQInteger>(code.length()), "console", 1);
	if (SQ_SUCCEEDED(compileRes)) {
		base_getroottable(vm->sqvm);
		SQRESULT callRes = sq_call(vm->sqvm, 1, SQFalse, SQFalse);
	}
}

void script_cmd(const CCommand& args) {
	run_script(args, GetServerVMPtr);
}

void script_client_cmd(const CCommand& args) {
	run_script(args, GetClientVMPtr);
}

void script_ui_cmd(const CCommand& args) {
	run_script(args, GetUIVMPtr);
}

//__int64 __fastcall sq_throwerrorhook(__int64 a1, const char* a2)
//{
//	return 0;
//	//if (a2)
//	//	Warning("sq_throwerror: %s\n", a2);
//	//return sq_throwerror(a1, a2);
//}