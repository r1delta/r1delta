// Linux end-to-end harness for the p2p transport layer.
//
// Runs the real p2p sources (mux, TURN client, rendezvous, delta_connect
// prober, plugin loader) against a stand-in "engine": one UDP game socket
// that is polled like the engine's NET_ReceivePacket and a trivial game
// protocol (connect challenge + GAME/ECHO datagrams). See run_e2e.sh.
//
//   fake_engine server <bind ip> <port> <master url>
//   fake_engine client <bind ip> <port> <master url> <server ip:port>
//
// Environment:
//   P2P_<cvar name>=value   overrides a delta_p2p_* convar default
//   FAKE_NAT=1              port-restricted NAT emulation on the game socket
//   R1P_PLUGIN_DIR          directory holding the iroh / tailcat plugins

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include "../p2p.h"
#include "../p2p_eos_bridge.h"
#include "../p2p_mux.h"
#include "../p2p_netinfo.h"
#include "../p2p_upnp.h"
#include "cvar.h"
#include "logging.h"

using json = nlohmann::json;

namespace
{
std::string g_role = "?";
std::mutex g_logMutex;
std::mutex g_cbufMutex;
std::deque<std::string> g_cbuf;
std::string g_chosen;

std::map<std::string, ConVarR1*> g_cvars;
std::map<std::string, void (*)(const CCommand&)> g_commands;

std::mutex g_natMutex;
std::set<std::pair<int, std::string>> g_natAllowed; // (socket, "ip:port") we have sent to
std::set<int> g_natSockets;

std::string AddrKey(const sockaddr* sa)
{
    char host[64] = {};
    if (sa->sa_family == AF_INET)
    {
        auto* v4 = reinterpret_cast<const sockaddr_in*>(sa);
        inet_ntop(AF_INET, &v4->sin_addr, host, sizeof(host));
        return std::string(host) + ":" + std::to_string(ntohs(v4->sin_port));
    }
    return "?";
}

int NatSendTo(SOCKET s, const char* data, int len, int flags, const sockaddr* to, int tolen)
{
    if (to && to->sa_family == AF_INET)
    {
        std::lock_guard lock(g_natMutex);
        g_natAllowed.insert({ s, AddrKey(to) });
    }
    return static_cast<int>(::sendto(s, data, len, flags, to, static_cast<socklen_t>(tolen)));
}

int NatRecvFrom(SOCKET s, char* buf, int len, int flags, sockaddr* from, int* fromlen)
{
    for (;;)
    {
        sockaddr_storage tmp{};
        socklen_t tmpLen = sizeof(tmp);
        const ssize_t n = ::recvfrom(s, buf, len, flags | MSG_DONTWAIT, reinterpret_cast<sockaddr*>(&tmp), &tmpLen);
        if (n < 0)
            return -1;
        {
            std::lock_guard lock(g_natMutex);
            if (g_natSockets.count(s) && !g_natAllowed.count({ s, AddrKey(reinterpret_cast<sockaddr*>(&tmp)) }))
                continue; // unsolicited: a port-restricted NAT drops it
        }
        if (from && fromlen)
        {
            const int copy = std::min<int>(*fromlen, static_cast<int>(tmpLen));
            std::memcpy(from, &tmp, copy);
            *fromlen = static_cast<int>(tmpLen);
        }
        return static_cast<int>(n);
    }
}

ConVarR1* FindVar(uintptr_t, const char* name)
{
    auto it = g_cvars.find(name);
    return it == g_cvars.end() ? nullptr : it->second;
}

bool TakeConnectCommand(std::string& address)
{
    std::lock_guard lock(g_cbufMutex);
    while (!g_cbuf.empty())
    {
        std::string cmd = g_cbuf.front();
        g_cbuf.pop_front();
        if (cmd.rfind("connect ", 0) == 0)
        {
            address = cmd.substr(8);
            while (!address.empty() && (address.back() == '\n' || address.back() == ' '))
                address.pop_back();
            return true;
        }
    }
    return false;
}

bool ParseConnectAddress(const std::string& text, sockaddr_storage& out, int& outLen)
{
    std::memset(&out, 0, sizeof(out));
    if (!text.empty() && text[0] == '[')
    {
        const size_t close = text.find(']');
        auto* v6 = reinterpret_cast<sockaddr_in6*>(&out);
        v6->sin6_family = AF_INET6;
        if (inet_pton(AF_INET6, text.substr(1, close - 1).c_str(), &v6->sin6_addr) != 1)
            return false;
        v6->sin6_port = htons(static_cast<uint16_t>(std::atoi(text.c_str() + close + 2)));
        outLen = sizeof(sockaddr_in6);
        return true;
    }
    const size_t colon = text.rfind(':');
    auto* v4 = reinterpret_cast<sockaddr_in*>(&out);
    v4->sin_family = AF_INET;
    if (inet_pton(AF_INET, text.substr(0, colon).c_str(), &v4->sin_addr) != 1)
        return false;
    v4->sin_port = htons(static_cast<uint16_t>(std::atoi(text.c_str() + colon + 1)));
    outLen = sizeof(sockaddr_in);
    return true;
}

// The engine side of the hooks: identical flow to eos_network.cpp's
// HookedRecvFrom / HookedSendTo (minus EOS).
struct Engine
{
    SOCKET sock = INVALID_SOCKET;
    p2p::Route route = p2p::Route::Unknown;

    int Recv(char* buf, int len, sockaddr_storage& from, int& fromLen)
    {
        fromLen = sizeof(from);
        p2p::NoteEngineSocket(sock, route);
        int queued = 0;
        if (p2p::PopQueued(route, buf, len, reinterpret_cast<sockaddr*>(&from), &fromLen, &queued))
            return queued;
        const int n = p2p::RealRecvFrom(sock, buf, len, reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n > 0 && p2p::ConsumeUdpControl(sock, route, reinterpret_cast<uint8_t*>(buf), n, reinterpret_cast<sockaddr*>(&from), fromLen))
            return -1;
        return n;
    }

    int Send(const void* data, int len, const sockaddr* to, int tolen)
    {
        int handled = 0;
        if (p2p::HandleSendTo(static_cast<const char*>(data), len, to, tolen, &handled))
            return handled;
        return p2p::RealSendTo(sock, data, len, to, tolen);
    }
};

SOCKET OpenSocket(const std::string& ip, uint16_t port)
{
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &sa.sin_addr);
    if (bind(s, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0)
    {
        perror("bind");
        std::exit(2);
    }
    return s;
}

void RunCommand(const std::string& name, std::vector<std::string> args)
{
    CCommand cmd;
    cmd.args.push_back(name);
    for (auto& a : args)
        cmd.args.push_back(a);
    g_commands.at(name)(cmd);
}

int RunServer(const std::string& ip, uint16_t port, const std::string& master)
{
    Engine engine;
    engine.sock = OpenSocket(ip, port);
    engine.route = p2p::Route::Server;
    if (std::getenv("FAKE_NAT"))
    {
        std::lock_guard lock(g_natMutex);
        g_natSockets.insert(engine.sock);
    }

    std::atomic<bool> running{ true };
    std::thread heartbeat([&] {
        httplib::Client cli(master);
        cli.set_connection_timeout(2, 0);
        while (running)
        {
            json j;
            j["host_name"] = "p2p devtest server";
            j["map_name"] = "mp_lobby";
            j["game_mode"] = "tdm";
            j["max_players"] = 12;
            j["port"] = port;
            j["has_auth"] = false;
            j["version"] = "3.0.0";
            j["has_password"] = false;
            j["description"] = "";
            j["playlist"] = "";
            j["playlist_display_name"] = "";
            j["players"] = json::array();
            p2p::AddHeartbeatFields(j, port);
            auto res = cli.Post("/heartbeat", j.dump(), "application/json");
            if (res && res->status == 200)
                p2p::OnHeartbeatResponse(res->body);
            else
                Msg("heartbeat failed: %d\n", res ? res->status : -1);
            for (int i = 0; i < 30 && running; ++i)
                Sleep(100);
        }
    });

    char buf[4096];
    int echoes = 0;
    for (;;)
    {
        sockaddr_storage from{};
        int fromLen = 0;
        const int n = engine.Recv(buf, sizeof(buf), from, fromLen);
        if (n <= 0)
        {
            Sleep(2);
            continue;
        }
        const std::string pkt(buf, n);
        if (n >= 22 && static_cast<uint8_t>(buf[0]) == 0xFF && buf[4] == 0x48 && pkt.compare(5, 7, "connect") == 0)
        {
            // Engine answer to the master server's validation challenge.
            std::string resp = "\xFF\xFF\xFF\xFF\x49\x01\x02\x03\x04";
            resp += "connect";
            resp += pkt.substr(12, 10);
            engine.Send(resp.data(), static_cast<int>(resp.size()), reinterpret_cast<sockaddr*>(&from), fromLen);
            continue;
        }
        if (pkt.rfind("GAME:", 0) == 0)
        {
            const std::string resp = "ECHO:" + pkt.substr(5);
            engine.Send(resp.data(), static_cast<int>(resp.size()), reinterpret_cast<sockaddr*>(&from), fromLen);
            if (++echoes % 20 == 1)
                Msg("echoing game traffic (%d)\n", echoes);
        }
    }
}

int RunClient(const std::string& ip, uint16_t port, const std::string& master, const std::string& target)
{
    Engine engine;
    engine.sock = OpenSocket(ip, port);
    engine.route = p2p::Route::Client;

    // Wait for the server to be listed (validated) and load its transports
    // like the server browser does.
    httplib::Client cli(master);
    bool listed = false;
    const std::string wantedTransports = std::getenv("WAIT_TRANSPORTS") ? std::getenv("WAIT_TRANSPORTS") : "";
    for (int i = 0; i < 120 && !listed; ++i)
    {
        auto res = cli.Get("/servers");
        if (res && res->status == 200)
        {
            for (const auto& entry : json::parse(res->body))
            {
                const std::string key = entry.value("ip", "") + ":" + std::to_string(entry.value("port", 0));
                if (key != target)
                    continue;
                bool haveAll = true;
                size_t pos = 0;
                while (!wantedTransports.empty() && pos <= wantedTransports.size())
                {
                    const size_t comma = wantedTransports.find(',', pos);
                    const std::string t = wantedTransports.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
                    if (!entry.contains("transports") || !entry["transports"].contains(t))
                        haveAll = false;
                    if (comma == std::string::npos)
                        break;
                    pos = comma + 1;
                }
                if (!haveAll)
                    continue;
                p2p::OnServerListEntry(entry);
                Msg("server listed: %s\n", entry.dump().c_str());
                listed = true;
            }
        }
        if (!listed)
            Sleep(500);
    }
    if (!listed)
    {
        Msg("RESULT fail: server never listed\n");
        return 1;
    }

    RunCommand("delta_connect", { target });

    // Emulate the engine's per-frame socket polling while waiting.
    std::string address;
    char buf[4096];
    for (int i = 0; i < 2000 && !TakeConnectCommand(address); ++i)
    {
        sockaddr_storage from{};
        int fromLen = 0;
        engine.Recv(buf, sizeof(buf), from, fromLen);
        Sleep(5);
    }
    if (address.empty())
    {
        Msg("RESULT fail: no connect command\n");
        return 1;
    }
    sockaddr_storage dest{};
    int destLen = 0;
    if (!ParseConnectAddress(address, dest, destLen))
    {
        Msg("RESULT fail: cannot parse %s\n", address.c_str());
        return 1;
    }

    // "Play": send game datagrams (some larger than an overlay MTU) and count echoes.
    int sent = 0, echoes = 0;
    for (int i = 0; i < 400 && echoes < 20; ++i)
    {
        if (i % 10 == 0)
        {
            const int seq = sent++;
            // Every third datagram is larger than the tailcat/iroh MTU to
            // exercise fragmentation.
            std::string payload = "GAME:" + std::to_string(seq) + ":" + std::string((seq % 3) * 900, 'x');
            engine.Send(payload.data(), static_cast<int>(payload.size()), reinterpret_cast<sockaddr*>(&dest), destLen);
        }
        sockaddr_storage from{};
        int fromLen = 0;
        const int n = engine.Recv(buf, sizeof(buf), from, fromLen);
        if (n > 5 && std::string(buf, 5) == "ECHO:")
            ++echoes;
        Sleep(5);
    }
    Msg("RESULT %s chosen=%s address=%s echoes=%d/%d\n", echoes >= 10 ? "ok" : "fail", g_chosen.c_str(), address.c_str(),
        echoes, sent);
    return echoes >= 10 ? 0 : 1;
}

} // namespace

// ---------------------------------------------------------------------------
// Engine symbol stand-ins
// ---------------------------------------------------------------------------
extern "C" void Msg(const char* fmt, ...)
{
    char buf[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    std::lock_guard lock(g_logMutex);
    std::fprintf(stderr, "[%s] %s", g_role.c_str(), buf);
    const char* via = std::strstr(buf, "-> connecting via ");
    if (via)
    {
        std::string rest(via + 18);
        g_chosen = rest.substr(0, rest.find(' '));
    }
}

extern "C" void Warning(const char* fmt, ...)
{
    char buf[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    std::fprintf(stderr, "[%s] WARNING %s", g_role.c_str(), buf);
}

void Cbuf_AddText(int, const char* text, unsigned int)
{
    std::lock_guard lock(g_cbufMutex);
    g_cbuf.emplace_back(text);
}

uintptr_t cvarinterface = 1;
ConVarR1* (*OriginalCCVar_FindVar)(uintptr_t, const char*) = FindVar;

ConVarR1* RegisterConVar(const char* name, const char* value, int, const char*)
{
    std::string v = value;
    if (const char* env = std::getenv((std::string("P2P_") + name).c_str()))
        v = env;
    auto* var = new ConVarR1{};
    var->m_Value.m_pszString = strdup(v.c_str());
    var->m_Value.m_StringLength = static_cast<int64_t>(v.size());
    var->m_Value.m_nValue = std::atoi(v.c_str());
    var->m_Value.m_fValue = static_cast<float>(std::atof(v.c_str()));
    g_cvars[name] = var;
    return var;
}

ConCommandR1* RegisterConCommand(const char* name, void (*callback)(const CCommand&), const char*, int)
{
    g_commands[name] = callback;
    return nullptr;
}

namespace p2p
{
namespace eos_bridge
{
bool EnsureInitialized() { return false; }
bool IsReady() { return false; }
std::string LocalProductUserId() { return {}; }
bool RegisterPeer(const std::string&, Ipv6Bytes&, uint16_t&) { return false; }
bool SendTo(const Ipv6Bytes&, const uint8_t*, size_t, Route) { return false; }
void PollControl(Route) {}
} // namespace eos_bridge

LocalAddresses GetLocalAddresses() { return {}; }
uint32_t GetPrimaryLocalIpv4() { return 0; }
uint32_t GetDefaultGatewayIpv4() { return 0; }
bool IsPrivateIpv4(uint32_t ip)
{
    return (ip >> 24) == 10 || (ip >> 20) == ((172u << 4) | 1u) || (ip >> 16) == ((192u << 8) | 168u);
}
bool IsTailscaleIpv4(uint32_t ip) { return (ip & 0xFFC00000u) == 0x64400000u; }
std::string Ipv4ToString(uint32_t ip)
{
    char buf[20];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
    return buf;
}

PortMapper& PortMap()
{
    static PortMapper mapper;
    return mapper;
}
void PortMapper::Start(uint16_t) {}
void PortMapper::Stop() {}
PortMappingStatus PortMapper::Status() const { return {}; }
} // namespace p2p

int main(int argc, char** argv)
{
    if (argc < 5)
    {
        std::fprintf(stderr, "usage: %s server|client <bind ip> <port> <master url> [target]\n", argv[0]);
        return 2;
    }
    g_role = argv[1];
    const std::string ip = argv[2];
    const uint16_t port = static_cast<uint16_t>(std::atoi(argv[3]));
    const std::string master = argv[4];

    p2p::SetRealSocketFunctions(NatSendTo, NatRecvFrom);
    RegisterConVar("delta_ms_url", master.c_str(), 0, "");
    p2p::Initialize();

    if (g_role == "server")
        return RunServer(ip, port, master);
    if (g_role == "client" && argc >= 6)
        return RunClient(ip, port, master, argv[5]);
    return 2;
}
