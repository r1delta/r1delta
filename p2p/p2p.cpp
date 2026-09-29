#include "p2p.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "core.h"
#include "cvar.h"
#include "logging.h"
#include "p2p_connect.h"
#include "p2p_log.h"
#include "p2p_mux.h"
#include "p2p_server.h"
#include "p2p_settings.h"

namespace p2p
{
namespace
{

ConVarR1* g_enable = nullptr;
ConVarR1* g_serverUpnp = nullptr;
ConVarR1* g_serverPunch = nullptr;
ConVarR1* g_serverEos = nullptr;
ConVarR1* g_serverIroh = nullptr;
ConVarR1* g_serverTailcat = nullptr;
ConVarR1* g_serverTurn = nullptr;
ConVarR1* g_punchSpray = nullptr;
ConVarR1* g_connectTimeout = nullptr;
ConVarR1* g_relayPenalty = nullptr;
ConVarR1* g_prefer = nullptr;
ConVarR1* g_irohRelay = nullptr;
ConVarR1* g_tailcatDerpMap = nullptr;
ConVarR1* g_debug = nullptr;

int IntValue(const ConVarR1* var, int fallback)
{
    return var ? var->m_Value.m_nValue : fallback;
}

std::string StringValue(const ConVarR1* var)
{
    return var && var->m_Value.m_pszString ? std::string(var->m_Value.m_pszString) : std::string();
}

std::string MasterServerUrl()
{
    if (!cvarinterface || !OriginalCCVar_FindVar)
        return {};
    ConVarR1* var = OriginalCCVar_FindVar(cvarinterface, "delta_ms_url");
    return StringValue(var);
}

uint16_t ParsePort(const char* text, uint16_t fallback)
{
    if (!text || !*text)
        return fallback;
    const long v = std::strtol(text, nullptr, 10);
    return v > 0 && v <= 0xFFFF ? static_cast<uint16_t>(v) : fallback;
}

void ConnectCommand(const CCommand& args)
{
    if (args.ArgC() < 2)
    {
        Msg("usage: delta_connect <ip:port>\n");
        return;
    }
    ConnectBest(args.Arg(1));
}

void ConnectIrohCommand(const CCommand& args)
{
    if (args.ArgC() < 2)
    {
        Msg("usage: delta_connect_iroh <iroh address> [game port]\n");
        return;
    }
    ConnectOverlay(Backend::Iroh, args.Arg(1), ParsePort(args.ArgC() > 2 ? args.Arg(2) : nullptr, 37015));
}

void ConnectTailcatCommand(const CCommand& args)
{
    if (args.ArgC() < 2)
    {
        Msg("usage: delta_connect_tailcat <tailcat address> [game port]\n");
        return;
    }
    ConnectOverlay(Backend::Tailcat, args.Arg(1), ParsePort(args.ArgC() > 2 ? args.Arg(2) : nullptr, 37015));
}

void StatusCommand(const CCommand&)
{
    std::string out = "Server transports:\n" + ServerDescribe();
    out += "Engine sockets: client " + std::string(GetEngineSocket(Route::Client) != INVALID_SOCKET ? "seen" : "not seen") +
           ", server " + (GetEngineSocket(Route::Server) != INVALID_SOCKET ? "seen" : "not seen") + "; overlay peers " +
           std::to_string(PeerCount()) + "\n";
    out += "Last connection attempt:\n" + ClientDescribe();
    Msg("%s", out.c_str());
}

} // namespace

void Log(const char* fmt, ...)
{
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Msg("[P2P] %s", buf);
}

void Debug(const char* fmt, ...)
{
    if (IntValue(g_debug, 0) == 0)
        return;
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Msg("[P2P] %s", buf);
}

Settings GetSettings()
{
    Settings s;
    s.enable = g_enable && IntValue(g_enable, 1) != 0;
    s.serverUpnp = IntValue(g_serverUpnp, 1) != 0;
    s.serverPunch = IntValue(g_serverPunch, 1) != 0;
    s.serverEos = IntValue(g_serverEos, 1) != 0;
    s.serverIroh = IntValue(g_serverIroh, 1) != 0;
    s.serverTailcat = IntValue(g_serverTailcat, 1) != 0;
    s.serverTurn = IntValue(g_serverTurn, 1) != 0;
    s.punchSpray = IntValue(g_punchSpray, 2);
    s.connectTimeoutMs = IntValue(g_connectTimeout, 2500);
    if (s.connectTimeoutMs < 300)
        s.connectTimeoutMs = 300;
    if (s.connectTimeoutMs > 15000)
        s.connectTimeoutMs = 15000;
    s.relayPenaltyMs = IntValue(g_relayPenalty, 20);
    s.prefer = StringValue(g_prefer);
    s.irohRelayUrl = StringValue(g_irohRelay);
    s.tailcatDerpMapUrl = StringValue(g_tailcatDerpMap);
    s.masterServerUrl = MasterServerUrl();
    s.debug = IntValue(g_debug, 0) != 0;
    return s;
}

void HandleControl(const ParsedControl& pkt, const ControlContext& ctx)
{
    switch (pkt.type)
    {
    case PacketType::Ping:
    {
        // Only game servers answer; the reply goes back over the transport
        // the ping arrived on, which is what the client is measuring.
        PingPong pp;
        if (ctx.route == Route::Server && ctx.reply && ParsePingPong(pkt, pp))
            ctx.reply(BuildPong(pp.probeId, pp.timestampUs, 0));
        break;
    }
    case PacketType::Pong:
    {
        PingPong pp;
        if (ctx.route == Route::Client && ParsePingPong(pkt, pp))
            ClientOnPong(pp.probeId, pp.timestampUs, ctx.via);
        break;
    }
    case PacketType::RegisterAck:
    {
        RegisterAck ack;
        if (!ctx.viaEngineUdp || !ParseRegisterAck(pkt, ack))
            break;
        if (ctx.route == Route::Server)
            ServerOnRegisterAck(ack, ctx.udpFrom);
        else
            ClientOnRegisterAck(ack, ctx.udpFrom);
        break;
    }
    case PacketType::PunchRequest:
    {
        PunchRequest req;
        if (ctx.route == Route::Server && ctx.viaEngineUdp && ParsePunchRequest(pkt, req))
            ServerOnPunchRequest(req, ctx.udpFrom);
        break;
    }
    case PacketType::Punch:
    {
        Id16 ticket;
        if (ctx.route == Route::Client && ctx.viaEngineUdp && ParseTicket(pkt, ticket))
            ClientOnPunch(ticket, ctx.udpFrom);
        break;
    }
    default:
        break;
    }
}

void Initialize()
{
    static bool initialized = false;
    if (initialized)
        return;
    initialized = true;

    if (IsR1ODedicatedServer())
        return; // the R1O fake dedicated server registers its cvars differently; keep it on the legacy path

    g_enable = RegisterConVar("delta_p2p_enable", "1", FCVAR_NONE,
                              "Enable NAT traversal and alternative transports (EOS, iroh, tailcat, TURN, hole punching, UPnP)");
    g_serverUpnp = RegisterConVar("delta_p2p_server_upnp", "1", FCVAR_NONE, "Servers ask the router to forward hostport via UPnP / NAT-PMP");
    g_serverPunch = RegisterConVar("delta_p2p_server_punch", "1", FCVAR_NONE, "Servers register with the master server rendezvous for UDP hole punching");
    g_serverEos = RegisterConVar("delta_p2p_server_eos", "1", FCVAR_NONE, "Servers log into EOS and advertise their ProductUserId");
    g_serverIroh = RegisterConVar("delta_p2p_server_iroh", "1", FCVAR_NONE, "Servers accept connections over iroh (needs r1delta_iroh.dll)");
    g_serverTailcat = RegisterConVar("delta_p2p_server_tailcat", "1", FCVAR_NONE, "Servers accept connections over tailcat (needs r1delta_tailcat.dll)");
    g_serverTurn = RegisterConVar("delta_p2p_server_turn", "1", FCVAR_NONE, "Servers that are not directly reachable relay through Cloudflare TURN when the master server provides credentials");
    g_punchSpray = RegisterConVar("delta_p2p_punch_spray", "2", FCVAR_NONE, "Extra ports above the client's mapped port to punch (sequential NAT port prediction)");
    g_connectTimeout = RegisterConVar("delta_p2p_connect_timeout_ms", "2500", FCVAR_ARCHIVE, "How long delta_connect probes routes before connecting");
    g_relayPenalty = RegisterConVar("delta_p2p_relay_penalty_ms", "20", FCVAR_ARCHIVE, "Latency handicap applied to relayed routes (TURN, iroh/tailcat relays) when choosing");
    g_prefer = RegisterConVar("delta_p2p_prefer", "", FCVAR_ARCHIVE, "Force a route when it answers: direct, punch, lan, tailscale, eos, iroh, tailcat or turn");
    g_irohRelay = RegisterConVar("delta_p2p_iroh_relay_url", "", FCVAR_NONE, "Custom iroh relay URL (default: n0 public relays)");
    g_tailcatDerpMap = RegisterConVar("delta_p2p_tailcat_derpmap_url", "", FCVAR_NONE, "Custom tailcat DERP map URL (default: tailcat.dev)");
    g_debug = RegisterConVar("delta_p2p_debug", "0", FCVAR_NONE, "Verbose NAT traversal logging");

    RegisterConCommand("delta_connect", ConnectCommand, "Connect to a server using the lowest-latency route (direct, hole punched, LAN, Tailscale, EOS, iroh, tailcat, TURN)", FCVAR_NONE);
    RegisterConCommand("delta_connect_iroh", ConnectIrohCommand, "Connect to a server by its iroh address", FCVAR_NONE);
    RegisterConCommand("delta_connect_tailcat", ConnectTailcatCommand, "Connect to a server by its tailcat address", FCVAR_NONE);
    RegisterConCommand("delta_p2p_status", StatusCommand, "Show NAT traversal / transport status", FCVAR_NONE);
}

void AddHeartbeatFields(nlohmann::json& heartbeat, int port)
{
    if (port <= 0 || port > 0xFFFF)
        return;
    ServerAddHeartbeatFields(heartbeat, static_cast<uint16_t>(port));
}

void OnHeartbeatResponse(const std::string& body)
{
    if (!GetSettings().enable)
        return;
    ServerHandleHeartbeatResponse(body);
}

void OnServerListEntry(const nlohmann::json& entry)
{
    if (!entry.is_object())
        return;
    const std::string ip = entry.value("ip", "");
    const int port = entry.value("port", 0);
    if (ip.empty() || port <= 0)
        return;
    const auto it = entry.find("transports");
    RememberServerTransports(ip + ":" + std::to_string(port), it != entry.end() ? *it : nlohmann::json());
}

} // namespace p2p
