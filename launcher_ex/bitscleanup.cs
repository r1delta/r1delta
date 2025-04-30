using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes; // Required for FILETIME
using System.Diagnostics; // For Debug.WriteLine

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
            Func<IBackgroundCopyJob, string, BG_JOB_STATE, bool> shouldDelete = null,
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

            IBackgroundCopyManager mgr = null;
            IEnumBackgroundCopyJobs enumJobs = null;
            IBackgroundCopyJob currentJob = null; // Renamed from 'job' for clarity within the loop

            try
            {
                log.WriteLine("[BitsJanitor] Connecting to BITS Manager...");
                // Obtain the BITS Manager COM object
                Guid clsidBackgroundCopyManager = new Guid("4991D34B-80A1-4291-83B6-3328366B9097");
                Type managerType = Type.GetTypeFromCLSID(clsidBackgroundCopyManager, true);
                mgr = (IBackgroundCopyManager)Activator.CreateInstance(managerType);
                log.WriteLine("[BitsJanitor] Connected. Enumerating jobs...");

                // Enumerate jobs (0 means current user)
                mgr.EnumJobs(0, out enumJobs);

                if (enumJobs == null)
                {
                    log.WriteLine("[BitsJanitor] No BITS jobs found to enumerate.");
                    return;
                }

                uint fetchedCount;
                int cleanedCount = 0;
                int totalCount = 0;
                uint jobCount;
                try { jobCount = enumJobs.GetCount(); } catch { jobCount = 0; } // GetCount might fail
                log.WriteLine($"[BitsJanitor] Found {jobCount} total jobs. Evaluating...");


                // Iterate through the jobs
                // Note: enumJobs.Next returns 0 (S_OK) when successful.
                while (enumJobs.Next(1, out currentJob, out fetchedCount) == 0 && fetchedCount == 1)
                {
                    totalCount++;
                    Guid jobId = Guid.Empty;
                    string jobName = "[Error Getting Name]";
                    BG_JOB_STATE jobState = BG_JOB_STATE.BG_JOB_STATE_ERROR; // Default to error state

                    try
                    {
                        // Retrieve job details safely
                        try { currentJob.GetId(out jobId); } catch { /* Ignore */ }
                        try { currentJob.GetDisplayName(out jobName); } catch { /* Ignore */ } // Can be null or empty
                        try { currentJob.GetState(out jobState); } catch { /* Ignore */ }

                        // Check if the job should be deleted based on the predicate
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
                        // Release the current job's COM object inside the loop
                        if (currentJob != null)
                        {
                            Marshal.ReleaseComObject(currentJob);
                            currentJob = null; // Prevent accidental reuse or double-release
                        }
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
                // Ensure all COM objects are released, even if errors occurred
                log.WriteLine("[BitsJanitor] Releasing COM objects...");
                // Note: currentJob should be null here if the loop finished normally or hit the inner finally.
                // This handles cases where an exception occurred between Next() and the inner try.
                if (currentJob != null) { Marshal.ReleaseComObject(currentJob); currentJob = null; }
                if (enumJobs != null) { Marshal.ReleaseComObject(enumJobs); enumJobs = null; }
                if (mgr != null) { Marshal.ReleaseComObject(mgr); mgr = null; }
                log.WriteLine("[BitsJanitor] Cleanup finished.");
            }
        }

        /// <summary>
        /// Default predicate targeting stale Mozilla update jobs as discussed in Bug 1856462.
        /// Specifically looks for jobs named "MozillaUpdate..." OR jobs that are stuck in the TRANSFERRED state.
        /// Does NOT target R1Delta jobs by default.
        /// </summary>
        public static bool DefaultFirefoxPredicate(IBackgroundCopyJob job, string name, BG_JOB_STATE state)
        {
            // We are specifically interested in Mozilla jobs or jobs have finished downloading
            // but haven't been completed/acknowledged by the application, leaving them stuck.
            bool isMozilla = !string.IsNullOrEmpty(name) && name.StartsWith("MozillaUpdate", StringComparison.OrdinalIgnoreCase);
            bool isStuckTransferred = state == BG_JOB_STATE.BG_JOB_STATE_TRANSFERRED;

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
        public static bool CombinedCleanupPredicate(IBackgroundCopyJob job, string name, BG_JOB_STATE state)
        {
            // 1) Check non-R1Delta jobs using the Firefox predicate
            if (string.IsNullOrEmpty(name) || !name.StartsWith("R1Delta|", StringComparison.OrdinalIgnoreCase))
            {
                if (DefaultFirefoxPredicate(job, name, state))
                {
                    Debug.WriteLine($"[BitsJanitor Predicate] Matched non-R1Delta job '{name}' via DefaultFirefoxPredicate.");
                    return true;
                }
                // If not R1Delta and not matched by Firefox rule, keep it.
                return false;
            }

            // 2) R1Delta-specific cleanup logic:
            Debug.WriteLine($"[BitsJanitor Predicate] Evaluating R1Delta job: {name} (State: {state})");

            // a) Orphaned file check
            IEnumBackgroundCopyFiles filesEnum = null;
            IBackgroundCopyFile fileInfo = null;
            string targetPath = null;
            bool fileExists = false;
            try
            {
                job.EnumFiles(out filesEnum);
                uint fetched;
                if (filesEnum.Next(1, out fileInfo, out fetched) == 0 && fetched == 1) // S_OK = 0
                {
                    targetPath = fileInfo.LocalName;
                    if (!string.IsNullOrEmpty(targetPath))
                    {
                        fileExists = File.Exists(targetPath);
                    }
                }
            }
            catch (COMException ex) { Debug.WriteLine($"[BitsJanitor Predicate] COM Error checking file for job {name}: {ex.Message}"); }
            finally
            {
                if (fileInfo != null) { Marshal.ReleaseComObject(fileInfo); }
                if (filesEnum != null) { Marshal.ReleaseComObject(filesEnum); }
            }

            if (!fileExists)
            {
                Debug.WriteLine($"[BitsJanitor Predicate] Matched R1Delta job '{name}': Target file '{targetPath ?? "null"}' does not exist (orphaned).");
                return true;
            }

            // b) TRANSFERRED but never completed (should ideally be handled by the app, but cleanup as fallback)
            if (state == BG_JOB_STATE.BG_JOB_STATE_TRANSFERRED)
            {
                 Debug.WriteLine($"[BitsJanitor Predicate] Matched R1Delta job '{name}': Stuck in TRANSFERRED state.");
                 return true;
            }

            // c) Older than 7 days (based on modification time)
            try
            {
                job.GetTimes(out var times);
                // Combine FILETIME parts into a long for DateTime conversion
                long fileTimeUtc = ((long)times.ModificationTime.dwHighDateTime << 32) | (uint)times.ModificationTime.dwLowDateTime;
                if (fileTimeUtc > 0) // Ensure fileTime is valid
                {
                    var modTime = DateTime.FromFileTimeUtc(fileTimeUtc);
                    var age = DateTime.UtcNow - modTime;
                    if (age > TimeSpan.FromDays(7))
                    {
                        Debug.WriteLine($"[BitsJanitor Predicate] Matched R1Delta job '{name}': Too old (Modified {modTime}, Age: {age.TotalDays:F1} days).");
                        return true;
                    }
                }
            }
            catch (COMException ex) { Debug.WriteLine($"[BitsJanitor Predicate] COM Error getting times for job {name}: {ex.Message}"); }
            catch (ArgumentOutOfRangeException ex) { Debug.WriteLine($"[BitsJanitor Predicate] Error converting file time for job {name}: {ex.Message}"); }


            // If none of the R1Delta cleanup conditions met, keep the job.
            Debug.WriteLine($"[BitsJanitor Predicate] No cleanup needed for R1Delta job: {name}");
            return false;
        }


        #region COM Interop Definitions for BITS

        // --- Enums ---
        /// <summary>Flags for EnumJobs.</summary>
        [Flags] // EnumJobs flags can be combined, though we only use 0 or 1 here.
        public enum BITS_JOB_ENUM : uint
        {
            /// <summary>Enumerate jobs owned by the current user.</summary>
            CURRENT_USER = 0,
            /// <summary>Enumerate all jobs in the BITS queue (requires administrator privileges).</summary>
            ALL_USERS = 0x1
        }

        /// <summary>State of a BITS job.</summary>
        public enum BG_JOB_STATE : uint
        {
            BG_JOB_STATE_QUEUED = 0,
            BG_JOB_STATE_CONNECTING = 1,
            BG_JOB_STATE_TRANSFERRING = 2,
            BG_JOB_STATE_SUSPENDED = 3,
            BG_JOB_STATE_ERROR = 4,
            BG_JOB_STATE_TRANSIENT_ERROR = 5,
            BG_JOB_STATE_TRANSFERRED = 6, // State often seen for stuck Mozilla jobs
            BG_JOB_STATE_ACKNOWLEDGED = 7,
            BG_JOB_STATE_CANCELLED = 8
        }

        /// <summary>Type of a BITS job.</summary>
        public enum BG_JOB_TYPE : uint
        {
            BG_JOB_TYPE_DOWNLOAD = 0,
            BG_JOB_TYPE_UPLOAD = 1,
            BG_JOB_TYPE_UPLOAD_REPLY = 2
        }

        // --- Interfaces ---
        // Note: Method order in COM interfaces is critical and must match the VTable layout.

        /// <summary>COM interface for the BITS Manager.</summary>
        [ComImport, Guid("5CE34C0D-0DC9-4C1F-897C-DAA1B78CEE7C"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        public interface IBackgroundCopyManager
        {
            // Method 0: CreateJob
            void CreateJob([MarshalAs(UnmanagedType.LPWStr)] string displayName, BG_JOB_TYPE type, out Guid pJobId, [MarshalAs(UnmanagedType.Interface)] out IBackgroundCopyJob pJob);

            // Method 1: GetJob
            void GetJob(ref Guid jobID, [MarshalAs(UnmanagedType.Interface)] out IBackgroundCopyJob ppJob);

            // Method 2: EnumJobs (Used by janitor)
            void EnumJobs(uint dwFlags, [MarshalAs(UnmanagedType.Interface)] out IEnumBackgroundCopyJobs ppEnum);

            // Method 3: GetErrorDescription
            void GetErrorDescription([In, MarshalAs(UnmanagedType.Error)] int hResult, uint languageId, [MarshalAs(UnmanagedType.LPWStr)] out string ppErrorDescription);
        }

        /// <summary>COM interface for enumerating BITS jobs.</summary>
        [ComImport, Guid("1AF4F612-3B71-466F-8F58-7B6F73AC57AD"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        public interface IEnumBackgroundCopyJobs
        {
            // Method 0: Next (Used by janitor)
            // Returns S_OK (0) if the next element is fetched, S_FALSE (1) if fewer than requested elements remain.
            [PreserveSig]
            int Next(uint celt, [MarshalAs(UnmanagedType.Interface)] out IBackgroundCopyJob rgelt, out uint pceltFetched);

            // Method 1: Skip
            void Skip(uint celt);

            // Method 2: Reset
            void Reset();

            // Method 3: Clone
            void Clone([MarshalAs(UnmanagedType.Interface)] out IEnumBackgroundCopyJobs ppenum);

            // Method 4: GetCount (Used by janitor for logging)
            uint GetCount(); // Changed return type to uint based on documentation
        }

        /// <summary>COM interface for a BITS job.</summary>
        [ComImport, Guid("37668D37-507E-4160-9316-26306D150B12"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        public interface IBackgroundCopyJob
        {
            // --- Methods MUST be in VTable order ---

            // Method 0: AddFileSet
            void AddFileSet(uint cFileCount, /*BG_FILE_INFO[]*/ IntPtr pFileSet); // IntPtr used for simplicity

            // Method 1: AddFile
            void AddFile([MarshalAs(UnmanagedType.LPWStr)] string RemoteUrl, [MarshalAs(UnmanagedType.LPWStr)] string LocalName);

            // Method 2: EnumFiles (Used by janitor predicate)
            void EnumFiles([MarshalAs(UnmanagedType.Interface)] out IEnumBackgroundCopyFiles ppEnum);

            // Method 3: Suspend
            void Suspend();

            // Method 4: Resume
            void Resume();

            // Method 5: Cancel (Used by janitor)
            void Cancel();

            // Method 6: Complete
            void Complete();

            // Method 7: GetId (Used by janitor)
            void GetId(out Guid pVal);

            // Method 8: GetType
            void GetType(out BG_JOB_TYPE pVal);

            // Method 9: GetProgress
            void GetProgress(out _BG_JOB_PROGRESS pVal);

            // Method 10: GetTimes (Used by janitor predicate)
            void GetTimes(out _BG_JOB_TIMES pVal);

            // Method 11: GetState (Used by janitor)
            void GetState(out BG_JOB_STATE pVal);

            // Method 12: GetError
            // Needs definition for IBackgroundCopyError if used fully
            void GetError([MarshalAs(UnmanagedType.Interface)] out object ppError); // Using object for simplicity

            // Method 13: GetOwner
            void GetOwner([MarshalAs(UnmanagedType.LPWStr)] out string pVal);

            // Method 14: SetDisplayName
            void SetDisplayName([MarshalAs(UnmanagedType.LPWStr)] string Val);

            // Method 15: GetDisplayName (Used by janitor)
            void GetDisplayName([MarshalAs(UnmanagedType.LPWStr)] out string pVal);

            // --- Methods after GetDisplayName are omitted as they are not needed by the janitor ---
            // SetDescription, GetDescription, SetPriority, GetPriority, etc.
        }

        /// <summary>COM interface for enumerating files within a BITS job.</summary>
        [ComImport, Guid("CA51E165-C365-424C-8D41-24AAA4FF3C40"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        public interface IEnumBackgroundCopyFiles
        {
            // Method 0: Next (Used by janitor predicate)
            [PreserveSig]
            int Next(uint celt, [MarshalAs(UnmanagedType.Interface)] out IBackgroundCopyFile rgelt, out uint pceltFetched);

            // Method 1: Skip
            void Skip(uint celt);

            // Method 2: Reset
            void Reset();

            // Method 3: Clone
            void Clone([MarshalAs(UnmanagedType.Interface)] out IEnumBackgroundCopyFiles ppenum);

            // Method 4: GetCount
            uint GetCount();
        }

        /// <summary>COM interface representing a file within a BITS job.</summary>
        [ComImport, Guid("01B7BD23-FB88-4A77-8490-5891D3E4653A"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        public interface IBackgroundCopyFile
        {
            // Method 0: GetRemoteName (Not used by janitor)
            void GetRemoteName([MarshalAs(UnmanagedType.LPWStr)] out string pVal);

            // Method 1: GetLocalName (Used by janitor predicate)
            void GetLocalName([MarshalAs(UnmanagedType.LPWStr)] out string pVal);

            // Method 2: GetProgress (Not used by janitor)
            void GetProgress(out _BG_FILE_PROGRESS pVal);
        }


        // --- Structs ---
        // Define necessary structs used by the interface methods.

        /// <summary>Contains progress information for a BITS job.</summary>
        [StructLayout(LayoutKind.Sequential)]
        public struct _BG_JOB_PROGRESS
        {
            public ulong BytesTotal;
            public ulong BytesTransferred;
            public uint FilesTotal;
            public uint FilesTransferred;
        }

        /// <summary>Contains time-related information for a BITS job.</summary>
        [StructLayout(LayoutKind.Sequential)]
        public struct _BG_JOB_TIMES
        {
            public System.Runtime.InteropServices.ComTypes.FILETIME CreationTime;
            public System.Runtime.InteropServices.ComTypes.FILETIME ModificationTime;
            public System.Runtime.InteropServices.ComTypes.FILETIME TransferCompletionTime;
        }

        /// <summary>Contains progress information for a file within a BITS job.</summary>
        [StructLayout(LayoutKind.Sequential)]
        public struct _BG_FILE_PROGRESS
        {
            public ulong BytesTotal;
            public ulong BytesTransferred;
            public int Completed; // BOOL (0 or 1)
        }

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
