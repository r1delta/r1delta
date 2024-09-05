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
constexpr const char* INVALID_CHARS = "{}()':;`\"\n";
bool g_bNoSendConVar = false;

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
        if (!IsValidUserInfo(args.Arg(2))) {
            Warning("Invalid user info value %s. Only certain characters are allowed.\n", args.Arg(1));
            return;
        }

        // Check for "nosend" argument
        bool noSend = (args.ArgC() >= 4 && strcmp(args.Arg(3), "nosend") == 0);

        std::vector<const char*> newArgv(noSend ? args.ArgC() - 1 : args.ArgC());
        newArgv[0] = args.Arg(0);

        char modifiedKey[CCommand::COMMAND_MAX_LENGTH];
        snprintf(modifiedKey, sizeof(modifiedKey), "%s %s", PERSIST_COMMAND, args.Arg(1));
        newArgv[1] = modifiedKey;

        // Copy arguments, skipping "nosend" if present
        std::copy(args.ArgV() + 2, args.ArgV() + (noSend ? args.ArgC() - 1 : args.ArgC()), newArgv.begin() + 2);

        // Allocate memory for CCommand on the stack
        char commandMemory[sizeof(CCommand)];
        CCommand* pCommand = reinterpret_cast<CCommand*>(commandMemory);

        // Construct the CCommand object using placement new
        ccommand_constructor(pCommand, noSend ? args.ArgC() - 1 : args.ArgC(), newArgv.data());

        // Set the global variable
        g_bNoSendConVar = noSend;

        // Use the constructed CCommand object
        setinfo_cmd(*pCommand);

        // Reset the global variable
        g_bNoSendConVar = false;

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
    if (g_bNoSendConVar) {
        // Write 0 ConVars
        buffer.WriteByte(0);
        return !buffer.IsOverflowed();
    }

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

SQInteger Script_ServerGetPersistentUserDataKVString(HSQUIRRELVM v) {
    const void* pPlayer = sq_getentity(v, 2);
    if (!pPlayer) {
        return sq_throwerror(v, "player is null");
    }

    const char* pKey, * pDefaultValue;
    sq_getstring(v, 3, &pKey);
    sq_getstring(v, 4, &pDefaultValue);
    std::string modifiedKey = PERSIST_COMMAND" ";
    modifiedKey += pKey;


    if (!IsValidUserInfo(modifiedKey.c_str()) || !IsValidUserInfo(pDefaultValue)) {
        return sq_throwerror(v, "Invalid user info key or default value.");
    }

    auto edict = *reinterpret_cast<__int64*>(reinterpret_cast<__int64>(pPlayer) + 64);
    auto index = ((edict - reinterpret_cast<__int64>(pGlobalVarsServer->pEdicts)) / 56) - 1;

    if (!g_pClientArray[index].m_ConVars) {
        return sq_throwerror(v, "Client has NULL m_ConVars.");
    }

    const char* pResult = g_pClientArray[index].m_ConVars->GetString(modifiedKey.c_str(), pDefaultValue);
    sq_pushstring(v, pResult, -1);
    return 1;
}

SQInteger Script_ServerSetPersistentUserDataKVString(HSQUIRRELVM v) {
    static auto CVEngineServer_ClientCommand = CMemory(CModule("engine.dll").GetModuleBase()).OffsetSelf(0xFE7F0).RCast<void (*)(__int64 a1, __int64 a2, const char* a3, ...)>();
    const void* pPlayer = sq_getentity(v, 2);
    if (!pPlayer) {
        return sq_throwerror(v, "player is null");
    }

    const char* pKey, * pValue;
    sq_getstring(v, 3, &pKey);
    sq_getstring(v, 4, &pValue);
    std::string modifiedKey = PERSIST_COMMAND" ";
    modifiedKey += pKey;

    if (!IsValidUserInfo(modifiedKey.c_str()) || !IsValidUserInfo(pValue)) {
        return sq_throwerror(v, "Invalid user info key or value.");
    }

    auto edict = *reinterpret_cast<__int64*>(reinterpret_cast<__int64>(pPlayer) + 64);

    auto index = ((edict - reinterpret_cast<__int64>(pGlobalVarsServer->pEdicts)) / 56) - 1;

    if (!g_pClientArray[index].m_ConVars) {
        return sq_throwerror(v, "Client has NULL m_ConVars.");
    }
    CVEngineServer_ClientCommand(0, edict, PERSIST_COMMAND" \"%s\" \"%s\" nosend", pKey, pValue);
    g_pClientArray[index].m_ConVars->SetString(modifiedKey.c_str(), pValue);

    sq_pushstring(v, pValue, -1);
    return 1;
}