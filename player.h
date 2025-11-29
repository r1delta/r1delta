// Player hooks for R1Delta
#pragma once

#include <cstdint>

// Rank functions (for Squirrel)
void __fastcall HookedSetRankFunction(__int64 player, int rank);
int __fastcall HookedGetRankFunction(__int64 player);

// Player level
int __fastcall CPlayer_GetLevel(__int64 thisptr);
