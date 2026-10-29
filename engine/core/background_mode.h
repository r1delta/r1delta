#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <string>

// Opt-in background test mode for agent and CI game windows. It is active only when the environment
// variable R1DELTA_BACKGROUND is set to something other than "0", or the command line contains
// -r1delta_background. Normal players and hand-launched games are untouched.
//
// In this mode the process must not disturb the person using the machine: the game window never takes
// focus, topmost state or the mouse, is parked off-screen at (-32000,-32000) (R1DELTA_BACKGROUND_WINDOW=minimized
// keeps it minimized instead), runs at fps_max 60, writes the crash log without opening notepad or playing the
// crash sound, and never shows a
// modal message box. The -nomessagebox switch suppresses message boxes in every mode.
// See docs/r2port.md ("Background test mode").
namespace r1delta::background {

// True when the background mode is on. Cached after the first call; cheap and safe from any thread,
// including the crash handler.
bool IsActive();

// True when message boxes must not be shown: background mode or -nomessagebox on the command line.
bool ShouldSuppressMessageBoxes();

// If message boxes are suppressed, logs the text and caption, stores the result a user would have chosen
// to continue (IDOK/IDYES/...) in `result` and returns true; otherwise returns false and the real box runs.
bool TrySuppressMessageBoxA(LPCSTR text, LPCSTR caption, UINT type, int& result);
bool TrySuppressMessageBoxW(LPCWSTR text, LPCWSTR caption, UINT type, int& result);

// Creates the user32 hooks (focus, topmost, cursor, input, window creation). Does nothing unless active.
// Call after MH_Initialize and before the MH_EnableHook(MH_ALL_HOOKS) that enables them.
void InstallHooks();

// Appends the engine switches background mode implies (-nosound -nomouse -nomessagebox,
// +cl_mouseenable 0 +cl_mouselook 0 +fps_max 60 +mat_vsync 0) to the engine command line text.
void AppendEngineArguments(std::string& commandLine);

// Starts the thread that re-applies fps_max 60 and the input cvars once a second so cfg files cannot
// override them. Does nothing unless active, and never runs for dedicated servers.
void StartCvarWatchdog();

// Registers r1d_background_selftest (messagebox | crash | status) for verifying this mode. Only
// registered when active.
void RegisterSelfTestCommand();

} // namespace r1delta::background
