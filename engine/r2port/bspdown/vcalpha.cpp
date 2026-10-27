// Origin: C:\Users\r3muxd\tmp\r2port\bspdown. Bundled in r1delta under GPL-3.0.
// Rule 22: opaque vertex colour alpha for vertex-tinted single-layer world materials.
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

}  // namespace

void convertVertexColorAlpha(Ctx& c) {
    const std::set<std::string> targets(std::begin(kOpaqueVertexAlphaMaterials), std::end(kOpaqueVertexAlphaMaterials));
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

    size_t meshesHit = 0, verticesWritten = 0, verticesChanged = 0;
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
            if (vb[o + 19] != 255) verticesChanged++;
            vb[o + 19] = 255;
            verticesWritten++;
        }
        meshesHit++;
    }
    c.say("[rule 22] vertex colour alpha of the listed vertex-tinted single-layer world materials (%zu names) forced to 255: %zu meshes, %zu vertices (%zu changed); R2 ignores it for these materials (no vcola token) and R1 would read it as dithered opacity",
          targets.size(), meshesHit, verticesWritten, verticesChanged);
}

}  // namespace detail
}  // namespace bspdown
