// Origin: C:\Users\r3muxd\tmp\r2port\modelconv. R2 static model converter bundled in r1delta under GPL-3.0.
// Copyright (c) 2022, rexx
// Copyright (c) 2023, rexx
// Licensed under GPL-3.0-only.
// Adapted from rmdlconv/src/studio/model/mdl_52.cpp, mdl_53.cpp, studio.h,
// and optimize.h. This is an independent static-model converter for R2 MDL
// v53 input and R1 MDL v52 output.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace r2port::modelconv {

struct ModelInput {
    std::span<const std::uint8_t> mdl;
    // Optional detached inputs. When MDL v53 contains embedded companion data,
    // the embedded data is used. These spans are used only if the embedded
    // section is absent.
    std::span<const std::uint8_t> vvd;
    std::span<const std::uint8_t> vtx;
    std::span<const std::uint8_t> vvc;
    std::span<const std::uint8_t> phy;
};

struct ModelOutput {
    std::vector<std::uint8_t> mdl;
    std::vector<std::uint8_t> vvd;
    std::vector<std::uint8_t> vtx;
    std::vector<std::uint8_t> vvc;
    std::vector<std::uint8_t> phy;
};

// Converts the bounded static subset supported by this library. On failure,
// error contains a specific reason and output is cleared.
bool ConvertModelR2ToR1(const ModelInput& input, ModelOutput& output, std::string& error);

// Lists the material names an R2 MDL v53 refers to: each texture name joined with every
// texture directory, forward slashes, no leading slash, case preserved, de-duplicated. The caller
// resolves them under materials/ with a .vmt extension.
bool ListModelMaterials(std::span<const std::uint8_t> r2Mdl, std::vector<std::string>& names, std::string& error);

// Checks structural bounds and the matching MDL/VVD/VTX/VVC checksums expected
// by the R1 model loader. This does not call or emulate the R1 engine.
bool VerifyModelR1(const ModelOutput& model, std::string& error);

} // namespace r2port::modelconv
