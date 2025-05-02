using System;
using System.Windows;

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
                StatusText.Text = $"Updating… {percent}%";
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