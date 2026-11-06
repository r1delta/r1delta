param(
    [Parameter(Mandatory = $true)]
    [string]$LauncherPath
)

# Drives TitanfallManager.DownloadAllFilesWithResume against a loopback HTTP server with a three-file
# manifest and records every status/progress report. The installer used to sit on "99.0%" while it hashed
# the downloaded files; these checks prove each phase now reports real, moving numbers and names its file.

$ErrorActionPreference = 'Stop'
$binding = [System.Reflection.BindingFlags]'Public,NonPublic,Static,Instance'
$launcherFull = (Resolve-Path -LiteralPath $LauncherPath).Path
$assembly = [System.Reflection.Assembly]::LoadFrom($launcherFull)
$manager = $assembly.GetType('R1Delta.TitanfallManager', $true)
$fileListType = $assembly.GetType('launcher_ex.TitanfallFileList', $true)
$download = $manager.GetMethod('DownloadAllFilesWithResume', $binding)
$ensureOwnership = $manager.GetMethod('TryEnsureManagedInstallOwnership', $binding)

# The manifest hash is XXH64 from the launcher's own dependency; load it from the NuGet cache when it is
# embedded (Costura) and therefore not resolvable by name from a PowerShell host.
$xxhash = [AppDomain]::CurrentDomain.GetAssemblies() | Where-Object { $_.GetName().Name -eq 'K4os.Hash.xxHash' } | Select-Object -First 1
if (-not $xxhash) {
    $candidates = @(
        (Join-Path (Split-Path -Parent $launcherFull) 'K4os.Hash.xxHash.dll'),
        (Join-Path $PSScriptRoot '..\packages\K4os.Hash.xxHash.1.0.8\lib\net462\K4os.Hash.xxHash.dll'))
    $path = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if (-not $path) { throw 'K4os.Hash.xxHash.dll was not found next to the launcher or in packages.' }
    $xxhash = [System.Reflection.Assembly]::LoadFrom((Resolve-Path -LiteralPath $path).Path)
}
$xxh64 = $xxhash.GetType('K4os.Hash.xxHash.XXH64', $true)
$digestOf = $xxh64.GetMethod('DigestOf', [Type[]]@([byte[]], [int], [int]))

# The launcher is a .NET Framework assembly whose interfaces name mscorlib, which PowerShell 7's compiler cannot
# resolve. Compile the recording progress sink with the framework's own csc.exe and load the result.
$recorderSource = @'
using System;
using System.Collections.Generic;
using R1Delta;

public sealed class RecordingProgress : IInstallProgress, IInstallProgressStatus
{
    public sealed class Entry
    {
        public InstallProgressPhase Phase;
        public string Detail;
        public long Bytes;
        public long Total;
        public double Rate;
    }

    public readonly List<Entry> Entries = new List<Entry>();
    public readonly List<string> Errors = new List<string>();
    private InstallProgressStatus _status = new InstallProgressStatus();
    public Action OnCancelRequested { get; set; }

    public void ReportStatus(InstallProgressStatus status) { lock (Entries) _status = status; }

    public void ReportProgress(long bytesDownloaded, long totalBytes, double bytesPerSecond)
    {
        lock (Entries)
            Entries.Add(new Entry
            {
                Phase = _status.Phase,
                Detail = _status.Detail,
                Bytes = bytesDownloaded,
                Total = totalBytes,
                Rate = bytesPerSecond
            });
    }

    public void ShowError(string message) { lock (Entries) Errors.Add(message); }
    public void Dispose() { }
}
'@
$csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if (-not (Test-Path -LiteralPath $csc)) { throw "csc.exe was not found at $csc" }
$compileDir = Join-Path ([System.IO.Path]::GetTempPath()) ('r1delta-progress-sink-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $compileDir | Out-Null
$sourcePath = Join-Path $compileDir 'RecordingProgress.cs'
$sinkPath = Join-Path $compileDir 'RecordingProgress.dll'
Set-Content -LiteralPath $sourcePath -Value $recorderSource -Encoding UTF8
& $csc /nologo /target:library "/out:$sinkPath" "/reference:$launcherFull" $sourcePath
if ($LASTEXITCODE -ne 0) { throw "csc.exe failed with exit code $LASTEXITCODE." }
[System.Reflection.Assembly]::LoadFrom($sinkPath) | Out-Null

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "FAILED: $Message"
    }
    Write-Host "PASS: $Message"
}

$root = Join-Path ([System.IO.Path]::GetTempPath()) ('r1delta-progress-' + [Guid]::NewGuid().ToString('N'))
$listener = $null
$savedList = $null
$list = $fileListType.GetField('s_fileList', $binding).GetValue($null)
try {
    New-Item -ItemType Directory -Path $root | Out-Null
    $installDir = Join-Path $root 'install'
    New-Item -ItemType Directory -Path $installDir | Out-Null

    # Three payloads: sizes are not multiples of the 4 MiB hashing block so the last read is partial.
    $payloads = @{}
    $random = [System.Random]::new(1234)
    foreach ($spec in @(@('data/alpha.bin', 9437184), @('data/beta.bin', 6291456 + 777), @('data/gamma.bin', 4194304))) {
        $bytes = New-Object byte[] $spec[1]
        $random.NextBytes($bytes)
        $payloads[$spec[0]] = $bytes
    }

    $port = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    $port.Start()
    $portNumber = ([System.Net.IPEndPoint]$port.LocalEndpoint).Port
    $port.Stop()
    $listener = [System.Net.HttpListener]::new()
    $listener.Prefixes.Add("http://127.0.0.1:$portNumber/")
    $listener.Start()
    $serverJob = Start-ThreadJob -ScriptBlock {
        param($listener, $payloads)
        while ($listener.IsListening) {
            try { $context = $listener.GetContext() } catch { break }
            $key = $context.Request.Url.AbsolutePath.TrimStart('/')
            $body = $payloads[$key]
            if ($null -eq $body) {
                $context.Response.StatusCode = 404
            }
            else {
                $context.Response.ContentLength64 = $body.Length
                $context.Response.OutputStream.Write($body, 0, $body.Length)
            }
            $context.Response.Close()
        }
    } -ArgumentList $listener, $payloads

    # Replace the real 12 GB manifest by the three loopback files for the duration of the test.
    $savedList = $list.ToArray()
    $list.Clear()
    foreach ($relative in $payloads.Keys) {
        $bytes = [byte[]]$payloads[$relative]
        $digestArgs = New-Object object[] 3
        $digestArgs[0] = $bytes
        $digestArgs[1] = 0
        $digestArgs[2] = $bytes.Length
        $hash = [uint64]$digestOf.Invoke($null, $digestArgs)
        $tuple = [System.ValueTuple[string, string, uint64, long]]::new(
            "http://127.0.0.1:$portNumber/$relative", $relative, $hash, [long]$bytes.Length)
        $list.Add($tuple)
    }
    $totalBytes = ($payloads.Values | ForEach-Object { $_.Length } | Measure-Object -Sum).Sum

    $ensureArgs = [object[]]@([string]$installDir, $null)
    Assert-True ([bool]$ensureOwnership.Invoke($null, $ensureArgs)) 'managed ownership is established for the scratch install'

    # --- Fresh install: download, then verification ---
    $recorder = [RecordingProgress]::new()
    $ok = [bool]$download.Invoke($null, [object[]]@([string]$installDir, $recorder, [System.Threading.CancellationToken]::None)).GetAwaiter().GetResult()
    Assert-True $ok 'fresh install through the loopback server completes'
    Assert-True ($recorder.Errors.Count -eq 0) 'fresh install reports no errors'
    foreach ($relative in $payloads.Keys) {
        $onDisk = [System.IO.File]::ReadAllBytes((Join-Path $installDir $relative))
        Assert-True ([System.Linq.Enumerable]::SequenceEqual[byte]($onDisk, $payloads[$relative])) "downloaded $relative matches its payload"
    }

    $entries = @($recorder.Entries.ToArray())
    $verification = @($entries | Where-Object { $_.Phase -eq 'Verification' })
    Assert-True ($verification.Count -ge 4) "verification reports progress repeatedly ($($verification.Count) reports)"
    Assert-True ($verification[0].Bytes -eq 0 -and $verification[0].Total -eq $totalBytes) 'verification starts at 0 of the full download size, not at 99%'
    Assert-True ($verification[-1].Bytes -eq $totalBytes) 'verification ends with every downloaded byte checked'
    $previous = -1
    $monotonic = $true
    foreach ($entry in $verification) {
        if ($entry.Bytes -lt $previous) { $monotonic = $false }
        $previous = $entry.Bytes
    }
    Assert-True $monotonic 'verification progress never moves backwards'
    Assert-True (@($verification | Where-Object { $_.Bytes -gt 0 -and $_.Bytes -lt $totalBytes }).Count -ge 1) 'verification reports intermediate progress between files'
    $named = @($verification | Where-Object { $_.Detail -match '^File [1-3] of 3: (alpha|beta|gamma)\.bin$' })
    Assert-True ($named.Count -ge 3) 'verification names the file being checked'

    $download99 = @($entries | Where-Object { $_.Phase -in @('Resume', 'Download', 'Fallback') })
    Assert-True ($download99.Count -ge 1) 'download phase reported progress'
    $highest = ($download99 | Measure-Object -Property Bytes -Maximum).Maximum
    Assert-True ($highest -le ($totalBytes - [math]::Ceiling($totalBytes / 100.0))) 'download progress stays below 100% until verification finishes'
    Assert-True (@($download99 | Where-Object { $_.Detail -match '^\d+ of 3 files complete, .* left to download$' }).Count -ge 1) 'download phase says how many files and bytes are left'
    Assert-True ($entries[-1].Phase -eq 'Complete' -and $entries[-1].Bytes -eq $totalBytes) 'the final report is Complete at 100%'

    # --- Existing install: the preflight hash pass reports progress ---
    $recorder = [RecordingProgress]::new()
    $ok = [bool]$download.Invoke($null, [object[]]@([string]$installDir, $recorder, [System.Threading.CancellationToken]::None)).GetAwaiter().GetResult()
    Assert-True $ok 'a second run over a complete install succeeds'
    $entries = @($recorder.Entries.ToArray())
    $preflight = @($entries | Where-Object { $_.Phase -eq 'Preflight' -and $_.Total -eq $totalBytes })
    Assert-True ($preflight.Count -ge 3) "preflight reports progress while hashing existing files ($($preflight.Count) reports)"
    Assert-True ($preflight[-1].Bytes -eq $totalBytes) 'preflight ends with every existing byte checked'
    Assert-True (@($preflight | Where-Object { $_.Detail -match '^File [1-3] of 3: data/' }).Count -ge 3) 'preflight names the file being checked'
    Assert-True (@($entries | Where-Object { $_.Phase -in @('Download', 'Fallback', 'Verification') }).Count -eq 0) 'a complete install is not downloaded or re-verified'

    Write-Host 'All installer progress tests passed.'
}
finally {
    if ($null -ne $savedList) {
        $list.Clear()
        foreach ($entry in $savedList) { $list.Add($entry) }
    }
    if ($null -ne $listener) { $listener.Stop(); $listener.Close() }
    if ($null -ne $serverJob) { Remove-Job -Job $serverJob -Force -ErrorAction SilentlyContinue }
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $compileDir -Recurse -Force -ErrorAction SilentlyContinue
}
