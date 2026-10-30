#include "p2p.h"

#include <cstdarg>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <unordered_map>
#include <cstdlib>
#include <string>

#include "core.h"
#include "cvar.h"
#include "logging.h"
#include "p2p_connect.h"
#include "p2p_identity.h"
#include "p2p_log.h"
#include "p2p_mux.h"
#include "p2p_server.h"
#include "p2p_settings.h"

namespace p2p
{
namespace
{

const CVValue_t* g_enable = nullptr;
const CVValue_t* g_serverUpnp = nullptr;
const CVValue_t* g_serverPunch = nullptr;
const CVValue_t* g_serverEos = nullptr;
const CVValue_t* g_serverIroh = nullptr;
const CVValue_t* g_serverTailcat = nullptr;
const CVValue_t* g_serverTurn = nullptr;
const CVValue_t* g_punchSpray = nullptr;
const CVValue_t* g_connectTimeout = nullptr;
const CVValue_t* g_relayPenalty = nullptr;
const CVValue_t* g_prefer = nullptr;
const CVValue_t* g_irohRelay = nullptr;
const CVValue_t* g_tailcatDerpMap = nullptr;
const CVValue_t* g_debug = nullptr;
const CVValue_t* g_requireIdentity = nullptr;
const CVValue_t* g_identityKey = nullptr;

// Convars live in the R1 ICvar on the client/listen/legacy dedicated modes and
// in the wrapped R1O ICvar on the R1O fake dedicated server; both expose the
// value through the same CVValue_t, so only registration and lookup differ.
int IntValue(const CVValue_t* value, int fallback)
{
    return value ? value->m_nValue : fallback;
}

std::string StringValue(const CVValue_t* value)
{
    return value && value->m_pszString ? std::string(value->m_pszString) : std::string();
}

template <typename ConVarType>
const CVValue_t* ValueOf(ConVarType* var)
{
    if (!var)
        return nullptr;
    ConVarType* parent = var->m_pParent ? var->m_pParent : var;
    return &parent->m_Value;
}

const CVValue_t* RegisterCv(const char* name, const char* value, int flags, const char* help)
{
    if (IsR1ODedicatedServer())
        return ValueOf(RegisterR1ODediConVar(name, value, flags, help));
    return ValueOf(RegisterConVar(name, value, flags, help));
}

void RegisterCmd(const char* name, void (*callback)(const CCommand&), const char* help, int flags)
{
    if (IsR1ODedicatedServer())
        RegisterR1ODediConCommand(name, callback, help, flags);
    else
        RegisterConCommand(name, callback, help, flags);
}

std::string MasterServerUrl()
{
    if (!cvarinterface)
        return {};
    if (IsR1ODedicatedServer())
        return StringValue(ValueOf(CCVar_FindVar(cvarinterface, "delta_ms_url")));
    if (!OriginalCCVar_FindVar)
        return {};
    return StringValue(ValueOf(OriginalCCVar_FindVar(cvarinterface, "delta_ms_url")));
}

uint16_t ParsePort(const char* text, uint16_t fallback)
{
    if (!text || !*text)
        return fallback;
    const long v = std::strtol(text, nullptr, 10);
    return v > 0 && v <= 0xFFFF ? static_cast<uint16_t>(v) : fallback;
}

// Base client m_nSignonState on the 2015 client engine (same offset as
// engine/core/load.cpp ClientLocalSignonState); -1 anywhere else.
int ReadClientSignonState()
{
#ifdef _MSC_VER
    if (GetR1DeltaEngineMode() != R1DeltaEngineMode::Client2015 || !G_engine)
        return -1;
    __try
    {
        return *reinterpret_cast<const int*>(G_engine + 0x797070 + 29 * sizeof(int));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
#else
    return -1;
#endif
}

void ConnectCommand(const CCommand& args)
{
    RefreshSettings();
    if (args.ArgC() < 2)
    {
        Msg("usage: delta_connect <ip[:port]>  (port defaults to %u)\n", static_cast<unsigned>(kDefaultServerPort));
        return;
    }
    ConnectBest(args.Arg(1));
}

void ConnectIrohCommand(const CCommand& args)
{
    RefreshSettings();
    if (args.ArgC() < 2)
    {
        Msg("usage: delta_connect_iroh <iroh address> [game port]\n");
        return;
    }
    ConnectOverlay(Backend::Iroh, args.Arg(1), ParsePort(args.ArgC() > 2 ? args.Arg(2) : nullptr, 37015));
}

void ConnectTailcatCommand(const CCommand& args)
{
    RefreshSettings();
    if (args.ArgC() < 2)
    {
        Msg("usage: delta_connect_tailcat <tailcat address> [game port]\n");
        return;
    }
    ConnectOverlay(Backend::Tailcat, args.Arg(1), ParsePort(args.ArgC() > 2 ? args.Arg(2) : nullptr, 37015));
}

void StatusCommand(const CCommand&)
{
    RefreshSettings();
    std::string out = "Server transports:\n" + ServerDescribe();
    out += "Engine sockets: client " + std::string(GetEngineSocket(Route::Client) != INVALID_SOCKET ? "seen" : "not seen") +
           ", server " + (GetEngineSocket(Route::Server) != INVALID_SOCKET ? "seen" : "not seen") + "; overlay peers " +
           std::to_string(PeerCount()) + "\n";
    out += "Last connection attempt:\n" + ClientDescribe();
    Msg("%s", out.c_str());
}

// Pings arrive on the public game port and may carry spoofed sources, so
// answers are rate limited per source IP (and globally).
bool AllowPong(uint32_t ip)
{
    struct Bucket
    {
        double tokens = 0;
        std::chrono::steady_clock::time_point last{};
    };
    static std::mutex mutex;
    static std::unordered_map<uint32_t, Bucket> buckets;
    static Bucket global;
    constexpr double kPerIpRate = 20, kPerIpBurst = 40, kGlobalRate = 2000, kGlobalBurst = 4000;
    const auto now = std::chrono::steady_clock::now();
    auto take = [&](Bucket& b, double rate, double burst) {
        if (b.last.time_since_epoch().count() == 0)
            b.tokens = burst;
        else
            b.tokens = std::min(burst, b.tokens + std::chrono::duration<double>(now - b.last).count() * rate);
        b.last = now;
        if (b.tokens < 1)
            return false;
        b.tokens -= 1;
        return true;
    };
    std::lock_guard lock(mutex);
    if (buckets.size() > 8192)
        buckets.clear();
    return take(buckets[ip], kPerIpRate, kPerIpBurst) && take(global, kGlobalRate, kGlobalBurst);
}

std::string Ipv4ToStringCompat(uint32_t ip)
{
    const std::string s = Ipv4Endpoint{ ip, 0 }.ToString();
    return s.substr(0, s.rfind(':'));
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

std::mutex g_settingsMutex;
Settings g_settings = [] {
    Settings s;
    s.enable = false; // until Initialize has registered the convars
    return s;
}();
std::chrono::steady_clock::time_point g_settingsRefreshed{};

Settings ReadSettingsFromConVars()
{
    Settings s;
    s.enable = g_enable && IntValue(g_enable, 1) != 0;
    s.serverUpnp = IntValue(g_serverUpnp, 1) != 0;
    s.serverPunch = IntValue(g_serverPunch, 1) != 0;
    s.serverEos = IntValue(g_serverEos, 1) != 0;
    s.serverIroh = IntValue(g_serverIroh, 1) != 0;
    s.serverTailcat = IntValue(g_serverTailcat, 1) != 0;
    s.serverTurn = IntValue(g_serverTurn, 0) != 0;
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
    s.requireIdentity = IntValue(g_requireIdentity, 1);
    s.identityPublicKeyHex = StringValue(g_identityKey);
    if (s.identityPublicKeyHex.empty())
        s.identityPublicKeyHex = kDefaultIdentityPublicKeyHex;
    s.debug = IntValue(g_debug, 0) != 0;
    return s;
}

Settings GetSettings()
{
    std::lock_guard lock(g_settingsMutex);
    return g_settings;
}

void RefreshSettings()
{
    Settings fresh = ReadSettingsFromConVars();
    std::lock_guard lock(g_settingsMutex);
    g_settings = std::move(fresh);
    g_settingsRefreshed = std::chrono::steady_clock::now();
}

void RefreshSettingsNumeric()
{
    // Plain int reads of the convar values; the string fields stay as the
    // engine thread last read them.
    std::lock_guard lock(g_settingsMutex);
    g_settings.enable = g_enable && IntValue(g_enable, 1) != 0;
    g_settings.serverUpnp = IntValue(g_serverUpnp, 1) != 0;
    g_settings.serverPunch = IntValue(g_serverPunch, 1) != 0;
    g_settings.serverEos = IntValue(g_serverEos, 1) != 0;
    g_settings.serverIroh = IntValue(g_serverIroh, 1) != 0;
    g_settings.serverTailcat = IntValue(g_serverTailcat, 1) != 0;
    g_settings.serverTurn = IntValue(g_serverTurn, 0) != 0;
    g_settings.punchSpray = IntValue(g_punchSpray, 2);
    g_settings.requireIdentity = IntValue(g_requireIdentity, 1);
    g_settings.debug = IntValue(g_debug, 0) != 0;
}

void OnEngineThreadTick()
{
    {
        std::lock_guard lock(g_settingsMutex);
        if (std::chrono::steady_clock::now() - g_settingsRefreshed < std::chrono::seconds(1))
            return;
    }
    RefreshSettings();
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
        if (ctx.route == Route::Server && ctx.reply && ParsePingPong(pkt, pp) &&
            (!ctx.viaEngineUdp || AllowPong(ctx.udpFrom.ip)))
            ctx.reply(BuildPong(pp.probeId, pp.timestampUs, 0, ServerPongTag()));
        break;
    }
    case PacketType::Pong:
    {
        PingPong pp;
        if (ctx.route == Route::Client && ParsePingPong(pkt, pp))
            ClientOnPong(pp.probeId, pp.timestampUs, ctx.via, pp.serverTag, ctx.viaEngineUdp, ctx.udpFrom);
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
    case PacketType::Identify:
    {
        if (ctx.route != Route::Server || !ctx.haveSource || !ctx.reply)
            break;
        const int64_t now = static_cast<int64_t>(std::time(nullptr));
        uint32_t ip = 0;
        const IdentityVerdict verdict = Identities().Present(pkt.payload, pkt.payloadSize, ctx.source, now, &ip);
        IdentityToken token;
        Id16 nonce{};
        if (ParseIdentityToken(pkt.payload, pkt.payloadSize, token))
            nonce = token.nonce;
        ctx.reply(BuildIdentifyAck(nonce, verdict));
        if (verdict == IdentityVerdict::Accepted)
            Debug("peer via %s identified as %s\n", ctx.via, Ipv4ToStringCompat(ip).c_str());
        else
            Log("rejected identity from a peer via %s: %s\n", ctx.via, IdentityVerdictName(verdict));
        break;
    }
    case PacketType::IdentifyAck:
    {
        Id16 nonce;
        IdentityVerdict verdict;
        if (ctx.route == Route::Client && ParseIdentifyAck(pkt, nonce, verdict))
            ClientOnIdentifyAck(nonce, static_cast<uint8_t>(verdict));
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

    g_enable = RegisterCv("delta_p2p_enable", "1", FCVAR_NONE,
                              "Enable NAT traversal and alternative transports (EOS, iroh, tailcat, TURN, hole punching, UPnP)");
    g_serverUpnp = RegisterCv("delta_p2p_server_upnp", "1", FCVAR_NONE, "Servers ask the router to forward hostport via UPnP / NAT-PMP");
    g_serverPunch = RegisterCv("delta_p2p_server_punch", "1", FCVAR_NONE, "Servers register with the master server rendezvous for UDP hole punching");
    g_serverEos = RegisterCv("delta_p2p_server_eos", "1", FCVAR_NONE, "Servers log into EOS and advertise their ProductUserId");
    g_serverIroh = RegisterCv("delta_p2p_server_iroh", "1", FCVAR_NONE, "Servers accept connections over iroh (needs r1delta_iroh.dll)");
    g_serverTailcat = RegisterCv("delta_p2p_server_tailcat", "1", FCVAR_NONE, "Servers accept connections over tailcat (needs r1delta_tailcat.dll)");
    g_serverTurn = RegisterCv("delta_p2p_server_turn", "0", FCVAR_NONE, "Opt in to Cloudflare TURN when the master server provides credentials (disabled for this rollout)");
    g_punchSpray = RegisterCv("delta_p2p_punch_spray", "2", FCVAR_NONE, "Extra ports above the client's mapped port to punch (sequential NAT port prediction)");
    g_connectTimeout = RegisterCv("delta_p2p_connect_timeout_ms", "2500", FCVAR_ARCHIVE, "How long delta_connect probes routes before connecting");
    g_relayPenalty = RegisterCv("delta_p2p_relay_penalty_ms", "20", FCVAR_ARCHIVE, "Latency handicap applied to relayed routes (TURN, iroh/tailcat relays) when choosing");
    g_prefer = RegisterCv("delta_p2p_prefer", "", FCVAR_ARCHIVE, "Force a route when it answers: direct, punch, lan, tailscale, eos, iroh, tailcat or turn");
    g_irohRelay = RegisterCv("delta_p2p_iroh_relay_url", "", FCVAR_NONE, "Custom iroh relay URL (default: n0 public relays)");
    g_tailcatDerpMap = RegisterCv("delta_p2p_tailcat_derpmap_url", "", FCVAR_NONE, "Custom tailcat DERP map URL (default: tailcat.dev)");
    g_debug = RegisterCv("delta_p2p_debug", "0", FCVAR_NONE, "Verbose NAT traversal logging");
    g_requireIdentity = RegisterCv("delta_p2p_require_identity", "1", FCVAR_NONE,
                                       "Servers only accept EOS/iroh/tailcat players that present a master-server attested IP (so IP bans apply on every transport): 0 = off, 1 = when the master server supports it, 2 = always");
    g_identityKey = RegisterCv("delta_p2p_identity_pubkey", "", FCVAR_NONE,
                                   "Hex X||Y of the master server's P-256 identity key (empty = official master server)");

    RegisterCmd("delta_connect", ConnectCommand, "Connect to a server using the lowest-latency route (direct, hole punched, LAN, Tailscale, EOS, iroh, tailcat, TURN)", FCVAR_NONE);
    RegisterCmd("delta_connect_iroh", ConnectIrohCommand, "Connect to a server by its iroh address", FCVAR_NONE);
    RegisterCmd("delta_connect_tailcat", ConnectTailcatCommand, "Connect to a server by its tailcat address", FCVAR_NONE);
    RegisterCmd("delta_p2p_status", StatusCommand, "Show NAT traversal / transport status", FCVAR_NONE);
    SetClientSignonStateReader(ReadClientSignonState);
    RefreshSettings();
}

void AddHeartbeatFields(nlohmann::json& heartbeat, int port)
{
    if (port <= 0 || port > 0xFFFF)
        return;
    ServerAddHeartbeatFields(heartbeat, static_cast<uint16_t>(port));
}

void EnsureServerServices(int port)
{
    if (port <= 0 || port > 0xFFFF)
        return;
    ServerEnsureServices(static_cast<uint16_t>(port));
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
