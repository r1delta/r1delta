﻿using Dark.Net;
using R1Delta;
using Squirrel;
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
using Squirrel;

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
        // Delegate for the exported C++ function (Unchanged)
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void SetR1DeltaLaunchArgsDelegate([MarshalAs(UnmanagedType.LPStr)] string args);


        // --- Mitigation Policy Structures & Enum ---
        // (Keep Mitigation Policy Enums and Structs as they were - unchanged)
        // ... PROCESS_MITIGATION_POLICY enum ...
        // ... MitigationFlags enum ...
        // ... PROCESS_MITIGATION_ASLR_POLICY struct ...
        // ... PROCESS_MITIGATION_EXTENSION_POINT_DISABLE_POLICY struct ...
        // ... PROCESS_MITIGATION_IMAGE_LOAD_POLICY struct ...
        // ... PROCESS_MITIGATION_USER_SHADOW_STACK_POLICY struct ...
        // ... MitigationPolicyNames dictionary ...
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
            return true;
#endif

            // Running from a path that doesn't look like Squirrel's install path?
            // Squirrel installs to %LocalAppData%\AppName\app-x.y.z\
            string currentDir = AppContext.BaseDirectory ?? "";
            if (!currentDir.Contains(Path.DirectorySeparatorChar + "app-"))
            {
                Debug.WriteLine($"[DevCheck] Current directory '{currentDir}' does not contain '\\app-'. Assuming dev environment.");
                return true;
            }

            return false;
        }
        private const string GitHubRepoUrl = "https://github.com/r1delta/r1delta";
        private const string SquirrelAppName = "R1Delta";

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
            using (var mgr = new UpdateManager("")) // Temp manager used for hooks
            {
                try
                {
                    SquirrelAwareApp.HandleEvents(
                        onInitialInstall: v => // 'v' is the version being installed
                        {
                            // Determine the names of the executables
                            var mainExeName = Path.GetFileName(Assembly.GetEntryAssembly().Location); // e.g., r1delta.exe
                            var dsExeName = "r1delta_ds.exe";
                            var updateOnlyFlag = false; // For initial install, always create

                            Debug.WriteLine($"[Squirrel Install] Creating shortcuts for {mainExeName} and {dsExeName}");

                            // Create shortcuts for the main executable (Start Menu)
                            mgr.CreateShortcutsForExecutable(mainExeName, ShortcutLocation.StartMenu, updateOnlyFlag, null, null);

                            // Create shortcut for the dedicated server executable (Start Menu ONLY)
                            mgr.CreateShortcutsForExecutable(dsExeName, ShortcutLocation.StartMenu, updateOnlyFlag, null, null);
                        },
                        onAppUpdate: v => // 'v' is the new version being updated to
                        {
                            // Determine the names of the executables
                            var mainExeName = Path.GetFileName(Assembly.GetEntryAssembly().Location);
                            var dsExeName = "r1delta_ds.exe";
                            var updateOnlyFlag = true; // For updates, only modify existing shortcuts

                            Debug.WriteLine($"[Squirrel Update] Updating shortcuts for {mainExeName} and {dsExeName}");

                            // Update shortcuts for the main executable (Start Menu)
                            mgr.CreateShortcutsForExecutable(mainExeName, ShortcutLocation.StartMenu, updateOnlyFlag, null, null);

                            // Update shortcut for the dedicated server executable (Start Menu ONLY)
                            mgr.CreateShortcutsForExecutable(dsExeName, ShortcutLocation.StartMenu, updateOnlyFlag, null, null);
                        },
                        onAppUninstall: v => // 'v' is the version being uninstalled
                        {
                            // --- 1. Standard Shortcut Removal ---
                            var mainExeName = Path.GetFileName(Assembly.GetEntryAssembly().Location);
                            var dsExeName = "r1delta_ds.exe";
                            Debug.WriteLine($"[Squirrel Uninstall] Removing standard shortcuts for {mainExeName} and {dsExeName}");
                            try
                            {
                                mgr.RemoveShortcutsForExecutable(mainExeName, ShortcutLocation.StartMenu);
                                mgr.RemoveShortcutsForExecutable(dsExeName, ShortcutLocation.StartMenu);
                            }
                            catch (Exception ex)
                            {
                                Debug.WriteLine($"[Squirrel Uninstall] Error removing shortcuts: {ex.Message}");
                                // Log or warn, but continue uninstall
                            }

                            // --- 2. Optional Game Content Deletion ---
                            string gamePath = null;
                            try
                            {
                                // Get the path where the launcher *thinks* the game files are
                                gamePath = RegistryHelper.GetInstallPath();

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
                                            $"(This will NOT affect game installations managed by Steam/EA App unless you pointed R1Delta to that folder AND the marker file '{markerFileName}' exists there, which is unlikely).",
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
                                            foreach (var fileInfo in TitanfallManager.s_fileList) // Hardcoded list of files that Delta installs use
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

                                            // Optionally, try to delete empty directories (use with caution)
                                            // Example: Delete 'vpk' if empty after file removal
                                            // try {
                                            //    var vpkDir = Path.Combine(gamePath, "vpk");
                                            //    if (Directory.Exists(vpkDir) && !Directory.EnumerateFileSystemEntries(vpkDir).Any()) {
                                            //        Directory.Delete(vpkDir);
                                            //        Debug.WriteLine($"  Deleted empty directory: {vpkDir}");
                                            //    }
                                            // } catch (Exception ex) { Debug.WriteLine($"Error deleting empty directory vpk: {ex.Message}"); errors++; }
                                            // Repeat for other potentially empty parent dirs like 'bin/x64_retail', 'bin' etc. if desired.


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
                using (var updateManager = await UpdateManager.GitHubUpdateManager(GitHubRepoUrl))
                {
                    var updateInfo = await updateManager.CheckForUpdate();

                    if (updateInfo.ReleasesToApply.Any())
                    {
                        Debug.WriteLine($"[Squirrel] Update available: {updateInfo.FutureReleaseEntry.Version}");

                        // Show some progress indication here if desired (e.g., simple progress window)
                        await updateManager.DownloadReleases(updateInfo.ReleasesToApply);
                        await updateManager.ApplyReleases(updateInfo);

                        Debug.WriteLine("[Squirrel] Update applied. Requesting restart.");
                        UpdateManager.RestartApp(); // Signal Squirrel to restart

                        // Shutdown(0); // Shutdown is often implicit after RestartApp signals, but explicit is fine.
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
                ShowWarning($"Could not check for updates. You might be offline or GitHub is unreachable.\n\n" +
                            $"The application might be out of date.\n\nError: {ex.Message}",
                            "Update Check Failed");
                // Allow execution to continue despite error, as per requirements
                return true; // Update check failed, but allow continuing startup
            }
        }
        // --- Main Entry Point ---
        [STAThread] // Required for MessageBox and SetupWindow
        protected override void OnStartup(StartupEventArgs e)
        {
            bool shouldContinueStartup = UpdateCheck().GetAwaiter().GetResult();

            // --- Exit if Update Check Handled It ---
            if (!shouldContinueStartup)
            {
                // If UpdateCheck returned false, it means it either triggered
                // a restart (via RestartApp) or called Shutdown directly.
                // We just need to exit OnStartup cleanly.
                return;
            }
            DarkNet.Instance.SetCurrentProcessTheme(Theme.Auto);

            string originalLauncherExeDir;
            try
            {
                originalLauncherExeDir = AppContext.BaseDirectory ?? Path.GetDirectoryName(Assembly.GetEntryAssembly().Location);
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
                Environment.Exit(1);
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
                Environment.Exit(1);
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
                    SetupWindow setupWindow = null;
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
                            // --- !!! ADD THIS CHECK HERE !!! ---
                            if (!finalShowSetupSetting) // This means the user checked "Do not show again"
                            {
                                MessageBox.Show(
                                    "Setup will not be shown automatically on the next launch.\n\n" +
                                    "Hold the F4 key while starting the launcher if you need to access setup options again (e.g., change path, arguments).",
                                    "Setup Hidden", // Title of the message box
                                    MessageBoxButton.OK,
                                    MessageBoxImage.Information // Icon
                                );
                            }
                        }
                        else
                        {
                            // User cancelled the setup window
                            Debug.WriteLine("[*] Setup Window cancelled by user.");
                            Environment.Exit(0); // Exit gracefully
                            return;
                        }
                    }
                    finally
                    {
                        setupWindow?.Dispose(); // Dispose IProgress if needed
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
                    if (!TitanfallManager.ValidateGamePath(finalInstallPath, originalLauncherExeDir))
                    {
                        ShowError($"The previously configured game path is no longer valid:\n{finalInstallPath}\n\nPlease restart the launcher. Setup will run again.");
                        // Optionally clear the invalid path from registry?
                        // RegistryHelper.SaveInstallPath("");
                        Environment.Exit(1);
                        return;
                    }
                }
            }
            catch (Exception ex)
            {
                ShowError($"An unexpected error occurred during the initial setup check: {ex.Message}\nThe application will now exit.");
                Environment.Exit(1);
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
                Environment.Exit(1);
                return;
            }

            // 7. Ensure VCPP Redistributables are installed
            VisualCppInstaller.EnsureVisualCppRedistributables(); // Assuming this class exists and works

            // 8. Force High Performance GPU (unchanged logic)
            IntPtr hNvApi = LoadLibraryW("nvapi64.dll");
            LoadLibraryW("TextShaping.dll");
            // Optional: FreeLibrary(hNvApi);

            // 9. Set ContentId environment variable (unchanged logic)
            Environment.SetEnvironmentVariable("ContentId", "1025161");

            // 10. Run audio installer if necessary. Uses Current Working Directory (now game dir).
            try
            {
                if (RunAudioInstallerIfNecessary() != 0) // No longer needs path argument
                {
                    ShowError($"Failed setting up game audio.\nThe game cannot continue and has to exit.");
                    Environment.Exit(1);
                }
            }
            catch (Exception ex)
            {
                ShowError($"An error occurred during the audio setup check.\nThe game cannot continue and has to exit.\n\nError: {ex.Message}");
                Environment.Exit(1);
            }

            // 11. Prepend required R1Delta directories (relative to original launcher dir) to PATH
            if (!PrependPath(finalInstallPath, originalLauncherExeDir))
            {
                // Warning already shown in PrependPath
            }


            // 13. Load the actual game launcher DLL (from original launcher dir)
            IntPtr hLauncherModule = IntPtr.Zero;
            IntPtr pLauncherMain = IntPtr.Zero;
            string launcherDllPath = Path.Combine(originalLauncherExeDir, R1DELTA_SUBDIR, BIN_SUBDIR, LAUNCHER_DLL_NAME);

            try
            {
                Debug.WriteLine($"[*] Loading {LAUNCHER_DLL_NAME} from {launcherDllPath}");
                // Use LOAD_WITH_ALTERED_SEARCH_PATH to ensure dependencies are found in the modified PATH
                hLauncherModule = LoadLibraryExW(launcherDllPath, IntPtr.Zero, LOAD_WITH_ALTERED_SEARCH_PATH);
                if (hLauncherModule == IntPtr.Zero)
                {
                    int errorCode = Marshal.GetLastWin32Error();
                    LibraryLoadError(errorCode, LAUNCHER_DLL_NAME, launcherDllPath, originalLauncherExeDir);
                    Environment.Exit(1);
                }
                IntPtr hCoreModule = LoadLibraryW(Path.Combine(originalLauncherExeDir, R1DELTA_SUBDIR, BIN_DELTA_SUBDIR, "tier0.dll"));
                IntPtr pSetArgs = GetProcAddress(hCoreModule, "SetR1DeltaLaunchArgs");

                Debug.WriteLine($"[*] Calling SetR1DeltaLaunchArgs with: '{finalLaunchArgs ?? ""}'");
                var setArgsDelegate = (SetR1DeltaLaunchArgsDelegate)Marshal.GetDelegateForFunctionPointer(pSetArgs, typeof(SetR1DeltaLaunchArgsDelegate));
                setArgsDelegate(finalLaunchArgs ?? ""); // Pass the arguments
                Debug.WriteLine($"[*] Successfully called SetR1DeltaLaunchArgs.");

                
                    


                Debug.WriteLine("[*] Getting LauncherMain address");
                pLauncherMain = GetProcAddress(hLauncherModule, "LauncherMain");
                if (pLauncherMain == IntPtr.Zero)
                {
                    int errorCode = Marshal.GetLastWin32Error();
                    ShowError($"Failed to find the 'LauncherMain' function in '{LAUNCHER_DLL_NAME}'.\nError code: {errorCode}\n\nThe game cannot continue and has to exit.");
                    FreeLibrary(hLauncherModule); // Clean up loaded library
                    Environment.Exit(1);
                }
            }
            catch (Exception ex)
            {
                ShowError($"An unexpected error occurred while loading '{LAUNCHER_DLL_NAME}'.\nError: {ex.Message}\n\nThe game cannot continue and has to exit.");
                if (hLauncherModule != IntPtr.Zero) FreeLibrary(hLauncherModule);
                Environment.Exit(1);
            }

            // 14. Apply Process Mitigation Policies
            SetMitigationPolicies();

            // 15. Prepare arguments and call LauncherMain
            try
            {
                Debug.WriteLine("[*] Preparing arguments for LauncherMain...");
                var launcherMain = (LauncherMainDelegate)Marshal.GetDelegateForFunctionPointer(pLauncherMain, typeof(LauncherMainDelegate));


                // Call LauncherMain with only the -game argument.
                int result = launcherMain(
                        IntPtr.Zero, // hInstance - Not readily available/needed
                        IntPtr.Zero, // hPrevInstance - Always zero in modern Windows
                        "",   
                        1            // nCmdShow - SW_SHOWNORMAL
                    );

                // Note: We don't FreeLibrary(hLauncherModule) here because the game process relies on it.
                // It will be unloaded when the process exits.

                Process.GetCurrentProcess().Kill();

            }
            catch (Exception ex)
            {
                // ShowError($"An error occurred while executing 'LauncherMain'.\nError: {ex.Message}\n\nThe game may not have started correctly.");
                // Don't FreeLibrary here either, the process might still be running somehow
                Process.GetCurrentProcess().Kill();
            }
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

        // Constants for FormatMessage
        private const uint FORMAT_MESSAGE_ALLOCATE_BUFFER = 0x00000100;
        private const uint FORMAT_MESSAGE_IGNORE_INSERTS = 0x00000200;
        private const uint FORMAT_MESSAGE_FROM_SYSTEM = 0x00001000;
        private const uint FORMAT_MESSAGE_ARGUMENT_ARRAY = 0x00002000;

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

        // --- Modified Helper Function ---
        private static string GetFormattedErrorMessage(int errorCode, params string[] args)
        {
            IntPtr lpMsgBuf = IntPtr.Zero;
            IntPtr[] pArgs = null;
            IntPtr[] pArgsPtr = null; // Pointer to array of string pointers

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
                        pArgs[i] = Marshal.StringToHGlobalUni(args[i]);
                    }
                    // Create a pointer to the array of pointers
                    pArgsPtr = pArgs;
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
                    pArgsPtr);   // Array of insert strings

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
                // Free the memory allocated for the arguments
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
            }
        }


        // --- Updated LibraryLoadError ---
        // Added originalLauncherExeDir for context
        private static void LibraryLoadError(int errorCode, string libName, string location, string originalLauncherExeDir)
        {
            // Get the formatted error message using the helper
            string errorMessage = GetFormattedErrorMessage(errorCode, location);

            StringBuilder text = new StringBuilder();

            text.AppendFormat("Failed to load the {0} at \"{1}\" ({2}):\n\n{3}\n\n", libName, location, errorCode, errorMessage);
            text.Append("Make sure you followed the R1Delta installation instructions carefully before reaching out for help.");

            bool fileExists = File.Exists(location);
            string gameExePathInLauncherDir = Path.Combine(originalLauncherExeDir, "Titanfall.exe"); // Check for accidental placement

            if (errorCode == 126 && fileExists) // ERROR_MOD_NOT_FOUND
            {
                text.AppendFormat("\n\nThe file '{0}' DOES exist, so this error indicates that one of its *dependencies* failed to be found.\n\n", libName);
                text.Append("This usually means a required R1Delta file (in r1delta\\bin or r1delta\\bin_delta) is missing or corrupted, or a system dependency is missing.\n\n");
                text.Append("Try the following steps:\n");
                text.Append("1. Ensure *all* R1Delta files were extracted correctly into the launcher's directory.\n");
                text.Append("2. Install Visual C++ Redistributable (usually 2015-2022 x64): https://aka.ms/vs/17/release/vc_redist.x64.exe\n");
                text.Append("3. If using Steam/EA App for the base game files, try verifying/repairing game files (though this error is more likely R1Delta related).");
            }
            else if (File.Exists(gameExePathInLauncherDir)) // Titanfall.exe found *next to the launcher*
            {
                string launcherDirName = new DirectoryInfo(originalLauncherExeDir).Name;
                text.AppendFormat("\n\nWe detected 'Titanfall.exe' in the *same directory* as the R1Delta launcher ('{0}').\n\n", originalLauncherExeDir);
                text.Append("This is incorrect. R1Delta should be placed in its *own* folder, separate from the Titanfall game files.\n");
                text.Append("Please move the R1Delta launcher and its associated files (like the 'r1delta' folder) to a new, empty directory.");
            }
            else if (!fileExists) // The specific DLL we tried to load is missing
            {
                text.AppendFormat("\n\nThe required R1Delta file '{0}' is missing from '{1}'.\nDid you unpack *all* R1Delta files into the launcher's directory?", libName, Path.GetDirectoryName(location));
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
                string r1DeltaBinDelta = Path.Combine(launcherExeDir, R1DELTA_SUBDIR, BIN_DELTA_SUBDIR);
                string r1DeltaBin = Path.Combine(launcherExeDir, R1DELTA_SUBDIR, BIN_SUBDIR);
                string retailBin = Path.Combine(gameInstallPath, RETAIL_BIN_SUBDIR); // Game's retail bin
                string retailGameDirBin = Path.Combine(gameInstallPath, "r1", RETAIL_BIN_SUBDIR); // Game's r1 retail bin

                // Construct the new PATH: r1delta_delta -> r1delta -> game_retail -> game_r1_retail -> original PATH
                // Order matters for DLL loading precedence. R1Delta overrides first.
                string newPath = string.Join(Path.PathSeparator.ToString(),
                                             r1DeltaBinDelta,
                                             r1DeltaBin,
                                             retailBin,
                                             retailGameDirBin,
                                             currentPath);

                Environment.SetEnvironmentVariable("PATH", newPath);
                Debug.WriteLine($"[*] Updated PATH:");
                Debug.WriteLine($"    Prepended: {r1DeltaBinDelta}");
                Debug.WriteLine($"    Prepended: {r1DeltaBin}");
                Debug.WriteLine($"    Prepended: {retailBin}");
                Debug.WriteLine($"    Prepended: {retailGameDirBin}");
                // Debug.WriteLine($"    Original: {currentPath}"); // Can be very long
                return true;
            }
            catch (Exception ex)
            {
                ShowWarning($"Warning: could not modify the PATH environment variable. Game components might fail to load.\nError: {ex.Message}");
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
                        return true;
                    }
                }
                return false;
            }
            catch (DllNotFoundException)
            {
                // imm32.dll might not be present on some very minimal Windows installs (unlikely)
                Debug.WriteLine("Warning: Could not check for IME installation (imm32.dll not found?).");
                return true; // Assume IME might be installed to be safe regarding mitigations
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Warning: Exception checking for IME installation: {ex.Message}");
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
                Debug.WriteLine("SetProcessMitigationPolicy not found. Skipping mitigation setup.");
                return; // Not supported on this OS version (Win7 or older)
            }

            // Dynamically load GetProcessMitigationPolicy if needed
            if (pGetPolicy != IntPtr.Zero)
            {
                pGetProcessMitigationPolicy = (GetProcessMitigationPolicyDelegate)Marshal.GetDelegateForFunctionPointer(pGetPolicy, typeof(GetProcessMitigationPolicyDelegate));
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
                        // Ignore ERROR_INVALID_PARAMETER (87) for ShadowStack if CPU doesn't support CET
                        // Also ignore ERROR_NOT_SUPPORTED (50) which might occur on some VMs or older Win10/11 builds
                        if (policy == PROCESS_MITIGATION_POLICY.ProcessUserShadowStackPolicy && (error == 87 || error == 50))
                        {
                            Debug.WriteLine($"Ignoring SetProcessMitigationPolicy error {error} for UserShadowStackPolicy as CPU/OS may lack CET support.");
                            return;
                        }
                        // Ignore ERROR_ACCESS_DENIED (5) which can happen if policy is already set by system/debugger.
                        if (error == 5) // Access denied might be less critical
                        {
                            Debug.WriteLine($"Ignoring SetProcessMitigationPolicy error {error} (Access Denied) for policy {policy}. It might be already enforced.");
                            return;
                        }


                        string policyName = MitigationPolicyNames.ContainsKey(policy) ? MitigationPolicyNames[policy] : policy.ToString();
                        // Use WPF MessageBox here as well
                        MessageBox.Show($"Failed to set mitigation policy: {policyName}\nError: {error} ({new Win32Exception(error).Message})\n\nThis is usually a non-fatal error, the game might still work.",
                                        "Mitigation Policy Warning", MessageBoxButton.OK, MessageBoxImage.Warning);
                    }
                    else
                    {
                        Debug.WriteLine($"Successfully applied mitigation policy: {policy}");
                    }
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
                    finally { if (handle.IsAllocated) handle.Free(); }
                }
                // Set desired flags: NoRemoteImages, PreferSystem32
                imgPolicy.SetFlags(true, false, true);
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
                    catch (Exception getEx) { Debug.WriteLine($"Error getting UserShadowStackPolicy: {getEx.Message}"); }
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
                using (FileStream fs = new FileStream(filepath, FileMode.Open, FileAccess.Read, FileShare.Read))
                {
                    int bytesRead;
                    int overlap = 0; // How many bytes from the previous buffer to prepend to the current one

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
                                Debug.WriteLine($"Found 'OggS' pattern in {filepath}");
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
                    }
                }
            }
            catch (FileNotFoundException)
            {
                Debug.WriteLine($"Warning: File not found during OggS scan: {filepath}");
                return false; // File doesn't exist, can't contain the pattern
            }
            catch (IOException ex)
            {
                Debug.WriteLine($"Warning: IO error during OggS scan of {filepath}: {ex.Message}");
                return false;
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

            if (File.Exists(audioDonePath))
            {
                Debug.WriteLine("Audio setup already marked as done.");
                return 0; // Already done
            }

            if (!File.Exists(camListPath))
            {
                Debug.WriteLine($"Warning: {CAM_LIST_FILE} not found in CWD ({Directory.GetCurrentDirectory()}). Cannot check audio state.");
                // Proceed without audio check/install for Delta VPKs.
                return 0; // Indicate success (nothing to do/check)
            }

            // Check header of cam_list.txt (unchanged logic)
            try
            {
                using (StreamReader reader = new StreamReader(camListPath))
                {
                    string firstLine = reader.ReadLine();
                    if (firstLine != "CAMLIST")
                    {
                        Debug.WriteLine($"Warning: Invalid header in {camListPath}. Expected 'CAMLIST', got '{firstLine}'. Cannot proceed with audio check.");
                        return 1; // Fail as list might be corrupt
                    }
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Error reading {camListPath}: {ex.Message}");
                return 1; // Failed to read list
            }

            // Scan the target VPK for "OggS"
            bool needsInstall = ScanFileForOggS(targetVpkPath);

            if (!needsInstall)
            {
                Debug.WriteLine($"Target VPK '{targetVpkPath}' does not contain 'OggS'. Audio setup not needed or already complete.");
                // Create the done file to skip check next time
                try { File.WriteAllText(audioDonePath, "1"); } catch (Exception ex) { Debug.WriteLine($"Warning: Could not create audio done file '{audioDonePath}': {ex.Message}"); }
                return 0; // Success (not needed)
            }

            // --- Need to run the installer ---
            Debug.WriteLine($"'OggS' found in '{targetVpkPath}'. Running audio installer...");

            if (!File.Exists(installerPath))
            {
                ShowError($"Audio installer '{AUDIO_INSTALLER_EXE}' not found in game directory ({Directory.GetCurrentDirectory()}). Cannot fix audio automatically.\nPlease ensure all base game files are present.", "Audio Installer Missing");
                return 1; // Installer missing
            }

            ProcessStartInfo psi = new ProcessStartInfo
            {
                FileName = installerPath,
                Arguments = $"\"{camListPath}\"", // Quote path in case of spaces
                UseShellExecute = false,
                CreateNoWindow = true,
                WorkingDirectory = Directory.GetCurrentDirectory() // Explicitly set CWD for the process
            };

            try
            {
                using (Process process = Process.Start(psi))
                {
                    if (process == null)
                    {
                        throw new Exception("Failed to start audio installer process.");
                    }
                    process.WaitForExit(); // Consider adding a timeout?

                    if (process.ExitCode != 0)
                    {
                        ShowError($"Audio installer failed with exit code {process.ExitCode}. Please check for errors or run it manually if necessary (Executable: {installerPath}).", "Audio Installation Failed");
                        return 1; // Installer failed
                    }

                    Debug.WriteLine("Audio installer finished successfully.");

                    // Re-scan VPK to confirm fix
                    if (ScanFileForOggS(targetVpkPath))
                    {
                        Debug.WriteLine($"Warning: Target VPK '{targetVpkPath}' still contains 'OggS' after installer ran. The fix might not have worked.");
                        ShowWarning($"Audio installer finished, but the target file still seems incorrect. Audio issues might persist.", "Audio Setup Warning");
                        // Don't create done file if re-scan fails
                    }
                    else
                    {
                        Debug.WriteLine("Target VPK verified clean after install.");
                        // Create the done file
                        try { File.WriteAllText(audioDonePath, "1"); } catch (Exception ex) { Debug.WriteLine($"Warning: Could not create audio done file '{audioDonePath}' after successful install: {ex.Message}"); }
                    }

                    return 0; // Success (even if verification failed, we tried)
                }
            }
            catch (Exception ex)
            {
                ShowError($"An error occurred while running the audio installer:\n{ex.Message}", "Audio Installation Error");
                return 1; // Error running installer
            }
        }
    }
}