using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Net;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;

namespace launcher_ex
{
    public static class NativePrerequisiteInstaller
    {
        private const string VisualCpp2010Url =
            "https://download.microsoft.com/download/1/6/5/165255E7-1014-4D0A-B094-B6A430A6BFFC/vcredist_x64.exe";
        private const string VisualCpp2010Sha256 =
            "F3B7A76D84D23F91957AA18456A14B4E90609E4CE8194C5653384ED38DADA6F3";
        private const string VisualCppV14Url = "https://aka.ms/vc14/vc_redist.x64.exe";
        private const string DirectXLegacyUrl =
            "https://download.microsoft.com/download/1/7/1/1718CCC4-6315-4D8E-9543-8E28A4E18C4C/dxwebsetup.exe";
        private const string DirectXLegacySha256 =
            "2CF71D098C608C56E07F4655855A886C3102553F648DF88458DF616B26FD612F";
        private static readonly Version VisualCpp2010MinimumVersion = new Version(10, 0, 40219, 325);
        private static readonly Version VisualCppV14MinimumVersion = new Version(14, 51, 0, 0);
        private static readonly Guid WinTrustActionGenericVerifyV2 =
            new Guid("00AAC56B-CD44-11d0-8CC2-00C04FC295EE");

        private enum WinTrustDataUiChoice : uint
        {
            None = 2
        }

        private enum WinTrustDataUnionChoice : uint
        {
            File = 1
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct WinTrustFileInfo
        {
            public uint StructSize;
            public IntPtr FilePath;
            public IntPtr FileHandle;
            public IntPtr KnownSubject;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct WinTrustData
        {
            public uint StructSize;
            public IntPtr PolicyCallbackData;
            public IntPtr SipClientData;
            public WinTrustDataUiChoice UiChoice;
            public uint RevocationChecks;
            public WinTrustDataUnionChoice UnionChoice;
            public IntPtr FileInfo;
            public uint StateAction;
            public IntPtr StateData;
            public IntPtr UrlReference;
            public uint ProviderFlags;
            public uint UiContext;
        }

        [DllImport("wintrust.dll", ExactSpelling = true, SetLastError = true)]
        private static extern uint WinVerifyTrust(
            IntPtr window,
            [In] ref Guid actionId,
            IntPtr trustData);

        /// <summary>
        /// Ensures that the x64 VC++ 2010 SP1 and current v14 runtimes plus the
        /// legacy DirectX components required by the packaged native binaries are
        /// installed. Returns false instead of launching the game with a partially
        /// installed runtime.
        /// </summary>
        public static bool EnsureNativePrerequisites(out string errorMessage)
        {
            errorMessage = null;

            try
            {
                if (!HasVisualCpp2010Runtime())
                {
                    if (!DownloadInstallAndVerify(
                            "Microsoft Visual C++ 2010 SP1 Redistributable (x64)",
                            VisualCpp2010Url,
                            VisualCpp2010Sha256,
                            "/quiet /norestart",
                            HasVisualCpp2010Runtime,
                            out errorMessage))
                        return false;
                }

                if (!HasVisualCppV14Runtime())
                {
                    if (!DownloadInstallAndVerify(
                            "Microsoft Visual C++ v14 Redistributable (x64)",
                            VisualCppV14Url,
                            null,
                            "/quiet /norestart",
                            HasVisualCppV14Runtime,
                            out errorMessage))
                        return false;
                }

                if (!HasLegacyDirectXRuntime())
                {
                    if (!DownloadInstallAndVerify(
                            "Microsoft DirectX End-User Runtime (June 2010)",
                            DirectXLegacyUrl,
                            DirectXLegacySha256,
                            "/Q",
                            HasLegacyDirectXRuntime,
                            out errorMessage))
                        return false;
                }

                return true;
            }
            catch (Exception error)
            {
                errorMessage = "Could not verify the required Microsoft native runtimes.\n\n" + error.Message;
                return false;
            }
        }

        private static bool HasVisualCpp2010Runtime()
        {
            return FileVersionAtLeast(SystemRuntimePath("msvcr100.dll"), VisualCpp2010MinimumVersion)
                && FileVersionAtLeast(SystemRuntimePath("msvcp100.dll"), VisualCpp2010MinimumVersion);
        }

        private static bool HasVisualCppV14Runtime()
        {
            return FileVersionAtLeast(SystemRuntimePath("vcruntime140.dll"), VisualCppV14MinimumVersion)
                && FileVersionAtLeast(SystemRuntimePath("vcruntime140_1.dll"), VisualCppV14MinimumVersion)
                && FileVersionAtLeast(SystemRuntimePath("msvcp140.dll"), VisualCppV14MinimumVersion);
        }

        private static bool HasLegacyDirectXRuntime()
        {
            string[] requiredFiles =
            {
                "D3DCompiler_43.dll",
                "d3dx9_43.dll",
                "XAudio2_7.dll",
                "XInput1_3.dll",
                "XAPOFX1_5.dll"
            };

            string[] systemDirectories =
            {
                Environment.GetFolderPath(Environment.SpecialFolder.System),
                Environment.GetFolderPath(Environment.SpecialFolder.SystemX86)
            };

            foreach (string directory in systemDirectories)
            {
                if (string.IsNullOrEmpty(directory))
                    return false;
                foreach (string fileName in requiredFiles)
                {
                    if (!File.Exists(Path.Combine(directory, fileName)))
                        return false;
                }
            }

            return true;
        }

        private static string SystemRuntimePath(string fileName)
        {
            return Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.System),
                fileName);
        }

        private static bool FileVersionAtLeast(string path, Version minimum)
        {
            if (!File.Exists(path))
                return false;

            string rawVersion = FileVersionInfo.GetVersionInfo(path).FileVersion;
            Version actual;
            return Version.TryParse(rawVersion, out actual) && actual >= minimum;
        }

        private static bool DownloadInstallAndVerify(
            string displayName,
            string url,
            string expectedSha256,
            string installerArguments,
            Func<bool> isInstalled,
            out string errorMessage)
        {
            errorMessage = null;
            string tempDirectory = Path.Combine(
                Path.GetTempPath(),
                "R1DeltaPrerequisite-" + Guid.NewGuid().ToString("N"));
            string installerPath = Path.Combine(tempDirectory, "installer.exe");

            try
            {
                Directory.CreateDirectory(tempDirectory);
                ServicePointManager.SecurityProtocol |= SecurityProtocolType.Tls12;

                using (WebClient client = new WebClient())
                {
                    client.Headers[HttpRequestHeader.UserAgent] = "R1Delta Launcher";
                    client.DownloadFile(new Uri(url), installerPath);
                }

                if (!string.IsNullOrEmpty(expectedSha256)
                    && !string.Equals(ComputeSha256(installerPath), expectedSha256, StringComparison.OrdinalIgnoreCase))
                {
                    errorMessage = displayName + " failed its SHA-256 integrity check. The downloaded installer was not run.";
                    return false;
                }

                if (!IsMicrosoftSigned(installerPath))
                {
                    errorMessage = displayName + " did not have a valid Microsoft Authenticode signature. The downloaded installer was not run.";
                    return false;
                }

                using (Process process = new Process())
                {
                    process.StartInfo = new ProcessStartInfo
                    {
                        FileName = installerPath,
                        Arguments = installerArguments,
                        UseShellExecute = true,
                        Verb = "runas",
                        WorkingDirectory = tempDirectory
                    };

                    try
                    {
                        process.Start();
                    }
                    catch (Win32Exception error) when (error.NativeErrorCode == 1223)
                    {
                        errorMessage = displayName + " is required, but its administrator prompt was cancelled.";
                        return false;
                    }

                    if (!process.WaitForExit(10 * 60 * 1000))
                    {
                        try { process.Kill(); } catch { }
                        errorMessage = displayName + " did not finish within ten minutes.";
                        return false;
                    }

                    if (process.ExitCode != 0 && process.ExitCode != 1638 && process.ExitCode != 3010)
                    {
                        errorMessage = displayName + " installation failed with exit code " + process.ExitCode + ".";
                        return false;
                    }
                }

                if (!isInstalled())
                {
                    errorMessage = displayName + " completed, but its required runtime files are still unavailable. A Windows restart may be required.";
                    return false;
                }

                return true;
            }
            catch (Exception error)
            {
                errorMessage = "Could not install " + displayName + ".\n\n" + error.Message;
                return false;
            }
            finally
            {
                try
                {
                    if (Directory.Exists(tempDirectory))
                        Directory.Delete(tempDirectory, true);
                }
                catch (Exception cleanupError)
                {
                    Debug.WriteLine("Could not remove prerequisite temporary directory: " + cleanupError.Message);
                }
            }
        }

        private static string ComputeSha256(string path)
        {
            using (FileStream stream = File.OpenRead(path))
            using (SHA256 sha256 = SHA256.Create())
                return BitConverter.ToString(sha256.ComputeHash(stream)).Replace("-", string.Empty);
        }

        private static bool IsMicrosoftSigned(string path)
        {
            IntPtr filePath = IntPtr.Zero;
            IntPtr fileInfoPointer = IntPtr.Zero;
            IntPtr trustDataPointer = IntPtr.Zero;

            try
            {
                filePath = Marshal.StringToCoTaskMemUni(path);
                WinTrustFileInfo fileInfo = new WinTrustFileInfo
                {
                    StructSize = (uint)Marshal.SizeOf(typeof(WinTrustFileInfo)),
                    FilePath = filePath
                };
                fileInfoPointer = Marshal.AllocCoTaskMem(Marshal.SizeOf(typeof(WinTrustFileInfo)));
                Marshal.StructureToPtr(fileInfo, fileInfoPointer, false);

                WinTrustData trustData = new WinTrustData
                {
                    StructSize = (uint)Marshal.SizeOf(typeof(WinTrustData)),
                    UiChoice = WinTrustDataUiChoice.None,
                    UnionChoice = WinTrustDataUnionChoice.File,
                    FileInfo = fileInfoPointer
                };
                trustDataPointer = Marshal.AllocCoTaskMem(Marshal.SizeOf(typeof(WinTrustData)));
                Marshal.StructureToPtr(trustData, trustDataPointer, false);

                Guid action = WinTrustActionGenericVerifyV2;
                if (WinVerifyTrust(IntPtr.Zero, ref action, trustDataPointer) != 0)
                    return false;

                using (X509Certificate2 certificate =
                    new X509Certificate2(X509Certificate.CreateFromSignedFile(path)))
                {
                    return certificate.Subject.IndexOf(
                        "O=Microsoft Corporation",
                        StringComparison.OrdinalIgnoreCase) >= 0;
                }
            }
            catch (CryptographicException)
            {
                return false;
            }
            finally
            {
                if (trustDataPointer != IntPtr.Zero)
                    Marshal.FreeCoTaskMem(trustDataPointer);
                if (fileInfoPointer != IntPtr.Zero)
                    Marshal.FreeCoTaskMem(fileInfoPointer);
                if (filePath != IntPtr.Zero)
                    Marshal.FreeCoTaskMem(filePath);
            }
        }
    }
}
