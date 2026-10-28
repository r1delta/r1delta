// r2_xegtao.h : screen-space ambient occlusion for the R1 engine hook, a D3D11 compute-shader port of Intel's XeGTAO.
//
// XeGTAO: Copyright (C) 2016-2021, Intel Corporation. SPDX-License-Identifier: MIT. https://github.com/GameTechDev/XeGTAO
// The embedded HLSL (r2_xegtao_hlsl.inl) is derived from XeGTAO.h / XeGTAO.hlsli / vaGTAO.hlsl; the constant-buffer layout and
// the GTAOUpdateConstants() math in r2_xegtao.cpp are taken from XeGTAO.h. The half-resolution passes (CSDownsample, CSUpsample) and
// everything else in this module are new code of the r2port project.
//
// MIT licence text (applies to the XeGTAO-derived parts):
//   Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation
//   files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy,
//   modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software
//   is furnished to do so, subject to the following conditions: The above copyright notice and this permission notice shall be
//   included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY
//   KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
//   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
//   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
//   OTHER DEALINGS IN THE SOFTWARE.
//
// Dependencies: d3d11, d3dcompiler (D3DCompile at Init), WRL ComPtr. Shader Model 5 compute only (cs_5_0). No global D3D state is saved
// or restored here: the caller owns that. Render() leaves CS shader, CS constant buffers/sampler, CS SRV slots 0..2 and CS UAV slots 0..4 unbound.
//
// Typical use (replaces the GFSDK HBAO+ RenderAO call):
//   r2port::ao::xegtao::Init(device);                                   // once; compiles ~10 compute shaders (~1 s), false on failure
//   r2port::ao::xegtao::Params p; p.quality = 2; p.denoisePasses = 2; p.sceneWidth = depthW; p.sceneHeight = depthH;
//   r2port::ao::xegtao::Render(ctx, depthSrv, proj, 39.0f, viewport, aoUav, p);   // aoUav: UAV of an R16_UNORM texture, viewport.Width x viewport.Height
//   // then multiply aoSrv (same texture) into the scene colour in a pixel shader: colour.rgb *= ao
#pragma once
#include <d3d11.h>

namespace r2port::ao::xegtao {

struct Params {
    int   quality       = 2;      // 0 = Low (1 slice x 2 steps), 1 = Medium (2 x 2), 2 = High (3 x 3). (XeGTAO Ultra 9x3 is not offered: 2.5x the cost of High.)
    bool  halfRes       = false;  // true: depth downsample -> AO at half resolution in each axis -> depth-aware bilateral upsample
    int   denoisePasses = 2;      // 1..3 (XeGTAO "Sharp"/"Medium"/"Soft"); 2 is the recommended default for a renderer without TAA
    float radiusMeters  = 1.0f;   // world radius in metres, multiplied by metersToViewUnits (R1: 39.0..39.37 units per metre)
    float intensity     = 1.0f;   // 1.0 = mean AO matched to HBAO+ at R1's default settings (power 4, radius 1 m); AO' = AO^intensity, larger = darker
    int   sceneWidth    = 0;      // size in texels of the depth texture behind depthSrv (0 = query it from the SRV)
    int   sceneHeight   = 0;
    int   frameIndex    = 0;      // noise index: frame % 64 when the result is temporally accumulated (TAA), otherwise leave 0
    bool  fp32DepthMips = false;  // true: R32_FLOAT depth MIP chain (5 % faster measured on an RTX 5080, double the memory); false: R16_FLOAT
    float finalValuePower = 0.0f; // > 0 overrides the power that intensity would produce (XeGTAO default is 2.2); for tests
};

// Creates the sampler, constant buffers and compiles the default shader set. Safe to call again with the same device. Returns false and
// sets LastError() when a required typed-UAV format is unsupported (R16_FLOAT, R32_FLOAT, R8_UNORM, R32_UINT) or a shader fails.
bool Init(ID3D11Device* device);
void Shutdown();               // releases everything (also on device loss before re-Init)
const char* LastError();       // text of the last failure, "" if none

// depthSrv         : R1's hardware depth (same SRV as the HBAO+ input): 0..1 non-reversed, R32_FLOAT or R24_UNORM_X8 view. Must not be bound as DSV.
// projection       : 16 floats exactly as R1 passes them to HBAO+ (D3D row-vector layout: m[0]=1/tan(hfov/2), m[5]=m[0]*aspect,
//                    m[10]=zf/(zf-zn), m[11]=1, m[14]=-zn*zf/(zf-zn)).
// metersToViewUnits: R1 passes 39.0 to HBAO+.
// viewport         : region of the depth texture to process (TopLeftX/Y may be non-zero). The AO is written to outAoUav at (0,0)-based
//                    coordinates 0..Width-1 x 0..Height-1. Width and Height must be >= 64.
// outAoUav         : UAV (R16_UNORM) of a texture at least Width x Height. Result: visibility, 1 = unoccluded, i.e. the same polarity as the
//                    HBAO+ output that is multiplied into the scene colour.
bool Render(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* depthSrv, const float projection[16], float metersToViewUnits,
            const D3D11_VIEWPORT& viewport, ID3D11UnorderedAccessView* outAoUav, const Params& params);

} // namespace r2port::ao::xegtao
