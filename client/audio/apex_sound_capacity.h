#pragma once
// Ported unchanged from the singleplayer line (branch sp, commit a1e447a, client/audio/apex_sound_capacity.h). The relocation table and
// install sequence are byte verified against the exact Client2015 engine.dll; see that history for the derivation.
#include <cstdint>

// The native sound packet writes a signed 16-bit alias index. Capacity includes
// index zero; 32767 is the largest positive index compatible with that codec.
inline constexpr std::uint32_t R1ApexSoundAliasCapacity = 32768;
inline constexpr std::uint32_t R1ApexSoundVariationCapacity = 32768;

// Exact Client2015 only, before any sound initialization or manifest admission.
// Allocates owning storage and relocates every verified table consumer, clear
// extent, and capacity check. Does NOT alter sound-bank CRC or signon checks.
// New manifests must remain private-listen/matched-content only; growing storage
// alone preserves the original bank order/identity and wire representation.
bool InstallR1ApexSoundCapacity(std::uintptr_t engineBase);

// Borrowed readonly native records, bounded by the current admitted bank counts
// and the owning allocation. Caller must hold the native audio CS throughout
// use; table addresses are process-lifetime, but bank contents are not.
// Alias records are 512 bytes; variation records are 400 bytes.
const void* R1ApexSoundAliasRecord(std::uint32_t index);
const void* R1ApexSoundVariationRecord(std::uint32_t index);
