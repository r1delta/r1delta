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
#include "r1d_version.h"
#include "server_usercmd.h"

void AddBotDummyConCommand(const CCommand& args);
void script_cmd(const CCommand& args);

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
int       (*OriginalCCvar__ProcessQueuedMaterialThreadConVarSets)(uintptr_t thisptr);
void*     (*OriginalCCvar__FactoryInternalIterator)(uintptr_t thisptr);
uintptr_t cvarinterface;
std::unordered_map<std::string, WVar*, HashStrings, std::equal_to<>> ccBaseMap;
ConVarR1O* convertToR1O(ConVarR1* var);
static std::vector<FnChangeCallback_t> s_R1OGlobalChangeCallbacks;
static bool s_R1OCallingGlobalChangeCallbacks;
static bool s_R1BackingGlobalChangeCallbackInstalled;

static void InvokeR1OGlobalChangeCallbacks(ConVarR1O* var, const char* pOldString, float flOldValue)
{
	if (!IsR1ODedicatedServer() || !var || s_R1OGlobalChangeCallbacks.empty() || s_R1OCallingGlobalChangeCallbacks)
		return;

	ConVarR1O* parent = var->m_pParent ? var->m_pParent : var;
	IConVar* iconVar = static_cast<IConVar*>(parent);
	auto callbacks = s_R1OGlobalChangeCallbacks;

	s_R1OCallingGlobalChangeCallbacks = true;
	for (FnChangeCallback_t callback : callbacks) {
		if (callback)
			callback(iconVar, pOldString, flOldValue);
	}
	s_R1OCallingGlobalChangeCallbacks = false;
}

void GlobalChangeCallback(ConVarR1* var, const char* pOldValue);

static void EnsureR1BackingGlobalChangeCallbackInstalled()
{
	if (s_R1BackingGlobalChangeCallbackInstalled || !OriginalCCvar__InstallGlobalChangeCallback || !cvarinterface)
		return;

	OriginalCCvar__InstallGlobalChangeCallback(cvarinterface, &GlobalChangeCallback);
	s_R1BackingGlobalChangeCallbackInstalled = true;
}

static void CvarDebugMessageBox(const char* functionName)
{
//	MessageBoxA(nullptr, functionName, "R1Delta cvar debug", MB_OK | MB_ICONINFORMATION);
}

static ConVarR1* GetR1ConVarForR1O(ConVarR1O* pConVar)
{
	if (!pConVar || !pConVar->m_pszName)
		return nullptr;

	auto it = ccBaseMap.find(pConVar->m_pszName);
	if (it == ccBaseMap.end() || !it->second || !it->second->r1ptr)
		return nullptr;

	return static_cast<ConVarR1*>(it->second->r1ptr);
}

static void DebugCVarFind(const char* functionName, const char* varName, const void* result)
{
	if (!varName || (IsR1ODedicatedServer() && !AreR1OFakeDediVerboseLogsEnabled()))
		return;

	if (strcmp(varName, "developer")
		&& strcmp(varName, "sv_cheats")
		&& strcmp(varName, "commentary")
		&& strcmp(varName, "host_thread_mode"))
		return;

	char buffer[384];
	_snprintf_s(
		buffer,
		sizeof(buffer),
		_TRUNCATE,
		"R1Delta: %s(%s) -> %p\n",
		functionName ? functionName : "CCVar_FindVar",
		varName,
		result);
	OutputDebugStringA(buffer);
}

static ConVarR1O* WrapFoundR1ConVar(const char* varName, ConVarR1* r1Var)
{
	if (!varName || !r1Var)
		return nullptr;

	auto converted = convertToR1O(r1Var);
	ccBaseMap[varName] = new WVar{ converted, r1Var, true, false };
	return converted;
}


static uintptr_t GetR1OTier1ModuleBase()
{
	if (IsR1ODedicatedServer()) {
		if (G_engine_r1o)
			return G_engine_r1o;
		if (G_engine)
			return G_engine;
		return reinterpret_cast<uintptr_t>(GetModuleHandleA("engine_r1o.dll"));
	}

	return G_server;
}

static void* GetR1OConCommandVTable()
{
	const uintptr_t base = GetR1OTier1ModuleBase();
	return reinterpret_cast<void*>(base + (IsR1ODedicatedServer() ? 0x57BDC8 : 0x9C75F0));
}

static void* GetR1OConVarVTable()
{
	const uintptr_t base = GetR1OTier1ModuleBase();
	return reinterpret_cast<void*>(base + (IsR1ODedicatedServer() ? 0x57BE58 : 0x9C7680));
}

static void* GetR1OIConVarVTable()
{
	const uintptr_t base = GetR1OTier1ModuleBase();
	return reinterpret_cast<void*>(base + (IsR1ODedicatedServer() ? 0x57BC30 : 0x9C74C0));
}

// HUD function pointers
GetHudType GetHud;
CHudFindElementType CHudFindElement;
CHudMenuSelectMenuItemType CHudMenuSelectMenuItem;


bool ConCommandBaseR1OIsCVar(ConCommandBaseR1O* ptr) {
	return !!((ConCommandR1O*)ptr)->unused2;
}

static bool IsR1ODediCommandRegistrationInteresting(const char* name)
{
	if (!name)
		return false;

	return !strcmp_static(name, "exec")
		|| !strcmp_static(name, "stuffcmds")
		|| !strcmp_static(name, "map")
		|| !strcmp_static(name, "host_map")
		|| !strcmp_static(name, "ss_map")
		|| !strcmp_static(name, "changelevel")
		|| !strcmp_static(name, "changelevel2")
		|| !strcmp_static(name, "disconnect")
		|| !strcmp_static(name, "developer")
		|| !strcmp_static(name, "sv_cheats")
		|| !strcmp_static(name, "commentary")
		|| !strcmp_static(name, "host_thread_mode")
		|| !strcmp_static(name, "ent_fire")
		|| !strcmp_static(name, "bot_dummy")
		|| !strcmp_static(name, "fatal_script_error_prompt")
		|| !strcmp_static(name, "fatal_script_errors")
		|| !strcmp_static(name, "fatal_script_errors_client")
		|| !strcmp_static(name, "fatal_script_errors_server")
		|| !strcmp_static(name, "script");
}

static bool IsR1OFatalScriptPolicyConVar(const char* name)
{
	if (!name)
		return false;

	return !strcmp_static(name, "fatal_script_error_prompt")
		|| !strcmp_static(name, "fatal_script_errors")
		|| !strcmp_static(name, "fatal_script_errors_client")
		|| !strcmp_static(name, "fatal_script_errors_server");
}

static ConVarR1O* ExposeR1OFatalScriptPolicyConVar(const char* name, ConVarR1O* var)
{
	if (!IsR1ODedicatedServer() || !IsR1OFatalScriptPolicyConVar(name) || !var)
		return var;

	const int exposedFlags = ~(FCVAR_HIDDEN | FCVAR_DEVELOPMENTONLY);
	var->m_nFlags &= exposedFlags;
	ConVarR1O* parent = var->m_pParent ? var->m_pParent : var;
	parent->m_nFlags &= exposedFlags;
	return var;
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

	*static_cast<ConCommandBaseR1O*>(commandR1O) = *convertToR1OBase(static_cast<ConCommandBaseR1*>(command));
	commandR1O->vtable = GetR1OConCommandVTable();
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
	varR1O->vtable = GetR1OConVarVTable();
	varR1O->__vftable = GetR1OIConVarVTable();

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
	if (!pCommandBase || !pCommandBase->m_pszName)
		return;

	// TFO ships the complete fatal-script policy as native launcher ConVars,
	// but marks the controls development-only/hidden. Preserve the native
	// objects and callbacks; only expose the policy knobs on fake dedicated
	// servers before mirroring them into the R1 ICvar backing store.
	if (IsR1ODedicatedServer()
		&& ConCommandBaseR1OIsCVar(pCommandBase)
		&& IsR1OFatalScriptPolicyConVar(pCommandBase->m_pszName)) {
		ExposeR1OFatalScriptPolicyConVar(
			pCommandBase->m_pszName,
			reinterpret_cast<ConVarR1O*>(pCommandBase));
	}

	if (IsR1ODedicatedServer() && AreR1OFakeDediVerboseLogsEnabled() && IsR1ODediCommandRegistrationInteresting(pCommandBase->m_pszName)) {
		char buffer[384];
		void* callback = nullptr;
		int callbackFlags = 0;
		if (!ConCommandBaseR1OIsCVar(pCommandBase))
		{
			auto command = reinterpret_cast<ConCommandR1O*>(pCommandBase);
			callback = command->m_pCommandCallback;
			callbackFlags = command->m_nCallbackFlags;
		}
		_snprintf_s(
			buffer,
			sizeof(buffer),
			_TRUNCATE,
			"R1Delta: CCVar_RegisterConCommand this=%p command=%p name=%s isCVar=%d flags=0x%X callback=%p callbackFlags=0x%X backing=%p\n",
			reinterpret_cast<void*>(thisptr),
			pCommandBase,
			pCommandBase->m_pszName,
			static_cast<int>(ConCommandBaseR1OIsCVar(pCommandBase)),
			pCommandBase->m_nFlags,
			callback,
			callbackFlags,
			reinterpret_cast<void*>(cvarinterface));
		OutputDebugStringA(buffer);
	}

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

static ConCommandBaseR1O* WrapFoundR1CommandBase(const char* name, ConCommandBaseR1* r1CommandBase)
{
	if (!name || !r1CommandBase)
		return nullptr;

	auto existing = ccBaseMap.find(name);
	if (existing != ccBaseMap.end() && existing->second && existing->second->r1optr)
		return existing->second->r1optr;

	ConVarR1* asVar = OriginalCCVar_FindVar2 ? const_cast<ConVarR1*>(OriginalCCVar_FindVar2(cvarinterface, name)) : nullptr;
	ConCommandR1* asCommand = OriginalCCVar_FindCommand ? OriginalCCVar_FindCommand(cvarinterface, name) : nullptr;
	const bool isCvar = asVar && static_cast<ConCommandBaseR1*>(asVar) == r1CommandBase;

	ConCommandBaseR1O* wrapped = isCvar
		? static_cast<ConCommandBaseR1O*>(convertToR1O(asVar))
		: static_cast<ConCommandBaseR1O*>(convertToR1O(asCommand ? asCommand : reinterpret_cast<ConCommandR1*>(r1CommandBase)));
	if (!wrapped)
		return nullptr;

	ccBaseMap[name] = new WVar{ wrapped, r1CommandBase, isCvar, false };
	return wrapped;
}

static ConCommandBaseR1O* GetCommandReturnObject(WVar* entry)
{
	if (!entry || entry->is_cvar)
		return nullptr;

	// The R1O command buffer dispatch calls virtual methods on the object returned
	// by ICvar::FindCommandBase. In fake-dedi mode those commands are registered
	// into the backing R1 cvar system as converted ConCommandR1 objects; returning
	// the original static R1O ConCommand makes dispatch report success but does not
	// run command callbacks such as exec/stuffcmds/map. Return the real backing
	// command object for commands, while FindVar continues to return wrapped cvars.
	return reinterpret_cast<ConCommandBaseR1O*>(entry->r1ptr);
}

static ConCommandR1O* WrapFoundR1Command(const char* name, ConCommandR1* r1Command)
{
	ConCommandBaseR1O* wrapped = WrapFoundR1CommandBase(name, static_cast<ConCommandBaseR1*>(r1Command));
	auto it = name ? ccBaseMap.find(name) : ccBaseMap.end();
	if (!wrapped || it == ccBaseMap.end() || !it->second || it->second->is_cvar)
		return nullptr;

	return static_cast<ConCommandR1O*>(GetCommandReturnObject(it->second));
}

class R1OWrappedCVarIterator
{
public:
	R1OWrappedCVarIterator()
		: m_it(ccBaseMap.begin()), m_end(ccBaseMap.end())
	{
		AdvanceToValid();
	}

	virtual void SetFirst()
	{
		m_it = ccBaseMap.begin();
		m_end = ccBaseMap.end();
		AdvanceToValid();
	}

	virtual void Next()
	{
		if (m_it != m_end)
			++m_it;
		AdvanceToValid();
	}

	virtual bool IsValid()
	{
		return m_it != m_end;
	}

	virtual ConCommandBaseR1O* Get()
	{
		if (m_it == m_end || !m_it->second)
			return nullptr;

		return m_it->second->r1optr;
	}

private:
	void AdvanceToValid()
	{
		while (m_it != m_end) {
			WVar* entry = m_it->second;
			if (entry && entry->r1optr && entry->r1optr->m_pszName)
				break;
			++m_it;
		}
	}

	std::unordered_map<std::string, WVar*, HashStrings, std::equal_to<>>::iterator m_it;
	std::unordered_map<std::string, WVar*, HashStrings, std::equal_to<>>::iterator m_end;
};

void* CCvar__FactoryInternalIterator(uintptr_t thisptr)
{
	if (!IsR1ODedicatedServer() || !OriginalCCvar__FactoryInternalIterator)
		return OriginalCCvar__FactoryInternalIterator
			? OriginalCCvar__FactoryInternalIterator(cvarinterface)
			: nullptr;

	const uintptr_t engineBase = MainEngineBase();
	if (!engineBase)
		return nullptr;

	auto r1oMallocBase = reinterpret_cast<void* (*)(size_t)>(engineBase + 0x48509C);
	void* memory = r1oMallocBase(sizeof(R1OWrappedCVarIterator));
	if (!memory)
		return nullptr;

	return new (memory) R1OWrappedCVarIterator();
}

ConCommandBaseR1O* CCVar_FindCommandBase(uintptr_t thisptr, const char* name) {
	ZoneScoped;
	if (!IsR1ODedicatedServer())
		return (ConCommandBaseR1O*)OriginalCCVar_FindCommandBase(cvarinterface, name);
	if (!name)
		return nullptr;

	auto it = ccBaseMap.find(name);
	if (it != ccBaseMap.end() && it->second) {
		if (it->second->is_cvar && it->second->r1optr)
			return ExposeR1OFatalScriptPolicyConVar(
				name,
				reinterpret_cast<ConVarR1O*>(it->second->r1optr));
		if (!it->second->is_cvar && it->second->r1ptr)
			return GetCommandReturnObject(it->second);
	}

	ConCommandBaseR1* ret = OriginalCCVar_FindCommandBase ? OriginalCCVar_FindCommandBase(cvarinterface, name) : nullptr;
	ConCommandBaseR1O* wrapped = WrapFoundR1CommandBase(name, ret);
	it = ccBaseMap.find(name);
	return it != ccBaseMap.end() && it->second && !it->second->is_cvar
		? GetCommandReturnObject(it->second)
		: ExposeR1OFatalScriptPolicyConVar(name, reinterpret_cast<ConVarR1O*>(wrapped));
}

const ConCommandBaseR1O* CCVar_FindCommandBase2(uintptr_t thisptr, const char* name) {
	ZoneScoped;
	if (!IsR1ODedicatedServer())
		return (ConCommandBaseR1O*)OriginalCCVar_FindCommandBase2(cvarinterface, name);
	if (!name)
		return nullptr;

	auto it = ccBaseMap.find(name);
	if (it != ccBaseMap.end() && it->second) {
		if (it->second->is_cvar && it->second->r1optr)
			return ExposeR1OFatalScriptPolicyConVar(
				name,
				reinterpret_cast<ConVarR1O*>(it->second->r1optr));
		if (!it->second->is_cvar && it->second->r1ptr)
			return GetCommandReturnObject(it->second);
	}

	const ConCommandBaseR1* ret = OriginalCCVar_FindCommandBase2 ? OriginalCCVar_FindCommandBase2(cvarinterface, name) : nullptr;
	ConCommandBaseR1O* wrapped = WrapFoundR1CommandBase(name, const_cast<ConCommandBaseR1*>(ret));
	it = ccBaseMap.find(name);
	return it != ccBaseMap.end() && it->second && !it->second->is_cvar
		? GetCommandReturnObject(it->second)
		: ExposeR1OFatalScriptPolicyConVar(name, reinterpret_cast<ConVarR1O*>(wrapped));
}

ConVarR1O* CCVar_FindVar(uintptr_t thisptr, const char* var_name) {
	ZoneScoped;
	
	//std::cout << __FUNCTION__ << ": " << var_name << std::endl;
	auto it = ccBaseMap.find(var_name);
	ConVarR1O* r1optr = it == ccBaseMap.end() ? NULL : (ConVarR1O*)it->second->r1optr;
	if (r1optr)
		return ExposeR1OFatalScriptPolicyConVar(var_name, r1optr);
	auto ret = (ConVarR1*)(OriginalCCVar_FindVar2(cvarinterface, var_name));
	if (!ret) {
		DebugCVarFind(__FUNCTION__, var_name, nullptr);
		return nullptr;
	}
	ConVarR1O* wrapped = WrapFoundR1ConVar(var_name, ret);
	DebugCVarFind(__FUNCTION__, var_name, wrapped);
	return ExposeR1OFatalScriptPolicyConVar(var_name, wrapped);
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

	const float oldFloat = pOldValue ? static_cast<float>(atof(pOldValue)) : 0.0f;
	InvokeR1OGlobalChangeCallbacks(r1ovar, pOldValue, oldFloat);

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
	EnsureR1BackingGlobalChangeCallbackInstalled();
	auto it = ccBaseMap.find(var_name);
	ConVarR1O* r1optr = it == ccBaseMap.end() ? NULL : (ConVarR1O*)it->second->r1optr;
	if (r1optr)
		return ExposeR1OFatalScriptPolicyConVar(var_name, r1optr);
	auto ret = (ConVarR1*)(OriginalCCVar_FindVar2(cvarinterface, var_name));
	if (!ret) {
		DebugCVarFind(__FUNCTION__, var_name, nullptr);
		return nullptr;
	}
	ConVarR1O* wrapped = WrapFoundR1ConVar(var_name, ret);
	DebugCVarFind(__FUNCTION__, var_name, wrapped);
	return ExposeR1OFatalScriptPolicyConVar(var_name, wrapped);
}

ConCommandR1O* CCVar_FindCommand(uintptr_t thisptr, const char* name) {
	ZoneScoped;
	if (!IsR1ODedicatedServer())
		return (ConCommandR1O*)((uintptr_t)OriginalCCVar_FindCommand(cvarinterface, name));
	if (!name)
		return nullptr;

	auto it = ccBaseMap.find(name);
	if (it != ccBaseMap.end() && it->second && !it->second->is_cvar && it->second->r1ptr)
		return static_cast<ConCommandR1O*>(GetCommandReturnObject(it->second));

	ConCommandR1* ret = OriginalCCVar_FindCommand ? OriginalCCVar_FindCommand(cvarinterface, name) : nullptr;
	return WrapFoundR1Command(name, ret);
}

const ConCommandR1O* CCVar_FindCommand2(uintptr_t thisptr, const char* name) {
	ZoneScoped;
	if (!IsR1ODedicatedServer())
		return (ConCommandR1O*)((uintptr_t)OriginalCCVar_FindCommand2(cvarinterface, name));
	if (!name)
		return nullptr;

	auto it = ccBaseMap.find(name);
	if (it != ccBaseMap.end() && it->second && !it->second->is_cvar && it->second->r1ptr)
		return static_cast<ConCommandR1O*>(GetCommandReturnObject(it->second));

	const ConCommandR1* ret = OriginalCCVar_FindCommand2 ? OriginalCCVar_FindCommand2(cvarinterface, name) : nullptr;
	return WrapFoundR1Command(name, const_cast<ConCommandR1*>(ret));
}
void CCVar_CallGlobalChangeCallbacks(uintptr_t thisptr, ConVarR1O* var, const char* pOldString, float flOldValue) {
	// if this crashes YOU ARE NOT CALLING CVAR REGISTER FOR A DLL YOU SHOULD BE CALLING CVAR REGISTER FOR
	if (!var || !var->m_pszName)
		return;

	auto it = ccBaseMap.find(var->m_pszName);
	if (it == ccBaseMap.end() || !it->second || !it->second->r1ptr) {
		if (!OriginalCCVar_FindVar2 || !cvarinterface)
			return;

		ConVarR1* found = const_cast<ConVarR1*>(OriginalCCVar_FindVar2(cvarinterface, var->m_pszName));
		if (!found)
			return;

		it = ccBaseMap.emplace(var->m_pszName, new WVar{ var, found, true, false }).first;
	}

	ConVarR1* r1var = static_cast<ConVarR1*>(it->second->r1ptr);
	if (!r1var || !r1var->m_pParent)
		return;

	r1var->m_pParent->m_Value.m_fValue = var->m_Value.m_fValue;
	r1var->m_pParent->m_Value.m_nValue = var->m_Value.m_nValue;
	r1var->m_pParent->m_Value.m_pszString = var->m_Value.m_pszString;
	r1var->m_pParent->m_Value.m_StringLength = var->m_Value.m_StringLength;

	// The fake-dedi path shares cvars through the backing R1 store, but R1O
	// engine systems still own the replicated-cvar global callback. Notify
	// those callbacks with the R1O IConVar subobject after synchronizing the
	// backing value so runtime replicated changes enqueue normal net_SetConVar
	// updates.
	InvokeR1OGlobalChangeCallbacks(var, pOldString, flOldValue);
}

void CCVar_QueueMaterialThreadSetValue1(uintptr_t thisptr, ConVarR1O* pConVar, const char* pValue) {
	CvarDebugMessageBox(__FUNCTION__);
	if (!IsR1ODedicatedServer())
		return;
	ConVarR1* r1Var = GetR1ConVarForR1O(pConVar);
	if (!r1Var || !OriginalCCVar_QueueMaterialThreadSetValue1)
		return;

	OriginalCCVar_QueueMaterialThreadSetValue1(cvarinterface, r1Var, pValue);
}

void CCVar_QueueMaterialThreadSetValue2(uintptr_t thisptr, ConVarR1O* pConVar, int nValue) {
	CvarDebugMessageBox(__FUNCTION__);
	if (!IsR1ODedicatedServer())
		return;
	ConVarR1* r1Var = GetR1ConVarForR1O(pConVar);
	if (!r1Var || !OriginalCCVar_QueueMaterialThreadSetValue2)
		return;

	OriginalCCVar_QueueMaterialThreadSetValue2(cvarinterface, r1Var, nValue);
}

void CCVar_QueueMaterialThreadSetValue3(uintptr_t thisptr, ConVarR1O* pConVar, float flValue) {
	CvarDebugMessageBox(__FUNCTION__);
	if (!IsR1ODedicatedServer())
		return;
	ConVarR1* r1Var = GetR1ConVarForR1O(pConVar);
	if (!r1Var || !OriginalCCVar_QueueMaterialThreadSetValue3)
		return;

	OriginalCCVar_QueueMaterialThreadSetValue3(cvarinterface, r1Var, flValue);
}

void __fastcall CCvar__InstallGlobalChangeCallback(
	uintptr_t thisptr,
	void* func)
{
	CvarDebugMessageBox(__FUNCTION__);
	if (!IsR1ODedicatedServer() || !func)
		return;

	EnsureR1BackingGlobalChangeCallbackInstalled();
	auto callback = reinterpret_cast<FnChangeCallback_t>(func);
	if (std::find(s_R1OGlobalChangeCallbacks.begin(), s_R1OGlobalChangeCallbacks.end(), callback) == s_R1OGlobalChangeCallbacks.end())
		s_R1OGlobalChangeCallbacks.push_back(callback);
}

void __fastcall CCvar__RemoveGlobalChangeCallback(
	uintptr_t thisptr,
	void* func)
{
	CvarDebugMessageBox(__FUNCTION__);
	if (!IsR1ODedicatedServer())
		return;

	auto callback = reinterpret_cast<FnChangeCallback_t>(func);
	s_R1OGlobalChangeCallbacks.erase(
		std::remove(s_R1OGlobalChangeCallbacks.begin(), s_R1OGlobalChangeCallbacks.end(), callback),
		s_R1OGlobalChangeCallbacks.end());
}
int __fastcall CCvar__ProcessQueuedMaterialThreadConVarSets(uintptr_t thisptr)
{
	CvarDebugMessageBox(__FUNCTION__);
	if (!IsR1ODedicatedServer() || !OriginalCCvar__ProcessQueuedMaterialThreadConVarSets)
		return 0;

	return OriginalCCvar__ProcessQueuedMaterialThreadConVarSets(cvarinterface);
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

static void* AllocateR1OEngineOwnedMemory(size_t size)
{
	void* memory = hkmalloc_base(size);
	if (!memory)
		throw std::bad_alloc();
	return memory;
}

static char* DuplicateR1OEngineOwnedString(const char* value)
{
	const char* source = value ? value : "";
	const size_t size = strlen(source) + 1;
	char* copy = static_cast<char*>(AllocateR1OEngineOwnedMemory(size));
	memcpy(copy, source, size);
	return copy;
}

static ConVarR1O* CreateR1OConVarForWrappedRegistration(const char* name, const char* value, int flags, const char* helpString)
{
	ConVarR1O* newVar = reinterpret_cast<ConVarR1O*>(AllocateR1OEngineOwnedMemory(sizeof(ConVarR1O)));
	memset(newVar, 0, sizeof(ConVarR1O));
	newVar->vtable = GetR1OConVarVTable();
	newVar->m_pNext = nullptr;
	newVar->m_bRegistered = false;
	newVar->m_pszName = DuplicateR1OEngineOwnedString(name);
	newVar->m_pszHelpString = DuplicateR1OEngineOwnedString(helpString);
	newVar->m_nFlags = flags;
	newVar->__vftable = GetR1OIConVarVTable();
	newVar->m_pParent = newVar;
	newVar->m_pszDefaultValue = DuplicateR1OEngineOwnedString(value);
	newVar->m_Value.m_pszString = DuplicateR1OEngineOwnedString(newVar->m_pszDefaultValue);
	newVar->m_Value.m_StringLength = strlen(newVar->m_Value.m_pszString) + 1;
	newVar->m_Value.m_fValue = static_cast<float>(atof(newVar->m_Value.m_pszString));
	newVar->m_Value.m_nValue = atoi(newVar->m_Value.m_pszString);
	newVar->m_bHasMin = false;
	newVar->m_fMinVal = 0.0f;
	newVar->m_bHasMax = false;
	newVar->m_fMaxVal = 0.0f;
	return newVar;
}

static __int64 __fastcall R1ODediConCommandPostCallbackNoop(__int64, __int64)
{
	return 0;
}

static void R1ODediDispatcherOwnedCommand(const CCommand&)
{
	Warning("R1Delta: R1O dedicated command dispatcher is unavailable.\n");
}

static ConCommandR1O* CreateR1OConCommandForWrappedRegistration(const char* name, void (*callback)(const CCommand&), int flags, const char* helpString)
{
	ConCommandR1O* newCommand = reinterpret_cast<ConCommandR1O*>(AllocateR1OEngineOwnedMemory(sizeof(ConCommandR1O)));
	memset(newCommand, 0, sizeof(ConCommandR1O));
	newCommand->vtable = GetR1OConCommandVTable();
	newCommand->m_pNext = nullptr;
	newCommand->m_bRegistered = false;
	newCommand->m_pszName = DuplicateR1OEngineOwnedString(name);
	newCommand->m_pszHelpString = DuplicateR1OEngineOwnedString(helpString);
	newCommand->m_nFlags = flags;
	newCommand->unused = reinterpret_cast<void*>(&R1ODediConCommandPostCallbackNoop);
	newCommand->unused2 = nullptr;
	newCommand->m_pCommandCallback = reinterpret_cast<FnCommandCallback_t>(callback);
	newCommand->m_pCompletionCallback = nullptr;
	newCommand->m_nCallbackFlags = 0x02;
	return newCommand;
}

ConCommandR1O* RegisterR1ODediConCommand(const char* name, void (*callback)(const CCommand&), const char* helpString, int flags)
{
	if (!IsR1ODedicatedServer() || !cvarinterface || !OriginalCCVar_RegisterConCommand || !name || !callback)
		return nullptr;

	if ((OriginalCCVar_FindCommand && OriginalCCVar_FindCommand(cvarinterface, name)) || ccBaseMap.find(name) != ccBaseMap.end())
		return CCVar_FindCommand(cvarinterface, name);

	ConCommandR1O* newCommand = CreateR1OConCommandForWrappedRegistration(name, callback, flags, helpString);
	CCVar_RegisterConCommand(cvarinterface, newCommand);
	return newCommand;
}

ConVarR1O* RegisterR1ODediConVar(const char* name, const char* value, int flags, const char* helpString)
{
	if (!IsR1ODedicatedServer() || !cvarinterface || !OriginalCCVar_RegisterConCommand || !OriginalCCVar_FindVar2 || !name)
		return nullptr;
	if (OriginalCCVar_FindVar2(cvarinterface, name) || ccBaseMap.find(name) != ccBaseMap.end())
		return CCVar_FindVar(cvarinterface, name);

	ConVarR1O* newVar = CreateR1OConVarForWrappedRegistration(name, value, flags, helpString);
	CCVar_RegisterConCommand(cvarinterface, newVar);
	return newVar;
}

void RegisterR1ODediDeltaConVars()
{
	if (!IsR1ODedicatedServer())
		return;

	EnsureR1BackingGlobalChangeCallbackInstalled();

	static bool s_registered = false;
	if (s_registered)
		return;

	RegisterR1ODediConVar("delta_ms_url", "ms.r1delta.net", FCVAR_CLIENTDLL, "Url for r1d masterserver");
	RegisterR1ODediConVar("delta_server_auth_token", "", FCVAR_USERINFO | FCVAR_SERVER_CANNOT_QUERY | FCVAR_DONTRECORD | FCVAR_PROTECTED | FCVAR_HIDDEN, "Per-server auth token");
	RegisterR1ODediConVar("delta_version", R1D_VERSION, FCVAR_USERINFO | FCVAR_DONTRECORD, "R1Delta version number");
	RegisterR1ODediConVar("delta_skip_version_check", "0", FCVAR_GAMEDLL, "Skip version check for connecting clients (sets server to dev mode)");
	RegisterR1ODediConVar("delta_online_auth_enable", "0", FCVAR_GAMEDLL, "Whether to use master server auth");
	RegisterR1ODediConVar("delta_discord_username_sync", "0", FCVAR_GAMEDLL, "Controls if player names are synced with Discord");
	RegisterR1ODediConVar("riff_floorislava", "0", FCVAR_HIDDEN, "Enable floor is lava mode");
	RegisterR1ODediConVar("hide_server", "0", FCVAR_NONE, "Whether the server should be hidden from the master server");
	RegisterR1ODediConVar("server_description", "", FCVAR_NONE, "Server description");
	RegisterR1ODediConVar("delta_ui_server_filter", "0", FCVAR_NONE, "Script managed vgui filter convar");
	RegisterR1ODediConVar("delta_autoBalanceTeams", "1", FCVAR_NONE, "Whether to autobalance teams on death/private match/lobby start. Managed by script");
	RegisterR1ODediConVar("delta_useLegacyProgressBar", "0", FCVAR_ARCHIVE, "Whether or not to use the old loading bar");
	RegisterR1ODediConVar("delta_return_to_lobby", "1", FCVAR_NONE, "Return to lobby after a game");
	RegisterR1ODediConVar("delta_allow_empty_server", "1", FCVAR_NONE, "Allow matches with no players.");
	RegisterR1ODediConVar("delta_skip_waiting_for_players", "0", FCVAR_NONE, "Skip waiting for players.");
	RegisterR1ODediConVar("sv_banlist_autosave", "1", FCVAR_ARCHIVE, "Automatically save ban lists after modification commands.");
	RegisterR1ODediConVar("bot_kick_on_death", "1", FCVAR_GAMEDLL | FCVAR_CHEAT, "Enable/disable bots getting kicked on death.");
	RegisterR1ODediConCommand("bot_dummy", AddBotDummyConCommand, "Adds a bot.", FCVAR_GAMEDLL | FCVAR_CHEAT);
	RegisterR1ODediConCommand("script", script_cmd, "Execute Squirrel code in server context", FCVAR_GAMEDLL | FCVAR_CHEAT);
	RegisterR1ODediConCommand("find", R1ODediDispatcherOwnedCommand, "Find a command or convar.", FCVAR_NONE);
	RegisterR1ODediConCommand("removeallids", R1ODediDispatcherOwnedCommand, "Remove all user IDs from the ban list.", FCVAR_NONE);
	RegisterR1ODediConCommand("removeallips", R1ODediDispatcherOwnedCommand, "Remove all IPs from the ban list.", FCVAR_NONE);
	RegisterR1ODediConVar("delta_vote_allowed", "1", FCVAR_GAMEDLL | FCVAR_REPLICATED, "Allow voting?");
	RegisterR1ODediConVar("delta_vote_timer_duration", "12.0", FCVAR_GAMEDLL | FCVAR_REPLICATED, "How long to allow voting on an issue");
	RegisterR1ODediConVar("delta_vote_failure_timer", "300.0", FCVAR_GAMEDLL | FCVAR_REPLICATED, "Vote failure cooldown");
	RegisterR1ODediConVar("delta_vote_creation_timer", "150.0", FCVAR_GAMEDLL | FCVAR_REPLICATED, "Vote creation cooldown");
	RegisterR1ODediConVar("delta_vote_holder_may_vote_no", "1", FCVAR_GAMEDLL | FCVAR_REPLICATED, "Vote holder may vote no");
	RegisterR1ODediConVar("delta_vote_next_map", "", FCVAR_GAMEDLL | FCVAR_REPLICATED, "Next voted map.");
	RegisterR1ODediConVar("delta_vote_next_mode", "", FCVAR_GAMEDLL | FCVAR_REPLICATED, "Next voted gamemode.");
	RegisterServerUserCmdConVars();

	s_registered = true;
	if (AreR1OFakeDediVerboseLogsEnabled())
		OutputDebugStringA("R1Delta: registered R1O fake-dedi Delta script ConVars\n");
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

bool PrintR1ODediFindResults(const char* search)
{
	if (!IsR1ODedicatedServer() || !search || !search[0] || !cvarinterface)
		return false;

	std::vector<ConCommandBaseR1*> matches;
	typedef ICVarIteratorInternal* (__thiscall *FactoryInternalIterator_t)(void* cvar);
	uintptr_t* vtable = *reinterpret_cast<uintptr_t**>(cvarinterface);
	auto factoryInternalIterator =
		vtable ? reinterpret_cast<FactoryInternalIterator_t>(vtable[40]) : nullptr;
	ICVarIteratorInternal* it = factoryInternalIterator
		? factoryInternalIterator(reinterpret_cast<void*>(cvarinterface))
		: nullptr;
	if (!it)
		return false;

	for (it->SetFirst(); it->IsValid(); it->Next())
	{
		ConCommandBaseR1* var = it->Get();
		if (!var || !var->m_pszName || !var->m_pszHelpString)
			continue;
		if (!V_stristr(var->m_pszName, search) && !V_stristr(var->m_pszHelpString, search))
			continue;
		matches.push_back(var);
	}

	std::sort(matches.begin(), matches.end(), ConVarSortFunc);
	for (const auto* var : matches) {
		ConCommandBaseR1O* r1oVar = CCVar_FindCommandBase(cvarinterface, var->m_pszName);
		if (r1oVar)
			ConVar_PrintDescription(reinterpret_cast<const ConCommandBaseR1*>(r1oVar));
	}
	return true;
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
