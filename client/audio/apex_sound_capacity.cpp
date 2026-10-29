// Ported unchanged from the singleplayer line (branch sp, commit a1e447a, client/audio/apex_sound_capacity.cpp). The relocation table and
// install sequence are byte verified against the exact Client2015 engine.dll; see that history for the derivation.
#include "apex_sound_capacity.h"
#include "apex_sound_capacity_profile.h"
#include "audio_hook_profile.h"
#include "load.h"
#include <array>
#include <cstddef>
#include <cstring>

namespace {
// The name enumerator +74F0 reads a terminal null entry, and may physically
// inspect the preceding entry for an empty bank. These sentinels belong to this
// allocation, not the unrelated native configuration globals after +8824A0.
// They do not make native name lookup valid on an empty bank: callers still
// require aliasCount>0 before +71B0, exactly as the parameter bridge enforces.
constexpr std::size_t kNameBytes = (R1ApexSoundAliasCapacity + 2ull) * 16;
constexpr std::size_t kVariationBytes = R1ApexSoundVariationCapacity * 400ull;
constexpr std::size_t kAliasBytes = R1ApexSoundAliasCapacity * 512ull;
constexpr std::size_t kStorageBytes = kNameBytes + kVariationBytes + kAliasBytes;
constexpr std::array<std::size_t, 3> kTableOffsets = {16, kNameBytes, kNameBytes + kVariationBytes};
constexpr std::array<std::size_t, 3> kLogicalBytes = {
    R1ApexSoundAliasCapacity * 16ull, kVariationBytes, kAliasBytes};
constexpr std::size_t kPatchCount = r1audio::capacity::references.size() + r1audio::capacity::constants.size();
static_assert(R1ApexSoundAliasCapacity == 32768, "native signed-16 alias codec ceiling");
static_assert(kStorageBytes < 0x4000000);
static_assert(kNameBytes % 16 == 0 && kVariationBytes % 16 == 0 && kAliasBytes % 16 == 0);
static_assert(r1audio::capacity::references.size() == 51 && r1audio::capacity::constants.size() == 7);
static_assert(r1audio::capacity::constants[0].value == R1ApexSoundVariationCapacity &&
    r1audio::capacity::constants[1].value == R1ApexSoundVariationCapacity &&
    r1audio::capacity::constants[2].value == R1ApexSoundAliasCapacity &&
    r1audio::capacity::constants[3].value == R1ApexSoundAliasCapacity &&
    r1audio::capacity::constants[4].value == kAliasBytes &&
    r1audio::capacity::constants[5].value == kNameBytes - 16 &&
    r1audio::capacity::constants[6].value == kVariationBytes, "profile bounds must match owned storage");

struct PreparedInstruction {
    std::uintptr_t address = 0;
    std::uint8_t size = 0;
    std::array<unsigned char, 15> original{};
    std::array<unsigned char, 15> replacement{};
};
void* s_storage = nullptr; // Process-lifetime owner; never a borrowed voice/cache pointer.
std::uintptr_t s_engine = 0;
bool s_installed = false;

std::uintptr_t AlignUp(std::uintptr_t value, std::uintptr_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

void* AllocateTablesNear(std::uintptr_t engine) {
    if (GetR1DeltaEngineMode() != R1DeltaEngineMode::Client2015) return nullptr;
    // Same VirtualQuery/VirtualAlloc region walk used by FFA's near relays.
    // RW data only. Leave enough reach for the entire allocation and all native
    // instruction sites; every actual displacement is checked again below.
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const auto granularity = static_cast<std::uintptr_t>(info.dwAllocationGranularity);
    const auto applicationMinimum = reinterpret_cast<std::uintptr_t>(info.lpMinimumApplicationAddress);
    const auto applicationMaximum = reinterpret_cast<std::uintptr_t>(info.lpMaximumApplicationAddress);
    constexpr std::uintptr_t reach = 0x7FFFFFFFull - kStorageBytes - 0x3264000;
    const auto lower = engine > reach ? engine - reach : applicationMinimum;
    const auto upper = engine < applicationMaximum - reach ? engine + reach : applicationMaximum;
    const auto minimum = lower > applicationMinimum ? lower : applicationMinimum;
    std::uintptr_t cursor = minimum;
    while (cursor < upper) {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &region, sizeof(region))) return nullptr;
        const auto begin = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        if (region.RegionSize > applicationMaximum - begin) return nullptr;
        const auto end = begin + region.RegionSize;
        if (region.State == MEM_FREE) {
            const auto candidate = AlignUp(begin > minimum ? begin : minimum, granularity);
            if (candidate < upper && candidate <= end && kStorageBytes <= end - candidate && kStorageBytes <= upper - candidate) {
                if (auto* allocation = VirtualAlloc(reinterpret_cast<void*>(candidate), kStorageBytes,
                    MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) return allocation;
            }
        }
        if (end <= cursor) return nullptr;
        cursor = end;
    }
    return nullptr;
}

bool WriteInstruction(const PreparedInstruction& instruction, bool restore) {
    if (GetR1DeltaEngineMode() != R1DeltaEngineMode::Client2015) return false;
    auto* address = reinterpret_cast<void*>(instruction.address);
    const auto& expected = restore ? instruction.replacement : instruction.original;
    const auto& replacement = restore ? instruction.original : instruction.replacement;
    if (std::memcmp(address, expected.data(), instruction.size)) return false;
    DWORD protection;
    if (!VirtualProtect(address, instruction.size, PAGE_EXECUTE_READWRITE, &protection)) return false;
    std::memcpy(address, replacement.data(), instruction.size);
    const bool flushed = FlushInstructionCache(GetCurrentProcess(), address, instruction.size) != 0;
    DWORD ignored;
    const bool protectedAgain = VirtualProtect(address, instruction.size, protection, &ignored) != 0;
    if (!flushed || !protectedAgain)
        Warning("R1Delta audio capacity: code write protection/cache failure at %p\n", address);
    return flushed && protectedAgain;
}
}

bool InstallR1ApexSoundCapacity(std::uintptr_t engineBase) {
    if (GetR1DeltaEngineMode() != R1DeltaEngineMode::Client2015) return false;
    if (s_installed) return engineBase == s_engine;
    static constexpr std::array<unsigned char, 32> digest = {
        0x02,0x14,0xA4,0x04,0x36,0xEC,0xA7,0x7F,0x16,0x0D,0x12,0x76,0x3A,0xB8,0xF3,0x92,
        0xA8,0xCE,0x22,0x68,0xBF,0xE0,0xA9,0x73,0x18,0x8E,0x42,0x6A,0xE4,0xC0,0x26,0x3C};
    if (!r1audio::VerifyModule(engineBase, 0x55038DAC, 0x3264000, digest, "R1 engine.dll capacity profile")) return false;
    // Never move live storage. Engine DLL load is before its audio CS is usable,
    // so do not try to acquire that CS from a loader notification or debugger.
    if (*reinterpret_cast<void**>(engineBase + 0x200DF78) ||
        *reinterpret_cast<std::int32_t*>(engineBase + 0x1A0DBC4) != 0 ||
        *reinterpret_cast<std::int32_t*>(engineBase + 0x1A0DBE0) != 0) {
        Warning("R1Delta audio capacity: refusing relocation after sound or bank initialization\n");
        return false;
    }
    std::array<PreparedInstruction, kPatchCount> patches{};
    std::size_t count = 0;
    for (const auto& reference : r1audio::capacity::references) {
        if (reference.table >= kTableOffsets.size() || reference.offset >= kLogicalBytes[reference.table] ||
            reference.size > reference.bytes.size() || reference.displacement + 4 > reference.size) return false;
        auto& patch = patches[count++];
        patch.address = engineBase + reference.rva;
        patch.size = reference.size;
        patch.original = reference.bytes;
        patch.replacement = reference.bytes;
    }
    for (const auto& constant : r1audio::capacity::constants) {
        if (constant.size > constant.bytes.size() || constant.immediate + 4 > constant.size) return false;
        auto& patch = patches[count++];
        patch.address = engineBase + constant.rva;
        patch.size = constant.size;
        patch.original = constant.bytes;
        patch.replacement = constant.bytes;
        std::memcpy(patch.replacement.data() + constant.immediate, &constant.value, sizeof(constant.value));
    }
    for (const auto& patch : patches) {
        if (std::memcmp(reinterpret_cast<const void*>(patch.address), patch.original.data(), patch.size)) {
            Warning("R1Delta audio capacity: mismatched complete instruction +0x%llx\n",
                static_cast<unsigned long long>(patch.address - engineBase));
            return false;
        }
    }
    auto* storage = AllocateTablesNear(engineBase);
    if (!storage) {
        Warning("R1Delta audio capacity: cannot allocate %llu bytes within RIP reach\n", static_cast<unsigned long long>(kStorageBytes));
        return false;
    }
    for (std::size_t i = 0; i != r1audio::capacity::references.size(); ++i) {
        const auto& reference = r1audio::capacity::references[i];
        auto& patch = patches[i];
        const auto destination = reinterpret_cast<std::uintptr_t>(storage) + kTableOffsets[reference.table] + reference.offset;
        const auto relative = static_cast<std::int64_t>(destination) - static_cast<std::int64_t>(patch.address + patch.size);
        if (relative < -0x80000000ll || relative > 0x7FFFFFFFll) {
            VirtualFree(storage, 0, MEM_RELEASE);
            Warning("R1Delta audio capacity: out-of-range native table displacement\n");
            return false;
        }
        const auto displacement = static_cast<std::int32_t>(relative);
        std::memcpy(patch.replacement.data() + reference.displacement, &displacement, sizeof(displacement));
    }
    for (std::size_t i = 0; i != patches.size(); ++i) {
        if (WriteInstruction(patches[i], false)) continue;
        bool restored = true;
        for (std::size_t rollback = i + 1; rollback != 0; --rollback) {
            const auto& patch = patches[rollback - 1];
            // A protection/cache failure can occur after bytes were written.
            if (std::memcmp(reinterpret_cast<void*>(patch.address), patch.original.data(), patch.size) == 0) continue;
            if (!WriteInstruction(patch, true)) restored = false;
        }
        if (restored) VirtualFree(storage, 0, MEM_RELEASE);
        else s_storage = storage; // Never free a block still referenced by code.
        Warning("R1Delta audio capacity: relocation failed at +0x%llx; rollback %s (startup must abort)\n",
            static_cast<unsigned long long>(patches[i].address - engineBase), restored ? "complete" : "FAILED");
        return false;
    }
    s_storage = storage;
    s_engine = engineBase;
    s_installed = true;
    Msg("R1Delta audio capacity: relocated 51 native table consumers and 7 backed bounds; aliases=%u variations=%u names=%p variants=%p aliases=%p, CRC code/protocol unmodified\n",
        R1ApexSoundAliasCapacity, R1ApexSoundVariationCapacity,
        static_cast<unsigned char*>(storage) + kTableOffsets[0],
        static_cast<unsigned char*>(storage) + kTableOffsets[1],
        static_cast<unsigned char*>(storage) + kTableOffsets[2]);
    return true;
}

const void* R1ApexSoundAliasRecord(std::uint32_t index) {
    if (!s_installed || GetR1DeltaEngineMode() != R1DeltaEngineMode::Client2015) return nullptr;
    const auto count = *reinterpret_cast<const std::int32_t*>(s_engine + 0x1A0DBC4);
    if (count <= 0 || static_cast<std::uint32_t>(count) > R1ApexSoundAliasCapacity ||
        index >= static_cast<std::uint32_t>(count)) return nullptr;
    return static_cast<const unsigned char*>(s_storage) + kTableOffsets[2] + index * 512ull;
}

const void* R1ApexSoundVariationRecord(std::uint32_t index) {
    if (!s_installed || GetR1DeltaEngineMode() != R1DeltaEngineMode::Client2015) return nullptr;
    const auto count = *reinterpret_cast<const std::int32_t*>(s_engine + 0x1A0DBE0);
    if (count <= 0 || static_cast<std::uint32_t>(count) > R1ApexSoundVariationCapacity ||
        index >= static_cast<std::uint32_t>(count)) return nullptr;
    return static_cast<const unsigned char*>(s_storage) + kTableOffsets[1] + index * 400ull;
}
