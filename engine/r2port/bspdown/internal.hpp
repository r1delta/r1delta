// Origin: C:\Users\r3muxd\tmp\r2port\bspdown. Bundled in r1delta under GPL-3.0.
// Internal types shared by the bspdown translation units.
#pragma once

#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "bspdown.hpp"

namespace bspdown {
namespace detail {

using Bytes = std::vector<uint8_t>;

// Lump indices (shared between R1 and R2 unless noted).
enum Lump : uint32_t {
    L_ENTITIES = 0x00,
    L_PLANES = 0x01,
    L_TEXDATA = 0x02,
    L_VERTICES = 0x03,
    L_R2_LPPARENT = 0x04,  // R2 only
    L_R2_SHADOWENV = 0x05,  // R2 only
    L_R2_LPBSPNODES = 0x06,  // R2 only
    L_R2_LPBSPREFS = 0x07,  // R2 only
    L_MODELS = 0x0E,
    L_ENTPART = 0x18,
    L_PHYSCOLLIDE = 0x1D,
    L_VNORMALS = 0x1E,
    L_GAME = 0x23,
    L_LEAFWATER = 0x24,
    L_PAKFILE = 0x28,
    L_CUBEMAPS = 0x2A,
    L_TDSTRDATA = 0x2B,
    L_TDSTRTAB = 0x2C,
    L_WORLDLIGHTS = 0x36,
    L_R2_WLPARENT = 0x37,  // R2 only
    L_TRICOLL_TRIS = 0x42,
    L_TRICOLL_NODES = 0x44,
    L_TRICOLL_HDR = 0x45,
    L_PHYSICSTRIS = 0x46,
    L_VTX_UNLIT = 0x47,
    L_VTX_LITFLAT = 0x48,
    L_VTX_LITBUMP = 0x49,
    L_VTX_UNLITTS = 0x4A,
    L_VTX_BLINN = 0x4B,
    L_MESHIDX = 0x4F,
    L_MESHES = 0x50,
    L_MESHBOUNDS = 0x51,
    L_SORTS = 0x52,
    L_LMHDR = 0x53,
    L_CMGRID = 0x55,
    L_CMCELLS = 0x56,
    L_CMGEOSETS = 0x57,
    L_CMGEOBOUNDS = 0x58,
    L_CMPRIMS = 0x59,
    L_CMPRIMBOUNDS = 0x5A,
    L_CMUNIQUE = 0x5B,
    L_CMBRUSHES = 0x5C,
    L_BEVSTARTS = 0x60,
    L_BEVINDICES = 0x61,
    L_LMSKY = 0x62,
    L_CSMAABB = 0x63,
    L_LIGHTPROBES = 0x65,
    L_SPLPINDICES = 0x66,
    L_LPTREE = 0x67,
    L_LPREFS = 0x68,
    L_LMRTL = 0x69,
    L_CELLBSPNODES = 0x6A,
    L_PORTALVERTS = 0x6D,
    L_OCCVERTS = 0x75,
    L_CELLAABB = 0x77,
    L_OBJREFBOUNDS = 0x79,
    L_R2_RTLPAGES = 0x7A,  // R2 only
    L_LEVELINFO = 0x7B,
    L_SHADOWOPAQUE = 0x7C,
    L_SHADOWALPHA = 0x7D,
    L_LUMPCOUNT = 128,
};

#pragma pack(push, 1)
struct RawLumpHeader {
    uint32_t offset, length, version, fourCC;
};
struct RawBspHeader {
    uint32_t ident, version, revision, lastLump;
    RawLumpHeader lumps[128];
};
static_assert(sizeof(RawBspHeader) == 0x810, "header");

// 16 byte oriented box used by geosets, primitives and tricoll nodes.
// Field order follows the data: at +6 the value is -cos(yaw)*32768 (so
// -32768 for yaw 0) and at +14 it is sin(yaw)*32768.
struct Bounds16 {
    int16_t origin[3];
    int16_t negCos;
    int16_t extents[3];
    int16_t sin;
};
static_assert(sizeof(Bounds16) == 16, "Bounds16");

struct GeoSet {
    uint16_t straddleGroup;
    uint16_t numPrimitives;
    uint32_t primitive;  // type:8 | index:16 | uniqueContents:8 (little endian value)
};
static_assert(sizeof(GeoSet) == 8, "GeoSet");

struct Grid {
    float scale;
    int32_t cellOffset[2];
    int32_t numCells[2];
    int32_t numStraddleGroups;
    int32_t firstBrushPlane;
};
static_assert(sizeof(Grid) == 28, "Grid");

struct GridCell {
    uint16_t firstGeoSet, numGeoSets;
};

struct TricollHeader {
    int16_t flags, textureFlags, textureData, numVertices;
    uint16_t numTriangles, numBevelIndices;
    int32_t firstVertex;
    uint32_t firstTriangle, firstNode, firstBevelIndex;
    float origin[3];
    float scale;
};
static_assert(sizeof(TricollHeader) == 44, "TricollHeader");

// Mesh as stored in R2. R1 uses the same 28 bytes, but bytes 10..11 are one
// uint16 vertex type there; R2 splits them into vertexType and cubemap.
struct MeshR2 {
    uint32_t firstMeshIndex;
    uint16_t numTriangles, firstVertex, numVertices;
    uint8_t vertexType;
    int8_t cubemap;
    uint8_t styles[4];
    int16_t luxelOrigin[2];
    uint8_t luxelOffsetMax[2];
    uint16_t materialSort;
    uint32_t flags;
};
static_assert(sizeof(MeshR2) == 28, "MeshR2");

struct Sort {
    int16_t textureData, lightmapHeader, cubemap;
    uint16_t lastVertex;
    int32_t vertexOffset;
};
static_assert(sizeof(Sort) == 12, "Sort");

struct MeshBounds {
    float origin[3], radius, extents[3], tanYaw;
};
static_assert(sizeof(MeshBounds) == 32, "MeshBounds");

struct Model {
    float mins[3], maxs[3];
    uint32_t firstMesh, numMeshes;
};
static_assert(sizeof(Model) == 32, "Model");

struct Vec3 {
    float x, y, z;
};

struct GameLumpHeader {
    uint32_t id;
    uint16_t flags, version;
    uint32_t offset, length;
};
static_assert(sizeof(GameLumpHeader) == 16, "GameLumpHeader");
#pragma pack(pop)

constexpr uint32_t kIdentRBSP = 'r' | ('B' << 8) | ('S' << 16) | ('P' << 24);
constexpr uint32_t kIdentSprp = 'p' | ('r' << 8) | ('p' << 16) | ('s' << 24);

struct Ctx {
    Options opt;
    Report* rep = nullptr;
    uint32_t revision = 0;
    Bytes lump[128];
    uint32_t version[128] = {};
    uint32_t fourCC[128] = {};
    bool present[128] = {};
    uint32_t origOffset[128] = {};
    bool gameLumpNeedsRebase = false;
    bool gridRegridded = false;  // rule 19 re-binned the collision grid (rule 18 padding is then not applied)

    // Collected hard failures; thrown together by throwIfFailed().
    std::vector<std::string> failures;

    void say(const char* fmt, ...)
#if defined(__GNUC__)
        __attribute__((format(BSPDOWN_PRINTF, 2, 3)))
#endif
        ;
    void fail(const char* fmt, ...)
#if defined(__GNUC__)
        __attribute__((format(BSPDOWN_PRINTF, 2, 3)))
#endif
        ;
    void throwIfFailed(const char* stage);
};

std::string vformat(const char* fmt, va_list ap);

template <class T>
std::vector<T> view(const Bytes& b, const char* what) {
    if (b.size() % sizeof(T) != 0) {
        throw Error(std::string(what) + ": lump size " + std::to_string(b.size()) + " is not a multiple of " +
                    std::to_string(sizeof(T)));
    }
    std::vector<T> v(b.size() / sizeof(T));
    if (!v.empty()) std::memcpy(v.data(), b.data(), b.size());
    return v;
}

template <class T>
Bytes pack(const std::vector<T>& v) {
    Bytes b(v.size() * sizeof(T));
    if (!v.empty()) std::memcpy(b.data(), v.data(), b.size());
    return b;
}

template <class T>
void appendRaw(Bytes& b, const T& v) {
    size_t o = b.size();
    b.resize(o + sizeof(T));
    std::memcpy(b.data() + o, &v, sizeof(T));
}

// Stages. Each reads and writes Ctx::lump.
void convertLighting(Ctx& c);  // rule 20 (+ rules 2, 8a): world lights, probe light indices, real-time lights
void convertRealTimeLightsFill(Ctx& c);  // rule 2 fallback: 0xFF fill
void convertLightProbeRefs(Ctx& c);
void convertTricoll(Ctx& c);
void convertStaticProps(Ctx& c);
void convertVertexFrames(Ctx& c);  // rule 11, runs after convertMeshes
void convertVertexColorAlpha(Ctx& c);  // rule 22, runs right after convertVertexFrames
void convertCubemaps(Ctx& c);  // rule 13
void convertShadow(Ctx& c);  // rule 17, shadow environments / CSM tree / shadow meshes
void convertPhysics(Ctx& c);  // rule 12
void convertPhysicsTriangles(Ctx& c);  // rule 14, header-count-aware all-absent table
bool physicsHasWorldSolids(const Bytes& lump, std::string* why);
void convertMeshes(Ctx& c);  // clipping, cubemap split, model/mesh bounds
void convertCollisionGrid(Ctx& c);
void regridCollision(Ctx& c);  // rule 19, runs right after convertCollisionGrid
void padCollisionGrid(Ctx& c);  // rule 18, runs right after convertCollisionGrid
void reportResidualBounds(Ctx& c);

// Contents remap (README.txt rule 9). Returns the R1 value for an R2 value.
uint32_t remapContentsValue(uint32_t r2);

// Axis aligned world box of an oriented Bounds16 (yaw about Z).
void boundsToAabb(const Bounds16& b, int lo[3], int hi[3]);
Bounds16 boundsFromAabb(const int lo[3], const int hi[3]);

}  // namespace detail
}  // namespace bspdown
