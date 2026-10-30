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
// Sends never go straight to the netchannel: every first send and resend is
// queued per player and leaves through a paced outbox (kSendBurst, then
// kSendPerSecond), because a fresh profile means hundreds of writes at once and
// the netchannel's reliable stream is a fixed 256 KiB buffer.
//
// Everything here is plain C++ so it can be unit tested without the engine.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
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

constexpr double kInitialResendDelay = 2.0;
constexpr int kMaxTimedResends = 5;
// A full snapshot asks for every unacknowledged write again, but never sooner
// than this after the write last went out: the client has had no time to
// answer yet, and several snapshots arriving back to back (signon retries,
// partial reports) must not multiply the traffic.
constexpr double kFullSnapshotResendHold = 1.0;

// Writes leave through a paced outbox: the netchannel's reliable stream is one
// fixed 256 KiB buffer, and a joining player with a fresh profile gets hundreds
// of commands at once. At most kSendBurst commands go out in one go, then
// kSendPerSecond (about 33 KiB/s at ~56 bytes a command, below the default
// 40000 B/s client rate).
constexpr double kSendBurst = 128.0;
constexpr double kSendPerSecond = 600.0;

struct PendingWrite
{
	std::string value;
	double lastSent = 0.0;
	int timedResends = 0;
	bool resendNow = false;
	// The newest value is waiting in PlayerState::outbox and has not been sent.
	bool queued = false;
};

struct PlayerState
{
	SessionKey session;
	// Full mirror of the client's data. Only kept where the engine has no
	// per-client userinfo table of its own (R1O fake dedicated servers).
	Values values;
	std::unordered_map<std::string, PendingWrite> pending;
	// Keys whose newest value still has to be sent, oldest first. A key is in
	// here once per PendingWrite::queued; an acknowledged key may leave a stale
	// entry that TakeOutbox skips.
	std::deque<std::string> outbox;
	double sendBudget = kSendBurst;
	double budgetTime = 0.0;
};

inline void Reset(PlayerState& state)
{
	state.session = {};
	state.values.clear();
	state.pending.clear();
	state.outbox.clear();
	state.sendBudget = kSendBurst;
	state.budgetTime = 0.0;
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
// when neither a pending write nor the mirror knows it (the caller then falls
// back to the engine's userinfo table).
inline const std::string* Find(const PlayerState& state, const std::string& key)
{
	if (const auto pending = state.pending.find(key); pending != state.pending.end())
		return &pending->second.value;
	if (const auto value = state.values.find(key); value != state.values.end())
		return &value->second;
	return nullptr;
}

// Records a server-side write and queues it for sending (see TakeOutbox).
// `clientValue` is what the engine currently believes the client has (nullptr
// if unknown). Returns true when the value has to be sent to the client.
inline bool RecordServerWrite(
	PlayerState& state,
	const std::string& key,
	const std::string& value,
	const std::string* clientValue,
	double now,
	bool keepMirror)
{
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
		return false;

	PendingWrite& write = state.pending[key];
	write.value = value;
	write.lastSent = now;
	write.timedResends = 0;
	write.resendNow = false;
	if (!write.queued) {
		write.queued = true;
		state.outbox.push_back(key);
	}
	if (keepMirror)
		state.values[key] = value;
	return true;
}

// Applies a batch of persistent values reported by the client. `entries` is
// rewritten in place so that the caller hands the engine the reconciled
// values; pending keys missing from a full snapshot are appended.
inline void ApplyClientUpdate(
	PlayerState& state,
	EntryList& entries,
	bool fullSnapshot,
	bool keepMirror)
{
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

// Moves the writes that are due again (full snapshot, backoff timer) onto the
// outbox and returns how many. A write that is still waiting in the outbox is
// never queued twice, and a full snapshot only re-queues a write that went out
// at least kFullSnapshotResendHold ago; until then the request stays pending
// and is dropped if the client acknowledges the write in the meantime.
inline size_t QueueResends(PlayerState& state, double now)
{
	size_t queued = 0;
	for (auto& [key, write] : state.pending) {
		if (write.queued) {
			write.resendNow = false;
			continue;
		}
		const bool timerDue = write.timedResends < kMaxTimedResends
			&& now - write.lastSent >= ResendDelay(write.timedResends);
		const bool snapshotDue = write.resendNow && now - write.lastSent >= kFullSnapshotResendHold;
		if (!snapshotDue && !timerDue)
			continue;
		if (!snapshotDue)
			++write.timedResends;
		write.resendNow = false;
		write.queued = true;
		state.outbox.push_back(key);
		++queued;
	}
	return queued;
}

// Pops up to `maxCount` queued writes (oldest first) and marks them sent. A key
// acknowledged while it waited is skipped.
inline EntryList TakeOutbox(PlayerState& state, double now, size_t maxCount)
{
	EntryList out;
	while (out.size() < maxCount && !state.outbox.empty()) {
		const std::string key = std::move(state.outbox.front());
		state.outbox.pop_front();
		const auto pending = state.pending.find(key);
		if (pending == state.pending.end() || !pending->second.queued)
			continue;
		pending->second.queued = false;
		pending->second.lastSent = now;
		out.emplace_back(key, pending->second.value);
	}
	return out;
}

// TakeOutbox limited by the send budget: kSendBurst commands at once, then
// kSendPerSecond. `hardLimit` bounds a single call.
inline EntryList TakePaced(PlayerState& state, double now, size_t hardLimit)
{
	if (state.outbox.empty())
		return {};
	const double elapsed = state.budgetTime > 0.0 && now > state.budgetTime ? now - state.budgetTime : 0.0;
	state.budgetTime = now;
	state.sendBudget = (std::min)(kSendBurst, state.sendBudget + elapsed * kSendPerSecond);
	const size_t allowed = (std::min)(hardLimit, static_cast<size_t>(state.sendBudget));
	EntryList out = TakeOutbox(state, now, allowed);
	state.sendBudget -= static_cast<double>(out.size());
	return out;
}

}
