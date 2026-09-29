#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include "p2p_protocol.h"

namespace p2p
{

// Remembers the "transports" object of a server-list entry ("ip:port" key).
void RememberServerTransports(const std::string& key, const nlohmann::json& transports);

// delta_connect: probes every path to the server in the background, then
// runs "connect <best address>". Falls back to a plain connect.
void ConnectBest(const std::string& target);

// Connects to a server by an overlay address shared out of band
// (delta_connect_iroh / delta_connect_tailcat).
void ConnectOverlay(Backend backend, const std::string& address, uint16_t port);

// Control packets for the client route.
void ClientOnPong(uint64_t probeId, uint64_t timestampUs, const char* via);
void ClientOnRegisterAck(const RegisterAck& ack, const Ipv4Endpoint& from);
void ClientOnPunch(const Id16& ticket, const Ipv4Endpoint& from);

std::string ClientDescribe();

uint64_t NowMicros();

} // namespace p2p
