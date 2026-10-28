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

bool RecomputeChecksum(std::vector<std::uint8_t>& dxbc, std::string& error);
bool VerifyChecksum(const std::vector<std::uint8_t>& dxbc);

} // namespace dxbc
