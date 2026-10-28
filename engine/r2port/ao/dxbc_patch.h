#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dxbc {

struct PatchOptions {
    std::uint32_t aoSlot = 31;
    float aoWeight = 0.5f;
    bool occlusionInput = false;   // t31 holds occlusion (1 - AO): so = 1 - aoWeight * t31
    float constantFactor = -1.0f;  // >= 0: test aid, every patched env term is multiplied by this constant instead of reading t31
};

struct PatchReport {
    std::uint32_t cubeSamples = 0;
    std::uint32_t maxDeclaredTextureSlot = 0;
    std::uint32_t positionRegister = 0;
    std::uint32_t oldTempCount = 0;
    std::uint32_t coordinateTemp = 0;
    std::uint32_t aoTemp = 0;
    std::uint32_t addedInstructionCount = 0;
    bool addedPositionSignature = false;
    bool addedPositionDeclaration = false;
    std::string error;
};

bool PatchDxbc(const std::vector<std::uint8_t>& input,
               std::vector<std::uint8_t>& output,
               PatchReport& report,
               const PatchOptions& options = PatchOptions{});

// r2port_lm_bicubic: stock LightmappedGeneric pixel shaders sample lightmapTexture_0 (t9/s9) and lightmapTexture_1
// (t10/s10) with one implicit-LOD `sample` each at the same coordinate (a centroid input). PatchLightmapBicubic replaces
// each of the two samples with the SDK 2013 / Godot bicubic B-spline filter (four weighted bilinear taps, explicit LOD 0
// since both textures have a single mip) and keeps sampler and resource bindings. The texture size comes from `resinfo`
// on t9. No resource, signature or reflection changes are needed.
struct LightmapBicubicReport {
    std::uint32_t texturesPatched = 0;      // 1 or 2 (t9 and t10)
    std::uint32_t oldTempCount = 0;
    std::uint32_t addedTemps = 0;
    std::int32_t addedInstructionCount = 0; // net, after removing the original samples
    std::string error;
};

bool PatchLightmapBicubic(const std::vector<std::uint8_t>& input,
                          std::vector<std::uint8_t>& output,
                          LightmapBicubicReport& report);

bool RecomputeChecksum(std::vector<std::uint8_t>& dxbc, std::string& error);
bool VerifyChecksum(const std::vector<std::uint8_t>& dxbc);

} // namespace dxbc
