#pragma once
#include <unordered_map>
#include "core.h"
/* 269 */
struct ConCommandBaseR1
{
  void *vtable;
  ConCommandBaseR1 *m_pNext;
  bool m_bRegistered;
  const char *m_pszName;
  const char *m_pszHelpString;
  int m_nFlags;
  int pad;
};
struct ConCommandBaseR1O
{
  void *vtable;
  ConCommandBaseR1O *m_pNext;
  bool m_bRegistered;
  const char *m_pszName;
  const char *m_pszHelpString;
  int m_nFlags;
  int pad;
  void* unk;
};

/* 267 */
struct IConVar
{
  void *__vftable;
};

/* 265 */
struct CVValue_t
{
  char *m_pszString;
  __int64 m_StringLength;
  float m_fValue;
  int m_nValue;
};

/* 258 */
struct ConVarR1 : ConCommandBaseR1, IConVar
{
  ConVarR1 *m_pParent;
  const char *m_pszDefaultValue;
  CVValue_t m_Value;
  bool m_bHasMin;
  float m_fMinVal;
  bool m_bHasMax;
  float m_fMaxVal;
  char pad[32];
};
struct ConVarR1O : ConCommandBaseR1O, IConVar
{
  ConVarR1O *m_pParent;
  const char *m_pszDefaultValue;
  CVValue_t m_Value;
  bool m_bHasMin;
  float m_fMinVal;
  bool m_bHasMax;
  float m_fMaxVal;
  char pad[32];
};
/* 264 */
typedef void (__stdcall *FnCommandCallback_t)(const void *command);

/* 262 */
typedef int (__fastcall *FnCommandCompletionCallback)(const char *, char (*)[64]);

/* 259 */
struct __declspec(align(8)) ConCommandR1 : ConCommandBaseR1
{
  void *unused;
  void *unused2;
  FnCommandCallback_t m_pCommandCallback;
  FnCommandCompletionCallback m_pCompletionCallback;
  char m_nCallbackFlags;
  char pad_0054[7];

};

struct __declspec(align(8)) ConCommandR1O : ConCommandBaseR1O
{
  void *unused;
  void *unused2;
  FnCommandCallback_t m_pCommandCallback;
  FnCommandCompletionCallback m_pCompletionCallback;
  char m_nCallbackFlags;
  char pad_0054[7];
};

/* 260 */
struct ICommandCompletionCallback
{
  void *__vftable;
};

/* 261 */
struct ICommandCallback
{
  void *__vftable;
};

/* 263 */
typedef void (__fastcall *FnCommandCallbackV1_t)();

/* 266 */
union callbackunion
{
  FnCommandCallbackV1_t m_fnCommandCallbackV1;
  FnCommandCallback_t m_fnCommandCallback;
  ICommandCallback *m_pCommandCallback;
};
extern uintptr_t cvarinterface;

/* 268 */
union completionunion
{
  FnCommandCompletionCallback m_fnCompletionCallback;
  ICommandCompletionCallback *m_pCommandCompletionCallback;
};
struct WVar {
	ConCommandBaseR1O* r1optr;
	ConCommandBaseR1* r1ptr;
	bool is_cvar;
	bool is_r1o;
};

extern void      (*OriginalCCVar_RegisterConCommand)(uintptr_t thisptr, ConCommandBaseR1* pCommandBase);
extern void      (*OriginalCCVar_UnregisterConCommand)(uintptr_t thisptr, ConCommandBaseR1* pCommandBase);
extern ConCommandBaseR1* (*OriginalCCVar_FindCommandBase)(uintptr_t thisptr, const char* name);
extern const ConCommandBaseR1* (*OriginalCCVar_FindCommandBase2)(uintptr_t thisptr, const char* name);
extern ConVarR1* (*OriginalCCVar_FindVar)(uintptr_t thisptr, const char* var_name);
extern const ConVarR1* (*OriginalCCVar_FindVar2)(uintptr_t thisptr, const char* var_name);
extern ConCommandR1* (*OriginalCCVar_FindCommand)(uintptr_t thisptr, const char* name);
extern const ConCommandR1* (*OriginalCCVar_FindCommand2)(uintptr_t thisptr, const char* name);
extern void      (*OriginalCCvar__InstallGlobalChangeCallback)(uintptr_t thisptr, void* func);
extern void      (*OriginalCCvar__RemoveGlobalChangeCallback)(uintptr_t thisptr, void* func);
extern void      (*OriginalCCVar_CallGlobalChangeCallbacks)(uintptr_t thisptr, ConVarR1* var, const char* pOldString, float flOldValue);
extern void      (*OriginalCCVar_QueueMaterialThreadSetValue1)(uintptr_t thisptr, ConVarR1* pConVar, const char* pValue);
extern void      (*OriginalCCVar_QueueMaterialThreadSetValue2)(uintptr_t thisptr, ConVarR1* pConVar, int nValue);
extern void      (*OriginalCCVar_QueueMaterialThreadSetValue3)(uintptr_t thisptr, ConVarR1* pConVar, float flValue);
extern std::unordered_map<std::string, WVar*> ccBaseMap;

void CCVar_RegisterConCommand(uintptr_t thisptr, ConCommandBaseR1O* pCommandBase);
void CCVar_UnregisterConCommand(uintptr_t thisptr, ConCommandBaseR1O* pCommandBase);
ConCommandBaseR1O* CCVar_FindCommandBase(uintptr_t thisptr, const char* name);
const ConCommandBaseR1O* CCVar_FindCommandBase2(uintptr_t thisptr, const char* name);
ConVarR1O* CCVar_FindVar(uintptr_t thisptr, const char* var_name);
const ConVarR1O* CCVar_FindVar2(uintptr_t thisptr, const char* var_name);
ConCommandR1O* CCVar_FindCommand(uintptr_t thisptr, const char* name);
const ConCommandR1O* CCVar_FindCommand2(uintptr_t thisptr, const char* name);
void CCVar_CallGlobalChangeCallbacks(uintptr_t thisptr, ConVarR1O* var, const char* pOldString, float flOldValue);
void CCVar_QueueMaterialThreadSetValue1(uintptr_t thisptr, ConVarR1O* pConVar, const char* pValue);
void CCVar_QueueMaterialThreadSetValue2(uintptr_t thisptr, ConVarR1O* pConVar, int nValue);
void CCVar_QueueMaterialThreadSetValue3(uintptr_t thisptr, ConVarR1O* pConVar, float flValue);
void CCvar__InstallGlobalChangeCallback(uintptr_t thisptr, void* func);
void CCvar__RemoveGlobalChangeCallback(uintptr_t thisptr, void* func);
__int64 CCvar__ProcessQueuedMaterialThreadConVarSets(__int64 a1);
