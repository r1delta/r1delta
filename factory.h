#pragma once
#include "cvar.h"
typedef void* (*CreateInterfaceFn)(const char* pName, int* pReturnCode);
typedef char(__fastcall* CServerGameDLL__DLLInitType)(void* thisptr, CreateInterfaceFn appSystemFactory,
	CreateInterfaceFn physicsFactory, CreateInterfaceFn fileSystemFactory,
	void* pGlobals);
extern CServerGameDLL__DLLInitType CServerGameDLL__DLLInitOriginal;
char __fastcall CServerGameDLL__DLLInit(void* thisptr, CreateInterfaceFn appSystemFactory,
	CreateInterfaceFn physicsFactory, CreateInterfaceFn fileSystemFactory,
	void* pGlobals);
__int64 VStdLib_GetICVarFactory();