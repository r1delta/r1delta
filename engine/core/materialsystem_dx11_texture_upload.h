#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace r1delta::materialsystem_dx11
{
	struct TextureInitialDataEntry
	{
		const void* sysMem;
		unsigned int sysMemPitch;
		unsigned int sysMemSlicePitch;
	};

	static_assert(sizeof(TextureInitialDataEntry) == 16);

	struct OwnedTextureUpload
	{
		std::vector<TextureInitialDataEntry> entries;
		std::vector<std::vector<unsigned char>> payloads;
	};

	enum class TextureUploadCopyError
	{
		none,
		metadataInvalid,
		arraySizeOverflow,
		arrayUnreadable,
		arrayCopyFault,
		entryUnreadable,
		entryCopyFault,
		payloadTooLarge,
		allocationFailed,
	};

	using TextureUploadReadableRange = bool(*)(const void* data, std::size_t size);
	using TextureUploadCopyRange = bool(*)(void* destination, const void* source, std::size_t size);

	struct TextureUploadCopyResult
	{
		std::unique_ptr<OwnedTextureUpload> upload;
		TextureUploadCopyError error = TextureUploadCopyError::none;
		std::size_t payloadBytes = 0;
	};

	TextureUploadCopyResult CopyTextureUpload(
		const TextureInitialDataEntry* sourceEntries,
		std::size_t subresourceCount,
		std::size_t maxSubresourceCount,
		std::size_t maxPayloadBytes,
		TextureUploadReadableRange readableRange,
		TextureUploadCopyRange copyRange);

	const char* TextureUploadCopyErrorName(TextureUploadCopyError error);

	class TextureLoadScratchBufferGate
	{
	public:
		[[nodiscard]] std::unique_lock<std::recursive_mutex> Acquire()
		{
			return std::unique_lock<std::recursive_mutex>(mutex_);
		}

	private:
		std::recursive_mutex mutex_;
	};

	class TextureUploadRegistry
	{
	public:
		bool Register(std::unique_ptr<OwnedTextureUpload> upload, const TextureInitialDataEntry** initialData);
		std::unique_ptr<OwnedTextureUpload> Take(const TextureInitialDataEntry* initialData);
		std::size_t Size() const;

	private:
		mutable std::mutex mutex_;
		std::unordered_map<const TextureInitialDataEntry*, std::unique_ptr<OwnedTextureUpload>> uploads_;
	};
}
