#pragma once
// DLC map ownership, the part that needs no engine: which map belongs to which DLC group, how a machine
// proves it owns a group, and which groups a server may rotate. Kept free of engine headers so the unit
// tests (tests/dlc_ownership_tests.cpp) can compile it on its own.
//
// Groups (bit n of a mask is group n; bit 0 is the base game and is always owned):
//   1-3  the Titanfall DLC map packs (Frontier's Edge, ...), stock engine groups
//   4    Titanfall Online maps (mp_box, mp_mia, mp_nest2), shipped only inside delta_common
//   5    the 20 converted Titanfall 2 maps, served from the player's own Titanfall 2 install
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>

namespace r1d_dlc {

constexpr int kGroupCount = 5;
constexpr uint32_t kBaseBit = 1u;
constexpr uint32_t kGroupBits = 0x3Eu; // bits 1..5

inline constexpr uint32_t GroupBit(int group)
{
	return group >= 1 && group <= kGroupCount ? (1u << group) : 0u;
}

struct MapGroup {
	const char* map;
	int group;
};

// Matches the stock engine table for groups 1-3 (engine.dll data at 0x793590) plus group 4.
inline constexpr MapGroup kMapGroups[] = {
	{ "mp_wargames", 1 }, { "mp_swampland", 1 }, { "mp_runoff", 1 },
	{ "mp_haven", 2 }, { "mp_switchback", 2 }, { "mp_harmony_mines", 2 },
	{ "mp_backwater", 3 }, { "mp_sandtrap", 3 }, { "mp_zone_18", 3 },
	{ "mp_box", 4 }, { "mp_mia", 4 }, { "mp_nest2", 4 },
};

inline bool EqualsNoCase(std::string_view a, std::string_view b)
{
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); ++i) {
		if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
			return false;
	}
	return true;
}

// "maps/mp_box.bsp" and "mp_box" name the same map.
inline std::string_view NormalizeMapName(std::string_view name)
{
	if (name.size() > 5 && EqualsNoCase(name.substr(0, 5), "maps/"))
		name.remove_prefix(5);
	if (name.size() > 4 && EqualsNoCase(name.substr(name.size() - 4), ".bsp"))
		name.remove_suffix(4);
	return name;
}

using MapPredicate = std::function<bool(std::string_view)>;

// 0 for a map that needs no DLC. `isGroup5Map` answers for the Titanfall 2 allowlist.
inline int GroupForMap(std::string_view map, const MapPredicate& isGroup5Map)
{
	map = NormalizeMapName(map);
	if (map.empty())
		return 0;
	for (const MapGroup& entry : kMapGroups) {
		if (EqualsNoCase(map, entry.map))
			return entry.group;
	}
	if (isGroup5Map && isGroup5Map(map))
		return 5;
	return 0;
}

// --- Presence on this machine ---------------------------------------------------------------------------

// Size in bytes of a file relative to the game root, or -1 when it does not exist.
using SizeProbe = std::function<int64_t(const std::string& relativePath)>;

inline const char* PackPrefix(bool server)
{
	return server ? "server_" : "client_";
}

// The launcher leaves zero-byte placeholders where a pack is missing, so existence is not enough.
inline bool NonEmpty(const SizeProbe& probe, const std::string& path)
{
	return probe(path) > 0;
}

// The shared content pack every Delta map directory VPK indexes.
inline bool DeltaCommonPresent(const SizeProbe& probe, bool server)
{
	if (NonEmpty(probe, std::string("vpk/") + PackPrefix(server) + "mp_delta_common.bsp.pak000_000.vpk"))
		return true;
	// A dedicated server without its own pack falls back to the client one (engine/filesystem/filesystem.cpp).
	return server && NonEmpty(probe, "vpk/client_mp_delta_common.bsp.pak000_000.vpk");
}

// One map's own pack: its content chunk and its directory VPK, both non-empty.
inline bool MapPackPresent(const SizeProbe& probe, bool server, const char* map)
{
	const char* prefixes[2] = { PackPrefix(server), "client_" };
	const int prefixCount = server ? 2 : 1;
	bool content = false;
	for (int i = 0; i < prefixCount && !content; ++i)
		content = NonEmpty(probe, std::string("vpk/") + prefixes[i] + map + ".bsp.pak000_000.vpk");
	if (!content)
		return false;
	for (int i = 0; i < prefixCount; ++i) {
		if (NonEmpty(probe, std::string("vpk/english") + prefixes[i] + map + ".bsp.pak000_dir.vpk")
			|| NonEmpty(probe, std::string("vpk/") + prefixes[i] + map + ".bsp.pak000_dir.vpk"))
			return true;
	}
	return false;
}

// A directory VPK alone (delta_common supplies the content).
inline bool MapDirectoryPresent(const SizeProbe& probe, bool server, const char* map)
{
	const char* prefixes[2] = { PackPrefix(server), "client_" };
	const int prefixCount = server ? 2 : 1;
	for (int i = 0; i < prefixCount; ++i) {
		if (NonEmpty(probe, std::string("vpk/english") + prefixes[i] + map + ".bsp.pak000_dir.vpk")
			|| NonEmpty(probe, std::string("vpk/") + prefixes[i] + map + ".bsp.pak000_dir.vpk"))
			return true;
	}
	return false;
}

struct Presence {
	bool have[kGroupCount + 1] = {}; // index = group, [0] unused

	uint32_t Mask() const
	{
		uint32_t mask = kBaseBit;
		for (int g = 1; g <= kGroupCount; ++g) {
			if (have[g])
				mask |= GroupBit(g);
		}
		return mask;
	}
};

// `group5Complete` is the already evaluated Titanfall 2 install check (it uses a different root).
inline Presence DetectPresence(const SizeProbe& vpk, bool server, bool group5Complete)
{
	Presence presence;
	const bool common = DeltaCommonPresent(vpk, server);
	for (int group = 1; group <= 4; ++group) {
		bool ok = true;
		bool any = false;
		for (const MapGroup& entry : kMapGroups) {
			if (entry.group != group)
				continue;
			any = true;
			// DLC 1-3 may also ship as their own content pack; DLC 4 only exists inside delta_common.
			const bool own = group <= 3 && MapPackPresent(vpk, server, entry.map);
			const bool viaCommon = common && MapDirectoryPresent(vpk, server, entry.map);
			if (!own && !viaCommon)
				ok = false;
		}
		presence.have[group] = any && ok;
	}
	presence.have[5] = group5Complete;
	return presence;
}

// --- Rotation -------------------------------------------------------------------------------------------

// The groups a server may put into rotation: the host owns the group, and every human player owns it
// or the host's override pretends they do. `playerMasks` holds one ownership mask per connected human.
inline uint32_t RotationMask(uint32_t hostMask, uint32_t overrideMask, const uint32_t* playerMasks, int playerCount)
{
	uint32_t result = kBaseBit;
	for (int g = 1; g <= kGroupCount; ++g) {
		const uint32_t bit = GroupBit(g);
		if ((hostMask & bit) == 0)
			continue;
		bool everyone = true;
		for (int i = 0; i < playerCount; ++i) {
			if ((playerMasks[i] & bit) == 0) {
				everyone = false;
				break;
			}
		}
		if (everyone || (overrideMask & bit) != 0)
			result |= bit;
	}
	return result;
}

inline bool LacksGroup(uint32_t playerMask, int group)
{
	const uint32_t bit = GroupBit(group);
	return bit != 0 && (playerMask & bit) == 0;
}

// --- Userinfo -------------------------------------------------------------------------------------------

inline const char* EnabledConVarName(int group)
{
	static const char* const names[kGroupCount + 1] = {
		"", "dlc1_enabled", "dlc2_enabled", "dlc3_enabled", "dlc4_enabled", "dlc5_enabled",
	};
	return group >= 1 && group <= kGroupCount ? names[group] : "";
}

inline const char* OverrideConVarName(int group)
{
	static const char* const names[kGroupCount + 1] = {
		"", "dlc1_override", "dlc2_override", "dlc3_override", "dlc4_override", "dlc5_override",
	};
	return group >= 1 && group <= kGroupCount ? names[group] : "";
}

// Ownership mask from a source of userinfo values (nullptr or "" = not sent = not owned).
inline uint32_t MaskFromUserInfo(const std::function<const char*(const char* name)>& get)
{
	uint32_t mask = kBaseBit;
	for (int g = 1; g <= kGroupCount; ++g) {
		const char* value = get(EnabledConVarName(g));
		if (value && std::atoi(value) > 0)
			mask |= GroupBit(g);
	}
	return mask;
}

// --- Messages -------------------------------------------------------------------------------------------

inline const char* GroupTitle(int group)
{
	switch (group) {
	case 1: return "DLC 1 (Expedition)";
	case 2: return "DLC 2 (Frontier's Edge)";
	case 3: return "DLC 3 (IMC Rising)";
	case 4: return "DLC 4 (Titanfall Online maps: Box, MIA, Nest 2)";
	case 5: return "DLC 5 (Titanfall 2 maps)";
	default: return "this map";
	}
}

// The text a player without the group sees (connect reject, kick, or the client's own disconnect).
inline std::string MissingGroupMessage(int group)
{
	std::string text = "You are missing ";
	text += GroupTitle(group);
	if (group == 5)
		text += ". Verify that Titanfall 2 is installed correctly, and purchase it if you do not own it.";
	else
		text += ". Redownload the game from the Delta launcher.";
	return text;
}

} // namespace r1d_dlc
