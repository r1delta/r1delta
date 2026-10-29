#pragma once
#include <cstdint>
#include <string>

// DLC map ownership (see dlc_ownership_logic.h for the groups).
//
// Each machine works out which groups it owns from the files on disk and publishes that in the userinfo
// convars dlc1_enabled..dlc5_enabled (never trusting a stale saved value: it is recomputed at startup and
// before every connect). A server enforces it twice: a joining client that lacks the group of the current map
// is rejected, and a map change kicks everybody who lacks the new map's group. A client refuses to start
// loading a map whose group it does not own, instead of reading a missing pack. The server's rotation
// (serverDLCMapGroups, read by the lobby scripts through ServerHasDLCMapGroupEnabled) only contains a group
// when the host owns it and every connected player does, or the host set dlcN_override.

// Registers dlc4/5_enabled, dlcN_override and (dedicated server only) the stock dlc1-3_enabled and
// serverDLCMapGroups. Needs the engine's CRT up, like the other RegisterConVar calls.
void Dlc_RegisterConVars();

// Recomputes this machine's ownership from disk and writes dlc1..5_enabled. Cheap (a few hundred stats).
void Dlc_RefreshLocalPresence(const char* why);
uint32_t Dlc_LocalMask();

// 0 for a map that needs no DLC, otherwise the group 1-5.
int Dlc_GroupForMap(const char* map);

// Client: false (with the text to show) when this machine does not own the group of `map`.
bool Dlc_LocalMayLoadMap(const char* map, std::string& reason);

// Server: a connecting client with this ownership mask is rejected when it lacks the current map's group.
bool Dlc_CheckConnect(uint32_t clientMask, char* reject, int rejectLength);
// Server: a map is about to start; kicks connected clients that lack its group and republishes the rotation.
void Dlc_OnServerMapStart(const char* map);
// Server: the groups that may enter rotation right now (also written to serverDLCMapGroups).
uint32_t Dlc_RotationMask();
// Dedicated server, once per host frame: republishes serverDLCMapGroups when a player joined or left or a
// dlcN_override changed since the last look (rate limited). Listen servers republish on every read instead.
void Dlc_ServerFrame();

// engine.dll (client and listen server): extends the stock group table, kick, userinfo parsing and the
// serverDLCMapGroups readers to five groups, and gates map loads. engineBase is G_engine.
void Dlc_InstallClientEngineHooks(uintptr_t engineBase);

// engine_ds.dll (legacy dedicated server): kicks clients that lack the group of a map that is about to start.
void Dlc_InstallDedicatedEngineHooks(uintptr_t engineDsBase);

// engine_r1o.dll (R1O fake-dedicated, the TFO engine): CGameClient::Connect rejects a client without the current
// map's group, changelevel/changelevel2 kick connected clients without the new map's group. engineR1oBase is G_engine_r1o.
void Dlc_InstallR1OEngineHooks(uintptr_t engineR1oBase);
