#include "titan_insignia_wire.h"

#include <charconv>
#include <cstdio>
#include <vector>
#include <zstd.h>

namespace TitanInsignia::Wire {
namespace {

constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
constexpr int kZstdLevel = 19;

int AlphabetIndex(char c)
{
	if (c >= 'A' && c <= 'Z')
		return c - 'A';
	if (c >= 'a' && c <= 'z')
		return c - 'a' + 26;
	if (c >= '0' && c <= '9')
		return c - '0' + 52;
	if (c == '-')
		return 62;
	if (c == '_')
		return 63;
	return -1;
}

std::string EncodeBase64Url(const std::vector<std::uint8_t>& input)
{
	std::string out;
	out.reserve((input.size() * 4 + 2) / 3);
	std::size_t i = 0;
	for (; i + 3 <= input.size(); i += 3) {
		const std::uint32_t triple = (input[i] << 16) | (input[i + 1] << 8) | input[i + 2];
		out.push_back(kAlphabet[(triple >> 18) & 63]);
		out.push_back(kAlphabet[(triple >> 12) & 63]);
		out.push_back(kAlphabet[(triple >> 6) & 63]);
		out.push_back(kAlphabet[triple & 63]);
	}
	const std::size_t rest = input.size() - i;
	if (rest) {
		const std::uint32_t triple = (input[i] << 16) | (rest == 2 ? input[i + 1] << 8 : 0);
		out.push_back(kAlphabet[(triple >> 18) & 63]);
		out.push_back(kAlphabet[(triple >> 12) & 63]);
		if (rest == 2)
			out.push_back(kAlphabet[(triple >> 6) & 63]);
	}
	return out;
}

bool DecodeBase64Url(std::string_view input, std::vector<std::uint8_t>& out)
{
	if (input.size() % 4 == 1)
		return false;
	out.clear();
	out.reserve(input.size() * 3 / 4);
	std::uint32_t buffer = 0;
	int bits = 0;
	for (const char c : input) {
		const int value = AlphabetIndex(c);
		if (value < 0)
			return false;
		buffer = (buffer << 6) | static_cast<std::uint32_t>(value);
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out.push_back(static_cast<std::uint8_t>(buffer >> bits));
			buffer &= (1u << bits) - 1;
		}
	}
	// Unused trailing bits must be zero, so every byte string has exactly one encoding.
	return buffer == 0;
}

}

std::string FormatMarker(const Marker& marker)
{
	char text[64];
	if (marker.id.empty())
		std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(marker.instance));
	else
		std::snprintf(text, sizeof(text), "%016llx:%s", static_cast<unsigned long long>(marker.instance), marker.id.c_str());
	return text;
}

bool ParseMarker(std::string_view text, Marker& marker)
{
	if (text.size() < 16)
		return false;
	std::uint64_t instance = 0;
	const auto [end, error] = std::from_chars(text.data(), text.data() + 16, instance, 16);
	if (error != std::errc() || end != text.data() + 16)
		return false;
	std::string_view rest = text.substr(16);
	std::string id;
	if (!rest.empty()) {
		if (rest.front() != ':' || !IsValidId(rest.substr(1)))
			return false;
		id.assign(rest.substr(1));
	}
	marker.instance = instance;
	marker.id = std::move(id);
	return true;
}

bool Pack(const TopMip& mip, std::string& packed)
{
	if (mip.size() != kTopMipBytes)
		return false;
	std::vector<std::uint8_t> compressed(ZSTD_compressBound(mip.size()));
	const std::size_t size = ZSTD_compress(compressed.data(), compressed.size(), mip.data(), mip.size(), kZstdLevel);
	if (ZSTD_isError(size))
		return false;
	compressed.resize(size);
	packed = EncodeBase64Url(compressed);
	return packed.size() <= kMaxPackedChars;
}

bool Unpack(std::string_view packed, TopMip& mip)
{
	if (packed.empty() || packed.size() > kMaxPackedChars)
		return false;
	std::vector<std::uint8_t> compressed;
	if (!DecodeBase64Url(packed, compressed))
		return false;
	if (ZSTD_getFrameContentSize(compressed.data(), compressed.size()) != kTopMipBytes)
		return false;
	TopMip result(kTopMipBytes);
	const std::size_t size = ZSTD_decompress(result.data(), result.size(), compressed.data(), compressed.size());
	if (ZSTD_isError(size) || size != kTopMipBytes)
		return false;
	mip = std::move(result);
	return true;
}

}
