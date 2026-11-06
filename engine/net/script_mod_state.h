#pragma once

#include "script_error_payload.h"
#include "script_modding.h"

#include <string>
#include <vector>

// The running process's facts for script_modding.h: the shipped content manifest, whether the
// app's r1delta tree and loaded native modules match the release, and which addons are enabled.
// Everything expensive (hashing the content tree and the loaded DLLs) happens once per process on
// a low-priority background thread; nothing here runs on the game thread.
namespace ScriptModState
{

struct Classification
{
    ScriptModding::ReasonMask reasons = 0;
    // False when the shipped content manifest could not be read: `core` could not be checked, so
    // an unmodded verdict is not a statement about the r1delta tree.
    bool manifestAvailable = false;
};

// Starts the one-per-process scan (idempotent, returns immediately).
void Warm() noexcept;

// Sets Frame::modded on each frame and returns the game-level classification. Blocks until the
// process scan has finished and reads the filesystem: call from a worker thread only.
Classification Classify(std::vector<ScriptErrorTelemetry::Frame>& frames) noexcept;

// Text for the crash report's "=== Modded ===" section. Never blocks; allocates only what the
// report text needs.
std::string GetCrashReportText() noexcept;

} // namespace ScriptModState
