// Physics system hooks for R1Delta
#pragma once

#include "core.h"
#include <windows.h>

// VPhysics critical section
extern CRITICAL_SECTION g_vphysics_cs;

// Entity physics hooks
extern __int64 (*oCBaseEntity__VPhysicsInitNormal)(void* a1, unsigned int a2, unsigned int a3, char a4, __int64 a5);
extern void (*oCBaseEntity__SetMoveType)(void* a1, __int64 a2, __int64 a3);
extern void (*oCProjectile__PhysicsSimulate)(__int64 thisptr);
extern void (*oCGrenadeFrag__ResolveFlyCollisionCustom)(__int64 a1, float* a2, float* a3);

__int64 CBaseEntity__VPhysicsInitNormal(void* a1, unsigned int a2, unsigned int a3, char a4, __int64 a5);
void CBaseEntity__SetMoveType(void* a1, __int64 a2, __int64 a3);
__int64 __fastcall UTIL_GetEntityByIndex(int iIndex);
void CProjectile__PhysicsSimulate(__int64 thisptr);
void CGrenadeFrag__ResolveFlyCollisionCustom(__int64 a1, float* a2, float* a3);

// VPhysics thread safety hooks
extern __int64 (*o_sub_1032C0)(__int64, char);
extern __int64 (*o_sub_103120)(__int64, __int64, __int64, int);

__int64 __fastcall sub_1032C0_hook(__int64 a1, char a2);
__int64 __fastcall sub_103120_hook(__int64 a1, __int64 a2, __int64 a3, int a4);

// VPhysics shutdown fix
typedef void(__fastcall* sub_180100880_type)(uintptr_t);
extern sub_180100880_type o_sub_100880;
void __fastcall sub_100880_hook(uintptr_t a1);

// WallrunMove hook
extern bool (*WallrunMove_BlockForTitans_Original)(__int64 a1, __int64 a2, __int64 a3);
bool WallrunMove_BlockForTitans(__int64 a1, __int64 a2, __int64 a3);

// Utility
bool IsMemoryReadable(void* ptr, size_t size, DWORD protect_required_flags_oneof);

// Initialization
void InitPhysicsHooks();
