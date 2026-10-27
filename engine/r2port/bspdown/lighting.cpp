// Origin: C:\Users\r3muxd\tmp\r2port\bspdown. Bundled in r1delta under GPL-3.0.
// Rule 20 (and rule 2/8a): world lights (0x36), light probe light indices (0x65) and real-time lights (0x69).
// See README.txt "Lighting" and lightref\FINDINGS.md for the evidence behind every constant.
//
// R2 and R1 number world lights differently, and several lumps store such numbers:
//   R2 array: [sky ambient x E][sun x E][real-time spots][real-time points][baked spots][baked points], E = number of light_environment
//             entities (every R2 map has 2: a dummy skybox environment first, the real one last).
//   R1 array: [sky ambient][sun][...same order...]; R1 reads light[0] as the sky ambient only when its type is 5, takes the sun from
//             light[1] (CPU path) or from the LAST type-3 entry (GPU constants), and resolves
//               probe static light index:  world index = stored + (light[0].type == 5)
//               GPU light slot:            world index - (light[0].type == 5)  (= the RTL byte)
//   R2 stores   probe static light index:  world index - E        (E = number of leading type-5 lights)
//               RTL palette entry:         world index - E
#include "internal.hpp"

#include <algorithm>
#include <cmath>

namespace bspdown {
namespace detail {

namespace {

constexpr size_t kRec = 100;
constexpr size_t kOffType = 52, kOffFlags = 88, kOffIntensity = 12, kOffRadius = 72, kOffC = 76, kOffL = 80, kOffQ = 84;
constexpr uint32_t kFlagFade = 0x4, kFlagRealtime = 0x8, kFlagPbr = 0x20, kFlagUnknown40 = 0x40, kFlagBit2 = 0x2;

struct Light {
    uint8_t b[kRec];
    int32_t type() const { int32_t v; std::memcpy(&v, b + kOffType, 4); return v; }
    int32_t flags() const { int32_t v; std::memcpy(&v, b + kOffFlags, 4); return v; }
    void setFlags(int32_t v) { std::memcpy(b + kOffFlags, &v, 4); }
    float f(size_t off) const { float v; std::memcpy(&v, b + off, 4); return v; }
    void setF(size_t off, float v) { std::memcpy(b + off, &v, 4); }
};

// Distance at which c + l*d + q*d^2 reaches 0.5 (the compiler fits the polynomial through (0,1), (fifty percent distance, 0.5),
// (radius, 0)). Returns 0 when there is no positive root.
float halfDistance(const Light& L) {
    double c = L.f(kOffC), l = L.f(kOffL), q = L.f(kOffQ);
    double a = q, b = l, cc = c - 0.5, best = 0;
    if (std::fabs(a) < 1e-12) {
        if (std::fabs(b) > 1e-12) best = -cc / b;
    } else {
        double disc = b * b - 4 * a * cc;
        if (disc >= 0) {
            double s = std::sqrt(disc);
            double r1 = (-b - s) / (2 * a), r2 = (-b + s) / (2 * a);
            for (double r : {r1, r2})
                if (r > 0 && (best == 0 || r < best)) best = r;
        }
    }
    return best > 0 ? (float)best : 0.0f;
}

float pbrIntensityScale(const Light& L, float scale) {
    float d50 = halfDistance(L);
    const float radius = L.f(kOffRadius);
    if (d50 <= 0) d50 = radius > 0 ? radius / 3.0f : 100.0f;
    return scale * 2.0f / (d50 * d50);
}

uint32_t rdU32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }

}  // namespace

void convertRealTimeLightsFill(Ctx& c) {
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
    rtl.assign((size_t)(texels * bpt), 0xFF);
}

void convertLighting(Ctx& c) {
    Bytes& wl = c.lump[L_WORLDLIGHTS];
    if (wl.empty()) {
        c.version[L_WORLDLIGHTS] = 1;
        convertRealTimeLightsFill(c);
        return;
    }
    size_t rec;
    switch (c.version[L_WORLDLIGHTS]) {
        case 1: rec = 100; break;
        case 2: rec = 108; break;
        case 3: rec = 112; break;
        default: throw Error("world lights (0x36): unknown lump version " + std::to_string(c.version[L_WORLDLIGHTS]));
    }
    if (wl.size() % rec)
        throw Error("world lights (0x36): size " + std::to_string(wl.size()) + " is not a multiple of " + std::to_string(rec) + " for lump version " + std::to_string(c.version[L_WORLDLIGHTS]));
    const size_t nOld = wl.size() / rec;
    std::vector<Light> old(nOld);
    for (size_t i = 0; i < nOld; i++) std::memcpy(old[i].b, &wl[i * rec], kRec);
    const uint32_t srcVersion = c.version[L_WORLDLIGHTS];

    // --- structure of the R2 array -----------------------------------------------------------------------------------------------
    size_t E = 0, S = 0;
    while (E < nOld && old[E].type() == 5) E++;
    while (E + S < nOld && old[E + S].type() == 3) S++;
    bool prefixClean = true;
    for (size_t i = E + S; i < nOld; i++)
        if (old[i].type() == 5 || old[i].type() == 3) prefixClean = false;

    // --- new order ------------------------------------------------------------------------------------------------------------------
    std::vector<int32_t> order;  // new index -> old index
    bool collapsed = false;
    if (c.opt.collapseEnvs && prefixClean && (E > 1 || S > 1)) {
        // The real environment is not always the last one: on mp_coliseum / mp_coliseum_column the arena environment is slot 0 and the
        // dummy skybox environment (sun exactly 3 3 3, direction -0.33544 0.52557 -0.78183, the compiler's template) is slot 1. Keep the
        // last environment whose sun is not that template; with none (or E != S), the last pair as before.
        size_t real = E ? E - 1 : 0, realSun = S ? S - 1 : 0;
        if (E == S) {
            for (size_t i = E; i-- > 0;) {
                const Light& sun = old[E + i];
                const bool dummy = std::fabs(sun.f(kOffIntensity) - 3.0f) < 1e-3f && std::fabs(sun.f(kOffIntensity + 4) - 3.0f) < 1e-3f &&
                                   std::fabs(sun.f(kOffIntensity + 8) - 3.0f) < 1e-3f && std::fabs(sun.f(24) + 0.33544f) < 2e-3f &&
                                   std::fabs(sun.f(28) - 0.52557f) < 2e-3f && std::fabs(sun.f(32) + 0.78183f) < 2e-3f;
                if (!dummy) { real = realSun = i; break; }
            }
        }
        if (E) order.push_back((int32_t)real);
        if (S) order.push_back((int32_t)(E + realSun));
        collapsed = true;
    } else {
        for (size_t i = 0; i < E + S; i++) order.push_back((int32_t)i);
    }
    const size_t head = order.size();
    for (size_t i = E + S; i < nOld; i++) order.push_back((int32_t)i);
    if (!prefixClean) c.say("[rule 20] note: type 3/5 lights occur after the leading ambient/sun block; environments are not collapsed");
    const int v1 = (!order.empty() && old[order[0]].type() == 5) ? 1 : 0;  // what R1 computes from light[0]

    // --- decode the R2 real-time lights --------------------------------------------------------------------------------------------
    const Bytes& hdr = c.lump[L_LMHDR];
    if (hdr.size() % 8) throw Error("lightmap headers (0x53): size is not a multiple of 8");
    std::vector<std::pair<uint32_t, uint32_t>> pages;  // (width, height) of type-1 pages
    uint64_t texels = 0;
    for (size_t i = 0; i < hdr.size() / 8; i++) {
        uint32_t type;
        uint16_t w, h;
        std::memcpy(&type, &hdr[i * 8], 4);
        std::memcpy(&w, &hdr[i * 8 + 4], 2);
        std::memcpy(&h, &hdr[i * 8 + 6], 2);
        if (type == 1) {
            pages.push_back({w, h});
            texels += (uint64_t)w * h;
        }
    }
    Bytes& rtl = c.lump[L_LMRTL];
    const Bytes& pal = c.lump[L_R2_RTLPAGES];
    const bool haveR2Rtl = !rtl.empty() && texels && rtl.size() == texels * 9 && pal.size() % 126 == 0 && !pal.empty();
    std::vector<uint16_t> texLights;  // 4 old world indices per texel (0xFFFF none)
    std::vector<double> usage(nOld, 0);
    const bool doRtl = c.opt.rtlConvert && haveR2Rtl;
    uint64_t droppedLowWeight = 0, droppedKind = 0, usedSlots = 0, badRefs = 0;
    const bool sunAlpha = c.opt.lightmapSunAlpha && haveR2Rtl && c.lump[L_LMSKY].size() == (size_t)texels * 8;
    uint64_t sunTexels = 0, sunLit = 0, sunPartial = 0;
    // lookup: R2 stores the sun visibility byte sRGB encoded (texture created with the sRGB flag), R1 plane B alpha is linear
    uint8_t sunAlphaOf[256];
    double linearVisibility[256];
    for (int i = 0; i < 256; i++) {
        double x = i / 255.0;
        double lin = x <= 0.04045 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4);
        linearVisibility[i] = lin;
        sunAlphaOf[i] = (uint8_t)(255 - (int)std::lround(255.0 * lin));
    }
    if (haveR2Rtl) {
        const size_t npal = pal.size() / 126;
        std::vector<uint16_t> palette(pal.size() / 2);
        std::memcpy(palette.data(), pal.data(), pal.size());
        texLights.assign((size_t)texels * 4, 0xFFFF);
        size_t off = 0, tex = 0, skyOff = 0;
        for (auto& pg : pages) {
            const size_t n = (size_t)pg.first * pg.second;
            const uint8_t* A = &rtl[off];
            const uint8_t* B = A + 4 * n;
            off += 9 * n;
            uint8_t* skyB = sunAlpha ? &c.lump[L_LMSKY][skyOff + 4 * n] : nullptr;
            skyOff += 8 * n;
            for (size_t t = 0; t < n; t++, tex++) {
                uint32_t a = rdU32(A + 4 * t);
                if (a == 0xFFFFFFFFu) continue;
                uint32_t sel = a >> 24;
                for (int s = 0; s < 4; s++) {
                    uint32_t slot = (a >> (6 * s)) & 63;
                    if (slot == 63) continue;
                    uint8_t w = B[4 * t + s];
                    if (sel >= npal) { badRefs++; continue; }
                    size_t wo = (size_t)palette[sel * 63 + slot] + E;
                    if (wo >= nOld) { badRefs++; continue; }
                    const Light& L = old[wo];
                    if (sunAlpha && L.type() == 3) {
                        // R1 world PS: outside the cascades / without CSM the sun shadow term is (1 - plane B alpha); R2 keeps that
                        // baked sun visibility in the RTL B weight of the sun slot (the plane B alpha is 255 everywhere in R2).
                        uint8_t al = sunAlphaOf[w];
                        skyB[4 * t + 3] = al;
                        sunTexels++;
                        if (al == 0) sunLit++; else if (al != 255) sunPartial++;
                        continue;
                    }
                    if ((L.type() != 1 && L.type() != 2) || !(L.flags() & (int32_t)kFlagRealtime)) { droppedKind++; continue; }
                    // Local B includes the spotlight cone as well as occlusion. The conservative contribution cutoff
                    // stays configurable: accepting every nonzero B would promote genuine partial shadow to full light.
                    if ((int)w < c.opt.rtlMinWeight) { droppedLowWeight++; continue; }
                    texLights[tex * 4 + s] = (uint16_t)wo;
                    usage[wo] += linearVisibility[w];
                    usedSlots++;
                }
            }
        }
    }

    // --- real-time lights occupy a contiguous group; the RTL byte limits it to 255 slots ---------------------------------------------
    std::vector<size_t> flaggedPos;  // positions in 'order' of real-time point/spot lights
    for (size_t k = head; k < order.size(); k++) {
        const Light& L = old[order[k]];
        if ((L.type() == 1 || L.type() == 2) && (L.flags() & (int32_t)kFlagRealtime)) flaggedPos.push_back(k);
    }
    size_t reordered = 0;
    if (doRtl && !flaggedPos.empty()) {
        // slot of the k-th flagged light = flaggedPos[k] - v1 (it must stay <= 254)
        const size_t lastSlot = flaggedPos.back() - (size_t)v1;
        if (lastSlot > 254) {
            // Rank contribution potential, not raw sRGB byte coverage. Distance/facing are evaluated by the shader;
            // this score preserves the most visible light energy without rasterizing the BSP during conversion.
            for (size_t p : flaggedPos) {
                const size_t i = (size_t)order[p];
                const Light& L = old[i];
                const bool pbr = c.opt.pbrLights && (L.flags() & (int32_t)kFlagPbr) && !(L.flags() & (int32_t)kFlagFade);
                const float scale = pbr ? pbrIntensityScale(L, c.opt.pbrScale) : 1.0f;
                usage[i] *= scale * (0.2126 * L.f(kOffIntensity) + 0.7152 * L.f(kOffIntensity + 4) + 0.0722 * L.f(kOffIntensity + 8));
            }
            std::vector<int32_t> ids;
            for (size_t p : flaggedPos) ids.push_back(order[p]);
            std::stable_sort(ids.begin(), ids.end(), [&](int32_t a, int32_t b) { return usage[a] > usage[b]; });
            for (size_t k = 0; k < flaggedPos.size(); k++) order[flaggedPos[k]] = ids[k];
            reordered = flaggedPos.size();
        }
    }

    std::vector<int32_t> newOf(nOld, -1);
    for (size_t k = 0; k < order.size(); k++) newOf[order[k]] = (int32_t)k;
    if (order.size() > c.opt.maxWorldLights)
        throw Error("world lights (0x36): " + std::to_string(order.size()) + " lights, R1 aborts above " + std::to_string(c.opt.maxWorldLights));

    // --- write the light array ---------------------------------------------------------------------------------------------------------
    uint64_t pbrConverted = 0, sunFlagsCleared = 0, flag2 = 0, rtlFlagCleared = 0;
    Bytes out(order.size() * kRec);
    for (size_t k = 0; k < order.size(); k++) {
        Light L = old[order[k]];
        const int32_t t = L.type();
        int32_t fl = L.flags();
        if (t == 3 || t == 5) {
            // R1 stock sun/ambient records carry flags 0; R2 uses 0x18 on the sun. Nothing in R1 reads them, but the GPU light
            // upload copies the record and 0x10 would ask for a shadow slot.
            if (fl) sunFlagsCleared++;
            fl = 0;
            const float gain = t == 5 ? c.opt.skyAmbientScale : c.opt.sunScale;
            if (gain != 1.0f)
                for (int ch = 0; ch < 3; ch++) L.setF(kOffIntensity + 4 * ch, L.f(kOffIntensity + 4 * ch) * gain);
        } else {
            if (c.opt.pbrLights && (fl & (int32_t)kFlagPbr) && !(fl & (int32_t)kFlagFade)) {
                const float scale = pbrIntensityScale(L, c.opt.pbrScale);  // match inverse-square intensity at d50
                for (int ch = 0; ch < 3; ch++) L.setF(kOffIntensity + 4 * ch, L.f(kOffIntensity + 4 * ch) * scale);
                fl |= (int32_t)kFlagFade;
                pbrConverted++;
            }
            fl &= ~(int32_t)(kFlagPbr | kFlagUnknown40);
            // Client sub_1800FCE60 keeps a 256 entry table (stack buffer + word_18150BFE0, 512 bytes) indexed by the GPU slot of every
            // light with flag 0x8 and without 0x10; a slot above 255 overflows it into the client's .data (ConVar objects). Stock R1
            // maps keep every real-time light at slot <= 254 (the RTL byte limit). The table index is the array index or the GPU slot depending on the base pointer the caller passes, so the conservative limit is array index <= 254. Lights beyond lose 0x8.
            if ((t == 1 || t == 2) && (fl & (int32_t)kFlagRealtime) && k >= 255) {
                fl &= ~(int32_t)kFlagRealtime;
                rtlFlagCleared++;
            }
            if (c.opt.worldLightFlagBit2 && (t == 1 || t == 2) && !(fl & (int32_t)kFlagBit2)) {
                fl |= (int32_t)kFlagBit2;
                flag2++;
            }
        }
        L.setFlags(fl);
        std::memcpy(&out[k * kRec], L.b, kRec);
    }
    wl.swap(out);
    c.version[L_WORLDLIGHTS] = 1;
    c.say("[rule 8] world lights (0x36): %zu lights, lump version %u -> version 1 (100 byte records, trailing %zu bytes dropped)", nOld, srcVersion, rec - kRec);
    c.say("[rule 20] world light layout: R2 had %zu sky ambient + %zu sun entries; R1 array = %zu entries before the local lights (%s), %zu lights total, R1 base v1 = %d",
          E, S, head, collapsed ? "last (real) environment kept, dummy skybox pairs dropped" : "unchanged", order.size(), v1);
    c.say("[rule 20] world light flags: sun/ambient flags cleared on %llu records (R1 stock 0); PBR lights (0x20, no 0x4) rewritten to the fade polynomial form with intensity x 2/d50^2: %llu; bits 0x20/0x40 cleared; bit 0x2 set on %llu; flag 0x8 cleared on %llu lights at array index > 254 (client 256 entry real-time light table)",
          (unsigned long long)sunFlagsCleared, (unsigned long long)pbrConverted, (unsigned long long)flag2, (unsigned long long)rtlFlagCleared);

    // --- light probes: static light indices ------------------------------------------------------------------------------------------
    {
        Bytes& pr = c.lump[L_LIGHTPROBES];
        if (pr.size() % 48) throw Error("light probes (0x65): size is not a multiple of 48");
        uint64_t n = pr.size() / 48, remapped = 0, dropped = 0;
        for (uint64_t i = 0; i < n; i++) {
            uint8_t* p = &pr[i * 48];
            for (int s = 0; s < 4; s++) {
                if (p[0x20 + s] == 0) continue;
                int16_t idx;
                std::memcpy(&idx, p + 0x24 + 2 * s, 2);
                int64_t wo = (int64_t)idx + (int64_t)E;
                int32_t nw = (idx >= 0 && wo < (int64_t)nOld) ? newOf[(size_t)wo] : -1;
                if (nw < 0) {
                    p[0x20 + s] = 0;
                    int16_t m = -1;
                    std::memcpy(p + 0x24 + 2 * s, &m, 2);
                    dropped++;
                } else {
                    int16_t ni = (int16_t)(nw - v1);
                    std::memcpy(p + 0x24 + 2 * s, &ni, 2);
                    remapped++;
                }
            }
        }
        c.say("[rule 20] light probes (0x65): %llu probes, static light slots remapped from 'world - %zu' (R2) to 'world - %d' (R1 reads stored + %d): %llu slots, %llu dropped (dummy environment sun)",
              (unsigned long long)n, E, v1, v1, (unsigned long long)remapped, (unsigned long long)dropped);
    }

    // --- lightmap plane B alpha (baked sun visibility outside the cascades) ---------------------------------------------------------
    if (sunAlpha)
        c.say("[rule 21] lightmap plane B alpha (0x62): R2 has 255 everywhere; R1 world PS uses shadow = (1 - alpha) outside the CSM cascades. Set from the R2 RTL sun slot visibility (sRGB -> linear) on %llu texels with a sun entry: %llu fully lit (alpha 0), %llu partial; all other texels keep 255 (no sun)",
              (unsigned long long)sunTexels, (unsigned long long)sunLit, (unsigned long long)sunPartial);

    // --- real-time lights ----------------------------------------------------------------------------------------------------------
    if (!doRtl) {
        if (c.opt.rtlConvert && !rtl.empty())
            c.say("[rule 20] real-time lights: input is not 9 bytes/texel with 126 byte palettes (%zu bytes, %zu texels, %zu palette bytes); falling back to 0xFF fill", rtl.size(), (size_t)texels, pal.size());
        convertRealTimeLightsFill(c);
        return;
    }
    Bytes outRtl((size_t)texels * 4, 0xFF);
    uint64_t overflow = 0, litTexels = 0, entries = 0;
    for (size_t t = 0; t < (size_t)texels; t++) {
        uint8_t slots[4];
        int ns = 0;
        for (int s = 0; s < 4; s++) {
            uint16_t wo = texLights[t * 4 + s];
            if (wo == 0xFFFF) continue;
            int32_t nw = newOf[wo];
            int slot = nw - v1;
            // Use the final table, including conservative flag clearing at array index 255. A byte may fit
            // while its world-light record is no longer real-time; never emit that inconsistent reference.
            if (nw < 0 || slot < 0 || slot > 254 || !(rdU32(&wl[(size_t)nw * kRec + kOffFlags]) & kFlagRealtime)) { overflow++; continue; }
            slots[ns++] = (uint8_t)slot;
        }
        if (!ns) continue;
        for (int a = 1; a < ns; a++)
            for (int b = a; b > 0 && slots[b - 1] > slots[b]; b--) std::swap(slots[b - 1], slots[b]);
        for (int k = 0; k < ns; k++) outRtl[t * 4 + k] = slots[k];
        litTexels++;
        entries += ns;
    }
    rtl.swap(outRtl);
    c.say("[rule 20] real-time lights (0x69): R2 9 bytes/texel (A: 4 x 6 bit palette slot + 8 bit palette id, B: 4 x authored contribution weight, C: count) + %zu palettes of 63 -> R1 4 bytes/texel = world light index - %d; %llu texels lit, %llu entries (%llu R2 slots used, %llu below contribution weight %d, %llu not point/spot or not flagged 0x8 (sun, baked), %llu bad refs, %llu outside the retained RTL table)%s",
          pal.size() / 126, v1, (unsigned long long)litTexels, (unsigned long long)entries, (unsigned long long)usedSlots,
          (unsigned long long)droppedLowWeight, c.opt.rtlMinWeight, (unsigned long long)droppedKind, (unsigned long long)badRefs, (unsigned long long)overflow,
          reordered ? "; real-time group reordered by contribution potential to fit the retained RTL table" : "");
}

}  // namespace detail
}  // namespace bspdown
