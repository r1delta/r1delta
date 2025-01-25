#pragma once
#include "load.h"
#include <cstdlib>
#include <crtdbg.h>	
#include <new>
#include "windows.h"
#include <iostream>
#include "cvar.h"
#include "logging.h"
#include "masterserver.h"
#include <string>
#include <vector>
#include <sstream>
#include <mutex>
#include <atomic>
#include <chrono>
#include <httplib.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;


// --- Agnostic Master Server Interface ---

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
    std::string ip; // IP as string
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

// These functions need to be implemented by the user of this interface
namespace MasterServerClient {
    static std::mutex httpMutex;
    static std::unique_ptr<httplib::Client> httpClient;
    static std::atomic<int64_t> lastHeartbeatTime{0};
    
    bool SendServerHeartbeat(const HeartbeatInfo& heartbeat) {
        auto* delta_ms_url = CCVar_FindVar(cvarinterface, "delta_ms_url");
        if (!delta_ms_url || !delta_ms_url->m_Value.m_pszString[0]) {
            Warning("MasterServerClient: delta_ms_url not set\n");
            return false;
        }

        std::lock_guard<std::mutex> lock(httpMutex);
        if (!httpClient) {
            httpClient = std::make_unique<httplib::Client>(delta_ms_url->m_Value.m_pszString);
            httpClient->set_connection_timeout(3);
        }

        json j;
        j["host_name"] = heartbeat.hostName;
        j["map_name"] = heartbeat.mapName;
        j["game_mode"] = heartbeat.gameMode;
        j["max_players"] = heartbeat.maxPlayers;
        j["port"] = heartbeat.port;
        
        j["players"] = json::array();
        for (const auto& p : heartbeat.players) {
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
        
        lastHeartbeatTime = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        return true;
    }

    std::vector<ServerInfo> GetServerList() {
        auto* delta_ms_url = CCVar_FindVar(cvarinterface, "delta_ms_url");
        if (!delta_ms_url) return {};

        httplib::Client cli(delta_ms_url->m_Value.m_pszString);
        cli.set_connection_timeout(2);
        
        auto res = cli.Get("/servers");
        if (!res || res->status != 200) return {};

        try {
            auto j = json::parse(res->body);
            std::vector<ServerInfo> servers;
            
            for (const auto& sj : j) {
                ServerInfo si;
                si.hostName = sj["host_name"];
                si.mapName = sj["map_name"];
                si.gameMode = sj["game_mode"];
                si.maxPlayers = sj["max_players"];
                si.port = sj["port"];
                si.ip = sj["ip"];
                
                for (const auto& pj : sj["players"]) {
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
        } catch (...) {
            Warning("MasterServerClient: Invalid server list response\n");
            return {};
        }
    }

    void OnServerShutdown(int port) {
        auto* delta_ms_url = CCVar_FindVar(cvarinterface, "delta_ms_url");
        if (!delta_ms_url) return;

        httplib::Client cli(delta_ms_url->m_Value.m_pszString);
        cli.set_connection_timeout(2);
        
        std::string path = "/heartbeat/" + std::to_string(port);
        auto res = cli.Delete(path.c_str());
        if (!res || res->status != 200) {
            Warning("MasterServerClient: Shutdown notification failed\n");
        }
    }
} // namespace MasterServerClient


SQInteger GetServerHeartbeat(HSQUIRRELVM v) {
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
        Warning("GetServerHeartbeat: Object at stack position 2 is not a table, but type %d\n", obj._type);
        return 1;
    }

    auto table = obj._unVal.pTable;
    if (!table) {
        Warning("GetServerHeartbeat: Table is null\n");
        return 1;
    }

    HeartbeatInfo heartbeat;
    std::vector<PlayerInfo> players;

    for (int i = 0; i < table->_numOfNodes; i++) {
        auto node = &table->_nodes[i];
        if (node->key._type != OT_STRING) {
            continue;
        }
        auto key = node->key._unVal.pString->_val;

        switch (node->val._type) {
        case OT_STRING: {
            auto str = reinterpret_cast<SQString*>(node->val._unVal.pRefCounted);
            if (strcmp_static(key, "host_name") == 0) {
                heartbeat.hostName = str->_val;
            }
            else if (strcmp_static(key, "map_name") == 0) {
                heartbeat.mapName = str->_val;
            }
            else if (strcmp_static(key, "game_mode") == 0) {
                heartbeat.gameMode = str->_val;
            }
            break;
        }
        case OT_INTEGER:
            if (strcmp_static(key, "max_players") == 0) {
                heartbeat.maxPlayers = node->val._unVal.nInteger;
            }
            else if (strcmp_static(key, "port") == 0) { // Port is also in heartbeat now
                heartbeat.port = node->val._unVal.nInteger;
            }
            break;
        case OT_ARRAY:
            if (strcmp_static(key, "players") == 0) {
                auto arr = node->val._unVal.pArray;
                if (arr) {
                    for (int j = 0; j < arr->_usedSlots; j++) {
                        if (arr->_values[j]._type != OT_TABLE) {
                            continue;
                        }
                        auto playerTable = arr->_values[j]._unVal.pTable;
                        if (playerTable) {
                            PlayerInfo playerInfo;
                            for (int k = 0; k < playerTable->_numOfNodes; k++) {
                                auto pnode = &playerTable->_nodes[k];
                                if (pnode->key._type != OT_STRING) {
                                    continue;
                                }
                                auto pkey = pnode->key._unVal.pString->_val;
                                switch (pnode->val._type) {
                                case OT_STRING: {
                                    auto str = reinterpret_cast<SQString*>(pnode->val._unVal.pRefCounted);
                                    if (strcmp_static(pkey, "name") == 0) {
                                        playerInfo.name = str->_val;
                                    }
                                    break;
                                }
                                case OT_INTEGER:
                                    if (strcmp_static(pkey, "gen") == 0) {
                                        playerInfo.gen = pnode->val._unVal.nInteger;
                                    }
                                    else if (strcmp_static(pkey, "lvl") == 0) {
                                        playerInfo.lvl = pnode->val._unVal.nInteger;
                                    }
                                    else if (strcmp_static(pkey, "team") == 0) {
                                        playerInfo.team = pnode->val._unVal.nInteger;
                                    }
                                    break;
                                }
                            }
                            players.push_back(playerInfo);
                        }
                    }
                }
            }
            break;
        }
    }
    heartbeat.players = players;

    static bool port_forward_warning_shown = false;
    if (!MasterServerClient::SendServerHeartbeat(heartbeat)) {
        if (!port_forward_warning_shown) {
            port_forward_warning_shown = true;
            if (!IsDedicatedServer()) {
                int hostport = OriginalCCVar_FindVar2(cvarinterface, "hostport")->m_Value.m_nValue;
                char cmd[64];
                snprintf(cmd, sizeof(cmd), "script_ui ShowPortForwardWarning(%d)\n", hostport);
                Cbuf_AddText(0, cmd, 0);
            }
            else {
                Warning("GetServerHeartbeat: Heartbeat send failed.\n"); // More generic message
            }
        }
    }
    else {
        port_forward_warning_shown = false; // Reset warning flag on successful heartbeat
    }

    return 1;
}

SQInteger GetServerList(HSQUIRRELVM v) {
    std::vector<ServerInfo> serverList = MasterServerClient::GetServerList();

    sq_newarray(v, 0);
    if (serverList.empty()) {
        // Create fake "No Servers Found" entry
        sq_newtable(v);

        sq_pushstring(v, "host_name", -1);
        sq_pushstring(v, "* But nobody came.", -1);
        sq_newslot(v, -3, 0);

        sq_pushstring(v, "map_name", -1);
        sq_pushstring(v, "mp_lobby", -1);
        sq_newslot(v, -3, 0);

        sq_pushstring(v, "game_mode", -1);
        sq_pushstring(v, "-", -1);
        sq_newslot(v, -3, 0);

        sq_pushstring(v, "max_players", -1);
        sq_pushinteger(0, v, 0);
        sq_newslot(v, -3, 0);

        sq_pushstring(v, "port", -1);
        sq_pushinteger(0, v, 0);
        sq_newslot(v, -3, 0);

        sq_pushstring(v, "ip", -1);
        sq_pushstring(v, "0.0.0.0", -1);
        sq_newslot(v, -3, 0);

        sq_pushstring(v, "players", -1);
        sq_newarray(v, 0);
        sq_newslot(v, -3, 0);

        sq_arrayappend(v, -2);
    }
    else {
        for (const auto& serverInfo : serverList) {
            sq_newtable(v);

            sq_pushstring(v, "host_name", -1);
            sq_pushstring(v, serverInfo.hostName.c_str(), -1);
            sq_newslot(v, -3, 0);

            sq_pushstring(v, "map_name", -1);
            sq_pushstring(v, serverInfo.mapName.c_str(), -1);
            sq_newslot(v, -3, 0);

            sq_pushstring(v, "game_mode", -1);
            sq_pushstring(v, serverInfo.gameMode.c_str(), -1);
            sq_newslot(v, -3, 0);

            sq_pushstring(v, "max_players", -1);
            sq_pushinteger(0, v, serverInfo.maxPlayers);
            sq_newslot(v, -3, 0);

            sq_pushstring(v, "port", -1);
            sq_pushinteger(0, v, serverInfo.port);
            sq_newslot(v, -3, 0);

            sq_pushstring(v, "ip", -1);
            sq_pushstring(v, serverInfo.ip.c_str(), -1);
            sq_newslot(v, -3, 0);

            sq_pushstring(v, "players", -1);
            sq_newarray(v, 0);
            for (const auto& playerInfo : serverInfo.players) {
                sq_newtable(v);

                sq_pushstring(v, "name", -1);
                sq_pushstring(v, playerInfo.name.c_str(), -1);
                sq_newslot(v, -3, 0);

                sq_pushstring(v, "gen", -1);
                sq_pushinteger(0, v, playerInfo.gen);
                sq_newslot(v, -3, 0);

                sq_pushstring(v, "lvl", -1);
                sq_pushinteger(0, v, playerInfo.lvl);
                sq_newslot(v, -3, 0);

                sq_pushstring(v, "team", -1);
                sq_pushinteger(0, v, playerInfo.team);
                sq_newslot(v, -3, 0);

                sq_arrayappend(v, -2);
            }
            sq_newslot(v, -3, 0);
            sq_arrayappend(v, -2);
        }
    }
    return 1;
}


typedef void(__fastcall* pCHostState__State_GameShutdown_t)(void* thisptr);
pCHostState__State_GameShutdown_t oGameShutDown;

void Hk_CHostState__State_GameShutdown(void* thisptr) {
    static auto hostport_cvar = CCVar_FindVar(cvarinterface, "hostport");
    static auto host_map_cvar = CCVar_FindVar(cvarinterface, "host_map");
    if (strlen(host_map_cvar->m_Value.m_pszString) > 2) {
        int port = hostport_cvar->m_Value.m_nValue;
        MasterServerClient::OnServerShutdown(port);

        host_map_cvar->m_Value.m_StringLength = 0;
        host_map_cvar->m_Value.m_pszString[0] = '\0';
    }
    oGameShutDown(thisptr);
}



