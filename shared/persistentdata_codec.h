#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace PersistentDataCodec {

constexpr char WireName[] = "_r1dp1";
constexpr size_t ChunkSize = 240;
constexpr size_t MaxEncodedSize = 4 * 1024 * 1024;
constexpr size_t MaxRawSize = 9 * 1024 * 1024;
constexpr size_t MaxKeySize = 256;
constexpr size_t MaxValueSize = 259;

struct Entry {
	std::string key;
	std::string value;
};

bool Encode(const std::vector<Entry>& entries, std::string& encoded);
bool Decode(std::string_view encoded, std::vector<Entry>& entries);
}
