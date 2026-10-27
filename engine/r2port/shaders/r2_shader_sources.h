#pragma once

// Embedded HLSL for the R2 port shader track. Each entry becomes `shaders\fxc\<name>_ps40.vcs` (and
// `<name>_vs40.vcs` when the source defines a vertex shader) and is selected by a converted VMT:
//
//   "screenspace_general"
//   {
//       "$pixshader"     "<name>"
//       "$vertexshader"  "<name>"      // only when the source has a vertex shader
//       ...
//   }
//
// Loose files `<gamedir>\r2port\shaders\<name>.hlsl` override (or add) a shader without a rebuild;
// see r2_shader_cache.cpp. Keep every literal below 16 KB (MSVC limit for one string literal).
//
// Register conventions (R1 screenspace_general, see docs/r2port.md "Shader track"):
//   b0  CBufScreenspaceGeneral: $c0_x..$c4_w as five float4
//   b2  CBufCommonPerCamera, b3 CBufModelInstance: bound for model/brush draws; the stock vertexTransform
//       vertex shader reads them, ours does the same
//   t0..t3 / s0..s3: $basetexture, $texture1, $texture2, $texture3
// Vertex-shader static combos follow the stock default VS: bit 0 = vertex colour ($vertexcolor),
// bit 1 = $vertexTransform. The compiler defines R2_VCOLOR and R2_XFORM per combo.

namespace r2port::shaders {

struct EmbeddedShader {
    const char* name;
    const char* source;
};

inline constexpr const char kCommonInclude[] = R"HLSL(
// r2_common.hlsli
cbuffer CBufScreenspaceGeneral : register(b0)
{
    float4 g_c0;
    float4 g_c1;
    float4 g_c2;
    float4 g_c3;
    float4 g_c4;
};

cbuffer CBufCommonPerCamera : register(b2)
{
    float3 c_cameraOrigin;
    float c_zNear;
    row_major float4x4 c_cameraRelativeToClip;
    float4 c_clipPlane;
    float4 c_fogK0;
    float4 c_fogK1;
    float4 c_fogK2;
    float4 c_fogK3;
    float4 c_fogK4;
    float3 c_skyColor;
    float c_shadowBleedFudge;
    float c_envMapLightScale;
    float3 c_sunColor;
    float3 c_sunDir;
    float c_gameTime;
};

cbuffer CBufModelInstance : register(b3)
{
    row_major float3x4 c_objectToCameraRelative;
    float4 c_diffuseModulation;
};

#ifndef R2_VCOLOR
#define R2_VCOLOR 0
#endif
#ifndef R2_XFORM
#define R2_XFORM 0
#endif

struct R2VSIn
{
    float3 pos : POSITION;
#if R2_VCOLOR
    float4 color : COLOR0;
#endif
    float2 uv : TEXCOORD0;
};

struct R2VSOut
{
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
    float4 camRel : TEXCOORD2;   // xyz: camera-relative position, w: game time
    float3 objRel : TEXCOORD3;   // camera-relative object origin
    float4 pos : SV_POSITION;
};

R2VSOut R2StandardVS(R2VSIn i)
{
    R2VSOut o;
    o.uv = i.uv;
#if R2_VCOLOR
    o.color = i.color;
#else
    o.color = float4(1.0, 1.0, 1.0, 1.0);
#endif
#if R2_XFORM
    const float4 p4 = float4(i.pos, 1.0);
    const float3 cr = float3(
        dot(p4, c_objectToCameraRelative[0]),
        dot(p4, c_objectToCameraRelative[1]),
        dot(p4, c_objectToCameraRelative[2]));
    o.pos = mul(c_cameraRelativeToClip, float4(cr, 1.0));
    o.objRel = float3(c_objectToCameraRelative[0].w, c_objectToCameraRelative[1].w, c_objectToCameraRelative[2].w);
    o.camRel = float4(cr, c_gameTime);
#else
    o.pos = float4(i.pos, 1.0);
    o.objRel = float3(0.0, 0.0, 0.0);
    o.camRel = float4(i.pos, 0.0);
#endif
    return o;
}

#define R2_STANDARD_VS R2VSOut VSMain(R2VSIn i) { return R2StandardVS(i); }
)HLSL";

// Lab shader: one VMT per mode, selected by $c0_x. Answers "what does this draw path give a custom shader".
inline constexpr const char kProbeSource[] = R"HLSL(
#include "r2_common.hlsli"

Texture2D<float4> BaseTex : register(t0);
SamplerState BaseSmp : register(s0);
TextureCube<float4> CubeTex : register(t2);
SamplerState CubeSmp : register(s2);

R2_STANDARD_VS

// mode ($c0_x): 0 flat magenta, 1 view direction as colour, 2 $basetexture times UV gradient,
// 3 cube map ($texture2) along the view direction, 4 game time (flashes white once a second),
// 5 camera-relative object origin (length / 4096), 6 vertex colour, 7 approximate dome normal.
float4 PSMain(R2VSOut i) : SV_Target
{
    const float mode = g_c0.x;
    const float3 v = normalize(i.camRel.xyz);
    if (mode < 0.5)
        return float4(1.0, 0.0, 1.0, 1.0);
    if (mode < 1.5)
        return float4(abs(v), 1.0);
    if (mode < 2.5)
        return float4(BaseTex.Sample(BaseSmp, i.uv).rgb * float3(saturate(i.uv.x), saturate(i.uv.y), 1.0 - saturate(i.uv.x)), 1.0);
    if (mode < 3.5)
        return float4(CubeTex.Sample(CubeSmp, v).rgb, 1.0);
    if (mode < 4.5)
        return float4(frac(i.camRel.w).xxx, 1.0);
    if (mode < 5.5)
        return float4((length(i.objRel) / 4096.0).xxx, 1.0);
    if (mode < 6.5)
        return float4(i.color.rgb, 1.0);
    return float4(abs(normalize(i.camRel.xyz - i.objRel)), 1.0);
}
)HLSL";

// World dome of R2 UnlitTwoTexture materials whose second texture is a cube map
// (world/test/kodai_fake_screen_01_world, skies/boomtowncubemap_*). R1's UnlitTwoTexture binds $texture2 as
// a 2D texture, so the cube layer is empty there. Layer 1 is the scan-line texture, layer 2 the cube map
// sampled along the view direction; both are weighted by a normal-to-view fresnel term and added
// ($addlayers). The dome normal is approximated as (position - object origin), which is exact for a sphere.
//
//   $c0.xyz layer 1 colour   $c0.w layer 1 alpha
//   $c1.x fresnel 1 inner    $c1.y outer   $c1.z exponent   $c1.w layer 1 uv scale
//   $c2.xyz layer 2 colour   $c2.w cube mip bias
//   $c3.x fresnel 2 inner    $c3.y outer   $c3.z exponent   $c3.w scroll speed (uv per second)
//   $c4.x overall scale (HDR / exposure match)
inline constexpr const char kDomeSource[] = R"HLSL(
#include "r2_common.hlsli"

Texture2D<float4> ScanTex : register(t0);
SamplerState ScanSmp : register(s0);
TextureCube<float4> SkyCube : register(t2);
SamplerState SkySmp : register(s2);

R2_STANDARD_VS

float Fresnel(float inner, float outer, float exponent, float nv)
{
    return lerp(inner, outer, pow(saturate(1.0 - nv), max(exponent, 0.01)));
}

float4 PSMain(R2VSOut i) : SV_Target
{
    const float3 v = normalize(i.camRel.xyz);
    const float3 n = normalize(i.camRel.xyz - i.objRel);
    const float nv = abs(dot(n, v));

    const float2 uv = i.uv * g_c1.w + float2(0.0, g_c3.w * i.camRel.w);
    const float3 scan = ScanTex.Sample(ScanSmp, uv).rgb * g_c0.rgb * g_c0.a;
    const float3 sky = SkyCube.SampleBias(SkySmp, v, g_c2.w).rgb * g_c2.rgb;

    const float3 color = scan * Fresnel(g_c1.x, g_c1.y, g_c1.z, nv)
                       + sky * Fresnel(g_c3.x, g_c3.y, g_c3.z, nv);
    return float4(color * g_c4.x, 1.0);
}
)HLSL";

inline constexpr EmbeddedShader kEmbeddedShaders[] = {
    { "r2_probe", kProbeSource },
    { "r2_dome", kDomeSource },
};

} // namespace r2port::shaders
