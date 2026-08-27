#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace r1delta::materialsystem_dx11
{
inline constexpr std::uintptr_t kInputLayoutCacheLookupRva = 0x1B4E0;
inline constexpr std::uintptr_t kInputLayoutCacheStorageRva = 0x2A3A10;
inline constexpr std::uintptr_t kImmediateContextRva = 0x290D90;
inline constexpr std::uintptr_t kInputLayoutCacheCountRva = 0x2A99BC;
inline constexpr std::uintptr_t kCurrentVertexFormatRva = 0x2820D0;
inline constexpr std::uintptr_t kCurrentInputLayoutRva = 0x2A39E8;

inline constexpr std::size_t kInputLayoutBucketHeadOffset = 0x1400;
inline constexpr std::size_t kInputLayoutNextOffset = 0x1600;
inline constexpr std::size_t kInputLayoutKeyOffset = 0x1E00;
inline constexpr std::size_t kInputLayoutPointerOffset = 0x3E00;
inline constexpr std::size_t kInputLayoutBucketCount =
	(kInputLayoutNextOffset - kInputLayoutBucketHeadOffset)
	/ sizeof(std::uint16_t);
inline constexpr std::size_t kInputLayoutCacheCapacity =
	(kInputLayoutPointerOffset - kInputLayoutKeyOffset)
	/ sizeof(std::uint64_t);

inline constexpr std::uint8_t kExpectedInputLayoutCacheLookupPrologue[] = {
	0x40, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x44, 0x0F,
	0xB6, 0x15, 0xD2, 0xE4, 0x28, 0x00, 0x48, 0x8D,
	0x3D, 0x1B, 0x85, 0x28, 0x00, 0x4C, 0x8B, 0xC9,
	0x4A, 0x8D, 0x94, 0x57, 0x00, 0x14, 0x00, 0x00
};

[[nodiscard]] inline constexpr std::size_t InputLayoutKeyStorageOffset(
	std::size_t index) noexcept
{
	return kInputLayoutKeyOffset + index * sizeof(std::uint64_t);
}

[[nodiscard]] inline constexpr std::size_t InputLayoutNextStorageOffset(
	std::size_t index) noexcept
{
	return kInputLayoutNextOffset + index * sizeof(std::uint16_t);
}

[[nodiscard]] inline constexpr std::size_t InputLayoutPointerStorageOffset(
	std::size_t index) noexcept
{
	return kInputLayoutPointerOffset + index * sizeof(void*);
}

static_assert(kInputLayoutBucketCount == 256);
static_assert(kInputLayoutCacheCapacity == 1024);
static_assert(
	InputLayoutKeyStorageOffset(kInputLayoutCacheCapacity)
	== InputLayoutPointerStorageOffset(0));
static_assert(
	InputLayoutNextStorageOffset(kInputLayoutCacheCapacity)
	== InputLayoutKeyStorageOffset(0));

using InputLayoutReleaseFunction = unsigned long(__fastcall*)(void* layout);

struct InputLayoutCacheView
{
	std::uint16_t* bucketHeads;
	std::uint16_t* next;
	std::uint64_t* keys;
	void** layouts;
	std::uint32_t* count;
	std::uint64_t* currentVertexFormat;
	void** currentInputLayout;
};

enum class InputLayoutCacheResetResult
{
	notFull,
	reset,
	stateMissing,
	releaseMissing,
	countCorrupt
};

struct InputLayoutCacheResetReport
{
	InputLayoutCacheResetResult result;
	std::uint32_t observedCount;
	std::size_t releasedLayouts;
};

[[nodiscard]] inline InputLayoutCacheResetReport ResetInputLayoutCacheIfFull(
	const InputLayoutCacheView& cache,
	InputLayoutReleaseFunction release)
{
	if (!cache.bucketHeads
		|| !cache.next
		|| !cache.keys
		|| !cache.layouts
		|| !cache.count
		|| !cache.currentVertexFormat
		|| !cache.currentInputLayout) {
		return { InputLayoutCacheResetResult::stateMissing, 0, 0 };
	}

	const std::uint32_t count = *cache.count;
	if (count < kInputLayoutCacheCapacity)
		return { InputLayoutCacheResetResult::notFull, count, 0 };
	if (count > kInputLayoutCacheCapacity) {
		return {
			InputLayoutCacheResetResult::countCorrupt,
			count,
			0
		};
	}
	if (!release)
		return { InputLayoutCacheResetResult::releaseMissing, count, 0 };

	std::size_t releasedLayouts = 0;
	for (std::size_t index = 0; index < kInputLayoutCacheCapacity; ++index) {
		void* const layout = cache.layouts[index];
		if (layout) {
			release(layout);
			++releasedLayouts;
		}
		cache.layouts[index] = nullptr;
		cache.keys[index] = 0;
		cache.next[index] = std::numeric_limits<std::uint16_t>::max();
	}
	for (std::size_t index = 0; index < kInputLayoutBucketCount; ++index) {
		cache.bucketHeads[index] =
			std::numeric_limits<std::uint16_t>::max();
	}

	*cache.count = 0;
	*cache.currentVertexFormat =
		std::numeric_limits<std::uint64_t>::max();
	*cache.currentInputLayout = nullptr;
	return {
		InputLayoutCacheResetResult::reset,
		count,
		releasedLayouts
	};
}
}
