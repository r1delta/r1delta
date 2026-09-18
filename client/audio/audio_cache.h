#pragma once
#include <cstdint>

// Client2015 only; verifies the exact R1 engine image and every target prologue.
bool InstallR1AudioCacheHooks(std::uintptr_t engineBase);
// Call after native cvar initialization. Rebuild uses native sound_reboot's
// stop/drain/destroy boundary, then rescans overrides and reloads manifests.
void RegisterR1AudioCacheCommands();
// True when the mounted content is the generated-audio (Ogg) delta set. When
// false this is the retail WAV set and the native audio path must stay intact.
bool R1AudioUsesGeneratedContent();
