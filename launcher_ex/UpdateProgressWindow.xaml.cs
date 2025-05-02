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
using System.Windows.Documents;
using System.Windows.Media.Imaging;

namespace launcher_ex
{
    public partial class UpdateProgressWindow : Window
    {
        bool _canceled;
        bool _applying;
        public bool Canceled => _canceled;

        public UpdateProgressWindow()
        {
            _applying = false;
            InitializeComponent();
        }

        /// <summary>
        /// Call this (on any thread) to update the UI.
        /// </summary>
        public void UpdateProgress(int percent)
        {
            Dispatcher.Invoke(() =>
            {
                if (_applying)
                {
                    ProgressBar.Value = percent;
                    //StatusText.Text = $"Updating… {percent}%";
                    StatusText.Text = string.Empty;
                    Run downloadHeader = new Run("Updating...\n");
                    downloadHeader.FontSize = 24;
                    downloadHeader.FontWeight = FontWeights.Bold;
                    Run subtext = new Run("Downloading files: ");
                    subtext.FontSize = 12;
                    subtext.FontWeight = FontWeights.Regular;
                    Run percentage = new Run(percent.ToString());
                    percentage.FontSize = 12;

                    StatusText.Inlines.Add(downloadHeader);
                    StatusText.Inlines.Add(subtext);
                    StatusText.Inlines.Add(percentage);
                }
                else
                {
                    ProgressBar.Value = percent;
                    //StatusText.Text = $"Updating… {percent}%";
                    StatusText.Text = string.Empty;
                    Run downloadHeader = new Run("Updating...\n");
                    downloadHeader.FontSize = 24;
                    downloadHeader.FontWeight = FontWeights.Bold;
                    Run subtext = new Run("Applying updated files: ");
                    subtext.FontSize = 12;
                    subtext.FontWeight = FontWeights.Regular;
                    Run percentage = new Run(percent.ToString());
                    percentage.FontSize = 12;

                    StatusText.Inlines.Add(downloadHeader);
                    StatusText.Inlines.Add(subtext);
                    StatusText.Inlines.Add(percentage);

                    if (percent == 100)
                        _applying = true;
                }
            });
        }

        private void CancelButton_Click(object sender, RoutedEventArgs e)
        {
            _canceled = true;
            CancelButton.IsEnabled = false;
            StatusText.Text = "Cancelling…";
        }
    }
}