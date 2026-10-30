#include "dlc_ownership.h"
#include "dlc_ownership_logic.h"
#include "core.h"
#include "cvar.h"
#include "factory.h"
#include "load.h"
#include "logging.h"
#include "parity_common.h"
#include "persistentdata.h"
#include "persistentdata_slots.h"
#include "r2_map_provider.h"

#include <MinHook.h>
#include <windows.h>
#include <atomic>
#include <intrin.h>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace {

using namespace r1d_dlc;

constexpr char kServerGroupsConVar[] = "serverDLCMapGroups";

struct State {
	std::atomic<uint32_t> localMask{ kBaseBit };
	std::atomic<DWORD> lastRefreshTick{ 0 };
	std::atomic<bool> refreshed{ false };
	std::atomic<uint32_t> publishedRotation{ 0xFFFFFFFFu };
	ConVarR1* enabled[kGroupCount + 1] = {};
	ConVarR1* overrides[kGroupCount + 1] = {};
	ConVarR1* serverGroups = nullptr;
	std::mutex refreshMutex;
};

State& S()
{
	static State state;
	return state;
}

void Report(bool warning, const char* format, ...)
{
	char line[1024];
	va_list args;
	va_start(args, format);
	vsnprintf_s(line, sizeof(line), _TRUNCATE, format, args);
	va_end(args);
	OutputDebugStringA(line);
	if (warning)
		Warning("%s", line);
	else
		Msg("%s", line);
}

// The directory that holds vpk/: the process directory when it has one, otherwise two levels above the loaded
// engine module (<root>\bin\x64_retail\engine.dll), which is what the stock startup check used.
std::string GameRoot()
{
	static std::string resolved;
	static bool haveResolved = false;
	if (haveResolved)
		return resolved;

	const auto hasVpk = [](const std::string& dir) {
		const DWORD attributes = GetFileAttributesA((dir + "vpk").c_str());
		return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
	};
	if (hasVpk("")) {
		haveResolved = true;
		return resolved;
	}
	for (const char* name : { "engine.dll", "filesystem_stdio.dll", "launcher.dll", "engine_ds.dll", "dedicated.dll" }) {
		HMODULE module = GetModuleHandleA(name);
		char path[MAX_PATH]{};
		if (!module || !GetModuleFileNameA(module, path, MAX_PATH))
			continue;
		// <root>\bin\x64_retail\<module>
		std::string candidate = std::filesystem::path(path).parent_path().parent_path().parent_path().string();
		if (!candidate.empty() && candidate.back() != '\\')
			candidate += '\\';
		if (hasVpk(candidate)) {
			resolved = candidate;
			haveResolved = true;
			return resolved;
		}
	}
	return std::string(); // not found yet; try again on the next call
}

// Size of the file itself. GetFileAttributesEx reads the directory entry, which can report 0 for a large
// hard-linked file (the 12.5 GB delta_common pack); an open handle reports the real size.
int64_t StatSizeW(const std::filesystem::path& path)
{
	HANDLE file = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return -1;
	LARGE_INTEGER size{};
	const BOOL ok = GetFileSizeEx(file, &size);
	BY_HANDLE_FILE_INFORMATION info{};
	const bool directory = GetFileInformationByHandle(file, &info) && (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
	CloseHandle(file);
	return ok && !directory ? static_cast<int64_t>(size.QuadPart) : -1;
}

int64_t StatSizeA(const char* path)
{
	return StatSizeW(std::filesystem::path(GameRoot() + path));
}

// Group 5 (the 20 converted Titanfall 2 maps) is owned exactly when the map provider has a valid Titanfall 2 install: the same
// r2port::GetR2InstallDirectory() search and validator that mounts the maps. That validator (IsValidR2Install in r2_map_provider.cpp) checks
// bin/x64_retail/rtech_game.dll, a common client VPK directory plus vpk/client_mp_common.bsp.pak000_000.vpk, no r1/gameinfo.txt (not an R1 root)
// and at least one .rpak in r2/paks/Win64. There is no per-file size manifest: any install the provider mounts counts (other languages/versions).
bool Group5Complete(std::string& problem)
{
	if (r2port::GetR2InstallDirectory().empty()) {
		problem = "no valid Titanfall 2 install found (R1DELTA_TITANFALL2_PATH or the usual Steam/EA/Origin folders; needs bin/x64_retail/rtech_game.dll, vpk/client_mp_common and r2/paks/Win64/*.rpak)";
		return false;
	}
	return true;
}

bool IsGroup5Map(std::string_view map)
{
	return r2port::IsR2PortMap(map);
}

uint32_t OverrideMask()
{
	uint32_t mask = 0;
	for (int g = 1; g <= kGroupCount; ++g) {
		const ConVarR1* var = S().overrides[g];
		if (var && var->m_Value.m_nValue > 0)
			mask |= GroupBit(g);
	}
	return mask;
}

uint32_t ClientMaskFromSlot(int slot)
{
	return MaskFromUserInfo([slot](const char* name) { return PData_ServerClientUserInfo(slot, name); });
}

const char* CurrentServerMap()
{
	if (!pGlobalVarsServer)
		return nullptr;
	__try {
		const char* map = static_cast<const char*>(pGlobalVarsServer->mapname_pszValue);
		return map && map[0] ? map : nullptr;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return nullptr;
	}
}

// --- TFO (engine_r1o.dll, R1O fake-dedicated) client table ---------------------------------------------------
// Layout read from engine_r1o.dll: CGameServer `sv`, its inline array of CGameClient objects (the IClient
// subobject sits at +8 of each 179096-byte client) and the two IClient virtuals used below.

constexpr uintptr_t kR1OServerStateRva = 0x2659558;  // sv.m_State, >= 2 while a map runs
constexpr uintptr_t kR1OMaxClientsRva = 0x265971C;   // sv.m_nMaxClients
constexpr uintptr_t kR1OFirstClientRva = 0x2659738;  // IClient of slot 0
constexpr uintptr_t kR1OClientStride = 179096;
constexpr uintptr_t kR1OClientVtableRva = 0x55C7D8;  // CGameClient::`vftable' (the IClient subobject)
constexpr uintptr_t kR1OSignonStateOffset = 1144;    // IClient: m_nSignonState, >= 2 once connected
constexpr uintptr_t kR1OFakeClientOffset = 18176;    // IClient: m_bFakePlayer
constexpr size_t kR1OGetUserSettingSlot = 20;        // IClient vtable +160: const char* (name)
constexpr size_t kR1ODisconnectSlot = 13;            // IClient vtable +104: (uint8, fmt, ...)
constexpr int kR1OMaxClientSlots = 32;

bool ServerActive()
{
	__try {
		if (IsR1ODedicatedServer())
			return G_engine_r1o && *reinterpret_cast<const int*>(G_engine_r1o + kR1OServerStateRva) >= 2;
		if (IsDedicatedServer())
			return G_engine_ds && *reinterpret_cast<const int*>(G_engine_ds + 0x1C89A78) >= 2;
		return G_engine && *reinterpret_cast<const int*>(G_engine + 0x2966168) >= 2;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

int R1OMaxClients()
{
	__try {
		const int count = G_engine_r1o ? *reinterpret_cast<const int*>(G_engine_r1o + kR1OMaxClientsRva) : 0;
		return count < 0 ? 0 : (count > kR1OMaxClientSlots ? kR1OMaxClientSlots : count);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return 0;
	}
}

// The IClient of a connected human (not a bot, not a half-open slot), or 0.
uintptr_t R1OHumanClient(int slot)
{
	if (!G_engine_r1o || slot < 0 || slot >= kR1OMaxClientSlots)
		return 0;
	const uintptr_t client = G_engine_r1o + kR1OFirstClientRva + static_cast<uintptr_t>(slot) * kR1OClientStride;
	__try {
		if (*reinterpret_cast<const uintptr_t*>(client) != G_engine_r1o + kR1OClientVtableRva)
			return 0;
		if (*reinterpret_cast<const int*>(client + kR1OSignonStateOffset) < 2)
			return 0;
		if (*reinterpret_cast<const uint8_t*>(client + kR1OFakeClientOffset) != 0)
			return 0;
		return client;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return 0;
	}
}

const char* R1OUserSetting(uintptr_t client, const char* name)
{
	__try {
		const auto getUserSetting = reinterpret_cast<const char*(__fastcall*)(uintptr_t, const char*)>(
			(*reinterpret_cast<void***>(client))[kR1OGetUserSettingSlot]);
		return getUserSetting(client, name);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return nullptr;
	}
}

void R1ODisconnect(uintptr_t client, const char* text)
{
	__try {
		const auto disconnect = reinterpret_cast<void(__cdecl*)(uintptr_t, uint8_t, const char*, ...)>(
			(*reinterpret_cast<void***>(client))[kR1ODisconnectSlot]);
		disconnect(client, 1, "%s", text);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
	}
}

// --- connected human players, whatever the engine ---------------------------------------------------------------

struct ServerPlayer {
	int slot;
	uint32_t mask;
};
constexpr int kMaxServerPlayers = PersistentDataSlots::kMaximumSupportedClients;

int EnumerateServerPlayers(ServerPlayer* out, int capacity)
{
	int count = 0;
	if (IsR1ODedicatedServer()) {
		const int maxClients = R1OMaxClients();
		for (int slot = 0; slot < maxClients && count < capacity; ++slot) {
			const uintptr_t client = R1OHumanClient(slot);
			if (client)
				out[count++] = { slot, MaskFromUserInfo([client](const char* name) { return R1OUserSetting(client, name); }) };
		}
		return count;
	}
	const int maxClients = PData_ServerMaxClients();
	for (int slot = 0; slot < maxClients && count < capacity; ++slot) {
		if (PData_ServerClientConnected(slot))
			out[count++] = { slot, ClientMaskFromSlot(slot) };
	}
	return count;
}

void KickServerPlayer(int slot, const char* text)
{
	if (IsR1ODedicatedServer()) {
		if (const uintptr_t client = R1OHumanClient(slot))
			R1ODisconnect(client, text);
		return;
	}
	PData_ServerKickClient(slot, text);
}

} // namespace

void Dlc_RegisterConVars()
{
	static bool done = false;
	if (done)
		return;
	done = true;

	State& state = S();
	if (IsR1ODedicatedServer()) {
		// TFO ships dlc1-3_enabled and serverDLCMapGroups itself (reached through the wrapped cvar system). Groups 4
		// and 5 must exist as R1O userinfo convars or the engine drops what a client sends for them, and the
		// overrides must be R1O convars so the host can set them on the console. The R1 mirrors are what the
		// ownership code reads and writes (R1O changes reach them through the global change callback).
		for (int g = 4; g <= kGroupCount; ++g) {
			RegisterR1ODediConVar(EnabledConVarName(g), "0", FCVAR_USERINFO | FCVAR_DONTRECORD,
				"Whether this machine owns the DLC map group. Recomputed from the installed files at startup and before connecting.");
		}
		for (int g = 1; g <= kGroupCount; ++g) {
			RegisterR1ODediConVar(OverrideConVarName(g), g <= 3 ? "1" : "0", FCVAR_GAMEDLL,
				"Server hoster: treat every player as owning this DLC group for map rotation. Players without it are kicked when a map of the group starts.");
			state.overrides[g] = OriginalCCVar_FindVar ? OriginalCCVar_FindVar(cvarinterface, OverrideConVarName(g)) : nullptr;
		}
		state.serverGroups = OriginalCCVar_FindVar ? OriginalCCVar_FindVar(cvarinterface, kServerGroupsConVar) : nullptr;
		return;
	}
	for (int g = 1; g <= kGroupCount; ++g) {
		// Client and listen server already have dlc1-3_enabled (stock, userinfo). The dedicated server has none
		// of them, and it drops userinfo it has no convar for, so it needs all five to see what clients own.
		ConVarR1* existing = OriginalCCVar_FindVar ? OriginalCCVar_FindVar(cvarinterface, EnabledConVarName(g)) : nullptr;
		state.enabled[g] = existing
			? existing
			: RegisterConVar(EnabledConVarName(g), "0", FCVAR_USERINFO | FCVAR_DONTRECORD,
				"Whether this machine owns the DLC map group. Recomputed from the installed files at startup and before connecting.");
		state.overrides[g] = RegisterConVar(OverrideConVarName(g), g <= 3 ? "1" : "0", FCVAR_GAMEDLL,
			"Server hoster: treat every player as owning this DLC group for map rotation. Players without it are kicked when a map of the group starts.");
	}
	state.serverGroups = OriginalCCVar_FindVar ? OriginalCCVar_FindVar(cvarinterface, kServerGroupsConVar) : nullptr;
	if (!state.serverGroups)
		state.serverGroups = RegisterConVar(kServerGroupsConVar, "1", FCVAR_REPLICATED, "DLC Map groups that the server has enabled");
}

void Dlc_RefreshLocalPresence(const char* why)
{
	State& state = S();
	std::lock_guard<std::mutex> lock(state.refreshMutex);

	std::string group5Problem;
	const bool group5 = Group5Complete(group5Problem);
	Presence presence;
	std::string tail = group5 ? std::string() : " (dlc5: " + group5Problem + ")";
	if (IsR1ODedicatedServer()) {
		// The TFO host loads its maps through its own engine and content (it refuses a map it cannot load), so
		// groups 1-4 are its own; group 5 is the Titanfall 2 install the R2 map provider reads.
		for (int g = 1; g <= 4; ++g)
			presence.have[g] = true;
		presence.have[5] = group5;
	}
	else {
		presence = DetectPresence([](const std::string& path) { return StatSizeA(path.c_str()); },
			IsDedicatedServer(), group5);
		tail += " root=\"" + GameRoot() + "\" delta_common=" + std::to_string(StatSizeA("vpk/client_mp_delta_common.bsp.pak000_000.vpk"));
	}
	state.localMask = presence.Mask();
	state.lastRefreshTick = GetTickCount();
	state.refreshed = true;

	for (int g = 1; g <= kGroupCount; ++g) {
		if (state.enabled[g])
			SetConvarStringOriginal(state.enabled[g], presence.have[g] ? "1" : "0");
	}
	Report(false, "R1Delta DLC: ownership (%s) dlc1=%d dlc2=%d dlc3=%d dlc4=%d dlc5=%d%s\n", why ? why : "refresh",
		presence.have[1], presence.have[2], presence.have[3], presence.have[4], presence.have[5], tail.c_str());
}

uint32_t Dlc_LocalMask()
{
	if (!S().refreshed)
		Dlc_RefreshLocalPresence("first use");
	return S().localMask;
}

int Dlc_GroupForMap(const char* map)
{
	return GroupForMap(map ? map : "", IsGroup5Map);
}

bool Dlc_LocalMayLoadMap(const char* map, std::string& reason)
{
	const int group = Dlc_GroupForMap(map);
	if (group == 0)
		return true;
	uint32_t mask = Dlc_LocalMask();
	if (LacksGroup(mask, group)) {
		// The player may have repaired the install since the last look; do not make them restart for it.
		if (GetTickCount() - S().lastRefreshTick > 1000) {
			Dlc_RefreshLocalPresence("map load");
			mask = S().localMask;
		}
	}
	if (!LacksGroup(mask, group)) {
		// Group 5 is owned for any valid Titanfall 2 install, but a player may have removed single maps (their VPK and rpaks):
		// the loader would then wait on files that are not there. Refuse the map with the file that is missing.
		std::string missing;
		if (group == 5 && !r2port::IsR2PortMapInstalled(NormalizeMapName(map), missing)) {
			reason = std::string("The Titanfall 2 map ") + std::string(NormalizeMapName(map)) + " is not installed (missing " + missing
				+ "). Verify that Titanfall 2 is installed correctly (Steam: Verify integrity of game files, EA app: Repair).";
			return false;
		}
		return true;
	}
	reason = MissingGroupMessage(group);
	return false;
}

bool Dlc_CheckConnect(uint32_t clientMask, char* reject, int rejectLength)
{
	const char* map = CurrentServerMap();
	const int group = map ? Dlc_GroupForMap(map) : 0;
	if (!LacksGroup(clientMask, group))
		return true;
	const std::string text = MissingGroupMessage(group);
	_snprintf_s(reject, rejectLength, _TRUNCATE, "%s", text.c_str());
	Report(false, "R1Delta DLC: rejected a client without dlc%d on %s\n", group, map);
	return false;
}

uint32_t Dlc_RotationMask()
{
	State& state = S();
	ServerPlayer players[kMaxServerPlayers];
	const int count = EnumerateServerPlayers(players, kMaxServerPlayers);
	uint32_t masks[kMaxServerPlayers];
	for (int i = 0; i < count; ++i)
		masks[i] = players[i].mask;

	const uint32_t mask = RotationMask(Dlc_LocalMask(), OverrideMask(), masks, count);
	if (state.serverGroups && SetConvarStringOriginal && state.serverGroups->m_Value.m_nValue != static_cast<int>(mask)) {
		// Compared with the live value, not a cached one: a cfg or console command may have overwritten it.
		char value[16];
		_snprintf_s(value, sizeof(value), _TRUNCATE, "%u", mask);
		SetConvarStringOriginal(state.serverGroups, value);
		if (state.publishedRotation.exchange(mask) != mask)
			Report(false, "R1Delta DLC: rotation groups now %u (%d player(s))\n", mask, count);
	}
	return mask;
}

void Dlc_ServerFrame()
{
	static DWORD lastTick = 0;
	const DWORD now = GetTickCount();
	if (now - lastTick < 250)
		return;
	lastTick = now;
	if (ServerActive())
		Dlc_RotationMask();
}

void Dlc_OnServerMapStart(const char* map)
{
	const int group = Dlc_GroupForMap(map);
	if (group != 0) {
		ServerPlayer players[kMaxServerPlayers];
		const int count = EnumerateServerPlayers(players, kMaxServerPlayers);
		for (int i = 0; i < count; ++i) {
			if (!LacksGroup(players[i].mask, group))
				continue;
			const std::string text = MissingGroupMessage(group);
			Report(false, "R1Delta DLC: kicking slot %d, no dlc%d for %s\n", players[i].slot, group, map);
			KickServerPlayer(players[i].slot, text.c_str());
		}
		std::string missing;
		if (group == 5 && !r2port::IsR2PortMapInstalled(NormalizeMapName(map), missing))
			Report(true, "R1Delta DLC: this host is missing files of the Titanfall 2 map %s (%s), the map cannot load. Verify the Titanfall 2 install.\n", map, missing.c_str());
	}
	Dlc_RotationMask();
}

// --- engine.dll hooks -----------------------------------------------------------------------------------

namespace {

constexpr uintptr_t kCheckDlcReturn = 0xD521A; // engine.dll: CGameClient::CheckDLC, right after its group lookup

uintptr_t g_engineBase = 0;

int __fastcall HookGroupForMap(const char* map)
{
	const int group = Dlc_GroupForMap(map);
	// CheckDLC disconnects any client (bots and the replay client included, which carry no dlc4/5 userinfo) whose
	// mask lacks the map's group, and doing that while a listen server is still starting crashes the engine.
	// Groups 4 and 5 are enforced by Dlc_CheckConnect and Dlc_OnServerMapStart instead, so the stock check
	// keeps seeing only the three stock groups.
	if (group > 3 && reinterpret_cast<uintptr_t>(_ReturnAddress()) == g_engineBase + kCheckDlcReturn)
		return 0;
	return group;
}

// The stock kick for groups 1-3 on a map change; replaced by the five group version.
__int64 __fastcall HookStockMapKick(const char* map)
{
	Dlc_OnServerMapStart(map);
	return 0;
}

bool(__fastcall* oRotationBit)(int group);
int(__fastcall* oRotationValue)();

bool __fastcall HookRotationBit(int group)
{
	if (!ServerActive())
		return oRotationBit(group);
	return ((Dlc_RotationMask() >> (group & 31)) & 1u) != 0;
}

int __fastcall HookRotationValue()
{
	if (!ServerActive())
		return oRotationValue();
	return static_cast<int>(Dlc_RotationMask());
}

void(__fastcall* oSignonNew)(__int64 clientState);
void(__fastcall* oHostMap)(unsigned int* args);

// CClientState signon NEW: the engine is about to check and load the server's map. A map whose pack this
// machine does not own would be read from a missing file (the engine then waits in the filesystem forever).
void __fastcall HookSignonNew(__int64 clientState)
{
	if (*reinterpret_cast<const int*>(clientState + 116) == 3) {
		const char* map = reinterpret_cast<const char*>(clientState + 236);
		std::string reason;
		if (map[0] && !Dlc_LocalMayLoadMap(map, reason)) {
			Report(true, "R1Delta DLC: refusing to load %s: %s\n", map, reason.c_str());
			Parity::DisconnectWithReason(reason.c_str());
			return;
		}
	}
	oSignonNew(clientState);
}

// The `map` command (console, menus, +map): the same check for a map this machine would host.
void __fastcall HookHostMap(unsigned int* args)
{
	if (args && args[0] > 1) {
		const char* map = *reinterpret_cast<const char* const*>(reinterpret_cast<uintptr_t>(args) + 0x418);
		std::string reason;
		if (map && map[0] && !Dlc_LocalMayLoadMap(map, reason)) {
			Report(true, "R1Delta DLC: not starting %s: %s\n", map, reason.c_str());
			return;
		}
	}
	oHostMap(args);
}

bool BytesMatch(uintptr_t address, const unsigned char* expected, size_t count)
{
	__try {
		return memcmp(reinterpret_cast<const void*>(address), expected, count) == 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool Hook(const char* name, uintptr_t target, const unsigned char* prologue, size_t prologueLength, void* detour, void** original)
{
	if (!BytesMatch(target, prologue, prologueLength)) {
		Report(true, "R1Delta DLC: %s not hooked, unexpected bytes at %p\n", name, reinterpret_cast<void*>(target));
		return false;
	}
	const MH_STATUS created = MH_CreateHook(reinterpret_cast<LPVOID>(target), detour, original);
	if (created != MH_OK && created != MH_ERROR_ALREADY_CREATED) {
		Report(true, "R1Delta DLC: %s hook create failed (%d)\n", name, static_cast<int>(created));
		return false;
	}
	const MH_STATUS enabled = MH_EnableHook(reinterpret_cast<LPVOID>(target));
	if (enabled != MH_OK && enabled != MH_ERROR_ENABLED) {
		Report(true, "R1Delta DLC: %s hook enable failed (%d)\n", name, static_cast<int>(enabled));
		return false;
	}
	return true;
}

// The stock userinfo parser reads dlc1..dlc3_enabled (`cmp ebx, 4`); read all five.
bool ExtendUserInfoLoop(uintptr_t engineBase)
{
	const uintptr_t compare = engineBase + 0xD51C7;
	const unsigned char expected[] = { 0x83, 0xFB, 0x04, 0x7C, 0xC4 };
	if (!BytesMatch(compare, expected, sizeof(expected))) {
		Report(true, "R1Delta DLC: userinfo parser not patched, unexpected bytes at %p\n", reinterpret_cast<void*>(compare));
		return false;
	}
	DWORD oldProtect = 0;
	unsigned char* limit = reinterpret_cast<unsigned char*>(compare + 2);
	if (!VirtualProtect(limit, 1, PAGE_EXECUTE_READWRITE, &oldProtect))
		return false;
	*limit = static_cast<unsigned char>(kGroupCount + 1);
	VirtualProtect(limit, 1, oldProtect, &oldProtect);
	FlushInstructionCache(GetCurrentProcess(), limit, 1);
	return true;
}

} // namespace

void Dlc_InstallClientEngineHooks(uintptr_t engineBase)
{
	if (!engineBase)
		return;
	g_engineBase = engineBase;
	ExtendUserInfoLoop(engineBase);

	{
		const unsigned char p[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10 };
		Hook("group for map", engineBase + 0x1CC90, p, sizeof(p), &HookGroupForMap, nullptr);
	}
	{
		const unsigned char p[] = { 0x40, 0x53, 0x48, 0x81, 0xEC, 0xD0, 0x0B, 0x00, 0x00 };
		Hook("map change kick", engineBase + 0x1CD40, p, sizeof(p), &HookStockMapKick, nullptr);
	}
	{
		// serverDLCMapGroups readers: bit test and raw value.
		const unsigned char bit[] = { 0x48, 0x8B, 0x05 };
		if (BytesMatch(engineBase + 0x1CD20, bit, sizeof(bit)))
			Hook("rotation bit", engineBase + 0x1CD20, bit, sizeof(bit), &HookRotationBit, reinterpret_cast<void**>(&oRotationBit));
		if (BytesMatch(engineBase + 0x1CD10, bit, sizeof(bit)))
			Hook("rotation value", engineBase + 0x1CD10, bit, sizeof(bit), &HookRotationValue, reinterpret_cast<void**>(&oRotationValue));
	}
	{
		const unsigned char p[] = { 0x40, 0x53, 0x48, 0x81, 0xEC, 0x60, 0x01, 0x00, 0x00, 0x83, 0x79, 0x74, 0x03 };
		Hook("client signon new", engineBase + 0x765B0, p, sizeof(p), &HookSignonNew, reinterpret_cast<void**>(&oSignonNew));
	}
	{
		const unsigned char p[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0xE8 };
		Hook("host map", engineBase + 0x136FC0, p, sizeof(p), &HookHostMap, reinterpret_cast<void**>(&oHostMap));
	}
}

// --- engine_ds.dll hooks --------------------------------------------------------------------------------

namespace {

__int64(__fastcall* oDedicatedMapPrepare)(const char* map);

// Called by the map, changelevel and changelevel2 commands before the new map is set up.
__int64 __fastcall HookDedicatedMapPrepare(const char* map)
{
	Dlc_OnServerMapStart(map);
	return oDedicatedMapPrepare(map);
}

} // namespace

void Dlc_InstallDedicatedEngineHooks(uintptr_t engineDsBase)
{
	if (!engineDsBase)
		return;
	const unsigned char p[] = { 0x40, 0x56, 0x48, 0x81, 0xEC, 0x40, 0x01, 0x00, 0x00, 0x48, 0x8B, 0xF1 };
	Hook("dedicated map prepare", engineDsBase + 0xA3610, p, sizeof(p), &HookDedicatedMapPrepare,
		reinterpret_cast<void**>(&oDedicatedMapPrepare));
}

// --- engine_r1o.dll hooks (TFO fake-dedicated server) -----------------------------------------------------
// TFO never reaches HookedCBaseClientConnect. Its own CGameClient::CheckDLC only knows groups 1-3 and kicks
// with #DISCONNECT_NEEDDLC after the connection is already accepted, so the five group check is made where the
// connect packet's userinfo is still in hand (CGameClient::Connect) and where a map change starts.

namespace {

// CGameClient::Connect has one more int64 (the platform user id) than the retail R1 function.
using R1OClientConnect_t = char(__fastcall*)(__int64 client, const char* name, int userId, __int64 platformUserId,
	__int64 netChannel, char fakePlayer, int challenge, CUtlVector<NetMessageCvar_t>* conVars, char* reject, int rejectLength);
using R1OChangeLevel_t = unsigned __int64(__fastcall*)(unsigned int* command);

R1OClientConnect_t oR1OClientConnect = nullptr;
R1OChangeLevel_t oR1OChangeLevel = nullptr;
R1OChangeLevel_t oR1OChangeLevel2 = nullptr;

int R1OConVarCount(CUtlVector<NetMessageCvar_t>* conVars)
{
	__try {
		return conVars ? conVars->Count() : 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return -1;
	}
}

// Copies the value of one userinfo convar of the connect packet; false when the packet does not carry it.
bool R1OConVarValue(CUtlVector<NetMessageCvar_t>* conVars, const char* name, char* out, size_t outSize)
{
	out[0] = 0;
	__try {
		const int count = conVars ? conVars->Count() : 0;
		for (int i = 0; i < count; ++i) {
			const NetMessageCvar_t& var = conVars->Element(i);
			if (_stricmp(var.name, name) == 0) {
				strncpy_s(out, outSize, var.value, _TRUNCATE);
				return true;
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
	}
	return false;
}

uint32_t R1OMaskFromConVars(CUtlVector<NetMessageCvar_t>* conVars)
{
	return MaskFromUserInfo([conVars](const char* name) -> const char* {
		static thread_local char value[sizeof(NetMessageCvar_t::value)];
		return R1OConVarValue(conVars, name, value, sizeof(value)) ? value : nullptr;
	});
}

char __fastcall HookR1OClientConnect(__int64 client, const char* name, int userId, __int64 platformUserId,
	__int64 netChannel, char fakePlayer, int challenge, CUtlVector<NetMessageCvar_t>* conVars, char* reject, int rejectLength)
{
	// `reject` is ConnectClient's 256 byte buffer: returning 0 makes the engine send "Connection rejected: <text>".
	// Bots (fakePlayer) carry no userinfo and are never gated. A real client always has a packet; a missing one
	// counts as owning nothing.
	if (!fakePlayer && reject && rejectLength > 0) {
		const uint32_t mask = R1OMaskFromConVars(conVars);
		static std::atomic<int> logBudget{ 32 };
		if (logBudget.fetch_sub(1) > 0) {
			char dlc[kGroupCount + 1][8] = {};
			for (int g = 1; g <= kGroupCount; ++g) {
				if (!R1OConVarValue(conVars, EnabledConVarName(g), dlc[g], sizeof(dlc[g])))
					strcpy_s(dlc[g], "-");
			}
			const char* map = CurrentServerMap();
			Report(false, "R1Delta DLC: TFO connect userinfo vars=%d dlc1=%s dlc2=%s dlc3=%s dlc4=%s dlc5=%s mask=0x%X map=%s\n",
				R1OConVarCount(conVars), dlc[1], dlc[2], dlc[3], dlc[4], dlc[5], mask, map ? map : "(none)");
		}
		if (!Dlc_CheckConnect(mask, reject, rejectLength))
			return 0;
	}
	const char accepted = oR1OClientConnect(client, name, userId, platformUserId, netChannel, fakePlayer, challenge,
		conVars, reject, rejectLength);
	if (accepted && !fakePlayer)
		Dlc_RotationMask();
	return accepted;
}

const char* R1OCommandMapArgument(unsigned int* command)
{
	__try {
		// CCommand: argc at +0, argv[1] at +0x418 (the same slot the stock handlers read).
		if (!command || command[0] < 2)
			return nullptr;
		return *reinterpret_cast<const char* const*>(reinterpret_cast<uintptr_t>(command) + 0x418);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return nullptr;
	}
}

// changelevel / changelevel2 with a running server: the new map's group is enforced before the engine moves on.
void R1OBeforeChangeLevel(unsigned int* command)
{
	const char* map = R1OCommandMapArgument(command);
	if (map && map[0] && ServerActive())
		Dlc_OnServerMapStart(map);
}

unsigned __int64 __fastcall HookR1OChangeLevel(unsigned int* command)
{
	R1OBeforeChangeLevel(command);
	return oR1OChangeLevel(command);
}

unsigned __int64 __fastcall HookR1OChangeLevel2(unsigned int* command)
{
	R1OBeforeChangeLevel(command);
	return oR1OChangeLevel2(command);
}

constexpr uintptr_t kR1OCheckDlcGroupReturn = 0x140A02; // CGameClient::CheckDLC, right after its group lookup call
uintptr_t g_r1oEngineBase = 0;

// The engine's map -> group lookup (also behind GetDLCMapGroupForMap in the scripts and the playlist availability
// mask) only knows groups 1-3. Five groups for everybody except CheckDLC, which builds its mask from dlc1-3 only
// and would disconnect every client on a group 4/5 map; groups 4 and 5 are enforced by the Connect and changelevel hooks.
int __fastcall HookR1OGroupForMap(const char* map)
{
	const int group = Dlc_GroupForMap(map);
	if (group > 3 && reinterpret_cast<uintptr_t>(_ReturnAddress()) == g_r1oEngineBase + kR1OCheckDlcGroupReturn)
		return 0;
	return group;
}

} // namespace

void Dlc_InstallR1OEngineHooks(uintptr_t engineR1oBase)
{
	if (!engineR1oBase)
		return;
	// Exact prologues of engine_r1o.dll (the Titanfall Online engine R1Delta hosts); a different build fails closed.
	const unsigned char connect[] = {
		0x4C, 0x8B, 0xDC, 0x57, 0x48, 0x83, 0xEC, 0x60, 0x49, 0xC7, 0x43, 0xE8, 0xFE, 0xFF, 0xFF, 0xFF,
		0x49, 0x89, 0x5B, 0x10, 0x49, 0x89, 0x6B, 0x18
	};
	Hook("TFO CGameClient::Connect", engineR1oBase + 0x142B80, connect, sizeof(connect), &HookR1OClientConnect,
		reinterpret_cast<void**>(&oR1OClientConnect));
	const unsigned char changeLevel[] = {
		0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0x01, 0x48, 0x8B, 0xD9, 0x83, 0xF8, 0x02, 0x0F, 0x8C, 0xE1, 0x00, 0x00, 0x00
	};
	Hook("TFO changelevel", engineR1oBase + 0x186B60, changeLevel, sizeof(changeLevel), &HookR1OChangeLevel,
		reinterpret_cast<void**>(&oR1OChangeLevel));
	Hook("TFO changelevel2", engineR1oBase + 0x186C70, changeLevel, sizeof(changeLevel), &HookR1OChangeLevel2,
		reinterpret_cast<void**>(&oR1OChangeLevel2));
	const unsigned char groupForMap[] = {
		0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89, 0x7C, 0x24, 0x18, 0x48, 0x8B, 0xF9
	};
	g_r1oEngineBase = engineR1oBase;
	Hook("TFO group for map", engineR1oBase + 0x58910, groupForMap, sizeof(groupForMap), &HookR1OGroupForMap, nullptr);
}

// --- server.dll hooks (legacy dedicated server) -----------------------------------------------------------

namespace {

// Script natives GetDLCMapGroupForMap(string) and ServerHasDLCMapGroupEnabled(int). Stock they tail-jump into
// IVEngineServer slots 199/200, which the dedicated engine does not implement.
int __fastcall HookScriptGroupForMap(const char* map)
{
	return Dlc_GroupForMap(map);
}

bool __fastcall HookScriptServerHasGroup(unsigned int group)
{
	return ((Dlc_RotationMask() >> (group & 31)) & 1u) != 0;
}

} // namespace

void Dlc_InstallDedicatedServerHooks(uintptr_t serverBase)
{
	if (!serverBase)
		return;
	const unsigned char groupForMap[] = { 0x48, 0x8B, 0xD1, 0x48, 0x8B, 0x0D, 0x4E, 0xEA, 0x99, 0x00, 0x48, 0x8B, 0x01, 0x48, 0xFF, 0xA0, 0x38, 0x06, 0x00, 0x00 };
	const unsigned char serverHas[] = { 0x8B, 0xD1, 0x48, 0x8B, 0x0D, 0x2F, 0xEA, 0x99, 0x00, 0x48, 0x8B, 0x01, 0x48, 0xFF, 0xA0, 0x40, 0x06, 0x00, 0x00 };
	Hook("script GetDLCMapGroupForMap", serverBase + 0x2925C0, groupForMap, sizeof(groupForMap), &HookScriptGroupForMap, nullptr);
	Hook("script ServerHasDLCMapGroupEnabled", serverBase + 0x2925E0, serverHas, sizeof(serverHas), &HookScriptServerHasGroup, nullptr);
}
