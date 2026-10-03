#include "p2p_identity.h"

#include <atomic>
#include <chrono>
#include <cstring>

namespace p2p
{
namespace
{

uint32_t Ror(uint32_t v, int s)
{
    return (v >> s) | (v << (32 - s));
}

uint64_t ReadU64(const uint8_t* p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v = (v << 8) | p[i];
    return v;
}

int64_t NowUnix()
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

constexpr int64_t kClockSkewSeconds = 120;
constexpr int64_t kRecordIdleSeconds = 2 * 60 * 60;
constexpr uint8_t kTokenMagic[4] = { 'R', '1', 'I', 'D' };

std::atomic<bool> g_enforce{ false };

} // namespace

const char kDefaultIdentityPublicKeyHex[] =
    "3dcd5f8cf8552dcfe9add293e64bb9c4d43d98cdeff451d3b27c21c76b4f6e65"
    "4d0744c027228d9d655ff7ddc73ed6f9f96fc9009b49085834b710f20411ce36";

Sha256Digest Sha256(const uint8_t* data, size_t size)
{
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
    };
    std::vector<uint8_t> msg(data, data + size);
    msg.push_back(0x80);
    while (msg.size() % 64 != 56)
        msg.push_back(0);
    const uint64_t bits = static_cast<uint64_t>(size) * 8;
    for (int i = 7; i >= 0; --i)
        msg.push_back(static_cast<uint8_t>(bits >> (8 * i)));

    uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    for (size_t off = 0; off < msg.size(); off += 64)
    {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (static_cast<uint32_t>(msg[off + i * 4]) << 24) | (static_cast<uint32_t>(msg[off + i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(msg[off + i * 4 + 2]) << 8) | msg[off + i * 4 + 3];
        for (int i = 16; i < 64; ++i)
        {
            const uint32_t s0 = Ror(w[i - 15], 7) ^ Ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = Ror(w[i - 2], 17) ^ Ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i)
        {
            const uint32_t S1 = Ror(e, 6) ^ Ror(e, 11) ^ Ror(e, 25);
            const uint32_t ch = (e & f) ^ (~e & g);
            const uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            const uint32_t S0 = Ror(a, 2) ^ Ror(a, 13) ^ Ror(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t t2 = S0 + maj;
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    }
    Sha256Digest out{};
    for (int i = 0; i < 8; ++i)
    {
        out[i * 4] = static_cast<uint8_t>(h[i] >> 24);
        out[i * 4 + 1] = static_cast<uint8_t>(h[i] >> 16);
        out[i * 4 + 2] = static_cast<uint8_t>(h[i] >> 8);
        out[i * 4 + 3] = static_cast<uint8_t>(h[i]);
    }
    return out;
}

Sha256Digest HmacSha256(const uint8_t* key, size_t keySize, const uint8_t* data, size_t size)
{
    uint8_t k[64] = {};
    if (keySize > 64)
    {
        const auto hk = Sha256(key, keySize);
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
    const auto ih = Sha256(inner.data(), inner.size());
    uint8_t outer[64 + 32];
    for (int i = 0; i < 64; ++i)
        outer[i] = k[i] ^ 0x5c;
    std::memcpy(outer + 64, ih.data(), 32);
    return Sha256(outer, sizeof(outer));
}

bool ConstantTimeEqual(const uint8_t* a, const uint8_t* b, size_t size)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < size; ++i)
        diff |= static_cast<uint8_t>(a[i] ^ b[i]);
    return diff == 0;
}

uint64_t ServerTagFor(const std::string& listKey)
{
    const auto h = TargetHash(listKey);
    uint64_t tag = 0;
    for (int i = 0; i < 8; ++i)
        tag = (tag << 8) | h[i];
    return tag == 0 ? 1 : tag;
}

bool HexToBytes(const std::string& hex, std::vector<uint8_t>& out)
{
    if (hex.size() % 2 != 0)
        return false;
    auto nibble = [](char c, uint8_t& v) {
        if (c >= '0' && c <= '9') { v = static_cast<uint8_t>(c - '0'); return true; }
        if (c >= 'a' && c <= 'f') { v = static_cast<uint8_t>(c - 'a' + 10); return true; }
        if (c >= 'A' && c <= 'F') { v = static_cast<uint8_t>(c - 'A' + 10); return true; }
        return false;
    };
    out.clear();
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2)
    {
        uint8_t hi, lo;
        if (!nibble(hex[i], hi) || !nibble(hex[i + 1], lo))
            return false;
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return true;
}

bool ParsePublicKeyHex(const std::string& hex, P256PublicKey& out)
{
    std::vector<uint8_t> bytes;
    if (!HexToBytes(hex, bytes) || bytes.size() != out.size())
        return false;
    std::memcpy(out.data(), bytes.data(), out.size());
    return true;
}

bool ParseIdentityToken(const uint8_t* data, size_t size, IdentityToken& out)
{
    if (!data || size != kIdentityTokenSize || std::memcmp(data, kTokenMagic, 4) != 0 || data[4] != 1)
        return false;
    out.ip = (static_cast<uint32_t>(data[5]) << 24) | (static_cast<uint32_t>(data[6]) << 16) |
             (static_cast<uint32_t>(data[7]) << 8) | data[8];
    out.expires = ReadU64(data + 9);
    std::memcpy(out.nonce.data(), data + 17, 16);
    std::memcpy(out.targetHash.data(), data + 33, 32);
    std::memcpy(out.signature.data(), data + kIdentitySignedSize, 64);
    out.digest = Sha256(data, kIdentitySignedSize);
    return true;
}

const char* IdentityVerdictName(IdentityVerdict verdict)
{
    switch (verdict)
    {
    case IdentityVerdict::Accepted: return "accepted";
    case IdentityVerdict::Malformed: return "malformed";
    case IdentityVerdict::BadSignature: return "bad signature";
    case IdentityVerdict::Expired: return "expired";
    case IdentityVerdict::WrongServer: return "issued for another server";
    case IdentityVerdict::Replayed: return "already used by another peer";
    case IdentityVerdict::NoKey: return "no verification key";
    }
    return "?";
}

std::vector<uint8_t> BuildIdentify(const std::vector<uint8_t>& token)
{
    std::vector<uint8_t> b(std::begin(kNatMagic), std::end(kNatMagic));
    b.push_back(kNatVersion);
    b.push_back(static_cast<uint8_t>(PacketType::Identify));
    b.insert(b.end(), token.begin(), token.end());
    return b;
}

std::vector<uint8_t> BuildIdentifyAck(const Id16& nonce, IdentityVerdict verdict)
{
    std::vector<uint8_t> b(std::begin(kNatMagic), std::end(kNatMagic));
    b.push_back(kNatVersion);
    b.push_back(static_cast<uint8_t>(PacketType::IdentifyAck));
    b.insert(b.end(), nonce.begin(), nonce.end());
    b.push_back(static_cast<uint8_t>(verdict));
    return b;
}

bool ParseIdentifyAck(const ParsedControl& pkt, Id16& nonce, IdentityVerdict& verdict)
{
    if (pkt.type != PacketType::IdentifyAck || pkt.payloadSize < 17)
        return false;
    std::memcpy(nonce.data(), pkt.payload, 16);
    verdict = static_cast<IdentityVerdict>(pkt.payload[16]);
    return true;
}

Ipv6Bytes MappedIpv4(uint32_t ip)
{
    Ipv6Bytes a{};
    a[10] = 0xFF;
    a[11] = 0xFF;
    a[12] = static_cast<uint8_t>(ip >> 24);
    a[13] = static_cast<uint8_t>(ip >> 16);
    a[14] = static_cast<uint8_t>(ip >> 8);
    a[15] = static_cast<uint8_t>(ip);
    return a;
}

bool IsMappedIpv4(const uint8_t* addr16, uint32_t* outIp)
{
    static const uint8_t kPrefix[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF };
    if (std::memcmp(addr16, kPrefix, sizeof(kPrefix)) != 0)
        return false;
    if (outIp)
        *outIp = (static_cast<uint32_t>(addr16[12]) << 24) | (static_cast<uint32_t>(addr16[13]) << 16) |
                 (static_cast<uint32_t>(addr16[14]) << 8) | addr16[15];
    return true;
}

// ---------------------------------------------------------------------------
// IdentityTable
// ---------------------------------------------------------------------------
void IdentityTable::SetPublicKey(const P256PublicKey& key)
{
    std::lock_guard lock(m_mutex);
    m_key = key;
    m_haveKey = true;
}

void IdentityTable::SetServerTargets(const std::vector<std::string>& targets)
{
    std::lock_guard lock(m_mutex);
    m_targetNames = targets;
    m_targets.clear();
    for (const auto& t : targets)
        m_targets.push_back(TargetHash(t));
}

std::vector<std::string> IdentityTable::ServerTargets() const
{
    std::lock_guard lock(m_mutex);
    return m_targetNames;
}

void IdentityTable::PruneLocked(int64_t now)
{
    if (now - m_lastPrune < 60)
        return;
    m_lastPrune = now;
    for (auto it = m_nonces.begin(); it != m_nonces.end();)
        it = (it->second.second + kClockSkewSeconds < now) ? m_nonces.erase(it) : std::next(it);
    for (auto it = m_records.begin(); it != m_records.end();)
        it = (now - it->second.lastSeen > kRecordIdleSeconds) ? m_records.erase(it) : std::next(it);
}

IdentityVerdict IdentityTable::Present(const uint8_t* data, size_t size, const Ipv6Bytes& source, int64_t now,
                                       uint32_t* outIp)
{
    IdentityToken token;
    if (!ParseIdentityToken(data, size, token))
        return IdentityVerdict::Malformed;

    P256PublicKey key;
    std::vector<Sha256Digest> targets;
    {
        std::lock_guard lock(m_mutex);
        if (!m_haveKey)
            return IdentityVerdict::NoKey;
        key = m_key;
        targets = m_targets;
    }
    if (static_cast<int64_t>(token.expires) + kClockSkewSeconds < now)
        return IdentityVerdict::Expired;
    bool forUs = false;
    for (const auto& t : targets)
        forUs |= t == token.targetHash;
    if (!forUs)
        return IdentityVerdict::WrongServer;
    // Signature last: it is the expensive check.
    if (!VerifyP256(key, token.digest, token.signature))
        return IdentityVerdict::BadSignature;

    std::lock_guard lock(m_mutex);
    PruneLocked(now);
    auto used = m_nonces.find(token.nonce);
    if (used != m_nonces.end() && used->second.first != source)
        return IdentityVerdict::Replayed;
    m_nonces[token.nonce] = { source, static_cast<int64_t>(token.expires) };
    m_records[source] = Record{ token.ip, now };
    if (outIp)
        *outIp = token.ip;
    return IdentityVerdict::Accepted;
}

void IdentityTable::Trust(const Ipv6Bytes& source, uint32_t ip)
{
    std::lock_guard lock(m_mutex);
    m_records[source] = Record{ ip, NowUnix() };
}

bool IdentityTable::Lookup(const Ipv6Bytes& source, uint32_t& outIp)
{
    std::lock_guard lock(m_mutex);
    auto it = m_records.find(source);
    if (it == m_records.end())
        return false;
    outIp = it->second.ip;
    const int64_t now = NowUnix();
    it->second.lastSeen = now;
    return true;
}

void IdentityTable::Forget(const Ipv6Bytes& source)
{
    std::lock_guard lock(m_mutex);
    m_records.erase(source);
}

void IdentityTable::MarkOutgoing(const Ipv6Bytes& addr)
{
    std::lock_guard lock(m_mutex);
    m_outgoing.insert(addr);
}

bool IdentityTable::IsOutgoing(const Ipv6Bytes& addr) const
{
    std::lock_guard lock(m_mutex);
    return m_outgoing.count(addr) != 0;
}

size_t IdentityTable::Size() const
{
    std::lock_guard lock(m_mutex);
    return m_records.size();
}

IdentityTable& Identities()
{
    static IdentityTable table;
    return table;
}

// ---------------------------------------------------------------------------
// Hook helpers
// ---------------------------------------------------------------------------
namespace
{
// Sources whose address alone is not a usable ban key: fake transport
// addresses and private / CGNAT (LAN, Tailscale) IPv4.
bool NeedsIdentityLookup(const uint8_t* addr16)
{
    if (IsOverlayAddress(addr16) || IsEosFakeAddress(addr16))
        return true;
    uint32_t ip;
    if (!IsMappedIpv4(addr16, &ip))
        return false;
    return (ip >> 24) == 10 || (ip >> 20) == ((172u << 4) | 1u) || (ip >> 16) == ((192u << 8) | 168u) ||
           (ip & 0xFFC00000u) == 0x64400000u;
}
} // namespace

bool LookupIdentityIpv4(const uint8_t* addr16, uint32_t& outIp)
{
    if (!addr16 || !NeedsIdentityLookup(addr16))
        return false;
    Ipv6Bytes key;
    std::memcpy(key.data(), addr16, key.size());
    return Identities().Lookup(key, outIp);
}

bool ShouldDropUnidentified(const uint8_t* addr16)
{
    if (!g_enforce.load(std::memory_order_relaxed) || !addr16)
        return false;
    if (!IsOverlayAddress(addr16) && !IsEosFakeAddress(addr16))
        return false;
    Ipv6Bytes key;
    std::memcpy(key.data(), addr16, key.size());
    uint32_t ip;
    if (Identities().IsOutgoing(key) || Identities().Lookup(key, ip))
        return false;
    return true;
}

void SetIdentityEnforcement(bool enabled)
{
    g_enforce.store(enabled);
}

bool IdentityEnforced()
{
    return g_enforce.load();
}

} // namespace p2p
