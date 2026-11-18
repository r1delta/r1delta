// Server side of the custom Titan insignia exchange (shared/titan_insignia_wire.h): validates what clients
// upload and relays it to the other R1Delta clients.
//
// The relay is bounded in three ways. A player's insignia changes at most once every kChangeCooldownMs
// (the newest upload waits for the cooldown), so one player cannot make every client load insignia after
// insignia. Commands to each client leave a queue at kCommandsPerTick every kTickMs instead of all at once
// on its reliable stream. And each client's record of which insignias its game has holds at most
// kMaxDelivered ids.
#include "titan_insignia.h"
#include "titan_insignia_image.h"
#include "titan_insignia_wire.h"

#include "cvar.h"
#include "logging.h"
#include "persistentdata.h"
#include "persistentdata_slots.h"

#include <windows.h>

#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <limits>
#include <string>
#include <unordered_map>

namespace Wire = TitanInsignia::Wire;

namespace {

constexpr char kServerConVar[] = "delta_titan_insignias";
constexpr ULONGLONG kTickMs = 250;
// About 32 commands (under 15 KB) a second per client: a worst-case insignia (27 parts) takes a second.
constexpr std::size_t kCommandsPerTick = 8;
constexpr ULONGLONG kChangeCooldownMs = 5000;
// The engine clears a client's reliable stream when it reconnects the client on a map change, so
// insignias that left the queue this recently may not have arrived and are sent again after its signon.
constexpr ULONGLONG kStreamClearWindowMs = 10000;
constexpr std::size_t kMaxDelivered = 256;
// `delivered` value for the client's own uploads, which it always has.
constexpr ULONGLONG kOwnInsignia = (std::numeric_limits<ULONGLONG>::max)();

struct Outgoing {
	std::string command;
	// Set on an insignia's last data part: the id the client has once this command arrives.
	std::string completes;
};

struct ServerPlayer {
	PersistentDataState::SessionKey session;
	// The client sent the marker: it understands r1d_insignia.
	bool capable = false;
	std::uint64_t instance = 0;
	// Insignias this client's game has or is being sent: id -> tick its last part left the queue (0 while
	// queued, kOwnInsignia for its own uploads).
	std::unordered_map<std::string, ULONGLONG> delivered;
	std::deque<Outgoing> outbox;

	// What the player wears, as relayed.
	std::string id;
	std::string packed;
	ULONGLONG lastChange = 0;
	// The newest upload, waiting for the change cooldown.
	bool hasPending = false;
	std::string pendingId;
	std::string pendingPacked;
};

std::array<ServerPlayer, PersistentDataSlots::kMaximumSupportedClients> g_server;
ConVarR1* g_serverEnabled = nullptr;
int g_warnings = 32;

void ReportWarning(const char* format, ...)
{
	if (g_warnings <= 0)
		return;
	--g_warnings;
	char text[512];
	va_list args;
	va_start(args, format);
	_vsnprintf_s(text, sizeof(text), _TRUNCATE, format, args);
	va_end(args);
	Warning("%s", text);
	OutputDebugStringA(text);
}

bool ServerEnabled()
{
	return g_serverEnabled && g_serverEnabled->m_Value.m_nValue != 0;
}

bool SameSession(const PersistentDataState::SessionKey& a, const PersistentDataState::SessionKey& b)
{
	return a.netChannel == b.netChannel && a.userId == b.userId;
}

void Enqueue(ServerPlayer& to, std::string completes, const char* format, ...)
{
	char command[CCommand::COMMAND_MAX_LENGTH];
	va_list args;
	va_start(args, format);
	_vsnprintf_s(command, sizeof(command), _TRUNCATE, format, args);
	va_end(args);
	to.outbox.push_back({ command, std::move(completes) });
}

// Queues for `receiver` what the player in `source` wears, with the data when its game lacks it. While the
// relay is off every player wears nothing.
void SendState(int receiver, int source)
{
	ServerPlayer& to = g_server[receiver];
	const ServerPlayer& from = g_server[source];
	if (!to.capable)
		return;
	const std::string& id = ServerEnabled() ? from.id : std::string();
	if (!id.empty() && !to.delivered.contains(id)) {
		if (to.delivered.size() >= kMaxDelivered)
			std::erase_if(to.delivered, [](const auto& entry) { return entry.second != kOwnInsignia; });
		to.delivered[id] = 0;
		const std::size_t parts = (from.packed.size() + Wire::kCommandChunkChars - 1) / Wire::kCommandChunkChars;
		for (std::size_t part = 0; part < parts; ++part) {
			const std::string chunk = from.packed.substr(part * Wire::kCommandChunkChars, Wire::kCommandChunkChars);
			Enqueue(to, part + 1 == parts ? id : std::string(), "%s data %s %zu %zu %s",
				Wire::kCommand, id.c_str(), part, parts, chunk.c_str());
		}
	}
	Enqueue(to, {}, "%s set %d %s", Wire::kCommand, source + 1, id.empty() ? Wire::kNoInsignia : id.c_str());
}

void Broadcast(int source)
{
	for (int receiver = 0; receiver < PersistentDataSlots::kMaximumSupportedClients; ++receiver)
		SendState(receiver, source);
}

// Drops what the server knew about a slot's previous connection.
void Forget(int slot)
{
	ServerPlayer& player = g_server[slot];
	const bool wore = !player.id.empty();
	player = {};
	if (wore)
		Broadcast(slot);
}

void ApplyChange(int slot, std::string id, std::string packed, ULONGLONG now)
{
	ServerPlayer& player = g_server[slot];
	player.id = std::move(id);
	player.packed = std::move(packed);
	player.lastChange = now;
	player.hasPending = false;
	player.pendingId.clear();
	player.pendingPacked.clear();
	Broadcast(slot);
}

// A client signed on (connect or map change): what was queued or recently sent may be lost.
void RestartStream(ServerPlayer& player, ULONGLONG now)
{
	player.outbox.clear();
	std::erase_if(player.delivered, [now](const auto& entry) {
		return entry.second != kOwnInsignia && (entry.second == 0 || now - entry.second < kStreamClearWindowMs);
	});
}

void Drain(int slot, ULONGLONG now)
{
	ServerPlayer& player = g_server[slot];
	for (std::size_t sent = 0; sent < kCommandsPerTick && !player.outbox.empty(); ++sent) {
		const Outgoing& next = player.outbox.front();
		SendServerCommandToClient(slot, next.command.c_str());
		if (!next.completes.empty()) {
			const auto found = player.delivered.find(next.completes);
			if (found != player.delivered.end() && found->second != kOwnInsignia)
				found->second = now;
		}
		player.outbox.pop_front();
	}
}

}

void TitanInsignia_RegisterServer()
{
	g_serverEnabled = RegisterConVar(kServerConVar, "1", FCVAR_GAMEDLL,
		"Relay players' custom Titan insignias to the other R1Delta clients (0: nobody sees them)");
}

bool TitanInsigniaUpload::Accept(const char* name, const char* value)
{
	if (!name)
		return false;
	if (strcmp(name, Wire::kMarkerConVar) == 0) {
		markerPresent = true;
		marker = value ? value : "";
		return true;
	}
	if (strcmp(name, Wire::kUploadChunkConVar) == 0) {
		const std::size_t length = value ? strlen(value) : 0;
		if (oversized || packed.size() + length > Wire::kMaxPackedChars) {
			oversized = true;
			packed.clear();
		}
		else {
			packed.append(value, length);
		}
		return true;
	}
	return false;
}

void TitanInsignia_ServerReceive(int playerSlot, const TitanInsigniaUpload& upload, bool signon)
{
	if (playerSlot < 0 || playerSlot >= PersistentDataSlots::kMaximumSupportedClients)
		return;
	PersistentDataState::SessionKey session;
	if (!PData_ServerClientSession(playerSlot, session) || !session.IsValid())
		return;
	if (!SameSession(g_server[playerSlot].session, session)) {
		Forget(playerSlot);
		g_server[playerSlot].session = session;
	}
	ServerPlayer& player = g_server[playerSlot];
	if (!upload.markerPresent)
		return;

	Wire::Marker marker;
	if (!Wire::ParseMarker(upload.marker, marker)) {
		ReportWarning("R1Delta: player slot %d sent a malformed custom Titan insignia marker\n", playerSlot);
		return;
	}
	if (marker.instance != player.instance) {
		player.instance = marker.instance;
		player.delivered.clear();
	}
	player.capable = true;
	const ULONGLONG now = GetTickCount64();
	if (signon)
		RestartStream(player, now);

	std::string id;
	std::string packed;
	if (!marker.id.empty()) {
		TitanInsignia::TopMip mip;
		if (upload.packed.empty() && marker.id == player.id) {
			id = player.id;
			packed = player.packed;
		}
		else if (!upload.oversized && Wire::Unpack(upload.packed, mip) && TitanInsignia::ComputeId(mip) == marker.id) {
			id = marker.id;
			packed = upload.packed;
		}
		else {
			ReportWarning("R1Delta: player slot %d sent custom Titan insignia %s without valid data\n",
				playerSlot, marker.id.c_str());
		}
	}
	// The sender has its own insignia.
	if (!id.empty())
		player.delivered[id] = kOwnInsignia;

	if (id == player.id) {
		// A newer upload went back to what the player already wears.
		player.hasPending = false;
	}
	else if (player.lastChange == 0 || now - player.lastChange >= kChangeCooldownMs) {
		ApplyChange(playerSlot, std::move(id), std::move(packed), now);
	}
	else {
		player.hasPending = true;
		player.pendingId = std::move(id);
		player.pendingPacked = std::move(packed);
	}

	if (signon) {
		for (int source = 0; source < PersistentDataSlots::kMaximumSupportedClients; ++source) {
			if (!g_server[source].id.empty())
				SendState(playerSlot, source);
		}
	}
}

void TitanInsignia_ServerFrame()
{
	static ULONGLONG lastTick = 0;
	const ULONGLONG now = GetTickCount64();
	if (now - lastTick < kTickMs)
		return;
	lastTick = now;

	// Turning the relay off or on tells every client what each player wears now.
	static int lastEnabled = -1;
	const int enabled = ServerEnabled() ? 1 : 0;
	if (lastEnabled != -1 && enabled != lastEnabled) {
		for (int slot = 0; slot < PersistentDataSlots::kMaximumSupportedClients; ++slot) {
			if (!g_server[slot].id.empty())
				Broadcast(slot);
		}
	}
	lastEnabled = enabled;

	for (int slot = 0; slot < PersistentDataSlots::kMaximumSupportedClients; ++slot) {
		ServerPlayer& player = g_server[slot];
		if (!player.session.IsValid())
			continue;
		PersistentDataState::SessionKey session;
		// A slot the engine cannot read right now keeps its state.
		if (PData_ServerClientSession(slot, session) && !SameSession(session, player.session)) {
			Forget(slot);
			continue;
		}
		if (player.hasPending && now - player.lastChange >= kChangeCooldownMs)
			ApplyChange(slot, std::move(player.pendingId), std::move(player.pendingPacked), now);
	}
	for (int slot = 0; slot < PersistentDataSlots::kMaximumSupportedClients; ++slot)
		Drain(slot, now);
}
