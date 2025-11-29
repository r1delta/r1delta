#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "eos_p2p.h"
#include "eos_sdk.h"

namespace eos
{

struct FakeEndpoint
{
    in6_addr address{};
    uint16_t port = 0; // host order (encodes ProductUserId bytes)
};

enum class PacketRoute : uint8_t
{
    None = 0,
    Client = 1 << 0,
    Server = 1 << 1,
    All = static_cast<uint8_t>(Client) | static_cast<uint8_t>(Server)
};

constexpr PacketRoute operator|(PacketRoute lhs, PacketRoute rhs)
{
    return static_cast<PacketRoute>(static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
}

constexpr PacketRoute operator&(PacketRoute lhs, PacketRoute rhs)
{
    return static_cast<PacketRoute>(static_cast<uint8_t>(lhs) & static_cast<uint8_t>(rhs));
}

inline bool AnyRoute(PacketRoute route)
{
    return static_cast<uint8_t>(route) != 0;
}

struct PendingPacket
{
    FakeEndpoint sender;
    std::vector<uint8_t> payload;
    PacketRoute route = PacketRoute::All;
};

class FakeIpLayer
{
public:
    FakeIpLayer(EOS_HP2P p2pHandle, EOS_ProductUserId localUser);

    FakeEndpoint RegisterPeer(EOS_ProductUserId remoteUser,
                              const char* socketName,
                              uint8_t channel);

    bool SendToPeer(const FakeEndpoint& endpoint,
                    const uint8_t* data,
                    size_t length,
                    PacketRoute route = PacketRoute::All);

    void PumpIncoming();
    bool PopPacket(PacketRoute desiredRoute, PendingPacket& outPacket);
    bool PopPacket(PendingPacket& outPacket) { return PopPacket(PacketRoute::All, outPacket); }

    void SetLocalUser(EOS_ProductUserId localUser);
    void Clear();
    FakeEndpoint GetLocalEndpoint() const { return m_localEndpoint; }

private:
    struct PeerBinding
    {
        EOS_ProductUserId remoteUser = nullptr;
        EOS_P2P_SocketId socketId{};
        uint8_t channel = 0;
        FakeEndpoint endpoint{};
        std::string productUserId;
        PacketRoute route = PacketRoute::All;
    };

    bool NormalizeProductId(const std::string& productId, std::string& normalized) const;
    bool EncodeProductId(const std::string& productId, FakeEndpoint& endpoint) const;
    bool DecodeProductId(const FakeEndpoint& endpoint, std::string& productId) const;
    bool GetProductIdString(EOS_ProductUserId user, std::string& outString) const;
    void UpdateLocalEndpoint();

    bool ResolveEndpoint(EOS_ProductUserId remoteUser,
                         const char* socketName,
                         uint8_t channel,
                         FakeEndpoint& outEndpoint,
                         PacketRoute* outRoute = nullptr) const;

    EOS_HP2P m_p2pHandle = nullptr;
    std::atomic<EOS_ProductUserId> m_localUser;

    mutable std::mutex m_bindingsMutex;
    mutable std::unordered_map<std::string, PeerBinding> m_peerBindings;

    std::mutex m_queueMutex;
    std::deque<PendingPacket> m_packets;
    FakeEndpoint m_localEndpoint{};
};

uint16_t GetConfiguredHostPort();
uint16_t GetConfiguredClientPort();
bool IsServerNetContext();
uint16_t GetPretendRemotePort();

} // namespace eos
