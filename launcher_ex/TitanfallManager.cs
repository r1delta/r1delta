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
using System.Net.Http;               // Using HttpClient for FastDownloadService likely
using System.Text;
// using Monitor.Core.Utilities;     // Not used directly here
using System.Reflection;
using Microsoft.Win32;
using Dark.Net;
using BitsCleanup;                   // For BitsJanitor.PurgeStaleJobs()

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

            if (string.IsNullOrWhiteSpace(installDir) || !Directory.Exists(installDir))
            {
                progressUI.ShowError("Internal Error: Invalid installation directory.");
                return false;
            }

            // Ensure installDir is an absolute path for FastDownloadService
            installDir = Path.GetFullPath(installDir);

            var fileReceivedBytes = new Dictionary<string, long>(StringComparer.OrdinalIgnoreCase);
            var fileTotalBytes = new Dictionary<string, long>(StringComparer.OrdinalIgnoreCase);
            var toDownload = new List<(string Url, string Dest, ulong Hash, long Size)>();
            var history = new Queue<(double Time, long Progress)>();
            const double rollingWindow = 5.0;
            double lastUpdate = -1;
            object progressLock = new object(); // Renamed from errorLock for clarity
            bool errorReported = false;

            Debug.WriteLine($"Verifying existing files in: {installDir}");
            try
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
                        progressUI.ShowError("Internal Error: Could not determine directory.");
                        return false;
                    }
                    Directory.CreateDirectory(dir);

                    bool needs = true;
                    if (File.Exists(dest))
                    {
                        try
                        {
                            var fi = new FileInfo(dest);
                            if (fi.Length == knownSize)
                            {
                                if (knownSize == 0) // Empty files are always considered valid if size matches
                                    needs = false;
                                else if (ComputeXxHash64(dest) == expectedHash)
                                    needs = false;
                                else
                                    Debug.WriteLine($"Checksum mismatch: {relPath}");
                            }
                            else
                            {
                                Debug.WriteLine($"Size mismatch: {relPath} (Expected: {knownSize}, Got: {fi.Length})");
                            }
                        }
                        catch (Exception ex)
                        {
                            Debug.WriteLine($"Warning verifying {dest}: {ex.Message}");
                            // Assume needs download if verification fails
                        }
                    }

                    fileTotalBytes[dest] = knownSize;
                    fileReceivedBytes[dest] = needs ? 0 : knownSize; // Assume 0 if needs download
                    if (needs)
                        toDownload.Add((url, dest, expectedHash, knownSize));
                }
            }
            catch (OperationCanceledException)
            {
                Debug.WriteLine("Verification cancelled.");
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
            var alreadyHave = fileReceivedBytes.Values.Where(kvp => !toDownload.Any(item => item.Dest.Equals(kvp, StringComparison.OrdinalIgnoreCase))).Sum(kvp => kvp.Value); // Sum only files NOT in toDownload list
            progressUI.ReportProgress(alreadyHave, totalNeeded, 0.0);

            if (!toDownload.Any())
            {
                Debug.WriteLine("All files present and verified.");
                progressUI.ReportProgress(totalNeeded, totalNeeded, 0.0);
                EnsurePlaceholderVpkExists(installDir);
                return true;
            }

            Debug.WriteLine($"{toDownload.Count} files to download/resume.");

            // --- BITS Cleanup ---
            try
            {
                Debug.WriteLine("Running BITS cleanup...");
                // Use the combined predicate for cleanup
                BitsJanitor.PurgeStaleJobs(BitsJanitor.CombinedCleanupPredicate, new DebugTextWriter());
                Debug.WriteLine("BITS cleanup finished.");
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Warning: BITS cleanup failed: {ex.Message}");
                // Continue regardless of cleanup failure
            }
            // --- End BITS Cleanup ---


            var stopwatch = Stopwatch.StartNew();
            var overallProgress = alreadyHave; // Start progress from already verified files
            using var linked = CancellationTokenSource.CreateLinkedTokenSource(externalCts);
            var token = linked.Token;
            using var sem = new SemaphoreSlim(10, 10); // Limit concurrent downloads

            // --- FastDownloadService Instantiation ---
            using var dl = new FastDownloadService(installDir); // Pass installDir

            var tasks = toDownload.Select(item => Task.Run(async () =>
            {
                await sem.WaitAsync(token).ConfigureAwait(false);
                token.ThrowIfCancellationRequested();

                try
                {
                    // Progress reporting setup
                    long currentFileBytes = 0; // Track progress for this specific file
                    void OnProg(long got, long total) // total here is BITS' total, might differ slightly if resuming
                    {
                        if (token.IsCancellationRequested) return;

                        lock (progressLock)
                        {
                            // Calculate delta for this file only
                            var delta = got - currentFileBytes;
                            currentFileBytes = got; // Update this file's progress

                            // Update overall progress
                            overallProgress += delta;
                            // Clamp overall progress to prevent over/undershooting due to concurrency/resume inaccuracies
                            overallProgress = Clamp(overallProgress, 0, totalNeeded);

                            // Update the dictionaries (optional, mainly for debugging/state)
                            fileReceivedBytes[item.Dest] = got;

                            // Calculate rolling speed
                            var now = stopwatch.Elapsed.TotalSeconds;
                            history.Enqueue((now, overallProgress));
                            while (history.Count > 1 && history.Peek().Time < now - rollingWindow)
                                history.Dequeue();

                            // Throttle UI updates
                            if (now - lastUpdate >= 0.5 || overallProgress == totalNeeded || got == item.Size) // Update slightly more often
                            {
                                double speed = 0;
                                if (history.Count > 1)
                                {
                                    var (t0, p0) = history.Peek();
                                    var dt = now - t0;
                                    var dp = overallProgress - p0;
                                    if (dt > 0.01) speed = dp / dt;
                                }
                                progressUI.ReportProgress(overallProgress, totalNeeded, speed);
                                lastUpdate = now;
                            }
                        }
                    }

                    dl.DownloadProgressChanged += OnProg;
                    try
                    {
                        Debug.WriteLine($"Downloading/Resuming {Path.GetFileName(item.Dest)}");
                        await dl.DownloadFileAsync(item.Url, item.Dest, token).ConfigureAwait(false);
                        Debug.WriteLine($"Finished {Path.GetFileName(item.Dest)}");
                    }
                    finally
                    {
                        dl.DownloadProgressChanged -= OnProg;
                    }

                    token.ThrowIfCancellationRequested(); // Check cancellation after download completes

                    // --- Post-Download Verification ---
                    var fi2 = new FileInfo(item.Dest);
                    if (fi2.Length != item.Size)
                    {
                        throw new IOException($"Size mismatch after download for {Path.GetFileName(item.Dest)}: expected {item.Size}, got {fi2.Length}");
                    }

                    if (item.Size > 0) // Only hash non-empty files
                    {
                        var actualHash = ComputeXxHash64(item.Dest);
                        if (actualHash != item.Hash)
                        {
                            throw new IOException($"Checksum mismatch after download for {Path.GetFileName(item.Dest)}: expected {item.Hash:X}, got {actualHash:X}");
                        }
                    }
                     Debug.WriteLine($"Verified {Path.GetFileName(item.Dest)} OK.");

                    // --- Final Progress Update ---
                    // Ensure this file's contribution is fully accounted for
                    lock (progressLock)
                    {
                        var previousBytes = fileReceivedBytes[item.Dest];
                        var finalDelta = item.Size - previousBytes;
                        overallProgress += finalDelta;
                        fileReceivedBytes[item.Dest] = item.Size; // Mark as complete in dictionary
                        overallProgress = Clamp(overallProgress, 0, totalNeeded); // Clamp again

                        // Force one last UI update for this file completion if needed
                        var now = stopwatch.Elapsed.TotalSeconds;
                        double speed = 0; // Speed is less relevant on final update
                         if (history.Count > 1) { var (t0, p0) = history.Peek(); var dt = now - t0; var dp = overallProgress - p0; if (dt > 0.01) speed = dp / dt; }
                        progressUI.ReportProgress(overallProgress, totalNeeded, speed);
                        lastUpdate = now;
                    }
                }
                catch (OperationCanceledException)
                {
                    Debug.WriteLine($"Cancelled: {Path.GetFileName(item.Dest)}");
                    // Don't re-throw cancellation, let Task.WhenAll handle it
                }
                catch (Exception ex)
                {
                    Debug.WriteLine($"Error downloading/verifying {Path.GetFileName(item.Dest)}: {ex}");
                    bool firstError;
                    lock (progressLock) // Use the same lock for error reporting flag
                    {
                        firstError = !errorReported;
                        if (firstError) errorReported = true;
                    }
                    if (firstError) // Report only the first error encountered
                        progressUI.ShowError($"Download error ({Path.GetFileName(item.Dest)}): {ex.Message}");

                    // We don't re-throw here to allow other downloads to potentially finish,
                    // but the errorReported flag will cause the overall result to be false.
                }
                finally
                {
                    sem.Release();
                }
            }, token)).ToList();

            try
            {
                Debug.WriteLine($"Waiting for {tasks.Count} download tasks...");
                await Task.WhenAll(tasks).ConfigureAwait(false);
                // Don't check token here, WhenAll throws if any task was cancelled

                // Final progress update after all tasks complete
                progressUI.ReportProgress(overallProgress, totalNeeded, 0);

                if (errorReported)
                {
                    Debug.WriteLine("Download process completed with errors.");
                    return false; // Return false if any error occurred
                }

                // Double-check overall progress after completion (optional sanity check)
                if (overallProgress != totalNeeded)
                {
                     Debug.WriteLine($"Warning: Final progress {overallProgress} does not match total {totalNeeded}.");
                     // Might indicate calculation issues or files changing size unexpectedly.
                     // Decide if this should be an error or just a warning.
                }


                Debug.WriteLine("All downloads completed and verified successfully.");
                EnsurePlaceholderVpkExists(installDir);
                return true; // Success
            }
            catch (OperationCanceledException)
            {
                Debug.WriteLine("Download operation was cancelled.");
                progressUI.ReportProgress(overallProgress, totalNeeded, 0); // Update progress on cancel
                return false; // Return false as the operation didn't complete fully
            }
            // No need for a general catch here, as individual task exceptions are handled within the Task.Run lambda
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

        private static ulong ComputeXxHash64(string filePath)
        {
            const int bufSize = 4 * 1024 * 1024;
            try
            {
                using var stream = new FileStream(filePath, FileMode.Open, FileAccess.Read, FileShare.Read, bufSize, FileOptions.SequentialScan);
                if (stream.Length == 0) return 0xEF46DB3751D8E999; // Precomputed hash for empty file
                var hasher = new XXH64();
                var buffer = new byte[bufSize];
                int read;
                while ((read = stream.Read(buffer, 0, buffer.Length)) > 0)
                    hasher.Update(buffer.AsSpan(0, read));
                return hasher.Digest();
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

        private static void TryDeleteFile(string filePath)
        {
            if (string.IsNullOrEmpty(filePath)) return;
            try
            {
                if (File.Exists(filePath))
                    File.Delete(filePath);
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Warning: Failed to delete '{filePath}': {ex.Message}");
            }
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

        // Helper class to redirect Console.Write/WriteLine to Debug.Write/WriteLine
        private class DebugTextWriter : TextWriter
        {
            public override Encoding Encoding => Encoding.UTF8;
            public override void WriteLine(string value) => Debug.WriteLine(value);
            public override void Write(string value) => Debug.Write(value);
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
