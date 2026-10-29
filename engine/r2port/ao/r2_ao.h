#pragma once

#include <cstdint>

// R1 ambient occlusion tuning for the R2 port (docs/r2port.md, "Ambient occlusion").
//
// R1 runs NVIDIA HBAO+ (GFSDK_SSAO.win64.dll, "r2_1" build) once per frame from materialsystem_dx11.dll
// sub_180017800 (RVA 0x17800), after the opaque pass: the AO is multiplied into the scene colour target. This module
// leaves that call in place and adds optional, default-OFF behaviour around it through the console variables
// r2port_ao_* (see r2_ao.cpp). With every variable at 0 nothing is hooked: no detour on sub_180017800, no vtable
// patch, no per-frame work.
//
// Fail closed: unsupported materialsystem or GFSDK builds install nothing and log one warning.

namespace r2port::ao {

// Registers the r2port_ao_* console variables and the r2port_ao_status command. Call once after the engine cvar
// system is usable (same place the other archived r1delta variables are registered).
void RegisterConVars();

// Remembers the materialsystem_dx11 base and installs the detour when a variable is already nonzero. Call from the
// materialsystem load path (next to InstallShaderOverrides).
void Install(std::uintptr_t materialSystemBase);

} // namespace r2port::ao
