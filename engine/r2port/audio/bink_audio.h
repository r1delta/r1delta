// Bink Audio (Miles "BCF1" container, DCT variant) decoder for the Titanfall 2 sound banks.
//
// The coefficient coding, band layout, quantiser table, DCT-III scaling and the overlap
// blend follow the Bink audio decoder of FFmpeg (libavcodec/binkaudio.c, Copyright (c) 2007-2011
// Peter Ross, Konstantin Shishkov, LGPL-2.1-or-later, usable under GPL-3.0). This is an
// independent implementation: the DCT is done with a 2N point complex FFT, and multi-channel
// sources are parsed as one block per 2-channel stream per frame (see BinkAudio below).
// It contains no Titanfall 2 data and no code from rsx.
//
// Container facts (observed in the player's own general_stream.mstr files):
//   source stream header ("hsz" bytes, at the source's header offset in the .mstr):
//     u32 'BCF1' ("1FCB" on disk), u8 version (2), u8 channels, u16 sample rate, u32 sample count,
//     u32 maxCompSpaceNeeded (high word set = Bink Audio 2, unsupported), u32 file size (header + data),
//     u32 seek info (version 2: low word = entry count, high word = frames per entry),
//     then u16 seek table (one entry per frame: its size in bytes including the frame header),
//     then the first frames of the stream (the header region ends on a frame boundary).
//   source data (at streamDataOffset + source data offset): the remaining frames.
//   frame: u16 0x9999, u16 size (0xFFFF: the real u16 size follows and the header is 8 bytes), payload.
//   payload: for each 2-channel stream (the last one may be mono) one block, each block 32-bit aligned:
//     2 bits skipped, then per channel two Bink floats and the band/coefficient data.
//   Each frame yields (frame_len - frame_len / 16) samples per channel; frame_len is 2048 for rates of
//   44100 and up, 1024 for 22050 and up, 512 below.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace r2audio {

struct BinkInfo {
    uint32_t channels = 0;
    uint32_t sampleRate = 0;
    uint32_t sampleCount = 0;      // frames (samples per channel) in the source
    uint32_t frameCount = 0;       // coded frames
    uint32_t samplesPerFrame = 0;  // new samples per channel per coded frame
};

class BinkAudio {
public:
    // Reads `bytes` bytes at `offset` from the source's data region (relative to its start).
    using ReadFn = std::function<bool(uint64_t offset, void* destination, size_t bytes)>;

    BinkAudio();
    ~BinkAudio();
    BinkAudio(const BinkAudio&) = delete;
    BinkAudio& operator=(const BinkAudio&) = delete;

    // `header` is the whole source stream header region. Fails (error set) on any layout it does not understand.
    bool Open(const uint8_t* header, size_t headerSize, ReadFn readData, std::string* error);
    const BinkInfo& Info() const { return info_; }

    // Writes `count` sample frames starting at `first` as interleaved float (+-1.0 full scale), channel order
    // stream 0 (c0, c1), stream 1 (c2, c3), ... Positions past the end are zero filled. Sequential reads are
    // decoded once; a read that does not continue the previous one is resynchronised with one priming frame.
    bool Read(uint64_t first, size_t count, float* interleaved, std::string* error);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    BinkInfo info_;
};

}  // namespace r2audio
