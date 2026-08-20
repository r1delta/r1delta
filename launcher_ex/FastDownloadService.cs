using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;
using Newtonsoft.Json.Linq;

public sealed class FastDownloadService : IDisposable
{
    private const int ProgressPollMs = 500;
    private const int RpcTimeoutMs = 2000;
    private const int ProcessShutdownTimeoutMs = 5000;
    private const int ProcessKillTimeoutMs = 10000;
    private const int MaxProcessAttempts = 5;
    private static readonly int[] RetryDelaysSeconds = { 0, 2, 5, 10, 20 };
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

        var aria2Path = ResolveAria2Path();
        if (string.IsNullOrEmpty(aria2Path))
            throw new FileNotFoundException("Bundled aria2c.exe was not found. Reinstall the launcher and try again.");

        foreach (var request in requests)
        {
            if (request == null)
                throw new ArgumentException("The download list contains an empty request.", nameof(files));
            if (string.IsNullOrWhiteSpace(request.Url))
                throw new ArgumentException("A download request has no URL.", nameof(files));
            if (string.IsNullOrWhiteSpace(request.DestinationPath))
                throw new ArgumentException("A download request has no destination path.", nameof(files));
        }

        foreach (var request in requests)
            PreparePartialForBackend(request.DestinationPath, request.DestinationPath);

        var pending = requests;
        var latestErrors = new Dictionary<DownloadRequest, string>();

        // Phase 1: aria2c. A later backend is never started until the owned
        // child has reached a confirmed exit state.
        var ariaFatal = false;
        for (var attempt = 1; attempt <= MaxProcessAttempts && !ariaFatal; attempt++)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (_disposed)
                throw new ObjectDisposedException(nameof(FastDownloadService));

            var retryDelaySeconds = RetryDelaysSeconds[attempt - 1];
            if (retryDelaySeconds > 0)
            {
                ReportStatus(pending, DownloadBackend.Aria2, DownloadTransferPhase.RetryDelay, attempt, MaxProcessAttempts);
                Debug.WriteLine($"[FastDownloadService] Retrying {pending.Count} failed file(s) in {retryDelaySeconds} seconds (attempt {attempt}/{MaxProcessAttempts}).");
                await Task.Delay(TimeSpan.FromSeconds(retryDelaySeconds), cancellationToken).ConfigureAwait(false);
            }

            ReportStatus(pending, DownloadBackend.Aria2, DownloadTransferPhase.Preparing, attempt, MaxProcessAttempts);

            AttemptResult result;
            try
            {
                result = await RunAria2AttemptAsync(aria2Path, pending, attempt, cancellationToken).ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                throw;
            }
            catch (AriaProcessTerminationException)
            {
                // Advancing would allow another backend to write while aria2c
                // may still own the destination.
                throw;
            }
            catch (Exception ex)
            {
                result = AttemptResult.FailAll(pending, ex.Message);
            }

            if (result.Failures.Count == 0)
                return;
            ariaFatal = !result.RetryAllowed;

            pending = result.Failures.Keys.ToList();
            foreach (var failure in result.Failures)
                latestErrors[failure.Key] = failure.Value;
        }

        // Phase 2: system curl.exe (uses a different resolver/TLS stack than aria2c).
        if (pending.Count > 0)
        {
            Debug.WriteLine($"[FastDownloadService] aria2c left {pending.Count} file(s); retrying with system curl.");
            ReportStatus(pending, DownloadBackend.Curl, DownloadTransferPhase.Preparing, 1, 1);
            AttemptResult curlResult;
            try
            {
                curlResult = await RunCurlAttemptAsync(pending, cancellationToken).ConfigureAwait(false);
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

            if (curlResult.Failures.Count == 0)
                return;

            pending = curlResult.Failures.Keys.ToList();
            foreach (var failure in curlResult.Failures)
                latestErrors[failure.Key] = failure.Value;
        }

        // Phase 3: in-process HttpClient (last resort; also distinct resolver/TLS behavior).
        if (pending.Count > 0)
        {
            Debug.WriteLine($"[FastDownloadService] curl left {pending.Count} file(s); retrying with HttpClient.");
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
                latestErrors[failure.Key] = failure.Value;
        }

        var details = string.Join(Environment.NewLine, pending.Select(request =>
        {
            latestErrors.TryGetValue(request, out var error);
            return $"{Path.GetFileName(request.DestinationPath)}: {error ?? "unknown download error"}";
        }));
        throw new DownloadException(
            $"All configured download backends failed for {pending.Count} file(s): aria2c, curl, and HttpClient." + Environment.NewLine +
            "Review the per-file errors below. You can retry later, try another connection, or use the manual game-file download linked in the #help channel and point the installer at that folder." + Environment.NewLine +
            details);
    }

    private async Task<AttemptResult> RunAria2AttemptAsync(string aria2Path, IReadOnlyCollection<DownloadRequest> requests, int attempt, CancellationToken cancellationToken)
    {
        var rpcPort = GetFreeTcpPort();
        var rpcSecret = Guid.NewGuid().ToString("N");
        var gidMap = new Dictionary<string, DownloadRequest>(StringComparer.OrdinalIgnoreCase);
        var completedLengths = new Dictionary<string, long>(StringComparer.OrdinalIgnoreCase);
        var remaining = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var failures = new Dictionary<DownloadRequest, string>();
        var retryAllowed = true;

        var startInfo = new ProcessStartInfo
        {
            FileName = aria2Path,
            Arguments = BuildAria2Arguments(rpcPort, rpcSecret),
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardError = true,
            RedirectStandardOutput = true
        };

        using var process = new Process { StartInfo = startInfo, EnableRaisingEvents = true };
        using var cancellationRegistration = cancellationToken.Register(() => KillProcess(process));
        Task stdoutTask = Task.CompletedTask;
        Task stderrTask = Task.CompletedTask;
        Task waitTask = CreateProcessExitTask(process);
        var processStarted = false;

        try
        {
            Debug.WriteLine($"[FastDownloadService] Starting aria2c attempt {attempt}/{MaxProcessAttempts} for {requests.Count} file(s).");
            if (!process.Start())
                throw new DownloadException("Failed to start aria2c.");

            processStarted = true;
            _activeProcess = process;
            stdoutTask = DrainOutputAsync(process.StandardOutput, "stdout");
            stderrTask = DrainOutputAsync(process.StandardError, "stderr");

            await WaitForRpcAsync(rpcPort, rpcSecret, waitTask, cancellationToken).ConfigureAwait(false);

            foreach (var request in requests)
            {
                cancellationToken.ThrowIfCancellationRequested();
                var gid = Guid.NewGuid().ToString("N").Substring(0, 16);
                request.Gid = gid;
                gidMap[gid] = request;
                completedLengths[gid] = 0;
                remaining.Add(gid);
                await AddDownloadAsync(rpcPort, rpcSecret, request).ConfigureAwait(false);
            }

            var lastProgressUtc = DateTime.UtcNow;
            while (remaining.Count > 0)
            {
                cancellationToken.ThrowIfCancellationRequested();
                if (waitTask.IsCompleted)
                {
                    await waitTask.ConfigureAwait(false);
                    throw new DownloadException($"aria2c exited unexpectedly with code {process.ExitCode}.");
                }

                foreach (var gid in remaining.ToList())
                {
                    var request = gidMap[gid];
                    var status = await TellStatusAsync(rpcPort, rpcSecret, gid).ConfigureAwait(false);
                    if (status == null)
                        continue;

                    DownloadProgressChanged?.Invoke(new DownloadProgressUpdate(
                        request.DestinationPath,
                        status.CompletedLength,
                        status.TotalLength,
                        DownloadBackend.Aria2,
                        string.Equals(status.Status, "complete", StringComparison.OrdinalIgnoreCase)
                            ? DownloadTransferPhase.TransferComplete
                            : DownloadTransferPhase.Downloading,
                        attempt,
                        MaxProcessAttempts));

                    if (status.CompletedLength > completedLengths[gid])
                    {
                        completedLengths[gid] = status.CompletedLength;
                        lastProgressUtc = DateTime.UtcNow;
                    }

                    if (string.Equals(status.Status, "complete", StringComparison.OrdinalIgnoreCase))
                    {
                        remaining.Remove(gid);
                    }
                    else if (string.Equals(status.Status, "error", StringComparison.OrdinalIgnoreCase) ||
                             string.Equals(status.Status, "removed", StringComparison.OrdinalIgnoreCase))
                    {
                        failures[request] = status.ErrorMessage ?? status.Status;
                        remaining.Remove(gid);
                    }
                }

                if (remaining.Count > 0 && DateTime.UtcNow - lastProgressUtc >= _noProgressTimeout)
                {
                    var timeoutMessage = $"no download progress for {(int)_noProgressTimeout.TotalSeconds} seconds";
                    foreach (var gid in remaining)
                        failures[gidMap[gid]] = timeoutMessage;
                    remaining.Clear();
                    retryAllowed = false;
                    Debug.WriteLine($"[FastDownloadService] Falling back from aria2c after {timeoutMessage}.");
                }

                if (remaining.Count > 0)
                    await Task.Delay(ProgressPollMs, cancellationToken).ConfigureAwait(false);
            }

            await ShutdownAria2Async(rpcPort, rpcSecret).ConfigureAwait(false);
            var exitCode = await TryGetProcessExitCodeWithinAsync(
                process,
                waitTask,
                ProcessShutdownTimeoutMs).ConfigureAwait(false);
            if (!exitCode.HasValue)
            {
                Debug.WriteLine("[FastDownloadService] aria2c did not exit after RPC shutdown; terminating it.");
                KillProcess(process);
                exitCode = await TryGetProcessExitCodeWithinAsync(
                    process,
                    waitTask,
                    ProcessKillTimeoutMs).ConfigureAwait(false);
                if (!exitCode.HasValue)
                    throw new AriaProcessTerminationException("aria2c did not exit after it was terminated.");
            }

            if (exitCode.Value != 0)
                Debug.WriteLine($"[FastDownloadService] aria2c attempt {attempt} exited with code {exitCode.Value} after reaching terminal download states.");
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (AriaProcessTerminationException)
        {
            throw;
        }
        catch (Exception ex)
        {
            var attributedFailure = false;
            foreach (var gid in remaining)
            {
                failures[gidMap[gid]] = ex.Message;
                attributedFailure = true;
            }

            foreach (var request in requests)
            {
                if (!gidMap.ContainsValue(request))
                {
                    failures[request] = ex.Message;
                    attributedFailure = true;
                }
            }

            if (!attributedFailure && failures.Count == 0)
                throw new DownloadException($"aria2c lifecycle failure: {ex.Message}", ex);
        }
        finally
        {
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
                        throw new AriaProcessTerminationException("aria2c did not exit during attempt cleanup.");
                }
                catch (AriaProcessTerminationException)
                {
                    throw;
                }
                catch (Exception ex)
                {
                    throw new AriaProcessTerminationException("Failed while waiting for aria2c to exit during attempt cleanup.", ex);
                }
            }

            try { await CompletesWithinAsync(Task.WhenAll(stdoutTask, stderrTask), ProcessKillTimeoutMs).ConfigureAwait(false); }
            catch (Exception) { }

            if (ReferenceEquals(_activeProcess, process))
                _activeProcess = null;
        }

        return new AttemptResult(failures, retryAllowed);
    }

    private static string ResolveCurlPath()
    {
        // Windows 10 1803+ ships curl.exe in System32. Prefer it so the fallback
        // uses the OS resolver/TLS stack, which differs from aria2c's.
        var candidates = new[]
        {
            Path.Combine(Environment.SystemDirectory, "curl.exe"),
            "curl.exe"
        };

        foreach (var candidate in candidates)
        {
            try
            {
                if (File.Exists(candidate))
                    return candidate;
            }
            catch
            {
                // Ignore probe failures; fall through to the next candidate.
            }
        }

        return null;
    }

    private async Task<AttemptResult> RunCurlAttemptAsync(IReadOnlyCollection<DownloadRequest> requests, CancellationToken cancellationToken)
    {
        var curlPath = ResolveCurlPath();
        if (string.IsNullOrEmpty(curlPath))
            return AttemptResult.FailAll(requests, "system curl.exe was not found");

        var failures = new Dictionary<DownloadRequest, string>();
        foreach (var request in requests)
        {
            cancellationToken.ThrowIfCancellationRequested();

            var destinationPath = Path.GetFullPath(request.DestinationPath);
            Directory.CreateDirectory(Path.GetDirectoryName(destinationPath));
            var partialPath = destinationPath + ".curl.partial";
            PreparePartialForBackend(destinationPath, partialPath);

            var arguments = string.Join(" ", new[]
            {
                "--fail",
                "--location",
                "--retry", "3",
                "--retry-delay", "2",
                "--connect-timeout", "15",
                "--max-time", "900",
                "--continue-at", "-",
                "--output", $"\"{partialPath}\"",
                $"\"{request.Url}\""
            });

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

                using var contentStream = await response.Content.ReadAsStreamAsync().ConfigureAwait(false);
                using var fileStream = new FileStream(partialPath, mode, FileAccess.Write, FileShare.None, 65536, useAsync: true);
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

    private static string BuildAria2Arguments(int rpcPort, string rpcSecret)
    {
        return string.Join(" ", new[]
        {
            "--continue=true",
            "--allow-overwrite=true",
            "--auto-file-renaming=false",
            "--file-allocation=none",
            "--max-concurrent-downloads=4",
            "--max-connection-per-server=2",
            "--split=2",
            "--min-split-size=16M",
            "--async-dns=false",
            "--retry-wait=2",
            "--max-tries=8",
            "--timeout=30",
            "--connect-timeout=15",
            "--summary-interval=1",
            "--download-result=hide",
            "--console-log-level=warn",
            "--enable-rpc=true",
            "--rpc-listen-all=false",
            "--rpc-listen-port=" + rpcPort,
            "--rpc-secret=" + rpcSecret,
            "--check-certificate=true"
        });
    }

    private async Task WaitForRpcAsync(int rpcPort, string rpcSecret, Task waitTask, CancellationToken cancellationToken)
    {
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(10);
        while (DateTime.UtcNow < deadline)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (waitTask.IsCompleted)
                throw new DownloadException("aria2c exited before the RPC server started.");

            try
            {
                await CallRpcAsync(rpcPort, rpcSecret, "aria2.getVersion", new JArray()).ConfigureAwait(false);
                return;
            }
            catch (WebException)
            {
                await Task.Delay(100, cancellationToken).ConfigureAwait(false);
            }
        }

        throw new TimeoutException("Timed out waiting for aria2c RPC server to start.");
    }

    private async Task AddDownloadAsync(int rpcPort, string rpcSecret, DownloadRequest request)
    {
        var destinationPath = Path.GetFullPath(request.DestinationPath);
        var directory = Path.GetDirectoryName(destinationPath);
        if (string.IsNullOrEmpty(directory))
            throw new IOException($"Failed to determine destination directory for '{destinationPath}'.");

        Directory.CreateDirectory(directory);

        var options = new JObject
        {
            ["gid"] = request.Gid,
            ["dir"] = directory,
            ["out"] = Path.GetFileName(destinationPath)
        };

        var parameters = new JArray
        {
            new JArray(request.Url),
            options
        };

        await CallRpcAsync(rpcPort, rpcSecret, "aria2.addUri", parameters).ConfigureAwait(false);
    }

    private async Task<Aria2Status> TellStatusAsync(int rpcPort, string rpcSecret, string gid)
    {
        var parameters = new JArray
        {
            gid,
            new JArray("completedLength", "totalLength", "status", "errorMessage")
        };

        var result = await CallRpcAsync(rpcPort, rpcSecret, "aria2.tellStatus", parameters).ConfigureAwait(false);
        if (result == null)
            return null;

        return new Aria2Status
        {
            CompletedLength = ParseAria2Length((string)result["completedLength"]),
            TotalLength = ParseAria2Length((string)result["totalLength"]),
            Status = (string)result["status"],
            ErrorMessage = (string)result["errorMessage"]
        };
    }

    private async Task ShutdownAria2Async(int rpcPort, string rpcSecret)
    {
        try
        {
            await CallRpcAsync(rpcPort, rpcSecret, "aria2.shutdown", new JArray()).ConfigureAwait(false);
        }
        catch (WebException)
        {
        }
    }

    private async Task<JToken> CallRpcAsync(int rpcPort, string rpcSecret, string method, JArray parameters)
    {
        var allParameters = new JArray { "token:" + rpcSecret };
        foreach (var parameter in parameters)
            allParameters.Add(parameter);

        var requestJson = new JObject
        {
            ["jsonrpc"] = "2.0",
            ["id"] = "r1delta",
            ["method"] = method,
            ["params"] = allParameters
        }.ToString(Newtonsoft.Json.Formatting.None);

        var request = (HttpWebRequest)WebRequest.Create($"http://127.0.0.1:{rpcPort}/jsonrpc");
        request.Method = "POST";
        request.ContentType = "application/json";
        request.Timeout = 2000;
        request.ReadWriteTimeout = 2000;

        using (var requestStream = await AwaitRpcAsync(request.GetRequestStreamAsync(), request).ConfigureAwait(false))
        using (var writer = new StreamWriter(requestStream))
        {
            await AwaitRpcAsync(writer.WriteAsync(requestJson), request).ConfigureAwait(false);
        }

        using (var response = (HttpWebResponse)await AwaitRpcAsync(request.GetResponseAsync(), request).ConfigureAwait(false))
        using (var responseStream = response.GetResponseStream())
        using (var reader = new StreamReader(responseStream))
        {
            var responseJson = await AwaitRpcAsync(reader.ReadToEndAsync(), request).ConfigureAwait(false);
            var responseObject = JObject.Parse(responseJson);
            var error = responseObject["error"];
            if (error != null)
                throw new DownloadException((string)error["message"] ?? $"aria2 RPC call failed: {method}");

            return responseObject["result"];
        }
    }

    private static async Task<T> AwaitRpcAsync<T>(Task<T> operation, HttpWebRequest request)
    {
        if (await Task.WhenAny(operation, Task.Delay(RpcTimeoutMs)).ConfigureAwait(false) != operation)
        {
            request.Abort();
            throw new WebException("Timed out waiting for aria2 RPC.", WebExceptionStatus.Timeout);
        }

        return await operation.ConfigureAwait(false);
    }

    private static async Task AwaitRpcAsync(Task operation, HttpWebRequest request)
    {
        if (await Task.WhenAny(operation, Task.Delay(RpcTimeoutMs)).ConfigureAwait(false) != operation)
        {
            request.Abort();
            throw new WebException("Timed out waiting for aria2 RPC.", WebExceptionStatus.Timeout);
        }

        await operation.ConfigureAwait(false);
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
    private static string ResolveAria2Path()
    {
        var baseDirectory = AppDomain.CurrentDomain.BaseDirectory;
        var candidates = new[]
        {
            Path.Combine(baseDirectory, "tools", "aria2", "aria2c.exe"),
            Path.Combine(baseDirectory, "aria2c.exe"),
            Path.Combine(Environment.CurrentDirectory, "launcher_ex", "tools", "aria2", "aria2c.exe"),
            Path.Combine(Environment.CurrentDirectory, "tools", "aria2", "aria2c.exe")
        };

        foreach (var candidate in candidates)
        {
            if (File.Exists(candidate))
                return candidate;
        }

        return null;
    }

    private static long ParseAria2Length(string value)
    {
        if (long.TryParse(value, out var result) && result > 0)
            return result;
        return 0;
    }

    private static int GetFreeTcpPort()
    {
        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        try
        {
            return ((IPEndPoint)listener.LocalEndpoint).Port;
        }
        finally
        {
            listener.Stop();
        }
    }

    private static async Task DrainOutputAsync(StreamReader reader, string streamName)
    {
        string line;
        while ((line = await reader.ReadLineAsync().ConfigureAwait(false)) != null)
        {
            if (line.Length > 0)
                Debug.WriteLine($"[aria2c:{streamName}] {line}");
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
            Debug.WriteLine($"[FastDownloadService] Failed to kill aria2c: {ex.Message}");
        }
    }

    public sealed class DownloadRequest
    {
        public string Url { get; set; }
        public string DestinationPath { get; set; }
        public string Gid { get; set; }
    }

    private sealed class AriaProcessTerminationException : DownloadException
    {
        public AriaProcessTerminationException(string message) : base(message) { }
        public AriaProcessTerminationException(string message, Exception innerException) : base(message, innerException) { }
    }

    private sealed class DownloadProcessTerminationException : DownloadException
    {
        public DownloadProcessTerminationException(string message) : base(message) { }
        public DownloadProcessTerminationException(string message, Exception innerException) : base(message, innerException) { }
    }

    private sealed class AttemptResult
    {
        public AttemptResult(Dictionary<DownloadRequest, string> failures, bool retryAllowed = true)
        {
            Failures = failures;
            RetryAllowed = retryAllowed;
        }

        public Dictionary<DownloadRequest, string> Failures { get; }
        public bool RetryAllowed { get; }

        public static AttemptResult FailAll(IEnumerable<DownloadRequest> requests, string error)
        {
            return new AttemptResult(requests.ToDictionary(request => request, request => error));
        }
    }

    private sealed class Aria2Status
    {
        public long CompletedLength { get; set; }
        public long TotalLength { get; set; }
        public string Status { get; set; }
        public string ErrorMessage { get; set; }
    }
}

public enum DownloadBackend
{
    Aria2,
    Curl,
    HttpClient
}

public enum DownloadTransferPhase
{
    Preparing,
    RetryDelay,
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
