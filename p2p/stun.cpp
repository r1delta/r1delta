#include "stun.h"

#include <cstring>

namespace p2p::stun
{
namespace
{

uint32_t Rol(uint32_t v, int s)
{
    return (v << s) | (v >> (32 - s));
}

void PutU16(uint8_t* p, uint16_t v)
{
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v);
}

void PutU32(uint8_t* p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}

uint16_t GetU16(const uint8_t* p)
{
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

uint32_t ReadBE32(const uint8_t* p)
{
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

size_t Pad4(size_t n)
{
    return (n + 3) & ~static_cast<size_t>(3);
}

} // namespace

// ---------------------------------------------------------------------------
// MD5 (RFC 1321)
// ---------------------------------------------------------------------------
std::array<uint8_t, 16> Md5(const uint8_t* data, size_t size)
{
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
    };
    static const int S[64] = {
        7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
        5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
        4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
        6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
    };

    std::vector<uint8_t> msg(data, data + size);
    msg.push_back(0x80);
    while (msg.size() % 64 != 56)
        msg.push_back(0);
    const uint64_t bits = static_cast<uint64_t>(size) * 8;
    for (int i = 0; i < 8; ++i)
        msg.push_back(static_cast<uint8_t>(bits >> (8 * i)));

    uint32_t a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;
    for (size_t off = 0; off < msg.size(); off += 64)
    {
        uint32_t M[16];
        for (int i = 0; i < 16; ++i)
            M[i] = static_cast<uint32_t>(msg[off + i * 4]) | (static_cast<uint32_t>(msg[off + i * 4 + 1]) << 8) |
                   (static_cast<uint32_t>(msg[off + i * 4 + 2]) << 16) | (static_cast<uint32_t>(msg[off + i * 4 + 3]) << 24);
        uint32_t A = a0, B = b0, C = c0, D = d0;
        for (int i = 0; i < 64; ++i)
        {
            uint32_t F;
            int g;
            if (i < 16) { F = (B & C) | (~B & D); g = i; }
            else if (i < 32) { F = (D & B) | (~D & C); g = (5 * i + 1) % 16; }
            else if (i < 48) { F = B ^ C ^ D; g = (3 * i + 5) % 16; }
            else { F = C ^ (B | ~D); g = (7 * i) % 16; }
            F = F + A + K[i] + M[g];
            A = D;
            D = C;
            C = B;
            B = B + Rol(F, S[i]);
        }
        a0 += A;
        b0 += B;
        c0 += C;
        d0 += D;
    }

    std::array<uint8_t, 16> out{};
    const uint32_t words[4] = { a0, b0, c0, d0 };
    for (int w = 0; w < 4; ++w)
        for (int i = 0; i < 4; ++i)
            out[w * 4 + i] = static_cast<uint8_t>(words[w] >> (8 * i));
    return out;
}

// ---------------------------------------------------------------------------
// SHA-1 (FIPS 180-1)
// ---------------------------------------------------------------------------
std::array<uint8_t, 20> Sha1(const uint8_t* data, size_t size)
{
    std::vector<uint8_t> msg(data, data + size);
    msg.push_back(0x80);
    while (msg.size() % 64 != 56)
        msg.push_back(0);
    const uint64_t bits = static_cast<uint64_t>(size) * 8;
    for (int i = 7; i >= 0; --i)
        msg.push_back(static_cast<uint8_t>(bits >> (8 * i)));

    uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
    for (size_t off = 0; off < msg.size(); off += 64)
    {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = ReadBE32(&msg[off + i * 4]);
        for (int i = 16; i < 80; ++i)
            w[i] = Rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i)
        {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            const uint32_t t = Rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = Rol(b, 30);
            b = a;
            a = t;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }
    std::array<uint8_t, 20> out{};
    for (int i = 0; i < 5; ++i)
        PutU32(&out[i * 4], h[i]);
    return out;
}

std::array<uint8_t, 20> HmacSha1(const uint8_t* key, size_t keySize, const uint8_t* data, size_t size)
{
    uint8_t k[64] = {};
    if (keySize > 64)
    {
        const auto hk = Sha1(key, keySize);
        std::memcpy(k, hk.data(), hk.size());
    }
    else if (keySize)
    {
        std::memcpy(k, key, keySize);
    }

    std::vector<uint8_t> inner(64 + size);
    for (int i = 0; i < 64; ++i)
        inner[i] = k[i] ^ 0x36;
    if (size)
        std::memcpy(inner.data() + 64, data, size);
    const auto innerHash = Sha1(inner.data(), inner.size());

    uint8_t outer[64 + 20];
    for (int i = 0; i < 64; ++i)
        outer[i] = k[i] ^ 0x5c;
    std::memcpy(outer + 64, innerHash.data(), 20);
    return Sha1(outer, sizeof(outer));
}

uint32_t Crc32(const uint8_t* data, size_t size)
{
    static uint32_t table[256];
    static bool init = [] {
        for (uint32_t i = 0; i < 256; ++i)
        {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        return true;
    }();
    (void)init;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i)
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

std::vector<uint8_t> LongTermKey(const std::string& username, const std::string& realm, const std::string& password)
{
    const std::string s = username + ":" + realm + ":" + password;
    const auto h = Md5(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    return std::vector<uint8_t>(h.begin(), h.end());
}

// ---------------------------------------------------------------------------
// MessageBuilder
// ---------------------------------------------------------------------------
MessageBuilder::MessageBuilder(uint16_t method, uint16_t messageClass, const TxId& tx)
{
    m_buf.resize(kHeaderSize);
    PutU16(&m_buf[0], static_cast<uint16_t>(method | messageClass));
    PutU16(&m_buf[2], 0);
    PutU32(&m_buf[4], kMagicCookie);
    std::memcpy(&m_buf[8], tx.data(), tx.size());
}

void MessageBuilder::SetLength(size_t attributeBytes)
{
    PutU16(&m_buf[2], static_cast<uint16_t>(attributeBytes));
}

void MessageBuilder::AddAttr(uint16_t type, const uint8_t* data, size_t size)
{
    const size_t start = m_buf.size();
    m_buf.resize(start + 4 + Pad4(size), 0);
    PutU16(&m_buf[start], type);
    PutU16(&m_buf[start + 2], static_cast<uint16_t>(size));
    if (size)
        std::memcpy(&m_buf[start + 4], data, size);
    SetLength(m_buf.size() - kHeaderSize);
}

void MessageBuilder::AddString(uint16_t type, const std::string& value)
{
    AddAttr(type, reinterpret_cast<const uint8_t*>(value.data()), value.size());
}

void MessageBuilder::AddU32(uint16_t type, uint32_t value)
{
    uint8_t b[4];
    PutU32(b, value);
    AddAttr(type, b, 4);
}

void MessageBuilder::AddXorAddress(uint16_t type, const Ipv4Endpoint& endpoint)
{
    uint8_t b[8] = { 0, 0x01 };
    PutU16(b + 2, static_cast<uint16_t>(endpoint.port ^ (kMagicCookie >> 16)));
    PutU32(b + 4, endpoint.ip ^ kMagicCookie);
    AddAttr(type, b, sizeof(b));
}

void MessageBuilder::AddRequestedTransportUdp()
{
    const uint8_t b[4] = { 17, 0, 0, 0 };
    AddAttr(kRequestedTransport, b, sizeof(b));
}

void MessageBuilder::AddChannelNumber(uint16_t channel)
{
    uint8_t b[4] = {};
    PutU16(b, channel);
    AddAttr(kChannelNumber, b, sizeof(b));
}

void MessageBuilder::AddIntegrity(const std::vector<uint8_t>& key)
{
    const size_t start = m_buf.size();
    // The length field must already count the MESSAGE-INTEGRITY attribute.
    SetLength(start + 24 - kHeaderSize);
    const auto mac = HmacSha1(key.data(), key.size(), m_buf.data(), start);
    AddAttr(kMessageIntegrity, mac.data(), mac.size());
}

void MessageBuilder::AddFingerprint()
{
    const size_t start = m_buf.size();
    SetLength(start + 8 - kHeaderSize);
    const uint32_t crc = Crc32(m_buf.data(), start) ^ 0x5354554Eu;
    AddU32(kFingerprint, crc);
}

// ---------------------------------------------------------------------------
// Message
// ---------------------------------------------------------------------------
bool LooksLikeStun(const uint8_t* data, size_t size)
{
    return data && size >= kHeaderSize && (data[0] & 0xC0) == 0 && ReadBE32(data + 4) == kMagicCookie &&
           (GetU16(data + 2) & 3) == 0;
}

bool Message::Parse(const uint8_t* data, size_t size)
{
    if (!LooksLikeStun(data, size))
        return false;
    const size_t length = GetU16(data + 2);
    if (kHeaderSize + length > size)
        return false;

    m_bytes.assign(data, data + kHeaderSize + length);
    m_attrs.clear();
    const uint16_t type = GetU16(data);
    m_class = type & 0x0110;
    m_method = type & ~0x0110 & 0x3FFF;
    std::memcpy(m_tx.data(), data + 8, m_tx.size());

    size_t off = kHeaderSize;
    while (off + 4 <= m_bytes.size())
    {
        Attribute a;
        a.type = GetU16(&m_bytes[off]);
        a.size = GetU16(&m_bytes[off + 2]);
        a.offset = off;
        if (off + 4 + a.size > m_bytes.size())
            return false;
        m_attrs.push_back(a);
        off += 4 + Pad4(a.size);
    }
    return true;
}

const Attribute* Message::Find(uint16_t type) const
{
    for (const auto& a : m_attrs)
        if (a.type == type)
            return &a;
    return nullptr;
}

bool Message::GetXorAddress(uint16_t type, Ipv4Endpoint& out) const
{
    const Attribute* a = Find(type);
    if (!a || a->size < 8)
        return false;
    const uint8_t* v = Value(*a);
    if (v[1] != 0x01)
        return false; // IPv6 relays are not used
    out.port = static_cast<uint16_t>(GetU16(v + 2) ^ (kMagicCookie >> 16));
    out.ip = ReadBE32(v + 4) ^ kMagicCookie;
    return true;
}

bool Message::GetString(uint16_t type, std::string& out) const
{
    const Attribute* a = Find(type);
    if (!a)
        return false;
    out.assign(reinterpret_cast<const char*>(Value(*a)), a->size);
    return true;
}

bool Message::GetU32(uint16_t type, uint32_t& out) const
{
    const Attribute* a = Find(type);
    if (!a || a->size < 4)
        return false;
    out = ReadBE32(Value(*a));
    return true;
}

bool Message::GetErrorCode(int& code, std::string& reason) const
{
    const Attribute* a = Find(kErrorCode);
    if (!a || a->size < 4)
        return false;
    const uint8_t* v = Value(*a);
    code = (v[2] & 0x7) * 100 + v[3];
    reason.assign(reinterpret_cast<const char*>(v + 4), a->size - 4);
    return true;
}

bool Message::GetData(const uint8_t*& data, size_t& size) const
{
    const Attribute* a = Find(kDataAttr);
    if (!a)
        return false;
    data = Value(*a);
    size = a->size;
    return true;
}

bool Message::VerifyIntegrity(const std::vector<uint8_t>& key) const
{
    const Attribute* a = Find(kMessageIntegrity);
    if (!a || a->size != 20)
        return false;
    std::vector<uint8_t> prefix(m_bytes.begin(), m_bytes.begin() + a->offset);
    PutU16(&prefix[2], static_cast<uint16_t>(a->offset + 24 - kHeaderSize));
    const auto mac = HmacSha1(key.data(), key.size(), prefix.data(), prefix.size());
    return std::memcmp(mac.data(), Value(*a), 20) == 0;
}

bool Message::VerifyFingerprint() const
{
    const Attribute* a = Find(kFingerprint);
    if (!a || a->size != 4)
        return false;
    std::vector<uint8_t> prefix(m_bytes.begin(), m_bytes.begin() + a->offset);
    PutU16(&prefix[2], static_cast<uint16_t>(a->offset + 8 - kHeaderSize));
    const uint32_t crc = Crc32(prefix.data(), prefix.size()) ^ 0x5354554Eu;
    return crc == ReadBE32(Value(*a));
}

// ---------------------------------------------------------------------------
// ChannelData
// ---------------------------------------------------------------------------
bool LooksLikeChannelData(const uint8_t* data, size_t size)
{
    return data && size >= 4 && data[0] >= 0x40 && data[0] <= 0x7F;
}

std::vector<uint8_t> BuildChannelData(uint16_t channel, const uint8_t* data, size_t size)
{
    std::vector<uint8_t> b(4 + size);
    PutU16(&b[0], channel);
    PutU16(&b[2], static_cast<uint16_t>(size));
    if (size)
        std::memcpy(&b[4], data, size);
    return b;
}

bool ParseChannelData(const uint8_t* data, size_t size, uint16_t& channel, const uint8_t*& payload, size_t& payloadSize)
{
    if (!LooksLikeChannelData(data, size))
        return false;
    channel = GetU16(data);
    payloadSize = GetU16(data + 2);
    if (4 + payloadSize > size)
        return false;
    payload = data + 4;
    return true;
}

} // namespace p2p::stun
