#include "ui_script_failure.h"
#include "cvar.h"
#include "logging.h"
#include "vsdk/public/tier0/platformtime.h"
#include <MinHook.h>
#include <windows.h>
#include <cstdio>
#include <cstring>

namespace {

// engine.dll (R1 2015) sub_1801A12D0, run every frame by CEngine: once the
// timer at qword_1830EB8D8 (armed by a fatal script error) expires it explains
// the error, then queues "gameui_hide; disconnect; gameui_activate;
// uiscript_reset".
constexpr std::uintptr_t kFatalScriptErrorHandlerRva = 0x1A12D0;
constexpr std::uintptr_t kFatalScriptErrorTimerRva = 0x30EB8D8;
constexpr unsigned char kHandlerPrologue[] = {
	0x48, 0x83, 0xEC, 0x38,                         // sub rsp, 38h
	0xF2, 0x0F, 0x10, 0x05, 0xFC, 0xA5, 0xF4, 0x02, // movsd xmm0, [timer]
};

using FatalScriptErrorHandlerFn = void (*)();
FatalScriptErrorHandlerFn g_original = nullptr;
double* g_timer = nullptr;

HWND GameWindow() {
	HWND window = FindWindowA("Respawn001", nullptr);
	DWORD pid = 0;
	if (window)
		GetWindowThreadProcessId(window, &pid);
	return pid == GetCurrentProcessId() ? window : nullptr;
}

// The error that armed the timer, as the engine stores it for its prompt.
const char* FatalScriptErrorText() {
	const ConVarR1* prompt = OriginalCCVar_FindVar
		? OriginalCCVar_FindVar(cvarinterface, "fatal_script_error_prompt")
		: nullptr;
	if (prompt && prompt->m_pParent)
		prompt = prompt->m_pParent;
	return prompt && prompt->m_Value.m_pszString ? prompt->m_Value.m_pszString : "";
}

void FatalScriptErrorHandler() {
	if (!g_timer || *g_timer <= 0.0 || Plat_FloatTime() <= *g_timer) {
		g_original();
		return;
	}
	const char* detail = FatalScriptErrorText();
	if (!strstr(detail, "UI SCRIPT COMPILE ERROR")) {
		g_original();
		return;
	}
	*g_timer = -1.0;

	char text[4096];
	_snprintf_s(text, sizeof(text), _TRUNCATE,
		"The menu (UI) scripts failed to load, so the game cannot show its menus.\n\n"
		"%s\n\n"
		"Fix the script, then press Retry to reload the UI scripts.\n"
		"Press Cancel to quit to the desktop.",
		detail);
	Warning("R1Delta: UI scripts failed to load:\n%s\n", detail);

	const int choice = MessageBoxA(GameWindow(), text, "R1Delta - UI script error",
		MB_RETRYCANCEL | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
	if (choice == IDRETRY)
		Cbuf_AddText(0, "uiscript_reset\n", 0);
	else
		Cbuf_AddText(0, "quit\n", 0);
}

}

bool InstallUiScriptFailureDialog(std::uintptr_t engineBase) {
	if (!engineBase)
		return false;
	auto* target = reinterpret_cast<unsigned char*>(engineBase + kFatalScriptErrorHandlerRva);
	if (memcmp(target, kHandlerPrologue, sizeof(kHandlerPrologue)) != 0) {
		Warning("R1Delta: fatal script error handler did not match; UI script errors keep the retail path\n");
		return false;
	}
	g_timer = reinterpret_cast<double*>(engineBase + kFatalScriptErrorTimerRva);
	if (MH_CreateHook(target, reinterpret_cast<void*>(&FatalScriptErrorHandler),
			reinterpret_cast<void**>(&g_original)) != MH_OK
		|| MH_EnableHook(target) != MH_OK) {
		Warning("R1Delta: could not hook the fatal script error handler\n");
		return false;
	}
	return true;
}
