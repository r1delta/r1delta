using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Net;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;

namespace launcher_ex
{
    public enum NativePrerequisiteStatus
    {
        Ready,
        Failed,
        RebootRequired
    }

    public sealed class NativePrerequisiteResult
    {
        private NativePrerequisiteResult(
            NativePrerequisiteStatus status,
            string message,
            int? installerExitCode)
        {
            Status = status;
            Message = message;
            InstallerExitCode = installerExitCode;
        }

        public NativePrerequisiteStatus Status { get; }
        public string Message { get; }
        public int? InstallerExitCode { get; }

        internal static NativePrerequisiteResult Ready(int? installerExitCode = null)
        {
            return new NativePrerequisiteResult(
                NativePrerequisiteStatus.Ready,
                null,
                installerExitCode);
        }

        internal static NativePrerequisiteResult Failed(string message, int? installerExitCode = null)
        {
            return new NativePrerequisiteResult(
                NativePrerequisiteStatus.Failed,
                message,
                installerExitCode);
        }

        internal static NativePrerequisiteResult RebootRequired(string message, int installerExitCode)
        {
            return new NativePrerequisiteResult(
                NativePrerequisiteStatus.RebootRequired,
                message,
                installerExitCode);
        }
    }

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

        internal sealed class RuntimeFileProbe
        {
            internal RuntimeFileProbe(
                string checkedPath,
                Version requiredVersion,
                bool exists,
                string rawVersion,
                Version parsedVersion,
                string readFailure)
            {
                CheckedPath = checkedPath;
                RequiredVersion = requiredVersion;
                Exists = exists;
                RawVersion = rawVersion;
                ParsedVersion = parsedVersion;
                ReadFailure = readFailure;
            }

            public string CheckedPath { get; }
            public Version RequiredVersion { get; }
            public bool Exists { get; }
            public string RawVersion { get; }
            public Version ParsedVersion { get; }
            public string ReadFailure { get; }

            public bool MeetsRequirement
            {
                get
                {
                    if (!Exists || ReadFailure != null)
                        return false;
                    if (RequiredVersion == null)
                        return true;
                    return ParsedVersion != null && ParsedVersion >= RequiredVersion;
                }
            }

            internal string Describe()
            {
                StringBuilder description = new StringBuilder();
                description.Append("Path: ").Append(CheckedPath).AppendLine();

                if (ReadFailure != null)
                    description.Append("Observed state: unreadable (").Append(ReadFailure).AppendLine(")");
                else if (!Exists)
                    description.AppendLine("Observed state: missing");
                else if (RequiredVersion == null)
                    description.AppendLine("Observed state: present");
                else if (ParsedVersion == null)
                    description.AppendLine("Observed state: raw version is not parseable");
                else if (ParsedVersion < RequiredVersion)
                    description.AppendLine("Observed state: version is below the required minimum");
                else
                    description.AppendLine("Observed state: version meets the required minimum");

                if (RequiredVersion != null)
                {
                    description.Append("Raw file version: ")
                        .Append(string.IsNullOrEmpty(RawVersion) ? "<unavailable>" : "\"" + RawVersion + "\"")
                        .AppendLine();
                    description.Append("Parsed version: ")
                        .Append(ParsedVersion == null ? "<unavailable>" : ParsedVersion.ToString())
                        .AppendLine();
                    description.Append("Required minimum: ").Append(RequiredVersion);
                }

                return description.ToString();
            }
        }

        private sealed class PrerequisiteVerification
        {
            internal PrerequisiteVerification(RuntimeFileProbe[] probes)
            {
                Probes = probes;
            }

            internal RuntimeFileProbe[] Probes { get; }

            internal bool IsSatisfied
            {
                get
                {
                    foreach (RuntimeFileProbe probe in Probes)
                    {
                        if (!probe.MeetsRequirement)
                            return false;
                    }

                    return true;
                }
            }

            internal string Describe()
            {
                StringBuilder description = new StringBuilder();
                for (int index = 0; index < Probes.Length; index++)
                {
                    if (index != 0)
                        description.AppendLine().AppendLine();
                    description.Append(Probes[index].Describe());
                }
                return description.ToString();
            }
        }


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

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct RtlOsVersionInfoEx
        {
            public uint Size;
            public uint MajorVersion;
            public uint MinorVersion;
            public uint BuildNumber;
            public uint PlatformId;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)]
            public string ServicePack;
            public ushort ServicePackMajor;
            public ushort ServicePackMinor;
            public ushort SuiteMask;
            public byte ProductType;
            public byte Reserved;
        }

        [DllImport("ntdll.dll", ExactSpelling = true, CharSet = CharSet.Unicode)]
        private static extern int RtlGetVersion(ref RtlOsVersionInfoEx versionInformation);

        [DllImport("wintrust.dll", ExactSpelling = true, SetLastError = true)]
        private static extern uint WinVerifyTrust(
            IntPtr window,
            [In] ref Guid actionId,
            IntPtr trustData);

        /// <summary>
        /// Ensures that the x64 VC++ 2010 SP1 and current v14 runtimes plus the
        /// legacy DirectX components required by the packaged native binaries are
        /// installed. The result remains non-ready until every required file passes
        /// verification.
        /// </summary>
        public static NativePrerequisiteResult EnsureNativePrerequisites()
        {
            int? lastInstallerExitCode = null;

            try
            {
                PrerequisiteVerification visualCpp2010 = ProbeVisualCpp2010Runtime();
                LogVerification("VC++ 2010", visualCpp2010);
                if (!visualCpp2010.IsSatisfied)
                {
                    NativePrerequisiteResult installResult = DownloadInstallAndVerify(
                        "Microsoft Visual C++ 2010 SP1 Redistributable (x64)",
                        VisualCpp2010Url,
                        VisualCpp2010Sha256,
                        "/quiet /norestart",
                        visualCpp2010,
                        ProbeVisualCpp2010Runtime);
                    if (installResult.Status != NativePrerequisiteStatus.Ready)
                        return installResult;
                    lastInstallerExitCode = installResult.InstallerExitCode;
                }

                PrerequisiteVerification visualCppV14 = ProbeVisualCppV14Runtime();
                LogVerification("VC++ v14", visualCppV14);
                if (!visualCppV14.IsSatisfied)
                {
                    NativePrerequisiteResult installResult = DownloadInstallAndVerify(
                        "Microsoft Visual C++ v14 Redistributable (x64)",
                        VisualCppV14Url,
                        null,
                        "/quiet /norestart",
                        visualCppV14,
                        ProbeVisualCppV14Runtime);
                    if (installResult.Status != NativePrerequisiteStatus.Ready)
                        return installResult;
                    lastInstallerExitCode = installResult.InstallerExitCode;
                }

                PrerequisiteVerification legacyDirectX = ProbeLegacyDirectXRuntime();
                LogVerification("DirectX legacy", legacyDirectX);
                if (!legacyDirectX.IsSatisfied)
                {
                    NativePrerequisiteResult installResult = DownloadInstallAndVerify(
                        "Microsoft DirectX End-User Runtime (June 2010)",
                        DirectXLegacyUrl,
                        DirectXLegacySha256,
                        "/Q",
                        legacyDirectX,
                        ProbeLegacyDirectXRuntime);
                    if (installResult.Status != NativePrerequisiteStatus.Ready)
                        return installResult;
                    lastInstallerExitCode = installResult.InstallerExitCode;
                }

                return NativePrerequisiteResult.Ready(lastInstallerExitCode);
            }
            catch (Exception error)
            {
                Debug.WriteLine("[Prerequisite] Verification failed with " + error.GetType().Name + ".");
                return NativePrerequisiteResult.Failed(
                    "Could not verify the required Microsoft native runtimes."
                    + "\n\nObserved error type: " + error.GetType().Name + "."
                    + "\n\n" + DescribeOperatingSystem());
            }
        }

        private static PrerequisiteVerification ProbeVisualCpp2010Runtime()
        {
            return new PrerequisiteVerification(
                new[]
                {
                    ProbeRuntimeFile(SystemRuntimePath("msvcr100.dll"), VisualCpp2010MinimumVersion),
                    ProbeRuntimeFile(SystemRuntimePath("msvcp100.dll"), VisualCpp2010MinimumVersion)
                });
        }

        private static PrerequisiteVerification ProbeVisualCppV14Runtime()
        {
            return new PrerequisiteVerification(
                ProbeVisualCppV14RuntimeFiles(ProbeRuntimeFile));
        }

        internal static RuntimeFileProbe[] ProbeVisualCppV14RuntimeFiles(
            Func<string, Version, RuntimeFileProbe> runtimeProbe)
        {
            return new[]
            {
                runtimeProbe(SystemRuntimePath("vcruntime140.dll"), VisualCppV14MinimumVersion),
                runtimeProbe(SystemRuntimePath("vcruntime140_1.dll"), VisualCppV14MinimumVersion),
                runtimeProbe(SystemRuntimePath("msvcp140.dll"), VisualCppV14MinimumVersion)
            };
        }

        private static PrerequisiteVerification ProbeLegacyDirectXRuntime()
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
            RuntimeFileProbe[] probes = new RuntimeFileProbe[
                requiredFiles.Length * systemDirectories.Length];
            int probeIndex = 0;

            foreach (string directory in systemDirectories)
            {
                foreach (string fileName in requiredFiles)
                {
                    string path = string.IsNullOrEmpty(directory)
                        ? fileName
                        : Path.Combine(directory, fileName);
                    probes[probeIndex++] = ProbeRuntimeFile(path, null);
                }
            }

            return new PrerequisiteVerification(probes);
        }

        private static string SystemRuntimePath(string fileName)
        {
            return Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.System),
                fileName);
        }

        internal static RuntimeFileProbe ProbeRuntimeFile(string path, Version requiredVersion)
        {
            string checkedPath;
            try
            {
                checkedPath = Path.GetFullPath(path);
            }
            catch (Exception error)
            {
                return new RuntimeFileProbe(
                    path,
                    requiredVersion,
                    false,
                    null,
                    null,
                    error.GetType().Name);
            }

            try
            {
                File.GetAttributes(checkedPath);
            }
            catch (FileNotFoundException)
            {
                return new RuntimeFileProbe(checkedPath, requiredVersion, false, null, null, null);
            }
            catch (DirectoryNotFoundException)
            {
                return new RuntimeFileProbe(checkedPath, requiredVersion, false, null, null, null);
            }
            catch (Exception error)
            {
                return new RuntimeFileProbe(
                    checkedPath,
                    requiredVersion,
                    true,
                    null,
                    null,
                    error.GetType().Name);
            }

            if (requiredVersion == null)
                return new RuntimeFileProbe(checkedPath, null, true, null, null, null);

            try
            {
                string rawVersion = FileVersionInfo.GetVersionInfo(checkedPath).FileVersion;
                Version parsedVersion;
                if (!Version.TryParse(rawVersion, out parsedVersion))
                    parsedVersion = null;
                return new RuntimeFileProbe(
                    checkedPath,
                    requiredVersion,
                    true,
                    rawVersion,
                    parsedVersion,
                    null);
            }
            catch (Exception error)
            {
                return new RuntimeFileProbe(
                    checkedPath,
                    requiredVersion,
                    true,
                    null,
                    null,
                    error.GetType().Name);
            }
        }

        private static NativePrerequisiteResult DownloadInstallAndVerify(
            string displayName,
            string url,
            string expectedSha256,
            string installerArguments,
            PrerequisiteVerification beforeInstall,
            Func<PrerequisiteVerification> verifyInstalled)
        {
            string tempDirectory = Path.Combine(
                Path.GetTempPath(),
                "R1DeltaPrerequisite-" + Guid.NewGuid().ToString("N"));
            string installerPath = Path.Combine(tempDirectory, "installer.exe");
            int? installerExitCode = null;

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
                    return FailureWithFacts(
                        displayName + " failed its SHA-256 integrity check. The downloaded installer was not run.",
                        beforeInstall,
                        "before installer launch",
                        null);
                }

                if (!IsMicrosoftSigned(installerPath))
                {
                    return FailureWithFacts(
                        displayName + " did not have a valid Microsoft Authenticode signature. The downloaded installer was not run.",
                        beforeInstall,
                        "before installer launch",
                        null);
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
                        return FailureWithFacts(
                            displayName + " is required, but its administrator prompt was cancelled.",
                            beforeInstall,
                            "before installer launch",
                            null);
                    }

                    if (!process.WaitForExit(10 * 60 * 1000))
                    {
                        try { process.Kill(); } catch { }
                        return FailureWithFacts(
                            displayName + " did not finish within ten minutes.",
                            beforeInstall,
                            "before installer launch",
                            null);
                    }
                    installerExitCode = process.ExitCode;
                }

                PrerequisiteVerification afterInstall = verifyInstalled();
                LogVerification(displayName + " after installer exit", afterInstall);

                return EvaluateInstallerExit(
                    displayName,
                    installerExitCode.Value,
                    afterInstall.Probes,
                    DescribeOperatingSystem());
            }
            catch (Exception error)
            {
                Debug.WriteLine(
                    "[Prerequisite] Could not install " + displayName + ": "
                    + error.GetType().Name + ".");
                string exitObservation = installerExitCode.HasValue
                    ? "\nInstaller exit code: " + installerExitCode.Value + "."
                    : string.Empty;
                return FailureWithFacts(
                    "Could not install " + displayName + "."
                    + exitObservation
                    + "\nObserved error type: " + error.GetType().Name + ".",
                    beforeInstall,
                    "before installer launch",
                    installerExitCode);
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
                    Debug.WriteLine(
                        "[Prerequisite] Could not remove prerequisite temporary directory: "
                        + cleanupError.GetType().Name + ".");
                }
            }
        }

        internal static NativePrerequisiteResult EvaluateInstallerExit(
            string displayName,
            int installerExitCode,
            RuntimeFileProbe[] observedFiles,
            string operatingSystemDescription)
        {
            PrerequisiteVerification afterInstall =
                new PrerequisiteVerification(observedFiles);

            if (installerExitCode == 3010)
            {
                return NativePrerequisiteResult.RebootRequired(
                    displayName + " installer exited with code 3010 (restart required)."
                    + "\n\nObserved runtime files after installer exit:"
                    + "\n" + afterInstall.Describe()
                    + "\n\n" + operatingSystemDescription,
                    installerExitCode);
            }

            if (installerExitCode != 0 && installerExitCode != 1638)
            {
                return FailureWithFacts(
                    displayName + " installation failed with exit code "
                    + installerExitCode + ".",
                    afterInstall,
                    "after installer exit",
                    installerExitCode,
                    operatingSystemDescription);
            }

            if (!afterInstall.IsSatisfied)
            {
                return FailureWithFacts(
                    displayName + " installer exited with code " + installerExitCode
                    + ", but prerequisite verification did not pass.",
                    afterInstall,
                    "after installer exit",
                    installerExitCode,
                    operatingSystemDescription);
            }

            return NativePrerequisiteResult.Ready(installerExitCode);
        }

        private static NativePrerequisiteResult FailureWithFacts(
            string message,
            PrerequisiteVerification verification,
            string observationTime,
            int? installerExitCode,
            string operatingSystemDescription = null)
        {
            return NativePrerequisiteResult.Failed(
                message
                + "\n\nObserved runtime files " + observationTime + ":"
                + "\n" + verification.Describe()
                + "\n\n" + (operatingSystemDescription ?? DescribeOperatingSystem()),
                installerExitCode);
        }

        internal static string DescribeOperatingSystem()
        {
            string versionDescription;
            try
            {
                RtlOsVersionInfoEx version = new RtlOsVersionInfoEx
                {
                    Size = (uint)Marshal.SizeOf(typeof(RtlOsVersionInfoEx))
                };
                int status = RtlGetVersion(ref version);
                if (status == 0)
                {
                    versionDescription =
                        $"Microsoft Windows NT {version.MajorVersion}.{version.MinorVersion}.{version.BuildNumber}";
                    if (!string.IsNullOrWhiteSpace(version.ServicePack))
                        versionDescription += " " + version.ServicePack.Trim();
                }
                else
                {
                    versionDescription = $"unavailable (RtlGetVersion NTSTATUS 0x{status:X8})";
                }
            }
            catch (Exception error) when (
                error is DllNotFoundException ||
                error is EntryPointNotFoundException ||
                error is BadImageFormatException)
            {
                versionDescription = $"unavailable ({error.GetType().Name})";
            }

            return "Operating system version: " + versionDescription
                + "\nOperating system architecture: "
                + (Environment.Is64BitOperatingSystem ? "64-bit" : "32-bit")
                + "\nLauncher process architecture: "
                + (Environment.Is64BitProcess ? "64-bit" : "32-bit");
        }

        private static void LogVerification(
            string displayName,
            PrerequisiteVerification verification)
        {
            Debug.WriteLine(
                "[Prerequisite] " + displayName + " verification:"
                + Environment.NewLine + verification.Describe());
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
