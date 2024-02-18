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
char __fastcall MatchRecvPropsToSendProps_R(__int64 a1, __int64 a2, __int64 pSendTable, __int64 a4);
typedef char(*sub_1801C79A0Type)(__int64 a1, __int64 a2);
extern sub_1801C79A0Type sub_1801C79A0Original;
char __fastcall sub_1801C79A0(__int64 a1, __int64 a2);
