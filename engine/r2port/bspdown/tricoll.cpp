// Origin: C:\Users\r3muxd\tmp\r2port\bspdown. Bundled in r1delta under GPL-3.0.
// Rule 3: tricoll bevel stream, R2 (11 bit entries) -> R1 (10 bit entries).
//
// This is the inverse of convertTricoll() in bsp_regen/src/tricoll.hpp (GPL-3.0,
// the BitReader / write11Bit helpers there are the model for the bit layout).
//
// Stream layout, per tricoll header (all little endian, entries are packed LSB
// first into 32 bit words):
//   * the header's stream holds num_bevel_indices entries;
//   * triangle k of the header has a start entry (lump 0x60) and a 4 bit bevel
//     count in bits 24..27 of its triangle word (lump 0x42);
//   * count 0..14: that many entries follow at the start entry;
//   * count 15: an "extended" run. Two entries hold a 20 bit value
//     data = e0 | (e1 << 11) in R2 (data = r0 | (r1 << 10) in R1), with
//     count = data & 0x7F and header = data >> 7, followed by `count` entries.
//     Runs repeat until header == this header's index or count == 0.
//   * the stream of header i occupies (n*bits + 31) / 32 words, headers are
//     stored back to back (first_bevel_index counts words).
// R1 limits that make some R2 input unconvertible: entries are 10 bits (<1024),
// a triangle index inside a header is 10 bits (<=1024 triangles per header),
// and the extended header index is 13 bits (<8192).
#include "internal.hpp"

#include <algorithm>

namespace bspdown {
namespace detail {

namespace {

struct Reader11 {
    const uint32_t* w;
    size_t words;
    // entry index relative to the stream start; caller guarantees the range.
    uint32_t get(size_t entry) const {
        size_t bit = entry * 11;
        size_t i = bit >> 5;
        unsigned o = (unsigned)(bit & 31);
        uint64_t v = w[i];
        if (o + 11 > 32) v |= (uint64_t)w[i + 1] << 32;
        return (uint32_t)((v >> o) & 0x7FF);
    }
};

inline void put10(uint32_t* w, size_t entry, uint32_t value) {
    size_t bit = entry * 10;
    size_t i = bit >> 5;
    unsigned o = (unsigned)(bit & 31);
    uint64_t v = w[i];
    if (o + 10 > 32) v |= (uint64_t)w[i + 1] << 32;
    v &= ~((uint64_t)0x3FF << o);
    v |= (uint64_t)(value & 0x3FF) << o;
    w[i] = (uint32_t)v;
    if (o + 10 > 32) w[i + 1] = (uint32_t)(v >> 32);
}

}  // namespace

void convertTricoll(Ctx& c) {
    std::vector<TricollHeader> hdr = view<TricollHeader>(c.lump[L_TRICOLL_HDR], "tricoll headers (0x45)");
    const Bytes& triB = c.lump[L_TRICOLL_TRIS];
    if (triB.size() % 4) throw Error("tricoll triangles (0x42): size is not a multiple of 4");
    std::vector<uint32_t> tris = view<uint32_t>(triB, "tricoll triangles (0x42)");
    std::vector<uint16_t> starts = view<uint16_t>(c.lump[L_BEVSTARTS], "tricoll bevel starts (0x60)");
    std::vector<uint32_t> inW = view<uint32_t>(c.lump[L_BEVINDICES], "tricoll bevel indices (0x61)");
    const Bytes& nodes = c.lump[L_TRICOLL_NODES];
    if (nodes.size() % 16) throw Error("tricoll nodes (0x44): size is not a multiple of 16");
    size_t nodeCount = nodes.size() / 16;
    if (starts.size() != tris.size())
        throw Error("tricoll bevel starts (0x60) has " + std::to_string(starts.size()) + " entries but triangles (0x42) has " + std::to_string(tris.size()));

    std::vector<uint32_t> outW;
    outW.reserve(inW.size());
    uint64_t entriesTotal = 0, entriesVisited = 0, extendedRuns = 0, maxEntry = 0, maxTris = 0, maxHeaderRef = 0;
    uint64_t nodesNeeded = 0;
    size_t nOverTris = 0, nBadNode = 0, extOverflow = 0, extOverflowHeaders = 0, lastOverflowHeader = SIZE_MAX;
    uint32_t firstOverflowHeader = 0, firstOverflowRef = 0;
    std::vector<std::pair<uint16_t, uint16_t>> st;
    std::vector<uint8_t> seen;
    for (size_t i = 0; i < hdr.size(); i++) {
        TricollHeader h = hdr[i];
        const uint32_t n = h.numBevelIndices;
        const uint32_t nt = h.numTriangles;
        maxTris = std::max<uint64_t>(maxTris, nt);
        if (nt > 1024) {
            c.fail("tricoll header %zu has %u triangles, R1 allows at most 1024 per header", i, nt);
            nOverTris++;
        }
        if ((uint64_t)h.firstTriangle + nt > tris.size()) {
            c.fail("tricoll header %zu: triangles [%u, +%u) exceed the triangle lump (%zu)", i, h.firstTriangle, nt, tris.size());
            continue;
        }
        // node count formula from the R1 loader (CollisionBSPData_LoadTriColl)
        if (nt > 3) {
            uint64_t nn = (4ull * (((uint64_t)nt - ((nt + 3) % 6) + 5) / 2) - 4) / 3;
            nodesNeeded += nn;
            if ((uint64_t)h.firstNode + nn > nodeCount) {
                c.fail("tricoll header %zu: nodes [%u, +%llu) exceed the node lump (%zu)", i, h.firstNode, (unsigned long long)nn, nodeCount);
                nBadNode++;
            }
        }
        const uint32_t inFirst = h.firstBevelIndex;
        h.firstBevelIndex = (uint32_t)outW.size();
        hdr[i] = h;
        if (n == 0) continue;

        const size_t inWords = ((size_t)n * 11 + 31) / 32;
        if ((uint64_t)inFirst + inWords > inW.size()) {
            c.fail("tricoll header %zu: bevel stream [%u, +%zu words) exceeds lump 0x61 (%zu words)", i, inFirst, inWords, inW.size());
            continue;
        }
        Reader11 rd{&inW[inFirst], inWords};
        const size_t outWords = ((size_t)n * 10 + 31) / 32;
        const size_t base = outW.size();
        outW.resize(base + outWords, 0);
        uint32_t* ow = &outW[base];
        entriesTotal += n;
        seen.assign(n, 0);

        // distinct (start, count) pairs, count = max over triangles sharing a start
        st.clear();
        for (uint32_t k = 0; k < nt; k++) {
            uint16_t s = starts[h.firstTriangle + k];
            uint16_t cnt = (uint16_t)((tris[h.firstTriangle + k] >> 24) & 0xF);
            st.emplace_back(s, cnt);
        }
        std::sort(st.begin(), st.end());
        size_t u = 0;
        for (size_t k = 0; k < st.size(); k++) {
            if (u && st[u - 1].first == st[k].first) {
                st[u - 1].second = std::max(st[u - 1].second, st[k].second);
            } else {
                st[u++] = st[k];
            }
        }
        st.resize(u);

        for (auto& pr : st) {
            uint32_t p = pr.first;
            uint32_t cnt = pr.second;
            if (cnt == 15) {
                for (;;) {
                    if ((uint64_t)p + 2 > n) {
                        c.fail("tricoll header %zu: extended bevel record at entry %u runs past the %u entries of the header", i, p, n);
                        break;
                    }
                    uint32_t e0 = rd.get(p), e1 = rd.get(p + 1);
                    uint32_t data = e0 | (e1 << 11);
                    uint32_t rc = data & 0x7F;
                    uint32_t ridx = data >> 7;
                    extendedRuns++;
                    maxHeaderRef = std::max<uint64_t>(maxHeaderRef, ridx);
                    if (e1 >= 512) {
                        if (!extOverflow) {
                            firstOverflowHeader = (uint32_t)i;
                            firstOverflowRef = ridx;
                        }
                        extOverflow++;
                        if (lastOverflowHeader != i) {
                            lastOverflowHeader = i;
                            extOverflowHeaders++;
                        }
                        break;
                    }
                    put10(ow, p, data & 0x3FF);
                    put10(ow, p + 1, data >> 10);
                    seen[p] = seen[p + 1] = 1;
                    p += 2;
                    if ((uint64_t)p + rc > n) {
                        c.fail("tricoll header %zu: extended bevel run of %u entries at entry %u runs past the %u entries of the header", i, rc, p, n);
                        break;
                    }
                    for (uint32_t j = 0; j < rc; j++, p++) {
                        uint32_t v = rd.get(p);
                        maxEntry = std::max<uint64_t>(maxEntry, v);
                        if (v > 1023) {
                            c.fail("tricoll header %zu: bevel entry %u has value %u, which does not fit R1's 10 bit entries (max 1023)", i, p, v);
                            continue;
                        }
                        put10(ow, p, v);
                        seen[p] = 1;
                    }
                    if (ridx == i || rc == 0) break;
                }
            } else {
                if ((uint64_t)p + cnt > n) {
                    c.fail("tricoll header %zu: bevel run of %u entries at entry %u runs past the %u entries of the header", i, cnt, p, n);
                    continue;
                }
                for (uint32_t j = 0; j < cnt; j++, p++) {
                    uint32_t v = rd.get(p);
                    maxEntry = std::max<uint64_t>(maxEntry, v);
                    if (v > 1023) {
                        c.fail("tricoll header %zu: bevel entry %u has value %u, which does not fit R1's 10 bit entries (max 1023)", i, p, v);
                        continue;
                    }
                    put10(ow, p, v);
                    seen[p] = 1;
                }
            }
        }
        for (uint32_t k = 0; k < n; k++) entriesVisited += seen[k];
    }
    if (extOverflow)
        c.fail("%zu extended bevel records (in %zu of %zu headers) reference a header index that does not fit R1's 13 bit field (max 8191); first: header %u references index %u. R1 cannot address tricoll headers >= 8192 from a bevel record",
               extOverflow, extOverflowHeaders, hdr.size(), firstOverflowHeader, firstOverflowRef);
    c.throwIfFailed("tricoll (rule 3)");

    c.lump[L_TRICOLL_HDR] = pack(hdr);
    c.lump[L_BEVINDICES] = pack(outW);
    // lumps 0x42 (triangles), 0x44 (nodes), 0x60 (bevel starts) keep their format.
    c.say("[rule 3] tricoll: %zu headers (max %llu triangles each, limit 1024; max extended header ref %llu, limit 8191), %llu nodes (R1 formula needs %llu)",
          hdr.size(), (unsigned long long)maxTris, (unsigned long long)maxHeaderRef, (unsigned long long)nodeCount, (unsigned long long)nodesNeeded);
    c.say("[rule 3] tricoll bevel stream: %zu words (11 bit) -> %zu words (10 bit); %llu entries, %llu reachable from triangle starts, max entry value %llu (limit 1023), %llu extended runs; unreachable entries are zeroed",
          inW.size(), outW.size(), (unsigned long long)entriesTotal, (unsigned long long)entriesVisited, (unsigned long long)maxEntry, (unsigned long long)extendedRuns);
    c.say("[rule 3] node lump (0x44), triangle lump (0x42), bevel starts (0x60): same record format in R1 and R2 (16, 4, 2 bytes), copied unchanged");
}

}  // namespace detail
}  // namespace bspdown
