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

// Weapons CORE used to ship under the engine's pre-registered mp_weapon_megaN
// slots now carry their real names. Profiles written before the rename hold the
// old names in keys (newLoadoutItems[<name>], newMods[<name>_<mod>]) and in
// values (loadout items). Only slots that can appear in persisted data are
// listed: mp_weapon_mega5 (throwing knife) has no persistent entry and is not
// in the schema, mega6 and above were never renamed.
struct RenamedName
{
	std::string_view from;
	std::string_view to;
};
inline constexpr RenamedName RenamedNames[] = {
	{ "mp_weapon_mega1", "mp_weapon_sr_valkyrie" },
	{ "mp_weapon_mega2", "mp_weapon_twinb_shotgun" },
	{ "mp_weapon_mega3", "mp_titanweapon_minigun" },
	{ "mp_weapon_mega4", "mp_titanweapon_charge_cannon" },
};

// Replaces every renamed name in `text`, matched as a whole token: not glued
// to a preceding letter, digit or underscore, and not followed by a letter or
// digit. So mp_weapon_mega1 matches inside mp_weapon_mega1_aog and
// newMods[mp_weapon_mega1_silencer], but never inside mp_weapon_mega10 or
// mp_weapon_mega1x. Matching ignores ASCII case (the schema's enum lookup does
// too); the replacement is always the canonical lower case name. Returns false
// and leaves `renamed` empty when nothing was replaced. Idempotent: no new name
// contains an old name.
bool RenameTokens(std::string_view text, std::string& renamed);

// Applies RenameTokens to every key and value of `entries`. A key that already
// exists under the new name is authoritative: when both the old and the new
// variant of a key are present the old one is dropped. If several old keys
// would collapse onto one new key (different letter case only), the first in
// key order wins. Renames that would push a key or value past the size limits
// are skipped. Returns how many entries were changed or dropped.
size_t MigrateRenamedNames(Entries& entries);

enum class LoadoutVerdict
{
	Neutral,   // no weapon rule applies; validate the value against the schema
	Accept,    // valid without further schema checks
	Reject,    // never valid for this key
};

// Weapon rules for loadout values, applied to names already migrated with
// MigrateRenamedNames. Titan loadouts take titan weapons (the former mega3 and
// mega4 slots, mp_titanweapon_minigun and mp_titanweapon_charge_cannon, and
// the remaining mp_weapon_mega* slots) but no pilot weapon. Pilot loadouts take
// no mp_titanweapon_* value.
LoadoutVerdict JudgeLoadoutValue(std::string_view key, std::string_view value);

// Formats one entry as it appears in the store file / legacy profile.
std::string FormatLine(std::string_view key, std::string_view value);

}
