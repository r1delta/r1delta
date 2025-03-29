using R1Delta;
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
using System.Windows.Forms;
using Application = System.Windows.Application;
using MessageBox = System.Windows.MessageBox;

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

        // --- Constants ---
        private const uint LOAD_WITH_ALTERED_SEARCH_PATH = 0x00000008;
        private const string LAUNCHER_DLL_NAME = "launcher.dll";
        private const string R1DELTA_SUBDIR = "r1delta";
        private const string BIN_SUBDIR = "bin";
        private const string BIN_DELTA_SUBDIR = "bin_delta";
        private const string RETAIL_BIN_SUBDIR = "bin\\x64_retail";
        private const string GAME_EXECUTABLE = "Titanfall.exe";
        private const string AUDIO_DONE_FILE = "vpk\\audio_done.txt";
        private const string CAM_LIST_FILE = "vpk\\cam_list.txt";
        private const string TARGET_VPK_FILE = "vpk\\client_mp_common.bsp.pak000_040.vpk";
        private const string AUDIO_INSTALLER_EXE = "bin\\x64\\audio_installer.exe";

        // --- Mitigation Policy Structures & Enum ---
        // Ensure correct layout and packing for P/Invoke
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
        // Define the delegate for LauncherMain
        // int LauncherMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, int nCmdShow);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        delegate int LauncherMainDelegate(IntPtr hInstance, IntPtr hPrevInstance, [MarshalAs(UnmanagedType.LPStr)] string lpCmdLine, int nCmdShow);

        // --- Main Entry Point ---
        [STAThread] // Required for MessageBox
        protected override void OnStartup(StartupEventArgs e)
        {
            string exePath;
            // Get the directory containing the current executable
            exePath = AppContext.BaseDirectory ?? Path.GetDirectoryName(Assembly.GetEntryAssembly().Location);
            if (string.IsNullOrEmpty(exePath))
            {
                throw new Exception("Could not determine application directory.");
            }
            // Ensure trailing slash is removed for consistency
            exePath = exePath.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);


            // Set the current directory to the executable's path
            Directory.SetCurrentDirectory(exePath);
            VisualCppInstaller.EnsureVisualCppRedistributables();
            string binPath = Path.Combine(exePath, "bin");
            string vpkPath = Path.Combine(exePath, "vpk");
            string r1Path = Path.Combine(exePath, "r1");

            bool isBinJunction = !Directory.Exists(binPath) || (new DirectoryInfo(binPath).Attributes & FileAttributes.ReparsePoint) != 0;
            bool isVpkJunction = !Directory.Exists(vpkPath) || (new DirectoryInfo(vpkPath).Attributes & FileAttributes.ReparsePoint) != 0;
            bool isR1Junction = !Directory.Exists(r1Path) || (new DirectoryInfo(r1Path).Attributes & FileAttributes.ReparsePoint) != 0;

            string targetVpkFullPath = Path.Combine(exePath, "vpk\\client_mp_common.bsp.pak000_000.vpk");
            if (!isBinJunction && !isVpkJunction && !isR1Junction && !File.Exists(targetVpkFullPath))
            {
                ShowError("Unfortunately, it appears your installation is corrupted. The required directories 'bin', 'vpk', and 'r1' are not junctions but the marker VPK file \'client_mp_common.bsp.pak000_000.vpk\' is missing. R1Delta cannot continue and must now exit.");
                Environment.Exit(1);
            }
            TitanfallManager.EnsureTitanfallPresent();
            // Force High Performance GPU on Laptops
            // NVIDIA: Load nvapi64.dll. This is enough to signal the driver.
            // AMD: The C++ export trick (__declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 1)
            //      doesn't have a direct, reliable C# equivalent using LoadLibrary.
            //      Users might need to configure this via AMD driver settings.
            IntPtr hNvApi = LoadLibraryW("nvapi64.dll");
            // We don't need to keep the handle, just loading it is the signal.
            // FreeLibrary(hNvApi); // Optional: Can free it if desired, but not necessary for the effect.

            // Set ContentId environment variable (needed for other languages)
            Environment.SetEnvironmentVariable("ContentId", "1025161");




            // Check if the game executable exists in the current directory
            // Do this *before* audio check and PATH modification as it's a fundamental check.
            /*if (!File.Exists(Path.Combine(exePath, GAME_EXECUTABLE)))
            {
                string extraHint = "";
                if (File.Exists(Path.Combine(exePath, "..", GAME_EXECUTABLE)) || File.Exists(Path.Combine(exePath, "..", "..", GAME_EXECUTABLE)))
                {
                    string currentDirName = new DirectoryInfo(exePath).Name;
                    string parentDirName = new DirectoryInfo(Path.GetFullPath(Path.Combine(exePath, ".."))).Name;
                    extraHint = $"\n\nWe detected that you might have extracted the files into a *subdirectory* ('{currentDirName}') of your Titanfall installation.\n\nPlease move all the files and folders from the current folder into the Titanfall installation directory ('{parentDirName}').";
                }
                else
                {
                    extraHint = "\n\nRemember: you need to unpack the contents of this archive into your Titanfall game installation directory, not just to any random folder.";
                }
                ShowError($"'{GAME_EXECUTABLE}' not found in the current directory ('{exePath}').\nMake sure the launcher is placed in the main Titanfall game folder.{extraHint}");
                Environment.Exit( 1);
            }*/

            // Run audio installer if necessary
            try
            {
                if (RunAudioInstallerIfNecessary(exePath) != 0)
                {
                    ShowError($"Failed setting up game audio.\nThe game cannot continue and has to exit.");
                    Environment.Exit( 1);
                }
            }
            catch (Exception ex)
            {
                ShowError($"An error occurred during the audio setup check.\nThe game cannot continue and has to exit.\n\nError: {ex.Message}");
                 Environment.Exit( 1);
            }


            // Prepend required directories to PATH
            if (!PrependPath(exePath))
            {
                // Warning already shown in PrependPath
            }
            

            // Load the actual game launcher DLL
            IntPtr hLauncherModule = IntPtr.Zero;
            IntPtr pLauncherMain = IntPtr.Zero;
            string launcherDllPath = Path.Combine(exePath, R1DELTA_SUBDIR, BIN_SUBDIR, LAUNCHER_DLL_NAME);

            try
            {
                Debug.WriteLine($"[*] Loading {LAUNCHER_DLL_NAME}");
                // Use LOAD_WITH_ALTERED_SEARCH_PATH to ensure dependencies are found in the modified PATH
                hLauncherModule = LoadLibraryExW(launcherDllPath, IntPtr.Zero, LOAD_WITH_ALTERED_SEARCH_PATH);
                if (hLauncherModule == IntPtr.Zero)
                {
                    int errorCode = Marshal.GetLastWin32Error();
                    LibraryLoadError(errorCode, LAUNCHER_DLL_NAME, launcherDllPath);
                    Environment.Exit(1);
                }

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

           /* try
            {*/
                Debug.WriteLine("[*] Launching the game via LauncherMain...");
                var launcherMain = (LauncherMainDelegate)Marshal.GetDelegateForFunctionPointer(pLauncherMain, typeof(LauncherMainDelegate));
            // Apply Process Mitigation Policies
            SetMitigationPolicies();

            // Call LauncherMain. Parameters are mostly unused by the target function according to the original code comment.
            // Pass dummy values or approximations.
            int result = launcherMain(
                    IntPtr.Zero, // hInstance - Not readily available/needed
                    IntPtr.Zero, // hPrevInstance - Always zero in modern Windows
                    string.Join(" ", Environment.GetCommandLineArgs()), // lpCmdLine - Pass command line arguments
                    1 // nCmdShow - SW_SHOWNORMAL
                );

                // Note: We don't FreeLibrary(hLauncherModule) here because the game process relies on it.
                // It will be unloaded when the process exits.

                Environment.Exit(result);
           /* }
            catch (Exception ex)
            {
                ShowError($"An error occurred while executing 'LauncherMain'.\nError: {ex.Message}\n\nThe game may not have started correctly.");
                // Don't FreeLibrary here either, the process might still be running somehow
                Environment.Exit(1); // Indicate failure
            }*/
        }

        // --- Helper Functions ---

        private static void ShowError(string message, string title = "R1Delta Launcher Error")
        {
            System.Windows.Forms.MessageBox.Show(message, title, MessageBoxButtons.OK, MessageBoxIcon.Error);
        }

        private static void ShowWarning(string message, string title = "R1Delta Launcher Warning")
        {
            System.Windows.Forms.MessageBox.Show(message, title, MessageBoxButtons.OK, MessageBoxIcon.Warning);
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
                    // (This might happen if the error code itself is invalid)
                    int formatMessageError = Marshal.GetLastWin32Error();
                    Console.WriteLine($"FormatMessage failed with error: {formatMessageError}"); // Debug info
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
        private static void LibraryLoadError(int errorCode, string libName, string location)
        {
            // Get the formatted error message using the helper
            // For error 193 (%1 is not a valid Win32 application), %1 is typically the filename.
            string errorMessage = GetFormattedErrorMessage(errorCode, location);

            StringBuilder text = new StringBuilder();

            text.AppendFormat("Failed to load the {0} at \"{1}\" ({2}):\n\n{3}\n\n", libName, location, errorCode, errorMessage);
            text.Append("Make sure you followed the R1Delta installation instructions carefully before reaching out for help.");

            bool fileExists = File.Exists(location);

            if (errorCode == 126 && fileExists) // ERROR_MOD_NOT_FOUND
            {
                text.AppendFormat("\n\nThe file at the specified location DOES exist, so this error indicates that one of its *dependencies* failed to be found.\n\n");
                text.Append("Try the following steps:\n");
                text.Append("1. Install Visual C++ Redistributable (usually 2015-2022 x64): https://aka.ms/vs/17/release/vc_redist.x64.exe\n");
                text.Append("2. If using Steam/EA App, try verifying/repairing game files.");
            }
            else if (!File.Exists(GAME_EXECUTABLE))
            {
                if (File.Exists(Path.Combine(Directory.GetCurrentDirectory(), "..", GAME_EXECUTABLE)) || File.Exists(Path.Combine(Directory.GetCurrentDirectory(), "..", "..", GAME_EXECUTABLE)))
                {
                    string currentDirName = new DirectoryInfo(Directory.GetCurrentDirectory()).Name;
                    string parentDirName = new DirectoryInfo(Path.GetFullPath(Path.Combine(Directory.GetCurrentDirectory(), ".."))).Name;
                    text.AppendFormat("\n\nWe detected that you might have extracted the files into a *subdirectory* ('{0}') of your Titanfall installation.\nPlease move all the files and folders from the current folder into the Titanfall installation directory ('{1}').", currentDirName, parentDirName);
                }
                else
                {
                    text.Append("\n\nRemember: you need to unpack the contents of this archive into your Titanfall game installation directory, not just to any random folder.");
                }
            }
            else if (File.Exists(GAME_EXECUTABLE) && !fileExists) // Game exe exists, but DLL doesn't
            {
                text.AppendFormat("\n\n'{0}' has been found in the current directory, but the required file '{1}' is missing.\nDid you unpack all R1Delta files here?", GAME_EXECUTABLE, libName);
            }

            ShowError(text.ToString()); // Replace with your actual UI error display method
        }

        private static bool PrependPath(string exePath)
        {
            try
            {
                string currentPath = Environment.GetEnvironmentVariable("PATH") ?? "";
                string r1DeltaBinDelta = Path.Combine(exePath, R1DELTA_SUBDIR, BIN_DELTA_SUBDIR);
                string r1DeltaBin = Path.Combine(exePath, R1DELTA_SUBDIR, BIN_SUBDIR);
                string retailBin = Path.Combine(exePath, RETAIL_BIN_SUBDIR);

                // Construct the new PATH: r1delta_delta -> r1delta -> retail -> current -> original
                string newPath = string.Join(";", r1DeltaBinDelta, r1DeltaBin, retailBin, ".", currentPath);

                Environment.SetEnvironmentVariable("PATH", newPath);
                Debug.WriteLine($"[*] Prepended to PATH: {r1DeltaBinDelta};{r1DeltaBin};{retailBin};.");
                return true;
            }
            catch (Exception ex)
            {
                ShowWarning($"Warning: could not modify the PATH environment variable. Game components might fail to load.\nError: {ex.Message}");
                return false;
            }
        }

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
                        // Ignore ERROR_INVALID_PARAMETER (5) for ShadowStack if CPU doesn't support CET
                        if (policy == PROCESS_MITIGATION_POLICY.ProcessUserShadowStackPolicy && error == 5)
                        {
                            Debug.WriteLine($"Ignoring SetProcessMitigationPolicy error {error} for UserShadowStackPolicy as CPU lacks CET support.");
                            return;
                        }
                        // Ignore ERROR_ACCESS_DENIED (5) which can happen if policy is already set by system/debugger.
                        if (error == 5 && policy != PROCESS_MITIGATION_POLICY.ProcessUserShadowStackPolicy) // Access denied might be more critical for non-CET
                        {
                            Debug.WriteLine($"Ignoring SetProcessMitigationPolicy error {error} (Access Denied) for policy {policy}. It might be already enforced.");
                            return;
                        }


                        string policyName = MitigationPolicyNames.ContainsKey(policy) ? MitigationPolicyNames[policy] : policy.ToString();
                        ShowWarning($"Failed to set mitigation policy: {policyName}\nError: {error} ({new Win32Exception(error).Message})\n\nThis is usually a non-fatal error, the game might still work.", "Mitigation Policy Warning");
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
            // Only attempt if CPU supports it and OS supports the API (checked via SetPolicyEnsureOK implicitly)
            if (true)
            {
                try
                {
                    var ussPolicy = new PROCESS_MITIGATION_USER_SHADOW_STACK_POLICY();
                    bool gotPolicy = false;
                    if (pGetProcessMitigationPolicy != null)
                    {
                        GCHandle handle = GCHandle.Alloc(ussPolicy, GCHandleType.Pinned);
                        try { gotPolicy = pGetProcessMitigationPolicy(GetCurrentProcess(), PROCESS_MITIGATION_POLICY.ProcessUserShadowStackPolicy, handle.AddrOfPinnedObject(), (nuint)Marshal.SizeOf(ussPolicy)); }
                        finally { if (handle.IsAllocated) handle.Free(); }
                    }

                    // If we could read the policy AND shadow stack is already enabled (by OS/loader), try enabling strict mode.
                    // Otherwise, don't try to enable it from scratch here, as it often requires loader support.
                    if (gotPolicy && ussPolicy.EnableUserShadowStack)
                    {
                        Debug.WriteLine("Shadow Stack already enabled, attempting to enable Strict Mode.");
                        // Read current flags again to be safe
                        uint currentFlags = ussPolicy.Flags;
                        // Set desired flags: Keep existing + Enable Strict Mode
                        // Caution: Strict mode can break some things. Test carefully.
                        ussPolicy.SetFlags(
                            enable: (currentFlags & (uint)MitigationFlags.Enable) != 0,
                            audit: (currentFlags & (uint)MitigationFlags.Audit) != 0,
                            setContext: (currentFlags & ((uint)MitigationFlags.Enable << 1)) != 0,
                            auditSetContext: (currentFlags & ((uint)MitigationFlags.Audit << 1)) != 0,
                            strictMode: true, // Enable Strict Mode
                            blockNonCet: (currentFlags & ((uint)MitigationFlags.Enable << 3)) != 0,
                            blockNonCetNonEhcont: (currentFlags & ((uint)MitigationFlags.Enable << 4)) != 0,
                            auditBlockNonCet: (currentFlags & ((uint)MitigationFlags.Audit << 3)) != 0,
                            auditBlockNonCetNonEhcont: (currentFlags & ((uint)MitigationFlags.Audit << 4)) != 0,
                            cetDynamicFix: (currentFlags & ((uint)MitigationFlags.Enable << 5)) != 0,
                            strictAlign: (currentFlags & ((uint)MitigationFlags.Enable << 6)) != 0
                        );
                        SetPolicyEnsureOK(PROCESS_MITIGATION_POLICY.ProcessUserShadowStackPolicy, ussPolicy);
                    }
                    else if (gotPolicy)
                    {
                        Debug.WriteLine("Shadow Stack is not enabled by default for this process.");
                    }
                    else
                    {
                        Debug.WriteLine("Could not get current UserShadowStackPolicy state.");
                    }
                }
                catch (Exception ex) { Debug.WriteLine($"Error applying User Shadow Stack policy: {ex.Message}"); }
            }
            //else
            //{
               // Debug.WriteLine("Skipping UserShadowStackPolicy setup as CPU does not support CET.");
            //}
        }

        // --- Audio Installer Logic ---

        /// <summary>
        /// Scans a file for the "OggS" magic bytes. Non-SIMD version.
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
                // Decide how to handle IO errors - maybe treat as "not found" or rethrow?
                // For this purpose, let's assume error means we can't confirm, so treat as not found.
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
        /// Returns 0 on success (or already done), 1 on failure.
        /// </summary>
        private static int RunAudioInstallerIfNecessary(string exePath)
        {
            string audioDonePath = Path.Combine(exePath, AUDIO_DONE_FILE);
            string camListPath = Path.Combine(exePath, CAM_LIST_FILE);
            string targetVpkPath = Path.Combine(exePath, TARGET_VPK_FILE);
            string installerPath = Path.Combine(exePath, AUDIO_INSTALLER_EXE);

            if (File.Exists(audioDonePath))
            {
                Debug.WriteLine("Audio setup already marked as done.");
                return 0; // Already done
            }

            if (!File.Exists(camListPath))
            {
                Debug.WriteLine($"Warning: {CAM_LIST_FILE} not found. Cannot check audio state.");
                // Proceed without audio check/install. We don't have this in Delta VPKs.
                // ShowWarning($"Required file '{CAM_LIST_FILE}' not found. Audio setup cannot be verified or performed.", "Audio Setup Warning");
                return 0; // Indicate success
            }

            try
            {
                // Check header of cam_list.txt
                using (StreamReader reader = new StreamReader(camListPath))
                {
                    string firstLine = reader.ReadLine();
                    if (firstLine != "CAMLIST")
                    {
                        Debug.WriteLine($"Warning: Invalid header in {CAM_LIST_FILE}. Expected 'CAMLIST', got '{firstLine}'.");
                        // Fail here as the list might be corrupt
                        return 1;
                    }
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Error reading {CAM_LIST_FILE}: {ex.Message}");
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
                ShowError($"Audio installer '{AUDIO_INSTALLER_EXE}' not found. Cannot fix audio automatically.\nPlease ensure all R1Delta files are extracted correctly.", "Audio Installer Missing");
                return 1; // Installer missing
            }

            ProcessStartInfo psi = new ProcessStartInfo
            {
                FileName = installerPath,
                Arguments = $"\"{camListPath}\"", // Quote path in case of spaces
                UseShellExecute = false, // Required to get exit code reliably
                CreateNoWindow = true, // Hide installer console window
                WorkingDirectory = exePath // Set working directory to game root
            };

            try
            {
                using (Process process = Process.Start(psi))
                {
                    if (process == null)
                    {
                        throw new Exception("Failed to start audio installer process.");
                    }
                    // Consider adding a timeout?
                    process.WaitForExit();

                    if (process.ExitCode != 0)
                    {
                        ShowError($"Audio installer failed with exit code {process.ExitCode}. Please check for errors or run it manually if necessary.", "Audio Installation Failed");
                        return 1; // Installer failed
                    }

                    Debug.WriteLine("Audio installer finished successfully.");

                    // Optional: Re-scan VPK to confirm fix, though trusting exit code 0 is usually enough.
                    if (ScanFileForOggS(targetVpkPath))
                    {
                        Debug.WriteLine($"Warning: Target VPK '{targetVpkPath}' still contains 'OggS' after installer ran. The fix might not have worked.");
                        // Don't create done file if re-scan fails? Or let it be created? Let's not create it.
                        ShowWarning($"Audio installer finished, but the target file still seems incorrect. Audio issues might persist.", "Audio Setup Warning");
                        // return 1; // Or return 0 but show warning? Let's return 0 but warn.
                    }
                    else
                    {
                        Debug.WriteLine("Target VPK verified clean after install.");
                        // Create the done file
                        try { File.WriteAllText(audioDonePath, "1"); } catch (Exception ex) { Debug.WriteLine($"Warning: Could not create audio done file '{audioDonePath}' after successful install: {ex.Message}"); }
                    }

                    return 0; // Success
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
