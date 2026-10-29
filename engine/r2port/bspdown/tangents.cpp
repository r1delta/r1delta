// Origin: C:\Users\r3muxd\tmp\r2port\bspdown. Bundled in r1delta under GPL-3.0.
// Rule 11: tangent frame of lit-bump (0x49) and unlit-TS (0x4A) vertices, and index validation
// of the vertex lumps 0x47..0x4B.
//
// R1 (dvertLitBump / dvertUnlitTS): tangentS and tangentT are both plain indices into
// VERTEX_NORMALS (0x1E). The R1 engine loader (sub_1800E1E80 in the R1O engine) does not
// bounds check them.
// R2: the first field is (index of tangent S | 0x80000000 when the UV mapping is mirrored), the
// second field is always 0. Measured by the lead on mp_lf_stacks: flag 0 <-> handedness +1,
// flag 1 <-> -1, and T = handedness * cross(normal, S).
//
// Conversion per vertex: s = field0 & 0x7FFFFFFF, h = field0 >> 31 ? -1 : +1,
// t = normalize(h * cross(normal, s)); t is appended to 0x1E (deduplicated on exact float
// bits, existing entries and indices untouched); field0 = s, field1 = index of t.
#include "internal.hpp"

#include <algorithm>
#include <map>
#include <tuple>

namespace bspdown {
namespace detail {

namespace {
struct Lumpdesc {
    uint32_t lump;
    unsigned stride;
    int tangentOffset;  // byte offset of tangentS, -1 if none
    const char* name;
};
const Lumpdesc kV[] = {
    {L_VTX_UNLIT, 20, -1, "unlit vertices (0x47)"},
    {L_VTX_LITFLAT, 36, -1, "lit flat vertices (0x48)"},
    {L_VTX_LITBUMP, 44, 36, "lit bump vertices (0x49)"},
    {L_VTX_UNLITTS, 28, 20, "unlit TS vertices (0x4A)"},
    {L_VTX_BLINN, 28, -1, "blinn-phong vertices (0x4B)"},
};
uint32_t ld(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
void st(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }
}  // namespace

void convertVertexFrames(Ctx& c) {
    Bytes& nb = c.lump[L_VNORMALS];
    if (nb.size() % 12) throw Error("vertex normals (0x1E): size is not a multiple of 12");
    const size_t nOrig = nb.size() / 12;
    const size_t nPos = c.lump[L_VERTICES].size() / 12;
    std::vector<Vec3> nrm(nOrig);
    if (nOrig) std::memcpy(nrm.data(), nb.data(), nb.size());

    std::map<std::tuple<uint32_t, uint32_t, uint32_t>, uint32_t> dedup;
    std::vector<Vec3> added;
    size_t total = 0, degenerate = 0, flagged = 0, notPerp = 0;
    double worstDot = 0;

    // Pass 1: validate position and normal indices in every vertex lump, convert tangents.
    for (const Lumpdesc& d : kV) {
        Bytes& b = c.lump[d.lump];
        if (b.size() % d.stride) throw Error(std::string(d.name) + ": size is not a multiple of " + std::to_string(d.stride));
        size_t n = b.size() / d.stride;
        for (size_t i = 0; i < n; i++) {
            uint8_t* r = &b[i * d.stride];
            uint32_t pi = ld(r), ni = ld(r + 4);
            if (pi >= nPos) c.fail("%s record %zu: position index %u >= %zu positions (0x03)", d.name, i, pi, nPos);
            if (ni >= nOrig + added.size()) {
                c.fail("%s record %zu: normal index %u >= %zu normals (0x1E)", d.name, i, ni, nOrig + added.size());
                continue;
            }
            if (c.failures.size() > 64) c.throwIfFailed("vertex lump validation (rule 11)");
            if (d.tangentOffset < 0) continue;
            total++;
            uint32_t f0 = ld(r + d.tangentOffset), f1 = ld(r + d.tangentOffset + 4);
            if (f1 != 0) {
                c.fail("%s record %zu: second tangent field is %#x, expected 0 in R2 data", d.name, i, f1);
                continue;
            }
            uint32_t si = f0 & 0x7FFFFFFFu;
            double h = (f0 >> 31) ? -1.0 : 1.0;
            flagged += (f0 >> 31);
            if (si >= nOrig + added.size()) {
                c.fail("%s record %zu: tangent S index %u >= %zu normals (0x1E)", d.name, i, si, nOrig + added.size());
                continue;
            }
            Vec3 nn = (ni < nOrig) ? nrm[ni] : added[ni - nOrig];
            Vec3 s = (si < nOrig) ? nrm[si] : added[si - nOrig];
            double cx = (double)nn.y * s.z - (double)nn.z * s.y;
            double cy = (double)nn.z * s.x - (double)nn.x * s.z;
            double cz = (double)nn.x * s.y - (double)nn.y * s.x;
            double len = std::sqrt(cx * cx + cy * cy + cz * cz);
            double dns = nn.x * (double)s.x + nn.y * (double)s.y + nn.z * (double)s.z;
            worstDot = std::max(worstDot, std::fabs(dns));
            if (std::fabs(dns) > 0.6) notPerp++;
            Vec3 t;
            if (len < 1e-3) {
                // Degenerate S (parallel to the normal): pick any unit vector perpendicular to the normal.
                degenerate++;
                double ax = std::fabs(nn.x) < 0.9 ? 1 : 0, ay = ax ? 0 : 1;
                double px = nn.y * 0 - nn.z * ay, py = nn.z * ax - nn.x * 0, pz = nn.x * ay - nn.y * ax;
                double pl = std::sqrt(px * px + py * py + pz * pz);
                t = Vec3{(float)(h * px / pl), (float)(h * py / pl), (float)(h * pz / pl)};
            } else {
                t = Vec3{(float)(h * cx / len), (float)(h * cy / len), (float)(h * cz / len)};
            }
            uint32_t bits[3];
            std::memcpy(bits, &t, 12);
            auto key = std::make_tuple(bits[0], bits[1], bits[2]);
            auto it = dedup.find(key);
            uint32_t ti;
            if (it == dedup.end()) {
                ti = (uint32_t)(nOrig + added.size());
                added.push_back(t);
                dedup.emplace(key, ti);
            } else {
                ti = it->second;
            }
            st(r + d.tangentOffset, si);
            st(r + d.tangentOffset + 4, ti);
        }
    }
    c.throwIfFailed("vertex lump validation (rule 11)");
    if (nOrig + added.size() > 0x7FFFFFFFull) throw Error("vertex normals exceed 2^31 entries");

    // Pass 2: final range check of everything that references 0x1E, against the grown lump.
    const size_t nNew = nOrig + added.size();
    for (const Lumpdesc& d : kV) {
        const Bytes& b = c.lump[d.lump];
        size_t n = b.size() / d.stride;
        for (size_t i = 0; i < n; i++) {
            const uint8_t* r = &b[i * d.stride];
            if (ld(r) >= nPos || ld(r + 4) >= nNew) c.fail("%s record %zu: position/normal index out of range after conversion", d.name, i);
            if (d.tangentOffset >= 0 && (ld(r + d.tangentOffset) >= nNew || ld(r + d.tangentOffset + 4) >= nNew))
                c.fail("%s record %zu: tangent index out of range after conversion", d.name, i);
        }
    }
    c.throwIfFailed("vertex lump validation (rule 11)");

    if (!added.empty()) {
        size_t o = nb.size();
        nb.resize(o + added.size() * 12);
        std::memcpy(&nb[o], added.data(), added.size() * 12);
    }
    c.say("[rule 11] vertex frames: %zu lit-bump/unlit-TS vertices converted (R2 flag|S, 0) -> (S, T index); %zu mirrored (flag set); T = handedness * cross(normal, S), normalised, appended to 0x1E deduplicated: normals %zu -> %zu (+%zu)",
          total, flagged, nOrig, nNew, added.size());
    c.say("[rule 11] %zu vertices had S nearly parallel to the normal (replaced by an arbitrary perpendicular); max |dot(normal, S)| in the data %.3f (%zu above 0.6); all position/normal/tangent indices of lumps 0x47..0x4B validated against 0x03 (%zu) and 0x1E",
          degenerate, worstDot, notPerp, nPos);
}

}  // namespace detail
}  // namespace bspdown
