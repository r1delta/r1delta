<#
Runtime check of the legacy dedicated server's playlist buffers (engine/playlist_loader.cpp).

Starts R1Delta_DS.exe from a PRIVATE test root (never a user tree: the root's r1delta\playlists.txt
is replaced during the run and restored afterwards) with the CORE playlists and enlarged copies
of it, in:
  * legacy console dedicated   (-console)
  * R1O fake-dedicated         (-console -r1o_dedi), checked through the loader's own size read
  * legacy VGUI dedicated      (no -console), under CDB, optional (-Gui): this is the start-up
                               path that loads playlists.txt before server.dll exists
and requires, for every size: the server starts, the startup line reports the exact file size, and
neither "larger than the buffer" nor "playlists.txt is ... bytes" appears. A file bigger than the
server-info message must stop the server with the explicit size error instead.

Every launch passes -multiple -allowmultiple -nomessagebox, runs hidden with R1DELTA_BACKGROUND=1,
uses its own PIDs only and never retries.

usage: verify_playlist_buffer.ps1 -Root <private root> [-BasePort 27400] [-Gui] [-SkipR1O]
#>
param(
    [Parameter(Mandatory)] [string]$Root,
    [int]$BasePort = 27400,
    [switch]$Gui,
    [switch]$SkipR1O,
    [int]$WaitSeconds = 60,
    [string]$Cdb = 'C:\Program Files\WindowsApps\Microsoft.WinDbg_1.2603.20001.0_x64__8wekyb3d8bbwe\amd64\cdb.exe'
)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$work = Join-Path $env:TEMP ("playlistbuf-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $work | Out-Null
$playlists = Join-Path $Root 'r1delta\playlists.txt'
$backup = Join-Path $work 'playlists.original.txt'
Copy-Item $playlists $backup
$stockSize = (Get-Item $backup).Length
$failures = New-Object System.Collections.Generic.List[string]

function Check([bool]$ok, [string]$text) {
    Write-Host ($(if ($ok) { 'PASS  ' } else { 'FAIL  ' }) + $text)
    if (-not $ok) { $failures.Add($text) }
}

function New-Variant([int]$Factor) {
    $path = Join-Path $work "playlists.x$Factor.txt"
    & python (Join-Path $here 'playlist_variants.py') $backup $path ($stockSize * $Factor) | Out-Null
    return $path
}

function Stop-Tree([int]$ProcessId) {
    if ($ProcessId) { & cmd.exe /c "taskkill.exe /PID $ProcessId /T /F >nul 2>&1" }
}

function Start-Dedicated([string]$Tag, [int]$Port, [string[]]$ModeArgs) {
    $log = Join-Path $work "$Tag.log"
    $game = @('-multiple', '-allowmultiple', '-nomessagebox', '-novid', '-port', $Port, "+hostport", $Port,
        '+sv_hibernate_when_empty', '0', '+developer', '0', '+delta_script_error_service_url', 'http://127.0.0.1:9/',
        '+map', 'mp_lobby') + $ModeArgs
    $exe = Join-Path $Root 'R1Delta_DS.exe'
    $flag = Join-Path $Root 'r1delta_background.flag'
    if (-not (Test-Path $flag)) { New-Item -ItemType File -Path $flag | Out-Null }
    $env:FROM_DELTA_LAUNCHER = '1'
    $env:R1DELTA_BACKGROUND = '1'
    $env:PATH = "$Root\r1delta\bin_delta;$Root\r1delta\bin;" + $env:PATH
    $quoted = ($game | ForEach-Object { "$_" }) -join ' '
    # cmd.exe owns a hidden console so the dedicated console reader works while stdout goes to a file
    $cmd = Start-Process -FilePath $env:ComSpec -ArgumentList ('/c ""' + $exe + '" ' + $quoted + ' > "' + $log + '" 2>&1"') `
        -WorkingDirectory $Root -WindowStyle Hidden -PassThru
    Start-Sleep -Milliseconds 800
    $child = Get-CimInstance Win32_Process -Filter "Name='R1Delta_DS.exe' AND ParentProcessId=$($cmd.Id)" | Select-Object -First 1
    return [pscustomobject]@{ Cmd = $cmd; Pid = $(if ($child) { $child.ProcessId } else { 0 }); Log = $log }
}

function Wait-Listening($run, [int]$Port) {
    $deadline = (Get-Date).AddSeconds($WaitSeconds)
    while ((Get-Date) -lt $deadline) {
        if (-not (Get-Process -Id $run.Pid -ErrorAction SilentlyContinue)) { return 'exited' }
        if (Get-NetUDPEndpoint -OwningProcess $run.Pid -ErrorAction SilentlyContinue | Where-Object LocalPort -eq $Port) { Start-Sleep 3; return $(if (Get-Process -Id $run.Pid -ErrorAction SilentlyContinue) { 'listening' } else { 'exited' }) }
        Start-Sleep -Milliseconds 500
    }
    return 'timeout'
}

try {
    $variants = [ordered]@{ 'core' = $backup; 'x2' = (New-Variant 2); 'x4' = (New-Variant 4) }
    $port = $BasePort
    $modes = @(@{ Name = 'legacy-console'; Args = @('-console', '-dev') })
    if (-not $SkipR1O) { $modes += @{ Name = 'r1o-fake-dedi'; Args = @('-console', '-dev', '-r1o_dedi') } }
    foreach ($mode in $modes) {
        foreach ($v in $variants.GetEnumerator()) {
            $port++
            Copy-Item $v.Value $playlists -Force
            $size = (Get-Item $playlists).Length
            $run = Start-Dedicated "$($mode.Name)-$($v.Key)" $port $mode.Args
            $state = Wait-Listening $run $port
            Stop-Tree $run.Pid; Stop-Tree $run.Cmd.Id
            $text = if (Test-Path $run.Log) { Get-Content $run.Log -Raw } else { '' }
            Check ($state -eq 'listening') "$($mode.Name) $($v.Key) ($size bytes): server reached listening (state $state)"
            Check ($text -notmatch 'larger than the buffer' -and $text -notmatch 'playlists\.txt is \d+ bytes') "$($mode.Name) $($v.Key): no playlists size error"
            if ($mode.Name -eq 'legacy-console') {
                Check ($text -match "R1Delta: playlists ""playlists\.txt"": $size bytes read") "$($mode.Name) $($v.Key): startup line reports $size bytes"
            }
        }
    }

    if ($Gui) {
        foreach ($v in $variants.GetEnumerator()) {
            $port++
            Copy-Item $v.Value $playlists -Force
            $size = (Get-Item $playlists).Length
            $cdbLog = Join-Path $work "gui-$($v.Key).cdb.log"
            $cmds = Join-Path $work 'gui.cmds'
            Set-Content $cmds -Value "sxe -c "".echo ==AV==;r rip;k 20;gh"" av`ng" -Encoding ascii
            $env:FROM_DELTA_LAUNCHER = '1'; $env:R1DELTA_BACKGROUND = '1'
            $env:PATH = "$Root\r1delta\bin_delta;$Root\r1delta\bin;" + $env:PATH
            $cdbArgs = @('-G', '-logo', $cdbLog, '-cf', $cmds, (Join-Path $Root 'R1Delta_DS.exe'),
                '-multiple', '-allowmultiple', '-nomessagebox', '-novid', '-port', $port, "+hostport", $port,
                '+sv_hibernate_when_empty', '0', '+developer', '0', '+delta_script_error_service_url', 'http://127.0.0.1:9/', '+map', 'mp_lobby')
            $p = Start-Process -FilePath $Cdb -ArgumentList $cdbArgs -WorkingDirectory $Root -WindowStyle Hidden -PassThru
            Start-Sleep -Seconds 30
            Stop-Tree $p.Id
            $text = if (Test-Path $cdbLog) { Get-Content $cdbLog -Raw } else { '' }
            Check ($text -match "R1Delta: playlists ""playlists\.txt"": $size bytes read") "gui $($v.Key) ($size bytes): startup line reports $size bytes"
            Check ($text -notmatch 'larger than the buffer' -and $text -notmatch 'Failed to load playlists' -and $text -notmatch 'Access violation') "gui $($v.Key): no size error, no load failure, no access violation"
        }
    }

    # a file no client could be sent must stop the server with the explicit size error
    $port++
    $huge = Join-Path $work 'playlists.huge.txt'
    & python (Join-Path $here 'playlist_variants.py') $backup $huge ($stockSize * 32) | Out-Null
    Copy-Item $huge $playlists -Force
    $run = Start-Dedicated 'legacy-huge' $port @('-console', '-dev')
    $state = Wait-Listening $run $port
    Stop-Tree $run.Pid; Stop-Tree $run.Cmd.Id
    $text = Get-Content $run.Log -Raw
    Check ($state -ne 'listening' -and $text -match 'playlists\.txt is \d+ bytes and sends \d+ bytes') 'oversize file: server stops with the explicit playlists size error'
}
finally {
    Copy-Item $backup $playlists -Force
    if (-not $failures.Count) { Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue }
}
if ($failures.Count) { Write-Host "$($failures.Count) check(s) failed; logs kept in $work"; exit 1 }
Write-Host 'all playlist buffer runtime checks passed'
