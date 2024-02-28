#pragma once

#include "framework.h"

class IMemAlloc {
public:
	virtual void* Alloc_dont(size_t nSize) = 0;
	virtual void* Alloc(size_t nSize) = 0;
	virtual void* Realloc_Dont(void* pMem, size_t nSize) = 0;
	virtual void* Realloc(void* pMem, size_t nSize) = 0;
	virtual void Free_Dont(void* pMem) = 0;
	virtual void Free(void* pMem) = 0;
	virtual void* Expand_NoLongerSupported(void* pMem, size_t nSize) = 0;
	virtual void* Expand_NoLongerSupported2(void* pMem, size_t nSize) = 0;
	virtual size_t GetSize(void* pMem) = 0;
};

typedef IMemAlloc* (*PFN_CreateGlobalMemAlloc)();



void* __cdecl hkcalloc_base(size_t Count, size_t Size);
void* __cdecl hkmalloc_base(size_t Size);
void* __cdecl hkrealloc_base(void* Block, size_t Size);
void __cdecl hkfree_base(void* Block);
void* __cdecl hkrecalloc_base(void* Block, size_t Count, size_t Size);
extern IMemAlloc* g_pMemAllocSingleton;
extern "C" __declspec(dllexport) IMemAlloc * CreateGlobalMemAlloc();