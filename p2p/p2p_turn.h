#pragma once

// TURN (RFC 5766) client used by game servers that are not directly
// reachable. The server allocates a relayed address on a TURN server
// (Cloudflare TURN, credentials minted by the master server) and advertises
// it; plain game clients send their normal UDP traffic to that address and
// never know a relay is involved. Only the server side speaks TURN.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "p2p_mux.h"
#include "stun.h"

namespace p2p
{

struct TurnCredentials
{
    std::string host;
    uint16_t port = 3478;
    std::string username;
    std::string password;

    bool operator==(const TurnCredentials& o) const
    {
        return host == o.host && port == o.port && username == o.username && password == o.password;
    }
    bool operator!=(const TurnCredentials& o) const { return !(*this == o); }
};

// Parses "turn:host[:port][?transport=udp]". Returns false for TCP/TLS URLs.
bool ParseTurnUrl(const std::string& url, std::string& host, uint16_t& port);

class TurnClient final : public OverlayBackend
{
public:
    // Starts (or restarts, when credentials changed) the allocation thread.
    void Start(const TurnCredentials& creds);
    void Stop();
    bool Running() const { return m_running.load(); }

    // Installs a permission so the given IPv4 address may send to us.
    void Permit(uint32_t ip);

    std::optional<Ipv4Endpoint> Relayed() const;
    std::string Describe() const;

    // OverlayBackend
    Backend Kind() const override { return Backend::Turn; }
    bool UsesFraming() const override { return false; }
    bool Send(uint64_t handle, const uint8_t* data, size_t size) override;
    size_t MaxDatagram(uint64_t) override { return 1400; }
    void Pump(const BackendSink& sink) override;
    void Close(uint64_t) override {}

    static uint64_t HandleFor(const Ipv4Endpoint& peer) { return (static_cast<uint64_t>(peer.ip) << 16) | peer.port; }
    static Ipv4Endpoint PeerFor(uint64_t handle)
    {
        return Ipv4Endpoint{ static_cast<uint32_t>(handle >> 16), static_cast<uint16_t>(handle & 0xFFFF) };
    }

private:
    enum class State
    {
        Idle,
        Resolving,
        Allocating,
        Allocated,
        Failed,
    };

    struct Pending
    {
        uint16_t method = 0;
        std::vector<uint8_t> bytes;
        std::chrono::steady_clock::time_point sentAt;
        int attempts = 0;
        bool authenticated = false;
        uint32_t lifetime = 0;
        uint32_t permitIp = 0;
        Ipv4Endpoint channelPeer;
        uint16_t channel = 0;
    };

    struct Permission
    {
        std::chrono::steady_clock::time_point refreshedAt{};
        bool confirmed = false;
    };

    struct Channel
    {
        uint16_t number = 0;
        bool bound = false;
        std::chrono::steady_clock::time_point refreshedAt{};
    };

    void Run(TurnCredentials creds, uint64_t generation);
    void HandleDatagram(const uint8_t* data, size_t size);
    void HandleResponse(const stun::Message& msg);
    stun::TxId SendRequest(uint16_t method, Pending pending);
    std::vector<uint8_t> BuildAuthenticated(uint16_t method, const stun::TxId& tx, const Pending& pending);
    void SendAllocate();
    void SendRefresh(uint32_t lifetime);
    void SendPermissions(const std::vector<uint32_t>& ips);
    void SendChannelBind(const Ipv4Endpoint& peer, uint16_t channel);
    void Tick();
    void MarkFailed(const std::string& why);
    stun::TxId NewTx();
    void RawSend(const std::vector<uint8_t>& bytes);

    mutable std::mutex m_mutex; // guards everything below except the atomics
    std::thread m_thread;
    std::atomic<bool> m_running{ false };
    std::atomic<uint64_t> m_generation{ 0 };
    TurnCredentials m_creds;
    // Rotated credentials from the master server; applied on the next
    // allocation so live relayed connections are not cut.
    std::optional<TurnCredentials> m_pendingCreds;
    SOCKET m_socket = INVALID_SOCKET;
    Ipv4Endpoint m_server;
    State m_state = State::Idle;
    std::string m_realm;
    std::string m_nonce;
    std::vector<uint8_t> m_key;
    Ipv4Endpoint m_relayed;
    Ipv4Endpoint m_mapped;
    uint32_t m_lifetime = 600;
    std::chrono::steady_clock::time_point m_allocatedAt{};
    int m_allocateFailures = 0;
    std::chrono::steady_clock::time_point m_lastRetry{};
    std::string m_lastError;
    std::map<stun::TxId, Pending> m_pending;
    std::map<uint32_t, Permission> m_permissions;
    std::set<uint32_t> m_wantedPermissions;
    std::map<uint64_t, Channel> m_channels; // by peer handle
    std::map<uint16_t, uint64_t> m_channelPeers;
    uint16_t m_nextChannel = stun::kMinChannel;
    std::deque<std::pair<Ipv4Endpoint, std::vector<uint8_t>>> m_incoming;
};

TurnClient& Turn();

} // namespace p2p
