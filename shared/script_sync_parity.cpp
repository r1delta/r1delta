#include "script_sync_parity.h"
#include "core.h"
#include "cvar.h"
#include "load.h"
#include "logging.h"
#include "parity_common.h"
#include "persistentdata.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

namespace {

enum Category { kRemoteFunctions, kServerVars, kEntityVars, kStrings, kCategoryCount };
constexpr const char* kCategoryLabels[kCategoryCount] = {
	"remote functions", "server vars", "entity vars", "registered strings" };

using Lists = std::array<std::vector<std::string>, kCategoryCount>;

// Retail's remote-function table holds 256 entries; the script tables are of
// the same order. Bound what a server can make a client hold.
constexpr std::size_t kMaxNamesPerCategory = 4096;

constexpr char kEnforceConVar[] = "delta_scriptsync_check";

// client.dll (R1 2015): the "retry" literal used only by the three
// RemoteFunctionCallsChecksum mismatch paths (CHudScriptElements +0x423440,
// +0x427010, +0x427CB0), each `lea rdx, [rip+disp32]`.
constexpr std::uintptr_t kRetryStringRva = 0x916328;
constexpr std::uintptr_t kRetryLeaRvas[] = { 0x42346B, 0x427304, 0x427DED };

Lists SplitLists(HSQUIRRELVM v) {
	Lists lists;
	for (int category = 0; category < kCategoryCount; ++category) {
		const char* text = nullptr;
		if (SQ_FAILED(sq_getstring(v, 2 + category, &text)) || !text)
			continue;
		for (const char* c = text; *c;) {
			const char* end = strchr(c, '\n');
			if (!end)
				end = c + strlen(c);
			if (end > c && lists[category].size() < kMaxNamesPerCategory)
				lists[category].emplace_back(c, end);
			c = *end ? end + 1 : end;
		}
	}
	return lists;
}

// --- Server ------------------------------------------------------------------

Lists g_serverLists;
bool g_serverPublished = false;

// --- Client ------------------------------------------------------------------

struct ClientState {
	Lists local;
	bool localReady = false;

	Lists server;
	bool serverBegun = false;
	bool serverEnded = false;
	bool serverHasNoLists = false;
	bool enforce = true;

	bool verdictDone = false;
	bool verdictMatched = false;
	bool retailMismatch = false;
	bool retailHandled = false;
};
ClientState g_client;

void PrintNames(const char* category, const char* side, const std::vector<std::string>& names) {
	if (names.empty())
		return;
	Parity::Report(true, "R1Delta script sync: %s only %s (%zu):\n", category, side, names.size());
	for (const std::string& name : names)
		Parity::Report(false, "    %s\n", name.c_str());
}

// Retail's mismatch, with no named verdict to explain it.
void RetailMismatch() {
	if (g_client.retailHandled)
		return;
	g_client.retailHandled = true;
	const bool enforce = !g_client.serverBegun || g_client.enforce;
	if (!enforce) {
		Parity::Report(true, "R1Delta script sync: the server uses different script remotes (allowed by the server)\n");
		return;
	}
	Parity::DisconnectWithReason("Server uses different script remotes, disconnecting");
}

void TryVerdict() {
	ClientState& c = g_client;
	if (c.verdictDone || !c.localReady || !c.serverEnded)
		return;
	c.verdictDone = true;

	bool matched = true;
	std::array<bool, kCategoryCount> differs = {};
	std::array<std::vector<std::string>, kCategoryCount> clientOnly;
	std::array<std::vector<std::string>, kCategoryCount> serverOnly;
	std::size_t namedLists = 0;
	for (int category = 0; category < kCategoryCount; ++category) {
		const std::vector<std::string>& mine = c.local[category];
		const std::vector<std::string>& theirs = c.server[category];
		if (mine == theirs)
			continue;
		matched = false;
		differs[category] = true;
		std::vector<std::string> mineSorted = mine;
		std::vector<std::string> theirsSorted = theirs;
		std::sort(mineSorted.begin(), mineSorted.end());
		std::sort(theirsSorted.begin(), theirsSorted.end());
		std::set_difference(mineSorted.begin(), mineSorted.end(), theirsSorted.begin(), theirsSorted.end(),
			std::back_inserter(clientOnly[category]));
		std::set_difference(theirsSorted.begin(), theirsSorted.end(), mineSorted.begin(), mineSorted.end(),
			std::back_inserter(serverOnly[category]));
		namedLists += !clientOnly[category].empty() + !serverOnly[category].empty();
	}

	// Every differing category gets a share of the 256-byte reason.
	const std::size_t budget = namedLists ? (std::max<std::size_t>)(24, 150 / namedLists) : 0;
	std::string detail;
	for (int category = 0; category < kCategoryCount; ++category) {
		if (!differs[category])
			continue;
		if (!detail.empty())
			detail += "; ";
		detail += kCategoryLabels[category];
		detail += ": ";
		if (clientOnly[category].empty() && serverOnly[category].empty()) {
			detail += "same " + std::to_string(c.local[category].size()) + " in a different order";
			continue;
		}
		if (!clientOnly[category].empty())
			detail += "only you have " + Parity::QuotedList(clientOnly[category], budget);
		if (!serverOnly[category].empty()) {
			if (!clientOnly[category].empty())
				detail += ", ";
			detail += "only the server has " + Parity::QuotedList(serverOnly[category], budget);
		}
	}

	c.verdictMatched = matched;
	if (matched) {
		Parity::Report(false, "R1Delta script sync: matches the server (%zu remote functions, %zu server vars, %zu entity vars, %zu registered strings)\n",
			c.local[kRemoteFunctions].size(), c.local[kServerVars].size(), c.local[kEntityVars].size(), c.local[kStrings].size());
		if (c.retailMismatch)
			RetailMismatch();
		return;
	}

	char reason[Parity::kMaxReason + 1] = {};
	_snprintf_s(reason, sizeof(reason), _TRUNCATE, "%s (%s)",
		c.enforce ? "Client/server scripts do not match, disconnecting" : "Client/server scripts do not match",
		detail.c_str());
	Parity::Report(true, "R1Delta script sync: %s\n", reason);
	for (int category = 0; category < kCategoryCount; ++category) {
		PrintNames(kCategoryLabels[category], "on this client", clientOnly[category]);
		PrintNames(kCategoryLabels[category], "on the server", serverOnly[category]);
	}
	// A named verdict also answers retail's checksum mismatch.
	c.retailHandled = true;
	if (c.enforce)
		Parity::DisconnectWithReason(reason);
}

void ScriptSyncCommand(const CCommand& args) {
	if (args.ArgC() < 2)
		return;
	ClientState& c = g_client;
	const char* verb = args.Arg(1);
	if (strcmp(verb, "begin") == 0 && args.ArgC() == 3) {
		c.server = {};
		c.serverBegun = true;
		c.serverEnded = false;
		c.serverHasNoLists = false;
		c.enforce = atoi(args.Arg(2)) != 0;
		c.verdictDone = false;
	}
	else if (strcmp(verb, "names") == 0 && args.ArgC() == 4 && c.serverBegun && !c.serverEnded) {
		const int category = atoi(args.Arg(2));
		if (category < 0 || category >= kCategoryCount)
			return;
		std::vector<std::string>& list = c.server[category];
		for (std::string& name : Parity::DecodeNameList(args.Arg(3), kMaxNamesPerCategory)) {
			if (list.size() < kMaxNamesPerCategory)
				list.push_back(std::move(name));
		}
	}
	else if (strcmp(verb, "end") == 0 && args.ArgC() == 2 + kCategoryCount && c.serverBegun) {
		for (int category = 0; category < kCategoryCount; ++category) {
			const std::size_t expected = static_cast<std::size_t>(strtoul(args.Arg(2 + category), nullptr, 10));
			if (c.server[category].size() != expected) {
				Parity::Report(true, "R1Delta script sync: received %zu of %zu server %s; not comparing\n",
					c.server[category].size(), expected, kCategoryLabels[category]);
				c.serverBegun = false;
				return;
			}
		}
		c.serverEnded = true;
		TryVerdict();
	}
	else if (strcmp(verb, "none") == 0) {
		c.serverHasNoLists = true;
	}
}

void RetailMismatchCommand(const CCommand&) {
	ClientState& c = g_client;
	if (c.verdictDone) {
		if (c.verdictMatched)
			RetailMismatch();
		return;
	}
	if (!c.serverBegun || c.serverHasNoLists) {
		// This server does not send its lists; nothing can name the difference.
		RetailMismatch();
		return;
	}
	// The named verdict is pending; it decides once both lists are known.
	c.retailMismatch = true;
}

}

SQInteger Script_ScriptSyncPublishServer(HSQUIRRELVM v) {
	g_serverLists = SplitLists(v);
	g_serverPublished = true;
	return 0;
}

SQInteger Script_ScriptSyncPublishClient(HSQUIRRELVM v) {
	g_client.local = SplitLists(v);
	g_client.localReady = true;
	TryVerdict();
	return 0;
}

void ScriptSyncParity_ServerPush(int playerSlot) {
	if (playerSlot < 0)
		return;
	char command[CCommand::COMMAND_MAX_LENGTH];
	if (!g_serverPublished) {
		_snprintf_s(command, sizeof(command), _TRUNCATE, "%s none", kScriptSyncCommand);
		SendServerCommandToClient(playerSlot, command);
		return;
	}
	_snprintf_s(command, sizeof(command), _TRUNCATE, "%s begin %d", kScriptSyncCommand,
		Parity::ServerConVarEnabled(kEnforceConVar) ? 1 : 0);
	SendServerCommandToClient(playerSlot, command);
	std::vector<const char*> names;
	for (int category = 0; category < kCategoryCount; ++category) {
		names.clear();
		for (const std::string& name : g_serverLists[category])
			names.push_back(name.c_str());
		char prefix[64];
		_snprintf_s(prefix, sizeof(prefix), _TRUNCATE, "%s names %d", kScriptSyncCommand, category);
		Parity::SendEncodedNames(playerSlot, prefix, names);
	}
	_snprintf_s(command, sizeof(command), _TRUNCATE, "%s end %zu %zu %zu %zu", kScriptSyncCommand,
		g_serverLists[kRemoteFunctions].size(), g_serverLists[kServerVars].size(),
		g_serverLists[kEntityVars].size(), g_serverLists[kStrings].size());
	SendServerCommandToClient(playerSlot, command);
}

void ScriptSyncParity_ClientSignon() {
	// A new map's client VM publishes its own lists after this.
	g_client = ClientState{};
}

void ScriptSyncParity_RegisterClient() {
	RegisterConCommand(kScriptSyncCommand, ScriptSyncCommand,
		"Server reply in the R1Delta script-sync parity check", FCVAR_SERVER_CAN_EXECUTE);
	RegisterConCommand(kScriptSyncRetailMismatchCommand, RetailMismatchCommand,
		"client.dll remote-function checksum mismatch (replaces retail's retry)", FCVAR_CLIENTCMD_CAN_EXECUTE);
}

bool ScriptSyncParity_InstallClientRetryRedirect(std::uintptr_t clientBase) {
	if (!clientBase)
		return false;
	auto* retry = reinterpret_cast<char*>(clientBase + kRetryStringRva);
	if (memcmp(retry, "retry\0", 6) != 0) {
		Warning("R1Delta script sync: client.dll remote-checksum retry string did not match; leaving retail retry\n");
		return false;
	}
	for (std::uintptr_t rva : kRetryLeaRvas) {
		const auto* at = reinterpret_cast<const unsigned char*>(clientBase + rva);
		const std::int32_t disp = *reinterpret_cast<const std::int32_t*>(at + 3);
		if (at[0] != 0x48 || at[1] != 0x8D || at[2] != 0x15
			|| clientBase + rva + 7 + static_cast<std::intptr_t>(disp) != clientBase + kRetryStringRva) {
			Warning("R1Delta script sync: client.dll remote-checksum retry site +0x%llX did not match; leaving retail retry\n",
				static_cast<unsigned long long>(rva));
			return false;
		}
	}
	static_assert(sizeof(kScriptSyncRetailMismatchCommand) == 6, "replacement must fit the retail literal");
	DWORD previous = 0;
	if (!VirtualProtect(retry, sizeof(kScriptSyncRetailMismatchCommand), PAGE_READWRITE, &previous)) {
		Warning("R1Delta script sync: cannot unprotect client.dll retry string (%lu)\n", GetLastError());
		return false;
	}
	memcpy(retry, kScriptSyncRetailMismatchCommand, sizeof(kScriptSyncRetailMismatchCommand));
	DWORD ignored = 0;
	VirtualProtect(retry, sizeof(kScriptSyncRetailMismatchCommand), previous, &ignored);
	return true;
}
