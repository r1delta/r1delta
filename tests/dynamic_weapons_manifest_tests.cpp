#include "../shared/dynamic_weapons_manifest.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace r1delta::dynamic_weapons;

namespace {

int g_failures = 0;

void Check(bool condition, const char* what)
{
	if (!condition) {
		std::printf("FAIL: %s\n", what);
		++g_failures;
	}
}

bool Names(const ManifestParse& parse, const std::vector<std::string>& expected)
{
	return parse.ok && parse.names == expected;
}

void TestValidManifest()
{
	const auto parse = ParseManifest(
		"// leading comment\n"
		"weaponscripts\n"
		"{\n"
		"\t\"file\"\t\"mp_weapon_sr_valkyrie\"\n"
		"\t\"file\"\t\"mp_weapon_twinb_shotgun\" // trailing comment\n"
		"}\n");
	Check(Names(parse, { "mp_weapon_sr_valkyrie", "mp_weapon_twinb_shotgun" }), "two quoted entries");
}

void TestBareAndQuotedTokens()
{
	Check(Names(ParseManifest("\"weaponscripts\" { file mp_weapon_a file \"mp_weapon_b\" }"),
		{ "mp_weapon_a", "mp_weapon_b" }), "bare and quoted tokens mix");
	Check(Names(ParseManifest("weaponscripts{\"FILE\"\"mp_weapon_a\"}"), { "mp_weapon_a" }),
		"keyword is case insensitive, no separators needed around quotes");
}

void TestCrlfAndComments()
{
	Check(Names(ParseManifest("weaponscripts\r\n{\r\n\t// \"file\" \"mp_weapon_commented\"\r\n\t\"file\" \"mp_weapon_a\"\r\n}\r\n"),
		{ "mp_weapon_a" }), "CRLF line endings and a commented out entry");
}

void TestSortedUnique()
{
	Check(Names(ParseManifest("w { file mp_weapon_z file mp_weapon_a file MP_WEAPON_A file mp_weapon_m }"),
		{ "mp_weapon_a", "mp_weapon_m", "mp_weapon_z" }), "sorted, unique, lower case");
}

void TestEntryNormalization()
{
	std::string name;
	std::string error;
	Check(NormalizeClassName("scripts/weapons/Mp_Weapon_X.txt", name, error) && name == "mp_weapon_x",
		"folder and extension are dropped, name is lower cased");
	Check(NormalizeClassName("scripts\\weapons\\mp_weapon_x.ctx", name, error) && name == "mp_weapon_x",
		"backslash folders");
	Check(NormalizeClassName("mp_weapon_x", name, error) && name == "mp_weapon_x", "plain class name");
	Check(!NormalizeClassName("", name, error), "empty entry is rejected");
	Check(!NormalizeClassName("mp weapon", name, error), "space is rejected");
	Check(!NormalizeClassName("mp-weapon", name, error), "dash is rejected");
	Check(!NormalizeClassName("weapons/", name, error), "folder without a name is rejected");
	const std::string exact(MaxClassNameLength, 'a');
	Check(NormalizeClassName(exact, name, error) && name == exact, "longest native name is accepted");
	Check(!NormalizeClassName(exact + "a", name, error), "one character over the native char[40] limit is rejected");
}

void TestMalformed()
{
	Check(!ParseManifest("").ok, "empty text");
	Check(!ParseManifest("   // only a comment\n").ok, "comment only");
	Check(!ParseManifest("{ file mp_weapon_a }").ok, "no root key");
	Check(!ParseManifest("weaponscripts").ok, "root key without a block");
	Check(!ParseManifest("weaponscripts { file mp_weapon_a").ok, "missing closing brace");
	Check(!ParseManifest("weaponscripts { }").ok, "empty list is rejected");
	Check(!ParseManifest("weaponscripts { file }").ok, "file without a value");
	Check(!ParseManifest("weaponscripts { file { } }").ok, "file followed by a brace");
	Check(!ParseManifest("weaponscripts { files mp_weapon_a }").ok, "unknown key");
	Check(!ParseManifest("weaponscripts { \"file\" \"mp_weapon_a }").ok, "unterminated string");
	Check(!ParseManifest("weaponscripts { \"file\" \"mp_weapon_a\n\" }").ok, "string across a line break");
	Check(!ParseManifest("weaponscripts { file mp_weapon_a } trailing").ok, "content after the closing brace");
	Check(!ParseManifest("weaponscripts { file mp_weapon_a { } }").ok, "nested block");
	Check(!ParseManifest("weaponscripts { #base file mp_weapon_a }").ok, "directive");
	Check(!ParseManifest("weaponscripts { file \"mp weapon\" }").ok, "invalid class name inside a valid grammar");
	const auto bad = ParseManifest("weaponscripts { file }");
	Check(!bad.error.empty() && bad.names.empty(), "failure carries a message and no names");
}

void TestRosterToken()
{
	Check(MakeRosterToken({}) == "0:cbf29ce484222325", "empty roster token is the FNV offset basis");
	const std::vector<std::string> a = { "mp_weapon_a", "mp_weapon_b" };
	const std::vector<std::string> b = { "mp_weapon_a", "mp_weapon_c" };
	const std::vector<std::string> c = { "mp_weapon_a" };
	Check(MakeRosterToken(a) == MakeRosterToken(a), "token is deterministic");
	Check(MakeRosterToken(a) != MakeRosterToken(b), "different name, different token");
	Check(MakeRosterToken(a) != MakeRosterToken(c), "different count, different token");
	Check(MakeRosterToken(a).rfind("2:", 0) == 0, "token starts with the count");
	// Names are separated, so "ab","c" differs from "a","bc".
	Check(MakeRosterToken({ "ab", "c" }) != MakeRosterToken({ "a", "bc" }), "name boundaries are part of the hash");
	// The shipped CORE manifest, as two clients and a server would read it.
	const auto shipped = ParseManifest(
		"weaponscripts\n{\n\t\"file\"\t\"mp_weapon_sr_valkyrie\"\n\t\"file\"\t\"mp_weapon_twinb_shotgun\"\n"
		"\t\"file\"\t\"mp_titanweapon_minigun\"\n\t\"file\"\t\"mp_titanweapon_charge_cannon\"\n"
		"\t\"file\"\t\"mp_weapon_throwing_knife\"\n}\n");
	const auto reordered = ParseManifest(
		"weaponscripts { file mp_weapon_throwing_knife file mp_titanweapon_charge_cannon file mp_titanweapon_minigun "
		"file mp_weapon_twinb_shotgun file mp_weapon_sr_valkyrie }");
	Check(shipped.ok && reordered.ok && MakeRosterToken(shipped.names) == MakeRosterToken(reordered.names),
		"entry order and formatting do not change the token");
	Check(shipped.ok && MakeRosterToken(shipped.names) == "5:34624381bf2a3726",
		"shipped five weapon roster token (matches the token the game logs)");
}

} // namespace

int main()
{
	TestValidManifest();
	TestBareAndQuotedTokens();
	TestCrlfAndComments();
	TestSortedUnique();
	TestEntryNormalization();
	TestMalformed();
	TestRosterToken();
	if (g_failures) {
		std::printf("%d failure(s)\n", g_failures);
		return 1;
	}
	std::printf("dynamic_weapons_manifest_tests: all passed\n");
	return 0;
}
