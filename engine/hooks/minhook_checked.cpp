#include "minhook_checked.h"

#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
constexpr size_t kMaxTrackedHooks = 4096;

struct TrackedHook
{
    LPVOID target;
    const char* sourceFile;
    int sourceLine;
};

SRWLOCK g_trackedHooksLock = SRWLOCK_INIT;
TrackedHook g_trackedHooks[kMaxTrackedHooks]{};
size_t g_trackedHookCount = 0;

MH_STATUS RawInitialize()
{
    return (MH_Initialize)();
}

MH_STATUS RawUninitialize()
{
    return (MH_Uninitialize)();
}

MH_STATUS RawCreateHook(LPVOID target, LPVOID detour, LPVOID* original)
{
    return (MH_CreateHook)(target, detour, original);
}

MH_STATUS RawRemoveHook(LPVOID target)
{
    return (MH_RemoveHook)(target);
}

MH_STATUS RawEnableHook(LPVOID target)
{
    return (MH_EnableHook)(target);
}

MH_STATUS RawDisableHook(LPVOID target)
{
    return (MH_DisableHook)(target);
}

MH_STATUS RawQueueEnableHook(LPVOID target)
{
    return (MH_QueueEnableHook)(target);
}

MH_STATUS RawQueueDisableHook(LPVOID target)
{
    return (MH_QueueDisableHook)(target);
}

MH_STATUS RawApplyQueued()
{
    return (MH_ApplyQueued)();
}

bool IsEnableSuccess(MH_STATUS status)
{
    return status == MH_OK || status == MH_ERROR_ENABLED;
}

void DescribeTarget(LPVOID target, char* output, size_t outputSize)
{
    if (target == MH_ALL_HOOKS)
    {
        std::snprintf(output, outputSize, "MH_ALL_HOOKS");
        return;
    }

    MEMORY_BASIC_INFORMATION memory{};
    if (target != nullptr &&
        VirtualQuery(target, &memory, sizeof(memory)) == sizeof(memory) &&
        memory.AllocationBase != nullptr)
    {
        char modulePath[MAX_PATH]{};
        if (GetModuleFileNameA(
                reinterpret_cast<HMODULE>(memory.AllocationBase),
                modulePath,
                static_cast<DWORD>(sizeof(modulePath))) != 0)
        {
            const char* moduleName = std::strrchr(modulePath, '\\');
            moduleName = moduleName != nullptr ? moduleName + 1 : modulePath;
            const auto rva =
                reinterpret_cast<uintptr_t>(target) -
                reinterpret_cast<uintptr_t>(memory.AllocationBase);
            std::snprintf(output, outputSize, "%s+0x%llX",
                moduleName, static_cast<unsigned long long>(rva));
            return;
        }
    }

    std::snprintf(output, outputSize, "%p", target);
}

void ReportStatus(
    const char* operation,
    LPVOID target,
    MH_STATUS status,
    const char* sourceFile,
    int sourceLine,
    const char* detail = nullptr)
{
    char targetDescription[MAX_PATH + 48]{};
    DescribeTarget(target, targetDescription, sizeof(targetDescription));

    char message[1024]{};
    std::snprintf(
        message,
        sizeof(message),
        "[r1delta_minhook] %s target=%s status=%s source=%s:%d%s%s\n",
        operation,
        targetDescription,
        (MH_StatusToString)(status),
        sourceFile != nullptr ? sourceFile : "<unknown>",
        sourceLine,
        detail != nullptr ? " detail=" : "",
        detail != nullptr ? detail : "");

    OutputDebugStringA(message);

    const HANDLE stderrHandle = GetStdHandle(STD_ERROR_HANDLE);
    if (stderrHandle != nullptr && stderrHandle != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0;
        WriteFile(
            stderrHandle,
            message,
            static_cast<DWORD>(std::strlen(message)),
            &written,
            nullptr);
    }
}

void TrackHook(LPVOID target, const char* sourceFile, int sourceLine)
{
    AcquireSRWLockExclusive(&g_trackedHooksLock);

    for (size_t i = 0; i < g_trackedHookCount; ++i)
    {
        if (g_trackedHooks[i].target == target)
        {
            ReleaseSRWLockExclusive(&g_trackedHooksLock);
            return;
        }
    }

    if (g_trackedHookCount < kMaxTrackedHooks)
    {
        g_trackedHooks[g_trackedHookCount++] =
            TrackedHook{target, sourceFile, sourceLine};
        ReleaseSRWLockExclusive(&g_trackedHooksLock);
        return;
    }

    ReleaseSRWLockExclusive(&g_trackedHooksLock);
    ReportStatus(
        "track",
        target,
        MH_ERROR_MEMORY_ALLOC,
        sourceFile,
        sourceLine,
        "tracked-hook capacity exhausted");
}

void UntrackHook(LPVOID target)
{
    AcquireSRWLockExclusive(&g_trackedHooksLock);

    for (size_t i = 0; i < g_trackedHookCount; ++i)
    {
        if (g_trackedHooks[i].target == target)
        {
            g_trackedHooks[i] = g_trackedHooks[g_trackedHookCount - 1];
            --g_trackedHookCount;
            break;
        }
    }

    ReleaseSRWLockExclusive(&g_trackedHooksLock);
}

void ClearTrackedHooks()
{
    AcquireSRWLockExclusive(&g_trackedHooksLock);
    g_trackedHookCount = 0;
    ReleaseSRWLockExclusive(&g_trackedHooksLock);
}

size_t SnapshotTrackedHooks(TrackedHook* hooks, size_t capacity)
{
    AcquireSRWLockShared(&g_trackedHooksLock);
    const size_t count =
        g_trackedHookCount < capacity ? g_trackedHookCount : capacity;
    std::memcpy(hooks, g_trackedHooks, count * sizeof(TrackedHook));
    ReleaseSRWLockShared(&g_trackedHooksLock);
    return count;
}
}

MH_STATUS WINAPI R1D_MH_Initialize(const char* sourceFile, int sourceLine)
{
    const MH_STATUS status = RawInitialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED)
        ReportStatus("initialize", nullptr, status, sourceFile, sourceLine);
    return status;
}

MH_STATUS WINAPI R1D_MH_Uninitialize(const char* sourceFile, int sourceLine)
{
    const MH_STATUS status = RawUninitialize();
    if (status == MH_OK)
        ClearTrackedHooks();
    else if (status != MH_ERROR_NOT_INITIALIZED)
        ReportStatus("uninitialize", nullptr, status, sourceFile, sourceLine);
    return status;
}

MH_STATUS WINAPI R1D_MH_CreateHook(
    LPVOID target,
    LPVOID detour,
    LPVOID* original,
    const char* sourceFile,
    int sourceLine)
{
    const MH_STATUS status = RawCreateHook(target, detour, original);
    if (status == MH_OK || status == MH_ERROR_ALREADY_CREATED)
        TrackHook(target, sourceFile, sourceLine);
    else
        ReportStatus("create failed", target, status, sourceFile, sourceLine);
    return status;
}

MH_STATUS WINAPI R1D_MH_RemoveHook(
    LPVOID target,
    const char* sourceFile,
    int sourceLine)
{
    const MH_STATUS status = RawRemoveHook(target);
    if (status == MH_OK)
        UntrackHook(target);
    else if (status != MH_ERROR_NOT_CREATED)
        ReportStatus("remove failed", target, status, sourceFile, sourceLine);
    return status;
}

MH_STATUS WINAPI R1D_MH_EnableHook(
    LPVOID target,
    const char* sourceFile,
    int sourceLine)
{
    const MH_STATUS status = RawEnableHook(target);
    if (target != MH_ALL_HOOKS)
    {
        if (!IsEnableSuccess(status))
            ReportStatus("enable failed", target, status, sourceFile, sourceLine);
        return status;
    }

    if (status == MH_OK)
        return status;

    ReportStatus(
        "bulk enable incomplete",
        target,
        status,
        sourceFile,
        sourceLine,
        "probing every successfully created hook");

    TrackedHook hooks[kMaxTrackedHooks]{};
    const size_t hookCount = SnapshotTrackedHooks(hooks, kMaxTrackedHooks);
    MH_STATUS recoveryStatus = MH_OK;
    size_t recoveredCount = 0;

    for (size_t i = 0; i < hookCount; ++i)
    {
        const MH_STATUS targetStatus = RawEnableHook(hooks[i].target);
        if (targetStatus == MH_OK)
        {
            ++recoveredCount;
            continue;
        }

        if (targetStatus != MH_ERROR_ENABLED)
        {
            recoveryStatus = targetStatus;
            ReportStatus(
                "bulk recovery failed",
                hooks[i].target,
                targetStatus,
                hooks[i].sourceFile,
                hooks[i].sourceLine);
        }
    }

    if (recoveryStatus == MH_OK)
    {
        char detail[96]{};
        std::snprintf(
            detail,
            sizeof(detail),
            "recovered=%zu tracked=%zu",
            recoveredCount,
            hookCount);
        ReportStatus(
            "bulk enable recovered",
            target,
            MH_OK,
            sourceFile,
            sourceLine,
            detail);
    }

    return recoveryStatus;
}

MH_STATUS WINAPI R1D_MH_DisableHook(
    LPVOID target,
    const char* sourceFile,
    int sourceLine)
{
    const MH_STATUS status = RawDisableHook(target);
    if (status != MH_OK && status != MH_ERROR_DISABLED)
        ReportStatus("disable failed", target, status, sourceFile, sourceLine);
    return status;
}

MH_STATUS WINAPI R1D_MH_QueueEnableHook(
    LPVOID target,
    const char* sourceFile,
    int sourceLine)
{
    const MH_STATUS status = RawQueueEnableHook(target);
    if (status != MH_OK)
        ReportStatus("queue enable failed", target, status, sourceFile, sourceLine);
    return status;
}

MH_STATUS WINAPI R1D_MH_QueueDisableHook(
    LPVOID target,
    const char* sourceFile,
    int sourceLine)
{
    const MH_STATUS status = RawQueueDisableHook(target);
    if (status != MH_OK)
        ReportStatus("queue disable failed", target, status, sourceFile, sourceLine);
    return status;
}

MH_STATUS WINAPI R1D_MH_ApplyQueued(const char* sourceFile, int sourceLine)
{
    const MH_STATUS status = RawApplyQueued();
    if (status != MH_OK)
        ReportStatus("apply queued failed", MH_ALL_HOOKS, status, sourceFile, sourceLine);
    return status;
}
