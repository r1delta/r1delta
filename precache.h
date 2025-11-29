// Precache system hooks for R1Delta
#pragma once

#include "core.h"

// SetPreCache hook
extern void* SetPreCache_o;
__int64 __fastcall SetPreCache(__int64 a1, __int64 a2, char a3);

// CHL2_Player precache hook
void CHL2_Player_Precache(uintptr_t a1, uintptr_t a2);

// Dump precache stats command
void __fastcall cl_DumpPrecacheStats(__int64 CClientState, const char* name);
