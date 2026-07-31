[CmdletBinding()]
param(
    [ValidateSet('Legacy', 'R1O', 'Both')]
    [string]$Mode = 'Both',
    [string]$Map = 'mp_box',
    [int]$Port = 37515,
    [ValidateRange(1, 20)]
    [int]$Repeats = 3,
    [ValidateRange(0, 300)]
    [int]$WarmupSeconds = 15,
    [ValidateRange(5, 3600)]
    [int]$SampleSeconds = 30,
    [ValidateRange(5, 300)]
    [int]$StartupTimeoutSeconds = 75,
    [string]$GameRoot = 'S:\game',
    [string]$ClientRoot = 'C:\whatever',
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Get-Median([double[]]$Values) {
    if (!$Values -or $Values.Count -eq 0) {
        return 0.0
    }

    $sorted = @($Values | Sort-Object)
    $middle = [int][Math]::Floor($sorted.Count / 2)
    if (($sorted.Count % 2) -eq 1) {
        return [double]$sorted[$middle]
    }
    return ([double]$sorted[$middle - 1] + [double]$sorted[$middle]) / 2.0
}

function Test-ListeningPort([int]$LocalPort) {
    return [bool](Get-NetTCPConnection -State Listen -LocalPort $LocalPort -ErrorAction SilentlyContinue | Select-Object -First 1)
}

function Stop-BenchmarkProcess([System.Diagnostics.Process]$Process) {
    if (!$Process -or $Process.HasExited) {
        return
    }

    & taskkill.exe /PID $Process.Id /T /F *> $null
    $Process.WaitForExit(5000) | Out-Null
}

function Invoke-DedicatedBenchmark([string]$Runtime, [int]$Run, [int]$RunPort, [string]$RunRoot) {
    $executable = Join-Path $GameRoot 'R1Delta_DS.exe'
    if (!(Test-Path -LiteralPath $executable -PathType Leaf)) {
        throw "Dedicated server executable not found: $executable"
    }

    $arguments = @(
        '-console', '-dev', '-novid', '-nomessagebox',
        '-port', [string]$RunPort,
        '+hostport', [string]$RunPort,
        '+sv_hibernate_when_empty', '0',
        '+developer', '0',
        '+map', $Map
    )
    if ($Runtime -eq 'R1O') {
        $arguments += '-r1o_dedi'
    }

    $stdoutPath = Join-Path $RunRoot "$($Runtime.ToLowerInvariant())_$Run.stdout.log"
    $stderrPath = Join-Path $RunRoot "$($Runtime.ToLowerInvariant())_$Run.stderr.log"
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process -FilePath $executable -ArgumentList $arguments -WorkingDirectory $GameRoot -WindowStyle Hidden -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath -PassThru

    try {
        $listenMilliseconds = $null
        while ($timer.Elapsed.TotalSeconds -lt $StartupTimeoutSeconds) {
            $process.Refresh()
            if ($process.HasExited) {
                break
            }
            if (Test-ListeningPort $RunPort) {
                $listenMilliseconds = [Math]::Round($timer.Elapsed.TotalMilliseconds, 1)
                break
            }
            Start-Sleep -Milliseconds 100
        }

        if ($null -eq $listenMilliseconds) {
            $exitCode = if ($process.HasExited) { $process.ExitCode } else { $null }
            return [pscustomobject]@{
                runtime = $Runtime
                run = $Run
                port = $RunPort
                status = if ($process.HasExited) { 'exited-before-listen' } else { 'listen-timeout' }
                exitCode = $exitCode
                startupToListenMs = $null
                samples = 0
                stdout = $stdoutPath
                stderr = $stderrPath
            }
        }

        Start-Sleep -Seconds $WarmupSeconds
        $process.Refresh()
        if ($process.HasExited) {
            return [pscustomobject]@{
                runtime = $Runtime
                run = $Run
                port = $RunPort
                status = 'exited-during-warmup'
                exitCode = $process.ExitCode
                startupToListenMs = $listenMilliseconds
                samples = 0
                stdout = $stdoutPath
                stderr = $stderrPath
            }
        }

        $moduleNames = @()
        try {
            $moduleNames = @($process.Modules | ForEach-Object ModuleName | Sort-Object -Unique)
        }
        catch {
            $moduleNames = @("<unavailable: $($_.Exception.Message)>")
        }

        $samples = [System.Collections.Generic.List[object]]::new()
        $previousCpu = $process.TotalProcessorTime.TotalSeconds
        $previousSample = [System.Diagnostics.Stopwatch]::StartNew()
        for ($second = 1; $second -le $SampleSeconds; ++$second) {
            Start-Sleep -Seconds 1
            $process.Refresh()
            if ($process.HasExited) {
                break
            }

            $elapsed = $previousSample.Elapsed.TotalSeconds
            $currentCpu = $process.TotalProcessorTime.TotalSeconds
            $cpuOneCore = 100.0 * ($currentCpu - $previousCpu) / $elapsed
            $previousCpu = $currentCpu
            $previousSample.Restart()

            $samples.Add([pscustomobject]@{
                second = $second
                cpuOneCorePercent = $cpuOneCore
                cpuHostPercent = $cpuOneCore / [Environment]::ProcessorCount
                workingSetBytes = $process.WorkingSet64
                privateBytes = $process.PrivateMemorySize64
                threadCount = $process.Threads.Count
                handleCount = $process.HandleCount
            })
        }

        $cpuOneCore = [double[]]@($samples | ForEach-Object cpuOneCorePercent)
        $cpuHost = [double[]]@($samples | ForEach-Object cpuHostPercent)
        $workingSet = [double[]]@($samples | ForEach-Object workingSetBytes)
        $privateBytes = [double[]]@($samples | ForEach-Object privateBytes)
        $threadCount = [double[]]@($samples | ForEach-Object threadCount)
        $handleCount = [double[]]@($samples | ForEach-Object handleCount)

        return [pscustomobject]@{
            runtime = $Runtime
            run = $Run
            port = $RunPort
            status = if ($samples.Count -eq $SampleSeconds) { 'ok' } else { 'exited-during-sampling' }
            exitCode = if ($process.HasExited) { $process.ExitCode } else { $null }
            startupToListenMs = $listenMilliseconds
            samples = $samples.Count
            cpuOneCoreMeanPercent = if ($cpuOneCore.Count) { ($cpuOneCore | Measure-Object -Average).Average } else { 0.0 }
            cpuOneCoreMedianPercent = Get-Median $cpuOneCore
            cpuHostMeanPercent = if ($cpuHost.Count) { ($cpuHost | Measure-Object -Average).Average } else { 0.0 }
            workingSetMeanBytes = if ($workingSet.Count) { ($workingSet | Measure-Object -Average).Average } else { 0.0 }
            workingSetPeakBytes = if ($workingSet.Count) { ($workingSet | Measure-Object -Maximum).Maximum } else { 0.0 }
            privateMeanBytes = if ($privateBytes.Count) { ($privateBytes | Measure-Object -Average).Average } else { 0.0 }
            privatePeakBytes = if ($privateBytes.Count) { ($privateBytes | Measure-Object -Maximum).Maximum } else { 0.0 }
            threadMedian = Get-Median $threadCount
            handleMedian = Get-Median $handleCount
            moduleCount = $moduleNames.Count
            modules = $moduleNames
            stdout = $stdoutPath
            stderr = $stderrPath
        }
    }
    finally {
        Stop-BenchmarkProcess $process
    }
}

$existing = @(Get-Process -Name 'R1Delta_DS' -ErrorAction SilentlyContinue)
if ($existing.Count -ne 0) {
    throw "Refusing to benchmark while R1Delta_DS is already running (PID(s): $($existing.Id -join ', '))."
}

if (!$OutputPath) {
    $timestamp = Get-Date -Format 'yyyyMMdd_HHmmss'
    $OutputPath = Join-Path $PSScriptRoot "..\.build\dedi_benchmark_$timestamp.json"
}
$OutputPath = [System.IO.Path]::GetFullPath($OutputPath)
$runRoot = Join-Path ([System.IO.Path]::GetDirectoryName($OutputPath)) ([System.IO.Path]::GetFileNameWithoutExtension($OutputPath))
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null

$originalPath = $env:PATH
$originalLauncher = $env:FROM_DELTA_LAUNCHER
try {
    $env:FROM_DELTA_LAUNCHER = '1'
    $env:PATH = "$GameRoot\r1delta\bin_delta;$GameRoot\r1delta\bin;$ClientRoot\bin\x64_retail;$ClientRoot\r1\bin\x64_retail;$originalPath"

    $runtimes = switch ($Mode) {
        'Both' { @('Legacy', 'R1O') }
        default { @($Mode) }
    }

    $results = [System.Collections.Generic.List[object]]::new()
    $runPort = $Port
    foreach ($runtime in $runtimes) {
        for ($run = 1; $run -le $Repeats; ++$run) {
            Write-Host "Benchmarking $runtime run $run/$Repeats on port $runPort..."
            $result = Invoke-DedicatedBenchmark $runtime $run $runPort $runRoot
            $results.Add($result)
            $result | Select-Object runtime, run, status, startupToListenMs, cpuOneCoreMeanPercent, workingSetMeanBytes, privateMeanBytes, threadMedian, handleMedian, moduleCount | Format-Table -AutoSize
            ++$runPort
        }
    }

    $document = [ordered]@{
        generatedAt = (Get-Date).ToString('o')
        machine = [ordered]@{
            computerName = $env:COMPUTERNAME
            processorCount = [Environment]::ProcessorCount
            osVersion = [Environment]::OSVersion.VersionString
        }
        parameters = [ordered]@{
            mode = $Mode
            map = $Map
            repeats = $Repeats
            warmupSeconds = $WarmupSeconds
            sampleSeconds = $SampleSeconds
            startupTimeoutSeconds = $StartupTimeoutSeconds
            basePort = $Port
            gameRoot = $GameRoot
            clientRoot = $ClientRoot
        }
        results = $results
    }
    $document | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $OutputPath -Encoding utf8
    Write-Host "Wrote $OutputPath"
}
finally {
    $env:PATH = $originalPath
    $env:FROM_DELTA_LAUNCHER = $originalLauncher
}
