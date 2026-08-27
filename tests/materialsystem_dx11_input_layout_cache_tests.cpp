#include "../engine/core/materialsystem_dx11_input_layout_cache.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>

namespace
{
using namespace r1delta::materialsystem_dx11;

static_assert(sizeof(kExpectedInputLayoutCacheLookupPrologue) == 32);
static_assert(kInputLayoutBucketCount == 256);
static_assert(kInputLayoutCacheCapacity == 1024);

struct FakeLayout
{
	unsigned int releases = 0;
};

bool Check(bool condition, const char* message)
{
	if (!condition)
		std::cerr << "FAILED: " << message << '\n';
	return condition;
}

unsigned long __fastcall ReleaseLayout(void* layout)
{
	auto* const fake = static_cast<FakeLayout*>(layout);
	return ++fake->releases;
}

struct CacheState
{
	std::array<std::uint16_t, kInputLayoutBucketCount> bucketHeads{};
	std::array<std::uint16_t, kInputLayoutCacheCapacity> next{};
	std::array<std::uint64_t, kInputLayoutCacheCapacity> keys{};
	std::array<void*, kInputLayoutCacheCapacity> layouts{};
	std::uint32_t count = 0;
	std::uint64_t currentVertexFormat = 0;
	void* currentInputLayout = nullptr;

	InputLayoutCacheView View()
	{
		return {
			bucketHeads.data(),
			next.data(),
			keys.data(),
			layouts.data(),
			&count,
			&currentVertexFormat,
			&currentInputLayout
		};
	}
};

bool TestOverflowAliasesLayoutPointer()
{
	return Check(
			InputLayoutKeyStorageOffset(kInputLayoutCacheCapacity)
				== InputLayoutPointerStorageOffset(0),
			"the overflowing key does not alias layout[0]")
		&& Check(
			InputLayoutNextStorageOffset(kInputLayoutCacheCapacity)
				== InputLayoutKeyStorageOffset(0),
			"the overflowing next link does not alias key[0]");
}

bool TestCacheBelowCapacityIsUnchanged()
{
	CacheState state;
	FakeLayout layout;
	state.bucketHeads[3] = 7;
	state.next[7] = 9;
	state.keys[7] = 0x001000000022001Dull;
	state.layouts[7] = &layout;
	state.count = static_cast<std::uint32_t>(
		kInputLayoutCacheCapacity - 1);
	state.currentVertexFormat = state.keys[7];
	state.currentInputLayout = &layout;

	const InputLayoutCacheResetReport report =
		ResetInputLayoutCacheIfFull(state.View(), &ReleaseLayout);
	return Check(
			report.result == InputLayoutCacheResetResult::notFull,
			"a cache below capacity was reset")
		&& Check(report.observedCount == state.count,
			"below-capacity count was not reported")
		&& Check(layout.releases == 0,
			"below-capacity layout was released")
		&& Check(state.bucketHeads[3] == 7,
			"below-capacity bucket was changed")
		&& Check(state.next[7] == 9,
			"below-capacity link was changed")
		&& Check(state.keys[7] == 0x001000000022001Dull,
			"below-capacity key was changed")
		&& Check(state.layouts[7] == &layout,
			"below-capacity layout pointer was changed");
}

bool TestFullCacheIsReleasedAndCleared()
{
	CacheState state;
	FakeLayout first;
	FakeLayout middle;
	FakeLayout last;
	state.bucketHeads.fill(3);
	state.next.fill(4);
	state.keys.fill(0x001000000022001Dull);
	state.layouts[0] = &first;
	state.layouts[511] = &middle;
	state.layouts[1023] = &last;
	state.count = static_cast<std::uint32_t>(kInputLayoutCacheCapacity);
	state.currentVertexFormat = 0x001000000022001Dull;
	state.currentInputLayout = &middle;

	const InputLayoutCacheResetReport report =
		ResetInputLayoutCacheIfFull(state.View(), &ReleaseLayout);
	bool passed = true;
	passed &= Check(
		report.result == InputLayoutCacheResetResult::reset,
		"full cache was not reset");
	passed &= Check(
		report.observedCount == kInputLayoutCacheCapacity,
		"full cache count was not reported");
	passed &= Check(report.releasedLayouts == 3,
		"released layout count mismatch");
	passed &= Check(
		first.releases == 1 && middle.releases == 1 && last.releases == 1,
		"cached layouts were not released exactly once");
	passed &= Check(state.count == 0, "cache count was not cleared");
	passed &= Check(
		state.currentVertexFormat
			== std::numeric_limits<std::uint64_t>::max(),
		"current vertex format was not invalidated");
	passed &= Check(state.currentInputLayout == nullptr,
		"current input layout was not invalidated");
	for (std::size_t index = 0; index < state.bucketHeads.size(); ++index) {
		passed &= Check(
			state.bucketHeads[index]
				== std::numeric_limits<std::uint16_t>::max(),
			"bucket head was not cleared");
	}
	for (std::size_t index = 0; index < state.layouts.size(); ++index) {
		passed &= Check(state.next[index]
				== std::numeric_limits<std::uint16_t>::max(),
			"cache link was not cleared");
		passed &= Check(state.keys[index] == 0,
			"cache key was not cleared");
		passed &= Check(state.layouts[index] == nullptr,
			"cache layout pointer was not cleared");
	}
	return passed;
}

bool TestCorruptCountFailsWithoutMutation()
{
	CacheState state;
	FakeLayout layout;
	state.bucketHeads[0] = 2;
	state.next[0] = 3;
	state.keys[0] = 4;
	state.layouts[0] = &layout;
	state.count = static_cast<std::uint32_t>(
		kInputLayoutCacheCapacity + 1);

	const InputLayoutCacheResetReport report =
		ResetInputLayoutCacheIfFull(state.View(), &ReleaseLayout);
	return Check(
			report.result == InputLayoutCacheResetResult::countCorrupt,
			"corrupt cache count was accepted")
		&& Check(layout.releases == 0,
			"corrupt cache released an untrusted pointer")
		&& Check(state.bucketHeads[0] == 2,
			"corrupt cache bucket was changed")
		&& Check(state.next[0] == 3,
			"corrupt cache link was changed")
		&& Check(state.keys[0] == 4,
			"corrupt cache key was changed")
		&& Check(state.layouts[0] == &layout,
			"corrupt cache layout was changed");
}

bool TestFullCacheRequiresReleaseOperation()
{
	CacheState state;
	state.count = static_cast<std::uint32_t>(kInputLayoutCacheCapacity);
	const InputLayoutCacheResetReport report =
		ResetInputLayoutCacheIfFull(state.View(), nullptr);
	return Check(
		report.result == InputLayoutCacheResetResult::releaseMissing,
		"full cache accepted a missing release operation")
		&& Check(state.count == kInputLayoutCacheCapacity,
			"missing release operation changed the cache");
}
}

int main()
{
	bool passed = true;
	passed &= TestOverflowAliasesLayoutPointer();
	passed &= TestCacheBelowCapacityIsUnchanged();
	passed &= TestFullCacheIsReleasedAndCleared();
	passed &= TestCorruptCountFailsWithoutMutation();
	passed &= TestFullCacheRequiresReleaseOperation();
	if (!passed)
		return 1;
	std::cout << "materialsystem DX11 input-layout cache tests passed\n";
	return 0;
}
