// %*++***###*##**##++**+++*++*%%%%%%%+*%+#*+%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%#=%%%#**#+#%
// ==----------------------------------------------------------------------=================+
// =------------------------------------::----------------------------------===---==========+
// ---------------------------------:-:--::::-::::-------------------=======================+
// =-------------------------------::::::::-::::-:::----------==============+===+++=========+
// ----------------------------::::::--:---=====----------===========++==++++++++++++++++++++
// ----------------------------:-----:---==++++++====-==========++++++++++++++++++++++++++++*
// -------------------------------------=+++++++=============++++++++++++++++++++++++++++++**
// -------------------------------------=++++*+========++++++++++++++++++++++++++++++++++++**
// ----------------------------:::::::--=+++++=======+++++++++++++++++++++++************++++*
// ---------------------::::::::::::::::-==+++===++++++++++++++++++++++++********###%%%##*++*
// -------:::::::::::::::::::::::::::::::-=====+####**+++++++++++++++++*********#%%%@@@@%%#**
// ------:-:::::::::::::::::::::::::::::::-====*%%%%#*++++++++++++++++++********##%@@@@@%%#**
// ----------::::::::::::::::::::::::::-=--====+#%%%*++++++++++++++++++++*********##%%%%%#***
// -------------=*=-:::::::::::::::::-=++======++***+++++++++++++++++++**************###*****
// -------------=*#=-------======++++*###*+=+=++=++++++++++*+++******************************
// =-----=======+*#*+++++++*****##########+=++++++++++***************************************
// +++++++++++****#################*****#*+=+++++++++****************************************
// ++**+++++++++++++======+++++++++++++****+=+++***################**************************
// *****+=--------::-::::::::::::::::::------=*#%%%%%%%%%%%%%%%%%%%#####*********************
// ******=-----------:::::::::::---:::::::::-=#%%%%@@@@@@@@@@@@@@%%%%###********************#
// ******=---------------:::::::::::-:::::::-*%%%@@@@@@@@@@@@@@@@@%%%%##********************#
// ****#*=-----------------:::::::::::::::::-=*%%@@@@@@@@@@@@@@@@@@%%##*********************#
// ******+===-------------::::::::::::---:::--=*#%%%@@@@@@@@@@@@@%%######**#**************###
// ==++==------------------:::::::::::::-------=+**##%%%@%%%%%%%%##########*****************#
// ==--------------------------::-:::::::::::---=++**##%%%%%%%%%%%##########*************####
// =--------------------------------:---::::--:--==+**###%#%%%%%%%%%%%#####**************####
// ====--------------------------:-------::-------==+++****###########******************#####
// ===--==------------------------------------::---==+++++******************************#####
// ===-------------------------------------:::-:----=+++********************************####%
// =====---------------------------------------------=++++******************************####%
// ======------------------==------------------------==+++***************************######%%
// =========-----===--------==------------------------==++********#*#####**#######*########%%

#include "memory.h"
#include "cvar.h"
#include <iostream>
#include <Psapi.h>
#include <intrin.h>
#include <unordered_map>
#include <mutex>
#include <algorithm>
#include <atomic>

#pragma intrinsic(_ReturnAddress)

ConCommandR1* RegisterConCommand(const char* commandName, void (*callback)(const CCommand&), const char* helpString, int flags);

void DeltaMemoryStats(const CCommand& c)
{
    GlobalAllocator()->DumpStats();
}

namespace
{
constexpr size_t kAllocationTraceFrameCount = 8;
constexpr size_t kAllocationTraceBucketCount = 8192;
constexpr size_t kAllocationTraceReportStep = 32ull << 20;
constexpr size_t kAllocationTraceTopCount = 12;
constexpr size_t kAllocationTraceMinimumSize = 1ull << 20;

struct AllocationTraceBucket
{
    void* frames[kAllocationTraceFrameCount];
    uint64_t totalBytes;
    uint64_t allocationCount;
    size_t largestAllocation;
    void* largestAllocationPointer;
    uint8_t heap;
    uint8_t tag;
    bool occupied;
};

SRWLOCK s_allocationTraceLock = SRWLOCK_INIT;
SRWLOCK s_allocationTraceOutputLock = SRWLOCK_INIT;
AllocationTraceBucket s_allocationTraceBuckets[kAllocationTraceBucketCount] = {};
uint64_t s_allocationTraceHeapTotals[HEAP_COUNT] = {};
uint64_t s_allocationTraceNextReports[HEAP_COUNT] = {};
std::atomic<uint64_t> s_allocationTraceDroppedBuckets = 0;
thread_local bool s_insideAllocationTrace = false;
INIT_ONCE s_allocationTraceFileInit = INIT_ONCE_STATIC_INIT;
HANDLE s_allocationTraceFile = INVALID_HANDLE_VALUE;
char s_allocationTraceFilePath[MAX_PATH] = {};

BOOL CALLBACK InitializeAllocationTraceFile(
    PINIT_ONCE,
    PVOID,
    PVOID*)
{
    char executablePath[MAX_PATH] = {};
    const DWORD executablePathLength =
        GetModuleFileNameA(nullptr, executablePath, static_cast<DWORD>(std::size(executablePath)));
    char* const finalSlash = executablePathLength
        ? strrchr(executablePath, '\\')
        : nullptr;
    if (finalSlash)
    {
        *finalSlash = '\0';
        snprintf(
            s_allocationTraceFilePath,
            std::size(s_allocationTraceFilePath),
            "%s\\r1delta_alloc_trace_%lu.log",
            executablePath,
            static_cast<unsigned long>(GetCurrentProcessId()));
    }
    else
    {
        snprintf(
            s_allocationTraceFilePath,
            std::size(s_allocationTraceFilePath),
            "r1delta_alloc_trace_%lu.log",
            static_cast<unsigned long>(GetCurrentProcessId()));
    }
    s_allocationTraceFile = CreateFileA(
        s_allocationTraceFilePath,
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    return TRUE;
}

void EmitAllocationTraceLine(const char* line)
{
    if (!line)
        return;

    InitOnceExecuteOnce(
        &s_allocationTraceFileInit,
        InitializeAllocationTraceFile,
        nullptr,
        nullptr);

    AcquireSRWLockExclusive(&s_allocationTraceOutputLock);
    if (s_allocationTraceFile != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0;
        WriteFile(
            s_allocationTraceFile,
            line,
            static_cast<DWORD>(strlen(line)),
            &written,
            nullptr);
    }
    ReleaseSRWLockExclusive(&s_allocationTraceOutputLock);

    OutputDebugStringA(line);
}

void FlushAllocationTraceFile()
{
    AcquireSRWLockExclusive(&s_allocationTraceOutputLock);
    if (s_allocationTraceFile != INVALID_HANDLE_VALUE)
        FlushFileBuffers(s_allocationTraceFile);
    ReleaseSRWLockExclusive(&s_allocationTraceOutputLock);
}

bool IsAllocatorTraceEnabled()
{
    static const bool enabled = [] {
        const wchar_t* commandLine = GetCommandLineW();
        return commandLine &&
            wcsstr(commandLine, L"-r1delta_trace_allocations") != nullptr;
    }();
    return enabled;
}

uint64_t HashAllocationTrace(
    void* const* frames,
    EDeltaAllocTags tag,
    EDeltaAllocHeaps heap)
{
    uint64_t hash = 1469598103934665603ull;
    hash ^= static_cast<uint8_t>(heap);
    hash *= 1099511628211ull;
    hash ^= static_cast<uint8_t>(tag);
    hash *= 1099511628211ull;
    for (size_t i = 0; i < kAllocationTraceFrameCount; ++i)
    {
        const uintptr_t frame = reinterpret_cast<uintptr_t>(frames[i]);
        hash ^= frame;
        hash *= 1099511628211ull;
    }
    return hash;
}

bool AllocationTraceMatches(
    const AllocationTraceBucket& bucket,
    void* const* frames,
    EDeltaAllocTags tag,
    EDeltaAllocHeaps heap)
{
    if (!bucket.occupied ||
        bucket.heap != static_cast<uint8_t>(heap) ||
        bucket.tag != static_cast<uint8_t>(tag))
    {
        return false;
    }
    return memcmp(bucket.frames, frames, sizeof(bucket.frames)) == 0;
}

void CopyTopAllocationTraceBuckets(
    AllocationTraceBucket (&top)[kAllocationTraceTopCount])
{
    memset(top, 0, sizeof(top));
    for (const AllocationTraceBucket& candidate : s_allocationTraceBuckets)
    {
        if (!candidate.occupied || candidate.totalBytes <= top[kAllocationTraceTopCount - 1].totalBytes)
            continue;

        size_t insertAt = kAllocationTraceTopCount - 1;
        while (insertAt > 0 && candidate.totalBytes > top[insertAt - 1].totalBytes)
        {
            top[insertAt] = top[insertAt - 1];
            --insertAt;
        }
        top[insertAt] = candidate;
    }
}

void DescribeAllocationTraceFrame(void* frame, char* output, size_t outputSize)
{
    if (!output || !outputSize)
        return;

    output[0] = '\0';
    MEMORY_BASIC_INFORMATION memory = {};
    if (!frame || !VirtualQuery(frame, &memory, sizeof(memory)) || !memory.AllocationBase)
    {
        snprintf(output, outputSize, "%p", frame);
        return;
    }

    char modulePath[MAX_PATH] = {};
    const HMODULE module = static_cast<HMODULE>(memory.AllocationBase);
    if (!GetModuleFileNameA(module, modulePath, static_cast<DWORD>(std::size(modulePath))))
    {
        snprintf(output, outputSize, "%p", frame);
        return;
    }

    const char* moduleName = strrchr(modulePath, '\\');
    moduleName = moduleName ? moduleName + 1 : modulePath;
    snprintf(
        output,
        outputSize,
        "%s+0x%llx",
        moduleName,
        static_cast<unsigned long long>(
            reinterpret_cast<uintptr_t>(frame) -
            reinterpret_cast<uintptr_t>(memory.AllocationBase)));
}

void PrintAllocationTraceSnapshot(
    const char* reason,
    uint64_t gameTotal,
    uint64_t deltaTotal,
    const AllocationTraceBucket (&top)[kAllocationTraceTopCount])
{
    char line[1024] = {};
    snprintf(
        line,
        std::size(line),
        "[MEMTRACE] %s large>=1MiB cumulative game=%.2f MiB delta=%.2f MiB droppedBuckets=%llu\n",
        reason ? reason : "snapshot",
        static_cast<double>(gameTotal) / static_cast<double>(1ull << 20),
        static_cast<double>(deltaTotal) / static_cast<double>(1ull << 20),
        static_cast<unsigned long long>(s_allocationTraceDroppedBuckets.load(std::memory_order_relaxed)));
    EmitAllocationTraceLine(line);

    for (size_t i = 0; i < kAllocationTraceTopCount; ++i)
    {
        const AllocationTraceBucket& bucket = top[i];
        if (!bucket.occupied)
            break;

        snprintf(
            line,
            std::size(line),
            "[MEMTRACE] top[%zu] heap=%s tag=%s total=%.2f MiB count=%llu max=%.2f MiB ptr=%p\n",
            i,
            Mem_heap_to_cstring(static_cast<EDeltaAllocHeaps>(bucket.heap)),
            Mem_tag_to_cstring(static_cast<EDeltaAllocTags>(bucket.tag)),
            static_cast<double>(bucket.totalBytes) / static_cast<double>(1ull << 20),
            static_cast<unsigned long long>(bucket.allocationCount),
            static_cast<double>(bucket.largestAllocation) / static_cast<double>(1ull << 20),
            bucket.largestAllocationPointer);
        EmitAllocationTraceLine(line);

        for (size_t frameIndex = 0;
             frameIndex < kAllocationTraceFrameCount && bucket.frames[frameIndex];
             ++frameIndex)
        {
            char description[MAX_PATH + 64] = {};
            DescribeAllocationTraceFrame(
                bucket.frames[frameIndex],
                description,
                std::size(description));
            snprintf(
                line,
                std::size(line),
                "[MEMTRACE]   #%zu %s\n",
                frameIndex,
                description);
            EmitAllocationTraceLine(line);
        }
    }
    FlushAllocationTraceFile();
}
}

void TraceAllocatorHeapCreated(
    EDeltaAllocHeaps heap,
    HANDLE handle,
    size_t initialSize)
{
    if (!IsAllocatorTraceEnabled())
        return;

    char line[512] = {};
    snprintf(
        line,
        std::size(line),
        "[MEMTRACE] HeapCreate heap=%s handle=%p initial=%.2f MiB growable=1\n",
        Mem_heap_to_cstring(heap),
        handle,
        static_cast<double>(initialSize) / static_cast<double>(1ull << 20));
    EmitAllocationTraceLine(line);
    FlushAllocationTraceFile();
}

void TraceAllocatorAllocation(
    void* allocation,
    size_t size,
    EDeltaAllocTags tag,
    EDeltaAllocHeaps heap)
{
    if (!IsAllocatorTraceEnabled() ||
        size < kAllocationTraceMinimumSize ||
        heap >= HEAP_COUNT ||
        s_insideAllocationTrace)
    {
        return;
    }

    s_insideAllocationTrace = true;
    void* frames[kAllocationTraceFrameCount] = {};
    CaptureStackBackTrace(
        2,
        static_cast<DWORD>(kAllocationTraceFrameCount),
        frames,
        nullptr);

    bool shouldReport = false;
    uint64_t gameTotal = 0;
    uint64_t deltaTotal = 0;
    AllocationTraceBucket top[kAllocationTraceTopCount] = {};

    AcquireSRWLockExclusive(&s_allocationTraceLock);
    const uint64_t hash = HashAllocationTrace(frames, tag, heap);
    AllocationTraceBucket* bucket = nullptr;
    for (size_t probe = 0; probe < kAllocationTraceBucketCount; ++probe)
    {
        AllocationTraceBucket& candidate =
            s_allocationTraceBuckets[(hash + probe) % kAllocationTraceBucketCount];
        if (!candidate.occupied)
        {
            candidate.occupied = true;
            candidate.heap = static_cast<uint8_t>(heap);
            candidate.tag = static_cast<uint8_t>(tag);
            memcpy(candidate.frames, frames, sizeof(candidate.frames));
            bucket = &candidate;
            break;
        }
        if (AllocationTraceMatches(candidate, frames, tag, heap))
        {
            bucket = &candidate;
            break;
        }
    }

    if (bucket)
    {
        bucket->totalBytes += size;
        ++bucket->allocationCount;
        if (size > bucket->largestAllocation)
        {
            bucket->largestAllocation = size;
            bucket->largestAllocationPointer = allocation;
        }
    }
    else
    {
        s_allocationTraceDroppedBuckets.fetch_add(1, std::memory_order_relaxed);
    }

    s_allocationTraceHeapTotals[heap] += size;
    if (!s_allocationTraceNextReports[heap])
        s_allocationTraceNextReports[heap] = kAllocationTraceReportStep;
    if (s_allocationTraceHeapTotals[heap] >= s_allocationTraceNextReports[heap])
    {
        while (s_allocationTraceNextReports[heap] <= s_allocationTraceHeapTotals[heap])
            s_allocationTraceNextReports[heap] += kAllocationTraceReportStep;
        shouldReport = true;
        gameTotal = s_allocationTraceHeapTotals[HEAP_GAME];
        deltaTotal = s_allocationTraceHeapTotals[HEAP_DELTA];
        CopyTopAllocationTraceBuckets(top);
    }
    ReleaseSRWLockExclusive(&s_allocationTraceLock);

    if (shouldReport)
        PrintAllocationTraceSnapshot("threshold", gameTotal, deltaTotal, top);
    s_insideAllocationTrace = false;
}

void DumpAllocatorTraceStats()
{
    if (!IsAllocatorTraceEnabled() || s_insideAllocationTrace)
    {
        Warning("[MEMTRACE] tracing is disabled; launch with -r1delta_trace_allocations\n");
        return;
    }

    s_insideAllocationTrace = true;
    uint64_t gameTotal = 0;
    uint64_t deltaTotal = 0;
    AllocationTraceBucket top[kAllocationTraceTopCount] = {};
    AcquireSRWLockShared(&s_allocationTraceLock);
    gameTotal = s_allocationTraceHeapTotals[HEAP_GAME];
    deltaTotal = s_allocationTraceHeapTotals[HEAP_DELTA];
    CopyTopAllocationTraceBuckets(top);
    ReleaseSRWLockShared(&s_allocationTraceLock);
    PrintAllocationTraceSnapshot("manual", gameTotal, deltaTotal, top);
    s_insideAllocationTrace = false;
}

void DeltaMemoryTraceStats(const CCommand&)
{
    DumpAllocatorTraceStats();
}

IMemAlloc* g_pMemAllocSingleton = 0;

// The retail tier0 (tier0_orig.dll) ships Valve's original memalloc, whose
// deferred reuse is the behavior the game was built around. Forward the process
// allocator to it instead of running our own heap/quarantine layer, which
// changed allocation reuse and surfaced latent filesystem/material lifetime
// bugs as hard crashes. CMimMemAlloc remains as a fallback only.
static IMemAlloc* ResolveRetailGlobalMemAlloc()
{
    HMODULE retailTier0 = GetModuleHandleW(L"tier0_orig.dll");
    if (!retailTier0)
        return nullptr;

    using CreateGlobalMemAllocFn = IMemAlloc* (WINAPI*)();
    auto create = reinterpret_cast<CreateGlobalMemAllocFn>(
        GetProcAddress(retailTier0, "CreateGlobalMemAlloc"));
    return create ? create() : nullptr;
}

extern "C" __declspec(dllexport) IMemAlloc * CreateGlobalMemAlloc() {
    if (!g_pMemAllocSingleton) {
        IMemAlloc* const retail = ResolveRetailGlobalMemAlloc();
        if (retail) {
            g_pMemAllocSingleton = retail;
            static bool logged = false;
            if (!logged) {
                logged = true;
                char buffer[160];
                _snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
                    "R1Delta: allocator delegation -> retail tier0_orig singleton %p\n",
                    static_cast<void*>(retail));
                OutputDebugStringA(buffer);
            }
        } else {
            // NOTE(mrsteyk): if to move it out - it will break due to static initialisation order.
            static CMimMemAlloc mem;
            g_pMemAllocSingleton = &mem;
            static bool logged = false;
            if (!logged) {
                logged = true;
                OutputDebugStringA("R1Delta: allocator delegation FAILED - falling back to CMimMemAlloc\n");
            }
        }
    }
    return g_pMemAllocSingleton;
}

IMemAlloc* GlobalAllocator()
{
    return CreateGlobalMemAlloc();
}

struct MsvcAllocatorFallbackRecord
{
    uintptr_t moduleBase;
    uintptr_t moduleEnd;
    MsvcCallocBaseFn callocFn;
    MsvcMallocBaseFn mallocFn;
    MsvcReallocBaseFn reallocFn;
    MsvcRecallocBaseFn recallocFn;
    MsvcFreeBaseFn freeFn;
    char moduleName[64];
};

static SRWLOCK s_msvcAllocatorFallbackLock = SRWLOCK_INIT;
static MsvcAllocatorFallbackRecord s_msvcAllocatorFallbacks[64] = {};

static bool FindMsvcAllocatorFallback(void* returnAddress, MsvcAllocatorFallbackRecord* out)
{
    const uintptr_t caller = reinterpret_cast<uintptr_t>(returnAddress);
    if (!caller || !out)
        return false;

    AcquireSRWLockShared(&s_msvcAllocatorFallbackLock);
    for (const MsvcAllocatorFallbackRecord& record : s_msvcAllocatorFallbacks)
    {
        if (record.moduleBase && caller >= record.moduleBase && caller < record.moduleEnd)
        {
            *out = record;
            ReleaseSRWLockShared(&s_msvcAllocatorFallbackLock);
            return true;
        }
    }
    ReleaseSRWLockShared(&s_msvcAllocatorFallbackLock);
    return false;
}

void RegisterMsvcAllocatorFallbacks(
    uintptr_t moduleBase,
    size_t moduleSize,
    const char* moduleName,
    MsvcCallocBaseFn callocFn,
    MsvcMallocBaseFn mallocFn,
    MsvcReallocBaseFn reallocFn,
    MsvcRecallocBaseFn recallocFn,
    MsvcFreeBaseFn freeFn)
{
    if (!moduleBase || !moduleSize)
        return;

    AcquireSRWLockExclusive(&s_msvcAllocatorFallbackLock);
    MsvcAllocatorFallbackRecord* slot = nullptr;
    for (MsvcAllocatorFallbackRecord& record : s_msvcAllocatorFallbacks)
    {
        if (record.moduleBase == moduleBase)
        {
            slot = &record;
            break;
        }
        if (!record.moduleBase && !slot)
        {
            slot = &record;
        }
    }

    if (slot)
    {
        slot->moduleBase = moduleBase;
        slot->moduleEnd = moduleBase + moduleSize;
        slot->callocFn = callocFn;
        slot->mallocFn = mallocFn;
        slot->reallocFn = reallocFn;
        slot->recallocFn = recallocFn;
        slot->freeFn = freeFn;
        slot->moduleName[0] = 0;
        if (moduleName)
            strncpy_s(slot->moduleName, moduleName, _TRUNCATE);
    }
    ReleaseSRWLockExclusive(&s_msvcAllocatorFallbackLock);
}

void* __cdecl hkcalloc_base(size_t Count, size_t Size)
{
    if (Size && Count > SIZE_MAX / Size)
        return nullptr;

    size_t nTotal = Count * Size;
    if (nTotal == 0) nTotal = 1;
    void* const pNew = CreateGlobalMemAlloc()->Alloc_Aligned(nTotal, alignof(std::max_align_t) * 2);
    if (pNew) {
        memset(pNew, 0, nTotal);
    }
    return pNew;
}

void* __cdecl hkmalloc_base(size_t Size)
{
    if (Size == 0) Size = 1;
    return CreateGlobalMemAlloc()->Alloc_Aligned(Size, alignof(std::max_align_t) * 2);
}

void* __cdecl hkrealloc_base(void* Block, size_t Size)
{
    if (!Block)
        return hkmalloc_base(Size);

    return CreateGlobalMemAlloc()->Realloc_Aligned(Block, Size, alignof(std::max_align_t) * 2);
}

void __cdecl hkfree_base(void* Block)
{
    if (!Block)
        return;

    CreateGlobalMemAlloc()->Free_Aligned(Block, alignof(std::max_align_t) * 2);
}

void* __cdecl hkrecalloc_base(void* Block, size_t Count, size_t Size)
{
    if (Size && Count > SIZE_MAX / Size)
        return nullptr;

    size_t const nTotal = Count * Size;
    void* const pMemOut = CreateGlobalMemAlloc()->Realloc_Aligned(Block, nTotal, alignof(std::max_align_t) * 2);
    if (pMemOut && !Block) {
        memset(pMemOut, 0, nTotal);
    }
    return pMemOut;
}

char* DuplicateDelegatedString(const char* value)
{
    const char* source = value ? value : "";
    const size_t size = strlen(source) + 1;
    void* memory = hkmalloc_base(size);
    if (!memory)
        throw std::bad_alloc();
    memcpy(memory, source, size);
    return static_cast<char*>(memory);
}

void FreeDelegatedString(void* p)
{
    hkfree_base(p);
}
