// Console commands for R1Delta
// Handles noclip, map toggle, and other misc commands

#include "commands.h"
#include "load.h"
#include "logging.h"
#include <array>
#include <cstring>

// Noclip command
#define MOVETYPE_NOCLIP 9

static void DisableNoClip(void* pPlayer) {
    auto sub_03B3200 = reinterpret_cast<void(*)(void*, int, int)>(G_server + 0x3B3200);
    auto sub_025D680 = reinterpret_cast<void(*)(void*, int, const char*)>(G_server + 0x25D680);

    sub_03B3200(pPlayer, 2, 0);
    sub_025D680(pPlayer, 0, "noclip OFF\n");
}

void noclip_cmd(const CCommand& args)
{
    auto UTIL_GetCommandClient = reinterpret_cast<void* (*)()>(G_server + 0x01438F0);
    auto EnableNoClip = reinterpret_cast<void (*)(void*)>(G_server + 0xDAC70);
    void* pPlayer = UTIL_GetCommandClient();
    if (!pPlayer)
        return;
    int pPlayer_GetMoveType = *(unsigned char*)(((uintptr_t)pPlayer) + 444);
    if (args.ArgC() >= 2)
    {
        bool bEnable = atoi(args.Arg(1)) ? true : false;
        if (bEnable && pPlayer_GetMoveType != MOVETYPE_NOCLIP)
        {
            EnableNoClip(pPlayer);
        }
        else if (!bEnable && pPlayer_GetMoveType == MOVETYPE_NOCLIP)
        {
            DisableNoClip(pPlayer);
        }
    }
    else
    {
        // Toggle the noclip state if there aren't any arguments.
        if (pPlayer_GetMoveType != MOVETYPE_NOCLIP)
        {
            EnableNoClip(pPlayer);
        }
        else
        {
            DisableNoClip(pPlayer);
        }
    }
}

// Dummy fullscreen map toggle command (eats input for script)
void toggleFullscreenMap_cmd(const CCommand& ccargs) {
    return;
}

namespace {

// TFO's variant_t is 24 bytes. The event queue expects its EHANDLE sentinel at
// +16 and its field type at +20.
struct R1OVariant {
    std::array<unsigned char, 16> value{};
    int entityHandle = -1;
    int fieldType = 0;
};
static_assert(sizeof(R1OVariant) == 24);

using R1OAllocPooledString = const char* (__fastcall*)(void*, const char*);
using R1OEventQueueAddEvent = void(__fastcall*)(
    void* eventQueue,
    const char* target,
    const char* action,
    const R1OVariant* value,
    const R1OVariant* secondaryValue,
    float delay,
    void* activator,
    void* caller,
    int outputId);

void SetR1OVariantString(R1OVariant& value, const char* pooledString)
{
    static_assert(sizeof(pooledString) <= sizeof(value.value));
    memcpy(value.value.data(), &pooledString, sizeof(pooledString));
    value.fieldType = 2; // FIELD_STRING in the TFO server.
}

} // namespace

bool TryHandleR1ODedicatedConsoleEntFire(int commandSource, int argc, const char* const* argv)
{
    if (!IsR1ODedicatedServer()
        || !G_server
        || commandSource != 1 // src_command: server console, cfg, or RCON.
        || argc < 1
        || !argv
        || !argv[0]
        || _stricmp(argv[0], "ent_fire") != 0)
        return false;

    ConVarR1* svCheats = OriginalCCVar_FindVar
        ? OriginalCCVar_FindVar(cvarinterface, "sv_cheats")
        : nullptr;
    if (!svCheats || svCheats->m_Value.m_nValue == 0) {
        if (AreR1OFakeDediVerboseLogsEnabled()) {
            char diagnostic[192];
            _snprintf_s(
                diagnostic,
                sizeof(diagnostic),
                _TRUNCATE,
                "R1Delta: R1O dedicated ent_fire denied svCheats=%p value=%d\n",
                svCheats,
                svCheats ? svCheats->m_Value.m_nValue : -1);
            OutputDebugStringA(diagnostic);
        }
        Msg("Can't use cheat command ent_fire unless the server has sv_cheats set to 1.\n");
        return true;
    }

    if (argc < 2 || !argv[1]) {
        Msg("Usage:\n   ent_fire <target> [action] [value] [delay]\n");
        return true;
    }

    auto allocPooledString = reinterpret_cast<R1OAllocPooledString>(G_server + 0x663CA0);
    auto addEvent = reinterpret_cast<R1OEventQueueAddEvent>(G_server + 0x0D5B90);
    void* stringPool = reinterpret_cast<void*>(G_server + 0xC48C38);
    void* eventQueue = reinterpret_cast<void*>(G_server + 0xB2D9F0);

    const char* target = allocPooledString(stringPool, argv[1]);
    const char* action = argc >= 3 && argv[2]
        ? allocPooledString(stringPool, argv[2])
        : "Use";

    R1OVariant value;
    if (argc >= 4 && argv[3])
        SetR1OVariantString(value, allocPooledString(stringPool, argv[3]));

    R1OVariant secondaryValue;
    const float delay = argc >= 5 && argv[4]
        ? static_cast<float>(atof(argv[4]))
        : 0.0f;

    addEvent(
        eventQueue,
        target,
        action,
        &value,
        &secondaryValue,
        delay,
        nullptr,
        nullptr,
        0);

    if (AreR1OFakeDediVerboseLogsEnabled()) {
        char diagnostic[768];
        _snprintf_s(
            diagnostic,
            sizeof(diagnostic),
            _TRUNCATE,
            "R1Delta: R1O dedicated console ent_fire queued target=%s action=%s value=%s delay=%.3f\n",
            argv[1],
            argc >= 3 && argv[2] ? argv[2] : "Use",
            argc >= 4 && argv[3] ? argv[3] : "",
            delay);
        OutputDebugStringA(diagnostic);
    }
    return true;
}
