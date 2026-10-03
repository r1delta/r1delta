#pragma once

// Cross-transport client identity.
//
// Peers reached through EOS, iroh, tailcat or TURN show up with fake
// addresses, so IP bans alone could be dodged by switching transport. The
// master server signs a short-lived token binding the client's real IPv4 (as
// it saw it) to one server; the client presents it over whatever route it
// picked (R1NX IDENTIFY) and the server:
//   * drops engine traffic from overlay/EOS peers that have not identified,
//   * checks IP bans against the attested address too (sv_filter.h),
//   * reports the attested address as the player's address (status, addip).
// TURN peers need no token: the relay reports their real address.
//
// Token (129 bytes): "R1ID" | 1 | IPv4[4] | expires unix[8] | nonce[16] |
// SHA-256(target)[32] | ECDSA P-256 r[32] s[32] over SHA-256 of the first 65
// bytes. target is "ip:port" (server list key), "iroh:<endpoint id>",
// "tailcat:<address>" or "eos:<puid>". See masterserver2/nat.go.

#include <array>
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "p2p_protocol.h"

namespace p2p
{

constexpr size_t kIdentityTokenSize = 129;
constexpr size_t kIdentitySignedSize = 65;

using Sha256Digest = std::array<uint8_t, 32>;
using P256PublicKey = std::array<uint8_t, 64>; // X || Y
using P256Signature = std::array<uint8_t, 64>; // r || s

Sha256Digest Sha256(const uint8_t* data, size_t size);
Sha256Digest HmacSha256(const uint8_t* key, size_t keySize, const uint8_t* data, size_t size);
bool ConstantTimeEqual(const uint8_t* a, const uint8_t* b, size_t size);
inline Sha256Digest TargetHash(const std::string& target)
{
    return Sha256(reinterpret_cast<const uint8_t*>(target.data()), target.size());
}

// Platform specific (p2p_crypto_win.cpp, devtest/crypto_openssl.cpp).
bool VerifyP256(const P256PublicKey& key, const Sha256Digest& digest, const P256Signature& signature);

// The master server's attestation key (same key that signs server auth
// tokens, see engine/auth.cpp). Overridable with delta_p2p_identity_pubkey.
extern const char kDefaultIdentityPublicKeyHex[];
bool ParsePublicKeyHex(const std::string& hex, P256PublicKey& out);

struct IdentityToken
{
    uint32_t ip = 0;
    uint64_t expires = 0;
    Id16 nonce{};
    Sha256Digest targetHash{};
    P256Signature signature{};
    Sha256Digest digest{}; // of the signed part
};
bool ParseIdentityToken(const uint8_t* data, size_t size, IdentityToken& out);
bool HexToBytes(const std::string& hex, std::vector<uint8_t>& out);

enum class IdentityVerdict : uint8_t
{
    Accepted = 0,
    Malformed = 1,
    BadSignature = 2,
    Expired = 3,
    WrongServer = 4,
    Replayed = 5,
    NoKey = 6,
};
const char* IdentityVerdictName(IdentityVerdict verdict);

std::vector<uint8_t> BuildIdentify(const std::vector<uint8_t>& token);
std::vector<uint8_t> BuildIdentifyAck(const Id16& nonce, IdentityVerdict verdict);
bool ParseIdentifyAck(const ParsedControl& pkt, Id16& nonce, IdentityVerdict& verdict);

// ::ffff:a.b.c.d, the form netadr_t and dual-stack sockets use for IPv4.
Ipv6Bytes MappedIpv4(uint32_t ip);
bool IsMappedIpv4(const uint8_t* addr16, uint32_t* outIp = nullptr);

class IdentityTable
{
public:
    void SetPublicKey(const P256PublicKey& key);
    // Every name this server can be reached by (list key, overlay ids).
    void SetServerTargets(const std::vector<std::string>& targets);
    std::vector<std::string> ServerTargets() const;

    // Verifies a token presented by `source` and records the identity.
    IdentityVerdict Present(const uint8_t* token, size_t size, const Ipv6Bytes& source, int64_t nowUnix,
                            uint32_t* outIp = nullptr);
    // Records an identity observed by trusted infrastructure (TURN relay).
    void Trust(const Ipv6Bytes& source, uint32_t ip);
    bool Lookup(const Ipv6Bytes& source, uint32_t& outIp);
    void Forget(const Ipv6Bytes& source);

    // Fake addresses this process connected out to (we are the client).
    void MarkOutgoing(const Ipv6Bytes& addr);
    bool IsOutgoing(const Ipv6Bytes& addr) const;

    size_t Size() const;

private:
    struct Record
    {
        uint32_t ip = 0;
        int64_t lastSeen = 0;
    };
    void PruneLocked(int64_t nowUnix);

    mutable std::mutex m_mutex;
    bool m_haveKey = false;
    P256PublicKey m_key{};
    std::vector<Sha256Digest> m_targets;
    std::vector<std::string> m_targetNames;
    std::map<Ipv6Bytes, Record> m_records;
    std::map<Id16, std::pair<Ipv6Bytes, int64_t>> m_nonces; // nonce -> (first presenter, expiry)
    std::set<Ipv6Bytes> m_outgoing;
    int64_t m_lastPrune = 0;
};

IdentityTable& Identities();

// Hook helpers (cheap for ordinary public IPv4 sources).
// Attested IPv4 of a fake/private source address, if known.
bool LookupIdentityIpv4(const uint8_t* addr16, uint32_t& outIp);
// Whether engine traffic from `addr16` must be dropped because it arrived
// over an overlay/EOS route without a valid identity.
bool ShouldDropUnidentified(const uint8_t* addr16);
void SetIdentityEnforcement(bool enabled);
bool IdentityEnforced();

} // namespace p2p
