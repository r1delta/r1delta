#pragma once

// HLSL to R1 shader-file conversion for the R2 port shader track. No engine dependencies (the
// engine glue is r2_shader_cache.cpp; tools/r2port/r2_shader_cli.cpp links this file alone).
//
// R1's materialsystem_dx11 loads every shader from `shaders\fxc\<name>.vcs` as a VCS v6 container
// holding D3D DXBC records (loader functions sub_18001EC50 / sub_18001EA10 / sub_18001D770 in
// materialsystem_dx11.dll). The `screenspace_general` material class names its shaders through the
// VMT keys `$pixshader` (file `<name>_ps40.vcs`) and `$vertexshader` (file `<name>_vs40.vcs`).
// This module compiles our own HLSL and writes those exact files.
//
// Ficool2's sdk_screenspace_shaders (screen-space shader SDK for Source, used with permission and
// credited in docs/r2port.md) documented the register convention (t0..t3/s0..s3 textures,
// c0..c3 constants) that screenspace_general follows; the code here is original.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace r2port::shaders {

struct ShaderMacro {
    std::string name;
    std::string value;
};

// Resolves #include "<name>" for the compiler. Return false when the name is unknown.
using IncludeResolver = bool (*)(std::string_view name, std::string& text);

struct CompileRequest {
    std::string sourceName;          // used in diagnostics
    std::string_view source;
    const char* entry{};             // "PSMain" / "VSMain"
    const char* profile{};           // "ps_4_0" / "vs_4_0"
    std::vector<ShaderMacro> macros;
    IncludeResolver includes{};
};

struct CompileResult {
    bool ok{};
    std::vector<std::uint8_t> dxbc;
    std::string messages;            // compiler errors and warnings (never empty when !ok)
};

CompileResult CompileHlsl(const CompileRequest& request);

// Wraps one DXBC blob per static combo into a VCS v6 file. combos[i] is the code for static combo
// id i (combo 0 must exist). `vertexShader` only selects the dword the stock vertex-shader files
// carry (4); the loader never reads it.
std::vector<std::uint8_t> BuildVcs(
    const std::vector<std::vector<std::uint8_t>>& combos, bool vertexShader);

// Wraps the dynamic variants of one or more static combos (ids 0..n-1) of a pixel shader into a VCS v6 file: per static
// combo one stored block with one {dynamic id, size, DXBC} record per variant, ids 0..m-1 (the stock lightmappedgeneric
// file has 12 per static combo). A requested combo id is static index * dynamic count.
std::vector<std::uint8_t> BuildPixelVcsDynamic(const std::vector<std::vector<std::vector<std::uint8_t>>>& staticCombos);

// Structural check of a VCS produced by BuildVcs / BuildPixelVcsDynamic (header, combo table, chunk bounds, record
// count per static combo). Empty string when valid, otherwise the reason.
std::string ValidateVcs(const std::vector<std::uint8_t>& vcs, std::size_t expectedCombos, std::size_t expectedRecords = 1);

struct BuiltShader {
    bool hasPixel{};
    std::size_t pixelStatics{ 1 };        // static combos in the pixel file
    std::size_t pixelRecords{ 1 };        // dynamic records inside every static combo of the pixel file
    bool hasVertex{};
    std::vector<std::uint8_t> pixelVcs;   // <name>_ps40.vcs
    std::vector<std::uint8_t> vertexVcs;       // <name>_vs40.vcs: single static combo 0 (R2_XFORM=1, R2_VCOLOR=0)
    std::vector<std::uint8_t> vertexColorVcs;  // <name>_vcol_vs40.vcs: same with R2_VCOLOR=1 (use with $vertexshader "<name>_vcol")
    std::string error;                    // empty on success; compiler text otherwise
};

// Compiles `source` (entry points PSMain and, when present, VSMain or the R2_STANDARD_VS macro) with
// ps_4_0 / vs_4_0, links the pair and wraps both as VCS. screenspace_general always asks a custom vertex shader for
// static combo 0, so each vertex file holds that one combo; see BuildShader for the two variants.
BuiltShader BuildShader(std::string_view name, std::string_view source, IncludeResolver includes);

// Checks that every pixel-shader input is produced by the vertex shader (same semantic and index,
// pixel mask subset of vertex mask). Returns an empty string when the pair links.
std::string CheckVertexPixelLinkage(
    const std::vector<std::uint8_t>& vertexDxbc, const std::vector<std::uint8_t>& pixelDxbc);

std::uint64_t HashBytes(std::string_view data, std::uint64_t seed = 14695981039346656037ull);

} // namespace r2port::shaders
