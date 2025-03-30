#include <windows.h>
#include <stdio.h>
#include <string>
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
const char* DEDICATED_DLL_NAME_A = "bin\\x64_retail\\dedicated.dll";

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
    if (!path || path[0] == '\0') return false;

    char validationFilePath[MAX_PATH];
    if (PathCombineA(validationFilePath, path, VALIDATION_FILE_RELATIVE_PATH_A) == NULL)
    {
        OutputDebugStringA("Failed to combine path for validation file.\n");
        return false;
    }

    // PathFileExistsA returns TRUE if it exists, FALSE otherwise
    BOOL exists = PathFileExistsA(validationFilePath);

    char msg[MAX_PATH + 100];
    sprintf_s(msg, sizeof(msg), "Validating path '%s': Checking for '%s' -> %s\n",
        path, validationFilePath, exists ? "Found" : "Not Found");
    OutputDebugStringA(msg);

    return exists == TRUE;
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
        //currentPath = ""; // Treat as empty if retrieval fails
        OutputDebugStringA("Warning: Failed to get current PATH environment variable.\n");
    }

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
    size_t requiredSize = strlen(r1DeltaBinDelta) + strlen(r1DeltaBin) + strlen(retailBin) + strlen(r1RetailBin) + strlen(currentPath) + 6;

    // Allocate buffer for the new PATH string
    std::vector<char> newPathBuffer(requiredSize);

    // Construct the new PATH string
    sprintf_s(newPathBuffer.data(), requiredSize, "%s;%s;%s;%s;%s",
        r1DeltaBinDelta,
        r1DeltaBin,
        retailBin,
        r1RetailBin,
        // ".\\", // Add current directory (game dir)? Generally done by LoadLibrary search order anyway.
        currentPath);

    // Set the new PATH environment variable
    if (_putenv_s("PATH", newPathBuffer.data()) != 0)
    {
        OutputDebugStringA("Error: Failed to set the PATH environment variable.\n");
        if (currentPath != nullptr && currentPath[0] != '\0') free(currentPath); // Free memory allocated by _dupenv_s
        return false;
    }

    OutputDebugStringA("Successfully updated PATH environment variable.\n");
    OutputDebugStringA("New PATH prefix:\n");
    OutputDebugStringA("  "); OutputDebugStringA(r1DeltaBinDelta); OutputDebugStringA("\n");
    OutputDebugStringA("  "); OutputDebugStringA(r1DeltaBin); OutputDebugStringA("\n");
    OutputDebugStringA("  "); OutputDebugStringA(retailBin); OutputDebugStringA("\n");
    OutputDebugStringA("  "); OutputDebugStringA(r1RetailBin); OutputDebugStringA("\n");


    if (currentPath != nullptr && currentPath[0] != '\0') free(currentPath); // Free memory allocated by _dupenv_s
    return true;
}

// Formats and displays a fatal error message then exits.
void FatalError(const char* message, const char* title = "R1Delta Dedicated Launcher Error")
{
    MessageBoxA(NULL, message, title, MB_OK | MB_ICONERROR);
    ExitProcess(1);
}

// Formats and displays a LoadLibrary error message then exits.
void LibraryLoadError(const char* libName, const char* location, DWORD errorCode)
{
    char* pszError = NULL;
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&pszError, 0, NULL);

    char szBuf[1024];
    sprintf_s(szBuf, sizeof(szBuf),
        "Failed to load required DLL:\n  %s\n"
        "Expected Location (relative to game dir):\n  %s\n\n"
        "Error Code: %lu\n"
        "System Message: %s\n\n"
        "Ensure the game directory is correct and all R1Delta files are present in the launcher directory.",
        libName, location, errorCode, pszError ? pszError : "Unknown error");

    if (pszError) LocalFree(pszError);

    FatalError(szBuf, "DLL Load Error");
}

// --- WinMain Entry Point ---
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
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
                " B) Place this dedicated launcher directly inside the main Titanfall game folder.",
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
        LibraryLoadError(DEDICATED_DLL_NAME_A, DEDICATED_DLL_NAME_A, GetLastError());
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
    return result;
}