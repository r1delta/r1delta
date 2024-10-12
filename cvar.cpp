// %*++***###*##**##++**+++*++*%%%%%%%+*%+#*+%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%#=%%%#**#+#%
// ==----------------------------------------------------------------------=================+
// =------------------------------------::----------------------------------===---==========+
// ---------------------------------:-:--::::-::::-------------------=======================+
// =-------------------------------::::::::-::::-:::----------==============+===+++=========+
// ----------------------------::::::--:---=====----------===========++==++++++++++++++++++++
// ----------------------------:-----:---==++++++====-==========++++++++++++++++++++++++++++*
// -------------------------------------=+++++++=============++++++++++++++++++++++++++++++**
// -------------------------------------=++++*+========++++++++++++++++++++++++++++++++++++**
// ----------------------------:::::::--=+++++=======+++++++++++++++++++++++************++++*
// ---------------------::::::::::::::::-==+++===++++++++++++++++++++++++********###%%%##*++*
// -------:::::::::::::::::::::::::::::::-=====+####**+++++++++++++++++*********#%%%@@@@%%#**
// ------:-:::::::::::::::::::::::::::::::-====*%%%%#*++++++++++++++++++********##%@@@@@%%#**
// ----------::::::::::::::::::::::::::-=--====+#%%%*++++++++++++++++++++*********##%%%%%#***
// -------------=*=-:::::::::::::::::-=++======++***+++++++++++++++++++**************###*****
// -------------=*#=-------======++++*###*+=+=++=++++++++++*+++******************************
// =-----=======+*#*+++++++*****##########+=++++++++++***************************************
// +++++++++++****#################*****#*+=+++++++++****************************************
// ++**+++++++++++++======+++++++++++++****+=+++***################**************************
// *****+=--------::-::::::::::::::::::------=*#%%%%%%%%%%%%%%%%%%%#####*********************
// ******=-----------:::::::::::---:::::::::-=#%%%%@@@@@@@@@@@@@@%%%%###********************#
// ******=---------------:::::::::::-:::::::-*%%%@@@@@@@@@@@@@@@@@%%%%##********************#
// ****#*=-----------------:::::::::::::::::-=*%%@@@@@@@@@@@@@@@@@@%%##*********************#
// ******+===-------------::::::::::::---:::--=*#%%%@@@@@@@@@@@@@%%######**#**************###
// ==++==------------------:::::::::::::-------=+**##%%%@%%%%%%%%##########*****************#
// ==--------------------------::-:::::::::::---=++**##%%%%%%%%%%%##########*************####
// =--------------------------------:---::::--:--==+**###%#%%%%%%%%%%%#####**************####
// ====--------------------------:-------::-------==+++****###########******************#####
// ===--==------------------------------------::---==+++++******************************#####
// ===-------------------------------------:::-:----=+++********************************####%
// =====---------------------------------------------=++++******************************####%
// ======------------------==------------------------==+++***************************######%%
// =========-----===--------==------------------------==++********#*#####**#######*########%%

#include <MinHook.h>
#include <cstdlib>
#include <crtdbg.h>	
#include <new>
#include "windows.h"
#include "tier0_orig.h"
#include <iostream>
#include "cvar.h"
#include <winternl.h>  // For UNICODE_STRING.
#include <fstream>
#include <filesystem>
#include <intrin.h>
#include "memory.h"
#include "filesystem.h"
#include "defs.h"
#include "factory.h"
#include "logging.h"
#include "load.h"
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
uintptr_t cvarinterface;
std::unordered_map<std::string, WVar*> ccBaseMap;


bool ConCommandBaseR1OIsCVar(ConCommandBaseR1O* ptr) {
	return !!((ConCommandR1O*)ptr)->unused2;
}
ConCommandBaseR1O* convertBaseToR1O(ConCommandBaseR1* commandBase) {
	if (!commandBase)
		return nullptr;

	ConCommandBaseR1O* commandBaseR1O = new ConCommandBaseR1O;

	// Copy everything except the last field (unk)
	std::memcpy(commandBaseR1O, commandBase, sizeof(ConCommandBaseR1));

	// Set unk to nullptr
	commandBaseR1O->unk = nullptr;

	// Handle m_pNext separately
	commandBaseR1O->m_pNext = nullptr;  // We'll handle this in the main conversion functions

	return commandBaseR1O;
}

ConCommandR1O* convertCommandToR1O(ConCommandR1* command) {
	if (!command)
		return nullptr;

	ConCommandR1O* commandR1O = new ConCommandR1O;

	// Copy the base class
	std::memcpy(static_cast<ConCommandBaseR1O*>(commandR1O), convertBaseToR1O(static_cast<ConCommandBaseR1*>(command)), sizeof(ConCommandBaseR1O));

	// Copy the rest of the fields
	std::memcpy(&commandR1O->unused, &command->unused, sizeof(ConCommandR1O) - sizeof(ConCommandBaseR1O));

	// Set vtable
	void* ptr = (void*)(G_server + 0x9C75F0);
	commandR1O->vtable = ptr;

	return commandR1O;
}

ConVarR1O* convertVarToR1O(ConVarR1* var) {
	if (!var)
		return nullptr;

	ConVarR1O* varR1O = new ConVarR1O;

	// Copy the base class
	std::memcpy(static_cast<ConCommandBaseR1O*>(varR1O), convertBaseToR1O(static_cast<ConCommandBaseR1*>(var)), sizeof(ConCommandBaseR1O));

	// Copy the rest of the fields
	std::memcpy(&varR1O->m_pParent, &var->m_pParent, sizeof(ConVarR1O) - sizeof(ConCommandBaseR1O));

	// Set vtables
	auto server = G_server;
	void* ptr = (void*)(server + 0x9C7680);
	void* ptr2 = (void*)(server + 0x9C74C0);
	varR1O->vtable = ptr;
	varR1O->__vftable = ptr2;

	// Handle m_pParent separately
	varR1O->m_pParent = varR1O; // Self-reference, as in the original code

	return varR1O;
}

ConCommandBaseR1* convertBaseToR1(ConCommandBaseR1O* commandBaseR1O) {
	if (!commandBaseR1O)
		return nullptr;

	ConCommandBaseR1* commandBase = new ConCommandBaseR1;

	// Copy everything except the last field (unk)
	std::memcpy(commandBase, commandBaseR1O, sizeof(ConCommandBaseR1));

	// Handle m_pNext separately
	commandBase->m_pNext = nullptr;  // We'll handle this in the main conversion functions

	return commandBase;
}

ConCommandR1* convertCommandToR1(ConCommandR1O* commandR1O) {
	if (!commandR1O)
		return nullptr;

	ConCommandR1* command = new ConCommandR1;

	// Copy the base class
	std::memcpy(static_cast<ConCommandBaseR1*>(command), convertBaseToR1(static_cast<ConCommandBaseR1O*>(commandR1O)), sizeof(ConCommandBaseR1));

	// Copy the rest of the fields
	std::memcpy(&command->unused, &commandR1O->unused, sizeof(ConCommandR1) - sizeof(ConCommandBaseR1));

	// Set vtable
	static void* ptr = (void*)((uintptr_t)GetModuleHandleA("vstdlib.dll") + 0x0576A8);
	command->vtable = ptr;

	return command;
}

ConVarR1* convertVarToR1(ConVarR1O* varR1O) {
	if (!varR1O)
		return nullptr;

	ConVarR1* var = new ConVarR1;

	// Copy the base class
	std::memcpy(static_cast<ConCommandBaseR1*>(var), convertBaseToR1(static_cast<ConCommandBaseR1O*>(varR1O)), sizeof(ConCommandBaseR1));

	// Copy the rest of the fields
	std::memcpy(&var->m_pParent, &varR1O->m_pParent, sizeof(ConVarR1) - sizeof(ConCommandBaseR1));

	// Set vtables
	static void* ptr = (void*)((uintptr_t)GetModuleHandleA("vstdlib.dll") + 0x057778);
	static void* ptr2 = (void*)((uintptr_t)GetModuleHandleA("vstdlib.dll") + 0x057728);
	var->vtable = ptr;
	var->__vftable = ptr2;

	// Handle m_pParent separately
	var->m_pParent = var; // Self-reference, as in the original code

	return var;
}

// Main conversion functions
ConCommandBaseR1O* convertToR1O(ConCommandBaseR1* commandBase) {
	if (!commandBase)
		return nullptr;

	ConCommandBaseR1O* result;
	if (ConCommandBaseR1OIsCVar((ConCommandBaseR1O*)commandBase)) {
		result = (ConCommandBaseR1O*)convertVarToR1O((ConVarR1*)commandBase);
	}
	else {
		result = (ConCommandBaseR1O*)convertCommandToR1O((ConCommandR1*)commandBase);
	}

	// Handle m_pNext
	if (result) {
		result->m_pNext = convertToR1O(commandBase->m_pNext);
	}

	return result;
}

ConCommandBaseR1* convertToR1(ConCommandBaseR1O* commandBaseR1O) {
	if (!commandBaseR1O)
		return nullptr;

	ConCommandBaseR1* result;
	if (ConCommandBaseR1OIsCVar(commandBaseR1O)) {
		result = (ConCommandBaseR1*)convertVarToR1((ConVarR1O*)commandBaseR1O);
	}
	else {
		result = (ConCommandBaseR1*)convertCommandToR1((ConCommandR1O*)commandBaseR1O);
	}

	// Handle m_pNext
	if (result) {
		result->m_pNext = convertToR1(commandBaseR1O->m_pNext);
	}

	return result;
}
void CCVar_RegisterConCommand(uintptr_t thisptr, ConCommandBaseR1O* pCommandBase) {
	if (!strcmp_static(pCommandBase->m_pszName, "toggleconsole"))
		return;
	if (!strcmp_static(pCommandBase->m_pszName, "hideconsole"))
		return;
	if (!strcmp_static(pCommandBase->m_pszName, "showconsole"))
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
	return OriginalCCVar_UnregisterConCommand(cvarinterface, (ConCommandR1*)pCommandBase);
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
static bool recursive = false;
void GlobalChangeCallback(ConVarR1* var, const char* pOldValue) {
	if (recursive)
		return;
	var = (ConVarR1*)(((uintptr_t)var) - 48);
	if (ConVar_PrintDescriptionOriginal)
		ConVar_PrintDescription(var);
	if (!strcmp(var->m_pszName, "sv_portal_players")) {
		var->m_Value.m_fValue = 1.0f;
		var->m_Value.m_nValue = 1;
	}
		
	auto it = ccBaseMap.find(var->m_pszName);
	if (it == ccBaseMap.end())
		return;
	ConVarR1O* r1ovar = ((ConVarR1O*)it->second->r1optr);
	r1ovar->m_pParent->m_Value.m_fValue = var->m_Value.m_fValue;
	r1ovar->m_pParent->m_Value.m_nValue = var->m_Value.m_nValue;
	r1ovar->m_pParent->m_Value.m_pszString = var->m_Value.m_pszString;
	r1ovar->m_pParent->m_Value.m_StringLength = var->m_Value.m_StringLength;
	for (int i = 0; i < r1ovar->m_pParent->m_fnChangeCallbacks.Count(); ++i)
	{
		r1ovar->m_pParent->m_fnChangeCallbacks[i](r1ovar, pOldValue, atof(pOldValue));
	}

	//	if (it->second->is_r1o)
	//		r1ovar->m_bHasMax
	// unimplemented - impl r1o convar change callbacks
}
const ConVarR1O* CCVar_FindVar2(uintptr_t thisptr, const char* var_name) {
	//std::cout << __FUNCTION__ << ": " << var_name << std::endl;
	//static int iFlag = 0;
	//static bool bInitDone = false;
	//if (!strcmp_static(var_name, "developer"))
	//	iFlag++;
	//if (!strcmp_static(var_name, "host_thread_mode"))
	//	iFlag = 3;
	//if (iFlag == 2) {
	//	return (ConVarR1O*)((uintptr_t)OriginalCCVar_FindVar2(cvarinterface, var_name) + 8);
	//}
	//if (iFlag == 3) {
	//	return (ConVarR1O*)((uintptr_t)OriginalCCVar_FindVar2(cvarinterface, var_name) - 8);
	//}
	if (!strcmp_static(var_name, "room_type")) // unused but crashes if NULL
		var_name = "portal_funnel_debug";
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
	OriginalCCVar_CallGlobalChangeCallbacks(cvarinterface, r1var, pOldString, flOldValue);
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
typedef char (*CEngineVGui__InitType)(__int64 a1);
CEngineVGui__InitType CEngineVGui__InitOriginal;
char CEngineVGui__Init(__int64 a1)
{
	staticGameConsole = (CGameConsole**)(G_engine + 0x316AC48);
	*staticGameConsole = (CGameConsole*)(reinterpret_cast<CreateInterfaceFn>(GetProcAddress((HMODULE)G_client, "CreateInterface"))("GameConsole004", 0));

	return CEngineVGui__InitOriginal(a1);
}

typedef char (*CEngineVGui__HideGameUIType)(__int64 a1);
CEngineVGui__HideGameUIType CEngineVGui__HideGameUIOriginal;
CGameConsole** staticGameConsole;
static bool recurse = false;
char __fastcall CEngineVGui__HideGameUI(__int64 a1)
{
	char ret = CEngineVGui__HideGameUIOriginal(a1);
	if (ret && staticGameConsole && *staticGameConsole && !recurse) {
		recurse = true;
		(*staticGameConsole)->Hide();
	}
	recurse = false;
	return ret;
}

void Con_ColorPrintf(const SourceColor* clr, char* fmt, ...)
{
	if (!staticGameConsole) return;
	if (!*staticGameConsole) return;
	if (!((*staticGameConsole)->m_pConsole)) return;
	if (!((*staticGameConsole)->m_pConsole->m_pConsolePanel)) return;

	// Create a buffer for the formatted message
	char pMessage[1024];

	// Initialize variable argument list
	va_list args;
	va_start(args, fmt);

	// Format the message
	vsnprintf(pMessage, sizeof(pMessage), fmt, args);

	// Clean up the variable argument list
	va_end(args);

	// Print the message with color
	(*staticGameConsole)->m_pConsole->m_pConsolePanel->ColorPrint(*clr, pMessage);
}
void ToggleConsoleCommand(const CCommand& args)
{
	if (!(*staticGameConsole)->m_bInitialized)
	{
		return;
	}

	if (!(*staticGameConsole)->IsConsoleVisible())
	{
		//typedef void (*Cbuf_AddTextType)(int a1, const char* a2, unsigned int a3);
		//Cbuf_AddTextType Cbuf_AddText = (Cbuf_AddTextType)(G_engine + 0x102D50);
		//Cbuf_AddText(0, "gameui_activate\n");
		(*staticGameConsole)->Activate();
	}
	else
	{
		(*staticGameConsole)->Hide();
	}
}
