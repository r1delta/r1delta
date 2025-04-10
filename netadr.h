#pragma once

#include <in6addr.h>
#include <cstdint>
#include <vcruntime_string.h>
#include <string.h>
#include <stdio.h>
#include <WS2tcpip.h>
#include <stdlib.h>
#include "bitbuf.h"
#define NET_IPV4_UNSPEC "0.0.0.0"
#define NET_IPV6_UNSPEC "::"
#define NET_IPV6_LOOPBACK "::1"

enum class netadrtype_t
{
    NA_NULL = 0,
    NA_LOOPBACK,
    NA_IP,
};

class CNetAdr
{
public:
    CNetAdr(void) { Clear(); }
    CNetAdr(const char* pch) { SetFromString(pch); }

    inline void Clear(void)
    {
        adr = {};
        port = 0;
        reliable = 0;
        type = netadrtype_t::NA_NULL;
    }

    inline void SetIP(IN6_ADDR* inAdr) { adr = *inAdr; }
    inline void SetPort(uint16_t newport) { port = newport; }
    inline void SetType(netadrtype_t newtype) { type = newtype; }
    inline netadrtype_t GetType(void) const { return type; }
    inline uint16_t GetPort(void) const { return port; }
    inline bool ComparePort(const CNetAdr& other) const { return port == other.port; }
    inline bool IsLoopback(void) const { return type == netadrtype_t::NA_LOOPBACK; }

    bool CompareAdr(const CNetAdr& other) const
    {
        if (type != other.type)
            return false;
        if (type == netadrtype_t::NA_LOOPBACK)
            return true;
        if (type == netadrtype_t::NA_IP)
            return (memcmp(&adr, &other.adr, sizeof(IN6_ADDR)) == 0);
        return false;
    }

    const char* ToString(bool bOnlyBase = false) const
    {
        static char s[4][128];
        static int slot = 0;
        int useSlot = (slot++) % 4;
        ToString(s[useSlot], sizeof(s[0]), bOnlyBase);
        return s[useSlot];
    }

    void ToString(char* pchBuffer, size_t unBufferSize, bool bOnlyBase = false) const
    {
        if (type == netadrtype_t::NA_NULL)
        {
            strncpy(pchBuffer, "null", unBufferSize);
        }
        else if (type == netadrtype_t::NA_LOOPBACK)
        {
            strncpy(pchBuffer, "loopback", unBufferSize);
        }
        else if (type == netadrtype_t::NA_IP)
        {
            char pStringBuf[128];
            inet_ntop(AF_INET6, &adr, pStringBuf, INET6_ADDRSTRLEN);

            if (bOnlyBase)
            {
                snprintf(pchBuffer, unBufferSize, "%s", pStringBuf);
            }
            else
            {
                snprintf(pchBuffer, unBufferSize, "[%s]:%i", pStringBuf, ntohs(port));
            }
        }
        else
        {
            memmove(pchBuffer, "unknown", unBufferSize);
        }
    }

    void ToAdrinfo(addrinfo* pHint) const
    {
        addrinfo hint{};
        hint.ai_flags = AI_PASSIVE;
        hint.ai_family = AF_INET6;
        hint.ai_socktype = SOCK_STREAM;
        hint.ai_protocol = IPPROTO_TCP;

        char szBuffer[33];
        _itoa_s(ntohs(GetPort()), szBuffer, sizeof(szBuffer), 10);
        getaddrinfo(ToString(true), szBuffer, &hint, &pHint);
    }

    void ToSockadr(struct sockaddr_storage* pSadr) const
    {
        memset(pSadr, 0, sizeof(struct sockaddr_storage));

        if (GetType() == netadrtype_t::NA_IP)
        {
            reinterpret_cast<sockaddr_in6*>(pSadr)->sin6_family = AF_INET6;
            reinterpret_cast<sockaddr_in6*>(pSadr)->sin6_port = port;
            inet_pton(AF_INET6, ToString(true), &reinterpret_cast<sockaddr_in6*>(pSadr)->sin6_addr);
        }
        else if (GetType() == netadrtype_t::NA_LOOPBACK)
        {
            reinterpret_cast<sockaddr_in6*>(pSadr)->sin6_family = AF_INET6;
            reinterpret_cast<sockaddr_in6*>(pSadr)->sin6_port = port;
            reinterpret_cast<sockaddr_in6*>(pSadr)->sin6_addr = in6addr_loopback;
        }
    }

    bool SetFromSockadr(struct sockaddr_storage* s)
    {
        char szAdrv6[INET6_ADDRSTRLEN]{};
        sockaddr_in6* pAdrv6 = reinterpret_cast<sockaddr_in6*>(s);

        inet_ntop(pAdrv6->sin6_family, &pAdrv6->sin6_addr, szAdrv6, sizeof(sockaddr_in6));

        SetFromString(szAdrv6);
        SetPort(pAdrv6->sin6_port);

        return true;
    }

    bool SetFromString(const char* pch, bool bUseDNS = false)
    {
        Clear();
        if (!pch)
            return false;

        SetType(netadrtype_t::NA_IP);

        char szAddress[128];
        strncpy(szAddress, pch, sizeof(szAddress));

        char* pszAddress = szAddress;
        szAddress[sizeof(szAddress) - 1] = '\0';

        if (szAddress[0] == '[')
            pszAddress = &szAddress[1];

        char* bracketEnd = strchr(szAddress, ']');
        if (bracketEnd)
        {
            *bracketEnd = '\0';

            char* portStart = &bracketEnd[1];
            char* pchColon = strrchr(portStart, ':');

            if (pchColon && strchr(portStart, ':') == pchColon)
            {
                pchColon[0] = '\0';
                SetPort(uint16_t(htons(uint16_t(atoi(&pchColon[1])))));
            }
        }

        if (!strchr(pszAddress, ':'))
        {
            char szNewAddressV4[128];
            snprintf(szNewAddressV4, sizeof(szNewAddressV4), "::FFFF:%s", pszAddress);

            if (inet_pton(AF_INET6, szNewAddressV4, &this->adr) > 0)
                return true;
        }
        else
        {
            if (inet_pton(AF_INET6, pszAddress, &this->adr) > 0)
                return true;
        }

        if (bUseDNS)
        {
            ADDRINFOA pHints{};
            PADDRINFOA ppResult = nullptr;

            pHints.ai_family = AF_INET6;
            pHints.ai_flags = AI_ALL | AI_V4MAPPED;

            if (getaddrinfo(pszAddress, nullptr, &pHints, &ppResult))
            {
                freeaddrinfo(ppResult);
                return false;
            }

            SetIP(reinterpret_cast<IN6_ADDR*>(&ppResult->ai_addr->sa_data[6]));
            freeaddrinfo(ppResult);

            return true;
        }

        return false;
    }
    // New method to get the address part as a string, preferring IPv4 format if possible
    void GetAddressString(char* pchBuffer, size_t unBufferSize) const
    {
        if (type == netadrtype_t::NA_NULL)
        {
            strncpy(pchBuffer, "null", unBufferSize);
        }
        else if (type == netadrtype_t::NA_LOOPBACK)
        {
            // Keep "loopback" for the specific type NA_LOOPBACK
            strncpy(pchBuffer, "loopback", unBufferSize);
        }
        else if (type == netadrtype_t::NA_IP)
        {
            // Check if it's an IPv4-mapped IPv6 address
            if (IN6_IS_ADDR_V4MAPPED(&adr))
            {
                // Extract the IPv4 address (last 4 bytes)
                struct in_addr ipv4_addr;
                memcpy(&ipv4_addr, &adr.s6_addr[12], sizeof(ipv4_addr)); // Copy the IPv4 part

                // Format using AF_INET
                inet_ntop(AF_INET, &ipv4_addr, pchBuffer, unBufferSize);
            }
            // Check if it's the IPv6 loopback ::1
            else if (IN6_IS_ADDR_LOOPBACK(&adr))
            {
                // Format IPv6 loopback specifically as ::1
                inet_ntop(AF_INET6, &adr, pchBuffer, unBufferSize);
            }
            // Otherwise, format as a standard IPv6 address
            else
            {
                inet_ntop(AF_INET6, &adr, pchBuffer, unBufferSize);
            }
        }
        else
        {
            strncpy(pchBuffer, "unknown", unBufferSize);
        }

        // Ensure null termination just in case inet_ntop didn't or strncpy didn't fill
        if (unBufferSize > 0) {
            pchBuffer[unBufferSize - 1] = '\0';
        }
    }

    // Version returning a static buffer for convenience (like ToString)
    const char* GetAddressString() const
    {
        static char s[4][INET6_ADDRSTRLEN]; // Use larger IPv6 buffer size
        static int slot = 0;
        int useSlot = (slot++) % 4;
        GetAddressString(s[useSlot], sizeof(s[0]));
        return s[useSlot];
    }

private:
    netadrtype_t type;
    IN6_ADDR adr;
    uint16_t port;
    bool field_16;
    bool reliable;
};

typedef class CNetAdr netadr_t;
typedef struct netpacket_s
{
    CNetAdr from;
    int source;
    double received;
    uint8_t* pData;
    bf_read message;
    int size;
    int wiresize;
    char stream;
    netpacket_s* pNext;
} netpacket_t;