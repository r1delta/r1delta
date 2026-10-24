#include "shared_particle_materials.h"

namespace r1delta::shared_particle_materials
{
namespace
{
using vpk_async_precache::ResourceEntryNode;
using vpk_async_precache::ResourceRecord;

// Canonical VPK bytes, including original CRLFs and whitespace. The explicit
// NUL plus the string literal's terminator supply the native length + 2 buffer.
// star_max01.vmt: mp_haven, CRC32 55BDA314, 190 bytes.
constexpr char kStarMax01Text[] =
	"\"SpriteCard\"\r\n"
	"{\r\n"
	"\t\"$basetexture\" \"effects/yellowflare\"\r\n"
	"\t\"$additive\" \"1\"\r\n"
	"\t\"$vertexcolor\" 1\r\n"
	"\t\"$vertexalpha\" 1\r\n"
	"\t$depthblend 0\r\n"
	"\r\n"
	"\t$maxsize 0.01\r\n"
	"\t$minsize 0.001\r\n"
	"\t\r\n"
	"\t$vertexFogAmount 1\r\n"
	"}\r\n"
	"\0";

// sniper_trail_add_ob6.vmt: mp_airbase, CRC32 7BB18917, 199 bytes.
constexpr char kSniperTrailAddOb6Text[] =
	"\"SpriteCard\"\r\n"
	"{\r\n"
	"\t\"$basetexture\" \"particle/smoke/sniper_trail_add\"\r\n"
	"\t\"$additive\" \t\"1\"\r\n"
	"\r\n"
	"\t$translucent \t1\r\n"
	"\t\"$nocull\"\t1\r\n"
	"\t$splinetype\t1\r\n"
	"\t\r\n"
	"\t//$overbrightfactor 6\r\n"
	"\t\r\n"
	"\t$vertexFogAmount 1\r\n"
	"\r\n"
	"}\r\n"
	"\r\n"
	"\r\n"
	"\r\n"
	"\0";

static_assert(sizeof(kStarMax01Text) == 190 + 2);
static_assert(sizeof(kSniperTrailAddOb6Text) == 199 + 2);

constexpr ResourceRecord kStarMax01Record{
	"materials/particle", "star_max01", "vmt", 0,
	sizeof(kStarMax01Text) - 2, {}
};
constexpr ResourceRecord kSniperTrailAddOb6Record{
	"materials/particle/smoke", "sniper_trail_add_ob6", "vmt", 0,
	sizeof(kSniperTrailAddOb6Text) - 2, {}
};

// These resources never enter a pack store, so map unmount/async-precache
// cleanup cannot destroy them. Their static storage lasts for the process.
// The native material cache reader borrows the immutable text; its external
// read-only CUtlBuffer does not own or free it. Each material has its own native
// lock, the only field the consumer mutates. Lookup must never reset that lock.
ResourceEntryNode s_starMax01Node{
	reinterpret_cast<std::uintptr_t>(kStarMax01Text), 1, 0, 0, 0
};
ResourceEntryNode s_sniperTrailAddOb6Node{
	reinterpret_cast<std::uintptr_t>(kSniperTrailAddOb6Text), 1, 0, 0, 0
};

bool MatchesPath(const char* path, const char* canonical) noexcept
{
	while (*canonical) {
		char value = *path++;
		if (value >= 'A' && value <= 'Z')
			value = static_cast<char>(value + ('a' - 'A'));
		else if (value == '\\')
			value = '/';
		if (value != *canonical++)
			return false;
	}
	return *path == '\0';
}
}

bool LookupResource(
	const char* path,
	vpk_async_precache::ResourceLookup& result) noexcept
{
	if (!path)
		return false;

	if (MatchesPath(path, "materials/particle/star_max01.vmt")) {
		result = { {}, &kStarMax01Record, &s_starMax01Node };
		return true;
	}
	if (MatchesPath(path, "materials/particle/smoke/sniper_trail_add_ob6.vmt")) {
		result = { {}, &kSniperTrailAddOb6Record, &s_sniperTrailAddOb6Node };
		return true;
	}
	return false;
}
}
