#include "eos_network.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include <winsock2.h>
#include <ws2tcpip.h>

#include "MinHook.h"
#include "core.h"
#include "eos_layer.h"
#include "eos_threading.h"
#include "logging.h"

namespace
{

constexpr char kProductId[] = "f90ebc836e864395a3a3765916ead66a";
constexpr char kSandboxId[] = "2159ca4fcc1445b98cb4e706ba316415";
constexpr char kDeploymentId[] = "45505f63034c418eb057f6ba3065f99e";
constexpr char kProductName[] = "r1delta";
constexpr char kProductVersion[] = "1.18.1.2";

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

    {
        std::lock_guard lock(g_socketRouteMutex);
        g_socketRoutes[socketHandle] = route;
    }

    return route;
}

int WSAAPI HookedSendTo(SOCKET socketHandle,
                        const char* buffer,
                        int length,
                        int flags,
                        const sockaddr* destAddr,
                        int destLen)
{
    auto* layer = eos::EosLayer::Instance().GetFakeIpLayer();
    eos::FakeEndpoint endpoint{};
    if (!layer || !buffer || length <= 0 || !ExtractFakeEndpoint(destAddr, endpoint))
    {
        return g_realSendTo
            ? g_realSendTo(socketHandle, buffer, length, flags, destAddr, destLen)
            : SOCKET_ERROR;
    }

    const eos::PacketRoute route = DetermineSocketRoute(socketHandle);
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
    auto* layer = eos::EosLayer::Instance().GetFakeIpLayer();
    if (layer)
    {
        eos::PendingPacket packet;
        const eos::PacketRoute desiredRoute = DetermineSocketRoute(socketHandle);
        if (layer->PopPacket(desiredRoute, packet))
        {
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

    return g_realRecvFrom
        ? g_realRecvFrom(socketHandle, buffer, length, flags, from, fromLen)
        : SOCKET_ERROR;
}

int WSAAPI HookedCloseSocket(SOCKET s)
{
    {
        std::lock_guard lock(g_socketRouteMutex);
        g_socketRoutes.erase(s);
    }
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

    if (MH_CreateHook(g_sendToTarget, &HookedSendTo, reinterpret_cast<LPVOID*>(&g_realSendTo)) != MH_OK)
        return false;
    if (MH_CreateHook(g_recvFromTarget, &HookedRecvFrom, reinterpret_cast<LPVOID*>(&g_realRecvFrom)) != MH_OK)
        return false;
    if (MH_CreateHook(g_closeSocketTarget, &HookedCloseSocket, reinterpret_cast<LPVOID*>(&g_realCloseSocket)) != MH_OK)
        return false;

    const MH_STATUS sendStatus = MH_EnableHook(g_sendToTarget);
    const MH_STATUS recvStatus = MH_EnableHook(g_recvFromTarget);
    const MH_STATUS closeStatus = MH_EnableHook(g_closeSocketTarget);
    if ((sendStatus != MH_OK && sendStatus != MH_ERROR_ENABLED) ||
        (recvStatus != MH_OK && recvStatus != MH_ERROR_ENABLED) ||
        (closeStatus != MH_OK && closeStatus != MH_ERROR_ENABLED))
    {
        return false;
    }

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

bool InitializeNetworking()
{
    auto& layer = EosLayer::Instance();
    if (layer.IsInitialized())
        return InstallSocketHooks();

    if (!layer.Initialize(kProductId, kSandboxId, kDeploymentId, kProductName, kProductVersion))
    {
        Error("EOS: Failed to initialize networking layer\n");
        return false;
    }

    if (!InstallSocketHooks())
    {
        Error("EOS: Failed to install socket hooks\n");
        layer.Shutdown();
        return false;
    }

    return true;
}

void ShutdownNetworking()
{
    RemoveSocketHooks();
    EosLayer::Instance().Shutdown();
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
