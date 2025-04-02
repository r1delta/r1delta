using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Threading;
using System.Threading.Tasks;

public class FastDownloadService : IDisposable
{
    /// <summary>
    /// Event to report download progress: (bytesReceived, totalBytes).
    /// totalBytes might be -1 or 0 if the total size is unknown.
    /// </summary>
    public event Action<long, long> DownloadProgressChanged;

    private readonly HttpClient _httpClient;
    private bool _disposed = false;

    public int BufferSize { get; set; } = 64 * 1024; // 64KB buffer for reading/writing

    public FastDownloadService()
    {
        var handler = new HttpClientHandler()
        {
            // Allow automatic decompression (optional but common)
            AutomaticDecompression = DecompressionMethods.GZip | DecompressionMethods.Deflate
        };
        _httpClient = new HttpClient(handler, disposeHandler: true)
        {
            Timeout = TimeSpan.FromSeconds(60) // Slightly longer default timeout
        };
        // Set a default User-Agent, some servers require it
        _httpClient.DefaultRequestHeaders.UserAgent.ParseAdd("FastDownloadService/1.0");
    }

    /// <summary>
    /// Downloads a file from <paramref name="url"/> to <paramref name="destinationPath"/>,
    /// attempting to resume if an existing partial file is found and the server supports range requests.
    /// Handles non-success HTTP status codes by throwing HttpRequestException.
    /// </summary>
    /// <exception cref="HttpRequestException">Thrown if the server returns a non-success status code (like 4xx or 5xx).</exception>
    /// <exception cref="ObjectDisposedException">Thrown if the service has been disposed.</exception>
    /// <exception cref="OperationCanceledException">Thrown if the cancellation token is triggered.</exception>
    /// <exception cref="IOException">Thrown for file system errors.</exception>
    /// <exception cref="NotSupportedException">Thrown for unsupported URI schemes or other HttpClient issues.</exception>
    public async Task DownloadFileAsync(string url, string destinationPath, CancellationToken cancellationToken)
    {
        if (_disposed) throw new ObjectDisposedException(nameof(FastDownloadService));
        if (string.IsNullOrWhiteSpace(url)) throw new ArgumentNullException(nameof(url));
        if (string.IsNullOrWhiteSpace(destinationPath)) throw new ArgumentNullException(nameof(destinationPath));

        // Ensure the directory exists
        var directory = Path.GetDirectoryName(destinationPath);
        if (!string.IsNullOrEmpty(directory) && !Directory.Exists(directory))
        {
            Directory.CreateDirectory(directory);
        }

        // 1) HEAD request to discover total size and range support
        long totalLength = -1; // Use -1 to indicate unknown size initially
        bool rangeSupported = false;
        try
        {
            (totalLength, rangeSupported) = await GetServerInfoAsync(url, cancellationToken);
        }
        catch (HttpRequestException ex)
        {
            // If HEAD fails (e.g., 403/404), we cannot proceed.
            Debug.WriteLine($"[FastDownloadService] HEAD request failed for {url}: {ex.Message}");
            throw; // Re-throw the exception
        }

        // 2) Check local file
        long existingLength = 0;
        bool tryResume = false;
        if (File.Exists(destinationPath))
        {
            try
            {
                var fi = new FileInfo(destinationPath);
                existingLength = fi.Length;

                // If the file is already fully downloaded (and we know the total length)
                if (totalLength > 0 && existingLength == totalLength)
                {
                    DownloadProgressChanged?.Invoke(totalLength, totalLength);
                    Debug.WriteLine($"[FastDownloadService] File already complete: {destinationPath}");
                    return; // Success, nothing to do
                }

                // If we have a partial file, and the server supports Range, plan to resume
                if (existingLength > 0 && totalLength > 0 && existingLength < totalLength && rangeSupported)
                {
                    tryResume = true;
                    Debug.WriteLine($"[FastDownloadService] Planning resume from {existingLength} bytes for {destinationPath}.");
                }
                else if (existingLength > 0)
                {
                    // Partial file exists but cannot resume (no range support, size mismatch, unknown total size)
                    // We will overwrite it.
                    Debug.WriteLine($"[FastDownloadService] Partial file exists but cannot resume. Overwriting {destinationPath}.");
                    existingLength = 0; // Treat as starting fresh
                    // No need to delete here, FileMode.Create will handle it
                }
            }
            catch (IOException ex)
            {
                Debug.WriteLine($"[FastDownloadService] Error accessing existing file '{destinationPath}': {ex.Message}");
                // Decide how to handle: re-throw, or try deleting and starting fresh?
                // Let's re-throw for now, as it indicates a potential filesystem issue.
                throw;
            }
        }

        // 3) Perform the download (either fresh or resume)
        // We pass totalLength even if unknown (-1), DownloadAndProcessResponseAsync handles it.
        await DownloadAndProcessResponseAsync(url, destinationPath, cancellationToken, tryResume ? existingLength : 0, totalLength);
    }


    /// <summary>
    /// Performs the GET request, handles potential resume logic based on server response,
    /// reads the stream, writes to file, and reports progress.
    /// </summary>
    private async Task DownloadAndProcessResponseAsync(
        string url,
        string destinationPath,
        CancellationToken cancellationToken,
        long resumeFromByte, // The byte offset to *request* in the Range header
        long expectedTotalLength) // Length from HEAD request (-1 if unknown)
    {
        long currentBytesDownloaded = resumeFromByte; // Start counting from resume point
        long actualTotalLength = expectedTotalLength; // May be updated by GET response

        using (var request = new HttpRequestMessage(HttpMethod.Get, url))
        {
            FileMode fileMode = FileMode.Create; // Default: overwrite or create new
            long fileStreamOffset = 0; // Where to start writing in the file

            // Setup Range header if attempting resume
            if (resumeFromByte > 0)
            {
                request.Headers.Range = new RangeHeaderValue(resumeFromByte, null);
                Debug.WriteLine($"[FastDownloadService] Requesting Range: bytes={resumeFromByte}-");
            }

            using (var response = await _httpClient.SendAsync(
                request,
                HttpCompletionOption.ResponseHeadersRead, // Don't buffer response content
                cancellationToken))
            {
                // --- CRITICAL FIX: Check status code *before* reading content ---

                // Case 1: Resume was requested AND Server responded with 206 Partial Content
                if (resumeFromByte > 0 && response.StatusCode == HttpStatusCode.PartialContent)
                {
                    Debug.WriteLine("[FastDownloadService] Server returned 206 Partial Content. Resuming download.");
                    fileMode = FileMode.OpenOrCreate; // Append/Open existing file
                    fileStreamOffset = resumeFromByte; // Start writing at this offset
                    // Optional: Update total length if Content-Range is present and reliable
                    actualTotalLength = GetTotalLengthFromContentRange(response.Content.Headers.ContentRange, expectedTotalLength);
                }
                // Case 2: Resume was requested BUT Server responded with 200 OK (ignored Range)
                else if (resumeFromByte > 0 && response.StatusCode == HttpStatusCode.OK)
                {
                    Debug.WriteLine("[FastDownloadService] WARNING: Resume requested, but server returned 200 OK (full content). Restarting download from beginning.");
                    fileMode = FileMode.Create; // Overwrite required
                    fileStreamOffset = 0;       // Start writing from beginning
                    currentBytesDownloaded = 0; // Reset progress counter
                    actualTotalLength = response.Content.Headers.ContentLength ?? -1; // Use length from GET response
                }
                // Case 3: Standard download (no resume requested) OR server gave unexpected code
                else
                {
                    // This handles non-success codes (4xx, 5xx) by throwing HttpRequestException
                    // It also handles the normal 200 OK for a fresh download.
                    response.EnsureSuccessStatusCode(); // Throws if not 2xx

                    // If it was a successful 200 OK (fresh download)
                    Debug.WriteLine("[FastDownloadService] Server returned 200 OK. Starting fresh download.");
                    fileMode = FileMode.Create; // Overwrite or create
                    fileStreamOffset = 0;
                    currentBytesDownloaded = 0;
                    actualTotalLength = response.Content.Headers.ContentLength ?? -1; // Use length from GET response
                }

                // --- Proceed with file writing and streaming ---

                // Use the determined actualTotalLength for progress reporting
                long progressTotalLength = actualTotalLength > 0 ? actualTotalLength : expectedTotalLength; // Prefer actual, fallback to expected
                if (progressTotalLength <= 0) progressTotalLength = -1; // Ensure it's -1 if unknown

                // Open the file stream with the determined mode
                using (var fs = new FileStream(destinationPath, fileMode, FileAccess.Write, FileShare.None, BufferSize, useAsync: true))
                {
                    // Seek to the correct position if resuming (or overwriting after failed resume)
                    if (fileStreamOffset > 0)
                    {
                        fs.Seek(fileStreamOffset, SeekOrigin.Begin);
                    }
                    // If overwriting (FileMode.Create or failed resume), ensure file is truncated
                    // FileMode.Create implicitly truncates, but setting length can be explicit if needed.
                    // fs.SetLength(fileStreamOffset); // Usually not needed with FileMode.Create

                    using (var networkStream = await response.Content.ReadAsStreamAsync())
                    {
                        var buffer = new byte[BufferSize];
                        int bytesRead;
                        while ((bytesRead = await networkStream.ReadAsync(buffer, 0, buffer.Length, cancellationToken).ConfigureAwait(false)) > 0)
                        {
                            await fs.WriteAsync(buffer, 0, bytesRead, cancellationToken).ConfigureAwait(false);
                            currentBytesDownloaded += bytesRead;
                            DownloadProgressChanged?.Invoke(currentBytesDownloaded, progressTotalLength);
                        }
                    }
                }

                // Final progress report if total size was known
                if (progressTotalLength > 0)
                {
                    // Verify final size matches expected (optional sanity check)
                    if (currentBytesDownloaded != progressTotalLength)
                    {
                        Debug.WriteLine($"[FastDownloadService] WARNING: Final downloaded size ({currentBytesDownloaded}) does not match expected total size ({progressTotalLength}). File may be incomplete or server provided inconsistent lengths.");
                        // Potentially throw an exception here if strict matching is required
                    }
                    // Ensure 100% report is sent
                    DownloadProgressChanged?.Invoke(progressTotalLength, progressTotalLength);
                }
                else
                {
                    // If total size was unknown, report the final downloaded amount
                    DownloadProgressChanged?.Invoke(currentBytesDownloaded, -1);
                }

                Debug.WriteLine($"[FastDownloadService] Download finished for {destinationPath}. Total bytes written: {currentBytesDownloaded}");
            }
        }
    }


    /// <summary>
    /// Attempts to parse the total length from the Content-Range header (e.g., "bytes 1000-1999/5000").
    /// Returns the parsed total length, or the fallbackLength if parsing fails.
    /// </summary>
    private long GetTotalLengthFromContentRange(ContentRangeHeaderValue contentRange, long fallbackLength)
    {
        if (contentRange?.HasLength == true)
        {
            return contentRange.Length.Value;
        }
        return fallbackLength; // Return the original length from HEAD if Content-Range doesn't provide total
    }

    /// <summary>
    /// Does a HEAD request to retrieve Content-Length and see if Range is accepted.
    /// Returns (totalLength, rangeSupported). totalLength is -1 if unknown.
    /// </summary>
    private async Task<(long totalLength, bool rangeSupported)> GetServerInfoAsync(string url, CancellationToken cancellationToken)
    {
        long length = -1;
        bool range = false;
        using (var req = new HttpRequestMessage(HttpMethod.Head, url))
        {
            // Add User-Agent if not using DefaultRequestHeaders (though we are)
            // req.Headers.UserAgent.ParseAdd("FastDownloadService/1.0");

            using (var resp = await _httpClient.SendAsync(req, HttpCompletionOption.ResponseHeadersRead, cancellationToken))
            {
                // Throws HttpRequestException for non-success codes (4xx, 5xx)
                resp.EnsureSuccessStatusCode();

                length = resp.Content.Headers.ContentLength ?? -1; // Use -1 for unknown
                range = resp.Headers.AcceptRanges?.Contains("bytes", StringComparer.OrdinalIgnoreCase) ?? false;

                Debug.WriteLine($"[FastDownloadService] HEAD Info for {url}: ContentLength={length}, AcceptRanges={(range ? "bytes" : "none/unknown")}");
            }
        }
        return (length, range);
    }

    /// <summary>Helper to delete a file if it exists. Logs errors.</summary>
    private bool TryDeleteFile(string filePath) // Made internal as it's not used externally now
    {
        try
        {
            if (File.Exists(filePath))
            {
                File.Delete(filePath);
                Debug.WriteLine($"[FastDownloadService] Deleted file: {filePath}");
                return true;
            }
            return false; // File didn't exist
        }
        catch (IOException ex) // Catch more specific IO exceptions
        {
            Debug.WriteLine($"[FastDownloadService] IO Error deleting file '{filePath}': {ex.Message}");
            return false;
        }
        catch (UnauthorizedAccessException ex)
        {
            Debug.WriteLine($"[FastDownloadService] Access Error deleting file '{filePath}': {ex.Message}");
            return false;
        }
        catch (Exception ex) // Catch broader exceptions as a fallback
        {
            Debug.WriteLine($"[FastDownloadService] General Error deleting file '{filePath}': {ex.GetType().Name} - {ex.Message}");
            return false;
        }
    }

    // Note: The FastDownloadService instance itself is not thread-safe
    // for concurrent calls to DownloadFileAsync. Create separate instances
    // for concurrent downloads.

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
                // Dispose managed state (managed objects).
                _httpClient?.Dispose();
            }
            // Free unmanaged resources (unmanaged objects) and override finalizer
            // Set large fields to null
            _disposed = true;
        }
    }

    // No finalizer needed if only managed resources are used
    // ~FastDownloadService()
    // {
    //     Dispose(false);
    // }
}