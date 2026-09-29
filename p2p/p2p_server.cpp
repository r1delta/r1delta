#include "p2p_server.h"

#include <winsock2.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <thread>

#include "p2p_eos_bridge.h"
#include "p2p_log.h"
#include "p2p_mux.h"
#include "p2p_netinfo.h"
#include "p2p_plugin.h"
#include "p2p_settings.h"
#include "p2p_turn.h"
#include "p2p_upnp.h"

namespace p2p
{
namespace
{

using Clock = std::chrono::steady_clock;
using json = nlohmann::json;

constexpr auto kRegisterInterval = std::chrono::seconds(10);
constexpr auto kHeartbeatStale = std::chrono::seconds(45);
constexpr auto kTurnIdleShutdown = std::chrono::minutes(5);
constexpr int kPunchBurst = 8;
constexpr auto kPunchSpacing = std::chrono::milliseconds(120);

struct PendingPunch
{
    Id16 ticket{};
    Ipv4Endpoint target;
    int remaining = 0;
    Clock::time_point next{};
};

struct ServerState
{
    std::mutex mutex;
    uint16_t port = 0;
    bool servicesStarted = false;
    std::string eosPuid;
    std::string irohAddress;    // "id|relay|ip:port,..."
    std::string tailcatAddress; // "tc..."
    std::string status[4];      // eos, iroh, tailcat, rendezvous (for delta_p2p_status)

    Ipv4Endpoint rendezvous;
    Id16 token{};
    bool haveToken = false;
    Ipv4Endpoint observedMapping;
    Clock::time_point lastHeartbeat{};
    Clock::time_point lastRegister{};
    std::deque<PendingPunch> punches;

    std::thread serviceThread;
};

ServerState& State()
{
    static ServerState state;
    return state;
}

std::string Utf8(const std::wstring& w)
{
    if (w.empty())
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

// %LOCALAPPDATA%\R1Delta\p2p\ (falls back to the DLL directory).
std::string IdentityDirectory()
{
    wchar_t buf[MAX_PATH]{};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    std::wstring dir = (n > 0 && n < MAX_PATH) ? std::wstring(buf, n) + L"\\R1Delta\\p2p\\" : ModuleDirectory() + L"p2p\\";
    return Utf8(dir);
}

std::string PluginConfig(Backend backend, uint16_t port, const Settings& settings)
{
    json cfg = json::object();
    if (backend == Backend::Iroh)
    {
        cfg["key_file"] = IdentityDirectory() + "iroh_" + std::to_string(port) + ".key";
        if (!settings.irohRelayUrl.empty())
            cfg["relay_url"] = settings.irohRelayUrl;
    }
    else
    {
        cfg["key_file"] = IdentityDirectory() + "tailcat_" + std::to_string(port) + ".json";
        if (!settings.tailcatDerpMapUrl.empty())
            cfg["relay_url"] = settings.tailcatDerpMapUrl;
        cfg["verbose"] = settings.debug;
    }
    return cfg.dump();
}

void StartOverlay(PluginBackend& plugin, uint16_t port, const Settings& settings, int statusIndex, std::string ServerState::* out)
{
    auto& st = State();
    if (!plugin.IsAvailable())
    {
        std::lock_guard lock(st.mutex);
        st.status[statusIndex] = "plugin not installed";
        return;
    }
    {
        std::lock_guard lock(st.mutex);
        st.status[statusIndex] = "starting";
    }
    std::string address;
    const bool ok = plugin.EnsureReady(PluginConfig(plugin.Kind(), port, settings)) && plugin.Listen(port, address);
    std::lock_guard lock(st.mutex);
    if (ok)
    {
        st.*out = address;
        st.status[statusIndex] = "listening";
        Log("%s listening: %s\n", BackendName(plugin.Kind()), address.c_str());
    }
    else
    {
        st.status[statusIndex] = "failed";
    }
}

void SendRegister()
{
    auto& st = State();
    const SOCKET s = GetEngineSocket(Route::Server);
    if (s == INVALID_SOCKET || !st.haveToken || !st.rendezvous.Valid())
        return;
    RealSendTo(s, BuildSrvRegister(st.token), st.rendezvous);
    st.lastRegister = Clock::now();
}

void ServiceLoop()
{
    auto& st = State();
    for (;;)
    {
        Sleep(50);
        const auto now = Clock::now();
        const Settings settings = GetSettings();
        {
            std::lock_guard lock(st.mutex);
            const bool heartbeating = now - st.lastHeartbeat < kHeartbeatStale;

            // Keep the game socket's NAT mapping towards the rendezvous alive.
            if (heartbeating && settings.serverPunch && now - st.lastRegister > kRegisterInterval)
                SendRegister();

            // Hole punching towards clients announced by the master server.
            const SOCKET s = GetEngineSocket(Route::Server);
            for (auto it = st.punches.begin(); it != st.punches.end();)
            {
                if (now < it->next)
                {
                    ++it;
                    continue;
                }
                if (s != INVALID_SOCKET)
                {
                    const auto punch = BuildPunch(it->ticket, 1);
                    RealSendTo(s, punch, it->target);
                    // Cheap port prediction for NATs that allocate sequentially.
                    if (it->remaining == kPunchBurst)
                        for (int d = 1; d <= settings.punchSpray; ++d)
                            RealSendTo(s, punch, Ipv4Endpoint{ it->target.ip, static_cast<uint16_t>(it->target.port + d) });
                }
                it->next = now + kPunchSpacing;
                if (--it->remaining <= 0)
                    it = st.punches.erase(it);
                else
                    ++it;
            }

            if (Turn().Running() && now - st.lastHeartbeat > kTurnIdleShutdown)
            {
                Log("TURN: no heartbeat for a while, releasing relay\n");
                std::thread([] { Turn().Stop(); }).detach();
            }
        }
        IrohPlugin().PumpLogs();
        TailcatPlugin().PumpLogs();
    }
}

void StartServices(uint16_t port)
{
    auto& st = State();
    const Settings settings = GetSettings();
    {
        std::lock_guard lock(st.mutex);
        if (st.servicesStarted)
            return;
        st.servicesStarted = true;
        st.port = port;
        st.serviceThread = std::thread(ServiceLoop);
        st.serviceThread.detach();
    }

    if (settings.serverUpnp)
        PortMap().Start(port);

    if (settings.serverEos)
    {
        std::thread([] {
            auto& st = State();
            {
                std::lock_guard lock(st.mutex);
                st.status[0] = "starting";
            }
            const bool ok = eos_bridge::EnsureInitialized();
            const std::string puid = ok ? eos_bridge::LocalProductUserId() : std::string();
            std::lock_guard lock(st.mutex);
            st.eosPuid = puid;
            st.status[0] = puid.empty() ? "failed" : "ready";
        }).detach();
    }
    if (settings.serverIroh)
        std::thread([port, settings] { StartOverlay(IrohPlugin(), port, settings, 1, &ServerState::irohAddress); }).detach();
    if (settings.serverTailcat)
        std::thread([port, settings] { StartOverlay(TailcatPlugin(), port, settings, 2, &ServerState::tailcatAddress); }).detach();
}

json IrohTransportJson(const std::string& address)
{
    // "id|relay|ip:port,ip:port"
    json out = json::object();
    const size_t a = address.find('|');
    out["id"] = address.substr(0, a);
    if (a == std::string::npos)
        return out;
    const size_t b = address.find('|', a + 1);
    const std::string relay = address.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
    if (!relay.empty())
        out["relay"] = relay;
    json addrs = json::array();
    if (b != std::string::npos)
    {
        std::string rest = address.substr(b + 1);
        size_t pos = 0;
        while (pos <= rest.size())
        {
            const size_t comma = rest.find(',', pos);
            const std::string item = rest.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
            if (auto ep = Ipv4Endpoint::Parse(item); ep && !IsPrivateIpv4(ep->ip))
                addrs.push_back(item); // only public addresses are worth advertising
            if (comma == std::string::npos)
                break;
            pos = comma + 1;
        }
    }
    if (!addrs.empty())
        out["addrs"] = addrs;
    return out;
}

} // namespace

void ServerAddHeartbeatFields(json& heartbeat, uint16_t port)
{
    const Settings settings = GetSettings();
    if (!settings.enable || port == 0)
        return;
    StartServices(port);

    auto& st = State();
    json transports = json::object();
    json nat = json::object();
    {
        std::lock_guard lock(st.mutex);
        if (!st.eosPuid.empty())
            transports["eos"] = { { "puid", st.eosPuid } };
        if (!st.irohAddress.empty())
            transports["iroh"] = IrohTransportJson(st.irohAddress);
        if (!st.tailcatAddress.empty())
            transports["tailcat"] = { { "addr", st.tailcatAddress } };
    }
    const LocalAddresses local = GetLocalAddresses();
    if (!local.tailscale.empty())
    {
        json ips = json::array();
        for (uint32_t ip : local.tailscale)
            ips.push_back(Ipv4ToString(ip));
        transports["tailscale"] = { { "ips", ips } };
    }
    if (auto relay = Turn().Relayed())
        transports["turn"] = { { "relay", relay->ToString() } };

    json lan = json::array();
    for (uint32_t ip : local.lan)
        lan.push_back(Ipv4Endpoint{ ip, port }.ToString());
    nat["lan"] = lan;
    nat["want_turn"] = settings.serverTurn;
    const PortMappingStatus mapping = PortMap().Status();
    nat["upnp"] = mapping.active ? mapping.method : "";
    nat["upnp_external"] = mapping.externalIp.empty() ? "" : mapping.externalIp + ":" + std::to_string(mapping.externalPort);

    heartbeat["transports"] = transports;
    heartbeat["nat"] = nat;
}

void ServerHandleHeartbeatResponse(const std::string& body)
{
    json r;
    try
    {
        r = json::parse(body);
    }
    catch (...)
    {
        return; // older master servers reply with an empty body
    }
    if (!r.is_object())
        return;

    const Settings settings = GetSettings();
    auto& st = State();
    bool registerNow = false;
    uint32_t rendezvousIp = 0;
    {
        std::lock_guard lock(st.mutex);
        st.lastHeartbeat = Clock::now();
        if (r.contains("rendezvous") && r["rendezvous"].is_string())
        {
            if (auto ep = Ipv4Endpoint::Parse(r["rendezvous"].get<std::string>()))
            {
                registerNow |= !(st.rendezvous == *ep);
                st.rendezvous = *ep;
            }
        }
        if (r.contains("token") && r["token"].is_string())
        {
            Id16 token;
            if (HexToId16(r["token"].get<std::string>(), token))
            {
                registerNow |= !st.haveToken || token != st.token;
                st.token = token;
                st.haveToken = true;
            }
        }
        if (registerNow && settings.serverPunch)
            SendRegister();
        rendezvousIp = st.rendezvous.ip;
    }

    if (settings.serverTurn && r.contains("turn") && r["turn"].is_object())
    {
        const json& t = r["turn"];
        TurnCredentials creds;
        creds.username = t.value("username", "");
        creds.password = t.value("credential", "");
        bool haveUrl = false;
        if (t.contains("urls") && t["urls"].is_array())
        {
            for (const auto& u : t["urls"])
            {
                if (u.is_string() && ParseTurnUrl(u.get<std::string>(), creds.host, creds.port))
                {
                    haveUrl = true;
                    break;
                }
            }
        }
        if (haveUrl && !creds.username.empty())
        {
            Turn().Start(creds);
            // Lets the master server validate us through the relay.
            Turn().Permit(rendezvousIp);
        }
    }
}

void ServerOnRegisterAck(const RegisterAck& ack, const Ipv4Endpoint& from)
{
    auto& st = State();
    std::lock_guard lock(st.mutex);
    if (from != st.rendezvous)
        return;
    if (!(st.observedMapping == ack.observed))
    {
        st.observedMapping = ack.observed;
        Log("Rendezvous sees this server at %s\n", ack.observed.ToString().c_str());
    }
    st.status[3] = "registered as " + ack.observed.ToString();
}

void ServerOnPunchRequest(const PunchRequest& req, const Ipv4Endpoint& from)
{
    auto& st = State();
    const Settings settings = GetSettings();
    {
        std::lock_guard lock(st.mutex);
        if (from != st.rendezvous)
            return; // only the master server may ask us to punch
        const SOCKET s = GetEngineSocket(Route::Server);
        if (s != INVALID_SOCKET)
            RealSendTo(s, BuildPunchReqAck(req.ticket, req.client), st.rendezvous);
        if (settings.serverPunch && req.client.port != 0)
        {
            for (const auto& p : st.punches)
                if (p.ticket == req.ticket && p.target == req.client)
                    return;
            if (st.punches.size() < 64)
                st.punches.push_back(PendingPunch{ req.ticket, req.client, kPunchBurst, Clock::now() });
        }
    }
    if (Turn().Running())
        Turn().Permit(req.client.ip);
    Debug("Punch request for %s\n", req.client.ToString().c_str());
}

std::string ServerDescribe()
{
    auto& st = State();
    std::string out;
    {
        std::lock_guard lock(st.mutex);
        if (!st.servicesStarted)
            return "  server services not started (no master server heartbeat yet)\n";
        out += "  port:       " + std::to_string(st.port) + "\n";
        out += "  eos:        " + (st.eosPuid.empty() ? st.status[0] : st.eosPuid) + "\n";
        out += "  iroh:       " + (st.irohAddress.empty() ? st.status[1] : st.irohAddress) + "\n";
        out += "  tailcat:    " + (st.tailcatAddress.empty() ? st.status[2] : st.tailcatAddress) + "\n";
        out += "  rendezvous: " + (st.rendezvous.Valid() ? st.rendezvous.ToString() : std::string("none")) + " " +
               st.status[3] + "\n";
    }
    const PortMappingStatus mapping = PortMap().Status();
    out += "  portmap:    " +
           (mapping.active ? mapping.method + " " + mapping.externalIp + ":" + std::to_string(mapping.externalPort)
                           : "inactive " + mapping.error) +
           "\n";
    out += "  turn:       " + Turn().Describe() + "\n";
    const LocalAddresses local = GetLocalAddresses();
    out += "  tailscale:  ";
    for (uint32_t ip : local.tailscale)
        out += Ipv4ToString(ip) + " ";
    out += "\n";
    return out;
}

} // namespace p2p
