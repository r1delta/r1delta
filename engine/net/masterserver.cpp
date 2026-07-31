#pragma once
#include <cstdlib>
#include <crtdbg.h>
#include <new>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <mutex>
#include <atomic>
#include <chrono>
#include <thread>
#include <ctime>
#include <random>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include "load.h"
#include "cvar.h"
#include "factory.h"
#include "logging.h"
#include "masterserver.h"
#include "r1d_version.h"

using json = nlohmann::json;

// --------------------------------
// Data Structures
// --------------------------------
struct PlayerInfo {
    std::string name;
    int gen;
    int lvl;
    int team;
};

struct ServerInfo {
    std::string hostName;
    std::string mapName;
    std::string gameMode;
    int maxPlayers;
    int port;
    bool has_auth;
    std::string ip;
	bool hasPassword;
    std::string version;
	std::string description;
	std::string playlist;
	std::string playlist_display_name;
    std::vector<PlayerInfo> players;
};

struct HeartbeatInfo {
    std::string hostName;
    std::string mapName;
    std::string gameMode;
    int maxPlayers;
    int port;
    bool has_auth;
	bool hasPassword;
	std::string description;
    std::string playlist;
	std::string playlist_display_name;
    std::vector<PlayerInfo> players;
};

// --------------------------------
// CVar Declarations
// --------------------------------
static ConVarR1O* delta_ms_url = nullptr;
static ConVarR1O* host_map = nullptr;
static ConVarR1O* hide_server = nullptr;
static ConVarR1O* server_description = nullptr;

// --------------------------------
// CVar Initialization
// --------------------------------
void InitMasterServerCVars() {
    static bool initialized = false;
    if (!initialized || !delta_ms_url || !host_map || !hide_server || !server_description) {
        delta_ms_url = CCVar_FindVar(cvarinterface, "delta_ms_url");
        host_map = CCVar_FindVar(cvarinterface, "host_map");
        hide_server = CCVar_FindVar(cvarinterface, "hide_server");
		server_description = CCVar_FindVar(cvarinterface, "server_description");
        
        initialized = true;
    }
}

static bool IsMasterServerPortValid(int port)
{
    return port >= 1025 && port <= 0xFFFF;
}

static int ResolveMasterServerPort()
{
    if (IsR1ODedicatedServer()) {
        const int boundPort = GetR1ODedicatedBoundServerPort();
        if (IsMasterServerPortValid(boundPort))
            return boundPort;
    }

    if (cvarinterface && OriginalCCVar_FindVar) {
        if (ConVarR1* backingHostPort =
                OriginalCCVar_FindVar(cvarinterface, "hostport")) {
            const int port = backingHostPort->m_Value.m_nValue;
            if (IsMasterServerPortValid(port))
                return port;
        }
    }

    if (ConVarR1O* wrappedHostPort =
            CCVar_FindVar(cvarinterface, "hostport")) {
        const int port = wrappedHostPort->m_Value.m_nValue;
        if (IsMasterServerPortValid(port))
            return port;
    }

    return 0;
}

namespace MasterServerClient {
    static std::vector<ServerInfo> serverList;
    static SRWLOCK serverListMutex = SRWLOCK_INIT;
    static SRWLOCK httpMutex = SRWLOCK_INIT;
    static SRWLOCK heartbeatMutex = SRWLOCK_INIT;
    static std::unique_ptr<httplib::Client> httpClient;
    static HeartbeatInfo lastHeartbeat;
    static std::atomic<bool> heartbeatThreadRunning{false};
    static std::atomic<std::chrono::system_clock::time_point> lastHeartbeatTime{std::chrono::system_clock::now()};
    std::atomic<bool> IsValidHeartBeat{ false };

    // --------------------------------
    // Internal: Ensures httpClient is valid
    // --------------------------------
    static void EnsureHttpClient(const std::string& url) {
        static std::string lastUrl;
        if (!httpClient || url != lastUrl) {
            httpClient = std::make_unique<httplib::Client>(url);
            httpClient->set_connection_timeout(3);
            httpClient->set_address_family(AF_INET);
            lastUrl = url;
        }
    }

    // --------------------------------
    // Heartbeat
    // --------------------------------
    bool SendServerHeartbeat(const HeartbeatInfo& heartbeat, bool isHibernating = false) {
        InitMasterServerCVars();
        if (!delta_ms_url || !delta_ms_url->m_Value.m_pszString || !delta_ms_url->m_Value.m_pszString[0]) {
            Warning("MasterServerClient: delta_ms_url not set\n");
            return false;
        }
        if (hide_server && hide_server->m_Value.m_nValue == 1) {
            static bool hasWarned = false;
            if (!hasWarned) {
                hasWarned = true;
                Warning("hide_server is 1, ignoring master server heartbeat requests\n");
            }
            return true;
        }

        const int port = ResolveMasterServerPort();
        if (!IsMasterServerPortValid(port)) {
            Warning(
                "MasterServerClient: Heartbeat skipped - no valid bound server port\n");
            return false;
        }

        SRWGuard lock(&httpMutex);
        EnsureHttpClient(delta_ms_url->m_Value.m_pszString);

        json j;
        j["host_name"] = heartbeat.hostName;
        j["map_name"] = heartbeat.mapName;
        j["game_mode"] = heartbeat.gameMode;
        j["max_players"] = heartbeat.maxPlayers;
        j["port"] = port;
        auto auth_Var = CCVar_FindVar(cvarinterface, "delta_online_auth_enable");
		j["has_auth"] = auth_Var && auth_Var->m_Value.m_nValue != 0;
        j["version"] = R1D_VERSION;
        auto password_var = CCVar_FindVar(cvarinterface, "sv_password");
        if (password_var && password_var->m_Value.m_pszString && password_var->m_Value.m_pszString[0]) {
            j["has_password"] = true;
        }
        else {
            j["has_password"] = false;
        }
        j["description"] = heartbeat.description;
        j["playlist"] = heartbeat.playlist;
        j["playlist_display_name"] = heartbeat.playlist_display_name;
        j["players"] = json::array();
        if (!isHibernating) {
            for (auto& p : heartbeat.players) {
                json pj;
                pj["name"] = p.name;
                pj["gen"] = p.gen;
                pj["lvl"] = p.lvl;
                pj["team"] = p.team;
                j["players"].push_back(pj);
            }
        }

        if (IsR1ODedicatedServer() && AreR1OFakeDediVerboseLogsEnabled()) {
            char diagnostic[192];
            _snprintf_s(
                diagnostic,
                sizeof(diagnostic),
                _TRUNCATE,
                "R1Delta: R1O heartbeat using bound port %d\n",
                port);
            OutputDebugStringA(diagnostic);
        }

        auto res = httpClient->Post("/heartbeat", j.dump(), "application/json");
        if (!res || (res->status != 200 && res->status != 429)) {

            Warning("MasterServerClient: Heartbeat failed - %s\n",
                res ? res->body.c_str() : "Connection failed");
            IsValidHeartBeat.store(false, std::memory_order_release);
            return false;
        }
        IsValidHeartBeat.store(true, std::memory_order_release);
        return true;
    }

    // --------------------------------
    // Heartbeat Thread
    // --------------------------------
    void StartHeartbeatThread() {
        if (heartbeatThreadRunning.load(std::memory_order_acquire)) {
            return; // Thread is already running
        }

        heartbeatThreadRunning.store(true, std::memory_order_release);
        
        // Start with a random delay (1-3 seconds) to avoid flooding the master server
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> delayDist(1, 3);
        std::this_thread::sleep_for(std::chrono::seconds(delayDist(gen)));
        
        std::thread([]{
            while (heartbeatThreadRunning.load(std::memory_order_acquire)) {
                HeartbeatInfo currentHeartbeat;
                bool isHibernating = false;
                
                {
                    SRWGuard lock(&heartbeatMutex);
                    currentHeartbeat = lastHeartbeat;
                    
                    // Check if server is hibernating (no heartbeat for 60 seconds)
                    auto now = std::chrono::system_clock::now();
                    auto lastTime = lastHeartbeatTime.load(std::memory_order_acquire);
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime).count();
                    
                    if (elapsed > 60) {
                        isHibernating = true;
                        // Clear player list during hibernation
                        currentHeartbeat.players.clear();
                    }
                }
                
                // Send the heartbeat
                SendServerHeartbeat(currentHeartbeat, isHibernating);
                
                // Wait 5-7 seconds before next heartbeat
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<> waitDist(5, 7);
                std::this_thread::sleep_for(std::chrono::seconds(waitDist(gen)));
            }
        }).detach();
    }

    // --------------------------------
    // Server List
    // --------------------------------
    void GetServerList() {
        InitMasterServerCVars();

        if (!delta_ms_url) 
            return;
        EnsureHttpClient(delta_ms_url->m_Value.m_pszString);
        auto res = httpClient->Get("/servers");

        if (!res || res->status != 200)
            return;

        try {
            auto j = json::parse(res->body);
            std::vector<ServerInfo> newServerList;
            
            for (auto& sj : j) {
                ServerInfo si;
                si.hostName = sj["host_name"];
                si.mapName = sj["map_name"];
                si.gameMode = sj["game_mode"];
                si.maxPlayers = sj["max_players"];
                si.port = sj["port"];
                si.ip = sj["ip"];
				si.hasPassword = sj["has_password"];
				si.has_auth = sj["has_auth"];
				si.description = sj["description"];
				si.playlist = sj["playlist"];
				si.playlist_display_name = sj["playlist_display_name"];
				si.version = sj["version"];
                for (auto& pj : sj["players"]) {
                    PlayerInfo pi;
                    pi.name = pj["name"];
                    pi.gen = pj["gen"];
                    pi.lvl = pj["lvl"];
                    pi.team = pj["team"];
                    si.players.push_back(pi);
                }
                newServerList.push_back(si);
            }
            
            // Update the server list with mutex protection
            {
                SRWGuard lock(&serverListMutex);
                serverList = std::move(newServerList);
            }
        }
        catch (...) {
            Warning("MasterServerClient: Invalid server list response\n");
            return;
        }
    }

    // --------------------------------
    // Stop Heartbeat Thread
    // --------------------------------
    void StopHeartbeatThread() {
        heartbeatThreadRunning.store(false, std::memory_order_release);
    }

    // --------------------------------
    // On Shutdown
    // --------------------------------
    void OnServerShutdown() {
        StopHeartbeatThread();
        InitMasterServerCVars();
        if (!delta_ms_url) return;
        const int port = ResolveMasterServerPort();
        if (!IsMasterServerPortValid(port))
            return;
        EnsureHttpClient(delta_ms_url->m_Value.m_pszString);
        std::string path = "/heartbeat/" + std::to_string(port);
        auto res = httpClient->Delete(path.c_str());
        if (!res || res->status != 200)
            Warning("MasterServerClient: Shutdown notification failed\n");
    }
} // namespace MasterServerClient

// --------------------------------
// Squirrel Interface
// --------------------------------
static bool MasterServerReadableProtect(DWORD protect)
{
    if (protect & (PAGE_GUARD | PAGE_NOACCESS))
        return false;

    protect &= 0xff;
    return protect == PAGE_READONLY
        || protect == PAGE_READWRITE
        || protect == PAGE_WRITECOPY
        || protect == PAGE_EXECUTE_READ
        || protect == PAGE_EXECUTE_READWRITE
        || protect == PAGE_EXECUTE_WRITECOPY;
}

static bool MasterServerReadableRange(const void* ptr, size_t size)
{
    if (!ptr || !size)
        return false;

    uintptr_t current = reinterpret_cast<uintptr_t>(ptr);
    const uintptr_t end = current + size;
    if (end < current)
        return false;

    while (current < end) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(reinterpret_cast<const void*>(current), &mbi, sizeof(mbi)))
            return false;
        if (mbi.State != MEM_COMMIT || !MasterServerReadableProtect(mbi.Protect))
            return false;

        const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (regionEnd <= current)
            return false;
        current = regionEnd < end ? regionEnd : end;
    }

    return true;
}

static std::string MasterServerReadSQString(const SQString* str)
{
    if (!str || !MasterServerReadableRange(str, 0x3A))
        return {};

    const unsigned char* base = reinterpret_cast<const unsigned char*>(str);
    const int length = *reinterpret_cast<const int*>(base + 0x28);
    const char* value = reinterpret_cast<const char*>(base + 0x38);

    if (length >= 0 && length <= 8192 && MasterServerReadableRange(value, static_cast<size_t>(length) + 1))
        return std::string(value, static_cast<size_t>(length));

    std::string fallback;
    fallback.reserve(64);
    for (int i = 0; i < 8192; ++i) {
        if (!MasterServerReadableRange(value + i, 1) || value[i] == '\0')
            break;
        fallback.push_back(value[i]);
    }
    return fallback;
}

static std::string MasterServerReadSQStringObject(const SQObject& obj)
{
    if (obj._type != OT_STRING)
        return {};
    return MasterServerReadSQString(obj._unVal.pString);
}

static bool MasterServerParsePlayerTable(const SQObject& obj, PlayerInfo& player)
{
    if (obj._type != OT_TABLE || !obj._unVal.pTable || !MasterServerReadableRange(obj._unVal.pTable, sizeof(SQTable)))
        return false;

    SQTable* table = obj._unVal.pTable;
    const int nodeCount = table->_numOfNodes;
    if (nodeCount <= 0 || nodeCount > 256 || !table->_nodes || !MasterServerReadableRange(table->_nodes, sizeof(SQTable::_HashNode) * static_cast<size_t>(nodeCount)))
        return false;

    bool parsedAny = false;
    for (int i = 0; i < nodeCount; ++i) {
        const auto& node = table->_nodes[i];
        if (node.key._type != OT_STRING)
            continue;

        std::string key = MasterServerReadSQStringObject(node.key);
        if (key.empty())
            continue;

        if (node.val._type == OT_STRING) {
            std::string value = MasterServerReadSQStringObject(node.val);
            if (key == "name") {
                player.name = std::move(value);
                parsedAny = true;
            }
        }
        else if (node.val._type == OT_INTEGER) {
            if (key == "gen") {
                player.gen = static_cast<int>(node.val._unVal.nInteger);
                parsedAny = true;
            }
            else if (key == "lvl") {
                player.lvl = static_cast<int>(node.val._unVal.nInteger);
                parsedAny = true;
            }
            else if (key == "team") {
                player.team = static_cast<int>(node.val._unVal.nInteger);
                parsedAny = true;
            }
        }
    }

    return parsedAny;
}

static bool MasterServerParsePlayersArray(const SQObject& obj, std::vector<PlayerInfo>& players)
{
    if (obj._type != OT_ARRAY || !obj._unVal.pArray || !MasterServerReadableRange(obj._unVal.pArray, sizeof(SQArray)))
        return false;

    SQArray* arr = obj._unVal.pArray;
    const int usedSlots = arr->_usedSlots;
    if (usedSlots < 0 || usedSlots > 128 || (usedSlots > 0 && (!arr->_values || !MasterServerReadableRange(arr->_values, sizeof(SQObject) * static_cast<size_t>(usedSlots)))))
        return false;

    players.clear();
    for (int i = 0; i < usedSlots; ++i) {
        PlayerInfo player{};
        if (MasterServerParsePlayerTable(arr->_values[i], player))
            players.push_back(std::move(player));
    }
    return true;
}

static bool MasterServerParseHeartbeatTable(const SQObject& obj, HeartbeatInfo& heartbeat)
{
    if (obj._type != OT_TABLE || !obj._unVal.pTable || !MasterServerReadableRange(obj._unVal.pTable, sizeof(SQTable)))
        return false;

    SQTable* table = obj._unVal.pTable;
    const int nodeCount = table->_numOfNodes;
    if (nodeCount <= 0 || nodeCount > 512 || !table->_nodes || !MasterServerReadableRange(table->_nodes, sizeof(SQTable::_HashNode) * static_cast<size_t>(nodeCount)))
        return false;

    bool parsedAny = false;
    std::vector<PlayerInfo> players;
    for (int i = 0; i < nodeCount; ++i) {
        const auto& node = table->_nodes[i];
        if (node.key._type != OT_STRING)
            continue;

        std::string key = MasterServerReadSQStringObject(node.key);
        if (key.empty())
            continue;

        if (node.val._type == OT_STRING) {
            std::string value = MasterServerReadSQStringObject(node.val);
            if (key == "host_name") {
                heartbeat.hostName = std::move(value);
                parsedAny = true;
            }
            else if (key == "map_name") {
                heartbeat.mapName = std::move(value);
                parsedAny = true;
            }
            else if (key == "game_mode") {
                heartbeat.gameMode = std::move(value);
                parsedAny = true;
            }
            else if (key == "playlist") {
                heartbeat.playlist = std::move(value);
                parsedAny = true;
            }
            else if (key == "playlist_display_name") {
                heartbeat.playlist_display_name = std::move(value);
                parsedAny = true;
            }
        }
        else if (node.val._type == OT_INTEGER) {
            if (key == "max_players") {
                heartbeat.maxPlayers = static_cast<int>(node.val._unVal.nInteger);
                parsedAny = true;
            }
            else if (key == "has_auth") {
                heartbeat.has_auth = node.val._unVal.nInteger != 0;
                parsedAny = true;
            }
        }
        else if (node.val._type == OT_ARRAY && key == "players") {
            if (MasterServerParsePlayersArray(node.val, players)) {
                heartbeat.players = std::move(players);
                parsedAny = true;
            }
        }
    }

    return parsedAny;
}

static const char* MasterServerCVarString(const char* name, const char* fallback = "")
{
    if (!cvarinterface || !name)
        return fallback;
    ConVarR1O* var = CCVar_FindVar(cvarinterface, name);
    if (!var || !var->m_Value.m_pszString)
        return fallback;
    return var->m_Value.m_pszString;
}

static int MasterServerCVarInt(const char* name, int fallback = 0)
{
    if (!cvarinterface || !name)
        return fallback;
    ConVarR1O* var = CCVar_FindVar(cvarinterface, name);
    return var ? var->m_Value.m_nValue : fallback;
}

static void MasterServerFillHeartbeatFallbacks(HeartbeatInfo& heartbeat)
{
    if (heartbeat.hostName.empty())
        heartbeat.hostName = MasterServerCVarString("hostname");
    if (heartbeat.mapName.empty())
        heartbeat.mapName = host_map && host_map->m_Value.m_pszString ? host_map->m_Value.m_pszString : "";
    if (heartbeat.gameMode.empty())
        heartbeat.gameMode = MasterServerCVarString("mp_gamemode");
    if (heartbeat.playlist.empty())
        heartbeat.playlist = MasterServerCVarString("playlist");
    if (heartbeat.maxPlayers <= 0)
        heartbeat.maxPlayers = MasterServerCVarInt("maxplayers");
}

SQInteger GetServerHeartbeat(HSQUIRRELVM v) {
    InitMasterServerCVars();
    SQObject obj;
    SQInteger top = sq_gettop(nullptr, v);
    if (top < 2) {
        Warning("GetServerHeartbeat: Stack has less than 2 elements\n");
        return 1;
    }
    if (SQ_FAILED(sq_getstackobj(nullptr, v, 2, &obj))) {
        Warning("GetServerHeartbeat: Failed to get stack object at position 2\n");
        return 1;
    }
    if (obj._type != OT_TABLE) {
        Warning("GetServerHeartbeat: Object at stack pos 2 is not a table, type %d\n", obj._type);
        return 1;
    }

    HeartbeatInfo heartbeat{};
    heartbeat.port = ResolveMasterServerPort();
	heartbeat.description = server_description && server_description->m_Value.m_pszString ? server_description->m_Value.m_pszString : "";
    const bool parsed = MasterServerParseHeartbeatTable(obj, heartbeat);
    if (!parsed && IsR1ODedicatedServer()) {
        static std::atomic<unsigned int> r1oHeartbeatFallbacks{0};
        if (r1oHeartbeatFallbacks.fetch_add(1, std::memory_order_relaxed) == 0)
            Warning("GetServerHeartbeat: R1O heartbeat table parse failed; using shared cvar fallbacks\n");
    }
    MasterServerFillHeartbeatFallbacks(heartbeat);

    // Update the last heartbeat time and data
    {
        ZoneScopedN("GetServerHeartbeat heartbeatMutex | update heartbeat");
        SRWGuard lock(&MasterServerClient::heartbeatMutex);
        MasterServerClient::lastHeartbeat = heartbeat;
        MasterServerClient::lastHeartbeatTime.store(std::chrono::system_clock::now(), std::memory_order_release);
    }

    // Ensure the heartbeat thread is running
    if (!MasterServerClient::heartbeatThreadRunning.load(std::memory_order_acquire)) {
        MasterServerClient::StartHeartbeatThread();
    }
    return 1;
}

SQInteger DispatchServerListReq(HSQUIRRELVM v) {
    {
        ZoneScopedN("DispatchServerListReq serverListMutex");
        SRWGuard lock(&MasterServerClient::serverListMutex);
        MasterServerClient::serverList.clear();
    }

    std::thread([]() {
        MasterServerClient::GetServerList();
    }).detach();


    return 1;
}

SQInteger PollServerList(HSQUIRRELVM v) {
    ZoneScoped;

    sq_newarray(v, 0);

#if 1
    {
        ZoneScopedN("PollServerList serverListMutex");
        SRWGuard lock(&MasterServerClient::serverListMutex);

        if (MasterServerClient::serverList.empty()) {
            //sq_newtable(v);
            //sq_pushstring(v, "host_name", -1); sq_pushstring(v, "No servers found.", -1); sq_newslot(v, -3, 0);
            //sq_pushstring(v, "map_name", -1); sq_pushstring(v, "mp_lobby", -1); sq_newslot(v, -3, 0);
            //sq_pushstring(v, "game_mode", -1); sq_pushstring(v, "-", -1); sq_newslot(v, -3, 0);
            //sq_pushstring(v, "max_players", -1); sq_pushinteger(0, v, 0); sq_newslot(v, -3, 0);
            //sq_pushstring(v, "port", -1); sq_pushinteger(0, v, 0); sq_newslot(v, -3, 0);
            //sq_pushstring(v, "ip", -1); sq_pushstring(v, "0.0.0.0", -1); sq_newslot(v, -3, 0);
            //sq_pushstring(v, "players", -1); sq_newarray(v, 0); sq_newslot(v, -3, 0);
            //sq_arrayappend(v, -2);
            return 1;
        }

        for (auto& s : MasterServerClient::serverList)
#else
    {
        decltype(MasterServerClient::serverList) serverList;
        {
            ZoneScopedN("PollServerList serverListMutex");
            SRWGuard lock(&MasterServerClient::serverListMutex);
            serverList = MasterServerClient::serverList;
        }

        for (auto& s : serverList)
#endif
        {
            sq_newtable(v);
            sq_pushstring_lit(v, "host_name"); sq_pushstring_std(v, s.hostName); sq_newslot(v, -3, 0);
            sq_pushstring_lit(v, "map_name"); sq_pushstring_std(v, s.mapName); sq_newslot(v, -3, 0);
            sq_pushstring_lit(v, "game_mode"); sq_pushstring_std(v, s.gameMode); sq_newslot(v, -3, 0);
            sq_pushstring_lit(v, "max_players"); sq_pushinteger(0, v, s.maxPlayers); sq_newslot(v, -3, 0);
            sq_pushstring_lit(v, "port"); sq_pushinteger(0, v, s.port); sq_newslot(v, -3, 0);
            sq_pushstring_lit(v, "ip"); sq_pushstring_std(v, s.ip); sq_newslot(v, -3, 0);
            sq_pushstring_lit(v, "has_password"); sq_pushinteger(0, v, s.hasPassword ? 1 : 0); sq_newslot(v, -3, 0);
			sq_pushstring_lit(v, "has_auth"); sq_pushinteger(0, v, s.has_auth ? 1 : 0); sq_newslot(v, -3, 0);
            sq_pushstring_lit(v, "description"); sq_pushstring_std(v, s.description); sq_newslot(v, -3, 0);
            sq_pushstring_lit(v, "playlist"); sq_pushstring_std(v, s.playlist); sq_newslot(v, -3, 0);
            sq_pushstring_lit(v, "playlist_display_name"); sq_pushstring_std(v, s.playlist_display_name); sq_newslot(v, -3, 0);
            sq_pushstring_lit(v, "version"); sq_pushstring_std(v, s.version); sq_newslot(v, -3, 0);
            sq_pushstring_lit(v, "players");
            sq_newarray(v, 0);
            for (auto& p : s.players) {
                sq_newtable(v);
                sq_pushstring_lit(v, "name"); sq_pushstring_std(v, p.name); sq_newslot(v, -3, 0);
                sq_pushstring_lit(v, "gen"); sq_pushinteger(0, v, p.gen); sq_newslot(v, -3, 0);
                sq_pushstring_lit(v, "lvl"); sq_pushinteger(0, v, p.lvl); sq_newslot(v, -3, 0);
                sq_pushstring_lit(v, "team"); sq_pushinteger(0, v, p.team); sq_newslot(v, -3, 0);
                sq_arrayappend(v, -2);
            }
            sq_newslot(v, -3, 0);
            sq_arrayappend(v, -2);
        }
    }

    return 1;
}

// --------------------------------
// Game Shutdown Hook
// --------------------------------
typedef void(__fastcall* pCHostState__State_GameShutdown_t)(void* thisptr);
pCHostState__State_GameShutdown_t oGameShutDown;

void Hk_CHostState__State_GameShutdown(void* thisptr) {
    InitMasterServerCVars();
    if (host_map && host_map->m_Value.m_pszString &&
        strlen(host_map->m_Value.m_pszString) > 2) {
        MasterServerClient::OnServerShutdown();
        host_map->m_Value.m_StringLength = 0;
        host_map->m_Value.m_pszString[0] = '\0';
    }
    oGameShutDown(thisptr);
}

// No need for a random initializer with <random>
