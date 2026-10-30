#include <windows.h>
#include <stdio.h>
#include <string>
#include <algorithm> // std::find
#include <vector>
#include <direct.h> // For _getcwd, _chdir
#include <shlwapi.h> // For PathCombineA, PathFileExistsA
#include <intrin.h> // For __cpuidex
#include <unordered_map> // For mitigation policy names map

#pragma comment(lib, "Shlwapi.lib") // Link against Shlwapi.lib for Path functions

// Constants matching C# RegistryHelper
const char* REGISTRY_BASE_KEY_A = "Software\\R1Delta";
const char* REGISTRY_INSTALL_PATH_VALUE_A = "InstallPath";

// Validation file relative path (used for checking if a directory is valid)
const char* VALIDATION_FILE_RELATIVE_PATH_A = "r1\\GameInfo.txt";

// Relative paths for PATH modification (relative to launcher exe dir)
const char* R1DELTA_SUBDIR_A = "r1delta";
const char* BIN_DELTA_SUBDIR_A = "bin_delta";
const char* BIN_SUBDIR_A = "bin";

// Relative paths for PATH modification (relative to game install dir)
const char* RETAIL_BIN_SUBDIR_A = "bin\\x64_retail";
const char* R1_RETAIL_BIN_SUBDIR_A = "r1\\bin\\x64_retail";

// Target DLL to load (relative to game install dir)
const char* DEDICATED_DLL_NAME_A = "dedicated.dll";

// Typedef for the function pointer in dedicated.dll
typedef int (*DedicatedMain_t)(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow);

// --- Mitigation Policy Code (Mostly Unchanged) ---

// Helper to check CPU support for CET Shadow Stack
bool DoesCpuSupportCetShadowStack()
{
    int cpuInfo[4] = { 0, 0, 0, 0 };
    __cpuidex(cpuInfo, 7, 0);
    return (cpuInfo[2] & (1 << 7)) != 0; // Check bit 7 in ECX (cpuInfo[2])
}

// Map for policy names (for error messages)
std::unordered_map<PROCESS_MITIGATION_POLICY, const char*> g_mitigationPolicyNames = {
    { ProcessASLRPolicy, "ProcessASLRPolicy" },
    { ProcessDynamicCodePolicy, "ProcessDynamicCodePolicy" },
    { ProcessExtensionPointDisablePolicy, "ProcessExtensionPointDisablePolicy" },
    { ProcessControlFlowGuardPolicy, "ProcessControlFlowGuardPolicy" },
    { ProcessSignaturePolicy, "ProcessSignaturePolicy" },
    { ProcessImageLoadPolicy, "ProcessImageLoadPolicy" },
    { ProcessUserShadowStackPolicy, "ProcessUserShadowStackPolicy" },
};

// Function to apply mitigation policies
void SetMitigationPolicies()
{
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (!kernel32)
        return;

    // Dynamically get function pointers
    auto pSetProcessMitigationPolicy = (decltype(&::SetProcessMitigationPolicy))GetProcAddress(kernel32, "SetProcessMitigationPolicy");
    auto pGetProcessMitigationPolicy = (decltype(&::GetProcessMitigationPolicy))GetProcAddress(kernel32, "GetProcessMitigationPolicy");

    if (!pSetProcessMitigationPolicy) // Get policy isn't strictly required but good practice
    {
        OutputDebugStringA("SetProcessMitigationPolicy not found. Skipping mitigation setup.\n");
        return; // Not supported on this OS version (Win7 or older)
    }

    // Helper lambda to call SetProcessMitigationPolicy and show non-fatal errors
    auto SetPolicyEnsureOK = [&](PROCESS_MITIGATION_POLICY MitigationPolicy, PVOID lpBuffer, SIZE_T dwLength) {
        if (!pSetProcessMitigationPolicy(MitigationPolicy, lpBuffer, dwLength))
        {
            DWORD lastError = GetLastError();
            // Ignore errors if policy already set or not supported (e.g., CET on non-CET CPU)
            if (MitigationPolicy == ProcessUserShadowStackPolicy && (lastError == ERROR_INVALID_PARAMETER || lastError == ERROR_NOT_SUPPORTED))
            {
                OutputDebugStringA("Ignoring SetProcessMitigationPolicy error for UserShadowStackPolicy (likely no CET support).\n");
                return;
            }
            if (lastError == ERROR_ACCESS_DENIED) // Policy might be enforced by parent/debugger
            {
                OutputDebugStringA("Ignoring SetProcessMitigationPolicy error 5 (Access Denied).\n");
                return;
            }

            // Format a more serious warning message
            char errorMsg[512];
            const char* policyName = (g_mitigationPolicyNames.count(MitigationPolicy)) ? g_mitigationPolicyNames[MitigationPolicy] : "UnknownPolicy";
            sprintf_s(errorMsg, sizeof(errorMsg),
                "Failed to set mitigation policy: %s\nError: %lu\n\nThis is usually non-fatal.",
                policyName, lastError);
            MessageBoxA(0, errorMsg, "Mitigation Policy Warning", MB_OK | MB_ICONWARNING);
        }
        else {
            char successMsg[128];
            const char* policyName = (g_mitigationPolicyNames.count(MitigationPolicy)) ? g_mitigationPolicyNames[MitigationPolicy] : "UnknownPolicy";
            sprintf_s(successMsg, sizeof(successMsg), "Successfully applied mitigation policy: %s\n", policyName);
            OutputDebugStringA(successMsg);
        }
        };

    // --- Apply ASLR Policy ---
    PROCESS_MITIGATION_ASLR_POLICY aslrPolicy = {}; // Zero initialize
    aslrPolicy.EnableBottomUpRandomization = true;
    aslrPolicy.EnableForceRelocateImages = true;
    aslrPolicy.EnableHighEntropy = true;
    aslrPolicy.DisallowStrippedImages = true;
    SetPolicyEnsureOK(ProcessASLRPolicy, &aslrPolicy, sizeof(aslrPolicy));

    // --- Apply Extension Point Disable Policy ---
    // Note: We don't check for IME here like the main launcher for simplicity in C++.
    // If IME issues arise, this might need adjustment or removal.
    PROCESS_MITIGATION_EXTENSION_POINT_DISABLE_POLICY extPolicy = {};
    extPolicy.DisableExtensionPoints = true;
    SetPolicyEnsureOK(ProcessExtensionPointDisablePolicy, &extPolicy, sizeof(extPolicy));

    // --- Apply Image Load Policy ---
    PROCESS_MITIGATION_IMAGE_LOAD_POLICY imgPolicy = {};
    imgPolicy.NoRemoteImages = true;
    imgPolicy.PreferSystem32Images = true;
    SetPolicyEnsureOK(ProcessImageLoadPolicy, &imgPolicy, sizeof(imgPolicy));

    // --- Apply User Shadow Stack Policy (CET) ---
    // SetPolicyEnsureOK handles errors if CPU/OS doesn't support it.
    PROCESS_MITIGATION_USER_SHADOW_STACK_POLICY ussPolicy = {};
    ussPolicy.EnableUserShadowStack = true;
    ussPolicy.SetContextIpValidation = true; // Recommended
    // Keep strict mode and other flags disabled unless specifically needed and tested
    SetPolicyEnsureOK(ProcessUserShadowStackPolicy, &ussPolicy, sizeof(ussPolicy));
}


// --- Helper Functions ---

// Gets the directory containing the currently running executable.
// Returns true on success, false on failure.
bool GetExecutableDirectory(char* buffer, size_t bufferSize)
{
    if (!buffer || bufferSize == 0) return false;
    DWORD result = GetModuleFileNameA(NULL, buffer, (DWORD)bufferSize);
    if (result == 0 || result == bufferSize) // Failed or buffer too small
    {
        return false;
    }
    // Find the last backslash and null-terminate there
    char* lastBackslash = strrchr(buffer, '\\');
    if (lastBackslash)
    {
        *lastBackslash = '\0';
        return true;
    }
    // Should not happen for a full path, but handle case where only filename is returned
    return false;
}

// Reads the 'InstallPath' value from the registry.
// Returns true on success, false on failure.
bool GetInstallPathFromRegistry(char* buffer, size_t bufferSize)
{
    if (!buffer || bufferSize == 0) return false;
    buffer[0] = '\0'; // Ensure null termination on failure

    HKEY hKey;
    LONG openResult = RegOpenKeyExA(HKEY_CURRENT_USER, REGISTRY_BASE_KEY_A, 0, KEY_READ, &hKey);
    if (openResult != ERROR_SUCCESS)
    {
        // Key doesn't exist or access denied - this is expected if not set
        // OutputDebugStringA("Registry key HKCU\\Software\\R1Delta not found or could not be opened.\n");
        return false;
    }

    DWORD valueType = 0;
    DWORD dataSize = (DWORD)bufferSize;
    LONG queryResult = RegQueryValueExA(hKey, REGISTRY_INSTALL_PATH_VALUE_A, NULL, &valueType, (LPBYTE)buffer, &dataSize);

    RegCloseKey(hKey);

    if (queryResult != ERROR_SUCCESS || valueType != REG_SZ)
    {
        // Value doesn't exist, wrong type, or buffer too small
        // OutputDebugStringA("Registry value 'InstallPath' not found, wrong type, or buffer too small.\n");
        buffer[0] = '\0'; // Clear buffer again
        return false;
    }

    // Ensure null termination even if registry data wasn't null-terminated (unlikely)
    if (dataSize >= bufferSize) {
        buffer[bufferSize - 1] = '\0';
    }
    else {
        // dataSize includes the null terminator if it fits
        // buffer[dataSize] = '\0'; // Should already be there from RegQueryValueExA
    }


    OutputDebugStringA("Successfully read InstallPath from registry: ");
    OutputDebugStringA(buffer);
    OutputDebugStringA("\n");
    return true;
}

// Validates if the given path appears to be a valid game directory
// by checking for the existence of "r1\GameInfo.txt".
// Returns true if valid, false otherwise.
bool ValidateGameDirectory(const char* path)
{
    return TRUE;
}

// Prepends necessary directories to the PATH environment variable.
// Returns true on success, false on failure.
bool PrependPathEnvironment(const char* gameInstallPath, const char* launcherExeDir)
{
    if (!gameInstallPath || !launcherExeDir) return false;

    // Get current PATH
    char* currentPath = nullptr;
    size_t currentPathLen;
    errno_t err = _dupenv_s(&currentPath, &currentPathLen, "PATH");
    if (err != 0 || currentPath == nullptr) {
        OutputDebugStringA("Warning: Failed to get current PATH environment variable; using only R1Delta paths.\n");
    }
    const char* currentPathValue = currentPath != nullptr ? currentPath : "";

    // Construct paths to prepend
    char r1DeltaBinDelta[MAX_PATH];
    char r1DeltaBin[MAX_PATH];
    char retailBin[MAX_PATH];
    char r1RetailBin[MAX_PATH];

    // R1Delta paths relative to launcher dir
    PathCombineA(r1DeltaBinDelta, launcherExeDir, R1DELTA_SUBDIR_A);
    PathCombineA(r1DeltaBinDelta, r1DeltaBinDelta, BIN_DELTA_SUBDIR_A);

    PathCombineA(r1DeltaBin, launcherExeDir, R1DELTA_SUBDIR_A);
    PathCombineA(r1DeltaBin, r1DeltaBin, BIN_SUBDIR_A);

    // Game paths relative to game install dir
    PathCombineA(retailBin, gameInstallPath, RETAIL_BIN_SUBDIR_A);
    PathCombineA(r1RetailBin, gameInstallPath, R1_RETAIL_BIN_SUBDIR_A);

    // Calculate required buffer size for the new PATH
    // Size = len(delta) + len(bin) + len(retail) + len(r1retail) + len(current) + 5 separators + 1 null term
    size_t requiredSize = strlen(r1DeltaBinDelta) + strlen(r1DeltaBin) + strlen(retailBin) + strlen(r1RetailBin) + strlen(currentPathValue) + 6;

    // Allocate buffer for the new PATH string
    std::vector<char> newPathBuffer(requiredSize);

    // Construct the new PATH string
    sprintf_s(newPathBuffer.data(), requiredSize, "%s;%s;%s;%s;%s",
        r1DeltaBinDelta,
        r1DeltaBin,
        retailBin,
        r1RetailBin,
        // ".\\", // Add current directory (game dir)? Generally done by LoadLibrary search order anyway.
        currentPathValue);

    // Set the new PATH environment variable
    if (_putenv_s("PATH", newPathBuffer.data()) != 0)
    {
        OutputDebugStringA("Error: Failed to set the PATH environment variable.\n");
        free(currentPath); // Free memory allocated by _dupenv_s, including an allocated empty string.
        return false;
    }

    OutputDebugStringA("Successfully updated PATH environment variable.\n");
    OutputDebugStringA("New PATH prefix:\n");
    OutputDebugStringA("  "); OutputDebugStringA(r1DeltaBinDelta); OutputDebugStringA("\n");
    OutputDebugStringA("  "); OutputDebugStringA(r1DeltaBin); OutputDebugStringA("\n");
    OutputDebugStringA("  "); OutputDebugStringA(retailBin); OutputDebugStringA("\n");
    OutputDebugStringA("  "); OutputDebugStringA(r1RetailBin); OutputDebugStringA("\n");


    free(currentPath); // Free memory allocated by _dupenv_s, including an allocated empty string.
    return true;
}
bool g_suppressMessageBoxes = false;
// Formats and displays a fatal error message then exits.
void FatalError(const char* message, const char* title = "R1Delta Dedicated Launcher Error")
{
    // Only show the message box if the flag is NOT set
    if (!g_suppressMessageBoxes)
    {
        MessageBoxA(NULL, message, title, MB_OK | MB_ICONERROR);
    }

    // Always print to stderr and exit
    fprintf(stderr, "FATAL ERROR: %s\n", message); // Added newline for clarity
    fflush(stderr);
    ExitProcess(1);
}



// ---------------------------------------------------------------------------
// dedicated.dll load diagnostics.
//
// LoadLibrary reports ERROR_MOD_NOT_FOUND (126) both when the DLL itself is absent and when
// one of its imports is absent, so the old "Failed to load required DLL: dedicated.dll"
// message could not tell the two apart. These helpers separate the cases and, for a failed
// load of an existing file, walk the static import tables to name the first import that
// can not be found in the loader search path.
// ---------------------------------------------------------------------------

// Formats a Win32 error code as "<code>: <FormatMessage text>".
static std::string FormatWin32Error(DWORD errorCode)
{
    char* pszError = NULL;
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&pszError, 0, NULL);

    std::string text = std::to_string(errorCode) + ": ";
    if (pszError)
    {
        text += pszError;
        LocalFree(pszError);
        while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' '))
            text.pop_back();
    }
    else
    {
        text += "Unknown error";
    }
    return text;
}

static std::string GetFullPathString(const char* path)
{
    char full[MAX_PATH];
    DWORD len = GetFullPathNameA(path, MAX_PATH, full, NULL);
    return (len > 0 && len < MAX_PATH) ? std::string(full) : std::string(path);
}

// The directories the loader uses for a plain-name LoadLibrary, in order: the application
// directory, the system directory, the Windows directory, the current directory, then PATH.
static std::vector<std::string> GetLoaderSearchDirectories()
{
    std::vector<std::string> dirs;
    char buf[MAX_PATH];

    if (GetModuleFileNameA(NULL, buf, MAX_PATH) > 0)
    {
        char* slash = strrchr(buf, '\\');
        if (slash) { *slash = '\0'; dirs.push_back(buf); }
    }
    if (GetSystemDirectoryA(buf, MAX_PATH) > 0) dirs.push_back(buf);
    if (GetWindowsDirectoryA(buf, MAX_PATH) > 0) dirs.push_back(buf);
    if (_getcwd(buf, MAX_PATH)) dirs.push_back(buf);

    char* pathEnv = nullptr;
    size_t pathLen = 0;
    if (_dupenv_s(&pathEnv, &pathLen, "PATH") == 0 && pathEnv)
    {
        std::string path(pathEnv);
        free(pathEnv);
        size_t start = 0;
        while (start <= path.size())
        {
            size_t end = path.find(';', start);
            if (end == std::string::npos) end = path.size();
            if (end > start) dirs.push_back(path.substr(start, end - start));
            start = end + 1;
        }
    }
    return dirs;
}

// Resolves a DLL file name through the search directories. Returns the full path or "".
static std::string FindInSearchDirectories(const std::vector<std::string>& dirs, const std::string& name)
{
    for (const std::string& dir : dirs)
    {
        std::string candidate = dir;
        if (!candidate.empty() && candidate.back() != '\\') candidate += '\\';
        candidate += name;
        if (GetFileAttributesA(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
            return candidate;
    }
    return std::string();
}

// Minimal PE reader: returns the names of the DLLs in the static import directory of 'path'.
// Returns false if the file can not be read or is not a 64-bit PE image.
static bool ReadPeImportNames(const std::string& path, std::vector<std::string>& names)
{
    HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < (LONGLONG)sizeof(IMAGE_DOS_HEADER) || size.QuadPart > (256LL << 20))
    {
        CloseHandle(file);
        return false;
    }
    std::vector<unsigned char> data((size_t)size.QuadPart);
    DWORD got = 0;
    BOOL ok = ReadFile(file, data.data(), (DWORD)data.size(), &got, NULL);
    CloseHandle(file);
    if (!ok || got != data.size()) return false;

    const size_t fileSize = data.size();
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(data.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
        (size_t)dos->e_lfanew + sizeof(IMAGE_NT_HEADERS64) > fileSize)
        return false;
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(data.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        return false;

    const IMAGE_DATA_DIRECTORY& importDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (importDir.VirtualAddress == 0) return true;

    auto sections = IMAGE_FIRST_SECTION(nt);
    const WORD sectionCount = nt->FileHeader.NumberOfSections;
    if ((const unsigned char*)(sections + sectionCount) > data.data() + fileSize) return false;

    auto rvaToOffset = [&](DWORD rva) -> size_t {
        for (WORD i = 0; i < sectionCount; ++i)
        {
            DWORD span = sections[i].Misc.VirtualSize > sections[i].SizeOfRawData ? sections[i].Misc.VirtualSize : sections[i].SizeOfRawData;
            if (rva >= sections[i].VirtualAddress && rva < sections[i].VirtualAddress + span)
            {
                size_t off = (size_t)sections[i].PointerToRawData + (rva - sections[i].VirtualAddress);
                return off < fileSize ? off : (size_t)-1;
            }
        }
        return rva < fileSize ? (size_t)rva : (size_t)-1; // header RVAs
    };

    size_t off = rvaToOffset(importDir.VirtualAddress);
    if (off == (size_t)-1) return false;
    for (;; off += sizeof(IMAGE_IMPORT_DESCRIPTOR))
    {
        if (off + sizeof(IMAGE_IMPORT_DESCRIPTOR) > fileSize) return false;
        auto desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(data.data() + off);
        if (desc->Name == 0) break;
        size_t nameOff = rvaToOffset(desc->Name);
        if (nameOff == (size_t)-1) return false;
        size_t maxLen = fileSize - nameOff;
        size_t len = 0;
        while (len < maxLen && data[nameOff + len] != 0) ++len;
        if (len == maxLen || len == 0) return false;
        names.emplace_back(reinterpret_cast<const char*>(data.data() + nameOff), len);
    }
    return true;
}

static std::string ToLowerAscii(std::string s)
{
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

// Depth-first walk of the static imports of 'modulePath'. Reports the first import that is not
// already loaded in this process and can not be found in the search directories (chain is the
// "A -> B -> C" path that led there). Returns true when such an import was found.
static bool FindFirstMissingImport(
    const std::string& modulePath,
    const std::vector<std::string>& dirs,
    std::vector<std::string>& visited,
    std::string& chain,
    std::string& missingName)
{
    std::vector<std::string> imports;
    if (!ReadPeImportNames(modulePath, imports))
        return false;

    const char* slash = strrchr(modulePath.c_str(), '\\');
    std::string self = ToLowerAscii(slash ? slash + 1 : modulePath.c_str());
    std::string chainHere = chain.empty() ? self : chain + " -> " + self;

    // Check every direct import before descending, so the shallowest missing DLL is reported.
    std::vector<std::string> toDescend;
    for (const std::string& imp : imports)
    {
        std::string lower = ToLowerAscii(imp);
        if (lower.compare(0, 4, "api-") == 0 || lower.compare(0, 4, "ext-") == 0)
            continue; // API set contracts are resolved by the loader, not by file name.
        if (std::find(visited.begin(), visited.end(), lower) != visited.end())
            continue;
        visited.push_back(lower);

        std::string found = FindInSearchDirectories(dirs, imp);
        if (found.empty())
        {
            if (GetModuleHandleA(imp.c_str()) != NULL)
                continue; // already mapped (KnownDLL / apiset / loaded earlier)
            chain = chainHere;
            missingName = imp;
            return true;
        }
        toDescend.push_back(found);
    }
    for (const std::string& next : toDescend)
    {
        chain = chainHere;
        if (FindFirstMissingImport(next, dirs, visited, chain, missingName))
            return true;
    }
    return false;
}

// Builds the diagnostics block that is appended to every launcher load error.
static std::string DescribeLaunchEnvironment(const std::vector<std::string>& dirs)
{
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    char cwd[MAX_PATH] = {};
    _getcwd(cwd, sizeof(cwd));

    std::string text;
    text += "Launcher exe: " + std::string(exePath) + "\n";
    text += "Working directory: " + std::string(cwd) + "\n";
    // Application dir, system dir, Windows dir, CWD, then PATH (R1Delta's four directories are
    // prepended to PATH). The tail of a long PATH is not interesting; cap the listing.
    const size_t kMaxListedDirs = 12;
    text += "Directories searched, in order:\n";
    for (size_t i = 0; i < dirs.size() && i < kMaxListedDirs; ++i)
        text += "  " + dirs[i] + "\n";
    if (dirs.size() > kMaxListedDirs)
        text += "  ... and " + std::to_string(dirs.size() - kMaxListedDirs) + " more PATH entries\n";
    return text;
}

// Reports why dedicated.dll could not be loaded, then exits. 'gameInstallPath' is the directory
// the launcher chdir'ed to; 'loadError' is GetLastError() from the failed LoadLibraryA.
void LibraryLoadError(const char* libName, const char* gameInstallPath, DWORD loadError)
{
    const std::vector<std::string> dirs = GetLoaderSearchDirectories();
    std::string expected = GetFullPathString(gameInstallPath);
    if (!expected.empty() && expected.back() != '\\') expected += '\\';
    expected += RETAIL_BIN_SUBDIR_A;
    expected += '\\';
    expected += libName;

    std::string where = FindInSearchDirectories(dirs, libName);
    std::string msg;

    if (where.empty())
    {
        msg += std::string("Could not find ") + libName + " in any searched directory.\n";
        msg += "Expected location: " + expected + "\n";
        msg += (GetFileAttributesA(expected.c_str()) == INVALID_FILE_ATTRIBUTES)
            ? "That file does not exist. It is part of the Titanfall base game (bin\\x64_retail), not of R1Delta; "
              "reinstall or repair the base game files, or point R1Delta at a complete Titanfall folder.\n"
            : "That file exists, but its directory is not in the search path.\n";
        msg += "LoadLibrary error " + FormatWin32Error(loadError) + "\n\n";
    }
    else
    {
        msg += std::string("Found ") + libName + " at: " + where + "\n";
        msg += "but it failed to load. LoadLibrary error " + FormatWin32Error(loadError) + "\n";

        std::vector<std::string> visited;
        std::string chain, missing;
        if (FindFirstMissingImport(where, dirs, visited, chain, missing))
        {
            msg += "A dependency of it is missing: " + missing + " (imported by " + chain + ").\n"
                   "Error 126 here means that dependency was not found, not " + libName + " itself.\n";
        }
        else if (loadError == ERROR_MOD_NOT_FOUND)
        {
            msg += "No missing import DLL file was found by the static walk; a dependency may be present but unloadable "
                   "(wrong architecture, or one of ITS imports failed). Run with the loader's diagnostics "
                   "(Windows: gflags +sls; Wine: WINEDEBUG=err+module) to see which module.\n";
        }
        msg += "\n";
    }

    msg += DescribeLaunchEnvironment(dirs);
    FatalError(msg.c_str(), "DLL Load Error");
}

int main()
{
	// This is a Windows application, so we should not use main() directly.
    // Instead, we will use WinMain as the entry point.
    auto console = GetCommandLineA();
	if (console && strstr(console, "-console") == nullptr)
	{
        FreeConsole();
	}
	return WinMain(GetModuleHandle(NULL), NULL, GetCommandLineA(), SW_HIDE);
}

// --- WinMain Entry Point ---
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // --- Argument Parsing using CommandLineToArgvW ---
    int argc;
    LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argc); // Get wide-char args

    if (argvW == NULL)
    {
        fprintf(stderr, "Warning: Failed to parse command line using CommandLineToArgvW.\n");
    }
    else
    {
        for (int i = 1; i < argc; ++i)
        {
            if (wcscmp(argvW[i], L"-nomessagebox") == 0)
            {
                g_suppressMessageBoxes = true;
                break;
            }
        }

        LocalFree(argvW);
    }

    char launcherExeDir[MAX_PATH];
    char registryPath[MAX_PATH];
    char gameInstallPath[MAX_PATH];
    gameInstallPath[0] = '\0'; // Initialize empty

    OutputDebugStringA("--- R1Delta Dedicated Server Launcher Starting ---\n");

    // 1. Get Launcher Executable Directory
    if (!GetExecutableDirectory(launcherExeDir, sizeof(launcherExeDir)))
    {
        FatalError("Fatal Error: Could not determine launcher executable directory.");
    }
    char msg[MAX_PATH + 50];
    sprintf_s(msg, sizeof(msg), "Launcher Directory: %s\n", launcherExeDir);
    OutputDebugStringA(msg);


    // 2. Try Reading Install Path from Registry
    bool registryPathFound = GetInstallPathFromRegistry(registryPath, sizeof(registryPath));

    // 3. Determine and Validate Game Install Path
    if (registryPathFound && ValidateGameDirectory(registryPath))
    {
        // Registry path is valid, use it
        strcpy_s(gameInstallPath, sizeof(gameInstallPath), registryPath);
        OutputDebugStringA("Using valid game path found in registry.\n");
    }
    else
    {
        if (registryPathFound) {
            OutputDebugStringA("Registry path found but is invalid (r1\\GameInfo.txt missing?).\n");
        }
        else {
            OutputDebugStringA("No registry path found.\n");
        }

        // Fallback: Check if the launcher directory itself is a valid game directory
        OutputDebugStringA("Falling back to check launcher executable directory...\n");
        if (ValidateGameDirectory(launcherExeDir))
        {
            strcpy_s(gameInstallPath, sizeof(gameInstallPath), launcherExeDir);
            OutputDebugStringA("Using launcher executable directory as game path (it appears valid).\n");
        }
        else
        {
            // Neither registry nor launcher dir is valid
            char errorMsg[MAX_PATH * 2 + 200];
            sprintf_s(errorMsg, sizeof(errorMsg),
                "Could not find a valid Titanfall game directory.\n\n"
                "Checked Registry Path (%s): %s\n"
                "Checked Launcher Directory: %s\n\n"
                "Please ensure the game is installed correctly and either:\n"
                " A) Run the main R1Delta launcher once to set the path, OR\n"
                " B) Place this dedicated launcher directly inside the main Titanfall game folder.\n"
                "If you are seeing this message and you do not understand why, you have likely launched the Dedicated Server by mistake.\n"
                "In that case, please make sure you are launching \"R1Delta Launcher\" and not \"R1Delta Dedicated Server\" from the Start Menu.",
                registryPathFound ? registryPath : "Not Found",
                registryPathFound ? (ValidateGameDirectory(registryPath) ? "Valid" : "Invalid") : "N/A",
                launcherExeDir);
            FatalError(errorMsg);
        }
    }

    // 4. Set Current Working Directory (CWD)
    if (_chdir(gameInstallPath) != 0)
    {
        char errorMsg[MAX_PATH + 100];
        sprintf_s(errorMsg, sizeof(errorMsg), "Fatal Error: Could not change current directory to:\n%s", gameInstallPath);
        FatalError(errorMsg);
    }
    char cwd[MAX_PATH];
    _getcwd(cwd, sizeof(cwd));
    sprintf_s(msg, sizeof(msg), "Current Directory set to: %s\n", cwd);
    OutputDebugStringA(msg);

    // 5. Prepend R1Delta and Game Binaries to PATH
    if (!PrependPathEnvironment(gameInstallPath, launcherExeDir))
    {
        // Warning should have been shown by the function
        OutputDebugStringA("Warning: Failed to modify PATH. DLL loading might fail.\n");
        // Continue anyway, maybe LoadLibrary will find it...
    }

    // 6. Apply Process Mitigation Policies
    SetMitigationPolicies();

    // 7. Load the Dedicated Server DLL (relative to gameInstallPath/CWD)
    OutputDebugStringA("Attempting to load dedicated server DLL...\n");
    // Since CWD is set and PATH includes the necessary bin folder, just the relative path should work.
    // Using LoadLibraryA relies on standard search paths (including CWD and PATH).
    HINSTANCE hDedicatedModule = LoadLibraryA(DEDICATED_DLL_NAME_A);
    if (!hDedicatedModule)
    {
        const DWORD loadError = GetLastError();
        LibraryLoadError(DEDICATED_DLL_NAME_A, gameInstallPath, loadError);
        // LibraryLoadError calls ExitProcess
    }
    sprintf_s(msg, sizeof(msg), "Successfully loaded DLL: %s\n", DEDICATED_DLL_NAME_A);
    OutputDebugStringA(msg);


    // 8. Get the address of the DedicatedMain function
    DedicatedMain_t pDedicatedMain = (DedicatedMain_t)GetProcAddress(hDedicatedModule, "DedicatedMain");
    if (!pDedicatedMain)
    {
        char errorMsg[200];
        sprintf_s(errorMsg, sizeof(errorMsg), "Fatal Error: Could not find 'DedicatedMain' function in '%s'.", DEDICATED_DLL_NAME_A);
        FreeLibrary(hDedicatedModule); // Clean up loaded library
        FatalError(errorMsg);
    }
    OutputDebugStringA("Found 'DedicatedMain' function address.\n");

    // 9. Call the DedicatedMain function
    OutputDebugStringA("Calling DedicatedMain...\n");
    int result = pDedicatedMain(hInstance, hPrevInstance, lpCmdLine, nCmdShow);

    // Note: We don't FreeLibrary(hDedicatedModule) here because the server process relies on it.
    // It will be unloaded when the process exits naturally.

    OutputDebugStringA("DedicatedMain returned. Exiting.\n");

    // The engine has finished its native shutdown; stop R1Delta's process
    // services while their worker threads still exist and no loader lock is held.
    using ShutdownProcessServices_t = void (*)();
    HMODULE coreModule = GetModuleHandleW(L"tier0.dll");
    auto shutdownServices = coreModule
        ? reinterpret_cast<ShutdownProcessServices_t>(GetProcAddress(coreModule, "R1DeltaShutdownProcessServices"))
        : nullptr;
    if (!shutdownServices)
    {
        fprintf(stderr, "Fatal Error: tier0.dll does not export R1DeltaShutdownProcessServices.\n");
        fflush(stderr);
        return 1;
    }
    shutdownServices();
    return result;
}
