#include "trigger_brush.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <utility>

namespace r2port::trigger_brush {
namespace {

constexpr std::size_t kHeaderSize = 16 + kBspLumpCount * 16;
constexpr std::uint32_t kRBspIdent = 0x50534272u;  // 'rBSP'
constexpr std::uint32_t kR1Version = 29;

constexpr int kLumpEntities = 0x00;
constexpr int kLumpPlanes = 0x01;
constexpr int kLumpTexData = 0x02;
constexpr int kLumpModels = 0x0E;
constexpr int kLumpTexDataStrings = 0x2B;
constexpr int kLumpTexDataOffsets = 0x2C;
constexpr int kLumpGrid = 0x55;
constexpr int kLumpCells = 0x56;
constexpr int kLumpGeosets = 0x57;
constexpr int kLumpGeosetBounds = 0x58;
constexpr int kLumpUniqueContents = 0x5B;
constexpr int kLumpBrushes = 0x5C;
constexpr int kLumpSideOffsets = 0x5D;
constexpr int kLumpSideProps = 0x5E;

constexpr std::uint32_t kContentsSolid = 0x1;
constexpr std::size_t kMaxUniqueContents = 256;
constexpr std::size_t kMaxPoolIndex = 65535;  // geoset index and brush index fields are 16 bit

#pragma pack(push, 1)
struct LumpHeader {
    std::uint32_t offset;
    std::uint32_t length;
    std::uint32_t version;
    std::uint32_t fourCC;
};
struct GridLump {
    float cellSize;
    std::int32_t cellOffset[2];
    std::int32_t cellCount[2];
    std::int32_t groupCount;
    std::int32_t firstBrushPlane;
};
struct ModelRecord {
    float mins[3];
    float maxs[3];
    std::uint32_t firstMesh;
    std::uint32_t numMeshes;
};
struct BrushRecord {
    float origin[3];
    std::uint8_t numNonAxial;
    std::uint8_t numNonAxialCopy;  // stock R1 data stores a value >= numNonAxial here; the plane reader ignores it
    std::uint16_t index;           // the record's own index
    float extents[3];
    std::uint32_t sideStart;       // start in the side offset lump; the side property run starts at 6 * index + sideStart
};
struct GeosetRecord {
    std::uint16_t group;
    std::uint16_t count;
    std::uint32_t word;
};
struct Bounds16 {
    std::int16_t origin[3];
    std::int16_t negCos;
    std::int16_t extents[3];
    std::int16_t sin;
};
struct CellRecord {
    std::uint16_t first;
    std::uint16_t count;
};
struct PlaneRecord {
    float normal[3];
    float dist;
};
#pragma pack(pop)

static_assert(sizeof(LumpHeader) == 16, "lump header");
static_assert(sizeof(GridLump) == 28, "grid lump");
static_assert(sizeof(ModelRecord) == 32, "model record");
static_assert(sizeof(BrushRecord) == 32, "brush record");
static_assert(sizeof(GeosetRecord) == 8, "geoset record");
static_assert(sizeof(Bounds16) == 16, "geoset bounds");
static_assert(sizeof(CellRecord) == 4, "cell record");
static_assert(sizeof(PlaneRecord) == 16, "plane record");

struct Vec3 {
    double v[3];
};

struct Plane {
    double n[3];
    double d;
};

// --- entity text scanning ---------------------------------------------------------------------------

struct Span {
    std::size_t begin = 0;
    std::size_t end = 0;
};

struct Pair {
    std::string_view key;
    std::string_view value;
    Span span;  // key opening quote .. value closing quote, extended over indentation and one line break
};

struct EntityBlock {
    Span span;  // '{' .. '}' inclusive
    std::size_t closeBrace = 0;
    std::vector<Pair> pairs;
};

bool IsBlank(char c) { return c == ' ' || c == '\t'; }

// Scans R2/R1 entity text: blocks of quoted key/value pairs. Comments (//) are only recognised outside
// quotes. Returns false when the structure is not balanced.
bool ScanEntities(std::string_view text, std::vector<EntityBlock>& blocks)
{
    std::size_t i = 0;
    const std::size_t n = text.size();
    while (i < n) {
        const char c = text[i];
        if (c == '/' && i + 1 < n && text[i + 1] == '/') {
            while (i < n && text[i] != '\n')
                ++i;
            continue;
        }
        if (c == '\0')
            break;
        if (c != '{') {
            ++i;
            continue;
        }

        EntityBlock block;
        block.span.begin = i;
        ++i;
        bool closed = false;
        while (i < n) {
            const char d = text[i];
            if (d == '}') {
                block.closeBrace = i;
                block.span.end = i + 1;
                ++i;
                closed = true;
                break;
            }
            if (d == '/' && i + 1 < n && text[i + 1] == '/') {
                while (i < n && text[i] != '\n')
                    ++i;
                continue;
            }
            if (d != '"') {
                if (d == '{')
                    return false;
                ++i;
                continue;
            }
            // key
            const std::size_t keyOpen = i;
            const std::size_t keyBegin = ++i;
            while (i < n && text[i] != '"')
                ++i;
            if (i >= n)
                return false;
            const std::string_view key = text.substr(keyBegin, i - keyBegin);
            ++i;
            while (i < n && (IsBlank(text[i]) || text[i] == '\r' || text[i] == '\n'))
                ++i;
            if (i >= n || text[i] != '"')
                return false;
            const std::size_t valueBegin = ++i;
            while (i < n && text[i] != '"')
                ++i;
            if (i >= n)
                return false;
            const std::string_view value = text.substr(valueBegin, i - valueBegin);
            ++i;

            Pair pair;
            pair.key = key;
            pair.value = value;
            std::size_t spanBegin = keyOpen;
            while (spanBegin > 0 && IsBlank(text[spanBegin - 1]))
                --spanBegin;
            std::size_t spanEnd = i;
            while (spanEnd < n && IsBlank(text[spanEnd]))
                ++spanEnd;
            if (spanEnd < n && text[spanEnd] == '\r')
                ++spanEnd;
            if (spanEnd < n && text[spanEnd] == '\n')
                ++spanEnd;
            pair.span = { spanBegin, spanEnd };
            block.pairs.push_back(pair);
        }
        if (!closed)
            return false;
        blocks.push_back(std::move(block));
    }
    return true;
}

const Pair* FindPair(const EntityBlock& block, std::string_view key)
{
    for (const Pair& pair : block.pairs) {
        if (pair.key == key)
            return &pair;
    }
    return nullptr;
}

bool StartsWith(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

bool ParseDouble(std::string_view text, double& out)
{
    while (!text.empty() && IsBlank(text.front()))
        text.remove_prefix(1);
    const char* first = text.data();
    const char* last = text.data() + text.size();
    const auto result = std::from_chars(first, last, out);
    if (result.ec != std::errc())
        return false;
    return result.ptr != first;
}

bool ParsePlane(std::string_view value, Plane& plane)
{
    double parsed[4];
    for (int i = 0; i < 4; ++i) {
        while (!value.empty() && IsBlank(value.front()))
            value.remove_prefix(1);
        const char* first = value.data();
        const char* last = value.data() + value.size();
        const auto result = std::from_chars(first, last, parsed[i]);
        if (result.ec != std::errc() || result.ptr == first)
            return false;
        value.remove_prefix(static_cast<std::size_t>(result.ptr - first));
    }
    for (int i = 0; i < 3; ++i)
        plane.n[i] = parsed[i];
    plane.d = parsed[3];
    return std::isfinite(plane.n[0]) && std::isfinite(plane.n[1]) && std::isfinite(plane.n[2]) && std::isfinite(plane.d);
}

// "*trigger_brush_<b>_plane_<p>"
bool ParseBrushKey(std::string_view key, int& brush, int& plane)
{
    constexpr std::string_view prefix = "*trigger_brush_";
    constexpr std::string_view middle = "_plane_";
    if (!StartsWith(key, prefix))
        return false;
    key.remove_prefix(prefix.size());
    const std::size_t split = key.find(middle);
    if (split == std::string_view::npos)
        return false;
    const std::string_view b = key.substr(0, split);
    const std::string_view p = key.substr(split + middle.size());
    if (b.empty() || p.empty())
        return false;
    auto toInt = [](std::string_view s, int& out) {
        if (s.size() > 6)
            return false;
        int value = 0;
        for (char c : s) {
            if (c < '0' || c > '9')
                return false;
            value = value * 10 + (c - '0');
        }
        out = value;
        return true;
    };
    return toInt(b, brush) && toInt(p, plane);
}

bool IsTriggerBrushKey(std::string_view key)
{
    return StartsWith(key, "*trigger_brush_") || key == "*trigger_bounds_mins" || key == "*trigger_bounds_maxs";
}

// --- brush building ---------------------------------------------------------------------------------

struct Brush {
    double mins[3];
    double maxs[3];
    std::vector<Plane> nonAxial;
};

// Polytope vertices from all plane triples (planes are few: a trigger has at most a few dozen).
bool HullAabb(const std::vector<Plane>& planes, double mins[3], double maxs[3])
{
    bool any = false;
    const std::size_t count = planes.size();
    for (std::size_t i = 0; i + 2 < count; ++i) {
        for (std::size_t j = i + 1; j + 1 < count; ++j) {
            const double* a = planes[i].n;
            const double* b = planes[j].n;
            for (std::size_t k = j + 1; k < count; ++k) {
                const double* c = planes[k].n;
                const double det = a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0])
                    + a[2] * (b[0] * c[1] - b[1] * c[0]);
                if (std::fabs(det) < 1e-6)
                    continue;
                const double bc[3] = { b[1] * c[2] - b[2] * c[1], b[2] * c[0] - b[0] * c[2], b[0] * c[1] - b[1] * c[0] };
                const double ca[3] = { c[1] * a[2] - c[2] * a[1], c[2] * a[0] - c[0] * a[2], c[0] * a[1] - c[1] * a[0] };
                const double ab[3] = { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
                double x[3];
                for (int t = 0; t < 3; ++t)
                    x[t] = (planes[i].d * bc[t] + planes[j].d * ca[t] + planes[k].d * ab[t]) / det;
                bool inside = true;
                for (const Plane& p : planes) {
                    if (p.n[0] * x[0] + p.n[1] * x[1] + p.n[2] * x[2] > p.d + 0.05) {
                        inside = false;
                        break;
                    }
                }
                if (!inside)
                    continue;
                if (!any) {
                    for (int t = 0; t < 3; ++t)
                        mins[t] = maxs[t] = x[t];
                    any = true;
                }
                else {
                    for (int t = 0; t < 3; ++t) {
                        mins[t] = std::min(mins[t], x[t]);
                        maxs[t] = std::max(maxs[t], x[t]);
                    }
                }
            }
        }
    }
    return any;
}

// Splits axial planes (they become the record AABB, which R1 turns back into 6 sides) from the rest and
// checks the polytope is a real solid. Axial planes that are not tight are kept as they are: a redundant
// bounding plane does not change the intersection.
bool MakeBrush(const std::vector<Plane>& planes, Brush& brush)
{
    if (planes.size() < 4)
        return false;
    double hullMin[3], hullMax[3];
    if (!HullAabb(planes, hullMin, hullMax))
        return false;

    bool have[6] = { false, false, false, false, false, false };
    double axial[6] = { 0, 0, 0, 0, 0, 0 };
    brush.nonAxial.clear();
    for (const Plane& p : planes) {
        bool isAxial = false;
        for (int a = 0; a < 3 && !isAxial; ++a) {
            if (std::fabs(std::fabs(p.n[a]) - 1.0) >= 1e-4)
                continue;
            bool others = true;
            for (int b = 0; b < 3; ++b) {
                if (b != a && std::fabs(p.n[b]) >= 1e-4)
                    others = false;
            }
            if (!others)
                continue;
            const int side = 2 * a + (p.n[a] > 0 ? 1 : 0);  // 2a = -axis side, 2a + 1 = +axis side
            if (!have[side] || p.d < axial[side]) {
                axial[side] = p.d;
                have[side] = true;
            }
            isAxial = true;
        }
        if (!isAxial)
            brush.nonAxial.push_back(p);
    }
    for (int a = 0; a < 3; ++a) {
        brush.mins[a] = have[2 * a] ? -axial[2 * a] : hullMin[a];
        brush.maxs[a] = have[2 * a + 1] ? axial[2 * a + 1] : hullMax[a];
        if (!(brush.maxs[a] > brush.mins[a]))
            return false;
    }
    return brush.nonAxial.size() <= 255;
}

// --- BSP access -------------------------------------------------------------------------------------

struct BspView {
    const std::vector<std::uint8_t>* bytes = nullptr;
    std::array<LumpHeader, kBspLumpCount> headers{};

    bool Open(const std::vector<std::uint8_t>& bsp, std::string& why)
    {
        bytes = &bsp;
        if (bsp.size() < kHeaderSize) {
            why = "BSP smaller than its header";
            return false;
        }
        std::uint32_t ident, version;
        std::memcpy(&ident, bsp.data(), 4);
        std::memcpy(&version, bsp.data() + 4, 4);
        if (ident != kRBspIdent || version != kR1Version) {
            why = "not a converted R1 (rBSP 29) image";
            return false;
        }
        for (std::size_t i = 0; i < kBspLumpCount; ++i) {
            std::memcpy(&headers[i], bsp.data() + 16 + i * sizeof(LumpHeader), sizeof(LumpHeader));
            const LumpHeader& h = headers[i];
            if (h.offset > bsp.size() || h.length > bsp.size() - h.offset) {
                why = "lump outside the file";
                return false;
            }
        }
        return true;
    }

    const std::uint8_t* Data(int lump) const { return bytes->data() + headers[lump].offset; }
    std::size_t Size(int lump) const { return headers[lump].length; }
};

template <typename T>
T ReadAt(const std::uint8_t* base, std::size_t index)
{
    T value;
    std::memcpy(&value, base + index * sizeof(T), sizeof(T));
    return value;
}

template <typename T>
void Append(std::vector<std::uint8_t>& lump, const T& record)
{
    const std::size_t at = lump.size();
    lump.resize(at + sizeof(T));
    std::memcpy(lump.data() + at, &record, sizeof(T));
}

std::vector<std::uint8_t> Copy(const BspView& view, int lump)
{
    return std::vector<std::uint8_t>(view.Data(lump), view.Data(lump) + view.Size(lump));
}

bool EqualsNoCase(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

// Index of the texdata entry whose material name is `name`, or -1.
int FindTexData(const BspView& view, std::string_view name)
{
    const std::size_t count = view.Size(kLumpTexDataOffsets) / 4;
    const std::size_t strings = view.Size(kLumpTexDataStrings);
    const char* data = reinterpret_cast<const char*>(view.Data(kLumpTexDataStrings));
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint32_t offset = ReadAt<std::uint32_t>(view.Data(kLumpTexDataOffsets), i);
        if (offset >= strings)
            continue;
        const std::size_t length = strnlen(data + offset, strings - offset);
        if (EqualsNoCase(std::string_view(data + offset, length), name))
            return static_cast<int>(i);
    }
    return -1;
}

std::int16_t ClampS16(double v)
{
    return static_cast<std::int16_t>(std::max(-32768.0, std::min(32767.0, v)));
}

} // namespace

Result Bake(const std::vector<std::uint8_t>& bsp, const std::vector<std::string_view>& partitions)
{
    Result result;
    result.texts.reserve(partitions.size());
    for (const std::string_view text : partitions)
        result.texts.emplace_back(text);

    BspView view;
    std::string why;
    auto fail = [&](const std::string& reason) {
        result.baked = false;
        result.report.failure = reason;
        result.texts.clear();
        for (const std::string_view text : partitions)
            result.texts.emplace_back(text);
        for (auto& lump : result.lumps)
            lump.clear();
        return std::move(result);
    };

    // Which entities need a model? Scan first so a map without trigger brushes costs almost nothing.
    struct Planned {
        std::size_t partition;
        std::size_t block;
        std::vector<Brush> brushes;
        int texture;  // 0 trigger, 1 out of bounds, 2 capture point
    };
    std::vector<std::vector<EntityBlock>> scanned(partitions.size());
    std::vector<Planned> planned;
    for (std::size_t p = 0; p < partitions.size(); ++p) {
        if (!ScanEntities(partitions[p], scanned[p])) {
            return fail("entity text of partition " + std::to_string(p) + " is not balanced");
        }
        for (std::size_t b = 0; b < scanned[p].size(); ++b) {
            const EntityBlock& block = scanned[p][b];
            const Pair* classname = FindPair(block, "classname");
            if (!classname || !StartsWith(classname->value, "trigger_"))
                continue;
            if (!FindPair(block, "*trigger_bounds_mins"))
                continue;
            if (FindPair(block, "model")) {
                ++result.report.entitiesSkippedHasModel;
                continue;
            }

            std::map<int, std::map<int, Plane>> byBrush;
            for (const Pair& pair : block.pairs) {
                int brushIndex, planeIndex;
                if (!ParseBrushKey(pair.key, brushIndex, planeIndex))
                    continue;
                Plane plane;
                if (!ParsePlane(pair.value, plane))
                    continue;
                byBrush[brushIndex][planeIndex] = plane;
            }
            Planned plan;
            plan.partition = p;
            plan.block = b;
            plan.texture = classname->value == "trigger_out_of_bounds" ? 1 : (classname->value == "trigger_capture_point" ? 2 : 0);
            for (const auto& entry : byBrush) {
                std::vector<Plane> planes;
                for (const auto& planeEntry : entry.second)
                    planes.push_back(planeEntry.second);
                Brush brush;
                if (MakeBrush(planes, brush))
                    plan.brushes.push_back(std::move(brush));
            }
            if (plan.brushes.empty()) {
                ++result.report.entitiesSkippedNoBrush;
                continue;
            }
            planned.push_back(std::move(plan));
        }
    }
    if (planned.empty())
        return result;

    if (!view.Open(bsp, why))
        return fail(why);

    for (int lump : { kLumpPlanes, kLumpModels, kLumpGrid, kLumpCells, kLumpGeosets, kLumpGeosetBounds, kLumpUniqueContents,
             kLumpBrushes, kLumpSideOffsets, kLumpSideProps }) {
        if (!view.Size(lump))
            return fail("lump " + std::to_string(lump) + " is missing");
    }
    if (view.Size(kLumpGrid) != sizeof(GridLump) || view.Size(kLumpPlanes) % sizeof(PlaneRecord)
        || view.Size(kLumpModels) % sizeof(ModelRecord) || view.Size(kLumpCells) % sizeof(CellRecord)
        || view.Size(kLumpGeosets) % sizeof(GeosetRecord) || view.Size(kLumpGeosetBounds) % sizeof(Bounds16)
        || view.Size(kLumpBrushes) % sizeof(BrushRecord) || view.Size(kLumpSideOffsets) % 2
        || view.Size(kLumpSideProps) % 2 || view.Size(kLumpUniqueContents) % 4)
        return fail("a collision lump has an unexpected size");

    const GridLump grid = ReadAt<GridLump>(view.Data(kLumpGrid), 0);
    const std::size_t modelCount = view.Size(kLumpModels) / sizeof(ModelRecord);
    const std::size_t brushCount = view.Size(kLumpBrushes) / sizeof(BrushRecord);
    const std::size_t geosetCount = view.Size(kLumpGeosets) / sizeof(GeosetRecord);
    const std::size_t planeCount = view.Size(kLumpPlanes) / sizeof(PlaneRecord);
    const std::size_t offsetCount = view.Size(kLumpSideOffsets) / 2;
    const std::size_t propCount = view.Size(kLumpSideProps) / 2;
    if (modelCount == 0 || grid.firstBrushPlane < 0 || static_cast<std::size_t>(grid.firstBrushPlane) > planeCount)
        return fail("collision grid header is inconsistent");
    if (view.Size(kLumpCells) / sizeof(CellRecord) != static_cast<std::size_t>(grid.cellCount[0]) * grid.cellCount[1] + modelCount)
        return fail("collision cell count is not grid + models");
    if (view.Size(kLumpGeosetBounds) / sizeof(Bounds16) != geosetCount)
        return fail("geoset and geoset bounds counts differ");
    if (propCount != 6 * brushCount + offsetCount)
        return fail("side property count is not 6 * brushes + side offsets");

    // Contents index of CONTENTS_SOLID (the value stock R1 trigger brushes use).
    std::vector<std::uint8_t> uniqueContents = Copy(view, kLumpUniqueContents);
    std::size_t contentsIndex = kMaxUniqueContents;
    const std::size_t contentsCount = uniqueContents.size() / 4;
    for (std::size_t i = 0; i < contentsCount; ++i) {
        if (ReadAt<std::uint32_t>(uniqueContents.data(), i) == kContentsSolid) {
            contentsIndex = i;
            break;
        }
    }
    bool contentsChanged = false;
    if (contentsIndex == kMaxUniqueContents) {
        if (contentsCount >= kMaxUniqueContents)
            return fail("unique contents table is full");
        Append(uniqueContents, kContentsSolid);
        contentsIndex = contentsCount;
        contentsChanged = true;
    }

    // Side textures: the tool materials stock R1 uses for these volumes, else texdata 0.
    int textureIds[3];
    const char* const textureNames[3] = { "TOOLS\\TOOLSTRIGGER", "TOOLS\\TOOLSOUT_OF_BOUNDS", "TOOLS\\TOOLSTRIGGER_CAPTUREPOINT" };
    for (int i = 0; i < 3; ++i) {
        textureIds[i] = FindTexData(view, textureNames[i]);
        if (textureIds[i] < 0)
            textureIds[i] = i == 0 ? 0 : textureIds[0];
        if (textureIds[i] >= 0x4000)
            textureIds[i] = 0;
    }

    std::vector<std::uint8_t> planes = Copy(view, kLumpPlanes);
    std::vector<std::uint8_t> models = Copy(view, kLumpModels);
    std::vector<std::uint8_t> cells = Copy(view, kLumpCells);
    std::vector<std::uint8_t> geosets = Copy(view, kLumpGeosets);
    std::vector<std::uint8_t> geosetBounds = Copy(view, kLumpGeosetBounds);
    std::vector<std::uint8_t> brushes = Copy(view, kLumpBrushes);
    std::vector<std::uint8_t> sideOffsets = Copy(view, kLumpSideOffsets);
    std::vector<std::uint8_t> sideProps = Copy(view, kLumpSideProps);

    // R1 reads a non-axial side as plane (firstBrushPlane + sideStart + k - offset). Every new plane is
    // appended in step with its offset entry, so one constant offset serves all of them. Pad both side
    // arrays (keeping props = 6 * brushes + offsets) when that constant would be negative.
    const std::int64_t planeBase = static_cast<std::int64_t>(planeCount) - grid.firstBrushPlane;
    std::int64_t offsetValue = static_cast<std::int64_t>(offsetCount) - planeBase;
    std::size_t padding = 0;
    if (offsetValue < 0) {
        padding = static_cast<std::size_t>(-offsetValue);
        offsetValue = 0;
    }
    if (offsetValue > 65535)
        return fail("side plane offset does not fit 16 bits");
    for (std::size_t i = 0; i < padding; ++i) {
        Append<std::uint16_t>(sideOffsets, 0);
        Append<std::uint16_t>(sideProps, 0);
    }

    std::size_t newBrushes = 0, newGeosets = 0;
    for (const Planned& plan : planned) {
        newBrushes += plan.brushes.size();
        newGeosets += plan.brushes.size();
    }
    if (brushCount + newBrushes > kMaxPoolIndex || geosetCount + newGeosets >= kMaxPoolIndex || modelCount + planned.size() >= 0x1000)
        return fail("brush, geoset or model index would overflow its 16 bit field");

    const ModelRecord lastModel = ReadAt<ModelRecord>(models.data(), modelCount - 1);
    const std::uint32_t meshEnd = lastModel.firstMesh + lastModel.numMeshes;

    std::vector<std::vector<std::size_t>> modelOfBlock(partitions.size());
    for (std::size_t p = 0; p < partitions.size(); ++p)
        modelOfBlock[p].assign(scanned[p].size(), static_cast<std::size_t>(-1));

    Report& report = result.report;
    report.firstModel = static_cast<std::uint32_t>(modelCount);
    for (const Planned& plan : planned) {
        const std::size_t firstGeoset = geosets.size() / sizeof(GeosetRecord);
        double extent[3] = { 0, 0, 0 };
        for (const Brush& brush : plan.brushes) {
            float origin[3], half[3];
            for (int a = 0; a < 3; ++a) {
                origin[a] = static_cast<float>((brush.mins[a] + brush.maxs[a]) * 0.5);
                half[a] = static_cast<float>((brush.maxs[a] - brush.mins[a]) * 0.5);
                extent[a] = std::max(extent[a], std::fabs(static_cast<double>(origin[a])) + half[a]);
            }
            const std::size_t brushIndex = brushes.size() / sizeof(BrushRecord);
            const std::size_t sideStart = sideOffsets.size() / 2;
            if (sideProps.size() / 2 != 6 * brushIndex + sideOffsets.size() / 2)
                return fail("internal side array accounting error");

            BrushRecord record{};
            std::memcpy(record.origin, origin, sizeof(origin));
            record.numNonAxial = static_cast<std::uint8_t>(brush.nonAxial.size());
            record.numNonAxialCopy = record.numNonAxial;
            record.index = static_cast<std::uint16_t>(brushIndex);
            std::memcpy(record.extents, half, sizeof(half));
            record.sideStart = static_cast<std::uint32_t>(sideStart);
            Append(brushes, record);

            const std::uint16_t texture = static_cast<std::uint16_t>(textureIds[plan.texture]);
            for (int side = 0; side < 6; ++side)
                Append(sideProps, texture);
            for (const Plane& plane : brush.nonAxial) {
                PlaneRecord record2{};
                for (int a = 0; a < 3; ++a)
                    record2.normal[a] = static_cast<float>(plane.n[a]);
                record2.dist = static_cast<float>(plane.d);
                Append(planes, record2);
                Append(sideOffsets, static_cast<std::uint16_t>(offsetValue));
                Append(sideProps, texture);
                ++report.nonAxialPlanes;
            }

            GeosetRecord geoset{};
            geoset.group = 0;
            geoset.count = 1;
            geoset.word = (static_cast<std::uint32_t>(brushIndex & 0x1FFFFF) << 8) | static_cast<std::uint32_t>(contentsIndex);
            Append(geosets, geoset);

            Bounds16 bounds{};
            for (int a = 0; a < 3; ++a)
                bounds.origin[a] = ClampS16(std::floor(origin[a] + 0.5));
            bounds.negCos = -32768;
            bounds.sin = 0;
            bounds.extents[0] = ClampS16(std::ceil(half[0]) + 1);
            bounds.extents[1] = ClampS16(std::ceil(half[1]) + 1);
            bounds.extents[2] = ClampS16(std::ceil(half[2]));
            Append(geosetBounds, bounds);
            ++report.brushes;
        }

        CellRecord cell{};
        cell.first = static_cast<std::uint16_t>(firstGeoset);
        cell.count = static_cast<std::uint16_t>(plan.brushes.size());
        Append(cells, cell);

        ModelRecord model{};
        for (int a = 0; a < 3; ++a) {
            model.mins[a] = static_cast<float>(-extent[a]);
            model.maxs[a] = static_cast<float>(extent[a]);
        }
        model.firstMesh = meshEnd;
        model.numMeshes = 0;
        Append(models, model);

        modelOfBlock[plan.partition][plan.block] = modelCount + report.entitiesBaked;
        ++report.entitiesBaked;
    }
    report.modelsAdded = static_cast<std::uint32_t>(report.entitiesBaked);

    // Entity text: drop the R2 plane/bounds keys, reference the new model.
    for (std::size_t p = 0; p < partitions.size(); ++p) {
        const std::string_view text = partitions[p];
        std::string out;
        std::size_t cursor = 0;
        bool changed = false;
        for (std::size_t b = 0; b < scanned[p].size(); ++b) {
            if (modelOfBlock[p][b] == static_cast<std::size_t>(-1))
                continue;
            const EntityBlock& block = scanned[p][b];
            if (!changed) {
                out.reserve(text.size());
                changed = true;
            }
            for (const Pair& pair : block.pairs) {
                if (!IsTriggerBrushKey(pair.key))
                    continue;
                out.append(text.substr(cursor, pair.span.begin - cursor));
                cursor = pair.span.end;
            }
            out.append(text.substr(cursor, block.closeBrace - cursor));
            cursor = block.closeBrace;
            if (!out.empty() && out.back() != '\n')
                out.push_back('\n');
            out.append("\"model\" \"*");
            out.append(std::to_string(modelOfBlock[p][b]));
            out.append("\"\n");
        }
        if (changed) {
            out.append(text.substr(cursor));
            result.texts[p] = std::move(out);
        }
    }

    result.lumps[kLumpEntities] = std::vector<std::uint8_t>(result.texts[0].begin(), result.texts[0].end());
    result.lumps[kLumpPlanes] = std::move(planes);
    result.lumps[kLumpModels] = std::move(models);
    result.lumps[kLumpCells] = std::move(cells);
    result.lumps[kLumpGeosets] = std::move(geosets);
    result.lumps[kLumpGeosetBounds] = std::move(geosetBounds);
    result.lumps[kLumpBrushes] = std::move(brushes);
    result.lumps[kLumpSideOffsets] = std::move(sideOffsets);
    result.lumps[kLumpSideProps] = std::move(sideProps);
    if (contentsChanged)
        result.lumps[kLumpUniqueContents] = std::move(uniqueContents);
    result.baked = true;
    return result;
}

} // namespace r2port::trigger_brush
