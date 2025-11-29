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

#include "core.h"

#include <MinHook.h>
#include <cstdlib>
#include <crtdbg.h>	
#include <new>
#include "windows.h"

#include <iostream>
#include <vector>
#include <algorithm>
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
#include "localize.h"
#include "mcp_server.h"
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
std::unordered_map<std::string, WVar*, HashStrings, std::equal_to<>> ccBaseMap;

// HUD function pointers
GetHudType GetHud;
CHudFindElementType CHudFindElement;
CHudMenuSelectMenuItemType CHudMenuSelectMenuItem;


bool ConCommandBaseR1OIsCVar(ConCommandBaseR1O* ptr) {
	return !!((ConCommandR1O*)ptr)->unused2;
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
	void* ptr = (void*)(G_server + 0x9C75F0);

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
	auto server = G_server;
	void* ptr = (void*)(server + 0x9C7680);
	void* ptr2 = (void*)(server + 0x9C74C0);

	//char whatever[19 * 8];
	//char whatever2[8 * 8];
	//size_t bytes;
	//static bool bDone = false;
	//if (!bDone) {
	//	ReadProcessMemory(GetCurrentProcess(), (void*)((uintptr_t)GetModuleHandleA("vstdlib.dll") + 0x057778), &whatever, 19 * 8, &bytes);
	//	WriteProcessMemory(GetCurrentProcess(), (void*)(server + 0x9C7680), &whatever, 19 * 8, &bytes);
	//	ReadProcessMemory(GetCurrentProcess(), (void*)((uintptr_t)GetModuleHandleA("vstdlib.dll") + 0x057778), &whatever2, 8 * 8, &bytes);
	//	WriteProcessMemory(GetCurrentProcess(), (void*)(server + 0x9C74C0), &whatever2, 8 * 8, &bytes);
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
	varR1O->m_fnChangeCallbacks = var->m_fnChangeCallbacks;
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
	//	WriteProcessMemory(GetCurrentProcess(), (void*)(G_server + 0x9C7680), &whatever, 19 * 8, &bytes);
	//	ReadProcessMemory(GetCurrentProcess(), (void*)((uintptr_t)GetModuleHandleA("vstdlib.dll") + 0x057778), &whatever2, 8 * 8, &bytes);
	//	WriteProcessMemory(GetCurrentProcess(), (void*)(G_server + 0x9C74C0), &whatever2, 8 * 8, &bytes);
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
	var->m_fnChangeCallbacks = varR1O->m_fnChangeCallbacks;
	//std::copy(varR1O->pad, varR1O->pad + 32, var->pad);

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
	//std::cout << __FUNCTION__ << ": " << name << std::endl;
	return (ConCommandBaseR1O*)OriginalCCVar_FindCommandBase(cvarinterface, name);
}

const ConCommandBaseR1O* CCVar_FindCommandBase2(uintptr_t thisptr, const char* name) {
	//std::cout << __FUNCTION__ << ": " << name << std::endl;
	return (ConCommandBaseR1O*)OriginalCCVar_FindCommandBase2(cvarinterface, name);
}

ConVarR1O* CCVar_FindVar(uintptr_t thisptr, const char* var_name) {
	ZoneScoped;
	
	//std::cout << __FUNCTION__ << ": " << var_name << std::endl;
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
static bool cvar_recursive = false;
void GlobalChangeCallback(ConVarR1* var, const char* pOldValue) {
	if (cvar_recursive)
		return;

	ZoneScoped;

	var = (ConVarR1*)(((uintptr_t)var) - 48);

	static uintptr_t physics_scaled_mem_val = 0;
	uintptr_t current = (uintptr_t)var;
	if (!physics_scaled_mem_val) {
		physics_scaled_mem_val = (uintptr_t)CCVar_FindVar2(cvarinterface, "physics_scaled_mem");
	}
	if (current == physics_scaled_mem_val) {
		return;
	}
	

	//if (ConVar_PrintDescriptionOriginal)
	//	ConVar_PrintDescription(var);
	if (!strcmp_static(var->m_pszName, "sv_portal_players")) {
		var->m_Value.m_fValue = 18.0f;
		var->m_Value.m_nValue = 18;
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
		if (r1ovar->m_pParent->m_fnChangeCallbacks[i])
			r1ovar->m_pParent->m_fnChangeCallbacks[i](r1ovar, pOldValue, atof(pOldValue));
	}

	//	if (it->second->is_r1o)
	//		r1ovar->m_bHasMax
	// unimplemented - impl r1o convar change callbacks
}
const ConVarR1O* CCVar_FindVar2(uintptr_t thisptr, const char* var_name) {
	ZoneScoped;
	
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
	//std::cout << __FUNCTION__ << ": " << name << std::endl;
	return (ConCommandR1O*)((uintptr_t)OriginalCCVar_FindCommand(cvarinterface, name));
}

const ConCommandR1O* CCVar_FindCommand2(uintptr_t thisptr, const char* name) {
	//std::cout << __FUNCTION__ << ": " << name << std::endl;
	return (ConCommandR1O*)((uintptr_t)OriginalCCVar_FindCommand2(cvarinterface, name));
}
void CCVar_CallGlobalChangeCallbacks(uintptr_t thisptr, ConVarR1O* var, const char* pOldString, float flOldValue) {
	// if this crashes YOU ARE NOT CALLING CVAR REGISTER FOR A DLL YOU SHOULD BE CALLING CVAR REGISTER FOR
	ConVarR1* r1var = (ConVarR1*)(ccBaseMap[var->m_pszName]->r1ptr);
	r1var->m_pParent->m_Value.m_fValue = var->m_Value.m_fValue;
	r1var->m_pParent->m_Value.m_nValue = var->m_Value.m_nValue;
	r1var->m_pParent->m_Value.m_pszString = var->m_Value.m_pszString;
	r1var->m_pParent->m_Value.m_StringLength = var->m_Value.m_StringLength;
	//OriginalCCVar_CallGlobalChangeCallbacks(cvarinterface, r1var, pOldString, flOldValue);
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
	auto ret = CEngineVGui__InitOriginal(a1);
	for (const auto& file : modLocalization_files) {
		o_pCLocalise__AddFile(G_localizeIface, file.c_str(), nullptr, false);
	}

	if (G_localizeIface) {
		CallVFunc<void>(8, G_localizeIface, "resource/delta_%language%.txt", nullptr, false);
	}
	return ret;
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

	ZoneScoped;

	// Create a buffer for the formatted message
	char pMessage[1024];

	// Initialize variable argument list
	va_list args;
	va_start(args, fmt);

	// Format the message
	auto len = vsnprintf(pMessage, sizeof(pMessage), fmt, args);

	// Clean up the variable argument list
	va_end(args);

	ZoneText(pMessage, len);

	// Capture for MCP server
	MCPServer::Server::GetInstance().CaptureConsoleOutput(pMessage);

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

void ClearConsoleCommand(const CCommand& args)
{
	if (!(*staticGameConsole)->m_bInitialized)
	{
		return;
	}
	(*staticGameConsole)->Clear();
}

void Slot1Command(const CCommand& args)
{
	if (!GetHud) return;
	void* hud = GetHud(0);
	if (!hud) return;

	__int64 hudMenu = CHudFindElement((__int64)hud, (__int64)"CHudMenu");
	if (hudMenu && *(_BYTE *)(hudMenu + 0x10)) {
		CHudMenuSelectMenuItem(hudMenu, 1);
	}
}

void Slot2Command(const CCommand& args)
{
	if (!GetHud) return;
	void* hud = GetHud(0);
	if (!hud) return;

	__int64 hudMenu = CHudFindElement((__int64)hud, (__int64)"CHudMenu");
	if (hudMenu && *(_BYTE *)(hudMenu + 0x10)) {
		CHudMenuSelectMenuItem(hudMenu, 2);
	}
}

void Slot3Command(const CCommand& args)
{
	if (!GetHud) return;
	void* hud = GetHud(0);
	if (!hud) return;

	__int64 hudMenu = CHudFindElement((__int64)hud, (__int64)"CHudMenu");
	if (hudMenu && *(_BYTE *)(hudMenu + 0x10)) {
		CHudMenuSelectMenuItem(hudMenu, 3);
	}
}

void Slot4Command(const CCommand& args)
{
	if (!GetHud) return;
	void* hud = GetHud(0);
	if (!hud) return;

	__int64 hudMenu = CHudFindElement((__int64)hud, (__int64)"CHudMenu");
	if (hudMenu && *(_BYTE *)(hudMenu + 0x10)) {
		CHudMenuSelectMenuItem(hudMenu, 4);
	}
}

void Slot5Command(const CCommand& args)
{
	if (!GetHud) return;
	void* hud = GetHud(0);
	if (!hud) return;

	__int64 hudMenu = CHudFindElement((__int64)hud, (__int64)"CHudMenu");
	if (hudMenu && *(_BYTE *)(hudMenu + 0x10)) {
		CHudMenuSelectMenuItem(hudMenu, 5);
	}
}

void Slot6Command(const CCommand& args)
{
	if (!GetHud) return;
	void* hud = GetHud(0);
	if (!hud) return;

	__int64 hudMenu = CHudFindElement((__int64)hud, (__int64)"CHudMenu");
	if (hudMenu && *(_BYTE *)(hudMenu + 0x10)) {
		CHudMenuSelectMenuItem(hudMenu, 6);
	}
}

void Slot7Command(const CCommand& args)
{
	if (!GetHud) return;
	void* hud = GetHud(0);
	if (!hud) return;

	__int64 hudMenu = CHudFindElement((__int64)hud, (__int64)"CHudMenu");
	if (hudMenu && *(_BYTE *)(hudMenu + 0x10)) {
		CHudMenuSelectMenuItem(hudMenu, 7);
	}
}

void Slot8Command(const CCommand& args)
{
	if (!GetHud) return;
	void* hud = GetHud(0);
	if (!hud) return;

	__int64 hudMenu = CHudFindElement((__int64)hud, (__int64)"CHudMenu");
	if (hudMenu && *(_BYTE *)(hudMenu + 0x10)) {
		CHudMenuSelectMenuItem(hudMenu, 8);
	}
}

void Slot9Command(const CCommand& args)
{
	if (!GetHud) return;
	void* hud = GetHud(0);
	if (!hud) return;

	__int64 hudMenu = CHudFindElement((__int64)hud, (__int64)"CHudMenu");
	if (hudMenu && *(_BYTE *)(hudMenu + 0x10)) {
		CHudMenuSelectMenuItem(hudMenu, 9);
	}
}

void Slot10Command(const CCommand& args)
{
	if (!GetHud) return;
	void* hud = GetHud(0);
	if (!hud) return;

	__int64 hudMenu = CHudFindElement((__int64)hud, (__int64)"CHudMenu");
	if (hudMenu && *(_BYTE *)(hudMenu + 0x10)) {
		CHudMenuSelectMenuItem(hudMenu, 10);
	}
}

// RegisterConCommand - creates a new console command
typedef void (*ConCommandConstructorType)(
	ConCommandR1* newCommand, const char* name, void (*callback)(const CCommand&), const char* helpString, int flags, void* parent);

ConCommandR1* RegisterConCommand(const char* commandName, void (*callback)(const CCommand&), const char* helpString, int flags) {
	ConCommandConstructorType conCommandConstructor = (ConCommandConstructorType)(IsDedicatedServer() ? (G_engine_ds + 0x31F260) : (G_engine + 0x4808F0));
	ConCommandR1* newCommand = new (GlobalAllocator()->mi_malloc(sizeof(ConCommandR1), TAG_GAME, HEAP_GAME)) ConCommandR1;

	conCommandConstructor(newCommand, commandName, callback, helpString, flags, nullptr);

	return newCommand;
}

// RegisterConVar - creates a new console variable
ConVarR1* RegisterConVar(const char* name, const char* value, int flags, const char* helpString) {
	typedef void (*ConVarConstructorType)(ConVarR1* newVar, const char* name, const char* value, int flags, const char* helpString);
	ConVarConstructorType conVarConstructor = (ConVarConstructorType)(IsDedicatedServer() ? (G_engine_ds + 0x320460) : (G_engine + 0x481AF0));
	ConVarR1* newVar = new (GlobalAllocator()->mi_malloc(sizeof(ConVarR1), TAG_GAME, HEAP_GAME)) ConVarR1;

	conVarConstructor(newVar, name, value, flags, helpString);

	return newVar;
}

// CVar iterator interface for finding commands/variables
class ICVarIteratorInternal
{
public:
    virtual void SetFirst() = 0;
	virtual void Next() = 0;
	virtual bool IsValid() = 0;
	virtual ConCommandBaseR1* Get() = 0;
};

inline bool CaselessStringLessThan(const char* const& lhs, const char* const& rhs) {
	if (!lhs) return false;
	if (!rhs) return true;
	return (_stricmp(lhs, rhs) < 0);
}

static bool ConVarSortFunc(ConCommandBaseR1* const& lhs, ConCommandBaseR1* const& rhs)
{
	return CaselessStringLessThan(lhs->m_pszName, rhs->m_pszName);
}

// Find command - searches for commands/cvars by name or help string
void Find(const CCommand& args)
{
	const char* search;

	if (args.ArgC() != 2)
	{
		Msg("Usage:  find <string>\n");
		return;
	}

	// Get substring to find
	search = args[1];

	// Use std::vector to store matching cvars for sorting
	std::vector<ConCommandBaseR1*> matches;

	// Use the FactoryInternalIterator to iterate through all cvars
	typedef ICVarIteratorInternal* (__thiscall *FactoryInternalIterator_t)(void* cvar);
	FactoryInternalIterator_t pFactoryInternalIterator = (FactoryInternalIterator_t)(*(uintptr_t**)((void*)cvarinterface))[40];
	ICVarIteratorInternal* it = pFactoryInternalIterator((void*)cvarinterface);
	if (it)
	{
		for (it->SetFirst(); it->IsValid(); it->Next())
		{
			ConCommandBaseR1* var = it->Get();
			if (!var) continue;

			if (!V_stristr(var->m_pszName, search) &&
				!V_stristr(var->m_pszHelpString, search))
				continue;

			matches.push_back(var);
		}
	}

	// Sort the results by name
	std::sort(matches.begin(), matches.end(), ConVarSortFunc);

	// Print the results
	for (const auto& var : matches)
	{
		ConVar_PrintDescription(var);
	}
}

// ConVar change callbacks for recent host tracking
ConVarR1* host_mostRecentMapCvar = nullptr;
ConVarR1* host_mostRecentGamemodeCvar = nullptr;

void GamemodeChangeCallback(IConVar* var_iconvar, const char* pOldValue, float flOldValue)
{
    auto Cbuf_AddText2 = (Cbuf_AddTextType)(IsDedicatedServer() ? (G_engine_ds + 0x72d70) : (G_engine + 0x102D50));

    ConVarR1* gamemodeCvar = OriginalCCVar_FindVar(cvarinterface, "mp_gamemode");
    const char* newValue = pOldValue;
    char command[256];
    snprintf(command, sizeof(command), "host_mostRecentGamemode \"%s\"\n", newValue ? newValue : "");
    Cbuf_AddText2(0, command, 0);
}

void HostMapChangeCallback(IConVar* var_iconvar, const char* pOldValue, float flOldValue)
{
    auto Cbuf_AddText2 = (Cbuf_AddTextType)(IsDedicatedServer() ? (G_engine_ds + 0x72d70) : (G_engine + 0x102D50));

    const char* newValue = pOldValue;

    // Check if the value is valid and not the lobby map
    if (newValue && newValue[0] != '\0' && strcmp_static(newValue, "mp_lobby.bsp") != 0)
    {
        char mapNameToStore[256];
        char command[256 + 30];

        strncpy(mapNameToStore, newValue, sizeof(mapNameToStore) - 1);
        mapNameToStore[sizeof(mapNameToStore) - 1] = '\0';

        // Trim ".bsp" suffix if present
        size_t len = strlen(mapNameToStore);
        const char* suffix = ".bsp";
        size_t suffixLen = strlen(suffix);

        if (len >= suffixLen && strcmp(mapNameToStore + len - suffixLen, suffix) == 0)
        {
            mapNameToStore[len - suffixLen] = '\0';
        }

        snprintf(command, sizeof(command), "host_mostRecentMap \"%s\"\n", mapNameToStore);
        Cbuf_AddText2(0, command, 0);
    }
}

void InitializeRecentHostVars()
{
    host_mostRecentGamemodeCvar = RegisterConVar(
        "host_mostRecentGamemode",
        "",
        FCVAR_HIDDEN,
        "Stores the last gamemode set via mp_gamemode."
    );

    host_mostRecentMapCvar = RegisterConVar(
        "host_mostRecentMap",
        "",
        FCVAR_HIDDEN,
        "Stores the last map set via host_map, excluding mp_lobby."
    );

    ConVarR1* mp_gamemode = OriginalCCVar_FindVar(cvarinterface, "mp_gamemode");
    ConVarR1* host_map = OriginalCCVar_FindVar(cvarinterface, "host_map");
    {
        mp_gamemode->m_fnChangeCallbacks.AddToTail((FnChangeCallback_t)GamemodeChangeCallback);
        GamemodeChangeCallback(nullptr, "", 0.0f);
    }
    {
        host_map->m_fnChangeCallbacks.AddToTail((FnChangeCallback_t)HostMapChangeCallback);
        HostMapChangeCallback(nullptr, "", 0.0f);
    }

    auto m_sensitivity = OriginalCCVar_FindVar(cvarinterface, "m_sensitivity");
    if (m_sensitivity) {
        m_sensitivity->m_fMinVal = 0.01f;
    }
}
