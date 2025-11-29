// Player hooks for R1Delta
// Handles rank, level, and player-related functions

#include "player.h"
#include "load.h"

// Rank function hooks (for Squirrel)
void __fastcall HookedSetRankFunction(__int64 player, int rank)
{
    if (rank > 100) return;
    *(int*)(player + 0x15A0) = rank;
}

int __fastcall HookedGetRankFunction(__int64 player)
{
    return *(int*)(player + 0x15A0);
}

// Player level hook
int __fastcall CPlayer_GetLevel(__int64 thisptr)
{
    int xp = *(int*)(thisptr + 0x1834);
    typedef int (*GetLevelFromXP_t)(int xp);
    GetLevelFromXP_t GetLevelFromXP = (GetLevelFromXP_t)(G_server + 0x28E740);
    return GetLevelFromXP(xp);
}
