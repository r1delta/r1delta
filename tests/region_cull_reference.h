#pragma once
/*
 * TEST ORACLE ONLY: this is the unoptimized reconstruction of client.dll
 * sub_1802319F0 (x64, base 0x180000000, RVA 0x2319F0) that
 * client/culling/region_cull_core.h is a faithful optimization of. It is compiled
 * only into tests/region_cull_tests.cpp, never into the shipped module, and it is
 * driven there against synthetic scenes with the real constant tables: identical
 * outputs and identical scheduler calls are the pass condition for the optimized code.
 *
 * Readable reconstruction of sub_1802319F0 (x64, address 0x1802319F0).
 *
 * Evidence: the supplied Hex-Rays pseudocode and matching assembly listings.
 * Names below are descriptive inferences; original identifiers are unavailable.
 * The repeated geometric code has been factored into helpers. This does not
 * establish whether those helpers existed in the original source.
 *
 * Recovered behavior:
 *   Traverse a packed bounds hierarchy for a projected region; emit contiguous
 *   accepted item ranges, and schedule large child sets as type-7 work items.
 *
 * Integration requirements:
 *   - Supply the external globals/tables below from the original program.
 *     Their contents were not included in either attachment; no lookup values
 *     or scene data are fabricated here. Address comments identify each symbol.
 *   - Replace the provisional WT declarations with the real scheduler header.
 *     Begin(kind), End(kind,count), and Try(worker,kind,abort) are the call shapes
 *     supported by register setup. The abort callback's signature is unknown.
 *   - The 64-bit return is retained from the supplied decompilation. Assembly
 *     only proves that RAX from the second TryDoTaskItem survives the epilogue;
 *     the original callback could have returned a smaller type, or void.
 *
 * Original preconditions are retained: valid indices, nonempty region edges,
 * sufficient queue/output storage, valid SIMD lane conventions, and stable
 * scene/view data for the duration of the task. Numeric equivalence targets
 * finite geometry with the same floating-point mode. Factoring commutative
 * SIMD operations does not promise identical NaN payloads or exception timing.
 * This is not a binary patch or a standalone program; address comments alone
 * do not bind the extern declarations to the original process's memory.
 *
 * Compile as an object, for example:
 *   g++ -std=c++17 -O2 -msse2 -fno-fast-math -ffp-contract=off \
 *       -Wall -Wextra -Wpedantic -c sub_1802319F0_reconstructed.cpp
 */

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <immintrin.h>

namespace recovered {

// Coverage counters used only by the differential harness: a green run has to prove it
// reached the branches that optimizations are most likely to break (the origin-inside-box
// early exit, queue wraparound, nonzero starting output counts, item-heavy leaves), instead
// of relying on the printed case count.
namespace coverage {
inline std::uint32_t originCodeZero = 0;
inline std::uint32_t queuePushes = 0;
inline std::uint32_t queueWraps = 0;
inline std::uint32_t nonzeroStartCounts = 0;
inline std::uint32_t emittedRanges = 0;
inline std::uint32_t individualItemTests = 0;
inline std::uint32_t splitToScheduler = 0;
inline std::uint32_t maxQueueOccupancy = 0;
inline std::uint32_t classifiedNodes = 0;
inline std::uint32_t partialNodes = 0;
inline std::uint32_t insideNodes = 0;
inline std::uint32_t outsideNodes = 0;
}

using Vec4 = __m128;

constexpr std::uint32_t kTaskKind = 7;
constexpr std::uint32_t kTaskCapacity = 0x2000;
constexpr std::uint32_t kQueueCapacity = 0x400;
constexpr std::uint32_t kQueueMask = kQueueCapacity - 1;
constexpr std::uint32_t kOutputRangeCapacity = 0x8000;

struct Range16 {
    std::uint16_t first;
    std::uint16_t count;
};

struct CullTask {
    std::uint16_t regionBlock;  // Offset into the region arena, in 64-byte units.
    std::uint16_t rootNode;
};

// The fourth lane of each 16-byte half contains metadata, not a coordinate.
struct alignas(16) HierarchyNode {
    float minimum[3];
    std::uint8_t childCount;          // +0x0C
    std::uint8_t directItemCount;     // +0x0D
    std::uint16_t subtreeItemCount;   // +0x0E

    float maximum[3];
    std::uint16_t firstChild;         // +0x1C
    std::uint16_t firstItem;          // +0x1E
};

struct alignas(16) ItemBounds {
    Vec4 minimum;
    Vec4 maximum;
};

struct RegionHeader {
    std::uint16_t edgeCount;
    std::byte unknown[6];
};

struct RegionEdge {
    std::uint16_t planeIndex;
    std::uint8_t endpoint0;
    std::uint8_t endpoint1;
};

struct WorkerOutput {
    Range16 ranges[kOutputRangeCapacity];
    std::uint32_t count;
};

static_assert(sizeof(Range16) == 4);
static_assert(sizeof(CullTask) == 4);
static_assert(sizeof(HierarchyNode) == 32);
static_assert(offsetof(HierarchyNode, childCount) == 0x0C);
static_assert(offsetof(HierarchyNode, directItemCount) == 0x0D);
static_assert(offsetof(HierarchyNode, subtreeItemCount) == 0x0E);
static_assert(offsetof(HierarchyNode, maximum) == 0x10);
static_assert(offsetof(HierarchyNode, firstChild) == 0x1C);
static_assert(offsetof(HierarchyNode, firstItem) == 0x1E);
static_assert(sizeof(ItemBounds) == 32);
static_assert(sizeof(RegionHeader) == 8);
static_assert(sizeof(RegionEdge) == 4);
static_assert(offsetof(WorkerOutput, count) == 0x20000);
static_assert(sizeof(WorkerOutput) == 0x20004);

// Original globals, renamed by inferred role. Supply definitions when linking.
extern const HierarchyNode* g_nodes;            // qword_180F95168
extern const ItemBounds* g_itemBounds;          // qword_180F95190
extern std::byte g_regionArena[];               // unk_1812DCF40
extern CullTask g_tasks[kTaskCapacity];         // word_18131CE80 / CE82
extern WorkerOutput g_workerOutputs[];          // unk_181324E80 / 181344E80

extern const Vec4 g_xyzMask;                    // xmmword_18097FC90
extern const Vec4 g_absoluteValueMask;          // xmmword_18097FB60
extern const Vec4 g_cornerSelectMasks[16];      // xmmword_18097FB80
extern const std::uint32_t g_silhouetteCodes[]; // dword_180869300; likely 64 entries
extern const std::uint32_t g_edgeEvents[16];     // dword_180869400

extern Vec4 g_viewOrigin;                       // xmmword_181158500
extern Vec4 g_projectionNormal;                 // xmmword_181158530
extern Vec4 g_sidePlanes[8];                    // xmmword_181158540 .. 1811585B0
extern std::uint32_t g_sidePlaneCount;           // dword_1811585C0; only == 8 matters
extern std::uint32_t g_individualTestThreshold;  // dword_181158610
extern std::uint32_t g_taskSplitThreshold;       // dword_181158614
extern Vec4 g_regionEdgePlanes[];               // xmmword_18125CEA0

// PROVISIONAL integration declarations, not recovered API definitions.
// Define RECOVERED_WT_API_PROVIDED and include the real declarations instead
// when integrating. The function-pointer type here carries an opaque address;
// this translation unit never invokes the abort callback directly.
#ifndef RECOVERED_WT_API_PROVIDED
using OpaqueAbortCallback = void (*)();
extern void WT_DefaultAbortCondition();
extern std::int32_t WT_AppendTaskItems_Begin(std::uint32_t kind);
extern void WT_AppendTaskItems_End(std::uint32_t kind, std::uint32_t count);
extern std::int64_t WT_TryDoTaskItem(std::uint32_t workerIndex,
                                   std::uint32_t kind,
                                   OpaqueAbortCallback abortCondition);
#endif

namespace detail {

// Byte copies avoid C++ aliasing violations when viewing packed metadata as SIMD.
template <class T>
inline T loadBytes(const std::byte* address)
{
    T value;
    std::memcpy(&value, address, sizeof(value));
    return value;
}

struct RegionView {
    std::uint16_t edgeCount;
    const std::byte* edgeData;
    const std::byte* vertexData;

    RegionEdge edge(std::uint32_t index) const
    {
        return loadBytes<RegionEdge>(edgeData + sizeof(RegionEdge) * index);
    }

    Vec4 vertex(std::uint8_t index) const
    {
        return loadBytes<Vec4>(vertexData + sizeof(Vec4) * index);
    }
};

inline RegionView getRegion(std::uint16_t block)
{
    const std::byte* blob = g_regionArena + std::size_t(block) * 64;
    const auto header = loadBytes<RegionHeader>(blob);
    const std::size_t vertexOffset =
        (sizeof(RegionHeader) + sizeof(RegionEdge) * header.edgeCount + 15)
        & ~std::size_t(15);
    return {header.edgeCount, blob + sizeof(RegionHeader), blob + vertexOffset};
}

enum class Classification : std::uint32_t {
    Outside = 0,
    Partial = 1,  // Includes conservative acceptance near the projection plane.
    Inside = 2
};

// Four horizontal dot products. Keep the original (z + x) + y association.
inline Vec4 sumXYZ(Vec4 p0, Vec4 p1, Vec4 p2, Vec4 p3)
{
    _MM_TRANSPOSE4_PS(p0, p1, p2, p3);
    return _mm_add_ps(_mm_add_ps(p2, p0), p1);
}

// This block uses all FOUR lanes and the different (z + w) + (x + y) order.
inline Vec4 sumXYZW(Vec4 p0, Vec4 p1, Vec4 p2, Vec4 p3)
{
    _MM_TRANSPOSE4_PS(p0, p1, p2, p3);
    return _mm_add_ps(_mm_add_ps(p2, p3), _mm_add_ps(p0, p1));
}

// Low two lanes are dot3(a,normal), dot3(b,normal), in original SSE order.
inline Vec4 dotPair(Vec4 a, Vec4 b, Vec4 normal)
{
    const Vec4 aProducts = _mm_mul_ps(a, normal);
    const Vec4 bProducts = _mm_mul_ps(b, normal);
    const Vec4 xy = _mm_unpacklo_ps(aProducts, bProducts);
    return _mm_add_ps(_mm_add_ps(_mm_unpackhi_ps(aProducts, bProducts), xy),
                      _mm_shuffle_ps(xy, xy, 0x4E));
}

inline Vec4 crossXYZ(Vec4 a, Vec4 b)
{
    const Vec4 bYZX = _mm_shuffle_ps(b, b, 0xC9);
    const Vec4 products = _mm_mul_ps(a, bYZX);
    return _mm_sub_ps(_mm_shuffle_ps(products, products, 0xC9),
                      _mm_mul_ps(_mm_shuffle_ps(a, a, 0xD2), bYZX));
}

inline int depthSignBits(Vec4 minimum, Vec4 maximum, Vec4 normal)
{
    const Vec4 p0 = _mm_mul_ps(minimum, normal);
    const Vec4 p1 = _mm_mul_ps(maximum, normal);
    const Vec4 lower = _mm_min_ps(p0, p1);
    const Vec4 upper = _mm_max_ps(p0, p1);
    const Vec4 xy = _mm_unpacklo_ps(lower, upper);
    const Vec4 depths = _mm_add_ps(
        _mm_add_ps(_mm_unpackhi_ps(lower, upper), xy),
        _mm_movehl_ps(xy, xy));
    return _mm_movemask_ps(depths);  // Sign bits, not an ordered < 0 comparison.
}

inline bool outsideFourPlanes(Vec4 minimum, Vec4 maximum, const Vec4* planes)
{
    const Vec4 p0 = _mm_max_ps(_mm_mul_ps(minimum, planes[0]),
                                _mm_mul_ps(maximum, planes[0]));
    const Vec4 p1 = _mm_max_ps(_mm_mul_ps(minimum, planes[1]),
                                _mm_mul_ps(maximum, planes[1]));
    const Vec4 p2 = _mm_max_ps(_mm_mul_ps(minimum, planes[2]),
                                _mm_mul_ps(maximum, planes[2]));
    const Vec4 p3 = _mm_max_ps(_mm_mul_ps(minimum, planes[3]),
                                _mm_mul_ps(maximum, planes[3]));
    return _mm_movemask_ps(sumXYZW(p0, p1, p2, p3)) != 0;
}

inline Vec4 selectCorner(Vec4 minimum, Vec4 maximum, std::uint32_t selector)
{
    const Vec4 mask = g_cornerSelectMasks[selector & 15];
    return _mm_or_ps(_mm_and_ps(mask, maximum), _mm_andnot_ps(mask, minimum));
}

struct ProjectedSilhouette {
    Vec4 vertices[7];  // Four or six corners, followed by a repeated first corner.
    Vec4 edgePlanes[6];
    std::uint32_t count;
};

inline ProjectedSilhouette projectSilhouette(Vec4 minimum, Vec4 maximum,
                                              Vec4 normal, std::uint32_t code)
{
    ProjectedSilhouette result{};
    const Vec4 a = selectCorner(minimum, maximum, code);
    const Vec4 b = selectCorner(minimum, maximum, code >> 4);
    const Vec4 c = selectCorner(minimum, maximum, code >> 8);
    const Vec4 d = selectCorner(minimum, maximum, code >> 12);

    // Normalize the first four corners together. RCPPS is deliberately retained;
    // division or a refined reciprocal can change classification at boundaries.
    Vec4 x = a, y = b, z = c, unusedW = d;
    _MM_TRANSPOSE4_PS(x, y, z, unusedW);
    const Vec4 inverseDepths = _mm_rcp_ps(_mm_add_ps(
        _mm_add_ps(_mm_mul_ps(z, _mm_shuffle_ps(normal, normal, 0xAA)),
                   _mm_mul_ps(x, _mm_shuffle_ps(normal, normal, 0x00))),
        _mm_mul_ps(y, _mm_shuffle_ps(normal, normal, 0x55))));
    result.vertices[0] = _mm_mul_ps(a, _mm_shuffle_ps(inverseDepths, inverseDepths, 0x00));
    result.vertices[1] = _mm_mul_ps(b, _mm_shuffle_ps(inverseDepths, inverseDepths, 0x55));
    result.vertices[2] = _mm_mul_ps(c, _mm_shuffle_ps(inverseDepths, inverseDepths, 0xAA));
    result.vertices[3] = _mm_mul_ps(d, _mm_shuffle_ps(inverseDepths, inverseDepths, 0xFF));

    // This is the exact observed bit test, not a guessed encoded count field.
    if ((code & 0x0FF00000u) != 0) {
        const Vec4 e = selectCorner(minimum, maximum, code >> 16);
        const Vec4 f = selectCorner(minimum, maximum, code >> 20);
        const Vec4 inversePair = _mm_rcp_ps(dotPair(e, f, normal));
        result.vertices[4] = _mm_mul_ps(e, _mm_shuffle_ps(inversePair, inversePair, 0x00));
        result.vertices[5] = _mm_mul_ps(f, _mm_shuffle_ps(inversePair, inversePair, 0x55));
        result.count = 6;
    } else {
        result.count = 4;
    }

    result.vertices[result.count] = result.vertices[0];
    for (std::uint32_t i = 0; i != result.count; ++i)
        result.edgePlanes[i] = crossXYZ(result.vertices[i], result.vertices[i + 1]);
    return result;
}

inline Classification classifyProjectedSilhouette(const ProjectedSilhouette& box,
                                                    const RegionView& region)
{
    const Vec4 zero = _mm_setzero_ps();
    std::uint32_t boxEdgeParity = 0;  // Persists across ALL region edges.
    std::uint32_t regionEdgeIndex = 0;

    // The original executes at least once; a valid region has nonzero edgeCount.
    do {
        const RegionEdge edge = region.edge(regionEdgeIndex);
        const Vec4 plane = g_regionEdgePlanes[edge.planeIndex];
        const Vec4 u = region.vertex(edge.endpoint0);
        const Vec4 v = region.vertex(edge.endpoint1);
        std::uint32_t regionEdgeParity = 0;
        std::uint32_t combinedEvents = 0;

        for (std::uint32_t i = 0; i != box.count; ++i) {
            const Vec4 boxPlane = box.edgePlanes[i];
            // The order is a,c,b,d, not a,b,c,d:
            //   a = dot3(box vertex i,     region edge plane)
            //   c = dot3(box edge plane,   region endpoint 0)
            //   b = dot3(box vertex i + 1, region edge plane)
            //   d = dot3(box edge plane,   region endpoint 1)
            const Vec4 dots = sumXYZ(
                _mm_mul_ps(box.vertices[i], plane), _mm_mul_ps(boxPlane, u),
                _mm_mul_ps(box.vertices[i + 1], plane), _mm_mul_ps(boxPlane, v));
            const int signs = _mm_movemask_ps(_mm_cmplt_ps(dots, zero));
            const std::uint32_t events = g_edgeEvents[signs];
            combinedEvents |= events;

            if ((events & 3) != 0) {
                const Vec4 magnitudes = _mm_and_ps(g_absoluteValueMask, dots);
                const Vec4 opposite = _mm_movehl_ps(zero, magnitudes); // [|b|,|d|,0,0]
                const std::uint32_t crossings = events & std::uint32_t(
                    _mm_movemask_ps(_mm_cmplt_ps(magnitudes, opposite)));
                boxEdgeParity ^= (crossings & 1) << i;
                regionEdgeParity ^= crossings;
            }
        }

        if (((regionEdgeParity & 2) | (combinedEvents & 4)) != 0)
            return Classification::Partial;
        ++regionEdgeIndex;
    } while (regionEdgeIndex < region.edgeCount);

    return boxEdgeParity != 0 ? Classification::Inside : Classification::Outside;
}

inline Classification classifyBounds(Vec4 minimum, Vec4 maximum,
                                      const RegionView& region)
{
    const Vec4 normal = g_projectionNormal;
    const int depthSigns = depthSignBits(minimum, maximum, normal);

    if ((depthSigns & 1) != 0) {
        if ((depthSigns & 2) != 0)
            return Classification::Outside;
        if (outsideFourPlanes(minimum, maximum, g_sidePlanes))
            return Classification::Outside;
        if (g_sidePlaneCount == 8 &&
            outsideFourPlanes(minimum, maximum, g_sidePlanes + 4))
            return Classification::Outside;
        return Classification::Partial;
    }

    const Vec4 zero = _mm_setzero_ps();
    const int originCode = _mm_movemask_ps(_mm_cmplt_ps(zero, minimum))
                         | (_mm_movemask_ps(_mm_cmplt_ps(maximum, zero)) << 3);
    if (originCode == 0) {
        ++coverage::originCodeZero;
        return Classification::Partial;
    }

    // Do not add '& 7' to the movemasks: the source listing does not do so.
    // Valid 3D lane conventions produce the expected six-bit table index.
    const auto box = projectSilhouette(minimum, maximum, normal,
                                       g_silhouetteCodes[originCode]);
    return classifyProjectedSilhouette(box, region);
}

inline Classification classifyNode(const HierarchyNode& node, Vec4 origin,
                                    const RegionView& region)
{
    const auto* bytes = reinterpret_cast<const std::byte*>(&node);
    const Vec4 minimum = _mm_sub_ps(
        _mm_and_ps(loadBytes<Vec4>(bytes), g_xyzMask), origin);
    const Vec4 maximum = _mm_sub_ps(
        _mm_and_ps(loadBytes<Vec4>(bytes + 16), g_xyzMask), origin);
    const Classification classification = classifyBounds(minimum, maximum, region);
    ++coverage::classifiedNodes;
    if (classification == Classification::Partial) ++coverage::partialNodes;
    else if (classification == Classification::Inside) ++coverage::insideNodes;
    else ++coverage::outsideNodes;
    return classification;
}

inline Classification classifyItem(const ItemBounds& bounds, Vec4 origin,
                                    const RegionView& region)
{
    // Unlike node loads, these two vectors are NOT masked in the assembly.
    return classifyBounds(_mm_sub_ps(bounds.minimum, origin),
                           _mm_sub_ps(bounds.maximum, origin), region);
}

inline void emitRange(WorkerOutput& output, std::uint32_t& outputCount,
                       std::uint16_t first, std::uint16_t count)
{
    if (count != 0)
        output.ranges[outputCount++] = {first, count};
}

inline void scheduleChildren(std::uint16_t regionBlock, const HierarchyNode& node)
{
    const std::uint32_t childCount = node.childCount;
    const std::int32_t firstSlot = WT_AppendTaskItems_Begin(kTaskKind);
    std::uint32_t appended = 0;

    // Keep the unsigned arithmetic/strict inequality from 0x1802322F3..22FF.
    if (childCount - 1 < kTaskCapacity - std::uint32_t(firstSlot)) {
        CullTask* destination = g_tasks + firstSlot;
        std::uint16_t child = node.firstChild;
        for (std::uint32_t i = 0; i != childCount; ++i, ++child)
            destination[i] = {regionBlock, child};
        appended = childCount;
    }

    // No local fallback appears when capacity is insufficient.
    WT_AppendTaskItems_End(kTaskKind, appended);
}

inline void emitTestedItems(const HierarchyNode& node, Vec4 origin,
                            const RegionView& region, WorkerOutput& output,
                            std::uint32_t& outputCount)
{
    std::uint32_t runStart = 0;
    const std::uint32_t itemCount = node.directItemCount;

    for (std::uint32_t i = 0; i != itemCount; ++i) {
        const auto& bounds = g_itemBounds[std::uint32_t(node.firstItem) + i];
        if (classifyItem(bounds, origin, region) == Classification::Outside) {
            emitRange(output, outputCount, std::uint16_t(node.firstItem + runStart),
                       std::uint16_t(i - runStart));
            runStart = i + 1;
        }
    }

    emitRange(output, outputCount, std::uint16_t(node.firstItem + runStart),
               std::uint16_t(itemCount - runStart));
}

} // namespace detail

// Suggested descriptive name: processRegionCullTask.
// Original source name and true return type cannot be recovered from this dump.
std::int64_t sub_1802319F0(std::uint32_t workerIndex, std::uint32_t taskIndex)
{
    using detail::Classification;

    const CullTask task = g_tasks[taskIndex];
    const detail::RegionView region = detail::getRegion(task.regionBlock);
    const Vec4 origin = _mm_and_ps(g_xyzMask, g_viewOrigin);

    // MOVSXD on ECX precedes the 0x20004-byte worker stride in the original.
    WorkerOutput& output = g_workerOutputs[static_cast<std::int32_t>(workerIndex)];
    std::uint32_t outputCount = output.count;
    if (outputCount != 0)
        ++coverage::nonzeroStartCounts;

    Range16 queue[kQueueCapacity];
    std::uint32_t head = 0;
    std::uint32_t tail = 1;
    queue[0] = {task.rootNode, 1};

    do {
        const Range16 range = queue[head];
        head = (head + 1) & kQueueMask;

        std::size_t nodeIndex = range.first;
        for (std::uint32_t remaining = range.count; remaining != 0;
             --remaining, ++nodeIndex) {
            const HierarchyNode& node = g_nodes[nodeIndex];
            const Classification classification = detail::classifyNode(node, origin, region);

            if (classification == Classification::Outside)
                continue;

            if (classification == Classification::Inside) {
                coverage::emittedRanges += node.subtreeItemCount != 0 ? 1u : 0u;
                detail::emitRange(output, outputCount, node.firstItem, node.subtreeItemCount);
                continue;
            }

            if (node.childCount != 0) {
                const std::uint32_t descendantItems =
                    std::uint32_t(node.subtreeItemCount) - std::uint32_t(node.directItemCount);
                if (descendantItems <= g_taskSplitThreshold) {
                    queue[tail] = {node.firstChild, node.childCount};
                    ++coverage::queuePushes;
                    const std::uint32_t nextTail = (tail + 1) & kQueueMask;
                    if (nextTail <= tail)
                        ++coverage::queueWraps;
                    tail = nextTail;
                    const std::uint32_t occupancy = (tail - head) & kQueueMask;
                    if (occupancy > coverage::maxQueueOccupancy)
                        coverage::maxQueueOccupancy = occupancy;
                } else {
                    // The assembly reloads this field before reserving children.
                    ++coverage::splitToScheduler;
                    detail::scheduleChildren(g_tasks[taskIndex].regionBlock, node);
                }

                // Direct items on internal nodes are emitted without individual tests.
                coverage::emittedRanges += node.directItemCount != 0 ? 1u : 0u;
                detail::emitRange(output, outputCount, node.firstItem, node.directItemCount);
                continue;
            }

            if (node.directItemCount <= g_individualTestThreshold) {
                coverage::emittedRanges += node.directItemCount != 0 ? 1u : 0u;
                detail::emitRange(output, outputCount, node.firstItem, node.directItemCount);
            } else {
                coverage::individualItemTests += node.directItemCount;
                detail::emitTestedItems(node, origin, region, output, outputCount);
            }
        }
    } while (head != tail);

    output.count = outputCount;
    WT_TryDoTaskItem(workerIndex, 0, WT_DefaultAbortCondition);
    return WT_TryDoTaskItem(workerIndex, 3, WT_DefaultAbortCondition);
}

} // namespace recovered
