#pragma once

#include <cstdint>

// R2 style "is the wall still there" test for R1 wallrunning (docs/r2port.md, "Wallrun").
//
// R1 keeps a wallrun alive while EITHER of two 36 unit line rays (hull half width + 20, at origin + 10 and origin + 16, along the
// latched wall normal) hits anything. R2 keeps it alive only while one box sweep (hull half width cube, centre at origin + 16,
// swept along the wall normal) touches a wall within wallrun_allowed_wall_dist (13 units) of the hull face, with R2's slope
// correction. The R1 and R2 per tick movement code is otherwise the same, so only that test is replaced.
//
// Both movement copies are patched, because wallrunning is predicted: server.dll sub_180153900 and client.dll sub_180163170 are the
// line ray helper used by the movement code; the detour answers only for the two post latch call sites and forwards every other
// caller (wallrun start, wall hang) to the original code untouched. Nothing runs outside the wallrun tick.
//
// Controls: console variable r2port_wallrun_r2 (default 1, 0 = stock R1 rays, live), r2port_wallrun_allowed_wall_dist (default 13),
// environment R2PORT_WALLRUN_R2=0 (nothing is hooked at all).
//
// Fail closed: an unknown build (PE stamp, image size, helper prologue or either call site) installs nothing and logs one line.

namespace r2port::wallrun {

// Registers the two console variables (idempotent). Safe to call from both the client and the server init paths.
void RegisterConVars();

// R1O fake dedicated server: the same variables through the wrapped cvar system.
void RegisterR1ODediConVars();

// serverBase is server.dll (or the R1O server_local.dll, which is the same binary); clientBase is the retail R1 client.dll.
void InstallServer(std::uintptr_t serverBase);
void InstallClient(std::uintptr_t clientBase);

} // namespace r2port::wallrun
