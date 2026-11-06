#include "../engine/net/script_error_payload.h"
#include "../engine/net/script_modding.h"

#include <cstdio>
#include <map>
#include <string>

using namespace ScriptModding;
using ScriptErrorTelemetry::BuildPayload;
using ScriptErrorTelemetry::Frame;
using ScriptErrorTelemetry::ReportBody;
using ScriptErrorTelemetry::SelectSchema;

namespace {

int g_failures = 0;

void Check(bool condition, const char* what)
{
	if (!condition) {
		std::printf("FAIL: %s\n", what);
		++g_failures;
	}
}

// 64 lowercase hex digits distinguishable by their first character.
std::string Hash(char digit)
{
	return std::string(kSha256HexLength, digit);
}

const std::string kStock = Hash('a');
const std::string kEdited = Hash('b');

std::string ManifestText()
{
	return std::string(kManifestHeader) + "\n"
		+ "S " + kStock + " scripts/vscripts/client/_cl_material_proxies.nut\n"
		+ "S " + kStock + " scripts/vscripts/_utility.nut\n"
		+ "S " + kStock + " resource/ui/menu_mode_select.menu\n"
		+ "N " + kStock + " bin_delta/tier0.dll\n"
		+ "N " + Hash('c') + " bin_nexon/launcher.dll\n";
}

Manifest ParsedManifest()
{
	Manifest manifest;
	std::string error;
	if (!ParseManifest(ManifestText(), manifest, &error))
		std::printf("manifest fixture failed to parse: %s\n", error.c_str());
	return manifest;
}

void TestReasons()
{
	Check(ReasonText(0) == "none", "no reasons read as none");
	Check(ReasonText(kReasonAddons | kReasonNative) == "addons,native", "reasons keep canonical order");
	Check(ReasonText(kReasonNative | kReasonCore | kReasonAddons) == "addons,core,native", "all reasons");
	Check(ReasonNames(kReasonCore).size() == 1 && ReasonNames(kReasonCore)[0] == "core", "single reason name");
}

void TestPaths()
{
	Check(NormalizePath(".\\Scripts\\VScripts\\Client\\A.NUT") == "scripts/vscripts/client/a.nut", "path normalization");
	Check(NormalizePath("/scripts/x") == "scripts/x", "leading slash removed");
	Check(FrameContentPath("client/_cl_material_proxies.nut") == "scripts/vscripts/client/_cl_material_proxies.nut",
		"frame source maps under scripts/vscripts");
	Check(FrameContentPath("Client\\_CL_Material_Proxies.nut") == "scripts/vscripts/client/_cl_material_proxies.nut",
		"frame source casing and separators are normalized");
	Check(FrameContentPath("console").empty(), "console is not a file");
	Check(FrameContentPath("mcp").empty(), "mcp is not a file");
	Check(FrameContentPath("../escape.nut").empty(), "parent traversal is not a file");
	Check(FrameContentPath("c:/abs.nut").empty(), "drive paths are not frame sources");
	Check(IsTrackedContentPath("scripts/vscripts/_utility.nut"), "vscripts are tracked");
	Check(IsTrackedContentPath("resource/ui/x.res") && IsTrackedContentPath("resource/m.menu"), "res and menu are tracked");
	Check(!IsTrackedContentPath("resource/ui/x.txt"), "other resource files are not tracked");
	Check(!IsTrackedContentPath("scripts/weapons/mp_weapon_x.txt"), "weapon scripts are not tracked");
	Check(!IsTrackedContentPath("scripts/vscripts/"), "the directory itself is not tracked");
	Check(IsTrackedModulePath("bin_delta/tier0.dll") && IsTrackedModulePath("bin/server.dll"), "module paths");
	Check(!IsTrackedModulePath("bin_delta/tier0.pdb") && !IsTrackedModulePath("tools/curl.dll"), "non-modules");
}

void TestManifestParsing()
{
	Manifest manifest;
	std::string error;
	Check(ParseManifest(ManifestText(), manifest, &error), "valid manifest parses");
	Check(manifest.content.size() == 3 && manifest.modules.size() == 2, "entries are split by kind");
	Check(manifest.content["scripts/vscripts/_utility.nut"] == kStock, "content hash stored");

	const Manifest before = ParsedManifest();
	Manifest target = before;
	const auto rejects = [&](const std::string& text, const char* what) {
		std::string message;
		const bool ok = ParseManifest(text, target, &message);
		Check(!ok && !message.empty(), what);
		Check(target.content == before.content && target.modules == before.modules,
			"a rejected manifest leaves the target untouched");
	};
	const std::string header = std::string(kManifestHeader) + "\n";
	rejects("", "empty manifest");
	rejects("not a manifest\n", "wrong header");
	rejects(header, "no content entries");
	rejects(header + "S " + std::string(64, 'A') + " scripts/vscripts/a.nut\n", "uppercase hash");
	rejects(header + "S abc scripts/vscripts/a.nut\n", "short hash");
	rejects(header + "S " + kStock + " scripts/vscripts/A.nut\n", "uppercase path");
	rejects(header + "S " + kStock + " scripts/vscripts/../a.nut\n", "parent traversal");
	rejects(header + "S " + kStock + " scripts\\vscripts\\a.nut\n", "backslash path");
	rejects(header + "S " + kStock + " cfg/autoexec.cfg\n", "content outside the tracked roots");
	rejects(header + "N " + kStock + " tools/curl.dll\n", "module outside the tracked roots");
	rejects(header + "X " + kStock + " scripts/vscripts/a.nut\n", "unknown kind");
	rejects(header + "S " + kStock + " scripts/vscripts/a.nut\nS " + kEdited + " scripts/vscripts/a.nut\n", "duplicate path");

	Manifest crlf;
	Check(ParseManifest(std::string(kManifestHeader) + "\r\nS " + kStock + " scripts/vscripts/a.nut\r\n", crlf),
		"CRLF line endings parse");
}

void TestContentComparison()
{
	const Manifest manifest = ParsedManifest();
	Check(CompareContentFile(manifest, "scripts/vscripts/_utility.nut", kStock) == FileState::Stock, "identical hash is stock");
	Check(CompareContentFile(manifest, "scripts/vscripts/_utility.nut", kEdited) == FileState::Modified, "different hash is modified");
	Check(CompareContentFile(manifest, "scripts/vscripts/autorun/sv_pilotbots.nut", kStock) == FileState::Added, "unlisted file is added");
	Check(CompareContentFile(manifest, "scripts/vscripts/_utility.nut", "") == FileState::Missing, "no hash means missing");
}

struct Facts {
	std::map<std::string, std::string> core;     // relative path -> sha256 of the r1delta copy
	std::map<std::string, bool> addon;           // relative path -> an enabled addon provides it

	FrameFacts Make() const
	{
		FrameFacts facts;
		facts.addonProvides = [this](const std::string& path) {
			const auto it = addon.find(path);
			return it != addon.end() && it->second;
		};
		facts.hashCoreFile = [this](const std::string& path, std::string& hash) {
			const auto it = core.find(path);
			if (it == core.end())
				return false;
			hash = it->second;
			return true;
		};
		return facts;
	}
};

void TestFrameClassification()
{
	const Manifest manifest = ParsedManifest();
	Facts facts;
	facts.core["scripts/vscripts/client/_cl_material_proxies.nut"] = kStock;
	facts.core["scripts/vscripts/_utility.nut"] = kStock;
	const FrameFacts frameFacts = facts.Make();

	FrameVerdict verdict = ClassifyFrame(manifest, "client/_cl_material_proxies.nut", frameFacts);
	Check(!verdict.modded && verdict.reasons == 0, "an untouched stock file is not modded");

	verdict = ClassifyFrame(manifest, "Client\\_CL_Material_Proxies.nut", frameFacts);
	Check(!verdict.modded, "casing and separators do not matter");

	// A hand-installed mod replaced the stock file in place: invisible to path filtering.
	facts.core["scripts/vscripts/client/_cl_material_proxies.nut"] = kEdited;
	verdict = ClassifyFrame(manifest, "client/_cl_material_proxies.nut", facts.Make());
	Check(verdict.modded && verdict.reasons == kReasonCore, "a stock path with edited content is core-modded");

	// Files a mod added to the r1delta tree: Pilot Bots and r1bots.
	facts.core["scripts/vscripts/mp/_pilotbots_botmanager.nut"] = kStock;
	facts.core["scripts/vscripts/r1bots/_r1bots.nut"] = kStock;
	Check(ClassifyFrame(manifest, "mp/_pilotbots_botmanager.nut", facts.Make()).reasons == kReasonCore,
		"a file absent from the manifest is core-modded");
	Check(ClassifyFrame(manifest, "r1bots/_r1bots.nut", facts.Make()).modded, "r1bots is flagged");

	// An addon provides the frame's file.
	facts.addon["scripts/vscripts/_utility.nut"] = true;
	verdict = ClassifyFrame(manifest, "_utility.nut", facts.Make());
	Check(verdict.modded && verdict.reasons == kReasonAddons, "an addon-provided stock path is addons-modded");

	// Both at once.
	facts.core["scripts/vscripts/_utility.nut"] = kEdited;
	verdict = ClassifyFrame(manifest, "_utility.nut", facts.Make());
	Check(verdict.reasons == (kReasonAddons | kReasonCore), "addon and edited core copy combine");

	// Addon-only file (no r1delta copy at all).
	facts.addon["scripts/vscripts/autorun/cl_reticle_material_proxies.nut"] = true;
	verdict = ClassifyFrame(manifest, "autorun/cl_reticle_material_proxies.nut", facts.Make());
	Check(verdict.modded && verdict.reasons == kReasonAddons, "an addon-only autorun file is modded");

	// Not a file we can see anywhere (retail VPK content, console input).
	Check(!ClassifyFrame(manifest, "_codecallbacks.nut", facts.Make()).modded, "content only the VPK has is stock");
	Check(!ClassifyFrame(manifest, "console", facts.Make()).modded, "console frames are stock");
	Check(!ClassifyFrame(manifest, "", facts.Make()).modded, "empty source is stock");

	// No hashing available (manifest missing): only addons can be told.
	FrameFacts noCore = facts.Make();
	noCore.hashCoreFile = nullptr;
	verdict = ClassifyFrame(manifest, "client/_cl_material_proxies.nut", noCore);
	Check(!verdict.modded, "without core hashing a frame is judged by addons alone");
}

ModuleContext MakeContext(const Manifest* manifest, std::map<std::string, std::string>* disk)
{
	ModuleContext context;
	context.systemRoot = "c:/windows";
	context.gameRoot = "c:/games/titanfall";
	context.manifest = manifest;
	if (disk) {
		context.hashFile = [disk](const std::string& path, std::string& hash) {
			const auto it = disk->find(path);
			if (it == disk->end())
				return false;
			hash = it->second;
			return true;
		};
	}
	return context;
}

void TestModuleClassification()
{
	const Manifest manifest = ParsedManifest();
	std::map<std::string, std::string> disk;
	const std::string tier0 = "C:\\Games\\Titanfall\\app-3.0.5\\r1delta\\bin_delta\\tier0.dll";
	const std::string launcher = "C:\\Games\\Titanfall\\app-3.0.5\\r1delta\\bin_nexon\\launcher.dll";
	disk[tier0] = kStock;
	disk[launcher] = Hash('c');
	const ModuleContext context = MakeContext(&manifest, &disk);

	Check(ClassifyModule({ "KERNEL32.DLL", "C:\\Windows\\System32\\KERNEL32.DLL" }, context) == ModuleKind::System,
		"system modules are ignored");
	Check(ClassifyModule({ "tier0.dll", tier0 }, context) == ModuleKind::Shipped, "release tier0 is shipped");
	Check(ClassifyModule({ "launcher.dll", launcher }, context) == ModuleKind::Shipped, "same name, other directory, own hash");

	// Pilot Bots patches the official tier0 to import pilotbots.dll.
	disk[tier0] = kEdited;
	Check(ClassifyModule({ "tier0.dll", tier0 }, context) == ModuleKind::Modified, "tier0 with changed bytes is modified");
	Check(IsNativeModdingKind(ModuleKind::Modified), "a modified shipped module is native modding");

	const std::string pilotBots = "C:\\Games\\Titanfall\\app-3.0.5\\r1delta\\bin_delta\\pilotbots.dll";
	Check(ClassifyModule({ "pilotbots.dll", pilotBots }, context) == ModuleKind::ForeignGame,
		"pilotbots.dll in the r1delta tree is foreign");
	Check(IsNativeModdingKind(ModuleKind::ForeignGame), "a foreign game module is native modding");
	Check(ClassifyModule({ "pilotbots.dll", "C:\\Games\\Titanfall\\pilotbots.dll" }, context) == ModuleKind::ForeignGame,
		"pilotbots.dll beside the exe is foreign");

	Check(ClassifyModule({ "engine.dll", "C:\\Games\\Titanfall\\bin\\x64_retail\\engine.dll" }, context) == ModuleKind::Retail,
		"retail modules are known");
	Check(ClassifyModule({ "ENGINE.DLL", "C:\\Games\\Titanfall\\bin\\x64_retail\\ENGINE.DLL" }, context) == ModuleKind::Retail,
		"retail names are case-insensitive");
	Check(ClassifyModule({ "engine.dll", "C:\\Games\\Titanfall\\app-3.0.5\\r1delta\\bin_delta\\engine.dll" }, context)
			== ModuleKind::ForeignGame,
		"a retail-named file inside the r1delta tree that the release does not ship is foreign");

	const ModuleKind overlay = ClassifyModule(
		{ "gameoverlayrenderer64.dll", "C:\\Program Files (x86)\\Steam\\gameoverlayrenderer64.dll" }, context);
	Check(overlay == ModuleKind::ForeignOther, "software outside the game folder is foreign but not native modding");
	Check(!IsNativeModdingKind(overlay), "overlays do not make the game modded");

	// Judging without hashing (crash reports) trusts the manifest's names and locations.
	Check(ClassifyModule({ "tier0.dll", tier0 }, MakeContext(&manifest, nullptr)) == ModuleKind::Shipped,
		"a null hash probe skips byte comparison");
	std::map<std::string, std::string> empty;
	Check(ClassifyModule({ "tier0.dll", tier0 }, MakeContext(&manifest, &empty)) == ModuleKind::Unverifiable,
		"an unreadable shipped module is unverifiable");

	// No manifest: only what needs no manifest can be said.
	const ModuleContext noManifest = MakeContext(nullptr, &disk);
	Check(ClassifyModule({ "engine.dll", "C:\\Games\\Titanfall\\bin\\x64_retail\\engine.dll" }, noManifest) == ModuleKind::Retail,
		"retail modules are known without a manifest");
	Check(ClassifyModule({ "pilotbots.dll", pilotBots }, noManifest) == ModuleKind::Unverifiable,
		"unknown modules are unverifiable without a manifest");
	Check(!IsNativeModdingKind(ModuleKind::Unverifiable), "unverifiable modules do not claim modding");
}

ReportBody SampleBody()
{
	ReportBody body;
	body.vmContext = "CLIENT";
	body.processMode = "CLIENT";
	body.cascade = false;
	body.error = "boom";
	body.frames.push_back({ "f", "a.nut", 3, true });
	body.frames.push_back({ "g", "b.nut", 9, false });
	body.moddedReasons = kReasonAddons | kReasonCore;
	return body;
}

void TestPayloads()
{
	ReportBody body = SampleBody();
	// Byte-for-byte what every deployed service already accepts: the modded fields must not leak in.
	Check(BuildPayload(body, ScriptErrorTelemetry::kSchemaLegacy)
			== "{\"cascade\":false,\"error\":\"boom\",\"frames\":[{\"function\":\"f\",\"line\":3,\"source\":\"a.nut\"},"
			   "{\"function\":\"g\",\"line\":9,\"source\":\"b.nut\"}],\"processMode\":\"CLIENT\",\"schema\":1,"
			   "\"vmContext\":\"CLIENT\"}",
		"schema 1 payload is unchanged and carries no modded data");

	Check(BuildPayload(body, ScriptErrorTelemetry::kSchemaModded)
			== "{\"cascade\":false,\"error\":\"boom\",\"frames\":[{\"function\":\"f\",\"line\":3,\"modded\":true,"
			   "\"source\":\"a.nut\"},{\"function\":\"g\",\"line\":9,\"modded\":false,\"source\":\"b.nut\"}],"
			   "\"modded\":[\"addons\",\"core\"],\"processMode\":\"CLIENT\",\"schema\":2,\"vmContext\":\"CLIENT\"}",
		"schema 2 payload lists reasons and per-frame flags");

	ReportBody stock = SampleBody();
	stock.moddedReasons = 0;
	stock.frames[0].modded = false;
	Check(BuildPayload(stock, ScriptErrorTelemetry::kSchemaModded).find("\"modded\":[]") != std::string::npos,
		"an unmodded game sends an empty reason list");

	// Oversized reports shed frames first, then error text, for both schemas.
	for (const int schema : { ScriptErrorTelemetry::kSchemaLegacy, ScriptErrorTelemetry::kSchemaModded }) {
		ReportBody large = SampleBody();
		large.frames.clear();
		for (int i = 0; i < 32; ++i)
			large.frames.push_back({ std::string(120, 'f'), std::string(180, 's'), i, i % 2 == 0 });
		large.error.assign(ScriptErrorTelemetry::kMaxPayloadLength, 'e');
		const std::string payload = BuildPayload(large, schema);
		Check(!payload.empty() && payload.size() <= ScriptErrorTelemetry::kMaxPayloadLength, "oversized payload is trimmed to fit");
		Check(large.error.size() < ScriptErrorTelemetry::kMaxPayloadLength, "the error text was trimmed");
	}
}

void TestSchemaSelection()
{
	Check(SelectSchema("{\"status\":\"ok\",\"schemas\":[1,2]}") == 2, "service advertising schema 2");
	Check(SelectSchema("{\"schemas\":[2,1],\"status\":\"ok\"}") == 2, "order does not matter");
	Check(SelectSchema("{\"status\":\"ok\"}") == 1, "a service that predates schema 2 stays on schema 1");
	Check(SelectSchema("{\"status\":\"ok\",\"schemas\":[1]}") == 1, "service advertising only schema 1");
	Check(SelectSchema("{\"status\":\"ok\",\"schemas\":[\"2\"]}") == 1, "a string is not a schema number");
	Check(SelectSchema("{\"status\":\"ok\",\"schemas\":2}") == 1, "schemas must be an array");
	Check(SelectSchema("{\"status\":\"down\",\"schemas\":[1,2]}") == 1, "an unhealthy service is not trusted");
	Check(SelectSchema("{\"schemas\":[1,2]}") == 1, "status is required");
	Check(SelectSchema("not json") == 1, "garbage selects schema 1");
	Check(SelectSchema("") == 1, "an empty body selects schema 1");
	Check(SelectSchema("[1,2]") == 1, "a non-object selects schema 1");
}

} // namespace

int main()
{
	TestReasons();
	TestPaths();
	TestManifestParsing();
	TestContentComparison();
	TestFrameClassification();
	TestModuleClassification();
	TestPayloads();
	TestSchemaSelection();

	if (g_failures != 0) {
		std::printf("%d failure(s)\n", g_failures);
		return 1;
	}
	std::printf("script modding tests passed\n");
	return 0;
}
