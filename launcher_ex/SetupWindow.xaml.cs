﻿using System;
using System.ComponentModel;
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
using System.Windows.Documents;
using System.Windows.Media.Imaging;

namespace launcher_ex
{
    public partial class SetupWindow : Window, IInstallProgress, IInstallProgressStatus
    {
        private CancellationTokenSource _cts;
        private Task<bool> _activeSetupTask;
        private Task _closeTask;
        private bool _allowClose;
        private InstallProgressStatus _installStatus = new InstallProgressStatus { Phase = InstallProgressPhase.Preflight };
        private long _lastBytesDownloaded;
        private long _lastTotalBytes;
        private double _lastBytesPerSecond;
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
            Closing += OnWindowClosing;

            // --- Use the new GetDefaultInstallPath logic ---
            string defaultPath = GetDefaultInstallPath();

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
            UpdatePlayOrInstallButton();
            PathTextBox.TextChanged += PathTextBox_TextChanged;
        }

        private void UpdatePlayOrInstallButton()
        {
            var resolution = TitanfallManager.ResolveGameRoot(PathTextBox.Text);
            PlayOrInstallButton.Content = resolution.Succeeded
                ? "Play"
                : resolution.IsUsableDestination ? "Install" : "Select Game Folder";
        }

        private void Window_Loaded(object sender, RoutedEventArgs e) => ApplyTheme();

        private void ApplyTheme()
        {
            bool useDarkTheme = true; // Default to light

            this.Icon = new BitmapImage(new Uri("pack://application:,,,/icon1.ico"));

            //try
            //{
            //    using (RegistryKey key = Registry.CurrentUser.OpenSubKey(@"SOFTWARE\Microsoft\Windows\CurrentVersion\Themes\Personalize"))
            //    {
            //        if (key != null)
            //        {
            //            object registryValueObject = key.GetValue("AppsUseLightTheme");
            //            if (registryValueObject != null)
            //            {
            //                int registryValue = (int)registryValueObject;
            //                if (registryValue == 0) // Dark Theme is Active
            //                {
            //                    useDarkTheme = true;
            //                }
            //            }
            //        }
            //    }
            //}
            //catch (Exception ex)
            //{
            //    Console.WriteLine($"Error reading theme registry key: {ex.Message}");
            //    // Defaulting to light theme on error
            //    useDarkTheme = false;
            //}

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
                //HeaderBorder.Background = headerBgBrush;
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
        private string GetDefaultInstallPath()
        {
            var configuredResolution = TitanfallManager.ResolveGameRoot(RegistryHelper.GetInstallPath());
            if (configuredResolution.IsUsableDestination)
            {
                Debug.WriteLine($"[SetupWindow.GetDefaultInstallPath] Using configured destination: {configuredResolution.ResolvedRoot}");
                return configuredResolution.ResolvedRoot;
            }

            string foundPath = TitanfallManager.TryFindExistingValidPath();
            if (!string.IsNullOrEmpty(foundPath))
            {
                Debug.WriteLine($"[SetupWindow.GetDefaultInstallPath] Using resolved game root: {foundPath}");
                return foundPath;
            }

            Debug.WriteLine("[SetupWindow.GetDefaultInstallPath] No existing valid path found.");
            return string.Empty;
        }


        // -------------------------------------------------------
        // UI Event Handlers
        // -------------------------------------------------------
        private void OnBrowseClicked(object sender, RoutedEventArgs e)
        {
            // The shell's folder picker (and the shell extensions it loads) can throw. An exception escaping this
            // handler ends setup right after the user chose a folder, so report it and keep the window open.
            try
            {
                BrowseForGameFolder();
            }
            catch (Exception ex)
            {
                LauncherLog.Write("the folder picker failed", ex);
                MessageBox.Show(
                    this,
                    $"The folder picker failed: {ex.Message}\n\n" +
                    "Type or paste the game folder path into the text box instead.\n\n" +
                    $"Details were written to:\n{LauncherLog.Path}",
                    "Folder Picker Error",
                    MessageBoxButton.OK,
                    MessageBoxImage.Error);
            }
        }

        private void BrowseForGameFolder()
        {
            var dialog = new CommonOpenFileDialog { IsFolderPicker = true, Title = "Select Titanfall Game Folder" };
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

            if (dialog.ShowDialog() == CommonFileDialogResult.Ok)
            {
                var resolution = TitanfallManager.ResolveGameRoot(dialog.FileName);
                if (!resolution.IsUsableDestination)
                {
                    MessageBox.Show(
                        this,
                        resolution.Message,
                        "Invalid Game Folder",
                        MessageBoxButton.OK,
                        MessageBoxImage.Warning);
                    return;
                }

                PathTextBox.Text = resolution.ResolvedRoot;
                SelectedPath = resolution.ResolvedRoot;
                UpdatePlayOrInstallButton();
            }

        }

        private void PathTextBox_TextChanged(object sender, TextChangedEventArgs e)
        {
            SelectedPath = PathTextBox.Text;
            UpdateInstructionsText();
            UpdatePlayOrInstallButton();
        }

        private void UpdateInstructionsText()
        {
            // ... (implementation unchanged)
            string path = PathTextBox.Text;

            InstructionsTextBlock.Text = string.Empty;

            Run initialText = new Run("Select your Titanfall game folder, or its parent when the game is in the direct r1delta child.\r\n");
            initialText.FontSize = 10;
            string drivePath = string.Empty;
            string sizeAvailable = string.Empty;
            string driveName = string.Empty;
            try 
            {
                if (!string.IsNullOrWhiteSpace(path))
                {
                    drivePath = Path.GetPathRoot(path);
                }
                if (!string.IsNullOrEmpty(drivePath))
                {
                    DriveInfo drive = new DriveInfo(drivePath);
                    double availableGB = drive.AvailableFreeSpace / (1024.0 * 1024 * 1024);
                    sizeAvailable = $"{availableGB:F1} GB";
                    driveName = drive.Name.TrimEnd('\\');
                }
            }
            catch { /* Ignore drive info errors */ }
            InstructionsTextBlock.Inlines.Add(initialText);
            InstructionsTextBlock.Inlines.Add("\n");
            Run openingParen = new Run("(");
            openingParen.FontSize = 7;
            InstructionsTextBlock.Inlines.Add(openingParen);
            Run gameSize = new Run("13 GB");
            gameSize.FontSize = 7;
            gameSize.FontWeight = FontWeights.Bold;
            InstructionsTextBlock.Inlines.Add(gameSize);
            Run middleofSentence = new Run(" will be required, ");
            middleofSentence.FontSize = 7;
            InstructionsTextBlock.Inlines.Add(middleofSentence);
            Run sizeAvailableRun = new Run(sizeAvailable);
            sizeAvailableRun.FontSize = 7;
            sizeAvailableRun.FontWeight = FontWeights.Bold;
            InstructionsTextBlock.Inlines.Add(sizeAvailableRun);
            Run availableOn = new Run(" available on ");
            availableOn.FontSize = 7;
            InstructionsTextBlock.Inlines.Add(availableOn);
            Run driveLetter = new Run(driveName);
            driveLetter.FontSize = 7;
            driveLetter.FontWeight = FontWeights.Bold;
            InstructionsTextBlock.Inlines.Add(driveLetter);
            Run closingParen = new Run(")");
            closingParen.FontSize = 7;
            InstructionsTextBlock.Inlines.Add(closingParen);
        }

        private const long RequiredSpaceBytes = 13L * 1024 * 1024 * 1024; // 13 GB
        private const string SetupWelcomeTitle = "Welcome to R1Delta!";

        // --- MODIFIED OnPathOkClicked ---
        private async void OnPathOkClicked(object sender, RoutedEventArgs e)
        {
            // An async void handler that throws terminates the process without a trace. Report instead.
            try
            {
                await ValidateAndInstallAsync();
            }
            catch (Exception ex)
            {
                LauncherLog.Write("setup failed while handling the install location", ex);
                ProgressPanel.Visibility = Visibility.Collapsed;
                PathSelectionPanel.Visibility = Visibility.Visible;
                SetupTitleBarText.Text = SetupWelcomeTitle;
                ShowError(
                    $"Setup hit an unexpected error:\n{ex.Message}\n\n" +
                    $"Details were written to:\n{LauncherLog.Path}");
            }
        }

        private async Task ValidateAndInstallAsync()
        {
            // Resolve the typed path once. An existing exact/direct-child root can
            // launch immediately; a normalized markerless root remains a valid
            // new or partial installation destination.
            var rootResolution = TitanfallManager.ResolveGameRoot(PathTextBox.Text);
            if (!rootResolution.IsUsableDestination)
            {
                MessageBox.Show(
                    this,
                    rootResolution.Message,
                    "Invalid Game Folder",
                    MessageBoxButton.OK,
                    MessageBoxImage.Warning);
                return;
            }

            string path = rootResolution.ResolvedRoot;
            PathTextBox.Text = path;
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

            // An install that lost a file launcher.dll needs (for example bin\x64_retail\vstdlib.dll) must go
            // through the download pass, which hash-checks every manifest entry and fetches only what is missing.
            string missingLaunchFiles = null;
            bool launchFilesIntact = !(rootResolution.Succeeded &&
                TitanfallManager.TryDescribeMissingLaunchFiles(rootResolution.ResolvedRoot, out missingLaunchFiles));
            bool isExistingInstallation = rootResolution.Succeeded && launchFilesIntact;

            // --- Show F4 Hint Message Box HERE if checkbox is checked ---
            // Determine the setting *before* potentially closing the window early
            bool showSetupNextTime = !(DoNotShowAgainCheckbox.IsChecked ?? false);
            if (!showSetupNextTime) // If the checkbox IS checked (meaning DON'T show setup)
            {
                MessageBox.Show(
                    this, // Owner window
                    "Setup will not be shown automatically on the next launch because the \"Do not show this window again\" box was checked.\n\n" +
                    "Hold the F4 key while starting the launcher if you need to access setup options again (e.g., change path, arguments).",
                    "Setup Hidden",
                    MessageBoxButton.OK,
                    MessageBoxImage.Information
                );
            }

            // --- 3. Handle Existing Installation Case (Shortcut) ---
            if (isExistingInstallation)
            {
                Debug.WriteLine($"Path '{path}' validated as existing installation. Skipping download prompt.");
                // --- Save Install Path to Registry (only path, not other settings) ---
                try { RegistryHelper.SaveInstallPath(SelectedPath); }
                catch (Exception ex) { Debug.WriteLine($"Error saving path to registry (shortcut path): {ex.Message}"); MessageBox.Show($"Could not save path to registry: {ex.Message}...", "Registry Save Warning", MessageBoxButton.OK, MessageBoxImage.Warning); }

                // --- Set return properties based on UI ---
                this.ShowSetupOnLaunch = showSetupNextTime; // Use the value determined above
                this.LaunchArguments = LaunchArgsTextBox.Text;

                // --- Close dialog ---
                this.DialogResult = true;
                Close();
                return;
            }
            // A drive/share root is never a valid destination for a fresh install. An already valid install
            // living at a root keeps working (it returns above as an existing one, or is repaired in place).
            if (TitanfallManager.TryGetDriveRootRefusal(path, rootResolution.Succeeded, out string driveRootRefusal))
            {
                MessageBox.Show(
                    this,
                    driveRootRefusal,
                    "Invalid Install Location",
                    MessageBoxButton.OK,
                    MessageBoxImage.Warning);
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
            double requiredGb = RequiredSpaceBytes / (1024.0 * 1024 * 1024);
            string confirmText = missingLaunchFiles != null
                ? $"This install is missing files the game needs: {missingLaunchFiles}.\n\nLocation: {path}\n\nSetup verifies every game file against its checksum and downloads only what is missing (at most about {requiredGb:F1} GB).\n\nDo you want to repair it now?"
                : $"The selected path appears valid for a new installation.\n\nLocation: {path}\n\nSetup will first check for the presence of existing downloaded files in that directory (if any).\n\nAfter that, Setup will now download approximately {requiredGb:F1} GB of game files.\n\nDo you want to proceed?";
            var confirmResult = MessageBox.Show(confirmText, "Confirm Download", MessageBoxButton.OKCancel, MessageBoxImage.Information);
            if (confirmResult != MessageBoxResult.OK) { return; }

            // --- Path is fully validated and confirmed for download ---
            // SelectedPath is already updated

            // --- 6. Start Download Process ---
            PathSelectionPanel.Visibility = Visibility.Collapsed; ProgressPanel.Visibility = Visibility.Visible; SetupTitleBarText.Text = "Setup in progress...";
            var operationCts = new CancellationTokenSource();
            _cts = operationCts;
            var setupTask = RunSetupOperationAsync(operationCts);
            _activeSetupTask = setupTask;

            bool success = await setupTask;
            if (ReferenceEquals(_activeSetupTask, setupTask))
                _activeSetupTask = null;

            // A close/cancel request owns closing the dialog after the operation
            // and all of its cleanup have finished.
            if (_closeTask != null)
                return;

            // --- 7. Set return properties and Close Window ---
            if (success)
            {
                this.ShowSetupOnLaunch = showSetupNextTime; // Use the value determined earlier
                this.LaunchArguments = LaunchArgsTextBox.Text;
            }
            // If success is false, the DialogResult will be false, and App.xaml.cs should handle exit.
            // We don't update the return properties if the download failed/cancelled.

            this.DialogResult = success;
            Close();
        }

        private async Task<bool> RunSetupOperationAsync(CancellationTokenSource operationCts)
        {
            // Return control to the click handler so task ownership is published
            // before setup can display UI or start a child process.
            await Task.Yield();

            try
            {
                if (!TitanfallManager.TryAcquireInstallOperationLease(
                    SelectedPath,
                    out IDisposable operationLease,
                    out string leaseError))
                {
                    Debug.WriteLine($"Could not acquire install-operation lease for '{SelectedPath}': {leaseError}");
                    StatusText.Text = "Another install operation is already using this destination.";
                    ShowError(leaseError);
                    return false;
                }
                using var heldOperationLease = operationLease;

                // The confirmation above is the ownership boundary. Create and
                // durably verify it before persisting the path or starting download.
                if (!TitanfallManager.TryEnsureManagedInstallOwnership(SelectedPath, out string markerError))
                {
                    Debug.WriteLine($"Could not establish managed-install ownership for '{SelectedPath}': {markerError}");
                    StatusText.Text = "Setup could not mark the selected destination.";
                    ShowError(
                        $"Setup could not safely mark this folder as an R1Delta-managed download destination.\n\n" +
                        $"{markerError}\n\nNo game files were downloaded.");
                    return false;
                }

                // --- Save Install Path to Registry BEFORE download starts ---
                try { RegistryHelper.SaveInstallPath(SelectedPath); Debug.WriteLine($"Download started. Saved install path to registry: {SelectedPath}"); }
                catch (Exception ex) { Debug.WriteLine($"Error saving path to registry (pre-download): {ex.Message}"); MessageBox.Show($"Could not save the selected path to the registry before downloading: {ex.Message}\nDownload will continue, but you might be prompted again later.", "Registry Save Warning", MessageBoxButton.OK, MessageBoxImage.Warning); }

                return await TitanfallManager.DownloadAllFilesWithResume(SelectedPath, this, operationCts.Token);
            }
            catch (OperationCanceledException)
            {
                StatusText.Text = "Download canceled.";
                Debug.WriteLine("Download operation canceled by user.");
                return false;
            }
            catch (Exception ex)
            {
                LauncherLog.Write("setup operation failed", ex);
                StatusText.Text = $"Error: {ex.Message}";
                ShowError(
                    $"Setup failed:\n{ex.Message}\n\n" +
                    $"Details were written to:\n{LauncherLog.Path}");
                return false;
            }
            finally
            {
                if (ReferenceEquals(_cts, operationCts))
                    _cts = null;
                operationCts.Dispose();
            }
        }

        private Task RequestCancelAndCloseAsync()
        {
            if (_closeTask != null)
                return _closeTask;

            var setupTask = _activeSetupTask;
            if (setupTask == null)
            {
                _allowClose = true;
                Close();
                return Task.CompletedTask;
            }

            var result = MessageBox.Show(this, "Are you sure you want to cancel the download?", "Confirm Cancellation", MessageBoxButton.YesNo, MessageBoxImage.Warning);
            if (result != MessageBoxResult.Yes)
                return Task.CompletedTask;

            CancelButton.IsEnabled = false;
            StatusText.Text = "Canceling...";
            OnCancelRequested?.Invoke();
            _closeTask = FinishCancellationAndCloseAsync(setupTask);
            return _closeTask;
        }

        private async Task FinishCancellationAndCloseAsync(Task setupTask)
        {
            try
            {
                await setupTask;
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Setup cleanup failed while closing: {ex}");
            }

            _allowClose = true;
            if (IsLoaded)
                DialogResult = false;
        }

        private void OnWindowClosing(object sender, CancelEventArgs e)
        {
            if (_allowClose || _activeSetupTask == null)
                return;

            e.Cancel = true;
            _ = RequestCancelAndCloseAsync();
        }

        private async void OnCancelDownloadClicked(object sender, RoutedEventArgs e)
        {
            await RequestCancelAndCloseAsync();
        }

        private async void OnCloseButtonClicked(object sender, RoutedEventArgs e)
        {
            if (_activeSetupTask == null)
            {
                _allowClose = true;
                Close();
                return;
            }

            await RequestCancelAndCloseAsync();
        }

        private void OnMinimizeButtonClicked(object sender, RoutedEventArgs e)
        {
            WindowState = WindowState.Minimized;
        }

        public void ReportStatus(InstallProgressStatus status)
        {
            if (status == null)
                throw new ArgumentNullException(nameof(status));

            if (!Dispatcher.CheckAccess())
            {
                Dispatcher.Invoke(() => ReportStatus(status));
                return;
            }

            _installStatus = status;
            RenderProgressStatus(_lastBytesDownloaded, _lastTotalBytes, _lastBytesPerSecond);
        }

        /// <summary>
        /// Reports aggregate install progress. The current status phase determines
        /// whether transfer speed/ETA or phase-specific work is displayed.
        /// </summary>
        public void ReportProgress(long bytesDownloaded, long totalBytes, double bytesPerSecond)
        {
            if (!Dispatcher.CheckAccess())
            {
                Dispatcher.Invoke(() => ReportProgress(bytesDownloaded, totalBytes, bytesPerSecond));
                return;
            }

            _lastBytesDownloaded = Math.Max(0, bytesDownloaded);
            _lastTotalBytes = Math.Max(0, totalBytes);
            _lastBytesPerSecond = Math.Max(0, bytesPerSecond);
            RenderProgressStatus(_lastBytesDownloaded, _lastTotalBytes, _lastBytesPerSecond);
        }

        private void RenderProgressStatus(long bytesDownloaded, long totalBytes, double bytesPerSecond)
        {
            StatusText.Text = InstallProgressText.Compose(
                _installStatus,
                bytesDownloaded,
                totalBytes,
                bytesPerSecond,
                out double percent);
            ProgressBar.Value = percent;
            CancelButton.IsEnabled = _installStatus.Phase != InstallProgressPhase.Complete;
        }

        public void ShowError(string message)
        {
            LauncherLog.Write($"setup error shown: {message}");
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

        private void DoNotShowAgainCheckbox_Checked(object sender, RoutedEventArgs e)
        {

        }
    }
}
