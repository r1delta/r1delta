#pragma once

#include <cstddef>
#include <cstdint>

struct VPKDirectoryLoadFlagRepairResult
{
	bool valid{};
	size_t entryCount{};
	size_t chunkCount{};
	size_t repairedEntryCount{};
	size_t repairedChunkCount{};
};

// Matches language-prefixed and non-language-prefixed client map archives while
// excluding the common and server archives.
bool IsBrokenR1MapVPKDirectoryPath(const char* path, size_t pathLength);

// Validates the complete Respawn VPK v2.3 directory tree before changing it.
// Call only for an archive selected by IsBrokenR1MapVPKDirectoryPath. Original
// legacy-class trees also restore visibility of inactive studio model records;
// normalized trees keep intentionally old records inactive on subsequent calls.
// The returned counts describe the tree after a successful validation.
VPKDirectoryLoadFlagRepairResult RepairBrokenR1MapVPKDirectoryLoadFlags(
	uint8_t* tree,
	size_t treeSize);
