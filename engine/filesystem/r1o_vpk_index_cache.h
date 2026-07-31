#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

struct R1OVPKChunk {
	uint32_t flags{};
	uint16_t textureFlags{};
	uint64_t offset{};
	uint64_t compressedSize{};
	uint64_t decompressedSize{};
};

struct R1OVPKEntry {
	std::string dirPath;
	std::vector<uint8_t> preload;
	std::vector<R1OVPKChunk> chunks;
	uint16_t packIndex{};
};

struct R1OVPKArchiveStamp {
	std::filesystem::path path;
	uint64_t size{};
	int64_t writeTime{};
};

bool R1OVPK_GetArchiveStamp(const std::filesystem::path& path, R1OVPKArchiveStamp& stamp);
std::string R1OVPK_GetIndexRootKey(const std::filesystem::path& vpkDirectory);
std::filesystem::path R1OVPK_GetIndexCachePath(const std::string& rootKey);
bool R1OVPK_LoadIndexCache(
	const std::filesystem::path& cachePath,
	const std::string& rootKey,
	const std::vector<R1OVPKArchiveStamp>& archives,
	std::unordered_map<std::string, R1OVPKEntry>& entries);
bool R1OVPK_StoreIndexCache(
	const std::filesystem::path& cachePath,
	const std::string& rootKey,
	const std::vector<R1OVPKArchiveStamp>& archives,
	const std::unordered_map<std::string, R1OVPKEntry>& entries);
