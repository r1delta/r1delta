#include "../engine/memory/allocation_header.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace
{
bool Check(bool condition, const char* message)
{
	if (!condition)
		std::cerr << "FAILED: " << message << '\n';
	return condition;
}

// Writes a block exactly the way CMimMemAlloc::mi_malloc does: header, payload, tail hash.
void* WriteBlock(std::uintptr_t blockStart, std::size_t payloadSize, std::uint8_t tag, std::uint8_t heap)
{
	auto* header = reinterpret_cast<alloc_check_t*>(blockStart);
	header->tag = tag;
	header->heap = heap;
	header->align_skip = static_cast<std::uint16_t>(sizeof(alloc_check_t));
	header->size = payloadSize;
	header->hash = alloc_check_t::do_hash(blockStart, payloadSize, header->align_skip, tag, heap);

	const std::uintptr_t payload = blockStart + sizeof(alloc_check_t);
	std::memcpy(reinterpret_cast<void*>(payload + payloadSize), &header->hash, sizeof(header->hash));
	return reinterpret_cast<void*>(payload);
}

// Three reserved pages with only the middle one committed, so reading the page in front of it or
// the page after it faults.
struct GuardedPage
{
	std::uintptr_t base = 0;
	std::uintptr_t readable = 0;
	std::size_t pageSize = 0;

	GuardedPage()
	{
		SYSTEM_INFO info;
		GetSystemInfo(&info);
		pageSize = info.dwPageSize;
		base = reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr, pageSize * 3, MEM_RESERVE, PAGE_NOACCESS));
		if (base)
			readable = reinterpret_cast<std::uintptr_t>(VirtualAlloc(reinterpret_cast<void*>(base + pageSize), pageSize, MEM_COMMIT, PAGE_READWRITE));
	}

	~GuardedPage()
	{
		if (base)
			VirtualFree(reinterpret_cast<void*>(base), 0, MEM_RELEASE);
	}

	GuardedPage(const GuardedPage&) = delete;
	GuardedPage& operator=(const GuardedPage&) = delete;
};

bool TestOwnedBlockIsAccepted()
{
	std::vector<std::uint8_t> storage(sizeof(alloc_check_t) + 64 + sizeof(alloc_check_t::hash_t) + 16);
	const std::uintptr_t blockStart = (reinterpret_cast<std::uintptr_t>(storage.data()) + 15) & ~std::uintptr_t{ 15 };
	void* payload = WriteBlock(blockStart, 64, 1, 1);

	bool passed = true;
	alloc_check_t header{};
	passed &= Check(TryReadOwnedAllocation(payload, &header), "intact block rejected");
	passed &= Check(header.size == 64 && header.tag == 1 && header.heap == 1, "intact block header not returned");

	alloc_check_t headerOnly{};
	passed &= Check(TryReadAllocationHeader(payload, &headerOnly), "intact header rejected");
	return passed;
}

bool TestCorruptedBlockIsRejected()
{
	std::vector<std::uint8_t> storage(sizeof(alloc_check_t) + 64 + sizeof(alloc_check_t::hash_t) + 16);
	const std::uintptr_t blockStart = (reinterpret_cast<std::uintptr_t>(storage.data()) + 15) & ~std::uintptr_t{ 15 };
	void* payload = WriteBlock(blockStart, 64, 1, 1);
	alloc_check_t header{};

	bool passed = true;
	auto* tail = reinterpret_cast<alloc_check_t::hash_t*>(reinterpret_cast<std::uintptr_t>(payload) + 64);
	*tail ^= 1;
	passed &= Check(!TryReadOwnedAllocation(payload, &header), "block with a corrupted tail accepted");
	passed &= Check(TryReadAllocationHeader(payload, &header), "header-only read must ignore the tail");

	*tail ^= 1;
	reinterpret_cast<alloc_check_t*>(blockStart)->size = 63;
	passed &= Check(!TryReadOwnedAllocation(payload, &header), "block with a corrupted header accepted");
	passed &= Check(!TryReadAllocationHeader(payload, &header), "corrupted header accepted by the header-only read");
	return passed;
}

// The v3.0.4 crash: a page-aligned foreign pointer whose preceding page is not mapped. Reading the
// 16-byte header in front of it used to fault inside the ownership check.
bool TestPageAlignedForeignPointerIsRejected()
{
	GuardedPage pages;
	if (!Check(pages.readable != 0, "could not set up the guarded page"))
		return false;

	std::memset(reinterpret_cast<void*>(pages.readable), 0xAB, pages.pageSize);
	void* foreign = reinterpret_cast<void*>(pages.readable);
	alloc_check_t header{};

	bool passed = true;
	passed &= Check(!TryReadOwnedAllocation(foreign, &header), "page-aligned foreign pointer accepted");
	passed &= Check(!TryReadAllocationHeader(foreign, &header), "page-aligned foreign header accepted");
	return passed;
}

// A header that hashes correctly but whose size puts the tail in the uncommitted page after it.
bool TestTailInUnmappedPageIsRejected()
{
	GuardedPage pages;
	if (!Check(pages.readable != 0, "could not set up the guarded page"))
		return false;

	const std::uintptr_t blockStart = pages.readable + pages.pageSize - sizeof(alloc_check_t) - 16;
	const std::uintptr_t payload = blockStart + sizeof(alloc_check_t);
	const std::uintptr_t nextPage = pages.readable + pages.pageSize;
	auto* header = reinterpret_cast<alloc_check_t*>(blockStart);
	header->tag = 0;
	header->heap = 1;
	header->align_skip = static_cast<std::uint16_t>(sizeof(alloc_check_t));
	header->size = nextPage + pages.pageSize / 2 - payload;
	header->hash = alloc_check_t::do_hash(blockStart, header->size, header->align_skip, header->tag, header->heap);

	alloc_check_t read{};
	bool passed = true;
	passed &= Check(TryReadAllocationHeader(reinterpret_cast<void*>(payload), &read), "forged header not readable");
	passed &= Check(!TryReadOwnedAllocation(reinterpret_cast<void*>(payload), &read), "block whose tail is unmapped accepted");
	return passed;
}

bool TestImpossibleSizesAndAddressesAreRejected()
{
	std::vector<std::uint8_t> storage(sizeof(alloc_check_t) + 32);
	const std::uintptr_t blockStart = (reinterpret_cast<std::uintptr_t>(storage.data()) + 15) & ~std::uintptr_t{ 15 };
	auto* header = reinterpret_cast<alloc_check_t*>(blockStart);
	header->tag = 0;
	header->heap = 1;
	header->align_skip = static_cast<std::uint16_t>(sizeof(alloc_check_t));
	header->size = ~std::size_t{ 0 } - 8;
	header->hash = alloc_check_t::do_hash(blockStart, header->size, header->align_skip, header->tag, header->heap);
	void* payload = reinterpret_cast<void*>(blockStart + sizeof(alloc_check_t));

	alloc_check_t read{};
	bool passed = true;
	passed &= Check(!TryReadOwnedAllocation(payload, &read), "header whose size wraps the address space accepted");
	passed &= Check(!TryReadOwnedAllocation(nullptr, &read), "null pointer accepted");
	passed &= Check(!TryReadOwnedAllocation(reinterpret_cast<void*>(0x8), &read), "pointer below the header size accepted");
	passed &= Check(!TryReadOwnedAllocation(reinterpret_cast<void*>(0x8000), &read), "pointer in the null page range accepted");
	passed &= Check(!TryReadOwnedAllocation(reinterpret_cast<void*>(0xFFFF800000000000ull), &read), "kernel-range pointer accepted");
	return passed;
}

bool TestUntrustedCopy()
{
	GuardedPage pages;
	if (!Check(pages.readable != 0, "could not set up the guarded page"))
		return false;

	const std::uint64_t pattern = 0x0123456789ABCDEFull;
	std::memcpy(reinterpret_cast<void*>(pages.readable), &pattern, sizeof(pattern));

	bool passed = true;
	std::uint64_t value = 0;
	passed &= Check(TryCopyUntrustedBytes(&value, pages.readable, sizeof(value)) && value == pattern, "readable bytes not copied");
	passed &= Check(!TryCopyUntrustedBytes(&value, pages.readable - 8, sizeof(value)), "copy straddling an unmapped page succeeded");
	passed &= Check(!TryCopyUntrustedBytes(&value, pages.base, sizeof(value)), "copy from an uncommitted page succeeded");
	passed &= Check(!TryCopyUntrustedBytes(&value, pages.readable, 0), "empty copy succeeded");
	return passed;
}
} // namespace

int main()
{
	bool passed = true;
	passed &= TestOwnedBlockIsAccepted();
	passed &= TestCorruptedBlockIsRejected();
	passed &= TestPageAlignedForeignPointerIsRejected();
	passed &= TestTailInUnmappedPageIsRejected();
	passed &= TestImpossibleSizesAndAddressesAreRejected();
	passed &= TestUntrustedCopy();

	if (!passed)
		return 1;

	std::cout << "allocation_header_tests passed\n";
	return 0;
}
