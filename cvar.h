#include <unordered_map>

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
void      (*OriginalCCVar_CallGlobalChangeCallbacks)(uintptr_t thisptr, ConVarR1* var, const char* pOldString, float flOldValue);
void      (*OriginalCCVar_QueueMaterialThreadSetValue1)(uintptr_t thisptr, ConVarR1* pConVar, const char* pValue);
void      (*OriginalCCVar_QueueMaterialThreadSetValue2)(uintptr_t thisptr, ConVarR1* pConVar, int nValue);
void      (*OriginalCCVar_QueueMaterialThreadSetValue3)(uintptr_t thisptr, ConVarR1* pConVar, float flValue);

std::unordered_map<std::string, ConCommandBaseR1O*> ccBaseMap;
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
	commandBaseR1O->m_pNext = convertToR1O(commandBase->m_pNext);
	commandBaseR1O->m_bRegistered = commandBase->m_bRegistered;
	commandBaseR1O->m_pszName = commandBase->m_pszName;
	commandBaseR1O->m_pszHelpString = commandBase->m_pszHelpString;
	commandBaseR1O->m_nFlags = commandBase->m_nFlags;
	commandBaseR1O->pad = commandBase->pad;
	commandBaseR1O->unk = nullptr;

	return commandBaseR1O;
}

const ConCommandBaseR1O* convertToR1OBase(const ConCommandBaseR1* commandBase) {
	if (!commandBase)
		return NULL;
	ConCommandBaseR1O* commandBaseR1O = new ConCommandBaseR1O;

	commandBaseR1O->vtable = commandBase->vtable;
	commandBaseR1O->m_pNext = convertToR1O(commandBase->m_pNext);
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

	*static_cast<ConCommandBaseR1O*>(commandR1O) = *convertToR1OBase(static_cast<ConCommandBaseR1*>(command));
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
	varR1O->m_pParent = varR1O;//convertToR1O(var->m_pParent);
	varR1O->m_pszDefaultValue = var->m_pszDefaultValue;
	varR1O->m_Value = var->m_Value;
	varR1O->m_bHasMin = var->m_bHasMin;
	varR1O->m_fMinVal = var->m_fMinVal;
	varR1O->m_bHasMax = var->m_bHasMax;
	varR1O->m_fMaxVal = var->m_fMaxVal;
	std::copy(var->pad, var->pad + 32, varR1O->pad);

	return varR1O;
}


const ConVarR1O* convertToR1O(const ConVarR1* var) {
	if (!var)
		return NULL;
	ConVarR1O* varR1O = new ConVarR1O;
	varR1O->vtable = var->vtable;
	varR1O->m_pNext = convertToR1O(var->m_pNext);
	varR1O->m_bRegistered = var->m_bRegistered;
	varR1O->m_pszName = var->m_pszName;
	varR1O->m_pszHelpString = var->m_pszHelpString;
	varR1O->m_nFlags = var->m_nFlags;
	varR1O->unk = nullptr;
	varR1O->m_pParent = varR1O;
	varR1O->m_pszDefaultValue = var->m_pszDefaultValue;
	varR1O->m_Value = var->m_Value;
	varR1O->m_bHasMin = var->m_bHasMin;
	varR1O->m_fMinVal = var->m_fMinVal;
	varR1O->m_bHasMax = var->m_bHasMax;
	varR1O->m_fMaxVal = var->m_fMaxVal;
	std::copy(var->pad, var->pad + 32, varR1O->pad);

	return varR1O;
}
ConCommandBaseR1* convertToR1(ConCommandBaseR1O* commandBaseR1O) {
	if (!commandBaseR1O)
		return NULL;
	ConCommandBaseR1* commandBase = new ConCommandBaseR1;

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

	*static_cast<ConCommandBaseR1*>(command) = *convertToR1(static_cast<ConCommandBaseR1O*>(commandR1O));
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

void CCVar_RegisterConCommand(uintptr_t thisptr, ConCommandBaseR1O* pCommandBase) {
	return;
	if (!((ConCommandR1O*)pCommandBase)->unused2) {
		ConCommandBaseR1* r1CommandBase = convertToR1((ConCommandR1O*)pCommandBase);
		OriginalCCVar_RegisterConCommand(cvarinterface, r1CommandBase);
	}
	else {
		ConCommandBaseR1* r1CommandBase = convertToR1((ConVarR1O*)pCommandBase);
		OriginalCCVar_RegisterConCommand(cvarinterface, r1CommandBase);
	}
}

void CCVar_UnregisterConCommand(uintptr_t thisptr, ConCommandBaseR1O* pCommandBase) {
	return;
	//ConCommandBaseR1* r1CommandBase = convertToR1(pCommandBase);
	//OriginalCCVar_UnregisterConCommand(cvarinterface, r1CommandBase);
	////delete r1CommandBase;
}

ConCommandBaseR1O* CCVar_FindCommandBase(uintptr_t thisptr, const char* name) {
	if (ccBaseMap.find(name) != ccBaseMap.end()) {
		//delete ccBaseMap[name];
	}
	ConCommandBaseR1* r1CommandBase = OriginalCCVar_FindCommandBase(cvarinterface, name);
	if (!r1CommandBase)
		return (ConVarR1O*)r1CommandBase;
	ccBaseMap[name] = convertToR1O(r1CommandBase);
	return ccBaseMap[name];
}

const ConCommandBaseR1O* CCVar_FindCommandBase2(uintptr_t thisptr, const char* name) {
	// No deletion as it's a const function
	if (ccBaseMap.find(name) == ccBaseMap.end()) {
		const ConCommandBaseR1* r1CommandBase = OriginalCCVar_FindCommandBase2(cvarinterface, name);
		if (!r1CommandBase)
			return (const ConVarR1O*)r1CommandBase;
		ccBaseMap[name] = (ConCommandBaseR1O*)convertToR1O(r1CommandBase);
	}
	return ccBaseMap[name];
}

ConVarR1O* CCVar_FindVar(uintptr_t thisptr, const char* var_name) {
	if (ccBaseMap.find(var_name) != ccBaseMap.end()) {
		//delete ccBaseMap[var_name];
	}
	ConVarR1* r1Var = OriginalCCVar_FindVar(cvarinterface, var_name);
	if (!r1Var)
		return (ConVarR1O*)r1Var;
	ccBaseMap[var_name] = convertToR1O(r1Var);
	return (ConVarR1O*)ccBaseMap[var_name];
}

const ConVarR1O* CCVar_FindVar2(uintptr_t thisptr, const char* var_name) {
	//// No deletion as it's a const function
	//if (ccBaseMap.find(var_name) == ccBaseMap.end()) {
	//	const ConVarR1* r1Var = OriginalCCVar_FindVar2(cvarinterface, var_name);
	//	if (!r1Var)
	//		return (const ConVarR1O*)r1Var;
	//	ccBaseMap[var_name] = (ConCommandBaseR1O*)convertToR1O(r1Var);
	//}
	//return (const ConVarR1O*)ccBaseMap[var_name];
	return NULL;
}

ConCommandR1O* CCVar_FindCommand(uintptr_t thisptr, const char* name) {
	if (ccBaseMap.find(name) != ccBaseMap.end()) {
		//delete ccBaseMap[name];
	}
	ConCommandR1* r1Command = OriginalCCVar_FindCommand(cvarinterface, name);
	if (!r1Command)
		return (ConCommandR1O*)r1Command;

	ccBaseMap[name] = convertToR1O(r1Command);
	return (ConCommandR1O*)ccBaseMap[name];
}

const ConCommandR1O* CCVar_FindCommand2(uintptr_t thisptr, const char* name) {
	// No deletion as it's a const function
	if (ccBaseMap.find(name) == ccBaseMap.end()) {
		const ConCommandR1* r1Command = OriginalCCVar_FindCommand2(cvarinterface, name);
		if (!r1Command)
			return (const ConCommandR1O*)r1Command;
		ccBaseMap[name] = (ConCommandBaseR1O*)convertToR1O(r1Command);
	}
	return (ConCommandR1O*)ccBaseMap[name];
}

void CCVar_CallGlobalChangeCallbacks(uintptr_t thisptr, ConVarR1O* var, const char* pOldString, float flOldValue) {
	//ConVarR1* r1Var = convertToR1WParent(var);
	//if (!CCVar_FindCommand(cvarinterface, var->m_pszName)) // cus broken
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