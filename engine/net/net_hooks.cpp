#include "net_hooks.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_set>

#include "logging.h"
#include "MinHook.h"
#include "core.h"

namespace net_hooks
{
namespace
{

using GetAddrInfoFn = int (WSAAPI*)(PCSTR, PCSTR, const addrinfo*, addrinfo**);
using FreeAddrInfoFn = void (WSAAPI*)(addrinfo*);

GetAddrInfoFn g_originalGetAddrInfo = nullptr;
FreeAddrInfoFn g_originalFreeAddrInfo = nullptr;
LPVOID g_getAddrInfoTarget = nullptr;
LPVOID g_freeAddrInfoTarget = nullptr;
bool g_hookInstalled = false;

std::unordered_set<addrinfo*> g_customAllocations;
std::mutex g_allocationMutex;

constexpr uint16_t kFakeIpv6Prefix = 0x3FFE;

bool TryParseFakeIPv6(const char* nodeName, in6_addr& outAddr)
{
    if (!nodeName)
        return false;

    if (InetPtonA(AF_INET6, nodeName, &outAddr) != 1)
        return false;

    return ntohs(outAddr.u.Word[0]) == kFakeIpv6Prefix;
}

u_short ParseServicePort(PCSTR serviceName)
{
    if (!serviceName || !*serviceName)
        return 0;

    char* end = nullptr;
    long port = strtol(serviceName, &end, 10);
    if (end && *end == '\0' && port >= 0 && port <= 0xFFFF)
        return static_cast<u_short>(port);

    return 0;
}

addrinfo* AllocateAddrInfoEntry(const addrinfo* hints,
                                u_short port,
                                const in6_addr& ipv6Addr)
{
    auto* info = static_cast<addrinfo*>(std::calloc(1, sizeof(addrinfo)));
    if (!info)
        return nullptr;

    info->ai_family = AF_INET6;
    info->ai_socktype = hints ? hints->ai_socktype : 0;
    info->ai_protocol = hints ? hints->ai_protocol : 0;
    info->ai_flags = hints ? hints->ai_flags : 0;

    auto* addr6 = static_cast<sockaddr_in6*>(std::calloc(1, sizeof(sockaddr_in6)));
    if (!addr6)
    {
        std::free(info);
        return nullptr;
    }

    addr6->sin6_family = AF_INET6;
    addr6->sin6_port = htons(port);
    addr6->sin6_addr = ipv6Addr;

    info->ai_addr = reinterpret_cast<sockaddr*>(addr6);
    info->ai_addrlen = sizeof(sockaddr_in6);
    return info;
}

void FreeAddrInfoChain(addrinfo* head)
{
    while (head)
    {
        addrinfo* next = head->ai_next;
        if (head->ai_addr)
        {
            std::free(head->ai_addr);
        }
        std::free(head);
        head = next;
    }
}

bool BuildFakeAddrInfo(const in6_addr& ipv6Addr,
                       PCSTR serviceName,
                       const addrinfo* hints,
                       addrinfo** outResult)
{
    if (!outResult)
        return false;

    if (hints && hints->ai_family == AF_INET)
        return false;

    const u_short port = ParseServicePort(serviceName);

    addrinfo* entry = AllocateAddrInfoEntry(hints, port, ipv6Addr);
    if (!entry)
        return false;

    *outResult = entry;
    {
        std::lock_guard lock(g_allocationMutex);
        g_customAllocations.insert(entry);
    }
    return true;
}

int GetBlockedHostnameError(const char* nodeName)
{
    if (!nodeName)
        return 0;

    const size_t len = std::strlen(nodeName);
    if (len >= 11 && strcmp_static(nodeName + len - 11, "respawn.com") == 0)
        return WSA_SECURE_HOST_NOT_FOUND;

    if (string_equal_size(nodeName, len, "r1-pc-int.s3.amazonaws.com") ||
        string_equal_size(nodeName, len, "r1-pc.s3.amazonaws.com"))
        return WSAHOST_NOT_FOUND;

    return 0;
}

int WSAAPI HookedGetAddrInfo(PCSTR pNodeName,
                             PCSTR pServiceName,
                             const addrinfo* pHints,
                             addrinfo** ppResult)
{
    if (int err = GetBlockedHostnameError(pNodeName))
        return err;

    const int result = g_originalGetAddrInfo
                           ? g_originalGetAddrInfo(pNodeName, pServiceName, pHints, ppResult)
                           : WSANO_DATA;
    if (result != WSANO_DATA || !pNodeName || !ppResult)
        return result;

    in6_addr fakeAddr{};
    if (!TryParseFakeIPv6(pNodeName, fakeAddr))
        return result;

    if (BuildFakeAddrInfo(fakeAddr, pServiceName, pHints, ppResult))
        return 0;

    return result;
}

void WSAAPI HookedFreeAddrInfo(addrinfo* info)
{
    if (!info)
    {
        if (g_originalFreeAddrInfo)
            g_originalFreeAddrInfo(info);
        return;
    }

    {
        std::lock_guard lock(g_allocationMutex);
        auto it = g_customAllocations.find(info);
        if (it != g_customAllocations.end())
        {
            g_customAllocations.erase(it);
            FreeAddrInfoChain(info);
            return;
        }
    }

    if (g_originalFreeAddrInfo)
        g_originalFreeAddrInfo(info);
}

} // namespace

void Initialize()
{
    if (g_hookInstalled)
        return;

    HMODULE ws2 = GetModuleHandleA("ws2_32.dll");
    if (!ws2)
        ws2 = LoadLibraryA("ws2_32.dll");
    if (!ws2)
    {
        Msg("Failed to load ws2_32.dll for getaddrinfo hook\n");
        return;
    }

    g_getAddrInfoTarget = GetProcAddress(ws2, "getaddrinfo");
    g_freeAddrInfoTarget = GetProcAddress(ws2, "freeaddrinfo");
    if (!g_getAddrInfoTarget || !g_freeAddrInfoTarget)
    {
        Msg("Failed to locate getaddrinfo/freeaddrinfo for hooking\n");
        g_getAddrInfoTarget = nullptr;
        g_freeAddrInfoTarget = nullptr;
        return;
    }

    if (MH_CreateHook(g_getAddrInfoTarget, &HookedGetAddrInfo, reinterpret_cast<void**>(&g_originalGetAddrInfo)) != MH_OK)
    {
        Msg("Failed to create getaddrinfo hook\n");
        return;
    }

    if (MH_CreateHook(g_freeAddrInfoTarget, &HookedFreeAddrInfo, reinterpret_cast<void**>(&g_originalFreeAddrInfo)) != MH_OK)
    {
        Msg("Failed to create freeaddrinfo hook\n");
        MH_RemoveHook(g_getAddrInfoTarget);
        g_originalGetAddrInfo = nullptr;
        g_getAddrInfoTarget = nullptr;
        return;
    }

    if (MH_EnableHook(g_getAddrInfoTarget) != MH_OK)
    {
        Msg("Failed to enable getaddrinfo hook\n");
        MH_RemoveHook(g_getAddrInfoTarget);
        MH_RemoveHook(g_freeAddrInfoTarget);
        g_originalGetAddrInfo = nullptr;
        g_originalFreeAddrInfo = nullptr;
        g_getAddrInfoTarget = nullptr;
        g_freeAddrInfoTarget = nullptr;
        return;
    }

    if (MH_EnableHook(g_freeAddrInfoTarget) != MH_OK)
    {
        Msg("Failed to enable freeaddrinfo hook\n");
        MH_DisableHook(g_getAddrInfoTarget);
        MH_RemoveHook(g_getAddrInfoTarget);
        MH_RemoveHook(g_freeAddrInfoTarget);
        g_originalGetAddrInfo = nullptr;
        g_originalFreeAddrInfo = nullptr;
        g_getAddrInfoTarget = nullptr;
        g_freeAddrInfoTarget = nullptr;
        return;
    }

    g_hookInstalled = true;
}

} // namespace net_hooks
