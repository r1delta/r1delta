#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "graphics_flight_recorder.h"

namespace r1delta::graphics_flight_recorder::detail
{
// Raw little-endian x64 layout, independent of the existing capture manifest.
// A slot is usable only when its stamp has a nonzero version and Busy is clear.
// Sealed|Busy is permanently incomplete, including after an interrupted writer
// resumes. Offline readers must also compare a resource's IdentityEpoch with its
// bucket epoch. Borrowed addresses are evidence, never an ownership assertion.
inline constexpr std::uint32_t kMagic = 0x46473152; // "R1GF"
inline constexpr std::uint32_t kVersion = 1;
inline constexpr std::size_t kResourceCapacity = 16384;
inline constexpr std::size_t kBucketWays = 4;
inline constexpr std::size_t kBucketCount = kResourceCapacity / kBucketWays;
inline constexpr std::size_t kLifecycleCapacity = 1024;
inline constexpr std::size_t kSubmissionCapacity = 8192;
inline constexpr std::size_t kBoundaryCapacity = 64;
inline constexpr std::uint64_t kSealed = std::uint64_t{1} << 63;
inline constexpr std::uint64_t kBusy = 1;
inline constexpr std::uint64_t kVersionMask = ~(kSealed | kBusy);
inline constexpr std::uint64_t kMaximumSequence = kVersionMask >> 1;

// All payload fields are 64-bit words; paired 32-bit fields use low/high halves.
enum EventWord : std::size_t
{
    Sequence, Qpc, Frame, ResetEpoch, MapEpoch, DeviceEpoch,
    Key, Generation, ThreadAndKind, Value1, Value2, Value3, Value4, EventWords
};
enum ResourceWord : std::size_t
{
    ResourceKey, StateGeneration, CreateSequence, CreateQpc,
    LastSequence, LastQpc, LastFrame, LastMapEpoch, LastDeviceEpoch,
    RetirementSequence, LastUseSequence, UseAfterRetirementCount,
    Result, LastThreadAndKind, Status, Resource, ShaderView, TargetView,
    WidthAndHeight, FormatAndMips, ArraySizeAndFlags,
    ViewLayoutAndDimension, Depth, IdentityEpoch, ResourceWords
};
enum ResourceStatus : std::uint64_t
{
    CreationStarted = 1,
    CreationReturned = 2,
    OwnerRetired = 4,
    ReleaseReturned = 8,
};

template <std::size_t Words>
struct AtomicSlot
{
    std::atomic<std::uint64_t> stamp{};
    std::array<std::atomic<std::uint64_t>, Words> words{};
};
using EventSlot = AtomicSlot<EventWords>;
using ResourceSlot = AtomicSlot<ResourceWords>;
using EventData = std::array<std::uint64_t, EventWords>;
using ResourceData = std::array<std::uint64_t, ResourceWords>;

enum class ReadResult { Empty, Complete, Incomplete };

template <std::size_t Words>
ReadResult ReadSlot(const AtomicSlot<Words>& slot,
    std::array<std::uint64_t, Words>& data) noexcept
{
    const auto before = slot.stamp.load(std::memory_order_acquire);
    if (before & kBusy)
        return ReadResult::Incomplete;
    if ((before & kVersionMask) == 0)
        return ReadResult::Empty;
    // Each word is release/acquire, not merely relaxed. Seeing any word from
    // a newer writer orders its Busy publication before the final stamp load;
    // equality therefore cannot validate a mix of two publications, even in a
    // non-crash report while recording continues. No reader claims or waits.
    for (std::size_t i = 0; i < Words; ++i)
        data[i] = slot.words[i].load(std::memory_order_acquire);
    return slot.stamp.load(std::memory_order_acquire) == before
        ? ReadResult::Complete : ReadResult::Incomplete;
}

struct ResourceBucket
{
    // Single-attempt writer reservation, never waited on. A lost lifecycle
    // update invalidates identities in this bucket rather than serving stale
    // generations from before an unobserved same-address creation.
    std::atomic<std::uint64_t> writing{};
    std::atomic<std::uint64_t> identityEpoch{};
};

struct LayoutHeader
{
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t layoutSize;
    std::uint32_t headerSize;
    std::uint32_t storageSize;
    std::uint32_t endianMarker;
    std::uint32_t pointerSize;
    std::uint32_t eventSlotSize;
    std::uint32_t resourceSlotSize;
    std::uint32_t bucketSize;
    std::uint32_t resourceCapacity;
    std::uint32_t lifecycleCapacity;
    std::uint32_t submissionCapacity;
    std::uint32_t bucketWays;
    std::uint32_t bucketsOffset;
    std::uint32_t resourcesOffset;
    std::uint32_t coverageOffset;
    std::uint32_t reservedOffset;
    std::uint64_t historyAddress;
    std::uint32_t historySize;
    std::uint32_t reserved;
    std::uint64_t sealedBit;
    std::uint64_t busyBit;
};

struct HistoryHeader
{
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t headerSize;
    std::uint32_t storageSize;
    std::uint32_t eventSlotSize;
    std::uint32_t lifecycleCapacity;
    std::uint32_t submissionCapacity;
    std::uint32_t boundaryCapacity;
    std::uint32_t lifecycleOffset;
    std::uint32_t submissionsOffset;
    std::uint32_t boundariesOffset;
    std::uint32_t reserved;
};

struct RawHeader
{
    LayoutHeader layout{};
    // initialization: 0 not attempted, 1 registering, 2 registered, 3 failed.
    std::atomic<std::uint64_t> initialization{};
    // Bit 0 registers RawStorage, bit 1 registers HistoryStorage.
    std::atomic<std::uint64_t> registeredRegions{};
    std::atomic<std::uint64_t> coverageDropped{};
    std::atomic<std::uint64_t> qpcFrequency{};
    // High bit closes admission; low bits count admitted, unfinished operations.
    std::atomic<std::uint64_t> writerGate{};
    std::atomic<std::uint64_t> freezeComplete{};
    std::atomic<std::uint64_t> freezeSequence{};
    std::atomic<std::uint64_t> freezeQpc{};
    std::atomic<std::uint64_t> writersAtFreeze{};
    std::atomic<std::uint64_t> sequence{};
    std::atomic<std::uint64_t> generation{};
    std::atomic<std::uint64_t> frame{};
    std::atomic<std::uint64_t> resetEpoch{};
    std::atomic<std::uint64_t> mapEpoch{};
    std::atomic<std::uint64_t> deviceEpoch{};
    std::atomic<std::uint64_t> activity{};
    std::atomic<std::uint64_t> lifecycleTickets{};
    std::atomic<std::uint64_t> submissionTickets{};
    std::atomic<std::uint64_t> lifecyclePublished{};
    std::atomic<std::uint64_t> submissionPublished{};
    std::atomic<std::uint64_t> lifecycleOverwritten{};
    std::atomic<std::uint64_t> submissionOverwritten{};
    std::atomic<std::uint64_t> lifecycleDropped{};
    std::atomic<std::uint64_t> submissionDropped{};
    std::atomic<std::uint64_t> boundaryTickets{};
    std::atomic<std::uint64_t> boundaryPublished{};
    std::atomic<std::uint64_t> boundaryOverwritten{};
    std::atomic<std::uint64_t> boundaryDropped{};
    std::atomic<std::uint64_t> stateCollisions{};
    std::atomic<std::uint64_t> liveEvictions{};
    std::atomic<std::uint64_t> retiredEvictions{};
    std::atomic<std::uint64_t> recreations{};
    std::atomic<std::uint64_t> stateContention{};
    std::atomic<std::uint64_t> stateUpdateMisses{};
    std::atomic<std::uint64_t> identityInvalidations{};
    std::atomic<std::uint64_t> lookupMisses{};
    std::atomic<std::uint64_t> staleGenerations{};
    std::atomic<std::uint64_t> useAfterRetirement{};
    std::atomic<std::uint64_t> rejectedAfterFreeze{};
    std::atomic<std::uint64_t> sequenceOverflow{};
    std::atomic<std::uint64_t> generationOverflow{};
};

struct RawStorage
{
    RawHeader header;
    EventSlot coverage;
    std::array<ResourceBucket, kBucketCount> buckets;
    std::array<ResourceSlot, kResourceCapacity> resources;
};

struct HistoryStorage
{
    HistoryHeader header{};
    std::array<EventSlot, kLifecycleCapacity> lifecycle;
    std::array<EventSlot, kSubmissionCapacity> submissions;
    std::array<EventSlot, kBoundaryCapacity> boundaries;
};

static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(sizeof(std::atomic<std::uint64_t>) == sizeof(std::uint64_t));
static_assert(sizeof(EventSlot) == (EventWords + 1) * sizeof(std::uint64_t));
static_assert(sizeof(ResourceSlot) == (ResourceWords + 1) * sizeof(std::uint64_t));
static_assert(std::is_standard_layout_v<RawStorage>);
static_assert(std::is_trivially_copyable_v<LayoutHeader>);

#if defined(R1DELTA_GRAPHICS_FLIGHT_RECORDER_TESTS)
RawStorage& StorageForTests() noexcept;
HistoryStorage& HistoryForTests() noexcept;
using WriterClaimedHook = void (*)(EventKind);
void SetWriterClaimedHookForTests(WriterClaimedHook hook) noexcept;
#endif
} // namespace r1delta::graphics_flight_recorder::detail
