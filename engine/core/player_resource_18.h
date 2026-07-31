#pragma once

#include <cstdint>

// Extends the R1 2015 client and the shared TFO server/server_local
// CPlayerResource layouts from element zero plus 16 players to element zero
// plus 18 players. Listen, legacy dedicated, and R1O fake-dedicated modes use
// that same server binary, so its wire schema must match the client in every
// mode.
//
// These are binary-version-specific installers. They validate the expected
// instructions and runtime network-table shapes before changing anything.
bool InstallR1ClientPlayerResource18(std::uintptr_t clientBase);
bool InstallTFOServerPlayerResource18(std::uintptr_t serverBase);
