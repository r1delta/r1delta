#pragma once

// Transport multiplexer: sits behind the Winsock sendto/recvfrom hooks
// (eos/eos_network.cpp) and routes engine datagrams addressed to fake
// 3ffd::/16 addresses through overlay backends (iroh, tailcat, TURN).
// It also filters R1NX control packets out of the engine's UDP traffic.

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "p2p_protocol.h"

namespace p2p
{

enum class Route : uint8_t
{
    Unknown = 0,
    Client = 1,
    Server = 2,
};

const char* RouteName(Route route);

// ---------------------------------------------------------------------------
// Raw socket access (bypassing our own hooks)
// ---------------------------------------------------------------------------
using SendToFn = int(WSAAPI*)(SOCKET, const char*, int, int, const sockaddr*, int);
using RecvFromFn = int(WSAAPI*)(SOCKET, char*, int, int, sockaddr*, int*);

void SetRealSocketFunctions(SendToFn sendTo, RecvFromFn recvFrom);
int RealSendTo(SOCKET s, const void* data, int len, const sockaddr* to, int tolen);
int RealRecvFrom(SOCKET s, void* buf, int len, sockaddr* from, int* fromlen);
int RealSendTo(SOCKET s, const std::vector<uint8_t>& data, const Ipv4Endpoint& to);

sockaddr_in ToSockaddr(const Ipv4Endpoint& ep);
bool FromSockaddr(const sockaddr* sa, int len, Ipv4Endpoint& out);

// ---------------------------------------------------------------------------
// Engine sockets
// ---------------------------------------------------------------------------
void NoteEngineSocket(SOCKET s, Route route);
void ForgetSocket(SOCKET s);
SOCKET GetEngineSocket(Route route); // INVALID_SOCKET if not seen yet

// Modules whose own Winsock calls must bypass the hooks entirely (EOS SDK,
// overlay plugins). Their sockets must never receive engine datagrams.
void RegisterForeignModule(HMODULE module);
bool IsForeignCaller(const void* returnAddress);

// ---------------------------------------------------------------------------
// Overlay backends
// ---------------------------------------------------------------------------
constexpr uint32_t kPeerIncoming = 0x1;
constexpr uint32_t kPeerClosed = 0x2;

using BackendSink = std::function<void(uint64_t handle, uint32_t flags, const uint8_t* data, size_t size)>;

class OverlayBackend
{
public:
    virtual ~OverlayBackend() = default;
    virtual Backend Kind() const = 0;
    // Plugin backends carry our fragmentation framing; TURN carries raw
    // engine datagrams to plain UDP clients.
    virtual bool UsesFraming() const = 0;
    virtual bool Send(uint64_t handle, const uint8_t* data, size_t size) = 0;
    virtual size_t MaxDatagram(uint64_t handle) = 0;
    virtual void Pump(const BackendSink& sink) = 0;
    virtual void Close(uint64_t handle) = 0;
};

// Backends live for the whole process; the mux does not own them.
void RegisterBackend(OverlayBackend* backend);

// Registers an outgoing (client-route) overlay connection; returns the mux
// peer id used in its fake address.
uint64_t AddOutgoingPeer(Backend backend, uint64_t handle, uint16_t port);
void ClosePeer(uint64_t muxId);
bool SendToPeer(uint64_t muxId, const uint8_t* data, size_t size);
bool PeerAddress(uint64_t muxId, Ipv6Bytes& addr, uint16_t& port);
size_t PeerCount();

// ---------------------------------------------------------------------------
// Control packets
// ---------------------------------------------------------------------------
struct ControlContext
{
    Route route = Route::Unknown;
    const char* via = "udp";  // transport the packet arrived on
    bool viaEngineUdp = false; // arrived on an engine UDP socket
    Ipv4Endpoint udpFrom;      // sender when viaEngineUdp && IPv4
    std::function<void(const std::vector<uint8_t>&)> reply;
};

// Implemented in p2p.cpp.
void HandleControl(const ParsedControl& pkt, const ControlContext& ctx);

// Parses data and, if it is a control packet, handles it. Returns true when
// the packet was consumed and must not reach the engine.
bool TryConsumeControl(const uint8_t* data, size_t size, const ControlContext& ctx);

// ---------------------------------------------------------------------------
// Hook entry points (called from eos/eos_network.cpp)
// ---------------------------------------------------------------------------

// sendto: handles datagrams addressed to 3ffd::/16. Returns true if handled.
bool HandleSendTo(const char* buf, int len, const sockaddr* to, int tolen, int* result);

// recvfrom: pops a datagram queued for the engine socket of `route`
// (overlay traffic, or packets read by PollEngineSocket).
bool PopQueued(Route route, char* buf, int len, sockaddr* from, int* fromlen, int* result);

// recvfrom: called with a datagram the engine socket really received.
// Returns true if it was a control packet (already handled).
bool ConsumeUdpControl(SOCKET s, Route route, const uint8_t* data, int len, const sockaddr* from, int fromlen);

// Queue a datagram for the engine as if it had arrived on its socket.
void InjectForEngine(Route route, const sockaddr* from, int fromlen, const uint8_t* data, size_t size);

// Pulls pending datagrams out of every backend.
void PumpBackends();

// Reads whatever is waiting on the engine socket of `route` (used while the
// client is probing and the engine may not be polling). Control packets are
// handled, everything else is re-queued for the engine.
void PollEngineSocket(Route route);

} // namespace p2p
