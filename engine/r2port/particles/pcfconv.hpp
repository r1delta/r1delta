// Origin: C:\Users\r3muxd\tmp\r2port\particles. R2 particle (PCF) converter bundled in r1delta under GPL-3.0.
// R2 -> R1 particle converter. Reads Titanfall 2 PCF files (DMX binary 5 / pcf 2, found in
// englishclient_frontend.bsp.pak000_dir.vpk under particles/) and writes one R1 PCF holding the
// requested systems and everything they reference as a child. No file I/O: callers pass bytes.
#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace r2particles {

struct SourcePcf {
    std::string name;                 // file name as in the VPK, e.g. "env_sun.pcf"
    std::vector<std::uint8_t> bytes;
};

struct Request {
    std::string system;               // R2 system name (case-insensitive)
    std::string file;                 // SourcePcf::name holding it
};

struct Options {
    // Every converted system is renamed prefix + original name so it can never replace an R1
    // system of the same name (R1 effects keep using their own definitions).
    std::string prefix = "r2p_";
};

struct SystemStatus {
    std::string target;                          // name written to the output
    std::vector<std::string> droppedAttributes;  // R2-only attributes removed, "system:attr" or "function:attr"
    std::vector<std::string> droppedFunctions;   // "list:function" with no R1 equivalent
    std::vector<std::string> approximations;     // "list:function -> replacement"
    std::size_t renderers = 0;                   // renderers left after conversion
};

struct Report {
    std::vector<std::string> systems;  // target names written, sorted
    std::vector<SystemStatus> status;  // one per system written, same order as systems
    std::vector<std::string> notes;    // one line per dropped function, approximation or failure
    std::set<std::string> materials;   // 'material' of every system written, as stored (no 'materials/')
    std::set<std::string> models;      // 'Render models' sequence models, as stored
    std::size_t droppedFunctions = 0;
    std::size_t approximatedFunctions = 0;
    std::size_t droppedAttributes = 0;
    std::size_t missingSystems = 0;    // requested systems not found in their file
};

// Target name for a requested system: Options::prefix + system, original case kept.
std::string TargetName(const Options& options, std::string_view system);

// Converts the requested systems. Succeeds when the output file is written even if individual
// requests were not found (counted in Report::missingSystems).
bool ConvertPcf(const std::vector<SourcePcf>& sources, const std::vector<Request>& requests,
    const Options& options, std::vector<std::uint8_t>& outPcf, Report& report, std::string& error);

} // namespace r2particles
