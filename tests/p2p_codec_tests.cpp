#include "../p2p/p2p_protocol.h"
#include "../p2p/stun.h"
#include "../p2p/upnp_codec.h"
#include "../p2p/p2p_identity.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
int g_failures = 0;

void Check(bool condition, const char* what)
{
    if (!condition)
    {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

std::string Hex(const uint8_t* data, size_t size)
{
    static const char k[] = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < size; ++i)
    {
        s += k[data[i] >> 4];
        s += k[data[i] & 0xF];
    }
    return s;
}

std::vector<uint8_t> Bytes(std::initializer_list<unsigned> list)
{
    std::vector<uint8_t> v;
    for (unsigned b : list)
        v.push_back(static_cast<uint8_t>(b));
    return v;
}

void TestHashes()
{
    using namespace p2p::stun;
    const auto md5Empty = Md5(nullptr, 0);
    Check(Hex(md5Empty.data(), 16) == "d41d8cd98f00b204e9800998ecf8427e", "md5 empty");
    const std::string fox = "The quick brown fox jumps over the lazy dog";
    const auto md5Fox = Md5(reinterpret_cast<const uint8_t*>(fox.data()), fox.size());
    Check(Hex(md5Fox.data(), 16) == "9e107d9d372bb6826bd81d3542a419d6", "md5 fox");

    const auto sha = Sha1(reinterpret_cast<const uint8_t*>("abc"), 3);
    Check(Hex(sha.data(), 20) == "a9993e364706816aba3e25717850c26c9cd0d89d", "sha1 abc");
    const std::string longMsg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    const auto sha2 = Sha1(reinterpret_cast<const uint8_t*>(longMsg.data()), longMsg.size());
    Check(Hex(sha2.data(), 20) == "84983e441c3bd26ebaae4aa1f95129e5e54670f1", "sha1 two blocks");

    std::vector<uint8_t> key(20, 0x0b);
    const auto mac = HmacSha1(key.data(), key.size(), reinterpret_cast<const uint8_t*>("Hi There"), 8);
    Check(Hex(mac.data(), 20) == "b617318655057264e28bc0b6fb378c8ef146be00", "hmac-sha1 rfc2202 #1");
    std::vector<uint8_t> bigKey(80, 0xaa);
    const std::string data6 = "Test Using Larger Than Block-Size Key - Hash Key First";
    const auto mac6 = HmacSha1(bigKey.data(), bigKey.size(), reinterpret_cast<const uint8_t*>(data6.data()), data6.size());
    Check(Hex(mac6.data(), 20) == "aa4ae5e15272d00e95705637ce8a3b55ed402112", "hmac-sha1 rfc2202 #6");

    Check(Crc32(reinterpret_cast<const uint8_t*>("123456789"), 9) == 0xCBF43926u, "crc32 check value");
}

// RFC 5769 section 2.1: sample request.
void TestRfc5769Request()
{
    using namespace p2p::stun;
    const auto msg = Bytes({
        0x00, 0x01, 0x00, 0x58, 0x21, 0x12, 0xa4, 0x42, 0xb7, 0xe7, 0xa7, 0x01, 0xbc, 0x34, 0xd6, 0x86,
        0xfa, 0x87, 0xdf, 0xae, 0x80, 0x22, 0x00, 0x10, 0x53, 0x54, 0x55, 0x4e, 0x20, 0x74, 0x65, 0x73,
        0x74, 0x20, 0x63, 0x6c, 0x69, 0x65, 0x6e, 0x74, 0x00, 0x24, 0x00, 0x04, 0x6e, 0x00, 0x01, 0xff,
        0x80, 0x29, 0x00, 0x08, 0x93, 0x2f, 0xf9, 0xb1, 0x51, 0x26, 0x3b, 0x36, 0x00, 0x06, 0x00, 0x09,
        0x65, 0x76, 0x74, 0x6a, 0x3a, 0x68, 0x36, 0x76, 0x59, 0x20, 0x20, 0x20, 0x00, 0x08, 0x00, 0x14,
        0x9a, 0xea, 0xa7, 0x0c, 0xbf, 0xd8, 0xcb, 0x56, 0x78, 0x1e, 0xf2, 0xb5, 0xb2, 0xd3, 0xf2, 0x49,
        0xc1, 0xb5, 0x71, 0xa2, 0x80, 0x28, 0x00, 0x04, 0xe5, 0x7a, 0x3b, 0xcf,
    });
    Message m;
    Check(m.Parse(msg.data(), msg.size()), "rfc5769 request parses");
    Check(m.GetMethod() == kBinding && m.GetClass() == kRequest, "rfc5769 request type");
    std::string user;
    Check(m.GetString(kUsername, user) && user == "evtj:h6vY", "rfc5769 username");
    const std::string pw = "VOkJxbRl1RmTxUk/WvJxBt";
    Check(m.VerifyIntegrity(std::vector<uint8_t>(pw.begin(), pw.end())), "rfc5769 request integrity");
    Check(!m.VerifyIntegrity(std::vector<uint8_t>(pw.begin(), pw.end() - 1)), "rfc5769 wrong key rejected");
    Check(m.VerifyFingerprint(), "rfc5769 request fingerprint");
}

// RFC 5769 section 2.2: sample IPv4 response.
void TestRfc5769Response()
{
    using namespace p2p::stun;
    const auto msg = Bytes({
        0x01, 0x01, 0x00, 0x3c, 0x21, 0x12, 0xa4, 0x42, 0xb7, 0xe7, 0xa7, 0x01, 0xbc, 0x34, 0xd6, 0x86,
        0xfa, 0x87, 0xdf, 0xae, 0x80, 0x22, 0x00, 0x0b, 0x74, 0x65, 0x73, 0x74, 0x20, 0x76, 0x65, 0x63,
        0x74, 0x6f, 0x72, 0x20, 0x00, 0x20, 0x00, 0x08, 0x00, 0x01, 0xa1, 0x47, 0xe1, 0x12, 0xa6, 0x43,
        0x00, 0x08, 0x00, 0x14, 0x2b, 0x91, 0xf5, 0x99, 0xfd, 0x9e, 0x90, 0xc3, 0x8c, 0x74, 0x89, 0xf9,
        0x2a, 0xf9, 0xba, 0x53, 0xf0, 0x6b, 0xe7, 0xd7, 0x80, 0x28, 0x00, 0x04, 0xc0, 0x7d, 0x4c, 0x96,
    });
    Message m;
    Check(m.Parse(msg.data(), msg.size()), "rfc5769 response parses");
    Check(m.GetMethod() == kBinding && m.GetClass() == kSuccess, "rfc5769 response type");
    p2p::Ipv4Endpoint ep;
    Check(m.GetXorAddress(kXorMappedAddress, ep), "rfc5769 xor-mapped present");
    Check(ep.ToString() == "192.0.2.1:32853", "rfc5769 xor-mapped value");
    const std::string pw = "VOkJxbRl1RmTxUk/WvJxBt";
    Check(m.VerifyIntegrity(std::vector<uint8_t>(pw.begin(), pw.end())), "rfc5769 response integrity");
    Check(m.VerifyFingerprint(), "rfc5769 response fingerprint");
}

void TestBuilderRoundTrip()
{
    using namespace p2p::stun;
    TxId tx{};
    for (size_t i = 0; i < tx.size(); ++i)
        tx[i] = static_cast<uint8_t>(i * 7);
    const auto key = LongTermKey("user", "realm", "pass");
    MessageBuilder b(kCreatePermission, kRequest, tx);
    p2p::Ipv4Endpoint peer{ 0x01020304, 4242 };
    b.AddXorAddress(kXorPeerAddress, peer);
    b.AddString(kUsername, "user");
    b.AddString(kRealm, "realm");
    b.AddString(kNonce, "abc");
    b.AddIntegrity(key);
    b.AddFingerprint();

    Message m;
    Check(m.Parse(b.Bytes().data(), b.Bytes().size()), "builder output parses");
    Check(m.GetMethod() == kCreatePermission && m.GetClass() == kRequest && m.GetTx() == tx, "builder header");
    p2p::Ipv4Endpoint got;
    Check(m.GetXorAddress(kXorPeerAddress, got) && got == peer, "builder xor-peer");
    Check(m.VerifyIntegrity(key), "builder integrity");
    Check(m.VerifyFingerprint(), "builder fingerprint");

    const uint8_t payload[] = { 1, 2, 3, 4, 5 };
    const auto cd = BuildChannelData(0x4001, payload, sizeof(payload));
    uint16_t ch = 0;
    const uint8_t* p = nullptr;
    size_t n = 0;
    Check(ParseChannelData(cd.data(), cd.size(), ch, p, n) && ch == 0x4001 && n == 5 && p[4] == 5, "channeldata");
    Check(!LooksLikeStun(cd.data(), cd.size()), "channeldata is not stun");
}

void TestControlPackets()
{
    using namespace p2p;
    const auto ping = BuildPing(0x1122334455667788ull, 99);
    ParsedControl pc;
    PingPong pp;
    Check(ParseControl(ping.data(), ping.size(), pc) && pc.type == PacketType::Ping, "ping header");
    Check(ParsePingPong(pc, pp) && pp.probeId == 0x1122334455667788ull && pp.timestampUs == 99, "ping payload");
    const auto pong = BuildPong(5, 6, 7);
    Check(ParseControl(pong.data(), pong.size(), pc) && ParsePingPong(pc, pp) && pp.flags == 7, "pong");

    const uint8_t engineChallenge[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0x48, 'c', 'o', 'n', 'n', 'e', 'c', 't' };
    Check(!IsControlPacket(engineChallenge, sizeof(engineChallenge)), "engine packet not control");

    // Master -> server punch request as produced by masterserver2/nat.go.
    const auto req = Bytes({ 0xFF, 0xFF, 0xFF, 0xFF, 'R', '1', 'N', 'X', 1, 0x04,
                             0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                             1, 2, 3, 4, 0x13, 0x88 });
    PunchRequest pr;
    Check(ParseControl(req.data(), req.size(), pc) && ParsePunchRequest(pc, pr) && !pr.haveMac, "punch request parses");
    Check(pr.client.ToString() == "1.2.3.4:5000" && pr.ticket[15] == 15, "punch request fields");

    const auto ack = Bytes({ 0xFF, 0xFF, 0xFF, 0xFF, 'R', '1', 'N', 'X', 1, 0x02,
                             9, 8, 7, 6, 0x00, 0x50, 0x01, 5, 6, 7, 8, 0x93, 0xA7 });
    RegisterAck ra;
    Check(ParseControl(ack.data(), ack.size(), pc) && ParseRegisterAck(pc, ra), "register ack parses");
    Check(ra.observed.ToString() == "9.8.7.6:80" && ra.server && ra.server->ToString() == "5.6.7.8:37799", "register ack fields");

    const auto reqAck = BuildPunchReqAck(pr.ticket, pr.client);
    Check(reqAck.size() == kNatHeaderSize + 22 && reqAck[kNatHeaderSize + 16] == 1 && reqAck.back() == 0x88, "punch request ack carries client");

    Id16 id{};
    Check(HexToId16("000102030405060708090a0b0c0d0e0f", id) && id[10] == 10, "hex id");
    Check(Id16ToHex(id) == "000102030405060708090a0b0c0d0e0f", "id hex");
    Check(Ipv4Endpoint::Parse("10.0.0.1:37015").value().port == 37015, "endpoint parse");
    Check(!Ipv4Endpoint::Parse("10.0.0.1").has_value(), "endpoint parse rejects missing port");
    Check(!Ipv4Endpoint::Parse("256.0.0.1:1").has_value(), "endpoint parse rejects big octet");
    Check(!Ipv4Endpoint::Parse("1.2.3.4:70000").has_value(), "endpoint parse rejects big port");
    Check(!Ipv4Endpoint::Parse("1.2.3.4:5x").has_value(), "endpoint parse rejects trailing junk");
    Check(!Ipv4Endpoint::Parse("1..3.4:5").has_value(), "endpoint parse rejects empty octet");
    Check(Ipv4Endpoint::Parse("255.255.255.255:65535").value().ip == 0xFFFFFFFFu, "endpoint parse max");
}

void TestOverlayAddressAndFraming()
{
    using namespace p2p;
    const auto addr = EncodeOverlayAddress(Backend::Tailcat, 0x0102030405060708ull);
    Backend backend;
    uint64_t peer;
    Check(DecodeOverlayAddress(addr, backend, peer) && backend == Backend::Tailcat && peer == 0x0102030405060708ull, "overlay addr");
    Check(FormatIpv6Connect(addr, 37015) == "[3ffd:2::102:304:506:708]:37015", "overlay connect string");
    Check(FormatIpv6Connect(EncodeOverlayAddress(Backend::Iroh, 5), 1) == "[3ffd:1::5]:1", "overlay connect string short");

    uint16_t msgId = 1;
    std::vector<uint8_t> big(3000);
    for (size_t i = 0; i < big.size(); ++i)
        big[i] = static_cast<uint8_t>(i * 31);
    auto frames = FrameDatagram(big.data(), big.size(), 1200, msgId);
    Check(frames.size() == 3, "fragment count");
    Reassembler r;
    std::vector<uint8_t> out;
    // Deliver out of order with a duplicate.
    Check(!r.Push(frames[2].data(), frames[2].size(), out), "frag 3 incomplete");
    Check(!r.Push(frames[0].data(), frames[0].size(), out), "frag 1 incomplete");
    Check(!r.Push(frames[0].data(), frames[0].size(), out), "dup ignored");
    Check(r.Push(frames[1].data(), frames[1].size(), out) && out == big, "reassembled");
    Check(r.PendingCount() == 0, "no pending after reassembly");

    const uint8_t small[] = { 9, 9, 9 };
    frames = FrameDatagram(small, sizeof(small), 1200, msgId);
    Check(frames.size() == 1 && frames[0].size() == 4, "whole frame");
    Check(r.Push(frames[0].data(), frames[0].size(), out) && out.size() == 3, "whole frame decode");

    // Stale fragments expire.
    frames = FrameDatagram(big.data(), big.size(), 1200, msgId);
    const auto t0 = Reassembler::Clock::now();
    r.Push(frames[0].data(), frames[0].size(), out, t0);
    r.Push(frames[1].data(), frames[1].size(), out, t0 + std::chrono::seconds(5));
    Check(r.PendingCount() == 1, "stale fragment expired");
}

void TestUpnp()
{
    using namespace p2p::upnp;
    const std::string ssdp =
        "HTTP/1.1 200 OK\r\nCACHE-CONTROL: max-age=120\r\nST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n"
        "USN: uuid:1::urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\nEXT:\r\nSERVER: Router UPnP/1.0\r\n"
        "location: http://192.168.1.1:5000/rootDesc.xml \r\n\r\n";
    const auto loc = ParseSsdpLocation(ssdp);
    Check(loc && *loc == "http://192.168.1.1:5000/rootDesc.xml", "ssdp location");
    Check(BuildMSearch("ssdp:all").find("MAN: \"ssdp:discover\"") != std::string::npos, "msearch");

    const std::string desc = R"(<?xml version="1.0"?>
<root xmlns="urn:schemas-upnp-org:device-1-0">
 <specVersion><major>1</major><minor>0</minor></specVersion>
 <device>
  <deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:1</deviceType>
  <serviceList><service>
    <serviceType>urn:schemas-upnp-org:service:Layer3Forwarding:1</serviceType>
    <controlURL>/ctl/L3F</controlURL>
  </service></serviceList>
  <deviceList><device>
   <deviceType>urn:schemas-upnp-org:device:WANDevice:1</deviceType>
   <deviceList><device>
    <deviceType>urn:schemas-upnp-org:device:WANConnectionDevice:1</deviceType>
    <serviceList>
     <service>
      <serviceType>urn:schemas-upnp-org:service:WANPPPConnection:1</serviceType>
      <controlURL>/ctl/PPP</controlURL>
     </service>
     <service>
      <serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType>
      <serviceId>urn:upnp-org:serviceId:WANIPConn1</serviceId>
      <controlURL>ctl/IPConn</controlURL>
     </service>
    </serviceList>
   </device></deviceList>
  </device></deviceList>
 </device>
</root>)";
    const auto svc = FindWanService(desc, "http://192.168.1.1:5000/rootDesc.xml");
    Check(svc && svc->serviceType == "urn:schemas-upnp-org:service:WANIPConnection:1", "upnp prefers WANIPConnection");
    Check(svc && svc->controlUrl == "http://192.168.1.1:5000/ctl/IPConn", "upnp relative control url");

    const auto body = BuildAddPortMapping(svc->serviceType, 37015, "UDP", 37015, "192.168.1.20", "R1Delta <server>", 3600);
    Check(body.find("<NewExternalPort>37015</NewExternalPort>") != std::string::npos, "soap external port");
    Check(body.find("R1Delta &lt;server&gt;") != std::string::npos, "soap escapes description");
    Check(SoapAction(svc->serviceType, "AddPortMapping") == "\"urn:schemas-upnp-org:service:WANIPConnection:1#AddPortMapping\"", "soap action");

    const std::string ext = R"(<?xml version="1.0"?><s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"><s:Body>
<u:GetExternalIPAddressResponse xmlns:u="urn:schemas-upnp-org:service:WANIPConnection:1">
<NewExternalIPAddress>203.0.113.7</NewExternalIPAddress></u:GetExternalIPAddressResponse></s:Body></s:Envelope>)";
    const auto ip = XmlValue(ext, "NewExternalIPAddress");
    Check(ip && *ip == "203.0.113.7", "soap external ip");
    const std::string fault = R"(<s:Envelope><s:Body><s:Fault><faultcode>s:Client</faultcode><detail><UPnPError xmlns="urn:schemas-upnp-org:control-1-0"><errorCode>725</errorCode><errorDescription>OnlyPermanentLeasesSupported</errorDescription></UPnPError></detail></s:Fault></s:Body></s:Envelope>)";
    Check(SoapErrorCode(fault) == 725, "soap error code");
    Check(SoapErrorCode(ext) == 0, "soap no error");

    Check(ParseUrl("http://[fe80::1]:80/x") .has_value(), "url ipv6 host");
    Check(!ParseUrl("https://x/").has_value(), "url rejects https");
    const auto u = ParseUrl("http://10.0.0.1/desc/root.xml");
    Check(u && u->port == 80 && ResolveUrl(*u, "ctl") == "http://10.0.0.1:80/desc/ctl", "url resolve relative dir");

    const auto req = BuildNatPmpMapUdpRequest(37015, 37015, 7200);
    Check(req.size() == 12 && req[1] == 1 && req[4] == 0x90 && req[5] == 0x97, "natpmp map request");
    const uint8_t mapResp[] = { 0, 129, 0, 0, 0, 0, 0, 5, 0x90, 0x97, 0x90, 0x98, 0, 0, 0x1C, 0x20 };
    const auto r = ParseNatPmpResponse(mapResp, sizeof(mapResp));
    Check(r && r->result == 0 && r->mappedPort == 37016 && r->lifetime == 7200, "natpmp map response");
    const uint8_t extResp[] = { 0, 128, 0, 0, 0, 0, 0, 5, 203, 0, 113, 9 };
    const auto e = ParseNatPmpResponse(extResp, sizeof(extResp));
    Check(e && e->externalIp == 0xCB007109u, "natpmp external address");
}

void TestIdentity()
{
    using namespace p2p;
    const auto e = Sha256(nullptr, 0);
    Check(Hex(e.data(), 32) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "sha256 empty");
    const std::string abc = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    const auto h = Sha256(reinterpret_cast<const uint8_t*>(abc.data()), abc.size());
    Check(Hex(h.data(), 32) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "sha256 two blocks");

    const std::string jefe = "Jefe", what = "what do ya want for nothing?";
    const auto mac = HmacSha256(reinterpret_cast<const uint8_t*>(jefe.data()), jefe.size(),
                                reinterpret_cast<const uint8_t*>(what.data()), what.size());
    Check(Hex(mac.data(), 32) == "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843", "hmac-sha256 rfc4231 #2");

    P256PublicKey defaultKey;
    Check(ParsePublicKeyHex(kDefaultIdentityPublicKeyHex, defaultKey), "default identity key parses");

    // Vector produced by the master server's signing code with a test-only key.
    P256PublicKey key;
    Check(ParsePublicKeyHex("4ff841ed51da467d1330208eeb759f793c670ffb0437868083a3c65601f0d4fa"
                            "4f1adb950eff900d7e9b1e9864d08f0510ac0165dbb79419b78a65c43cba66bc", key), "test key parses");
    std::vector<uint8_t> token;
    Check(HexToBytes("5231494401cb00712d00000000713fb30000112233445566778899aabbccddeeff308fa6b4b73b759ab781fccadf8e5e1cc55c0266725105b3bc4e44ebebeaeef6ff940254394ff90fc423f8f90e8e0ea758e9ad08f91a48ba39eb3863e366c6a559d4174e5f77eca0b7bcb3861da7c41beec2f5895f790e58ada37ddd94a61e3b", token), "token hex");
    IdentityToken parsed;
    Check(ParseIdentityToken(token.data(), token.size(), parsed), "token parses");
    Check(parsed.ip == 0xCB00712Du && parsed.expires == 1900000000ull && parsed.nonce[1] == 0x11, "token fields");
    Check(parsed.targetHash == TargetHash("198.51.100.7:37015"), "token target hash");
    Check(VerifyP256(key, parsed.digest, parsed.signature), "token signature verifies");
    Check(!VerifyP256(defaultKey, parsed.digest, parsed.signature), "token rejected under another key");
    auto tampered = parsed;
    tampered.digest[0] ^= 1;
    Check(!VerifyP256(key, tampered.digest, parsed.signature), "tampered token rejected");

    IdentityTable table;
    const Ipv6Bytes peerA = EncodeOverlayAddress(Backend::Iroh, 7);
    const Ipv6Bytes peerB = EncodeOverlayAddress(Backend::Tailcat, 9);
    const int64_t now = 1899999000;
    Check(table.Present(token.data(), token.size(), peerA, now) == IdentityVerdict::NoKey, "no key -> rejected");
    table.SetPublicKey(key);
    table.SetServerTargets({ "iroh:someotherserver" });
    Check(table.Present(token.data(), token.size(), peerA, now) == IdentityVerdict::WrongServer, "wrong server rejected");
    table.SetServerTargets({ "198.51.100.7:37015", "iroh:abc" });
    Check(table.Present(token.data(), token.size(), peerA, 1900001000) == IdentityVerdict::Expired, "expired rejected");
    uint32_t ip = 0;
    Check(table.Present(token.data(), token.size(), peerA, now, &ip) == IdentityVerdict::Accepted && ip == 0xCB00712Du, "valid token accepted");
    Check(table.Present(token.data(), token.size(), peerA, now) == IdentityVerdict::Accepted, "same peer may re-present");
    Check(table.Present(token.data(), token.size(), peerB, now) == IdentityVerdict::Replayed, "replay from another peer rejected");
    Check(table.Lookup(peerA, ip) && ip == 0xCB00712Du && !table.Lookup(peerB, ip), "lookup");
    auto bad = token;
    bad[20] ^= 0x40; // nonce byte: signature no longer matches
    Check(table.Present(bad.data(), bad.size(), peerB, now) == IdentityVerdict::BadSignature, "bad signature rejected");
    Check(table.Present(token.data(), token.size() - 1, peerB, now) == IdentityVerdict::Malformed, "short token rejected");

    const auto identify = BuildIdentify(token);
    ParsedControl pc;
    Check(ParseControl(identify.data(), identify.size(), pc) && pc.type == PacketType::Identify && pc.payloadSize == token.size(), "identify packet");
    const auto ack = BuildIdentifyAck(parsed.nonce, IdentityVerdict::Replayed);
    Id16 nonce;
    IdentityVerdict verdict;
    Check(ParseControl(ack.data(), ack.size(), pc) && ParseIdentifyAck(pc, nonce, verdict) && nonce == parsed.nonce &&
          verdict == IdentityVerdict::Replayed, "identify ack");

    uint32_t mapped = 0;
    Check(IsMappedIpv4(MappedIpv4(0xC0A80105u).data(), &mapped) && mapped == 0xC0A80105u, "mapped ipv4");
}
} // namespace

int main()
{
    TestHashes();
    TestRfc5769Request();
    TestRfc5769Response();
    TestBuilderRoundTrip();
    TestControlPackets();
    TestOverlayAddressAndFraming();
    TestUpnp();
    TestIdentity();
    std::printf("%s\n", g_failures == 0 ? "p2p codec tests passed" : "p2p codec tests FAILED");
    return g_failures == 0 ? 0 : 1;
}
