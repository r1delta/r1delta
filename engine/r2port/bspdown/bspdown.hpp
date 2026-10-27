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
    // 4.0 is the largest factor that keeps R2 values up to 1.0 inside the R1 encodable range (alpha 255 -> 4.0);
    // it was chosen by in-game comparison on mp_wargames (README "Rosetta" section), the old 1/6 left the map 30% darker.
    float cubemapScale = 4.0f;

    // World light flags: stock R1 lights carry bit 0x2 (R2 never sets it); set it on lights with non-zero flags.
    bool worldLightFlagBit2 = false;

    // Rule 20: world lights, light probe light indices and real-time lights (lumps 0x36, 0x65, 0x69), see README.txt.
    // collapseEnvs: R2 maps carry one sky-ambient/sun pair per light_environment (a dummy skybox environment first, the real one last).
    //   R1 supports exactly one pair at the start of the array ([ambient, sun, locals...]); the dummy pairs are dropped, the real (last)
    //   pair is moved to indices 0/1 and every light index stored in other lumps is remapped.
    // rtlConvert: decode the R2 real-time light lump (9 bytes per texel + 0x7A palettes) and write the R1 layout (4 slots of
    //   world light index - 1). false: all 0xFF (no real-time lights), the behaviour before rule 20.
    // rtlMinWeight: R2 stores an sRGB encoded visibility (0..255; quarter steps 0, 137, 187, 224, 255 = 0, .25, .5, .75, 1) per slot, R1 has none; slots below this byte are not listed (187 = at least half visible).
    // pbrLights: R2 lights with flag 0x20 (_pbr_falloff) and without 0x4 carry a luminous intensity (about 392x the usual unit) and
    //   R1 evaluates lights without 0x4 as a reciprocal polynomial; true rewrites them to the R1 fade-polynomial form.
    bool collapseEnvs = true;
    bool rtlConvert = true;
    int rtlMinWeight = 187;
    bool pbrLights = true;

    // Rule 21: lightmap plane B alpha (lump 0x62). The R1 world pixel shader (lightmappedgeneric_ps50) uses (1 - plane B alpha) as the sun
    // shadow term wherever the CSM cascades do not cover the pixel (and everywhere when CSM is off). R1 compilers bake the sun visibility
    // there (stock mp_rise: 89 % of texels 255 = shadowed, 6 % 0 = sunlit, graded in between); R2 writes 255 everywhere and keeps the
    // visibility in the real-time-light B weight of the sun slot. true: write 255 - 255*linear(sun visibility) from the R2 RTL sun slot.
    bool lightmapSunAlpha = true;

    // Drop lumps R1 never reads (0x04..0x07, 0x37, 0x7A). They are large
    // (about 2 MB on eden) and cannot be interpreted by R1.
    bool dropR2OnlyLumps = true;

    // R1 aborts map load above this many world lights.
    uint32_t maxWorldLights = 4064;

    // Rule 18: pad the collision grid (lumps 0x55/0x56) with empty cells so it covers +-32768 plus 3 cells around the origin
    // (R1's grid walker reads the cell table at stale out-of-grid indices when a trace reaches the grid border).
    bool gridPad = true;

    // Rule 19: cell size the collision grid is re-binned to (R2 uses 704, R1 stock maps 256). 0 keeps the R2 size.
    // The walker (sub_18010BE50) reads unwritten stack slots for hull half extents between 0.354 * size and 0.7071 * size,
    // so the window must not contain the hulls in use (player 16, AIN 38/60/65/325): 2048 gives 724..1448.
    float gridCellSize = 2048.0f;

    // Rule 16: static prop flag bit 0x10. R2 sets it on 14,138 of the 16,183 mp_glitch props (Source NO_SHADOW meaning). The R1 loader
    // (sub_18018B710) stores (flags & 0x10) == 0 at prop+58 and the prop drawers (sub_18018D7A0 / sub_18018F1D0) draw a prop in the main
    // pass only when that is 1, in the shadow pass always: in R1 the bit means "shadow caster only, not drawn". Default: clear the bit.
    // keepPropFlag10 = true copies it unchanged (the behaviour before rule 16).
    bool keepPropFlag10 = false;

    // Diagnostic variants (default keeps all props):
    //   HideScatter:  props of models with at least scatterMinCount instances get scale 0.0001
    //                 (prop count and indices stay valid: object reference lumps index props)
    //   HideAll:      every prop gets scale 0.0001
    enum class PropMode { Keep, HideScatter, HideAll };
    PropMode props = PropMode::Keep;
    uint32_t scatterMinCount = 500;

    // Rule 17: shadow data (lumps 0x05, 0x63, 0x64, 0x7C..0x7F). R2 stores one or more shadow environments
    // (lump 0x05) with their CSM aabb nodes / object refs / shadow meshes concatenated; R1 has no such table and
    // starts the CSM tree at node 0 and the shadow meshes at 0.
    //   csmRebase:    keep only the last (real) environment: nodes, object refs, shadow meshes and their indices
    //                 are rebased so the root is node 0 (default off)
    //   emptyShadowMeshes: write lumps 0x7C..0x7F empty (default off)
    bool csmRebase = false;
    bool emptyShadowMeshes = false;
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
