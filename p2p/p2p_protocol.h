#pragma once

// Portable wire formats shared by all R1Delta NAT-traversal transports.
// Nothing in here touches sockets or Windows APIs so it can be unit tested
// on any platform (see tests/p2p_codec_tests.cpp).
//
// Control packets ("R1NX") travel over the game's own UDP socket and inside
// overlay transports (EOS, iroh, tailcat, TURN). They start with the
// connectionless marker FF FF FF FF so an engine that does not understand them
// simply drops them. The same format is implemented by the master server
// (masterserver2/nat.go). All integers are big-endian.

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace p2p
{

// ---------------------------------------------------------------------------
// Control packets
// ---------------------------------------------------------------------------

constexpr uint8_t kNatMagic[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 'R', '1', 'N', 'X' };
constexpr uint8_t kNatVersion = 1;
constexpr size_t kNatHeaderSize = sizeof(kNatMagic) + 2;

enum class PacketType : uint8_t
{
    SrvRegister = 0x01,  // server -> master: token[16]
    RegisterAck = 0x02,  // master -> peer: observed addr, flags, [server addr]
    CliRegister = 0x03,  // client -> master: ticket[16]
    PunchRequest = 0x04, // master -> server: ticket[16], client addr
    Punch = 0x05,        // peer <-> peer: ticket[16], role
    Ping = 0x06,         // client -> server: probe id, timestamp
    Pong = 0x07,         // server -> client: probe id, timestamp, flags
    PunchReqAck = 0x08,  // server -> master: ticket[16], client addr
};

using Id16 = std::array<uint8_t, 16>;

struct Ipv4Endpoint
{
    uint32_t ip = 0; // host order, e.g. 0x7F000001 for 127.0.0.1
    uint16_t port = 0;

    bool Valid() const { return ip != 0; }
    bool operator==(const Ipv4Endpoint& o) const { return ip == o.ip && port == o.port; }
    bool operator!=(const Ipv4Endpoint& o) const { return !(*this == o); }
    std::string ToString() const;
    static std::optional<Ipv4Endpoint> Parse(const std::string& text); // "a.b.c.d:port"
};

struct ParsedControl
{
    PacketType type{};
    const uint8_t* payload = nullptr;
    size_t payloadSize = 0;
};

// True if data is an R1NX control packet of a version we understand.
bool ParseControl(const uint8_t* data, size_t size, ParsedControl& out);
inline bool IsControlPacket(const uint8_t* data, size_t size)
{
    ParsedControl ignored;
    return ParseControl(data, size, ignored);
}

std::vector<uint8_t> BuildSrvRegister(const Id16& token);
std::vector<uint8_t> BuildCliRegister(const Id16& ticket);
std::vector<uint8_t> BuildPunch(const Id16& ticket, uint8_t role);
std::vector<uint8_t> BuildPunchReqAck(const Id16& ticket, const Ipv4Endpoint& client);
std::vector<uint8_t> BuildPing(uint64_t probeId, uint64_t timestampUs);
std::vector<uint8_t> BuildPong(uint64_t probeId, uint64_t timestampUs, uint8_t flags);

struct RegisterAck
{
    Ipv4Endpoint observed;
    std::optional<Ipv4Endpoint> server;
};
bool ParseRegisterAck(const ParsedControl& pkt, RegisterAck& out);

struct PunchRequest
{
    Id16 ticket{};
    Ipv4Endpoint client; // port 0 == "permission only"
};
bool ParsePunchRequest(const ParsedControl& pkt, PunchRequest& out);
bool ParseTicket(const ParsedControl& pkt, Id16& out); // Punch / PunchReqAck / CliRegister

struct PingPong
{
    uint64_t probeId = 0;
    uint64_t timestampUs = 0;
    uint8_t flags = 0; // pong only
};
bool ParsePingPong(const ParsedControl& pkt, PingPong& out);

bool HexToId16(const std::string& hex, Id16& out);
std::string Id16ToHex(const Id16& id);

// ---------------------------------------------------------------------------
// Fake IPv6 addresses for overlay peers
// ---------------------------------------------------------------------------
//
// The engine only knows sockaddrs, so every peer reached through an overlay
// transport gets a fake IPv6 address in 3ffd::/16 (EOS keeps using 3ffe::/16):
//   3ffd:00BB:0000:0000:<64-bit peer id>   (BB = Backend)

enum class Backend : uint8_t
{
    None = 0,
    Iroh = 1,
    Tailcat = 2,
    Turn = 3,
};

const char* BackendName(Backend backend);

using Ipv6Bytes = std::array<uint8_t, 16>;

Ipv6Bytes EncodeOverlayAddress(Backend backend, uint64_t peerId);
bool DecodeOverlayAddress(const Ipv6Bytes& addr, Backend& backend, uint64_t& peerId);
bool IsOverlayAddress(const uint8_t* addr16);
bool IsEosFakeAddress(const uint8_t* addr16);

// ---------------------------------------------------------------------------
// Overlay framing (fragmentation for transports with a small datagram MTU)
// ---------------------------------------------------------------------------
//
//   00 <payload>                                    whole datagram
//   01 <msgid:u16> <index:u8> <count:u8> <chunk>    fragment

constexpr uint8_t kFrameWhole = 0x00;
constexpr uint8_t kFrameFragment = 0x01;
constexpr size_t kFragmentHeaderSize = 5;
constexpr size_t kMaxFragments = 32;

// Splits payload into frames of at most maxFrameSize bytes each.
std::vector<std::vector<uint8_t>> FrameDatagram(const uint8_t* data,
                                                size_t size,
                                                size_t maxFrameSize,
                                                uint16_t& nextMessageId);

// Reassembles frames of a single peer.
class Reassembler
{
public:
    using Clock = std::chrono::steady_clock;

    // Returns true and fills out when a full datagram is available.
    bool Push(const uint8_t* frame, size_t size, std::vector<uint8_t>& out, Clock::time_point now = Clock::now());
    size_t PendingCount() const { return m_pending.size(); }

private:
    struct Pending
    {
        Clock::time_point firstSeen;
        uint8_t count = 0;
        uint8_t received = 0;
        std::vector<std::vector<uint8_t>> chunks;
        std::vector<bool> have;
    };
    void Expire(Clock::time_point now);

    std::map<uint16_t, Pending> m_pending;
};

// ---------------------------------------------------------------------------
// Misc helpers
// ---------------------------------------------------------------------------

// Formats an address for the engine's "connect" command.
std::string FormatIpv6Connect(const Ipv6Bytes& addr, uint16_t port);

} // namespace p2p
