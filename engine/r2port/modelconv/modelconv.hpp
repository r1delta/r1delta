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
    // Human-readable account of the animation outcome ("animated: ..." or "bind pose only: reason");
    // empty for plain static models. Informational only.
    std::string note;
};

// Converts the bounded static subset supported by this library. On failure,
// error contains a specific reason and output is cleared.
bool ConvertModelR2ToR1(const ModelInput& input, ModelOutput& output, std::string& error);

// Lists the materials a converted model refers to, as R1 resolves them: "r2m/" + each texture name joined
// with every texture directory, forward slashes, case preserved, de-duplicated (the converter renames all
// model materials into the r2m/ namespace). The caller resolves them under materials/ with a .vmt
// extension; the R2 material is the name without the r2m/ prefix.
bool ListModelMaterials(std::span<const std::uint8_t> r2Mdl, std::vector<std::string>& names, std::string& error);

// Checks structural bounds and the matching MDL/VVD/VTX/VVC checksums expected
// by the R1 model loader. This does not call or emulate the R1 engine.
bool VerifyModelR1(const ModelOutput& model, std::string& error);

} // namespace r2port::modelconv
