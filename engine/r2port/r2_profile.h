// R2PORT_PROFILE=1 load profiler for the R2 port provider path (header only, nothing runs when the variable is unset).
//
//   R2P_SCOPE("stage")                  times the enclosing scope; records (stage, thread, start, duration)
//   R2P_SCOPE_L("stage", label)         same with a label (asset name, lump name, ...)
//   R2P_SCOPE_B(var, "stage", label)    named scope so the caller can attach a byte count: var.SetBytes(n)
//   r2port::prof::Mark("stage", label, bytes)   zero-duration event that only carries bytes
//
// With the variable unset every macro costs one relaxed atomic load. With R2PORT_PROFILE=1 each event is appended to a mutex
// protected vector (a few thousand events per map load). r2port::prof::WriteReport turns the events of the current map window
// into <dir>/<seq>_<map>.csv (one row per event) and <dir>/<seq>_<map>.txt (stage table ranked by total time).
//
// Vendored libraries (r2tex, bspdown, modelconv) include this header only when it is on the include path, so their standalone
// builds under C:\Users\r3muxd\tmp\r2port keep compiling without it (see the R2P_ macros at the bottom).
#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace r2port::prof {

struct Event {
    const char* stage;
    std::string label;
    std::uint32_t tid;
    std::int64_t startUs;   // since process profiler origin
    std::int64_t durUs;
    std::uint64_t bytes;
};

namespace detail {

inline std::atomic<int> g_state{-1};   // -1 unknown, 0 off, 1 on
inline std::mutex g_mutex;
inline std::vector<Event> g_events;
inline std::unordered_map<std::uint32_t, std::string> g_roles;
inline std::int64_t g_origin = 0;
inline std::int64_t g_frequency = 1;
inline std::string g_map;
inline std::int64_t g_mapStartUs = 0;
inline std::uint32_t g_sequence = 0;
inline std::int64_t g_signonUs = -1;
inline double g_signonAgeMs = 0.0;

inline std::int64_t NowTicks()
{
    LARGE_INTEGER value;
    QueryPerformanceCounter(&value);
    return value.QuadPart;
}

inline int Init()
{
    char flag[8]{};
    const bool on = GetEnvironmentVariableA("R2PORT_PROFILE", flag, sizeof(flag)) > 0 && flag[0] != '0';
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    int expected = -1;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_state.load(std::memory_order_relaxed) == -1) {
            g_frequency = frequency.QuadPart > 0 ? frequency.QuadPart : 1;
            g_origin = NowTicks();
            g_state.store(on ? 1 : 0, std::memory_order_release);
        }
    }
    (void)expected;
    return g_state.load(std::memory_order_acquire);
}

}  // namespace detail

inline bool Enabled()
{
    int state = detail::g_state.load(std::memory_order_relaxed);
    if (state < 0)
        state = detail::Init();
    return state == 1;
}

// Microseconds since the profiler origin.
inline std::int64_t NowUs()
{
    return (detail::NowTicks() - detail::g_origin) * 1000000ll / detail::g_frequency;
}

inline void Record(const char* stage, std::string_view label, std::int64_t startUs, std::int64_t durUs, std::uint64_t bytes = 0)
{
    if (!Enabled())
        return;
    Event event{ stage, std::string(label), static_cast<std::uint32_t>(GetCurrentThreadId()), startUs, durUs, bytes };
    std::lock_guard<std::mutex> lock(detail::g_mutex);
    detail::g_events.push_back(std::move(event));
}

inline void Mark(const char* stage, std::string_view label, std::uint64_t bytes)
{
    if (Enabled())
        Record(stage, label, NowUs(), 0, bytes);
}

// Names the calling thread in the report ("main", "conv-worker", "index", "model-prefetch", ...).
inline void SetThreadRole(const char* role)
{
    if (!Enabled())
        return;
    std::lock_guard<std::mutex> lock(detail::g_mutex);
    detail::g_roles[static_cast<std::uint32_t>(GetCurrentThreadId())] = role;
}

class Scope {
public:
    explicit Scope(const char* stage, std::string_view label = {})
        : m_stage(stage)
    {
        if (Enabled()) {
            m_active = true;
            m_label.assign(label.data(), label.size());
            m_start = NowUs();
        }
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    void SetBytes(std::uint64_t bytes) { m_bytes = bytes; }
    std::uint64_t Bytes() const { return m_bytes; }
    void SetLabel(std::string_view label) { if (m_active) m_label.assign(label.data(), label.size()); }
    ~Scope()
    {
        if (m_active)
            Record(m_stage, m_label, m_start, NowUs() - m_start, m_bytes);
    }

private:
    const char* m_stage;
    std::string m_label;
    std::int64_t m_start = 0;
    std::uint64_t m_bytes = 0;
    bool m_active = false;
};

// ---- map windows ------------------------------------------------------------------------------------------------------

// Called when a map load starts (SetActiveMap). Events recorded before the mark belong to the previous map or to start-up.
inline void MarkMapStart(std::string_view map)
{
    if (!Enabled())
        return;
    std::lock_guard<std::mutex> lock(detail::g_mutex);
    detail::g_map.assign(map.data(), map.size());
    detail::g_mapStartUs = NowUs();
    detail::g_signonUs = -1;
    ++detail::g_sequence;
}

// Milliseconds since this process was created (launch to now), from GetProcessTimes.
inline double ProcessAgeMs()
{
    FILETIME created, exited, kernel, user, now;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
        return 0.0;
    GetSystemTimeAsFileTime(&now);
    const auto asInt = [](const FILETIME& t) { return (static_cast<std::uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
    return static_cast<double>(asInt(now) - asInt(created)) / 10000.0;
}

inline void MarkSignonFull()
{
    if (!Enabled())
        return;
    std::lock_guard<std::mutex> lock(detail::g_mutex);
    detail::g_signonUs = NowUs();
    detail::g_signonAgeMs = ProcessAgeMs();
}

inline std::int64_t SignonUs()
{
    std::lock_guard<std::mutex> lock(detail::g_mutex);
    return detail::g_signonUs;
}

inline void ClearSignon()
{
    std::lock_guard<std::mutex> lock(detail::g_mutex);
    detail::g_signonUs = -1;
}

inline std::string CsvEscape(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (c == ',' || c == '\r' || c == '\n' || c == '"')
            out.push_back('_');
        else
            out.push_back(c);
    }
    return out;
}

// Writes <directory>/<seq>_<map>.csv and .txt for every event since MarkMapStart (plus start-up events when the map window
// is the first). Safe to call repeatedly: the files are rewritten from the full event list.
inline void WriteReport(const std::filesystem::path& directory, const char* tag = "")
{
    if (!Enabled())
        return;
    std::vector<Event> events;
    std::unordered_map<std::uint32_t, std::string> roles;
    std::string map;
    std::int64_t mapStart = 0, signon = -1;
    double signonAgeMs = 0.0;
    std::uint32_t sequence = 0;
    {
        std::lock_guard<std::mutex> lock(detail::g_mutex);
        events = detail::g_events;
        roles = detail::g_roles;
        map = detail::g_map;
        mapStart = detail::g_mapStartUs;
        signon = detail::g_signonUs;
        sequence = detail::g_sequence;
        signonAgeMs = detail::g_signonAgeMs;
    }
    if (map.empty())
        map = "startup";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    char base[256]{};
    _snprintf_s(base, sizeof(base), _TRUNCATE, "%02u_%s%s", sequence, map.c_str(), tag);
    const std::filesystem::path csvPath = directory / (std::string(base) + ".csv");
    const std::filesystem::path txtPath = directory / (std::string(base) + ".txt");

    // The window: everything from the map mark on (events that started earlier but ended later still count by their start).
    const std::int64_t now = NowUs();
    // The first map's window also holds the start-up events (index open, provider registration) that precede its mark.
    const std::int64_t windowStart = sequence <= 1 ? std::numeric_limits<std::int64_t>::min() : mapStart;
    std::vector<const Event*> window;
    for (const Event& event : events)
        if (event.startUs >= windowStart)
            window.push_back(&event);
    std::sort(window.begin(), window.end(), [](const Event* a, const Event* b) { return a->startUs < b->startUs; });

    if (FILE* csv = nullptr; fopen_s(&csv, csvPath.string().c_str(), "wb") == 0 && csv) {
        std::fprintf(csv, "stage,label,thread,role,start_ms,dur_ms,bytes\n");
        for (const Event* e : window) {
            const auto role = roles.find(e->tid);
            std::fprintf(csv, "%s,%s,%u,%s,%.3f,%.3f,%llu\n", e->stage, CsvEscape(e->label).c_str(), e->tid,
                role != roles.end() ? role->second.c_str() : "", (e->startUs - mapStart) / 1000.0, e->durUs / 1000.0,
                static_cast<unsigned long long>(e->bytes));
        }
        std::fclose(csv);
    }

    struct Aggregate { std::uint64_t calls = 0; std::int64_t totalUs = 0; std::int64_t maxUs = 0; std::uint64_t bytes = 0; };
    std::map<std::string, Aggregate> byStage;
    std::map<std::uint32_t, std::int64_t> busyByThread;   // conversion/worker busy time (stages starting with "w.")
    for (const Event* e : window) {
        Aggregate& a = byStage[e->stage];
        ++a.calls;
        a.totalUs += e->durUs;
        a.maxUs = std::max(a.maxUs, e->durUs);
        a.bytes += e->bytes;
        if (e->stage[0] == 'w' && e->stage[1] == 't' && e->stage[2] == '.')
            busyByThread[e->tid] += e->durUs;
    }
    std::vector<std::pair<std::string, Aggregate>> ranked(byStage.begin(), byStage.end());
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.second.totalUs > b.second.totalUs; });
    const double wallMs = (signon >= 0 ? signon - mapStart : now - mapStart) / 1000.0;
    const double windowMs = (now - mapStart) / 1000.0;

    if (FILE* txt = nullptr; fopen_s(&txt, txtPath.string().c_str(), "wb") == 0 && txt) {
        std::fprintf(txt, "R2PORT_PROFILE map=%s seq=%u events=%zu\n", map.c_str(), sequence, window.size());
        std::fprintf(txt, "map-start to signon-full: %.1f ms; map-start to report: %.1f ms; process age at signon-full: %.1f ms\n", wallMs, windowMs, signonAgeMs);
        std::fprintf(txt, "Stage totals sum the time of every event of the stage on every thread (parallel stages exceed wall time).\n");
        std::fprintf(txt, "%-26s %7s %11s %9s %7s %14s\n", "stage", "calls", "total_ms", "max_ms", "%wall", "bytes");
        for (const auto& [name, a] : ranked)
            std::fprintf(txt, "%-26s %7llu %11.1f %9.1f %6.1f%% %14llu\n", name.c_str(), static_cast<unsigned long long>(a.calls),
                a.totalUs / 1000.0, a.maxUs / 1000.0, wallMs > 0 ? 100.0 * (a.totalUs / 1000.0) / wallMs : 0.0,
                static_cast<unsigned long long>(a.bytes));
        std::fprintf(txt, "\nWorker threads (top-level stages wt.*): thread, role, busy_ms, utilisation of the map window\n");
        for (const auto& [tid, busy] : busyByThread) {
            const auto role = roles.find(tid);
            std::fprintf(txt, "  %u %-16s %9.1f ms  %5.1f%%\n", tid, role != roles.end() ? role->second.c_str() : "", busy / 1000.0,
                windowMs > 0 ? 100.0 * (busy / 1000.0) / windowMs : 0.0);
        }
        std::fclose(txt);
    }
}

}  // namespace r2port::prof

// Stage macros. In the engine build the header is on the include path; the vendored r2port libraries compile without it.
#define R2P_CAT2(a, b) a##b
#define R2P_CAT(a, b) R2P_CAT2(a, b)
#define R2P_SCOPE(stage) ::r2port::prof::Scope R2P_CAT(r2p_scope_, __LINE__)(stage)
#define R2P_SCOPE_L(stage, label) ::r2port::prof::Scope R2P_CAT(r2p_scope_, __LINE__)(stage, label)
#define R2P_SCOPE_B(var, stage, label) ::r2port::prof::Scope var(stage, label)
