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
