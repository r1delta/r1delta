// Physics system hooks for R1Delta
// Handles VPhysics fixes, entity physics, grenade physics, etc.

#include "physics.h"
#include "physics_hooks.h"
#include "core.h"
#include "load.h"
#include "logging.h"
#include "vector.h"
#include "defs.h"
#include "factory.h"

#include <windows.h>
#include <intrin.h>

#pragma intrinsic(_ReturnAddress)

// VPhysics critical section for thread safety
CRITICAL_SECTION g_vphysics_cs;

// Original function pointers
__int64 (*oCBaseEntity__VPhysicsInitNormal)(void* a1, unsigned int a2, unsigned int a3, char a4, __int64 a5) = nullptr;
void (*oCBaseEntity__SetMoveType)(void* a1, __int64 a2, __int64 a3) = nullptr;
void (*oCProjectile__PhysicsSimulate)(__int64 thisptr) = nullptr;
void (*oCGrenadeFrag__ResolveFlyCollisionCustom)(__int64 a1, float* a2, float* a3) = nullptr;

__int64 CBaseEntity__VPhysicsInitNormal(void* a1, unsigned int a2, unsigned int a3, char a4, __int64 a5)
{
    if (uintptr_t(_ReturnAddress()) == (G_server + 0xB63FD))
        return 0;
    else
        return oCBaseEntity__VPhysicsInitNormal(a1, a2, a3, a4, a5);
}

void CBaseEntity__SetMoveType(void* a1, __int64 a2, __int64 a3)
{
    if (uintptr_t(_ReturnAddress()) == (G_server + 0xB64EC))
    {
        a2 = 5;
        a3 = 1;
    }
    return oCBaseEntity__SetMoveType(a1, a2, a3);
}

__int64 __fastcall UTIL_GetEntityByIndex(int iIndex)
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
        // Inline of IServerUnknown::GetBaseEntity
        v1 = *(_QWORD*)(pEnt + 48);
        if (!v1)
            return 0LL;

        return v1;
    }

    return 0LL;
}

void CProjectile__PhysicsSimulate(__int64 thisptr)
{
    oCProjectile__PhysicsSimulate(thisptr);

    // Only apply to specific projectile type (wingman knives?)
    if (*(uintptr_t*)thisptr == (G_server + 0x8DA9D0)) {
        // Get current velocity
        Vector vecVelocity;
        memcpy(&vecVelocity, reinterpret_cast<Vector* (*)(uintptr_t)>(G_server + 0x91B10)(thisptr), sizeof(Vector));

        float flSpeed = vecVelocity.Length();

        // Stop when velocity is very low
        if (flSpeed < 5.f && vecVelocity.z) {
            reinterpret_cast<Vector* (*)(uintptr_t)>(G_server + 0x3BB300)(thisptr)->Zero();
            return;
        }

        // Calculate direction vector
        Vector vecDirection;
        if (flSpeed > 0.1f) {
            vecDirection.x = vecVelocity.x / flSpeed;
            vecDirection.y = vecVelocity.y / flSpeed;
            vecDirection.z = vecVelocity.z / flSpeed;
        }
        else {
            vecDirection.x = 0;
            vecDirection.y = 0;
            vecDirection.z = 1;
        }

        // Base rotation speed
        float baseSpinRate = 8.0f;

        // Make spin rate somewhat proportional to speed
        float speedFactor = 500.f * (0.3f + 0.7f * (flSpeed / 1300.0f));
        float spinRate = baseSpinRate * speedFactor;

        // Set rotation primarily along the direction of travel
        Vector angVelocity;
        angVelocity.x = vecDirection.x * spinRate;
        angVelocity.y = vecDirection.y * spinRate;
        angVelocity.z = vecDirection.z * spinRate;

        // Apply the new angular velocity each frame
        reinterpret_cast<void(*)(__int64 thisptr, float, float, float)>(G_server + 0x3BB2A0)(thisptr,
            angVelocity.x, angVelocity.y, angVelocity.z);
    }
}

void CGrenadeFrag__ResolveFlyCollisionCustom(__int64 a1, float* a2, float* a3)
{
    float x = *(float*)(a1 + 636);
    float y = *((float*)(a1 + 636) + 1);
    float z = *(float*)(a1 + 644);

    char trace[104];
    Vector vel;
    memcpy(trace, a2, sizeof(trace));
    memcpy(&vel, a3, sizeof(vel));

    static auto sub_4AA8E0 = reinterpret_cast<void(*)(__int64, __int64)>(G_server + 0x4AA8E0);

    // Calculate speed squared
    float speedSqr = x * x + y * y + z * z;

    // If speed is below threshold (30*30), zero out velocity
    if (speedSqr < 900.0f)
    {
        Vector zeroVel = Vector(0, 0, 0);
        static auto CBaseEntity__SetAbsVelocity = reinterpret_cast<void(*)(__int64, Vector*)>(G_server + 0x3BA970);
        CBaseEntity__SetAbsVelocity(a1, &zeroVel);
        oCBaseEntity__SetMoveType((void*)a1, 0, 0);
        return;
    }

    oCGrenadeFrag__ResolveFlyCollisionCustom(a1, (float*)&trace, (float*)&vel);

    // Stop if on ground.
    if (a2[10] > 0.9)  // Floor
    {
        static auto sub_1803B9B30 = reinterpret_cast<void(*)(__int64)>(G_server + 0x3b9b30);
        if ((*(_DWORD*)(a1 + 352) & 0x1000LL) != 0)
            sub_1803B9B30(a1);

        // Get current velocity after bounce
        Vector currVel = *(Vector*)(a1 + 636);

        // Calculate speed squared
        float speedSqr = currVel.x * currVel.x + currVel.y * currVel.y + currVel.z * currVel.z;

        // If speed is below threshold (30*30), zero out velocity
        if (speedSqr < 900.0f)
        {
            currVel = Vector(0, 0, 0);
            static auto CBaseEntity__SetAbsVelocity = reinterpret_cast<void(*)(__int64, Vector*)>(G_server + 0x3BA970);
            CBaseEntity__SetAbsVelocity(a1, &currVel);
        }

        // Set ground entity
        auto ent = *(_QWORD*)((__int64)(a2)+96);
        if ((*(unsigned __int8(__fastcall**)(__int64, __int64))(*(_QWORD*)a1 + 824LL))(a1, ent) && a2[10] == 1.0f)
            sub_4AA8E0(a1, ent);
    }
}

// VPhysics thread safety hooks
__int64 (*o_sub_1032C0)(__int64, char) = nullptr;
__int64 (*o_sub_103120)(__int64, __int64, __int64, int) = nullptr;

__int64 __fastcall sub_1032C0_hook(__int64 a1, char a2)
{
    EnterCriticalSection(&g_vphysics_cs);
    __int64 ret = o_sub_1032C0(a1, a2);
    LeaveCriticalSection(&g_vphysics_cs);
    return ret;
}

__int64 __fastcall sub_103120_hook(__int64 a1, __int64 a2, __int64 a3, int a4)
{
    EnterCriticalSection(&g_vphysics_cs);

    // Get base address of vphysics.dll
    static uintptr_t base = (uintptr_t)GetModuleHandleA("vphysics.dll");

    // Calculate addresses of key memory locations
    uintptr_t physics_pp_mindists_addr = base + 0x1EF1D0;
    int* physics_pp_mindists = (int*)(physics_pp_mindists_addr + 0x5C);
    uintptr_t qword_1801EF258_addr = base + 0x1EF258;
    void** qword_1801EF258 = (void**)qword_1801EF258_addr;

    // Check if we need to manually set the pointer (non-parallel path)
    int needsManualSet = (*physics_pp_mindists == 0);

    // Save original value and set our value if needed
    void* original_value = NULL;
    if (needsManualSet) {
        original_value = *qword_1801EF258;
        *qword_1801EF258 = (void*)(a1 + 0x100078);
    }

    // Call original function
    __int64 ret = o_sub_103120(a1, a2, a3, a4);

    // Restore original value if we changed it
    if (needsManualSet) {
        *qword_1801EF258 = original_value;
    }

    LeaveCriticalSection(&g_vphysics_cs);
    return ret;
}

inline bool IsMemoryReadable(void* ptr, size_t size, DWORD protect_required_flags_oneof)
{
    static SYSTEM_INFO sysInfo;
    if (!sysInfo.dwPageSize)
        GetSystemInfo(&sysInfo);

    MEMORY_BASIC_INFORMATION memInfo;

    if (!VirtualQuery(ptr, &memInfo, sizeof(memInfo)))
        return false;

    if (memInfo.RegionSize < size)
        return false;

    return (memInfo.State & MEM_COMMIT) && !(memInfo.Protect & PAGE_NOACCESS) && (memInfo.Protect & protect_required_flags_oneof) != 0;
}

typedef void(__fastcall* sub_180100880_type)(uintptr_t);
sub_180100880_type o_sub_100880 = nullptr;

void __fastcall sub_100880_hook(uintptr_t a1)
{
    uintptr_t vPhysicsBase = (uintptr_t)GetModuleHandleA("vphysics.dll");
    static auto* sub_1800FFB50 = reinterpret_cast<__int64(*)(uintptr_t)>(vPhysicsBase + 0xFFB50);
    static auto* sub_1800FF010 = reinterpret_cast<__int64(*)(uintptr_t)>(vPhysicsBase + 0xFF010);
    static auto* sub_1800CA0B0 = reinterpret_cast<__int64(*)(uintptr_t)>(vPhysicsBase + 0xCA0B0);
    void(__fastcall * **v2)(_QWORD, __int64);
    __int64 v3;
    sub_1800FFB50(a1);
    sub_1800FF010(a1);
    int i = 0;
    while (*(__int16*)(a1 + 1310866))
    {
        i++;
        v2 = **(void(__fastcall*****)(_QWORD, __int64))(a1 + 1310872);
        if (v2)
        {
            if (*v2 && **v2)
            {
                // Always do memory readable check
                if (!IsMemoryReadable(**v2, 8, PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))
                {
                    break;
                }
                (**v2)((_QWORD)v2, 1i64);
            }
        }
    }
    v3 = *(_QWORD*)(a1 + 1310872);
    if (v3 != a1 + 1310880)
    {
        if (v3)
            sub_1800CA0B0(v3);
        *(_QWORD*)(a1 + 1310872) = 0i64;
        *(__int16*)(a1 + 1310864) = 0;
    }
    *(__int16*)(a1 + 1310866) = 0;
    DeleteCriticalSection((LPCRITICAL_SECTION)(a1 + 8));
}

// WallrunMove hook - blocks titans from wallrunning (otherwise they try to)
bool (*WallrunMove_BlockForTitans_Original)(__int64 a1, __int64 a2, __int64 a3) = nullptr;

bool WallrunMove_BlockForTitans(__int64 a1, __int64 a2, __int64 a3)
{
    auto ent = *(_QWORD*)(a1 + 8);
    if (ent && *(char**)(ent + 0xd8) && V_stristr(*(char**)(ent + 0xd8), "titan"))
        return 0;
    return WallrunMove_BlockForTitans_Original(a1, a2, a3);
}

void InitPhysicsHooks()
{
    InitializeCriticalSectionAndSpinCount(&g_vphysics_cs, 4000);
}
