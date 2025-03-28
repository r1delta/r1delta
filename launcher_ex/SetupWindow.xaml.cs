using System;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls; // for TextChangedEventArgs
using System.Windows.Forms; // for FolderBrowserDialog
using R1Delta;             // so we can call TitanfallManager methods
using Microsoft.WindowsAPICodePack.Dialogs;
using System.IO;
using System.Reflection;
using System.Text.RegularExpressions;

namespace launcher_ex
{
    public partial class SetupWindow : Window, IInstallProgress
    {
        private CancellationTokenSource _cts;

        // We'll store the path the user chose here:
        public string SelectedPath { get; private set; } = string.Empty;

        // The IInstallProgress interface wants an Action for cancellation:
        public Action OnCancelRequested { get; set; }
        // Helper method to test if a folder is writable by attempting to create and delete a temporary file.
        private bool CanWriteToFolder(string folderPath)
        {
            try
            {
                string testFile = Path.Combine(folderPath, Path.GetRandomFileName());
                using (FileStream fs = File.Create(testFile, 1, FileOptions.DeleteOnClose))
                {
                    // File created successfully.
                }
                return true;
            }
            catch (Exception)
            {
                return false;
            }
        }

        public SetupWindow()
        {
            InitializeComponent();

            // Wire up cancellation action
            OnCancelRequested = () =>
            {
                _cts?.Cancel();
            };

            // Default the path to the current exe's directory
            // Get the current executable directory.
            string exePath = Assembly.GetExecutingAssembly().Location;
            string exeDirectory = Path.GetDirectoryName(exePath);

            // Check if the directory name matches the Squirrel version pattern (e.g. "app-1.3").
            string dirName = new DirectoryInfo(exeDirectory).Name;
            bool isSquirrel = Regex.IsMatch(dirName, @"^app-[0-9\.]+$");

            string defaultPath;
            if (isSquirrel)
            {
                // If running under Squirrel, use the persistent content folder.
                string localappData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
                defaultPath = Path.Combine(localappData, "r1delta", "content");
            }
            else
            {
                // Otherwise, default to the current executable directory.
                defaultPath = Path.Combine(exeDirectory, "content");
            }

            // Ensure the directory exists.
            if (!Directory.Exists(defaultPath))
            {
                Directory.CreateDirectory(defaultPath);
            }

            // Assign to TextBox and internal variable.
            PathTextBox.Text = defaultPath;
            SelectedPath = defaultPath;
            UpdateInstructionsText();
            PathTextBox.TextChanged += PathTextBox_TextChanged;
        }

        // -------------------------------------------------------
        // 1) User picks a folder
        // -------------------------------------------------------
        private void OnBrowseClicked(object sender, RoutedEventArgs e)
        {
            var dialog = new CommonOpenFileDialog
            {
                IsFolderPicker = true,
                Title = "Select your Titanfall install folder (or where you want it downloaded)."
            };

            if (dialog.ShowDialog() == CommonFileDialogResult.Ok)
            {
                PathTextBox.Text = dialog.FileName;
            }
        }

        // Event handler for when the text in the path box changes
        private void PathTextBox_TextChanged(object sender, TextChangedEventArgs e)
        {
            UpdateInstructionsText();
        }

        // Helper method to update the instructions text based on drive free space
        private void UpdateInstructionsText()
        {
            string path = PathTextBox.Text;
            string finalText = "To play, please point us to your Titanfall directory.\r\nDon’t have the game? It'll be installed there instead.\r\n(13 GB required";
            if (!string.IsNullOrWhiteSpace(path) && path.Length >= 2 && path[1] == ':')
            {
                char driveLetter = path[0];
                try
                {
                    DriveInfo drive = new DriveInfo(driveLetter.ToString());
                    double availableGB = drive.AvailableFreeSpace / (1024.0 * 1024 * 1024);
                    finalText += $", {availableGB:0} GB available on drive {driveLetter}:";
                    if (availableGB < 13)
                        finalText += " (!)";
                    finalText += ")";
                }
                catch (Exception)
                {
                    // If DriveInfo fails, fall back to default text
                    finalText += ")";
                }
            }
            else
            {
                finalText += ")";
            }
            InstructionsTextBlock.Text = finalText;
        }
        private const long RequiredSpaceBytes = 13L * 1024 * 1024 * 1024; // 13 GB

        private async void OnPathOkClicked(object sender, RoutedEventArgs e)
        {
            // Validate
            string path = PathTextBox.Text;
            if (string.IsNullOrWhiteSpace(path))
            {
                System.Windows.MessageBox.Show("Please select or enter a valid path.");
                return;
            }
            // Get the current executable directory
            string exePath = Assembly.GetExecutingAssembly().Location;
            string exeDirectory = Path.GetDirectoryName(exePath);

            if (string.Equals(Path.GetFullPath(path), Path.GetFullPath(exeDirectory), StringComparison.OrdinalIgnoreCase))
            {
                System.Windows.MessageBox.Show("The install path cannot be the current executable directory. Please choose a different folder.", "Invalid Path", MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }

            SelectedPath = path;
            // Check available free space if the path is in the form "C:\..."
            if (path.Length >= 2 && path[1] == ':')
            {
                char driveLetter = path[0];
                try
                {
                    DriveInfo drive = new DriveInfo(driveLetter.ToString());
                    long freeSpace = drive.AvailableFreeSpace;
                    if (freeSpace < RequiredSpaceBytes)
                    {
                        // Warn the user if there's not enough space.
                        var result = System.Windows.MessageBox.Show(
                            $"The selected drive {driveLetter}: only has {freeSpace / (1024 * 1024 * 1024)} GB available, which is less than the required 13 GB.\nDo you want to continue anyway?",
                            "Low Disk Space Warning",
                            MessageBoxButton.YesNo,
                            MessageBoxImage.Warning);
                        if (result != MessageBoxResult.Yes)
                        {
                            // User chose not to proceed.
                            return;
                        }
                    }
                }
                catch (Exception)
                {
                    // If DriveInfo fails, we continue without warning.
                }
            }
            // Create the folder if it doesn't exist
            if (!Directory.Exists(SelectedPath))
            {
                try
                {
                    Directory.CreateDirectory(SelectedPath);
                }
                catch (Exception ex)
                {
                    System.Windows.MessageBox.Show("Failed to create the destination folder: " + ex.Message, "Error", MessageBoxButton.OK, MessageBoxImage.Error);
                    return;
                }
            }
            // First, check write access by trying to create a temporary file.
            if (!CanWriteToFolder(SelectedPath))
            {
                System.Windows.MessageBox.Show("Cannot write to the selected destination folder. Please choose a different location.", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
                return;
            }


            // Hide the path panel, show the progress panel
            PathSelectionPanel.Visibility = Visibility.Collapsed;
            ProgressPanel.Visibility = Visibility.Visible;
            SetupTitleBarText.Text = "Setup in progress...";
            // Start the download (and possibly the resume logic)
            _cts = new CancellationTokenSource();

            bool success = false;
            try
            {
                success = await TitanfallManager.DownloadAllFilesWithResume(SelectedPath, this, _cts.Token);
            }
            catch (OperationCanceledException)
            {
                // user canceled
               // ShowError("Download canceled.");
            }
            catch (Exception ex)
            {
                // any other error
                ShowError($"Unexpected error: {ex.Message}");
            }

            // If success is false or an exception occurred, we treat that as “dialogResult = false”
            if (!success)
            {
                this.DialogResult = false;
                Close();
                return;
            }

            // Otherwise, success
            this.DialogResult = true;
            Close();
        }

        // -------------------------------------------------------
        // 2) If user clicks Cancel during the download
        // -------------------------------------------------------
        private void OnCancelDownloadClicked(object sender, RoutedEventArgs e)
        {
            // Show confirmation dialog before cancelling
            var result = System.Windows.MessageBox.Show(
                this, // Owner window
                "Are you sure you want to cancel the download?\nYou will need to finish it later before you can play.",
                "Confirm Cancellation",
                MessageBoxButton.YesNo,
                MessageBoxImage.Warning); // Use Warning icon

            // Check the result
            if (result == MessageBoxResult.Yes)
            {
                // User confirmed, proceed with cancellation
                OnCancelRequested?.Invoke();
            }

        }

        // -------------------------------------------------------
        // 3) IInstallProgress interface methods
        // -------------------------------------------------------

        public void ReportProgress(double percent, double bytesPerSecond)
        {
            this.Dispatcher.Invoke(() =>
            {
                ProgressBar.Value = percent;
                StatusText.Text = $"Downloading... {percent:0.0}% at {FormatBytesPerSec(bytesPerSecond)}";
            });
        }


        public void ShowError(string message)
        {
            // Called by the manager if something goes wrong
            this.Dispatcher.Invoke(() =>
            {
                System.Windows.MessageBox.Show(message, "Error", MessageBoxButton.OK, MessageBoxImage.Error);
            });
        }

        public void Dispose()
        {
            // Clean up resources if needed.
        }

        // Optional: format bytes/second for display
        private string FormatBytesPerSec(double bytesPerSec)
        {
            if (bytesPerSec < 1024) return $"{bytesPerSec:0.0} B/s";
            if (bytesPerSec < 1024 * 1024) return $"{bytesPerSec / 1024:0.0} KB/s";
            return $"{bytesPerSec / (1024 * 1024):0.0} MB/s";
        }
    }
}
