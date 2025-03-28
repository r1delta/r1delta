#include "steam.h"
#include "load.h"


typedef void(__fastcall* CBaseClientState_SendConnectPacket_t)(void* thisptr, netadr* addr, int chalNum);

CBaseClientState_SendConnectPacket_t CBaseClientState_SendConnectPacketOriginal;


typedef __int64(*Host_InitType)(bool a1);
Host_InitType Host_InitOriginal;
// Patch LdrLoadDll by overwriting its first byte with the RET opcode (0xC3)
void PatchLdrLoadDll()
{
	// Get handle to ntdll.dll
	HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
	if (!hNtdll)
	{
		return;
	}

	// Get the address of LdrLoadDll in ntdll.dll
	FARPROC ldrLoadDll = GetProcAddress(hNtdll, "LdrLoadDll");
	if (!ldrLoadDll)
	{
		return;
	}

	// Change memory protection so we can write to the function address
	DWORD oldProtect;
	if (VirtualProtect(ldrLoadDll, 1, PAGE_EXECUTE_READWRITE, &oldProtect))
	{
		// Write the RET opcode (0xC3) to the first byte of LdrLoadDll
		*((BYTE*)ldrLoadDll) = 0xC3;

		// Restore the original protection
		VirtualProtect(ldrLoadDll, 1, oldProtect, &oldProtect);
	}
}
__int64 __fastcall Host_InitHook(bool a1) {

	auto ret = Host_InitOriginal(a1);
	LoadLibraryW(L"OnDemandConnRouteHelper"); // stop fucking reloading this thing
	LoadLibraryW(L"TextShaping.dll"); // fix "Patcher Error" dialogs having no text
	LoadLibraryW(L"bin\\x64_retail\\datacache.dll");
	LoadLibraryW(L"bin\\x64_retail\\engine.dll");
	LoadLibraryW(L"bin\\x64_retail\\filesystem_stdio.dll");
	LoadLibraryW(L"bin\\x64_retail\\inputsystem.dll");
	LoadLibraryW(L"r1delta\\bin\\launcher.dll");
	LoadLibraryW(L"bin\\x64_retail\\localize.dll");
	LoadLibraryW(L"bin\\x64_retail\\materialsystem_dx11.dll");
	LoadLibraryW(L"bin\\x64_retail\\studiorender.dll");
	LoadLibraryW(L"bin\\x64_retail\\valve_avi.dll");
	LoadLibraryW(L"bin\\x64_retail\\vgui2.dll");
	LoadLibraryW(L"bin\\x64_retail\\vguimatsurface.dll");
	LoadLibraryW(L"bin\\x64_retail\\vphysics.dll");
	LoadLibraryW(L"r1\\bin\\x64_retail\\client.dll");
	LoadLibraryW(L"r1delta\\bin\\DevIL.dll");
	LoadLibraryW(L"r1delta\\bin\\engine_ds.dll");
	LoadLibraryW(L"r1delta\\bin\\engine_r1o.dll");
	LoadLibraryW(L"r1delta\\bin\\GFSDK_SSAO.win64.dll");
	LoadLibraryW(L"r1delta\\bin\\GFSDK_TXAA.win64.dll");
	LoadLibraryW(L"r1delta\\bin\\launcher.dll");
	LoadLibraryW(L"r1delta\\bin\\server.dll");
	LoadLibraryW(L"r1delta\\bin\\tier0_orig.dll");
	LoadLibraryW(L"r1delta\\bin\\vtex_dll.dll");
	LoadLibraryW(L"r1delta\\bin_delta\\vaudio_speex.dll");
	PatchLdrLoadDll();

	return ret;
}


void __fastcall CBaseClientState_SendConnectPacket(void* thisptr, netadr* addr, int chalNum) {

	// do steam stuff

	CBaseClientState_SendConnectPacketOriginal(thisptr, addr, chalNum);
}


void InitSteamHooks()
{
	MH_CreateHook((void*)(G_engine + 0x133AA0), &Host_InitHook, (void**)&Host_InitOriginal);
	MH_CreateHook((void*)(G_engine + 0x2A200), &CBaseClientState_SendConnectPacket, (void**)&CBaseClientState_SendConnectPacketOriginal);
}