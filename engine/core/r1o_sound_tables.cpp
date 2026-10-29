#include "r1o_sound_tables.h"
#include "r1o_sound_tables_profile.h"
#include "audio_hook_profile.h"
#include "core.h"
#include "logging.h"
#include <MinHook.h>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <windows.h>

namespace {
namespace profile = r1o_sound_tables::profile;

// The expansion is live only where it has been verified end to end. R1DELTA_R1O_R2_SOUND=1 forces it on, =0 forces it off.
constexpr bool kEnabledByDefault = false;

// Capacities. The wire carries the alias index as a signed 16-bit value, so 32767 is the hard ceiling; the DS expansion uses the same
// sizes (24,576 aliases / 32,768 variations) and the stock set plus every R2 alias needs 18,618 / 22,366.
constexpr std::uint32_t kAliasCapacity = 24576;
constexpr std::uint32_t kVariationCapacity = 32768;
static_assert(kAliasCapacity <= 32767 && kVariationCapacity <= 0x7FFFFFFF / 400, "native codec and record offsets");

constexpr std::size_t kAliasRecord = 512;
constexpr std::size_t kVariationRecord = 400;
constexpr std::size_t kNameRecord = 16;
// The name lookup reads entry -1 of an empty bank and the enumerator reads one terminal null entry past the last name: both belong to
// this allocation (a 16 byte lead pad and one trailing entry) instead of the unrelated globals around the original tables.
constexpr std::size_t kNameBytes = (kAliasCapacity + 2ull) * kNameRecord;
constexpr std::size_t kVariationBytes = kVariationCapacity * kVariationRecord;
constexpr std::size_t kAliasBytes = kAliasCapacity * kAliasRecord;
constexpr std::size_t kStorageBytes = kNameBytes + kVariationBytes + kAliasBytes;
constexpr std::array<std::size_t, 3> kTableOffsets = {kNameRecord, kNameBytes, kNameBytes + kVariationBytes};
constexpr std::array<std::size_t, 3> kLogicalBytes = {kAliasCapacity * kNameRecord, kVariationBytes, kAliasBytes};
static_assert(kStorageBytes < 0x4000000, "allocation must stay far inside rip reach");
static_assert(profile::ripReferences.size() == 52 && profile::imageRelativeReferences.size() == 2 && profile::constants.size() == 7);

// Engine globals that are NOT moved.
constexpr std::uint32_t kCrcRva = 0x18836F0;
constexpr std::uint32_t kAliasCountRva = 0x18836F4;
constexpr std::uint32_t kRangeCountRva = 0x100B110;
// sub_180044CA0(const char* file): loads one sound script of game_sounds_manifest.txt (the client's +98B0 and engine_ds +5F20 twin).
constexpr std::uint32_t kScriptLoadRva = 0x44CA0;
constexpr unsigned char kScriptLoadPrologue[24] = {
    0x48, 0x8B, 0xC4, 0x57, 0x48, 0x81, 0xEC, 0x50, 0x01, 0x00, 0x00, 0x48, 0xC7, 0x44, 0x24, 0x30,
    0xFE, 0xFF, 0xFF, 0xFF, 0x48, 0x89, 0x58, 0x08};

struct PreparedInstruction {
    std::uintptr_t address = 0;
    std::uint8_t size = 0;
    std::array<unsigned char, 15> original{};
    std::array<unsigned char, 15> replacement{};
};
constexpr std::size_t kPatchCount = profile::ripReferences.size() + profile::imageRelativeReferences.size() + profile::constants.size();

void* s_storage = nullptr;  // process lifetime owner of the relocated tables
std::uintptr_t s_engine = 0;
bool s_installed = false;

using ScriptLoadFn = void(__fastcall*)(const char*);
ScriptLoadFn s_scriptLoad = nullptr;
std::uint32_t s_hiddenAliases = 0;
std::uint32_t s_hiddenStart = ~0u;

bool EnabledByEnvironment() {
    char value[8]{};
    const DWORD length = GetEnvironmentVariableA("R1DELTA_R1O_R2_SOUND", value, sizeof(value));
    if (length == 1 && value[0] == '1')
        return true;
    if (length == 1 && value[0] == '0')
        return false;
    return kEnabledByDefault;
}

std::uintptr_t AlignUp(std::uintptr_t value, std::uintptr_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

// A free region within +-2 GB of the image so every rip-relative displacement and every image-base relative disp32 reaches it.
void* AllocateTablesNear(std::uintptr_t engine) {
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const auto granularity = static_cast<std::uintptr_t>(info.dwAllocationGranularity);
    const auto applicationMinimum = reinterpret_cast<std::uintptr_t>(info.lpMinimumApplicationAddress);
    const auto applicationMaximum = reinterpret_cast<std::uintptr_t>(info.lpMaximumApplicationAddress);
    constexpr std::uintptr_t reach = 0x7FFFFFFFull - kStorageBytes - profile::kSizeOfImage;
    const auto lower = engine > reach ? engine - reach : applicationMinimum;
    const auto upper = engine < applicationMaximum - reach ? engine + reach : applicationMaximum;
    const auto minimum = lower > applicationMinimum ? lower : applicationMinimum;
    std::uintptr_t cursor = minimum;
    while (cursor < upper) {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &region, sizeof(region)))
            return nullptr;
        const auto begin = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        if (region.RegionSize > applicationMaximum - begin)
            return nullptr;
        const auto end = begin + region.RegionSize;
        if (region.State == MEM_FREE) {
            const auto candidate = AlignUp(begin > minimum ? begin : minimum, granularity);
            if (candidate < upper && candidate <= end && kStorageBytes <= end - candidate && kStorageBytes <= upper - candidate) {
                if (auto* allocation = VirtualAlloc(reinterpret_cast<void*>(candidate), kStorageBytes,
                        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE))
                    return allocation;
            }
        }
        if (end <= cursor)
            return nullptr;
        cursor = end;
    }
    return nullptr;
}

bool WriteInstruction(const PreparedInstruction& instruction, bool restore) {
    auto* address = reinterpret_cast<void*>(instruction.address);
    const auto& expected = restore ? instruction.replacement : instruction.original;
    const auto& replacement = restore ? instruction.original : instruction.replacement;
    if (std::memcmp(address, expected.data(), instruction.size))
        return false;
    DWORD protection;
    if (!VirtualProtect(address, instruction.size, PAGE_EXECUTE_READWRITE, &protection))
        return false;
    std::memcpy(address, replacement.data(), instruction.size);
    const bool flushed = FlushInstructionCache(GetCurrentProcess(), address, instruction.size) != 0;
    DWORD ignored;
    const bool protectedAgain = VirtualProtect(address, instruction.size, protection, &ignored) != 0;
    if (!flushed || !protectedAgain)
        Warning("R1Delta R1O sound tables: code write protection/cache failure at %p\n", address);
    return flushed && protectedAgain;
}

std::uint32_t ConstantValue(std::uint8_t kind) {
    switch (kind) {
    case 0: return kAliasCapacity;
    case 1: return kVariationCapacity;
    case 2: return static_cast<std::uint32_t>(kAliasBytes);
    case 3: return static_cast<std::uint32_t>(kNameBytes - kNameRecord);  // names plus the terminal null entry
    default: return static_cast<std::uint32_t>(kVariationBytes);
    }
}

// Records the CRC and the alias count before the generated R2 script and restores the CRC after it.
void __fastcall ScriptLoadHook(const char* filename) {
    auto* engine = reinterpret_cast<unsigned char*>(s_engine);
    auto& crc = *reinterpret_cast<std::uint32_t*>(engine + kCrcRva);
    const int aliases = *reinterpret_cast<const int*>(engine + kAliasCountRva);
    if (aliases >= 0 && static_cast<std::uint32_t>(aliases) < s_hiddenStart) {  // the table was cleared for a reload
        s_hiddenAliases = 0;
        s_hiddenStart = ~0u;
    }
    const std::uint32_t crcBefore = crc;
    s_scriptLoad(filename);
    if (filename && _stricmp(filename, "scripts/r2port_sounds.txt") == 0) {
        const int after = *reinterpret_cast<const int*>(engine + kAliasCountRva);
        s_hiddenStart = static_cast<std::uint32_t>(aliases);
        s_hiddenAliases = after > aliases ? static_cast<std::uint32_t>(after - aliases) : 0;
        crc = crcBefore;
    }
}

bool InstallScriptLoadHook(std::uintptr_t engineBase) {
    auto* target = reinterpret_cast<unsigned char*>(engineBase + kScriptLoadRva);
    if (std::memcmp(target, kScriptLoadPrologue, sizeof(kScriptLoadPrologue)) != 0) {
        Warning("R1Delta R1O sound tables: sound script loader prologue did not match; tail hiding unavailable\n");
        return false;
    }
    if (MH_CreateHook(target, reinterpret_cast<void*>(&ScriptLoadHook), reinterpret_cast<void**>(&s_scriptLoad)) != MH_OK) {
        s_scriptLoad = nullptr;
        Warning("R1Delta R1O sound tables: cannot hook the sound script loader\n");
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        MH_RemoveHook(target);
        s_scriptLoad = nullptr;
        Warning("R1Delta R1O sound tables: cannot enable the sound script loader hook\n");
        return false;
    }
    return true;
}
}  // namespace

bool InstallR1OSoundTables(std::uintptr_t engineBase) {
    if (s_installed)
        return engineBase == s_engine;
    if (!IsR1ODedicatedServer() || !engineBase)
        return false;
    if (!EnabledByEnvironment()) {
        Msg("R1Delta R1O sound tables: expansion disabled (R1DELTA_R1O_R2_SOUND); stock 18552-alias tables, no R2 sound aliases\n");
        return false;
    }
    if (!r1audio::VerifyModule(engineBase, profile::kTimeDateStamp, profile::kSizeOfImage, profile::kSha256, "R1O engine_r1o.dll sound table profile"))
        return false;
    // Never move live storage: the stock tables must still be empty.
    if (*reinterpret_cast<const std::int32_t*>(engineBase + kAliasCountRva) != 0 ||
        *reinterpret_cast<const std::int32_t*>(engineBase + kRangeCountRva) != 0) {
        Warning("R1Delta R1O sound tables: refusing relocation after the sound scripts were loaded\n");
        return false;
    }

    std::array<PreparedInstruction, kPatchCount> patches{};
    std::size_t count = 0;
    const auto prepareReference = [&](const profile::Reference& reference) {
        if (reference.table >= kTableOffsets.size() || reference.offset >= kLogicalBytes[reference.table] ||
            reference.size > reference.bytes.size() || reference.displacement + 4 > reference.size)
            return false;
        auto& patch = patches[count++];
        patch.address = engineBase + reference.rva;
        patch.size = reference.size;
        patch.original = reference.bytes;
        patch.replacement = reference.bytes;
        return true;
    };
    for (const auto& reference : profile::ripReferences)
        if (!prepareReference(reference))
            return false;
    for (const auto& reference : profile::imageRelativeReferences)
        if (!prepareReference(reference))
            return false;
    for (std::size_t i = 0; i != profile::constants.size(); ++i) {
        const auto& constant = profile::constants[i];
        if (constant.size > constant.bytes.size() || constant.immediate + 4 > constant.size)
            return false;
        auto& patch = patches[count++];
        patch.address = engineBase + constant.rva;
        patch.size = constant.size;
        patch.original = constant.bytes;
        patch.replacement = constant.bytes;
        const std::uint32_t value = ConstantValue(profile::constantKinds[i]);
        std::memcpy(patch.replacement.data() + constant.immediate, &value, sizeof(value));
    }
    // Every complete instruction must match before anything is mutated.
    for (const auto& patch : patches) {
        if (std::memcmp(reinterpret_cast<const void*>(patch.address), patch.original.data(), patch.size)) {
            Warning("R1Delta R1O sound tables: mismatched complete instruction +0x%llx\n",
                static_cast<unsigned long long>(patch.address - engineBase));
            return false;
        }
    }
    {  // The loader hook target is checked up front too, so a build that cannot hide the tail never relocates either.
        if (std::memcmp(reinterpret_cast<const void*>(engineBase + kScriptLoadRva), kScriptLoadPrologue, sizeof(kScriptLoadPrologue)) != 0) {
            Warning("R1Delta R1O sound tables: sound script loader prologue did not match\n");
            return false;
        }
    }

    auto* storage = AllocateTablesNear(engineBase);
    if (!storage) {
        Warning("R1Delta R1O sound tables: cannot allocate %llu bytes within displacement reach\n",
            static_cast<unsigned long long>(kStorageBytes));
        return false;
    }

    std::size_t index = 0;
    for (const auto& reference : profile::ripReferences) {
        auto& patch = patches[index++];
        const auto destination = reinterpret_cast<std::uintptr_t>(storage) + kTableOffsets[reference.table] + reference.offset;
        const auto relative = static_cast<std::int64_t>(destination) - static_cast<std::int64_t>(patch.address + patch.size);
        if (relative < INT32_MIN || relative > INT32_MAX) {
            VirtualFree(storage, 0, MEM_RELEASE);
            Warning("R1Delta R1O sound tables: out-of-range native table displacement\n");
            return false;
        }
        const auto displacement = static_cast<std::int32_t>(relative);
        std::memcpy(patch.replacement.data() + reference.displacement, &displacement, sizeof(displacement));
    }
    for (const auto& reference : profile::imageRelativeReferences) {
        auto& patch = patches[index++];
        const auto destination = reinterpret_cast<std::uintptr_t>(storage) + kTableOffsets[reference.table] + reference.offset;
        const auto relative = static_cast<std::int64_t>(destination) - static_cast<std::int64_t>(engineBase);
        if (relative < INT32_MIN || relative > INT32_MAX) {
            VirtualFree(storage, 0, MEM_RELEASE);
            Warning("R1Delta R1O sound tables: out-of-range image-relative table displacement\n");
            return false;
        }
        const auto displacement = static_cast<std::int32_t>(relative);
        std::memcpy(patch.replacement.data() + reference.displacement, &displacement, sizeof(displacement));
    }

    for (std::size_t i = 0; i != patches.size(); ++i) {
        if (WriteInstruction(patches[i], false))
            continue;
        bool restored = true;
        for (std::size_t rollback = i + 1; rollback != 0; --rollback) {
            const auto& patch = patches[rollback - 1];
            if (std::memcmp(reinterpret_cast<void*>(patch.address), patch.original.data(), patch.size) == 0)
                continue;
            if (!WriteInstruction(patch, true))
                restored = false;
        }
        if (restored)
            VirtualFree(storage, 0, MEM_RELEASE);
        else
            s_storage = storage;  // never free a block still referenced by code
        Warning("R1Delta R1O sound tables: relocation failed at +0x%llx; rollback %s\n",
            static_cast<unsigned long long>(patches[i].address - engineBase), restored ? "complete" : "FAILED");
        return false;
    }
    s_storage = storage;
    s_engine = engineBase;
    s_installed = true;
    Msg("R1Delta R1O sound tables: relocated %u table consumers and %u bounds; aliases=%u variations=%u (stock 18552/22204) block=%p\n",
        static_cast<unsigned>(profile::ripReferences.size() + profile::imageRelativeReferences.size()),
        static_cast<unsigned>(profile::constants.size()), kAliasCapacity, kVariationCapacity, storage);

    // The CRC/parity hiding is required for the tail to be harmless to a stock peer; without it the larger table still works
    // (it only holds what the scripts define), so a failed hook just keeps the R2 alias file from being served.
    return InstallScriptLoadHook(engineBase);
}

const void* GetR1OSoundNameEntries() {
    return s_installed ? static_cast<const unsigned char*>(s_storage) + kTableOffsets[0] : nullptr;
}

const void* GetR1OSoundAliasRecords() {
    return s_installed ? static_cast<const unsigned char*>(s_storage) + kTableOffsets[2] : nullptr;
}

std::uint32_t R1OHiddenTrailingAliases() {
    return s_hiddenAliases;
}
