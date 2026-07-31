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
using Newtonsoft.Json;               // If resume manifest is used later
using K4os.Hash.xxHash;
using launcher_ex;                   // For IInstallProgress, SetupWindow
// using Monitor.Core.Utilities;     // Not used directly here
using System.Reflection;
using Microsoft.Win32;
using Dark.Net;

// Assuming FastDownloadService class exists and handles downloads
// using FastDownloadService;        // Or whatever namespace it's in

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
    }

    /// <summary>
    /// Manages detection, validation, and download of Titanfall game files.
    /// </summary>
    public static class TitanfallManager
    {
        internal const string ValidationFileRelativePath = @"vpk\client_mp_common.bsp.pak000_000.vpk";
        private const int MaxVerificationDownloadPasses = 3;

        /// <summary>
        /// Tries to locate an existing valid Titanfall directory via registry or custom finder.
        /// </summary>
        internal static string TryFindExistingValidPath(string originalLauncherDir, bool fullValidate)
        {
            // a) Registry
            var registryPath = RegistryHelper.GetInstallPath();
            if (ValidateGamePath(registryPath, originalLauncherDir))
            {
                Debug.WriteLine($"[TryFindExistingValidPath] Found via registry: {registryPath}");
                return registryPath;
            }

            // b) TitanfallFinder
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

            if (!string.IsNullOrEmpty(finderDir) &&
                (!fullValidate || ValidateGamePath(finderDir, originalLauncherDir)))
            {
                Debug.WriteLine($"[TryFindExistingValidPath] Found via finder: {finderDir}");
                return finderDir;
            }

            Debug.WriteLine("[TryFindExistingValidPath] No valid path found.");
            return null;
        }

        /// <summary>
        /// Validates that the given path points to a working Titanfall install.
        /// </summary>
        internal static bool ValidateGamePath(string path, string originalLauncherDir)
        {
            if (string.IsNullOrWhiteSpace(path))
                return false;

            try
            {
                var full = Path.GetFullPath(path);
                if (!Directory.Exists(full))
                {
                    Debug.WriteLine($"[ValidateGamePath] Not found: {full}");
                    return false;
                }

                var check = Path.Combine(full, ValidationFileRelativePath);
                if (!File.Exists(check))
                {
                    Debug.WriteLine($"[ValidateGamePath] Missing file: {check}");
                    return false;
                }

                Debug.WriteLine($"[ValidateGamePath] OK: {full}");
                return true;
            }
            catch (Exception ex) when (
                ex is NotSupportedException ||
                ex is System.Security.SecurityException ||
                ex is ArgumentException ||
                ex is PathTooLongException ||
                ex is IOException)
            {
                Debug.WriteLine($"[ValidateGamePath] Error validating '{path}': {ex.Message}");
                return false;
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[ValidateGamePath] Unexpected for '{path}': {ex.GetType().Name} - {ex.Message}");
                return false;
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

            // Ensure installDir is an absolute, normalized path for FastDownloadService.
            try
            {
                installDir = Path.GetFullPath(installDir);
            }
            catch (Exception ex)
            {
                progressUI.ShowError($"Internal Error: Invalid installation directory '{installDir}': {ex.Message}");
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
            long overallProgress = 0; // Initialize overall progress
            long maxReportedOverallProgress = -1; // Initialize max reported progress to ensure first report goes through
            var totalManifestBytes = TitanfallFileList.s_fileList.Sum(file => file.Size);
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
                // Ensure final progress is reported as 100%
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
                projectedProgress = Math.Min(projectedProgress, preVerificationProgressCap);
                maxReportedOverallProgress = Math.Max(maxReportedOverallProgress, projectedProgress);
                progressUI.ReportProgress(maxReportedOverallProgress, totalNeeded, Math.Max(0, speed));
                lastUpdate = stopwatch.Elapsed.TotalSeconds;
            }

            ReportAggregateProgress(overallProgress, 0.0);

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
                dl.DownloadProgressChanged += (destinationPath, got, total) =>
                {
                    if (token.IsCancellationRequested) return;

                    lock (progressLock)
                    {
                        var expectedSize = fileTotalBytes.TryGetValue(destinationPath, out var knownSize) ? knownSize : total;
                        fileReceivedBytes[destinationPath] = Clamp(got, 0, expectedSize);
                        overallProgress = Clamp(fileReceivedBytes.Values.Sum(), 0, totalNeeded);
                        RecordSpeedSample(overallProgress);

                        var now = stopwatch.Elapsed.TotalSeconds;
                        if (now - lastUpdate >= 0.5)
                            ReportAggregateProgress(overallProgress, CalculateSpeed());
                    }
                };

                var pendingDownloads = toDownload.ToList();
                for (var verificationPass = 1; verificationPass <= MaxVerificationDownloadPasses; verificationPass++)
                {
                    var downloadRequests = pendingDownloads.Select(item => new FastDownloadService.DownloadRequest
                    {
                        Url = item.Url,
                        DestinationPath = item.Dest
                    }).ToList();

                    Debug.WriteLine($"Starting aria2c batch for {downloadRequests.Count} files (verification pass {verificationPass}/{MaxVerificationDownloadPasses}).");
                    await dl.DownloadFilesAsync(downloadRequests, token).ConfigureAwait(false);

                    lock (progressLock)
                    {
                        foreach (var item in pendingDownloads)
                            fileReceivedBytes[item.Dest] = item.Size;

                        overallProgress = Clamp(fileReceivedBytes.Values.Sum(), 0, totalNeeded);
                        ReportAggregateProgress(overallProgress, 0);
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
                        history.Clear();
                        RecordSpeedSample(overallProgress);
                        ReportAggregateProgress(overallProgress, 0);
                    }

                    pendingDownloads = failedDownloads;
                    Debug.WriteLine($"Retrying {pendingDownloads.Count} file(s) after clean verification repair.");
                }

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

        private static void DeleteDownloadArtifacts(string filePath)
        {
            DeleteFileIfExists(filePath);
            DeleteFileIfExists(filePath + ".aria2");
        }

        private static void DeleteFileIfExists(string filePath)
        {
            if (File.Exists(filePath))
                File.Delete(filePath);
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
}
