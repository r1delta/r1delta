#pragma once

// Runtime-compiled custom shaders for the R2 port (see docs/r2port.md, "Shader track").
//
// HLSL (embedded in r2_shader_sources.h, plus loose <gamedir>\r2port\shaders\<name>.hlsl overrides) is compiled with
// D3DCompile into R1 VCS v6 files under <gamedir>\r2port\shadercache. R1's shader loader opens
// `shaders\fxc\<name>.vcs` through IBaseFileSystem::Open; when R1's own file system has no such file the Open hook
// (factory.cpp, R1BaseFileSystem_Open) asks ResolveShaderFile for a cached file and opens that absolute path.
// A VMT selects a shader with `$pixshader "<name>"` / `$vertexshader "<name>"` on a `screenspace_general` material.

#include <string>

namespace r2port::shaders {

// requestedPath is the path R1 passed to Open (`shaders\fxc\<name>_ps40.vcs`, any slash style or case).
// Builds the cache on first use. Returns true and the absolute file path when a custom shader file exists for it.
bool ResolveShaderFile(const char* requestedPath, std::string& absolutePath);

// True when the cache holds a built pixel shader `<name>_ps40.vcs` for `name` (builds the cache on first use).
bool HasShaderPixelFile(const char* name);

// Recompiles every shader, ignoring the up-to-date check, and prints the result. A material that already loaded a
// shader keeps the old bytes until the map (or the material system) reloads; use new shader names while iterating.
void RebuildAllShaders();

// Prints every known shader with its build state and any compiler error.
void PrintShaderStatus();

} // namespace r2port::shaders
