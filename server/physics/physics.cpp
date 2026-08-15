// Physics system hooks for R1Delta
// Handles VPhysics fixes, entity physics, grenade physics, etc.

#include "physics.h"
#include "physics_hooks.h"
#include "vphysics_shutdown_guard.h"
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

namespace
{
r1delta::vphysics::ShutdownFunctions s_R1VPhysicsShutdownFunctions{};
uintptr_t s_R1VPhysicsShutdownTarget{};
volatile LONG s_R1VPhysicsShutdownLogBudget = 8;

void __fastcall DeleteR1VPhysicsCriticalSection(uintptr_t address)
{
    DeleteCriticalSection(reinterpret_cast<LPCRITICAL_SECTION>(address));
}

void __fastcall R1VPhysicsShutdownGuard(uintptr_t owner)
{
    const r1delta::vphysics::ShutdownResult result =
        r1delta::vphysics::RunR1VPhysicsShutdown(
            owner,
            s_R1VPhysicsShutdownFunctions);
    if (result.failure == r1delta::vphysics::ShutdownFailure::None
        || InterlockedDecrement(&s_R1VPhysicsShutdownLogBudget) < 0)
    {
        return;
    }

    Warning(
        "R1Delta: VPhysics level-shutdown recovery stopped draining: %s "
        "(detail=%p callbacks=%u storageReleased=%d criticalSectionDeleted=%d)\n",
        r1delta::vphysics::ShutdownFailureText(result.failure),
        reinterpret_cast<void*>(result.detail),
        result.callbacksInvoked,
        result.storageReleased ? 1 : 0,
        result.criticalSectionDeleted ? 1 : 0);
}
}

bool InstallR1VPhysicsShutdownGuard(uintptr_t vphysicsBase)
{
    if (!r1delta::vphysics::IsExpectedR1VPhysicsModule(vphysicsBase))
    {
        Warning(
            "R1Delta: VPhysics level-shutdown guard refused module at %p; "
            "expected mapped AMD64 image timestamp=0x%08X size=0x%X\n",
            reinterpret_cast<void*>(vphysicsBase),
            r1delta::vphysics::kR1VPhysicsTimeDateStamp,
            r1delta::vphysics::kR1VPhysicsSizeOfImage);
        return false;
    }
    const uintptr_t target =
        vphysicsBase + r1delta::vphysics::kR1VPhysicsShutdownRva;
    if (s_R1VPhysicsShutdownTarget)
    {
        if (s_R1VPhysicsShutdownTarget != target)
        {
            Warning(
                "R1Delta: VPhysics level-shutdown guard refused a second "
                "module target at %p\n",
                reinterpret_cast<void*>(target));
            return false;
        }

        const MH_STATUS enableStatus =
            MH_EnableHook(reinterpret_cast<void*>(target));
        return enableStatus == MH_OK || enableStatus == MH_ERROR_ENABLED;
    }

    if (!r1delta::vphysics::HasExpectedR1VPhysicsShutdownCode(vphysicsBase))
    {
        Warning(
            "R1Delta: VPhysics level-shutdown guard skipped; "
            "expected executable code bytes did not match at %p\n",
            reinterpret_cast<void*>(target));
        return false;
    }

    r1delta::vphysics::ShutdownFunctions functions{
        reinterpret_cast<r1delta::vphysics::ShutdownStep>(
            vphysicsBase + r1delta::vphysics::kR1VPhysicsPrepareQueueRva),
        reinterpret_cast<r1delta::vphysics::ShutdownStep>(
            vphysicsBase + r1delta::vphysics::kR1VPhysicsPrepareResourcesRva),
        reinterpret_cast<r1delta::vphysics::ShutdownStep>(
            vphysicsBase + r1delta::vphysics::kR1VPhysicsReleaseStorageRva),
        &DeleteR1VPhysicsCriticalSection,
    };

    const MH_STATUS createStatus = MH_CreateHook(
        reinterpret_cast<void*>(target),
        &R1VPhysicsShutdownGuard,
        nullptr);
    if (createStatus != MH_OK)
    {
        Warning(
            "R1Delta: VPhysics level-shutdown guard create failed "
            "(status=%d target=%p)\n",
            static_cast<int>(createStatus),
            reinterpret_cast<void*>(target));
        return false;
    }

    s_R1VPhysicsShutdownFunctions = functions;
    s_R1VPhysicsShutdownTarget = target;
    const MH_STATUS enableStatus =
        MH_EnableHook(reinterpret_cast<void*>(target));
    if (enableStatus == MH_OK || enableStatus == MH_ERROR_ENABLED)
        return true;

    Warning(
        "R1Delta: VPhysics level-shutdown guard enable failed "
        "(status=%d target=%p)\n",
        static_cast<int>(enableStatus),
        reinterpret_cast<void*>(target));
    const MH_STATUS removeStatus =
        MH_RemoveHook(reinterpret_cast<void*>(target));
    if (removeStatus == MH_OK)
    {
        s_R1VPhysicsShutdownFunctions = {};
        s_R1VPhysicsShutdownTarget = 0;
    }
    else
    {
        Warning(
            "R1Delta: VPhysics level-shutdown guard rollback failed "
            "(status=%d); retaining valid detour state for bulk enable\n",
            static_cast<int>(removeStatus));
    }
    return false;
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
