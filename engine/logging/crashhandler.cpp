#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>
#include <time.h>
#include <tlhelp32.h>
#include <string>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <vector>
#include "core.h"
#include "background_mode.h"
#include "load.h"
#include "logging.h"
#include "crash_report_context.h"
#include <shellapi.h>
#include <mmsystem.h>
#include "resource.h"
#include "r1d_version.h"
#include "crash_report_minidump.h"
#include "client/rendering/graphics_flight_recorder.h"
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "version.lib")
const char* g_CrashMessages[] = {
    "// Northstar has cras- oh wait, wrong line.",
   // "// Disconnect: Disconnect: Authentication Failed..",
    "// Seeing this error message does not fill you with determination.",
    "// ****NEWS**FLASH**** 1",
    "// Time Wasted Debugging: %s",
    "// SMAAAASH!!!"
};
// Function declarations
void GetSystemInfoEnhanced(std::stringstream& ss);
void GetCallStack(CONTEXT* context, std::stringstream& ss);
void GetLoadedModules(std::stringstream& ss);
std::string GetExceptionDescription(EXCEPTION_RECORD* record);
void GetRegistersWithContent(CONTEXT* context, std::stringstream& ss);
void DumpStackMemory(CONTEXT* context, std::stringstream& ss, size_t bytesToDump = 512);
std::string GetModuleNameForAddress(HANDLE process, DWORD64 address);
std::string GetAddressContent(DWORD64 address);
std::string GetWindowsVersionInfo();
std::string GetLastErrorString(DWORD errorCode);

// File version of a loaded module, read from its version resource. Empty when the file is
// unreadable or carries no version resource. Triage otherwise has to recover driver and
// overlay versions from the minidump by hand.
std::string GetModuleFileVersion(const char* path)
{
    if (!path || !*path)
        return {};

    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeA(path, &ignored);
    // Version blocks are small; anything large is not a version resource worth stalling the
    // crash handler for. The module enumeration also stops on an aggregate time budget.
    if (size == 0 || size > 512u * 1024u)
        return {};

    std::vector<char> buffer(size);
    if (!GetFileVersionInfoA(path, 0, size, buffer.data()))
        return {};

    VS_FIXEDFILEINFO* info = nullptr;
    UINT length = 0;
    if (!VerQueryValueA(buffer.data(), "\\", reinterpret_cast<LPVOID*>(&info), &length)
        || !info || length < sizeof(VS_FIXEDFILEINFO) || info->dwSignature != 0xFEEF04BD)
        return {};

    char text[64];
    sprintf_s(text, "%u.%u.%u.%u",
        HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
        HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS));
    return text;
}

typedef NTSTATUS(WINAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);

// Stage tracing for the crash handler itself. A dedicated crash once left the handler spinning
// in allocator warnings instead of writing a report, and a debugger cannot observe it: the
// handler steps aside (EXCEPTION_CONTINUE_SEARCH) whenever one is attached, so the process
// terminates before the loop is visible. These lines go to crash\r1delta_crash_trace.txt
// through Win32 only, truncated per crash, so the last line names the stage that never
// returned. Six small appends during a crash are cheap enough to keep permanently.
void CrashTraceStage(const char* stage)
{
    static HANDLE traceFile = INVALID_HANDLE_VALUE;
    static bool initialised = false;

    if (!initialised) {
        char modulePath[MAX_PATH] = {};
        DWORD length = GetModuleFileNameA(nullptr, modulePath, MAX_PATH);
        char* separator = length ? strrchr(modulePath, '\\') : nullptr;
        if (separator) separator[1] = '\0';

        char directory[MAX_PATH];
        sprintf_s(directory, "%scrash", modulePath);
        CreateDirectoryA(directory, nullptr);

        char tracePath[MAX_PATH];
        sprintf_s(tracePath, "%s\\r1delta_crash_trace.txt", directory);
        traceFile = CreateFileA(tracePath, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (traceFile == INVALID_HANDLE_VALUE)
            return;   // not marked initialised: the next stage tries again
        initialised = true;
    }

    if (traceFile == INVALID_HANDLE_VALUE)
        return;

    char line[192];
    const int written = sprintf_s(line, "[%lu] %s\r\n", GetTickCount(), stage);
    DWORD ignored = 0;
    if (written > 0)
        WriteFile(traceFile, line, static_cast<DWORD>(written), &ignored, nullptr);
}

#define CRASH_TRACE(stage) CrashTraceStage(stage)

// --- Crash-report context ----------------------------------------------------------------
// Everything below runs inside the crash handler on the faulting thread: no locks another
// thread can hold, no calls into engine code that could be the faulting subsystem, and every
// pointer taken from engine state is read under a guard.

struct GpuDescription {
    std::string model;
    unsigned long long vramBytes = 0;
};

bool QueryPrimaryGpuDescription(GpuDescription& out)
{
    HKEY key = nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\0000",
        0, KEY_READ, &key) != ERROR_SUCCESS)
        return false;

    char description[256] = {};
    DWORD size = sizeof(description);
    if (RegQueryValueExA(key, "DriverDesc", nullptr, nullptr, reinterpret_cast<LPBYTE>(description), &size) == ERROR_SUCCESS)
        out.model = description;

    // HardwareInformation.qwMemorySize is a REG_QWORD; reading it as a DWORD reports 0 for
    // cards with more than 4 GB of dedicated memory.
    unsigned long long vramBytes = 0;
    size = sizeof(vramBytes);
    if (RegQueryValueExA(key, "HardwareInformation.qwMemorySize", nullptr, nullptr, reinterpret_cast<LPBYTE>(&vramBytes), &size) == ERROR_SUCCESS)
        out.vramBytes = vramBytes;

    RegCloseKey(key);
    return !out.model.empty();
}

// Reads a NUL-terminated string out of engine memory without letting a bad pointer take the
// crash handler down with it (implemented in engine/logging/crash_report_context.cpp).

// The engine string at that offset is a multi-line block ("Map: x\nGame: y\nBuild: n\n..."),
// so reports need one token per line: take the value of the "Map:" field, else the first
// non-empty line.
std::string GetCurrentMapForReport()
{
    if (!G_engine) return "-";

    char text[256];
    if (!TryReadEngineString(static_cast<uintptr_t>(G_engine) + 0x2291560, text, sizeof(text)))
        return "-";

    const char* cursor = text;
    while (*cursor != '\0') {
        const char* lineEnd = cursor;
        while (*lineEnd != '\0' && *lineEnd != '\n' && *lineEnd != '\r') ++lineEnd;

        const std::size_t lineLength = static_cast<std::size_t>(lineEnd - cursor);
        if (lineLength != 0) {
            if (lineLength > 4 && _strnicmp(cursor, "Map:", 4) == 0) {
                const char* value = cursor + 4;
                while (value < lineEnd && (*value == ' ' || *value == '\t')) ++value;
                const std::string map(value, static_cast<std::size_t>(lineEnd - value));
                return map.empty() ? "-" : map;
            }
            return std::string(cursor, lineLength);
        }

        if (*lineEnd == '\r' && *(lineEnd + 1) == '\n') ++lineEnd;
        cursor = (*lineEnd == '\0') ? lineEnd : lineEnd + 1;
    }
    return "-";
}

const char* GetRuntimeModeLabel()
{
    if (G_is_dedi) return G_is_r1o_dedi ? "r1o-dedi" : "legacy-dedi";
    if (G_client) return "client";
    return "unknown";
}

DWORD GetProcessThreadCount()
{
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;

    DWORD count = 0;
    const DWORD pid = GetCurrentProcessId();
    THREADENTRY32 entry = {};
    entry.dwSize = sizeof(entry);
    if (Thread32First(snapshot, &entry)) {
        do {
            if (entry.th32OwnerProcessID == pid) ++count;
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return count;
}

// "module.dll+0x1A2B3F" — the form triage greps for and can look up in a symbol file. Built
// from a Toolhelp snapshot rather than the dbghelp module table, because the summary line is
// written before the symbol handler is initialized.
std::string GetModuleRelativeAddress(DWORD64 address)
{
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snapshot != INVALID_HANDLE_VALUE) {
        // Same trick the module dump uses: the tchar macros map MODULEENTRY32 to the wide
        // variant, and module names here are plain ASCII.
#undef MODULEENTRY32
#undef Module32First
#undef Module32Next
        MODULEENTRY32 module = {};
        module.dwSize = sizeof(module);
        if (Module32First(snapshot, &module)) {
            do {
                const DWORD64 base = reinterpret_cast<DWORD64>(module.modBaseAddr);
                if (address >= base && address < base + module.modBaseSize) {
                    char offset[32];
                    sprintf_s(offset, "+0x%llX", static_cast<unsigned long long>(address - base));
                    std::string result = module.szModule;
                    result += offset;
                    CloseHandle(snapshot);
                    return result;
                }
            } while (Module32Next(snapshot, &module));
        }
        CloseHandle(snapshot);
    }

    char raw[32];
    sprintf_s(raw, "0x%llX", address);
    return raw;
}

std::string GetGraphicsDriverForReport()
{
    static const char* const kDriverModules[] = {
        "nvwgf2umx.dll", "nvldumdx.dll", "nvwgf2um.dll",
        "atidxx64.dll", "amdxc64.dll",
        "igd10iumd64.dll", "igdumdim64.dll", "igvk64.dll",
    };

    for (const char* moduleName : kDriverModules) {
        const HMODULE module = GetModuleHandleA(moduleName);
        if (!module) continue;

        char path[MAX_PATH];
        if (!GetModuleFileNameA(module, path, MAX_PATH)) continue;

        const std::string version = GetModuleFileVersion(path);
        return std::string(moduleName) + (version.empty() ? " (no version resource)" : " " + version);
    }
    return "-";
}

// Process-side resource counters. The system section above reports machine totals; these are
// what the crashing process itself held, which is what the memory-pressure crash families need.
std::string GetProcessCountersText()
{
    std::stringstream ss;

    PROCESS_MEMORY_COUNTERS_EX memory = {};
    memory.cb = sizeof(memory);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory))) {
        ss << "Working Set: " << (memory.WorkingSetSize / (1024 * 1024)) << " MB"
           << " (peak " << (memory.PeakWorkingSetSize / (1024 * 1024)) << " MB)"
           << ", Private: " << (memory.PrivateUsage / (1024 * 1024)) << " MB"
           << ", Pagefile: " << (memory.PagefileUsage / (1024 * 1024)) << " MB" << std::endl;
    }
    else {
        ss << "Working Set: [unavailable]" << std::endl;
    }

    DWORD handles = 0;
    if (GetProcessHandleCount(GetCurrentProcess(), &handles))
        ss << "Handle Count: " << handles << std::endl;

    const DWORD gdiObjects = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    const DWORD userObjects = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    if (gdiObjects != 0 || userObjects != 0)
        ss << "GDI Objects: " << gdiObjects << ", USER Objects: " << userObjects << std::endl;

    MEMORYSTATUSEX status = {};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        ss << "System Memory: " << status.dwMemoryLoad << "% used, "
           << ((status.ullTotalPhys - status.ullAvailPhys) / (1024ull * 1024ull)) << " of "
           << (status.ullTotalPhys / (1024ull * 1024ull)) << " MB physical, commit "
           << ((status.ullTotalPageFile - status.ullAvailPageFile) / (1024ull * 1024ull)) << " of "
           << (status.ullTotalPageFile / (1024ull * 1024ull)) << " MB" << std::endl;
    }

    return ss.str();
}

// One grep-able line so a corpus of reports can be bucketed without parsing the sections.
std::string BuildCrashSummaryLine(EXCEPTION_POINTERS* exInfo)
{
    GpuDescription gpu;
    QueryPrimaryGpuDescription(gpu);
    const std::string map = GetCurrentMapForReport();
    const std::string driver = GetGraphicsDriverForReport();
    const std::string fault = GetModuleRelativeAddress(
        reinterpret_cast<DWORD64>(exInfo->ExceptionRecord->ExceptionAddress));

    char line[1024];
    sprintf_s(line,
        "SUMMARY: version=%s commit=%s mode=%s map=%s uptime=%llus threads=%lu gpu=\"%s\" driver=\"%s\" fault=%s tid=%lu",
        R1D_VERSION,
        (R1D_COMMIT[0] != '\0' ? R1D_COMMIT : "unknown"),
        GetRuntimeModeLabel(),
        map.c_str(),
        GetProcessUptimeSeconds(),
        static_cast<unsigned long>(GetProcessThreadCount()),
        (gpu.model.empty() ? "unknown" : gpu.model.c_str()),
        driver.c_str(),
        fault.c_str(),
        static_cast<unsigned long>(GetCurrentThreadId()));
    return line;
}
// Global crash handler function
LONG WINAPI CustomCrashHandler(EXCEPTION_POINTERS* exInfo)
{
    // Before anything else: everything that would log, allocate or feed console/MCP pipelines
    // during teardown has to stand down, or one crash turns into a storm of warnings and
    // allocations that hangs the handler (seen in dedicated crashes). A second entry (a fault
    // inside the handler itself) returns immediately rather than recursing.
    static std::atomic<bool> handling{ false };
    if (handling.exchange(true, std::memory_order_acq_rel))
        return EXCEPTION_EXECUTE_HANDLER;

    SetCrashHandlerActive();
    CRASH_TRACE("handler entry");

    if (IsDebuggerPresent())
        return EXCEPTION_CONTINUE_SEARCH;
    // Capture GetLastError as early as possible
    DWORD lastError = GetLastError();

    bool cont = false;
    switch (exInfo->ExceptionRecord->ExceptionCode)
    {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    case EXCEPTION_BREAKPOINT:
    case EXCEPTION_DATATYPE_MISALIGNMENT:
    case EXCEPTION_FLT_DENORMAL_OPERAND:
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    case EXCEPTION_FLT_INEXACT_RESULT:
    case EXCEPTION_FLT_INVALID_OPERATION:
    case EXCEPTION_FLT_OVERFLOW:
    case EXCEPTION_FLT_STACK_CHECK:
    case EXCEPTION_FLT_UNDERFLOW:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_INT_OVERFLOW:
    case EXCEPTION_INVALID_DISPOSITION:
    case EXCEPTION_NONCONTINUABLE_EXCEPTION:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_SINGLE_STEP:
    case EXCEPTION_STACK_OVERFLOW:
        cont = true;
    }
    if (!cont)
        return EXCEPTION_CONTINUE_SEARCH;
    // Seal diagnostic publications before sound, minidump work, or delays. A
    // faulting/interrupted graphics writer is marked incomplete, never waited on.
    r1delta::graphics_flight_recorder::Freeze();
    if (!IsDedicatedServer() && !r1delta::background::IsActive()) {
        // Get the handle to THIS DLL module
        HMODULE hModule;
        if (GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)&CustomCrashHandler,
            &hModule
        )) {
            // Find the resource by name
            HRSRC hRes = FindResourceA(hModule, "CRASH_SOUND", "WAVE");
            if (hRes) {
                HGLOBAL hData = LoadResource(hModule, hRes);
                if (hData) {
                    LPVOID pData = LockResource(hData);
                    DWORD size = SizeofResource(hModule, hRes);

                    // Play directly from memory
                    PlaySoundA((LPCSTR)pData, NULL, SND_MEMORY);
                }
            }
        }
    }
    // Get current time as unix timestamp
    time_t currentTime = time(nullptr);

    // Get current process ID
    DWORD pid = GetCurrentProcessId();

    // Create filename
    CreateDirectoryA("crashes", NULL);

    // Create filename with path
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);

    // Extract directory from path
    char exeDir[MAX_PATH];
    strcpy_s(exeDir, exePath);
    for (int i = strlen(exeDir) - 1; i >= 0; i--) {
        if (exeDir[i] == '\\' || exeDir[i] == '/') {
            exeDir[i + 1] = '\0';
            break;
        }
    }

    // Create crashes directory path
    char crashesDir[MAX_PATH];
    sprintf_s(crashesDir, "%scrashes", exeDir);

    // Create crashes directory if it doesn't exist
    if (!CreateDirectoryA(crashesDir, NULL)) {
        // If directory creation failed but it's because the directory already exists, that's fine
        if (GetLastError() != ERROR_ALREADY_EXISTS) {
            // Just continue - we'll try to write to the current directory as a fallback
        }
    }

    // Create filename with path
    char filename[MAX_PATH];
    char filepath[MAX_PATH];
    sprintf_s(filename, "r1delta_crash_%lld_%lu.log", (long long)currentTime, pid);
    sprintf_s(filepath, "%s\\%s", crashesDir, filename);
    std::string minidumpSection;
    CRASH_TRACE("minidump build start");
    r1delta::logging::crash_report_minidump::BuildCrashReportMinidumpSection(
        exInfo,
        filepath,
        minidumpSection);
    CRASH_TRACE("minidump build done");

    // Safety net: the minidump section carries the dump, and every section below it can still
    // fail (a provider fault or an allocation failure) before the real write happens. Writing it
    // first means the dump survives that, and the final write truncates this placeholder.
    {
        std::ofstream emergency(filepath);
        if (emergency.is_open()) {
            emergency << minidumpSection;
            emergency.flush();
        }
    }
    srand(time(NULL));
    std::stringstream crashLog;
    // Pick a random message from the array
    int randomIndex = rand() % (sizeof(g_CrashMessages) / sizeof(g_CrashMessages[0]));
    crashLog << g_CrashMessages[randomIndex] << std::endl;
    crashLog << "Unfortunately, R1Delta " R1D_DISPLAY_VERSION " has crashed. Please send this crash report to a programmer in the Discord server.";
#ifdef _DEBUG   
    crashLog << ".. or don't, because this is a debug build.";
#endif
    crashLog << std::endl << std::endl;

    // Add timestamp
    std::string timeStr = ctime(&currentTime);
    timeStr.pop_back(); // Remove the trailing newline

    crashLog << "=== Crash Report ===" << std::endl;
    crashLog << "Timestamp: " << currentTime << " (" << timeStr << ")" << std::endl;
    crashLog << "Process ID: " << pid << std::endl;
    crashLog << "Build Date: " << __DATE__ << " " << __TIME__ << std::endl;
    crashLog << BuildCrashSummaryLine(exInfo) << std::endl << std::endl;
    // Add last error info
    crashLog << "=== Last Error Info ===" << std::endl;
    crashLog << "Error Code: " << lastError << " (0x" << std::hex << lastError << ")" << std::dec << std::endl;
    crashLog << "Error Description: " << GetLastErrorString(lastError) << std::endl << std::endl;

    // Add exception info
    crashLog << "=== Exception Information ===" << std::endl;
    crashLog << "Exception Code: 0x" << std::hex << exInfo->ExceptionRecord->ExceptionCode << std::dec << std::endl;
    crashLog << "Exception Address: 0x" << std::hex << (ULONG_PTR)exInfo->ExceptionRecord->ExceptionAddress << std::dec << std::endl;
    crashLog << "Exception Description: " << GetExceptionDescription(exInfo->ExceptionRecord) << std::endl;
    // Add module info for exception address
    std::string moduleName = GetModuleNameForAddress(GetCurrentProcess(), (DWORD64)exInfo->ExceptionRecord->ExceptionAddress);
    if (!moduleName.empty()) {
        crashLog << "Module: " << moduleName << std::endl;
    }
    crashLog << std::endl;

    // Add register info with content
    crashLog << "=== Register Values ===" << std::endl;
    GetRegistersWithContent(exInfo->ContextRecord, crashLog);
    crashLog << std::endl;

    // Dumps 1024 bytes above RSP, and 1024 bytes below.
    crashLog << "=== Stack Memory Dump ===" << std::endl;
    DumpStackMemory(exInfo->ContextRecord, crashLog, 1024);
    crashLog << std::endl;

    // Add call stack info (NOT prioritized higher, fuck you Gemini, edits CTX)
    crashLog << "=== Call Stack ===" << std::endl;
    GetCallStack(exInfo->ContextRecord, crashLog);
    crashLog << std::endl;

    // Add loaded modules info
    crashLog << "=== Loaded Modules ===" << std::endl;
    GetLoadedModules(crashLog);
    crashLog << std::endl;

    // Add enhanced system info
    crashLog << "=== System Information ===" << std::endl;
    GetSystemInfoEnhanced(crashLog);
    crashLog << std::endl;

    if (G_engine) {
        crashLog << "=== Steam Crash Comment ===" << std::endl;
        char steamComment[512];
        if (TryReadEngineString(static_cast<uintptr_t>(G_engine) + 0x2291560, steamComment, sizeof(steamComment)))
            crashLog << steamComment << std::endl;
        else
            crashLog << "(unreadable)" << std::endl;
    }
    crashLog << std::endl;

    // R1Delta's own view of the run. The Steam comment above depends on engine state and comes
    // out empty in exactly the crashes where the engine is already gone.
    crashLog << "=== R1Delta State ===" << std::endl;
    crashLog << "Runtime Mode: " << GetRuntimeModeLabel() << std::endl;
    crashLog << "Current Map: " << GetCurrentMapForReport() << std::endl;
    crashLog << "Graphics Driver: " << GetGraphicsDriverForReport() << std::endl;

    const unsigned long faultingThread = GetCurrentThreadId();
    const char* faultingThreadName = GetR1DeltaThreadName(faultingThread);
    crashLog << "Faulting Thread: " << faultingThread << " ("
             << (faultingThreadName != nullptr ? faultingThreadName : "unregistered: main or engine-owned")
             << ") of " << GetProcessThreadCount() << " threads" << std::endl;

    crashLog << "Named R1Delta Threads:" << std::endl;
    crashLog << GetR1DeltaThreadCensusText();
    crashLog << GetDiscordCrashStateLine() << std::endl;
    crashLog << GetRegionCullCrashStateLine() << std::endl;
    crashLog << GetLetterboxHookStateLine() << std::endl;
    crashLog << GetProcessCountersText();
    crashLog << "Watched Convars:" << std::endl << GetWatchedConvarsText() << std::endl;

    r1delta::graphics_flight_recorder::WriteReport(crashLog);

    crashLog << "=== Hooks ===" << std::endl;
    crashLog << GetHookStateReportText() << std::endl;

    crashLog << "=== Addons ===" << std::endl;
    crashLog << GetEnabledAddonsReportText() << std::endl;

    crashLog << "=== Recent Content Files (last " << kRecentFileSlots << ") ===" << std::endl;
    crashLog << GetRecentFilesText() << std::endl;

    crashLog << "=== Recent Console (last " << kConsoleTailSlots << " lines) ===" << std::endl;
    crashLog << GetConsoleTailText() << std::endl;
    // Write crash log to file
    CRASH_TRACE("report write start");
    std::ofstream logFile(filepath);
    if (logFile.is_open()) {
        logFile << crashLog.str();
        logFile.flush();
        logFile << (minidumpSection.empty()
            ? r1delta::logging::crash_report_minidump::kUnavailableSection
            : minidumpSection);
        logFile.close();
        CRASH_TRACE("report write done");
        // Background test mode: the crash log is written above, but nothing pops up (no notepad, no sound).
        if (!IsDedicatedServer() && !r1delta::background::IsActive()) {

            char cmdLine[1024];
            sprintf_s(cmdLine, "notepad.exe \"%s\"", filepath);  // Build command line with the file path

            STARTUPINFOA si = { 0 };
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi = { 0 };

            if (CreateProcessA(
                NULL,        // Application name: NULL means the executable is the first token of the command line.
                cmdLine,     // Command line (must be mutable)
                NULL,        // Process handle not inheritable
                NULL,        // Thread handle not inheritable
                FALSE,       // Set handle inheritance to FALSE
                0,           // No creation flags
                NULL,        // Use parent's environment block
                NULL,        // Use parent's starting directory 
                &si,         // Pointer to STARTUPINFO structure
                &pi          // Pointer to PROCESS_INFORMATION structure
            ))
            {
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                Sleep(100);
            }
            // Get the handle to THIS DLL module
            HMODULE hModule;
            if (GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                (LPCSTR)&CustomCrashHandler,
                &hModule
            )) {
                // Find the resource by name
                HRSRC hRes = FindResourceA(hModule, "CRASH_SOUND2", "WAVE");
                if (hRes) {
                    HGLOBAL hData = LoadResource(hModule, hRes);
                    if (hData) {
                        LPVOID pData = LockResource(hData);
                        DWORD size = SizeofResource(hModule, hRes);

                        // Play directly from memory
                        PlaySoundA((LPCSTR)pData, NULL, SND_MEMORY);
                    }
                }
            }
            Sleep(50);
        }

    }

    // Terminate the process
    TerminateProcess(GetCurrentProcess(), 1);

    CRASH_TRACE("handler exit");
    return EXCEPTION_EXECUTE_HANDLER;
}

// Helper function to get a string description of the last error
std::string GetLastErrorString(DWORD errorCode)
{
    if (errorCode == 0) return "No error";

    LPSTR messageBuffer = nullptr;
    size_t size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&messageBuffer,
        0,
        NULL
    );

    std::string message = "Unknown error";
    if (messageBuffer) {
        message = std::string(messageBuffer, size);

        // Remove trailing newlines and periods that FormatMessage adds
        while (!message.empty() && (message.back() == '\n' || message.back() == '\r' || message.back() == '.')) {
            message.pop_back();
        }

        LocalFree(messageBuffer);
    }

    return message;
}

// Helper function to get exception description
std::string GetExceptionDescription(EXCEPTION_RECORD* record)
{
    switch (record->ExceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION:
    {
        std::stringstream ss;
        ss << "Access Violation - ";
        if (record->ExceptionInformation[0] == 0) {
            ss << "Attempted to read from: 0x" << std::hex << record->ExceptionInformation[1];
        }
        else if (record->ExceptionInformation[0] == 1) {
            ss << "Attempted to write to: 0x" << std::hex << record->ExceptionInformation[1];
        }
        else if (record->ExceptionInformation[0] == 8) {
            ss << "DEP violation at: 0x" << std::hex << record->ExceptionInformation[1];
        }
        else {
            ss << "Unknown access violation at: 0x" << std::hex << record->ExceptionInformation[1];
        }
        return ss.str();
    }
    case EXCEPTION_DATATYPE_MISALIGNMENT:    return "Datatype Misalignment";
    case EXCEPTION_BREAKPOINT:               return "Breakpoint";
    case EXCEPTION_SINGLE_STEP:              return "Single Step";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "Array Bounds Exceeded";
    case EXCEPTION_FLT_DENORMAL_OPERAND:     return "Float Denormal Operand";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "Float Divide By Zero";
    case EXCEPTION_FLT_INEXACT_RESULT:       return "Float Inexact Result";
    case EXCEPTION_FLT_INVALID_OPERATION:    return "Float Invalid Operation";
    case EXCEPTION_FLT_OVERFLOW:             return "Float Overflow";
    case EXCEPTION_FLT_STACK_CHECK:          return "Float Stack Check";
    case EXCEPTION_FLT_UNDERFLOW:            return "Float Underflow";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "Integer Divide By Zero";
    case EXCEPTION_INT_OVERFLOW:             return "Integer Overflow";
    case EXCEPTION_PRIV_INSTRUCTION:         return "Privileged Instruction";
    case EXCEPTION_IN_PAGE_ERROR:            return "In Page Error";
    case EXCEPTION_ILLEGAL_INSTRUCTION:      return "Illegal Instruction";
    case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "Noncontinuable Exception";
    case EXCEPTION_STACK_OVERFLOW:           return "Stack Overflow";
    case EXCEPTION_INVALID_DISPOSITION:      return "Invalid Disposition";
    case EXCEPTION_GUARD_PAGE:               return "Guard Page Violation";
    case EXCEPTION_INVALID_HANDLE:           return "Invalid Handle";
    default:                                 return "Unknown Exception";
    }
}

// Get module name for an address
std::string GetModuleNameForAddress(HANDLE process, DWORD64 address)
{
    DWORD64 moduleBase = SymGetModuleBase64(process, address);
    if (moduleBase) {
        IMAGEHLP_MODULE64 moduleInfo = { sizeof(IMAGEHLP_MODULE64) };
        if (SymGetModuleInfo64(process, moduleBase, &moduleInfo)) {
            std::stringstream ss;
            ss << moduleInfo.ModuleName << "+0x" << std::hex << (address - moduleBase);
            return ss.str();
        }
    }
    return "";
}

// Get a bounded, lossless preview for readable memory.
std::string GetAddressContent(DWORD64 address)
{
    return r1delta::logging::crash_report_minidump::ReadRegisterMemoryPreview(
        reinterpret_cast<const void*>(static_cast<uintptr_t>(address)));
}

// Helper function to get register values with content examination
void GetRegistersWithContent(CONTEXT* context, std::stringstream& ss)
{
    SymInitialize(GetCurrentProcess(), NULL, TRUE);

#ifdef _M_X64
    std::vector<std::pair<std::string, DWORD64>> registers = {
        {"RAX", context->Rax},
        {"RBX", context->Rbx},
        {"RCX", context->Rcx},
        {"RDX", context->Rdx},
        {"RSI", context->Rsi},
        {"RDI", context->Rdi},
        {"RBP", context->Rbp},
        {"RSP", context->Rsp},
        {"R8 ", context->R8},
        {"R9 ", context->R9},
        {"R10", context->R10},
        {"R11", context->R11},
        {"R12", context->R12},
        {"R13", context->R13},
        {"R14", context->R14},
        {"R15", context->R15},
        {"RIP", context->Rip}
    };

    for (const auto& reg : registers) {
        ss << reg.first << ": 0x" << std::hex << std::setw(16) << std::setfill('0') << reg.second;

        // Add module info if it points into a module
        std::string moduleName = GetModuleNameForAddress(GetCurrentProcess(), reg.second);
        if (!moduleName.empty()) {
            ss << " (" << moduleName << ")";
        }

        // Try to display content if it's a pointer
        std::string content = GetAddressContent(reg.second);
        if (!content.empty()) {
            ss << " " << content;
        }

        ss << std::endl;
    }

    ss << "EFLAGS: 0x" << std::hex << std::setw(8) << std::setfill('0') << context->EFlags << std::endl;
#else
    std::vector<std::pair<std::string, DWORD>> registers = {
        {"EAX", context->Eax},
        {"EBX", context->Ebx},
        {"ECX", context->Ecx},
        {"EDX", context->Edx},
        {"ESI", context->Esi},
        {"EDI", context->Edi},
        {"EBP", context->Ebp},
        {"ESP", context->Esp},
        {"EIP", context->Eip}
    };

    for (const auto& reg : registers) {
        ss << reg.first << ": 0x" << std::hex << std::setw(8) << std::setfill('0') << reg.second;

        // Add module info if it points into a module
        std::string moduleName = GetModuleNameForAddress(GetCurrentProcess(), reg.second);
        if (!moduleName.empty()) {
            ss << " (" << moduleName << ")";
        }

        // Try to display content if it's a pointer
        std::string content = GetAddressContent(reg.second);
        if (!content.empty()) {
            ss << " " << content;
        }

        ss << std::endl;
    }

    ss << "EFLAGS: 0x" << std::hex << std::setw(8) << std::setfill('0') << context->EFlags << std::endl;
#endif
    ss << std::dec; // Reset to decimal
    SymCleanup(GetCurrentProcess());
}

// Helper function to dump stack memory
void DumpStackMemory(CONTEXT* context, std::stringstream& ss, size_t bytesToDump)
{
#ifdef _M_X64
    DWORD64 stackPointer = context->Rsp - bytesToDump;
#else
    DWORD64 stackPointer = context->Esp;
#endif

    // Ensure bytesToDump is multiple of 16
    bytesToDump *= 2;
    bytesToDump = (bytesToDump + 15) & ~15;

    // Align stack pointer for nice output
    DWORD64 alignedSP = stackPointer & ~0xF;

    char buffer[16]; // 16 bytes per line
    SIZE_T bytesRead;

    for (size_t offset = 0; offset < bytesToDump; offset += 16) {
        DWORD64 currentAddress = alignedSP + offset;

        ss << "0x" << std::hex << std::setw(16) << std::setfill('0') << currentAddress << " | ";

        if (ReadProcessMemory(GetCurrentProcess(), (LPCVOID)currentAddress, buffer, 16, &bytesRead)) {
            // Hex display
            for (size_t i = 0; i < 16; i++) {
                if (i < bytesRead) {
                    ss << std::hex << std::setw(2) << std::setfill('0') << (int)(unsigned char)buffer[i] << " ";
                }
                else {
                    ss << "   "; // Padding for incomplete line
                }
            }

            ss << "| ";

            // ASCII display
            for (size_t i = 0; i < 16; i++) {
                if (i < bytesRead) {
                    char c = buffer[i];
                    if (c >= 32 && c <= 126) { // Printable ASCII
                        ss << c;
                    }
                    else {
                        ss << ".";
                    }
                }
                else {
                    ss << " "; // Padding for incomplete line
                }
            }
        }
        else {
            ss << "<memory not accessible>";
        }

        ss << std::endl;
    }

    ss << std::dec; // Reset to decimal
}

// Helper function to get Windows version information using RtlGetVersion
std::string GetWindowsVersionInfo()
{
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) return "Unknown Windows version";

    RtlGetVersionPtr rtlGetVersion = (RtlGetVersionPtr)GetProcAddress(ntdll, "RtlGetVersion");
    if (!rtlGetVersion) return "Unknown Windows version";

    RTL_OSVERSIONINFOW osvi = { 0 };
    osvi.dwOSVersionInfoSize = sizeof(osvi);

    NTSTATUS status = rtlGetVersion(&osvi);
    if (status != 0) return "Unknown Windows version";

    std::stringstream result;

    // Get more details from registry
    std::string productName = "Windows";
    std::string displayVersion = "";
    std::string ubr = "";

    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char buffer[256];
        DWORD bufSize = sizeof(buffer);

        if (RegQueryValueExA(hKey, "ProductName", NULL, NULL, (LPBYTE)buffer, &bufSize) == ERROR_SUCCESS) {
            productName = buffer;
        }

        bufSize = sizeof(buffer);
        if (RegQueryValueExA(hKey, "DisplayVersion", NULL, NULL, (LPBYTE)buffer, &bufSize) == ERROR_SUCCESS) {
            displayVersion = buffer;
        }

        DWORD ubrValue = 0;
        bufSize = sizeof(ubrValue);
        if (RegQueryValueExA(hKey, "UBR", NULL, NULL, (LPBYTE)&ubrValue, &bufSize) == ERROR_SUCCESS) {
            ubr = std::to_string(ubrValue);
        }

        RegCloseKey(hKey);
    }

    // Determine Windows version
    result << productName;

    if (!displayVersion.empty()) {
        result << " (Version " << displayVersion;
        if (!ubr.empty()) {
            result << ", Build " << osvi.dwBuildNumber << "." << ubr;
        }
        else {
            result << ", Build " << osvi.dwBuildNumber;
        }
        result << ")";
    }
    else {
        result << " (Version " << osvi.dwMajorVersion << "." << osvi.dwMinorVersion;
        result << ", Build " << osvi.dwBuildNumber << ")";
    }

#ifdef _WIN64
    result << " 64-bit";
#else
    BOOL isWow64 = FALSE;
    if (IsWow64Process(GetCurrentProcess(), &isWow64) && isWow64) {
        result << " 32-bit on 64-bit";
    }
    else {
        result << " 32-bit";
    }
#endif

    return result.str();
}

// Helper function to get call stack
void GetCallStack(CONTEXT* context, std::stringstream& ss)
{
    // Initialize symbols
    HANDLE process = GetCurrentProcess();
    SymInitialize(process, NULL, TRUE);
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);

    // Initialize stack frame
    STACKFRAME64 stackFrame = {};
#ifdef _M_X64
    stackFrame.AddrPC.Offset = context->Rip;
    stackFrame.AddrFrame.Offset = context->Rbp;
    stackFrame.AddrStack.Offset = context->Rsp;
    DWORD machineType = IMAGE_FILE_MACHINE_AMD64;
#else
    stackFrame.AddrPC.Offset = context->Eip;
    stackFrame.AddrFrame.Offset = context->Ebp;
    stackFrame.AddrStack.Offset = context->Esp;
    DWORD machineType = IMAGE_FILE_MACHINE_I386;
#endif
    stackFrame.AddrPC.Mode = AddrModeFlat;
    stackFrame.AddrFrame.Mode = AddrModeFlat;
    stackFrame.AddrStack.Mode = AddrModeFlat;

    // Walk the stack
    for (int frameNum = 0; frameNum < 100; frameNum++) {
        if (!StackWalk64(machineType, process, GetCurrentThread(), &stackFrame, context,
            NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL)) {
            break;
        }

        if (stackFrame.AddrPC.Offset == 0) {
            continue;
        }

        // Get module information for this address
        DWORD64 moduleBase = SymGetModuleBase64(process, stackFrame.AddrPC.Offset);
        char moduleName[MAX_PATH] = "Unknown";

        if (moduleBase) {
            IMAGEHLP_MODULE64 moduleInfo = { sizeof(IMAGEHLP_MODULE64) };
            if (SymGetModuleInfo64(process, moduleBase, &moduleInfo)) {
                strcpy_s(moduleName, moduleInfo.ModuleName);
            }
        }

        // Calculate the RVA (Relative Virtual Address)
        DWORD64 rva = stackFrame.AddrPC.Offset - moduleBase;

        // Print the stack frame
        ss << "Frame " << std::setw(2) << frameNum << ": "
            << moduleName << "+0x" << std::hex << rva << std::dec << std::endl;
    }

    SymCleanup(process);
}

// Helper function to get loaded modules
void GetLoadedModules(std::stringstream& ss)
{
    HANDLE hModuleSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (hModuleSnap == INVALID_HANDLE_VALUE) {
        ss << "Failed to get loaded modules" << std::endl;
        return;
    }
#undef MODULEENTRY32
#undef Module32First
#undef Module32Next
    MODULEENTRY32 me32;
    me32.dwSize = sizeof(MODULEENTRY32);

    if (!Module32First(hModuleSnap, &me32)) {
        CloseHandle(hModuleSnap);
        ss << "Failed to get first module" << std::endl;
        return;
    }

    HANDLE process = GetCurrentProcess();
    int moduleCount = 0;
    // Version resources are read from disk per module; on a slow or unavailable path that can
    // stall the handler, so the lookups stop once the budget is spent and the rest of the list
    // still gets written.
    const ULONGLONG versionBudgetDeadline = GetTickCount64() + 750ull;

    do {
        MODULEINFO moduleInfo;
        const std::string fileVersion = GetTickCount64() < versionBudgetDeadline
            ? GetModuleFileVersion(me32.szExePath)
            : std::string();
        if (GetModuleInformation(process, me32.hModule, &moduleInfo, sizeof(moduleInfo))) {
            ss << "[" << std::setw(2) << moduleCount++ << "] "
                << me32.szModule << " (Base: 0x" << std::hex << (ULONG_PTR)me32.hModule
                << ", Size: 0x" << moduleInfo.SizeOfImage
                << ", Region: 0x" << (ULONG_PTR)me32.hModule << " - 0x"
                << (ULONG_PTR)me32.hModule + moduleInfo.SizeOfImage << ")"
                << ", Version: " << (fileVersion.empty() ? "-" : fileVersion)
                << std::dec << std::endl;
        }
        else {
            ss << "[" << std::setw(2) << moduleCount++ << "] "
                << me32.szModule << " (Base: 0x" << std::hex << (ULONG_PTR)me32.hModule
                << ", Size: Unknown)"
                << ", Version: " << (fileVersion.empty() ? "-" : fileVersion)
                << std::dec << std::endl;
        }
    } while (Module32Next(hModuleSnap, &me32));

    CloseHandle(hModuleSnap);
}

// Helper function to get enhanced system information
void GetSystemInfoEnhanced(std::stringstream& ss)
{
    // Get OS information
    ss << "Operating System: " << GetWindowsVersionInfo() << std::endl;

    // Get basic system info
    SYSTEM_INFO sysInfo;
    ::GetSystemInfo(&sysInfo);

    ss << "Processor Architecture: ";
    switch (sysInfo.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_AMD64: ss << "x64 (AMD or Intel)"; break;
    case PROCESSOR_ARCHITECTURE_ARM: ss << "ARM"; break;
    case PROCESSOR_ARCHITECTURE_ARM64: ss << "ARM64"; break;
    case PROCESSOR_ARCHITECTURE_INTEL: ss << "x86 (Intel)"; break;
    default: ss << "Unknown"; break;
    }
    ss << std::endl;

    ss << "Number of Processors: " << sysInfo.dwNumberOfProcessors << std::endl;

    // Get processor name from registry
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char processorName[256];
        DWORD bufSize = sizeof(processorName);
        if (RegQueryValueExA(hKey, "ProcessorNameString", NULL, NULL, (LPBYTE)processorName, &bufSize) == ERROR_SUCCESS) {
            ss << "CPU: " << processorName << std::endl;
        }

        // Get processor speed
        DWORD mhz;
        bufSize = sizeof(mhz);
        if (RegQueryValueExA(hKey, "~MHz", NULL, NULL, (LPBYTE)&mhz, &bufSize) == ERROR_SUCCESS) {
            ss << "CPU Speed: " << mhz << " MHz" << std::endl;
        }

        RegCloseKey(hKey);
    }

    // Get memory info
    MEMORYSTATUSEX memInfo = { sizeof(MEMORYSTATUSEX) };
    if (GlobalMemoryStatusEx(&memInfo)) {
        ss << "Total RAM: " << memInfo.ullTotalPhys / (1024 * 1024) << " MB" << std::endl;
        ss << "Available RAM: " << memInfo.ullAvailPhys / (1024 * 1024) << " MB" << std::endl;
        ss << "Memory Load: " << memInfo.dwMemoryLoad << "%" << std::endl;
    }

    // Get current executable path
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);

    // Get disk info for the drive containing the executable
    std::string exePathStr(exePath);
    std::string driveLetter = exePathStr.substr(0, 3); // Get "C:\"

    ULARGE_INTEGER freeBytesAvailable, totalBytes, totalFreeBytes;
    if (GetDiskFreeSpaceExA(driveLetter.c_str(), &freeBytesAvailable, &totalBytes, &totalFreeBytes)) {
        ss << "Disk Total Space (" << driveLetter << "): " << totalBytes.QuadPart / (1024 * 1024 * 1024) << " GB" << std::endl;
        ss << "Disk Free Space (" << driveLetter << "): " << freeBytesAvailable.QuadPart / (1024 * 1024 * 1024) << " GB" << std::endl;
    }

    // GPU identity and dedicated video memory from the display class registry key
    GpuDescription gpu;
    if (QueryPrimaryGpuDescription(gpu)) {
        ss << "GPU Model: " << gpu.model << std::endl;
        if (gpu.vramBytes != 0)
            ss << "VRAM: " << (gpu.vramBytes / (1024ull * 1024ull)) << " MB" << std::endl;
    }
    else {
        ss << "GPU Model: [Unable to retrieve GPU information]" << std::endl;
    }

    // Get executable info
    FILETIME creationTime, accessTime, writeTime;
    HANDLE hExe = CreateFileA(exePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hExe != INVALID_HANDLE_VALUE) {
        if (GetFileTime(hExe, &creationTime, &accessTime, &writeTime)) {
            SYSTEMTIME stUTC, stLocal;
            FileTimeToSystemTime(&writeTime, &stUTC);
            SystemTimeToTzSpecificLocalTime(NULL, &stUTC, &stLocal);

            char dateStr[64];
            sprintf_s(dateStr, "%04d-%02d-%02d %02d:%02d:%02d",
                stLocal.wYear, stLocal.wMonth, stLocal.wDay,
                stLocal.wHour, stLocal.wMinute, stLocal.wSecond);

            ss << "Executable Path: " << exePath << std::endl;
            ss << "Executable Last Modified: " << dateStr << std::endl;
        }
        CloseHandle(hExe);
    }

    // Provenance that is otherwise only visible in a debugger or the launcher's log: the
    // PEB command line is empty when the launcher injected its arguments internally.
    const char* launchArgs = GetR1DeltaLaunchArgs();
    ss << "R1Delta Compatibility Version: " << R1D_VERSION
       << " (commit " << (R1D_COMMIT[0] != '\0' ? R1D_COMMIT : "unknown") << ")" << std::endl;
    ss << "R1Delta Launch Args: " << ((launchArgs && *launchArgs) ? launchArgs : "(none)") << std::endl;
    ss << "Process Command Line: " << GetCommandLineA() << std::endl;
}

namespace
{
using FnMiniDump = void(*)(unsigned int, EXCEPTION_POINTERS*, const char*);
using SetMiniDumpFunctionFn = FnMiniDump(*)(FnMiniDump);
using MinidumpSetUnhandledExceptionFunctionFn = void(*)(FnMiniDump);

void RetailMinidumpCallback(unsigned int, EXCEPTION_POINTERS* exceptionInfo, const char*)
{
    if (exceptionInfo)
        CustomCrashHandler(exceptionInfo);
}
}

void InstallRetailMinidumpCallbacks()
{
    HMODULE retailTier0 = GetModuleHandleW(L"tier0_orig.dll");
    if (!retailTier0)
        return;

    auto setWriter = reinterpret_cast<SetMiniDumpFunctionFn>(
        GetProcAddress(retailTier0, "SetMiniDumpFunction"));
    auto setUnhandled = reinterpret_cast<MinidumpSetUnhandledExceptionFunctionFn>(
        GetProcAddress(retailTier0, "MinidumpSetUnhandledExceptionFunction"));
    FnMiniDump previousWriter = nullptr;
    if (setWriter)
        previousWriter = setWriter(&RetailMinidumpCallback);
    if (setUnhandled)
        setUnhandled(&RetailMinidumpCallback);

    char buffer[224];
    _snprintf_s(
        buffer,
        sizeof(buffer),
        _TRUNCATE,
        "R1Delta: retail minidump callbacks installed writer=%p unhandled=%p previous=%p\n",
        reinterpret_cast<void*>(setWriter),
        reinterpret_cast<void*>(setUnhandled),
        reinterpret_cast<void*>(previousWriter));
    OutputDebugStringA(buffer);

    SetUnhandledExceptionFilter(CustomCrashHandler);
}

// Installation function to set up the exception handler
void InstallExceptionHandler()
{
    SetUnhandledExceptionFilter(CustomCrashHandler);
    // Do NOT register CustomCrashHandler as a first-chance vectored handler.
    // Vectored handlers run before frame-based SEH, and this handler reports and
    // calls TerminateProcess for access violations, so a vectored registration
    // killed the process before any of the __try/__except guards in
    // engine/core/factory.cpp and engine/core/load.cpp could recover the fault
    // (for example the constant-buffer flush guard, which already accepted the
    // exact fault, or the shader-resource and VPK guards). Reporting therefore
    // stays owned by the unhandled pass: a fault a guard recovers continues,
    // and everything else still produces the same report it always did.
    InstallRetailMinidumpCallbacks();
}
