#pragma once

#include <cstdint>
#include <string>

// Dynamic weapon classes: an authored `scripts/weapon_manifest.txt` (any GAME search
// path, so loose files, addons and VPKs) names concrete weapon classes that the native
// R1 client.dll / TFO server.dll do not register themselves. The classes are registered
// natively before class / schema / weapon-data publication. See docs/dynamic-weapons.md.
//
// Modes: client.dll hooks install only where client.dll is loaded (client, listen);
// server.dll hooks install in every mode (listen, legacy dedicated, R1O fake-dedicated),
// because all three run the same TFO server.dll. An absent manifest leaves the native
// registries untouched; a malformed manifest, a missing weapon definition, an unsupported
// module build or a late registration is fatal. Membership is immutable until restart.
namespace r1delta::dynamic_weapons
{
// Called from the module load notifications (engine/core/load.cpp), right after the
// module image is mapped. MinHook must be initialized. Each installs its lifecycle hooks
// and enables them immediately. A module that is not the pinned retail build is fatal
// (the SHA-256 of both pinned files is recorded in docs/dynamic-weapons.md).
void InstallClient(std::uintptr_t clientBase);
void InstallServer(std::uintptr_t serverBase);

// Roster token: "none" when no manifest is present, otherwise "<count>:<fnv1a64 hex>"
// of the sorted class names. Identical on every machine that mounts the same manifest.
// Valid once the first side has registered; "" before that.
std::string RosterToken();

// Client: the userinfo convar delta_weapon_roster carries RosterToken() to the server
// during connect; called before the signon that serializes userinfo.
void ClientConnecting();

// Server: true when the connecting client's token (the delta_weapon_roster userinfo
// value, or null if it did not send one) matches this server's. On false, `reason`
// receives a short client-facing explanation.
bool ServerAcceptsClientRoster(const char* clientToken, char* reason, int reasonSize);

// Legacy dedicated: the engine asks for the server classes before ServerGameDLL::DLLInit
// supplies the file system factory. DLLInit calls this once oFileSystemFactory is set; it
// registers the roster if the first class request could not (no-op otherwise).
void ServerFileSystemReady();
}

