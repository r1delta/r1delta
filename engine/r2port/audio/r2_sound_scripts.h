// Generated R1 sound scripts and soundscape entity rewrite for the converted R2 maps.
//
// R2 names its ambient emitters (ambient_generic soundName) and room tones (trigger_soundscape play_event) after
// Miles events that R1 does not have. This module resolves those events in the player's own R2 sound bank and writes
// the R1 equivalents (all text is generated at runtime from the R2 install and cached in memory only):
//   scripts/r2port_sounds.txt             one R1 sound alias per R2-only event, waves named r2port/<source>.wav
//                                         (decoded on demand by r2_audio_source, never written to disk)
//   scripts/soundscapes_<map>.txt         R1 soundscapes "r2ss_<event>" (room tone loop + automatic DSP) and "r2ss_dsp"
//   trigger_soundscape rewrite            gives each R2 trigger the `soundscape` key R1 reads and appends the
//                                         env_soundscape_triggerable entities it points at.
// Names that R1 already defines (case-insensitive) are never redefined: R1 content wins.
//
// Client and server must produce identical alias text (R1 compares a CRC of the sound scripts at connect), so everything
// here is a pure function of the R2 bank, the embedded name table and the map list.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace r2audio {

class SoundBank;

// The generated aliases are appended to the engine's sound alias table, whose capacity is fixed unless the engine relocated
// it (client: apex_sound_capacity, legacy dedicated: legacy_ds_sound_tables). Client and server must generate the SAME alias
// list (the alias index is on the wire), so it is always the union of all allowlisted maps and is only served when the
// engine in this process reported room for it.
void SetAliasCapacityOk(bool ok);

// Maps whose sounds are generated: every allowlisted map. The environment variable R1DELTA_R2PORT_SOUND_MAPS ("all", "none" or
// a comma separated list) overrides it for testing, and also bypasses the capacity requirement.
std::vector<std::string> EnabledSoundMaps();
// False when the alias file must not be served (no relocated alias table in this process and no test override).
bool SoundAliasesAllowed();
// R2 map sounds are ON by default; env R2PORT_SOUND=0 turns the whole path off (no alias file served, no soundscape trigger rewrite, no relocated
// alias table use), so the process behaves like a build without the feature.
bool SoundEnabled();

struct AliasReport {
    unsigned aliases = 0;          // aliases written
    unsigned waves = 0;            // wave entries written (R1 counts these as variations)
    unsigned skippedR1 = 0;        // names R1 already defines
    unsigned unresolved = 0;       // names missing from the bank or not parseable
    std::vector<std::string> unresolvedNames;
};

// Alias script for the union of `maps` (sorted, deduplicated). Empty string when nothing is defined.
std::string BuildAliasScript(const SoundBank& bank, const std::vector<std::string>& maps, AliasReport* report);

// Soundscape file for one map (empty when the map has no generated room tones and needs no DSP soundscape).
std::string BuildSoundscapeScript(const SoundBank& bank, std::string_view map);

// True when the event `name` resolves in the bank (used to decide which triggers can be wired).
bool EventDefined(const SoundBank& bank, std::string_view name);

// Rewrites the trigger_soundscape entities of a `_snd.ent` text of `map`. Idempotent. `changed` receives the number of
// triggers wired. Text it cannot parse is returned unchanged.
std::string RewriteSoundscapeTriggers(const SoundBank& bank, std::string_view entityText, std::string_view map,
                                      std::uint64_t* changed = nullptr);

// Wave name used in the generated aliases for source `lowerSourceName`.
std::string MakeWaveName(std::string_view lowerSourceName);
// Inverse of MakeWaveName on a canonical R1 sound path ("sound/r2port/<source>.wav"). False for other paths.
bool ParseWaveName(std::string_view canonicalPath, std::string& lowerSourceName);

}  // namespace r2audio
