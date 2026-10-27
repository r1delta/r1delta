#pragma once

// Embedded HLSL for the R1 LightmappedGeneric pixel-shader replacement (Phase 2 of the R2 port shader track, see
// r2_shader_override.cpp and docs/r2port.md). Included by r2_shader_sources.h. The stock lighting terms below were
// reconstructed from the behaviour of the stock ps50 bytecode (resource bindings and constant layout read through
// reflection); no Respawn source or bytes are copied. `// R2_PS_DYNAMIC 12 5` tells the compiler to build the 12 stock
// dynamic variants of five static combos (see BuildShader).

namespace r2port::shaders {

inline constexpr const char kLmgPbrSource[] =
    R"HLSL(
// R2_PS_DYNAMIC 12 5
// R2 port: pixel shader replacement for R1 LightmappedGeneric (world brushes), static pixel combo family of
// "single layer, $bumpmap + $reflectionTint, no envmap, no detail normal, real-time-light texture (t20)", i.e. what
// converted R2 world materials select (ps50 combo 9229 with VS combos that output TEXCOORD0/2/3/4/5 + COLOR0).
// The stock lighting terms are reproduced from the stock bytecode behaviour (lightmap decode, bumped sky weight, CSM
// PCF with cloud mask, real-time light list from t20, hardware light list, fog, c_lightScale), and the R2 material
// terms are added: gloss -> GGX specular, F0 from $reflectionTint, AO on the lightmap/sky/ambient term, cavity on
// specular. The stock dynamic combo (0..11) is built from R2_LIGHTS (0/1 hardware lights), R2_SHADOW (0 none, 1 one cascade,
// 2 blended cascades) and R2_FOGB (0/1 fog flavour): dynamic index = R2_LIGHTS + 2 * (R2_SHADOW + 3 * R2_FOGB).
//
// Static combo 1 (R2_TC1) is the same code for the vertex-shader combos that add a TEXCOORD1.xy output (a detail
// texture is present): the interface is linked by register, so the unused TEXCOORD1 must still occupy its register.
//
// Material inputs (VMT): $basetexture t0, $bumpmap t1, $reflectionTint t2 (F0 rgb), $detailColor t4 = packed
// r = gloss, g = AO, b = cavity (sampled with the base UV). Two-layer materials add $baseTexture2 t11, $bumpmap2 t12,
// $reflectionTint2 t13 and $blendModulateTexture t15 (r, g = stock blend range); their packed gloss lives in the blend
// texture, b = layer 1 and a = layer 2 (AO and cavity are not carried for two layers).
#ifndef R2_SPEC_SRGB
#define R2_SPEC_SRGB 1
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
#ifndef R2_LIGHTS
#define R2_LIGHTS 0
#endif
#ifndef R2_SHADOW
#define R2_SHADOW 0
#endif
#ifndef R2_FOGB
#define R2_FOGB 0
#endif
#ifndef R2_NEUTRAL
#define R2_NEUTRAL 0
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
#if !R2_DN
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
Texture2D<float4> csmAtlas : register(t6);
Texture2D<float4> cloudMask : register(t8);
Texture2D<float4> lightmap0 : register(t9);
Texture2D<float4> lightmap1 : register(t10);
SamplerState baseSampler : register(s0);
SamplerState bumpSampler : register(s1);
SamplerState reflectionTintSampler : register(s2);
#if !R2_DN
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
#if R2_NEUTRAL
    return pow(nh, expo);
#else
    // GGX shape normalised to a peak of 1 (the stock lobe is a peak-1 pow), dimmed with roughness
    float a = rough * rough;
    float a2 = a * a;
    float d = nh * nh * (a2 - 1.0) + 1.0;
    return (a2 * a2) / (d * d) * (1.0 - 0.75 * rough);
#endif
}

void AddLight(uint index, float3 N, float3 V, float3 worldPos, float rough, float expo, float ndv, inout float3 diff, inout float3 spec)
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
#if !R2_NEUTRAL && R2_SPEC_SRGB
    // R2 stores F0 reflectance (a dielectric is about 0.03..0.04, the _spc mean of stone is 8/255) but the converted
    // texture carries the source sRGB flag, so the sampler returned the sRGB decode; encode it back to the stored value.
    // (Define R2_SPEC_SRGB 0 if r2tex stops flagging the _spc textures as sRGB.)
    rt.rgb = (rt.rgb <= 0.0031308) ? rt.rgb * 12.92 : 1.055 * pow(rt.rgb, 1.0 / 2.4) - 0.055;
#endif
    float3 f0 = (rt.x + cb0[3].w * (rt.xyz - rt.x)) * cb0[2].xyz;
    float specExp = cb0[2].w;
    float rtAlpha = rt.a;
    float fresnelAmount = cb0[10].w;
    float fresnelMax = cb0[11].x;
    float fresnelSharp = cb0[12].w;
    float gloss = 0.5, ao = 1.0, cav = 1.0;
#if R2_L2
    // layer blend: stock smoothstep over the range given by the blend texture's r / g and the vertex alpha (TEXCOORD3.w)
    const float4 bm = blendTexture.Sample(blendSampler, i.uv.xy);
    const float lo = max(bm.y - bm.x - 0.001, 0.0);
    const float hi = min(bm.x + bm.y + 0.001, 1.0);
    const float sb = saturate(((1.0 - i.nrm.w) - lo) / (hi - lo));
    const float blend = sb * sb * (3.0 - 2.0 * sb);
    base = lerp(base, baseTexture2.Sample(baseSampler2, i.uv.xy), blend);
    nts = lerp(nts, bumpTexture2.Sample(bumpSampler2, i.uv.xy).xyz * cb0[17].yzw + cb0[18].xyz, blend);
#if R2_DN
    {   // detail normals (one per layer) at the detail UV, combined as in the stock shader
        float3 dn = detailNormalTexture.Sample(detailSampler, i.uvDetail).xyz * cb0[11].yzw + cb0[12].xyz;
        const float3 dn2 = detailNormalTexture2.Sample(detailSampler2, i.uvDetail).xyz * cb0[19].yzw + cb0[20].xyz;
        dn = lerp(dn, dn2, blend);
        nts = nts * dn.z + nts.z * dn;
    }
#endif
    float4 rt2 = reflectionTintTexture2.Sample(reflectionTintSampler2, i.uv.xy);
#if !R2_NEUTRAL && R2_SPEC_SRGB
    rt2.rgb = (rt2.rgb <= 0.0031308) ? rt2.rgb * 12.92 : 1.055 * pow(rt2.rgb, 1.0 / 2.4) - 0.055;
#endif
#if !R2_NEUTRAL
    gloss = lerp(bm.z, bm.w, blend);
#endif
    f0 = lerp(f0, (rt2.x + cb0[16].w * (rt2.xyz - rt2.x)) * cb0[15].xyz, blend);
    specExp = lerp(cb0[2].w, cb0[15].w, blend);
    rtAlpha = lerp(rt.a, rt2.a, blend);
    fresnelAmount = lerp(cb0[10].w, cb0[18].w, blend);
    fresnelMax = lerp(cb0[11].x, cb0[19].x, blend);
    fresnelSharp = lerp(cb0[12].w, cb0[20].w, blend);
#else
#if !R2_NEUTRAL
    const float3 pkk = packedTexture.Sample(packedSampler, i.uv.xy).rgb;
    gloss = pkk.r; ao = pkk.g; cav = pkk.b;
#endif
#endif
    const float expo = abs(min(1.0 / pow(cb0[4].z - cb0[4].w * rtAlpha, 2.0), -specExp));
    const float3 pk = float3(gloss, ao, cav);
    const float rough = max(1.0 - gloss, 0.04);)HLSL"
    R"HLSL(
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

    float3 N = normalize(nts.x * i.tan + nts.y * i.bin + nts.z * i.nrm.xyz);
    float distSq = dot(i.pos, i.pos);
    float invDist = rsqrt(distSq);
    float3 V = i.pos * invDist;                 // camera -> pixel
    float3 worldPos = i.pos + cb2[0].xyz;
    float ndv = saturate(dot(N, -V));
    float fr = saturate(1.0 - 1.743447 * dot(N, -V));
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
        AddLight(idx + 32, N, V, worldPos, rough, expo, ndv, diffRtl, specRtl);
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
#if !R2_NEUTRAL
    // AO on the baked/sky/ambient part, cavity on specular only, plus a baked-light-direction highlight
    float lmLen = sqrt(lenSq);
    float3 ld = lmDir / max(lmLen, 1e-4);
    float3 hl = ld - V;
    float lmNh = saturate(dot(N, hl) * rsqrt(dot(hl, hl)));
    float lmNdl = saturate(dot(N, ld));
    specAcc += cb0[7].xyz * lmEnergy * saturate(lmLen) * lmNdl * SpecLobe(lmNh, lmNdl, ndv, rough, expo) * 0.5;
    diffAcc = cb0[7].xyz * (lmEnergy * ng2 + skyW * cb2[11].xyz) * ao + cb0[7].xyz * sunCol + diffRtl;
    diffAcc += cb0[8].xyz * ao;
#else
    diffAcc += cb0[8].xyz;
#endif

#if R2_LIGHTS
    uint packed = asuint(cb3[8].w);
    [loop] while (packed != 0) {
        AddLight(packed & 127, N, V, worldPos, rough, expo, ndv, diffAcc, specAcc);
        packed >>= 8;
    }
#endif

#if R2_NEUTRAL
    float fw = rtAlpha * fr * fresnelAmount;
    float3 fresnel = f0 + fw * (fresnelMax - f0);
    float3 color = albedo * diffAcc + fresnel * specAcc;
#else
    float fw = fr * fresnelAmount;
    float3 fresnel = f0 + fw * (fresnelMax - f0);
    float specOcc = lerp(1.0, ao, 0.5);
    float3 color = albedo * diffAcc + fresnel * specAcc * cav * specOcc;
#endif

#if R2_DEBUG == 7
    {   // 4 x 2 panels: lit diffuse, lit specular, specular before F, diffuse light, N, gloss/ao/cav, sky/ng2/shadow, rough/expo/lightScale
        const float ls = tonemapGlobals[0].c_lightScale;
        const uint panel = uint(sp.x / 480.0) + 4u * uint(sp.y / 540.0);
        float3 v[8] = { albedo * diffAcc * ls, fresnel * specAcc * cav * specOcc * ls, specAcc * ls, diffAcc * ls, N * 0.5 + 0.5,
                        float3(gloss, ao, cav), float3(skyW, ng2, shadow), float3(rough, expo / 100.0, ls * 10.0) };
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
