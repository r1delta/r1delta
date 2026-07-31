#include "persistentdata_codec.h"

#include <cstdint>
#include <limits>
#include <unordered_set>
#include <zstd.h>

namespace PersistentDataCodec {
namespace {

constexpr uint32_t Magic = 0x31445052; // RPD1
constexpr size_t HeaderSize = sizeof(uint32_t) * 2;
constexpr size_t MaxEntryCount = 4096 * 4;
constexpr size_t MaxCommandLength = 512;

void AppendU16(std::vector<uint8_t>& output, uint16_t value)
{
	output.push_back(static_cast<uint8_t>(value));
	output.push_back(static_cast<uint8_t>(value >> 8));
}

void AppendU32(std::vector<uint8_t>& output, uint32_t value)
{
	for (int shift = 0; shift < 32; shift += 8)
		output.push_back(static_cast<uint8_t>(value >> shift));
}

bool ReadU16(const std::vector<uint8_t>& input, size_t& offset, uint16_t& value)
{
	if (offset > input.size() || input.size() - offset < 2)
		return false;
	value = static_cast<uint16_t>(input[offset])
		| static_cast<uint16_t>(input[offset + 1] << 8);
	offset += 2;
	return true;
}

bool ReadU32(const std::vector<uint8_t>& input, size_t& offset, uint32_t& value)
{
	if (offset > input.size() || input.size() - offset < 4)
		return false;
	value = static_cast<uint32_t>(input[offset])
		| (static_cast<uint32_t>(input[offset + 1]) << 8)
		| (static_cast<uint32_t>(input[offset + 2]) << 16)
		| (static_cast<uint32_t>(input[offset + 3]) << 24);
	offset += 4;
	return true;
}

std::string Base64Encode(const std::vector<uint8_t>& input)
{
	static constexpr char alphabet[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string output;
	output.reserve(((input.size() + 2) / 3) * 4);
	for (size_t i = 0; i < input.size(); i += 3) {
		const uint32_t a = input[i];
		const uint32_t b = i + 1 < input.size() ? input[i + 1] : 0;
		const uint32_t c = i + 2 < input.size() ? input[i + 2] : 0;
		const uint32_t value = (a << 16) | (b << 8) | c;
		output.push_back(alphabet[(value >> 18) & 0x3F]);
		output.push_back(alphabet[(value >> 12) & 0x3F]);
		output.push_back(i + 1 < input.size() ? alphabet[(value >> 6) & 0x3F] : '=');
		output.push_back(i + 2 < input.size() ? alphabet[value & 0x3F] : '=');
	}
	return output;
}

int Base64Value(char value)
{
	if (value >= 'A' && value <= 'Z')
		return value - 'A';
	if (value >= 'a' && value <= 'z')
		return value - 'a' + 26;
	if (value >= '0' && value <= '9')
		return value - '0' + 52;
	if (value == '+')
		return 62;
	if (value == '/')
		return 63;
	return -1;
}

bool Base64Decode(std::string_view input, std::vector<uint8_t>& output)
{
	if (input.empty() || input.size() > MaxEncodedSize || input.size() % 4 != 0)
		return false;

	output.clear();
	output.reserve((input.size() / 4) * 3);
	for (size_t i = 0; i < input.size(); i += 4) {
		const int a = Base64Value(input[i]);
		const int b = Base64Value(input[i + 1]);
		const bool cPadding = input[i + 2] == '=';
		const bool dPadding = input[i + 3] == '=';
		const int c = cPadding ? 0 : Base64Value(input[i + 2]);
		const int d = dPadding ? 0 : Base64Value(input[i + 3]);
		const bool finalGroup = i + 4 == input.size();
		if (a < 0 || b < 0 || c < 0 || d < 0
			|| (cPadding && !dPadding)
			|| ((cPadding || dPadding) && !finalGroup))
			return false;

		const uint32_t value = (static_cast<uint32_t>(a) << 18)
			| (static_cast<uint32_t>(b) << 12)
			| (static_cast<uint32_t>(c) << 6)
			| static_cast<uint32_t>(d);
		output.push_back(static_cast<uint8_t>(value >> 16));
		if (!cPadding)
			output.push_back(static_cast<uint8_t>(value >> 8));
		if (!dPadding)
			output.push_back(static_cast<uint8_t>(value));
	}
	return true;
}

bool ParseProfileLine(
	std::string_view line,
	std::string_view& identity,
	std::string_view& key,
	std::string_view& value,
	bool& persistent)
{
	if (!line.empty() && line.back() == '\r')
		line.remove_suffix(1);
	if (line.empty() || line.size() >= MaxCommandLength)
		return false;

	persistent = line.compare(0, 3, "__ ") == 0;
	const size_t nameStart = persistent ? 3 : 0;
	const size_t nameEnd = line.find(' ', nameStart);
	if (nameEnd == std::string_view::npos || nameEnd == nameStart)
		return false;
	key = line.substr(nameStart, nameEnd - nameStart);
	if (key.find_first_of(" \t\r\n\";") != std::string_view::npos)
		return false;

	const size_t valueStart = nameEnd + 1;
	if (valueStart + 1 >= line.size() || line[valueStart] != '"' || line.back() != '"')
		return false;
	value = line.substr(valueStart + 1, line.size() - valueStart - 2);
	if (value.find('"') != std::string_view::npos)
		return false;
	identity = persistent ? line.substr(0, nameEnd) : key;
	return true;
}

}

bool Encode(const std::vector<Entry>& entries, std::string& encoded)
{
	if (entries.empty() || entries.size() > MaxEntryCount)
		return false;

	std::vector<uint8_t> raw;
	raw.reserve(256 * 1024);
	AppendU32(raw, Magic);
	AppendU32(raw, static_cast<uint32_t>(entries.size()));
	for (const Entry& entry : entries) {
		if (entry.key.empty() || entry.key.size() > MaxKeySize
			|| entry.value.size() > MaxValueSize
			|| entry.key.size() > std::numeric_limits<uint16_t>::max()
			|| entry.value.size() > std::numeric_limits<uint16_t>::max())
			return false;
		const size_t entrySize = 4 + entry.key.size() + entry.value.size();
		if (raw.size() > MaxRawSize || MaxRawSize - raw.size() < entrySize)
			return false;

		AppendU16(raw, static_cast<uint16_t>(entry.key.size()));
		AppendU16(raw, static_cast<uint16_t>(entry.value.size()));
		raw.insert(raw.end(), entry.key.begin(), entry.key.end());
		raw.insert(raw.end(), entry.value.begin(), entry.value.end());
	}

	std::vector<uint8_t> compressed(ZSTD_compressBound(raw.size()));
	const size_t compressedSize = ZSTD_compress(
		compressed.data(), compressed.size(), raw.data(), raw.size(), 3);
	if (ZSTD_isError(compressedSize))
		return false;
	compressed.resize(compressedSize);
	encoded = Base64Encode(compressed);
	return !encoded.empty() && encoded.size() <= MaxEncodedSize;
}

bool Decode(std::string_view encoded, std::vector<Entry>& entries)
{
	std::vector<uint8_t> compressed;
	if (!Base64Decode(encoded, compressed))
		return false;

	const unsigned long long frameSize = ZSTD_getFrameContentSize(compressed.data(), compressed.size());
	if (frameSize == ZSTD_CONTENTSIZE_ERROR || frameSize == ZSTD_CONTENTSIZE_UNKNOWN
		|| frameSize < HeaderSize || frameSize > MaxRawSize)
		return false;

	std::vector<uint8_t> raw(static_cast<size_t>(frameSize));
	const size_t decompressedSize = ZSTD_decompress(raw.data(), raw.size(), compressed.data(), compressed.size());
	if (ZSTD_isError(decompressedSize) || decompressedSize != raw.size())
		return false;

	size_t offset = 0;
	uint32_t magic = 0;
	uint32_t entryCount = 0;
	if (!ReadU32(raw, offset, magic) || magic != Magic
		|| !ReadU32(raw, offset, entryCount) || !entryCount || entryCount > MaxEntryCount)
		return false;

	std::vector<Entry> decoded;
	decoded.reserve(entryCount);
	for (uint32_t i = 0; i < entryCount; ++i) {
		uint16_t keyLength = 0;
		uint16_t valueLength = 0;
		if (!ReadU16(raw, offset, keyLength) || !ReadU16(raw, offset, valueLength)
			|| !keyLength || keyLength > MaxKeySize || valueLength > MaxValueSize
			|| offset > raw.size()
			|| raw.size() - offset < static_cast<size_t>(keyLength) + valueLength)
			return false;

		Entry entry;
		entry.key.assign(reinterpret_cast<const char*>(raw.data() + offset), keyLength);
		offset += keyLength;
		entry.value.assign(reinterpret_cast<const char*>(raw.data() + offset), valueLength);
		offset += valueLength;
		decoded.push_back(std::move(entry));
	}
	if (offset != raw.size())
		return false;
	entries = std::move(decoded);
	return true;
}

bool ValidateProfile(
	std::string_view contents,
	ProfileEntryValidator persistentValidator,
	void* validatorContext)
{
	if (contents.empty() || contents.back() != '\n')
		return false;

	std::unordered_set<std::string> identities;
	size_t offset = 0;
	while (offset < contents.size()) {
		const size_t lineEnd = contents.find('\n', offset);
		if (lineEnd == std::string_view::npos)
			return false;

		std::string_view identity;
		std::string_view key;
		std::string_view value;
		bool persistent = false;
		if (!ParseProfileLine(contents.substr(offset, lineEnd - offset), identity, key, value, persistent)
			|| !identities.emplace(identity).second
			|| (persistent && persistentValidator
				&& !persistentValidator(key, value, validatorContext)))
			return false;
		offset = lineEnd + 1;
	}
	return true;
}

}
