using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Threading;
using System.Threading.Tasks;

public sealed class FastDownloadService : IDisposable
{
    private const int ProgressPollMs = 500;
    private const int ProcessKillTimeoutMs = 10000;
    private const string BundledCurlSha256 = "589c8e4d297b4831c82adf0261fc1ca57ce59d663b91b4106d2ee7dff3972648";
    private static readonly TimeSpan DefaultNoProgressTimeout = TimeSpan.FromSeconds(90);

    private readonly TimeSpan _noProgressTimeout;
    private bool _disposed;
    private Process _activeProcess;

    public event Action<DownloadProgressUpdate> DownloadProgressChanged;

    public FastDownloadService(string installRoot)
        : this(installRoot, DefaultNoProgressTimeout)
    {
    }

    internal FastDownloadService(string installRoot, TimeSpan noProgressTimeout)
    {
        if (string.IsNullOrWhiteSpace(installRoot))
            throw new ArgumentNullException(nameof(installRoot));
        if (noProgressTimeout <= TimeSpan.Zero)
            throw new ArgumentOutOfRangeException(nameof(noProgressTimeout));

        Directory.CreateDirectory(Path.GetFullPath(installRoot));
        _noProgressTimeout = noProgressTimeout;
    }

    public async Task DownloadFilesAsync(IEnumerable<DownloadRequest> files, CancellationToken cancellationToken)
    {
        if (_disposed) throw new ObjectDisposedException(nameof(FastDownloadService));
        if (files == null) throw new ArgumentNullException(nameof(files));

        var requests = files.ToList();
        if (requests.Count == 0)
            return;

        foreach (var request in requests)
        {
            if (request == null)
                throw new ArgumentException("The download list contains an empty request.", nameof(files));
            if (string.IsNullOrWhiteSpace(request.Url))
                throw new ArgumentException("A download request has no URL.", nameof(files));
            if (string.IsNullOrWhiteSpace(request.DestinationPath))
                throw new ArgumentException("A download request has no destination path.", nameof(files));
            if (request.ExpectedSize < 0)
                throw new ArgumentOutOfRangeException(nameof(files), "A download request has a negative expected size.");
        }

        var pending = new List<DownloadRequest>();
        foreach (var request in requests)
        {
            DeleteOversizedDownloadArtifacts(request);
            var partialPath = Path.GetFullPath(request.DestinationPath) + ".curl.partial";
            PreparePartialForBackend(request.DestinationPath, partialPath);
            if (!TryPromoteCompletePartial(request, partialPath, DownloadBackend.Curl))
                pending.Add(request);
        }

        if (pending.Count == 0)
            return;

        var latestErrors = new Dictionary<DownloadRequest, string>();

        ReportStatus(pending, DownloadBackend.Curl, DownloadTransferPhase.Preparing, 1, 1);
        AttemptResult curlResult;
        var curlPath = ResolveBundledCurlPath();
        if (string.IsNullOrEmpty(curlPath))
        {
            curlResult = AttemptResult.FailAll(
                pending,
                "bundled ECH-enabled curl.exe is missing or failed its integrity check");
            ReportStatus(pending, DownloadBackend.Curl, DownloadTransferPhase.Failed, 1, 1);
        }
        else
        {
            try
            {
                curlResult = await RunCurlAttemptAsync(curlPath, pending, cancellationToken).ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                throw;
            }
            catch (DownloadProcessTerminationException)
            {
                throw;
            }
            catch (Exception ex)
            {
                curlResult = AttemptResult.FailAll(pending, ex.Message);
            }
        }

        if (curlResult.Failures.Count == 0)
            return;

        pending = new List<DownloadRequest>();
        foreach (var failure in curlResult.Failures)
        {
            var partialPath = Path.GetFullPath(failure.Key.DestinationPath) + ".curl.partial";
            if (TryPromoteCompletePartial(failure.Key, partialPath, DownloadBackend.Curl))
                continue;

            pending.Add(failure.Key);
            latestErrors[failure.Key] = $"curl: {failure.Value}";
        }

        if (pending.Count == 0)
            return;

        Debug.WriteLine($"[FastDownloadService] Bundled curl left {pending.Count} file(s); retrying with HttpClient.");
        ReportStatus(pending, DownloadBackend.HttpClient, DownloadTransferPhase.Preparing, 1, 1);
        AttemptResult httpResult;
        try
        {
            httpResult = await RunHttpClientAttemptAsync(pending, cancellationToken).ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (Exception ex)
        {
            httpResult = AttemptResult.FailAll(pending, ex.Message);
        }

        if (httpResult.Failures.Count == 0)
            return;

        pending = httpResult.Failures.Keys.ToList();
        foreach (var failure in httpResult.Failures)
        {
            latestErrors.TryGetValue(failure.Key, out var curlError);
            latestErrors[failure.Key] =
                $"{curlError ?? "curl: unknown download error"}; HttpClient: {failure.Value}";
        }

        var details = string.Join(Environment.NewLine, pending.Select(request =>
        {
            latestErrors.TryGetValue(request, out var error);
            return $"{Path.GetFileName(request.DestinationPath)}: {error ?? "unknown download error"}";
        }));
        throw new DownloadException(
            $"All configured download backends failed for {pending.Count} file(s): bundled ECH-enabled curl and HttpClient." + Environment.NewLine +
            "Review the per-file errors below. You can retry later, try another connection, or use the manual game-file download linked in the #help channel and point the installer at that folder." + Environment.NewLine +
            details);
    }

    internal static string ResolveBundledCurlPath()
    {
        var launcherDirectory = Path.GetDirectoryName(typeof(FastDownloadService).Assembly.Location);
        if (string.IsNullOrEmpty(launcherDirectory))
            return null;

        var curlPath = Path.Combine(launcherDirectory, "tools", "curl", "curl.exe");
        try
        {
            if (!File.Exists(curlPath))
                return null;

            using var stream = new FileStream(curlPath, FileMode.Open, FileAccess.Read, FileShare.Read);
            using var sha256 = SHA256.Create();
            var actualHash = BitConverter.ToString(sha256.ComputeHash(stream))
                .Replace("-", string.Empty)
                .ToLowerInvariant();
            if (!string.Equals(actualHash, BundledCurlSha256, StringComparison.Ordinal))
            {
                Debug.WriteLine(
                    $"[FastDownloadService] Refusing bundled curl.exe with unexpected SHA-256 {actualHash}.");
                return null;
            }

            return curlPath;
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[FastDownloadService] Failed to verify bundled curl.exe: {ex.Message}");
            return null;
        }
    }

    private static string BuildCurlArguments(DownloadRequest request, string partialPath)
    {
        return string.Join(" ", new[]
        {
            "--fail",
            "--location",
            "--ech", "true",
            "--ca-native",
            "--no-progress-meter",
            "--show-error",
            "--retry", "3",
            "--retry-all-errors",
            "--retry-delay", "2",
            "--connect-timeout", "15",
            "--max-time", "900",
            "--continue-at", "-",
            "--user-agent", "\"r1delta-installer/1.0\"",
            "--output", $"\"{partialPath}\"",
            $"\"{request.Url}\""
        });
    }

    private async Task<AttemptResult> RunCurlAttemptAsync(string curlPath, IReadOnlyCollection<DownloadRequest> requests, CancellationToken cancellationToken)
    {
        if (string.IsNullOrWhiteSpace(curlPath) || !File.Exists(curlPath))
            return AttemptResult.FailAll(requests, "bundled curl.exe was not found");

        var failures = new Dictionary<DownloadRequest, string>();
        foreach (var request in requests)
        {
            cancellationToken.ThrowIfCancellationRequested();

            var destinationPath = Path.GetFullPath(request.DestinationPath);
            Directory.CreateDirectory(Path.GetDirectoryName(destinationPath));
            var partialPath = destinationPath + ".curl.partial";
            PreparePartialForBackend(destinationPath, partialPath);

            var arguments = BuildCurlArguments(request, partialPath);

            var startInfo = new ProcessStartInfo
            {
                FileName = curlPath,
                Arguments = arguments,
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardError = true,
                RedirectStandardOutput = true
            };

            Process process = null;
            Task waitTask = null;
            Task stderrTask = Task.CompletedTask;
            Task stdoutTask = Task.CompletedTask;
            CancellationTokenRegistration cancellationRegistration = default;
            var processStarted = false;
            try
            {
                process = new Process { StartInfo = startInfo, EnableRaisingEvents = true };
                waitTask = CreateProcessExitTask(process);
                if (!process.Start())
                {
                    failures[request] = "failed to start curl.exe";
                    continue;
                }

                processStarted = true;
                _activeProcess = process;
                cancellationRegistration = cancellationToken.Register(() => KillProcess(process));
                stderrTask = DrainOutputAsync(process.StandardError, "curl-stderr");
                stdoutTask = DrainOutputAsync(process.StandardOutput, "curl-stdout");

                var lastLength = GetFileLength(partialPath);
                var lastProgressUtc = DateTime.UtcNow;
                var processDeadlineUtc = lastProgressUtc + TimeSpan.FromMinutes(20);
                DownloadProgressChanged?.Invoke(new DownloadProgressUpdate(
                    request.DestinationPath,
                    lastLength,
                    0,
                    DownloadBackend.Curl,
                    DownloadTransferPhase.Downloading,
                    1,
                    1));

                string timeoutMessage = null;
                while (!waitTask.IsCompleted)
                {
                    cancellationToken.ThrowIfCancellationRequested();

                    var delayTask = Task.Delay(ProgressPollMs, cancellationToken);
                    var completedTask = await Task.WhenAny(waitTask, delayTask).ConfigureAwait(false);
                    if (completedTask != waitTask)
                        await delayTask.ConfigureAwait(false);

                    cancellationToken.ThrowIfCancellationRequested();
                    var currentLength = GetFileLength(partialPath);
                    if (currentLength != lastLength)
                    {
                        if (currentLength > lastLength)
                            lastProgressUtc = DateTime.UtcNow;
                        lastLength = currentLength;
                        DownloadProgressChanged?.Invoke(new DownloadProgressUpdate(
                            request.DestinationPath,
                            currentLength,
                            0,
                            DownloadBackend.Curl,
                            DownloadTransferPhase.Downloading,
                            1,
                            1));
                    }

                    var now = DateTime.UtcNow;
                    if (now - lastProgressUtc >= _noProgressTimeout)
                        timeoutMessage = $"curl made no byte progress for {(int)_noProgressTimeout.TotalSeconds} seconds";
                    else if (now >= processDeadlineUtc)
                        timeoutMessage = "curl.exe timed out after 20 minutes";

                    if (timeoutMessage != null)
                    {
                        KillProcess(process);
                        break;
                    }
                }

                if (timeoutMessage != null)
                {
                    var timeoutExitCode = await TryGetProcessExitCodeWithinAsync(
                        process,
                        waitTask,
                        ProcessKillTimeoutMs).ConfigureAwait(false);
                    if (!timeoutExitCode.HasValue)
                        throw new DownloadProcessTerminationException("curl.exe did not exit after timeout termination.");
                    failures[request] = timeoutMessage;
                    DownloadProgressChanged?.Invoke(new DownloadProgressUpdate(
                        request.DestinationPath,
                        GetFileLength(partialPath),
                        0,
                        DownloadBackend.Curl,
                        DownloadTransferPhase.Failed,
                        1,
                        1));
                    continue;
                }

                await waitTask.ConfigureAwait(false);
                var exitCode = process.ExitCode;
                await Task.WhenAll(stderrTask, stdoutTask).ConfigureAwait(false);
                if (exitCode != 0)
                {
                    failures[request] = $"curl exited with code {exitCode}";
                    DownloadProgressChanged?.Invoke(new DownloadProgressUpdate(
                        request.DestinationPath,
                        GetFileLength(partialPath),
                        0,
                        DownloadBackend.Curl,
                        DownloadTransferPhase.Failed,
                        1,
                        1));
                    continue;
                }

                try
                {
                    if (File.Exists(destinationPath))
                        File.Delete(destinationPath);
                    File.Move(partialPath, destinationPath);
                    DownloadProgressChanged?.Invoke(new DownloadProgressUpdate(
                        request.DestinationPath,
                        GetFileLength(destinationPath),
                        0,
                        DownloadBackend.Curl,
                        DownloadTransferPhase.TransferComplete,
                        1,
                        1));
                }
                catch (Exception ex)
                {
                    failures[request] = $"curl promote failed: {ex.Message}";
                }
            }
            catch (OperationCanceledException)
            {
                throw;
            }
            catch (DownloadProcessTerminationException)
            {
                throw;
            }
            catch (Exception ex)
            {
                failures[request] = $"curl failed: {ex.Message}";
            }
            finally
            {
                cancellationRegistration.Dispose();
                if (processStarted && !process.HasExited)
                    KillProcess(process);

                if (processStarted && waitTask != null)
                {
                    try
                    {
                        var cleanupExitCode = await TryGetProcessExitCodeWithinAsync(
                            process,
                            waitTask,
                            ProcessKillTimeoutMs).ConfigureAwait(false);
                        if (!cleanupExitCode.HasValue)
                            throw new DownloadProcessTerminationException("curl.exe did not exit during attempt cleanup.");
                    }
                    catch (DownloadProcessTerminationException)
                    {
                        throw;
                    }
                    catch (Exception ex)
                    {
                        throw new DownloadProcessTerminationException("Failed while waiting for curl.exe to exit during attempt cleanup.", ex);
                    }
                }

                try { await CompletesWithinAsync(Task.WhenAll(stderrTask, stdoutTask), ProcessKillTimeoutMs).ConfigureAwait(false); }
                catch (Exception) { }

                if (process != null && ReferenceEquals(_activeProcess, process))
                    _activeProcess = null;
                process?.Dispose();
            }
        }

        return new AttemptResult(failures);
    }

    private async Task<AttemptResult> RunHttpClientAttemptAsync(IReadOnlyCollection<DownloadRequest> requests, CancellationToken cancellationToken)
    {
        var failures = new Dictionary<DownloadRequest, string>();
        using var client = new System.Net.Http.HttpClient(new System.Net.Http.HttpClientHandler());
        client.Timeout = TimeSpan.FromMinutes(20);
        client.DefaultRequestHeaders.TryAddWithoutValidation("User-Agent", "r1delta-installer/1.0");

        foreach (var request in requests)
        {
            cancellationToken.ThrowIfCancellationRequested();

            var destinationPath = Path.GetFullPath(request.DestinationPath);
            Directory.CreateDirectory(Path.GetDirectoryName(destinationPath));
            var partialPath = destinationPath + ".part";
            PreparePartialForBackend(destinationPath, partialPath);

            DownloadProgressChanged?.Invoke(new DownloadProgressUpdate(
                request.DestinationPath,
                GetFileLength(partialPath),
                0,
                DownloadBackend.HttpClient,
                DownloadTransferPhase.Downloading,
                1,
                1));
            try
            {
                using var requestMessage = new System.Net.Http.HttpRequestMessage(System.Net.Http.HttpMethod.Get, request.Url);
                long resumeFrom = 0;
                if (File.Exists(partialPath))
                {
                    resumeFrom = new FileInfo(partialPath).Length;
                    if (resumeFrom > 0)
                        requestMessage.Headers.Range = new System.Net.Http.Headers.RangeHeaderValue(resumeFrom, null);
                }

                using var response = await SendHttpClientRequestWithIdleTimeoutAsync(
                    client,
                    requestMessage,
                    cancellationToken).ConfigureAwait(false);
                if (!response.IsSuccessStatusCode)
                {
                    failures[request] = $"HTTP {(int)response.StatusCode} {response.ReasonPhrase}";
                    continue;
                }

                var mode = response.StatusCode == System.Net.HttpStatusCode.PartialContent
                    ? FileMode.Append
                    : FileMode.Create;
                if (mode == FileMode.Create)
                    resumeFrom = 0;

                using (var contentStream = await response.Content.ReadAsStreamAsync().ConfigureAwait(false))
                using (var fileStream = new FileStream(partialPath, mode, FileAccess.Write, FileShare.None, 65536, useAsync: true))
                {
                    var buffer = new byte[65536];
                    long received = resumeFrom;
                    var lastProgress = DateTime.UtcNow;
                    while (true)
                    {
                        int bytesRead;
                        using var readTimeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
                        readTimeout.CancelAfter(_noProgressTimeout);
                        try
                        {
                            bytesRead = await contentStream.ReadAsync(buffer, 0, buffer.Length, readTimeout.Token).ConfigureAwait(false);
                        }
                        catch (OperationCanceledException) when (readTimeout.IsCancellationRequested && !cancellationToken.IsCancellationRequested)
                        {
                            throw new TimeoutException(
                                $"idle timeout: no response body bytes received for {_noProgressTimeout.TotalSeconds:g} seconds");
                        }

                        if (bytesRead == 0)
                            break;

                        await fileStream.WriteAsync(buffer, 0, bytesRead, cancellationToken).ConfigureAwait(false);
                        received += bytesRead;

                        var now = DateTime.UtcNow;
                        if (now - lastProgress >= TimeSpan.FromMilliseconds(ProgressPollMs))
                        {
                            lastProgress = now;
                            var remainingLength = response.Content.Headers.ContentLength ?? 0;
                            var total = response.Content.Headers.ContentRange?.Length
                                ?? (mode == FileMode.Append ? resumeFrom + remainingLength : remainingLength);
                            DownloadProgressChanged?.Invoke(new DownloadProgressUpdate(
                                request.DestinationPath,
                                received,
                                total,
                                DownloadBackend.HttpClient,
                                DownloadTransferPhase.Downloading,
                                1,
                                1));
                        }
                    }

                    await fileStream.FlushAsync(cancellationToken).ConfigureAwait(false);
                }

                if (File.Exists(destinationPath))
                    File.Delete(destinationPath);
                File.Move(partialPath, destinationPath);
                DownloadProgressChanged?.Invoke(new DownloadProgressUpdate(
                    request.DestinationPath,
                    GetFileLength(destinationPath),
                    response.Content.Headers.ContentRange?.Length ?? response.Content.Headers.ContentLength ?? 0,
                    DownloadBackend.HttpClient,
                    DownloadTransferPhase.TransferComplete,
                    1,
                    1));
            }
            catch (OperationCanceledException)
            {
                throw;
            }
            catch (Exception ex)
            {
                failures[request] = $"HttpClient failed: {ex.Message}";
            }
        }

        return new AttemptResult(failures);
    }
    private async Task<System.Net.Http.HttpResponseMessage> SendHttpClientRequestWithIdleTimeoutAsync(
        System.Net.Http.HttpClient client,
        System.Net.Http.HttpRequestMessage request,
        CancellationToken cancellationToken)
    {
        using var headerTimeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        headerTimeout.CancelAfter(_noProgressTimeout);
        try
        {
            return await client.SendAsync(
                request,
                System.Net.Http.HttpCompletionOption.ResponseHeadersRead,
                headerTimeout.Token).ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (headerTimeout.IsCancellationRequested && !cancellationToken.IsCancellationRequested)
        {
            throw new TimeoutException(
                $"idle timeout: no response headers received for {_noProgressTimeout.TotalSeconds:g} seconds");
        }
    }



    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;

        var process = _activeProcess;
        if (process == null)
            return;

        try
        {
            if (process.HasExited)
                return;

            KillProcess(process);
            if (!process.WaitForExit(ProcessKillTimeoutMs))
                throw new DownloadProcessTerminationException("The owned download process did not exit after it was terminated during disposal.");
        }
        catch (InvalidOperationException)
        {
            // The operation's normal cleanup already disposed the process.
        }
        catch (DownloadProcessTerminationException)
        {
            throw;
        }
        catch (Exception ex)
        {
            throw new DownloadProcessTerminationException("Failed while waiting for the owned download process to exit during disposal.", ex);
        }
        finally
        {
            if (ReferenceEquals(_activeProcess, process))
                _activeProcess = null;
        }
    }


    private static Task CreateProcessExitTask(Process process)
    {
        if (process == null)
            throw new ArgumentNullException(nameof(process));

        var source = new TaskCompletionSource<bool>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        process.Exited += (sender, args) => source.TrySetResult(true);
        return source.Task;
    }

    private static async Task<int?> TryGetProcessExitCodeWithinAsync(
        Process process,
        Task exitTask,
        int timeoutMs)
    {
        if (process == null)
            throw new ArgumentNullException(nameof(process));
        if (exitTask == null)
            throw new ArgumentNullException(nameof(exitTask));
        if (timeoutMs <= 0)
            throw new ArgumentOutOfRangeException(nameof(timeoutMs));

        if (await CompletesWithinAsync(exitTask, timeoutMs).ConfigureAwait(false))
        {
            await exitTask.ConfigureAwait(false);
            return process.ExitCode;
        }

        try
        {
            if (!process.HasExited)
                return null;

            process.WaitForExit();
            return process.ExitCode;
        }
        catch (InvalidOperationException)
        {
            return null;
        }
    }

    private static async Task<bool> CompletesWithinAsync(Task task, int timeoutMs)
    {
        return await Task.WhenAny(task, Task.Delay(timeoutMs)).ConfigureAwait(false) == task;
    }

    private void ReportStatus(
        IEnumerable<DownloadRequest> requests,
        DownloadBackend backend,
        DownloadTransferPhase phase,
        int attempt,
        int maxAttempts)
    {
        foreach (var request in requests)
        {
            DownloadProgressChanged?.Invoke(new DownloadProgressUpdate(
                request.DestinationPath,
                GetLargestPartialLength(request.DestinationPath),
                0,
                backend,
                phase,
                attempt,
                maxAttempts));
        }
    }

    private static void PreparePartialForBackend(string destinationPath, string backendPath)
    {
        destinationPath = Path.GetFullPath(destinationPath);
        backendPath = Path.GetFullPath(backendPath);
        var candidates = new[]
        {
            destinationPath,
            destinationPath + ".part",
            destinationPath + ".curl.partial"
        };

        var bestPath = candidates
            .Where(File.Exists)
            .OrderByDescending(GetFileLength)
            .FirstOrDefault();
        if (bestPath == null || string.Equals(bestPath, backendPath, StringComparison.OrdinalIgnoreCase))
            return;

        if (File.Exists(backendPath))
            File.Delete(backendPath);
        File.Move(bestPath, backendPath);

        var ariaControlPath = destinationPath + ".aria2";
        if (File.Exists(ariaControlPath))
            File.Delete(ariaControlPath);
    }

    private static void DeleteOversizedDownloadArtifacts(DownloadRequest request)
    {
        if (request.ExpectedSize <= 0)
            return;

        var destinationPath = Path.GetFullPath(request.DestinationPath);
        var deletedOversizedArtifact = false;
        foreach (var candidatePath in new[]
        {
            destinationPath,
            destinationPath + ".part",
            destinationPath + ".curl.partial"
        })
        {
            if (File.Exists(candidatePath) && new FileInfo(candidatePath).Length > request.ExpectedSize)
            {
                File.Delete(candidatePath);
                deletedOversizedArtifact = true;
            }
        }

        if (deletedOversizedArtifact)
        {
            var ariaControlPath = destinationPath + ".aria2";
            if (File.Exists(ariaControlPath))
                File.Delete(ariaControlPath);
        }
    }

    private bool TryPromoteCompletePartial(
        DownloadRequest request,
        string partialPath,
        DownloadBackend backend)
    {
        if (request.ExpectedSize <= 0 ||
            GetFileLength(partialPath) != request.ExpectedSize)
        {
            return false;
        }

        var destinationPath = Path.GetFullPath(request.DestinationPath);
        try
        {
            if (File.Exists(destinationPath))
                File.Delete(destinationPath);
            File.Move(partialPath, destinationPath);

            foreach (var stalePath in new[]
            {
                destinationPath + ".part",
                destinationPath + ".curl.partial",
                destinationPath + ".aria2"
            })
            {
                if (File.Exists(stalePath))
                    File.Delete(stalePath);
            }

            DownloadProgressChanged?.Invoke(new DownloadProgressUpdate(
                request.DestinationPath,
                request.ExpectedSize,
                request.ExpectedSize,
                backend,
                DownloadTransferPhase.TransferComplete,
                1,
                1));
            Debug.WriteLine(
                $"[FastDownloadService] Promoted complete partial without issuing an at-EOF range: {request.DestinationPath}");
            return true;
        }
        catch (Exception ex)
        {
            Debug.WriteLine(
                $"[FastDownloadService] Could not promote complete partial '{partialPath}': {ex.Message}");
            return false;
        }
    }

    private static long GetLargestPartialLength(string destinationPath)
    {
        destinationPath = Path.GetFullPath(destinationPath);
        return new[]
        {
            destinationPath,
            destinationPath + ".part",
            destinationPath + ".curl.partial"
        }
        .Where(File.Exists)
        .Select(GetFileLength)
        .DefaultIfEmpty(0)
        .Max();
    }

    private static long GetFileLength(string path)
    {
        try
        {
            return File.Exists(path) ? new FileInfo(path).Length : 0;
        }
        catch
        {
            return 0;
        }
    }

    private static async Task DrainOutputAsync(StreamReader reader, string streamName)
    {
        string line;
        while ((line = await reader.ReadLineAsync().ConfigureAwait(false)) != null)
        {
            if (line.Length > 0)
                Debug.WriteLine($"[download-process:{streamName}] {line}");
        }
    }

    private static void KillProcess(Process process)
    {
        try
        {
            if (process != null && !process.HasExited)
                process.Kill();
        }
        catch (InvalidOperationException)
        {
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[FastDownloadService] Failed to kill the owned download process: {ex.Message}");
        }
    }

    public sealed class DownloadRequest
    {
        public string Url { get; set; }
        public string DestinationPath { get; set; }
        public long ExpectedSize { get; set; }
    }


    private sealed class DownloadProcessTerminationException : DownloadException
    {
        public DownloadProcessTerminationException(string message) : base(message) { }
        public DownloadProcessTerminationException(string message, Exception innerException) : base(message, innerException) { }
    }

    private sealed class AttemptResult
    {
        public AttemptResult(Dictionary<DownloadRequest, string> failures)
        {
            Failures = failures;
        }

        public Dictionary<DownloadRequest, string> Failures { get; }

        public static AttemptResult FailAll(IEnumerable<DownloadRequest> requests, string error)
        {
            return new AttemptResult(requests.ToDictionary(request => request, request => error));
        }
    }

}

public enum DownloadBackend
{
    Curl,
    HttpClient
}

public enum DownloadTransferPhase
{
    Preparing,
    Downloading,
    TransferComplete,
    Failed
}

public readonly struct DownloadProgressUpdate
{
    public DownloadProgressUpdate(
        string destinationPath,
        long bytesReceived,
        long totalBytes,
        DownloadBackend backend,
        DownloadTransferPhase phase,
        int attempt,
        int maxAttempts)
    {
        DestinationPath = destinationPath;
        BytesReceived = Math.Max(0, bytesReceived);
        TotalBytes = Math.Max(0, totalBytes);
        Backend = backend;
        Phase = phase;
        Attempt = attempt;
        MaxAttempts = maxAttempts;
    }

    public string DestinationPath { get; }
    public long BytesReceived { get; }
    public long TotalBytes { get; }
    public DownloadBackend Backend { get; }
    public DownloadTransferPhase Phase { get; }
    public int Attempt { get; }
    public int MaxAttempts { get; }
}

public class DownloadException : Exception
{
    public DownloadException(string message) : base(message) { }
    public DownloadException(string message, Exception innerException) : base(message, innerException) { }
}
