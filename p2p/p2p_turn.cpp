#include "p2p_turn.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <random>

#include "p2p_log.h"

namespace p2p
{
namespace
{

using Clock = std::chrono::steady_clock;

constexpr auto kRetransmitAfter = std::chrono::milliseconds(500);
constexpr int kMaxAttempts = 6;
constexpr auto kPermissionRefresh = std::chrono::seconds(240); // permissions live 300s
constexpr auto kChannelRefresh = std::chrono::seconds(480);    // channels live 600s
constexpr size_t kMaxIncoming = 4096;
constexpr auto kPermissionUnusedExpiry = std::chrono::minutes(10);
constexpr auto kPermissionRetry = std::chrono::seconds(5);
constexpr size_t kMaxWantedPermissions = 1024;
constexpr const char* kSoftware = "R1Delta";

std::string Trim(const std::string& s)
{
    const size_t b = s.find_first_not_of(" \t\r\n");
    const size_t e = s.find_last_not_of(" \t\r\n");
    return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

} // namespace

bool ParseTurnUrl(const std::string& urlIn, std::string& host, uint16_t& port)
{
    std::string url = Trim(urlIn);
    if (url.rfind("turn:", 0) != 0)
        return false; // turns: (TLS) is not supported by the game
    url = url.substr(5);
    std::string query;
    const size_t q = url.find('?');
    if (q != std::string::npos)
    {
        query = url.substr(q + 1);
        url = url.substr(0, q);
    }
    if (!query.empty() && query.find("transport=udp") == std::string::npos)
        return false;
    port = 3478;
    const size_t colon = url.rfind(':');
    if (colon != std::string::npos && url.find(']') == std::string::npos)
    {
        const std::string portText = url.substr(colon + 1);
        char* end = nullptr;
        const long p = std::strtol(portText.c_str(), &end, 10);
        if (!end || *end != '\0' || p <= 0 || p > 0xFFFF)
            return false;
        port = static_cast<uint16_t>(p);
        url = url.substr(0, colon);
    }
    host = url;
    return !host.empty();
}

TurnClient& Turn()
{
    static TurnClient client;
    return client;
}

stun::TxId TurnClient::NewTx()
{
    static thread_local std::mt19937_64 rng{ std::random_device{}() };
    stun::TxId tx;
    for (auto& b : tx)
        b = static_cast<uint8_t>(rng());
    return tx;
}

void TurnClient::Start(const TurnCredentials& creds)
{
    {
        std::lock_guard lock(m_mutex);
        if (m_running.load() && m_creds == creds)
            return;
        if (m_running.load() && m_creds.host == creds.host && m_creds.port == creds.port)
        {
            // Same relay, new credentials: an allocation stays bound to the
            // username it was created with, so keep using the old ones until
            // it has to be recreated.
            m_pendingCreds = creds;
            return;
        }
    }
    Stop();
    std::lock_guard lock(m_mutex);
    m_creds = creds;
    m_running.store(true);
    const uint64_t generation = ++m_generation;
    m_thread = std::thread([this, creds, generation] { Run(creds, generation); });
    RegisterBackend(this);
}

void TurnClient::Stop()
{
    std::thread t;
    {
        std::lock_guard lock(m_mutex);
        m_running.store(false);
        // The thread may have exited on its own (DNS / socket failure) and
        // still be joinable; always reap it so a new one can be assigned.
        t = std::move(m_thread);
    }
    if (t.joinable())
        t.join();
}

TurnClient::~TurnClient()
{
    // Static destruction at process exit: never block or terminate here.
    m_running.store(false);
    if (m_thread.joinable())
        m_thread.detach();
}

void TurnClient::Permit(uint32_t ip)
{
    if (!ip)
        return;
    std::lock_guard lock(m_mutex);
    if (m_wantedPermissions.size() >= kMaxWantedPermissions && !m_wantedPermissions.count(ip))
        return;
    m_wantedPermissions[ip] = Clock::now();
}

std::optional<Ipv4Endpoint> TurnClient::Relayed() const
{
    std::lock_guard lock(m_mutex);
    if (m_state != State::Allocated || !m_relayed.Valid())
        return std::nullopt;
    return m_relayed;
}

std::string TurnClient::Describe() const
{
    std::lock_guard lock(m_mutex);
    switch (m_state)
    {
    case State::Idle: return "idle";
    case State::Resolving: return "resolving " + m_creds.host;
    case State::Allocating: return "allocating on " + m_server.ToString();
    case State::Allocated:
        return "relay " + m_relayed.ToString() + " (" + std::to_string(m_permissions.size()) + " permissions, " +
               std::to_string(m_channels.size()) + " channels)";
    case State::Failed: return "failed: " + m_lastError;
    }
    return "?";
}

void TurnClient::RawSend(const std::vector<uint8_t>& bytes)
{
    if (m_socket == INVALID_SOCKET || !m_server.Valid())
        return;
    const sockaddr_in sa = ToSockaddr(m_server);
    RealSendTo(m_socket, bytes.data(), static_cast<int>(bytes.size()), reinterpret_cast<const sockaddr*>(&sa), sizeof(sa));
}

std::vector<uint8_t> TurnClient::BuildAuthenticated(uint16_t method, const stun::TxId& tx, const Pending& pending)
{
    stun::MessageBuilder b(method, stun::kRequest, tx);
    switch (method)
    {
    case stun::kAllocate:
        b.AddRequestedTransportUdp();
        b.AddU32(stun::kLifetime, 600);
        break;
    case stun::kRefresh:
        b.AddU32(stun::kLifetime, pending.lifetime);
        break;
    case stun::kCreatePermission:
        b.AddXorAddress(stun::kXorPeerAddress, Ipv4Endpoint{ pending.permitIp, 0 });
        break;
    case stun::kChannelBind:
        b.AddChannelNumber(pending.channel);
        b.AddXorAddress(stun::kXorPeerAddress, pending.channelPeer);
        break;
    default:
        break;
    }
    b.AddString(stun::kSoftware, kSoftware);
    if (!m_realm.empty())
    {
        b.AddString(stun::kUsername, m_creds.username);
        b.AddString(stun::kRealm, m_realm);
        b.AddString(stun::kNonce, m_nonce);
        b.AddIntegrity(m_key);
    }
    b.AddFingerprint();
    return b.Bytes();
}

stun::TxId TurnClient::SendRequest(uint16_t method, Pending pending)
{
    const stun::TxId tx = NewTx();
    pending.method = method;
    pending.authenticated = !m_realm.empty();
    pending.bytes = BuildAuthenticated(method, tx, pending);
    pending.sentAt = Clock::now();
    pending.attempts = 1;
    RawSend(pending.bytes);
    m_pending[tx] = std::move(pending);
    return tx;
}

void TurnClient::SendAllocate()
{
    m_state = State::Allocating;
    SendRequest(stun::kAllocate, Pending{});
}

void TurnClient::SendRefresh(uint32_t lifetime)
{
    Pending p;
    p.lifetime = lifetime;
    SendRequest(stun::kRefresh, p);
}

void TurnClient::SendPermissions(const std::vector<uint32_t>& ips)
{
    for (uint32_t ip : ips)
    {
        Pending p;
        p.permitIp = ip;
        m_permissions[ip].requestedAt = Clock::now();
        SendRequest(stun::kCreatePermission, p);
    }
}

void TurnClient::SendChannelBind(const Ipv4Endpoint& peer, uint16_t channel)
{
    Pending p;
    p.channelPeer = peer;
    p.channel = channel;
    m_channels[HandleFor(peer)].refreshedAt = Clock::now();
    SendRequest(stun::kChannelBind, p);
}

void TurnClient::Run(TurnCredentials creds, uint64_t generation)
{
    {
        std::lock_guard lock(m_mutex);
        m_state = State::Resolving;
        m_realm.clear();
        m_nonce.clear();
        m_key.clear();
        m_relayed = {};
        m_pending.clear();
        m_permissions.clear();
        m_channels.clear();
        m_channelPeers.clear();
        m_nextChannel = stun::kMinChannel;
        m_allocateFailures = 0;
    }

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* result = nullptr;
    const std::string portText = std::to_string(creds.port);
    if (getaddrinfo(creds.host.c_str(), portText.c_str(), &hints, &result) != 0 || !result)
    {
        std::lock_guard lock(m_mutex);
        m_state = State::Failed;
        m_lastError = "cannot resolve " + creds.host;
        Log("TURN: %s\n", m_lastError.c_str());
        m_running.store(false);
        return;
    }
    Ipv4Endpoint server;
    FromSockaddr(result->ai_addr, static_cast<int>(result->ai_addrlen), server);
    freeaddrinfo(result);

    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET)
    {
        std::lock_guard lock(m_mutex);
        m_state = State::Failed;
        m_lastError = "socket() failed";
        m_running.store(false);
        return;
    }
    sockaddr_in local{};
    local.sin_family = AF_INET;
    bind(s, reinterpret_cast<const sockaddr*>(&local), sizeof(local));

    {
        std::lock_guard lock(m_mutex);
        m_socket = s;
        m_server = server;
        SendAllocate();
    }
    Log("TURN: allocating relay on %s (%s)\n", creds.host.c_str(), server.ToString().c_str());

    std::vector<uint8_t> buf(65536);
    while (m_running.load() && m_generation.load() == generation)
    {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(s, &readSet);
        timeval tv{ 0, 50 * 1000 };
        if (select(0, &readSet, nullptr, nullptr, &tv) > 0)
        {
            sockaddr_storage from{};
            int fromLen = sizeof(from);
            const int n = RealRecvFrom(s, buf.data(), static_cast<int>(buf.size()), reinterpret_cast<sockaddr*>(&from), &fromLen);
            Ipv4Endpoint sender;
            if (n > 0 && FromSockaddr(reinterpret_cast<sockaddr*>(&from), fromLen, sender) && sender == server)
            {
                std::lock_guard lock(m_mutex);
                HandleDatagram(buf.data(), static_cast<size_t>(n));
            }
        }
        std::lock_guard lock(m_mutex);
        Tick();
    }

    // Release the allocation so the relay stops forwarding (and billing).
    {
        std::lock_guard lock(m_mutex);
        if (m_state == State::Allocated)
            SendRefresh(0);
        m_state = State::Idle;
        m_relayed = {};
        m_socket = INVALID_SOCKET;
    }
    Sleep(100);
    closesocket(s);
    Log("TURN: relay released\n");
}

void TurnClient::Tick()
{
    const auto now = Clock::now();

    // Retransmissions.
    for (auto it = m_pending.begin(); it != m_pending.end();)
    {
        Pending& p = it->second;
        if (now - p.sentAt < kRetransmitAfter * p.attempts)
        {
            ++it;
            continue;
        }
        if (p.attempts >= kMaxAttempts)
        {
            const uint16_t method = p.method;
            const uint32_t lifetime = p.lifetime;
            it = m_pending.erase(it);
            if (method == stun::kAllocate)
                MarkFailed("allocate timed out");
            else if (method == stun::kRefresh && lifetime != 0)
                MarkFailed("refresh timed out");
            continue;
        }
        ++p.attempts;
        p.sentAt = now;
        RawSend(p.bytes);
        ++it;
    }

    if (m_state == State::Failed)
    {
        // Back off and retry the allocation.
        if (now - m_lastRetry > std::chrono::seconds(30 * std::min(m_allocateFailures, 4)))
        {
            m_lastRetry = now;
            m_pending.clear();
            m_realm.clear();
            m_nonce.clear();
            m_permissions.clear();
            m_channels.clear();
            m_channelPeers.clear();
            m_nextChannel = stun::kMinChannel;
            if (m_pendingCreds)
            {
                m_creds = *m_pendingCreds;
                m_pendingCreds.reset();
            }
            SendAllocate();
        }
        return;
    }
    if (m_state != State::Allocated)
        return;

    // Allocation refresh at half its lifetime.
    if (now - m_allocatedAt > std::chrono::seconds(m_lifetime / 2))
    {
        m_allocatedAt = now;
        SendRefresh(600);
    }

    // New and expiring permissions; forget IPs nobody asked for lately.
    std::vector<uint32_t> toPermit;
    for (auto it = m_wantedPermissions.begin(); it != m_wantedPermissions.end();)
    {
        if (now - it->second > kPermissionUnusedExpiry)
        {
            m_permissions.erase(it->first);
            it = m_wantedPermissions.erase(it);
            continue;
        }
        const Permission& perm = m_permissions[it->first];
        const bool fresh = perm.confirmedAt.time_since_epoch().count() != 0 && now - perm.confirmedAt < kPermissionRefresh;
        if (!fresh && now - perm.requestedAt > kPermissionRetry)
            toPermit.push_back(it->first);
        ++it;
    }
    if (!toPermit.empty())
        SendPermissions(toPermit);

    for (auto& [handle, channel] : m_channels)
    {
        if (channel.bound && now - channel.refreshedAt > kChannelRefresh)
            SendChannelBind(PeerFor(handle), channel.number);
    }
}

void TurnClient::MarkFailed(const std::string& why)
{
    if (m_state == State::Allocated)
        Log("TURN: relay %s lost (%s); reallocating\n", m_relayed.ToString().c_str(), why.c_str());
    else
        Log("TURN: %s\n", why.c_str());
    m_state = State::Failed;
    m_lastError = why;
    m_relayed = {};
    ++m_allocateFailures;
}

void TurnClient::HandleDatagram(const uint8_t* data, size_t size)
{
    uint16_t channel = 0;
    const uint8_t* payload = nullptr;
    size_t payloadSize = 0;
    if (stun::ParseChannelData(data, size, channel, payload, payloadSize))
    {
        auto it = m_channelPeers.find(channel);
        if (it == m_channelPeers.end())
            return;
        if (m_incoming.size() >= kMaxIncoming)
            m_incoming.pop_front();
        const Ipv4Endpoint peer = PeerFor(it->second);
        if (auto wanted = m_wantedPermissions.find(peer.ip); wanted != m_wantedPermissions.end())
            wanted->second = Clock::now();
        m_incoming.emplace_back(peer, std::vector<uint8_t>(payload, payload + payloadSize));
        return;
    }

    stun::Message msg;
    if (!msg.Parse(data, size))
        return;

    if (msg.GetMethod() == stun::kData && msg.GetClass() == stun::kIndication)
    {
        Ipv4Endpoint peer;
        const uint8_t* d = nullptr;
        size_t n = 0;
        if (!msg.GetXorAddress(stun::kXorPeerAddress, peer) || !msg.GetData(d, n))
            return;
        if (m_incoming.size() >= kMaxIncoming)
            m_incoming.pop_front();
        if (auto wanted = m_wantedPermissions.find(peer.ip); wanted != m_wantedPermissions.end())
            wanted->second = Clock::now();
        m_incoming.emplace_back(peer, std::vector<uint8_t>(d, d + n));

        // Bind a channel so subsequent traffic uses 4-byte ChannelData headers.
        const uint64_t handle = HandleFor(peer);
        if (m_channels.find(handle) == m_channels.end() && m_nextChannel <= stun::kMaxChannel)
        {
            Channel c;
            c.number = m_nextChannel++;
            m_channels[handle] = c;
            SendChannelBind(peer, c.number);
        }
        return;
    }

    if (msg.GetClass() == stun::kSuccess || msg.GetClass() == stun::kError)
        HandleResponse(msg);
}

void TurnClient::HandleResponse(const stun::Message& msg)
{
    auto it = m_pending.find(msg.GetTx());
    if (it == m_pending.end())
        return;
    Pending pending = std::move(it->second);
    m_pending.erase(it);

    if (msg.GetClass() == stun::kError)
    {
        int code = 0;
        std::string reason;
        msg.GetErrorCode(code, reason);
        std::string realm, nonce;
        const bool hasRealm = msg.GetString(stun::kRealm, realm);
        const bool hasNonce = msg.GetString(stun::kNonce, nonce);
        if ((code == 401 || code == 438) && hasNonce)
        {
            // 401 on first contact / 438 on nonce rotation: (re)authenticate.
            // A 401 for a request that was already authenticated with the
            // same realm means the credentials are bad.
            if (code == 401 && pending.authenticated && (!hasRealm || realm == m_realm))
            {
                // Expired or rejected credentials: the allocation is lost.
                if (pending.method == stun::kAllocate || pending.method == stun::kRefresh)
                    MarkFailed("TURN credentials rejected");
                return;
            }
            if (hasRealm)
                m_realm = realm;
            m_nonce = nonce;
            m_key = stun::LongTermKey(m_creds.username, m_realm, m_creds.password);
            SendRequest(pending.method, pending);
            return;
        }
        if (pending.method == stun::kAllocate || (pending.method == stun::kRefresh && pending.lifetime != 0))
        {
            // Includes 437 Allocation Mismatch after the relay forgot us.
            MarkFailed(std::string(pending.method == stun::kAllocate ? "allocate" : "refresh") + " error " +
                       std::to_string(code) + " " + reason);
        }
        else
        {
            Debug("TURN: request %u failed: %d %s\n", pending.method, code, reason.c_str());
        }
        return;
    }

    switch (pending.method)
    {
    case stun::kAllocate:
    {
        Ipv4Endpoint relayed;
        if (!msg.GetXorAddress(stun::kXorRelayedAddress, relayed))
            return;
        msg.GetXorAddress(stun::kXorMappedAddress, m_mapped);
        uint32_t lifetime = 600;
        msg.GetU32(stun::kLifetime, lifetime);
        m_lifetime = std::max<uint32_t>(lifetime, 120);
        m_relayed = relayed;
        m_allocatedAt = Clock::now();
        m_state = State::Allocated;
        m_allocateFailures = 0;
        Log("TURN: relay allocated at %s (lifetime %us)\n", relayed.ToString().c_str(), m_lifetime);
        break;
    }
    case stun::kRefresh:
        if (pending.lifetime != 0)
        {
            uint32_t lifetime = 600;
            if (msg.GetU32(stun::kLifetime, lifetime))
                m_lifetime = std::max<uint32_t>(lifetime, 120);
        }
        break;
    case stun::kCreatePermission:
        m_permissions[pending.permitIp].confirmedAt = Clock::now();
        break;
    case stun::kChannelBind:
    {
        const uint64_t handle = HandleFor(pending.channelPeer);
        auto& c = m_channels[handle];
        c.number = pending.channel;
        c.bound = true;
        m_channelPeers[pending.channel] = handle;
        break;
    }
    default:
        break;
    }
}

bool TurnClient::Send(uint64_t handle, const uint8_t* data, size_t size)
{
    std::lock_guard lock(m_mutex);
    if (m_state != State::Allocated)
        return false;
    const Ipv4Endpoint peer = PeerFor(handle);
    auto it = m_channels.find(handle);
    if (it != m_channels.end() && it->second.bound)
    {
        RawSend(stun::BuildChannelData(it->second.number, data, size));
        return true;
    }
    stun::MessageBuilder b(stun::kSend, stun::kIndication, NewTx());
    b.AddXorAddress(stun::kXorPeerAddress, peer);
    b.AddAttr(stun::kDataAttr, data, size);
    RawSend(b.Bytes());
    return true;
}

void TurnClient::Pump(const BackendSink& sink)
{
    std::deque<std::pair<Ipv4Endpoint, std::vector<uint8_t>>> items;
    {
        std::lock_guard lock(m_mutex);
        items.swap(m_incoming);
    }
    for (auto& [peer, data] : items)
        sink(HandleFor(peer), kPeerIncoming, data.data(), data.size());
}

} // namespace p2p
