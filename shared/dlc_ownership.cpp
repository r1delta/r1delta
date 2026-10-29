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
#include <cstdio>
#include <cstring>
#include <mutex>

namespace {

using namespace r1d_dlc;

constexpr char kServerGroupsConVar[] = "serverDLCMapGroups";

// The Titanfall 2 files a machine needs for the 20 converted maps (see tools/r2port/gen_install_manifest.py).
constexpr FileRequirement kR2Manifest[] = {
#include "r2_install_manifest.inc"
};

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

bool Group5Complete(std::string& problem)
{
	const std::filesystem::path install = r2port::GetR2InstallDirectory();
	if (install.empty()) {
		problem = "no valid Titanfall 2 install found";
		return false;
	}
	const SizeProbe probe = [&install](const std::string& relative) {
		return StatSizeW(install / std::filesystem::path(relative));
	};
	return ManifestSatisfied(probe, kR2Manifest, std::size(kR2Manifest), &problem);
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

bool ServerActive()
{
	__try {
		if (IsDedicatedServer())
			return G_engine_ds && *reinterpret_cast<const int*>(G_engine_ds + 0x1C89A78) >= 2;
		return G_engine && *reinterpret_cast<const int*>(G_engine + 0x2966168) >= 2;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

} // namespace

void Dlc_RegisterConVars()
{
	static bool done = false;
	if (done)
		return;
	done = true;

	State& state = S();
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
	const Presence presence = DetectPresence([](const std::string& path) { return StatSizeA(path.c_str()); },
		IsDedicatedServer(), group5);
	state.localMask = presence.Mask();
	state.lastRefreshTick = GetTickCount();
	state.refreshed = true;

	for (int g = 1; g <= kGroupCount; ++g) {
		if (state.enabled[g])
			SetConvarStringOriginal(state.enabled[g], presence.have[g] ? "1" : "0");
	}
	const std::string tail = (group5 ? std::string() : " (dlc5: " + group5Problem + ")")
		+ " root=\"" + GameRoot() + "\" delta_common=" + std::to_string(StatSizeA("vpk/client_mp_delta_common.bsp.pak000_000.vpk"));
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
	if (!LacksGroup(mask, group))
		return true;
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
	uint32_t players[PersistentDataSlots::kMaximumSupportedClients];
	int count = 0;
	const int maxClients = PData_ServerMaxClients();
	for (int slot = 0; slot < maxClients && count < static_cast<int>(std::size(players)); ++slot) {
		if (PData_ServerClientConnected(slot))
			players[count++] = ClientMaskFromSlot(slot);
	}

	const uint32_t mask = RotationMask(Dlc_LocalMask(), OverrideMask(), players, count);
	if (state.serverGroups && state.serverGroups->m_Value.m_nValue != static_cast<int>(mask)) {
		// Compared with the live value, not a cached one: a cfg or console command may have overwritten it.
		char value[16];
		_snprintf_s(value, sizeof(value), _TRUNCATE, "%u", mask);
		SetConvarStringOriginal(state.serverGroups, value);
		if (state.publishedRotation.exchange(mask) != mask)
			Report(false, "R1Delta DLC: rotation groups now %u (%d player(s))\n", mask, count);
	}
	return mask;
}

void Dlc_OnServerMapStart(const char* map)
{
	const int group = Dlc_GroupForMap(map);
	if (group != 0) {
		const int maxClients = PData_ServerMaxClients();
		for (int slot = 0; slot < maxClients; ++slot) {
			if (!PData_ServerClientConnected(slot) || !LacksGroup(ClientMaskFromSlot(slot), group))
				continue;
			const std::string text = MissingGroupMessage(group);
			Report(false, "R1Delta DLC: kicking slot %d, no dlc%d for %s\n", slot, group, map);
			PData_ServerKickClient(slot, text.c_str());
		}
	}
	Dlc_RotationMask();
}

// --- engine.dll hooks -----------------------------------------------------------------------------------

namespace {

int __fastcall HookGroupForMap(const char* map)
{
	return Dlc_GroupForMap(map);
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
