#include "p2p_netinfo.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cwctype>
#include <mutex>

#pragma comment(lib, "iphlpapi.lib")

namespace p2p
{
namespace
{

bool ContainsNoCase(const wchar_t* haystack, const wchar_t* needle)
{
    if (!haystack || !needle)
        return false;
    std::wstring h(haystack), n(needle);
    std::transform(h.begin(), h.end(), h.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    std::transform(n.begin(), n.end(), n.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return h.find(n) != std::wstring::npos;
}

LocalAddresses Enumerate()
{
    LocalAddresses out;
    ULONG size = 16 * 1024;
    std::vector<uint8_t> buffer(size);
    const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG r = GetAdaptersAddresses(AF_INET, flags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
    if (r == ERROR_BUFFER_OVERFLOW)
    {
        buffer.resize(size);
        r = GetAdaptersAddresses(AF_INET, flags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
    }
    if (r != NO_ERROR)
        return out;

    for (auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()); a; a = a->Next)
    {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
            continue;
        const bool isTailscale = ContainsNoCase(a->Description, L"tailscale") || ContainsNoCase(a->FriendlyName, L"tailscale");
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next)
        {
            if (!u->Address.lpSockaddr || u->Address.lpSockaddr->sa_family != AF_INET)
                continue;
            const uint32_t ip = ntohl(reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr)->sin_addr.s_addr);
            if (isTailscale && IsTailscaleIpv4(ip))
                out.tailscale.push_back(ip);
            else if (!isTailscale && IsPrivateIpv4(ip))
                out.lan.push_back(ip);
        }
    }
    return out;
}

} // namespace

bool IsPrivateIpv4(uint32_t ip)
{
    return (ip >> 24) == 10 || (ip >> 20) == ((172u << 4) | 1u) || (ip >> 16) == ((192u << 8) | 168u);
}

bool IsTailscaleIpv4(uint32_t ip)
{
    return (ip & 0xFFC00000u) == 0x64400000u; // 100.64.0.0/10
}

std::string Ipv4ToString(uint32_t ip)
{
    char buf[20];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
    return buf;
}

LocalAddresses GetLocalAddresses()
{
    static std::mutex mutex;
    static LocalAddresses cached;
    static std::chrono::steady_clock::time_point cachedAt{};
    std::lock_guard lock(mutex);
    const auto now = std::chrono::steady_clock::now();
    if (cachedAt.time_since_epoch().count() == 0 || now - cachedAt > std::chrono::seconds(30))
    {
        cached = Enumerate();
        cachedAt = now;
    }
    return cached;
}

uint32_t GetPrimaryLocalIpv4()
{
    // Connecting a UDP socket sends nothing; it just picks the route.
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET)
        return 0;
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = htons(53);
    target.sin_addr.s_addr = htonl(0x01010101); // 1.1.1.1
    uint32_t ip = 0;
    if (connect(s, reinterpret_cast<sockaddr*>(&target), sizeof(target)) == 0)
    {
        sockaddr_in local{};
        int len = sizeof(local);
        if (getsockname(s, reinterpret_cast<sockaddr*>(&local), &len) == 0)
            ip = ntohl(local.sin_addr.s_addr);
    }
    closesocket(s);
    return ip;
}

uint32_t GetDefaultGatewayIpv4()
{
    MIB_IPFORWARDROW row{};
    if (GetBestRoute(htonl(0x01010101), 0, &row) != NO_ERROR)
        return 0;
    return ntohl(row.dwForwardNextHop);
}

} // namespace p2p
