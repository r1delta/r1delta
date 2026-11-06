#pragma once

// Server-side view of one connected player's persistent data.
//
// The client owns the data and stores it; the server only writes to it by
// sending "__ key value" string commands. Those commands travel on the
// reliable stream, which the engine throws away whenever it clears a
// netchannel (every client is reconnected on changelevel). The client then
// resends a full snapshot of what it has, which is older than what the server
// just wrote. Without extra bookkeeping the server adopts that stale snapshot
// and the client never receives the write: progression "rolls back" on map
// change.
//
// To make server writes durable for the lifetime of the connection, every
// write stays *pending* until the client echoes the same value back. While a
// write is pending:
//   - reads see the pending value,
//   - any client report of a different value is rewritten to the pending
//     value before the engine stores it,
//   - the write is resent after a full snapshot (i.e. after a reconnect) and
//     on a backoff timer in case it was dropped for any other reason.
//
// A server only overwrites values it has seen. Until the client's data has
// arrived on this connection, and for keys whose client value this server's
// schema rejected (a client or server of another version, or a mod), every
// read is a default: a write computed from it (new-player defaults, loadout
// "repairs") would replace real progression. Such writes stay local to the
// server: reads see them, the client never receives them.
//
// Everything here is plain C++ so it can be unit tested without the engine.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace PersistentDataState {

struct SessionKey
{
	uintptr_t netChannel = 0;
	int userId = -1;

	bool IsValid() const
	{
		return netChannel != 0;
	}
};

using Values = std::unordered_map<std::string, std::string>;
using EntryList = std::vector<std::pair<std::string, std::string>>;
using KeyList = std::vector<std::string>;

constexpr double kInitialResendDelay = 2.0;
constexpr int kMaxTimedResends = 5;

struct PendingWrite
{
	std::string value;
	double lastSent = 0.0;
	int timedResends = 0;
	bool resendNow = false;
};

struct PlayerState
{
	SessionKey session;
	// The client's data set has arrived on this connection.
	bool dataReceived = false;
	// Full mirror of the client's data. Only kept where the engine has no
	// per-client userinfo table of its own (R1O fake dedicated servers).
	Values values;
	// Server writes the client must not receive (see above), served to reads.
	Values local;
	// Keys the client holds whose value this server's schema rejected.
	std::unordered_set<std::string> rejected;
	std::unordered_map<std::string, PendingWrite> pending;
};

// How a client message relates to the player's data set.
enum class ReportKind
{
	Delta,          // individual changes
	Signon,         // connect-time userinfo without the full-snapshot marker:
	                // clients only omit the marker when they hold no data
	FullSnapshot,   // the client's complete data set
};

// Classifies a client NET_SetConVar. The signon message is the one that
// carries the sound-script upload (every compatible client sends it).
inline ReportKind ClassifyReport(bool fullSnapshotMarker, bool signon)
{
	if (fullSnapshotMarker)
		return ReportKind::FullSnapshot;
	return signon ? ReportKind::Signon : ReportKind::Delta;
}

enum class WriteOutcome
{
	Unchanged,   // the client already has the value
	Send,        // deliver to the client, pending until it is echoed
	KeepLocal,   // the client's value is unknown here: serve reads, never send
};

inline void Reset(PlayerState& state)
{
	state.session = {};
	state.dataReceived = false;
	state.values.clear();
	state.local.clear();
	state.rejected.clear();
	state.pending.clear();
}

// Binds the state to a connection. A different connection in the same slot
// (new netchannel, or a different known userid) wipes everything, so one
// player's writes can never leak into the next player's profile.
inline bool BeginSession(PlayerState& state, SessionKey incoming)
{
	if (!incoming.IsValid())
		return false;

	const bool netChannelChanged = state.session.netChannel != incoming.netChannel;
	const bool knownUserChanged = state.session.userId >= 0
		&& incoming.userId >= 0
		&& state.session.userId != incoming.userId;
	if (netChannelChanged || knownUserChanged) {
		Reset(state);
		state.session = incoming;
		return true;
	}

	// The engine can assign the userid after the first client payload. Promote
	// an unknown id without treating the same netchannel as a new connection.
	if (state.session.userId < 0 && incoming.userId >= 0)
		state.session.userId = incoming.userId;
	return true;
}

// Returns the value the server should consider current for `key`, or nullptr
// when neither a pending or local write nor the mirror knows it (the caller
// then falls back to the engine's userinfo table).
inline const std::string* Find(const PlayerState& state, const std::string& key)
{
	if (const auto pending = state.pending.find(key); pending != state.pending.end())
		return &pending->second.value;
	if (const auto local = state.local.find(key); local != state.local.end())
		return &local->second;
	if (const auto value = state.values.find(key); value != state.values.end())
		return &value->second;
	return nullptr;
}

// Records a server-side write. `clientValue` is what the engine currently
// believes the client has (nullptr if unknown).
inline WriteOutcome RecordServerWrite(
	PlayerState& state,
	const std::string& key,
	const std::string& value,
	const std::string* clientValue,
	double now,
	bool keepMirror)
{
	if (!state.dataReceived || state.rejected.count(key)) {
		state.local.insert_or_assign(key, value);
		return WriteOutcome::KeepLocal;
	}

	const std::string* current = nullptr;
	if (const auto pending = state.pending.find(key); pending != state.pending.end())
		current = &pending->second.value;
	else if (keepMirror) {
		if (const auto mirrored = state.values.find(key); mirrored != state.values.end())
			current = &mirrored->second;
	}
	if (!current)
		current = clientValue;
	if (current && *current == value)
		return WriteOutcome::Unchanged;

	PendingWrite& write = state.pending[key];
	write.value = value;
	write.lastSent = now;
	write.timedResends = 0;
	write.resendNow = false;
	if (keepMirror)
		state.values[key] = value;
	return WriteOutcome::Send;
}

// Applies a batch of persistent values reported by the client. `entries` is
// rewritten in place so that the caller hands the engine the reconciled
// values; pending keys missing from a full snapshot are appended. `rejected`
// lists the keys the client reported with values this server's schema
// rejected (they are not in `entries`).
inline void ApplyClientUpdate(
	PlayerState& state,
	EntryList& entries,
	const KeyList& rejected,
	ReportKind kind,
	bool keepMirror)
{
	const bool fullSnapshot = kind == ReportKind::FullSnapshot;
	if (kind != ReportKind::Delta)
		state.dataReceived = true;
	if (fullSnapshot)
		state.rejected.clear();
	// The client holds a value here that the server cannot read: anything the
	// server wrote or mirrored for the key is stale.
	for (const std::string& key : rejected) {
		state.rejected.insert(key);
		state.pending.erase(key);
		if (keepMirror)
			state.values.erase(key);
	}
	for (const auto& entry : entries)
		state.rejected.erase(entry.first);
	// Once the data is here, local writes only stand in for rejected keys.
	if (state.dataReceived) {
		for (auto it = state.local.begin(); it != state.local.end();) {
			if (state.rejected.count(it->first))
				++it;
			else
				it = state.local.erase(it);
		}
	}

	std::unordered_map<std::string, bool> reported;
	if (fullSnapshot)
		reported.reserve(entries.size());

	for (auto& [key, value] : entries) {
		const auto pending = state.pending.find(key);
		if (pending == state.pending.end()) {
			if (fullSnapshot)
				reported.insert_or_assign(key, false);
			continue;
		}
		// A batch may contain the same key more than once. The engine keeps
		// the last value, so only that report can acknowledge the write.
		// Keep the pending write alive until every entry has been reconciled.
		const bool acknowledged = pending->second.value == value;
		reported.insert_or_assign(key, acknowledged);
		value = pending->second.value;
		if (fullSnapshot && !acknowledged) {
			pending->second.resendNow = true;
			pending->second.timedResends = 0;
		}
	}

	for (const auto& [key, acknowledged] : reported) {
		if (acknowledged)
			state.pending.erase(key);
	}

	if (fullSnapshot) {
		for (auto& [key, write] : state.pending) {
			if (reported.count(key))
				continue;
			entries.emplace_back(key, write.value);
			write.resendNow = true;
			write.timedResends = 0;
		}
	}

	if (!keepMirror)
		return;
	if (fullSnapshot)
		state.values.clear();
	for (const auto& [key, value] : entries)
		state.values.insert_or_assign(key, value);
}

inline double ResendDelay(int timedResends)
{
	double delay = kInitialResendDelay;
	for (int i = 0; i < timedResends; ++i)
		delay *= 2.0;
	return delay;
}

// Returns the pending writes that should be (re)sent now and marks them sent.
inline EntryList CollectResends(PlayerState& state, double now)
{
	EntryList due;
	for (auto& [key, write] : state.pending) {
		const bool timerDue = write.timedResends < kMaxTimedResends
			&& now - write.lastSent >= ResendDelay(write.timedResends);
		if (!write.resendNow && !timerDue)
			continue;
		if (!write.resendNow)
			++write.timedResends;
		write.resendNow = false;
		write.lastSent = now;
		due.emplace_back(key, write.value);
	}
	return due;
}

}
