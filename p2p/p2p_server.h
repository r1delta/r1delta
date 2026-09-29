#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include "p2p_protocol.h"

namespace p2p
{

// Adds "transports" and "nat" to a heartbeat and lazily starts the server
// side services (UPnP, EOS, iroh, tailcat). Called from the heartbeat thread.
void ServerAddHeartbeatFields(nlohmann::json& heartbeat, uint16_t port);

// Consumes the master server's heartbeat reply (rendezvous token, TURN
// credentials).
void ServerHandleHeartbeatResponse(const std::string& body);

// Rendezvous control packets that arrived on the server socket.
void ServerOnRegisterAck(const RegisterAck& ack, const Ipv4Endpoint& from);
void ServerOnPunchRequest(const PunchRequest& req, const Ipv4Endpoint& from);

std::string ServerDescribe();

} // namespace p2p
