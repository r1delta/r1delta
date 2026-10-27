#include "r2_map_patch_source.h"

#include "engine/filesystem/r2_map_provider.h"
#include "engine/logging/logging.h"
#include "engine/r2port/patch/r2patch_reader.h"

#include <zstd.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <cstdio>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace r2port {
namespace {

constexpr std::uint64_t kMaximumPhysicsLumpBytes = 256ull * 1024 * 1024;
constexpr std::size_t kMaximumPatchFileBytes = 256ull * 1024 * 1024;
constexpr std::uint32_t kMaximumAinBodyBytes = 64u * 1024 * 1024;
constexpr std::uint32_t kAinFormatVersion = 2;
constexpr std::uint32_t kAinVersion = 54;

std::vector<std::uint8_t> ReadPatchFile(const std::filesystem::path& path, std::size_t maximumBytes)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return {};
    input.seekg(0, std::ios::end);
    const std::streamoff length = input.tellg();
    if (length <= 0 || static_cast<std::uint64_t>(length) > maximumBytes
        || static_cast<std::uint64_t>(length) > std::numeric_limits<std::size_t>::max())
        return {};

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    input.seekg(0, std::ios::beg);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        return {};
    return bytes;
}

bool ReadU32(const std::uint8_t* bytes, std::size_t size, std::size_t& offset, std::uint32_t& value)
{
    if (offset > size || sizeof(value) > size - offset)
        return false;
    value = r2patch::ReadU32(bytes + offset);
    offset += sizeof(value);
    return true;
}

bool ReadI32(const std::uint8_t* bytes, std::size_t size, std::size_t& offset, std::int32_t& value)
{
    std::uint32_t raw{};
    if (!ReadU32(bytes, size, offset, raw))
        return false;
    value = static_cast<std::int32_t>(raw);
    return true;
}

bool ReadU16(const std::uint8_t* bytes, std::size_t size, std::size_t& offset, std::uint16_t& value)
{
    if (offset > size || sizeof(value) > size - offset)
        return false;
    value = r2patch::ReadU16(bytes + offset);
    offset += sizeof(value);
    return true;
}

bool SkipRecords(std::size_t& offset, std::size_t size, std::size_t count, std::size_t recordSize)
{
    if (offset > size || (recordSize && count > (size - offset) / recordSize))
        return false;
    offset += count * recordSize;
    return true;
}

bool ReadCount(const std::uint8_t* bytes, std::size_t size, std::size_t& offset,
               std::size_t elementSize, std::size_t& count)
{
    std::int32_t signedCount{};
    if (!ReadI32(bytes, size, offset, signedCount) || signedCount < 0)
        return false;
    count = static_cast<std::size_t>(signedCount);
    return !elementSize || count <= (size - offset) / elementSize;
}

bool ValidateAinV54Body(const std::uint8_t* bytes, std::size_t size)
{
    if (!bytes)
        return false;
    std::size_t offset = 0;
    std::size_t nodeCount{};
    if (!ReadCount(bytes, size, offset, 68, nodeCount)
        || !SkipRecords(offset, size, nodeCount, 68))
        return false;

    std::size_t linkCount{};
    if (!ReadCount(bytes, size, offset, 10, linkCount))
        return false;
    for (std::size_t link = 0; link < linkCount; ++link) {
        if (offset > size || 4 > size - offset)
            return false;
        const std::uint16_t source = r2patch::ReadU16(bytes + offset);
        const std::uint16_t destination = r2patch::ReadU16(bytes + offset + 2);
        if (source >= nodeCount || destination >= nodeCount)
            return false;
        offset += 10;
    }

    if (!SkipRecords(offset, size, nodeCount, sizeof(std::int32_t)))
        return false;
    std::uint16_t traverseCount{};
    if (!ReadU16(bytes, size, offset, traverseCount) || traverseCount > 0x7fff
        || !SkipRecords(offset, size, traverseCount, 20))
        return false;

    for (int hull = 0; hull < 5; ++hull) {
        if (!SkipRecords(offset, size, 1, sizeof(std::int32_t)))
            return false;
        std::uint16_t bitCount{}, wordCount{};
        if (!ReadU16(bytes, size, offset, bitCount)
            || !ReadU16(bytes, size, offset, wordCount)
            || wordCount != (static_cast<std::size_t>(bitCount) + 31) / 32
            || !SkipRecords(offset, size, wordCount, sizeof(std::uint32_t)))
            return false;
    }

    std::size_t unknownNodeCount{};
    if (!ReadCount(bytes, size, offset, 26, unknownNodeCount))
        return false;
    for (std::size_t node = 0; node < unknownNodeCount; ++node) {
        if (!SkipRecords(offset, size, 1, 17))
            return false;
        std::size_t memberCount{};
        if (!ReadCount(bytes, size, offset, sizeof(std::int16_t), memberCount)
            || !SkipRecords(offset, size, memberCount, sizeof(std::int16_t)))
            return false;
        std::size_t linkMemberCount{};
        if (!ReadCount(bytes, size, offset, sizeof(std::int16_t), linkMemberCount)
            || !SkipRecords(offset, size, linkMemberCount, sizeof(std::int16_t))
            || !SkipRecords(offset, size, 1, 1))
            return false;
    }

    std::size_t unknownLinkCount{};
    if (!ReadCount(bytes, size, offset, 11, unknownLinkCount)
        || !SkipRecords(offset, size, unknownLinkCount, 11))
        return false;

    std::int32_t scriptVersion{};
    return ReadI32(bytes, size, offset, scriptVersion) && offset == size;
}

void WriteU32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value)
{
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
    bytes[offset + 2] = static_cast<std::uint8_t>(value >> 16);
    bytes[offset + 3] = static_cast<std::uint8_t>(value >> 24);
}

FileBytes ReadAinPatch(const std::filesystem::path& patchPath, std::string_view mapName,
                       const BspSha256& sourceBspSha256, std::int32_t mapVersion,
                       std::int32_t developerValue)
{
    if (mapVersion <= 0 || developerValue != 0)
        return {};

    const std::vector<std::uint8_t> patch = ReadPatchFile(patchPath, kMaximumPatchFileBytes);
    constexpr std::size_t fixedHeaderSize = 52;
    if (patch.size() < fixedHeaderSize || std::memcmp(patch.data(), "R2AP", 4) != 0)
        return {};
    if (r2patch::ReadU16(patch.data() + 4) != kAinFormatVersion)
        return {};

    const std::uint16_t headerLength = r2patch::ReadU16(patch.data() + 6);
    const std::uint16_t mapNameLength = r2patch::ReadU16(patch.data() + 8);
    const std::uint16_t compression = r2patch::ReadU16(patch.data() + 10);
    const std::uint32_t uncompressedLength = r2patch::ReadU32(patch.data() + 12);
    const std::uint32_t compressedLength = r2patch::ReadU32(patch.data() + 16);
    if (compression != 1 || mapNameLength != mapName.size()
        || headerLength < fixedHeaderSize + mapNameLength || headerLength > patch.size()
        || uncompressedLength == 0 || uncompressedLength > kMaximumAinBodyBytes
        || compressedLength != patch.size() - headerLength
        || std::memcmp(patch.data() + 20, sourceBspSha256.data(), sourceBspSha256.size()) != 0
        || std::memcmp(patch.data() + fixedHeaderSize, mapName.data(), mapNameLength) != 0)
        return {};

    const std::size_t reservedStart = fixedHeaderSize + mapNameLength;
    if (!std::all_of(patch.begin() + reservedStart, patch.begin() + headerLength,
        [](std::uint8_t byte) { return byte == 0; }))
        return {};

    std::vector<std::uint8_t> body(uncompressedLength);
    const std::size_t decoded = ZSTD_decompress(
        body.data(), body.size(), patch.data() + headerLength, compressedLength);
    if (ZSTD_isError(decoded) || decoded != body.size() || !ValidateAinV54Body(body.data(), body.size()))
        return {};

    std::vector<std::uint8_t> ain(12 + body.size());
    WriteU32(ain, 0, kAinVersion);
    WriteU32(ain, 4, static_cast<std::uint32_t>(mapVersion));
    WriteU32(ain, 8, 0);
    std::copy(body.begin(), body.end(), ain.begin() + 12);

    char message[512]{};
    std::snprintf(message, sizeof(message),
        "R1Delta: R2 map patch served map=%.*s file=maps/graphs/%.*s.ain bytes=%zu mapversion=%d developer=0\n",
        static_cast<int>(mapName.size()), mapName.data(),
        static_cast<int>(mapName.size()), mapName.data(), ain.size(), mapVersion);
    Msg("%s", message);
    return std::make_shared<const std::vector<std::uint8_t>>(std::move(ain));
}

class R2MapPatchSource final : public MapPatchSource {
public:
    std::shared_ptr<const MapPatch> LoadPatch(
        std::string_view mapName, const BspSha256& sourceBspSha256) override
    {
        const std::filesystem::path root = GetR2MapPatchDirectory();
        if (root.empty())
            return {};
        const std::filesystem::path path = root / "patches" / (std::string(mapName) + ".r2patch");
        const std::vector<std::uint8_t> bytes = ReadPatchFile(path, kMaximumPatchFileBytes);
        if (bytes.empty())
            return {};

        std::vector<std::uint8_t> physics;
        const r2patch::ApplyResult result = r2patch::Apply(
            bytes.data(), bytes.size(), sourceBspSha256.data(),
            kMaximumPhysicsLumpBytes, &ZSTD_decompress, physics);
        if (!result)
            return {};
        auto patch = std::make_shared<MapPatch>();
        patch->sourceBspSha256 = sourceBspSha256;
        patch->lumpOverrides.emplace(result.lumpId,
            std::make_shared<const std::vector<std::uint8_t>>(std::move(physics)));
        return patch;
    }

    FileBytes LoadFileOverride(std::string_view mapName, std::string_view normalizedPath,
        const BspSha256& sourceBspSha256,
        std::int32_t mapVersion, std::int32_t developerValue) override
    {
        const std::string expectedPath = "maps/graphs/" + std::string(mapName) + ".ain";
        if (normalizedPath != expectedPath)
            return {};

        const std::filesystem::path root = GetR2MapPatchDirectory();
        if (root.empty())
            return {};
        const std::filesystem::path directory = root / "patches";
        return ReadAinPatch(directory / (std::string(mapName) + ".ain.r2patch"), mapName,
            sourceBspSha256, mapVersion, developerValue);
    }
};

} // namespace

void InstallR2MapPatchSource()
{
    static std::once_flag installOnce;
    std::call_once(installOnce, []() {
        RegisterMapPatchSource(std::make_shared<R2MapPatchSource>());
    });
}

} // namespace r2port
