#include "p2p_invite.h"

namespace p2p
{
namespace
{
bool Safe(std::string_view s)
{
    for (unsigned char c : s)
        if (c <= 32 || c >= 127 || c == '"' || c == '\\' || c == ';') return false;
    return true;
}

bool Number(std::string_view s, unsigned max, unsigned& value)
{
    if (s.empty()) return false;
    value = 0;
    for (char c : s)
    {
        if (c < '0' || c > '9') return false;
        const unsigned digit = c - '0';
        if (value > (max - digit) / 10) return false;
        value = value * 10 + digit;
    }
    return true;
}

bool Port(std::string_view s, uint16_t& port)
{
    unsigned n;
    if (!Number(s, 65535, n) || n == 0) return false;
    port = static_cast<uint16_t>(n);
    return true;
}

bool Ipv4(std::string_view s)
{
    for (int i = 0; i < 4; ++i)
    {
        const size_t end = s.find('.');
        const auto part = s.substr(0, end);
        unsigned n;
        if (part.size() > 3 || !Number(part, 255, n)) return false;
        if (i == 3) return end == s.npos;
        if (end == s.npos) return false;
        s.remove_prefix(end + 1);
    }
    return false;
}

bool Hex(std::string_view s)
{
    for (char c : s)
        if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f') && !(c >= 'A' && c <= 'F')) return false;
    return !s.empty();
}

bool Ipv6(std::string_view s)
{
    const size_t compression = s.find("::");
    if (compression != s.npos && s.find("::", compression + 2) != s.npos) return false;
    unsigned groups = 0;
    auto count = [&groups](std::string_view side)
    {
        if (side.empty()) return true;
        for (;;)
        {
            const size_t end = side.find(':');
            const auto part = side.substr(0, end);
            if (part.size() > 4 || !Hex(part)) return false;
            ++groups;
            if (end == side.npos) return true;
            side.remove_prefix(end + 1);
        }
    };
    if (compression == s.npos) return count(s) && groups == 8;
    return count(s.substr(0, compression)) && count(s.substr(compression + 2)) && groups < 8;
}

bool Endpoint(std::string_view s, bool eos = false)
{
    uint16_t port;
    if (!s.empty() && s.front() == '[')
    {
        const size_t close = s.find(']');
        if (close == s.npos || close + 1 >= s.size() || s[close + 1] != ':' || !Port(s.substr(close + 2), port)) return false;
        const auto ip = s.substr(1, close - 1);
        if (!Ipv6(ip)) return false;
        const bool local = ip.size() >= 5 && ip[0] == '3' && (ip[1] == 'f' || ip[1] == 'F') &&
            (ip[2] == 'f' || ip[2] == 'F') && (ip[3] == 'd' || ip[3] == 'D') && ip[4] == ':';
        const bool portable = ip.size() >= 5 && ip[0] == '3' && (ip[1] == 'f' || ip[1] == 'F') &&
            (ip[2] == 'f' || ip[2] == 'F') && (ip[3] == 'e' || ip[3] == 'E') && ip[4] == ':';
        return !local && (!eos || portable);
    }
    if (eos) return false;
    const size_t colon = s.find(':');
    return colon != s.npos && Ipv4(s.substr(0, colon)) && Port(s.substr(colon + 1), port);
}

bool Relay(std::string_view s)
{
    if (s.substr(0, 7) == "http://") s.remove_prefix(7);
    else if (s.substr(0, 8) == "https://") s.remove_prefix(8);
    else return false;
    const auto authority = s.substr(0, s.find_first_of("/?#"));
    if (authority.empty()) return false;
    if (authority.front() == '[')
    {
        const size_t close = authority.find(']');
        if (close == authority.npos || !Ipv6(authority.substr(1, close - 1))) return false;
        uint16_t port;
        return close + 1 == authority.size() || (authority[close + 1] == ':' && Port(authority.substr(close + 2), port));
    }
    const size_t colon = authority.find(':');
    const auto host = authority.substr(0, colon);
    if (host.empty() || host.front() == '.' || host.front() == '-' || host.back() == '-') return false;
    for (char c : host)
        if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9') && c != '.' && c != '-') return false;
    uint16_t port;
    return colon == authority.npos || Port(authority.substr(colon + 1), port);
}

bool Overlay(InviteRoute route, std::string_view address)
{
    if (address.empty() || !Safe(address)) return false;
    if (route == InviteRoute::Tailcat)
    {
        // Plugin Addr is "tc" + unpadded base64url CBOR (upstream wire format).
        if (address.size() <= 2 || address.substr(0, 2) != "tc" || (address.size() - 2) % 4 == 1) return false;
        for (char c : address.substr(2))
            if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9') && c != '-' && c != '_') return false;
        return true;
    }
    if (route != InviteRoute::Iroh) return false;
    const size_t first = address.find('|');
    const auto id = address.substr(0, first);
    if (id.size() != 64 || !Hex(id)) return false;
    if (first == address.npos) return true;
    address.remove_prefix(first + 1);
    const size_t second = address.find('|');
    const auto relay = address.substr(0, second);
    if (!relay.empty() && !Relay(relay)) return false;
    if (second == address.npos) return true;
    address.remove_prefix(second + 1);
    if (address.empty()) return true;
    for (;;)
    {
        const size_t end = address.find(',');
        if (!Endpoint(address.substr(0, end))) return false;
        if (end == address.npos) return true;
        address.remove_prefix(end + 1);
    }
}
} // namespace

std::optional<InviteTarget> ParseInviteTarget(std::string_view secret)
{
    if (secret.empty() || secret.size() > kMaxInviteSecretSize || !Safe(secret)) return std::nullopt;
    if (secret.substr(0, 6) == "r1di1|")
    {
        secret.remove_prefix(6);
        const size_t routeEnd = secret.find('|');
        const auto name = secret.substr(0, routeEnd);
        InviteRoute route;
        if (name == "iroh") route = InviteRoute::Iroh;
        else if (name == "tailcat") route = InviteRoute::Tailcat;
        else return std::nullopt;
        secret.remove_prefix(routeEnd + 1);
        const size_t portEnd = secret.find('|');
        uint16_t port;
        if (portEnd == secret.npos || !Port(secret.substr(0, portEnd), port)) return std::nullopt;
        secret.remove_prefix(portEnd + 1);
        if (!Overlay(route, secret)) return std::nullopt;
        return InviteTarget{ route, std::string(secret), port };
    }
    if (secret.front() == '[')
    {
        if (!Endpoint(secret, true)) return std::nullopt;
        uint16_t port;
        Port(secret.substr(secret.find(']') + 2), port);
        return InviteTarget{ InviteRoute::Eos, std::string(secret), port };
    }
    const size_t colon = secret.find(':');
    uint16_t port = 0;
    if (!Ipv4(secret.substr(0, colon)) || (colon != secret.npos && !Port(secret.substr(colon + 1), port))) return std::nullopt;
    return InviteTarget{ InviteRoute::Server, std::string(secret), port };
}

namespace
{
bool OverlayFits(InviteRoute route, std::string_view address, uint16_t port)
{
    if (!port) return false;
    unsigned digits = 1;
    for (unsigned n = port; n >= 10; n /= 10) ++digits;
    const size_t overhead = route == InviteRoute::Iroh ? 12 : 15;
    return address.size() + overhead + digits <= kMaxInviteSecretSize && Overlay(route, address);
}
}

std::string EncodeOverlayInvite(Backend backend, std::string_view address, uint16_t port)
{
    const InviteRoute route = backend == Backend::Iroh ? InviteRoute::Iroh : InviteRoute::Tailcat;
    if ((backend != Backend::Iroh && backend != Backend::Tailcat) || !OverlayFits(route, address, port)) return {};
    const std::string prefix = std::string("r1di1|") + (backend == Backend::Iroh ? "iroh|" : "tailcat|") + std::to_string(port) + "|";
    if (prefix.size() + address.size() > kMaxInviteSecretSize) return {};
    return prefix + std::string(address);
}

bool IsPublicIpv4(uint32_t ip)
{
    const unsigned a = ip >> 24, b = (ip >> 16) & 0xFF;
    if (a == 0 || a == 10 || a == 127 || a >= 224) return false;
    if (a == 100 && (b & 0xC0) == 64) return false; // 100.64.0.0/10
    if (a == 169 && b == 254) return false;
    if (a == 172 && (b & 0xF0) == 16) return false;
    if (a == 192 && b == 168) return false;
    return true;
}

std::string PortableIrohAddress(std::string_view raw, uint16_t port)
{
    const size_t first = raw.find('|');
    const auto id = raw.substr(0, first);
    if (id.size() != 64 || !Hex(id)) return {};
    std::string_view relay, addrs;
    if (first != raw.npos)
    {
        const auto rest = raw.substr(first + 1);
        const size_t second = rest.find('|');
        relay = rest.substr(0, second);
        if (second != rest.npos) addrs = rest.substr(second + 1);
    }
    if (!relay.empty() && !Relay(relay)) relay = {};

    std::vector<std::string> direct;
    while (!addrs.empty())
    {
        const size_t end = addrs.find(',');
        const std::string item(addrs.substr(0, end));
        if (!item.empty() && item.front() != '[')
            if (const auto ep = Ipv4Endpoint::Parse(item); ep && ep->port && IsPublicIpv4(ep->ip)) direct.push_back(item);
        if (end == addrs.npos) break;
        addrs.remove_prefix(end + 1);
    }

    const auto fits = [port](const std::string& address) { return !EncodeOverlayInvite(Backend::Iroh, address, port).empty(); };
    std::string address(id);
    bool haveRelay = false, haveDirect = false;
    if (!relay.empty())
    {
        std::string withRelay = address + "|" + std::string(relay);
        if (fits(withRelay))
        {
            address = std::move(withRelay);
            haveRelay = true;
        }
    }
    for (const auto& endpoint : direct)
    {
        const std::string separator = haveDirect ? "," : (haveRelay ? "|" : "||");
        std::string extended = address + separator + endpoint;
        if (!fits(extended)) continue;
        address = std::move(extended);
        haveDirect = true;
    }
    return fits(address) ? address : std::string();
}

std::string EosInviteForProductUserId(std::string_view productUserId, uint16_t port)
{
    if (!port || productUserId.size() != 32 || productUserId.substr(0, 4) != "0002" || !Hex(productUserId)) return {};
    const auto nibble = [](char c) -> uint8_t {
        return static_cast<uint8_t>(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
    };
    Ipv6Bytes address{};
    address[0] = 0x3f;
    address[1] = 0xfe;
    for (size_t i = 2; i < address.size(); ++i)
        address[i] = static_cast<uint8_t>((nibble(productUserId[i * 2]) << 4) | nibble(productUserId[i * 2 + 1]));
    const std::string invite = FormatIpv6Connect(address, port);
    return Endpoint(invite, true) ? invite : std::string();
}

std::string HostOverlayInvite(const HostRoutes& routes, uint16_t port)
{
    if (!routes.iroh.empty())
    {
        const std::string portable = PortableIrohAddress(routes.iroh, port);
        if (!portable.empty())
        {
            const std::string invite = EncodeOverlayInvite(Backend::Iroh, portable, port);
            if (!invite.empty()) return invite;
        }
    }
    if (!routes.tailcat.empty())
    {
        const std::string invite = EncodeOverlayInvite(Backend::Tailcat, routes.tailcat, port);
        if (!invite.empty()) return invite;
    }
    return EosInviteForProductUserId(routes.eosPuid, port);
}

std::string InviteConnectCommand(const InviteTarget& target)
{
    if (target.route == InviteRoute::Iroh || target.route == InviteRoute::Tailcat)
    {
        if (!OverlayFits(target.route, target.address, target.port)) return {};
        return std::string("disconnect;delta_connect_") + (target.route == InviteRoute::Iroh ? "iroh" : "tailcat") + " \"" + target.address + "\" " + std::to_string(target.port) + "\n";
    }
    const auto parsed = ParseInviteTarget(target.address);
    if (!parsed || parsed->route != target.route || parsed->port != target.port) return {};
    return "disconnect;delta_connect " + target.address + "\n";
}
} // namespace p2p
