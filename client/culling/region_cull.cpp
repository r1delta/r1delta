#include "region_cull.h"
#include "region_cull_core.h"
#include "load.h"
#include "cvar.h"
#include "logging.h"

#include <MinHook.h>
#include <Windows.h>
#include <intrin.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

// The client runs this task on its worker threads, so everything on the hot path stays
// allocation-free and lock-free: convars are read live, the verify scratch buffer is
// allocated once per worker, and counters are plain atomics.

namespace {

using namespace region_cull;

// Retail client.dll (13,346,816 bytes, sha256 5b3f0080...7c9c) addresses.
constexpr std::uintptr_t kRegionCullRva              = 0x2319F0;
constexpr std::uintptr_t kNodesPointerRva            = 0x0F95168;
constexpr std::uintptr_t kItemBoundsPointerRva       = 0x0F95190;
constexpr std::uintptr_t kRegionArenaRva             = 0x12DCF40;
constexpr std::uintptr_t kTasksRva                   = 0x131CE80;
constexpr std::uintptr_t kWorkerOutputsRva           = 0x1324E80;
constexpr std::uintptr_t kSilhouetteCodesRva         = 0x0869300;
constexpr std::uintptr_t kEdgeEventsRva              = 0x0869400;
constexpr std::uintptr_t kAbsoluteValueMaskRva       = 0x097FB60;
constexpr std::uintptr_t kCornerSelectMasksRva       = 0x097FB80;
constexpr std::uintptr_t kXyzMaskRva                 = 0x097FC90;
constexpr std::uintptr_t kViewOriginRva              = 0x1158500;
constexpr std::uintptr_t kProjectionNormalRva        = 0x1158530;
constexpr std::uintptr_t kSidePlanesRva              = 0x1158540;
constexpr std::uintptr_t kSidePlaneCountRva          = 0x11585C0;
constexpr std::uintptr_t kIndividualTestThresholdRva = 0x1158610;
constexpr std::uintptr_t kTaskSplitThresholdRva      = 0x1158614;
constexpr std::uintptr_t kRegionEdgePlanesRva        = 0x125CEA0;

// vstdlib imports the original calls through its own IAT; reading those slots keeps the
// reimplementation bound to exactly the scheduler entry points the original used.
constexpr std::uintptr_t kIatTryDoTaskItemRva          = 0x7DA708;
constexpr std::uintptr_t kIatDefaultAbortConditionRva  = 0x7DA710;
constexpr std::uintptr_t kIatAppendTaskItemsEndRva     = 0x7DA738;
constexpr std::uintptr_t kIatAppendTaskItemsBeginRva   = 0x7DA740;

// Build guard: the hook is only valid for the exact module build it was recovered from.
constexpr std::uint8_t kRegionCullSignature[48] = {
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18,
    0x48, 0x89, 0x7C, 0x24, 0x20, 0x89, 0x4C, 0x24, 0x08, 0x55,
    0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0xB8, 0x70,
    0x13, 0x00, 0x00, 0xE8, 0x2A, 0xF6, 0x50, 0x00, 0x48, 0x2B,
    0xE0, 0x0F, 0x29, 0xB4, 0x24, 0x60, 0x13, 0x00,
};

// The prologue check above only proves the function, not the data the reimplementation reads.
// A different client.dll can keep those bytes and move a table or an IAT slot, so the tables
// are hash-checked against the retail dumps (same FNV-1a 64 the differential tests assert) and
// the scheduler entry points are checked to land inside a loaded module. Any mismatch leaves the
// original code in place instead of hooking a build we do not know.
std::uint64_t Fnv1a64(const void* data, std::size_t size)
{
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::uint64_t hash = 14695981039346656037ull;
    for (std::size_t i = 0; i != size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

bool MatchesRetailTables(const CullEnv& env)
{
    struct TableCheck {
        const void* data;
        std::size_t size;
        std::uint64_t expected;
        const char* name;
    };

    const TableCheck checks[] = {
        { env.silhouetteCodes, sizeof(std::uint32_t) * 64, 0x76AF6FB5AB7A8D2Cull, "silhouette codes" },
        { env.edgeEvents, sizeof(std::uint32_t) * 16, 0x2C5382FDA7DA7FA3ull, "edge events" },
        { env.cornerSelectMasks, sizeof(Vec4) * 16, 0xF9C15F569303D903ull, "corner masks" },
        { env.absoluteValueMask, sizeof(Vec4), 0x456057DC8994CE33ull, "absolute value mask" },
        { env.xyzMask, sizeof(Vec4), 0x085110BAC1E1E4D7ull, "xyz mask" },
    };

    for (const TableCheck& check : checks) {
        if (Fnv1a64(check.data, check.size) != check.expected) {
            Warning("R1Delta cull: %s table does not match the retail client.dll; skipping hook\n", check.name);
            return false;
        }
    }
    return true;
}

// The scheduler entry points were read out of four vstdlib IAT slots; a build where those slots
// hold something else would otherwise be hooked with non-null garbage.
bool PointsIntoModule(const void* pointer, const char* moduleName)
{
    if (!pointer)
        return false;

    const HMODULE module = GetModuleHandleA(moduleName);
    if (!module)
        return false;

    MODULEINFO info = {};
    if (!GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info)))
        return false;

    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    const auto base = reinterpret_cast<std::uintptr_t>(info.lpBaseOfDll);
    return address >= base && address < base + info.SizeOfImage;
}

constexpr std::uint32_t kMaxTrackedWorkers = 64;

std::uintptr_t s_client = 0;
bool s_installed = false;
using RegionCullFn = std::int64_t(__fastcall*)(std::uint32_t workerIndex, std::uint32_t taskIndex);
RegionCullFn s_original = nullptr;

ConVarR1* s_optimize = nullptr;
ConVarR1* s_verify = nullptr;

// Task counters are per worker and cache-line padded: a relaxed fetch_add on one shared
// counter was real overhead for tasks the traversal rejects immediately, and relaxed ordering
// does not make a shared atomic update independent across workers. The status command sums
// these slots.
struct alignas(64) PaddedCounter {
    std::atomic<std::uint64_t> value{ 0 };
};
PaddedCounter s_workerTasks[kMaxTrackedWorkers];
PaddedCounter s_workerVerifyTasks[kMaxTrackedWorkers];
PaddedCounter s_workerVerifySkips[kMaxTrackedWorkers];

std::uint64_t SumCounters(const PaddedCounter* counters)
{
    std::uint64_t total = 0;
    for (std::uint32_t i = 0; i != kMaxTrackedWorkers; ++i)
        total += counters[i].value.load(std::memory_order_relaxed);
    return total;
}

std::atomic<std::uint64_t> s_verifyMismatches{ 0 };
// Pairwise cycle counts of the original and the reimplementation on the same tasks.
// Only accumulated in verification mode, which is why the atomics cannot distort it.
std::atomic<std::uint64_t> s_originalCycles{ 0 };
std::atomic<std::uint64_t> s_optimizedCycles{ 0 };
std::atomic<bool> s_reportedFirstOptimized{ false };
std::atomic<bool> s_reportedFirstVerified{ false };

struct MismatchRecord {
    std::uint32_t workerIndex;
    std::uint32_t taskIndex;
    std::uint32_t rangeIndex;
    std::uint32_t expectedCount;
    std::uint32_t actualCount;
    std::uint16_t expectedFirst;
    std::uint16_t expectedItems;
    std::uint16_t actualFirst;
    std::uint16_t actualItems;
};

MismatchRecord s_firstMismatch{};

// Publication states for s_firstMismatch. A bool flag set before the record was written let a
// concurrent status command read a half-built record; claim the slot, write it, then publish
// with a release store and read it only after an acquire load observes Ready.
enum MismatchState : std::uint8_t {
    kMismatchNone = 0,
    kMismatchWriting = 1,
    kMismatchReady = 2,
};
std::atomic<std::uint8_t> s_mismatchState{ kMismatchNone };

std::atomic<std::uint8_t*> s_scratch[kMaxTrackedWorkers];

// Resolved once when the hook is installed: the module-relative addresses and the four
// scheduler IAT slots used to be rebuilt for every task. The fields stay pointers into the
// client module, so the scene's live data (node/item arrays, view origin, planes, task slots)
// keeps being read through them; only the address arithmetic left the task path.
CullEnv s_env{};
bool s_envResolved = false;

int ConvarInt(const ConVarR1* convar, int fallback)
{
    return convar ? convar->m_Value.m_nValue : fallback;
}

std::uint8_t* ScratchFor(std::uint32_t workerIndex)
{
    if (workerIndex >= kMaxTrackedWorkers)
        return nullptr;

    std::uint8_t* existing = s_scratch[workerIndex].load(std::memory_order_acquire);
    if (existing)
        return existing;

    auto* fresh = static_cast<std::uint8_t*>(
        VirtualAlloc(nullptr, kWorkerOutputStride, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!fresh)
        return nullptr;

    std::uint8_t* expected = nullptr;
    if (!s_scratch[workerIndex].compare_exchange_strong(expected, fresh, std::memory_order_acq_rel)) {
        VirtualFree(fresh, 0, MEM_RELEASE);
        return expected;
    }
    return fresh;
}

template <class T>
T ReadPointer(std::uintptr_t address)
{
    T value = nullptr;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    return value;
}

CullEnv ResolveEnv(std::uintptr_t client)
{
    CullEnv env;
    env.nodes = reinterpret_cast<const HierarchyNode* const*>(client + kNodesPointerRva);
    env.items = reinterpret_cast<const ItemBounds* const*>(client + kItemBoundsPointerRva);
    env.regionArena = reinterpret_cast<const std::uint8_t*>(client + kRegionArenaRva);
    env.tasks = reinterpret_cast<CullTask*>(client + kTasksRva);
    env.workerOutputs = reinterpret_cast<std::uint8_t*>(client + kWorkerOutputsRva);
    env.silhouetteCodes = reinterpret_cast<const std::uint32_t*>(client + kSilhouetteCodesRva);
    env.edgeEvents = reinterpret_cast<const std::uint32_t*>(client + kEdgeEventsRva);
    env.xyzMask = reinterpret_cast<const Vec4*>(client + kXyzMaskRva);
    env.absoluteValueMask = reinterpret_cast<const Vec4*>(client + kAbsoluteValueMaskRva);
    env.cornerSelectMasks = reinterpret_cast<const Vec4*>(client + kCornerSelectMasksRva);
    env.viewOrigin = reinterpret_cast<const Vec4*>(client + kViewOriginRva);
    env.projectionNormal = reinterpret_cast<const Vec4*>(client + kProjectionNormalRva);
    env.sidePlanes = reinterpret_cast<const Vec4*>(client + kSidePlanesRva);
    env.sidePlaneCount = reinterpret_cast<const std::uint32_t*>(client + kSidePlaneCountRva);
    env.individualTestThreshold = reinterpret_cast<const std::uint32_t*>(client + kIndividualTestThresholdRva);
    env.taskSplitThreshold = reinterpret_cast<const std::uint32_t*>(client + kTaskSplitThresholdRva);
    env.regionEdgePlanes = reinterpret_cast<const Vec4*>(client + kRegionEdgePlanesRva);

    env.scheduler.slots = env.tasks;
    env.scheduler.appendBegin = ReadPointer<std::int32_t(*)(std::uint32_t)>(client + kIatAppendTaskItemsBeginRva);
    env.scheduler.appendEnd = ReadPointer<void(*)(std::uint32_t, std::uint32_t)>(client + kIatAppendTaskItemsEndRva);
    env.scheduler.tryDoTaskItem = ReadPointer<std::int64_t(*)(std::uint32_t, std::uint32_t, void(*)())>(client + kIatTryDoTaskItemRva);
    env.scheduler.abortCondition = ReadPointer<void(*)()>(client + kIatDefaultAbortConditionRva);
    return env;
}

// Verifies the reimplementation against the untouched original on live tasks: the original
// runs first and owns every real effect, the reimplementation is compared on a scratch
// output with scheduler commits disabled.
bool VerifyAgainstOriginal(const CullEnv& env, std::uint32_t workerIndex, std::uint32_t taskIndex,
                           const CullTask& taskSnapshot,
                           const std::uint32_t beforeCount, const std::uint32_t afterCount,
                           std::uint64_t originalCycles)
{
    std::uint8_t* scratch = ScratchFor(workerIndex);
    if (!scratch) {
        // A skipped comparison must be visible: the caller's counters would otherwise show
        // fewer verifyTasks than tasks and read as "everything matched".
        if (workerIndex < kMaxTrackedWorkers)
            s_workerVerifySkips[workerIndex].value.fetch_add(1, std::memory_order_relaxed);
        std::uint64_t skipped = 0;
        for (std::uint32_t i = 0; i != kMaxTrackedWorkers; ++i)
            skipped += s_workerVerifySkips[i].value.load(std::memory_order_relaxed);
        if (skipped == 1)
            Warning("R1Delta cull: verification cannot allocate scratch for worker %u; "
                    "comparisons are being skipped (worker %u task %u)\n",
                    workerIndex, workerIndex, taskIndex);
        return true;
    }

    CullEnv scratchEnv = env;
    // The scratch buffer is worker-indexed like the live one; compute the base with
    // integer math instead of forming a pointer outside the allocation.
    scratchEnv.workerOutputs = reinterpret_cast<std::uint8_t*>(
        reinterpret_cast<std::uintptr_t>(scratch)
        - static_cast<std::size_t>(static_cast<std::int32_t>(workerIndex)) * kWorkerOutputStride);
    *reinterpret_cast<std::uint32_t*>(scratch + kWorkerOutputCountOffset) = 0;
    unsigned int timestampAux = 0;
    const std::uint64_t optimizedStart = __rdtscp(&timestampAux);
    ProcessRegionCullTaskFromTask(scratchEnv, workerIndex, taskIndex, false, taskSnapshot);
    const std::uint64_t optimizedCycles = __rdtscp(&timestampAux) - optimizedStart;
    s_originalCycles.fetch_add(originalCycles, std::memory_order_relaxed);
    s_optimizedCycles.fetch_add(optimizedCycles, std::memory_order_relaxed);
    const std::uint32_t actualCount = *reinterpret_cast<const std::uint32_t*>(scratch + kWorkerOutputCountOffset);

    const Range16* const expected = reinterpret_cast<const Range16*>(
        env.workerOutputs + (std::size_t)static_cast<std::int32_t>(workerIndex) * kWorkerOutputStride);
    const auto* actual = reinterpret_cast<const Range16*>(scratch);

    const std::uint32_t expectedRanges = afterCount - beforeCount;
    if (workerIndex < kMaxTrackedWorkers)
        s_workerVerifyTasks[workerIndex].value.fetch_add(1, std::memory_order_relaxed);
    if (!s_reportedFirstVerified.load(std::memory_order_relaxed) &&
        !s_reportedFirstVerified.exchange(true, std::memory_order_acq_rel)) {
        Msg("R1Delta cull: verification active; first task compared (worker %u, task %u, %u ranges)\n",
            workerIndex, taskIndex, expectedRanges);
    }

    bool mismatch = expectedRanges != actualCount;
    std::uint32_t mismatchIndex = mismatch ? 0 : expectedRanges;
    if (!mismatch) {
        for (std::uint32_t i = 0; i != expectedRanges; ++i) {
            const Range16 e = expected[beforeCount + i];
            const Range16 a = actual[i];
            if (e.first != a.first || e.count != a.count) {
                mismatch = true;
                mismatchIndex = i;
                break;
            }
        }
    }

    if (!mismatch)
        return true;

    ++s_verifyMismatches;

    std::uint8_t expectedState = kMismatchNone;
    if (s_mismatchState.compare_exchange_strong(expectedState, kMismatchWriting,
            std::memory_order_acq_rel, std::memory_order_relaxed)) {
        MismatchRecord record{};
        record.workerIndex = workerIndex;
        record.taskIndex = taskIndex;
        record.rangeIndex = mismatchIndex;
        record.expectedCount = expectedRanges;
        record.actualCount = actualCount;
        if (mismatchIndex < expectedRanges)
            record.expectedFirst = expected[beforeCount + mismatchIndex].first,
            record.expectedItems = expected[beforeCount + mismatchIndex].count;
        if (mismatchIndex < actualCount)
            record.actualFirst = actual[mismatchIndex].first,
            record.actualItems = actual[mismatchIndex].count;
        s_firstMismatch = record;
        // Release: the record above is what the state publishes.
        s_mismatchState.store(kMismatchReady, std::memory_order_release);
        Warning("R1Delta cull: optimized traversal result differs from the original "
                "(worker %u task %u range %u): original %u ranges, optimized %u\n",
                workerIndex, taskIndex, mismatchIndex, expectedRanges, actualCount);
    }
    return false;
}

std::int64_t __fastcall RegionCullDetour(std::uint32_t workerIndex, std::uint32_t taskIndex)
{
    if (!s_original)
        return 0;

    // The hook and the validated environment are installed together, but if a path ever leaves
    // the hook in place without an environment, the original code must still run: returning 0
    // would silently skip cull work for that task.
    if (!s_envResolved)
        return s_original(workerIndex, taskIndex);

    if (ConvarInt(s_optimize, 1) == 0)
        return s_original(workerIndex, taskIndex);

    const CullEnv& env = s_env;

    if (ConvarInt(s_verify, 0) == 0) {
        if (workerIndex < kMaxTrackedWorkers)
            s_workerTasks[workerIndex].value.fetch_add(1, std::memory_order_relaxed);
        if (!s_reportedFirstOptimized.load(std::memory_order_relaxed) &&
            !s_reportedFirstOptimized.exchange(true, std::memory_order_acq_rel)) {
            Msg("R1Delta cull: optimized traversal serving cull tasks (first task worker %u index %u)\n",
                workerIndex, taskIndex);
        }
        return ProcessRegionCullTask(env, workerIndex, taskIndex, true);
    }

    // The shadow must work from the task as it stood before the original ran: the scheduler's
    // append calls can reuse this very slot for a child, and re-reading it afterwards is how a
    // verifier reports mismatches that are not there.
    const CullTask taskSnapshot = env.tasks[taskIndex];

    auto* const countPtr = reinterpret_cast<std::uint32_t*>(
        env.workerOutputs + (std::size_t)static_cast<std::int32_t>(workerIndex) * kWorkerOutputStride
        + kWorkerOutputCountOffset);
    const std::uint32_t beforeCount = *countPtr;
    unsigned int timestampAux = 0;
    const std::uint64_t originalStart = __rdtscp(&timestampAux);
    const std::int64_t result = s_original(workerIndex, taskIndex);
    const std::uint64_t originalCycles = __rdtscp(&timestampAux) - originalStart;
    const std::uint32_t afterCount = *countPtr;
    if (workerIndex < kMaxTrackedWorkers)
        s_workerTasks[workerIndex].value.fetch_add(1, std::memory_order_relaxed);
    VerifyAgainstOriginal(env, workerIndex, taskIndex, taskSnapshot, beforeCount, afterCount, originalCycles);
    return result;
}

void CullStatusCommand(const CCommand&)
{
    Msg("%s\n", GetRegionCullStatusLine().c_str());
    const std::uint64_t verifyTasksDone = SumCounters(s_workerVerifyTasks);
    if (verifyTasksDone != 0) {
        const std::uint64_t originalCycles = s_originalCycles.load();
        const std::uint64_t optimizedCycles = s_optimizedCycles.load();
        // Scope warning: the original is timed with its scheduler commits and completion calls,
        // the shadow runs with commits disabled, and the shadow always runs second (warm
        // cache), so this ratio is not an equivalent-work speedup.
        Msg("R1Delta cull: pairwise cycles original=%llu optimized=%llu perTaskOriginal=%llu perTaskOptimized=%llu ratio=%.3f (not scheduler-equivalent; original includes commits, shadow runs second)\n",
            static_cast<unsigned long long>(originalCycles),
            static_cast<unsigned long long>(optimizedCycles),
            static_cast<unsigned long long>(originalCycles / verifyTasksDone),
            static_cast<unsigned long long>(optimizedCycles / verifyTasksDone),
            originalCycles ? static_cast<double>(optimizedCycles) / static_cast<double>(originalCycles) : 0.0);
    }
    if (s_mismatchState.load(std::memory_order_acquire) == kMismatchReady) {
        const MismatchRecord& record = s_firstMismatch;
        Msg("R1Delta cull: first mismatch worker=%u task=%u range=%u originalRanges=%u optimizedRanges=%u "
            "originalFirst=%u originalItems=%u optimizedFirst=%u optimizedItems=%u\n",
            record.workerIndex, record.taskIndex, record.rangeIndex,
            record.expectedCount, record.actualCount,
            record.expectedFirst, record.expectedItems,
            record.actualFirst, record.actualItems);
    }
}

} // namespace

// Report and console surfaces live outside the anonymous namespace: the crash handler links
// against them, while everything on the hot path stays internal.
std::string GetRegionCullStatusLine()
{
    char line[256];
    sprintf_s(line, "R1Delta cull: optimize=%d verify=%d tasks=%llu verifyTasks=%llu verifySkipped=%llu mismatches=%llu",
        ConvarInt(s_optimize, 1), ConvarInt(s_verify, 0),
        static_cast<unsigned long long>(SumCounters(s_workerTasks)),
        static_cast<unsigned long long>(SumCounters(s_workerVerifyTasks)),
        static_cast<unsigned long long>(SumCounters(s_workerVerifySkips)),
        static_cast<unsigned long long>(s_verifyMismatches.load()));
    return line;
}

// Crash-report variant: counters only. The crash handler must not dereference the convar
// pointers, which are engine-owned memory, so the recorded convar values are left out.
std::string GetRegionCullCrashStateLine()
{
    char line[192];
    sprintf_s(line, "R1Delta cull counters: tasks=%llu verifyTasks=%llu verifySkipped=%llu mismatches=%llu",
        static_cast<unsigned long long>(SumCounters(s_workerTasks)),
        static_cast<unsigned long long>(SumCounters(s_workerVerifyTasks)),
        static_cast<unsigned long long>(SumCounters(s_workerVerifySkips)),
        static_cast<unsigned long long>(s_verifyMismatches.load()));
    return line;
}

void InitRegionCullHooks()
{
    if (s_installed)
        return;

    if (!G_client) {
        Warning("R1Delta cull: client module base is unavailable; skipping hook\n");
        return;
    }

    const auto* entry = reinterpret_cast<const std::uint8_t*>(G_client + kRegionCullRva);
    if (std::memcmp(entry, kRegionCullSignature, sizeof(kRegionCullSignature)) != 0) {
        Warning("R1Delta cull: client.dll region-cull build signature mismatch; skipping hook\n");
        return;
    }

    s_optimize = RegisterConVar("r1delta_cull_optimize", "1", FCVAR_ARCHIVE,
                                "Use the optimized region-cull traversal (0 = original client.dll code)");
    s_verify = RegisterConVar("r1delta_cull_verify", "0", FCVAR_NONE,
                              "Re-run each cull task through the original and compare results (diagnostic)");
    RegisterConCommand("r1delta_cull_status", CullStatusCommand,
                       "Print region-cull optimization counters and the first verification mismatch", FCVAR_NONE);

    // Everything is validated before the hook exists: a build we do not recognise leaves the
    // original code running instead of installing a detour that reads the wrong tables or IAT
    // slots, and a failure here never has to be undone.
    const CullEnv probe = ResolveEnv(G_client);
    if (!probe.scheduler.appendBegin || !probe.scheduler.appendEnd || !probe.scheduler.tryDoTaskItem
        || !probe.scheduler.abortCondition) {
        Warning("R1Delta cull: vstdlib scheduler entry points are missing; leaving the original in place\n");
        return;
    }
    if (!PointsIntoModule(reinterpret_cast<const void*>(probe.scheduler.appendBegin), "vstdlib.dll")
        || !PointsIntoModule(reinterpret_cast<const void*>(probe.scheduler.appendEnd), "vstdlib.dll")
        || !PointsIntoModule(reinterpret_cast<const void*>(probe.scheduler.tryDoTaskItem), "vstdlib.dll")
        || !PointsIntoModule(reinterpret_cast<const void*>(probe.scheduler.abortCondition), "vstdlib.dll")) {
        Warning("R1Delta cull: vstdlib scheduler entry points are not in vstdlib; leaving the original in place\n");
        return;
    }
    if (!MatchesRetailTables(probe))
        return;

    const MH_STATUS status = MH_CreateHook(reinterpret_cast<LPVOID>(G_client + kRegionCullRva),
                                           reinterpret_cast<LPVOID>(&RegionCullDetour),
                                           reinterpret_cast<LPVOID*>(&s_original));
    if (status != MH_OK && status != MH_ERROR_ALREADY_CREATED) {
        Warning("R1Delta cull: cannot hook client+0x%llX (%d)\n",
                static_cast<unsigned long long>(kRegionCullRva), static_cast<int>(status));
        return;
    }

    // Cache the resolved addresses now that they are known good. Their contents stay live:
    // the environment holds pointers into the client module, so map swaps and view updates are
    // still read per task. IAT rebinding after this point would need a rebuild, which no
    // supported path does while the hook is installed.
    s_env = probe;
    s_envResolved = true;

    MH_EnableHook(reinterpret_cast<LPVOID>(G_client + kRegionCullRva));
    s_client = G_client;
    s_installed = true;
    Msg("R1Delta cull: optimized region-cull task installed at client+0x%llX\n",
        static_cast<unsigned long long>(kRegionCullRva));
}
