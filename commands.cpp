// Console commands for R1Delta
// Handles noclip, map toggle, and other misc commands

#include "commands.h"
#include "load.h"
#include "logging.h"

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
