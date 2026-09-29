#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace p2p
{

struct LocalAddresses
{
    std::vector<uint32_t> lan;       // RFC1918 IPv4 addresses (host order)
    std::vector<uint32_t> tailscale; // 100.64.0.0/10 addresses on a Tailscale adapter
};

// Enumerates the machine's up adapters. Cached for a few seconds.
LocalAddresses GetLocalAddresses();

// Local IPv4 address used to reach the internet (0 if unknown).
uint32_t GetPrimaryLocalIpv4();

// Default IPv4 gateway (0 if unknown).
uint32_t GetDefaultGatewayIpv4();

bool IsPrivateIpv4(uint32_t ip);
bool IsTailscaleIpv4(uint32_t ip);
std::string Ipv4ToString(uint32_t ip);

} // namespace p2p
