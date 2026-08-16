#include "materialsystem_dx11_texture_upload.h"

#include <new>

namespace r1delta::materialsystem_dx11
{
	namespace
	{
		bool MulSize(std::size_t a, std::size_t b, std::size_t* out)
		{
			if (!out || (a && b > static_cast<std::size_t>(-1) / a))
				return false;
			*out = a * b;
			return true;
		}
	}

	TextureUploadCopyResult CopyTextureUpload(
		const TextureInitialDataEntry* sourceEntries,
		std::size_t subresourceCount,
		std::size_t mipLevels,
		std::size_t baseDepth,
		std::size_t maxSubresourceCount,
		std::size_t maxPayloadBytes,
		TextureUploadReadableRange readableRange,
		TextureUploadCopyRange copyRange)
	{
		TextureUploadCopyResult result;
		if (!sourceEntries
			|| !subresourceCount
			|| !mipLevels
			|| !baseDepth
			|| subresourceCount % mipLevels != 0
			|| subresourceCount > maxSubresourceCount
			|| !readableRange
			|| !copyRange) {
			result.error = TextureUploadCopyError::metadataInvalid;
			return result;
		}

		std::size_t entriesBytes = 0;
		if (!MulSize(subresourceCount, sizeof(TextureInitialDataEntry), &entriesBytes)) {
			result.error = TextureUploadCopyError::arraySizeOverflow;
			return result;
		}
		if (!readableRange(sourceEntries, entriesBytes)) {
			result.error = TextureUploadCopyError::arrayUnreadable;
			return result;
		}

		try {
			auto upload = std::make_unique<OwnedTextureUpload>();
			upload->entries.resize(subresourceCount);
			upload->payloads.resize(subresourceCount);
			if (!copyRange(upload->entries.data(), sourceEntries, entriesBytes)) {
				result.error = TextureUploadCopyError::arrayCopyFault;
				return result;
			}

			for (std::size_t i = 0; i < subresourceCount; ++i) {
				const TextureInitialDataEntry& entry = upload->entries[i];
				std::size_t mipDepth = baseDepth;
				for (std::size_t mipLevel = i % mipLevels; mipLevel > 0 && mipDepth > 1; --mipLevel)
					mipDepth >>= 1;

				std::size_t entryBytes = 0;
				if (!MulSize(entry.sysMemSlicePitch, mipDepth, &entryBytes)) {
					result.error = TextureUploadCopyError::payloadSizeOverflow;
					return result;
				}
				if (!entryBytes || !entry.sysMem) {
					result.error = TextureUploadCopyError::entryUnreadable;
					return result;
				}
				if (result.payloadBytes > maxPayloadBytes || entryBytes > maxPayloadBytes - result.payloadBytes) {
					result.error = TextureUploadCopyError::payloadTooLarge;
					return result;
				}
				if (!readableRange(entry.sysMem, entryBytes)) {
					result.error = TextureUploadCopyError::entryUnreadable;
					return result;
				}
				upload->payloads[i].resize(entryBytes);
				result.payloadBytes += entryBytes;
			}

			for (std::size_t i = 0; i < subresourceCount; ++i) {
				const std::size_t entryBytes = upload->payloads[i].size();
				if (!copyRange(upload->payloads[i].data(), upload->entries[i].sysMem, entryBytes)) {
					result.error = TextureUploadCopyError::entryCopyFault;
					return result;
				}
				upload->entries[i].sysMem = upload->payloads[i].data();
			}

			result.upload = std::move(upload);
			return result;
		}
		catch (const std::bad_alloc&) {
			result.error = TextureUploadCopyError::allocationFailed;
			return result;
		}
	}

	const char* TextureUploadCopyErrorName(TextureUploadCopyError error)
	{
		switch (error) {
		case TextureUploadCopyError::none:
			return "none";
		case TextureUploadCopyError::metadataInvalid:
			return "metadata-invalid";
		case TextureUploadCopyError::arraySizeOverflow:
			return "array-size-overflow";
		case TextureUploadCopyError::arrayUnreadable:
			return "array-unreadable";
		case TextureUploadCopyError::arrayCopyFault:
			return "array-copy-fault";
		case TextureUploadCopyError::entryUnreadable:
			return "entry-unreadable";
		case TextureUploadCopyError::entryCopyFault:
			return "entry-copy-fault";
		case TextureUploadCopyError::payloadSizeOverflow:
			return "payload-size-overflow";
		case TextureUploadCopyError::payloadTooLarge:
			return "payload-too-large";
		case TextureUploadCopyError::allocationFailed:
			return "allocation-failed";
		}
		return "unknown";
	}

	bool TextureUploadRegistry::Register(
		std::unique_ptr<OwnedTextureUpload> upload,
		const TextureInitialDataEntry** initialData)
	{
		if (initialData)
			*initialData = nullptr;
		if (!initialData || !upload || upload->entries.empty())
			return false;

		const TextureInitialDataEntry* key = upload->entries.data();
		try {
			std::lock_guard lock(mutex_);
			const auto inserted = uploads_.emplace(key, std::move(upload));
			if (!inserted.second)
				return false;
			*initialData = key;
			return true;
		}
		catch (const std::bad_alloc&) {
			return false;
		}
	}

	std::unique_ptr<OwnedTextureUpload> TextureUploadRegistry::Take(const TextureInitialDataEntry* initialData)
	{
		std::lock_guard lock(mutex_);
		const auto found = uploads_.find(initialData);
		if (found == uploads_.end())
			return nullptr;

		auto upload = std::move(found->second);
		uploads_.erase(found);
		return upload;
	}

	std::size_t TextureUploadRegistry::Size() const
	{
		std::lock_guard lock(mutex_);
		return uploads_.size();
	}
}
