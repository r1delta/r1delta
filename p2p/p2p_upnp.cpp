#include "p2p_upnp.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <httplib.h>

#include <chrono>
#include <set>
#include <vector>

#include "p2p_log.h"
#include "p2p_mux.h"
#include "p2p_netinfo.h"
#include "upnp_codec.h"

namespace p2p
{
namespace
{

constexpr uint32_t kLeaseSeconds = 3600;
constexpr const char* kDescription = "R1Delta game server";

bool WaitReadable(SOCKET s, int timeoutMs)
{
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(s, &readSet);
    timeval tv{ timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
    return select(0, &readSet, nullptr, nullptr, &tv) > 0;
}

std::vector<std::string> DiscoverLocations(uint32_t localIp)
{
    std::vector<std::string> locations;
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET)
        return locations;

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(localIp);
    bind(s, reinterpret_cast<sockaddr*>(&local), sizeof(local));
    in_addr iface{};
    iface.s_addr = htonl(localIp);
    setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, reinterpret_cast<const char*>(&iface), sizeof(iface));
    DWORD ttl = 2;
    setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, reinterpret_cast<const char*>(&ttl), sizeof(ttl));

    sockaddr_in group{};
    group.sin_family = AF_INET;
    group.sin_port = htons(1900);
    group.sin_addr.s_addr = htonl(0xEFFFFFFA); // 239.255.255.250

    static const char* kTargets[] = {
        "urn:schemas-upnp-org:device:InternetGatewayDevice:1",
        "urn:schemas-upnp-org:device:InternetGatewayDevice:2",
        "urn:schemas-upnp-org:service:WANIPConnection:1",
    };
    for (int round = 0; round < 2; ++round)
    {
        for (const char* target : kTargets)
        {
            const std::string msg = upnp::BuildMSearch(target);
            RealSendTo(s, msg.data(), static_cast<int>(msg.size()), reinterpret_cast<sockaddr*>(&group), sizeof(group));
        }
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(round == 0 ? 1200 : 800);
        std::set<std::string> seen(locations.begin(), locations.end());
        char buf[2048];
        while (std::chrono::steady_clock::now() < until)
        {
            if (!WaitReadable(s, 100))
                continue;
            sockaddr_storage from{};
            int fromLen = sizeof(from);
            const int n = RealRecvFrom(s, buf, sizeof(buf) - 1, reinterpret_cast<sockaddr*>(&from), &fromLen);
            if (n <= 0)
                continue;
            if (auto loc = upnp::ParseSsdpLocation(std::string(buf, n)); loc && seen.insert(*loc).second)
                locations.push_back(*loc);
        }
        if (!locations.empty())
            break;
    }
    closesocket(s);
    return locations;
}

std::unique_ptr<httplib::Client> MakeClient(const upnp::Url& url)
{
    auto cli = std::make_unique<httplib::Client>(url.host, url.port);
    cli->set_connection_timeout(2, 0);
    cli->set_read_timeout(3, 0);
    cli->set_write_timeout(3, 0);
    return cli;
}

bool SoapCall(const std::string& controlUrl, const std::string& serviceType, const char* action, const std::string& body,
              std::string& response, int& status)
{
    const auto url = upnp::ParseUrl(controlUrl);
    if (!url)
        return false;
    auto cli = MakeClient(*url);
    httplib::Headers headers{ { "SOAPAction", upnp::SoapAction(serviceType, action) } };
    auto res = cli->Post(url->path, headers, body, "text/xml; charset=\"utf-8\"");
    if (!res)
        return false;
    status = res->status;
    response = res->body;
    return true;
}

} // namespace

PortMapper& PortMap()
{
    static PortMapper mapper;
    return mapper;
}

void PortMapper::Start(uint16_t port)
{
    std::lock_guard lock(m_mutex);
    if (m_running.load() && m_port == port)
        return;
    if (m_running.load())
        return; // a different port is already mapped; keep the first one
    m_port = port;
    m_running.store(true);
    m_thread = std::thread([this, port] { Run(port); });
    m_thread.detach();
}

void PortMapper::Stop()
{
    m_running.store(false);
}

PortMappingStatus PortMapper::Status() const
{
    std::lock_guard lock(m_mutex);
    return m_status;
}

bool PortMapper::TryUpnp(uint16_t port, uint32_t localIp)
{
    std::string controlUrl, serviceType;
    {
        std::lock_guard lock(m_mutex);
        controlUrl = m_upnpControlUrl;
        serviceType = m_upnpServiceType;
    }

    if (controlUrl.empty())
    {
        for (const std::string& location : DiscoverLocations(localIp))
        {
            const auto url = upnp::ParseUrl(location);
            if (!url)
                continue;
            auto cli = MakeClient(*url);
            auto res = cli->Get(url->path);
            if (!res || res->status != 200)
                continue;
            if (auto svc = upnp::FindWanService(res->body, location))
            {
                controlUrl = svc->controlUrl;
                serviceType = svc->serviceType;
                break;
            }
        }
        if (controlUrl.empty())
            return false;
        std::lock_guard lock(m_mutex);
        m_upnpControlUrl = controlUrl;
        m_upnpServiceType = serviceType;
    }

    const std::string localText = Ipv4ToString(localIp);
    std::string response;
    int status = 0;
    uint32_t lease = kLeaseSeconds;
    bool ok = SoapCall(controlUrl, serviceType, "AddPortMapping",
                       upnp::BuildAddPortMapping(serviceType, port, "UDP", port, localText, kDescription, lease), response,
                       status) &&
              status == 200;
    if (!ok && upnp::SoapErrorCode(response) == 725)
    {
        // OnlyPermanentLeasesSupported: fall back to an unlimited lease.
        lease = 0;
        ok = SoapCall(controlUrl, serviceType, "AddPortMapping",
                      upnp::BuildAddPortMapping(serviceType, port, "UDP", port, localText, kDescription, lease), response,
                      status) &&
             status == 200;
    }
    if (!ok)
    {
        std::lock_guard lock(m_mutex);
        m_status.error = "UPnP AddPortMapping failed (HTTP " + std::to_string(status) + ", UPnP error " +
                         std::to_string(upnp::SoapErrorCode(response)) + ")";
        // Forget the control point so the next attempt rediscovers it.
        m_upnpControlUrl.clear();
        return false;
    }

    std::string externalIp;
    if (SoapCall(controlUrl, serviceType, "GetExternalIPAddress", upnp::BuildGetExternalIp(serviceType), response, status) &&
        status == 200)
        externalIp = upnp::XmlValue(response, "NewExternalIPAddress").value_or("");

    std::lock_guard lock(m_mutex);
    m_status.active = true;
    m_status.method = "upnp";
    m_status.externalIp = externalIp;
    m_status.externalPort = port;
    m_status.error.clear();
    return true;
}

bool PortMapper::TryNatPmp(uint16_t port, uint32_t lifetime)
{
    const uint32_t gateway = GetDefaultGatewayIpv4();
    if (!gateway)
        return false;
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET)
        return false;
    sockaddr_in local{};
    local.sin_family = AF_INET;
    bind(s, reinterpret_cast<sockaddr*>(&local), sizeof(local));
    sockaddr_in gw{};
    gw.sin_family = AF_INET;
    gw.sin_port = htons(5351);
    gw.sin_addr.s_addr = htonl(gateway);

    auto exchange = [&](const std::vector<uint8_t>& request, uint8_t expectOpcode) -> std::optional<upnp::NatPmpResponse> {
        int timeout = 250;
        for (int attempt = 0; attempt < 4; ++attempt, timeout *= 2)
        {
            RealSendTo(s, request.data(), static_cast<int>(request.size()), reinterpret_cast<sockaddr*>(&gw), sizeof(gw));
            const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
            while (std::chrono::steady_clock::now() < until)
            {
                if (!WaitReadable(s, 50))
                    continue;
                uint8_t buf[64];
                sockaddr_storage from{};
                int fromLen = sizeof(from);
                const int n = RealRecvFrom(s, buf, sizeof(buf), reinterpret_cast<sockaddr*>(&from), &fromLen);
                Ipv4Endpoint sender;
                if (n <= 0 || !FromSockaddr(reinterpret_cast<sockaddr*>(&from), fromLen, sender) || sender.ip != gateway)
                    continue;
                auto r = upnp::ParseNatPmpResponse(buf, static_cast<size_t>(n));
                if (r && r->opcode == expectOpcode)
                    return r;
            }
        }
        return std::nullopt;
    };

    const auto mapped = exchange(upnp::BuildNatPmpMapUdpRequest(port, port, lifetime), 129);
    std::optional<upnp::NatPmpResponse> external;
    if (mapped && mapped->result == 0 && lifetime > 0)
        external = exchange(upnp::BuildNatPmpExternalAddressRequest(), 128);
    closesocket(s);

    if (lifetime == 0)
        return mapped.has_value();
    if (!mapped || mapped->result != 0)
    {
        std::lock_guard lock(m_mutex);
        if (m_status.error.empty())
            m_status.error = "no UPnP/NAT-PMP gateway answered";
        return false;
    }
    std::lock_guard lock(m_mutex);
    m_status.active = true;
    m_status.method = "natpmp";
    m_status.externalPort = mapped->mappedPort;
    m_status.externalIp = external && external->result == 0 ? Ipv4ToString(external->externalIp) : "";
    m_status.error.clear();
    return true;
}

void PortMapper::RemoveMapping(uint16_t port)
{
    std::string controlUrl, serviceType, method;
    {
        std::lock_guard lock(m_mutex);
        controlUrl = m_upnpControlUrl;
        serviceType = m_upnpServiceType;
        method = m_status.method;
        m_status.active = false;
    }
    if (method == "upnp" && !controlUrl.empty())
    {
        std::string response;
        int status = 0;
        SoapCall(controlUrl, serviceType, "DeletePortMapping", upnp::BuildDeletePortMapping(serviceType, port, "UDP"),
                 response, status);
    }
    else if (method == "natpmp")
    {
        TryNatPmp(port, 0);
    }
}

void PortMapper::Run(uint16_t port)
{
    const uint32_t localIp = GetPrimaryLocalIpv4();
    if (!localIp)
    {
        std::lock_guard lock(m_mutex);
        m_status.error = "no IPv4 route";
        m_running.store(false);
        return;
    }
    if (!IsPrivateIpv4(localIp) && !IsTailscaleIpv4(localIp))
    {
        // The host has a public address itself; nothing to forward.
        std::lock_guard lock(m_mutex);
        m_status.active = true;
        m_status.method = "public";
        m_status.externalIp = Ipv4ToString(localIp);
        m_status.externalPort = port;
        m_running.store(false);
        return;
    }

    auto nextAttempt = std::chrono::steady_clock::now();
    int failures = 0;
    bool announced = false;
    while (m_running.load())
    {
        if (std::chrono::steady_clock::now() >= nextAttempt)
        {
            const bool ok = TryUpnp(port, localIp) || TryNatPmp(port, kLeaseSeconds);
            const auto status = Status();
            if (ok)
            {
                if (!announced)
                    Log("UDP port %u forwarded via %s (external %s:%u)\n", port, status.method.c_str(),
                        status.externalIp.c_str(), status.externalPort);
                announced = true;
                failures = 0;
                nextAttempt = std::chrono::steady_clock::now() + std::chrono::seconds(kLeaseSeconds / 2);
            }
            else
            {
                if (failures == 0)
                    Log("Could not forward UDP port %u automatically: %s\n", port, status.error.c_str());
                ++failures;
                // Routers rarely appear later; retry slowly.
                nextAttempt = std::chrono::steady_clock::now() + std::chrono::minutes(failures < 3 ? 2 : 15);
            }
        }
        Sleep(500);
    }
    RemoveMapping(port);
}

} // namespace p2p
