#include "r2_shader_override.h"

#include "r2_shader_cache.h"

#include "engine/logging/logging.h"

#include <MinHook.h>
#include <Windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>

namespace r2port::shaders {
namespace {

constexpr std::uintptr_t kExpectedTimestamp = 0x54874238;
constexpr std::uintptr_t kExpectedImageSize = 0x3E0000;
constexpr std::uintptr_t kUnlitTwoTextureStateRva = 0x2FA10;  // sub_18002FA10, vtable slot 9 of UnlitTwoTexture
constexpr unsigned char kPrologue[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x4C, 0x89, 0x44, 0x24, 0x18, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41,
    0x56, 0x41, 0x57, 0x48, 0x83, 0xEC,
};

// Parameter block layout of UnlitTwoTexture (dword indexes into *a2): 50 = $godray, 51 = $godrayMinFadeDist.
constexpr int kGodrayMinFadeIndex = 51;
constexpr float kOverrideIdBase = 7700.0f;

struct Override {
    const char* pixelName;      // value written to a4+24 (file shaders\fxc\<pixelName>.vcs)
    const char* cacheName;      // shader cache entry that must exist
};

// id 1: coliseum/grave/desert cube-map dome (R2 UnlitTwoTexture with a cube in $texture2)
constexpr Override kOverrides[] = {
    { nullptr, nullptr },
    { "r2_ut_dome_ps40", "r2_ut_dome" },
};
constexpr int kOverrideCount = static_cast<int>(sizeof(kOverrides) / sizeof(kOverrides[0]));

using StateFunction = __int64(__fastcall*)(__int64 a1, int** a2, __int64 a3, __int64 a4);
StateFunction s_original = nullptr;
std::atomic<bool> s_missingLogged[kOverrideCount];
std::atomic<int> s_applied{0};

__int64 __fastcall Hook(__int64 a1, int** a2, __int64 a3, __int64 a4)
{
    const __int64 result = s_original(a1, a2, a3, a4);
    if (!result || !a2 || !*a2)
        return result;
    float marker;
    std::memcpy(&marker, &(*a2)[kGodrayMinFadeIndex], sizeof(marker));
    if (!(marker >= kOverrideIdBase + 1.0f && marker < kOverrideIdBase + kOverrideCount))
        return result;
    const int id = static_cast<int>(marker - kOverrideIdBase + 0.5f);
    if (id < 1 || id >= kOverrideCount)
        return result;
    const Override& entry = kOverrides[id];
    if (!HasShaderPixelFile(entry.cacheName)) {
        if (!s_missingLogged[id].exchange(true)) {
            Warning("R1Delta: R2 SHADER OVERRIDE id=%d wants '%s' but the shader cache has no pixel shader for it; the stock UnlitTwoTexture shader is used\n",
                id, entry.cacheName);
        }
        return result;
    }
    *reinterpret_cast<const char**>(a4 + 24) = entry.pixelName;
    *reinterpret_cast<std::uint32_t*>(a4 + 72) = 0;
    if (s_applied.fetch_add(1, std::memory_order_relaxed) == 0)
        Msg("R1Delta: R2 shader override active (UnlitTwoTexture id=%d -> %s)\n", id, entry.pixelName);
    return result;
}

} // namespace

void InstallShaderOverrides(std::uintptr_t base)
{
    static bool installed = false;
    if (installed || !base)
        return;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
        Warning("R1Delta: R2 shader overrides not installed: unsupported image\n");
        return;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64
        || nt->FileHeader.TimeDateStamp != kExpectedTimestamp || nt->OptionalHeader.SizeOfImage != kExpectedImageSize) {
        Warning("R1Delta: R2 shader overrides not installed: materialsystem_dx11 is not the retail R1 build\n");
        return;
    }
    void* target = reinterpret_cast<void*>(base + kUnlitTwoTextureStateRva);
    if (std::memcmp(target, kPrologue, sizeof(kPrologue)) != 0) {
        Warning("R1Delta: R2 shader overrides not installed: UnlitTwoTexture state function prologue mismatch\n");
        return;
    }
    const MH_STATUS create = MH_CreateHook(target, reinterpret_cast<void*>(&Hook), reinterpret_cast<void**>(&s_original));
    const MH_STATUS enable = create == MH_OK ? MH_EnableHook(target) : create;
    installed = enable == MH_OK;
    char text[160];
    _snprintf_s(text, sizeof(text), _TRUNCATE, "R1Delta: R2 shader overrides hook create=%d enable=%d target=%p\n",
        static_cast<int>(create), static_cast<int>(enable), target);
    OutputDebugStringA(text);
}

} // namespace r2port::shaders
