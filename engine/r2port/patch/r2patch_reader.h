// Origin: C:\Users\r3muxd\tmp\r2port\physgen\r2patch_reader.h, first-party R1Delta patch reader supplied with physgen.
// Copyright (c) 2026 r1delta contributors.
//
// Inline reader for the R2PT format documented in FORMAT.md. The caller passes
// the expected map hash and its linked ZSTD_decompress function, avoiding a new
// zstd dependency inside this header.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

namespace r2patch {
inline constexpr size_t kHeaderSize = 64;
inline constexpr uint16_t kVersion = 1;
inline constexpr uint32_t kPhysicsLumpId = 0x1D;

using ZstdDecompress = size_t (*)(void* dst, size_t dstCapacity,
                                  const void* src, size_t compressedSize);

enum class ApplyError {
    None,
    NullArgument,
    ShortHeader,
    BadMagic,
    UnsupportedVersion,
    BadHeaderSize,
    UnsupportedLump,
    UnsupportedCodec,
    InvalidSize,
    MapHashMismatch,
    AllocationFailed,
    DecompressionFailed
};

struct ApplyResult {
    ApplyError error;
    uint32_t lumpId;
    explicit operator bool() const { return error == ApplyError::None; }
};

inline uint16_t ReadU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}
inline uint32_t ReadU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
inline uint64_t ReadU64(const uint8_t* p) {
    return static_cast<uint64_t>(ReadU32(p)) | (static_cast<uint64_t>(ReadU32(p + 4)) << 32);
}

// expectedMapSha256 must be SHA-256 of the exact maps/<map>.bsp bytes currently
// loaded by the runtime. maxUncompressedBytes is a caller-owned memory limit.
// On failure, output is empty and no BSP data is modified.
inline ApplyResult Apply(const uint8_t* patch, size_t patchSize,
                         const uint8_t expectedMapSha256[32],
                         uint64_t maxUncompressedBytes,
                         ZstdDecompress decompress,
                         std::vector<uint8_t>& output) {
    output.clear();
    if (!patch || !expectedMapSha256 || !decompress)
        return {ApplyError::NullArgument, 0};
    if (patchSize < kHeaderSize) return {ApplyError::ShortHeader, 0};
    if (std::memcmp(patch, "R2PT", 4) != 0) return {ApplyError::BadMagic, 0};
    if (ReadU16(patch + 4) != kVersion) return {ApplyError::UnsupportedVersion, 0};
    if (ReadU16(patch + 6) != kHeaderSize) return {ApplyError::BadHeaderSize, 0};
    const uint32_t lumpId = ReadU32(patch + 8);
    if (lumpId != kPhysicsLumpId) return {ApplyError::UnsupportedLump, 0};
    if (ReadU32(patch + 12) != 0) return {ApplyError::UnsupportedCodec, 0};
    const uint64_t uncompressedSize = ReadU64(patch + 16);
    const uint64_t compressedSize = ReadU64(patch + 24);
    if (!uncompressedSize || uncompressedSize > maxUncompressedBytes ||
        uncompressedSize > std::numeric_limits<size_t>::max() ||
        compressedSize != static_cast<uint64_t>(patchSize - kHeaderSize))
        return {ApplyError::InvalidSize, 0};
    if (std::memcmp(patch + 32, expectedMapSha256, 32) != 0)
        return {ApplyError::MapHashMismatch, 0};
    std::vector<uint8_t> decoded;
    if (uncompressedSize > decoded.max_size())
        return {ApplyError::InvalidSize, 0};
    try {
        decoded.resize(static_cast<size_t>(uncompressedSize));
    } catch (const std::bad_alloc&) {
        return {ApplyError::AllocationFailed, 0};
    }
    const size_t result = decompress(decoded.data(), decoded.size(), patch + kHeaderSize,
                                     static_cast<size_t>(compressedSize));
    if (result != decoded.size()) return {ApplyError::DecompressionFailed, 0};
    output.swap(decoded);
    return {ApplyError::None, lumpId};
}
} // namespace r2patch
