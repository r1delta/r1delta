using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;   // For Marshal, InvalidComObjectException, COMException
using System.Threading;
using System.Threading.Tasks;
using usis.Net.Bits;                    // BITS interop

public class FastDownloadService : IDisposable
{
    public event Action<long, long> DownloadProgressChanged;

    private BackgroundCopyManager _bitsManager;
    private bool _disposed = false;
    private readonly string _installRoot; // Added install root field

    // Synchronization for manager initialization on background thread
    private readonly ManualResetEventSlim _managerReady = new ManualResetEventSlim(false);
    private Thread _managerThread; // Keep reference to the MTA thread

    private readonly object _disposeLock = new object(); // Lock for manager disposal

    // Updated Constructor
    public FastDownloadService(string installRoot)
    {
        if (string.IsNullOrWhiteSpace(installRoot))
            throw new ArgumentNullException(nameof(installRoot));
        if (!Directory.Exists(installRoot))
            throw new DirectoryNotFoundException($"Install root directory not found: {installRoot}");

        _installRoot = Path.GetFullPath(installRoot); // Store the full path

        _managerThread = new Thread(() =>
        {
            try
            {
                Debug.WriteLine("[FastDownloadService] MTA thread started. Connecting to BITS...");
                // This thread is MTA, so BITS manager is created here.
                _bitsManager = BackgroundCopyManager.Connect();
                Debug.WriteLine($"[FastDownloadService] Connected to BITS version: {_bitsManager.Version}");
                _managerReady.Set(); // Signal that the manager is ready

                // Keep the thread alive to host the COM object's apartment.
                Thread.Sleep(Timeout.Infinite);
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[FastDownloadService] BITS MTA thread failed: {ex.Message}");
                // Let _bitsManager remain null
                 _managerReady.Set(); // Signal completion even on failure
            }
            finally
            {
                 Debug.WriteLine("[FastDownloadService] BITS MTA thread exiting.");
            }
        })
        { IsBackground = true, Name = "BITS-MTA" };

        _managerThread.SetApartmentState(ApartmentState.MTA);
        _managerThread.Start();

        // Wait for the manager to be initialized (or fail) on the background thread.
        _managerReady.Wait(); // Consider adding a timeout

        if (_bitsManager == null)
        {
             // The background thread failed to initialize BITS.
             throw new PlatformNotSupportedException(
                 "Failed to connect to BITS service on background thread. Ensure BITS is enabled and running."
             );
        }
         Debug.WriteLine("[FastDownloadService] Constructor finished, BITS Manager ready.");
    }

    // Added Helper Method
    private IBackgroundCopyJob GetOrCreateJob(string url, string destAbsPath)
    {
        // Ensure manager is available (should be, due to constructor wait)
        if (_bitsManager == null) throw new InvalidOperationException("BITS Manager not initialized.");

        // 1) Build a deterministic job name from the file's relative path
        string relPath;
        try
        {
            // Use Uri for robust relative path calculation
            var installRootUri = new Uri(_installRoot.EndsWith(Path.DirectorySeparatorChar) ? _installRoot : _installRoot + Path.DirectorySeparatorChar);
            var destAbsUri = new Uri(destAbsPath);
            relPath = Uri.UnescapeDataString(installRootUri.MakeRelativeUri(destAbsUri).ToString())
                         .Replace('/', Path.DirectorySeparatorChar); // Keep OS separator for consistency? Or force '/'? Let's force '/' for job name.
             relPath = relPath.Replace(Path.DirectorySeparatorChar, '/').ToLowerInvariant();
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[FastDownloadService] Error creating relative path for job name ('{_installRoot}', '{destAbsPath}'): {ex.Message}");
            // Fallback to a less ideal name if relative path fails
            relPath = Path.GetFileName(destAbsPath).ToLowerInvariant();
        }

        string jobName = $"R1Delta|{relPath}";
        Debug.WriteLine($"[FastDownloadService] Searching/Creating job: {jobName}");

        IEnumBackgroundCopyJobs enumJobs = null;
        IBackgroundCopyJob job = null;
        IEnumBackgroundCopyFiles filesEnum = null;
        IBackgroundCopyFile fileInfo = null;

        try
        {
            // 2) Scan existing jobs
            _bitsManager.EnumJobs(0, out enumJobs); // 0 = Current user jobs
            uint fetched;
            while (enumJobs.Next(1, out job, out fetched) == 0 && fetched == 1) // S_OK = 0
            {
                string currentJobName = null;
                try { currentJobName = job.DisplayName; } catch { /* Ignore COM errors getting name */ }

                if (currentJobName != jobName)
                {
                    Marshal.ReleaseComObject(job); // Release job if name doesn't match
                    job = null;
                    continue;
                }

                // Found job by name, now check its state and validity
                Debug.WriteLine($"[FastDownloadService] Found existing job {job.Id} with name {jobName}");
                var state = job.State;

                // If the destination file no longer exists on disk, cancel & re-create
                string existingPath = null;
                try
                {
                    job.EnumFiles(out filesEnum);
                    if (filesEnum.Next(1, out fileInfo, out fetched) == 0 && fetched == 1) // S_OK = 0
                    {
                        existingPath = fileInfo.LocalName;
                    }
                }
                catch (COMException ex) { Debug.WriteLine($"[FastDownloadService] COM Error checking files for job {job.Id}: {ex.Message}"); }
                finally
                {
                    if (fileInfo != null) { Marshal.ReleaseComObject(fileInfo); fileInfo = null; }
                    if (filesEnum != null) { Marshal.ReleaseComObject(filesEnum); filesEnum = null; }
                }

                if (string.IsNullOrEmpty(existingPath) || !File.Exists(existingPath))
                {
                    Debug.WriteLine($"[FastDownloadService] Destination file '{existingPath ?? "null"}' missing for job {job.Id}. Cancelling and recreating.");
                    try { job.Cancel(); } catch (COMException ex) { Debug.WriteLine($"[FastDownloadService] Error cancelling job {job.Id}: {ex.Message}"); }
                    Marshal.ReleaseComObject(job);
                    job = null;
                    break; // Exit loop to create a new job
                }

                Debug.WriteLine($"[FastDownloadService] Job {job.Id} state: {state}");
                // States you can resume from:
                if (state == BackgroundCopyJobState.Suspended ||
                    state == BackgroundCopyJobState.Error ||
                    state == BackgroundCopyJobState.TransientError ||
                    state == BackgroundCopyJobState.Queued ||
                    state == BackgroundCopyJobState.Connecting || // Can also resume from these potentially
                    state == BackgroundCopyJobState.Transferring)
                {
                    Debug.WriteLine($"[FastDownloadService] Resuming job {job.Id} from state {state}.");
                    return job; // Return the existing job to be resumed
                }

                // Already transferred? Complete it now and return null (no download needed)
                if (state == BackgroundCopyJobState.Transferred)
                {
                    Debug.WriteLine($"[FastDownloadService] Job {job.Id} already transferred. Completing.");
                    try { job.Complete(); } catch (COMException ex) { Debug.WriteLine($"[FastDownloadService] Error completing job {job.Id}: {ex.Message}"); }
                    Marshal.ReleaseComObject(job);
                    return null; // Signal that download is already complete
                }

                // Otherwise (Cancelled / Acknowledged) - tear down & recreate
                Debug.WriteLine($"[FastDownloadService] Job {job.Id} in state {state}. Cancelling and recreating.");
                try { job.Cancel(); } catch (COMException ex) { Debug.WriteLine($"[FastDownloadService] Error cancelling job {job.Id}: {ex.Message}"); }
                Marshal.ReleaseComObject(job);
                job = null;
                break; // Exit loop to create a new job
            }
        }
        catch (COMException ex)
        {
            Debug.WriteLine($"[FastDownloadService] COM Error enumerating jobs: {ex.Message}");
            // Clean up potentially acquired job object if error occurred mid-loop
            if (job != null) { Marshal.ReleaseComObject(job); job = null; }
            throw; // Re-throw the exception
        }
        finally
        {
            // Ensure enumerator is released
            if (enumJobs != null) Marshal.ReleaseComObject(enumJobs);
            // Ensure intermediate COM objects are released if loop exited unexpectedly
             if (fileInfo != null) { Marshal.ReleaseComObject(fileInfo); }
             if (filesEnum != null) { Marshal.ReleaseComObject(filesEnum); }
        }

        // 3) No suitable existing job found - create a new one
        Debug.WriteLine($"[FastDownloadService] Creating new job for {jobName}");
        var newJob = _bitsManager.CreateJob(jobName, BackgroundCopyJobType.Download);
        try
        {
            newJob.AddFile(url, destAbsPath);
            newJob.Priority = BackgroundCopyJobPriority.Foreground;
            // Resilience tweaks:
            newJob.SetNoProgressTimeout(120); // 2 minutes
            newJob.SetMinimumRetryDelay(30);  // 30 seconds
            Debug.WriteLine($"[FastDownloadService] Created new job {newJob.Id}");
            return newJob;
        }
        catch (Exception)
        {
            // If adding file or setting properties fails, clean up the created job
            try { newJob.Cancel(); } catch { /* Ignore */ }
            newJob.Dispose(); // Release the COM object
            throw;
        }
    }


    public async Task DownloadFileAsync(string url, string destinationPath, CancellationToken cancellationToken)
    {
        if (_disposed) throw new ObjectDisposedException(nameof(FastDownloadService));
        if (_bitsManager == null) throw new InvalidOperationException("BITS Manager not initialized.");
        if (string.IsNullOrWhiteSpace(url)) throw new ArgumentNullException(nameof(url));
        if (string.IsNullOrWhiteSpace(destinationPath)) throw new ArgumentNullException(nameof(destinationPath));

        // Prepare destination directory
        var directory = Path.GetDirectoryName(destinationPath);
        if (!string.IsNullOrEmpty(directory))
        {
            try { Directory.CreateDirectory(directory); }
            catch (Exception ex) when (
                ex is IOException ||
                ex is UnauthorizedAccessException ||
                ex is PathTooLongException ||
                ex is DirectoryNotFoundException)
            {
                Debug.WriteLine($"[FastDownloadService] Error creating directory '{directory}': {ex.Message}");
                throw new IOException($"Failed to create destination directory: {directory}", ex);
            }
        }

        BackgroundCopyJob job = null;
        var tcs = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
        CancellationTokenRegistration cancellationRegistration = default;

        try
        {
            // 1) Get or Create job using the new helper
            job = GetOrCreateJob(url, destinationPath);
            if (job == null)
            {
                // File already fully downloaded and job completed by GetOrCreateJob
                Debug.WriteLine($"[FastDownloadService] Job for {Path.GetFileName(destinationPath)} already complete.");
                DownloadProgressChanged?.Invoke(new FileInfo(destinationPath).Length, new FileInfo(destinationPath).Length); // Report 100%
                tcs.TrySetResult(true);
                goto exitLoop; // Skip download logic
            }

            // Register cancellation callback AFTER getting the job
            cancellationRegistration = cancellationToken.Register(() =>
            {
                if (job != null && !_disposed) // Check job existence and disposed state
                {
                    try
                    {
                        Debug.WriteLine($"[FastDownloadService] Cancellation requested. Suspending job {job.Id}");
                        job.Suspend(); // Suspend instead of Cancel
                    }
                    catch (COMException ex)
                    {
                        // Ignore errors if job is already in a final state or gone
                        Debug.WriteLine($"[FastDownloadService] Error suspending job {job.Id} on cancellation: {ex.Message} (State: {job?.State})");
                    }
                    catch (InvalidComObjectException)
                    {
                        Debug.WriteLine($"[FastDownloadService] Job {job.Id} COM object already released during cancellation.");
                    }
                }
                tcs.TrySetCanceled(cancellationToken);
            }, useSynchronizationContext: false);


            // 2) Start the job (if not already running)
            var initialState = job.State;
            if (initialState != BackgroundCopyJobState.Queued &&
                initialState != BackgroundCopyJobState.Transferring &&
                initialState != BackgroundCopyJobState.Connecting)
            {
                 // Only resume if it's not already in a potentially active state
                 if (initialState == BackgroundCopyJobState.Suspended ||
                     initialState == BackgroundCopyJobState.Error ||
                     initialState == BackgroundCopyJobState.TransientError)
                 {
                    Debug.WriteLine($"[FastDownloadService] Resuming job {job.Id} from state {initialState}");
                    job.Resume();
                 }
                 else
                 {
                     // This case shouldn't be hit often due to GetOrCreateJob logic, but log if it does.
                     Debug.WriteLine($"[FastDownloadService] Job {job.Id} in unexpected state {initialState} before loop.");
                 }
            }
            else
            {
                 Debug.WriteLine($"[FastDownloadService] Job {job.Id} already in active state {initialState}.");
            }


            // 3) Poll job state
            while (true)
            {
                // Check cancellation token FIRST in loop
                if (cancellationToken.IsCancellationRequested)
                {
                    // The registration callback should have suspended and set cancelled state.
                    // No need to suspend again here. Just break.
                    Debug.WriteLine($"[FastDownloadService] Cancellation detected in loop for job {job.Id}.");
                    tcs.TrySetCanceled(cancellationToken); // Ensure TCS is set
                    break;
                }

                var state = job.State;
                switch (state)
                {
                    case BackgroundCopyJobState.Transferred:
                        Debug.WriteLine($"[FastDownloadService] Job {job.Id} transferred. Completing...");
                        job.Complete();
                        tcs.TrySetResult(true);
                        Debug.WriteLine($"[FastDownloadService] Job {job.Id} completed.");
                        goto exitLoop;

                    case BackgroundCopyJobState.Error:
                        var error = job.GetErrorInfo();
                        var errorMsg = $"BITS job failed: {error.Description} (0x{error.Code:X})";
                        Debug.WriteLine($"[FastDownloadService] Job {job.Id} failed: {error.Description}");
                        tcs.TrySetException(new BackgroundCopyException(errorMsg));
                        goto exitLoop;

                    case BackgroundCopyJobState.Connecting:
                    case BackgroundCopyJobState.Transferring:
                        var prog = job.RetrieveProgress();
                        long done = (long)prog.BytesTransferred;
                        long total = (long)prog.BytesTotal;
                        // Ensure total is plausible (BITS sometimes reports 0 or -1 initially)
                        if (total <= 0) {
                            try { total = new FileInfo(destinationPath).Length; } catch {} // Try to get size from potentially existing file
                        }
                        if (done >= 0 && total > 0) // Only report if total seems valid
                            DownloadProgressChanged?.Invoke(done, total);
                        break;

                    case BackgroundCopyJobState.Canceled:
                        Debug.WriteLine($"[FastDownloadService] Job {job.Id} detected as Cancelled.");
                        tcs.TrySetCanceled(cancellationToken); // Use the original token
                        goto exitLoop;

                    case BackgroundCopyJobState.Acknowledged:
                        Debug.WriteLine($"[FastDownloadService] Job {job.Id} detected as Acknowledged (already complete).");
                        tcs.TrySetResult(true);
                        goto exitLoop;

                    case BackgroundCopyJobState.Suspended:
                        // If suspended due to our cancellation request, the TCS should already be cancelled.
                        // If suspended for other reasons (e.g., policy), just wait.
                        Debug.WriteLine($"[FastDownloadService] Job {job.Id} is Suspended.");
                        if (cancellationToken.IsCancellationRequested)
                        {
                             tcs.TrySetCanceled(cancellationToken);
                             goto exitLoop;
                        }
                        break;

                    case BackgroundCopyJobState.Queued:
                    case BackgroundCopyJobState.TransientError:
                        // Waiting state, report current progress if available
                         Debug.WriteLine($"[FastDownloadService] Job {job.Id} in state {state}. Waiting.");
                         var currentProg = job.RetrieveProgress();
                         long currentDone = (long)currentProg.BytesTransferred;
                         long currentTotal = (long)currentProg.BytesTotal;
                         if (currentTotal <= 0) { try { currentTotal = new FileInfo(destinationPath).Length; } catch {} }
                         if (currentDone >= 0 && currentTotal > 0)
                            DownloadProgressChanged?.Invoke(currentDone, currentTotal);
                        break;

                }

                // Wait before next poll
                try
                {
                    await Task.Delay(500, cancellationToken); // Use token here for faster exit on cancel
                }
                catch (TaskCanceledException)
                {
                    // Expected if cancellation happens during delay
                    Debug.WriteLine($"[FastDownloadService] Task.Delay cancelled for job {job.Id}.");
                    tcs.TrySetCanceled(cancellationToken); // Ensure TCS is set
                    break; // Exit loop
                }
            }
        exitLoop:; // Label needed for goto

            // 4) Wait for completion task (TCS)
            await tcs.Task;
        }
        catch (Exception ex) when (ex is not OperationCanceledException && ex is not BackgroundCopyException)
        {
             Debug.WriteLine($"[FastDownloadService] Unexpected error during download for {Path.GetFileName(destinationPath)}: {ex}");
             // Try to cancel the job if an unexpected error occurs, preserving data might not be useful
             if (job != null) { try { job.Cancel(); } catch {} }
             throw; // Re-throw unexpected exceptions
        }
        finally
        {
            // Dispose of cancellation registration FIRST
            cancellationRegistration.Dispose();

            // Dispose of the job COM object wrapper
            if (job != null)
            {
                var toDispose = job; // Capture variable for safety
                job = null; // Prevent reuse
                try
                {
                    // Do NOT call Cancel() or Suspend() here. State should be handled by the loop/cancellation logic.
                    // The Dispose() method on the wrapper should release the COM object reference.
                    toDispose.Dispose();
                    Debug.WriteLine($"[FastDownloadService] Job {toDispose.Id} disposed via wrapper.");
                }
                catch (InvalidComObjectException)
                {
                     Debug.WriteLine($"[FastDownloadService] Job {toDispose.Id} COM object already released.");
                }
                catch (COMException ex)
                {
                    // Log COM errors during dispose, but don't throw from finally
                    Debug.WriteLine($"[FastDownloadService] COM Error disposing job {toDispose.Id}: {ex.Message}");
                }
                catch (Exception ex)
                {
                    // Log other potential errors during dispose
                    Debug.WriteLine($"[FastDownloadService] Error disposing job {toDispose.Id}: {ex.Message}");
                }
            }
        }
    }

    public void Dispose()
    {
        Dispose(true);
        GC.SuppressFinalize(this);
    }

    protected virtual void Dispose(bool disposing)
    {
        lock (_disposeLock)
        {
            if (!_disposed)
            {
                if (disposing)
                {
                    Debug.WriteLine("[FastDownloadService] Disposing...");

                    // Dispose the manager COM object
                    if (_bitsManager != null)
                    {
                        Debug.WriteLine("[FastDownloadService] Disposing BITS Manager...");
                        try
                        {
                            // The Dispose() method should ideally handle marshalling if needed,
                            // or we might need to explicitly marshal this call to the MTA thread.
                            // Assuming usis.Net.Bits handles this.
                            _bitsManager.Dispose();
                            Debug.WriteLine("[FastDownloadService] Disposed BITS Manager successfully.");
                        }
                        catch (InvalidComObjectException)
                        {
                             Debug.WriteLine("[FastDownloadService] BITS Manager COM object already released.");
                        }
                        catch (COMException ex)
                        {
                            Debug.WriteLine($"[FastDownloadService] COM Error disposing BITS Manager: {ex.Message}");
                        }
                        catch (Exception ex)
                        {
                            Debug.WriteLine($"[FastDownloadService] Error disposing BITS Manager: {ex.Message}");
                        }
                        _bitsManager = null;
                    }
                    else
                    {
                        Debug.WriteLine("[FastDownloadService] BITS Manager was already null or failed to initialize.");
                    }

                    // Clean up the ManualResetEventSlim
                    _managerReady?.Dispose();

                    // Note: We don't explicitly Abort() the background thread.
                    // Since it's a background thread with Sleep(Infinite), it will terminate
                    // automatically when the application exits. Forcing an abort is generally discouraged.
                     Debug.WriteLine("[FastDownloadService] Managed resources disposed.");
                }

                // Release unmanaged resources here if any directly held (none in this case)

                _disposed = true;
                Debug.WriteLine("[FastDownloadService] Dispose completed.");
            }
        }
    }

    ~FastDownloadService()
    {
        Debug.WriteLine("[FastDownloadService] Finalizer called. Dispose was potentially not called!");
        Dispose(false);
    }
}


// Helper Exception Class
public class BackgroundCopyException : Exception
{
    public BackgroundCopyException(string message) : base(message) { }
    public BackgroundCopyException(string message, Exception innerException) : base(message, innerException) { }
}
