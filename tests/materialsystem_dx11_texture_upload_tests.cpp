#include "../engine/core/materialsystem_dx11_texture_upload.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

namespace
{
	using namespace r1delta::materialsystem_dx11;

	int g_copyCalls = 0;
	int g_failCopyCall = 0;
	int g_readableCalls = 0;

	bool Check(bool condition, const char* message)
	{
		if (!condition)
			std::cerr << "FAILED: " << message << '\n';
		return condition;
	}

	bool Readable(const void* data, std::size_t size)
	{
		++g_readableCalls;
		return data && size;
	}

	bool Unreadable(const void*, std::size_t)
	{
		return false;
	}


	bool Copy(void* destination, const void* source, std::size_t size)
	{
		++g_copyCalls;
		if (g_failCopyCall && g_copyCalls == g_failCopyCall)
			return false;
		std::memcpy(destination, source, size);
		return true;
	}

	void ResetCallbacks(int failCopyCall = 0)
	{
		g_copyCalls = 0;
		g_failCopyCall = failCopyCall;
		g_readableCalls = 0;
	}

	bool TestDeepCopyOutlivesSources()
	{
		const std::array<unsigned char, 6> expectedFirst{ 1, 2, 3, 4, 5, 6 };
		const std::array<unsigned char, 9> expectedSecond{ 11, 12, 13, 14, 15, 16, 17, 18, 19 };
		TextureUploadCopyResult result;
		const void* sourcePointers[2]{};

		{
			auto first = expectedFirst;
			auto second = expectedSecond;
			TextureInitialDataEntry entries[] = {
				{ first.data(), 3, static_cast<unsigned int>(first.size()) },
				{ second.data(), 6, static_cast<unsigned int>(second.size()) },
			};
			sourcePointers[0] = entries[0].sysMem;
			sourcePointers[1] = entries[1].sysMem;
			ResetCallbacks();
			result = CopyTextureUpload(entries, 2, 16, 1024, &Readable, &Copy);
			first.fill(0);
			second.fill(0);
		}

		bool passed = true;
		passed &= Check(result.error == TextureUploadCopyError::none && result.upload,
			"valid upload was not copied");
		if (!result.upload)
			return false;
		passed &= Check(result.payloadBytes == expectedFirst.size() + expectedSecond.size(),
			"payload byte total changed");
		passed &= Check(result.upload->entries.size() == 2 && result.upload->payloads.size() == 2,
			"subresource count changed");
		passed &= Check(result.upload->entries[0].sysMem != sourcePointers[0]
			&& result.upload->entries[1].sysMem != sourcePointers[1],
			"payload pointers were not rebased");
		passed &= Check(result.upload->entries[0].sysMemPitch == 3
			&& result.upload->entries[0].sysMemSlicePitch == expectedFirst.size()
			&& result.upload->entries[1].sysMemPitch == 6
			&& result.upload->entries[1].sysMemSlicePitch == expectedSecond.size(),
			"descriptor order or pitch metadata changed");
		passed &= Check(std::equal(expectedFirst.begin(), expectedFirst.end(), result.upload->payloads[0].begin())
			&& std::equal(expectedSecond.begin(), expectedSecond.end(), result.upload->payloads[1].begin()),
			"copied payload did not outlive source storage");
		passed &= Check(result.upload->entries[0].sysMem == result.upload->payloads[0].data()
			&& result.upload->entries[1].sysMem == result.upload->payloads[1].data(),
			"descriptor pointers do not reference owned payloads");
		return passed;
	}

	bool TestMalformedPayloadsAreRejected()
	{
		bool passed = true;
		unsigned char payload[8]{};

		ResetCallbacks();
		auto result = CopyTextureUpload(nullptr, 1, 16, 1024, &Readable, &Copy);
		passed &= Check(result.error == TextureUploadCopyError::metadataInvalid && !result.upload,
			"null descriptor array was accepted");

		TextureInitialDataEntry zeroSlice{ payload, 4, 0 };
		ResetCallbacks();
		result = CopyTextureUpload(&zeroSlice, 1, 16, 1024, &Readable, &Copy);
		passed &= Check(result.error == TextureUploadCopyError::entryUnreadable && !result.upload,
			"zero slice pitch was accepted");

		TextureInitialDataEntry nullPayload{ nullptr, 4, 8 };
		ResetCallbacks();
		result = CopyTextureUpload(&nullPayload, 1, 16, 1024, &Readable, &Copy);
		passed &= Check(result.error == TextureUploadCopyError::entryUnreadable && !result.upload,
			"null payload was accepted");

		TextureInitialDataEntry oversized{ payload, 4, UINT_MAX };
		ResetCallbacks();
		result = CopyTextureUpload(&oversized, 1, 16, 64, &Readable, &Copy);
		passed &= Check(result.error == TextureUploadCopyError::payloadTooLarge && !result.upload,
			"oversized payload was accepted");
		passed &= Check(g_readableCalls == 1 && g_copyCalls == 1,
			"oversized payload was probed or copied before rejection");

		TextureInitialDataEntry valid{ payload, 4, 8 };
		ResetCallbacks();
		result = CopyTextureUpload(&valid, 1, 16, 1024, &Unreadable, &Copy);
		passed &= Check(result.error == TextureUploadCopyError::arrayUnreadable && !result.upload,
			"unreadable descriptor array was accepted");

		ResetCallbacks(1);
		result = CopyTextureUpload(&valid, 1, 16, 1024, &Readable, &Copy);
		passed &= Check(result.error == TextureUploadCopyError::arrayCopyFault && !result.upload,
			"descriptor copy fault was accepted");

		ResetCallbacks(2);
		result = CopyTextureUpload(&valid, 1, 16, 1024, &Readable, &Copy);
		passed &= Check(result.error == TextureUploadCopyError::entryCopyFault && !result.upload,
			"payload copy fault was accepted");
		return passed;
	}


	bool TestLoadScratchBufferLifetimeIsSerialized()
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
					auto scratchBufferLock = gate.Acquire();
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

		bool passed = true;
		passed &= Check(violations.load(std::memory_order_relaxed) == 0,
			"concurrent texture loads overlapped their shared scratch-buffer lifetime");
		return passed;
	}

	std::unique_ptr<OwnedTextureUpload> MakeUpload(unsigned char value)
	{
		std::array<unsigned char, 4> payload{ value, value, value, value };
		TextureInitialDataEntry entry{ payload.data(), 4, static_cast<unsigned int>(payload.size()) };
		ResetCallbacks();
		return CopyTextureUpload(&entry, 1, 16, 1024, &Readable, &Copy).upload;
	}

	bool TestOwnershipRegistryHandoff()
	{
		bool passed = true;
		TextureUploadRegistry registry;
		const TextureInitialDataEntry* key = nullptr;
		passed &= Check(registry.Register(MakeUpload(0x5A), &key) && key,
			"owned upload was not registered");
		passed &= Check(registry.Size() == 1,
			"registry did not retain queued upload");

		auto upload = registry.Take(key);
		passed &= Check(upload && registry.Size() == 0,
			"worker did not take queued upload");
		passed &= Check(!registry.Take(key),
			"registry handoff was not one-shot");
		passed &= Check(upload
			&& static_cast<const unsigned char*>(upload->entries[0].sysMem)[0] == 0x5A,
			"taken ownership did not remain valid through worker scope");

		const TextureInitialDataEntry* concurrentKey = nullptr;
		passed &= Check(registry.Register(MakeUpload(0xC3), &concurrentKey) && concurrentKey,
			"concurrent upload was not registered");
		std::atomic<bool> start{ false };
		std::atomic<int> winners{ 0 };
		std::atomic<int> validWinners{ 0 };
		auto take = [&]() {
			while (!start.load(std::memory_order_acquire))
				std::this_thread::yield();
			auto owned = registry.Take(concurrentKey);
			if (owned) {
				++winners;
				if (static_cast<const unsigned char*>(owned->entries[0].sysMem)[0] == 0xC3)
					++validWinners;
			}
		};
		std::thread first(take);
		std::thread second(take);
		start.store(true, std::memory_order_release);
		first.join();
		second.join();
		passed &= Check(winners == 1 && validWinners == 1 && registry.Size() == 0,
			"concurrent worker handoff did not produce exactly one valid owner");
		return passed;
	}
}

int main()
{
	bool passed = true;
	passed &= TestDeepCopyOutlivesSources();
	passed &= TestMalformedPayloadsAreRejected();
	passed &= TestLoadScratchBufferLifetimeIsSerialized();
	passed &= TestOwnershipRegistryHandoff();
	if (!passed)
		return EXIT_FAILURE;
	std::cout << "All materialsystem DX11 texture upload ownership tests passed\n";
	return EXIT_SUCCESS;
}

