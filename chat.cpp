// Chat system hooks for R1Delta
// Handles say text messages, chat logging, and related functionality

#include "chat.h"
#include "core.h"
#include "load.h"
#include "logging.h"
#include "squirrel.h"
#include "factory.h"

#include <cstdio>

// Original function pointer
void (*oCServerGameDLL_OnSayTextMsg)(void* pThis, int clientIndex, char* text, char isTeamChat) = nullptr;

// Simplified CBasePlayer interface for accessing player data
class CBasePlayer_Chat {
public:
    uint8_t GetLifeState() const {
        // Offset 865 from the start of the CBaseEntity/CBasePlayer object
        return *(uint8_t*)((uintptr_t)this + 865);
    }

    // Check if alive (assuming LIFE_ALIVE == 0)
    bool IsAlive() const {
        return GetLifeState() == 0; // LIFE_ALIVE is typically 0
    }
};

static __int64 __fastcall UTIL_GetEntityByIndex_Chat(int iIndex)
{
    __int64 result;
    char* pEdicts;
    char* pEnt;
    __int64 v1;

    if (iIndex <= 0)
        return 0LL;

    pEdicts = (char*)pGlobalVarsServer->pEdicts;
    if (!pEdicts)
        return 0LL;

    pEnt = &pEdicts[56 * iIndex];
    result = *(_DWORD*)pEnt >> 1;

    if ((*(_DWORD*)pEnt & 2) == 0)
    {
        v1 = *(_QWORD*)(pEnt + 48);
        if (!v1)
            return 0LL;

        return v1;
    }

    return 0LL;
}

void __fastcall CServerGameDLL_OnSayTextMsg(void* pThis, int clientIndex, char* text, char isTeamChat) {
    // Call Squirrel callback if available
    auto r1sqvm = GetServerVMPtr();
    if (r1sqvm) {
        auto v = r1sqvm->sqvm;

        base_getroottable(v);
        sq_pushstring(v, "CodeCallback_OnClientChatMsg", -1);
        if (SQ_SUCCEEDED(sq_get_noerr(v, -2)) && sq_gettype(v, -1) == OT_CLOSURE) {
            base_getroottable(v);
            sq_pushinteger(r1sqvm, v, clientIndex);
            sq_pushstring(v, text, -1);
            sq_pushbool(r1sqvm, v, isTeamChat);
            if (SQ_SUCCEEDED(sq_call(v, 4, SQTrue, SQTrue))) {
                if (sq_gettype(v, -1) == OT_STRING) sq_getstring(v, -1, (const SQChar**)&text);
                sq_pop(v, 1);
            }
            sq_pop(v, 1);
        }
        sq_pop(v, 1);
    }

    if (!text || !text[0]) return;

    oCServerGameDLL_OnSayTextMsg(pThis, clientIndex, text, isTeamChat);

    // Log chat messages on dedicated server
    if (IsDedicatedServer()) {
        // Get Player Entity
        CBasePlayer_Chat* pSenderEntity = (CBasePlayer_Chat*)UTIL_GetEntityByIndex_Chat(clientIndex);

        if (!pSenderEntity) {
            return;
        }

        // Get Player Name (using vtable index 43 / offset 344)
        uintptr_t* vtable = *(uintptr_t**)pSenderEntity;
        auto getPlayerNameFunc = (const char* (*)(const CBasePlayer_Chat*)) vtable[43];

        const char* playerName = nullptr;
        if (getPlayerNameFunc) {
            playerName = getPlayerNameFunc(pSenderEntity);
        }

        if (!playerName) {
            playerName = "<Unknown>";
        }

        // Check if Player is Dead
        bool isSenderDead = !pSenderEntity->IsAlive();

        // Format the Output String
        char formattedMsg[1024];
        const char* deadPrefix = isSenderDead ? "[DEAD]" : "";
        const char* teamPrefix = isTeamChat ? "[TEAM]" : "";

        snprintf(formattedMsg, sizeof(formattedMsg), "*** CHAT *** %s%s%s: %s",
            deadPrefix,
            teamPrefix,
            playerName,
            text ? text : "<Empty Message>");

        Msg("%s\n", formattedMsg);
    }
}
