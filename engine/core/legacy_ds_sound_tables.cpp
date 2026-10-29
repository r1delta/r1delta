#include "legacy_ds_sound_tables.h"
#include "logging.h"
#include <MinHook.h>
#include <cstring>
#include <windows.h>

#include "engine/filesystem/r2_map_provider.h"

namespace {

// Verified against S:\game\r1delta\bin\engine_ds.dll (R1 2014 dedicated build).
constexpr std::uint32_t kImageSize = 0x24EF000;
constexpr std::uint32_t kAliasCapacityRva = 0x5059;    // cmp eax, 40F4h
constexpr std::uint32_t kAliasLimitArgRva = 0x506B;    // mov r9d, 40F4h
constexpr std::uint32_t kAliasCountRva = 0x15713E4;    // dword_1815713E4
constexpr std::uint32_t kAliasLoaderRva = 0x6EF0;      // re-memsets + reloads game_sounds_manifest

constexpr std::uint32_t kLegacyAliasCapacity = 0x40F4;

// Populated by a successful expansion; readers use GetLegacyDSAliasRecords.
std::uintptr_t g_aliases = 0;

constexpr std::size_t kAliasRecord = 512;      // shl rbx, 9
constexpr std::size_t kNameRecord = 16;        // { const char* name; int index }
constexpr std::size_t kVariationRecord = 400;  // imul rax, 190h

// Current 2015 client engine.dll: 18,552 aliases / 18,552 names / 22,204 variations,
// loading 18,396 / 18,396 / 22,106. Keep headroom over both.
constexpr std::size_t kAliasCapacity = 24576;
constexpr std::size_t kNameCapacity = 24576;
constexpr std::size_t kVariationCapacity = 32768;

enum class Table {
	Names,
	Variations,
	Aliases,
};

struct LeaSite {
	std::uint32_t rva;
	const char* bytes;  // 3-byte prefix of `lea reg, [rip+disp32]`
	Table table;
	std::uint32_t delta;
	std::uint32_t expectedDisp;
};

// Every RIP-relative reference to unk_1805844F0 (names), unk_1805C9590
// (variations) and unk_180D52BE0 (aliases). The +1E8/+1FE entries address
// record 0's file-name/flags fields as engine statics; their offset is kept.
const LeaSite kLeaSites[] = {
	{0x003870, "\x48\x8d\x2d", Table::Names, 0, 0x00580C79},
	{0x0038F1, "\x48\x8d\x0d", Table::Aliases, 0, 0x00D4F2E8},
	{0x00393B, "\x48\x8d\x0d", Table::Aliases, 0x1FE, 0x00D4F49C},
	{0x003C29, "\x4c\x8d\x3d", Table::Names, 0, 0x005808C0},
	{0x004338, "\x48\x8d\x05", Table::Variations, 0, 0x005C5251},
	{0x004517, "\x48\x8d\x0d", Table::Variations, 0, 0x005C5072},
	{0x0050B3, "\x48\x8d\x05", Table::Aliases, 0, 0x00D4DB26},
	{0x005141, "\x48\x8d\x3d", Table::Aliases, 0x1E8, 0x00D4DC80},
	{0x00528D, "\x48\x8d\x3d", Table::Names, 0, 0x0057F25C},
	{0x005B7E, "\x4c\x8d\x0d", Table::Variations, 0, 0x005C3A0B},
	{0x005D03, "\x4c\x8d\x0d", Table::Variations, 0, 0x005C3886},
	{0x006BFE, "\x48\x8d\x0d", Table::Variations, 0, 0x005C298B},
	{0x006C05, "\x4c\x8d\x35", Table::Aliases, 0x1FE, 0x00D4C1D2},
	{0x006C91, "\x48\x8d\x0d", Table::Variations, 0, 0x005C28F8},
	{0x006D59, "\x48\x8d\x0d", Table::Variations, 0, 0x005C2830},
	{0x006EFA, "\x48\x8d\x0d", Table::Aliases, 0, 0x00D4BCDF},
	{0x006F0E, "\x48\x8d\x35", Table::Names, 0, 0x0057D5DB},
	{0x006F25, "\x48\x8d\x0d", Table::Variations, 0, 0x005C2664},
};

bool WriteBytes(void* target, const void* source, std::size_t length, const char* what) {
	DWORD previous = 0;
	if (!VirtualProtect(target, length, PAGE_EXECUTE_READWRITE, &previous)) {
		Warning("R1Delta legacy DS: cannot unprotect %s (%lu)\n", what, static_cast<unsigned long>(GetLastError()));
		return false;
	}
	std::memcpy(target, source, length);
	FlushInstructionCache(GetCurrentProcess(), target, length);
	DWORD ignored = 0;
	VirtualProtect(target, length, previous, &ignored);
	return true;
}

void* AllocateNear(std::uintptr_t moduleBase, std::size_t size) {
	SYSTEM_INFO info{};
	GetSystemInfo(&info);
	const std::uintptr_t granularity = info.dwAllocationGranularity ? info.dwAllocationGranularity : 0x10000;
	// Stay inside the +/-2GB window that rip-relative disp32 can reach, and far
	// enough from the image that the reservation cannot collide with it.
	for (std::uintptr_t distance = 0x1000000; distance < 0x70000000; distance += granularity) {
		if (moduleBase > distance) {
			if (void* block = VirtualAlloc(reinterpret_cast<void*>(moduleBase - distance), size,
				MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE))
				return block;
		}
		if (void* block = VirtualAlloc(reinterpret_cast<void*>(moduleBase + distance), size,
			MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE))
			return block;
	}
	return nullptr;
}

std::uintptr_t TableBase(Table table, std::uintptr_t aliases, std::uintptr_t names, std::uintptr_t variations) {
	switch (table) {
	case Table::Names: return names;
	case Table::Variations: return variations;
	default: return aliases;
	}
}

}

bool InstallLegacyDSSoundTableExpansion(std::uintptr_t engineDSBase) {
	if (!engineDSBase) {
		Warning("R1Delta legacy DS: sound-alias table expansion skipped, engine_ds.dll is not mapped\n");
		return false;
	}

	// PE image guard: never patch a different dedicated engine build.
	auto* image = reinterpret_cast<unsigned char*>(engineDSBase);
	const std::uint32_t lfanew = *reinterpret_cast<const std::uint32_t*>(image + 0x3C);
	if (lfanew == 0 || lfanew > 0x1000
		|| *reinterpret_cast<const std::uint32_t*>(image + lfanew) != 0x00004550) {
		Warning("R1Delta legacy DS: sound-alias table expansion skipped, engine_ds.dll has no PE header\n");
		return false;
	}
	const std::uint32_t imageSize = *reinterpret_cast<const std::uint32_t*>(image + lfanew + 0x50);
	if (imageSize != kImageSize) {
		Warning("R1Delta legacy DS: sound-alias table expansion skipped, engine_ds.dll image size 0x%X is not 0x%X\n",
			imageSize, kImageSize);
		return false;
	}

	// Verify every site before mutating anything.
	for (const auto& site : kLeaSites) {
		const auto* at = image + site.rva;
		if (std::memcmp(at, site.bytes, 3) != 0
			|| *reinterpret_cast<const std::uint32_t*>(at + 3) != site.expectedDisp) {
			Warning("R1Delta legacy DS: sound-alias table expansion skipped, reference at +0x%X did not match\n",
				site.rva);
			return false;
		}
	}
	if (std::memcmp(image + kAliasCapacityRva, "\x3D\xF4\x40\x00\x00", 5) != 0) {
		Warning("R1Delta legacy DS: sound-alias table expansion skipped, alias capacity check did not match\n");
		return false;
	}
	if (std::memcmp(image + kAliasLimitArgRva, "\x41\xB9\xF4\x40\x00\x00", 6) != 0) {
		Warning("R1Delta legacy DS: sound-alias table expansion skipped, alias capacity diagnostic did not match\n");
		return false;
	}

	const std::size_t aliasBytes = kAliasCapacity * kAliasRecord;
	const std::size_t nameBytes = kNameCapacity * kNameRecord;
	const std::size_t variationBytes = kVariationCapacity * kVariationRecord;
	const std::size_t totalBytes = aliasBytes + nameBytes + variationBytes;
	void* block = AllocateNear(engineDSBase, totalBytes);
	if (!block) {
		Warning("R1Delta legacy DS: sound-alias table expansion skipped, no %llu byte block near engine_ds.dll\n",
			static_cast<unsigned long long>(totalBytes));
		return false;
	}
	// VirtualAlloc hands out zeroed pages. The native loader only zeroes the
	// legacy prefix on each reload, which stays correct: slots beyond it start
	// zeroed and are rewritten before the alias count reaches them.
	const auto aliases = reinterpret_cast<std::uintptr_t>(block);
	const auto names = aliases + aliasBytes;
	const auto variations = names + nameBytes;

	for (const auto& site : kLeaSites) {
		auto* at = const_cast<unsigned char*>(image + site.rva);
		const auto instructionEnd = engineDSBase + site.rva + 7;
		const auto target = TableBase(site.table, aliases, names, variations) + site.delta;
		const auto displacement = static_cast<std::int64_t>(target) - static_cast<std::int64_t>(instructionEnd);
		if (displacement < INT32_MIN || displacement > INT32_MAX) {
			Warning("R1Delta legacy DS: relocated tables are not reachable from +0x%X\n", site.rva);
			return false;
		}
		const std::uint32_t newDisp = static_cast<std::uint32_t>(static_cast<std::int32_t>(displacement));
		if (!WriteBytes(at + 3, &newDisp, sizeof(newDisp), "alias table reference")) {
			return false;
		}
	}

	const std::uint32_t newCapacity = static_cast<std::uint32_t>(kAliasCapacity);
	if (!WriteBytes(image + kAliasCapacityRva + 1, &newCapacity, sizeof(newCapacity), "alias capacity check"))
		return false;
	if (!WriteBytes(image + kAliasLimitArgRva + 2, &newCapacity, sizeof(newCapacity), "alias capacity diagnostic"))
		return false;

	g_aliases = aliases;
	Msg("R1Delta legacy DS: expanded sound-alias tables aliases=%u (was %u) names=%u variations=%u block=%p\n",
		static_cast<unsigned>(kAliasCapacity), kLegacyAliasCapacity,
		static_cast<unsigned>(kNameCapacity), static_cast<unsigned>(kVariationCapacity), block);

	// The dedicated engine loads its alias set from CDedicatedServerAPI during
	// startup; this hook normally runs first. If a table already exists, reload
	// it so the truncated set is replaced.
	const int loadedAliases = *reinterpret_cast<const int*>(image + kAliasCountRva);
	if (loadedAliases > 0) {
		Msg("R1Delta legacy DS: %d aliases were already loaded before the expansion; reloading sound scripts\n",
			loadedAliases);
		reinterpret_cast<void (*)()>(engineDSBase + kAliasLoaderRva)();
	}
	return true;
}

const void* GetLegacyDSAliasRecords() {
	return reinterpret_cast<const void*>(g_aliases);
}

namespace {
// engine_ds.dll sub_180005F20(const char* filename) loads one sound script file (the client's +98B0 twin; same prologue).
constexpr std::uint32_t kScriptLoadRva = 0x5F20;
constexpr unsigned char kScriptLoadPrologue[14] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x81, 0xEC};
constexpr std::uint32_t kCrcRva = 0x15713E0;
constexpr std::uint32_t kRangeCountRva = 0x1571400;  // dword_181571400, limit MAX_SOUND_RANGES
using ScriptLoadFn = std::int64_t (*)(const char*);
ScriptLoadFn g_scriptLoad = nullptr;
std::uintptr_t g_dsEngine = 0;
std::uint32_t g_hiddenAliases = 0, g_hiddenStart = ~0u;

std::int64_t DSScriptLoad(const char* filename) {
	auto* engine = reinterpret_cast<unsigned char*>(g_dsEngine);
	auto& crc = *reinterpret_cast<std::uint32_t*>(engine + kCrcRva);
	const int aliases = *reinterpret_cast<const int*>(engine + kAliasCountRva);
	if (aliases >= 0 && static_cast<std::uint32_t>(aliases) < g_hiddenStart) {  // the table was cleared for a reload
		g_hiddenAliases = 0;
		g_hiddenStart = ~0u;
	}
	if (filename && _stricmp(filename, "scripts/r2port_sounds.txt") == 0) {
		// The dedicated engine's lookup of this file reaches no provider hook and no mountable search path: place it loose.
		if (!r2port::PlaceGeneratedSoundScript())
			Msg("R1Delta legacy DS: no generated R2 sound script available; the R2 aliases are not loaded\n");
	}
	const std::uint32_t crcBefore = crc;
	const std::int64_t result = g_scriptLoad(filename);
	if (filename && _stricmp(filename, "scripts/r2port_sounds.txt") == 0) {
		g_hiddenStart = static_cast<std::uint32_t>(aliases);
		g_hiddenAliases = static_cast<std::uint32_t>(*reinterpret_cast<const int*>(engine + kAliasCountRva) - aliases);
		crc = crcBefore;
	}
	return result;
}
}

bool InstallLegacyDSAliasHiding(std::uintptr_t engineDSBase) {
	if (!engineDSBase || g_scriptLoad)
		return g_scriptLoad != nullptr;
	auto* image = reinterpret_cast<unsigned char*>(engineDSBase);
	if (std::memcmp(image + kScriptLoadRva, kScriptLoadPrologue, sizeof(kScriptLoadPrologue)) != 0) {
		Warning("R1Delta legacy DS: R2 alias hiding skipped, sound script loader prologue did not match\n");
		return false;
	}
	g_dsEngine = engineDSBase;
	if (MH_CreateHook(image + kScriptLoadRva, reinterpret_cast<void*>(&DSScriptLoad), reinterpret_cast<void**>(&g_scriptLoad)) != MH_OK) {
		g_scriptLoad = nullptr;
		Warning("R1Delta legacy DS: R2 alias hiding skipped, cannot hook the sound script loader\n");
		return false;
	}
	return true;
}

std::uint32_t LegacyDSHiddenTrailingAliases() { return g_hiddenAliases; }
