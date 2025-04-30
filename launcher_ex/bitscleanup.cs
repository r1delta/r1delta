using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes; // Required for FILETIME

namespace BitsCleanup
{
    /// <summary>
    /// Provides functionality to remove stale Background Intelligent Transfer Service (BITS) jobs.
    /// Primarily targets Firefox/Thunderbird updater jobs that may remain in a 'TRANSFERRED' state
    /// indefinitely, as described in Bug 1856462 (https://bugzilla.mozilla.org/show_bug.cgi?id=1856462).
    /// </summary>
    public static class BitsJanitor
    {
        /// <summary>
        /// Scans all BITS jobs accessible to the current user context (or all users if run with sufficient privileges)
        /// and cancels jobs that match the specified predicate.
        /// </summary>
        /// <param name="shouldDelete">A function that determines if a given job should be deleted.
        /// If null, defaults to <see cref="DefaultFirefoxPredicate"/>.</param>
        /// <param name="log">The TextWriter to log output to. If null, defaults to <see cref="Console.Out"/>.</param>
        public static void PurgeStaleJobs(
            Func<IBackgroundCopyJob, string, BG_JOB_STATE, bool> shouldDelete = null,
            TextWriter log = null)
        {
            // Use default predicate and logger if none are provided
            if (shouldDelete == null)
            {
                shouldDelete = DefaultFirefoxPredicate;
            }
            if (log == null)
            {
                log = Console.Out;
            }

            IBackgroundCopyManager mgr = null;
            IEnumBackgroundCopyJobs enumJobs = null;
            IBackgroundCopyJob currentJob = null; // Renamed from 'job' for clarity within the loop

            try
            {
                // Obtain the BITS Manager COM object
                Guid clsidBackgroundCopyManager = new Guid("4991D34B-80A1-4291-83B6-3328366B9097");
                Type managerType = Type.GetTypeFromCLSID(clsidBackgroundCopyManager, true);
                mgr = (IBackgroundCopyManager)Activator.CreateInstance(managerType);

                // Enumerate jobs (0 means current user)
                mgr.EnumJobs(0, out enumJobs);

                if (enumJobs == null)
                {
                    log.WriteLine("No BITS jobs found to enumerate.");
                    return;
                }

                uint fetchedCount;
                int cleanedCount = 0;
                int totalCount = 0;

                // Iterate through the jobs
                // Note: enumJobs.Next returns 0 (S_OK) when successful.
                while (enumJobs.Next(1, out currentJob, out fetchedCount) == 0 && fetchedCount == 1)
                {
                    totalCount++;
                    try
                    {
                        // Retrieve job details
                        Guid jobId;
                        string jobName;
                        BG_JOB_STATE jobState;

                        currentJob.GetId(out jobId);
                        currentJob.GetDisplayName(out jobName); // Can be null or empty
                        currentJob.GetState(out jobState);

                        // Check if the job should be deleted based on the predicate
                        if (shouldDelete(currentJob, jobName ?? string.Empty, jobState))
                        {
                            currentJob.Cancel(); // Attempt to cancel the job
                            cleanedCount++;
                            log.WriteLine(string.Format("✔ Cancelled {0} ({1}) - State: {2}", jobId, jobName ?? "[No Name]", jobState));
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
                        Guid errorJobId;
                        try { currentJob.GetId(out errorJobId); } catch { errorJobId = Guid.Empty; }
                        log.WriteLine(string.Format("[BitsJanitor] WARNING: COM Error processing job {0}: {1} (0x{2:X})",
                                                    errorJobId != Guid.Empty ? errorJobId.ToString() : "[Unknown ID]",
                                                    comEx.Message, comEx.ErrorCode));
                    }
                    catch (Exception ex)
                    {
                        // Log unexpected errors for a specific job
                        Guid errorJobId;
                        try { currentJob.GetId(out errorJobId); } catch { errorJobId = Guid.Empty; }
                        log.WriteLine(string.Format("[BitsJanitor] WARNING: Error processing job {0}: {1}",
                                                    errorJobId != Guid.Empty ? errorJobId.ToString() : "[Unknown ID]",
                                                    ex.Message));
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

                log.WriteLine(string.Format("\nSummary: {0} out of {1} BITS jobs cancelled.", cleanedCount, totalCount));
            }
            catch (COMException comEx)
            {
                // Log errors related to initializing BITS or enumerating jobs
                log.WriteLine(string.Format("[BitsJanitor] FATAL COM Error: {0} (0x{1:X})", comEx.Message, comEx.ErrorCode));
                log.WriteLine("Ensure the BITS service is running and you have necessary permissions.");
            }
            catch (Exception ex)
            {
                // Log other fatal errors
                log.WriteLine(string.Format("[BitsJanitor] FATAL Error: {0}", ex));
            }
            finally
            {
                // Ensure all COM objects are released, even if errors occurred
                // Note: currentJob should be null here if the loop finished normally or hit the inner finally.
                // This handles cases where an exception occurred between Next() and the inner try.
                if (currentJob != null) Marshal.ReleaseComObject(currentJob);
                if (enumJobs != null) Marshal.ReleaseComObject(enumJobs);
                if (mgr != null) Marshal.ReleaseComObject(mgr);
            }
        }

        /// <summary>
        /// Default predicate targeting stale Mozilla update jobs as discussed in Bug 1856462.
        /// Specifically looks for jobs named "MozillaUpdate..." or jobs that are stuck in the TRANSFERRED state.
        /// Also clears up our jobs (R1Delta).
        /// </summary>
        private static bool DefaultFirefoxPredicate(IBackgroundCopyJob job, string name, BG_JOB_STATE state)
        {
            // We are specifically interested in Mozilla jobs or jobs have finished downloading
            // but haven't been completed/acknowledged by the application, leaving them stuck.
            return !string.IsNullOrEmpty(name)
                   && ((name.StartsWith("MozillaUpdate", StringComparison.OrdinalIgnoreCase)
                   || state == BG_JOB_STATE.BG_JOB_STATE_TRANSFERRED)) || (name.StartsWith("R1Delta", StringComparison.OrdinalIgnoreCase));
        }


        #region COM Interop Definitions for BITS

        // --- Enums ---
        /// <summary>Flags for EnumJobs.</summary>
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
            // Method 0: CreateJob (Not used by janitor)
            void CreateJob([MarshalAs(UnmanagedType.LPWStr)] string displayName, BG_JOB_TYPE type, out Guid pJobId, [MarshalAs(UnmanagedType.Interface)] out IBackgroundCopyJob pJob);

            // Method 1: GetJob (Not used by janitor)
            void GetJob(ref Guid jobID, [MarshalAs(UnmanagedType.Interface)] out IBackgroundCopyJob ppJob);

            // Method 2: EnumJobs (Used by janitor)
            void EnumJobs(uint dwFlags, [MarshalAs(UnmanagedType.Interface)] out IEnumBackgroundCopyJobs ppEnum);

            // Method 3: GetErrorDescription (Not used by janitor)
            void GetErrorDescription([In] int hResult, uint languageId, [MarshalAs(UnmanagedType.LPWStr)] out string ppErrorDescription);
        }

        /// <summary>COM interface for enumerating BITS jobs.</summary>
        [ComImport, Guid("1AF4F612-3B71-466F-8F58-7B6F73AC57AD"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        public interface IEnumBackgroundCopyJobs
        {
            // Method 0: Next (Used by janitor)
            // Returns S_OK (0) if the next element is fetched, S_FALSE (1) if fewer than requested elements remain.
            [PreserveSig]
            int Next(uint celt, [MarshalAs(UnmanagedType.Interface)] out IBackgroundCopyJob rgelt, out uint pceltFetched);

            // Method 1: Skip (Not used by janitor)
            void Skip(uint celt);

            // Method 2: Reset (Not used by janitor)
            void Reset();

            // Method 3: Clone (Not used by janitor)
            void Clone([MarshalAs(UnmanagedType.Interface)] out IEnumBackgroundCopyJobs ppenum);

            // Method 4: GetCount (Not used by janitor)
            // void GetCount(out uint puCount); // This method exists but might not be needed here
        }

        /// <summary>COM interface for a BITS job.</summary>
        [ComImport, Guid("37668D37-507E-4160-9316-26306D150B12"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        public interface IBackgroundCopyJob
        {
            // --- Methods MUST be in VTable order ---

            // Method 0: AddFileSet (Not used by janitor)
            void AddFileSet(uint cFileCount, /*BG_FILE_INFO[]*/ IntPtr pFileSet);

            // Method 1: AddFile (Not used by janitor)
            void AddFile([MarshalAs(UnmanagedType.LPWStr)] string RemoteUrl, [MarshalAs(UnmanagedType.LPWStr)] string LocalName);

            // Method 2: EnumFiles (Not used by janitor)
            void EnumFiles([MarshalAs(UnmanagedType.Interface)] out /*IEnumBackgroundCopyFiles*/ IntPtr ppEnum);

            // Method 3: Suspend (Not used by janitor)
            void Suspend();

            // Method 4: Resume (Not used by janitor)
            void Resume();

            // Method 5: Cancel (Used by janitor)
            void Cancel();

            // Method 6: Complete (Not used by janitor)
            void Complete();

            // Method 7: GetId (Used by janitor)
            void GetId(out Guid pVal);

            // Method 8: GetType (Not used by janitor)
            void GetType(out BG_JOB_TYPE pVal);

            // Method 9: GetProgress (Not used by janitor)
            void GetProgress(out _BG_JOB_PROGRESS pVal);

            // Method 10: GetTimes (Not used by janitor)
            void GetTimes(out _BG_JOB_TIMES pVal);

            // Method 11: GetState (Used by janitor)
            void GetState(out BG_JOB_STATE pVal);

            // Method 12: GetError (Not used by janitor)
            // Needs definition for IBackgroundCopyError if used
            void GetError([MarshalAs(UnmanagedType.Interface)] out object ppError);

            // Method 13: GetOwner (Not used by janitor)
            void GetOwner([MarshalAs(UnmanagedType.LPWStr)] out string pVal);

            // Method 14: SetDisplayName (Not used by janitor)
            void SetDisplayName([MarshalAs(UnmanagedType.LPWStr)] string Val);

            // Method 15: GetDisplayName (Used by janitor)
            void GetDisplayName([MarshalAs(UnmanagedType.LPWStr)] out string pVal);

            // --- Methods after GetDisplayName are omitted as they are not needed ---
            // SetDescription, GetDescription, SetPriority, GetPriority, etc.
        }

        // --- Structs ---
        // Define necessary structs used by the interface methods, even if the methods themselves aren't called directly by the janitor.

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

        #endregion
    }
}