#pragma once
#include "squirrel.h"
typedef void(__fastcall* pCHostState__State_GameShutdown_t)(void* thisptr);

extern pCHostState__State_GameShutdown_t oGameShutDown;


void Hk_CHostState__State_GameShutdown(void* thisptr);
SQInteger DispatchServerListReq(HSQUIRRELVM v);
SQInteger PollServerList(HSQUIRRELVM v);
SQInteger GetServerHeartbeat(HSQUIRRELVM v);

//bool IsValidHeartBeat;

namespace MasterServerClient {
    void StartHeartbeatThread();
    void StopHeartbeatThread();
    extern std::atomic<bool> IsValidHeartBeat; // Changed to extern and atomic
}
