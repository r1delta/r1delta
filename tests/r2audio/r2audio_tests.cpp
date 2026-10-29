// R2 sound support tests. Pure checks always run; checks that read the player's Titanfall 2 install run only when it is found
// (R1DELTA_TITANFALL2_PATH, else D:\Titanfall2) and are reported as skipped otherwise. No R2 data is stored in this file: the
// expected values are derived numbers (a few decoded samples, an RMS, event parameters).
#include "engine/r2port/audio/bink_audio.h"
#include "engine/r2port/audio/r2_audio_source.h"
#include "engine/r2port/audio/r2_sound_bank.h"
#include "engine/r2port/audio/r2_sound_scripts.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace r2audio;

namespace {
int g_failures = 0, g_skipped = 0;
void Check(bool ok, const char* what) {
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}
std::filesystem::path FindInstall() {
    char env[MAX_PATH]{};
    if (GetEnvironmentVariableA("R1DELTA_TITANFALL2_PATH", env, sizeof(env)) > 0 && std::filesystem::exists(std::filesystem::path(env) / "r2" / "sound" / "general.mbnk"))
        return env;
    if (std::filesystem::exists("D:\\Titanfall2\\r2\\sound\\general.mbnk")) return "D:\\Titanfall2";
    return {};
}

void WaveNames() {
    std::string name;
    Check(ParseWaveName("sound/r2port/some_source_1ch_v1_01_lp.wav", name) && name == "some_source_1ch_v1_01_lp", "wave name parses");
    Check(MakeWaveName("abc") == "r2port/abc.wav", "wave name builds");
    Check(!ParseWaveName("sound/weapons/abc.wav", name), "other paths are not R2 waves");
    Check(!ParseWaveName("sound/r2port/dir/abc.wav", name), "nested paths are rejected");
    Check(!ParseWaveName("sound/r2port/.wav", name), "empty names are rejected");
}

void DecoderRejectsBadHeaders() {
    BinkAudio bink;
    std::string error;
    std::vector<uint8_t> header(24, 0);
    auto read = [](uint64_t, void*, size_t) { return false; };
    Check(!bink.Open(header.data(), 8, read, &error), "short header rejected");
    Check(!bink.Open(header.data(), header.size(), read, &error), "bad magic rejected");
    std::memcpy(header.data(), "1FCB", 4);
    header[4] = 2; header[5] = 2; header[6] = 0x44; header[7] = 0xAC;  // 44100 Hz
    header[8] = 100;                                                    // sample count
    header[14] = 1;                                                     // maxCompSpaceNeeded high word set: Bink Audio 2
    Check(!bink.Open(header.data(), header.size(), read, &error), "Bink Audio 2 rejected");
    header[14] = 0;
    BinkAudio good;
    Check(good.Open(header.data(), header.size(), read, &error), "plausible empty-table header opens");
    std::vector<float> out(100 * 2);
    Check(!good.Read(0, 100, out.data(), &error), "a read with no stream data fails instead of inventing audio");
}

// The sound rewrite of an entity partition must keep every original entity byte for byte: only the `soundscape` key of
// trigger_soundscape blocks and the appended env_soundscape_triggerable blocks may differ, and a terminating NUL stays last
// (anything after it is cut off by the engine, which also drops the partitions that follow). Reads the entity partitions
// extracted from the player's install (R1DELTA_R2_ENT_DIR = <dir>/<map>/maps/<map>_<part>.ent), skipped when unset.
std::vector<std::string> SplitEntityBlocks(const std::string& text) {
    std::vector<std::string> blocks;
    size_t pos = 0;
    while ((pos = text.find("{\n", pos)) != std::string::npos) {
        const size_t end = text.find("\n}\n", pos);
        if (end == std::string::npos) break;
        blocks.push_back(text.substr(pos, end + 3 - pos));
        pos = end + 3;
    }
    return blocks;
}

std::string ClassnameOf(const std::string& block) {
    const size_t at = block.find("\"classname\" \"");
    if (at == std::string::npos) return {};
    const size_t start = at + 13;
    return block.substr(start, block.find('"', start) - start);
}

void AllMapEntityPartitions(const SoundBank& bank) {
    char dir[MAX_PATH]{};
    if (GetEnvironmentVariableA("R1DELTA_R2_ENT_DIR", dir, sizeof(dir)) == 0) {
        std::printf("SKIPPED: R1DELTA_R2_ENT_DIR not set (extracted entity partitions of the 20 maps)\n");
        ++g_skipped;
        return;
    }
    size_t maps = 0, partitions = 0, wiredTotal = 0;
    for (const std::string& map : EnabledSoundMaps()) {
        const std::filesystem::path mapsDir = std::filesystem::path(dir) / map / "maps";
        if (!std::filesystem::exists(mapsDir)) continue;
        ++maps;
        for (const auto& entry : std::filesystem::directory_iterator(mapsDir)) {
            const std::string name = entry.path().filename().string();
            if (entry.path().extension() != ".ent") continue;
            FILE* f = std::fopen(entry.path().string().c_str(), "rb");
            if (!f) continue;
            std::string input;
            char buf[65536];
            size_t n;
            while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) input.append(buf, n);
            std::fclose(f);
            ++partitions;
            const size_t inputNuls = std::count(input.begin(), input.end(), '\0');
            std::uint64_t wired = 0;
            const std::string out = RewriteSoundscapeTriggers(bank, input, map, &wired);
            wiredTotal += wired;
            const std::string label = map + "/" + name;
            const size_t outNuls = std::count(out.begin(), out.end(), '\0');
            Check(outNuls == inputNuls, (label + ": NUL count unchanged").c_str());
            if (inputNuls) Check(out.back() == '\0', (label + ": terminator is still the last byte").c_str());
            const std::string in = input.substr(0, input.find('\0')), rewritten = out.substr(0, out.find('\0'));
            const auto inBlocks = SplitEntityBlocks(in), outBlocks = SplitEntityBlocks(rewritten);
            Check(outBlocks.size() >= inBlocks.size(), (label + ": no entity lost").c_str());
            bool identical = outBlocks.size() >= inBlocks.size();
            size_t soundscapeKeys = 0;
            for (size_t i = 0; identical && i < inBlocks.size(); ++i) {
                std::string block = outBlocks[i];
                const size_t key = block.find("\"soundscape\" \"r2");
                if (key != std::string::npos && inBlocks[i].find("\"soundscape\"") == std::string::npos) {
                    const size_t lineEnd = block.find('\n', key);
                    block.erase(key, lineEnd + 1 - key);
                    ++soundscapeKeys;
                }
                identical = block == inBlocks[i];
            }
            Check(identical, (label + ": original entities are byte-identical apart from the added soundscape key").c_str());
            Check(soundscapeKeys == wired, (label + ": added keys equal the reported edits").c_str());
            for (size_t i = inBlocks.size(); i < outBlocks.size(); ++i)
                Check(ClassnameOf(outBlocks[i]) == "env_soundscape_triggerable", (label + ": only env_soundscape_triggerable is appended").c_str());
            if (!wired) Check(out == input, (label + ": untouched when nothing is wired").c_str());
        }
    }
    Check(maps == 20, "all 20 maps' partitions were read");
    Check(wiredTotal > 0 && partitions >= 20, "triggers were wired across the maps");
}

void BankAndDecode(const std::filesystem::path& install) {
    SetInstallDirectory(install);
    std::string error;
    const SoundBank* bank = SharedBank(&error);
    Check(bank != nullptr, "bank opens");
    if (!bank) return;
    std::vector<PlayAction> plays;
    // R1 ported event that exists in the R2 bank with an R1 twin: radius 3000 in R1's alias, pitch lo,hi 88,105, loop forever.
    Check(bank->ResolveEvent("Amb_Airbase_Emit_Turbine", plays) && !plays.empty(), "legacy event resolves");
    if (!plays.empty()) {
        Check(std::floor(plays[0].radius) == 3000.0f, "attenuation graph end equals R1 radius 3000");
        const double lo = 100.0 * std::pow(2.0, (plays[0].pitchSt - plays[0].pitchRandomSt) / 12.0);
        const double hi = 100.0 * std::pow(2.0, (plays[0].pitchSt + plays[0].pitchRandomSt) / 12.0);
        Check(std::lround(lo) == 88 && std::lround(hi) == 105, "pitch formula reproduces R1 alias 88,105");
        Check(plays[0].loopCount != 1, "loop count marks the turbine as looping");
    }
    SourceRecord record;
    Check(bank->FindSource("homestead_emit_runningstream_1ch_v1_01_lp", record), "source found by lower case name");
    Check(record.channels == 1 && record.sampleCount == 1749120, "source parameters");

    std::string wave = "sound/r2port/homestead_emit_runningstream_1ch_v1_01_lp.wav";
    auto source = WaveSource::Open(wave, &error);
    Check(source != nullptr, "wave opens");
    if (!source) return;
    Check(source->Info().channels == 1 && source->Info().frames == 1749120 && source->Info().loop, "wave info");
    std::vector<float> pcm(4096);
    Check(source->Read(0, 4096, pcm.data(), &error), "first block decodes");
    // Reference values from the FFmpeg-verified decode (+-1 LSB: float rounding of the last bit).
    static const int head[8] = {2614, 2309, 2040, 2012, 2101, 2153, 2210, 2347};
    static const int tail[8] = {-1781, -1636, -1566, -1468, -1246, -1123, -1257, -1394};
    bool matches = true;
    for (int i = 0; i < 8; ++i) {
        matches = matches && std::abs(std::lround(pcm[i] * 32768.0f) - head[i]) <= 1;
        matches = matches && std::abs(std::lround(pcm[4088 + i] * 32768.0f) - tail[i]) <= 1;
    }
    double sum = 0;
    for (float v : pcm) sum += static_cast<double>(v) * v;
    Check(matches && std::fabs(std::sqrt(sum / pcm.size()) - 0.069377678) < 1e-6, "decoded samples match the reference decode (FFmpeg-verified)");
    // seeking must reproduce the sequential decode
    std::vector<float> a(3000), b(3000);
    Check(source->Read(500000, 3000, a.data(), &error), "seek read");
    Check(source->Read(499000, 1000, b.data(), &error) && source->Read(500000, 3000, b.data(), &error), "seek read again");
    Check(std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0, "seek is deterministic");
    // 4 channel sources are presented as 6
    auto quad = WaveSource::Open("sound/r2port/homestead_amb_ext_global_4ch_v1_01_lp.wav", &error);
    Check(quad && quad->Info().channels == 6, "4 channel source maps to 6");
    if (quad) {
        std::vector<float> six(6 * 2048);
        Check(quad->Read(100000, 2048, six.data(), &error), "4 channel block decodes");
        double centre = 0;
        for (size_t i = 0; i < 2048; ++i) centre += std::fabs(six[i * 6 + 2]) + std::fabs(six[i * 6 + 3]);
        Check(centre == 0.0, "centre and LFE are empty for 4 channel sources");
    }

    // Alias text: every R2-only name resolves, no alias is defined twice, R1 names are skipped.
    SetAliasCapacityOk(true);
    AliasReport report;
    const std::string text = BuildAliasScript(*bank, EnabledSoundMaps(), &report);
    Check(report.unresolved == 0 && report.aliases > 200 && report.skippedR1 == 11, "alias script covers every R2-only name");
    Check(text.find("\"amb_colony_fire_small\"") == std::string::npos, "R1 aliases are not redefined");
    Check(text.find("r2port/") != std::string::npos && text.find("__g") == std::string::npos, "waves use plain r2port names");

    // Trigger rewrite on synthetic text: wired once, idempotent, env appended with the trigger origin.
    const char* entities =
        "ENTITIES01\n{\n\"origin\" \"1 2 3\"\n\"environment\" \"Base_Small_Room\"\n\"play_event\" \"glitch_amb_int_room_tone\"\n\"classname\" \"trigger_soundscape\"\n}\n"
        "{\n\"origin\" \"4 5 6\"\n\"environment\" \"Base_Exterior\"\n\"classname\" \"trigger_soundscape\"\n}\n"
        "{\n\"origin\" \"7 8 9\"\n\"soundscape\" \"existing\"\n\"classname\" \"trigger_soundscape\"\n}\n";
    std::uint64_t changed = 0;
    const std::string once = RewriteSoundscapeTriggers(*bank, entities, "mp_glitch", &changed);
    Check(changed == 2, "two triggers without a soundscape are wired");
    Check(once.find("\"soundscape\" \"r2env_r2ss_glitch_amb_int_room_tone\"") != std::string::npos, "room tone trigger points at its env");
    Check(once.find("\"soundscape\" \"r2env_r2ss_dsp\"") != std::string::npos, "environment-only trigger gets the DSP soundscape");
    Check(once.find("\"origin\" \"1 2 3\"\n\"soundscape\" \"r2ss_glitch_amb_int_room_tone\"") != std::string::npos, "env uses the first trigger origin");
    Check(once.find("\"soundscape\" \"existing\"") != std::string::npos && once.find("r2env_existing") == std::string::npos, "existing soundscape keys are untouched");
    std::uint64_t again = 0;
    Check(RewriteSoundscapeTriggers(*bank, once, "mp_glitch", &again) == once && again == 0, "rewrite is idempotent");
    Check(RewriteSoundscapeTriggers(*bank, entities, "mp_not_a_map", &changed) == entities && changed == 0, "other maps are untouched");
    // Partitions end with a NUL; everything appended must stay before it or the partitions after _snd.ent are cut off.
    const std::string withNul = entities + std::string(1, '\0');
    const std::string rewrittenNul = RewriteSoundscapeTriggers(*bank, withNul, "mp_glitch", &changed);
    Check(!rewrittenNul.empty() && rewrittenNul.back() == '\0' && rewrittenNul.find('\0') == rewrittenNul.size() - 1 && changed == 2, "partition NUL stays the last byte, nothing after it");
    const std::string soundscapes = BuildSoundscapeScript(*bank, "mp_glitch");
    Check(soundscapes.find("\"r2ss_glitch_amb_int_room_tone\"") != std::string::npos && soundscapes.find("\"r2ss_dsp\"") != std::string::npos, "soundscape file defines what the rewrite points at");
    AllMapEntityPartitions(*bank);
}
}  // namespace

int main() {
    WaveNames();
    DecoderRejectsBadHeaders();
    const std::filesystem::path install = FindInstall();
    if (install.empty()) {
        std::printf("SKIPPED: no Titanfall 2 install (set R1DELTA_TITANFALL2_PATH) for the bank and decode checks\n");
        ++g_skipped;
    } else {
        BankAndDecode(install);
    }
    std::printf("%s (%d failures, %d skipped groups)\n", g_failures ? "FAILED" : "OK", g_failures, g_skipped);
    return g_failures ? 1 : 0;
}
