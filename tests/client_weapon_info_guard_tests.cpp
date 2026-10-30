#include "../engine/core/client_weapon_info_guard.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

namespace
{
using namespace r1delta::client_weapon_info;

bool Check(bool condition, const char* message)
{
	if (!condition)
		std::cerr << "FAILED: " << message << '\n';
	return condition;
}

// The retail lookup, for comparison: *(void**)(m_pElements + 24 * index + 16).
void* NativeLookup(const void* dict, std::uint16_t index)
{
	const auto* const elements = detail::Read<const std::uint8_t*>(dict, kElementsOffset);
	return detail::Read<void*>(elements, std::size_t{index} * kNodeSize + kNodeValueOffset);
}

// A CUtlRBTree-shaped block with a node array of `allocCount` nodes. Only the fields the guard
// reads are modelled; everything else stays zero.
struct FakeDict
{
	alignas(8) std::array<std::uint8_t, 0x30> dict{};
	std::vector<std::uint8_t> nodes;

	FakeDict(std::uint16_t allocCount, std::uint16_t lastAlloc, std::uint16_t numElements)
		: nodes(std::size_t{allocCount} * kNodeSize + 64, 0xCD)
	{
		const std::uint8_t* const elements = nodes.data();
		std::memcpy(dict.data() + kElementsOffset, &elements, sizeof(elements));
		std::memcpy(dict.data() + kAllocCountOffset, &allocCount, sizeof(allocCount));
		std::memcpy(dict.data() + kNumElementsOffset, &numElements, sizeof(numElements));
		std::memcpy(dict.data() + kLastAllocOffset, &lastAlloc, sizeof(lastAlloc));
	}

	void SetValue(std::uint16_t index, std::uintptr_t value)
	{
		std::memcpy(nodes.data() + std::size_t{index} * kNodeSize + kNodeValueOffset,
			&value, sizeof(value));
	}

	void SetElements(const std::uint8_t* elements)
	{
		std::memcpy(dict.data() + kElementsOffset, &elements, sizeof(elements));
	}

	const void* Get() const { return dict.data(); }
};

constexpr std::uintptr_t Fake(std::uint16_t index) { return 0x7FF600000000ull + index * 0x5A00ull; }

FakeDict MakeDict(std::uint16_t count, std::uint16_t allocCount)
{
	FakeDict d(allocCount, count ? std::uint16_t(count - 1) : kInvalidIndex, count);
	for (std::uint16_t i = 0; i < count; ++i)
		d.SetValue(i, Fake(i));
	return d;
}

bool TestValidIndexesAreIdenticalToTheNativeLookup()
{
	FakeDict d = MakeDict(167, 256);
	bool same = true;
	for (std::uint16_t i = 0; i < 167; ++i)
		same &= ResolveRecord(d.Get(), i) == NativeLookup(d.Get(), i)
			&& ResolveRecord(d.Get(), i) == reinterpret_cast<void*>(Fake(i));
	return Check(same, "a valid index did not return the native lookup's pointer")
		&& Check(ResolveRecord(d.Get(), 166) != nullptr, "last allocated index rejected");
}

bool TestInvalidHandleAndOutOfRangeIndexesAreRejected()
{
	FakeDict d = MakeDict(167, 256);
	// 0xFFFF is what the C_BaseCombatWeapon constructor stores (RCX = 3 * 0xFFFF in the crashes).
	return Check(ResolveRecord(d.Get(), kInvalidIndex) == nullptr, "0xFFFF index accepted")
		&& Check(ResolveRecord(d.Get(), 167) == nullptr, "first index past the last allocated node accepted")
		&& Check(ResolveRecord(d.Get(), 255) == nullptr, "allocated-but-unused slot accepted")
		&& Check(ResolveRecord(d.Get(), 256) == nullptr, "index equal to the allocation count accepted")
		&& Check(ResolveRecord(d.Get(), 0x8000) == nullptr, "far out-of-range index accepted");
}

bool TestIndexPastAllocationCountIsRejectedEvenIfLastAllocLies()
{
	FakeDict d(/*allocCount=*/16, /*lastAlloc=*/200, /*numElements=*/201);
	d.SetValue(15, Fake(15));
	return Check(ResolveRecord(d.Get(), 15) == reinterpret_cast<void*>(Fake(15)),
			"index inside the allocation was rejected")
		&& Check(ResolveRecord(d.Get(), 16) == nullptr, "index at the allocation count accepted")
		&& Check(ResolveRecord(d.Get(), 200) == nullptr, "index past the allocation accepted");
}

bool TestEmptyOrFreedDictionary()
{
	FakeDict empty = MakeDict(0, 0);
	FakeDict freed = MakeDict(4, 4);
	freed.SetElements(nullptr); // sub_1807D3750 frees the table and nulls m_pElements at shutdown
	return Check(ResolveRecord(empty.Get(), 0) == nullptr, "empty dictionary returned a record")
		&& Check(ResolveRecord(freed.Get(), 1) == nullptr, "freed table returned a record")
		&& Check(ResolveRecord(nullptr, 0) == nullptr, "null dictionary returned a record")
		&& Check(NumElements(nullptr) == 0, "null dictionary has elements")
		&& Check(NumElements(MakeDict(5, 8).Get()) == 5, "element count read wrong");
}

bool TestNullAndGarbageEntryPointersAreRejected()
{
	FakeDict d = MakeDict(8, 8);
	d.SetValue(2, 0);       // a node whose value pointer is null
	d.SetValue(3, 0x56);    // the mp_thaw crash: RAX = 0x56
	d.SetValue(4, 0xFFFF);  // still below the plausible-pointer floor
	d.SetValue(5, kMinPlausiblePointer);
	return Check(ResolveRecord(d.Get(), 2) == nullptr, "null entry pointer accepted")
		&& Check(ResolveRecord(d.Get(), 3) == nullptr, "0x56 entry pointer accepted")
		&& Check(ResolveRecord(d.Get(), 4) == nullptr, "0xFFFF entry pointer accepted")
		&& Check(ResolveRecord(d.Get(), 5) == reinterpret_cast<void*>(kMinPlausiblePointer),
			"lowest plausible entry pointer rejected")
		&& Check(ResolveRecord(d.Get(), 6) == reinterpret_cast<void*>(Fake(6)),
			"neighbouring valid entry affected");
}

bool TestDictionarySignature()
{
	const std::uintptr_t base = 0x7FFA12340000ull;
	std::array<std::uint8_t, 0x30> block{};
	const std::uintptr_t relocated = base + kDictLessFuncRva;
	std::memcpy(block.data(), &relocated, sizeof(relocated));
	bool passed = Check(LooksLikeWeaponDict(block.data(), base), "relocated signature rejected");
	const std::uintptr_t preferred = kPreferredImageBase + kDictLessFuncRva;
	std::memcpy(block.data(), &preferred, sizeof(preferred));
	passed &= Check(LooksLikeWeaponDict(block.data(), base), "preferred-base signature rejected");
	const std::uintptr_t other = base + kDictLessFuncRva + 0x10;
	std::memcpy(block.data(), &other, sizeof(other));
	passed &= Check(!LooksLikeWeaponDict(block.data(), base), "wrong signature accepted");
	passed &= Check(!LooksLikeWeaponDict(nullptr, base), "null dictionary accepted");
	return passed;
}

bool TestFirstSeenSet()
{
	FirstSeenSet seen;
	bool passed = Check(seen.Claim(0xFFFF), "0xFFFF not claimable the first time")
		&& Check(!seen.Claim(0xFFFF), "0xFFFF claimable twice")
		&& Check(seen.Claim(0), "0 not claimable the first time")
		&& Check(!seen.Claim(0), "0 claimable twice")
		&& Check(seen.Claim(63) && seen.Claim(64) && seen.Claim(1023),
			"neighbouring values collided")
		&& Check(!seen.Claim(63) && !seen.Claim(64) && !seen.Claim(1023),
			"neighbouring values claimable twice");

	FirstSeenSet raced;
	std::atomic<int> winners{0};
	std::vector<std::thread> threads;
	for (int t = 0; t < 8; ++t) {
		threads.emplace_back([&] {
			for (std::uint16_t v = 0x1000; v < 0x1100; ++v)
				if (raced.Claim(v))
					winners.fetch_add(1);
		});
	}
	for (auto& thread : threads)
		thread.join();
	return passed && Check(winners.load() == 0x100, "racing claims did not win exactly once per value");
}

int g_ctorCalls;
bool g_ctorSawZeroedBuffer;
void* g_ctorRecord;

void* __fastcall FakeCtor(void* record)
{
	++g_ctorCalls;
	const auto* const bytes = static_cast<const std::uint8_t*>(record);
	g_ctorSawZeroedBuffer = true;
	for (std::size_t i = 0; i < kRecordSize; ++i)
		g_ctorSawZeroedBuffer &= bytes[i] == 0;
	g_ctorRecord = record;
	static_cast<std::uint8_t*>(record)[0] = 0xA5;                      // vtable stand-in
	static_cast<std::uint8_t*>(record)[kNativeRecordSize - 1] = 0x5A;  // last byte of the native object
	return record;
}

bool TestFallbackRecordIsBuiltOnceFromTheNativeConstructor()
{
	g_ctorCalls = 0;
	FallbackRecord fallback;
	void* const first = fallback.Get(&FakeCtor);
	void* const second = fallback.Get(&FakeCtor);
	const auto* const bytes = static_cast<const std::uint8_t*>(first);
	return Check(first == second, "fallback record moved")
		&& Check(g_ctorCalls == 1, "constructor not called exactly once")
		&& Check(g_ctorSawZeroedBuffer, "constructor did not receive a zeroed buffer")
		&& Check(g_ctorRecord == first, "constructor built a different buffer")
		&& Check(reinterpret_cast<std::uintptr_t>(first) % 64 == 0, "fallback record misaligned")
		&& Check(bytes[0] == 0xA5 && bytes[kNativeRecordSize - 1] == 0x5A,
			"constructor output lost")
		&& Check(bytes[kRecordSize - 1] == 0 && bytes[kNativeRecordSize] == 0,
			"bytes past the native object not zero")
		&& Check(fallback.Bytes() == first, "Bytes() disagrees with Get()");
}

bool TestFallbackRecordWithoutConstructorIsZero()
{
	FallbackRecord fallback;
	const auto* const bytes = static_cast<const std::uint8_t*>(fallback.Get(nullptr));
	bool zero = true;
	for (std::size_t i = 0; i < kRecordSize; ++i)
		zero &= bytes[i] == 0;
	return Check(zero, "constructor-less fallback record is not zero")
		&& Check(fallback.Get(&FakeCtor) == bytes, "record moved after construction");
}

bool TestFallbackRecordRacingFirstUse()
{
	g_ctorCalls = 0;
	FallbackRecord fallback;
	std::atomic<int> sawMarker{0};
	std::vector<std::thread> threads;
	for (int t = 0; t < 8; ++t) {
		threads.emplace_back([&] {
			const auto* const bytes = static_cast<const std::uint8_t*>(fallback.Get(&FakeCtor));
			if (bytes[0] == 0xA5 && bytes[kNativeRecordSize - 1] == 0x5A)
				sawMarker.fetch_add(1);
		});
	}
	for (auto& thread : threads)
		thread.join();
	return Check(g_ctorCalls == 1, "racing first use built the record more than once")
		&& Check(sawMarker.load() == 8, "a racing caller saw a half-built record");
}

bool TestPinnedLayoutConstants()
{
	// Offsets and sizes recovered from client.dll (stock R1, TimeDateStamp 0x55038DB8).
	static_assert(kNodeSize == 24 && kNodeValueOffset == 16);
	static_assert(kLastAllocOffset == 0x26 && kNumElementsOffset == 0x22);
	static_assert(sizeof(kExpectedLookupBytes) == 20);
	return Check(kExpectedLookupBytes[0] == 0x0F && kExpectedLookupBytes[19] == 0xC3,
			"lookup byte signature is not movzx ... ret")
		&& Check(kRecordSize >= kNativeRecordSize, "fallback record smaller than FileWeaponInfo_t")
		&& Check(kInvalidIndex == 0xFFFF, "invalid weapon-info handle changed");
}
}

int main()
{
	bool passed = true;
	passed &= TestValidIndexesAreIdenticalToTheNativeLookup();
	passed &= TestInvalidHandleAndOutOfRangeIndexesAreRejected();
	passed &= TestIndexPastAllocationCountIsRejectedEvenIfLastAllocLies();
	passed &= TestEmptyOrFreedDictionary();
	passed &= TestNullAndGarbageEntryPointersAreRejected();
	passed &= TestDictionarySignature();
	passed &= TestFirstSeenSet();
	passed &= TestFallbackRecordIsBuiltOnceFromTheNativeConstructor();
	passed &= TestFallbackRecordWithoutConstructorIsZero();
	passed &= TestFallbackRecordRacingFirstUse();
	passed &= TestPinnedLayoutConstants();
	if (!passed)
		return 1;
	std::cout << "client weapon-info guard tests passed\n";
	return 0;
}
