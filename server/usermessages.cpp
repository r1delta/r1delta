// User message hooks for R1Delta
// Handles reordering/disabling user messages for TF Online compatibility

#include "usermessages.h"
#include "core.h"
#include "load.h"
#include "logging.h"

#include <cstring>

// Commands to block for TF Online compatibility
static bool shouldBlock(const char* a2) {
    struct str8 {
        char* p;
        size_t l;
    };
#define str8_lit_(S) { (char*)S, sizeof(S) - 1 }
    const str8 commandsToSave[] = {
        str8_lit_("InitHUD"),
        str8_lit_("FullyConnected"),
        str8_lit_("MatchIsOver"),
        str8_lit_("ChangeTitanBuildPoint"),
        str8_lit_("ChangeCurrentTitanBuildPoint"),
        str8_lit_("TutorialStatus"),
        str8_lit_("ChangeCurrentTitanOID")
    };
#undef str8_lit_
    for (auto& command : commandsToSave) {
        if (memcmp(a2, command.p, command.l + 1) == 0) {
            return true;
        }
    }
    return false;
}

// Original function pointer
UserMessage_ReorderHook_t UserMessage_ReorderHook_Original = nullptr;

// Hook to reorder/disable UserMessages so that usermessage IDs from server.dll
// in Titanfall Online line up with those on the client (or vice versa)
__int64 __fastcall UserMessage_ReorderHook(__int64 a1, const char* a2, int a3) {
    if (shouldBlock(a2)) {
        return -1;
    }
    return UserMessage_ReorderHook_Original(a1, a2, a3);
}
