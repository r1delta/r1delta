#pragma once
#include <cstdint>

// R1O fake dedicated server (TFO engine_r1o.dll): sound alias tables large enough for the generated R2 sound aliases
// (scripts/r2port_sounds.txt, 222 aliases appended after the 18,396 stock ones; the stock tables hold 18,552 aliases / 22,204 variations
// and the engine drops everything beyond them with "Too many sound aliases" / "Exceeded MAX_SOUND_RANGES").
//
// The alias index is on the wire (CServerSound::EmitSound writes the load-order index as a signed 16-bit value into SVC_Sounds, the client
// engine indexes its own table with it), so the server must hold the SAME list at the SAME indexes as an R2-installed client. This module
//  - relocates the three static tables (names, variations, aliases) into one near allocation and enlarges every capacity check and
//    clear extent that bounds them (exact binary pinned by PE identity and SHA256; every patched instruction is byte verified first and
//    the whole set is rolled back on any mismatch),
//  - detours the per-file sound script loader (+0x44CA0) so the sound-script CRC and the parity-visible alias count stop before
//    scripts/r2port_sounds.txt (same scheme as the client's audio_cache.cpp ScriptLoad and the legacy dedicated server), keeping the
//    tail invisible to delta_soundtable_check and to a stock client's CRC while the aliases stay in the table.
//
// Without a Titanfall 2 install no R2 alias file is produced: the server holds exactly the stock table (no hidden tail), which is what a
// client without R2 holds as well, and an R2 client keeps its tail hidden. Nothing here changes the wire format.
bool InstallR1OSoundTables(std::uintptr_t engineBase);

// Relocated tables, or nullptr when the expansion is not installed (the link-time addresses are in use then).
const void* GetR1OSoundNameEntries();   // { const char* name; int index; int pad } x count, sorted by name once the manifest is loaded
const void* GetR1OSoundAliasRecords();  // 512 byte records, record+0 = index, record+0x1E0 = interned name

// Aliases loaded after (and including) scripts/r2port_sounds.txt; they trail the parity-visible table.
std::uint32_t R1OHiddenTrailingAliases();
