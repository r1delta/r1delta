#include "../engine/core/materialsystem_dx11_multithread.h"

#include <iostream>
#include <limits>

namespace
{
using namespace r1delta::materialsystem_dx11;

struct FakeMultithread
{
	long queryResult;
	bool provideInterface;
	bool protectedState;
	bool refuseEnable;
	int queryCalls;
	int setCalls;
	int getCalls;
	int releaseCalls;
};

FakeMultithread* g_fake;

bool Check(bool condition, const char* message)
{
	if (!condition)
		std::cerr << message << '\n';
	return condition;
}

long __fastcall QueryMultithread(void*, void** multithreadInterface)
{
	++g_fake->queryCalls;
	*multithreadInterface = g_fake->provideInterface ? g_fake : nullptr;
	return g_fake->queryResult;
}

int __fastcall SetProtected(void* multithreadInterface, int enabled)
{
	auto* const fake = static_cast<FakeMultithread*>(multithreadInterface);
	++fake->setCalls;
	const bool previous = fake->protectedState;
	if (!fake->refuseEnable)
		fake->protectedState = enabled != 0;
	return previous ? 1 : 0;
}

int __fastcall GetProtected(void* multithreadInterface)
{
	auto* const fake = static_cast<FakeMultithread*>(multithreadInterface);
	++fake->getCalls;
	return fake->protectedState ? 1 : 0;
}

unsigned long __fastcall ReleaseMultithread(void* multithreadInterface)
{
	auto* const fake = static_cast<FakeMultithread*>(multithreadInterface);
	++fake->releaseCalls;
	return 1;
}

MultithreadProtectionOperations Operations()
{
	return {
		&QueryMultithread,
		&SetProtected,
		&GetProtected,
		&ReleaseMultithread
	};
}

FakeMultithread State(
	long queryResult = 0,
	bool provideInterface = true,
	bool protectedState = false,
	bool refuseEnable = false)
{
	return {
		queryResult,
		provideInterface,
		protectedState,
		refuseEnable,
		0,
		0,
		0,
		0
	};
}

bool TestRetailHookConstants()
{
	return Check(kCreateD3D11DeviceRva == 0x21F0, "unexpected create-device RVA")
		&& Check(kImmediateContextRva == 0x290D90, "unexpected immediate-context RVA")
		&& Check(kDrawFramePointerRestoreRva == 0xCFF7, "unexpected frame-restore RVA")
		&& Check(kDrawFramePointerOffsetFromRsp == 0xD1, "unexpected frame-pointer offset")
		&& Check(sizeof(kExpectedCreateD3D11DevicePrologue) == 32, "unexpected create-device prologue size")
		&& Check(kExpectedCreateD3D11DevicePrologue[0] == 0x4C, "unexpected create-device prologue start")
		&& Check(kExpectedCreateD3D11DevicePrologue[31] == 0x89, "unexpected create-device prologue end")
		&& Check(sizeof(kExpectedDrawFramePointerRestoreNop) == 9, "unexpected frame-restore NOP size")
		&& Check(sizeof(kRestoreDrawFramePointer) == 9, "unexpected frame-restore patch size")
		&& Check(kExpectedDrawFramePointerRestoreNop[0] == 0x66, "unexpected frame-restore NOP start")
		&& Check(kRestoreDrawFramePointer[0] == 0x48, "unexpected frame-restore patch start")
		&& Check(kRestoreDrawFramePointer[4] == 0xD1, "unexpected frame-restore displacement")
		&& Check(kRestoreDrawFramePointer[8] == 0x90, "unexpected frame-restore patch padding");
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

bool TestEnablesAndReleasesInterface()
{
	FakeMultithread fake = State();
	g_fake = &fake;
	const auto report = EnableMultithreadProtection(&fake, Operations());
	return Check(report.result == MultithreadProtectionResult::enabled, "protection was not enabled")
		&& Check(!report.wasProtected, "fresh context reported prior protection")
		&& Check(fake.protectedState, "set operation did not enable protection")
		&& Check(fake.queryCalls == 1, "query call count mismatch")
		&& Check(fake.setCalls == 1, "set call count mismatch")
		&& Check(fake.getCalls == 1, "get call count mismatch")
		&& Check(fake.releaseCalls == 1, "release call count mismatch");
}

bool TestAlreadyProtectedContextRemainsEnabled()
{
	FakeMultithread fake = State(0, true, true);
	g_fake = &fake;
	const auto report = EnableMultithreadProtection(&fake, Operations());
	return Check(report.result == MultithreadProtectionResult::enabled, "protected context failed verification")
		&& Check(report.wasProtected, "prior protection was not reported")
		&& Check(fake.releaseCalls == 1, "protected interface was not released");
}

bool TestQueryFailureReleasesReturnedInterface()
{
	FakeMultithread fake = State(-1, true);
	g_fake = &fake;
	const auto report = EnableMultithreadProtection(&fake, Operations());
	return Check(report.result == MultithreadProtectionResult::queryFailed, "query failure was not reported")
		&& Check(fake.setCalls == 0, "set ran after query failure")
		&& Check(fake.getCalls == 0, "get ran after query failure")
		&& Check(fake.releaseCalls == 1, "failed query interface was not released");
}

bool TestMissingInterfaceFailsClosed()
{
	FakeMultithread fake = State(0, false);
	g_fake = &fake;
	const auto report = EnableMultithreadProtection(&fake, Operations());
	return Check(report.result == MultithreadProtectionResult::interfaceMissing, "missing interface was not reported")
		&& Check(fake.releaseCalls == 0, "null interface was released");
}

bool TestFailedEnableVerificationFailsClosed()
{
	FakeMultithread fake = State(0, true, false, true);
	g_fake = &fake;
	const auto report = EnableMultithreadProtection(&fake, Operations());
	return Check(report.result == MultithreadProtectionResult::enableFailed, "failed enable was not reported")
		&& Check(!report.wasProtected, "failed enable reported prior protection")
		&& Check(fake.releaseCalls == 1, "failed-enable interface was not released");
}

bool TestMissingInputsFailClosed()
{
	FakeMultithread fake = State();
	g_fake = &fake;
	const auto operations = Operations();
	const auto missingContext = EnableMultithreadProtection(nullptr, operations);
	const MultithreadProtectionOperations missingOperations{};
	const auto missingCallbacks = EnableMultithreadProtection(&fake, missingOperations);
	return Check(missingContext.result == MultithreadProtectionResult::contextMissing, "missing context was not reported")
		&& Check(missingCallbacks.result == MultithreadProtectionResult::operationsMissing, "missing operations were not reported")
		&& Check(fake.queryCalls == 0, "query ran with invalid inputs");
}
}

int main()
{
	const bool passed = TestRetailHookConstants()
		&& TestCrashFramePointerReconstruction()
		&& TestEnablesAndReleasesInterface()
		&& TestAlreadyProtectedContextRemainsEnabled()
		&& TestQueryFailureReleasesReturnedInterface()
		&& TestMissingInterfaceFailsClosed()
		&& TestFailedEnableVerificationFailsClosed()
		&& TestMissingInputsFailClosed();
	if (!passed)
		return 1;
	std::cout << "materialsystem_dx11_multithread_tests passed\n";
	return 0;
}
