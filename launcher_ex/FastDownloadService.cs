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
    private const int ProcessKillTimeoutMs = 2000;
    private const int MaxProcessAttempts = 5;
    private static readonly int[] RetryDelaysSeconds = { 0, 2, 5, 10, 20 };
    private static readonly TimeSpan DefaultNoProgressTimeout = TimeSpan.FromSeconds(90);

    private readonly TimeSpan _noProgressTimeout;
    private bool _disposed;
    private Process _activeProcess;

    public event Action<string, long, long> DownloadProgressChanged;

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

        var pending = requests;
        var latestErrors = new Dictionary<DownloadRequest, string>();

        // Phase 1: aria2c. A process-termination failure no longer aborts the
        // whole install; the remaining files fall through to the next stack.
        var ariaFatal = false;
        for (var attempt = 1; attempt <= MaxProcessAttempts && !ariaFatal; attempt++)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (_disposed)
                throw new ObjectDisposedException(nameof(FastDownloadService));

            var retryDelaySeconds = RetryDelaysSeconds[attempt - 1];
            if (retryDelaySeconds > 0)
            {
                Debug.WriteLine($"[FastDownloadService] Retrying {pending.Count} failed file(s) in {retryDelaySeconds} seconds (attempt {attempt}/{MaxProcessAttempts}).");
                await Task.Delay(TimeSpan.FromSeconds(retryDelaySeconds), cancellationToken).ConfigureAwait(false);
            }

            AttemptResult result;
            try
            {
                result = await RunAria2AttemptAsync(aria2Path, pending, attempt, cancellationToken).ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                throw;
            }
            catch (AriaProcessTerminationException ex)
            {
                Debug.WriteLine($"[FastDownloadService] aria2c terminated abnormally on attempt {attempt}: {ex.Message}. Falling back to the next download stack.");
                result = AttemptResult.FailAll(pending, ex.Message);
                ariaFatal = true;
            }
            catch (Exception ex)
            {
                result = AttemptResult.FailAll(pending, ex.Message);
            }

            if (result.Failures.Count == 0)
                return;

            pending = result.Failures.Keys.ToList();
            foreach (var failure in result.Failures)
                latestErrors[failure.Key] = failure.Value;
        }

        // Phase 2: system curl.exe (uses a different resolver/TLS stack than aria2c).
        if (pending.Count > 0)
        {
            Debug.WriteLine($"[FastDownloadService] aria2c left {pending.Count} file(s); retrying with system curl.");
            AttemptResult curlResult;
            try
            {
                curlResult = await RunCurlAttemptAsync(pending, cancellationToken).ConfigureAwait(false);
            }
            catch (OperationCanceledException)
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
            $"Could not download {pending.Count} file(s) with aria2c, curl, or HttpClient." + Environment.NewLine +
            "This usually means your network is blocking the download domain (DNS filtering or ISP blocks)." + Environment.NewLine +
            "Try again later, switch networks/DNS, or download the game files manually from the Google Drive mirror linked in the #help channel, then point the installer at that folder." + Environment.NewLine +
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
        Task<int> waitTask = null;
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
            waitTask = Task.Run(() =>
            {
                process.WaitForExit();
                return process.ExitCode;
            }, CancellationToken.None);

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
                    throw new DownloadException($"aria2c exited unexpectedly with code {await waitTask.ConfigureAwait(false)}.");

                foreach (var gid in remaining.ToList())
                {
                    var request = gidMap[gid];
                    var status = await TellStatusAsync(rpcPort, rpcSecret, gid).ConfigureAwait(false);
                    if (status == null)
                        continue;

                    DownloadProgressChanged?.Invoke(request.DestinationPath, status.CompletedLength, Math.Max(status.TotalLength, status.CompletedLength));

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
                    Debug.WriteLine($"[FastDownloadService] Restarting aria2c after {timeoutMessage}.");
                }

                if (remaining.Count > 0)
                    await Task.Delay(ProgressPollMs, cancellationToken).ConfigureAwait(false);
            }

            await ShutdownAria2Async(rpcPort, rpcSecret).ConfigureAwait(false);
            if (!await CompletesWithinAsync(waitTask, ProcessShutdownTimeoutMs).ConfigureAwait(false))
            {
                Debug.WriteLine("[FastDownloadService] aria2c did not exit after RPC shutdown; terminating it.");
                KillProcess(process);
                if (!await CompletesWithinAsync(waitTask, ProcessKillTimeoutMs).ConfigureAwait(false))
                    throw new AriaProcessTerminationException("aria2c did not exit after it was terminated.");
            }

            var exitCode = await waitTask.ConfigureAwait(false);
            if (exitCode != 0)
                Debug.WriteLine($"[FastDownloadService] aria2c attempt {attempt} exited with code {exitCode} after reaching terminal download states.");
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

            if (waitTask != null)
            {
                if (!await CompletesWithinAsync(waitTask, ProcessKillTimeoutMs).ConfigureAwait(false))
                    throw new AriaProcessTerminationException("aria2c did not exit during attempt cleanup.");

                try { await waitTask.ConfigureAwait(false); }
                catch (Exception ex) { throw new AriaProcessTerminationException("Failed while waiting for aria2c to exit during attempt cleanup.", ex); }
            }

            try { await CompletesWithinAsync(Task.WhenAll(stdoutTask, stderrTask), ProcessKillTimeoutMs).ConfigureAwait(false); }
            catch (Exception) { }

            if (ReferenceEquals(_activeProcess, process))
                _activeProcess = null;
        }

        return new AttemptResult(failures);
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

            var arguments = string.Join(" ", new[]
            {
                "--fail",
                "--location",
                "--retry", "3",
                "--retry-delay", "2",
                "--connect-timeout", "15",
                "--max-time", "900",
                "--continue-at", "-",
                "--output", $"\"{destinationPath}\"",
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

            try
            {
                using var process = new Process { StartInfo = startInfo };
                if (!process.Start())
                {
                    failures[request] = "failed to start curl.exe";
                    continue;
                }

                _activeProcess = process;
                var stderrTask = DrainOutputAsync(process.StandardError, "curl-stderr");
                var stdoutTask = DrainOutputAsync(process.StandardOutput, "curl-stdout");

                var exited = await WaitForExitAsync(process, TimeSpan.FromMinutes(20), cancellationToken).ConfigureAwait(false);
                if (!exited)
                {
                    KillProcess(process);
                    failures[request] = "curl.exe timed out";
                    continue;
                }

                var exitCode = process.ExitCode;
                if (exitCode != 0)
                {
                    await Task.WhenAll(stderrTask, stdoutTask).ConfigureAwait(false);
                    failures[request] = $"curl exited with code {exitCode}";
                }
            }
            catch (OperationCanceledException)
            {
                throw;
            }
            catch (Exception ex)
            {
                failures[request] = $"curl failed: {ex.Message}";
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

                using var response = await client.SendAsync(requestMessage, System.Net.Http.HttpCompletionOption.ResponseHeadersRead, cancellationToken).ConfigureAwait(false);
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
                int bytesRead;
                while ((bytesRead = await contentStream.ReadAsync(buffer, 0, buffer.Length, cancellationToken).ConfigureAwait(false)) > 0)
                {
                    await fileStream.WriteAsync(buffer, 0, bytesRead, cancellationToken).ConfigureAwait(false);
                    received += bytesRead;

                    var now = DateTime.UtcNow;
                    if (now - lastProgress >= TimeSpan.FromMilliseconds(ProgressPollMs))
                    {
                        lastProgress = now;
                        var total = response.Content.Headers.ContentLength ?? received;
                        DownloadProgressChanged?.Invoke(request.DestinationPath, received, Math.Max(total, received));
                    }
                }

                if (File.Exists(destinationPath))
                    File.Delete(destinationPath);
                File.Move(partialPath, destinationPath);
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

    private static async Task<bool> WaitForExitAsync(Process process, TimeSpan timeout, CancellationToken cancellationToken)
    {
        var waitTask = Task.Run(() => process.WaitForExit(), CancellationToken.None);
        var winner = await Task.WhenAny(waitTask, Task.Delay(timeout, cancellationToken)).ConfigureAwait(false);
        if (winner != waitTask)
            return false;

        await waitTask.ConfigureAwait(false);
        return true;
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;

        var process = _activeProcess;
        if (process != null)
            KillProcess(process);
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

    private async Task WaitForRpcAsync(int rpcPort, string rpcSecret, Task<int> waitTask, CancellationToken cancellationToken)
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

    private static async Task<bool> CompletesWithinAsync(Task task, int timeoutMs)
    {
        return await Task.WhenAny(task, Task.Delay(timeoutMs)).ConfigureAwait(false) == task;
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

    private sealed class Aria2Status
    {
        public long CompletedLength { get; set; }
        public long TotalLength { get; set; }
        public string Status { get; set; }
        public string ErrorMessage { get; set; }
    }
}

public class DownloadException : Exception
{
    public DownloadException(string message) : base(message) { }
    public DownloadException(string message, Exception innerException) : base(message, innerException) { }
}
