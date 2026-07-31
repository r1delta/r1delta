// Console commands for R1Delta
#pragma once

#include "cvar.h"

// Noclip command
void noclip_cmd(const CCommand& args);

// Dummy fullscreen map toggle
void toggleFullscreenMap_cmd(const CCommand& ccargs);

// Handles the TFO ent_fire implementation's deliberate player-only early-out
// when the command came from the trusted R1O dedicated server console. R1O's
// CCommand storage is not ABI-compatible with R1's CCommand class, so callers
// must pass the already-decoded argument vector.
bool TryHandleR1ODedicatedConsoleEntFire(int commandSource, int argc, const char* const* argv);
