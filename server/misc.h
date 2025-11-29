// Miscellaneous hooks for R1Delta
#pragma once

#include "core.h"
#include "cvar.h"
#include "factory.h"

// VTable manipulation
BOOL RemoveItemsFromVTable(uintptr_t vTableAddr, size_t indexStart, size_t itemCount);

// Build number
const char* GetBuildNo();

// Rank functions
void __fastcall HookedSetRankFunction(__int64 player, int rank);
int __fastcall HookedGetRankFunction(__int64 player);

// Player level
int __fastcall CPlayer_GetLevel(__int64 thisptr);

// Noclip command
void noclip_cmd(const CCommand& args);

// Dummy fullscreen map toggle
void toggleFullscreenMap_cmd(const CCommand& ccargs);

// Recent host vars
extern ConVarR1* host_mostRecentMapCvar;
extern ConVarR1* host_mostRecentGamemodeCvar;
void GamemodeChangeCallback(IConVar* var_iconvar, const char* pOldValue, float flOldValue);
void HostMapChangeCallback(IConVar* var_iconvar, const char* pOldValue, float flOldValue);
void InitializeRecentHostVars();

// Shutdown hook
__int64 __fastcall CServerGameDLL_DLLShutdown(__int64 a1, void*** a2, __int64 a3);
