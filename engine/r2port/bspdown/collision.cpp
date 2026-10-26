// Origin: C:\Users\r3muxd\tmp\r2port\bspdown. Bundled in r1delta under GPL-3.0.
// Rule 4 (collision grid, geosets, primitives), contents remap (rule 9),
// and the collision / bounds part of rule 7.
//
// Data model, as observed in R1 and R2 files and in bsp_regen/src/main.cpp
// (addPropsToCmGrid, GPL-3.0):
//   Grid (0x55)        one 28 byte record: scale, cell offset, cell count,
//                      straddle group count, first brush plane.
//   Cells (0x56)       count.x * count.y world cells (index = y * count.x + x),
//                      followed by one cell per BSP model; each cell is a
//                      range [first, first+num) of GeoSets.
//   GeoSets (0x57)     {straddle group, numPrimitives, primitive word}.
//                      numPrimitives == 1: the primitive word is the primitive
//                      itself (type:8 | index:16 | uniqueContents:8).
//                      otherwise: word = (index << 8) | uniqueContents where
//                      index points at a range of numPrimitives entries in the
//                      primitive pool (0x59), type 0, and the unique contents
//                      value is the OR of the children's (verified on R1 and R2).
//   Primitives (0x59)  pool of primitive words, ranges are shared between
//                      geoset copies of the same object in different cells.
//   Bounds (0x58,0x5A) 16 byte oriented boxes, parallel to GeoSets / Primitives.
//   Unique contents (0x5B) table of contents flags, indexed by an 8 bit field.
// Primitive type 0 = brush, 0x40 = tricoll header, 0x60 = static prop (R2 only
// in the sense that R1 maps do not contain it; bsp_regen adds them to R1->R2).
#include "internal.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace bspdown {
namespace detail {

void boundsToAabb(const Bounds16& b, int lo[3], int hi[3]) {
    double c = 1.0, s = 0.0;
    if (!(b.negCos == 0 && b.sin == 0)) {
        c = -(double)b.negCos / 32768.0;
        s = (double)b.sin / 32768.0;
    }
    double wx = std::fabs(c) * b.extents[0] + std::fabs(s) * b.extents[1];
    double wy = std::fabs(s) * b.extents[0] + std::fabs(c) * b.extents[1];
    double wz = b.extents[2];
    lo[0] = (int)std::floor(b.origin[0] - wx);
    hi[0] = (int)std::ceil(b.origin[0] + wx);
    lo[1] = (int)std::floor(b.origin[1] - wy);
    hi[1] = (int)std::ceil(b.origin[1] + wy);
    lo[2] = (int)std::floor(b.origin[2] - wz);
    hi[2] = (int)std::ceil(b.origin[2] + wz);
}

Bounds16 boundsFromAabb(const int lo[3], const int hi[3]) {
    Bounds16 b;
    for (int a = 0; a < 3; a++) {
        int o = (int)std::floor((lo[a] + hi[a]) / 2.0);
        int e = std::max(hi[a] - o, o - lo[a]);
        b.origin[a] = (int16_t)std::max(-32768, std::min(32767, o));
        b.extents[a] = (int16_t)std::max(0, std::min(32767, e));
    }
    b.negCos = -32768;
    b.sin = 0;
    return b;
}

namespace {

bool outsideBox(const int lo[3], const int hi[3], int L) {
    for (int a = 0; a < 3; a++)
        if (lo[a] > L || hi[a] < -L) return true;
    return false;
}

bool exceeds(const int lo[3], const int hi[3], int L) {
    for (int a = 0; a < 3; a++)
        if (lo[a] < -L || hi[a] > L) return true;
    return false;
}

void clampInto(int lo[3], int hi[3], int L) {
    for (int a = 0; a < 3; a++) {
        lo[a] = std::max(lo[a], -L);
        hi[a] = std::min(hi[a], L);
    }
}

}  // namespace

// Contents remap. R2 has no SOLID bit on world collision; bsp_regen's forward
// (R1 -> R2) table for static props is
//   SOLID  (1): contents = (contents & ~0x1)        | 0xEB0280
//   WINDOW (2): contents = (contents & ~0x80002)    | 0xE30240
//   GRATE  (8): contents = (contents & ~0x480008)   | 0xA30240
// The signatures below are the inverse: when all signature bits are present,
// replace them by the R1 flag.
uint32_t remapContentsValue(uint32_t c) {
    const uint32_t SOLID_SIG = 0xEB0280, WINDOW_SIG = 0xE30240, GRATE_SIG = 0xA30240;
    if ((c & SOLID_SIG) == SOLID_SIG) return (c & ~SOLID_SIG) | 0x1;
    if ((c & WINDOW_SIG) == WINDOW_SIG) return (c & ~WINDOW_SIG) | 0x2;
    if ((c & GRATE_SIG) == GRATE_SIG) return (c & ~GRATE_SIG) | 0x8;
    return c;
}

void convertCollisionGrid(Ctx& c) {
    const int L = (int)std::floor(c.opt.clipLimit);
    std::vector<GeoSet> gs = view<GeoSet>(c.lump[L_CMGEOSETS], "cm geosets (0x57)");
    std::vector<Bounds16> gb = view<Bounds16>(c.lump[L_CMGEOBOUNDS], "cm geoset bounds (0x58)");
    std::vector<uint32_t> prims = view<uint32_t>(c.lump[L_CMPRIMS], "cm primitives (0x59)");
    std::vector<Bounds16> pb = view<Bounds16>(c.lump[L_CMPRIMBOUNDS], "cm primitive bounds (0x5A)");
    std::vector<uint32_t> uc = view<uint32_t>(c.lump[L_CMUNIQUE], "cm unique contents (0x5B)");
    std::vector<GridCell> cells = view<GridCell>(c.lump[L_CMCELLS], "cm grid cells (0x56)");
    if (c.lump[L_CMGRID].size() != sizeof(Grid)) throw Error("cm grid (0x55): size is " + std::to_string(c.lump[L_CMGRID].size()) + ", expected 28");
    Grid grid;
    std::memcpy(&grid, c.lump[L_CMGRID].data(), sizeof(grid));
    if (gs.size() != gb.size()) throw Error("cm geosets (0x57) and bounds (0x58) differ in count");
    if (prims.size() != pb.size()) throw Error("cm primitives (0x59) and bounds (0x5A) differ in count");
    if (uc.size() > 256) throw Error("cm unique contents (0x5B): more than 256 entries");

    const size_t nModels = c.lump[L_MODELS].size() / sizeof(Model);
    const int64_t worldCells = (int64_t)grid.numCells[0] * grid.numCells[1];
    if ((int64_t)cells.size() != worldCells + (int64_t)nModels)
        throw Error("cm grid cells (0x56): " + std::to_string(cells.size()) + " cells, expected " + std::to_string(worldCells) + " world cells + " + std::to_string(nModels) + " model cells");
    // R1 loader check: sizeof(cells) == 4 * (numModels + numCells.x * numCells.y)

    // ---- primitive pool: drop props and primitives entirely outside the limit
    std::vector<uint8_t> pkeep(prims.size(), 1);
    size_t propPrims = 0, outsidePrims = 0, clampedPrims = 0;
    uint64_t typeCount[256] = {};
    for (size_t i = 0; i < prims.size(); i++) {
        uint32_t type = prims[i] >> 24;
        typeCount[type]++;
        int lo[3], hi[3];
        boundsToAabb(pb[i], lo, hi);
        if (type == 0x60) {
            pkeep[i] = 0;
            propPrims++;
        } else if (outsideBox(lo, hi, L)) {
            pkeep[i] = 0;
            outsidePrims++;
        } else if (exceeds(lo, hi, L)) {
            clampInto(lo, hi, L);
            pb[i] = boundsFromAabb(lo, hi);
            clampedPrims++;
        }
        if (uc.size() && (prims[i] & 0xFF) >= uc.size()) throw Error("cm primitive " + std::to_string(i) + ": unique contents index " + std::to_string(prims[i] & 0xFF) + " out of range");
    }
    std::vector<uint32_t> pPre(prims.size() + 1, 0);
    for (size_t i = 0; i < prims.size(); i++) pPre[i + 1] = pPre[i] + pkeep[i];

    // ---- grid extents: drop cell rows/columns lying completely outside the limit
    const double sc = grid.scale;
    int minx = grid.numCells[0], maxx = -1, miny = grid.numCells[1], maxy = -1;
    size_t partialCells = 0;
    for (int y = 0; y < grid.numCells[1]; y++) {
        for (int x = 0; x < grid.numCells[0]; x++) {
            double x0 = (x + grid.cellOffset[0]) * sc, x1 = x0 + sc, y0 = (y + grid.cellOffset[1]) * sc, y1 = y0 + sc;
            bool out = x1 <= -L || x0 >= L || y1 <= -L || y0 >= L;
            if (out) continue;
            minx = std::min(minx, x);
            maxx = std::max(maxx, x);
            miny = std::min(miny, y);
            maxy = std::max(maxy, y);
            if (x0 < -L || x1 > L || y0 < -L || y1 > L) partialCells++;
        }
    }
    if (maxx < 0) throw Error("cm grid: no cell lies inside the clip limit");
    const int newNx = maxx - minx + 1, newNy = maxy - miny + 1;
    const bool trimmed = newNx != grid.numCells[0] || newNy != grid.numCells[1];
    size_t trimmedCells = (size_t)(worldCells - (int64_t)newNx * newNy);
    size_t trimmedGeosets = 0;
    // ---- geosets
    // geosets referenced only by trimmed cells are dropped with them
    std::vector<uint32_t> refd(gs.size(), 0);
    for (size_t ci = 0; ci < cells.size(); ci++) {
        if (ci < (size_t)worldCells) {
            int x = (int)(ci % grid.numCells[0]), y = (int)(ci / grid.numCells[0]);
            if (x < minx || x > maxx || y < miny || y > maxy) continue;
        }
        uint32_t f = cells[ci].firstGeoSet, n = cells[ci].numGeoSets;
        if ((uint64_t)f + n > gs.size()) throw Error("cm grid cell " + std::to_string(ci) + ": geoset range [" + std::to_string(f) + ", +" + std::to_string(n) + ") exceeds the geoset lump (" + std::to_string(gs.size()) + ")");
        for (uint32_t k = f; k < f + n; k++) refd[k]++;
    }
    std::vector<GeoSet> ngs;
    std::vector<Bounds16> ngb;
    std::vector<uint32_t> gPre(gs.size() + 1, 0);
    size_t gsProp = 0, gsOutside = 0, gsEmptied = 0, gsReduced = 0, gsToSingle = 0, gsClamped = 0, gsZero = 0;
    for (size_t gi = 0; gi < gs.size(); gi++) {
        GeoSet g = gs[gi];
        Bounds16 b = gb[gi];
        bool keep = refd[gi] != 0;
        if (!keep) trimmedGeosets++;
        if (g.numPrimitives == 0) {
            keep = false;
            gsZero++;
        } else if (g.numPrimitives == 1) {
            uint32_t type = g.primitive >> 24;
            if (type == 0x60) {
                keep = false;
                gsProp++;
            }
            if ((g.primitive & 0xFF) >= uc.size() && uc.size()) throw Error("cm geoset " + std::to_string(gi) + ": unique contents index out of range");
        } else {
            uint32_t idx = (g.primitive >> 8) & 0xFFFF;
            uint32_t n = g.numPrimitives;
            if ((uint64_t)idx + n > prims.size()) throw Error("cm geoset " + std::to_string(gi) + ": primitive range [" + std::to_string(idx) + ", +" + std::to_string(n) + ") exceeds the pool (" + std::to_string(prims.size()) + ")");
            uint32_t kept = pPre[idx + n] - pPre[idx];
            if (kept == 0) {
                keep = false;
                gsEmptied++;
            } else if (kept != n) {
                uint32_t first = 0, ucv = 0;
                int ulo[3] = {INT32_MAX, INT32_MAX, INT32_MAX}, uhi[3] = {INT32_MIN, INT32_MIN, INT32_MIN};
                bool gotFirst = false;
                for (uint32_t k = idx; k < idx + n; k++) {
                    if (!pkeep[k]) continue;
                    if (!gotFirst) {
                        first = k;
                        gotFirst = true;
                    }
                    ucv |= uc[prims[k] & 0xFF];
                    int lo[3], hi[3];
                    boundsToAabb(pb[k], lo, hi);
                    for (int a = 0; a < 3; a++) {
                        ulo[a] = std::min(ulo[a], lo[a]);
                        uhi[a] = std::max(uhi[a], hi[a]);
                    }
                }
                if (kept == 1) {
                    g.primitive = prims[first];
                    g.numPrimitives = 1;
                    b = pb[first];
                    gsToSingle++;
                } else {
                    size_t ui = std::find(uc.begin(), uc.end(), ucv) - uc.begin();
                    if (ui == uc.size()) {
                        if (uc.size() >= 256) {
                            c.fail("cm geoset %zu: reduced contents %#x needs a new unique-contents entry but the table already has 256", gi, ucv);
                            ui = 0;
                        } else {
                            uc.push_back(ucv);
                        }
                    }
                    g.primitive = (pPre[idx] << 8) | (uint32_t)ui;
                    g.numPrimitives = (uint16_t)kept;
                    b = boundsFromAabb(ulo, uhi);
                    gsReduced++;
                }
            } else {
                g.primitive = (pPre[idx] << 8) | (g.primitive & 0xFF);
            }
        }
        if (keep) {
            int lo[3], hi[3];
            boundsToAabb(b, lo, hi);
            if (outsideBox(lo, hi, L)) {
                keep = false;
                gsOutside++;
            } else if (exceeds(lo, hi, L)) {
                clampInto(lo, hi, L);
                b = boundsFromAabb(lo, hi);
                gsClamped++;
            }
        }
        gPre[gi + 1] = gPre[gi] + (keep ? 1 : 0);
        if (keep) {
            ngs.push_back(g);
            ngb.push_back(b);
        }
    }
    c.throwIfFailed("collision grid (rule 4)");
    if (ngs.size() > 0xFFFF) throw Error("cm geosets: " + std::to_string(ngs.size()) + " after conversion, cell ranges hold 16 bit indices");

    // ---- straddle groups: dense renumbering in pool order, 0 = none
    std::map<uint16_t, uint16_t> sgMap;
    std::set<uint16_t> oldIds;
    for (auto& g : gs)
        if (g.straddleGroup) oldIds.insert(g.straddleGroup);
    for (auto& g : ngs) {
        if (!g.straddleGroup) continue;
        auto it = sgMap.find(g.straddleGroup);
        if (it == sgMap.end()) it = sgMap.emplace(g.straddleGroup, (uint16_t)(sgMap.size() + 1)).first;
        g.straddleGroup = it->second;
    }
    int32_t newStraddle = (int32_t)sgMap.size() + 1;

    // ---- cells
    std::vector<GridCell> ncells(cells.size());
    for (size_t i = 0; i < cells.size(); i++) {
        uint32_t f = cells[i].firstGeoSet, n = cells[i].numGeoSets;
        if ((uint64_t)f + n > gs.size()) throw Error("cm grid cell " + std::to_string(i) + ": geoset range [" + std::to_string(f) + ", +" + std::to_string(n) + ") exceeds the geoset lump (" + std::to_string(gs.size()) + ")");
        ncells[i].firstGeoSet = (uint16_t)gPre[f];
        ncells[i].numGeoSets = (uint16_t)(gPre[f + n] - gPre[f]);
    }

    if (trimmed) {
        std::vector<GridCell> trimmedArr;
        trimmedArr.reserve((size_t)newNx * newNy + nModels);
        for (int y = miny; y <= maxy; y++)
            for (int x = minx; x <= maxx; x++) trimmedArr.push_back(ncells[(size_t)y * grid.numCells[0] + x]);
        for (size_t m = 0; m < nModels; m++) trimmedArr.push_back(ncells[(size_t)worldCells + m]);
        ncells.swap(trimmedArr);
        grid.cellOffset[0] += minx;
        grid.cellOffset[1] += miny;
        grid.numCells[0] = newNx;
        grid.numCells[1] = newNy;
    }
    const int32_t oldStraddleCount = grid.numStraddleGroups;
    grid.numStraddleGroups = newStraddle;

    // ---- pool output
    std::vector<uint32_t> nprims;
    std::vector<Bounds16> npb;
    for (size_t i = 0; i < prims.size(); i++)
        if (pkeep[i]) {
            nprims.push_back(prims[i]);
            npb.push_back(pb[i]);
        }

    // ---- contents remap
    size_t remapped = 0;
    std::map<uint32_t, uint32_t> remapTable;
    if (c.opt.remapContents) {
        for (auto& u : uc) {
            uint32_t r = remapContentsValue(u);
            if (r != u) {
                remapped++;
                remapTable[u] = r;
                u = r;
            }
        }
    }

    c.lump[L_CMGEOSETS] = pack(ngs);
    c.lump[L_CMGEOBOUNDS] = pack(ngb);
    c.lump[L_CMPRIMS] = pack(nprims);
    c.lump[L_CMPRIMBOUNDS] = pack(npb);
    c.lump[L_CMUNIQUE] = pack(uc);
    c.lump[L_CMCELLS] = pack(ncells);
    c.lump[L_CMGRID] = Bytes((const uint8_t*)&grid, (const uint8_t*)&grid + sizeof(grid));

    c.say("[rule 4] collision grid: scale %g (unchanged; R1 stock maps use 256), offset (%d,%d) count (%d,%d), %zu cells (%lld world + %zu model)",
          (double)grid.scale, grid.cellOffset[0], grid.cellOffset[1], grid.numCells[0], grid.numCells[1], ncells.size(), (long long)((int64_t)grid.numCells[0] * grid.numCells[1]), nModels);
    c.say("[rule 4] primitives: %zu -> %zu (type 0x00 brush %llu, 0x40 tricoll %llu, 0x60 prop %llu in input); removed %zu prop primitives, %zu primitives outside +-%d, clamped bounds of %zu",
          prims.size(), nprims.size(), (unsigned long long)typeCount[0], (unsigned long long)typeCount[0x40], (unsigned long long)typeCount[0x60], propPrims, outsidePrims, L, clampedPrims);
    c.say("[rule 4] geosets: %zu -> %zu; removed %zu single-prop geosets, %zu prop-only multi geosets, %zu outside, %zu malformed (0 primitives); %zu multi geosets lost children (%zu became single, %zu re-OR'd unique contents), %zu bounds clamped",
          gs.size(), ngs.size(), gsProp, gsEmptied, gsOutside, gsZero, gsReduced + gsToSingle, gsToSingle, gsReduced, gsClamped);
    c.say("[rule 4] straddle groups: %zu distinct ids -> %zu, Grid.num_straddle_groups %d -> %d", oldIds.size(), sgMap.size(), oldStraddleCount, newStraddle);
    c.say("[rule 4] unique contents: %zu entries; first_brush_plane %d unchanged", uc.size(), grid.firstBrushPlane);
    if (trimmed)
        c.say("[rule 7] collision grid trimmed: %zu cells entirely outside +-%d removed (%zu geosets only they referenced dropped), new extent x [%g, %g) y [%g, %g)", trimmedCells, L,
              trimmedGeosets, grid.cellOffset[0] * sc, (grid.cellOffset[0] + grid.numCells[0]) * sc, grid.cellOffset[1] * sc, (grid.cellOffset[1] + grid.numCells[1]) * sc);
    else
        c.say("[rule 7] collision grid not trimmed: extent x [%g, %g) y [%g, %g)", grid.cellOffset[0] * sc, (grid.cellOffset[0] + grid.numCells[0]) * sc,
              grid.cellOffset[1] * sc, (grid.cellOffset[1] + grid.numCells[1]) * sc);
    if (partialCells)
        c.say("[rule 7] note: %zu grid cells straddle +-%d and are kept whole (cell size %g cannot be cut)", partialCells, L, sc);
    if (c.opt.remapContents) {
        std::string t;
        for (auto& kv : remapTable) {
            char b[48];
            std::snprintf(b, sizeof(b), "%s%#x->%#x", t.empty() ? "" : " ", kv.first, kv.second);
            t += b;
        }
        c.say("[rule 9] unique contents remap R2 -> R1: %zu of %zu entries changed: %s", remapped, uc.size(), t.empty() ? "(none)" : t.c_str());
        std::string un;
        for (uint32_t u : uc) {
            // Values that still look like R2 patterns after the remap.
            if ((u & 0x800000) || (u & 0x280) == 0x280) {
                char b[24];
                std::snprintf(b, sizeof(b), "%s%#x", un.empty() ? "" : " ", u);
                un += b;
            }
        }
        if (!un.empty()) c.say("[rule 9] values still carrying R2-style bits (0x800000 or 0x280) after remap, left as is: %s", un.c_str());
    } else {
        c.say("[rule 9] contents remap disabled");
    }

    // brushes beyond the limit are reported, not modified
    {
        const Bytes& bl = c.lump[L_CMBRUSHES];
        size_t n = bl.size() / 32, bad = 0;
        for (size_t i = 0; i < n; i++) {
            float f[6];
            // Brush: origin(3f) numNonAxial(u8) numPlaneOffsets(u8) index(i16) extents(3f) sideOffset(i32)
            std::memcpy(&f[0], &bl[i * 32], 12);
            std::memcpy(&f[3], &bl[i * 32 + 16], 12);
            for (int a = 0; a < 3; a++)
                if (f[a] - f[3 + a] < -L || f[a] + f[3 + a] > L) {
                    bad++;
                    break;
                }
        }
        c.say("[rule 7] brushes (0x5C): %zu of %zu extend beyond +-%d (not modified; clipping is only done on render meshes and bounds)", bad, n, L);
    }
}

namespace {

// Count float AABB records (mins at minOff, maxs at maxOff, 3 floats each) that
// reach beyond +-L, including infinite bounds. Stock R1 maps contain such
// records (cell AABB roots with +-inf, shadow cascade boxes beyond 16376), so
// they are reported but not modified.
size_t countBeyond(const Bytes& b, size_t stride, size_t minOff, size_t maxOff, float L) {
    size_t n = b.size() / stride, beyond = 0;
    for (size_t i = 0; i < n; i++) {
        bool ex = false;
        for (int a = 0; a < 3; a++) {
            float mn, mx;
            std::memcpy(&mn, &b[i * stride + minOff + 4 * a], 4);
            std::memcpy(&mx, &b[i * stride + maxOff + 4 * a], 4);
            if (mn < -L || mx > L) ex = true;
        }
        beyond += ex;
    }
    return beyond;
}

float maxAbsVec3(const Bytes& b, size_t stride, size_t off, size_t count3) {
    float m = 0;
    size_t n = b.size() / stride;
    for (size_t i = 0; i < n; i++)
        for (size_t k = 0; k < count3; k++) {
            float f[3];
            std::memcpy(f, &b[i * stride + off + 12 * k], 12);
            for (int a = 0; a < 3; a++) m = std::max(m, std::fabs(f[a]));
        }
    return m;
}

}  // namespace

void reportResidualBounds(Ctx& c) {
    const float L = c.opt.clipLimit;
    size_t a = countBeyond(c.lump[L_CELLAABB], 32, 0, 16, L);
    size_t b = countBeyond(c.lump[L_CSMAABB], 32, 0, 16, L);
    size_t d = countBeyond(c.lump[L_OBJREFBOUNDS], 32, 0, 16, L);
    c.say("[rule 7] float AABB records reaching beyond +-%g, not modified (stock R1 maps have such records too: +-inf cell roots, shadow boxes): cell aabb nodes (0x77) %zu of %zu, csm aabb nodes (0x63) %zu of %zu, obj ref bounds (0x79) %zu of %zu",
          (double)L, a, c.lump[L_CELLAABB].size() / 32, b, c.lump[L_CSMAABB].size() / 32, d, c.lump[L_OBJREFBOUNDS].size() / 32);
    // residual extents of position lumps that are not rewritten
    c.say("[rule 7] residual max |coordinate| in lumps that are not rewritten: portal verts (0x6D) %.0f, occluder verts (0x75) %.0f, shadow opaque verts (0x7C) %.0f, shadow alpha verts (0x7D) %.0f, vertices (0x03, incl. unreferenced originals) %.0f",
          maxAbsVec3(c.lump[L_PORTALVERTS], 12, 0, 1), maxAbsVec3(c.lump[L_OCCVERTS], 12, 0, 1), maxAbsVec3(c.lump[L_SHADOWOPAQUE], 12, 0, 1),
          maxAbsVec3(c.lump[L_SHADOWALPHA], 20, 0, 1), maxAbsVec3(c.lump[L_VERTICES], 12, 0, 1));
}

}  // namespace detail
}  // namespace bspdown
