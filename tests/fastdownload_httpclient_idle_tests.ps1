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
$attemptMethod = $serviceType.GetMethod('RunHttpClientAttemptAsync', $binding)

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "FAILED: $Message"
    }
    Write-Host "PASS: $Message"
}

function New-RequestArray([string]$Url, [string]$Destination) {
    $request = [Activator]::CreateInstance($requestType)
    $requestType.GetProperty('Url').SetValue($request, $Url)
    $requestType.GetProperty('DestinationPath').SetValue($request, $Destination)
    $requests = [Array]::CreateInstance($requestType, 1)
    $requests.SetValue($request, 0)
    return ,$requests
}

function Get-FailureMessage($Result) {
    $failures = $Result.GetType().GetProperty('Failures', $binding).GetValue($Result)
    foreach ($message in $failures.Values) {
        return [string]$message
    }
    return $null
}

function Invoke-StallAttempt(
    [string]$Destination,
    [scriptblock]$Respond,
    [System.Threading.CancellationTokenSource]$CancellationSource,
    [int]$CancelAfterResponseMilliseconds = -1
) {
    $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    $client = $null
    try {
        $listener.Start()
        $port = ([System.Net.IPEndPoint]$listener.LocalEndpoint).Port
        $requests = New-RequestArray "http://127.0.0.1:$port/file" $Destination
        $acceptTask = $listener.AcceptTcpClientAsync()
        $arguments = [object[]]@([object]$requests, $CancellationSource.Token)
        $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
        $attemptTask = $attemptMethod.Invoke($script:service, $arguments)
        $client = $acceptTask.GetAwaiter().GetResult()
        if ($null -ne $Respond) {
            & $Respond $client.GetStream()
        }

        if ($CancelAfterResponseMilliseconds -ge 0) {
            $CancellationSource.CancelAfter($CancelAfterResponseMilliseconds)
        }
        try {
            $result = $attemptTask.GetAwaiter().GetResult()
            return [pscustomobject]@{
                Result = $result
                Exception = $null
                Elapsed = $stopwatch.Elapsed
            }
        }
        catch {
            $exception = $_.Exception
            while ($exception -isnot [OperationCanceledException] -and $null -ne $exception.InnerException) {
                $exception = $exception.InnerException
            }
            return [pscustomobject]@{
                Result = $null
                Exception = $exception
                Elapsed = $stopwatch.Elapsed
            }
        }
    }
    finally {
        if ($null -ne $client) {
            try {
                $client.Dispose()
            }
            catch {
            }
        }
        try {
            $listener.Stop()
        }
        catch {
        }
    }
}

$root = Join-Path ([System.IO.Path]::GetTempPath()) ('r1delta-http-idle-smoke-' + [Guid]::NewGuid().ToString('N'))
$service = $null
try {
    New-Item -ItemType Directory -Path $root | Out-Null
    $service = $constructor.Invoke([object[]]@([string]$root, [TimeSpan]::FromMilliseconds(500)))

    $headerCts = [System.Threading.CancellationTokenSource]::new()
    $header = Invoke-StallAttempt (Join-Path $root 'header.bin') $null $headerCts
    $headerMessage = Get-FailureMessage $header.Result
    Assert-True ($null -eq $header.Exception) 'header idle timeout is aggregated as a backend failure'
    Assert-True ($headerMessage -like '*no response headers received*') 'header idle timeout reports the stalled phase'
    Assert-True ($header.Elapsed.TotalSeconds -lt 3) 'header idle timeout fires at the injected bound'

    $headersOnly = {
        param($stream)
        $bytes = [System.Text.Encoding]::ASCII.GetBytes("HTTP/1.1 200 OK`r`nContent-Length: 4`r`nConnection: close`r`n`r`n")
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush()
    }
    $bodyDestination = Join-Path $root 'body.bin'
    $bodyCts = [System.Threading.CancellationTokenSource]::new()
    $body = Invoke-StallAttempt $bodyDestination $headersOnly $bodyCts
    $bodyMessage = Get-FailureMessage $body.Result
    Assert-True ($null -eq $body.Exception) 'body idle timeout is aggregated as a backend failure'
    Assert-True ($bodyMessage -like '*no response body bytes received*') 'body idle timeout reports the stalled phase'
    Assert-True ($body.Elapsed.TotalSeconds -lt 3) 'body idle timeout fires at the injected bound'
    Assert-True (Test-Path -LiteralPath ($bodyDestination + '.part')) 'body idle timeout retains the partial file'
    Assert-True (-not (Test-Path -LiteralPath $bodyDestination)) 'body idle timeout does not promote a partial file'

    $cancelDestination = Join-Path $root 'cancel.bin'
    $cancelCts = [System.Threading.CancellationTokenSource]::new()
    $cancelled = Invoke-StallAttempt $cancelDestination $headersOnly $cancelCts 100
    Assert-True ($cancelled.Exception -is [OperationCanceledException]) 'external cancellation propagates instead of becoming an idle failure'
    Assert-True ($cancelled.Elapsed.TotalSeconds -lt 3) 'external cancellation is prompt'
    Assert-True (Test-Path -LiteralPath ($cancelDestination + '.part')) 'external cancellation retains the partial file'
    Assert-True (-not (Test-Path -LiteralPath $cancelDestination)) 'external cancellation does not promote a partial file'

    Write-Host 'All HttpClient idle watchdog smoke tests passed.'
}
finally {
    if ($null -ne $service) {
        ([IDisposable]$service).Dispose()
    }
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}
