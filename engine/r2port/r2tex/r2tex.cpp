// Origin: C:\Users\r3muxd\tmp\r2port\r2tex. R2 texture conversion library bundled with r1delta under GPL-3.0.
// Local adaptation: family cache size is configurable with a 256 MiB default.
#include "r2tex.hpp"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <iostream>
#include <list>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <unordered_map>
#include <utility>

#define BCDEC_STATIC
#define BCDEC_BC4BC5_PRECISE
#define BCDEC_IMPLEMENTATION
#include "bcdec.h"

using _QWORD = std::uint64_t;
using _DWORD = std::uint32_t;
using _WORD = std::uint16_t;
using _BYTE = std::uint8_t;
enum PakDecodeMode_e { MODE_DISABLED = -1, MODE_RTECH, MODE_ZSTD };
struct RTechDecodeState_s {
    const std::uint8_t* inputBuf;
    std::uint8_t* outputBuf;
    std::uint64_t inputMask;
    std::uint64_t outputMask;
    std::size_t fileSize;
    std::size_t decompSize;
    std::uint64_t inputInvMask;
    std::uint64_t outputInvMask;
    std::uint32_t headerOffset;
    PakDecodeMode_e decodeMode;
    std::uint64_t inBufBytePos;
    std::uint64_t outBufBytePos;
    std::size_t bufferSizeNeeded;
    std::uint64_t currentByte;
    std::uint32_t currentBit;
    std::uint32_t dword6C;
    union { std::uint64_t qword70; bool allChunksStreamed; };
    union { std::size_t compressedStreamSize; std::size_t nextInputSize; };
    union { std::size_t decompressedStreamSize; void* zstreamContext; };
};
#include "rtech_decode.inc"

namespace r2tex {
namespace fs = std::filesystem;
namespace {

constexpr std::uint32_t kTxtr = 0x72747874u;
constexpr std::uint32_t kMatl = 0x6c74616du;
constexpr std::uint32_t kShds = 0x73646873u;
constexpr std::uint32_t kBspTextureData = 2;
constexpr std::uint32_t kBspTextureNameData = 43;
constexpr std::uint32_t kBspTextureNameTable = 44;
constexpr std::size_t kMaxPakDecompressed = 1024ull * 1024ull * 1024ull;
constexpr std::size_t kMaximumFamilyCacheLimit = 768ull * 1024ull * 1024ull;
std::atomic<std::size_t> gFamilyCacheLimit{256ull * 1024ull * 1024ull};
constexpr char kIndexMagic[8] = {'R','2','T','E','X','I','D','X'};
constexpr std::uint32_t kIndexVersion = 10;

#pragma pack(push, 1)
struct Hdr7 {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t flags;
    std::uint64_t createdTime;
    std::uint64_t crc;
    std::uint64_t cmpSize;
    std::uint64_t pad20;
    std::uint64_t dcmpSize;
    std::uint64_t pad30;
    std::uint16_t streamingFilesBufSize;
    std::uint16_t numSegments;
    std::uint16_t numPages;
    std::uint16_t patchCount;
    std::uint32_t numPointers;
    std::uint32_t numAssets;
    std::uint32_t numGuidRefs;
    std::uint32_t numDependencies;
    std::uint32_t numExternalAssetRefs;
    std::uint32_t externalAssetRefsSize;
};
struct PageHdr { std::int32_t segment, align, size; };
struct PagePtr { std::uint32_t index, offset; };
struct AssetEnt {
    std::uint64_t guid;
    std::uint64_t pad08;
    PagePtr head;
    PagePtr data;
    std::int64_t starpakOffset;
    std::uint16_t pageEnd;
    std::uint16_t remainingDeps;
    std::uint32_t dependentsIndex;
    std::uint32_t dependenciesIndex;
    std::uint32_t dependentsCount;
    std::uint32_t dependenciesCount;
    std::uint32_t headerSize;
    std::uint32_t version;
    std::uint32_t type;
};
#pragma pack(pop)
static_assert(sizeof(Hdr7) == 0x58, "RPak v7 header size");
static_assert(sizeof(AssetEnt) == 0x48, "RPak v7 asset size");

std::uint16_t u16(const std::uint8_t* p) { std::uint16_t v; std::memcpy(&v, p, 2); return v; }
std::uint32_t u32(const std::uint8_t* p) { std::uint32_t v; std::memcpy(&v, p, 4); return v; }
std::uint64_t u64(const std::uint8_t* p) { std::uint64_t v; std::memcpy(&v, p, 8); return v; }
std::int64_t i64(const std::uint8_t* p) { std::int64_t v; std::memcpy(&v, p, 8); return v; }
void put16(std::uint8_t* p, std::uint16_t v) { std::memcpy(p, &v, 2); }
void put32(std::uint8_t* p, std::uint32_t v) { std::memcpy(p, &v, 4); }
void putI32(std::uint8_t* p, std::int32_t v) { std::memcpy(p, &v, 4); }
void putFloat(std::uint8_t* p, float v) { std::memcpy(p, &v, 4); }
bool validUtf8(std::string_view text) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
    std::size_t i = 0;
    while (i < text.size()) {
        const unsigned char lead = bytes[i++];
        if (lead <= 0x7f) continue;
        std::uint32_t codepoint = 0;
        std::size_t continuationCount = 0;
        if (lead >= 0xc2 && lead <= 0xdf) { codepoint = lead & 0x1fu; continuationCount = 1; }
        else if (lead >= 0xe0 && lead <= 0xef) { codepoint = lead & 0x0fu; continuationCount = 2; }
        else if (lead >= 0xf0 && lead <= 0xf4) { codepoint = lead & 0x07u; continuationCount = 3; }
        else return false;
        if (continuationCount > text.size() - i) return false;
        for (std::size_t j = 0; j < continuationCount; ++j) {
            const unsigned char next = bytes[i++];
            if ((next & 0xc0u) != 0x80u) return false;
            if (j == 0 && ((lead == 0xe0 && next < 0xa0) || (lead == 0xed && next >= 0xa0) ||
                           (lead == 0xf0 && next < 0x90) || (lead == 0xf4 && next >= 0x90))) return false;
            codepoint = (codepoint << 6) | (next & 0x3fu);
        }
        if (codepoint > 0x10ffffu || (codepoint >= 0xd800u && codepoint <= 0xdfffu)) return false;
    }
    return true;
}


bool readWholeFile(const fs::path& path, std::vector<std::uint8_t>& out, std::string& error) {
    std::error_code ec;
    const auto length = fs::file_size(path, ec);
    if (ec || length > std::numeric_limits<std::size_t>::max()) {
        error = "cannot size file: " + path.u8string();
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) { error = "cannot open: " + path.u8string(); return false; }
    out.resize(static_cast<std::size_t>(length));
    if (!out.empty() && !in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()))) {
        error = "short read: " + path.u8string();
        out.clear();
        return false;
    }
    return true;
}

std::uint64_t nextPowerMask(std::uint64_t n) {
    if (n <= 1) return 0;
    std::uint64_t v = 1;
    while (v < n) {
        if (v > (std::numeric_limits<std::uint64_t>::max() >> 1)) throw std::runtime_error("pak size overflow");
        v <<= 1;
    }
    return v - 1;
}

struct PakInfo {
    fs::path path;
    std::string relativeName;
    std::string familyName;
    std::uint64_t fileSize = 0;
    std::int64_t writeStamp = 0;
    std::uint32_t family = 0;
    std::uint16_t patchCount = 0;
};

struct PakData {
    std::uint32_t infoIndex = 0;
    Hdr7 header{};
    std::vector<std::uint8_t> bytes;
    std::size_t pageHeadersOffset = 0;
    std::size_t assetTableOffset = 0;
    std::uint32_t firstPage = 0;
    std::int64_t patchStreamSize = 0;
    std::vector<std::size_t> pageOffsets;
    std::vector<std::int32_t> pageSizes;
    std::vector<std::string> starpaks;
    std::size_t memoryBytes() const { return bytes.capacity() + pageOffsets.capacity() * sizeof(std::size_t) + pageSizes.capacity() * sizeof(std::int32_t); }
};

std::string lowerAscii(std::string s) {
    for (char& c : s) {
        const auto b = static_cast<unsigned char>(c);
        if (b >= 'A' && b <= 'Z') c = static_cast<char>(b + ('a' - 'A'));
        else if (c == '\\') c = '/';
    }
    return s;
}

std::string familyNameFor(const std::string& filename) {
    std::string base = fs::u8path(filename).stem().u8string();
    const auto open = base.rfind('(');
    if (open != std::string::npos && !base.empty() && base.back() == ')' && open + 2 < base.size()) {
        bool digits = true;
        for (std::size_t i = open + 1; i + 1 < base.size(); ++i)
            if (base[i] < '0' || base[i] > '9') digits = false;
        if (digits) base.resize(open);
    }
    return lowerAscii(base);
}

bool readPakInfo(const fs::path& path, PakInfo& info, std::string& error) {
    std::error_code ec;
    info.path = path;
    info.relativeName = path.filename().u8string();
    info.familyName = familyNameFor(info.relativeName);
    info.fileSize = fs::file_size(path, ec);
    if (ec) { error = "cannot stat pak " + path.u8string(); return false; }
    const auto stamp = fs::last_write_time(path, ec);
    if (ec) { error = "cannot read pak timestamp " + path.u8string(); return false; }
    info.writeStamp = static_cast<std::int64_t>(stamp.time_since_epoch().count());
    std::ifstream in(path, std::ios::binary);
    std::array<std::uint8_t, sizeof(Hdr7)> h{};
    if (!in.read(reinterpret_cast<char*>(h.data()), static_cast<std::streamsize>(h.size()))) {
        error = "short pak header: " + path.u8string(); return false;
    }
    if (u32(h.data()) != 0x6b615052u || u16(h.data() + 4) != 7) {
        error = "not RPak v7: " + path.u8string(); return false;
    }
    info.patchCount = u16(h.data() + 0x3e);
    return true;
}

bool loadPak(const PakInfo& info, std::uint32_t infoIndex, PakData& pak, std::string& error) {
    pak.infoIndex = infoIndex;
    std::vector<std::uint8_t> input;
    if (!readWholeFile(info.path, input, error)) return false;
    if (input.size() < sizeof(Hdr7)) { error = "short RPak: " + info.relativeName; return false; }
    std::memcpy(&pak.header, input.data(), sizeof(Hdr7));
    if (pak.header.magic != 0x6b615052u || pak.header.version != 7) {
        error = "bad RPak header: " + info.relativeName; return false;
    }
    if (pak.header.flags & 0x8000u) { error = "unsupported Zstd RPak: " + info.relativeName; return false; }
    if (pak.header.flags & 0x100u) {
        if (!pak.header.dcmpSize || pak.header.dcmpSize > kMaxPakDecompressed || pak.header.cmpSize > input.size()) {
            error = "invalid RTech size: " + info.relativeName; return false;
        }
        const auto inMask = nextPowerMask(input.size() + 64);
        const auto outMask = nextPowerMask(pak.header.dcmpSize + 64);
        if (inMask > std::numeric_limits<std::size_t>::max() - 64 || outMask > std::numeric_limits<std::size_t>::max() - 64) {
            error = "RPak allocation overflow: " + info.relativeName; return false;
        }
        std::vector<std::uint8_t> inbuf(static_cast<std::size_t>(inMask + 1 + 64), 0);
        std::memcpy(inbuf.data(), input.data(), input.size());
        std::vector<std::uint8_t>().swap(input);
        std::vector<std::uint8_t> output(static_cast<std::size_t>(outMask + 1 + 64), 0);
        RTechDecodeState_s decoder{};
        decoder.decodeMode = MODE_RTECH;
        decoder.fileSize = inbuf.size();
        decoder.inputBuf = inbuf.data();
        decoder.outputBuf = output.data();
        decoder.inputMask = inMask;
        decoder.outputMask = outMask;
        decoder.inBufBytePos = sizeof(Hdr7);
        decoder.outBufBytePos = sizeof(Hdr7);
        const auto decodedHeader = Pak_RTechDecoderInit(&decoder, inbuf.data(), inMask,
            static_cast<std::size_t>(pak.header.cmpSize), 0, sizeof(Hdr7));
        if (decodedHeader != pak.header.dcmpSize) { error = "RTech size mismatch: " + info.relativeName; return false; }
        const bool complete = Pak_RTechStreamDecode(&decoder, static_cast<std::size_t>(pak.header.cmpSize),
            static_cast<std::size_t>(pak.header.dcmpSize));
        if (!complete || decoder.outBufBytePos != pak.header.dcmpSize) {
            error = "incomplete RTech stream: " + info.relativeName; return false;
        }
        std::memcpy(output.data(), &pak.header, sizeof(Hdr7));
        output.resize(static_cast<std::size_t>(pak.header.dcmpSize));
        pak.bytes = std::move(output);
    } else {
        pak.bytes = std::move(input);
    }
    const std::uint8_t* b = pak.bytes.data();
    const std::size_t n = pak.bytes.size();
    std::size_t off = sizeof(Hdr7);
    auto ensure = [&](std::uint64_t amount) { return amount <= n && off <= n - static_cast<std::size_t>(amount); };
    if (pak.header.patchCount) {
        const std::uint64_t patchHeaderBytes = 8ull + 18ull * pak.header.patchCount;
        if (!ensure(patchHeaderBytes)) { error = "truncated patch header: " + info.relativeName; return false; }
        const std::int32_t patchBytes = static_cast<std::int32_t>(u32(b + off));
        const std::int32_t firstPage = static_cast<std::int32_t>(u32(b + off + 4));
        if (patchBytes < 0 || firstPage < 0) { error = "invalid patch header: " + info.relativeName; return false; }
        pak.patchStreamSize = patchBytes;
        pak.firstPage = static_cast<std::uint32_t>(firstPage);
        off += static_cast<std::size_t>(patchHeaderBytes);
    }
    if (pak.firstPage > pak.header.numPages) { error = "invalid first patch page: " + info.relativeName; return false; }
    if (!ensure(pak.header.streamingFilesBufSize)) { error = "truncated starpak path list: " + info.relativeName; return false; }
    const std::size_t starEnd = off + pak.header.streamingFilesBufSize;
    while (off < starEnd) {
        const auto end = std::find(b + off, b + starEnd, static_cast<std::uint8_t>(0));
        if (end == b + starEnd) { error = "unterminated starpak path: " + info.relativeName; return false; }
        pak.starpaks.emplace_back(reinterpret_cast<const char*>(b + off), static_cast<std::size_t>(end - (b + off)));
        off = static_cast<std::size_t>(end - b) + 1;
    }
    off = starEnd;
    const std::uint64_t segmentBytes = 16ull * pak.header.numSegments;
    const std::uint64_t pageHeaderBytes = 12ull * pak.header.numPages;
    const std::uint64_t pointerBytes = 8ull * pak.header.numPointers;
    const std::uint64_t assetBytes = static_cast<std::uint64_t>(sizeof(AssetEnt)) * pak.header.numAssets;
    const std::uint64_t guidRefBytes = 8ull * pak.header.numGuidRefs;
    const std::uint64_t dependencyBytes = 4ull * pak.header.numDependencies;
    if (!ensure(segmentBytes + pageHeaderBytes + pointerBytes + assetBytes + guidRefBytes + dependencyBytes)) {
        error = "truncated RPak section tables: " + info.relativeName; return false;
    }
    off += static_cast<std::size_t>(segmentBytes);
    pak.pageHeadersOffset = off;
    pak.pageSizes.resize(pak.header.numPages);
    for (std::uint32_t i = 0; i < pak.header.numPages; ++i) {
        const std::int32_t size = static_cast<std::int32_t>(u32(b + off + i * 12 + 8));
        if (size < 0) { error = "invalid page size: " + info.relativeName; return false; }
        pak.pageSizes[i] = size;
    }
    off += static_cast<std::size_t>(pageHeaderBytes + pointerBytes);
    pak.assetTableOffset = off;
    off += static_cast<std::size_t>(assetBytes + guidRefBytes + dependencyBytes);
    if (pak.header.numExternalAssetRefs) {
        const std::uint64_t externalBytes = 4ull * pak.header.numExternalAssetRefs + pak.header.externalAssetRefsSize;
        if (!ensure(externalBytes)) { error = "truncated external refs: " + info.relativeName; return false; }
        off += static_cast<std::size_t>(externalBytes);
    }
    if (pak.patchStreamSize < 0 || !ensure(static_cast<std::uint64_t>(pak.patchStreamSize))) {
        error = "truncated patch stream: " + info.relativeName; return false;
    }
    off += static_cast<std::size_t>(pak.patchStreamSize);
    pak.pageOffsets.assign(pak.header.numPages, std::numeric_limits<std::size_t>::max());
    for (std::uint32_t i = pak.firstPage; i < pak.header.numPages; ++i) {
        const auto pageSize = static_cast<std::size_t>(pak.pageSizes[i]);
        if (!ensure(pageSize)) { error = "truncated page data: " + info.relativeName; return false; }
        pak.pageOffsets[i] = off;
        off += pageSize;
    }
    if (off != n) { error = "RPak section sum mismatch: " + info.relativeName; return false; }
    return true;
}

struct PageLocation {
    const PakData* pak = nullptr;
    std::size_t offset = 0;
    std::size_t size = 0;
};
struct Family {
    std::uint32_t id = 0;
    std::vector<std::shared_ptr<PakData>> paks;
    std::vector<PageLocation> pages;
    std::size_t bytes = 0;
};
const PakData* findPakOwner(const Family& family, std::uint32_t infoIndex) {
    for (const auto& pak : family.paks)
        if (pak->infoIndex == infoIndex) return pak.get();
    return nullptr;
}


bool resolvePageAtVersion(const Family& family, const PakData& owner, std::uint32_t page,
                          PageLocation& location) {
    if (page >= family.pages.size()) return false;
    std::size_t ownerIndex = family.paks.size();
    for (std::size_t i = 0; i < family.paks.size(); ++i) {
        if (family.paks[i].get() == &owner) { ownerIndex = i; break; }
    }
    if (ownerIndex == family.paks.size()) return false;
    for (std::size_t i = 0; i <= ownerIndex; ++i) {
        const PakData& pak = *family.paks[i];
        if (page >= pak.pageOffsets.size() || pak.pageOffsets[page] == std::numeric_limits<std::size_t>::max()) continue;
        location = {&pak, pak.pageOffsets[page], static_cast<std::size_t>(pak.pageSizes[page])};
    }
    return location.pak != nullptr;
}

bool readPageRange(const Family& family, const PakData& owner, PagePtr ptr, std::size_t length,
                   std::vector<std::uint8_t>& out, std::string& error) {
    if (ptr.index == 0xffffffffu || ptr.index >= family.pages.size()) { error = "invalid page index"; return false; }
    out.resize(length);
    std::size_t remaining = length, dest = 0;
    std::uint32_t page = ptr.index;
    std::size_t pageOffset = ptr.offset;
    while (remaining) {
        PageLocation loc;
        if (!resolvePageAtVersion(family, owner, page, loc) || pageOffset > loc.size) {
            error = "missing page in patch chain"; return false;
        }
        const std::size_t available = loc.size - pageOffset;
        if (!available) { error = "empty page in patch chain"; return false; }
        const std::size_t chunk = std::min(available, remaining);
        std::memcpy(out.data() + dest, loc.pak->bytes.data() + loc.offset + pageOffset, chunk);
        dest += chunk;
        remaining -= chunk;
        pageOffset = 0;
        if (remaining) {
            if (page + 1 >= family.pages.size()) { error = "asset range past final page"; return false; }
            ++page;
        }
    }
    return true;
}

bool readCString(const Family& family, const PakData& owner, PagePtr ptr, std::string& value) {
    if (ptr.index == 0xffffffffu || ptr.index >= family.pages.size()) return false;
    PageLocation loc;
    if (!resolvePageAtVersion(family, owner, ptr.index, loc) || ptr.offset >= loc.size) return false;
    const char* data = reinterpret_cast<const char*>(loc.pak->bytes.data() + loc.offset + ptr.offset);
    const std::size_t maximum = loc.size - ptr.offset;
    const void* nul = std::memchr(data, 0, maximum);
    if (!nul) return false;
    const auto length = static_cast<const char*>(nul) - data;
    if (length <= 0 || length > 4096) return false;
    value.assign(data, static_cast<std::size_t>(length));
    return true;
}


bool readPageHeader(const Family& family, const PakData& owner, PagePtr ptr, std::size_t length,
                    std::vector<std::uint8_t>& header) {
    std::string ignored;
    return readPageRange(family, owner, ptr, length, header, ignored);
}


std::uint32_t typeFromFourcc(const char* s) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(s[0])) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(s[1])) << 8) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(s[2])) << 16) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(s[3])) << 24);
}

struct AssetKey {
    std::uint64_t guid;
    std::uint32_t type;
    bool operator==(const AssetKey& o) const noexcept { return guid == o.guid && type == o.type; }
};
struct AssetKeyHash {
    std::size_t operator()(const AssetKey& k) const noexcept {
        std::uint64_t x = k.guid ^ (static_cast<std::uint64_t>(k.type) << 32);
        x ^= x >> 33; x *= 0xff51afd7ed558ccdull; x ^= x >> 33;
        return static_cast<std::size_t>(x);
    }
};
struct AssetRecord {
    std::uint64_t guid = 0;
    std::uint32_t type = 0;
    std::uint32_t pakIndex = 0;
    std::uint32_t assetIndex = 0;
    PagePtr head{};
    PagePtr data{};
    std::int64_t starpakOffset = -1;
    std::uint32_t version = 0;
    std::uint32_t headerSize = 0;
    std::string name;
};

bool readAssetEnt(const PakData& pak, std::uint32_t index, AssetEnt& asset) {
    if (index >= pak.header.numAssets) return false;
    const std::size_t off = pak.assetTableOffset + static_cast<std::size_t>(index) * sizeof(AssetEnt);
    if (off > pak.bytes.size() || sizeof(AssetEnt) > pak.bytes.size() - off) return false;
    std::memcpy(&asset, pak.bytes.data() + off, sizeof(asset));
    return true;
}

bool readHeaderAndName(const Family& family, const PakData& owner, const AssetEnt& asset,
                       std::vector<std::uint8_t>& header, std::string& name) {
    std::size_t required = asset.type == kTxtr ? 0x38u : asset.type == kMatl ? 0xd0u : 0x58u;
    if (asset.headerSize && asset.headerSize < required) return false;
    if (!readPageHeader(family, owner, asset.head, required, header)) return false;
    std::size_t ptrOffset = asset.type == kMatl ? 0x18u : asset.type == kTxtr || asset.type == kShds ? 0x08u : required;
    if (ptrOffset + sizeof(PagePtr) > header.size()) return false;
    PagePtr namePtr{};
    std::memcpy(&namePtr, header.data() + ptrOffset, sizeof(PagePtr));
    if (!readCString(family, owner, namePtr, name)) return false;
    if (!validUtf8(name)) return false;
    for (unsigned char c : name) if (c < 0x20u || c == 0x7fu) return false;
    return true;
}

bool writeString(std::ofstream& out, const std::string& s) {
    if (s.size() > 0xffffffffu) return false;
    const std::uint32_t n = static_cast<std::uint32_t>(s.size());
    out.write(reinterpret_cast<const char*>(&n), sizeof(n));
    if (n) out.write(s.data(), n);
    return static_cast<bool>(out);
}
bool readString(std::ifstream& in, std::string& s, std::uint32_t max = 1u << 20) {
    std::uint32_t n = 0;
    if (!in.read(reinterpret_cast<char*>(&n), sizeof(n)) || n > max) return false;
    s.resize(n);
    return !n || static_cast<bool>(in.read(s.data(), n));
}

template<class T> bool writePod(std::ofstream& out, const T& v) {
    out.write(reinterpret_cast<const char*>(&v), sizeof(v)); return static_cast<bool>(out);
}
template<class T> bool readPod(std::ifstream& in, T& v) {
    return static_cast<bool>(in.read(reinterpret_cast<char*>(&v), sizeof(v)));
}

struct CacheEntry {
    std::shared_ptr<Family> family;
    std::list<std::uint32_t>::iterator age;
};
struct Database {
    fs::path installDir;
    fs::path pakDir;
    fs::path cacheFile;
    std::string installIdentity;
    std::vector<PakInfo> paks;
    std::vector<std::vector<std::uint32_t>> familyMembers;
    std::vector<std::string> familyNames;
    std::unordered_map<AssetKey, AssetRecord, AssetKeyHash> assets;
    std::mutex familyMutex;
    std::unordered_map<std::uint32_t, CacheEntry> families;
    std::list<std::uint32_t> lru;
    std::size_t familyBytes = 0;

    std::shared_ptr<Family> getFamily(std::uint32_t id, std::string& error) {
        std::lock_guard<std::mutex> lock(familyMutex);
        auto found = families.find(id);
        if (found != families.end()) {
            lru.splice(lru.begin(), lru, found->second.age);
            return found->second.family;
        }
        if (id >= familyMembers.size()) { error = "invalid package family"; return {}; }
        auto family = std::make_shared<Family>();
        family->id = id;
        std::uint32_t maxPages = 0;
        for (std::uint32_t pi : familyMembers[id]) {
            auto pak = std::make_shared<PakData>();
            std::string why;
            if (!loadPak(paks[pi], pi, *pak, why)) continue;
            family->bytes += pak->memoryBytes();
            maxPages = std::max(maxPages, static_cast<std::uint32_t>(pak->pageOffsets.size()));
            family->paks.push_back(std::move(pak));
        }
        family->pages.resize(maxPages);
        for (const auto& pak : family->paks) {
            for (std::uint32_t page = pak->firstPage; page < pak->pageOffsets.size(); ++page) {
                const std::size_t pageOffset = pak->pageOffsets[page];
                if (pageOffset == std::numeric_limits<std::size_t>::max()) continue;
                family->pages[page] = {pak.get(), pageOffset, static_cast<std::size_t>(pak->pageSizes[page])};
            }
        }
        if (family->paks.empty()) { error = "cannot load any pak in family " + familyNames[id]; return {}; }
        lru.push_front(id);
        families.emplace(id, CacheEntry{family, lru.begin()});
        familyBytes += family->bytes;
        while (familyBytes > gFamilyCacheLimit.load(std::memory_order_relaxed) && lru.size() > 1) {
            const std::uint32_t oldId = lru.back();
            if (oldId == id) break;
            auto old = families.find(oldId);
            if (old != families.end()) {
                familyBytes -= old->second.family->bytes;
                families.erase(old);
            }
            lru.pop_back();
        }
        return family;
    }
};

std::mutex g_openMutex;
std::shared_ptr<Database> g_database;

bool enumeratePaks(Database& db, std::string& error) {
    db.pakDir = db.installDir / L"paks" / L"Win64";
    std::error_code ec;
    if (!fs::is_directory(db.pakDir, ec)) { error = "R2 install has no paks/Win64 directory: " + db.pakDir.u8string(); return false; }
    std::vector<fs::path> paths;
    for (fs::directory_iterator it(db.pakDir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        if (lowerAscii(it->path().extension().u8string()) == ".rpak") paths.push_back(it->path());
    }
    if (ec) { error = "cannot enumerate rpaks: " + ec.message(); return false; }
    std::sort(paths.begin(), paths.end(), [](const fs::path& a, const fs::path& b) {
        return lowerAscii(a.filename().u8string()) < lowerAscii(b.filename().u8string());
    });
    if (paths.empty()) { error = "no rpaks found in " + db.pakDir.u8string(); return false; }
    std::map<std::string, std::vector<std::uint32_t>> byFamily;
    for (const auto& path : paths) {
        PakInfo info;
        if (!readPakInfo(path, info, error)) return false;
        const std::uint32_t index = static_cast<std::uint32_t>(db.paks.size());
        db.paks.push_back(std::move(info));
        byFamily[db.paks.back().familyName].push_back(index);
    }
    db.familyNames.reserve(byFamily.size());
    db.familyMembers.reserve(byFamily.size());
    std::uint32_t familyIndex = 0;
    for (auto& pair : byFamily) {
        db.familyNames.push_back(pair.first);
        auto& members = pair.second;
        std::sort(members.begin(), members.end(), [&](std::uint32_t a, std::uint32_t b) {
            if (db.paks[a].patchCount != db.paks[b].patchCount) return db.paks[a].patchCount < db.paks[b].patchCount;
            return lowerAscii(db.paks[a].relativeName) < lowerAscii(db.paks[b].relativeName);
        });
        for (auto pi : members) {
            db.paks[pi].family = familyIndex;
            db.paks[pi].familyName = pair.first;
        }
        db.familyMembers.push_back(members);
        ++familyIndex;
    }
    return true;
}

bool cacheMatches(const fs::path& cachePath, Database& db, std::string& error) {
    std::ifstream in(cachePath, std::ios::binary);
    if (!in) return false;
    // The cached database is validated against the install path and every pak timestamp.
    std::array<char, 8> magic{};
    std::uint32_t version = 0, pakCount = 0;
    std::uint64_t assetCount = 0;
    std::string installIdentity;
    if (!in.read(magic.data(), magic.size()) || magic != std::array<char,8>{'R','2','T','E','X','I','D','X'} ||
        !readPod(in, version) || version != kIndexVersion || !readString(in, installIdentity, 32768) ||
        installIdentity != db.installIdentity || !readPod(in, pakCount) || !readPod(in, assetCount) ||
        pakCount != db.paks.size() || assetCount > 1000000ull) return false;
    for (std::uint32_t i = 0; i < pakCount; ++i) {
        std::string name;
        std::uint64_t size = 0;
        std::int64_t stamp = 0;
        std::uint32_t family = 0;
        std::uint16_t patch = 0;
        if (!readString(in, name, 4096) || !readPod(in, size) || !readPod(in, stamp) || !readPod(in, family) || !readPod(in, patch)) return false;
        const auto& expected = db.paks[i];
        if (name != expected.relativeName || size != expected.fileSize || stamp != expected.writeStamp ||
            family != expected.family || patch != expected.patchCount) return false;
    }
    std::unordered_map<AssetKey, AssetRecord, AssetKeyHash> records;
    records.reserve(static_cast<std::size_t>(assetCount));
    for (std::uint64_t i = 0; i < assetCount; ++i) {
        AssetRecord r{};
        if (!readPod(in, r.guid) || !readPod(in, r.type) || !readPod(in, r.pakIndex) || !readPod(in, r.assetIndex) ||
            !readPod(in, r.head.index) || !readPod(in, r.head.offset) || !readPod(in, r.data.index) || !readPod(in, r.data.offset) ||
            !readPod(in, r.starpakOffset) || !readPod(in, r.version) || !readPod(in, r.headerSize) || !readString(in, r.name, 4096)) return false;
        if (r.pakIndex >= db.paks.size() || (r.type != kTxtr && r.type != kMatl && r.type != kShds) || r.name.empty()) return false;
        records.emplace(AssetKey{r.guid, r.type}, std::move(r));
    }
    if (!in) return false;
    db.assets = std::move(records);
    error.clear();
    return true;
}

bool saveIndex(const fs::path& path, const Database& db, std::string& error) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) { error = "cannot create cache directory: " + ec.message(); return false; }
    const fs::path tmp = path.wstring() + L".tmp";
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) { error = "cannot create index: " + tmp.u8string(); return false; }
    out.write(kIndexMagic, sizeof(kIndexMagic));
    const std::uint32_t version = kIndexVersion;
    const std::uint32_t pakCount = static_cast<std::uint32_t>(db.paks.size());
    const std::uint64_t assetCount = db.assets.size();
    if (!writePod(out, version) || !writeString(out, db.installIdentity) || !writePod(out, pakCount) || !writePod(out, assetCount)) {
        error = "cannot write cache header"; return false;
    }
    for (const auto& p : db.paks) {
        if (!writeString(out, p.relativeName) || !writePod(out, p.fileSize) || !writePod(out, p.writeStamp) ||
            !writePod(out, p.family) || !writePod(out, p.patchCount)) { error = "cannot write cache pak list"; return false; }
    }
    std::vector<const AssetRecord*> sorted;
    sorted.reserve(db.assets.size());
    for (const auto& pair : db.assets) sorted.push_back(&pair.second);
    std::sort(sorted.begin(), sorted.end(), [](const AssetRecord* a, const AssetRecord* b) {
        if (a->type != b->type) return a->type < b->type;
        return a->guid < b->guid;
    });
    for (const AssetRecord* r : sorted) {
        if (!writePod(out, r->guid) || !writePod(out, r->type) || !writePod(out, r->pakIndex) || !writePod(out, r->assetIndex) ||
            !writePod(out, r->head.index) || !writePod(out, r->head.offset) || !writePod(out, r->data.index) || !writePod(out, r->data.offset) ||
            !writePod(out, r->starpakOffset) || !writePod(out, r->version) || !writePod(out, r->headerSize) || !writeString(out, r->name)) {
            error = "cannot write cache asset table"; return false;
        }
    }
    out.close();
    if (!out) { error = "cannot finish index write"; return false; }
    fs::remove(path, ec);
    ec.clear();
    fs::rename(tmp, path, ec);
    if (ec) { error = "cannot replace index: " + ec.message(); fs::remove(tmp, ec); return false; }
    return true;
}

bool buildIndex(Database& db, std::string& error) {
    const auto begin = std::chrono::steady_clock::now();
    db.assets.clear();
    for (std::uint32_t familyId = 0; familyId < db.familyMembers.size(); ++familyId) {
        auto family = db.getFamily(familyId, error);
        if (!family) continue;
        for (const auto& pakPtr : family->paks) {
            const PakData& pak = *pakPtr;
            for (std::uint32_t ai = 0; ai < pak.header.numAssets; ++ai) {
                AssetEnt asset{};
                if (!readAssetEnt(pak, ai, asset) || (asset.type != kTxtr && asset.type != kMatl && asset.type != kShds)) continue;
                const std::uint32_t requiredVersion = asset.type == kTxtr || asset.type == kShds ? 8u : 12u;
                if (asset.version != requiredVersion) continue;
                std::vector<std::uint8_t> header;
                std::string name;
                if (!readHeaderAndName(*family, pak, asset, header, name)) continue;
                AssetRecord record;
                record.guid = asset.guid;
                record.type = asset.type;
                record.pakIndex = pak.infoIndex;
                record.assetIndex = ai;
                record.head = asset.head;
                record.data = asset.data;
                record.starpakOffset = asset.starpakOffset;
                record.version = asset.version;
                record.headerSize = asset.headerSize;
                record.name = name;
                const AssetKey key{record.guid, record.type};
                auto found = db.assets.find(key);
                if (found == db.assets.end()) {
                    db.assets.emplace(key, std::move(record));
                } else {
                    const auto& oldPak = db.paks[found->second.pakIndex];
                    const auto& newPak = db.paks[record.pakIndex];
                    const auto pageIsLocal = [](const PakData& pak, PagePtr ptr) {
                        return ptr.index < pak.pageOffsets.size() &&
                            pak.pageOffsets[ptr.index] != std::numeric_limits<std::size_t>::max();
                    };
                    // Patch asset tables repeat inherited entries; only local pages identify replacements.
                    if (newPak.family == oldPak.family && newPak.patchCount >= oldPak.patchCount &&
                        (pageIsLocal(pak, record.head) || pageIsLocal(pak, record.data))) {
                        found->second = std::move(record);
                    }
                }
            }
        }
        std::cerr << "index: scanned family " << (familyId + 1) << "/" << db.familyMembers.size()
                  << " (" << db.familyNames[familyId] << ")\r";
    }
    std::cerr << "\n";
    if (db.assets.empty()) { error = "no txtr/matl assets indexed"; return false; }
    const auto secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    std::cerr << "r2tex: indexed " << db.assets.size() << " txtr/matl/shds assets from " << db.paks.size()
              << " rpaks in " << std::fixed << std::setprecision(2) << secs << " s\n";
    return true;
}

bool loadIndex(Database& db, std::string& error, bool& cacheHit) {
    cacheHit = false;
    if (cacheMatches(db.cacheFile, db, error)) {
        cacheHit = true;
        return true;
    }
    error.clear();
    if (!buildIndex(db, error)) return false;
    std::string saveError;
    if (!saveIndex(db.cacheFile, db, saveError)) std::cerr << "r2tex: warning: " << saveError << "\n";
    return true;
}

std::uint64_t hashGuid(std::string_view input) {
    std::uint64_t hash = 0;
    std::uint64_t mixed = 0;
    std::uint64_t multiplied = 0;
    std::size_t offset = 0;
    for (;;) {
        std::uint32_t word = 0;
        std::uint32_t slashMask = 0;
        int terminal = 0;
        for (std::uint32_t i = 0; i < 4; ++i) {
            const std::size_t at = offset + i;
            if (at >= input.size() || input[at] == '\0') {
                terminal = static_cast<int>(i);
                break;
            }
            unsigned char c = static_cast<unsigned char>(input[at]);
            if (c == '\\' || c == '/') {
                slashMask |= (0x80u << (8 * i));
                if (c == '\\') c = static_cast<unsigned char>(c - 45);
            }
            c &= 0xdfu;
            word |= static_cast<std::uint32_t>(c) << (8 * i);
            terminal = -1;
        }
        std::uint32_t terminalMask = 0xffffffffu;
        if (terminal >= 0) {
            terminalMask = terminal == 3 ? 0x00ffffffu : terminal == 2 ? 0x0000ffffu : terminal == 1 ? 0x000000ffu : 0u;
            // The original mask includes the zero byte; it does not alter the hashed word.
        }
        (void)slashMask;
        const std::uint64_t mixedWord = (0xFB8C4D96501ull * static_cast<std::uint64_t>(word & terminalMask)) >> 24;
        multiplied = 0x0633D5F1ull * hash;
        mixed = mixedWord;
        if (terminal >= 0) {
            const std::size_t length = offset + static_cast<std::size_t>(terminal);
            return mixed + multiplied - 0x00AE502812AA7333ull * static_cast<std::uint32_t>(length);
        }
        const std::uint64_t sum = multiplied + mixed;
        hash = (sum >> 61) ^ sum;
        offset += 4;
    }
}

std::string cleanAssetName(std::string_view raw, bool removeExtension) {
    std::string name(raw);
    for (char& c : name) if (c == '\\') c = '/';
    while (name.rfind("materials/", 0) == 0) name.erase(0, 10);
    while (!name.empty() && name.front() == '/') name.erase(name.begin());
    if (removeExtension) {
        const auto slash = name.find_last_of('/');
        const auto dot = name.find_last_of('.');
        if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) name.resize(dot);
    }
    name = lowerAscii(std::move(name));
    return name;
}

bool validAssetName(const std::string& name) {
    if (!validUtf8(name) || name.empty() || name.size() >= 250) return false;
    if (name.front() == '/' || name.find(':') != std::string::npos) return false;
    std::size_t start = 0;
    while (start <= name.size()) {
        const auto end = name.find('/', start);
        const auto part = name.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (part.empty() || part == "." || part == "..") return false;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}

std::uint64_t textureGuid(std::string_view rawName) {
    const std::string name = cleanAssetName(rawName, true);
    return hashGuid("texture/" + name + ".rpak");
}
std::uint64_t materialGuid(std::string_view rawName, const char* variant) {
    const std::string name = cleanAssetName(rawName, true);
    return hashGuid("material/" + name + "_" + variant + ".rpak");
}

std::shared_ptr<Database> getDatabase() {
    std::lock_guard<std::mutex> lock(g_openMutex);
    return g_database;
}

const AssetRecord* findRecord(const Database& db, std::uint64_t guid, std::uint32_t type) {
    auto it = db.assets.find(AssetKey{guid, type});
    return it == db.assets.end() ? nullptr : &it->second;
}

bool readAssetHeader(const Database& db, const AssetRecord& record, const std::shared_ptr<Family>& family,
                     std::vector<std::uint8_t>& header, std::string& error) {
    if (record.pakIndex >= db.paks.size()) { error = "asset pak index is invalid"; return false; }
    const PakData* owner = nullptr;
    for (const auto& p : family->paks) if (p->infoIndex == record.pakIndex) { owner = p.get(); break; }
    if (!owner) { error = "asset pak is unavailable"; return false; }
    const std::size_t size = record.type == kTxtr ? 0x38u : record.type == kMatl ? 0xd0u : 0x58u;
    if (!readPageRange(*family, *owner, record.head, size, header, error)) return false;
    return true;
}

struct FormatInfo {
    bool block = false;
    std::uint8_t bytes = 0;
};
FormatInfo sourceFormat(std::uint16_t fmt) {
    if (fmt == 0 || fmt == 1 || fmt == 6 || fmt == 7) return {true, 8};
    if (fmt >= 2 && fmt <= 5) return {true, 16};
    if (fmt >= 8 && fmt <= 13) return {true, 16};
    if (fmt == 31 || fmt == 32) return {false, 4};
    if (fmt == 44) return {false, 2};
    if (fmt == 53 || fmt == 57) return {false, 1};
    return {};
}
std::size_t mipSize(std::uint16_t fmt, std::uint32_t w, std::uint32_t h) {
    const FormatInfo fi = sourceFormat(fmt);
    if (!fi.bytes || !w || !h) return 0;
    if (fi.block) return static_cast<std::size_t>(fi.bytes) * std::max(1u, (w + 3) / 4) * std::max(1u, (h + 3) / 4);
    if (static_cast<std::uint64_t>(w) * h > std::numeric_limits<std::size_t>::max() / fi.bytes) return 0;
    return static_cast<std::size_t>(fi.bytes) * w * h;
}
std::size_t aligned16(std::size_t n) { return (n + 15u) & ~std::size_t(15u); }

bool readAtExact(const fs::path& path, std::uint64_t offset, std::uint8_t* out, std::size_t count, std::string& error) {
    if (count == 0) return true;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
    if (file == INVALID_HANDLE_VALUE) { error = "cannot open starpak " + path.u8string(); return false; }
    std::size_t done = 0;
    bool ok = true;
    while (done < count) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(count - done, 1u << 20));
        OVERLAPPED ov{};
        const std::uint64_t at = offset + done;
        ov.Offset = static_cast<DWORD>(at & 0xffffffffu);
        ov.OffsetHigh = static_cast<DWORD>(at >> 32);
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ov.hEvent) { ok = false; error = "cannot create starpak read event"; break; }
        DWORD received = 0;
        BOOL result = ReadFile(file, out + done, chunk, &received, &ov);
        if (!result && GetLastError() == ERROR_IO_PENDING) result = GetOverlappedResult(file, &ov, &received, TRUE);
        CloseHandle(ov.hEvent);
        if (!result || received != chunk) { ok = false; error = "short starpak range read: " + path.u8string(); break; }
        done += received;
    }
    CloseHandle(file);
    return ok;
}

struct ImageMip { std::uint32_t width = 0, height = 0; std::vector<std::uint8_t> bytes; };

std::uint8_t toByte(float v) {
    if (!std::isfinite(v)) v = 0.0f;
    v = std::min(1.0f, std::max(0.0f, v));
    return static_cast<std::uint8_t>(std::lround(v * 255.0f));
}
float encodeSrgb(float v) {
    v = std::max(0.0f, v);
    return v <= 0.0031308f ? 12.92f * v : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}
std::uint8_t toneMapHdr(float v) {
    if (!std::isfinite(v) || v <= 0.0f) return 0;
    const float mapped = v / (1.0f + v);
    return toByte(encodeSrgb(mapped));
}

bool decodeMip(std::uint16_t fmt, std::uint32_t w, std::uint32_t h,
               const std::uint8_t* src, std::size_t srcSize,
               std::uint16_t& imageFormat, std::uint32_t& flags, ImageMip& out,
               std::string& error) {
    out.width = w; out.height = h;
    const FormatInfo fi = sourceFormat(fmt);
    const std::size_t required = mipSize(fmt, w, h);
    if (!required || srcSize < required) { error = "invalid or unsupported source mip format " + std::to_string(fmt); return false; }
    bool srgb = fmt == 1 || fmt == 3 || fmt == 5 || fmt == 13 || fmt == 32;
    bool hasAlpha = fmt == 2 || fmt == 3 || fmt == 4 || fmt == 5 || fmt == 12 || fmt == 13 || fmt == 31 || fmt == 32;
    if (fmt == 0 || fmt == 1) imageFormat = 13;
    else if (fmt == 2 || fmt == 3) imageFormat = 14;
    else if (fmt == 4 || fmt == 5) imageFormat = 15;
    else if (fmt == 6) imageFormat = 35;
    else if (fmt == 8) imageFormat = 34;
    else if (fmt == 31 || fmt == 32) imageFormat = 0;
    else if (fmt == 44) imageFormat = 64;
    else if (fmt == 53) imageFormat = 62;
    else if (fmt == 57) imageFormat = 8;
    else if (fmt == 12 || fmt == 13 || fmt == 10 || fmt == 11 || fmt == 7 || fmt == 9) imageFormat = 0;
    else { error = "unsupported R2 texture format " + std::to_string(fmt); return false; }
    if (srgb) flags |= 0x80000u;
    if (hasAlpha && (imageFormat == 0 || imageFormat == 14 || imageFormat == 15)) flags |= 0x2000u;

    if (fmt == 0 || fmt == 1 || fmt == 2 || fmt == 3 || fmt == 4 || fmt == 5 || fmt == 6 || fmt == 8 || fmt == 31 || fmt == 32 || fmt == 44 || fmt == 53 || fmt == 57) {
        out.bytes.assign(src, src + required);
        return true;
    }

    if (static_cast<std::uint64_t>(w) * h > std::numeric_limits<std::size_t>::max() / 4) {
        error = "decoded mip is too large"; return false;
    }
    out.bytes.assign(static_cast<std::size_t>(w) * h * 4, 0xff);
    const std::uint32_t blocksX = (w + 3) / 4;
    const std::uint32_t blocksY = (h + 3) / 4;
    std::array<std::uint8_t, 64> rgba{};
    for (std::uint32_t by = 0; by < blocksY; ++by) {
        for (std::uint32_t bx = 0; bx < blocksX; ++bx) {
            const std::uint8_t* block = src + (static_cast<std::size_t>(by) * blocksX + bx) * fi.bytes;
            if (fmt == 12 || fmt == 13) {
                bcdec_bc7(block, rgba.data(), 16);
            } else if (fmt == 10 || fmt == 11) {
                std::array<float, 48> rgb{};
                bcdec_bc6h_float(block, rgb.data(), 4 * 3, fmt == 11 ? 1 : 0);
                for (std::size_t p = 0; p < 16; ++p) {
                    rgba[p * 4 + 0] = toneMapHdr(rgb[p * 3 + 0]);
                    rgba[p * 4 + 1] = toneMapHdr(rgb[p * 3 + 1]);
                    rgba[p * 4 + 2] = toneMapHdr(rgb[p * 3 + 2]);
                    rgba[p * 4 + 3] = 255;
                }
            } else {
                std::array<float, 32> channels{};
                if (fmt == 7) bcdec_bc4_float(block, channels.data(), static_cast<int>(4 * sizeof(float)), 1);
                else bcdec_bc5_float(block, channels.data(), static_cast<int>(4 * 2 * sizeof(float)), 1);
                for (std::size_t p = 0; p < 16; ++p) {
                    const float x = channels[p * (fmt == 7 ? 1 : 2) + 0];
                    const float y = fmt == 7 ? 0.0f : channels[p * 2 + 1];
                    rgba[p * 4 + 0] = toByte(x * 0.5f + 0.5f);
                    rgba[p * 4 + 1] = fmt == 7 ? 128 : toByte(y * 0.5f + 0.5f);
                    if (fmt == 9 && (flags & 0x80u)) {
                        const float nx = x, ny = y;
                        const float nz = std::sqrt(std::max(0.0f, 1.0f - nx * nx - ny * ny));
                        rgba[p * 4 + 2] = toByte(nz * 0.5f + 0.5f);
                    } else rgba[p * 4 + 2] = 255;
                    rgba[p * 4 + 3] = 255;
                }
            }
            const std::uint32_t widthInBlock = std::min(4u, w - bx * 4);
            const std::uint32_t heightInBlock = std::min(4u, h - by * 4);
            for (std::uint32_t y = 0; y < heightInBlock; ++y) {
                const std::size_t dstOffset = (static_cast<std::size_t>(by * 4 + y) * w + bx * 4) * 4;
                std::memcpy(out.bytes.data() + dstOffset, rgba.data() + y * 16, static_cast<std::size_t>(widthInBlock) * 4);
            }
        }
    }
    if (fmt == 10 || fmt == 11) flags |= 0x80000u;
    if (fmt == 12 || fmt == 13) {
        srgb = fmt == 13;
        if (srgb) flags |= 0x80000u;
    }
    return true;
}

bool isNormalName(std::string_view name) {
    const std::string n = lowerAscii(std::string(name));
    return n.size() >= 4 && (n.rfind("_nml") == n.size() - 4 || n.rfind("_normal") == n.size() - 7 || n.rfind("_bump") == n.size() - 5);
}

bool convertTextureWithDb(const std::shared_ptr<Database>& db, const AssetRecord& record,
                          const TextureOptions& options, std::vector<std::uint8_t>& vtf,
                          std::string& error) {
    auto family = db->getFamily(db->paks[record.pakIndex].family, error);
    if (!family) return false;
    std::vector<std::uint8_t> header;
    if (!readAssetHeader(*db, record, family, header, error)) return false;
    if (header.size() < 0x38) { error = "short txtr header"; return false; }
    const std::uint16_t width = u16(header.data() + 0x10);
    const std::uint16_t height = u16(header.data() + 0x12);
    const std::uint16_t depth = u16(header.data() + 0x14);
    const std::uint16_t format = u16(header.data() + 0x16);
    const std::uint32_t declaredDataSize = u32(header.data() + 0x18);
    const std::uint8_t opt = header[0x1d];
    const std::uint8_t arraySize = header[0x1e];
    const std::uint8_t layers = header[0x1f];
    const std::uint8_t perm = header[0x21];
    const std::uint8_t streamed = header[0x22];
    if (!width || !height || depth || arraySize || layers || !perm || opt) {
        error = "unsupported txtr dimensions, array, or optional mips"; return false;
    }
    const std::uint32_t count = static_cast<std::uint32_t>(opt) + perm + streamed;
    if (!count || count > 32 || count > 255) { error = "invalid txtr mip count"; return false; }
    const FormatInfo info = sourceFormat(format);
    if (!info.bytes) { error = "unsupported R2 texture format " + std::to_string(format); return false; }
    std::vector<ImageMip> images(count);
    const PakData* assetOwner = findPakOwner(*family, record.pakIndex);
    if (!assetOwner) { error = "texture pak is unavailable"; return false; }
    std::uint64_t permanentSize = 0;
    for (int m = static_cast<int>(count) - 1; m >= static_cast<int>(opt + streamed); --m) {
        const std::uint32_t mw = std::max(1u, static_cast<std::uint32_t>(width) >> m);
        const std::uint32_t mh = std::max(1u, static_cast<std::uint32_t>(height) >> m);
        const std::size_t raw = mipSize(format, mw, mh);
        if (!raw || permanentSize > std::numeric_limits<std::uint64_t>::max() - aligned16(raw)) {
            error = "invalid permanent mip size"; return false;
        }
        permanentSize += aligned16(raw);
        images[static_cast<std::size_t>(m)].width = mw;
        images[static_cast<std::size_t>(m)].height = mh;
    }
    if (permanentSize > std::numeric_limits<std::size_t>::max()) { error = "permanent mip data is too large"; return false; }
    if (declaredDataSize && permanentSize > declaredDataSize) {
        error = "permanent txtr data exceeds declared dataSize"; return false;
    }
    std::vector<std::uint8_t> permanentData;
    if (!readPageRange(*family, *assetOwner, record.data, static_cast<std::size_t>(permanentSize), permanentData, error)) return false;
    std::size_t permanentOffset = 0;
    for (int m = static_cast<int>(count) - 1; m >= static_cast<int>(opt + streamed); --m) {
        const auto& img = images[static_cast<std::size_t>(m)];
        const std::size_t raw = mipSize(format, img.width, img.height);
        if (permanentOffset > permanentData.size() || raw > permanentData.size() - permanentOffset) {
            error = "permanent mip range invalid"; return false;
        }
        images[static_cast<std::size_t>(m)].bytes.assign(
            permanentData.begin() + permanentOffset, permanentData.begin() + permanentOffset + raw);
        permanentOffset += aligned16(raw);
    }
    if (streamed) {
        if (record.starpakOffset < 0) { error = "streamed mips have no starpak offset"; return false; }
        const PakData* owner = assetOwner;
        if (!owner) { error = "texture pak is unavailable"; return false; }
        const auto starIndex = static_cast<std::uint32_t>(static_cast<std::uint64_t>(record.starpakOffset) & 0xfffu);
        const std::uint64_t starOffset = static_cast<std::uint64_t>(record.starpakOffset) & ~std::uint64_t(0xfffu);
        if (starIndex >= owner->starpaks.size()) { error = "starpak index is out of range"; return false; }
        fs::path starPath = db->pakDir / fs::u8path(owner->starpaks[starIndex]).filename();
        std::uint64_t streamBytes = 0;
        for (int m = static_cast<int>(opt + streamed) - 1; m >= static_cast<int>(opt); --m) {
            const std::uint32_t mw = std::max(1u, static_cast<std::uint32_t>(width) >> m);
            const std::uint32_t mh = std::max(1u, static_cast<std::uint32_t>(height) >> m);
            const auto raw = mipSize(format, mw, mh);
            if (!raw || streamBytes > std::numeric_limits<std::uint64_t>::max() - aligned16(raw)) { error = "streamed mip size overflow"; return false; }
            streamBytes += aligned16(raw);
        }
        if (streamBytes > std::numeric_limits<std::size_t>::max() || starOffset > std::numeric_limits<std::uint64_t>::max() - streamBytes) {
            error = "streamed mip range overflow"; return false;
        }
        std::vector<std::uint8_t> streamedData(static_cast<std::size_t>(streamBytes));
        if (!readAtExact(starPath, starOffset, streamedData.data(), streamedData.size(), error)) return false;
        std::size_t sourceOffset = 0;
        for (int m = static_cast<int>(opt + streamed) - 1; m >= static_cast<int>(opt); --m) {
            const std::uint32_t mw = std::max(1u, static_cast<std::uint32_t>(width) >> m);
            const std::uint32_t mh = std::max(1u, static_cast<std::uint32_t>(height) >> m);
            const std::size_t raw = mipSize(format, mw, mh);
            const std::size_t padded = aligned16(raw);
            ImageMip& img = images[static_cast<std::size_t>(m)];
            img.width = mw; img.height = mh;
            img.bytes.assign(streamedData.begin() + sourceOffset, streamedData.begin() + sourceOffset + raw);
            sourceOffset += padded;
        }
    }

    std::uint16_t imageFormat = 0xffff;
    std::uint32_t flags = isNormalName(record.name) ? 0x80u : 0u;
    const bool passBc7 = (format == 12 || format == 13) && options.bc7Passthrough;
    const bool passBc6h = (format == 10 || format == 11) && options.bc6hPassthrough;
    std::vector<ImageMip> converted;
    if (passBc7 || passBc6h) {
        imageFormat = passBc7 ? 67 : 66;
        if (passBc7) {
            flags |= 0x2000u;
            if (format == 13) flags |= 0x80000u;
        }
        converted = std::move(images);
    } else {
        converted.resize(count);
        for (std::uint32_t m = 0; m < count; ++m) {
            if (images[m].bytes.empty()) { error = "missing mip " + std::to_string(m); return false; }
            std::uint16_t thisFormat = 0xffff;
            std::uint32_t thisFlags = flags;
            if (!decodeMip(format, images[m].width, images[m].height, images[m].bytes.data(), images[m].bytes.size(),
                           thisFormat, thisFlags, converted[m], error)) return false;
            if (imageFormat == 0xffff) imageFormat = thisFormat;
            else if (imageFormat != thisFormat) { error = "inconsistent converted mip format"; return false; }
            flags |= thisFlags;
        }
    }
    std::uint64_t totalSize = 0x58;
    for (const auto& mip : converted) {
        if (mip.bytes.size() > std::numeric_limits<std::uint64_t>::max() - totalSize) { error = "VTF size overflow"; return false; }
        totalSize += mip.bytes.size();
    }
    if (totalSize > std::numeric_limits<std::size_t>::max()) { error = "VTF too large"; return false; }
    vtf.assign(static_cast<std::size_t>(totalSize), 0);
    std::memcpy(vtf.data(), "VTF\0", 4);
    put32(vtf.data() + 4, 7); put32(vtf.data() + 8, 5); put32(vtf.data() + 12, 0x58);
    put16(vtf.data() + 0x10, width); put16(vtf.data() + 0x12, height); put32(vtf.data() + 0x14, flags);
    put16(vtf.data() + 0x18, 1); put16(vtf.data() + 0x1a, 0);
    putFloat(vtf.data() + 0x20, 0.5f); putFloat(vtf.data() + 0x24, 0.5f); putFloat(vtf.data() + 0x28, 0.5f);
    putFloat(vtf.data() + 0x30, 1.0f);
    putI32(vtf.data() + 0x34, static_cast<std::int32_t>(imageFormat));
    vtf[0x38] = static_cast<std::uint8_t>(count);
    putI32(vtf.data() + 0x39, -1); vtf[0x3d] = 0; vtf[0x3e] = 0; put16(vtf.data() + 0x3f, 1);
    put32(vtf.data() + 0x44, 1);
    vtf[0x50] = 0x30; vtf[0x51] = 0; vtf[0x52] = 0; vtf[0x53] = 0; put32(vtf.data() + 0x54, 0x58);
    std::size_t outOffset = 0x58;
    for (int m = static_cast<int>(count) - 1; m >= 0; --m) {
        const auto& bytes = converted[static_cast<std::size_t>(m)].bytes;
        std::memcpy(vtf.data() + outOffset, bytes.data(), bytes.size());
        outOffset += bytes.size();
    }
    return true;
}

bool expandLayerMaskVtf(std::vector<std::uint8_t>& vtf, bool invert, std::string& error) {
    if (vtf.size() < 0x58 || std::memcmp(vtf.data(), "VTF\0", 4) != 0) {
        error = "invalid source VTF for layer mask";
        return false;
    }
    const std::uint32_t width = u16(vtf.data() + 0x10);
    const std::uint32_t height = u16(vtf.data() + 0x12);
    const std::uint32_t mipCount = vtf[0x38];
    const std::uint32_t format = u32(vtf.data() + 0x34);
    const std::uint32_t sourceOffset = u32(vtf.data() + 0x54);
    if (!width || !height || !mipCount || mipCount > 32 || sourceOffset < 0x58 ||
        sourceOffset > vtf.size()) {
        error = "invalid source VTF dimensions for layer mask";
        return false;
    }

    std::uint64_t outputSize = 0x58;
    for (std::uint32_t m = mipCount; m-- > 0;) {
        const std::uint64_t w = std::max(1u, width >> m);
        const std::uint64_t h = std::max(1u, height >> m);
        if (w > (std::numeric_limits<std::size_t>::max() - outputSize) / (h * 4)) {
            error = "expanded layer mask is too large";
            return false;
        }
        outputSize += w * h * 4;
    }
    if (outputSize > std::numeric_limits<std::size_t>::max()) {
        error = "expanded layer mask is too large";
        return false;
    }
    std::vector<std::uint8_t> expanded(static_cast<std::size_t>(outputSize), 0);
    std::memcpy(expanded.data(), vtf.data(), 0x58);
    put32(expanded.data() + 0x14, u32(vtf.data() + 0x14) & 0x0cu);
    putI32(expanded.data() + 0x34, 0);
    put32(expanded.data() + 0x54, 0x58);

    std::size_t input = sourceOffset;
    std::size_t output = 0x58;
    for (std::uint32_t m = mipCount; m-- > 0;) {
        const std::uint32_t w = std::max(1u, width >> m);
        const std::uint32_t h = std::max(1u, height >> m);
        const std::uint32_t blocksX = (w + 3) / 4;
        const std::uint32_t blocksY = (h + 3) / 4;
        std::size_t sourceBlockBytes = 0;
        if (format == 35) sourceBlockBytes = 8;
        else if (format == 13) sourceBlockBytes = 8;
        else if (format == 14 || format == 15 || format == 34) sourceBlockBytes = 16;
        std::size_t mipSourceBytes = 0;
        if (sourceBlockBytes) {
            if (static_cast<std::uint64_t>(blocksX) * blocksY >
                std::numeric_limits<std::size_t>::max() / sourceBlockBytes) {
                error = "layer mask mip size overflow";
                return false;
            }
            mipSourceBytes = static_cast<std::size_t>(blocksX) * blocksY * sourceBlockBytes;
        } else if (format == 0 || format == 8) {
            const std::size_t pixelBytes = format == 0 ? 4 : 3;
            if (static_cast<std::uint64_t>(w) * h >
                std::numeric_limits<std::size_t>::max() / pixelBytes) {
                error = "layer mask mip size overflow";
                return false;
            }
            mipSourceBytes = static_cast<std::size_t>(w) * h * pixelBytes;
        } else {
            error = "layer mask source VTF format " + std::to_string(format) + " is unsupported";
            return false;
        }
        if (input > vtf.size() || mipSourceBytes > vtf.size() - input) {
            error = "truncated source VTF layer mask mip";
            return false;
        }
        if (format == 0 || format == 8) {
            const std::size_t pixelBytes = format == 0 ? 4 : 3;
            for (std::uint32_t y = 0; y < h; ++y) {
                for (std::uint32_t x = 0; x < w; ++x) {
                    const std::size_t p = static_cast<std::size_t>(y) * w + x;
                    const std::uint8_t value = vtf[input + p * pixelBytes + (format == 0 ? 0 : 2)];
                    const std::uint8_t mask = invert ? static_cast<std::uint8_t>(255u - value) : value;
                    std::uint8_t* dst = expanded.data() + output + p * 4;
                    dst[0] = mask; dst[1] = mask; dst[2] = 0; dst[3] = 255;
                }
            }
        } else {
            std::array<std::uint8_t, 64> rgba{};
            std::array<std::uint8_t, 32> rg{};
            std::array<std::uint8_t, 16> r{};
            for (std::uint32_t by = 0; by < blocksY; ++by) {
                for (std::uint32_t bx = 0; bx < blocksX; ++bx) {
                    const std::uint8_t* block = vtf.data() + input +
                        (static_cast<std::size_t>(by) * blocksX + bx) * sourceBlockBytes;
                    if (format == 35) bcdec_bc4(block, r.data(), 4, 0);
                    else if (format == 34) bcdec_bc5(block, rg.data(), 8, 0);
                    else if (format == 13) bcdec_bc1(block, rgba.data(), 16);
                    else if (format == 14) bcdec_bc2(block, rgba.data(), 16);
                    else bcdec_bc3(block, rgba.data(), 16);
                    for (std::uint32_t py = 0; py < 4 && by * 4 + py < h; ++py) {
                        for (std::uint32_t px = 0; px < 4 && bx * 4 + px < w; ++px) {
                            const std::size_t local = static_cast<std::size_t>(py) * 4 + px;
                            const std::uint8_t value = format == 35 ? r[local] :
                                format == 34 ? rg[local * 2] : rgba[local * 4];
                            const std::uint8_t mask = invert ? static_cast<std::uint8_t>(255u - value) : value;
                            const std::size_t pixel = static_cast<std::size_t>(by * 4 + py) * w + bx * 4 + px;
                            std::uint8_t* dst = expanded.data() + output + pixel * 4;
                            dst[0] = mask; dst[1] = mask; dst[2] = 0; dst[3] = 255;
                        }
                    }
                }
            }
        }
        input += mipSourceBytes;
        output += static_cast<std::size_t>(w) * h * 4;
    }
    vtf = std::move(expanded);
    return true;
}

std::string layerMaskName(std::uint64_t guid, TextureTransform transform) {
    std::ostringstream name;
    name << "r2tex/layer_masks/" << std::hex << std::setw(16) << std::setfill('0') << guid
         << (transform == TextureTransform::maskRg ? "_rg" : "_rg_inv");
    return name.str();
}

// R2 albedo textures (BC7 in particular) carry non-opacity data in the alpha channel. R1 reads the base texture alpha of
// an opaque lightmapped/vertex-lit material as screen-door (dithered) transparency, which flickers every frame: this is the
// "shimmer" (heat-mirage wobble, ghost shadow copies; gone with csm_enabled 0 + mat_global_lighting 1). Materials that
// are not translucent, additive or alpha-tested therefore reference an alias whose alpha is forced to 255.
constexpr const char* kOpaqueAlbedoPrefix = "r2tex/opaque/";

std::string opaqueAlbedoName(const std::string& textureName) {
    return std::string(kOpaqueAlbedoPrefix) + cleanAssetName(textureName, true);
}

bool forceOpaqueVtf(std::vector<std::uint8_t>& vtf, std::string& error) {
    if (vtf.size() < 0x58 || std::memcmp(vtf.data(), "VTF\0", 4) != 0) {
        error = "invalid source VTF for opaque albedo";
        return false;
    }
    const std::uint32_t width = u16(vtf.data() + 0x10);
    const std::uint32_t height = u16(vtf.data() + 0x12);
    const std::uint32_t mipCount = vtf[0x38];
    const std::uint32_t format = u32(vtf.data() + 0x34);
    std::size_t offset = u32(vtf.data() + 0x54);
    if (!width || !height || !mipCount || mipCount > 32 || offset < 0x58 || offset > vtf.size()) {
        error = "invalid source VTF dimensions for opaque albedo";
        return false;
    }
    if (format != 0 && format != 14 && format != 15) return true;  // no alpha channel to clear
    for (std::uint32_t m = mipCount; m-- > 0;) {
        const std::uint32_t w = std::max(1u, width >> m);
        const std::uint32_t h = std::max(1u, height >> m);
        std::size_t mipBytes;
        if (format == 0) mipBytes = static_cast<std::size_t>(w) * h * 4;
        else mipBytes = static_cast<std::size_t>((w + 3) / 4) * ((h + 3) / 4) * 16;
        if (offset > vtf.size() || mipBytes > vtf.size() - offset) {
            error = "truncated source VTF opaque albedo mip";
            return false;
        }
        std::uint8_t* mip = vtf.data() + offset;
        if (format == 0) {
            for (std::size_t p = 3; p < mipBytes; p += 4) mip[p] = 255;
        } else {
            for (std::size_t block = 0; block < mipBytes; block += 16) {
                if (format == 14) {
                    std::memset(mip + block, 0xff, 8);  // DXT3: sixteen 4 bit alpha values
                } else {
                    mip[block] = 255; mip[block + 1] = 255;  // DXT5: alpha0 == alpha1 == 255, all indices 0
                    std::memset(mip + block + 2, 0, 6);
                }
            }
        }
        offset += mipBytes;
    }
    return true;
}

bool surfaceKnown(const std::string& p) {
    static const std::set<std::string> names = {
        "default", "concrete", "metal", "dirt", "grass", "wood", "glass", "plastic", "rock", "sand",
        "tile", "water", "snow", "gravel", "asphalt", "rubber", "cloth", "flesh", "paper", "plaster",
        "brick", "mud", "leaves", "foliage", "ice", "slosh", "computer", "cardboard", "chain", "carpet",
        "ceramic", "stone", "slime", "wade", "ladder", "no_decal", "no_impact", "alienflesh", "antlion",
        "armorflesh", "bloodyflesh", "bugshell", "metal_bouncy", "metal_vehicle", "metalgrate", "metalpanel",
        "metalvent", "metalduct", "metalpipe", "metal_seafloor", "wood_box", "wood_crate", "wood_furniture"
    };
    return names.find(lowerAscii(p)) != names.end();
}

std::string vmtTextureName(std::string name) {
    name = cleanAssetName(name, true);
    for (char& c : name) if (c == '/') c = '\\';
    return name;
}
void addVmtParam(std::ostringstream& out, const char* key, const std::string& value) {
    out << "\t\"" << key << "\" \"" << value << "\"\n";
}

// R2 "world/atmosphere" additive sheets (godray_*, atmosphere_*) multiply a nearly flat cloud-noise texture
// (RGB about 0.66, alpha about 0.16) into per-vertex colour and alpha. R1's UnlitGeneric additive blend reads
// the texture alpha as the beam intensity, so any moment where the texture is not resident (a white,
// alpha 1 placeholder, or half uploaded data) turns the beams into hard, saturated cards or rainbow noise.
// When the texture is flat enough, bake its mean into $color/$alpha and drop the texture dependency.
struct FlatSheet {
    bool ok = false;
    double rgb[3] = {1.0, 1.0, 1.0};
    double alpha = 1.0;
};

FlatSheet flatSheetStats(const std::shared_ptr<Database>& db, const AssetRecord& texture) {
    FlatSheet flat;
    std::vector<std::uint8_t> vtf;
    std::string why;
    if (!convertTextureWithDb(db, texture, TextureOptions{}, vtf, why) || vtf.size() < 0x40) return flat;
    if (std::memcmp(vtf.data(), "VTF\0", 4) != 0) return flat;
    const std::uint32_t headerSize = u32(vtf.data() + 12);
    const std::uint32_t width = u16(vtf.data() + 16);
    const std::uint32_t height = u16(vtf.data() + 18);
    const std::uint32_t frames = u16(vtf.data() + 24);
    const std::uint32_t format = u32(vtf.data() + 0x34);
    // Only the plain RGBA8888 layout is decoded; the top mip is the last image in the file.
    if (format != 0 || frames != 1 || !width || !height || headerSize >= vtf.size()) return flat;
    const std::size_t topBytes = static_cast<std::size_t>(width) * height * 4u;
    if (vtf.size() < topBytes + headerSize) return flat;
    const std::uint8_t* top = vtf.data() + vtf.size() - topBytes;
    double sum[4] = {0, 0, 0, 0};
    int low[4] = {255, 255, 255, 255}, high[4] = {0, 0, 0, 0};
    for (std::size_t i = 0; i < topBytes; i += 4)
        for (int c = 0; c < 4; ++c) {
            sum[c] += top[i + c];
            low[c] = std::min<int>(low[c], top[i + c]);
            high[c] = std::max<int>(high[c], top[i + c]);
        }
    const double pixels = static_cast<double>(topBytes / 4u);
    for (int c = 0; c < 4; ++c) {
        // Flat means every pixel stays within 0.12 of the mean on every channel.
        const double mean = sum[c] / pixels;
        if (mean - low[c] > 30.6 || high[c] - mean > 30.6) return flat;
        if (c < 3) flat.rgb[c] = mean / 255.0; else flat.alpha = mean / 255.0;
    }
    flat.ok = true;
    return flat;
}

bool convertMaterialWithDb(const std::shared_ptr<Database>& db, std::string_view requested,
                           const MaterialOptions& options, Material& material, std::string& error) {
    const std::string name = cleanAssetName(requested, true);
    if (!validAssetName(name)) { error = "invalid material path"; return false; }
    if (name == "tools/toolsskybox" || name.find("tools/sky") == 0) {
        error = "sky compile tool material is not emitted as a VMT; use the map sky setting";
        return false;
    }
    // Map-pack materials that the rpaks do not carry and that R1 cannot draw as authored. The R2 VMT of the
    // arena border dome (mp_coliseum, mp_coliseum_column) is an UnlitTwoTexture whose $texture2 is the
    // skies/skycube cube map. R1's UnlitTwoTexture binds $texture2 as a 2D texture, so the sky layer comes
    // out empty and only the blue scan-line layer remains; a raw skies/skycube.vtf also samples as blown-out
    // white through $envmap. UnlitGeneric with the map's own env_cubemap draws the sky through the
    // scan-lines instead. The scan-line texture is a plain VTF that the map-pack provider already serves.
    if (name == "world/test/kodai_fake_screen_01_world") {
        material = Material{};
        material.name = name;
        material.vmt =
            "\"UnlitGeneric\"\n{\n"
            "\t\"$basetexture\" \"effects\\dev_scanline_small_mip\"\n"
            "\t\"$color\" \"[0.45 0.9 1.5]\"\n"
            "\t\"$envmap\" \"env_cubemap\"\n"
            "\t\"$envmaptint\" \"[0.25 0.3 0.4]\"\n"
            "}\n";
        return true;
    }
    const char* variants[] = {"wld", "fix", "skn", "gen"};
    const AssetRecord* record = nullptr;
    const char* variant = nullptr;
    for (const char* candidate : variants) {
        const auto guid = materialGuid(name, candidate);
        record = findRecord(*db, guid, kMatl);
        if (record) { variant = candidate; break; }
    }
    if (!record) { error = "unresolved material " + name; return false; }
    auto family = db->getFamily(db->paks[record->pakIndex].family, error);
    if (!family) return false;
    std::vector<std::uint8_t> header;
    if (!readAssetHeader(*db, *record, family, header, error)) return false;
    if (header.size() < 0xd0) { error = "short matl header"; return false; }
    const PakData* materialOwner = findPakOwner(*family, record->pakIndex);
    if (!materialOwner) { error = "material pak is unavailable"; return false; }

    std::string shaderSet;
    const std::uint64_t shaderGuid = u64(header.data() + 0x90);
    if (const auto* shader = findRecord(*db, shaderGuid, kShds)) shaderSet = lowerAscii(shader->name);
    std::string surface;
    PagePtr surfacePtr{};
    std::memcpy(&surfacePtr, header.data() + 0x20, sizeof(surfacePtr));
    readCString(*family, *materialOwner, surfacePtr, surface);
    surface = lowerAscii(surface);

    PagePtr textureBegin{}, textureEnd{};
    std::memcpy(&textureBegin, header.data() + 0x98, sizeof(textureBegin));
    std::memcpy(&textureEnd, header.data() + 0xa0, sizeof(textureEnd));
    std::vector<std::uint64_t> slotGuids;
    if (textureBegin.index == textureEnd.index && textureEnd.offset >= textureBegin.offset &&
        ((textureEnd.offset - textureBegin.offset) % 8u) == 0) {
        const std::uint32_t slots = (textureEnd.offset - textureBegin.offset) / 8u;
        if (slots <= 64) {
            std::vector<std::uint8_t> table;
            if (readPageRange(*family, *materialOwner, textureBegin, static_cast<std::size_t>(slots) * 8, table, error)) {
                slotGuids.resize(slots);
                if (!table.empty()) std::memcpy(slotGuids.data(), table.data(), table.size());
            }
        }
    }
    auto texName = [&](std::size_t slot) -> std::string {
        if (slot >= slotGuids.size() || !slotGuids[slot]) return {};
        const AssetRecord* texture = findRecord(*db, slotGuids[slot], kTxtr);
        if (!texture) return {};
        std::string name = cleanAssetName(texture->name, true);
        return validAssetName(name) ? name : std::string();
    };
    std::map<std::size_t, std::string> usedSlots;
    auto use = [&](std::size_t slot) {
        const std::string tex = texName(slot);
        if (tex.empty()) return false;
        usedSlots.emplace(slot, tex);
        return true;
    };
    const bool tools = name.rfind("tools/", 0) == 0;
    const bool unlit = std::strcmp(variant, "gen") == 0 || shaderSet.find("unlit") != std::string::npos;
    const std::string shader = unlit ? "UnlitGeneric" :
        (std::strcmp(variant, "wld") == 0 ? "LightmappedGeneric" : "VertexLitGeneric");
    const bool lightmapped = shader == "LightmappedGeneric";
    const std::array<std::size_t, 4> layerSlots = {22, 23, 24, 26};
    const bool hasLayerSlots = lightmapped && std::any_of(layerSlots.begin(), layerSlots.end(),
        [&](std::size_t slot) { return slot < slotGuids.size() && slotGuids[slot] != 0; });
    const bool layersOff = hasLayerSlots && options.layerMode == LayerMode::layersOff;
    // `current` (the default) expands the single-channel R2 layer mask (slot 22, BC4 red only) to R=G=mask: R1's
    // $blendModulateTexture reads red AND green. `redOnly` keeps the old unexpanded mapping.
    const LayerMode effMode = options.layerMode == LayerMode::current ? LayerMode::maskRg : options.layerMode;
    material.usesLayers = hasLayerSlots;
    if (!tools) {
        use(0);
        // layers-off removes only the second layer (slots 22/23/24/26). The normal, specular, emissive and detail
        // slots must stay: a lightmapped material without $bumpmap runs a shader permutation that does not match
        // bumped lightmap data and shimmers in the sun/light shadow paths.
        use(1); use(3); use(4); use(14); use(15);
        if (lightmapped && !layersOff) {
            use(22); use(23); use(24); use(26);
        }
        if (usedSlots.find(0) == usedSlots.end()) { error = "material has no resolvable base texture: " + name; return false; }
    }

    // dxState[1] is the material draw state. R2 stores D3D blend factors minus one.
    const std::uint32_t blend = u32(header.data() + 0x70);
    const std::uint16_t depth = u16(header.data() + 0x70 + 0x14);
    const std::uint16_t raster = u16(header.data() + 0x70 + 0x16);
    const bool cut = shaderSet.find("cut") != std::string::npos;
    const bool blendEnabled = (blend & 0x2u) != 0;
    const std::uint32_t sourceBlend = ((blend >> 2u) & 0x1fu) + 1u;
    const std::uint32_t destinationBlend = ((blend >> 7u) & 0x1fu) + 1u;
    constexpr std::uint32_t kBlendOne = 2;
    constexpr std::uint32_t kBlendInvSrcAlpha = 6;
    const bool additive = blendEnabled && sourceBlend == kBlendOne && destinationBlend == kBlendOne;
    const bool premultipliedAlpha = blendEnabled &&
        sourceBlend == kBlendOne && destinationBlend == kBlendInvSrcAlpha;
    const bool translucent = !additive &&
        (shaderSet.find("trans") != std::string::npos || blendEnabled);
    const bool rasterDecalPreset = (raster & 0x70u) == 0x10u;
    const bool overlayDepthState = (depth & 0x17u) == 0x07u;
    const bool overlayProfile = premultipliedAlpha &&
        shaderSet.find("trans") != std::string::npos &&
        overlayDepthState && rasterDecalPreset;
    // Map this observed R2 surface-overlay profile to R1's $decal parameter.
    const bool decal = name.find("/decal/") != std::string::npos ||
        shaderSet.find("decal") != std::string::npos || overlayProfile;
    const bool glass = name.find("glass") != std::string::npos || surface == "glass";

    // Additive atmosphere sheets with a flat texture: bake the texture mean into constants (see flatSheetStats).
    FlatSheet flatSheet;
    if (!tools && unlit && additive && name.rfind("world/atmosphere/", 0) == 0 &&
        shaderSet.find("vcola") != std::string::npos) {
        const auto slot0 = usedSlots.find(0);
        if (slot0 != usedSlots.end() && slotGuids[0])
            if (const AssetRecord* texture = findRecord(*db, slotGuids[0], kTxtr))
                flatSheet = flatSheetStats(db, *texture);
    }
    const bool vertexLit = shader == "VertexLitGeneric";
    std::ostringstream out;
    std::set<std::size_t> referencedSlots;
    out << '\"' << shader << "\"\n{\n";
    const std::string safeSurface = surfaceKnown(surface) ? surface : "default";
    addVmtParam(out, "$surfaceprop", safeSurface);
    const bool opaqueAlbedo = !tools && !additive && !translucent && !glass && !cut &&
        shaderSet.find("vcola") == std::string::npos;
    if (tools) {
        addVmtParam(out, "%compilenodraw", "1");
    } else {
        // Opaque materials must not see R2's non-opacity albedo alpha (see forceOpaqueVtf).
        if (opaqueAlbedo) {
            for (const std::size_t albedoSlot : {std::size_t(0), std::size_t(23)}) {
                auto albedo = usedSlots.find(albedoSlot);
                if (albedo != usedSlots.end()) albedo->second = opaqueAlbedoName(albedo->second);
            }
        }
        auto param = [&](std::size_t slot, const char* key) {
            auto it = usedSlots.find(slot);
            if (it != usedSlots.end()) {
                addVmtParam(out, key, vmtTextureName(it->second));
                referencedSlots.insert(slot);
            }
        };
        if (!flatSheet.ok) param(0, "$basetexture");
        if (!unlit) param(1, "$bumpmap");
        if (!unlit && usedSlots.find(3) != usedSlots.end()) {
            param(3, "$reflectiontint");
            param(3, "$reflectiontintshininess");
            addVmtParam(out, "$reflectiontintcolor", "1");
            addVmtParam(out, "$envmaptint", "[1 1 1]");
        }
        if (vertexLit && usedSlots.find(4) != usedSlots.end()) {
            addVmtParam(out, "$selfillum", "1");
            param(4, "$selfIllumMask");
        }
        param(14, "$detailColor");
        if (!unlit) param(15, "$detailNormal");
        if (referencedSlots.find(14) != referencedSlots.end() ||
            referencedSlots.find(15) != referencedSlots.end())
            addVmtParam(out, "$detailScale", "[1 1]");
        if (lightmapped) {
            if (effMode == LayerMode::maskRg || effMode == LayerMode::maskRgInv) {
                auto mask = usedSlots.find(22);
                if (mask != usedSlots.end())
                    mask->second = layerMaskName(slotGuids[22],
                        effMode == LayerMode::maskRg ? TextureTransform::maskRg : TextureTransform::maskRgInv);
            }
            param(22, "$blendModulateTexture");
            param(23, "$baseTexture2");
            param(24, "$bumpmap2");
            param(26, "$reflectionTint2");
        }
        if (flatSheet.ok) {
            char text[96];
            std::snprintf(text, sizeof(text), "[%.3f %.3f %.3f]", flatSheet.rgb[0], flatSheet.rgb[1], flatSheet.rgb[2]);
            addVmtParam(out, "$color", text);
            std::snprintf(text, sizeof(text), "%.3f", flatSheet.alpha);
            addVmtParam(out, "$alpha", text);
        }
        if (cut) { addVmtParam(out, "$alphatest", "1"); addVmtParam(out, "$alphaTestReference", "0.7"); }
        if (additive) addVmtParam(out, "$additive", "1");
        else if (translucent || glass) addVmtParam(out, "$translucent", "1");
        if (decal) addVmtParam(out, "$decal", "1");
        if (glass || (!unlit && usedSlots.find(3) != usedSlots.end())) addVmtParam(out, "$envmap", "env_cubemap");
        // A layered R2 material uses vertex alpha as the layer blend weight. Once its layers are removed (layers-off),
        // R1 would multiply that graded alpha into the opaque surface and dither it: the mp_glitch ground shimmered
        // (metric 2.5..3.9) with "$vertexcolor 1" and not (0.0) without it.
        // World (BSP) materials that are opaque and have no second layer: their vertex colour is white (rgb 255 on all of
        // mp_eden's) while the vertex alpha can be graded (33 such materials, 343k vertices over 21 maps), and R1 reads that
        // alpha as dithered opacity. Drop $vertexcolor there; `models/` materials keep it (baked tint, rgb about 110).
        const bool whiteVertexWorld = lightmapped && !hasLayerSlots && opaqueAlbedo && name.rfind("world/", 0) == 0;
        if (shaderSet.find("vcolt") != std::string::npos && !layersOff && !whiteVertexWorld)
            addVmtParam(out, "$vertexcolor", "1");
        if (shaderSet.find("vcola") != std::string::npos) addVmtParam(out, "$vertexalpha", "1");
        if ((raster & 0x06u) == 0x02u) addVmtParam(out, "$nocull", "1");
    }
    out << "}\n";

    material.name = name;
    material.vmt = out.str();
    material.textures.clear();
    for (const auto& pair : usedSlots) {
        const std::size_t slot = pair.first;
        if (slot >= slotGuids.size() || referencedSlots.find(slot) == referencedSlots.end()) continue;
        TextureRef texture;
        texture.guid = slotGuids[slot];
        texture.name = pair.second;
        texture.slot = static_cast<std::uint32_t>(slot);
        if (slot == 22 && (effMode == LayerMode::maskRg || effMode == LayerMode::maskRgInv)) {
            texture.transform = effMode == LayerMode::maskRg ?
                TextureTransform::maskRg : TextureTransform::maskRgInv;
        }
        if (opaqueAlbedo && (slot == 0 || slot == 23)) texture.transform = TextureTransform::opaqueAlpha;
        material.textures.push_back(std::move(texture));
    }
    return true;
}

bool parseBspNames(const std::uint8_t* data, std::size_t byteCount,
                   std::vector<std::string>& names, std::string& error) {
    if (!data && byteCount) { error = "null BSP buffer"; return false; }
    constexpr std::size_t headerBytes = 16 + 128 * 16;
    if (byteCount < headerBytes || !data || std::memcmp(data, "rBSP", 4) != 0) {
        error = "not a Titanfall Respawn BSP";
        return false;
    }
    const std::uint32_t version = u32(data + 4);
    const std::uint32_t lumpCount = u32(data + 12);
    if (version < 29 || version > 0x10000 || lumpCount != 127) {
        error = "unsupported Respawn BSP header";
        return false;
    }
    struct Lump { std::uint32_t offset, length; } lumps[128]{};
    for (std::uint32_t i = 0; i < 128; ++i) {
        const std::uint8_t* p = data + 16 + i * 16;
        lumps[i] = {u32(p), u32(p + 4)};
        if (lumps[i].offset > byteCount || lumps[i].length > byteCount - lumps[i].offset) {
            error = "BSP lump outside file";
            return false;
        }
    }
    const Lump td = lumps[kBspTextureData];
    const Lump stringData = lumps[kBspTextureNameData];
    const Lump stringTable = lumps[kBspTextureNameTable];
    if (td.length % 36 || stringTable.length % 4) {
        error = "invalid BSP texture lump sizes";
        return false;
    }
    const std::uint8_t* stringBytes = data + stringData.offset;
    const std::uint32_t tableCount = stringTable.length / 4;
    std::vector<std::uint32_t> offsets(tableCount);
    for (std::uint32_t i = 0; i < tableCount; ++i)
        offsets[i] = u32(data + stringTable.offset + i * 4);
    std::set<std::string> unique;
    const std::uint32_t recordCount = td.length / 36;
    for (std::uint32_t i = 0; i < recordCount; ++i) {
        const std::int32_t index = static_cast<std::int32_t>(u32(data + td.offset + i * 36 + 12));
        if (index < 0 || static_cast<std::uint32_t>(index) >= tableCount) continue;
        const std::uint32_t offset = offsets[static_cast<std::uint32_t>(index)];
        if (offset >= stringData.length) continue;
        const char* start = reinterpret_cast<const char*>(stringBytes + offset);
        const std::size_t avail = stringData.length - offset;
        const void* end = std::memchr(start, 0, avail);
        if (!end) continue;
        const std::size_t nameSize = static_cast<const char*>(end) - start;
        if (!nameSize || nameSize > 4096) continue;
        std::string name(start, nameSize);
        name = cleanAssetName(name, true);
        if (validAssetName(name)) unique.insert(std::move(name));
    }
    names.assign(unique.begin(), unique.end());
    if (names.empty()) { error = "BSP contains no material names"; return false; }
    error.clear();
    return true;
}

bool parseBspNames(const fs::path& path, std::vector<std::string>& names, std::string& error) {
    std::vector<std::uint8_t> bytes;
    if (!readWholeFile(path, bytes, error)) return false;
    return parseBspNames(bytes.data(), bytes.size(), names, error);
}

} // namespace

void SetFamilyCacheLimitMiB(std::uint32_t megabytes) noexcept {
    const auto maximumMiB = static_cast<std::uint32_t>(kMaximumFamilyCacheLimit / (1024ull * 1024ull));
    const auto boundedMiB = std::min(megabytes, maximumMiB);
    gFamilyCacheLimit.store(static_cast<std::size_t>(boundedMiB) * 1024ull * 1024ull,
                            std::memory_order_relaxed);
}
bool Open(const std::filesystem::path& installDir, const std::filesystem::path& cacheDir, std::string* error) {
    return Open(installDir, cacheDir, error, nullptr);
}

bool Open(const std::filesystem::path& installDir, const std::filesystem::path& cacheDir,
          std::string* error, bool* cacheHit) {
    if (cacheHit) *cacheHit = false;
    std::lock_guard<std::mutex> openLock(g_openMutex);
    try {
        auto db = std::make_shared<Database>();
        db->installDir = fs::absolute(installDir).lexically_normal();
        db->installIdentity = lowerAscii(db->installDir.u8string());
        db->cacheFile = cacheDir / L"r2tex.index";
        std::string why;
        if (!enumeratePaks(*db, why)) { if (error) *error = why; return false; }
        bool hit = false;
        if (!loadIndex(*db, why, hit)) { if (error) *error = why; return false; }
        g_database = std::move(db);
        if (cacheHit) *cacheHit = hit;
        if (error) error->clear();
        return true;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

bool ConvertTexture(std::uint64_t guid, std::vector<std::uint8_t>& vtf, std::string* error) {
    return ConvertTexture(guid, vtf, TextureOptions{}, error);
}

bool ConvertTexture(std::uint64_t guid, std::vector<std::uint8_t>& vtf,
                    const TextureOptions& options, std::string* error) {
    vtf.clear();
    try {
        auto db = getDatabase();
        if (!db) { if (error) *error = "r2tex::Open has not succeeded"; return false; }
        const AssetRecord* record = findRecord(*db, guid, kTxtr);
        if (!record) { if (error) *error = "unresolved texture GUID"; return false; }
        std::string why;
        const bool result = convertTextureWithDb(db, *record, options, vtf, why);
        if (!result && error) *error = why;
        else if (error) error->clear();
        return result;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        vtf.clear();
        return false;
    }
}

bool ConvertTexture(std::string_view name, std::vector<std::uint8_t>& vtf, std::string* error) {
    return ConvertTexture(name, vtf, TextureOptions{}, error);
}

bool ConvertTexture(std::string_view name, std::vector<std::uint8_t>& vtf,
                    const TextureOptions& options, std::string* error) {
    const std::string cleaned = cleanAssetName(name, true);
    if (!validAssetName(cleaned)) { vtf.clear(); if (error) *error = "invalid texture path"; return false; }
    // Synthetic names emitted by ConvertMaterial: opaque albedo aliases and expanded layer masks.
    if (cleaned.rfind(kOpaqueAlbedoPrefix, 0) == 0) {
        const std::string original = cleaned.substr(std::strlen(kOpaqueAlbedoPrefix));
        if (!validAssetName(original)) { vtf.clear(); if (error) *error = "invalid texture path"; return false; }
        TextureRef ref;
        ref.guid = textureGuid(original); ref.name = original; ref.transform = TextureTransform::opaqueAlpha;
        return ConvertTexture(ref, vtf, options, error);
    }
    constexpr const char* kLayerMaskPrefix = "r2tex/layer_masks/";
    if (cleaned.rfind(kLayerMaskPrefix, 0) == 0) {
        const std::string rest = cleaned.substr(std::strlen(kLayerMaskPrefix));
        const bool inverse = rest.size() == 16 + 7 && rest.compare(16, 7, "_rg_inv") == 0;
        if ((rest.size() == 16 + 3 && rest.compare(16, 3, "_rg") == 0) || inverse) {
            char* end = nullptr;
            const std::string hex = rest.substr(0, 16);
            const std::uint64_t guid = std::strtoull(hex.c_str(), &end, 16);
            if (end == hex.c_str() + 16) {
                TextureRef ref;
                ref.guid = guid; ref.name = cleaned;
                ref.transform = inverse ? TextureTransform::maskRgInv : TextureTransform::maskRg;
                return ConvertTexture(ref, vtf, options, error);
            }
        }
        vtf.clear(); if (error) *error = "invalid layer mask texture path"; return false;
    }
    const std::uint64_t guid = textureGuid(cleaned);
    if (!ConvertTexture(guid, vtf, options, error)) {
        if (error && !error->empty()) *error = "texture " + cleaned + ": " + *error;
        return false;
    }
    return true;
}

bool ConvertTexture(const TextureRef& texture, std::vector<std::uint8_t>& vtf, std::string* error) {
    return ConvertTexture(texture, vtf, TextureOptions{}, error);
}

bool ConvertTexture(const TextureRef& texture, std::vector<std::uint8_t>& vtf,
                    const TextureOptions& options, std::string* error) {
    if (texture.transform == TextureTransform::none)
        return ConvertTexture(texture.guid, vtf, options, error);
    vtf.clear();
    try {
        auto db = getDatabase();
        if (!db) { if (error) *error = "r2tex::Open has not succeeded"; return false; }
        const AssetRecord* record = findRecord(*db, texture.guid, kTxtr);
        if (!record) { if (error) *error = "unresolved texture GUID"; return false; }
        TextureOptions decodeOptions = options;
        decodeOptions.bc7Passthrough = false;
        decodeOptions.bc6hPassthrough = false;
        std::string why;
        if (!convertTextureWithDb(db, *record, decodeOptions, vtf, why)) {
            if (error) *error = why;
            return false;
        }
        const bool invert = texture.transform == TextureTransform::maskRgInv;
        if (texture.transform == TextureTransform::opaqueAlpha) {
            if (!forceOpaqueVtf(vtf, why)) {
                vtf.clear();
                if (error) *error = why;
                return false;
            }
            if (error) error->clear();
            return true;
        }
        if (texture.transform != TextureTransform::maskRg && !invert) {
            vtf.clear();
            if (error) *error = "unsupported texture transform";
            return false;
        }
        if (!expandLayerMaskVtf(vtf, invert, why)) {
            vtf.clear();
            if (error) *error = why;
            return false;
        }
        if (error) error->clear();
        return true;
    } catch (const std::exception& e) {
        vtf.clear();
        if (error) *error = e.what();
        return false;
    }
}

bool ConvertMaterial(std::string_view name, Material& material, std::string* error) {
    return ConvertMaterial(name, material, MaterialOptions{}, error);
}

bool ConvertMaterial(std::string_view name, Material& material,
                     const MaterialOptions& options, std::string* error) {
    material = Material{};
    try {
        auto db = getDatabase();
        if (!db) { if (error) *error = "r2tex::Open has not succeeded"; return false; }
        std::string why;
        const bool result = convertMaterialWithDb(db, name, options, material, why);
        if (!result) {
            if (error) *error = why;
            material = Material{};
            return false;
        }
        if (error) error->clear();
        return true;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        material = Material{};
        return false;
    }
}

bool ListMapMaterials(const std::filesystem::path& bspPath, std::vector<std::string>& names, std::string* error) {
    names.clear();
    std::string why;
    try {
        if (!parseBspNames(bspPath, names, why)) { if (error) *error = why; return false; }
        if (error) error->clear();
        return true;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        names.clear();
        return false;
    }
}

bool ListMapMaterials(const std::uint8_t* bspBytes, std::size_t size,
                      std::vector<std::string>& names, std::string* error) {
    names.clear();
    std::string why;
    try {
        if (!parseBspNames(bspBytes, size, names, why)) { if (error) *error = why; return false; }
        if (error) error->clear();
        return true;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        names.clear();
        return false;
    }
}

} // namespace r2tex
