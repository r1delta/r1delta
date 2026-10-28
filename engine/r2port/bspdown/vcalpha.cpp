// Origin: C:\Users\r3muxd\tmp\r2port\bspdown. Bundled in r1delta under GPL-3.0.
// Rule 22: opaque vertex colour alpha for vertex-tinted single-layer world materials.
// Rule 24: emitter glow weight in the vertex alpha of layered pure-emitter world materials.
//
// R2 shader sets with the `vcolt` token multiply the albedo by the mesh vertex colour (a real tint: mp_black_water_canal's red
// beacon_metal_panel_grey_02_vc walls carry (139,57,57)). r2tex now emits `$vertexcolor 1` for these materials (kTintedVertexWorldMaterials in
// r2tex.cpp), but their vertices also carry a graded alpha (135 on the BWC wall) that R2 ignores (no `vcola` token, not layered). R1 reads the
// vertex alpha of an opaque LightmappedGeneric as dithered opacity: measured in game on mp_black_water_canal cap0, high-frequency frame noise of the
// wall 1.47 with `$vertexcolor 1` and the original alpha, 0.76 (= the untinted baseline) with alpha 255.
//
// Vertex layouts of all four lit/unlit lumps keep the colour at +16..+19 (RGBA); the alpha byte is +19. Vertices are owned by one sort (one
// material) so rewriting by material name is exact. Meshes of other materials are untouched.
#include "internal.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <string>

namespace bspdown {
namespace detail {

namespace {

// Keep in sync with the entries of kTintedVertexWorldMaterials (r2tex.cpp) that carry graded vertex alpha. Other entries have opaque alpha
// already (the pass is then a no-op), so the whole r2tex list may be listed here; only names that are present in the map matter.
const char* const kOpaqueVertexAlphaMaterials[] = {
    "world/beacon/beacon_metal_panel_grey_02/beacon_metal_panel_grey_02_vc",
    "world/concrete/concrete_broken_surface",
    "world/concrete/concrete_dam_01",
    "world/concrete/dam_concrete_01",
    "world/metal/ext_building_panel_imc_02",
    "world/metal/ext_building_panel_imc_03",
    "world/metal/metal_wall_panels_clean_vc",
    "world/ola/sewer_lab_trim_02",
    "world/ola/sewer_lab_trim_04",
    "world/ola/sewer_lab_trim_05",
    "world/ola/sewer_padded_ceiling_panels",
    "world/ola/sewer_rubber_floor_panel_01",
    "world/signs/beacon_tileable_decal_01",
    "world/timeshift/concrete/concrete_gap_wall_01",
    "world/timeshift/rock/ts_rockwall01_vc",
};

// Rule 24. Layered pure emitters (r2tex ConvertMaterial pure-emitter route with the glow variant: black albedo, white _ilm, HDR emissive tint, layer
// slot 22, premultiplied blend) are drawn by R2 as out = E * c + dst * (1 - c) with c a function of the vertex alpha (mp_glitch
// vr_light_trim_01_totrans: the 32 unit trim strips carry alpha 89 on the outer vertices and 255 on the inner ones, other meshes of the material
// 0 / 63 / 127 / 191; R2 shows a soft warm glow that is strongest at the outer edge). Measured on the R2 frame at the ramp wall panel
// (eye 267.20 -201.98 316.03): the glow pixel at the alpha 89 edge is 123 116 110 on dst 23 55 90 with E = 177 149 121, which gives c = 0.65 on
// all three channels, and the glow is gone about 47 % across the strip, so c = clamp((160 - alpha) / 109, 0, 1) (alpha 0 -> 1, 63 -> 0.89,
// 89 -> 0.65, 127 -> 0.30, 160 and above -> 0). The R1 VMT (UnlitGeneric, $translucent 1, $vertexalpha 1, $color = E) blends with the vertex alpha as
// out = $color * alpha + dst * (1 - alpha), the same equation, so the vertex alpha is rewritten to c * 255. R1 interpolates it linearly between the
// vertices, so the glow falls to 0 at the inner edge instead of at 47 %; the integrated glow over the strip is the same (0.325 of E).
// Without the rule the R2 alpha would draw the glow inverted (dim at the outer edge, solid inward). Keep the list in sync with the r2tex pure-emitter glow
// predicate (hasLayerSlots && translucent); only world/vr_training/vr_light_trim_01_totrans over the 20 maps qualifies.
const char* const kEmitterGlowMaterials[] = {
    "world/vr_training/vr_light_trim_01_totrans",
};
constexpr double kGlowAlphaZero = 160.0;  // vertex alpha at which the R2 glow weight reaches 0
constexpr double kGlowAlphaSpan = 109.0;  // alpha range over which the weight rises from 0 to 1 (alpha 51 and below -> 1)

struct VtxType {
    uint32_t lump;
    unsigned stride;
};
// index = (mesh flags >> 9) & 3 (same table as meshclip.cpp)
const VtxType kVtxTypes[4] = {
    {L_VTX_LITFLAT, 36},
    {L_VTX_LITBUMP, 44},
    {L_VTX_UNLIT, 20},
    {L_VTX_UNLITTS, 28},
};

std::string normalized(std::string n) {
    for (auto& ch : n) {
        if (ch == '\\') ch = '/';
        ch = (char)std::tolower((unsigned char)ch);
    }
    return n;
}

struct AlphaPassStats {
    size_t meshesHit = 0, verticesWritten = 0, verticesChanged = 0;
};

// Rewrites the alpha byte of every vertex of the meshes whose material name is in `targets`; `map` gives the new alpha for the old one.
template <class Map>
AlphaPassStats rewriteVertexAlpha(Ctx& c, const std::set<std::string>& targets, Map map) {
    const std::vector<MeshR2> meshes = view<MeshR2>(c.lump[L_MESHES], "meshes (0x50)");
    const std::vector<Sort> sorts = view<Sort>(c.lump[L_SORTS], "material sorts (0x52)");
    const Bytes& texdata = c.lump[L_TEXDATA];
    const Bytes& strTab = c.lump[L_TDSTRTAB];
    const Bytes& strData = c.lump[L_TDSTRDATA];
    const Bytes& meshIdx = c.lump[L_MESHIDX];

    std::vector<int8_t> sortIsTarget(sorts.size(), -1);  // -1 unknown, 0 no, 1 yes
    auto isTargetSort = [&](size_t si) -> bool {
        if (si >= sorts.size()) return false;
        if (sortIsTarget[si] >= 0) return sortIsTarget[si] == 1;
        bool yes = false;
        const int td = sorts[si].textureData;
        if (td >= 0 && (size_t)(td + 1) * 36 <= texdata.size()) {
            int32_t nameIndex;
            std::memcpy(&nameIndex, &texdata[(size_t)td * 36 + 12], 4);
            if (nameIndex >= 0 && (size_t)(nameIndex + 1) * 4 <= strTab.size()) {
                uint32_t off;
                std::memcpy(&off, &strTab[(size_t)nameIndex * 4], 4);
                if (off < strData.size()) {
                    size_t n = 0;
                    while (off + n < strData.size() && strData[off + n]) n++;
                    yes = targets.count(normalized(std::string((const char*)&strData[off], n))) != 0;
                }
            }
        }
        sortIsTarget[si] = yes ? 1 : 0;
        return yes;
    };

    AlphaPassStats s;
    for (const MeshR2& m : meshes) {
        if (m.numTriangles == 0 || !isTargetSort(m.materialSort)) continue;
        const VtxType& T = kVtxTypes[(m.flags >> 9) & 3];
        Bytes& vb = c.lump[T.lump];
        const size_t nIdx = 3ull * m.numTriangles;
        if ((uint64_t)m.firstMeshIndex * 2 + nIdx * 2 > meshIdx.size()) continue;
        const uint32_t base = (uint32_t)sorts[m.materialSort].vertexOffset;
        std::set<uint32_t> seen;
        for (size_t k = 0; k < nIdx; k++) {
            uint16_t ix;
            std::memcpy(&ix, &meshIdx[((size_t)m.firstMeshIndex + k) * 2], 2);
            const uint32_t v = base + ix;
            if (!seen.insert(v).second) continue;
            const size_t o = (size_t)v * T.stride;
            if (o + T.stride > vb.size()) continue;
            const uint8_t a = map(vb[o + 19]);
            if (vb[o + 19] != a) s.verticesChanged++;
            vb[o + 19] = a;
            s.verticesWritten++;
        }
        s.meshesHit++;
    }
    return s;
}

}  // namespace

void convertVertexColorAlpha(Ctx& c) {
    const std::set<std::string> targets(std::begin(kOpaqueVertexAlphaMaterials), std::end(kOpaqueVertexAlphaMaterials));
    const AlphaPassStats s = rewriteVertexAlpha(c, targets, [](uint8_t) -> uint8_t { return 255; });
    c.say("[rule 22] vertex colour alpha of the listed vertex-tinted single-layer world materials (%zu names) forced to 255: %zu meshes, %zu vertices (%zu changed); R2 ignores it for these materials (no vcola token) and R1 would read it as dithered opacity",
          targets.size(), s.meshesHit, s.verticesWritten, s.verticesChanged);
}

void convertEmitterGlowAlpha(Ctx& c) {
    const std::set<std::string> targets(std::begin(kEmitterGlowMaterials), std::end(kEmitterGlowMaterials));
    const AlphaPassStats s = rewriteVertexAlpha(c, targets, [](uint8_t a) -> uint8_t {
        const double w = std::clamp((kGlowAlphaZero - (double)a) / kGlowAlphaSpan, 0.0, 1.0);
        return (uint8_t)std::lround(255.0 * w);
    });
    c.say("[rule 24] vertex alpha of the layered pure-emitter world materials (%zu names) rewritten to the R2 glow weight clamp((160 - alpha) / 109, 0, 1) * 255: %zu meshes, %zu vertices (%zu changed); the r2tex VMT blends with it ($translucent 1, $vertexalpha 1)",
          targets.size(), s.meshesHit, s.verticesWritten, s.verticesChanged);
}

}  // namespace detail
}  // namespace bspdown
