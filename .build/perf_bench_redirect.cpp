// Test harness only (perf_bench.py --inject). Build (x64 Native Tools prompt, from the CODE tree root):
//   cl /nologo /O2 /MT /LD .build\perf_bench_redirect.cpp /I packages\minhook.1.3.3\lib\native\include
//      packages\minhook.1.3.3\lib\native\lib\libMinHook-x64-v141-mt.lib shell32.lib ole32.lib user32.lib /Fe:perf_bench_redirect.dll
// for R1Delta builds without background mode / R1DELTA_SAVEDGAMES:
// - FOLDERID_SavedGames and FOLDERID_Documents resolve to %SGREDIR_PATH%, so the run never touches the user's profile;
// - top-level windows never activate or take the foreground (SW_SHOWNOACTIVATE, SWP_NOACTIVATE, WS_EX_NOACTIVATE, no
//   SetForegroundWindow / SetActiveWindow / SetFocus), so the user's desktop keeps its focus. Pair with +not_focus_sleep 0.
#include <Windows.h>
#include <KnownFolders.h>
#include <ShlObj.h>
#include <MinHook.h>

namespace {
using KnownFolderFn = HRESULT(WINAPI*)(REFKNOWNFOLDERID, DWORD, HANDLE, PWSTR*);
using ShowWindowFn = BOOL(WINAPI*)(HWND, int);
using SetWindowPosFn = BOOL(WINAPI*)(HWND, HWND, int, int, int, int, UINT);
using CreateWindowExWFn = HWND(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
using CreateWindowExAFn = HWND(WINAPI*)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
KnownFolderFn s_knownFolder;
ShowWindowFn s_showWindow;
SetWindowPosFn s_setWindowPos;
CreateWindowExWFn s_createW;
CreateWindowExAFn s_createA;
void* s_unused;

HRESULT WINAPI KnownFolderHook(REFKNOWNFOLDERID id, DWORD flags, HANDLE token, PWSTR* path)
{
    if (path && (IsEqualGUID(id, FOLDERID_SavedGames) || IsEqualGUID(id, FOLDERID_Documents))) {
        wchar_t value[MAX_PATH];
        const DWORD length = GetEnvironmentVariableW(L"SGREDIR_PATH", value, MAX_PATH);
        if (length > 0 && length < MAX_PATH) {
            const SIZE_T bytes = (length + 1) * sizeof(wchar_t);
            *path = static_cast<PWSTR>(CoTaskMemAlloc(bytes));
            if (!*path)
                return E_OUTOFMEMORY;
            memcpy(*path, value, bytes);
            return S_OK;
        }
    }
    return s_knownFolder(id, flags, token, path);
}

int NoActivate(int command)
{
    switch (command) {
    case SW_SHOWNORMAL: case SW_SHOW: case SW_RESTORE: case SW_SHOWDEFAULT: return SW_SHOWNOACTIVATE;
    case SW_SHOWMAXIMIZED: return SW_SHOWNOACTIVATE;
    case SW_SHOWMINIMIZED: return SW_SHOWMINNOACTIVE;
    default: return command;
    }
}
BOOL WINAPI ShowWindowHook(HWND window, int command) { return s_showWindow(window, NoActivate(command)); }
BOOL WINAPI SetWindowPosHook(HWND window, HWND after, int x, int y, int cx, int cy, UINT flags)
{
    if (after == HWND_TOPMOST)
        after = HWND_NOTOPMOST;
    return s_setWindowPos(window, after, x, y, cx, cy, flags | SWP_NOACTIVATE);
}
HWND WINAPI CreateWHook(DWORD ex, LPCWSTR c, LPCWSTR n, DWORD s, int x, int y, int w, int h, HWND p, HMENU m, HINSTANCE i, LPVOID a)
{
    if (!p && !(s & WS_CHILD))
        ex = (ex | WS_EX_NOACTIVATE) & ~static_cast<DWORD>(WS_EX_TOPMOST);
    return s_createW(ex, c, n, s, x, y, w, h, p, m, i, a);
}
HWND WINAPI CreateAHook(DWORD ex, LPCSTR c, LPCSTR n, DWORD s, int x, int y, int w, int h, HWND p, HMENU m, HINSTANCE i, LPVOID a)
{
    if (!p && !(s & WS_CHILD))
        ex = (ex | WS_EX_NOACTIVATE) & ~static_cast<DWORD>(WS_EX_TOPMOST);
    return s_createA(ex, c, n, s, x, y, w, h, p, m, i, a);
}
BOOL WINAPI SetForegroundHook(HWND) { return TRUE; }
BOOL WINAPI BringToTopHook(HWND) { return TRUE; }
HWND WINAPI SetActiveHook(HWND) { return GetActiveWindow(); }
HWND WINAPI SetFocusHook(HWND) { return GetFocus(); }

bool Hook(HMODULE module, const char* name, void* detour, void** original)
{
    void* target = module ? reinterpret_cast<void*>(GetProcAddress(module, name)) : nullptr;
    return target && MH_CreateHook(target, detour, original) == MH_OK && MH_EnableHook(target) == MH_OK;
}
} // namespace

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        // The injector loads shell32 (and with it user32) first: no LoadLibrary under the loader lock.
        HMODULE shell = GetModuleHandleW(L"shell32.dll");
        HMODULE user = GetModuleHandleW(L"user32.dll");
        const bool ok = MH_Initialize() == MH_OK
            && Hook(shell, "SHGetKnownFolderPath", &KnownFolderHook, reinterpret_cast<void**>(&s_knownFolder))
            && Hook(user, "ShowWindow", &ShowWindowHook, reinterpret_cast<void**>(&s_showWindow))
            && Hook(user, "SetWindowPos", &SetWindowPosHook, reinterpret_cast<void**>(&s_setWindowPos))
            && Hook(user, "CreateWindowExW", &CreateWHook, reinterpret_cast<void**>(&s_createW))
            && Hook(user, "CreateWindowExA", &CreateAHook, reinterpret_cast<void**>(&s_createA))
            && Hook(user, "SetForegroundWindow", &SetForegroundHook, &s_unused)
            && Hook(user, "BringWindowToTop", &BringToTopHook, &s_unused)
            && Hook(user, "SetActiveWindow", &SetActiveHook, &s_unused)
            && Hook(user, "SetFocus", &SetFocusHook, &s_unused);
        if (!ok)
            TerminateProcess(GetCurrentProcess(), 0xBADF00D);   // never run unredirected
    }
    return TRUE;
}
