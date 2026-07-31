#include "memory.h"

#include <thread>

#include "../vsdk/public/vector.h"
#include <bitvec.h>
#include <tier1/utlmemory.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <unordered_map>

namespace
{
	struct Allocation
	{
		bool aligned;
		std::size_t alignment;
	};

	std::mutex g_allocationMutex;
	std::unordered_map<void*, Allocation> g_allocations;
	std::size_t g_foreignFrees = 0;
	VSDKTestMemAlloc g_allocator;

	bool Check(bool condition, const char* message)
	{
		if (!condition)
			std::cerr << "FAILED: " << message << '\n';
		return condition;
	}

	bool RegisterAllocation(void* memory, bool aligned, std::size_t alignment)
	{
		if (!memory)
			return false;
		std::lock_guard lock(g_allocationMutex);
		return g_allocations.emplace(memory, Allocation{ aligned, alignment }).second;
	}

	bool RemoveAllocation(void* memory, bool aligned, std::size_t alignment)
	{
		if (!memory)
			return true;
		std::lock_guard lock(g_allocationMutex);
		const auto allocation = g_allocations.find(memory);
		if (allocation == g_allocations.end()
			|| allocation->second.aligned != aligned
			|| (aligned && allocation->second.alignment != alignment)) {
			++g_foreignFrees;
			return false;
		}
		g_allocations.erase(allocation);
		return true;
	}
}

void* VSDKTestMemAlloc::Alloc(std::size_t size)
{
	void* memory = std::malloc(size ? size : 1);
	RegisterAllocation(memory, false, 0);
	return memory;
}

void* VSDKTestMemAlloc::Realloc(void* memory, std::size_t size)
{
	if (!memory)
		return Alloc(size);
	if (!size) {
		Free(memory);
		return nullptr;
	}
	if (!RemoveAllocation(memory, false, 0))
		return nullptr;
	void* replacement = std::realloc(memory, size);
	if (!replacement) {
		RegisterAllocation(memory, false, 0);
		return nullptr;
	}
	RegisterAllocation(replacement, false, 0);
	return replacement;
}

void VSDKTestMemAlloc::Free(void* memory)
{
	if (RemoveAllocation(memory, false, 0))
		std::free(memory);
}

void* VSDKTestMemAlloc::Alloc_Aligned(std::size_t size, std::size_t alignment)
{
	void* memory = _aligned_malloc(size ? size : 1, alignment);
	RegisterAllocation(memory, true, alignment);
	return memory;
}

void* VSDKTestMemAlloc::Realloc_Aligned(void* memory, std::size_t size, std::size_t alignment)
{
	if (!memory)
		return Alloc_Aligned(size, alignment);
	if (!size) {
		Free_Aligned(memory, alignment);
		return nullptr;
	}
	if (!RemoveAllocation(memory, true, alignment))
		return nullptr;
	void* replacement = _aligned_realloc(memory, size, alignment);
	if (!replacement) {
		RegisterAllocation(memory, true, alignment);
		return nullptr;
	}
	RegisterAllocation(replacement, true, alignment);
	return replacement;
}

void VSDKTestMemAlloc::Free_Aligned(void* memory, std::size_t alignment)
{
	if (RemoveAllocation(memory, true, alignment))
		_aligned_free(memory);
}

VSDKTestMemAlloc* CreateGlobalMemAlloc()
{
	return &g_allocator;
}

void VSDKTestMemAllocReset()
{
	std::lock_guard lock(g_allocationMutex);
	g_allocations.clear();
	g_foreignFrees = 0;
}

bool VSDKTestMemAllocOwns(const void* memory)
{
	std::lock_guard lock(g_allocationMutex);
	return g_allocations.contains(const_cast<void*>(memory));
}

std::size_t VSDKTestMemAllocOutstanding()
{
	std::lock_guard lock(g_allocationMutex);
	return g_allocations.size();
}

std::size_t VSDKTestMemAllocForeignFrees()
{
	std::lock_guard lock(g_allocationMutex);
	return g_foreignFrees;
}

int main()
{
	VSDKTestMemAllocReset();
	bool passed = true;

	{
		CUtlMemory<int> memory(0, 4);
		passed &= Check(VSDKTestMemAllocOwns(memory.Base()),
			"CUtlMemory constructor did not use tier0");
		memory[0] = 17;
		memory.EnsureCapacity(16);
		passed &= Check(VSDKTestMemAllocOwns(memory.Base()),
			"CUtlMemory EnsureCapacity did not use tier0");
		passed &= Check(memory[0] == 17,
			"CUtlMemory EnsureCapacity did not preserve contents");
	}

	{
		int external[4] = { 1, 2, 3, 4 };
		CUtlMemory<int> memory(external, 4);
		memory.ConvertToGrowableMemory(0);
		passed &= Check(VSDKTestMemAllocOwns(memory.Base()),
			"CUtlMemory ConvertToGrowableMemory did not use tier0");
		passed &= Check(memory[3] == 4,
			"CUtlMemory ConvertToGrowableMemory did not preserve contents");
	}

	{
		CUtlMemoryAligned<std::uint64_t, 32> memory(0, 4);
		passed &= Check(VSDKTestMemAllocOwns(memory.Base()),
			"CUtlMemoryAligned constructor did not use tier0");
		passed &= Check((reinterpret_cast<std::uintptr_t>(memory.Base()) & 31u) == 0,
			"CUtlMemoryAligned constructor returned misaligned storage");
		memory[0] = 0x123456789ABCDEF0ull;
		memory.EnsureCapacity(12);
		passed &= Check(VSDKTestMemAllocOwns(memory.Base()),
			"CUtlMemoryAligned EnsureCapacity did not use tier0");
		passed &= Check(memory[0] == 0x123456789ABCDEF0ull,
			"CUtlMemoryAligned EnsureCapacity did not preserve contents");
	}

	{
		CVarBitVec bits(96);
		passed &= Check(VSDKTestMemAllocOwns(bits.Base()),
			"CVarBitVec constructor did not use tier0");
		bits.Set(70);
		bits.Resize(192);
		passed &= Check(VSDKTestMemAllocOwns(bits.Base()),
			"CVarBitVec Resize did not use tier0");
		passed &= Check(bits.IsBitSet(70),
			"CVarBitVec Resize did not preserve contents");
	}

	{
		CVarBitVec bits(16);
		bits.Set(7);
		uint32* detached = nullptr;
		int detachedBits = 0;
		passed &= Check(bits.Detach(&detached, &detachedBits),
			"CVarBitVec Detach failed");
		passed &= Check(detachedBits == 16 && ((*detached & (1u << 7)) != 0),
			"CVarBitVec Detach did not preserve inline contents");
		passed &= Check(VSDKTestMemAllocOwns(detached),
			"CVarBitVec Detach did not return tier0-owned storage");
		CreateGlobalMemAlloc()->Free(detached);
	}

	{
		auto* attached = static_cast<uint32*>(CreateGlobalMemAlloc()->Alloc(sizeof(uint32)));
		*attached = 1u << 9;
		CVarBitVec bits;
		bits.Attach(attached, 16);
		passed &= Check(bits.IsBitSet(9),
			"CVarBitVec Attach did not preserve inline contents");
		passed &= Check(!VSDKTestMemAllocOwns(attached),
			"CVarBitVec Attach did not return transferred storage to tier0");
	}

	passed &= Check(VSDKTestMemAllocOutstanding() == 0,
		"VSDK allocations were not returned to tier0");
	passed &= Check(VSDKTestMemAllocForeignFrees() == 0,
		"VSDK attempted to return foreign storage to tier0");

	if (!passed)
		return EXIT_FAILURE;
	std::cout << "All VSDK tier0 allocator tests passed\n";
	return EXIT_SUCCESS;
}
