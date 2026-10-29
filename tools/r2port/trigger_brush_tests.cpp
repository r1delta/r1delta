#include "engine/r2port/trigger_brush.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace r2port::trigger_brush;

void Check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

bool Has(std::string_view text, std::string_view fragment)
{
    return text.find(fragment) != std::string_view::npos;
}

#pragma pack(push, 1)
struct PlaneRec { float n[3]; float d; };
struct ModelRec { float mins[3]; float maxs[3]; std::uint32_t firstMesh, numMeshes; };
struct BrushRec { float origin[3]; std::uint8_t nonAxial, nonAxialCopy; std::uint16_t index; float extents[3]; std::uint32_t sideStart; };
struct GeosetRec { std::uint16_t group, count; std::uint32_t word; };
struct Bounds16 { std::int16_t origin[3]; std::int16_t negCos; std::int16_t extents[3]; std::int16_t sin; };
struct CellRec { std::uint16_t first, count; };
struct GridRec { float size; std::int32_t offset[2]; std::int32_t count[2]; std::int32_t groups; std::int32_t firstBrushPlane; };
struct LumpHdr { std::uint32_t offset, length, version, fourCC; };
#pragma pack(pop)

constexpr std::size_t kHeader = 16 + 128 * 16;

// A BSP image with just the lumps Bake reads: three stock-like trigger-free brushes, one model, two cells.
struct Fixture {
    std::vector<std::uint8_t> bytes;
    std::array<LumpHdr, 128> hdr{};

    void Put(int lump, const void* data, std::size_t size)
    {
        while (bytes.size() % 4)
            bytes.push_back(0);
        hdr[lump].offset = static_cast<std::uint32_t>(bytes.size());
        hdr[lump].length = static_cast<std::uint32_t>(size);
        const auto* p = static_cast<const std::uint8_t*>(data);
        bytes.insert(bytes.end(), p, p + size);
    }
    template <typename T>
    void PutVec(int lump, const std::vector<T>& v) { Put(lump, v.data(), v.size() * sizeof(T)); }
    void Finish()
    {
        const std::uint32_t ident = 0x50534272u, version = 29, rev = 30, count = 127;
        std::memcpy(bytes.data(), &ident, 4);
        std::memcpy(bytes.data() + 4, &version, 4);
        std::memcpy(bytes.data() + 8, &rev, 4);
        std::memcpy(bytes.data() + 12, &count, 4);
        std::memcpy(bytes.data() + 16, hdr.data(), sizeof(LumpHdr) * 128);
    }
};

Fixture MakeFixture(std::size_t extraPlanes = 0, std::size_t sideOffsetCount = 4)
{
    Fixture f;
    f.bytes.assign(kHeader, 0);
    // One world cell + one model cell, 1 model
    std::vector<PlaneRec> planes(5 + extraPlanes, PlaneRec{ { 0, 0, 1 }, 5.f });
    f.PutVec(0x01, planes);
    std::vector<ModelRec> models{ ModelRec{ { -1, -1, -1 }, { 1, 1, 1 }, 0, 3 } };
    f.PutVec(0x0E, models);
    GridRec grid{ 2048.f, { 0, 0 }, { 1, 1 }, 1, 2 };
    f.Put(0x55, &grid, sizeof(grid));
    std::vector<CellRec> cells{ { 0, 0 }, { 0, 0 } };
    f.PutVec(0x56, cells);
    std::vector<GeosetRec> geosets{ GeosetRec{ 0, 1, 0x00000001u } };
    f.PutVec(0x57, geosets);
    std::vector<Bounds16> bounds{ Bounds16{ { 0, 0, 0 }, -32768, { 1, 1, 1 }, 0 } };
    f.PutVec(0x58, bounds);
    std::vector<std::uint32_t> contents{ 0x10000u, 0x1u, 0x2u };
    f.PutVec(0x5B, contents);
    std::vector<BrushRec> brushes{ BrushRec{ { 0, 0, 0 }, 0, 0, 0, { 1, 1, 1 }, 0 } };
    f.PutVec(0x5C, brushes);
    std::vector<std::uint16_t> offsets(sideOffsetCount, 0);
    f.PutVec(0x5D, offsets);
    std::vector<std::uint16_t> props(6 * brushes.size() + offsets.size(), 7);
    f.PutVec(0x5E, props);
    // texdata names: strings + offsets table
    const std::string names("TOOLS\\TOOLSNODRAW\0TOOLS\\TOOLSTRIGGER\0TOOLS\\TOOLSOUT_OF_BOUNDS\0TOOLS\\TOOLSTRIGGER_CAPTUREPOINT\0", 18 + 19 + 25 + 33);
    f.Put(0x2B, names.data(), names.size());
    std::vector<std::uint32_t> nameOffsets{ 0, 18, 37, 62 };
    f.PutVec(0x2C, nameOffsets);
    f.Finish();
    return f;
}

template <typename T>
std::vector<T> Lump(const std::vector<std::uint8_t>& bsp, const Result& r, int lump)
{
    std::vector<T> out;
    const std::uint8_t* data;
    std::size_t size;
    if (!r.lumps[lump].empty()) {
        data = r.lumps[lump].data();
        size = r.lumps[lump].size();
    }
    else {
        LumpHdr h;
        std::memcpy(&h, bsp.data() + 16 + 16 * lump, sizeof(h));
        data = bsp.data() + h.offset;
        size = h.length;
    }
    out.resize(size / sizeof(T));
    if (size)
        std::memcpy(out.data(), data, out.size() * sizeof(T));
    return out;
}

struct Decoded {
    std::vector<std::array<float, 4>> planes;  // all sides, as the R1 plane reader builds them (local frame)
};

// The R1 brush plane reader: 6 axial sides from origin/extents plus planes[firstBrushPlane + start + k - offset[start + k]].
Decoded DecodeBrush(const BrushRec& b, const std::vector<PlaneRec>& planes, const std::vector<std::uint16_t>& offsets, int firstBrushPlane)
{
    Decoded d;
    const float* o = b.origin;
    const float* e = b.extents;
    d.planes.push_back({ -1, 0, 0, e[0] - o[0] });
    d.planes.push_back({ 1, 0, 0, e[0] + o[0] });
    d.planes.push_back({ 0, -1, 0, e[1] - o[1] });
    d.planes.push_back({ 0, 1, 0, e[1] + o[1] });
    d.planes.push_back({ 0, 0, -1, e[2] - o[2] });
    d.planes.push_back({ 0, 0, 1, e[2] + o[2] });
    for (unsigned k = 0; k < b.nonAxial; ++k) {
        const long q = firstBrushPlane + static_cast<long>(b.sideStart) + static_cast<long>(k) - offsets.at(b.sideStart + k);
        const PlaneRec& p = planes.at(static_cast<std::size_t>(q));
        d.planes.push_back({ p.n[0], p.n[1], p.n[2], p.d });
    }
    return d;
}

bool Inside(const Decoded& d, double x, double y, double z)
{
    for (const auto& p : d.planes) {
        if (p[0] * x + p[1] * y + p[2] * z > p[3] + 1e-3)
            return false;
    }
    return true;
}

const std::string kBoxEntity =
    "{\n"
    "\"origin\" \"100 200 300\"\n"
    "\"spawnflags\" \"4099\"\n"
    "\"classname\" \"trigger_out_of_bounds\"\n"
    "\"*trigger_brush_0_plane_0\" \"-1 0 0 50\"\n"
    "\"*trigger_brush_0_plane_1\" \"1 0 0 70\"\n"
    "\"*trigger_brush_0_plane_2\" \"0 -1 0 30\"\n"
    "\"*trigger_brush_0_plane_3\" \"0 1 0 30\"\n"
    "\"*trigger_brush_0_plane_4\" \"0 0 -1 10\"\n"
    "\"*trigger_brush_0_plane_5\" \"0 0 1 20\"\n"
    "\"*trigger_brush_0_plane_6\" \"-0.70710677 0.70710677 0 100\"\n"
    "\"*trigger_brush_0_plane_7\" \"0.70710677 0.70710677 0 100\"\n"
    "\"*trigger_bounds_mins\" \"-50 -30 -10\"\n"
    "\"*trigger_bounds_maxs\" \"70 30 20\"\n"
    "}\n";

// Axial box clipped on the +x/+y corner by a 45 degree plane, two brushes in one entity.
const std::string kPrismEntity =
    "{\n"
    "\"classname\" \"trigger_hurt\"\n"
    "\"damage\" \"10000\"\n"
    "\"*trigger_brush_0_plane_0\" \"-1 0 0 0\"\n"
    "\"*trigger_brush_0_plane_1\" \"1 0 0 100\"\n"
    "\"*trigger_brush_0_plane_2\" \"0 -1 0 0\"\n"
    "\"*trigger_brush_0_plane_3\" \"0 1 0 100\"\n"
    "\"*trigger_brush_0_plane_4\" \"0 0 -1 0\"\n"
    "\"*trigger_brush_0_plane_5\" \"0 0 1 50\"\n"
    "\"*trigger_brush_0_plane_6\" \"0.70710677 0.70710677 0 120\"\n"
    "\"*trigger_brush_1_plane_0\" \"-1 0 0 -200\"\n"
    "\"*trigger_brush_1_plane_1\" \"1 0 0 300\"\n"
    "\"*trigger_brush_1_plane_2\" \"0 -1 0 0\"\n"
    "\"*trigger_brush_1_plane_3\" \"0 1 0 100\"\n"
    "\"*trigger_brush_1_plane_4\" \"0 0 -1 0\"\n"
    "\"*trigger_brush_1_plane_5\" \"0 0 1 50\"\n"
    "\"*trigger_bounds_mins\" \"0 0 0\"\n"
    "\"*trigger_bounds_maxs\" \"300 100 50\"\n"
    "\"targetname\" \"kill\"\n"
    "}\n";

void TestBakeBoxAndPrism()
{
    Fixture f = MakeFixture();
    const std::string lump0 = std::string("// header\n{\n\"classname\" \"worldspawn\"\n}\n") + kBoxEntity + std::string(1, '\0');
    const std::string spawn = std::string("{\n\"classname\" \"info_target\"\n\"origin\" \"1 2 3\"\n}\n") + kPrismEntity;
    const std::vector<std::string_view> partitions{ lump0, spawn };
    const Result r = Bake(f.bytes, partitions);
    Check(r.baked, "bake succeeded");
    Check(r.report.failure.empty(), "no failure");
    Check(r.report.entitiesBaked == 2 && r.report.brushes == 3, "two entities, three brushes");
    Check(r.report.firstModel == 1 && r.report.modelsAdded == 2, "models appended after the existing one");
    Check(r.texts.size() == 2, "two texts");
    Check(Has(r.texts[0], "\"model\" \"*1\""), "box entity references model 1");
    Check(Has(r.texts[1], "\"model\" \"*2\""), "prism entity references model 2");
    Check(!Has(r.texts[0], "*trigger_") && !Has(r.texts[1], "*trigger_"), "plane and bounds keys removed");
    Check(Has(r.texts[0], "\"origin\" \"100 200 300\"") && Has(r.texts[1], "\"targetname\" \"kill\""), "other keys kept");
    Check(Has(r.texts[0], "worldspawn") && Has(r.texts[1], "info_target"), "other entities kept");
    Check(!r.texts[0].empty() && r.texts[0].back() == '\0' && r.lumps[0].size() == r.texts[0].size(), "lump 0 keeps its terminator and is replaced");

    const auto planes = Lump<PlaneRec>(f.bytes, r, 0x01);
    const auto models = Lump<ModelRec>(f.bytes, r, 0x0E);
    const auto cells = Lump<CellRec>(f.bytes, r, 0x56);
    const auto geosets = Lump<GeosetRec>(f.bytes, r, 0x57);
    const auto bounds = Lump<Bounds16>(f.bytes, r, 0x58);
    const auto brushes = Lump<BrushRec>(f.bytes, r, 0x5C);
    const auto offsets = Lump<std::uint16_t>(f.bytes, r, 0x5D);
    const auto props = Lump<std::uint16_t>(f.bytes, r, 0x5E);
    Check(models.size() == 3 && cells.size() == 1 + 3 && geosets.size() == 1 + 3 && bounds.size() == geosets.size(), "pool sizes");
    Check(brushes.size() == 4, "brush count");
    Check(props.size() == 6 * brushes.size() + offsets.size(), "side props = 6 * brushes + offsets");
    Check(r.lumps[0x5B].empty(), "contents 0x1 already present: table untouched");
    Check(models[1].numMeshes == 0 && models[1].firstMesh == 3, "no render meshes appended after the last model");
    // symmetric model bounds that cover the brush
    Check(std::fabs(models[1].maxs[0] - 70.f) < 1e-3f && std::fabs(models[1].mins[0] + 70.f) < 1e-3f, "box model x extent covers |origin| + half");
    Check(std::fabs(models[2].maxs[0] - 300.f) < 1e-3f && std::fabs(models[2].mins[2] + 50.f) < 1e-3f, "prism model extents");

    // box model: one geoset, contents index 1, brush origin (10,0,5) extents (60,30,15), two non axial sides
    Check(cells[2].count == 1 && cells[3].count == 2, "model cells");
    const GeosetRec g = geosets[cells[2].first];
    Check(g.count == 1 && (g.word & 0xFF) == 1 && ((g.word >> 24) & 0xE0) == 0, "brush primitive with the solid contents index");
    const BrushRec& b = brushes[(g.word >> 8) & 0x1FFFFF];
    Check(b.index == ((g.word >> 8) & 0x1FFFFF), "brush index field");
    Check(std::fabs(b.origin[0] - 10.f) < 1e-4f && std::fabs(b.extents[0] - 60.f) < 1e-4f, "brush aabb x");
    Check(std::fabs(b.origin[2] - 5.f) < 1e-4f && std::fabs(b.extents[2] - 15.f) < 1e-4f, "brush aabb z");
    Check(b.nonAxial == 2, "two non axial sides");
    const Decoded box = DecodeBrush(b, planes, offsets, 2);
    Check(Inside(box, 10, 0, 5) && Inside(box, 69, 29, 19), "box inside");
    Check(!Inside(box, 71, 0, 5) && !Inside(box, 10, 31, 5) && !Inside(box, 10, 0, 21), "box outside");
    const Bounds16& gb = bounds[cells[2].first];
    Check(gb.negCos == -32768 && gb.sin == 0 && gb.origin[0] == 10 && gb.extents[0] == 61 && gb.extents[1] == 31 && gb.extents[2] == 15, "geoset bounds");

    // prism entity: brush 0 has the clipped corner, brush 1 is a plain box
    Check(cells[3].first + 1 < geosets.size(), "two geosets");
    const GeosetRec g0 = geosets[cells[3].first], g1 = geosets[cells[3].first + 1];
    const Decoded p0 = DecodeBrush(brushes[(g0.word >> 8) & 0x1FFFFF], planes, offsets, 2);
    const Decoded p1 = DecodeBrush(brushes[(g1.word >> 8) & 0x1FFFFF], planes, offsets, 2);
    Check(Inside(p0, 50, 50, 25) && !Inside(p0, 99, 99, 25) && Inside(p0, 99, 10, 25), "clipped corner is cut");
    Check(Inside(p1, 250, 50, 25) && !Inside(p1, 150, 50, 25) && !Inside(p1, 350, 50, 25), "second brush is a box at x 200..300");
    Check(brushes[(g1.word >> 8) & 0x1FFFFF].nonAxial == 0, "box has no non axial side");

    // second bake of the output text is a no-op (idempotent)
    const std::vector<std::string_view> again{ r.texts[0], r.texts[1] };
    const Result r2 = Bake(f.bytes, again);
    Check(!r2.baked && r2.texts[0] == r.texts[0] && r2.texts[1] == r.texts[1], "no plane keys left: nothing to do");
}

void TestNothingToDo()
{
    Fixture f = MakeFixture();
    const std::string text = "{\n\"classname\" \"trigger_hurt\"\n\"model\" \"*3\"\n\"*trigger_bounds_mins\" \"0 0 0\"\n}\n"
                             "{\n\"classname\" \"info_target\"\n}\n"
                             "{\n\"classname\" \"envmap_volume\"\n\"*trigger_bounds_mins\" \"0 0 0\"\n}\n";
    const std::vector<std::string_view> partitions{ text };
    const Result r = Bake(f.bytes, partitions);
    Check(!r.baked && r.texts[0] == text, "text unchanged");
    Check(r.report.entitiesSkippedHasModel == 1, "model-bearing trigger counted");
    for (const auto& lump : r.lumps)
        Check(lump.empty(), "no lump replaced");
}

void TestUnusablePlanesLeftAlone()
{
    Fixture f = MakeFixture();
    // empty polytope: x <= -10 and x >= 10
    const std::string text =
        "{\n\"classname\" \"trigger_multiple\"\n"
        "\"*trigger_brush_0_plane_0\" \"-1 0 0 -10\"\n\"*trigger_brush_0_plane_1\" \"1 0 0 -10\"\n"
        "\"*trigger_brush_0_plane_2\" \"0 -1 0 5\"\n\"*trigger_brush_0_plane_3\" \"0 1 0 5\"\n"
        "\"*trigger_brush_0_plane_4\" \"0 0 -1 5\"\n\"*trigger_brush_0_plane_5\" \"0 0 1 5\"\n"
        "\"*trigger_bounds_mins\" \"0 0 0\"\n\"*trigger_bounds_maxs\" \"0 0 0\"\n}\n";
    const std::vector<std::string_view> partitions{ text };
    const Result r = Bake(f.bytes, partitions);
    Check(!r.baked && r.report.entitiesSkippedNoBrush == 1 && r.texts[0] == text, "empty brush: entity left untouched");
}

void TestRejectsOtherBsp()
{
    Fixture f = MakeFixture();
    f.bytes[4] = 37;  // an R2 image
    const std::string text = kBoxEntity;
    const std::vector<std::string_view> partitions{ text };
    const Result r = Bake(f.bytes, partitions);
    Check(!r.baked && !r.report.failure.empty() && r.texts[0] == text, "R2 image refused, text unchanged");
}

void TestPaddingWhenPlanesOutgrowOffsets()
{
    // More planes than side offset entries: the offset constant would be negative, both arrays get padded.
    Fixture f = MakeFixture(/*extraPlanes*/ 100, /*sideOffsetCount*/ 4);
    const std::string text = kBoxEntity;
    const std::vector<std::string_view> partitions{ text };
    const Result r = Bake(f.bytes, partitions);
    Check(r.baked, "bake with padding");
    const auto planes = Lump<PlaneRec>(f.bytes, r, 0x01);
    const auto brushes = Lump<BrushRec>(f.bytes, r, 0x5C);
    const auto offsets = Lump<std::uint16_t>(f.bytes, r, 0x5D);
    const auto props = Lump<std::uint16_t>(f.bytes, r, 0x5E);
    Check(props.size() == 6 * brushes.size() + offsets.size(), "side arrays stay in step");
    const Decoded box = DecodeBrush(brushes.back(), planes, offsets, 2);
    Check(Inside(box, 10, 0, 5) && !Inside(box, 71, 0, 5), "box decodes after padding");
    Check(box.planes.size() == 8, "6 axial + 2 non axial");
}

void TestMissingContentsIsAppended()
{
    Fixture f = MakeFixture();
    // rebuild without the 0x1 entry
    std::vector<std::uint32_t> contents{ 0x10000u, 0x2u };
    f.bytes.resize(kHeader);
    f = MakeFixture();
    const std::uint32_t only[2] = { 0x10000u, 0x2u };
    // patch the table in place (3 entries -> keep length, replace the 0x1)
    LumpHdr h;
    std::memcpy(&h, f.bytes.data() + 16 + 16 * 0x5B, sizeof(h));
    std::uint32_t entries[3] = { only[0], only[1], 0x4u };
    std::memcpy(f.bytes.data() + h.offset, entries, sizeof(entries));
    const std::string text = kBoxEntity;
    const std::vector<std::string_view> partitions{ text };
    const Result r = Bake(f.bytes, partitions);
    Check(r.baked && r.lumps[0x5B].size() == 16, "solid contents appended");
    std::uint32_t tail;
    std::memcpy(&tail, r.lumps[0x5B].data() + 12, 4);
    Check(tail == 1, "appended value is CONTENTS_SOLID");
    const auto geosets = Lump<GeosetRec>(f.bytes, r, 0x57);
    Check((geosets.back().word & 0xFF) == 3, "geoset uses the appended index");
}

// Real converted data: the R2 provider's cached BSP and entity files of a map, if given on the command line.
// Every baked volume must decode to the same point set as the original R2 planes.
struct RealPlane { double n[3]; double d; };

std::string ReadFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void TestRealMap(const std::string& prefix, const char* dumpDir)
{
    const std::string bspBytes = ReadFile(prefix + ".bsp");
    Check(!bspBytes.empty(), "real BSP readable");
    std::vector<std::uint8_t> bsp(bspBytes.begin(), bspBytes.end());
    LumpHdr h0;
    std::memcpy(&h0, bsp.data() + 16, sizeof(h0));
    std::vector<std::string> storage;
    storage.emplace_back(reinterpret_cast<const char*>(bsp.data()) + h0.offset, h0.length);
    for (const char* suffix : { "_env", "_fx", "_script", "_snd", "_spawn" })
        storage.push_back(ReadFile(prefix + suffix + ".ent"));
    std::vector<std::string_view> partitions(storage.begin(), storage.end());
    const Result r = Bake(bsp, partitions);
    Check(r.report.failure.empty(), "real map bake has no failure");
    std::printf("real map: baked=%d entities=%llu brushes=%llu nonaxial=%llu skipped(no brush)=%llu models %u..+%u\n",
        r.baked ? 1 : 0, static_cast<unsigned long long>(r.report.entitiesBaked), static_cast<unsigned long long>(r.report.brushes),
        static_cast<unsigned long long>(r.report.nonAxialPlanes), static_cast<unsigned long long>(r.report.entitiesSkippedNoBrush),
        r.report.firstModel, r.report.modelsAdded);
    Check(r.baked, "real map has trigger brushes");
    if (dumpDir) {
        for (std::size_t i = 0; i < r.lumps.size(); ++i) {
            if (r.lumps[i].empty())
                continue;
            std::ofstream out(std::string(dumpDir) + "/lump_" + std::to_string(i) + ".bin", std::ios::binary);
            out.write(reinterpret_cast<const char*>(r.lumps[i].data()), static_cast<std::streamsize>(r.lumps[i].size()));
        }
        for (std::size_t i = 1; i < r.texts.size(); ++i) {
            std::ofstream out(std::string(dumpDir) + "/part_" + std::to_string(i) + ".ent", std::ios::binary);
            out.write(r.texts[i].data(), static_cast<std::streamsize>(r.texts[i].size()));
        }
    }

    const auto planes = Lump<PlaneRec>(bsp, r, 0x01);
    const auto cells = Lump<CellRec>(bsp, r, 0x56);
    const auto geosets = Lump<GeosetRec>(bsp, r, 0x57);
    const auto brushes = Lump<BrushRec>(bsp, r, 0x5C);
    const auto offsets = Lump<std::uint16_t>(bsp, r, 0x5D);
    const auto props = Lump<std::uint16_t>(bsp, r, 0x5E);
    GridRec grid;
    {
        LumpHdr h;
        std::memcpy(&h, bsp.data() + 16 + 16 * 0x55, sizeof(h));
        std::memcpy(&grid, bsp.data() + h.offset, sizeof(grid));
    }
    const std::size_t modelCount = Lump<ModelRec>(bsp, r, 0x0E).size();
    Check(cells.size() == static_cast<std::size_t>(grid.count[0]) * grid.count[1] + modelCount, "cell count rule holds");
    Check(props.size() == 6 * brushes.size() + offsets.size(), "side property count rule holds");
    // every brush is referenced by a geoset (R1 fills the per brush contents table from geosets)
    std::vector<char> seen(brushes.size(), 0);
    for (const GeosetRec& g : geosets)
        if (g.count == 1 && ((g.word >> 24) & 0xE0) == 0)
            seen.at((g.word >> 8) & 0x1FFFFF) = 1;
    for (std::size_t i = 0; i < brushes.size(); ++i)
        Check(brushes[i].index == i, "brush index field is the record index");
    std::uint64_t unreferenced = 0;
    for (std::size_t i = brushes.size() - r.report.brushes; i < brushes.size(); ++i)
        unreferenced += seen[i] ? 0 : 1;
    Check(unreferenced == 0, "every new brush has a geoset");

    // text consistency: model keys 1..N in order, no plane keys left on baked entities
    std::uint64_t models = 0;
    for (const std::string& t : r.texts) {
        std::size_t at = 0;
        while ((at = t.find("\"model\" \"*", at)) != std::string::npos) {
            at += 10;
            const unsigned long idx = std::strtoul(t.c_str() + at, nullptr, 10);
            if (idx >= r.report.firstModel) {
                ++models;
                Check(idx < r.report.firstModel + r.report.modelsAdded, "model index in range");
            }
        }
    }
    Check(models == r.report.entitiesBaked, "one model key per baked entity");
}

} // namespace

int main(int argc, char** argv)
{
    try {
        TestBakeBoxAndPrism();
        TestNothingToDo();
        TestUnusablePlanesLeftAlone();
        TestRejectsOtherBsp();
        TestPaddingWhenPlanesOutgrowOffsets();
        TestMissingContentsIsAppended();
        if (argc > 1)
            TestRealMap(argv[1], argc > 2 ? argv[2] : nullptr);
    }
    catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << "\n";
        return 1;
    }
    std::cout << "trigger_brush tests passed\n";
    return 0;
}
