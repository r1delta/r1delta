#include <unordered_map>
#define DEDICATED
#ifdef DEDICATED
#define SERVER_DLL "server_ds.dll"
#define SERVER_DLLWIDE L"server_ds.dll"
#define ENGINE_DLL "engine_ds.dll"
#else
#define SERVER_DLLWIDE L"server.dll"

#define SERVER_DLL "server.dll"
#define ENGINE_DLL "engine.dll"
#endif
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
uintptr_t cvarinterface;

/* 268 */
union completionunion
{
  FnCommandCompletionCallback m_fnCompletionCallback;
  ICommandCompletionCallback *m_pCommandCompletionCallback;
};
void      (*OriginalCCVar_RegisterConCommand)(uintptr_t thisptr, ConCommandBaseR1* pCommandBase);
void      (*OriginalCCVar_UnregisterConCommand)(uintptr_t thisptr, ConCommandBaseR1* pCommandBase);
ConCommandBaseR1* (*OriginalCCVar_FindCommandBase)(uintptr_t thisptr, const char* name);
const ConCommandBaseR1* (*OriginalCCVar_FindCommandBase2)(uintptr_t thisptr, const char* name);
ConVarR1* (*OriginalCCVar_FindVar)(uintptr_t thisptr, const char* var_name);
const ConVarR1* (*OriginalCCVar_FindVar2)(uintptr_t thisptr, const char* var_name);
ConCommandR1* (*OriginalCCVar_FindCommand)(uintptr_t thisptr, const char* name);
const ConCommandR1* (*OriginalCCVar_FindCommand2)(uintptr_t thisptr, const char* name);
void      (*OriginalCCvar__InstallGlobalChangeCallback)(uintptr_t thisptr, void* func);
void      (*OriginalCCvar__RemoveGlobalChangeCallback)(uintptr_t thisptr, void* func);
void      (*OriginalCCVar_CallGlobalChangeCallbacks)(uintptr_t thisptr, ConVarR1* var, const char* pOldString, float flOldValue);
void      (*OriginalCCVar_QueueMaterialThreadSetValue1)(uintptr_t thisptr, ConVarR1* pConVar, const char* pValue);
void      (*OriginalCCVar_QueueMaterialThreadSetValue2)(uintptr_t thisptr, ConVarR1* pConVar, int nValue);
void      (*OriginalCCVar_QueueMaterialThreadSetValue3)(uintptr_t thisptr, ConVarR1* pConVar, float flValue);
struct WVar {
	ConCommandBaseR1O* r1optr;
	ConCommandBaseR1* r1ptr;
	bool is_cvar;
	bool is_r1o;
};

std::unordered_map<std::string, WVar*> ccBaseMap;

bool ConCommandBaseR1OIsCVar(ConCommandBaseR1O* ptr) {
	return ((ConCommandR1O*)ptr)->unused2;
}
ConCommandBaseR1O* convertToR1O(const ConCommandBaseR1* commandBase);
ConCommandBaseR1O* convertToR1O(ConCommandBaseR1* commandBase);
ConCommandBaseR1O* convertToR1OBase(ConCommandBaseR1* commandBase) {
	if (!commandBase)
		return NULL;
	ConCommandBaseR1O* commandBaseR1O = new ConCommandBaseR1O;

	commandBaseR1O->vtable = commandBase->vtable;
	commandBaseR1O->m_pNext = NULL;// convertToR1O(commandBase->m_pNext);
	commandBaseR1O->m_bRegistered = commandBase->m_bRegistered;
	commandBaseR1O->m_pszName = commandBase->m_pszName;
	commandBaseR1O->m_pszHelpString = commandBase->m_pszHelpString;
	commandBaseR1O->m_nFlags = commandBase->m_nFlags;
	commandBaseR1O->pad = commandBase->pad;
	commandBaseR1O->unk = nullptr;

	return commandBaseR1O;
}


ConCommandR1O* convertToR1O(ConCommandR1* command) {
	if (!command)
		return NULL;
	ConCommandR1O* commandR1O = new ConCommandR1O;
	static void* ptr = (void*)((uintptr_t)GetModuleHandleA(SERVER_DLL) + 0x9C75F0);

	*static_cast<ConCommandBaseR1O*>(commandR1O) = *convertToR1OBase(static_cast<ConCommandBaseR1*>(command));
	commandR1O->vtable = ptr;
	commandR1O->unused = command->unused;
	commandR1O->unused2 = command->unused2;
	commandR1O->m_pCommandCallback = command->m_pCommandCallback;
	commandR1O->m_pCompletionCallback = command->m_pCompletionCallback;
	commandR1O->m_nCallbackFlags = command->m_nCallbackFlags;
	commandR1O->pad = command->pad;
	std::copy(commandR1O->pad_0054, commandR1O->pad_0054 + 7, command->pad_0054);

	return commandR1O;
}

ConVarR1O* convertToR1O(ConVarR1* var) {
	if (!var)
		return NULL;
	ConVarR1O* varR1O = new ConVarR1O;

	*static_cast<ConCommandBaseR1O*>(varR1O) = *convertToR1OBase(static_cast<ConCommandBaseR1*>(var));
	//varR1O->unk = varR1O->__vftable;
	static void* ptr = (void*)((uintptr_t)GetModuleHandleA(SERVER_DLL) + 0x9C7680);
	static void* ptr2 = (void*)((uintptr_t)GetModuleHandleA(SERVER_DLL) + 0x9C74C0);

	//char whatever[19 * 8];
	//char whatever2[8 * 8];
	//size_t bytes;
	//static bool bDone = false;
	//if (!bDone) {
	//	ReadProcessMemory(GetCurrentProcess(), (void*)((uintptr_t)GetModuleHandleA("vstdlib.dll") + 0x057778), &whatever, 19 * 8, &bytes);
	//	WriteProcessMemory(GetCurrentProcess(), (void*)((uintptr_t)GetModuleHandleA(SERVER_DLL) + 0x9C7680), &whatever, 19 * 8, &bytes);
	//	ReadProcessMemory(GetCurrentProcess(), (void*)((uintptr_t)GetModuleHandleA("vstdlib.dll") + 0x057778), &whatever2, 8 * 8, &bytes);
	//	WriteProcessMemory(GetCurrentProcess(), (void*)((uintptr_t)GetModuleHandleA(SERVER_DLL) + 0x9C74C0), &whatever2, 8 * 8, &bytes);
	//}
	varR1O->vtable = ptr;//varR1O->vtable;
	varR1O->__vftable = ptr2;

	varR1O->m_pParent = varR1O;//convertToR1O(var->m_pParent);
	varR1O->m_pszDefaultValue = var->m_pszDefaultValue;
	varR1O->m_Value = var->m_Value;
	varR1O->m_bHasMin = var->m_bHasMin;
	varR1O->m_fMinVal = var->m_fMinVal;
	varR1O->m_bHasMax = var->m_bHasMax;
	varR1O->m_fMaxVal = var->m_fMaxVal;
	//std::copy(var->pad, var->pad + 32, varR1O->pad);

	return varR1O;
}


ConCommandBaseR1* convertToR1(ConCommandBaseR1O* commandBaseR1O) {
	if (!commandBaseR1O)
		return NULL;
	ConCommandBaseR1* commandBase = new ConCommandBaseR1;
//	static void* vstdlibptr = (void*)(((uintptr_t)GetModuleHandleA("vstdlib.dll")) + 0x0);
	commandBase->vtable = commandBaseR1O->vtable;
	commandBase->m_pNext = convertToR1(commandBaseR1O->m_pNext);
	commandBase->m_bRegistered = commandBaseR1O->m_bRegistered;
	commandBase->m_pszName = commandBaseR1O->m_pszName;
	commandBase->m_pszHelpString = commandBaseR1O->m_pszHelpString;
	commandBase->m_nFlags = commandBaseR1O->m_nFlags;
	commandBase->pad = commandBaseR1O->pad;

	return commandBase;
}

ConCommandR1* convertToR1(ConCommandR1O* commandR1O) {
	if (!commandR1O)
		return NULL;
	ConCommandR1* command = new ConCommandR1;
	static void* ptr = (void*)((uintptr_t)GetModuleHandleA("vstdlib.dll") + 0x0576A8);

	*static_cast<ConCommandBaseR1*>(command) = *convertToR1(static_cast<ConCommandBaseR1O*>(commandR1O));
	command->vtable = ptr;
	command->unused = commandR1O->unused;
	command->unused2 = commandR1O->unused2;
	command->m_pCommandCallback = commandR1O->m_pCommandCallback;
	command->m_pCompletionCallback = commandR1O->m_pCompletionCallback;
	command->m_nCallbackFlags = commandR1O->m_nCallbackFlags;
	command->pad = commandR1O->pad;
	std::copy(commandR1O->pad_0054, commandR1O->pad_0054 + 7, command->pad_0054);
	return command;
}

ConVarR1* convertToR1(ConVarR1O* varR1O) {
	if (!varR1O)
		return NULL;
	ConVarR1* var = new ConVarR1;

	*static_cast<ConCommandBaseR1*>(var) = *convertToR1(static_cast<ConCommandBaseR1O*>(varR1O));
	static void* ptr = (void*)((uintptr_t)GetModuleHandleA("vstdlib.dll") + 0x057778);
	static void* ptr2 = (void*)((uintptr_t)GetModuleHandleA("vstdlib.dll") + 0x057728);

	//char whatever[19 * 8];
	//char whatever2[8 * 8];
	//size_t bytes;
	//static bool bDone = false;
	//if (!bDone) {
	//	ReadProcessMemory(GetCurrentProcess(), (void*)((uintptr_t)GetModuleHandleA("vstdlib.dll") + 0x057778), &whatever, 19 * 8, &bytes);
	//	WriteProcessMemory(GetCurrentProcess(), (void*)((uintptr_t)GetModuleHandleA(SERVER_DLL) + 0x9C7680), &whatever, 19 * 8, &bytes);
	//	ReadProcessMemory(GetCurrentProcess(), (void*)((uintptr_t)GetModuleHandleA("vstdlib.dll") + 0x057778), &whatever2, 8 * 8, &bytes);
	//	WriteProcessMemory(GetCurrentProcess(), (void*)((uintptr_t)GetModuleHandleA(SERVER_DLL) + 0x9C74C0), &whatever2, 8 * 8, &bytes);
	//}
	var->vtable = ptr;//varR1O->vtable;
	var->__vftable = ptr2;
	var->m_pParent = var; //convertToR1(varR1O->m_pParent);
	var->m_pszDefaultValue = varR1O->m_pszDefaultValue;
	var->m_Value = varR1O->m_Value;
	var->m_bHasMin = varR1O->m_bHasMin;
	var->m_fMinVal = varR1O->m_fMinVal;
	var->m_bHasMax = varR1O->m_bHasMax;
	var->m_fMaxVal = varR1O->m_fMaxVal;
	std::copy(varR1O->pad, varR1O->pad + 32, var->pad);

	return var;
}
ConCommandBaseR1O* convertToR1O(ConCommandBaseR1* commandBase) {
	if (!commandBase)
		return NULL;
	if (ConCommandBaseR1OIsCVar((ConCommandBaseR1O*)commandBase)) {
		return convertToR1O((ConCommandR1*)commandBase);
	}
	else {
		return convertToR1O((ConVarR1*)commandBase);
	}
	return NULL;
}
ConCommandBaseR1O* convertToR1O(const ConCommandBaseR1* commandBase) {
	if (!commandBase)
		return NULL;
	if (ConCommandBaseR1OIsCVar((ConCommandBaseR1O*)commandBase)) {
		return convertToR1O((ConCommandR1*)commandBase);
	}
	else {
		return convertToR1O((ConVarR1*)commandBase);
	}
	return NULL;
}

typedef void (*ConCommandConstructorType)(
	ConCommandR1* newCommand, const char* name, void (*callback)(void*), const char* helpString, int flags, void* parent);
ConCommandConstructorType conCommandConstructor;
typedef void* (*CreateInterfaceFn)(const char* pName, int* pReturnCode);

void CCVar_RegisterConCommand(uintptr_t thisptr, ConCommandBaseR1O* pCommandBase) {
	if (!strcmp(pCommandBase->m_pszName, "toggleconsole"))
		return;
	if (!strcmp(pCommandBase->m_pszName, "hideconsole"))
		return;
	if (!strcmp(pCommandBase->m_pszName, "showconsole"))
		return;

//		__debugbreak();
	//std::cout << __FUNCTION__ << ": " << pCommandBase->m_pszName << std::endl;
	if (!ConCommandBaseR1OIsCVar(pCommandBase)) {
		ConCommandBaseR1* r1CommandBase = convertToR1((ConCommandR1O*)pCommandBase);
		ccBaseMap[r1CommandBase->m_pszName] = new WVar{ pCommandBase, r1CommandBase, false, true };
		OriginalCCVar_RegisterConCommand(cvarinterface, r1CommandBase);
	}
	else {
		ConCommandBaseR1* r1CommandBase = convertToR1((ConVarR1O*)pCommandBase);
		ccBaseMap[r1CommandBase->m_pszName] = new WVar{ pCommandBase, r1CommandBase, true, true };
		OriginalCCVar_RegisterConCommand(cvarinterface, r1CommandBase);
	}
}

void CCVar_UnregisterConCommand(uintptr_t thisptr, ConCommandBaseR1O* pCommandBase) {
	return;
}

ConCommandBaseR1O* CCVar_FindCommandBase(uintptr_t thisptr, const char* name) {
	std::cout << __FUNCTION__ << ": " << name << std::endl;
	return (ConCommandBaseR1O*)OriginalCCVar_FindCommandBase(cvarinterface, name);
}

const ConCommandBaseR1O* CCVar_FindCommandBase2(uintptr_t thisptr, const char* name) {
	std::cout << __FUNCTION__ << ": " << name << std::endl;
	return (ConCommandBaseR1O*)OriginalCCVar_FindCommandBase2(cvarinterface, name);
}

ConVarR1O* CCVar_FindVar(uintptr_t thisptr, const char* var_name) {
	std::cout << __FUNCTION__ << ": " << var_name << std::endl;
	return (ConVarR1O*)((uintptr_t)OriginalCCVar_FindVar(cvarinterface, var_name));
}

void GlobalChangeCallback(ConVarR1* var, const char* pOldValue) {
	var = (ConVarR1*)(((uintptr_t)var) - 48);
	std::cout << __FUNCTION__ << ": " << var->m_pszName << std::endl;
	auto it = ccBaseMap.find(var->m_pszName);
	if (it == ccBaseMap.end())
		return;
	ConVarR1O* r1ovar = ((ConVarR1O*)it->second->r1optr);
	r1ovar->m_pParent->m_Value.m_fValue = var->m_Value.m_fValue;
	r1ovar->m_pParent->m_Value.m_nValue = var->m_Value.m_nValue;
	r1ovar->m_pParent->m_Value.m_pszString = var->m_Value.m_pszString;
	r1ovar->m_pParent->m_Value.m_StringLength = var->m_Value.m_StringLength;
	//	if (it->second->is_r1o)
	//		r1ovar->m_bHasMax
	// unimplemented - impl r1o convar change callbacks
}
const ConVarR1O* CCVar_FindVar2(uintptr_t thisptr, const char* var_name) {
	//std::cout << __FUNCTION__ << ": " << var_name << std::endl;
	//static int iFlag = 0;
	//static bool bInitDone = false;
	//if (!strcmp(var_name, "developer"))
	//	iFlag++;
	//if (!strcmp(var_name, "host_thread_mode"))
	//	iFlag = 3;
	//if (iFlag == 2) {
	//	return (ConVarR1O*)((uintptr_t)OriginalCCVar_FindVar2(cvarinterface, var_name) + 8);
	//}
	//if (iFlag == 3) {
	//	return (ConVarR1O*)((uintptr_t)OriginalCCVar_FindVar2(cvarinterface, var_name) - 8);
	//}
	if (!strcmp(var_name, "room_type")) // unused but crashes if NULL
		var_name = "sv_cheats";
	static bool bDone = false;
	if (!bDone) {
		OriginalCCvar__InstallGlobalChangeCallback(cvarinterface, &GlobalChangeCallback);
		bDone = true;
	}
	auto it = ccBaseMap.find(var_name);
	ConVarR1O* r1optr = it == ccBaseMap.end() ? NULL : (ConVarR1O*)it->second->r1optr;
	if (r1optr)
		return r1optr;
	auto ret = (ConVarR1*)(OriginalCCVar_FindVar2(cvarinterface, var_name));
	if (!ret)
		return nullptr;
	ccBaseMap[var_name] = new WVar{ convertToR1O(ret), ret, true, false };
	return (ConVarR1O*)(ccBaseMap[var_name]->r1optr);
}

ConCommandR1O* CCVar_FindCommand(uintptr_t thisptr, const char* name) {
	std::cout << __FUNCTION__ << ": " << name << std::endl;
	return (ConCommandR1O*)((uintptr_t)OriginalCCVar_FindCommand(cvarinterface, name));
}

const ConCommandR1O* CCVar_FindCommand2(uintptr_t thisptr, const char* name) {
	std::cout << __FUNCTION__ << ": " << name << std::endl;
	return (ConCommandR1O*)((uintptr_t)OriginalCCVar_FindCommand2(cvarinterface, name));
}

void CCVar_CallGlobalChangeCallbacks(uintptr_t thisptr, ConVarR1O* var, const char* pOldString, float flOldValue) {
	// if this crashes YOU ARE NOT CALLING CVAR REGISTER FOR A DLL YOU SHOULD BE CALLING CVAR REGISTER FOR
	ConVarR1* r1var = (ConVarR1*)(ccBaseMap[var->m_pszName]->r1ptr);
	r1var->m_pParent->m_Value.m_fValue = var->m_Value.m_fValue;
	r1var->m_pParent->m_Value.m_nValue = var->m_Value.m_nValue;
	r1var->m_pParent->m_Value.m_pszString = var->m_Value.m_pszString;
	r1var->m_pParent->m_Value.m_StringLength = var->m_Value.m_StringLength;

	return;
	//OriginalCCVar_CallGlobalChangeCallbacks(cvarinterface, r1Var, pOldString, flOldValue);
	////delete r1Var;
}

void CCVar_QueueMaterialThreadSetValue1(uintptr_t thisptr, ConVarR1O* pConVar, const char* pValue) {
	//ConVarR1* r1Var = convertToR1(pConVar);
	//OriginalCCVar_QueueMaterialThreadSetValue1(cvarinterface, r1Var, pValue);
	////delete r1Var;
	// cus broken
}

void CCVar_QueueMaterialThreadSetValue2(uintptr_t thisptr, ConVarR1O* pConVar, int nValue) {
	//ConVarR1* r1Var = convertToR1(pConVar);
	//OriginalCCVar_QueueMaterialThreadSetValue2(cvarinterface, r1Var, nValue);
	////delete r1Var;
	// cus broken
}

void CCVar_QueueMaterialThreadSetValue3(uintptr_t thisptr, ConVarR1O* pConVar, float flValue) {
	//ConVarR1* r1Var = convertToR1(pConVar);
	//OriginalCCVar_QueueMaterialThreadSetValue3(cvarinterface, r1Var, flValue);
	////delete r1Var;
	// cus broken
}

void __fastcall CCvar__InstallGlobalChangeCallback( // cus broken
	uintptr_t thisptr,
	void* func)
{
}

void __fastcall CCvar__RemoveGlobalChangeCallback( // cus broken
	uintptr_t thisptr,
	void* func)
{
}
__int64 __fastcall CCvar__ProcessQueuedMaterialThreadConVarSets(__int64 a1)
{
	return 0; // cus broken
}
