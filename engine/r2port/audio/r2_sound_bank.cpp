#include "r2_sound_bank.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace r2audio {
namespace {

constexpr uint32_t kBankVersion = 13;
constexpr uint32_t kSourceStride = 0x58;
constexpr int kMaxEventDepth = 4;
constexpr size_t kMaxLeaves = 512;

template <class T>
T Read(const uint8_t* p) {
    T v;
    std::memcpy(&v, p, sizeof(T));
    return v;
}

char Lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }

int CompareLower(std::string_view a, std::string_view b) {
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        const unsigned char x = static_cast<unsigned char>(Lower(a[i])), y = static_cast<unsigned char>(Lower(b[i]));
        if (x != y) return x < y ? -1 : 1;
    }
    return a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1);
}

}  // namespace

struct SoundBank::Impl {
    std::filesystem::path dir;
    HANDLE file = INVALID_HANDLE_VALUE;
    HANDLE mapping = nullptr;
    const uint8_t* base = nullptr;
    uint64_t size = 0;
    uint32_t sources = 0, eventNames = 0, eventData = 0, strings = 0, graphData = 0, sourceSelectors = 0;
    uint32_t sourceCount = 0, eventCount = 0, graphDataSize = 0, buildTag = 0;

    mutable std::once_flag indexOnce;
    mutable std::unordered_map<std::string, uint32_t> sourceIndex;
    mutable std::mutex streamMutex;
    mutable std::unordered_map<int, uint32_t> streamData;

    ~Impl() {
        if (base) UnmapViewOfFile(base);
        if (mapping) CloseHandle(mapping);
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    }

    bool Has(uint64_t offset, uint64_t length) const { return offset <= size && length <= size - offset; }

    std::string_view StringAt(uint32_t offset) const {
        const uint64_t at = static_cast<uint64_t>(strings) + offset;
        if (at >= size) return {};
        const uint8_t* start = base + at;
        const void* end = std::memchr(start, 0, static_cast<size_t>(std::min<uint64_t>(size - at, 512)));
        if (!end) return {};
        return std::string_view(reinterpret_cast<const char*>(start), static_cast<size_t>(static_cast<const uint8_t*>(end) - start));
    }

    // `at` is the absolute file offset of the selector; list child offsets are relative to the ROOT selector of the tree.
    bool ParseSelector(uint64_t root, uint64_t at, int depth, std::vector<std::string>& waves) const {
        if (depth > 6 || waves.size() > kMaxLeaves) return false;
        if (!Has(at, 0x28)) return false;
        const uint8_t* p = base + at;
        if (p[0] == 1) {
            const int32_t nameOffset = Read<int32_t>(p + 0x20);
            if (nameOffset < 0) return true;  // empty leaf
            const std::string_view name = StringAt(static_cast<uint32_t>(nameOffset));
            if (name.empty()) return false;
            waves.emplace_back(name);
            return true;
        }
        if (p[0] != 0) return false;
        const unsigned count = p[3];
        if (!Has(at, 8 + 4ull * count)) return false;
        for (unsigned i = 0; i < count; ++i)
            if (!ParseSelector(root, root + Read<uint32_t>(p + 8 + 4 * i), depth + 1, waves)) return false;
        return true;
    }

    static float Finite(float v, float fallback) { return std::isfinite(v) ? v : fallback; }

    float GraphRadius(int32_t offset) const {
        if (offset < 0 || !Has(static_cast<uint64_t>(graphData) + offset, 8)) return 0.0f;
        if (static_cast<uint32_t>(offset) >= graphDataSize) return 0.0f;
        const uint8_t* p = base + graphData + offset;
        const unsigned points = p[0];
        if (points < 2 || points > 64) return 0.0f;
        if (!Has(static_cast<uint64_t>(graphData) + offset, 8 + points * 4ull + points * 20ull)) return 0.0f;
        const uint8_t* xs = p + 8;
        const uint8_t* segments = xs + points * 4;
        for (unsigned i = 0; i + 1 < points; ++i) {
            const float end = Read<float>(segments + i * 20 + 4);
            if (std::isfinite(end) && end <= -40.0f) return Finite(Read<float>(xs + (i + 1) * 4), 0.0f);
        }
        return 0.0f;
    }

    bool FindEventOffset(std::string_view name, uint32_t& dataOffset) const {
        if (!Has(eventNames, 8ull * eventCount)) return false;
        const uint8_t* table = base + eventNames;
        size_t lo = 0, hi = eventCount;
        while (lo < hi) {
            const size_t mid = (lo + hi) / 2;
            const int cmp = CompareLower(StringAt(Read<uint32_t>(table + mid * 8)), name);
            if (cmp == 0) {
                dataOffset = Read<uint32_t>(table + mid * 8 + 4);
                return true;
            }
            if (cmp < 0)
                lo = mid + 1;
            else
                hi = mid;
        }
        for (size_t i = 0; i < eventCount; ++i)  // not sorted the way we assumed: fall back to a scan
            if (CompareLower(StringAt(Read<uint32_t>(table + i * 8)), name) == 0) {
                dataOffset = Read<uint32_t>(table + i * 8 + 4);
                return true;
            }
        return false;
    }

    bool Resolve(std::string_view name, int depth, std::vector<PlayAction>& plays, std::unordered_set<std::string>& seen) const {
        if (depth > kMaxEventDepth) return false;
        std::string key(name);
        for (char& c : key) c = Lower(c);
        if (!seen.insert(key).second) return true;
        uint32_t dataOffset;
        if (!FindEventOffset(name, dataOffset)) return false;
        uint64_t at = static_cast<uint64_t>(eventData) + dataOffset;
        bool any = false;
        for (int guard = 0; guard < 32; ++guard) {
            if (!Has(at, 4)) return any;
            const uint8_t* p = base + at;
            const unsigned type = p[0] & 0xF, last = p[0] >> 4, dwords = p[1];
            if (dwords == 0 || !Has(at, dwords * 4ull)) return any;
            if (type == 0) {
                if (dwords * 4u < 0x60) return any;
                PlayAction play;
                const int32_t selector = Read<int32_t>(p + 0x3C);
                if (selector < 0 || !ParseSelector(static_cast<uint64_t>(sourceSelectors) + static_cast<uint32_t>(selector), static_cast<uint64_t>(sourceSelectors) + static_cast<uint32_t>(selector), 0, play.waves)) return any;
                play.volumeDb = Finite(Read<float>(p + 0x5C), 0.0f);
                play.addVolumeDb = Finite(Read<float>(p + 0x50), 0.0f);
                play.pitchSt = Finite(Read<float>(p + 0x58), 0.0f);
                play.pitchRandomSt = std::fabs(Finite(Read<float>(p + 0x0C), 0.0f));
                play.loopCount = Read<uint16_t>(p + 0x38);
                play.radius = GraphRadius(Read<int32_t>(p + 0x28));
                if (!play.waves.empty()) {
                    plays.push_back(std::move(play));
                    any = true;
                }
            } else if (type == 8 && dwords * 4u >= 0x0E) {
                const std::string_view target = StringAt(Read<uint32_t>(p + 0x0A));
                if (!target.empty() && Resolve(target, depth + 1, plays, seen)) any = true;
            }
            if (last) break;
            at += dwords * 4ull;
        }
        return any;
    }
};

SoundBank::SoundBank() = default;
SoundBank::~SoundBank() = default;

bool SoundBank::IsOpen() const { return impl_ && impl_->base; }

bool SoundBank::Open(const std::filesystem::path& soundDirectory, std::string* error) {
    auto fail = [&](const char* message) {
        if (error) *error = message;
        impl_.reset();
        return false;
    };
    auto impl = std::make_unique<Impl>();
    impl->dir = soundDirectory;
    const std::filesystem::path bank = soundDirectory / "general.mbnk";
    impl->file = CreateFileW(bank.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (impl->file == INVALID_HANDLE_VALUE) {
        impl_ = std::move(impl);
        return fail("general.mbnk could not be opened");
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(impl->file, &size) || size.QuadPart < 0x100) {
        impl_ = std::move(impl);
        return fail("general.mbnk is too small");
    }
    impl->size = static_cast<uint64_t>(size.QuadPart);
    impl->mapping = CreateFileMappingW(impl->file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!impl->mapping) {
        impl_ = std::move(impl);
        return fail("general.mbnk could not be mapped");
    }
    impl->base = static_cast<const uint8_t*>(MapViewOfFile(impl->mapping, FILE_MAP_READ, 0, 0, 0));
    if (!impl->base) {
        impl_ = std::move(impl);
        return fail("general.mbnk view failed");
    }
    const uint8_t* b = impl->base;
    if (std::memcmp(b, "KNBC", 4) != 0 || Read<uint32_t>(b + 4) != kBankVersion) {
        impl_ = std::move(impl);
        return fail("not a version 13 Miles bank");
    }
    if (Read<uint32_t>(b + 8) != impl->size) {
        impl_ = std::move(impl);
        return fail("bank size field does not match the file");
    }
    impl->sources = Read<uint32_t>(b + 0x48);
    impl->eventNames = Read<uint32_t>(b + 0x60);
    impl->eventData = Read<uint32_t>(b + 0x68);
    impl->strings = Read<uint32_t>(b + 0x70);
    impl->graphData = Read<uint32_t>(b + 0x78);
    impl->sourceSelectors = Read<uint32_t>(b + 0x80);
    impl->sourceCount = Read<uint32_t>(b + 0xA0);
    impl->eventCount = Read<uint32_t>(b + 0xA8);
    impl->graphDataSize = Read<uint32_t>(b + 0xB4);
    impl->buildTag = Read<uint32_t>(b + 0xBC);
    if (!impl->Has(impl->sources, static_cast<uint64_t>(impl->sourceCount) * kSourceStride) ||
        !impl->Has(impl->eventNames, 8ull * impl->eventCount) || !impl->Has(impl->eventData, 4) || !impl->Has(impl->strings, 1) ||
        !impl->Has(impl->graphData, impl->graphDataSize) || !impl->Has(impl->sourceSelectors, 4) || impl->sourceCount > 1000000 ||
        impl->eventCount > 1000000) {
        impl_ = std::move(impl);
        return fail("bank tables are out of range");
    }
    impl_ = std::move(impl);
    return true;
}

bool SoundBank::ResolveEvent(std::string_view name, std::vector<PlayAction>& plays) const {
    plays.clear();
    if (!IsOpen()) return false;
    std::unordered_set<std::string> seen;
    return impl_->Resolve(name, 0, plays, seen) && !plays.empty();
}

bool SoundBank::FindSource(std::string_view lowerName, SourceRecord& record) const {
    if (!IsOpen()) return false;
    std::call_once(impl_->indexOnce, [this]() {
        impl_->sourceIndex.reserve(impl_->sourceCount * 2);
        for (uint32_t i = 0; i < impl_->sourceCount; ++i) {
            const uint8_t* p = impl_->base + impl_->sources + static_cast<uint64_t>(i) * kSourceStride;
            std::string name(impl_->StringAt(Read<uint32_t>(p + 0x10)));
            for (char& c : name) c = Lower(c);
            impl_->sourceIndex.emplace(std::move(name), i);
        }
    });
    const auto found = impl_->sourceIndex.find(std::string(lowerName));
    if (found == impl_->sourceIndex.end()) return false;
    const uint8_t* p = impl_->base + impl_->sources + static_cast<uint64_t>(found->second) * kSourceStride;
    record.name = std::string(impl_->StringAt(Read<uint32_t>(p + 0x10)));
    record.sampleRate = Read<uint16_t>(p + 0x14);
    record.channels = p[0x18];
    record.headerSize = Read<uint32_t>(p + 0x30);
    record.sampleCount = Read<uint32_t>(p + 0x34);
    record.headerOffset = Read<uint64_t>(p + 0x38);
    record.dataOffset = Read<uint64_t>(p + 0x40);
    record.language = Read<int16_t>(p + 0x50);
    record.patch = Read<int16_t>(p + 0x52);
    return true;
}

std::filesystem::path SoundBank::StreamPath(const SourceRecord& record) const {
    if (!impl_ || record.language != -1 || record.patch < 0 || record.patch > 31) return {};
    return impl_->dir / (record.patch == 0 ? std::string("general_stream.mstr") : "general_stream_patch_" + std::to_string(record.patch) + ".mstr");
}

uint32_t SoundBank::StreamDataOffset(int patch) const {
    if (!impl_ || patch < 0 || patch > 31) return 0;
    std::lock_guard<std::mutex> lock(impl_->streamMutex);
    const auto cached = impl_->streamData.find(patch);
    if (cached != impl_->streamData.end()) return cached->second;
    uint32_t result = 0;
    SourceRecord probe;
    probe.patch = static_cast<int16_t>(patch);
    const std::filesystem::path path = StreamPath(probe);
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        uint8_t header[20];
        DWORD got = 0;
        if (ReadFile(f, header, sizeof(header), &got, nullptr) && got == sizeof(header) && std::memcmp(header, "RTSC", 4) == 0 &&
            Read<uint16_t>(header + 4) == 2 && Read<int32_t>(header + 12) == patch && Read<uint32_t>(header + 16) == impl_->buildTag)
            result = Read<uint32_t>(header + 8);
        CloseHandle(f);
    }
    impl_->streamData.emplace(patch, result);
    return result;
}

}  // namespace r2audio
