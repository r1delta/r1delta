// stb_image_write's TGA/HDR writers call sprintf, which SDL checks reject; set before any CRT header.
#define _CRT_SECURE_NO_WARNINGS

#include "shared/titan_insignia_image.h"
#include "shared/titan_insignia_wire.h"

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#define BCDEC_STATIC
#define BCDEC_IMPLEMENTATION
#include "engine/r2port/bspdown/bcdec.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

namespace Wire = TitanInsignia::Wire;

int failures = 0;

void Check(bool condition, const char* name)
{
	if (!condition) {
		std::cerr << "FAIL: " << name << '\n';
		++failures;
	}
}

void AppendBytes(void* context, void* data, int size)
{
	auto* out = static_cast<std::vector<std::uint8_t>*>(context);
	const auto* bytes = static_cast<const std::uint8_t*>(data);
	out->insert(out->end(), bytes, bytes + size);
}

// A width x height PNG: opaque red left half, opaque blue right half.
std::vector<std::uint8_t> MakePng(int width, int height)
{
	std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			std::uint8_t* p = pixels.data() + (static_cast<std::size_t>(y) * width + x) * 4;
			const bool left = x < width / 2;
			p[0] = left ? 255 : 0;
			p[1] = 0;
			p[2] = left ? 0 : 255;
			p[3] = 255;
		}
	}
	std::vector<std::uint8_t> png;
	stbi_write_png_to_func(&AppendBytes, &png, width, height, 4, pixels.data(), width * 4);
	return png;
}

std::array<std::uint8_t, 4> TexelOf(const TitanInsignia::TopMip& mip, int x, int y)
{
	std::uint8_t block[16 * 4];
	const int blocks = TitanInsignia::kSize / 4;
	bcdec_bc3(mip.data() + ((y / 4) * blocks + x / 4) * 16, block, 4 * 4);
	const std::uint8_t* texel = block + ((y % 4) * 4 + x % 4) * 4;
	return { texel[0], texel[1], texel[2], texel[3] };
}

template <typename T>
T Read(const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
	T value{};
	std::memcpy(&value, bytes.data() + offset, sizeof(T));
	return value;
}

void TestEncodeKeepsAspectRatio()
{
	// 256x128 fits as 128x64, centered vertically: rows 32..95 hold the image, the rest is transparent.
	const std::vector<std::uint8_t> png = MakePng(256, 128);
	TitanInsignia::TopMip mip;
	std::string error;
	Check(TitanInsignia::EncodeImageFile(png, mip, error), "encode a 256x128 PNG");
	Check(mip.size() == TitanInsignia::kTopMipBytes, "top mip is 16 KB");
	if (mip.size() != TitanInsignia::kTopMipBytes)
		return;
	Check(TexelOf(mip, 64, 4)[3] == 0, "letterbox above the image is transparent");
	Check(TexelOf(mip, 64, 124)[3] == 0, "letterbox below the image is transparent");
	const auto red = TexelOf(mip, 20, 64);
	Check(red[3] == 255 && red[0] > 200 && red[2] < 60, "left half is opaque red");
	const auto blue = TexelOf(mip, 108, 64);
	Check(blue[3] == 255 && blue[2] > 200 && blue[0] < 60, "right half is opaque blue");
}

void TestEncodeRejectsBadFiles()
{
	TitanInsignia::TopMip mip;
	std::string error;
	Check(!TitanInsignia::EncodeImageFile({}, mip, error) && !error.empty(), "reject an empty file");
	const std::vector<std::uint8_t> text = { 'n', 'o', 't', ' ', 'a', 'n', ' ', 'i', 'm', 'a', 'g', 'e' };
	error.clear();
	Check(!TitanInsignia::EncodeImageFile(text, mip, error) && !error.empty(), "reject bytes that are no image");
	std::vector<std::uint8_t> truncated = MakePng(64, 64);
	truncated.resize(truncated.size() / 2);
	error.clear();
	Check(!TitanInsignia::EncodeImageFile(truncated, mip, error) && !error.empty(), "reject a truncated PNG");
}

void TestSizeLimit()
{
	TitanInsignia::TopMip mip;
	std::string error;
	Check(TitanInsignia::EncodeImageFile(MakePng(512, 512), mip, error), "accept a 512x512 PNG");
	error.clear();
	Check(!TitanInsignia::EncodeImageFile(MakePng(513, 512), mip, error)
		&& error.find("512x512") != std::string::npos && error.find("513x512") != std::string::npos,
		"reject a 513x512 PNG and name both sizes");
	error.clear();
	Check(!TitanInsignia::EncodeImageFile(MakePng(64, 513), mip, error) && !error.empty(), "reject a 64x513 PNG");
}

void TestIdAndVtf()
{
	TitanInsignia::TopMip mip;
	std::string error;
	Check(TitanInsignia::EncodeImageFile(MakePng(128, 128), mip, error), "encode a 128x128 PNG");
	const std::string id = TitanInsignia::ComputeId(mip);
	Check(TitanInsignia::IsValidId(id), "id is 32 lowercase hex digits");
	Check(TitanInsignia::ComputeId(mip) == id, "id is stable");
	TitanInsignia::TopMip other = mip;
	other[100] ^= 1;
	Check(TitanInsignia::ComputeId(other) != id, "a changed block changes the id");
	Check(!TitanInsignia::IsValidId("ABCDEF0123456789ABCDEF0123456789"), "uppercase ids are invalid");
	Check(!TitanInsignia::IsValidId("abc"), "short ids are invalid");

	const std::vector<std::uint8_t> vtf = TitanInsignia::BuildVtf(mip);
	Check(vtf.size() == 0x60 + 128 + 21872, "VTF size: header, DXT1 thumbnail, DXT5 mips 128..1");
	if (vtf.size() != 0x60 + 128 + 21872)
		return;
	Check(std::memcmp(vtf.data(), "VTF", 4) == 0, "VTF signature");
	Check(Read<std::uint32_t>(vtf, 0x04) == 7 && Read<std::uint32_t>(vtf, 0x08) == 5, "VTF version 7.5");
	Check(Read<std::uint16_t>(vtf, 0x10) == 128 && Read<std::uint16_t>(vtf, 0x12) == 128, "VTF is 128x128");
	Check(Read<std::uint32_t>(vtf, 0x14) == 0x2200, "VTF flags match the stock insignias");
	Check(Read<std::int32_t>(vtf, 0x34) == 15 && vtf[0x38] == 8, "DXT5 with 8 mips");
	Check(Read<std::int32_t>(vtf, 0x39) == 13 && vtf[0x3D] == 16 && vtf[0x3E] == 16, "16x16 DXT1 thumbnail");
	Check(Read<std::uint32_t>(vtf, 0x44) == 2, "two resources");
	Check(Read<std::uint32_t>(vtf, 0x50) == 0x01 && Read<std::uint32_t>(vtf, 0x54) == 0x60, "thumbnail resource");
	Check(Read<std::uint32_t>(vtf, 0x58) == 0x30 && Read<std::uint32_t>(vtf, 0x5C) == 0x60 + 128, "image resource");
	Check(std::memcmp(vtf.data() + vtf.size() - mip.size(), mip.data(), mip.size()) == 0,
		"the largest mip is stored last, unchanged");
	Check(TitanInsignia::BuildVtf(mip) == vtf, "the VTF is deterministic");
	Check(TitanInsignia::BuildVtf(TitanInsignia::TopMip(100)).empty(), "a short top mip builds no VTF");
}

void TestWire()
{
	TitanInsignia::TopMip mip;
	std::string error;
	Check(TitanInsignia::EncodeImageFile(MakePng(200, 300), mip, error), "encode a 200x300 PNG");
	std::string packed;
	Check(Wire::Pack(mip, packed), "pack");
	Check(!packed.empty() && packed.size() <= Wire::kMaxPackedChars, "packed size is bounded");
	Check(packed.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_") == std::string::npos,
		"packed text needs no quoting");
	TitanInsignia::TopMip unpacked;
	Check(Wire::Unpack(packed, unpacked) && unpacked == mip, "unpack round trip");

	Check(!Wire::Unpack({}, unpacked), "reject empty data");
	Check(!Wire::Unpack(packed.substr(0, packed.size() - 8), unpacked), "reject truncated data");
	std::string invalid = packed;
	invalid[invalid.size() / 2] = '+';
	Check(!Wire::Unpack(invalid, unpacked), "reject characters outside base64url");
	Check(!Wire::Unpack(std::string(Wire::kMaxPackedChars + 4, 'A'), unpacked), "reject oversized data");

	// A valid frame of the wrong size is not an insignia.
	TitanInsignia::TopMip half(TitanInsignia::kTopMipBytes / 2, 7);
	std::string wrongSize;
	Check(!Wire::Pack(half, wrongSize), "refuse to pack a short top mip");
	Check(!Wire::Pack(TitanInsignia::TopMip(), wrongSize), "refuse to pack an empty top mip");
}

void TestMarker()
{
	Wire::Marker marker;
	marker.instance = 0x0123456789abcdefull;
	Check(Wire::FormatMarker(marker) == "0123456789abcdef", "marker without insignia");
	marker.id = "00112233445566778899aabbccddeeff";
	const std::string text = Wire::FormatMarker(marker);
	Check(text == "0123456789abcdef:00112233445566778899aabbccddeeff", "marker with insignia");
	Check(text.size() < 260, "marker fits a userinfo value");

	Wire::Marker parsed;
	Check(Wire::ParseMarker(text, parsed) && parsed.instance == marker.instance && parsed.id == marker.id, "parse marker");
	Check(Wire::ParseMarker("0123456789abcdef", parsed) && parsed.id.empty(), "parse marker without insignia");
	Check(!Wire::ParseMarker("", parsed), "reject an empty marker");
	Check(!Wire::ParseMarker("0123456789abcdeg", parsed), "reject a non-hex instance");
	Check(!Wire::ParseMarker("0123456789abcdef:", parsed), "reject an empty id");
	Check(!Wire::ParseMarker("0123456789abcdef:00112233", parsed), "reject a short id");
	Check(!Wire::ParseMarker("0123456789abcdef-00112233445566778899aabbccddeeff", parsed), "reject a wrong separator");
}

}

int main()
{
	TestEncodeKeepsAspectRatio();
	TestEncodeRejectsBadFiles();
	TestSizeLimit();
	TestIdAndVtf();
	TestWire();
	TestMarker();
	if (failures) {
		std::cerr << failures << " check(s) failed\n";
		return 1;
	}
	std::cout << "titan_insignia_tests: all checks passed\n";
	return 0;
}
