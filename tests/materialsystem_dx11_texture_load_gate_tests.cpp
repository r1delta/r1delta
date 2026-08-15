#include "../engine/core/materialsystem_dx11_texture_load_gate.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

namespace
{
	using r1delta::materialsystem_dx11::TextureLoadScratchBufferGate;

	bool Check(bool condition, const char* message)
	{
		if (!condition)
			std::cerr << "FAILED: " << message << '\n';
		return condition;
	}

	bool TestLoadScratchBufferLifetimeIsSerializedAndRecursive()
	{
		TextureLoadScratchBufferGate gate;
		constexpr int threadCount = 32;
		constexpr int iterations = 256;
		std::atomic<bool> start{ false };
		std::atomic<int> ready{ 0 };
		std::atomic<int> entered{ 0 };
		std::atomic<int> active{ 0 };
		std::atomic<int> violations{ 0 };
		std::atomic<unsigned int> scratchWord{ 0 };
		std::vector<std::thread> threads;
		threads.reserve(threadCount);

		for (int threadIndex = 0; threadIndex < threadCount; ++threadIndex) {
			threads.emplace_back([&, threadIndex]() {
				ready.fetch_add(1, std::memory_order_release);
				while (!start.load(std::memory_order_acquire))
					std::this_thread::yield();
				entered.fetch_add(1, std::memory_order_release);
				while (entered.load(std::memory_order_acquire) != threadCount)
					std::this_thread::yield();

				for (int iteration = 0; iteration < iterations; ++iteration) {
					auto outerLock = gate.Acquire();
					auto recursiveLock = gate.Acquire();
					if (!outerLock.owns_lock() || !recursiveLock.owns_lock())
						violations.fetch_add(1, std::memory_order_relaxed);
					if (active.fetch_add(1, std::memory_order_acq_rel) != 0)
						violations.fetch_add(1, std::memory_order_relaxed);
					const unsigned int value = static_cast<unsigned int>((threadIndex + 1) * 1000 + iteration);
					scratchWord.store(value, std::memory_order_release);
					std::this_thread::yield();
					if (scratchWord.load(std::memory_order_acquire) != value)
						violations.fetch_add(1, std::memory_order_relaxed);
					active.fetch_sub(1, std::memory_order_acq_rel);
				}
			});
		}

		while (ready.load(std::memory_order_acquire) != threadCount)
			std::this_thread::yield();
		start.store(true, std::memory_order_release);
		for (auto& thread : threads)
			thread.join();

		return Check(violations.load(std::memory_order_relaxed) == 0,
			"recursive or concurrent texture loads violated the shared scratch-buffer lifetime");
	}
}

int main()
{
	if (!TestLoadScratchBufferLifetimeIsSerializedAndRecursive())
		return EXIT_FAILURE;
	std::cout << "All materialsystem DX11 texture load gate tests passed\n";
	return EXIT_SUCCESS;
}
