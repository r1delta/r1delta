#pragma once

#include "script_modding.h"

#include <string>
#include <string_view>
#include <vector>

namespace ScriptErrorTelemetry
{

// Wire schemas of POST /v1/reports. The service advertises the ones it accepts in GET /health;
// schema 1 is what every deployed service understands and must stay byte-for-byte what it was.
constexpr int kSchemaLegacy = 1;
constexpr int kSchemaModded = 2;
constexpr std::size_t kMaxPayloadLength = 16 * 1024;

struct Frame
{
    std::string function;
    std::string source;
    int line = 0;
    bool modded = false; // schema 2 only
};

struct ReportBody
{
    std::string vmContext;
    std::string processMode;
    bool cascade = false;
    std::string error;
    std::vector<Frame> frames;
    ScriptModding::ReasonMask moddedReasons = 0; // schema 2 only
};

// The newest schema a service advertises in its GET /health body ({"status":"ok","schemas":[1,2]}).
// A service that predates schema 2 (no "schemas" member) or an unparsable body selects schema 1.
int SelectSchema(std::string_view healthResponse);

// Serializes `body` for `schema`. Frames, then error text are trimmed until the result fits
// kMaxPayloadLength (the trimmed error is written back to `body`); empty when it cannot fit.
std::string BuildPayload(ReportBody& body, int schema);

} // namespace ScriptErrorTelemetry
