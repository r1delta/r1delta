#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>

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

struct PlayerState
{
	SessionKey session;
	Values values;
};

inline bool BeginSession(PlayerState& state, SessionKey incoming)
{
	if (!incoming.IsValid())
		return false;

	const bool netChannelChanged = state.session.netChannel != incoming.netChannel;
	const bool knownUserChanged = state.session.userId >= 0
		&& incoming.userId >= 0
		&& state.session.userId != incoming.userId;
	if (netChannelChanged || knownUserChanged) {
		state.values.clear();
		state.session = incoming;
		return true;
	}

	// The engine can assign the userid after the first client payload. Promote an
	// unknown id without treating the same netchannel as a second connection.
	if (state.session.userId < 0 && incoming.userId >= 0)
		state.session.userId = incoming.userId;
	return true;
}

inline bool Replace(PlayerState& state, SessionKey session, Values replacement)
{
	if (!BeginSession(state, session))
		return false;
	state.values = std::move(replacement);
	return true;
}

inline bool Merge(PlayerState& state, SessionKey session, Values updates)
{
	if (!BeginSession(state, session))
		return false;
	for (auto& entry : updates)
		state.values.insert_or_assign(std::move(entry.first), std::move(entry.second));
	return true;
}

}
