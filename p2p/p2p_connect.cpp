#include "p2p_connect.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <httplib.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "logging.h"
#include "p2p_eos_bridge.h"
#include "p2p_log.h"
#include "p2p_mux.h"
#include "p2p_netinfo.h"
#include "p2p_plugin.h"
#include "p2p_settings.h"

namespace p2p
{
namespace
{

using Clock = std::chrono::steady_clock;
using json = nlohmann::json;

constexpr auto kPingInterval = std::chrono::milliseconds(150);
constexpr auto kGraceAfterFirstAnswer = std::chrono::milliseconds(300);
constexpr auto kLoopStep = std::chrono::milliseconds(10);
constexpr int kWantedAnswers = 3;
constexpr size_t kMaxCandidates = 64; // probe ids carry the index in 8 bits

enum class Kind
{
    Direct,
    Punch,
    Lan,
    Tailscale,
    Eos,
    Iroh,
    Tailcat,
    Turn,
};

const char* KindName(Kind k)
{
    switch (k)
    {
    case Kind::Direct: return "direct";
    case Kind::Punch: return "punch";
    case Kind::Lan: return "lan";
    case Kind::Tailscale: return "tailscale";
    case Kind::Eos: return "eos";
    case Kind::Iroh: return "iroh";
    case Kind::Tailcat: return "tailcat";
    case Kind::Turn: return "turn";
    }
    return "?";
}

// Small tie-breaker (ms) so that when two routes measure about the same the
// simpler one wins: plain UDP needs no extra process-side work, overlays add
// encryption/framing and depend on third-party infrastructure.
double KindBiasMs(Kind k)
{
    switch (k)
    {
    case Kind::Direct:
    case Kind::Lan: return 0.0;
    case Kind::Punch: return 0.25;
    case Kind::Tailscale: return 0.5;
    default: return 1.0;
    }
}

bool IsUdpKind(Kind k)
{
    return k == Kind::Direct || k == Kind::Punch || k == Kind::Lan || k == Kind::Tailscale || k == Kind::Turn;
}

enum class CandState
{
    Preparing,
    Probing,
    Answered,
    Failed,
};

struct Candidate
{
    Kind kind = Kind::Direct;
    std::string label;
    Ipv4Endpoint udp;
    std::string remote; // overlay address / EOS ProductUserId
    uint64_t probeId = 0;

    CandState state = CandState::Preparing;
    bool relay = false;
    double rttMs = -1;
    int answers = 0;
    Clock::time_point lastPing{};
    uint64_t muxId = 0;
    uint64_t pluginHandle = 0;
    Ipv6Bytes eosAddress{};
    uint16_t eosPort = 0;
    std::string note;

    std::string ConnectAddress(uint16_t serverPort) const
    {
        switch (kind)
        {
        case Kind::Eos: return FormatIpv6Connect(eosAddress, eosPort);
        case Kind::Iroh:
        case Kind::Tailcat:
            return FormatIpv6Connect(EncodeOverlayAddress(kind == Kind::Iroh ? Backend::Iroh : Backend::Tailcat, muxId),
                                     serverPort);
        default: return udp.ToString();
        }
    }
};

struct Session
{
    std::mutex mutex;
    uint64_t generation = 0;
    std::string target;
    uint16_t serverPort = 0;
    Id16 ticket{};
    bool haveTicket = false;
    Ipv4Endpoint rendezvous;
    std::vector<Candidate> candidates;
    SOCKET socket = INVALID_SOCKET;
    bool ownsSocket = false;
    bool serverPunched = false;
    Clock::time_point started;
    Clock::time_point firstAnswer{};
    bool overlayOnly = false;
};

std::mutex g_mutex;
std::shared_ptr<Session> g_session;
std::atomic<uint64_t> g_generation{ 0 };
std::unordered_map<std::string, json> g_transports;
std::string g_lastSummary;
uint64_t g_activeOverlayPeer = 0; // overlay route of the current/last connection

std::shared_ptr<Session> CurrentSession()
{
    std::lock_guard lock(g_mutex);
    return g_session;
}

bool StillCurrent(const std::shared_ptr<Session>& s)
{
    return g_generation.load() == s->generation;
}

std::string ClientPluginConfig(Backend backend, const Settings& settings)
{
    json cfg = json::object();
    if (backend == Backend::Iroh && !settings.irohRelayUrl.empty())
        cfg["relay_url"] = settings.irohRelayUrl;
    if (backend == Backend::Tailcat)
    {
        if (!settings.tailcatDerpMapUrl.empty())
            cfg["relay_url"] = settings.tailcatDerpMapUrl;
        cfg["verbose"] = settings.debug;
    }
    return cfg.dump();
}

struct MasterInfo
{
    bool ok = false;
    bool p2p = false; // server runs a build that answers R1NX pings
    json transports;
    Id16 ticket{};
    bool haveTicket = false;
    Ipv4Endpoint rendezvous;
    Ipv4Endpoint serverMapped;
    std::vector<Ipv4Endpoint> lan;
};

MasterInfo QueryMaster(const std::string& msUrl, const std::string& target)
{
    MasterInfo info;
    if (msUrl.empty())
        return info;
    try
    {
        httplib::Client cli(msUrl);
        cli.set_connection_timeout(2, 0);
        cli.set_read_timeout(2, 0);
        cli.set_address_family(AF_INET); // the rendezvous compares our HTTP and UDP IPv4
        cli.set_follow_location(true);
        const json body = { { "server", target } };
        auto res = cli.Post("/nat/connect", body.dump(), "application/json");
        if (!res || res->status != 200)
        {
            Debug("master /nat/connect failed (%d)\n", res ? res->status : -1);
            return info;
        }
        const json r = json::parse(res->body);
        info.ok = true;
        info.p2p = r.value("p2p", false);
        if (r.contains("transports") && r["transports"].is_object())
            info.transports = r["transports"];
        if (r.contains("ticket") && r["ticket"].is_string())
            info.haveTicket = HexToId16(r["ticket"].get<std::string>(), info.ticket);
        if (r.contains("rendezvous") && r["rendezvous"].is_string())
            if (auto ep = Ipv4Endpoint::Parse(r["rendezvous"].get<std::string>()))
                info.rendezvous = *ep;
        if (r.contains("server_mapped") && r["server_mapped"].is_string())
            if (auto ep = Ipv4Endpoint::Parse(r["server_mapped"].get<std::string>()))
                info.serverMapped = *ep;
        if (r.contains("lan") && r["lan"].is_array())
            for (const auto& l : r["lan"])
                if (l.is_string())
                    if (auto ep = Ipv4Endpoint::Parse(l.get<std::string>()))
                        info.lan.push_back(*ep);
    }
    catch (...)
    {
        info.ok = false;
    }
    return info;
}

std::string JsonString(const json& j, const char* key)
{
    if (j.is_object() && j.contains(key) && j[key].is_string())
        return j[key].get<std::string>();
    return {};
}

void AddCandidate(Session& s, Kind kind, const std::string& label)
{
    Candidate c;
    c.kind = kind;
    c.label = label;
    c.probeId = (s.generation << 8) | static_cast<uint64_t>(s.candidates.size());
    s.candidates.push_back(std::move(c));
}

void BuildCandidates(Session& s, const Ipv4Endpoint& server, const MasterInfo& info, const json& transports,
                     const Settings& settings)
{
    AddCandidate(s, Kind::Direct, server.ToString());
    s.candidates.back().udp = server;
    s.candidates.back().state = CandState::Probing;

    if (info.serverMapped.Valid() && info.serverMapped != server)
    {
        AddCandidate(s, Kind::Punch, info.serverMapped.ToString());
        s.candidates.back().udp = info.serverMapped;
        s.candidates.back().state = CandState::Probing;
    }
    for (const auto& lan : info.lan)
    {
        AddCandidate(s, Kind::Lan, lan.ToString());
        s.candidates.back().udp = lan;
        s.candidates.back().state = CandState::Probing;
    }

    const LocalAddresses local = GetLocalAddresses();
    if (!local.tailscale.empty() && transports.contains("tailscale"))
    {
        const json& ts = transports["tailscale"];
        if (ts.contains("ips") && ts["ips"].is_array())
        {
            for (const auto& ip : ts["ips"])
            {
                if (!ip.is_string())
                    continue;
                auto ep = Ipv4Endpoint::Parse(ip.get<std::string>() + ":" + std::to_string(server.port));
                if (ep && IsTailscaleIpv4(ep->ip))
                {
                    AddCandidate(s, Kind::Tailscale, ep->ToString());
                    s.candidates.back().udp = *ep;
                    s.candidates.back().state = CandState::Probing;
                }
            }
        }
    }

    if (transports.contains("eos"))
    {
        const std::string puid = JsonString(transports["eos"], "puid");
        if (!puid.empty())
        {
            AddCandidate(s, Kind::Eos, "puid " + puid.substr(0, 8) + "...");
            s.candidates.back().remote = puid;
        }
    }
    if (transports.contains("iroh") && IrohPlugin().IsAvailable())
    {
        const json& ir = transports["iroh"];
        const std::string id = JsonString(ir, "id");
        if (!id.empty())
        {
            std::string remote = id + "|" + JsonString(ir, "relay") + "|";
            if (ir.contains("addrs") && ir["addrs"].is_array())
            {
                bool first = true;
                for (const auto& a : ir["addrs"])
                {
                    if (!a.is_string())
                        continue;
                    remote += (first ? "" : ",") + a.get<std::string>();
                    first = false;
                }
            }
            AddCandidate(s, Kind::Iroh, id.substr(0, 10) + "...");
            s.candidates.back().remote = remote;
        }
    }
    if (transports.contains("tailcat") && TailcatPlugin().IsAvailable())
    {
        const std::string addr = JsonString(transports["tailcat"], "addr");
        if (!addr.empty())
        {
            AddCandidate(s, Kind::Tailcat, addr.substr(0, 12) + "...");
            s.candidates.back().remote = addr;
        }
    }
    if (transports.contains("turn"))
    {
        if (auto relay = Ipv4Endpoint::Parse(JsonString(transports["turn"], "relay")))
        {
            AddCandidate(s, Kind::Turn, relay->ToString());
            s.candidates.back().udp = *relay;
            s.candidates.back().relay = true;
            s.candidates.back().state = CandState::Probing;
        }
    }
    (void)settings;
}

// Prepares an overlay candidate on a worker thread (plugin load, EOS login,
// connection setup can all block for seconds).
void PrepareOverlay(std::shared_ptr<Session> session, size_t index, Settings settings)
{
    Kind kind;
    std::string remote;
    uint16_t port;
    {
        std::lock_guard lock(session->mutex);
        kind = session->candidates[index].kind;
        remote = session->candidates[index].remote;
        port = session->serverPort;
    }

    auto fail = [&](const char* why) {
        std::lock_guard lock(session->mutex);
        session->candidates[index].state = CandState::Failed;
        session->candidates[index].note = why;
    };

    if (kind == Kind::Eos)
    {
        if (!eos_bridge::EnsureInitialized())
            return fail("EOS unavailable");
        Ipv6Bytes addr;
        uint16_t eosPort = 0;
        if (!eos_bridge::RegisterPeer(remote, addr, eosPort))
            return fail("EOS peer registration failed");
        std::lock_guard lock(session->mutex);
        auto& c = session->candidates[index];
        c.eosAddress = addr;
        c.eosPort = eosPort;
        c.state = CandState::Probing;
        return;
    }

    PluginBackend& plugin = kind == Kind::Iroh ? IrohPlugin() : TailcatPlugin();
    if (!plugin.EnsureReady(ClientPluginConfig(plugin.Kind(), settings)))
        return fail("plugin failed to start");
    uint64_t handle = 0;
    if (!plugin.Connect(remote, port, handle))
        return fail("connect failed");
    const uint64_t muxId = AddOutgoingPeer(plugin.Kind(), handle, port);
    std::lock_guard lock(session->mutex);
    auto& c = session->candidates[index];
    c.pluginHandle = handle;
    c.muxId = muxId;
    // Stays Preparing until the plugin reports the connection as up.
}

void SendPing(Session& s, Candidate& c, const Clock::time_point now)
{
    c.lastPing = now;
    const auto ping = BuildPing(c.probeId, NowMicros());
    switch (c.kind)
    {
    case Kind::Eos:
        eos_bridge::SendTo(c.eosAddress, ping.data(), ping.size(), Route::Client);
        break;
    case Kind::Iroh:
    case Kind::Tailcat:
        SendToPeer(c.muxId, ping.data(), ping.size());
        break;
    default:
        if (c.kind == Kind::Punch && s.haveTicket)
            RealSendTo(s.socket, BuildPunch(s.ticket, 0), c.udp);
        RealSendTo(s.socket, ping, c.udp);
        break;
    }
}

void PollOwnSocket(Session& s)
{
    if (!s.ownsSocket || s.socket == INVALID_SOCKET)
        return;
    uint8_t buf[2048];
    for (int i = 0; i < 64; ++i)
    {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(s.socket, &readSet);
        timeval tv{ 0, 0 };
        if (select(0, &readSet, nullptr, nullptr, &tv) <= 0)
            return;
        sockaddr_storage from{};
        int fromLen = sizeof(from);
        const int n = RealRecvFrom(s.socket, buf, sizeof(buf), reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n <= 0)
            return;
        ControlContext ctx;
        ctx.route = Route::Client;
        ctx.viaEngineUdp = true;
        FromSockaddr(reinterpret_cast<sockaddr*>(&from), fromLen, ctx.udpFrom);
        TryConsumeControl(buf, static_cast<size_t>(n), ctx);
    }
}

std::string FormatSummary(const Session& s, const Candidate* chosen, double elapsedMs)
{
    std::string out;
    char line[256];
    std::snprintf(line, sizeof(line), "Route probe for %s (%.0f ms):\n", s.target.c_str(), elapsedMs);
    out += line;
    for (const auto& c : s.candidates)
    {
        std::string result;
        if (c.state == CandState::Answered)
        {
            char rtt[64];
            std::snprintf(rtt, sizeof(rtt), "%.1f ms%s", c.rttMs, c.relay ? " (relayed)" : "");
            result = rtt;
        }
        else if (c.state == CandState::Failed)
            result = "failed" + (c.note.empty() ? std::string() : ": " + c.note);
        else
            result = c.state == CandState::Preparing ? "not ready" : "no answer";
        std::snprintf(line, sizeof(line), "  %s %-10s %-28s %s\n", &c == chosen ? "*" : " ", KindName(c.kind),
                      c.label.c_str(), result.c_str());
        out += line;
    }
    return out;
}

void RunSession(std::shared_ptr<Session> s, Settings settings, std::string fallbackTarget)
{
    const auto deadline = s->started + std::chrono::milliseconds(settings.connectTimeoutMs);

    // Kick off overlay preparation.
    {
        std::lock_guard lock(s->mutex);
        for (size_t i = 0; i < s->candidates.size(); ++i)
        {
            const Kind k = s->candidates[i].kind;
            if (k == Kind::Eos || k == Kind::Iroh || k == Kind::Tailcat)
                std::thread(PrepareOverlay, s, i, settings).detach();
        }
    }

    int registerSends = 0;
    Clock::time_point lastRegister{};
    bool eosCandidate = false;
    for (;;)
    {
        std::this_thread::sleep_for(kLoopStep);
        if (!StillCurrent(s))
            break;
        const auto now = Clock::now();

        // Drain everything that might carry a PONG.
        PollEngineSocket(Route::Client);
        PollOwnSocket(*s);
        PumpBackends();
        if (eosCandidate)
            eos_bridge::PollControl(Route::Client);

        std::lock_guard lock(s->mutex);

        // Register our client-socket mapping with the rendezvous so the
        // master can tell the server where to punch.
        if (s->haveTicket && s->rendezvous.Valid() && registerSends < 4 &&
            now - lastRegister > std::chrono::milliseconds(200))
        {
            RealSendTo(s->socket, BuildCliRegister(s->ticket), s->rendezvous);
            lastRegister = now;
            ++registerSends;
        }

        bool allDone = true;
        bool waitingForPreferred = false;
        for (auto& c : s->candidates)
        {
            if (c.kind == Kind::Eos)
                eosCandidate = true;
            if ((c.kind == Kind::Iroh || c.kind == Kind::Tailcat) && c.pluginHandle && c.state != CandState::Failed)
            {
                PluginBackend& plugin = c.kind == Kind::Iroh ? IrohPlugin() : TailcatPlugin();
                const auto st = plugin.PeerStatus(c.pluginHandle);
                if (st.state == "connected" && c.state == CandState::Preparing)
                    c.state = CandState::Probing;
                else if (st.state == "failed" || st.state == "closed")
                {
                    c.state = CandState::Failed;
                    c.note = st.error;
                }
                c.relay = st.path == "relay";
            }
            if (c.state == CandState::Probing || (c.state == CandState::Answered && c.answers < kWantedAnswers))
            {
                if (now - c.lastPing >= kPingInterval)
                    SendPing(*s, c, now);
            }
            const bool done = c.state == CandState::Failed || (c.state == CandState::Answered && c.answers >= kWantedAnswers);
            allDone &= done;
            if (!done && c.state != CandState::Answered && !settings.prefer.empty() && settings.prefer == KindName(c.kind))
                waitingForPreferred = true;
        }

        // Once something answered, give slower routes a short grace period
        // rather than the full timeout: a path that is not even up by then
        // will not beat the one we already have. A route forced through
        // delta_p2p_prefer is always waited for.
        const bool graceOver = s->firstAnswer.time_since_epoch().count() != 0 &&
                               now - s->firstAnswer > kGraceAfterFirstAnswer && !s->overlayOnly && !waitingForPreferred;
        if (allDone || graceOver || now >= deadline)
            break;
    }

    if (!StillCurrent(s))
    {
        std::lock_guard lock(s->mutex);
        for (auto& c : s->candidates)
            if (c.muxId)
                ClosePeer(c.muxId);
        if (s->ownsSocket)
            closesocket(s->socket);
        return;
    }

    // Pick the winner.
    std::string command;
    std::string summary;
    {
        std::lock_guard lock(s->mutex);
        const Candidate* best = nullptr;
        double bestScore = 1e18;
        for (const auto& c : s->candidates)
        {
            if (c.state != CandState::Answered)
                continue;
            double score = c.rttMs + KindBiasMs(c.kind) + (c.relay ? settings.relayPenaltyMs : 0);
            if (!settings.prefer.empty() && settings.prefer == KindName(c.kind))
                score = -1; // forced by delta_p2p_prefer
            if (score < bestScore)
            {
                bestScore = score;
                best = &c;
            }
        }
        const double elapsed = std::chrono::duration<double, std::milli>(Clock::now() - s->started).count();
        summary = FormatSummary(*s, best, elapsed);
        if (best)
        {
            command = "connect " + best->ConnectAddress(s->serverPort);
            summary += std::string("  -> connecting via ") + KindName(best->kind) + " (" + best->ConnectAddress(s->serverPort) + ")\n";
        }
        else if (!fallbackTarget.empty())
        {
            command = "connect " + fallbackTarget;
            summary += "  -> no path answered, falling back to a plain connect\n";
        }
        else
        {
            summary += "  -> no path answered\n";
        }
        for (const auto& c : s->candidates)
            if (c.muxId && &c != best)
                ClosePeer(c.muxId);
        if (s->ownsSocket)
            closesocket(s->socket);
        if (best && best->muxId)
        {
            std::lock_guard globalLock(g_mutex);
            g_activeOverlayPeer = best->muxId;
        }
    }

    {
        std::lock_guard lock(g_mutex);
        g_lastSummary = summary;
    }
    Log("%s", summary.c_str());
    if (!command.empty())
    {
        command += "\n";
        Cbuf_AddText(0, command.c_str(), 0);
    }
}

// A new connection attempt replaces whatever overlay route the previous
// connection used.
void ReleaseActiveOverlay()
{
    uint64_t previous = 0;
    {
        std::lock_guard lock(g_mutex);
        previous = g_activeOverlayPeer;
        g_activeOverlayPeer = 0;
    }
    if (previous)
        ClosePeer(previous);
}

std::shared_ptr<Session> NewSession(const std::string& target, uint16_t port)
{
    ReleaseActiveOverlay();
    auto s = std::make_shared<Session>();
    s->generation = ++g_generation;
    s->target = target;
    s->serverPort = port;
    s->started = Clock::now();
    s->socket = GetEngineSocket(Route::Client);
    if (s->socket == INVALID_SOCKET)
    {
        // The engine has not touched its client socket yet; probe from a
        // private socket (hole punching then only helps the probe, but
        // direct/TURN/LAN results stay meaningful).
        s->socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        s->ownsSocket = s->socket != INVALID_SOCKET;
    }
    std::lock_guard lock(g_mutex);
    g_session = s;
    return s;
}

} // namespace

uint64_t NowMicros()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count());
}

void RememberServerTransports(const std::string& key, const json& transports)
{
    std::lock_guard lock(g_mutex);
    if (transports.is_object())
        g_transports[key] = transports;
    else
        g_transports.erase(key);
}

void ConnectBest(const std::string& targetIn)
{
    const Settings settings = GetSettings();
    std::string target = targetIn;
    auto server = Ipv4Endpoint::Parse(target);
    if (!settings.enable || !server)
    {
        // Hostnames, IPv6 literals and fake addresses go straight to the engine.
        Cbuf_AddText(0, ("connect " + target + "\n").c_str(), 0);
        return;
    }
    if (server->port == 0)
        server->port = 37015;
    target = server->ToString();

    json cached;
    {
        std::lock_guard lock(g_mutex);
        auto it = g_transports.find(target);
        if (it != g_transports.end())
            cached = it->second;
    }

    Log("Finding the best route to %s...\n", target.c_str());
    std::thread([target, server = *server, cached, settings] {
        const MasterInfo info = QueryMaster(settings.masterServerUrl, target);
        if (!info.ok || !info.p2p)
        {
            ReleaseActiveOverlay();
            // Unlisted server, master unreachable, or a server build that
            // would not answer our pings: behave exactly like "connect".
            Log("%s does not advertise p2p routes; connecting directly\n", target.c_str());
            Cbuf_AddText(0, ("connect " + target + "\n").c_str(), 0);
            return;
        }
        const json transports = info.transports.is_object() ? info.transports : cached;
        auto s = NewSession(target, server.port);
        {
            std::lock_guard lock(s->mutex);
            s->haveTicket = info.haveTicket;
            s->ticket = info.ticket;
            s->rendezvous = info.rendezvous;
            BuildCandidates(*s, server, info, transports.is_object() ? transports : json::object(), settings);
        }
        RunSession(s, settings, target);
    }).detach();
}

void ConnectOverlay(Backend backend, const std::string& address, uint16_t port)
{
    Settings settings = GetSettings();
    if (port == 0)
        port = 37015;
    PluginBackend& plugin = backend == Backend::Iroh ? IrohPlugin() : TailcatPlugin();
    if (!plugin.IsAvailable())
    {
        Log("%s plugin (%s) is not installed\n", BackendName(backend), backend == Backend::Iroh ? "r1delta_iroh.dll" : "r1delta_tailcat.dll");
        return;
    }
    settings.connectTimeoutMs = 20000; // overlay bootstrap can take a while
    std::thread([backend, address, port, settings] {
        auto s = NewSession(std::string(BackendName(backend)) + " " + address.substr(0, 16) + "...", port);
        {
            std::lock_guard lock(s->mutex);
            s->overlayOnly = true;
            AddCandidate(*s, backend == Backend::Iroh ? Kind::Iroh : Kind::Tailcat, address.substr(0, 16) + "...");
            s->candidates.back().remote = address;
        }
        RunSession(s, settings, "");
    }).detach();
}

void ClientOnPong(uint64_t probeId, uint64_t timestampUs, const char* via)
{
    auto s = CurrentSession();
    if (!s || (probeId >> 8) != s->generation)
        return;
    const size_t index = static_cast<size_t>(probeId & 0xFF);
    const uint64_t now = NowMicros();
    if (timestampUs > now)
        return;
    const double rtt = static_cast<double>(now - timestampUs) / 1000.0;
    std::lock_guard lock(s->mutex);
    if (index >= s->candidates.size())
        return;
    auto& c = s->candidates[index];
    if (c.state == CandState::Failed)
        return;
    c.state = CandState::Answered;
    c.rttMs = c.answers == 0 ? rtt : std::min(c.rttMs, rtt);
    ++c.answers;
    if (s->firstAnswer.time_since_epoch().count() == 0)
        s->firstAnswer = Clock::now();
    Debug("pong from %s via %s: %.1f ms\n", KindName(c.kind), via, rtt);
}

void ClientOnRegisterAck(const RegisterAck& ack, const Ipv4Endpoint& from)
{
    auto s = CurrentSession();
    if (!s)
        return;
    std::lock_guard lock(s->mutex);
    if (from != s->rendezvous || s->candidates.size() >= kMaxCandidates)
        return;
    Debug("rendezvous sees this client at %s\n", ack.observed.ToString().c_str());
    // The master may learn the server mapping later than our HTTP query.
    if (ack.server && ack.server->Valid())
    {
        for (const auto& c : s->candidates)
            if ((c.kind == Kind::Direct || c.kind == Kind::Punch) && c.udp == *ack.server)
                return;
        AddCandidate(*s, Kind::Punch, ack.server->ToString());
        s->candidates.back().udp = *ack.server;
        s->candidates.back().state = CandState::Probing;
    }
}

void ClientOnPunch(const Id16& ticket, const Ipv4Endpoint& from)
{
    auto s = CurrentSession();
    if (!s)
        return;
    std::lock_guard lock(s->mutex);
    if (!s->haveTicket || ticket != s->ticket || s->candidates.size() >= kMaxCandidates)
        return;
    if (!s->serverPunched)
        Debug("server punched through from %s\n", from.ToString().c_str());
    s->serverPunched = true;
    // A punch from an address we do not know yet means the server's NAT
    // mapped it differently for us; probe it too.
    for (const auto& c : s->candidates)
        if (IsUdpKind(c.kind) && c.udp == from)
            return;
    AddCandidate(*s, Kind::Punch, from.ToString());
    s->candidates.back().udp = from;
    s->candidates.back().state = CandState::Probing;
}

std::string ClientDescribe()
{
    std::lock_guard lock(g_mutex);
    return g_lastSummary.empty() ? "  no connection attempt yet\n" : g_lastSummary;
}

} // namespace p2p
