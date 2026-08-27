#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace r1delta::materialsystem_dx11
{
inline constexpr std::uintptr_t kConstantBufferFlushRva = 0x184F0;
inline constexpr std::uintptr_t kDrawFramePointerRestoreRva = 0xCFF7;
inline constexpr std::uintptr_t kDrawFramePointerOffsetFromRsp = 0xD1;

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
