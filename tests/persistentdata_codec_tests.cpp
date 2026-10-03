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

	EntryList snapshot{ { "__ xp", "100" }, { "__ gen", "1" } };
	ApplyClientUpdate(state, snapshot, true, false);
	Check(snapshot[0].second == "250", "stale snapshot value is rewritten to the pending write");
	Check(snapshot[1].second == "1", "unrelated values pass through");
	Check(state.pending.count("__ xp") == 1, "stale snapshot does not acknowledge the write");

	EntryList due = CollectResends(state, 10.1);
	Check(due.size() == 1 && due[0].first == "__ xp" && due[0].second == "250",
		"full snapshot triggers an immediate resend");
	Check(CollectResends(state, 10.2).empty(), "no duplicate resend right away");

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
	TestPersistentPlayerSlots();
	TestSessionBinding();
	TestServerWriteSurvivesReconnectSnapshot();
	TestSnapshotMissingPendingKeyIsAppended();
	TestOlderEchoDoesNotAcknowledgeNewerWrite();
	TestDuplicateReportsCannotReplacePendingWrite();
	TestRedundantWritesAreNotSent();
	TestTimedResendBackoff();
	TestMirrorKeepsFullDataSet();

	if (failures) {
		std::cerr << failures << " persistent-data test(s) failed\n";
		return 1;
	}
	std::cout << "All persistent-data tests passed\n";
	return 0;
}
