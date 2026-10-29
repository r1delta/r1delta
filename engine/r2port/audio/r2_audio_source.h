// Runtime source for the generated R1 wave names `sound/r2port/<source>.wav`: decodes the Titanfall 2 Bink
// Audio stream of <source> straight from the player's general_stream*.mstr on demand. Nothing is cached on disk.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace r2audio {

class SoundBank;

// The R2 install directory (the folder that holds r2/sound). Set once by the engine; empty = no R2 install.
void SetInstallDirectory(const std::filesystem::path& installDirectory);
std::filesystem::path InstallDirectory();

// The lazily opened bank, or null when there is no install or the bank is not the expected build.
const SoundBank* SharedBank(std::string* error = nullptr);

// One line for the console: waves opened, decoded sample frames and read failures since start.
std::string StatsLine();

struct WaveInfo {
    uint32_t sampleRate = 0;
    uint32_t channels = 0;   // channel count R1 sees: 1, 2 or 6 (4 channel sources are laid out as FL FR - - BL BR)
    uint64_t frames = 0;     // sample frames in the source rate, 
    bool loop = true;
};

class WaveSource {
public:
    ~WaveSource();
    // `canonicalPath` is the lower case R1 path ("sound/r2port/....wav"). Null when the name is not an R2 wave or is unusable.
    static std::unique_ptr<WaveSource> Open(std::string_view canonicalPath, std::string* error);
    const WaveInfo& Info() const { return info_; }
    // Interleaved float, WaveInfo::channels wide, +-1.0 full scale.
    bool Read(uint64_t first, size_t count, float* interleaved, std::string* error);

private:
    WaveSource();
    struct Impl;
    std::unique_ptr<Impl> impl_;
    WaveInfo info_;
};

}  // namespace r2audio
