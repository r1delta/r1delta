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

bool TryHoldShaderResourceForTest(
	void* shaderResource,
	void** heldShaderResource)
{
	if (!heldShaderResource
		|| shaderResource == reinterpret_cast<void*>(2)) {
		return false;
	}
	*heldShaderResource = shaderResource;
	return true;
}
int g_releasedShaderResources = 0;


void ReleaseShaderResourceForTest(void*)
{
	++g_releasedShaderResources;
}





bool TestRetailHookConstants()
{
	return Check(
			kConstantBufferFlushRva == 0x184F0,
			"unexpected constant-buffer flush RVA")
		&& Check(
			kShaderResourceFlushRva == 0x93D0,
			"unexpected shader-resource flush RVA")
		&& Check(
			kPendingShaderResourcesRva == 0x298730,
			"unexpected pending shader-resource RVA")
		&& Check(
			kPendingShaderResourceCountRva == 0x2987F0,
			"unexpected pending shader-resource count RVA")
		&& Check(
			kPreviousShaderResourceCountRva == 0x2987F4,
			"unexpected previous shader-resource count RVA")
		&& Check(
			kPendingShaderResourceCapacity == 24,
			"unexpected pending shader-resource capacity")
		&& Check(
			sizeof(kExpectedShaderResourceFlushPrologue) == 32,
			"unexpected shader-resource flush prologue size")
		&& Check(
			kExpectedShaderResourceFlushPrologue[0] == 0x40,
			"unexpected shader-resource flush prologue start")
		&& Check(
			kExpectedShaderResourceFlushPrologue[31] == 0x0D,
			"unexpected shader-resource flush prologue end")
		&& Check(
			kShaderResourceSingleProducerRva == 0x140E0,
			"unexpected single shader-resource producer RVA")
		&& Check(
			kShaderResourceIndexedProducerRva == 0x14220,
			"unexpected indexed shader-resource producer RVA")
		&& Check(
			kShaderResourceArrayProducerRva == 0x142D0,
			"unexpected array shader-resource producer RVA")
		&& Check(
			kShaderResourceMaskedArrayProducerRva == 0x14490,
			"unexpected masked-array shader-resource producer RVA")
		&& Check(
			kShaderResourceDirectProducerRva == 0x145A0,
			"unexpected direct shader-resource producer RVA")
		&& Check(
			sizeof(kExpectedShaderResourceSingleProducerPrologue) == 32,
			"unexpected single shader-resource producer prologue")
		&& Check(
			sizeof(kExpectedShaderResourceIndexedProducerPrologue) == 32,
			"unexpected indexed shader-resource producer prologue")
		&& Check(
			sizeof(kExpectedShaderResourceArrayProducerPrologue) == 32,
			"unexpected array shader-resource producer prologue")
		&& Check(
			sizeof(kExpectedShaderResourceMaskedArrayProducerPrologue) == 32,
			"unexpected masked-array shader-resource producer prologue")
		&& Check(
			sizeof(kExpectedShaderResourceDirectProducerPrologue) == 32,
			"unexpected direct shader-resource producer prologue")
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
bool TestShaderResourceOwnership()
{
	void* shaderResources[] = {
		reinterpret_cast<void*>(1),
		reinterpret_cast<void*>(2),
		nullptr,
		reinterpret_cast<void*>(3)
	};
	void* heldShaderResources[] = {
		reinterpret_cast<void*>(9),
		reinterpret_cast<void*>(9),
		reinterpret_cast<void*>(9),
		nullptr
	};
	g_releasedShaderResources = 0;

	const bool validAccepted = ReplacePendingShaderResourceHold(
		shaderResources,
		heldShaderResources,
		4,
		0,
		&TryHoldShaderResourceForTest,
		&ReleaseShaderResourceForTest);
	const bool zombieAccepted = ReplacePendingShaderResourceHold(
		shaderResources,
		heldShaderResources,
		4,
		1,
		&TryHoldShaderResourceForTest,
		&ReleaseShaderResourceForTest);
	const bool nullAccepted = ReplacePendingShaderResourceHold(
		shaderResources,
		heldShaderResources,
		4,
		2,
		&TryHoldShaderResourceForTest,
		&ReleaseShaderResourceForTest);

	return Check(validAccepted, "valid shader resource was rejected")
		&& Check(!zombieAccepted, "zombie shader resource was accepted")
		&& Check(nullAccepted, "null shader resource was rejected")
		&& Check(
			shaderResources[0] == reinterpret_cast<void*>(1),
			"valid shader resource was cleared")
		&& Check(
			shaderResources[1] == nullptr,
			"zombie shader resource was retained")
		&& Check(
			heldShaderResources[0] == reinterpret_cast<void*>(1),
			"valid shader resource was not held")
		&& Check(
			heldShaderResources[1] == nullptr,
			"zombie shader resource was held")
		&& Check(
			heldShaderResources[2] == nullptr,
			"null shader resource retained an old hold")
		&& Check(
			g_releasedShaderResources == 3,
			"replaced shader-resource holds were not released")
		&& Check(
			ClampShaderResourceCount(23) == 23,
			"in-range shader-resource count changed")
		&& Check(
			ClampShaderResourceCount(25) == kPendingShaderResourceCapacity,
			"oversized shader-resource count was not clamped")
		&& Check(
			!ReplacePendingShaderResourceHold(
				shaderResources,
				heldShaderResources,
				4,
				4,
				&TryHoldShaderResourceForTest,
				&ReleaseShaderResourceForTest),
			"out-of-range shader-resource slot was accepted");
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
		&& TestShaderResourceOwnership()
		&& TestCrashFramePointerReconstruction();
	if (!passed)
		return 1;
	std::cout << "materialsystem_dx11_render_thread_tests passed\n";
	return 0;
}
