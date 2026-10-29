#include "soundscript_parity.h"
#include "core.h"
#include "cvar.h"
#include "load.h"
#include "logging.h"
#include "parity_common.h"
#include "persistentdata.h"
#include "legacy_ds_sound_tables.h"
#include "audio_cache.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace {

// Alias records are 512 bytes in every engine build; the interned name pointer
// lives at +0x1E0 (engine.dll sub_1800089B0, engine_ds sub_180005020,
// engine_r1o sub_180043900 all store it there).
constexpr std::size_t kAliasRecordSize = 512;
constexpr std::size_t kAliasNameOffset = 0x1E0;

// Alias table, loaded alias count and sound-script CRC globals per engine build.
struct TableLayout {
	std::uintptr_t records;
	std::uintptr_t count;
	std::uintptr_t crc;
};
constexpr TableLayout kEngineLayout{ 0x10FEBC0, 0x1A0DBC4, 0x1A0DBC0 };    // engine.dll (2015)
constexpr TableLayout kEngineDsLayout{ 0xD52BE0, 0x15713E4, 0x15713E0 };   // engine_ds.dll (2014)
constexpr TableLayout kEngineR1OLayout{ 0x6F9110, 0x18836F4, 0x18836F0 };  // engine_r1o.dll

constexpr int kHashesPerChunk = kSoundScriptBuckets / kSoundScriptUploadChunks;
static_assert(kHashesPerChunk * 8 < sizeof(SoundScriptUploadChunk::value), "bucket chunk does not fit a convar value");

// A real content mismatch (a mod adding or replacing a few aliases) touches a
// handful of buckets; beyond this the verdict lists counts only.
constexpr int kMaxNamedBuckets = 8;
constexpr std::size_t kMaxNamesPerBucket = 1024;

struct LocalTable {
	const unsigned char* records = nullptr;
	int count = 0;
	std::uint32_t engineCrc = 0;

	const char* NameAt(int index) const {
		return *reinterpret_cast<const char* const*>(
			records + static_cast<std::size_t>(index) * kAliasRecordSize + kAliasNameOffset);
	}
};

bool ReadLayout(const TableLayout& layout, std::uintptr_t base, LocalTable& out) {
	if (!base)
		return false;
	out.records = reinterpret_cast<const unsigned char*>(base + layout.records);
	out.count = *reinterpret_cast<const int*>(base + layout.count);
	out.engineCrc = *reinterpret_cast<const std::uint32_t*>(base + layout.crc);
	return out.count > 0;
}

// The table whose indices this process puts on, or reads off, the wire.
bool ReadLocalTable(LocalTable& out) {
	switch (GetR1DeltaEngineMode()) {
	case R1DeltaEngineMode::R1OFakeDedicated2017:
		return ReadLayout(kEngineR1OLayout,
			G_engine_r1o ? G_engine_r1o : reinterpret_cast<std::uintptr_t>(GetModuleHandleA("engine_r1o.dll")), out);
	case R1DeltaEngineMode::Dedicated2014:
		if (!ReadLayout(kEngineDsLayout, G_engine_ds, out))
			return false;
		if (const void* relocated = GetLegacyDSAliasRecords())
			out.records = static_cast<const unsigned char*>(relocated);
		return true;
	case R1DeltaEngineMode::Client2015:
		if (!ReadLayout(kEngineLayout, G_engine, out))
			return false;
		// The generated R2 aliases trail the table and are excluded (audio_cache.cpp): they exist only when the R2 install does.
		if (const int hidden = static_cast<int>(R1AudioHiddenTrailingAliases()); hidden > 0 && hidden < out.count)
			out.count -= hidden;
		return true;
	}
	return false;
}

unsigned char FoldCase(unsigned char c) {
	return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c - 'A' + 'a') : c;
}

// FNV-1a over the case-folded name: alias lookup is case-insensitive.
std::uint32_t HashName(const char* name) {
	std::uint32_t hash = 2166136261u;
	for (const unsigned char* c = reinterpret_cast<const unsigned char*>(name); *c; ++c) {
		hash ^= FoldCase(*c);
		hash *= 16777619u;
	}
	return hash;
}

int BucketOf(const char* name) {
	return static_cast<int>(HashName(name) & (kSoundScriptBuckets - 1));
}

struct Fingerprint {
	int aliases = 0;
	std::uint32_t engineCrc = 0;
	std::uint32_t mapping = 0;  // index -> name; what the wire depends on
	std::uint32_t set = 0;      // name multiset; order independent
};

// Buckets hold the sum of their names' hashes: independent of load order and
// duplicate aware, so a pure index shift leaves every bucket unchanged while an
// added, removed or renamed alias changes exactly its own bucket.
bool Summarize(const LocalTable& table, Fingerprint& fingerprint, std::uint32_t (&buckets)[kSoundScriptBuckets]) {
	std::memset(buckets, 0, sizeof(buckets));
	std::uint32_t mapping = 2166136261u;
	for (int i = 0; i < table.count; ++i) {
		const char* name = table.NameAt(i);
		if (!name)
			return false;
		const std::uint32_t nameHash = HashName(name);
		mapping = (mapping ^ static_cast<std::uint32_t>(i)) * 16777619u;
		mapping = (mapping ^ nameHash) * 16777619u;
		buckets[nameHash & (kSoundScriptBuckets - 1)] += nameHash;
	}
	std::uint32_t set = 2166136261u;
	for (std::uint32_t bucket : buckets)
		set = (set ^ bucket) * 16777619u;
	fingerprint.aliases = table.count;
	fingerprint.engineCrc = table.engineCrc;
	fingerprint.mapping = mapping;
	fingerprint.set = set;
	return true;
}

void FormatFingerprint(const Fingerprint& value, char* out, std::size_t outSize) {
	_snprintf_s(out, outSize, _TRUNCATE, "%d:%08x:%08x:%08x", value.aliases, value.engineCrc, value.mapping, value.set);
}

bool ParseFingerprint(const char* text, Fingerprint& out) {
	Fingerprint value;
	if (!text || sscanf_s(text, "%d:%x:%x:%x", &value.aliases, &value.engineCrc, &value.mapping, &value.set) != 4
		|| value.aliases <= 0)
		return false;
	out = value;
	return true;
}

// The engine CRC also hashes which script file declared each alias; playback
// only depends on the index -> name mapping.
bool SameMapping(const Fingerprint& a, const Fingerprint& b) {
	return a.aliases == b.aliases && a.mapping == b.mapping;
}

int HexDigit(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

bool NameLess(const std::string& a, const std::string& b) {
	return _stricmp(a.c_str(), b.c_str()) < 0;
}

// --- Client ------------------------------------------------------------------

struct ClientState {
	ConVarR1* marker = nullptr;
	bool prepared = false;
	SoundScriptUploadChunk chunks[kSoundScriptUploadChunks] = {};
	bool awaitingVerdict = false;
	std::vector<std::pair<int, std::string>> serverNames;
};
ClientState g_client;

void PrintNames(const char* heading, const std::vector<std::string>& names) {
	if (names.empty())
		return;
	Parity::Report(true, "%s (%zu):\n", heading, names.size());
	for (const std::string& name : names)
		Parity::Report(false, "    %s\n", name.c_str());
}

void ClientVerdict(const CCommand& args) {
	g_client.awaitingVerdict = false;
	std::vector<std::pair<int, std::string>> received = std::move(g_client.serverNames);
	g_client.serverNames.clear();

	const bool enforce = atoi(args.Arg(2)) != 0;
	Fingerprint server;
	if (!ParseFingerprint(args.Arg(3), server)) {
		Warning("R1Delta sound scripts: the server sent an unreadable verdict \"%s\"\n", args.Arg(3));
		return;
	}
	const int differingBuckets = atoi(args.Arg(4));
	bool listed[kSoundScriptBuckets] = {};
	int listedCount = 0;
	for (const char* c = args.Arg(5); *c;) {
		char* end = nullptr;
		const long bucket = strtol(c, &end, 10);
		if (end == c)
			break;
		if (bucket >= 0 && bucket < kSoundScriptBuckets && !listed[bucket]) {
			listed[bucket] = true;
			++listedCount;
		}
		c = *end == ',' ? end + 1 : end;
	}

	LocalTable table;
	Fingerprint local;
	std::uint32_t buckets[kSoundScriptBuckets];
	if (!ReadLocalTable(table) || !Summarize(table, local, buckets)) {
		Warning("R1Delta sound scripts: cannot read the local alias table to compare with the server\n");
		return;
	}

	// Equal names always share a bucket, so one multiset difference over every
	// listed bucket names both sides exactly.
	std::vector<std::string> mine;
	for (int i = 0; i < table.count; ++i) {
		const char* name = table.NameAt(i);
		if (listed[BucketOf(name)])
			mine.emplace_back(name);
	}
	std::vector<std::string> theirs;
	theirs.reserve(received.size());
	for (auto& entry : received) {
		if (listed[entry.first])
			theirs.push_back(std::move(entry.second));
	}
	std::sort(mine.begin(), mine.end(), NameLess);
	std::sort(theirs.begin(), theirs.end(), NameLess);
	std::vector<std::string> clientOnly;
	std::vector<std::string> serverOnly;
	std::set_difference(mine.begin(), mine.end(), theirs.begin(), theirs.end(), std::back_inserter(clientOnly), NameLess);
	std::set_difference(theirs.begin(), theirs.end(), mine.begin(), mine.end(), std::back_inserter(serverOnly), NameLess);

	const char* lead = enforce ? "Client/server sounds do not match, disconnecting" : "Client/server sounds do not match";
	char reason[Parity::kMaxReason + 1] = {};
	if (!clientOnly.empty() || !serverOnly.empty()) {
		const std::size_t budget = clientOnly.empty() || serverOnly.empty() ? 150 : 70;
		std::string detail;
		if (!clientOnly.empty())
			detail += "you have " + Parity::QuotedList(clientOnly, budget) + " that the server doesn't have";
		if (!serverOnly.empty()) {
			if (!detail.empty())
				detail += "; ";
			detail += "the server has " + Parity::QuotedList(serverOnly, budget) + " that your client doesn't have";
		}
		_snprintf_s(reason, sizeof(reason), _TRUNCATE, "%s (%s)", lead, detail.c_str());
	}
	else if (server.set == local.set) {
		_snprintf_s(reason, sizeof(reason), _TRUNCATE,
			"%s (same %d sounds, loaded in a different order; remove sound script mods the server does not run)",
			lead, local.aliases);
	}
	else {
		_snprintf_s(reason, sizeof(reason), _TRUNCATE, "%s (you have %d sounds, the server has %d)",
			lead, local.aliases, server.aliases);
	}

	Parity::Report(true, "R1Delta sound scripts: %s\n", reason);
	Parity::Report(true, "R1Delta sound scripts: client %d aliases (crc %08x), server %d aliases (crc %08x)\n",
		local.aliases, local.engineCrc, server.aliases, server.engineCrc);
	PrintNames("Sounds only on this client", clientOnly);
	PrintNames("Sounds only on the server", serverOnly);
	if (differingBuckets > listedCount)
		Warning("R1Delta sound scripts: %d of %d differing name groups were not listed\n",
			differingBuckets - listedCount, differingBuckets);

	if (enforce)
		Parity::DisconnectWithReason(reason);
}

void SoundScriptCommand(const CCommand& args) {
	if (args.ArgC() < 2 || !g_client.awaitingVerdict)
		return;
	const char* verb = args.Arg(1);
	if (strcmp(verb, "names") == 0 && args.ArgC() == 4) {
		const int bucket = atoi(args.Arg(2));
		if (bucket < 0 || bucket >= kSoundScriptBuckets)
			return;
		for (std::string& name : Parity::DecodeNameList(args.Arg(3), kMaxNamesPerBucket)) {
			if (g_client.serverNames.size() < kMaxNamedBuckets * kMaxNamesPerBucket)
				g_client.serverNames.emplace_back(bucket, std::move(name));
		}
	}
	else if (strcmp(verb, "done") == 0 && args.ArgC() == 6) {
		ClientVerdict(args);
	}
}

}

bool SoundScriptUpload::Accept(const char* name, const char* value) {
	constexpr std::size_t prefixLength = sizeof(kSoundScriptMarkerConVar) - 1;
	if (!name || strncmp(name, kSoundScriptMarkerConVar, prefixLength) != 0)
		return false;
	const char* suffix = name + prefixLength;
	if (!*suffix) {
		if (value && *value) {
			strncpy_s(fingerprint, sizeof(fingerprint), value, _TRUNCATE);
			present = true;
		}
		return true;
	}
	if (suffix[0] < '0' || suffix[0] >= '0' + kSoundScriptUploadChunks || suffix[1]
		|| !value || strlen(value) != kHashesPerChunk * 8)
		return true;
	const int chunk = suffix[0] - '0';
	std::uint32_t parsed[kHashesPerChunk];
	for (int i = 0; i < kHashesPerChunk; ++i) {
		std::uint32_t word = 0;
		for (int digit = 0; digit < 8; ++digit) {
			const int nibble = HexDigit(value[i * 8 + digit]);
			if (nibble < 0)
				return true;
			word = (word << 4) | static_cast<std::uint32_t>(nibble);
		}
		parsed[i] = word;
	}
	std::memcpy(buckets + chunk * kHashesPerChunk, parsed, sizeof(parsed));
	chunkMask |= 1u << chunk;
	return true;
}

void SoundScriptParity_ServerCheck(int playerSlot, const SoundScriptUpload& upload) {
	if (!upload.present)
		return;
	Fingerprint remote;
	if (!ParseFingerprint(upload.fingerprint, remote)) {
		Warning("R1Delta sound scripts: client in slot %d sent an unreadable fingerprint \"%s\"\n",
			playerSlot, upload.fingerprint);
		return;
	}
	LocalTable table;
	Fingerprint local;
	std::uint32_t buckets[kSoundScriptBuckets];
	if (!ReadLocalTable(table) || !Summarize(table, local, buckets)) {
		Warning("R1Delta sound scripts: this server's alias table is unreadable; parity check skipped\n");
		return;
	}
	if (SameMapping(local, remote))
		return;
	if (playerSlot < 0) {
		Warning("R1Delta sound scripts: a client's sounds do not match, but its player slot is unknown\n");
		return;
	}

	int slotOfBucket[kSoundScriptBuckets];
	std::fill(std::begin(slotOfBucket), std::end(slotOfBucket), -1);
	std::vector<int> named;
	int differing = 0;
	if (upload.HasAllBuckets()) {
		for (int bucket = 0; bucket < kSoundScriptBuckets; ++bucket) {
			if (buckets[bucket] == upload.buckets[bucket])
				continue;
			++differing;
			if (static_cast<int>(named.size()) < kMaxNamedBuckets) {
				slotOfBucket[bucket] = static_cast<int>(named.size());
				named.push_back(bucket);
			}
		}
	}
	std::vector<std::vector<const char*>> names(named.size());
	for (int i = 0; i < table.count; ++i) {
		const char* name = table.NameAt(i);
		const int slot = slotOfBucket[BucketOf(name)];
		if (slot >= 0 && names[slot].size() <= kMaxNamesPerBucket)
			names[slot].push_back(name);
	}

	std::string listed;
	for (std::size_t i = 0; i < named.size(); ++i) {
		if (names[i].size() > kMaxNamesPerBucket)
			continue;
		char prefix[64];
		_snprintf_s(prefix, sizeof(prefix), _TRUNCATE, "%s names %d", kSoundScriptCommand, named[i]);
		Parity::SendEncodedNames(playerSlot, prefix, names[i]);
		if (!listed.empty())
			listed.push_back(',');
		listed += std::to_string(named[i]);
	}

	const bool enforce = Parity::ServerConVarEnabled("delta_soundtable_check");
	char fingerprint[64];
	FormatFingerprint(local, fingerprint, sizeof(fingerprint));
	char command[CCommand::COMMAND_MAX_LENGTH];
	_snprintf_s(command, sizeof(command), _TRUNCATE, "%s done %d \"%s\" %d \"%s\"",
		kSoundScriptCommand, enforce ? 1 : 0, fingerprint, differing, listed.c_str());
	SendServerCommandToClient(playerSlot, command);

	Parity::Report(true, "R1Delta sound scripts: client in slot %d does not match this server (client %d aliases, server %d; %s)%s\n",
		playerSlot, remote.aliases, local.aliases,
		remote.set == local.set ? "same aliases in a different order"
			: (std::to_string(differing) + " name group(s) differ").c_str(),
		enforce ? "; the client will disconnect" : "");
}

void SoundScriptParity_RegisterClient() {
	// Host_BuildUserInfoUpdateMessage puts only userinfo convars of split-screen
	// slot -1 into the signon NET_SetConVar; FCVAR_PERSIST maps a convar there
	// (CConVar__GetSplitScreenPlayerSlot), as it does for persistent data.
	g_client.marker = RegisterConVar(kSoundScriptMarkerConVar, "", FCVAR_USERINFO | FCVAR_PERSIST | FCVAR_DONTRECORD,
		"R1Delta sound-script fingerprint sent to servers");
	RegisterConCommand(kSoundScriptCommand, SoundScriptCommand,
		"Server reply in the R1Delta sound-script parity check", FCVAR_SERVER_CAN_EXECUTE);
}

void SoundScriptParity_ClientConnecting() {
	g_client.prepared = false;
	g_client.awaitingVerdict = false;
	g_client.serverNames.clear();
	if (!g_client.marker)
		return;

	LocalTable table;
	Fingerprint local;
	std::uint32_t buckets[kSoundScriptBuckets];
	if (!ReadLocalTable(table) || !Summarize(table, local, buckets)) {
		Warning("R1Delta sound scripts: cannot read the local alias table; servers cannot check sound parity\n");
		SetConvarStringOriginal(g_client.marker, "");
		return;
	}
	char fingerprint[64];
	FormatFingerprint(local, fingerprint, sizeof(fingerprint));
	SetConvarStringOriginal(g_client.marker, fingerprint);

	for (int chunk = 0; chunk < kSoundScriptUploadChunks; ++chunk) {
		SoundScriptUploadChunk& out = g_client.chunks[chunk];
		_snprintf_s(out.name, sizeof(out.name), _TRUNCATE, "%s%d", kSoundScriptMarkerConVar, chunk);
		char* cursor = out.value;
		for (int i = 0; i < kHashesPerChunk; ++i, cursor += 8)
			_snprintf_s(cursor, sizeof(out.value) - (cursor - out.value), _TRUNCATE, "%08x",
				buckets[chunk * kHashesPerChunk + i]);
	}
	g_client.prepared = true;
}

int SoundScriptParity_ClientUploadChunks(bool messageCarriesMarker,
	SoundScriptUploadChunk (&out)[kSoundScriptUploadChunks]) {
	if (!messageCarriesMarker || !g_client.prepared)
		return 0;
	std::copy(std::begin(g_client.chunks), std::end(g_client.chunks), std::begin(out));
	g_client.awaitingVerdict = true;
	g_client.serverNames.clear();
	return kSoundScriptUploadChunks;
}
