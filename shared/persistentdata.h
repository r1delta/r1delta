#pragma once
#include "bitbuf.h"
#include "vsdk/public/tier1/utlvector.h"
#include "vsdk/public/tier1/utlmemory.h"
#include "cvar.h"
#include "squirrel.h"
#include "netchanwarnings.h"
#include "persistentdata_state.h"
#include <string>
#include <vector>
struct NetMessageCvar_t // sizeof=0x208
{
	char name[260];
    char value[260];
};
struct alignas(8) NET_SetConVar : INetMessage
{
	bool m_bReliable;
	void* m_NetChannel;
	void* m_pMessageHandler;
	CUtlVector<NetMessageCvar_t, CUtlMemory<NetMessageCvar_t>> m_ConVars;
};
//static_assert(sizeof(whatever) == sizeof(CUtlVector<NetMessageCvar_t, CUtlMemory<NetMessageCvar_t>>));
//static_assert(offsetof(whatever, m_ConVars) == 32);
//static_assert(offsetof(whatever, m_ConVars_count) == 56);
//static_assert(offsetof(NET_SetConVar, m_ConVars) == 32);
//static_assert(offsetof(NET_SetConVar, m_pMessageHandler) == 24);
//static_assert((offsetof(NET_SetConVar, m_ConVars) + offsetof(CUtlVector<NetMessageCvar_t>, m_Size)) == 56);

typedef bool (*NET_SetConVar__ReadFromBufferType)(NET_SetConVar* thisptr, bf_read& buffer);
extern NET_SetConVar__ReadFromBufferType NET_SetConVar__ReadFromBufferOriginal;

bool __fastcall NET_SetConVar__ReadFromBuffer(NET_SetConVar* thisptr, bf_read& buffer);
bool __fastcall NET_SetConVar__WriteToBuffer(NET_SetConVar* thisptr, bf_write& buffer);
unsigned long long GenerateSyntheticPlatformUserId();
bool SafePrefixConVarName(char* name, size_t nameBufferSize, const char* prefix);
bool IsPackedPDataWireName(const char* name);
bool DecodePackedPDataWire(const std::string& encoded, std::vector<NetMessageCvar_t>& output);
bool IsPDataFullSnapshotMarker(const char* name);
__int64 CConVar__GetSplitScreenPlayerSlot(char* thisptr);
void setinfopersist_cmd(const CCommand& args);

// Server: reconciles the persistent values a client just reported against the
// server's unacknowledged writes (rewrites stale values in `staged`, appends
// missing ones for full snapshots, resends lost writes). `session` may be null
// to resolve it from the engine's client table.
bool PData_ServerReconcileIncoming(
	int playerSlot,
	const PersistentDataState::SessionKey* session,
	std::vector<NetMessageCvar_t>& staged,
	bool fullSnapshot);
void PData_ServerRunFrame();
void R1OClearPersistentUserDataForPlayer(int playerSlot);
bool R1OGetPersistentUserDataConVar(int playerSlot, const char* name, std::string& value);
// Implemented in factory.cpp.
bool R1OResolvePersistenceSessionForSlot(int playerSlot, PersistentDataState::SessionKey& session);

bool IsValidUserInfo(const char* value, int length = -1);
struct CBaseClient;
extern CBaseClient* g_pClientArray;
struct CBaseClientDS;
extern CBaseClientDS* g_pClientArrayDS;
extern void Script_XPChanged_Rebuild(void* pPlayer);
extern void Script_GenChanged_Rebuild(void* pPlayer);
bool R1OMarkTFOPlayerNetworkStateChanged(void* pPlayer);

extern SQInteger Script_ClientGetPersistentData(HSQUIRRELVM v, __int64 a2, __int64 a3);

SQInteger Script_ClientGetPersistentData(HSQUIRRELVM v);
SQInteger Script_ClientGetPersistentDataAsInt(HSQUIRRELVM v);
SQInteger Script_ServerGetPersistentUserDataKVString(HSQUIRRELVM v);
SQInteger Script_ServerSetPersistentUserDataKVString(HSQUIRRELVM v);
typedef char (*CBaseClientState__InternalProcessStringCmdType)(void* thisptr, void* msg, bool bIsHLTV);
extern CBaseClientState__InternalProcessStringCmdType CBaseClientState__InternalProcessStringCmdOriginal;
char CBaseClientState__InternalProcessStringCmd(void* thisptr, void* msg, bool bIsHLTV);
char ExecuteConfigFile(int configType);
// Client: persistent data store lifecycle.
void PData_OnConsoleCommand(const char* str);
void PData_RunFrame();
void PData_Flush(bool quiet = false);
void PData_OnSchemaReloaded();
void InstallPersistentProfileWriterHook(uintptr_t engineBase);
class PDataValidator;
class PDef {
private:
	static std::unique_ptr<PDataValidator> s_validator;
	static std::once_flag s_initFlag;
public:
	static void InitValidator();
	static bool IsValidKeyAndValue(const std::string& key, const std::string& value);
	static bool ValidateKeyIndices(const std::string_view& key);
};
