#include "r2_propfade.h"

#include "engine/core/core.h"
#include "engine/cvar.h"
#include "engine/filesystem/r2_map_provider.h"
#include "engine/filesystem/r2port_provider.h"
#include "engine/logging/logging.h"

#include <MinHook.h>
#include <Windows.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace r2port::propfade {
namespace {

// ---------------------------------------------------------------------------------------------------------------------
// Retail R1 engine.dll (9,358,848 bytes, SHA-256 0214A404...263C; checked at run time by PE time stamp, image size and the
// prologue of the one function we detour).
//
// sub_18018B710(prop, index, diskRecord, model) is the per-prop loader of CStaticPropMgr (called once per sprp record at level
// load). It stores the squared fade distance at prop+276: sprp fade (record +0x24) when > 0, else the model's own fade, else
// hull diagonal * model_defaultFadeDistScale (>= model_defaultFadeDistMin), then max(., 100)^2. The visible-prop gather
// (sub_18018C890) culls with dist^2 / zoom^2 > fade2 * ((1 + model_fadeRangeFraction)^2 * 0.996 - 0.0039) and starts the dither
// fade at dist^2 / zoom^2 > fade2.
//
// CStaticPropMgr instance 0x1807C6450: props pointer +56, prop count +80, "level initialised" byte +96, props are 288 bytes and
// carry (index | 0x40000000) at +72.
// ---------------------------------------------------------------------------------------------------------------------
constexpr std::uint32_t kEngineTimeDateStamp = 0x55038DAC;
constexpr std::uint32_t kEngineImageSize = 0x3264000;
constexpr std::uintptr_t kPropLoaderRva = 0x18B710;
constexpr unsigned char kPropLoaderPrologue[] = {
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
    0x41, 0x54, 0x41, 0x55, 0x48, 0x81, 0xEC, 0x60, 0x01, 0x00, 0x00,
};
constexpr std::uintptr_t kStaticPropMgrRva = 0x7C6450;
constexpr std::size_t kMgrPropsOffset = 56;
constexpr std::size_t kMgrCountOffset = 80;
constexpr std::size_t kMgrLevelFlagOffset = 96;
constexpr std::size_t kPropStride = 288;
constexpr std::size_t kPropTagOffset = 72;
constexpr std::size_t kPropFadeSqOffset = 276;
constexpr std::int32_t kMaxProps = 57343;

constexpr float kMinScale = 0.25f;
constexpr float kMaxScale = 8.0f;

using PropLoaderFn = char(__fastcall*)(std::int64_t prop, int index, std::int64_t diskRecord, std::int64_t model);

PropLoaderFn g_original = nullptr;
std::uintptr_t g_engineBase = 0;
ConVarR1* g_cvScale = nullptr;
// Scale baked into the props of the current level; 1 on a stock map. g_levelConverted tells whether the cvar may rescale them.
std::atomic<float> g_applied{ 1.0f };
std::atomic<bool> g_levelConverted{ false };
std::string g_levelMap;

float ClampScale(float value)
{
    if (!std::isfinite(value))
        return 1.0f;
    return value < kMinScale ? kMinScale : value > kMaxScale ? kMaxScale : value;
}

float CvarScale()
{
    return ClampScale(g_cvScale ? g_cvScale->m_Value.m_fValue : 1.0f);
}

struct MgrView {
    unsigned char* props;
    std::int32_t count;
    bool initialised;
};

MgrView ReadMgr()
{
    const auto* mgr = reinterpret_cast<const unsigned char*>(g_engineBase + kStaticPropMgrRva);
    MgrView view{};
    view.props = *reinterpret_cast<unsigned char* const*>(mgr + kMgrPropsOffset);
    view.count = *reinterpret_cast<const std::int32_t*>(mgr + kMgrCountOffset);
    view.initialised = *(mgr + kMgrLevelFlagOffset) != 0;
    return view;
}

char __fastcall PropLoaderHook(std::int64_t prop, int index, std::int64_t diskRecord, std::int64_t model)
{
    if (index == 0) {
        // First prop of a level: decide once. The provider records every opened maps/*.bsp, so the active map is the one loading.
        std::string map = r2port::ActiveMapName();
        const bool converted = r2port::IsR2PortMap(map);
        g_levelMap = std::move(map);
        g_levelConverted.store(converted);
        g_applied.store(converted ? CvarScale() : 1.0f);
        if (converted)
            Msg("R1Delta: R2 static props on %s: fade distance scale %.2f (r2port_prop_fade_scale)\n", g_levelMap.c_str(), g_applied.load());
    }
    const char result = g_original(prop, index, diskRecord, model);
    const float scale = g_applied.load(std::memory_order_relaxed);
    if (scale != 1.0f) {
        float* fadeSq = reinterpret_cast<float*>(static_cast<std::uintptr_t>(prop) + kPropFadeSqOffset);
        *fadeSq *= scale * scale;
    }
    return result;
}

// The cvar changed while a level is loaded: scale the already baked fade distances by new / applied.
void Rescale()
{
    if (!g_original || !g_levelConverted.load())
        return;
    const MgrView mgr = ReadMgr();
    if (!mgr.initialised || !mgr.props || mgr.count <= 0 || mgr.count > kMaxProps)
        return;
    const float from = g_applied.load();
    const float to = CvarScale();
    if (from == to)
        return;
    const float ratio = to / from;
    const float ratioSq = ratio * ratio;
    std::int32_t scaled = 0;
    for (std::int32_t i = 0; i < mgr.count; i++) {
        unsigned char* prop = mgr.props + static_cast<std::size_t>(i) * kPropStride;
        if (*reinterpret_cast<const std::int32_t*>(prop + kPropTagOffset) != (i | 0x40000000))
            break;
        *reinterpret_cast<float*>(prop + kPropFadeSqOffset) *= ratioSq;
        scaled++;
    }
    g_applied.store(to);
    Msg("R1Delta: R2 static prop fade distance scale %.2f -> %.2f on %d of %d props\n", from, to, scaled, mgr.count);
}

void OnCvarChanged(void*, const char*, float)
{
    Rescale();
}

void StatusCommand(const CCommand&)
{
    const MgrView mgr = ReadMgr();
    Msg("R1Delta: R2 prop fade: hook=%s level=%s converted=%d applied=%.3f cvar=%.3f props=%d initialised=%d\n",
        g_original ? "installed" : "not installed", g_levelMap.empty() ? "(none)" : g_levelMap.c_str(),
        g_levelConverted.load() ? 1 : 0, g_applied.load(), CvarScale(), mgr.count, mgr.initialised ? 1 : 0);
    if (mgr.initialised && mgr.props && mgr.count > 0 && mgr.count <= kMaxProps) {
        const float* first = reinterpret_cast<const float*>(mgr.props + kPropFadeSqOffset);
        Msg("  prop 0 fade distance %.1f (squared %.1f)\n", std::sqrt(*first), *first);
    }
}

bool VerifyEngine(std::uintptr_t base)
{
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64
        || nt->FileHeader.TimeDateStamp != kEngineTimeDateStamp || nt->OptionalHeader.SizeOfImage != kEngineImageSize)
        return false;
    return std::memcmp(reinterpret_cast<const void*>(base + kPropLoaderRva), kPropLoaderPrologue, sizeof(kPropLoaderPrologue)) == 0;
}

#ifdef R2_PROPFADE_TRACE
// ---------------------------------------------------------------------------------------------------------------------
// Measurement-only (private build, never shipped): detours the per-view visible-prop gather sub_18018C890(mgr, job, view) and
// dumps, per call after `r2port_prop_fade_dump`, the view zoom / origin and one CSV row per static prop: whether the prop was
// in the visibility bitset before the gather, whether it survived, its LOD byte, dither flag and alpha.
// view (CWorldRenderList): origin +180280, zoom +180292, word count +196720, prop bitset +196736 + 8 * wordCount,
// visible entries +49208 + 8 * i {u16 prop, u8 lod, u8 dither, float alpha}, visible count +32808.
// ---------------------------------------------------------------------------------------------------------------------
constexpr std::uintptr_t kGatherRva = 0x18C890;
using GatherFn = std::int64_t(__fastcall*)(std::int64_t mgr, unsigned int job, std::int64_t view);
GatherFn g_gatherOriginal = nullptr;
std::atomic<int> g_traceArmed{ 0 };
std::atomic<int> g_traceSeq{ 0 };

std::int64_t __fastcall GatherHook(std::int64_t mgr, unsigned int job, std::int64_t view)
{
    if (g_traceArmed.load() <= 0)
        return g_gatherOriginal(mgr, job, view);
    const int seq = g_traceSeq.fetch_add(1);
    const unsigned char* v = reinterpret_cast<const unsigned char*>(view);
    const unsigned char* props = *reinterpret_cast<unsigned char* const*>(mgr + 48);
    const int count = *reinterpret_cast<const int*>(mgr + 72);
    const unsigned wordCount = *reinterpret_cast<const unsigned*>(v + 196720);
    const unsigned long long* bits = reinterpret_cast<const unsigned long long*>(v + 196736 + 8ull * wordCount);
    std::string before((static_cast<std::size_t>(count) + 7) / 8, '\0');
    for (int i = 0; i < count; i++)
        if ((bits[i >> 6] >> (i & 63)) & 1ull)
            before[i >> 3] |= static_cast<char>(1 << (i & 7));
    const std::int64_t result = g_gatherOriginal(mgr, job, view);
    const float zoom = *reinterpret_cast<const float*>(v + 180292);
    const float* origin = reinterpret_cast<const float*>(v + 180280);
    const unsigned visible = *reinterpret_cast<const unsigned*>(v + 32808);
    std::string vis(static_cast<std::size_t>(count), '\0');
    std::string lod(static_cast<std::size_t>(count), '\0');
    std::string dith(static_cast<std::size_t>(count), '\0');
    std::string alpha(static_cast<std::size_t>(count) * 4, '\0');
    unsigned dithered = 0, noLod = 0;
    for (unsigned i = 0; i < visible && i < 0x4000; i++) {
        const unsigned char* e = v + 49208 + 8ull * i;
        const unsigned idx = *reinterpret_cast<const unsigned short*>(e);
        if (idx >= static_cast<unsigned>(count))
            continue;
        vis[idx] = 1;
        lod[idx] = static_cast<char>(e[2]);
        dith[idx] = static_cast<char>(e[3]);
        std::memcpy(&alpha[static_cast<std::size_t>(idx) * 4], e + 4, 4);
        if (e[3]) dithered++;
        if (e[2] == 0xFF) noLod++;
    }
    unsigned pvs = 0;
    for (int i = 0; i < count; i++)
        if ((before[i >> 3] >> (i & 7)) & 1) pvs++;
    char path[MAX_PATH]{};
    _snprintf_s(path, sizeof(path), _TRUNCATE, "propfade_dump_%d.csv", seq);
    FILE* f = nullptr;
    if (fopen_s(&f, path, "w") == 0 && f) {
        std::fprintf(f, "# seq %d map %s applied_scale %.4f zoom %.5f origin %.2f %.2f %.2f props %d pvs_bits %u visible %u dithered %u nolod %u job %u\n",
            seq, g_levelMap.c_str(), g_applied.load(), zoom, origin[0], origin[1], origin[2], count, pvs, visible, dithered, noLod, job);
        std::fprintf(f, "idx,x,y,z,fade,pvs,vis,lod,dither,alpha\n");
        for (int i = 0; i < count; i++) {
            const unsigned char* p = props + static_cast<std::size_t>(i) * kPropStride;
            const float* o = reinterpret_cast<const float*>(p + 16);
            const float fsq = *reinterpret_cast<const float*>(p + kPropFadeSqOffset);
            float a = 0.0f;
            std::memcpy(&a, &alpha[static_cast<std::size_t>(i) * 4], 4);
            std::fprintf(f, "%d,%.2f,%.2f,%.2f,%.2f,%d,%d,%d,%d,%.4f\n", i, o[0], o[1], o[2], std::sqrt(fsq),
                (before[i >> 3] >> (i & 7)) & 1, static_cast<int>(vis[i]), static_cast<int>(static_cast<unsigned char>(lod[i])),
                static_cast<int>(dith[i]), a);
        }
        std::fclose(f);
    }
    Msg("R1Delta: prop fade trace %d: map %s scale %.3f zoom %.4f origin %.1f %.1f %.1f props %d pvs %u visible %u dithered %u nolod %u job %u -> %s\n",
        seq, g_levelMap.c_str(), g_applied.load(), zoom, origin[0], origin[1], origin[2], count, pvs, visible, dithered, noLod, job, path);
    g_traceArmed.fetch_sub(1);
    return result;
}

void DumpCommand(const CCommand& args)
{
    int n = 1;
    if (args.ArgC() > 1)
        n = std::atoi(args.Arg(1));
    g_traceSeq.store(0);
    g_traceArmed.store(n < 1 ? 1 : n);
    Msg("R1Delta: prop fade trace armed for %d gather call(s)\n", n < 1 ? 1 : n);
}

void InstallTrace(std::uintptr_t engineBase)
{
    void* target = reinterpret_cast<void*>(engineBase + kGatherRva);
    if (MH_CreateHook(target, &GatherHook, reinterpret_cast<LPVOID*>(&g_gatherOriginal)) == MH_OK)
        MH_EnableHook(target);
    RegisterConCommand("r2port_prop_fade_dump", DumpCommand, "Dump the next N static prop gather calls to propfade_dump_<n>.csv (measurement build).", FCVAR_CLIENTDLL);
}
#endif

} // namespace

void InstallEngine(std::uintptr_t engineBase)
{
    if (!engineBase || g_engineBase)
        return;
    g_engineBase = engineBase;

    g_cvScale = RegisterConVar("r2port_prop_fade_scale", "2.25", FCVAR_CLIENTDLL | FCVAR_ARCHIVE_PLAYERPROFILE,
        "Fade distance scale of static props on converted R2 maps (R2 Model Draw Distance: 1 Normal, 1.5 Far, 2.25 Very Far). "
        "Stock R1 maps are unchanged. Applies to the loaded level at once.");
    if (g_cvScale) {
        g_cvScale->m_bHasMin = true;
        g_cvScale->m_fMinVal = kMinScale;
        g_cvScale->m_bHasMax = true;
        g_cvScale->m_fMaxVal = kMaxScale;
        g_cvScale->m_fnChangeCallbacks.AddToTail(OnCvarChanged);
    }
    RegisterConCommand("r2port_prop_fade_status", StatusCommand, "Show the R2 static prop fade distance scale state.", FCVAR_CLIENTDLL);

    if (!VerifyEngine(engineBase)) {
        Warning("R1Delta: R2 static prop fade unavailable: engine.dll is not the retail R1 build or the prop loader bytes differ; r2port_prop_fade_scale does nothing\n");
        return;
    }
    void* target = reinterpret_cast<void*>(engineBase + kPropLoaderRva);
    const MH_STATUS status = MH_CreateHook(target, &PropLoaderHook, reinterpret_cast<LPVOID*>(&g_original));
    if (status != MH_OK && status != MH_ERROR_ALREADY_CREATED) {
        Warning("R1Delta: failed to hook the static prop loader (%d)\n", static_cast<int>(status));
        g_original = nullptr;
        return;
    }
    const MH_STATUS enableStatus = MH_EnableHook(target);
    if (enableStatus != MH_OK) {
        Warning("R1Delta: failed to enable the static prop loader hook (%d)\n", static_cast<int>(enableStatus));
        g_original = nullptr;
    }
#ifdef R2_PROPFADE_TRACE
    InstallTrace(engineBase);
#endif
}

} // namespace r2port::propfade
