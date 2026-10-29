// Origin: C:\Users\r3muxd\tmp\r2port\bspdown. Bundled in r1delta under GPL-3.0.
// Rules 7 (render mesh clipping and bounds), 10 (cubemaps), and 15 (no-draw render geometry).
//
// Render data model (bsp_tool respawn/titanfall.py, verified against the files):
//   Meshes (0x50, 28 bytes)  first index, triangle count, first_vertex = smallest
//                            index of the mesh, num_vertices = largest - smallest,
//                            material sort, flags (bits 0x600 select the vertex lump).
//   Mesh indices (0x4F)      uint16, relative to MaterialSort.vertex_offset. Every
//                            mesh starts on a multiple of 6 indices; an odd triangle
//                            count is padded with a degenerate triangle that repeats
//                            the last index (the R1 loader may read the padded triangle).
//   Material sorts (0x52)    {texture data, lightmap header, cubemap, last_vertex,
//                            vertex_offset}; last_vertex = largest - smallest index of
//                            the meshes in the sort.
//   Vertex lumps 0x47..0x4A  records start with position index (into 0x03) and normal
//                            index (into 0x1E). Tangents of lit bump / unlit TS
//                            vertices are indices into 0x1E too (bit 31 is a flag).
//   Mesh bounds (0x51)       oriented box: origin, radius, extents, tan(yaw).
//
// Clipping rewrites each world mesh (model 0) that has a vertex beyond the limit:
// triangles are clipped against the box [-L, L]^3 (Sutherland-Hodgman), all
// attributes are interpolated with the barycentric weights of the original
// triangle, and the result is written as a new vertex block at the end of the
// vertex lump with its own material sort (vertex indices are 16 bit, relative to
// the sort's offset, so the block must start at offset 0 of a sort).
// Meshes outside the clip limit are emptied; skybox tool meshes outside it are also emptied.
// Rule 15 empties non-sky tools and SKIP-flagged meshes but keeps their mesh slots and sorts.
#include "internal.hpp"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdint>
#include <map>
#include <unordered_map>

namespace bspdown {
namespace detail {

namespace {

struct VtxType {
    uint32_t lump;
    unsigned stride;
    bool hasLightmap;
    int tangentOffset;  // -1 if none
};
// index = (mesh flags >> 9) & 3
const VtxType kVtx[4] = {
    {L_VTX_LITFLAT, 36, true, -1},
    {L_VTX_LITBUMP, 44, true, 36},
    {L_VTX_UNLIT, 20, false, -1},
    {L_VTX_UNLITTS, 28, false, 20},
};

struct D3 {
    double v[3];
};

struct PolyVert {
    double w[3];
};

struct MeshState {
    std::vector<MeshR2> meshes;
    std::vector<MeshBounds> bounds;
    std::vector<Sort> sorts;
    std::vector<uint16_t> idx;
    std::vector<Vec3> pos;
    std::vector<Vec3> nrm;
    Bytes vtx[4];
    Bytes texdata, strTab, strData;
};

uint32_t rdU32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
float rdF32(const uint8_t* p) {
    float v;
    std::memcpy(&v, p, 4);
    return v;
}
void wrU32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }
void wrF32(uint8_t* p, float v) { std::memcpy(p, &v, 4); }

std::string materialName(const MeshState& s, size_t sortIdx) {
    if (sortIdx >= s.sorts.size()) return "?";
    int td = s.sorts[sortIdx].textureData;
    if (td < 0 || (size_t)(td + 1) * 36 > s.texdata.size()) return "?";
    int32_t nameIndex;
    std::memcpy(&nameIndex, &s.texdata[(size_t)td * 36 + 12], 4);
    if (nameIndex < 0 || (size_t)(nameIndex + 1) * 4 > s.strTab.size()) return "?";
    uint32_t off = rdU32(&s.strTab[(size_t)nameIndex * 4]);
    if (off >= s.strData.size()) return "?";
    const char* p = (const char*)&s.strData[off];
    size_t n = 0;
    while (off + n < s.strData.size() && p[n]) n++;
    return std::string(p, n);
}

std::string normalizedMaterialName(std::string n) {
    for (auto& ch : n) {
        if (ch == '\\') ch = '/';
        ch = (char)std::tolower((unsigned char)ch);
    }
    return n;
}

bool isSkyboxTool(const std::string& n) {
    return n == "tools/toolsskybox";
}

bool isToolMaterial(const std::string& n) {
    return n.rfind("tools/", 0) == 0;
}

bool isSkyRelatedMesh(const MeshR2& m, const std::string& n) {
    // Bit 0x2 is named SKY_2D in the flag table, but R2 uses it on the
    // non-sky tools/toolsblocklight meshes as well. Preserve only SKY (0x4)
    // or an explicitly sky-named tool material.
    return (m.flags & 0x00000004u) != 0 ||
        (n.rfind("tools/", 0) == 0 && n.find("sky") != std::string::npos);
}

bool shouldRemoveNoDrawMesh(const MeshR2& m, const std::string& n) {
    if (isSkyRelatedMesh(m, n)) return false;
    return isToolMaterial(n) || (m.flags & 0x00020000u) != 0;
}

struct VertexAccess {
    MeshState& s;
    const uint8_t* rec(int vt, uint32_t global) const {
        size_t o = (size_t)global * kVtx[vt].stride;
        if (o + kVtx[vt].stride > s.vtx[vt].size()) throw Error("vertex record " + std::to_string(global) + " lies outside lump 0x" + std::to_string(kVtx[vt].lump));
        return &s.vtx[vt][o];
    }
    Vec3 position(int vt, uint32_t global) const {
        uint32_t pi = rdU32(rec(vt, global));
        if (pi >= s.pos.size()) throw Error("vertex record " + std::to_string(global) + ": position index " + std::to_string(pi) + " out of range");
        return s.pos[pi];
    }
};

inline bool outLimit(const Vec3& p, float L) { return std::fabs(p.x) > L || std::fabs(p.y) > L || std::fabs(p.z) > L; }

Vec3 normalized(double x, double y, double z, const Vec3& fallback) {
    double l = std::sqrt(x * x + y * y + z * z);
    if (l < 1e-9) return fallback;
    return Vec3{(float)(x / l), (float)(y / l), (float)(z / l)};
}

// Interpolate the vertex record built from three source records. New position,
// normal (and tangent vectors) are appended to the shared pools.
Bytes blendVertex(MeshState& s, int vt, const uint8_t* r[3], const double w[3], const Vec3& position) {
    const VtxType& T = kVtx[vt];
    Bytes out(T.stride, 0);
    int best = 0;
    for (int k = 1; k < 3; k++)
        if (w[k] > w[best]) best = k;
    // position
    wrU32(&out[0], (uint32_t)s.pos.size());
    s.pos.push_back(position);
    // normal
    {
        double x = 0, y = 0, z = 0;
        Vec3 fb{0, 0, 1};
        for (int k = 0; k < 3; k++) {
            uint32_t ni = rdU32(r[k] + 4);
            if (ni >= s.nrm.size()) throw Error("vertex normal index " + std::to_string(ni) + " out of range");
            const Vec3& n = s.nrm[ni];
            x += w[k] * n.x;
            y += w[k] * n.y;
            z += w[k] * n.z;
            if (k == best) fb = n;
        }
        wrU32(&out[4], (uint32_t)s.nrm.size());
        s.nrm.push_back(normalized(x, y, z, fb));
    }
    // albedo uv at +8
    for (int a = 0; a < 2; a++) {
        double v = 0;
        for (int k = 0; k < 3; k++) v += w[k] * rdF32(r[k] + 8 + 4 * a);
        wrF32(&out[8 + 4 * a], (float)v);
    }
    // colour at +16
    for (int a = 0; a < 4; a++) {
        double v = 0;
        for (int k = 0; k < 3; k++) v += w[k] * r[k][16 + a];
        out[16 + a] = (uint8_t)std::max(0.0, std::min(255.0, std::floor(v + 0.5)));
    }
    if (T.hasLightmap) {
        for (int a = 0; a < 4; a++) {  // lightmap uv (2f) and step (2f)
            double v = 0;
            for (int k = 0; k < 3; k++) v += w[k] * rdF32(r[k] + 20 + 4 * a);
            wrF32(&out[20 + 4 * a], (float)v);
        }
    }
    if (T.tangentOffset >= 0) {
        for (int a = 0; a < 2; a++) {
            uint32_t raw[3];
            for (int k = 0; k < 3; k++) raw[k] = rdU32(r[k] + T.tangentOffset + 4 * a);
            uint32_t flag = raw[best] & 0x80000000u;
            if (raw[0] == 0 && raw[1] == 0 && raw[2] == 0) {
                wrU32(&out[T.tangentOffset + 4 * a], 0);
                continue;
            }
            bool ok = true;
            double x = 0, y = 0, z = 0;
            Vec3 fb{1, 0, 0};
            for (int k = 0; k < 3; k++) {
                uint32_t ti = raw[k] & 0x7FFFFFFFu;
                if (ti >= s.nrm.size()) {
                    ok = false;
                    break;
                }
                const Vec3& n = s.nrm[ti];
                x += w[k] * n.x;
                y += w[k] * n.y;
                z += w[k] * n.z;
                if (k == best) fb = n;
            }
            if (!ok) {
                wrU32(&out[T.tangentOffset + 4 * a], raw[best]);
                continue;
            }
            uint32_t ni = (uint32_t)s.nrm.size();
            s.nrm.push_back(normalized(x, y, z, fb));
            wrU32(&out[T.tangentOffset + 4 * a], ni | flag);
        }
    }
    return out;
}

struct ClipResult {
    std::vector<PolyVert> poly;
};

// Clip the triangle (weights of the original three corners) against [-L, L]^3.
std::vector<PolyVert> clipTriangle(const D3 P[3], double L) {
    std::vector<PolyVert> poly = {{{1, 0, 0}}, {{0, 1, 0}}, {{0, 0, 1}}};
    auto coord = [&](const PolyVert& pv, int axis) { return pv.w[0] * P[0].v[axis] + pv.w[1] * P[1].v[axis] + pv.w[2] * P[2].v[axis]; };
    for (int axis = 0; axis < 3 && !poly.empty(); axis++) {
        for (int sign = -1; sign <= 1; sign += 2) {
            std::vector<PolyVert> out;
            size_t n = poly.size();
            for (size_t i = 0; i < n; i++) {
                const PolyVert& a = poly[i];
                const PolyVert& b = poly[(i + 1) % n];
                double da = sign * coord(a, axis) - L, db = sign * coord(b, axis) - L;  // <= 0 inside
                bool ia = da <= 0, ib = db <= 0;
                if (ia) out.push_back(a);
                if (ia != ib) {
                    double t = da / (da - db);
                    PolyVert m;
                    for (int k = 0; k < 3; k++) m.w[k] = a.w[k] + (b.w[k] - a.w[k]) * t;
                    out.push_back(m);
                }
            }
            poly.swap(out);
            if (poly.size() < 3) {
                poly.clear();
                break;
            }
        }
    }
    return poly;
}

struct MeshLog {
    size_t mesh;
    std::string material;
    const char* action;
    size_t trisIn, trisOut, clipped, dropped, newVerts;
};

void worldAabbOfBounds(const MeshBounds& b, double lo[3], double hi[3]) {
    double yaw = std::atan((double)b.tanYaw);
    double c = std::fabs(std::cos(yaw)), s = std::fabs(std::sin(yaw));
    double wx = c * b.extents[0] + s * b.extents[1];
    double wy = s * b.extents[0] + c * b.extents[1];
    double w[3] = {wx, wy, b.extents[2]};
    for (int a = 0; a < 3; a++) {
        lo[a] = b.origin[a] - w[a];
        hi[a] = b.origin[a] + w[a];
    }
}

}  // namespace

void convertMeshes(Ctx& c) {
    const float L = c.opt.clipLimit;
    MeshState s;
    s.meshes = view<MeshR2>(c.lump[L_MESHES], "meshes (0x50)");
    s.bounds = view<MeshBounds>(c.lump[L_MESHBOUNDS], "mesh bounds (0x51)");
    s.sorts = view<Sort>(c.lump[L_SORTS], "material sorts (0x52)");
    s.idx = view<uint16_t>(c.lump[L_MESHIDX], "mesh indices (0x4F)");
    s.pos = view<Vec3>(c.lump[L_VERTICES], "vertices (0x03)");
    s.nrm = view<Vec3>(c.lump[L_VNORMALS], "vertex normals (0x1E)");
    std::vector<Model> models = view<Model>(c.lump[L_MODELS], "models (0x0E)");
    for (int t = 0; t < 4; t++) {
        if (c.lump[kVtx[t].lump].size() % kVtx[t].stride) throw Error("vertex lump size mismatch");
        s.vtx[t] = c.lump[kVtx[t].lump];
    }
    s.texdata = c.lump[L_TEXDATA];
    s.strTab = c.lump[L_TDSTRTAB];
    s.strData = c.lump[L_TDSTRDATA];
    if (s.meshes.size() != s.bounds.size()) throw Error("meshes (0x50) has " + std::to_string(s.meshes.size()) + " entries but mesh bounds (0x51) has " + std::to_string(s.bounds.size()) + " (R1 'funny lump size')");
    if (models.empty()) throw Error("models (0x0E) is empty");
    const size_t nOrigMeshes = s.meshes.size(), nOrigSorts = s.sorts.size();
    const size_t nOrigPos = s.pos.size(), nOrigNrm = s.nrm.size(), nOrigIdx = s.idx.size();
    const size_t world0 = models[0].firstMesh, worldN = models[0].numMeshes;
    if (world0 + worldN > s.meshes.size()) throw Error("models[0] mesh range exceeds the mesh lump");
    VertexAccess va{s};
    for (size_t i = 0; i < s.meshes.size(); i++) {
        if (s.meshes[i].materialSort >= s.sorts.size()) throw Error("mesh " + std::to_string(i) + ": material sort index out of range");
    }
    std::vector<std::string> materialNames(s.sorts.size());
    std::vector<uint8_t> materialNameReady(s.sorts.size(), 0);
    auto materialForSort = [&](size_t si) -> const std::string& {
        if (!materialNameReady[si]) {
            materialNames[si] = normalizedMaterialName(materialName(s, si));
            materialNameReady[si] = 1;
        }
        return materialNames[si];
    };

    std::vector<uint8_t> ownSort(s.meshes.size(), 0);
    std::vector<MeshLog> log;
    size_t nClipped = 0, nEmptied = 0, nSkyDropped = 0, trisClipped = 0, trisDropped = 0, trisIn = 0, newVertsTotal = 0;
    size_t clipCubemapOverride = 0;
    size_t nNoDrawMeshes = 0, nNoDrawTris = 0;
    size_t nNoDrawToolMeshes = 0, nNoDrawToolTris = 0;
    size_t nNoDrawSkipMeshes = 0, nNoDrawSkipTris = 0;
    std::map<std::string, std::pair<size_t, size_t>> noDrawByMaterial;
    for (size_t mi = 0; mi < s.meshes.size(); mi++) {
        MeshR2& m = s.meshes[mi];
        if (!m.numTriangles) continue;
        const std::string& mat = materialForSort(m.materialSort);
        if (!shouldRemoveNoDrawMesh(m, mat)) continue;
        if ((uint64_t)m.firstMeshIndex + 3ull * m.numTriangles > s.idx.size())
            throw Error("mesh " + std::to_string(mi) + ": no-draw index range exceeds lump 0x4F");
        nNoDrawMeshes++;
        nNoDrawTris += m.numTriangles;
        if (isToolMaterial(mat)) {
            nNoDrawToolMeshes++;
            nNoDrawToolTris += m.numTriangles;
        }
        if (m.flags & 0x00020000u) {
            nNoDrawSkipMeshes++;
            nNoDrawSkipTris += m.numTriangles;
        }
        auto& count = noDrawByMaterial[mat];
        count.first++;
        count.second += m.numTriangles;
        m.numTriangles = 0;
        m.firstVertex = 0;
        m.numVertices = 0;
        MeshBounds& mb = s.bounds[mi];
        for (int a = 0; a < 3; a++) {
            mb.origin[a] = 0;
            mb.extents[a] = 0;
        }
        mb.radius = 0;
        mb.tanYaw = 0;
    }

    // ---- non-drawing render meshes
    if (nNoDrawMeshes)
        c.say("[rule 15] emptied %zu non-drawing meshes / %zu triangles: tools/* %zu/%zu, SKIP-flagged %zu/%zu; sky meshes retained; collision lumps untouched",
              nNoDrawMeshes, nNoDrawTris, nNoDrawToolMeshes, nNoDrawToolTris,
              nNoDrawSkipMeshes, nNoDrawSkipTris);
    for (const auto& kv : noDrawByMaterial)
        c.say("[rule 15]   '%s': %zu meshes, %zu triangles", kv.first.c_str(), kv.second.first, kv.second.second);


    // ---- clip world meshes
    for (size_t mi = world0; mi < world0 + worldN; mi++) {
        MeshR2& m = s.meshes[mi];
        if (m.numTriangles == 0) continue;
        int vt = (m.flags >> 9) & 3;
        const VtxType& T = kVtx[vt];
        if ((uint64_t)m.firstMeshIndex + 3ull * m.numTriangles > s.idx.size()) throw Error("mesh " + std::to_string(mi) + ": index range exceeds lump 0x4F");
        const uint32_t base = (uint32_t)s.sorts[m.materialSort].vertexOffset;
        const size_t nIdx = 3ull * m.numTriangles;
        std::vector<Vec3> P(nIdx);
        std::vector<uint32_t> G(nIdx);
        bool any = false;
        for (size_t k = 0; k < nIdx; k++) {
            G[k] = base + s.idx[m.firstMeshIndex + k];
            P[k] = va.position(vt, G[k]);
            if (outLimit(P[k], L)) any = true;
        }
        trisIn += m.numTriangles;
        if (!any) continue;

        const std::string& mat = materialForSort(m.materialSort);
        MeshLog lg{mi, mat, "clipped", m.numTriangles, 0, 0, 0, 0};
        MeshBounds& mb = s.bounds[mi];
        const int cubemap = m.cubemap;

        auto empty = [&](const char* action) {
            m.numTriangles = 0;
            m.firstVertex = 0;
            m.numVertices = 0;
            for (int a = 0; a < 3; a++) {
                mb.origin[a] = std::max(-L, std::min(L, mb.origin[a]));
                mb.extents[a] = 0;
            }
            mb.radius = 0;
            mb.tanYaw = 0;
            lg.action = action;
        };

        if (isSkyboxTool(mat)) {
            nSkyDropped++;
            lg.dropped = m.numTriangles;
            trisDropped += m.numTriangles;
            empty("sky marker dropped");
            log.push_back(lg);
            continue;
        }

        std::unordered_map<uint32_t, uint16_t> origMap;
        Bytes block;
        uint32_t blockCount = 0;
        std::vector<uint16_t> newIdx;
        auto useOriginal = [&](uint32_t g) -> uint16_t {
            auto it = origMap.find(g);
            if (it != origMap.end()) return it->second;
            const uint8_t* r = va.rec(vt, g);
            block.insert(block.end(), r, r + T.stride);
            origMap[g] = (uint16_t)blockCount;
            if (blockCount >= 65535) throw Error("mesh " + std::to_string(mi) + ": more than 65535 vertices after clipping");
            return (uint16_t)blockCount++;
        };
        size_t newVerts = 0;
        for (uint32_t t = 0; t < m.numTriangles; t++) {
            const Vec3* p = &P[3 * t];
            const uint32_t* g = &G[3 * t];
            bool allIn = !outLimit(p[0], L) && !outLimit(p[1], L) && !outLimit(p[2], L);
            if (allIn) {
                for (int k = 0; k < 3; k++) newIdx.push_back(useOriginal(g[k]));
                continue;
            }
            bool allOut = false;
            for (int a = 0; a < 3 && !allOut; a++) {
                float c0 = (&p[0].x)[a], c1 = (&p[1].x)[a], c2 = (&p[2].x)[a];
                if ((c0 > L && c1 > L && c2 > L) || (c0 < -L && c1 < -L && c2 < -L)) allOut = true;
            }
            if (allOut) {
                lg.dropped++;
                continue;
            }
            D3 Pd[3];
            for (int k = 0; k < 3; k++) {
                Pd[k].v[0] = (&p[k].x)[0];
                Pd[k].v[1] = (&p[k].x)[1];
                Pd[k].v[2] = (&p[k].x)[2];
            }
            std::vector<PolyVert> poly = clipTriangle(Pd, (double)L);
            if (poly.size() < 3) {
                lg.dropped++;
                continue;
            }
            lg.clipped++;
            std::vector<uint16_t> pv(poly.size());
            const uint8_t* src[3] = {va.rec(vt, g[0]), va.rec(vt, g[1]), va.rec(vt, g[2])};
            for (size_t k = 0; k < poly.size(); k++) {
                const PolyVert& q = poly[k];
                int exact = -1;
                for (int a = 0; a < 3; a++)
                    if (q.w[a] >= 1.0 - 1e-7) exact = a;
                if (exact >= 0) {
                    pv[k] = useOriginal(g[exact]);
                    continue;
                }
                Vec3 np;
                float* npf = &np.x;
                for (int a = 0; a < 3; a++) {
                    double v = q.w[0] * Pd[0].v[a] + q.w[1] * Pd[1].v[a] + q.w[2] * Pd[2].v[a];
                    v = std::max(-(double)L, std::min((double)L, v));
                    npf[a] = (float)v;
                }
                Bytes rec = blendVertex(s, vt, src, q.w, np);
                block.insert(block.end(), rec.begin(), rec.end());
                if (blockCount >= 65535) throw Error("mesh " + std::to_string(mi) + ": more than 65535 vertices after clipping");
                pv[k] = (uint16_t)blockCount++;
                newVerts++;
            }
            for (size_t k = 1; k + 1 < poly.size(); k++) {
                newIdx.push_back(pv[0]);
                newIdx.push_back(pv[k]);
                newIdx.push_back(pv[k + 1]);
            }
        }
        trisDropped += lg.dropped;
        trisClipped += lg.clipped;
        newVertsTotal += newVerts;
        lg.newVerts = newVerts;
        size_t nTri = newIdx.size() / 3;
        lg.trisOut = nTri;
        if (nTri == 0) {
            nEmptied++;
            empty("entirely outside, emptied");
            log.push_back(lg);
            continue;
        }
        if (nTri > 65535) throw Error("mesh " + std::to_string(mi) + ": more than 65535 triangles after clipping");

        // new sort + vertex block
        uint32_t blockStart = (uint32_t)(s.vtx[vt].size() / T.stride);
        s.vtx[vt].insert(s.vtx[vt].end(), block.begin(), block.end());
        Sort ns = s.sorts[m.materialSort];
        ns.vertexOffset = (int32_t)blockStart;
        uint16_t mn = 0xFFFF, mx = 0;
        for (uint16_t v : newIdx) {
            mn = std::min(mn, v);
            mx = std::max(mx, v);
        }
        ns.lastVertex = (uint16_t)(mx - mn);
        if (c.opt.cubemaps == CubemapMode::Split) {
            if (ns.cubemap != cubemap) clipCubemapOverride++;
            ns.cubemap = (int16_t)cubemap;
        } else if (c.opt.cubemaps == CubemapMode::None) {
            ns.cubemap = -1;
        }
        if (s.sorts.size() >= 65535) throw Error("more than 65535 material sorts after clipping");
        m.materialSort = (uint16_t)s.sorts.size();
        s.sorts.push_back(ns);
        ownSort[mi] = 1;
        // new indices, aligned to 6, odd count padded with a degenerate triangle
        while (s.idx.size() % 6) s.idx.push_back(0);
        m.firstMeshIndex = (uint32_t)s.idx.size();
        s.idx.insert(s.idx.end(), newIdx.begin(), newIdx.end());
        if (nTri & 1) {
            uint16_t last = newIdx.back();
            for (int k = 0; k < 3; k++) s.idx.push_back(last);
        }
        m.numTriangles = (uint16_t)nTri;
        m.firstVertex = mn;
        m.numVertices = (uint16_t)(mx - mn);
        // bounds from the used vertices
        double lo[3] = {1e30, 1e30, 1e30}, hi[3] = {-1e30, -1e30, -1e30};
        std::vector<Vec3> used;
        for (uint16_t v : newIdx) used.push_back(va.position(vt, blockStart + v));
        for (auto& p : used)
            for (int a = 0; a < 3; a++) {
                lo[a] = std::min(lo[a], (double)(&p.x)[a]);
                hi[a] = std::max(hi[a], (double)(&p.x)[a]);
            }
        double ctr[3], r2 = 0;
        for (int a = 0; a < 3; a++) {
            ctr[a] = (lo[a] + hi[a]) / 2;
            mb.origin[a] = (float)ctr[a];
            mb.extents[a] = (float)((hi[a] - lo[a]) / 2);
        }
        for (auto& p : used) {
            double d = 0;
            for (int a = 0; a < 3; a++) d += ((&p.x)[a] - ctr[a]) * ((&p.x)[a] - ctr[a]);
            r2 = std::max(r2, d);
        }
        mb.radius = (float)std::sqrt(r2);
        mb.tanYaw = 0;
        nClipped++;
        log.push_back(lg);
    }

    // ---- cubemap per material sort (all meshes, all models)
    size_t sortsSplit = 0, meshesCubemapDiffers = 0;
    if (c.opt.cubemaps == CubemapMode::Split) {
        const int16_t UNSET = INT16_MIN;
        std::vector<int16_t> assigned(s.sorts.size(), UNSET);
        std::map<std::pair<uint32_t, int>, uint16_t> variant;
        for (size_t mi = 0; mi < s.meshes.size(); mi++) {
            MeshR2& m = s.meshes[mi];
            if (ownSort[mi]) continue;
            int cm = m.cubemap;
            uint32_t sidx = m.materialSort;
            if (s.sorts[sidx].cubemap != cm) meshesCubemapDiffers++;
            if (assigned[sidx] == UNSET) {
                assigned[sidx] = (int16_t)cm;
                s.sorts[sidx].cubemap = (int16_t)cm;
            } else if (assigned[sidx] != cm) {
                auto key = std::make_pair(sidx, cm);
                auto it = variant.find(key);
                if (it == variant.end()) {
                    if (s.sorts.size() >= 65535) throw Error("more than 65535 material sorts after cubemap split");
                    Sort ns = s.sorts[sidx];
                    ns.cubemap = (int16_t)cm;
                    it = variant.emplace(key, (uint16_t)s.sorts.size()).first;
                    s.sorts.push_back(ns);
                    sortsSplit++;
                }
                m.materialSort = it->second;
            }
        }
    }
    size_t sortsSetNone = 0;
    if (c.opt.cubemaps == CubemapMode::None) {
        for (auto& so : s.sorts) {
            if (so.cubemap != -1) sortsSetNone++;
            so.cubemap = -1;
        }
    }

    // ---- mesh bounds that still exceed the limit although the vertices do not
    size_t boundsRecomputed = 0, nonWorldExceed = 0;
    for (size_t mi = 0; mi < s.meshes.size(); mi++) {
        MeshBounds& mb = s.bounds[mi];
        double lo[3], hi[3];
        worldAabbOfBounds(mb, lo, hi);
        bool ex = false;
        for (int a = 0; a < 3; a++)
            if (lo[a] < -L || hi[a] > L) ex = true;
        if (!ex) continue;
        bool world = mi >= world0 && mi < world0 + worldN;
        if (!world) {
            nonWorldExceed++;
            continue;
        }
        MeshR2& m = s.meshes[mi];
        if (m.numTriangles == 0) {
            for (int a = 0; a < 3; a++) {
                mb.origin[a] = std::max(-L, std::min(L, mb.origin[a]));
                mb.extents[a] = 0;
            }
            mb.radius = 0;
            mb.tanYaw = 0;
            continue;
        }
        int vt = (m.flags >> 9) & 3;
        const uint32_t base = (uint32_t)s.sorts[m.materialSort].vertexOffset;
        double vlo[3] = {1e30, 1e30, 1e30}, vhi[3] = {-1e30, -1e30, -1e30};
        std::vector<Vec3> used;
        for (size_t k = 0; k < 3ull * m.numTriangles; k++) used.push_back(va.position(vt, base + s.idx[m.firstMeshIndex + k]));
        for (auto& p : used)
            for (int a = 0; a < 3; a++) {
                vlo[a] = std::min(vlo[a], (double)(&p.x)[a]);
                vhi[a] = std::max(vhi[a], (double)(&p.x)[a]);
            }
        double ctr[3], r2 = 0;
        for (int a = 0; a < 3; a++) {
            ctr[a] = (vlo[a] + vhi[a]) / 2;
            mb.origin[a] = (float)ctr[a];
            mb.extents[a] = (float)((vhi[a] - vlo[a]) / 2);
        }
        for (auto& p : used) {
            double d = 0;
            for (int a = 0; a < 3; a++) d += ((&p.x)[a] - ctr[a]) * ((&p.x)[a] - ctr[a]);
            r2 = std::max(r2, d);
        }
        mb.radius = (float)std::sqrt(r2);
        mb.tanYaw = 0;
        boundsRecomputed++;
    }

    // ---- model bounds
    size_t modelsClamped = 0;
    double w0min[3], w0max[3];
    for (size_t i = 0; i < models.size(); i++) {
        Model& md = models[i];
        bool ch = false;
        for (int a = 0; a < 3; a++) {
            float lo = std::max(-L, std::min(L, md.mins[a])), hi = std::max(-L, std::min(L, md.maxs[a]));
            if (lo != md.mins[a] || hi != md.maxs[a]) ch = true;
            md.mins[a] = lo;
            md.maxs[a] = hi;
        }
        modelsClamped += ch;
        if (i == 0)
            for (int a = 0; a < 3; a++) {
                w0min[a] = md.mins[a];
                w0max[a] = md.maxs[a];
            }
    }

    // ---- verify, write back
    {
        size_t bad = 0;
        for (size_t mi = world0; mi < world0 + worldN; mi++) {
            const MeshR2& m = s.meshes[mi];
            if (!m.numTriangles) continue;
            int vt = (m.flags >> 9) & 3;
            const uint32_t base = (uint32_t)s.sorts[m.materialSort].vertexOffset;
            for (size_t k = 0; k < 3ull * m.numTriangles; k++)
                if (outLimit(va.position(vt, base + s.idx[m.firstMeshIndex + k]), L)) {
                    bad++;
                    break;
                }
        }
        if (bad) throw Error("internal error: " + std::to_string(bad) + " world meshes still have vertices beyond the clip limit");
    }
    // Rebuild render indices from active meshes. This removes dead tool/skip
    // triangles, preserves each mesh slot, and keeps R1's 6-index alignment.
    const size_t nIdxBeforeCompact = s.idx.size();
    std::vector<uint16_t> compactIdx;
    compactIdx.reserve(s.idx.size());
    for (MeshR2& m : s.meshes) {
        if (!m.numTriangles) {
            m.firstMeshIndex = 0;
            continue;
        }
        if ((uint64_t)m.firstMeshIndex + 3ull * m.numTriangles > s.idx.size())
            throw Error("active mesh index range exceeds lump 0x4F before compaction");
        while (compactIdx.size() % 6) compactIdx.push_back(0);
        const size_t start = m.firstMeshIndex;
        const size_t count = 3ull * m.numTriangles;
        m.firstMeshIndex = (uint32_t)compactIdx.size();
        compactIdx.insert(compactIdx.end(), s.idx.begin() + start, s.idx.begin() + start + count);
        if (m.numTriangles & 1) {
            const uint16_t last = compactIdx.back();
            compactIdx.insert(compactIdx.end(), 3, last);
        }
    }
    s.idx.swap(compactIdx);

    for (auto& m : s.meshes) m.cubemap = 0;  // R1 reads bytes 10..11 as one uint16 vertex type
    c.lump[L_MESHES] = pack(s.meshes);
    c.lump[L_MESHBOUNDS] = pack(s.bounds);
    c.lump[L_SORTS] = pack(s.sorts);
    c.lump[L_MESHIDX] = pack(s.idx);
    c.lump[L_VERTICES] = pack(s.pos);
    c.lump[L_VNORMALS] = pack(s.nrm);
    for (int t = 0; t < 4; t++) c.lump[kVtx[t].lump] = s.vtx[t];
    c.lump[L_MODELS] = pack(models);

    c.say("[rule 7] world meshes (model 0): %zu, triangles %zu; clip limit +-%g", worldN, trisIn, (double)L);
    c.say("[rule 7] meshes clipped %zu (triangles clipped %zu, dropped %zu), emptied (no triangle left) %zu, TOOLS/TOOLSSKYBOX meshes emptied %zu; %zu interpolated vertices; positions +%zu, normals +%zu, sorts +%zu (clip), indices lump %zu -> %zu entries",
          nClipped, trisClipped, trisDropped, nEmptied, nSkyDropped, newVertsTotal, s.pos.size() - nOrigPos, s.nrm.size() - nOrigNrm, s.sorts.size() - nOrigSorts, nOrigIdx, s.idx.size());
    for (size_t i = 0; i < log.size() && i < 24; i++) {
        const MeshLog& l = log[i];
        c.say("[rule 7]   mesh %zu '%s': %s; triangles %zu -> %zu (%zu clipped, %zu dropped, %zu new vertices)", l.mesh, l.material.c_str(), l.action, l.trisIn, l.trisOut, l.clipped, l.dropped, l.newVerts);
    }
    if (log.size() > 24) c.say("[rule 7]   ... %zu more clipped meshes not listed", log.size() - 24);
    c.say("[rule 7] mesh bounds (0x51): %zu world meshes recomputed because the oriented box exceeded the limit although the vertices did not; %zu non-world meshes still exceed (not modified)", boundsRecomputed, nonWorldExceed);
    c.say("[rule 7] models (0x0E): %zu of %zu clamped; model 0 now mins (%g %g %g) maxs (%g %g %g)", modelsClamped, models.size(), w0min[0], w0min[1], w0min[2], w0max[0], w0max[1], w0max[2]);
    if (c.opt.cubemaps == CubemapMode::Split)
        c.say("[rule 10] cubemap per material sort (split): %zu meshes had a cubemap differing from their sort's; %zu sorts added by splitting (sorts %zu -> %zu); mesh cubemap byte cleared (R1 reads it as part of the vertex type). The R1 engine needs patch materials maps/<map>/<material>_<x>_<y>_<z> for every sort with a cubemap",
              meshesCubemapDiffers, sortsSplit, nOrigSorts, s.sorts.size());
    else if (c.opt.cubemaps == CubemapMode::None)
        c.say("[rule 10] cubemap per material sort (none): %zu sorts set to -1 (plain material names, no patch materials needed); mesh cubemap byte cleared (R1 reads it as part of the vertex type)", sortsSetNone);
    else
        c.say("[rule 10] cubemap per material sort (keep): sorts keep the R2 value, mesh cubemap byte cleared");
    c.say("[rule 15] mesh index lump compacted %zu -> %zu entries; mesh slots and material-sort IDs retained",
          nIdxBeforeCompact, s.idx.size());
    (void)nOrigMeshes;
    (void)clipCubemapOverride;
}

// Rule 26: world mesh order (README.txt). R1's per-view world draw item builder (engine.dll sub_1800A0110) visits the visible meshes in
// index order, opens a new draw run whenever the material sort changes and stops at 2048 runs (`if (v2 == 2048) goto done`): every visible
// mesh after the 2048th run is not drawn. R2 lists meshes in an order unrelated to the material sort, so the number of runs in a view is
// close to the number of visible meshes (mp_eden: 5256 runs for all 11712 world meshes) and the high mesh indices vanish depending on
// the view. Stock R1 files are sorted by material sort. The meshes of model 0 below the prop base are stably sorted by material sort; the
// mesh indices stored in the cell object references (0x78) and in the CSM object references (0x64) (ids below the prop base are meshes,
// the others static props) are renumbered. Mesh bounds (0x51) move with their mesh; the index and vertex lumps are addressed through the
// mesh records and the sort's vertex offset, so they do not change.
void orderMeshes(Ctx& c) {
    if (!c.opt.orderMeshes) {
        c.say("[rule 26] mesh order: off (R2 order kept)");
        return;
    }
    std::vector<MeshR2> meshes = view<MeshR2>(c.lump[L_MESHES], "meshes (0x50)");
    std::vector<MeshBounds> bounds = view<MeshBounds>(c.lump[L_MESHBOUNDS], "mesh bounds (0x51)");
    const std::vector<Model> models = view<Model>(c.lump[L_MODELS], "models (0x0E)");
    if (meshes.size() != bounds.size()) throw Error("rule 26: meshes (0x50) has " + std::to_string(meshes.size()) + " entries but mesh bounds (0x51) has " + std::to_string(bounds.size()));
    if (models.empty()) throw Error("rule 26: models (0x0E) is empty");
    if (c.lump[L_LEVELINFO].size() < 12) throw Error("rule 26: level info (0x7B) is too short to hold the prop base");
    int32_t meshWord;
    std::memcpy(&meshWord, &c.lump[L_LEVELINFO][8], 4);
    if (meshWord < 0) throw Error("rule 26: level info word 2 is negative");
    // Level info word 2 = number of regular render meshes. Model 0 lists them first and then the SKY meshes (flag 0x4, no object references:
    // mp_black_water_canal ids 5012..5019 of 5020, mp_eden 11707..11711); R1 treats ids >= word2 of model 0 as that tail, so the tail must stay
    // where it is (sorting it into the middle crashed mp_black_water_canal in client.dll sub_1802AF720). Object ids >= propBase =
    // (word2 + 63) & ~63 name static props (rule 17). Only [model 0 first mesh, word2) is reordered.
    const size_t world0 = models[0].firstMesh;
    const size_t worldEnd = std::min<size_t>((size_t)models[0].firstMesh + models[0].numMeshes, std::min((size_t)meshWord, meshes.size()));
    if (world0 > worldEnd || (size_t)models[0].firstMesh + models[0].numMeshes > meshes.size()) throw Error("rule 26: models[0] mesh range exceeds the mesh lump");
    const size_t nSorts = c.lump[L_SORTS].size() / sizeof(Sort);
    for (size_t i = world0; i < worldEnd; i++)
        if (meshes[i].materialSort >= nSorts) throw Error("rule 26: mesh " + std::to_string(i) + ": material sort index out of range");

    auto countRuns = [&](const std::vector<MeshR2>& m) {
        size_t runs = 0;
        for (size_t i = world0; i < worldEnd; i++)
            if (i == world0 || m[i].materialSort != m[i - 1].materialSort) runs++;
        return runs;
    };
    const size_t runsBefore = countRuns(meshes);

    // oldAt[newIndex] = old index; newOf[old] = new index (identity outside [world0, worldEnd)).
    std::vector<uint32_t> oldAt(meshes.size()), newOf(meshes.size());
    for (size_t i = 0; i < meshes.size(); i++) oldAt[i] = (uint32_t)i;
    // R2 lists the meshes in flag classes (mp_black_water_canal: 0x100a20, 0x100a00, 0x100a30, 0x100a10 blocks) and the later classes hold the
    // decals and overlays (sign lettering, stripes, bullet holes) that must draw after their base meshes: sorting by material sort alone drew
    // them first and the walls covered them. The class rank is the order of first appearance of the flags value, so the sort key is
    // (flag class, material sort) and no mesh moves in front of a mesh of an earlier class.
    std::vector<uint32_t> classRank(meshes.size(), 0);
    {
        std::vector<uint32_t> seen;
        for (size_t i = world0; i < worldEnd; i++) {
            size_t k = 0;
            while (k < seen.size() && seen[k] != meshes[i].flags) k++;
            if (k == seen.size()) seen.push_back(meshes[i].flags);
            classRank[i] = (uint32_t)k;
        }
    }
    std::stable_sort(oldAt.begin() + (ptrdiff_t)world0, oldAt.begin() + (ptrdiff_t)worldEnd,
                     [&](uint32_t a, uint32_t b) {
                         return classRank[a] != classRank[b] ? classRank[a] < classRank[b] : meshes[a].materialSort < meshes[b].materialSort;
                     });
    size_t moved = 0;
    for (size_t i = 0; i < meshes.size(); i++) {
        newOf[oldAt[i]] = (uint32_t)i;
        if (oldAt[i] != i) moved++;
    }
    if (moved == 0) {
        c.say("[rule 26] mesh order: world meshes [%zu, %zu) already sorted by material sort (%zu runs)", world0, worldEnd, runsBefore);
        return;
    }

    std::vector<MeshR2> sortedMeshes(meshes.size());
    std::vector<MeshBounds> sortedBounds(meshes.size());
    for (size_t i = 0; i < meshes.size(); i++) {
        sortedMeshes[i] = meshes[oldAt[i]];
        sortedBounds[i] = bounds[oldAt[i]];
    }
    const size_t runsAfter = countRuns(sortedMeshes);

    size_t renumbered[2] = {0, 0};
    const uint32_t refLumps[2] = {0x78, 0x64};
    for (int k = 0; k < 2; k++) {
        Bytes& b = c.lump[refLumps[k]];
        if (b.size() % 2) throw Error(std::string("rule 26: object reference lump ") + (k == 0 ? "0x78" : "0x64") + " has an odd size");
        for (size_t o = 0; o + 2 <= b.size(); o += 2) {
            uint16_t v;
            std::memcpy(&v, &b[o], 2);
            if (v >= world0 && v < worldEnd && newOf[v] != v) {
                const uint16_t nv = (uint16_t)newOf[v];
                std::memcpy(&b[o], &nv, 2);
                renumbered[k]++;
            }
        }
    }
    c.lump[L_MESHES] = pack(sortedMeshes);
    c.lump[L_MESHBOUNDS] = pack(sortedBounds);
    c.say("[rule 26] mesh order: %zu of %zu world meshes [%zu, %zu) moved so equal material sorts are adjacent; draw runs when every mesh is visible %zu -> %zu (R1 draws at most 2048 per view; %zu distinct sorts in %zu sorts); renumbered %zu cell object references (0x78) and %zu csm object references (0x64)",
          moved, worldEnd - world0, world0, worldEnd, runsBefore, runsAfter, runsAfter, nSorts, renumbered[0], renumbered[1]);
    if (runsAfter > 2048)
        c.say("[rule 26] warning: %zu distinct material sorts in the world meshes exceed R1's 2048 draw runs per view; a view that sees more than 2048 of them still loses the highest sorts", runsAfter);
}

}  // namespace detail
}  // namespace bspdown
