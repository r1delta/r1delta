using System;
using System.Collections.Generic;
using System.Text;
using R1Delta;

namespace launcher_ex
{
    /// <summary>
    /// Builds the progress text of the setup window from an install status and the byte counters, so the
    /// wording of every phase (including the checking phases that used to look stalled) is testable without
    /// a window.
    /// </summary>
    internal static class InstallProgressText
    {
        internal static string Compose(
            InstallProgressStatus status,
            long bytesDownloaded,
            long totalBytes,
            double bytesPerSecond,
            out double percent)
        {
            percent = totalBytes > 0
                ? (double)bytesDownloaded * 100.0 / totalBytes
                : 0;
            percent = Math.Max(0, Math.Min(100, percent));

            string progressDetail = totalBytes > 0
                ? $"{percent:0.0}% ({FormatBytes(bytesDownloaded)} / {FormatBytes(totalBytes)})"
                : $"{percent:0.0}%";

            var lines = new List<string> { $"{GetPhaseHeading(status)}: {progressDetail}" };
            if (!string.IsNullOrEmpty(status.Detail))
                lines.Add(status.Detail);

            string rateLabel = GetRateLabel(status);
            if (rateLabel != null)
            {
                string eta = "--:--";
                if (bytesPerSecond > 1 && totalBytes > bytesDownloaded)
                    eta = FormatEta(TimeSpan.FromSeconds((totalBytes - bytesDownloaded) / bytesPerSecond));
                else if (totalBytes > 0 && bytesDownloaded >= totalBytes)
                    eta = "Done";

                lines.Add($"{rateLabel}: {FormatBytesPerSec(bytesPerSecond)} | ETA: {eta}");
            }
            lines.Add("Setup will launch the game automatically when complete.");
            return string.Join("\n", lines);
        }

        /// <summary>
        /// Network transfers report download speed; checking files on disk (before and after the download)
        /// reports how fast the files are being read. Other phases have no meaningful rate.
        /// </summary>
        internal static string GetRateLabel(InstallProgressStatus status)
        {
            switch (status.Phase)
            {
                case InstallProgressPhase.Download:
                case InstallProgressPhase.Fallback:
                    return status.TransferPhase == DownloadTransferPhase.Downloading ? "Speed" : null;
                case InstallProgressPhase.Preflight:
                case InstallProgressPhase.Verification:
                    return "Checking speed";
                default:
                    return null;
            }
        }

        internal static string FormatBytesPerSec(double bytesPerSec)
        {
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
        internal static string FormatEta(TimeSpan eta)
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

        /// <summary> Formats byte count into B/KB/MB/GB string. </summary>
        internal static string FormatBytes(long bytes)
        {
            if (bytes < 0) bytes = 0;
            const double KB = 1024.0;
            const double MB = KB * 1024.0;
            const double GB = MB * 1024.0;

            if (bytes < KB) return $"{bytes} B";
            if (bytes < MB) return $"{bytes / KB:F1} KB";
            if (bytes < GB) return $"{bytes / MB:F1} MB";
            return $"{bytes / GB:F1} GB";
        }

        internal static string GetPhaseHeading(InstallProgressStatus status)
        {
            switch (status.Phase)
            {
                case InstallProgressPhase.Preflight:
                    return "Checking existing files";
                case InstallProgressPhase.Resume:
                    return "Preparing resumable files";
                case InstallProgressPhase.Download:
                    return GetTransferHeading(GetBackendName(status.Backend), false, status);
                case InstallProgressPhase.Fallback:
                    return GetTransferHeading(GetBackendName(status.Backend), true, status);
                case InstallProgressPhase.Verification:
                    return status.VerificationPass > 0
                        ? $"Verifying downloaded files (pass {status.VerificationPass}/{status.MaxVerificationPasses})"
                        : "Verifying downloaded files";
                case InstallProgressPhase.ChecksumRepair:
                    return status.VerificationPass > 0
                        ? $"Repairing files that failed verification (after pass {status.VerificationPass})"
                        : "Repairing files that failed verification";
                case InstallProgressPhase.Complete:
                    return "Setup complete";
                default:
                    return "Setup in progress";
            }
        }

        private static string GetTransferHeading(
            string backendName,
            bool isFallback,
            InstallProgressStatus status)
        {
            string attempt = status.Attempt > 0 && status.MaxAttempts > 0
                ? $" (attempt {status.Attempt}/{status.MaxAttempts})"
                : string.Empty;
            string transferName = isFallback ? backendName + " fallback" : backendName;

            switch (status.TransferPhase)
            {
                case DownloadTransferPhase.Preparing:
                    return $"Preparing {transferName} download{attempt}";
                case DownloadTransferPhase.Downloading:
                    return $"Downloading with {transferName}{attempt}";
                case DownloadTransferPhase.TransferComplete:
                    return $"Finishing {transferName} transfer{attempt}";
                case DownloadTransferPhase.Failed:
                    return $"{transferName} download attempt failed{attempt}";
                default:
                    return $"{transferName} transfer{attempt}";
            }
        }

        private static string GetBackendName(DownloadBackend? backend)
        {
            switch (backend)
            {
                case DownloadBackend.Curl:
                    return "curl";
                case DownloadBackend.HttpClient:
                    return "HTTP";
                default:
                    return "network";
            }
        }
    }
}
