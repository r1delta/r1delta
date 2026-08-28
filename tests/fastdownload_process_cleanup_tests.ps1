param(
    [Parameter(Mandatory = $true)]
    [string]$LauncherPath
)

$ErrorActionPreference = 'Stop'
$binding = [System.Reflection.BindingFlags]'Public,NonPublic,Static,Instance'
$assembly = [System.Reflection.Assembly]::LoadFrom((Resolve-Path -LiteralPath $LauncherPath))
$serviceType = $assembly.GetType('FastDownloadService', $true)
$requestType = $serviceType.GetNestedType('DownloadRequest', $binding)
$constructor = $serviceType.GetConstructor(
    $binding,
    $null,
    [Type[]]@([string], [TimeSpan]),
    $null)
$createExitTask = $serviceType.GetMethod('CreateProcessExitTask', $binding)
$tryGetExitCode = $serviceType.GetMethod('TryGetProcessExitCodeWithinAsync', $binding)
$curlAttempt = $serviceType.GetMethod('RunCurlAttemptAsync', $binding)
$buildCurlArguments = $serviceType.GetMethod('BuildCurlArguments', $binding)
$resolveBundledCurl = $serviceType.GetMethod('ResolveBundledCurlPath', $binding)
$downloadFiles = $serviceType.GetMethod('DownloadFilesAsync', $binding)
$activeProcessField = $serviceType.GetField('_activeProcess', $binding)

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "FAILED: $Message"
    }
    Write-Host "PASS: $Message"
}

function New-CommandProcess([string]$Arguments) {
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $process.StartInfo.FileName = $env:ComSpec
    $process.StartInfo.Arguments = $Arguments
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.CreateNoWindow = $true
    $process.EnableRaisingEvents = $true
    return $process
}

function New-RequestArray(
    [string]$Url,
    [string]$Destination,
    [long]$ExpectedSize = 0
) {
    $request = [Activator]::CreateInstance($requestType)
    $requestType.GetProperty('Url').SetValue($request, $Url)
    $requestType.GetProperty('DestinationPath').SetValue($request, $Destination)
    $requestType.GetProperty('ExpectedSize').SetValue($request, $ExpectedSize)
    $requests = [Array]::CreateInstance($requestType, 1)
    $requests.SetValue($request, 0)
    return ,$requests
}

function Read-HttpRequestHeaders([System.IO.Stream]$Stream) {
    $bytes = [System.Collections.Generic.List[byte]]::new()
    while ($bytes.Count -lt 32768) {
        $value = $Stream.ReadByte()
        if ($value -lt 0) {
            throw 'HTTP client closed before completing its request headers.'
        }
        $bytes.Add([byte]$value)
        $count = $bytes.Count
        if ($count -ge 4 -and
            $bytes[$count - 4] -eq 13 -and
            $bytes[$count - 3] -eq 10 -and
            $bytes[$count - 2] -eq 13 -and
            $bytes[$count - 1] -eq 10) {
            return [System.Text.Encoding]::ASCII.GetString($bytes.ToArray())
        }
    }
    throw 'HTTP request headers exceeded the test bound.'
}

$root = Join-Path ([System.IO.Path]::GetTempPath()) ('r1delta-process-cleanup-' + [Guid]::NewGuid().ToString('N'))
$service = $null
$listener = $null
$client = $null
try {
    New-Item -ItemType Directory -Path $root | Out-Null

    Assert-True ($null -ne $createExitTask) 'launcher exposes an event-backed child exit observer'
    Assert-True ($null -ne $tryGetExitCode) 'launcher exposes bounded exit observation with an OS-state fallback'

    $observedProcess = New-CommandProcess '/d /c exit 23'
    try {
        $observedExitTask = $createExitTask.Invoke($null, [object[]]@($observedProcess))
        Assert-True $observedProcess.Start() 'observer test process starts'
        $null = $observedExitTask.GetAwaiter().GetResult()
        $observedResultTask = $tryGetExitCode.Invoke(
            $null,
            [object[]]@($observedProcess, $observedExitTask, 1000))
        $observedExitCode = $observedResultTask.GetAwaiter().GetResult()
        Assert-True ($null -ne $observedExitCode -and [int]$observedExitCode -eq 23) 'event-backed observer exposes the child exit code through its owner'
    }
    finally {
        $observedProcess.Dispose()
    }

    $staleProcess = New-CommandProcess '/d /c exit 31'
    try {
        Assert-True $staleProcess.Start() 'stale-observer test process starts'
        Assert-True $staleProcess.WaitForExit(5000) 'stale-observer test process exits'
        $staleSource = [System.Threading.Tasks.TaskCompletionSource[bool]]::new()
        $staleResultTask = $tryGetExitCode.Invoke(
            $null,
            [object[]]@($staleProcess, $staleSource.Task, 25))
        $staleExitCode = $staleResultTask.GetAwaiter().GetResult()
        Assert-True ($null -ne $staleExitCode -and [int]$staleExitCode -eq 31) 'OS exit state wins when asynchronous notification is delayed'
    }
    finally {
        $staleProcess.Dispose()
    }

    $liveProcess = New-CommandProcess '/d /c ping.exe -n 30 127.0.0.1 >nul'
    try {
        Assert-True $liveProcess.Start() 'live-child test process starts'
        $liveSource = [System.Threading.Tasks.TaskCompletionSource[bool]]::new()
        $liveResultTask = $tryGetExitCode.Invoke(
            $null,
            [object[]]@($liveProcess, $liveSource.Task, 25))
        $liveExitCode = $liveResultTask.GetAwaiter().GetResult()
        Assert-True ($null -eq $liveExitCode) 'bounded observer does not report a live child as exited'
    }
    finally {
        if (-not $liveProcess.HasExited) {
            $liveProcess.Kill()
            $liveProcess.WaitForExit(5000) | Out-Null
        }
        $liveProcess.Dispose()
    }

    $launcherDirectory = Split-Path -Parent (Resolve-Path -LiteralPath $LauncherPath)
    $curlPath = Join-Path $launcherDirectory 'tools/curl/curl.exe'
    Assert-True (Test-Path -LiteralPath $curlPath -PathType Leaf) 'bundled static curl is available for lifecycle smoke'
    $resolvedCurlPath = [string]$resolveBundledCurl.Invoke($null, $null)
    Assert-True (
        [System.IO.Path]::GetFullPath($resolvedCurlPath) -eq [System.IO.Path]::GetFullPath($curlPath)
    ) 'launcher resolves only the hash-verified assembly-relative curl binary'
    $curlVersion = (& $curlPath -V 2>&1) -join "`n"
    Assert-True ($LASTEXITCODE -eq 0) 'bundled static curl reports its version'
    Assert-True (
        $curlVersion.Contains(' ECH ') -and $curlVersion.Contains(' HTTPSRR ')
    ) 'bundled static curl reports ECH and HTTPS RR support'

    $completeDestination = Join-Path $root 'complete-resume.bin'
    $completePayload = [System.Text.Encoding]::UTF8.GetBytes('already complete curl partial')
    [System.IO.File]::WriteAllBytes($completeDestination + '.curl.partial', $completePayload)
    $completeRequests = New-RequestArray 'http://127.0.0.1:1/must-not-connect' $completeDestination $completePayload.Length
    $completeService = $constructor.Invoke([object[]]@([string]$root, [TimeSpan]::FromSeconds(5)))
    try {
        $completeTask = $downloadFiles.Invoke(
            $completeService,
            [object[]]@([object]$completeRequests, [System.Threading.CancellationToken]::None))
        Assert-True $completeTask.Wait(5000) 'complete curl partial is handled without a network request'
        $null = $completeTask.GetAwaiter().GetResult()
    }
    finally {
        ([IDisposable]$completeService).Dispose()
    }
    Assert-True ([System.Linq.Enumerable]::SequenceEqual([byte[]](Get-Content -LiteralPath $completeDestination -AsByteStream), $completePayload)) 'complete curl partial is promoted intact'
    Assert-True (-not (Test-Path -LiteralPath ($completeDestination + '.curl.partial'))) 'complete curl partial sidecar is removed after promotion'

    $service = $constructor.Invoke([object[]]@([string]$root, [TimeSpan]::FromSeconds(5)))
    $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    $listener.Start()
    $port = ([System.Net.IPEndPoint]$listener.LocalEndpoint).Port
    $destination = Join-Path $root 'curl-smoke.bin'
    $payload = [System.Text.Encoding]::UTF8.GetBytes('r1delta curl cleanup regression payload')
    $requests = New-RequestArray "http://127.0.0.1:$port/payload.bin" $destination $payload.Length
    $curlArguments = [string]$buildCurlArguments.Invoke(
        $null,
        [object[]]@($requests.GetValue(0), [string]($destination + '.curl.partial')))
    Assert-True ($curlArguments -like '*--ech true*') 'curl command explicitly enables opportunistic ECH'
    Assert-True ($curlArguments -like '*--ca-native*') 'curl command uses the Windows native CA store'
    $acceptTask = $listener.AcceptTcpClientAsync()
    $attemptTask = $curlAttempt.Invoke(
        $service,
        [object[]]@([string]$curlPath, [object]$requests, [System.Threading.CancellationToken]::None))

    Assert-True $acceptTask.Wait(10000) 'curl connects to the local download endpoint'
    $client = $acceptTask.GetAwaiter().GetResult()
    $client.ReceiveTimeout = 5000
    $client.SendTimeout = 5000
    $stream = $client.GetStream()
    $headers = Read-HttpRequestHeaders $stream
    Assert-True ($headers -like 'GET /payload.bin HTTP/*') 'curl requests the expected local payload'
    $responseHeaders = [System.Text.Encoding]::ASCII.GetBytes(
        "HTTP/1.1 200 OK`r`nContent-Length: $($payload.Length)`r`nConnection: close`r`n`r`n")
    $stream.Write($responseHeaders, 0, $responseHeaders.Length)
    $stream.Write($payload, 0, $payload.Length)
    $stream.Flush()
    $client.Dispose()
    $client = $null

    Assert-True $attemptTask.Wait(20000) 'curl attempt completes including process cleanup'
    $attemptResult = $attemptTask.GetAwaiter().GetResult()
    $failures = $attemptResult.GetType().GetProperty('Failures', $binding).GetValue($attemptResult)
    Assert-True ($failures.Count -eq 0) 'successful curl transfer has no attributed failure'
    Assert-True ([System.Linq.Enumerable]::SequenceEqual([byte[]](Get-Content -LiteralPath $destination -AsByteStream), $payload)) 'curl writes the expected payload'
    Assert-True ($null -eq $activeProcessField.GetValue($service)) 'curl cleanup releases active process ownership'

    ([IDisposable]$service).Dispose()
    $service = $null
    $listener.Stop()
    $listener = $null

    $service = $constructor.Invoke([object[]]@([string]$root, [TimeSpan]::FromSeconds(1)))
    $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    $listener.Start()
    $stallPort = ([System.Net.IPEndPoint]$listener.LocalEndpoint).Port
    $stallDestination = Join-Path $root 'curl-stall.bin'
    $stallRequests = New-RequestArray "http://127.0.0.1:$stallPort/stall.bin" $stallDestination
    $stallAcceptTask = $listener.AcceptTcpClientAsync()
    $stallAttemptTask = $curlAttempt.Invoke(
        $service,
        [object[]]@([string]$curlPath, [object]$stallRequests, [System.Threading.CancellationToken]::None))

    Assert-True $stallAcceptTask.Wait(10000) 'curl connects to the stalled download endpoint'
    $client = $stallAcceptTask.GetAwaiter().GetResult()
    $client.ReceiveTimeout = 5000
    $stallHeaders = Read-HttpRequestHeaders $client.GetStream()
    Assert-True ($stallHeaders -like 'GET /stall.bin HTTP/*') 'stalled endpoint receives the curl request'
    Assert-True $stallAttemptTask.Wait(20000) 'curl no-progress attempt reaches its injected idle bound'
    $stallResult = $stallAttemptTask.GetAwaiter().GetResult()
    $stallFailures = $stallResult.GetType().GetProperty('Failures', $binding).GetValue($stallResult)
    Assert-True ($stallFailures.Count -eq 1) 'curl no-progress attempt attributes the stalled request'
    Assert-True ($null -eq $activeProcessField.GetValue($service)) 'stalled curl cleanup releases active process ownership'

    $client.Dispose()
    $client = $null
    $listener.Stop()
    $listener = $null
    ([IDisposable]$service).Dispose()
    $service = $constructor.Invoke([object[]]@([string]$root, [TimeSpan]::FromSeconds(30)))

    $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    $listener.Start()
    $fallbackPort = ([System.Net.IPEndPoint]$listener.LocalEndpoint).Port
    $fallbackDestination = Join-Path $root 'httpclient-fallback.bin'
    $fallbackPayload = [System.Text.Encoding]::UTF8.GetBytes('r1delta HttpClient fallback payload')
    $fallbackRequests = New-RequestArray "http://127.0.0.1:$fallbackPort/fallback.bin" $fallbackDestination $fallbackPayload.Length
    $downloadTask = $downloadFiles.Invoke(
        $service,
        [object[]]@([object]$fallbackRequests, [System.Threading.CancellationToken]::None))

    for ($requestNumber = 1; $requestNumber -le 5; $requestNumber++) {
        $fallbackAcceptTask = $listener.AcceptTcpClientAsync()
        if (-not $fallbackAcceptTask.Wait(15000)) {
            if ($downloadTask.IsCompleted) {
                try {
                    $null = $downloadTask.GetAwaiter().GetResult()
                }
                catch {
                    throw "FAILED: fallback request $requestNumber was not received; download task failed: $($_.Exception)"
                }
                throw "FAILED: fallback request $requestNumber was not received; download task completed without the expected request"
            }
            throw "FAILED: fallback request $requestNumber was not received; download task remains incomplete"
        }
        Write-Host "PASS: fallback endpoint accepts request $requestNumber"
        $client = $fallbackAcceptTask.GetAwaiter().GetResult()
        $client.ReceiveTimeout = 5000
        $client.SendTimeout = 5000
        $fallbackStream = $client.GetStream()
        $fallbackHeaders = Read-HttpRequestHeaders $fallbackStream
        Assert-True ($fallbackHeaders -like 'GET /fallback.bin HTTP/*') "fallback request $requestNumber targets the expected payload"
        if ($requestNumber -le 4) {
            $fallbackResponse = [System.Text.Encoding]::ASCII.GetBytes(
                "HTTP/1.1 503 Service Unavailable`r`nContent-Length: 0`r`nConnection: close`r`n`r`n")
            $fallbackStream.Write($fallbackResponse, 0, $fallbackResponse.Length)
        }
        else {
            $fallbackResponse = [System.Text.Encoding]::ASCII.GetBytes(
                "HTTP/1.1 200 OK`r`nContent-Length: $($fallbackPayload.Length)`r`nConnection: close`r`n`r`n")
            $fallbackStream.Write($fallbackResponse, 0, $fallbackResponse.Length)
            $fallbackStream.Write($fallbackPayload, 0, $fallbackPayload.Length)
        }
        $fallbackStream.Flush()
        $client.Dispose()
        $client = $null
    }

    Assert-True $downloadTask.Wait(30000) 'HttpClient fallback completes after curl exhausts its retries'
    $null = $downloadTask.GetAwaiter().GetResult()
    Assert-True ([System.Linq.Enumerable]::SequenceEqual([byte[]](Get-Content -LiteralPath $fallbackDestination -AsByteStream), $fallbackPayload)) 'HttpClient fallback promotes the expected payload'
    Assert-True ($null -eq $activeProcessField.GetValue($service)) 'fallback begins only after curl process ownership is released'

    Write-Host 'All fast-download process cleanup tests passed.'
}
finally {
    if ($null -ne $client) {
        try { $client.Dispose() } catch {}
    }
    if ($null -ne $listener) {
        try { $listener.Stop() } catch {}
    }
    if ($null -ne $service) {
        try { ([IDisposable]$service).Dispose() } catch {}
    }
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}
