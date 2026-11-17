#pragma once
#include "squirrel.h"

#include <cstdint>
#include <string>

struct NET_SetConVar;

// Custom Titan insignias: a player picks an image from <game>/r1/insignias, and every R1Delta client in
// the match draws it on that player's Titans in place of the stock insignia the loadout names.
// shared/titan_insignia_wire.h describes the exchange; client/rendering/titan_insignia_render.h draws.

// --- Client ------------------------------------------------------------------
// Registers the client convars and the server-executable `r1d_insignia` command.
void TitanInsignia_RegisterClient();
// Before the connect packet: encodes the selected image and publishes the userinfo marker.
void TitanInsignia_ClientConnecting();
// NET_SetConVar writer: appends the packed insignia when the outgoing message carries the marker.
// `signon` is true for the client's signon message, which restarts the server's assignments.
void TitanInsignia_ClientWriteUpload(NET_SetConVar* message, bool signon);
// Main thread, once per frame: applies a changed selection and services the renderer.
void TitanInsignia_ClientFrame();

SQInteger Script_TitanInsigniaGetFiles(HSQUIRRELVM v);
SQInteger Script_TitanInsigniaGetSelected(HSQUIRRELVM v);
SQInteger Script_TitanInsigniaSelect(HSQUIRRELVM v);
SQInteger Script_TitanInsigniaOpenFolder(HSQUIRRELVM v);

// --- Server ------------------------------------------------------------------
// Registers `delta_titan_insignias`.
void TitanInsignia_RegisterServer();

// One client's insignia upload, collected while reading its NET_SetConVar.
struct TitanInsigniaUpload {
	bool markerPresent = false;
	std::string marker;
	std::string packed;
	bool oversized = false;

	// True when the convar belongs to the insignia exchange; such convars are consumed here and never
	// applied as userinfo.
	bool Accept(const char* name, const char* value);
};

// The NET_SetConVar reader received `upload` from `playerSlot`; `signon` marks the client's signon message.
void TitanInsignia_ServerReceive(int playerSlot, const TitanInsigniaUpload& upload, bool signon);
// About four times a second: forgets players who left and tells the other clients.
void TitanInsignia_ServerFrame();
