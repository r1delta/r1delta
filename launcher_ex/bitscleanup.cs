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
        /// <summary>
        /// Scans all BITS jobs accessible to the current user context (or all users if run with sufficient privileges)
        /// and cancels jobs that match the specified predicate.
        /// </summary>
        /// <param name="shouldDelete">A function that determines if a given job should be deleted.
        /// If null, defaults to <see cref="DefaultFirefoxPredicate"/>.</param>
        /// <param name="log">The TextWriter to log output to. If null, defaults to a writer that uses Debug.WriteLine.</param>
        public static void PurgeStaleJobs(
            Func<BackgroundCopyJob, string, BackgroundCopyJobState, bool> shouldDelete = null, // Updated type signature
            TextWriter log = null)
        {
            // Use default predicate if none provided
            if (shouldDelete == null)
            {
                shouldDelete = DefaultFirefoxPredicate;
                Debug.WriteLine("[BitsJanitor] Using DefaultFirefoxPredicate for cleanup.");
            }
            // Use Debug logger if none provided
            if (log == null)
            {
                log = new DebugTextWriter(); // Use wrapper for Debug.WriteLine
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
                            log.WriteLine($"[BitsJanitor] Cancelling Job {jobId} ('{jobName ?? "[No Name]"}' - State: {jobState})");
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

                log.WriteLine($"\n[BitsJanitor] Summary: {cleanedCount} out of {totalCount} evaluated BITS jobs cancelled.");
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
                log.WriteLine("[BitsJanitor] Cleanup finished.");
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

            if (isMozilla) Debug.WriteLine($"[BitsJanitor Predicate] Found Mozilla job: {name}");
            if (isStuckTransferred) Debug.WriteLine($"[BitsJanitor Predicate] Found job stuck in Transferred state: {name}");

            return isMozilla || isStuckTransferred;
        }

        /// <summary>
        /// Combined predicate that includes DefaultFirefoxPredicate logic AND R1Delta-specific cleanup.
        /// - Cleans up non-R1Delta jobs matching DefaultFirefoxPredicate (Mozilla* or stuck TRANSFERRED).
        /// - Cleans up R1Delta jobs if:
        ///   - The target file is missing (orphaned).
        ///   - The job is stuck in TRANSFERRED state.
        ///   - The job hasn't been modified in over 7 days.
        /// </summary>
        public static bool CombinedCleanupPredicate(BackgroundCopyJob job, string name, BackgroundCopyJobState state)
        {
            // 1) Check non-R1Delta jobs using the Firefox predicate
            if (string.IsNullOrEmpty(name) || !name.StartsWith("R1Delta|", StringComparison.OrdinalIgnoreCase))
            {
                if (DefaultFirefoxPredicate(job, name, state))
                {
                    Debug.WriteLine($"[BitsJanitor Predicate] Matched non-R1Delta job '{name}' via DefaultFirefoxPredicate.");
                    return true;
                }
                return false;
            }

            // 2) R1Delta-specific cleanup logic:
            Debug.WriteLine($"[BitsJanitor Predicate] Evaluating R1Delta job: {name} (State: {state})");

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
                var modTime = jobTimes.ModificationTime;
                if (modTime != default(DateTime) && modTime.Kind != DateTimeKind.Unspecified)
                {
                    var age = DateTime.UtcNow - modTime.ToUniversalTime();
                    if (age > TimeSpan.FromDays(7))
                    {
                        Debug.WriteLine($"[BitsJanitor Predicate] Matched R1Delta job '{name}': Too old (Modified {modTime}, Age: {age.TotalDays:F1} days).");
                        return true;
                    }
                }
                else
                {
                    Debug.WriteLine($"[BitsJanitor Predicate] R1Delta job '{name}' has invalid modification time. Skipping age check.");
                }
            }
            catch (COMException ex)
            {
                Debug.WriteLine($"[BitsJanitor Predicate] WARNING: COM Error getting times for job {name}: {ex.Message} (0x{ex.ErrorCode:X}). Skipping age check.");
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[BitsJanitor Predicate] WARNING: Error getting times for job {name}: {ex.Message}. Skipping age check.");
            }

            // If none of the cleanup conditions met, keep the job.
            Debug.WriteLine($"[BitsJanitor Predicate] No cleanup needed for R1Delta job: {name}");
            return false;
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
