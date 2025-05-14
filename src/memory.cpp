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
    return CreateGlobalMemAlloc()->Realloc_Aligned(Block, Size, alignof(std::max_align_t) * 2);
}

void __cdecl hkfree_base(void* Block)
{
    CreateGlobalMemAlloc()->Free_Aligned(Block, alignof(std::max_align_t) * 2);
}

void* __cdecl hkrecalloc_base(void* Block, size_t Count, size_t Size)
{
    size_t const nTotal = Count * Size;
    void* const pMemOut = CreateGlobalMemAlloc()->Realloc_Aligned(Block, nTotal, alignof(std::max_align_t) * 2);
    if (pMemOut && !Block) {
        memset(pMemOut, 0, nTotal);
    }
    return pMemOut;
}
