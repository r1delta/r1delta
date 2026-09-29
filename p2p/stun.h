#pragma once

// Minimal STUN (RFC 5389) / TURN (RFC 5766) message codec, including the
// hashing it needs (MD5 for long-term credential keys, HMAC-SHA1 for
// MESSAGE-INTEGRITY and CRC-32 for FINGERPRINT). Portable, no sockets.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "p2p_protocol.h"

namespace p2p::stun
{

constexpr uint32_t kMagicCookie = 0x2112A442;
constexpr size_t kHeaderSize = 20;

enum Method : uint16_t
{
    kBinding = 0x001,
    kAllocate = 0x003,
    kRefresh = 0x004,
    kSend = 0x006,
    kData = 0x007,
    kCreatePermission = 0x008,
    kChannelBind = 0x009,
};

enum Class : uint16_t
{
    kRequest = 0x0000,
    kIndication = 0x0010,
    kSuccess = 0x0100,
    kError = 0x0110,
};

enum Attr : uint16_t
{
    kMappedAddress = 0x0001,
    kUsername = 0x0006,
    kMessageIntegrity = 0x0008,
    kErrorCode = 0x0009,
    kChannelNumber = 0x000C,
    kLifetime = 0x000D,
    kXorPeerAddress = 0x0012,
    kDataAttr = 0x0013,
    kRealm = 0x0014,
    kNonce = 0x0015,
    kXorRelayedAddress = 0x0016,
    kRequestedTransport = 0x0019,
    kXorMappedAddress = 0x0020,
    kSoftware = 0x8022,
    kFingerprint = 0x8028,
};

using TxId = std::array<uint8_t, 12>;

// --- hashing ---------------------------------------------------------------
std::array<uint8_t, 16> Md5(const uint8_t* data, size_t size);
std::array<uint8_t, 20> Sha1(const uint8_t* data, size_t size);
std::array<uint8_t, 20> HmacSha1(const uint8_t* key, size_t keySize, const uint8_t* data, size_t size);
uint32_t Crc32(const uint8_t* data, size_t size);

// MD5(username ":" realm ":" password), the TURN long-term credential key.
std::vector<uint8_t> LongTermKey(const std::string& username, const std::string& realm, const std::string& password);

// --- building --------------------------------------------------------------
class MessageBuilder
{
public:
    MessageBuilder(uint16_t method, uint16_t messageClass, const TxId& tx);

    void AddAttr(uint16_t type, const uint8_t* data, size_t size);
    void AddString(uint16_t type, const std::string& value);
    void AddU32(uint16_t type, uint32_t value);
    void AddXorAddress(uint16_t type, const Ipv4Endpoint& endpoint);
    void AddRequestedTransportUdp();
    void AddChannelNumber(uint16_t channel);
    // Must be added after every other attribute except FINGERPRINT.
    void AddIntegrity(const std::vector<uint8_t>& key);
    void AddFingerprint();

    const std::vector<uint8_t>& Bytes() const { return m_buf; }

private:
    void SetLength(size_t attributeBytes);

    std::vector<uint8_t> m_buf;
};

// --- parsing ---------------------------------------------------------------
struct Attribute
{
    uint16_t type = 0;
    size_t offset = 0; // offset of the attribute header inside the message
    size_t size = 0;   // value size (without padding)
};

class Message
{
public:
    bool Parse(const uint8_t* data, size_t size);

    uint16_t GetMethod() const { return m_method; }
    uint16_t GetClass() const { return m_class; }
    const TxId& GetTx() const { return m_tx; }

    const Attribute* Find(uint16_t type) const;
    const uint8_t* Value(const Attribute& attr) const { return m_bytes.data() + attr.offset + 4; }

    bool GetXorAddress(uint16_t type, Ipv4Endpoint& out) const;
    bool GetString(uint16_t type, std::string& out) const;
    bool GetU32(uint16_t type, uint32_t& out) const;
    bool GetErrorCode(int& code, std::string& reason) const;
    bool GetData(const uint8_t*& data, size_t& size) const;

    bool VerifyIntegrity(const std::vector<uint8_t>& key) const;
    bool VerifyFingerprint() const;

private:
    std::vector<uint8_t> m_bytes;
    std::vector<Attribute> m_attrs;
    uint16_t m_method = 0;
    uint16_t m_class = 0;
    TxId m_tx{};
};

// First two bits zero + magic cookie.
bool LooksLikeStun(const uint8_t* data, size_t size);

// --- ChannelData -----------------------------------------------------------
constexpr uint16_t kMinChannel = 0x4000;
constexpr uint16_t kMaxChannel = 0x7FFE;

bool LooksLikeChannelData(const uint8_t* data, size_t size);
std::vector<uint8_t> BuildChannelData(uint16_t channel, const uint8_t* data, size_t size);
bool ParseChannelData(const uint8_t* data, size_t size, uint16_t& channel, const uint8_t*& payload, size_t& payloadSize);

} // namespace p2p::stun
