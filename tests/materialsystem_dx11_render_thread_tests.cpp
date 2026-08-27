#include "../engine/core/materialsystem_dx11_render_thread.h"

#include <iostream>
#include <limits>

namespace
{
using namespace r1delta::materialsystem_dx11;


bool Check(bool condition, const char* message)
{
	if (!condition)
		std::cerr << message << '\n';
	return condition;
}


bool TestRetailHookConstants()
{
	return Check(
			kConstantBufferFlushRva == 0x184F0,
			"unexpected constant-buffer flush RVA")
		&& Check(
			kDrawFramePointerRestoreRva == 0xCFF7,
			"unexpected frame-restore RVA")
		&& Check(
			kDrawFramePointerOffsetFromRsp == 0xD1,
			"unexpected frame-pointer offset")
		&& Check(
			sizeof(kExpectedConstantBufferFlushPrologue) == 35,
			"unexpected constant-buffer prologue size")
		&& Check(
			kExpectedConstantBufferFlushPrologue[0] == 0x48,
			"unexpected constant-buffer prologue start")
		&& Check(
			kExpectedConstantBufferFlushPrologue[34] == 0x00,
			"unexpected constant-buffer prologue end")
		&& Check(
			sizeof(kExpectedDrawFramePointerRestoreNop) == 9,
			"unexpected frame-restore NOP size")
		&& Check(
			sizeof(kRestoreDrawFramePointer) == 9,
			"unexpected frame-restore patch size")
		&& Check(
			kExpectedDrawFramePointerRestoreNop[0] == 0x66,
			"unexpected frame-restore NOP start")
		&& Check(
			kRestoreDrawFramePointer[0] == 0x48,
			"unexpected frame-restore patch start")
		&& Check(
			kRestoreDrawFramePointer[4] == 0xD1,
			"unexpected frame-restore displacement")
		&& Check(
			kRestoreDrawFramePointer[8] == 0x90,
			"unexpected frame-restore patch padding");
}

bool TestConstantBufferExceptionFilter()
{
	return Check(
			ShouldHandleConstantBufferException(
				kAccessViolationExceptionCode,
				0x136730,
				0x28),
			"prerelease-26 constant-buffer AV was rejected")
		&& Check(
			ShouldHandleConstantBufferException(
				kAccessViolationExceptionCode,
				0x1366E4,
				0x10000),
			"sibling memmove AV was rejected")
		&& Check(
			ShouldHandleConstantBufferException(
				kAccessViolationExceptionCode,
				kConstantBufferMemmoveStartRva,
				0x10000),
			"memmove range start was rejected")
		&& Check(
			!ShouldHandleConstantBufferException(
				kAccessViolationExceptionCode,
				kConstantBufferMemmoveEndRva,
				0x10000),
			"memmove range end was accepted")
		&& Check(
			ShouldHandleConstantBufferException(
				kAccessViolationExceptionCode,
				0x50000,
				0xFFF8),
			"near-null AV was rejected")
		&& Check(
			!ShouldHandleConstantBufferException(
				kAccessViolationExceptionCode,
				0x50000,
				0x10000),
			"unrelated AV was accepted")
		&& Check(
			!ShouldHandleConstantBufferException(
				0xC0000094,
				0x136730,
				0x28),
			"non-AV exception was accepted");
}

bool TestCrashFramePointerReconstruction()
{
	constexpr std::uintptr_t crashStackPointer = 0x000000708EA09220;
	constexpr std::uintptr_t expectedFramePointer = 0x000000708EA092F1;
	constexpr std::uintptr_t corruptedFramePointer = 0x000000708EA093F0;
	std::uintptr_t framePointer = 0;
	const bool reconstructed = ComputeDrawFramePointer(
		crashStackPointer,
		&framePointer);
	std::uintptr_t overflowFramePointer = 0;
	const bool overflowAccepted = ComputeDrawFramePointer(
		std::numeric_limits<std::uintptr_t>::max(),
		&overflowFramePointer);
	return Check(reconstructed, "crash frame pointer was not reconstructed")
		&& Check(framePointer == expectedFramePointer, "crash frame pointer reconstruction mismatch")
		&& Check(framePointer != corruptedFramePointer, "corrupted crash frame pointer was retained")
		&& Check(!overflowAccepted, "overflowing stack pointer was accepted")
		&& Check(!ComputeDrawFramePointer(crashStackPointer, nullptr), "null frame-pointer output was accepted");
}

}

int main()
{
	const bool passed = TestRetailHookConstants()
		&& TestConstantBufferExceptionFilter()
		&& TestCrashFramePointerReconstruction();
	if (!passed)
		return 1;
	std::cout << "materialsystem_dx11_render_thread_tests passed\n";
	return 0;
}
