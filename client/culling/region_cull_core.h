#pragma once
// Optimized reimplementation of the client.dll region-cull task (sub_1802319F0).
//
// Layout, constants and call shapes were recovered from the retail client.dll IDB
// (image base 0x180000000; function at +0x2319F0, 0x1802319F0-0x180232AC6; scheduler
// WT_* imported from vstdlib.dll). This header is pure logic: every global arrives
// through CullEnv so the same code runs against live client memory and against
// synthetic test scenes.
//
// Numeric behavior is intentionally preserved: RCPPS stays approximate, dot-product and
// side-plane reductions keep the original association, sign-bit tests stay distinct from
// ordered comparisons, and no fast-math is used.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <immintrin.h>

namespace region_cull {

using Vec4 = __m128;

constexpr std::uint32_t kTaskKind = 7;
constexpr std::uint32_t kTaskCapacity = 0x2000;
constexpr std::uint32_t kQueueCapacity = 0x400;
constexpr std::uint32_t kQueueMask = kQueueCapacity - 1;
constexpr std::uint32_t kOutputRangeCapacity = 0x8000;
constexpr std::uint32_t kWorkerOutputStride = 0x20004;
constexpr std::uint32_t kWorkerOutputCountOffset = 0x20000;
constexpr std::uint32_t kPreparedEdgeLimit = 128;

struct Range16 {
    std::uint16_t first;
    std::uint16_t count;
};

struct CullTask {
    std::uint16_t regionBlock;
    std::uint16_t rootNode;
};

// The fourth lane of each 16-byte half holds metadata, not a coordinate.
struct alignas(16) HierarchyNode {
    float minimum[3];
    std::uint8_t childCount;         // +0x0C
    std::uint8_t directItemCount;    // +0x0D
    std::uint16_t subtreeItemCount;  // +0x0E

    float maximum[3];
    std::uint16_t firstChild;        // +0x1C
    std::uint16_t firstItem;         // +0x1E
};

struct alignas(16) ItemBounds {
    Vec4 minimum;
    Vec4 maximum;
};

struct RegionHeader {
    std::uint16_t edgeCount;
    std::uint8_t reserved[6];
};

struct RegionEdge {
    std::uint16_t planeIndex;
    std::uint8_t endpoint0;
    std::uint8_t endpoint1;
};

struct alignas(16) PreparedEdge {
    Vec4 plane;
    Vec4 u;
    Vec4 v;
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
static_assert(offsetof(PreparedEdge, plane) == 0);
static_assert(offsetof(PreparedEdge, u) == 16);
static_assert(offsetof(PreparedEdge, v) == 32);

enum class Classification : std::uint32_t {
    Outside = 0,
    Partial = 1,  // Includes conservative acceptance near the projection plane.
    Inside = 2
};

// Scheduler access (WT_* in vstdlib.dll). Slots point at the live task array.
// tryDoTaskItem/abortCondition are the two completion calls the original makes on
// the worker after the traversal (kinds 0 and 3).
struct CullScheduler {
    CullTask* slots = nullptr;                              // g_tasks, client+0x131CE80
    std::int32_t (*appendBegin)(std::uint32_t kind) = nullptr;   // WT_AppendTaskItems_Begin
    void (*appendEnd)(std::uint32_t kind, std::uint32_t count) = nullptr;  // WT_AppendTaskItems_End
    std::int64_t (*tryDoTaskItem)(std::uint32_t workerIndex, std::uint32_t kind,
                                  void (*abortCondition)()) = nullptr;      // WT_TryDoTaskItem
    void (*abortCondition)() = nullptr;                            // WT_DefaultAbortCondition
};

// Every global the task reads, resolved by the caller. Members that are pointers to
// pointers are read once per task, because the scene swaps the arrays per map.
struct CullEnv {
    const HierarchyNode* const* nodes = nullptr;      // qword_180F95168
    const ItemBounds* const* items = nullptr;         // qword_180F95190
    const std::uint8_t* regionArena = nullptr;        // unk_1812DCF40
    CullTask* tasks = nullptr;                        // word_18131CE80
    std::uint8_t* workerOutputs = nullptr;            // unk_181324E80
    const std::uint32_t* silhouetteCodes = nullptr;   // dword_180869300 (64 entries)
    const std::uint32_t* edgeEvents = nullptr;        // dword_180869400 (16 entries)
    const Vec4* xyzMask = nullptr;                    // xmmword_18097FC90
    const Vec4* absoluteValueMask = nullptr;          // xmmword_18097FB60
    const Vec4* cornerSelectMasks = nullptr;          // xmmword_18097FB80 (16 entries)
    const Vec4* viewOrigin = nullptr;                 // xmmword_181158500
    const Vec4* projectionNormal = nullptr;           // xmmword_181158530
    const Vec4* sidePlanes = nullptr;                 // xmmword_181158540 (8 entries)
    const std::uint32_t* sidePlaneCount = nullptr;    // dword_1811585C0 (only == 8 matters)
    const std::uint32_t* individualTestThreshold = nullptr;  // dword_181158610
    const std::uint32_t* taskSplitThreshold = nullptr;       // dword_181158614
    const Vec4* regionEdgePlanes = nullptr;           // xmmword_18125CEA0
    CullScheduler scheduler{};
};

// Output surface of one worker: range array plus the running count the engine owns.
struct WorkerOutput {
    Range16* ranges = nullptr;
    std::uint32_t* count = nullptr;
};

namespace detail {

template <class T>
inline T loadBytes(const std::uint8_t* address)
{
    T value;
    std::memcpy(&value, address, sizeof(value));
    return value;
}

struct RegionView {
    std::uint16_t edgeCount;
    const std::uint8_t* edgeData;
    const std::uint8_t* vertexData;

    RegionEdge edge(std::uint32_t index) const
    {
        return loadBytes<RegionEdge>(edgeData + sizeof(RegionEdge) * index);
    }

    Vec4 vertex(std::uint8_t index) const
    {
        return loadBytes<Vec4>(vertexData + sizeof(Vec4) * index);
    }
};

inline RegionView getRegion(const CullEnv& env, std::uint16_t block)
{
    const std::uint8_t* blob = env.regionArena + std::size_t(block) * 64;
    const RegionHeader header = loadBytes<RegionHeader>(blob);
    const std::size_t vertexOffset =
        (sizeof(RegionHeader) + sizeof(RegionEdge) * header.edgeCount + 15)
        & ~std::size_t(15);
    return { header.edgeCount, blob + sizeof(RegionHeader), blob + vertexOffset };
}

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

inline Vec4 selectCorner(const Vec4* cornerSelectMasks, Vec4 minimum, Vec4 maximum,
                         std::uint32_t selector)
{
    const Vec4 mask = cornerSelectMasks[selector & 15];
    return _mm_or_ps(_mm_and_ps(mask, maximum), _mm_andnot_ps(mask, minimum));
}

struct ProjectedSilhouette {
    Vec4 vertices[7];  // Four or six corners, followed by a repeated first corner.
    Vec4 edgePlanes[6];
    std::uint32_t count;
};

inline ProjectedSilhouette projectSilhouette(const CullEnv& env, Vec4 minimum, Vec4 maximum,
                                             Vec4 normal, std::uint32_t code)
{
    // Deliberately not zero-initialized: the classifier only reads the active
    // vertices, the active edge planes and the repeated closing vertex.
    ProjectedSilhouette result;
    const Vec4* const masks = env.cornerSelectMasks;
    const Vec4 a = selectCorner(masks, minimum, maximum, code);
    const Vec4 b = selectCorner(masks, minimum, maximum, code >> 4);
    const Vec4 c = selectCorner(masks, minimum, maximum, code >> 8);
    const Vec4 d = selectCorner(masks, minimum, maximum, code >> 12);

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
        const Vec4 e = selectCorner(masks, minimum, maximum, code >> 16);
        const Vec4 f = selectCorner(masks, minimum, maximum, code >> 20);
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

// Per-task edge cache: the region is invariant for the whole task, while the original
// reloads the indexed plane/vertex triples for every box it classifies. Only the prefix the
// classifier actually reaches is gathered: every invocation starts at edge zero and advances
// by one, so an early exit (the common case for a rejected box) leaves a short prefix behind
// instead of preparing all edges the original never touches. Above the scratch capacity the
// gather is skipped and edges are read through.
struct PreparedRegion {
    const CullEnv* env = nullptr;
    RegionView region{};
    PreparedEdge* scratch = nullptr;
    std::uint32_t count = 0;
    std::uint32_t filledCount = 0;
    bool usable = false;

    inline PreparedEdge edgeAt(std::uint32_t index)
    {
        if (usable) {
            if (index < filledCount)
                return scratch[index];

            const RegionEdge e = region.edge(index);
            PreparedEdge preparedEdge;
            preparedEdge.plane = env->regionEdgePlanes[e.planeIndex];
            preparedEdge.u = region.vertex(e.endpoint0);
            preparedEdge.v = region.vertex(e.endpoint1);

            // Gather-and-return: filling through a loop and reloading the entry costs
            // complete scans more than the reload it saves.
            scratch[index] = preparedEdge;
            filledCount = index + 1;
            return preparedEdge;
        }
        const RegionEdge e = region.edge(index);
        PreparedEdge p;
        p.plane = env->regionEdgePlanes[e.planeIndex];
        p.u = region.vertex(e.endpoint0);
        p.v = region.vertex(e.endpoint1);
        return p;
    }
};

inline PreparedRegion prepareRegion(const CullEnv& env, const RegionView& region,
                                    PreparedEdge* scratch, std::uint32_t scratchCapacity)
{
    PreparedRegion prepared;
    prepared.env = &env;
    prepared.region = region;
    prepared.scratch = scratch;
    prepared.count = region.edgeCount;
    // A zero-edge region still executes one edge test in the original (do/while), so it
    // must be read through the on-demand path rather than from a cache that holds nothing.
    prepared.usable = region.edgeCount != 0 && region.edgeCount <= scratchCapacity && scratch != nullptr;
    return prepared;
}

inline Classification classifyProjectedSilhouette(PreparedRegion& prepared,
                                                  const ProjectedSilhouette& box)
{
    const Vec4 zero = _mm_setzero_ps();
    const Vec4 absoluteValueMask = *prepared.env->absoluteValueMask;
    const std::uint32_t* const edgeEvents = prepared.env->edgeEvents;
    std::uint32_t boxEdgeParity = 0;  // Persists across ALL region edges.
    std::uint32_t regionEdgeIndex = 0;

    // The original executes at least once; a valid region has nonzero edgeCount.
    do {
        const PreparedEdge edge = prepared.edgeAt(regionEdgeIndex);
        const Vec4 plane = edge.plane;
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
                _mm_mul_ps(box.vertices[i], plane), _mm_mul_ps(boxPlane, edge.u),
                _mm_mul_ps(box.vertices[i + 1], plane), _mm_mul_ps(boxPlane, edge.v));
            const int signs = _mm_movemask_ps(_mm_cmplt_ps(dots, zero));
            const std::uint32_t events = edgeEvents[signs];

            // Bit 4 is sticky within this region edge: combinedEvents is only OR-ed,
            // and a Partial result does not depend on the remaining box edges.
            if ((events & 4) != 0)
                return Classification::Partial;

            combinedEvents |= events;

            if ((events & 3) != 0) {
                const Vec4 magnitudes = _mm_and_ps(absoluteValueMask, dots);
                const Vec4 opposite = _mm_movehl_ps(zero, magnitudes);  // [|b|,|d|,0,0]
                const std::uint32_t crossings = events & std::uint32_t(
                    _mm_movemask_ps(_mm_cmplt_ps(magnitudes, opposite)));
                boxEdgeParity ^= (crossings & 1) << i;
                regionEdgeParity ^= crossings;
            }
        }

        if ((regionEdgeParity & 2) != 0)
            return Classification::Partial;
        ++regionEdgeIndex;
    } while (regionEdgeIndex < prepared.count);

    return boxEdgeParity != 0 ? Classification::Inside : Classification::Outside;
}

inline Classification classifyBounds(const CullEnv& env, PreparedRegion& prepared,
                                     Vec4 minimum, Vec4 maximum)
{
    const Vec4 normal = *env.projectionNormal;
    const int depthSigns = depthSignBits(minimum, maximum, normal);

    if ((depthSigns & 1) != 0) {
        if ((depthSigns & 2) != 0)
            return Classification::Outside;
        if (outsideFourPlanes(minimum, maximum, env.sidePlanes))
            return Classification::Outside;
        if (*env.sidePlaneCount == 8 &&
            outsideFourPlanes(minimum, maximum, env.sidePlanes + 4))
            return Classification::Outside;
        return Classification::Partial;
    }

    const Vec4 zero = _mm_setzero_ps();
    const int originCode = _mm_movemask_ps(_mm_cmplt_ps(zero, minimum))
                         | (_mm_movemask_ps(_mm_cmplt_ps(maximum, zero)) << 3);
    if (originCode == 0)
        return Classification::Partial;

    // No '& 7' on the movemasks: the source listing does not mask them either.
    const auto box = projectSilhouette(env, minimum, maximum, normal,
                                       env.silhouetteCodes[originCode]);
    return classifyProjectedSilhouette(prepared, box);
}

inline Classification classifyNode(const CullEnv& env, PreparedRegion& prepared,
                                   const Vec4& origin, const HierarchyNode& node)
{
    const std::uint8_t* bytes = reinterpret_cast<const std::uint8_t*>(&node);
    const Vec4 minimum = _mm_sub_ps(
        _mm_and_ps(loadBytes<Vec4>(bytes), *env.xyzMask), origin);
    const Vec4 maximum = _mm_sub_ps(
        _mm_and_ps(loadBytes<Vec4>(bytes + 16), *env.xyzMask), origin);
    return classifyBounds(env, prepared, minimum, maximum);
}

inline Classification classifyItem(const CullEnv& env, PreparedRegion& prepared,
                                   const Vec4& origin, const ItemBounds& bounds)
{
    // Unlike node loads, these two vectors are NOT masked in the assembly.
    return classifyBounds(env, prepared,
                          _mm_sub_ps(bounds.minimum, origin),
                          _mm_sub_ps(bounds.maximum, origin));
}

inline void emitRange(WorkerOutput& output, std::uint32_t& outputCount,
                      std::uint16_t first, std::uint16_t count)
{
    if (count != 0)
        output.ranges[outputCount++] = { first, count };
}

// Commits child tasks through the WT scheduler; sends nothing when commit is false
// (shadow/verification runs observe the classification without mutating scheduler state).
inline void scheduleChildren(const CullEnv& env, std::uint16_t regionBlock,
                             const HierarchyNode& node, bool commit)
{
    if (!commit)
        return;

    const std::uint32_t childCount = node.childCount;
    const std::int32_t firstSlot = env.scheduler.appendBegin(kTaskKind);
    std::uint32_t appended = 0;

    // Keep the unsigned arithmetic/strict inequality from 0x1802322F3..22FF.
    if (childCount - 1 < kTaskCapacity - std::uint32_t(firstSlot)) {
        CullTask* destination = env.scheduler.slots + firstSlot;
        std::uint16_t child = node.firstChild;
        for (std::uint32_t i = 0; i != childCount; ++i, ++child)
            destination[i] = { regionBlock, child };
        appended = childCount;
    }

    // No local fallback appears when the reservation lacks capacity.
    env.scheduler.appendEnd(kTaskKind, appended);
}

inline void emitTestedItems(const CullEnv& env, PreparedRegion& prepared,
                            const HierarchyNode& node, const Vec4& origin,
                            WorkerOutput& output, std::uint32_t& outputCount)
{
    const ItemBounds* const items = *env.items;
    std::uint32_t runStart = 0;
    const std::uint32_t itemCount = node.directItemCount;

    for (std::uint32_t i = 0; i != itemCount; ++i) {
        const ItemBounds& bounds = items[std::uint32_t(node.firstItem) + i];
        if (classifyItem(env, prepared, origin, bounds) == Classification::Outside) {
            emitRange(output, outputCount,
                      std::uint16_t(node.firstItem + runStart),
                      std::uint16_t(i - runStart));
            runStart = i + 1;
        }
    }

    emitRange(output, outputCount, std::uint16_t(node.firstItem + runStart),
              std::uint16_t(itemCount - runStart));
}

} // namespace detail

// Reference behavior: traverse the packed hierarchy for the task's projected region,
// emit contiguous accepted item ranges, split oversized child sets onto the scheduler,
// then run the two worker completion calls. Returns the second completion result (the
// original returns RAX of the last WT_TryDoTaskItem call).
//
// The starting task is a parameter rather than a live read: production passes the slot's
// current contents, while the verification shadow passes the copy it took before the original
// ran, because the scheduler's append calls can reuse that slot for a child.
inline std::int64_t ProcessRegionCullTaskFromTask(const CullEnv& env, std::uint32_t workerIndex,
                                                  std::uint32_t taskIndex, bool commitScheduler,
                                                  const CullTask& task)
{
    using namespace detail;
    using Classification = region_cull::Classification;

    const RegionView region = getRegion(env, task.regionBlock);

    PreparedEdge edgeScratch[kPreparedEdgeLimit];
    PreparedRegion prepared = prepareRegion(env, region, edgeScratch, kPreparedEdgeLimit);

    const Vec4 origin = _mm_and_ps(*env.xyzMask, *env.viewOrigin);

    // MOVSXD on ECX precedes the 0x20004-byte worker stride in the original.
    std::uint8_t* const workerBase =
        env.workerOutputs + (std::size_t)static_cast<std::int32_t>(workerIndex) * kWorkerOutputStride;
    WorkerOutput output{
        reinterpret_cast<Range16*>(workerBase),
        reinterpret_cast<std::uint32_t*>(workerBase + kWorkerOutputCountOffset)
    };

    const HierarchyNode* const nodes = *env.nodes;
    std::uint32_t outputCount = *output.count;

    Range16 queue[kQueueCapacity];
    std::uint32_t head = 0;
    std::uint32_t tail = 1;
    queue[0] = { task.rootNode, 1 };

    do {
        const Range16 range = queue[head];
        head = (head + 1) & kQueueMask;

        std::size_t nodeIndex = range.first;
        for (std::uint32_t remaining = range.count; remaining != 0;
             --remaining, ++nodeIndex) {
            const HierarchyNode& node = nodes[nodeIndex];
            const Classification classification = classifyNode(env, prepared, origin, node);

            if (classification == Classification::Outside)
                continue;

            if (classification == Classification::Inside) {
                emitRange(output, outputCount, node.firstItem, node.subtreeItemCount);
                continue;
            }

            if (node.childCount != 0) {
                const std::uint32_t descendantItems =
                    std::uint32_t(node.subtreeItemCount) - std::uint32_t(node.directItemCount);
                if (descendantItems <= *env.taskSplitThreshold) {
                    queue[tail] = { node.firstChild, node.childCount };
                    tail = (tail + 1) & kQueueMask;
                } else {
                    // The original reloads this field before reserving children; the shadow
                    // reuses its snapshot instead, because that slot may already belong to a
                    // child the original scheduled.
                    const std::uint16_t childRegionBlock = commitScheduler
                        ? env.tasks[taskIndex].regionBlock
                        : task.regionBlock;
                    scheduleChildren(env, childRegionBlock, node, commitScheduler);
                }

                // Direct items on internal nodes are emitted without individual tests.
                emitRange(output, outputCount, node.firstItem, node.directItemCount);
                continue;
            }

            if (node.directItemCount <= *env.individualTestThreshold) {
                emitRange(output, outputCount, node.firstItem, node.directItemCount);
            } else {
                emitTestedItems(env, prepared, node, origin, output, outputCount);
            }
        }
    } while (head != tail);

    *output.count = outputCount;

    if (commitScheduler && env.scheduler.tryDoTaskItem) {
        const CullScheduler& scheduler = env.scheduler;
        scheduler.tryDoTaskItem(workerIndex, 0, scheduler.abortCondition);
        return scheduler.tryDoTaskItem(workerIndex, 3, scheduler.abortCondition);
    }
    return 0;
}

// Production entry: the task slot is read here, exactly where the original reads it.
inline std::int64_t ProcessRegionCullTask(const CullEnv& env, std::uint32_t workerIndex,
                                         std::uint32_t taskIndex, bool commitScheduler)
{
    return ProcessRegionCullTaskFromTask(env, workerIndex, taskIndex, commitScheduler,
                                         env.tasks[taskIndex]);
}

} // namespace region_cull
