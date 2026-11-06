using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;

namespace launcher_ex
{
    /// <summary>
    /// Append-only diagnostic log of the launcher and setup. Every failure that would otherwise end
    /// the process without a visible trace is recorded here, and dialogs name the file.
    /// </summary>
    internal static class LauncherLog
    {
        /// <summary>Overrides the log directory (used by tests and for support sessions).</summary>
        internal const string LogDirectoryEnvironmentVariable = "R1DELTA_LAUNCHER_LOG_DIR";
        internal const string LogFileName = "launcher.log";
        internal const long MaxLogBytes = 1024 * 1024;
        private const string LogDirectoryName = "R1DeltaLauncher";
        private const string LogSubdirectoryName = "logs";

        private static readonly object s_lock = new object();
        private static string s_path;
        private static bool s_headerWritten;

        /// <summary>Full path of the log file that receives (or would receive) entries.</summary>
        internal static string Path
        {
            get
            {
                lock (s_lock)
                    return ResolvePathLocked();
            }
        }

        /// <summary>
        /// Candidate log directories in preference order: the explicit override, the per-user local
        /// application data folder, then the temp folder.
        /// </summary>
        internal static string[] GetCandidateDirectories(string overrideDirectory, string localAppData, string tempDirectory)
        {
            var candidates = new System.Collections.Generic.List<string>();
            if (!string.IsNullOrWhiteSpace(overrideDirectory))
                candidates.Add(overrideDirectory);
            if (!string.IsNullOrWhiteSpace(localAppData))
                candidates.Add(System.IO.Path.Combine(localAppData, LogDirectoryName, LogSubdirectoryName));
            if (!string.IsNullOrWhiteSpace(tempDirectory))
                candidates.Add(System.IO.Path.Combine(tempDirectory, LogDirectoryName));
            return candidates.ToArray();
        }

        private static string ResolvePathLocked()
        {
            if (s_path != null)
                return s_path;

            string[] candidates = GetCandidateDirectories(
                Environment.GetEnvironmentVariable(LogDirectoryEnvironmentVariable),
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                System.IO.Path.GetTempPath());
            foreach (string directory in candidates)
            {
                try
                {
                    Directory.CreateDirectory(directory);
                    string candidate = System.IO.Path.Combine(directory, LogFileName);
                    RotateIfLarge(candidate);
                    using (new FileStream(candidate, FileMode.Append, FileAccess.Write, FileShare.ReadWrite | FileShare.Delete))
                    {
                    }
                    s_path = candidate;
                    return s_path;
                }
                catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException || ex is ArgumentException || ex is NotSupportedException)
                {
                    Debug.WriteLine($"Launcher log directory '{directory}' is not usable: {ex.Message}");
                }
            }

            // No directory is writable. Report the preferred location so dialogs still name something
            // truthful; Write() will keep failing quietly because there is nowhere to write.
            s_path = candidates.Length > 0
                ? System.IO.Path.Combine(candidates[0], LogFileName)
                : LogFileName;
            return s_path;
        }

        private static void RotateIfLarge(string logFile)
        {
            var info = new FileInfo(logFile);
            if (!info.Exists || info.Length <= MaxLogBytes)
                return;
            string previous = System.IO.Path.ChangeExtension(logFile, ".1.log");
            if (File.Exists(previous))
                File.Delete(previous);
            File.Move(logFile, previous);
        }

        internal static void Write(string message)
        {
            lock (s_lock)
            {
                try
                {
                    string path = ResolvePathLocked();
                    var text = new StringBuilder();
                    if (!s_headerWritten)
                    {
                        s_headerWritten = true;
                        text.AppendLine(
                            $"{Timestamp()} [{Process.GetCurrentProcess().Id}] ---- launcher session: " +
                            $"exe={AppContext.BaseDirectory} os={Environment.OSVersion.VersionString} " +
                            $"clr={Environment.Version} 64bit={Environment.Is64BitProcess} " +
                            $"args=\"{Environment.CommandLine}\"");
                    }
                    text.AppendLine($"{Timestamp()} [{Process.GetCurrentProcess().Id}] {message}");
                    File.AppendAllText(path, text.ToString(), Encoding.UTF8);
                }
                catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException || ex is ArgumentException || ex is NotSupportedException)
                {
                    Debug.WriteLine($"Could not write the launcher log: {ex.Message}");
                }
            }
            Debug.WriteLine(message);
        }

        internal static void Write(string context, Exception exception)
        {
            Write($"{context}: {exception}");
        }

        private static string Timestamp() => DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff");
    }

    /// <summary>
    /// Makes every way the launcher/setup can stop unexpectedly visible: a dialog that names the
    /// problem and the log file, and a log entry with the full exception.
    /// </summary>
    internal static class StartupDiagnostics
    {
        private const uint MB_OK = 0x00000000;
        private const uint MB_ICONERROR = 0x00000010;
        private const uint MB_ICONWARNING = 0x00000030;
        private const uint MB_SETFOREGROUND = 0x00010000;
        private const uint MB_TOPMOST = 0x00040000;
        private const int UnexpectedFailureExitCode = 1;

        [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern int MessageBoxW(IntPtr hWnd, string text, string caption, uint type);

        private static int s_fatalInProgress;

        /// <summary>Registers handlers for every unhandled-exception channel. Call before any other work.</summary>
        internal static void Install(Application application)
        {
            LauncherLog.Write("launcher started");

            AppDomain.CurrentDomain.UnhandledException += (sender, args) =>
            {
                var exception = args.ExceptionObject as Exception;
                ReportFatal("unhandled exception on a background thread", exception, args.ExceptionObject);
            };

            application.DispatcherUnhandledException += (sender, args) =>
            {
                ReportFatal("unhandled exception on the UI thread", args.Exception, null);
                args.Handled = true;
            };

            TaskScheduler.UnobservedTaskException += (sender, args) =>
            {
                // Not fatal by itself, but a faulted background task nobody awaited is exactly the kind
                // of failure that used to vanish: record it.
                LauncherLog.Write("unobserved task exception", args.Exception);
                args.SetObserved();
            };
        }

        /// <summary>
        /// Logs the failure, shows a dialog naming the log file, and ends the process. Used for
        /// failures the launcher cannot continue from.
        /// </summary>
        internal static void ReportFatal(string context, Exception exception, object rawException = null)
        {
            // A second failure while the first dialog is open (for example from another thread) would
            // stack dialogs on a process that is already ending.
            if (Interlocked.Exchange(ref s_fatalInProgress, 1) != 0)
            {
                LauncherLog.Write($"additional failure while reporting another ({context})", exception ?? new Exception(rawException?.ToString()));
                return;
            }

            if (exception != null)
                LauncherLog.Write(context, exception);
            else
                LauncherLog.Write($"{context}: {rawException}");

            ShowDialog(
                "R1Delta Launcher Error",
                BuildFatalMessage(context, exception, rawException, LauncherLog.Path),
                error: true);
            Environment.Exit(UnexpectedFailureExitCode);
        }

        internal static string BuildFatalMessage(string context, Exception exception, object rawException, string logPath)
        {
            string detail = exception != null
                ? $"{exception.GetType().Name}: {exception.Message}"
                : Convert.ToString(rawException);
            return
                "R1Delta hit an unexpected error and has to close.\n\n" +
                $"What happened: {context}\n" +
                $"{detail}\n\n" +
                $"Full details were written to:\n{logPath}\n\n" +
                "Include that file when asking for help in the R1Delta Discord.";
        }

        /// <summary>
        /// Shows a modal dialog that is foreground and topmost. A plain ownerless dialog can open behind
        /// other windows, which leaves a process that looks like it did nothing.
        /// </summary>
        internal static void ShowDialog(string title, string message, bool error)
        {
            MessageBoxW(
                IntPtr.Zero,
                message,
                title,
                MB_OK | (error ? MB_ICONERROR : MB_ICONWARNING) | MB_SETFOREGROUND | MB_TOPMOST);
        }
    }
}
