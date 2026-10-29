#pragma once
#include <cstdint>

// Legacy (2014) dedicated engine sound-alias table expansion.
//
// engine_ds.dll hard-caps its sound-alias tables at 16,628 alias records and
// 19,757/19,760 variation records, while current R1 scripts load 18,396 aliases
// and 22,106 variations through the client engine. The dedicated server
// therefore truncates its table ("Too many sound aliases"), computes a
// sound-script CRC that can never equal a client's, and retail's ServerInfo
// check ("Server and client sound scripts do not match. Disconnecting") rejects
// every join.
//
// This relocates the three BSS tables into a larger zeroed block reachable by
// the engine's RIP-relative references and raises the alias capacity, so the DS
// loads the full script set and publishes the same CRC as a client.
//
// Must be called after engine_ds.dll is loaded and before its first sound
// script load. Returns false without touching code when the image or any
// verified site does not match.
bool InstallLegacyDSSoundTableExpansion(std::uintptr_t engineDSBase);

// Alias records the engine uses after a successful expansion; null when the
// expansion did not run, in which case the link-time table is still in use.
// The loaded alias count stays in the engine's own global.
const void* GetLegacyDSAliasRecords();

// The generated R2 aliases (scripts/r2port_sounds.txt, the last manifest file) are folded into the dedicated engine's sound-script
// CRC like every other alias, which clients compare at connect. This detours the per-file script loader (+5F20), remembers the CRC
// and alias/range counts before that file and restores the CRC afterwards (same scheme as the client, audio_cache.cpp ScriptLoad).
// Returns false without touching code when the prologue does not match. Call with the table expansion, before the first script load.
bool InstallLegacyDSAliasHiding(std::uintptr_t engineDSBase);
// Aliases appended after the CRC/parity-visible table by the generated R2 script.
std::uint32_t LegacyDSHiddenTrailingAliases();

