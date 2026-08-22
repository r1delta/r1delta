// Physics system hooks for R1Delta
#pragma once

#include "core.h"
#include <windows.h>

// VPhysics critical section
extern CRITICAL_SECTION g_vphysics_cs;

__int64 __fastcall UTIL_GetEntityByIndex(int iIndex);

void InstallR1VPhysicsSequentialDispatcherGuard(uintptr_t vphysicsBase);

// R1 VPhysics level-shutdown recovery
bool InstallR1VPhysicsShutdownGuard(uintptr_t vphysicsBase);

// WallrunMove hook
extern bool (*WallrunMove_BlockForTitans_Original)(__int64 a1, __int64 a2, __int64 a3);
bool WallrunMove_BlockForTitans(__int64 a1, __int64 a2, __int64 a3);


// Initialization
void InitPhysicsHooks();
