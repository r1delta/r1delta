// Restores the console output of client.dll's getpos / getpos_exact, which retail compiled out.
#pragma once

#include <cstdint>

// client.dll was loaded at clientBase: checks the retail prologues of the command callback and its value helper, then
// hooks the callback (enabled by the caller's MH_EnableHook(MH_ALL_HOOKS)). Warns and leaves the command alone on a
// mismatch.
void InstallGetPosCommandOutput(std::uintptr_t clientBase);
