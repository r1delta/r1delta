#include "graphics_flight_recorder.h"
#include "graphics_flight_recorder_storage.h"
#include "engine/logging/crash_report_memory_capture.h"

#include <Windows.h>
#include <algorithm>
#include <iomanip>
#include <ostream>
#include <vector>

namespace r1delta::graphics_flight_recorder
{
namespace
{
using namespace detail;
constexpr std::size_t kReportedResourceCapacity = 8;
constexpr std::size_t kReportedEventCapacity = 32;

// Zero-initialized process-lifetime storage; Initialize writes only the small
// immutable layout headers before registration, not megabytes of initialized
// data in the DLL image. Neither region is ever unregistered or reclaimed.
RawStorage g_storage{};
HistoryStorage g_history{};
static_assert(sizeof(g_storage) <= logging::crash_report_minidump::kMaximumRegisteredMemoryRegionBytes);
static_assert(sizeof(g_history) <= logging::crash_report_minidump::kMaximumRegisteredMemoryRegionBytes);

#if defined(R1DELTA_GRAPHICS_FLIGHT_RECORDER_TESTS)
std::atomic<WriterClaimedHook> g_writerClaimedHook{};
#endif

std::uint64_t Pack(std::uint32_t low, std::uint32_t high) noexcept
{
    return low | (std::uint64_t{high} << 32);
}

std::uint64_t Timestamp() noexcept
{
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return static_cast<std::uint64_t>(value.QuadPart);
}

std::size_t BucketIndex(std::uintptr_t key) noexcept
{
    // Pool records are aligned, so retain their higher address bits as well.
    std::uint64_t hash = key;
    hash ^= hash >> 33;
    hash *= 0xff51afd7ed558ccdULL;
    hash ^= hash >> 33;
    return static_cast<std::size_t>(hash) & (kBucketCount - 1);
}

class Operation
{
public:
    Operation() noexcept
    {
        auto& header = g_storage.header;
        if ((header.writerGate.load(std::memory_order_acquire) & kSealed) == 0)
        {
            const auto previous = header.writerGate.fetch_add(1, std::memory_order_acq_rel);
            if ((previous & kSealed) == 0)
            {
                admitted_ = true;
                return;
            }
            header.writerGate.fetch_sub(1, std::memory_order_release);
        }
        header.rejectedAfterFreeze.fetch_add(1, std::memory_order_relaxed);
    }
    ~Operation()
    {
        if (admitted_)
            g_storage.header.writerGate.fetch_sub(1, std::memory_order_release);
    }
    explicit operator bool() const noexcept { return admitted_; }
    Operation(const Operation&) = delete;
    Operation& operator=(const Operation&) = delete;
private:
    bool admitted_ = false;
};

std::uint64_t NextIdentity(std::atomic<std::uint64_t>& counter,
    std::atomic<std::uint64_t>& overflow) noexcept
{
    // Stop permanently before the stamp's reserved bits, rather than wrapping
    // into an apparently valid old identity. This also bounds ring tickets.
    if (counter.load(std::memory_order_relaxed) < kMaximumSequence)
    {
        const auto value = counter.fetch_add(1, std::memory_order_relaxed) + 1;
        if (value <= kMaximumSequence)
            return value;
    }
    overflow.fetch_add(1, std::memory_order_relaxed);
    return 0;
}

EventData MakeEvent(EventKind kind, std::uintptr_t key, std::uint64_t generation,
    std::uint64_t value1, std::uint64_t value2,
    std::uint64_t value3, std::uint64_t value4) noexcept
{
    auto& header = g_storage.header;
    EventData data{};
    data[Sequence] = NextIdentity(header.sequence, header.sequenceOverflow);
    if (data[Sequence] == 0)
        return data;
    data[Qpc] = Timestamp();
    data[Frame] = header.frame.load(std::memory_order_acquire);
    data[ResetEpoch] = header.resetEpoch.load(std::memory_order_acquire);
    data[MapEpoch] = header.mapEpoch.load(std::memory_order_acquire);
    data[DeviceEpoch] = header.deviceEpoch.load(std::memory_order_acquire);
    data[Key] = key;
    data[Generation] = generation;
    data[ThreadAndKind] = Pack(GetCurrentThreadId(), static_cast<std::uint32_t>(kind));
    data[Value1] = value1;
    data[Value2] = value2;
    data[Value3] = value3;
    data[Value4] = value4;
    if (kind != EventKind::RecorderStarted)
        header.activity.fetch_add(1, std::memory_order_relaxed);
    return data;
}

bool IsLifecycle(EventKind kind) noexcept
{
    switch (kind)
    {
    case EventKind::RecorderStarted:
    case EventKind::TextureCreateBegin:
    case EventKind::TextureCreateEnd:
    case EventKind::TextureReleaseBegin:
    case EventKind::TextureReleaseEnd:
    case EventKind::VtfLoadBegin:
    case EventKind::VtfLoadEnd:
    case EventKind::LevelShutdownBegin:
    case EventKind::LevelShutdownEnd:
    case EventKind::DeviceShutdownBegin:
    case EventKind::DeviceShutdownEnd:
    case EventKind::DeviceDisconnectBegin:
    case EventKind::DeviceDisconnectEnd:
    case EventKind::DeviceReady:
        return true;
    default:
        return false;
    }
}

bool IsResourceUse(EventKind kind) noexcept
{
    switch (kind)
    {
    case EventKind::TextureUploadQueued:
    case EventKind::TextureUploadBegin:
    case EventKind::TextureUploadEnd:
    case EventKind::ShaderResourcePending:
    case EventKind::ShaderResourceSubmit:
    case EventKind::TxaaQueued:
    case EventKind::TxaaExecute:
        return true;
    default:
        return false;
    }
}

template <std::size_t Words>
bool ClaimSlot(AtomicSlot<Words>& slot, std::uint64_t sequence,
    std::uint64_t& previous) noexcept
{
    previous = slot.stamp.load(std::memory_order_acquire);
    if ((previous & (kBusy | kSealed)) != 0 || (previous >> 1) >= sequence)
        return false;
    return slot.stamp.compare_exchange_strong(previous, (sequence << 1) | kBusy,
        std::memory_order_acq_rel, std::memory_order_acquire);
}

template <std::size_t Words>
bool PublishSlot(AtomicSlot<Words>& slot, std::uint64_t sequence,
    const std::array<std::uint64_t, Words>& data) noexcept
{
    for (std::size_t i = 0; i < Words; ++i)
        slot.words[i].store(data[i], std::memory_order_release);
    auto expected = (sequence << 1) | kBusy;
    // Freeze sets Sealed without waiting. A writer interrupted anywhere above
    // cannot undo that seal or turn its partial raw bytes into a valid record.
    return slot.stamp.compare_exchange_strong(expected, sequence << 1,
        std::memory_order_release, std::memory_order_relaxed);
}

void PublishIntoRing(const EventData& data, EventSlot* slots, std::size_t capacity,
    std::atomic<std::uint64_t>& tickets, std::atomic<std::uint64_t>& published,
    std::atomic<std::uint64_t>& overwritten, std::atomic<std::uint64_t>& dropped) noexcept
{
    if (data[Sequence] == 0)
        return;
    const auto ticket = tickets.fetch_add(1, std::memory_order_relaxed);
    EventSlot& slot = slots[ticket % capacity];
    std::uint64_t previous = 0;
    if (!ClaimSlot(slot, data[Sequence], previous))
    {
        dropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if ((previous & kVersionMask) != 0)
        overwritten.fetch_add(1, std::memory_order_relaxed);
#if defined(R1DELTA_GRAPHICS_FLIGHT_RECORDER_TESTS)
    if (const auto hook = g_writerClaimedHook.load(std::memory_order_acquire))
        hook(static_cast<EventKind>(data[ThreadAndKind] >> 32));
#endif
    if (PublishSlot(slot, data[Sequence], data))
        published.fetch_add(1, std::memory_order_relaxed);
    else
        dropped.fetch_add(1, std::memory_order_relaxed);
}

void PublishEvent(const EventData& data) noexcept
{
    auto& header = g_storage.header;
    const auto kind = static_cast<EventKind>(data[ThreadAndKind] >> 32);
    const bool lifecycle = IsLifecycle(kind);
    PublishIntoRing(data,
        lifecycle ? g_history.lifecycle.data() : g_history.submissions.data(),
        lifecycle ? kLifecycleCapacity : kSubmissionCapacity,
        lifecycle ? header.lifecycleTickets : header.submissionTickets,
        lifecycle ? header.lifecyclePublished : header.submissionPublished,
        lifecycle ? header.lifecycleOverwritten : header.submissionOverwritten,
        lifecycle ? header.lifecycleDropped : header.submissionDropped);
    switch (kind)
    {
    case EventKind::LevelShutdownBegin:
    case EventKind::LevelShutdownEnd:
    case EventKind::DeviceShutdownBegin:
    case EventKind::DeviceShutdownEnd:
    case EventKind::DeviceDisconnectBegin:
    case EventKind::DeviceDisconnectEnd:
    case EventKind::DeviceReady:
        PublishIntoRing(data, g_history.boundaries.data(), kBoundaryCapacity,
            header.boundaryTickets, header.boundaryPublished,
            header.boundaryOverwritten, header.boundaryDropped);
        break;
    default:
        break;
    }
}

void StoreDescription(ResourceData& data, const ResourceDescription& description) noexcept
{
    data[Resource] = description.resource;
    data[ShaderView] = description.shaderView;
    data[TargetView] = description.targetView;
    data[WidthAndHeight] = Pack(description.width, description.height);
    data[FormatAndMips] = Pack(description.format, description.mipCount);
    data[ArraySizeAndFlags] = Pack(description.arraySize, description.flags);
    data[ViewLayoutAndDimension] = Pack(description.viewLayout, description.dimension);
    data[Depth] = description.depth;
}

void InvalidateBucket(ResourceBucket& bucket) noexcept
{
    bucket.identityEpoch.fetch_add(1, std::memory_order_acq_rel);
    g_storage.header.identityInvalidations.fetch_add(1, std::memory_order_relaxed);
}

void UpdateResource(const EventData& event,
    const ResourceDescription* description = nullptr) noexcept
{
    auto& header = g_storage.header;
    const auto kind = static_cast<EventKind>(event[ThreadAndKind] >> 32);
    const bool creating = kind == EventKind::TextureCreateBegin;
    const bool lifecycle = IsLifecycle(kind);
    if (event[Sequence] == 0)
        return;
    if (event[Key] == 0 || event[Generation] == 0)
    {
        header.stateUpdateMisses.fetch_add(1, std::memory_order_relaxed);
        if (event[Key] != 0 && lifecycle)
            InvalidateBucket(g_storage.buckets[BucketIndex(static_cast<std::uintptr_t>(event[Key]))]);
        return;
    }

    const auto bucketIndex = BucketIndex(static_cast<std::uintptr_t>(event[Key]));
    auto& bucket = g_storage.buckets[bucketIndex];
    std::uint64_t expected = 0;
    if (!bucket.writing.compare_exchange_strong(expected, 1,
        std::memory_order_acquire, std::memory_order_relaxed))
    {
        header.stateContention.fetch_add(1, std::memory_order_relaxed);
        header.stateUpdateMisses.fetch_add(1, std::memory_order_relaxed);
        if (lifecycle)
            InvalidateBucket(bucket);
        return;
    }

    const auto epoch = bucket.identityEpoch.load(std::memory_order_acquire);
    ResourceSlot* selected = nullptr;
    ResourceSlot* victim = nullptr;
    unsigned victimRank = 4;
    std::uint64_t victimAge = UINT64_MAX;
    for (std::size_t way = 0; way < kBucketWays; ++way)
    {
        auto& slot = g_storage.resources[bucketIndex * kBucketWays + way];
        const auto stamp = slot.stamp.load(std::memory_order_acquire);
        if ((stamp & (kBusy | kSealed)) != 0)
            continue;
        const auto key = slot.words[ResourceKey].load(std::memory_order_acquire);
        if (key == event[Key] && (stamp & kVersionMask) != 0)
        {
            selected = &slot;
            break;
        }
        if (!creating)
            continue;
        if (key != 0)
            header.stateCollisions.fetch_add(1, std::memory_order_relaxed);
        const unsigned rank = (stamp & kVersionMask) == 0 ? 0
            : slot.words[IdentityEpoch].load(std::memory_order_acquire) != epoch ? 1
            : (slot.words[Status].load(std::memory_order_acquire) & OwnerRetired) != 0 ? 2 : 3;
        const auto age = slot.words[LastSequence].load(std::memory_order_acquire);
        if (rank < victimRank || (rank == victimRank && age < victimAge))
        {
            victim = &slot;
            victimRank = rank;
            victimAge = age;
        }
    }

    ResourceData data{};
    bool valid = false;
    bool staleIdentity = false;
    std::atomic<std::uint64_t>* evictionCounter = nullptr;
    if (creating)
    {
        if (selected && selected->words[StateGeneration].load(std::memory_order_acquire) >= event[Generation])
        {
            staleIdentity = true;
            header.staleGenerations.fetch_add(1, std::memory_order_relaxed);
        }
        else
        {
            if (selected)
                header.recreations.fetch_add(1, std::memory_order_relaxed);
            else
            {
                selected = victim;
                if (selected && (selected->stamp.load(std::memory_order_acquire) & kVersionMask) != 0)
                {
                    evictionCounter = (selected->words[Status].load(std::memory_order_acquire) & OwnerRetired)
                        ? &header.retiredEvictions : &header.liveEvictions;
                }
            }
            if (selected)
            {
                data[ResourceKey] = event[Key];
                data[StateGeneration] = event[Generation];
                data[CreateSequence] = event[Sequence];
                data[CreateQpc] = event[Qpc];
                data[Status] = CreationStarted;
                data[IdentityEpoch] = epoch;
                valid = true;
            }
        }
    }
    else if (selected && ReadSlot(*selected, data) == ReadResult::Complete)
    {
        if (data[StateGeneration] != event[Generation])
        {
            staleIdentity = true;
            header.staleGenerations.fetch_add(1, std::memory_order_relaxed);
        }
        else if (data[IdentityEpoch] == epoch)
            valid = true;
    }

    if (valid)
    {
        data[LastSequence] = event[Sequence];
        data[LastQpc] = event[Qpc];
        data[LastFrame] = event[Frame];
        data[LastMapEpoch] = event[MapEpoch];
        data[LastDeviceEpoch] = event[DeviceEpoch];
        data[LastThreadAndKind] = event[ThreadAndKind];
        if (description)
            StoreDescription(data, *description);
        if (kind == EventKind::TextureCreateEnd)
        {
            data[Status] |= CreationReturned;
            data[Result] = event[Value4];
        }
        if (kind == EventKind::TextureReleaseBegin || kind == EventKind::TextureReleaseEnd)
        {
            data[Status] |= OwnerRetired;
            if (data[RetirementSequence] == 0)
                data[RetirementSequence] = event[Sequence];
            if (kind == EventKind::TextureReleaseEnd)
                data[Status] |= ReleaseReturned;
        }
        if (IsResourceUse(kind))
        {
            data[LastUseSequence] = event[Sequence];
            if (data[Status] & OwnerRetired)
            {
                ++data[UseAfterRetirementCount];
                header.useAfterRetirement.fetch_add(1, std::memory_order_relaxed);
            }
        }
        std::uint64_t previous = 0;
        valid = ClaimSlot(*selected, event[Sequence], previous);
        if (valid)
        {
            if (evictionCounter)
                evictionCounter->fetch_add(1, std::memory_order_relaxed);
            valid = PublishSlot(*selected, event[Sequence], data);
        }
    }
    if (!valid)
    {
        header.stateUpdateMisses.fetch_add(1, std::memory_order_relaxed);
        // A stale generation must not invalidate the newer generation. Other
        // lost lifecycle updates make this bucket's state explicitly unknown.
        if (lifecycle && !staleIdentity)
        {
            InvalidateBucket(bucket);
        }
    }
    bucket.writing.store(0, std::memory_order_release);
}

void RecordAdmitted(EventKind kind, std::uintptr_t key, std::uint64_t generation,
    std::uint64_t value1, std::uint64_t value2,
    std::uint64_t value3, std::uint64_t value4) noexcept
{
    const auto event = MakeEvent(kind, key, generation, value1, value2, value3, value4);
    if (kind == EventKind::RecorderStarted && event[Sequence] != 0)
    {
        std::uint64_t previous = 0;
        if (!ClaimSlot(g_storage.coverage, event[Sequence], previous)
            || !PublishSlot(g_storage.coverage, event[Sequence], event))
        {
            g_storage.header.coverageDropped.fetch_add(1, std::memory_order_relaxed);
        }
    }
    if (key != 0 && IsResourceUse(kind))
        UpdateResource(event);
    PublishEvent(event);
}

const char* EventName(EventKind kind) noexcept
{
    switch (kind)
    {
#define R1D_GRAPHICS_EVENT_NAME(name) case EventKind::name: return #name
    R1D_GRAPHICS_EVENT_NAME(RecorderStarted);
    R1D_GRAPHICS_EVENT_NAME(TextureCreateBegin);
    R1D_GRAPHICS_EVENT_NAME(TextureCreateEnd);
    R1D_GRAPHICS_EVENT_NAME(TextureReleaseBegin);
    R1D_GRAPHICS_EVENT_NAME(TextureReleaseEnd);
    R1D_GRAPHICS_EVENT_NAME(TextureUploadQueued);
    R1D_GRAPHICS_EVENT_NAME(TextureUploadBegin);
    R1D_GRAPHICS_EVENT_NAME(TextureUploadEnd);
    R1D_GRAPHICS_EVENT_NAME(ShaderResourcePending);
    R1D_GRAPHICS_EVENT_NAME(ShaderResourceSubmit);
    R1D_GRAPHICS_EVENT_NAME(VtfLoadBegin);
    R1D_GRAPHICS_EVENT_NAME(VtfLoadEnd);
    R1D_GRAPHICS_EVENT_NAME(TxaaQueued);
    R1D_GRAPHICS_EVENT_NAME(TxaaExecute);
    R1D_GRAPHICS_EVENT_NAME(PresentBegin);
    R1D_GRAPHICS_EVENT_NAME(PresentEnd);
    R1D_GRAPHICS_EVENT_NAME(LevelShutdownBegin);
    R1D_GRAPHICS_EVENT_NAME(LevelShutdownEnd);
    R1D_GRAPHICS_EVENT_NAME(DeviceShutdownBegin);
    R1D_GRAPHICS_EVENT_NAME(DeviceShutdownEnd);
    R1D_GRAPHICS_EVENT_NAME(DeviceDisconnectBegin);
    R1D_GRAPHICS_EVENT_NAME(DeviceDisconnectEnd);
    R1D_GRAPHICS_EVENT_NAME(DeviceReady);
#undef R1D_GRAPHICS_EVENT_NAME
    default: return "Unknown";
    }
}

struct RingSummary
{
    std::size_t complete = 0;
    std::size_t incomplete = 0;
    std::uint64_t firstSequence = UINT64_MAX;
    std::uint64_t lastSequence = 0;
    std::uint64_t firstQpc = UINT64_MAX;
    std::uint64_t lastQpc = 0;
    std::uint64_t firstFrame = UINT64_MAX;
    std::uint64_t lastFrame = 0;
};

template <std::size_t Capacity>
RingSummary SnapshotEvents(const std::array<EventSlot, Capacity>& slots,
    std::vector<EventData>& events)
{
    RingSummary summary;
    const auto tailStart = events.size();
    const auto newer = [](const EventData& a, const EventData& b) {
        return a[Sequence] > b[Sequence];
    };
    for (const auto& slot : slots)
    {
        EventData data{};
        const auto result = ReadSlot(slot, data);
        if (result == ReadResult::Incomplete)
            ++summary.incomplete;
        if (result != ReadResult::Complete)
            continue;
        ++summary.complete;
        summary.firstSequence = (std::min)(summary.firstSequence, data[Sequence]);
        summary.lastSequence = (std::max)(summary.lastSequence, data[Sequence]);
        summary.firstQpc = (std::min)(summary.firstQpc, data[Qpc]);
        summary.lastQpc = (std::max)(summary.lastQpc, data[Qpc]);
        summary.firstFrame = (std::min)(summary.firstFrame, data[Frame]);
        summary.lastFrame = (std::max)(summary.lastFrame, data[Frame]);
        // Keep only this ring's newest text-tail candidates. The full ring
        // remains untouched in registered minidump storage.
        if (events.size() - tailStart < kReportedEventCapacity) {
            events.push_back(data);
            std::push_heap(events.begin() + tailStart, events.end(), newer);
        }
        else if (data[Sequence] > events[tailStart][Sequence]) {
            std::pop_heap(events.begin() + tailStart, events.end(), newer);
            events.back() = data;
            std::push_heap(events.begin() + tailStart, events.end(), newer);
        }
    }
    return summary;
}

void WriteRingSummary(std::ostream& output, const char* name, std::size_t capacity,
    const RingSummary& summary, std::uint64_t attempted,
    std::uint64_t published, std::uint64_t overwritten, std::uint64_t dropped)
{
    output << name << ": capacity=" << capacity << " attempted=" << attempted
        << " published=" << published << " overwritten=" << overwritten
        << " dropped=" << dropped << " retained=" << summary.complete
        << " incomplete-or-raced=" << summary.incomplete;
    if (summary.complete)
    {
        output << " sequence=" << summary.firstSequence << ".." << summary.lastSequence
            << " qpc=" << summary.firstQpc << ".." << summary.lastQpc
            << " frame=" << summary.firstFrame << ".." << summary.lastFrame;
    }
    output << '\n';
}
} // namespace

bool Initialize() noexcept
{
    auto& header = g_storage.header;
    std::uint64_t expected = 0;
    if (!header.initialization.compare_exchange_strong(expected, 1,
        std::memory_order_acq_rel, std::memory_order_acquire))
    {
        return expected == 2;
    }
    header.layout = {
        kMagic, kVersion, sizeof(LayoutHeader), sizeof(RawHeader), sizeof(RawStorage),
        0x01020304, sizeof(std::uintptr_t), sizeof(EventSlot), sizeof(ResourceSlot),
        sizeof(ResourceBucket), kResourceCapacity, kLifecycleCapacity, kSubmissionCapacity,
        kBucketWays, offsetof(RawStorage, buckets), offsetof(RawStorage, resources),
        offsetof(RawStorage, coverage), 0,
        reinterpret_cast<std::uintptr_t>(&g_history), sizeof(HistoryStorage), 0, kSealed, kBusy
    };
    g_history.header = {
        kMagic, kVersion, sizeof(HistoryHeader), sizeof(HistoryStorage), sizeof(EventSlot),
        kLifecycleCapacity, kSubmissionCapacity, kBoundaryCapacity, offsetof(HistoryStorage, lifecycle),
        offsetof(HistoryStorage, submissions), offsetof(HistoryStorage, boundaries), 0
    };
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    header.qpcFrequency.store(static_cast<std::uint64_t>(frequency.QuadPart), std::memory_order_release);
    using namespace logging::crash_report_minidump;
    std::uint64_t registered = RegisterCrashMemoryRegion(&g_storage, sizeof(g_storage),
        CrashMemoryRegionPriority::Critical) ? 1 : 0;
    if (RegisterCrashMemoryRegion(&g_history, sizeof(g_history), CrashMemoryRegionPriority::Critical))
        registered |= 2;
    header.registeredRegions.store(registered, std::memory_order_release);
    header.initialization.store(registered == 3 ? 2 : 3, std::memory_order_release);
    // RecorderStarted is reserved for the installer's native coverage masks.
    return registered == 3;
}

void Record(EventKind kind, std::uintptr_t key, std::uint64_t generation,
    std::uint64_t value1, std::uint64_t value2,
    std::uint64_t value3, std::uint64_t value4) noexcept
{
    const Operation operation;
    if (operation)
        RecordAdmitted(kind, key, generation, value1, value2, value3, value4);
}

std::uint64_t ResourceCreateBegin(std::uintptr_t key,
    const ResourceDescription& description, std::uintptr_t callerRva) noexcept
{
    const Operation operation;
    if (!operation)
        return 0;
    auto& header = g_storage.header;
    const auto generation = NextIdentity(header.generation, header.generationOverflow);
    const auto event = MakeEvent(EventKind::TextureCreateBegin, key, generation, callerRva,
        Pack(description.width, description.height), Pack(description.format, description.mipCount),
        Pack(description.arraySize, description.flags));
    UpdateResource(event, &description);
    PublishEvent(event);
    return generation;
}

void ResourceCreateEnd(std::uintptr_t key, std::uint64_t generation,
    const ResourceDescription& description, std::int64_t result) noexcept
{
    const Operation operation;
    if (!operation)
        return;
    const auto event = MakeEvent(EventKind::TextureCreateEnd, key, generation,
        description.resource, description.shaderView, description.targetView,
        static_cast<std::uint64_t>(result));
    UpdateResource(event, &description);
    PublishEvent(event);
}

std::uint64_t ResourceGeneration(std::uintptr_t key) noexcept
{
    if (key != 0 && g_storage.header.sequenceOverflow.load(std::memory_order_acquire) == 0
        && g_storage.header.generationOverflow.load(std::memory_order_acquire) == 0)
    {
        const auto index = BucketIndex(key);
        const auto& bucket = g_storage.buckets[index];
        const auto epoch = bucket.identityEpoch.load(std::memory_order_acquire);
        for (std::size_t way = 0; way < kBucketWays; ++way)
        {
            const auto& slot = g_storage.resources[index * kBucketWays + way];
            const auto before = slot.stamp.load(std::memory_order_acquire);
            if ((before & kBusy) != 0 || (before & kVersionMask) == 0)
                continue;
            const auto storedKey = slot.words[ResourceKey].load(std::memory_order_acquire);
            const auto generation = slot.words[StateGeneration].load(std::memory_order_acquire);
            const auto storedEpoch = slot.words[IdentityEpoch].load(std::memory_order_acquire);
            if (storedKey == key && storedEpoch == epoch
                && slot.stamp.load(std::memory_order_acquire) == before
                && bucket.identityEpoch.load(std::memory_order_acquire) == epoch)
            {
                return generation;
            }
        }
    }
    g_storage.header.lookupMisses.fetch_add(1, std::memory_order_relaxed);
    return 0;
}

void ResourceReleaseBegin(std::uintptr_t key, std::uint64_t generation,
    std::uintptr_t callerRva) noexcept
{
    const Operation operation;
    if (!operation)
        return;
    const auto event = MakeEvent(EventKind::TextureReleaseBegin, key, generation, callerRva, 0, 0, 0);
    UpdateResource(event);
    PublishEvent(event);
}

void ResourceReleaseEnd(std::uintptr_t key, std::uint64_t generation) noexcept
{
    const Operation operation;
    if (!operation)
        return;
    const auto event = MakeEvent(EventKind::TextureReleaseEnd, key, generation, 0, 0, 0, 0);
    UpdateResource(event);
    PublishEvent(event);
}

void FrameBoundary(bool begin) noexcept
{
    const Operation operation;
    if (!operation)
        return;
    if (begin)
        g_storage.header.frame.fetch_add(1, std::memory_order_acq_rel);
    RecordAdmitted(begin ? EventKind::PresentBegin : EventKind::PresentEnd, 0, 0, 0, 0, 0, 0);
}

void ResetBoundary(EventKind kind, std::uintptr_t object) noexcept
{
    const Operation operation;
    if (!operation)
        return;
    auto& header = g_storage.header;
    if (kind == EventKind::LevelShutdownBegin)
    {
        header.mapEpoch.fetch_add(1, std::memory_order_acq_rel);
        header.resetEpoch.fetch_add(1, std::memory_order_acq_rel);
    }
    if (kind == EventKind::DeviceShutdownBegin || kind == EventKind::DeviceDisconnectBegin
        || kind == EventKind::DeviceReady)
    {
        header.deviceEpoch.fetch_add(1, std::memory_order_acq_rel);
        header.resetEpoch.fetch_add(1, std::memory_order_acq_rel);
    }
    RecordAdmitted(kind, object, 0, 0, 0, 0, 0);
}

void Freeze() noexcept
{
    auto& header = g_storage.header;
    const auto previous = header.writerGate.fetch_or(kSealed, std::memory_order_acq_rel);
    if (previous & kSealed)
        return;
    header.writersAtFreeze.store(previous, std::memory_order_release);
    header.freezeQpc.store(Timestamp(), std::memory_order_release);
    // Finite scans, no retry loops, waits, native/COM reads or allocation. In a
    // sealed busy slot even a resumed writer can only change invalid payload.
    g_storage.coverage.stamp.fetch_or(kSealed, std::memory_order_acq_rel);
    for (auto& slot : g_history.lifecycle)
        slot.stamp.fetch_or(kSealed, std::memory_order_acq_rel);
    for (auto& slot : g_history.submissions)
        slot.stamp.fetch_or(kSealed, std::memory_order_acq_rel);
    for (auto& slot : g_history.boundaries)
        slot.stamp.fetch_or(kSealed, std::memory_order_acq_rel);
    for (auto& slot : g_storage.resources)
        slot.stamp.fetch_or(kSealed, std::memory_order_acq_rel);
    header.freezeSequence.store(header.sequence.load(std::memory_order_acquire), std::memory_order_release);
    header.freezeComplete.store(1, std::memory_order_release);
}

void WriteReport(std::ostream& output)
{
    const auto oldFlags = output.flags();
    output << std::dec << "=== Graphics Flight Recorder ===\n";
    const auto& header = g_storage.header;
    const auto initialized = header.initialization.load(std::memory_order_acquire);
    const auto registrations = header.registeredRegions.load(std::memory_order_acquire);
    const auto gate = header.writerGate.load(std::memory_order_acquire);
    output << "Status: " << (header.activity.load(std::memory_order_acquire) ? "active" : "not active (no graphics events)")
        << "; snapshot=" << ((gate & kSealed) ? "frozen" : "live/non-atomic across slots")
        << "; freeze-complete=" << header.freezeComplete.load(std::memory_order_acquire) << '\n';
    output << "Raw layout: magic=0x" << std::hex << kMagic << std::dec << " version=" << kVersion
        << " resource-region=" << sizeof(g_storage) << " history-region=" << sizeof(g_history)
        << " event-slot=" << sizeof(EventSlot) << " resource-slot=" << sizeof(ResourceSlot) << '\n';
    output << "Minidump registration: state=" << initialized
        << " (0=not-attempted 1=in-progress 2=complete 3=FAILED) resources="
        << ((registrations & 1) ? "registered" : "NOT REGISTERED")
        << " histories=" << ((registrations & 2) ? "registered" : "NOT REGISTERED") << '\n';
    output << "Clock: QPC-frequency=" << header.qpcFrequency.load(std::memory_order_acquire)
        << " frame=" << header.frame.load(std::memory_order_acquire)
        << " reset=" << header.resetEpoch.load(std::memory_order_acquire)
        << " map=" << header.mapEpoch.load(std::memory_order_acquire)
        << " device=" << header.deviceEpoch.load(std::memory_order_acquire)
        << " freeze-sequence=" << header.freezeSequence.load(std::memory_order_acquire)
        << " freeze-qpc=" << header.freezeQpc.load(std::memory_order_acquire) << '\n';
    output << "Writers: at-freeze=" << header.writersAtFreeze.load(std::memory_order_acquire)
        << " unfinished-now=" << (gate & ~kSealed)
        << " rejected-after-freeze=" << header.rejectedAfterFreeze.load(std::memory_order_acquire)
        << " sequence-overflow=" << header.sequenceOverflow.load(std::memory_order_acquire)
        << " generation-overflow=" << header.generationOverflow.load(std::memory_order_acquire) << '\n';
    EventData coverage{};
    const auto coverageRead = ReadSlot(g_storage.coverage, coverage);
    output << "Native coverage: publication="
        << (coverageRead == ReadResult::Complete ? "complete" : coverageRead == ReadResult::Empty ? "absent" : "INCOMPLETE")
        << " dropped=" << header.coverageDropped.load(std::memory_order_acquire);
    if (coverageRead == ReadResult::Complete)
    {
        output << " installed=0x" << std::hex << coverage[Value1]
            << " expected=0x" << coverage[Value2]
            << " missing=0x" << (coverage[Value2] & ~coverage[Value1])
            << " module=0x" << coverage[Value3]
            << " timestamp-and-size=0x" << coverage[Value4] << std::dec;
    }
    output << '\n';

    std::vector<EventData> events;
    events.reserve(kReportedEventCapacity * 3);
    const auto lifecycle = SnapshotEvents(g_history.lifecycle, events);
    const auto submissions = SnapshotEvents(g_history.submissions, events);
    const auto boundaries = SnapshotEvents(g_history.boundaries, events);
    WriteRingSummary(output, "Lifecycle", kLifecycleCapacity, lifecycle,
        header.lifecycleTickets.load(std::memory_order_acquire), header.lifecyclePublished.load(std::memory_order_acquire),
        header.lifecycleOverwritten.load(std::memory_order_acquire), header.lifecycleDropped.load(std::memory_order_acquire));
    WriteRingSummary(output, "Submission", kSubmissionCapacity, submissions,
        header.submissionTickets.load(std::memory_order_acquire), header.submissionPublished.load(std::memory_order_acquire),
        header.submissionOverwritten.load(std::memory_order_acquire), header.submissionDropped.load(std::memory_order_acquire));
    WriteRingSummary(output, "Map/device boundaries", kBoundaryCapacity, boundaries,
        header.boundaryTickets.load(std::memory_order_acquire), header.boundaryPublished.load(std::memory_order_acquire),
        header.boundaryOverwritten.load(std::memory_order_acquire), header.boundaryDropped.load(std::memory_order_acquire));
    output << "Retention is event-bounded, not a guaranteed number of frames. Missing identities stay generation=0.\n"
        << "Retirement means owner release observed, NOT final COM destruction; subsequent use is correlation, not proof of a bug.\n"
        << "CreateBegin values=callerRva,width|height<<32,format|mips<<32,arraySize|flags<<32; "
        << "CreateEnd values=resource,shaderView,targetView,native-result. Other payloads are hook-specific scalars.\n";

    std::vector<ResourceData> resources;
    resources.reserve(kReportedResourceCapacity);
    std::size_t incomplete = 0, invalidated = 0, total = 0, retired = 0;
    for (std::size_t i = 0; i < kResourceCapacity; ++i)
    {
        const auto& bucket = g_storage.buckets[i / kBucketWays];
        const auto epoch = bucket.identityEpoch.load(std::memory_order_acquire);
        ResourceData data{};
        const auto result = ReadSlot(g_storage.resources[i], data);
        if (result == ReadResult::Incomplete)
            ++incomplete;
        if (result != ReadResult::Complete)
            continue;
        if (data[IdentityEpoch] != epoch || bucket.identityEpoch.load(std::memory_order_acquire) != epoch)
        {
            ++invalidated;
            continue;
        }
        ++total;
        if (data[Status] & OwnerRetired)
            ++retired;
        if (resources.size() < kReportedResourceCapacity)
            resources.push_back(data);
        else
        {
            auto oldest = std::min_element(resources.begin(), resources.end(),
                [](const auto& a, const auto& b) { return a[LastSequence] < b[LastSequence]; });
            if ((*oldest)[LastSequence] < data[LastSequence])
                *oldest = data;
        }
    }
    output << "Resources: capacity=" << kResourceCapacity << " bucket-ways=" << kBucketWays
        << " retained=" << total << " owner-retired=" << retired
        << " incomplete-or-raced=" << incomplete << " invalidated=" << invalidated
        << " selected-newest=" << resources.size() << " omitted-from-text=" << total - resources.size() << '\n';
    output << "State loss: collisions=" << header.stateCollisions.load(std::memory_order_acquire)
        << " live-evictions=" << header.liveEvictions.load(std::memory_order_acquire)
        << " retired-evictions=" << header.retiredEvictions.load(std::memory_order_acquire)
        << " same-address-recreations=" << header.recreations.load(std::memory_order_acquire)
        << " contention=" << header.stateContention.load(std::memory_order_acquire)
        << " update-misses=" << header.stateUpdateMisses.load(std::memory_order_acquire)
        << " identity-invalidations=" << header.identityInvalidations.load(std::memory_order_acquire)
        << " lookup-misses=" << header.lookupMisses.load(std::memory_order_acquire)
        << " stale-generations=" << header.staleGenerations.load(std::memory_order_acquire)
        << " observed-uses-after-owner-retirement=" << header.useAfterRetirement.load(std::memory_order_acquire) << '\n';
    std::sort(resources.begin(), resources.end(),
        [](const auto& a, const auto& b) { return a[LastSequence] < b[LastSequence]; });
    for (const auto& data : resources)
    {
        output << "resource key=0x" << std::hex << data[ResourceKey] << std::dec
            << " generation=" << data[StateGeneration] << " status=" << data[Status]
            << " create=" << data[CreateSequence] << " last=" << data[LastSequence]
            << " qpc=" << data[CreateQpc] << ".." << data[LastQpc]
            << " frame=" << data[LastFrame] << " map=" << data[LastMapEpoch] << " device=" << data[LastDeviceEpoch]
            << " retirement=" << data[RetirementSequence] << " last-use=" << data[LastUseSequence]
            << " uses-after-retirement=" << data[UseAfterRetirementCount]
            << " thread=" << static_cast<std::uint32_t>(data[LastThreadAndKind])
            << " event=" << EventName(static_cast<EventKind>(data[LastThreadAndKind] >> 32))
            << " result=" << static_cast<std::int64_t>(data[Result])
            << " dimensions=" << static_cast<std::uint32_t>(data[WidthAndHeight]) << 'x'
            << (data[WidthAndHeight] >> 32) << 'x' << data[Depth]
            << " dimension=" << (data[ViewLayoutAndDimension] >> 32)
            << " format=" << static_cast<std::uint32_t>(data[FormatAndMips])
            << " mips=" << (data[FormatAndMips] >> 32)
            << " array=" << static_cast<std::uint32_t>(data[ArraySizeAndFlags])
            << " flags=0x" << std::hex << (data[ArraySizeAndFlags] >> 32)
            << " view-layout=0x" << static_cast<std::uint32_t>(data[ViewLayoutAndDimension])
            << " resource=0x" << data[Resource] << " shader-view-or-array=0x" << data[ShaderView]
            << " target-view-or-array=0x" << data[TargetView] << std::dec << '\n';
    }
    output << "Resource status bits: 1=creation-started 2=creation-returned 4=owner-retired 8=release-returned; "
        << "view-layout bits: 1=shader-array 2=target-array 4=depth-stencil. All addresses borrowed.\n";
    std::sort(events.begin(), events.end(),
        [](const auto& a, const auto& b) { return a[Sequence] < b[Sequence]; });
    events.erase(std::unique(events.begin(), events.end(),
        [](const auto& a, const auto& b) { return a[Sequence] == b[Sequence]; }), events.end());
    if (events.size() > kReportedEventCapacity)
        events.erase(events.begin(), events.end() - kReportedEventCapacity);
    output << "Text event tail: newest=" << events.size() << " limit=" << kReportedEventCapacity
        << "; sequence reservation order, not GPU completion order.\n"
        << "Full resource states and all retained history rings are in the compressed minidump; "
        << "only the readable text is abbreviated.\n";
    for (const auto& data : events)
    {
        output << "event seq=" << data[Sequence] << " qpc=" << data[Qpc]
            << " thread=" << static_cast<std::uint32_t>(data[ThreadAndKind])
            << " frame=" << data[Frame] << " reset=" << data[ResetEpoch]
            << " map=" << data[MapEpoch] << " device=" << data[DeviceEpoch]
            << " kind=" << EventName(static_cast<EventKind>(data[ThreadAndKind] >> 32))
            << " key=0x" << std::hex << data[Key] << std::dec << " generation=" << data[Generation]
            << " values=0x" << std::hex << data[Value1] << ",0x" << data[Value2]
            << ",0x" << data[Value3] << ",0x" << data[Value4] << std::dec << '\n';
    }
    output << '\n';
    output.flags(oldFlags);
}

#if defined(R1DELTA_GRAPHICS_FLIGHT_RECORDER_TESTS)
detail::RawStorage& detail::StorageForTests() noexcept { return g_storage; }
detail::HistoryStorage& detail::HistoryForTests() noexcept { return g_history; }
void detail::SetWriterClaimedHookForTests(WriterClaimedHook hook) noexcept
{
    g_writerClaimedHook.store(hook, std::memory_order_release);
}
#endif
} // namespace r1delta::graphics_flight_recorder
