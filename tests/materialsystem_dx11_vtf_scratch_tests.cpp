#include "../engine/core/materialsystem_dx11_vtf_scratch.h"

#include <algorithm>
#include <atomic>
#include <barrier>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <iostream>
#include <thread>
#include <vector>

namespace
{
	using namespace r1delta::materialsystem_dx11;

	bool Check(bool condition, const char* message)
	{
		if (!condition)
			std::cerr << "FAILED: " << message << '\n';
		return condition;
	}

	struct FakeDecoder
	{
		unsigned char marker = 0;
	};

	int decoderCreates = 0;
	int decoderDestroys = 0;
	int decoderDestroyFlags = 0;

	void ResetFakeDecoderCounters()
	{
		decoderCreates = 0;
		decoderDestroys = 0;
		decoderDestroyFlags = 0;
	}

	void* CreateFakeDecoder()
	{
		++decoderCreates;
		return new (std::nothrow) FakeDecoder;
	}

	void* FailFakeDecoderCreation()
	{
		++decoderCreates;
		return nullptr;
	}

	void* DestroyFakeDecoder(void* decoder, unsigned char flags)
	{
		++decoderDestroys;
		decoderDestroyFlags += flags;
		delete static_cast<FakeDecoder*>(decoder);
		return decoder;
	}

	bool TestNestedLoadsOwnDistinctDecoderObjects()
	{
		ResetFakeDecoderCounters();
		FakeDecoder outerDecoder{ 0x5A };
		void* decoderSlot = &outerDecoder;
		bool passed = true;

		VtfDecoderSlotScope outer(
			1,
			&decoderSlot,
			CreateFakeDecoder,
			DestroyFakeDecoder);
		passed &= Check(outer.Entered() && !outer.Replaced(),
			"outer load replaced the stock decoder");
		passed &= Check(decoderSlot == &outerDecoder && decoderCreates == 0,
			"outer load changed or allocated a decoder");

		VtfDecoderSlotScope nested(
			2,
			&decoderSlot,
			CreateFakeDecoder,
			DestroyFakeDecoder);
		passed &= Check(nested.Entered() && nested.Replaced(),
			"recursive load did not replace the stock decoder");
		void* const nestedDecoder = decoderSlot;
		passed &= Check(nestedDecoder && nestedDecoder != &outerDecoder,
			"recursive load reused its caller's decoder");
		static_cast<FakeDecoder*>(nestedDecoder)->marker = 0xA5;
		passed &= Check(outerDecoder.marker == 0x5A,
			"recursive decoder mutation reached its caller");

		VtfDecoderSlotScope recursive(
			3,
			&decoderSlot,
			CreateFakeDecoder,
			DestroyFakeDecoder);
		passed &= Check(
			recursive.Entered()
				&& recursive.Replaced()
				&& decoderSlot != nestedDecoder,
			"third-level load reused its caller's decoder");
		passed &= Check(recursive.Close() && decoderSlot == nestedDecoder,
			"third-level load did not restore its caller's decoder");
		passed &= Check(nested.Close() && decoderSlot == &outerDecoder,
			"recursive load did not restore the stock decoder");
		passed &= Check(outer.Close(),
			"outer decoder scope did not close");
		passed &= Check(
			decoderCreates == 2
				&& decoderDestroys == 2
				&& decoderDestroyFlags == 2,
			"recursive decoders were not destroyed exactly once");

		VtfDecoderSlotScope noLoad(
			0,
			&decoderSlot,
			CreateFakeDecoder,
			DestroyFakeDecoder);
		passed &= Check(
			!noLoad.Entered()
				&& noLoad.Error() == VtfScratchError::noActiveLoad,
			"decoder scope accepted zero load depth");

		VtfDecoderSlotScope invalidSlot(
			2,
			nullptr,
			CreateFakeDecoder,
			DestroyFakeDecoder);
		passed &= Check(
			!invalidSlot.Entered()
				&& invalidSlot.Error() == VtfScratchError::invalidDecoderSlot,
			"recursive decoder scope accepted a missing slot");

		VtfDecoderSlotScope allocationFailure(
			2,
			&decoderSlot,
			FailFakeDecoderCreation,
			DestroyFakeDecoder);
		passed &= Check(
			!allocationFailure.Entered()
				&& allocationFailure.Error()
					== VtfScratchError::decoderAllocationFailed
				&& decoderSlot == &outerDecoder,
			"decoder allocation failure changed the stock slot");
		return passed;
	}

	bool TestNestedLoadsOwnDistinctScratchFrames()
	{
		VtfScratchContext context;
		bool passed = true;
		passed &= Check(!context.LeaveLoad(), "unbalanced leave was accepted");
		passed &= Check(
			context.Prepare(1).error == VtfScratchError::noActiveLoad,
			"scratch allocation without an active load was accepted");
		passed &= Check(context.EnterLoad(), "outer load entry failed");

		const VtfScratchBuffer outer = context.Prepare(1024);
		passed &= Check(outer && outer.capacity == 0x200000,
			"outer load did not receive the stock two-MiB minimum capacity");
		if (!outer)
			return false;
		std::memset(outer.data, 0x5A, 64);

		const VtfScratchBuffer repeated = context.Prepare(0x100000);
		passed &= Check(repeated.data == outer.data && repeated.capacity == outer.capacity,
			"repeated reads in one load did not reuse its frame");
		passed &= Check(context.EnterLoad(), "recursive load entry failed");

		const VtfScratchBuffer nested = context.Prepare(1024);
		passed &= Check(nested && nested.data != outer.data,
			"recursive load aliased its caller's scratch buffer");
		if (nested)
			std::memset(nested.data, 0xA5, 64);
		passed &= Check(
			static_cast<const unsigned char*>(outer.data)[0] == 0x5A,
			"recursive load overwrote its caller's scratch data");
		passed &= Check(context.LeaveLoad(), "recursive load leave failed");

		const VtfScratchBuffer resumed = context.Prepare(1024);
		passed &= Check(resumed.data == outer.data,
			"outer load did not resume its original frame after recursion");
		passed &= Check(context.LeaveLoad() && context.Depth() == 0,
			"outer load leave did not restore zero depth");
		passed &= Check(context.FrameCount() == 2,
			"recursive frame ownership was not retained independently");
		return passed;
	}

	bool TestCapacityGrowthAndLargeBufferRetirement()
	{
		VtfScratchContext context;
		bool passed = true;
		passed &= Check(context.EnterLoad(), "growth test load entry failed");
		const VtfScratchBuffer minimum = context.Prepare(1);
		passed &= Check(minimum && minimum.capacity == 0x200000,
			"minimum capacity changed");

		const VtfScratchBuffer grown = context.Prepare(0x200001);
		passed &= Check(grown && grown.capacity == 0x210000,
			"scratch growth was not rounded to VirtualAlloc alignment");
		passed &= Check(context.LeaveLoad(), "retained buffer leave failed");
		passed &= Check(context.EnterLoad(), "retained buffer reentry failed");
		const VtfScratchBuffer retained = context.Prepare(1);
		passed &= Check(retained.data == grown.data && retained.capacity == grown.capacity,
			"sub-four-MiB scratch buffer was not retained for its thread");
		passed &= Check(context.LeaveLoad(), "retained buffer second leave failed");

		passed &= Check(context.EnterLoad(), "large buffer load entry failed");
		const VtfScratchBuffer large = context.Prepare(0x400000);
		passed &= Check(large && large.capacity == 0x400000,
			"large scratch allocation failed");
		passed &= Check(context.LeaveLoad(), "large buffer leave failed");
		passed &= Check(context.EnterLoad(), "post-retirement load entry failed");
		const VtfScratchBuffer afterRetirement = context.Prepare(1);
		passed &= Check(afterRetirement && afterRetirement.capacity == 0x200000,
			"four-MiB scratch buffer was retained instead of retired");
		passed &= Check(context.LeaveLoad(), "post-retirement leave failed");

		passed &= Check(context.EnterLoad(), "invalid size load entry failed");
		passed &= Check(
			context.Prepare(-1).error == VtfScratchError::invalidSize,
			"negative scratch size was accepted");
		passed &= Check(context.LeaveLoad(), "invalid size load leave failed");
		return passed;
	}

	bool TestProductionThreadScopeHandlesRecursion()
	{
		bool passed = true;
		passed &= Check(
			PrepareThreadVtfScratch(1).error == VtfScratchError::noActiveLoad,
			"production TLS prepared scratch outside a load scope");

		VtfScratchLoadScope outerScope;
		passed &= Check(
			outerScope.Entered() && ThreadVtfScratchDepth() == 1,
			"production TLS outer scope did not enter at depth one");
		const VtfScratchBuffer outer = PrepareThreadVtfScratch(1);
		passed &= Check(
			static_cast<bool>(outer),
			"production TLS outer scope did not allocate scratch");

		VtfScratchLoadScope nestedScope;
		passed &= Check(
			nestedScope.Entered() && ThreadVtfScratchDepth() == 2,
			"production TLS recursive scope did not enter at depth two");
		const VtfScratchBuffer nested = PrepareThreadVtfScratch(1);
		passed &= Check(
			nested && nested.data != outer.data,
			"production TLS recursive scope aliased its caller");
		passed &= Check(
			nestedScope.Close() && ThreadVtfScratchDepth() == 1,
			"production TLS recursive scope did not restore depth one");

		const VtfScratchBuffer resumed = PrepareThreadVtfScratch(1);
		passed &= Check(
			resumed.data == outer.data,
			"production TLS outer scope did not recover its frame");
		passed &= Check(
			outerScope.Close() && ThreadVtfScratchDepth() == 0,
			"production TLS outer scope did not restore depth zero");
		return passed;
	}

	bool TestConcurrentLoadsNeverShareOrSerializeScratch()
	{
		constexpr int threadCount = 16;
		std::vector<void*> addresses(threadCount);
		std::vector<std::thread> threads;
		std::barrier rendezvous(threadCount + 1);
		std::atomic<int> active{ 0 };
		std::atomic<int> maximumActive{ 0 };
		std::atomic<int> failures{ 0 };
		std::atomic<bool> release{ false };

		threads.reserve(threadCount);
		for (int index = 0; index < threadCount; ++index) {
			threads.emplace_back([&, index]() {
				VtfScratchLoadScope scope;
				const bool entered = scope.Entered();
				VtfScratchBuffer buffer;
				if (entered)
					buffer = PrepareThreadVtfScratch(0x100000 + index);
				if (entered && ThreadVtfScratchDepth() != 1)
					failures.fetch_add(1, std::memory_order_relaxed);
				if (!entered || !buffer) {
					failures.fetch_add(1, std::memory_order_relaxed);
				}
				else {
					addresses[index] = buffer.data;
					static_cast<unsigned char*>(buffer.data)[index] =
						static_cast<unsigned char>(index + 1);
					const int current = active.fetch_add(1, std::memory_order_acq_rel) + 1;
					int observed = maximumActive.load(std::memory_order_relaxed);
					while (current > observed
						&& !maximumActive.compare_exchange_weak(
							observed,
							current,
							std::memory_order_relaxed)) {
					}
				}

				rendezvous.arrive_and_wait();
				while (!release.load(std::memory_order_acquire))
					std::this_thread::yield();
				if (buffer)
					active.fetch_sub(1, std::memory_order_acq_rel);
				if (entered
					&& (!scope.Close() || ThreadVtfScratchDepth() != 0)) {
					failures.fetch_add(1, std::memory_order_relaxed);
				}
			});
		}

		rendezvous.arrive_and_wait();
		release.store(true, std::memory_order_release);
		for (std::thread& thread : threads)
			thread.join();

		std::sort(addresses.begin(), addresses.end());
		bool passed = true;
		passed &= Check(failures.load(std::memory_order_relaxed) == 0,
			"a concurrent scratch lifecycle operation failed");
		passed &= Check(maximumActive.load(std::memory_order_relaxed) == threadCount,
			"concurrent scratch loads were serialized");
		passed &= Check(
			std::adjacent_find(addresses.begin(), addresses.end()) == addresses.end(),
			"concurrent threads received the same scratch allocation");
		return passed;
	}
}

int main()
{
	bool passed = true;
	passed &= TestNestedLoadsOwnDistinctScratchFrames();
	passed &= TestCapacityGrowthAndLargeBufferRetirement();
	passed &= TestProductionThreadScopeHandlesRecursion();
	passed &= TestConcurrentLoadsNeverShareOrSerializeScratch();
	passed &= TestNestedLoadsOwnDistinctDecoderObjects();
	if (!passed)
		return EXIT_FAILURE;
	std::cout << "All materialsystem DX11 VTF scratch tests passed\n";
	return EXIT_SUCCESS;
}
