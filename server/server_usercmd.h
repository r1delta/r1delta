#pragma once

#include <cstdint>

// Shared engine registration: also called on clients that never load server.dll.
bool RegisterMovementConVars();
void RegisterServerUserCmdConVars();
void InstallServerUserCmdHooks(uintptr_t serverBase);
