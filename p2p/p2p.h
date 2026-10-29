#pragma once

// Public entry points of the NAT traversal / multi-transport layer for the
// rest of R1Delta. See p2p/README.md for the design.

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace p2p
{

// Registers convars and console commands. Call once from Host_Init, after
// the Winsock hooks are installed (eos::InitializeNetworking).
void Initialize();

// Master server heartbeat integration (engine/net/masterserver.cpp).
void AddHeartbeatFields(nlohmann::json& heartbeat, int port);
void OnHeartbeatResponse(const std::string& body);

// Starts the server side services (EOS login, overlays, UPnP) without a
// heartbeat. The heartbeat thread calls it while the server is hidden
// (hide_server 1), where no heartbeat is sent but direct and p2p joins must
// still work. Idempotent.
void EnsureServerServices(int port);

// Server list integration: remembers each entry's advertised transports so
// delta_connect can use them.
void OnServerListEntry(const nlohmann::json& entry);

} // namespace p2p
