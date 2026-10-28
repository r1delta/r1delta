#include "r2_ao.h"

#include "engine/cvar.h"
#include "engine/filesystem/r2_map_provider.h"
#include "engine/logging/logging.h"
#include "engine/r2port/shaders/r2_shader_compiler.h"
#include "dxbc_patch.h"

#include <MinHook.h>
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace r2port::ao {
namespace {

using Microsoft::WRL::ComPtr;

// ---- supported builds ------------------------------------------------------------------------------------------
// materialsystem_dx11.dll (R1 retail) and GFSDK_SSAO.win64.dll ("hbao+\rel\r2_1", the build R1 ships in bin\).
constexpr std::uintptr_t kMatSysTimestamp = 0x54874238;
constexpr std::uintptr_t kMatSysImageSize = 0x3E0000;
constexpr std::uintptr_t kHbaoWrapperRva = 0x17800;     // sub_180017800: the only caller of GFSDK RenderAO
constexpr std::uintptr_t kHbaoContextRva = 0x298C50;    // qword_180298C50: GFSDK Renderer* (0 or -1 before creation)
constexpr unsigned char kHbaoWrapperPrologue[] = {
    0x4C, 0x8B, 0xDC, 0x55, 0x49, 0x8D, 0xAB, 0x98, 0xF7, 0xFF, 0xFF, 0x48, 0x81, 0xEC, 0x60, 0x09, 0x00, 0x00,
};
constexpr std::uintptr_t kGfsdkTimestamp = 0x537DE692;
constexpr std::uintptr_t kGfsdkImageSize = 0xF8000;
constexpr std::uintptr_t kRenderAoThunkRva = 0x12D5;    // vtable slot 2 value: jmp to the RenderAO body at RVA 0xFA40
constexpr int kRenderAoSlot = 2;
constexpr std::size_t kParamBytes = 96;                 // GFSDK_SSAO_Parameters as R1 builds it (sub_180017800 stack frame)
// GFSDK_SSAO_Parameters dword indexes used here: [2] DetailAO, [9] Blur.Radius (0 = 2 px, 1 = 4 px, 2 = 8 px).
constexpr int kParamDetail = 2;
constexpr int kParamBlurRadius = 9;
constexpr unsigned kRenderMaskAo = 3;                   // r_hbaoDebugModeOverride default: plain AO
// r2port_sso: pixel-shader file loader (sub_18001D770, the only caller of ID3D11Device::CreatePixelShader for VCS shaders)
// and the engine's device / immediate context pointers.
constexpr std::uintptr_t kShaderLoadRva = 0x1D770;
constexpr unsigned char kShaderLoadPrologue[] = {
    0x48, 0x89, 0x4C, 0x24, 0x08, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x48,
};
constexpr std::uintptr_t kDeviceRva = 0x290D88;         // ppDevice
constexpr std::uintptr_t kContextRva = 0x290D90;        // ppImmediateContext
constexpr int kCreatePixelShaderSlot = 15;

// ---- configuration -----------------------------------------------------------------------------------------------
struct ConVars {
    ConVarR1* timing{};
    ConVarR1* blur{};
    ConVarR1* detail{};
    ConVarR1* luma{};
    ConVarR1* lumaLo{};
    ConVarR1* lumaHi{};
    ConVarR1* sso{};
    ConVarR1* ssoStrength{};
    ConVarR1* ssoDebug{};
};
ConVars g_cvar;

std::atomic<int> g_timing{0};
std::atomic<int> g_blur{0};          // 0 = leave R1's value, 2 / 4 / 8 = force
std::atomic<float> g_detail{0.0f};   // > 0 = override DetailAO
std::atomic<float> g_luma{0.0f};     // > 0 = luma-attenuated composite
std::atomic<float> g_lumaLo{0.03f};
std::atomic<float> g_lumaHi{0.35f};
std::atomic<bool> g_dumpRequest{false};   // r2port_ao_dump: write the next frame's HBAO+ depth input to disk
std::atomic<bool> g_ssoOn{false};          // r2port_sso: stock model/world shaders get an occlusion factor on their env term
std::atomic<float> g_ssoStrength{0.5f};    // r2port_sso_strength: scales the occlusion written for them
std::atomic<bool> g_ssoDebug{false};       // r2port_sso_debug
bool g_ssoEnv = false;                     // env R2_SSO
bool g_ssoConstantZero = false;            // env R2_SSO=zero (test aid): patched env terms are multiplied by 0 whatever t31 holds
std::atomic<unsigned> g_loaderCalls{0};
std::atomic<unsigned> g_firstPatchLoaderCall{0};

// r2port_sso runtime state. The occlusion texture is bound at t31 (PS) after every HBAO+ wrapper call.
constexpr UINT kOcclusionSlot = 31;
ID3D11ShaderResourceView* g_pendingOcc = nullptr;   // occlusion SRV to bind after this frame's HBAO+ wrapper returns (render thread)
bool g_occBound = false;
ID3D11ShaderResourceView* g_lastBound = nullptr;
std::atomic<unsigned> g_occKept{0};
std::atomic<unsigned> g_occLost{0};
using CreatePixelShaderFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11PixelShader**);
CreatePixelShaderFn g_origCreatePs = nullptr;
void** g_devVtable = nullptr;
using ShaderLoaderFn = __int64(__fastcall*)(unsigned short*, unsigned int*);
ShaderLoaderFn g_origLoader = nullptr;
std::atomic<bool> g_loaderHookInstalled{false};
bool g_loaderHookFailed = false;
std::atomic<unsigned> g_patchedShaders{0};
std::atomic<unsigned> g_unpatchedShaders{0};     // not patchable (no cube sample at t5, not SM5, or already uses t31)
std::atomic<unsigned> g_patchFailedCreates{0};   // patched bytes rejected by CreatePixelShader (stock bytes used)

bool AnyEnabled()
{
    return g_timing.load(std::memory_order_relaxed) != 0 || g_blur.load(std::memory_order_relaxed) != 0
        || g_detail.load(std::memory_order_relaxed) > 0.0f || g_luma.load(std::memory_order_relaxed) > 0.0f
        || g_ssoOn.load(std::memory_order_relaxed) || g_dumpRequest.load(std::memory_order_relaxed);
}

float ReadFloat(ConVarR1* variable, float fallback)
{
    return variable ? variable->m_Value.m_fValue : fallback;
}

void Refresh();

void ReloadConfig()
{
    g_timing.store(ReadFloat(g_cvar.timing, 0.0f) != 0.0f ? 1 : 0);
    const int blur = static_cast<int>(ReadFloat(g_cvar.blur, 0.0f) + 0.5f);
    g_blur.store(blur == 2 || blur == 4 || blur == 8 ? blur : 0);
    g_detail.store(std::max(0.0f, ReadFloat(g_cvar.detail, 0.0f)));
    g_luma.store(std::clamp(ReadFloat(g_cvar.luma, 0.0f), 0.0f, 1.0f));
    g_lumaLo.store(std::clamp(ReadFloat(g_cvar.lumaLo, 0.03f), 0.0f, 4.0f));
    g_lumaHi.store(std::clamp(ReadFloat(g_cvar.lumaHi, 0.35f), 0.0f, 4.0f));
    g_ssoOn.store(g_ssoEnv || ReadFloat(g_cvar.sso, 0.0f) != 0.0f);
    g_ssoStrength.store(std::clamp(ReadFloat(g_cvar.ssoStrength, 0.5f), 0.0f, 1.0f));
    g_ssoDebug.store(ReadFloat(g_cvar.ssoDebug, 0.0f) != 0.0f);
}

void OnCvarChanged(void*, const char*, float)
{
    ReloadConfig();
    Refresh();
}

// ---- GPU timing ------------------------------------------------------------------------------------------------
constexpr int kRing = 6;
constexpr int kLogEvery = 240;

struct TimingSlot {
    ComPtr<ID3D11Query> disjoint;
    ComPtr<ID3D11Query> t0, t1, t2;
    bool pending{};
    bool hasComposite{};
};

struct Stats {
    double aoSum{}, aoMin{1e9}, aoMax{};
    double compSum{}, compMin{1e9}, compMax{};
    int count{};
    int compCount{};
};

struct Timer {
    TimingSlot slot[kRing];
    int head{};
    bool created{};
    Stats stats;
    Stats last;
    int totalSamples{};
};
Timer g_timer;
std::mutex g_statsMutex;

bool CreateTimer(ID3D11Device* device)
{
    D3D11_QUERY_DESC disjointDesc{ D3D11_QUERY_TIMESTAMP_DISJOINT, 0 };
    D3D11_QUERY_DESC stampDesc{ D3D11_QUERY_TIMESTAMP, 0 };
    for (TimingSlot& s : g_timer.slot) {
        if (FAILED(device->CreateQuery(&disjointDesc, &s.disjoint)) || FAILED(device->CreateQuery(&stampDesc, &s.t0))
            || FAILED(device->CreateQuery(&stampDesc, &s.t1)) || FAILED(device->CreateQuery(&stampDesc, &s.t2)))
            return false;
    }
    g_timer.created = true;
    return true;
}

void FlushStats(const Stats& s)
{
    Msg("R1Delta: r2port_ao timing over %d calls: HBAO+ %.3f ms avg (min %.3f max %.3f)", s.count,
        s.count ? s.aoSum / s.count : 0.0, s.count ? s.aoMin : 0.0, s.aoMax);
    if (s.compCount)
        Msg(", composite %.3f ms avg (min %.3f max %.3f)", s.compSum / s.compCount, s.compMin, s.compMax);
    Msg("\n");
}

// Reads finished slots oldest first without stalling the GPU.
void CollectTimings(ID3D11DeviceContext* ctx)
{
    for (int i = 0; i < kRing; ++i) {
        TimingSlot& s = g_timer.slot[(g_timer.head + i) % kRing];
        if (!s.pending)
            continue;
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
        if (ctx->GetData(s.disjoint.Get(), &disjoint, sizeof(disjoint), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
            break;
        UINT64 a = 0, b = 0, c = 0;
        if (ctx->GetData(s.t0.Get(), &a, sizeof(a), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK
            || ctx->GetData(s.t1.Get(), &b, sizeof(b), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK
            || (s.hasComposite && ctx->GetData(s.t2.Get(), &c, sizeof(c), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK))
            break;
        s.pending = false;
        if (disjoint.Disjoint || !disjoint.Frequency)
            continue;
        const double ms = 1000.0 / static_cast<double>(disjoint.Frequency);
        Stats& st = g_timer.stats;
        const double ao = static_cast<double>(b - a) * ms;
        st.aoSum += ao; st.aoMin = std::min(st.aoMin, ao); st.aoMax = std::max(st.aoMax, ao); ++st.count;
        if (s.hasComposite) {
            const double comp = static_cast<double>(c - b) * ms;
            st.compSum += comp; st.compMin = std::min(st.compMin, comp); st.compMax = std::max(st.compMax, comp); ++st.compCount;
        }
        if (st.count >= kLogEvery) {
            {
                std::lock_guard<std::mutex> lock(g_statsMutex);
                g_timer.last = st;
            }
            FlushStats(st);
            st = Stats{};
        }
    }
}

// Claims the next slot; returns null when the ring is still full of unread results.
TimingSlot* BeginTiming(ID3D11DeviceContext* ctx)
{
    if (!g_timer.created) {
        ComPtr<ID3D11Device> device;
        ctx->GetDevice(&device);
        if (!device || !CreateTimer(device.Get())) {
            Warning("R1Delta: r2port_ao_timing: timestamp queries are not available; timing disabled\n");
            g_timing.store(0);
            return nullptr;
        }
    }
    CollectTimings(ctx);
    TimingSlot& s = g_timer.slot[g_timer.head];
    if (s.pending)
        return nullptr;
    g_timer.head = (g_timer.head + 1) % kRing;
    s.pending = true;
    s.hasComposite = false;
    ctx->Begin(s.disjoint.Get());
    ctx->End(s.t0.Get());
    return &s;
}

void EndTiming(ID3D11DeviceContext* ctx, TimingSlot* s)
{
    ctx->End(s->disjoint.Get());
}

// ---- composite resources ----------------------------------------------------------------------------------------
const char* const kCompositeSource = R"hlsl(
Texture2D<float4> Scene : register(t0);
Texture2D<float>  Ao    : register(t1);
cbuffer Params : register(b0) { float4 g_p; float4 g_q; };   // g_p: x luma strength, y luma low, z luma high, w occlusion strength; g_q.x: occlusion debug (1 = full occlusion everywhere)

float4 VSMain(uint id : SV_VertexID) : SV_Position
{
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

struct Out
{
    float4 color : SV_Target0;   // scene target (the stock path multiplies it by the AO)
    float  occ   : SV_Target1;   // occlusion texture read by the r2port_sso patched stock shaders
};

// Luma composite: scene = copy(scene) * lerp(ao, 1, lit weight). The blend state replaces the destination.
Out PSMain(float4 sv : SV_Position)
{
    int3 p = int3(sv.xy, 0);
    float4 c = Scene.Load(p);
    float ao = Ao.Load(p);
    float luma = dot(c.rgb, float3(0.2126, 0.7152, 0.0722));
    float lit = saturate((luma - g_p.y) / max(g_p.z - g_p.y, 1e-4));
    float a = lerp(ao, 1.0, lit * g_p.x);
    Out o;
    o.color = float4(c.rgb * a, c.a);
    o.occ = lerp(saturate(1.0 - ao), 1.0, g_q.x) * g_p.w;
    return o;
}

// Stock multiply: the blend state (ZERO, SRC_COLOR) multiplies the scene by the AO, as the HBAO+ DLL does.
Out PSMul(float4 sv : SV_Position)
{
    float ao = Ao.Load(int3(sv.xy, 0));
    Out o;
    o.color = float4(ao, ao, ao, 1.0);
    o.occ = lerp(saturate(1.0 - ao), 1.0, g_q.x) * g_p.w;
    return o;
}
)hlsl";

struct Composite {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11PixelShader> psMul;
    ComPtr<ID3D11Buffer> cb;
    ComPtr<ID3D11BlendState> blend;
    ComPtr<ID3D11BlendState> blendMul;
    ComPtr<ID3D11DepthStencilState> depth;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11Texture2D> aoTex;
    ComPtr<ID3D11RenderTargetView> aoRtv;
    ComPtr<ID3D11ShaderResourceView> aoSrv;
    ComPtr<ID3D11Texture2D> sceneCopy;
    ComPtr<ID3D11ShaderResourceView> sceneSrv;
    ComPtr<ID3D11Texture2D> occTex[2];                 // R8_UNORM occlusion, written this frame, read by the next (ping-pong)
    ComPtr<ID3D11RenderTargetView> occRtv[2];
    ComPtr<ID3D11ShaderResourceView> occSrv[2];
    int occWrite{};
    UINT width{}, height{};
    DXGI_FORMAT texFormat{ DXGI_FORMAT_UNKNOWN };
    DXGI_FORMAT viewFormat{ DXGI_FORMAT_UNKNOWN };
    bool broken{};
    bool logged{};
};
Composite g_comp;

void Break(const char* why)
{
    if (!g_comp.broken)
        Warning("R1Delta: r2port_ao composite disabled: %s (stock HBAO+ multiply is used)\n", why);
    g_comp.broken = true;
}

bool CreateStaticComposite(ID3D11Device* device)
{
    r2port::shaders::CompileRequest request;
    request.sourceName = "r2port_ao_composite";
    request.source = kCompositeSource;
    request.entry = "VSMain";
    request.profile = "vs_4_0";
    r2port::shaders::CompileResult vsBlob = r2port::shaders::CompileHlsl(request);
    request.entry = "PSMain";
    request.profile = "ps_4_0";
    r2port::shaders::CompileResult psBlob = r2port::shaders::CompileHlsl(request);
    request.entry = "PSMul";
    r2port::shaders::CompileResult mulBlob = r2port::shaders::CompileHlsl(request);
    if (!vsBlob.ok || !psBlob.ok || !mulBlob.ok) {
        Break(!vsBlob.ok ? vsBlob.messages.c_str() : (!psBlob.ok ? psBlob.messages.c_str() : mulBlob.messages.c_str()));
        return false;
    }
    if (FAILED(device->CreateVertexShader(vsBlob.dxbc.data(), vsBlob.dxbc.size(), nullptr, &g_comp.vs))
        || FAILED(device->CreatePixelShader(psBlob.dxbc.data(), psBlob.dxbc.size(), nullptr, &g_comp.ps))
        || FAILED(device->CreatePixelShader(mulBlob.dxbc.data(), mulBlob.dxbc.size(), nullptr, &g_comp.psMul))) {
        Break("CreateVertexShader/CreatePixelShader failed");
        return false;
    }
    D3D11_BUFFER_DESC cbd{ 32, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 };
    D3D11_BLEND_DESC bd{};
    bd.IndependentBlendEnable = TRUE;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    bd.RenderTarget[1].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED;
    D3D11_BLEND_DESC mulDesc = bd;
    mulDesc.RenderTarget[0].BlendEnable = TRUE;
    mulDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ZERO;
    mulDesc.RenderTarget[0].DestBlend = D3D11_BLEND_SRC_COLOR;
    mulDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    mulDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
    mulDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    mulDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    D3D11_DEPTH_STENCIL_DESC dd{};
    dd.DepthEnable = FALSE;
    dd.DepthFunc = D3D11_COMPARISON_ALWAYS;
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    if (FAILED(device->CreateBuffer(&cbd, nullptr, &g_comp.cb)) || FAILED(device->CreateBlendState(&bd, &g_comp.blend))
        || FAILED(device->CreateBlendState(&mulDesc, &g_comp.blendMul))
        || FAILED(device->CreateDepthStencilState(&dd, &g_comp.depth)) || FAILED(device->CreateRasterizerState(&rd, &g_comp.raster))) {
        Break("state object creation failed");
        return false;
    }
    g_comp.device = device;
    return true;
}

// Builds or resizes the AO texture, the scene copy and the occlusion textures for this render target. False = fail
// closed (stock path).
bool EnsureComposite(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv)
{
    if (g_comp.broken)
        return false;
    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtv->GetDesc(&rtvDesc);
    if (rtvDesc.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2D || rtvDesc.Texture2D.MipSlice != 0) {
        Break("scene render target view is not a plain Texture2D mip 0 (MSAA or array)");
        return false;
    }
    ComPtr<ID3D11Resource> resource;
    rtv->GetResource(&resource);
    ComPtr<ID3D11Texture2D> scene;
    if (!resource || FAILED(resource.As(&scene))) {
        Break("scene render target is not a Texture2D");
        return false;
    }
    D3D11_TEXTURE2D_DESC desc{};
    scene->GetDesc(&desc);
    if (desc.SampleDesc.Count != 1 || desc.ArraySize != 1) {
        Break("scene render target is multisampled or an array");
        return false;
    }
    ComPtr<ID3D11Device> device;
    ctx->GetDevice(&device);
    if (!device) {
        Break("no device");
        return false;
    }
    if (!g_comp.vs && !CreateStaticComposite(device.Get()))
        return false;
    if (g_comp.device.Get() != device.Get()) {
        Break("device changed");
        return false;
    }
    const DXGI_FORMAT viewFormat = rtvDesc.Format != DXGI_FORMAT_UNKNOWN ? rtvDesc.Format : desc.Format;
    if (g_comp.aoTex && g_comp.width == desc.Width && g_comp.height == desc.Height && g_comp.texFormat == desc.Format
        && g_comp.viewFormat == viewFormat)
        return true;

    g_comp.aoTex.Reset(); g_comp.aoRtv.Reset(); g_comp.aoSrv.Reset(); g_comp.sceneCopy.Reset(); g_comp.sceneSrv.Reset();
    for (int i = 0; i < 2; ++i) {
        g_comp.occTex[i].Reset(); g_comp.occRtv[i].Reset(); g_comp.occSrv[i].Reset();
    }
    g_pendingOcc = nullptr;
    D3D11_TEXTURE2D_DESC aoDesc{};
    aoDesc.Width = desc.Width; aoDesc.Height = desc.Height; aoDesc.MipLevels = 1; aoDesc.ArraySize = 1;
    aoDesc.Format = DXGI_FORMAT_R16_UNORM; aoDesc.SampleDesc.Count = 1; aoDesc.Usage = D3D11_USAGE_DEFAULT;
    aoDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    D3D11_TEXTURE2D_DESC occDesc = aoDesc;
    occDesc.Format = DXGI_FORMAT_R8_UNORM;
    D3D11_TEXTURE2D_DESC copyDesc = desc;
    copyDesc.MipLevels = 1; copyDesc.Usage = D3D11_USAGE_DEFAULT; copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    copyDesc.CPUAccessFlags = 0; copyDesc.MiscFlags = 0;
    D3D11_SHADER_RESOURCE_VIEW_DESC copySrv{};
    copySrv.Format = viewFormat; copySrv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; copySrv.Texture2D.MipLevels = 1;
    bool ok = SUCCEEDED(device->CreateTexture2D(&aoDesc, nullptr, &g_comp.aoTex)) && SUCCEEDED(device->CreateRenderTargetView(g_comp.aoTex.Get(), nullptr, &g_comp.aoRtv))
        && SUCCEEDED(device->CreateShaderResourceView(g_comp.aoTex.Get(), nullptr, &g_comp.aoSrv))
        && SUCCEEDED(device->CreateTexture2D(&copyDesc, nullptr, &g_comp.sceneCopy))
        && SUCCEEDED(device->CreateShaderResourceView(g_comp.sceneCopy.Get(), &copySrv, &g_comp.sceneSrv));
    for (int i = 0; i < 2 && ok; ++i) {
        ok = SUCCEEDED(device->CreateTexture2D(&occDesc, nullptr, &g_comp.occTex[i]))
            && SUCCEEDED(device->CreateRenderTargetView(g_comp.occTex[i].Get(), nullptr, &g_comp.occRtv[i]))
            && SUCCEEDED(device->CreateShaderResourceView(g_comp.occTex[i].Get(), nullptr, &g_comp.occSrv[i]));
    }
    if (!ok) {
        Break("texture creation failed");
        return false;
    }
    g_comp.width = desc.Width; g_comp.height = desc.Height; g_comp.texFormat = desc.Format; g_comp.viewFormat = viewFormat;
    if (!g_comp.logged) {
        g_comp.logged = true;
        Msg("R1Delta: r2port_ao composite: scene target %ux%u texture format %d view format %d, AO target R16_UNORM, occlusion R8_UNORM x2\n",
            desc.Width, desc.Height, static_cast<int>(desc.Format), static_cast<int>(viewFormat));
    }
    return true;
}

std::atomic<bool> g_probeRequest{false};

// Test aid (r2port_sso_probe): logs min / mean / max of an R8 and an R16 texture read back through staging copies.
void ProbeTexture(ID3D11DeviceContext* ctx, ID3D11Texture2D* occ, ID3D11Texture2D* ao)
{
    ComPtr<ID3D11Device> device;
    ctx->GetDevice(&device);
    for (int which = 0; which < 2; ++which) {
        ID3D11Texture2D* source = which == 0 ? occ : ao;
        D3D11_TEXTURE2D_DESC desc{};
        source->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; desc.MiscFlags = 0;
        ComPtr<ID3D11Texture2D> staging;
        if (!device || FAILED(device->CreateTexture2D(&desc, nullptr, &staging)))
            continue;
        ctx->CopyResource(staging.Get(), source);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
            continue;
        double sum = 0.0, lo = 1e9, hi = -1e9;
        for (UINT y = 0; y < desc.Height; y += 4) {
            const auto* row = static_cast<const unsigned char*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch;
            for (UINT x = 0; x < desc.Width; x += 4) {
                const double v = which == 0 ? row[x] / 255.0 : reinterpret_cast<const std::uint16_t*>(row)[x] / 65535.0;
                sum += v; lo = std::min(lo, v); hi = std::max(hi, v);
            }
        }
        ctx->Unmap(staging.Get(), 0);
        const double count = static_cast<double>((desc.Height + 3) / 4) * ((desc.Width + 3) / 4);
        Msg("R1Delta: r2port_sso_probe: %s %ux%u min %.3f mean %.3f max %.3f\n", which == 0 ? "occlusion" : "ao", desc.Width, desc.Height,
            lo, sum / count, hi);
    }
}

// Draws the scene composite and, for r2port_sso, the occlusion texture. lumaStrength > 0: scene = copy * lerp(ao, 1, lit);
// otherwise the stock multiply by blending. State touched here is all in the set R1's HBAO wrapper (sub_180011A30 /
// sub_180015940) saves and restores around the call.
void DrawComposite(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, const D3D11_VIEWPORT& viewport, float lumaStrength, bool sso)
{
    const bool luma = lumaStrength > 0.0f;
    if (luma) {
        ComPtr<ID3D11Resource> resource;
        rtv->GetResource(&resource);
        ctx->CopyResource(g_comp.sceneCopy.Get(), resource.Get());
    }
    const float params[8] = { lumaStrength, g_lumaLo.load(), std::max(g_lumaHi.load(), g_lumaLo.load() + 1e-3f),
        std::clamp(g_ssoStrength.load(), 0.0f, 1.0f), g_ssoDebug.load() ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };
    ctx->UpdateSubresource(g_comp.cb.Get(), 0, nullptr, params, 0, 0);

    const int write = g_comp.occWrite;
    ID3D11RenderTargetView* targets[2] = { rtv, sso ? g_comp.occRtv[write].Get() : nullptr };
    ctx->OMSetRenderTargets(2, targets, nullptr);
    ctx->OMSetBlendState(luma ? g_comp.blend.Get() : g_comp.blendMul.Get(), nullptr, 0xFFFFFFFFu);
    ctx->OMSetDepthStencilState(g_comp.depth.Get(), 0);
    ctx->RSSetState(g_comp.raster.Get());
    ctx->RSSetViewports(1, &viewport);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(g_comp.vs.Get(), nullptr, 0);
    ctx->GSSetShader(nullptr, nullptr, 0);
    ctx->HSSetShader(nullptr, nullptr, 0);
    ctx->DSSetShader(nullptr, nullptr, 0);
    ctx->PSSetShader(luma ? g_comp.ps.Get() : g_comp.psMul.Get(), nullptr, 0);
    ID3D11ShaderResourceView* views[2] = { g_comp.sceneSrv.Get(), g_comp.aoSrv.Get() };
    ctx->PSSetShaderResources(0, 2, views);
    ID3D11Buffer* cbs[1] = { g_comp.cb.Get() };
    ctx->PSSetConstantBuffers(0, 1, cbs);
    ctx->Draw(3, 0);
    ID3D11ShaderResourceView* none[2] = {};
    ctx->PSSetShaderResources(0, 2, none);
    if (sso) {
        g_pendingOcc = g_comp.occSrv[write].Get();
        g_comp.occWrite = write ^ 1;
        if (g_probeRequest.exchange(false))
            ProbeTexture(ctx, g_comp.occTex[write].Get(), g_comp.aoTex.Get());
    }
}

// ---- depth dump (input for the offline AO benchmark) --------------------------------------------------------------
std::mutex g_dumpPathMutex;
std::string g_dumpPath;

// Writes the HBAO+ depth input of this call as `.r2d`: u32 'R2D1', u32 width, u32 height, u32 format (0 = float depth
// 0..1, hardware depth), float projection[16] as R1 passes it, float metersToViewUnits, then width*height float depths.
void DumpDepth(ID3D11DeviceContext* ctx, const void* input)
{
    std::string path;
    {
        std::lock_guard<std::mutex> lock(g_dumpPathMutex);
        path = g_dumpPath;
    }
    const auto* bytes = static_cast<const unsigned char*>(input);
    auto* srv = *reinterpret_cast<ID3D11ShaderResourceView* const*>(bytes + 24);
    const float* projection = *reinterpret_cast<const float* const*>(bytes + 8);
    const int layout = *reinterpret_cast<const int*>(bytes + 16);
    const float meters = *reinterpret_cast<const float*>(bytes + 20);
    ComPtr<ID3D11Resource> resource;
    ComPtr<ID3D11Texture2D> texture;
    if (!srv || !projection)
        return Warning("R1Delta: r2port_ao_dump: HBAO+ input has no depth SRV or projection\n");
    srv->GetResource(&resource);
    if (!resource || FAILED(resource.As(&texture)))
        return Warning("R1Delta: r2port_ao_dump: depth resource is not a Texture2D\n");
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    if (desc.SampleDesc.Count != 1 || desc.ArraySize != 1)
        return Warning("R1Delta: r2port_ao_dump: depth texture is multisampled or an array\n");
    ComPtr<ID3D11Device> device;
    ctx->GetDevice(&device);
    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.MipLevels = 1; stagingDesc.Usage = D3D11_USAGE_STAGING; stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; stagingDesc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    if (!device || FAILED(device->CreateTexture2D(&stagingDesc, nullptr, &staging)))
        return Warning("R1Delta: r2port_ao_dump: cannot create the staging texture (format %d)\n", static_cast<int>(desc.Format));
    ctx->CopyResource(staging.Get(), texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return Warning("R1Delta: r2port_ao_dump: Map failed\n");
    std::vector<float> depth(static_cast<std::size_t>(desc.Width) * desc.Height);
    bool converted = true;
    for (UINT y = 0; y < desc.Height && converted; ++y) {
        const auto* row = static_cast<const unsigned char*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch;
        float* out = depth.data() + static_cast<std::size_t>(y) * desc.Width;
        for (UINT x = 0; x < desc.Width; ++x) {
            switch (desc.Format) {
            case DXGI_FORMAT_R32_TYPELESS: case DXGI_FORMAT_D32_FLOAT: case DXGI_FORMAT_R32_FLOAT:
                std::memcpy(&out[x], row + 4 * x, 4); break;
            case DXGI_FORMAT_R24G8_TYPELESS: case DXGI_FORMAT_D24_UNORM_S8_UINT: {
                std::uint32_t v; std::memcpy(&v, row + 4 * x, 4); out[x] = static_cast<float>(v & 0xFFFFFFu) / 16777215.0f; break; }
            case DXGI_FORMAT_R32G8X24_TYPELESS: case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
                std::memcpy(&out[x], row + 8 * x, 4); break;
            case DXGI_FORMAT_R16_TYPELESS: case DXGI_FORMAT_D16_UNORM: {
                std::uint16_t v; std::memcpy(&v, row + 2 * x, 2); out[x] = static_cast<float>(v) / 65535.0f; break; }
            default: converted = false; break;
            }
            if (!converted)
                break;
        }
    }
    ctx->Unmap(staging.Get(), 0);
    if (!converted)
        return Warning("R1Delta: r2port_ao_dump: unsupported depth format %d\n", static_cast<int>(desc.Format));
    if (path.empty())
        path = (r2port::GetR2MapPatchDirectory() / "depth_dump.r2d").string();
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    const std::uint32_t header[4] = { 0x31443252u, desc.Width, desc.Height, 0u };
    file.write(reinterpret_cast<const char*>(header), sizeof(header));
    file.write(reinterpret_cast<const char*>(projection), 16 * sizeof(float));
    file.write(reinterpret_cast<const char*>(&meters), sizeof(meters));
    file.write(reinterpret_cast<const char*>(depth.data()), static_cast<std::streamsize>(depth.size() * sizeof(float)));
    Msg("R1Delta: r2port_ao_dump: wrote %ux%u depth (texture format %d, projection layout %d, meters %.2f) to %s (%s)\n", desc.Width,
        desc.Height, static_cast<int>(desc.Format), layout, meters, path.c_str(), file ? "ok" : "WRITE FAILED");
    Msg("R1Delta: r2port_ao_dump: projection %f %f %f %f / %f %f %f %f / %f %f %f %f / %f %f %f %f\n", projection[0], projection[1],
        projection[2], projection[3], projection[4], projection[5], projection[6], projection[7], projection[8], projection[9],
        projection[10], projection[11], projection[12], projection[13], projection[14], projection[15]);
}

// ---- GFSDK RenderAO hook -----------------------------------------------------------------------------------------
using RenderAoFn = int(__fastcall*)(void* self, ID3D11DeviceContext* ctx, const void* input, const void* params,
    ID3D11RenderTargetView* rtv, unsigned mask);
RenderAoFn g_origRenderAo = nullptr;
std::atomic<std::uint64_t> g_calls{ 0 };
std::atomic<bool> g_loggedFirst{ false };

int __fastcall RenderAoHook(void* self, ID3D11DeviceContext* ctx, const void* input, const void* params,
    ID3D11RenderTargetView* rtv, unsigned mask)
{
    if (!ctx || !params || !rtv || mask != kRenderMaskAo || !AnyEnabled())
        return g_origRenderAo(self, ctx, input, params, rtv, mask);
    g_calls.fetch_add(1, std::memory_order_relaxed);
    if (g_dumpRequest.exchange(false))
        DumpDepth(ctx, input);

    alignas(16) unsigned char local[kParamBytes];
    std::memcpy(local, params, kParamBytes);
    const int blur = g_blur.load(std::memory_order_relaxed);
    if (blur != 0)
        reinterpret_cast<int*>(local)[kParamBlurRadius] = blur == 2 ? 0 : (blur == 4 ? 1 : 2);
    const float detail = g_detail.load(std::memory_order_relaxed);
    if (detail > 0.0f)
        reinterpret_cast<float*>(local)[kParamDetail] = detail;
    const float strength = g_luma.load(std::memory_order_relaxed);
    const bool sso = g_ssoOn.load(std::memory_order_relaxed) && g_ssoStrength.load(std::memory_order_relaxed) > 0.0f;

    TimingSlot* timing = g_timing.load(std::memory_order_relaxed) ? BeginTiming(ctx) : nullptr;

    // Redirect HBAO+ into our own texture whenever something needs the AO value (luma composite, occlusion for r2port_sso).
    bool composite = (strength > 0.0f || sso) && EnsureComposite(ctx, rtv);
    D3D11_VIEWPORT viewport{};
    if (composite) {
        UINT count = 1;
        ctx->RSGetViewports(&count, &viewport);
        if (count == 0)
            composite = false;
    }
    int result;
    if (composite) {
        const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        ctx->ClearRenderTargetView(g_comp.aoRtv.Get(), white);
        result = g_origRenderAo(self, ctx, input, local, g_comp.aoRtv.Get(), mask);
        if (timing)
            ctx->End(timing->t1.Get());
        if (result == 0)
            DrawComposite(ctx, rtv, viewport, strength, sso);
        else
            Break("HBAO+ RenderAO returned an error with the redirected target");
    }
    else {
        result = g_origRenderAo(self, ctx, input, local, rtv, mask);
        if (timing)
            ctx->End(timing->t1.Get());
    }
    if (timing) {
        timing->hasComposite = composite;
        if (composite)
            ctx->End(timing->t2.Get());
        EndTiming(ctx, timing);
    }
    if (!g_loggedFirst.exchange(true))
        Msg("R1Delta: r2port_ao: first HBAO+ call through the hook (blur=%d detail=%.2f luma=%.2f sso=%d timing=%d composite=%d)\n",
            blur, detail, strength, sso ? 1 : 0, g_timing.load(), composite ? 1 : 0);
    return result;
}

// ---- installation ------------------------------------------------------------------------------------------------
std::uintptr_t g_base = 0;
bool g_baseChecked = false;
bool g_baseOk = false;
std::atomic<bool> g_detourInstalled{ false };
std::atomic<bool> g_failed{ false };
void** g_patchedVtable = nullptr;

using WrapperFn = void(__fastcall*)(void*, void*, void*, void*);
WrapperFn g_origWrapper = nullptr;

bool CheckBase(std::uintptr_t base)
{
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
        return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.TimeDateStamp != kMatSysTimestamp
        || nt->OptionalHeader.SizeOfImage != kMatSysImageSize)
        return false;
    return std::memcmp(reinterpret_cast<const void*>(base + kHbaoWrapperRva), kHbaoWrapperPrologue, sizeof(kHbaoWrapperPrologue)) == 0;
}

// Swaps vtable slot 2 (RenderAO) of the live GFSDK Renderer for our hook. The vtable is shared by all contexts of the
// DLL, so one swap covers context re-creation. Fails closed on any mismatch.
void EnsureVtablePatched()
{
    if (g_failed.load(std::memory_order_relaxed))
        return;
    void* context = *reinterpret_cast<void**>(g_base + kHbaoContextRva);
    if (!context || context == reinterpret_cast<void*>(~static_cast<std::uintptr_t>(0)))
        return;
    void** vtable = *reinterpret_cast<void***>(context);
    if (vtable == g_patchedVtable)
        return;
    HMODULE gfsdk = GetModuleHandleA("GFSDK_SSAO.win64.dll");
    bool ok = gfsdk != nullptr;
    if (ok) {
        const auto base = reinterpret_cast<std::uintptr_t>(gfsdk);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        ok = nt->FileHeader.TimeDateStamp == kGfsdkTimestamp && nt->OptionalHeader.SizeOfImage == kGfsdkImageSize
            && reinterpret_cast<std::uintptr_t>(vtable[kRenderAoSlot]) == base + kRenderAoThunkRva;
    }
    if (!ok) {
        g_failed.store(true);
        Warning("R1Delta: r2port_ao not active: GFSDK_SSAO.win64.dll is not the expected HBAO+ r2_1 build (R1 stock behaviour kept)\n");
        return;
    }
    DWORD old = 0;
    if (!VirtualProtect(&vtable[kRenderAoSlot], sizeof(void*), PAGE_READWRITE, &old)) {
        g_failed.store(true);
        Warning("R1Delta: r2port_ao not active: cannot unprotect the HBAO+ vtable\n");
        return;
    }
    g_origRenderAo = reinterpret_cast<RenderAoFn>(vtable[kRenderAoSlot]);
    vtable[kRenderAoSlot] = reinterpret_cast<void*>(&RenderAoHook);
    VirtualProtect(&vtable[kRenderAoSlot], sizeof(void*), old, &old);
    g_patchedVtable = vtable;
    Msg("R1Delta: r2port_ao hook active (HBAO+ RenderAO slot %d of %p patched)\n", kRenderAoSlot, static_cast<void*>(vtable));
}

// Binds (or unbinds) the occlusion texture at t31 once the wrapper has restored R1's saved shader resources. The stock
// model and world shaders patched by r2port_sso read it with `ld` at SV_Position; an unbound slot reads 0 = no occlusion.
void BindOcclusion()
{
    auto* ctx = *reinterpret_cast<ID3D11DeviceContext**>(g_base + kContextRva);
    if (!ctx)
        return;
    if (g_pendingOcc) {
        ID3D11ShaderResourceView* view = g_pendingOcc;
        ctx->PSSetShaderResources(kOcclusionSlot, 1, &view);
        g_occBound = true;
        g_lastBound = view;
    }
    else if (g_occBound) {
        ID3D11ShaderResourceView* none = nullptr;
        ctx->PSSetShaderResources(kOcclusionSlot, 1, &none);
        g_occBound = false;
    }
}

void __fastcall WrapperHook(void* a1, void* a2, void* a3, void* a4)
{
    if (g_occBound) {
        // Test aid: is the occlusion SRV still at t31 when the next frame's HBAO+ wrapper starts?
        if (auto* ctx = *reinterpret_cast<ID3D11DeviceContext**>(g_base + kContextRva)) {
            ID3D11ShaderResourceView* current = nullptr;
            ctx->PSGetShaderResources(kOcclusionSlot, 1, &current);
            if (current != g_lastBound)
                g_occLost.fetch_add(1, std::memory_order_relaxed);
            else
                g_occKept.fetch_add(1, std::memory_order_relaxed);
            if (current)
                current->Release();
        }
    }
    g_pendingOcc = nullptr;
    if (AnyEnabled())
        EnsureVtablePatched();
    g_origWrapper(a1, a2, a3, a4);
    BindOcclusion();
}

// ---- stock shader patching (r2port_sso) ---------------------------------------------------------------------------
// R1 creates every pixel shader of a VCS file through ID3D11Device::CreatePixelShader inside the loader at RVA 0x1D770.
// With r2port_sso set, the device vtable slot is swapped (once the device exists, from that loader's entry) and every
// SM5 pixel shader with a cube sample at t5 is rewritten (dxbc_patch.cpp) before creation. Failure of the patcher or of
// CreatePixelShader on the patched bytes falls back to the stock bytes.
HRESULT STDMETHODCALLTYPE CreatePixelShaderHook(ID3D11Device* device, const void* bytecode, SIZE_T length, ID3D11ClassLinkage* linkage,
    ID3D11PixelShader** shader)
{
    if (g_ssoOn.load(std::memory_order_relaxed) && bytecode && length > 64) {
        const auto* bytes = static_cast<const std::uint8_t*>(bytecode);
        std::vector<std::uint8_t> input(bytes, bytes + length), output;
        dxbc::PatchOptions options;
        options.aoWeight = 1.0f;
        options.occlusionInput = true;
        if (g_ssoConstantZero)
            options.constantFactor = 0.0f;
        dxbc::PatchReport report;
        if (dxbc::PatchDxbc(input, output, report, options)) {
            const HRESULT hr = g_origCreatePs(device, output.data(), output.size(), linkage, shader);
            if (SUCCEEDED(hr)) {
                if (g_patchedShaders.fetch_add(1, std::memory_order_relaxed) == 0) {
                    g_firstPatchLoaderCall.store(g_loaderCalls.load());
                    Msg("R1Delta: r2port_sso: first stock pixel shader patched (%zu -> %zu bytes) at loader call %u\n", static_cast<size_t>(length),
                        output.size(), g_loaderCalls.load());
                }
                return hr;
            }
            g_patchFailedCreates.fetch_add(1, std::memory_order_relaxed);
        }
        else {
            g_unpatchedShaders.fetch_add(1, std::memory_order_relaxed);
        }
    }
    return g_origCreatePs(device, bytecode, length, linkage, shader);
}

void EnsureDeviceHook()
{
    if (!g_ssoOn.load())
        return;
    auto* device = *reinterpret_cast<ID3D11Device**>(g_base + kDeviceRva);
    if (!device)
        return;
    void** vtable = *reinterpret_cast<void***>(device);
    if (vtable == g_devVtable)
        return;
    DWORD old = 0;
    if (!VirtualProtect(&vtable[kCreatePixelShaderSlot], sizeof(void*), PAGE_READWRITE, &old)) {
        Warning("R1Delta: r2port_sso not active: cannot unprotect the D3D11 device vtable\n");
        return;
    }
    g_origCreatePs = reinterpret_cast<CreatePixelShaderFn>(vtable[kCreatePixelShaderSlot]);
    vtable[kCreatePixelShaderSlot] = reinterpret_cast<void*>(&CreatePixelShaderHook);
    VirtualProtect(&vtable[kCreatePixelShaderSlot], sizeof(void*), old, &old);
    g_devVtable = vtable;
    Msg("R1Delta: r2port_sso: CreatePixelShader hook active (device vtable %p)\n", static_cast<void*>(vtable));
}

__int64 __fastcall ShaderLoaderHook(unsigned short* a1, unsigned int* a2)
{
    g_loaderCalls.fetch_add(1, std::memory_order_relaxed);
    EnsureDeviceHook();
    return g_origLoader(a1, a2);
}

void InstallShaderLoaderHook()
{
    if (g_loaderHookInstalled.load() || g_loaderHookFailed)
        return;
    void* target = reinterpret_cast<void*>(g_base + kShaderLoadRva);
    if (std::memcmp(target, kShaderLoadPrologue, sizeof(kShaderLoadPrologue)) != 0) {
        g_loaderHookFailed = true;
        Warning("R1Delta: r2port_sso not installed: shader loader prologue differs (stock shaders kept)\n");
        return;
    }
    const MH_STATUS create = MH_CreateHook(target, reinterpret_cast<void*>(&ShaderLoaderHook), reinterpret_cast<void**>(&g_origLoader));
    const MH_STATUS enable = create == MH_OK ? MH_EnableHook(target) : create;
    g_loaderHookInstalled.store(enable == MH_OK);
    g_loaderHookFailed = enable != MH_OK;
    Msg("R1Delta: r2port_sso shader loader hook create=%d enable=%d target=%p\n", static_cast<int>(create), static_cast<int>(enable), target);
    EnsureDeviceHook();
}

void Refresh()
{
    if (!g_base)
        return;
    if (!g_baseChecked) {
        g_baseChecked = true;
        g_baseOk = CheckBase(g_base);
        if (!g_baseOk)
            Warning("R1Delta: r2port_ao not installed: materialsystem_dx11 is not the retail R1 build or the HBAO+ wrapper prologue differs\n");
    }
    if (!g_baseOk)
        return;
    if (g_ssoOn.load())
        InstallShaderLoaderHook();
    if (!AnyEnabled() || g_detourInstalled.load())
        return;
    void* target = reinterpret_cast<void*>(g_base + kHbaoWrapperRva);
    const MH_STATUS create = MH_CreateHook(target, reinterpret_cast<void*>(&WrapperHook), reinterpret_cast<void**>(&g_origWrapper));
    const MH_STATUS enable = create == MH_OK ? MH_EnableHook(target) : create;
    g_detourInstalled.store(enable == MH_OK);
    Msg("R1Delta: r2port_ao detour on the HBAO+ wrapper create=%d enable=%d target=%p\n", static_cast<int>(create),
        static_cast<int>(enable), target);
}

void StatusCommand(const CCommand&)
{
    Stats last;
    {
        std::lock_guard<std::mutex> lock(g_statsMutex);
        last = g_timer.last;
    }
    Msg("r2port_ao: timing=%d blur=%d detail=%.2f luma=%.2f (lo %.2f hi %.2f) detour=%d vtable=%d failed=%d composite_broken=%d calls=%llu\n",
        g_timing.load(), g_blur.load(), g_detail.load(), g_luma.load(), g_lumaLo.load(), g_lumaHi.load(),
        g_detourInstalled.load() ? 1 : 0, g_patchedVtable ? 1 : 0, g_failed.load() ? 1 : 0, g_comp.broken ? 1 : 0,
        static_cast<unsigned long long>(g_calls.load()));
    Msg("r2port_sso: enabled=%d (env=%d) strength=%.2f debug=%d shader hook=%d loader calls=%u first patch at loader call=%u device vtable=%d patched=%u unpatched=%u create_failed=%u occlusion bound=%d (still at t31 next frame: %u, lost: %u)\n",
        g_ssoOn.load() ? 1 : 0, g_ssoEnv ? 1 : 0, g_ssoStrength.load(), g_ssoDebug.load() ? 1 : 0, g_loaderHookInstalled.load() ? 1 : 0,
        g_loaderCalls.load(), g_firstPatchLoaderCall.load(), g_devVtable ? 1 : 0, g_patchedShaders.load(),
        g_unpatchedShaders.load(), g_patchFailedCreates.load(), g_occBound ? 1 : 0, g_occKept.load(), g_occLost.load());
    if (last.count)
        FlushStats(last);
}

void DumpCommand(const CCommand& command)
{
    if (command.ArgC() > 1) {
        std::lock_guard<std::mutex> lock(g_dumpPathMutex);
        g_dumpPath = command.ArgV()[1];
    }
    else {
        std::lock_guard<std::mutex> lock(g_dumpPathMutex);
        g_dumpPath.clear();
    }
    g_dumpRequest.store(true);
    Refresh();
    Msg("r2port_ao_dump: armed, the next HBAO+ call writes the depth input (r_hbaoEnabled must be 1)\n");
}

ConVarR1* Register(const char* name, const char* value, const char* help)
{
    ConVarR1* variable = RegisterConVar(name, value, FCVAR_ARCHIVE, help);
    if (variable)
        variable->m_fnChangeCallbacks.AddToTail(OnCvarChanged);
    return variable;
}

} // namespace

void RegisterConVars()
{
    static bool registered = false;
    if (registered)
        return;
    registered = true;
    g_cvar.timing = Register("r2port_ao_timing", "0",
        "1 = log the GPU time of the HBAO+ pass (and the composite) every 240 frames to the console.");
    g_cvar.blur = Register("r2port_ao_blur", "0",
        "0 = r_hbaoBlurRadius decides, 2 / 4 / 8 = force the HBAO+ blur radius in pixels (2 is cheaper than R1's 4 and 8).");
    g_cvar.detail = Register("r2port_ao_detail", "0",
        "0 = off (R1 keeps DetailAO 0). 0.2 to 1 adds HBAO+ detail AO (finer contact shadows).");
    g_cvar.luma = Register("r2port_ao_luma", "0",
        "0 = stock multiply. 0..1 = fade AO out on bright (sunlit) pixels so it darkens shade and creases, not direct light.");
    g_cvar.lumaLo = Register("r2port_ao_luma_lo", "0.03", "Linear scene luma below which r2port_ao_luma leaves the AO untouched (display value ~ 90).");
    g_cvar.lumaHi = Register("r2port_ao_luma_hi", "0.35", "Linear scene luma above which r2port_ao_luma removes the AO completely (display value ~ 165).");
    RegisterConCommand("r2port_ao_status", StatusCommand, "Print the r2port_ao state and the last GPU timing.", FCVAR_NONE);
    RegisterConCommand("r2port_ao_dump", DumpCommand,
        "Write the next frame's HBAO+ depth input to <gamedir>/r2port/depth_dump.r2d (or the path given); needs r_hbaoEnabled 1.", FCVAR_NONE);
    RegisterConCommand("r2port_sso_probe", [](const CCommand&) { g_probeRequest.store(true); },
        "Test aid: the next frame logs min/mean/max of the occlusion texture r2port_sso writes (and of the AO texture).", FCVAR_NONE);
    g_cvar.sso = Register("r2port_sso", "0",
        "1 = specular occlusion: patch the env-cubemap term of the stock model and world shaders to be occluded by the previous frame's HBAO+ result (needs r_hbaoEnabled 1; applies to shaders created after it is set, so put it in autoexec or the launch line, then reload the map).");
    g_cvar.ssoStrength = Register("r2port_sso_strength", "0.5",
        "0..1 = how strongly full AO occlusion removes the env reflection (live; 0.5 halves it).");
    g_cvar.ssoDebug = Register("r2port_sso_debug", "0",
        "1 = test aid for r2port_sso: full occlusion everywhere (env reflection of every patched shader scaled by 1 - r2port_sso_strength).");
    ReloadConfig();
    Refresh();
}

void Install(std::uintptr_t materialSystemBase)
{
    g_base = materialSystemBase;
    // Env R2_SSO (any value but 0) switches the specular occlusion shader patching on from the very first shader
    // load, before any console variable or config file can be applied; the variables then only tune it.
    char env[8] = {};
    if (GetEnvironmentVariableA("R2_SSO", env, sizeof(env)) && env[0] && std::strcmp(env, "0") != 0) {
        g_ssoEnv = true;
        g_ssoConstantZero = std::strcmp(env, "zero") == 0;
    }
    Msg("R1Delta: r2port_ao Install: sso env=%d sso cvar=%d\n", g_ssoEnv ? 1 : 0, g_ssoOn.load() ? 1 : 0);
    ReloadConfig();
    Refresh();
}

} // namespace r2port::ao
