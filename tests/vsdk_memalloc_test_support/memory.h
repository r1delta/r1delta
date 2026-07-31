#pragma once

#include <cstddef>

class VSDKTestMemAlloc
{
public:
	void* Alloc(std::size_t size);
	void* Realloc(void* memory, std::size_t size);
	void Free(void* memory);

	void* Alloc_Aligned(std::size_t size, std::size_t alignment);
	void* Realloc_Aligned(void* memory, std::size_t size, std::size_t alignment);
	void Free_Aligned(void* memory, std::size_t alignment);
};

VSDKTestMemAlloc* CreateGlobalMemAlloc();

void VSDKTestMemAllocReset();
bool VSDKTestMemAllocOwns(const void* memory);
std::size_t VSDKTestMemAllocOutstanding();
std::size_t VSDKTestMemAllocForeignFrees();
