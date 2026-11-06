#include "script_error_payload.h"

#include <nlohmann/json.hpp>

namespace ScriptErrorTelemetry
{
namespace
{

nlohmann::json BuildJson(const ReportBody& body, int schema)
{
    nlohmann::json payload;
    payload["schema"] = schema;
    payload["vmContext"] = body.vmContext;
    payload["processMode"] = body.processMode;
    payload["cascade"] = body.cascade;
    payload["error"] = body.error;
    payload["frames"] = nlohmann::json::array();
    for (const Frame& frame : body.frames) {
        nlohmann::json entry = {
            { "function", frame.function },
            { "source", frame.source },
            { "line", frame.line }
        };
        if (schema >= kSchemaModded)
            entry["modded"] = frame.modded;
        payload["frames"].push_back(std::move(entry));
    }
    if (schema >= kSchemaModded) {
        payload["modded"] = nlohmann::json::array();
        for (const std::string_view reason : ScriptModding::ReasonNames(body.moddedReasons))
            payload["modded"].push_back(std::string(reason));
    }
    return payload;
}

std::string Serialize(const nlohmann::json& payload)
{
    return payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

} // namespace

std::string BuildPayload(ReportBody& body, int schema)
{
    nlohmann::json payload = BuildJson(body, schema);
    std::string serialized = Serialize(payload);
    while (serialized.size() > kMaxPayloadLength && !payload["frames"].empty()) {
        payload["frames"].erase(payload["frames"].end() - 1);
        serialized = Serialize(payload);
    }
    while (serialized.size() > kMaxPayloadLength && body.error.size() > 64) {
        body.error.resize(body.error.size() - 64);
        payload["error"] = body.error;
        serialized = Serialize(payload);
    }
    return serialized.size() <= kMaxPayloadLength ? serialized : std::string();
}

int SelectSchema(std::string_view healthResponse)
{
    const nlohmann::json health = nlohmann::json::parse(healthResponse, nullptr, false);
    if (health.is_discarded() || !health.is_object())
        return kSchemaLegacy;
    const auto status = health.find("status");
    const auto schemas = health.find("schemas");
    if (status == health.end() || !status->is_string() || *status != "ok"
        || schemas == health.end() || !schemas->is_array()) {
        return kSchemaLegacy;
    }
    for (const nlohmann::json& schema : *schemas) {
        if (schema.is_number_integer() && schema.get<int>() == kSchemaModded)
            return kSchemaModded;
    }
    return kSchemaLegacy;
}

} // namespace ScriptErrorTelemetry
