// Test-only stand-in for the crash-report context.
//
// Test projects link individual product translation units. `engine/filesystem/r1o_vpk.cpp`
// names its parse workers through the crash-report thread registry, and the real registry lives
// in `engine/logging/crash_report_context.cpp` next to the watched-convar snapshot, which would
// drag the cvar interface and its implementation into every test binary. Projects that link a
// TU using these helpers add this file instead; the signatures come from the real header so a
// drift shows up as a compile error here.
#include "crash_report_context.h"

void RegisterR1DeltaThread(const char*)
{
}

void RecordRecentFileOpen(const char*)
{
}

// The allocator shim silences its foreign-free warnings through this gate; there is no crash
// handler in a test binary, so it always reads false.
bool IsCrashHandlerActive()
{
    return false;
}

void SetCrashHandlerActive()
{
}
