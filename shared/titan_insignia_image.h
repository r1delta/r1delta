#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Custom Titan insignia images.
//
// Every stock insignia (models/titans/custom_decals/decal_pack_01/*_col.vtf) is a 128x128 DXT5 texture
// with a full mip chain. A custom insignia travels as the DXT5 blocks of its 128x128 top mip only:
// every client rebuilds the same mip chain and VTF from those blocks, and the SHA-256 of the blocks
// names the insignia on the wire and in material paths.
namespace TitanInsignia {

constexpr int kSize = 128;
constexpr std::size_t kTopMipBytes = (kSize / 4) * (kSize / 4) * 16;
constexpr std::size_t kIdLength = 32;
constexpr std::size_t kMaxSourceFileBytes = 16 * 1024 * 1024;
// Larger images are refused rather than scaled; the insignia itself is 128x128.
constexpr int kMaxSourceDimension = 512;

// DXT5 blocks of the 128x128 top mip, row-major, exactly kTopMipBytes.
using TopMip = std::vector<std::uint8_t>;

// Decodes an image file (PNG, JPEG, TGA, BMP, GIF, PSD), fits it into 128x128 keeping its aspect ratio
// (the rest stays transparent) and DXT5-encodes it. False with `error` set when the bytes are not a
// usable image.
bool EncodeImageFile(std::span<const std::uint8_t> file, TopMip& out, std::string& error);

// First 16 bytes of the SHA-256 of the top mip, as 32 lowercase hex digits. Empty when hashing fails.
std::string ComputeId(const TopMip& mip);
bool IsValidId(std::string_view id);

// The VTF 7.5 file for a top mip: DXT5 with all 8 mips and a 16x16 DXT1 thumbnail, with the stock
// insignia textures' header flags. Empty when `mip` is not kTopMipBytes long.
std::vector<std::uint8_t> BuildVtf(const TopMip& mip);

}
