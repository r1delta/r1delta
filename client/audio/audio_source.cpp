#include "audio_source.h"
#include "logging.h"
#include "engine/r2port/audio/r2_audio_source.h"
#include <vorbis/vorbisfile.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <climits>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace {
constexpr std::uint32_t kMixerRate = 44100;
constexpr std::size_t kPrefetchFrames = 1024;
constexpr std::size_t kOutputBlock = 4096;
constexpr int kFilterRadius = 24;
void* g_fileSystem = nullptr;

// Windowed-sinc kernel sampled at kKernelPhases + 1 fractional positions (linear interpolation between rows), the same filter
// as the exact path below. Generated R2 waves are 43-48 kHz and stream continuously, so evaluating sin/cos per tap and frame
// (about 6 ms of CPU per second of 6 channel audio) is replaced by table rows.
constexpr int kKernelPhases = 512;
constexpr int kKernelTaps = 2 * kFilterRadius + 1;
struct KernelTable {
    std::vector<float> weights;  // (kKernelPhases + 1) rows of kKernelTaps
};
const KernelTable& GetKernel(std::uint32_t rate) {
    static std::mutex lock;
    static std::unordered_map<std::uint32_t, std::unique_ptr<KernelTable>> tables;
    // Rates below the mixer rate share one kernel (cutoff 1); above it the cutoff depends on the rate.
    const std::uint32_t key = rate <= kMixerRate ? kMixerRate : rate;
    std::lock_guard<std::mutex> guard(lock);
    auto& entry = tables[key];
    if (!entry) {
        entry = std::make_unique<KernelTable>();
        entry->weights.resize(static_cast<std::size_t>(kKernelPhases + 1) * kKernelTaps);
        const double cutoff = (std::min)(1.0, double(kMixerRate) / key);
        constexpr double pi = 3.14159265358979323846;
        for (int phase = 0; phase <= kKernelPhases; ++phase) {
            const double fraction = double(phase) / kKernelPhases;
            for (int tap = -kFilterRadius; tap <= kFilterRadius; ++tap) {
                const double distance = tap - fraction;
                double weight = 0;
                if (std::abs(distance) < kFilterRadius) {
                    const double angle = pi * distance * cutoff;
                    const double sinc = std::abs(angle) < 1e-12 ? 1 : std::sin(angle) / angle;
                    const double window = 0.42 + 0.5 * std::cos(pi * distance / kFilterRadius) + 0.08 * std::cos(2 * pi * distance / kFilterRadius);
                    weight = cutoff * sinc * window;
                }
                entry->weights[static_cast<std::size_t>(phase) * kKernelTaps + (tap + kFilterRadius)] = static_cast<float>(weight);
            }
        }
    }
    return *entry;
}

struct AudioError : std::runtime_error { using std::runtime_error::runtime_error; };
[[noreturn]] void Fail(const char* message) { throw AudioError(message); }

template<class Function> Function InnerFunction(std::size_t slot) {
    auto self = static_cast<unsigned char*>(g_fileSystem) + 8;
    return reinterpret_cast<Function>((*reinterpret_cast<void***>(self))[slot]);
}
template<class Function> Function PrimaryFunction(std::size_t slot) {
    return reinterpret_cast<Function>((*reinterpret_cast<void***>(g_fileSystem))[slot]);
}

// Use the owning native IFileSystem search paths; never CRT-open game assets.
// The inner IBaseFileSystem ABI is proven by engine +AB80 and the existing
// filesystem SyncRead adapter. Each decoder owns its handle and seek position.
class NativeFile {
public:
    void* handle = nullptr;
    std::uint64_t size = 0;
    std::uint64_t position = 0;
    // An empty path opens nothing (R2 generated waves have no native file).
    explicit NativeFile(const std::string& path) {
        if (path.empty()) return;
        auto self = static_cast<unsigned char*>(g_fileSystem) + 8;
        handle = InnerFunction<void* (*)(void*, const char*, const char*, const char*, int)>(2)(self, path.c_str(), "rb", "GAME", 0);
        if (handle) {
            size = InnerFunction<std::uint64_t (*)(void*, void*)>(7)(self, handle);
        }
    }
    ~NativeFile() {
        if (handle) InnerFunction<void (*)(void*, void*)>(3)(static_cast<unsigned char*>(g_fileSystem) + 8, handle);
    }
    NativeFile(const NativeFile&) = delete;
    NativeFile& operator=(const NativeFile&) = delete;
    void Seek(std::uint64_t offset) {
        if (offset > size || offset > INT64_MAX) Fail("file seek is outside the source");
        InnerFunction<void (*)(void*, void*, std::int64_t, int)>(4)(static_cast<unsigned char*>(g_fileSystem) + 8, handle, static_cast<std::int64_t>(offset), 0);
        position = offset;
    }
    std::size_t Read(void* out, std::size_t count) {
        count = static_cast<std::size_t>((std::min)(std::uint64_t(count), size - position));
        const auto got = InnerFunction<std::int64_t (*)(void*, void*, std::int64_t, void*)>(0)(static_cast<unsigned char*>(g_fileSystem) + 8, out, static_cast<std::int64_t>(count), handle);
        if (got < 0 || std::uint64_t(got) > count) Fail("native filesystem read failed");
        position += got;
        return static_cast<std::size_t>(got);
    }
    void ReadAt(std::uint64_t offset, void* out, std::size_t count) {
        Seek(offset);
        if (Read(out, count) != count) Fail("truncated source file");
    }
};

std::string NormalizePath(std::string path) {
    for (char& c : path) {
        if (c == '\\') c = '/';
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    }
    return path;
}

std::pair<unsigned, std::size_t> SourcePriority(const NativeFile& file, const std::string& name) {
    // Matching filesystem +14900 constructs CFileHandle (144 bytes, CFHa at
    // +136): VPK file index +24, archive search-order index +100. +14780
    // constructs regular handles with file index -1. Never infer provenance
    // from the filename extension or bypass mounted VPK/loose override hooks.
    const auto* bytes = static_cast<const unsigned char*>(file.handle);
    std::uint32_t marker;
    std::int32_t index;
    std::memcpy(&marker, bytes + 136, sizeof(marker));
    std::memcpy(&index, bytes + 24, sizeof(index));
    if (marker != 0x43464861) Fail("unexpected native CFileHandle ABI");
    if (index != -1) {
        std::uint32_t archive;
        std::memcpy(&archive, bytes + 100, sizeof(archive));
        return {1, archive};
    }
    // Native +1AD90 resolves loose/pack paths; +19930 exposes their exact
    // GAME search order. Preserve that order across WAV/Ogg alternatives.
    char resolved[1024]{};
    using Resolve = const char* (*)(void*, const char*, const char*, char*, std::int64_t, int, unsigned*);
    using SearchPaths = std::int64_t (*)(void*, const char*, bool, char*, std::int64_t);
    void* pack = nullptr;
    std::memcpy(&pack, bytes + 16, sizeof(pack));
    if (!PrimaryFunction<Resolve>(15)(g_fileSystem, name.c_str(), "GAME", resolved, sizeof(resolved), pack ? 2 : 1, nullptr))
        Fail("native filesystem could not resolve the selected source search path");
    const auto required = PrimaryFunction<SearchPaths>(16)(g_fileSystem, "GAME", true, nullptr, 0);
    if (required <= 0 || required > 1024 * 1024) Fail("invalid native GAME search-path list size");
    std::vector<char> paths(static_cast<std::size_t>(required));
    if (PrimaryFunction<SearchPaths>(16)(g_fileSystem, "GAME", true, paths.data(), required) > required)
        Fail("native search paths changed during source selection; rebuild audio metadata");
    const auto actual = NormalizePath(resolved);
    const auto list = NormalizePath(paths.data());
    std::size_t first = 0, rank = 0;
    while (first < list.size()) {
        const auto next = list.find(';', first);
        auto root = list.substr(first, next == std::string::npos ? next : next - first);
        if (!root.empty() && root.back() != '/') root += '/';
        if (!root.empty() && actual.compare(0, root.size(), root) == 0) return {0, rank};
        if (next == std::string::npos) break;
        first = next + 1;
        ++rank;
    }
    Fail("selected audio source does not belong to a native GAME search path");
}

// Generated waves of the converted R2 maps ("sound/r2port/<source>.wav") are decoded from the player's own Titanfall 2
// sound streams on demand; there is no native file behind them.
bool IsR2Wave(const std::string& path) { return path.compare(0, 13, "sound/r2port/") == 0; }

std::uint16_t U16(const unsigned char* p) { return std::uint16_t(p[0]) | (std::uint16_t(p[1]) << 8); }
std::uint32_t U32(const unsigned char* p) { return std::uint32_t(U16(p)) | (std::uint32_t(U16(p + 2)) << 16); }
bool Tag(const unsigned char* p, const char* tag) { return std::memcmp(p, tag, 4) == 0; }

struct SourceInfo {
    std::uint32_t rate = 0;
    std::uint32_t channels = 0;
    std::uint64_t frames = 0;
    std::uint64_t dataOffset = 0;
    std::uint16_t format = 0;
    std::uint16_t bits = 0;
    bool loop = false;
    bool loopSpecified = false;
    bool vorbis = false;
};
void ValidateInfo(const SourceInfo& info) {
    if (info.channels != 1 && info.channels != 2 && info.channels != 6)
        Fail("R1 supports mono, stereo, or 5.1 audio; this channel layout is unsupported");
    if (info.rate < 8000 || info.rate > 192000) Fail("supported source sample rates are 8000 through 192000 Hz");
    if (!info.frames) Fail("source contains no audio frames");
    if (info.frames > (UINT64_MAX - info.rate / 2) / kMixerRate)
        Fail("source frame count overflows canonical sample conversion");
    const auto frames = (info.frames * kMixerRate + info.rate / 2) / info.rate;
    if (!frames || frames > std::uint64_t(INT32_MAX) / (2 * info.channels))
        Fail("canonical PCM byte count exceeds the signed 32-bit R1 streaming ABI");
}
void SetLoop(SourceInfo& info, std::uint64_t start, std::uint64_t end) {
    if (start != 0 || end != info.frames)
        Fail("partial loop cannot be represented by R1's whole-source loop flag; provide a whole-source loop or split the asset");
    info.loopSpecified = true;
    info.loop = true;
}

SourceInfo ReadWaveInfo(NativeFile& file) {
    unsigned char header[12];
    file.ReadAt(0, header, sizeof(header));
    if (!Tag(header, "RIFF") || !Tag(header + 8, "WAVE")) Fail("source is not RIFF WAVE (RF64, RIFX, and raw PCM are unsupported)");
    const std::uint64_t end = std::uint64_t(U32(header + 4)) + 8;
    if (end > file.size || end < 12) Fail("invalid RIFF length");
    SourceInfo info;
    bool haveFormat = false, haveData = false;
    std::uint64_t dataBytes = 0, loopStart = 0, loopEnd = 0;
    bool haveSampleLoop = false;
    std::uint64_t cueCount = 0;
    std::uint32_t cueStart = 0;
    for (std::uint64_t cursor = 12; cursor < end;) {
        if (end - cursor < 8) Fail("truncated RIFF chunk header");
        unsigned char chunk[8];
        file.ReadAt(cursor, chunk, sizeof(chunk));
        const auto length = U32(chunk + 4);
        const auto payload = cursor + 8;
        if (length > end - payload) Fail("RIFF chunk exceeds the declared file length");
        if (Tag(chunk, "fmt ")) {
            if (haveFormat || length < 16) Fail("duplicate or truncated WAVE format chunk");
            unsigned char format[40]{};
            file.ReadAt(payload, format, (std::min)(std::size_t(length), sizeof(format)));
            info.format = U16(format);
            info.channels = U16(format + 2);
            info.rate = U32(format + 4);
            info.bits = U16(format + 14);
            if (info.format == 0xFFFE) {
                if (length < 40 || U16(format + 16) < 22 || U16(format + 18) != info.bits)
                    Fail("unsupported WAVE_FORMAT_EXTENSIBLE valid-bit layout");
                static constexpr unsigned char suffix[12] = {0,0,0x10,0,0x80,0,0,0xAA,0,0x38,0x9B,0x71};
                if (U16(format + 26) != 0 || std::memcmp(format + 28, suffix, sizeof(suffix)))
                    Fail("unsupported WAVE_FORMAT_EXTENSIBLE codec GUID");
                info.format = U16(format + 24);
                const auto mask = U32(format + 20);
                if (mask && mask != (info.channels == 1 ? 4u : info.channels == 2 ? 3u : 0x3Fu))
                    Fail("unsupported WAVE channel mask (expected mono, stereo, or FL/FR/FC/LFE/BL/BR)");
            }
            if (!((info.format == 1 && (info.bits == 8 || info.bits == 16 || info.bits == 24 || info.bits == 32)) ||
                (info.format == 3 && (info.bits == 32 || info.bits == 64))))
                Fail("unsupported WAVE codec; expected PCM8/16/24/32 or IEEE float32/64");
            if (U16(format + 12) != info.channels * (info.bits / 8) ||
                std::uint64_t(U32(format + 8)) != std::uint64_t(info.rate) * U16(format + 12))
                Fail("inconsistent WAVE block alignment or byte rate");
            haveFormat = true;
        } else if (Tag(chunk, "data")) {
            if (haveData) Fail("multiple WAVE data chunks are unsupported");
            info.dataOffset = payload;
            dataBytes = length;
            haveData = true;
        } else if (Tag(chunk, "smpl")) {
            if (length < 36) Fail("truncated smpl chunk");
            unsigned char sample[60]{};
            file.ReadAt(payload, sample, (std::min)(std::size_t(length), sizeof(sample)));
            const auto loops = U32(sample + 28);
            if (loops > 1 || (loops && (length < 60 || U32(sample + 40) || U32(sample + 52) || U32(sample + 56))))
                Fail("R1 cannot represent multiple, alternating, fractional, or finite-count smpl loops");
            if (loops) {
                if (haveSampleLoop) Fail("duplicate smpl loop");
                loopStart = U32(sample + 44);
                loopEnd = std::uint64_t(U32(sample + 48)) + 1; // smpl end is inclusive.
                haveSampleLoop = true;
            }
        } else if (Tag(chunk, "cue ")) {
            if (length < 4) Fail("truncated cue chunk");
            unsigned char count[4];
            file.ReadAt(payload, count, 4);
            const auto n = U32(count);
            if (n > (length - 4) / 24) Fail("truncated WAVE cue records");
            for (std::uint32_t i = 0; i < n; ++i) {
                unsigned char cue[24];
                file.ReadAt(payload + 4 + std::uint64_t(i) * 24, cue, sizeof(cue));
                if (!Tag(cue + 8, "data") || U32(cue + 12) || U32(cue + 16))
                    Fail("unsupported segmented WAVE cue");
                if (!cueCount) cueStart = U32(cue + 20);
                ++cueCount;
            }
        }
        cursor = payload + length + (length & 1);
        if (cursor > end) Fail("missing RIFF chunk padding");
    }
    if (!haveFormat || !haveData) Fail("WAVE requires both fmt and data chunks");
    const auto alignment = info.channels * (info.bits / 8);
    if (!alignment || dataBytes % alignment) Fail("WAVE data ends in a partial frame");
    info.frames = dataBytes / alignment;
    ValidateInfo(info);
    if (haveSampleLoop) SetLoop(info, loopStart, loopEnd);
    // Source-style cue loop: exactly one cue identifies the loop start. Do not
    // discard nonzero starts or multiple cues as if they were whole-file loops.
    if (!haveSampleLoop && cueCount) {
        if (cueCount != 1) Fail("multiple WAVE cues have no unambiguous R1 whole-source loop representation");
        SetLoop(info, cueStart, info.frames);
    }
    return info;
}

std::size_t VorbisRead(void* ptr, std::size_t size, std::size_t count, void* source) {
    if (!size || count > SIZE_MAX / size) return 0;
    try { return static_cast<NativeFile*>(source)->Read(ptr, size * count) / size; }
    catch (...) { return 0; }
}
int VorbisSeek(void* source, ogg_int64_t offset, int origin) {
    auto& file = *static_cast<NativeFile*>(source);
    const auto base = origin == SEEK_SET ? 0 : origin == SEEK_CUR ? file.position : origin == SEEK_END ? file.size : UINT64_MAX;
    if (base == UINT64_MAX || base > INT64_MAX || (offset < 0 && std::uint64_t(-(offset + 1)) + 1 > base) ||
        (offset > 0 && std::uint64_t(offset) > file.size - base)) return -1;
    try { file.Seek(static_cast<std::uint64_t>(static_cast<std::int64_t>(base) + offset)); return 0; }
    catch (...) { return -1; }
}
long VorbisTell(void* source) { return static_cast<long>(static_cast<NativeFile*>(source)->position); }
int VorbisClose(void*) { return 0; } // NativeFile owns the handle, not libvorbis.

std::uint64_t CommentNumber(vorbis_comment* comments, const char* name, bool& found) {
    const int count = vorbis_comment_query_count(comments, const_cast<char*>(name));
    found = count != 0;
    if (!count) return 0;
    if (count != 1) Fail("duplicate Vorbis loop tag");
    const char* value = vorbis_comment_query(comments, const_cast<char*>(name), 0);
    std::uint64_t number = 0;
    const auto end = value + std::strlen(value);
    const auto parsed = std::from_chars(value, end, number);
    if (parsed.ec != std::errc{} || parsed.ptr != end) Fail("Vorbis loop tag must be an unsigned frame index");
    return number;
}

class Decoder {
public:
    NativeFile file;
    SourceInfo info;
    OggVorbis_File vorbis{};
    bool opened = false;
    std::vector<unsigned char> waveScratch;
    std::unique_ptr<r2audio::WaveSource> r2;
    bool Valid() const { return r2 || file.handle; }
    explicit Decoder(const std::string& path) : file(IsR2Wave(path) ? std::string() : path) {
        if (IsR2Wave(path)) {
            std::string error;
            r2 = r2audio::WaveSource::Open(path, &error);
            if (!r2) throw AudioError("R2 wave: " + error);
            const auto& wave = r2->Info();
            info.rate = wave.sampleRate;
            info.channels = wave.channels;
            info.frames = wave.frames;
            info.bits = 16;
            info.loop = wave.loop;
            info.loopSpecified = true;
            ValidateInfo(info);
            return;
        }
        if (!file.handle) return;
        unsigned char magic[4];
        file.ReadAt(0, magic, sizeof(magic));
        if (Tag(magic, "OggS")) {
            if (file.size > LONG_MAX) Fail("Vorbis source exceeds the seekable Win64 vorbisfile size limit");
            file.Seek(0);
            const ov_callbacks callbacks{VorbisRead, VorbisSeek, VorbisClose, VorbisTell};
            const int status = ov_open_callbacks(&file, &vorbis, nullptr, 0, callbacks);
            if (status) throw AudioError("Vorbis open failed: " + std::to_string(status) + " (Opus and other Ogg codecs are unsupported)");
            opened = true;
            try {
                if (ov_streams(&vorbis) != 1) Fail("chained Vorbis streams are unsupported");
                const auto* format = ov_info(&vorbis, 0);
                const auto frames = ov_pcm_total(&vorbis, 0);
                if (!format || frames <= 0 || format->rate <= 0) Fail("invalid Vorbis stream metadata");
                info.vorbis = true;
                info.rate = static_cast<std::uint32_t>(format->rate);
                info.channels = static_cast<std::uint32_t>(format->channels);
                info.frames = static_cast<std::uint64_t>(frames);
                info.bits = 16;
                ValidateInfo(info);
                bool hasStart, hasEnd, hasLength;
                auto* comments = ov_comment(&vorbis, 0);
                const auto start = CommentNumber(comments, "LOOPSTART", hasStart);
                auto end = CommentNumber(comments, "LOOPEND", hasEnd);
                const auto length = CommentNumber(comments, "LOOPLENGTH", hasLength);
                if (hasStart || hasEnd || hasLength) {
                    if (!hasStart || (!hasEnd && !hasLength)) Fail("Vorbis loop needs LOOPSTART and LOOPEND or LOOPLENGTH");
                    if (hasLength) {
                        if (length > UINT64_MAX - start || (hasEnd && end != start + length)) Fail("inconsistent Vorbis loop extent");
                        end = start + length;
                    }
                    SetLoop(info, start, end); // Vorbis LOOPEND is exclusive.
                }
            } catch (...) { ov_clear(&vorbis); opened = false; throw; }
        } else {
            info = ReadWaveInfo(file);
        }
    }
    ~Decoder() { if (opened) ov_clear(&vorbis); }
    Decoder(const Decoder&) = delete;
    void ReadFrames(std::uint64_t first, std::size_t count, std::vector<float>& output) {
        if (first > info.frames || count > info.frames - first) Fail("source frame request exceeds EOF");
        output.resize(count * info.channels);
        if (r2) {
            std::string error;
            if (!r2->Read(first, count, output.data(), &error)) throw AudioError("R2 wave read: " + error);
            return;
        }
        if (info.vorbis) {
            const int seek = ov_pcm_seek(&vorbis, static_cast<ogg_int64_t>(first));
            if (seek) throw AudioError("Vorbis seek failed: " + std::to_string(seek));
            std::size_t done = 0;
            // The R1Delta content set stores its six channel Ogg files in WAVE order (FL FR FC LFE BL BR): of the 436 six
            // channel Oggs in the shipped delta VPK, 421 have symmetric FL/FR and BL/BR levels in that order, 5 are
            // ambiguous and none is clearly Vorbis I order (FL FC FR BL BR LFE). The encoder wrote no channel mask. Interleave
            // the planes as they come: remapping them as Vorbis I order put the front right signal in the centre slot and the
            // LFE in the rear left slot, which played every 6 channel sound with a wrong stereo balance (40mm, shoulder
            // rockets, titan embark, ...).
            while (done < count) {
                float** planes = nullptr;
                int section = 0;
                const long got = ov_read_float(&vorbis, &planes, static_cast<int>(count - done), &section);
                if (got <= 0) throw AudioError("Vorbis decode failed before declared EOF: " + std::to_string(got));
                for (long frame = 0; frame < got; ++frame)
                    for (unsigned channel = 0; channel < info.channels; ++channel)
                        output[(done + frame) * info.channels + channel] = planes[channel][frame];
                done += got;
            }
        } else {
            const auto width = info.bits / 8;
            waveScratch.resize(count * info.channels * width);
            file.ReadAt(info.dataOffset + first * info.channels * width, waveScratch.data(), waveScratch.size());
            for (std::size_t i = 0; i < output.size(); ++i) {
                const auto* p = waveScratch.data() + i * width;
                double sample;
                if (info.format == 3) {
                    if (width == 4) { float value; std::memcpy(&value, p, 4); sample = value; }
                    else { double value; std::memcpy(&value, p, 8); sample = value; }
                } else if (width == 1) sample = (int(*p) - 128) / 128.0;
                else if (width == 2) sample = static_cast<std::int16_t>(U16(p)) / 32768.0;
                else if (width == 3) {
                    std::int32_t value = std::int32_t(p[0]) | (std::int32_t(p[1]) << 8) | (std::int32_t(p[2]) << 16);
                    if (value & 0x800000) value -= 0x1000000;
                    sample = value / 8388608.0;
                } else sample = static_cast<std::int32_t>(U32(p)) / 2147483648.0;
                if (!std::isfinite(sample)) Fail("WAVE contains a non-finite floating-point sample");
                output[i] = static_cast<float>(sample);
            }
        }
    }
    void Canonical(std::uint64_t first, std::size_t count, std::int16_t* destination) {
        std::vector<float> source;
        while (count) {
            const auto block = (std::min)(count, kOutputBlock);
            const std::uint64_t firstSource = first * info.rate / kMixerRate;
            const std::uint64_t lastSource = (first + block - 1) * info.rate / kMixerRate;
            const auto begin = firstSource > kFilterRadius ? firstSource - kFilterRadius : 0;
            const auto end = (std::min)(info.frames, lastSource + kFilterRadius + 2);
            ReadFrames(begin, static_cast<std::size_t>(end - begin), source);
            for (std::size_t frame = 0; frame < block; ++frame) {
                const auto numerator = (first + frame) * info.rate;
                const auto center = numerator / kMixerRate;
                const double fraction = double(numerator % kMixerRate) / kMixerRate;
                std::array<double, 6> values{};
                if (info.rate == kMixerRate) {
                    for (unsigned channel = 0; channel < info.channels; ++channel)
                        values[channel] = source[(center - begin) * info.channels + channel];
                } else if (r2) {
                    const KernelTable& kernel = GetKernel(info.rate);
                    const double position = fraction * kKernelPhases;
                    const int row = static_cast<int>(position);
                    const float blend = static_cast<float>(position - row);
                    const float* low = kernel.weights.data() + static_cast<std::size_t>(row) * kKernelTaps;
                    const float* high = low + kKernelTaps;
                    double weightSum = 0;
                    for (int tap = -kFilterRadius; tap <= kFilterRadius; ++tap) {
                        const float weight = low[tap + kFilterRadius] + (high[tap + kFilterRadius] - low[tap + kFilterRadius]) * blend;
                        if (weight == 0.0f) continue;
                        const auto index = (std::max)(std::int64_t(0), (std::min)(std::int64_t(info.frames - 1), std::int64_t(center) + tap));
                        const float* frame = source.data() + (std::uint64_t(index) - begin) * info.channels;
                        for (unsigned channel = 0; channel < info.channels; ++channel) values[channel] += weight * frame[channel];
                        weightSum += weight;
                    }
                    for (unsigned channel = 0; channel < info.channels; ++channel) values[channel] /= weightSum;
                } else {
                    // Windowed-sinc low-pass conversion. Absolute rational phase
                    // makes independent seeks and adjacent reads sample-identical.
                    const double cutoff = (std::min)(1.0, double(kMixerRate) / info.rate);
                    double weightSum = 0;
                    for (int tap = -kFilterRadius; tap <= kFilterRadius; ++tap) {
                        const double distance = tap - fraction;
                        if (std::abs(distance) >= kFilterRadius) continue;
                        constexpr double pi = 3.14159265358979323846;
                        const double phase = pi * distance * cutoff;
                        const double sinc = std::abs(phase) < 1e-12 ? 1 : std::sin(phase) / phase;
                        const double window = 0.42 + 0.5 * std::cos(pi * distance / kFilterRadius) + 0.08 * std::cos(2 * pi * distance / kFilterRadius);
                        const double weight = cutoff * sinc * window;
                        const auto index = (std::max)(std::int64_t(0), (std::min)(std::int64_t(info.frames - 1), std::int64_t(center) + tap));
                        for (unsigned channel = 0; channel < info.channels; ++channel)
                            values[channel] += weight * source[(std::uint64_t(index) - begin) * info.channels + channel];
                        weightSum += weight;
                    }
                    for (unsigned channel = 0; channel < info.channels; ++channel) values[channel] /= weightSum;
                }
                for (unsigned channel = 0; channel < info.channels; ++channel) {
                    const auto value = values[channel];
                    if (!std::isfinite(value)) Fail("decoded audio contains a non-finite sample");
                    const double scaled = std::round(value * 32768.0);
                    *destination++ = static_cast<std::int16_t>((std::max)(-32768.0, (std::min)(32767.0, scaled)));
                }
            }
            first += block;
            count -= block;
        }
    }
};

struct Entry {
    R1AudioCacheRecord record{};
    SourceInfo info;
    std::string sourcePath;
    std::vector<std::int16_t> prefix;
    // The generated path used to reopen and reparse the source, and seek a fresh
    // decoder, on every stream read. Keep one decoder per source instead; reads of
    // the same source serialize on decoderMutex, different sources stay parallel.
    std::mutex decoderMutex;
    std::unique_ptr<Decoder> decoder;
};
std::mutex g_sourcesMutex;
std::unordered_map<std::string, std::shared_ptr<Entry>> g_sources;
std::unordered_map<std::string, std::string> g_errors;

std::string CanonicalName(const char* input) {
    if (!input) Fail("null sound name");
    std::string name(input);
    for (char& c : name) {
        if (c == '\\') c = '/';
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    }
    if (name.compare(0, 6, "sound/") != 0) name.insert(0, "sound/");
    if (name.size() < 4 || name.compare(name.size() - 4, 4, ".wav")) name += ".wav";
    if (name.size() >= sizeof(R1AudioCacheRecord::filename)) Fail("sound path exceeds the R1 260-byte record");
    return name;
}

std::shared_ptr<Entry> LoadEntry(const std::string& name) {
    auto entry = std::make_shared<Entry>();
    const auto oggName = name.substr(0, name.size() - 4) + ".ogg";
    if (IsR2Wave(name)) {
        entry->sourcePath = name;
    } else {
        NativeFile ogg(oggName);
        NativeFile wave(name);
        if (!ogg.handle && !wave.handle) Fail("neither WAV nor Ogg exists on native GAME search paths");
        // Existing Ogg replacement wins only a same-mount tie. A higher
        // priority loose WAV is not hidden by a lower-priority packaged Ogg.
        const bool chooseOgg = ogg.handle && (!wave.handle || SourcePriority(ogg, oggName) <= SourcePriority(wave, name));
        entry->sourcePath = chooseOgg ? oggName : name;
    }
    entry->decoder = std::make_unique<Decoder>(entry->sourcePath);
    if (!entry->decoder->Valid()) Fail("selected audio source disappeared while loading metadata");
    entry->info = entry->decoder->info;
    if (entry->info.vorbis && !entry->info.loopSpecified) {
        // A transcoded Ogg cannot recreate discarded loop metadata. Recover it
        // only from the actual corresponding WAV, never another sound record.
        NativeFile wave(name);
        if (wave.handle) {
            const auto original = ReadWaveInfo(wave);
            if (original.loop) {
                if (original.frames != entry->info.frames || original.rate != entry->info.rate || original.channels != entry->info.channels)
                    Fail("Ogg lacks loop tags and does not match the corresponding looping WAV; migrate explicit Vorbis loop tags");
                entry->info.loop = true;
                entry->info.loopSpecified = true;
            }
        }
    }
    const auto frames = (entry->info.frames * kMixerRate + entry->info.rate / 2) / entry->info.rate;
    auto& record = entry->record;
    std::memcpy(record.filename, name.c_str(), name.size() + 1);
    record.channels = entry->info.channels;
    record.sampleCount = static_cast<std::int32_t>(frames * entry->info.channels);
    record.duration = static_cast<float>(double(frames) / kMixerRate);
    record.loop = entry->info.loop;
    entry->prefix.resize(static_cast<std::size_t>((std::min)(frames, std::uint64_t(kPrefetchFrames))) * entry->info.channels);
    entry->decoder->Canonical(0, entry->prefix.size() / entry->info.channels, entry->prefix.data());
    record.prefetch = entry->prefix.data();
    record.streamOffset = frames > kPrefetchFrames ? entry->prefix.size() * sizeof(std::int16_t) : 0;
    return entry;
}
}

void InitializeR1AudioSources(void* nativeFileSystem) {
    std::lock_guard<std::mutex> lock(g_sourcesMutex);
    g_fileSystem = nativeFileSystem;
}
R1AudioCacheRecord* FindR1AudioSource(const char* input) {
    std::string name;
    try {
        name = CanonicalName(input);
        if (!g_fileSystem) Fail("native filesystem is not initialized");
        {
            std::lock_guard<std::mutex> lock(g_sourcesMutex);
            const auto existing = g_sources.find(name);
            if (existing != g_sources.end()) return &existing->second->record;
            if (g_errors.find(name) != g_errors.end()) return nullptr;
        }
        // Build metadata outside the registry lock: opening and decoding a source
        // must not block lookups and stream reads for every other sound.
        auto entry = LoadEntry(name);
        auto* record = &entry->record;
        {
            std::lock_guard<std::mutex> lock(g_sourcesMutex);
            const auto existing = g_sources.find(name);
            if (existing != g_sources.end()) return &existing->second->record;
            g_sources.emplace(name, std::move(entry));
        }
        return record;
    } catch (const std::exception& error) {
        Warning("R1Delta audio metadata '%s': %s\n", input ? input : "<null>", error.what());
        if (!name.empty()) {
            std::lock_guard<std::mutex> lock(g_sourcesMutex);
            g_errors.emplace(name, error.what());
        }
        return nullptr;
    }
}
R1AudioReadResult ReadR1AudioSource(const char* filename, const char* pathID,
    std::uint64_t offset, void* destination, std::size_t bytes,
    std::size_t& bytesRead, std::string& error) {
    bytesRead = 0;
    std::shared_ptr<Entry> entry;
    try {
        if (!filename || (pathID && std::strcmp(pathID, "GAME"))) return R1AudioReadResult::NotManaged;
        const auto name = CanonicalName(filename);
        {
            std::lock_guard<std::mutex> lock(g_sourcesMutex);
            const auto found = g_sources.find(name);
            if (found == g_sources.end()) return R1AudioReadResult::NotManaged;
            entry = found->second;
        }
        const auto frameBytes = entry->info.channels * sizeof(std::int16_t);
        const auto totalBytes = std::uint64_t(entry->record.sampleCount) * sizeof(std::int16_t);
        if (offset > totalBytes || offset % frameBytes || bytes % frameBytes || (!destination && bytes))
            Fail("canonical PCM read is unaligned, outside EOF, or has no destination buffer");
        const auto count = static_cast<std::size_t>((std::min)(std::uint64_t(bytes), totalBytes - offset));
        std::lock_guard<std::mutex> decodeLock(entry->decoderMutex);
        if (!entry->decoder) {
            entry->decoder = std::make_unique<Decoder>(entry->sourcePath);
            if (!entry->decoder->Valid()) Fail("cached source was removed; run r1delta_audio_rebuild");
        }
        entry->decoder->Canonical(offset / frameBytes, count / frameBytes, static_cast<std::int16_t*>(destination));
        bytesRead = count;
        return R1AudioReadResult::Success;
    } catch (const std::exception& exception) {
        error = exception.what();
        return entry ? R1AudioReadResult::Error : R1AudioReadResult::NotManaged;
    }
}
void DestroyR1AudioSources() {
    std::lock_guard<std::mutex> lock(g_sourcesMutex);
    g_sources.clear();
    g_errors.clear();
}
void ReportR1AudioSources() {
    std::lock_guard<std::mutex> lock(g_sourcesMutex);
    std::size_t prefixBytes = 0;
    for (const auto& source : g_sources) prefixBytes += source.second->prefix.size() * sizeof(std::int16_t);
    char message[256];
    std::snprintf(message, sizeof(message), "R1Delta audio: %zu generated records, %zu failed sources, %zu prefetch bytes; no decoded-track cache\n", g_sources.size(), g_errors.size(), prefixBytes);
    Msg("%s", message);
    Msg("%s", r2audio::StatsLine().c_str());
}
