[CmdletBinding()]
param(
    [string]$GameRoot = 'S:\game',
    [string]$ClientRoot = 'C:\whatever',
    [string]$CacheDirectory,
    [ValidateRange(1024, 65000)]
    [int]$BasePort = 37641,
    [ValidateRange(5, 120)]
    [int]$StartupTimeoutSeconds = 30
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $CacheDirectory) {
    $CacheDirectory = Join-Path $repoRoot '.build\r1o_vpk_cache_integration'
}
$cachePath = [System.IO.Path]::GetFullPath($CacheDirectory)
$serverPath = Join-Path $GameRoot 'R1Delta_DS.exe'
if (-not (Test-Path -LiteralPath $serverPath -PathType Leaf)) {
    throw "Dedicated server executable not found: $serverPath"
}

$originalPath = $env:PATH
$originalLauncher = $env:FROM_DELTA_LAUNCHER
$originalCache = $env:R1DELTA_VPK_CACHE_DIR

function Stop-TestServer {
    Get-Process -Name R1Delta_DS -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
}

function Invoke-CacheRun {
    param(
        [Parameter(Mandatory)] [string]$Label,
        [Parameter(Mandatory)] [int]$Port
    )

    $stdout = Join-Path $repoRoot ".build\r1o_cache_${Label}_stdout.log"
    $stderr = Join-Path $repoRoot ".build\r1o_cache_${Label}_stderr.log"
    Remove-Item -LiteralPath $stdout, $stderr -Force -ErrorAction SilentlyContinue

    $arguments = @(
        '-console', '-dev', '-novid',
        '-port', [string]$Port,
        '+hostport', [string]$Port,
        '+sv_hibernate_when_empty', '0',
        '+developer', '1',
        '+playlist', 'ps',
        '+map', 'mp_box',
        '-r1o_dedi'
    )

    $startParameters = @{
        FilePath = $serverPath
        ArgumentList = $arguments
        WorkingDirectory = $GameRoot
        PassThru = $true
        WindowStyle = 'Hidden'
        RedirectStandardOutput = $stdout
        RedirectStandardError = $stderr
    }
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process @startParameters

    $status = $null
    $vpkReadyMs = $null
    $listenMs = $null
    try {
        while ($stopwatch.Elapsed.TotalSeconds -lt $StartupTimeoutSeconds -and -not $process.HasExited) {
            if ($null -eq $status -and (Test-Path -LiteralPath $stdout)) {
                $match = Select-String -LiteralPath $stdout -Pattern '^\[R1O dedicated\] VPK ready:.*source=(scan|cache)$' |
                    Select-Object -Last 1
                if ($match) {
                    $status = $match.Line
                    $vpkReadyMs = $stopwatch.Elapsed.TotalMilliseconds
                }
            }

            if (Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue) {
                $listenMs = $stopwatch.Elapsed.TotalMilliseconds
                break
            }
            Start-Sleep -Milliseconds 40
        }

        if ($process.HasExited -or $null -eq $status -or $null -eq $listenMs) {
            $stderrTail = if (Test-Path -LiteralPath $stderr) {
                (Get-Content -LiteralPath $stderr -Tail 20) -join [Environment]::NewLine
            }
            throw "$Label launch failed (exit=$($process.HasExited), status=$status, listenMs=$listenMs). $stderrTail"
        }

        if ($status -notmatch 'source=(scan|cache)$') {
            throw "$Label emitted an invalid VPK status line: $status"
        }

        [pscustomobject]@{
            label = $Label
            source = $Matches[1]
            vpkReadyMs = [Math]::Round($vpkReadyMs, 1)
            listenMs = [Math]::Round($listenMs, 1)
            status = $status
        }
    }
    finally {
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        }
        Start-Sleep -Milliseconds 250
    }
}

try {
    Stop-TestServer
    Remove-Item -LiteralPath $cachePath -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Path $cachePath -Force | Out-Null

    $env:R1DELTA_VPK_CACHE_DIR = $cachePath
    $env:FROM_DELTA_LAUNCHER = '1'
    $env:PATH = "$GameRoot\r1delta\bin_delta;$GameRoot\r1delta\bin;$ClientRoot\bin\x64_retail;$ClientRoot\r1\bin\x64_retail;$originalPath"

    $cold = Invoke-CacheRun -Label 'cold' -Port $BasePort
    $hot = Invoke-CacheRun -Label 'hot' -Port ($BasePort + 1)

    $cacheFiles = @(Get-ChildItem -LiteralPath $cachePath -Filter '*.bin' -File)
    if ($cacheFiles.Count -ne 1) {
        throw "Expected one cache file, found $($cacheFiles.Count) in $cachePath"
    }
    $cacheFile = $cacheFiles[0]
    $originalLength = $cacheFile.Length
    [System.IO.File]::WriteAllBytes($cacheFile.FullName, [byte[]](1..16))

    $recovered = Invoke-CacheRun -Label 'corrupt_recovery' -Port ($BasePort + 2)
    $rewrittenLength = (Get-Item -LiteralPath $cacheFile.FullName).Length
    $rehit = Invoke-CacheRun -Label 'rehit' -Port ($BasePort + 3)

    $results = @($cold, $hot, $recovered, $rehit)
    $expectedSources = @('scan', 'cache', 'scan', 'cache')
    for ($index = 0; $index -lt $results.Count; ++$index) {
        if ($results[$index].source -ne $expectedSources[$index]) {
            throw "Unexpected source for $($results[$index].label): $($results[$index].source)"
        }
    }
    if ($originalLength -le 16 -or $rewrittenLength -ne $originalLength) {
        throw "Cache was not atomically rebuilt after corruption: original=$originalLength rewritten=$rewrittenLength"
    }

    $results | Format-Table -AutoSize
    "CACHE_FILE=$($cacheFile.FullName) CACHE_BYTES=$rewrittenLength"
}
finally {
    Stop-TestServer
    $env:PATH = $originalPath
    $env:FROM_DELTA_LAUNCHER = $originalLauncher
    $env:R1DELTA_VPK_CACHE_DIR = $originalCache
}
