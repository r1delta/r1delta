#include "upnp_codec.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace p2p::upnp
{
namespace
{

std::string Lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string Trim(const std::string& s)
{
    const size_t b = s.find_first_not_of(" \t\r\n");
    const size_t e = s.find_last_not_of(" \t\r\n");
    return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

std::string EscapeXml(const std::string& s)
{
    std::string out;
    for (char c : s)
    {
        switch (c)
        {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c; break;
        }
    }
    return out;
}

std::string Envelope(const std::string& serviceType, const std::string& action, const std::string& args)
{
    return "<?xml version=\"1.0\"?>\r\n"
           "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
           "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
           "<s:Body><u:" + action + " xmlns:u=\"" + serviceType + "\">" + args + "</u:" + action +
           "></s:Body></s:Envelope>\r\n";
}

void PutU16(std::vector<uint8_t>& b, uint16_t v)
{
    b.push_back(static_cast<uint8_t>(v >> 8));
    b.push_back(static_cast<uint8_t>(v));
}

void PutU32(std::vector<uint8_t>& b, uint32_t v)
{
    PutU16(b, static_cast<uint16_t>(v >> 16));
    PutU16(b, static_cast<uint16_t>(v));
}

uint16_t GetU16(const uint8_t* p)
{
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

uint32_t GetU32(const uint8_t* p)
{
    return (static_cast<uint32_t>(GetU16(p)) << 16) | GetU16(p + 2);
}

} // namespace

std::optional<Url> ParseUrl(const std::string& text)
{
    const std::string t = Trim(text);
    const size_t schemeEnd = t.find("://");
    if (schemeEnd == std::string::npos)
        return std::nullopt;
    Url u;
    u.scheme = Lower(t.substr(0, schemeEnd));
    if (u.scheme != "http")
        return std::nullopt;
    const size_t hostStart = schemeEnd + 3;
    const size_t pathStart = t.find('/', hostStart);
    std::string hostPort = t.substr(hostStart, pathStart == std::string::npos ? std::string::npos : pathStart - hostStart);
    u.path = pathStart == std::string::npos ? "/" : t.substr(pathStart);
    const size_t colon = hostPort.rfind(':');
    if (colon != std::string::npos && hostPort.find(']') == std::string::npos)
    {
        const long port = std::strtol(hostPort.c_str() + colon + 1, nullptr, 10);
        if (port <= 0 || port > 0xFFFF)
            return std::nullopt;
        u.port = static_cast<uint16_t>(port);
        hostPort = hostPort.substr(0, colon);
    }
    u.host = hostPort;
    if (u.host.empty())
        return std::nullopt;
    return u;
}

std::string ResolveUrl(const Url& base, const std::string& refIn)
{
    const std::string ref = Trim(refIn);
    if (ref.find("://") != std::string::npos)
        return ref;
    std::string root = "http://" + base.host + ":" + std::to_string(base.port);
    if (!ref.empty() && ref[0] == '/')
        return root + ref;
    const size_t slash = base.path.rfind('/');
    const std::string dir = slash == std::string::npos ? "/" : base.path.substr(0, slash + 1);
    return root + dir + ref;
}

std::string BuildMSearch(const std::string& searchTarget)
{
    return "M-SEARCH * HTTP/1.1\r\n"
           "HOST: 239.255.255.250:1900\r\n"
           "MAN: \"ssdp:discover\"\r\n"
           "MX: 2\r\n"
           "ST: " + searchTarget + "\r\n\r\n";
}

std::optional<std::string> ParseSsdpLocation(const std::string& response)
{
    size_t pos = 0;
    while (pos < response.size())
    {
        size_t end = response.find('\n', pos);
        if (end == std::string::npos)
            end = response.size();
        const std::string line = response.substr(pos, end - pos);
        const size_t colon = line.find(':');
        if (colon != std::string::npos && Lower(Trim(line.substr(0, colon))) == "location")
        {
            const std::string value = Trim(line.substr(colon + 1));
            if (!value.empty())
                return value;
        }
        pos = end + 1;
    }
    return std::nullopt;
}

std::optional<std::string> XmlValue(const std::string& xml, const std::string& tag)
{
    // Matches <tag>, <ns:tag> and <tag attr="...">.
    size_t pos = 0;
    while ((pos = xml.find('<', pos)) != std::string::npos)
    {
        size_t nameStart = pos + 1;
        if (nameStart < xml.size() && (xml[nameStart] == '/' || xml[nameStart] == '?' || xml[nameStart] == '!'))
        {
            ++pos;
            continue;
        }
        size_t nameEnd = nameStart;
        while (nameEnd < xml.size() && !std::isspace(static_cast<unsigned char>(xml[nameEnd])) && xml[nameEnd] != '>' &&
               xml[nameEnd] != '/')
            ++nameEnd;
        std::string name = xml.substr(nameStart, nameEnd - nameStart);
        const size_t colon = name.find(':');
        const std::string local = colon == std::string::npos ? name : name.substr(colon + 1);
        const size_t close = xml.find('>', nameEnd);
        if (close == std::string::npos)
            return std::nullopt;
        if (local == tag && xml[close - 1] != '/')
        {
            const std::string endTag = "</" + name + ">";
            const size_t valueEnd = xml.find(endTag, close + 1);
            if (valueEnd == std::string::npos)
                return std::nullopt;
            return Trim(xml.substr(close + 1, valueEnd - close - 1));
        }
        pos = close + 1;
    }
    return std::nullopt;
}

std::optional<ControlPoint> FindWanService(const std::string& xml, const std::string& location)
{
    const auto base = ParseUrl(location);
    if (!base)
        return std::nullopt;

    // URLBase (UPnP 1.0) overrides the location for relative control URLs.
    Url resolveBase = *base;
    if (auto urlBase = XmlValue(xml, "URLBase"))
        if (auto parsed = ParseUrl(*urlBase))
            resolveBase = *parsed;

    static const char* kPreferred[] = {
        "urn:schemas-upnp-org:service:WANIPConnection:2",
        "urn:schemas-upnp-org:service:WANIPConnection:1",
        "urn:schemas-upnp-org:service:WANPPPConnection:1",
    };
    for (const char* wanted : kPreferred)
    {
        size_t pos = 0;
        while ((pos = xml.find("<service>", pos)) != std::string::npos)
        {
            const size_t end = xml.find("</service>", pos);
            if (end == std::string::npos)
                break;
            const std::string block = xml.substr(pos, end - pos);
            pos = end;
            const auto type = XmlValue(block, "serviceType");
            const auto control = XmlValue(block, "controlURL");
            if (type && control && *type == wanted)
                return ControlPoint{ *type, ResolveUrl(resolveBase, *control) };
        }
    }
    return std::nullopt;
}

std::string SoapAction(const std::string& serviceType, const char* action)
{
    return "\"" + serviceType + "#" + action + "\"";
}

std::string BuildAddPortMapping(const std::string& serviceType, uint16_t externalPort, const char* protocol,
                                uint16_t internalPort, const std::string& internalClient,
                                const std::string& description, uint32_t leaseSeconds)
{
    const std::string args = "<NewRemoteHost></NewRemoteHost>"
                             "<NewExternalPort>" + std::to_string(externalPort) + "</NewExternalPort>"
                             "<NewProtocol>" + std::string(protocol) + "</NewProtocol>"
                             "<NewInternalPort>" + std::to_string(internalPort) + "</NewInternalPort>"
                             "<NewInternalClient>" + EscapeXml(internalClient) + "</NewInternalClient>"
                             "<NewEnabled>1</NewEnabled>"
                             "<NewPortMappingDescription>" + EscapeXml(description) + "</NewPortMappingDescription>"
                             "<NewLeaseDuration>" + std::to_string(leaseSeconds) + "</NewLeaseDuration>";
    return Envelope(serviceType, "AddPortMapping", args);
}

std::string BuildDeletePortMapping(const std::string& serviceType, uint16_t externalPort, const char* protocol)
{
    const std::string args = "<NewRemoteHost></NewRemoteHost>"
                             "<NewExternalPort>" + std::to_string(externalPort) + "</NewExternalPort>"
                             "<NewProtocol>" + std::string(protocol) + "</NewProtocol>";
    return Envelope(serviceType, "DeletePortMapping", args);
}

std::string BuildGetExternalIp(const std::string& serviceType)
{
    return Envelope(serviceType, "GetExternalIPAddress", "");
}

int SoapErrorCode(const std::string& body)
{
    const auto code = XmlValue(body, "errorCode");
    return code ? std::atoi(code->c_str()) : 0;
}

std::vector<uint8_t> BuildNatPmpExternalAddressRequest()
{
    return { 0, 0 };
}

std::vector<uint8_t> BuildNatPmpMapUdpRequest(uint16_t internalPort, uint16_t externalPort, uint32_t lifetime)
{
    std::vector<uint8_t> b{ 0, 1 };
    PutU16(b, 0); // reserved
    PutU16(b, internalPort);
    PutU16(b, externalPort);
    PutU32(b, lifetime);
    return b;
}

std::optional<NatPmpResponse> ParseNatPmpResponse(const uint8_t* data, size_t size)
{
    if (!data || size < 8 || data[0] != 0 || data[1] < 128)
        return std::nullopt;
    NatPmpResponse r;
    r.opcode = data[1];
    r.result = GetU16(data + 2);
    if (r.opcode == 128)
    {
        if (size < 12)
            return std::nullopt;
        r.externalIp = GetU32(data + 8);
    }
    else if (r.opcode == 129 || r.opcode == 130)
    {
        if (size < 16)
            return std::nullopt;
        r.internalPort = GetU16(data + 8);
        r.mappedPort = GetU16(data + 10);
        r.lifetime = GetU32(data + 12);
    }
    return r;
}

} // namespace p2p::upnp
