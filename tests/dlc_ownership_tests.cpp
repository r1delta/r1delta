#include "../shared/dlc_ownership_logic.h"

#include <cstdio>
#include <map>
#include <string>

using namespace r1d_dlc;

namespace {

int g_failures = 0;

void Check(bool condition, const char* what)
{
	if (!condition) {
		std::printf("FAIL: %s\n", what);
		++g_failures;
	}
}

struct FakeDisk {
	std::map<std::string, int64_t> files;
	SizeProbe Probe() const
	{
		return [this](const std::string& path) -> int64_t {
			const auto it = files.find(path);
			return it == files.end() ? -1 : it->second;
		};
	}
};

bool IsR2(std::string_view map)
{
	return map == "mp_glitch" || map == "mp_thaw";
}

void TestGroupForMap()
{
	Check(GroupForMap("mp_runoff", IsR2) == 1, "runoff is dlc1");
	Check(GroupForMap("MP_Haven", IsR2) == 2, "map names are case insensitive");
	Check(GroupForMap("maps/mp_sandtrap.bsp", IsR2) == 3, "path and extension are stripped");
	Check(GroupForMap("mp_nest2", IsR2) == 4, "nest2 is dlc4");
	Check(GroupForMap("mp_box", IsR2) == 4, "box is dlc4");
	Check(GroupForMap("mp_mia", IsR2) == 4, "mia is dlc4");
	Check(GroupForMap("mp_glitch", IsR2) == 5, "an allowlisted R2 map is dlc5");
	Check(GroupForMap("mp_angel_city", IsR2) == 0, "a base map needs no dlc");
	Check(GroupForMap("", IsR2) == 0, "no map needs no dlc");
	Check(GroupForMap("mp_runoff_extra", IsR2) == 0, "only exact names match");
}

void AddMapDir(FakeDisk& disk, const char* map, int64_t size = 1000)
{
	disk.files[std::string("vpk/englishclient_") + map + ".bsp.pak000_dir.vpk"] = size;
}

void AddMapContent(FakeDisk& disk, const char* map, int64_t size = 5000)
{
	disk.files[std::string("vpk/client_") + map + ".bsp.pak000_000.vpk"] = size;
}

void AddAllDirs(FakeDisk& disk)
{
	for (const MapGroup& entry : kMapGroups)
		AddMapDir(disk, entry.map);
}

void TestDirectoriesWithoutContentAreNotOwnership()
{
	FakeDisk disk;
	AddAllDirs(disk);
	const Presence none = DetectPresence(disk.Probe(), false, false);
	Check(none.Mask() == kBaseBit, "directory VPKs alone own nothing");
}

void TestDeltaCommonOwnsGroupsOneToFour()
{
	FakeDisk disk;
	AddAllDirs(disk);
	disk.files["vpk/client_mp_delta_common.bsp.pak000_000.vpk"] = 12'000'000'000;
	const Presence presence = DetectPresence(disk.Probe(), false, false);
	Check(presence.have[1] && presence.have[2] && presence.have[3] && presence.have[4], "delta_common owns dlc1-4");
	Check(!presence.have[5], "delta_common says nothing about dlc5");
	Check(presence.Mask() == (kBaseBit | 0x1Eu), "mask of dlc1-4");
}

void TestZeroByteDeltaCommonIsAPlaceholder()
{
	FakeDisk disk;
	AddAllDirs(disk);
	disk.files["vpk/client_mp_delta_common.bsp.pak000_000.vpk"] = 0;
	Check(DetectPresence(disk.Probe(), false, false).Mask() == kBaseBit, "an empty delta_common is a placeholder");
}

void TestDeltaCommonNeedsDirectories()
{
	FakeDisk disk;
	disk.files["vpk/client_mp_delta_common.bsp.pak000_000.vpk"] = 1;
	AddMapDir(disk, "mp_box");
	AddMapDir(disk, "mp_mia");
	const Presence presence = DetectPresence(disk.Probe(), false, false);
	Check(!presence.have[4], "dlc4 needs the nest2 directory too");
	AddMapDir(disk, "mp_nest2");
	Check(DetectPresence(disk.Probe(), false, false).have[4], "dlc4 with all three directories");
}

void TestOwnPackOwnsOneGroup()
{
	FakeDisk disk;
	for (const char* map : { "mp_runoff", "mp_swampland", "mp_wargames" }) {
		AddMapDir(disk, map);
		AddMapContent(disk, map);
	}
	AddMapDir(disk, "mp_haven");
	AddMapContent(disk, "mp_haven");
	const Presence presence = DetectPresence(disk.Probe(), false, false);
	Check(presence.have[1], "dlc1 from its own packs");
	Check(!presence.have[2], "dlc2 with one of three packs is not owned");
	Check(!presence.have[4], "dlc4 is never owned without delta_common");
}

void TestServerPrefixAndFallback()
{
	FakeDisk disk;
	AddAllDirs(disk);
	disk.files["vpk/server_mp_delta_common.bsp.pak000_000.vpk"] = 1;
	Check(DetectPresence(disk.Probe(), true, false).have[1], "a server owns through server_ delta_common");
	Check(!DetectPresence(disk.Probe(), false, false).have[1], "a client does not use the server pack");

	FakeDisk clientOnly;
	AddAllDirs(clientOnly);
	clientOnly.files["vpk/client_mp_delta_common.bsp.pak000_000.vpk"] = 1;
	Check(DetectPresence(clientOnly.Probe(), true, false).have[1], "a server falls back to the client pack");
}

void TestManifest()
{
	const FileRequirement files[] = { { "r2/paks/Win64/a.rpak", 100 }, { "vpk/b.vpk", 50 } };
	FakeDisk disk;
	disk.files["r2/paks/Win64/a.rpak"] = 100;
	disk.files["vpk/b.vpk"] = 70;
	std::string problem;
	Check(ManifestSatisfied(disk.Probe(), files, 2, &problem), "complete install");
	disk.files["r2/paks/Win64/a.rpak"] = 99;
	Check(!ManifestSatisfied(disk.Probe(), files, 2, &problem) && problem.find("smaller") != std::string::npos,
		"a truncated file fails and is named");
	disk.files.erase("r2/paks/Win64/a.rpak");
	Check(!ManifestSatisfied(disk.Probe(), files, 2, &problem) && problem.find("missing") != std::string::npos,
		"a missing file fails and is named");
}

void TestRotation()
{
	const uint32_t host = kBaseBit | GroupBit(1) | GroupBit(2) | GroupBit(4) | GroupBit(5);
	const uint32_t everyoneOwnsOneAndTwo[] = { kBaseBit | GroupBit(1) | GroupBit(2), kBaseBit | GroupBit(1) | GroupBit(2) | GroupBit(5) };
	Check(RotationMask(host, 0, everyoneOwnsOneAndTwo, 2) == (kBaseBit | GroupBit(1) | GroupBit(2)),
		"only groups every player owns");
	Check(RotationMask(host, GroupBit(5) | GroupBit(4), everyoneOwnsOneAndTwo, 2)
			== (kBaseBit | GroupBit(1) | GroupBit(2) | GroupBit(4) | GroupBit(5)),
		"override pretends everybody owns the group");
	Check(RotationMask(host, GroupBit(3), everyoneOwnsOneAndTwo, 2) == (kBaseBit | GroupBit(1) | GroupBit(2)),
		"override has no effect on a group the host lacks");
	Check(RotationMask(host, 0, nullptr, 0) == host, "an empty server rotates everything the host owns");
	const uint32_t oneLacksFive[] = { kBaseBit | GroupBit(5), kBaseBit };
	Check((RotationMask(host, 0, oneLacksFive, 2) & GroupBit(5)) == 0, "one player without dlc5 removes it");
	Check((RotationMask(host, GroupBit(5), oneLacksFive, 2) & GroupBit(5)) != 0, "dlc5 override keeps it");
}

void TestUserInfo()
{
	std::map<std::string, std::string> info = { { "dlc1_enabled", "1" }, { "dlc2_enabled", "0" }, { "dlc4_enabled", "1" } };
	const auto get = [&info](const char* name) -> const char* {
		const auto it = info.find(name);
		return it == info.end() ? nullptr : it->second.c_str();
	};
	const uint32_t mask = MaskFromUserInfo(get);
	Check(mask == (kBaseBit | GroupBit(1) | GroupBit(4)), "mask from userinfo; unsent and 0 are not owned");
	Check(LacksGroup(mask, 5) && LacksGroup(mask, 2) && !LacksGroup(mask, 1) && !LacksGroup(mask, 0),
		"lacks check, group 0 is never lacking");
}

void TestMessages()
{
	Check(MissingGroupMessage(1).find("Redownload the game from the Delta launcher") != std::string::npos, "dlc1 message");
	Check(MissingGroupMessage(4).find("Redownload the game from the Delta launcher") != std::string::npos, "dlc4 message");
	const std::string five = MissingGroupMessage(5);
	Check(five.find("Verify that Titanfall 2 is installed correctly, and purchase it if you do not own it.") != std::string::npos,
		"dlc5 message");
	Check(five.find("Delta launcher") == std::string::npos, "dlc5 does not send people to the Delta launcher");
}

} // namespace

int main()
{
	TestGroupForMap();
	TestDirectoriesWithoutContentAreNotOwnership();
	TestDeltaCommonOwnsGroupsOneToFour();
	TestZeroByteDeltaCommonIsAPlaceholder();
	TestDeltaCommonNeedsDirectories();
	TestOwnPackOwnsOneGroup();
	TestServerPrefixAndFallback();
	TestManifest();
	TestRotation();
	TestUserInfo();
	TestMessages();
	if (g_failures) {
		std::printf("%d failure(s)\n", g_failures);
		return 1;
	}
	std::printf("dlc_ownership_tests: all passed\n");
	return 0;
}
