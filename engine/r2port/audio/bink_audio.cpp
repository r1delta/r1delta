// See bink_audio.h for the provenance of the algorithm (FFmpeg binkaudio.c model, LGPL-2.1-or-later).
#include "bink_audio.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>

namespace r2audio {
namespace {

// ff_wma_critical_freqs
constexpr uint16_t kCriticalFreqs[25] = {100,  200,  300,  400,  510,  630,  770,  920,  1080,
                                         1270, 1480, 1720, 2000, 2320, 2700, 3150, 3700, 4400,
                                         5300, 6400, 7700, 9500, 12000, 15500, 24500};
constexpr uint8_t kRleLength[16] = {2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 32, 64};
constexpr uint32_t kMaxFrameBytes = 1u << 20;

// Little-endian bit reader (first bit read is the least significant bit of the first byte).
// The buffer must have at least 8 readable bytes past `sizeBits / 8`.
struct BitReader {
    const uint8_t* data = nullptr;
    uint64_t sizeBits = 0;
    uint64_t pos = 0;
    uint32_t Get(unsigned n) {
        uint64_t word;
        std::memcpy(&word, data + (pos >> 3), 8);
        const uint32_t value = static_cast<uint32_t>((word >> (pos & 7)) & ((1ull << n) - 1));
        pos += n;
        return value;
    }
    bool Overrun() const { return pos > sizeBits; }
    void Align32() { pos = (pos + 31) & ~uint64_t(31); }
};

float GetFloat(BitReader& bits) {
    const int power = static_cast<int>(bits.Get(5));
    const uint32_t mantissa = bits.Get(23);
    float value = std::ldexp(static_cast<float>(mantissa), power - 23);
    if (bits.Get(1)) value = -value;
    return value;
}

// DCT-III with the Bink convention x[t] = c[0] + sum_{k>=1} c[k] cos(pi (2t+1) k / 2N),
// computed as Re(IFFT_2N(c[k] e^{i pi k / 2N})).
struct DctTables {
    int n = 0;
    std::vector<double> preRe, preIm;  // e^{i pi k / 2N}, k < N
    std::vector<double> twRe, twIm;    // e^{+2 pi i j / 2N}, j < N
    std::vector<uint32_t> rev;         // bit reversal for 2N
};

const DctTables& GetTables(int n) {
    static std::mutex lock;
    static std::unique_ptr<DctTables> tables[3];
    const int slot = n == 512 ? 0 : n == 1024 ? 1 : 2;
    std::lock_guard<std::mutex> guard(lock);
    if (!tables[slot]) {
        auto t = std::make_unique<DctTables>();
        t->n = n;
        const int m = 2 * n;
        t->preRe.resize(n);
        t->preIm.resize(n);
        for (int k = 0; k < n; ++k) {
            const double a = 3.14159265358979323846 * k / m;
            t->preRe[k] = std::cos(a);
            t->preIm[k] = std::sin(a);
        }
        t->twRe.resize(n);
        t->twIm.resize(n);
        for (int j = 0; j < n; ++j) {
            const double a = 2.0 * 3.14159265358979323846 * j / m;
            t->twRe[j] = std::cos(a);
            t->twIm[j] = std::sin(a);
        }
        t->rev.resize(m);
        int bits = 0;
        while ((1 << bits) < m) ++bits;
        for (int i = 0; i < m; ++i) {
            uint32_t r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b)) r |= 1u << (bits - 1 - b);
            t->rev[i] = r;
        }
        tables[slot] = std::move(t);
    }
    return *tables[slot];
}

void InverseDct(const DctTables& t, const float* coeffs, double scale, float* out, std::vector<double>& re,
                std::vector<double>& im) {
    const int n = t.n, m = 2 * n;
    re.assign(m, 0.0);
    im.assign(m, 0.0);
    for (int k = 0; k < n; ++k) {
        const double c = coeffs[k];
        re[t.rev[k]] = c * t.preRe[k];
        im[t.rev[k]] = c * t.preIm[k];
    }
    for (int len = 2; len <= m; len <<= 1) {
        const int half = len >> 1, step = m / len;
        for (int base = 0; base < m; base += len) {
            for (int j = 0; j < half; ++j) {
                const double wr = t.twRe[j * step], wi = t.twIm[j * step];
                const int a = base + j, b = a + half;
                const double xr = re[b] * wr - im[b] * wi;
                const double xi = re[b] * wi + im[b] * wr;
                re[b] = re[a] - xr;
                im[b] = im[a] - xi;
                re[a] += xr;
                im[a] += xi;
            }
        }
    }
    for (int i = 0; i < n; ++i) out[i] = static_cast<float>(re[i] * scale);
}

struct StreamState {
    unsigned channels = 2;
    bool first = true;
    std::vector<float> previous;  // channels * overlap
};

struct FrameSlot {
    int64_t index = -1;
    std::vector<float> planar;  // channels * samplesPerFrame
};

}  // namespace

struct BinkAudio::Impl {
    ReadFn readData;
    std::vector<uint8_t> headerFrames;  // frames stored in the header region
    std::vector<uint64_t> prefix;       // virtual byte offset of each seek entry (+ total)
    uint32_t framesPerEntry = 1;
    unsigned frameLen = 0, overlap = 0, bands = 0;
    double root = 0, dctScale = 0;
    float quantTable[96];
    std::vector<int> bandStart;
    std::vector<StreamState> streams;
    std::vector<FrameSlot> cache = std::vector<FrameSlot>(2);
    unsigned cacheNext = 0;
    int64_t nextFrame = 0;
    uint64_t nextOffset = 0;
    BinkInfo* info = nullptr;
    std::vector<double> fftRe, fftIm;
    std::vector<float> coeff;
    std::vector<uint8_t> payload;

    bool ReadVirtual(uint64_t offset, void* destination, size_t bytes) {
        auto* out = static_cast<uint8_t*>(destination);
        const uint64_t headerBytes = headerFrames.size();
        if (offset < headerBytes) {
            const size_t part = static_cast<size_t>(std::min<uint64_t>(bytes, headerBytes - offset));
            std::memcpy(out, headerFrames.data() + offset, part);
            out += part;
            bytes -= part;
            offset += part;
        }
        if (!bytes) return true;
        return readData(offset - headerBytes, out, bytes);
    }

    // Reads the frame at `offset` into `payload` (zero padded); advances offset past it.
    bool ReadFrame(uint64_t& offset, std::string* error) {
        uint8_t head[8];
        if (!ReadVirtual(offset, head, 4)) return Fail(error, "frame header read failed");
        uint16_t marker, size;
        std::memcpy(&marker, head, 2);
        std::memcpy(&size, head + 2, 2);
        if (marker != 0x9999) return Fail(error, "bad frame marker");
        uint32_t headerSize = 4;
        uint32_t payloadSize = size;
        if (size == 0xFFFF) {
            if (!ReadVirtual(offset + 4, head + 4, 4)) return Fail(error, "frame header read failed");
            uint16_t real;
            std::memcpy(&real, head + 4, 2);
            payloadSize = real;
            headerSize = 8;
        }
        if (payloadSize > kMaxFrameBytes) return Fail(error, "frame too large");
        payload.assign(payloadSize + 16, 0);
        if (!ReadVirtual(offset + headerSize, payload.data(), payloadSize)) return Fail(error, "frame payload read failed");
        offset += headerSize + payloadSize;
        return true;
    }

    static bool Fail(std::string* error, const char* message) {
        if (error) *error = message;
        return false;
    }

    void ResetStreams() {
        for (auto& s : streams) {
            s.first = true;
            std::fill(s.previous.begin(), s.previous.end(), 0.0f);
        }
    }

    // Decodes one block (one 2-channel stream) into planar[ch * frameLen + i].
    bool DecodeBlock(BitReader& bits, StreamState& stream, std::vector<float>& planar, std::string* error) {
        bits.Get(2);
        for (unsigned ch = 0; ch < stream.channels; ++ch) {
            float* c = planar.data() + static_cast<size_t>(ch) * frameLen;
            c[0] = GetFloat(bits) * static_cast<float>(root);
            c[1] = GetFloat(bits) * static_cast<float>(root);
            float quant[25];
            for (unsigned i = 0; i < bands; ++i) quant[i] = quantTable[std::min<uint32_t>(bits.Get(8), 95)];
            unsigned k = 0;
            float q = quant[0];
            unsigned i = 2;
            while (i < frameLen) {
                unsigned j;
                if (bits.Get(1))
                    j = i + kRleLength[bits.Get(4)] * 8;
                else
                    j = i + 8;
                j = std::min(j, frameLen);
                const unsigned width = bits.Get(4);
                if (width == 0) {
                    std::memset(c + i, 0, (j - i) * sizeof(float));
                    i = j;
                    while (static_cast<unsigned>(bandStart[k]) < i) q = quant[k++];
                } else {
                    while (i < j) {
                        if (static_cast<unsigned>(bandStart[k]) == i) q = quant[k++];
                        const uint32_t coefficient = bits.Get(width);
                        if (coefficient) {
                            const float v = q * static_cast<float>(coefficient);
                            c[i] = bits.Get(1) ? -v : v;
                        } else {
                            c[i] = 0.0f;
                        }
                        ++i;
                    }
                }
                if (bits.Overrun()) return Fail(error, "block overran its frame");
            }
        }
        return !bits.Overrun() || Fail(error, "block overran its frame");
    }

    // Decodes the frame at nextOffset; output is the planar new samples of every channel.
    bool DecodeNext(std::vector<float>& planarOut, std::string* error) {
        if (!ReadFrame(nextOffset, error)) return false;
        BitReader bits;
        bits.data = payload.data();
        bits.sizeBits = static_cast<uint64_t>(payload.size() - 16) * 8;
        const unsigned total = info->channels;
        const unsigned spf = info->samplesPerFrame;
        planarOut.assign(static_cast<size_t>(total) * spf, 0.0f);
        std::vector<float> block(static_cast<size_t>(2) * frameLen);
        std::vector<float> time(frameLen);
        const DctTables& tables = GetTables(static_cast<int>(frameLen));
        unsigned channelBase = 0;
        for (auto& stream : streams) {
            if (!DecodeBlock(bits, stream, block, error)) return false;
            bits.Align32();
            for (unsigned ch = 0; ch < stream.channels; ++ch) {
                float* coefficients = block.data() + static_cast<size_t>(ch) * frameLen;
                // Bink halves the DC term in the DCT definition used by FFmpeg: x = DCT3(c with c0 * 2), c0 weight 1/2.
                InverseDct(tables, coefficients, dctScale, time.data(), fftRe, fftIm);
                float* previous = stream.previous.data() + static_cast<size_t>(ch) * overlap;
                if (!stream.first) {
                    const unsigned count = overlap * stream.channels;
                    for (unsigned j = 0; j < overlap; ++j) {
                        const unsigned weight = ch + stream.channels * j;
                        time[j] = (previous[j] * static_cast<float>(count - weight) + time[j] * static_cast<float>(weight)) /
                                  static_cast<float>(count);
                    }
                }
                std::memcpy(previous, time.data() + (frameLen - overlap), overlap * sizeof(float));
                std::memcpy(planarOut.data() + static_cast<size_t>(channelBase + ch) * spf, time.data(), spf * sizeof(float));
            }
            stream.first = false;
            channelBase += stream.channels;
        }
        return true;
    }

    bool Position(int64_t frame, std::string* error) {
        // Virtual offset of `frame`, via the seek table then header skipping.
        const uint64_t entry = static_cast<uint64_t>(frame) / framesPerEntry;
        uint64_t offset = 0;
        uint64_t skipped = 0;
        if (!prefix.empty() && entry < prefix.size()) {
            offset = prefix[entry];
            skipped = entry * framesPerEntry;
        }
        while (skipped < static_cast<uint64_t>(frame)) {
            uint8_t head[8];
            if (!ReadVirtual(offset, head, 4)) return Fail(error, "seek read failed");
            uint16_t marker, size;
            std::memcpy(&marker, head, 2);
            std::memcpy(&size, head + 2, 2);
            if (marker != 0x9999) return Fail(error, "seek hit a bad frame marker");
            uint32_t headerSize = 4, payloadSize = size;
            if (size == 0xFFFF) {
                if (!ReadVirtual(offset + 4, head + 4, 4)) return Fail(error, "seek read failed");
                uint16_t real;
                std::memcpy(&real, head + 4, 2);
                payloadSize = real;
                headerSize = 8;
            }
            offset += headerSize + payloadSize;
            ++skipped;
        }
        nextOffset = offset;
        nextFrame = frame;
        return true;
    }

    const std::vector<float>* GetFrame(int64_t frame, std::string* error) {
        for (auto& slot : cache)
            if (slot.index == frame) return &slot.planar;
        if (frame != nextFrame) {
            ResetStreams();
            if (frame > 0) {
                // Prime the overlap with the previous frame; its own output is not blended and is discarded.
                if (!Position(frame - 1, error)) return nullptr;
                std::vector<float> discard;
                if (!DecodeNext(discard, error)) return nullptr;
                nextFrame = frame;
            } else if (!Position(0, error)) {
                return nullptr;
            }
        }
        FrameSlot& slot = cache[cacheNext];
        cacheNext = (cacheNext + 1) % cache.size();
        slot.index = -1;
        if (!DecodeNext(slot.planar, error)) {
            nextFrame = -1;  // force resynchronisation after a failure
            return nullptr;
        }
        slot.index = frame;
        nextFrame = frame + 1;
        return &slot.planar;
    }
};

BinkAudio::BinkAudio() = default;
BinkAudio::~BinkAudio() = default;

bool BinkAudio::Open(const uint8_t* header, size_t headerSize, ReadFn readData, std::string* error) {
    auto fail = [&](const char* message) {
        if (error) *error = message;
        return false;
    };
    if (headerSize < 24) return fail("stream header too small");
    if (std::memcmp(header, "1FCB", 4) != 0) return fail("not a BCF1 stream");
    const uint8_t version = header[4];
    if (version != 1 && version != 2) return fail("unsupported BCF1 version");
    uint16_t rate16;
    uint32_t sampleCount, maxComp, fileSize, seekInfo;
    std::memcpy(&rate16, header + 6, 2);
    std::memcpy(&sampleCount, header + 8, 4);
    std::memcpy(&maxComp, header + 12, 4);
    std::memcpy(&fileSize, header + 16, 4);
    std::memcpy(&seekInfo, header + 20, 4);
    if ((maxComp >> 16) != 0) return fail("Bink Audio 2 streams are not supported");

    impl_ = std::make_unique<Impl>();
    impl_->info = &info_;
    impl_->readData = std::move(readData);
    info_.channels = header[5];
    info_.sampleRate = rate16;
    info_.sampleCount = sampleCount;
    if (info_.channels < 1 || info_.channels > 16 || info_.sampleRate < 8000 || !sampleCount) return fail("bad stream parameters");

    uint32_t entries = seekInfo;
    impl_->framesPerEntry = 1;
    if (version == 2) {
        entries = seekInfo & 0xFFFF;
        impl_->framesPerEntry = std::max<uint32_t>(1, seekInfo >> 16);
    }
    const uint64_t tableEnd = 24 + 2ull * entries;
    if (tableEnd > headerSize) return fail("seek table exceeds the stream header");
    impl_->headerFrames.assign(header + tableEnd, header + headerSize);
    impl_->prefix.resize(entries + 1);
    uint64_t sum = 0;
    for (uint32_t i = 0; i < entries; ++i) {
        uint16_t size;
        std::memcpy(&size, header + 24 + 2 * i, 2);
        impl_->prefix[i] = sum;
        sum += size;
    }
    impl_->prefix[entries] = sum;

    const unsigned bits = info_.sampleRate < 22050 ? 9 : info_.sampleRate < 44100 ? 10 : 11;
    impl_->frameLen = 1u << bits;
    impl_->overlap = impl_->frameLen / 16;
    info_.samplesPerFrame = impl_->frameLen - impl_->overlap;
    info_.frameCount = (sampleCount + info_.samplesPerFrame - 1) / info_.samplesPerFrame;
    // The header sample count can run a few samples past the last coded frame; those samples are silence.
    if (entries) info_.frameCount = std::min<uint32_t>(info_.frameCount, entries * impl_->framesPerEntry);
    impl_->root = impl_->frameLen / (std::sqrt(static_cast<double>(impl_->frameLen)) * 32768.0);
    impl_->dctScale = 2.0 / impl_->frameLen;
    for (int i = 0; i < 96; ++i)
        impl_->quantTable[i] = static_cast<float>(std::exp(i * 0.15289164787221953823f) * impl_->root);

    const unsigned half = (info_.sampleRate + 1) / 2;
    unsigned nb = 1;
    while (nb < 25 && half > kCriticalFreqs[nb - 1]) ++nb;
    impl_->bands = nb;
    impl_->bandStart.assign(nb + 1, 0);
    impl_->bandStart[0] = 2;
    for (unsigned i = 1; i < nb; ++i)
        impl_->bandStart[i] = static_cast<int>((static_cast<uint64_t>(kCriticalFreqs[i - 1]) * impl_->frameLen / half) & ~1ull);
    impl_->bandStart[nb] = static_cast<int>(impl_->frameLen);

    for (unsigned remaining = info_.channels; remaining;) {
        StreamState stream;
        stream.channels = remaining >= 2 ? 2 : 1;
        stream.previous.assign(static_cast<size_t>(stream.channels) * impl_->overlap, 0.0f);
        impl_->streams.push_back(std::move(stream));
        remaining -= impl_->streams.back().channels;
    }
    (void)fileSize;
    return true;
}

bool BinkAudio::Read(uint64_t first, size_t count, float* interleaved, std::string* error) {
    if (!impl_) {
        if (error) *error = "not open";
        return false;
    }
    const unsigned channels = info_.channels, spf = info_.samplesPerFrame;
    std::fill(interleaved, interleaved + count * channels, 0.0f);
    if (!count || first >= info_.sampleCount) return true;
    const uint64_t last = std::min<uint64_t>(first + count, info_.sampleCount);
    for (uint64_t frame = first / spf; frame * spf < last && frame < info_.frameCount; ++frame) {
        const std::vector<float>* planar = impl_->GetFrame(static_cast<int64_t>(frame), error);
        if (!planar) return false;
        const uint64_t begin = std::max<uint64_t>(first, frame * spf);
        const uint64_t end = std::min<uint64_t>(last, (frame + 1) * spf);
        for (unsigned ch = 0; ch < channels; ++ch) {
            const float* source = planar->data() + static_cast<size_t>(ch) * spf;
            for (uint64_t s = begin; s < end; ++s) interleaved[(s - first) * channels + ch] = source[s - frame * spf];
        }
    }
    return true;
}

}  // namespace r2audio
