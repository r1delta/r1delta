#pragma once

// Account-hold recovery: the derived id a client offers after the server refused a join because it
// still holds the account id of a connection of ours that died in a timeout (a P2P route that dropped
// while the map was loading), and the rules that decide when that is allowed and when it ends.
//
// Both platform id generators produce a non-negative decimal string - a Discord snowflake, or the
// numeric fallback - so flipping the sign yields a value neither of them can produce, while staying
// a plain number that every consumer can still parse and compare. It is trivially reversible: the
// account is the id with the sign put back. Repeated refusals walk one step further so the same
// value is never offered twice, which is what stops the engine from retrying in a circle.
//
// The refusal text alone proves nothing: a server that counts one connection twice, or an account that
// really is online elsewhere, sends the same text on a first join. The identity is only changed when
// this process has itself lost an accepted connection to a timeout shortly before (the hold is ours),
// and the original id is put back as soon as the connection attempt that needed the derived one is over,
// so the change never outlives its episode.
//
// Split out here because it is the whole of the decision and it must not drift from what the
// recovery path does; tests/account_hold_tests.cpp exercises it.

#include <cstdlib>
#include <cstring>
#include <string>

inline bool R1DeltaDeriveReleasedAccountId(const char* id, long long* out)
{
	if (!id || !*id || !out)
		return false;

	char* end = nullptr;
	const long long value = std::strtoll(id, &end, 10);
	if (end == id || (end && *end != '\0'))
		return false;   // not one of ours; leave it alone rather than inventing an id

	*out = value > 0 ? -value : value - 1;
	return true;
}

// A derived id as written by the recovery: '-' and at least ten digits (both generators write that many).
inline bool R1DeltaIsDerivedAccountId(const char* id)
{
	if (!id || id[0] != '-')
		return false;
	size_t digits = 0;
	for (const char* p = id + 1; *p; ++p, ++digits) {
		if (*p < '0' || *p > '9')
			return false;
	}
	return digits >= 10;
}

inline bool R1DeltaContainsNoCase(const char* text, const char* needle)
{
	if (!text || !needle || !*needle)
		return false;
	const size_t length = std::strlen(needle);
	for (; *text; ++text) {
		size_t i = 0;
		while (i < length && text[i] && (text[i] | 0x20) == (needle[i] | 0x20))
			++i;
		if (i == length)
			return true;
	}
	return false;
}

// The refusal the engine sends when it already holds the connecting account.
inline bool R1DeltaReasonIsHeldAccount(const char* reason)
{
	return R1DeltaContainsNoCase(reason, "already on the server")
		|| R1DeltaContainsNoCase(reason, "AccountAlreadyOnServer");
}

// How the engine explains a connection that died without a word from the other side.
inline bool R1DeltaReasonIsTimeout(const char* reason)
{
	return R1DeltaContainsNoCase(reason, "DISCONNECT_TIMEDOUT")
		|| R1DeltaContainsNoCase(reason, "timed out");
}

class AccountHoldEpisode {
public:
	// The engine's SIGNONSTATE_CONNECTED: the first state in which the server has accepted us and holds our account.
	static constexpr int kSignonConnected = 2;
	static constexpr int kAttemptLimit = 3;
	static constexpr unsigned long long kCooldownMs = 5000;
	static constexpr unsigned long long kAttemptWindowMs = 600000;
	// How long after losing an accepted connection to a timeout a refusal can still be our own held account.
	static constexpr unsigned long long kOwnTimeoutWindowMs = 120000;
	// Connect packets of one attempt follow each other at the engine's resend cadence (cl_resend, 5 s by default).
	static constexpr unsigned long long kAttemptGapMs = 15000;
	static constexpr size_t kTargetBytes = 24;   // sizeof(netadr_t)

	enum class Verdict {
		Recover,             // write DerivedId() and retry
		NoEarlierConnection, // no accepted connection of ours timed out recently: not our hold, identity stays
		Exhausted,           // attempt limit or cooldown
		NotNumeric,          // platform_user_id is not an id this derivation understands
	};

	// A disconnect of our client was explained as a timeout while in `signonState`.
	void NoteTimeout(int signonState, unsigned long long nowMs)
	{
		if (signonState < kSignonConnected)
			return;   // never accepted, the server holds nothing of ours
		m_haveTimeout = true;
		m_timeoutMs = nowMs;
	}

	// The server refused a join with "account already on the server"; `currentId` is platform_user_id as sent.
	Verdict OnHeldRefusal(const char* currentId, unsigned long long nowMs)
	{
		long long next = 0;
		if (!R1DeltaDeriveReleasedAccountId(currentId, &next))
			return Verdict::NotNumeric;
		if (!m_haveTimeout || nowMs - m_timeoutMs > kOwnTimeoutWindowMs)
			return Verdict::NoEarlierConnection;

		if (m_lastAttemptMs && nowMs - m_lastAttemptMs > kAttemptWindowMs)
			m_attempts = 0;   // the cap is per episode, not per session
		if (m_attempts >= kAttemptLimit || (m_lastAttemptMs && nowMs - m_lastAttemptMs < kCooldownMs))
			return Verdict::Exhausted;

		if (!m_active) {
			m_original = currentId;
			m_active = true;
		}
		++m_attempts;
		m_lastAttemptMs = nowMs;
		m_retryPending = true;
		m_derived = std::to_string(next);
		return Verdict::Recover;
	}

	// A connect packet is about to be built for `target` (kTargetBytes of netadr_t). True when the episode is over: the caller
	// must put OriginalId() back before the packet is serialized. The retry the recovery issued, and the resends of the attempt
	// it started (same target, engine cadence), keep the derived id; any other attempt is a new episode.
	bool BeginConnectAttempt(const void* target, unsigned long long nowMs)
	{
		const bool sameAttempt = m_haveTarget
			&& std::memcmp(m_target, target, kTargetBytes) == 0
			&& nowMs - m_lastConnectMs <= kAttemptGapMs;
		std::memcpy(m_target, target, kTargetBytes);
		m_haveTarget = true;
		m_lastConnectMs = nowMs;

		if (!m_active)
			return false;
		if (m_retryPending) {
			m_retryPending = false;
			return false;
		}
		if (sameAttempt)
			return false;
		m_active = false;
		return true;
	}

	bool Active() const { return m_active; }
	const std::string& OriginalId() const { return m_original; }
	const std::string& DerivedId() const { return m_derived; }
	int Attempts() const { return m_attempts; }
	unsigned long long LastAttemptMs() const { return m_lastAttemptMs; }

private:
	bool m_haveTimeout = false;
	unsigned long long m_timeoutMs = 0;
	int m_attempts = 0;
	unsigned long long m_lastAttemptMs = 0;
	bool m_active = false;
	bool m_retryPending = false;
	std::string m_original;
	std::string m_derived;
	bool m_haveTarget = false;
	unsigned char m_target[kTargetBytes] = {};
	unsigned long long m_lastConnectMs = 0;
};
