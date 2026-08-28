// TitanfallManager.cs
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices; // For P/Invokes if ever needed again
using System.Threading;
using System.Windows;                // For MessageBox – consider abstracting UI interactions
using System.Threading.Tasks;
using K4os.Hash.xxHash;
using launcher_ex;                   // For IInstallProgress, SetupWindow
// using Monitor.Core.Utilities;     // Not used directly here
using System.Reflection;
using Microsoft.Win32;
using Dark.Net;


namespace R1Delta
{
    /// <summary>
    /// Helper class for Registry operations and persisting launcher settings.
    /// </summary>
    internal static class RegistryHelper
    {
        private const string RegistryBaseKey = @"Software\R1Delta";
        private const string InstallPathValueName = "InstallPath";
        private const string ShowSetupOnLaunchValueName = "ShowSetupOnLaunch";
        private const string LaunchArgumentsValueName = "LaunchArguments";
        internal const int PrerequisiteWarningClaimVersion = 1;
        private const string PrerequisiteWarningClaimValueName =
            "PrerequisiteWarningClaimVersion";
        private const string PrerequisiteWarningClaimMutexNamePrefix =
            @"Global\R1Delta.PrerequisiteWarningClaim.";
        private const int PrerequisiteWarningClaimMutexTimeoutMilliseconds = 2000;

        public static string GetInstallPath()
        {
            try
            {
                using var key = Registry.CurrentUser.OpenSubKey(RegistryBaseKey);
                if (key != null)
                    return key.GetValue(InstallPathValueName) as string;
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Error reading registry key {RegistryBaseKey}\\{InstallPathValueName}: {ex.Message}");
            }
            return null;
        }

        public static void SaveInstallPath(string path)
        {
            if (string.IsNullOrWhiteSpace(path)) return;
            try
            {
                using var key = Registry.CurrentUser.CreateSubKey(RegistryBaseKey);
                if (key != null)
                {
                    key.SetValue(InstallPathValueName, path, RegistryValueKind.String);
                    Debug.WriteLine($"Saved registry value: HKCU\\{RegistryBaseKey}\\{InstallPathValueName} = {path}");
                }
                else
                {
                    Debug.WriteLine($"Error: Could not open or create HKCU\\{RegistryBaseKey}");
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Error writing registry key {RegistryBaseKey}\\{InstallPathValueName}: {ex.Message}");
                MessageBox.Show(
                    $"Warning: Could not save the installation path to the registry.\n" +
                    $"The game might ask for the location again on next launch.\n\nError: {ex.Message}",
                    "Registry Warning",
                    MessageBoxButton.OK,
                    MessageBoxImage.Warning
                );
            }
        }

        public static bool GetShowSetupOnLaunch()
        {
            try
            {
                using var key = Registry.CurrentUser.OpenSubKey(RegistryBaseKey);
                if (key != null)
                {
                    var value = key.GetValue(ShowSetupOnLaunchValueName);
                    if (value is int intVal)
                        return intVal != 0;
                    if (value is string strVal && bool.TryParse(strVal, out var boolVal))
                        return boolVal;
                    if (value != null)
                        Debug.WriteLine($"Warning: {ShowSetupOnLaunchValueName} has unexpected type {value.GetType()}; defaulting to false.");
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Error reading registry key {RegistryBaseKey}\\{ShowSetupOnLaunchValueName}: {ex.Message}");
            }
            // Default: show setup
            return false;
        }

        public static void SaveShowSetupOnLaunch(bool show)
        {
            try
            {
                using var key = Registry.CurrentUser.CreateSubKey(RegistryBaseKey);
                if (key != null)
                {
                    key.SetValue(ShowSetupOnLaunchValueName, show ? 1 : 0, RegistryValueKind.DWord);
                    Debug.WriteLine($"Saved registry value: HKCU\\{RegistryBaseKey}\\{ShowSetupOnLaunchValueName} = {show}");
                }
                else
                {
                    Debug.WriteLine($"Error: Could not open or create HKCU\\{RegistryBaseKey}");
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Error writing registry key {RegistryBaseKey}\\{ShowSetupOnLaunchValueName}: {ex.Message}");
            }
        }

        public static string GetLaunchArguments()
        {
            try
            {
                using var key = Registry.CurrentUser.OpenSubKey(RegistryBaseKey);
                if (key != null)
                    return (key.GetValue(LaunchArgumentsValueName) as string) ?? string.Empty;
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Error reading registry key {RegistryBaseKey}\\{LaunchArgumentsValueName}: {ex.Message}");
            }
            return string.Empty;
        }

        public static void SaveLaunchArguments(string args)
        {
            var toSave = args ?? string.Empty;
            try
            {
                using var key = Registry.CurrentUser.CreateSubKey(RegistryBaseKey);
                if (key != null)
                {
                    key.SetValue(LaunchArgumentsValueName, toSave, RegistryValueKind.String);
                    Debug.WriteLine($"Saved registry value: HKCU\\{RegistryBaseKey}\\{LaunchArgumentsValueName} = \"{toSave}\"");
                }
                else
                {
                    Debug.WriteLine($"Error: Could not open or create HKCU\\{RegistryBaseKey}");
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Error writing registry key {RegistryBaseKey}\\{LaunchArgumentsValueName}: {ex.Message}");
            }
        }

        internal static bool TryClaimPrerequisiteWarning()
        {
            Mutex claimMutex = null;
            bool mutexHeld = false;

            try
            {
                using var identity = System.Security.Principal.WindowsIdentity.GetCurrent();
                string userSid = identity?.User?.Value;
                if (string.IsNullOrWhiteSpace(userSid))
                {
                    Debug.WriteLine(
                        "Could not claim the native prerequisite warning because the current user SID is unavailable.");
                    return false;
                }
                claimMutex = new Mutex(
                    false,
                    BuildPrerequisiteWarningClaimMutexName(userSid));
                try
                {
                    mutexHeld = claimMutex.WaitOne(
                        PrerequisiteWarningClaimMutexTimeoutMilliseconds);
                }
                catch (AbandonedMutexException)
                {
                    mutexHeld = true;
                }

                if (!mutexHeld)
                {
                    Debug.WriteLine(
                        "Could not claim the native prerequisite warning before the mutex timeout; it will not be displayed.");
                    return false;
                }

                using var key = Registry.CurrentUser.CreateSubKey(RegistryBaseKey);
                if (key == null)
                {
                    Debug.WriteLine(
                        $"Could not claim the native prerequisite warning: HKCU\\{RegistryBaseKey} could not be opened or created.");
                    return false;
                }

                bool writeAttempted = false;
                bool claimed = TryClaimOneTimeWarning(
                    PrerequisiteWarningClaimVersion,
                    () =>
                    {
                        object value = key.GetValue(PrerequisiteWarningClaimValueName);
                        return value is int version ? version : 0;
                    },
                    version =>
                    {
                        writeAttempted = true;
                        key.SetValue(
                            PrerequisiteWarningClaimValueName,
                            version,
                            RegistryValueKind.DWord);
                    });
                if (!claimed && writeAttempted)
                {
                    Debug.WriteLine(
                        "Could not claim the native prerequisite warning; the persisted version did not match.");
                }
                return claimed;
            }
            catch (Exception ex)
            {
                Debug.WriteLine(
                    $"Could not claim the native prerequisite warning; it will not be displayed: {ex.Message}");
                return false;
            }
            finally
            {
                if (mutexHeld)
                {
                    try
                    {
                        claimMutex.ReleaseMutex();
                    }
                    catch (ApplicationException)
                    {
                    }
                }
                claimMutex?.Dispose();
            }
        }

        internal static string BuildPrerequisiteWarningClaimMutexName(string userSid)
        {
            if (string.IsNullOrWhiteSpace(userSid))
                throw new ArgumentException("A user SID is required.", nameof(userSid));

            return PrerequisiteWarningClaimMutexNamePrefix + userSid;
        }

        internal static bool TryClaimOneTimeWarning(
            int claimVersion,
            Func<int> readClaimVersion,
            Action<int> writeClaimVersion)
        {
            if (readClaimVersion == null)
                throw new ArgumentNullException(nameof(readClaimVersion));
            if (writeClaimVersion == null)
                throw new ArgumentNullException(nameof(writeClaimVersion));

            if (readClaimVersion() >= claimVersion)
                return false;

            writeClaimVersion(claimVersion);
            return readClaimVersion() >= claimVersion;
        }
    }

    /// <summary>
    /// Manages detection, validation, and download of Titanfall game files.
    /// </summary>
    public static class TitanfallManager
    {
        internal const string ValidationFileRelativePath = @"vpk\client_mp_common.bsp.pak000_000.vpk";
        private const int MaxVerificationDownloadPasses = 3;
        internal const string ManagedInstallMarkerRelativePath = ".r1delta-managed-install";
        internal const string ManagedInstallMarkerMagic = "R1DELTA_MANAGED_INSTALL_V1\r\n";
        internal const string LegacyCompletedInstallVpkRelativePath = @"vpk\client_mp_delta_common.bsp.pak000_000.vpk";
        private static readonly byte[] ManagedInstallMarkerBytes =
            new System.Text.UTF8Encoding(false, true).GetBytes(ManagedInstallMarkerMagic);

        internal readonly struct GameRootResolution
        {
            internal GameRootResolution(
                bool succeeded,
                bool isUsableDestination,
                string resolvedRoot,
                string selectedCandidate,
                string childCandidate,
                string message)
            {
                Succeeded = succeeded;
                IsUsableDestination = isUsableDestination;
                ResolvedRoot = resolvedRoot;
                SelectedCandidate = selectedCandidate;
                ChildCandidate = childCandidate;
                Message = message;
            }

            internal bool Succeeded { get; }
            internal bool IsUsableDestination { get; }
            internal string ResolvedRoot { get; }
            internal string SelectedCandidate { get; }
            internal string ChildCandidate { get; }
            internal string Message { get; }
        }

        /// <summary>
        /// Resolves a selected directory to either that exact game root or its direct r1delta child.
        /// No other descendants are considered.
        /// </summary>
        internal static GameRootResolution ResolveGameRoot(string selectedPath)
        {
            if (string.IsNullOrWhiteSpace(selectedPath))
            {
                return new GameRootResolution(
                    false,
                    false,
                    null,
                    selectedPath,
                    null,
                    $"The selected path is empty. Required marker: '{ValidationFileRelativePath}'. " +
                    "No root candidates could be tested.");
            }
            string rawChildCandidate =
                selectedPath.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar) +
                Path.DirectorySeparatorChar +
                "r1delta";


            string selectedCandidate;
            string childCandidate;
            string selectedMarker;
            string childMarker;
            try
            {
                selectedCandidate = Path.GetFullPath(selectedPath);
                string selectedPathRoot = Path.GetPathRoot(selectedCandidate);
                if (!string.Equals(selectedCandidate, selectedPathRoot, StringComparison.OrdinalIgnoreCase))
                {
                    selectedCandidate = selectedCandidate.TrimEnd(
                        Path.DirectorySeparatorChar,
                        Path.AltDirectorySeparatorChar);
                }
                childCandidate = Path.Combine(selectedCandidate, "r1delta");
                selectedMarker = Path.Combine(selectedCandidate, ValidationFileRelativePath);
                childMarker = Path.Combine(childCandidate, ValidationFileRelativePath);
            }
            catch (Exception ex) when (
                ex is NotSupportedException ||
                ex is System.Security.SecurityException ||
                ex is ArgumentException ||
                ex is PathTooLongException ||
                ex is IOException)
            {
                return new GameRootResolution(
                    false,
                    false,
                    null,
                    selectedPath,
                    rawChildCandidate,
                    $"The selected path '{selectedPath}' is invalid: {ex.Message} " +
                    $"Required marker: '{ValidationFileRelativePath}'. " +
                    $"Candidates could not be tested: '{selectedPath}' and '{rawChildCandidate}'.");
            }

            return ResolveGameRootCandidates(
                selectedCandidate,
                childCandidate,
                selectedMarker,
                childMarker,
                File.Exists(selectedMarker),
                File.Exists(childMarker));
        }

        /// <summary>
        /// Pure candidate decision: the same normalized candidates and marker observations always produce the same result.
        /// </summary>
        internal static GameRootResolution ResolveGameRootCandidates(
            string selectedCandidate,
            string childCandidate,
            string selectedMarker,
            string childMarker,
            bool selectedIsValid,
            bool childIsValid)
        {
            if (selectedIsValid && childIsValid)
            {
                return new GameRootResolution(
                    false,
                    false,
                    null,
                    selectedCandidate,
                    childCandidate,
                    $"The selected location is ambiguous because both candidates contain the required marker " +
                    $"'{ValidationFileRelativePath}'. Tested candidates: '{selectedCandidate}' and '{childCandidate}'.");
            }

            if (selectedIsValid)
            {
                return new GameRootResolution(
                    true,
                    true,
                    selectedCandidate,
                    selectedCandidate,
                    childCandidate,
                    $"Resolved game root '{selectedCandidate}' using marker '{selectedMarker}'.");
            }

            if (childIsValid)
            {
                return new GameRootResolution(
                    true,
                    true,
                    childCandidate,
                    selectedCandidate,
                    childCandidate,
                    $"Resolved game root '{childCandidate}' using marker '{childMarker}'.");
            }

            return new GameRootResolution(
                false,
                true,
                selectedCandidate,
                selectedCandidate,
                childCandidate,
                $"The required marker '{ValidationFileRelativePath}' was not observed at either candidate. " +
                $"Using '{selectedCandidate}' as a new or partial installation destination. " +
                $"Tested candidates: '{selectedCandidate}' (marker '{selectedMarker}') and " +
                $"'{childCandidate}' (marker '{childMarker}').");
        }

        /// <summary>
        /// Tries to locate an existing valid Titanfall directory via registry or custom finder.
        /// </summary>
        internal static string TryFindExistingValidPath()
        {
            var registryResolution = ResolveGameRoot(RegistryHelper.GetInstallPath());
            if (registryResolution.Succeeded)
            {
                Debug.WriteLine($"[TryFindExistingValidPath] Found via registry: {registryResolution.ResolvedRoot}");
                return registryResolution.ResolvedRoot;
            }

            var exePath = TitanfallFinder.TitanfallLocator.FindTitanfallOrR1Delta();
            string finderDir = null;
            if (!string.IsNullOrEmpty(exePath))
            {
                try
                {
                    finderDir = Path.GetDirectoryName(exePath);
                }
                catch (Exception ex)
                {
                    Debug.WriteLine($"[TryFindExistingValidPath] Error parsing finder path '{exePath}': {ex.Message}");
                }
            }

            var finderResolution = ResolveGameRoot(finderDir);
            if (finderResolution.Succeeded)
            {
                Debug.WriteLine($"[TryFindExistingValidPath] Found via finder: {finderResolution.ResolvedRoot}");
                return finderResolution.ResolvedRoot;
            }

            Debug.WriteLine("[TryFindExistingValidPath] No valid path found.");
            return null;
        }


        internal static bool TryNormalizeInstallRootPath(
            string installDir,
            out string normalizedRoot,
            out string error)
        {
            normalizedRoot = null;
            error = null;
            if (string.IsNullOrWhiteSpace(installDir))
            {
                error = "The installation directory is empty.";
                return false;
            }

            try
            {
                normalizedRoot = Path.GetFullPath(installDir);
                string pathRoot = Path.GetPathRoot(normalizedRoot);
                if (!string.Equals(normalizedRoot, pathRoot, StringComparison.OrdinalIgnoreCase))
                {
                    normalizedRoot = normalizedRoot.TrimEnd(
                        Path.DirectorySeparatorChar,
                        Path.AltDirectorySeparatorChar);
                }
                return true;
            }
            catch (Exception ex)
            {
                error = $"The installation directory '{installDir}' is invalid: {ex.Message}";
                return false;
            }
        }

        private sealed class InstallOperationLease : IDisposable
        {
            private FileStream _lockStream;

            internal InstallOperationLease(FileStream lockStream)
            {
                _lockStream = lockStream;
            }

            public void Dispose()
            {
                Interlocked.Exchange(ref _lockStream, null)?.Dispose();
            }
        }

        internal static bool TryAcquireInstallOperationLease(
            string installDir,
            out IDisposable operationLease,
            out string error)
        {
            operationLease = null;
            if (!TryNormalizeInstallRootPath(installDir, out string normalizedRoot, out error))
                return false;

            try
            {
                Directory.CreateDirectory(normalizedRoot);
                string lockPath = Path.Combine(normalizedRoot, ".r1delta-install-operation.lock");

                FileStream lockStream;
                try
                {
                    lockStream = new FileStream(
                        lockPath,
                        FileMode.OpenOrCreate,
                        FileAccess.ReadWrite,
                        FileShare.None,
                        4096,
                        FileOptions.DeleteOnClose);
                }
                catch (IOException ex)
                {
                    error =
                        $"Another setup, update, or uninstall operation may already be using '{normalizedRoot}'. " +
                        $"Wait for it to finish and try again. ({ex.Message})";
                    return false;
                }

                try
                {
                    lockStream.SetLength(0);
                    byte[] ownerBytes = System.Text.Encoding.UTF8.GetBytes(
                        $"root={normalizedRoot}\r\nprocess={Process.GetCurrentProcess().Id}\r\n");
                    lockStream.Write(ownerBytes, 0, ownerBytes.Length);
                    lockStream.Flush(true);
                    lockStream.Position = 0;
                    operationLease = new InstallOperationLease(lockStream);
                    return true;
                }
                catch
                {
                    lockStream.Dispose();
                    throw;
                }
            }
            catch (Exception ex)
            {
                error = $"Could not acquire the install-operation lease for '{normalizedRoot}': {ex.Message}";
                return false;
            }
        }


        internal static bool TryGetManagedInstallMarkerPath(
            string installDir,
            out string markerPath,
            out string error)
        {
            markerPath = null;
            if (!TryNormalizeInstallRootPath(installDir, out string normalizedRoot, out error))
                return false;

            try
            {
                markerPath = Path.Combine(normalizedRoot, ManagedInstallMarkerRelativePath);
                return true;
            }
            catch (Exception ex)
            {
                error = $"Could not construct the managed-install marker path for '{normalizedRoot}': {ex.Message}";
                return false;
            }
        }

        internal static bool IsManagedInstallMarkerContent(string content)
        {
            return string.Equals(content, ManagedInstallMarkerMagic, StringComparison.Ordinal);
        }

        internal static bool HasValidManagedInstallOwnership(string installDir)
        {
            if (!TryGetManagedInstallMarkerPath(installDir, out string markerPath, out string error))
            {
                Debug.WriteLine($"[ManagedInstall] Ownership validation failed: {error}");
                return false;
            }

            try
            {
                return HasExactManagedInstallMarkerAtPath(markerPath);
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[ManagedInstall] Could not validate marker '{markerPath}': {ex.Message}");
                return false;
            }
        }

        internal static bool TryEnsureManagedInstallOwnership(string installDir, out string error)
        {
            error = null;
            if (!TryNormalizeInstallRootPath(installDir, out string normalizedRoot, out error))
                return false;

            string markerPath;
            try
            {
                markerPath = Path.Combine(normalizedRoot, ManagedInstallMarkerRelativePath);
                Directory.CreateDirectory(normalizedRoot);
            }
            catch (Exception ex)
            {
                error = $"Could not prepare the managed-install marker in '{normalizedRoot}': {ex.Message}";
                return false;
            }

            if (File.Exists(markerPath))
            {
                if (HasValidManagedInstallOwnership(normalizedRoot))
                    return true;

                error = $"The existing managed-install marker '{markerPath}' does not contain the expected R1Delta ownership value.";
                return false;
            }

            string temporaryPath = markerPath + ".tmp." + Guid.NewGuid().ToString("N");
            try
            {
                using (var stream = new FileStream(
                    temporaryPath,
                    FileMode.CreateNew,
                    FileAccess.Write,
                    FileShare.None,
                    4096,
                    FileOptions.WriteThrough))
                {
                    stream.Write(ManagedInstallMarkerBytes, 0, ManagedInstallMarkerBytes.Length);
                    stream.Flush(true);
                }

                try
                {
                    File.Move(temporaryPath, markerPath);
                }
                catch (IOException) when (HasValidManagedInstallOwnership(normalizedRoot))
                {
                    // Another setup operation atomically installed the same valid marker.
                }

                if (!HasValidManagedInstallOwnership(normalizedRoot))
                {
                    error = $"The managed-install marker '{markerPath}' could not be verified after creation.";
                    return false;
                }

                Debug.WriteLine($"[ManagedInstall] Durable ownership marker ready: {markerPath}");
                return true;
            }
            catch (Exception ex)
            {
                error = $"Could not create the managed-install marker '{markerPath}': {ex.Message}";
                return false;
            }
            finally
            {
                try
                {
                    if (File.Exists(temporaryPath))
                        File.Delete(temporaryPath);
                }
                catch (Exception ex)
                {
                    Debug.WriteLine($"[ManagedInstall] Could not remove temporary marker '{temporaryPath}': {ex.Message}");
                }
            }
        }

        internal static bool TryRemoveManagedInstallOwnership(string installDir, out string error)
        {
            error = null;
            if (!TryGetManagedInstallMarkerPath(installDir, out string markerPath, out error))
                return false;

            try
            {
                if (!File.Exists(markerPath))
                    return true;

                if (!HasExactManagedInstallMarkerAtPath(markerPath))
                {
                    error = $"Refusing to remove malformed managed-install marker '{markerPath}'.";
                    return false;
                }

                File.Delete(markerPath);
                if (File.Exists(markerPath))
                {
                    error = $"The managed-install marker '{markerPath}' still exists after deletion.";
                    return false;
                }

                Debug.WriteLine($"[ManagedInstall] Removed ownership marker: {markerPath}");
                return true;
            }
            catch (Exception ex)
            {
                error = $"Could not remove the managed-install marker '{markerPath}': {ex.Message}";
                return false;
            }
        }

        internal static bool HasLegacyCompletedInstallProof(string installDir)
        {
            if (!TryNormalizeInstallRootPath(installDir, out string normalizedRoot, out string error))
            {
                Debug.WriteLine($"[ManagedInstall] Legacy proof validation failed: {error}");
                return false;
            }

            try
            {
                return File.Exists(Path.Combine(normalizedRoot, LegacyCompletedInstallVpkRelativePath));
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[ManagedInstall] Could not validate legacy completion proof in '{normalizedRoot}': {ex.Message}");
                return false;
            }
        }

        internal static bool IsManagedCleanupAuthorized(bool hasValidMarker, bool hasLegacyCompletedInstallProof)
        {
            return hasValidMarker || hasLegacyCompletedInstallProof;
        }

        private static bool HasExactManagedInstallMarkerAtPath(string markerPath)
        {
            using (var stream = new FileStream(
                markerPath,
                FileMode.Open,
                FileAccess.Read,
                FileShare.Read,
                ManagedInstallMarkerBytes.Length,
                FileOptions.SequentialScan))
            {
                if (stream.Length != ManagedInstallMarkerBytes.Length)
                    return false;

                for (int index = 0; index < ManagedInstallMarkerBytes.Length; index++)
                {
                    if (stream.ReadByte() != ManagedInstallMarkerBytes[index])
                        return false;
                }
                return stream.ReadByte() == -1;
            }
        }

        /// <summary>
        /// Creates an empty placeholder VPK if the real one is missing.
        /// </summary>
        private static void EnsurePlaceholderVpkExists(string installDir)
        {
            var placeholder = Path.Combine(installDir, ValidationFileRelativePath);
            if (File.Exists(placeholder)) return;

            try
            {
                var dir = Path.GetDirectoryName(placeholder);
                if (!string.IsNullOrEmpty(dir))
                {
                    Directory.CreateDirectory(dir);
                    using var f = File.Create(placeholder);
                    Debug.WriteLine($"Created placeholder: {placeholder}");
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Warning: Could not create placeholder VPK '{placeholder}': {ex.Message}");
            }
        }

        /// <summary>
        /// Downloads all files in the manifest, resuming via FastDownloadService and reporting progress.
        /// </summary>
        public static async Task<bool> DownloadAllFilesWithResume(
            string installDir,
            IInstallProgress progressUI,
            CancellationToken externalCts)
        {
            if (progressUI == null)
            {
                Debug.WriteLine("Error: progressUI is null.");
                return false;
            }

            // Refuse to hand an unowned destination to a downloader. Setup creates
            // and durably verifies the marker only after the user confirms.
            if (!TryNormalizeInstallRootPath(installDir, out string normalizedInstallDir, out string pathError))
            {
                progressUI.ShowError($"Internal Error: {pathError}");
                return false;
            }
            installDir = normalizedInstallDir;

            if (!HasValidManagedInstallOwnership(installDir))
            {
                progressUI.ShowError(
                    $"Setup cannot download files because the R1Delta managed-install marker is missing or malformed in '{installDir}'.");
                return false;
            }

            if (!Directory.Exists(installDir))
            {
                 // It might be created later, but let's ensure the base exists for verification/cleanup
                 try { Directory.CreateDirectory(installDir); } catch (Exception ex) {
                     progressUI.ShowError($"Internal Error: Could not create installation directory '{installDir}': {ex.Message}");
                     return false;
                 }
            }


            // Dictionary to track bytes received per file path. Crucial for aggregate progress.
            var fileReceivedBytes = new Dictionary<string, long>(StringComparer.OrdinalIgnoreCase);
            var fileTotalBytes = new Dictionary<string, long>(StringComparer.OrdinalIgnoreCase);
            var toDownload = new List<(string Url, string Dest, ulong Hash, long Size)>();
            var history = new Queue<(double Time, long Progress)>();
            const double rollingWindow = 5.0;
            double lastUpdate = -1;
            object progressLock = new object();
            long overallProgress = 0;
            var totalManifestBytes = TitanfallFileList.s_fileList.Sum(file => file.Size);
            var statusProgress = progressUI as IInstallProgressStatus;
            statusProgress?.ReportStatus(new InstallProgressStatus
            {
                Phase = InstallProgressPhase.Preflight
            });
            progressUI.ReportProgress(0, totalManifestBytes, 0.0);

            Debug.WriteLine($"Verifying existing files in: {installDir}");
            try
            {
                string verificationError = null;
                await Task.Run(() =>
                {
                    foreach (var (url, relPath, expectedHash, knownSize) in TitanfallFileList.s_fileList)
                    {
                        externalCts.ThrowIfCancellationRequested();
                        if (string.IsNullOrWhiteSpace(relPath))
                        {
                            Debug.WriteLine("Warning: Empty relative path.");
                            continue;
                        }

                        var dest = Path.Combine(installDir, relPath);
                        var dir = Path.GetDirectoryName(dest);
                        if (string.IsNullOrEmpty(dir))
                        {
                            verificationError = "Internal Error: Could not determine directory.";
                            return;
                        }
                        Directory.CreateDirectory(dir);

                        bool needs = true;
                        long currentSize = 0;
                        if (File.Exists(dest))
                        {
                            try
                            {
                                var fi = new FileInfo(dest);
                                currentSize = fi.Length;
                                if (fi.Length == knownSize)
                                {
                                    if (knownSize == 0 || ComputeXxHash64(dest, externalCts) == expectedHash)
                                    {
                                        needs = false;
                                    }
                                    else
                                    {
                                        Debug.WriteLine($"Checksum mismatch, restarting cleanly: {relPath}");
                                        DeleteDownloadArtifacts(dest);
                                        currentSize = 0;
                                    }
                                }
                                else if (fi.Length > knownSize)
                                {
                                    Debug.WriteLine($"Oversized file, restarting cleanly: {relPath} (Expected: {knownSize}, Got: {fi.Length})");
                                    DeleteDownloadArtifacts(dest);
                                    currentSize = 0;
                                }
                                else
                                {
                                    Debug.WriteLine($"Resuming partial file: {relPath} ({fi.Length} / {knownSize})");
                                }
                            }
                            catch (OperationCanceledException)
                            {
                                throw;
                            }
                            catch (Exception ex)
                            {
                                Debug.WriteLine($"Unable to validate existing file, restarting cleanly: {dest}: {ex.Message}");
                                DeleteDownloadArtifacts(dest);
                                currentSize = 0;
                            }
                        }
                        else
                        {
                            DeleteFileIfExists(dest + ".aria2");
                        }

                        fileTotalBytes[dest] = knownSize;
                        // Initialize received bytes: Use actual current size if file exists, otherwise 0.
                        // This makes the initial progress reflect resumable on-disk data.
                        fileReceivedBytes[dest] = currentSize;
                        if (needs)
                            toDownload.Add((url, dest, expectedHash, knownSize));
                    }
                }, externalCts).ConfigureAwait(false);

                if (verificationError != null)
                {
                    progressUI.ShowError(verificationError);
                    return false;
                }
            }
            catch (OperationCanceledException)
            {
                Debug.WriteLine("Verification cancelled.");
                if (!externalCts.IsCancellationRequested)
                    progressUI.ShowError("Operation Cancelled");
                return false;
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Fatal error during verification: {ex}");
                progressUI.ShowError($"Error during file check: {ex.Message}");
                return false;
            }

            var totalNeeded = fileTotalBytes.Values.Sum();
            // Calculate initial overall progress by summing the initial state of fileReceivedBytes
            // Clamp initial progress to ensure it doesn't exceed totalNeeded due to oversized existing files
            overallProgress = Clamp(fileReceivedBytes.Values.Sum(), 0, totalNeeded);

            if (!toDownload.Any())
            {
                Debug.WriteLine("All files present and verified.");
                statusProgress?.ReportStatus(new InstallProgressStatus
                {
                    Phase = InstallProgressPhase.Complete
                });
                progressUI.ReportProgress(totalNeeded, totalNeeded, 0.0);
                EnsurePlaceholderVpkExists(installDir);
                return true;
            }

            Debug.WriteLine($"{toDownload.Count} files to download/resume.");

            var stopwatch = Stopwatch.StartNew();
            using var linked = CancellationTokenSource.CreateLinkedTokenSource(externalCts);
            var token = linked.Token;
            var downloadedFilesTotal = toDownload.Sum(item => item.Size);
            var validationProgress = 0L;
            var validatedFileBytes = new Dictionary<string, long>(StringComparer.OrdinalIgnoreCase);
            var validationWindow = Math.Max(1L, totalNeeded / 100L);
            var downloadCap = Math.Max(0L, totalNeeded - validationWindow);
            var onePercentCeiling = totalNeeded / 100L + (totalNeeded % 100L == 0 ? 0L : 1L);
            var preVerificationProgressCap = Math.Max(0L, totalNeeded - Math.Max(1L, onePercentCeiling));

            var currentProgressPhase = InstallProgressPhase.Resume;
            DownloadBackend? currentBackend = null;
            var currentTransferPhase = DownloadTransferPhase.Preparing;
            var currentAttempt = 0;
            var phaseMaxReportedProgress = -1L;

            long ProjectProgress(long downloadedBytes, long validatedBytes)
            {
                if (downloadedFilesTotal <= 0)
                    return Clamp(downloadedBytes, 0, totalNeeded);

                var cappedDownload = Clamp(downloadedBytes, 0, downloadCap);
                var validationContribution = Clamp((validationWindow * validatedBytes) / downloadedFilesTotal, 0, validationWindow);
                return Clamp(cappedDownload + validationContribution, 0, totalNeeded);
            }

            void ReportAggregateProgress(long rawOverallProgress, double speed)
            {
                var projectedProgress = ProjectProgress(rawOverallProgress, validationProgress);
                if (currentProgressPhase != InstallProgressPhase.Verification)
                    projectedProgress = Math.Min(projectedProgress, preVerificationProgressCap);
                phaseMaxReportedProgress = phaseMaxReportedProgress < 0
                    ? projectedProgress
                    : Math.Max(phaseMaxReportedProgress, projectedProgress);
                progressUI.ReportProgress(phaseMaxReportedProgress, totalNeeded, Math.Max(0, speed));
                lastUpdate = stopwatch.Elapsed.TotalSeconds;
            }

            void BeginProgressPhase(
                InstallProgressPhase phase,
                DownloadBackend? backend = null,
                DownloadTransferPhase transferPhase = DownloadTransferPhase.Preparing,
                int attempt = 0,
                int maxAttempts = 0,
                int verificationPass = 0)
            {
                currentProgressPhase = phase;
                currentBackend = backend;
                currentTransferPhase = transferPhase;
                currentAttempt = attempt;
                phaseMaxReportedProgress = -1;
                lastUpdate = -1;
                history.Clear();
                statusProgress?.ReportStatus(new InstallProgressStatus
                {
                    Phase = phase,
                    Backend = backend,
                    TransferPhase = transferPhase,
                    Attempt = attempt,
                    MaxAttempts = maxAttempts,
                    VerificationPass = verificationPass,
                    MaxVerificationPasses = MaxVerificationDownloadPasses
                });
                ReportAggregateProgress(overallProgress, 0);
            }

            BeginProgressPhase(InstallProgressPhase.Resume);

            void RecordSpeedSample(long rawOverallProgress)
            {
                var now = stopwatch.Elapsed.TotalSeconds;
                history.Enqueue((Time: now, Progress: rawOverallProgress));
                while (history.Count > 1 && history.Peek().Time < now - rollingWindow)
                    history.Dequeue();
            }

            double CalculateSpeed()
            {
                if (history.Count <= 1)
                    return 0;

                (double t0, long p0) = history.Peek();
                var dt = stopwatch.Elapsed.TotalSeconds - t0;
                var dp = overallProgress - p0;
                return dt > 0.01 ? dp / dt : 0;
            }

            try
            {
                using var dl = new FastDownloadService(installDir);
                dl.DownloadProgressChanged += update =>
                {
                    if (token.IsCancellationRequested) return;

                    lock (progressLock)
                    {
                        var aggregatePhase = update.Backend == DownloadBackend.Curl
                            ? InstallProgressPhase.Download
                            : InstallProgressPhase.Fallback;
                        if (aggregatePhase != currentProgressPhase ||
                            update.Backend != currentBackend ||
                            update.Phase != currentTransferPhase ||
                            update.Attempt != currentAttempt)
                        {
                            BeginProgressPhase(
                                aggregatePhase,
                                update.Backend,
                                update.Phase,
                                update.Attempt,
                                update.MaxAttempts);
                        }

                        var expectedSize = fileTotalBytes.TryGetValue(update.DestinationPath, out var knownSize)
                            ? knownSize
                            : Math.Max(update.TotalBytes, update.BytesReceived);
                        fileReceivedBytes[update.DestinationPath] = Clamp(update.BytesReceived, 0, expectedSize);
                        overallProgress = Clamp(fileReceivedBytes.Values.Sum(), 0, totalNeeded);
                        RecordSpeedSample(overallProgress);

                        var now = stopwatch.Elapsed.TotalSeconds;
                        if (now - lastUpdate >= 0.5)
                            ReportAggregateProgress(overallProgress, CalculateSpeed());
                    }
                };

                var pendingDownloads = toDownload.ToList();
                var completedVerificationPass = 0;
                for (var verificationPass = 1; verificationPass <= MaxVerificationDownloadPasses; verificationPass++)
                {
                    completedVerificationPass = verificationPass;
                    var downloadRequests = pendingDownloads.Select(item => new FastDownloadService.DownloadRequest
                    {
                        Url = item.Url,
                        DestinationPath = item.Dest,
                        ExpectedSize = item.Size
                    }).ToList();

                    Debug.WriteLine($"Starting bundled curl batch for {downloadRequests.Count} files (verification pass {verificationPass}/{MaxVerificationDownloadPasses}).");
                    await dl.DownloadFilesAsync(downloadRequests, token).ConfigureAwait(false);

                    lock (progressLock)
                    {
                        foreach (var item in pendingDownloads)
                            fileReceivedBytes[item.Dest] = item.Size;

                        overallProgress = Clamp(fileReceivedBytes.Values.Sum(), 0, totalNeeded);
                        ReportAggregateProgress(overallProgress, 0);
                    }

                    lock (progressLock)
                    {
                        BeginProgressPhase(
                            InstallProgressPhase.Verification,
                            verificationPass: verificationPass);
                    }

                    Debug.WriteLine($"Downloads complete. Verifying pass {verificationPass}/{MaxVerificationDownloadPasses}...");
                    var failedDownloads = new List<(string Url, string Dest, ulong Hash, long Size)>();
                    var failureMessages = new List<string>();

                    foreach (var item in pendingDownloads)
                    {
                        token.ThrowIfCancellationRequested();

                        lock (progressLock)
                        {
                            if (validatedFileBytes.TryGetValue(item.Dest, out var previousValidated))
                                validationProgress = Math.Max(0, validationProgress - previousValidated);
                            validatedFileBytes[item.Dest] = 0;
                        }

                        string failureMessage = null;
                        try
                        {
                            if (!File.Exists(item.Dest))
                            {
                                failureMessage = $"{Path.GetFileName(item.Dest)} is missing after download";
                            }
                            else
                            {
                                var fi = new FileInfo(item.Dest);
                                if (fi.Length != item.Size)
                                {
                                    failureMessage = $"{Path.GetFileName(item.Dest)} has size {fi.Length}, expected {item.Size}";
                                }
                                else if (item.Size > 0)
                                {
                                    var actualHash = ComputeXxHash64(item.Dest, token, bytesRead =>
                                    {
                                        lock (progressLock)
                                        {
                                            var previous = validatedFileBytes[item.Dest];
                                            var delta = bytesRead - previous;
                                            if (delta <= 0)
                                                return;

                                            validatedFileBytes[item.Dest] = bytesRead;
                                            validationProgress = Clamp(validationProgress + delta, 0, downloadedFilesTotal);
                                            ReportAggregateProgress(overallProgress, 0);
                                        }
                                    });

                                    if (actualHash != item.Hash)
                                        failureMessage = $"{Path.GetFileName(item.Dest)} checksum is {actualHash:X}, expected {item.Hash:X}";
                                }
                            }
                        }
                        catch (OperationCanceledException)
                        {
                            throw;
                        }
                        catch (Exception ex)
                        {
                            failureMessage = $"{Path.GetFileName(item.Dest)} could not be verified: {ex.Message}";
                        }

                        if (failureMessage == null)
                        {
                            Debug.WriteLine($"Verified {Path.GetFileName(item.Dest)} OK.");
                            continue;
                        }

                        failedDownloads.Add(item);
                        failureMessages.Add(failureMessage);
                        Debug.WriteLine($"Verification failed: {failureMessage}");
                    }

                    if (failedDownloads.Count == 0)
                        break;

                    foreach (var item in failedDownloads)
                        DeleteDownloadArtifacts(item.Dest);

                    if (verificationPass == MaxVerificationDownloadPasses)
                    {
                        throw new IOException($"Verification failed after {MaxVerificationDownloadPasses} download/verification passes: {string.Join("; ", failureMessages)}");
                    }

                    lock (progressLock)
                    {
                        foreach (var item in failedDownloads)
                        {
                            fileReceivedBytes[item.Dest] = 0;
                            if (validatedFileBytes.TryGetValue(item.Dest, out var previousValidated))
                            {
                                validationProgress = Math.Max(0, validationProgress - previousValidated);
                                validatedFileBytes[item.Dest] = 0;
                            }
                        }

                        overallProgress = Clamp(fileReceivedBytes.Values.Sum(), 0, totalNeeded);
                        BeginProgressPhase(
                            InstallProgressPhase.ChecksumRepair,
                            verificationPass: verificationPass);
                        RecordSpeedSample(overallProgress);
                    }

                    pendingDownloads = failedDownloads;
                    Debug.WriteLine($"Retrying {pendingDownloads.Count} file(s) after clean verification repair.");
                }

                statusProgress?.ReportStatus(new InstallProgressStatus
                {
                    Phase = InstallProgressPhase.Complete,
                    VerificationPass = completedVerificationPass,
                    MaxVerificationPasses = MaxVerificationDownloadPasses
                });
                progressUI.ReportProgress(totalNeeded, totalNeeded, 0);
                Debug.WriteLine("All downloads completed and verified successfully.");
                EnsurePlaceholderVpkExists(installDir);
                return true;
            }
            catch (OperationCanceledException)
            {
                Debug.WriteLine("Download operation was cancelled.");
                if (!externalCts.IsCancellationRequested)
                    progressUI.ShowError("Operation Cancelled");
                return false;
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Download process failed: {ex}");
                progressUI.ShowError($"Download error: {ex.Message}");
                linked.Cancel();
                return false;
            }
            finally
            {
                stopwatch.Stop();
                Debug.WriteLine($"Download process finished in {stopwatch.Elapsed.TotalSeconds:F1}s");
            }
        }

        private static string FormatBytes(long bytes)
        {
            if (bytes < 0) bytes = 0;
            const double KB = 1024.0, MB = KB * 1024.0, GB = MB * 1024.0;
            return bytes switch
            {
                < (long)KB => $"{bytes} B",
                < (long)MB => $"{bytes / KB:F1} KB",
                < (long)GB => $"{bytes / MB:F1} MB",
                _ => $"{bytes / GB:F1} GB"
            };
        }

        private static ulong ComputeXxHash64(string filePath, CancellationToken cancellationToken = default, Action<long> progress = null)
        {
            const int bufSize = 4 * 1024 * 1024;
            try
            {
                cancellationToken.ThrowIfCancellationRequested();
                using var stream = new FileStream(filePath, FileMode.Open, FileAccess.Read, FileShare.Read, bufSize, FileOptions.SequentialScan);
                if (stream.Length == 0) return 0xEF46DB3751D8E999; // Precomputed hash for empty file
                var hasher = new XXH64();
                var buffer = new byte[bufSize];
                int read;
                long totalRead = 0;
                while ((read = stream.Read(buffer, 0, buffer.Length)) > 0)
                {
                    cancellationToken.ThrowIfCancellationRequested();
                    hasher.Update(buffer.AsSpan(0, read));
                    totalRead += read;
                    progress?.Invoke(totalRead);
                }
                return hasher.Digest();
            }
            catch (OperationCanceledException)
            {
                throw;
            }
            catch (IOException ex) // Catch specific IO exceptions
            {
                Debug.WriteLine($"IO Error hashing {filePath}: {ex.Message}");
                return 0; // Return 0 on error to force re-download
            }
            catch (Exception ex) // Catch other potential exceptions
            {
                 Debug.WriteLine($"Unexpected Error hashing {filePath}: {ex.Message}");
                 return 0; // Return 0 on error
            }
        }

        internal static bool HasDownloadSidecars(string filePath)
        {
            return GetDownloadSidecarPaths(filePath).Any(File.Exists);
        }

        internal static int DeleteDownloadArtifacts(string filePath)
        {
            var deleted = DeleteFileIfExists(filePath) ? 1 : 0;
            foreach (var sidecarPath in GetDownloadSidecarPaths(filePath))
            {
                if (DeleteFileIfExists(sidecarPath))
                    deleted++;
            }
            return deleted;
        }

        private static IEnumerable<string> GetDownloadSidecarPaths(string filePath)
        {
            yield return filePath + ".aria2";
            yield return filePath + ".part";
            yield return filePath + ".curl.partial";
        }

        private static bool DeleteFileIfExists(string filePath)
        {
            if (!File.Exists(filePath))
                return false;
            File.Delete(filePath);
            return true;
        }

        /// <summary>
        /// Clamp extension to keep a value within [min, max].
        /// </summary>
        public static T Clamp<T>(this T val, T min, T max) where T : IComparable<T>
        {
            if (val.CompareTo(min) < 0) return min;
            if (val.CompareTo(max) > 0) return max;
            return val;
        }
    }


    /// <summary>
    /// Interface for reporting installation/download progress and errors.
    /// </summary>
    public interface IInstallProgress : IDisposable
    {
        /// <param name="bytesDownloaded">Total bytes downloaded so far across all files.</param>
        /// <param name="totalBytes">Total bytes required for all files.</param>
        /// <param name="bytesPerSecond">Current estimated download speed.</param>
        void ReportProgress(long bytesDownloaded, long totalBytes, double bytesPerSecond);

        /// <summary>Action invoked if the user requests cancellation.</summary>
        Action OnCancelRequested { get; set; }

        /// <summary>Shows a modal or inline error message.</summary>
        void ShowError(string message);
    }

    public interface IInstallProgressStatus
    {
        void ReportStatus(InstallProgressStatus status);
    }

    public enum InstallProgressPhase
    {
        Preflight,
        Resume,
        Download,
        Fallback,
        Verification,
        ChecksumRepair,
        Complete
    }

    public sealed class InstallProgressStatus
    {
        public InstallProgressPhase Phase { get; set; }
        public DownloadBackend? Backend { get; set; }
        public DownloadTransferPhase TransferPhase { get; set; }
        public int Attempt { get; set; }
        public int MaxAttempts { get; set; }
        public int VerificationPass { get; set; }
        public int MaxVerificationPasses { get; set; }
    }
}
