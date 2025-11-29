// Networking hooks for R1Delta
#pragma once

#include "core.h"
#include "engine_vtable.h"
#include "bitbuf.h"
#include "netadr.h"

// CNetChan address hook
extern const char* (*oCNetChan__GetAddress)(CNetChan* thisptr);
const char* CNetChan__GetAddress(CNetChan* thisptr);

// AllTalk fix
extern __int64 (*oStringCompare_AllTalkHookDedi)(__int64 a1, __int64 a2);
__int64 StringCompare_AllTalkHookDedi(__int64 a1, __int64 a2);

// Circuit breaker for stuck connections
extern __int64 (*oCNetChan__SendDatagramLISTEN_Part2)(__int64 thisptr, unsigned int length, int SendToResult);
__int64 CNetChan__SendDatagramLISTEN_Part2_Hook(__int64 thisptr, unsigned int length, int SendToResult);

// Connect packet conversion
int32_t ReadConnectPacket2015AndWriteConnectPacket2014(bf_read& msg, bf_write& buffer);

// Connectionless packet processors
typedef char (*ProcessConnectionlessPacketType)(unsigned int* a1, netpacket_s* a2);
extern ProcessConnectionlessPacketType ProcessConnectionlessPacketOriginal;
extern ProcessConnectionlessPacketType ProcessConnectionlessPacketOriginalClient;

char __fastcall ProcessConnectionlessPacketDedi(unsigned int* a1, netpacket_s* a2);
char __fastcall ProcessConnectionlessPacketClient(unsigned int* a1, netpacket_s* a2);

// Retry fix
typedef void (*CL_Retry_fType)();
extern CL_Retry_fType CL_Retry_fOriginal;
void CL_Retry_f();

// ServerInfo hook
typedef char (*SVC_ServerInfo__WriteToBufferType)(__int64 a1, __int64 a2);
extern SVC_ServerInfo__WriteToBufferType SVC_ServerInfo__WriteToBufferOriginal;
char __fastcall SVC_ServerInfo__WriteToBuffer(__int64 a1, __int64 a2);
