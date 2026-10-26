// Origin: C:\Users\r3muxd\tmp\r2port\bspdown. Bundled in r1delta under GPL-3.0.
// Rule 17: shadow environments (R2 lump 0x05), CSM aabb tree (0x63) and object references (0x64),
// shadow meshes (0x7C..0x7F).
//
// Layouts (data, bsp_tool titanfall.py/titanfall2.py, R1 loaders sub_1800B2540 / sub_1800BC060):
//   0x05 (R2 only): records of 36 bytes {int first[3] (csm node, csm obj ref, shadow mesh),
//                   int last[3] (exclusive ends), float sunDir[3]}; every R2 map checked has two records: a dummy
//                   environment (one empty node with +-FLT_MAX bounds, no refs, one 6 triangle mesh) and the real
//                   one. R1 has no such table: it reads 0x63 as a tree whose root is node 0 and 0x7C..0x7F as one
//                   set. Stock R1 files (mp_box, mp_rise) have the real root at node 0.
//   0x63: 32 byte nodes {float mins[3]; u8 numChildren, numObjRefs; u16 totalObjRefs; float maxs[3];
//                   u16 firstChild, firstObjRef}; firstChild is an absolute node index.
//   0x64: u16 object references (shared object index space, not touched).
//   0x7F: 12 byte shadow meshes {u32 vertexOffset; u32 numTriangles; u16 isOpaque; i16 materialSort}; the indices of mesh
//                   k start at sum(numTriangles of meshes < k) * 3 in 0x7E (u16, relative to vertexOffset).
#include "internal.hpp"

namespace bspdown {
namespace detail {

namespace {

#pragma pack(push, 1)
struct ShadowEnv {
    int32_t first[3];
    int32_t last[3];
    float sun[3];
};
static_assert(sizeof(ShadowEnv) == 36, "ShadowEnv");
struct CsmNode {
    float mins[3];
    uint8_t numChildren, numObjRefs;
    uint16_t totalObjRefs;
    float maxs[3];
    uint16_t firstChild, firstObjRef;
};
static_assert(sizeof(CsmNode) == 32, "CsmNode");
struct ShadowMesh {
    uint32_t vertexOffset;
    uint32_t numTriangles;
    uint16_t isOpaque;
    int16_t materialSort;
};
static_assert(sizeof(ShadowMesh) == 12, "ShadowMesh");
#pragma pack(pop)

}  // namespace

void convertShadow(Ctx& c) {
    const Bytes& envB = c.lump[L_R2_SHADOWENV];
    Bytes& nodesB = c.lump[L_CSMAABB];
    Bytes& refsB = c.lump[0x64];
    Bytes& meshB = c.lump[0x7F];
    Bytes& idxB = c.lump[0x7E];
    const size_t nEnv = envB.size() / sizeof(ShadowEnv);
    c.say("[rule 17] shadow data: %zu shadow environments (0x05), %zu csm nodes, %zu csm obj refs, %zu shadow meshes, %zu shadow indices, %zu opaque + %zu alpha shadow vertices",
          nEnv, nodesB.size() / 32, refsB.size() / 2, meshB.size() / 12, idxB.size() / 2, c.lump[L_SHADOWOPAQUE].size() / 12, c.lump[L_SHADOWALPHA].size() / 20);

    if (c.opt.csmRebase) {
        if (envB.size() % sizeof(ShadowEnv) || nEnv < 2) {
            c.say("[rule 17] csm rebase: lump 0x05 has %zu bytes (%zu records), fewer than 2 environments: nothing to rebase", envB.size(), nEnv);
        } else {
            std::vector<ShadowEnv> env(nEnv);
            std::memcpy(env.data(), envB.data(), envB.size());
            const size_t nNodes = nodesB.size() / 32, nRefs = refsB.size() / 2, nMesh = meshB.size() / 12;
            // The environments must tile the arrays.
            for (size_t i = 0; i < nEnv; i++) {
                for (int k = 0; k < 3; k++) {
                    int32_t expectFirst = i == 0 ? 0 : env[i - 1].last[k];
                    if (k == 1) expectFirst = 0;  // obj refs are a shared pool: every environment's refs start at its own first_ref, see below
                    if (k != 1 && env[i].first[k] != expectFirst)
                        c.fail("[rule 17] shadow environment %zu: first[%d] = %d does not follow the previous environment's end", i, k, env[i].first[k]);
                }
            }
            const ShadowEnv& e = env[nEnv - 1];
            if (e.last[0] != (int32_t)nNodes || e.last[2] != (int32_t)nMesh || e.last[1] > (int32_t)nRefs)
                c.fail("[rule 17] last shadow environment ends at node %d / ref %d / mesh %d but the lumps hold %zu / %zu / %zu", e.last[0], e.last[1], e.last[2], nNodes, nRefs, nMesh);
            if (e.first[0] < 1 || e.first[2] < 1) c.fail("[rule 17] last shadow environment starts at node %d / mesh %d (expected a preceding dummy environment)", e.first[0], e.first[2]);
            c.throwIfFailed("rule 17");
            const uint32_t fn = (uint32_t)e.first[0], fr = (uint32_t)e.first[1], fm = (uint32_t)e.first[2];
            // Nodes.
            Bytes nn(nodesB.begin() + (size_t)fn * 32, nodesB.end());
            size_t fixedChild = 0;
            for (size_t i = 0; i < nn.size() / 32; i++) {
                CsmNode n;
                std::memcpy(&n, &nn[i * 32], 32);
                if (n.numChildren > 0) {
                    if (n.firstChild < fn || n.firstChild + n.numChildren > nNodes) c.fail("[rule 17] csm node %zu: child range [%u, +%u) outside the last environment", i + fn, n.firstChild, n.numChildren);
                    n.firstChild = (uint16_t)(n.firstChild - fn);
                    fixedChild++;
                }
                if (n.numObjRefs > 0) {
                    if (n.firstObjRef < fr) c.fail("[rule 17] csm node %zu: object ref start %u before the environment's first ref %u", i + fn, n.firstObjRef, fr);
                    n.firstObjRef = (uint16_t)(n.firstObjRef - fr);
                }
                std::memcpy(&nn[i * 32], &n, 32);
            }
            c.throwIfFailed("rule 17");
            // Object references: the pool is not renumbered (values are object indices), only the part before the environment is cut.
            Bytes rr(refsB.begin() + (size_t)fr * 2, refsB.begin() + (size_t)e.last[1] * 2);
            // Shadow meshes and their indices.
            std::vector<ShadowMesh> sm(nMesh);
            std::memcpy(sm.data(), meshB.data(), meshB.size());
            size_t dropIdx = 0;
            for (uint32_t i = 0; i < fm; i++) dropIdx += (size_t)sm[i].numTriangles * 3;
            size_t totalIdx = 0;
            for (auto& m : sm) totalIdx += (size_t)m.numTriangles * 3;
            if (totalIdx * 2 != idxB.size()) c.fail("[rule 17] shadow meshes address %zu indices, lump 0x7E holds %zu", totalIdx, idxB.size() / 2);
            c.throwIfFailed("rule 17");
            Bytes mm(meshB.begin() + (size_t)fm * 12, meshB.end());
            Bytes ii(idxB.begin() + dropIdx * 2, idxB.end());
            c.say("[rule 17] csm rebase: last environment first=(node %u, ref %u, mesh %u) last=(%d, %d, %d); dropped %u leading node(s), %u leading ref(s), %u leading shadow mesh(es) with %zu indices; %zu nodes' child indices lowered by %u",
                  fn, fr, fm, e.last[0], e.last[1], e.last[2], fn, fr, fm, dropIdx, fixedChild, fn);
            nodesB = std::move(nn);
            refsB = std::move(rr);
            meshB = std::move(mm);
            idxB = std::move(ii);
        }
    }

    if (c.opt.emptyShadowMeshes) {
        const size_t v = c.lump[L_SHADOWOPAQUE].size(), a = c.lump[L_SHADOWALPHA].size(), i = idxB.size(), m = meshB.size();
        c.lump[L_SHADOWOPAQUE].clear();
        c.lump[L_SHADOWALPHA].clear();
        idxB.clear();
        meshB.clear();
        c.say("[rule 17] shadow meshes emptied: removed %zu opaque vertex bytes, %zu alpha vertex bytes, %zu index bytes, %zu mesh bytes (lumps 0x7C..0x7F written empty)", v, a, i, m);
    }
}

}  // namespace detail
}  // namespace bspdown
