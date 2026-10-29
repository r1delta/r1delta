#include "audio_cache.h"
#include "audio.h"
#include "audio_source.h"
#include "audio_hook_profile.h"
#include "load.h"
#include "cvar.h"
#include "filecache.h"
#include "filesystem.h"
#include <MinHook.h>
#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
std::uintptr_t s_engine = 0;
bool s_installed = false;
bool s_commands = false;
bool s_rescanOnInitialize = false;
// The native audio critical section is recursive. Protect the complete four
// borrowed-record consumers, not just the lookup that returns their pointer.
// The native mixer takes this same lock. The device lifecycle's sound_reboot
// detour owns it across stop/drain/destroy/reinitialize/manifest reload.
class AudioScope {
    CRITICAL_SECTION* section;
public:
    AudioScope() : section(reinterpret_cast<CRITICAL_SECTION*>(s_engine + 0x2017170)) { EnterCriticalSection(section); }
    ~AudioScope() { LeaveCriticalSection(section); }
};
using DurationFn = float (*)(const char*, std::uintptr_t);
using PrecacheFn = void (*)();
using DelayFn = float (*)(int, int, int, const float*, const float*, int, float);
using StartFn = std::int64_t (*)(int, int, unsigned, const float*, const float*, unsigned, int, float, float, void**);
DurationFn s_duration = nullptr;
PrecacheFn s_precache = nullptr;
DelayFn s_delay = nullptr;
StartFn s_start = nullptr;

R1AudioCacheRecord* Lookup(const char* name) { return FindR1AudioSource(name); }

// The generated R2 aliases (scripts/r2port_sounds.txt) are the last manifest file. The engine folds every loaded alias into
// the sound-script CRC it sends at connect and R1Delta's parity check compares the alias table, so an R2-installed client
// would mismatch every stock server. Both are restored right after that file: its aliases stay in the table (R2 map sessions
// have the same list at the same indexes on both ends) but are invisible to the CRC and to the parity fingerprint.
using ScriptLoadFn = std::int64_t (*)(const char*);
ScriptLoadFn s_scriptLoad = nullptr;
std::uint32_t s_hiddenAliases = 0, s_hiddenRanges = 0, s_hiddenStart = ~0u;
std::int64_t ScriptLoad(const char* filename) {
    auto* engine = reinterpret_cast<unsigned char*>(s_engine);
    auto& crc = *reinterpret_cast<std::uint32_t*>(engine + 0x1A0DBC0);
    const int aliases = *reinterpret_cast<const int*>(engine + 0x1A0DBC4);
    const int ranges = *reinterpret_cast<const int*>(engine + 0x1A0DBE0);
    if (aliases >= 0 && static_cast<std::uint32_t>(aliases) < s_hiddenStart) {  // the table was cleared for a reload
        s_hiddenAliases = s_hiddenRanges = 0;
        s_hiddenStart = ~0u;
    }
    const std::uint32_t crcBefore = crc;
    const std::int64_t result = s_scriptLoad(filename);
    if (filename && _stricmp(filename, "scripts/r2port_sounds.txt") == 0) {
        s_hiddenStart = static_cast<std::uint32_t>(aliases);
        s_hiddenAliases = static_cast<std::uint32_t>(*reinterpret_cast<const int*>(engine + 0x1A0DBC4) - aliases);
        s_hiddenRanges = static_cast<std::uint32_t>(*reinterpret_cast<const int*>(engine + 0x1A0DBE0) - ranges);
        crc = crcBefore;
    }
    return result;
}
bool Initialize() {
    if (!R1AudioReadHooksInstalled()) {
        Error("R1Delta audio: required native filesystem PCM read hook is not installed\n");
        return false;
    }
    void* filesystem = *reinterpret_cast<void**>(s_engine + 0x2EC3580);
    if (!filesystem) {
        Error("R1Delta audio: native GAME filesystem is not initialized\n");
        return false;
    }
    if (s_rescanOnInitialize) {
        // Run after the native shutdown/drain, not while old streams are live.
        // This uses the existing current-root publication barrier, not a second
        // directory scanner or the persistent-data schema reload wrapper.
        auto& files = FileCache::GetInstance();
        files.BeginAddonSearchPathUpdate();
        if (!files.EndAddonSearchPathUpdate()) {
            Error("R1Delta audio: filesystem override snapshot could not be rebuilt\n");
            return false;
        }
        InvalidateFileSystemNegativePathCache();
        s_rescanOnInitialize = false;
    }
    InitializeR1AudioSources(filesystem);
    // These native globals belong to the removed fixed-size serialized cache,
    // not the stream scheduler. The scheduler and its buffer ownership remain native.
    *reinterpret_cast<int*>(s_engine + 0x1A0DEB0) = 0;
    *reinterpret_cast<void**>(s_engine + 0x1FE1240) = nullptr;
    return true;
}
void Destroy() {
    // +114B0 reaches here only after +B1A0 stops sounds and drains all native
    // stream read callbacks, and after releasing the XAudio2 engine/voices.
    DestroyR1AudioSources();
}
float Duration(const char* name, std::uintptr_t context) { AudioScope lock; return s_duration(name, context); }
void Precache() { AudioScope lock; s_precache(); }
float Delay(int a, int b, int c, const float* d, const float* e, int f, float g) {
    AudioScope lock; return s_delay(a, b, c, d, e, f, g);
}
std::int64_t Start(int a, int b, unsigned c, const float* d, const float* e, unsigned f, int g, float h, float i, void** j) {
    AudioScope lock; return s_start(a, b, c, d, e, f, g, h, i, j);
}

// Diagnostic: runs a sound path through the same two entry points the engine hooks use (record lookup, then streamed PCM
// reads) and reports what comes out. Works without a native audio device.
//   r1delta_audio_probe <path under sound/, e.g. r2port/<source>.wav> [seconds]
void Probe(const CCommand& args) {
    if (args.ArgC() < 2) { Msg("usage: r1delta_audio_probe <wave path under sound/> [seconds]\n"); return; }
    const auto begin = std::chrono::steady_clock::now();
    R1AudioCacheRecord* record = FindR1AudioSource(args.Arg(1));
    if (!record) { Msg("r1delta_audio_probe: no record for '%s' (see the warning above)\n", args.Arg(1)); return; }
    const double seconds = args.ArgC() > 2 ? atof(args.Arg(2)) : 4.0;
    Msg("r1delta_audio_probe: %s channels=%d samples=%d duration=%.2fs loop=%d streamOffset=%llu lookup %.2f ms\n", record->filename,
        record->channels, record->sampleCount, record->duration, record->loop, static_cast<unsigned long long>(record->streamOffset),
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count());
    const std::size_t frameBytes = static_cast<std::size_t>(record->channels) * sizeof(std::int16_t);
    const std::uint64_t totalBytes = static_cast<std::uint64_t>(record->sampleCount) * sizeof(std::int16_t);
    std::vector<std::int16_t> pcm(4096 * record->channels);
    auto readRange = [&](const char* label, std::uint64_t firstFrame, std::uint64_t frames) {
        double peak = 0, sum = 0; std::uint64_t samples = 0;
        const auto t0 = std::chrono::steady_clock::now();
        for (std::uint64_t done = 0; done < frames;) {
            const std::uint64_t at = (firstFrame + done) * frameBytes;
            if (at >= totalBytes) break;
            const std::size_t want = static_cast<std::size_t>(std::min<std::uint64_t>({pcm.size() * sizeof(std::int16_t), (frames - done) * frameBytes, totalBytes - at}));
            std::size_t got = 0; std::string error;
            const R1AudioReadResult result = ReadR1AudioSource(args.Arg(1), "GAME", at, pcm.data(), want, got, error);
            if (result != R1AudioReadResult::Success) { Msg("r1delta_audio_probe: %s read failed at frame %llu: %s\n", label, static_cast<unsigned long long>(firstFrame + done), error.c_str()); return; }
            for (std::size_t i = 0; i < got / sizeof(std::int16_t); ++i) { const double v = std::abs(pcm[i]) / 32768.0; peak = std::max(peak, v); sum += v * v; ++samples; }
            done += got / frameBytes;
            if (!got) break;
        }
        Msg("r1delta_audio_probe: %s frames=%llu peak=%.3f rms=%.4f read %.1f ms\n", label, static_cast<unsigned long long>(samples / std::max(1, record->channels)), peak,
            samples ? std::sqrt(sum / samples) : 0.0, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    };
    const std::uint64_t totalFrames = totalBytes / frameBytes;
    const std::uint64_t window = static_cast<std::uint64_t>(seconds * 44100.0);
    readRange("start", 0, std::min(window, totalFrames));
    if (totalFrames > window * 3) readRange("middle", totalFrames / 2, window);
    if (totalFrames > window) readRange("end", totalFrames - std::min(window, totalFrames), window);
}
void Rebuild(const CCommand&) {
    if (!s_installed || GetR1DeltaEngineMode() != R1DeltaEngineMode::Client2015) return;
    s_rescanOnInitialize = true;
    // The exact native sound_reboot owner (+118C0) does shutdown, S_Init,
    // metadata init, streaming buffer init, then +A880 reloads game_sounds_manifest.
    // Its detour registers/unregisters notifications outside the audio lock.
    reinterpret_cast<void (*)()>(s_engine + 0x118C0)();
    Msg("R1Delta audio: stopped/drained sounds, discarded generated metadata, and reloaded game_sounds manifests; subsequent lookups rebuild from GAME assets\n");
}
void Stats(const CCommand&) {
    ReportR1AudioSources();
    // Native sound tables (engine.dll): aliases 0x1A0DBC4 (limit 18552), sound ranges 0x1A0DBE0 (limit 22204), and the
    // native audio engine state R1Delta's lifecycle code checks (+0x200DFA5 failure flag, +0x200DF78 engine pointer).
    const auto* engine = reinterpret_cast<const unsigned char*>(s_engine);
    const bool ready = !*(engine + 0x200DFA5) && *reinterpret_cast<void* const*>(engine + 0x200DF78) != nullptr;
    Msg("R1Delta audio: native sound aliases %d, sound ranges %d, sound script crc %08x (hidden R2 tail: %u aliases, %u ranges), native audio engine %s\n",
        *reinterpret_cast<const int*>(engine + 0x1A0DBC4), *reinterpret_cast<const int*>(engine + 0x1A0DBE0), *reinterpret_cast<const unsigned*>(engine + 0x1A0DBC0), s_hiddenAliases, s_hiddenRanges, ready ? "ready" : "NOT ready");
}
}

bool R1AudioUsesGeneratedContent() { return HasR1DeltaContent(); }

std::uint32_t R1AudioHiddenTrailingAliases() { return s_hiddenAliases; }

bool InstallR1AudioCacheHooks(std::uintptr_t engineBase) {
    if (GetR1DeltaEngineMode() != R1DeltaEngineMode::Client2015) return false;
    if (s_installed) return s_engine == engineBase;
    static constexpr std::array<unsigned char, 32> digest = {0x02,0x14,0xa4,0x04,0x36,0xec,0xa7,0x7f,0x16,0x0d,0x12,0x76,0x3a,0xb8,0xf3,0x92,0xa8,0xce,0x22,0x68,0xbf,0xe0,0xa9,0x73,0x18,0x8e,0x42,0x6a,0xe4,0xc0,0x26,0x3c};
    if (!r1audio::VerifyModule(engineBase, 0x55038DAC, 0x3264000, digest, "R1 engine.dll")) return false;
    struct Hook { std::uintptr_t rva; const char* bytes; std::size_t length; void* detour; void** original; };
    Hook hooks[] = {
        {0xAE00, "\x40\x57\x48\x81\xEC\x60\x03\x00\x00\x8B\x05\x39\x64\xFD\x01", 15, reinterpret_cast<void*>(Lookup), nullptr},
        {0xADA0, "\x48\x83\xEC\x28\x83\x3D\x9D\x64\xFD\x01\x00\x75\x1A", 13, reinterpret_cast<void*>(Initialize), nullptr},
        {0xADE0, "\x48\x83\xEC\x28\x83\x3D\x5D\x64\xFD\x01\x00\x75\x0C", 13, reinterpret_cast<void*>(Destroy), nullptr},
        {0xBDE0, "\x48\x83\xEC\x28\x80\x3D\xBA\x21\x00\x02\x00\x74\x08", 13, reinterpret_cast<void*>(Duration), reinterpret_cast<void**>(&s_duration)},
        {0x11620, "\x48\x83\xEC\x48\xE8\x67\x5B\x10\x00\x84\xC0", 11, reinterpret_cast<void*>(Precache), reinterpret_cast<void**>(&s_precache)},
        {0x11AD0, "\x48\x89\x74\x24\x18\x57\x41\x54\x41\x55\x48\x83\xEC\x20", 14, reinterpret_cast<void*>(Delay), reinterpret_cast<void**>(&s_delay)},
        {0x12FE0, "\x4C\x89\x4C\x24\x20\x44\x89\x44\x24\x18\x89\x54\x24\x10\x89\x4C\x24\x08", 17, reinterpret_cast<void*>(Start), reinterpret_cast<void**>(&s_start)},
        {0x98B0, "\x48\x89\x5C\x24\x08\x48\x89\x74\x24\x10\x57\x48\x81\xEC", 14, reinterpret_cast<void*>(ScriptLoad), reinterpret_cast<void**>(&s_scriptLoad)}
    };
    // Validate every entry and the non-hooked reboot owner before mutating code.
    static constexpr unsigned char reboot[] = {0x48,0x83,0xEC,0x28,0xE8,0xE7,0xFB,0xFF,0xFF,0xE8,0x32,0xD1,0xFF,0xFF};
    if (std::memcmp(reinterpret_cast<const void*>(engineBase + 0x118C0), reboot, sizeof(reboot))) return false;
    for (const auto& hook : hooks) {
        if (std::memcmp(reinterpret_cast<const void*>(engineBase + hook.rva), hook.bytes, hook.length)) {
            Warning("R1Delta audio: refusing mismatched engine prologue +0x%llX\n", static_cast<unsigned long long>(hook.rva));
            return false;
        }
    }
    s_engine = engineBase;
    std::size_t created = 0;
    for (const auto& hook : hooks) {
        void* target = reinterpret_cast<void*>(engineBase + hook.rva);
        const auto create = MH_CreateHook(target, hook.detour, hook.original);
        if (create != MH_OK) break;
        ++created;
        const auto enable = MH_EnableHook(target); // Required for loader-notification installs.
        if (enable != MH_OK) {
            MH_RemoveHook(target);
            --created;
            break;
        }
    }
    if (created != std::size(hooks)) {
        while (created) {
            void* target = reinterpret_cast<void*>(engineBase + hooks[--created].rva);
            MH_DisableHook(target);
            MH_RemoveHook(target);
        }
        Warning("R1Delta audio: required metadata hook transaction failed; cutover not installed\n");
        s_engine = 0;
        return false;
    }
    s_installed = true;
    Msg("R1Delta audio: installed exact R1 lazy metadata/prefetch owner; wav.acache is not read\n");
    return true;
}
void RegisterR1AudioCacheCommands() {
    if (!s_installed || s_commands || GetR1DeltaEngineMode() != R1DeltaEngineMode::Client2015) return;
    RegisterConCommand("r1delta_audio_rebuild", Rebuild, "Stop/drain audio, discard generated metadata, and reload game_sounds manifests. No wav.acache required.", 0);
    RegisterConCommand("r1delta_audio_cache_info", Stats, "Report generated audio metadata and bounded prefetch memory.", 0);
    RegisterConCommand("r1delta_audio_probe", Probe, "Decode a generated sound path through the engine hooks and report peak/rms (works without an audio device).", 0);
    s_commands = true;
}
