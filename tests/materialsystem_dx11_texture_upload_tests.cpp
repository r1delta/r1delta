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
			result = CopyTextureUpload(entries, 2, 2, 1, 16, 1024, &Readable, &Copy);
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

	bool TestVolumeMipDepthIsOwned()
	{
		std::array<unsigned char, 32> expectedFirst{};
		std::array<unsigned char, 4> expectedSecond{};
		std::array<unsigned char, 1> expectedThird{};
		for (std::size_t i = 0; i < expectedFirst.size(); ++i)
			expectedFirst[i] = static_cast<unsigned char>(i + 1);
		for (std::size_t i = 0; i < expectedSecond.size(); ++i)
			expectedSecond[i] = static_cast<unsigned char>(0x80 + i);
		expectedThird[0] = 0xF3;

		TextureUploadCopyResult result;
		{
			auto first = expectedFirst;
			auto second = expectedSecond;
			auto third = expectedThird;
			TextureInitialDataEntry entries[] = {
				{ first.data(), 2, 8 },
				{ second.data(), 2, 2 },
				{ third.data(), 1, 1 },
			};
			ResetCallbacks();
			result = CopyTextureUpload(entries, 3, 3, 4, 16, 1024, &Readable, &Copy);
			first.fill(0);
			second.fill(0);
			third.fill(0);
		}

		bool passed = true;
		passed &= Check(result.error == TextureUploadCopyError::none && result.upload,
			"volume upload was not copied");
		if (!result.upload)
			return false;
		passed &= Check(result.payloadBytes == 37,
			"volume mip depth was not included in payload size");
		passed &= Check(result.upload->payloads[0].size() == 32
			&& result.upload->payloads[1].size() == 4
			&& result.upload->payloads[2].size() == 1,
			"volume mip payload sizes did not follow 4-2-1 depth");
		passed &= Check(std::equal(expectedFirst.begin(), expectedFirst.end(), result.upload->payloads[0].begin())
			&& std::equal(expectedSecond.begin(), expectedSecond.end(), result.upload->payloads[1].begin())
			&& std::equal(expectedThird.begin(), expectedThird.end(), result.upload->payloads[2].begin()),
			"volume upload retained borrowed depth slices");
		return passed;
	}

	bool TestMalformedPayloadsAreRejected()
	{
		bool passed = true;
		unsigned char payload[8]{};

		ResetCallbacks();
		auto result = CopyTextureUpload(nullptr, 1, 1, 1, 16, 1024, &Readable, &Copy);
		passed &= Check(result.error == TextureUploadCopyError::metadataInvalid && !result.upload,
			"null descriptor array was accepted");

		TextureInitialDataEntry zeroSlice{ payload, 4, 0 };
		ResetCallbacks();
		result = CopyTextureUpload(&zeroSlice, 1, 1, 1, 16, 1024, &Readable, &Copy);
		passed &= Check(result.error == TextureUploadCopyError::entryUnreadable && !result.upload,
			"zero slice pitch was accepted");

		TextureInitialDataEntry nullPayload{ nullptr, 4, 8 };
		ResetCallbacks();
		result = CopyTextureUpload(&nullPayload, 1, 1, 1, 16, 1024, &Readable, &Copy);
		passed &= Check(result.error == TextureUploadCopyError::entryUnreadable && !result.upload,
			"null payload was accepted");

		TextureInitialDataEntry oversized{ payload, 4, UINT_MAX };
		ResetCallbacks();
		result = CopyTextureUpload(&oversized, 1, 1, 1, 16, 64, &Readable, &Copy);
		passed &= Check(result.error == TextureUploadCopyError::payloadTooLarge && !result.upload,
			"oversized payload was accepted");
		passed &= Check(g_readableCalls == 1 && g_copyCalls == 1,
			"oversized payload was probed or copied before rejection");

		TextureInitialDataEntry valid{ payload, 4, 8 };
		ResetCallbacks();
		result = CopyTextureUpload(&valid, 1, 1, 1, 16, 1024, &Unreadable, &Copy);
		passed &= Check(result.error == TextureUploadCopyError::arrayUnreadable && !result.upload,
			"unreadable descriptor array was accepted");

		ResetCallbacks(1);
		result = CopyTextureUpload(&valid, 1, 1, 1, 16, 1024, &Readable, &Copy);
		passed &= Check(result.error == TextureUploadCopyError::arrayCopyFault && !result.upload,
			"descriptor copy fault was accepted");

		ResetCallbacks(2);
		result = CopyTextureUpload(&valid, 1, 1, 1, 16, 1024, &Readable, &Copy);
		passed &= Check(result.error == TextureUploadCopyError::entryCopyFault && !result.upload,
			"payload copy fault was accepted");
		return passed;
	}

	std::unique_ptr<OwnedTextureUpload> MakeUpload(unsigned char value)
	{
		std::array<unsigned char, 4> payload{ value, value, value, value };
		TextureInitialDataEntry entry{ payload.data(), 4, static_cast<unsigned int>(payload.size()) };
		ResetCallbacks();
		return CopyTextureUpload(&entry, 1, 1, 1, 16, 1024, &Readable, &Copy).upload;
	}

	bool TestOwnershipRegistryHandoff()
	{
		bool passed = true;
		TextureUploadRegistry registry;
		const TextureInitialDataEntry* rejectedKey = reinterpret_cast<const TextureInitialDataEntry*>(1);
		passed &= Check(!registry.Register(nullptr, &rejectedKey)
			&& !rejectedKey
			&& registry.Size() == 0,
			"invalid upload entered the ownership registry");
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
	passed &= TestVolumeMipDepthIsOwned();
	passed &= TestMalformedPayloadsAreRejected();
	passed &= TestOwnershipRegistryHandoff();
	if (!passed)
		return EXIT_FAILURE;
	std::cout << "All materialsystem DX11 texture upload ownership tests passed\n";
	return EXIT_SUCCESS;
}

