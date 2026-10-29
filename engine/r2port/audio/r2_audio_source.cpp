#include "r2_audio_source.h"

#include "bink_audio.h"
#include "r2_sound_bank.h"
#include "r2_sound_scripts.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <vector>

namespace r2audio {
namespace {

std::mutex g_mutex;
std::filesystem::path g_install;
std::unique_ptr<SoundBank> g_bank;
bool g_bankTried = false;
std::string g_bankError;
std::atomic<uint64_t> g_opened{0}, g_framesRead{0}, g_failures{0};

}  // namespace

void SetInstallDirectory(const std::filesystem::path& installDirectory) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_install == installDirectory) return;
    g_install = installDirectory;
    g_bank.reset();
    g_bankTried = false;
    g_bankError.clear();
}

std::filesystem::path InstallDirectory() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_install;
}

std::string StatsLine() {
    char line[192];
    std::snprintf(line, sizeof(line), "R1Delta R2 audio: %llu waves opened, %llu sample frames decoded, %llu failures\n",
                  static_cast<unsigned long long>(g_opened.load()), static_cast<unsigned long long>(g_framesRead.load()),
                  static_cast<unsigned long long>(g_failures.load()));
    return line;
}

const SoundBank* SharedBank(std::string* error) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_bankTried) {
        g_bankTried = true;
        if (g_install.empty()) {
            g_bankError = "no Titanfall 2 install";
        } else {
            auto bank = std::make_unique<SoundBank>();
            if (bank->Open(g_install / "r2" / "sound", &g_bankError)) g_bank = std::move(bank);
        }
    }
    if (!g_bank && error) *error = g_bankError;
    return g_bank.get();
}

struct WaveSource::Impl {
    HANDLE file = INVALID_HANDLE_VALUE;
    uint64_t dataBase = 0;  // absolute offset of the source's data region in the .mstr
    BinkAudio bink;
    uint64_t sourceFrames = 0;
    unsigned binkChannels = 0;
    unsigned outChannels = 0;
    std::vector<float> scratch;

    ~Impl() {
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    }

    bool ReadAt(uint64_t offset, void* destination, size_t bytes) {
        auto* out = static_cast<uint8_t*>(destination);
        while (bytes) {
            OVERLAPPED overlapped{};
            overlapped.Offset = static_cast<DWORD>(offset & 0xFFFFFFFFu);
            overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32);
            DWORD got = 0;
            const DWORD want = static_cast<DWORD>(std::min<size_t>(bytes, 1u << 20));
            if (!ReadFile(file, out, want, &got, &overlapped) || got == 0) return false;
            out += got;
            offset += got;
            bytes -= got;
        }
        return true;
    }
};

WaveSource::WaveSource() = default;
WaveSource::~WaveSource() = default;

std::unique_ptr<WaveSource> WaveSource::Open(std::string_view canonicalPath, std::string* error) {
    auto fail = [&](const char* message) -> std::unique_ptr<WaveSource> {
        if (error) *error = message;
        return nullptr;
    };
    std::string sourceName;
    if (!ParseWaveName(canonicalPath, sourceName)) return fail("not an r2port wave name");
    const SoundBank* bank = SharedBank(error);
    if (!bank) return nullptr;
    SourceRecord record;
    if (!bank->FindSource(sourceName, record)) return fail("source is not in the R2 sound bank");
    const std::filesystem::path stream = bank->StreamPath(record);
    if (stream.empty()) return fail("source stream is localised or in an unknown patch");
    const uint32_t streamDataOffset = bank->StreamDataOffset(record.patch);
    if (!streamDataOffset) return fail("stream file header does not match the bank");
    if (record.headerSize < 24 || record.headerSize > (4u << 20)) return fail("bad stream header size");
    if (record.channels != 1 && record.channels != 2 && record.channels != 4 && record.channels != 6)
        return fail("unsupported channel count");

    std::unique_ptr<WaveSource> wave(new WaveSource());
    wave->impl_ = std::make_unique<Impl>();
    Impl& impl = *wave->impl_;
    impl.file = CreateFileW(stream.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                            FILE_FLAG_RANDOM_ACCESS, nullptr);
    if (impl.file == INVALID_HANDLE_VALUE) return fail("stream file could not be opened");
    impl.dataBase = static_cast<uint64_t>(streamDataOffset) + record.dataOffset;
    std::vector<uint8_t> header(record.headerSize);
    if (!impl.ReadAt(record.headerOffset, header.data(), header.size())) return fail("stream header read failed");
    Impl* raw = &impl;
    if (!impl.bink.Open(header.data(), header.size(),
                        [raw](uint64_t offset, void* destination, size_t bytes) { return raw->ReadAt(raw->dataBase + offset, destination, bytes); },
                        error))
        return nullptr;
    const BinkInfo& bink = impl.bink.Info();
    if (bink.channels != record.channels) return fail("stream channel count differs from the bank");
    impl.binkChannels = bink.channels;
    impl.outChannels = bink.channels == 4 ? 6 : bink.channels;
    impl.sourceFrames = bink.sampleCount;
    wave->info_.sampleRate = bink.sampleRate;
    wave->info_.channels = impl.outChannels;
    wave->info_.frames = impl.sourceFrames;
    wave->info_.loop = true;
    ++g_opened;
    return wave;
}

bool WaveSource::Read(uint64_t first, size_t count, float* out, std::string* error) {
    Impl& impl = *impl_;
    g_framesRead += count;
    struct Failure {
        bool ok = false;
        ~Failure() { if (!ok) ++g_failures; }
    } failure;
    std::fill(out, out + count * impl.outChannels, 0.0f);
    if (first >= impl.sourceFrames || !count) { failure.ok = true; return true; }  // past the end: silence
    const size_t usable = static_cast<size_t>(std::min<uint64_t>(count, impl.sourceFrames - first));
    if (impl.binkChannels == impl.outChannels) return failure.ok = impl.bink.Read(first, usable, out, error);
    // 4 channel sources: FL FR BL BR -> FL FR (empty) (empty) BL BR
    impl.scratch.resize(usable * impl.binkChannels);
    if (!impl.bink.Read(first, usable, impl.scratch.data(), error)) return false;
    static constexpr unsigned map[4] = {0, 1, 4, 5};
    for (size_t i = 0; i < usable; ++i)
        for (unsigned c = 0; c < 4; ++c) out[i * 6 + map[c]] = impl.scratch[i * 4 + c];
    failure.ok = true;
    return true;
}

}  // namespace r2audio
