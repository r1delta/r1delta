using System;
using System.Collections.Generic;
using System.Diagnostics; // For Debug.WriteLine
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Threading;
using System.Threading.Tasks;

public class FastDownloadService : IDisposable
{
    // Number of segments to download in parallel.
    public int ParallelCount { get; set; } = 12; // Reduced default, 16 might be too high sometimes

    // Buffer size for reading from network stream and writing to file.
    public int BufferSize { get; set; } = 64 * 1024; // Standard buffer size (80KB), 1MB might be excessive per segment

    // Event to report download progress:
    // long: current bytes downloaded
    // long: total bytes to download.
    public event Action<long, long> DownloadProgressChanged;

    // HttpClient is intended to be reused. Create it once.
    // Use WinHttpHandler for potential HTTP/2 support on .NET Framework 4.6.2+ on compatible Windows versions.
    // Avoid forcing HTTP/2, let the handler negotiate.
    private readonly HttpClient _httpClient;
    private bool _disposed = false;
    public class Http2CustomHandler : WinHttpHandler
    {
        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, System.Threading.CancellationToken cancellationToken)
        {
            request.Version = new Version("2.0");
            return base.SendAsync(request, cancellationToken);
        }
    }

    public FastDownloadService()
    {
        // WinHttpHandler is generally more performant on Windows than HttpClientHandler
        // and is required for HTTP/2 on .NET Framework before .NET Core 3.0 / .NET 5+
        var handler = new Http2CustomHandler { };
        _httpClient = new HttpClient(handler, disposeHandler: true);
        // Set a reasonable timeout
        _httpClient.Timeout = TimeSpan.FromMinutes(0.5); // Adjust as needed
    }

    /// <summary>
    /// Downloads a file from the specified URL to the destination path using parallel segments if supported.
    /// </summary>
    /// <param name="url">File URL</param>
    /// <param name="destinationPath">Local file destination path</param>
    /// <param name="cancellationToken">Cancellation token</param>
    public async Task DownloadFileAsync(string url, string destinationPath, CancellationToken cancellationToken)
    {
        if (_disposed) throw new ObjectDisposedException(nameof(FastDownloadService));
        if (string.IsNullOrEmpty(url)) throw new ArgumentNullException(nameof(url));
        if (string.IsNullOrEmpty(destinationPath)) throw new ArgumentNullException(nameof(destinationPath));

        long totalLength = 0;
        bool rangeProcessingSupported = false;

        try
        {
            // 1. HEAD Request to get file size and check for Range support
            using (var headRequest = new HttpRequestMessage(HttpMethod.Head, url))
            using (var headResponse = await _httpClient.SendAsync(headRequest, HttpCompletionOption.ResponseHeadersRead, cancellationToken))
            {
                // Allow redirects if necessary (HttpClient handles this by default, but check status)
                headResponse.EnsureSuccessStatusCode();

                totalLength = headResponse.Content.Headers.ContentLength ?? 0;
                rangeProcessingSupported = headResponse.Headers.AcceptRanges?.Contains("bytes") == true;

                Debug.WriteLine($"HEAD Request Results: TotalLength={totalLength}, RangeSupported={rangeProcessingSupported}");

                if (totalLength <= 0)
                {
                    // If size is unknown or zero, parallel download based on size isn't possible.
                    // Consider falling back to sequential download or throwing an error.
                    // For simplicity, we'll throw if size is needed for parallel logic.
                    rangeProcessingSupported = false; // Force sequential if size is 0/unknown
                    Debug.WriteLine("Content-Length is zero or unknown.");
                    // If you want to handle zero-byte files specifically:
                    if (totalLength == 0)
                    {
                        File.WriteAllBytes(destinationPath, new byte[0]); // Create empty file
                        DownloadProgressChanged?.Invoke(0, 0);
                        Debug.WriteLine("Created empty file as Content-Length is 0.");
                        return; // Download complete
                    }
                    // If length is unknown, proceed to sequential download below.
                }
            }

            // 2. Check if file already exists and matches size
            if (File.Exists(destinationPath))
            {
                var existingFileInfo = new FileInfo(destinationPath);
                if (existingFileInfo.Length == totalLength)
                {
                    Debug.WriteLine($"File '{destinationPath}' already exists with correct size ({totalLength} bytes). Skipping download.");
                    DownloadProgressChanged?.Invoke(totalLength, totalLength); // Report as complete
                    return;
                }
                else
                {
                    Debug.WriteLine($"File '{destinationPath}' exists but size mismatch (Expected: {totalLength}, Actual: {existingFileInfo.Length}). Overwriting.");
                }
            }

            // Ensure directory exists
            Directory.CreateDirectory(Path.GetDirectoryName(destinationPath));

            // 3. Decide on Download Strategy (Parallel vs Sequential)
            if (rangeProcessingSupported && totalLength > 0 && ParallelCount > 1)
            {
                Debug.WriteLine("Attempting parallel download...");
                await DownloadFileParallelAsync(url, destinationPath, totalLength, cancellationToken);
            }
            else
            {
                Debug.WriteLine("Server does not support range requests, Content-Length is unknown, or ParallelCount is 1. Falling back to sequential download...");
                await DownloadFileSequentialAsync(url, destinationPath, totalLength, cancellationToken);
            }
        }
        catch (OperationCanceledException)
        {
            Debug.WriteLine("Download cancelled.");
            // Optionally delete partial file on cancellation
            TryDeleteFile(destinationPath);
            throw; // Re-throw cancellation exception
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Error during download: {ex.Message}");
            // Attempt to delete the potentially corrupted partial file
            TryDeleteFile(destinationPath);
            throw; // Re-throw the exception
        }
    }

    private async Task DownloadFileParallelAsync(string url, string destinationPath, long totalLength, CancellationToken cancellationToken)
    {
        long totalDownloaded = 0;
        object progressLock = new object(); // Lock for progress reporting
        object fileLock = new object(); // <<< Lock for FileStream access

        using (var destinationStream = new FileStream(destinationPath, FileMode.Create, FileAccess.Write, FileShare.None, BufferSize, useAsync: true))
        {
            destinationStream.SetLength(totalLength);

            int actualParallelCount = (int)Math.Min(ParallelCount, totalLength);
            if (actualParallelCount <= 0) actualParallelCount = 1;

            long segmentSize = totalLength / actualParallelCount;
            var downloadTasks = new List<Task>(actualParallelCount);

            using (var linkedCts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken))
            {
                var internalToken = linkedCts.Token;

                for (int i = 0; i < actualParallelCount; i++)
                {
                    long start = i * segmentSize;
                    long end = (i == actualParallelCount - 1) ? totalLength - 1 : start + segmentSize - 1;

                    if (start > end || start >= totalLength) continue;

                    downloadTasks.Add(Task.Run(async () =>
                    {
                        byte[] buffer = new byte[BufferSize];
                        int bytesRead;
                        long currentSegmentBytesRead = 0;
                        long currentFilePosition = start; // Track position for this segment

                        using (var request = new HttpRequestMessage(HttpMethod.Get, url))
                        {
                            request.Headers.Range = new RangeHeaderValue(start, end);
                            Debug.WriteLine($"Starting segment {i}: Range {start}-{end}");

                            using (var response = await _httpClient.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, internalToken))
                            {
                                // Ensure success *and* partial content
                                if (!response.IsSuccessStatusCode)
                                {
                                    // Throw specific exception including status code
                                    throw new HttpRequestException($"Segment download failed with status code {response.StatusCode} for range {start}-{end}.");
                                }
                                if (response.StatusCode != HttpStatusCode.PartialContent)
                                {
                                    throw new InvalidOperationException($"Server did not return PartialContent for range {start}-{end}. Status: {response.StatusCode}. Ensure server supports Range requests properly.");
                                }
                                // Optional: Verify Content-Range header matches request if present
                                // var contentRange = response.Content.Headers.ContentRange;
                                // if (contentRange == null || contentRange.From != start || contentRange.To != end) { ... handle mismatch ... }


                                using (var networkStream = await response.Content.ReadAsStreamAsync())
                                {
                                    while ((bytesRead = await networkStream.ReadAsync(buffer, 0, buffer.Length, internalToken)) > 0)
                                    {
                                        // --- CRITICAL SECTION ---
                                        // Lock the FileStream before seeking and writing
                                        lock (fileLock) // <<< Acquire lock
                                        {
                                            // Seek must be inside the lock with the Write
                                            destinationStream.Seek(currentFilePosition, SeekOrigin.Begin);
                                            // Write synchronously within the lock for simplicity and guaranteed atomicity
                                            // of seek+write. Async write within lock can be complex.
                                            // Since disk I/O is often the bottleneck anyway, sync write here
                                            // might not hurt overall performance significantly compared to network download time.
                                            destinationStream.Write(buffer, 0, bytesRead);

                                            // Alternatively, keep WriteAsync but ensure the lock covers both Seek and await WriteAsync:
                                            // This requires the lock statement itself to be async, which isn't directly possible.
                                            // You'd need SemaphoreSlim(1,1).WaitAsync() / Release() instead of lock().
                                            // For simplicity and guaranteed correctness, Write() sync is often preferred here.

                                        } // <<< Release lock

                                        currentFilePosition += bytesRead; // Update position for the *next* write of this task
                                        currentSegmentBytesRead += bytesRead;

                                        // Update total progress safely
                                        lock (progressLock)
                                        {
                                            totalDownloaded += bytesRead;
                                            DownloadProgressChanged?.Invoke(totalDownloaded, totalLength);
                                        }
                                    }
                                }
                            }
                            Debug.WriteLine($"Finished segment {i}: Range {start}-{end}, Read {currentSegmentBytesRead} bytes.");
                        }
                    }, internalToken));
                }

                try
                {
                    await Task.WhenAll(downloadTasks);
                    // Flush FileStream buffers to ensure all data is written to disk
                    await destinationStream.FlushAsync(internalToken); // Add flush before closing
                    DownloadProgressChanged?.Invoke(totalLength, totalLength);
                    Debug.WriteLine("Parallel download completed successfully.");
                }
                catch (Exception ex)
                {
                    Debug.WriteLine($"Error occurred during parallel download: {ex.Message}");
                    try { linkedCts.Cancel(); } catch { /* Ignore */ }
                    // Important: Ensure partial file deletion happens on ANY exception from WhenAll
                    throw; // Re-throw the original exception
                }
                // No finally block needed for TryDeleteFile here, as it's handled in the outer DownloadFileAsync catch block.
            }
        }
    }

    private async Task DownloadFileSequentialAsync(string url, string destinationPath, long totalLength, CancellationToken cancellationToken)
    {
        long totalDownloaded = 0;
        byte[] buffer = new byte[BufferSize];
        int bytesRead;

        using (var request = new HttpRequestMessage(HttpMethod.Get, url))
        using (var response = await _httpClient.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, cancellationToken))
        {
            response.EnsureSuccessStatusCode();

            // If totalLength was unknown from HEAD, try to get it now
            if (totalLength <= 0)
            {
                totalLength = response.Content.Headers.ContentLength ?? -1; // Use -1 if still unknown
                Debug.WriteLine($"Sequential download: ContentLength from GET response: {totalLength}");
            }

            using (var networkStream = await response.Content.ReadAsStreamAsync())
            using (var fileStream = new FileStream(destinationPath, FileMode.Create, FileAccess.Write, FileShare.None, BufferSize, useAsync: true))
            {
                while ((bytesRead = await networkStream.ReadAsync(buffer, 0, buffer.Length, cancellationToken)) > 0)
                {
                    await fileStream.WriteAsync(buffer, 0, bytesRead, cancellationToken);
                    totalDownloaded += bytesRead;
                    // Report progress (totalLength might be -1 if unknown)
                    DownloadProgressChanged?.Invoke(totalDownloaded, totalLength);
                }
            }
        }

        // Final progress report if length was known
        if (totalLength > 0)
        {
            DownloadProgressChanged?.Invoke(totalLength, totalLength);
        }
        Debug.WriteLine("Sequential download completed successfully.");
    }

    private void TryDeleteFile(string filePath)
    {
        try
        {
            if (File.Exists(filePath))
            {
                File.Delete(filePath);
                Debug.WriteLine($"Deleted partial file: {filePath}");
            }
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Failed to delete partial file '{filePath}': {ex.Message}");
            // Log or handle the inability to delete if necessary
        }
    }

    public void Dispose()
    {
        Dispose(true);
        GC.SuppressFinalize(this);
    }

    protected virtual void Dispose(bool disposing)
    {
        if (!_disposed)
        {
            if (disposing)
            {
                // Dispose managed resources
                _httpClient?.Dispose();
            }
            // Dispose unmanaged resources if any (none in this class)
            _disposed = true;
        }
    }

    // Finalizer (just in case Dispose is not called)
    ~FastDownloadService()
    {
        Dispose(false);
    }
}