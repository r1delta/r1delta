#pragma once
#include <cstdint>

// A UI-VM script compile error leaves the menus unable to draw. Retail then
// explains the error through the UI it just lost and resets the UI scripts,
// which fail again: a black screen that keeps recompiling. For UI compile
// errors only, this shows a blocking native dialog instead: Retry reloads the
// UI scripts (after the script was fixed), Cancel quits to the desktop. Other
// fatal script errors keep the engine's in-menu explanation.

// Client engine.dll: hooks the fatal-script-error handler (engine+0x1A12D0).
bool InstallUiScriptFailureDialog(std::uintptr_t engineBase);
