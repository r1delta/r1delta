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
typedef void* (*CScriptManager__CreateNewVMType)(__int64 a1, int a2, unsigned int a3);
extern CScriptManager__CreateNewVMType CScriptManager__CreateNewVMOriginal;
void* CScriptManager__CreateNewVM(__int64 a1, int a2, unsigned int a3);
typedef __int64 (*CScriptVM__ctortype)(void* thisptr);
extern CScriptVM__ctortype CScriptVM__ctororiginal;
__int64 __fastcall CScriptVM__ctor(void* thisptr);
typedef void* (*CScriptVM__GetUnknownVMPtrType)();
extern CScriptVM__GetUnknownVMPtrType CScriptVM__GetUnknownVMPtrOriginal;

void* CScriptVM__GetUnknownVMPtr();

typedef __int64 (*sub_180008AB0Type)(__int64 a1, void* a2, unsigned int* a3, unsigned int a4, __int64 a5, int* a6);
extern sub_180008AB0Type sub_180008AB0Original;
__int64 __fastcall sub_180008AB0(__int64 a1, void* a2, unsigned int* a3, unsigned int a4, __int64 a5, int* a6);
typedef void (*CScriptManager__DestroyVMType)(void* a1, void* vmptr);
extern CScriptManager__DestroyVMType CScriptManager__DestroyVMOriginal;
void __fastcall CScriptManager__DestroyVM(void* a1, void* vmptr);

struct ScriptVariant_t;
typedef void (*CSquirrelVM__RegisterFunctionGutsType)(__int64* a1, __int64 a2, const char** a3);
extern CSquirrelVM__RegisterFunctionGutsType CSquirrelVM__RegisterFunctionGutsOriginal;
typedef __int64 (*CSquirrelVM__PushVariantType)(__int64* a1, ScriptVariant_t* a2);
extern CSquirrelVM__PushVariantType CSquirrelVM__PushVariantOriginal;
typedef char (*CSquirrelVM__ConvertToVariantType)(__int64* a1, __int64 a2, ScriptVariant_t* a3);
extern CSquirrelVM__ConvertToVariantType CSquirrelVM__ConvertToVariantOriginal;
typedef __int64 (*CSquirrelVM__ReleaseValueType)(__int64* a1, ScriptVariant_t* a2);
extern CSquirrelVM__ReleaseValueType CSquirrelVM__ReleaseValueOriginal;
typedef bool (*CSquirrelVM__SetValueType)(__int64* a1, void* a2, unsigned int a3, ScriptVariant_t* a4);
extern CSquirrelVM__SetValueType CSquirrelVM__SetValueOriginal;
typedef bool (*CSquirrelVM__SetValueExType)(__int64* a1, __int64 a2, const char* a3, ScriptVariant_t* a4);
extern CSquirrelVM__SetValueExType CSquirrelVM__SetValueExOriginal;
typedef __int64 (*CSquirrelVM__TranslateCallType)(__int64* a1);
extern CSquirrelVM__TranslateCallType CSquirrelVM__TranslateCallOriginal;
void __fastcall CSquirrelVM__TranslateCall(__int64* a1);
void __fastcall CSquirrelVM__RegisterFunctionGuts(__int64* a1, __int64 a2, const char** a3);
__int64 __fastcall CSquirrelVM__PushVariant(__int64* a1, ScriptVariant_t* a2);
char __fastcall CSquirrelVM__ConvertToVariant(__int64* a1, __int64 a2, ScriptVariant_t* a3);
__int64 __fastcall CSquirrelVM__ReleaseValue(__int64* a1, ScriptVariant_t* a2);
bool __fastcall CSquirrelVM__SetValue(__int64* a1, void* a2, unsigned int a3, ScriptVariant_t* a4);
bool __fastcall CSquirrelVM__SetValueEx(__int64* a1, __int64 a2, const char* a3, ScriptVariant_t* a4);