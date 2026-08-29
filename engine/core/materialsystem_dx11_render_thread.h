#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace r1delta::materialsystem_dx11
{
inline constexpr std::uintptr_t kConstantBufferFlushRva = 0x184F0;
inline constexpr std::uintptr_t kDrawFramePointerRestoreRva = 0xCFF7;
inline constexpr std::uintptr_t kDrawFramePointerOffsetFromRsp = 0xD1;
inline constexpr std::uintptr_t kShaderResourceFlushRva = 0x93D0;
inline constexpr std::uintptr_t kShaderResourceSingleProducerRva = 0x140E0;
inline constexpr std::uintptr_t kShaderResourceIndexedProducerRva = 0x14220;
inline constexpr std::uintptr_t kShaderResourceArrayProducerRva = 0x142D0;
inline constexpr std::uintptr_t kShaderResourceMaskedArrayProducerRva = 0x14490;
inline constexpr std::uintptr_t kShaderResourceDirectProducerRva = 0x145A0;
inline constexpr std::uintptr_t kPendingShaderResourcesRva = 0x298730;
inline constexpr std::uintptr_t kPendingShaderResourceCountRva = 0x2987F0;
inline constexpr std::uintptr_t kPreviousShaderResourceCountRva = 0x2987F4;
inline constexpr std::size_t kPendingShaderResourceCapacity = 24;

inline constexpr std::uint8_t kExpectedShaderResourceFlushPrologue[] = {
	0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0x0D,
	0x14, 0xF4, 0x28, 0x00, 0x8B, 0x05, 0x12, 0xF4,
	0x28, 0x00, 0x3B, 0xC1, 0x0F, 0x47, 0xC8, 0x8B,
	0xD9, 0x85, 0xC9, 0x74, 0x7C, 0x48, 0x8B, 0x0D
};
inline constexpr std::uint8_t
kExpectedShaderResourceSingleProducerPrologue[] = {
	0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
	0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57,
	0x48, 0x83, 0xEC, 0x20, 0x0F, 0xBF, 0xC2, 0x48,
	0x63, 0xF1, 0xFF, 0xC8, 0x8B, 0xD0, 0x44, 0x8B
};
inline constexpr std::uint8_t
kExpectedShaderResourceIndexedProducerPrologue[] = {
	0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
	0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57,
	0x48, 0x83, 0xEC, 0x20, 0x0F, 0xBF, 0xC2, 0x48,
	0x63, 0xF9, 0x48, 0x8D, 0x2D, 0xBF, 0xBD, 0xFE
};
inline constexpr std::uint8_t
kExpectedShaderResourceArrayProducerPrologue[] = {
	0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x57, 0x41,
	0x55, 0x48, 0x83, 0xEC, 0x20, 0x45, 0x33, 0xED,
	0x48, 0x8B, 0xFA, 0x48, 0x8B, 0xE9, 0x41, 0x8B,
	0xDD, 0x48, 0x85, 0xD2, 0x0F, 0x84, 0xB6, 0x00
};
inline constexpr std::uint8_t
kExpectedShaderResourceMaskedArrayProducerPrologue[] = {
	0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x57, 0x41,
	0x54, 0x41, 0x55, 0x41, 0x57, 0x48, 0x83, 0xEC,
	0x20, 0x45, 0x33, 0xFF, 0x4D, 0x8B, 0xE8, 0x48,
	0x8B, 0xEA, 0x4C, 0x8B, 0xE1, 0x41, 0x8D, 0x7F
};
inline constexpr std::uint8_t
kExpectedShaderResourceDirectProducerPrologue[] = {
	0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
	0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57,
	0x41, 0x54, 0x41, 0x55, 0x48, 0x83, 0xEC, 0x40,
	0x48, 0x8B, 0x02, 0x48, 0x8B, 0xFA, 0x48, 0x63
};

using TryHoldShaderResourceFunction =
	bool(*)(void* shaderResource, void** heldShaderResource);
using ReleaseHeldShaderResourceFunction =
	void(*)(void* heldShaderResource);

[[nodiscard]] inline constexpr std::size_t ClampShaderResourceCount(
	std::size_t count) noexcept
{
	return count < kPendingShaderResourceCapacity
		? count
		: kPendingShaderResourceCapacity;
}

[[nodiscard]] inline bool ReplacePendingShaderResourceHold(
	void** shaderResources,
	void** heldShaderResources,
	std::size_t capacity,
	std::size_t slot,
	TryHoldShaderResourceFunction tryHold,
	ReleaseHeldShaderResourceFunction releaseHeld) noexcept
{
	if (!shaderResources
		|| !heldShaderResources
		|| !tryHold
		|| !releaseHeld
		|| slot >= capacity) {
		return false;
	}

	void* const shaderResource = shaderResources[slot];
	void* const previousHold = heldShaderResources[slot];
	if (shaderResource == previousHold)
		return true;

	void* replacementHold = nullptr;
	const bool accepted = !shaderResource
		|| (tryHold(shaderResource, &replacementHold)
			&& replacementHold);
	if (!accepted)
		shaderResources[slot] = nullptr;

	heldShaderResources[slot] = replacementHold;
	if (previousHold)
		releaseHeld(previousHold);
	return accepted;
}

inline constexpr std::uint8_t kExpectedConstantBufferFlushPrologue[] = {
	0x48, 0x89, 0x5C, 0x24, 0x08,
	0x48, 0x89, 0x6C, 0x24, 0x10,
	0x48, 0x89, 0x74, 0x24, 0x18,
	0x48, 0x89, 0x7C, 0x24, 0x20,
	0x41, 0x54,
	0x41, 0x55,
	0x41, 0x56,
	0x48, 0x83, 0xEC, 0x40,
	0xB8, 0x01, 0x00, 0x00, 0x00
};

inline constexpr std::uint32_t kAccessViolationExceptionCode =
	0xC0000005u;
inline constexpr std::uintptr_t kConstantBufferMemmoveStartRva =
	0x1364E0;
inline constexpr std::uintptr_t kConstantBufferMemmoveEndRva =
	0x136814;

[[nodiscard]] inline constexpr bool ShouldHandleConstantBufferException(
	std::uint32_t exceptionCode,
	std::uintptr_t exceptionRva,
	std::uintptr_t faultAddress) noexcept
{
	return exceptionCode == kAccessViolationExceptionCode
		&& (faultAddress < 0x10000
			|| (exceptionRva >= kConstantBufferMemmoveStartRva
				&& exceptionRva < kConstantBufferMemmoveEndRva));
}

inline constexpr std::uint8_t kExpectedDrawFramePointerRestoreNop[] = {
	0x66, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kRestoreDrawFramePointer[] = {
	0x48, 0x8D, 0xAC, 0x24, 0xD1, 0x00, 0x00, 0x00, 0x90
};

[[nodiscard]] inline bool ComputeDrawFramePointer(
	std::uintptr_t stackPointer,
	std::uintptr_t* framePointer)
{
	if (!framePointer
		|| stackPointer > std::numeric_limits<std::uintptr_t>::max()
			- kDrawFramePointerOffsetFromRsp) {
		return false;
	}
	*framePointer = stackPointer + kDrawFramePointerOffsetFromRsp;
	return true;
}

}
