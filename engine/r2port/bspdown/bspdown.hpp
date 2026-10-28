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

    // Rule 13b: per-cubemap scale from the R2 cubemap multiplier array (resource CMA of cubemaps.hdr.vtf, one float per cubemap
    // frame, the shader constant R2's materialsystem uploads per cubemap: the stored cubes are exposure normalised, mp_forwardbase_kodai
    // 5.0 3.26 2.34 8.89 0.88 0.86, mp_glitch 1.3, mp_lf_stacks 0.7). scale = clamp(cubemapCmaGain * CMA, cubemapMinScale, cubemapScale);
    // cubemapScale stays the upper bound, so maps whose CMA is large (night maps, mp_wargames 27..187) keep exactly the old scale 4.
    // cubemapCmaGain 0 = one global scale (the behaviour before rule 13b).
    float cubemapCmaGain = 1.0f;
    float cubemapMinScale = 0.25f;

    // Rule 13c: R1 picks the cubemap of a dynamic model (viewmodel, players, non-static props) as the sample nearest to the model's
    // lighting origin (only for models whose studiohdr has USES_ENV_CUBEMAP 0x2, see modelconv); R2 assigns a cubemap to every light probe
    // ref (lump 0x68 trailing int32; mp_forwardbase_kodai: 77 % of the refs use the one outdoor cube 4, the nearest sample picks cubes 1/3/5
    // there). Up to cubemapAssignMax extra samples (copies of an existing cube) are added greedily as 1-nearest-neighbour prototypes: the
    // medoid of a still misclassified same-label cluster (radii 0.5/1/2/4 x cubemapAssignCell) with the largest net gain, exploring up to 24
    // samples past a local optimum and keeping the best prefix. 0 = off.
    int cubemapAssignMax = 64;
    float cubemapAssignCell = 256.0f;

    // Diagnostic (not a product setting): 1 = every cubemap frame is replaced by one flat colour that encodes its R2 frame index
    // (base 4 digits d2 d1 d0 -> R,G,B = {0.2,0.45,0.9,1.8}[digit]), copies made by rule 13c carry the colour of the frame they copy;
    // 2 = every frame is flat neutral 1.0. Screenshots then show which cube each surface class (world, prop, dynamic model, viewmodel) uses.
    int cubemapDiag = 0;

    // World light flags: stock R1 lights carry bit 0x2 (R2 never sets it); set it on lights with non-zero flags.
    bool worldLightFlagBit2 = false;

    // Rule 20: world lights, light probe light indices and real-time lights (lumps 0x36, 0x65, 0x69), see README.txt.
    // collapseEnvs: R2 maps carry one sky-ambient/sun pair per light_environment (a dummy skybox environment first, the real one last).
    //   R1 supports exactly one pair at the start of the array ([ambient, sun, locals...]); the dummy pairs are dropped, the real (last)
    //   pair is moved to indices 0/1 and every light index stored in other lumps is remapped.
    // rtlConvert: decode the R2 real-time light lump (9 bytes per texel + 0x7A palettes) and write the R1 layout (4 slots of
    //   world light index - 1). false: all 0xFF (no real-time lights), the behaviour before rule 20.
    // The visibility cutoff is fixed at sRGB byte 187 (about half visible); it is not a raw B/contribution cutoff.
    //   Local B includes a spotlight's cone. Reconstruct sampled lightmap receivers and compare linear(B) >= linear(187) * cone.
    //   Point-light B is tested directly. Zero B is always absent; R1 still cannot preserve fractional static visibility.
    // pbrLights: R2 lights with flag 0x20 (_pbr_falloff) and without 0x4 carry a luminous intensity (about 392x the usual unit) and
    //   R1 evaluates lights without 0x4 as a reciprocal polynomial; true rewrites them to the R1 fade-polynomial form.
    bool collapseEnvs = true;
    bool rtlConvert = true;
    bool pbrLights = true;
    // Rule 20e (LightVerify, mp_glitch / mp_lf_stacks measured against original R2 frames, glitchlight\ROUND3.md): linear multipliers on the
    //   intensity of the kept light records. The R1 shaders add sky ambient (type 5) and sun (type 3) with a different weighting than the R2 ones
    //   (R1 world PS: lmEnergy + bumpSkyWeight*skyColor + shadow*cloud*N.L*sunColor), so equal numbers over-light the shadows (blue/lilac
    //   shadows: ground shadow luminance 86 vs 59 in R2 at the mp_glitch spawn).
    //   skyAmbientScale: multiplies the type 5 records (GPU sky constant and CPU prop ambient, both then times mat_sky_scale). 0.45 puts the
    //     shadow luminance and the whole frame within a few percent of R2 on mp_glitch and mp_lf_stacks with mat_sky_scale 1.6 (user config);
    //     0.35 gave the same numbers, 1.0 left ground +29 %, walls +7 %.
    //   sunScale: multiplies the type 3 records (times mat_sun_scale). 1.0: 1.25 changed no class by more than 2 % and made mp_lf_stacks worse.
    //   pbrScale: extra multiplier on the PBR (flag 0x20) lights after the 2/d50^2 rewrite. 1.0: 0.01 .. 4 changes the world by < 3 % (those
    //     lights are baked) and the CPU-lit gun by 8 % (no flare-up near the light).
    float skyAmbientScale = 0.45f;
    float sunScale = 1.0f;
    float pbrScale = 1.0f;

    // Rule 21: lightmap plane B alpha (lump 0x62). The R1 world pixel shader (lightmappedgeneric_ps50) uses (1 - plane B alpha) as the sun
    // shadow term wherever the CSM cascades do not cover the pixel (and everywhere when CSM is off). R1 compilers bake the sun visibility
    // there (stock mp_rise: 89 % of texels 255 = shadowed, 6 % 0 = sunlit, graded in between); R2 writes 255 everywhere and keeps the
    // visibility in the real-time-light B weight of the sun slot. true: write 255 - 255*linear(sun visibility) from the R2 RTL sun slot.
    bool lightmapSunAlpha = true;

    // Rule 20f (StairStep experiment): bake real-time spot/point light contributions into lightmap plane A instead of the binary RTL list.
    //   0 = off (default), 1 = lights whose pools have shadow edges, 2 = every real-time light.
    int rtlBake = 0;
    float rtlBakeEdgeMin = 0.0f;

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

    // Rule 23: R1 culls a static prop by the distance from the view to its ORIGIN against a fade distance (R2 -1 = auto from the model hull
    // size). R2 props whose origin is far from their geometry (water planes, waterfalls, god rays at 0,0,0) are culled although the view is
    // next to the geometry. Default: raise the fade distance of such props. keepPropFade = true copies the R2 value for every prop.
    bool keepPropFade = false;

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
    //   csmRebase:    keep only the real environment (largest node + ref + mesh count; NOT "the last": R2 lists it first on
    //                 mp_black_water_canal, mp_angel_city and both coliseum maps): nodes, object refs, shadow meshes and their indices
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
