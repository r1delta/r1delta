using Dark.Net;
using R1Delta; // Assuming this namespace contains RegistryHelper and VisualCppInstaller
using Squirrel;
using Squirrel.Sources;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Configuration;
using System.Data;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
// using System.Windows.Forms; // No longer needed
using Application = System.Windows.Application;
using MessageBox = System.Windows.MessageBox; // Use WPF MessageBox
using MessageBoxButton = System.Windows.MessageBoxButton; // Disambiguate
using MessageBoxImage = System.Windows.MessageBoxImage; // Disambiguate
// Note: Squirrel using statement was duplicated, removed one.

namespace launcher_ex
{
    /// <summary>
    /// Interaction logic for App.xaml
    /// </summary>
    public partial class App : Application
    {
        // --- P/Invoke Signatures ---

        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        private static extern IntPtr LoadLibraryW(string lpFileName);

        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        private static extern IntPtr LoadLibraryExW(string lpFileName, IntPtr hFile, uint dwFlags);

        [DllImport("kernel32.dll", CharSet = CharSet.Ansi, ExactSpelling = true, SetLastError = true)]
        private static extern IntPtr GetProcAddress(IntPtr hModule, string procName);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool FreeLibrary(IntPtr hModule);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr GetCurrentProcess();

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool SetProcessMitigationPolicy(PROCESS_MITIGATION_POLICY MitigationPolicy, IntPtr lpBuffer, nuint dwLength);

        // Note: GetProcessMitigationPolicy might not exist on older systems. We'll load it dynamically.
        private delegate bool GetProcessMitigationPolicyDelegate(IntPtr hProcess, PROCESS_MITIGATION_POLICY MitigationPolicy, IntPtr lpBuffer, nuint dwLength);
        private static GetProcessMitigationPolicyDelegate pGetProcessMitigationPolicy = null;

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr GetModuleHandleW(string lpModuleName);

        [DllImport("user32.dll", SetLastError = true)]
        private static extern int GetKeyboardLayoutList(int nBuff, [Out] IntPtr[] lphkl);

        [DllImport("imm32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool ImmIsIME(IntPtr hkl);

        // --- NEW: P/Invoke for checking key state ---
        [DllImport("user32.dll")]
        private static extern short GetAsyncKeyState(int vKey);
        private const int VK_F4 = 0x73; // Virtual key code for F4

        // P/Invoke declaration for FormatMessage
        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        private static extern uint FormatMessage(
            uint dwFlags,
            IntPtr lpSource,
            uint dwMessageId,
            uint dwLanguageId,
            out IntPtr lpBuffer, // Using IntPtr for out buffer allocated by the system
            uint nSize,
            IntPtr[] Arguments); // Changed from va_list* to IntPtr[] for C# marshalling

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr LocalFree(IntPtr hMem);


        // --- Constants ---
        private const uint LOAD_WITH_ALTERED_SEARCH_PATH = 0x00000008;
        internal const string R1DELTA_SUBDIR = "r1delta"; // Made internal for TitanfallManager
        private const string LAUNCHER_DLL_NAME = "launcher.dll";
        private const string BIN_SUBDIR = "bin";
        private const string BIN_DELTA_SUBDIR = "bin_delta";
        private const string RETAIL_BIN_SUBDIR = "bin\\x64_retail";
        // private const string GAME_EXECUTABLE = "Titanfall.exe"; // Game presence check moved
        private const string AUDIO_DONE_FILE = "vpk\\audio_done.txt"; // Relative to game dir
        private const string CAM_LIST_FILE = "vpk\\cam_list.txt"; // Relative to game dir
        private const string TARGET_VPK_FILE = "vpk\\client_mp_common.bsp.pak000_040.vpk"; // Relative to game dir
        private const string AUDIO_INSTALLER_EXE = "bin\\x64\\audio_installer.exe"; // Relative to game dir

        // Constants for FormatMessage
        private const uint FORMAT_MESSAGE_ALLOCATE_BUFFER = 0x00000100;
        private const uint FORMAT_MESSAGE_IGNORE_INSERTS = 0x00000200;
        private const uint FORMAT_MESSAGE_FROM_SYSTEM = 0x00001000;
        private const uint FORMAT_MESSAGE_ARGUMENT_ARRAY = 0x00002000;

        private const string GitHubRepoUrl = "https://github.com/r1delta/r1delta"; // Make sure this is correct
        private const string SquirrelAppName = "R1Delta"; // Needs to match the name used during packaging


        // Delegate for the exported C++ function (Unchanged)
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void SetR1DeltaLaunchArgsDelegate([MarshalAs(UnmanagedType.LPStr)] string args);


        // --- Mitigation Policy Structures & Enum ---
        #region Mitigation Policy Structs & Enums
        private enum PROCESS_MITIGATION_POLICY
        {
            ProcessDEPPolicy = 0,
            ProcessASLRPolicy = 1,
            ProcessDynamicCodePolicy = 2,
            ProcessStrictHandleCheckPolicy = 3,
            ProcessSystemCallDisablePolicy = 4,
            ProcessMitigationOptionsMask = 5,
            ProcessExtensionPointDisablePolicy = 6,
            ProcessControlFlowGuardPolicy = 7,
            ProcessSignaturePolicy = 8,
            ProcessFontDisablePolicy = 9,
            ProcessImageLoadPolicy = 10,
            ProcessSystemCallFilterPolicy = 11,
            ProcessPayloadRestrictionPolicy = 12,
            ProcessChildProcessPolicy = 13,
            ProcessSideChannelMitigationPolicy = 14,
            ProcessUserShadowStackPolicy = 15, // Requires Windows 10 20H1+ and CET hardware
            ProcessRedirectionTrustPolicy = 16,
            MaxProcessMitigationPolicy = 17
        }

        [Flags]
        private enum MitigationFlags : uint
        {
            Enable = 0x1,
            Disable = 0x2,
            Audit = 0x4 // Often requires specific OS versions
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct PROCESS_MITIGATION_ASLR_POLICY
        {
            public uint Flags;
            public bool EnableBottomUpRandomization => (Flags & (uint)MitigationFlags.Enable) != 0;
            public bool EnableForceRelocateImages => (Flags & (uint)MitigationFlags.Enable << 1) != 0;
            public bool EnableHighEntropy => (Flags & (uint)MitigationFlags.Enable << 2) != 0;
            public bool DisallowStrippedImages => (Flags & (uint)MitigationFlags.Enable << 3) != 0;

            public void SetFlags(bool enableBottomUp, bool enableForceRelocate, bool enableHighEntropy, bool disallowStripped)
            {
                Flags = 0;
                if (enableBottomUp) Flags |= (uint)MitigationFlags.Enable;
                if (enableForceRelocate) Flags |= (uint)MitigationFlags.Enable << 1;
                if (enableHighEntropy) Flags |= (uint)MitigationFlags.Enable << 2;
                if (disallowStripped) Flags |= (uint)MitigationFlags.Enable << 3;
            }
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct PROCESS_MITIGATION_EXTENSION_POINT_DISABLE_POLICY
        {
            public uint Flags;
            public bool DisableExtensionPoints => (Flags & (uint)MitigationFlags.Enable) != 0;

            public void SetFlags(bool disable)
            {
                Flags = disable ? (uint)MitigationFlags.Enable : 0;
            }
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct PROCESS_MITIGATION_IMAGE_LOAD_POLICY
        {
            public uint Flags;
            public bool NoRemoteImages => (Flags & (uint)MitigationFlags.Enable) != 0;
            public bool NoLowMandatoryLabelImages => (Flags & (uint)MitigationFlags.Enable << 1) != 0;
            public bool PreferSystem32Images => (Flags & (uint)MitigationFlags.Enable << 2) != 0;
            public bool AuditNoRemoteImages => (Flags & (uint)MitigationFlags.Audit) != 0;
            public bool AuditNoLowMandatoryLabelImages => (Flags & (uint)MitigationFlags.Audit << 1) != 0;

            public void SetFlags(bool noRemote, bool noLowMandatory, bool preferSystem32, bool auditRemote = false, bool auditLowMandatory = false)
            {
                Flags = 0;
                if (noRemote) Flags |= (uint)MitigationFlags.Enable;
                if (noLowMandatory) Flags |= (uint)MitigationFlags.Enable << 1;
                if (preferSystem32) Flags |= (uint)MitigationFlags.Enable << 2;
                if (auditRemote) Flags |= (uint)MitigationFlags.Audit;
                if (auditLowMandatory) Flags |= (uint)MitigationFlags.Audit << 1;
            }
        }


        [StructLayout(LayoutKind.Sequential)]
        private struct PROCESS_MITIGATION_USER_SHADOW_STACK_POLICY // Needs Win10 20H1+ / Win 11
        {
            public uint Flags;
            public bool EnableUserShadowStack => (Flags & (uint)MitigationFlags.Enable) != 0;
            public bool AuditUserShadowStack => (Flags & (uint)MitigationFlags.Audit) != 0;
            public bool SetContextIpValidation => (Flags & (uint)MitigationFlags.Enable << 1) != 0;
            public bool AuditSetContextIpValidation => (Flags & (uint)MitigationFlags.Audit << 1) != 0;
            public bool EnableUserShadowStackStrictMode => (Flags & (uint)MitigationFlags.Enable << 2) != 0; // Needs later Win10/Win11
            public bool BlockNonCetBinaries => (Flags & (uint)MitigationFlags.Enable << 3) != 0;
            public bool BlockNonCetBinariesNonEhcont => (Flags & (uint)MitigationFlags.Enable << 4) != 0;
            public bool AuditBlockNonCetBinaries => (Flags & (uint)MitigationFlags.Audit << 3) != 0;
            public bool AuditBlockNonCetBinariesNonEhcont => (Flags & (uint)MitigationFlags.Audit << 4) != 0;
            public bool CetDynamicAptFix => (Flags & (uint)MitigationFlags.Enable << 5) != 0;
            public bool UserShadowStackStrictAlignment => (Flags & (uint)MitigationFlags.Enable << 6) != 0;


            public void SetFlags(bool enable, bool audit, bool setContext, bool auditSetContext,
                                 bool strictMode, bool blockNonCet, bool blockNonCetNonEhcont,
                                 bool auditBlockNonCet, bool auditBlockNonCetNonEhcont, bool cetDynamicFix, bool strictAlign)
            {
                Flags = 0;
                if (enable) Flags |= (uint)MitigationFlags.Enable;
                if (audit) Flags |= (uint)MitigationFlags.Audit;
                if (setContext) Flags |= (uint)MitigationFlags.Enable << 1;
                if (auditSetContext) Flags |= (uint)MitigationFlags.Audit << 1;
                if (strictMode) Flags |= (uint)MitigationFlags.Enable << 2;
                if (blockNonCet) Flags |= (uint)MitigationFlags.Enable << 3;
                if (blockNonCetNonEhcont) Flags |= (uint)MitigationFlags.Enable << 4;
                if (auditBlockNonCet) Flags |= (uint)MitigationFlags.Audit << 3;
                if (auditBlockNonCetNonEhcont) Flags |= (uint)MitigationFlags.Audit << 4;
                if (cetDynamicFix) Flags |= (uint)MitigationFlags.Enable << 5;
                if (strictAlign) Flags |= (uint)MitigationFlags.Enable << 6;
            }
        }

        private static readonly Dictionary<PROCESS_MITIGATION_POLICY, string> MitigationPolicyNames = new Dictionary<PROCESS_MITIGATION_POLICY, string>
        {
            { PROCESS_MITIGATION_POLICY.ProcessASLRPolicy, "ProcessASLRPolicy" },
            { PROCESS_MITIGATION_POLICY.ProcessDynamicCodePolicy, "ProcessDynamicCodePolicy" },
            { PROCESS_MITIGATION_POLICY.ProcessExtensionPointDisablePolicy, "ProcessExtensionPointDisablePolicy" },
            { PROCESS_MITIGATION_POLICY.ProcessControlFlowGuardPolicy, "ProcessControlFlowGuardPolicy" },
            { PROCESS_MITIGATION_POLICY.ProcessSignaturePolicy, "ProcessSignaturePolicy" },
            { PROCESS_MITIGATION_POLICY.ProcessImageLoadPolicy, "ProcessImageLoadPolicy" },
            { PROCESS_MITIGATION_POLICY.ProcessUserShadowStackPolicy, "ProcessUserShadowStackPolicy" },
        };
        #endregion

        // Define the delegate for LauncherMain
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        delegate int LauncherMainDelegate(IntPtr hInstance, IntPtr hPrevInstance, [MarshalAs(UnmanagedType.LPStr)] string lpCmdLine, int nCmdShow);

        private bool IsDevelopmentEnvironment()
        {
            // Debugger attached?
            if (Debugger.IsAttached)
            {
                Debug.WriteLine("[DevCheck] Debugger is attached.");
                return true;
            }

            // Compiled in Debug mode? (Less reliable if Release builds are debugged)
#if DEBUG
            Debug.WriteLine("[DevCheck] Compiled in DEBUG mode.");
            // return true; // Let's rely more on path check for dev vs installed
#endif

            // Running from a path that doesn't look like Squirrel's install path?
            // Squirrel installs to %LocalAppData%\AppName\app-x.y.z\
            // Or %LocalAppData%\AppName for the top-level stubs
            string currentDir = AppContext.BaseDirectory ?? "";
            string parentDirName = Path.GetFileName(Path.GetDirectoryName(currentDir.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar)));
            string currentDirName = Path.GetFileName(currentDir.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar));

            // Check if we are inside an 'app-x.y.z' folder or if the parent folder is the SquirrelAppName
            bool isInAppFolder = currentDirName.StartsWith("app-");
            bool isInSquirrelRoot = !string.IsNullOrEmpty(parentDirName) && parentDirName.Equals(SquirrelAppName, StringComparison.OrdinalIgnoreCase);

            if (!isInAppFolder && !isInSquirrelRoot)
            {
                 Debug.WriteLine($"[DevCheck] Current directory '{currentDir}' doesn't appear to be a Squirrel install path (not in 'app-x.y.z' or '{SquirrelAppName}'). Assuming dev environment.");
                 return true;
            }
            else
            {
                 Debug.WriteLine($"[DevCheck] Current directory '{currentDir}' looks like a Squirrel install path. Assuming installed environment.");
                 return false;
            }
        }


        protected async Task<bool> UpdateCheck() // Returns true if startup should continue, false otherwise
        {
            // --- Determine if Update Check is Needed ---
            bool isDevMode = IsDevelopmentEnvironment();
            if (isDevMode)
            {
                Debug.WriteLine("[Squirrel] Skipping update check in development environment.");
                return true; // Development mode, OK to continue startup
            }

            // --- Squirrel App Hooks ---
            // Run these first thing.
            // Use UpdateManager(urlOrPath, appName) constructor for hooks if needed,
            // though an empty one often works if RootAppDirectory is found correctly.
            using (var mgr = new UpdateManager("", SquirrelAppName)) // Temp manager used for hooks
            {
                try
                {
                    // Define target executable names *outside* the lambdas
                    const string mainExeTargetName = "r1delta.exe";
                    const string dsExeTargetName = "r1delta_ds.exe";

                    SquirrelAwareApp.HandleEvents(
                        onInitialInstall: (v,t) => // 'v' is the version being installed
                        {
                            var updateOnlyFlag = false; // For initial install, always create
                            var locations = ShortcutLocation.StartMenu; // Explicitly Start Menu ONLY

                            Debug.WriteLine($"[Squirrel Install] Creating Start Menu shortcuts for {mainExeTargetName} and {dsExeTargetName}");

                            try
                            {
                                // Create shortcuts for the main executable (Start Menu)
                                // Squirrel will use the AssemblyTitle/Company from mainExeTargetName
                                mgr.CreateShortcutsForExecutable(mainExeTargetName, locations, updateOnlyFlag, null, null);
                                Debug.WriteLine($"[Squirrel Install] Created shortcut for {mainExeTargetName}");
                            }
                            catch (Exception ex)
                            {
                                Debug.WriteLine($"[Squirrel Install] FAILED to create shortcut for {mainExeTargetName}: {ex.Message}");
                            }

                            try
                            {
                                // Create shortcut for the dedicated server executable (Start Menu ONLY)
                                // Squirrel will use the AssemblyTitle/Company from dsExeTargetName
                                mgr.CreateShortcutsForExecutable(dsExeTargetName, locations, updateOnlyFlag, null, null);
                                Debug.WriteLine($"[Squirrel Install] Created shortcut for {dsExeTargetName}");
                            }
                            catch (Exception ex)
                            {
                                Debug.WriteLine($"[Squirrel Install] FAILED to create shortcut for {dsExeTargetName}: {ex.Message}");
                            }

                            // --- Optional: First Run Launch ---
                            // This might not be strictly necessary if the SquirrelAware attribute
                            // is correctly set on r1delta.exe, as Squirrel might launch it anyway.
                            // try
                            // {
                            //     // Get the path to the newly installed main executable
                            //     string appDir = mgr.RootAppDirectory; // Gets %LocalAppData%\R1Delta
                            //     string currentAppDir = Path.Combine(appDir, "app-" + v.Version.ToString());
                            //     string mainExePath = Path.Combine(currentAppDir, mainExeTargetName);

                            //     if (File.Exists(mainExePath))
                            //     {
                            //         Debug.WriteLine($"[Squirrel Install] Attempting to launch {mainExePath} after initial install.");
                            //         Process.Start(mainExePath);
                            //     }
                            //     else
                            //     {
                            //          Debug.WriteLine($"[Squirrel Install] Could not find {mainExePath} to launch after install.");
                            //     }
                            // }
                            // catch(Exception ex)
                            // {
                            //     Debug.WriteLine($"[Squirrel Install] Error trying to launch app after install: {ex.Message}");
                            // }
                        },
                        onAppUpdate: (v,t) => // 'v' is the new version being updated to
                        {
                            var updateOnlyFlag = true; // For updates, only modify existing shortcuts
                            var locations = ShortcutLocation.StartMenu; // Explicitly Start Menu ONLY

                            Debug.WriteLine($"[Squirrel Update] Updating Start Menu shortcuts for {mainExeTargetName} and {dsExeTargetName}");

                            try
                            {
                                // Update shortcuts for the main executable (Start Menu)
                                mgr.CreateShortcutsForExecutable(mainExeTargetName, locations, updateOnlyFlag, null, null);
                                Debug.WriteLine($"[Squirrel Update] Updated shortcut for {mainExeTargetName}");
                            }
                            catch (Exception ex)
                            {
                                Debug.WriteLine($"[Squirrel Update] FAILED to update shortcut for {mainExeTargetName}: {ex.Message}");
                            }

                            try
                            {
                                // Update shortcut for the dedicated server executable (Start Menu ONLY)
                                mgr.CreateShortcutsForExecutable(dsExeTargetName, locations, updateOnlyFlag, null, null);
                                Debug.WriteLine($"[Squirrel Update] Updated shortcut for {dsExeTargetName}");
                            }
                            catch (Exception ex)
                            {
                                Debug.WriteLine($"[Squirrel Update] FAILED to update shortcut for {dsExeTargetName}: {ex.Message}");
                            }
                        },
                        onAppUninstall: (v,t) => // 'v' is the version being uninstalled
                        {
                            var locations = ShortcutLocation.StartMenu; // Explicitly Start Menu ONLY

                            Debug.WriteLine($"[Squirrel Uninstall] Removing Start Menu shortcuts for {mainExeTargetName} and {dsExeTargetName}");
                            try
                            {
                                // Use the *target* exe names here too for consistency
                                mgr.RemoveShortcutsForExecutable(mainExeTargetName, locations);
                                Debug.WriteLine($"[Squirrel Uninstall] Removed shortcut for {mainExeTargetName}");
                            }
                            catch (Exception ex)
                            {
                                Debug.WriteLine($"[Squirrel Uninstall] Error removing shortcut for {mainExeTargetName}: {ex.Message}");
                            }
                            try
                            {
                                mgr.RemoveShortcutsForExecutable(dsExeTargetName, locations);
                                Debug.WriteLine($"[Squirrel Uninstall] Removed shortcut for {dsExeTargetName}");
                            }
                            catch (Exception ex)
                            {
                                Debug.WriteLine($"[Squirrel Uninstall] Error removing shortcut for {dsExeTargetName}: {ex.Message}");
                            }

                            // --- Optional Game Content Deletion ---
                            string gamePath = null;
                            try
                            {
                                // Get the path where the launcher *thinks* the game files are
                                gamePath = RegistryHelper.GetInstallPath(); // Assumes RegistryHelper exists in R1Delta namespace

                                if (!string.IsNullOrEmpty(gamePath) && Directory.Exists(gamePath))
                                {
                                    // Define the marker file path
                                    string markerFileName = Path.Combine("vpk", "client_mp_delta_common.bsp.pak000_000.vpk"); // Relative path, only exists in Delta installs (not vanilla)
                                    string fullMarkerPath = Path.Combine(gamePath, markerFileName);

                                    Debug.WriteLine($"[Squirrel Uninstall] Checking for downloaded content marker: {fullMarkerPath}");

                                    // Check if the marker file exists
                                    if (File.Exists(fullMarkerPath))
                                    {
                                        Debug.WriteLine($"[Squirrel Uninstall] Downloaded content marker found.");

                                        // Ask the user for confirmation
                                        var result = MessageBox.Show(
                                            $"R1Delta appears to have downloaded Titanfall game files to:\n\n{gamePath}\n\n" +
                                            $"Do you want to delete these downloaded game files?\n\n" +
                                            $"(This will NOT affect game installations managed by Steam/EA App.)",
                                            "Delete Downloaded Game Files?",
                                            MessageBoxButton.YesNo,
                                            MessageBoxImage.Question);

                                        if (result == MessageBoxResult.Yes)
                                        {
                                            Debug.WriteLine($"[Squirrel Uninstall] User confirmed deletion. Deleting files listed in TitanfallManager.s_fileList from {gamePath}...");
                                            int filesDeleted = 0;
                                            int errors = 0;
                                            List<string> errorDetails = new List<string>();

                                            // Iterate through the list of files downloaded by your manager
                                            // Assumes TitanfallManager and s_fileList exist in R1Delta namespace
                                            foreach (var fileInfo in TitanfallManager.s_fileList)
                                            {
                                                string fileToDelete = Path.Combine(gamePath, fileInfo.RelativePath);
                                                try
                                                {
                                                    if (File.Exists(fileToDelete))
                                                    {
                                                        File.Delete(fileToDelete);
                                                        filesDeleted++;
                                                        Debug.WriteLine($"  Deleted: {fileToDelete}");
                                                    }
                                                    else
                                                    {
                                                        Debug.WriteLine($"  Skipped (already missing): {fileToDelete}");
                                                    }
                                                }
                                                catch (IOException ioEx) // Catch specific IO errors
                                                {
                                                    errors++;
                                                    string errorMsg = $"  ERROR deleting {fileToDelete}: {ioEx.Message}";
                                                    Debug.WriteLine(errorMsg);
                                                    errorDetails.Add(errorMsg);
                                                }
                                                catch (UnauthorizedAccessException authEx) // Catch permissions errors
                                                {
                                                    errors++;
                                                    string errorMsg = $"  ERROR deleting {fileToDelete} (Access Denied): {authEx.Message}";
                                                    Debug.WriteLine(errorMsg);
                                                    errorDetails.Add(errorMsg);
                                                }
                                                catch (Exception ex) // Catch other unexpected errors
                                                {
                                                    errors++;
                                                    string errorMsg = $"  UNEXPECTED ERROR deleting {fileToDelete}: {ex.Message}";
                                                    Debug.WriteLine(errorMsg);
                                                    errorDetails.Add(errorMsg);
                                                }
                                            }

                                            Debug.WriteLine($"[Squirrel Uninstall] Deletion attempt complete. Files deleted: {filesDeleted}, Errors: {errors}");

                                            // Inform the user of the outcome
                                            string summaryMessage = $"Attempted to delete downloaded game files.\n\nFiles successfully deleted: {filesDeleted}\nErrors encountered: {errors}";
                                            if (errors > 0)
                                            {
                                                summaryMessage += "\n\nSome files could not be deleted (they might be in use or permissions may be required):\n" + string.Join("\n", errorDetails.Take(5)) + (errorDetails.Count > 5 ? "\n..." : "");
                                            }
                                            MessageBox.Show(summaryMessage, "Deletion Result", MessageBoxButton.OK, errors > 0 ? MessageBoxImage.Warning : MessageBoxImage.Information);
                                        }
                                        else
                                        {
                                            Debug.WriteLine($"[Squirrel Uninstall] User declined deletion.");
                                        }
                                    }
                                    else
                                    {
                                        Debug.WriteLine($"[Squirrel Uninstall] Downloaded content marker NOT found. Skipping deletion prompt.");
                                    }
                                }
                                else
                                {
                                    Debug.WriteLine($"[Squirrel Uninstall] Game path registry key not found, empty, or directory does not exist ('{gamePath}'). Cannot check for downloaded files.");
                                }
                            }
                            catch (Exception ex)
                            {
                                // Catch errors during the check/deletion process itself (e.g., reading registry failed)
                                Debug.WriteLine($"[Squirrel Uninstall] Error during optional game file deletion process: {ex.Message}");
                                MessageBox.Show($"An error occurred while attempting to check for or delete downloaded game files:\n{ex.Message}",
                                                "Uninstall Error", MessageBoxButton.OK, MessageBoxImage.Warning);
                            }
                        } // End of onAppUninstall lambda
                        // onAppObsoleted could also remove shortcuts if needed
                        // onAppObsoleted: v => {
                        //     var locations = ShortcutLocation.StartMenu;
                        //     mgr.RemoveShortcutsForExecutable(mainExeTargetName, locations);
                        //     mgr.RemoveShortcutsForExecutable(dsExeTargetName, locations);
                        // }
                    );
                }
                catch (Exception ex)
                {
                    Debug.WriteLine($"[Squirrel] Error handling Squirrel events: {ex.Message}");
                    // Log or warn, but likely continue
                }
            } // Dispose the temporary UpdateManager


            // --- Perform Update Check ---
            Debug.WriteLine("[Squirrel] Checking for updates...");
            try
            {
                // Use the GitHub URL associated with your *actual application*
                // Pass the SquirrelAppName to ensure it looks in the correct %LocalAppData% folder
                using (var updateManager = new UpdateManager(new GithubSource(GitHubRepoUrl, "", false), SquirrelAppName))
                {
                    var updateInfo = await updateManager.CheckForUpdate();

                    if (updateInfo.ReleasesToApply.Any())
                    {
                        Debug.WriteLine($"[Squirrel] Update available: {updateInfo.FutureReleaseEntry.Version}");

                        // Consider showing a progress window here
                        // var progressWindow = new UpdateProgressWindow(); // Example
                        // progressWindow.Show();
                        // Progress<int> progress = new Progress<int>(p => progressWindow.UpdateProgress(p));

                        await updateManager.DownloadReleases(updateInfo.ReleasesToApply/*, progress*/);
                        await updateManager.ApplyReleases(updateInfo/*, progress*/);

                        // progressWindow.Close();

                        Debug.WriteLine("[Squirrel] Update applied. Requesting restart.");
                        UpdateManager.RestartApp(); // Signal Squirrel to restart the *currently running* app (r1delta.exe)

                        return false; // Update applied, DO NOT continue current startup
                    }
                    else
                    {
                        Debug.WriteLine("[Squirrel] No updates found.");
                        return true; // No updates, OK to continue startup
                    }
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[Squirrel] Update check failed: {ex.Message}");
                // Avoid showing message box if it's a common network error maybe?
                ShowWarning($"Could not check for updates. You might be offline or GitHub is unreachable.\n\n" +
                            $"The application might be out of date.\n\nError: {ex.Message}",
                            "Update Check Failed");
                return true; // Update check failed, but allow continuing startup
            }
        }

        // --- Main Entry Point ---
        [STAThread] // Required for MessageBox and SetupWindow
        protected override async void OnStartup(StartupEventArgs e) // Changed to async void
        {
            // --- Run Update Check Asynchronously ---
            bool shouldContinueStartup = false; // Default to not continuing
            try
            {
                shouldContinueStartup = await UpdateCheck(); // Use await
            }
            catch (Exception ex)
            {
                // Catch potential exceptions from UpdateCheck itself if not caught inside
                Debug.WriteLine($"[Startup] Critical error during async UpdateCheck: {ex.Message}");
                ShowError($"A critical error occurred during the update check:\n{ex.Message}\n\nThe application cannot start.", "Startup Error");
                Shutdown(1); // Use Shutdown instead of Environment.Exit in WPF App
                return;
            }

            // --- Exit if Update Check Handled It ---
            if (!shouldContinueStartup)
            {
                // If UpdateCheck returned false, it likely called RestartApp or handled shutdown.
                // We might need to explicitly shut down if RestartApp doesn't guarantee it immediately.
                Debug.WriteLine("[Startup] UpdateCheck indicated shutdown/restart. Exiting OnStartup.");
                // Check if Shutdown has already been called or is pending
                if (Application.Current != null && Application.Current.Dispatcher != null && !Application.Current.Dispatcher.HasShutdownStarted)
                {
                     Shutdown(0); // Graceful shutdown
                }
                return; // Exit OnStartup cleanly
            }

            // --- Continue with the rest of the startup logic ---
            DarkNet.Instance.SetCurrentProcessTheme(Theme.Auto);

            string originalLauncherExeDir;
            try
            {
                originalLauncherExeDir = AppContext.BaseDirectory ?? Path.GetDirectoryName(Assembly.GetEntryAssembly()?.Location);
                if (string.IsNullOrEmpty(originalLauncherExeDir))
                {
                    throw new Exception("Could not determine launcher executable directory.");
                }
                originalLauncherExeDir = originalLauncherExeDir.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
                Debug.WriteLine($"[*] Original Launcher Directory: {originalLauncherExeDir}");
            }
            catch (Exception ex)
            {
                ShowError($"Fatal error getting launcher path: {ex.Message}");
                Shutdown(1); // Use Shutdown
                return; // Keep compiler happy
            }

            // Check if the "r1delta" subdirectory exists next to the launcher executable.
            string requiredR1DeltaPath = Path.Combine(originalLauncherExeDir, R1DELTA_SUBDIR);
            if (!Directory.Exists(requiredR1DeltaPath))
            {
                ShowError($"Essential directory '{R1DELTA_SUBDIR}' not found in:\n{originalLauncherExeDir}\n\n" +
                          $"This usually means the R1Delta archive was not extracted correctly.\n\n" +
                          $"Please ensure you extract *ALL* files and folders from the R1Delta zip file directly into a new, empty folder where this launcher ({Path.GetFileName(Assembly.GetEntryAssembly()?.Location ?? "launcher_ex.exe")}) is located.",
                          "R1Delta Installation Error");
                Shutdown(1); // Use Shutdown
                return; // Exit immediately
            }

            // --- Setup and Validation Logic ---
            string finalInstallPath = null;
            string finalLaunchArgs = null;
            bool finalShowSetupSetting = true; // Default to showing setup

            try
            {
                // 1. Check F4 key state
                bool f4Pressed = (GetAsyncKeyState(VK_F4) & 0x8000) != 0;
                if (f4Pressed) Debug.WriteLine("[*] F4 key detected during startup.");

                // 2. Read current settings from registry
                bool currentShowSetupSetting = RegistryHelper.GetShowSetupOnLaunch();
                string currentArgs = RegistryHelper.GetLaunchArguments();
                string currentInstallPath = RegistryHelper.GetInstallPath();

                // 3. Check if core VPK exists in the currently configured path
                bool coreVpkExists = false;
                string coreVpkFullPath = null;
                if (!string.IsNullOrEmpty(currentInstallPath))
                {
                    try
                    {
                        // Use the constant from TitanfallManager
                        // Assumes TitanfallManager and ValidationFileRelativePath exist in R1Delta namespace
                        coreVpkFullPath = Path.GetFullPath(Path.Combine(currentInstallPath, TitanfallManager.ValidationFileRelativePath));
                        coreVpkExists = File.Exists(coreVpkFullPath);
                    }
                    catch (Exception ex)
                    {
                        Debug.WriteLine($"[*] Error checking core VPK existence at '{currentInstallPath}': {ex.Message}");
                        coreVpkExists = false; // Treat errors as VPK not existing
                    }
                }
                Debug.WriteLine($"[*] Current Install Path: '{currentInstallPath ?? "Not Set"}', Core VPK Exists: {coreVpkExists} (at '{coreVpkFullPath ?? "N/A"}')");

                // 4. Determine if Setup window is needed
                bool needsSetup = f4Pressed || !coreVpkExists || currentShowSetupSetting;
                Debug.WriteLine($"[*] Needs Setup? {needsSetup} (F4: {f4Pressed}, VPK Exists: {coreVpkExists}, Show Setting: {currentShowSetupSetting})");

                // 5. Run Setup or Use Existing Settings
                if (needsSetup)
                {
                    Debug.WriteLine("[*] Launching Setup Window...");
                    SetupWindow setupWindow = null; // Assumes SetupWindow exists in R1Delta namespace
                    try
                    {
                        // Pass current settings to SetupWindow constructor
                        setupWindow = new SetupWindow(originalLauncherExeDir, currentShowSetupSetting, currentArgs);
                        DarkNet.Instance.SetWindowThemeWpf(setupWindow, Theme.Auto);

                        bool? dialogResult = setupWindow.ShowDialog();

                        if (dialogResult == true)
                        {
                            // Setup completed successfully (path validated, download done if needed)
                            finalInstallPath = setupWindow.SelectedPath;
                            finalShowSetupSetting = setupWindow.ShowSetupOnLaunch;
                            finalLaunchArgs = setupWindow.LaunchArguments;
                            Debug.WriteLine($"[*] Setup Window OK. Path: '{finalInstallPath}', ShowNextTime: {finalShowSetupSetting}, Args: '{finalLaunchArgs}'");

                            // Save the chosen settings back to the registry
                            RegistryHelper.SaveInstallPath(finalInstallPath); // Path saved by SetupWindow only if download happened, ensure it's saved here too.
                            RegistryHelper.SaveShowSetupOnLaunch(finalShowSetupSetting);
                            RegistryHelper.SaveLaunchArguments(finalLaunchArgs);

                            // Show F4 hint if user disabled setup window
                            if (!finalShowSetupSetting)
                            {
                                MessageBox.Show(
                                    "Setup will not be shown automatically on the next launch because the \"Do not show this window again\" box was checked.\n\n" +
                                    "Hold the F4 key while starting the launcher if you need to access setup options again (e.g., change path, arguments).",
                                    "Setup Hidden",
                                    MessageBoxButton.OK,
                                    MessageBoxImage.Information
                                );
                            }
                        }
                        else
                        {
                            // User cancelled the setup window
                            Debug.WriteLine("[*] Setup Window cancelled by user.");
                            Shutdown(0); // Exit gracefully using WPF's Shutdown
                            return;
                        }
                    }
                    finally
                    {
                        // If SetupWindow implements IDisposable (e.g., for IProgress), dispose it
                        (setupWindow as IDisposable)?.Dispose();
                    }
                }
                else
                {
                    // Setup not needed, use existing settings
                    Debug.WriteLine("[*] Skipping Setup Window based on settings and VPK check.");
                    finalInstallPath = currentInstallPath; // Already validated by coreVpkExists check
                    finalShowSetupSetting = currentShowSetupSetting; // Keep existing setting
                    finalLaunchArgs = currentArgs; // Keep existing args

                    // Sanity check: Ensure the path is still valid (it should be)
                    // Assumes TitanfallManager and ValidateGamePath exist in R1Delta namespace
                    if (!TitanfallManager.ValidateGamePath(finalInstallPath, originalLauncherExeDir))
                    {
                        ShowError($"The previously configured game path is no longer valid:\n{finalInstallPath}\n\nPlease restart the launcher. Setup will run again.");
                        // Optionally clear the invalid path from registry?
                        // RegistryHelper.SaveInstallPath("");
                        Shutdown(1); // Use Shutdown
                        return;
                    }
                }
            }
            catch (Exception ex)
            {
                ShowError($"An unexpected error occurred during the initial setup check: {ex.Message}\nThe application will now exit.");
                Shutdown(1); // Use Shutdown
                return;
            }


            // --- Proceed to Launch ---

            // 6. Set Current Directory
            try
            {
                Directory.SetCurrentDirectory(finalInstallPath);
                Debug.WriteLine($"[*] Current Directory set to: {Directory.GetCurrentDirectory()}");
            }
            catch (Exception ex)
            {
                ShowError($"Fatal Error: Could not change directory to the game path:\n{finalInstallPath}\n\nError: {ex.Message}");
                Shutdown(1); // Use Shutdown
                return;
            }

            // 7. Ensure VCPP Redistributables are installed
            VisualCppInstaller.EnsureVisualCppRedistributables(); // Assuming this class exists and works in R1Delta namespace

            // 8. Force High Performance GPU (unchanged logic)
            IntPtr hNvApi = LoadLibraryW("nvapi64.dll"); // Attempt to load nvapi
            LoadLibraryW("TextShaping.dll"); // Load TextShaping
            // Optional: FreeLibrary(hNvApi); // Usually not needed, OS handles ref counting

            // 9. Set ContentId environment variable (unchanged logic)
            Environment.SetEnvironmentVariable("ContentId", "1025161");

            // 10. Run audio installer if necessary. Uses Current Working Directory (now game dir).
            try
            {
                if (RunAudioInstallerIfNecessary() != 0) // No longer needs path argument
                {
                    // Error message shown inside RunAudioInstallerIfNecessary
                    Shutdown(1); // Use Shutdown
                    return;
                }
            }
            catch (Exception ex)
            {
                ShowError($"An error occurred during the audio setup check.\nThe game cannot continue and has to exit.\n\nError: {ex.Message}");
                Shutdown(1); // Use Shutdown
                return;
            }

            // 11. Prepend required R1Delta directories (relative to original launcher dir) to PATH
            if (!PrependPath(finalInstallPath, originalLauncherExeDir))
            {
                // Warning already shown in PrependPath, but continue execution
            }


            // 13. Load the actual game launcher DLL (from original launcher dir)
            IntPtr hLauncherModule = IntPtr.Zero;
            IntPtr pLauncherMain = IntPtr.Zero;
            IntPtr hCoreModule = IntPtr.Zero; // Keep track of core module
            string launcherDllPath = Path.Combine(originalLauncherExeDir, R1DELTA_SUBDIR, BIN_SUBDIR, LAUNCHER_DLL_NAME);
            string coreDllPath = Path.Combine(originalLauncherExeDir, R1DELTA_SUBDIR, BIN_DELTA_SUBDIR, "tier0.dll"); // Assuming tier0 is the core

            try
            {
                Debug.WriteLine($"[*] Loading {LAUNCHER_DLL_NAME} from {launcherDllPath}");
                // Use LOAD_WITH_ALTERED_SEARCH_PATH to ensure dependencies are found in the modified PATH
                hLauncherModule = LoadLibraryExW(launcherDllPath, IntPtr.Zero, LOAD_WITH_ALTERED_SEARCH_PATH);
                if (hLauncherModule == IntPtr.Zero)
                {
                    int errorCode = Marshal.GetLastWin32Error();
                    LibraryLoadError(errorCode, LAUNCHER_DLL_NAME, launcherDllPath, originalLauncherExeDir);
                    Shutdown(1); // Use Shutdown
                    return; // Keep compiler happy
                }

                // Load the core DLL to get the SetArgs function
                Debug.WriteLine($"[*] Loading core DLL from {coreDllPath}");
                hCoreModule = LoadLibraryExW(coreDllPath, IntPtr.Zero, LOAD_WITH_ALTERED_SEARCH_PATH); // Use altered search path here too
                if (hCoreModule == IntPtr.Zero)
                {
                    int errorCode = Marshal.GetLastWin32Error();
                    LibraryLoadError(errorCode, "tier0.dll", coreDllPath, originalLauncherExeDir); // Adjust libName if different
                    FreeLibrary(hLauncherModule); // Clean up already loaded library
                    Shutdown(1);
                    return;
                }

                IntPtr pSetArgs = GetProcAddress(hCoreModule, "SetR1DeltaLaunchArgs");
                if (pSetArgs == IntPtr.Zero)
                {
                    int errorCode = Marshal.GetLastWin32Error();
                    ShowError($"Failed to find the 'SetR1DeltaLaunchArgs' function in the core DLL ('{Path.GetFileName(coreDllPath)}').\nError code: {errorCode}\n\nThe game cannot continue and has to exit.");
                    FreeLibrary(hLauncherModule);
                    FreeLibrary(hCoreModule);
                    Shutdown(1);
                    return;
                }

                Debug.WriteLine($"[*] Calling SetR1DeltaLaunchArgs with: '{finalLaunchArgs ?? ""}'");
                var setArgsDelegate = (SetR1DeltaLaunchArgsDelegate)Marshal.GetDelegateForFunctionPointer(pSetArgs, typeof(SetR1DeltaLaunchArgsDelegate));
                setArgsDelegate(finalLaunchArgs ?? ""); // Pass the arguments
                Debug.WriteLine($"[*] Successfully called SetR1DeltaLaunchArgs.");

                // Now get LauncherMain from the launcher DLL
                Debug.WriteLine("[*] Getting LauncherMain address");
                pLauncherMain = GetProcAddress(hLauncherModule, "LauncherMain");
                if (pLauncherMain == IntPtr.Zero)
                {
                    int errorCode = Marshal.GetLastWin32Error();
                    ShowError($"Failed to find the 'LauncherMain' function in '{LAUNCHER_DLL_NAME}'.\nError code: {errorCode}\n\nThe game cannot continue and has to exit.");
                    FreeLibrary(hLauncherModule);
                    FreeLibrary(hCoreModule);
                    Shutdown(1);
                    return;
                }
            }
            catch (Exception ex)
            {
                ShowError($"An unexpected error occurred while loading DLLs or functions.\nError: {ex.Message}\n\nThe game cannot continue and has to exit.");
                if (hLauncherModule != IntPtr.Zero) FreeLibrary(hLauncherModule);
                if (hCoreModule != IntPtr.Zero) FreeLibrary(hCoreModule);
                Shutdown(1);
                return;
            }
            // Do not free hCoreModule here, the game might depend on it.

            // 14. Apply Process Mitigation Policies
            SetMitigationPolicies();

            // 15. Prepare arguments and call LauncherMain
            try
            {
                Debug.WriteLine("[*] Preparing arguments for LauncherMain...");
                var launcherMain = (LauncherMainDelegate)Marshal.GetDelegateForFunctionPointer(pLauncherMain, typeof(LauncherMainDelegate));

                // Call LauncherMain. Arguments are now passed via SetR1DeltaLaunchArgs.
                int result = launcherMain(
                        IntPtr.Zero, // hInstance - Not readily available/needed
                        IntPtr.Zero, // hPrevInstance - Always zero in modern Windows
                        "",          // lpCmdLine - Pass empty string, args handled internally
                        1            // nCmdShow - SW_SHOWNORMAL
                    );

                // If LauncherMain returns, the game process likely exited or the C++ code returned control.
                Debug.WriteLine($"[*] LauncherMain returned with code: {result}. Shutting down launcher.");
                // Don't FreeLibrary(hLauncherModule/hCoreModule) here because the game process relies on them until it fully exits.
                // The OS will clean them up when the process terminates.
                Shutdown(result); // Exit with the code from LauncherMain
            }
            catch (Exception ex)
            {
                ShowError($"An error occurred while executing 'LauncherMain'.\nError: {ex.Message}\n\nThe game may not have started correctly.");
                // Don't FreeLibrary here either, the process might still be running somehow
                Shutdown(1); // Exit with an error code
            }
            // No need for Process.GetCurrentProcess().Kill() if using Shutdown() properly.
        }


        // --- Helper Functions ---

        private static void ShowError(string message, string title = "R1Delta Launcher Error")
        {
            // Use WPF MessageBox
            MessageBox.Show(message, title, MessageBoxButton.OK, MessageBoxImage.Error);
        }

        private static void ShowWarning(string message, string title = "R1Delta Launcher Warning")
        {
            // Use WPF MessageBox
            MessageBox.Show(message, title, MessageBoxButton.OK, MessageBoxImage.Warning);
        }

        // --- Modified Helper Function ---
        private static string GetFormattedErrorMessage(int errorCode, params string[] args)
        {
            IntPtr lpMsgBuf = IntPtr.Zero;
            IntPtr[] pArgs = null;
            IntPtr pArgsPtr = IntPtr.Zero; // Pointer to array of string pointers (use IntPtr for the array itself)

            try
            {
                uint dwFlags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM;
                uint dwMessageId = (uint)errorCode;
                uint dwLangId = 0; // Default language

                if (args != null && args.Length > 0)
                {
                    dwFlags |= FORMAT_MESSAGE_ARGUMENT_ARRAY;
                    pArgs = new IntPtr[args.Length];
                    // Allocate memory for each string and store pointers
                    for (int i = 0; i < args.Length; i++)
                    {
                        // Use StringToHGlobalUni for Unicode strings (%1 expects LPWSTR usually)
                        pArgs[i] = Marshal.StringToHGlobalUni(args[i] ?? ""); // Handle null args gracefully
                    }
                    // Allocate unmanaged memory for the array of pointers and copy the pointers
                    int sizeOfPtr = Marshal.SizeOf<IntPtr>();
                    pArgsPtr = Marshal.AllocHGlobal(args.Length * sizeOfPtr);
                    for(int i = 0; i < args.Length; i++)
                    {
                        Marshal.WriteIntPtr(pArgsPtr, i * sizeOfPtr, pArgs[i]);
                    }
                }
                else
                {
                    // If no args, tell FormatMessage to ignore inserts
                    dwFlags |= FORMAT_MESSAGE_IGNORE_INSERTS;
                }


                // Call FormatMessage
                uint charCount = FormatMessage(
                    dwFlags,
                    IntPtr.Zero, // Source (null for system errors)
                    dwMessageId,
                    dwLangId,    // Default language
                    out lpMsgBuf,// Output buffer allocated by system
                    0,           // Minimum size (0 lets system decide)
                    pArgsPtr == IntPtr.Zero ? null : new IntPtr[] { pArgsPtr }); // Pass the pointer to the array if it exists

                if (charCount == 0)
                {
                    // FormatMessage failed, fall back to Win32Exception
                    int formatMessageError = Marshal.GetLastWin32Error();
                    Debug.WriteLine($"FormatMessage failed with error: {formatMessageError}"); // Debug info
                    return new Win32Exception(errorCode).Message; // Fallback
                }

                // Convert the buffer to a C# string
                string message = Marshal.PtrToStringUni(lpMsgBuf).Trim(); // Trim trailing newline/whitespace

                return message;
            }
            finally
            {
                // Free the buffer allocated by FormatMessage
                if (lpMsgBuf != IntPtr.Zero)
                {
                    LocalFree(lpMsgBuf);
                }
                // Free the memory allocated for the argument strings
                if (pArgs != null)
                {
                    for (int i = 0; i < pArgs.Length; i++)
                    {
                        if (pArgs[i] != IntPtr.Zero)
                        {
                            Marshal.FreeHGlobal(pArgs[i]);
                        }
                    }
                }
                // Free the memory allocated for the array of pointers
                if (pArgsPtr != IntPtr.Zero)
                {
                    Marshal.FreeHGlobal(pArgsPtr);
                }
            }
        }


        // --- Updated LibraryLoadError ---
        // Added originalLauncherExeDir for context
        private static void LibraryLoadError(int errorCode, string libName, string location, string originalLauncherExeDir)
        {
            // Get the formatted error message using the helper
            string errorMessage = GetFormattedErrorMessage(errorCode, location);

            StringBuilder text = new StringBuilder();

            text.AppendFormat("Failed to load the library '{0}'\nLocation: \"{1}\"\nError Code: {2}\n\nSystem Message: {3}\n\n", libName, location, errorCode, errorMessage);
            text.Append("Make sure you followed the R1Delta installation instructions carefully before reaching out for help.");

            bool fileExists = false;
            try { fileExists = File.Exists(location); } catch { /* Ignore errors checking existence */ }

            string gameExePathInLauncherDir = Path.Combine(originalLauncherExeDir, "Titanfall.exe"); // Check for accidental placement
            bool gameExeMisplaced = false;
            try { gameExeMisplaced = File.Exists(gameExePathInLauncherDir); } catch { /* Ignore */ }


            if (errorCode == 126) // ERROR_MOD_NOT_FOUND
            {
                text.AppendFormat("\n\nThis error means Windows found the file '{0}' but could not load one of its *dependencies*.", libName);
                text.Append("\n\nCommon Causes & Solutions:");
                text.Append("\n1. Missing R1Delta Files: Ensure *all* files and folders from the R1Delta zip were extracted correctly into the launcher's directory (especially the 'r1delta\\bin' and 'r1delta\\bin_delta' subfolders). Re-extracting might help.");
                text.Append("\n2. Missing Visual C++ Redistributable: Install the latest Microsoft Visual C++ Redistributable (usually x64 version for 2015-2022). Link: https://aka.ms/vs/17/release/vc_redist.x64.exe");
                text.Append("\n3. Corrupted Files: Antivirus might have interfered, or the download was incomplete. Try disabling antivirus temporarily and re-extracting R1Delta.");
                text.Append("\n4. Incorrect PATH: Although the launcher tries to set the PATH, external factors could interfere. This is less likely if other parts loaded.");
            }
            else if (errorCode == 127) // ERROR_PROC_NOT_FOUND
            {
                 text.AppendFormat("\n\nThis error means a required function was not found within '{0}' or one of its dependencies.", libName);
                 text.Append("\n\nThis usually indicates a version mismatch (e.g., an older R1Delta file mixed with newer ones) or corrupted files.");
                 text.Append("\n\nSolution: Delete the 'r1delta' folder completely and re-extract the latest version of R1Delta.");
            }
            else if (errorCode == 193) // ERROR_BAD_EXE_FORMAT
            {
                 text.AppendFormat("\n\nThis error means '{0}' is not a valid Windows application or DLL, or it's the wrong architecture (e.g., trying to load a 32-bit DLL in a 64-bit process).", libName);
                 text.Append("\n\nThis strongly suggests file corruption or incorrect download/extraction.");
                 text.Append("\n\nSolution: Delete the 'r1delta' folder and re-extract the correct (usually 64-bit) version of R1Delta.");
            }
            else if (gameExeMisplaced) // Titanfall.exe found *next to the launcher*
            {
                string launcherDirName = new DirectoryInfo(originalLauncherExeDir).Name;
                text.AppendFormat("\n\nIMPORTANT: We detected 'Titanfall.exe' in the *same directory* as the R1Delta launcher ('{0}').", originalLauncherExeDir);
                text.Append("\n\nThis is incorrect. R1Delta should be placed in its *own* folder, separate from the Titanfall game files.");
                text.Append("\nPlease move the R1Delta launcher and its associated files (like the 'r1delta' folder) to a new, empty directory, then run the setup again.");
            }
            else if (!fileExists) // The specific DLL we tried to load is missing
            {
                text.AppendFormat("\n\nThe required R1Delta file '{0}' is missing from '{1}'.", libName, Path.GetDirectoryName(location));
                text.Append("\nDid you unpack *all* R1Delta files into the launcher's directory?");
            }

            ShowError(text.ToString());
        }


        /// <summary>
        /// Prepends R1Delta's specific bin directories (relative to the launcher's install location)
        /// and the game's retail bin directories to the PATH.
        /// </summary>
        /// <param name="gameInstallPath">The validated path to the Titanfall game installation (where CWD is set).</param>
        /// <param name="launcherExeDir">The directory where the R1Delta launcher executable resides.</param>
        /// <returns>True on success, false on failure.</returns>
        private static bool PrependPath(string gameInstallPath, string launcherExeDir)
        {
            try
            {
                string currentPath = Environment.GetEnvironmentVariable("PATH") ?? "";
                // Ensure paths exist before adding them to avoid cluttering PATH with invalid entries
                string r1DeltaBinDelta = Path.Combine(launcherExeDir, R1DELTA_SUBDIR, BIN_DELTA_SUBDIR);
                string r1DeltaBin = Path.Combine(launcherExeDir, R1DELTA_SUBDIR, BIN_SUBDIR);
                string retailBin = Path.Combine(gameInstallPath, RETAIL_BIN_SUBDIR); // Game's retail bin
                string retailGameDirBin = Path.Combine(gameInstallPath, "r1", RETAIL_BIN_SUBDIR); // Game's r1 retail bin (less common?)

                List<string> pathsToAdd = new List<string>();
                if (Directory.Exists(r1DeltaBinDelta)) pathsToAdd.Add(r1DeltaBinDelta);
                if (Directory.Exists(r1DeltaBin)) pathsToAdd.Add(r1DeltaBin);
                if (Directory.Exists(retailBin)) pathsToAdd.Add(retailBin);
                if (Directory.Exists(retailGameDirBin)) pathsToAdd.Add(retailGameDirBin);

                // Construct the new PATH: r1delta_delta -> r1delta -> game_retail -> game_r1_retail -> original PATH
                // Order matters for DLL loading precedence. R1Delta overrides first.
                string newPath = string.Join(Path.PathSeparator.ToString(), pathsToAdd.Concat(new[] { currentPath }));

                Environment.SetEnvironmentVariable("PATH", newPath);
                Debug.WriteLine($"[*] Updated PATH:");
                foreach(var p in pathsToAdd) { Debug.WriteLine($"    Prepended: {p}"); }
                // Debug.WriteLine($"    Original: {currentPath}"); // Can be very long
                return true;
            }
            catch (ArgumentException argEx) // Path.Combine might throw this
            {
                 ShowWarning($"Warning: Could not construct PATH elements. Check for invalid characters in paths.\nGame Path: {gameInstallPath}\nLauncher Path: {launcherExeDir}\nError: {argEx.Message}");
                 return false;
            }
            catch (System.Security.SecurityException secEx)
            {
                 ShowWarning($"Warning: Insufficient permissions to modify the PATH environment variable.\nError: {secEx.Message}");
                 return false;
            }
            catch (Exception ex)
            {
                ShowWarning($"Warning: An unexpected error occurred while modifying the PATH environment variable.\nError: {ex.Message}");
                return false;
            }
        }

        // --- IsAnyIMEInstalled (Unchanged) ---
        private static bool IsAnyIMEInstalled()
        {
            try
            {
                int count = GetKeyboardLayoutList(0, null);
                if (count == 0) return false;

                IntPtr[] list = new IntPtr[count];
                int result = GetKeyboardLayoutList(count, list);
                if (result == 0) return false; // Error getting list

                for (int i = 0; i < count; i++)
                {
                    if (ImmIsIME(list[i]))
                    {
                        Debug.WriteLine("IME detected.");
                        return true;
                    }
                }
                Debug.WriteLine("No IME detected.");
                return false;
            }
            catch (DllNotFoundException)
            {
                // imm32.dll might not be present on some very minimal Windows installs (unlikely)
                Debug.WriteLine("Warning: Could not check for IME installation (imm32.dll not found?). Assuming IME present.");
                return true; // Assume IME might be installed to be safe regarding mitigations
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Warning: Exception checking for IME installation: {ex.Message}. Assuming IME present.");
                return true; // Assume IME might be installed
            }
        }

        // --- SetMitigationPolicies (Unchanged - applies to current process) ---
        private static void SetMitigationPolicies()
        {
            IntPtr hKernel32 = GetModuleHandleW("kernel32.dll");
            if (hKernel32 == IntPtr.Zero) return;

            IntPtr pSetPolicy = GetProcAddress(hKernel32, "SetProcessMitigationPolicy");
            IntPtr pGetPolicy = GetProcAddress(hKernel32, "GetProcessMitigationPolicy");

            if (pSetPolicy == IntPtr.Zero)
            {
                Debug.WriteLine("SetProcessMitigationPolicy not found. Skipping mitigation setup (OS likely older than Windows 8).");
                return; // Not supported on this OS version (Win7 or older)
            }

            // Dynamically load GetProcessMitigationPolicy if needed
            if (pGetPolicy != IntPtr.Zero)
            {
                try
                {
                    pGetProcessMitigationPolicy = (GetProcessMitigationPolicyDelegate)Marshal.GetDelegateForFunctionPointer(pGetPolicy, typeof(GetProcessMitigationPolicyDelegate));
                }
                catch (Exception ex)
                {
                     Debug.WriteLine($"Warning: Could not get delegate for GetProcessMitigationPolicy: {ex.Message}");
                     pGetProcessMitigationPolicy = null;
                }
            }


            // Helper to call SetProcessMitigationPolicy and show non-fatal errors
            Action<PROCESS_MITIGATION_POLICY, object> SetPolicyEnsureOK = (policy, policyStruct) =>
            {
                GCHandle handle = GCHandle.Alloc(policyStruct, GCHandleType.Pinned);
                try
                {
                    IntPtr buffer = handle.AddrOfPinnedObject();
                    nuint size = (nuint)Marshal.SizeOf(policyStruct);
                    if (!SetProcessMitigationPolicy(policy, buffer, size))
                    {
                        int error = Marshal.GetLastWin32Error();
                        string policyName = MitigationPolicyNames.ContainsKey(policy) ? MitigationPolicyNames[policy] : policy.ToString();

                        // Ignore errors that are common/expected or less critical
                        if (policy == PROCESS_MITIGATION_POLICY.ProcessUserShadowStackPolicy && (error == 87 /*INVALID_PARAMETER*/ || error == 50 /*NOT_SUPPORTED*/))
                        {
                            Debug.WriteLine($"Ignoring SetProcessMitigationPolicy error {error} for {policyName} as CPU/OS may lack CET support.");
                            return;
                        }
                        if (error == 5) // ERROR_ACCESS_DENIED
                        {
                            Debug.WriteLine($"Ignoring SetProcessMitigationPolicy error {error} (Access Denied) for policy {policyName}. It might be already enforced by system/debugger/group policy.");
                            return;
                        }
                        if (error == 87 && policy != PROCESS_MITIGATION_POLICY.ProcessUserShadowStackPolicy) // ERROR_INVALID_PARAMETER for other policies might indicate OS incompatibility
                        {
                            Debug.WriteLine($"Ignoring SetProcessMitigationPolicy error {error} (Invalid Parameter) for policy {policyName}. OS might not support this specific policy/flag combination.");
                            return;
                        }


                        // Show warning for other errors
                        ShowWarning($"Failed to set mitigation policy: {policyName}\nError: {error} ({new Win32Exception(error).Message})\n\nThis is usually a non-fatal error, the game might still work.",
                                        "Mitigation Policy Warning");
                    }
                    else
                    {
                        Debug.WriteLine($"Successfully applied mitigation policy: {policy}");
                    }
                }
                catch (Exception ex) // Catch unexpected errors during the P/Invoke or marshalling
                {
                     string policyName = MitigationPolicyNames.ContainsKey(policy) ? MitigationPolicyNames[policy] : policy.ToString();
                     Debug.WriteLine($"Unexpected exception setting mitigation policy {policyName}: {ex.Message}");
                     ShowWarning($"An unexpected error occurred while trying to set mitigation policy: {policyName}\nError: {ex.Message}", "Mitigation Policy Error");
                }
                finally
                {
                    if (handle.IsAllocated) handle.Free();
                }
            };

            // --- Apply ASLR Policy ---
            try
            {
                var aslrPolicy = new PROCESS_MITIGATION_ASLR_POLICY();
                // Getting current policy is optional but good practice if available
                if (pGetProcessMitigationPolicy != null)
                {
                    GCHandle handle = GCHandle.Alloc(aslrPolicy, GCHandleType.Pinned);
                    try { pGetProcessMitigationPolicy(GetCurrentProcess(), PROCESS_MITIGATION_POLICY.ProcessASLRPolicy, handle.AddrOfPinnedObject(), (nuint)Marshal.SizeOf(aslrPolicy)); }
                    catch (Exception getEx) { Debug.WriteLine($"Warning: Failed to get current ASLR policy: {getEx.Message}"); }
                    finally { if (handle.IsAllocated) handle.Free(); }
                }
                // Set desired flags: BottomUp, ForceRelocate, HighEntropy, DisallowStripped
                aslrPolicy.SetFlags(true, true, true, true);
                SetPolicyEnsureOK(PROCESS_MITIGATION_POLICY.ProcessASLRPolicy, aslrPolicy);
            }
            catch (Exception ex) { Debug.WriteLine($"Error applying ASLR policy: {ex.Message}"); }


            // --- Apply Extension Point Disable Policy ---
            // This can break IME input, so check first
            if (!IsAnyIMEInstalled())
            {
                try
                {
                    var extPolicy = new PROCESS_MITIGATION_EXTENSION_POINT_DISABLE_POLICY();
                    if (pGetProcessMitigationPolicy != null)
                    {
                        GCHandle handle = GCHandle.Alloc(extPolicy, GCHandleType.Pinned);
                        try { pGetProcessMitigationPolicy(GetCurrentProcess(), PROCESS_MITIGATION_POLICY.ProcessExtensionPointDisablePolicy, handle.AddrOfPinnedObject(), (nuint)Marshal.SizeOf(extPolicy)); }
                        catch (Exception getEx) { Debug.WriteLine($"Warning: Failed to get current Extension Point policy: {getEx.Message}"); }
                        finally { if (handle.IsAllocated) handle.Free(); }
                    }
                    // Set desired flags: DisableExtensionPoints
                    extPolicy.SetFlags(true);
                    SetPolicyEnsureOK(PROCESS_MITIGATION_POLICY.ProcessExtensionPointDisablePolicy, extPolicy);
                }
                catch (Exception ex) { Debug.WriteLine($"Error applying Extension Point policy: {ex.Message}"); }
            }
            else
            {
                Debug.WriteLine("Skipping ExtensionPointDisablePolicy because an IME is installed.");
            }

            // --- Apply Image Load Policy ---
            try
            {
                var imgPolicy = new PROCESS_MITIGATION_IMAGE_LOAD_POLICY();
                if (pGetProcessMitigationPolicy != null)
                {
                    GCHandle handle = GCHandle.Alloc(imgPolicy, GCHandleType.Pinned);
                    try { pGetProcessMitigationPolicy(GetCurrentProcess(), PROCESS_MITIGATION_POLICY.ProcessImageLoadPolicy, handle.AddrOfPinnedObject(), (nuint)Marshal.SizeOf(imgPolicy)); }
                    catch (Exception getEx) { Debug.WriteLine($"Warning: Failed to get current Image Load policy: {getEx.Message}"); }
                    finally { if (handle.IsAllocated) handle.Free(); }
                }
                // Set desired flags: NoRemoteImages, PreferSystem32
                // NoLowMandatoryLabelImages can sometimes cause issues with certain overlays/injectors, keep false unless needed.
                imgPolicy.SetFlags(noRemote: true, noLowMandatory: false, preferSystem32: true);
                SetPolicyEnsureOK(PROCESS_MITIGATION_POLICY.ProcessImageLoadPolicy, imgPolicy);
            }
            catch (Exception ex) { Debug.WriteLine($"Error applying Image Load policy: {ex.Message}"); }


            // --- Apply User Shadow Stack Policy (CET) ---
            // Only attempt if API exists (checked via pSetPolicy != IntPtr.Zero earlier)
            // SetPolicyEnsureOK handles errors like lack of CPU/OS support internally now.
            try
            {
                var ussPolicy = new PROCESS_MITIGATION_USER_SHADOW_STACK_POLICY();
                bool gotPolicy = false;
                if (pGetProcessMitigationPolicy != null)
                {
                    GCHandle handle = GCHandle.Alloc(ussPolicy, GCHandleType.Pinned);
                    try
                    {
                        // Size must be correct for the OS version
                        nuint policySize = (nuint)Marshal.SizeOf(ussPolicy);
                        // Adjust size for older OS versions if necessary, though usually PROCESS_MITIGATION_USER_SHADOW_STACK_POLICY is only on newer OSes
                        // For simplicity, we assume the struct size is correct for OSes that support the policy.
                        gotPolicy = pGetProcessMitigationPolicy(GetCurrentProcess(), PROCESS_MITIGATION_POLICY.ProcessUserShadowStackPolicy, handle.AddrOfPinnedObject(), policySize);
                    }
                    catch (EntryPointNotFoundException) { /* GetPolicy might not exist */ }
                    catch (Exception getEx) { Debug.WriteLine($"Warning: Error getting UserShadowStackPolicy: {getEx.Message}"); }
                    finally { if (handle.IsAllocated) handle.Free(); }
                }

                // Attempt to enable basic CET protection.
                // Strict mode and other advanced flags are often problematic unless the whole toolchain supports them.
                // Let's just try enabling the base feature. SetPolicyEnsureOK will ignore errors if not supported.
                Debug.WriteLine("Attempting to enable User Shadow Stack (CET).");
                ussPolicy.SetFlags(
                    enable: true, // Enable base protection
                    audit: false, // Disable audit mode
                    setContext: true, // Enable SetContextIpValidation (recommended)
                    auditSetContext: false,
                    strictMode: false, // Strict mode often breaks things
                    blockNonCet: false, // Block non-CET often breaks things
                    blockNonCetNonEhcont: false,
                    auditBlockNonCet: false,
                    auditBlockNonCetNonEhcont: false,
                    cetDynamicFix: false, // Usually not needed unless specific issues arise
                    strictAlign: false // Usually not needed unless specific issues arise
                );
                SetPolicyEnsureOK(PROCESS_MITIGATION_POLICY.ProcessUserShadowStackPolicy, ussPolicy);

            }
            catch (Exception ex) { Debug.WriteLine($"Error applying User Shadow Stack policy: {ex.Message}"); }
        }


        // --- Audio Installer Logic ---

        /// <summary>
        /// Scans a file for the "OggS" magic bytes. Non-SIMD version.
        /// (Unchanged)
        /// </summary>
        private static bool ScanFileForOggS(string filepath)
        {
            const int bufferSize = 64 * 1024; // 64 KB buffer
            byte[] buffer = new byte[bufferSize];
            // "OggS" in ASCII bytes
            byte[] pattern = { 0x4F, 0x67, 0x67, 0x53 };
            int patternLength = pattern.Length;

            try
            {
                // Ensure file exists before trying to open
                if (!File.Exists(filepath))
                {
                    Debug.WriteLine($"Warning: File not found for OggS scan: {filepath}");
                    return false;
                }

                using (FileStream fs = new FileStream(filepath, FileMode.Open, FileAccess.Read, FileShare.Read))
                {
                    int bytesRead;
                    int overlap = 0; // How many bytes from the previous buffer to prepend to the current one
                    long filePosition = 0; // Track position for better debugging

                    while ((bytesRead = fs.Read(buffer, overlap, bufferSize - overlap)) > 0)
                    {
                        int currentBufferSize = bytesRead + overlap;

                        // Scan the current buffer (including overlap from previous read)
                        for (int i = 0; i <= currentBufferSize - patternLength; i++)
                        {
                            bool match = true;
                            for (int j = 0; j < patternLength; j++)
                            {
                                if (buffer[i + j] != pattern[j])
                                {
                                    match = false;
                                    break;
                                }
                            }
                            if (match)
                            {
                                Debug.WriteLine($"Found 'OggS' pattern in {filepath} at approx offset {filePosition + i}");
                                return true; // Pattern found
                            }
                        }

                        // Prepare overlap for the next read (last patternLength - 1 bytes)
                        overlap = Math.Min(patternLength - 1, currentBufferSize);
                        if (overlap > 0)
                        {
                            // Copy the end of the buffer to the beginning for the next read
                            Buffer.BlockCopy(buffer, currentBufferSize - overlap, buffer, 0, overlap);
                        }
                        filePosition += bytesRead; // Update approximate position
                    }
                }
            }
            catch (FileNotFoundException) // Should be caught by File.Exists check, but keep for safety
            {
                Debug.WriteLine($"Warning: File not found during OggS scan (should have been checked): {filepath}");
                return false;
            }
            catch (IOException ex)
            {
                Debug.WriteLine($"Warning: IO error during OggS scan of {filepath}: {ex.Message}");
                return false; // Treat IO errors as pattern not found / cannot verify
            }
            catch (UnauthorizedAccessException ex)
            {
                 Debug.WriteLine($"Warning: Access denied during OggS scan of {filepath}: {ex.Message}");
                 return false; // Treat permission errors as pattern not found / cannot verify
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Warning: Unexpected error during OggS scan of {filepath}: {ex.Message}");
                return false; // Treat unexpected errors as not found
            }

            // If loop completes without finding the pattern
            Debug.WriteLine($"'OggS' pattern not found in {filepath}");
            return false;
        }

        /// <summary>
        /// Checks if audio installation is needed and runs the installer if required.
        /// Uses the Current Working Directory (which should be the game directory).
        /// Returns 0 on success (or already done), 1 on failure.
        /// </summary>
        private static int RunAudioInstallerIfNecessary() // Removed exePath parameter
        {
            // Paths are now relative to the Current Working Directory (game dir)
            string audioDonePath = Path.Combine(Directory.GetCurrentDirectory(), AUDIO_DONE_FILE);
            string camListPath = Path.Combine(Directory.GetCurrentDirectory(), CAM_LIST_FILE);
            string targetVpkPath = Path.Combine(Directory.GetCurrentDirectory(), TARGET_VPK_FILE);
            string installerPath = Path.Combine(Directory.GetCurrentDirectory(), AUDIO_INSTALLER_EXE);

            try
            {
                if (File.Exists(audioDonePath))
                {
                    Debug.WriteLine("Audio setup already marked as done.");
                    return 0; // Already done
                }

                if (!File.Exists(camListPath))
                {
                    Debug.WriteLine($"Required file '{CAM_LIST_FILE}' not found in CWD ({Directory.GetCurrentDirectory()}). Assuming Delta install or cannot check audio state. Skipping audio install.");
                    // Proceed without audio check/install for Delta VPKs or if base files are missing.
                    return 0; // Indicate success (nothing to do/check)
                }

                // Check header of cam_list.txt
                try
                {
                    using (StreamReader reader = new StreamReader(camListPath))
                    {
                        string firstLine = reader.ReadLine();
                        if (firstLine != "CAMLIST")
                        {
                            Debug.WriteLine($"Warning: Invalid header in {camListPath}. Expected 'CAMLIST', got '{firstLine}'. Cannot proceed with audio check.");
                            // Don't fail here, maybe the VPK scan is still useful? Or maybe fail? Let's fail to be safe.
                            ShowError($"Audio check file '{CAM_LIST_FILE}' has an invalid format. Cannot verify audio state.", "Audio Check Error");
                            return 1; // Fail as list might be corrupt
                        }
                    }
                }
                catch (Exception ex)
                {
                    Debug.WriteLine($"Error reading {camListPath}: {ex.Message}");
                    ShowError($"Could not read the audio check file '{CAM_LIST_FILE}'. Cannot verify audio state.\nError: {ex.Message}", "Audio Check Error");
                    return 1; // Failed to read list
                }

                // Scan the target VPK for "OggS"
                bool needsInstall = ScanFileForOggS(targetVpkPath);

                if (!needsInstall)
                {
                    // This case covers:
                    // 1. VPK doesn't exist (ScanFileForOggS returns false) - Nothing to fix.
                    // 2. VPK exists but doesn't contain "OggS" - Already fixed or never broken.
                    Debug.WriteLine($"Target VPK '{Path.GetFileName(targetVpkPath)}' does not contain 'OggS' or is missing. Audio setup not needed or already complete.");
                    // Create the done file to skip check next time, but only if cam_list was present.
                    try
                    {
                         // Ensure directory exists before writing
                         Directory.CreateDirectory(Path.GetDirectoryName(audioDonePath));
                         File.WriteAllText(audioDonePath, DateTime.UtcNow.ToString("o")); // Write timestamp
                         Debug.WriteLine($"Created audio done marker: {audioDonePath}");
                    }
                    catch (Exception ex)
                    {
                        Debug.WriteLine($"Warning: Could not create audio done file '{audioDonePath}': {ex.Message}");
                        // Non-fatal, just means check runs again next time.
                    }
                    return 0; // Success (not needed or already done)
                }

                // --- Need to run the installer ---
                Debug.WriteLine($"'OggS' found in '{Path.GetFileName(targetVpkPath)}'. Running audio installer...");

                if (!File.Exists(installerPath))
                {
                    ShowError($"Audio installer '{AUDIO_INSTALLER_EXE}' not found in game directory ({Directory.GetCurrentDirectory()}). Cannot fix audio automatically.\nPlease ensure all base game files are present, or verify/repair game files via Steam/EA App if applicable.", "Audio Installer Missing");
                    return 1; // Installer missing
                }

                ProcessStartInfo psi = new ProcessStartInfo
                {
                    FileName = installerPath,
                    Arguments = $"\"{camListPath}\"", // Quote path in case of spaces
                    UseShellExecute = false, // Must be false to redirect IO or wait correctly without shell
                    CreateNoWindow = true,   // Hide console window
                    WorkingDirectory = Directory.GetCurrentDirectory() // Explicitly set CWD for the process
                };

                try
                {
                    using (Process process = Process.Start(psi))
                    {
                        if (process == null)
                        {
                            throw new Exception("Failed to start audio installer process (Process.Start returned null).");
                        }

                        // Wait for the process to exit. Add a reasonable timeout.
                        bool exited = process.WaitForExit(120000); // 120 seconds timeout

                        if (!exited)
                        {
                             ShowError($"Audio installer process did not exit within the timeout period (2 minutes). It might be stuck. Please check Task Manager.", "Audio Installation Timeout");
                             try { process.Kill(); } catch { /* Ignore errors killing */ }
                             return 1; // Timed out
                        }

                        if (process.ExitCode != 0)
                        {
                            ShowError($"Audio installer failed with exit code {process.ExitCode}. Please check for errors or run it manually if necessary.\nExecutable: {installerPath}\nArguments: \"{camListPath}\"", "Audio Installation Failed");
                            return 1; // Installer failed
                        }

                        Debug.WriteLine("Audio installer finished successfully.");

                        // Re-scan VPK to confirm fix
                        if (ScanFileForOggS(targetVpkPath))
                        {
                            Debug.WriteLine($"Warning: Target VPK '{Path.GetFileName(targetVpkPath)}' still contains 'OggS' after installer ran. The fix might not have worked or the file is locked.");
                            ShowWarning($"Audio installer finished, but the target file still seems incorrect. Audio issues might persist. Try restarting the launcher.", "Audio Setup Warning");
                            // Don't create done file if re-scan fails
                        }
                        else
                        {
                            Debug.WriteLine("Target VPK verified clean after install.");
                            // Create the done file
                            try
                            {
                                Directory.CreateDirectory(Path.GetDirectoryName(audioDonePath));
                                File.WriteAllText(audioDonePath, DateTime.UtcNow.ToString("o"));
                                Debug.WriteLine($"Created audio done marker: {audioDonePath}");
                            }
                            catch (Exception ex)
                            {
                                Debug.WriteLine($"Warning: Could not create audio done file '{audioDonePath}' after successful install: {ex.Message}");
                            }
                        }

                        return 0; // Success (installer ran, even if verification failed, we tried)
                    }
                }
                catch (Win32Exception w32ex) // Catch specific errors from Process.Start
                {
                     ShowError($"An error occurred starting the audio installer (Win32 Error {w32ex.NativeErrorCode}):\n{w32ex.Message}\n\nCheck file permissions and if the installer exists.", "Audio Installation Error");
                     return 1;
                }
                catch (Exception ex)
                {
                    ShowError($"An unexpected error occurred while running the audio installer:\n{ex.Message}", "Audio Installation Error");
                    return 1; // Error running installer
                }
            }
            catch (Exception outerEx) // Catch errors before even starting checks (e.g., CWD access denied)
            {
                 ShowError($"A critical error occurred during audio setup initialization:\n{outerEx.Message}", "Audio Setup Error");
                 return 1;
            }
        }
    }
}