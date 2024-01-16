#include "memory.h"

extern IMemAlloc* g_pMemAllocSingleton = nullptr;

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