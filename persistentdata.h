#pragma once
#include "bitbuf.h"
#include "vsdk/public/tier1/utlvector.h"
#include "vsdk/public/tier1/utlmemory.h"
#include "cvar.h"
struct NetMessageCvar_t // sizeof=0x208
{
	char name[260];
    char value[260];
};
struct alignas(8) NET_SetConVar
{
	void* vtable;
	bool m_bReliable;
	void* m_NetChannel;
	void* m_pMessageHandler;
	CUtlVector<NetMessageCvar_t, CUtlMemory<NetMessageCvar_t>> m_ConVars;
};
//static_assert(sizeof(whatever) == sizeof(CUtlVector<NetMessageCvar_t, CUtlMemory<NetMessageCvar_t>>));
//static_assert(offsetof(whatever, m_ConVars) == 32);
//static_assert(offsetof(whatever, m_ConVars_count) == 56);
//static_assert(offsetof(NET_SetConVar, m_ConVars) == 32);
//static_assert(offsetof(NET_SetConVar, m_pMessageHandler) == 24);
//static_assert((offsetof(NET_SetConVar, m_ConVars) + offsetof(CUtlVector<NetMessageCvar_t>, m_Size)) == 56);

typedef bool (*NET_SetConVar__ReadFromBufferType)(NET_SetConVar* thisptr, bf_read& buffer);
extern NET_SetConVar__ReadFromBufferType NET_SetConVar__ReadFromBufferOriginal;

bool __fastcall NET_SetConVar__ReadFromBuffer(NET_SetConVar* thisptr, bf_read& buffer);
bool __fastcall NET_SetConVar__WriteToBuffer(NET_SetConVar* thisptr, bf_write& buffer);
__int64 CConVar__GetSplitScreenPlayerSlot(char* thisptr);
void setinfopersist_cmd(const CCommand& args);

bool IsValidUserInfoKey(const char* key);
bool IsValidUserInfoValue(const char* key);