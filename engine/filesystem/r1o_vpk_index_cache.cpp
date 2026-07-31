#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "r1o_vpk_index_cache.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace {

constexpr uint64_t kCacheMagic = 0x314558444B505652ULL;
constexpr uint32_t kCacheVersion = 1;
constexpr size_t kMaximumCacheBytes = 256ULL * 1024 * 1024;
constexpr uint32_t kMaximumArchiveCount = 4096;
constexpr uint32_t kMaximumEntryCount = 1000000;
constexpr uint32_t kMaximumStringBytes = 1024 * 1024;
constexpr uint32_t kMaximumPreloadBytes = 64 * 1024 * 1024;
constexpr uint32_t kMaximumChunksPerEntry = 65536;
constexpr uint64_t kFNVOffsetBasis = 14695981039346656037ULL;
constexpr uint64_t kFNVPrime = 1099511628211ULL;

uint64_t HashBytes(const void* data, size_t size)
{
	const auto* bytes = static_cast<const uint8_t*>(data);
	uint64_t hash = kFNVOffsetBasis;
	for (size_t i = 0; i < size; ++i) {
		hash ^= bytes[i];
		hash *= kFNVPrime;
	}
	return hash;
}

template <typename T>
void AppendValue(std::vector<uint8_t>& output, const T& value)
{
	const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
	output.insert(output.end(), bytes, bytes + sizeof(value));
}

void AppendString(std::vector<uint8_t>& output, const std::string& value)
{
	AppendValue(output, static_cast<uint32_t>(value.size()));
	output.insert(output.end(), value.begin(), value.end());
}

template <typename T>
bool ReadValue(const uint8_t*& cursor, const uint8_t* end, T& value)
{
	if (cursor > end || static_cast<size_t>(end - cursor) < sizeof(value))
		return false;
	memcpy(&value, cursor, sizeof(value));
	cursor += sizeof(value);
	return true;
}

bool ReadString(const uint8_t*& cursor, const uint8_t* end, std::string& value)
{
	uint32_t size = 0;
	if (!ReadValue(cursor, end, size) || size > kMaximumStringBytes
		|| cursor > end || static_cast<size_t>(end - cursor) < size) {
		return false;
	}
	value.assign(reinterpret_cast<const char*>(cursor), size);
	cursor += size;
	return true;
}

std::string NormalizeRootKey(std::filesystem::path path)
{
	std::error_code error;
	std::filesystem::path resolved = std::filesystem::weakly_canonical(path, error);
	if (error) {
		error.clear();
		resolved = std::filesystem::absolute(path, error);
		if (error)
			resolved = std::move(path);
	}

	std::string key = resolved.lexically_normal().generic_string();
	std::transform(key.begin(), key.end(), key.begin(), [](unsigned char value) {
		return static_cast<char>(std::tolower(value));
	});
	return key;
}

std::filesystem::path CacheBaseDirectory()
{
	char overridePath[32768]{};
	const DWORD overrideLength = GetEnvironmentVariableA(
		"R1DELTA_VPK_CACHE_DIR",
		overridePath,
		static_cast<DWORD>(std::size(overridePath)));
	if (overrideLength > 0 && overrideLength < std::size(overridePath))
		return overridePath;

	char localAppData[32768]{};
	const DWORD localLength = GetEnvironmentVariableA(
		"LOCALAPPDATA",
		localAppData,
		static_cast<DWORD>(std::size(localAppData)));
	if (localLength > 0 && localLength < std::size(localAppData))
		return std::filesystem::path(localAppData) / "R1Delta" / "cache";

	std::error_code error;
	std::filesystem::path temporary = std::filesystem::temp_directory_path(error);
	return error ? std::filesystem::path(".") : temporary / "R1Delta" / "cache";
}

bool ReadCacheFile(const std::filesystem::path& path, std::vector<uint8_t>& bytes)
{
	std::error_code error;
	const uint64_t size = std::filesystem::file_size(path, error);
	if (error || size < sizeof(uint64_t) * 2 || size > kMaximumCacheBytes)
		return false;

	std::ifstream stream(path, std::ios::binary);
	if (!stream)
		return false;

	bytes.resize(static_cast<size_t>(size));
	stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	return stream.good();
}

bool WriteCacheFileAtomically(const std::filesystem::path& path, const std::vector<uint8_t>& bytes)
{
	std::error_code error;
	std::filesystem::create_directories(path.parent_path(), error);
	if (error)
		return false;

	std::filesystem::path temporary = path;
	temporary += ".tmp." + std::to_string(GetCurrentProcessId());
	{
		std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
		if (!stream)
			return false;
		stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		stream.flush();
		if (!stream.good()) {
			stream.close();
			std::filesystem::remove(temporary, error);
			return false;
		}
	}

	if (MoveFileExW(
		temporary.c_str(),
		path.c_str(),
		MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		return true;
	}

	std::filesystem::remove(temporary, error);
	return false;
}

} // namespace

bool R1OVPK_GetArchiveStamp(const std::filesystem::path& path, R1OVPKArchiveStamp& stamp)
{
	std::error_code error;
	const uint64_t size = std::filesystem::file_size(path, error);
	if (error)
		return false;

	const auto writeTime = std::filesystem::last_write_time(path, error);
	if (error)
		return false;

	stamp.path = path;
	stamp.size = size;
	stamp.writeTime = static_cast<int64_t>(writeTime.time_since_epoch().count());
	return true;
}

std::string R1OVPK_GetIndexRootKey(const std::filesystem::path& vpkDirectory)
{
	return NormalizeRootKey(vpkDirectory);
}

std::filesystem::path R1OVPK_GetIndexCachePath(const std::string& rootKey)
{
	char name[64]{};
	_snprintf_s(
		name,
		sizeof(name),
		_TRUNCATE,
		"r1o_vpk_index_v%u_%016llx.bin",
		kCacheVersion,
		static_cast<unsigned long long>(HashBytes(rootKey.data(), rootKey.size())));
	return CacheBaseDirectory() / name;
}

bool R1OVPK_LoadIndexCache(
	const std::filesystem::path& cachePath,
	const std::string& rootKey,
	const std::vector<R1OVPKArchiveStamp>& archives,
	std::unordered_map<std::string, R1OVPKEntry>& entries)
{
	std::vector<uint8_t> bytes;
	if (!ReadCacheFile(cachePath, bytes))
		return false;

	uint64_t storedHash = 0;
	memcpy(&storedHash, bytes.data() + bytes.size() - sizeof(storedHash), sizeof(storedHash));
	const size_t payloadSize = bytes.size() - sizeof(storedHash);
	if (HashBytes(bytes.data(), payloadSize) != storedHash)
		return false;

	const uint8_t* cursor = bytes.data();
	const uint8_t* end = bytes.data() + payloadSize;
	uint64_t magic = 0;
	uint32_t version = 0;
	uint32_t archiveCount = 0;
	uint32_t entryCount = 0;
	uint64_t indexedBytes = 0;
	std::string storedRoot;
	if (!ReadValue(cursor, end, magic)
		|| !ReadValue(cursor, end, version)
		|| !ReadValue(cursor, end, archiveCount)
		|| !ReadValue(cursor, end, entryCount)
		|| !ReadValue(cursor, end, indexedBytes)
		|| !ReadString(cursor, end, storedRoot)
		|| magic != kCacheMagic
		|| version != kCacheVersion
		|| archiveCount != archives.size()
		|| archiveCount > kMaximumArchiveCount
		|| entryCount > kMaximumEntryCount
		|| storedRoot != rootKey) {
		return false;
	}

	uint64_t expectedIndexedBytes = 0;
	for (uint32_t index = 0; index < archiveCount; ++index) {
		std::string name;
		uint64_t size = 0;
		int64_t writeTime = 0;
		if (!ReadString(cursor, end, name)
			|| !ReadValue(cursor, end, size)
			|| !ReadValue(cursor, end, writeTime)
			|| name != archives[index].path.filename().string()
			|| size != archives[index].size
			|| writeTime != archives[index].writeTime) {
			return false;
		}
		expectedIndexedBytes += size;
	}
	if (expectedIndexedBytes != indexedBytes)
		return false;

	std::unordered_map<std::string, R1OVPKEntry> loaded;
	loaded.reserve(entryCount);
	for (uint32_t index = 0; index < entryCount; ++index) {
		std::string key;
		uint32_t archiveIndex = 0;
		uint16_t packIndex = 0;
		uint32_t preloadSize = 0;
		uint32_t chunkCount = 0;
		if (!ReadString(cursor, end, key)
			|| !ReadValue(cursor, end, archiveIndex)
			|| !ReadValue(cursor, end, packIndex)
			|| !ReadValue(cursor, end, preloadSize)
			|| preloadSize > kMaximumPreloadBytes
			|| cursor > end || static_cast<size_t>(end - cursor) < preloadSize) {
			return false;
		}
		if (key.empty() || archiveIndex >= archiveCount || loaded.find(key) != loaded.end())
			return false;

		R1OVPKEntry entry;
		entry.dirPath = archives[archiveIndex].path.string();
		entry.packIndex = packIndex;
		entry.preload.assign(cursor, cursor + preloadSize);
		cursor += preloadSize;

		if (!ReadValue(cursor, end, chunkCount) || chunkCount > kMaximumChunksPerEntry)
			return false;
		entry.chunks.reserve(chunkCount);
		for (uint32_t chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex) {
			R1OVPKChunk chunk;
			if (!ReadValue(cursor, end, chunk.flags)
				|| !ReadValue(cursor, end, chunk.textureFlags)
				|| !ReadValue(cursor, end, chunk.offset)
				|| !ReadValue(cursor, end, chunk.compressedSize)
				|| !ReadValue(cursor, end, chunk.decompressedSize)
				|| chunk.offset > std::numeric_limits<uint64_t>::max() - chunk.compressedSize) {
				return false;
			}
			entry.chunks.push_back(chunk);
		}
		loaded.emplace(std::move(key), std::move(entry));
	}
	if (cursor != end)
		return false;

	entries = std::move(loaded);
	return true;
}

bool R1OVPK_StoreIndexCache(
	const std::filesystem::path& cachePath,
	const std::string& rootKey,
	const std::vector<R1OVPKArchiveStamp>& archives,
	const std::unordered_map<std::string, R1OVPKEntry>& entries)
{
	if (rootKey.size() > kMaximumStringBytes
		|| archives.size() > kMaximumArchiveCount
		|| entries.size() > kMaximumEntryCount) {
		return false;
	}

	std::unordered_map<std::string, uint32_t> archiveIndices;
	archiveIndices.reserve(archives.size());
	uint64_t indexedBytes = 0;
	for (uint32_t index = 0; index < archives.size(); ++index) {
		archiveIndices.emplace(archives[index].path.string(), index);
		indexedBytes += archives[index].size;
	}

	std::vector<uint8_t> bytes;
	bytes.reserve(std::min<size_t>(kMaximumCacheBytes, entries.size() * 128));
	AppendValue(bytes, kCacheMagic);
	AppendValue(bytes, kCacheVersion);
	AppendValue(bytes, static_cast<uint32_t>(archives.size()));
	AppendValue(bytes, static_cast<uint32_t>(entries.size()));
	AppendValue(bytes, indexedBytes);
	AppendString(bytes, rootKey);
	for (const R1OVPKArchiveStamp& archive : archives) {
		const std::string name = archive.path.filename().string();
		if (name.size() > kMaximumStringBytes)
			return false;
		AppendString(bytes, name);
		AppendValue(bytes, archive.size);
		AppendValue(bytes, archive.writeTime);
	}

	for (const auto& [key, entry] : entries) {
		const auto archive = archiveIndices.find(entry.dirPath);
		if (key.empty() || key.size() > kMaximumStringBytes
			|| archive == archiveIndices.end()
			|| entry.preload.size() > kMaximumPreloadBytes
			|| entry.chunks.size() > kMaximumChunksPerEntry) {
			return false;
		}
		AppendString(bytes, key);
		AppendValue(bytes, archive->second);
		AppendValue(bytes, entry.packIndex);
		AppendValue(bytes, static_cast<uint32_t>(entry.preload.size()));
		bytes.insert(bytes.end(), entry.preload.begin(), entry.preload.end());
		AppendValue(bytes, static_cast<uint32_t>(entry.chunks.size()));
		for (const R1OVPKChunk& chunk : entry.chunks) {
			AppendValue(bytes, chunk.flags);
			AppendValue(bytes, chunk.textureFlags);
			AppendValue(bytes, chunk.offset);
			AppendValue(bytes, chunk.compressedSize);
			AppendValue(bytes, chunk.decompressedSize);
		}
		if (bytes.size() > kMaximumCacheBytes - sizeof(uint64_t))
			return false;
	}

	AppendValue(bytes, HashBytes(bytes.data(), bytes.size()));
	return WriteCacheFileAtomically(cachePath, bytes);
}
