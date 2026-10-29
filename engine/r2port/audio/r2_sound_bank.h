// Reader for the player's own Titanfall 2 sound bank (r2/sound/general.mbnk, Miles bank version 13) and its
// stream files (general_stream*.mstr). Layout facts only (field offsets observed in the real files and the
// public descriptions of the format); no Titanfall 2 data is embedded or copied.
//
//   bank header: u32 'KNBC', u32 version (13), u32 file size, u32 bank magic, then offsets at fixed positions
//     (sources 0x48, eventNames 0x60, eventData 0x68, strings 0x70, graphData 0x78, sourceSelectors 0x80) and counts
//     (sourceCount 0xA0, eventCount 0xA8, graphDataSize 0xB4), build tag at 0xBC.
//   source record (0x58 bytes): name offset (strings) @0x10, u16 rate @0x14, u8 channels @0x18, u32 header size @0x30,
//     u32 sample count @0x34, u64 stream header offset @0x38, u64 stream data offset @0x40, i16 language @0x50,
//     i16 patch @0x52 (patch N lives in general_stream_patch_N.mstr, 0 in general_stream.mstr).
//   .mstr header: u32 'CSTR', u16 version (2), u16 language, u32 streamDataOffset, i32 patch, u32 build tag.
//   play action fields follow the R2 start-action layout recovered from the Miles runtime by the SP tree tools
//   (tools/apex_sounds/r2_event_graph.py START table): VolumeRandomDb @8, PitchRandomSt @12, DelayRandomMs @16, LoopCount @56 u16,
//   SourceSelector @60, AdditionalVolumeDb @80, PitchSt @88, VolumeDb @92.
//   event: sorted (case-insensitive) {u32 name offset, u32 data offset} table; the data is a list of actions
//     (byte0 low nibble = type, high nibble = last flag, byte1 = size in dwords). Type 0 (play) holds the volume
//     (dB, f32 @0x5C), a distance attenuation graph offset (i32 @0x28) and a source selector offset (i32 @0x3C);
//     type 8 runs another event (name offset u32 @0x0A).
//   selector: byte0 == 1 is a leaf (u32 name offset of the source @0x20), byte0 == 0 is a list (u8 mode @2,
//     u8 count @3, u32 child offsets @8 relative to the root selector of the tree).
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace r2audio {

struct SourceRecord {
    std::string name;  // as stored in the bank
    uint32_t sampleRate = 0;
    uint32_t channels = 0;
    uint32_t sampleCount = 0;
    uint32_t headerSize = 0;
    uint64_t headerOffset = 0;
    uint64_t dataOffset = 0;
    int16_t language = -1;
    int16_t patch = 0;
};

// One play action of an event.
struct PlayAction {
    std::vector<std::string> waves;  // source names, selector leaves in order
    float volumeDb = 0.0f;
    float addVolumeDb = 0.0f;  // AdditionalVolumeDb @0x50, added to volumeDb
    float pitchSt = 0.0f;      // PitchSt @0x58 (semitones)
    float pitchRandomSt = 0.0f;  // PitchRandomSt @0x0C (+- semitones)
    float radius = 0.0f;       // distance (units) at which the attenuation graph reaches -40 dB, 0 when it never does
    uint32_t loopCount = 0;    // LoopCount @0x38: 1 = play once, 0 = loop forever (other values loop)
};

class SoundBank {
public:
    SoundBank();
    ~SoundBank();
    SoundBank(const SoundBank&) = delete;
    SoundBank& operator=(const SoundBank&) = delete;

    // `soundDirectory` is <R2 install>/r2/sound.
    bool Open(const std::filesystem::path& soundDirectory, std::string* error);
    bool IsOpen() const;

    // Follows run-event actions (depth limited). False when the event does not exist or cannot be parsed.
    bool ResolveEvent(std::string_view name, std::vector<PlayAction>& plays) const;
    // `lowerName` must be lower case ASCII.
    bool FindSource(std::string_view lowerName, SourceRecord& record) const;
    // Path of the .mstr that holds the source.
    std::filesystem::path StreamPath(const SourceRecord& record) const;
    // streamDataOffset of that .mstr (read once, cached); 0 on failure.
    uint32_t StreamDataOffset(int patch) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace r2audio
