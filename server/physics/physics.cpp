// Physics system hooks for R1Delta
// Handles VPhysics fixes, entity physics, grenade physics, etc.

#include "physics.h"
#include "physics_hooks.h"
#include "vphysics_shutdown_guard.h"
#include "vphysics_parallel_guard.h"
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

namespace
{
using R1VPhysicsSequentialDispatcher =
    __int64(__fastcall*)(__int64 manager);

R1VPhysicsSequentialDispatcher s_R1VPhysicsSequentialDispatcherOriginal;
uintptr_t s_R1VPhysicsSequentialDispatcherTarget;
uintptr_t s_R1VPhysicsSequentialDispatcherBase;

class R1VPhysicsCriticalSectionScope
{
public:
    explicit R1VPhysicsCriticalSectionScope(CRITICAL_SECTION* criticalSection)
        : m_criticalSection(criticalSection)
    {
        EnterCriticalSection(m_criticalSection);
    }

    ~R1VPhysicsCriticalSectionScope()
    {
        LeaveCriticalSection(m_criticalSection);
    }

    R1VPhysicsCriticalSectionScope(
        const R1VPhysicsCriticalSectionScope&) = delete;
    R1VPhysicsCriticalSectionScope& operator=(
        const R1VPhysicsCriticalSectionScope&) = delete;

private:
    CRITICAL_SECTION* m_criticalSection;
};

[[noreturn]] void FailR1VPhysicsSequentialDispatcherInvariant(
    const char* reason,
    uintptr_t detail,
    MH_STATUS status)
{
    constexpr DWORD kSequentialDispatcherInvariantFailure = 0xE042101C;
    char buffer[320];
    _snprintf_s(
        buffer,
        sizeof(buffer),
        _TRUNCATE,
        "R1Delta: fatal R1 VPhysics sequential-dispatch invariant "
        "reason=%s detail=%p status=%d\n",
        reason,
        reinterpret_cast<void*>(detail),
        static_cast<int>(status));
    OutputDebugStringA(buffer);

    const ULONG_PTR arguments[] = {
        reinterpret_cast<ULONG_PTR>(reason),
        static_cast<ULONG_PTR>(detail),
        static_cast<ULONG_PTR>(status),
    };
    RaiseException(
        kSequentialDispatcherInvariantFailure,
        EXCEPTION_NONCONTINUABLE,
        static_cast<DWORD>(std::size(arguments)),
        arguments);
    TerminateProcess(
        GetCurrentProcess(),
        kSequentialDispatcherInvariantFailure);
    __assume(0);
}

__int64 __fastcall R1VPhysicsSequentialDispatcherGuard(__int64 manager)
{
    using namespace r1delta::vphysics;
    if (!s_R1VPhysicsSequentialDispatcherOriginal)
    {
        FailR1VPhysicsSequentialDispatcherInvariant(
            "missing-original",
            s_R1VPhysicsSequentialDispatcherTarget,
            MH_UNKNOWN);
    }
    if (manager < 0x10000)
    {
        FailR1VPhysicsSequentialDispatcherInvariant(
            "invalid-manager",
            static_cast<uintptr_t>(manager),
            MH_UNKNOWN);
    }

    R1VPhysicsCriticalSectionScope lock(&g_vphysics_cs);
    auto* const parallelEnabled = reinterpret_cast<int*>(
        s_R1VPhysicsSequentialDispatcherBase + kParallelEnabledRva);
    auto* const workerPointer = reinterpret_cast<uintptr_t*>(
        s_R1VPhysicsSequentialDispatcherBase
        + kSequentialWorkerPointerRva);
    ScopedSequentialDispatcherState state(
        parallelEnabled,
        workerPointer,
        static_cast<uintptr_t>(manager));
    if (!state.Entered())
    {
        FailR1VPhysicsSequentialDispatcherInvariant(
            "state-entry",
            static_cast<uintptr_t>(manager),
            MH_UNKNOWN);
    }

    return s_R1VPhysicsSequentialDispatcherOriginal(manager);
}
}

void InstallR1VPhysicsSequentialDispatcherGuard(uintptr_t vphysicsBase)
{
    using namespace r1delta::vphysics;
    const uintptr_t target =
        vphysicsBase + kSequentialDispatcherRva;
    if (!IsExpectedR1VPhysicsModule(vphysicsBase))
    {
        FailR1VPhysicsSequentialDispatcherInvariant(
            "unexpected-image",
            vphysicsBase,
            MH_UNKNOWN);
    }

    if (s_R1VPhysicsSequentialDispatcherTarget)
    {
        if (s_R1VPhysicsSequentialDispatcherTarget != target)
        {
            FailR1VPhysicsSequentialDispatcherInvariant(
                "second-target",
                target,
                MH_UNKNOWN);
        }
        const MH_STATUS enableStatus =
            MH_EnableHook(reinterpret_cast<void*>(target));
        if (enableStatus == MH_OK
            || enableStatus == MH_ERROR_ENABLED)
        {
            return;
        }
        FailR1VPhysicsSequentialDispatcherInvariant(
            "reenable",
            target,
            enableStatus);
    }

    if (memcmp(
            reinterpret_cast<const void*>(target),
            kSequentialDispatcherExpectedPrologue,
            sizeof(kSequentialDispatcherExpectedPrologue)) != 0)
    {
        FailR1VPhysicsSequentialDispatcherInvariant(
            "prologue",
            target,
            MH_UNKNOWN);
    }

    const MH_STATUS createStatus = MH_CreateHook(
        reinterpret_cast<void*>(target),
        &R1VPhysicsSequentialDispatcherGuard,
        reinterpret_cast<void**>(
            &s_R1VPhysicsSequentialDispatcherOriginal));
    if (createStatus != MH_OK
        || !s_R1VPhysicsSequentialDispatcherOriginal)
    {
        FailR1VPhysicsSequentialDispatcherInvariant(
            "create",
            target,
            createStatus);
    }

    s_R1VPhysicsSequentialDispatcherBase = vphysicsBase;
    s_R1VPhysicsSequentialDispatcherTarget = target;
    const MH_STATUS enableStatus =
        MH_EnableHook(reinterpret_cast<void*>(target));
    if (enableStatus != MH_OK
        && enableStatus != MH_ERROR_ENABLED)
    {
        FailR1VPhysicsSequentialDispatcherInvariant(
            "enable",
            target,
            enableStatus);
    }

    OutputDebugStringA(
        "R1Delta: R1 VPhysics sequential dispatcher guard installed\n");
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
