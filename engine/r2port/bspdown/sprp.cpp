// Origin: C:\Users\r3muxd\tmp\r2port\bspdown. Bundled in r1delta under GPL-3.0.
// Rule 5: static prop game lump (0x23), sprp v13 (R2, 64 byte props) -> v12 (R1, 84 byte props).
//
// Layouts (bsp_tool respawn/titanfall.py StaticPropv12, respawn/titanfall2.py
// StaticPropv13, bsp_regen titanfall.hpp / titanfall2.hpp; R1 reader checked in
// the engine: sub_18018FD90 reads three ints then 84 byte records, the model
// index is the uint16 at +0x18).
//
//   game lump:  int32 count; count * GameLumpHeader{id, flags:u16, version:u16, offset, length};
//               offsets are absolute file offsets.
//   sprp v12:   u32 numModels; numModels * char[128]; u32 numLeaves; u16 leaves[numLeaves];
//               u32 numProps; u32 unknown1; u32 unknown2; numProps * StaticPropV12 (84)
//   sprp v13:   u32 numModels; numModels * char[128];
//               u32 numProps; u32 unknown1; u32 unknown2; numProps * StaticPropV13 (64);
//               u32 numUnknown3; numUnknown3 * 64 bytes (R2 only, dropped)
#include "internal.hpp"

#include <algorithm>
#include <cmath>

namespace bspdown {
namespace detail {

namespace {

// Rule 23: a prop whose origin is farther than half-diagonal + this from its cell-reference bounds counts as far-origin.
constexpr float kFarOriginMargin = 256.0f;
// Rule 23: fade distance for far-origin props whose R2 fade distance is -1 (never fades in R2). R1 culls at about 0.9 * fade
// (sub_18018C890, wide view zoom 2.1x), so 65536 covers any map extent.
constexpr float kNoFadeDistance = 65536.0f;

#pragma pack(push, 1)
struct PropR2 {
    float origin[3];           // 0x00
    float angles[3];           // 0x0C
    float scale;               // 0x18
    uint16_t modelName;        // 0x1C
    uint8_t solidType;         // 0x1E
    uint8_t flags;             // 0x1F
    uint16_t skin;             // 0x20
    uint16_t cubemap;          // 0x22
    float fadeDistance;        // 0x24 (named forced_fade_scale in bsp_regen; holds -1 or a distance in the data)
    float lightingOrigin[3];   // 0x28
    uint8_t diffuse[4];        // 0x34
    uint32_t collisionAdd;     // 0x38
    uint32_t collisionRemove;  // 0x3C
};
static_assert(sizeof(PropR2) == 64, "PropR2");

struct PropR1 {
    float origin[3];                        // 0x00
    float angles[3];                        // 0x0C
    uint16_t modelName;                     // 0x18
    uint16_t firstLeaf;                     // 0x1A
    uint16_t numLeaves;                     // 0x1C
    uint8_t solidType;                      // 0x1E
    uint8_t flags;                          // 0x1F
    uint16_t skin;                          // 0x20
    uint16_t cubemap;                       // 0x22
    float fadeMin;                          // 0x24
    float fadeMax;                          // 0x28
    float lightingOrigin[3];                // 0x2C
    float forcedFadeScale;                  // 0x38
    int8_t cpuMin, cpuMax, gpuMin, gpuMax;  // 0x3C
    uint8_t diffuse[4];                     // 0x40
    uint32_t disableX360;                   // 0x44
    float scale;                            // 0x48
    uint32_t collisionAdd;                  // 0x4C
    uint32_t collisionRemove;               // 0x50
};
static_assert(sizeof(PropR1) == 84, "PropR1");
#pragma pack(pop)

uint32_t rd32(const Bytes& b, size_t o) {
    if (o + 4 > b.size()) throw Error("game lump (0x23): truncated at offset " + std::to_string(o));
    uint32_t v;
    std::memcpy(&v, &b[o], 4);
    return v;
}

// Convert the payload of one sprp v13 sub-lump to v12.
Bytes convertSprp(Ctx& c, const Bytes& payload) {
    size_t o = 0;
    uint32_t numModels = rd32(payload, o);
    o += 4;
    if (o + (size_t)numModels * 128 > payload.size()) throw Error("sprp: model name table exceeds the sub-lump");
    Bytes res;
    appendRaw(res, numModels);
    res.insert(res.end(), payload.begin() + o, payload.begin() + o + (size_t)numModels * 128);
    o += (size_t)numModels * 128;
    uint32_t numProps = rd32(payload, o);
    uint32_t unk1 = rd32(payload, o + 4), unk2 = rd32(payload, o + 8);
    o += 12;
    if (o + (size_t)numProps * sizeof(PropR2) > payload.size()) throw Error("sprp: prop array exceeds the sub-lump");
    size_t propsEnd = o + (size_t)numProps * sizeof(PropR2);
    size_t tail = payload.size() - propsEnd;
    uint32_t numUnk3 = 0;
    if (tail >= 4) {
        numUnk3 = rd32(payload, propsEnd);
        if (tail != 4 + (size_t)numUnk3 * 64)
            throw Error("sprp: " + std::to_string(tail) + " trailing bytes after the props do not match a count of " + std::to_string(numUnk3) + " 64 byte records");
    } else if (tail != 0) {
        throw Error("sprp: " + std::to_string(tail) + " unexplained trailing bytes");
    }
    uint32_t zero = 0;
    appendRaw(res, zero);  // numLeaves
    appendRaw(res, numProps);
    appendRaw(res, unk1);
    appendRaw(res, unk2);
    std::vector<uint32_t> modelCount(numModels ? numModels : 1, 0);
    for (uint32_t k = 0; k < numProps; k++) {
        uint16_t mn;
        std::memcpy(&mn, &payload[o + (size_t)k * sizeof(PropR2) + 0x1C], 2);
        if (mn < numModels) modelCount[mn]++;
    }
    const auto pm = c.opt.props;
    size_t hidden = 0, flag10Cleared = 0;
    size_t badScale = 0, modelOob = 0;
    // Rule 23 input: world bounds of every prop = union of its cell object-reference bounds (lumps 0x78 refs, 0x79 bounds). A reference
    // id >= propBase = (levelinfo word 2 + 63) & ~63 names prop (id - propBase); the cell data is what R2 culls with, so the bounds
    // are the real geometry location whatever the origin is.
    std::vector<float> pbMin((size_t)numProps * 3, 0.0f), pbMax((size_t)numProps * 3, 0.0f);
    std::vector<uint8_t> hasBounds(numProps, 0);
    size_t farOrigin = 0, noRefs = 0;
    {
        const Bytes& li = c.lump[L_LEVELINFO];
        const Bytes& rf = c.lump[0x78];
        const Bytes& rb = c.lump[L_OBJREFBOUNDS];
        if (li.size() >= 12 && rf.size() % 2 == 0 && rb.size() == (rf.size() / 2) * 32) {
            int32_t word2;
            std::memcpy(&word2, &li[8], 4);
            const uint64_t propBase = ((uint64_t)(uint32_t)word2 + 63) & ~63ull;
            const size_t nRefs = rf.size() / 2;
            for (size_t j = 0; j < nRefs; j++) {
                uint16_t id;
                std::memcpy(&id, &rf[j * 2], 2);
                if (id < propBase || id - propBase >= numProps) continue;
                const uint32_t k = (uint32_t)(id - propBase);
                float b[8];
                std::memcpy(b, &rb[j * 32], 32);
                for (int a = 0; a < 3; a++) {
                    const float lo = b[a], hi = b[4 + a];
                    if (!hasBounds[k]) {
                        pbMin[(size_t)k * 3 + a] = lo;
                        pbMax[(size_t)k * 3 + a] = hi;
                    } else {
                        pbMin[(size_t)k * 3 + a] = std::min(pbMin[(size_t)k * 3 + a], lo);
                        pbMax[(size_t)k * 3 + a] = std::max(pbMax[(size_t)k * 3 + a], hi);
                    }
                }
                hasBounds[k] = 1;
            }
            for (uint32_t k = 0; k < numProps; k++) {
                if (!hasBounds[k]) { noRefs++; continue; }
                for (int a = 0; a < 3; a++)
                    if (!std::isfinite(pbMin[(size_t)k * 3 + a]) || !std::isfinite(pbMax[(size_t)k * 3 + a]) ||
                        std::fabs(pbMin[(size_t)k * 3 + a]) > 1e6f || std::fabs(pbMax[(size_t)k * 3 + a]) > 1e6f) { hasBounds[k] = 0; noRefs++; break; }
            }
        } else {
            noRefs = numProps;
            c.say("[rule 23] cell object-reference lumps unusable (levelinfo %zu bytes, 0x78 %zu bytes, 0x79 %zu bytes); prop fade distance left as R2", li.size(), rf.size(), rb.size());
        }
    }
    res.reserve(res.size() + (size_t)numProps * sizeof(PropR1));
    for (uint32_t k = 0; k < numProps; k++) {
        PropR2 p;
        std::memcpy(&p, &payload[o + (size_t)k * sizeof(PropR2)], sizeof(p));
        PropR1 q;
        std::memset(&q, 0, sizeof(q));
        std::memcpy(q.origin, p.origin, 12);
        std::memcpy(q.angles, p.angles, 12);
        q.modelName = p.modelName;
        q.solidType = p.solidType;
        q.flags = p.flags;
        q.skin = p.skin;
        q.cubemap = p.cubemap;
        q.fadeMin = p.fadeDistance;
        q.fadeMax = p.fadeDistance;
        std::memcpy(q.lightingOrigin, p.lightingOrigin, 12);
        q.forcedFadeScale = 1.0f;
        std::memcpy(q.diffuse, p.diffuse, 4);
        q.scale = p.scale;
        q.collisionAdd = p.collisionAdd;
        q.collisionRemove = p.collisionRemove;
        if (hasBounds[k]) {
            // Rule 23: R1 culls a prop by the distance from the view to its ORIGIN (sub_18018C890) against the fade distance
            // derived from the model hull. R2 "world space" props sit at an origin far from their geometry.
            const float* mn = &pbMin[(size_t)k * 3];
            const float* mx = &pbMax[(size_t)k * 3];
            double d2 = 0, h2 = 0;
            for (int a = 0; a < 3; a++) {
                double cen = 0.5 * ((double)mn[a] + (double)mx[a]);
                double dd = (double)p.origin[a] - cen, hh = 0.5 * ((double)mx[a] - (double)mn[a]);
                d2 += dd * dd;
                h2 += hh * hh;
            }
            const double dist = std::sqrt(d2), half = std::sqrt(h2);
            if (dist > half + kFarOriginMargin) {
                farOrigin++;
                if (!c.opt.keepPropFade) {
                    const float fade = p.fadeDistance > 0.0f ? p.fadeDistance + (float)dist : kNoFadeDistance;
                    q.fadeMin = fade;
                    q.fadeMax = fade;
                }
            }
        }
        {
            bool hide = pm == Options::PropMode::HideAll ||
                        (pm == Options::PropMode::HideScatter && p.modelName < numModels && modelCount[p.modelName] >= c.opt.scatterMinCount);
            if (hide) {
                q.scale = 0.0001f;
                hidden++;
            }
            if (!c.opt.keepPropFlag10 && (q.flags & 0x10) && !(q.flags & 0x40)) {
                q.flags &= (uint8_t)~0x10;
                flag10Cleared++;
            }
        }
        if (p.modelName >= numModels) modelOob++;
        if (!(p.scale > 0.0f)) badScale++;
        appendRaw(res, q);
    }
    if (modelOob) throw Error("sprp: " + std::to_string(modelOob) + " props reference a model index beyond the " + std::to_string(numModels) + " model names");
    if (numProps > 40960) throw Error("sprp: " + std::to_string(numProps) + " props, R1 aborts above 40960");
    c.say("[rule 5] sprp: v13 -> v12, %u model names, %u props (64 -> 84 bytes), leaf list emptied, dropped %u trailing 64-byte records; unknown ints %u / %u kept",
          numModels, numProps, numUnk3, unk1, unk2);
    c.say("[rule 5] sprp defaults for fields R2 lacks: first_leaf 0, num_leaves 0, fade_distance_max = fade_distance_min = R2 value at +0x24, forced_fade_scale 1.0, cpu/gpu level 0,0,0,0, disable_x360 0 (what every stock R1 prop has, except the fade distance: R1 stores -1 or a large value)");
    c.say("[rule 5] collision_flags_add/remove copied unchanged (R2 bit meanings, see rule 9); %zu props have a non-positive scale", badScale);
    c.say("[rule 16] prop flag bit 0x10: %s on %zu of %u props (R1 reads it as 'not drawn in the main pass, shadow caster only'; kept on props that also carry 0x40, the stock R1 vista combination 0x54)",
          c.opt.keepPropFlag10 ? "kept (--keep-prop-flag10)" : "cleared", flag10Cleared, numProps);
    if (c.opt.keepPropFade)
        c.say("[rule 23] prop fade distance: R2 value copied for every prop (--keep-prop-fade); %zu props have an origin more than half-diagonal + %.0f units outside their cell-reference bounds", farOrigin, kFarOriginMargin);
    else
        c.say("[rule 23] prop fade distance: %zu props have an origin more than half-diagonal + %.0f units from their cell-reference bounds; fade distance raised (R2 -1 -> %.0f, R2 distance D -> D + origin-to-bounds-centre distance) so R1's origin-based distance cull no longer drops them; %zu props had no cell references",
              farOrigin, kFarOriginMargin, kNoFadeDistance, noRefs);
    if (pm != Options::PropMode::Keep)
        c.say("[rule 16] diagnostic prop variant (%s): %zu props scaled to 0.0001; prop count and order unchanged (object reference lumps index props); scatter threshold %u instances",
              pm == Options::PropMode::HideAll ? "hide-all" : "hide-scatter", hidden, c.opt.scatterMinCount);
    return res;
}

}  // namespace

void convertStaticProps(Ctx& c) {
    const Bytes& g = c.lump[L_GAME];
    if (g.empty()) {
        c.say("[rule 5] game lump (0x23) is empty, nothing to convert");
        return;
    }
    const uint32_t count = rd32(g, 0);
    if (count > 64) throw Error("game lump (0x23): implausible sub-lump count " + std::to_string(count));
    if (4 + (size_t)count * 16 > g.size()) throw Error("game lump (0x23): directory exceeds the lump");
    std::vector<GameLumpHeader> dir(count);
    if (count) std::memcpy(dir.data(), &g[4], (size_t)count * 16);
    const uint64_t lumpStart = c.origOffset[L_GAME];

    struct Out {
        GameLumpHeader h;
        Bytes payload;
    };
    std::vector<Out> outs;
    bool sawSprp = false;
    for (uint32_t i = 0; i < count; i++) {
        GameLumpHeader h = dir[i];
        if (h.offset < lumpStart + 4 + 16ull * count)
            throw Error("game lump (0x23): sub-lump " + std::to_string(i) + " offset " + std::to_string(h.offset) + " lies before the end of the directory");
        uint64_t rel = h.offset - lumpStart;
        if (rel + h.length > g.size())
            throw Error("game lump (0x23): sub-lump " + std::to_string(i) + " exceeds the lump (offset " + std::to_string(rel) + ", length " + std::to_string(h.length) + ", lump " + std::to_string(g.size()) + ")");
        Bytes payload(g.begin() + (size_t)rel, g.begin() + (size_t)rel + h.length);
        if (h.id == kIdentSprp) {
            if (h.version != 13) throw Error("sprp sub-lump has version " + std::to_string(h.version) + ", expected 13 (Titanfall 2)");
            payload = convertSprp(c, payload);
            h.version = 12;
            h.flags = 0;
            sawSprp = true;
        }
        h.length = (uint32_t)payload.size();
        outs.push_back({h, std::move(payload)});
    }
    if (!sawSprp) c.say("[rule 5] no sprp sub-lump found; game lump copied");

    // Directory offsets are absolute file offsets; the writer adds the lump's
    // output offset (Ctx::gameLumpNeedsRebase). Store lump-relative offsets here.
    Bytes res;
    appendRaw(res, count);
    size_t cur = 4 + (size_t)count * 16;
    for (auto& o : outs) {
        o.h.offset = (uint32_t)cur;
        appendRaw(res, o.h);
        cur += o.payload.size();
    }
    for (auto& o : outs) res.insert(res.end(), o.payload.begin(), o.payload.end());
    c.lump[L_GAME] = std::move(res);
    c.gameLumpNeedsRebase = true;
}

}  // namespace detail
}  // namespace bspdown
