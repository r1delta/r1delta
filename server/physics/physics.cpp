// Physics system hooks for R1Delta
// Handles VPhysics fixes, entity physics, grenade physics, etc.

#include "physics.h"
#include "physics_hooks.h"
#include "core.h"
#include "load.h"
#include "logging.h"
#include "defs.h"
#include "factory.h"

#include <windows.h>

// VPhysics critical section for thread safety
CRITICAL_SECTION g_vphysics_cs;

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
