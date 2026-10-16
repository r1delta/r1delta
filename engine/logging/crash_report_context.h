#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Facts a crash report needs from the running process: recent console output, recent file
// opens, process timebase, named worker threads, the watched convar snapshot.
//
// Every writer here is allocation-free and lock-free, and every reader is non-blocking. The
// crash handler runs on the faulting thread while other threads are mid-work, so a busy lock
// or a half-written line may cost a line of the report — it must never hang or fault the
// reporter itself.

// --- crash-handler gate ---------------------------------------------------------------------

// Set by the crash handler before it does anything else. Everything that would allocate, log or
// otherwise feed back into the process during teardown checks this: an allocator warning or an
// MCP-console capture inside a dying process turns one crash into thousands of allocations and
// warnings, which hangs the handler instead of writing the report.
void SetCrashHandlerActive();
bool IsCrashHandlerActive();

// --- recent console lines -------------------------------------------------------------------

constexpr std::size_t kConsoleTailSlots = 128;
constexpr std::size_t kConsoleTailLineChars = 384;

void RecordConsoleTailLine(const char* line);
std::string GetConsoleTailText();

// --- recent file opens ----------------------------------------------------------------------

constexpr std::size_t kRecentFileSlots = 64;
constexpr std::size_t kRecentFileChars = 208;

void RecordRecentFileOpen(const char* path);
std::string GetRecentFilesText();

// --- process timebase -----------------------------------------------------------------------

// Seconds since tier0.dll was loaded, i.e. process uptime rather than the machine-wide value
// GetTickCount64 reports on its own.
unsigned long long GetProcessUptimeSeconds();

// --- guarded engine memory reads ------------------------------------------------------------

// Reads a NUL-terminated string out of engine memory without letting a bad pointer take the
// crash handler down with it.
bool TryReadEngineString(std::uintptr_t address, char* out, std::size_t outChars);

// --- named worker threads -------------------------------------------------------------------

// Recorded at the top of each R1Delta worker thread. Engine-owned threads are not registered:
// a faulting thread that is neither the main thread nor one of these is an engine worker.
void RegisterR1DeltaThread(const char* name);

// nullptr when the thread id was never registered.
const char* GetR1DeltaThreadName(unsigned long threadId);

std::string GetR1DeltaThreadCensusText();

// --- watched convars ------------------------------------------------------------------------

// Resolves the allowlisted convars while the cvar interface is alive and caches the pointers.
// Call once from startup, after the interface exists. Returns false when the interface is not
// ready yet, which lets callers retry later.
bool InitWatchedConvarsSnapshot();

// Values read from the cached pointers. The convar table itself is never walked here, so the
// handler does not depend on the cvar system being healthy.
std::string GetWatchedConvarsText();
