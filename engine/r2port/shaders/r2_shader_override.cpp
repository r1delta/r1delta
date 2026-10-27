#include "r2_shader_override.h"

#include "r2_shader_cache.h"

#include "engine/logging/logging.h"

#include <MinHook.h>
#include <Windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include "engine/filesystem/r2_map_provider.h"

namespace r2port::shaders {
namespace {

inline std::uint64_t HashKey(std::uint32_t a, std::uint32_t b)
{
    return (static_cast<std::uint64_t>(a) << 32 | b) * 0x9E3779B97F4A7C15ull;
}

// LightmappedGeneric (world brushes), vtable slot 9 = RVA 0x28610. Env `R2_LMG_PBR`: unset or 0 = no hook at all,
// `log` = log every distinct draw-state to <r2port dir>\lmg_log.txt (analysis only).
constexpr std::uintptr_t kLmgStateRva = 0x28610;
constexpr unsigned char kLmgPrologue[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x4C, 0x89, 0x4C, 0x24, 0x20, 0x48, 0x89, 0x54, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41,
    0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x40,
};

bool SafeCopy(const void* source, void* destination, std::size_t size)
{
    __try {
        std::memcpy(destination, source, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool LooksLikeName(const char* text, std::size_t capacity)
{
    std::size_t length = 0;
    while (length < capacity && text[length] && static_cast<unsigned char>(text[length]) >= 0x20 && static_cast<unsigned char>(text[length]) < 0x7F)
        ++length;
    if (length < 6 || length >= capacity || text[length] != 0)
        return false;
    bool separator = false;
    for (std::size_t i = 0; i < length; ++i)
        separator |= text[i] == '/' || text[i] == '\\';
    return separator;
}

// Looks for a path-like C string in an engine object, inline, one pointer away or two pointers away (texture name).
bool FindObjectName(const void* object, char* out, std::size_t outSize)
{
    out[0] = 0;
    if (!object)
        return false;
    unsigned char bytes[512];
    if (!SafeCopy(object, bytes, sizeof(bytes)))
        return false;
    for (std::size_t i = 0; i + 8 < sizeof(bytes); ++i) {
        if (LooksLikeName(reinterpret_cast<const char*>(bytes + i), sizeof(bytes) - i)) {
            _snprintf_s(out, outSize, _TRUNCATE, "inl:%s", reinterpret_cast<const char*>(bytes + i));
            return true;
        }
    }
    for (int level = 1; level <= 2; ++level) {
        for (std::size_t i = 0; i + 8 <= sizeof(bytes); i += 8) {
            std::uintptr_t word;
            std::memcpy(&word, bytes + i, sizeof(word));
            if (word < 0x10000 || word > 0x7FFFFFFFFFFFull)
                continue;
            char text[128];
            if (level == 1) {
                if (SafeCopy(reinterpret_cast<const void*>(word), text, sizeof(text)) && LooksLikeName(text, sizeof(text))) {
                    _snprintf_s(out, outSize, _TRUNCATE, "p%zu:%s", i, text);
                    return true;
                }
                continue;
            }
            std::uintptr_t inner[16];
            if (!SafeCopy(reinterpret_cast<const void*>(word), inner, sizeof(inner)))
                continue;
            for (std::uintptr_t w2 : inner) {
                if (w2 < 0x10000 || w2 > 0x7FFFFFFFFFFFull)
                    continue;
                if (SafeCopy(reinterpret_cast<const void*>(w2), text, sizeof(text)) && LooksLikeName(text, sizeof(text))) {
                    _snprintf_s(out, outSize, _TRUNCATE, "pp%zu:%s", i, text);
                    return true;
                }
            }
        }
    }
    return false;
}

std::uintptr_t ParamPointer(const unsigned char* params, int offset)
{
    std::uintptr_t value = 0;
    SafeCopy(params + offset, &value, sizeof(value));
    return value;
}

std::uint32_t ParamDword(const unsigned char* params, int offset)
{
    std::uint32_t value = 0;
    SafeCopy(params + offset, &value, sizeof(value));
    return value;
}

using LmgStateFunction = __int64(__fastcall*)(__int64 a1, __int64* a2, __int64 a3, __int64 a4);
LmgStateFunction s_lmgOriginal = nullptr;

std::filesystem::path s_lmgLogPath;
bool s_lmgLogEnabled = false;

void LmgLog(const char* line)
{
    Msg("%s", line);
    if (s_lmgLogPath.empty()) {
        const std::filesystem::path directory = GetR2MapPatchDirectory();
        if (directory.empty())
            return;
        s_lmgLogPath = directory / "lmg_log.txt";
    }
    if (FILE* file = _wfopen(s_lmgLogPath.c_str(), L"ab")) {
        std::fputs(line, file);
        std::fclose(file);
    }
}

struct LmgSeen {
    std::uint64_t key;
};
std::atomic<std::uint32_t> s_lmgSeenCount{0};
LmgSeen s_lmgSeen[4096];

int LmgEligible(const unsigned char* params, __int64 a4, bool needPacked);

void LmgLogState(__int64 a3, __int64 a4, const unsigned char* params)
{
    const std::uintptr_t baseTexture = ParamPointer(params, 32);
    std::uint64_t key = HashKey(*reinterpret_cast<const std::uint32_t*>(a4 + 72), *reinterpret_cast<const std::uint32_t*>(a4 + 76));
    key = key * 1099511628211ull ^ baseTexture;
    const std::uint32_t seen = s_lmgSeenCount.load(std::memory_order_acquire);
    for (std::uint32_t i = 0; i < seen && i < 4096; ++i)
        if (s_lmgSeen[i].key == key)
            return;
    if (seen >= 4096)
        return;
    const std::uint32_t slot = s_lmgSeenCount.fetch_add(1, std::memory_order_acq_rel);
    if (slot >= 4096)
        return;
    s_lmgSeen[slot].key = key;

    const char* pixelName = *reinterpret_cast<const char* const*>(a4 + 24);
    const char* vertexName = *reinterpret_cast<const char* const*>(a4 + 32);
    char line[1400];
    _snprintf_s(line, sizeof(line), _TRUNCATE,
        "LMG ps=%u vs=%u fmt=%016llx st=%08x psn=%s vsn=%s flags=%08x flags2=%08x sway=%d "
        "tex[base=%d dc=%d dn=%d dc2=%d dn2=%d env=%d rt=%d rt2=%d bump=%d bump2=%d base2=%d blend=%d] "
        "modes[%u %u %u %u] d372=%08x d376=%08x d380=%08x d384=%08x d388=%08x d392=%08x f396=%g f400=%g elig[neutral=%d pbr=%d]\n",
        *reinterpret_cast<const std::uint32_t*>(a4 + 72), *reinterpret_cast<const std::uint32_t*>(a4 + 76),
        static_cast<unsigned long long>(*reinterpret_cast<const std::uint64_t*>(a4 + 16)),
        *reinterpret_cast<const std::uint32_t*>(a3 + 4), pixelName ? pixelName : "?", vertexName ? vertexName : "?",
        ParamDword(params, 0), ParamDword(params, 4), static_cast<int>(ParamDword(params, 24)),
        ParamPointer(params, 32) != 0, ParamPointer(params, 80) != 0, ParamPointer(params, 88) != 0,
        ParamPointer(params, 96) != 0, ParamPointer(params, 104) != 0, ParamPointer(params, 136) != 0,
        ParamPointer(params, 232) != 0, ParamPointer(params, 240) != 0, ParamPointer(params, 304) != 0,
        ParamPointer(params, 320) != 0, ParamPointer(params, 328) != 0, ParamPointer(params, 336) != 0,
        ParamDword(params, 112), ParamDword(params, 116), ParamDword(params, 120), ParamDword(params, 124),
        ParamDword(params, 372), ParamDword(params, 376), ParamDword(params, 380), ParamDword(params, 384),
        ParamDword(params, 388), ParamDword(params, 392),
        static_cast<double>(*reinterpret_cast<const float*>(params + 396)), static_cast<double>(*reinterpret_cast<const float*>(params + 400)),
        LmgEligible(params, a4, false), LmgEligible(params, a4, true));
    LmgLog(line);
}

// ---- the override ----------------------------------------------------------------------------------------------
//
// Parameter block of LightmappedGeneric (byte offsets into *a2, read from sub_180028610 / 28FE0): 0 flags, 4 flags2,
// 24 $treesway (marker, int), 32 $basetexture, 80 $detailColor, 88 $detailNormal, 96 $detailColor2, 104 $detailNormal2,
// 112 $detailColorMode, 136 $envmap, 232 $reflectionTint, 240 $reflectionTint2, 304 $bumpmap, 320 $bumpmap2,
// 328 $baseTexture2, 336 $blendModulateTexture (all textures are pointers).
//
// Eligible draw: stock pixel shader ps50 with stock vertex shader vs40, a single layer with $bumpmap and
// $reflectionTint and nothing else (no second layer, no detail normal, no godray), and the real-time-light texture
// (t20) path active in the stock combo (global render flags 0x400000 set, 0x2000000 shadow-array path clear). Those are
// exactly the inputs r2_lmg_pbr declares (TEXCOORD0/2/3/4/5 + COLOR0 from every bump vertex-shader combo, t0 t1 t2 t4,
// t6 t8 t9 t10, t16 t18 t20, cb0 cb2 cb3), so the stock VS, vertex format and draw path stay.
constexpr std::uintptr_t kLmgRenderFlagsRva = 0x38FB2C;   // HIDWORD(xmmword_18038FB20) = dword 3 of the 16 bytes at RVA 0x38FB20 (measured 0x00e81009), read by sub_180028610
constexpr std::uint32_t kLmgDynamicCombos = 12;            // dynamic pixel combos per static combo in our file (header word)
constexpr int kLmgMarkerValue = 7701;                     // $treesway 7701 opts a material in (mode 1)

enum class LmgMode { Log, Marked, All, Neutral, Mark, Debug };   // Mark: like Neutral, tints replaced pixels; Debug: r2_lmg_dbgN visualisations (test aids)
char s_lmgDebugName[24];
bool s_lmgDebugNoPack = false;
bool s_lmgNeutralPacked = false;
char s_lmgDebugPixel[32];
LmgMode s_lmgMode = LmgMode::Log;
std::uintptr_t s_lmgBase = 0;
std::atomic<int> s_lmgApplied{0};
std::atomic<bool> s_lmgMissing{false};

// Test aid (only reachable when R2_LMG_PBR is set): while `<r2port dir>\lmg_off.flag` exists the hook passes every draw
// through unchanged, so one session can capture the stock and the replaced frame from the same camera and exposure.
std::atomic<bool> s_lmgBypass{false};
std::atomic<std::uint32_t> s_lmgCalls{0};

void LmgRefreshBypass()
{
    const std::filesystem::path directory = GetR2MapPatchDirectory();
    if (directory.empty())
        return;
    s_lmgBypass.store(GetFileAttributesW((directory / "lmg_off.flag").c_str()) != INVALID_FILE_ATTRIBUTES, std::memory_order_relaxed);
}

std::atomic<std::uint32_t> s_lmgReasonSeen{0};

// 0 = eligible, otherwise the reason code of the first failed condition (logged in log mode).
int LmgEligible(const unsigned char* params, __int64 a4, bool needPacked)
{
    const char* pixelName = *reinterpret_cast<const char* const*>(a4 + 24);
    const char* vertexName = *reinterpret_cast<const char* const*>(a4 + 32);
    if (!pixelName || !vertexName || std::strcmp(pixelName, "lightmappedgeneric_ps50") != 0
        || std::strcmp(vertexName, "lightmappedgeneric_vs40") != 0)
        return 1;
    const std::uint32_t renderFlags = *reinterpret_cast<const std::uint32_t*>(s_lmgBase + kLmgRenderFlagsRva);
    if ((renderFlags & 0x1400000) != 0x400000 || (renderFlags & 0x2000000) != 0)
        return 2;
    const std::uint64_t format = *reinterpret_cast<const std::uint64_t*>(a4 + 16);
    if (format & 0x800000)   // godray adds COLOR1
        return 3;
    // vertex shader combo (a4+76 / 2) = 2 * bump + 3 * detail + 6 * x: tangent and binormal outputs need the bump bit
    const std::uint32_t vertexCombo = *reinterpret_cast<const std::uint32_t*>(a4 + 76) / 2;
    if (vertexCombo % 6 != 2 && vertexCombo % 6 != 5)
        return 4;
    if (!ParamPointer(params, 32) || !ParamPointer(params, 304) || !ParamPointer(params, 232))
        return 5;
    if (ParamPointer(params, 96) || ParamPointer(params, 88) || ParamPointer(params, 104) || ParamPointer(params, 240)
        || ParamPointer(params, 320) || ParamPointer(params, 328) || ParamPointer(params, 336))
        return 6;
    if (ParamDword(params, 0) & 0x10)   // $vertexcolor takes another stock static combo that was not compared
        return 7;
    const bool packed = ParamPointer(params, 80) != 0 && ParamDword(params, 112) != 0;
    if (needPacked ? !packed : ParamPointer(params, 80) != 0)
        return 8;
    return 0;
}

__int64 __fastcall LmgHook(__int64 a1, __int64* a2, __int64 a3, __int64 a4)
{
    const __int64 result = s_lmgOriginal(a1, a2, a3, a4);
    if (!a2 || !a4)
        return result;
    const auto* params = reinterpret_cast<const unsigned char*>(*a2);
    if (!params)
        return result;
    if (s_lmgLogEnabled)
        LmgLogState(a3, a4, params);
    if (s_lmgMode == LmgMode::Log)
        return result;
    if ((s_lmgCalls.fetch_add(1, std::memory_order_relaxed) & 255) == 0)
        LmgRefreshBypass();
    if (s_lmgBypass.load(std::memory_order_relaxed))
        return result;
    if (s_lmgMode == LmgMode::Marked && static_cast<int>(ParamDword(params, 24)) != kLmgMarkerValue)
        return result;
    const bool debug = s_lmgMode == LmgMode::Debug;
    const bool mark = s_lmgMode == LmgMode::Mark;
    const bool neutral = s_lmgMode == LmgMode::Neutral || mark;
    const int reason = LmgEligible(params, a4, !(neutral || (debug && s_lmgDebugNoPack)) || (neutral && s_lmgNeutralPacked));
    if (reason != 0) {
        if (s_lmgLogEnabled && !(s_lmgReasonSeen.fetch_or(1u << reason) & (1u << reason))) {
            char line[128];
            const auto* rf = reinterpret_cast<const std::uint32_t*>(s_lmgBase + kLmgRenderFlagsRva - 12);
            _snprintf_s(line, sizeof(line), _TRUNCATE, "LMG REJECT reason=%d renderflags=%08x %08x %08x %08x\n", reason, rf[0], rf[1], rf[2], rf[3]);
            LmgLog(line);
        }
        return result;
    }
    const char* shaderName = debug ? s_lmgDebugName : mark ? "r2_lmg_mark" : neutral ? "r2_lmg_neutral" : "r2_lmg_pbr";
    if (!HasShaderPixelFile(shaderName)) {
        if (!s_lmgMissing.exchange(true))
            Warning("R1Delta: R2 LMG override wants '%s' but the shader cache has no pixel shader for it; the stock shader is used\n", shaderName);
        return result;
    }
    *reinterpret_cast<const char**>(a4 + 24) = debug ? s_lmgDebugPixel : mark ? "r2_lmg_mark_ps40" : neutral ? "r2_lmg_neutral_ps40" : "r2_lmg_pbr_ps40";
    // static combo 1 = vertex-shader combos with the extra TEXCOORD1.xy detail output (a requested combo id is static index * 12 dynamic combos)
    const std::uint32_t staticIndex = (*reinterpret_cast<const std::uint32_t*>(a4 + 76) / 2) % 6 == 5 ? 1u : 0u;
    *reinterpret_cast<std::uint32_t*>(a4 + 72) = staticIndex * kLmgDynamicCombos;
    const int applied = s_lmgApplied.fetch_add(1, std::memory_order_relaxed);
    if (applied == 0)
        Msg("R1Delta: R2 LightmappedGeneric override active (%s)\n", shaderName);
    if (s_lmgLogEnabled && (applied == 0 || applied == 1000 || applied == 20000 || applied == 200000)) {
        char line[96];
        _snprintf_s(line, sizeof(line), _TRUNCATE, "LMG APPLIED count=%d shader=%s\n", applied + 1, shaderName);
        LmgLog(line);
    }
    return result;
}

} // namespace

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

namespace {

void InstallLmgOverride(std::uintptr_t base)
{
    // R2_LMG_PBR (default off, no hook installed): `1` = materials with `$treesway 7701`, `all` = every eligible
    // material, `neutral` = every eligible material without a packed texture, drawn with the stock-formula replica
    // (validation: must match the stock frame), `log` = only log the distinct draw states. Append `+log` to log as well.
    char mode[48] = {};
    if (!GetEnvironmentVariableA("R2_LMG_PBR", mode, sizeof(mode)) || !mode[0] || std::strcmp(mode, "0") == 0)
        return;
    s_lmgLogEnabled = std::strstr(mode, "log") != nullptr;
    if (std::strncmp(mode, "all", 3) == 0)
        s_lmgMode = LmgMode::All;
    else if (std::strncmp(mode, "dbg", 3) == 0 && mode[3] >= '6' && mode[3] <= '8') {
        s_lmgMode = LmgMode::Debug;
        _snprintf_s(s_lmgDebugName, sizeof(s_lmgDebugName), _TRUNCATE, "r2_lmg_dbg%c", mode[3]);
        _snprintf_s(s_lmgDebugPixel, sizeof(s_lmgDebugPixel), _TRUNCATE, "r2_lmg_dbg%c_ps40", mode[3]);
        s_lmgDebugNoPack = mode[4] == 'n';
    }
    else if (std::strncmp(mode, "mark", 4) == 0)
        s_lmgMode = LmgMode::Mark;
    else if (std::strncmp(mode, "neutral", 7) == 0)
        s_lmgMode = LmgMode::Neutral;
    else if (mode[0] == '1')
        s_lmgMode = LmgMode::Marked;
    else
        s_lmgMode = LmgMode::Log;
    s_lmgNeutralPacked = std::strstr(mode, "neutralpk") != nullptr;   // test aid: neutral shader on materials that carry a packed texture
    s_lmgBase = base;
    void* target = reinterpret_cast<void*>(base + kLmgStateRva);
    if (std::memcmp(target, kLmgPrologue, sizeof(kLmgPrologue)) != 0) {
        Warning("R1Delta: R2 LMG override not installed: LightmappedGeneric state function prologue mismatch\n");
        return;
    }
    const MH_STATUS create = MH_CreateHook(target, reinterpret_cast<void*>(&LmgHook), reinterpret_cast<void**>(&s_lmgOriginal));
    const MH_STATUS enable = create == MH_OK ? MH_EnableHook(target) : create;
    Msg("R1Delta: R2 LMG hook mode=%s create=%d enable=%d target=%p\n", mode, static_cast<int>(create), static_cast<int>(enable), target);
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
    InstallLmgOverride(base);
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
