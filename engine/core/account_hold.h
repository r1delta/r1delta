#pragma once

// Account-hold recovery: the derived id a client offers after the server refused a join because it
// still holds the account id of a connection that died in a P2P timeout.
//
// Both platform id generators produce a non-negative decimal string - a Discord snowflake, or the
// numeric fallback - so flipping the sign yields a value neither of them can produce, while staying
// a plain number that every consumer can still parse and compare. It is trivially reversible: the
// account is the id with the sign put back. Repeated refusals walk one step further so the same
// value is never offered twice, which is what stops the engine from retrying in a circle.
//
// Split out here because it is the whole of the decision and it must not drift from what the
// recovery path does.

#include <cstdlib>

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
