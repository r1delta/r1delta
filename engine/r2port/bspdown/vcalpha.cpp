// Origin: C:\Users\r3muxd\tmp\r2port\bspdown. Bundled in r1delta under GPL-3.0.
// Rule 22: opaque vertex colour alpha for vertex-tinted single-layer world materials.
// Rule 24: emitter glow ramp coordinate for layered pure-emitter world materials.
//
// R2 shader sets with the `vcolt` token multiply the albedo by the mesh vertex colour (a real tint: mp_black_water_canal's red
// beacon_metal_panel_grey_02_vc walls carry (139,57,57)). r2tex now emits `$vertexcolor 1` for these materials (kTintedVertexWorldMaterials in
// r2tex.cpp), but their vertices also carry a graded alpha (135 on the BWC wall) that R2 ignores (no `vcola` token, not layered). R1 reads the
// vertex alpha of an opaque LightmappedGeneric as dithered opacity: measured in game on mp_black_water_canal cap0, high-frequency frame noise of the
// wall 1.47 with `$vertexcolor 1` and the original alpha, 0.76 (= the untinted baseline) with alpha 255.
//
// Vertex layouts of all four lit/unlit lumps keep the albedo texture coordinate at +8 (2 floats) and the colour at +16..+19 (RGBA); the alpha byte
// is +19. Vertices are owned by one sort (one material) so rewriting by material name is exact. Meshes of other materials are untouched.
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
// slot 22, premultiplied blend) are drawn by R2 as out = E * c + dst * (1 - c) with c a function of the interpolated mesh vertex alpha a
// (mp_glitch vr_light_trim_01_totrans: the 32 unit trim strips carry alpha 89 on the outer vertices and 255 on the inner ones, other meshes of the
// material 0 / 63 / 127 / 191; R2 shows a soft warm glow that is strongest at the outer edge). Measured on the R2 frame at the ramp wall panel
// (eye 267.20 -201.98 316.03): the glow pixel at the alpha 89 edge is 123 116 110 on dst 23 55 90 with E = 177 149 121, which gives c = 0.65 on all
// three channels, and the glow is gone about 47 % across the strip, so c = clamp((160 - a) / 109, 0, 1) (a 0 -> 1, 63 -> 0.89, 89 -> 0.65, 127 -> 0.30,
// 160 and above -> 0).
// R1 cannot apply that clamp per pixel from the vertex alpha: $vertexalpha interpolates the two vertex rows linearly, so a first version that stored c
// in the alpha byte spread the glow over the whole strip (veil over the panel, measured against the R2 frame). Instead the r2tex VMT is an
// UnlitGeneric $translucent 1 of a generated ramp texture (r2tex/glow/<_ilm name>, 256 x 4 RGBA8888, clamped, alpha of texel i = c(i)) and this
// rule stores the vertex alpha as the albedo texture coordinate: u = (a + 0.5) / 256, v = 0.5, alpha byte 255. The rasteriser interpolates a
// (perspective correct, like R2 does with the alpha), the texture lookup then evaluates c per pixel, clamp included. The ramp texel centre of a is
// (a + 0.5) / 256, so the vertex values hit texel centres exactly. Keep the list and the 256 width in sync with r2tex (kGlowRampPrefix,
// buildGlowRampVtf); only world/vr_training/vr_light_trim_01_totrans qualifies over the 20 maps. The R2 tiling of this material's texture
// coordinates is dropped (the source texture is a uniform white mask, so nothing is lost).
const char* const kEmitterGlowMaterials[] = {
    "world/vr_training/vr_light_trim_01_totrans",
};
constexpr unsigned kGlowRampWidth = 256;

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

struct VertexPassStats {
    size_t meshesHit = 0, verticesWritten = 0, verticesChanged = 0;
};

// Calls `edit(vertexRecord)` once for every vertex referenced by the meshes whose material name is in `targets`; `edit` returns true when it
// changed the record.
template <class Edit>
VertexPassStats rewriteVertices(Ctx& c, const std::set<std::string>& targets, Edit edit) {
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

    VertexPassStats s;
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
            if (edit(&vb[o])) s.verticesChanged++;
            s.verticesWritten++;
        }
        s.meshesHit++;
    }
    return s;
}

}  // namespace

void convertVertexColorAlpha(Ctx& c) {
    const std::set<std::string> targets(std::begin(kOpaqueVertexAlphaMaterials), std::end(kOpaqueVertexAlphaMaterials));
    const VertexPassStats s = rewriteVertices(c, targets, [](uint8_t* vertex) {
        const bool changed = vertex[19] != 255;
        vertex[19] = 255;
        return changed;
    });
    c.say("[rule 22] vertex colour alpha of the listed vertex-tinted single-layer world materials (%zu names) forced to 255: %zu meshes, %zu vertices (%zu changed); R2 ignores it for these materials (no vcola token) and R1 would read it as dithered opacity",
          targets.size(), s.meshesHit, s.verticesWritten, s.verticesChanged);
}

void convertEmitterGlowRamp(Ctx& c) {
    const std::set<std::string> targets(std::begin(kEmitterGlowMaterials), std::end(kEmitterGlowMaterials));
    const VertexPassStats s = rewriteVertices(c, targets, [](uint8_t* vertex) {
        const float u = ((float)vertex[19] + 0.5f) / (float)kGlowRampWidth;
        const float v = 0.5f;
        std::memcpy(vertex + 8, &u, 4);
        std::memcpy(vertex + 12, &v, 4);
        vertex[19] = 255;
        return true;
    });
    c.say("[rule 24] layered pure-emitter world materials (%zu names): vertex alpha a stored as the texture coordinate ((a + 0.5) / %u, 0.5) for the r2tex glow ramp texture (clamp((160 - a) / 109)), alpha byte 255: %zu meshes, %zu vertices",
          targets.size(), kGlowRampWidth, s.meshesHit, s.verticesWritten);
}

}  // namespace detail
}  // namespace bspdown
