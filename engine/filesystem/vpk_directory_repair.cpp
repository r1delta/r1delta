#include "vpk_directory_repair.h"

#include <cstring>
#include <string_view>

namespace {

constexpr uint16_t kChunkTerminator = 0xFFFF;
constexpr uint32_t kEntryVisible = 0x100;
constexpr uint32_t kAlternatePrecacheClass = 0x400;
constexpr size_t kChunkMetadataSizeAfterLoadFlagsBeforeTerminator =
	sizeof(uint16_t) + sizeof(uint64_t) * 3;

char AsciiLower(char value)
{
	return value >= 'A' && value <= 'Z'
		? static_cast<char>(value + ('a' - 'A'))
		: value;
}

bool EndsWithInsensitive(std::string_view value, std::string_view suffix)
{
	if (value.size() < suffix.size())
		return false;

	const size_t offset = value.size() - suffix.size();
	for (size_t i = 0; i < suffix.size(); ++i) {
		if (AsciiLower(value[offset + i]) != AsciiLower(suffix[i]))
			return false;
	}
	return true;
}

bool IsStudioModelComponent(
	std::string_view extension,
	std::string_view filename)
{
	if (extension.size() != 3)
		return false;

	return EndsWithInsensitive(extension, "mdl")
		|| EndsWithInsensitive(extension, "phy")
		|| EndsWithInsensitive(extension, "vvd")
		|| (EndsWithInsensitive(extension, "vtx")
			&& EndsWithInsensitive(filename, ".dx11"));
}

template <typename T>
bool ReadValue(const uint8_t* tree, size_t treeSize, size_t& cursor, T& value)
{
	if (!tree || cursor > treeSize || treeSize - cursor < sizeof(value))
		return false;
	memcpy(&value, tree + cursor, sizeof(value));
	cursor += sizeof(value);
	return true;
}

bool ReadNullString(
	const uint8_t* tree,
	size_t treeSize,
	size_t& cursor,
	std::string_view& value)
{
	if (!tree || cursor >= treeSize)
		return false;

	const void* terminator = memchr(tree + cursor, 0, treeSize - cursor);
	if (!terminator)
		return false;

	const size_t end = static_cast<const uint8_t*>(terminator) - tree;
	value = std::string_view(
		reinterpret_cast<const char*>(tree + cursor), end - cursor);
	cursor = end + 1;
	return true;
}

bool RemainingBytesAreZero(const uint8_t* tree, size_t treeSize, size_t cursor)
{
	for (; cursor < treeSize; ++cursor) {
		if (tree[cursor] != 0)
			return false;
	}
	return true;
}

VPKDirectoryLoadFlagRepairResult ParseDirectoryTree(
	uint8_t* tree,
	size_t treeSize,
	bool applyRepair)
{
	VPKDirectoryLoadFlagRepairResult result{};
	size_t cursor = 0;

	for (;;) {
		std::string_view extension;
		if (!ReadNullString(tree, treeSize, cursor, extension))
			return result;
		if (extension.empty()) {
			result.valid = RemainingBytesAreZero(tree, treeSize, cursor);
			return result;
		}

		for (;;) {
			std::string_view directory;
			if (!ReadNullString(tree, treeSize, cursor, directory))
				return result;
			if (directory.empty())
				break;

			for (;;) {
				std::string_view filename;
				if (!ReadNullString(tree, treeSize, cursor, filename))
					return result;
				if (filename.empty())
					break;

				uint32_t crc = 0;
				uint16_t preloadSize = 0;
				uint16_t packIndex = 0;
				if (!ReadValue(tree, treeSize, cursor, crc)
					|| !ReadValue(tree, treeSize, cursor, preloadSize)
					|| !ReadValue(tree, treeSize, cursor, packIndex)) {
					return result;
				}
				(void)crc;
				(void)packIndex;

				const bool repairModelVisibility =
					applyRepair && IsStudioModelComponent(extension, filename);
				bool repairedEntry = false;
				for (;;) {
					const size_t loadFlagsOffset = cursor;
					uint32_t loadFlags = 0;
					if (!ReadValue(tree, treeSize, cursor, loadFlags))
						return result;

					if (cursor > treeSize
						|| treeSize - cursor
							< kChunkMetadataSizeAfterLoadFlagsBeforeTerminator) {
						return result;
					}
					cursor += kChunkMetadataSizeAfterLoadFlagsBeforeTerminator;

					const bool alternateClass =
						(loadFlags & kAlternatePrecacheClass) != 0;
					const bool inactiveModel =
						repairModelVisibility && loadFlags == 0x001;
					if (alternateClass || inactiveModel) {
						repairedEntry = true;
						++result.repairedChunkCount;
						if (applyRepair) {
							// The legacy class encoding inverts visibility. Bare
							// inactive model records in the same original trees
							// also lack visibility, unlike their healthy copies.
							if (alternateClass)
								loadFlags ^= kEntryVisible | kAlternatePrecacheClass;
							else
								loadFlags |= kEntryVisible;
							memcpy(
								tree + loadFlagsOffset,
								&loadFlags,
								sizeof(loadFlags));
						}
					}

					uint16_t terminator = 0;
					if (!ReadValue(tree, treeSize, cursor, terminator))
						return result;
					++result.chunkCount;
					if (terminator == kChunkTerminator)
						break;
				}

				if (cursor > treeSize || treeSize - cursor < preloadSize)
					return result;
				cursor += preloadSize;

				if (repairedEntry)
					++result.repairedEntryCount;
				++result.entryCount;
			}
		}
	}
}

}

bool IsBrokenR1MapVPKDirectoryPath(const char* path, size_t pathLength)
{
	if (!path || pathLength == 0)
		return false;

	std::string_view basename(path, pathLength);
	const size_t separator = basename.find_last_of("\\/");
	if (separator != std::string_view::npos)
		basename.remove_prefix(separator + 1);

	return EndsWithInsensitive(
			basename,
			"client_mp_mia.bsp.pak000_dir.vpk")
		|| EndsWithInsensitive(
			basename,
			"client_mp_mia.bsp.pak000")
		|| EndsWithInsensitive(
			basename,
			"client_mp_nest2.bsp.pak000_dir.vpk")
		|| EndsWithInsensitive(
			basename,
			"client_mp_nest2.bsp.pak000");
}

VPKDirectoryLoadFlagRepairResult RepairBrokenR1MapVPKDirectoryLoadFlags(
	uint8_t* tree,
	size_t treeSize)
{
	// Only original trees containing the legacy class encoding may restore
	// bare inactive model records. After normalization, 0x501 becomes 0x001;
	// repeating the repair must not make those intentionally old entries live.
	VPKDirectoryLoadFlagRepairResult validation =
		ParseDirectoryTree(tree, treeSize, false);
	if (!validation.valid || validation.repairedChunkCount == 0)
		return validation;

	const VPKDirectoryLoadFlagRepairResult repaired =
		ParseDirectoryTree(tree, treeSize, true);
	return repaired.valid ? repaired : VPKDirectoryLoadFlagRepairResult{};
}
