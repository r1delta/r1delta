#include "titan_insignia_image.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>

#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_TGA
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_ONLY_PSD
#define STBI_NO_STDIO
#define STBI_FAILURE_USERMSG
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_RESIZE_STATIC
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>

#define STB_DXT_STATIC
#define STB_DXT_IMPLEMENTATION
#include <stb_dxt.h>

#define BCDEC_STATIC
#define BCDEC_IMPLEMENTATION
#include "engine/r2port/bspdown/bcdec.h"

namespace TitanInsignia {
namespace {

using Rgba = std::vector<std::uint8_t>;

constexpr int kMipCount = 8;
constexpr int kThumbnailSize = 16;
constexpr int kThumbnailLevel = 3;
// Stock insignia textures: TEXTUREFLAGS_EIGHTBITALPHA | TEXTUREFLAGS_NOLOD.
constexpr std::uint32_t kVtfFlags = 0x2200;
constexpr std::int32_t kFormatDxt1 = 13;
constexpr std::int32_t kFormatDxt5 = 15;
constexpr std::uint32_t kHeaderBytes = 0x60;
constexpr std::uint32_t kThumbnailBytes = (kThumbnailSize / 4) * (kThumbnailSize / 4) * 8;

void ReadBlock(const Rgba& image, int size, int blockX, int blockY, std::uint8_t (&block)[64])
{
	// Levels below 4x4 repeat their edge pixels to fill the block.
	for (int y = 0; y < 4; ++y) {
		const int sourceY = (std::min)(blockY * 4 + y, size - 1);
		for (int x = 0; x < 4; ++x) {
			const int sourceX = (std::min)(blockX * 4 + x, size - 1);
			std::memcpy(block + (y * 4 + x) * 4, image.data() + (sourceY * size + sourceX) * 4, 4);
		}
	}
}

void EncodeLevel(const Rgba& image, int size, bool withAlpha, std::vector<std::uint8_t>& out)
{
	const int blocks = (std::max)(1, size / 4);
	const std::size_t blockBytes = withAlpha ? 16 : 8;
	for (int blockY = 0; blockY < blocks; ++blockY) {
		for (int blockX = 0; blockX < blocks; ++blockX) {
			std::uint8_t block[64];
			ReadBlock(image, size, blockX, blockY, block);
			unsigned char encoded[16];
			stb_compress_dxt_block(encoded, block, withAlpha ? 1 : 0, STB_DXT_HIGHQUAL);
			out.insert(out.end(), encoded, encoded + blockBytes);
		}
	}
}

Rgba DecodeTopMip(const TopMip& mip)
{
	Rgba image(static_cast<std::size_t>(kSize) * kSize * 4);
	constexpr int blocks = kSize / 4;
	for (int blockY = 0; blockY < blocks; ++blockY) {
		for (int blockX = 0; blockX < blocks; ++blockX) {
			bcdec_bc3(mip.data() + (blockY * blocks + blockX) * 16,
				image.data() + (blockY * 4 * kSize + blockX * 4) * 4, kSize * 4);
		}
	}
	return image;
}

// Alpha-weighted 2x2 box filter, integer-only so every client builds identical mips.
Rgba Downsample(const Rgba& source, int size)
{
	const int half = size / 2;
	Rgba result(static_cast<std::size_t>(half) * half * 4);
	for (int y = 0; y < half; ++y) {
		for (int x = 0; x < half; ++x) {
			const std::uint8_t* texels[4] = {
				source.data() + ((y * 2) * size + x * 2) * 4,
				source.data() + ((y * 2) * size + x * 2 + 1) * 4,
				source.data() + ((y * 2 + 1) * size + x * 2) * 4,
				source.data() + ((y * 2 + 1) * size + x * 2 + 1) * 4,
			};
			std::uint32_t alpha = 0;
			for (const std::uint8_t* texel : texels)
				alpha += texel[3];
			std::uint8_t* out = result.data() + (y * half + x) * 4;
			for (int channel = 0; channel < 3; ++channel) {
				std::uint32_t sum = 0;
				if (alpha) {
					for (const std::uint8_t* texel : texels)
						sum += static_cast<std::uint32_t>(texel[channel]) * texel[3];
					out[channel] = static_cast<std::uint8_t>((sum + alpha / 2) / alpha);
				}
				else {
					for (const std::uint8_t* texel : texels)
						sum += texel[channel];
					out[channel] = static_cast<std::uint8_t>((sum + 2) / 4);
				}
			}
			out[3] = static_cast<std::uint8_t>((alpha + 2) / 4);
		}
	}
	return result;
}

std::array<float, 3> Reflectivity(const Rgba& image)
{
	std::array<double, 3> sum{};
	const std::size_t texels = image.size() / 4;
	for (std::size_t i = 0; i < texels; ++i) {
		for (int channel = 0; channel < 3; ++channel)
			sum[channel] += std::pow(image[i * 4 + channel] / 255.0, 2.2);
	}
	return { static_cast<float>(sum[0] / texels), static_cast<float>(sum[1] / texels),
		static_cast<float>(sum[2] / texels) };
}

template <typename T>
void Put(std::vector<std::uint8_t>& out, std::size_t offset, T value)
{
	std::memcpy(out.data() + offset, &value, sizeof(T));
}

}

bool EncodeImageFile(std::span<const std::uint8_t> file, TopMip& out, std::string& error)
{
	out.clear();
	if (file.empty()) {
		error = "the file is empty";
		return false;
	}
	if (file.size() > kMaxSourceFileBytes) {
		error = "the file is larger than 16 MB";
		return false;
	}
	const int length = static_cast<int>(file.size());
	int width = 0;
	int height = 0;
	int components = 0;
	if (!stbi_info_from_memory(file.data(), length, &width, &height, &components)) {
		error = std::string("not a supported image (") + stbi_failure_reason() + ")";
		return false;
	}
	if (width <= 0 || height <= 0 || width > kMaxSourceDimension || height > kMaxSourceDimension) {
		error = "the image is larger than " + std::to_string(kMaxSourceDimension) + "x" + std::to_string(kMaxSourceDimension)
			+ " (it is " + std::to_string(width) + "x" + std::to_string(height) + ")";
		return false;
	}
	std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
		stbi_load_from_memory(file.data(), length, &width, &height, &components, 4), &stbi_image_free);
	if (!pixels) {
		error = std::string("the image could not be decoded (") + stbi_failure_reason() + ")";
		return false;
	}

	const double scale = static_cast<double>(kSize) / (std::max)(width, height);
	const int fittedWidth = std::clamp(static_cast<int>(std::lround(width * scale)), 1, kSize);
	const int fittedHeight = std::clamp(static_cast<int>(std::lround(height * scale)), 1, kSize);
	Rgba fitted(static_cast<std::size_t>(fittedWidth) * fittedHeight * 4);
	if (!stbir_resize_uint8_srgb(pixels.get(), width, height, 0, fitted.data(), fittedWidth, fittedHeight, 0, STBIR_RGBA)) {
		error = "the image could not be resized";
		return false;
	}

	Rgba canvas(static_cast<std::size_t>(kSize) * kSize * 4, 0);
	const int left = (kSize - fittedWidth) / 2;
	const int top = (kSize - fittedHeight) / 2;
	for (int y = 0; y < fittedHeight; ++y) {
		std::memcpy(canvas.data() + ((top + y) * kSize + left) * 4,
			fitted.data() + static_cast<std::size_t>(y) * fittedWidth * 4,
			static_cast<std::size_t>(fittedWidth) * 4);
	}
	out.reserve(kTopMipBytes);
	EncodeLevel(canvas, kSize, true, out);
	return true;
}

std::string ComputeId(const TopMip& mip)
{
	BCRYPT_ALG_HANDLE algorithm{};
	if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
		return {};
	std::array<unsigned char, 32> digest{};
	const NTSTATUS status = BCryptHash(algorithm, nullptr, 0, const_cast<PUCHAR>(mip.data()),
		static_cast<ULONG>(mip.size()), digest.data(), static_cast<ULONG>(digest.size()));
	BCryptCloseAlgorithmProvider(algorithm, 0);
	if (status < 0)
		return {};
	static constexpr char kHex[] = "0123456789abcdef";
	std::string id;
	id.reserve(kIdLength);
	for (std::size_t i = 0; i < kIdLength / 2; ++i) {
		id.push_back(kHex[digest[i] >> 4]);
		id.push_back(kHex[digest[i] & 0xF]);
	}
	return id;
}

bool IsValidId(std::string_view id)
{
	return id.size() == kIdLength
		&& std::all_of(id.begin(), id.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

std::vector<std::uint8_t> BuildVtf(const TopMip& mip)
{
	if (mip.size() != kTopMipBytes)
		return {};
	std::vector<Rgba> levels;
	levels.reserve(kMipCount);
	levels.push_back(DecodeTopMip(mip));
	for (int level = 1; level < kMipCount; ++level)
		levels.push_back(Downsample(levels.back(), kSize >> (level - 1)));

	std::vector<std::uint8_t> vtf(kHeaderBytes, 0);
	std::memcpy(vtf.data(), "VTF", 4);
	Put<std::uint32_t>(vtf, 0x04, 7);
	Put<std::uint32_t>(vtf, 0x08, 5);
	Put<std::uint32_t>(vtf, 0x0C, kHeaderBytes);
	Put<std::uint16_t>(vtf, 0x10, kSize);
	Put<std::uint16_t>(vtf, 0x12, kSize);
	Put<std::uint32_t>(vtf, 0x14, kVtfFlags);
	Put<std::uint16_t>(vtf, 0x18, 1);
	const std::array<float, 3> reflectivity = Reflectivity(levels[0]);
	Put<float>(vtf, 0x20, reflectivity[0]);
	Put<float>(vtf, 0x24, reflectivity[1]);
	Put<float>(vtf, 0x28, reflectivity[2]);
	Put<float>(vtf, 0x30, 1.0f);
	Put<std::int32_t>(vtf, 0x34, kFormatDxt5);
	Put<std::uint8_t>(vtf, 0x38, kMipCount);
	Put<std::int32_t>(vtf, 0x39, kFormatDxt1);
	Put<std::uint8_t>(vtf, 0x3D, kThumbnailSize);
	Put<std::uint8_t>(vtf, 0x3E, kThumbnailSize);
	Put<std::uint16_t>(vtf, 0x3F, 1);
	Put<std::uint32_t>(vtf, 0x44, 2);
	Put<std::uint32_t>(vtf, 0x50, 0x01);
	Put<std::uint32_t>(vtf, 0x54, kHeaderBytes);
	Put<std::uint32_t>(vtf, 0x58, 0x30);
	Put<std::uint32_t>(vtf, 0x5C, kHeaderBytes + kThumbnailBytes);

	EncodeLevel(levels[kThumbnailLevel], kThumbnailSize, false, vtf);
	// VTF stores the smallest mip first.
	for (int level = kMipCount - 1; level > 0; --level)
		EncodeLevel(levels[level], kSize >> level, true, vtf);
	vtf.insert(vtf.end(), mip.begin(), mip.end());
	return vtf;
}

}
