#include "../shared/movement_fingerprint.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace r1delta::movement;

namespace {

int g_failures = 0;

void Check(bool condition, const char* what)
{
	if (!condition) {
		std::printf("FAIL: %s\n", what);
		++g_failures;
	}
}

std::string Fp(const char* name, unsigned slots, const std::string& source)
{
	char text[65]{};
	if (!FingerprintMovementSource(name, source.c_str(), slots, text)) {
		std::printf("FAIL: fingerprint could not be computed\n");
		++g_failures;
		return {};
	}
	return text;
}

// Shaped like the real programs: a Squirrel verbatim string with a block, strings and comments over several lines.
const char kLf[] =
	"if (Move_GetPhase() == MOVE_SETUP) Move_SetCheckpointMask(MOVE_CHECKPOINT_ALL);\n"
	"{\n"
	"    local a = 1;\n"
	"    // comment\n"
	"    local b = \"x\";\n"
	"}\n";

std::string ToCrlf(const std::string& lf)
{
	std::string out;
	for (char c : lf) {
		if (c == '\n') out += "\r\n";
		else out += c;
	}
	return out;
}

void TestNormalize()
{
	Check(NormalizeMovementSource("") == "", "empty stays empty");
	Check(NormalizeMovementSource("a\nb") == "a\nb", "LF is untouched");
	Check(NormalizeMovementSource("a\r\nb") == "a\nb", "CRLF becomes LF");
	Check(NormalizeMovementSource("a\rb") == "a\nb", "lone CR becomes LF");
	Check(NormalizeMovementSource("a\r\n\r\nb") == "a\n\nb", "two CRLF stay two newlines");
	Check(NormalizeMovementSource("a\r\rb") == "a\n\nb", "two CR stay two newlines");
	Check(NormalizeMovementSource("a\n\rb") == "a\n\nb", "LF CR is two newlines");
	Check(NormalizeMovementSource("a\r") == "a\n", "trailing CR becomes LF");
	Check(NormalizeMovementSource("a \r\nb") == "a \nb", "trailing blanks are kept");
	Check(NormalizeMovementSource(ToCrlf(kLf)) == kLf, "a whole CRLF program normalizes to the LF program");
}

void TestFingerprint()
{
	const std::string lf = kLf;
	const std::string crlf = ToCrlf(lf);
	const std::string cr = [&] { std::string s = lf; for (char& c : s) if (c == '\n') c = '\r'; return s; }();
	std::string mixed = lf;
	mixed.replace(mixed.find('\n'), 1, "\r\n");

	const std::string reference = Fp("pilot.slide_grapple", 64, lf);
	Check(reference.size() == 64, "fingerprint is 64 hex characters");
	Check(Fp("pilot.slide_grapple", 64, crlf) == reference, "CRLF and LF checkouts fingerprint the same");
	Check(Fp("pilot.slide_grapple", 64, cr) == reference, "CR-only and LF fingerprint the same");
	Check(Fp("pilot.slide_grapple", 64, mixed) == reference, "mixed endings fingerprint the same");

	Check(Fp("pilot.slide_grapple", 64, "{\n    local a = 1;\n    local b = \"x\";\n}\n")
		== "17f036662fdb43a9c55e09558901233cbd8bcb6b2de7cd1b35129cc2da4bbebf",
		"an LF source keeps the pre-normalization fingerprint (SHA-256 computed independently), so existing LF peers still match");

	std::string changed = lf;
	changed.replace(changed.find("a = 1"), 5, "a = 2");
	Check(Fp("pilot.slide_grapple", 64, changed) != reference, "a changed constant differs");
	Check(Fp("pilot.slide_grapple", 64, ToCrlf(changed)) != reference, "a changed constant differs under CRLF too");
	Check(Fp("pilot.slide_grapple", 64, ToCrlf(changed)) == Fp("pilot.slide_grapple", 64, changed), "changed program is still ending independent");
	Check(Fp("pilot.slide_grapple", 64, lf + "\n") != reference, "an extra blank line is a real difference");
	Check(Fp("pilot.slide_grapple", 64, lf.substr(0, lf.size() - 1)) != reference, "a missing final newline is a real difference");
	Check(Fp("pilot.slide_grapple", 64, [&] { std::string s = lf; s.replace(s.find("local a"), 1, " l"); return s; }()) != reference,
		"changed whitespace inside a line differs");
	Check(Fp("pilot.slide_grapple2", 64, lf) != reference, "a different program name differs");
	Check(Fp("pilot.slide_grapple", 63, lf) != reference, "a different slot count differs");
}

} // namespace

int main()
{
	TestNormalize();
	TestFingerprint();
	if (g_failures) {
		std::printf("%d failure(s)\n", g_failures);
		return 1;
	}
	std::printf("movement_fingerprint_tests: all checks passed\n");
	return 0;
}
