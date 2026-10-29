#include "r2_shader_override.h"

#include "r2_shader_cache.h"

#include "engine/logging/logging.h"

#include <MinHook.h>
#include <Windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <string>

#include "engine/filesystem/r2_map_provider.h"

namespace r2port::shaders {
namespace {

inline std::uint64_t HashKey(std::uint32_t a, std::uint32_t b)
{
    return (static_cast<std::uint64_t>(a) << 32 | b) * 0x9E3779B97F4A7C15ull;
}

// LightmappedGeneric (world brushes), vtable slot 9 = RVA 0x28610. Env `R2_UBER`: unset or 0 = no hook at all,
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
bool s_uberLogEnabled = false;

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

// The engine keeps the pixel shader name as a bare pointer in the draw state: hand out storage that never moves.
const char* StableName(const char* name)
{
    static std::mutex mutex;
    static std::set<std::string> names;
    std::lock_guard<std::mutex> lock(mutex);
    return names.insert(name).first->c_str();
}

struct LmgSeen {
    std::uint64_t key;
};
std::atomic<std::uint32_t> s_lmgSeenCount{0};
LmgSeen s_lmgSeen[4096];

// ---- the override ----------------------------------------------------------------------------------------------
//
// Parameter block of LightmappedGeneric (byte offsets into *a2, read from sub_180028610 / 28FE0; shaderlab\uber\tools\
// paramlayout.py prints them from the descriptor tables): 0 flags (0x10 = $alphatest), 4 flags2, 24 $treesway (marker,
// int), 32 $basetexture, 80 $detailColor, 88 $detailNormal, 96 $detailColor2, 104 $detailNormal2, 112 $detailColorMode,
// 136 $envmap, 164 $envmapLightScale (float), 232 $reflectionTint, 240 $reflectionTint2, 304 $bumpmap, 320 $bumpmap2,
// 328 $baseTexture2, 336 $blendModulateTexture (textures are pointers).
//
// Eligible draw: stock pixel shader ps50 with stock vertex shader vs40, a single layer with $bumpmap and $reflectionTint
// or the complete two-layer set, optionally with $envmap and $alphatest, and the real-time-light
// texture (t20) path active in the stock combo (global render flags 0x400000 set, 0x2000000 shadow-array path clear).
// Those are exactly the inputs the uber source declares (TEXCOORD0/2/3/4/5 + COLOR0 from every bump vertex-shader combo,
// t0 t1 t2 t4 t5, t6 t8 t9 t10, t16 t18 t20, cb0 cb2 cb3), so the stock VS, vertex format and draw path stay.
constexpr std::uintptr_t kLmgRenderFlagsRva = 0x38FB2C;   // HIDWORD(xmmword_18038FB20) = dword 3 of the 16 bytes at RVA 0x38FB20 (measured 0x00e81009), read by sub_180028610
constexpr std::uint32_t kLmgDynamicCombos = 12;            // dynamic pixel combos per static combo in our file (header word)
constexpr int kLmgMarkerValue = 7701;                     // $treesway 7701 marks a layered material that carries AO / cavity in $blendModulateTexture b / a

// Env R2_UBER: comma separated tokens. Mode: `log` (only log draw states), `neutral` (stock formulas, no R2 terms:
// validation), `mark` (neutral, red tint on replaced pixels), `dbg6|7|8` (panel visualisations). R2 terms: `lod`, `fenv`,
// `so`, `ggx`, `pack`, `all` (= all of them), `rough=<v>` (test aid: roughness of every pixel). `+log` anywhere also logs.
// Class selection: `props` hooks only VertexLitGeneric (world stays stock), `world` only LightmappedGeneric (props stay stock);
// neither (or both) hooks both classes.
enum class UberMode { Log, Neutral, Mark, Debug, Uber };
UberMode s_uberMode = UberMode::Log;
char s_uberMacros[256];       // ';' separated NAME=VALUE macros every variant is built with (R2 terms, mode)
char s_uberTag[64];           // short text appended to the shader name so different token sets never share a cache file
std::uintptr_t s_lmgBase = 0;
std::atomic<int> s_lmgApplied{0};
std::atomic<bool> s_lmgMissing{false};
std::atomic<std::uint32_t> s_lmgReasonSeen{0};
bool s_lmgHasPack = false;   // pack token given: single layer materials with a packed $detailColor and layered ones with the marker carry AO / cavity

struct LmgFeatures {
    int staticIndex = 0;   // 0 single layer, 1 single layer + detail UV, 2 two layers, 3 two layers + detail UV, 4 two layers + detail normals
    int env = 0;           // 0 none, 1 $envmap, 2 $envmap with $envmapLightScale > 0
    bool alphaTest = false;
    bool pack = false;
    bool array = false;    // shadow-array statics (t7 light shadow maps)
};

// 0 = eligible, otherwise the reason code of the first failed condition (logged in log mode).
int LmgEligible(const unsigned char* params, __int64 a4, bool allowPack, LmgFeatures* features)
{
    const char* pixelName = *reinterpret_cast<const char* const*>(a4 + 24);
    const char* vertexName = *reinterpret_cast<const char* const*>(a4 + 32);
    if (!pixelName || !vertexName || std::strcmp(pixelName, "lightmappedgeneric_ps50") != 0
        || std::strcmp(vertexName, "lightmappedgeneric_vs40") != 0)
        return 1;
    const std::uint32_t renderFlags = *reinterpret_cast<const std::uint32_t*>(s_lmgBase + kLmgRenderFlagsRva);
    if ((renderFlags & 0x1400000) != 0x400000)
        return 2;
    // render flag 0x2000000 selects the shadow-array statics (+18432 on the stock static id: bits 0x4800), where the
    // real-time lights read a variance shadow map array at t7 (R2_ARR); without the matching static id keep the stock shader
    const std::uint32_t stockStatic = *reinterpret_cast<const std::uint32_t*>(a4 + 72) / 18;
    const bool arrayStatic = (renderFlags & 0x2000000) != 0 && (stockStatic & 0x4800) == 0x4800;
    if ((renderFlags & 0x2000000) != 0 && !arrayStatic)
        return 2;
    const std::uint64_t format = *reinterpret_cast<const std::uint64_t*>(a4 + 16);
    if (format & 0x800000)   // godray adds COLOR1
        return 3;
    // vertex shader combo (a4+76 / 2) = 2 * bump + 3 * detail + 6 * x, x = v20 + 2 * v37 + 4 * v21 + 8 * v23 + 16 * v45 + 32 * v52:
    // tangent and binormal outputs need the bump bit; v23 turns TEXCOORD1 into a float4, v45 / v52 add COLOR1
    const std::uint32_t vertexCombo = *reinterpret_cast<const std::uint32_t*>(a4 + 76) / 2;
    if (vertexCombo % 6 != 2 && vertexCombo % 6 != 5)
        return 4;
    if ((vertexCombo / 6) & (8 | 16 | 32))
        return 3;
    const bool detailUv = vertexCombo % 6 == 5;
    if (!ParamPointer(params, 32) || !ParamPointer(params, 304) || !ParamPointer(params, 232))
        return 5;
    const bool layered = ParamPointer(params, 240) || ParamPointer(params, 320) || ParamPointer(params, 328) || ParamPointer(params, 336);
    if (layered && !(ParamPointer(params, 240) && ParamPointer(params, 320) && ParamPointer(params, 328) && ParamPointer(params, 336)))
        return 6;
    // detail textures count only with a non-zero mode (the stock state function ignores them otherwise: converted materials
    // name $detailNormal / $detailColor without a mode)
    if ((ParamPointer(params, 96) && ParamDword(params, 120) != 0) || (ParamPointer(params, 104) && ParamDword(params, 124) != 0))
        return 6;
    const bool detailNormal = ParamPointer(params, 88) && ParamDword(params, 116) != 0;
    if (detailNormal && !layered)
        return 6;
    if (detailNormal && !detailUv)
        return 6;
    const bool detailColor = ParamPointer(params, 80) != 0 && ParamDword(params, 112) != 0;
    bool pack = false;
    if (layered) {
        if (detailColor)   // t4 is the second detail normal / unused here
            return 8;
        pack = allowPack && static_cast<int>(ParamDword(params, 24)) == kLmgMarkerValue;
    }
    else if (detailColor) {
        if (!allowPack)   // a stock detail colour on a single layer is not reproduced
            return 8;
        pack = true;
    }
    int env = 0;
    if (ParamPointer(params, 136)) {
        // stock: env mode 2 when $envmapLightScale (+164) is > 0, or on a layered material when $envmapLightScale2 (+168) is
        const float lightScale = *reinterpret_cast<const float*>(params + 164);
        const float lightScale2 = *reinterpret_cast<const float*>(params + 168);
        env = (lightScale > 0.0f || (layered && lightScale2 > 0.0f)) ? 2 : 1;
    }
    if (features) {
        features->staticIndex = layered ? (detailNormal ? 4 : detailUv ? 3 : 2) : (detailUv ? 1 : 0);
        features->env = env;
        features->alphaTest = (ParamDword(params, 0) & 0x10) != 0;
        features->pack = pack;
        features->array = arrayStatic;
    }
    return 0;
}

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
    LmgFeatures features;
    const int reason = LmgEligible(params, a4, true, &features);
    char line[1500];
    _snprintf_s(line, sizeof(line), _TRUNCATE,
        "LMG ps=%u static=%u vs=%u fmt=%016llx st=%08x psn=%s vsn=%s flags=%08x flags2=%08x sway=%d "
        "tex[base=%d dc=%d dn=%d dc2=%d dn2=%d env=%d rt=%d rt2=%d bump=%d bump2=%d base2=%d blend=%d] "
        "modes[%u %u %u %u] envLS=%g envLS2=%g f396=%g f400=%g elig[reason=%d env=%d atest=%d pack=%d idx=%d]\n",
        *reinterpret_cast<const std::uint32_t*>(a4 + 72), *reinterpret_cast<const std::uint32_t*>(a4 + 72) / 18,
        *reinterpret_cast<const std::uint32_t*>(a4 + 76),
        static_cast<unsigned long long>(*reinterpret_cast<const std::uint64_t*>(a4 + 16)),
        *reinterpret_cast<const std::uint32_t*>(a3 + 4), pixelName ? pixelName : "?", vertexName ? vertexName : "?",
        ParamDword(params, 0), ParamDword(params, 4), static_cast<int>(ParamDword(params, 24)),
        ParamPointer(params, 32) != 0, ParamPointer(params, 80) != 0, ParamPointer(params, 88) != 0,
        ParamPointer(params, 96) != 0, ParamPointer(params, 104) != 0, ParamPointer(params, 136) != 0,
        ParamPointer(params, 232) != 0, ParamPointer(params, 240) != 0, ParamPointer(params, 304) != 0,
        ParamPointer(params, 320) != 0, ParamPointer(params, 328) != 0, ParamPointer(params, 336) != 0,
        ParamDword(params, 112), ParamDword(params, 116), ParamDword(params, 120), ParamDword(params, 124),
        static_cast<double>(*reinterpret_cast<const float*>(params + 164)), static_cast<double>(*reinterpret_cast<const float*>(params + 168)),
        static_cast<double>(*reinterpret_cast<const float*>(params + 396)), static_cast<double>(*reinterpret_cast<const float*>(params + 400)),
        reason, features.env, features.alphaTest ? 1 : 0, features.pack ? 1 : 0, features.staticIndex);
    LmgLog(line);
}

__int64 __fastcall LmgHook(__int64 a1, __int64* a2, __int64 a3, __int64 a4)
{
    const __int64 result = s_lmgOriginal(a1, a2, a3, a4);
    if (!a2 || !a4)
        return result;
    const auto* params = reinterpret_cast<const unsigned char*>(*a2);
    if (!params)
        return result;
    if (s_uberLogEnabled)
        LmgLogState(a3, a4, params);
    if (s_uberMode == UberMode::Log)
        return result;
    LmgFeatures features;
    const bool uber = s_uberMode == UberMode::Uber;
    const int reason = LmgEligible(params, a4, uber && s_lmgHasPack, &features);
    if (reason != 0) {
        if (s_uberLogEnabled && !(s_lmgReasonSeen.fetch_or(1u << reason) & (1u << reason))) {
            char line[128];
            const auto* rf = reinterpret_cast<const std::uint32_t*>(s_lmgBase + kLmgRenderFlagsRva - 12);
            _snprintf_s(line, sizeof(line), _TRUNCATE, "LMG REJECT reason=%d renderflags=%08x %08x %08x %08x\n", reason, rf[0], rf[1], rf[2], rf[3]);
            LmgLog(line);
        }
        return result;
    }
    // one shader file per (env mode, alpha test, packed carrier) holding the 5 static combos x 12 dynamic combos
    char shaderName[96];
    char pixelFile[112];
    char macros[320];
    _snprintf_s(shaderName, sizeof(shaderName), _TRUNCATE, "r2_uber_lmg_e%da%dp%ds%d%s", features.env, features.alphaTest ? 1 : 0, features.pack ? 1 : 0, features.array ? 1 : 0, s_uberTag);
    _snprintf_s(pixelFile, sizeof(pixelFile), _TRUNCATE, "%s_ps40", shaderName);
    _snprintf_s(macros, sizeof(macros), _TRUNCATE, "R2_ENV=%d;R2_ATEST=%d;%s%s%s", features.env, features.alphaTest ? 1 : 0,
        features.pack ? "R2_T_PACK=1;" : "", features.array ? "R2_ARR=1;" : "", s_uberMacros);
    if (!EnsureUberShader(UberShader::LightmappedGeneric, shaderName, macros)) {
        if (!s_lmgMissing.exchange(true))
            Warning("R1Delta: R2 LMG override wants '%s' but its build failed (see R2 SHADER BUILD FAILED); the stock shader is used\n", shaderName);
        return result;
    }
    // the stable storage the engine keeps the name pointer in: one entry per shader name
    *reinterpret_cast<const char**>(a4 + 24) = StableName(pixelFile);
    // requested combo id = static index * 12 dynamic combos
    *reinterpret_cast<std::uint32_t*>(a4 + 72) = static_cast<std::uint32_t>(features.staticIndex) * kLmgDynamicCombos;
    const int applied = s_lmgApplied.fetch_add(1, std::memory_order_relaxed);
    if (applied == 0)
        Msg("R1Delta: R2 LightmappedGeneric override active (%s)\n", shaderName);
    if (s_uberLogEnabled && (applied == 0 || applied == 1000 || applied == 20000 || applied == 200000)) {
        char line[96];
        _snprintf_s(line, sizeof(line), _TRUNCATE, "LMG APPLIED count=%d shader=%s\n", applied + 1, shaderName);
        LmgLog(line);
    }
    return result;
}

// VertexLitGeneric slot 9, retail R1 sub_180030BC0. Slot 10 (0x304C0) keeps selecting the dynamic record:
// shadowMode + 3 * (drawFlag0x200 + 2 * ((boneCount > 1) + 2 * fogFlavour)). Only the PS name / static offset changes.
constexpr std::uintptr_t kVlgStateRva = 0x30BC0;
constexpr unsigned char kVlgPrologue[24] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55,
    0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x49, 0x83, 0x78, 0x10,
};
using VlgStateFunction = __int64(__fastcall*)(__int64, int**, __int64, __int64);
VlgStateFunction s_vlgOriginal = nullptr;
std::atomic<bool> s_vlgMissing{false};
std::atomic<int> s_vlgApplied{0};

// VLG parameter offsets are class-specific, from paramlayout.py VertexLitGeneric / sub_180030BC0.
// Eligibility is an exact static-id allowlist, not a prediction of render globals. Stock id bits (sub_180030BC0):
// 0x4000 shadow-array path (render flag 0x2000000: the four weighted lights read the t7 variance shadow map array),
// 0x8000 alpha dither (t19 noise, cb2[24].zw), only seen together with 0x4000. In the shadow-array render mode every
// prop material is created with 0x4000 and a twin with 0xC000, so the plain statics never occur there.
// Unsupported dither-only, cockpit, lightwarp and detail statics are never replaced.
constexpr std::uint32_t kVlgArrayBit = 0x4000;
constexpr std::uint32_t kVlgDitherBit = 0x8000;

int VlgEligible(const unsigned char* params, __int64 a4)
{
    const char* ps = *reinterpret_cast<const char* const*>(a4 + 24);
    const char* vs = *reinterpret_cast<const char* const*>(a4 + 32);
    if (!ps || !vs || std::strcmp(ps, "vertexlit_and_unlit_generic_ps50") != 0
        || std::strcmp(vs, "vertexlit_and_unlit_generic_vs40") != 0)
        return 1;
    const std::uint32_t stock = *reinterpret_cast<const std::uint32_t*>(a4 + 72) / 36;
    const std::uint32_t plain = stock & ~(kVlgArrayBit | kVlgDitherBit);
    if (plain != 4180 && plain != 4212 && plain != 135252)
        return 2;
    if ((stock & kVlgDitherBit) && !(stock & kVlgArrayBit))
        return 2;
    // VS statics 0..3 differ only in vertex colour / alpha; 0 and 2 have matching 48 output signatures.
    if (*reinterpret_cast<const std::uint32_t*>(a4 + 76) / 48 > 3)
        return 3;
    if (!ParamPointer(params, 32) || !ParamPointer(params, 136) || !ParamPointer(params, 144) || !ParamPointer(params, 248))
        return 4;
    if ((ParamDword(params, 4) & 0x2000000) || ParamPointer(params, 200) || ParamPointer(params, 216)
        || ParamPointer(params, 224) || ParamPointer(params, 232) || ParamPointer(params, 240))
        return 5;
    if ((ParamPointer(params, 88) && ParamDword(params, 104)) || (ParamPointer(params, 96) && ParamDword(params, 108)))
        return 6;
    if (plain == 4212 && !ParamPointer(params, 208))
        return 7;
    return 0;
}

__int64 __fastcall VlgHook(__int64 a1, int** a2, __int64 a3, __int64 a4)
{
    const __int64 result = s_vlgOriginal(a1, a2, a3, a4);
    if (!a2 || !*a2 || !a4)
        return result;
    const auto* params = reinterpret_cast<const unsigned char*>(*a2);
    const int reason = VlgEligible(params, a4);
    const std::uint32_t stock = *reinterpret_cast<const std::uint32_t*>(a4 + 72) / 36;
    if (s_uberLogEnabled) {
        // distinct (static, vs, reason, bound textures) histogram: first sight of each key is logged, totals at 64 / 512 / 4096 / 32768 states
        static std::mutex mutex;
        static std::map<std::uint64_t, std::uint32_t> histogram;
        static std::uint32_t total = 0;
        const std::uint32_t vsStatic = *reinterpret_cast<const std::uint32_t*>(a4 + 76) / 48;
        const std::uint64_t key = std::uint64_t(stock) | std::uint64_t(vsStatic & 0xFFF) << 20 | std::uint64_t(reason) << 32
            | std::uint64_t(ParamPointer(params, 136) != 0) << 40 | std::uint64_t(ParamPointer(params, 144) != 0) << 41
            | std::uint64_t(ParamPointer(params, 248) != 0) << 42 | std::uint64_t(ParamPointer(params, 208) != 0) << 43;
        std::lock_guard<std::mutex> lock(mutex);
        char line[256];
        const bool first = histogram[key]++ == 0;
        if (first) {
            _snprintf_s(line, sizeof(line), _TRUNCATE, "VLG static=%u vs=%u fmt=%016llx reason=%d env=%d bump=%d rt=%d selfmask=%d\n",
                stock, vsStatic, static_cast<unsigned long long>(*reinterpret_cast<const std::uint64_t*>(a4 + 16)), reason,
                ParamPointer(params, 136) != 0, ParamPointer(params, 144) != 0, ParamPointer(params, 248) != 0, ParamPointer(params, 208) != 0);
            LmgLog(line);
        }
        ++total;
        if (total == 64 || total == 512 || total == 4096 || total == 32768) {
            for (const auto& entry : histogram) {
                _snprintf_s(line, sizeof(line), _TRUNCATE, "VLG HIST total=%u static=%u vs=%u reason=%d env=%d bump=%d rt=%d selfmask=%d count=%u\n",
                    total, static_cast<unsigned>(entry.first & 0xFFFFF), static_cast<unsigned>((entry.first >> 20) & 0xFFF),
                    static_cast<int>((entry.first >> 32) & 0xFF), static_cast<int>((entry.first >> 40) & 1), static_cast<int>((entry.first >> 41) & 1),
                    static_cast<int>((entry.first >> 42) & 1), static_cast<int>((entry.first >> 43) & 1), entry.second);
                LmgLog(line);
            }
        }
    }
    if (reason || s_uberMode == UberMode::Log)
        return result;
    char name[96], pixelFile[112], macros[320];
    _snprintf_s(name, sizeof(name), _TRUNCATE, "r2_uber_vlg_s%u%s", stock, s_uberTag);
    _snprintf_s(pixelFile, sizeof(pixelFile), _TRUNCATE, "%s_ps40", name);
    const std::uint32_t plain = stock & ~(kVlgArrayBit | kVlgDitherBit);
    _snprintf_s(macros, sizeof(macros), _TRUNCATE, "R2_SELFILLUM=%d;R2_ATEST=%d;R2_ARR=%d;R2_DITHER=%d;%s",
        plain == 4212 ? 1 : 0, plain == 135252 ? 1 : 0, (stock & kVlgArrayBit) ? 1 : 0, (stock & kVlgDitherBit) ? 1 : 0, s_uberMacros);
    if (!EnsureUberShader(UberShader::VertexLitGeneric, name, macros)) {
        if (!s_vlgMissing.exchange(true))
            Warning("R1Delta: R2 VLG override build failed for '%s'; keeping the stock shader\n", name);
        return result;
    }
    *reinterpret_cast<const char**>(a4 + 24) = StableName(pixelFile);
    *reinterpret_cast<std::uint32_t*>(a4 + 72) = 0;  // one static, 24 dynamic records; slot 10 still chooses the dynamic
    if (s_vlgApplied.fetch_add(1, std::memory_order_relaxed) == 0)
        Msg("R1Delta: R2 VertexLitGeneric override active (%s)\n", name);
    return result;
}

void InstallVlgOverride(std::uintptr_t base)
{
    // LMG debug panels are not VLG panels. Keep stock props in those diagnostic modes.
    if (s_uberMode == UberMode::Debug)
        return;
    void* target = reinterpret_cast<void*>(base + kVlgStateRva);
    if (std::memcmp(target, kVlgPrologue, sizeof(kVlgPrologue)) != 0) {
        Warning("R1Delta: R2 VLG override not installed: VertexLitGeneric state function prologue mismatch\n");
        return;
    }
    const MH_STATUS create = MH_CreateHook(target, reinterpret_cast<void*>(&VlgHook), reinterpret_cast<void**>(&s_vlgOriginal));
    const MH_STATUS enable = create == MH_OK ? MH_EnableHook(target) : create;
    Msg("R1Delta: R2 VLG hook mode=%d tag=%s macros=%s create=%d enable=%d target=%p\n",
        static_cast<int>(s_uberMode), s_uberTag, s_uberMacros, static_cast<int>(create), static_cast<int>(enable), target);
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

void InstallUberOverrides(std::uintptr_t base)
{
    // Shared R2_UBER options. DEFAULT ON (round 19): unset or empty means the terms `so,lod,fenv,ggx` for world and props; `0` or `off` installs
    // neither hook and compiles nothing. `pack` stays opt-in.
    char mode[160] = {};
    const DWORD modeLength = GetEnvironmentVariableA("R2_UBER", mode, sizeof(mode));
    if (modeLength >= sizeof(mode))
        return;
    if (modeLength == 0 || !mode[0])
        std::snprintf(mode, sizeof(mode), "so,lod,fenv,ggx");
    else if (std::strcmp(mode, "0") == 0 || _stricmp(mode, "off") == 0)
        return;
    s_uberLogEnabled = std::strstr(mode, "log") != nullptr;
    std::string macros;
    std::string tag;
    bool anyTerm = false;
    auto term = [&](const char* name, const char* macro) {
        macros += macro;
        macros += "=1;";
        tag += "_";
        tag += name;
        anyTerm = true;
    };
    bool onlyProps = false;
    bool onlyWorld = false;
    s_uberMode = UberMode::Log;
    for (char* token = std::strtok(mode, ",+ "); token; token = std::strtok(nullptr, ",+ ")) {
        if (std::strcmp(token, "log") == 0)
            continue;
        if (std::strcmp(token, "props") == 0) {   // hook only VertexLitGeneric (props); world stays stock
            onlyProps = true;
            continue;
        }
        if (std::strcmp(token, "world") == 0) {   // hook only LightmappedGeneric (world); props stay stock
            onlyWorld = true;
            continue;
        }
        if (std::strcmp(token, "neutral") == 0)
            s_uberMode = UberMode::Neutral, macros += "R2_NEUTRAL=1;", tag += "_neutral";
        else if (std::strcmp(token, "mark") == 0)
            s_uberMode = UberMode::Mark, macros += "R2_NEUTRAL=1;R2_MARK=1;", tag += "_mark";
        else if (std::strncmp(token, "dbg", 3) == 0 && token[3] >= '6' && token[3] <= '8') {
            s_uberMode = UberMode::Debug;
            macros += std::string("R2_DEBUG=") + token[3] + ";";
            tag += std::string("_dbg") + token[3];
        }
        else if (std::strcmp(token, "all") == 0) {
            term("lod", "R2_T_LOD"), term("fenv", "R2_T_FENV"), term("so", "R2_T_SO"), term("ggx", "R2_T_GGX"), term("pack", "R2_T_PACK");
            s_lmgHasPack = true;
        }
        else if (std::strcmp(token, "lod") == 0)
            term("lod", "R2_T_LOD");
        else if (std::strcmp(token, "fenv") == 0)
            term("fenv", "R2_T_FENV");
        else if (std::strcmp(token, "so") == 0)
            term("so", "R2_T_SO");
        else if (std::strcmp(token, "ggx") == 0)
            term("ggx", "R2_T_GGX");
        else if (std::strcmp(token, "pack") == 0)
            term("pack", "R2_T_PACK"), s_lmgHasPack = true;
        else if (std::strncmp(token, "rough=", 6) == 0) {
            macros += std::string("R2_ROUGH_FORCE=") + (token + 6) + ";";
            tag += std::string("_r") + (token + 6);
            anyTerm = true;
        }
        else
            Warning("R1Delta: R2_UBER: unknown token '%s' ignored\n", token);
    }
    if (s_uberMode == UberMode::Log && anyTerm)
        s_uberMode = UberMode::Uber;
    // R2_T_PACK is set per material (only materials that carry the packed texture), the rest of the terms for every variant
    const std::string packOnly = "R2_T_PACK=1;";
    for (std::size_t at; (at = macros.find(packOnly)) != std::string::npos;)
        macros.erase(at, packOnly.size());
    std::snprintf(s_uberMacros, sizeof(s_uberMacros), "%s", macros.c_str());
    std::snprintf(s_uberTag, sizeof(s_uberTag), "%s", tag.c_str());
    s_lmgBase = base;
    if (!onlyWorld || onlyProps)
        InstallVlgOverride(base);  // independent of the optional LMG hook's prologue / MinHook result
    if (onlyProps && !onlyWorld)
        return;
    void* target = reinterpret_cast<void*>(base + kLmgStateRva);
    if (std::memcmp(target, kLmgPrologue, sizeof(kLmgPrologue)) != 0) {
        Warning("R1Delta: R2 LMG override not installed: LightmappedGeneric state function prologue mismatch\n");
        return;
    }
    const MH_STATUS create = MH_CreateHook(target, reinterpret_cast<void*>(&LmgHook), reinterpret_cast<void**>(&s_lmgOriginal));
    const MH_STATUS enable = create == MH_OK ? MH_EnableHook(target) : create;
    Msg("R1Delta: R2 LMG hook mode=%d tag=%s macros=%s create=%d enable=%d target=%p\n", static_cast<int>(s_uberMode), s_uberTag, s_uberMacros,
        static_cast<int>(create), static_cast<int>(enable), target);
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
    InstallUberOverrides(base);
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
