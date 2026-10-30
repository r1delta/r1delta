#pragma once

#include "p2p_protocol.h"
#include <string_view>

namespace p2p
{
enum class InviteRoute { Server, Iroh, Tailcat, Eos };
struct InviteTarget
{
    InviteRoute route;
    std::string address;
    uint16_t port;
};

// Discord's 128-byte SDK buffer includes the terminating NUL.
constexpr size_t kMaxInviteSecretSize = 127;
std::optional<InviteTarget> ParseInviteTarget(std::string_view secret);
std::string EncodeOverlayInvite(Backend backend, std::string_view address, uint16_t port);
// Revalidates even manually constructed targets; invalid targets produce no command.
std::string InviteConnectCommand(const InviteTarget& target);

// Whether an IPv4 (host order) is a globally routable unicast address: not
// private, loopback, link-local, CGNAT/Tailscale, multicast or reserved.
bool IsPublicIpv4(uint32_t ip);

// Reduces the address printed by the iroh plugin's r1p_listen
// ("<id>|<relay url>|<ip:port>,...") to what may be put in a Discord secret:
// the endpoint id, the relay URL and only public IPv4 direct addresses (private,
// LAN, Tailscale and IPv6 endpoints are dropped), as much as fits in 127 bytes
// together with the "r1di1|iroh|<port>|" prefix. Empty when raw is not an iroh address.
std::string PortableIrohAddress(std::string_view raw, uint16_t port);

// "[3ffe:...]:port" EOS fake-IP invite for a local ProductUserId ("0002" + 28
// hex chars); the inverse of the decode in p2p_connect.cpp EnsureEosIdentity
// and eos/fake_ip_layer.cpp. Empty for anything else.
std::string EosInviteForProductUserId(std::string_view productUserId, uint16_t port);

// What a server can be reached by besides its UDP port.
struct HostRoutes
{
    std::string iroh;       // as printed by r1p_listen, empty when not listening
    std::string tailcat;    // "tc..." address, empty when not listening
    std::string eosPuid;    // 32 lowercase hex chars, empty when not logged in
};

// The best portable invite secret for a host that is not (yet) reachable through
// the master server: iroh (hole punching, relay fallback), then tailcat (DERP
// relay), then EOS. Empty when none is available or fits in 127 bytes, in which
// case the caller keeps the plain "ip:port" invite.
std::string HostOverlayInvite(const HostRoutes& routes, uint16_t port);
} // namespace p2p
