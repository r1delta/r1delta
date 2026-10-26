[CmdletBinding()]
param(
    [ValidateSet('Client', 'Dedicated', 'Listen')]
    [string] $Mode = 'Client',

    [string] $Map = 'mp_box',
    [int] $Port = 27050,
    [int] $ClientPort = 27051,
    [ValidateRange(0, 86400)]
    [int] $Seconds = 0,
    [string] $LogDirectory = 'C:\Users\r3muxd\tmp\r2port\runlogs',
    [switch] $Cdb,
    [switch] $FatalScriptErrors,
    [switch] $R1OFakeDedicated,
    [string[]] $GameArguments = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Root = 'S:\game\r2'
$CoreRoot = Join-Path $Root 'r1delta'
$LockPath = 'C:\Users\r3muxd\tmp\r2port\GAME.lock'
$CdbPath = 'C:\Program Files\WindowsApps\Microsoft.WinDbg_1.2603.20001.0_x64__8wekyb3d8bbwe\amd64\cdb.exe'
$CdbCommands = 'C:\Users\r3muxd\tmp\r2port\ds-cdb-cmds.txt'
$ClientWorkingDirectory = 'C:\whatever'
$script:LockToken = $null
$script:OwnedLauncherPid = 0
$script:OwnedGamePid = 0
$script:ExpectedLauncherPath = ''
$script:ExpectedGamePath = ''
$script:ExpectedGameProcessPath = ''
$script:TimedOut = $false

function Get-ProcessById([int] $ProcessId) {
    return Get-CimInstance Win32_Process -Filter "ProcessId=$ProcessId" -ErrorAction SilentlyContinue
}

function Get-R2Processes {
    $rootPrefix = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    $physicalPrefix = 'C:\Users\r3muxd\newr1root3\game\r2\'
    return @(Get-CimInstance Win32_Process -ErrorAction Stop | Where-Object {
        if (-not $_.ExecutablePath) { return $false }
        $path = [IO.Path]::GetFullPath($_.ExecutablePath)
        $path.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase) -or
            $path.StartsWith($physicalPrefix, [StringComparison]::OrdinalIgnoreCase)
    })
}

function Assert-R2Idle {
    $running = @(Get-R2Processes)
    if ($running.Count -ne 0) {
        $listing = ($running | ForEach-Object { "pid=$($_.ProcessId) path=$($_.ExecutablePath)" }) -join '; '
        throw "Refusing to start a second game from S:\game\r2: $listing"
    }
}

function Remove-StaleGameLock {
    if (-not (Test-Path -LiteralPath $LockPath -PathType Leaf)) { return }
    $lock = $null
    try {
        $lock = Get-Content -LiteralPath $LockPath -Raw | ConvertFrom-Json
        $started = [DateTimeOffset]::Parse([string] $lock.started)
    } catch {
        throw "Cannot safely interpret GAME.lock at '$LockPath': $($_.Exception.Message)"
    }
    $age = [DateTimeOffset]::UtcNow - $started.ToUniversalTime()
    if ($age.TotalMinutes -le 25) {
        throw "GAME.lock is active (owner '$($lock.owner)', purpose '$($lock.purpose)', started '$($lock.started)')."
    }
    $liveLockPids = @()
    $processIdsProperty = $lock.PSObject.Properties['processIds']
    if ($null -ne $processIdsProperty) {
        foreach ($processId in @($processIdsProperty.Value)) {
            if ($null -ne (Get-ProcessById ([int] $processId))) { $liveLockPids += [int] $processId }
        }
    }
    if ($liveLockPids.Count -ne 0) {
        throw "GAME.lock is older than 25 minutes but recorded process IDs are still alive: $($liveLockPids -join ', ')."
    }
    Assert-R2Idle
    Remove-Item -LiteralPath $LockPath -Force
}

function Write-GameLock([int[]] $ProcessIds, [bool] $Create) {
    $record = [ordered]@{
        owner = "run-in-r2.ps1 pid $PID"
        purpose = "$Mode game from $Root"
        started = [DateTimeOffset]::UtcNow.ToString('o')
        token = $script:LockToken
        processIds = @($ProcessIds | Select-Object -Unique)
    }
    $json = ConvertTo-Json -InputObject $record -Compress
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes($json + [Environment]::NewLine)
    if ($Create) {
        $stream = $null
        try {
            $stream = [IO.File]::Open($LockPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
            $stream.Write($bytes, 0, $bytes.Length)
            $stream.Flush()
        } catch [IO.IOException] {
            throw "Could not atomically acquire GAME.lock at '$LockPath': $($_.Exception.Message)"
        } finally {
            if ($null -ne $stream) { $stream.Dispose() }
        }
    } else {
        $current = Get-Content -LiteralPath $LockPath -Raw | ConvertFrom-Json
        $tokenProperty = $current.PSObject.Properties['token']
        if ($null -eq $tokenProperty -or [string] $tokenProperty.Value -ne $script:LockToken) {
            throw 'GAME.lock ownership changed while this launch was running; refusing to update it.'
        }
        $temporary = "$LockPath.$($script:LockToken).tmp"
        try {
            [IO.File]::WriteAllBytes($temporary, $bytes)
            Move-Item -LiteralPath $temporary -Destination $LockPath -Force
        } finally {
            if (Test-Path -LiteralPath $temporary -PathType Leaf) { Remove-Item -LiteralPath $temporary -Force }
        }
    }
}

function Acquire-GameLock {
    Remove-StaleGameLock
    $script:LockToken = [Guid]::NewGuid().ToString('N')
    $initialPids = @([int] $PID)
    Write-GameLock -ProcessIds $initialPids -Create $true
}

function Release-GameLock {
    if (-not $script:LockToken) { return }
    if (Test-Path -LiteralPath $LockPath -PathType Leaf) {
        try {
            $current = Get-Content -LiteralPath $LockPath -Raw | ConvertFrom-Json
            $tokenProperty = $current.PSObject.Properties['token']
            if ($null -ne $tokenProperty -and [string] $tokenProperty.Value -eq $script:LockToken) {
                Remove-Item -LiteralPath $LockPath -Force
            }
        } finally {
            $script:LockToken = $null
        }
    }
}

function ConvertTo-QuotedArgument([string] $Value) {
    if ($Value.Length -gt 0 -and $Value -notmatch '[\s"]') { return $Value }
    $builder = [Text.StringBuilder]::new()
    [void] $builder.Append('"')
    $slashes = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq [char] '\') {
            $slashes++
            continue
        }
        if ($character -eq [char] '"') {
            [void] $builder.Append(('\' * (2 * $slashes + 1)))
            [void] $builder.Append('"')
            $slashes = 0
            continue
        }
        if ($slashes -gt 0) {
            [void] $builder.Append(('\' * $slashes))
            $slashes = 0
        }
        [void] $builder.Append($character)
    }
    if ($slashes -gt 0) { [void] $builder.Append(('\' * (2 * $slashes))) }
    [void] $builder.Append('"')
    return $builder.ToString()
}

function ConvertTo-ArgumentString([string[]] $Values) {
    return (($Values | ForEach-Object { ConvertTo-QuotedArgument ([string] $_) }) -join ' ')
}

function Get-ProcessRecord([int] $ProcessId) {
    return Get-CimInstance Win32_Process -Filter "ProcessId=$ProcessId" -ErrorAction SilentlyContinue
}

function Test-R2ExecutablePath([string] $Candidate, [string] $Expected) {
    if ([string]::IsNullOrWhiteSpace($Candidate)) { return $false }
    $candidatePath = [IO.Path]::GetFullPath($Candidate)
    $expectedPath = [IO.Path]::GetFullPath($Expected)
    if ($candidatePath.Equals($expectedPath, [StringComparison]::OrdinalIgnoreCase)) { return $true }
    $rootPrefix = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    $physicalPrefix = 'C:\Users\r3muxd\newr1root3\game\r2\'
    $underRoot = $candidatePath.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase) -or
        $candidatePath.StartsWith($physicalPrefix, [StringComparison]::OrdinalIgnoreCase)
    return $underRoot -and [IO.Path]::GetFileName($candidatePath).Equals([IO.Path]::GetFileName($expectedPath), [StringComparison]::OrdinalIgnoreCase)
}

function Find-GameChild([int] $ParentProcessId, [string] $ExpectedPath) {
    return @(Get-CimInstance Win32_Process -Filter "ParentProcessId=$ParentProcessId" -ErrorAction SilentlyContinue | Where-Object {
        $_.ExecutablePath -and (Test-R2ExecutablePath $_.ExecutablePath $ExpectedPath)
    } | Select-Object -First 1)
}

function Stop-OwnedProcess([int] $ProcessId, [string] $ExpectedPath) {
    if ($ProcessId -le 0) { return }
    $record = Get-ProcessRecord $ProcessId
    if ($null -eq $record -or -not $record.ExecutablePath) { return }
    $actualPath = [IO.Path]::GetFullPath($record.ExecutablePath)
    if (-not (Test-R2ExecutablePath $actualPath $ExpectedPath)) { return }
    Stop-Process -Id $ProcessId -Force -ErrorAction SilentlyContinue
}

function Start-R2Game {
    if (-not (Test-Path -LiteralPath (Join-Path $Root 'r1delta.exe') -PathType Leaf)) {
        throw "R2 root is not staged; missing $(Join-Path $Root 'r1delta.exe'). Run .build\r2-root.ps1 -Stage first."
    }
    if (-not (Test-Path -LiteralPath $CoreRoot -PathType Container)) { throw "Missing R2 CORE: $CoreRoot" }
    if ($Mode -eq 'Dedicated' -and (Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue)) {
        throw "UDP port $Port already in use."
    }

    switch ($Mode) {
        'Client' {
            $executable = Join-Path $Root 'Titanfall.exe'
            $workingDirectory = $ClientWorkingDirectory
            $gameArgs = @('-console', '-dev', '-novid', '-windowed', '-noborder')
            if ($ClientPort -gt 0) { $gameArgs += @('+clientport', [string] $ClientPort) }
        }
        'Listen' {
            $executable = Join-Path $Root 'Titanfall.exe'
            $workingDirectory = $ClientWorkingDirectory
            $gameArgs = @('-console', '-dev', '-novid', '-windowed', '-noborder', '+map', $Map)
            if ($ClientPort -gt 0) { $gameArgs += @('+clientport', [string] $ClientPort) }
        }
        'Dedicated' {
            $executable = Join-Path $Root 'R1Delta_DS.exe'
            $workingDirectory = $Root
            $gameArgs = @('-console', '-dev', '-novid', '-port', [string] $Port, '+hostport', [string] $Port, '+map', $Map)
            if ($R1OFakeDedicated) { $gameArgs += '-r1o_dedi' }
        }
    }
    if ($FatalScriptErrors) { $gameArgs += @('+fatal_script_errors', '1') }
    if ($GameArguments.Count -gt 0) { $gameArgs += $GameArguments }
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) { throw "Missing game executable: $executable" }
    if (-not (Test-Path -LiteralPath $workingDirectory -PathType Container)) { throw "Missing working directory: $workingDirectory" }
    $script:ExpectedGamePath = [IO.Path]::GetFullPath($executable)

    $oldPath = $env:PATH
    $oldFromLauncher = $env:FROM_DELTA_LAUNCHER
    $env:FROM_DELTA_LAUNCHER = '1'
    $env:PATH = "$CoreRoot\bin_delta;$CoreRoot\bin;$ClientWorkingDirectory\bin\x64_retail;$ClientWorkingDirectory\r1\bin\x64_retail;$oldPath"
$logPath = ''
    try {
        $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
        New-Item -ItemType Directory -Path $LogDirectory -Force | Out-Null
        $logPath = Join-Path $LogDirectory "r2-$($Mode.ToLowerInvariant())-$Map-$stamp.cdb.log"
        $launcher = $null
        if ($Cdb) {
            if (-not (Test-Path -LiteralPath $CdbPath -PathType Leaf)) { throw "CDB not found: $CdbPath" }
            if (-not (Test-Path -LiteralPath $CdbCommands -PathType Leaf)) { throw "CDB command file not found: $CdbCommands" }
            $debuggerArgs = @('-G', '-cf', $CdbCommands, '-logo', $logPath, $executable) + $gameArgs
            $launcher = Start-Process -FilePath $CdbPath -ArgumentList (ConvertTo-ArgumentString $debuggerArgs) -WorkingDirectory $workingDirectory -WindowStyle Hidden -PassThru
            $script:OwnedLauncherPid = [int] $launcher.Id
            $script:ExpectedLauncherPath = [IO.Path]::GetFullPath($CdbPath)
            $gameProcess = $null
            $startupDeadline = (Get-Date).AddSeconds(60)
            while ((Get-Date) -lt $startupDeadline -and $null -eq $gameProcess) {
                $matches = @(Find-GameChild $script:OwnedLauncherPid $script:ExpectedGamePath)
                if ($matches.Count -gt 0) { $gameProcess = $matches[0] }
                elseif ($null -eq (Get-ProcessRecord $script:OwnedLauncherPid)) { break }
                else { Start-Sleep -Milliseconds 500 }
            }
            if ($null -eq $gameProcess) { throw "CDB did not start the expected game executable under $Root; see $logPath" }
            $script:OwnedGamePid = [int] $gameProcess.ProcessId
            $script:ExpectedGameProcessPath = [IO.Path]::GetFullPath($gameProcess.ExecutablePath)
        } else {
            $launcher = Start-Process -FilePath $script:ExpectedGamePath -ArgumentList (ConvertTo-ArgumentString $gameArgs) -WorkingDirectory $workingDirectory -PassThru
            $script:OwnedLauncherPid = [int] $launcher.Id
            $script:ExpectedLauncherPath = $script:ExpectedGamePath
$gameProcess = Get-ProcessRecord $script:OwnedLauncherPid
            if ($null -eq $gameProcess -or -not $gameProcess.ExecutablePath -or -not (Test-R2ExecutablePath $gameProcess.ExecutablePath $script:ExpectedGamePath)) {
                $observedPath = if ($null -ne $gameProcess) { $gameProcess.ExecutablePath } else { 'not running' }
                throw "Started PID $($script:OwnedLauncherPid), but its executable path '$observedPath' is not the requested executable under $Root."
            }
            $script:OwnedGamePid = $script:OwnedLauncherPid
            $script:ExpectedGameProcessPath = [IO.Path]::GetFullPath($gameProcess.ExecutablePath)
        }

        $ownedPids = @([int] $PID, [int] $script:OwnedLauncherPid, [int] $script:OwnedGamePid)
        Write-GameLock -ProcessIds $ownedPids -Create $false
        Write-Host "Mode: $Mode; map: $Map; game pid: $($script:OwnedGamePid); executable path: $($gameProcess.ExecutablePath)"
        Write-Host "Working directory: $workingDirectory; R2 root: $Root; bounded seconds: $Seconds"
        if ($Cdb) { Write-Host "CDB log: $logPath" }
        $deadline = if ($Seconds -gt 0) { (Get-Date).AddSeconds($Seconds) } else { [DateTime]::MaxValue }
        while ($true) {
            $current = Get-ProcessRecord $script:OwnedGamePid
            if ($null -eq $current) { break }
            if ($Seconds -gt 0 -and (Get-Date) -ge $deadline) {
                $script:TimedOut = $true
                break
            }
            Start-Sleep -Seconds 1
        }
    } finally {
        Stop-OwnedProcess $script:OwnedGamePid $script:ExpectedGameProcessPath
        if ($script:OwnedLauncherPid -gt 0 -and $script:OwnedLauncherPid -ne $script:OwnedGamePid) {
            Stop-OwnedProcess $script:OwnedLauncherPid $script:ExpectedLauncherPath
        }
        if ($script:TimedOut) { Write-Host "Bounded window reached $Seconds seconds; stopped only PIDs launched by this script." }
        if ($Cdb -and $logPath) { Write-Host "CDB log: $logPath" }
        $env:PATH = $oldPath
        if ($null -eq $oldFromLauncher) { Remove-Item Env:\FROM_DELTA_LAUNCHER -ErrorAction SilentlyContinue } else { $env:FROM_DELTA_LAUNCHER = $oldFromLauncher }
    }
}

try {
    if (-not (Test-Path -LiteralPath $Root -PathType Container)) { throw "Permanent R2 root does not exist: $Root. Run .build\r2-root.ps1 -Create, then -Stage." }
    if ($R1OFakeDedicated -and $Mode -ne 'Dedicated') { throw '-R1OFakeDedicated is valid only with -Mode Dedicated.' }
    Assert-R2Idle
    Acquire-GameLock
    Assert-R2Idle
    Start-R2Game
} finally {
    if ($script:LockToken) { Release-GameLock }
    if ($script:TimedOut) {
        Write-Host "Window ended at the requested $Seconds-second bound."
    }
}
