#include "shared/persistentdata_codec.h"

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
	if (argc > 1)
		TestProfileFile(argv[1]);

	if (failures) {
		std::cerr << failures << " persistent-data test(s) failed\n";
		return 1;
	}
	std::cout << "All persistent-data codec tests passed\n";
	return 0;
}
