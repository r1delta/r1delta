#include "engine/core/background_mode.h"

#include <cctype>

#include "engine/core/core.h"
#include "engine/cvar.h"
#include "engine/logging/logging.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace r1delta::background {
namespace {

// Off-screen parking spot for R1DELTA_BACKGROUND_WINDOW=offscreen. Windows treats -32000 as the
// coordinate of a minimized window, so no monitor ever shows it.
constexpr int kOffscreenCoordinate = -32000;

enum class WindowMode { Minimized, OffScreen };

std::atomic<int> g_active{ -1 };            // -1 unresolved, 0 off, 1 on
std::atomic<int> g_messageBoxesOff{ -1 };
// Off-screen is the default: a minimized window has a 0x0 client area (no client-area grabs), while the
// off-screen one keeps its full size, renders at 60 fps and can be captured. R1DELTA_BACKGROUND_WINDOW=minimized
// selects minimized instead (it works after start-up; creating the window already minimized crashes graphics init).
WindowMode g_windowMode = WindowMode::OffScreen;
bool g_hooksInstalled = false;
thread_local bool t_inMessageBoxLog = false;

bool EnvironmentFlagSet(const char* name)
{
    char value[16]{};
    const DWORD length = GetEnvironmentVariableA(name, value, static_cast<DWORD>(sizeof(value)));
    if (length == 0 || length >= sizeof(value))
        return length >= sizeof(value);
    return std::strcmp(value, "0") != 0;
}

bool CommandLineHas(const char* flag)
{
    const char* commandLine = GetCommandLineA();
    return commandLine && std::strstr(commandLine, flag) != nullptr;
}

// An agent root carries an empty r1delta_background.flag next to the game executable (.build\r2-root.ps1 creates
// it for the agent roots only), so every launch from that root is a background launch whatever script started it.
// Only the executable's own directory is checked: the working directory is shared (C:\whatever) and must not switch
// anybody's launch into background mode.
bool MarkerFileExists()
{
    char path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameA(nullptr, path, static_cast<DWORD>(sizeof(path)));
    if (!length || length >= sizeof(path))
        return false;
    char* slash = std::strrchr(path, '\\');
    if (!slash)
        return false;
    *slash = '\0';
    if (std::strlen(path) + std::strlen("\\r1delta_background.flag") >= sizeof(path))
        return false;
    std::strcat(path, "\\r1delta_background.flag");
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

std::string NarrowForLog(LPCWSTR text)
{
    if (!text)
        return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1)
        return {};
    std::string narrow(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, narrow.data(), length, nullptr, nullptr);
    narrow.resize(static_cast<size_t>(length) - 1);
    return narrow;
}

// The answer that lets the program carry on, the way an unattended click on the default button would.
int ContinueResult(UINT type)
{
    switch (type & MB_TYPEMASK) {
    case MB_OKCANCEL: return IDOK;
    case MB_ABORTRETRYIGNORE: return IDIGNORE;
    case MB_YESNOCANCEL: return IDYES;
    case MB_YESNO: return IDYES;
    case MB_RETRYCANCEL: return IDCANCEL;
    case MB_CANCELTRYCONTINUE: return IDCONTINUE;
    default: return IDOK;
    }
}

void LogSuppressedMessageBox(const char* text, const char* caption, UINT type, int result)
{
    char buffer[2048]{};
    _snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
        "R1Delta: message box suppressed (type=0x%x result=%d) caption=\"%s\" text=\"%s\"\n",
        type, result, caption ? caption : "", text ? text : "");
    OutputDebugStringA(buffer);
    if (t_inMessageBoxLog)
        return;
    t_inMessageBoxLog = true;
    Msg("%s", buffer);
    t_inMessageBoxLog = false;
}

// ---- window and input hooks (background mode only) -------------------------------------------------

using ShowWindowFn = BOOL(WINAPI*)(HWND, int);
using SetWindowPosFn = BOOL(WINAPI*)(HWND, HWND, int, int, int, int, UINT);
using MoveWindowFn = BOOL(WINAPI*)(HWND, int, int, int, int, BOOL);
using SetWindowPlacementFn = BOOL(WINAPI*)(HWND, const WINDOWPLACEMENT*);
using SetForegroundWindowFn = BOOL(WINAPI*)(HWND);
using BringWindowToTopFn = BOOL(WINAPI*)(HWND);
using SetActiveWindowFn = HWND(WINAPI*)(HWND);
using SetFocusFn = HWND(WINAPI*)(HWND);
using SwitchToThisWindowFn = void(WINAPI*)(HWND, BOOL);
using ClipCursorFn = BOOL(WINAPI*)(const RECT*);
using SetCursorPosFn = BOOL(WINAPI*)(int, int);
using SetCaptureFn = HWND(WINAPI*)(HWND);
using MouseEventFn = void(WINAPI*)(DWORD, DWORD, DWORD, DWORD, ULONG_PTR);
using SendInputFn = UINT(WINAPI*)(UINT, LPINPUT, int);
using RegisterRawInputDevicesFn = BOOL(WINAPI*)(PCRAWINPUTDEVICE, UINT, UINT);
using CreateWindowExAFn = HWND(WINAPI*)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
using CreateWindowExWFn = HWND(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
using SetWindowLongPtrFn = LONG_PTR(WINAPI*)(HWND, int, LONG_PTR);
using SetWindowLongFn = LONG(WINAPI*)(HWND, int, LONG);

ShowWindowFn oShowWindow;
ShowWindowFn oShowWindowAsync;
SetWindowPosFn oSetWindowPos;
MoveWindowFn oMoveWindow;
SetWindowPlacementFn oSetWindowPlacement;
SetForegroundWindowFn oSetForegroundWindow;
BringWindowToTopFn oBringWindowToTop;
SetActiveWindowFn oSetActiveWindow;
SetFocusFn oSetFocus;
SwitchToThisWindowFn oSwitchToThisWindow;
ClipCursorFn oClipCursor;
SetCursorPosFn oSetCursorPos;
SetCaptureFn oSetCapture;
MouseEventFn oMouseEvent;
SendInputFn oSendInput;
RegisterRawInputDevicesFn oRegisterRawInputDevices;
CreateWindowExAFn oCreateWindowExA;
CreateWindowExWFn oCreateWindowExW;
SetWindowLongPtrFn oSetWindowLongPtrA;
SetWindowLongPtrFn oSetWindowLongPtrW;
SetWindowLongFn oSetWindowLongA;
SetWindowLongFn oSetWindowLongW;

bool IsTopLevelWindow(HWND window)
{
    if (!window || !IsWindow(window))
        return false;
    return (GetWindowLongPtrW(window, GWL_STYLE) & WS_CHILD) == 0;
}

// Minimized mode keeps every shown window minimized and inactive; off-screen mode shows it without
// activation (the position is forced off-screen separately). SW_HIDE passes through.
int MapShowCommand(int command)
{
    if (command == SW_HIDE)
        return command;
    return g_windowMode == WindowMode::Minimized ? SW_SHOWMINNOACTIVE : SW_SHOWNOACTIVATE;
}

DWORD NoActivateExStyle(DWORD exStyle)
{
    return (exStyle | WS_EX_NOACTIVATE) & ~static_cast<DWORD>(WS_EX_TOPMOST);
}

BOOL WINAPI ShowWindowHook(HWND window, int command)
{
    return oShowWindow(window, IsTopLevelWindow(window) ? MapShowCommand(command) : command);
}

BOOL WINAPI ShowWindowAsyncHook(HWND window, int command)
{
    return oShowWindowAsync(window, IsTopLevelWindow(window) ? MapShowCommand(command) : command);
}

BOOL WINAPI SetWindowPosHook(HWND window, HWND insertAfter, int x, int y, int cx, int cy, UINT flags)
{
    if (insertAfter == HWND_TOPMOST)
        insertAfter = HWND_NOTOPMOST;
    flags |= SWP_NOACTIVATE;
    if (g_windowMode == WindowMode::OffScreen && !(flags & SWP_NOMOVE) && IsTopLevelWindow(window)) {
        x = kOffscreenCoordinate;
        y = kOffscreenCoordinate;
    }
    return oSetWindowPos(window, insertAfter, x, y, cx, cy, flags);
}

BOOL WINAPI MoveWindowHook(HWND window, int x, int y, int width, int height, BOOL repaint)
{
    if (g_windowMode == WindowMode::OffScreen && IsTopLevelWindow(window)) {
        x = kOffscreenCoordinate;
        y = kOffscreenCoordinate;
    }
    return oMoveWindow(window, x, y, width, height, repaint);
}

BOOL WINAPI SetWindowPlacementHook(HWND window, const WINDOWPLACEMENT* placement)
{
    if (!placement || placement->length != sizeof(WINDOWPLACEMENT) || !IsTopLevelWindow(window))
        return oSetWindowPlacement(window, placement);
    WINDOWPLACEMENT adjusted = *placement;
    adjusted.showCmd = static_cast<UINT>(MapShowCommand(static_cast<int>(adjusted.showCmd)));
    if (g_windowMode == WindowMode::OffScreen) {
        const LONG width = adjusted.rcNormalPosition.right - adjusted.rcNormalPosition.left;
        const LONG height = adjusted.rcNormalPosition.bottom - adjusted.rcNormalPosition.top;
        adjusted.rcNormalPosition = { kOffscreenCoordinate, kOffscreenCoordinate,
            kOffscreenCoordinate + width, kOffscreenCoordinate + height };
    }
    return oSetWindowPlacement(window, &adjusted);
}

BOOL WINAPI SetForegroundWindowHook(HWND) { return TRUE; }
BOOL WINAPI BringWindowToTopHook(HWND) { return TRUE; }
HWND WINAPI SetActiveWindowHook(HWND) { return GetActiveWindow(); }
HWND WINAPI SetFocusHook(HWND) { return GetFocus(); }
void WINAPI SwitchToThisWindowHook(HWND, BOOL) {}
BOOL WINAPI ClipCursorHook(const RECT*) { return TRUE; }
BOOL WINAPI SetCursorPosHook(int, int) { return TRUE; }
HWND WINAPI SetCaptureHook(HWND) { return GetCapture(); }
void WINAPI MouseEventHook(DWORD, DWORD, DWORD, DWORD, ULONG_PTR) {}

UINT WINAPI SendInputHook(UINT, LPINPUT, int)
{
    SetLastError(ERROR_ACCESS_DENIED);
    return 0;
}

// The engine's input system would read the mouse through WM_INPUT even without focus. Report success
// without registering, so no raw input is ever delivered.
BOOL WINAPI RegisterRawInputDevicesHook(PCRAWINPUTDEVICE, UINT, UINT) { return TRUE; }

HWND WINAPI CreateWindowExAHook(DWORD exStyle, LPCSTR className, LPCSTR windowName, DWORD style,
    int x, int y, int width, int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID param)
{
    if (!parent && !(style & WS_CHILD)) {
        exStyle = NoActivateExStyle(exStyle);
        if (g_windowMode == WindowMode::OffScreen) {
            x = kOffscreenCoordinate;
            y = kOffscreenCoordinate;
        }
    }
    return oCreateWindowExA(exStyle, className, windowName, style, x, y, width, height, parent, menu, instance, param);
}

HWND WINAPI CreateWindowExWHook(DWORD exStyle, LPCWSTR className, LPCWSTR windowName, DWORD style,
    int x, int y, int width, int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID param)
{
    if (parent != HWND_MESSAGE && !parent && !(style & WS_CHILD)) {
        exStyle = NoActivateExStyle(exStyle);
        if (g_windowMode == WindowMode::OffScreen) {
            x = kOffscreenCoordinate;
            y = kOffscreenCoordinate;
        }
    }
    return oCreateWindowExW(exStyle, className, windowName, style, x, y, width, height, parent, menu, instance, param);
}

LONG_PTR WINAPI SetWindowLongPtrAHook(HWND window, int index, LONG_PTR value)
{
    if (index == GWL_EXSTYLE)
        value = static_cast<LONG_PTR>(NoActivateExStyle(static_cast<DWORD>(value)));
    return oSetWindowLongPtrA(window, index, value);
}

LONG_PTR WINAPI SetWindowLongPtrWHook(HWND window, int index, LONG_PTR value)
{
    if (index == GWL_EXSTYLE)
        value = static_cast<LONG_PTR>(NoActivateExStyle(static_cast<DWORD>(value)));
    return oSetWindowLongPtrW(window, index, value);
}

LONG WINAPI SetWindowLongAHook(HWND window, int index, LONG value)
{
    if (index == GWL_EXSTYLE)
        value = static_cast<LONG>(NoActivateExStyle(static_cast<DWORD>(value)));
    return oSetWindowLongA(window, index, value);
}

LONG WINAPI SetWindowLongWHook(HWND window, int index, LONG value)
{
    if (index == GWL_EXSTYLE)
        value = static_cast<LONG>(NoActivateExStyle(static_cast<DWORD>(value)));
    return oSetWindowLongW(window, index, value);
}

template <typename Fn>
void CreateUser32Hook(HMODULE user32, const char* name, void* detour, Fn& original)
{
    void* target = reinterpret_cast<void*>(GetProcAddress(user32, name));
    if (!target) {
        char buffer[160]{};
        _snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "R1Delta: background mode: user32!%s not found\n", name);
        OutputDebugStringA(buffer);
        return;
    }
    const MH_STATUS status = MH_CreateHook(target, detour, reinterpret_cast<LPVOID*>(&original));
    if (status != MH_OK && status != MH_ERROR_ALREADY_CREATED) {
        char buffer[160]{};
        _snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
            "R1Delta: background mode: hook of user32!%s failed (status %d)\n", name, static_cast<int>(status));
        OutputDebugStringA(buffer);
    }
}

// ---- cvar watchdog -------------------------------------------------------------------------------

void AppendCommand(char* commands, size_t size, const char* text)
{
    strncat_s(commands, size, text, _TRUNCATE);
}

// The engine sleeps ~50 ms per frame (about 20 fps) while it believes it is not the active app, which an
// unfocused background window always is. Tell its window it is active; input stays blocked by the hooks above.
BOOL CALLBACK PostActivateToOwnWindow(HWND window, LPARAM)
{
    DWORD processId = 0;
    const DWORD threadId = GetWindowThreadProcessId(window, &processId);
    if (processId == GetCurrentProcessId() && (IsWindowVisible(window) || IsIconic(window)))
        PostMessageW(window, WM_ACTIVATEAPP, TRUE, static_cast<LPARAM>(threadId));
    return TRUE;
}

// Result of one guarded cvar read. The watchdog also runs while the game shuts down, when client.dll (which owns most
// ConVar objects) is already unmapped and the cvar list still points into it: the lookup then faults. A fault means the
// cvar system is gone, so the thread stops.
enum class CvarCheck { Matches, Differs, Missing, Gone };

CvarCheck CheckFloatCvar(const char* name, float wanted)
{
    __try {
        ConVarR1* variable = OriginalCCVar_FindVar(cvarinterface, name);
        if (!variable)
            return CvarCheck::Missing;
        return variable->m_Value.m_fValue != wanted ? CvarCheck::Differs : CvarCheck::Matches;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return CvarCheck::Gone;
    }
}

CvarCheck CheckIntCvar(const char* name, int wanted)
{
    __try {
        ConVarR1* variable = OriginalCCVar_FindVar(cvarinterface, name);
        if (!variable)
            return CvarCheck::Missing;
        return variable->m_Value.m_nValue != wanted ? CvarCheck::Differs : CvarCheck::Matches;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return CvarCheck::Gone;
    }
}

DWORD WINAPI CvarWatchdogMain(LPVOID)
{
    for (unsigned tick = 0;; ++tick) {
        Sleep(500);
        EnumWindows(&PostActivateToOwnWindow, 0);
        if ((tick & 1) == 0 || !IsCvarInterfacePublished() || !cvarinterface || !OriginalCCVar_FindVar)
            continue;

        char commands[256]{};
        const CvarCheck fps = CheckFloatCvar("fps_max", 60.0f);
        if (fps == CvarCheck::Gone)
            return 0;
        if (fps == CvarCheck::Differs)
            AppendCommand(commands, sizeof(commands), "fps_max 60\n");
        const CvarCheck vsync = CheckIntCvar("mat_vsync", 0);
        if (vsync == CvarCheck::Gone)
            return 0;
        if (vsync == CvarCheck::Differs)
            AppendCommand(commands, sizeof(commands), "mat_vsync 0\n");
        if (commands[0])
            Cbuf_AddText(0, commands, 0);
    }
}

// ---- self test command ---------------------------------------------------------------------------

void SelfTestCommand(const CCommand& command)
{
    const char* mode = command.ArgC() > 1 ? command.ArgV()[1] : "status";
    if (std::strcmp(mode, "messagebox") == 0) {
        const auto started = std::chrono::steady_clock::now();
        const int resultA = MessageBoxA(nullptr, "r1d_background_selftest: modal box (A)", "R1Delta self test", MB_YESNO | MB_ICONERROR | MB_TOPMOST);
        const int resultW = MessageBoxW(nullptr, L"r1d_background_selftest: modal box (W)", L"R1Delta self test", MB_OKCANCEL | MB_ICONWARNING);
        const int resultExA = MessageBoxExA(nullptr, "r1d_background_selftest: modal box (ExA)", "R1Delta self test", MB_RETRYCANCEL, 0);
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        Msg("r1d_background_selftest messagebox: MessageBoxA=%d (IDYES=%d) MessageBoxW=%d (IDOK=%d) MessageBoxExA=%d (IDCANCEL=%d) elapsed=%lld ms\n",
            resultA, IDYES, resultW, IDOK, resultExA, IDCANCEL, static_cast<long long>(elapsed));
    }
    else if (std::strcmp(mode, "crash") == 0) {
        Msg("r1d_background_selftest: writing through a null pointer now\n");
        *reinterpret_cast<volatile int*>(static_cast<uintptr_t>(0)) = 1;
    }
    else if (std::strcmp(mode, "hooks") == 0) {
        // A MinHook-patched function starts with a relative jump (E9).
        HMODULE user32 = GetModuleHandleA("user32.dll");
        static const char* const names[] = { "ShowWindow", "ShowWindowAsync", "SetWindowPos", "MoveWindow", "SetWindowPlacement",
            "SetForegroundWindow", "BringWindowToTop", "SetActiveWindow", "SetFocus", "SwitchToThisWindow", "ClipCursor",
            "SetCursorPos", "SetCapture", "mouse_event", "SendInput", "RegisterRawInputDevices", "CreateWindowExA",
            "CreateWindowExW", "SetWindowLongPtrA", "SetWindowLongPtrW", "SetWindowLongA", "SetWindowLongW",
            "MessageBoxA", "MessageBoxW", "MessageBoxExA", "MessageBoxExW" };
        std::string report;
        int hooked = 0;
        for (const char* name : names) {
            const auto* bytes = reinterpret_cast<const unsigned char*>(GetProcAddress(user32, name));
            const bool patched = bytes && bytes[0] == 0xE9;
            hooked += patched ? 1 : 0;
            if (!patched) {
                report += ' ';
                report += name;
            }
        }
        Msg("r1d_background_selftest hooks: %d of %d user32 functions patched; not patched:%s\n",
            hooked, static_cast<int>(sizeof(names) / sizeof(names[0])), report.empty() ? " none" : report.c_str());
    }
    else {
        const HWND foreground = GetForegroundWindow();
        DWORD foregroundPid = 0;
        GetWindowThreadProcessId(foreground, &foregroundPid);
        POINT cursor{};
        GetCursorPos(&cursor);
        Msg("r1d_background_selftest status: active=1 window=%s foreground-pid=%lu own-pid=%lu cursor=%ld,%ld\n",
            g_windowMode == WindowMode::Minimized ? "minimized" : "offscreen",
            static_cast<unsigned long>(foregroundPid), static_cast<unsigned long>(GetCurrentProcessId()),
            static_cast<long>(cursor.x), static_cast<long>(cursor.y));
    }
}

} // namespace

bool IsActive()
{
    int state = g_active.load(std::memory_order_relaxed);
    if (state < 0) {
        state = (EnvironmentFlagSet("R1DELTA_BACKGROUND") || CommandLineHas("-r1delta_background") || MarkerFileExists()) ? 1 : 0;
        g_active.store(state, std::memory_order_relaxed);
    }
    return state == 1;
}

bool ShouldSuppressMessageBoxes()
{
    if (IsActive())
        return true;
    int state = g_messageBoxesOff.load(std::memory_order_relaxed);
    if (state < 0) {
        state = CommandLineHas("-nomessagebox") ? 1 : 0;
        g_messageBoxesOff.store(state, std::memory_order_relaxed);
    }
    return state == 1;
}

bool TrySuppressMessageBoxA(LPCSTR text, LPCSTR caption, UINT type, int& result)
{
    if (!ShouldSuppressMessageBoxes())
        return false;
    result = ContinueResult(type);
    LogSuppressedMessageBox(text, caption, type, result);
    return true;
}

bool TrySuppressMessageBoxW(LPCWSTR text, LPCWSTR caption, UINT type, int& result)
{
    if (!ShouldSuppressMessageBoxes())
        return false;
    result = ContinueResult(type);
    const std::string narrowText = NarrowForLog(text);
    const std::string narrowCaption = NarrowForLog(caption);
    LogSuppressedMessageBox(narrowText.c_str(), narrowCaption.c_str(), type, result);
    return true;
}

void InstallHooks()
{
    if (!IsActive() || g_hooksInstalled)
        return;
    g_hooksInstalled = true;

    char mode[16]{};
    if (GetEnvironmentVariableA("R1DELTA_BACKGROUND_WINDOW", mode, sizeof(mode)) && _stricmp(mode, "minimized") == 0)
        g_windowMode = WindowMode::Minimized;

    HMODULE user32 = GetModuleHandleA("user32.dll");
    if (!user32)
        user32 = LoadLibraryA("user32.dll");
    if (!user32)
        return;

    CreateUser32Hook(user32, "ShowWindow", &ShowWindowHook, oShowWindow);
    CreateUser32Hook(user32, "ShowWindowAsync", &ShowWindowAsyncHook, oShowWindowAsync);
    CreateUser32Hook(user32, "SetWindowPos", &SetWindowPosHook, oSetWindowPos);
    CreateUser32Hook(user32, "MoveWindow", &MoveWindowHook, oMoveWindow);
    CreateUser32Hook(user32, "SetWindowPlacement", &SetWindowPlacementHook, oSetWindowPlacement);
    CreateUser32Hook(user32, "SetForegroundWindow", &SetForegroundWindowHook, oSetForegroundWindow);
    CreateUser32Hook(user32, "BringWindowToTop", &BringWindowToTopHook, oBringWindowToTop);
    CreateUser32Hook(user32, "SetActiveWindow", &SetActiveWindowHook, oSetActiveWindow);
    CreateUser32Hook(user32, "SetFocus", &SetFocusHook, oSetFocus);
    CreateUser32Hook(user32, "SwitchToThisWindow", &SwitchToThisWindowHook, oSwitchToThisWindow);
    CreateUser32Hook(user32, "ClipCursor", &ClipCursorHook, oClipCursor);
    CreateUser32Hook(user32, "SetCursorPos", &SetCursorPosHook, oSetCursorPos);
    CreateUser32Hook(user32, "SetCapture", &SetCaptureHook, oSetCapture);
    CreateUser32Hook(user32, "mouse_event", &MouseEventHook, oMouseEvent);
    CreateUser32Hook(user32, "SendInput", &SendInputHook, oSendInput);
    CreateUser32Hook(user32, "RegisterRawInputDevices", &RegisterRawInputDevicesHook, oRegisterRawInputDevices);
    CreateUser32Hook(user32, "CreateWindowExA", &CreateWindowExAHook, oCreateWindowExA);
    CreateUser32Hook(user32, "CreateWindowExW", &CreateWindowExWHook, oCreateWindowExW);
    CreateUser32Hook(user32, "SetWindowLongPtrA", &SetWindowLongPtrAHook, oSetWindowLongPtrA);
    CreateUser32Hook(user32, "SetWindowLongPtrW", &SetWindowLongPtrWHook, oSetWindowLongPtrW);
    CreateUser32Hook(user32, "SetWindowLongA", &SetWindowLongAHook, oSetWindowLongA);
    CreateUser32Hook(user32, "SetWindowLongW", &SetWindowLongWHook, oSetWindowLongW);

    OutputDebugStringA(g_windowMode == WindowMode::Minimized
        ? "R1Delta: background mode on (windows start minimized, never take focus)\n"
        : "R1Delta: background mode on (windows parked off-screen, never take focus)\n");
}

void AppendEngineArguments(std::string& commandLine)
{
    if (!IsActive())
        return;
    const auto appendIfMissing = [&](const char* token, const char* text) {
        // Whole-token match: "+fps_max" must not be satisfied by "+fps_max_low_latency".
        for (const char* at = std::strstr(commandLine.c_str(), token); at; at = std::strstr(at + 1, token)) {
            const size_t length = std::strlen(token);
            const bool startsToken = at == commandLine.c_str() || at[-1] == ' ';
            const bool endsToken = at[length] == '\0' || at[length] == ' ';
            if (startsToken && endsToken)
                return;
        }
        if (!commandLine.empty() && commandLine.back() != ' ')
            commandLine += ' ';
        commandLine += text;
    };
    // Background windows are silent unless R1DELTA_BACKGROUND_SOUND=1 (e.g. to test audio on purpose).
    if (!EnvironmentFlagSet("R1DELTA_BACKGROUND_SOUND"))
        appendIfMissing("-nosound", "-nosound");
    appendIfMissing("-nomouse", "-nomouse");
    appendIfMissing("-nomessagebox", "-nomessagebox");
    appendIfMissing("+cl_mouseenable", "+cl_mouseenable 0");
    appendIfMissing("+cl_mouselook", "+cl_mouselook 0");
    appendIfMissing("+fps_max", "+fps_max 60");
    appendIfMissing("+mat_vsync", "+mat_vsync 0");
}

void StartCvarWatchdog()
{
    if (!IsActive() || IsDedicatedServer())
        return;
    HANDLE thread = CreateThread(nullptr, 0, &CvarWatchdogMain, nullptr, 0, nullptr);
    if (thread)
        CloseHandle(thread);
}

void RegisterSelfTestCommand()
{
    if (!IsActive())
        return;
    RegisterConCommand("r1d_background_selftest", SelfTestCommand,
        "Background mode self test: messagebox | crash | status", FCVAR_NONE);
}

} // namespace r1delta::background
