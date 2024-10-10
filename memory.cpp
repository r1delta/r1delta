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
#include <iostream>
#include <Psapi.h>
#include <intrin.h>
#include <unordered_map>
#include <mutex>

#pragma intrinsic(_ReturnAddress)


IMemAlloc* g_pMemAllocSingleton;
extern "C" __declspec(dllexport) IMemAlloc * CreateGlobalMemAlloc() {
	if (!g_pMemAllocSingleton) {
        g_pMemAllocSingleton = new CMimMemAlloc();
	}
	return g_pMemAllocSingleton;
}

void* __cdecl hkcalloc_base(size_t Count, size_t Size)
{
    size_t nTotal = Count * Size;
    if (nTotal == 0) nTotal = 1;
    void* const pNew = CreateGlobalMemAlloc()->Alloc_Aligned(nTotal, alignof(std::max_align_t));
    if (pNew) {
        memset(pNew, 0, nTotal);
    }
    return pNew;
}

void* __cdecl hkmalloc_base(size_t Size)
{
    if (Size == 0) Size = 1;
    return CreateGlobalMemAlloc()->Alloc_Aligned(Size, alignof(std::max_align_t));
}

void* __cdecl hkrealloc_base(void* Block, size_t Size)
{
    return CreateGlobalMemAlloc()->Realloc_Aligned(Block, Size, alignof(std::max_align_t));
}

void __cdecl hkfree_base(void* Block)
{
    CreateGlobalMemAlloc()->Free_Aligned(Block, alignof(std::max_align_t));
}

void* __cdecl hkrecalloc_base(void* Block, size_t Count, size_t Size)
{
    size_t const nTotal = Count * Size;
    void* const pMemOut = CreateGlobalMemAlloc()->Realloc_Aligned(Block, nTotal, alignof(std::max_align_t));
    if (pMemOut && !Block) {
        memset(pMemOut, 0, nTotal);
    }
    return pMemOut;
}

typedef void* (*AllocFn)(void*, size_t);
typedef void (*FreeFn)(void*, void*);
typedef void* (*ReallocFn)(void*, void*, size_t);
typedef size_t(*GetSizeFn)(void*, void*);
typedef void* (*RegionAllocFn)(void*, int, size_t);
AllocFn g_originalAlloc = nullptr;
FreeFn g_originalFree = nullptr;
ReallocFn g_originalRealloc = nullptr;
GetSizeFn g_originalGetSize = nullptr;
RegionAllocFn g_originalRegionAlloc = nullptr;



std::unordered_multimap<void*, size_t> g_allocations;
std::mutex g_allocationsMutex;

// Hooked Alloc function
void* HookedAlloc(void* thisptr, size_t size)
{
    void* ptr = g_originalAlloc(thisptr, size);
    if (ptr != nullptr)
    {
        std::lock_guard<std::mutex> lock(g_allocationsMutex);
        g_allocations.insert(std::make_pair(ptr, size));
    }
    return ptr;
}

// Hooked Free function
void HookedFree(void* thisptr, void* ptr)
{
    {
        std::lock_guard<std::mutex> lock(g_allocationsMutex);
        auto range = g_allocations.equal_range(ptr);
        if (range.first != g_allocations.end())
        {
            g_allocations.erase(range.first);
        }
        else
        {
            // Allocation not found, potential double free
            // ... (rest of the code for handling double free)
        }
    }
    g_originalFree(thisptr, ptr);
}

// Hooked Realloc function
void* HookedRealloc(void* thisptr, void* ptr, size_t size)
{
    void* newPtr = g_originalRealloc(thisptr, ptr, size);
    if (newPtr != nullptr)
    {
        std::lock_guard<std::mutex> lock(g_allocationsMutex);
        auto range = g_allocations.equal_range(ptr);
        if (range.first != g_allocations.end())
        {
            g_allocations.erase(range.first);
        }
        g_allocations.insert(std::make_pair(newPtr, size));
    }
    return newPtr;
}

// Hooked GetSize function
size_t HookedGetSize(void* thisptr, void* ptr)
{
    std::lock_guard<std::mutex> lock(g_allocationsMutex);
    auto range = g_allocations.equal_range(ptr);
    if (range.first != g_allocations.end())
    {
        return range.first->second;
    }
    return g_originalGetSize(thisptr, ptr);
}

// Hooked RegionAlloc function
void* HookedRegionAlloc(void* thisptr, int region, size_t size)
{
    void* ptr = g_originalRegionAlloc(thisptr, region, size);
    if (ptr != nullptr)
    {
        std::lock_guard<std::mutex> lock(g_allocationsMutex);
        g_allocations.insert(std::make_pair(ptr, size));
    }
    return ptr;
}