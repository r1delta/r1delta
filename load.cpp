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

typedef __int64 (*SendPropBoolType)(__int64 result, char* pVarName, int offset, int sizeofVar, int a5);
SendPropBoolType SendPropBoolOriginal;
typedef __int64(__fastcall* SendPropIntType)(__int64 a1, char* pVarName, int a3, int a4, int a5, int a6, __int64(__fastcall* a7)(), char a8);
SendPropIntType SendPropIntOriginal;

__int64 __fastcall SendPropBool(__int64 a1, char* pVarName, int a3, int a4, int a5) {
	if (!strcmp(pVarName, "m_bDoubleJump") || !strcmp(pVarName, "m_hasHacking") || !strcmp(pVarName, "m_titanBuildStarted") || !strcmp(pVarName, "m_titanDeployed") || !strcmp(pVarName, "m_titanReady"))
		return (__int64)(SendPropIntOriginal((__int64)a1, (char*)"should_never_see_this", 0, 4, -1, 0, 0i64, 128));
	return SendPropBoolOriginal(a1, pVarName, a3, a4, a5);
}

__int64 __fastcall SendPropInt(__int64 a1, char* pVarName, int a3, int a4, int a5, int a6, __int64(__fastcall* a7)(), char a8) {
	if (!strcmp(pVarName, "m_bFireZooming") || !strcmp(pVarName, "m_bWallRun") || !strcmp(pVarName, "m_level") || !strcmp(pVarName, "m_liveryCode") || !strcmp(pVarName, "m_tierLevel"))
		return (__int64)(SendPropIntOriginal((__int64)a1, (char*)"should_never_see_this", 0, 4, -1, 0, 0i64, 128));
	return SendPropIntOriginal(a1, pVarName, a3, a4, a5, a6, a7, a8);
}
typedef __int64(__fastcall* SendPropVectorType)(__int64 a1, char* pVarName, int a3, __int64 a4, int a5, int a6, float a7, float a8, __int64 a9, char a10);
SendPropVectorType SendPropVectorOriginal;

__int64 __fastcall SendPropVector(__int64 a1, char* pVarName, int a3, __int64 a4, int a5, int a6, float a7, float a8, __int64 a9, char a10) {
	if (!strcmp(pVarName, "m_liveryColor0") || !strcmp(pVarName, "m_liveryColor1") || !strcmp(pVarName, "m_liveryColor2"))
		return (__int64)(SendPropIntOriginal((__int64)a1, (char*)"should_never_see_this", 0, 4, -1, 0, 0i64, 128));
	return SendPropVectorOriginal(a1, pVarName, a3, a4, a5, a6, a7, a8, a9, a10);
}
typedef __int64(__fastcall* SendPropUnknownType)(__int64 a1, const char* pVarName, int a3, int a4, int a5);
SendPropUnknownType SendPropUnknownOriginal;

__int64 __fastcall SendPropUnknown(__int64 a1, const char* pVarName, int a3, int a4, int a5) {
	if (!strcmp(pVarName, "m_titanRespawnTime"))
		return (__int64)(SendPropIntOriginal((__int64)a1, (char*)"should_never_see_this", 0, 4, -1, 0, 0i64, 128));
	return SendPropUnknownOriginal(a1, pVarName, a3, a4, a5);
}
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



void __stdcall LoaderNotificationCallback(
	unsigned long notification_reason,
	const LDR_DLL_NOTIFICATION_DATA* notification_data,
	void* context) {
	if (notification_reason != LDR_DLL_NOTIFICATION_REASON_LOADED)
		return;

	if (std::wstring((wchar_t*)notification_data->Loaded.BaseDllName->Buffer, notification_data->Loaded.BaseDllName->Length).find(SERVER_DLLWIDE) != std::string::npos) {
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(SERVER_DLL) + 0x143A10), &CServerGameDLL__DLLInit, (LPVOID*)&CServerGameDLL__DLLInitOriginal);
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(SERVER_DLL) + 0x71E0BC), &hkcalloc_base, NULL);
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(SERVER_DLL) + 0x71E99C), &hkmalloc_base, NULL);
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(SERVER_DLL) + 0x71E9FC), &hkrealloc_base, NULL);
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(SERVER_DLL) + 0x72B480), &hkrecalloc_base, NULL);
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(SERVER_DLL) + 0x721000), &hkfree_base, NULL);

		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("launcher.dll") + 0x6A60), &CScriptVM__ctor, reinterpret_cast<LPVOID*>(&CScriptVM__ctororiginal));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("launcher.dll") + 0x1210), &CScriptManager__CreateNewVM, reinterpret_cast<LPVOID*>(&CScriptManager__CreateNewVMOriginal));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("launcher.dll") + 0x1630), &CScriptVM__GetUnknownVMPtr, reinterpret_cast<LPVOID*>(&CScriptVM__GetUnknownVMPtrOriginal));
		//MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("launcher.dll") + 0x8AB0), &sub_180008AB0, reinterpret_cast<LPVOID*>(&sub_180008AB0Original));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("launcher.dll") + 0x15F0), &sub_1800015F0, reinterpret_cast<LPVOID*>(&sub_1800015F0Original));
		//MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("launcher.dll") + 0xBE60), &sub_18000BE60, reinterpret_cast<LPVOID*>(NULL));

		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("launcher.dll") + 0xCDB0), &CSquirrelVM__RegisterFunctionGuts, reinterpret_cast<LPVOID*>(&CSquirrelVM__RegisterFunctionGutsOriginal));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("launcher.dll") + 0xD650), &CSquirrelVM__PushVariant, reinterpret_cast<LPVOID*>(&CSquirrelVM__PushVariantOriginal));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("launcher.dll") + 0xD7B0), &CSquirrelVM__ConvertToVariant, reinterpret_cast<LPVOID*>(&CSquirrelVM__ConvertToVariantOriginal));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("launcher.dll") + 0xB110), &CSquirrelVM__ReleaseValue, reinterpret_cast<LPVOID*>(&CSquirrelVM__ReleaseValueOriginal));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("launcher.dll") + 0xA1F0), &CSquirrelVM__SetValue, reinterpret_cast<LPVOID*>(&CSquirrelVM__SetValueOriginal));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("launcher.dll") + 0x9C40), &CSquirrelVM__SetValueEx, reinterpret_cast<LPVOID*>(&CSquirrelVM__SetValueExOriginal));
		MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA("launcher.dll") + 0xBE60), &CSquirrelVM__TranslateCall, reinterpret_cast<LPVOID*>(&CSquirrelVM__TranslateCallOriginal));
		
		//MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(SERVER_DLL) + 0x1FBAE0), &SendPropBool, reinterpret_cast<LPVOID*>(&SendPropBoolOriginal));
		//MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(SERVER_DLL) + 0x0F3CA0), &SendPropInt, reinterpret_cast<LPVOID*>(&SendPropIntOriginal));
		//MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(SERVER_DLL) + 0x0F3590), &SendPropVector, reinterpret_cast<LPVOID*>(&SendPropVectorOriginal));
		//MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(SERVER_DLL) + 0x1FBCE0), &SendPropUnknown, reinterpret_cast<LPVOID*>(&SendPropUnknownOriginal));

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
		//MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(ENGINE_DLL) + 0x1D9E70), &MatchRecvPropsToSendProps_R, reinterpret_cast<LPVOID*>(NULL));
#endif
		//MH_CreateHook((LPVOID)((uintptr_t)GetModuleHandleA(ENGINE_DLL) + 0x217C30), &sub_180217C30, NULL);
		// Cast the function pointer to the function at 0x4E80
		MH_EnableHook(MH_ALL_HOOKS);
		std::cout << "did hooks" << std::endl;

	}
}