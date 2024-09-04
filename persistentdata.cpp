#include <string>
#include <vector>
#include <cstring>
#include <algorithm>
#include "bitbuf.h"
#include "thirdparty/silver-bun/silver-bun.h"
#include "cvar.h"
#include "persistentdata.h"
#include "logging.h"
#include "squirrel.h"
#include "keyvalues.h"
#include "factory.h"

// Constants
constexpr size_t MAX_LENGTH = 254;
constexpr const char* INVALID_CHARS = "{}()':;\"\n";

// Utility functions
bool IsValidUserInfo(const char* value) {
    if (!value) return false;

    return std::strlen(value) <= MAX_LENGTH &&
        std::none_of(value, value + std::strlen(value), [](char c) {
        return std::strchr(INVALID_CHARS, c) != nullptr;
            });
}

// Command handling
void setinfopersist_cmd(const CCommand& args) {
    static CModule engine("engine.dll");
    static auto setinfo_cmd = CMemory(engine.GetModuleBase()).OffsetSelf(0x5B520).RCast<decltype(&setinfopersist_cmd)>();
    static auto setinfo_cmd_flags = CMemory(engine.GetModuleBase()).OffsetSelf(0x05B5FF).RCast<int*>();
    static auto ccommand_constructor = CMemory(engine.GetModuleBase()).OffsetSelf(0x4806F0).RCast<void(*)(CCommand * thisptr, int nArgC, const char** ppArgV)>();

    static bool bUnprotectedFlags = false;
    if (!bUnprotectedFlags) {
        bUnprotectedFlags = true;
        DWORD out;
        VirtualProtect(setinfo_cmd_flags, sizeof(int), PAGE_EXECUTE_READWRITE, &out);
    }

    *setinfo_cmd_flags = FCVAR_PERSIST_MASK;

    if (args.ArgC() >= 3) {
        if (!IsValidUserInfo(args.Arg(1))) {
            Warning("Invalid user info key %s. Only certain characters are allowed.\n", args.Arg(1));
            return;
        }

        std::vector<const char*> newArgv(args.ArgC());
        newArgv[0] = args.Arg(0);

        char modifiedKey[CCommand::COMMAND_MAX_LENGTH];
        snprintf(modifiedKey, sizeof(modifiedKey), "%s %s", PERSIST_COMMAND, args.Arg(1));
        newArgv[1] = modifiedKey;

        std::copy(args.ArgV() + 2, args.ArgV() + args.ArgC(), newArgv.begin() + 2);

        // Allocate memory for CCommand on the stack
        char commandMemory[sizeof(CCommand)];
        CCommand* pCommand = reinterpret_cast<CCommand*>(commandMemory);

        // Construct the CCommand object using placement new
        ccommand_constructor(pCommand, args.ArgC(), newArgv.data());

        // Use the constructed CCommand object
        setinfo_cmd(*pCommand);

        // Manually call the destructor
        pCommand->~CCommand();
    }
    else if (args.ArgC() == 2) {
        auto result = OriginalCCVar_FindVar(cvarinterface, args.GetCommandString());
        if (result)
            ConVar_PrintDescription(result);
    }
    else {
        setinfo_cmd(args);
    }

    *setinfo_cmd_flags = FCVAR_USERINFO;
}

// ConVar handling
__int64 CConVar__GetSplitScreenPlayerSlot(char* fakethisptr) {
    ConVarR1* thisptr = reinterpret_cast<ConVarR1*>(fakethisptr - 48);
    return (thisptr->m_nFlags & FCVAR_PERSIST) ? -1 : 0;
}

// Network message handling
bool NET_SetConVar__ReadFromBuffer(NET_SetConVar* thisptr, bf_read& buffer) {
    uint32_t numvars;
    uint8_t byteCount = buffer.ReadByte();

    if (byteCount == static_cast<uint8_t>(-1)) {
        numvars = buffer.ReadUBitVar();
    }
    else {
        numvars = byteCount;
    }

    thisptr->m_ConVars.RemoveAll();
    for (uint32_t i = 0; i < numvars; i++) {
        NetMessageCvar_t var;
        buffer.ReadString(var.name, sizeof(var.name));
        buffer.ReadString(var.value, sizeof(var.value));
        thisptr->m_ConVars.AddToTail(var);
    }
    return !buffer.IsOverflowed();
}

bool NET_SetConVar__WriteToBuffer(NET_SetConVar* thisptr, bf_write& buffer) {
    uint32_t numvars = thisptr->m_ConVars.Count();

    if (numvars < 255) {
        buffer.WriteByte(numvars);
    }
    else {
        buffer.WriteByte(static_cast<uint8_t>(-1));
        buffer.WriteUBitVar(numvars);
    }

    for (uint32_t i = 0; i < numvars; i++) {
        NetMessageCvar_t* var = &thisptr->m_ConVars[i];
        buffer.WriteString(var->name);
        buffer.WriteString(var->value);
    }
    return !buffer.IsOverflowed();
}

// Squirrel VM functions
SQInteger Script_ClientGetPersistentData(HSQUIRRELVM v) {
    if (sq_gettop(nullptr, v) != 2) {
        return sq_throwerror(v, "Expected 1 parameter");
    }

    const SQChar* str;
    if (SQ_FAILED(sq_getstring(v, 2, &str))) {
        return sq_throwerror(v, "Parameter 1 must be a string");
    }

    std::string varName = std::string(PERSIST_COMMAND) + " " + str;
    auto var = OriginalCCVar_FindVar(cvarinterface, varName.c_str());

    if (!var) {
        Warning("Couldn't find persistent variable %s; defaulting to empty\n", str);
        sq_pushstring(v, "", 0);
    }
    else {
        sq_pushstring(v, var->m_Value.m_pszString, var->m_Value.m_StringLength);
    }

    return 1;
}
struct CBaseClient
{
    _BYTE gap0[1040];
    KeyValues* m_ConVars;
};
CBaseClient* g_pClientArray;

SQInteger Script_ServerGetUserInfoKVString(HSQUIRRELVM v) {
    const void* pPlayer = sq_getentity(v, 2);
    if (!pPlayer) {
        return sq_throwerror(v, "player is null");
    }

    const char* pKey, * pDefaultValue;
    sq_getstring(v, 3, &pKey);
    sq_getstring(v, 4, &pDefaultValue);

    if (!IsValidUserInfo(pKey) || !IsValidUserInfo(pDefaultValue)) {
        return sq_throwerror(v, "Invalid user info key or default value.");
    }

    auto v2 = *reinterpret_cast<__int64*>(reinterpret_cast<__int64>(pPlayer) + 64);
    auto index = ((v2 - reinterpret_cast<__int64>(pGlobalVarsServer->pEdicts)) / 56) - 1;

    if (!g_pClientArray[index].m_ConVars) {
        return sq_throwerror(v, "Client has NULL m_ConVars.");
    }

    const char* pResult = g_pClientArray[index].m_ConVars->GetString(pKey, pDefaultValue);
    sq_pushstring(v, pResult, -1);
    return 1;
}

SQInteger Script_ServerSetUserInfoKVString(HSQUIRRELVM v) {
    const void* pPlayer = sq_getentity(v, 2);
    if (!pPlayer) {
        return sq_throwerror(v, "player is null");
    }

    const char* pKey, * pValue;
    sq_getstring(v, 3, &pKey);
    sq_getstring(v, 4, &pValue);

    if (!IsValidUserInfo(pKey) || !IsValidUserInfo(pValue)) {
        return sq_throwerror(v, "Invalid user info key or value.");
    }

    auto v2 = *reinterpret_cast<__int64*>(reinterpret_cast<__int64>(pPlayer) + 64);
    auto index = ((v2 - reinterpret_cast<__int64>(pGlobalVarsServer->pEdicts)) / 56) - 1;

    if (!g_pClientArray[index].m_ConVars) {
        return sq_throwerror(v, "Client has NULL m_ConVars.");
    }

    g_pClientArray[index].m_ConVars->SetString(pKey, pValue);
    sq_pushstring(v, pValue, -1);
    return 1;
}