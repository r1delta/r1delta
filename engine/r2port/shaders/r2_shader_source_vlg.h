#pragma once

// VertexLitGeneric stock replica and opt-in environment terms.
// Generated from shaderlab/uber/hlsl by tools/hlsl2hdr.py; the HLSL file is the source of truth.

namespace r2port::shaders {

inline constexpr const char kUberVlgSource[] =
    R"HLSL(
// R2_PS_DYNAMIC 24 1 VLG
// R2 port, R1 VertexLitGeneric pixel-shader replacement ("uber", props and models). One source, feature macros chosen per
// material by the draw-state hook. Everything marked "stock" was reconstructed from the behaviour of the stock ps50
// records of vertexlit_and_unlit_generic_ps50.vcs (resource bindings and constants read through reflection) and is
// proven against those records by harness/uberdiff via tools/sweep_vlg.py (all 24 dynamic variants, relative difference
// below 2e-4). R2 terms exist only behind the R2_T_* macros and are absent in neutral mode.
//
// Dynamic index (measured by diffing the 24 stock records of static 4180; the draw function sub_1800304C0 builds it the
// same way) = R2_SHADOW + 3 * (R2_UNSH + 2 * (R2_INST + 2 * R2_FOGB)):
//   R2_SHADOW 0/1/2  sun shadow: baked visibility only / one cascade (t6) / blended cascades (t6)
//   R2_UNSH   1      "unshadowed lighting scale" (cb0[4].x): scales the ambient, the weighted lights and the env term
//                    (and the baked sun visibility in shadow mode 0)
//   R2_INST   1      per-instance data (ambient SH, sky direction / sun visibility, packed lights, diffuse modulation) comes
//                    from the structured buffer t17 indexed by the instance id (SV_InstanceID passed through TEXCOORD5)
//                    instead of the constant buffer b3
//   R2_FOGB   1      fog flavour: height fog with per channel transmittance
//
// Static 4180 (the dominant prop combo): $basetexture t0, $bumpmap t1, $reflectionTint t2 (rgb F0, a gloss: the stock
// shader reads a for its mip / lobe exponent / fresnel weight), $envmap t5. Lighting = per-instance ambient SH, sky
// weight, sun (with cloud mask t8 and CSM t6), up to four weighted lights and a packed list of hardware lights (t18).
// Also reproduced: 4212 (+ selfillum mask), 135252 (+ alpha test), 135184 (alpha test, no reflection tint or envmap).
#ifndef R2_SPEC_SRGB
#define R2_SPEC_SRGB 0   // 0: F0 = the sampled (sRGB-decoded) _spc like R2; 1 re-encodes to the stored byte
#endif
#ifndef R2_SPEC
#define R2_SPEC 1        // $reflectionTint (0 for the non-specular foliage static 135184)
#endif
#ifndef R2_SELFILLUM
#define R2_SELFILLUM 0   // $selfIllumMask t10 (static 4212)
#endif
#ifndef R2_ENV
#define R2_ENV 1         // $envmap bound (static 4180 has it; 0 reproduces the same record without the env term)
#endif
#ifndef R2_ATEST
#define R2_ATEST 0       // $alphatest (flags & 0x10): discard raw base alpha against cb0[3].x, output vertex * instance alpha
#endif
#ifndef R2_ARR
#define R2_ARR 0         // stock shadow-array statics (+0x4000 on the stock id, render flag 0x2000000): the four weighted lights read a variance shadow map array at t7
#endif
#ifndef R2_DITHER
#define R2_DITHER 0      // stock static bit 0x8000: the output alpha is dithered with a 16x16 noise texture at t19 (cb2[24].zw)
#endif
// R2 terms (all off in neutral mode, which reproduces the stock shader):
//   R2_T_LOD   cube mip from roughness (rough * 6 + R2_CUBE_LOD_BIAS) instead of the stock shininess-derived mip
//   R2_T_FENV  analytic environment BRDF lerp(F0, max(F0, (1-rough)^2), (1-NdotV)^5) instead of the stock fresnel mix
//   R2_T_SO    ambient specular occlusion: env *= lerp(1, saturate(ambient light), R2_SO_SCALE) (ambient SH + sky, no sun / lights)
#ifndef R2_NEUTRAL
#define R2_NEUTRAL 0
#endif
#if R2_NEUTRAL
#undef R2_T_LOD
#undef R2_T_FENV
#undef R2_T_SO
#endif
#ifndef R2_CUBE_LOD_BIAS
#define R2_CUBE_LOD_BIAS -2.0   // our probes are 64x64 (R2 256x256): R2 mip m is our mip m - 2
#endif
#ifndef R2_ENV_GAIN
#define R2_ENV_GAIN 0.25        // R2_T_FENV: gain on the R2 environment BRDF (the LMG file's empirical pick; models carry $envmaptint 0.25 from the converter, which this term does not use)
#endif
#ifndef R2_GLOSS_POW
#define R2_GLOSS_POW 2          // t2.a carries gloss^R2_GLOSS_POW (2: the packer stores gloss^2 because the stock fresnel weight is t2.a * f(NV); 1 = linear gloss)
#endif
#ifndef R2_SO_SCALE
#define R2_SO_SCALE 1.0         // R2_T_SO strength
#endif
// R2_ROUGH_FORCE <value>: test aid, defining it replaces the roughness of every pixel
#ifndef R2_SHADOW
#define R2_SHADOW 0
#endif
#ifndef R2_UNSH
#define R2_UNSH 0
#endif
#ifndef R2_INST
#define R2_INST 0
#endif
#ifndef R2_FOGB
#define R2_FOGB 0
#endif
#ifndef R2_MARK
#define R2_MARK 0
#endif

cbuffer CBufVertexLitAndUnlitGenericStatic : register(b0) { float4 cb0[14]; };
cbuffer CBufCommonPerCamera : register(b2) { float4 cb2[28]; };

struct ModelInst { float4 xf[3]; float4 diffMod; float4 sh[3]; float4 skyDirSunVis; uint4 packedLights; };
#if R2_INST
StructuredBuffer<ModelInst> g_modelInst : register(t17);
#else
cbuffer CBufModelInstance : register(b3) { ModelInst c_modelInst; };
#endif

struct Light { float3 color; int shadowIndex; float4 pos; float4 spot; float4 att; float4 axisX; float4 axisY; };
struct TonemapElement { float c_lightScale; uint c_historyPos; float c_hist[10]; };
StructuredBuffer<TonemapElement> tonemapGlobals : register(t16);
StructuredBuffer<Light> g_lights : register(t18);

Texture2D<float4> baseTexture : register(t0);
Texture2D<float4> bumpTexture : register(t1);
Texture2D<float4> reflectionTintTexture : register(t2);
#if R2_SELFILLUM
Texture2D<float4> selfIllumTexture : register(t10);
SamplerState selfIllumSampler : register(s10);
#endif
#if R2_ENV
TextureCube<float4> envTexture : register(t5);
SamplerState envSampler : register(s5);
#endif
#if R2_ARR
Texture2DArray<float4> lightShadowMaps : register(t7);
SamplerState lightShadowSampler : register(s7);
#endif
#if R2_DITHER
Texture2D<float4> ditherNoise : register(t19);
#endif
Texture2D<float4> csmAtlas : register(t6);
Texture2D<float4> cloudMask : register(t8);
SamplerState baseSampler : register(s0);
SamplerState bumpSampler : register(s1);
SamplerState reflectionTintSampler : register(s2);
SamplerComparisonState csmSampler : register(s6);
SamplerState cloudSampler : register(s8);

struct PSIn
{
    centroid float2 uv : TEXCOORD0;
    float4 vtx : TEXCOORD1;              // x = vertex ambient weight, w = vertex alpha (y z unused by the stock shader)
    centroid float4 pos : TEXCOORD2;     // xyz camera-relative position, w = binormal.x
    centroid float4 nrm : TEXCOORD3;     // xyz normal, w = binormal.y
    centroid float4 tan : TEXCOORD4;     // xyz tangent, w = binormal.z
#if R2_INST
    nointerpolation uint inst : TEXCOORD5;   // the stock record names it SV_InstanceID (written by the vertex shader)
#endif
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
#if R2_SHADOW == 0
    return baked;
#else
    float3 p = cam.x * cb2[15].xyz + cb2[14].xyz;
    p = cam.y * cb2[16].xyz + p;
    p = cam.z * cb2[17].xyz + p;
#if R2_SHADOW == 1
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
#endif
}

// one hardware light (t18): lw = weight of the light (1 for the packed list, byte / 255 for the four weighted lights)
void AddLight(uint index, float lw, bool shadowed, float3 N, float3 V, float3 worldPos, float expo, inout float3 diff, inout float3 spec)
{
    Light L = g_lights[index];
    float w2 = L.pos.w * L.pos.w;
    float3 toL = L.pos.xyz - worldPos * L.pos.w;
    float d2 = min(max(w2, dot(toL, toL)), 1.0);
    float invd = rsqrt(d2);
    float den = 1.0 + d2 * (invd * L.att.y + L.att.z);
    float c = saturate(dot(L.spot.xyz, toL) * invd + L.spot.w);
    c = c + L.att.x * (c * c - c);
    float wgt = lw * c * den;
    if (wgt != 0.0) {
#if R2_ARR
        if (shadowed && L.shadowIndex < 4096) {   // one-sided Chebyshev on the variance shadow map of this light (weighted lights only)
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
#if R2_SPEC
        float3 h = toL * invd - V;
        float nh = saturate(dot(N, h) * rsqrt(dot(h, h)));
        spec += wgt * ndl * pow(nh, expo) * L.color;
#endif
    }
}

float4 PSMain(PSIn i
#if R2_DITHER
    , float4 sp : SV_Position
#endif
    ) : SV_Target
{
#if R2_INST
    const ModelInst mi = g_modelInst[i.inst];
#else
    const ModelInst mi = c_modelInst;
#endif
    const float4 base = baseTexture.Sample(baseSampler, i.uv);
    const float3 nts = mad(bumpTexture.Sample(bumpSampler, i.uv).xyz, cb0[4].yzw, cb0[5].xyz);
#if R2_SPEC
    const float4 rt = reflectionTintTexture.Sample(reflectionTintSampler, i.uv);
    const float3 rtc = rt.x + cb0[9].w * (rt.xyz - rt.x);                 // stock: tint colour (lerp grey / colour)
    const float3 f0 = rtc * cb0[8].xyz;
    const float3 envTint = rtc * cb0[9].xyz;
    const float rtAlpha = rt.a;
    const float expo = abs(min(1.0 / pow(cb0[10].z - cb0[10].w * rtAlpha, 2.0), -cb0[8].w));   // stock phong exponent
#else
    const float3 rtc = 0.0, f0 = 0.0, envTint = 0.0;
    const float rtAlpha = 0.0, expo = 1.0;
#endif
#if R2_UNSH
    const float us = cb0[4].x;
#else
    const float us = 1.0;
#endif
#if R2_T_LOD || R2_T_FENV
    float3 f0R2 = rtc;
#if R2_SPEC_SRGB
    f0R2 = (f0R2 <= 0.0031308) ? f0R2 * 12.92 : 1.055 * pow(max(f0R2, 1e-6), 1.0 / 2.4) - 0.055;
#endif
    f0R2 *= cb0[8].xyz;
#ifdef R2_ROUGH_FORCE
    const float rough = max(R2_ROUGH_FORCE, 0.04);
#elif R2_GLOSS_POW == 2
    const float rough = max(1.0 - sqrt(rtAlpha), 0.04);
#else
    const float rough = max(1.0 - rtAlpha, 0.04);
#endif
#endif

    float3 albedo = base.rgb * mi.diffMod.rgb;
    float alpha = base.a * i.vtx.w * mi.diffMod.w;
#if R2_SELFILLUM
    alpha = (1.0 + cb0[12].x * (base.a - 1.0)) * i.vtx.w * mi.diffMod.w;
#endif
#if R2_ATEST
    clip(base.a - cb0[3].x);
    alpha = i.vtx.w * mi.diffMod.w;
#endif

    const float3 bin = float3(i.pos.w, i.nrm.w, i.tan.w);
    // Keep the stock accumulation order. A one-ULP normal change can cross a cubemap texel boundary.
    precise float3 normalTS = nts.x * i.tan.xyz;
    normalTS = mad(nts.z, i.nrm.xyz, normalTS);
    normalTS = mad(nts.y, bin, normalTS);
    const float3 N = normalize(normalTS);
)HLSL"
    R"HLSL(    const float distSq = dot(i.pos.xyz, i.pos.xyz);
    const float invDist = rsqrt(distSq);
    const float3 V = i.pos.xyz * invDist;                 // camera -> pixel
    const float3 worldPos = i.pos.xyz + cb2[0].xyz;
    const float ndvRaw = dot(N, -V);
    float fr = saturate(1.0 - 1.743447 * ndvRaw);
    fr *= fr;
    fr = fr * ((1.0 - cb0[11].x) + cb0[11].x * fr);
    const float fw = rtAlpha * fr * cb0[6].x;

    // ambient: per-instance SH (three rows of dot(N, xyz) + w), weighted by the vertex term and the bumped normal against the geometric one
    const float3 sh = max(float3(dot(N, mi.sh[0].xyz) + mi.sh[0].w, dot(N, mi.sh[1].xyz) + mi.sh[1].w, dot(N, mi.sh[2].xyz) + mi.sh[2].w), 0.0);
    const float ng2 = dot(N, i.nrm.xyz) * dot(N, i.nrm.xyz) * i.vtx.x;
    const float3 skyDir = mi.skyDirSunVis.xyz;
    const float lenSq = dot(skyDir, skyDir);
    const float ndDir = dot(N, skyDir);
    const float skyW = saturate((max(lenSq, ndDir) + ndDir) * 0.5 * sqrt(lenSq)) * (1.0 + lenSq * (ng2 - 1.0));

    // sun: baked visibility (scaled in shadow mode 0 when R2_UNSH) or the cascades, times the cloud mask
#if R2_SHADOW == 0
    const float sunBase = mi.skyDirSunVis.w * us;
#else
    const float sunBase = mi.skyDirSunVis.w;
#endif
    const float sunShadow = Shadow(i.pos.xyz, sunBase);
    float2 cloudUv = i.pos.x * cb2[26].zw + cb2[26].xy;
    cloudUv = i.pos.y * cb2[27].xy + cloudUv;
    cloudUv = i.pos.z * cb2[27].zw + cloudUv;
    const float3 sunVis = sunShadow * cloudMask.Sample(cloudSampler, cloudUv).xyz;
    const float3 sunCol = sunVis * saturate(dot(N, cb2[13].xyz)) * cb2[12].yzw;
    const float3 hs = cb2[13].xyz - V;
    const float sunNh = saturate(dot(N, hs) * rsqrt(dot(hs, hs)));

    const float3 ambient = sh * ng2 + skyW * cb2[11].xyz;
    float3 diffLoop = ambient * us + sunCol;      // the stock shader scales the ambient before adding the sun
    float3 specLoop = sunCol * pow(sunNh, expo);
    float3 diffW = 0.0, specW = 0.0;

    // packed list of hardware lights, four 7-bit indices
    uint packedIds = mi.packedLights.w;
    [loop] while (packedIds != 0) {
        AddLight(packedIds & 127, 1.0, false, N, V, worldPos, expo, diffLoop, specLoop);
        packedIds >>= 8;
    }
    // four weighted lights: byte k of x is the weight, y / z carry the 16 bit indices; light k exists while the bytes from k up are not all zero
    if (mi.packedLights.x != 0) {
        [unroll] for (uint k = 0; k < 4; ++k) {
            if ((mi.packedLights.x >> (8 * k)) != 0) {
                const uint ids = (k < 2) ? mi.packedLights.y : mi.packedLights.z;
                const uint idx = (k & 1) ? (ids >> 16) : (ids & 0xffff);
                AddLight(idx, float((mi.packedLights.x >> (8 * k)) & 255) * (1.0 / 255.0), true, N, V, worldPos, expo, diffW, specW);
            }
        }
    }
    const float3 diffAcc = diffW * us + diffLoop;
    const float3 specAcc = specW * us + specLoop;

#if R2_SELFILLUM
    const float3 mask = selfIllumTexture.Sample(selfIllumSampler, i.uv).rgb;
    const float3 emissive = (base.a + cb0[12].x * (mask - base.a)) * cb0[12].yzw * us;
    float3 color = albedo * diffAcc + emissive;
#else
    float3 color = albedo * diffAcc;
#endif
#if R2_SPEC
    const float3 fresnel = f0 + fw * (cb0[7].w - f0);
    color += fresnel * specAcc;
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
        lod = cb0[10].y * log2(max(expo * 0.25 - 2.0, 0.25)) + cb0[10].x;
#endif
        const float4 cube = envTexture.SampleLevel(envSampler, refl, lod);
#if R2_T_FENV
        {   // R2 analytic environment BRDF (no lambertian shaping of the stock term)
            const float h = (1.0 - rough) * (1.0 - rough);
            const float b = 1.0 - abs(ndvRaw);
            const float b2 = b * b;
            envFactor = lerp(f0R2, max(f0R2, h.xxx), b2 * b2 * b) * R2_ENV_GAIN;
            envW = cb2[12].x * us;
        }
#else
        {
            const float envLambert = saturate(cb0[11].y - 0.5 * rtAlpha);
            envW = saturate(envLambert * (1.0 - ndvRaw) + ndvRaw) * (cb2[12].x * us);
            envFactor = envTint + fw * (cb0[7].w - envTint);
        }
#endif
        float3 env = (envW * (cube.a * 3.984375 + 0.015625)) * cube.rgb;
#if R2_T_SO
        env = env * lerp(1.0, saturate(ambient * us), R2_SO_SCALE);
#endif
        color += env * envFactor;
    }
#endif

    // fog
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
    float b2f = cb2[6].x * a1 + a3 * cb2[6].y;
    float amount = (a2 * q) * 0.5 + b1;
    amount = amount * a2 + b2f;
    amount = 1.0 - exp2(-amount);   // the stock bytecode uses the base-2 exp instruction
    float sunFog = saturate((-dot(cb2[9].xyz, -V) - cb2[8].w) * cb2[9].w);
    sunFog *= sunFog;
    float3 fogColor = cb2[8].xyz * sunFog + cb2[7].xyz;
    amount *= cb2[6].w;
    color = color + amount * (fogColor * cb0[13].w - color);
#else
    float hz = cb2[10].w - max(cb2[10].w, 0.0);
    float vz = min(i.pos.z, 0.0);
    float s = 1.0 - hz / vz;
    float dist = sqrt(distSq);
    float sd = s * dist;
    float len = -i.pos.z * s + sd;
    float3 trans = exp2(len * cb2[6].xyz);
    float scat = (sd * cb0[13].w) / max(len, 0.0);
    float3 inscatter = scat * cb2[7].xyz * max(1.0 - trans, 0.0);
    color = (color * trans + inscatter) * cb2[10].xyz;
#endif
#if R2_MARK
    color *= float3(1.0, 0.2, 0.2);   // test aid: tints every replaced pixel
#endif
#if R2_DITHER
    {   // stock: alpha = saturate(round(alpha * cb2[24].z + (noise * 0.996094 - 0.498047)) * cb2[24].w), noise = t19 red at (x & 15, y & 15)
        const int2 dp = int2(sp.xy) & 15;
        const float dn = ditherNoise.Load(int3(dp, 0)).x;
        alpha = saturate(round(mad(alpha, cb2[24].z, mad(dn, 0.99609375, -0.498046875))) * cb2[24].w);   // 255/256, -127.5/256: the printed l(0.996094) / l(-0.498047) are the rounded values
    }
#endif
    return float4(color * tonemapGlobals[0].c_lightScale, alpha);
}
)HLSL";

} // namespace r2port::shaders
