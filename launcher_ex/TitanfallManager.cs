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

            var fileReceivedBytes = new Dictionary<string, long>(StringComparer.OrdinalIgnoreCase);
            var fileTotalBytes = new Dictionary<string, long>(StringComparer.OrdinalIgnoreCase);
            var toDownload = new List<(string Url, string Dest, ulong Hash, long Size)>();
            var history = new Queue<(double Time, long Progress)>();
            const double rollingWindow = 5.0;
            double lastUpdate = -1;
            object errorLock = new object();
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
                                if (knownSize == 0)
                                    needs = false;
                                else if (ComputeXxHash64(dest) == expectedHash)
                                    needs = false;
                                else
                                    Debug.WriteLine($"Checksum mismatch: {relPath}");
                            }
                            else
                            {
                                Debug.WriteLine($"Size mismatch: {relPath}");
                            }
                        }
                        catch (Exception ex)
                        {
                            Debug.WriteLine($"Warning verifying {dest}: {ex.Message}");
                        }
                    }

                    fileTotalBytes[dest] = knownSize;
                    fileReceivedBytes[dest] = needs ? 0 : knownSize;
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
            var alreadyHave = fileReceivedBytes.Values.Sum();
            progressUI.ReportProgress(alreadyHave, totalNeeded, 0.0);

            if (!toDownload.Any())
            {
                Debug.WriteLine("All files present.");
                progressUI.ReportProgress(totalNeeded, totalNeeded, 0.0);
                EnsurePlaceholderVpkExists(installDir);
                return true;
            }

            Debug.WriteLine($"{toDownload.Count} files to download.");
            var stopwatch = Stopwatch.StartNew();
            var overall = alreadyHave;
            using var linked = CancellationTokenSource.CreateLinkedTokenSource(externalCts);
            var token = linked.Token;
            using var sem = new SemaphoreSlim(10, 10);

            BitsJanitor.PurgeStaleJobs();

            var tasks = toDownload.Select(item => Task.Run(async () =>
            {
                await sem.WaitAsync(token).ConfigureAwait(false);
                token.ThrowIfCancellationRequested();

                using var dl = new FastDownloadService();
                try
                {
                    void OnProg(long got, long total)
                    {
                        if (token.IsCancellationRequested) return;
                        lock (errorLock)
                        {
                            var prev = fileReceivedBytes[item.Dest];
                            var delta = got - prev;
                            overall += delta;
                            fileReceivedBytes[item.Dest] = got;
                            overall = Clamp(overall, alreadyHave, totalNeeded);

                            var now = stopwatch.Elapsed.TotalSeconds;
                            history.Enqueue((now, overall));
                            while (history.Count > 1 && history.Peek().Time < now - rollingWindow)
                                history.Dequeue();

                            if (now - lastUpdate >= 1 || overall == totalNeeded)
                            {
                                double speed = 0;
                                if (history.Count > 1)
                                {
                                    var (t0, p0) = history.Peek();
                                    var dt = now - t0;
                                    var dp = overall - p0;
                                    if (dt > 0.01) speed = dp / dt;
                                }
                                progressUI.ReportProgress(overall, totalNeeded, speed);
                                lastUpdate = now;
                            }
                        }
                    }

                    dl.DownloadProgressChanged += OnProg;
                    try
                    {
                        Debug.WriteLine($"Downloading {Path.GetFileName(item.Dest)}");
                        await dl.DownloadFileAsync(item.Url, item.Dest, token).ConfigureAwait(false);
                    }
                    finally
                    {
                        dl.DownloadProgressChanged -= OnProg;
                    }

                    // Final correction
                    lock (errorLock)
                    {
                        var prev = fileReceivedBytes[item.Dest];
                        var delta = item.Size - prev;
                        overall += delta;
                        fileReceivedBytes[item.Dest] = item.Size;
                        overall = Clamp(overall, alreadyHave, totalNeeded);
                    }

                    token.ThrowIfCancellationRequested();

                    var fi2 = new FileInfo(item.Dest);
                    if (fi2.Length != item.Size)
                        throw new IOException($"Size mismatch after download: expected {item.Size}, got {fi2.Length}");

                    if (item.Size > 0 && ComputeXxHash64(item.Dest) != item.Hash)
                        throw new IOException($"Checksum mismatch on {Path.GetFileName(item.Dest)}");
                }
                catch (OperationCanceledException)
                {
                    Debug.WriteLine($"Cancelled: {Path.GetFileName(item.Dest)}");
                    throw;
                }
                catch (Exception ex)
                {
                    Debug.WriteLine($"Error on {Path.GetFileName(item.Dest)}: {ex}");
                    bool first;
                    lock (errorLock)
                    {
                        first = !errorReported;
                        if (first) errorReported = true;
                    }
                    if (first)
                        progressUI.ShowError($"Download error ({Path.GetFileName(item.Dest)}): {ex.Message}");
                    throw;
                }
                finally
                {
                    sem.Release();
                }
            }, token)).ToList();

            try
            {
                Debug.WriteLine($"Waiting for {tasks.Count} downloads...");
                await Task.WhenAll(tasks).ConfigureAwait(false);
                token.ThrowIfCancellationRequested();

                progressUI.ReportProgress(overall, totalNeeded, 0);
                if (errorReported)
                {
                    Debug.WriteLine("Completed with errors.");
                    return false;
                }

                Debug.WriteLine("All downloads verified.");
                EnsurePlaceholderVpkExists(installDir);
                return true;
            }
            catch (OperationCanceledException)
            {
                Debug.WriteLine("Download cancelled.");
                progressUI.ReportProgress(overall, totalNeeded, 0);
                return false;
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Unexpected error: {ex}");
                if (!errorReported)
                    progressUI.ShowError($"Unexpected error: {ex.Message}");
                progressUI.ReportProgress(overall, totalNeeded, 0);
                return false;
            }
            finally
            {
                stopwatch.Stop();
                Debug.WriteLine($"Finished in {stopwatch.Elapsed.TotalSeconds:F1}s");
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
                if (stream.Length == 0) return 0xEF46DB3751D8E999;
                var hasher = new XXH64();
                var buffer = new byte[bufSize];
                int read;
                while ((read = stream.Read(buffer, 0, buffer.Length)) > 0)
                    hasher.Update(buffer.AsSpan(0, read));
                return hasher.Digest();
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Error hashing {filePath}: {ex.Message}");
                return 0;
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
    }

    /// <summary>
    /// Interface for reporting installation/download progress and errors.
    /// </summary>
    public interface IInstallProgress : IDisposable
    {
        /// <param name="bytesDownloaded">Total bytes downloaded so far.</param>
        /// <param name="totalBytes">Total bytes to download.</param>
        /// <param name="bytesPerSecond">Current download speed.</param>
        void ReportProgress(long bytesDownloaded, long totalBytes, double bytesPerSecond);

        /// <summary>Action invoked if the user requests cancellation.</summary>
        Action OnCancelRequested { get; set; }

        /// <summary>Shows a modal or inline error message.</summary>
        void ShowError(string message);
    }
}
