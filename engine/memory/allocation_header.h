#pragma once

// Block header of the tier0 allocator (CMimMemAlloc in memory.h) and the test that decides
// whether an untrusted pointer is one of its blocks.
//
// Every block is [alloc_check_t][payload: size bytes][hash_t tail]; callers get the payload
// pointer. Free, Realloc and GetSize are also reached with pointers this allocator never handed
// out (CRT blocks, other modules' VirtualAlloc'd buffers, stale or corrupted pointers from game
// code), so the ownership test must not fault on them. A page-aligned foreign buffer with nothing
// mapped in front of it puts the 16 header bytes in an unmapped page, and a header that happens to
// hash correctly can put the tail anywhere, so both are copied under SEH. x64 SEH is table-based:
// the guarded copy costs nothing unless it faults. Do not replace it with VirtualQuery, a system
// call on every free that showed up as microstutter.

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

// idTech 7 heap allocator thing from TheGreatCircle.
struct alloc_check_t
{
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
static_assert(sizeof(alloc_check_t) == 16, "alloc_check_t has invalid size! not 16");

inline constexpr size_t MEM_ALLOC_OVERHEAD = sizeof(alloc_check_t) + sizeof(alloc_check_t::hash_t);

// Lowest and highest user-mode addresses on x64 Windows.
inline constexpr uintptr_t kAllocationMinUserAddress = 0x10000;
inline constexpr uintptr_t kAllocationMaxUserAddress = 0x00007FFFFFFFFFFF;

// Address shape only: [begin, begin + size) is a non-empty user-mode range that does not wrap.
// It says nothing about whether those bytes are mapped.
inline bool IsUserAddressRange(uintptr_t begin, size_t size)
{
    if (!size || begin < kAllocationMinUserAddress || begin > kAllocationMaxUserAddress)
        return false;

    return size - 1 <= kAllocationMaxUserAddress - begin;
}

// Copies `size` bytes from an untrusted address into `destination`. Returns false (leaving
// `destination` unspecified) when the range is not a user-mode range or a byte of it is unreadable.
inline bool TryCopyUntrustedBytes(void* destination, uintptr_t source, size_t size)
{
    if (!IsUserAddressRange(source, size))
        return false;

    __try
    {
        memcpy(destination, reinterpret_cast<const void*>(source), size);
    }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
    {
        return false;
    }
    return true;
}

// Reads the header in front of `aligned` and checks its header size and self-hash. This proves
// only that the 16 bytes before `aligned` look like one of this allocator's headers.
inline bool TryReadAllocationHeader(const void* aligned, alloc_check_t* header)
{
    const uintptr_t payload = reinterpret_cast<uintptr_t>(aligned);
    if (payload < sizeof(alloc_check_t))
        return false;

    const uintptr_t headerAddress = payload - sizeof(alloc_check_t);
    alloc_check_t local;
    if (!TryCopyUntrustedBytes(&local, headerAddress, sizeof(local)))
        return false;

    if (local.align_skip < sizeof(alloc_check_t) || local.align_skip > MEM_ALLOC_OVERHEAD)
        return false;

    if (local.hash != alloc_check_t::do_hash(headerAddress, local.size, local.align_skip, local.tag, local.heap))
        return false;

    *header = local;
    return true;
}

// Ownership: a valid header and the matching tail hash right after its payload.
inline bool TryReadOwnedAllocation(const void* aligned, alloc_check_t* header)
{
    alloc_check_t local;
    if (!TryReadAllocationHeader(aligned, &local))
        return false;

    const uintptr_t payload = reinterpret_cast<uintptr_t>(aligned);
    if (payload > kAllocationMaxUserAddress || local.size > kAllocationMaxUserAddress - payload)
        return false;

    alloc_check_t::hash_t tail;
    if (!TryCopyUntrustedBytes(&tail, payload + local.size, sizeof(tail)) || tail != local.hash)
        return false;

    *header = local;
    return true;
}
