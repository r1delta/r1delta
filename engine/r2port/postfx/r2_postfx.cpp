#include "r2_postfx.h"

#include "engine/core/core.h"
#include "engine/cvar.h"
#include "engine/filesystem/r2_map_provider.h"
#include "engine/filesystem/r2port_provider.h"
#include "engine/logging/logging.h"
#include "engine/r2port/shaders/r2_shader_cache.h"

#include <MinHook.h>
#include <Windows.h>
#include <d3d11.h>

#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace r2port::postfx {
namespace {

// ---------------------------------------------------------------------------------------------------------------------
// Retail R1 client.dll (checked at run time: PE time stamp, image size and the bytes of every function we touch).
//
// sub_1802AA380 is the client's post pass. It copies the frame into _rt_FullFrameFB, runs the bloom chain, draws the
// engine_post material (IMatRenderContext::DrawScreenSpaceRectangle, vtable +936, dest 0,0,w,h) into the current render
// target and returns 1 when that draw was issued; it is a Push/Pop-balanced function, so when it returns the same target is
// still bound. The HUD (VGUI) is drawn later in the frame. Under mat_queue_mode the first call only queues itself and
// returns 0, the queued call on the render thread does the work.
// ---------------------------------------------------------------------------------------------------------------------
constexpr std::uint32_t kClientTimeDateStamp = 0x55038DB8;
constexpr std::uint32_t kClientImageSize = 0x39A4000;
constexpr std::uintptr_t kPostPassRva = 0x2AA380;
constexpr std::uintptr_t kFullFrameFbRva = 0x245C70;     // sub_180245C70(int index): ITexture* of _rt_FullFrameFB[index]
constexpr std::uintptr_t kMaterialsRva = 0x380E688;      // qword_18380E688: IMaterialSystem*
constexpr std::uintptr_t kHalfDestCvarRva = 0x150B568;   // qword_18150B568: ConVar* that halves the engine_post dest rect
constexpr unsigned char kPostPassPrologue[] = {
    0x40, 0x56, 0x48, 0x81, 0xEC, 0xF0, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xF1, 0xE8, 0x4F, 0xE4, 0xFF, 0xFF,
    0x80, 0x3D, 0xA9, 0x03, 0x26, 0x01, 0x00,
};
constexpr unsigned char kFullFramePrologue[] = {
    0x40, 0x53, 0x48, 0x81, 0xEC, 0x20, 0x01, 0x00, 0x00, 0x48, 0x8D, 0x15, 0xB8, 0xBF, 0x1E, 0x01,
    0x48, 0x63, 0xC1, 0x48, 0x83, 0x3C, 0xC2, 0x00,
};

// Retail R1 materialsystem_dx11.dll: only used for the one-time D3D11 target format log.
constexpr std::uint32_t kMatSystemTimeDateStamp = 0x54874238;
constexpr std::uint32_t kMatSystemImageSize = 0x3E0000;
constexpr std::uintptr_t kMatSystemImmediateContextRva = 0x290D90;

// Virtual function offsets (bytes) of the engine interfaces, taken from the client's own calls in sub_1802AA380.
constexpr std::size_t kMaterialsFindMaterial = 680;
constexpr std::size_t kMaterialsGetRenderContext = 896;
constexpr std::size_t kContextRelease = 8;
constexpr std::size_t kMaterialIsFinished = 768;   // sub_180039FA0: (flags & 2), the material finished loading
constexpr std::size_t kMaterialReference = 920;    // sub_1800378C0: marks the material in use
constexpr std::size_t kMaterialFinish = 320;       // sub_18003AD80: precache and finish (what Bind does for a new material)
constexpr std::size_t kContextBegin = 16;
constexpr std::size_t kContextEnd = 24;
constexpr std::size_t kContextGetRenderTargetSize = 96;
constexpr std::size_t kContextDrawScreenSpaceRectangle = 936;
constexpr std::size_t kContextCopyRenderTargetToTexture = 976;
constexpr std::size_t kMaterialGetName = 0;
constexpr std::size_t kMaterialFindVar = 432;
constexpr std::size_t kMaterialSetFloat = 608;
constexpr std::size_t kTextureWidth = 24;
constexpr std::size_t kTextureHeight = 32;

constexpr const char* kMaterialName = "r1d/post";
constexpr std::string_view kVmtPath = "materials/r1d/post.vmt";
constexpr const char* kShaderName = "r1d_post";

template <typename Function>
Function Virtual(void* object, std::size_t byteOffset)
{
    return reinterpret_cast<Function>((*reinterpret_cast<void***>(object))[byteOffset / sizeof(void*)]);
}

void LogLine(const char* format, ...)
{
    char text[1024];
    va_list args;
    va_start(args, format);
    _vsnprintf_s(text, sizeof(text), _TRUNCATE, format, args);
    va_end(args);
    Msg("%s", text);
    OutputDebugStringA(text);
}

void WarnLine(const char* format, ...)
{
    char text[1024];
    va_list args;
    va_start(args, format);
    _vsnprintf_s(text, sizeof(text), _TRUNCATE, format, args);
    va_end(args);
    Warning("%s", text);
    OutputDebugStringA(text);
}

// ---------------------------------------------------------------------------------------------------------------------
// VMT provider
// ---------------------------------------------------------------------------------------------------------------------
std::string BuildVmt()
{
    // screenspace_general reads $basetexture at t0 and writes the current render target; the target view is an sRGB view, so
    // the hardware decodes on read and encodes on write and the shader converts back and forth (c0.w = 1, see the shader).
    return
        "\"screenspace_general\"\n"
        "{\n"
        "\t\"$pixshader\" \"r1d_post\"\n"
        "\t\"$basetexture\" \"_rt_FullFrameFB\"\n"
        "\t\"$ignorez\" \"1\"\n"
        "\t\"$additive\" \"0\"\n"
        "\t\"$linearread_basetexture\" \"1\"\n"
        "\t\"$linearwrite\" \"1\"\n"
        "\t\"%noToolTexture\" \"1\"\n"
        "\t\"$c0_x\" \"0\"\n\t\"$c0_y\" \"0.5\"\n\t\"$c0_z\" \"1\"\n\t\"$c0_w\" \"1\"\n"
        "\t\"$c1_x\" \"0\"\n\t\"$c1_y\" \"1\"\n\t\"$c1_z\" \"1\"\n"
        "}\n";
}

FileBytes MakeBytes(const std::string& text)
{
    return std::make_shared<const std::vector<std::uint8_t>>(text.begin(), text.end());
}

class PostFxMaterialProvider final : public FileProvider {
public:
    PostFxMaterialProvider() : m_vmt(MakeBytes(BuildVmt())) {}

    bool MayContain(std::string_view path) const override { return Pick(path) != nullptr; }
    bool CanCacheMisses() const override { return true; }
    bool Exists(std::string_view path) override { return Pick(path) != nullptr; }

    std::optional<std::uint64_t> Size(std::string_view path) override
    {
        const FileBytes bytes = Pick(path);
        return bytes ? std::optional<std::uint64_t>(bytes->size()) : std::nullopt;
    }

    FileBytes Read(std::string_view path) override
    {
        const FileBytes bytes = Pick(path);
        if (bytes && !m_logged.exchange(true))
            LogLine("R1Delta: PostFx served %.*s (%zu bytes)\n", static_cast<int>(path.size()), path.data(), bytes->size());
        return bytes;
    }

private:
    FileBytes Pick(std::string_view path) const
    {
        return path == kVmtPath ? m_vmt : FileBytes{};
    }

    FileBytes m_vmt;
    std::atomic<bool> m_logged{};
};

std::atomic<bool> g_providerRegistered{};

// ---------------------------------------------------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------------------------------------------------
using PostPassFn = char(__fastcall*)(int* view);
using FullFrameFn = void*(__fastcall*)(int index);
using FindMaterialFn = void*(__fastcall*)(void* materials, const char* name, const char* group, int context, char complain, const char* prefix);
using GetContextFn = void*(__fastcall*)(void* materials);
using ContextCallFn = void(__fastcall*)(void* context);
using GetTargetSizeFn = void(__fastcall*)(void* context, int* width, int* height);
using CopyTargetFn = void(__fastcall*)(void* context, void* texture, unsigned int flags, const int* source, const int* destination);
using DrawRectFn = void(__fastcall*)(void* context, void* material, int x, int y, int width, int height,
    float sx0, float sy0, float sx1, float sy1, int textureWidth, int textureHeight, void* renderable, int xDice, int yDice);
using GetNameFn = const char*(__fastcall*)(void* material);
using FindVarFn = unsigned short(__fastcall*)(void* material, const char* name, int a, int b);
using SetFloatFn = void(__fastcall*)(void* material, unsigned short index, float value);
using IntGetterFn = int(__fastcall*)(void* object);
using FlagGetterFn = unsigned char(__fastcall*)(void* object);
using VoidMethodFn = void(__fastcall*)(void* object);

constexpr const char* kVarNames[] = { "$c0_x", "$c0_y", "$c0_z", "$c0_w", "$c1_x", "$c1_y", "$c1_z" };
constexpr int kVarCount = static_cast<int>(sizeof(kVarNames) / sizeof(kVarNames[0]));

std::uintptr_t g_clientBase = 0;
bool g_buildOk = false;                 // client stamp and function bytes verified
PostPassFn g_original = nullptr;
void* g_target = nullptr;
bool g_hookCreated = false;
std::atomic<bool> g_active{};           // hook enabled and the cvars ask for work
std::atomic<bool> g_dead{};             // fail closed after a fault: never draws again this session
std::mutex g_reconcileMutex;

ConVarR1* g_cvCurve = nullptr;
ConVarR1* g_cvAmount = nullptr;
ConVarR1* g_cvDenoise = nullptr;
ConVarR1* g_cvWidth = nullptr;
ConVarR1* g_cvStats = nullptr;
ConVarR1* g_cvSrgb = nullptr;

// Material state. The set is built on the main thread (PrepareMaterialOnMainThread) and published for the render thread.
struct MaterialSet {
    void* material = nullptr;
    unsigned short index[kVarCount]{};
    float last[kVarCount]{};     // render thread only
};
std::atomic<const MaterialSet*> g_set{ nullptr };
bool g_materialFailed = false;   // main thread only: the material did not load (not retried until the next activation)
DWORD g_mainThreadId = 0;

std::atomic<unsigned long long> g_passCount{};
std::atomic<unsigned long long> g_skipCount{};
std::atomic<int> g_lastSkipReason{};
std::atomic<unsigned int> g_skipLogged{};   // bit per reason: warn once

enum SkipReason {
    SkipNone = 0,
    SkipViewOrigin = 1,
    SkipViewSize = 2,
    SkipHalfDest = 3,
    SkipTargetSize = 4,
    SkipNoMaterial = 5,
    SkipNoTexture = 6,
    SkipNoContext = 7,
    SkipNotReady = 8,
};

const char* SkipText(int reason)
{
    switch (reason) {
    case SkipViewOrigin: return "the view rectangle does not start at 0,0 (split screen)";
    case SkipViewSize: return "the view rectangle is empty";
    case SkipHalfDest: return "the engine_post destination is halved";
    case SkipTargetSize: return "the render target is not the size of _rt_FullFrameFB or smaller than the view";
    case SkipNoMaterial: return "the r1d/post material is not usable";
    case SkipNoTexture: return "_rt_FullFrameFB is not available";
    case SkipNoContext: return "no render context";
    case SkipNotReady: return "the main thread has not prepared the r1d/post material yet";
    default: return "";
    }
}

void NoteSkip(int reason)
{
    g_skipCount.fetch_add(1, std::memory_order_relaxed);
    g_lastSkipReason.store(reason, std::memory_order_relaxed);
    if (reason == SkipNotReady)
        return;   // normal for the first frame after a cvar change: the main thread prepares the material before the render thread draws
    const unsigned int bit = 1u << reason;
    if (!(g_skipLogged.fetch_or(bit) & bit))
        WarnLine("R1Delta: PostFx skipped the post pass: %s (reported once per reason)\n", SkipText(reason));
}

float CvarFloat(const ConVarR1* variable, float fallback)
{
    return variable ? variable->m_Value.m_fValue : fallback;
}

float Clamp01(float value)
{
    return std::isfinite(value) ? (value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value) : 0.0f;
}

// ---------------------------------------------------------------------------------------------------------------------
// Optional diagnostics: the D3D11 format of the target at the seam (logged once) and the frame time (r2port_postfx_stats 1).
// There is deliberately no GPU timestamp query: with queries issued from the pass the NVIDIA driver faulted on its own worker
// thread (nvwgf2umx, read of address 0x18) within seconds in two runs; without them neither run faulted.
// ---------------------------------------------------------------------------------------------------------------------
ID3D11DeviceContext* g_d3dContext = nullptr;
bool g_formatsLogged = false;
bool g_d3dFailed = false;
LARGE_INTEGER g_frequency{};
LARGE_INTEGER g_lastFrame{};
double g_frameSumMs = 0.0;
unsigned g_frameSamples = 0;

// Appends the stats line to <gamedir>/r2port/postfx_stats.txt as well, so a test harness can read it without the console.
void AppendStatsFile(unsigned frames, double frameMs, float curve, float amount)
{
    const std::filesystem::path directory = GetR2MapPatchDirectory();
    if (directory.empty())
        return;
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    FILE* file = nullptr;
    if (_wfopen_s(&file, (directory / "postfx_stats.txt").c_str(), L"ab") != 0 || !file)
        return;
    fprintf(file, "frames=%u frame_ms=%.3f fps=%.1f curve=%.2f sharpen=%.2f\n", frames, frameMs, 1000.0 / frameMs, curve, amount);
    fclose(file);
}

ID3D11DeviceContext* MatSystemContext()
{
    if (g_d3dContext || g_d3dFailed)
        return g_d3dContext;
    const HMODULE module = GetModuleHandleW(L"materialsystem_dx11.dll");
    if (!module) {
        g_d3dFailed = true;
        return nullptr;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || nt->Signature != IMAGE_NT_SIGNATURE
        || nt->FileHeader.TimeDateStamp != kMatSystemTimeDateStamp || nt->OptionalHeader.SizeOfImage != kMatSystemImageSize) {
        g_d3dFailed = true;
        return nullptr;
    }
    g_d3dContext = *reinterpret_cast<ID3D11DeviceContext**>(base + kMatSystemImmediateContextRva);
    if (!g_d3dContext)
        g_d3dFailed = true;
    return g_d3dContext;
}

const char* FormatName(DXGI_FORMAT format)
{
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return "R8G8B8A8_UNORM_SRGB";
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return "R8G8B8A8_TYPELESS";
    case DXGI_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return "B8G8R8A8_UNORM_SRGB";
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return "B8G8R8A8_TYPELESS";
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return "R16G16B16A16_FLOAT";
    case DXGI_FORMAT_R10G10B10A2_UNORM: return "R10G10B10A2_UNORM";
    case DXGI_FORMAT_R11G11B10_FLOAT: return "R11G11B10_FLOAT";
    case DXGI_FORMAT_R16G16B16A16_UNORM: return "R16G16B16A16_UNORM";
    default: return "other";
    }
}

void DescribeResource(ID3D11Resource* resource, char* out, size_t outSize)
{
    out[0] = 0;
    if (!resource)
        return;
    ID3D11Texture2D* texture = nullptr;
    if (SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture))) && texture) {
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        _snprintf_s(out, outSize, _TRUNCATE, "res=%s %ux%u samples=%u mips=%u", FormatName(desc.Format), desc.Width, desc.Height,
            desc.SampleDesc.Count, desc.MipLevels);
        texture->Release();
    }
}

// Logs the view format and the texture of the bound render target (call right after the draw). The sampled side is not logged:
// the engine rebinds t0 after the draw, so reading it back here would describe the wrong texture. Its encoding follows from
// the output (docs/r2port.md, "Post effects").
void LogSeamFormats(ID3D11DeviceContext* context, const char* materialName)
{
    if (g_formatsLogged)
        return;
    g_formatsLogged = true;
    char rtDesc[160]{};
    int rtFormat = -1;
    ID3D11RenderTargetView* rtv = nullptr;
    context->OMGetRenderTargets(1, &rtv, nullptr);
    if (rtv) {
        D3D11_RENDER_TARGET_VIEW_DESC desc{};
        rtv->GetDesc(&desc);
        rtFormat = static_cast<int>(desc.Format);
        ID3D11Resource* resource = nullptr;
        rtv->GetResource(&resource);
        DescribeResource(resource, rtDesc, sizeof(rtDesc));
        if (resource) resource->Release();
        rtv->Release();
    }
    LogLine("R1Delta: PostFx seam target material=%s view=%s (%d) %s\n", materialName,
        rtFormat < 0 ? "none" : FormatName(static_cast<DXGI_FORMAT>(rtFormat)), rtFormat, rtDesc);
}

// ---------------------------------------------------------------------------------------------------------------------
// The pass
// ---------------------------------------------------------------------------------------------------------------------
void* FindPostMaterial(void* materials)
{
    void* material = Virtual<FindMaterialFn>(materials, kMaterialsFindMaterial)(materials, kMaterialName, nullptr, 10, 1, nullptr);
    if (!material)
        return nullptr;
    const char* actual = Virtual<GetNameFn>(material, kMaterialGetName)(material);
    if (!actual || _stricmp(actual, kMaterialName) != 0)
        return nullptr;   // the engine answered with its error material: the VMT or the shader did not load
    return material;
}

// Runs on the main thread only (the thread that loaded client.dll and runs the frame). FindMaterial and the first FindVar
// (which precaches the material) must not run on the render thread: the render thread would ask the main thread to finish the
// material and wait for it while the main thread waits for the render thread (deadlock seen in the first test run).
void PrepareMaterialOnMainThread()
{
    if (g_materialFailed || g_set.load(std::memory_order_acquire))
        return;
    void* const materials = *reinterpret_cast<void**>(g_clientBase + kMaterialsRva);
    if (!materials)
        return;
    void* material = FindPostMaterial(materials);
    if (material && !Virtual<FlagGetterFn>(material, kMaterialIsFinished)(material)) {
        // Bind() does this on the render thread for a material it has not seen; that finishes the material through a call into
        // the main thread and waits for it, which deadlocks while the main thread waits for the render thread.
        Virtual<VoidMethodFn>(material, kMaterialReference)(material);
        Virtual<VoidMethodFn>(material, kMaterialFinish)(material);
        if (!Virtual<FlagGetterFn>(material, kMaterialIsFinished)(material))
            material = nullptr;
    }
    auto* set = material ? new MaterialSet{} : nullptr;
    if (set) {
        set->material = material;
        for (int i = 0; i < kVarCount; ++i) {
            set->index[i] = Virtual<FindVarFn>(material, kMaterialFindVar)(material, kVarNames[i], 1, 0);
            set->last[i] = std::nanf("");
            if (set->index[i] == 0xFFFF) {
                delete set;
                set = nullptr;
                break;
            }
        }
    }
    if (!set) {
        g_materialFailed = true;
        WarnLine("R1Delta: PostFx disabled: material %s did not load or has no $c0_x..$c1_z parameters (shader '%s' missing or VMT not served); "
            "stock look kept\n", kMaterialName, kShaderName);
        return;
    }
    g_set.store(set, std::memory_order_release);
    LogLine("R1Delta: PostFx material %s ready\n", kMaterialName);
}

void SetMaterialVars(MaterialSet* set, const float (&values)[kVarCount])
{
    const SetFloatFn setFloat = Virtual<SetFloatFn>(set->material, kMaterialSetFloat);
    for (int i = 0; i < kVarCount; ++i) {
        if (!(values[i] == set->last[i])) {
            setFloat(set->material, set->index[i], values[i]);
            set->last[i] = values[i];
        }
    }
}

void ApplyPass(int* view)
{
    const float curve = Clamp01(CvarFloat(g_cvCurve, 0.0f));
    const float amount = Clamp01(CvarFloat(g_cvAmount, 0.0f));
    const bool stats = g_cvStats && g_cvStats->m_Value.m_nValue != 0;
    if (curve <= 0.0f && amount <= 0.0f && !stats)
        return;

    // frame interval (the post pass runs once per rendered frame)
    if (stats) {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        if (g_lastFrame.QuadPart) {
            g_frameSumMs += static_cast<double>(now.QuadPart - g_lastFrame.QuadPart) * 1000.0 / static_cast<double>(g_frequency.QuadPart);
            ++g_frameSamples;
        }
        g_lastFrame = now;
        if (g_frameSamples >= 300) {
            const double avg = g_frameSumMs / g_frameSamples;
            LogLine("R1Delta: PostFx stats frames=%u frame_ms=%.3f fps=%.1f curve=%.2f sharpen=%.2f\n",
                g_frameSamples, avg, 1000.0 / avg, curve, amount);
            AppendStatsFile(g_frameSamples, avg, curve, amount);
            g_frameSumMs = 0.0;
            g_frameSamples = 0;
        }
    }
    else {
        g_lastFrame.QuadPart = 0;
    }
    if (curve <= 0.0f && amount <= 0.0f)
        return;

    if (view[0] != 0 || view[1] != 0) { NoteSkip(SkipViewOrigin); return; }
    const int width = view[2];
    const int height = view[3];
    if (width <= 0 || height <= 0) { NoteSkip(SkipViewSize); return; }
    if (const std::uintptr_t halfCvar = *reinterpret_cast<std::uintptr_t*>(g_clientBase + kHalfDestCvarRva)) {
        if (*reinterpret_cast<int*>(halfCvar + 92) != 0) { NoteSkip(SkipHalfDest); return; }
    }

    void* const materials = *reinterpret_cast<void**>(g_clientBase + kMaterialsRva);
    if (!materials) { NoteSkip(SkipNoContext); return; }
    const MaterialSet* const published = g_set.load(std::memory_order_acquire);
    if (!published) { NoteSkip(SkipNotReady); return; }
    MaterialSet* const set = const_cast<MaterialSet*>(published);   // `last` is touched by the render thread only

    void* const frameBuffer = reinterpret_cast<FullFrameFn>(g_clientBase + kFullFrameFbRva)(0);
    if (!frameBuffer) { NoteSkip(SkipNoTexture); return; }

    void* const context = Virtual<GetContextFn>(materials, kMaterialsGetRenderContext)(materials);
    if (!context) { NoteSkip(SkipNoContext); return; }
    Virtual<ContextCallFn>(context, kContextBegin)(context);

    int targetWidth = 0, targetHeight = 0;
    Virtual<GetTargetSizeFn>(context, kContextGetRenderTargetSize)(context, &targetWidth, &targetHeight);
    const int textureWidth = Virtual<IntGetterFn>(frameBuffer, kTextureWidth)(frameBuffer);
    const int textureHeight = Virtual<IntGetterFn>(frameBuffer, kTextureHeight)(frameBuffer);
    if (targetWidth != textureWidth || targetHeight != textureHeight || width > targetWidth || height > targetHeight) {
        Virtual<ContextCallFn>(context, kContextEnd)(context);
        Virtual<ContextCallFn>(context, kContextRelease)(context);
        NoteSkip(SkipTargetSize);
        return;
    }

    const float values[kVarCount] = {
        amount,
        Clamp01(CvarFloat(g_cvDenoise, 0.5f)),
        std::isfinite(CvarFloat(g_cvWidth, 1.0f)) ? (CvarFloat(g_cvWidth, 1.0f) < 1.0f ? 1.0f : CvarFloat(g_cvWidth, 1.0f) > 4.0f ? 4.0f : CvarFloat(g_cvWidth, 1.0f)) : 1.0f,
        (g_cvSrgb && g_cvSrgb->m_Value.m_nValue == 0) ? 0.0f : 1.0f,
        curve,
        static_cast<float>(width),
        static_cast<float>(height),
    };
    SetMaterialVars(set, values);

    ID3D11DeviceContext* const d3d = g_formatsLogged ? nullptr : MatSystemContext();

    // The final image is in the target now: copy it into _rt_FullFrameFB (the engine_post input, free again) and draw it back
    // through the shader. Same source and destination rectangle, so texel (x, y) of the copy is pixel (x, y) of the target.
    const int rect[4] = { 0, 0, width, height };
    Virtual<CopyTargetFn>(context, kContextCopyRenderTargetToTexture)(context, frameBuffer, 0, rect, rect);
    Virtual<DrawRectFn>(context, kContextDrawScreenSpaceRectangle)(context, set->material, 0, 0, width, height, 0.0f, 0.0f,
        static_cast<float>(width), static_cast<float>(height), textureWidth, textureHeight, nullptr, 1, 1);

    if (d3d && !g_formatsLogged)
        LogSeamFormats(d3d, kMaterialName);

    Virtual<ContextCallFn>(context, kContextEnd)(context);
    Virtual<ContextCallFn>(context, kContextRelease)(context);
    g_passCount.fetch_add(1, std::memory_order_relaxed);
}

unsigned long GuardedApply(int* view)
{
    __try {
        ApplyPass(view);
        return 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}

unsigned long GuardedPrepare()
{
    __try {
        PrepareMaterialOnMainThread();
        return 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}

char __fastcall PostPassHook(int* view)
{
    if (g_active.load(std::memory_order_relaxed) && !g_dead.load(std::memory_order_relaxed) && GetCurrentThreadId() == g_mainThreadId) {
        const unsigned long fault = GuardedPrepare();
        if (fault) {
            g_dead.store(true);
            g_active.store(false);
            WarnLine("R1Delta: PostFx faulted (exception 0x%08lX) while preparing the material; it is switched off for this session.\n", fault);
        }
    }
    const char result = g_original(view);
    if (result && g_active.load(std::memory_order_relaxed) && !g_dead.load(std::memory_order_relaxed)) {
        const unsigned long fault = GuardedApply(view);
        if (fault) {
            g_dead.store(true);
            g_active.store(false);
            WarnLine("R1Delta: PostFx faulted (exception 0x%08lX) in the post pass; it is switched off for this session. "
                "Set the r2port_display_curve / r2port_sharpen_amount cvars to 0 and restart the game.\n", fault);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Activation: the hook exists only while a cvar asks for work
// ---------------------------------------------------------------------------------------------------------------------
bool WantsWork()
{
    return CvarFloat(g_cvCurve, 0.0f) > 0.0f || CvarFloat(g_cvAmount, 0.0f) > 0.0f
        || (g_cvStats && g_cvStats->m_Value.m_nValue != 0);
}

void Reconcile()
{
    std::lock_guard<std::mutex> lock(g_reconcileMutex);
    if (g_dead.load())
        return;
    const bool want = WantsWork();
    if (!g_buildOk) {
        static std::atomic<bool> warned{};
        if (want && !warned.exchange(true))
            WarnLine("R1Delta: PostFx cvars were set but this client.dll is not the retail R1 build the hook was written for; nothing is drawn\n");
        return;
    }
    if (want && !g_active.load()) {
        if (!g_hookCreated) {
            // build the shader cache here (console thread), not on the render thread at the first draw
            if (!shaders::HasShaderPixelFile(kShaderName)) {
                WarnLine("R1Delta: PostFx cannot start: shader '%s' is not built (see r2port_shaders); stock look kept\n", kShaderName);
                return;
            }
            const MH_STATUS create = MH_CreateHook(g_target, reinterpret_cast<void*>(&PostPassHook), reinterpret_cast<void**>(&g_original));
            if (create != MH_OK && create != MH_ERROR_ALREADY_CREATED) {
                WarnLine("R1Delta: PostFx cannot start: hook creation failed (%d); stock look kept\n", static_cast<int>(create));
                g_original = nullptr;
                return;
            }
            g_hookCreated = true;
        }
        g_materialFailed = false;
        const MH_STATUS enable = MH_EnableHook(g_target);
        if (enable != MH_OK && enable != MH_ERROR_ENABLED) {
            WarnLine("R1Delta: PostFx cannot start: hook enable failed (%d); stock look kept\n", static_cast<int>(enable));
            return;
        }
        g_active.store(true);
        LogLine("R1Delta: PostFx active (curve %.2f, sharpen %.2f)\n", CvarFloat(g_cvCurve, 0.0f), CvarFloat(g_cvAmount, 0.0f));
    }
    else if (!want && g_active.load()) {
        g_active.store(false);
        // The trampoline stays allocated: a render thread that already entered the detour may still call through it.
        MH_DisableHook(g_target);
        LogLine("R1Delta: PostFx off, the post pass hook is disabled\n");
    }
}

void OnCvarChanged(void*, const char*, float)
{
    Reconcile();
}

void StatusCommand(const CCommand&)
{
    LogLine("R1Delta: PostFx build=%s hook=%s active=%d dead=%d passes=%llu skipped=%llu last_skip=%s\n",
        g_buildOk ? "verified" : "unsupported", g_hookCreated ? "created" : "not installed", g_active.load() ? 1 : 0,
        g_dead.load() ? 1 : 0, g_passCount.load(), g_skipCount.load(), SkipText(g_lastSkipReason.load()));
    LogLine("  r2port_display_curve=%.3f r2port_sharpen_amount=%.3f r2port_sharpen_denoise=%.3f r2port_sharpen_width=%.1f "
        "r2port_postfx_srgb=%d r2port_postfx_stats=%d\n",
        CvarFloat(g_cvCurve, 0.0f), CvarFloat(g_cvAmount, 0.0f), CvarFloat(g_cvDenoise, 0.5f), CvarFloat(g_cvWidth, 1.0f),
        g_cvSrgb ? g_cvSrgb->m_Value.m_nValue : 1, g_cvStats ? g_cvStats->m_Value.m_nValue : 0);
    const MaterialSet* const published = g_set.load();
    LogLine("  main_thread=%lu material=%s shader=%s\n", g_mainThreadId, published ? kMaterialName : (g_materialFailed ? "FAILED" : "not loaded yet"),
        shaders::HasShaderPixelFile(kShaderName) ? "built" : "NOT built");
}

ConVarR1* RegisterTuned(const char* name, const char* value, int flags, const char* help, bool hasRange, float minimum, float maximum)
{
    ConVarR1* variable = RegisterConVar(name, value, flags, help);
    if (!variable)
        return nullptr;
    if (hasRange) {
        variable->m_bHasMin = true;
        variable->m_fMinVal = minimum;
        variable->m_bHasMax = true;
        variable->m_fMaxVal = maximum;
    }
    variable->m_fnChangeCallbacks.AddToTail(OnCvarChanged);
    return variable;
}

bool VerifyClient(std::uintptr_t base)
{
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        WarnLine("R1Delta: PostFx unavailable: client.dll has no DOS header\n");
        return false;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64
        || nt->FileHeader.TimeDateStamp != kClientTimeDateStamp || nt->OptionalHeader.SizeOfImage != kClientImageSize) {
        WarnLine("R1Delta: PostFx unavailable: client.dll is not the retail R1 build (stamp 0x%08X size 0x%X); the cvars do nothing\n",
            nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage);
        return false;
    }
    if (std::memcmp(reinterpret_cast<const void*>(base + kPostPassRva), kPostPassPrologue, sizeof(kPostPassPrologue)) != 0) {
        WarnLine("R1Delta: PostFx unavailable: post pass prologue mismatch; the cvars do nothing\n");
        return false;
    }
    if (std::memcmp(reinterpret_cast<const void*>(base + kFullFrameFbRva), kFullFramePrologue, sizeof(kFullFramePrologue)) != 0) {
        WarnLine("R1Delta: PostFx unavailable: _rt_FullFrameFB accessor prologue mismatch; the cvars do nothing\n");
        return false;
    }
    return true;
}

} // namespace

void RegisterMaterialProvider()
{
    if (g_providerRegistered.exchange(true))
        return;
    if (!RegisterProvider("materials", std::make_shared<PostFxMaterialProvider>()))
        WarnLine("R1Delta: PostFx material provider could not register\n");
}

void InstallClient(std::uintptr_t clientBase)
{
    if (!clientBase || g_clientBase)
        return;
    g_clientBase = clientBase;
    g_mainThreadId = GetCurrentThreadId();
    QueryPerformanceFrequency(&g_frequency);
    g_buildOk = VerifyClient(clientBase);
    g_target = reinterpret_cast<void*>(clientBase + kPostPassRva);

    constexpr int kFlags = FCVAR_CLIENTDLL | FCVAR_ARCHIVE_PLAYERPROFILE;
    g_cvCurve = RegisterTuned("r2port_display_curve", "0", kFlags,
        "R2-style display curve strength, 0 (off, stock R1 look) to 1. Runs after the stock post pass, before the HUD.", true, 0.0f, 1.0f);
    g_cvAmount = RegisterTuned("r2port_sharpen_amount", "0", kFlags,
        "RCAS sharpening strength, 0 (off) to 1. Runs after the stock post pass, before the HUD.", true, 0.0f, 1.0f);
    g_cvDenoise = RegisterTuned("r2port_sharpen_denoise", "0.5", kFlags,
        "RCAS noise attenuation, 0 (none) to 1; 0.5 is AMD's default.", true, 0.0f, 1.0f);
    g_cvWidth = RegisterTuned("r2port_sharpen_width", "1", kFlags,
        "RCAS tap distance in pixels (1 = AMD's cross).", true, 1.0f, 4.0f);
    g_cvStats = RegisterTuned("r2port_postfx_stats", "0", FCVAR_CLIENTDLL,
        "1: log the average frame time every 300 frames (also with both effects off; installs the hook so the frames can be counted).", true, 0.0f, 1.0f);
    g_cvSrgb = RegisterTuned("r2port_postfx_srgb", "1", FCVAR_CLIENTDLL,
        "Diagnostic: 1 (default) = the pass converts between the sRGB views of the frame and display values; 0 = it treats the sampled values as display values.", true, 0.0f, 1.0f);
    RegisterConCommand("r2port_postfx_status", StatusCommand, "Show the R2 port post effects (display curve, RCAS) state.", FCVAR_CLIENTDLL);
    if (!g_cvCurve || !g_cvAmount || !g_cvDenoise || !g_cvWidth)
        WarnLine("R1Delta: PostFx cvars could not be registered\n");
    Reconcile();
}

} // namespace r2port::postfx
