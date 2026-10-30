#include "shared/persistentdata_codec.h"
#include "shared/persistentdata_slots.h"
#include "shared/persistentdata_state.h"
#include "shared/persistentdata_store.h"

#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void Check(bool condition, const char* name)
{
	if (!condition) {
		std::cerr << "FAIL: " << name << '\n';
		++failures;
	}
}

// Test helper: queue what is due again and send it right away (no pacing).
PersistentDataState::EntryList CollectResends(PersistentDataState::PlayerState& state, double now)
{
	PersistentDataState::QueueResends(state, now);
	return PersistentDataState::TakeOutbox(state, now, SIZE_MAX);
}

std::vector<PersistentDataCodec::Entry> MakeEntries(size_t count)
{
	std::vector<PersistentDataCodec::Entry> entries;
	entries.reserve(count);
	for (size_t i = 0; i < count; ++i) {
		entries.push_back({
			"pilotLoadouts[" + std::to_string(i) + "].weaponMod",
			(i % 3 == 0 ? "mp_weapon_rspn101" : "value_" + std::to_string(i))
		});
	}
	return entries;
}

void TestPackedRoundTrip()
{
	const std::vector<PersistentDataCodec::Entry> source = MakeEntries(4539);
	std::string encoded;
	Check(PersistentDataCodec::Encode(source, encoded), "encode 4539 entries");
	Check(!encoded.empty(), "encoded payload is non-empty");
	Check(encoded.size() <= PersistentDataCodec::MaxEncodedSize, "encoded payload is bounded");

	std::vector<PersistentDataCodec::Entry> decoded;
	Check(PersistentDataCodec::Decode(encoded, decoded), "decode 4539 entries");
	Check(decoded.size() == source.size(), "decoded entry count");
	if (decoded.size() == source.size()) {
		for (size_t i = 0; i < source.size(); ++i) {
			if (decoded[i].key != source[i].key || decoded[i].value != source[i].value) {
				Check(false, "decoded entry identity");
				break;
			}
		}
	}

	Check(!PersistentDataCodec::Decode({}, decoded), "reject empty payload");
	Check(!PersistentDataCodec::Decode("AAAA", decoded), "reject non-zstd payload");
	Check(!PersistentDataCodec::Decode(encoded.substr(0, encoded.size() - 4), decoded), "reject truncated payload");
	std::string invalidCharacter = encoded;
	invalidCharacter[invalidCharacter.size() / 2] = '!';
	Check(!PersistentDataCodec::Decode(invalidCharacter, decoded), "reject invalid base64 character");
	Check(!PersistentDataCodec::Decode(encoded + "AAAA", decoded), "reject concatenated payload data");
}

void TestPackedLimits()
{
	std::string encoded;
	std::vector<PersistentDataCodec::Entry> entries = {{"", "value"}};
	Check(!PersistentDataCodec::Encode(entries, encoded), "reject empty key");
	entries = {{std::string(PersistentDataCodec::MaxKeySize + 1, 'k'), "value"}};
	Check(!PersistentDataCodec::Encode(entries, encoded), "reject oversized key");
	entries = {{"key", std::string(PersistentDataCodec::MaxValueSize + 1, 'v')}};
	Check(!PersistentDataCodec::Encode(entries, encoded), "reject oversized value");
	entries = {{std::string(PersistentDataCodec::MaxKeySize, 'k'), std::string(PersistentDataCodec::MaxValueSize, 'v')}};
	Check(PersistentDataCodec::Encode(entries, encoded), "accept maximum key and value");
}

void TestStoreRoundTrip()
{
	using namespace PersistentDataStore;
	Entries entries;
	for (int i = 0; i < 4539; ++i)
		entries["pilotLoadouts[" + std::to_string(i % 40) + "].slot" + std::to_string(i)] = "value " + std::to_string(i);
	entries["xp"] = "123456";
	entries["gen"] = "9";

	const std::string serialized = Serialize(entries);
	Entries parsed;
	Check(Parse(serialized, parsed) == ParseStatus::Valid, "serialized store parses as valid");
	Check(parsed == entries, "store round trip preserves every entry");

	Entries empty;
	Check(Parse(Serialize(empty), parsed) == ParseStatus::Valid, "empty store is valid");
	Check(parsed.empty(), "empty store round trips");

	Check(Parse("", parsed) == ParseStatus::Invalid, "reject empty file");
	Check(Parse("garbage\n", parsed) == ParseStatus::Invalid, "reject garbage file");
}

void TestStoreDetectsDamage()
{
	using namespace PersistentDataStore;
	Entries entries{ { "xp", "500" }, { "gen", "3" }, { "ranked.gems", "12" } };
	const std::string serialized = Serialize(entries);
	Entries parsed;

	// Torn write: trailer missing.
	const std::string truncated = serialized.substr(0, serialized.rfind("// end"));
	Check(Parse(truncated, parsed) == ParseStatus::Damaged, "missing trailer is damaged");
	Check(parsed == entries, "missing trailer still salvages all complete entries");

	// Cut in the middle of a line.
	const std::string cut = serialized.substr(0, serialized.find("xp") + 4);
	Check(Parse(cut, parsed) == ParseStatus::Damaged, "mid-line truncation is damaged");
	Check(parsed.count("gen") == 1 && parsed.count("xp") == 0, "mid-line truncation salvages earlier lines only");

	// A value was altered after writing.
	std::string altered = serialized;
	altered.replace(altered.find("\"500\""), 5, "\"999\"");
	Check(Parse(altered, parsed) == ParseStatus::Damaged, "checksum catches altered value");

	// A line was dropped.
	std::string dropped = serialized;
	const size_t line = dropped.find("__ gen");
	dropped.erase(line, dropped.find('\n', line) - line + 1);
	Check(Parse(dropped, parsed) == ParseStatus::Damaged, "entry count catches dropped line");

	// Data after the trailer.
	Check(Parse(serialized + "__ extra \"1\"\n", parsed) == ParseStatus::Damaged, "data after trailer is damaged");

	// CRLF conversion by an editor is tolerated.
	std::string crlf;
	for (char c : serialized) {
		if (c == '\n')
			crlf += '\r';
		crlf += c;
	}
	Check(Parse(crlf, parsed) == ParseStatus::Valid, "CRLF line endings are tolerated");
	Check(parsed == entries, "CRLF parse keeps values");
}

void TestStoreRejectsUnsafeTokens()
{
	using namespace PersistentDataStore;
	Check(IsSafeToken("pilotLoadouts[0].primary", MaxKeySize), "accept normal key");
	Check(IsSafeToken("mp_weapon_rspn101", MaxValueSize), "accept normal value");
	Check(IsSafeToken("with space", MaxValueSize), "accept space in value");
	Check(!IsSafeToken("", MaxValueSize), "reject empty token");
	Check(!IsSafeToken("a;quit", MaxValueSize), "reject command separator");
	Check(!IsSafeToken("a\"b", MaxValueSize), "reject quote");
	Check(!IsSafeToken("a\nb", MaxValueSize), "reject newline");
	Check(!IsSafeToken(std::string(MaxValueSize + 1, 'v'), MaxValueSize), "reject oversized token");

	Entries parsed;
	const std::string hostile =
		"__ good \"1\"\n"
		"__ bad;key \"1\"\n"
		"__ key \"bad;value\"\n"
		"__ embedded \"a\"b\"\n";
	Check(Parse(hostile, parsed) == ParseStatus::Damaged, "unsafe lines make the file damaged");
	Check(parsed.size() == 1 && parsed["good"] == "1", "unsafe lines are never loaded");
}

void TestLegacyProfileMigration()
{
	using namespace PersistentDataStore;
	const std::string profile =
		"cl_fovScale \"1.3\"\n"
		"__ xp \"1000\"\r\n"
		"  __ gen \"2\"\n"
		"__ unquoted 5\n"
		"__ broken \"no-close\n"
		"__ bad;key \"1\"\n"
		"bind \"F\" \"+use\"\n"
		"__ xp \"1500\"\n"
		"__not_persistent \"1\"\n";

	Entries entries;
	Check(ExtractLegacyProfileEntries(profile, entries) == 4, "extract every well-formed legacy entry");
	Check(entries.size() == 3, "later legacy lines override earlier ones");
	Check(entries["xp"] == "1500", "legacy import keeps the last value");
	Check(entries["gen"] == "2", "legacy import tolerates leading whitespace");
	Check(entries["unquoted"] == "5", "legacy import accepts unquoted values");

	const std::string stripped = StripPersistentLines(profile);
	Check(stripped ==
		"cl_fovScale \"1.3\"\n"
		"bind \"F\" \"+use\"\n"
		"__not_persistent \"1\"\n",
		"strip every persistent-data line and keep everything else");
	Check(StripPersistentLines("__ xp \"1\"") == "", "strip unterminated final persistent line");

	Check(ProfileHasOwnerMarker("cl_fovScale \"1\"\ndelta_pdata_store \"1\"\n__ xp \"1\"\n"),
		"detect the owner marker written by this build");
	Check(!ProfileHasOwnerMarker(profile), "older profile has no owner marker");
	Check(!ProfileHasOwnerMarker("delta_pdata_store_other \"1\"\n"), "marker match is exact");
}

void TestLegacyMerge()
{
	using namespace PersistentDataStore;
	Entries store{ { "xp", "500" }, { "gen", "2" }, { "dormant.mod", "1" } };
	Check(MergeLegacyEntries(store, Entries{ { "xp", "500" }, { "gen", "2" } }) == 0,
		"an unchanged mirror imports nothing");
	Check(store.size() == 3, "mirror without dormant entries keeps them");
	Check(MergeLegacyEntries(store, Entries{ { "xp", "600" }, { "ranked.gems", "3" } }) == 2,
		"older-build changes are counted");
	Check(store["xp"] == "600" && store["ranked.gems"] == "3" && store["gen"] == "2",
		"older-build changes are merged over the store");
}

std::string Renamed(std::string_view text)
{
	std::string output;
	return PersistentDataStore::RenameTokens(text, output) ? output : std::string(text);
}

void TestRenameTokens()
{
	Check(Renamed("mp_weapon_mega1") == "mp_weapon_sr_valkyrie", "mega1 becomes the valkyrie");
	Check(Renamed("mp_weapon_mega2") == "mp_weapon_twinb_shotgun", "mega2 becomes the twin-b");
	Check(Renamed("mp_weapon_mega3") == "mp_titanweapon_minigun", "mega3 becomes the minigun");
	Check(Renamed("mp_weapon_mega4") == "mp_titanweapon_charge_cannon", "mega4 becomes the charge cannon");

	// Composite mod names and array keys: the weapon is a prefix or an index.
	Check(Renamed("mp_weapon_mega1_aog") == "mp_weapon_sr_valkyrie_aog", "mod composite is renamed");
	Check(Renamed("mp_weapon_mega1_burn_mod_valkyrie") == "mp_weapon_sr_valkyrie_burn_mod_valkyrie",
		"burn mod composite is renamed");
	Check(Renamed("mp_weapon_mega4_burn_mod_charge_cannon")
		== "mp_titanweapon_charge_cannon_burn_mod_charge_cannon", "charge cannon composite is renamed");
	Check(Renamed("newMods[mp_weapon_mega2_silencer]") == "newMods[mp_weapon_twinb_shotgun_silencer]",
		"array index is renamed");
	Check(Renamed("newLoadoutItems[mp_weapon_mega3]") == "newLoadoutItems[mp_titanweapon_minigun]",
		"loadout array index is renamed");

	// Token boundaries: other slots and glued text are not touched.
	Check(Renamed("mp_weapon_mega10") == "mp_weapon_mega10", "mega10 is not mega1");
	Check(Renamed("mp_weapon_mega11") == "mp_weapon_mega11", "mega11 is not mega1");
	Check(Renamed("mp_weapon_mega12_aog") == "mp_weapon_mega12_aog", "mega12 composite is not mega1");
	Check(Renamed("newLoadoutItems[mp_weapon_mega10]") == "newLoadoutItems[mp_weapon_mega10]",
		"mega10 array index is untouched");
	Check(Renamed("mp_weapon_mega5") == "mp_weapon_mega5", "mega5 has no persistent entry and stays");
	Check(Renamed("mp_weapon_mega6") == "mp_weapon_mega6", "mega6 stays");
	Check(Renamed("mp_weapon_mega") == "mp_weapon_mega", "bare prefix stays");
	Check(Renamed("mp_weapon_mega1x") == "mp_weapon_mega1x", "a letter after the digit is not a token end");
	Check(Renamed("xmp_weapon_mega1") == "xmp_weapon_mega1", "a letter before is not a token start");
	Check(Renamed("_mp_weapon_mega1") == "_mp_weapon_mega1", "an underscore before is not a token start");
	Check(Renamed("mp_weapon_mega_turret") == "mp_weapon_mega_turret", "mega_turret stays");
	Check(Renamed("mp_weapon_mega1mp_weapon_mega2") == "mp_weapon_mega1mp_weapon_mega2",
		"glued names are not tokens");
	Check(Renamed("") == "", "empty text");
	Check(Renamed("mp_weapon_mega") == "mp_weapon_mega", "truncated text at the end");
	Check(Renamed("mp_weapon_mega1") != "mp_weapon_mega1", "token at the very end");

	// Several tokens in one string, and ASCII case.
	Check(Renamed("a mp_weapon_mega1,mp_weapon_mega2.b") == "a mp_weapon_sr_valkyrie,mp_weapon_twinb_shotgun.b",
		"several tokens in one string");
	Check(Renamed("MP_WEAPON_MEGA1_AOG") == "mp_weapon_sr_valkyrie_AOG", "case insensitive match, canonical name");

	std::string output = "stale";
	Check(!PersistentDataStore::RenameTokens("mp_weapon_sniper", output) && output.empty(),
		"no rename reports false and clears the output");
}

void TestRenameMigratesProfile()
{
	using namespace PersistentDataStore;
	Entries entries{
		{ "xp", "500" },
		{ "pilotLoadouts[0].primary", "mp_weapon_mega1" },
		{ "pilotLoadouts[0].sidearm", "mp_weapon_mega2" },
		{ "pilotLoadouts[0].primaryMod", "mp_weapon_mega1_aog" },
		{ "titanLoadouts[3].primary", "mp_weapon_mega3" },
		{ "titanLoadouts[3].special", "mp_weapon_mega4" },
		{ "newLoadoutItems[mp_weapon_mega1]", "1" },
		{ "newMods[mp_weapon_mega1_burn_mod_valkyrie]", "1" },
		{ "newMods[mp_weapon_mega2_silencer]", "0" },
		{ "newMods[mp_weapon_mega4_burn_mod_charge_cannon]", "1" },
		{ "newLoadoutItems[mp_weapon_mega10]", "1" },
		{ "pilotLoadouts[1].primary", "mp_weapon_mega10" },
		{ "pilotLoadouts[2].primary", "mp_weapon_rspn101" },
	};
	const size_t before = entries.size();
	Check(MigrateRenamedNames(entries) == 9, "nine entries carry old names");
	Check(entries.size() == before, "migration does not add or lose entries");
	Check(entries.at("pilotLoadouts[0].primary") == "mp_weapon_sr_valkyrie", "value renamed");
	Check(entries.at("pilotLoadouts[0].sidearm") == "mp_weapon_twinb_shotgun", "sidearm value renamed");
	Check(entries.at("pilotLoadouts[0].primaryMod") == "mp_weapon_sr_valkyrie_aog", "mod composite value renamed");
	Check(entries.at("titanLoadouts[3].primary") == "mp_titanweapon_minigun", "titan primary renamed");
	Check(entries.at("titanLoadouts[3].special") == "mp_titanweapon_charge_cannon", "titan special renamed");
	Check(entries.at("newLoadoutItems[mp_weapon_sr_valkyrie]") == "1", "key renamed, value kept");
	Check(entries.count("newLoadoutItems[mp_weapon_mega1]") == 0, "old key is gone");
	Check(entries.at("newMods[mp_weapon_sr_valkyrie_burn_mod_valkyrie]") == "1", "burn mod key renamed");
	Check(entries.at("newMods[mp_weapon_twinb_shotgun_silencer]") == "0", "twin-b mod key renamed");
	Check(entries.at("newMods[mp_titanweapon_charge_cannon_burn_mod_charge_cannon]") == "1",
		"charge cannon mod key renamed");
	Check(entries.at("newLoadoutItems[mp_weapon_mega10]") == "1", "mega10 key untouched");
	Check(entries.at("pilotLoadouts[1].primary") == "mp_weapon_mega10", "mega10 value untouched");
	Check(entries.at("pilotLoadouts[2].primary") == "mp_weapon_rspn101", "other weapons untouched");
	Check(entries.at("xp") == "500", "unrelated keys untouched");

	// What gets written back carries only the new names and round trips.
	const std::string serialized = Serialize(entries);
	Check(serialized.find("mp_weapon_mega1_") == std::string::npos
		&& serialized.find("mp_weapon_mega1\"") == std::string::npos
		&& serialized.find("mp_weapon_mega1]") == std::string::npos
		&& serialized.find("mp_weapon_mega4") == std::string::npos,
		"serialized store holds no old names");
	Entries reloaded;
	Check(Parse(serialized, reloaded) == ParseStatus::Valid && reloaded == entries,
		"migrated store round trips as valid");
}

void TestRenameConflictAndIdempotence()
{
	using namespace PersistentDataStore;

	// Old and new variants of the same key: the new key is authoritative.
	Entries entries{
		{ "newMods[mp_weapon_mega1_aog]", "1" },
		{ "newMods[mp_weapon_sr_valkyrie_aog]", "0" },
		{ "newLoadoutItems[mp_weapon_mega2]", "1" },
	};
	Check(MigrateRenamedNames(entries) == 2, "both variants present: the old entries are counted");
	Check(entries.size() == 2, "the old variant is dropped, not merged");
	Check(entries.at("newMods[mp_weapon_sr_valkyrie_aog]") == "0", "new variant wins over the old one");
	Check(entries.at("newLoadoutItems[mp_weapon_twinb_shotgun]") == "1", "lone old key is moved");

	// Idempotent: a second pass changes nothing.
	const Entries snapshot = entries;
	Check(MigrateRenamedNames(entries) == 0, "second migration changes nothing");
	Check(entries == snapshot, "second migration leaves the entries identical");

	// Case-only collisions resolve deterministically (first in key order).
	Entries collide{
		{ "newMods[MP_WEAPON_MEGA1_aog]", "1" },
		{ "newMods[mp_weapon_mega1_aog]", "0" },
	};
	MigrateRenamedNames(collide);
	Check(collide.size() == 1 && collide.count("newMods[mp_weapon_sr_valkyrie_aog]") == 1,
		"case variants collapse onto one key");
	Check(collide.begin()->second == "1", "the first key in order wins a case collision");

	Entries empty;
	Check(MigrateRenamedNames(empty) == 0, "empty store");

	// A rename that would exceed the size limits is skipped, not truncated.
	const std::string longValue = std::string(MaxValueSize - 20, 'a') + " mp_weapon_mega4";
	Entries oversized{ { "key", longValue } };
	Check(MigrateRenamedNames(oversized) == 0, "an oversized rename is not counted");
	Check(oversized.at("key") == longValue, "a rename that would pass the size limit is skipped");
}

void TestRenameLegacyProfile()
{
	using namespace PersistentDataStore;

	// An old profile.cfg mirror: parse, migrate, merge over a store that already
	// holds new names, and the older build's progress still wins.
	const std::string profile =
		"cl_fovScale \"1.1\"\n"
		"__ pilotLoadouts[0].primary \"mp_weapon_mega1\"\n"
		"__ newMods[mp_weapon_mega1_stabilizer] \"1\"\n"
		"__ newLoadoutItems[mp_weapon_mega10] \"1\"\n";
	Entries legacy;
	Check(ExtractLegacyProfileEntries(profile, legacy) == 3, "three legacy entries");
	Check(MigrateRenamedNames(legacy) == 2, "two legacy entries use old names");

	Entries store{
		{ "pilotLoadouts[0].primary", "mp_weapon_rspn101" },
		{ "newMods[mp_weapon_sr_valkyrie_stabilizer]", "0" },
	};
	Check(MergeLegacyEntries(store, legacy) == 3, "legacy import changes three entries");
	Check(store.at("pilotLoadouts[0].primary") == "mp_weapon_sr_valkyrie", "legacy loadout lands under the new name");
	Check(store.at("newMods[mp_weapon_sr_valkyrie_stabilizer]") == "1", "legacy mod flag wins over the store");
	Check(store.count("newMods[mp_weapon_mega1_stabilizer]") == 0, "no old key survives the import");
	Check(store.at("newLoadoutItems[mp_weapon_mega10]") == "1", "mega10 imported unchanged");

	Check(IsSafeToken("newMods[mp_titanweapon_charge_cannon_burn_mod_charge_cannon]", MaxKeySize),
		"the longest new key is a safe token within the limit");
}

void TestLoadoutVerdicts()
{
	using namespace PersistentDataStore;
	const auto verdict = [](const char* key, const char* value) {
		return JudgeLoadoutValue(key, value);
	};

	// Pilot loadouts: pilot weapons stay legal, titan weapons are illegal.
	Check(verdict("pilotLoadouts[0].primary", "mp_weapon_sr_valkyrie") == LoadoutVerdict::Neutral,
		"valkyrie is legal for pilots");
	Check(verdict("pilotLoadouts[0].sidearm", "mp_weapon_twinb_shotgun") == LoadoutVerdict::Neutral,
		"twin-b is legal for pilots");
	Check(verdict("pilotLoadouts[0].primary", "mp_weapon_rspn101") == LoadoutVerdict::Neutral,
		"stock pilot weapon is legal for pilots");
	Check(verdict("pilotLoadouts[0].primaryMod", "mp_weapon_sr_valkyrie_aog") == LoadoutVerdict::Neutral,
		"valkyrie mod composite is legal for pilots");
	Check(verdict("pilotLoadouts[0].primary", "mp_titanweapon_minigun") == LoadoutVerdict::Reject,
		"minigun is illegal for pilots");
	Check(verdict("pilotLoadouts[0].special", "mp_titanweapon_charge_cannon") == LoadoutVerdict::Reject,
		"charge cannon is illegal for pilots");
	Check(verdict("pilotLoadouts[0].primaryMod", "mp_titanweapon_minigun_burn_mod_thunderbolt") == LoadoutVerdict::Reject,
		"titan weapon composite is illegal for pilots");
	Check(verdict("pilotLoadouts[0].primary", "mp_titanweapon_xo16") == LoadoutVerdict::Reject,
		"stock titan weapon is illegal for pilots");

	// Titan loadouts: titan weapons are legal, pilot weapons are not.
	Check(verdict("titanLoadouts[0].primary", "mp_titanweapon_minigun") == LoadoutVerdict::Accept,
		"minigun is legal for titans");
	Check(verdict("titanLoadouts[0].special", "mp_titanweapon_charge_cannon") == LoadoutVerdict::Accept,
		"charge cannon is legal for titans");
	Check(verdict("titanLoadouts[0].primaryMod", "mp_titanweapon_minigun_burn_mod_thunderbolt") == LoadoutVerdict::Accept,
		"minigun composite is legal for titans");
	Check(verdict("titanLoadouts[0].primary", "mp_weapon_sr_valkyrie") == LoadoutVerdict::Reject,
		"valkyrie is illegal for titans");
	Check(verdict("titanLoadouts[0].primary", "mp_weapon_twinb_shotgun") == LoadoutVerdict::Reject,
		"twin-b is illegal for titans");
	Check(verdict("titanLoadouts[0].primary", "mp_weapon_rspn101") == LoadoutVerdict::Reject,
		"stock pilot weapon is illegal for titans");
	Check(verdict("titanLoadouts[0].primary", "mp_titanweapon_xo16") == LoadoutVerdict::Neutral,
		"stock titan weapon goes to the schema for titans");
	Check(verdict("titanLoadouts[0].primary", "mp_weapon_mega6") == LoadoutVerdict::Accept,
		"the remaining mega slots keep their titan exemption");
	Check(verdict("titanLoadouts[0].primary", "mp_weapon_mega10") == LoadoutVerdict::Accept,
		"mega10 keeps its titan exemption");
	Check(verdict("titanLoadouts[0].name", "scout") == LoadoutVerdict::Neutral,
		"a name has no weapon rule");

	// Keys without a loadout have no weapon rule at all.
	Check(verdict("newLoadoutItems[mp_weapon_sr_valkyrie]", "1") == LoadoutVerdict::Neutral,
		"unlock flags have no weapon rule");
	Check(verdict("newMods[mp_titanweapon_minigun_burn_mod_thunderbolt]", "1") == LoadoutVerdict::Neutral,
		"titan mod flag has no weapon rule");

	// Old names from an older client are migrated first and then follow the new rules.
	const std::string oldTitan = Renamed("mp_weapon_mega3");
	Check(JudgeLoadoutValue("titanLoadouts[1].primary", oldTitan) == LoadoutVerdict::Accept,
		"old minigun name is accepted for titans once migrated");
	Check(JudgeLoadoutValue("pilotLoadouts[1].primary", oldTitan) == LoadoutVerdict::Reject,
		"old minigun name is rejected for pilots once migrated");
	const std::string oldPilot = Renamed("mp_weapon_mega1");
	Check(JudgeLoadoutValue("pilotLoadouts[1].primary", oldPilot) == LoadoutVerdict::Neutral,
		"old valkyrie name is legal for pilots once migrated");
	Check(JudgeLoadoutValue("titanLoadouts[1].primary", oldPilot) == LoadoutVerdict::Reject,
		"old valkyrie name is rejected for titans once migrated");
}

void TestPersistentPlayerSlots()
{
	using PersistentDataSlots::IsValidPlayerSlot;
	using PersistentDataSlots::IsReplayPlayerSlot;
	Check(!IsValidPlayerSlot(-1, 18), "reject negative player slot");
	Check(IsValidPlayerSlot(0, 18), "accept first player slot");
	Check(IsValidPlayerSlot(17, 18), "accept final 18-player slot");
	Check(!IsValidPlayerSlot(18, 18), "reject replay slot after 18 players");
	Check(IsReplayPlayerSlot(18), "identify replay slot");
	Check(!IsReplayPlayerSlot(17), "do not identify final client as replay");
	Check(!IsValidPlayerSlot(0, 0), "reject player slot before server initialization");
	Check(!IsValidPlayerSlot(0, 65), "reject unsupported max-client count");
}

void TestSessionBinding()
{
	using namespace PersistentDataState;
	PlayerState state;
	Check(!BeginSession(state, SessionKey{}), "reject a session without a netchannel");
	Check(BeginSession(state, SessionKey{ 0x1000, -1 }), "bind first connection");
	Check(RecordServerWrite(state, "__ xp", "100", nullptr, 0.0, true), "record a write");
	Check(BeginSession(state, SessionKey{ 0x1000, 7 }), "late userid on same connection");
	Check(state.pending.size() == 1, "late userid keeps pending writes");
	Check(BeginSession(state, SessionKey{ 0x1000, 8 }), "new userid in same slot");
	Check(state.pending.empty() && state.values.empty(), "new userid wipes the previous player's writes");
	RecordServerWrite(state, "__ xp", "100", nullptr, 0.0, true);
	Check(BeginSession(state, SessionKey{ 0x2000, 8 }), "new netchannel in same slot");
	Check(state.pending.empty() && state.values.empty(), "new netchannel wipes the previous player's writes");
}

void TestServerWriteSurvivesReconnectSnapshot()
{
	// The changelevel rollback: the server writes, the netchannel is cleared
	// before the command reaches the client, and the client reconnects with
	// its old snapshot.
	using namespace PersistentDataState;
	PlayerState state;
	BeginSession(state, SessionKey{ 0x1000, 3 });

	const std::string clientXp = "100";
	Check(RecordServerWrite(state, "__ xp", "250", &clientXp, 10.0, false), "server write must be sent");
	Check(*Find(state, "__ xp") == "250", "reads see the pending write immediately");
	Check(TakeOutbox(state, 10.0, SIZE_MAX).size() == 1, "the write goes out");

	EntryList snapshot{ { "__ xp", "100" }, { "__ gen", "1" } };
	ApplyClientUpdate(state, snapshot, true, false);
	Check(snapshot[0].second == "250", "stale snapshot value is rewritten to the pending write");
	Check(snapshot[1].second == "1", "unrelated values pass through");
	Check(state.pending.count("__ xp") == 1, "stale snapshot does not acknowledge the write");

	Check(CollectResends(state, 10.1).empty(), "a snapshot right after the send is held back");
	EntryList due = CollectResends(state, 11.1);
	Check(due.size() == 1 && due[0].first == "__ xp" && due[0].second == "250",
		"full snapshot triggers a resend once the hold has passed");
	Check(CollectResends(state, 11.2).empty(), "no duplicate resend right away");

	EntryList echo{ { "__ xp", "250" } };
	ApplyClientUpdate(state, echo, false, false);
	Check(state.pending.empty(), "client echo acknowledges the write");
	Check(Find(state, "__ xp") == nullptr, "acknowledged write falls back to the engine table");
}

void TestSnapshotMissingPendingKeyIsAppended()
{
	using namespace PersistentDataState;
	PlayerState state;
	BeginSession(state, SessionKey{ 0x1000, 3 });
	RecordServerWrite(state, "__ challenges[5].progress", "3", nullptr, 0.0, false);

	EntryList snapshot{ { "__ xp", "100" } };
	ApplyClientUpdate(state, snapshot, true, false);
	Check(snapshot.size() == 2, "pending key missing from a full snapshot is appended");
	Check(snapshot[1].first == "__ challenges[5].progress" && snapshot[1].second == "3",
		"appended entry carries the pending value");

	EntryList delta{ { "__ xp", "101" } };
	ApplyClientUpdate(state, delta, false, false);
	Check(delta.size() == 1, "deltas are never padded");
}

void TestOlderEchoDoesNotAcknowledgeNewerWrite()
{
	using namespace PersistentDataState;
	PlayerState state;
	BeginSession(state, SessionKey{ 0x1000, 3 });
	RecordServerWrite(state, "__ xp", "200", nullptr, 0.0, false);
	RecordServerWrite(state, "__ xp", "300", nullptr, 0.1, false);

	EntryList olderEcho{ { "__ xp", "200" } };
	ApplyClientUpdate(state, olderEcho, false, false);
	Check(olderEcho[0].second == "300", "echo of an older write is rewritten to the newest");
	Check(state.pending.count("__ xp") == 1, "echo of an older write keeps the newest pending");

	EntryList newestEcho{ { "__ xp", "300" } };
	ApplyClientUpdate(state, newestEcho, false, false);
	Check(state.pending.empty(), "echo of the newest write acknowledges it");
}

void TestDuplicateReportsCannotReplacePendingWrite()
{
	using namespace PersistentDataState;
	for (bool fullSnapshot : { false, true }) {
		for (bool keepMirror : { false, true }) {
			PlayerState state;
			BeginSession(state, SessionKey{ 0x1000, 3 });
			RecordServerWrite(state, "__ xp", "300", nullptr, 0.0, keepMirror);

			EntryList reports{ { "__ xp", "300" }, { "__ xp", "100" } };
			ApplyClientUpdate(state, reports, fullSnapshot, keepMirror);
			Check(reports[0].second == "300" && reports[1].second == "300",
				"an early acknowledgement cannot expose a later stale report");
			Check(state.pending.count("__ xp") == 1 && *Find(state, "__ xp") == "300",
				"the last stale report keeps the write pending");

			EntryList acknowledged{ { "__ xp", "100" }, { "__ xp", "300" } };
			ApplyClientUpdate(state, acknowledged, fullSnapshot, keepMirror);
			Check(acknowledged[0].second == "300" && acknowledged[1].second == "300",
				"every duplicate report is reconciled before acknowledging");
			Check(state.pending.empty(), "the last current report acknowledges the write");
			if (keepMirror)
				Check(*Find(state, "__ xp") == "300", "the mirror retains the acknowledged value");
		}
	}
}

void TestRedundantWritesAreNotSent()
{
	using namespace PersistentDataState;
	PlayerState state;
	BeginSession(state, SessionKey{ 0x1000, 3 });
	const std::string clientValue = "5";
	Check(!RecordServerWrite(state, "__ gen", "5", &clientValue, 0.0, false), "write equal to the client value is skipped");
	Check(state.pending.empty(), "skipped write is not pending");
	Check(RecordServerWrite(state, "__ gen", "6", &clientValue, 0.0, false), "changed value is sent");
	Check(!RecordServerWrite(state, "__ gen", "6", &clientValue, 0.0, false), "repeat of a pending value is skipped");
	const std::string staleClient = "6";
	Check(RecordServerWrite(state, "__ gen", "5", &staleClient, 0.0, false),
		"write compares against the pending value, not the engine table");
}

void TestTimedResendBackoff()
{
	using namespace PersistentDataState;
	PlayerState state;
	BeginSession(state, SessionKey{ 0x1000, 3 });
	RecordServerWrite(state, "__ xp", "1", nullptr, 0.0, false);
	TakeOutbox(state, 0.0, SIZE_MAX);

	Check(CollectResends(state, 1.0).empty(), "no resend before the first delay");
	Check(CollectResends(state, 2.0).size() == 1, "first timed resend after 2s");
	Check(CollectResends(state, 5.0).empty(), "second resend waits 4s");
	Check(CollectResends(state, 6.0).size() == 1, "second timed resend after 4s");
	double now = 6.0;
	int sent = 2;
	for (int i = 0; i < 20; ++i) {
		now += 1000.0;
		sent += static_cast<int>(CollectResends(state, now).size());
	}
	Check(sent == kMaxTimedResends, "timed resends are capped");

	EntryList snapshot{ { "__ xp", "0" } };
	ApplyClientUpdate(state, snapshot, true, false);
	Check(CollectResends(state, now + 0.01).size() == 1, "a reconnect snapshot resends even after the cap");
}

void TestMirrorKeepsFullDataSet()
{
	// R1O servers mirror the whole data set. A delta must never shrink it (the
	// old implementation treated any large delta as a full replacement).
	using namespace PersistentDataState;
	PlayerState state;
	BeginSession(state, SessionKey{ 0x1000, 3 });
	EntryList snapshot;
	for (int i = 0; i < 100; ++i)
		snapshot.emplace_back("__ k" + std::to_string(i), std::to_string(i));
	ApplyClientUpdate(state, snapshot, true, true);
	Check(state.values.size() == 100, "snapshot fills the mirror");

	EntryList bigDelta;
	for (int i = 0; i < 40; ++i)
		bigDelta.emplace_back("__ k" + std::to_string(i), "changed");
	ApplyClientUpdate(state, bigDelta, false, true);
	Check(state.values.size() == 100, "large delta keeps the rest of the mirror");
	Check(state.values["__ k0"] == "changed" && state.values["__ k99"] == "99", "delta merges into the mirror");

	RecordServerWrite(state, "__ k50", "server", nullptr, 0.0, true);
	Check(*Find(state, "__ k50") == "server", "mirror reads see the server write");
	EntryList staleSnapshot{ { "__ k50", "50" } };
	ApplyClientUpdate(state, staleSnapshot, true, true);
	Check(state.values.size() == 1 && state.values["__ k50"] == "server",
		"full snapshot replaces the mirror but keeps the pending write");
}

void TestFullSnapshotsBackToBackDoNotMultiplyResends()
{
	// A fresh profile: hundreds of writes at once, then several full snapshots
	// that report nothing arrive within milliseconds (the 4x resend burst that
	// filled a joining client's reliable stream).
	using namespace PersistentDataState;
	PlayerState state;
	BeginSession(state, SessionKey{ 0x1000, 3 });
	for (int i = 0; i < 500; ++i)
		RecordServerWrite(state, "__ k" + std::to_string(i), "v", nullptr, 0.0, false);
	Check(TakeOutbox(state, 0.0, SIZE_MAX).size() == 500, "all writes go out once");

	for (int snapshot = 0; snapshot < 4; ++snapshot) {
		EntryList empty;
		ApplyClientUpdate(state, empty, true, false);
		Check(QueueResends(state, 0.001 * (snapshot + 1)) == 0, "snapshot right after the send is held back");
	}
	Check(state.outbox.empty(), "nothing is queued during the hold");

	Check(QueueResends(state, 1.5) == 500, "after the hold the unacknowledged writes resend once");
	for (int snapshot = 0; snapshot < 4; ++snapshot) {
		EntryList empty;
		ApplyClientUpdate(state, empty, true, false);
		Check(QueueResends(state, 1.5) == 0, "a write waiting in the outbox is not queued again");
	}
	Check(state.outbox.size() == 500, "one outbox entry per write");
}

void TestHeldSnapshotResendIsDroppedWhenAcknowledged()
{
	using namespace PersistentDataState;
	PlayerState state;
	BeginSession(state, SessionKey{ 0x1000, 3 });
	RecordServerWrite(state, "__ xp", "5", nullptr, 0.0, false);
	TakeOutbox(state, 0.0, SIZE_MAX);

	EntryList snapshot{ { "__ xp", "1" } };
	ApplyClientUpdate(state, snapshot, true, false);
	Check(QueueResends(state, 0.1) == 0, "held back right after the send");
	EntryList echo{ { "__ xp", "5" } };
	ApplyClientUpdate(state, echo, false, false);
	Check(state.pending.empty() && QueueResends(state, 5.0) == 0, "an acknowledged write is never resent");
}

void TestOutboxIsPaced()
{
	using namespace PersistentDataState;
	PlayerState state;
	BeginSession(state, SessionKey{ 0x1000, 3 });
	for (int i = 0; i < 1000; ++i)
		RecordServerWrite(state, "__ k" + std::to_string(i), "v", nullptr, 0.0, false);

	Check(TakePaced(state, 1.0, SIZE_MAX).size() == static_cast<size_t>(kSendBurst), "first call sends one burst");
	Check(TakePaced(state, 1.0, SIZE_MAX).empty(), "the budget is spent");
	const size_t after100ms = TakePaced(state, 1.1, SIZE_MAX).size();
	Check(after100ms >= 59 && after100ms <= 61, "the budget refills at kSendPerSecond");
	Check(TakePaced(state, 1.1, 10).empty(), "no refill without elapsed time");
	Check(TakePaced(state, 100.0, 10).size() == 10, "hardLimit bounds a single call");
	Check(TakePaced(state, 200.0, SIZE_MAX).size() == static_cast<size_t>(kSendBurst), "the budget never exceeds one burst");
	size_t sent = 128 + after100ms + 10 + 128;
	for (double t = 201.0; !state.outbox.empty(); t += 1.0)
		sent += TakePaced(state, t, SIZE_MAX).size();
	Check(sent == 1000, "every write is eventually sent exactly once");
}

void TestOutboxSkipsAcknowledgedAndStaleKeys()
{
	using namespace PersistentDataState;
	PlayerState state;
	BeginSession(state, SessionKey{ 0x1000, 3 });
	RecordServerWrite(state, "__ a", "1", nullptr, 0.0, false);
	RecordServerWrite(state, "__ b", "1", nullptr, 0.0, false);
	EntryList echoA{ { "__ a", "1" } };
	ApplyClientUpdate(state, echoA, false, false);
	EntryList out = TakeOutbox(state, 0.0, SIZE_MAX);
	Check(out.size() == 1 && out[0].first == "__ b", "a key acknowledged while queued is not sent");

	// Same key written, acknowledged and written again before the outbox is read:
	// the stale outbox entry must not send it twice.
	RecordServerWrite(state, "__ c", "1", nullptr, 0.0, false);
	EntryList echoC{ { "__ c", "1" } };
	ApplyClientUpdate(state, echoC, false, false);
	RecordServerWrite(state, "__ c", "2", nullptr, 0.0, false);
	out = TakeOutbox(state, 0.0, SIZE_MAX);
	Check(out.size() == 1 && out[0].first == "__ c" && out[0].second == "2", "a rewritten key is sent once, newest value");

	// Overwriting a queued value keeps one outbox entry and sends the newest.
	RecordServerWrite(state, "__ d", "1", nullptr, 0.0, false);
	RecordServerWrite(state, "__ d", "2", nullptr, 0.0, false);
	out = TakeOutbox(state, 0.0, SIZE_MAX);
	Check(out.size() == 1 && out[0].second == "2", "overwriting a queued write sends only the newest value");
}
}

int main()
{
	TestPackedRoundTrip();
	TestPackedLimits();
	TestStoreRoundTrip();
	TestStoreDetectsDamage();
	TestStoreRejectsUnsafeTokens();
	TestLegacyProfileMigration();
	TestLegacyMerge();
	TestRenameTokens();
	TestRenameMigratesProfile();
	TestRenameConflictAndIdempotence();
	TestRenameLegacyProfile();
	TestLoadoutVerdicts();
	TestPersistentPlayerSlots();
	TestSessionBinding();
	TestServerWriteSurvivesReconnectSnapshot();
	TestSnapshotMissingPendingKeyIsAppended();
	TestOlderEchoDoesNotAcknowledgeNewerWrite();
	TestDuplicateReportsCannotReplacePendingWrite();
	TestRedundantWritesAreNotSent();
	TestTimedResendBackoff();
	TestMirrorKeepsFullDataSet();
	TestFullSnapshotsBackToBackDoNotMultiplyResends();
	TestHeldSnapshotResendIsDroppedWhenAcknowledged();
	TestOutboxIsPaced();
	TestOutboxSkipsAcknowledgedAndStaleKeys();

	if (failures) {
		std::cerr << failures << " persistent-data test(s) failed\n";
		return 1;
	}
	std::cout << "All persistent-data tests passed\n";
	return 0;
}
