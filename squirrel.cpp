#pragma once

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
#include "thirdparty/silver-bun/silver-bun.h"
#include <fcntl.h>
#include <io.h>
#include <streambuf>
#include "logging.h"
#include <map>
#pragma intrinsic(_ReturnAddress)



typedef __int64 (*CScriptVM__ctortype)(void* thisptr);
CScriptVM__ctortype CScriptVM__ctororiginal;
bool scriptflag = false;
void __fastcall CScriptVM__SetTFOFlag(__int64 a1, char a2)
{
	scriptflag = a2;
}
char __fastcall CScriptVM__GetTFOFlag(__int64 a1)
{
	return scriptflag;
}
// Prototype for the function to update the vtable pointer of a CScriptVM object.
void SetNewVTable(void* thisptr, uintptr_t* newVTable);

// Implementation for creating a new vtable and inserting functions.
void* CreateNewVTable(void* thisptr) {
	// Original vtable can be obtained by dereferencing the this pointer.
	uintptr_t* originalVTable = *(uintptr_t**)thisptr;

	// Allocate memory for the new vtable with 122 entries (original 120 + 2 new).
	uintptr_t* newVTable = new uintptr_t[122];

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
void* fakevmptr;
void* realvmptr = 0;
typedef void* (*CScriptManager__CreateNewVMType)(__int64 a1, int a2, unsigned int a3);
CScriptManager__CreateNewVMType CScriptManager__CreateNewVMOriginal;
bool isServerScriptVM = false;
void* CScriptManager__CreateNewVM(__int64 a1, int a2, unsigned int a3) {
	isServerScriptVM = a3 == 0;
	if (isServerScriptVM) {
		fakevmptr = 0;
		realvmptr = 0;
	}
	void* ret = CScriptManager__CreateNewVMOriginal(a1, a2, a3);
	if (isServerScriptVM) {
		//std::cout << "created Server SCRIPT VM" << std::endl;
		realvmptr = ret;
		fakevmptr = CreateNewVTable(ret);
		isServerScriptVM = false;
		return &fakevmptr;
	}
	isServerScriptVM = false;
	return ret;
}

typedef void* (*CScriptVM__GetUnknownVMPtrType)();
CScriptVM__GetUnknownVMPtrType CScriptVM__GetUnknownVMPtrOriginal;
BOOL IsReturnAddressInServerDll(void* returnAddress) {
	HMODULE module2;
	BOOL check1 = GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCTSTR)returnAddress, &module2);
	BOOL check2 = module2 == GetModuleHandle(TEXT("server.dll"));
	return check1 && check2;
}

void* CScriptVM__GetUnknownVMPtr()
{
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
__forceinline bool serverRunning(void* a1) {
	//return isServerScriptVM || a1 == realvmptr || a1 == fakevmptr || (realvmptr && a1 == *(void**)(((uintptr_t)realvmptr + 8)));
	if (isServerScriptVM || a1 == realvmptr || a1 == fakevmptr || (realvmptr && a1 == *(void**)(((uintptr_t)realvmptr + 8))))
		return true; // SQVM handle check
	static const HMODULE serverDllBase = GetModuleHandleA("server.dll");
	static const SIZE_T serverDllSize = 0xFB5000; // no comment
	void* stack[128];
	USHORT frames = CaptureStackBackTrace(0, 128, stack, NULL);

	for (USHORT i = 0; i < frames; i++) {
		if ((stack[i] >= serverDllBase) && ((ULONG_PTR)stack[i] < (ULONG_PTR)serverDllBase + serverDllSize)) {
			return TRUE;
		}
	}

	return FALSE;
}

const char* FieldTypeToString(int fieldType)
{
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
	static HMODULE hModule = GetModuleHandleA("server.dll");
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
		LPCVOID baseAddressDll = (LPCVOID)GetModuleHandleA(VSCRIPT_DLL);
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
	static LPCVOID baseAddressDll = (LPCVOID)GetModuleHandleA(VSCRIPT_DLL);
	static LPVOID address1 = (LPVOID)((uintptr_t)(baseAddressDll)+(IsDedicatedServer() ? 0xc3cf : 0xC3AF));
	static LPVOID address2 = (LPVOID)((uintptr_t)(baseAddressDll)+(IsDedicatedServer() ? 0xc7e4 : 0xC7C4));

	char value1 = 0x21;
	char data1[] = { 0x00, 0x07, 0x01, 0x07, 0x02, 0x07, 0x03, 0x07, 0x04, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x05, 0x06 };
	char value2 = 0x20;
	char data2[] = { 0x00, 0x07, 0x01, 0x07, 0x02, 0x03, 0x07, 0x04, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x05, 0x06 };

	DWORD oldProtect;
	VirtualProtect(address1, 1, PAGE_EXECUTE_READWRITE, &oldProtect);
	VirtualProtect(address2, sizeof(data1), PAGE_EXECUTE_READWRITE, &oldProtect);

	if (!serverRunning(a1)) {
		*(char*)address1 = value2;
		memcpy(address2, data2, sizeof(data2));
		FlushInstructionCache(GetCurrentProcess(), address1, 1);
		FlushInstructionCache(GetCurrentProcess(), address2, sizeof(data1));
		CSquirrelVM__TranslateCallOriginal(a1);
		return;
	}

	*(char*)address1 = value1;
	memcpy(address2, data1, sizeof(data1));
	FlushInstructionCache(GetCurrentProcess(), address1, 1);
	FlushInstructionCache(GetCurrentProcess(), address2, sizeof(data1));
	CSquirrelVM__TranslateCallOriginal(a1);

}

__int64 __fastcall CSquirrelVM__PushVariant(__int64* a1, ScriptVariant_t* a2)
{
	if (serverRunning(a1)) {
		ConvertScriptVariant(a2, R1O_TO_R1);
		//std::cout << __FUNCTION__ ": converted variant" << std::endl;
	}
	else {
		//std::cout << __FUNCTION__ ": did not convert variant" << std::endl;
	}
	return CSquirrelVM__PushVariantOriginal(a1, a2);
}

char __fastcall CSquirrelVM__ConvertToVariant(__int64* a1, __int64 a2, ScriptVariant_t* a3)
{
	bool ret = CSquirrelVM__ConvertToVariantOriginal(a1, a2, a3);
	if (serverRunning(a1))
		ConvertScriptVariant(a3, R1_TO_R1O);
	return ret;
}
__int64 __fastcall CSquirrelVM__ReleaseValue(__int64* a1, ScriptVariant_t* a2)
{
	if (serverRunning(a1))
		ConvertScriptVariant(a2, R1O_TO_R1);
	return CSquirrelVM__ReleaseValueOriginal(a1, a2);
}
bool __fastcall CSquirrelVM__SetValue(__int64* a1, void* a2, unsigned int a3, ScriptVariant_t* a4)
{
	if (serverRunning(a1))
		ConvertScriptVariant(a4, R1O_TO_R1);
	return CSquirrelVM__SetValueOriginal(a1, a2, a3, a4);
}

bool __fastcall CSquirrelVM__SetValueEx(__int64* a1, __int64 a2, const char* a3, ScriptVariant_t* a4)
{
	if (serverRunning(a1))
		ConvertScriptVariant(a4, R1O_TO_R1);
	return CSquirrelVM__SetValueExOriginal(a1, a2, a3, a4);

}
typedef void (*CScriptManager__DestroyVMType)(void* a1, void* vmptr);
CScriptManager__DestroyVMType CScriptManager__DestroyVMOriginal;

__declspec(dllexport) R1SquirrelVM* GetServerVMPtr() {
	return (R1SquirrelVM*)(realvmptr);
}
void __fastcall CScriptManager__DestroyVM(void* a1, void* vmptr)
{
	//if (serverRunning(a1) || serverRunning(vmptr) || serverRunning(*(void**)vmptr) || serverRunning(*(void**)a1)) {
	//	vmptr = realvmptr;
	//	//std::cout << "set vm ptr" << std::endl;
	//}
	//else {
	//	//std::cout << "did NOT set vm ptr" << std::endl;
	//}
	if (*((void**)vmptr) == fakevmptr) {
		vmptr = realvmptr;
		free(fakevmptr);
		realvmptr = 0;
		fakevmptr = 0;
		hasRegisteredServerFuncs = true;
	}
	return CScriptManager__DestroyVMOriginal(a1, vmptr);
}
void CSquirrelVM__PrintFunc1(void* m_hVM, const char* s, ...)
{
	char string[2048]; // [esp+0h] [ebp-800h] BYREF
	va_list params; // [esp+810h] [ebp+10h] BYREF

	va_start(params, s);
	vsnprintf(string, 2048, s, params);
	Msg("[SERVER SCRIPT] %s", string);
}
void CSquirrelVM__PrintFunc2(void* m_hVM, const char* s, ...)
{
	char string[2048]; // [esp+0h] [ebp-800h] BYREF
	va_list params; // [esp+810h] [ebp+10h] BYREF

	va_start(params, s);
	vsnprintf(string, 2048, s, params);
	Msg("[CLIENT SCRIPT] %s", string);
}
void CSquirrelVM__PrintFunc3(void* m_hVM, const char* s, ...)
{
	char string[2048]; // [esp+0h] [ebp-800h] BYREF
	va_list params; // [esp+810h] [ebp+10h] BYREF

	va_start(params, s);
	vsnprintf(string, 2048, s, params);
	Msg("[UI SCRIPT] %s", string);
}

R1SquirrelVM* GetClientVMPtr() {
	return *(R1SquirrelVM**)(uintptr_t(GetModuleHandleA("client.dll")) + 0x16BBE78);
}
R1SquirrelVM* GetUIVMPtr() {
	return *(R1SquirrelVM**)(uintptr_t(GetModuleHandleA("client.dll")) + 0x16C1FA8);
}
using SQCompileBufferFn = SQRESULT(*)(HSQUIRRELVM, const SQChar*, SQInteger, const SQChar*, SQBool);
using BaseGetRootTableFn = __int64(*)(HSQUIRRELVM);
using SQCallFn = SQRESULT(*)(HSQUIRRELVM, SQInteger, SQBool, SQBool);

void run_script(const CCommand& args, R1SquirrelVM* (*GetVMPtr)())
{
	static SQCompileBufferFn sq_compilebuffer = reinterpret_cast<SQCompileBufferFn>(uintptr_t(GetModuleHandleA("launcher.dll")) + 0x1A5E0);
	static BaseGetRootTableFn base_getroottable = reinterpret_cast<BaseGetRootTableFn>(uintptr_t(GetModuleHandleA("launcher.dll")) + 0x56440);
	static SQCallFn sq_call = reinterpret_cast<SQCallFn>(uintptr_t(GetModuleHandleA("launcher.dll")) + 0x18C40);

	std::string code = args.ArgS();
	R1SquirrelVM* vm = GetVMPtr();
	if (!vm) {
		Warning("Can't run script code on a VM when that VM is not present.");
		return;
	}

	SQRESULT compileRes = sq_compilebuffer(vm->sqvm, code.c_str(), static_cast<SQInteger>(code.length()), "console", 1);
	if (SQ_SUCCEEDED(compileRes))
	{
		base_getroottable(vm->sqvm);
		SQRESULT callRes = sq_call(vm->sqvm, 1, SQFalse, SQFalse);
	}
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

__int64 __fastcall sq_throwerrorhook(__int64 a1, const char* a2)
{
	return 0;
	//if (a2)
	//	Warning("sq_throwerror: %s\n", a2);
	//return sq_throwerror(a1, a2);
}