#pragma once

// Narrow interface to the existing EOS fake-IP transport (eos/), so the p2p
// code does not depend on the EOS SDK headers. Implemented in
// eos/eos_network.cpp.

#include <cstddef>
#include <cstdint>
#include <string>

#include "p2p_mux.h"
#include "p2p_protocol.h"

namespace p2p::eos_bridge
{

// Blocking; initialises the EOS platform + login if needed.
bool EnsureInitialized();
bool IsReady();

// Our ProductUserId as 32 lowercase hex chars, or "" when not logged in.
std::string LocalProductUserId();

// Registers a remote ProductUserId and returns its 3ffe:: fake address.
bool RegisterPeer(const std::string& productUserId, Ipv6Bytes& outAddress, uint16_t& outPort);

// Sends a datagram to a 3ffe:: fake address.
bool SendTo(const Ipv6Bytes& address, const uint8_t* data, size_t size, Route route);

// Handles control packets waiting in the EOS queue for `route`; anything
// else is re-queued for the engine through InjectForEngine.
void PollControl(Route route);

} // namespace p2p::eos_bridge
