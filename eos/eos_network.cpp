#include "eos_network.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <intrin.h>

#include "MinHook.h"
#include "core.h"
#include "eos_layer.h"
#include "eos_threading.h"
#include "logging.h"
#include "r1d_version.h"
#include "p2p/p2p_connect.h"
#include "p2p/p2p_eos_bridge.h"
#include "p2p/p2p_identity.h"
#include "p2p/p2p_mux.h"

#pragma intrinsic(_ReturnAddress)

namespace
{

constexpr char kProductId[] = "f90ebc836e864395a3a3765916ead66a";
constexpr char kSandboxId[] = "2159ca4fcc1445b98cb4e706ba316415";
constexpr char kDeploymentId[] = "45505f63034c418eb057f6ba3065f99e";
constexpr char kProductName[] = "r1delta";
constexpr char kProductVersion[] = "1.18.1.2";

struct Version
{
    int major = 0;
    int minor = 0;
    int patch = 0;
};

bool ParseVersion(const char* versionStr, Version& out)
{
    if (!versionStr)
        return false;

    // Skip leading 'v' if present
    const char* ptr = versionStr;
    if (*ptr == 'v' || *ptr == 'V')
        ++ptr;

    char* end = nullptr;
    out.major = static_cast<int>(strtol(ptr, &end, 10));
    if (end == ptr || *end != '.')
        return false;

    ptr = end + 1;
    out.minor = static_cast<int>(strtol(ptr, &end, 10));
    if (end == ptr || *end != '.')
        return false;

    ptr = end + 1;
    out.patch = static_cast<int>(strtol(ptr, &end, 10));
    if (end == ptr)
        return false;

    return true;
}

bool IsVersionAtLeast(const Version& version, int major, int minor, int patch)
{
    if (version.major > major)
        return true;
    if (version.major < major)
        return false;
    if (version.minor > minor)
        return true;
    if (version.minor < minor)
        return false;
    return version.patch >= patch;
}

bool ShouldEnableEOS()
{
    const char* versionStr = R1D_VERSION;

    // Enable in dev builds
    if (strcmp(versionStr, "dev") == 0)
        return true;

    Version version{};
    if (!ParseVersion(versionStr, version))
        return false;

    // Enable for version >= 3.0.0
    return IsVersionAtLeast(version, 3, 0, 0);
}

using SendToFn = int (WSAAPI*)(SOCKET, const char*, int, int, const sockaddr*, int);
using RecvFromFn = int (WSAAPI*)(SOCKET, char*, int, int, sockaddr*, int*);
using CloseSocketFn = int (WSAAPI*)(SOCKET);

SendToFn g_realSendTo = nullptr;
RecvFromFn g_realRecvFrom = nullptr;
CloseSocketFn g_realCloseSocket = nullptr;
LPVOID g_sendToTarget = nullptr;
LPVOID g_recvFromTarget = nullptr;
LPVOID g_closeSocketTarget = nullptr;
bool g_hooksInstalled = false;
bool g_eosEnabled = false;
std::mutex g_socketRouteMutex;
std::unordered_map<SOCKET, eos::PacketRoute> g_socketRoutes;

bool ExtractFakeEndpoint(const sockaddr* address, eos::FakeEndpoint& outEndpoint)
{
    if (!address || address->sa_family != AF_INET6)
        return false;

    const auto* ipv6 = reinterpret_cast<const sockaddr_in6*>(address);
    if (ntohs(ipv6->sin6_addr.u.Word[0]) != 0x3FFE)
        return false;

    outEndpoint.address = ipv6->sin6_addr;
    outEndpoint.port = ntohs(ipv6->sin6_port);
    return true;
}

void WriteSockaddrForFakeEndpoint(const eos::FakeEndpoint& endpoint,
                                  sockaddr* outAddress,
                                  int* outLength)
{
    if (!outAddress || !outLength)
        return;

    uint16_t pretendPort = eos::GetPretendRemotePort();
    if (pretendPort == 0)
        pretendPort = endpoint.port;

    if (*outLength >= static_cast<int>(sizeof(sockaddr_in6)))
    {
        auto* ipv6 = reinterpret_cast<sockaddr_in6*>(outAddress);
        std::memset(ipv6, 0, sizeof(sockaddr_in6));
        ipv6->sin6_family = AF_INET6;
        ipv6->sin6_port = htons(pretendPort);
        ipv6->sin6_addr = endpoint.address;
        *outLength = sizeof(sockaddr_in6);
        return;
    }

    if (*outLength >= static_cast<int>(sizeof(sockaddr_in)))
    {
        auto* ipv4 = reinterpret_cast<sockaddr_in*>(outAddress);
        std::memset(ipv4, 0, sizeof(sockaddr_in));
        ipv4->sin_family = AF_INET;
        ipv4->sin_port = htons(pretendPort);
        ipv4->sin_addr.S_un.S_addr = 0;
        *outLength = sizeof(sockaddr_in);
    }
}

bool IsEndpointValid(const eos::FakeEndpoint& endpoint)
{
    return ntohs(endpoint.address.u.Word[0]) == 0x3FFE;
}

uint16_t GetLocalSocketPort(SOCKET socketHandle)
{
    sockaddr_storage addr{};
    int addrLen = sizeof(addr);
    if (getsockname(socketHandle, reinterpret_cast<sockaddr*>(&addr), &addrLen) == SOCKET_ERROR)
        return 0;

    if (addr.ss_family == AF_INET)
        return ntohs(reinterpret_cast<sockaddr_in*>(&addr)->sin_port);

    if (addr.ss_family == AF_INET6)
        return ntohs(reinterpret_cast<sockaddr_in6*>(&addr)->sin6_port);

    return 0;
}

eos::PacketRoute ClassifySocketRoute(uint16_t port)
{
    if (port == 0)
        return eos::PacketRoute::All;

    const uint16_t clientPort = eos::GetConfiguredClientPort();
    const uint16_t hostPort = eos::GetConfiguredHostPort();

    if (clientPort && port == clientPort)
        return eos::PacketRoute::Client;

    if (hostPort && port == hostPort)
        return eos::PacketRoute::Server;

    return eos::PacketRoute::All;
}

eos::PacketRoute DetermineSocketRoute(SOCKET socketHandle)
{
    {
        std::lock_guard lock(g_socketRouteMutex);
        auto it = g_socketRoutes.find(socketHandle);
        if (it != g_socketRoutes.end())
            return it->second;
    }

    const uint16_t port = GetLocalSocketPort(socketHandle);
    const eos::PacketRoute route = ClassifySocketRoute(port);

    // An unbound socket can be observed before its game port is assigned.
    // Do not freeze that classification, or it never becomes a game socket.
    if (route != eos::PacketRoute::All)
    {
        std::lock_guard lock(g_socketRouteMutex);
        g_socketRoutes[socketHandle] = route;
    }
    return route;
}

p2p::Route ToP2pRoute(eos::PacketRoute route)
{
    switch (route)
    {
    case eos::PacketRoute::Client: return p2p::Route::Client;
    case eos::PacketRoute::Server: return p2p::Route::Server;
    default: return p2p::Route::Unknown;
    }
}

eos::PacketRoute ToEosRoute(p2p::Route route)
{
    switch (route)
    {
    case p2p::Route::Client: return eos::PacketRoute::Client;
    case p2p::Route::Server: return eos::PacketRoute::Server;
    default: return eos::PacketRoute::All;
    }
}

// Handles R1NX control packets (pings from delta_connect) that arrived over
// EOS. Returns true if the packet was consumed.
bool ConsumeEosControl(eos::FakeIpLayer* layer, const eos::PendingPacket& packet, p2p::Route socketRoute)
{
    if (packet.payload.size() < p2p::kNatHeaderSize || packet.payload[0] != 0xFF)
        return false;
    p2p::ControlContext ctx;
    ctx.route = socketRoute != p2p::Route::Unknown
                    ? socketRoute
                    : (eos::IsServerNetContext() ? p2p::Route::Server : p2p::Route::Client);
    ctx.via = "eos";
    std::memcpy(ctx.source.data(), &packet.sender.address, ctx.source.size());
    ctx.haveSource = true;
    const eos::FakeEndpoint sender = packet.sender;
    const eos::PacketRoute replyRoute = ToEosRoute(ctx.route);
    ctx.reply = [layer, sender, replyRoute](const std::vector<uint8_t>& reply) {
        layer->SendToPeer(sender, reply.data(), reply.size(), replyRoute);
    };
    return p2p::TryConsumeControl(packet.payload.data(), packet.payload.size(), ctx);
}

int WSAAPI HookedSendTo(SOCKET socketHandle,
                        const char* buffer,
                        int length,
                        int flags,
                        const sockaddr* destAddr,
                        int destLen)
{
    if (p2p::IsForeignCaller(_ReturnAddress()))
    {
        return g_realSendTo
            ? g_realSendTo(socketHandle, buffer, length, flags, destAddr, destLen)
            : SOCKET_ERROR;
    }

    // iroh / tailcat / TURN peers live in 3ffd::/16.
    int overlayResult = 0;
    if (p2p::HandleSendTo(buffer, length, destAddr, destLen, &overlayResult))
        return overlayResult;

    eos::FakeEndpoint endpoint{};
    if (!g_eosEnabled || !buffer || length <= 0 || !ExtractFakeEndpoint(destAddr, endpoint))
    {
        return g_realSendTo
            ? g_realSendTo(socketHandle, buffer, length, flags, destAddr, destLen)
            : SOCKET_ERROR;
    }

    // Lazy initialize EOS when we first try to send to a fakeip
    if (!eos::EnsureEosInitialized())
    {
        WSASetLastError(WSAENOTCONN);
        return SOCKET_ERROR;
    }

    auto* layer = eos::EosLayer::Instance().GetFakeIpLayer();
    if (!layer)
    {
        WSASetLastError(WSAENOTCONN);
        return SOCKET_ERROR;
    }

    const eos::PacketRoute route = DetermineSocketRoute(socketHandle);
    if (route == eos::PacketRoute::Client)
    {
        // We are the client of this EOS peer: never treat its replies as an
        // unidentified inbound player, and prove our identity to it.
        p2p::Ipv6Bytes address;
        std::memcpy(address.data(), &endpoint.address, address.size());
        p2p::Identities().MarkOutgoing(address);
        p2p::EnsureEosIdentity(address);
    }
    const bool sent = layer->SendToPeer(endpoint,
                                        reinterpret_cast<const uint8_t*>(buffer),
                                        static_cast<size_t>(length),
                                        route);
    if (!sent)
    {
        WSASetLastError(WSAECONNABORTED);
        return SOCKET_ERROR;
    }

    return length;
}

int WSAAPI HookedRecvFrom(SOCKET socketHandle,
                          char* buffer,
                          int length,
                          int flags,
                          sockaddr* from,
                          int* fromLen)
{
    // Sockets owned by the EOS SDK or the overlay plugins must never be
    // handed engine datagrams.
    if (p2p::IsForeignCaller(_ReturnAddress()))
    {
        return g_realRecvFrom
            ? g_realRecvFrom(socketHandle, buffer, length, flags, from, fromLen)
            : SOCKET_ERROR;
    }

    const eos::PacketRoute desiredRoute = DetermineSocketRoute(socketHandle);
    const p2p::Route p2pRoute = ToP2pRoute(desiredRoute);
    if (p2pRoute == p2p::Route::Unknown || !buffer || length <= 0 ||
        (from && (!fromLen || *fromLen < static_cast<int>(sizeof(sockaddr_in)))))
    {
        return g_realRecvFrom
            ? g_realRecvFrom(socketHandle, buffer, length, flags, from, fromLen)
            : SOCKET_ERROR;
    }

    p2p::NoteEngineSocket(socketHandle, p2pRoute);

    // Datagrams from overlay transports (iroh, tailcat, TURN) and packets the
    // delta_connect prober read off this socket.
    int queuedResult = 0;
    if (p2p::PopQueued(p2pRoute, buffer, length, from, fromLen, &queuedResult))
        return queuedResult;

    auto* layer = eos::EosLayer::Instance().GetFakeIpLayer();
    if (layer)
    {
        eos::PendingPacket packet;
        while (layer->PopPacket(desiredRoute, packet))
        {
            if (ConsumeEosControl(layer, packet, p2pRoute))
                continue;

            // Players joining over EOS must have presented a master-attested
            // identity (IP bans apply across transports).
            if (p2pRoute != p2p::Route::Client &&
                p2p::ShouldDropUnidentified(reinterpret_cast<const uint8_t*>(&packet.sender.address)))
                continue;

            const int copyLength = static_cast<int>(std::min<size_t>(static_cast<size_t>(length), packet.payload.size()));
            if (buffer && copyLength > 0)
            {
                std::memcpy(buffer, packet.payload.data(), copyLength);
            }

            if (from && fromLen)
            {
                WriteSockaddrForFakeEndpoint(packet.sender, from, fromLen);
            }

            return copyLength;
        }
    }

    if (!g_realRecvFrom)
        return SOCKET_ERROR;

    // Hole-punching / rendezvous / ping control packets never reach the
    // engine. After consuming one, keep reading while more data is queued so
    // a stream of control packets cannot stall the engine's receive loop; a
    // zero-timeout select guarantees we never block, even on a blocking socket.
    const int fromLenIn = fromLen ? *fromLen : 0;
    for (int consumed = 0; consumed < 64; ++consumed)
    {
        if (fromLen)
            *fromLen = fromLenIn;
        const int received = g_realRecvFrom(socketHandle, buffer, length, flags, from, fromLen);
        if (received <= 0 || p2pRoute == p2p::Route::Unknown ||
            !p2p::ConsumeUdpControl(socketHandle, p2pRoute, reinterpret_cast<const uint8_t*>(buffer), received, from,
                                    fromLen ? *fromLen : 0))
        {
            return received;
        }

        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(socketHandle, &readSet);
        timeval noWait{ 0, 0 };
        if (select(0, &readSet, nullptr, nullptr, &noWait) <= 0)
            break;
    }
    WSASetLastError(WSAEWOULDBLOCK);
    return SOCKET_ERROR;
}

int WSAAPI HookedCloseSocket(SOCKET s)
{
    {
        std::lock_guard lock(g_socketRouteMutex);
        g_socketRoutes.erase(s);
    }
    p2p::ForgetSocket(s);
    return g_realCloseSocket ? g_realCloseSocket(s) : 0;
}

bool InstallSocketHooks()
{
    if (g_hooksInstalled)
        return true;

    HMODULE ws2 = GetModuleHandleA("wsock32.dll");
    if (!ws2)
    {
        ws2 = LoadLibraryA("wsock32.dll");
    }
    if (!ws2)
        return false;

    g_sendToTarget = reinterpret_cast<LPVOID>(GetProcAddress(ws2, "sendto"));
    g_recvFromTarget = reinterpret_cast<LPVOID>(GetProcAddress(ws2, "recvfrom"));
    g_closeSocketTarget = reinterpret_cast<LPVOID>(GetProcAddress(ws2, "closesocket"));
    if (!g_sendToTarget || !g_recvFromTarget || !g_closeSocketTarget)
        return false;

    LPVOID created[3]{};
    size_t createdCount = 0;
    auto rollback = [&] {
        for (size_t i = 0; i < createdCount; ++i)
        {
            MH_DisableHook(created[i]);
            MH_RemoveHook(created[i]);
        }
        g_realSendTo = nullptr;
        g_realRecvFrom = nullptr;
        g_realCloseSocket = nullptr;
    };

    if (MH_CreateHook(g_sendToTarget, &HookedSendTo, reinterpret_cast<LPVOID*>(&g_realSendTo)) != MH_OK)
        return false;
    created[createdCount++] = g_sendToTarget;
    if (MH_CreateHook(g_recvFromTarget, &HookedRecvFrom, reinterpret_cast<LPVOID*>(&g_realRecvFrom)) != MH_OK)
    {
        rollback();
        return false;
    }
    created[createdCount++] = g_recvFromTarget;
    if (MH_CreateHook(g_closeSocketTarget, &HookedCloseSocket, reinterpret_cast<LPVOID*>(&g_realCloseSocket)) != MH_OK)
    {
        rollback();
        return false;
    }
    created[createdCount++] = g_closeSocketTarget;

    const MH_STATUS sendStatus = MH_EnableHook(g_sendToTarget);
    const MH_STATUS recvStatus = MH_EnableHook(g_recvFromTarget);
    const MH_STATUS closeStatus = MH_EnableHook(g_closeSocketTarget);
    if ((sendStatus != MH_OK && sendStatus != MH_ERROR_ENABLED) ||
        (recvStatus != MH_OK && recvStatus != MH_ERROR_ENABLED) ||
        (closeStatus != MH_OK && closeStatus != MH_ERROR_ENABLED))
    {
        rollback();
        return false;
    }

    p2p::SetRealSocketFunctions(g_realSendTo, g_realRecvFrom);
    g_hooksInstalled = true;
    return true;
}

void RemoveSocketHooks()
{
    if (!g_hooksInstalled)
        return;

    if (g_sendToTarget)
    {
        MH_DisableHook(g_sendToTarget);
        MH_RemoveHook(g_sendToTarget);
        g_sendToTarget = nullptr;
    }
    if (g_recvFromTarget)
    {
        MH_DisableHook(g_recvFromTarget);
        MH_RemoveHook(g_recvFromTarget);
        g_recvFromTarget = nullptr;
    }
    if (g_closeSocketTarget)
    {
        MH_DisableHook(g_closeSocketTarget);
        MH_RemoveHook(g_closeSocketTarget);
        g_closeSocketTarget = nullptr;
    }

    p2p::SetRealSocketFunctions(nullptr, nullptr);
    g_realSendTo = nullptr;
    g_realRecvFrom = nullptr;
    g_realCloseSocket = nullptr;
    g_hooksInstalled = false;
    {
        std::lock_guard lock(g_socketRouteMutex);
        g_socketRoutes.clear();
    }
}

} // namespace

namespace eos
{

static std::mutex g_lazyInitMutex;
static bool g_lazyInitAttempted = false;
static bool g_lazyInitSuccess = false;

bool EnsureEosInitialized()
{
    std::lock_guard lock(g_lazyInitMutex);

    if (g_lazyInitAttempted)
        return g_lazyInitSuccess;

    g_lazyInitAttempted = true;

    auto& layer = EosLayer::Instance();
    if (layer.IsInitialized())
    {
        g_lazyInitSuccess = true;
        return true;
    }

    if (!layer.Initialize(kProductId, kSandboxId, kDeploymentId, kProductName, kProductVersion))
    {
        Warning("EOS: Optional networking layer unavailable; EOS routes disabled, direct IP/LAN remains available\n");
        g_lazyInitSuccess = false;
        return false;
    }

    g_lazyInitSuccess = true;
    return true;
}

bool InitializeNetworking()
{
    // The socket hooks are shared by EOS and the p2p transports (hole
    // punching, iroh, tailcat, TURN), so they are installed regardless of
    // whether EOS itself is enabled for this build.
    if (!InstallSocketHooks())
    {
        Error("EOS: Failed to install socket hooks\n");
        return false;
    }

    // The EOS SDK's own sockets bypass the hooks.
    p2p::RegisterForeignModule(GetModuleHandleA("EOSSDK-Win64-Shipping.dll"));

    // Check if EOS should be enabled based on version
    g_eosEnabled = ShouldEnableEOS();
    if (!g_eosEnabled)
    {
        Msg("EOS: Disabled for version %s (requires >= 3.0.0 or dev)\n", R1D_VERSION);
        return true;
    }

    Msg("EOS: Hooks installed for version %s, will initialize on first fakeip packet\n", R1D_VERSION);
    return true;
}

void ShutdownNetworking()
{
    RemoveSocketHooks();
    EosLayer::Instance().Shutdown();

    // Reset lazy init state
    std::lock_guard lock(g_lazyInitMutex);
    g_lazyInitAttempted = false;
    g_lazyInitSuccess = false;
}

bool IsReady()
{
    const auto& layer = EosLayer::Instance();
    return layer.IsInitialized() && layer.GetLocalUser() != nullptr;
}

bool RegisterPeerByString(const char* remoteProductUserId,
                          const char* socketName,
                          uint8_t channel,
                          FakeEndpoint* outEndpoint)
{
    auto& layer = EosLayer::Instance();
    if (!IsReady() || !remoteProductUserId || !socketName)
        return false;

    auto* fakeLayer = layer.GetFakeIpLayer();
    if (!fakeLayer)
        return false;

    EOS_ProductUserId remoteUser = nullptr;
    {
        SdkLock lock(GetSdkMutex());
        remoteUser = EOS_ProductUserId_FromString(remoteProductUserId);
    }
    if (!remoteUser)
        return false;

    const FakeEndpoint endpoint = fakeLayer->RegisterPeer(remoteUser, socketName, channel);
    if (!IsEndpointValid(endpoint))
        return false;

    if (outEndpoint)
    {
        *outEndpoint = endpoint;
    }

    return true;
}

EOS_ProductUserId GetLocalProductUserId()
{
    return EosLayer::Instance().GetLocalUser();
}

FakeEndpoint GetLocalFakeEndpoint()
{
    const auto* layer = EosLayer::Instance().GetFakeIpLayer();
    if (layer)
        return layer->GetLocalEndpoint();
    return {};
}

} // namespace eos

namespace p2p::eos_bridge
{

bool EnsureInitialized()
{
    return g_eosEnabled && eos::EnsureEosInitialized() && eos::IsReady();
}

bool IsReady()
{
    return g_eosEnabled && eos::IsReady();
}

std::string LocalProductUserId()
{
    EOS_ProductUserId user = eos::GetLocalProductUserId();
    if (!user)
        return {};
    char buffer[EOS_PRODUCTUSERID_MAX_LENGTH + 1]{};
    int32_t length = static_cast<int32_t>(sizeof(buffer));
    {
        eos::SdkLock lock(eos::GetSdkMutex());
        if (EOS_ProductUserId_ToString(user, buffer, &length) != EOS_EResult::EOS_Success)
            return {};
    }
    std::string normalized;
    for (int32_t i = 0; i < length && buffer[i]; ++i)
    {
        const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(buffer[i])));
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))
            normalized.push_back(c);
    }
    return normalized.size() == 32 ? normalized : std::string();
}

bool RegisterPeer(const std::string& productUserId, Ipv6Bytes& outAddress, uint16_t& outPort)
{
    eos::FakeEndpoint endpoint{};
    if (!eos::RegisterPeerByString(productUserId.c_str(), "r1delta", 0, &endpoint))
        return false;
    std::memcpy(outAddress.data(), &endpoint.address, outAddress.size());
    outPort = eos::GetPretendRemotePort();
    if (outPort == 0)
        outPort = endpoint.port;
    return true;
}

bool SendTo(const Ipv6Bytes& address, const uint8_t* data, size_t size, Route route)
{
    auto* layer = eos::EosLayer::Instance().GetFakeIpLayer();
    if (!layer)
        return false;
    eos::FakeEndpoint endpoint{};
    std::memcpy(&endpoint.address, address.data(), address.size());
    return layer->SendToPeer(endpoint, data, size, ToEosRoute(route));
}

void PollControl(Route route)
{
    auto* layer = eos::EosLayer::Instance().GetFakeIpLayer();
    if (!layer || route == Route::Unknown)
        return;
    eos::PendingPacket packet;
    for (int i = 0; i < 256 && layer->PopPacket(ToEosRoute(route), packet); ++i)
    {
        if (ConsumeEosControl(layer, packet, route))
            continue;
        sockaddr_in6 from{};
        int fromLen = sizeof(from);
        WriteSockaddrForFakeEndpoint(packet.sender, reinterpret_cast<sockaddr*>(&from), &fromLen);
        InjectForEngine(route, reinterpret_cast<sockaddr*>(&from), fromLen, packet.payload.data(), packet.payload.size());
    }
}

} // namespace p2p::eos_bridge
