#include "engine/core/r2port_particles_hook.h"

#include "engine/core/core.h"
#include "engine/filesystem/r2_particle_provider.h"
#include "engine/logging/logging.h"

#include <cstdio>
#include <cstring>

namespace {

// Retail R1 client.dll: ParseParticleEffects(bool loadSheets) reads particles_manifest.txt and calls
// ReadParticleConfigFile for every entry; g_pParticleSystemMgr is the pointer at 0x380F1F0.
constexpr std::uintptr_t kParseParticleEffectsRva = 0x1CD5B0;
constexpr std::uintptr_t kReadParticleConfigFileRva = 0x591520;
constexpr std::uintptr_t kParticleSystemMgrRva = 0x380F1F0;
constexpr unsigned char kParseExpected[] = { 0x40, 0x56, 0x48, 0x83, 0xEC, 0x40, 0x0F, 0xB6, 0xD1, 0x48, 0x8B, 0x0D };
constexpr unsigned char kReadExpected[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x81, 0xEC, 0xB0, 0x01, 0x00, 0x00 };

using ParseParticleEffectsFn = std::int64_t(__fastcall*)(unsigned char loadSheets);
using ReadParticleConfigFileFn = char(__fastcall*)(void* manager, const char* file, unsigned char precache, unsigned char decommit);

ParseParticleEffectsFn g_originalParse = nullptr;
ReadParticleConfigFileFn g_readConfigFile = nullptr;
std::uintptr_t g_clientBase = 0;

std::int64_t __fastcall ParseParticleEffectsHook(unsigned char loadSheets)
{
    const std::int64_t result = g_originalParse(loadSheets);
    try {
        if (r2port::R2ParticlesAvailable()) {
            void* manager = *reinterpret_cast<void**>(g_clientBase + kParticleSystemMgrRva);
            if (manager) {
                const char loaded = g_readConfigFile(manager, r2port::kR2ParticlePcfPath, 0, 0);
                char message[256]{};
                _snprintf_s(message, sizeof(message), _TRUNCATE,
                    "R1Delta: R2 particle file %s loaded=%d\n", r2port::kR2ParticlePcfPath, static_cast<int>(loaded));
                Msg("%s", message);
                OutputDebugStringA(message);
            }
        }
    }
    catch (...) {
    }
    return result;
}

} // namespace

void InstallClientParticleHook(std::uintptr_t clientBase)
{
    if (!clientBase || g_originalParse)
        return;
    void* parse = reinterpret_cast<void*>(clientBase + kParseParticleEffectsRva);
    void* read = reinterpret_cast<void*>(clientBase + kReadParticleConfigFileRva);
    if (std::memcmp(parse, kParseExpected, sizeof(kParseExpected)) != 0
        || std::memcmp(read, kReadExpected, sizeof(kReadExpected)) != 0)
        return;

    g_clientBase = clientBase;
    g_readConfigFile = reinterpret_cast<ReadParticleConfigFileFn>(read);
    const MH_STATUS status = MH_CreateHook(parse, &ParseParticleEffectsHook, reinterpret_cast<LPVOID*>(&g_originalParse));
    if (status != MH_OK && status != MH_ERROR_ALREADY_CREATED) {
        Warning("R1Delta: failed to hook ParseParticleEffects (%d)\n", static_cast<int>(status));
        g_originalParse = nullptr;
        return;
    }
    const MH_STATUS enableStatus = MH_EnableHook(parse);
    if (enableStatus != MH_OK)
        Warning("R1Delta: failed to enable the ParseParticleEffects hook (%d)\n", static_cast<int>(enableStatus));
}
