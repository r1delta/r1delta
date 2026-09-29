#include "p2p_protocol.h"

#include <cstdio>
#include <cstring>

namespace p2p
{
namespace
{

void AppendU16(std::vector<uint8_t>& b, uint16_t v)
{
    b.push_back(static_cast<uint8_t>(v >> 8));
    b.push_back(static_cast<uint8_t>(v));
}

void AppendU64(std::vector<uint8_t>& b, uint64_t v)
{
    for (int shift = 56; shift >= 0; shift -= 8)
        b.push_back(static_cast<uint8_t>(v >> shift));
}

uint16_t ReadU16(const uint8_t* p)
{
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

uint32_t ReadU32(const uint8_t* p)
{
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

uint64_t ReadU64(const uint8_t* p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v = (v << 8) | p[i];
    return v;
}

std::vector<uint8_t> Header(PacketType type)
{
    std::vector<uint8_t> b(std::begin(kNatMagic), std::end(kNatMagic));
    b.push_back(kNatVersion);
    b.push_back(static_cast<uint8_t>(type));
    return b;
}

bool ReadEndpoint(const uint8_t* p, size_t size, Ipv4Endpoint& out)
{
    if (size < 6)
        return false;
    out.ip = ReadU32(p);
    out.port = ReadU16(p + 4);
    return true;
}

std::vector<uint8_t> TicketPacket(PacketType type, const Id16& id)
{
    auto b = Header(type);
    b.insert(b.end(), id.begin(), id.end());
    return b;
}

} // namespace

std::string Ipv4Endpoint::ToString() const
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u:%u",
                  (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF, port);
    return buf;
}

std::optional<Ipv4Endpoint> Ipv4Endpoint::Parse(const std::string& text)
{
    // "a.b.c.d:port" with decimal components only.
    uint32_t parts[5] = {};
    size_t part = 0;
    size_t digits = 0;
    for (size_t i = 0; i <= text.size(); ++i)
    {
        const char c = i < text.size() ? text[i] : '\0';
        if (c >= '0' && c <= '9')
        {
            parts[part] = parts[part] * 10 + static_cast<uint32_t>(c - '0');
            if (++digits > 5 || parts[part] > 0xFFFF)
                return std::nullopt;
            continue;
        }
        const char expected = part < 3 ? '.' : (part == 3 ? ':' : '\0');
        if (c != expected || digits == 0)
            return std::nullopt;
        if (part < 4 && parts[part] > 255)
            return std::nullopt;
        digits = 0;
        if (++part == 5)
            break;
    }
    if (part != 5)
        return std::nullopt;
    Ipv4Endpoint ep;
    ep.ip = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
    ep.port = static_cast<uint16_t>(parts[4]);
    return ep;
}

bool ParseControl(const uint8_t* data, size_t size, ParsedControl& out)
{
    if (!data || size < kNatHeaderSize)
        return false;
    if (std::memcmp(data, kNatMagic, sizeof(kNatMagic)) != 0)
        return false;
    if (data[sizeof(kNatMagic)] != kNatVersion)
        return false;
    out.type = static_cast<PacketType>(data[sizeof(kNatMagic) + 1]);
    out.payload = data + kNatHeaderSize;
    out.payloadSize = size - kNatHeaderSize;
    return true;
}

std::vector<uint8_t> BuildSrvRegister(const Id16& token)
{
    return TicketPacket(PacketType::SrvRegister, token);
}

std::vector<uint8_t> BuildCliRegister(const Id16& ticket)
{
    return TicketPacket(PacketType::CliRegister, ticket);
}

std::vector<uint8_t> BuildPunch(const Id16& ticket, uint8_t role)
{
    auto b = TicketPacket(PacketType::Punch, ticket);
    b.push_back(role);
    return b;
}

std::vector<uint8_t> BuildPunchReqAck(const Id16& ticket, const Ipv4Endpoint& client)
{
    auto b = TicketPacket(PacketType::PunchReqAck, ticket);
    for (int shift = 24; shift >= 0; shift -= 8)
        b.push_back(static_cast<uint8_t>(client.ip >> shift));
    AppendU16(b, client.port);
    return b;
}

std::vector<uint8_t> BuildPing(uint64_t probeId, uint64_t timestampUs)
{
    auto b = Header(PacketType::Ping);
    AppendU64(b, probeId);
    AppendU64(b, timestampUs);
    return b;
}

std::vector<uint8_t> BuildPong(uint64_t probeId, uint64_t timestampUs, uint8_t flags, uint64_t serverTag)
{
    auto b = Header(PacketType::Pong);
    AppendU64(b, probeId);
    AppendU64(b, timestampUs);
    b.push_back(flags);
    AppendU64(b, serverTag);
    return b;
}

bool ParseRegisterAck(const ParsedControl& pkt, RegisterAck& out)
{
    if (pkt.type != PacketType::RegisterAck || !ReadEndpoint(pkt.payload, pkt.payloadSize, out.observed))
        return false;
    out.server.reset();
    if (pkt.payloadSize >= 7 && (pkt.payload[6] & 0x01) != 0)
    {
        Ipv4Endpoint server;
        if (!ReadEndpoint(pkt.payload + 7, pkt.payloadSize - 7, server))
            return false;
        out.server = server;
    }
    return true;
}

bool ParsePunchRequest(const ParsedControl& pkt, PunchRequest& out)
{
    if (pkt.type != PacketType::PunchRequest || pkt.payloadSize < 22)
        return false;
    std::memcpy(out.ticket.data(), pkt.payload, 16);
    std::memcpy(out.signedPart.data(), pkt.payload, out.signedPart.size());
    out.haveMac = pkt.payloadSize >= 38;
    if (out.haveMac)
        std::memcpy(out.mac.data(), pkt.payload + 22, out.mac.size());
    return ReadEndpoint(pkt.payload + 16, pkt.payloadSize - 16, out.client);
}

bool ParseTicket(const ParsedControl& pkt, Id16& out)
{
    if (pkt.payloadSize < 16)
        return false;
    std::memcpy(out.data(), pkt.payload, 16);
    return true;
}

bool ParsePingPong(const ParsedControl& pkt, PingPong& out)
{
    if (pkt.type != PacketType::Ping && pkt.type != PacketType::Pong)
        return false;
    if (pkt.payloadSize < 16)
        return false;
    out.probeId = ReadU64(pkt.payload);
    out.timestampUs = ReadU64(pkt.payload + 8);
    out.flags = (pkt.type == PacketType::Pong && pkt.payloadSize >= 17) ? pkt.payload[16] : 0;
    out.serverTag = (pkt.type == PacketType::Pong && pkt.payloadSize >= 25) ? ReadU64(pkt.payload + 17) : 0;
    return true;
}

bool HexToId16(const std::string& hex, Id16& out)
{
    if (hex.size() != 32)
        return false;
    auto nibble = [](char c, uint8_t& v) {
        if (c >= '0' && c <= '9') { v = static_cast<uint8_t>(c - '0'); return true; }
        if (c >= 'a' && c <= 'f') { v = static_cast<uint8_t>(c - 'a' + 10); return true; }
        if (c >= 'A' && c <= 'F') { v = static_cast<uint8_t>(c - 'A' + 10); return true; }
        return false;
    };
    for (size_t i = 0; i < 16; ++i)
    {
        uint8_t hi, lo;
        if (!nibble(hex[i * 2], hi) || !nibble(hex[i * 2 + 1], lo))
            return false;
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

std::string Id16ToHex(const Id16& id)
{
    static const char kHex[] = "0123456789abcdef";
    std::string s(32, '0');
    for (size_t i = 0; i < 16; ++i)
    {
        s[i * 2] = kHex[id[i] >> 4];
        s[i * 2 + 1] = kHex[id[i] & 0xF];
    }
    return s;
}

const char* BackendName(Backend backend)
{
    switch (backend)
    {
    case Backend::Iroh: return "iroh";
    case Backend::Tailcat: return "tailcat";
    case Backend::Turn: return "turn";
    default: return "none";
    }
}

Ipv6Bytes EncodeOverlayAddress(Backend backend, uint64_t peerId)
{
    Ipv6Bytes a{};
    a[0] = 0x3F;
    a[1] = 0xFD;
    a[3] = static_cast<uint8_t>(backend);
    for (int i = 0; i < 8; ++i)
        a[8 + i] = static_cast<uint8_t>(peerId >> (56 - 8 * i));
    return a;
}

bool DecodeOverlayAddress(const Ipv6Bytes& addr, Backend& backend, uint64_t& peerId)
{
    if (!IsOverlayAddress(addr.data()))
        return false;
    backend = static_cast<Backend>(addr[3]);
    peerId = ReadU64(addr.data() + 8);
    return backend == Backend::Iroh || backend == Backend::Tailcat || backend == Backend::Turn;
}

bool IsOverlayAddress(const uint8_t* addr16)
{
    return addr16 && addr16[0] == 0x3F && addr16[1] == 0xFD;
}

bool IsEosFakeAddress(const uint8_t* addr16)
{
    return addr16 && addr16[0] == 0x3F && addr16[1] == 0xFE;
}

std::vector<std::vector<uint8_t>> FrameDatagram(const uint8_t* data,
                                                size_t size,
                                                size_t maxFrameSize,
                                                uint16_t& nextMessageId)
{
    std::vector<std::vector<uint8_t>> frames;
    if (!data || maxFrameSize <= kFragmentHeaderSize)
        return frames;

    if (size + 1 <= maxFrameSize)
    {
        std::vector<uint8_t> f;
        f.reserve(size + 1);
        f.push_back(kFrameWhole);
        f.insert(f.end(), data, data + size);
        frames.push_back(std::move(f));
        return frames;
    }

    const size_t chunk = maxFrameSize - kFragmentHeaderSize;
    const size_t count = (size + chunk - 1) / chunk;
    if (count > kMaxFragments)
        return frames;

    const uint16_t msgId = nextMessageId++;
    for (size_t i = 0; i < count; ++i)
    {
        const size_t offset = i * chunk;
        const size_t len = (size - offset < chunk) ? size - offset : chunk;
        std::vector<uint8_t> f;
        f.reserve(kFragmentHeaderSize + len);
        f.push_back(kFrameFragment);
        AppendU16(f, msgId);
        f.push_back(static_cast<uint8_t>(i));
        f.push_back(static_cast<uint8_t>(count));
        f.insert(f.end(), data + offset, data + offset + len);
        frames.push_back(std::move(f));
    }
    return frames;
}

bool Reassembler::Push(const uint8_t* frame, size_t size, std::vector<uint8_t>& out, Clock::time_point now)
{
    if (!frame || size < 1)
        return false;

    if (frame[0] == kFrameWhole)
    {
        out.assign(frame + 1, frame + size);
        return true;
    }
    if (frame[0] != kFrameFragment || size < kFragmentHeaderSize)
        return false;

    Expire(now);

    const uint16_t msgId = ReadU16(frame + 1);
    const uint8_t index = frame[3];
    const uint8_t count = frame[4];
    if (count < 2 || count > kMaxFragments || index >= count)
        return false;

    // Bound memory use per peer.
    if (m_pending.size() >= 16 && m_pending.find(msgId) == m_pending.end())
        m_pending.erase(m_pending.begin());

    Pending& p = m_pending[msgId];
    if (p.count == 0)
    {
        p.firstSeen = now;
        p.count = count;
        p.chunks.assign(count, {});
        p.have.assign(count, false);
    }
    else if (p.count != count)
    {
        m_pending.erase(msgId);
        return false;
    }

    if (p.have[index])
        return false; // duplicate
    p.have[index] = true;
    p.chunks[index].assign(frame + kFragmentHeaderSize, frame + size);
    ++p.received;
    if (p.received < p.count)
        return false;

    out.clear();
    for (auto& c : p.chunks)
        out.insert(out.end(), c.begin(), c.end());
    m_pending.erase(msgId);
    return true;
}

void Reassembler::Expire(Clock::time_point now)
{
    for (auto it = m_pending.begin(); it != m_pending.end();)
    {
        if (now - it->second.firstSeen > std::chrono::seconds(2))
            it = m_pending.erase(it);
        else
            ++it;
    }
}

std::string FormatIpv6Connect(const Ipv6Bytes& addr, uint16_t port)
{
    uint16_t groups[8];
    for (int i = 0; i < 8; ++i)
        groups[i] = static_cast<uint16_t>((addr[i * 2] << 8) | addr[i * 2 + 1]);

    // Find the longest run (>= 2) of zero groups to compress.
    int bestStart = -1, bestLen = 0;
    for (int i = 0; i < 8;)
    {
        if (groups[i] != 0)
        {
            ++i;
            continue;
        }
        int j = i;
        while (j < 8 && groups[j] == 0)
            ++j;
        if (j - i > bestLen && j - i >= 2)
        {
            bestStart = i;
            bestLen = j - i;
        }
        i = j;
    }

    std::string s = "[";
    char buf[8];
    for (int i = 0; i < 8; ++i)
    {
        if (i == bestStart)
        {
            s += "::";
            i += bestLen - 1;
            continue;
        }
        if (!s.empty() && s.back() != '[' && s.back() != ':')
            s += ':';
        std::snprintf(buf, sizeof(buf), "%x", groups[i]);
        s += buf;
    }
    std::snprintf(buf, sizeof(buf), "%u", port);
    s += "]:";
    s += buf;
    return s;
}

} // namespace p2p
