#include "shared/persistentdata_codec.h"
#include "shared/persistentdata_slots.h"
#include "shared/persistentdata_state.h"
#include "shared/persistentdata_transaction.h"

#include <fstream>
#include <iostream>
#include <iterator>
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

bool TestValidator(std::string_view key, std::string_view value, void*)
{
	return key != "rejected" && value != "rejected";
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

void TestProfileValidation()
{
	using PersistentDataCodec::ValidateProfile;
	Check(ValidateProfile("__ key \"value\"\ncvar \"1\"\n", TestValidator), "accept valid complete profile");
	Check(ValidateProfile("__ same \"1\"\nsame \"2\"\n", TestValidator), "persistent and cvar identities are distinct");
	Check(!ValidateProfile("", TestValidator), "reject empty profile");
	Check(!ValidateProfile("__ key \"value\"", TestValidator), "reject truncated final line");
	Check(!ValidateProfile("__ key \"1\"\n__ key \"2\"\n", TestValidator), "reject duplicate persistent key");
	Check(!ValidateProfile("cvar \"1\"\ncvar \"2\"\n", TestValidator), "reject duplicate cvar");
	Check(!ValidateProfile("__ bad;key \"1\"\n", TestValidator), "reject command separator in key");
	Check(!ValidateProfile("__ key unquoted\n", TestValidator), "reject unquoted value");
	Check(!ValidateProfile("__ key \"embedded\"quote\"\n", TestValidator), "reject embedded quote");
	Check(!ValidateProfile("__ rejected \"1\"\n", TestValidator), "apply persistent schema validator");
	Check(!ValidateProfile("__ key \"rejected\"\n", TestValidator), "apply persistent value validator");
	Check(ValidateProfile("rejected \"rejected\"\n", TestValidator), "schema validator does not reject regular cvars");

	std::string largeProfile;
	largeProfile.reserve(2 * 1024 * 1024);
	const std::string largeValue(220, 'v');
	for (size_t i = 0; i < 5000; ++i) {
		largeProfile += "__ large[" + std::to_string(i) + "] \"" + largeValue + "\"\n";
	}
	Check(largeProfile.size() > 1024 * 1024, "large profile exceeds obsolete one-megabyte cap");
	Check(largeProfile.size() < PersistentDataCodec::MaxRawSize, "large profile remains within supported bound");
	Check(ValidateProfile(largeProfile, TestValidator), "accept complete profile beyond obsolete one-megabyte cap");
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

void TestProfileRecoverySelection()
{
	using PersistentDataTransaction::RecoveryAction;
	using PersistentDataTransaction::SelectRecoveryAction;
	Check(
		SelectRecoveryAction(true, false) == RecoveryAction::CommitPrimary,
		"commit a valid primary without a backup");
	Check(
		SelectRecoveryAction(true, true) == RecoveryAction::CommitPrimary,
		"prefer a valid primary over an older backup");
	Check(
		SelectRecoveryAction(false, true) == RecoveryAction::RestoreBackup,
		"restore a valid backup after an invalid save");
	Check(
		SelectRecoveryAction(false, false) == RecoveryAction::PreserveForRecovery,
		"preserve evidence when neither transaction file validates");
}

void TestPersistentDataStateUpdates()
{
	using PersistentDataState::Merge;
	using PersistentDataState::PlayerState;
	using PersistentDataState::Replace;
	using PersistentDataState::SessionKey;
	using PersistentDataState::Values;

	PlayerState state;
	const SessionKey firstSession{ 0x1000, 4 };
	Check(
		Replace(state, firstSession, Values{ { "__ xp", "100" }, { "__ gen", "2" } }),
		"accept a full persistent-data snapshot");
	Check(state.values.size() == 2, "snapshot installs every persistent-data key");
	Check(
		Merge(state, firstSession, Values{ { "__ xp", "125" } }),
		"accept a persistent-data delta");
	Check(state.values.size() == 2, "delta preserves unrelated persistent-data keys");
	Check(state.values["__ xp"] == "125", "delta replaces its named key");
	Check(state.values["__ gen"] == "2", "delta retains the generation key");

	Check(
		Replace(state, firstSession, Values{ { "__ xp", "200" } }),
		"accept a later full persistent-data snapshot");
	Check(state.values.size() == 1, "later snapshot removes stale keys");
	Check(state.values["__ xp"] == "200", "later snapshot installs current value");

	PlayerState lateUserId;
	Check(
		Replace(lateUserId, SessionKey{ 0x2000, -1 }, Values{ { "__ xp", "300" } }),
		"accept snapshot before userid assignment");
	Check(
		Merge(lateUserId, SessionKey{ 0x2000, 9 }, Values{ { "__ gen", "3" } }),
		"promote userid on the same network session");
	Check(lateUserId.values.size() == 2, "userid promotion does not discard the profile");

	Check(
		Merge(lateUserId, SessionKey{ 0x2000, 10 }, Values{ { "__ xp", "400" } }),
		"accept a reused slot with a new userid");
	Check(lateUserId.values.size() == 1, "new userid clears the reused slot before merging");
	Check(lateUserId.values["__ xp"] == "400", "new userid receives only its own delta");

	Check(
		Merge(lateUserId, SessionKey{ 0x3000, 10 }, Values{ { "__ gen", "4" } }),
		"accept a replacement network session");
	Check(lateUserId.values.size() == 1, "new netchannel clears the previous session");
	Check(lateUserId.values["__ gen"] == "4", "new netchannel receives only its own data");
	Check(
		!Merge(lateUserId, SessionKey{}, Values{ { "__ xp", "500" } }),
		"reject updates without a network-session owner");
}

void TestProfileFile(const char* path)
{
	std::ifstream file(path, std::ios::binary);
	Check(file.good(), "open supplied profile fixture");
	if (!file)
		return;
	const std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	Check(contents.size() > 16 * 1024, "profile fixture exceeds obsolete 16 KiB cap");
	Check(PersistentDataCodec::ValidateProfile(contents), "validate supplied complete profile fixture");
}

}

int main(int argc, char** argv)
{
	TestPackedRoundTrip();
	TestPackedLimits();
	TestProfileValidation();
	TestPersistentPlayerSlots();
	TestProfileRecoverySelection();
	TestPersistentDataStateUpdates();
	if (argc > 1)
		TestProfileFile(argv[1]);

	if (failures) {
		std::cerr << failures << " persistent-data test(s) failed\n";
		return 1;
	}
	std::cout << "All persistent-data codec tests passed\n";
	return 0;
}
