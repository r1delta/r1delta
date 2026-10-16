#include "crash_report_context.h"

#include <windows.h>

#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "cvar.h"

namespace {

const std::uint64_t g_processStartTick = ::GetTickCount64();

std::atomic<bool> g_crashHandlerActive{ false };

std::uint32_t CurrentProcessSeconds()
{
    return static_cast<std::uint32_t>((::GetTickCount64() - g_processStartTick) / 1000ull);
}

// Credential-shaped text must not travel in a crash report users post publicly. Deliberately
// coarse: a redacted line loses diagnostic value, a leaked token does not. The whole stored line
// is scanned (not a prefix) and quoted JSON keys are covered, because opaque values do not have
// to look like a JWT.
bool LooksSensitive(const char* text)
{
    static const char* const kMarkers[] = {
        "auth_token", "admin_token", "identity_pubkey", "authorization:", "bearer ",
        "password", "api_key", "apikey", "secret", "token=", "eyj",  // eyJ... = base64 JSON, a JWT
        "\"token\"", "\"access_token\"", "\"refresh_token\"", "\"password\"", "\"secret\"",
        "\"private_key\"", "\"api_key\"", "private_key", "access_token", "refresh_token",
    };

    char lowered[kConsoleTailLineChars];
    std::size_t length = 0;
    for (; text[length] != '\0' && length + 1 < sizeof(lowered); ++length)
        lowered[length] = static_cast<char>(::tolower(static_cast<unsigned char>(text[length])));
    lowered[length] = '\0';

    for (const char* marker : kMarkers) {
        if (std::strstr(lowered, marker) != nullptr)
            return true;
    }
    return false;
}

// One line per slot, no allocation, no lock. The tick is published last and a zero tick means
// "not ready", so a line may be dropped or torn while the process dies, but a reader (the
// crash handler, on the faulting thread) can never block on a writer.
template <std::size_t Slots, std::size_t LineChars>
class LineRing {
public:
    void Append(const char* line, bool skipIfRepeated)
    {
        if (line == nullptr || *line == '\0')
            return;

        while (*line == '\n' || *line == '\r')
            ++line;
        if (*line == '\0')
            return;

        // Claim the slot with fetch_add: console output arrives from several threads at once,
        // and loads-then-stores would let two lines share one slot.
        const std::uint32_t index = nextIndex_.fetch_add(1, std::memory_order_relaxed);
        Slot& slot = slots_[index % Slots];

        if (skipIfRepeated && index != 0 && TextMatches(slots_[(index - 1) % Slots], line)) {
            // The claimed slot stays empty; readers skip it. Worse than not claiming it, but
            // a repeated path is not worth a second write.
            return;
        }

        slot.tickSeconds.store(0, std::memory_order_relaxed);

        if (LooksSensitive(line)) {
            std::strncpy(slot.text, "<redacted: credential-shaped line>", LineChars - 1);
            slot.text[LineChars - 1] = '\0';
        }
        else {
            std::size_t out = 0;
            for (; line[out] != '\0' && out + 1 < LineChars; ++out) {
                const char c = line[out];
                slot.text[out] = (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
            }
            slot.text[out] = '\0';
        }

        slot.tickSeconds.store(CurrentProcessSeconds(), std::memory_order_release);
    }

    std::string Dump() const
    {
        const std::uint32_t written = nextIndex_.load(std::memory_order_acquire);
        const std::uint32_t count = written < Slots ? written : Slots;
        if (count == 0)
            return {};

        std::string result;
        result.reserve(count * 64);
        char prefix[32];

        for (std::uint32_t offset = 0; offset != count; ++offset) {
            const Slot& slot = slots_[(written - count + offset) % Slots];
            const std::uint32_t tick = slot.tickSeconds.load(std::memory_order_acquire);
            if (tick == 0)
                continue;  // slot is being written, or has never been used

            const std::size_t textLength = ::strnlen_s(slot.text, LineChars);
            bool hasContent = false;
            for (std::size_t position = 0; position < textLength; ++position) {
                if (slot.text[position] != ' ' && slot.text[position] != '\t') {
                    hasContent = true;
                    break;
                }
            }
            if (!hasContent)
                continue;

            ::sprintf_s(prefix, "[%us] ", tick);
            result.append(prefix);
            result.append(slot.text, textLength);
            result.push_back('\n');
        }

        return result;
    }

private:
    struct Slot {
        std::atomic<std::uint32_t> tickSeconds{ 0 };
        char text[LineChars] = {};
    };

    static bool TextMatches(const Slot& slot, const char* line)
    {
        const std::size_t length = ::strnlen_s(slot.text, LineChars);
        return length != 0 && std::strncmp(slot.text, line, length) == 0 && line[length] == '\0';
    }

    Slot slots_[Slots];
    std::atomic<std::uint32_t> nextIndex_{ 0 };
};

LineRing<kConsoleTailSlots, kConsoleTailLineChars> g_consoleRing;
LineRing<kRecentFileSlots, kRecentFileChars> g_recentFilesRing;

// --- named worker threads -------------------------------------------------------------------

constexpr std::size_t kThreadSlots = 64;

struct ThreadRecord {
    const char* name = nullptr;
    std::atomic<unsigned long> threadId{ 0 };
};

ThreadRecord g_threads[kThreadSlots];
std::atomic<std::uint32_t> g_threadNext{ 0 };

// --- watched convars ------------------------------------------------------------------------

enum class ConvarKind { Float, Int, String };

struct WatchedConvar {
    const char* name;
    ConvarKind kind;
};

// Feature flags that change which code paths run, plus the graphics/reflex knobs the driver
// crash family needs. Never add token-bearing entries: these values travel in public reports.
const WatchedConvar kWatchedConvars[] = {
    { "r1delta_cull_optimize", ConvarKind::Int },
    { "r1delta_cull_verify", ConvarKind::Int },
    { "delta_pdata_store", ConvarKind::Int },
    { "delta_online_auth_enable", ConvarKind::Int },
    { "delta_allow_empty_server", ConvarKind::Int },
    { "delta_return_to_lobby", ConvarKind::Int },
    { "delta_script_error_reporting", ConvarKind::Int },
    { "delta_script_errors_notification", ConvarKind::Int },
    { "delta_discord_username_sync", ConvarKind::Int },
    { "delta_improved_colorblind", ConvarKind::Int },
    { "delta_old_reticles", ConvarKind::Int },
    { "delta_enable_ads_sway", ConvarKind::Int },
    { "delta_damage_numbers", ConvarKind::Int },
    { "delta_damage_numbers_batching", ConvarKind::Int },
    { "delta_hud_misc_changes", ConvarKind::Int },
    { "delta_hud_show_chat", ConvarKind::Int },
    { "delta_play_hitsounds", ConvarKind::Int },
    { "delta_play_killsounds", ConvarKind::Int },
    { "cl_hold_to_rodeo_enable", ConvarKind::Int },
    { "bot_kick_on_death", ConvarKind::Int },
    { "fps_max", ConvarKind::Float },
    { "fps_max_low_latency", ConvarKind::Int },
    { "gfx_nvnUseLowLatency", ConvarKind::Int },
    { "gfx_nvnUseLowLatencyBoost", ConvarKind::Int },
    { "gfx_nvnUseMarkersToOptimize", ConvarKind::Int },
    { "mat_vsync", ConvarKind::Int },
};

ConVarR1* g_watchedConvars[std::size(kWatchedConvars)] = {};
std::atomic<bool> g_watchedConvarsAttempted{ false };

// POD in, POD out: no object that needs unwinding may live across __try.
bool ReadWatchedConvar(const WatchedConvar& spec, const ConVarR1* var, char* out, std::size_t outChars)
{
    __try {
        switch (spec.kind) {
        case ConvarKind::Float:
            ::sprintf_s(out, outChars, "%g", var->m_Value.m_fValue);
            break;
        case ConvarKind::Int:
            ::sprintf_s(out, outChars, "%d", var->m_Value.m_nValue);
            break;
        case ConvarKind::String: {
            const char* value = var->m_Value.m_pszString;
            if (value == nullptr || !TryReadEngineString(reinterpret_cast<std::uintptr_t>(value), out, outChars))
                ::strcpy_s(out, outChars, "<unreadable>");
            break;
        }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        ::strcpy_s(out, outChars, "<unreadable>");
        return false;
    }
    return true;
}

} // namespace

void RecordConsoleTailLine(const char* line)
{
    // The crash handler never needs new console lines, and writing them while the process is
    // tearing down only produces work in a dying process.
    if (g_crashHandlerActive.load(std::memory_order_relaxed))
        return;
    g_consoleRing.Append(line, /*skipIfRepeated=*/false);
}

void SetCrashHandlerActive()
{
    g_crashHandlerActive.store(true, std::memory_order_relaxed);
}

bool IsCrashHandlerActive()
{
    return g_crashHandlerActive.load(std::memory_order_relaxed);
}

std::string GetConsoleTailText()
{
    const std::string text = g_consoleRing.Dump();
    return text.empty() ? "No console output was recorded.\n" : text;
}

void RecordRecentFileOpen(const char* path)
{
    g_recentFilesRing.Append(path, /*skipIfRepeated=*/true);
}

std::string GetRecentFilesText()
{
    const std::string text = g_recentFilesRing.Dump();
    return text.empty() ? "No file opens were recorded.\n" : text;
}

unsigned long long GetProcessUptimeSeconds()
{
    return (::GetTickCount64() - g_processStartTick) / 1000ull;
}

bool TryReadEngineString(std::uintptr_t address, char* out, std::size_t outChars)
{
    if (address == 0 || out == nullptr || outChars < 2)
        return false;
    out[0] = '\0';

    __try {
        const char* source = reinterpret_cast<const char*>(address);
        const std::size_t length = ::strnlen_s(source, outChars - 1);
        std::memcpy(out, source, length);
        out[length] = '\0';
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        out[0] = '\0';
        return false;
    }
    return out[0] != '\0';
}

void RegisterR1DeltaThread(const char* name)
{
    if (name == nullptr || *name == '\0')
        return;

    const unsigned long threadId = ::GetCurrentThreadId();

    for (std::size_t i = 0; i < kThreadSlots; ++i) {
        if (g_threads[i].threadId.load(std::memory_order_acquire) == threadId)
            return;  // thread already registered, e.g. a pool worker reused across jobs
    }

    const std::uint32_t index = g_threadNext.fetch_add(1, std::memory_order_relaxed);
    if (index >= kThreadSlots)
        return;  // census is bounded; further threads stay unnamed

    g_threads[index].name = name;  // string literal, valid for the process lifetime
    g_threads[index].threadId.store(threadId, std::memory_order_release);
}

const char* GetR1DeltaThreadName(unsigned long threadId)
{
    if (threadId == 0)
        return nullptr;
    for (std::size_t i = 0; i < kThreadSlots; ++i) {
        if (g_threads[i].threadId.load(std::memory_order_acquire) == threadId)
            return g_threads[i].name;
    }
    return nullptr;
}

std::string GetR1DeltaThreadCensusText()
{
    std::string result;
    char entry[96];
    std::size_t named = 0;

    for (std::size_t i = 0; i < kThreadSlots; ++i) {
        const unsigned long threadId = g_threads[i].threadId.load(std::memory_order_acquire);
        if (threadId == 0 || g_threads[i].name == nullptr)
            continue;
        ::sprintf_s(entry, "%s%s=%lu", named == 0 ? "" : ", ", g_threads[i].name, threadId);
        result += entry;
        ++named;
    }

    if (named == 0)
        return "No R1Delta worker threads registered.\n";
    result += '\n';
    return result;
}

bool InitWatchedConvarsSnapshot()
{
    if (g_watchedConvarsAttempted.load(std::memory_order_acquire))
        return true;
    if (cvarinterface == 0 || OriginalCCVar_FindVar == nullptr)
        return false;  // cvar interface not up yet; the next call retries

    bool attempted = true;
    std::size_t resolved = 0;

    for (std::size_t i = 0; i < std::size(kWatchedConvars); ++i) {
        if (g_watchedConvars[i] == nullptr)
            g_watchedConvars[i] = OriginalCCVar_FindVar(cvarinterface, kWatchedConvars[i].name);
        if (g_watchedConvars[i] != nullptr)
            ++resolved;
    }

    // Nothing resolved means the interface answered too early rather than "these convars do
    // not exist"; keep retrying until at least one lands, then record the rest as unresolved.
    if (resolved == 0)
        attempted = false;

    g_watchedConvarsAttempted.store(attempted, std::memory_order_release);
    return attempted;
}

std::string GetWatchedConvarsText()
{
    std::string result;
    char entry[320];
    char value[192];

    for (std::size_t i = 0; i < std::size(kWatchedConvars); ++i) {
        const ConVarR1* var = g_watchedConvars[i];
        if (var == nullptr || !ReadWatchedConvar(kWatchedConvars[i], var, value, sizeof(value)))
            ::strcpy_s(value, "<unresolved>");

        ::sprintf_s(entry, "%s=%s", kWatchedConvars[i].name, value);
        if (!result.empty())
            result += ' ';
        result += entry;
    }

    result += '\n';
    return result;
}
