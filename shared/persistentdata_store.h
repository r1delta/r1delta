#pragma once

// Client-side persistent data store: the single source of truth for a player's
// progression on this machine. It lives in memory, and is only ever *read* from
// disk once per process. Everything in here is plain C++ so it can be unit
// tested without the engine.

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace PersistentDataStore {

// key (without the "__ " prefix) -> value
using Entries = std::map<std::string, std::string, std::less<>>;

constexpr size_t MaxFileSize = 16 * 1024 * 1024;
constexpr size_t MaxKeySize = 254;
constexpr size_t MaxValueSize = 254;

// Keys and values are later spliced into console commands, so only accept the
// characters the rest of the pdata pipeline accepts.
bool IsSafeToken(std::string_view text, size_t maxSize);

enum class ParseStatus
{
	Valid,     // well-formed file with a matching integrity trailer
	Damaged,   // some entries were recovered, but the file is not trustworthy
	Invalid,   // nothing usable
};

// Serializes the store. The output ends with an integrity trailer that records
// the entry count and a checksum of every entry line, so a torn or truncated
// write is detected on load instead of silently dropping progression.
std::string Serialize(const Entries& entries);

// Parses a file produced by Serialize. Always recovers as many entries as it
// can into `entries`; the status tells the caller whether it can be trusted.
ParseStatus Parse(std::string_view contents, Entries& entries);

// Pulls legacy "__ key \"value\"" lines out of an old profile.cfg. Malformed
// lines are skipped instead of discarding the whole profile.
size_t ExtractLegacyProfileEntries(std::string_view profile, Entries& entries);

// Archived convar that only builds with this store register. Its presence in
// profile.cfg means the "__" lines there are this build's own write-only
// mirror; its absence means an older build wrote the file.
constexpr char ProfileOwnerConVar[] = "delta_pdata_store";
bool ProfileHasOwnerMarker(std::string_view profile);

// Copies legacy entries over the store and returns how many were added or
// changed (0 means the legacy profile is just the mirror of the store).
size_t MergeLegacyEntries(Entries& store, const Entries& legacy);

// Returns profile.cfg with every persistent-data line removed. Profile
// settings are still executed by the engine, but persistent data is never
// re-applied from profile.cfg once the store owns it.
std::string StripPersistentLines(std::string_view profile);

// Formats one entry as it appears in the store file / legacy profile.
std::string FormatLine(std::string_view key, std::string_view value);

}
