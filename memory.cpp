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
		// Load the tier0_r1.dll library
		HMODULE hModule = LoadLibraryA("tier0_r1.dll");
		if (hModule != nullptr) {
			// Get the address of CreateGlobalMemAlloc in the loaded library
			PFN_CreateGlobalMemAlloc pfnCreateGlobalMemAlloc = (PFN_CreateGlobalMemAlloc)GetProcAddress(hModule, "CreateGlobalMemAlloc");
			if (pfnCreateGlobalMemAlloc != nullptr) {
				// Call the function from the DLL and set the singleton
				g_pMemAllocSingleton = pfnCreateGlobalMemAlloc();
			}
			else {
				// Handle the error if the function is not found
				// You might want to log an error or perform other error handling here
			}
		}
		else {
			// Handle the error if the DLL is not loaded
			// You might want to log an error or perform other error handling here
		}
	}
	return g_pMemAllocSingleton;
}

void* __cdecl hkcalloc_base(size_t Count, size_t Size)
{
	size_t const nTotal = Count * Size;
	void* const pNew = CreateGlobalMemAlloc()->Alloc(nTotal);

	memset(pNew, NULL, nTotal);
	return pNew;
}
void* __cdecl hkmalloc_base(size_t Size)
{
	return CreateGlobalMemAlloc()->Alloc(Size);

}

void* __cdecl hkrealloc_base(void* Block, size_t Size)
{
	if (Size) {
		return CreateGlobalMemAlloc()->Realloc(Block, Size);
	}
	else
	{
		CreateGlobalMemAlloc()->Free(Block);
		return nullptr;
	}
}

void __cdecl hkfree_base(void* Block)
{
	CreateGlobalMemAlloc()->Free(Block);

}

void* __cdecl hkrecalloc_base(void* Block, size_t Count, size_t Size)
{
	const size_t nTotal = Count * Size;
	void* const pMemOut = CreateGlobalMemAlloc()->Realloc(Block, nTotal);

	if (!Block)
		memset(pMemOut, NULL, nTotal);

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