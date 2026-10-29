#include "r2_sound_scripts.h"

#include "r2_sound_bank.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <unordered_map>

namespace r2audio {
namespace {

#include "r2_sound_requests.inc"

constexpr size_t kNameCount = sizeof(kR2SoundNames) / sizeof(kR2SoundNames[0]);
constexpr size_t kMapCount = sizeof(kR2SoundMaps) / sizeof(kR2SoundMaps[0]);
constexpr char kEmitterMix[] = "ambient,SFX,SFX_AMB,SFX_AMB_Emitters";
constexpr char kBedMix[] = "ambient,SFX,SFX_AMB,SFX_AMB_Beds";
constexpr char kDspSoundscape[] = "r2ss_dsp";
constexpr char kEnvPrefix[] = "r2env_";
constexpr char kSoundscapePrefix[] = "r2ss_";
constexpr char kNewline[] = "\r\n";

std::string Lower(std::string_view v) {
    std::string out(v);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    return out;
}

const std::unordered_map<std::string, size_t>& NameIndex() {
    static const std::unordered_map<std::string, size_t> index = [] {
        std::unordered_map<std::string, size_t> m;
        for (size_t i = 0; i < kNameCount; ++i) m.emplace(Lower(kR2SoundNames[i].name), i);
        return m;
    }();
    return index;
}

const R2SoundMap* FindMap(std::string_view map) {
    for (size_t i = 0; i < kMapCount; ++i)
        if (map == kR2SoundMaps[i].map) return &kR2SoundMaps[i];
    return nullptr;
}

std::string Number(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.4g", value);
    return buffer;
}

// R1 pitch is a percentage (lo,hi); R2 gives PitchSt +- PitchRandomSt semitones. Verified on every R1 ambient alias that
// also exists in the R2 bank (e.g. amb_airbase_emit_console: -0.4 +- 3.5 st = 80,120).
std::string PitchText(const PlayAction& play) {
    auto percent = [](double semitones) { return static_cast<int>(std::lround(100.0 * std::pow(2.0, semitones / 12.0))); };
    const int lo = percent(play.pitchSt - play.pitchRandomSt), hi = percent(play.pitchSt + play.pitchRandomSt);
    if (lo == 100 && hi == 100) return {};
    return lo == hi ? std::to_string(lo) : std::to_string(lo) + "," + std::to_string(hi);
}

// Alias text for one event; empty when the bank cannot resolve it.
std::string AliasText(const SoundBank& bank, const R2SoundName& request, unsigned& waveCount) {
    std::vector<PlayAction> plays;
    if (!bank.ResolveEvent(request.name, plays)) return {};
    const bool bed = request.kind == 1;
    const bool layered = plays.size() > 1;

    std::string text = "\"" + std::string(request.name) + "\"" + kNewline + "{" + kNewline;
    text += std::string("\t\"mixgroup\"\t\"") + (bed ? kBedMix : kEmitterMix) + "\"" + kNewline;
    // LoopCount 1 plays the event once; anything else (0 = forever) loops. Room tones always loop.
    bool loops = bed;
    for (const PlayAction& play : plays) loops = loops || play.loopCount != 1;
    std::string flags = bed ? "omni" : "mobile";
    if (loops) flags += ",loop";
    if (layered) flags += ",layered";
    text += "\t\"flags\"\t\"" + flags + "\"" + kNewline;
    float radius = 0.0f;
    if (!bed) {
        radius = plays[0].radius;
        for (const PlayAction& play : plays) radius = std::max(radius, play.radius);
        radius = radius > 0.0f ? std::min(std::max(radius, 64.0f), 16384.0f) : 1500.0f;
    }
    // R2's attenuation graph ends at <radius>.5 for every R1-ported event; R1 writes the integer.
    // Layered aliases (several simultaneous play actions) carry their distances per rndwave instead, as R1's own do.
    if (!layered) text += "\t\"radius\"\t\"" + std::to_string(static_cast<int>(std::floor(radius))) + "\"" + kNewline;
    // R2 event volumes are dB at the end of R2's mixer chain; R1 aliases are linear and run through R1's mixgroup chain
    // (all 0.16 x SFX 0.54 x SFX_AMB 1.8 x Beds 1.3). Every R1 ambient alias that also exists in the R2 bank (the legacy R1
    // maps are in it) differs from its R2 event by exactly 11.1 dB of total gain, so
    //   aliasVolume = 10^((dB + 11.1) / 20) / mixgroupChain.
    auto linear = [&](float db) {
        const double chain = bed ? 0.16 * 0.54 * 1.8 * 1.3 : 0.16 * 0.54 * 1.8;
        return std::min(40.0, std::max(0.01, std::pow(10.0, (db + 11.1) / 20.0) / chain));
    };
    text += "\t\"volume\"\t\"" + Number(layered ? 1.0 : linear(plays[0].volumeDb + plays[0].addVolumeDb)) + "\"" + kNewline;
    if (!layered) {
        const std::string pitch = PitchText(plays[0]);
        if (!pitch.empty()) text += "\t\"pitch\"\t\"" + pitch + "\"" + kNewline;
    }
    for (const PlayAction& play : plays) {
        text += std::string("\t\"rndwave\"") + kNewline + "\t{" + kNewline;
        if (layered) {
            const float playRadius = play.radius > 0.0f ? std::min(std::max(play.radius, 64.0f), 16384.0f) : radius;
            text += "\t\t\"volume\"\t\"" + Number(linear(play.volumeDb + play.addVolumeDb)) + "\"" + kNewline;
            text += std::string("\t\t\"startDistance\"\t\"0\"") + kNewline + "\t\t\"peakDistance\"\t\"0\"" + kNewline +
                    "\t\t\"falloffStart\"\t\"0\"" + kNewline + "\t\t\"falloffEnd\"\t\"" +
                    std::to_string(static_cast<int>(std::floor(playRadius))) + "\"" + kNewline + "\t\t\"startCurve\"\t\"0\"" + kNewline +
                    "\t\t\"falloffCurve\"\t\"0\"" + kNewline;
        }
        if (layered) {
            const std::string pitch = PitchText(play);
            if (!pitch.empty()) text += "\t\t\"pitch\"\t\"" + pitch + "\"" + kNewline;
        }
        text += std::string("\t\t\"limit\"\t\"") + (play.waves.size() > 1 ? "2" : "1") + "\"" + kNewline;
        for (const std::string& wave : play.waves) {
            text += "\t\t\"wave\"\t\"" + MakeWaveName(Lower(wave)) + "\"" + kNewline;
            ++waveCount;
        }
        text += std::string("\t}") + kNewline;
    }
    text += std::string("}") + kNewline + kNewline;
    return text;
}

std::set<std::string> SortedUniqueNames(const std::vector<std::string>& maps, bool onlyR2) {
    std::set<std::string> lowered;  // lower-case key, preserve the first spelling via the name index
    for (const std::string& map : maps) {
        const R2SoundMap* entry = FindMap(map);
        if (!entry) continue;
        for (unsigned i = 0; i < entry->count; ++i) {
            const R2SoundName& n = kR2SoundNames[entry->names[i]];
            if (onlyR2 && n.r1) continue;
            lowered.insert(Lower(n.name));
        }
    }
    return lowered;
}

// Names whose alias the generated script defines: R2-only names of the enabled maps that the bank resolves.
// (Declared here, defined after EventDefined below.)
bool AliasDefined(const SoundBank& bank, const R2SoundName& request);

}  // namespace

std::string MakeWaveName(std::string_view lowerSourceName) {
    return "r2port/" + std::string(lowerSourceName) + ".wav";
}

bool ParseWaveName(std::string_view path, std::string& lowerSourceName) {
    constexpr std::string_view prefix = "sound/r2port/", suffix = ".wav";
    if (path.size() <= prefix.size() + suffix.size() || path.compare(0, prefix.size(), prefix) != 0 ||
        path.compare(path.size() - suffix.size(), suffix.size(), suffix) != 0)
        return false;
    std::string name(path.substr(prefix.size(), path.size() - prefix.size() - suffix.size()));
    if (name.empty() || name.find('/') != std::string::npos) return false;
    lowerSourceName = std::move(name);
    return true;
}

namespace {
std::atomic<bool> g_capacityOk{false};
bool HasEnvironmentOverride() {
    char env[8]{};
    return GetEnvironmentVariableA("R1DELTA_R2PORT_SOUND_MAPS", env, sizeof(env)) > 0;
}
}  // namespace

void SetAliasCapacityOk(bool ok) { g_capacityOk = ok; }

bool SoundAliasesAllowed() { return g_capacityOk.load() || HasEnvironmentOverride(); }

std::vector<std::string> EnabledSoundMaps() {
    char env[1024]{};
    const DWORD length = GetEnvironmentVariableA("R1DELTA_R2PORT_SOUND_MAPS", env, sizeof(env));
    std::vector<std::string> maps;
    if (length > 0 && length < sizeof(env)) {
        const std::string value = Lower(env);
        if (value == "all") {
            for (size_t i = 0; i < kMapCount; ++i) maps.emplace_back(kR2SoundMaps[i].map);
            return maps;
        }
        size_t start = 0;
        while (start <= value.size()) {
            size_t end = value.find(',', start);
            if (end == std::string::npos) end = value.size();
            const std::string item = value.substr(start, end - start);
            if (FindMap(item)) maps.push_back(item);
            start = end + 1;
        }
        return maps;
    }
    for (size_t i = 0; i < kMapCount; ++i) maps.emplace_back(kR2SoundMaps[i].map);
    return maps;
}

bool EventDefined(const SoundBank& bank, std::string_view name) {
    std::vector<PlayAction> plays;
    return bank.IsOpen() && bank.ResolveEvent(name, plays);
}

namespace {
bool AliasDefined(const SoundBank& bank, const R2SoundName& request) {
    static const std::set<std::string> enabled = SoundAliasesAllowed() ? SortedUniqueNames(EnabledSoundMaps(), true) : std::set<std::string>();
    return !request.r1 && enabled.count(Lower(request.name)) != 0 && EventDefined(bank, request.name);
}
}  // namespace

std::string BuildAliasScript(const SoundBank& bank, const std::vector<std::string>& maps, AliasReport* report) {
    AliasReport local;
    AliasReport& r = report ? *report : local;
    r = AliasReport{};
    std::string text;
    if (!bank.IsOpen() || !SoundAliasesAllowed()) return text;
    const std::set<std::string> names = SortedUniqueNames(maps, false);
    for (const std::string& lower : names) {
        const auto found = NameIndex().find(lower);
        if (found == NameIndex().end()) continue;
        const R2SoundName& request = kR2SoundNames[found->second];
        if (request.r1) {
            ++r.skippedR1;
            continue;
        }
        const std::string alias = AliasText(bank, request, r.waves);
        if (alias.empty()) {
            ++r.unresolved;
            r.unresolvedNames.push_back(request.name);
            continue;
        }
        text += alias;
        ++r.aliases;
    }
    if (!text.empty())
        text = std::string("// Generated by R1Delta from the player's own Titanfall 2 sound bank. Do not edit.") + kNewline + kNewline + text;
    return text;
}

std::string BuildSoundscapeScript(const SoundBank& bank, std::string_view map) {
    const R2SoundMap* entry = FindMap(map);
    if (!entry || !bank.IsOpen()) return {};
    std::string text = std::string("// Generated by R1Delta for the converted R2 map ") + std::string(map) + kNewline + kNewline;
    text += std::string("\"") + kDspSoundscape + "\"" + kNewline + "{" + kNewline + "\t\"dsp\"\t\"1\"" + kNewline +
            "\t\"dsp_volume\"\t\".15\"" + kNewline + "}" + kNewline + kNewline;
    std::set<std::string> seen;
    for (unsigned i = 0; i < entry->count; ++i) {
        const R2SoundName& n = kR2SoundNames[entry->names[i]];
        if (n.kind != 1 || !seen.insert(Lower(n.name)).second) continue;
        if (!AliasDefined(bank, n)) continue;
        text += std::string("\"") + kSoundscapePrefix + Lower(n.name) + "\"" + kNewline + "{" + kNewline + "\t\"dsp\"\t\"1\"" + kNewline +
                "\t\"dsp_volume\"\t\".15\"" + kNewline + "\t\"playlooping\"\t\"" + n.name + "\"" + kNewline + "}" + kNewline + kNewline;
    }
    return text;
}

namespace {

struct Block {
    size_t begin, end;  // [begin, end) covers '{' .. '}'
};

// Top level blocks of an entity text; quotes may contain braces.
bool SplitBlocks(std::string_view text, std::vector<Block>& blocks) {
    size_t i = 0;
    bool inQuote = false;
    size_t open = std::string::npos;
    for (; i < text.size(); ++i) {
        const char c = text[i];
        if (inQuote) {
            if (c == '"') inQuote = false;
            else if (c == '\n') return false;
            continue;
        }
        if (c == '"') {
            inQuote = true;
        } else if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            while (i < text.size() && text[i] != '\n') ++i;
        } else if (c == '{') {
            if (open != std::string::npos) return false;
            open = i;
        } else if (c == '}') {
            if (open == std::string::npos) return false;
            blocks.push_back({open, i + 1});
            open = std::string::npos;
        }
    }
    return open == std::string::npos && !inQuote;
}

struct KeyValue {
    std::string key, value;
};

std::vector<KeyValue> ParseBlock(std::string_view block) {
    std::vector<KeyValue> out;
    size_t i = 0;
    auto quoted = [&](std::string& value) {
        while (i < block.size() && block[i] != '"') ++i;
        if (i >= block.size()) return false;
        const size_t start = ++i;
        while (i < block.size() && block[i] != '"') ++i;
        if (i >= block.size()) return false;
        value.assign(block.substr(start, i - start));
        ++i;
        return true;
    };
    while (i < block.size()) {
        KeyValue kv;
        if (!quoted(kv.key) || !quoted(kv.value)) break;
        out.push_back(std::move(kv));
    }
    return out;
}

const std::string* Find(const std::vector<KeyValue>& kvs, std::string_view key) {
    for (const KeyValue& kv : kvs)
        if (kv.key == key) return &kv.value;
    return nullptr;
}

}  // namespace

std::string RewriteSoundscapeTriggers(const SoundBank& bank, std::string_view text, std::string_view map, std::uint64_t* changed) {
    if (changed) *changed = 0;
    if (!bank.IsOpen() || !FindMap(map)) return std::string(text);
    std::vector<Block> blocks;
    if (!SplitBlocks(text, blocks)) return std::string(text);

    struct Edit {
        size_t insertAt;
        std::string line;
    };
    std::vector<Edit> edits;
    std::map<std::string, std::string> envs;      // soundscape name -> origin of the first trigger that uses it
    std::set<std::string> existingTargets;
    for (const Block& b : blocks) {
        const auto kvs = ParseBlock(text.substr(b.begin, b.end - b.begin));
        const std::string* cls = Find(kvs, "classname");
        const std::string* target = Find(kvs, "targetname");
        if (cls && *cls == "env_soundscape_triggerable" && target) existingTargets.insert(*target);
    }
    const char* newline = text.find("\r\n") != std::string_view::npos ? "\r\n" : "\n";
    for (const Block& b : blocks) {
        const auto kvs = ParseBlock(text.substr(b.begin, b.end - b.begin));
        const std::string* cls = Find(kvs, "classname");
        if (!cls || *cls != "trigger_soundscape" || Find(kvs, "soundscape")) continue;
        std::string soundscape = kDspSoundscape;
        if (const std::string* event = Find(kvs, "play_event")) {
            const auto found = NameIndex().find(Lower(*event));
            if (found != NameIndex().end() && kR2SoundNames[found->second].kind == 1) {
                const R2SoundName& n = kR2SoundNames[found->second];
                if (n.r1)
                    soundscape = n.name;  // R1 has a soundscape of this name
                else if (AliasDefined(bank, n))
                    soundscape = kSoundscapePrefix + Lower(n.name);
            }
        }
        const std::string* origin = Find(kvs, "origin");
        envs.emplace(soundscape, origin ? *origin : "0 0 0");
        // Insert before the closing brace, on its own line.
        std::string line = std::string("\"soundscape\" \"") + kEnvPrefix + soundscape + "\"" + newline;
        size_t at = b.end - 1;
        edits.push_back({at, std::move(line)});
    }
    if (edits.empty()) return std::string(text);

    std::string out;
    out.reserve(text.size() + edits.size() * 48 + envs.size() * 220);
    size_t cursor = 0;
    for (const Edit& e : edits) {
        out.append(text.substr(cursor, e.insertAt - cursor));
        // keep the closing brace on its own line: if the character before it is not a newline add one first
        if (e.insertAt > 0 && text[e.insertAt - 1] != '\n') out += newline;
        out += e.line;
        cursor = e.insertAt;
    }
    out.append(text.substr(cursor));
    if (!out.empty() && out.back() != '\n') out += newline;
    for (const auto& env : envs) {
        const std::string target = kEnvPrefix + env.first;
        if (existingTargets.count(target)) continue;
        out += std::string("{") + newline;
        out += "\"StartDisabled\" \"0\"" + std::string(newline);
        out += "\"requireLOS\" \"0\"" + std::string(newline);
        out += "\"radius\" \"128\"" + std::string(newline);
        out += "\"origin\" \"" + env.second + "\"" + newline;
        out += "\"soundscape\" \"" + env.first + "\"" + newline;
        out += "\"targetname\" \"" + target + "\"" + newline;
        out += "\"classname\" \"env_soundscape_triggerable\"" + std::string(newline);
        out += std::string("}") + newline;
    }
    if (changed) *changed = edits.size();
    return out;
}

}  // namespace r2audio
