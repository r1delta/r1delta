using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes; // Required for FILETIME
using System.Diagnostics; // For Debug.WriteLine
using System.Linq; // For FirstOrDefault
using usis.Net.Bits; // Use types from the BITS library

namespace BitsCleanup
{
    /// <summary>
    /// Provides functionality to remove stale Background Intelligent Transfer Service (BITS) jobs.
    /// Primarily targets Firefox/Thunderbird updater jobs that may remain in a 'TRANSFERRED' state
    /// indefinitely, as described in Bug 1856462 (https://bugzilla.mozilla.org/show_bug.cgi?id=1856462),
    /// and also cleans up potentially orphaned or old R1Delta download jobs.
    /// </summary>
    public static class BitsJanitor
    {
        // Constant for the R1Delta job name prefix
        private const string R1DeltaJobPrefix = "R1Delta|";

        /// <summary>
        /// Scans all BITS jobs accessible to the current user context (or all users if run with sufficient privileges)
        /// and cancels jobs that match the specified predicate.
        /// </summary>
        /// <param name="shouldDelete">A function that determines if a given job should be deleted.
        /// If null, defaults to <see cref="DefaultFirefoxPredicate"/>.</param>
        /// <param name="log">The TextWriter to log output to. If null, defaults to a writer that uses Debug.WriteLine.</param>
        public static void PurgeStaleJobs(
            Func<BackgroundCopyJob, string, BackgroundCopyJobState, bool> shouldDelete, // Removed default null, predicate is now required
            TextWriter log = null)
        {
            // Use Debug logger if none provided
            if (log == null)
            {
                log = new DebugTextWriter(); // Use wrapper for Debug.WriteLine
            }
            // Ensure a predicate is provided
            if (shouldDelete == null)
            {
                log.WriteLine("[BitsJanitor] ERROR: PurgeStaleJobs called with a null predicate. Aborting cleanup.");
                // Optionally throw an exception: throw new ArgumentNullException(nameof(shouldDelete));
                return;
            }


            BackgroundCopyManager mgr = null;
            // IEnumerable<BackgroundCopyJob> enumJobs = null; // Changed from IEnumBackgroundCopyJobs
            // BackgroundCopyJob currentJob = null; // Defined inside loop

            try
            {
                log.WriteLine("[BitsJanitor] Connecting to BITS Manager...");
                // Obtain the BITS Manager COM object using the library's connect method
                mgr = BackgroundCopyManager.Connect();
                log.WriteLine("[BitsJanitor] Connected. Enumerating jobs...");

                // Enumerate jobs (false means current user)
                var enumJobs = mgr.EnumerateJobs(false); // Returns IEnumerable<BackgroundCopyJob>

                if (enumJobs == null) // Should return empty enumerable, not null, but check anyway
                {
                    log.WriteLine("[BitsJanitor] No BITS jobs found to enumerate.");
                    return;
                }

                int cleanedCount = 0;
                int totalCount = 0;
                // GetCount() might not be efficient or available on IEnumerable, count manually
                // log.WriteLine($"[BitsJanitor] Evaluating jobs...");


                // Iterate through the jobs using foreach
                foreach (var currentJob in enumJobs)
                {
                    totalCount++;
                    Guid jobId = Guid.Empty;
                    string jobName = "[Error Getting Name]";
                    BackgroundCopyJobState jobState = BackgroundCopyJobState.Error; // Default to error state

                    try
                    {
                        // Retrieve job details safely using the wrapper properties/methods
                        try { jobId = currentJob.Id; } catch { /* Ignore */ }
                        try { jobName = currentJob.DisplayName; } catch { /* Ignore */ } // Can be null or empty
                        try { jobState = currentJob.State; } catch { /* Ignore */ }

                        // Check if the job should be deleted based on the predicate
                        // Pass the BackgroundCopyJob object itself
                        if (shouldDelete(currentJob, jobName ?? string.Empty, jobState))
                        {
                            log.WriteLine($"[BitsJanitor] Cancelling Job {jobId} ('{jobName ?? "[No Name]"}' - State: {jobState}) matching predicate.");
                            try
                            {
                                currentJob.Cancel(); // Attempt to cancel the job
                                cleanedCount++;
                                log.WriteLine($"[BitsJanitor] ✔ Cancelled {jobId}");
                            }
                            catch (COMException cancelEx)
                            {
                                log.WriteLine($"[BitsJanitor] WARNING: Failed to cancel job {jobId}: {cancelEx.Message} (0x{cancelEx.ErrorCode:X})");
                            }
                        }
                        // Optional: Log jobs that were evaluated but not removed
                        // else
                        // {
                        //     log.WriteLine(string.Format("ℹ Skipped {0} ({1}) - State: {2}", jobId, jobName ?? "[No Name]", jobState));
                        // }
                    }
                    catch (COMException comEx)
                    {
                        // Log errors encountered while processing a specific job, but continue processing others
                        log.WriteLine($"[BitsJanitor] WARNING: COM Error processing job {jobId}: {comEx.Message} (0x{comEx.ErrorCode:X})");
                    }
                    catch (Exception ex)
                    {
                        // Log unexpected errors for a specific job
                        log.WriteLine($"[BitsJanitor] WARNING: Error processing job {jobId}: {ex.Message}");
                    }
                    finally
                    {
                        // Dispose the job wrapper, which releases the COM object
                        currentJob?.Dispose();
                    }
                }

                log.WriteLine($"\n[BitsJanitor] Summary for current predicate: {cleanedCount} out of {totalCount} evaluated BITS jobs cancelled.");
            }
            catch (COMException comEx)
            {
                // Log errors related to initializing BITS or enumerating jobs
                log.WriteLine($"[BitsJanitor] FATAL COM Error: {comEx.Message} (0x{comEx.ErrorCode:X})");
                log.WriteLine("[BitsJanitor] Ensure the BITS service is running and you have necessary permissions.");
            }
            catch (Exception ex)
            {
                // Log other fatal errors
                log.WriteLine($"[BitsJanitor] FATAL Error: {ex}");
            }
            finally
            {
                // Ensure the manager COM object is released
                log.WriteLine("[BitsJanitor] Releasing BITS Manager...");
                mgr?.Dispose(); // Dispose the manager wrapper
                log.WriteLine("[BitsJanitor] Cleanup finished for current predicate.");
            }
        }

        /// <summary>
        /// Default predicate targeting stale Mozilla update jobs as discussed in Bug 1856462.
        /// Specifically looks for jobs named "MozillaUpdate..." OR jobs that are stuck in the TRANSFERRED state.
        /// Does NOT target R1Delta jobs by default.
        /// </summary>
        public static bool DefaultFirefoxPredicate(BackgroundCopyJob job, string name, BackgroundCopyJobState state) // Updated type signature
        {
            // We are specifically interested in Mozilla jobs or jobs have finished downloading
            // but haven't been completed/acknowledged by the application, leaving them stuck.
            bool isMozilla = !string.IsNullOrEmpty(name) && name.StartsWith("MozillaUpdate", StringComparison.OrdinalIgnoreCase);
            bool isStuckTransferred = state == BackgroundCopyJobState.Transferred;

            // Don't log here, let the caller decide verbosity
            // if (isMozilla) Debug.WriteLine($"[BitsJanitor Predicate] Found Mozilla job: {name}");
            // if (isStuckTransferred) Debug.WriteLine($"[BitsJanitor Predicate] Found job stuck in Transferred state: {name}");

            return isMozilla || isStuckTransferred;
        }

        /// <summary>
        /// Combined predicate that includes DefaultFirefoxPredicate logic AND R1Delta-specific cleanup.
        /// - Cleans up non-R1Delta jobs matching DefaultFirefoxPredicate (Mozilla* or stuck TRANSFERRED).
        /// - Cleans up R1Delta jobs if:
        ///   - The job is stuck in TRANSFERRED state.
        ///   - The job hasn't been modified in over 7 days.
        /// Does NOT clean up based on target file existence anymore (handled by Orphaned predicate).
        /// </summary>
        public static bool CombinedCleanupPredicate(BackgroundCopyJob job, string name, BackgroundCopyJobState state)
        {
            // 1) Check non-R1Delta jobs using the Firefox predicate
            if (string.IsNullOrEmpty(name) || !name.StartsWith(R1DeltaJobPrefix, StringComparison.OrdinalIgnoreCase))
            {
                if (DefaultFirefoxPredicate(job, name, state))
                {
                    Debug.WriteLine($"[BitsJanitor Predicate] Matched non-R1Delta job '{name}' via DefaultFirefoxPredicate.");
                    return true;
                }
                return false;
            }

            // 2) R1Delta-specific cleanup logic (excluding orphaned check):
            Debug.WriteLine($"[BitsJanitor Predicate] Evaluating R1Delta job for general cleanup: {name} (State: {state})");

            // b) TRANSFERRED but never completed
            if (state == BackgroundCopyJobState.Transferred)
            {
                Debug.WriteLine($"[BitsJanitor Predicate] Matched R1Delta job '{name}': Stuck in TRANSFERRED state.");
                return true;
            }

            // c) Older than 7 days (based on modification time)
            try
            {
                var jobTimes = job.RetrieveTimes();
                // Use UtcNow for comparison consistency
                var modTimeUtc = jobTimes.ModificationTime.ToUniversalTime();
                // Check if ModificationTime is valid (not default/unspecified) before comparing
                if (jobTimes.ModificationTime != default(DateTime) && jobTimes.ModificationTime.Kind != DateTimeKind.Unspecified)
                {
                    var age = DateTime.UtcNow - modTimeUtc;
                    if (age > TimeSpan.FromDays(7))
                    {
                        Debug.WriteLine($"[BitsJanitor Predicate] Matched R1Delta job '{name}': Too old (Modified UTC {modTimeUtc}, Age: {age.TotalDays:F1} days).");
                        return true;
                    }
                }
                else
                {
                    // Log if the modification time seems invalid, might indicate an issue with the job itself.
                    Debug.WriteLine($"[BitsJanitor Predicate] R1Delta job '{name}' has invalid modification time ({jobTimes.ModificationTime}). Skipping age check.");
                }
            }
            catch (COMException ex)
            {
                Debug.WriteLine($"[BitsJanitor Predicate] WARNING: COM Error getting times for job {name}: {ex.Message} (0x{ex.ErrorCode:X}). Skipping age check.");
            }
            catch (Exception ex) // Catch potential timezone conversion errors etc.
            {
                Debug.WriteLine($"[BitsJanitor Predicate] WARNING: Error getting/processing times for job {name}: {ex.Message}. Skipping age check.");
            }


            // If none of the cleanup conditions met, keep the job.
            Debug.WriteLine($"[BitsJanitor Predicate] No general cleanup needed for R1Delta job: {name}");
            return false;
        }

        /// <summary>
        /// Creates a predicate function that identifies R1Delta BITS jobs associated with a *different*
        /// installation directory than the one provided.
        /// Assumes job names are formatted like "R1Delta|C:\Path\To\Install\Dir\vpk\file.vpk".
        /// </summary>
        /// <param name="currentInstallDir">The absolute, normalized path to the current installation directory.</param>
        /// <returns>A predicate function for use with PurgeStaleJobs.</returns>
        public static Func<BackgroundCopyJob, string, BackgroundCopyJobState, bool> CreateOrphanedR1DeltaPredicate(string currentInstallDir)
        {
            // Normalize the path for reliable comparison (e.g., ensure trailing slash consistency)
            // Path.TrimEndingDirectorySeparator is available in .NET Core 3.0+ / .NET 5+
            // For broader compatibility, we can manually ensure it doesn't end with a slash unless it's a root (e.g., "C:\")
            string normalizedInstallDir = Path.GetFullPath(currentInstallDir).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            // Handle root case like "C:\" which becomes "C:" after trimming
             if (normalizedInstallDir.Length == 2 && normalizedInstallDir[1] == ':')
             {
                 normalizedInstallDir += Path.DirectorySeparatorChar;
             }

            // Construct the expected prefix for jobs belonging to the *current* directory
            string currentJobPrefix = $"{R1DeltaJobPrefix}{normalizedInstallDir}{Path.DirectorySeparatorChar}".Replace("\\","/");
            // Use OrdinalIgnoreCase for case-insensitive file system paths
            var comparisonType = StringComparison.OrdinalIgnoreCase;

            Debug.WriteLine($"[BitsJanitor Predicate] Creating Orphaned predicate for install dir: '{normalizedInstallDir}'");
            Debug.WriteLine($"[BitsJanitor Predicate] -> Will keep jobs starting with: '{currentJobPrefix}'");

            // Return the actual predicate function (lambda)
            return (job, name, state) =>
            {
                // Basic check: Is it an R1Delta job at all?
                if (string.IsNullOrEmpty(name) || !name.StartsWith(R1DeltaJobPrefix, comparisonType))
                {
                    return false; // Not an R1Delta job, ignore.
                }

                // Check: Does it belong to the CURRENT installation directory?
                // We expect the job name to be "R1Delta|DRIVE:\path\to\install\actual\file.ext"
                // So we check if the name starts with "R1Delta|DRIVE:\path\to\install\"
                if (name.StartsWith(currentJobPrefix, comparisonType))
                {
                    // Belongs to the current directory, keep it (don't delete).
                    return false;
                }

                // It IS an R1Delta job, but it does NOT belong to the current directory.
                Debug.WriteLine($"[BitsJanitor Predicate] Matched Orphaned R1Delta job: '{name}' (Does not match current prefix '{currentJobPrefix}')");
                return true; // Delete it.
            };
        }


        #region COM Interop Definitions for BITS (REMOVED)
        // Removed all manual COM definitions. Relying on usis.Net.Bits library.
        #endregion

        // Helper class to redirect Console.Write/WriteLine to Debug.Write/WriteLine
        private class DebugTextWriter : TextWriter
        {
            public override System.Text.Encoding Encoding => System.Text.Encoding.UTF8;
            public override void WriteLine(string value) => Debug.WriteLine(value);
            public override void Write(string value) => Debug.Write(value);
            // Override WriteLine() without args to avoid ambiguity if base class has it
            public override void WriteLine() => Debug.WriteLine(string.Empty);
        }
    }
}
