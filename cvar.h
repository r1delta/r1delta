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

#pragma once
#include <unordered_map>
#include "core.h"
#include <string>
#include "color.h"

//-----------------------------------------------------------------------------
// ConVar flags
//-----------------------------------------------------------------------------
// The default, no flags at all
#define FCVAR_NONE				0 

// Command to ConVars and ConCommands
// ConVar Systems
#define FCVAR_UNREGISTERED		(1<<0)	// If this is set, don't add to linked list, etc.
#define FCVAR_DEVELOPMENTONLY	(1<<1)	// Hidden in released products. Flag is removed automatically if ALLOW_DEVELOPMENT_CVARS is defined.
#define FCVAR_GAMEDLL			(1<<2)	// defined by the game DLL
#define FCVAR_CLIENTDLL			(1<<3)  // defined by the client DLL
#define FCVAR_HIDDEN			(1<<4)	// Hidden. Doesn't appear in find or auto complete. Like DEVELOPMENTONLY, but can't be compiled out.

// ConVar only
#define FCVAR_PROTECTED			(1<<5)  // It's a server cvar, but we don't send the data since it's a password, etc.  Sends 1 if it's not bland/zero, 0 otherwise as value
#define FCVAR_SPONLY			(1<<6)  // This cvar cannot be changed by clients connected to a multiplayer server.
#define	FCVAR_ARCHIVE			(1<<7)	// set to cause it to be saved to vars.rc
#define	FCVAR_NOTIFY			(1<<8)	// notifies players when changed
#define	FCVAR_USERINFO			(1<<9)	// changes the client's info string

#define FCVAR_PRINTABLEONLY		(1<<10)  // This cvar's string cannot contain unprintable characters ( e.g., used for player name etc ).

#define FCVAR_GAMEDLL_FOR_REMOTE_CLIENTS		(1<<10)  // When on concommands this allows remote clients to execute this cmd on the server. 
														 // We are changing the default behavior of concommands to disallow execution by remote clients without
														 // this flag due to the number existing concommands that can lag or crash the server when clients abuse them.

#define FCVAR_UNLOGGED			(1<<11)  // If this is a FCVAR_SERVER, don't log changes to the log file / console if we are creating a log
#define FCVAR_NEVER_AS_STRING	(1<<12)  // never try to print that cvar

// It's a ConVar that's shared between the client and the server.
// At signon, the values of all such ConVars are sent from the server to the client (skipped for local
//  client, of course )
// If a change is requested it must come from the console (i.e., no remote client changes)
// If a value is changed while a server is active, it's replicated to all connected clients
#define FCVAR_REPLICATED		(1<<13)	// server setting enforced on clients, TODO rename to FCAR_SERVER at some time
#define FCVAR_CHEAT				(1<<14) // Only useable in singleplayer / debug / multiplayer & sv_cheats
#define FCVAR_SS				(1<<15) // causes varnameN where N == 2 through max splitscreen slots for mod to be autogenerated
#define FCVAR_DEMO				(1<<16) // record this cvar when starting a demo file
#define FCVAR_DONTRECORD		(1<<17) // don't record these command in demofiles
#define FCVAR_SS_ADDED			(1<<18) // This is one of the "added" FCVAR_SS variables for the splitscreen players
#define FCVAR_RELEASE			(1<<19) // Cvars tagged with this are the only cvars avaliable to customers
#define FCVAR_RELOAD_MATERIALS	(1<<20)	// If this cvar changes, it forces a material reload
#define FCVAR_RELOAD_TEXTURES	(1<<21)	// If this cvar changes, if forces a texture reload

#define FCVAR_NOT_CONNECTED		(1<<22)	// cvar cannot be changed by a client that is connected to a server
#define FCVAR_MATERIAL_SYSTEM_THREAD (1<<23)	// Indicates this cvar is read from the material system thread
#define FCVAR_ARCHIVE_GAMECONSOLE	(1<<24) // cvar written to config.cfg on the Xbox

#define FCVAR_SERVER_CAN_EXECUTE	(1<<28)// the server is allowed to execute this command on clients via ClientCommand/NET_StringCmd/CBaseClientState::ProcessStringCmd.
#define FCVAR_SERVER_CANNOT_QUERY	(1<<29)// If this is set, then the server is not allowed to query this cvar's value (via IServerPluginHelpers::StartQueryCvarValue).
#define FCVAR_CLIENTCMD_CAN_EXECUTE	(1<<30)	// IVEngineClient::ClientCmd is allowed to execute this command. 
											// Note: IVEngineClient::ClientCmd_Unrestricted can run any client command.

#define FCVAR_ACCESSIBLE_FROM_THREADS	(1<<25)	// used as a debugging tool necessary to check material system thread convars
// #define FCVAR_AVAILABLE			(1<<26)
// #define FCVAR_AVAILABLE			(1<<27)
// #define FCVAR_AVAILABLE			(1<<31)

#define FCVAR_MATERIAL_THREAD_MASK ( FCVAR_RELOAD_MATERIALS | FCVAR_RELOAD_TEXTURES | FCVAR_MATERIAL_SYSTEM_THREAD )	

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

class EditablePanel
{
public:
	virtual ~EditablePanel() = 0;
	unsigned char unknown[0x2A8];
};


class IConsoleDisplayFunc
{
public:
	virtual void ColorPrint(const SourceColor& clr, const char* pMessage) = 0;
	virtual void Print(const char* pMessage) = 0;
	virtual void DPrint(const char* pMessage) = 0;
};

class CConsolePanel : public EditablePanel, public IConsoleDisplayFunc
{

};
class CConsoleDialog
{
public:
	struct VTable
	{
		//void* unknown[298];
		void* unknown[294];
		void(*OnCommandSubmitted)(CConsoleDialog* consoleDialog, const char* pCommand);
	};

	VTable* m_vtable;
	//unsigned char unknown[0x398];
	unsigned char unknown[0x390];
	CConsolePanel* m_pConsolePanel;
};
class CGameConsole
{
public:
	virtual ~CGameConsole() = 0;

	// activates the console, makes it visible and brings it to the foreground
	virtual void Activate() = 0;

	virtual void Initialize() = 0;

	// hides the console
	virtual void Hide() = 0;

	// clears the console
	virtual void Clear() = 0;

	// return true if the console has focus
	virtual bool IsConsoleVisible() = 0;

	virtual void SetParent(int parent) = 0;

	bool m_bInitialized;
	CConsoleDialog* m_pConsole;
};

class IConCommandBaseAccessor
{
public:
	// Flags is a combination of FCVAR flags in cvar.h.
	// hOut is filled in with a handle to the variable.
	virtual bool RegisterConCommandBase(ConCommandBaseR1* pVar) = 0;
};

class CCommand
{
public:
	CCommand() = delete;

	int64_t ArgC() const;
	const char** ArgV() const;
	const char* ArgS() const; // All args that occur after the 0th arg, in string form
	const char* GetCommandString() const; // The entire command in string form, including the 0th arg
	const char* operator[](int nIndex) const; // Gets at arguments
	const char* Arg(int nIndex) const; // Gets at arguments

	static int MaxCommandLength();

private:
	enum
	{
		COMMAND_MAX_ARGC = 64,
		COMMAND_MAX_LENGTH = 512,
	};

	int64_t m_nArgc;
	int64_t m_nArgv0Size;
	char m_pArgSBuffer[COMMAND_MAX_LENGTH];
	char m_pArgvBuffer[COMMAND_MAX_LENGTH];
	const char* m_ppArgv[COMMAND_MAX_ARGC];
};

inline int CCommand::MaxCommandLength()
{
	return COMMAND_MAX_LENGTH - 1;
}
inline int64_t CCommand::ArgC() const
{
	return m_nArgc;
}
inline const char** CCommand::ArgV() const
{
	return m_nArgc ? (const char**)m_ppArgv : NULL;
}
inline const char* CCommand::ArgS() const
{
	return m_nArgv0Size ? &m_pArgSBuffer[m_nArgv0Size] : "";
}
inline const char* CCommand::GetCommandString() const
{
	return m_nArgc ? m_pArgSBuffer : "";
}
inline const char* CCommand::Arg(int nIndex) const
{
	// FIXME: Many command handlers appear to not be particularly careful
	// about checking for valid argc range. For now, we're going to
	// do the extra check and return an empty string if it's out of range
	if (nIndex < 0 || nIndex >= m_nArgc)
		return "";
	return m_ppArgv[nIndex];
}
inline const char* CCommand::operator[](int nIndex) const
{
	return Arg(nIndex);
}
void ToggleConsoleCommand(const CCommand& args);
extern CGameConsole** staticGameConsole;
typedef char (*CEngineVGui__HideGameUIType)(__int64 a1);
extern CEngineVGui__HideGameUIType CEngineVGui__HideGameUIOriginal;
char __fastcall CEngineVGui__HideGameUI(__int64 a1);
void Con_ColorPrintf(const SourceColor* clr, char* fmt, ...);
typedef char (*CEngineVGui__InitType)(__int64 a1);
extern CEngineVGui__InitType CEngineVGui__InitOriginal;
char CEngineVGui__Init(__int64 a1);
