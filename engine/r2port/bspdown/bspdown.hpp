// Origin: C:\Users\r3muxd\tmp\r2port\bspdown. Bundled in r1delta under GPL-3.0.
// bspdown: rewrite a Titanfall 2 (rBSP v37) multiplayer BSP into a form the
// Titanfall 1 engine (rBSP v29) can load.
//
// Rule list and the reasoning behind each rule: see README.txt in this directory.
//
// Format knowledge comes from the open projects bsp_tool (GPL-3.0), bsp_regen
// (GPL-3.0, the R1 -> R2 direction of the tricoll/prop conversion is inverted
// here) and from reading the R1 engine's map loaders.
//
// This file is part of r1delta and is GPL-3.0 like the rest of the project.
#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(__MINGW32__)
#define BSPDOWN_PRINTF gnu_printf
#else
#define BSPDOWN_PRINTF printf
#endif

namespace bspdown {

// Thrown for any input the converter cannot turn into a valid R1 BSP. The
// message names the lump, record and value that failed.
struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// How the cubemap index of material sorts is written (README.txt rule 10).
// R2 stores the cubemap per mesh and every sort says 0; R1 stores it per sort
// and, for every sort whose cubemap is not -1, looks up a patch material
// "maps/<map>/<material>_<x>_<y>_<z>" and a cubemap texture of that origin.
enum class CubemapMode {
    None,   // every sort gets -1: plain material names, no patch materials needed
    Split,  // sorts are split so each mesh's R2 cubemap lands on its sort (needs patch materials)
    Keep,   // sorts keep the R2 value (0)
};

// Lump 0x1D handling (README.txt rule 12).
enum class PhysicsMode {
    Stub,  // replace a lump without world solids by a 1-solid stub
    None,  // keep the input lump
};

struct Options {
    // Render meshes, collision bounds and model bounds are clipped to
    // [-clipLimit, +clipLimit] on every axis. R1 rejects worldspawn bounds
    // beyond 16384 and clamps rays at 16379.
    float clipLimit = 16376.0f;

    // Real-time-light lump (0x69): bytes per texel in the output, 1..4.
    // R2 stores 9 bytes per texel in a layout that is not understood, so the
    // output is filled with 0xFF (no lights) at this width.
    int rtlBytesPerTexel = 4;

    // Map R2 unique-contents values to the R1 meaning (R2 has no plain SOLID
    // bit on world collision; see README.txt rule 9).
    bool remapContents = true;

    CubemapMode cubemaps = CubemapMode::None;

    PhysicsMode physics = PhysicsMode::Stub;

    // Rule 13: write per-cubemap textures (c<x>_<y>_<z>[.hdr].vtf, cubemapdefault) into the pakfile.
    bool cubemapFiles = true;

    // Rule 13: multiplier applied to the R2 BC6H cubemap values before the R1 encoding.
    float cubemapScale = 1.0f / 6.0f;

    // World light flags: stock R1 lights carry bit 0x2 (R2 never sets it); set it on lights with non-zero flags.
    bool worldLightFlagBit2 = false;

    // Drop lumps R1 never reads (0x04..0x07, 0x37, 0x7A). They are large
    // (about 2 MB on eden) and cannot be interpreted by R1.
    bool dropR2OnlyLumps = true;

    // R1 aborts map load above this many world lights.
    uint32_t maxWorldLights = 4064;
};

// Per-rule human readable report. One entry per line.
struct Report {
    std::vector<std::string> lines;
    void add(const char* fmt, ...)
#if defined(__GNUC__)
        __attribute__((format(BSPDOWN_PRINTF, 2, 3)))
#endif
        ;
    std::string text() const;
};

// Convert an R2 rBSP v37 image in memory to an R1 rBSP v29 image.
// Throws bspdown::Error when the input is not convertible.
std::vector<uint8_t> DowngradeBsp(const uint8_t* data, size_t size);
std::vector<uint8_t> DowngradeBsp(const uint8_t* data, size_t size, const Options& options, Report* report);

}  // namespace bspdown
