#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace r1delta::materialsystem_dx11
{
inline constexpr std::uintptr_t kCreateD3D11DeviceRva = 0x21F0;
inline constexpr std::uintptr_t kImmediateContextRva = 0x290D90;
inline constexpr std::uintptr_t kDrawFramePointerRestoreRva = 0xCFF7;
inline constexpr std::uintptr_t kDrawFramePointerOffsetFromRsp = 0xD1;

inline constexpr std::uint8_t kExpectedCreateD3D11DevicePrologue[] = {
	0x4C, 0x8B, 0xDC, 0x48, 0x83, 0xEC, 0x68, 0x48,
	0x89, 0x0D, 0x82, 0xEB, 0x28, 0x00, 0x48, 0x8D,
	0x05, 0x2B, 0xF8, 0xFF, 0xFF, 0x49, 0x8D, 0x4B,
	0x18, 0xBA, 0x01, 0x00, 0x00, 0x00, 0x49, 0x89
};

inline constexpr std::uint8_t kExpectedDrawFramePointerRestoreNop[] = {
	0x66, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00
};

inline constexpr std::uint8_t kRestoreDrawFramePointer[] = {
	0x48, 0x8D, 0xAC, 0x24, 0xD1, 0x00, 0x00, 0x00, 0x90
};

enum class MultithreadProtectionResult
{
	enabled,
	contextMissing,
	operationsMissing,
	queryFailed,
	interfaceMissing,
	enableFailed
};

using QueryMultithreadInterface = long(__fastcall*)(
	void* context,
	void** multithreadInterface);
using SetMultithreadProtected = int(__fastcall*)(
	void* multithreadInterface,
	int enabled);
using GetMultithreadProtected = int(__fastcall*)(
	void* multithreadInterface);
using ReleaseMultithreadInterface = unsigned long(__fastcall*)(
	void* multithreadInterface);

struct MultithreadProtectionOperations
{
	QueryMultithreadInterface query;
	SetMultithreadProtected setProtected;
	GetMultithreadProtected getProtected;
	ReleaseMultithreadInterface release;
};

struct MultithreadProtectionReport
{
	MultithreadProtectionResult result;
	bool wasProtected;
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

[[nodiscard]] inline MultithreadProtectionReport EnableMultithreadProtection(
	void* context,
	const MultithreadProtectionOperations& operations)
{
	if (!context)
		return { MultithreadProtectionResult::contextMissing, false };
	if (!operations.query
		|| !operations.setProtected
		|| !operations.getProtected
		|| !operations.release) {
		return { MultithreadProtectionResult::operationsMissing, false };
	}

	void* multithreadInterface = nullptr;
	const long queryResult = operations.query(context, &multithreadInterface);
	if (queryResult < 0) {
		if (multithreadInterface)
			operations.release(multithreadInterface);
		return { MultithreadProtectionResult::queryFailed, false };
	}
	if (!multithreadInterface)
		return { MultithreadProtectionResult::interfaceMissing, false };

	const bool wasProtected = operations.setProtected(
		multithreadInterface,
		1) != 0;
	const bool enabled = operations.getProtected(multithreadInterface) != 0;
	operations.release(multithreadInterface);
	return {
		enabled
			? MultithreadProtectionResult::enabled
			: MultithreadProtectionResult::enableFailed,
		wasProtected
	};
}
}
