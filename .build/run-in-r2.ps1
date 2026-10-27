[CmdletBinding()]
param(
    [Alias('Root')]
    [ValidateSet('r2-provider', 'r2-texlib', 'r2-shader', 'r2-lead')]
    [string] $RootName = 'r2-shader',

    [ValidateSet('ProviderHook', 'TexLib', 'ShaderLab', 'RootBuilder', 'Lead')]
    [string] $Owner = 'RootBuilder',

    [ValidateSet('Client', 'Dedicated', 'Listen')]
    [string] $Mode = 'Client',

    [string] $Map = 'mp_box',
    [int] $Port = 0,
    [int] $ClientPort = 0,
    [int] $McpPort = 0,
    [ValidateCount(2, 2)]
    [int[]] $McpPortRange,
    [ValidateRange(1, 180)]
    [int] $Seconds = 180,
    [string] $LogDirectory = 'C:\Users\r3muxd\tmp\r2port\runlogs',
    [switch] $Cdb,
    [string] $CdbCommandFile = 'C:\Users\r3muxd\tmp\r2port\ds-cdb-cmds.txt',
    [switch] $NoConsole,
    [ValidateSet('Normal', 'Minimized', 'Hidden')]
    [string] $WindowStyle,
    [switch] $R1OFakeDedicated,
    [switch] $FatalScriptErrors,
    [string[]] $GameArguments = @()
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:RunStartedAt = [DateTimeOffset]::UtcNow
$script:RunDeadline = $script:RunStartedAt.AddSeconds($Seconds)
$script:RunsDirectory = 'C:\Users\r3muxd\tmp\r2port\runs'
$script:RegistryMutexName = 'Local\R2PortRunRegistry'
$script:RegistrationPath = Join-Path $script:RunsDirectory "$PID.json"
$script:RegistrationToken = [Guid]::NewGuid().ToString('N')
$script:RegistrationCreated = $false
$script:PortWasSpecified = $PSBoundParameters.ContainsKey('Port')
$script:ClientPortWasSpecified = $PSBoundParameters.ContainsKey('ClientPort')
$script:McpPortWasSpecified = $PSBoundParameters.ContainsKey('McpPort')
$script:McpPortRangeWasSpecified = $PSBoundParameters.ContainsKey('McpPortRange')
$script:NoConsoleWasSpecified = $PSBoundParameters.ContainsKey('NoConsole')
$script:WindowStyleWasSpecified = $PSBoundParameters.ContainsKey('WindowStyle')
$script:SuppressConsole = if ($script:NoConsoleWasSpecified) { [bool] $NoConsole } else { $Mode -eq 'Client' -and $script:McpPortWasSpecified }
$script:SelectedWindowStyle = if ($script:WindowStyleWasSpecified) { $WindowStyle } elseif ($Mode -eq 'Dedicated') { 'Hidden' } else { 'Minimized' }
$script:RootWasSpecified = $PSBoundParameters.ContainsKey('RootName')
$script:OwnerWasSpecified = $PSBoundParameters.ContainsKey('Owner')
$script:RootByOwner = @{
    ProviderHook = 'r2-provider'
    TexLib       = 'r2-texlib'
    ShaderLab    = 'r2-shader'
    RootBuilder  = 'r2-shader'
    Lead         = 'r2-lead'
}
$script:OwnerByRoot = @{
    'r2-provider' = 'ProviderHook'
    'r2-texlib'   = 'TexLib'
    'r2-shader'   = 'RootBuilder'
    'r2-lead'     = 'Lead'
}
if ($script:OwnerWasSpecified -and -not $script:RootWasSpecified) {
    $RootName = $script:RootByOwner[$Owner]
} elseif ($script:RootWasSpecified -and -not $script:OwnerWasSpecified) {
    $Owner = $script:OwnerByRoot[$RootName]
}
if ($script:RootByOwner[$Owner] -ne $RootName) {
    throw "Owner '$Owner' is mapped to '$($script:RootByOwner[$Owner])', not '$RootName'."
}

$Root = Join-Path 'S:\game' $RootName
$script:PhysicalRoot = Join-Path 'C:\Users\r3muxd\newr1root3\game' $RootName
$CoreRoot = Join-Path $Root 'r1delta'
$CdbPath = 'C:\Program Files\WindowsApps\Microsoft.WinDbg_1.2603.20001.0_x64__8wekyb3d8bbwe\amd64\cdb.exe'
$ClientWorkingDirectory = 'C:\whatever'
$script:RunProcessIdentities = @()
$script:SelectedPorts = [ordered]@{}
$script:OwnerRanges = $null
$script:OwnerMcpRange = $null
$script:TimedOut = $false
$script:OwnedLauncherPid = 0
$script:OwnedGamePid = 0
$script:ExpectedLauncherPath = ''
$script:ExpectedGamePath = ''
$script:ExpectedGameProcessPath = ''

function Get-ProcessRecord([int] $ProcessId) {
    if ($ProcessId -le 0) { return $null }
    return Get-CimInstance Win32_Process -Filter "ProcessId=$ProcessId" -ErrorAction SilentlyContinue
}

function Convert-CimCreationTime($Record) {
    if ($null -eq $Record -or $null -eq $Record.CreationDate) { return $null }
    if ($Record.CreationDate -is [DateTime]) {
        $created = $Record.CreationDate.ToUniversalTime()
    } else {
        $created = [System.Management.ManagementDateTimeConverter]::ToDateTime([string] $Record.CreationDate).ToUniversalTime()
    }
    return [DateTimeOffset]::new($created)
}

function Get-ProcessIdentity([int] $ProcessId) {
    $record = Get-ProcessRecord $ProcessId
    if ($null -eq $record -or [string]::IsNullOrWhiteSpace([string] $record.ExecutablePath)) { return $null }
    $created = Convert-CimCreationTime $record
    if ($null -eq $created) { return $null }
    return [pscustomobject]@{
        pid     = [int] $ProcessId
        path    = [IO.Path]::GetFullPath([string] $record.ExecutablePath)
        created = $created.ToString('o')
    }
}

function Get-OwnerPortRanges {
    switch ($Owner) {
        'ProviderHook' {
            return [pscustomobject]@{
                Server = [int[]] (27060..27069)
                Client = [int[]] (27090..27099)
                Mcp    = [int[]] (8760..8769)
            }
        }
        'TexLib' {
            $network = [int[]] (27080..27089)
            return [pscustomobject]@{
                Server = $network
                Client = $network
                Mcp    = [int[]] (8780..8789)
            }
        }
        'ShaderLab' {
            $network = [int[]] (27070..27079)
            return [pscustomobject]@{
                Server = $network
                Client = $network
                Mcp    = [int[]] (8770..8779)
            }
        }
        'RootBuilder' {
            $network = [int[]] (27110..27119)
            return [pscustomobject]@{
                Server = $network
                Client = $network
                Mcp    = [int[]] (8790..8799)
            }
        }
        'Lead' {
            $network = [int[]] (27050..27059)
            return [pscustomobject]@{
                Server = $network
                Client = $network
                Mcp    = $null
            }
        }
    }
}

function Initialize-PortRanges {
    $script:OwnerRanges = Get-OwnerPortRanges
    if ($script:McpPortRangeWasSpecified) {
        if ($Owner -ne 'Lead') { throw '-McpPortRange is only valid for Lead, which has no default MCP range.' }
        if ($McpPortRange.Count -ne 2) { throw '-McpPortRange must have exactly two values: inclusive start and end.' }
        $first = [int] $McpPortRange[0]
        $last = [int] $McpPortRange[1]
        if ($first -lt 1 -or $last -gt 65535 -or $first -gt $last) {
            throw "Invalid MCP port range '$first-$last'."
        }
        $script:OwnerMcpRange = [int[]] ($first..$last)
    } else {
        $script:OwnerMcpRange = $script:OwnerRanges.Mcp
    }

    if ($script:PortWasSpecified) {
        if ($script:OwnerRanges.Server -notcontains $Port) { throw "-Port $Port is outside the $Owner server port range." }
        if ($Mode -eq 'Client') { throw '-Port is only used by Listen and Dedicated modes.' }
    }
    if ($script:ClientPortWasSpecified) {
        if ($script:OwnerRanges.Client -notcontains $ClientPort) { throw "-ClientPort $ClientPort is outside the $Owner client port range." }
        if ($Mode -eq 'Dedicated') { throw '-ClientPort is only used by Client and Listen modes.' }
    }
    if ($script:McpPortWasSpecified -and $null -eq $script:OwnerMcpRange) {
        throw 'Lead has no default MCP port range; supply -McpPortRange with -McpPort to enable MCP.'
    }
    if (($script:McpPortWasSpecified -or $script:McpPortRangeWasSpecified) -and $null -eq $script:OwnerMcpRange) {
        throw "Owner '$Owner' has no MCP port range."
    }
}

function Invoke-WithRegistryMutex([scriptblock] $Action) {
    $mutex = [Threading.Mutex]::new($false, $script:RegistryMutexName)
    $held = $false
    try {
        try {
            $held = $mutex.WaitOne([TimeSpan]::FromSeconds(15))
        } catch [Threading.AbandonedMutexException] {
            $held = $true
        }
        if (-not $held) { throw 'Timed out waiting for the R2 run-registry mutex.' }
        & $Action
    } finally {
        if ($held) { [void] $mutex.ReleaseMutex() }
        $mutex.Dispose()
    }
}

function Read-RunRecord([string] $Path) {
    $record = Get-Content -LiteralPath $Path -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    foreach ($name in @('owner', 'purpose', 'pids', 'ports', 'started', 'deadline', 'token', 'processes')) {
        if ($null -eq $record.PSObject.Properties[$name]) { throw "Run registry record '$Path' is missing '$name'." }
    }
    if ([string]::IsNullOrWhiteSpace([string] $record.token)) { throw "Run registry record '$Path' has no ownership token." }
    [void] [DateTimeOffset]::Parse([string] $record.started)
    [void] [DateTimeOffset]::Parse([string] $record.deadline)
    $pids = @($record.pids | ForEach-Object { [int] $_ } | Sort-Object -Unique)
    $identityPids = @($record.processes | ForEach-Object { [int] $_.pid } | Sort-Object -Unique)
    if ($pids.Count -ne $identityPids.Count -or @($pids | Where-Object { $identityPids -notcontains $_ }).Count -ne 0) {
        throw "Run registry record '$Path' has inconsistent PIDs and process identities."
    }
    foreach ($identity in @($record.processes)) {
        if ([string]::IsNullOrWhiteSpace([string] $identity.path)) { throw "Run registry record '$Path' has a process without a path." }
        [void] [DateTimeOffset]::Parse([string] $identity.created)
    }
    return $record
}

function Test-RunRecordAlive($Record) {
    foreach ($identity in @($Record.processes)) {
        $current = Get-ProcessRecord ([int] $identity.pid)
        if ($null -eq $current -or [string]::IsNullOrWhiteSpace([string] $current.ExecutablePath)) { continue }
        $currentPath = [IO.Path]::GetFullPath([string] $current.ExecutablePath)
        if (-not $currentPath.Equals([IO.Path]::GetFullPath([string] $identity.path), [StringComparison]::OrdinalIgnoreCase)) { continue }
        $currentCreated = Convert-CimCreationTime $current
        if ($null -eq $currentCreated) { continue }
        $recordedCreated = [DateTimeOffset]::Parse([string] $identity.created)
        if ([Math]::Abs(($currentCreated - $recordedCreated).TotalSeconds) -le 3) { return $true }
    }
    return $false
}

function Get-ActiveRunRecords {
    $records = @()
    if (-not (Test-Path -LiteralPath $script:RunsDirectory -PathType Container)) { return $records }
    foreach ($path in [IO.Directory]::GetFiles($script:RunsDirectory, '*.json')) {
        $record = $null
        try {
            $record = Read-RunRecord $path
        } catch {
            $namePid = 0
            $baseName = [IO.Path]::GetFileNameWithoutExtension($path)
            if ([int]::TryParse($baseName, [ref] $namePid) -and $null -eq (Get-ProcessRecord $namePid)) {
                Remove-Item -LiteralPath $path -Force
                continue
            }
            throw "Cannot safely interpret run registry record '$path': $($_.Exception.Message)"
        }
        if (Test-RunRecordAlive $record) {
            $records += $record
        } else {
            Remove-Item -LiteralPath $path -Force
        }
    }
    return $records
}

function Test-PortAvailable([string] $Protocol, [int] $PortNumber, [object[]] $Records, $AssignedPorts) {
    foreach ($record in $Records) {
        foreach ($property in $record.ports.PSObject.Properties) {
            if ([int] $property.Value -eq $PortNumber) { return $false }
        }
    }
    foreach ($entry in $AssignedPorts.GetEnumerator()) {
        if ([int] $entry.Value -eq $PortNumber) { return $false }
    }
    if ($Protocol -eq 'Tcp') {
        $endpoint = Get-NetTCPConnection -LocalPort $PortNumber -ErrorAction SilentlyContinue | Select-Object -First 1
    } else {
        $endpoint = Get-NetUDPEndpoint -LocalPort $PortNumber -ErrorAction SilentlyContinue | Select-Object -First 1
    }
    return ($null -eq $endpoint)
}

function Select-RunPort([string] $Name, [string] $Protocol, [int[]] $Range, [int] $Requested, [bool] $Explicit, [object[]] $Records) {
    if ($null -eq $Range -or $Range.Count -eq 0) { throw "No $Name port range is configured for owner '$Owner'." }
    if ($Explicit) {
        if ($Range -notcontains $Requested) { throw "-$Name $Requested is outside the $Owner port range." }
        if (-not (Test-PortAvailable $Protocol $Requested $Records $script:SelectedPorts)) {
            throw "$Protocol port $Requested is already reserved or in use."
        }
        return $Requested
    }
    foreach ($candidate in $Range) {
        if (Test-PortAvailable $Protocol ([int] $candidate) $Records $script:SelectedPorts) { return [int] $candidate }
    }
    throw "No free $Name port remains in the $Owner range ($($Range[0])-$($Range[-1]))."
}

function New-RunRecord {
    return [ordered]@{
        owner     = $Owner
        purpose   = "$Mode game from $RootName"
        pids      = @($script:RunProcessIdentities | ForEach-Object { [int] $_.pid } | Sort-Object -Unique)
        ports     = $script:SelectedPorts
        started   = $script:RunStartedAt.ToString('o')
        deadline  = $script:RunDeadline.ToString('o')
        token     = $script:RegistrationToken
        processes = @($script:RunProcessIdentities)
    }
}

function Write-RunRecord([string] $Path, $Record, [bool] $Create) {
    $json = ConvertTo-Json -InputObject $Record -Depth 8 -Compress
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes($json + [Environment]::NewLine)
    if ($Create) {
        $stream = $null
        try {
            $stream = [IO.File]::Open($Path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
            $stream.Write($bytes, 0, $bytes.Length)
            $stream.Flush($true)
        } finally {
            if ($null -ne $stream) { $stream.Dispose() }
        }
        return
    }

    $temporary = "$Path.$PID.$([Guid]::NewGuid().ToString('N')).tmp"
    $backup = "$Path.$PID.$([Guid]::NewGuid().ToString('N')).bak"
    try {
        [IO.File]::WriteAllBytes($temporary, $bytes)
        if ([IO.File]::Exists($Path)) {
            [IO.File]::Replace($temporary, $Path, $backup)
        } else {
            [IO.File]::Move($temporary, $Path)
        }
    } finally {
        if ([IO.File]::Exists($backup)) { Remove-Item -LiteralPath $backup -Force }
        if ([IO.File]::Exists($temporary)) { Remove-Item -LiteralPath $temporary -Force }
    }
}

function Register-Run {
    New-Item -ItemType Directory -Path $script:RunsDirectory -Force | Out-Null
    $runnerIdentity = Get-ProcessIdentity ([int] $PID)
    if ($null -eq $runnerIdentity) { throw "Cannot identify launcher process PID $PID for the run registry." }
    $script:RunProcessIdentities = @($runnerIdentity)

    Invoke-WithRegistryMutex {
        $activeRecords = @(Get-ActiveRunRecords)
        $script:SelectedPorts = [ordered]@{}
        if ($Mode -eq 'Listen' -or $Mode -eq 'Dedicated') {
            $script:SelectedPorts['ServerUdp'] = Select-RunPort 'Port' 'Udp' $script:OwnerRanges.Server $Port $script:PortWasSpecified $activeRecords
        }
        if ($Mode -eq 'Listen' -or $Mode -eq 'Client') {
            $script:SelectedPorts['ClientUdp'] = Select-RunPort 'ClientPort' 'Udp' $script:OwnerRanges.Client $ClientPort $script:ClientPortWasSpecified $activeRecords
        }
        if ($script:McpPortWasSpecified -or ($Owner -eq 'Lead' -and $script:McpPortRangeWasSpecified)) {
            $script:SelectedPorts['McpTcp'] = Select-RunPort 'McpPort' 'Tcp' $script:OwnerMcpRange $McpPort $script:McpPortWasSpecified $activeRecords
        }
        $record = New-RunRecord
        Write-RunRecord $script:RegistrationPath $record $true
        $script:RegistrationCreated = $true
    }
    Write-Host "Reserved ports: $(($script:SelectedPorts.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ', ')"
}

function Update-RunRegistration {
    if (-not $script:RegistrationCreated) { return }
    $record = New-RunRecord
    Invoke-WithRegistryMutex {
        if (-not [IO.File]::Exists($script:RegistrationPath)) { throw 'This run lost its registry record; refusing to continue unregistered.' }
        $current = Read-RunRecord $script:RegistrationPath
        if ([string] $current.token -ne $script:RegistrationToken) { throw 'Run registry ownership changed; refusing to overwrite another run.' }
        Write-RunRecord $script:RegistrationPath $record $false
    }
}

function Add-RunProcess([int] $ProcessId) {
    $identity = Get-ProcessIdentity $ProcessId
    if ($null -eq $identity) { throw "Cannot identify launched process PID $ProcessId." }
    $script:RunProcessIdentities = @($script:RunProcessIdentities | Where-Object { [int] $_.pid -ne $ProcessId }) + @($identity)
    Update-RunRegistration
}

function Remove-RunRegistration {
    if (-not $script:RegistrationCreated) { return }
    Invoke-WithRegistryMutex {
        if ([IO.File]::Exists($script:RegistrationPath)) {
            $current = Read-RunRecord $script:RegistrationPath
            if ([string] $current.token -eq $script:RegistrationToken) {
                Remove-Item -LiteralPath $script:RegistrationPath -Force
            }
        }
        $script:RegistrationCreated = $false
    }
}

function Assert-NoReservedGameArguments {
    foreach ($argument in $GameArguments) {
        if ([string] $argument -match '^\s*(?i:-port|-clientport|\+port|\+hostport|\+clientport|\+mcp_start_http|-usemcp)(?:\s|=|$)') {
            throw "GameArguments cannot override allocated port switch '$argument'."
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

function Get-R2RootRelativePath([string] $Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) { return $null }
    $fullPath = [IO.Path]::GetFullPath($Path)
    $rootPrefixes = @(
        [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
        [IO.Path]::GetFullPath($script:PhysicalRoot).TrimEnd('\') + '\'
    )
    foreach ($prefix in $rootPrefixes) {
        if ($fullPath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
            return $fullPath.Substring($prefix.Length)
        }
    }
    return $null
}

function Test-R2ExecutablePath([string] $Candidate, [string] $Expected) {
    $candidateRelative = Get-R2RootRelativePath $Candidate
    $expectedRelative = Get-R2RootRelativePath $Expected
    return $null -ne $candidateRelative -and $null -ne $expectedRelative -and
        $candidateRelative.Equals($expectedRelative, [StringComparison]::OrdinalIgnoreCase)
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
    $actualPath = [IO.Path]::GetFullPath([string] $record.ExecutablePath)
    $expected = [IO.Path]::GetFullPath($ExpectedPath)
    if ($expected.Equals([IO.Path]::GetFullPath($CdbPath), [StringComparison]::OrdinalIgnoreCase)) {
        if (-not $actualPath.Equals($expected, [StringComparison]::OrdinalIgnoreCase)) { return }
    } elseif (-not (Test-R2ExecutablePath $actualPath $expected)) {
        return
    }
    Stop-Process -Id $ProcessId -Force -ErrorAction SilentlyContinue
}

function Assert-RunRootLayout {
    foreach ($path in @($Root, $CoreRoot)) {
        $item = Get-Item -LiteralPath $path -Force -ErrorAction Stop
        if (-not $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Refusing to launch through a non-real root directory: $path"
        }
    }

    foreach ($name in @('bin', 'r1', 'tools')) {
        $path = Join-Path $Root $name
        $item = Get-Item -LiteralPath $path -Force -ErrorAction Stop
        if (-not $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0 -or [string] $item.LinkType -ne 'Junction') {
            throw "Expected the launch-root $name path to be a junction: $path"
        }
        $targetProperty = $item.PSObject.Properties['Target']
        if ($null -eq $targetProperty -or @($targetProperty.Value).Count -eq 0) { throw "Cannot read launch-root junction target: $path" }
        $actualTarget = [IO.Path]::GetFullPath([string] @($targetProperty.Value)[0]).TrimEnd('\')
        $expectedTargets = @(
            [IO.Path]::GetFullPath((Join-Path 'S:\game' $name)).TrimEnd('\'),
            [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $script:PhysicalRoot) $name)).TrimEnd('\')
        )
        if ($expectedTargets -notcontains $actualTarget) { throw "Launch-root junction $path has an unexpected target: $actualTarget" }
    }

    foreach ($name in @('bin', 'bin_nexon', 'materials', 'models', 'sound', 'vgui', 'cfg', 'config', 'admin', 'scripts', 'resource', 'maps', 'shaders', 'bin_delta', 'crash', 'crashes')) {
        $path = if ($name -in @('crash', 'crashes')) { Join-Path $Root $name } else { Join-Path $CoreRoot $name }
        $item = Get-Item -LiteralPath $path -Force -ErrorAction Stop
        if (-not $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Refusing to use a non-private directory in $Root`: $path"
        }
    }
    foreach ($relative in @('r2port', 'r2port\cache', 'r2port_cache')) {
        $path = Join-Path $CoreRoot $relative
        if (-not (Test-Path -LiteralPath $path)) { continue }
        $item = Get-Item -LiteralPath $path -Force
        if (-not $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Refusing to use a non-private runtime directory in $Root`: $path"
        }
    }
}

function Start-R2Game {
    if (-not (Test-Path -LiteralPath (Join-Path $Root 'r1delta.exe') -PathType Leaf)) {
        throw "R2 root is not staged; missing $(Join-Path $Root 'r1delta.exe'). Run .build\r2-root.ps1 -Stage -Root $RootName first."
    }
    if (-not (Test-Path -LiteralPath $CoreRoot -PathType Container)) { throw "Missing R2 CORE: $CoreRoot" }
    if ([DateTimeOffset]::UtcNow -ge $script:RunDeadline) { $script:TimedOut = $true; return }

    $consoleArgument = @()
    if (-not $script:SuppressConsole) { $consoleArgument = @('-console') }
    switch ($Mode) {
        'Client' {
            $executable = Join-Path $Root 'Titanfall.exe'
            $workingDirectory = $ClientWorkingDirectory
            $gameArgs = @('-multiple', '-allowmultiple') + $consoleArgument + @('-dev', '-novid', '-windowed', '-noborder', '+clientport', [string] $script:SelectedPorts['ClientUdp'])
        }
        'Listen' {
            $serverPort = [string] $script:SelectedPorts['ServerUdp']
            $clientPortValue = [string] $script:SelectedPorts['ClientUdp']
            $executable = Join-Path $Root 'Titanfall.exe'
            $workingDirectory = $ClientWorkingDirectory
            $gameArgs = @('-multiple', '-allowmultiple') + $consoleArgument + @('-dev', '-novid', '-windowed', '-noborder', '-port', $serverPort, '+hostport', $serverPort, '+clientport', $clientPortValue, '+map', $Map)
        }
        'Dedicated' {
            $serverPort = [string] $script:SelectedPorts['ServerUdp']
            $executable = Join-Path $Root 'R1Delta_DS.exe'
            $workingDirectory = $Root
            $gameArgs = $consoleArgument + @('-dev', '-novid', '-port', $serverPort, '+hostport', $serverPort, '+map', $Map)
            if ($R1OFakeDedicated) { $gameArgs += '-r1o_dedi' }
        }
    }
    if ($script:SelectedPorts.Contains('McpTcp')) { $gameArgs += @('-usemcp', '+mcp_start_http', [string] $script:SelectedPorts['McpTcp']) }
    # Default for every agent launch: 60 fps cap and no modal message boxes (the engine adds -nosound -nomouse and the cl_mouse cvars itself in background mode).
    $gameArgs += @('+fps_max', '60', '-nomessagebox')
    if ($FatalScriptErrors) { $gameArgs += @('+fatal_script_errors', '1') }
    if ($GameArguments.Count -gt 0) { $gameArgs += $GameArguments }
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) { throw "Missing game executable: $executable" }
    if (-not (Test-Path -LiteralPath $workingDirectory -PathType Container)) { throw "Missing working directory: $workingDirectory" }
    $script:ExpectedGamePath = [IO.Path]::GetFullPath($executable)
    Write-Host "Launch options: console=$(-not $script:SuppressConsole); window=$($script:SelectedWindowStyle)"

    $oldPath = $env:PATH
    $oldFromLauncher = $env:FROM_DELTA_LAUNCHER
    $env:FROM_DELTA_LAUNCHER = '1'
    # Background test mode (docs/r2port.md): the window never takes focus, input or topmost, fps is capped, and no modal box or notepad appears.
    $oldBackground = $env:R1DELTA_BACKGROUND
    $env:R1DELTA_BACKGROUND = '1'
    $env:PATH = "$CoreRoot\bin_delta;$CoreRoot\bin;$ClientWorkingDirectory\bin\x64_retail;$ClientWorkingDirectory\r1\bin\x64_retail;$oldPath"
    $logPath = ''
    try {
        $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
        New-Item -ItemType Directory -Path $LogDirectory -Force | Out-Null
        $logPath = Join-Path $LogDirectory "r2-$($Owner.ToLowerInvariant())-$($Mode.ToLowerInvariant())-$Map-$stamp-$PID.cdb.log"
        if ($Cdb) {
            if (-not (Test-Path -LiteralPath $CdbPath -PathType Leaf)) { throw "CDB not found: $CdbPath" }
            if (-not (Test-Path -LiteralPath $CdbCommandFile -PathType Leaf)) { throw "CDB command file not found: $CdbCommandFile" }
            $debuggerArgs = @('-g', '-G', '-cf', $CdbCommandFile, '-logo', $logPath, $executable) + $gameArgs
            $launcher = Start-Process -FilePath $CdbPath -ArgumentList (ConvertTo-ArgumentString $debuggerArgs) -WorkingDirectory $workingDirectory -WindowStyle Hidden -PassThru
            $script:OwnedLauncherPid = [int] $launcher.Id
            $script:ExpectedLauncherPath = [IO.Path]::GetFullPath($CdbPath)
            Add-RunProcess $script:OwnedLauncherPid
            $gameProcess = $null
            while ([DateTimeOffset]::UtcNow -lt $script:RunDeadline -and $null -eq $gameProcess) {
                $matches = @(Find-GameChild $script:OwnedLauncherPid $script:ExpectedGamePath)
                if ($matches.Count -gt 0) { $gameProcess = $matches[0] }
                elseif ($null -eq (Get-ProcessRecord $script:OwnedLauncherPid)) { break }
                else { Start-Sleep -Milliseconds 250 }
            }
            if ($null -eq $gameProcess) {
                if ([DateTimeOffset]::UtcNow -ge $script:RunDeadline) { $script:TimedOut = $true; return }
                throw "CDB did not start the expected game executable under $Root; see $logPath"
            }
            $script:OwnedGamePid = [int] $gameProcess.ProcessId
            $script:ExpectedGameProcessPath = [IO.Path]::GetFullPath([string] $gameProcess.ExecutablePath)
            Add-RunProcess $script:OwnedGamePid
        } else {
            $launcher = Start-Process -FilePath $script:ExpectedGamePath -ArgumentList (ConvertTo-ArgumentString $gameArgs) -WorkingDirectory $workingDirectory -WindowStyle $script:SelectedWindowStyle -PassThru
            $script:OwnedLauncherPid = [int] $launcher.Id
            $script:ExpectedLauncherPath = $script:ExpectedGamePath
            Add-RunProcess $script:OwnedLauncherPid
            $gameProcess = Get-ProcessRecord $script:OwnedLauncherPid
            if ($null -eq $gameProcess -or -not $gameProcess.ExecutablePath -or -not (Test-R2ExecutablePath $gameProcess.ExecutablePath $script:ExpectedGamePath)) {
                $observedPath = if ($null -ne $gameProcess) { $gameProcess.ExecutablePath } else { 'not running' }
                throw "Started PID $($script:OwnedLauncherPid), but its executable path '$observedPath' is not the requested executable under $Root."
            }
            $script:OwnedGamePid = $script:OwnedLauncherPid
            $script:ExpectedGameProcessPath = [IO.Path]::GetFullPath([string] $gameProcess.ExecutablePath)
        }

        Write-Host "Owner: $Owner; root: $RootName; mode: $Mode; map: $Map; game pid: $($script:OwnedGamePid); executable path: $($gameProcess.ExecutablePath)"
        Write-Host "Working directory: $workingDirectory; bounded seconds from run start: $Seconds"
        if ($Cdb) { Write-Host "CDB log: $logPath" }
        while ($true) {
            if ([DateTimeOffset]::UtcNow -ge $script:RunDeadline) {
                $script:TimedOut = $true
                break
            }
            $current = Get-ProcessRecord $script:OwnedGamePid
            if ($null -eq $current) { break }
            $remainingMilliseconds = [int] [Math]::Ceiling(($script:RunDeadline - [DateTimeOffset]::UtcNow).TotalMilliseconds)
            if ($remainingMilliseconds -le 0) { $script:TimedOut = $true; break }
            Start-Sleep -Milliseconds ([Math]::Min(500, $remainingMilliseconds))
        }
    } finally {
        Stop-OwnedProcess $script:OwnedGamePid $script:ExpectedGameProcessPath
        if ($script:OwnedLauncherPid -gt 0 -and $script:OwnedLauncherPid -ne $script:OwnedGamePid) {
            Stop-OwnedProcess $script:OwnedLauncherPid $script:ExpectedLauncherPath
        }
        if ($script:TimedOut) { Write-Host "Run deadline reached at $Seconds seconds; stopped only PIDs launched by this script." }
        if ($Cdb -and $logPath) { Write-Host "CDB log: $logPath" }
        $env:PATH = $oldPath
        if ($null -eq $oldFromLauncher) { Remove-Item Env:\FROM_DELTA_LAUNCHER -ErrorAction SilentlyContinue } else { $env:FROM_DELTA_LAUNCHER = $oldFromLauncher }
        if ($null -eq $oldBackground) { Remove-Item Env:\R1DELTA_BACKGROUND -ErrorAction SilentlyContinue } else { $env:R1DELTA_BACKGROUND = $oldBackground }
    }
}

try {
    if (-not (Test-Path -LiteralPath $Root -PathType Container)) { throw "R2 root does not exist: $Root. Create and stage '$RootName' before running it." }
    Assert-RunRootLayout
    if ($R1OFakeDedicated -and $Mode -ne 'Dedicated') { throw '-R1OFakeDedicated is valid only with -Mode Dedicated.' }
    Initialize-PortRanges
    Assert-NoReservedGameArguments
    Register-Run
    if ([DateTimeOffset]::UtcNow -ge $script:RunDeadline) {
        $script:TimedOut = $true
    } else {
        Start-R2Game
    }
} finally {
    Remove-RunRegistration
    if ($script:TimedOut) { Write-Host "Window ended at the requested $Seconds-second bound." }
}
