#pragma once

#include "core.h"

#include "load.h"
#include <cctype>
#include <cstring>
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
#include <atomic>
#include <intrin.h>
#include "memory.h"
#include "filesystem.h"
#include "filecache.h"
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
#include "ffa_targeting.h"
#include "script_variant_abi.h"

#include <random>
#include "masterserver.h"
#include "script_error_telemetry.h"
#include "shellapi.h"
#include "discord.h"
#include "r1d_version.h"
#include "client.h"
#include "surfacerender.h"
#include "localize.h"

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
		for (const auto& func : m_functions) {
			if (func->GetContext() == context) {
				AddSquirrelReg(reinterpret_cast<R1SquirrelVM*>(vmPtr), func->GetInternalReg());
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
typedef SQRESULT(*sq_throwerror_t)(HSQUIRRELVM, const char *err);
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
SQVM_Pop_t sq_pop;
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
sq_removetwo_t sq_removetwo;
RunCallback_t RunCallback;
CScriptManager__CreateNewVMType CScriptManager__CreateNewVMOriginal;

void CSquirrelVM__PrintFunc1(void* m_hVM, const char* s, ...);
void CSquirrelVM__PrintFunc2(void* m_hVM, const char* s, ...);
void CSquirrelVM__PrintFunc3(void* m_hVM, const char* s, ...);

namespace {

using TfoAddSquirrelReg_t = int64_t(__fastcall*)(void*, void*, const char**);
static TfoAddSquirrelReg_t g_R1OTfoAddSquirrelReg = nullptr;

struct R1OTfoSQFuncRegistrationInternal {
	const char* squirrelFuncName;
	const char* cppFuncName;
	const char* helpText;
	const char* szTypeMask;
	int nparamscheck;
	int pad24;
	const char* returnValueTypeText;
	const char* argNamesText;
	__int64 unk38;
	char pad40[0x20];
	void* pfnBinding;
	void* pFunction;
	__int64 flags;
};
static_assert(offsetof(R1OTfoSQFuncRegistrationInternal, pfnBinding) == 0x60);
static_assert(offsetof(R1OTfoSQFuncRegistrationInternal, pFunction) == 0x68);
static_assert(offsetof(R1OTfoSQFuncRegistrationInternal, flags) == 0x70);

static std::vector<std::pair<SQFuncRegistrationInternal*, std::unique_ptr<R1OTfoSQFuncRegistrationInternal>>> g_R1OTfoRegAdapters;

__int64 __fastcall R1OTfoSQNativeBinding(__int64 pFunction, __int64, __int64* args, __int64, __int64* ret)
{
	SQInteger result = 0;
	if (pFunction && args)
		result = reinterpret_cast<SQFUNCTION>(pFunction)(reinterpret_cast<HSQUIRRELVM>(args[0]));
	if (ret)
		*ret = result;
	return result;
}

R1OTfoSQFuncRegistrationInternal* GetR1OTfoRegAdapter(SQFuncRegistrationInternal* reg)
{
	for (auto& entry : g_R1OTfoRegAdapters) {
		if (entry.first == reg)
			return entry.second.get();
	}

	auto adapter = std::make_unique<R1OTfoSQFuncRegistrationInternal>();
	memset(adapter.get(), 0, sizeof(*adapter));
	adapter->squirrelFuncName = reg->squirrelFuncName;
	adapter->cppFuncName = reg->cppFuncName;
	adapter->helpText = reg->helpText;
	adapter->szTypeMask = reg->szTypeMask;
	adapter->nparamscheck = static_cast<int>(reg->nparamscheck_probably);
	adapter->returnValueTypeText = reg->returnValueTypeText;
	adapter->argNamesText = reg->argNamesText;
	adapter->unk38 = reg->UnkSeemsToAlwaysBe32;
	adapter->pfnBinding = reinterpret_cast<void*>(&R1OTfoSQNativeBinding);
	adapter->pFunction = reg->pFunction;
	adapter->flags = 2;
	R1OTfoSQFuncRegistrationInternal* result = adapter.get();
	g_R1OTfoRegAdapters.emplace_back(reg, std::move(adapter));
	return result;
}

int64_t __fastcall R1OTfoAddSquirrelRegWrapper(R1SquirrelVM* vm, SQFuncRegistrationInternal* reg)
{
	if (!g_R1OTfoAddSquirrelReg || !vm || !reg)
		return 0;
	auto adapter = GetR1OTfoRegAdapter(reg);
	return g_R1OTfoAddSquirrelReg(vm, adapter, nullptr);
}

SQInteger R1OTfo_sq_gettop(R1SquirrelVM*, HSQUIRRELVM v)
{
	if (!v)
		return 0;
	auto base = reinterpret_cast<const unsigned char*>(v);
	return *reinterpret_cast<const int*>(base + 0x50) - *reinterpret_cast<const int*>(base + 0x54);
}

SQRESULT R1OTfo_sq_getstackobj(R1SquirrelVM*, HSQUIRRELVM v, SQInteger idx, SQObject* out)
{
	if (!v || !out)
		return SQ_ERROR;
	auto base = reinterpret_cast<const unsigned char*>(v);
	auto stack = *reinterpret_cast<const unsigned char* const*>(base + 0x30);
	const int top = *reinterpret_cast<const int*>(base + 0x50);
	const int stackBase = *reinterpret_cast<const int*>(base + 0x54);
	const int slot = idx < 0 ? top + idx : stackBase + idx - 1;
	if (!stack || slot < 0 || slot >= top)
		return SQ_ERROR;
	memcpy(out, stack + (static_cast<size_t>(slot) * sizeof(SQObject)), sizeof(SQObject));
	return SQ_OK;
}

SQObjectType R1OTfo_sq_gettype(HSQUIRRELVM v, SQInteger idx)
{
	SQObject obj;
	if (SQ_FAILED(R1OTfo_sq_getstackobj(nullptr, v, idx, &obj)))
		return OT_NULL;
	return obj._type;
}

SQRESULT R1OTfo_sq_getstring(HSQUIRRELVM v, SQInteger idx, const SQChar** out)
{
	if (!out)
		return SQ_ERROR;
	SQObject obj;
	if (SQ_FAILED(R1OTfo_sq_getstackobj(nullptr, v, idx, &obj)) || obj._type != OT_STRING || !obj._unVal.pString)
		return SQ_ERROR;

	// TFO SQString layout matches the old launcher string pool: characters begin at +0x38.
	*out = reinterpret_cast<const SQChar*>(reinterpret_cast<const unsigned char*>(obj._unVal.pString) + 0x38);
	return SQ_OK;
}

SQRESULT R1OTfo_sq_getinteger(R1SquirrelVM*, HSQUIRRELVM v, SQInteger idx, SQInteger* out)
{
	if (!out)
		return SQ_ERROR;
	SQObject obj;
	if (SQ_FAILED(R1OTfo_sq_getstackobj(nullptr, v, idx, &obj)) || (obj._type & SQOBJECT_NUMERIC) == 0)
		return SQ_ERROR;
	*out = obj._type == OT_FLOAT ? static_cast<SQInteger>(obj._unVal.fFloat) : obj._unVal.nInteger;
	return SQ_OK;
}

SQRESULT R1OTfo_sq_getfloat(R1SquirrelVM*, HSQUIRRELVM v, SQInteger idx, SQFloat* out)
{
	if (!out)
		return SQ_ERROR;
	SQObject obj;
	if (SQ_FAILED(R1OTfo_sq_getstackobj(nullptr, v, idx, &obj)) || (obj._type & SQOBJECT_NUMERIC) == 0)
		return SQ_ERROR;
	*out = obj._type == OT_FLOAT ? obj._unVal.fFloat : static_cast<SQFloat>(obj._unVal.nInteger);
	return SQ_OK;
}

SQRESULT R1OTfo_sq_getbool(R1SquirrelVM*, HSQUIRRELVM v, SQInteger idx, SQBool* out)
{
	if (!out)
		return SQ_ERROR;
	SQObject obj;
	if (SQ_FAILED(R1OTfo_sq_getstackobj(nullptr, v, idx, &obj)) || obj._type != OT_BOOL)
		return SQ_ERROR;
	*out = static_cast<SQBool>(obj._unVal.nInteger != 0);
	return SQ_OK;
}

SQInteger R1OTfo_sq_getsize(R1SquirrelVM*, HSQUIRRELVM v, SQInteger idx)
{
	SQObject obj;
	if (SQ_FAILED(R1OTfo_sq_getstackobj(nullptr, v, idx, &obj)))
		return SQ_ERROR;
	if (obj._type == OT_STRING && obj._unVal.pString)
		return *reinterpret_cast<const int*>(reinterpret_cast<const unsigned char*>(obj._unVal.pString) + 0x28);
	return SQ_ERROR;
}

uintptr_t g_R1OTfoLauncherBase = 0;

using TfoSqPushNull_t = void(__fastcall*)(HSQUIRRELVM);
using TfoSqPushString_t = void(__fastcall*)(HSQUIRRELVM, const SQChar*, SQInteger);
using TfoSqPushInteger_t = void(__fastcall*)(HSQUIRRELVM, SQInteger);
using TfoSqPushBool_t = void(__fastcall*)(HSQUIRRELVM, SQBool);
using TfoSqPushFloat_t = void(__fastcall*)(HSQUIRRELVM, SQFloat);
using TfoSqSetTop_t = void(__fastcall*)(HSQUIRRELVM, SQInteger);

void R1OTfo_sq_pushnull(HSQUIRRELVM v)
{
    reinterpret_cast<TfoSqPushNull_t>(g_R1OTfoLauncherBase + 0x2BDD0)(v);
}

void R1OTfo_sq_pushstring(HSQUIRRELVM v, const SQChar* str, SQInteger len)
{
    reinterpret_cast<TfoSqPushString_t>(g_R1OTfoLauncherBase + 0x2BE30)(v, str, len);
}

void R1OTfo_sq_pushinteger(R1SquirrelVM*, HSQUIRRELVM v, SQInteger value)
{
    reinterpret_cast<TfoSqPushInteger_t>(g_R1OTfoLauncherBase + 0x2BF30)(v, value);
}

void R1OTfo_sq_pushbool(R1SquirrelVM*, HSQUIRRELVM v, SQBool value)
{
    reinterpret_cast<TfoSqPushBool_t>(g_R1OTfoLauncherBase + 0x2BF90)(v, value);
}

void R1OTfo_sq_pushfloat(R1SquirrelVM*, HSQUIRRELVM v, SQFloat value)
{
    reinterpret_cast<TfoSqPushFloat_t>(g_R1OTfoLauncherBase + 0x2C000)(v, value);
}

void R1OTfo_sq_settop(HSQUIRRELVM v, int top)
{
    reinterpret_cast<TfoSqSetTop_t>(g_R1OTfoLauncherBase + 0x2D270)(v, top);
}

void R1OTfo_sq_pop(HSQUIRRELVM v, SQInteger count)
{
	if (!v || count <= 0)
		return;

	const SQInteger currentTop = R1OTfo_sq_gettop(nullptr, v);
	const SQInteger newTop = count < currentTop ? currentTop - count : 0;
	R1OTfo_sq_settop(v, newTop);
}

CScriptManager__CreateNewVMType g_R1OTfoCreateNewVMOriginal = nullptr;
uintptr_t g_R1OTfoPrintHooksLauncherBase = 0;

struct R1OTfoImmediateHookResult {
	MH_STATUS create;
	MH_STATUS enable;
};

bool R1OTfoCreateStatusOk(MH_STATUS status)
{
	return status == MH_OK || status == MH_ERROR_ALREADY_CREATED;
}

bool R1OTfoEnableStatusOk(MH_STATUS status)
{
	return status == MH_OK || status == MH_ERROR_ENABLED;
}

R1OTfoImmediateHookResult InstallR1OTfoImmediateHook(uintptr_t target, void* detour, void** original)
{
	const MH_STATUS createStatus = MH_CreateHook(reinterpret_cast<void*>(target), detour, original);
	const MH_STATUS enableStatus = R1OTfoCreateStatusOk(createStatus)
		? MH_EnableHook(reinterpret_cast<void*>(target))
		: createStatus;
	return { createStatus, enableStatus };
}

bool R1OTfoImmediateHookInstalled(const R1OTfoImmediateHookResult& result)
{
	return R1OTfoCreateStatusOk(result.create) && R1OTfoEnableStatusOk(result.enable);
}

bool InstallR1OTfoPrintHooks(uintptr_t launcherBase)
{
	if (!launcherBase)
		return false;
	if (g_R1OTfoPrintHooksLauncherBase == launcherBase)
		return true;

	// TFO stores these by VM context in launcher+0x1FE40: 0=server, 1=client, else=UI.
	const R1OTfoImmediateHookResult serverPrint = InstallR1OTfoImmediateHook(launcherBase + 0x254F0, reinterpret_cast<void*>(&::CSquirrelVM__PrintFunc1), nullptr);
	const R1OTfoImmediateHookResult clientPrint = InstallR1OTfoImmediateHook(launcherBase + 0x255E0, reinterpret_cast<void*>(&::CSquirrelVM__PrintFunc2), nullptr);
	const R1OTfoImmediateHookResult uiPrint = InstallR1OTfoImmediateHook(launcherBase + 0x256D0, reinterpret_cast<void*>(&::CSquirrelVM__PrintFunc3), nullptr);
	const bool installed = R1OTfoImmediateHookInstalled(serverPrint)
		&& R1OTfoImmediateHookInstalled(clientPrint)
		&& R1OTfoImmediateHookInstalled(uiPrint);

	if (installed)
		g_R1OTfoPrintHooksLauncherBase = launcherBase;

	if (AreR1OFakeDediVerboseLogsEnabled()) {
		char hookLog[512];
		_snprintf_s(
			hookLog,
			sizeof(hookLog),
			_TRUNCATE,
			"R1Delta: R1O TFO squirrel print hooks launcher=%p serverTarget=%p server=[%d,%d] clientTarget=%p client=[%d,%d] uiTarget=%p ui=[%d,%d] installed=%d\n",
			reinterpret_cast<void*>(launcherBase),
			reinterpret_cast<void*>(launcherBase + 0x254F0),
			serverPrint.create,
			serverPrint.enable,
			reinterpret_cast<void*>(launcherBase + 0x255E0),
			clientPrint.create,
			clientPrint.enable,
			reinterpret_cast<void*>(launcherBase + 0x256D0),
			uiPrint.create,
			uiPrint.enable,
			installed);
		OutputDebugStringA(hookLog);
	}
	return installed;
}

}
sq_settop_t sq_settop;
CSquirrelVM__RegisterGlobalConstantInt_t CSquirrelVM__RegisterGlobalConstantInt;
CSquirrelVM__GetEntityFromInstance_t CSquirrelVM__GetEntityFromInstance;
sq_GetEntityConstant_CBaseEntity_t sq_GetEntityConstant_CBaseEntity; // CLIENT
AddSquirrelReg_t AddSquirrelReg;

static bool RunR1OTfoScriptCode(R1SquirrelVM* vm, const char* code, const char* sourceName);
static std::atomic<R1SquirrelVM*> s_R1OTfoPendingServerAutorunVm{ nullptr };
static std::atomic<bool> s_R1OTfoServerAutorunBootstrapComplete{ false };
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
SQInteger SquirrelNativeFunctionTest(HSQUIRRELVM v, __int64 a2, __int64 a3)
{
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

bool R1OMarkTFOPlayerNetworkStateChanged(void* pPlayer)
{
	if (!IsR1ODedicatedServer() || !pPlayer || !CPlayer__Script_XP_ChangedOrig)
		return false;

	// TFO's native XPChanged callback marks the owning edict's network state
	// dirty and then clears m_xp. Preserve the real value while using that exact
	// native path as the generic player-entity dirtying primitive.
	auto* xp = reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(pPlayer) + 0x1834);
	const int savedXP = *xp;
	*xp = savedXP != 0 ? savedXP : 1;
	CPlayer__Script_XP_ChangedOrig(reinterpret_cast<__int64>(pPlayer));
	*xp = savedXP;
	return true;
}

void __fastcall CPlayer__Script_XP_ChangedHook(__int64 a1) {
	Msg("CPlayer__Script_XP_ChangedHook %x\n",a1);
	CPlayer__Script_XP_ChangedOrig(a1);
}

typedef void (*CPlayer__Script_XP_Changed)(__int64 at);
CPlayer__Script_XP_Changed CPlayer__Script_Gen_Changed_Orig;

void __fastcall CPlayer__Script_Gen_Changed(__int64 a1) {
	Msg("Gen Changed Hook\n");
	CPlayer__Script_Gen_Changed_Orig(a1);
}

R1SquirrelVM* GetClientVMPtr() {
	return *(R1SquirrelVM**)(G_client + 0x16BBE78);
}
R1SquirrelVM* GetUIVMPtr() {
	return *(R1SquirrelVM**)(G_client + 0x16C1FA8);
}

typedef void (*CPlayer__SetXP)(__int64 a1,int a2);

CPlayer__SetXP CPlayer__SetXPRebuildOrig;
void __fastcall CPlayer__SetXPRebuild(__int64 a1, int a2) {
	*(int*)(a1 + 0x1834) = a2;
	return;
}

typedef __int64 (*CPlayer__SetGen)(__int64 a1, int a2);

CPlayer__SetGen CPlayer__SetGenOrig;

void __fastcall CPlayer__SetGenHook(__int64 a1, int a2) {
	*(int*)(a1 + 0x183C) = a2;
}


void __fastcall SetGen(__int64 a1, int a2) {
	if(a2 < 0) {
		*(int*)(a1 + 0x183C) = 0;
	}
	else if (a2 > 9) {
		*(int*)(a1 + 0x183C) = 9;
	}
	else {
		*(int*)(a1 + 0x183C) = a2;
	}
}


void* __fastcall CSquirrelVM__GetEntityFromInstance_Rebuild(__int64 a2, __int64 a3)
{
	__int64 result; // rax
	__int64 v4; // rax
	__int64 v5; // rcx
	__int64 v6; // rax
	unsigned __int64 v7; // rax

	if (!a2)
	{
		return 0LL;
	}
	if (*(_DWORD*)a2 != 0xA008000)
		return 0LL;
	v4 = *(_QWORD*)(a2 + 8);
	v5 = *(_QWORD*)(v4 + 64);
	if (!v5)
		return 0LL;
	v6 = *(_QWORD*)(v4 + 56);
	if (a3)
	{
		v7 = *(_QWORD*)(v6 + 128);
		if (v7 != a3)
		{
			if (v7 < 2)
				return 0LL;
			result = *(_QWORD*)(v7 + 24);
			if (!result)
				return 0LL;
			while (result != a3)
			{
				result = *(_QWORD*)(result + 24);
				if (!result)
					return (void*)result;
			}
		}
	}
	return (void*)(*(_QWORD*)v5);
}

void* sq_getentity(HSQUIRRELVM v, SQInteger iStackPos)
{
	SQObject obj;
	if (!sq_getstackobj || SQ_FAILED(sq_getstackobj(nullptr, v, iStackPos, &obj)))
		return nullptr;

	if (IsR1ODedicatedServer()) {
		if (obj._type != OT_INSTANCE || !obj._unVal.pInstance)
			return nullptr;

		__try {
			auto context = *reinterpret_cast<uintptr_t**>(
				reinterpret_cast<uintptr_t>(obj._unVal.pInstance) + 0x40);
			if (!context || !context[0])
				return nullptr;

			void* entity = reinterpret_cast<void*>(context[0]);
			if (context[1]) {
				void* adapter = *reinterpret_cast<void**>(context[1] + 0x40);
				if (adapter) {
					void** vtable = *reinterpret_cast<void***>(adapter);
					if (vtable && vtable[0]) {
						using UnwrapEntityFn = void*(__fastcall*)(void*, void*);
						entity = reinterpret_cast<UnwrapEntityFn>(vtable[0])(adapter, entity);
					}
				}
			}
			return entity;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return nullptr;
		}
	}

	auto constant = (G_server + 0xD42040);
	return CSquirrelVM__GetEntityFromInstance_Rebuild(
		reinterpret_cast<__int64>(&obj),
		static_cast<__int64>(constant));
}

template <typename Return, typename ... Arguments>
constexpr Return Call(void* vmt, const std::uint32_t index, Arguments ... args) noexcept
{
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



int UpdateAddons(HSQUIRRELVM v) {
	//const char* str = "thread void function() { wait 1 while(true) {  wait 1 } }"; 
	//auto result = sq_compilebuffer(v, str, strlen(str), "console", SQTrue);
	//if (result != -1)
	//{	
	//	base_getroottable(v);
	//	SQRESULT callResult = sq_call(v, 1,false,true);
	//	Msg("Call Result: %d\n", callResult);
	//	return 1;
	//}
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
	//char szAddonDirName[60];
	auto ret = func(file_system, "MOD", 0, szModPath, 260);
	snprintf(szAddOnListPath, 260, "%s%s", szModPath, "addonlist.txt");
	KeyValues* kv = new KeyValues("AddonList");
	kv_load_file_addr(kv, base_file_system, szAddOnListPath, nullptr, 0);
	auto vm = GetUIVMPtr();
	SQInteger index = 0;
	SQBool enabled = SQFalse;
	if (SQ_FAILED(sq_getinteger(vm, v, 2, &index))) {
		kv->DeleteThis();
		return sq_throwerror(v, "index must be an integer");
	}
	if (SQ_FAILED(sq_getbool(vm, v, 3, &enabled))) {
		kv->DeleteThis();
		return sq_throwerror(v, "enabled must be a bool");
	}
	std::cout << kv->GetString() << std::endl;
	int i = 0; // Declare the iterator before the loop
	for (KeyValues* subkey = kv->GetFirstValue(); subkey; subkey = subkey->GetNextValue(), ++i) {
		const char* name = subkey->GetName();
		bool value = subkey->GetInt(NULL, 0) != 0;
		if (i == index) {
			subkey->SetInt(NULL, enabled);
			std::cout << "Updated: " << name << " to " << enabled << std::endl;
			break;
		}
		// find the index of the addon
	}
	sq_pushinteger(vm, v, 1);
	kv_write_file_addr(kv, base_file_system, szAddOnListPath);
	kv->DeleteThis();
	// Save the keyvalues to the file
	return 1;
}

int GetAddonsPath(HSQUIRRELVM v) {
	auto func_addr = g_CVFileSystem->GetSearchPath;

	auto func = (int(__fastcall*)(void*, const char*, int64, char*, int64))func_addr;

	char szModPath[260];
	void* file_system = *(void**)(G_client + 0x380E678);

	auto ret = func(file_system, "MOD", 0, szModPath, 260);

	//sq_pushstring(v, szModPath, -1);

	char szAddonsPath[260];


	sprintf(szAddonsPath, "%s/%s", szModPath, "addons");

	sq_pushstring(v, szAddonsPath, -1);

	// open the path in explorer
	ShellExecuteA(NULL, "open", szAddonsPath, NULL, NULL, SW_SHOWNORMAL);


	return 1;

}



int GetMods(HSQUIRRELVM v) {

	auto func_addr = g_CVFileSystem->GetSearchPath;
	auto remove_file_addr = g_CVFileSystem->RemoveFile;
	auto kv_load_file = G_client + 0x65F980;
	void* file_system = *(void**)(G_client + 0x380E678);
	auto base_file_system = (uintptr_t)file_system + 0x8;
	auto kv_load_file_addr = (int(__fastcall*)(KeyValues*, int64, char*, const char*, int))kv_load_file;
	auto load_addon_info_addr = G_client + 0x3D82F0;
	auto get_addon_image_addr = G_client + 0x3DAB30;
	auto load_addon_info_file = (int(__fastcall*)(void*, KeyValues**, const char*, bool))load_addon_info_addr;
	auto get_addon_image = (int(__fastcall*)(void*, const char*, char*, int,bool))get_addon_image_addr;
	auto func = (int(__fastcall*)(void*, const char*, int64, char*, int64))func_addr;
	auto remove_file = (int(__fastcall*)(void*, char*, int))remove_file_addr;
	auto extract_addon_info_addr = G_client + 0x3D9910;
	auto extract_addon_info = (int(__fastcall*)(void*, char*))extract_addon_info_addr;
	auto strip_extention_addr = G_client + 0x658700;
	auto strip_extention = (int(__fastcall*)(const char*,char*,int))strip_extention_addr;
	auto create_vpk_addr = G_client + 0x511E30;
	auto create_vpk = (int(__fastcall*)(char*,char*, void*,int,int,int64))create_vpk_addr;
	auto vpk_open_file_addr = G_client + 0x50C250;
	auto vpk_open_file = (int(__fastcall*)(char*, int*, const char*))vpk_open_file_addr;
	char szModPath[260];
	char szAddOnListPath[260];
	char szAddonDirName[64];
	auto vm = GetUIVMPtr();
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
		if (V_stristr(name, ".vpk"))
		{
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
		else
		{
			bIsVPK = 0;
			V_strncpy(szAddonDirName, name, 60);
		}
		printf("Addon: %s\n", szAddonDirName);
		KeyValues* loadedAddonInfo = nullptr;
		if (load_addon_info_file(nullptr, &loadedAddonInfo, name, bIsVPK)) {
			char image[260];
			get_addon_image(nullptr, name, image, 260, bIsVPK);
			snprintf(addoninfoFilename, 260, "%s%s%c%s%c%s", szModPath, "addons", '\\', szAddonDirName, '\\', "addoninfo.txt");
			KeyValues* addoninfo = new KeyValues("AddonInfo");
			kv_load_file_addr(addoninfo, base_file_system, addoninfoFilename, nullptr, 0);
			bool enabled = subkey->GetInt(NULL, 0) != 0;
			auto author = addoninfo->GetWString("addonauthor");
			auto addon_name = addoninfo->GetWString("addontitle");
			auto version = addoninfo->GetWString("addonversion");
			auto description = addoninfo->GetWString("addondescription");
			auto localization = addoninfo->GetWString("addonlocalization");
			char author_str[260];
			char addon_name_str[260];
			char description_str[1024];
			char version_str[260];
			char localization_str[260];
			wcstombs(addon_name_str, addon_name, 260);
			wcstombs(author_str, author, 260);
			wcstombs(description_str, description, 1024);
			wcstombs(version_str, version, 260);
			wcstombs(localization_str, localization, 260);
			if (localization_str[0] != '\0') {
				// don't add dup;aicates 
				//if it does not exist add it
				auto it = std::find(modLocalization_files.begin(), modLocalization_files.end(), localization_str);
				if (it != modLocalization_files.end()) {
				}
				else {
					modLocalization_files.push_back(localization_str);
					typedef void(__fastcall* pCLocalize__ReloadLocalizationFiles_t)(void*);
					if (G_localize) {
						static void(__fastcall * pCLocalize__ReloadLocalizationFiles)(void* pVguiLocalize) = (pCLocalize__ReloadLocalizationFiles_t)(G_localize + 0x3A40);
						pCLocalize__ReloadLocalizationFiles(G_localizeIface);
					}
				}
			}
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
			sq_pushstring(v, "localization", -1);
			sq_pushstring(v, localization_str, strlen(localization_str));
			sq_newslot(v, -3, 0);
			sq_arrayappend(v, -2);
			addoninfo->DeleteThis();
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
		if (loadedAddonInfo)
			loadedAddonInfo->DeleteThis();
	}
	kv->DeleteThis();
	return 1;
}





SQInteger AddXp(HSQUIRRELVM v) {
	SQInteger xp;
	if (SQ_FAILED(sq_getinteger(IsR1ODedicatedServer() ? nullptr : GetServerVMPtr(), v, 3, &xp)))
		return sq_throwerror(v, "xp must be an integer");
	auto player = sq_getentity(v, 2);
	if (!player)
		return sq_throwerror(v, "player is null");
	CPlayer__SetXPRebuild(reinterpret_cast<__int64>(player), xp);
	return 0;
}



SQInteger SetGenSQ(HSQUIRRELVM v) {
	SQInteger gen;
	if (SQ_FAILED(sq_getinteger(IsR1ODedicatedServer() ? nullptr : GetServerVMPtr(), v, 3, &gen)))
		return sq_throwerror(v, "generation must be an integer");
	auto player = sq_getentity(v, 2);
	if (!player)
		return sq_throwerror(v, "player is null");
	SetGen(reinterpret_cast<__int64>(player), gen);
	return 0;
}

SQInteger SetRanked(HSQUIRRELVM v) {
	SQBool ranked;
	const void* player = sq_getentity(v, 2);
	if (!player)
		return sq_throwerror(v, "player is null");
	if (SQ_FAILED(sq_getbool(IsR1ODedicatedServer() ? nullptr : GetServerVMPtr(), v, 3, &ranked)))
		return sq_throwerror(v, "ranked state must be a boolean");
	auto player_ptr = reinterpret_cast<__int64>(player);
	*(bool*)(player_ptr + 0x1844) = ranked != 0;
	return 0;
}

SQInteger Script_GetLoadingStatusText(HSQUIRRELVM v)
{
	if (g_loadingStatusText)
		sq_pushstring(v, g_loadingStatusText, -1);
	else
		sq_pushnull(v);

	return 1;
}

SQInteger Script_IsDedicated(HSQUIRRELVM v)
{
	if (IsR1ODedicatedServer()) {
		sq_pushbool(nullptr, v, IsDedicatedServer());
		return 1;
	}

	auto r1sqvm = GetServerVMPtr();

	sq_pushbool(r1sqvm, r1sqvm->sqvm, IsDedicatedServer());

	return 1;
}

SQInteger Script_Server_SetActiveBurnCardIndex(HSQUIRRELVM v) {
	const void* player = sq_getentity(v, 2);
	if (!player)
		return sq_throwerror(v, "player is null");

	SQInteger index;
	if (SQ_FAILED(sq_getinteger(IsR1ODedicatedServer() ? nullptr : GetServerVMPtr(), v, 3, &index)))
		return sq_throwerror(v, "burn card index must be an integer");

	auto player_ptr = reinterpret_cast<__int64>(player);
	*(int*)(player_ptr + 0x1A14) = index;
	if (IsR1ODedicatedServer() && !R1OMarkTFOPlayerNetworkStateChanged(const_cast<void*>(player)))
		return sq_throwerror(v, "failed to mark burn card state for replication");
	return 0;
}

SQInteger Script_Server_GetActiveBurnCardIndex(HSQUIRRELVM v) {
	const void* player = sq_getentity(v, 2);
	if (!player)
		return sq_throwerror(v, "player is null");

	auto player_ptr = reinterpret_cast<__int64>(player);
	int value = *(int*)(player_ptr + 0x1A14);
	
	sq_pushinteger(IsR1ODedicatedServer() ? nullptr : GetServerVMPtr(), v, value);

	return 1;
}

SQInteger Script_ServerGetPlayerUserID(HSQUIRRELVM v) {
	if (IsR1ODedicatedServer()) {
		sq_pushinteger(nullptr, v, 0);
		return 1;
	}

	void* player = sq_getentity(v, 2);
	if (!player)
	{
		return sq_throwerror(v, "player is null");
	}
	auto serverVm = GetServerVMPtr();
	auto edict = *reinterpret_cast<__int64*>(reinterpret_cast<__int64>(player) + 64);
	if (!edict) {
		return sq_throwerror(v, "edict is null");
	}
	typedef int (*GetPlayerNetInfo_t)(uintptr_t, uintptr_t);
	static auto get_player_user_id = (GetPlayerNetInfo_t)(g_CVEngineServer->GetPlayerUserId);
	auto user_id = get_player_user_id(g_CVEngineServerInterface, edict);
	sq_pushinteger(serverVm,v, user_id);
	return 1;
}

SQInteger Script_ServerGetPlayerPlatformUserID(HSQUIRRELVM v) {
	void* player = sq_getentity(v, 2);
	if (!player)
	{
		return sq_throwerror(v, "player is null");
	}

	const uint64_t uid = *reinterpret_cast<const uint64_t*>(
		reinterpret_cast<uintptr_t>(player) + 0x1448);
	char uidString[32];
	_snprintf_s(uidString, sizeof(uidString), _TRUNCATE, "%llu",
		static_cast<unsigned long long>(uid));
	sq_pushstring(v, uidString, -1);
	return 1;
}

SQInteger Script_ServerGetPlayerIp(HSQUIRRELVM v)
{
	if (IsR1ODedicatedServer()) {
		sq_pushstring(v, "", -1);
		return 1;
	}

	void* player = sq_getentity(v, 2);
	if (!player)
	{
		return sq_throwerror(v, "player is null");
	}

	auto edict = *reinterpret_cast<__int64*>(reinterpret_cast<__int64>(player) + 64);
	auto index = ((edict - reinterpret_cast<__int64>(pGlobalVarsServer->pEdicts)) / 56);
	if (index < 0 || index >= 32)
	{
		return sq_throwerror(v, "invalid player index");
	}
	typedef void* (*GetPlayerNetInfo_t)(uintptr_t, int);
	static auto get_player_net_info = (GetPlayerNetInfo_t)(g_CVEngineServer->GetPlayerNetInfo);
	auto net_chan = get_player_net_info(g_CVEngineServerInterface, index);
	if (!net_chan) {
		return sq_throwerror(v, "invalid net chan");
	}
	std::string ip = CallVFunc<char*>(0x1, (void*)net_chan);
	if (ip.empty()) {
		return sq_throwerror(v, "invalid ip");
	}
	sq_pushstring(v, ip.c_str(), -1);
	return 1;
}

SQInteger OpenDiscordURL(HSQUIRRELVM v) {
	// Define the URL as a wide string
	const wchar_t* url = L"https://discord.gg/tUVWPp4Hv3";

	// Prepare the command line buffer: "explorer.exe <url>"
	wchar_t commandLine[256];
	// Ensure safe formatting; you may adjust the buffer size if necessary
	if (swprintf(commandLine, sizeof(commandLine) / sizeof(wchar_t), L"explorer.exe %s", url) < 0) {
		return sq_throwerror(v, "Failed to prepare command line");
	}

	// Initialize the STARTUPINFO structure
	STARTUPINFOW si = { 0 };
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi = { 0 };

	// Create the process
	BOOL success = CreateProcessW(
		NULL,           // Use command line parsing to find executable
		commandLine,    // Command line string
		NULL,           // Process security attributes
		NULL,           // Thread security attributes
		FALSE,          // Do not inherit handles
		0,              // No creation flags
		NULL,           // Use parent's environment block
		NULL,           // Use parent's starting directory 
		&si,            // Pointer to STARTUPINFO structure
		&pi             // Pointer to PROCESS_INFORMATION structure
	);

	if (!success) {
		return sq_throwerror(v, "Failed to open URL");
	}

	// Close process and thread handles since they are not needed
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	return 0;
}

size_t to_narrow(const wchar_t* src, char* dest, size_t dest_len) {
	size_t i;
	wchar_t code;

	i = 0;

	while (src[i] != '\0' && i < (dest_len - 1)) {
		code = src[i];
		if (code < 128)
			dest[i] = char(code);
		else {
			dest[i] = '?';
			if (code >= 0xD800 && code <= 0xDBFF)
				// lead surrogate, skip the next code unit, which is the trail
				i++;
		}
		i++;
	}

	dest[i] = '\0';

	return i - 1;

}

// Convert a wide Unicode string to an UTF8 string
std::string utf8_encode(const std::wstring& wstr)
{
	if (wstr.empty()) return std::string();
	int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
	std::string strTo(size_needed, 0);
	WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
	return strTo;
}

// Convert an UTF8 string to a wide Unicode String
std::wstring utf8_decode(const std::string& str)
{
	if (str.empty()) return std::wstring();
	int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
	std::wstring wstrTo(size_needed, 0);
	MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
	return wstrTo;
}

SQInteger Script_Localize(HSQUIRRELVM v) {
	const char* str;
	sq_getstring(v, -1, &str);
	
	auto mod = GetModuleHandleA("localize.dll");
	if (!mod) {
		Msg("Failed to get localize.dll module handle\n");
		return 0;
	}
	CreateInterfaceFn localizeCreate = (CreateInterfaceFn)(GetProcAddress(mod,"CreateInterface"));
	if (!localizeCreate) {
		Msg("Failed to get CreateInterface function\n");
		return 0;
	}
	auto localize_vtable = localizeCreate("Localize_001", nullptr);
	if (!localize_vtable) {
		Msg("Failed to get Localize_001 interface\n");
		return 0;
	}
	auto result =  Call<const wchar_t*>((void*)localize_vtable, 10,str);
	// script_client(printt(Localize("#EOG_XP_TOTAL")))
	if (result == nullptr) {
		sq_pushstring(v, (const char*)"", -1);
		return 1;
	}

	auto ut8_str = utf8_encode(result);
	//Msg("Original: %s Localize: %ls, UTF8: %s\n", str, result, ut8_str);

	// Push the localized string onto the stack
	sq_pushstring(v, ut8_str.c_str(), -1);

	return 1;

}


void localilze_string(const char* str, char* localized_str, int size)
{
	auto mod = GetModuleHandleA("localize.dll");
	if (!mod) {
		Msg("Failed to get localize.dll module handle\n");
		return;
	}
	CreateInterfaceFn localizeCreate = (CreateInterfaceFn)(GetProcAddress(mod, "CreateInterface"));
	if (!localizeCreate) {
		Msg("Failed to get CreateInterface function\n");
		return;
	}
	auto localize_vtable = localizeCreate("Localize_001", nullptr);
	if (!localize_vtable) {
		Msg("Failed to get Localize_001 interface\n");
		return;
	}
	auto result = Call<const wchar_t*>((void*)localize_vtable, 10, str);
	// script_client(printt(Localize("#EOG_XP_TOTAL")))
	if (result == nullptr) {
		strncpy(localized_str, "", size);
		return;
	}

	auto ut8_str = utf8_encode(result);
	//Msg("Original: %s Localize: %ls, UTF8: %s\n", str, result, ut8_str);
	strncpy(localized_str, ut8_str.c_str(), size);
	return;
}

int GetR1DVersion(HSQUIRRELVM v) {
	const char* newVersionString = R1D_VERSION;
	sq_pushstring(v, newVersionString, -1);
	return 1;
}

int GetR1DDisplayVersion(HSQUIRRELVM v) {
	sq_pushstring(v, R1D_DISPLAY_VERSION, -1);
	return 1;
}



// please god someone change this to pushconst
int GetMinimumR1DVersion(HSQUIRRELVM v)
{
	const char* versionString = R1D_MINIMUM_VERSION;
	sq_pushstring(v, versionString, -1);
	return 1;
}

struct CRecipientFilter {
	char pad[58];
};

static uintptr_t GetUserMessageEngineMethod(size_t index, void** engineServer)
{
	if (!engineServer)
		return 0;

	if (IsR1ODedicatedServer()) {
		// R1O hands server_local.dll a native TFO-layout VEngineServer022.  Use
		// that exact interface here, just as the persistence delivery path does;
		// the legacy compatibility vtable is not the live R1O server interface.
		void* nativeEngineServer = GetR1ONativeEngineServer022();
		if (!nativeEngineServer)
			return 0;

		auto vtable = *reinterpret_cast<uintptr_t* const*>(nativeEngineServer);
		if (!vtable)
			return 0;

		*engineServer = nativeEngineServer;
		return vtable[index];
	}

	*engineServer = g_r1oCVEngineServerInterface;
	return g_r1oCVEngineServerInterface[index];
}

int64 UserMsgBegin_Wrapper(CRecipientFilter* filter, const char* name) {
	auto UserMsgBegin = reinterpret_cast<int (*)(CRecipientFilter*, const char**)>(G_server + 0x629910);
	auto v6 = UserMsgBegin(0, &name);
	if (v6 == -1) {
		Error("UserMessageBegin:  Unregistered message '%s'\n", name);
	}
	void* engineServer = nullptr;
	auto ServerCreateMessage = reinterpret_cast<int64 (*)(void*, CRecipientFilter*, int64, const char*, char)>(
		GetUserMessageEngineMethod(42, &engineServer));
	if (!ServerCreateMessage || !engineServer || !filter || !name)
		return 0;
	return ServerCreateMessage(engineServer, filter, v6, name, 1);
}

void EndMessage() {
	void* engineServer = nullptr;
	auto ServerEndMessage = reinterpret_cast<int64(*)(void*)>(
		GetUserMessageEngineMethod(43, &engineServer));
	if (ServerEndMessage && engineServer)
		ServerEndMessage(engineServer);
}

void ConstructCRecipientFilter(void* a1)
{
	// 1) patch in the vtable pointer:
	void** vftable = reinterpret_cast<void**>(G_server + 0x07C9298);
	*reinterpret_cast<void***>(a1) = vftable;

	// 2) zero the rest of the fields at offsets 8,16,24,32,40,48:
	//    – WORD at +8
	*reinterpret_cast<uint16_t*>  (reinterpret_cast<uint8_t*>(a1) + 8) = 0;
	//    – QWORDs at +16, +24, +32
	*reinterpret_cast<uint64_t*>  (reinterpret_cast<uint8_t*>(a1) + 16) = 0;
	*reinterpret_cast<uint64_t*>  (reinterpret_cast<uint8_t*>(a1) + 24) = 0;
	*reinterpret_cast<uint64_t*>  (reinterpret_cast<uint8_t*>(a1) + 32) = 0;
	//    – DWORD at +40
	*reinterpret_cast<uint32_t*>  (reinterpret_cast<uint8_t*>(a1) + 40) = 0;
	//    – WORD at +48
	*reinterpret_cast<uint16_t*>  (reinterpret_cast<uint8_t*>(a1) + 48) = 0;
}

bool SendChatMsg(CRecipientFilter* filter, int fromIndex, const char* msg, bool team, bool dead)
{
	static auto MessageWriteByte = reinterpret_cast<void (*)(int64, int, int)>(G_server + 0x142FA0);
	static auto MessageWriteString = reinterpret_cast<void (*)(int64,const char*)>(G_server + 0x663AF0);
	static auto MessageWriteBool = reinterpret_cast<void (*)(bool)>(G_server + 0x14AB60);

	static int64_t* activeMsg = reinterpret_cast<int64_t*>(G_server + 0xC31058);

	*activeMsg = UserMsgBegin_Wrapper(filter, "SayText");
	if (!*activeMsg)
		return false;
	MessageWriteByte(*activeMsg, fromIndex, 8);
	MessageWriteString(0, msg);
	MessageWriteBool(team);
	MessageWriteBool(dead);
	EndMessage();
	*activeMsg = 0;
	return true;
}

bool SendChatMessageToRecipient(void* recipient, int fromIndex, const char* message, bool team, bool dead)
{
	if (!recipient)
		return false;

	static auto CRecipientFilter__AddRecipient = reinterpret_cast<void(*)(void*, void*)>(G_server + 0x1E7CB0);
	static auto DestroyFilter = reinterpret_cast<void(*)(void*, bool)>(G_server + 0x1E78D0);

	CRecipientFilter filter;
	ConstructCRecipientFilter(&filter);
	CRecipientFilter__AddRecipient(&filter, recipient);
	const bool sent = SendChatMsg(&filter, fromIndex, message, team, dead);
	DestroyFilter(&filter, false);
	return sent;
}


int SendChatWrapper(HSQUIRRELVM v) {
	// args: this, entity player / bool = true (broadcast) / array of entities for multiple recipients, int fromPlayerIndex, string text, bool isTeam = false, bool isDead = false

	static auto CRecipientFilter__AddAllPlayers = reinterpret_cast<void(*)(void*)>(G_server + 0x1E7BA0);
	static auto CRecipientFilter__AddRecipient = reinterpret_cast<void(*)(void*, void*)>(G_server + 0x1E7CB0);
	static auto DestroyFilter = reinterpret_cast<void(*)(void*, bool)>(G_server + 0x1E78D0);

	SQInteger fromPlayer = 0;
	if (SQ_FAILED(sq_getinteger(nullptr, v, 3, &fromPlayer))) return -1;

	const SQChar* msg = nullptr;
	if (SQ_FAILED(sq_getstring(v, 4, &msg))) return -1;

	SQBool isTeam = false, isDead = false;
	sq_getbool(nullptr, v, 5, &isTeam);
	sq_getbool(nullptr, v, 6, &isDead);

	CRecipientFilter filter;
	ConstructCRecipientFilter(&filter);

	SQObjectType playerType = sq_gettype(v, 2);
	if (playerType == OT_BOOL) {
		SQBool whichPlayer = false;
		if (SQ_FAILED(sq_getbool(nullptr, v, 2, &whichPlayer))) return -1;
		if (whichPlayer) CRecipientFilter__AddAllPlayers(&filter);
	} else if (playerType == OT_INSTANCE) {
		void* entity = sq_getentity(v, 2);
		if (!entity) {
			sq_throwerror(v, "Passed instance is not a valid entity");
			return -1;
		}
		CRecipientFilter__AddRecipient(&filter, entity);
	} else if (playerType == OT_ARRAY) {
		sq_push(v, 2);
		sq_pushnull(v);
		while (SQ_SUCCEEDED(sq_next(v, -2))) {
			if (sq_gettype(v, -1) != OT_INSTANCE) {
				sq_pop(v, 4);
				sq_throwerror(v, "Array element is not an entity");
				return -1;
			}
			void* entity = sq_getentity(v, -1);
			if (!entity) {
				sq_pop(v, 4);
				sq_throwerror(v, "Array member instance is not a valid entity");
				return -1;
			}
			CRecipientFilter__AddRecipient(&filter, entity);
			sq_pop(v, 2);
		}

		sq_pop(v, 2);
	}

	SendChatMsg(&filter, fromPlayer, msg, isTeam, isDead);
	DestroyFilter(&filter, 0);
	return 0;
}

// --- raw one-packet send ---
static inline void SendShowMenuRaw(void* filter, uint16_t keysMask, int secondsToStayOpen, bool needMore, const char* text)
{
	static auto MessageWriteBits = reinterpret_cast<void (*)(int64_t, int, int)>(G_server + 0x142FA0);
	static auto MessageWriteStr = reinterpret_cast<void (*)(int64_t, const char*)>(G_server + 0x663AF0);
	static int64_t* gActiveMsg = reinterpret_cast<int64_t*>(G_server + 0xC31058);

	*gActiveMsg = UserMsgBegin_Wrapper((CRecipientFilter*)filter, "ShowMenu");
	if (!*gActiveMsg)
		return;
	MessageWriteBits(*gActiveMsg, (int)(keysMask & 0xFFFF), 16);
	MessageWriteBits(*gActiveMsg, (int)(secondsToStayOpen & 0xFF), 8);
	MessageWriteBits(*gActiveMsg, needMore ? 1 : 0, 8);
	MessageWriteStr(*gActiveMsg, text ? text : "");
	EndMessage();
}

// --- auto-chunk (splits on '\n' where possible) ---
static inline void SendShowMenuChunked(void* filter, uint16_t keysMask, int secondsToStayOpen, const char* fullText, size_t maxBytesPerChunk = 190)
{
	if (!fullText || !*fullText) { SendShowMenuRaw(filter, keysMask, secondsToStayOpen, /*needMore=*/false, ""); return; }

	const char* p = fullText; const char* end = fullText + std::strlen(fullText);
	while (p < end) {
		const char* start = p; const char* lastNL = nullptr; size_t taken = 0;
		while (p < end && taken < maxBytesPerChunk) { if (*p == '\n') lastNL = p; ++p; ++taken; }
		const char* chunkEnd = (p >= end) ? end : (lastNL ? lastNL + 1 : start + taken);
		if (lastNL && chunkEnd == lastNL + 1) p = chunkEnd; // break after newline
		std::string chunk(start, chunkEnd);
		SendShowMenuRaw(filter, keysMask, secondsToStayOpen, /*needMore=*/(chunkEnd < end), chunk.c_str());
	}
}

// --- one public function: SendShowMenu(recips, text, keysMask, seconds) ---
int SendShowMenu(HSQUIRRELVM v)
{
	if (IsR1ODedicatedServer())
		return 0;

	// Expect: this + 4 args
	if (sq_gettop(GetServerVMPtr(), v) != 5) return sq_throwerror(v, "usage: SendShowMenu(recips, text, keysMask, secondsToStayOpen)");

	const SQChar* text = nullptr; SQInteger mask = 0, secs = -1;
	if (SQ_FAILED(sq_getstring(v, 3, &text))) return sq_throwerror(v, "text required");
	if (SQ_FAILED(sq_getinteger(GetServerVMPtr(), v, 4, &mask))) return sq_throwerror(v, "keysMask required");
	if (SQ_FAILED(sq_getinteger(GetServerVMPtr(), v, 5, &secs))) return sq_throwerror(v, "secondsToStayOpen required");

	// Build filter ON THIS STACK FRAME
	static auto AddAll = reinterpret_cast<void(*)(void*)>(G_server + 0x1E7BA0);
	static auto AddRec = reinterpret_cast<void(*)(void*, void*)>(G_server + 0x1E7CB0);
	static auto DestroyF = reinterpret_cast<void(*)(void*, bool)>(G_server + 0x1E78D0);

	CRecipientFilter filter;                 // <— real object, correct size/layout
	ConstructCRecipientFilter(&filter);

	SQObjectType t = sq_gettype(v, 2);
	if (t == OT_BOOL) {
		SQBool all = SQFalse; sq_getbool(nullptr, v, 2, &all);
		if (all) AddAll(&filter);
	}
	else if (t == OT_INSTANCE) {
		void* ent = sq_getentity(v, 2);
		if (!ent) { DestroyF(&filter, 0); return sq_throwerror(v, "instance is not a valid entity"); }
		AddRec(&filter, ent);                // expects CBaseEntity*
	}
	else if (t == OT_ARRAY) {
		sq_push(v, 2); sq_pushnull(v);
		while (SQ_SUCCEEDED(sq_next(v, -2))) {
			if (sq_gettype(v, -1) != OT_INSTANCE) { sq_pop(v, 4); DestroyF(&filter, 0); return sq_throwerror(v, "array element is not an entity"); }
			void* ent = sq_getentity(v, -1);
			if (!ent) { sq_pop(v, 4); DestroyF(&filter, 0); return sq_throwerror(v, "array element is not a valid entity"); }
			AddRec(&filter, ent);
			sq_pop(v, 2);
		}
		sq_pop(v, 2);
	}
	else {
		DestroyF(&filter, 0);
		return sq_throwerror(v, "arg1 must be entity, array<entity>, or bool(true=all)");
	}

	// Send (auto-chunked)
	SendShowMenuChunked(&filter, (uint16_t)(mask & 0xFFFF), (int)secs, text);

	DestroyF(&filter, 0);
	return 0;
}


static bool RunR1OTfoAutorunFile(R1SquirrelVM* vm, const char* runFilename)
{
	if (!vm || !runFilename || !runFilename[0])
		return false;

	using OpenFunc = FileHandle_t(__thiscall*)(void*, const char*, const char*, const char*);
	using SizeFunc = int64_t(__thiscall*)(void*, FileHandle_t);
	using ReadFunc = int(__thiscall*)(void*, void*, int, FileHandle_t);
	using CloseFunc = void(__thiscall*)(void*, FileHandle_t);

	void* fileSystemInterface = GetR1ONativeFileSystem();
	if (!fileSystemInterface)
		return false;
	const auto fileSystemVtable = *reinterpret_cast<uintptr_t**>(fileSystemInterface);
	if (!fileSystemVtable)
		return false;

	// The first CBaseFileSystem methods are unchanged in TFO's
	// VFileSystem017: Read/Open/Close/Size(handle) are slots 0/2/3/7.
	const auto readFunc = reinterpret_cast<ReadFunc>(fileSystemVtable[0]);
	const auto openFunc = reinterpret_cast<OpenFunc>(fileSystemVtable[2]);
	const auto closeFunc = reinterpret_cast<CloseFunc>(fileSystemVtable[3]);
	const auto sizeFunc = reinterpret_cast<SizeFunc>(fileSystemVtable[7]);
	if (!openFunc || !sizeFunc || !readFunc || !closeFunc)
		return false;

	char scriptPath[MAX_PATH] = {};
	_snprintf_s(
		scriptPath,
		sizeof(scriptPath),
		_TRUNCATE,
		"scripts/vscripts/%s",
		runFilename);

	char resolvedPath[MAX_PATH] = {};
	bool hasReplacement = false;
	HANDLE replacementFile = INVALID_HANDLE_VALUE;
	{
		auto lease = FileCache::GetInstance().AcquireReadLease();
		hasReplacement = FileCache::GetInstance().ResolveReplacementFile(
			lease,
			scriptPath,
			resolvedPath,
			sizeof(resolvedPath));
		if (hasReplacement) {
			replacementFile = CreateFileA(
				resolvedPath,
				GENERIC_READ,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
				nullptr,
				OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL,
				nullptr);
		}
	}
	const char* openPath = hasReplacement ? resolvedPath : scriptPath;
	const char* pathId = hasReplacement ? nullptr : "GAME";

	constexpr int64_t kMaxAutorunScriptBytes = 8 * 1024 * 1024;
	std::string script;
	if (hasReplacement) {
		HANDLE file = replacementFile;
		if (file == INVALID_HANDLE_VALUE) {
			Warning("R1O autorun could not open %s (error %lu).\n", resolvedPath, GetLastError());
			return false;
		}

		LARGE_INTEGER size = {};
		if (!GetFileSizeEx(file, &size)
			|| size.QuadPart <= 0
			|| size.QuadPart > kMaxAutorunScriptBytes) {
			const DWORD error = GetLastError();
			CloseHandle(file);
			Warning(
				"R1O autorun rejected %s with size %lld (error %lu).\n",
				resolvedPath,
				static_cast<long long>(size.QuadPart),
				error);
			return false;
		}

		script.resize(static_cast<size_t>(size.QuadPart));
		DWORD bytesRead = 0;
		const BOOL readOk = ReadFile(
			file,
			script.data(),
			static_cast<DWORD>(script.size()),
			&bytesRead,
			nullptr);
		const DWORD error = readOk ? ERROR_SUCCESS : GetLastError();
		CloseHandle(file);
		if (!readOk || bytesRead != script.size()) {
			Warning(
				"R1O autorun short read for %s (%lu of %zu bytes, error %lu).\n",
				resolvedPath,
				bytesRead,
				script.size(),
				error);
			return false;
		}
	}
	else {
		FileHandle_t file = openFunc(
			fileSystemInterface,
			openPath,
			"rb",
			pathId);
		if (!file) {
			Warning("R1O autorun could not open %s.\n", scriptPath);
			return false;
		}

		const int64_t fileSize = sizeFunc(fileSystemInterface, file);
		if (fileSize <= 0 || fileSize > kMaxAutorunScriptBytes) {
			closeFunc(fileSystemInterface, file);
			Warning(
				"R1O autorun rejected %s with invalid size %lld.\n",
				scriptPath,
				static_cast<long long>(fileSize));
			return false;
		}

		script.resize(static_cast<size_t>(fileSize));
		const int bytesRead = readFunc(
			fileSystemInterface,
			script.data(),
			static_cast<int>(fileSize),
			file);
		closeFunc(fileSystemInterface, file);
		if (bytesRead != fileSize) {
			Warning(
				"R1O autorun short read for %s (%d of %lld bytes).\n",
				scriptPath,
				bytesRead,
				static_cast<long long>(fileSize));
			return false;
		}
	}

	// Legacy RunAutorunScripts invokes the VM's file runner directly.  Do not
	// wrap this outer file in IncludeScript: TFO's native DoIncludeScript is
	// non-reentrant, and an addon autorun that calls IncludeFile would fail
	// while the outer IncludeScript was still active.
	return RunR1OTfoScriptCode(vm, script.c_str(), runFilename);
}

void RunAutorunScripts(R1SquirrelVM* r1sqvm, const char* prefix) {
	if (!r1sqvm || !prefix)
		return;

	using FindFirstType = const char*(__fastcall*)(uintptr_t, const char*, uintptr_t*);
	using FindNextType = const char*(__fastcall*)(uintptr_t, uintptr_t);
	using FindCloseType = void(__fastcall*)(uintptr_t, uintptr_t);

	uintptr_t fileSystemInterface = g_CVFileSystemInterface;
	FindFirstType FindFirst = g_CVFileSystem
		? reinterpret_cast<FindFirstType>(g_CVFileSystem->FindFirst)
		: nullptr;
	FindNextType FindNext = g_CVFileSystem
		? reinterpret_cast<FindNextType>(g_CVFileSystem->FindNext)
		: nullptr;
	FindCloseType FindClose = g_CVFileSystem
		? reinterpret_cast<FindCloseType>(g_CVFileSystem->FindClose)
		: nullptr;

	if (IsR1ODedicatedServer()) {
		void* nativeFileSystem = GetR1ONativeFileSystem();
		if (!nativeFileSystem)
			return;
		auto nativeVtable = *reinterpret_cast<uintptr_t**>(nativeFileSystem);
		if (!nativeVtable)
			return;

		// TFO VFileSystem017 inserts four methods at slots 8-11.  Its native
		// FindFirst/FindNext/FindClose methods therefore live at 32/33/35.
		fileSystemInterface = reinterpret_cast<uintptr_t>(nativeFileSystem);
		FindFirst = reinterpret_cast<FindFirstType>(nativeVtable[32]);
		FindNext = reinterpret_cast<FindNextType>(nativeVtable[33]);
		FindClose = reinterpret_cast<FindCloseType>(nativeVtable[35]);
	}

	if (!fileSystemInterface)
		return;
	if (!FindFirst || !FindNext || !FindClose)
		return;

	char search[MAX_PATH] = { 0 };
	sprintf_s(search, "scripts/vscripts/autorun/%s", prefix);

	char runFilename[MAX_PATH] = { 0 };
	std::unordered_set<std::string, HashStrings, std::equal_to<>> executedScripts;
	const auto runScript = [&](const char* filename) {
		if (!filename || !filename[0])
			return;

		std::string folded(filename);
		std::transform(
			folded.begin(),
			folded.end(),
			folded.begin(),
			[](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
		if (!executedScripts.insert(folded).second)
			return;

		sprintf_s(runFilename, "autorun/%s", filename);
		if (IsR1ODedicatedServer()) {
			RunR1OTfoAutorunFile(r1sqvm, runFilename);
		}
		else {
			sprintf_s(search, "scripts/vscripts/%s", runFilename);
			Call<void, const char*, const char*>(r1sqvm, 20, search, runFilename);
		}
	};

	uintptr_t handle = 0;
	const char* filename = FindFirst(fileSystemInterface, search, &handle);
	while (filename) {
		runScript(filename);
		filename = FindNext(fileSystemInterface, handle);
	}
	if (handle)
		FindClose(fileSystemInterface, handle);

	if (IsR1ODedicatedServer()) {
		const std::vector<std::string> replacements =
			FileCache::GetInstance().FindReplacementFileNames(
				"scripts/vscripts/autorun",
				prefix);
		for (const std::string& replacement : replacements) {
			runScript(replacement.c_str());
		}
	}
}

uintptr_t(__fastcall *oOnCreateClientScriptVM)(uintptr_t);
uintptr_t OnCreateClientScriptVM(uintptr_t thisptr) {
	r1delta::ffa_targeting::SetClientFfaBased(false);
	auto ret = oOnCreateClientScriptVM(thisptr);
	RunAutorunScripts(GetClientVMPtr(), "cl_*");
	return ret;
}

bool(__fastcall *oOnCreateUIScriptVM)();
bool OnCreateUIScriptVM() {
	bool ret = oOnCreateUIScriptVM();
	if (ret) RunAutorunScripts(GetUIVMPtr(), "ui_*");
	return ret;
}

uintptr_t(__fastcall *oOnCreateServerScriptVM)();
uintptr_t OnCreateServerScriptVM() {
	r1delta::ffa_targeting::SetServerFfaBased(false);
	auto ret = oOnCreateServerScriptVM();
	RunAutorunScripts(GetServerVMPtr(), "sv_*");
	return ret;
}

SQInteger Script_SetClientFfaBased(HSQUIRRELVM v) {
	SQBool enabled = SQFalse;
	if (SQ_FAILED(sq_getbool(nullptr, v, 2, &enabled)))
		return sq_throwerror(v, "FFA state must be a boolean");
	r1delta::ffa_targeting::SetClientFfaBased(enabled != SQFalse);
	return 0;
}

SQInteger Script_SetServerFfaBased(HSQUIRRELVM v) {
	SQBool enabled = SQFalse;
	if (SQ_FAILED(sq_getbool(nullptr, v, 2, &enabled)))
		return sq_throwerror(v, "FFA state must be a boolean");
	r1delta::ffa_targeting::SetServerFfaBased(enabled != SQFalse);
	return 0;
}

int AutoCVar(HSQUIRRELVM v) {
	const char *key, *defaultValue = "", * desc = "";
	if (SQ_FAILED(sq_getstring(v, 2, &key))) return -1;
	sq_getstring(v, 3, &defaultValue);
	sq_getstring(v, 4, &desc);

	constexpr const char* AUTOCVAR_PREFIX = "autocvar_";
	constexpr size_t AUTOCVAR_PREFIX_SIZE = std::char_traits<char>::length(AUTOCVAR_PREFIX);

	size_t size = AUTOCVAR_PREFIX_SIZE + strlen(key) + 1;
	// The R1 ConVar stores this pointer for its lifetime and releases it
	// through the process-wide allocator, so a CRT buffer is foreign.
	char* actualKey = static_cast<char*>(GlobalAllocator()->Alloc(size));
	if (!actualKey)
		return -1;
	memcpy(actualKey, AUTOCVAR_PREFIX, AUTOCVAR_PREFIX_SIZE);
	strcpy_s(actualKey + AUTOCVAR_PREFIX_SIZE, size - AUTOCVAR_PREFIX_SIZE, key);

	const char* defaultValueCopy = DuplicateDelegatedString(defaultValue);
	const char* descCopy = DuplicateDelegatedString(desc);
	if (!defaultValueCopy || !descCopy) {
		GlobalAllocator()->Free(actualKey);
		FreeDelegatedString(const_cast<char*>(defaultValueCopy));
		FreeDelegatedString(const_cast<char*>(descCopy));
		return -1;
	}

	// if exists, bail
	if (IsR1ODedicatedServer()) {
		if (CCVar_FindVar(cvarinterface, actualKey)) {
			GlobalAllocator()->Free(actualKey);
			FreeDelegatedString(const_cast<char*>(defaultValueCopy));
			FreeDelegatedString(const_cast<char*>(descCopy));
			return 0;
		}
		RegisterR1ODediConVar(actualKey, defaultValue, FCVAR_GAMEDLL, desc);
		GlobalAllocator()->Free(actualKey);
		FreeDelegatedString(const_cast<char*>(defaultValueCopy));
		FreeDelegatedString(const_cast<char*>(descCopy));
		return 0;
	}

	if (OriginalCCVar_FindVar(cvarinterface, actualKey)) {
		GlobalAllocator()->Free(actualKey);
		FreeDelegatedString(const_cast<char*>(defaultValueCopy));
		FreeDelegatedString(const_cast<char*>(descCopy));
		return 0;
	}

	// register cvar
	ConVarR1* RegisterConVar(const char* name, const char* value, int flags, const char* helpString);
	RegisterConVar(
		actualKey,
		defaultValueCopy,
		FCVAR_GAMEDLL,
		descCopy
	);
	// ConCommandBase::Create stores the supplied name/help pointers instead of
	// copying them.  AutoCVar's strings therefore have the same process
	// lifetime as the ConVar itself.  In particular, do not free actualKey
	// here: a later allocation can otherwise reuse the buffer and silently
	// rename this ConVar to an unrelated command in lookup/autocomplete.

	return 0;
}

int TableToKeyValues(HSQUIRRELVM v, KeyValues* kv, int tableN) {
	sq_push(v, tableN);
	sq_pushnull(v);

	while (SQ_SUCCEEDED(sq_next(v, -2))) {
		if (sq_gettype(v, -2) != OT_STRING) {
			sq_pop(v, 4);
			sq_throwerror(v, "Table keys must be strings");
			return -1;
		}

		const char* key = nullptr;
		sq_getstring(v, -2, &key);

		switch (sq_gettype(v, -1)) {
		case OT_BOOL:
			{
				SQBool val;
				sq_getbool(nullptr, v, -1, &val);
				kv->SetInt(key, val);
				break;
			}
		case OT_INTEGER:
			{
				SQInteger val;
				sq_getinteger(nullptr, v, -1, &val);
				kv->SetInt(key, val);
				break;
			}
		case OT_FLOAT:
			{
				SQFloat val;
				sq_getfloat(nullptr, v, -1, &val);
				kv->SetFloat(key, val);
				break;
			}
		case OT_STRING:
			{
				const SQChar* val = nullptr;
				sq_getstring(v, -1, &val);
				kv->SetString(key, val);
				break;
			}
		case OT_TABLE:
			{
				KeyValues* subKV = kv->FindKey(key, true);
				if (SQ_FAILED(TableToKeyValues(v, subKV, -1)))
					return -1;
				break;
			}
		default:
			{
				sq_pop(v, 4);
				sq_throwerror(v, "Unknown value type in table");
				return -1;
			}
		}

		sq_pop(v, 2);
	}

	sq_pop(v, 2);

	return 0;
}

int SendMenu(HSQUIRRELVM v) {
	if (IsR1ODedicatedServer())
		return 0;

	void* player = sq_getentity(v, 2);
	if (!player) {
		sq_throwerror(v, "Passed instance is not a valid entity");
		return -1;
	}

	SQInteger type = -1;
	sq_getinteger(nullptr, v, 3, &type);
	if (type < 0 || type > 3) {
		sq_throwerror(v, "Invalid menu type [0-3]");
		return -1;
	}

	KeyValues* kv = new KeyValues("menu");
	SQObjectType dataType = sq_gettype(v, 4);
	if (dataType == OT_TABLE) {
		if (SQ_FAILED(TableToKeyValues(v, kv, 4))) {
			kv->DeleteThis();
			return -1;
		}
	} else {
		kv->DeleteThis();
		// sq_throwerror(v, "Invalid data type, need string for KV file path or table for direct KV");
		// maybe later
		sq_throwerror(v, "Invalid data type, need table for KV data");
		return -1;
	}

	auto engine = IsDedicatedServer() ? G_engine_ds : G_engine;
	void(__fastcall* pluginSendMessage)(void* thisptr, void* entity, int type, KeyValues* kv, void* plugin) = (decltype(pluginSendMessage))(engine + (IsDedicatedServer() ? 0x63140 : 0xF24E0));

	char fakePluginThisptr[64];
	memset(fakePluginThisptr, 0, sizeof(fakePluginThisptr));

	auto edict = *reinterpret_cast<void**>(reinterpret_cast<__int64>(player) + 64);
	pluginSendMessage(&fakePluginThisptr, edict, type, kv, (void*)1);

	kv->DeleteThis();
	return 0;
}

using SQFinalize_t = __int64(__fastcall*)(void* self);
static SQFinalize_t  oSQFinalize;

__int64 __fastcall SQFinalize_Seatbelt(void* self) {
    // _class is at +56 (0x38)
    void* klass = *reinterpret_cast<void**>((char*)self + 56);
    if (!klass) {
        static std::atomic<uint64_t> hits{0};
        if ((hits.fetch_add(1, std::memory_order_relaxed) & 0xFF) == 0) {
            Warning("[sq] Seatbelt: Finalize on %p with null _class — skipping (count=%llu). THIS SHOULD NEVER HAPPEN. If you see this, ping @r3muxd on Discord immediately!\n", self, hits.load());
        }
        return 0; // treat as already-finalized
    }

    return oSQFinalize(self);
}

using DedicatedScriptErrorHandlerFn = uint64_t(__fastcall*)(uintptr_t);
static DedicatedScriptErrorHandlerFn s_DedicatedScriptErrorHandlerOriginal = nullptr;

static uint64_t __fastcall DedicatedScriptErrorHandler(uintptr_t sqstate)
{
	ScriptErrorTelemetry::BeginErrorBlock(ScriptErrorTelemetry::VmContext::Server);
	const uint64_t result = s_DedicatedScriptErrorHandlerOriginal(sqstate);
	ScriptErrorTelemetry::EndErrorBlock(ScriptErrorTelemetry::VmContext::Server);
	return result;
}

// Function to initialize all SQVM functions
bool GetSQVMFuncs() {
	ScriptErrorTelemetry::Initialize();
	static bool initialized = false;
	if (initialized) return true;
	const bool r1oFakeDedi = IsR1ODedicatedServer();
	const uintptr_t engine = MainEngineBase();
	if (!r1oFakeDedi) {
		g_pClientArray = (CBaseClient*)(G_engine + 0x2966340);
		g_pClientArrayDS = (CBaseClientDS*)(G_engine_ds + 0x1C89C48);
	}
	else {
		g_pClientArray = nullptr;
		g_pClientArrayDS = nullptr;
	}

#if BUILD_DEBUG
	if (!G_vscript) MessageBoxW(0, L"G_launcher is null in GetSQVMFuncs", L"ASSERT!!!", MB_ICONERROR | MB_OK);
#endif

	if (!r1oFakeDedi && engine && MH_CreateHook(reinterpret_cast<void*>((engine + (IsDedicatedServer() ? 0xAA4A0 : 0x14BB10))), &Hk_CHostState__State_GameShutdown, reinterpret_cast<void**>(&oGameShutDown)) != MH_OK) {
			Msg("Failed to hook CHostState__State_GameShutdown\n");
	}


	if (!r1oFakeDedi)
		MH_CreateHook((LPVOID)(G_launcher + 0x4D6D0), &SQFinalize_Seatbelt, (LPVOID*)&oSQFinalize);

	uintptr_t baseAddress = G_vscript;
	if (G_server) {
		if (!r1oFakeDedi && IsDedicatedServer()) {
			// dedicated+0x3A6C0 is the script-error handler entry. The old
			// +0x3A5E0 target landed four bytes into the RIP-relative instruction
			// at +0x3A5DC and corrupted the instruction when MinHook installed the
			// detour.
			constexpr uintptr_t kDedicatedScriptErrorHandlerRva = 0x3A6C0;
			constexpr unsigned char kExpectedPrologue[] = {
				0x40, 0x53, 0x55, 0x57, 0x41, 0x54, 0x41, 0x55,
				0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0xD0
			};
			void* target = reinterpret_cast<void*>(G_launcher + kDedicatedScriptErrorHandlerRva);
			if (std::memcmp(target, kExpectedPrologue, sizeof(kExpectedPrologue)) != 0) {
				Warning("R1Delta: dedicated script-error handler prologue mismatch; telemetry hook not installed.\n");
			} else {
				const MH_STATUS status = MH_CreateHook(target, &DedicatedScriptErrorHandler,
					reinterpret_cast<void**>(&s_DedicatedScriptErrorHandlerOriginal));
				if (status != MH_OK && status != MH_ERROR_ALREADY_CREATED)
					Warning("R1Delta: failed to create dedicated script-error telemetry hook (%s).\n", MH_StatusToString(status));
			}
		}

		struct PlayerPersistenceHook {
			uintptr_t target;
			void* detour;
			void** original;
			const char* name;
		};
		const PlayerPersistenceHook hooks[] = {
			{ G_server + 0x50EA30, reinterpret_cast<void*>(&CPlayer__SetXPRebuild),
				reinterpret_cast<void**>(&CPlayer__SetXPRebuildOrig), "CPlayer::SetXP" },
			{ G_server + 0x50E740, reinterpret_cast<void*>(&Script_XPChanged_Rebuild),
				reinterpret_cast<void**>(&CPlayer__Script_XP_ChangedOrig), "CPlayer::XPChanged" },
			{ G_server + 0x50E7A0, reinterpret_cast<void*>(&Script_GenChanged_Rebuild),
				reinterpret_cast<void**>(&CPlayer__Script_Gen_Changed_Orig), "CPlayer::GenChanged" },
		};
		for (const PlayerPersistenceHook& hook : hooks) {
			const R1OTfoImmediateHookResult result =
				InstallR1OTfoImmediateHook(hook.target, hook.detour, hook.original);
			if (!R1OTfoImmediateHookInstalled(result)) {
				Warning(
					"Failed to install %s hook (create=%d enable=%d)\n",
					hook.name,
					static_cast<int>(result.create),
					static_cast<int>(result.enable));
			}
		}
	}
	if (!r1oFakeDedi) {
	sq_compile = reinterpret_cast<sq_compile_t>(baseAddress + (IsDedicatedServer() ? 0x14A50 : 0x14970));
	sq_compilebuffer = reinterpret_cast<sq_compilebuffer_t>(baseAddress + (IsDedicatedServer() ? 0x1A6C0 : 0x1A5E0));
	base_getroottable = reinterpret_cast<base_getroottable_t>(baseAddress + (IsDedicatedServer() ? 0x56520 : 0x56440));
	sq_call = reinterpret_cast<sq_call_t>(baseAddress + (IsDedicatedServer() ? 0x18D20 : 0x18C40));
	sq_newslot = reinterpret_cast<sq_newslot_t>(baseAddress + (IsDedicatedServer() ? 0x17340 : 0x17260));
	sq_pop = reinterpret_cast<SQVM_Pop_t>(baseAddress + (IsDedicatedServer() ? 0x2BD40 : 0x2BC60));
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
	sq_settop = reinterpret_cast<sq_settop_t>(baseAddress + (IsDedicatedServer() ? 0x171E0 : 0x017100));
	sq_removetwo = reinterpret_cast<sq_removetwo_t>(baseAddress + (IsDedicatedServer() ? 0x2BBF0 : 0x2bb10));
	RunCallback = reinterpret_cast<RunCallback_t>(baseAddress + (IsDedicatedServer() ? 0x89C0 : 0x89A0));
	CSquirrelVM__RegisterGlobalConstantInt = reinterpret_cast<CSquirrelVM__RegisterGlobalConstantInt_t>(baseAddress + (IsDedicatedServer() ? 0xA6A0 : 0xA680));
	CSquirrelVM__GetEntityFromInstance = reinterpret_cast<CSquirrelVM__GetEntityFromInstance_t>(baseAddress + (IsDedicatedServer() ? 0x9950 : 0x9930));
	AddSquirrelReg = reinterpret_cast<AddSquirrelReg_t>(baseAddress + (IsDedicatedServer() ? 0x8E70 : 0x8E50));
	sq_GetEntityConstant_CBaseEntity = reinterpret_cast<sq_GetEntityConstant_CBaseEntity_t>(G_client + 0x2EF850);

	MH_CreateHook((LPVOID)(G_client + 0x2BECF0), OnCreateClientScriptVM, (LPVOID*)&oOnCreateClientScriptVM);
	MH_CreateHook((LPVOID)(G_client + 0x2E4AD0), OnCreateUIScriptVM, (LPVOID*)&oOnCreateUIScriptVM);
	if (G_server) MH_CreateHook((LPVOID)(G_server + 0x276600), OnCreateServerScriptVM, (LPVOID*)&oOnCreateServerScriptVM);
	MH_EnableHook(MH_ALL_HOOKS);
	}

	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_CLIENT,
		"GetPersistentString",
		(SQFUNCTION)Script_ClientGetPersistentData,
		".ss", // String
		3,      // Expects 2 parameters
		"string",    // Returns a string
		"string key, string defaultValue",
		"Get a persistent data value"
	);

	REGISTER_SCRIPT_FUNCTION(
		 SCRIPT_CONTEXT_UI,
		"Localize",
		(SQFUNCTION)Script_Localize,
		".s", // String
		2,      // Expects 2 parameters
		"string",    // Returns a string
		"string locKey",
		"Localize string"
	);

	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_SERVER,
		"SendChatMsg",
		(SQFUNCTION)SendChatWrapper,
		"..isbb", // String
		-4,      // Expects at least 4 parameters
		"void",
		"entity player / bool = true (broadcast) / array of entities for multiple recipients, int fromPlayerIndex, string text, bool isTeam = false, bool isDead = false",
		"Send a chat message"
	);

	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_SERVER,
		"SendShowMenu",
		(SQFUNCTION)SendShowMenu,
		".Isii",  // this, recipients, string, int keysMask, int seconds
		5,        // IMPORTANT: expect 5 total (includes `this`)
		"void",
		"entity|array|bool recipients, string text, int keysMask, int secondsToStayOpen",
		"Send ShowMenu (CS:S/EP2) with automatic chunking and full keys mask."
	);


	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_CLIENT,
		"Localize",
		(SQFUNCTION)Script_Localize,
		".s", // String
		2,      // Expects 2 parameters
		"string",    // Returns a string
		"string locKey",
		"Localize string"
	);

	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_SERVER,
		"Localize",
		(SQFUNCTION)Script_Localize,
		".s", // String
		2,      // Expects 2 parameters
		"string",    // Returns a string
		"string locKey",
		"Localize string"
	);

	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_UI,
		"SendDiscordUI",
		(SQFUNCTION)SendDiscordUI,
		".", // String
		2,      // Expects 2 parameters
		"void",    // Returns a string
		"string levelName",
		"Send discord UI"
	);

	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_UI,
		"GetR1DVersion",
		(SQFUNCTION)GetR1DVersion,
		".", // String
		1,      // Expects 2 parameters
		"string",    // Returns a string
		"",
		"Get R1Delta version"
	);

	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_UI,
		"GetR1DDisplayVersion",
		(SQFUNCTION)GetR1DDisplayVersion,
		".",
		1,
		"string",
		"",
		"Get the human-readable R1Delta release label"
	);
	
	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_UI,
		"GetMinimumR1DVersion",
		(SQFUNCTION)GetMinimumR1DVersion,
		".", // String
		1,     
		"string",
		"",
		"Get r1d minimum server for filtering"
	);

	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_SERVER,
		"GetPlayerIP",
		(SQFUNCTION)Script_ServerGetPlayerIp,
		".I", // String
		2,
		"string",
		"",
		"Get player ip"
	);

	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_SERVER,
		"GetPlayerUserId",
		(SQFUNCTION)Script_ServerGetPlayerUserID,
		".I", // String
		2,
		"int",
		"",
		"Get player user id"
	);

	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_SERVER,
		"GetPlayerPlatformUserId",
		(SQFUNCTION)Script_ServerGetPlayerPlatformUserID,
		".I", // String
		2,
		"string",
		"",
		"Get player platform user id"
	);

	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_CLIENT,
		"SendDiscordClient",
		(SQFUNCTION)SendDiscordClient,
		"..", // String
		3,      // Expects 2 parameters
		"void",    // Returns a string
		"table data, bool init",
		"Send discord client"
	);

	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_CLIENT,
		"AddDamageNumber",
		(SQFUNCTION)Script_AddDamageNumber,
		".ffffbbi",
		8,
		"void",
		"float damage, float x, float y, float z, bool isCritical, bool isKillShot, int sourceID",
		"Adds a floating damage number to the HUD."
	);

	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_UI,
		"OpenDiscordURL",
		(SQFUNCTION)OpenDiscordURL,
		".", // String
		1,      // Expects 2 parameters
		"void",    // Returns a string
		"",
		"Open a discord URL"
	);

	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_UI,
		"GetMods",
		(SQFUNCTION)GetMods,
		".", // String
		1,      // Expects 2 parameters
		"array",
		"",
		"Get installed mods"
	);



	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_UI,
		"GetAddonsPath",
		(SQFUNCTION)GetAddonsPath,
		"..",
		2,
		"string",    // Returns a string
		"",
		"Get path to addons and open it in explorer"
	);

	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_UI,
		"GetPersistentString",
		(SQFUNCTION)Script_ClientGetPersistentData,
		".ss", // String
		3,      // Expects 2 parameters
		"string",    // Returns a string
		"string key, string defaultValue",
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


	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_UI,
		"UpdateAddons",
		(SQFUNCTION)UpdateAddons,
		".ib", // String
		3,      // Expects 2 parameters
		"int",    // Returns a string
		"int index, bool enabled",
		"Updates the selected addons"
	);

	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_UI,
		"DispatchServerListReq",
		(SQFUNCTION)DispatchServerListReq,
		".", // String
		1,      // Expects 2 parameters
		"void",    // Returns a string
		"",
		"Starts fetching server list from MS"
	);

	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_UI,
		"PollServerList",
		(SQFUNCTION)PollServerList,
		".", // String
		1,      // Expects 2 parameters
		"table",    // Returns a string
		"",
		"Gets server list"
	);

	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_SERVER,
		"SendDataToCppServer",
		(SQFUNCTION)GetServerHeartbeat,
		".t",
		2,
		"void",
		"table data",
		"Send data to the cpp server"
	);

	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_SERVER,
		"AddXpServer",
		(SQFUNCTION)AddXp,
		".Ii",
		3,
		"void",
		"entity player, int xp",
		"Add XP"
	);


	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_SERVER,
		"SetIsPlayingRanked",
		(SQFUNCTION)SetRanked,
		".Ib",
		3,
		"void",
		"entity player, bool isPlayingRanked",
		"Set player ranked"
	);

	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_SERVER,
		"SetGen",
		(SQFUNCTION)SetGenSQ,
		".Ii",
		3,
		"void",
		"entity player, int gen",
		"Set player gen"
	);

	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_SERVER,
		"AutoCVar",
		(SQFUNCTION)AutoCVar,
		".sss",
		-2,
		"void",
		"string name, string defaultValue = \"\", string description = \"\"",
		"Create a script-used ConVar if it doesn't already exist."
	);

	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_SERVER,
		"SendMenu",
		(SQFUNCTION)SendMenu,
		".Ist",
		4,
		"void",
		"entity player, int type, table data",
		"Send a plugin menu to this player."
	);

	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_SERVER, // Available in client script contexts
		"GetPersistentStringForClient",
		Script_ServerGetPersistentUserDataKVString,
		"..ss", // String
		4,      // Expects 3 parameters
		"string",    // Returns an int (idk if i is the right char for this lmao)
		"string key, string defaultValue",
		"Get a persistent userinfo value"
	);

	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_SERVER, // Available in client script contexts
		"SetActiveBurnCardIndexForPlayer",
		Script_Server_SetActiveBurnCardIndex,
		".Ii", // String
		3,      // Expects 2 parameters
		"void",    // Returns an int (idk if i is the right char for this lmao)
		"entity player, int index",
		"Get a persistent userinfo value"
	);

	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_SERVER, // Available in client script contexts
		"GetActiveBurnCardIndex",
		Script_Server_GetActiveBurnCardIndex,
		".I", // String
		2,      // Expects 2 parameters
		"int",    // Returns an int (idk if i is the right char for this lmao)
		"entity player",
		"Get a persistent userinfo value"
	);

	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_SERVER, // Available in client script contexts
		"SetPersistentStringForClient",
		Script_ServerSetPersistentUserDataKVString,
		"..ss", // String
		4,      // Expects 3 parameters
		"string",    // Returns an int (idk if i is the right char for this lmao)
		"string key, string value",
		"Set a persistent userinfo value (this does NOT replicate you will need to send the replication command)"
	);
	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_UI, // Available in client script contexts
		"GetLoadingStatusText",
		Script_GetLoadingStatusText,
		"..ss", // String
		0, 
		"string",
		"",
		"Get the loading progres status text from engine vgui."
	);
	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_SERVER, // Available in client script contexts
		"IsDedicated",
		Script_IsDedicated,
		"..ss", // String
		0,
		"bool",
		"",
		"Returns whether the the server is dedicated or listen."
	);
	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_CLIENT,
		"R1Delta_SetFFABased",
		(SQFUNCTION)Script_SetClientFfaBased,
		".b",
		2,
		"void",
		"bool enabled",
		"Synchronize native player relationship behavior with the replicated FFA state."
	);
	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_SERVER,
		"R1Delta_SetFFABased",
		(SQFUNCTION)Script_SetServerFfaBased,
		".b",
		2,
		"void",
		"bool enabled",
		"Synchronize native player relationship behavior with the authoritative FFA state."
	);
	REGISTER_SCRIPT_FUNCTION(
		SCRIPT_CONTEXT_UI, // Available in client script contexts
		"SquirrelNativeFunctionTest", (SQFUNCTION)SquirrelNativeFunctionTest, ".sifb", 0, "string", "string text, int a2, float a3, bool a4", "Test registering and calling native function in Squirrel.");

	initialized = true;
	return true;
}


using r1delta::script_variant::ABI;

namespace {

constexpr std::int32_t kMaxFunctionParameterCount = 4096;
constexpr DWORD kFakeServerVmFailureCode = 0xE0425356;

using FunctionDescriptorBytes = std::array<
	unsigned char,
	r1delta::script_variant::kFunctionDescriptorSize>;

struct FakeServerVM
{
	uintptr_t* vtable = nullptr;
};

struct FunctionCallbackState
{
	void* binding = nullptr;
	void* function = nullptr;
};

struct PersistentFunctionDescriptor
{
	alignas(void*) FunctionDescriptorBytes descriptor{};
	std::vector<std::int32_t> parameters;
	FunctionCallbackState callback;
};

struct PersistentClassDescriptor
{
	const void* source = nullptr;
	void* originalFunctionBase = nullptr;
	std::vector<FunctionDescriptorBytes> functions;
	std::vector<std::vector<std::int32_t>> parameters;
	std::vector<FunctionCallbackState> callbacks;
	bool complete = false;
};

struct PackedVariantRecord
{
	std::int32_t index;
	std::int32_t padding;
	ScriptVariant_t value;
};

static_assert(sizeof(ScriptVariant_t)
	== r1delta::script_variant::kScriptVariantSize);
static_assert(offsetof(ScriptVariant_t, m_type)
	== r1delta::script_variant::kScriptVariantTypeOffset);
static_assert(offsetof(ScriptVariant_t, m_flags)
	== r1delta::script_variant::kScriptVariantFlagsOffset);
static_assert(sizeof(PackedVariantRecord) == 0x18);
static_assert(offsetof(PackedVariantRecord, value) == 0x08);

static FakeServerVM g_fakeServerVm;
static void* g_realServerVm = nullptr;
static void* g_R1OTfoServerVm = nullptr;
static bool g_serverProxyFlag = false;
static std::array<
	uintptr_t,
	r1delta::script_variant::kR1VtableSlotCount>
	g_originalR1Destinations{};
static bool g_haveOriginalR1Destinations = false;
static std::map<const void*, std::unique_ptr<PersistentFunctionDescriptor>>
	g_functionDescriptorClones;
static std::map<const void*, std::unique_ptr<PersistentClassDescriptor>>
	g_classDescriptorClones;

[[noreturn]] static void FailFakeServerVm(const char* reason)
{
	Warning(
		"R1Delta: fatal fake server Squirrel VM ABI failure: %s\n",
		reason ? reason : "unknown");
	const ULONG_PTR arguments[] = {
		reinterpret_cast<ULONG_PTR>(reason),
	};
	RaiseException(
		kFakeServerVmFailureCode,
		EXCEPTION_NONCONTINUABLE,
		1,
		arguments);
	TerminateProcess(GetCurrentProcess(), kFakeServerVmFailureCode);
	__assume(0);
}

static void ReportFakeServerAdapterFailure(
	std::size_t sourceSlot,
	const char* reason)
{
	Warning(
		"R1Delta: fake server Squirrel VM source slot %zu rejected: %s\n",
		sourceSlot,
		reason ? reason : "unknown");
}

template <typename Function>
static Function OriginalR1Slot(std::size_t sourceSlot)
{
	if (sourceSlot >= g_originalR1Destinations.size()
		|| !g_originalR1Destinations[sourceSlot]) {
		FailFakeServerVm("missing original R1 vtable destination");
	}
	return reinterpret_cast<Function>(
		g_originalR1Destinations[sourceSlot]);
}

static bool ConvertVariantToR1(
	const ScriptVariant_t* source,
	ScriptVariant_t& destination)
{
	return source
		&& r1delta::script_variant::ConvertVariant(
			*source,
			ABI::R1O,
			ABI::R1,
			destination);
}

static void ReleaseR1ScratchVariant(
	void* realVm,
	ScriptVariant_t& variant)
{
	using ReleaseValueFn = __int64(__fastcall*)(
		void*,
		ScriptVariant_t*);
	OriginalR1Slot<ReleaseValueFn>(57)(realVm, &variant);
}

static bool TransferR1Output(
	void* realVm,
	ScriptVariant_t& source,
	ScriptVariant_t* destination)
{
	if (!destination)
		return false;
	ScriptVariant_t converted{};
	if (!r1delta::script_variant::ConvertVariant(
			source,
			ABI::R1,
			ABI::R1O,
			converted)) {
		ReleaseR1ScratchVariant(realVm, source);
		return false;
	}
	*destination = converted;
	return true;
}

static bool ConvertFieldTypeToR1(
	std::int32_t source,
	std::int32_t& destination)
{
	if (source < (std::numeric_limits<std::int16_t>::min)()
		|| source > (std::numeric_limits<std::int16_t>::max)()) {
		return false;
	}
	std::int16_t converted{};
	if (!r1delta::script_variant::ConvertType(
			static_cast<std::int16_t>(source),
			ABI::R1O,
			ABI::R1,
			converted)) {
		return false;
	}
	destination = converted;
	return true;
}

using DescriptorBindingFn = __int64(__fastcall*)(
	void*,
	void*,
	ScriptVariant_t*,
	__int64,
	ScriptVariant_t*);

static __int64 __fastcall ServerFunctionBindingAdapter(
	void* callbackState,
	void* context,
	ScriptVariant_t* arguments,
	__int64 argumentCount,
	ScriptVariant_t* returnValue)
{
	auto* state = static_cast<FunctionCallbackState*>(callbackState);
	if (!state || !state->binding || argumentCount < 0
		|| argumentCount > kMaxFunctionParameterCount
		|| (argumentCount && !arguments)) {
		ReportFakeServerAdapterFailure(33, "invalid descriptor callback");
		return 0;
	}

	std::vector<ScriptVariant_t> convertedArguments(
		static_cast<std::size_t>(argumentCount));
	for (__int64 i = 0; i < argumentCount; ++i) {
		if (!r1delta::script_variant::ConvertVariant(
				arguments[i],
				ABI::R1,
				ABI::R1O,
				convertedArguments[static_cast<std::size_t>(i)])) {
			ReportFakeServerAdapterFailure(
				33,
				"unsupported R1 callback argument type");
			return 0;
		}
	}

	ScriptVariant_t convertedReturn{};

	const __int64 result =
		reinterpret_cast<DescriptorBindingFn>(state->binding)(
			state->function,
			context,
			convertedArguments.empty()
				? nullptr
				: convertedArguments.data(),
			argumentCount,
			returnValue ? &convertedReturn : nullptr);

	if (returnValue) {
		ScriptVariant_t r1Return{};
		if (!r1delta::script_variant::ConvertVariant(
				convertedReturn,
				ABI::R1O,
				ABI::R1,
				r1Return)) {
			ReportFakeServerAdapterFailure(
				33,
				"unsupported R1O callback return type");
			return 0;
		}
		*returnValue = r1Return;
	}
	return result;
}

static bool CloneFunctionDescriptor(
	const void* source,
	FunctionDescriptorBytes& destination,
	std::vector<std::int32_t>& parameterStorage,
	FunctionCallbackState& callback)
{
	if (!source)
		return false;

	memcpy(
		destination.data(),
		source,
		r1delta::script_variant::kFunctionDescriptorSize);
	const auto sourceBytes =
		static_cast<const unsigned char*>(source);
	const std::uint64_t flags = *reinterpret_cast<const std::uint64_t*>(
		sourceBytes
			+ r1delta::script_variant::kFunctionDescriptorFlagsOffset);
	if (!r1delta::script_variant::ShouldAdaptTypedRegistration(flags)) {
		parameterStorage.clear();
		callback = {};
		return true;
	}

	const std::int32_t parameterCount =
		*reinterpret_cast<const std::int32_t*>(
			sourceBytes
				+ r1delta::script_variant::
					kFunctionDescriptorParameterCountOffset);
	if (parameterCount < 0
		|| parameterCount > kMaxFunctionParameterCount) {
		return false;
	}

	callback.binding = *reinterpret_cast<void* const*>(
		sourceBytes
			+ r1delta::script_variant::kFunctionDescriptorBindingOffset);
	callback.function = *reinterpret_cast<void* const*>(
		sourceBytes
			+ r1delta::script_variant::kFunctionDescriptorFunctionOffset);
	if (!callback.binding) {
		return false;
	}
	*reinterpret_cast<void**>(
		destination.data()
			+ r1delta::script_variant::kFunctionDescriptorBindingOffset) =
		reinterpret_cast<void*>(&ServerFunctionBindingAdapter);
	*reinterpret_cast<void**>(
		destination.data()
			+ r1delta::script_variant::kFunctionDescriptorFunctionOffset) =
		&callback;

	const auto* sourceParameters = *reinterpret_cast<
		const std::int32_t* const*>(
			sourceBytes
				+ r1delta::script_variant::
					kFunctionDescriptorParameterVectorBaseOffset);
	if (parameterCount) {
		if (!sourceParameters)
			return false;
		parameterStorage.resize(static_cast<std::size_t>(parameterCount));
		for (std::int32_t i = 0; i < parameterCount; ++i) {
			if (!ConvertFieldTypeToR1(
					sourceParameters[i],
					parameterStorage[static_cast<std::size_t>(i)])) {
				return false;
			}
		}
		*reinterpret_cast<std::int32_t**>(
			destination.data()
				+ r1delta::script_variant::
					kFunctionDescriptorParameterVectorBaseOffset) =
			parameterStorage.data();
	}

	const std::int32_t sourceReturnType =
		*reinterpret_cast<const std::int32_t*>(
			sourceBytes
				+ r1delta::script_variant::
					kFunctionDescriptorReturnTypeOffset);
	std::int32_t convertedReturnType{};
	if (!ConvertFieldTypeToR1(
			sourceReturnType,
			convertedReturnType)) {
		return false;
	}
	*reinterpret_cast<std::int32_t*>(
		destination.data()
			+ r1delta::script_variant::
				kFunctionDescriptorReturnTypeOffset) =
		convertedReturnType;
	return true;
}

static void* GetFunctionDescriptorClone(const void* source)
{
	if (!source)
		return nullptr;
	const auto existing = g_functionDescriptorClones.find(source);
	if (existing != g_functionDescriptorClones.end())
		return existing->second->descriptor.data();

	auto clone = std::make_unique<PersistentFunctionDescriptor>();
	if (!CloneFunctionDescriptor(
			source,
			clone->descriptor,
			clone->parameters,
			clone->callback)) {
		return nullptr;
	}
	void* result = clone->descriptor.data();
	g_functionDescriptorClones.emplace(source, std::move(clone));
	return result;
}

static void* GetClassDescriptorClone(const void* source)
{
	if (!source)
		return nullptr;
	const auto existing = g_classDescriptorClones.find(source);
	if (existing != g_classDescriptorClones.end()) {
		return existing->second->complete
			? const_cast<void*>(source)
			: nullptr;
	}

	auto adapter = std::make_unique<PersistentClassDescriptor>();
	adapter->source = source;
	g_classDescriptorClones.emplace(source, std::move(adapter));
	auto& stored = *g_classDescriptorClones.find(source)->second;

	const auto* sourceBytes =
		static_cast<const unsigned char*>(source);
	const void* sourceBase = *reinterpret_cast<void* const*>(
		sourceBytes + r1delta::script_variant::kClassDescriptorBaseOffset);
	if (sourceBase && !GetClassDescriptorClone(sourceBase)) {
		g_classDescriptorClones.erase(source);
		return nullptr;
	}

	const std::int32_t functionCount =
		*reinterpret_cast<const std::int32_t*>(
			sourceBytes
				+ r1delta::script_variant::kClassDescriptorFunctionCountOffset);
	if (functionCount < 0
		|| functionCount > kMaxFunctionParameterCount) {
		g_classDescriptorClones.erase(source);
		return nullptr;
	}
	const auto* sourceFunctions = *reinterpret_cast<
		const unsigned char* const*>(
			sourceBytes
				+ r1delta::script_variant::
					kClassDescriptorFunctionVectorBaseOffset);
	stored.originalFunctionBase =
		const_cast<unsigned char*>(sourceFunctions);
	if (functionCount) {
		if (!sourceFunctions) {
			g_classDescriptorClones.erase(source);
			return nullptr;
		}
		stored.functions.resize(static_cast<std::size_t>(functionCount));
		stored.parameters.resize(static_cast<std::size_t>(functionCount));
		stored.callbacks.resize(static_cast<std::size_t>(functionCount));
		for (std::int32_t i = 0; i < functionCount; ++i) {
			const auto index = static_cast<std::size_t>(i);
			if (!CloneFunctionDescriptor(
					sourceFunctions
						+ index
							* r1delta::script_variant::
								kFunctionDescriptorSize,
					stored.functions[index],
					stored.parameters[index],
					stored.callbacks[index])) {
				g_classDescriptorClones.erase(source);
				return nullptr;
			}
		}
		auto* writableSource =
			const_cast<unsigned char*>(sourceBytes);
		*reinterpret_cast<FunctionDescriptorBytes**>(
			writableSource
				+ r1delta::script_variant::
					kClassDescriptorFunctionVectorBaseOffset) =
			stored.functions.data();
	}
	stored.complete = true;
	return const_cast<void*>(source);
}

static __int64 __fastcall ExecuteFunctionAdapter(
	void* realVm,
	void* function,
	ScriptVariant_t* arguments,
	std::uint32_t argumentCount,
	ScriptVariant_t* returnValue,
	void* scope)
{
	using Function = __int64(__fastcall*)(
		void*,
		void*,
		ScriptVariant_t*,
		std::uint32_t,
		ScriptVariant_t*,
		void*);
	if (argumentCount > kMaxFunctionParameterCount
		|| (argumentCount && !arguments)) {
		ReportFakeServerAdapterFailure(32, "invalid argument array");
		if (returnValue)
			*returnValue = {};
		return -1;
	}
	std::vector<ScriptVariant_t> convertedArguments(argumentCount);
	for (std::uint32_t i = 0; i < argumentCount; ++i) {
		if (!ConvertVariantToR1(
				&arguments[i],
				convertedArguments[i])) {
			ReportFakeServerAdapterFailure(
				32,
				"unsupported R1O argument type");
			if (returnValue)
				*returnValue = {};
			return -1;
		}
	}
	ScriptVariant_t convertedReturn{};
	const __int64 result = OriginalR1Slot<Function>(32)(
		realVm,
		function,
		convertedArguments.empty()
			? nullptr
			: convertedArguments.data(),
		argumentCount,
		returnValue ? &convertedReturn : nullptr,
		scope);
	if (result == -1 && returnValue)
		*returnValue = {};
	if (result != -1 && returnValue
		&& !TransferR1Output(
			realVm,
			convertedReturn,
			returnValue)) {
		ReportFakeServerAdapterFailure(
			32,
			"unsupported R1 return type");
		*returnValue = {};
		return -1;
	}
	return result;
}

static __int64 __fastcall RegisterFunctionAdapter(
	void* realVm,
	const void* descriptor)
{
	using Function = __int64(__fastcall*)(void*, const void*);
	void* clone = GetFunctionDescriptorClone(descriptor);
	if (!clone) {
		ReportFakeServerAdapterFailure(
			33,
			"could not clone function descriptor");
		return 0;
	}
	return OriginalR1Slot<Function>(33)(realVm, clone);
}

static bool __fastcall RegisterClassAdapter(
	void* realVm,
	const void* descriptor)
{
	using Function = bool(__fastcall*)(void*, const void*);
	void* clone = GetClassDescriptorClone(descriptor);
	if (!clone) {
		ReportFakeServerAdapterFailure(
			34,
			"could not clone class descriptor");
		return false;
	}
	return OriginalR1Slot<Function>(34)(realVm, clone);
}

static void* __fastcall RegisterInstanceAdapter(
	void* realVm,
	const void* descriptor,
	void* instance)
{
	using Function = void*(__fastcall*)(void*, const void*, void*);
	void* clone = GetClassDescriptorClone(descriptor);
	if (descriptor && !clone) {
		ReportFakeServerAdapterFailure(
			36,
			"could not clone instance class descriptor");
		return nullptr;
	}
	return OriginalR1Slot<Function>(36)(realVm, clone, instance);
}

static void* __fastcall GetInstanceValueAdapter(
	void* realVm,
	void* instance,
	const void* expectedType)
{
	using Function = void*(__fastcall*)(void*, void*, const void*);
	void* clone = GetClassDescriptorClone(expectedType);
	if (expectedType && !clone) {
		ReportFakeServerAdapterFailure(
			41,
			"could not clone expected class descriptor");
		return nullptr;
	}
	return OriginalR1Slot<Function>(41)(realVm, instance, clone);
}

template <std::size_t SourceSlot>
static __int64 CreateVariantOutputAdapter(
	void* realVm,
	ScriptVariant_t* output)
{
	using Function = __int64(__fastcall*)(void*, ScriptVariant_t*);
	if (!output)
		FailFakeServerVm("null mandatory variant output");
	ScriptVariant_t scratch{};
	const __int64 result =
		OriginalR1Slot<Function>(SourceSlot)(realVm, &scratch);
	if (!TransferR1Output(realVm, scratch, output))
		FailFakeServerVm("could not translate mandatory variant output");
	return result;
}

static bool __fastcall ArrayAppendAdapter(
	void* realVm,
	void* array,
	const ScriptVariant_t* value)
{
	using Function = bool(__fastcall*)(
		void*,
		void*,
		const ScriptVariant_t*);
	ScriptVariant_t scratch{};
	if (!ConvertVariantToR1(value, scratch)) {
		ReportFakeServerAdapterFailure(
			46,
			"unsupported R1O array value type");
		return false;
	}
	return OriginalR1Slot<Function>(46)(realVm, array, &scratch);
}

static bool __fastcall GetArrayValueAdapter(
	void* realVm,
	void* array,
	std::int32_t index,
	ScriptVariant_t* output)
{
	using Function = bool(__fastcall*)(
		void*,
		void*,
		std::int32_t,
		ScriptVariant_t*);
	if (!output)
		return false;
	ScriptVariant_t scratch{};
	const bool result =
		OriginalR1Slot<Function>(48)(realVm, array, index, &scratch);
	if (result && !TransferR1Output(realVm, scratch, output)) {
		ReportFakeServerAdapterFailure(
			48,
			"unsupported R1 array output type");
		return false;
	}
	return result;
}

static bool __fastcall SetArrayValueAdapter(
	void* realVm,
	void* array,
	std::uint32_t index,
	const ScriptVariant_t* value)
{
	using Function = bool(__fastcall*)(
		void*,
		void*,
		std::uint32_t,
		const ScriptVariant_t*);
	ScriptVariant_t scratch{};
	if (!ConvertVariantToR1(value, scratch)) {
		ReportFakeServerAdapterFailure(
			49,
			"unsupported R1O array input type");
		return false;
	}
	return OriginalR1Slot<Function>(49)(
		realVm,
		array,
		index,
		&scratch);
}

static bool __fastcall SetValueAdapter(
	void* realVm,
	void* scope,
	const char* key,
	const ScriptVariant_t* value)
{
	using Function = bool(__fastcall*)(
		void*,
		void*,
		const char*,
		const ScriptVariant_t*);
	ScriptVariant_t scratch{};
	if (!ConvertVariantToR1(value, scratch)) {
		ReportFakeServerAdapterFailure(
			50,
			"unsupported R1O keyed input type");
		return false;
	}
	return OriginalR1Slot<Function>(50)(
		realVm,
		scope,
		key,
		&scratch);
}

static bool __fastcall GetValueAdapter(
	void* realVm,
	void* scope,
	const char* key,
	ScriptVariant_t* output)
{
	using Function = bool(__fastcall*)(
		void*,
		void*,
		const char*,
		ScriptVariant_t*);
	if (!output)
		return false;
	ScriptVariant_t scratch{};
	const bool result =
		OriginalR1Slot<Function>(56)(realVm, scope, key, &scratch);
	if (result && !TransferR1Output(realVm, scratch, output)) {
		ReportFakeServerAdapterFailure(
			56,
			"unsupported R1 keyed output type");
		return false;
	}
	return result;
}

static __int64 __fastcall ReleaseValueAdapter(
	void* realVm,
	ScriptVariant_t* value)
{
	using Function = __int64(__fastcall*)(void*, ScriptVariant_t*);
	ScriptVariant_t scratch{};
	if (!ConvertVariantToR1(value, scratch)) {
		ReportFakeServerAdapterFailure(
			57,
			"unsupported R1O release type");
		return 0;
	}
	const __int64 result =
		OriginalR1Slot<Function>(57)(realVm, &scratch);
	ScriptVariant_t released{};
	if (!r1delta::script_variant::ConvertVariant(
			scratch,
			ABI::R1,
			ABI::R1O,
			released)) {
		FailFakeServerVm("could not propagate released variant state");
	}
	*value = released;
	return result;
}

static bool __fastcall TransitiveVariantOutputAdapter(
	void* realVm,
	ScriptVariant_t* output,
	void* argument3,
	void* argument4)
{
	using Function = bool(__fastcall*)(
		void*,
		ScriptVariant_t*,
		void*,
		void*);
	if (!output)
		return false;
	ScriptVariant_t scratch{};
	const bool result = OriginalR1Slot<Function>(59)(
		realVm,
		&scratch,
		argument3,
		argument4);
	if (result && !TransferR1Output(realVm, scratch, output)) {
		ReportFakeServerAdapterFailure(
			59,
			"unsupported transitive R1 output type");
		return false;
	}
	return result;
}

static bool __fastcall DirectVariantOutputAdapter(
	void* realVm,
	ScriptVariant_t* output,
	void* argument3,
	bool argument4)
{
	using Function = bool(__fastcall*)(
		void*,
		ScriptVariant_t*,
		void*,
		bool);
	if (!output)
		return false;
	ScriptVariant_t scratch{};
	const bool result = OriginalR1Slot<Function>(61)(
		realVm,
		&scratch,
		argument3,
		argument4);
	if (result && !TransferR1Output(realVm, scratch, output)) {
		ReportFakeServerAdapterFailure(
			61,
			"unsupported direct R1 output type");
		return false;
	}
	return result;
}

static __int64 __fastcall PackedVariantArgumentsAdapter(
	void* realVm,
	void* argument2,
	std::int32_t argument3,
	void* argument4,
	const ScriptVariant_t* optionalValue,
	const PackedVariantRecord* records,
	std::int32_t recordCount)
{
	using Function = __int64(__fastcall*)(
		void*,
		void*,
		std::int32_t,
		void*,
		const ScriptVariant_t*,
		const PackedVariantRecord*,
		std::int32_t);
	if (recordCount < 0 || recordCount > kMaxFunctionParameterCount
		|| (recordCount && !records)) {
		FailFakeServerVm("invalid packed variant record array");
	}
	ScriptVariant_t optionalScratch{};
	const ScriptVariant_t* optionalR1 = nullptr;
	if (optionalValue) {
		if (!ConvertVariantToR1(optionalValue, optionalScratch))
			FailFakeServerVm("unsupported optional packed variant type");
		optionalR1 = &optionalScratch;
	}
	std::vector<PackedVariantRecord> convertedRecords(
		static_cast<std::size_t>(recordCount));
	for (std::int32_t i = 0; i < recordCount; ++i) {
		const auto index = static_cast<std::size_t>(i);
		convertedRecords[index] = records[index];
		if (!ConvertVariantToR1(
				&records[index].value,
				convertedRecords[index].value)) {
			FailFakeServerVm("unsupported packed record variant type");
		}
	}
	return OriginalR1Slot<Function>(82)(
		realVm,
		argument2,
		argument3,
		argument4,
		optionalR1,
		convertedRecords.empty() ? nullptr : convertedRecords.data(),
		recordCount);
}

static __int64 __fastcall FinalVariantArgumentAdapter(
	void* realVm,
	void* argument2,
	void* argument3,
	void* argument4,
	void* argument5,
	const ScriptVariant_t* value)
{
	using Function = __int64(__fastcall*)(
		void*,
		void*,
		void*,
		void*,
		void*,
		const ScriptVariant_t*);
	ScriptVariant_t scratch{};
	if (!ConvertVariantToR1(value, scratch))
		FailFakeServerVm("unsupported final variant argument type");
	return OriginalR1Slot<Function>(84)(
		realVm,
		argument2,
		argument3,
		argument4,
		argument5,
		&scratch);
}

static void __fastcall SetPerVmFlagAdapter(void*, bool value)
{
	g_serverProxyFlag = value;
}

static bool __fastcall GetPerVmFlagAdapter(void*)
{
	return g_serverProxyFlag;
}

static void* AdapterForSourceSlot(std::size_t sourceSlot)
{
	switch (sourceSlot) {
	case 32: return reinterpret_cast<void*>(&ExecuteFunctionAdapter);
	case 33: return reinterpret_cast<void*>(&RegisterFunctionAdapter);
	case 34: return reinterpret_cast<void*>(&RegisterClassAdapter);
	case 36: return reinterpret_cast<void*>(&RegisterInstanceAdapter);
	case 41: return reinterpret_cast<void*>(&GetInstanceValueAdapter);
	case 44:
		return reinterpret_cast<void*>(&CreateVariantOutputAdapter<44>);
	case 45:
		return reinterpret_cast<void*>(&CreateVariantOutputAdapter<45>);
	case 46: return reinterpret_cast<void*>(&ArrayAppendAdapter);
	case 48: return reinterpret_cast<void*>(&GetArrayValueAdapter);
	case 49: return reinterpret_cast<void*>(&SetArrayValueAdapter);
	case 50: return reinterpret_cast<void*>(&SetValueAdapter);
	case 52:
		return reinterpret_cast<void*>(&CreateVariantOutputAdapter<52>);
	case 53:
		return reinterpret_cast<void*>(&CreateVariantOutputAdapter<53>);
	case 56: return reinterpret_cast<void*>(&GetValueAdapter);
	case 57: return reinterpret_cast<void*>(&ReleaseValueAdapter);
	case 59:
		return reinterpret_cast<void*>(&TransitiveVariantOutputAdapter);
	case 61:
		return reinterpret_cast<void*>(&DirectVariantOutputAdapter);
	case 82: return reinterpret_cast<void*>(&PackedVariantArgumentsAdapter);
	case 84: return reinterpret_cast<void*>(&FinalVariantArgumentAdapter);
	default: return nullptr;
	}
}

static uintptr_t* CreateFakeServerVtable()
{
	auto* vtable = new (std::nothrow) uintptr_t[
		r1delta::script_variant::kTfoVtableSlotCount]{};
	if (!vtable)
		FailFakeServerVm("could not allocate fake vtable");

	for (const auto& entry :
		r1delta::script_variant::kVtableSlotInventory) {
		void* destination = reinterpret_cast<void*>(
			g_originalR1Destinations[entry.sourceSlot]);
		if (entry.disposition
			== r1delta::script_variant::SourceSlotDisposition::Adapter) {
			destination = AdapterForSourceSlot(entry.sourceSlot);
		}
		if (!destination) {
			delete[] vtable;
			FailFakeServerVm("real R1 vtable destination is null");
		}
		const uintptr_t thunk =
			CreateFunctionIndirect(destination, &g_realServerVm);
		if (!thunk) {
			delete[] vtable;
			FailFakeServerVm("could not allocate vtable thunk");
		}
		vtable[entry.targetSlot] = thunk;
	}

	vtable[r1delta::script_variant::kTfoSetPerVmFlagSlot] =
		CreateFunctionIndirect(
			reinterpret_cast<void*>(&SetPerVmFlagAdapter),
			&g_realServerVm);
	vtable[r1delta::script_variant::kTfoGetPerVmFlagSlot] =
		CreateFunctionIndirect(
			reinterpret_cast<void*>(&GetPerVmFlagAdapter),
			&g_realServerVm);
	if (!vtable[r1delta::script_variant::kTfoSetPerVmFlagSlot]
		|| !vtable[r1delta::script_variant::kTfoGetPerVmFlagSlot]) {
		delete[] vtable;
		FailFakeServerVm("could not allocate per-VM flag thunks");
	}
	return vtable;
}

static void OnServerVmCreated(void* vmPtr)
{
	if (!vmPtr)
		FailFakeServerVm("null real VM");
	if (g_realServerVm && g_realServerVm != vmPtr)
		FailFakeServerVm("overlapping real server VMs");

	auto* requestedDestinations = *reinterpret_cast<uintptr_t**>(vmPtr);
	if (!requestedDestinations)
		FailFakeServerVm("real VM has no vtable");

	if (g_haveOriginalR1Destinations) {
		if (r1delta::script_variant::VtableNeedsRecreation(
				g_originalR1Destinations.data(),
				requestedDestinations)) {
			FailFakeServerVm(
				"real VM vtable destinations changed on recreation");
		}
	}
	else {
		for (std::size_t slot = 0;
			slot < g_originalR1Destinations.size();
			++slot) {
			g_originalR1Destinations[slot] =
				requestedDestinations[slot];
		}
		g_haveOriginalR1Destinations = true;
	}

	g_serverProxyFlag = false;
	g_realServerVm = vmPtr;
	if (!g_fakeServerVm.vtable)
		g_fakeServerVm.vtable = CreateFakeServerVtable();
}

static void OnServerVmDestroyed()
{
	for (auto& [source, adapter] : g_classDescriptorClones) {
		if (!source || !adapter || !adapter->complete)
			continue;
		auto* writableSource = const_cast<unsigned char*>(
			static_cast<const unsigned char*>(source));
		*reinterpret_cast<void**>(
			writableSource
				+ r1delta::script_variant::
					kClassDescriptorFunctionVectorBaseOffset) =
			adapter->originalFunctionBase;
	}
	g_functionDescriptorClones.clear();
	g_classDescriptorClones.clear();
	g_serverProxyFlag = false;
	g_realServerVm = nullptr;
}

} // namespace


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
		OnServerVmCreated(vmPtr);

		// Return the fake VM pointer for server context
		return &g_fakeServerVm;
	}

	return vmPtr;
}

void* CScriptManager__CreateNewVM_R1OTFO(__int64 a1, int a2, unsigned int a3) {
	void* vmPtr = g_R1OTfoCreateNewVMOriginal ? g_R1OTfoCreateNewVMOriginal(a1, a2, a3) : nullptr;
	if (!vmPtr)
		return nullptr;

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
		if (AreR1OFakeDediVerboseLogsEnabled()) {
			char unknownLog[256];
			_snprintf_s(unknownLog, sizeof(unknownLog), _TRUNCATE, "R1Delta: R1O TFO CScriptManager::CreateNewVM unknown context=%u vm=%p\n", a3, vmPtr);
			OutputDebugStringA(unknownLog);
		}
		return vmPtr;
	}

	GetSQVMFuncs();
	if (AddSquirrelReg)
		ScriptFunctionRegistry::getInstance().registerFunctions(vmPtr, context);
	else if (AreR1OFakeDediVerboseLogsEnabled())
		OutputDebugStringA("R1Delta: R1O TFO squirrel native registration skipped: AddSquirrelReg is null\n");

	if (context == SCRIPT_CONTEXT_SERVER) {
		g_R1OTfoServerVm = vmPtr;
		// The normal R1 autorun hook sits above CScriptManager::CreateNewVM,
		// after the VM's root table and IncludeScript helper are ready.  R1O
		// does not use that hook.  Leave this VM pending and reset its bootstrap
		// readiness; the engine command dispatcher marks it ready only after
		// the map's native "exec server.cfg" callback has returned, when the
		// initial file-scope traversal is no longer re-entrant.
		s_R1OTfoServerAutorunBootstrapComplete.store(
			false,
			std::memory_order_release);
		s_R1OTfoPendingServerAutorunVm.store(
			reinterpret_cast<R1SquirrelVM*>(vmPtr),
			std::memory_order_release);
	}

	if (AreR1OFakeDediVerboseLogsEnabled()) {
		char vmLog[256];
		_snprintf_s(vmLog, sizeof(vmLog), _TRUNCATE, "R1Delta: R1O TFO squirrel VM created context=%u vm=%p natives registered\n", a3, vmPtr);
		OutputDebugStringA(vmLog);
	}
	return vmPtr;
}

bool InstallR1OTFOSquirrelHooks(uintptr_t launcherBase)
{
	if (!launcherBase)
		return false;

	g_R1OTfoLauncherBase = launcherBase;
	auto addRegTarget = reinterpret_cast<TfoAddSquirrelReg_t>(launcherBase + 0x22900);
	g_R1OTfoAddSquirrelReg = addRegTarget;
	AddSquirrelReg = &R1OTfoAddSquirrelRegWrapper;
	sq_gettop = &R1OTfo_sq_gettop;
	sq_getstackobj = &R1OTfo_sq_getstackobj;
	sq_gettype = &R1OTfo_sq_gettype;
	sq_getstring = &R1OTfo_sq_getstring;
	sq_getinteger = &R1OTfo_sq_getinteger;
	sq_getfloat = &R1OTfo_sq_getfloat;
	sq_getbool = &R1OTfo_sq_getbool;
	sq_getsize = &R1OTfo_sq_getsize;
	sq_throwerror = reinterpret_cast<sq_throwerror_t>(launcherBase + 0x2E720);
	sq_pushnull = &R1OTfo_sq_pushnull;
	sq_pushstring = &R1OTfo_sq_pushstring;
	sq_pushinteger = &R1OTfo_sq_pushinteger;
	sq_pushbool = &R1OTfo_sq_pushbool;
	sq_pushfloat = &R1OTfo_sq_pushfloat;
	sq_settop = &R1OTfo_sq_settop;
	base_getroottable = reinterpret_cast<base_getroottable_t>(launcherBase + 0x2CE00);
	sq_get = reinterpret_cast<sq_get_t>(launcherBase + 0x2DCC0);
	sq_get_noerr = reinterpret_cast<sq_get_noerr_t>(launcherBase + 0x2DEC0);
	sq_call = reinterpret_cast<sq_call_t>(launcherBase + 0x2E7B0);
	sq_pop = &R1OTfo_sq_pop;

	const R1OTfoImmediateHookResult printHooks = { MH_OK, InstallR1OTfoPrintHooks(launcherBase) ? MH_OK : MH_UNKNOWN };
	const R1OTfoImmediateHookResult createNewVMHook = InstallR1OTfoImmediateHook(
		launcherBase + 0x1C2A0,
		reinterpret_cast<void*>(&CScriptManager__CreateNewVM_R1OTFO),
		reinterpret_cast<void**>(&g_R1OTfoCreateNewVMOriginal));
	if (AreR1OFakeDediVerboseLogsEnabled()) {
		char hookLog[512];
		_snprintf_s(
			hookLog,
			sizeof(hookLog),
			_TRUNCATE,
			"R1Delta: R1O TFO squirrel hooks launcher=%p createTarget=%p createStatus=%d enable=%d addReg=%p printInstalled=%d installed=%d\n",
			reinterpret_cast<void*>(launcherBase),
			reinterpret_cast<void*>(launcherBase + 0x1C2A0),
			createNewVMHook.create,
			createNewVMHook.enable,
			reinterpret_cast<void*>(g_R1OTfoAddSquirrelReg),
			R1OTfoImmediateHookInstalled(printHooks),
			R1OTfoImmediateHookInstalled(createNewVMHook) && R1OTfoImmediateHookInstalled(printHooks));
		OutputDebugStringA(hookLog);
	}
	return R1OTfoImmediateHookInstalled(createNewVMHook) && R1OTfoImmediateHookInstalled(printHooks);
}


typedef void* (*CScriptVM__GetUnknownVMPtrType)();
CScriptVM__GetUnknownVMPtrType CScriptVM__GetUnknownVMPtrOriginal;
bool IsPointerFromServerDll(void* pointer);
BOOL IsReturnAddressInServerDll(void* returnAddress) {
	return IsPointerFromServerDll(returnAddress);
}

void* CScriptVM__GetUnknownVMPtr()
{
	if (IsReturnAddressInServerDll(_ReturnAddress())) {
		///std::cout << "returning addr to Server SCRIPT VM" << std::endl;
		return &g_fakeServerVm;
	}
	return CScriptVM__GetUnknownVMPtrOriginal();
}
bool IsPointerFromServerDll(void* pointer) {
	// G_server is populated from whichever shared server binary was loaded:
	// server.dll on R1 or server_local.dll (with server.dll fallback) on R1O.
	HMODULE hModule = (HMODULE)G_server;
	if (!hModule) {
		std::cerr << "Failed to get the loaded server module base\n";
		return false;
	}

	const uintptr_t baseAddress = reinterpret_cast<uintptr_t>(hModule);
	static std::atomic<uintptr_t> cachedBase{ 0 };
	static std::atomic<uintptr_t> cachedEnd{ 0 };
	uintptr_t endAddress = cachedEnd.load(std::memory_order_acquire);
	if (cachedBase.load(std::memory_order_acquire) != baseAddress) {
		const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(baseAddress);
		if (dos->e_magic != IMAGE_DOS_SIGNATURE)
			return false;
		const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(baseAddress + dos->e_lfanew);
		if (nt->Signature != IMAGE_NT_SIGNATURE)
			return false;
		endAddress = baseAddress + nt->OptionalHeader.SizeOfImage;
		cachedEnd.store(endAddress, std::memory_order_release);
		cachedBase.store(baseAddress, std::memory_order_release);
	}

	const uintptr_t ptrAddress = reinterpret_cast<uintptr_t>(pointer);
	return ptrAddress >= baseAddress && ptrAddress < endAddress;
}


CScriptManager__DestroyVMType CScriptManager__DestroyVMOriginal;

__declspec(dllexport) R1SquirrelVM* GetServerVMPtr()
{
	return static_cast<R1SquirrelVM*>(
		IsR1ODedicatedServer() ? g_R1OTfoServerVm : g_realServerVm);
}

void __fastcall CScriptManager__DestroyVM(void* manager, void* vmPtr)
{
	const bool destroyingServerVm =
		vmPtr == &g_fakeServerVm
		|| (vmPtr && vmPtr == g_realServerVm);
	void* realVmPtr =
		vmPtr == &g_fakeServerVm ? g_realServerVm : vmPtr;
	CScriptManager__DestroyVMOriginal(manager, realVmPtr);
	if (destroyingServerVm)
		OnServerVmDestroyed();
}
// Track incomplete lines for each VM type
static bool g_serverLineIncomplete = false;
static bool g_clientLineIncomplete = false;
static bool g_uiLineIncomplete = false;

static void EmitSquirrelPrint(const char* prefix, bool& lineIncomplete, const char* string)
{
	if (!lineIncomplete) {
		Msg("%s %s", prefix, string);
	}
	else {
		Msg("%s", string);
	}

	const size_t len = strlen(string);
	lineIncomplete = !(len > 0 && string[len - 1] == '\n');
}

void CSquirrelVM__PrintFunc1(void* m_hVM, const char* s, ...)
{
	char string[2048];
	va_list params;

	va_start(params, s);
	vsnprintf(string, 2048, s, params);
	EmitSquirrelPrint("[SERVER SCRIPT]", g_serverLineIncomplete, string);
	va_end(params);
	ScriptErrorTelemetry::CapturePrint(ScriptErrorTelemetry::VmContext::Server, string);
}

void CSquirrelVM__PrintFunc2(void* m_hVM, const char* s, ...)
{
	char string[2048];
	va_list params;

	va_start(params, s);
	vsnprintf(string, 2048, s, params);
	EmitSquirrelPrint("[CLIENT SCRIPT]", g_clientLineIncomplete, string);
	va_end(params);
	ScriptErrorTelemetry::CapturePrint(ScriptErrorTelemetry::VmContext::Client, string);
}

void CSquirrelVM__PrintFunc3(void* m_hVM, const char* s, ...)
{
	char string[2048];
	va_list params;

	va_start(params, s);
	vsnprintf(string, 2048, s, params);
	EmitSquirrelPrint("[UI SCRIPT]", g_uiLineIncomplete, string);
	va_end(params);
	ScriptErrorTelemetry::CapturePrint(ScriptErrorTelemetry::VmContext::Ui, string);
}
using SQCompileBufferFn = SQRESULT(*)(HSQUIRRELVM, const SQChar*, SQInteger, const SQChar*, SQBool);
using BaseGetRootTableFn = __int64(*)(HSQUIRRELVM);
using SQCallFn = SQRESULT(*)(HSQUIRRELVM, SQInteger, SQBool, SQBool);
using R1OTfoSQCompileBufferFn = SQRESULT(*)(HSQUIRRELVM, void*, BufState*, const SQChar*, SQBool);
using R1OTfoSQPushRootTableFn = __int64(*)(HSQUIRRELVM);
using R1OTfoSQCallFn = SQRESULT(*)(HSQUIRRELVM, SQInteger, SQBool, SQBool);

static bool RunR1OTfoScriptCode(R1SquirrelVM* vm, const char* code, const char* sourceName)
{
	if (!IsR1ODedicatedServer() || !vm || !vm->sqvm || !code || !*code || !G_launcher)
		return false;

	auto tfo_compilebuffer = reinterpret_cast<R1OTfoSQCompileBufferFn>(G_launcher + 0x2BBE0);
	auto tfo_pushroottable = reinterpret_cast<R1OTfoSQPushRootTableFn>(G_launcher + 0x2CE00);
	auto tfo_call = reinterpret_cast<R1OTfoSQCallFn>(G_launcher + 0x2E7B0);
	if (!tfo_compilebuffer || !tfo_pushroottable || !tfo_call)
		return false;

	const SQInteger oldTop = sq_gettop ? sq_gettop(vm, vm->sqvm) : -1;
	BufState state{ code, 0, static_cast<SQInteger>(strlen(code)) };
	const char* compileSource = sourceName && *sourceName ? sourceName : "console";
	const SQRESULT compileResult =
		tfo_compilebuffer(vm->sqvm, nullptr, &state, compileSource, SQTrue);
	SQRESULT callResult = SQ_ERROR;
	if (SQ_SUCCEEDED(compileResult)) {
		tfo_pushroottable(vm->sqvm);
		callResult = tfo_call(vm->sqvm, 1, SQFalse, SQTrue);
	}
	if (oldTop >= 0 && sq_settop)
		sq_settop(vm->sqvm, oldTop);

	if (AreR1OFakeDediVerboseLogsEnabled()) {
		char buffer[768];
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: R1O script source=%s compile=%d call=%d code=%s\n",
			compileSource,
			static_cast<int>(compileResult),
			static_cast<int>(callResult),
			code);
		OutputDebugStringA(buffer);
	}
	if (SQ_FAILED(compileResult))
		Warning("R1O script compile failed for %s.\n", compileSource);
	else if (SQ_FAILED(callResult))
		Warning("R1O script call failed for %s.\n", compileSource);

	return SQ_SUCCEEDED(compileResult) && SQ_SUCCEEDED(callResult);
}

void MarkR1OServerAutorunBootstrapComplete()
{
	if (!IsR1ODedicatedServer())
		return;

	if (s_R1OTfoPendingServerAutorunVm.load(std::memory_order_acquire))
		s_R1OTfoServerAutorunBootstrapComplete.store(true, std::memory_order_release);
}

bool RunR1OServerAutorunScriptsIfPending()
{
	if (!IsR1ODedicatedServer())
		return false;
	if (!s_R1OTfoServerAutorunBootstrapComplete.load(std::memory_order_acquire))
		return false;

	R1SquirrelVM* pendingVm =
		s_R1OTfoPendingServerAutorunVm.load(std::memory_order_acquire);
	if (!pendingVm || pendingVm != GetServerVMPtr())
		return false;
	if (!pendingVm->sqvm || !GetR1ONativeFileSystem())
		return false;
	if (!FileCache::GetInstance().RefreshAddonSnapshotFromWorkingDirectory()) {
		Warning("R1O server autorun could not refresh the working-directory addon snapshot.\n");
		return false;
	}

	RunAutorunScripts(pendingVm, "sv_*");
	s_R1OTfoPendingServerAutorunVm.compare_exchange_strong(
		pendingVm,
		nullptr,
		std::memory_order_acq_rel);
	s_R1OTfoServerAutorunBootstrapComplete.store(false, std::memory_order_release);
	if (AreR1OFakeDediVerboseLogsEnabled())
		OutputDebugStringA("R1Delta: completed R1O server autorun pass\n");
	return true;
}

static bool CopyCommandCStringForR1O(const char* source, std::string& out, size_t maxLen = CCommand::COMMAND_MAX_LENGTH)
{
	if (!source)
		return false;

	out.clear();
	for (size_t i = 0; i < maxLen; ++i) {
		const char ch = source[i];
		if (!ch)
			return true;
		out.push_back(ch);
	}

	return true;
}

static std::string GetScriptCodeFromCommand(const CCommand& args)
{
	if (!IsR1ODedicatedServer())
		return args.ArgS();

	std::string fullCommand;
	std::string commandName;
	CopyCommandCStringForR1O(args.GetCommandString(), fullCommand);
	CopyCommandCStringForR1O(args.Arg(0), commandName, 128);

	if (!fullCommand.empty() && !commandName.empty() && fullCommand.rfind(commandName, 0) == 0) {
		size_t pos = commandName.length();
		while (pos < fullCommand.length() && isspace(static_cast<unsigned char>(fullCommand[pos])))
			++pos;
		if (pos < fullCommand.length())
			return fullCommand.substr(pos);
	}

	std::string code;
	const int64_t argc = args.ArgC();
	if (argc > 1 && argc <= CCommand::COMMAND_MAX_ARGC) {
		for (int64_t i = 1; i < argc; ++i) {
			std::string arg;
			if (!CopyCommandCStringForR1O(args.Arg(static_cast<int>(i)), arg))
				continue;
			if (!code.empty())
				code.push_back(' ');
			code += arg;
		}
	}

	if (!code.empty())
		return code;

	std::string argS;
	CopyCommandCStringForR1O(args.ArgS(), argS);
	return argS;
}

void run_script(const CCommand& args, R1SquirrelVM* (*GetVMPtr)())
{
	static auto fatal_script_errors = OriginalCCVar_FindVar(cvarinterface, "fatal_script_errors");
	auto launcher = G_launcher;

	std::string code = GetScriptCodeFromCommand(args);
	R1SquirrelVM* vm = GetVMPtr();
	if (!vm) {
		Warning("Can't run script code on a VM when that VM is not present.");
		return;
	}

	auto fatalParent = fatal_script_errors ? fatal_script_errors->m_pParent : nullptr;
	int bak = 0;
	if (fatalParent)
	{
		bak = fatalParent->m_Value.m_nValue;
		fatalParent->m_Value.m_nValue = 0;
	}

	ConVarR1O* fatalR1OParent = nullptr;
	int r1oBak = 0;
	if (IsR1ODedicatedServer())
	{
		ConVarR1O* fatalR1O = CCVar_FindVar(cvarinterface, "fatal_script_errors");
		fatalR1OParent = fatalR1O && fatalR1O->m_pParent ? fatalR1O->m_pParent : fatalR1O;
		if (fatalR1OParent)
		{
			r1oBak = fatalR1OParent->m_Value.m_nValue;
			fatalR1OParent->m_Value.m_nValue = 0;
		}
	}

	const auto restoreFatalScriptPolicy = [&]()
	{
		if (fatalParent)
			fatalParent->m_Value.m_nValue = bak;
		if (fatalR1OParent)
			fatalR1OParent->m_Value.m_nValue = r1oBak;
	};

	if (IsR1ODedicatedServer())
	{
		if (!launcher || !vm->sqvm)
		{
			Warning("Can't run R1O script code without launcher and SQVM pointers.\n");
			restoreFatalScriptPolicy();
			return;
		}

		if (AreR1OFakeDediVerboseLogsEnabled()) {
			char buffer[768];
			_snprintf_s(
				buffer,
				sizeof(buffer),
				_TRUNCATE,
				"R1Delta: R1O script run enter vm=%p sqvm=%p oldTop=%lld code=%s\n",
				vm,
				vm->sqvm,
				static_cast<long long>(sq_gettop ? sq_gettop(vm, vm->sqvm) : -1),
				code.c_str());
			OutputDebugStringA(buffer);
		}
		RunR1OTfoScriptCode(vm, code.c_str(), "console");
		restoreFatalScriptPolicy();
		return;
	}

	SQCompileBufferFn sq_compilebuffer = reinterpret_cast<SQCompileBufferFn>(launcher + (IsDedicatedServer() ? 0x1A6C0 : 0x1A5E0));
	BaseGetRootTableFn base_getroottable = reinterpret_cast<BaseGetRootTableFn>(launcher + (IsDedicatedServer() ? 0x56520 : 0x56440));
	SQCallFn sq_call = reinterpret_cast<SQCallFn>(launcher + (IsDedicatedServer() ? 0x18D20 : 0x18C40));

	SQRESULT compileRes = sq_compilebuffer(vm->sqvm, code.c_str(), static_cast<SQInteger>(code.length()), "console", 1);
	if (SQ_SUCCEEDED(compileRes))
	{
		base_getroottable(vm->sqvm);
		SQRESULT callRes = sq_call(vm->sqvm, 1, SQFalse, SQTrue);
		if (SQ_FAILED(callRes))
			Warning("Script call failed for: %s\n", code.c_str());
	}
	restoreFatalScriptPolicy();
}

void script_cmd(const CCommand& args)
{
	run_script(args, GetServerVMPtr);
}

void script_client_cmd(const CCommand& args)
{
	run_script(args, GetClientVMPtr);
}

void script_ui_cmd(const CCommand& args)
{
	run_script(args, GetUIVMPtr);
}



//__int64 __fastcall sq_throwerrorhook(__int64 a1, const char* a2)
//{
//	return 0;
//	//if (a2)
//	//	Warning("sq_throwerror: %s\n", a2);
//	//return sq_throwerror(a1, a2);
//}

