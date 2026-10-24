#include "client/rendering/graphics_flight_recorder.h"
#include "client/rendering/graphics_flight_recorder_storage.h"
#include "engine/logging/crash_report_memory_capture.h"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <charconv>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
namespace recorder = r1delta::graphics_flight_recorder;
using namespace recorder::detail;
using recorder::EventKind;
int g_failures = 0;

void Check(bool condition, const char* name)
{
    if (!condition)
    {
        ++g_failures;
        std::cerr << "FAILED: graphics recorder: " << name << '\n';
    }
}

bool FindResource(std::uintptr_t key, ResourceData& data, std::size_t* index = nullptr)
{
    auto& storage = StorageForTests();
    for (std::size_t i = 0; i < storage.resources.size(); ++i)
    {
        if (ReadSlot(storage.resources[i], data) == ReadResult::Complete
            && data[ResourceKey] == key
            && data[IdentityEpoch] == storage.buckets[i / kBucketWays].identityEpoch.load())
        {
            if (index)
                *index = i;
            return true;
        }
    }
    return false;
}

template <std::size_t Capacity>
std::vector<EventData> CompleteEvents(const std::array<EventSlot, Capacity>& slots)
{
    std::vector<EventData> result;
    for (const auto& slot : slots)
    {
        EventData data{};
        if (ReadSlot(slot, data) == ReadResult::Complete)
            result.push_back(data);
    }
    return result;
}

void TestInitializationAndIdentity()
{
    auto& storage = StorageForTests();
    std::ostringstream inactive;
    recorder::WriteReport(inactive);
    Check(storage.header.activity.load() == 0 && storage.header.sequence.load() == 0,
        "empty report does not activate recording or manufacture events");
    Check(recorder::Initialize(), "both process-lifetime regions register");
    Check(recorder::Initialize(), "repeated initialization preserves successful registration");
    Check(storage.header.registeredRegions.load() == 3, "both registration outcomes are retained");
    Check(sizeof(storage) <= r1delta::logging::crash_report_minidump::kMaximumRegisteredMemoryRegionBytes
        && sizeof(HistoryForTests()) <= r1delta::logging::crash_report_minidump::kMaximumRegisteredMemoryRegionBytes,
        "individual recorder regions respect the existing capture limit");
    Check(storage.header.layout.magic == kMagic && storage.header.layout.version == kVersion
        && storage.header.layout.storageSize == sizeof(storage)
        && storage.header.layout.historyAddress == reinterpret_cast<std::uintptr_t>(&HistoryForTests()),
        "raw layout self-identifies and locates its separately registered history");

    recorder::Record(EventKind::RecorderStarted, 0, 0, 0x3f, 0x7f, 0x180000000,
        (std::uint64_t{0x54874238} << 32) | 0x3e0000);
    Check(storage.header.activity.load() == 0, "hook coverage alone does not imply graphics activity");

    // Deliberately inaccessible fake addresses: core must never dereference them.
    constexpr std::uintptr_t key = 0x12345000;
    recorder::ResourceDescription description;
    description.resource = 0x1111222233334444;
    description.shaderView = 0x5555666677778888;
    description.targetView = 0x9999aaaabbbbcccc;
    description.width = 1200;
    description.height = 700;
    description.depth = 19;
    description.format = 91;
    description.mipCount = 7;
    description.arraySize = 6;
    description.flags = 0xf0e1d2c3;
    description.viewLayout = 3;
    description.dimension = 3;
    const auto first = recorder::ResourceCreateBegin(key, description, 0x1234);
    Check(first != 0 && recorder::ResourceGeneration(key) == first,
        "creation identity is visible before the native call can return");
    ResourceData state{};
    Check(FindResource(key, state) && (state[Status] & CreationStarted)
        && !(state[Status] & CreationReturned), "creation-in-progress is distinguishable from return");
    recorder::ResourceCreateEnd(key, first, description, -42);
    Check(FindResource(key, state) && state[Resource] == description.resource
        && state[ShaderView] == description.shaderView && state[TargetView] == description.targetView
        && state[WidthAndHeight] == (description.width | (std::uint64_t{description.height} << 32))
        && state[Depth] == description.depth
        && state[FormatAndMips] == (description.format | (std::uint64_t{description.mipCount} << 32))
        && state[ArraySizeAndFlags] == (description.arraySize | (std::uint64_t{description.flags} << 32))
        && state[ViewLayoutAndDimension] == (description.viewLayout | (std::uint64_t{description.dimension} << 32))
        && static_cast<std::int64_t>(state[Result]) == -42,
        "creation retains exact descriptor, view-layout, borrowed addresses and native result");

    recorder::ResourceReleaseBegin(key, first, 0x5678);
    recorder::Record(EventKind::ShaderResourceSubmit, key, first, description.shaderView);
    recorder::ResourceReleaseEnd(key, first);
    Check(FindResource(key, state) && (state[Status] & OwnerRetired)
        && (state[Status] & ReleaseReturned) && state[RetirementSequence] < state[LastUseSequence]
        && state[UseAfterRetirementCount] == 1 && recorder::ResourceGeneration(key) == first,
        "owner retirement is retained separately from subsequent use and never erases identity");

    description.width = 300;
    const auto second = recorder::ResourceCreateBegin(key, description, 0x8888);
    Check(second > first && recorder::ResourceGeneration(key) == second,
        "same native address receives a fresh monotonic creation generation");
    Check(FindResource(key, state) && !(state[Status] & OwnerRetired)
        && state[UseAfterRetirementCount] == 0, "recreation does not inherit old ownership state");
    recorder::ResourceReleaseEnd(key, first);
    recorder::ResourceCreateEnd(key, first, {}, 0);
    Check(FindResource(key, state) && state[StateGeneration] == second
        && !(state[Status] & OwnerRetired) && state[Resource] == description.resource,
        "late events for an old generation cannot mutate the replacement");
    const auto third = recorder::ResourceCreateBegin(key + 0x10000, description, 0);
    Check(third > second, "creation generations are global rather than per address");
    const auto generationBeforeLookup = storage.header.generation.load();
    Check(recorder::ResourceGeneration(0xdeadbeef) == 0
        && storage.header.generation.load() == generationBeforeLookup,
        "unknown lookup returns zero without inventing an identity");

    const auto sequenceBeforeReport = storage.header.sequence.load();
    std::ostringstream live;
    recorder::WriteReport(live);
    Check(storage.header.sequence.load() == sequenceBeforeReport
        && !(storage.header.writerGate.load() & kSealed), "non-crash reports neither publish nor freeze");
    recorder::FrameBoundary(true);
    recorder::FrameBoundary(false);
    recorder::ResetBoundary(EventKind::LevelShutdownBegin);
    recorder::ResetBoundary(EventKind::LevelShutdownEnd);
    recorder::ResetBoundary(EventKind::DeviceReady);
    Check(FindResource(key, state) && state[StateGeneration] == second,
        "frame and reset boundaries preserve older resource evidence");
    const auto events = CompleteEvents(HistoryForTests().lifecycle);
    bool oldCreation = false, mapBoundary = false, deviceBoundary = false;
    for (const auto& event : events)
    {
        oldCreation |= event[Key] == key && event[Generation] == first;
        mapBoundary |= static_cast<EventKind>(event[ThreadAndKind] >> 32) == EventKind::LevelShutdownBegin
            && event[MapEpoch] != 0 && event[ResetEpoch] != 0;
        deviceBoundary |= static_cast<EventKind>(event[ThreadAndKind] >> 32) == EventKind::DeviceReady
            && event[DeviceEpoch] != 0 && event[Frame] != 0;
    }
    Check(oldCreation && mapBoundary && deviceBoundary, "history spans map/device epochs with frame identity");
}

void TestBoundedStateLoss()
{
    auto& storage = StorageForTests();
    const auto evictedBefore = storage.header.liveEvictions.load();
    constexpr std::uintptr_t base = 0x400000000;
    for (std::size_t i = 0; i <= kResourceCapacity; ++i)
        recorder::ResourceCreateBegin(base + i * 0x100, {}, 0);
    std::size_t retained = 0;
    for (std::size_t i = 0; i <= kResourceCapacity; ++i)
        retained += recorder::ResourceGeneration(base + i * 0x100) != 0;
    Check(retained <= kResourceCapacity && retained < kResourceCapacity + 1
        && storage.header.liveEvictions.load() > evictedBefore
        && storage.header.stateCollisions.load() != 0,
        "bounded resource pressure exposes collisions, eviction and unknown identities");

    constexpr std::uintptr_t key = 0x987654321;
    const auto oldGeneration = recorder::ResourceCreateBegin(key, {}, 0);
    ResourceData state{};
    std::size_t index = 0;
    const bool found = FindResource(key, state, &index);
    Check(found, "contention test locates its own current resource");
    if (!found)
        return;
    auto& bucket = storage.buckets[index / kBucketWays];
    const auto invalidations = storage.header.identityInvalidations.load();
    // Model a stopped table writer without ever blocking a production writer.
    bucket.writing.store(1);
    const auto missedGeneration = recorder::ResourceCreateBegin(key, {}, 0);
    bucket.writing.store(0);
    Check(missedGeneration > oldGeneration && recorder::ResourceGeneration(key) == 0
        && storage.header.identityInvalidations.load() > invalidations,
        "lost same-address creation invalidates stale identity instead of returning the old generation");
    const auto repairedGeneration = recorder::ResourceCreateBegin(key, {}, 0);
    Check(repairedGeneration > missedGeneration && recorder::ResourceGeneration(key) == repairedGeneration,
        "later observed creation restores a known identity without inventing missed state");
    recorder::ResourceReleaseBegin(key, 0, 0);
    Check(recorder::ResourceGeneration(key) == 0,
        "an unassociated owner release leaves uncertainty rather than apparently live state");
}

void TestIndependentRingWrap()
{
    auto& header = StorageForTests().header;
    const auto lifecycleBefore = CompleteEvents(HistoryForTests().lifecycle);
    const auto overwrittenBefore = header.submissionOverwritten.load();
    const auto ticketsBefore = header.submissionTickets.load();
    for (std::size_t i = 0; i < kSubmissionCapacity + 17; ++i)
        recorder::Record(EventKind::ShaderResourceSubmit, 0, 0, i, i + 1, i + 2, i + 3);
    Check(CompleteEvents(HistoryForTests().lifecycle) == lifecycleBefore,
        "busy submission traffic cannot erase lifecycle history");
    Check(header.submissionTickets.load() - ticketsBefore == kSubmissionCapacity + 17
        && header.submissionOverwritten.load() > overwrittenBefore
        && CompleteEvents(HistoryForTests().submissions).size() == kSubmissionCapacity,
        "submission wrap is bounded and explicitly counted");
    const auto lifecycleOverwrittenBefore = header.lifecycleOverwritten.load();
    for (std::size_t i = 0; i < kLifecycleCapacity + 7; ++i)
        recorder::Record(EventKind::VtfLoadBegin, 0, 0, i);
    Check(header.lifecycleOverwritten.load() > lifecycleOverwrittenBefore
        && CompleteEvents(HistoryForTests().lifecycle).size() == kLifecycleCapacity,
        "lifecycle wrap has independent overwrite accounting");
    EventData coverage{};
    Check(ReadSlot(StorageForTests().coverage, coverage) == ReadResult::Complete
        && coverage[Value1] == 0x3f && coverage[Value2] == 0x7f,
        "hook coverage survives both history rings wrapping");
    const auto boundaries = CompleteEvents(HistoryForTests().boundaries);
    bool retainedMapBoundary = false;
    bool retainedDeviceBoundary = false;
    for (const auto& event : boundaries)
    {
        retainedMapBoundary |= static_cast<EventKind>(event[ThreadAndKind] >> 32) == EventKind::LevelShutdownBegin;
        retainedDeviceBoundary |= static_cast<EventKind>(event[ThreadAndKind] >> 32) == EventKind::DeviceReady;
    }
    Check(retainedMapBoundary && retainedDeviceBoundary,
        "dedicated boundary history survives texture churn and submission wrap");
}

void TestCompactReportPreservesFullHistory()
{
    for (std::uint64_t i = 0; i < 128; ++i)
        recorder::Record(EventKind::ShaderResourceSubmit, 0, 0,
            UINT64_MAX - i, UINT64_MAX, UINT64_MAX, UINT64_MAX);
    // Boundary entries are also in the lifecycle ring: the text must merge
    // duplicates before selecting its newest tail.
    recorder::ResetBoundary(EventKind::LevelShutdownBegin);
    recorder::ResetBoundary(EventKind::LevelShutdownEnd);
    const auto lifecycle = CompleteEvents(HistoryForTests().lifecycle);
    const auto submissions = CompleteEvents(HistoryForTests().submissions);
    const auto boundaries = CompleteEvents(HistoryForTests().boundaries);
    auto retained = lifecycle;
    retained.insert(retained.end(), submissions.begin(), submissions.end());
    retained.insert(retained.end(), boundaries.begin(), boundaries.end());
    std::sort(retained.begin(), retained.end(),
        [](const auto& a, const auto& b) { return a[Sequence] < b[Sequence]; });
    retained.erase(std::unique(retained.begin(), retained.end(),
        [](const auto& a, const auto& b) { return a[Sequence] == b[Sequence]; }), retained.end());

    const auto sequence = StorageForTests().header.sequence.load();
    std::ostringstream output;
    recorder::WriteReport(output);
    const auto text = output.str();
    Check(text.size() < 32u * 1024u,
        "readable report stays below 32 KiB with full histories instead of expanding every event");
    std::vector<std::uint64_t> displayed;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        constexpr std::string_view prefix = "event seq=";
        if (!line.starts_with(prefix))
            continue;
        std::uint64_t value = 0;
        const auto parsed = std::from_chars(line.data() + prefix.size(),
            line.data() + line.size(), value);
        Check(parsed.ec == std::errc{}, "structured text event has a readable sequence");
        displayed.push_back(value);
    }
    std::vector<std::uint64_t> expected;
    for (auto event = retained.end() - 32; event != retained.end(); ++event)
        expected.push_back((*event)[Sequence]);
    Check(displayed == expected,
        "short text tail contains the newest 32 distinct events in sequence order, including boundaries");
    Check(StorageForTests().header.sequence.load() == sequence
        && CompleteEvents(HistoryForTests().lifecycle) == lifecycle
        && CompleteEvents(HistoryForTests().submissions) == submissions
        && CompleteEvents(HistoryForTests().boundaries) == boundaries,
        "text abbreviation never trims or mutates the full histories retained for the minidump");
}


bool CoherentSubmission(const EventData& data)
{
    return data[Value2] == ~data[Value1]
        && data[Value3] == (data[Value1] ^ 0x1020304050607080ULL)
        && data[Value4] == data[Value1] + 7;
}

void TestConcurrentPublication()
{
    auto& header = StorageForTests().header;
    const auto ticketsBefore = header.submissionTickets.load();
    const auto publishedBefore = header.submissionPublished.load();
    const auto droppedBefore = header.submissionDropped.load();
    constexpr std::size_t writers = 4;
    std::barrier start(static_cast<std::ptrdiff_t>(writers + 1));
    std::array<std::thread, writers> threads;
    for (std::size_t writer = 0; writer < writers; ++writer)
    {
        threads[writer] = std::thread([&, writer]
        {
            start.arrive_and_wait();
            for (std::size_t i = 0; i < kSubmissionCapacity * 2; ++i)
            {
                const auto token = ((writer + 1ULL) << 48) | (i + 1);
                recorder::Record(EventKind::TxaaQueued, 0, 0, token, ~token,
                    token ^ 0x1020304050607080ULL, token + 7);
            }
        });
    }
    start.arrive_and_wait();
    std::ostringstream live;
    recorder::WriteReport(live);
    bool coherent = true;
    std::size_t observed = 0;
    for (unsigned pass = 0; pass < 16; ++pass)
    {
        for (const auto& slot : HistoryForTests().submissions)
        {
            EventData data{};
            if (ReadSlot(slot, data) == ReadResult::Complete
                && static_cast<EventKind>(data[ThreadAndKind] >> 32) == EventKind::TxaaQueued)
            {
                ++observed;
                coherent &= CoherentSubmission(data);
            }
        }
    }
    for (auto& thread : threads)
        thread.join();
    for (const auto& data : CompleteEvents(HistoryForTests().submissions))
    {
        if (static_cast<EventKind>(data[ThreadAndKind] >> 32) == EventKind::TxaaQueued)
        {
            ++observed;
            coherent &= CoherentSubmission(data);
        }
    }
    Check(observed != 0 && coherent, "concurrent wrap never accepts a torn multiword publication");
    Check(header.submissionTickets.load() - ticketsBefore
        == header.submissionPublished.load() - publishedBefore + header.submissionDropped.load() - droppedBefore,
        "every completed concurrent attempt is either published or explicitly dropped");
    Check(!(header.writerGate.load() & kSealed), "concurrent report leaves future recording enabled");
}

HANDLE g_writerClaimed = nullptr;
HANDLE g_writerResume = nullptr;
std::atomic<bool> g_pauseArmed{};

void PauseClaimedWriter(EventKind kind)
{
    if (kind == EventKind::TxaaExecute && g_pauseArmed.exchange(false))
    {
        SetEvent(g_writerClaimed);
        WaitForSingleObject(g_writerResume, 10000);
    }
}

void TestFreezeInterruptedWriter()
{
    auto& header = StorageForTests().header;
    g_writerClaimed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_writerResume = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE frozen = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Check(g_writerClaimed && g_writerResume && frozen, "bounded freeze synchronization is available");
    if (!g_writerClaimed || !g_writerResume || !frozen)
    {
        if (g_writerClaimed) CloseHandle(g_writerClaimed);
        if (g_writerResume) CloseHandle(g_writerResume);
        if (frozen) CloseHandle(frozen);
        recorder::Freeze();
        return;
    }
    const auto ticket = header.submissionTickets.load();
    auto& interrupted = HistoryForTests().submissions[ticket % kSubmissionCapacity];
    g_pauseArmed.store(true);
    SetWriterClaimedHookForTests(PauseClaimedWriter);
    std::thread writer([] { recorder::Record(EventKind::TxaaExecute, 0, 0, 11, 22, 33, 44); });
    const bool claimed = WaitForSingleObject(g_writerClaimed, 5000) == WAIT_OBJECT_0;
    Check(claimed, "test pauses an actual recorder writer after its slot reservation");
    const auto droppedBefore = header.submissionDropped.load();
    if (claimed)
    {
        for (std::size_t i = 0; i < kSubmissionCapacity; ++i)
            recorder::Record(EventKind::ShaderResourceSubmit, 0, 0, i);
        Check(header.submissionDropped.load() > droppedBefore,
            "a busy wrapped slot drops visibly instead of waiting for its writer");
    }
    std::thread freezer([&] { recorder::Freeze(); SetEvent(frozen); });
    const bool progressed = WaitForSingleObject(frozen, 2000) == WAIT_OBJECT_0;
    Check(progressed, "freeze completes while an admitted writer remains stopped");
    EventData data{};
    if (claimed && progressed)
    {
        Check(header.writersAtFreeze.load() != 0 && header.freezeComplete.load() != 0
            && ReadSlot(interrupted, data) == ReadResult::Incomplete,
            "frozen interrupted publication is explicit and cannot be read as complete");
        std::ostringstream report;
        const auto sequenceBeforeReport = header.sequence.load();
        recorder::WriteReport(report);
        Check(header.sequence.load() == sequenceBeforeReport
            && ReadSlot(interrupted, data) == ReadResult::Incomplete,
            "frozen report leaves the interrupted publication invalid without manufacturing new events");
    }
    SetEvent(g_writerResume);
    writer.join();
    freezer.join();
    SetWriterClaimedHookForTests(nullptr);
    if (claimed)
    {
        Check((interrupted.stamp.load() & (kSealed | kBusy)) == (kSealed | kBusy)
            && ReadSlot(interrupted, data) == ReadResult::Incomplete,
            "resuming an interrupted writer cannot publish over the crash seal");
    }
    const auto ticketsAfterFreeze = header.submissionTickets.load();
    const auto rejectedBefore = header.rejectedAfterFreeze.load();
    recorder::Record(EventKind::ShaderResourceSubmit);
    recorder::FrameBoundary(true);
    Check(header.submissionTickets.load() == ticketsAfterFreeze
        && header.rejectedAfterFreeze.load() > rejectedBefore
        && (header.writerGate.load() & ~kSealed) == 0,
        "freeze closes admission without leaking active writer claims");
    recorder::Freeze();
    CloseHandle(g_writerClaimed);
    CloseHandle(g_writerResume);
    CloseHandle(frozen);
    g_writerClaimed = g_writerResume = nullptr;
}
} // namespace

int RunGraphicsFlightRecorderTests()
{
    TestInitializationAndIdentity();
    TestBoundedStateLoss();
    TestIndependentRingWrap();
    TestCompactReportPreservesFullHistory();
    TestConcurrentPublication();
    TestFreezeInterruptedWriter();
    return g_failures;
}
