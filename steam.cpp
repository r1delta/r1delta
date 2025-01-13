#include "steam.h"
#include "load.h"


typedef void(__fastcall* CBaseClientState_SendConnectPacket_t)(void* thisptr, netadr* addr, int chalNum);

CBaseClientState_SendConnectPacket_t CBaseClientState_SendConnectPacketOriginal;


typedef __int64(*Host_InitType)(bool a1);
Host_InitType Host_InitOriginal;

__int64 __fastcall Host_InitHook(bool a1) {
	// do steam stuff

	return Host_InitOriginal(a1);
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