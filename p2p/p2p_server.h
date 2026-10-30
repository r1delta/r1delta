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

// Starts the services without touching a heartbeat (no-op when p2p is off).
void ServerEnsureServices(uint16_t port);

// Discord join secret for a server that is not reachable through the master
// server (hidden or not listed yet): the best portable overlay invite for
// `port`, see HostOverlayInvite. Empty when the plain "ip:port" invite should
// be used (a router mapping makes the port provably public, p2p is off, no
// overlay is up) or, with *pending set, when the services are still
// starting and the caller should wait a moment.
std::string ServerHostInvite(uint16_t port, bool* pending);

// Rendezvous control packets that arrived on the server socket.
void ServerOnRegisterAck(const RegisterAck& ack, const Ipv4Endpoint& from);
void ServerOnPunchRequest(const PunchRequest& req, const Ipv4Endpoint& from);

std::string ServerDescribe();

// Tag this server puts in PONGs (hash of its list key), 0 until known.
uint64_t ServerPongTag();

} // namespace p2p
