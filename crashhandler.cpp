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
#include "load.h"
#include <shellapi.h>
#include <mmsystem.h>
#include "resource.h"
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "psapi.lib")

// Function declarations
void GetSystemInfoEnhanced(std::stringstream& ss);
void GetCallStack(CONTEXT* context, std::stringstream& ss);
void GetLoadedModules(std::stringstream& ss);
std::string GetExceptionDescription(EXCEPTION_RECORD* record);
void GetRegistersWithContent(CONTEXT* context, std::stringstream& ss);
void DumpStackMemory(CONTEXT* context, std::stringstream& ss, size_t bytesToDump = 512);
std::string GetModuleNameForAddress(HANDLE process, DWORD64 address);
bool IsReadableMemory(LPCVOID address, SIZE_T size);
std::string GetAddressContent(DWORD64 address);
std::string GetWindowsVersionInfo();
std::string GetLastErrorString(DWORD errorCode);

typedef NTSTATUS(WINAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
// Global crash handler function
LONG WINAPI CustomCrashHandler(EXCEPTION_POINTERS* exInfo)
{
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
    if (!IsDedicatedServer()) {
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

    std::stringstream crashLog;
    crashLog << "// SMAAAASH!!!" << std::endl;
    crashLog << "Unfortunately, R1Delta has crashed. Please send this crash report to a programmer in the Discord server.";
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
    crashLog << "Build Date: " << __DATE__ << " " << __TIME__ << std::endl << std::endl;;
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

    // Add call stack info (prioritized higher)
    crashLog << "=== Call Stack ===" << std::endl;
    GetCallStack(exInfo->ContextRecord, crashLog);
    crashLog << std::endl;

    // Add stack memory dump (new section)
    crashLog << "=== Stack Memory Dump ===" << std::endl;
    DumpStackMemory(exInfo->ContextRecord, crashLog);
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
        crashLog << (char*)(G_engine + 0x2291560) << std::endl;
    }
    // Write crash log to file
    std::ofstream logFile(filepath);
    if (logFile.is_open()) {
        logFile << crashLog.str();
        logFile.close();
        if (!IsDedicatedServer()) {

            ShellExecuteA(NULL, "open", "notepad.exe", filepath, NULL, SW_SHOWNORMAL);
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

// Check if memory is readable
bool IsReadableMemory(LPCVOID address, SIZE_T size)
{
    if (address == nullptr) return false;

    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(address, &mbi, sizeof(mbi)) == 0) return false;

    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect == PAGE_NOACCESS || mbi.Protect == PAGE_GUARD) return false;

    // Try to read the memory to absolutely confirm
    __try {
        volatile char test = *((char*)address);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Check if buffer looks like a string
bool LooksLikeString(const char* buffer, size_t length)
{
    if (length < 4) return false; // Too short to be useful

    // Must contain mostly printable ASCII
    int printable = 0;
    int nulls = 0;

    for (size_t i = 0; i < length; i++) {
        char c = buffer[i];
        if (c == 0) {
            nulls++;
            if (i > 0 && i < length - 1) return true; // Found null terminator not at edges
        }
        else if (c >= 32 && c <= 126) {
            printable++;
        }
    }

    return (double)printable / length > 0.7; // At least 70% printable chars
}

// Get content for an address
std::string GetAddressContent(DWORD64 address)
{
    if (!IsReadableMemory((LPCVOID)address, 64)) return "";

    char buffer[65] = { 0 };
    SIZE_T bytesRead = 0;

    if (ReadProcessMemory(GetCurrentProcess(), (LPCVOID)address, buffer, 64, &bytesRead)) {
        // Check if it's a readable string
        if (LooksLikeString(buffer, bytesRead)) {
            std::string result = "\"";
            for (size_t i = 0; i < bytesRead && buffer[i] != 0; i++) {
                char c = buffer[i];
                if (c >= 32 && c <= 126) {
                    result += c;
                }
                else {
                    result += '.';
                }

                if (i >= 32) { // Don't display too much text
                    result += "...\"";
                    return result;
                }
            }
            result += "\"";
            return result;
        }
        else {
            // Return hex representation of first few bytes
            std::stringstream ss;
            ss << "[";
            for (size_t i = 0; i < (std::min)((size_t)16, bytesRead); i++) {
                if (i > 0) ss << " ";
                ss << std::hex << std::setw(2) << std::setfill('0')
                    << (int)(unsigned char)buffer[i];
            }
            ss << "]";
            return ss.str();
        }
    }
    return "";
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
    DWORD64 stackPointer = context->Rsp;
#else
    DWORD64 stackPointer = context->Esp;
#endif

    // Ensure bytesToDump is multiple of 16
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

    do {
        MODULEINFO moduleInfo;
        if (GetModuleInformation(process, me32.hModule, &moduleInfo, sizeof(moduleInfo))) {
            ss << "[" << std::setw(2) << moduleCount++ << "] "
                << me32.szModule << " (Base: 0x" << std::hex << (ULONG_PTR)me32.hModule
                << ", Size: 0x" << moduleInfo.SizeOfImage
                << ", Region: 0x" << (ULONG_PTR)me32.hModule << " - 0x"
                << (ULONG_PTR)me32.hModule + moduleInfo.SizeOfImage << ")" << std::dec << std::endl;
        }
        else {
            ss << "[" << std::setw(2) << moduleCount++ << "] "
                << me32.szModule << " (Base: 0x" << std::hex << (ULONG_PTR)me32.hModule
                << ", Size: Unknown)" << std::dec << std::endl;
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

    // Try to get GPU info from registry
    HKEY hKeyDisplay;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\0000", 0, KEY_READ, &hKeyDisplay) == ERROR_SUCCESS) {
        char gpuDesc[256];
        DWORD bufSize = sizeof(gpuDesc);
        if (RegQueryValueExA(hKeyDisplay, "DriverDesc", NULL, NULL, (LPBYTE)gpuDesc, &bufSize) == ERROR_SUCCESS) {
            ss << "GPU Model: " << gpuDesc << std::endl;
        }

        // Try to get video memory info
        DWORD dedicatedVideoMemory = 0;
        bufSize = sizeof(dedicatedVideoMemory);
        if (RegQueryValueExA(hKeyDisplay, "HardwareInformation.qwMemorySize", NULL, NULL, (LPBYTE)&dedicatedVideoMemory, &bufSize) == ERROR_SUCCESS) {
            ss << "VRAM: " << dedicatedVideoMemory / (1024 * 1024) << " MB" << std::endl;
        }

        RegCloseKey(hKeyDisplay);
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
}

// Installation function to set up the exception handler
void InstallExceptionHandler()
{
    SetUnhandledExceptionFilter(CustomCrashHandler);
    AddVectoredExceptionHandler(1, (PVECTORED_EXCEPTION_HANDLER)CustomCrashHandler);
}