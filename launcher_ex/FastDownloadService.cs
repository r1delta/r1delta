using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;   // For Marshal, InvalidComObjectException
using System.Threading;
using System.Threading.Tasks;
using usis.Net.Bits;                    // BITS interop

public class FastDownloadService : IDisposable
{
    public event Action<long, long> DownloadProgressChanged;

    private BackgroundCopyManager _bitsManager;
    private bool _disposed = false;

    // Counter for any in-flight BITS callback
    private int _activeCallbacks = 0;
    private readonly object _disposeLock = new object(); // Lock for manager disposal

    public FastDownloadService()
    {
        try
        {
            // BITS objects should ideally be created and accessed from an MTA thread.
            // While BackgroundCopyManager.Connect might handle this, explicit MTA can be safer.
            // Consider if the calling context ensures this or if you need to manage a dedicated thread.
            // For now, assume the calling context is appropriate or Connect handles it.
            _bitsManager = BackgroundCopyManager.Connect();
            Debug.WriteLine($"[FastDownloadService] Connected to BITS version: {_bitsManager.Version}");
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[FastDownloadService] Failed to connect to BITS: {ex.Message}");
            // Ensure manager is null if connection failed
            _bitsManager = null;
            throw new PlatformNotSupportedException(
                "Failed to connect to BITS service. Ensure BITS is enabled and running.",
                ex
            );
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
            // 1) Create & configure job
            string name = $"R1DeltaDownload_{Path.GetFileNameWithoutExtension(destinationPath)}_{Guid.NewGuid()}";
            job = _bitsManager.CreateJob(name, BackgroundCopyJobType.Download);
            job.Priority = BackgroundCopyJobPriority.Foreground;
            job.AddFile(url, destinationPath);

            // 2) Start the job
            if (cancellationToken.IsCancellationRequested)
            {
                job.Cancel();
                tcs.TrySetCanceled(cancellationToken);
            }
            else
            {
                job.Resume();
                Debug.WriteLine($"[FastDownloadService] Job {job.Id} resumed");
            }

            // 3) Poll job state
            while (true)
            {
                if (cancellationToken.IsCancellationRequested)
                {
                    job.Cancel();
                    tcs.TrySetCanceled(cancellationToken);
                    break;
                }

                var state = job.State; // Use property to avoid COM method call issues
                switch (state)
                {
                    case BackgroundCopyJobState.Transferred:
                        job.Complete();
                        tcs.TrySetResult(true);
                        Debug.WriteLine($"[FastDownloadService] Job {job.Id} completed");
                        goto exitLoop;

                    case BackgroundCopyJobState.Error:
                        var error = job.GetErrorInfo();
                        tcs.TrySetException(new BackgroundCopyException(
                            $"BITS job failed: {error.Description} (0x{error.Code:X})"));
                        Debug.WriteLine($"[FastDownloadService] Job {job.Id} failed: {error.Description}");
                        goto exitLoop;

                    case BackgroundCopyJobState.Connecting:
                    case BackgroundCopyJobState.Transferring:
                        var prog = job.RetrieveProgress();
                        long done = (long)prog.BytesTransferred;
                        long total = (long)prog.BytesTotal;
                        if (done > 0 || total > 0)
                            DownloadProgressChanged?.Invoke(done, total);
                        break;

                    case BackgroundCopyJobState.Canceled:
                        tcs.TrySetCanceled(cancellationToken);
                        goto exitLoop;

                    case BackgroundCopyJobState.Acknowledged:
                        tcs.TrySetResult(true);
                        goto exitLoop;
                }

                await Task.Delay(500, cancellationToken); // Adjust delay as needed
            }
        exitLoop:

            // 4) Wait for completion
            await tcs.Task;
        }
        catch
        {
            throw; // Let caller handle exceptions
        }
        finally
        {
            // Dispose of cancellation registration
            cancellationRegistration.Dispose();

            // Dispose of the job
            if (job != null)
            {
                var toDispose = job;
                job = null;
                try
                {
                    try
                    {
                        toDispose.Cancel();
                    }
                    catch (Exception ex) { };
                    toDispose.Dispose();                    
                    Debug.WriteLine($"[FastDownloadService] Job {toDispose.Id} disposed");
                }
                catch (Exception ex)
                {
                    Debug.WriteLine($"[FastDownloadService] Dispose error: {ex}");
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
        // Use lock to prevent race condition if Dispose is called concurrently
        lock (_disposeLock)
        {
            if (!_disposed)
            {
                if (disposing)
                {
                    Debug.WriteLine("[FastDownloadService] Disposing BITS Manager...");
                    if (_bitsManager != null)
                    {
                        try
                        {
                            // Dispose the manager COM object
                            _bitsManager.Dispose();
                            Debug.WriteLine("[FastDownloadService] Disposed BITS Manager successfully.");
                        }
                        catch (Exception ex)
                        {
                            // Log error during manager disposal
                            Debug.WriteLine($"[FastDownloadService] Error disposing BITS Manager: {ex.Message}");
                        }
                        _bitsManager = null;
                    }
                    else
                    {
                        Debug.WriteLine("[FastDownloadService] BITS Manager was already null or failed to initialize.");
                    }
                }
                // Release unmanaged resources here if any directly held by this class (none in this case)
                _disposed = true;
                Debug.WriteLine("[FastDownloadService] Dispose completed.");
            }
        }
    }

    // Finalizer as a safeguard if Dispose() isn't called (optional, usually needed if holding unmanaged resources directly)
    ~FastDownloadService()
    {
        Debug.WriteLine("[FastDownloadService] Finalizer called. Dispose was not called!");
        Dispose(false); // Dispose only unmanaged resources from finalizer
    }
}


// Helper Exception Class (consider moving to its own file)
public class BackgroundCopyException : Exception
{
    public BackgroundCopyException(string message) : base(message) { }
    public BackgroundCopyException(string message, Exception innerException) : base(message, innerException) { }
}