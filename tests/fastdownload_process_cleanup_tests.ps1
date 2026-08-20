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
$ariaAttempt = $serviceType.GetMethod('RunAria2AttemptAsync', $binding)
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

function New-RequestArray([string]$Url, [string]$Destination) {
    $request = [Activator]::CreateInstance($requestType)
    $requestType.GetProperty('Url').SetValue($request, $Url)
    $requestType.GetProperty('DestinationPath').SetValue($request, $Destination)
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
    $aria2Path = Join-Path $launcherDirectory 'tools/aria2/aria2c.exe'
    if (-not (Test-Path -LiteralPath $aria2Path -PathType Leaf)) {
        $aria2Path = Join-Path (Split-Path -Parent $PSScriptRoot) 'launcher_ex/tools/aria2/aria2c.exe'
    }
    Assert-True (Test-Path -LiteralPath $aria2Path -PathType Leaf) 'bundled aria2c is available for lifecycle smoke'

    $service = $constructor.Invoke([object[]]@([string]$root, [TimeSpan]::FromSeconds(5)))
    $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    $listener.Start()
    $port = ([System.Net.IPEndPoint]$listener.LocalEndpoint).Port
    $destination = Join-Path $root 'aria2-smoke.bin'
    $payload = [System.Text.Encoding]::UTF8.GetBytes('r1delta aria2 cleanup regression payload')
    $requests = New-RequestArray "http://127.0.0.1:$port/payload.bin" $destination
    $acceptTask = $listener.AcceptTcpClientAsync()
    $attemptTask = $ariaAttempt.Invoke(
        $service,
        [object[]]@([string]$aria2Path, [object]$requests, 1, [System.Threading.CancellationToken]::None))

    Assert-True $acceptTask.Wait(10000) 'aria2 connects to the local download endpoint'
    $client = $acceptTask.GetAwaiter().GetResult()
    $client.ReceiveTimeout = 5000
    $client.SendTimeout = 5000
    $stream = $client.GetStream()
    $headers = Read-HttpRequestHeaders $stream
    Assert-True ($headers -like 'GET /payload.bin HTTP/*') 'aria2 requests the expected local payload'
    $responseHeaders = [System.Text.Encoding]::ASCII.GetBytes(
        "HTTP/1.1 200 OK`r`nContent-Length: $($payload.Length)`r`nConnection: close`r`n`r`n")
    $stream.Write($responseHeaders, 0, $responseHeaders.Length)
    $stream.Write($payload, 0, $payload.Length)
    $stream.Flush()
    $client.Dispose()
    $client = $null

    Assert-True $attemptTask.Wait(20000) 'aria2 attempt completes including RPC shutdown and cleanup'
    $attemptResult = $attemptTask.GetAwaiter().GetResult()
    $failures = $attemptResult.GetType().GetProperty('Failures', $binding).GetValue($attemptResult)
    Assert-True ($failures.Count -eq 0) 'successful aria2 transfer has no attributed failure'
    Assert-True ([System.Linq.Enumerable]::SequenceEqual([byte[]](Get-Content -LiteralPath $destination -AsByteStream), $payload)) 'aria2 writes the expected payload'
    Assert-True ($null -eq $activeProcessField.GetValue($service)) 'aria2 cleanup releases active process ownership'

    ([IDisposable]$service).Dispose()
    $service = $null
    $listener.Stop()
    $listener = $null

    $service = $constructor.Invoke([object[]]@([string]$root, [TimeSpan]::FromSeconds(1)))
    $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    $listener.Start()
    $stallPort = ([System.Net.IPEndPoint]$listener.LocalEndpoint).Port
    $stallDestination = Join-Path $root 'aria2-stall.bin'
    $stallRequests = New-RequestArray "http://127.0.0.1:$stallPort/stall.bin" $stallDestination
    $stallAcceptTask = $listener.AcceptTcpClientAsync()
    $stallAttemptTask = $ariaAttempt.Invoke(
        $service,
        [object[]]@([string]$aria2Path, [object]$stallRequests, 1, [System.Threading.CancellationToken]::None))

    Assert-True $stallAcceptTask.Wait(10000) 'aria2 connects to the stalled download endpoint'
    $client = $stallAcceptTask.GetAwaiter().GetResult()
    $client.ReceiveTimeout = 5000
    $stallHeaders = Read-HttpRequestHeaders $client.GetStream()
    Assert-True ($stallHeaders -like 'GET /stall.bin HTTP/*') 'stalled endpoint receives the aria2 request'
    Assert-True $stallAttemptTask.Wait(20000) 'aria2 no-progress attempt reaches its injected idle bound'
    $stallResult = $stallAttemptTask.GetAwaiter().GetResult()
    $stallFailures = $stallResult.GetType().GetProperty('Failures', $binding).GetValue($stallResult)
    $retryAllowedProperty = $stallResult.GetType().GetProperty('RetryAllowed', $binding)
    Assert-True ($stallFailures.Count -eq 1) 'aria2 no-progress attempt attributes the stalled request'
    Assert-True ($null -ne $retryAllowedProperty) 'aria2 attempt result exposes retry policy'
    Assert-True (-not [bool]$retryAllowedProperty.GetValue($stallResult)) 'aria2 no-progress attempt immediately enables fallback'
    Assert-True ($null -eq $activeProcessField.GetValue($service)) 'stalled aria2 cleanup releases active process ownership'

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
