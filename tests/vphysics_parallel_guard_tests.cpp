#include "../server/physics/vphysics_parallel_guard.h"

#include <iostream>
#include <limits>

namespace
{
using namespace r1delta::vphysics;

bool Check(bool condition, const char* message)
{
	if (!condition)
		std::cerr << message << '\n';
	return condition;
}

bool TestRetailHookConstants()
{
	return Check(kSequentialDispatcherRva == 0x103120, "unexpected dispatcher RVA")
		&& Check(kParallelConfigurationRva == 0x1EF1D0, "unexpected parallel configuration RVA")
		&& Check(kParallelEnabledRva == 0x1EF22C, "unexpected parallel flag RVA")
		&& Check(kSequentialWorkerPointerRva == 0x1EF258, "unexpected worker pointer RVA")
		&& Check(kSequentialBatchOffset == 0x100078, "unexpected sequential batch offset")
		&& Check(sizeof(kSequentialDispatcherExpectedPrologue) == 32, "unexpected dispatcher prologue size")
		&& Check(kSequentialDispatcherExpectedPrologue[0] == 0x48, "unexpected dispatcher prologue start")
		&& Check(kSequentialDispatcherExpectedPrologue[15] == 0x57, "unexpected dispatcher nonvolatile save")
		&& Check(kSequentialDispatcherExpectedPrologue[31] == 0x0D, "unexpected dispatcher prologue end");
}

bool TestOverridesAndRestoresParallelState()
{
	int parallelEnabled = 1;
	std::uintptr_t workerPointer = 0x12345678;
	constexpr std::uintptr_t owner = 0x20000000;
	{
		ScopedSequentialDispatcherState state(
			&parallelEnabled,
			&workerPointer,
			owner);
		if (!Check(state.Entered(), "valid sequential state was rejected")
			|| !Check(parallelEnabled == 0, "parallel processing remained enabled")
			|| !Check(workerPointer == owner + kSequentialBatchOffset, "sequential worker pointer mismatch")) {
			return false;
		}
	}
	return Check(parallelEnabled == 1, "parallel state was not restored")
		&& Check(workerPointer == 0x12345678, "worker pointer was not restored");
}

bool TestAlreadySequentialStateRestoresExactly()
{
	int parallelEnabled = 0;
	std::uintptr_t workerPointer = 0;
	{
		ScopedSequentialDispatcherState state(
			&parallelEnabled,
			&workerPointer,
			0x40000000);
		if (!Check(state.Entered(), "already-sequential state was rejected")
			|| !Check(parallelEnabled == 0, "already-sequential flag changed")
			|| !Check(workerPointer == 0x40100078, "already-sequential worker pointer mismatch")) {
			return false;
		}
	}
	return Check(parallelEnabled == 0, "already-sequential flag was not restored")
		&& Check(workerPointer == 0, "null worker pointer was not restored");
}

bool TestNestedScopesRestoreOuterState()
{
	int parallelEnabled = 1;
	std::uintptr_t workerPointer = 0x1111;
	{
		ScopedSequentialDispatcherState outer(
			&parallelEnabled,
			&workerPointer,
			0x50000000);
		{
			ScopedSequentialDispatcherState inner(
				&parallelEnabled,
				&workerPointer,
				0x60000000);
			if (!Check(inner.Entered(), "nested sequential state was rejected")
				|| !Check(parallelEnabled == 0, "nested state enabled parallel processing")
				|| !Check(workerPointer == 0x60100078, "nested worker pointer mismatch")) {
				return false;
			}
		}
		if (!Check(parallelEnabled == 0, "inner scope did not restore outer parallel state")
			|| !Check(workerPointer == 0x50100078, "inner scope did not restore outer worker pointer")) {
			return false;
		}
	}
	return Check(parallelEnabled == 1, "outer scope did not restore parallel state")
		&& Check(workerPointer == 0x1111, "outer scope did not restore worker pointer");
}

bool TestInvalidInputsFailClosed()
{
	int parallelEnabled = 1;
	std::uintptr_t workerPointer = 0x2222;
	ScopedSequentialDispatcherState missingFlag(
		nullptr,
		&workerPointer,
		0x70000000);
	ScopedSequentialDispatcherState missingWorker(
		&parallelEnabled,
		nullptr,
		0x70000000);
	ScopedSequentialDispatcherState overflowingOwner(
		&parallelEnabled,
		&workerPointer,
		std::numeric_limits<std::uintptr_t>::max());
	return Check(!missingFlag.Entered(), "missing parallel flag was accepted")
		&& Check(!missingWorker.Entered(), "missing worker pointer was accepted")
		&& Check(!overflowingOwner.Entered(), "overflowing owner was accepted")
		&& Check(parallelEnabled == 1, "invalid state changed parallel flag")
		&& Check(workerPointer == 0x2222, "invalid state changed worker pointer");
}
}

int main()
{
	const bool passed = TestRetailHookConstants()
		&& TestOverridesAndRestoresParallelState()
		&& TestAlreadySequentialStateRestoresExactly()
		&& TestNestedScopesRestoreOuterState()
		&& TestInvalidInputsFailClosed();
	if (!passed)
		return 1;
	std::cout << "vphysics_parallel_guard_tests passed\n";
	return 0;
}
