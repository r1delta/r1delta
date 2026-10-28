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
            // The real environment is chosen by content, not by position: R2 lists the empty dummy (1 node, no refs, 1 shadow mesh) FIRST
            // on 17 maps but LAST on mp_black_water_canal and mp_angel_city (whose real tree sits in records 0), so "keep the last" kept
            // the dummy there and dropped the whole level tree and its 17 world shadow meshes. Largest node + ref + mesh count wins, ties
            // go to the later record (every other map: the real record is last and the only non-dummy one). opt.csmLastEnv restores "the last
            // record": mp_coliseum and mp_coliseum_column have no cloudmask, so the entity rewrite leaves their dummy light_environment last
            // and R1's CSM sun direction stays the dummy's (-0.335 0.526 -0.782); their real tree (a column) would cast a long wrong shadow.
            size_t pick = nEnv - 1, bestScore = 0;
            for (size_t i = 0; i < nEnv && !c.opt.csmLastEnv; i++) {
                const size_t score = (size_t)(env[i].last[0] - env[i].first[0]) + (size_t)(env[i].last[1] - env[i].first[1]) + (size_t)(env[i].last[2] - env[i].first[2]);
                if (i == 0 || score >= bestScore) { pick = i; bestScore = score; }
            }
            const ShadowEnv& e = env[pick];
            if (env[nEnv - 1].last[0] != (int32_t)nNodes || env[nEnv - 1].last[2] != (int32_t)nMesh || env[nEnv - 1].last[1] > (int32_t)nRefs)
                c.fail("[rule 17] last shadow environment ends at node %d / ref %d / mesh %d but the lumps hold %zu / %zu / %zu", env[nEnv - 1].last[0], env[nEnv - 1].last[1], env[nEnv - 1].last[2], nNodes, nRefs, nMesh);
            for (size_t i = 0; i < nEnv; i++)
                if (env[i].first[1] < 0 || env[i].first[1] > env[i].last[1] || env[i].last[1] > (int32_t)nRefs) c.fail("[rule 17] shadow environment %zu: object refs [%d, %d) outside the %zu refs", i, env[i].first[1], env[i].last[1], nRefs);
            c.throwIfFailed("rule 17");
            const uint32_t fn = (uint32_t)e.first[0], fr = (uint32_t)e.first[1], fm = (uint32_t)e.first[2];
            const uint32_t ln = (uint32_t)e.last[0], lr = (uint32_t)e.last[1], lm = (uint32_t)e.last[2];
            // Nodes.
            Bytes nn(nodesB.begin() + (size_t)fn * 32, nodesB.begin() + (size_t)ln * 32);
            size_t fixedChild = 0;
            for (size_t i = 0; i < nn.size() / 32; i++) {
                CsmNode n;
                std::memcpy(&n, &nn[i * 32], 32);
                if (n.numChildren > 0) {
                    if (n.firstChild < fn || (uint32_t)(n.firstChild + n.numChildren) > ln) c.fail("[rule 17] csm node %zu: child range [%u, +%u) outside the chosen environment", i + fn, n.firstChild, n.numChildren);
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
            Bytes rr(refsB.begin() + (size_t)fr * 2, refsB.begin() + (size_t)lr * 2);
            // Shadow meshes and their indices.
            std::vector<ShadowMesh> sm(nMesh);
            std::memcpy(sm.data(), meshB.data(), meshB.size());
            size_t dropIdx = 0;
            for (uint32_t i = 0; i < fm; i++) dropIdx += (size_t)sm[i].numTriangles * 3;
            size_t keepIdx = 0;
            for (uint32_t i = fm; i < lm; i++) keepIdx += (size_t)sm[i].numTriangles * 3;
            size_t totalIdx = 0;
            for (auto& m : sm) totalIdx += (size_t)m.numTriangles * 3;
            if (totalIdx * 2 != idxB.size()) c.fail("[rule 17] shadow meshes address %zu indices, lump 0x7E holds %zu", totalIdx, idxB.size() / 2);
            c.throwIfFailed("rule 17");
            Bytes mm(meshB.begin() + (size_t)fm * 12, meshB.begin() + (size_t)lm * 12);
            Bytes ii(idxB.begin() + dropIdx * 2, idxB.begin() + (dropIdx + keepIdx) * 2);
            c.say("[rule 17] csm rebase: chose environment %zu of %zu (largest content) first=(node %u, ref %u, mesh %u) last=(%u, %u, %u); dropped %u leading + %zu trailing node(s), %u leading + %zu trailing ref(s), %u leading + %zu trailing shadow mesh(es) with %zu indices; %zu nodes' child indices lowered by %u",
                  pick, nEnv, fn, fr, fm, ln, lr, lm, fn, nNodes - ln, fr, nRefs - lr, fm, nMesh - lm, dropIdx + (idxB.size() / 2 - dropIdx - keepIdx), fixedChild, fn);
            // Object references beyond the object space name objects R1 does not have: R1 would index past its prop array. Drop them and
            // re-derive every node's ref ranges. The space is [0, propBase) for render meshes and [propBase, propBase + props) for static
            // props, with propBase = levelinfo word 2 rounded up to 64 (R1 loader sub_1800B2630: v40 = (word2 + 63) & ~63; R2 uses the same
            // base: on mp_glitch word 2 = 2903, base 2944, refs 2947..19125 and not one of the 1322 refs names a NO_SHADOW prop at that base).
            // It is not meshes (lump 0x50) + props: that bound dropped the last 34 valid prop references of mp_glitch.
            {
                uint32_t meshWord = 0, nProps = 0;
                if (c.lump[L_LEVELINFO].size() >= 16) {
                    std::memcpy(&meshWord, &c.lump[L_LEVELINFO][8], 4);
                    std::memcpy(&nProps, &c.lump[L_LEVELINFO][12], 4);
                }
                const size_t propBase = ((size_t)meshWord + 63) & ~(size_t)63;
                const size_t objTotal = propBase + nProps;
                const size_t n = rr.size() / 2;
                std::vector<uint32_t> prefix(n + 1, 0);
                size_t dropped = 0;
                for (size_t i = 0; i < n; i++) {
                    uint16_t v;
                    std::memcpy(&v, &rr[i * 2], 2);
                    bool keep = v < objTotal;
                    prefix[i + 1] = prefix[i] + (keep ? 1u : 0u);
                    if (!keep) dropped++;
                }
                if (dropped) {
                    Bytes kept;
                    kept.reserve((n - dropped) * 2);
                    for (size_t i = 0; i < n; i++)
                        if (prefix[i + 1] != prefix[i]) kept.insert(kept.end(), rr.begin() + i * 2, rr.begin() + i * 2 + 2);
                    for (size_t i = 0; i < nn.size() / 32; i++) {
                        CsmNode nd;
                        std::memcpy(&nd, &nn[i * 32], 32);
                        const size_t f = nd.firstObjRef;
                        const size_t own = nd.numObjRefs, tot = nd.totalObjRefs;
                        if (f + tot > n || f + own > n) c.fail("[rule 17] csm node %zu: ref range [%zu, +%zu) outside the reference pool", i, f, tot);
                        else {
                            nd.firstObjRef = (uint16_t)prefix[f];
                            nd.numObjRefs = (uint8_t)(prefix[f + own] - prefix[f]);
                            nd.totalObjRefs = (uint16_t)(prefix[f + tot] - prefix[f]);
                        }
                        std::memcpy(&nn[i * 32], &nd, 32);
                    }
                    c.throwIfFailed("rule 17");
                    rr = std::move(kept);
                }
                c.say("[rule 17] csm rebase: object space = prop base %zu (levelinfo word 2 = %u rounded up to 64) + %u props = %zu; %zu of %zu object references named objects beyond it and were dropped",
                      propBase, meshWord, nProps, objTotal, dropped, n);
            }
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
