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

#include "framework.h"
#include <concurrent_unordered_map.h>
#include <MinHook.h>
#include "thirdparty/mimalloc-2.1.7/include/mimalloc.h"
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
class CMimMemAlloc : public IMemAlloc {
public:
    CMimMemAlloc() : m_pfnAllocFailHandler(nullptr), m_nFailedAllocationSize(0) {}
    void* Alloc(size_t nSize) override {
        CheckVPhysics();

        void* p = mi_malloc(nSize);
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

        return p;
    }



    void* Realloc(void* pMem, size_t nSize) override {
        void* p = mi_realloc(pMem, nSize);
        if (!p) {
            m_nFailedAllocationSize = nSize;
            if (m_pfnAllocFailHandler) {
                size_t result = m_pfnAllocFailHandler(nSize);
                if (result == 0) {
                    p = mi_realloc(pMem, nSize);
                    if (!p) {
                        m_nFailedAllocationSize = nSize;
                    }
                }
            }
        }
        return p;
    }

    void Free(void* pMem) override {
        mi_free(pMem);
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
        return mi_usable_size(pMem);
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
        size_t elapsed_msecs, user_msecs, system_msecs;
        size_t current_rss, peak_rss;
        size_t current_commit, peak_commit;
        size_t page_faults;
        mi_process_info(&elapsed_msecs, &user_msecs, &system_msecs,
            &current_rss, &peak_rss,
            &current_commit, &peak_commit, &page_faults);
        if (pUsedMemory) {
            *pUsedMemory = current_commit;
        }
        if (pFreeMemory) {
            *pFreeMemory = 0;
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
        void* p = mi_malloc_aligned(nSize, alignment);
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
        return p;
    }

    void* Realloc_Aligned(void* pMem, size_t nSize, size_t alignment) override {
        if (nSize == 0) {
            Free_Aligned(pMem, alignment);
            return nullptr;
        }
        void* p = mi_realloc_aligned(pMem, nSize, alignment);
        if (!p) {
            m_nFailedAllocationSize = nSize;
            if (m_pfnAllocFailHandler) {
                size_t result = m_pfnAllocFailHandler(nSize);
                if (result == 0) {
                    p = mi_realloc_aligned(pMem, nSize, alignment);
                    if (!p) {
                        m_nFailedAllocationSize = nSize;
                    }
                }
            }
        }
        return p;
    }

    void Free_Aligned(void* pMem, size_t alignment) override {
        mi_free_aligned(pMem, alignment);
    }

    void CheckVPhysics() {
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

// Original function pointers
typedef void* (*AllocFn)(void*, size_t);
typedef void (*FreeFn)(void*, void*);
typedef void* (*ReallocFn)(void*,void*, size_t);
typedef size_t(*GetSizeFn)(void*,void*);
typedef void* (*RegionAllocFn)(void*, int, size_t);

extern AllocFn g_originalAlloc;
extern FreeFn g_originalFree;
extern ReallocFn g_originalRealloc;
extern GetSizeFn g_originalGetSize;
extern RegionAllocFn g_originalRegionAlloc;
