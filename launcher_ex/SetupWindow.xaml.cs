using System;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using R1Delta;             // Need namespace for TitanfallManager, RegistryHelper
using Microsoft.WindowsAPICodePack.Dialogs;
using System.IO;
using System.Reflection;
using System.Text.RegularExpressions;
using MessageBox = System.Windows.MessageBox;
using MessageBoxButton = System.Windows.MessageBoxButton;
using MessageBoxImage = System.Windows.MessageBoxImage;
using System.Diagnostics;
using Microsoft.Win32;
using System.Windows.Media;
using System.Linq;
using System.Runtime.InteropServices;
using System.Windows.Interop;
using System.Text;

namespace launcher_ex
{
    public partial class SetupWindow : Window, IInstallProgress
    {
        private CancellationTokenSource _cts;
        public string SelectedPath { get; private set; } = string.Empty;
        public Action OnCancelRequested { get; set; }

        // --- Properties to return user choices ---
        public bool ShowSetupOnLaunch { get; private set; } = true; // Default to show
        public string LaunchArguments { get; private set; } = string.Empty;

        // --- Store the original launcher directory passed from TitanfallManager/App ---
        private readonly string _originalLauncherDir;

        /// <summary>
        /// Checks if a folder exists and is writable. Creates it if necessary.
        /// </summary>
        private bool CanWriteToFolder(string folderPath)
        {
            // ... (implementation unchanged)
            try
            {
                if (!Directory.Exists(folderPath)) { Directory.CreateDirectory(folderPath); }
                string testFile = Path.Combine(folderPath, Path.GetRandomFileName());
                using (FileStream fs = File.Create(testFile, 1, FileOptions.DeleteOnClose)) { }
                return true;
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Error checking write access to {folderPath}: {ex.Message}");
                return false;
            }
        }

        // --- MODIFIED Constructor: Accepts originalLauncherDir and current settings ---
        public SetupWindow(string originalLauncherDir, bool currentShowSetting, string currentArgs)
        {
            InitializeComponent();
            _originalLauncherDir = originalLauncherDir; // Store the passed directory

            OnCancelRequested = () => { _cts?.Cancel(); };

            // --- Use the new GetDefaultInstallPath logic ---
            string defaultPath = GetDefaultInstallPath(false);

            // Ensure the potential default directory exists before assigning
            try
            {
                if (!string.IsNullOrWhiteSpace(defaultPath) && !Directory.Exists(defaultPath))
                {
                    Directory.CreateDirectory(defaultPath);
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Could not ensure default directory exists {defaultPath}: {ex.Message}");
                // PathTextBox will still be set, but validation might fail later.
            }

            PathTextBox.Text = defaultPath;
            SelectedPath = defaultPath; // Keep internal variable updated initially

            // --- Initialize UI elements from current settings ---
            DoNotShowAgainCheckbox.IsChecked = !currentShowSetting; // Checkbox checked means DON'T show
            LaunchArgsTextBox.Text = currentArgs ?? string.Empty;
            // Initialize internal properties to current settings initially
            this.ShowSetupOnLaunch = currentShowSetting;
            this.LaunchArguments = currentArgs ?? string.Empty;


            UpdateInstructionsText();
            PathTextBox.TextChanged += PathTextBox_TextChanged;
        }

        private void Window_Loaded(object sender, RoutedEventArgs e) => ApplyTheme();

        private void ApplyTheme()
        {
            bool useDarkTheme = false; // Default to light

            try
            {
                using (RegistryKey key = Registry.CurrentUser.OpenSubKey(@"SOFTWARE\Microsoft\Windows\CurrentVersion\Themes\Personalize"))
                {
                    if (key != null)
                    {
                        object registryValueObject = key.GetValue("AppsUseLightTheme");
                        if (registryValueObject != null)
                        {
                            int registryValue = (int)registryValueObject;
                            if (registryValue == 0) // Dark Theme is Active
                            {
                                useDarkTheme = true;
                            }
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error reading theme registry key: {ex.Message}");
                // Defaulting to light theme on error
                useDarkTheme = false;
            }

            // --- Apply the chosen theme ---
            if (useDarkTheme)
            {
                ApplyDarkThemeStyling();
            }
            else
            {
                ApplyLightThemeStyling(); // Call new method for light theme
            }
        }

        // Renamed LoadDarkTheme for clarity
        private void ApplyDarkThemeStyling()
        {
            var darkThemeUri = new Uri("DarkTheme.xaml", UriKind.Relative);
            ResourceDictionary darkTheme = new ResourceDictionary { Source = darkThemeUri };

            // Avoid adding the dictionary multiple times if ApplyTheme is ever called again
            if (!this.Resources.MergedDictionaries.Any(d => d.Source == darkThemeUri))
            {
                this.Resources.MergedDictionaries.Add(darkTheme);
            }


            // --- Force apply styles to top-level elements ---
            if (TryFindResource("WindowBackgroundBrush") is Brush windowBgBrush)
            {
                this.Background = windowBgBrush;
            }
            if (TryFindResource("ControlBrush") is Brush headerBgBrush)
            {
                HeaderBorder.Background = headerBgBrush;
            }
            if (TryFindResource("BorderBrushDark") is Brush headerBorderBrush)
            {
                HeaderBorder.BorderBrush = headerBorderBrush;
            }
            if (TryFindResource("TextBrush") is Brush headerTextBrush)
            {
                SetupTitleBarText.Foreground = headerTextBrush;
            }
            // Apply styles to other controls if needed (TextBox, CheckBox, etc.)
            // This usually happens automatically if the ResourceDictionary is merged correctly,
            // but sometimes explicit setting is needed for complex scenarios or initial load.
        }

        // --- NEW Method for Light Theme ---
        private void ApplyLightThemeStyling()
        {
            // Option 1: Remove the dark theme dictionary if it exists (safer if switching themes)
            var darkThemeUri = new Uri("DarkTheme.xaml", UriKind.Relative);
            var existingDarkTheme = this.Resources.MergedDictionaries.FirstOrDefault(d => d.Source == darkThemeUri);
            if (existingDarkTheme != null)
            {
                this.Resources.MergedDictionaries.Remove(existingDarkTheme);
            }

            // Option 2: Explicitly set light theme colors (more direct for this case)
            // Use SystemColors for standard light theme appearance
            this.Background = SystemColors.ControlBrush; // Standard window background
            HeaderBorder.Background = SystemColors.ControlLightLightBrush; // Light background for header
            HeaderBorder.BorderBrush = SystemColors.ControlDarkBrush; // Darker border line
            SetupTitleBarText.Foreground = SystemColors.ControlTextBrush; // Standard text color

            // Ensure BorderThickness is correct for the light theme visual style if needed
            HeaderBorder.BorderThickness = new Thickness(0, 0, 0, 1); // As defined in your desired XAML

            // Explicitly set colors for other controls if they don't pick up SystemColors correctly
            InstructionsTextBlock.Foreground = SystemColors.ControlTextBrush;
            PathTextBox.Foreground = SystemColors.ControlTextBrush;
            PathTextBox.Background = SystemColors.WindowBrush;
            LaunchArgsTextBox.Foreground = SystemColors.ControlTextBrush;
            LaunchArgsTextBox.Background = SystemColors.WindowBrush;
            DoNotShowAgainCheckbox.Foreground = SystemColors.ControlTextBrush;
            StatusText.Foreground = SystemColors.ControlTextBrush;
            // Buttons usually adapt well, but can be set explicitly if needed
            // BrowseButton.Foreground = ...; // Keep orange?
            // OKButton.Foreground = SystemColors.ControlTextBrush;
            // CancelButton.Foreground = SystemColors.ControlTextBrush;
        }


        // --- MODIFIED: Uses TitanfallManager's detection logic first ---
        private string GetDefaultInstallPath(bool validateRegistry)
        {
            // --- Attempt to find existing valid path using the SAME logic as the manager ---
            // Note: We pass the stored _originalLauncherDir here
            // Use the registry path as the primary default if it's valid
            string registryPath = RegistryHelper.GetInstallPath();
            if ((!validateRegistry && registryPath != null ) || TitanfallManager.ValidateGamePath(registryPath, _originalLauncherDir))
            {
                Debug.WriteLine($"[SetupWindow.GetDefaultInstallPath] Using registry path as default: {registryPath}");
                return registryPath;
            }

            // Fallback to automatic detection (TitanfallFinder)
            string foundPath = TitanfallManager.TryFindExistingValidPath(_originalLauncherDir, true); // Use full validation here
            if (!string.IsNullOrEmpty(foundPath))
            {
                Debug.WriteLine($"[SetupWindow.GetDefaultInstallPath] Using automatically detected valid path as default: {foundPath}");
                return foundPath;
            }

            // --- Fallback logic if no valid path was automatically found ---
            Debug.WriteLine("[SetupWindow.GetDefaultInstallPath] No existing valid path found. Generating fallback default path.");
            try
            {
                string localAppData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
                if (!string.IsNullOrEmpty(localAppData))
                {
                    return Path.Combine(localAppData, "R1DeltaContent");
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[SetupWindow.GetDefaultInstallPath] Error getting LocalAppData: {ex.Message}");
            }

            // Final fallback
            return @"C:\Games\R1Delta\Content"; // Or consider Path.GetPathRoot(Environment.SystemDirectory) + "Games\\..."
        }


        // -------------------------------------------------------
        // UI Event Handlers
        // -------------------------------------------------------
        private void OnBrowseClicked(object sender, RoutedEventArgs e)
        {
            // ... (implementation unchanged)
            var dialog = new CommonOpenFileDialog { IsFolderPicker = true, Title = "Select Titanfall Install/Download Folder" };
            // Try to start browsing from the current path or a known location
            string initialDir = PathTextBox.Text;
            try
            {
                if (!string.IsNullOrWhiteSpace(initialDir) && Directory.Exists(Path.GetDirectoryName(initialDir)))
                {
                    dialog.InitialDirectory = Path.GetDirectoryName(initialDir);
                }
                else if (!string.IsNullOrWhiteSpace(initialDir) && Directory.Exists(initialDir))
                {
                    dialog.InitialDirectory = initialDir;
                }
                else
                {
                    // Fallback if current path is bad
                    string programFiles = Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86);
                    if (Directory.Exists(Path.Combine(programFiles, "Origin Games", "Titanfall")))
                        dialog.InitialDirectory = Path.Combine(programFiles, "Origin Games");
                    else if (Directory.Exists(Path.Combine(programFiles, "Steam", "steamapps", "common", "Titanfall")))
                        dialog.InitialDirectory = Path.Combine(programFiles, "Steam", "steamapps", "common");
                }
            }
            catch { /* Ignore errors setting initial directory */ }

            if (dialog.ShowDialog() == CommonFileDialogResult.Ok) { PathTextBox.Text = dialog.FileName; }
        }

        private void PathTextBox_TextChanged(object sender, TextChangedEventArgs e)
        {
            // ... (implementation unchanged)
            SelectedPath = PathTextBox.Text;
            UpdateInstructionsText();
        }

        private void UpdateInstructionsText()
        {
            // ... (implementation unchanged)
            string path = PathTextBox.Text;
            string finalText = "To play, please point us to your Titanfall directory.\nDon't have the game? It'll be installed there instead.\r\n(13 GB required";
            string drivePath = string.Empty;
            try { if (!string.IsNullOrWhiteSpace(path)) { drivePath = Path.GetPathRoot(path); } if (!string.IsNullOrEmpty(drivePath)) { DriveInfo drive = new DriveInfo(drivePath); double availableGB = drive.AvailableFreeSpace / (1024.0 * 1024 * 1024); finalText += $", {availableGB:F1} GB available on {drive.Name.TrimEnd('\\')}"; if (availableGB < 13) finalText += " (!)"; } }
            catch { /* Ignore drive info errors */ }
            finalText += ")"; InstructionsTextBlock.Text = finalText;
        }

        private const long RequiredSpaceBytes = 13L * 1024 * 1024 * 1024; // 13 GB

        // --- MODIFIED OnPathOkClicked ---
        private async void OnPathOkClicked(object sender, RoutedEventArgs e)
        {
            // --- 1. Basic Path Validation and Normalization ---
            string path = PathTextBox.Text;
            if (string.IsNullOrWhiteSpace(path)) { MessageBox.Show("Please select or enter a valid path.", "Invalid Path", MessageBoxButton.OK, MessageBoxImage.Warning); return; }
            try { path = Path.GetFullPath(path); PathTextBox.Text = path; } catch (Exception ex) { MessageBox.Show($"The entered path is invalid: {ex.Message}", "Invalid Path", MessageBoxButton.OK, MessageBoxImage.Warning); return; }

            // --- Update internal SelectedPath state ---
            SelectedPath = path;
            // Construct the absolute path to the forbidden subdirectory
            string forbiddenPath = Path.GetFullPath(Path.Combine(_originalLauncherDir, "r1delta"));

            // Compare the normalized selected path with the normalized forbidden path, ignoring case.
            if (string.Equals(SelectedPath, forbiddenPath, StringComparison.OrdinalIgnoreCase))
            {
                MessageBox.Show(
                    this, // Explicitly set owner window
                    $"Installation into the 'r1delta' subdirectory of the launcher's current location ('{_originalLauncherDir}') is not allowed.\n\n" +
                    $"Please choose a different directory.",
                    "Invalid Install Location",
                    MessageBoxButton.OK,
                    MessageBoxImage.Warning);
                return; // Stop processing if the path is forbidden
            }

            // --- 2. Use TitanfallManager's validation ---
            bool pathLooksValidInitially = TitanfallManager.ValidateGamePath(path, _originalLauncherDir);
            bool isExistingInstallation = false;
            if (pathLooksValidInitially)
            {
                // Check if the key file *actually* exists to differentiate between
                // a valid *existing* install and a valid *potential* install location.
                // Use the constant defined in TitanfallManager
                string fullKeyFilePath = Path.Combine(path, TitanfallManager.ValidationFileRelativePath);

                try
                {
                    isExistingInstallation = File.Exists(fullKeyFilePath);
                }
                catch (Exception ex)
                {
                    Debug.WriteLine($"Error checking key file existence for '{path}': {ex.Message}");
                    // Assume not existing if check fails
                }
            }

            // --- 3. Handle Existing Installation Case (Shortcut) ---
            if (pathLooksValidInitially && isExistingInstallation)
            {
                Debug.WriteLine($"Path '{path}' validated as existing installation. Skipping download prompt.");
                // --- Save Install Path to Registry (only path, not other settings) ---
                try { RegistryHelper.SaveInstallPath(SelectedPath); }
                catch (Exception ex) { Debug.WriteLine($"Error saving path to registry (shortcut path): {ex.Message}"); MessageBox.Show($"Could not save path to registry: {ex.Message}...", "Registry Save Warning", MessageBoxButton.OK, MessageBoxImage.Warning); }

                // --- Set return properties based on UI ---
                this.ShowSetupOnLaunch = !(DoNotShowAgainCheckbox.IsChecked ?? false);
                this.LaunchArguments = LaunchArgsTextBox.Text;

                // --- Close dialog ---
                this.DialogResult = true;
                Close();
                return;
            }
            // --- 4. Perform Full Validation Checks (for NEW install location) ---
            // Prevent installing directly into system folders (Unchanged)
            string systemRoot = Environment.GetFolderPath(Environment.SpecialFolder.Windows); string programFiles = Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles); string programFilesX86 = Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86);
            if (!string.IsNullOrEmpty(systemRoot) && path.StartsWith(systemRoot, StringComparison.OrdinalIgnoreCase) || !string.IsNullOrEmpty(programFiles) && path.StartsWith(programFiles, StringComparison.OrdinalIgnoreCase) || !string.IsNullOrEmpty(programFilesX86) && path.StartsWith(programFilesX86, StringComparison.OrdinalIgnoreCase)) { var sysFolderResult = MessageBox.Show("Installing directly into system folders (like Windows or Program Files) is strongly discouraged unless you have an existing install there already and may cause issues. Are you sure you want to continue?", "Installation Location Warning", MessageBoxButton.YesNo, MessageBoxImage.Warning); if (sysFolderResult != MessageBoxResult.Yes) { return; } }

            // Check available free space (Unchanged)
            string drivePath = Path.GetPathRoot(path); if (!string.IsNullOrEmpty(drivePath)) { try { DriveInfo drive = new DriveInfo(drivePath); long freeSpace = drive.AvailableFreeSpace; if (freeSpace < RequiredSpaceBytes) { var spaceResult = MessageBox.Show($"The selected drive ({drive.Name.TrimEnd('\\')}) only has {freeSpace / (1024.0 * 1024 * 1024):F1} GB available, but {RequiredSpaceBytes / (1024.0 * 1024 * 1024):F0} GB is required. Continue anyway?", "Low Disk Space Warning", MessageBoxButton.YesNo, MessageBoxImage.Warning); if (spaceResult != MessageBoxResult.Yes) { return; } } } catch (Exception ex) { Debug.WriteLine($"Could not check free space for '{drivePath}': {ex.Message}"); MessageBox.Show($"Could not verify free space on the selected drive. Please ensure you have enough space ({RequiredSpaceBytes / (1024.0 * 1024 * 1024):F0} GB) before continuing.", "Disk Space Check Warning", MessageBoxButton.OK, MessageBoxImage.Warning); } } else { Debug.WriteLine($"Could not determine drive root for path '{path}'."); MessageBox.Show($"Could not determine the drive for the selected path. Cannot verify free space.", "Disk Space Check Warning", MessageBoxButton.OK, MessageBoxImage.Warning); }

            // Ensure the folder exists and is writable (Unchanged)
            try { if (!Directory.Exists(path)) { Directory.CreateDirectory(path); Debug.WriteLine($"Created directory: {path}"); } if (!CanWriteToFolder(path)) { MessageBox.Show($"Cannot write to the selected folder:\n{path}\nPlease check permissions or choose a different location.", "Write Access Error", MessageBoxButton.OK, MessageBoxImage.Error); return; } } catch (Exception ex) { MessageBox.Show($"Failed to create or access the destination folder:\n{path}\n\nError: {ex.Message}", "Folder Error", MessageBoxButton.OK, MessageBoxImage.Error); return; }

            // --- 5. Confirmation Before Download ---
            double requiredGb = RequiredSpaceBytes / (1024.0 * 1024 * 1024); var confirmResult = MessageBox.Show($"The selected path appears valid for a new installation.\n\nLocation: {path}\n\nSetup will first check for the presence of existing downloaded files in that directory (if any).\n\nAfter that, Setup will now download approximately {requiredGb:F1} GB of game files.\n\nDo you want to proceed?", "Confirm Download", MessageBoxButton.OKCancel, MessageBoxImage.Information);
            if (confirmResult != MessageBoxResult.OK) { return; }

            // --- Path is fully validated and confirmed for download ---
            // SelectedPath is already updated

            // --- 6. Start Download Process ---
            PathSelectionPanel.Visibility = Visibility.Collapsed; ProgressPanel.Visibility = Visibility.Visible; SetupTitleBarText.Text = "Setup in progress...";
            _cts = new CancellationTokenSource(); bool success = false;
            try
            {
                // --- Save Install Path to Registry BEFORE download starts ---
                try { RegistryHelper.SaveInstallPath(SelectedPath); Debug.WriteLine($"Download started. Saved install path to registry: {SelectedPath}"); }
                catch (Exception ex) { Debug.WriteLine($"Error saving path to registry (pre-download): {ex.Message}"); MessageBox.Show($"Could not save the selected path to the registry before downloading: {ex.Message}\nDownload will continue, but you might be prompted again later.", "Registry Save Warning", MessageBoxButton.OK, MessageBoxImage.Warning); }

                success = await TitanfallManager.DownloadAllFilesWithResume(SelectedPath, this, _cts.Token);

            }
            catch (OperationCanceledException) { StatusText.Text = "Download canceled."; Debug.WriteLine("Download operation canceled by user."); success = false; } // Ensure success is false on cancel
            catch (Exception ex) { Debug.WriteLine($"Unexpected error during download process: {ex}"); StatusText.Text = $"Error: {ex.Message}"; success = false; }
            finally { _cts?.Dispose(); _cts = null; }

            // --- 7. Set return properties and Close Window ---
            if (success)
            {
                this.ShowSetupOnLaunch = !(DoNotShowAgainCheckbox.IsChecked ?? false);
                this.LaunchArguments = LaunchArgsTextBox.Text;
            }
            // If success is false, the DialogResult will be false, and App.xaml.cs should handle exit.
            // We don't update the return properties if the download failed/cancelled.

            this.DialogResult = success;
            Close();
        }


        private void OnCancelDownloadClicked(object sender, RoutedEventArgs e)
        {
            // ... (implementation unchanged)
            var result = MessageBox.Show(this, "Are you sure you want to cancel the download?", "Confirm Cancellation", MessageBoxButton.YesNo, MessageBoxImage.Warning);
            if (result == MessageBoxResult.Yes) { CancelButton.IsEnabled = false; StatusText.Text = "Canceling..."; OnCancelRequested?.Invoke(); }
        }
        // --- Helper Methods ---

        private string FormatBytesPerSec(double bytesPerSec)
        {
            // ... (implementation unchanged)
            if (bytesPerSec < 0) bytesPerSec = 0;
            const double KB = 1024.0;
            const double MB = KB * 1024.0;
            if (bytesPerSec < KB) return $"{bytesPerSec:F0} B/s";
            if (bytesPerSec < MB) return $"{bytesPerSec / KB:F1} KB/s";
            return $"{bytesPerSec / MB:F1} MB/s";
        }

        /// <summary>
        /// Formats a TimeSpan into a human-readable ETA string (e.g., "1h 15m", "3m 20s", "45s").
        /// </summary>
        private string FormatEta(TimeSpan eta)
        {
            if (eta.TotalSeconds <= 0)
            {
                return "--:--";
            }

            var sb = new StringBuilder();
            if (eta.Days > 0)
            {
                sb.Append($"{eta.Days}d ");
                sb.Append($"{eta.Hours}h"); // Show hours if days are present
            }
            else if (eta.Hours > 0)
            {
                sb.Append($"{eta.Hours}h ");
                sb.Append($"{eta.Minutes:00}m"); // Show minutes if hours are present
            }
            else if (eta.Minutes > 0)
            {
                sb.Append($"{eta.Minutes}m ");
                sb.Append($"{eta.Seconds:00}s"); // Show seconds if minutes are present
            }
            else
            {
                sb.Append($"{eta.Seconds:F0}s"); // Only show seconds if less than a minute
            }

            return sb.ToString();
        }

        // --- Static Helper (needs to be accessible by ReportProgress, move if needed) ---
        /// <summary> Formats byte count into B/KB/MB/GB string. </summary>
        private static string FormatBytes(long bytes) // Made static if needed elsewhere or keep instance if only used here
        {
            // (Implementation copied from TitanfallManager or shared)
            if (bytes < 0) bytes = 0;
            const double KB = 1024.0;
            const double MB = KB * 1024.0;
            const double GB = MB * 1024.0;

            if (bytes < KB) return $"{bytes} B";
            if (bytes < MB) return $"{bytes / KB:F1} KB";
            if (bytes < GB) return $"{bytes / MB:F1} MB";
            return $"{bytes / GB:F1} GB";
        }

        // IInstallProgress methods (Unchanged)
        /// <summary>
        /// Reports download progress to the UI, including size, speed, and ETA.
        /// </summary>
        /// <param name="bytesDownloaded">Total bytes downloaded so far for the entire operation.</param>
        /// <param name="totalBytes">Total bytes expected for the entire operation.</param>
        /// <param name="bytesPerSecond">Current calculated download speed.</param>
        public void ReportProgress(long bytesDownloaded, long totalBytes, double bytesPerSecond)
        {
            // Ensure UI updates happen on the UI thread
            if (!this.Dispatcher.CheckAccess())
            {
                this.Dispatcher.Invoke(() => ReportProgress(bytesDownloaded, totalBytes, bytesPerSecond));
                return;
            }

            // Calculate Percentage
            double percent = (totalBytes > 0) ? ((double)bytesDownloaded * 100.0 / totalBytes) : 0;
            percent = Math.Max(0, Math.Min(100, percent)); // Clamp percentage

            // Update Progress Bar
            ProgressBar.Value = percent;

            // Format Sizes
            string downloadedSizeStr = FormatBytes(bytesDownloaded);
            string totalSizeStr = (totalBytes > 0) ? FormatBytes(totalBytes) : "Unknown"; // Handle unknown total size case

            // Format Speed
            string speedStr = FormatBytesPerSec(bytesPerSecond);

            // Calculate and Format ETA
            string etaStr = "--:--"; // Default ETA string
            if (bytesPerSecond > 1 && totalBytes > 0 && bytesDownloaded < totalBytes) // Need speed, known total, and work remaining
            {
                long bytesRemaining = totalBytes - bytesDownloaded;
                if (bytesRemaining > 0)
                {
                    double etaSeconds = bytesRemaining / bytesPerSecond;
                    etaStr = FormatEta(TimeSpan.FromSeconds(etaSeconds));
                }
            }
            else if (bytesDownloaded >= totalBytes && totalBytes > 0)
            {
                etaStr = "Done"; // Or clear it if preferred
            }
            else if (bytesPerSecond <= 1 && bytesDownloaded < totalBytes) // Handle stalled downloads
            {
                etaStr = "Stalled";
            }


            // Update Status Text Label
            // Example format: Downloading... 55% (1.2 GB / 2.2 GB) - 15.3 MB/s - ETA: 1m 30s
            StatusText.Text = $"Downloading: {percent:0}% ({downloadedSizeStr} / {totalSizeStr})\nSpeed: {speedStr} | ETA: {etaStr}\nSetup will launch the game automatically when complete.";

            // Special case for completion
            if (percent >= 100)
            {
                StatusText.Text = $"Download Complete ({totalSizeStr})";
                CancelButton.IsEnabled = false; // Disable cancel when done
            }
        }

        public void ShowError(string message)
        {
            // Check if we can access the dispatcher
            if (this.Dispatcher == null) // Should ideally not happen if called correctly
            {
                Debug.WriteLine($"SetupWindow.ShowError (Dispatcher is null): {message}");
                return;
            }

            // Always dispatch the check AND the message box show
            if (!this.Dispatcher.CheckAccess())
            {
                this.Dispatcher.Invoke(() =>
                {
                    // Now perform the IsLoaded check safely on the UI thread
                    if (this.IsLoaded)
                    {
                        MessageBox.Show(this, message, "Error", MessageBoxButton.OK, MessageBoxImage.Error);
                    }
                    else
                    {
                        // Log if window isn't loaded when ShowError is finally processed on UI thread
                        Debug.WriteLine($"SetupWindow.ShowError (Window not loaded on UI thread): {message}");
                    }
                });
            }
            else // Already on the UI thread
            {
                // Perform the IsLoaded check directly
                if (this.IsLoaded)
                {
                    MessageBox.Show(this, message, "Error", MessageBoxButton.OK, MessageBoxImage.Error);
                }
                else
                {
                    Debug.WriteLine($"SetupWindow.ShowError (Window not loaded on UI thread): {message}");
                }
            }
        }

        public void Dispose() { _cts?.Dispose(); }
        // Win32 constants
        private const uint WS_MINIMIZEBOX = 0x00020000;
        private const uint WS_MAXIMIZEBOX = 0x00010000;
        private const int GWL_STYLE = -16;
        private const int GWL_EXSTYLE = -20;
        private const int SWP_NOSIZE = 0x0001;
        private const int SWP_NOMOVE = 0x0002;
        private const int SWP_NOZORDER = 0x0004;
        private const int SWP_FRAMECHANGED = 0x0020;
        private const int WM_SYSCOMMAND = 0x0112;
        private const int WM_SETICON = 0x0080;
        private const int WS_EX_DLGMODALFRAME = 0x0001;

        [DllImport("user32.dll")]
        public static extern int SendMessage(IntPtr hWnd, int wMsg, IntPtr wParam, IntPtr lParam);

        [DllImport("user32.dll")]
        private static extern uint GetWindowLong(IntPtr hwnd, int index);

        [DllImport("user32.dll")]
        private static extern int SetWindowLong(IntPtr hwnd, int index, uint newStyle);

        [DllImport("user32.dll")]
        private static extern bool SetWindowPos(IntPtr hwnd, IntPtr hwndInsertAfter, int x, int y, int width, int height, uint flags);

        protected override void OnSourceInitialized(EventArgs e)
        {
            base.OnSourceInitialized(e);

            IntPtr hwnd = new System.Windows.Interop.WindowInteropHelper(this).Handle;
            uint styles = GetWindowLong(hwnd, GWL_STYLE);

            // Remove the maximize and minimize buttons
            styles &= 0xFFFFFFFF ^ (WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
            SetWindowLong(hwnd, GWL_STYLE, styles);

            // Change to dialog modal - necessary for the final step to work!
            styles = GetWindowLong(hwnd, GWL_EXSTYLE);
            styles |= WS_EX_DLGMODALFRAME;
            SetWindowLong(hwnd, GWL_EXSTYLE, styles);

            SetWindowPos(hwnd, IntPtr.Zero, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

            // Remove the icon
            SendMessage(hwnd, WM_SETICON, new IntPtr(1), IntPtr.Zero);
            SendMessage(hwnd, WM_SETICON, IntPtr.Zero, IntPtr.Zero);
        }
    }
}
