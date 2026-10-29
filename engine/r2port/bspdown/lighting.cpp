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
#include <limits>
#include <map>

namespace bspdown {
namespace detail {

namespace {

constexpr size_t kRec = 100;
constexpr size_t kOffType = 52, kOffFlags = 88, kOffIntensity = 12, kOffRadius = 72, kOffC = 76, kOffL = 80, kOffQ = 84;
// sRGB-encoded static-visibility cutoff of rule 20 (about half visible), applied after spotlight-cone normalization.
constexpr int kRtlVisibilityCutoff = 187;
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

// The native local B texture contains cone * visibility. R1 evaluates the cone
// analytically but has only a binary light list. Compare B >= threshold * cone;
// never divide by a small cone or interpret every small B as a blocked light.
double rtlSpotCone(const Light& light, const std::array<double, 3>& p) {
    double delta[3], distance2 = 0, dot = 0;
    for (int k = 0; k < 3; ++k) {
        delta[k] = p[k] - light.f(4 * k);
        distance2 += delta[k] * delta[k];
        dot += delta[k] * light.f(24 + 4 * k);
    }
    const double width = light.f(60) - light.f(64);
    const double exponent = light.f(68);
    const double scale = exponent <= 0.5 || width < 1e-10 ? 100000.0 : 1.0 / width;
    const double cosine = dot / std::sqrt(std::max(distance2, 1.0));
    const double cone = std::clamp((cosine - light.f(64)) * scale, 0.0, 1.0);
    const double selector = std::clamp(exponent - 1.0, 0.0, 1.0);
    return cone + selector * (cone * cone - cone);
}

void quantizeRtlVisibility(Ctx& c, const std::vector<Light>& lights, std::vector<uint16_t>& ids,
                           const double linear[256], std::vector<double>& usage,
                           uint64_t& rejected, uint64_t& accepted) {
    constexpr uint32_t none = uint32_t(-1);
    struct Page {
        size_t first = 0, rtlOffset = 0;
        uint32_t width = 0, height = 0;
        bool needed = false;
    };
    struct Sample {
        uint32_t texel = 0, currentMesh = none, firstMesh = none;
        uint8_t weak = 0, anyKeep = 0, allKeep = 0;
        uint8_t bestAny = 0, bestAll = 0, coverage = 0;
        bool alias = false, bestAlias = false, unassigned = false;
        double bestDistance = 0;
        Vec3 bestPosition{}, firstPosition{};
    };
    const Bytes& headers = c.lump[L_LMHDR];
    const Bytes& rtl = c.lump[L_LMRTL];
    const int minimum = kRtlVisibilityCutoff;
    const double threshold = linear[minimum];
    std::vector<Page> pages(headers.size() / 8);
    std::vector<Sample> samples;
    size_t first = 0, rtlOffset = 0;
    for (size_t h = 0; h < pages.size(); ++h) {
        if (rdU32(&headers[h * 8]) != 1) continue;
        uint16_t width, height;
        std::memcpy(&width, &headers[h * 8 + 4], 2);
        std::memcpy(&height, &headers[h * 8 + 6], 2);
        pages[h] = {first, rtlOffset, width, height};
        const size_t count = size_t(width) * height;
        const uint8_t* weights = &rtl[rtlOffset + 4 * count];
        for (size_t t = 0; t < count; ++t) {
            Sample sample;
            sample.texel = (uint32_t)(first + t); // a 32-bit BSP lump has fewer than 2^32 / 9 texels
            for (int s = 0; s < 4; ++s) {
                uint16_t& id = ids[(first + t) * 4 + s];
                if (id == 0xFFFF) continue;
                if (!weights[t * 4 + s]) { id = 0xFFFF; ++rejected; continue; }
                if (weights[t * 4 + s] >= minimum) continue;
                if (lights[id].type() == 2) sample.weak |= uint8_t(1u << s);
                else { id = 0xFFFF; ++rejected; }
            }
            if (sample.weak) {
                sample.allKeep = sample.weak;
                samples.push_back(sample);
                pages[h].needed = true;
            }
        }
        first += count;
        rtlOffset += count * 9;
    }
    if (!samples.empty()) {
        const Bytes& meshes = c.lump[L_MESHES];
        const Bytes& sorts = c.lump[L_SORTS];
        const Bytes& indices = c.lump[L_MESHIDX];
        const Bytes& positions = c.lump[L_VERTICES];
        if (meshes.size() % sizeof(MeshR2) || sorts.size() % sizeof(Sort) ||
            indices.size() % 2 || positions.size() % sizeof(Vec3))
            throw Error("RTL visibility: malformed mesh/sort/index/position lump");
        std::vector<uint32_t> byTexel(ids.size() / 4, none);
        for (size_t i = 0; i < samples.size(); ++i) byTexel[samples[i].texel] = (uint32_t)i;
        std::vector<uint32_t> touched;
        std::vector<uint8_t> aliasMeshes(meshes.size() / sizeof(MeshR2), 0);
        const auto different = [](const Vec3& a, const Vec3& b) {
            const double x = double(a.x) - b.x, y = double(a.y) - b.y, z = double(a.z) - b.z;
            return x * x + y * y + z * z > 0.0001; // distinguish receivers, not floating-point seams
        };
        for (size_t mi = 0; mi < meshes.size() / sizeof(MeshR2); ++mi) {
            MeshR2 mesh;
            std::memcpy(&mesh, &meshes[mi * sizeof(mesh)], sizeof(mesh));
            const uint32_t format = (mesh.flags >> 9) & 3;
            if (format > 1 || !mesh.numTriangles) continue;
            if (mesh.materialSort >= sorts.size() / sizeof(Sort))
                throw Error("RTL visibility: mesh material sort out of range");
            Sort sort;
            std::memcpy(&sort, &sorts[mesh.materialSort * sizeof(sort)], sizeof(sort));
            if (sort.lightmapHeader < 0) continue;
            if ((size_t)sort.lightmapHeader >= pages.size())
                throw Error("RTL visibility: lightmap header out of range");
            const Page& page = pages[sort.lightmapHeader];
            if (!page.needed) continue;
            const uint32_t stride = format ? 44 : 36;
            const Bytes& vertices = c.lump[format ? L_VTX_LITBUMP : L_VTX_LITFLAT];
            if (vertices.size() % stride || sort.vertexOffset < 0 ||
                (uint64_t(mesh.firstMeshIndex) + uint64_t(mesh.numTriangles) * 3) * 2 > indices.size())
                throw Error("RTL visibility: invalid vertex/index range");
            const size_t pageCount = size_t(page.width) * page.height;
            const uint8_t* weights = &rtl[page.rtlOffset + 4 * pageCount];
            touched.clear();
            for (size_t tri = 0; tri < mesh.numTriangles; ++tri) {
                uint16_t local[3];
                std::memcpy(local, &indices[(size_t(mesh.firstMeshIndex) + tri * 3) * 2], sizeof(local));
                const uint8_t* records[3];
                double uv[3][2];
                for (int k = 0; k < 3; ++k) {
                    const size_t vertex = size_t(sort.vertexOffset) + local[k];
                    if (vertex >= vertices.size() / stride)
                        throw Error("RTL visibility: mesh vertex out of range");
                    records[k] = &vertices[vertex * stride];
                    float texcoord[2];
                    std::memcpy(texcoord, records[k] + 20, sizeof(texcoord));
                    uv[k][0] = double(texcoord[0]) * page.width;
                    uv[k][1] = double(texcoord[1]) * page.height;
                    if (!std::isfinite(uv[k][0]) || !std::isfinite(uv[k][1]))
                        throw Error("RTL visibility: non-finite lightmap UV");
                }
                const double minX = std::min({uv[0][0], uv[1][0], uv[2][0]});
                const double maxX = std::max({uv[0][0], uv[1][0], uv[2][0]});
                const double minY = std::min({uv[0][1], uv[1][1], uv[2][1]});
                const double maxY = std::max({uv[0][1], uv[1][1], uv[2][1]});
                if (maxX < 0 || maxY < 0 || minX >= page.width || minY >= page.height) continue;
                const int x0 = (int)std::floor(std::clamp(minX, 0.0, double(page.width - 1)));
                const int x1 = (int)std::floor(std::clamp(maxX, 0.0, double(page.width - 1)));
                const int y0 = (int)std::floor(std::clamp(minY, 0.0, double(page.height - 1)));
                const int y1 = (int)std::floor(std::clamp(maxY, 0.0, double(page.height - 1)));
                const double determinant = (uv[1][1] - uv[2][1]) * (uv[0][0] - uv[2][0]) +
                                           (uv[2][0] - uv[1][0]) * (uv[0][1] - uv[2][1]);
                const double orientation = determinant < 0 ? -1.0 : 1.0;
                Vec3 world[3];
                bool worldLoaded = false;
                for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
                    const uint32_t index = byTexel[page.first + size_t(y) * page.width + x];
                    if (index == none) continue;
                    Sample& sample = samples[index];
                    const double px = x + 0.5, py = y + 0.5;
                    // Integer Load can sample exactly those texel squares touched by the UV triangle.
                    // SAT uses the square axes (the bounding box above) and the three triangle edges.
                    bool intersects = true;
                    for (int k = 0; k < 3; ++k) {
                        const int next = (k + 1) % 3;
                        const double ex = uv[next][0] - uv[k][0], ey = uv[next][1] - uv[k][1];
                        const double side = orientation * (ex * (py - uv[k][1]) - ey * (px - uv[k][0]));
                        if (side + 0.5 * (std::fabs(ex) + std::fabs(ey)) < -1e-9) intersects = false;
                    }
                    if (!intersects) continue;
                    if (!worldLoaded) {
                        for (int k = 0; k < 3; ++k) {
                            const uint32_t pi = rdU32(records[k]);
                            if (pi >= positions.size() / sizeof(Vec3))
                                throw Error("RTL visibility: position index out of range");
                            std::memcpy(&world[k], &positions[size_t(pi) * sizeof(Vec3)], sizeof(Vec3));
                            if (!std::isfinite(world[k].x) || !std::isfinite(world[k].y) || !std::isfinite(world[k].z))
                                throw Error("RTL visibility: non-finite world position");
                        }
                        worldLoaded = true;
                    }
                    if (std::fabs(determinant) < 1e-12) {
                        const double ax = double(world[1].x) - world[0].x, ay = double(world[1].y) - world[0].y, az = double(world[1].z) - world[0].z;
                        const double bx = double(world[2].x) - world[0].x, by = double(world[2].y) - world[0].y, bz = double(world[2].z) - world[0].z;
                        const double nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx;
                        if (nx * nx + ny * ny + nz * nz > 1e-12) sample.unassigned = true;
                        continue; // zero-area world triangles have no rasterized receiver
                    }
                    double bary[3];
                    bary[0] = ((uv[1][1] - uv[2][1]) * (px - uv[2][0]) + (uv[2][0] - uv[1][0]) * (py - uv[2][1])) / determinant;
                    bary[1] = ((uv[2][1] - uv[0][1]) * (px - uv[2][0]) + (uv[0][0] - uv[2][0]) * (py - uv[2][1])) / determinant;
                    bary[2] = 1.0 - bary[0] - bary[1];
                    double distance = 0;
                    if (std::min({bary[0], bary[1], bary[2]}) < -1e-8) {
                        // A sampled border texel need not have its centre inside the chart. Use the
                        // nearest point of this UV triangle, not an arbitrary dilation/neighbor fill.
                        distance = std::numeric_limits<double>::infinity();
                        for (int k = 0; k < 3; ++k) {
                            const int next = (k + 1) % 3;
                            const double ex = uv[next][0] - uv[k][0], ey = uv[next][1] - uv[k][1];
                            const double length2 = ex * ex + ey * ey;
                            const double t = length2 ? std::clamp(((px - uv[k][0]) * ex + (py - uv[k][1]) * ey) / length2, 0.0, 1.0) : 0;
                            const double dx = px - uv[k][0] - t * ex, dy = py - uv[k][1] - t * ey;
                            if (dx * dx + dy * dy < distance) {
                                distance = dx * dx + dy * dy;
                                bary[0] = bary[1] = bary[2] = 0;
                                bary[k] = 1.0 - t;
                                bary[next] = t;
                            }
                        }
                    }
                    std::array<double, 3> p{};
                    for (int k = 0; k < 3; ++k) {
                        p[0] += bary[k] * world[k].x;
                        p[1] += bary[k] * world[k].y;
                        p[2] += bary[k] * world[k].z;
                    }
                    const Vec3 position{(float)p[0], (float)p[1], (float)p[2]};
                    uint8_t keep = 0;
                    const size_t localTexel = sample.texel - page.first;
                    for (int s = 0; s < 4; ++s) if (sample.weak & (1u << s)) {
                        const uint16_t id = ids[size_t(sample.texel) * 4 + s];
                        if (linear[weights[localTexel * 4 + s]] >= threshold * rtlSpotCone(lights[id], p))
                            keep |= uint8_t(1u << s);
                    }
                    if (sample.currentMesh != mi) {
                        sample.currentMesh = (uint32_t)mi;
                        sample.bestDistance = std::numeric_limits<double>::infinity();
                        touched.push_back(index);
                    }
                    if (distance < sample.bestDistance - 1e-9) {
                        sample.bestDistance = distance;
                        sample.bestAny = sample.bestAll = keep;
                        sample.bestPosition = position;
                        sample.bestAlias = false;
                    } else if (std::fabs(distance - sample.bestDistance) <= 1e-9) {
                        sample.bestAny |= keep;
                        sample.bestAll &= keep;
                        sample.bestAlias |= different(sample.bestPosition, position);
                    }
                }
            }
            for (uint32_t index : touched) {
                Sample& sample = samples[index];
                if (sample.firstMesh == none) {
                    sample.firstMesh = (uint32_t)mi;
                    sample.firstPosition = sample.bestPosition;
                } else if (different(sample.firstPosition, sample.bestPosition)) sample.alias = true;
                sample.alias |= sample.bestAlias;
                if (sample.alias) {
                    aliasMeshes[mi] = 1;
                    aliasMeshes[sample.firstMesh] = 1;
                }
                sample.coverage |= sample.bestDistance == 0 ? 1 : 2;
                sample.anyKeep |= sample.bestAny;
                sample.allKeep &= sample.bestAll; // equivalent to the maximum receiver cone: never favor the leakier alias
            }
        }
        uint64_t centres = 0, borders = 0, unreachable = 0, unassigned = 0, aliases = 0, disagreements = 0;
        uint64_t rescued = 0, rejectedShadow = 0, unreachableSlots = 0;
        for (const Sample& sample : samples) {
            if (sample.unassigned) ++unassigned;
            if (sample.coverage & 1) ++centres;
            else if (sample.coverage) ++borders;
            else ++unreachable;
            if (sample.alias) ++aliases;
            for (int s = 0; s < 4; ++s) if (sample.weak & (1u << s)) {
                if (sample.coverage && ((sample.anyKeep ^ sample.allKeep) & (1u << s))) ++disagreements;
                if (sample.coverage && (sample.allKeep & (1u << s))) ++rescued;
                else {
                    ids[size_t(sample.texel) * 4 + s] = 0xFFFF;
                    ++rejected;
                    if (sample.coverage) ++rejectedShadow;
                    else ++unreachableSlots;
                }
            }
        }
        c.say("[rule 20 visibility] %zu weak-spot texels: %llu centre-covered, %llu sampled chart-border footprints, %llu unreachable; %llu aliased texels across %zu meshes, %llu disagreeing slot decisions (maximum-cone conservative intersection); %llu reachable but unassigned degenerate-UV texels; %llu cone-tail slots rescued, %llu shadow slots rejected, %llu unreachable slots cleared",
              samples.size(), (unsigned long long)centres, (unsigned long long)borders, (unsigned long long)unreachable,
              (unsigned long long)aliases, (size_t)std::count(aliasMeshes.begin(), aliasMeshes.end(), uint8_t(1)),
              (unsigned long long)disagreements, (unsigned long long)unassigned, (unsigned long long)rescued,
              (unsigned long long)rejectedShadow, (unsigned long long)unreachableSlots);
        if (unassigned)
            throw Error("RTL visibility: reachable spotlight texels have non-invertible lightmap UVs; refusing an unquantified visibility fallback");
    }
    for (const Page& page : pages) {
        if (!page.width || !page.height) continue;
        const size_t count = size_t(page.width) * page.height;
        const uint8_t* weights = &rtl[page.rtlOffset + 4 * count];
        for (size_t t = 0; t < count; ++t) for (int s = 0; s < 4; ++s) {
            const uint16_t id = ids[(page.first + t) * 4 + s];
            if (id != 0xFFFF) { usage[id] += linear[weights[t * 4 + s]]; ++accepted; }
        }
    }
}

// ---- Rule 20f (StairStep experiment): bake real-time light contributions into lightmap plane A ---------------------------------------
// R1's RTL list is nearest-sampled and binary; R2's B plane is a filtered visibility*cone field. A light whose pool has shadow edges
// shows texel-aligned steps in R1. Baking the light (colour * attenuation * facing * linear(B)) into plane A makes the bilinear lightmap
// sampler do the smoothing. Receiver positions/normals come from the same UV-triangle reconstruction as the visibility quantizer.
struct BakeLight {
    bool ok = false, bake = false;
    double org[3]{}, inten[3]{}, radius = 0, A = 0, Bq = 0, c = 1, l = 0, q = 0;
    bool rational = false;
    uint64_t edge = 0, partial = 0, total = 0, hot = 0;
};

// Receivers on materials whose R1 shader ignores the real-time light term must keep their RTL slots (baking would only add light R1 never
// drew). Layered terrain materials (name suffix _bm: WorldVertexTransition-style blends) are RTL-blind in the measured R1 build (homestead
// terrain). Options::rtlBakeBm: 2 = every _bm material, 1 = only terrain-like names, 0 = none.
bool bmMaterialName(Ctx& c, int16_t textureData, std::string& name) {
    const Bytes& td = c.lump[L_TEXDATA];
    const Bytes& strTab = c.lump[L_TDSTRTAB];
    const Bytes& strData = c.lump[L_TDSTRDATA];
    if (textureData < 0 || size_t(textureData + 1) * 36 > td.size()) return false;
    int32_t idx;
    std::memcpy(&idx, &td[size_t(textureData) * 36 + 12], 4);
    if (idx < 0 || size_t(idx + 1) * 4 > strTab.size()) return false;
    uint32_t off;
    std::memcpy(&off, &strTab[size_t(idx) * 4], 4);
    if (off >= strData.size()) return false;
    const char* s = reinterpret_cast<const char*>(&strData[off]);
    size_t n = 0;
    while (off + n < strData.size() && s[n]) ++n;
    if (n < 3) return false;
    const char a = s[n - 3], b = s[n - 2], d = s[n - 1];
    if (!(a == '_' && (b == 'b' || b == 'B') && (d == 'm' || d == 'M'))) return false;
    name.assign(s, n);
    for (char& ch : name) ch = (char)std::tolower((unsigned char)ch);
    return true;
}

// Terrain-like layered materials: some token of the path (split at \ / _) names ground (grass, mud, rock, cliff, sand, dirt, riverbed, ter).
// "dirty" and similar words are not ground tokens.
bool terrainLikeName(const std::string& name) {
    size_t i = 0;
    while (i < name.size()) {
        size_t j = i;
        while (j < name.size() && name[j] != '\\' && name[j] != '/' && name[j] != '_') ++j;
        const std::string tok = name.substr(i, j - i);
        i = j + 1;
        if (tok.empty() || tok == "dirty") continue;
        if (tok == "ter" || tok == "sand" || tok == "patchy") return true;
        for (const char* root : {"grass", "mud", "rock", "cliff", "riverbed", "dirt"}) if (tok.find(root) != std::string::npos) return true;
    }
    return false;
}

bool rtlBlindMaterial(Ctx& c, int16_t textureData, int mode) {
    if (mode <= 0) return false;
    std::string name;
    if (!bmMaterialName(c, textureData, name)) return false;
    return mode >= 2 || terrainLikeName(name);
}

void bakeRtlLights(Ctx& c, const Bytes& wl, const std::vector<int32_t>& newOf, int v1, const std::vector<Light>& oldLights,
                   const std::vector<uint16_t>& orig, std::vector<uint16_t>& ids, const double linear[256]) {
    const Bytes& headers = c.lump[L_LMHDR];
    size_t skippedMeshes = 0;
    const bool listBm = getenv("R2BAKE_LIST") != nullptr;
    std::map<std::string, size_t> bmNames;
    Bytes& rtl = c.lump[L_LMRTL];
    Bytes& sky = c.lump[L_LMSKY];
    struct Page {
        size_t first = 0, rtlOffset = 0, skyOffset = 0;
        uint32_t width = 0, height = 0;
    };
    std::vector<Page> pages(headers.size() / 8);
    size_t first = 0, rtlOffset = 0, skyOffset = 0;
    for (size_t h = 0; h < pages.size(); ++h) {
        if (rdU32(&headers[h * 8]) != 1) continue;
        uint16_t width, height;
        std::memcpy(&width, &headers[h * 8 + 4], 2);
        std::memcpy(&height, &headers[h * 8 + 6], 2);
        pages[h] = {first, rtlOffset, skyOffset, width, height};
        const size_t count = size_t(width) * height;
        first += count;
        rtlOffset += count * 9;
        skyOffset += count * 8;
    }
    const size_t texels = first;
    if (sky.size() != texels * 8) { c.say("[rule 20f] lightmap lump size mismatch, bake skipped"); return; }
    // final light records
    std::vector<BakeLight> BL(oldLights.size());
    for (size_t i = 0; i < oldLights.size(); ++i) {
        const Light& o = oldLights[i];
        if ((o.type() != 1 && o.type() != 2) || !(o.flags() & (int32_t)kFlagRealtime)) continue;
        const int32_t nw = newOf[i];
        if (nw < 0 || nw - v1 < 0 || nw - v1 > 254) continue;
        Light L;
        std::memcpy(L.b, &wl[size_t(nw) * kRec], kRec);
        if (!(L.flags() & (int32_t)kFlagRealtime)) continue;
        BakeLight& b = BL[i];
        b.ok = true;
        for (int k = 0; k < 3; ++k) { b.org[k] = L.f(4 * k); b.inten[k] = L.f(kOffIntensity + 4 * k); }
        b.radius = L.f(kOffRadius);
        b.c = L.f(kOffC); b.l = L.f(kOffL); b.q = L.f(kOffQ);
        const double den = b.radius * b.l + b.radius * b.radius * b.q;
        if (b.radius > 0 && std::fabs(den) > 1e-12) {
            b.A = -1.0000005 * (b.radius * b.l) / den;
            b.Bq = -1.0000005 * (b.radius * b.radius * b.q) / den;
            b.rational = true;
        }
    }
    // texels that carry any candidate slot
    std::vector<uint8_t> need(texels, 0);
    uint64_t candidateSlots = 0;
    for (size_t t = 0; t < texels; ++t) for (int s = 0; s < 4; ++s) {
        const uint16_t id = orig[t * 4 + s];
        if (id != 0xFFFF && BL[id].ok) { need[t] = 1; ++candidateSlots; }
    }
    // receivers
    std::vector<float> bestDist(texels, std::numeric_limits<float>::infinity());
    std::vector<float> rpos(texels * 3, 0.0f), rnrm(texels * 3, 0.0f);
    const Bytes& meshes = c.lump[L_MESHES];
    const Bytes& sorts = c.lump[L_SORTS];
    const Bytes& indices = c.lump[L_MESHIDX];
    const Bytes& positions = c.lump[L_VERTICES];
    const Bytes& normals = c.lump[L_VNORMALS];
    if (meshes.size() % sizeof(MeshR2) || sorts.size() % sizeof(Sort) || indices.size() % 2 || positions.size() % sizeof(Vec3) || normals.size() % sizeof(Vec3))
        throw Error("RTL bake: malformed mesh/sort/index/position/normal lump");
    constexpr double reach2 = 2.25;      // receiver reach: 1.5 texels squared
    constexpr double reachPad = 2.0;     // bounding box padding in texels (max(2, reach + 0.5))
    for (size_t mi = 0; mi < meshes.size() / sizeof(MeshR2); ++mi) {
        MeshR2 mesh;
        std::memcpy(&mesh, &meshes[mi * sizeof(mesh)], sizeof(mesh));
        const uint32_t format = (mesh.flags >> 9) & 3;
        if (format > 1 || !mesh.numTriangles) continue;
        if (mesh.materialSort >= sorts.size() / sizeof(Sort)) throw Error("RTL bake: mesh material sort out of range");
        Sort sort;
        std::memcpy(&sort, &sorts[mesh.materialSort * sizeof(sort)], sizeof(sort));
        if (sort.lightmapHeader < 0) continue;
        if ((size_t)sort.lightmapHeader >= pages.size()) throw Error("RTL bake: lightmap header out of range");
        const Page& page = pages[sort.lightmapHeader];
        if (!page.width) continue;
        if (listBm) { std::string nm; if (bmMaterialName(c, sort.textureData, nm)) ++bmNames[nm + (terrainLikeName(nm) ? "  [terrain-like]" : "  [other]")]; }
        if (rtlBlindMaterial(c, sort.textureData, c.opt.rtlBakeBm)) { ++skippedMeshes; continue; }
        const uint32_t stride = format ? 44 : 36;
        const Bytes& vertices = c.lump[format ? L_VTX_LITBUMP : L_VTX_LITFLAT];
        if (vertices.size() % stride || sort.vertexOffset < 0 || (uint64_t(mesh.firstMeshIndex) + uint64_t(mesh.numTriangles) * 3) * 2 > indices.size())
            throw Error("RTL bake: invalid vertex/index range");
        for (size_t tri = 0; tri < mesh.numTriangles; ++tri) {
            uint16_t local[3];
            std::memcpy(local, &indices[(size_t(mesh.firstMeshIndex) + tri * 3) * 2], sizeof(local));
            double uv[3][2], wpos[3][3], wn[3][3];
            for (int k = 0; k < 3; ++k) {
                const size_t vertex = size_t(sort.vertexOffset) + local[k];
                if (vertex >= vertices.size() / stride) throw Error("RTL bake: mesh vertex out of range");
                const uint8_t* rec = &vertices[vertex * stride];
                float texcoord[2];
                std::memcpy(texcoord, rec + 20, sizeof(texcoord));
                uv[k][0] = double(texcoord[0]) * page.width;
                uv[k][1] = double(texcoord[1]) * page.height;
                if (!std::isfinite(uv[k][0]) || !std::isfinite(uv[k][1])) throw Error("RTL bake: non-finite lightmap UV");
                const uint32_t pi = rdU32(rec), ni = rdU32(rec + 4);
                if (pi >= positions.size() / sizeof(Vec3) || ni >= normals.size() / sizeof(Vec3)) throw Error("RTL bake: position/normal index out of range");
                Vec3 p, n;
                std::memcpy(&p, &positions[size_t(pi) * sizeof(Vec3)], sizeof(Vec3));
                std::memcpy(&n, &normals[size_t(ni) * sizeof(Vec3)], sizeof(Vec3));
                wpos[k][0] = p.x; wpos[k][1] = p.y; wpos[k][2] = p.z;
                wn[k][0] = n.x; wn[k][1] = n.y; wn[k][2] = n.z;
            }
            const double minX = std::min({uv[0][0], uv[1][0], uv[2][0]}), maxX = std::max({uv[0][0], uv[1][0], uv[2][0]});
            const double minY = std::min({uv[0][1], uv[1][1], uv[2][1]}), maxY = std::max({uv[0][1], uv[1][1], uv[2][1]});
            if (maxX < 0 || maxY < 0 || minX >= page.width || minY >= page.height) continue;
            const int x0 = (int)std::floor(std::clamp(minX - reachPad, 0.0, double(page.width - 1)));
            const int x1 = (int)std::floor(std::clamp(maxX + reachPad, 0.0, double(page.width - 1)));
            const int y0 = (int)std::floor(std::clamp(minY - reachPad, 0.0, double(page.height - 1)));
            const int y1 = (int)std::floor(std::clamp(maxY + reachPad, 0.0, double(page.height - 1)));
            const double det = (uv[1][1] - uv[2][1]) * (uv[0][0] - uv[2][0]) + (uv[2][0] - uv[1][0]) * (uv[0][1] - uv[2][1]);
            if (std::fabs(det) < 1e-12) continue;
            const double orientation = det < 0 ? -1.0 : 1.0;
            for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
                const size_t texel = page.first + size_t(y) * page.width + x;
                if (!need[texel]) continue;
                const double px = x + 0.5, py = y + 0.5;
                // gutter texels (bilinear taps outside the chart) take the nearest point of the triangle, up to 1.5 texels away
                double bary[3];
                bary[0] = ((uv[1][1] - uv[2][1]) * (px - uv[2][0]) + (uv[2][0] - uv[1][0]) * (py - uv[2][1])) / det;
                bary[1] = ((uv[2][1] - uv[0][1]) * (px - uv[2][0]) + (uv[0][0] - uv[2][0]) * (py - uv[2][1])) / det;
                bary[2] = 1.0 - bary[0] - bary[1];
                double distance = 0;
                if (std::min({bary[0], bary[1], bary[2]}) < -1e-8) {
                    distance = std::numeric_limits<double>::infinity();
                    for (int k = 0; k < 3; ++k) {
                        const int next = (k + 1) % 3;
                        const double ex = uv[next][0] - uv[k][0], ey = uv[next][1] - uv[k][1];
                        const double length2 = ex * ex + ey * ey;
                        const double t = length2 ? std::clamp(((px - uv[k][0]) * ex + (py - uv[k][1]) * ey) / length2, 0.0, 1.0) : 0;
                        const double dx = px - uv[k][0] - t * ex, dy = py - uv[k][1] - t * ey;
                        if (dx * dx + dy * dy < distance) {
                            distance = dx * dx + dy * dy;
                            bary[0] = bary[1] = bary[2] = 0;
                            bary[k] = 1.0 - t;
                            bary[next] = t;
                        }
                    }
                }
                if (distance > reach2 || distance >= bestDist[texel] - 1e-9) continue;
                bestDist[texel] = (float)distance;
                double n[3] = {0, 0, 0};
                for (int k = 0; k < 3; ++k) {
                    rpos[texel * 3 + k] = 0;
                    for (int m = 0; m < 3; ++m) { rpos[texel * 3 + k] += (float)(bary[m] * wpos[m][k]); n[k] += bary[m] * wn[m][k]; }
                }
                const double nl = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                for (int k = 0; k < 3; ++k) rnrm[texel * 3 + k] = nl > 1e-9 ? (float)(n[k] / nl) : 0.0f;
            }
        }
    }
    for (const auto& kv : bmNames) c.say("[rule 20f]   _bm material %s: %zu lit meshes", kv.first.c_str(), kv.second);
    const auto reached = [&](size_t t) { return std::isfinite(bestDist[t]); };
    const auto cone = [&](const Light& L, const double p[3]) {
        return rtlSpotCone(L, std::array<double, 3>{p[0], p[1], p[2]});
    };
    // per-light edge statistics: partial visibility texels and texels bordering a dark neighbour that the cone says should be lit
    const auto weightOf = [&](size_t t, uint16_t id) -> double {
        // linear B of light id at texel t (0 when absent)
        size_t pg = 0;
        for (size_t h = 0; h < pages.size(); ++h) if (pages[h].width && t >= pages[h].first && t < pages[h].first + size_t(pages[h].width) * pages[h].height) { pg = h; break; }
        const size_t local = t - pages[pg].first;
        const uint8_t* B = &rtl[pages[pg].rtlOffset + 4 * size_t(pages[pg].width) * pages[pg].height];
        for (int s = 0; s < 4; ++s) if (orig[t * 4 + s] == id) return linear[B[local * 4 + s]];
        return 0.0;
    };
    (void)weightOf;
    for (size_t h = 0; h < pages.size(); ++h) {
        const Page& pg = pages[h];
        if (!pg.width) continue;
        const uint8_t* B = &rtl[pg.rtlOffset + 4 * size_t(pg.width) * pg.height];
        for (uint32_t y = 0; y < pg.height; ++y) for (uint32_t x = 0; x < pg.width; ++x) {
            const size_t t = pg.first + size_t(y) * pg.width + x;
            if (!need[t] || !reached(t)) continue;
            for (int s = 0; s < 4; ++s) {
                const uint16_t id = orig[t * 4 + s];
                if (id == 0xFFFF || !BL[id].ok) continue;
                const double w = linear[B[(size_t(y) * pg.width + x) * 4 + s]];
                BakeLight& b = BL[id];
                if (w <= 0) continue;
                ++b.total;
                {
                    const double hx = b.org[0] - rpos[t * 3], hy = b.org[1] - rpos[t * 3 + 1], hz = b.org[2] - rpos[t * 3 + 2];
                    const double hd2 = hx * hx + hy * hy + hz * hz, hd = std::sqrt(std::max(hd2, 1e-12));
                    const double hf = std::clamp((hx * rnrm[t * 3] + hy * rnrm[t * 3 + 1] + hz * rnrm[t * 3 + 2]) / hd, 0.0, 1.0);
                    double ha;
                    if (b.rational) {
                        const double sn = std::min(1.0, std::max(1.0 / (b.radius * b.radius), hd2 / (b.radius * b.radius)));
                        ha = 1.0 + b.A * std::sqrt(sn) + b.Bq * sn;
                    } else ha = b.c + b.l * hd + b.q * hd2;
                    ha = std::clamp(ha, 0.0, 1.0);
                    if (std::max({b.inten[0], b.inten[1], b.inten[2]}) * ha * hf * w > 3.5) ++b.hot;
                }
                const double p[3] = {rpos[t * 3], rpos[t * 3 + 1], rpos[t * 3 + 2]};
                const double cn = oldLights[id].type() == 2 ? cone(oldLights[id], p) : 1.0;
                if (cn < 0.25) continue;
                const double vis = std::min(1.0, w / cn);
                if (vis < 0.8) { ++b.partial; ++b.edge; continue; }
                // hard edge: a 4-neighbour inside the same cone that has no (or zero) weight for this light
                static const int dxs[4] = {1, -1, 0, 0}, dys[4] = {0, 0, 1, -1};
                bool edge = false;
                for (int d = 0; d < 4 && !edge; ++d) {
                    const int nx = int(x) + dxs[d], ny = int(y) + dys[d];
                    if (nx < 0 || ny < 0 || nx >= (int)pg.width || ny >= (int)pg.height) continue;
                    const size_t nt = pg.first + size_t(ny) * pg.width + nx;
                    if (!reached(nt)) continue;
                    const double q[3] = {rpos[nt * 3], rpos[nt * 3 + 1], rpos[nt * 3 + 2]};
                    const double dd = std::sqrt((q[0] - b.org[0]) * (q[0] - b.org[0]) + (q[1] - b.org[1]) * (q[1] - b.org[1]) + (q[2] - b.org[2]) * (q[2] - b.org[2]));
                    if (b.radius > 0 && dd > b.radius) continue;
                    const double ncn = oldLights[id].type() == 2 ? cone(oldLights[id], q) : 1.0;
                    if (ncn < 0.25) continue;
                    // same surface? skip neighbours whose normal differs a lot (different face)
                    const double dn = rnrm[t * 3] * rnrm[nt * 3] + rnrm[t * 3 + 1] * rnrm[nt * 3 + 1] + rnrm[t * 3 + 2] * rnrm[nt * 3 + 2];
                    if (dn < 0.9) continue;
                    double nw = 0;
                    const uint8_t* NB = B;
                    for (int ss = 0; ss < 4; ++ss) if (orig[nt * 4 + ss] == id) nw = linear[NB[(size_t(ny) * pg.width + nx) * 4 + ss]];
                    if (nw < 0.05 * ncn) edge = true;
                }
                if (edge) ++b.edge;
            }
        }
    }
    const uint64_t edgeMin = c.opt.rtlBakeEdgeMin > 0 ? (uint64_t)c.opt.rtlBakeEdgeMin : 8;
    size_t lightsBaked = 0, lightsKept = 0;
    for (size_t i = 0; i < BL.size(); ++i) {
        BakeLight& b = BL[i];
        if (!b.ok) continue;
        // R1 creates local variance shadow maps only for flag 0x8 lights WITHOUT 0x10; by default those keep the RTL path (dynamic shadows).
        const bool vsmCapable = !(oldLights[i].flags() & 0x10) && !c.opt.rtlBakeVsm;
        const bool tooHot = b.total && b.hot * 4 > b.total && !c.opt.rtlBakeHot;  // lightmap energy is capped at 4.0 per channel
        b.bake = !vsmCapable && !tooHot && (c.opt.rtlBake == 2 || b.edge >= edgeMin);
        if (b.bake) ++lightsBaked;
        else {
            ++lightsKept;
            if (getenv("R2BAKE_LIST"))
                c.say("[rule 20f]   kept light %zu: type %d flags 0x%x radius %.0f inten %.3g/%.3g/%.3g org %.0f %.0f %.0f texels %llu edge %llu hot %llu reason %s", i,
                      oldLights[i].type(), (unsigned)oldLights[i].flags(), b.radius, b.inten[0], b.inten[1], b.inten[2], b.org[0], b.org[1], b.org[2],
                      (unsigned long long)b.total, (unsigned long long)b.edge, (unsigned long long)b.hot,
                      vsmCapable ? "vsm (no flag 0x10)" : tooHot ? "hot" : "few edge texels");
        }
    }
    // accumulate and re-encode plane A
    auto toLinear = [](double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    auto toSrgb = [](double v) { v = std::clamp(v, 0.0, 1.0); return v <= 0.0031308 ? v * 12.92 : 1.055 * std::pow(v, 1 / 2.4) - 0.055; };
    uint64_t bakedSlots = 0, unreached = 0, texelsChanged = 0, clipped = 0;
    double addedEnergy = 0;
    const int smoothK = std::clamp(c.opt.rtlBakeSmooth, 0, 2);
    uint64_t smoothedSlots = 0;
    // Binomial low-pass of one light's visibility over same-surface neighbour texels (neighbours without a receiver, on another surface or
    // outside the light radius are left out and the kernel renormalised). The raw B is a quarter-step field at luxel resolution.
    const auto smoothW = [&](const Page& pg, const uint8_t* B, size_t lt, size_t t, uint16_t id, double own) -> double {
        static const double kern[3][3] = {{1, 0, 0}, {2, 1, 0}, {6, 4, 1}};  // index by k (0 unused, 1 = [1 2 1], 2 = [1 4 6 4 1]) and |d|
        const double* kk = smoothK == 1 ? kern[1] : kern[2];
        const BakeLight& b = BL[id];
        const int x = int(lt % pg.width), y = int(lt / pg.width);
        double sum = 0, wsum = 0;
        for (int dy = -smoothK; dy <= smoothK; ++dy) for (int dx = -smoothK; dx <= smoothK; ++dx) {
            const int nx = x + dx, ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= (int)pg.width || ny >= (int)pg.height) continue;
            const size_t nt = pg.first + size_t(ny) * pg.width + nx;
            const double kw = kk[std::abs(dx)] * kk[std::abs(dy)];
            double wn;
            if (nt == t) wn = own;
            else {
                if (!reached(nt)) continue;
                const double dn = rnrm[t * 3] * rnrm[nt * 3] + rnrm[t * 3 + 1] * rnrm[nt * 3 + 1] + rnrm[t * 3 + 2] * rnrm[nt * 3 + 2];
                if (dn < 0.9) continue;
                const double off = rnrm[t * 3] * (rpos[nt * 3] - rpos[t * 3]) + rnrm[t * 3 + 1] * (rpos[nt * 3 + 1] - rpos[t * 3 + 1]) + rnrm[t * 3 + 2] * (rpos[nt * 3 + 2] - rpos[t * 3 + 2]);
                if (std::fabs(off) > 4.0) continue;
                bool found = false;
                wn = 0;
                for (int ss = 0; ss < 4; ++ss) if (orig[nt * 4 + ss] == id) { wn = linear[B[(size_t(ny) * pg.width + nx) * 4 + ss]]; found = true; break; }
                if (!found) {
                    const double ex = rpos[nt * 3] - b.org[0], ey = rpos[nt * 3 + 1] - b.org[1], ez = rpos[nt * 3 + 2] - b.org[2];
                    if (b.radius > 0 && ex * ex + ey * ey + ez * ez > b.radius * b.radius) continue;
                }
            }
            sum += kw * wn;
            wsum += kw;
        }
        return wsum > 0 ? sum / wsum : own;
    };
    // Plane A is sampled by the R1 world pixel shader as lm.rgb * (lm.a * 3.984375 + 0.015625) with the hardware bilinear blend applied to rgb and
    // alpha SEPARATELY, so the decoded value of a footprint is (blend rgb) * (blend scale), not the blend of the products. That is exact only while
    // the alpha (the exposure) is the same over the 2x2 footprint; where the exposure differs between neighbours, the cross term overshoots both
    // texels (up to 3.8x measured on mp_glitch with the earlier per-texel minimal-alpha encode, a thin pale contour along the edge of every baked
    // pool). R2's own pages keep the exposure constant over big areas. So a baked texel keeps its R2 exposure unless its energy needs more, and the
    // higher exposure of such texels is spread over the neighbourhood with a geometric decay (rtlBakeAlphaStep per texel, Chebyshev distance), which
    // keeps the exposure ratio between neighbours <= step and the cross term overshoot below ~1 + (step - 1)^2 / (4 step) for equal energies.
    // Exposure only ever rises, so no texel loses range; texels whose exposure changes are re-encoded from their energy.
    const double alphaStep = c.opt.rtlBakeAlphaStep;
    uint64_t raisedTexels = 0, spreadTexels = 0;
    const auto encodeSmooth = [&](const Page& pg, uint8_t* A, const std::vector<double>& chE, const std::vector<double>& chPk, const std::vector<uint8_t>& chg) {
        const int w = int(pg.width), h = int(pg.height);
        const size_t N = size_t(w) * h;
        std::vector<double> env(N, 0.0);
        int bx0 = w, bx1 = -1, by0 = h, by1 = -1;
        for (size_t lt = 0; lt < N; ++lt) {
            if (!chg[lt]) continue;
            if (chPk[lt] > (A[lt * 4 + 3] + 1) / 64.0) {
                env[lt] = chPk[lt];
                ++raisedTexels;
                const int x = int(lt % w), y = int(lt / w);
                bx0 = std::min(bx0, x); bx1 = std::max(bx1, x); by0 = std::min(by0, y); by1 = std::max(by1, y);
            }
        }
        if (bx1 >= 0) {
            std::vector<double> nxt;
            for (;;) {
                // the active rectangle grows by one texel per sweep (a value can only reach a texel one step further each time)
                bx0 = std::max(bx0 - 1, 0); by0 = std::max(by0 - 1, 0); bx1 = std::min(bx1 + 1, w - 1); by1 = std::min(by1 + 1, h - 1);
                nxt = env;
                bool moved = false;
                for (int y = by0; y <= by1; ++y) for (int x = bx0; x <= bx1; ++x) {
                    double m = 0;
                    for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = x + dx, ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                        m = std::max(m, env[size_t(ny) * w + nx]);
                    }
                    const double cand = m / alphaStep;
                    const size_t i = size_t(y) * w + x;
                    if (cand >= 1.0 / 64 && cand > env[i]) { nxt[i] = cand; moved = true; }
                }
                if (!moved) break;
                env.swap(nxt);
            }
        }
        for (size_t lt = 0; lt < N; ++lt) {
            uint8_t* px = &A[lt * 4];
            const double s0 = (px[3] + 1) / 64.0;
            if (!chg[lt] && env[lt] <= s0) continue;  // exposure unchanged: the R2 bytes stay
            double need = std::max(s0, env[lt]);
            if (chg[lt]) need = std::max(need, chPk[lt]);
            const int alpha = std::clamp((int)std::ceil(need * 64.0) - 1, 0, 255);
            const double denom = (alpha + 1) / 64.0;
            double e[3];
            for (int ch = 0; ch < 3; ++ch) e[ch] = chg[lt] ? chE[lt * 3 + ch] : toLinear(px[ch] / 255.0) * s0;
            if (!chg[lt]) ++spreadTexels;
            for (int ch = 0; ch < 3; ++ch) px[ch] = (uint8_t)std::lround(255.0 * toSrgb(std::min(e[ch], 4.0) / denom));
            px[3] = (uint8_t)alpha;
        }
    };
    for (size_t h = 0; h < pages.size(); ++h) {
        const Page& pg = pages[h];
        if (!pg.width) continue;
        const uint8_t* B = &rtl[pg.rtlOffset + 4 * size_t(pg.width) * pg.height];
        uint8_t* A = &sky[pg.skyOffset];
        const size_t npg = size_t(pg.width) * pg.height;
        std::vector<double> chE, chPk;
        std::vector<uint8_t> chg;
        if (alphaStep > 1.0) { chE.assign(npg * 3, 0.0); chPk.assign(npg, 0.0); chg.assign(npg, 0); }
        for (size_t lt = 0; lt < size_t(pg.width) * pg.height; ++lt) {
            const size_t t = pg.first + lt;
            if (!need[t]) continue;
            double add[3] = {0, 0, 0};
            bool any = false;
            for (int s = 0; s < 4; ++s) {
                const uint16_t id = orig[t * 4 + s];
                if (id == 0xFFFF || !BL[id].ok || !BL[id].bake) continue;
                double w = linear[B[lt * 4 + s]];
                if (smoothK > 0 && reached(t)) { const double sw = smoothW(pg, B, lt, t, id, w); if (sw != w) ++smoothedSlots; w = sw; }
                if (w <= 0) { ids[t * 4 + s] = 0xFFFF; continue; }
                if (!reached(t)) { ++unreached; continue; }
                const BakeLight& b = BL[id];
                const double dx = b.org[0] - rpos[t * 3], dy = b.org[1] - rpos[t * 3 + 1], dz = b.org[2] - rpos[t * 3 + 2];
                const double d2 = dx * dx + dy * dy + dz * dz, d = std::sqrt(d2);
                if (d < 1e-6) continue;
                double facing = (dx * rnrm[t * 3] + dy * rnrm[t * 3 + 1] + dz * rnrm[t * 3 + 2]) / d;
                facing = std::clamp(facing, 0.0, 1.0);
                double att;
                if (b.rational) {
                    const double sn = std::min(1.0, std::max(1.0 / (b.radius * b.radius), d2 / (b.radius * b.radius)));
                    att = 1.0 + b.A * std::sqrt(sn) + b.Bq * sn;
                } else att = b.c + b.l * d + b.q * d2;
                att = std::clamp(att, 0.0, 1.0);
                const double k = att * facing * w;
                for (int ch = 0; ch < 3; ++ch) add[ch] += b.inten[ch] * k;
                ids[t * 4 + s] = 0xFFFF;
                ++bakedSlots;
                any = true;
            }
            if (!any) continue;
            uint8_t* px = &A[lt * 4];
            const double scale = (px[3] + 1) / 64.0;
            double e[3];
            for (int ch = 0; ch < 3; ++ch) e[ch] = toLinear(px[ch] / 255.0) * scale + add[ch];
            const double peak = std::max({e[0], e[1], e[2], 1.0 / 64});
            if (peak > 4.0) { ++clipped; for (int ch = 0; ch < 3; ++ch) e[ch] *= 4.0 / peak; }  // keep the hue when the 4.0 cap is exceeded
            const double pk = std::min(peak, 4.0);
            if (alphaStep > 1.0) {
                chg[lt] = 1;
                chPk[lt] = pk;
                for (int ch = 0; ch < 3; ++ch) chE[lt * 3 + ch] = std::min(e[ch], 4.0);
            } else {
                int alpha = (int)std::ceil(pk * 64.0) - 1;
                alpha = std::clamp(alpha, 0, 255);
                const double denom = (alpha + 1) / 64.0;
                for (int ch = 0; ch < 3; ++ch) px[ch] = (uint8_t)std::lround(255.0 * toSrgb(std::min(e[ch], 4.0) / denom));
                px[3] = (uint8_t)alpha;
            }
            ++texelsChanged;
            addedEnergy += (add[0] + add[1] + add[2]) / 3.0;
        }
        if (alphaStep > 1.0) encodeSmooth(pg, A, chE, chPk, chg);
    }
    c.say("[rule 20f] meshes on RTL-blind materials (_bm layered terrain) skipped as receivers: %zu", skippedMeshes);
    c.say("[rule 20f] RTL bake (%s): %zu lights baked, %zu kept as RTL (edge texel minimum %llu); %llu candidate slots, %llu slots baked into plane A over %llu texels (mean added energy %.4f, %llu texels above the encodable 4.0), %llu baked slots without a receiver kept as RTL",
          c.opt.rtlBake == 2 ? "all" : "edge", lightsBaked, lightsKept, (unsigned long long)edgeMin, (unsigned long long)candidateSlots,
          (unsigned long long)bakedSlots, (unsigned long long)texelsChanged, texelsChanged ? addedEnergy / texelsChanged : 0.0, (unsigned long long)clipped, (unsigned long long)unreached);
    if (alphaStep > 1.0)
        c.say("[rule 20f] plane A exposure (alpha) kept at the R2 value unless the baked energy needs more: %llu texels raised, exposure spread over the neighbours with decay %.3g per texel, %llu unbaked texels re-encoded (cross term of the separate rgb / alpha bilinear blend stays below the R2 level)",
              (unsigned long long)raisedTexels, alphaStep, (unsigned long long)spreadTexels);
    else
        c.say("[rule 20f] plane A encoded per texel with the minimal exposure (--rtl-bake-alpha-step 0): the rgb / alpha blend of neighbouring texels overshoots along baked edges");
    if (smoothK > 0 || c.opt.rtlBakeBm != 2 || c.opt.rtlBakeHot || c.opt.rtlBakeVsm)
        c.say("[rule 20f] tuning: _bm skip mode %d, smoothing window %d (%llu slots changed), hot lights %s, lights without flag 0x10 %s", c.opt.rtlBakeBm, smoothK,
              (unsigned long long)smoothedSlots, c.opt.rtlBakeHot ? "baked with clip" : "kept as RTL", c.opt.rtlBakeVsm ? "baked" : "kept as RTL");
}


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
        if (doRtl) texLights.assign((size_t)texels * 4, 0xFFFF);
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
                    if (!doRtl) continue;
                    texLights[tex * 4 + s] = (uint16_t)wo;
                }
            }
        }
    }

    std::vector<uint16_t> texLightsOrig;
    if (doRtl && c.opt.rtlBake) texLightsOrig = texLights;
    if (doRtl)
        quantizeRtlVisibility(c, old, texLights, linearVisibility, usage, droppedLowWeight, usedSlots);

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
            // Rank the retained authored energy; geometry-dependent visibility has already been quantized above.
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

    if (doRtl && c.opt.rtlBake) bakeRtlLights(c, wl, newOf, v1, old, texLightsOrig, texLights, linearVisibility);

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
    c.say("[rule 20] real-time lights (0x69): R2 9 bytes/texel (A: 4 x 6 bit palette slot + 8 bit palette id, B: 4 x authored contribution weight, C: count) + %zu palettes of 63 -> R1 4 bytes/texel = world light index - %d; %llu texels lit, %llu entries (%llu R2 slots used, %llu rejected by visibility threshold %d / atlas coverage, %llu not point/spot or not flagged 0x8 (sun, baked), %llu bad refs, %llu outside the retained RTL table)%s",
          pal.size() / 126, v1, (unsigned long long)litTexels, (unsigned long long)entries, (unsigned long long)usedSlots,
          (unsigned long long)droppedLowWeight, kRtlVisibilityCutoff, (unsigned long long)droppedKind, (unsigned long long)badRefs, (unsigned long long)overflow,
          reordered ? "; real-time group reordered by contribution potential to fit the retained RTL table" : "");
}

}  // namespace detail
}  // namespace bspdown
