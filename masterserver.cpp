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
#include <httplib.h>
#include <nlohmann/json.hpp>
#include "load.h"
#include "cvar.h"
#include "logging.h"
#include "masterserver.h"

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
    std::string ip;
    std::vector<PlayerInfo> players;
};

struct HeartbeatInfo {
    std::string hostName;
    std::string mapName;
    std::string gameMode;
    int maxPlayers;
    int port;
    std::vector<PlayerInfo> players;
};

// --------------------------------
// CVar Declarations
// --------------------------------
static ConVarR1O* delta_ms_url = nullptr;
static ConVarR1O* hostport = nullptr;
static ConVarR1O* host_map = nullptr;

// --------------------------------
// CVar Initialization
// --------------------------------
void InitMasterServerCVars() {
    static bool initialized = false;
    if (!initialized) {
        delta_ms_url = CCVar_FindVar(cvarinterface, "delta_ms_url");
        hostport = CCVar_FindVar(cvarinterface, "hostport");
        host_map = CCVar_FindVar(cvarinterface, "host_map");
        initialized = true;
    }
}

namespace MasterServerClient {
    static std::mutex httpMutex;
    static std::unique_ptr<httplib::Client> httpClient;

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
    bool SendServerHeartbeat(const HeartbeatInfo& heartbeat) {
        InitMasterServerCVars();
        if (!delta_ms_url || !delta_ms_url->m_Value.m_pszString[0]) {
            Warning("MasterServerClient: delta_ms_url not set\n");
            return false;
        }

        std::lock_guard<std::mutex> lock(httpMutex);
        EnsureHttpClient(delta_ms_url->m_Value.m_pszString);

        json j;
        j["host_name"] = heartbeat.hostName;
        j["map_name"] = heartbeat.mapName;
        j["game_mode"] = heartbeat.gameMode;
        j["max_players"] = heartbeat.maxPlayers;
        j["port"] = heartbeat.port;

        j["players"] = json::array();
        for (auto& p : heartbeat.players) {
            json pj;
            pj["name"] = p.name;
            pj["gen"] = p.gen;
            pj["lvl"] = p.lvl;
            pj["team"] = p.team;
            j["players"].push_back(pj);
        }

        auto res = httpClient->Post("/heartbeat", j.dump(), "application/json");
        if (!res || res->status != 200) {
            Warning("MasterServerClient: Heartbeat failed - %s\n",
                res ? res->body.c_str() : "Connection failed");
            return false;
        }

        return true;
    }

    // --------------------------------
    // Server List
    // --------------------------------
    std::vector<ServerInfo> GetServerList() {
        InitMasterServerCVars();
        if (!delta_ms_url) return {};

        httplib::Client cli(delta_ms_url->m_Value.m_pszString);
        cli.set_connection_timeout(2);
        cli.set_address_family(AF_INET);

        auto res = cli.Get("/servers");
        if (!res || res->status != 200) return {};

        try {
            auto j = json::parse(res->body);
            std::vector<ServerInfo> servers;

            for (auto& sj : j) {
                ServerInfo si;
                si.hostName = sj["host_name"];
                si.mapName = sj["map_name"];
                si.gameMode = sj["game_mode"];
                si.maxPlayers = sj["max_players"];
                si.port = sj["port"];
                si.ip = sj["ip"];

                for (auto& pj : sj["players"]) {
                    PlayerInfo pi;
                    pi.name = pj["name"];
                    pi.gen = pj["gen"];
                    pi.lvl = pj["lvl"];
                    pi.team = pj["team"];
                    si.players.push_back(pi);
                }
                servers.push_back(si);
            }
            return servers;
        }
        catch (...) {
            Warning("MasterServerClient: Invalid server list response\n");
            return {};
        }
    }

    // --------------------------------
    // On Shutdown
    // --------------------------------
    void OnServerShutdown(int port) {
        InitMasterServerCVars();
        if (!delta_ms_url) return;

        httplib::Client cli(delta_ms_url->m_Value.m_pszString);
        cli.set_connection_timeout(2);
        cli.set_address_family(AF_INET);

        std::string path = "/heartbeat/" + std::to_string(port);
        auto res = cli.Delete(path.c_str());
        if (!res || res->status != 200)
            Warning("MasterServerClient: Shutdown notification failed\n");
    }
} // namespace MasterServerClient

// --------------------------------
// Squirrel Interface
// --------------------------------
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

    HeartbeatInfo heartbeat;
    heartbeat.port = hostport->m_Value.m_nValue;
    std::vector<PlayerInfo> players;

    auto table = obj._unVal.pTable;
    if (!table) {
        Warning("GetServerHeartbeat: Table is null\n");
        return 1;
    }

    for (int i = 0; i < table->_numOfNodes; i++) {
        auto& node = table->_nodes[i];
        if (node.key._type != OT_STRING) continue;
        auto key = node.key._unVal.pString->_val;

        switch (node.val._type) {
        case OT_STRING: {
            auto s = reinterpret_cast<SQString*>(node.val._unVal.pRefCounted);
            if (!strcmp_static(key, "host_name")) heartbeat.hostName = s->_val;
            if (!strcmp_static(key, "map_name")) heartbeat.mapName = s->_val;
            if (!strcmp_static(key, "game_mode")) heartbeat.gameMode = s->_val;
            break;
        }
        case OT_INTEGER:
            if (!strcmp_static(key, "max_players")) heartbeat.maxPlayers = node.val._unVal.nInteger;
            break;
        case OT_ARRAY:
            if (!strcmp_static(key, "players")) {
                auto arr = node.val._unVal.pArray;
                if (arr) {
                    for (int j = 0; j < arr->_usedSlots; j++) {
                        if (arr->_values[j]._type != OT_TABLE) continue;
                        auto ptable = arr->_values[j]._unVal.pTable;
                        if (!ptable) continue;
                        PlayerInfo pi;
                        for (int k = 0; k < ptable->_numOfNodes; k++) {
                            auto& pnode = ptable->_nodes[k];
                            if (pnode.key._type != OT_STRING) continue;
                            auto pkey = pnode.key._unVal.pString->_val;

                            if (pnode.val._type == OT_STRING) {
                                auto s2 = reinterpret_cast<SQString*>(pnode.val._unVal.pRefCounted);
                                if (!strcmp_static(pkey, "name")) pi.name = s2->_val;
                            }
                            else if (pnode.val._type == OT_INTEGER) {
                                if (!strcmp_static(pkey, "gen")) pi.gen = pnode.val._unVal.nInteger;
                                if (!strcmp_static(pkey, "lvl")) pi.lvl = pnode.val._unVal.nInteger;
                                if (!strcmp_static(pkey, "team")) pi.team = pnode.val._unVal.nInteger;
                            }
                        }
                        players.push_back(pi);
                    }
                }
            }
            break;
        }
    }

    heartbeat.players = players;

    static bool portForwardWarningShown = false;
    if (!MasterServerClient::SendServerHeartbeat(heartbeat)) {
        if (!portForwardWarningShown) {
            portForwardWarningShown = true;
            if (!IsDedicatedServer()) {
                int val = hostport ? hostport->m_Value.m_nValue : 27015;
                char cmd[64];
                snprintf(cmd, sizeof(cmd), "script_ui ShowPortForwardWarning(%d)\n", val);
                Cbuf_AddText(0, cmd, 0);
            }
            else {
                Warning("GetServerHeartbeat: Heartbeat send failed.\n");
            }
        }
    }
    else {
        portForwardWarningShown = false;
    }

    return 1;
}

SQInteger GetServerList(HSQUIRRELVM v) {
    auto servers = MasterServerClient::GetServerList();
    sq_newarray(v, 0);

    if (servers.empty()) {
        sq_newtable(v);
        sq_pushstring(v, "host_name", -1); sq_pushstring(v, "* But nobody came.", -1); sq_newslot(v, -3, 0);
        sq_pushstring(v, "map_name", -1); sq_pushstring(v, "mp_lobby", -1); sq_newslot(v, -3, 0);
        sq_pushstring(v, "game_mode", -1); sq_pushstring(v, "-", -1); sq_newslot(v, -3, 0);
        sq_pushstring(v, "max_players", -1); sq_pushinteger(0, v, 0); sq_newslot(v, -3, 0);
        sq_pushstring(v, "port", -1); sq_pushinteger(0, v, 0); sq_newslot(v, -3, 0);
        sq_pushstring(v, "ip", -1); sq_pushstring(v, "0.0.0.0", -1); sq_newslot(v, -3, 0);
        sq_pushstring(v, "players", -1); sq_newarray(v, 0); sq_newslot(v, -3, 0);
        sq_arrayappend(v, -2);
        return 1;
    }

    for (auto& s : servers) {
        sq_newtable(v);
        sq_pushstring(v, "host_name", -1); sq_pushstring(v, s.hostName.c_str(), -1); sq_newslot(v, -3, 0);
        sq_pushstring(v, "map_name", -1); sq_pushstring(v, s.mapName.c_str(), -1); sq_newslot(v, -3, 0);
        sq_pushstring(v, "game_mode", -1); sq_pushstring(v, s.gameMode.c_str(), -1); sq_newslot(v, -3, 0);
        sq_pushstring(v, "max_players", -1); sq_pushinteger(0, v, s.maxPlayers); sq_newslot(v, -3, 0);
        sq_pushstring(v, "port", -1); sq_pushinteger(0, v, s.port); sq_newslot(v, -3, 0);
        sq_pushstring(v, "ip", -1); sq_pushstring(v, s.ip.c_str(), -1); sq_newslot(v, -3, 0);

        sq_pushstring(v, "players", -1);
        sq_newarray(v, 0);
        for (auto& p : s.players) {
            sq_newtable(v);
            sq_pushstring(v, "name", -1); sq_pushstring(v, p.name.c_str(), -1); sq_newslot(v, -3, 0);
            sq_pushstring(v, "gen", -1); sq_pushinteger(0, v, p.gen); sq_newslot(v, -3, 0);
            sq_pushstring(v, "lvl", -1); sq_pushinteger(0, v, p.lvl); sq_newslot(v, -3, 0);
            sq_pushstring(v, "team", -1); sq_pushinteger(0, v, p.team); sq_newslot(v, -3, 0);
            sq_arrayappend(v, -2);
        }
        sq_newslot(v, -3, 0);
        sq_arrayappend(v, -2);
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
    if (strlen(host_map->m_Value.m_pszString) > 2) {
        MasterServerClient::OnServerShutdown(hostport->m_Value.m_nValue);
        host_map->m_Value.m_StringLength = 0;
        host_map->m_Value.m_pszString[0] = '\0';
    }
    oGameShutDown(thisptr);
}
