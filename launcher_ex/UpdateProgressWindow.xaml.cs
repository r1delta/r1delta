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
        public bool Canceled => _canceled;

        public UpdateProgressWindow()
        {
            InitializeComponent();
        }

        /// <summary>
        /// Call this (on any thread) to update the UI.
        /// </summary>
        public void UpdateProgress(int percent)
        {
            Dispatcher.Invoke(() =>
            {
                ProgressBar.Value = percent;
                //StatusText.Text = $"Updating… {percent}%";
                StatusText.Text = string.Empty;
                Run downloadHeader = new Run("Updating...\n");
                downloadHeader.FontSize = 24;
                downloadHeader.FontWeight = FontWeights.Bold;
                Run percentage = new Run(percent.ToString());
                percentage.FontSize = 12;

                StatusText.Inlines.Add(downloadHeader);
                StatusText.Inlines.Add(percentage);
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