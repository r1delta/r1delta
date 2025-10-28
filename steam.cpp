#include "steam.h"
#include "load.h"


typedef void(__fastcall* CBaseClientState_SendConnectPacket_t)(void* thisptr, netadr* addr, int chalNum);

CBaseClientState_SendConnectPacket_t CBaseClientState_SendConnectPacketOriginal;

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


void __fastcall CBaseClientState_SendConnectPacket(void* thisptr, netadr* addr, int chalNum) {

	// do steam stuff

	CBaseClientState_SendConnectPacketOriginal(thisptr, addr, chalNum);
}


void InitSteamHooks()
{
	//MH_CreateHook((void*)(G_engine + 0x133AA0), &Host_InitHook, (void**)&Host_InitOriginal);
	MH_CreateHook((void*)(G_engine + 0x2A200), &CBaseClientState_SendConnectPacket, (void**)&CBaseClientState_SendConnectPacketOriginal);
}