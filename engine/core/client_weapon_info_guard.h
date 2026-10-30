#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <thread>

// Retail R1 client.dll (TimeDateStamp 0x55038DB8, SizeOfImage 0x39A4000), the same image as
// client_studio_header_guard.h.
//
// client.dll+0x2ED300 is the 20-byte weapon-info lookup
//     FileWeaponInfo_t* GetFileWeaponInfoByIndex(uint16_t index)
//         = *(FileWeaponInfo_t**)(dict.m_pElements + 24 * index + 16)
// over the CUtlDict<FileWeaponInfo_t*, uint16_t> at client.dll+0xB0EE20 (a CUtlRBTree: 24-byte
// nodes {left u16, right u16, parent u16, tag u16, key char*, value FileWeaponInfo_t*}). It has no
// bounds or null check. C_BaseCombatWeapon keeps its index at +0x2378 (9080) and the constructor
// (client.dll+0x255F0) initialises it to 0xFFFF; it only becomes valid when the entity's Precache
// (C_WeaponX::Precache, client.dll+0x481A00 = C_BaseCombatWeapon vtable +0x100, run from the
// IClientNetworkable slot 17 function at client.dll+0x483450) resolves the weapon class name. A
// weapon released before that (C_WeaponX::UpdateOnRemove, client.dll+0x492D60 ->
// client.dll+0x242C0 -> lookup) reads node 0xFFFF, 1.5 MB past the table, and dereferences
// whatever is there (crash reports: client.dll+0x243A5 reading [RAX+0xAD], RCX = 3 * 0xFFFF =
// 0x2FFFD, RAX = 0 or 0x56).
namespace r1delta::client_weapon_info
{
inline constexpr std::uintptr_t kLookupRva = 0x2ED300;
inline constexpr std::uintptr_t kDictRva = 0xB0EE20;
// Value stored at dict+0: the comparison function, an absolute (relocated) pointer to
// client.dll+0x1F40. Used as a structural check that kDictRva really is the weapon dictionary.
inline constexpr std::uintptr_t kDictLessFuncRva = 0x1F40;
inline constexpr std::uintptr_t kPreferredImageBase = 0x180000000;
inline constexpr std::uintptr_t kFileWeaponInfoCtorRva = 0x2EBA90;

inline constexpr std::size_t kElementsOffset = 0x08;   // Node_t* (CUtlMemory::m_pMemory)
inline constexpr std::size_t kAllocCountOffset = 0x10; // low word: CUtlMemory allocation count
inline constexpr std::size_t kNumElementsOffset = 0x22;
inline constexpr std::size_t kLastAllocOffset = 0x26;  // highest allocated node, 0xFFFF = none
inline constexpr std::size_t kNodeSize = 24;
inline constexpr std::size_t kNodeValueOffset = 16;
inline constexpr std::uint16_t kInvalidIndex = 0xFFFF;

// sizeof(FileWeaponInfo_t) is 22936 (the allocation size in client.dll+0x2EEAC0 and +0x2EED10);
// round up to a multiple of 64.
inline constexpr std::size_t kNativeRecordSize = 22936;
inline constexpr std::size_t kRecordSize = 0x5A00;
static_assert(kRecordSize >= kNativeRecordSize && kRecordSize % 64 == 0);

// Anything below this cannot be a heap pointer (null, or small garbage such as the 0x56 seen in
// the mp_thaw crash).
inline constexpr std::uintptr_t kMinPlausiblePointer = 0x10000;

inline constexpr std::uint8_t kExpectedLookupBytes[] = {
	0x0F, 0xB7, 0xC1,                         // movzx eax, cx
	0x48, 0x8D, 0x0C, 0x40,                   // lea rcx, [rax+rax*2]
	0x48, 0x8B, 0x05, 0x1A, 0x1B, 0x82, 0x00, // mov rax, [rip+0x821B1A]   (dict.m_pElements)
	0x48, 0x8B, 0x44, 0xC8, 0x10,             // mov rax, [rax+rcx*8+0x10]
	0xC3,                                     // ret
};
static_assert(sizeof(kExpectedLookupBytes) == 20);

inline constexpr std::uint8_t kExpectedCtorPrologue[] = {
	0x48, 0x89, 0x5C, 0x24, 0x08,             // mov [rsp+8], rbx
	0x57,                                     // push rdi
	0x48, 0x83, 0xEC, 0x20,                   // sub rsp, 20h
	0x48, 0x8D, 0x05, 0x57, 0x5F, 0x5A, 0x00, // lea rax, [rip+0x5A5F57]   (FileWeaponInfo_t vftable)
};

using RecordConstructor = void*(__fastcall*)(void* record);

namespace detail
{
template <typename T>
inline T Read(const void* base, std::size_t offset) noexcept
{
	T value;
	std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(T));
	return value;
}
}

inline std::uint16_t NumElements(const void* dict) noexcept
{
	return dict ? detail::Read<std::uint16_t>(dict, kNumElementsOffset) : std::uint16_t{0};
}

// The record the native lookup would return for `index`, or nullptr when the native lookup would
// read outside the table or return a null / implausible pointer. For every index the native
// function handles correctly the result is the identical pointer.
inline void* ResolveRecord(const void* dict, std::uint16_t index) noexcept
{
	if (!dict)
		return nullptr;
	const auto* const elements = detail::Read<const std::uint8_t*>(dict, kElementsOffset);
	if (!elements)
		return nullptr;
	const std::uint16_t allocCount = detail::Read<std::uint16_t>(dict, kAllocCountOffset);
	const std::uint16_t lastAlloc = detail::Read<std::uint16_t>(dict, kLastAllocOffset);
	if (lastAlloc == kInvalidIndex || index > lastAlloc || index >= allocCount)
		return nullptr;
	void* const record =
		detail::Read<void*>(elements, std::size_t{index} * kNodeSize + kNodeValueOffset);
	return reinterpret_cast<std::uintptr_t>(record) < kMinPlausiblePointer ? nullptr : record;
}

// True when `dict` looks like the weapon dictionary of the pinned image: its comparison function
// is the expected pointer, relocated to `clientBase` (or still the preferred-base value, in case
// the check runs before the loader has applied relocations).
inline bool LooksLikeWeaponDict(const void* dict, std::uintptr_t clientBase) noexcept
{
	if (!dict)
		return false;
	const std::uintptr_t lessFunc = detail::Read<std::uintptr_t>(dict, 0);
	return lessFunc == clientBase + kDictLessFuncRva
		|| lessFunc == kPreferredImageBase + kDictLessFuncRva;
}

// Lock-free "first time this value is seen" set over all 65536 uint16_t values (8 KiB).
class FirstSeenSet
{
public:
	// Returns true exactly once per distinct value, for any number of racing callers.
	bool Claim(std::uint16_t value) noexcept
	{
		const std::uint64_t bit = std::uint64_t{1} << (value & 63);
		return (m_words[value >> 6].fetch_or(bit, std::memory_order_relaxed) & bit) == 0;
	}

private:
	std::atomic<std::uint64_t> m_words[1024]{};
};

// The record handed out for an invalid index: a FileWeaponInfo_t built by the game's own
// constructor (the state of a weapon definition that has not parsed a script: empty strings,
// null resource pointers, flags false, the constructor's non-zero defaults), constructed once on
// first use. Zero-filled when no constructor is supplied. Never freed; the native code only
// writes caches into it (muzzle/sound bits), which are harmless on a throwaway record.
class FallbackRecord
{
public:
	void* Get(RecordConstructor construct) noexcept
	{
		if (m_state.load(std::memory_order_acquire) == kReady)
			return m_bytes;
		int expected = kEmpty;
		if (m_state.compare_exchange_strong(expected, kBuilding, std::memory_order_acquire)) {
			std::memset(m_bytes, 0, sizeof(m_bytes));
			if (construct)
				construct(m_bytes);
			m_state.store(kReady, std::memory_order_release);
		} else {
			while (m_state.load(std::memory_order_acquire) != kReady)
				std::this_thread::yield();
		}
		return m_bytes;
	}

	const std::uint8_t* Bytes() const noexcept { return m_bytes; }

private:
	enum : int { kEmpty = 0, kBuilding = 1, kReady = 2 };
	alignas(64) std::uint8_t m_bytes[kRecordSize]{};
	std::atomic<int> m_state{kEmpty};
};
}
