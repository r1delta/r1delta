#pragma once

// LightmappedGeneric pixel-shader replacement (R2 Uber port), see r2_shader_override.cpp and docs/r2port.md.
// Generated from shaderlab/uber/hlsl by tools/hlsl2hdr.py; the HLSL file is the source of truth.

namespace r2port::shaders {

inline constexpr const char kUberLmgSource[] =
    R"HLSL(
// R2_PS_DYNAMIC 12 5
// R2 port, R1 LightmappedGeneric pixel-shader replacement ("uber"). One source, feature macros chosen per material by the
// draw-state hook. Everything marked "stock" was reconstructed from the behaviour of the stock ps50 records (resource
// bindings and constants read through reflection) and is proven against the stock records by tools/uberdiff (all 12
// dynamic variants, relative difference below 2e-4). R2 terms exist only behind the R2_T_* macros and are absent in
// neutral mode.
// Dynamic index = R2_LIGHTS + 2 * (R2_SHADOW + 3 * R2_FOGB) like the stock file (hardware lights, shadow mode 0/1/2
// = baked only / one cascade / blended cascades, fog flavour).
//
// Material inputs (VMT): $basetexture t0, $bumpmap t1, $reflectionTint t2 (rgb F0, a gloss: the stock shader reads a for
// its mip / lobe exponent / fresnel weight too), $envmap t5 (R2_ENV), AO and cavity packed in $detailColor t4
// (single layer) or $blendModulateTexture t15 b / a (layers) when R2_T_PACK. Two-layer materials add $baseTexture2 t11,
// $bumpmap2 t12, $reflectionTint2 t13, $blendModulateTexture t15 (r, g = stock blend range).
#ifndef R2_SPEC_SRGB
#define R2_SPEC_SRGB 0   // 0: F0 = the sampled (sRGB-decoded) _spc like R2, which reads the sRGB flagged texture through an sRGB view; 1 re-encodes to the stored byte
#endif
#ifndef R2_STATIC
#define R2_STATIC 0
#endif
// static combos: 0 single layer, 1 single layer + TEXCOORD1.xy (detail UV), 2 two layers (TEXCOORD3.w = vertex blend),
// 3 two layers + TEXCOORD1.xy, 4 two layers + TEXCOORD1.xy + detail normals (t3 / t4 at the detail UV)
#if R2_STATIC == 1
#define R2_TC1 1
#define R2_L2 0
#define R2_DN 0
#elif R2_STATIC == 2
#define R2_TC1 0
#define R2_L2 1
#define R2_DN 0
#elif R2_STATIC == 3
#define R2_TC1 1
#define R2_L2 1
#define R2_DN 0
#elif R2_STATIC == 4
#define R2_TC1 1
#define R2_L2 1
#define R2_DN 1
#else
#define R2_TC1 0
#define R2_L2 0
#define R2_DN 0
#endif
#ifndef R2_ENV
#define R2_ENV 0   // 0 none, 1 $envmap, 2 $envmap with $envmapLightScale > 0 (the stock combo env bits)
#endif
#ifndef R2_ARR
#define R2_ARR 0   // stock shadow-array statics (render flags & 0x2000000): real-time lights read a variance shadow map array at t7
#endif
#ifndef R2_ATEST
#define R2_ATEST 0  // $alphatest (flags & 0x10): discard against cb0[6].w, alpha 1
#endif
// R2 terms (all off in neutral mode, which reproduces the stock shader):
//   R2_T_LOD   cube mip from roughness (rough * 6 + R2_CUBE_LOD_BIAS) instead of the stock shininess-derived mip
//   R2_T_FENV  analytic environment BRDF lerp(F0, max(F0, (1-rough)^2), (1-NdotV)^5) instead of the stock fresnel mix
//   R2_T_SO    lightmap specular occlusion: env *= lerp(1, saturate(light), scale)
//   R2_T_GGX   GGX lobes for sun, real-time and hardware lights and the baked-direction highlight
//   R2_T_PACK  AO and cavity from the packed carrier (single layer: $detailColor t4 .g/.b, layers: blend texture .b/.a)
#ifndef R2_NEUTRAL
#define R2_NEUTRAL 0
#endif
#if R2_NEUTRAL
#undef R2_T_LOD
#undef R2_T_FENV
#undef R2_T_SO
#undef R2_T_GGX
#undef R2_T_PACK
#endif
#ifndef R2_CUBE_LOD_BIAS
#define R2_CUBE_LOD_BIAS -2.0   // our probes are 64x64 (R2 256x256): R2 mip m is our mip m - 2
#endif
#ifndef R2_ENV_GAIN
#define R2_ENV_GAIN 0.25        // R2_T_FENV: gain on the R2 environment BRDF (0.25 is an empirical pick at the SUBSTATION wall: gain 1 / 0.5 / 0.2 / 0.08 give wall/crates 0.578 / 0.508 / 0.456 / 0.432, stock 0.474, R2 0.339; the remaining gap to R2 is diffuse, not env) (the VMT $envmapTint is the converter's calibration of the stock term and is not used)
#endif
#ifndef R2_GLOSS_POW
#define R2_GLOSS_POW 2          // t2.a carries gloss^R2_GLOSS_POW (2: the packer stores gloss^2 because the stock fresnel weight is t2.a * f(NV); 1 = linear gloss)
#endif
#ifndef R2_SO_SCALE
#define R2_SO_SCALE 1.0         // R2_T_SO strength when the material has no $envmapLightScale
#endif
// R2_ROUGH_FORCE <value>: test aid, defining it replaces the roughness of every pixel
#ifndef R2_LIGHTS
#define R2_LIGHTS 0
#endif
#ifndef R2_SHADOW
#define R2_SHADOW 0
#endif
#ifndef R2_FOGB
#define R2_FOGB 0
#endif
#ifndef R2_MARK
#define R2_MARK 0
#endif
#ifndef R2_DEBUG
#define R2_DEBUG 0
#endif

cbuffer CBufLightmappedStatic : register(b0) { float4 cb0[21]; };
cbuffer CBufCommonPerCamera : register(b2) { float4 cb2[28]; };
cbuffer CBufModelInstance : register(b3) { float4 cb3[9]; };

struct Light { float3 color; int shadowIndex; float4 pos; float4 spot; float4 att; float4 axisX; float4 axisY; };
struct TonemapElement { float c_lightScale; uint c_historyPos; float c_hist[10]; };
StructuredBuffer<TonemapElement> tonemapGlobals : register(t16);
StructuredBuffer<Light> g_lights : register(t18);
Texture2D<uint4> g_rtl : register(t20);

Texture2D<float4> baseTexture : register(t0);
Texture2D<float4> bumpTexture : register(t1);
Texture2D<float4> reflectionTintTexture : register(t2);
#if R2_T_PACK && !R2_DN && !R2_L2
Texture2D<float4> packedTexture : register(t4);
#endif
#if R2_L2
Texture2D<float4> baseTexture2 : register(t11);
Texture2D<float4> bumpTexture2 : register(t12);
Texture2D<float4> reflectionTintTexture2 : register(t13);
Texture2D<float4> blendTexture : register(t15);
#endif
#if R2_DN
Texture2D<float4> detailNormalTexture : register(t3);
Texture2D<float4> detailNormalTexture2 : register(t4);
#endif
#if R2_ENV
TextureCube<float4> envTexture : register(t5);
SamplerState envSampler : register(s5);
#endif
#if R2_ARR
Texture2DArray<float4> lightShadowMaps : register(t7);
SamplerState lightShadowSampler : register(s7);
#endif
Texture2D<float4> csmAtlas : register(t6);
Texture2D<float4> cloudMask : register(t8);
Texture2D<float4> lightmap0 : register(t9);
Texture2D<float4> lightmap1 : register(t10);
SamplerState baseSampler : register(s0);
SamplerState bumpSampler : register(s1);
SamplerState reflectionTintSampler : register(s2);
#if R2_T_PACK && !R2_DN && !R2_L2
SamplerState packedSampler : register(s4);
#endif
#if R2_L2
SamplerState baseSampler2 : register(s11);
SamplerState bumpSampler2 : register(s12);
SamplerState reflectionTintSampler2 : register(s13);
SamplerState blendSampler : register(s15);
#endif
#if R2_DN
SamplerState detailSampler : register(s3);
SamplerState detailSampler2 : register(s4);
#endif
SamplerComparisonState csmSampler : register(s6);
SamplerState cloudSampler : register(s8);
SamplerState lightmap0Sampler : register(s9);
SamplerState lightmap1Sampler : register(s10);

struct PSIn
{
    centroid float4 uv : TEXCOORD0;      // xy base, zw lightmap
#if R2_TC1
    centroid float2 uvDetail : TEXCOORD1; // declared only to keep the register layout of the detail vertex-shader combos
#endif
    centroid float3 pos : TEXCOORD2;     // camera-relative position
#if R2_L2
    centroid float4 nrm : TEXCOORD3;     // w = vertex blend
#else
    centroid float3 nrm : TEXCOORD3;
#endif
    centroid float3 tan : TEXCOORD4;
    centroid float3 bin : TEXCOORD5;
    float4 color : COLOR0;
};

float Pcf(float2 uv, float z)
{
    float a = csmAtlas.SampleCmpLevelZero(csmSampler, uv, z, int2(1, 1)).x + csmAtlas.SampleCmpLevelZero(csmSampler, uv, z, int2(-1, 1)).x
            + csmAtlas.SampleCmpLevelZero(csmSampler, uv, z, int2(1, -1)).x + csmAtlas.SampleCmpLevelZero(csmSampler, uv, z, int2(-1, -1)).x;
    float b = csmAtlas.SampleCmpLevelZero(csmSampler, uv, z, int2(1, 0)).x + csmAtlas.SampleCmpLevelZero(csmSampler, uv, z, int2(-1, 0)).x
            + csmAtlas.SampleCmpLevelZero(csmSampler, uv, z, int2(0, -1)).x + csmAtlas.SampleCmpLevelZero(csmSampler, uv, z, int2(0, 1)).x;
    return a * 0.0625 + b * 0.125 + csmAtlas.SampleCmpLevelZero(csmSampler, uv, z, int2(0, 0)).x * 0.25;
}

float Shadow(float3 cam, float baked)
{
    float3 p = cam.x * cb2[15].xyz + cb2[14].xyz;
    p = cam.y * cb2[16].xyz + p;
    p = cam.z * cb2[17].xyz + p;
#if R2_SHADOW == 0
    return baked;
#elif R2_SHADOW == 1
    float2 e = saturate(abs(p.xy) * cb2[16].w + cb2[17].w);
    float w = e.y * e.x;
    if (w != 0.0) {
        float s = Pcf(p.xy * cb2[20].xy + cb2[20].zw, p.z);
        baked = baked + w * (s - baked);
    }
    return baked;
#else
    float4 q = p.xyxy * cb2[18] + cb2[19];
    float2 c0 = saturate(abs(p.xy) * cb2[16].w + cb2[17].w);
    float4 c1 = saturate(abs(q) * cb2[16].w + cb2[17].w);
    float3 m = float3(c0.y * c0.x, c1.y * c1.x, c1.w * c1.z);
    float3 f = (m > 0.0) ? 1.0 : 0.0;
    f.y = f.y - f.y * f.x;
    float s01 = f.y + f.x;
    f.z = f.z - f.z * s01;
    float wsel = dot(m, f);
    float2 uv0 = p.xy * cb2[20].xy + cb2[20].zw;
    float4 a12 = q * cb2[21] + cb2[22];
    float2 uv = uv0 * f.x + f.y * a12.xy + a12.zw * f.z;
    float shadowA = Pcf(uv, p.z);
    if (wsel < s01) {
        float2 uv2 = f.y * a12.zw + a12.xy * f.x;
        baked = Pcf(uv2, p.z);
    }
    return baked + wsel * (shadowA - baked);
#endif
}

float SpecLobe(float nh, float ndl, float ndv, float rough, float expo)
{
#if R2_T_GGX
    // GGX shape normalised to a peak of 1 (the stock lobe is a peak-1 pow), dimmed with roughness
    float a = rough * rough;
    float a2 = a * a;
    float d = nh * nh * (a2 - 1.0) + 1.0;
    return (a2 * a2) / (d * d) * (1.0 - 0.75 * rough);
#else
    return pow(nh, expo);
#endif
}

void AddLight(uint index, bool shadowed, float3 N, float3 V, float3 worldPos, float rough, float expo, float ndv, inout float3 diff, inout float3 spec)
{
    Light L = g_lights[index];
    float w2 = L.pos.w * L.pos.w;
    float3 toL = L.pos.xyz - worldPos * L.pos.w;
    float d2 = min(max(w2, dot(toL, toL)), 1.0);
    float invd = rsqrt(d2);
    float den = 1.0 + d2 * (invd * L.att.y + L.att.z);
    float c = saturate(dot(L.spot.xyz, toL) * invd + L.spot.w);
    c = c + L.att.x * (c * c - c);
    float wgt = den * c;
    if (wgt != 0.0) {
#if R2_ARR
        if (shadowed && L.shadowIndex < 4096) {   // one-sided Chebyshev on the variance shadow map of this light
            const float rd = rcp(dot(L.spot.xyz, toL));
            const float2 uv = float2(dot(L.axisX.xyz, toL) * rd + 0.5, dot(L.axisY.xyz, toL) * rd + 0.5);
            const float2 m = lightShadowMaps.SampleLevel(lightShadowSampler, float3(uv, float(L.shadowIndex)), 0).xy;
            const float var = max(w2 * cb2[23].y, m.y - m.x * m.x);
            const float dd = max(d2 * invd - m.x, 0.0);
            wgt *= var / (dd * dd + var);
        }
#endif
        float ndl = saturate(invd * dot(N, toL));
        diff += wgt * ndl * L.color;
        float3 h = toL * invd - V;
        float nh = saturate(dot(N, h) * rsqrt(dot(h, h)));
        spec += wgt * ndl * SpecLobe(nh, ndl, ndv, rough, expo) * L.color;
    }
}

float4 PSMain(PSIn i
#if R2_DEBUG >= 6
    , float4 sp : SV_Position
#endif
    ) : SV_Target
{
    float4 base = baseTexture.Sample(baseSampler, i.uv.xy);
    float4 lm0 = lightmap0.Sample(lightmap0Sampler, i.uv.zw);
    const float4 lm1 = lightmap1.Sample(lightmap1Sampler, i.uv.zw);
    const float3 lmEnergy = lm0.rgb * (lm0.a * 3.984375 + 0.015625);
    const float3 lmDir = lm1.rgb * 2.007874 - 1.0;
    const float bakedSun = 1.0 - lm1.a;

    float3 nts = bumpTexture.Sample(bumpSampler, i.uv.xy).xyz * cb0[9].yzw + cb0[10].xyz;
    float4 rt = reflectionTintTexture.Sample(reflectionTintSampler, i.uv.xy);
    float3 rtc = rt.x + cb0[3].w * (rt.xyz - rt.x);                       // stock: tint colour (lerp grey / colour)
    float3 f0 = rtc * cb0[2].xyz;
    float3 envTint = rtc * cb0[3].xyz;
    float envLambertian = cb0[8].w;
    float envLightScale = cb0[9].x;
    float specExp = cb0[2].w;
    float rtAlpha = rt.a;
    float fresnelAmount = cb0[10].w;
    float fresnelMax = cb0[11].x;
    float fresnelSharp = cb0[12].w;
    float ao = 1.0, cav = 1.0;
#if R2_L2
    // layer blend: stock smoothstep over the range given by the blend texture's r / g and the vertex alpha (TEXCOORD3.w)
    const float4 bm = blendTexture.Sample(blendSampler, i.uv.xy);
    const float lo = max(bm.y - bm.x - 0.001, 0.0);
    const float hi = min(bm.x + bm.y + 0.001, 1.0);
)HLSL"
    R"HLSL(    const float sb = saturate(((1.0 - i.nrm.w) - lo) / (hi - lo));
    const float blend = sb * sb * (3.0 - 2.0 * sb);
    base = lerp(base, baseTexture2.Sample(baseSampler2, i.uv.xy), blend);
    nts = lerp(nts, bumpTexture2.Sample(bumpSampler2, i.uv.xy).xyz * cb0[17].yzw + cb0[18].xyz, blend);
#if R2_DN
    {   // detail normals (one per layer) at the detail UV, combined as in the stock shader
        float3 dn = detailNormalTexture.Sample(detailSampler, i.uvDetail).xyz * cb0[11].yzw + cb0[12].xyz;
        const float3 dn2 = detailNormalTexture2.Sample(detailSampler2, i.uvDetail).xyz * cb0[19].yzw + cb0[20].xyz;
        dn = lerp(dn, dn2, blend);
        nts = nts * dn.z + nts.z * dn;
        nts.z *= 0.5;   // stock: the combined normal's z is halved before the tangent basis (measured on static 9493)
    }
#endif
    const float4 rt2 = reflectionTintTexture2.Sample(reflectionTintSampler2, i.uv.xy);
    const float3 rtc2 = rt2.x + cb0[16].w * (rt2.xyz - rt2.x);
    rtc = lerp(rtc, rtc2, blend);
    f0 = lerp(f0, rtc2 * cb0[15].xyz, blend);
    envTint = lerp(envTint, rtc2 * cb0[16].xyz, blend);
    envLambertian = lerp(cb0[8].w, cb0[14].z, blend);
    envLightScale = lerp(cb0[9].x, cb0[17].x, blend);
    specExp = lerp(cb0[2].w, cb0[15].w, blend);
    rtAlpha = lerp(rt.a, rt2.a, blend);
    fresnelAmount = lerp(cb0[10].w, cb0[18].w, blend);
    fresnelMax = lerp(cb0[11].x, cb0[19].x, blend);
    fresnelSharp = lerp(cb0[12].w, cb0[20].w, blend);
#if R2_T_PACK
    ao = bm.z; cav = bm.w;
#endif
#elif R2_T_PACK
    {
        const float3 pkk = packedTexture.Sample(packedSampler, i.uv.xy).rgb;
        ao = pkk.g; cav = pkk.b;
    }
#endif
    const float expo = abs(min(1.0 / pow(cb0[4].z - cb0[4].w * rtAlpha, 2.0), -specExp));   // stock phong exponent
    // R2 terms read gloss from the alpha of the reflection tint texture, F0 from its (stored, not sRGB-decoded) colour
#if R2_T_GGX || R2_T_LOD || R2_T_FENV
    float3 f0R2 = rtc;
#if R2_SPEC_SRGB
    f0R2 = (f0R2 <= 0.0031308) ? f0R2 * 12.92 : 1.055 * pow(max(f0R2, 1e-6), 1.0 / 2.4) - 0.055;
#endif
    f0R2 *= cb0[2].xyz;
#ifdef R2_ROUGH_FORCE
    const float rough = max(R2_ROUGH_FORCE, 0.04);
#else
#if R2_GLOSS_POW == 2
    const float rough = max(1.0 - sqrt(rtAlpha), 0.04);
#else
    const float rough = max(1.0 - rtAlpha, 0.04);
#endif
#endif
#else
    const float rough = 0.5;
#endif
    const float3 pk = float3(rtAlpha, ao, cav);
#if R2_DEBUG == 6
    {   // 4 x 2 panels (1920 x 1080): pk, N, rt x 20, base / lightmap x 10, F0 x 100, expo / 1000, 1-dot(N,geo normal)
        const uint panel = uint(sp.x / 480.0) + 4u * uint(sp.y / 540.0);
        float3 nn = normalize(nts.x * i.tan + nts.y * i.bin + nts.z * i.nrm.xyz);
        float3 v[8] = { pk, nn * 0.5 + 0.5, rt.rgb * 20.0, base.rgb, lmEnergy * 10.0, f0 * 100.0, expo.xxx / 1000.0, (1.0 - dot(nn, normalize(i.nrm.xyz))).xxx * 4.0 };
        return float4(v[min(panel, 7u)], 1.0);
    }
#endif
    float3 albedo = base.rgb * i.color.rgb;
    float alpha = base.a * i.color.a;
#if R2_ATEST
    clip(alpha - cb0[6].w);
    alpha = 1.0;
#endif

    float3 N = normalize(nts.x * i.tan + nts.y * i.bin + nts.z * i.nrm.xyz);
    float distSq = dot(i.pos, i.pos);
    float invDist = rsqrt(distSq);
    float3 V = i.pos * invDist;                 // camera -> pixel
    float3 worldPos = i.pos + cb2[0].xyz;
    const float ndvRaw = dot(N, -V);
    float ndv = saturate(ndvRaw);
    float fr = saturate(1.0 - 1.743447 * ndvRaw);
    fr *= fr;
    fr = fr * ((1.0 - fresnelSharp) + fresnelSharp * fr);

    // real-time light list (one texel of t20 per lightmap texel, up to four indices)
    uint2 rtlSize;
    g_rtl.GetDimensions(rtlSize.x, rtlSize.y);
    uint4 rtlIds = g_rtl.Load(int3(int2(float2(rtlSize) * i.uv.zw), 0));
    float3 diffRtl = 0.0, specRtl = 0.0;
    [loop] for (uint k = 0; k < 4; ++k) {
        uint idx = rtlIds[k];
        if (idx == 255)
            break;
        AddLight(idx + 32, true, N, V, worldPos, rough, expo, ndv, diffRtl, specRtl);
    }

    // sun with CSM + cloud mask, lightmap, bumped sky weight
    float shadow = Shadow(i.pos, bakedSun);
    float2 cloudUv = i.pos.x * cb2[26].zw + cb2[26].xy;
    cloudUv = i.pos.y * cb2[27].xy + cloudUv;
    cloudUv = i.pos.z * cb2[27].zw + cloudUv;
    float3 sunVis = shadow * cloudMask.Sample(cloudSampler, cloudUv).xyz;
    float ndg = dot(N, i.nrm.xyz);
    float ng2 = ndg * ndg;
    float lenSq = dot(lmDir, lmDir);
    float ndDir = dot(N, lmDir);
    float skyW = saturate((max(lenSq, ndDir) + ndDir) * 0.5 * sqrt(lenSq)) * (1.0 + lenSq * (ng2 - 1.0));
    float3 sun = sunVis * saturate(dot(N, cb2[13].xyz));
    float3 sunCol = sun * cb2[12].yzw;
    float3 hs = cb2[13].xyz - V;
    float sunNh = saturate(dot(N, hs) * rsqrt(dot(hs, hs)));
    float3 specAcc = sunCol * SpecLobe(sunNh, saturate(dot(N, cb2[13].xyz)), ndv, rough, expo) + specRtl;

    float3 lmTerm = lmEnergy * ng2 + skyW * cb2[11].xyz + sunCol;
    float3 diffAcc = cb0[7].xyz * lmTerm + diffRtl;
#if R2_T_PACK
    // AO on the baked / sky / ambient part only (never on the sun or the real-time lights)
    diffAcc = cb0[7].xyz * (lmEnergy * ng2 + skyW * cb2[11].xyz) * ao + cb0[7].xyz * sunCol + diffRtl;
    diffAcc += cb0[8].xyz * ao;
#else
    diffAcc += cb0[8].xyz;
#endif
#if R2_T_GGX
    {   // highlight along the baked light direction (lightmap plane B)
        float lmLen = sqrt(lenSq);
        float3 ld = lmDir / max(lmLen, 1e-4);
        float3 hl = ld - V;
        float lmNh = saturate(dot(N, hl) * rsqrt(dot(hl, hl)));
        float lmNdl = saturate(dot(N, ld));
        specAcc += cb0[7].xyz * lmEnergy * saturate(lmLen) * lmNdl * SpecLobe(lmNh, lmNdl, ndv, rough, expo) * 0.5;
    }
#endif

#if R2_LIGHTS
    uint packed = asuint(cb3[8].w);
    [loop] while (packed != 0) {
        AddLight(packed & 127, false, N, V, worldPos, rough, expo, ndv, diffAcc, specAcc);
        packed >>= 8;
    }
#endif

    // specular: stock fresnel mix of the light-list specular
#if R2_T_GGX
    float fw = fr * fresnelAmount;
    float3 fresnel = f0R2 + fw * (fresnelMax - f0R2);
    float3 color = albedo * diffAcc + fresnel * specAcc * cav;
#else
    float fw = rtAlpha * fr * fresnelAmount;
    float3 fresnel = f0 + fw * (fresnelMax - f0);
    float3 color = albedo * diffAcc + fresnel * specAcc;
#endif
#if R2_ENV
    {
        const float3 refl = V + 2.0 * ndvRaw * N;
        float3 envFactor;
        float lod;
        float envW;
#if R2_T_LOD
        uint cw, ch, cmips;
        envTexture.GetDimensions(0, cw, ch, cmips);
        lod = min(rough * 6.0 + R2_CUBE_LOD_BIAS, float(cmips - 1u));
#else
        lod = cb0[4].y * log2(max(expo * 0.25 - 2.0, 0.25)) + cb0[4].x;
#endif
        const float4 cube = envTexture.SampleLevel(envSampler, refl, lod);
#if R2_T_FENV
        {   // R2 analytic environment BRDF (no light-scale / lambertian shaping of the stock term)
            const float h = (1.0 - rough) * (1.0 - rough);
            const float b = 1.0 - abs(ndvRaw);
            const float b2 = b * b;
            envFactor = lerp(f0R2, max(f0R2, h.xxx), b2 * b2 * b) * R2_ENV_GAIN;
            envW = cb2[12].x;
        }
#else
        {
            const float envLambert = saturate(envLambertian - 0.5 * rtAlpha);
            envW = saturate(envLambert * (1.0 - ndvRaw) + ndvRaw) * cb2[12].x;
            envFactor = envTint + fw * (fresnelMax - envTint);
        }
#endif
        float3 env = (envW * (cube.a * 3.984375 + 0.015625)) * cube.rgb;
#if R2_T_SO
        env = env * lerp(1.0, saturate(diffAcc), max(envLightScale, R2_SO_SCALE));
#elif R2_ENV == 2
        env = env * lerp(1.0, saturate(diffAcc), envLightScale);
#endif
#if R2_T_PACK
        env *= cav;
#endif
        color += env * envFactor;
    }
#endif
#if R2_DEBUG == 7
    {   // 4 x 2 panels: lit diffuse, lit specular, specular before F, diffuse light, N, gloss/ao/cav, sky/ng2/shadow, rough/expo/lightScale
        const float ls = tonemapGlobals[0].c_lightScale;
        const uint panel = uint(sp.x / 480.0) + 4u * uint(sp.y / 540.0);
        float3 v[8] = { albedo * diffAcc * ls, fresnel * specAcc * cav * ls, specAcc * ls, diffAcc * ls, N * 0.5 + 0.5,
                        pk, float3(skyW, ng2, shadow), float3(rough, expo / 100.0, ls * 10.0) };
        return float4(v[min(panel, 7u)], 1.0);
    }
#endif
    // fog
    const float3 colorPre = color;
    float3 fogDbg = 0.0;
#if !R2_FOGB
    float d = max(sqrt(distSq) + cb2[7].w, 1e-5);
    float zmax = max(worldPos.z, cb2[0].z);
    float zmin = min(worldPos.z, cb2[0].z);
    float dz = zmax - zmin;
    float inv = 1.0 / max(dz, 1e-5);
    float2 t = saturate(inv * (cb2[10].xy - zmin));
    float a1 = d * t.x;
    float a2 = d * t.y - a1;
    float a3 = d - d * t.y;
    float2 g = saturate(float2(dz, zmin - cb2[10].x) * cb2[10].z);
    float b1 = cb2[6].z * g.y + cb2[6].x;
    float b0 = g.x * cb2[6].z;
    float q = b0 / d;
    float b2 = cb2[6].x * a1 + a3 * cb2[6].y;
    float amount = (a2 * q) * 0.5 + b1;
    amount = amount * a2 + b2;
    amount = 1.0 - exp2(-amount);   // the stock bytecode uses the base-2 exp instruction
    float sunFog = saturate((-dot(cb2[9].xyz, -V) - cb2[8].w) * cb2[9].w);
    sunFog *= sunFog;
    float3 fogColor = cb2[8].xyz * sunFog + cb2[7].xyz;
    amount *= cb2[6].w;
    color = color + amount * (fogColor * cb0[7].w - color);
    fogDbg = float3(amount, sunFog, d * 0.001);
#else
    float hz = cb2[10].w - max(cb2[10].w, 0.0);
    float vz = min(i.pos.z, 0.0);
    float s = 1.0 - hz / vz;
    float dist = sqrt(distSq);
    float sd = s * dist;
    float len = -i.pos.z * s + sd;
    float3 trans = exp2(len * cb2[6].xyz);
    float scat = (sd * cb0[7].w) / max(len, 0.0);
    float3 inscatter = scat * cb2[7].xyz * max(1.0 - trans, 0.0);
    color = (color * trans + inscatter) * cb2[10].xyz;
    fogDbg = trans;
#endif
#if R2_DEBUG == 8
    {   // 4 x 2 panels: colour before fog, after fog (both x lightScale), fog values, k6, k7, k10, scat/len/dist
        const float ls = tonemapGlobals[0].c_lightScale;
        const uint panel = uint(sp.x / 480.0) + 4u * uint(sp.y / 540.0);
        float3 v[8] = { colorPre * ls, color * ls, fogDbg, cb2[6].xyz * 10.0 + 0.5, cb2[7].xyz, cb2[10].xyz, cb2[8].xyz, float3(cb2[6].w, cb2[10].w, cb0[7].w) };
        return float4(v[min(panel, 7u)], 1.0);
    }
#endif
#if R2_MARK
    color *= float3(1.0, 0.2, 0.2);   // test aid: tints every replaced pixel (r2_lmg_mark)
#endif
    return float4(color * tonemapGlobals[0].c_lightScale, alpha);
}
)HLSL";

} // namespace r2port::shaders
