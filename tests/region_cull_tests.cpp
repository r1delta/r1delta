// Differential test for the optimized region-cull traversal (client.dll sub_1802319F0).
//
// The shipped implementation (client/culling/region_cull_core.h) is compared against the
// unoptimized oracle reconstruction (tests/region_cull_reference.h) on synthetic scenes that
// use the real constant tables dumped from the retail client.dll .rdata. Both implementations
// run against the same memory (the oracle's globals), and the pass condition covers the
// emitted ranges, the scheduler appends (kinds, reservation slots, appended child tasks), the
// worker completion calls, and the return value.
//
// Covered branches: depth/side-plane rejection, projection-plane crossing, 4- and 6-corner
// silhouettes, region edge counts below/above the prepared-edge cache and zero-edge regions,
// queue growth, child splitting onto the scheduler (with and without reservation capacity),
// leaf item runs, and the individual-item test threshold.

#include "client/culling/region_cull_core.h"
#include "region_cull_reference.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace core = region_cull;
namespace oracle = recovered;

namespace {

using oracle::Range16;
using core::Vec4;

constexpr std::uint32_t kWorkers = 4;
constexpr std::uint32_t kArenaBytes = 1u << 20;
constexpr std::uint32_t kPlaneCount = 32;
constexpr std::uint32_t kVertexCount = 16;
constexpr std::uint32_t kMaxAppendRecords = 512;
constexpr std::uint32_t kMaxCapturedRanges = 128;
constexpr std::uint32_t kMaxCapturedSlots = 16;
constexpr std::uint32_t kRandomCases = 3000;

inline core::Vec4 vec(float x, float y, float z, float w = 0.0f)
{
    return _mm_set_ps(w, z, y, x);
}

// ---------------------------------------------------------------------------
// Constant tables, copied byte-for-byte from the retail client.dll .rdata dumps.
// The FNV-1a hashes are of those raw dumps and are verified at startup.
// ---------------------------------------------------------------------------

constexpr std::uint32_t kSilhouetteCodes[64] = {
    0x00000000, 0x00026402, 0x00015401, 0x02645102, 0x00013201, 0x02640132, 0x01540231, 0x02645132,
    0x00015731, 0x00000000, 0x01375401, 0x00000000, 0x01573201, 0x00000000, 0x03754023, 0x00000000,
    0x00026732, 0x04673204, 0x00000000, 0x00000000, 0x02673102, 0x03764013, 0x00000000, 0x00000000,
    0x01576231, 0x00000000, 0x00000000, 0x00000000, 0x02675102, 0x00000000, 0x00000000, 0x00000000,
    0x00045764, 0x02675402, 0x01576401, 0x02675102, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x03764513, 0x00000000, 0x03764013, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x03754623, 0x03754023, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x02645132, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

constexpr std::uint32_t kEdgeEvents[16] = {
    0x00000000, 0x00000002, 0x00000001, 0x00000004, 0x00000002, 0x00000000, 0x00000004, 0x00000001,
    0x00000001, 0x00000004, 0x00000000, 0x00000002, 0x00000004, 0x00000001, 0x00000002, 0x00000000,
};

constexpr std::uint32_t kCornerMaskLanes[16][4] = {
    { 0x00000000, 0x00000000, 0x00000000, 0x00000000 },
    { 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000 },
    { 0x00000000, 0xFFFFFFFF, 0x00000000, 0x00000000 },
    { 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000 },
    { 0x00000000, 0x00000000, 0xFFFFFFFF, 0x00000000 },
    { 0xFFFFFFFF, 0x00000000, 0xFFFFFFFF, 0x00000000 },
    { 0x00000000, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000 },
    { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000 },
    { 0x00000000, 0x00000000, 0x00000000, 0xFFFFFFFF },
    { 0xFFFFFFFF, 0x00000000, 0x00000000, 0xFFFFFFFF },
    { 0x00000000, 0xFFFFFFFF, 0x00000000, 0xFFFFFFFF },
    { 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0xFFFFFFFF },
    { 0x00000000, 0x00000000, 0xFFFFFFFF, 0xFFFFFFFF },
    { 0xFFFFFFFF, 0x00000000, 0xFFFFFFFF, 0xFFFFFFFF },
    { 0x00000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF },
    { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF },
};

constexpr std::uint32_t kAbsoluteValueMaskLanes[4] = { 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF };
constexpr std::uint32_t kXyzMaskLanes[4] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000 };

constexpr std::uint64_t kSilhouetteCodesHash = 0x76AF6FB5AB7A8D2Cull;
constexpr std::uint64_t kEdgeEventsHash = 0x2C5382FDA7DA7FA3ull;
constexpr std::uint64_t kCornerMasksHash = 0xF9C15F569303D903ull;
constexpr std::uint64_t kAbsoluteValueMaskHash = 0x456057DC8994CE33ull;
constexpr std::uint64_t kXyzMaskHash = 0x085110BAC1E1E4D7ull;

std::uint64_t HashBytes(const void* data, std::size_t size)
{
    std::uint64_t hash = 1469598103934665603ull;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i != size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

inline core::Vec4 MaskFromLanes(const std::uint32_t (&lanes)[4])
{
    return _mm_castsi128_ps(_mm_set_epi32(static_cast<int>(lanes[3]), static_cast<int>(lanes[2]),
                                          static_cast<int>(lanes[1]), static_cast<int>(lanes[0])));
}

// ---------------------------------------------------------------------------
// Scheduler trace shared by both implementations (the oracle calls these through its
// provisional WT declarations; the optimized code calls them through CullScheduler).
// ---------------------------------------------------------------------------

struct Trace {
    struct Append {
        std::uint32_t kind;
        std::uint32_t count;
        std::int32_t slot;
    };
    Append appends[kMaxAppendRecords];
    std::uint32_t appendCount;
    std::uint32_t completionKinds[8];
    std::uint32_t completionCount;
    std::int32_t nextSlot;

    void Reset(std::int32_t firstSlot)
    {
        appendCount = 0;
        completionCount = 0;
        nextSlot = firstSlot;
    }
};

Trace g_trace{};

struct Rng {
    std::uint64_t state;

    explicit Rng(std::uint64_t seed) : state(seed ? seed : 0x9E3779B97F4A7C15ull) {}

    std::uint32_t Next()
    {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return static_cast<std::uint32_t>(state >> 32);
    }

    std::uint32_t Below(std::uint32_t bound) { return bound ? Next() % bound : 0; }

    float Unit() { return static_cast<float>(Next() >> 8) / 16777216.0f; }

    float Range(float lo, float hi) { return lo + (hi - lo) * Unit(); }
};

} // namespace

// ---------------------------------------------------------------------------
// Oracle globals (declared extern by tests/region_cull_reference.h).
// ---------------------------------------------------------------------------

namespace recovered {

const HierarchyNode* g_nodes = nullptr;
const ItemBounds* g_itemBounds = nullptr;
std::byte g_regionArena[kArenaBytes];
CullTask g_tasks[kTaskCapacity];
WorkerOutput g_workerOutputs[kWorkers];

const Vec4 g_xyzMask = MaskFromLanes(kXyzMaskLanes);
const Vec4 g_absoluteValueMask = MaskFromLanes(kAbsoluteValueMaskLanes);
const Vec4 g_cornerSelectMasks[16] = {
    MaskFromLanes(kCornerMaskLanes[0]), MaskFromLanes(kCornerMaskLanes[1]),
    MaskFromLanes(kCornerMaskLanes[2]), MaskFromLanes(kCornerMaskLanes[3]),
    MaskFromLanes(kCornerMaskLanes[4]), MaskFromLanes(kCornerMaskLanes[5]),
    MaskFromLanes(kCornerMaskLanes[6]), MaskFromLanes(kCornerMaskLanes[7]),
    MaskFromLanes(kCornerMaskLanes[8]), MaskFromLanes(kCornerMaskLanes[9]),
    MaskFromLanes(kCornerMaskLanes[10]), MaskFromLanes(kCornerMaskLanes[11]),
    MaskFromLanes(kCornerMaskLanes[12]), MaskFromLanes(kCornerMaskLanes[13]),
    MaskFromLanes(kCornerMaskLanes[14]), MaskFromLanes(kCornerMaskLanes[15]),
};
const std::uint32_t g_silhouetteCodes[64] = {
    kSilhouetteCodes[0], kSilhouetteCodes[1], kSilhouetteCodes[2], kSilhouetteCodes[3],
    kSilhouetteCodes[4], kSilhouetteCodes[5], kSilhouetteCodes[6], kSilhouetteCodes[7],
    kSilhouetteCodes[8], kSilhouetteCodes[9], kSilhouetteCodes[10], kSilhouetteCodes[11],
    kSilhouetteCodes[12], kSilhouetteCodes[13], kSilhouetteCodes[14], kSilhouetteCodes[15],
    kSilhouetteCodes[16], kSilhouetteCodes[17], kSilhouetteCodes[18], kSilhouetteCodes[19],
    kSilhouetteCodes[20], kSilhouetteCodes[21], kSilhouetteCodes[22], kSilhouetteCodes[23],
    kSilhouetteCodes[24], kSilhouetteCodes[25], kSilhouetteCodes[26], kSilhouetteCodes[27],
    kSilhouetteCodes[28], kSilhouetteCodes[29], kSilhouetteCodes[30], kSilhouetteCodes[31],
    kSilhouetteCodes[32], kSilhouetteCodes[33], kSilhouetteCodes[34], kSilhouetteCodes[35],
    kSilhouetteCodes[36], kSilhouetteCodes[37], kSilhouetteCodes[38], kSilhouetteCodes[39],
    kSilhouetteCodes[40], kSilhouetteCodes[41], kSilhouetteCodes[42], kSilhouetteCodes[43],
    kSilhouetteCodes[44], kSilhouetteCodes[45], kSilhouetteCodes[46], kSilhouetteCodes[47],
    kSilhouetteCodes[48], kSilhouetteCodes[49], kSilhouetteCodes[50], kSilhouetteCodes[51],
    kSilhouetteCodes[52], kSilhouetteCodes[53], kSilhouetteCodes[54], kSilhouetteCodes[55],
    kSilhouetteCodes[56], kSilhouetteCodes[57], kSilhouetteCodes[58], kSilhouetteCodes[59],
    kSilhouetteCodes[60], kSilhouetteCodes[61], kSilhouetteCodes[62], kSilhouetteCodes[63],
};
const std::uint32_t g_edgeEvents[16] = {
    kEdgeEvents[0], kEdgeEvents[1], kEdgeEvents[2], kEdgeEvents[3],
    kEdgeEvents[4], kEdgeEvents[5], kEdgeEvents[6], kEdgeEvents[7],
    kEdgeEvents[8], kEdgeEvents[9], kEdgeEvents[10], kEdgeEvents[11],
    kEdgeEvents[12], kEdgeEvents[13], kEdgeEvents[14], kEdgeEvents[15],
};

Vec4 g_viewOrigin = _mm_setzero_ps();
Vec4 g_projectionNormal = _mm_setzero_ps();
Vec4 g_sidePlanes[8] = {};
std::uint32_t g_sidePlaneCount = 0;
std::uint32_t g_individualTestThreshold = 0;
std::uint32_t g_taskSplitThreshold = 0;
Vec4 g_regionEdgePlanes[kPlaneCount] = {};

std::int32_t WT_AppendTaskItems_Begin(std::uint32_t kind)
{
    (void)kind;
    return g_trace.nextSlot;
}

void WT_AppendTaskItems_End(std::uint32_t kind, std::uint32_t count)
{
    if (g_trace.appendCount < kMaxAppendRecords)
        g_trace.appends[g_trace.appendCount++] = { kind, count, g_trace.nextSlot };
    g_trace.nextSlot += static_cast<std::int32_t>(count);
}

std::int64_t WT_TryDoTaskItem(std::uint32_t workerIndex, std::uint32_t kind, OpaqueAbortCallback)
{
    (void)workerIndex;
    if (g_trace.completionCount < 8)
        g_trace.completionKinds[g_trace.completionCount++] = kind;
    return static_cast<std::int64_t>(kind) * 1000 + 7;
}

void WT_DefaultAbortCondition() {}

} // namespace recovered

namespace {

// ---------------------------------------------------------------------------
// Scene construction and run comparison
// ---------------------------------------------------------------------------

struct Snapshot {
    std::uint32_t rangeCount = 0;
    Range16 ranges[kMaxCapturedRanges]{};
    Trace trace{};
    oracle::CullTask slots[kMaxAppendRecords][kMaxCapturedSlots]{};
    std::uint32_t slotCounts[kMaxAppendRecords]{};
    std::int64_t value = 0;
};

struct Scene {
    std::vector<oracle::HierarchyNode> nodes;
    std::vector<oracle::ItemBounds> items;
    oracle::RegionEdge edges[256]{};
    Vec4 vertices[kVertexCount]{};
    std::uint32_t edgeCount = 1;
    std::uint16_t rootNode = 0;
    std::uint16_t regionBlock = 0;
    std::uint16_t taskSlotIndex = 0;
};

void WriteRegion(const Scene& scene)
{
    auto* block = reinterpret_cast<std::uint8_t*>(&recovered::g_regionArena[
        static_cast<std::size_t>(scene.regionBlock) * 64]);
    oracle::RegionHeader header{};
    header.edgeCount = static_cast<std::uint16_t>(scene.edgeCount);
    std::memcpy(block, &header, sizeof(header));
    // The traversal executes the first edge test even for a zero-edge region, so the
    // record at index 0 must always hold values the classifier can index safely.
    const std::uint32_t written = scene.edgeCount == 0 ? 1 : scene.edgeCount;
    std::memcpy(block + sizeof(header), scene.edges, sizeof(oracle::RegionEdge) * written);
    const std::size_t vertexOffset =
        (sizeof(header) + sizeof(oracle::RegionEdge) * scene.edgeCount + 15) & ~std::size_t(15);
    std::memcpy(block + vertexOffset, scene.vertices, sizeof(scene.vertices));
}

core::CullEnv BuildEnv()
{
    core::CullEnv env;
    env.nodes = reinterpret_cast<const core::HierarchyNode* const*>(&recovered::g_nodes);
    env.items = reinterpret_cast<const core::ItemBounds* const*>(&recovered::g_itemBounds);
    env.regionArena = reinterpret_cast<const std::uint8_t*>(recovered::g_regionArena);
    env.tasks = reinterpret_cast<core::CullTask*>(recovered::g_tasks);
    env.workerOutputs = reinterpret_cast<std::uint8_t*>(recovered::g_workerOutputs);
    env.silhouetteCodes = recovered::g_silhouetteCodes;
    env.edgeEvents = recovered::g_edgeEvents;
    env.xyzMask = &recovered::g_xyzMask;
    env.absoluteValueMask = &recovered::g_absoluteValueMask;
    env.cornerSelectMasks = recovered::g_cornerSelectMasks;
    env.viewOrigin = &recovered::g_viewOrigin;
    env.projectionNormal = &recovered::g_projectionNormal;
    env.sidePlanes = recovered::g_sidePlanes;
    env.sidePlaneCount = &recovered::g_sidePlaneCount;
    env.individualTestThreshold = &recovered::g_individualTestThreshold;
    env.taskSplitThreshold = &recovered::g_taskSplitThreshold;
    env.regionEdgePlanes = recovered::g_regionEdgePlanes;
    env.scheduler.slots = reinterpret_cast<core::CullTask*>(recovered::g_tasks);
    env.scheduler.appendBegin = &recovered::WT_AppendTaskItems_Begin;
    env.scheduler.appendEnd = &recovered::WT_AppendTaskItems_End;
    env.scheduler.tryDoTaskItem = &recovered::WT_TryDoTaskItem;
    env.scheduler.abortCondition = &recovered::WT_DefaultAbortCondition;
    return env;
}

void PrepareScene(const Scene& scene)
{
    WriteRegion(scene);
    recovered::g_nodes = scene.nodes.data();
    recovered::g_itemBounds = scene.items.data();
    recovered::g_tasks[scene.taskSlotIndex] = { scene.regionBlock, scene.rootNode };
}

void ResetRunState(std::int32_t firstSlot)
{
    std::memset(recovered::g_tasks, 0, sizeof(recovered::g_tasks));
    for (auto& output : recovered::g_workerOutputs)
        output.count = 0;
    g_trace.Reset(firstSlot);
}

Snapshot Capture(std::uint32_t workerIndex, std::int64_t value)
{
    Snapshot snapshot;
    snapshot.value = value;
    const auto& output = recovered::g_workerOutputs[workerIndex];
    snapshot.rangeCount = output.count;
    const std::uint32_t captured = output.count < kMaxCapturedRanges ? output.count : kMaxCapturedRanges;
    for (std::uint32_t i = 0; i != captured; ++i)
        snapshot.ranges[i] = output.ranges[i];

    snapshot.trace = g_trace;
    for (std::uint32_t i = 0; i != g_trace.appendCount && i < kMaxAppendRecords; ++i) {
        const auto& append = g_trace.appends[i];
        if (append.slot < 0)
            continue;
        std::uint32_t count = append.count;
        if (count > kMaxCapturedSlots)
            count = kMaxCapturedSlots;
        snapshot.slotCounts[i] = count;
        for (std::uint32_t j = 0; j != count; ++j) {
            const std::uint32_t slot = static_cast<std::uint32_t>(append.slot) + j;
            if (slot < core::kTaskCapacity)
                snapshot.slots[i][j] = recovered::g_tasks[slot];
        }
    }
    return snapshot;
}

struct Mismatch {
    bool different = false;
    char detail[512]{};
};

Mismatch CompareSnapshots(const Snapshot& a, const Snapshot& b, bool compareScheduler)
{
    Mismatch mismatch;
    auto fail = [&mismatch](const char* what, long long left, long long right) {
        mismatch.different = true;
        std::snprintf(mismatch.detail, sizeof(mismatch.detail), "%s: oracle=%lld optimized=%lld",
                      what, left, right);
    };

    if (a.rangeCount != b.rangeCount) {
        fail("range count", a.rangeCount, b.rangeCount);
        return mismatch;
    }
    const std::uint32_t ranges = a.rangeCount < kMaxCapturedRanges ? a.rangeCount : kMaxCapturedRanges;
    for (std::uint32_t i = 0; i != ranges; ++i) {
        if (a.ranges[i].first != b.ranges[i].first || a.ranges[i].count != b.ranges[i].count) {
            mismatch.different = true;
            std::snprintf(mismatch.detail, sizeof(mismatch.detail),
                          "range %u: oracle={%u,%u} optimized={%u,%u}", i,
                          a.ranges[i].first, a.ranges[i].count, b.ranges[i].first, b.ranges[i].count);
            return mismatch;
        }
    }

    if (!compareScheduler)
        return mismatch;

    if (a.trace.appendCount != b.trace.appendCount) {
        fail("append count", a.trace.appendCount, b.trace.appendCount);
        return mismatch;
    }
    for (std::uint32_t i = 0; i != a.trace.appendCount; ++i) {
        const auto& left = a.trace.appends[i];
        const auto& right = b.trace.appends[i];
        if (left.kind != right.kind || left.count != right.count || left.slot != right.slot) {
            mismatch.different = true;
            std::snprintf(mismatch.detail, sizeof(mismatch.detail),
                          "append %u: oracle={kind %u count %u slot %d} optimized={kind %u count %u slot %d}",
                          i, left.kind, left.count, left.slot, right.kind, right.count, right.slot);
            return mismatch;
        }
        for (std::uint32_t j = 0; j != a.slotCounts[i]; ++j) {
            if (a.slots[i][j].regionBlock != b.slots[i][j].regionBlock
                || a.slots[i][j].rootNode != b.slots[i][j].rootNode) {
                mismatch.different = true;
                std::snprintf(mismatch.detail, sizeof(mismatch.detail),
                              "append %u slot %u: oracle={%u,%u} optimized={%u,%u}", i, j,
                              a.slots[i][j].regionBlock, a.slots[i][j].rootNode,
                              b.slots[i][j].regionBlock, b.slots[i][j].rootNode);
                return mismatch;
            }
        }
    }

    if (a.trace.completionCount != b.trace.completionCount) {
        fail("completion count", a.trace.completionCount, b.trace.completionCount);
        return mismatch;
    }
    for (std::uint32_t i = 0; i != a.trace.completionCount; ++i) {
        if (a.trace.completionKinds[i] != b.trace.completionKinds[i]) {
            fail("completion kind", a.trace.completionKinds[i], b.trace.completionKinds[i]);
            return mismatch;
        }
    }

    if (a.value != b.value)
        fail("return value", static_cast<long long>(a.value), static_cast<long long>(b.value));
    return mismatch;
}

// Builds a BFS-ordered hierarchy whose child groups are contiguous, like the engine's.
Scene BuildScene(Rng& rng, bool wideRegion)
{
    Scene scene;
    struct Spec {
        std::uint32_t depth;
        std::uint32_t itemCount;
        std::uint32_t firstChild;
        std::uint32_t childCount;
        std::uint32_t firstItem;
        std::uint32_t subtreeItems;
    };

    const std::uint32_t maxDepth = 3 + rng.Below(2);
    const std::uint32_t maxChildren = 1 + rng.Below(3);
    const std::uint32_t maxItems = rng.Below(7);

    std::vector<Spec> specs;
    specs.reserve(512);
    specs.push_back({ 0, 0, 0, 0, 0, 0 });
    const std::uint32_t nodeLimit = 60 + rng.Below(60);
    for (std::uint32_t i = 0; i != specs.size(); ++i) {
        // Index (never a reference) across push_back: growth would invalidate it.
        specs[i].itemCount = rng.Below(maxItems + 1);
        if (specs[i].depth < maxDepth && specs.size() + maxChildren < nodeLimit) {
            const std::uint32_t firstChild = static_cast<std::uint32_t>(specs.size());
            const std::uint32_t childCount = 1 + rng.Below(maxChildren);
            const std::uint32_t childDepth = specs[i].depth + 1;
            for (std::uint32_t c = 0; c != childCount; ++c)
                specs.push_back({ childDepth, 0, 0, 0, 0, 0 });
            specs[i].firstChild = firstChild;
            specs[i].childCount = childCount;
        }
    }

    // Item IDs follow subtree preorder — a node's direct items, then its descendants' — so the
    // packed [firstItem, firstItem + subtreeItemCount) interval is exactly that subtree. The
    // previous BFS numbering let both implementations agree on layouts no real hierarchy can
    // produce, which is precisely the emission path optimizations touch.
    std::uint32_t itemCount = 0;
    const auto assignItems = [&](auto&& self, std::uint32_t index) -> void {
        Spec& spec = specs[index];
        spec.firstItem = itemCount;
        itemCount += spec.itemCount;
        for (std::uint32_t c = 0; c != spec.childCount; ++c)
            self(self, spec.firstChild + c);
    };
    assignItems(assignItems, 0);

    const float spread = 200.0f + rng.Range(0.0f, 400.0f);
    scene.items.resize(itemCount);
    std::vector<Vec4> itemMin(itemCount), itemMax(itemCount);
    for (std::uint32_t i = 0; i != itemCount; ++i) {
        const float cx = rng.Range(-spread, spread);
        const float cy = rng.Range(-spread, spread);
        const float cz = rng.Range(-spread, spread);
        const float ex = rng.Range(1.0f, 40.0f);
        const float ey = rng.Range(1.0f, 40.0f);
        const float ez = rng.Range(1.0f, 40.0f);
        scene.items[i].minimum = vec(cx - ex, cy - ey, cz - ez);
        scene.items[i].maximum = vec(cx + ex, cy + ey, cz + ez);
        itemMin[i] = scene.items[i].minimum;
        itemMax[i] = scene.items[i].maximum;
    }

    scene.nodes.resize(specs.size());
    for (std::uint32_t i = static_cast<std::uint32_t>(specs.size()); i-- != 0;) {
        Spec& spec = specs[i];
        oracle::HierarchyNode& node = scene.nodes[i];
        node.childCount = static_cast<std::uint8_t>(spec.childCount);
        node.directItemCount = static_cast<std::uint8_t>(spec.itemCount);
        node.firstChild = static_cast<std::uint16_t>(spec.firstChild);
        node.firstItem = static_cast<std::uint16_t>(spec.firstItem);

        Vec4 minimum = vec(1e9f, 1e9f, 1e9f);
        Vec4 maximum = vec(-1e9f, -1e9f, -1e9f);
        bool any = false;
        for (std::uint32_t item = 0; item != spec.itemCount; ++item) {
            const std::uint32_t index = spec.firstItem + item;
            minimum = _mm_min_ps(minimum, itemMin[index]);
            maximum = _mm_max_ps(maximum, itemMax[index]);
            any = true;
        }
        std::uint32_t subtreeItems = spec.itemCount;
        for (std::uint32_t c = 0; c != spec.childCount; ++c) {
            const Spec& child = specs[spec.firstChild + c];
            subtreeItems += child.subtreeItems;
            const oracle::HierarchyNode& childNode = scene.nodes[spec.firstChild + c];
            const Vec4 childMin = _mm_load_ps(&childNode.minimum[0]);
            const Vec4 childMax = _mm_load_ps(&childNode.maximum[0]);
            minimum = _mm_min_ps(minimum, childMin);
            maximum = _mm_max_ps(maximum, childMax);
            any = true;
        }
        if (!any) {
            const float cx = rng.Range(-spread, spread);
            const float cy = rng.Range(-spread, spread);
            const float cz = rng.Range(-spread, spread);
            minimum = vec(cx - 10.0f, cy - 10.0f, cz - 10.0f);
            maximum = vec(cx + 10.0f, cy + 10.0f, cz + 10.0f);
        } else {
            const float pad = rng.Range(0.0f, 2.0f);
            minimum = _mm_sub_ps(minimum, vec(pad, pad, pad));
            maximum = _mm_add_ps(maximum, vec(pad, pad, pad));
        }
        std::memcpy(&node.minimum[0], &minimum, sizeof(float) * 3);
        std::memcpy(&node.maximum[0], &maximum, sizeof(float) * 3);
        spec.subtreeItems = subtreeItems;

        // The engine's packed 16-bit field holds the subtree item count.
        const std::uint32_t clamped = subtreeItems > 0xFFFFu ? 0xFFFFu : subtreeItems;
        node.subtreeItemCount = static_cast<std::uint16_t>(clamped);
    }

    scene.rootNode = 0;
    scene.regionBlock = static_cast<std::uint16_t>(rng.Below(8));
    scene.taskSlotIndex = static_cast<std::uint16_t>(rng.Below(4));

    scene.edgeCount = wideRegion ? (129u + rng.Below(16u)) : rng.Below(9u);
    if (rng.Below(16) == 0 && !wideRegion)
        scene.edgeCount = 0;
    for (std::uint32_t i = 0; i != scene.edgeCount && i < 256; ++i) {
        scene.edges[i].planeIndex = static_cast<std::uint16_t>(rng.Below(kPlaneCount));
        scene.edges[i].endpoint0 = static_cast<std::uint8_t>(rng.Below(kVertexCount));
        scene.edges[i].endpoint1 = static_cast<std::uint8_t>(rng.Below(kVertexCount));
    }
    for (std::uint32_t i = 0; i != kVertexCount; ++i)
        scene.vertices[i] = vec(rng.Range(-2.0f, 2.0f), rng.Range(-2.0f, 2.0f), rng.Range(-2.0f, 2.0f));

    for (std::uint32_t i = 0; i != kPlaneCount; ++i)
        recovered::g_regionEdgePlanes[i] = vec(rng.Range(-1.0f, 1.0f), rng.Range(-1.0f, 1.0f),
                                               rng.Range(-1.0f, 1.0f));

    recovered::g_viewOrigin = vec(rng.Range(-spread, spread), rng.Range(-spread, spread), rng.Range(-spread, spread));
    recovered::g_projectionNormal = vec(rng.Range(-1.0f, 1.0f), rng.Range(-1.0f, 1.0f), rng.Range(-1.0f, 1.0f));
    for (std::uint32_t i = 0; i != 8; ++i)
        recovered::g_sidePlanes[i] = vec(rng.Range(-1.0f, 1.0f), rng.Range(-1.0f, 1.0f),
                                         rng.Range(-1.0f, 1.0f), rng.Range(-30.0f, 30.0f));
    recovered::g_sidePlaneCount = rng.Below(4) == 0 ? 4u : 8u;
    recovered::g_individualTestThreshold = rng.Below(6) == 0 ? rng.Below(32u) : rng.Below(5u);
    recovered::g_taskSplitThreshold = rng.Below(6) == 0 ? rng.Below(64u) : rng.Below(9u);
    return scene;
}

struct CaseResult {
    bool verified = true;
    char detail[512]{};
};

// Deterministic scenes for the branches the random suite does not reach on its own. Each one
// still runs the full oracle comparison; they exist so coverage cannot silently rot.
//
// Both are derived from a real random scene: an earlier hand-written version used degenerate
// planes, and every node of it classified Outside, which proved the fixture was measuring the
// geometry rather than the traversal.

// Probes a scene's nodes through the reference implementation to find one that classifies
// Partial and queues its children, and returns that node index. Uses an isolated task slot and
// clears the output counters, so the probe leaves no trace in the traced state.
std::uint32_t FindQueuingNode(Scene& scene)
{
    for (std::uint32_t index = 0; index != scene.nodes.size(); ++index) {
        ResetRunState(0);
        scene.taskSlotIndex = 0;
        PrepareScene(scene);
        recovered::g_tasks[0] = { scene.regionBlock, static_cast<std::uint16_t>(index) };

        const std::uint32_t pushesBefore = recovered::coverage::queuePushes;
        oracle::sub_1802319F0(0, 0);
        if (recovered::coverage::queuePushes != pushesBefore)
            return index;
    }
    return UINT32_MAX;
}

// A chain of internal nodes that each queue one child range: total pushes exceed the ring
// capacity, so head and tail pass the array end while entries are live. Occupancy stays small,
// which is what the engine's split threshold is for.
Scene BuildQueueWraparoundScene()
{
    Rng search(0x51A1CEu);
    Scene scene = BuildScene(search, false);
    const std::uint32_t queuingNode = FindQueuingNode(scene);
    if (queuingNode == UINT32_MAX)
        return {};   // the caller fails the coverage floor rather than measuring nothing

    // Every chain node copies the probed node's bounds, so they all classify the same way.
    oracle::HierarchyNode pattern = scene.nodes[queuingNode];
    scene.items.resize(1);
    scene.items[0].minimum = vec(-4.0f, -4.0f, -4.0f);
    scene.items[0].maximum = vec(4.0f, 4.0f, 4.0f);

    constexpr std::uint32_t kChainNodes = 1200;   // > kQueueCapacity pushes in one task
    scene.nodes.assign(kChainNodes + 1, pattern);
    for (std::uint32_t i = 0; i != kChainNodes; ++i) {
        oracle::HierarchyNode& node = scene.nodes[i];
        node = pattern;
        node.childCount = 1;
        node.directItemCount = 0;
        // Each node's only child is the next node, so one task walks the whole chain and queues
        // a range per link.
        node.firstChild = static_cast<std::uint16_t>(i + 1);
        node.firstItem = 0;
        node.subtreeItemCount = 0;
    }
    {
        oracle::HierarchyNode& tail = scene.nodes[kChainNodes];
        tail = pattern;
        tail.childCount = 0;
        tail.directItemCount = 0;
        tail.firstChild = 0;
        tail.firstItem = 0;
        tail.subtreeItemCount = 0;
    }

    scene.rootNode = 0;
    scene.regionBlock = 0;
    scene.taskSlotIndex = 0;
    // Local queueing instead of scheduler splits, so the ring is what carries the work.
    recovered::g_taskSplitThreshold = 0xFFFFu;
    recovered::g_individualTestThreshold = 0xFFFFu;
    return scene;
}

// A box that straddles the view plane along the projection normal: `depthSigns & 1` is set and
// the classifier answers Partial after the two side-plane groups. This is the branch that makes
// the `originCode == 0` exit unreachable — a box the origin sits inside is by definition also
// straddling along the normal, so the depth test has already returned.
Scene BuildStraddlingBoxScene()
{
    Rng search(0x0B0C0DEu);
    Scene scene = BuildScene(search, false);
    scene.nodes.resize(1);
    scene.items.resize(1);

    const Vec4 origin = _mm_and_ps(recovered::g_xyzMask, recovered::g_viewOrigin);
    float originF[4]{};
    std::memcpy(originF, &origin, sizeof(originF));

    oracle::HierarchyNode& node = scene.nodes[0];
    node.childCount = 0;
    node.directItemCount = 1;
    node.firstChild = 0;
    node.firstItem = 0;
    node.subtreeItemCount = 1;
    // Bounds centred on the origin: inside on every axis, and therefore behind the near
    // depth plane as well.
    node.minimum[0] = originF[0] - 10.0f;
    node.minimum[1] = originF[1] - 10.0f;
    node.minimum[2] = originF[2] - 10.0f;
    node.maximum[0] = originF[0] + 10.0f;
    node.maximum[1] = originF[1] + 10.0f;
    node.maximum[2] = originF[2] + 10.0f;
    scene.items[0].minimum = vec(originF[0] - 1.0f, originF[1] - 1.0f, originF[2] - 1.0f);
    scene.items[0].maximum = vec(originF[0] + 1.0f, originF[1] + 1.0f, originF[2] + 1.0f);

    scene.rootNode = 0;
    scene.regionBlock = 0;
    scene.taskSlotIndex = 0;
    // Perpendicular side planes would reject the box before the depth test decides.
    for (std::uint32_t i = 0; i != 8; ++i)
        recovered::g_sidePlanes[i] = vec(0.0f, 0.0f, 0.0f, 0.0f);
    recovered::g_sidePlaneCount = 8;
    recovered::g_individualTestThreshold = 0;
    recovered::g_taskSplitThreshold = 0xFFFFu;
    return scene;
}

CaseResult RunCase(Scene& scene, std::uint32_t workerIndex, std::int32_t firstSlot, bool shadowOnly)
{
    CaseResult result;

    ResetRunState(firstSlot);
    PrepareScene(scene);
    const std::int64_t oracleValue = oracle::sub_1802319F0(workerIndex, scene.taskSlotIndex);
    const Snapshot oracleSnapshot = Capture(workerIndex, oracleValue);

    ResetRunState(firstSlot);
    PrepareScene(scene);
    core::CullEnv env = BuildEnv();
    const bool commit = !shadowOnly;
    const std::int64_t coreValue = core::ProcessRegionCullTask(env, workerIndex, scene.taskSlotIndex, commit);
    const Snapshot coreSnapshot = Capture(workerIndex, coreValue);

    if (shadowOnly) {
        // Shadow runs must not touch scheduler state at all.
        if (coreSnapshot.trace.appendCount != 0 || coreSnapshot.trace.completionCount != 0) {
            result.verified = false;
            std::snprintf(result.detail, sizeof(result.detail),
                          "shadow run performed scheduler calls: appends=%u completions=%u",
                          coreSnapshot.trace.appendCount, coreSnapshot.trace.completionCount);
            return result;
        }
    }

    const Mismatch mismatch = CompareSnapshots(oracleSnapshot, coreSnapshot, !shadowOnly);
    result.verified = !mismatch.different;
    if (mismatch.different)
        std::snprintf(result.detail, sizeof(result.detail), "%s", mismatch.detail);
    return result;
}

} // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    // The embedded tables must equal the retail .rdata dumps byte for byte.
    struct TableCheck {
        const char* name;
        const void* data;
        std::size_t size;
        std::uint64_t expected;
    };
    const TableCheck checks[] = {
        { "silhouette codes", kSilhouetteCodes, sizeof(kSilhouetteCodes), kSilhouetteCodesHash },
        { "edge events", kEdgeEvents, sizeof(kEdgeEvents), kEdgeEventsHash },
        { "corner masks", kCornerMaskLanes, sizeof(kCornerMaskLanes), kCornerMasksHash },
        { "absolute value mask", kAbsoluteValueMaskLanes, sizeof(kAbsoluteValueMaskLanes), kAbsoluteValueMaskHash },
        { "xyz mask", kXyzMaskLanes, sizeof(kXyzMaskLanes), kXyzMaskHash },
    };
    for (const TableCheck& check : checks) {
        const std::uint64_t hash = HashBytes(check.data, check.size);
        if (hash != check.expected) {
            std::printf("FAIL: %s table does not match the retail client.dll dump "
                        "(fnv1a64=%016llX expected %016llX)\n",
                        check.name, static_cast<unsigned long long>(hash),
                        static_cast<unsigned long long>(check.expected));
            return 1;
        }
    }
    std::printf("ok: constant tables match the retail client.dll .rdata dumps\n");

    if (sizeof(oracle::WorkerOutput) != core::kWorkerOutputStride) {
        std::printf("FAIL: oracle worker output stride %zu != %u\n",
                    sizeof(oracle::WorkerOutput), core::kWorkerOutputStride);
        return 1;
    }
    static_assert(sizeof(oracle::HierarchyNode) == sizeof(core::HierarchyNode));
    static_assert(sizeof(oracle::ItemBounds) == sizeof(core::ItemBounds));
    static_assert(sizeof(oracle::CullTask) == sizeof(core::CullTask));
    static_assert(sizeof(oracle::Range16) == sizeof(core::Range16));

    std::uint32_t failures = 0;
    std::uint32_t cases = 0;
    std::uint32_t scheduledCases = 0;
    std::uint32_t shadowCases = 0;

    Rng rng(0xC0115EEDu);
    for (std::uint32_t i = 0; i != kRandomCases; ++i) {
        const bool wideRegion = rng.Below(16) == 0;
        const bool shadowOnly = rng.Below(16) == 0;
        Scene scene = BuildScene(rng, wideRegion);
        const std::uint32_t workerIndex = rng.Below(kWorkers);
        const std::int32_t firstSlot = rng.Below(3) == 0
            ? static_cast<std::int32_t>(core::kTaskCapacity) - 1 - static_cast<std::int32_t>(rng.Below(4))
            : static_cast<std::int32_t>(rng.Below(4));

        const CaseResult result = RunCase(scene, workerIndex, firstSlot, shadowOnly);
        ++cases;
        if (shadowOnly)
            ++shadowCases;
        if (!result.verified) {
            if (++failures <= 5) {
                std::printf("FAIL case %u (worker %u, edgeCount %u, firstSlot %d, shadow %d): %s\n",
                            i, workerIndex, scene.edgeCount, firstSlot, shadowOnly ? 1 : 0, result.detail);
            }
        }

        // Track how often the split path is exercised so a green run proves coverage.
        if (g_trace.completionCount != 0)
            ++scheduledCases;
    }

    std::printf("%s: %u cases (%u shadow), %u exercised the scheduler path, %u mismatches\n",
                failures == 0 ? "PASS" : "FAIL", cases, shadowCases, scheduledCases, failures);

    // Focused scenes: the random suite does not produce these on its own.
    {
        Scene wraparound = BuildQueueWraparoundScene();
        const std::uint32_t wrapsBefore = oracle::coverage::queueWraps;
        const std::uint32_t pushesBefore = oracle::coverage::queuePushes;
        const std::uint32_t classifiedBefore = oracle::coverage::classifiedNodes;
        const std::uint32_t partialBefore = oracle::coverage::partialNodes;
        const std::uint32_t insideBefore = oracle::coverage::insideNodes;
        const std::uint32_t outsideBefore = oracle::coverage::outsideNodes;
        const CaseResult result = RunCase(wraparound, 0, 0, false);
        ++cases;
        if (!result.verified) {
            ++failures;
            std::printf("FAIL focused queue-wraparound scene: %s\n", result.detail);
        }
        const std::uint32_t pushes = oracle::coverage::queuePushes - pushesBefore;
        const std::uint32_t wraps = oracle::coverage::queueWraps - wrapsBefore;
        const bool wrapsOk = wraps > 0 && pushes > oracle::kQueueCapacity;
        std::printf("%s: queue wraparound scene pushed %u ranges (capacity %u) with %u wraps; delta classified=%u partial=%u inside=%u outside=%u\n",
                    wrapsOk ? "ok" : "FAIL", pushes, oracle::kQueueCapacity, wraps,
                    oracle::coverage::classifiedNodes - classifiedBefore,
                    oracle::coverage::partialNodes - partialBefore,
                    oracle::coverage::insideNodes - insideBefore,
                    oracle::coverage::outsideNodes - outsideBefore);
        if (!wrapsOk)
            ++failures;
    }
    {
        Scene straddling = BuildStraddlingBoxScene();
        const CaseResult result = RunCase(straddling, 0, 0, false);
        ++cases;
        if (!result.verified) {
            ++failures;
            std::printf("FAIL focused straddling-box scene: %s\n", result.detail);
        }
        else {
            std::printf("ok: straddling-box scene (%u node(s)) matched the reference\n",
                        static_cast<unsigned>(straddling.nodes.size()));
        }
    }

    // Coverage floors for the random suite: these are the paths optimizations keep touching, so
    // a fixture change that stops reaching one of them has to fail the run rather than quietly
    // weaken it. originCode == 0 is deliberately not a floor: a box the origin sits inside is
    // also straddling along the projection normal, so the depth test returns Partial first and
    // the exit is unreachable. The counter stays in the report so a change that made it live
    // would be visible here.
    struct CoverageFloor {
        const char* name;
        std::uint32_t value;
    };
    const CoverageFloor floors[] = {
        { "queued child ranges", oracle::coverage::queuePushes },
        { "queue wraparounds", oracle::coverage::queueWraps },
        { "emitted item ranges", oracle::coverage::emittedRanges },
        { "individual item classifications", oracle::coverage::individualItemTests },
        { "splits handed to the scheduler", oracle::coverage::splitToScheduler },
        { "nodes classified", oracle::coverage::classifiedNodes },
    };
    for (const CoverageFloor& floor : floors) {
        if (floor.value == 0) {
            std::printf("FAIL: no coverage for %s\n", floor.name);
            ++failures;
        }
    }
    std::printf("coverage: queuePushes=%u queueWraps=%u maxQueueOccupancy=%u emittedRanges=%u "
                "individualItemTests=%u splitToScheduler=%u nodes classified=%u "
                "(originCodeZero=%u, expected 0)\n",
                oracle::coverage::queuePushes, oracle::coverage::queueWraps,
                oracle::coverage::maxQueueOccupancy, oracle::coverage::emittedRanges,
                oracle::coverage::individualItemTests, oracle::coverage::splitToScheduler,
                oracle::coverage::classifiedNodes, oracle::coverage::originCodeZero);

    if (failures != 0)
        std::printf("FAIL: %u failures across %u cases\n", failures, cases);
    return failures == 0 ? 0 : 1;
}
