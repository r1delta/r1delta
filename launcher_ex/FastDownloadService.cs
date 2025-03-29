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
    /// <summary>Event to report download progress: (bytesReceived, totalBytes).</summary>
    public event Action<long, long> DownloadProgressChanged;

    private readonly HttpClient _httpClient;
    private bool _disposed = false;

    public int BufferSize { get; set; } = 64 * 1024; // 64KB buffer for reading/writing

    public FastDownloadService()
    {
        // Using a basic handler here for simplicity; you can still try WinHttpHandler if needed.
        var handler = new HttpClientHandler();
        _httpClient = new HttpClient(handler, disposeHandler: true)
        {
            Timeout = TimeSpan.FromSeconds(30) // example timeout
        };
    }

    /// <summary>
    /// Downloads a file from <paramref name="url"/> to <paramref name="destinationPath"/>,
    /// attempting to **resume** if an existing partial file is found and the server supports range requests.
    /// </summary>
    public async Task DownloadFileAsync(string url, string destinationPath, CancellationToken cancellationToken)
    {
        if (_disposed) throw new ObjectDisposedException(nameof(FastDownloadService));

        // 1) HEAD request to discover total size and range support
        long totalLength;
        bool rangeSupported;
        (totalLength, rangeSupported) = await GetServerInfoAsync(url, cancellationToken);

        // 2) Check local file
        long existingLength = 0;
        bool doPartialResume = false;
        if (File.Exists(destinationPath))
        {
            var fi = new FileInfo(destinationPath);
            existingLength = fi.Length;

            // If the file is already fully downloaded, skip
            if (existingLength == totalLength && totalLength > 0)
            {
                // OPTIONAL: do a quick checksum if you want to confirm correctness
                DownloadProgressChanged?.Invoke(totalLength, totalLength);
                Debug.WriteLine($"[FastDownloadService] File already complete: {destinationPath}");
                return;
            }

            // If we have a partial file, and the server supports Range, plan to resume
            if (existingLength > 0 && existingLength < totalLength && rangeSupported)
            {
                doPartialResume = true;
            }
            else
            {
                // If partial file is useless (no range support or mismatch), overwrite
                existingLength = 0;
                TryDeleteFile(destinationPath);
            }
        }

        // 3) If totalLength was unknown or zero, fallback to sequential from scratch
        if (totalLength <= 0)
        {
            // Some servers do not provide Content-Length reliably; we will treat it as unknown
            // and do a normal single GET.  We'll call that a "sequential" non-resumable scenario.
            await DownloadFromZeroAsync(url, destinationPath, cancellationToken, 0, -1);
            return;
        }

        // 4) If partial resume is possible, do it
        if (doPartialResume)
        {
            // Resume from the existingLength to the end
            // e.g. Range: bytes=existingLength-(totalLength-1)
            Debug.WriteLine($"[FastDownloadService] Resuming from {existingLength} of {totalLength} bytes.");
            await DownloadFromZeroAsync(url, destinationPath, cancellationToken, existingLength, totalLength);
        }
        else
        {
            // Otherwise, start fresh
            Debug.WriteLine("[FastDownloadService] No partial resume; starting fresh download.");
            await DownloadFromZeroAsync(url, destinationPath, cancellationToken, 0, totalLength);
        }
    }

    /// <summary>
    /// Actually does the GET. If <paramref name="resumeStart"/> &gt; 0, it tries a Range request.
    /// If the server doesn't support Range, the server's response might be full content.
    /// </summary>
    private async Task DownloadFromZeroAsync(
        string url,
        string destinationPath,
        CancellationToken cancellationToken,
        long resumeStart,
        long totalLength)
    {
        long bytesDownloadedSoFar = resumeStart;
        var mode = (resumeStart > 0) ? FileMode.OpenOrCreate : FileMode.Create;
        using (var fs = new FileStream(destinationPath, mode, FileAccess.Write, FileShare.None, BufferSize, useAsync: true))
        {
            // If resuming, ensure we seek to the correct offset in the file
            if (resumeStart > 0)
            {
                fs.Seek(resumeStart, SeekOrigin.Begin);
            }

            // Build the request
            using (var request = new HttpRequestMessage(HttpMethod.Get, url))
            {
                if (resumeStart > 0 && totalLength > 0)
                {
                    // e.g. "Range: bytes=resumeStart-"
                    request.Headers.Range = new RangeHeaderValue(resumeStart, null);
                }

                // 1) Send request
                using (var response = await _httpClient.SendAsync(
                    request,
                    HttpCompletionOption.ResponseHeadersRead,
                    cancellationToken))
                {
                    response.EnsureSuccessStatusCode();

                    // 2) If partial requested but server doesn't comply, 
                    // it might return status 200 + full content instead of 206
                    // We'll proceed to read the whole stream anyway.

                    // 3) Read stream in chunks, write to file, report progress
                    using (var networkStream = await response.Content.ReadAsStreamAsync())
                    {
                        var buffer = new byte[BufferSize];
                        int bytesRead;
                        while ((bytesRead = await networkStream.ReadAsync(buffer, 0, buffer.Length, cancellationToken)) > 0)
                        {
                            await fs.WriteAsync(buffer, 0, bytesRead, cancellationToken);
                            bytesDownloadedSoFar += bytesRead;
                            DownloadProgressChanged?.Invoke(bytesDownloadedSoFar, totalLength);
                        }
                    }
                }
            }

            // If we knew the total, do a final 100% event
            if (totalLength > 0 && bytesDownloadedSoFar >= totalLength)
            {
                DownloadProgressChanged?.Invoke(totalLength, totalLength);
            }
        }
    }

    /// <summary>
    /// Does a HEAD request to retrieve Content-Length and see if Range is accepted.
    /// Returns (totalLength, rangeSupported).
    /// </summary>
    private async Task<(long totalLength, bool rangeSupported)> GetServerInfoAsync(string url, CancellationToken cancellationToken)
    {
        long length = 0;
        bool range = false;
        using (var req = new HttpRequestMessage(HttpMethod.Head, url))
        {
            using (var resp = await _httpClient.SendAsync(req, HttpCompletionOption.ResponseHeadersRead, cancellationToken))
            {
                resp.EnsureSuccessStatusCode();
                length = resp.Content.Headers.ContentLength ?? 0;
                if (resp.Headers.AcceptRanges != null)
                {
                    // "bytes" is typical if range requests are supported
                    range = resp.Headers.AcceptRanges.Any(val => val.Equals("bytes", StringComparison.OrdinalIgnoreCase));
                }
            }
        }
        return (length, range);
    }

    /// <summary>Helper to delete a file if it exists.</summary>
    private void TryDeleteFile(string filePath)
    {
        try
        {
            if (File.Exists(filePath))
            {
                File.Delete(filePath);
                Debug.WriteLine($"[FastDownloadService] Deleted file: {filePath}");
            }
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[FastDownloadService] Error deleting file '{filePath}': {ex.Message}");
        }
    }

    public void Dispose()
    {
        if (!_disposed)
        {
            _httpClient.Dispose();
            _disposed = true;
        }
        GC.SuppressFinalize(this);
    }
}
