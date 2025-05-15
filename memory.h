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

#pragma once

#include "core.h"

#include "framework.h"
#include <concurrent_unordered_map.h>
#include <MinHook.h>
#include <array>
// NOTE(mrsteyk): cyclic includes
extern "C" __declspec(dllimport) void Warning(const char* _Printf_format_string_ pMsg, ...);
//#define ASAN_ENABLED 1
// NOTE(mrsteyk): for heap check it's also advised to disable new/delete overrides. It also might not be really necessary as corruption detection can easilly see it.
#define HEAP_CHECK_ENABLED 0

#define MI_MALLOC_VERSION 228
#define mi_collect(...) for (size_t i = 0; i <(size_t)HEAP_COUNT; ++i) HeapCompact(this->_heaps[i], 0)
#define mi_stats_print(...)
#define mi_process_info(...)

#include <Windows.h>
#include <Psapi.h>
typedef size_t(*MemAllocFailHandler_t)(size_t); // should return 0, first argument is size of failed allocation
// mem stats are SBH_cur, SBH_max, MBH_cur, MBH_max, LBH_cur, LBH_max, LBH_arena, LBH_free, mspace_cur, mspace_max, mspace_size, sys_heap, VMM_reserved, VMM_reserved_max, VMM_committed, VMM_committed_max, MemStacks, phys_total, phys_free, phys_free_min
// sbh = small block heap (if applicable)
// mbh = medium block heap (if applicable)
// lbh = large block heap (if applicable)
struct GenericMemoryStat_t 
{
	const char* name;
	int value;
};

class IMemAlloc
{
public:
	// Release versions
	virtual void* Alloc(size_t nSize) = 0;
public:
	virtual void* Realloc(void* pMem, size_t nSize) = 0;

	virtual void Free(void* pMem) = 0;
	virtual void* Expand_NoLongerSupported(void* pMem, size_t nSize) = 0;

	// Debug versions
	virtual void* Alloc(size_t nSize, const char* pFileName, int nLine) = 0;
public:
	virtual void* Realloc(void* pMem, size_t nSize, const char* pFileName, int nLine) = 0;
	virtual void  Free(void* pMem, const char* pFileName, int nLine) = 0;
	virtual void* Expand_NoLongerSupported(void* pMem, size_t nSize, const char* pFileName, int nLine) = 0;

	inline void* IndirectAlloc(size_t nSize) { return Alloc(nSize); }
	inline void* IndirectAlloc(size_t nSize, const char* pFileName, int nLine) { return Alloc(nSize, pFileName, nLine); }

	// Returns the size of a particular allocation (NOTE: may be larger than the size requested!)
	virtual size_t GetSize(void* pMem) = 0;

	// Force file + line information for an allocation
	virtual void PushAllocDbgInfo(const char* pFileName, int nLine) = 0;
	virtual void PopAllocDbgInfo() = 0;

	// FIXME: Remove when we have our own allocator
	// these methods of the Crt debug code is used in our codebase currently
	virtual int32_t CrtSetBreakAlloc(int32_t lNewBreakAlloc) = 0;
	virtual	int CrtSetReportMode(int nReportType, int nReportMode) = 0;
	virtual int CrtIsValidHeapPointer(const void* pMem) = 0;
	virtual int CrtIsValidPointer(const void* pMem, unsigned int size, int access) = 0;
	virtual int CrtCheckMemory(void) = 0;
	virtual int CrtSetDbgFlag(int nNewFlag) = 0;
	virtual void CrtMemCheckpoint(_CrtMemState* pState) = 0;

	virtual void DumpStats() = 0;
	virtual void DumpStatsFileBase(char const* pchFileBase) = 0;
	virtual size_t ComputeMemoryUsedBy(char const* pchSubStr) = 0;

	virtual void* CrtSetReportFile(int nRptType, void* hFile) = 0;
	virtual void* CrtSetReportHook(void* pfnNewHook) = 0;
	virtual int CrtDbgReport(int nRptType, const char* szFile,
			int nLine, const char* szModule, const char* pMsg) = 0;

	virtual int heapchk() = 0;

	virtual bool IsDebugHeap() = 0;

	virtual void GetActualDbgInfo(const char*& pFileName, int& nLine) = 0;
	virtual void RegisterAllocation(const char* pFileName, int nLine, size_t nLogicalSize, size_t nActualSize, unsigned nTime) = 0;
	virtual void RegisterDeallocation(const char* pFileName, int nLine, size_t nLogicalSize, size_t nActualSize, unsigned nTime) = 0;

	virtual int GetVersion() = 0;

	virtual void CompactHeap() = 0;

	// Function called when malloc fails or memory limits hit to attempt to free up memory (can come in any thread)
	virtual MemAllocFailHandler_t SetAllocFailHandler(MemAllocFailHandler_t pfnMemAllocFailHandler) = 0;

	virtual void DumpBlockStats(void*) = 0;

	virtual void SetStatsExtraInfo(const char* pMapName, const char* pComment) = 0;

	// Returns 0 if no failure, otherwise the size_t of the last requested chunk
	virtual size_t MemoryAllocFailed() = 0;

	virtual void CompactIncremental() = 0;

	virtual void OutOfMemory(size_t nBytesAttempted = 0) = 0;

	// Region-based allocations
	virtual void* RegionAlloc(int region, size_t nSize) = 0;
	virtual void* RegionAlloc(int region, size_t nSize, const char* pFileName, int nLine) = 0;

	// Replacement for ::GlobalMemoryStatus which accounts for unused memory in our system
	virtual void GlobalMemoryStatus(size_t* pUsedMemory, size_t* pFreeMemory) = 0;

	// Obtain virtual memory manager interface
	virtual void* AllocateVirtualMemorySection(size_t numMaxBytes) = 0; // should just always return 0, virtual memory manager is unsupported

	// Request 'generic' memory stats (returns a list of N named values; caller should assume this list will change over time)
	virtual int GetGenericMemoryStats(GenericMemoryStat_t** ppMemoryStats) = 0;

	virtual ~IMemAlloc() { };
    virtual void* Alloc_Aligned(size_t nSize, size_t alignment) = 0;
    virtual void* Realloc_Aligned(void* pMem, size_t nSize, size_t alignment) = 0;
    virtual void Free_Aligned(void* pMem, size_t alignment) = 0;
};

enum EDeltaAllocTags : uint8_t
{
    // Unsorted junk
    TAG_DEFAULT = 0,
    // Unsorted game junk
    TAG_GAME,
    // C++ junk with new.
    TAG_NEW,

    TAG_COUNT,
};
static_assert(TAG_COUNT <= 256, "too many allocation tags!");

enum EDeltaAllocHeaps : uint8_t
{
    HEAP_DEFAULT = -1,
    
    // GPH
    HEAP_SYSTEM = 0,
    // Only for game allocations
    HEAP_GAME,
    // Only for us - R1Delta.
    HEAP_DELTA,

    HEAP_COUNT,
};
static_assert(HEAP_COUNT <= 256, "too many allocation heaps!");

__forceinline static const char* Mem_tag_to_cstring(EDeltaAllocTags tag)
{
    switch (tag)
    {
    case TAG_DEFAULT:
        return "TAG_DEFAULT";
    case TAG_GAME:
        return "TAG_GAME";
    case TAG_NEW:
        return "TAG_NEW";
    default:
        return "TAG_UNKNOWN";
    }
}
__forceinline static const char* Mem_heap_to_cstring(EDeltaAllocHeaps heap)
{
    switch (heap)
    {
    case HEAP_DEFAULT:
        return "HEAP_DEFAULT";
    case HEAP_SYSTEM:
        return "HEAP_SYSTEM";
    case HEAP_GAME:
        return "HEAP_GAME";
    case HEAP_DELTA:
        return "HEAP_DELTA";
    default:
        return "HEAP_UNKNOWN";
    }
}

class CMimMemAlloc : public IMemAlloc {
    std::array<HANDLE, (size_t)HEAP_COUNT> _heaps = { 0 };

#if HEAP_CHECK_ENABLED
    static constexpr size_t FREE_E_STACK = 8;
    struct free_e
    {
        void* aligned;
        void* retaddr[FREE_E_STACK];
    };
    // NOTE(mrsteyk): this will fucking kill your perf.
    static constexpr size_t FREE_LRU_SIZE = 20480;
    free_e free_lru[FREE_LRU_SIZE] = {};
    size_t free_lru_idx = 0;
    SRWLOCK free_lru_lock = SRWLOCK_INIT;
#endif
    
    // idTech 7 heap allocator thing from TheGreatCircle.
    struct alloc_check_t {
        using hash_t = uint32_t;

        uint8_t tag;
        uint8_t heap;
        uint16_t align_skip;
        hash_t hash;
        size_t size;

        static constexpr hash_t do_hash(uintptr_t ptr, size_t size, uint16_t align_skip, uint8_t tag, uint8_t heap_id)
        {
            hash_t first = hash_t(heap_id) | (hash_t(tag) << 8ul) | (hash_t(align_skip) << 16ul);

            uint64_t second64 = ptr ^ size;

            return first ^ hash_t(second64) ^ hash_t(second64 >> 32);
        }
    };
    static constexpr size_t MEM_ALLOC_OVERHEAD = sizeof(alloc_check_t) + sizeof(alloc_check_t::hash_t);
    static_assert(sizeof(alloc_check_t) == 16, "alloc_check_t has invalid size! not 16");

public:
    void* mi_malloc(size_t nSize, EDeltaAllocTags tag = TAG_DEFAULT, EDeltaAllocHeaps heap = HEAP_DEFAULT)
    {
        //R1DAssert(nSize);
        if (!nSize)
        {
            nSize = 16;
        }

        HANDLE _heap;
        if (heap == HEAP_DEFAULT)
        {
            R1DAssert(0);
            heap = HEAP_GAME;
        }
        _heap = _heaps[heap];
        R1DAssert(_heap);

#if HEAP_CHECK_ENABLED
        AcquireSRWLockExclusive(&free_lru_lock);
#endif
        uint8_t* p = (uint8_t*)HeapAlloc(_heap, 0, nSize + MEM_ALLOC_OVERHEAD);
        R1DAssert(p);
        if (!p)
        {
#if HEAP_CHECK_ENABLED
            ReleaseSRWLockExclusive(&free_lru_lock);
#endif
            return 0;
        }

        void* aligned = p + sizeof(alloc_check_t);
        ptrdiff_t align_skip = uintptr_t(aligned) - uintptr_t(p);
        R1DAssert(uintptr_t(aligned) % 16 == 0);

        alloc_check_t* check = (alloc_check_t*)(uintptr_t(aligned) - sizeof(alloc_check_t));
        check->align_skip = align_skip;
        check->tag = tag;
        check->heap = heap;
        check->size = nSize;
        alloc_check_t::hash_t hash = check->do_hash(uintptr_t(check), check->size, check->align_skip, check->tag, check->heap);
        check->hash = hash;

        *(alloc_check_t::hash_t*)(uintptr_t(aligned) + nSize) = hash;

#if HEAP_CHECK_ENABLED
        {
            //AcquireSRWLockExclusive(&free_lru_lock);
            auto size = free_lru_idx;
            if (size >= FREE_LRU_SIZE)
            {
                size = FREE_LRU_SIZE;
            }
            for (size_t i = 0; i < size; i++)
            {
                free_e* e = free_lru + i;
                if (e->aligned == aligned)
                {
                    memset(e, 0, sizeof(*e));
                }
            }
            ReleaseSRWLockExclusive(&free_lru_lock);
        }
#endif

        return aligned;
    }

    void* mi_malloc_aligned(size_t nSize, size_t alignment, EDeltaAllocTags tag = TAG_DEFAULT, EDeltaAllocHeaps heap = HEAP_DEFAULT)
    {
        R1DAssert(alignment <= 16);
        return mi_malloc(nSize, tag, heap);
    }

    void mi_free(void* aligned, EDeltaAllocTags tag = TAG_DEFAULT, EDeltaAllocHeaps heap = HEAP_DEFAULT)
    {
        if (!aligned)
            return;

#if HEAP_CHECK_ENABLED
        {
            AcquireSRWLockShared(&free_lru_lock);
            auto size = free_lru_idx;
            if (size >= FREE_LRU_SIZE)
            {
                size = FREE_LRU_SIZE;
            }
            for (size_t i = 0; i < size; i++)
            {
                const free_e* e = free_lru + i;
                if (e->aligned == aligned)
                {
                    R1DAssert(!"Double free detected?");
                }
            }
            ReleaseSRWLockShared(&free_lru_lock);
        }
#endif

        alloc_check_t* check = (alloc_check_t*)(uintptr_t(aligned) - sizeof(alloc_check_t));
        alloc_check_t::hash_t hash = check->do_hash(uintptr_t(check), check->size, check->align_skip, check->tag, check->heap);
        if (check->hash == hash)
        {
            alloc_check_t::hash_t* check2 = (alloc_check_t::hash_t*)(uintptr_t(aligned) + check->size);
            if (*check2 == hash)
            {
                // TODO(mrsteyk): game might be in a too fragile of a state to call anything...
                R1DAssert(heap == check->heap);
                if (heap != check->heap)
                {
                    Warning("[MEM] Heap index mismatch in FREE at %p - %d(%s) expected, but got %d(%s)!\n", aligned, (int)heap, Mem_heap_to_cstring(heap), (int)check->heap, Mem_heap_to_cstring((EDeltaAllocHeaps)check->heap));
                }
                R1DAssert(tag == check->tag);
                if (tag != check->tag)
                {
                    Warning("[MEM] Heap tag mismatch in FREE at %p - %d(%s) expected, but got %d(%s)!\n", aligned, (int)tag, Mem_tag_to_cstring(tag), (int)check->tag, Mem_tag_to_cstring((EDeltaAllocTags)check->tag));
                }

                auto heap = check->heap;
                HANDLE _heap = _heaps[heap];
                R1DAssert(_heap);

                // NOTE(mrsteyk): have 99.99% confidence that it will break hashing.
                auto align_skip = check->align_skip;
                memset(check, 0, sizeof(*check));
                *check2 = 0;
                check = 0;
                check2 = 0;

#if HEAP_CHECK_ENABLED
                AcquireSRWLockExclusive(&free_lru_lock);
#endif
                
                R1DAssert(HeapFree(_heap, 0, (void*)(uintptr_t(aligned) - align_skip)));

#if HEAP_CHECK_ENABLED
                free_e e;
                e.aligned = aligned;
                USHORT frames = CaptureStackBackTrace(0, FREE_E_STACK, e.retaddr, NULL);

                auto free_lru_idx_bak = free_lru_idx;
                while (free_lru[free_lru_idx % FREE_LRU_SIZE].aligned != 0)
                {
                    free_lru_idx++;
                    if ((free_lru_idx % FREE_LRU_SIZE) == (free_lru_idx_bak % FREE_LRU_SIZE))
                    {
                        //R1DAssert(!"Too much shit in free LRU cache!");
                        break;
                    }
                }
                free_lru[free_lru_idx % FREE_LRU_SIZE] = e;
                free_lru_idx++;
                ReleaseSRWLockExclusive(&free_lru_lock);
#endif
            }
            else {
                R1DAssert(!"Heap corruption at the end!");
                Warning("[MEM] Heap corruption in FREE at %p at the end - %d expected but got %d\n", aligned, (int)hash, (int)*check2);
            }
        }
        else {
            R1DAssert(!"Heap corruption at beginning!");
            Warning("[MEM] Heap corruption in FREE at %p at the beginning - %d expected but got %d\n", aligned, (int)hash, (int)check->hash);
        }
    }

    void mi_free_aligned(void* aligned, size_t alignment, EDeltaAllocTags tag = TAG_DEFAULT, EDeltaAllocHeaps heap = HEAP_DEFAULT)
    {
        R1DAssert(alignment <= 16);
        return mi_free(aligned, tag, heap);
    }

    void mi_free_size(void* aligned, size_t size, EDeltaAllocTags tag = TAG_DEFAULT, EDeltaAllocHeaps heap = HEAP_DEFAULT)
    {
        if (!aligned)
            return;

        alloc_check_t* check = (alloc_check_t*)(uintptr_t(aligned) - sizeof(alloc_check_t));
        alloc_check_t::hash_t hash = check->do_hash(uintptr_t(check), check->size, check->align_skip, check->tag, check->heap);
        if (check->hash == hash)
        {
            alloc_check_t::hash_t* check2 = (alloc_check_t::hash_t*)(uintptr_t(aligned) + check->size);
            if (*check2 == hash)
            {
                R1DAssert(size == check->size);
                // TODO(mrsteyk): game might be in a too fragile of a state to call anything...
                if (size != check->size)
                {
                    Warning("[MEM] Heap size mismatch in FREE_SIZE at %p - %zu expected, but got %zu!\n", aligned, (size_t)size, (size_t)check->size);
                }
                return mi_free(aligned, tag, heap);
            }
            else {
                R1DAssert(!"Heap corruption at the end!");
                Warning("[MEM] Heap corruption in FREE_SIZE at %p at the end - %d expected but got %d\n", aligned, (int)hash, (int)*check2);
            }
        }
        else {
            R1DAssert(!"Heap corruption at beginning!");
            Warning("[MEM] Heap corruption in FREE_SIZE at %p at the beginning - %d expected but got %d\n", aligned, (int)hash, (int)check->hash);
        }
    }

    void mi_free_size_aligned(void* aligned, size_t size, size_t align, EDeltaAllocTags tag = TAG_DEFAULT, EDeltaAllocHeaps heap = HEAP_DEFAULT)
    {
        R1DAssert(align <= 16);
        return mi_free_size(aligned, size, tag, heap);
    }

    size_t mi_usable_size(void* aligned)
    {
        if (!aligned)
            return 0;

        alloc_check_t* check = (alloc_check_t*)(uintptr_t(aligned) - sizeof(alloc_check_t));
        alloc_check_t::hash_t hash = check->do_hash(uintptr_t(check), check->size, check->align_skip, check->tag, check->heap);
        if (check->hash == hash)
        {
            alloc_check_t::hash_t check2 = *(alloc_check_t::hash_t*)(uintptr_t(aligned) + check->size);
            if (check2 == hash)
            {
                return check->size;
            }
            else {
                R1DAssert(!"Heap corruption at the end!");
                Warning("[MEM] Heap corruption in USEABLE_SIZE at %p at the end - %d expected but got %d\n", aligned, (int)hash, (int)check2);
                return 0;
            }
        }
        else {
            R1DAssert(!"Heap corruption at beginning!");
            Warning("[MEM] Heap corruption in USEABLE_SIZE at %p at the beginning - %d expected but got %d\n", aligned, (int)hash, (int)check->hash);
            return 0;
        }
    }

    void* mi_realloc(void* aligned, size_t size, EDeltaAllocTags tag = TAG_DEFAULT, EDeltaAllocHeaps heap = HEAP_DEFAULT)
    {
        alloc_check_t* check = (alloc_check_t*)(uintptr_t(aligned) - sizeof(alloc_check_t));
        alloc_check_t::hash_t hash = check->do_hash(uintptr_t(check), check->size, check->align_skip, check->tag, check->heap);
        if (check->hash == hash)
        {
            alloc_check_t::hash_t* check2 = (alloc_check_t::hash_t*)(uintptr_t(aligned) + check->size);
            if (*check2 == hash)
            {
                void* po = (void*)(uintptr_t(aligned) - check->align_skip);

                // TODO(mrsteyk): game might be in a too fragile of a state to call anything...
                R1DAssert(heap == check->heap);
                if (heap != check->heap)
                {
                    Warning("[MEM] Heap index mismatch in REALLOC at %p - %d(%s) expected, but got %d(%s)!\n", aligned, (int)heap, Mem_heap_to_cstring(heap), (int)check->heap, Mem_heap_to_cstring((EDeltaAllocHeaps)check->heap));
                }
                R1DAssert(tag == check->tag);
                if (tag != check->tag)
                {
                    Warning("[MEM] Heap tag mismatch in REALLOC at %p - %d(%s) expected, but got %d(%s)!\n", aligned, (int)tag, Mem_tag_to_cstring(tag), (int)check->tag, Mem_tag_to_cstring((EDeltaAllocTags)check->tag));
                }
                
                auto heap = check->heap;
                HANDLE _heap = _heaps[heap];
                R1DAssert(_heap);


                // TODO(mrsteyk): for now assume realloc always succeeds!
                auto tag = check->tag;
                memset(check, 0, sizeof(*check));
                *check2 = 0;
                check = 0;
                check2 = 0;

#if HEAP_CHECK_ENABLED
                AcquireSRWLockExclusive(&free_lru_lock);
#endif
                void* p = HeapReAlloc(_heap, 0, po, size + MEM_ALLOC_OVERHEAD);
                R1DAssert(p);

                if (p) {
                    void* aligned_new = (void*)(uintptr_t(p) + sizeof(alloc_check_t));
                    ptrdiff_t align_skip_new = uintptr_t(aligned_new) - uintptr_t(p);
                    alloc_check_t* check_new = (alloc_check_t*)(uintptr_t(aligned_new) - sizeof(alloc_check_t));
                    check_new->align_skip = align_skip_new;
                    check_new->tag = tag;
                    check_new->heap = heap;
                    check_new->size = size;
                    alloc_check_t::hash_t hash = check_new->do_hash(uintptr_t(check_new), check_new->size, check_new->align_skip, check_new->tag, check_new->heap);
                    check_new->hash = hash;

                    *(alloc_check_t::hash_t*)(uintptr_t(aligned_new) + size) = hash;

#if HEAP_CHECK_ENABLED
                    {
                        //AcquireSRWLockExclusive(&free_lru_lock);
                        auto size = free_lru_idx;
                        if (size >= FREE_LRU_SIZE)
                        {
                            size = FREE_LRU_SIZE;
                        }
                        for (size_t i = 0; i < size; i++)
                        {
                            free_e* e = free_lru + i;
                            if (e->aligned == aligned || e->aligned == aligned_new)
                            {
                                memset(e, 0, sizeof(*e));
                            }
                        }
                        ReleaseSRWLockExclusive(&free_lru_lock);
                    }
#endif

                    return aligned_new;
                }
                else {
#if HEAP_CHECK_ENABLED
                    ReleaseSRWLockExclusive(&free_lru_lock);
#endif
                    R1DAssert(!"Realloc fail");
                    Warning("[MEM] REALLOC failed from %p to size %zu\n", aligned, (size_t)size);
                    return 0;
                }
            }
            else {
                R1DAssert(!"Heap corruption at the end!");
                Warning("[MEM] Heap corruption in REALLOC at %p at the end - %d expected but got %d\n", aligned, (int)hash, (int)*check2);
                return 0;
            }
        }
        else {
            R1DAssert(!"Heap corruption at beginning!");
            Warning("[MEM] Heap corruption in REALLOC at %p at the beginning - %d expected but got %d\n", aligned, (int)hash, (int)check->hash);
            return 0;
        }
    }

    void* mi_realloc_aligned(void* aligned, size_t size, size_t alignment, EDeltaAllocTags tag = TAG_DEFAULT, EDeltaAllocHeaps heap = HEAP_DEFAULT)
    {
        R1DAssert(alignment <= 16);
        return mi_realloc(aligned, size, tag, heap);
    }

public:
    CMimMemAlloc() : m_pfnAllocFailHandler(nullptr), m_nFailedAllocationSize(0)
    {
        DWORD options = HEAP_CREATE_ALIGN_16;
#if BUILD_DEBUG
        options |= HEAP_GENERATE_EXCEPTIONS;
#endif

        _heaps[HEAP_SYSTEM] = GetProcessHeap();

        _heaps[HEAP_GAME] = HeapCreate(options, 128ull * (1ull << 20), 0);
        _heaps[HEAP_DELTA] = HeapCreate(options, 16ull * (1ull << 20), 0);
        
#if 0
        for (size_t i = 0; i < (size_t)HEAP_COUNT; ++i)
        {
            R1DAssert(_heaps[i]);
            // Low fragment heap setting
            ULONG HeapInformation = 2;
            HeapSetInformation(_heaps[i], HeapCompatibilityInformation, &HeapInformation, sizeof(HeapInformation));
        }
#else
        ULONG HeapInformation = 2;
        HeapSetInformation(_heaps[HEAP_SYSTEM], HeapCompatibilityInformation, &HeapInformation, sizeof(HeapInformation));
#endif
    }

    void* Alloc(size_t nSize) override {
        CheckVPhysics();

        void* p = mi_malloc(nSize, TAG_GAME, HEAP_GAME);
        if (!p) {
            m_nFailedAllocationSize = nSize;
            if (m_pfnAllocFailHandler) {
                size_t result = m_pfnAllocFailHandler(nSize);
                if (result == 0) {
                    p = mi_malloc(nSize);
                    if (!p) {
                        m_nFailedAllocationSize = nSize;
                    }
                }
            }
        }

        void* returnAddress = _ReturnAddress();
        if (m_vphysicsStart && returnAddress >= m_vphysicsStart && returnAddress < m_vphysicsEnd) {
            memset(p, 0xFE, nSize);
        }

        R1DAssert(p);
        return p;
    }



    void* Realloc(void* pMem, size_t nSize) override {
        void* p = pMem ? mi_realloc(pMem, nSize, TAG_GAME, HEAP_GAME) : mi_malloc(nSize, TAG_GAME, HEAP_GAME);
        if (!p) {
            m_nFailedAllocationSize = nSize;
            if (m_pfnAllocFailHandler) {
                size_t result = m_pfnAllocFailHandler(nSize);
                if (result == 0) {
                    p = mi_realloc(pMem, nSize, TAG_GAME, HEAP_GAME);
                    if (!p) {
                        m_nFailedAllocationSize = nSize;
                    }
                }
            }
        }
        R1DAssert(p);
        return p;
    }

    void Free(void* pMem) override {
        mi_free(pMem, TAG_GAME, HEAP_GAME);
    }

    void* Expand_NoLongerSupported(void* pMem, size_t nSize) override {
        (void)pMem;
        (void)nSize;
        return NULL;
    }

    // Debug versions
    void* Alloc(size_t nSize, const char* pFileName, int nLine) override {
        (void)pFileName;
        (void)nLine;
        return Alloc(nSize);
    }

    void* Realloc(void* pMem, size_t nSize, const char* pFileName, int nLine) override {
        (void)pFileName;
        (void)nLine;
        return Realloc(pMem, nSize);
    }

    void Free(void* pMem, const char* pFileName, int nLine) override {
        (void)pFileName;
        (void)nLine;
        Free(pMem);
    }

    void* Expand_NoLongerSupported(void* pMem, size_t nSize, const char* pFileName, int nLine) override {
        (void)pFileName;
        (void)nLine;
        return Expand_NoLongerSupported(pMem, nSize);
    }

    size_t GetSize(void* pMem) override {
        return pMem ? mi_usable_size(pMem) : 0;
    }

    void PushAllocDbgInfo(const char* pFileName, int nLine) override {
        (void)pFileName;
        (void)nLine;
    }

    void PopAllocDbgInfo() override {
    }

    int32_t CrtSetBreakAlloc(int32_t lNewBreakAlloc) override {
        (void)lNewBreakAlloc;
        return 0;
    }

    int CrtSetReportMode(int nReportType, int nReportMode) override {
        (void)nReportType;
        (void)nReportMode;
        return 0;
    }

    int CrtIsValidHeapPointer(const void* pMem) override {
        return pMem != nullptr;
    }

    int CrtIsValidPointer(const void* pMem, unsigned int size, int access) override {
        (void)size;
        (void)access;
        return pMem != nullptr;
    }

    int CrtCheckMemory(void) override {
        return 1;
    }

    int CrtSetDbgFlag(int nNewFlag) override {
        (void)nNewFlag;
        return 0;
    }

    void CrtMemCheckpoint(_CrtMemState* pState) override {
        (void)pState;
    }

    void DumpStats() override {
        mi_stats_print(nullptr);
    }

    void DumpStatsFileBase(char const* pchFileBase) override {
        (void)pchFileBase;
        mi_stats_print(nullptr);
    }

    size_t ComputeMemoryUsedBy(char const* pchSubStr) override {
        (void)pchSubStr;
        return 0;
    }

    void* CrtSetReportFile(int nRptType, void* hFile) override {
        (void)nRptType;
        (void)hFile;
        return nullptr;
    }

    void* CrtSetReportHook(void* pfnNewHook) override {
        (void)pfnNewHook;
        return nullptr;
    }

    int CrtDbgReport(int nRptType, const char* szFile,
        int nLine, const char* szModule, const char* pMsg) override {
        (void)nRptType;
        (void)szFile;
        (void)nLine;
        (void)szModule;
        (void)pMsg;
        return 0;
    }

    int heapchk() override {
        return 0;
    }

    bool IsDebugHeap() override {
        return false;
    }

    void GetActualDbgInfo(const char*& pFileName, int& nLine) override {
        pFileName = nullptr;
        nLine = 0;
    }

    void RegisterAllocation(const char* pFileName, int nLine, size_t nLogicalSize, size_t nActualSize, unsigned nTime) override {
        (void)pFileName;
        (void)nLine;
        (void)nLogicalSize;
        (void)nActualSize;
        (void)nTime;
    }

    void RegisterDeallocation(const char* pFileName, int nLine, size_t nLogicalSize, size_t nActualSize, unsigned nTime) override {
        (void)pFileName;
        (void)nLine;
        (void)nLogicalSize;
        (void)nActualSize;
        (void)nTime;
    }

    int GetVersion() override {
        return MI_MALLOC_VERSION;
    }

    void CompactHeap() override {
        mi_collect(true);
    }

    MemAllocFailHandler_t SetAllocFailHandler(MemAllocFailHandler_t pfnMemAllocFailHandler) override {
        MemAllocFailHandler_t oldHandler = m_pfnAllocFailHandler;
        m_pfnAllocFailHandler = pfnMemAllocFailHandler;
        return oldHandler;
    }

    void DumpBlockStats(void* p) override {
        (void)p;
    }

    void SetStatsExtraInfo(const char* pMapName, const char* pComment) override {
        (void)pMapName;
        (void)pComment;
    }

    size_t MemoryAllocFailed() override {
        size_t temp = m_nFailedAllocationSize;
        m_nFailedAllocationSize = 0;
        return temp;
    }

    void CompactIncremental() override {
        //mi_collect(false);
        // not needed on mimalloc, causes hitches
    }

    void OutOfMemory(size_t nBytesAttempted = 0) override {
        if (m_pfnAllocFailHandler) {
            m_pfnAllocFailHandler(nBytesAttempted);
        }
    }

    void* RegionAlloc(int region, size_t nSize) override {
        (void)region;
        return Alloc(nSize);
    }

    void* RegionAlloc(int region, size_t nSize, const char* pFileName, int nLine) override {
        (void)region;
        return Alloc(nSize, pFileName, nLine);
    }

    void GlobalMemoryStatus(size_t* pUsedMemory, size_t* pFreeMemory) override {
        size_t current_commit = 0;
        size_t free_memory = 0;

        HANDLE _heap = this->_heaps[HEAP_GAME];
        HeapLock(_heap);
        PROCESS_HEAP_ENTRY e = {};
        while (HeapWalk(_heap, &e))
        {
            if (!(e.wFlags & PROCESS_HEAP_UNCOMMITTED_RANGE))
            {
                current_commit += e.cbData + e.cbOverhead;
            }
            else
            {
                free_memory += e.cbData + e.cbOverhead;
            }
        }
        HeapUnlock(_heap);

        if (pUsedMemory) {
            *pUsedMemory = current_commit;
        }
        if (pFreeMemory) {
            *pFreeMemory = free_memory;
        }
    }

    void* AllocateVirtualMemorySection(size_t numMaxBytes) override {
        (void)numMaxBytes;
        return nullptr;
    }

    int GetGenericMemoryStats(GenericMemoryStat_t** ppMemoryStats) override {
        if (ppMemoryStats) {
            *ppMemoryStats = nullptr;
        }
        return 0;
    }

    ~CMimMemAlloc() override {}
    void* Alloc_Aligned(size_t nSize, size_t alignment) override {
        if (nSize == 0) nSize = 1;
        
        void* p = mi_malloc_aligned(nSize, alignment, TAG_GAME, HEAP_GAME);
        if (!p) {
            m_nFailedAllocationSize = nSize;
            if (m_pfnAllocFailHandler) {
                size_t result = m_pfnAllocFailHandler(nSize);
                if (result == 0) {
                    p = mi_malloc_aligned(nSize, alignment);
                    if (!p) {
                        m_nFailedAllocationSize = nSize;
                    }
                }
            }
        }
        memset(p, 0xFE, nSize);
        R1DAssert(p);
        return p;
    }

    void* Realloc_Aligned(void* pMem, size_t nSize, size_t alignment) override {
        if (nSize == 0) {
            Free_Aligned(pMem, alignment);
            return nullptr;
        }
        void* p = pMem ? mi_realloc_aligned(pMem, nSize, alignment, TAG_GAME, HEAP_GAME) : mi_malloc_aligned(nSize, alignment, TAG_GAME, HEAP_GAME);
        if (!p) {
            m_nFailedAllocationSize = nSize;
            if (m_pfnAllocFailHandler) {
                size_t result = m_pfnAllocFailHandler(nSize);
                if (result == 0) {
                    p = mi_realloc_aligned(pMem, nSize, alignment, TAG_GAME, HEAP_GAME);
                    if (!p) {
                        m_nFailedAllocationSize = nSize;
                    }
                }
            }
        }
        R1DAssert(p);
        return p;
    }

    void Free_Aligned(void* pMem, size_t alignment) override {
        mi_free_aligned(pMem, alignment, TAG_GAME, HEAP_GAME);
    }

    void CheckVPhysics() {
#if 0
        if (!m_vphysicsChecked) {
            HMODULE hModule = GetModuleHandleA("vphysics.dll");
            if (hModule) {
                MODULEINFO modInfo;
                if (GetModuleInformation(GetCurrentProcess(), hModule, &modInfo, sizeof(MODULEINFO))) {
                    m_vphysicsStart = modInfo.lpBaseOfDll;
                    m_vphysicsEnd = (char*)m_vphysicsStart + modInfo.SizeOfImage;
                }
            }
            m_vphysicsChecked = true;
        }
#endif
    }
private:
    MemAllocFailHandler_t m_pfnAllocFailHandler;
    size_t m_nFailedAllocationSize;
private:
    void* m_vphysicsStart = nullptr;
    void* m_vphysicsEnd = nullptr;
    bool m_vphysicsChecked = false;
};

typedef IMemAlloc* (*PFN_CreateGlobalMemAlloc)();



void* __cdecl hkcalloc_base(size_t Count, size_t Size);
void* __cdecl hkmalloc_base(size_t Size);
void* __cdecl hkrealloc_base(void* Block, size_t Size);
void __cdecl hkfree_base(void* Block);
void* __cdecl hkrecalloc_base(void* Block, size_t Count, size_t Size);
extern IMemAlloc* g_pMemAllocSingleton;
extern "C" __declspec(dllexport) IMemAlloc * CreateGlobalMemAlloc();

CMimMemAlloc* GlobalAllocator();