#pragma once

// Asks the local router to forward the game server's UDP port, first through
// UPnP IGD, then NAT-PMP. Runs on its own thread and renews the lease.

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace p2p
{

struct PortMappingStatus
{
    bool active = false;
    std::string method;     // "upnp", "natpmp", "public" (no NAT) or ""
    std::string externalIp; // as reported by the router, may be empty
    uint16_t externalPort = 0;
    std::string error;
};

class PortMapper
{
public:
    void Start(uint16_t port);
    void Stop();
    PortMappingStatus Status() const;

private:
    void Run(uint16_t port);
    bool TryUpnp(uint16_t port, uint32_t localIp);
    bool TryNatPmp(uint16_t port, uint32_t lifetime);
    void RemoveMapping(uint16_t port);

    mutable std::mutex m_mutex;
    std::thread m_thread;
    std::atomic<bool> m_running{ false };
    uint16_t m_port = 0;
    PortMappingStatus m_status;
    std::string m_upnpControlUrl;
    std::string m_upnpServiceType;
};

PortMapper& PortMap();

} // namespace p2p
