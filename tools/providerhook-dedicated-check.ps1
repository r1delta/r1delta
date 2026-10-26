[CmdletBinding()]
param(
    [ValidateSet('mp_box', 'mp_black_water_canal', 'mp_coliseum', 'mp_coliseum_column', 'mp_colony02', 'mp_complex3', 'mp_crashsite3', 'mp_drydock', 'mp_eden', 'mp_forwardbase_kodai', 'mp_glitch', 'mp_grave', 'mp_homestead', 'mp_lf_deck', 'mp_lf_meadow', 'mp_lf_stacks', 'mp_lf_township', 'mp_lf_traffic', 'mp_lf_uma', 'mp_relic02', 'mp_thaw')]
    [string] $Map = 'mp_box',
    [ValidateRange(20, 180)]
    [int] $Seconds = 30,
    [ValidateRange(5, 165)]
    [int] $MapTimeoutSeconds = 20,
    [ValidateRange(27060, 27069)]
    [int] $Port = 27060,
    [ValidateRange(8760, 8769)]
    [int] $McpPort = 8761,
    [switch] $WarmCache,
    [switch] $KeepCache
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$runScript = Join-Path $repoRoot '.build\run-in-r2.ps1'
$root = 'S:\game\r2-provider'
$coreRoot = Join-Path $root 'r1delta'
$runlogs = 'C:\Users\r3muxd\tmp\r2port\runlogs'
$runsDirectory = 'C:\Users\r3muxd\tmp\r2port\runs'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$transcriptPath = Join-Path $runlogs "ProviderHook-dedicated-$Map-$stamp.txt"
$runnerStdout = Join-Path $runlogs "ProviderHook-dedicated-runner-$Map-$stamp.stdout.txt"
$runnerStderr = Join-Path $runlogs "ProviderHook-dedicated-runner-$Map-$stamp.stderr.txt"
$sessionId = ''
$runner = $null
$transcriptStarted = $false
$script:NextMcpId = 3

function Invoke-Mcp([hashtable] $Message, [string] $Session = '') {
    $headers = @{
        'MCP-Protocol-Version' = '2025-06-18'
        Accept = 'application/json, text/event-stream'
    }
    if ($Session) { $headers['Mcp-Session-Id'] = $Session }
    $body = $Message | ConvertTo-Json -Depth 16 -Compress
    return Invoke-WebRequest -Uri "http://127.0.0.1:$McpPort/" -Method Post -Headers $headers -ContentType 'application/json' -Body $body -TimeoutSec 15 -UseBasicParsing
}

function ConvertFrom-McpContent([string] $Content) {
    $text = $Content.Trim()
    if ($text.StartsWith('data:', [StringComparison]::OrdinalIgnoreCase)) {
        $dataLines = @($text -split "`r?`n" | Where-Object { $_ -match '^data:\s*\{' })
        if ($dataLines.Count -gt 0) { $text = $dataLines[-1] -replace '^data:\s*', '' }
    }
    return $text | ConvertFrom-Json
}

function Invoke-McpTool([string] $Name, [hashtable] $Arguments) {
    $id = $script:NextMcpId++
    $response = Invoke-Mcp @{ jsonrpc = '2.0'; id = $id; method = 'tools/call'; params = @{ name = $Name; arguments = $Arguments } } $sessionId
    return (ConvertFrom-McpContent $response.Content).result
}

function Get-ConsoleLines {
    $result = Invoke-McpTool 'get_console_log' @{}
    if ($result.structuredContent -and $result.structuredContent.lines) {
        return @($result.structuredContent.lines | ForEach-Object { [string] $_ })
    }
    return @($result.content | ForEach-Object { [string] $_.text })
}

function Find-GameProcess {
    if ($null -eq $runner) { return $null }
    $expected = [IO.Path]::GetFullPath((Join-Path $root 'R1Delta_DS.exe'))
    $registrationPath = Join-Path $runsDirectory "$($runner.Id).json"
    if (Test-Path -LiteralPath $registrationPath -PathType Leaf) {
        try {
            $registration = Get-Content -LiteralPath $registrationPath -Raw | ConvertFrom-Json
            foreach ($identity in @($registration.processes)) {
                if (-not [IO.Path]::GetFullPath([string] $identity.path).Equals($expected, [StringComparison]::OrdinalIgnoreCase)) { continue }
                $record = Get-CimInstance Win32_Process -Filter "ProcessId=$([int] $identity.pid)" -ErrorAction SilentlyContinue
                if ($record -and $record.ExecutablePath -and [IO.Path]::GetFullPath([string] $record.ExecutablePath).Equals($expected, [StringComparison]::OrdinalIgnoreCase)) {
                    return $record
                }
            }
        } catch {}
    }
    return Get-CimInstance Win32_Process -Filter "ParentProcessId=$($runner.Id)" -ErrorAction SilentlyContinue |
        Where-Object { $_.ExecutablePath -and [IO.Path]::GetFullPath([string] $_.ExecutablePath).Equals($expected, [StringComparison]::OrdinalIgnoreCase) } |
        Select-Object -First 1
}

function Send-Console([string] $Command) {
    return Invoke-McpTool 'execute_console_command' @{ command = $Command; capture_output = $true }
}

try {
    New-Item -ItemType Directory -Path $runlogs -Force | Out-Null
    if (-not (Test-Path -LiteralPath $runScript -PathType Leaf)) { throw "Missing run launcher: $runScript" }
    if (-not (Test-Path -LiteralPath (Join-Path $root 'R1Delta_DS.exe') -PathType Leaf)) { throw "The ProviderHook root is not ready/staged: $root" }
    if (-not (Test-Path -LiteralPath $coreRoot -PathType Container)) { throw "Missing CORE directory: $coreRoot" }
    if ($MapTimeoutSeconds -ge $Seconds) { throw 'MapTimeoutSeconds must be shorter than the bounded run duration.' }

    $rootPrefix = [IO.Path]::GetFullPath($root).TrimEnd('\') + '\'
    $rootProcesses = @(Get-CimInstance Win32_Process -ErrorAction Stop | Where-Object {
        $_.ExecutablePath -and [IO.Path]::GetFullPath([string] $_.ExecutablePath).StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)
    })
    if ($rootProcesses.Count -gt 0) {
        throw "Refusing to clear/use the map cache while processes run from ${root}: $($rootProcesses.Name -join ', ')"
    }
    $looseMapPaths = @((Join-Path $coreRoot "maps\$Map.bsp"))
    foreach ($suffix in @('_env', '_fx', '_script', '_snd', '_spawn')) {
        $looseMapPaths += Join-Path $coreRoot "maps\${Map}_${suffix}.ent"
    }
    $looseMapFiles = @($looseMapPaths | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf })
    if ($looseMapFiles.Count -gt 0) { throw "No-loose check refused because files exist: $($looseMapFiles -join ', ')" }

    if (-not $WarmCache) {
        $mapCache = Join-Path $coreRoot 'r2port_cache'
        if (Test-Path -LiteralPath $mapCache) { Remove-Item -LiteralPath $mapCache -Recurse -Force }
    }
    Start-Transcript -LiteralPath $transcriptPath -Force | Out-Null
    $transcriptStarted = $true
    Write-Output "RUN owner=ProviderHook root=$root mode=Dedicated map=$Map hostport=$Port mcp=$McpPort warm-cache=$WarmCache"
    Write-Output "NO_LOOSE_MAP_FILES map=$Map"
    Write-Output "LOG transcript=$transcriptPath"

    $gameArguments = "@('+sv_cheats','1')"
    $runCommand = "`$gameArguments = $gameArguments`n& '$runScript' -Owner ProviderHook -Root r2-provider -Mode Dedicated -Map '$Map' -Seconds $Seconds -Port $Port -McpPort $McpPort -NoConsole -WindowStyle Hidden -FatalScriptErrors -GameArguments `$gameArguments"
    $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($runCommand))
    $powershell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $runner = Start-Process -FilePath $powershell -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-EncodedCommand', $encoded) -WindowStyle Hidden -RedirectStandardOutput $runnerStdout -RedirectStandardError $runnerStderr -PassThru
    Write-Output "RUNNER pid=$($runner.Id) stdout=$runnerStdout stderr=$runnerStderr"

    $healthDeadline = (Get-Date).AddSeconds(60)
    $healthReady = $false
    while ((Get-Date) -lt $healthDeadline) {
        if ($runner.HasExited) { throw "Run launcher exited before MCP health was ready. See $runnerStdout and $runnerStderr" }
        try {
            $health = Invoke-WebRequest -Uri "http://127.0.0.1:$McpPort/health" -TimeoutSec 2 -UseBasicParsing
            if ($health.StatusCode -eq 200) { $healthReady = $true; break }
        } catch {}
        Start-Sleep -Milliseconds 500
    }
    if (-not $healthReady) { throw "MCP health did not start at port $McpPort within 60 seconds." }
    $game = Find-GameProcess
    if ($null -eq $game) { throw "Could not identify the dedicated process under $root." }
    Write-Output "SERVER pid=$($game.ProcessId) path=$($game.ExecutablePath)"

    $initializeResponse = Invoke-Mcp @{ jsonrpc = '2.0'; id = 1; method = 'initialize'; params = @{ protocolVersion = '2025-03-26'; capabilities = @{}; clientInfo = @{ name = 'ProviderHook'; version = '1' } } }
    $sessionId = [string] $initializeResponse.Headers['Mcp-Session-Id']
    if (-not $sessionId) { throw 'MCP initialize response did not include a session ID.' }
    $null = Invoke-Mcp @{ jsonrpc = '2.0'; method = 'notifications/initialized' } $sessionId
    $toolResponse = Invoke-Mcp @{ jsonrpc = '2.0'; id = 2; method = 'tools/list'; params = @{} } $sessionId
    $toolNames = @((ConvertFrom-McpContent $toolResponse.Content).result.tools | ForEach-Object { [string] $_.name })
    Write-Output "MCP_TOOLS $($toolNames -join ',')"
    if ($toolNames -notcontains 'execute_console_command' -or $toolNames -notcontains 'get_game_state') {
        throw 'MCP does not expose the console-command and game-state tools required by this check.'
    }

    $deadline = (Get-Date).AddSeconds($MapTimeoutSeconds)
    $mapLoaded = $false
    $mapState = $null
    while ((Get-Date) -lt $deadline -and -not $runner.HasExited) {
        $stateResult = Invoke-McpTool 'get_game_state' @{}
        $mapState = $stateResult.structuredContent
        if ($null -ne $mapState) {
            $currentMap = ([string] $mapState.current_map) -replace '(?i)\.bsp$', ''
            if ($mapState.in_game -and $currentMap.Equals($Map, [StringComparison]::OrdinalIgnoreCase)) { $mapLoaded = $true; break }
        }
        Start-Sleep -Milliseconds 1000
    }
    Write-Output "MAP_STATE=$($mapState | ConvertTo-Json -Compress -Depth 10)"
    $consoleLines = Get-ConsoleLines
    Write-Output 'CONSOLE_BEGIN'
    $consoleLines | ForEach-Object { Write-Output $_ }
    Write-Output 'CONSOLE_END'
    $providerServed = @($consoleLines | Where-Object { $_ -match "R2 map provider served map=$([regex]::Escape($Map))\b" })
    if ($Map -ne 'mp_box' -and $providerServed.Count -eq 0) {
        throw "The R2 map provider did not log serving $Map."
    }
    $scriptErrors = @($consoleLines | Where-Object { $_ -match '(?i)script error|fatal script' })
    Write-Output "SCRIPT_ERROR_COUNT=$($scriptErrors.Count)"
    $masterMessages = @($consoleLines | Where-Object { $_ -match 'MasterServerClient: (server is|Heartbeat|Delist)' })
    Write-Output "MASTER_ADVERTISEMENT_MESSAGES=$($masterMessages.Count)"
    if ($masterMessages.Count -gt 0) { $masterMessages | ForEach-Object { Write-Output "MASTER_MESSAGE=$_" } }
    if (-not $mapLoaded) { throw "Dedicated server did not reach map $Map within $MapTimeoutSeconds seconds." }
    if ($scriptErrors.Count -gt 0) { throw "Dedicated server reported script errors on $Map." }

    $status = Send-Console 'status'
    Write-Output "STATUS $($status | ConvertTo-Json -Compress -Depth 10)"
    $stats = Send-Console 'r2port_dump_stats'
    Write-Output "STATS_COMMAND $($stats | ConvertTo-Json -Compress -Depth 10)"
    Start-Sleep -Milliseconds 750
    $statsLines = Get-ConsoleLines
    Write-Output 'CONSOLE_WITH_STATS_BEGIN'
    $statsLines | ForEach-Object { Write-Output $_ }
    Write-Output 'CONSOLE_WITH_STATS_END'
    Write-Output "RESULT=PASS map=$Map dedicated=loaded script-errors=0"
}
finally {
    if ($sessionId) { try { $null = Send-Console 'quit' } catch {} }
    if ($null -ne $runner) {
        $runner.Refresh()
        if (-not $runner.HasExited) { try { $null = $runner.WaitForExit(15000) } catch {} }
        if (-not $runner.HasExited) {
            $game = Find-GameProcess
            if ($null -ne $game -and [IO.Path]::GetFullPath([string] $game.ExecutablePath).Equals([IO.Path]::GetFullPath((Join-Path $root 'R1Delta_DS.exe')), [StringComparison]::OrdinalIgnoreCase)) {
                Stop-Process -Id ([int] $game.ProcessId) -Force -ErrorAction SilentlyContinue
            }
            try { $null = $runner.WaitForExit(15000) } catch {}
        }
        $runner.Refresh()
        if (-not $runner.HasExited) { Stop-Process -Id $runner.Id -Force -ErrorAction SilentlyContinue }
        $runner.Refresh()
    }
    if ($null -ne $runner -and $runner.HasExited -and -not $KeepCache) {
        $mapCache = Join-Path $coreRoot 'r2port_cache'
        if (Test-Path -LiteralPath $mapCache) { Remove-Item -LiteralPath $mapCache -Recurse -Force -ErrorAction SilentlyContinue }
    }
    if ($transcriptStarted) { Stop-Transcript | Out-Null }
}
