#pragma once

#include <cstdint>

// Server side of the packaged R2 AI graphs. With developer > 0 the retail server.dll compares the graph header CRC
// with a CRC over the BSP lumps and the AI node entity text, answers "out of date" on a mismatch and the game then
// rebuilds its own graph. A converted map never matches, so when the provider has just served an AI graph package
// (ConsumeServedAinPackage) the mismatch answer is turned into "valid". Stock maps are unaffected.
void InstallServerAinCrcGuard(std::uintptr_t serverBase);
