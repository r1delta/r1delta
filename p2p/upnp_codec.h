#pragma once

// Portable helpers for UPnP IGD (SSDP discovery + SOAP port mapping) and
// NAT-PMP (RFC 6886). No sockets here; see p2p_upnp.cpp for the driver.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace p2p::upnp
{

struct Url
{
    std::string scheme; // "http"
    std::string host;
    uint16_t port = 80;
    std::string path; // starts with '/'
};

std::optional<Url> ParseUrl(const std::string& text);
// Resolves a possibly relative reference against a base URL.
std::string ResolveUrl(const Url& base, const std::string& ref);

// Builds an M-SEARCH request for the given search target.
std::string BuildMSearch(const std::string& searchTarget);
// Extracts the LOCATION header of an SSDP response (case-insensitive).
std::optional<std::string> ParseSsdpLocation(const std::string& response);

struct ControlPoint
{
    std::string serviceType; // e.g. urn:schemas-upnp-org:service:WANIPConnection:1
    std::string controlUrl;  // absolute
};

// Finds the WAN connection service in a device description document.
// Prefers WANIPConnection:2, then :1, then WANPPPConnection:1.
std::optional<ControlPoint> FindWanService(const std::string& descriptionXml, const std::string& location);

std::string BuildAddPortMapping(const std::string& serviceType, uint16_t externalPort, const char* protocol,
                                uint16_t internalPort, const std::string& internalClient,
                                const std::string& description, uint32_t leaseSeconds);
std::string BuildDeletePortMapping(const std::string& serviceType, uint16_t externalPort, const char* protocol);
std::string BuildGetExternalIp(const std::string& serviceType);
std::string SoapAction(const std::string& serviceType, const char* action);

// Returns the value of the first <tag>...</tag> element (namespace
// prefixes on the tag are ignored).
std::optional<std::string> XmlValue(const std::string& xml, const std::string& tag);
// UPnP error code from a SOAP fault, or 0.
int SoapErrorCode(const std::string& body);

// --- NAT-PMP ---------------------------------------------------------------
std::vector<uint8_t> BuildNatPmpExternalAddressRequest();
std::vector<uint8_t> BuildNatPmpMapUdpRequest(uint16_t internalPort, uint16_t externalPort, uint32_t lifetime);

struct NatPmpResponse
{
    uint8_t opcode = 0; // 128 + request opcode
    uint16_t result = 0;
    uint32_t externalIp = 0; // opcode 128
    uint16_t internalPort = 0;
    uint16_t mappedPort = 0;
    uint32_t lifetime = 0;
};
std::optional<NatPmpResponse> ParseNatPmpResponse(const uint8_t* data, size_t size);

} // namespace p2p::upnp
