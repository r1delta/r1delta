#pragma once
#include "squirrel.h"
typedef void(__fastcall* pCHostState__State_GameShutdown_t)(void* thisptr);

extern pCHostState__State_GameShutdown_t oGameShutDown;

void Hk_CHostState__State_GameShutdown(void* thisptr);
SQInteger GetServerList(HSQUIRRELVM v);
void GetServerHeartbeat(HSQUIRRELVM v);