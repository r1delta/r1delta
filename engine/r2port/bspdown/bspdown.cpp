// Origin: C:\Users\r3muxd\tmp\r2port\bspdown. Bundled in r1delta under GPL-3.0.
// bspdown driver: parse the R2 file, run the conversion stages, write the R1 file.
// See README.txt for the rule list.
#include "internal.hpp"

#include <algorithm>

namespace bspdown {

void Report::add(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    lines.push_back(detail::vformat(fmt, ap));
    va_end(ap);
}

std::string Report::text() const {
    std::string s;
    for (const auto& l : lines) {
        s += l;
        s += '\n';
    }
    return s;
}

namespace detail {

std::string vformat(const char* fmt, va_list ap) {
    va_list ap2;
    va_copy(ap2, ap);
    int n = std::vsnprintf(nullptr, 0, fmt, ap2);
    va_end(ap2);
    std::string s(n > 0 ? (size_t)n : 0, '\0');
    if (n > 0) std::vsnprintf(&s[0], s.size() + 1, fmt, ap);
    return s;
}

void Ctx::say(const char* fmt, ...) {
    if (!rep) return;
    va_list ap;
    va_start(ap, fmt);
    rep->lines.push_back(vformat(fmt, ap));
    va_end(ap);
}

void Ctx::fail(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    failures.push_back(vformat(fmt, ap));
    va_end(ap);
}

void Ctx::throwIfFailed(const char* stage) {
    if (failures.empty()) return;
    std::string m = std::string(stage) + ": " + std::to_string(failures.size()) + " problem(s); first: ";
    for (size_t i = 0; i < failures.size() && i < 8; i++) {
        if (i) m += " | ";
        m += failures[i];
    }
    throw Error(m);
}

// ---------------------------------------------------------------------------
// Rule 2: real time lights, 9 bytes per texel in R2.
// R1 (Mod_LoadLightmapDataRealTimeLights) requires the size to be a whole
// number (1..4) of bytes per texel over all lightmap headers of type 1, and
// treats 0xFF as "no light". The R2 texel layout is not understood (the file
// in the VPK has 8 bytes per texel, the copy embedded in the BSP has 9), so the
// output carries no light assignments.
void convertRealTimeLights(Ctx& c) {
    const Bytes& hdr = c.lump[L_LMHDR];
    if (hdr.size() % 8) throw Error("lightmap headers (0x53): size is not a multiple of 8");
    uint64_t texels = 0;
    for (size_t i = 0; i < hdr.size() / 8; i++) {
        uint32_t type;
        uint16_t w, h;
        std::memcpy(&type, &hdr[i * 8], 4);
        std::memcpy(&w, &hdr[i * 8 + 4], 2);
        std::memcpy(&h, &hdr[i * 8 + 6], 2);
        if (type == 1) texels += (uint64_t)w * h;
    }
    Bytes& rtl = c.lump[L_LMRTL];
    size_t inLen = rtl.size();
    if (inLen == 0) {
        c.say("[rule 2] real-time lights: input lump empty, output empty (R1 treats an empty lump as no data)");
        return;
    }
    if (texels == 0) throw Error("real-time lights (0x69): lump present but no type-1 lightmap headers");
    int bpt = c.opt.rtlBytesPerTexel;
    if (bpt < 1 || bpt > 4) throw Error("rtlBytesPerTexel must be 1..4");
    c.say("[rule 2] real-time lights: %llu type-1 texels, input %zu bytes (%.2f per texel), output %llu bytes (%d per texel) filled with 0xFF",
          (unsigned long long)texels, inLen, (double)inLen / (double)texels, (unsigned long long)(texels * bpt), bpt);
    if (inLen != texels * 9)
        c.say("[rule 2] note: input size is not 9 bytes per texel (expected %llu)", (unsigned long long)(texels * 9));
    rtl.assign((size_t)(texels * bpt), 0xFF);
}

// Rule 6: light probe refs, 20 -> 16 bytes (drop the trailing int32).
void convertLightProbeRefs(Ctx& c) {
    Bytes& l = c.lump[L_LPREFS];
    if (l.size() % 20) throw Error("light probe refs (0x68): size " + std::to_string(l.size()) + " is not a multiple of 20 (R2 record size)");
    size_t n = l.size() / 20;
    Bytes out(n * 16);
    uint64_t nonzero = 0;
    for (size_t i = 0; i < n; i++) {
        std::memcpy(&out[i * 16], &l[i * 20], 16);
        int32_t unk;
        std::memcpy(&unk, &l[i * 20 + 16], 4);
        if (unk != 0) nonzero++;
    }
    l.swap(out);
    c.say("[rule 6] light probe refs (0x68): %zu records 20 -> 16 bytes; dropped trailing int32 (non-zero in %llu records)", n, (unsigned long long)nonzero);
    // The other probe lumps: R1 reads 0x65 as 48 byte records, 0x67 as 8 byte records, 0x66 as 4 byte indices.
    const struct { uint32_t lump; unsigned size; const char* name; } chk[] = {
        {L_LIGHTPROBES, 48, "light probes (0x65)"},
        {L_LPTREE, 8, "light probe tree (0x67)"},
        {L_SPLPINDICES, 4, "static prop probe indices (0x66)"},
    };
    for (auto& k : chk) {
        if (c.lump[k.lump].size() % k.size)
            throw Error(std::string(k.name) + ": size " + std::to_string(c.lump[k.lump].size()) + " is not a multiple of " + std::to_string(k.size));
    }
    c.say("[rule 6] 0x65 (%zu probes x48), 0x67 (%zu x8), 0x66 (%zu x4): record sizes match R1, copied unchanged",
          c.lump[L_LIGHTPROBES].size() / 48, c.lump[L_LPTREE].size() / 8, c.lump[L_SPLPINDICES].size() / 4);
}

// Rule 8a: world lights. R1 requires lump version 1 and 100 byte records
// (count = length / 100) and aborts the map above 4064 lights. R2 uses
// version 2 (108 bytes) or 3 (112 bytes); the first 100 bytes are the R1 record.
void convertWorldLights(Ctx& c) {
    Bytes& l = c.lump[L_WORLDLIGHTS];
    if (l.empty()) {
        c.version[L_WORLDLIGHTS] = 1;
        return;
    }
    size_t rec;
    switch (c.version[L_WORLDLIGHTS]) {
        case 1: rec = 100; break;
        case 2: rec = 108; break;
        case 3: rec = 112; break;
        default: throw Error("world lights (0x36): unknown lump version " + std::to_string(c.version[L_WORLDLIGHTS]));
    }
    if (l.size() % rec)
        throw Error("world lights (0x36): size " + std::to_string(l.size()) + " is not a multiple of " + std::to_string(rec) + " for lump version " + std::to_string(c.version[L_WORLDLIGHTS]));
    size_t n = l.size() / rec;
    if (n > c.opt.maxWorldLights)
        throw Error("world lights (0x36): " + std::to_string(n) + " lights, R1 aborts above " + std::to_string(c.opt.maxWorldLights));
    if (rec != 100) {
        Bytes out(n * 100);
        for (size_t i = 0; i < n; i++) std::memcpy(&out[i * 100], &l[i * rec], 100);
        l.swap(out);
    }
    if (c.opt.worldLightFlagBit2) {
        size_t set = 0;
        for (size_t i = 0; i < n; i++) {
            int32_t flags;
            std::memcpy(&flags, &l[i * 100 + 88], 4);
            if (flags != 0 && !(flags & 2)) {
                flags |= 2;
                std::memcpy(&l[i * 100 + 88], &flags, 4);
                set++;
            }
        }
        c.say("[rule 8] world lights flags: set bit 0x2 on %zu of %zu lights with non-zero flags (stock R1 maps have it on every light except the sun/sky entries, R2 never sets it; meaning unknown, default off, enable with --worldlight-flag2)", set, n);
    }
    c.say("[rule 8] world lights (0x36): %zu lights, lump version %u (%zu byte records) -> version 1 (100 byte records); trailing %zu bytes per record dropped",
          n, c.version[L_WORLDLIGHTS], rec, rec - 100);
    c.version[L_WORLDLIGHTS] = 1;
}

}  // namespace detail

// ---------------------------------------------------------------------------

std::vector<uint8_t> DowngradeBsp(const uint8_t* data, size_t size) {
    return DowngradeBsp(data, size, Options(), nullptr);
}

std::vector<uint8_t> DowngradeBsp(const uint8_t* data, size_t size, const Options& options, Report* report) {
    using namespace detail;
    if (size < sizeof(RawBspHeader)) throw Error("input smaller than a BSP header");
    RawBspHeader in;
    std::memcpy(&in, data, sizeof(in));
    if (in.ident != kIdentRBSP) throw Error("input is not an rBSP file");
    if (in.version != 37) throw Error("input is rBSP version " + std::to_string(in.version) + ", expected 37 (Titanfall 2)");
    if (in.lastLump != 127) throw Error("header lastLump field is " + std::to_string(in.lastLump) + ", expected 127");

    Ctx c;
    c.opt = options;
    c.rep = report;
    c.revision = in.revision;
    for (uint32_t i = 0; i < 128; i++) {
        const RawLumpHeader& h = in.lumps[i];
        c.version[i] = h.version;
        c.fourCC[i] = h.fourCC;
        c.origOffset[i] = h.offset;
        if (h.fourCC != 0)
            throw Error("lump " + std::to_string(i) + " has a non-zero uncompressed size field (compressed lumps are not supported)");
        if (h.length) {
            if ((uint64_t)h.offset + h.length > size)
                throw Error("lump " + std::to_string(i) + " lies outside the file (offset " + std::to_string(h.offset) + ", length " + std::to_string(h.length) + ", file " + std::to_string(size) + ")");
            c.lump[i].assign(data + h.offset, data + h.offset + h.length);
        }
        c.present[i] = h.offset != 0 || h.length != 0;
    }
    c.say("[rule 1] header: rBSP v37 revision %u -> v29, lastLump 127 kept, lumps re-laid out 4-byte aligned", in.revision);

    // Order matters: tricoll and meshes read lumps that the collision and
    // bounds stages then reuse.
    convertWorldLights(c);
    convertRealTimeLights(c);
    convertLightProbeRefs(c);
    convertTricoll(c);
    convertCollisionGrid(c);
    convertStaticProps(c);
    convertMeshes(c);
    convertVertexFrames(c);
    convertPhysics(c);
    convertPhysicsTriangles(c);
    convertCubemaps(c);
    reportResidualBounds(c);

    if (c.opt.dropR2OnlyLumps) {
        const uint32_t drop[] = {L_R2_LPPARENT, L_R2_SHADOWENV, L_R2_LPBSPNODES, L_R2_LPBSPREFS, L_R2_WLPARENT, L_R2_RTLPAGES};
        size_t total = 0;
        std::string names;
        for (uint32_t d : drop) {
            if (c.present[d]) {
                char b[64];
                std::snprintf(b, sizeof(b), "%s0x%02X(%zu)", names.empty() ? "" : " ", d, c.lump[d].size());
                names += b;
                total += c.lump[d].size();
            }
            c.lump[d].clear();
            c.present[d] = false;
            c.version[d] = 0;
        }
        c.say("[rule 8] dropped R2-only lumps R1 never reads: %s, %zu bytes", names.c_str(), total);
    }

    // Lump sizes that R1's loaders check, final validation of the output.
    {
        const struct { uint32_t lump; unsigned size; const char* name; } chk[] = {
            {L_PLANES, 16, "planes (0x01)"},        {L_TEXDATA, 36, "texture data (0x02)"},
            {L_VERTICES, 12, "vertices (0x03)"},    {L_MODELS, 32, "models (0x0E)"},
            {L_VNORMALS, 12, "vertex normals (0x1E)"}, {L_LEAFWATER, 12, "leaf water (0x24)"},
            {L_CUBEMAPS, 16, "cubemaps (0x2A)"},    {L_TDSTRTAB, 4, "texdata string table (0x2C)"},
            {L_WORLDLIGHTS, 100, "world lights (0x36)"}, {L_TRICOLL_TRIS, 4, "tricoll triangles (0x42)"},
            {L_TRICOLL_NODES, 16, "tricoll nodes (0x44)"}, {L_TRICOLL_HDR, 44, "tricoll headers (0x45)"},
            {L_VTX_UNLIT, 20, "unlit vertices (0x47)"}, {L_VTX_LITFLAT, 36, "lit flat vertices (0x48)"},
            {L_VTX_LITBUMP, 44, "lit bump vertices (0x49)"}, {L_VTX_UNLITTS, 28, "unlit TS vertices (0x4A)"},
            {L_VTX_BLINN, 28, "blinn-phong vertices (0x4B)"}, {L_MESHIDX, 2, "mesh indices (0x4F)"},
            {L_MESHES, 28, "meshes (0x50)"},        {L_MESHBOUNDS, 32, "mesh bounds (0x51)"},
            {L_SORTS, 12, "material sorts (0x52)"}, {L_LMHDR, 8, "lightmap headers (0x53)"},
            {L_CMCELLS, 4, "cm grid cells (0x56)"}, {L_CMGEOSETS, 8, "cm geosets (0x57)"},
            {L_CMGEOBOUNDS, 16, "cm geoset bounds (0x58)"}, {L_CMPRIMS, 4, "cm primitives (0x59)"},
            {L_CMPRIMBOUNDS, 16, "cm primitive bounds (0x5A)"}, {L_CMUNIQUE, 4, "cm unique contents (0x5B)"},
            {L_CMBRUSHES, 32, "cm brushes (0x5C)"}, {0x5D, 2, "brush side planes (0x5D)"},
            {0x5E, 2, "brush side props (0x5E)"},  {0x5F, 32, "brush side texvecs (0x5F)"},
            {L_BEVSTARTS, 2, "bevel starts (0x60)"}, {L_BEVINDICES, 4, "bevel indices (0x61)"},
            {L_CSMAABB, 32, "csm aabb (0x63)"},     {0x64, 2, "csm obj refs (0x64)"},
            {L_LIGHTPROBES, 48, "light probes (0x65)"}, {L_LPTREE, 8, "light probe tree (0x67)"},
            {L_LPREFS, 16, "light probe refs (0x68)"}, {0x6A, 8, "cell bsp nodes (0x6A)"},
            {0x6B, 8, "cells (0x6B)"},              {0x6C, 12, "portals (0x6C)"},
            {0x6D, 12, "portal verts (0x6D)"},      {0x6E, 4, "portal edges (0x6E)"},
            {0x6F, 16, "portal vert edges (0x6F)"}, {0x70, 2, "portal vert refs (0x70)"},
            {0x71, 2, "portal edge refs (0x71)"},   {0x72, 16, "portal isect edge (0x72)"},
            {0x73, 16, "portal isect vert (0x73)"}, {0x74, 8, "portal isect header (0x74)"},
            {0x75, 12, "occluder verts (0x75)"},    {0x76, 2, "occluder indices (0x76)"},
            {L_CELLAABB, 32, "cell aabb (0x77)"},   {0x78, 2, "obj refs (0x78)"},
            {L_OBJREFBOUNDS, 32, "obj ref bounds (0x79)"}, {L_SHADOWOPAQUE, 12, "shadow opaque verts (0x7C)"},
            {L_SHADOWALPHA, 20, "shadow alpha verts (0x7D)"}, {0x7E, 2, "shadow indices (0x7E)"},
            {0x7F, 12, "shadow meshes (0x7F)"},
        };
        for (auto& k : chk) {
            if (c.lump[k.lump].size() % k.size)
                throw Error(std::string(k.name) + ": size " + std::to_string(c.lump[k.lump].size()) + " is not a multiple of " + std::to_string(k.size) + " (R1 'funny lump size')");
        }
        if (!c.lump[L_CMGRID].empty() && c.lump[L_CMGRID].size() != sizeof(Grid)) throw Error("cm grid (0x55): size is not 28");
        if (c.lump[L_LEVELINFO].size() != 28) throw Error("level info (0x7B): size is " + std::to_string(c.lump[L_LEVELINFO].size()) + ", expected 28");
        // sky lightmap size must equal 8 bytes per type-1 texel
        const Bytes& hdr = c.lump[L_LMHDR];
        uint64_t texels = 0;
        for (size_t i = 0; i + 8 <= hdr.size(); i += 8) {
            uint32_t type;
            uint16_t w, h;
            std::memcpy(&type, &hdr[i], 4);
            std::memcpy(&w, &hdr[i + 4], 2);
            std::memcpy(&h, &hdr[i + 6], 2);
            if (type == 1) texels += (uint64_t)w * h;
        }
        if (!c.lump[L_LMSKY].empty() && c.lump[L_LMSKY].size() != texels * 8)
            throw Error("sky lightmap (0x62): size " + std::to_string(c.lump[L_LMSKY].size()) + " != 8 * type-1 texels (" + std::to_string(texels * 8) + "), R1 aborts with 'Sky lightmaps exist, but are too small/large'");
        c.say("[rule 8] validated %zu lump record sizes against R1 loader requirements; sky lightmap size %zu = 8 x %llu texels",
              sizeof(chk) / sizeof(chk[0]), c.lump[L_LMSKY].size(), (unsigned long long)texels);
    }

    // Rule 1: layout.
    size_t total = sizeof(RawBspHeader);
    for (uint32_t i = 0; i < 128; i++) {
        total = (total + 3) & ~(size_t)3;
        total += c.lump[i].size();
    }
    Bytes out(total, 0);
    RawBspHeader oh;
    std::memset(&oh, 0, sizeof(oh));
    oh.ident = kIdentRBSP;
    oh.version = 29;
    oh.revision = c.revision;
    oh.lastLump = 127;
    size_t cur = sizeof(RawBspHeader);
    for (uint32_t i = 0; i < 128; i++) {
        cur = (cur + 3) & ~(size_t)3;
        if (c.present[i] || !c.lump[i].empty()) {
            oh.lumps[i].offset = (uint32_t)cur;
            oh.lumps[i].length = (uint32_t)c.lump[i].size();
            oh.lumps[i].version = c.version[i];
            oh.lumps[i].fourCC = 0;
            if (!c.lump[i].empty()) std::memcpy(&out[cur], c.lump[i].data(), c.lump[i].size());
            if (i == L_GAME && c.gameLumpNeedsRebase && !c.lump[i].empty()) {
                uint32_t n;
                std::memcpy(&n, &out[cur], 4);
                for (uint32_t k = 0; k < n; k++) {
                    GameLumpHeader gh;
                    std::memcpy(&gh, &out[cur + 4 + 16 * k], 16);
                    gh.offset += (uint32_t)cur;
                    std::memcpy(&out[cur + 4 + 16 * k], &gh, 16);
                }
            }
        }
        cur += c.lump[i].size();
    }
    if (cur > 0xFFFFFFFFull) throw Error("output exceeds 4 GiB");
    std::memcpy(out.data(), &oh, sizeof(oh));
    c.say("[rule 1] output size %zu bytes (input %zu)", out.size(), size);
    return out;
}

}  // namespace bspdown
