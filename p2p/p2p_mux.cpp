#include "p2p_mux.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <unordered_map>

namespace p2p
{
namespace
{

constexpr size_t kMaxQueuedPerRoute = 4096;
constexpr uint16_t kIncomingPeerPort = 37005;
// Overlay connections keep themselves alive (QUIC / WireGuard keep-alives),
// so peers that stopped carrying game traffic are closed explicitly.
constexpr auto kPeerIdleTimeout = std::chrono::seconds(120);
constexpr auto kReapInterval = std::chrono::seconds(5);

std::atomic<SendToFn> g_realSendTo{ nullptr };
std::atomic<RecvFromFn> g_realRecvFrom{ nullptr };

std::atomic<SOCKET> g_engineSockets[3] = { INVALID_SOCKET, INVALID_SOCKET, INVALID_SOCKET };

struct ModuleRange
{
    std::atomic<uintptr_t> begin{ 0 };
    std::atomic<uintptr_t> end{ 0 };
};
std::array<ModuleRange, 8> g_foreignModules;
std::atomic<size_t> g_foreignCount{ 0 };

struct Peer
{
    uint64_t id = 0;
    OverlayBackend* backend = nullptr;
    uint64_t handle = 0;
    Route route = Route::Unknown;
    uint16_t port = 0;
    uint16_t nextMessageId = 1;
    Reassembler reassembler;
    std::chrono::steady_clock::time_point lastActivity = std::chrono::steady_clock::now();
};

struct Queued
{
    sockaddr_storage from{};
    int fromLen = 0;
    std::vector<uint8_t> data;
};

std::mutex g_mutex; // guards everything below
std::unordered_map<uint64_t, Peer> g_peers;
std::map<std::pair<Backend, uint64_t>, uint64_t> g_byHandle;
std::deque<Queued> g_queues[3];
std::vector<OverlayBackend*> g_backends;
uint64_t g_nextPeerId = 1;

std::mutex g_pumpMutex;
std::chrono::steady_clock::time_point g_lastReap{};

size_t RouteIndex(Route route)
{
    return static_cast<size_t>(route) < 3 ? static_cast<size_t>(route) : 0;
}

OverlayBackend* FindBackendLocked(Backend kind)
{
    for (auto* b : g_backends)
        if (b->Kind() == kind)
            return b;
    return nullptr;
}

sockaddr_in6 MakeFakeSockaddr(Backend backend, uint64_t id, uint16_t port)
{
    sockaddr_in6 sa{};
    sa.sin6_family = AF_INET6;
    sa.sin6_port = htons(port);
    const Ipv6Bytes bytes = EncodeOverlayAddress(backend, id);
    std::memcpy(&sa.sin6_addr, bytes.data(), bytes.size());
    return sa;
}

void PushLocked(Route route, const sockaddr* from, int fromLen, const uint8_t* data, size_t size)
{
    auto& q = g_queues[RouteIndex(route)];
    if (q.size() >= kMaxQueuedPerRoute)
        q.pop_front();
    Queued item;
    const int copyLen = std::min<int>(fromLen, static_cast<int>(sizeof(item.from)));
    if (from && copyLen > 0)
        std::memcpy(&item.from, from, copyLen);
    item.fromLen = copyLen;
    item.data.assign(data, data + size);
    q.push_back(std::move(item));
}

// Writes `src` into the caller's sockaddr buffer, degrading an IPv6 address
// to 0.0.0.0 when the caller only provided room for sockaddr_in (same
// behaviour as the EOS path).
void WriteFrom(const sockaddr_storage& src, int srcLen, sockaddr* out, int* outLen)
{
    if (!out || !outLen)
        return;
    if (*outLen >= srcLen)
    {
        std::memcpy(out, &src, srcLen);
        *outLen = srcLen;
        return;
    }
    if (*outLen >= static_cast<int>(sizeof(sockaddr_in)))
    {
        sockaddr_in v4{};
        v4.sin_family = AF_INET;
        if (src.ss_family == AF_INET6)
            v4.sin_port = reinterpret_cast<const sockaddr_in6*>(&src)->sin6_port;
        std::memcpy(out, &v4, sizeof(v4));
        *outLen = sizeof(v4);
    }
}

} // namespace

const char* RouteName(Route route)
{
    switch (route)
    {
    case Route::Client: return "client";
    case Route::Server: return "server";
    default: return "unknown";
    }
}

// ---------------------------------------------------------------------------
// Raw sockets
// ---------------------------------------------------------------------------
void SetRealSocketFunctions(SendToFn sendTo, RecvFromFn recvFrom)
{
    g_realSendTo.store(sendTo);
    g_realRecvFrom.store(recvFrom);
}

int RealSendTo(SOCKET s, const void* data, int len, const sockaddr* to, int tolen)
{
    SendToFn fn = g_realSendTo.load();
    if (!fn)
        return ::sendto(s, static_cast<const char*>(data), len, 0, to, tolen);
    return fn(s, static_cast<const char*>(data), len, 0, to, tolen);
}

int RealRecvFrom(SOCKET s, void* buf, int len, sockaddr* from, int* fromlen)
{
    RecvFromFn fn = g_realRecvFrom.load();
    if (!fn)
        return ::recvfrom(s, static_cast<char*>(buf), len, 0, from, fromlen);
    return fn(s, static_cast<char*>(buf), len, 0, from, fromlen);
}

int RealSendTo(SOCKET s, const std::vector<uint8_t>& data, const Ipv4Endpoint& to)
{
    if (s == INVALID_SOCKET || !to.Valid())
        return SOCKET_ERROR;
    const sockaddr_in sa = ToSockaddr(to);
    return RealSendTo(s, data.data(), static_cast<int>(data.size()), reinterpret_cast<const sockaddr*>(&sa), sizeof(sa));
}

sockaddr_in ToSockaddr(const Ipv4Endpoint& ep)
{
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(ep.port);
    sa.sin_addr.s_addr = htonl(ep.ip);
    return sa;
}

bool FromSockaddr(const sockaddr* sa, int len, Ipv4Endpoint& out)
{
    if (!sa)
        return false;
    if (sa->sa_family == AF_INET && len >= static_cast<int>(sizeof(sockaddr_in)))
    {
        const auto* v4 = reinterpret_cast<const sockaddr_in*>(sa);
        out.ip = ntohl(v4->sin_addr.s_addr);
        out.port = ntohs(v4->sin_port);
        return true;
    }
    if (sa->sa_family == AF_INET6 && len >= static_cast<int>(sizeof(sockaddr_in6)))
    {
        // IPv4-mapped IPv6 (::ffff:a.b.c.d) from dual-stack sockets.
        const auto* v6 = reinterpret_cast<const sockaddr_in6*>(sa);
        const uint8_t* b = reinterpret_cast<const uint8_t*>(&v6->sin6_addr);
        static const uint8_t kMapped[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF };
        if (std::memcmp(b, kMapped, sizeof(kMapped)) != 0)
            return false;
        out.ip = (static_cast<uint32_t>(b[12]) << 24) | (static_cast<uint32_t>(b[13]) << 16) |
                 (static_cast<uint32_t>(b[14]) << 8) | b[15];
        out.port = ntohs(v6->sin6_port);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Engine sockets / foreign modules
// ---------------------------------------------------------------------------
void NoteEngineSocket(SOCKET s, Route route)
{
    if (route == Route::Unknown || s == INVALID_SOCKET)
        return;
    g_engineSockets[RouteIndex(route)].store(s, std::memory_order_relaxed);
}

void ForgetSocket(SOCKET s)
{
    for (auto& slot : g_engineSockets)
    {
        SOCKET expected = s;
        slot.compare_exchange_strong(expected, INVALID_SOCKET);
    }
}

SOCKET GetEngineSocket(Route route)
{
    return g_engineSockets[RouteIndex(route)].load(std::memory_order_relaxed);
}

void RegisterForeignModule(HMODULE module)
{
    if (!module)
        return;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const uint8_t*>(module) + dos->e_lfanew);
    const uintptr_t begin = reinterpret_cast<uintptr_t>(module);
    const uintptr_t end = begin + nt->OptionalHeader.SizeOfImage;

    const size_t count = g_foreignCount.load();
    for (size_t i = 0; i < count; ++i)
        if (g_foreignModules[i].begin.load() == begin)
            return;
    const size_t slot = g_foreignCount.fetch_add(1);
    if (slot >= g_foreignModules.size())
    {
        g_foreignCount.store(g_foreignModules.size());
        return;
    }
    g_foreignModules[slot].end.store(end);
    g_foreignModules[slot].begin.store(begin);
}

bool IsForeignCaller(const void* returnAddress)
{
    const uintptr_t addr = reinterpret_cast<uintptr_t>(returnAddress);
    const size_t count = std::min(g_foreignCount.load(std::memory_order_relaxed), g_foreignModules.size());
    for (size_t i = 0; i < count; ++i)
    {
        const uintptr_t begin = g_foreignModules[i].begin.load(std::memory_order_relaxed);
        if (begin && addr >= begin && addr < g_foreignModules[i].end.load(std::memory_order_relaxed))
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Peers
// ---------------------------------------------------------------------------
void RegisterBackend(OverlayBackend* backend)
{
    std::lock_guard lock(g_mutex);
    if (std::find(g_backends.begin(), g_backends.end(), backend) == g_backends.end())
        g_backends.push_back(backend);
}

uint64_t AddOutgoingPeer(Backend backend, uint64_t handle, uint16_t port)
{
    std::lock_guard lock(g_mutex);
    OverlayBackend* impl = FindBackendLocked(backend);
    if (!impl)
        return 0;
    auto existing = g_byHandle.find({ backend, handle });
    if (existing != g_byHandle.end())
        return existing->second;
    Peer peer;
    peer.id = g_nextPeerId++;
    peer.backend = impl;
    peer.handle = handle;
    peer.route = Route::Client;
    peer.port = port;
    g_byHandle[{ backend, handle }] = peer.id;
    const uint64_t id = peer.id;
    g_peers.emplace(id, std::move(peer));
    return id;
}

void ClosePeer(uint64_t muxId)
{
    OverlayBackend* backend = nullptr;
    uint64_t handle = 0;
    {
        std::lock_guard lock(g_mutex);
        auto it = g_peers.find(muxId);
        if (it == g_peers.end())
            return;
        backend = it->second.backend;
        handle = it->second.handle;
        g_byHandle.erase({ backend->Kind(), handle });
        g_peers.erase(it);
    }
    backend->Close(handle);
}

bool PeerAddress(uint64_t muxId, Ipv6Bytes& addr, uint16_t& port)
{
    std::lock_guard lock(g_mutex);
    auto it = g_peers.find(muxId);
    if (it == g_peers.end())
        return false;
    addr = EncodeOverlayAddress(it->second.backend->Kind(), muxId);
    port = it->second.port;
    return true;
}

size_t PeerCount()
{
    std::lock_guard lock(g_mutex);
    return g_peers.size();
}

bool SendToPeer(uint64_t muxId, const uint8_t* data, size_t size)
{
    OverlayBackend* backend = nullptr;
    uint64_t handle = 0;
    uint16_t messageId = 0;
    {
        std::lock_guard lock(g_mutex);
        auto it = g_peers.find(muxId);
        if (it == g_peers.end())
            return false;
        backend = it->second.backend;
        handle = it->second.handle;
        it->second.lastActivity = std::chrono::steady_clock::now();
        if (backend->UsesFraming())
        {
            messageId = it->second.nextMessageId;
            // Reserve enough ids for this datagram's fragments.
            it->second.nextMessageId = static_cast<uint16_t>(it->second.nextMessageId + 1);
        }
    }

    if (!backend->UsesFraming())
        return backend->Send(handle, data, size);

    size_t mtu = backend->MaxDatagram(handle);
    if (mtu < 256)
        mtu = 1024;
    auto frames = FrameDatagram(data, size, mtu, messageId);
    if (frames.empty())
        return false;
    bool ok = true;
    for (const auto& f : frames)
        ok &= backend->Send(handle, f.data(), f.size());
    return ok;
}

// ---------------------------------------------------------------------------
// Control
// ---------------------------------------------------------------------------
bool TryConsumeControl(const uint8_t* data, size_t size, const ControlContext& ctx)
{
    ParsedControl pkt;
    if (!ParseControl(data, size, pkt))
        return false;
    HandleControl(pkt, ctx);
    return true;
}

// ---------------------------------------------------------------------------
// Hook entry points
// ---------------------------------------------------------------------------
bool HandleSendTo(const char* buf, int len, const sockaddr* to, int tolen, int* result)
{
    if (!to || to->sa_family != AF_INET6 || tolen < static_cast<int>(sizeof(sockaddr_in6)))
        return false;
    const auto* v6 = reinterpret_cast<const sockaddr_in6*>(to);
    Ipv6Bytes addr;
    std::memcpy(addr.data(), &v6->sin6_addr, addr.size());
    if (!IsOverlayAddress(addr.data()))
        return false;

    Backend backend;
    uint64_t id;
    if (!DecodeOverlayAddress(addr, backend, id) || !buf || len <= 0)
    {
        WSASetLastError(WSAEHOSTUNREACH);
        *result = SOCKET_ERROR;
        return true;
    }
    if (!SendToPeer(id, reinterpret_cast<const uint8_t*>(buf), static_cast<size_t>(len)))
    {
        WSASetLastError(WSAEHOSTUNREACH);
        *result = SOCKET_ERROR;
        return true;
    }
    *result = len;
    return true;
}

void InjectForEngine(Route route, const sockaddr* from, int fromlen, const uint8_t* data, size_t size)
{
    if (route == Route::Unknown)
        return;
    std::lock_guard lock(g_mutex);
    PushLocked(route, from, fromlen, data, size);
}

void PumpBackends()
{
    std::unique_lock pumpLock(g_pumpMutex, std::try_to_lock);
    if (!pumpLock.owns_lock())
        return; // another thread is already draining the backends

    std::vector<OverlayBackend*> backends;
    {
        std::lock_guard lock(g_mutex);
        backends = g_backends;
    }

    for (OverlayBackend* backend : backends)
    {
        const Backend kind = backend->Kind();
        backend->Pump([&](uint64_t handle, uint32_t flags, const uint8_t* data, size_t size) {
            uint64_t muxId = 0;
            Route route = Route::Unknown;
            std::vector<uint8_t> payload;
            {
                std::lock_guard lock(g_mutex);
                auto found = g_byHandle.find({ kind, handle });
                if (flags & kPeerClosed)
                {
                    if (found != g_byHandle.end())
                    {
                        g_peers.erase(found->second);
                        g_byHandle.erase(found);
                    }
                    return;
                }
                if (found == g_byHandle.end())
                {
                    if (!(flags & kPeerIncoming))
                        return; // stale outgoing peer we already closed
                    Peer peer;
                    peer.id = g_nextPeerId++;
                    peer.backend = backend;
                    peer.handle = handle;
                    peer.route = Route::Server;
                    peer.port = kIncomingPeerPort;
                    found = g_byHandle.emplace(std::make_pair(kind, handle), peer.id).first;
                    g_peers.emplace(peer.id, std::move(peer));
                }
                Peer& peer = g_peers[found->second];
                peer.lastActivity = std::chrono::steady_clock::now();
                muxId = peer.id;
                route = peer.route;
                if (backend->UsesFraming())
                {
                    if (!peer.reassembler.Push(data, size, payload))
                        return;
                }
                else
                {
                    payload.assign(data, data + size);
                }
            }

            ControlContext ctx;
            ctx.route = route;
            ctx.via = BackendName(kind);
            ctx.reply = [muxId](const std::vector<uint8_t>& reply) { SendToPeer(muxId, reply.data(), reply.size()); };
            if (TryConsumeControl(payload.data(), payload.size(), ctx))
                return;

            std::lock_guard lock(g_mutex);
            auto it = g_peers.find(muxId);
            if (it == g_peers.end())
                return;
            const sockaddr_in6 from = MakeFakeSockaddr(kind, muxId, it->second.port);
            PushLocked(route, reinterpret_cast<const sockaddr*>(&from), sizeof(from), payload.data(), payload.size());
        });
    }

    const auto now = std::chrono::steady_clock::now();
    if (now - g_lastReap < kReapInterval)
        return;
    g_lastReap = now;
    std::vector<uint64_t> idle;
    {
        std::lock_guard lock(g_mutex);
        for (const auto& [id, peer] : g_peers)
            if (now - peer.lastActivity > kPeerIdleTimeout)
                idle.push_back(id);
    }
    for (uint64_t id : idle)
        ClosePeer(id);
}

bool PopQueued(Route route, char* buf, int len, sockaddr* from, int* fromlen, int* result)
{
    if (route == Route::Unknown)
        return false;
    auto& q = g_queues[RouteIndex(route)];
    {
        std::lock_guard lock(g_mutex);
        if (q.empty() && g_backends.empty())
            return false;
    }

    auto tryPop = [&]() -> bool {
        std::lock_guard lock(g_mutex);
        if (q.empty())
            return false;
        Queued item = std::move(q.front());
        q.pop_front();
        const int copy = std::min<int>(len, static_cast<int>(item.data.size()));
        if (buf && copy > 0)
            std::memcpy(buf, item.data.data(), copy);
        WriteFrom(item.from, item.fromLen, from, fromlen);
        *result = copy;
        return true;
    };

    if (tryPop())
        return true;
    PumpBackends();
    return tryPop();
}

bool ConsumeUdpControl(SOCKET s, Route route, const uint8_t* data, int len, const sockaddr* from, int fromlen)
{
    if (len < static_cast<int>(kNatHeaderSize) || !data || data[0] != 0xFF)
        return false;
    ParsedControl pkt;
    if (!ParseControl(data, static_cast<size_t>(len), pkt))
        return false;

    ControlContext ctx;
    ctx.route = route;
    ctx.via = "udp";
    ctx.viaEngineUdp = true;
    FromSockaddr(from, fromlen, ctx.udpFrom);

    sockaddr_storage replyTo{};
    const int replyLen = std::min<int>(fromlen, static_cast<int>(sizeof(replyTo)));
    if (from && replyLen > 0)
        std::memcpy(&replyTo, from, replyLen);
    ctx.reply = [s, replyTo, replyLen](const std::vector<uint8_t>& reply) {
        RealSendTo(s, reply.data(), static_cast<int>(reply.size()), reinterpret_cast<const sockaddr*>(&replyTo), replyLen);
    };
    HandleControl(pkt, ctx);
    return true;
}

void PollEngineSocket(Route route)
{
    const SOCKET s = GetEngineSocket(route);
    if (s == INVALID_SOCKET)
        return;
    std::vector<uint8_t> buf(65536);
    for (int i = 0; i < 256; ++i)
    {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(s, &readSet);
        timeval tv{ 0, 0 };
        if (select(0, &readSet, nullptr, nullptr, &tv) <= 0)
            return;
        sockaddr_storage from{};
        int fromLen = sizeof(from);
        const int n = RealRecvFrom(s, buf.data(), static_cast<int>(buf.size()), reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n <= 0)
            return;
        if (ConsumeUdpControl(s, route, buf.data(), n, reinterpret_cast<sockaddr*>(&from), fromLen))
            continue;
        InjectForEngine(route, reinterpret_cast<sockaddr*>(&from), fromLen, buf.data(), static_cast<size_t>(n));
    }
}

} // namespace p2p
