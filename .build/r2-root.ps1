[CmdletBinding()]
param(
    [switch] $Create,
    [switch] $Verify,
    [switch] $Reset,
    [switch] $Stage,
    [switch] $Status
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$CodeRoot = Split-Path -Parent $PSScriptRoot
$GameRoot = 'S:\game'
$SourceLaunchRoot = Join-Path $GameRoot 'mp'
$SourceCoreRoot = Join-Path $GameRoot 'r1delta'
$Root = Join-Path $GameRoot 'r2'
$CoreRoot = Join-Path $Root 'r1delta'
$Pin = '316993bd0142e706ea5386943530f2b8d2c711dd'
$BuildRoot = Join-Path $PSScriptRoot 'release'
$VerifyScript = Join-Path $CodeRoot 'ci\verify_release_payload.ps1'
$LockPath = 'C:\Users\r3muxd\tmp\r2port\GAME.lock'
$GraphSourceRoot = 'C:\Users\r3muxd\tmp\r2port\ainconv\out'
$LargeCoreDirectories = @('bin', 'bin_nexon', 'materials', 'models', 'sound', 'vgui', 'cfg', 'config', 'admin')
$AlwaysCheckoutDirectories = @('scripts', 'resource', 'maps')
$SampleBlobPaths = @(
    'GameInfo.txt',
    'playlists.txt',
    'scripts/vscripts/_mapspawn.nut',
    'scripts/vscripts/mp/_control_panel.nut'
)
$GraphNames = @('mp_glitch.ain', 'mp_lf_stacks.ain')
$StubNames = @('d3d11.dll', 'GFSDK_SSAO.win64.dll', 'GFSDK_TXAA.win64.dll')
$RootExecutables = @('Titanfall.exe', 'R1Delta_DS.exe', 'r1delta.exe')
$script:MutationLockToken = $null

$selectedModes = @($Create, $Verify, $Reset, $Stage, $Status | Where-Object { $_ }).Count
if ($selectedModes -ne 1) {
    throw 'Select exactly one of -Create, -Verify, -Reset, -Stage, or -Status.'
}

function Invoke-Git([string] $Directory, [string[]] $GitArgs) {
    $previousErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $output = & git -c core.quotepath=false -C $Directory @GitArgs 2>&1
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    if ($exitCode -ne 0) {
        throw "git -C '$Directory' $($GitArgs -join ' ') failed ($exitCode): $($output -join [Environment]::NewLine)"
    }
    return @($output | ForEach-Object { [string] $_ })
}

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
        throw "Refusing to modify S:\game\r2 while processes from that root are running: $listing"
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

function Acquire-MutationLock([string] $Purpose) {
    Remove-StaleGameLock
    $token = [Guid]::NewGuid().ToString('N')
    $record = [ordered]@{
        owner = "r2-root.ps1 pid $PID"
        purpose = $Purpose
        started = [DateTimeOffset]::UtcNow.ToString('o')
        token = $token
        processIds = @([int] $PID)
    }
    $json = ConvertTo-Json -InputObject $record -Compress
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes($json + [Environment]::NewLine)
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
    $script:MutationLockToken = $token
}

function Release-MutationLock {
    if (-not $script:MutationLockToken) { return }
    if (-not (Test-Path -LiteralPath $LockPath -PathType Leaf)) { return }
    try {
        $lock = Get-Content -LiteralPath $LockPath -Raw | ConvertFrom-Json
        $tokenProperty = $lock.PSObject.Properties['token']
        if ($null -ne $tokenProperty -and [string] $tokenProperty.Value -eq $script:MutationLockToken) {
            Remove-Item -LiteralPath $LockPath -Force
        }
    } finally {
        $script:MutationLockToken = $null
    }
}

function Remove-TreeEntry([string] $Path) {
    $item = Get-Item -LiteralPath $Path -Force
    if (-not $item.PSIsContainer) {
        [IO.File]::Delete($item.FullName)
        return
    }
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        [IO.Directory]::Delete($item.FullName, $false)
        return
    }
    foreach ($child in Get-ChildItem -LiteralPath $item.FullName -Force) {
        Remove-TreeEntry $child.FullName
    }
    [IO.Directory]::Delete($item.FullName, $false)
}

function Get-CoreDirectoryPlan {
    $shared = [System.Collections.Generic.List[string]]::new()
    $checkout = [System.Collections.Generic.List[string]]::new()
    foreach ($name in $LargeCoreDirectories) {
        if (-not (Test-Path -LiteralPath (Join-Path $SourceCoreRoot $name) -PathType Container)) { continue }
        $tree = @(Invoke-Git $SourceCoreRoot @('ls-tree', '-d', '--name-only', $Pin, '--', $name))
        if ($tree -notcontains $name) {
            if ($name -eq 'config' -and @(Get-ChildItem -LiteralPath (Join-Path $SourceCoreRoot $name) -Force).Count -eq 0) {
                $shared.Add($name)
            }
            continue
        }
        $diff = @(Invoke-Git $SourceCoreRoot @('diff', '--stat', $Pin, '--', $name))
        $untracked = @(Invoke-Git $SourceCoreRoot @('ls-files', '--others', '--exclude-standard', '--', $name))
        if ($diff.Count -eq 0 -and $untracked.Count -eq 0) { $shared.Add($name) } else { $checkout.Add($name) }
    }
    return [pscustomobject]@{ Shared = @($shared.ToArray()); Checkout = @($checkout.ToArray()) }
}

function Get-RootStageNames {
    $names = [System.Collections.Generic.List[string]]::new()
    foreach ($name in $RootExecutables) { $names.Add($name) }
    $dedicatedDll = Join-Path $BuildRoot 'dedicated.dll'
    if (Test-Path -LiteralPath $dedicatedDll -PathType Leaf) { $names.Add('dedicated.dll') }
    return @($names.ToArray())
}

function New-DirectoryJunction([string] $Path, [string] $Target) {
    if (Test-Path -LiteralPath $Path) { throw "Refusing to replace an existing path with a junction: $Path" }
    New-Item -ItemType Junction -Path $Path -Target $Target | Out-Null
}

function Add-LocalExclude([string] $Core, [string[]] $Patterns) {
    $excludePath = Join-Path $Core '.git\info\exclude'
    $existing = @()
    if (Test-Path -LiteralPath $excludePath -PathType Leaf) {
        $existing = [IO.File]::ReadAllLines($excludePath)
    }
    $toAppend = @($Patterns | Where-Object { $existing -notcontains $_ })
    if ($toAppend.Count -eq 0) { return }
    $text = [Environment]::NewLine + ($toAppend -join [Environment]::NewLine) + [Environment]::NewLine
    [IO.File]::AppendAllText($excludePath, $text, [Text.UTF8Encoding]::new($false))
}

function Copy-DirectoryContents([string] $Source, [string] $Destination) {
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    foreach ($item in Get-ChildItem -LiteralPath $Source -Force) {
        Copy-Item -LiteralPath $item.FullName -Destination $Destination -Recurse -Force
    }
}

function Copy-FileAtomically([string] $Source, [string] $DestinationDirectory) {
    $destination = Join-Path $DestinationDirectory (Split-Path -Leaf $Source)
    $temporary = "$destination.$([Guid]::NewGuid().ToString('N')).r1dstaging"
    try {
        Copy-Item -LiteralPath $Source -Destination $temporary -Force
        Move-Item -LiteralPath $temporary -Destination $destination -Force
    } finally {
        if (Test-Path -LiteralPath $temporary -PathType Leaf) {
            Remove-Item -LiteralPath $temporary -Force
        }
    }
}

function New-R2Root {
    if (-not (Test-Path -LiteralPath $SourceLaunchRoot -PathType Container)) { throw "Missing source launch root: $SourceLaunchRoot" }
    if (-not (Test-Path -LiteralPath $SourceCoreRoot -PathType Container)) { throw "Missing source CORE clone: $SourceCoreRoot" }
    foreach ($name in $RootExecutables) {
        if (-not (Test-Path -LiteralPath (Join-Path $BuildRoot $name) -PathType Leaf)) {
            throw "Cannot build the R2 root before the Release executable exists: $(Join-Path $BuildRoot $name)"
        }
    }
    $resolvedPin = @(Invoke-Git $SourceCoreRoot @('rev-parse', '--verify', "${Pin}^{commit}"))
    if ($resolvedPin.Count -ne 1) { throw "Could not resolve pinned CORE commit $Pin." }
    $plan = Get-CoreDirectoryPlan
    $stageNames = Get-RootStageNames

    New-Item -ItemType Directory -Path $Root | Out-Null
    try {
        foreach ($file in Get-ChildItem -LiteralPath $SourceLaunchRoot -File -Force) {
            if ($stageNames -contains $file.Name) { continue }
            New-Item -ItemType HardLink -Path (Join-Path $Root $file.Name) -Target $file.FullName | Out-Null
        }
        foreach ($name in @('bin', 'r1', 'tools')) {
            New-DirectoryJunction (Join-Path $Root $name) (Join-Path $GameRoot $name)
        }
        foreach ($name in @('crash', 'crashes')) {
            New-Item -ItemType Directory -Path (Join-Path $Root $name) | Out-Null
        }

        Invoke-Git $Root @('clone', '--no-checkout', '--shared', $SourceCoreRoot, $CoreRoot) | Out-Null
        Invoke-Git $CoreRoot @('config', '--local', 'core.autocrlf', 'false') | Out-Null
        Invoke-Git $CoreRoot @('sparse-checkout', 'init', '--cone') | Out-Null
        $sparsePaths = @($AlwaysCheckoutDirectories) + @($plan.Checkout)
        Invoke-Git $CoreRoot (@('sparse-checkout', 'set', '--cone') + $sparsePaths) | Out-Null
        $available = $true
        try { Invoke-Git $CoreRoot @('rev-parse', '--verify', "${Pin}^{commit}") | Out-Null } catch { $available = $false }
        if (-not $available) { Invoke-Git $CoreRoot @('fetch', '--no-tags', $SourceCoreRoot, $Pin) | Out-Null }
        Invoke-Git $CoreRoot @('checkout', '--detach', $Pin) | Out-Null
        Invoke-Git $CoreRoot @('config', '--local', 'core.autocrlf', 'input') | Out-Null

        foreach ($name in $plan.Shared) {
            $destination = Join-Path $CoreRoot $name
            if (Test-Path -LiteralPath $destination) {
                Remove-TreeEntry $destination
            }
            New-DirectoryJunction $destination (Join-Path $SourceCoreRoot $name)
        }

        $binDeltaSource = Join-Path $SourceCoreRoot 'bin_delta'
        if (-not (Test-Path -LiteralPath $binDeltaSource -PathType Container)) { throw "Missing source bin_delta: $binDeltaSource" }
        Copy-DirectoryContents $binDeltaSource (Join-Path $CoreRoot 'bin_delta')

        $graphDirectory = Join-Path $CoreRoot 'maps\graphs'
        New-Item -ItemType Directory -Path $graphDirectory -Force | Out-Null
        foreach ($name in $GraphNames) {
            $source = Join-Path $GraphSourceRoot $name
            if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing test graph: $source" }
            Copy-Item -LiteralPath $source -Destination (Join-Path $graphDirectory $name) -Force
        }
        Add-LocalExclude $CoreRoot @('/bin_delta/', '/maps/graphs/mp_glitch.ain', '/maps/graphs/mp_lf_stacks.ain', '/r2port/cache/', '/r2port_cache/')

        foreach ($relativePath in $SampleBlobPaths) {
            $expected = @(Invoke-Git $CoreRoot @('rev-parse', '--verify', "${Pin}:$relativePath"))
            $actual = @(Invoke-Git $CoreRoot @('hash-object', '--no-filters', '--', (Join-Path $CoreRoot ($relativePath.Replace('/', '\')))))
            if ($expected.Count -ne 1 -or $actual.Count -ne 1 -or $actual[0] -ne $expected[0]) {
                throw "Pinned checkout hash mismatch for $relativePath (expected '$($expected -join ',')', got '$($actual -join ',')')."
            }
        }

        $status = @(Invoke-Git $CoreRoot @('status', '--porcelain', '--untracked-files=all'))
        if ($status.Count -ne 0) { throw "New pinned CORE clone is not clean: $($status -join '; ')" }
    } catch {
        if (Test-Path -LiteralPath $Root) { Remove-TreeEntry $Root }
        throw
    }

    Write-Host "Created $Root with CORE pin $Pin."
    Write-Host "CORE directories shared by junction: $(if ($plan.Shared.Count) { $plan.Shared -join ', ' } else { 'none' })."
    if ($plan.Checkout.Count) { Write-Host "Changed large directories checked out privately: $($plan.Checkout -join ', ')." }
}

function Get-LinkType($Item) {
    $property = $Item.PSObject.Properties['LinkType']
    if ($null -eq $property) { return '' }
    return [string] $property.Value
}

function Assert-Junction([string] $Path, [string] $Target) {
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0 -or (Get-LinkType $item) -ne 'Junction') {
        throw "Expected a junction at $Path."
    }
    $targetProperty = $item.PSObject.Properties['Target']
    if ($null -eq $targetProperty -or @($targetProperty.Value).Count -eq 0) { throw "Junction target cannot be read: $Path" }
    $actualTarget = [IO.Path]::GetFullPath([string] @($targetProperty.Value)[0]).TrimEnd('\')
    $expectedTarget = [IO.Path]::GetFullPath($Target).TrimEnd('\')
    if (-not $actualTarget.Equals($expectedTarget, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Junction $Path targets '$actualTarget', expected '$expectedTarget'."
    }
}

function Assert-RealDirectory([string] $Path) {
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if (-not $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Expected a real directory at $Path."
    }
}

function Assert-PinnedCheckout {
    if (-not (Test-Path -LiteralPath $Root -PathType Container)) { throw "R2 launch root does not exist: $Root" }
    Assert-RealDirectory $Root
    foreach ($name in @('bin', 'r1', 'tools')) { Assert-Junction (Join-Path $Root $name) (Join-Path $GameRoot $name) }
    foreach ($name in @('crash', 'crashes')) { Assert-RealDirectory (Join-Path $Root $name) }
    Assert-RealDirectory $CoreRoot
    Assert-RealDirectory (Join-Path $CoreRoot '.git')
    foreach ($name in $AlwaysCheckoutDirectories) { Assert-RealDirectory (Join-Path $CoreRoot $name) }

    $head = @(Invoke-Git $CoreRoot @('rev-parse', 'HEAD'))
    if ($head.Count -ne 1 -or $head[0] -ne $Pin) { throw "CORE HEAD is '$($head -join ',')', expected pinned commit $Pin." }
    $autocrlf = @(Invoke-Git $CoreRoot @('config', '--local', '--get', 'core.autocrlf'))
    if ($autocrlf.Count -ne 1 -or $autocrlf[0] -ne 'input') { throw "CORE clone core.autocrlf is '$($autocrlf -join ',')', expected input after a raw-byte checkout with autocrlf=false." }
    $commonDir = @(Invoke-Git $CoreRoot @('rev-parse', '--git-common-dir'))
    $commonPath = [IO.Path]::GetFullPath((Join-Path $CoreRoot $commonDir[0]))
    $corePrefix = [IO.Path]::GetFullPath($CoreRoot).TrimEnd('\') + '\'
    if (-not $commonPath.StartsWith($corePrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "CORE is not an independent clone; git common directory resolves outside ${CoreRoot}: $commonPath"
    }

    $plan = Get-CoreDirectoryPlan
    foreach ($name in $plan.Shared) { Assert-Junction (Join-Path $CoreRoot $name) (Join-Path $SourceCoreRoot $name) }
    foreach ($name in $plan.Checkout) { Assert-RealDirectory (Join-Path $CoreRoot $name) }
    foreach ($name in $GraphNames) {
        $source = Join-Path $GraphSourceRoot $name
        $target = Join-Path $CoreRoot "maps\graphs\$name"
        if (-not (Test-Path -LiteralPath $target -PathType Leaf)) { throw "Missing loose test graph: $target" }
        if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash) {
            throw "Loose graph differs from its generated source: $name"
        }
    }
    if (Test-Path -LiteralPath (Join-Path $CoreRoot 'scripts\vscripts\movement')) {
        throw 'Pinned CORE unexpectedly contains scripts/vscripts/movement.'
    }

    foreach ($relativePath in $SampleBlobPaths) {
        $expected = @(Invoke-Git $CoreRoot @('rev-parse', '--verify', "${Pin}:$relativePath"))
        $actual = @(Invoke-Git $CoreRoot @('hash-object', '--no-filters', '--', (Join-Path $CoreRoot ($relativePath.Replace('/', '\')))))
        if ($expected.Count -ne 1 -or $actual.Count -ne 1 -or $actual[0] -ne $expected[0]) {
            throw "Pinned checkout hash mismatch for $relativePath."
        }
    }

    Assert-RealDirectory (Join-Path $CoreRoot 'bin_delta')
    foreach ($name in @('CoherentUIGT.dll', 'materialsystem_nodx.dll', 'tier0.dll', 'vaudio_speex.dll')) {
        if (-not (Test-Path -LiteralPath (Join-Path (Join-Path $CoreRoot 'bin_delta') $name) -PathType Leaf)) {
            throw "CORE bin_delta is missing $name. Run -Stage after creating the root."
        }
    }
    $stubRoot = Join-Path $CoreRoot 'bin_delta\r1o_stubs'
    Assert-RealDirectory $stubRoot
    foreach ($name in $StubNames) {
        if (-not (Test-Path -LiteralPath (Join-Path $stubRoot $name) -PathType Leaf)) { throw "CORE r1o_stubs is missing $name." }
    }
    foreach ($name in @('Titanfall.exe', 'R1Delta_DS.exe', 'r1delta.exe', 'r1delta.exe.config')) {
        if (-not (Test-Path -LiteralPath (Join-Path $Root $name) -PathType Leaf)) { throw "Launch root is missing $name. Run -Stage after creating the root." }
    }
    $status = @(Invoke-Git $CoreRoot @('status', '--porcelain', '--untracked-files=all'))
    if ($status.Count -ne 0) { throw "Pinned CORE clone is not clean: $($status -join '; ')" }
    Write-Host "Verified ${Root}: independent clean CORE clone at $Pin; checkout was made with core.autocrlf=false and blob hashes match; local autocrlf=input keeps shared worktrees clean; test graphs installed."
    Write-Host "Shared CORE directories: $(if ($plan.Shared.Count) { $plan.Shared -join ', ' } else { 'none' })."
}

function Stage-R2Root {
    Assert-R2Idle
    Assert-RealDirectory $Root
    Assert-RealDirectory $CoreRoot
    foreach ($path in @($VerifyScript, (Join-Path $BuildRoot 'Titanfall.exe'), (Join-Path $BuildRoot 'R1Delta_DS.exe'), (Join-Path $BuildRoot 'r1delta.exe'))) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required staging input is missing: $path" }
    }
    $verifyCore = Join-Path $env:TEMP ("r2port-core-verify-" + [Guid]::NewGuid().ToString('N'))
    try {
        foreach ($directory in @('bin', 'bin_nexon')) {
            New-Item -ItemType Directory -Path (Join-Path $verifyCore $directory) -Force | Out-Null
        }
        foreach ($directory in @('bin', 'bin_nexon')) {
            $sourceDirectory = Join-Path $CoreRoot $directory
            $targetDirectory = Join-Path $verifyCore $directory
            Get-ChildItem -LiteralPath $sourceDirectory -File |
                Where-Object { $_.Extension -in @('.dll', '.exe') } |
                ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $targetDirectory $_.Name) }
        }
        Copy-Item -LiteralPath (Join-Path (Join-Path $CoreRoot 'bin_nexon') 'SHA256SUMS') -Destination (Join-Path (Join-Path $verifyCore 'bin_nexon') 'SHA256SUMS')
        & $VerifyScript -CorePath $verifyCore -BuildPath $BuildRoot
    } finally {
        if (Test-Path -LiteralPath $verifyCore) { Remove-Item -LiteralPath $verifyCore -Recurse -Force }
    }

    $binDelta = Join-Path $CoreRoot 'bin_delta'
    New-Item -ItemType Directory -Path $binDelta -Force | Out-Null
    foreach ($file in Get-ChildItem -LiteralPath $BuildRoot -File | Where-Object { $_.Extension -in @('.dll', '.png') }) {
        if ($StubNames -contains $file.Name) { continue }
        Copy-FileAtomically $file.FullName $binDelta
    }
    $stubSource = Join-Path $BuildRoot 'r1o_stubs'
    $stubDestination = Join-Path $binDelta 'r1o_stubs'
    New-Item -ItemType Directory -Path $stubDestination -Force | Out-Null
    foreach ($name in $StubNames) {
        $source = Join-Path $stubSource $name
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing R1O graphics stub: $source" }
        Copy-FileAtomically $source $stubDestination
    }
    foreach ($name in (Get-RootStageNames)) {
        $source = Join-Path $BuildRoot $name
        if (Test-Path -LiteralPath $source -PathType Leaf) { Copy-FileAtomically $source $Root }
    }
    Write-Host "Staged Release DLL/PNG payload into $binDelta and Release executables into $Root by copy-then-move."
}

function Get-PrivatePayloadBytes([string] $Path) {
    [long] $bytes = 0
    foreach ($item in Get-ChildItem -LiteralPath $Path -Force) {
        if ($item.PSIsContainer) {
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { continue }
            $bytes += Get-PrivatePayloadBytes $item.FullName
        } elseif ((Get-LinkType $item) -ne 'HardLink') {
            $bytes += [long] $item.Length
        }
    }
    return $bytes
}

function Show-R2Status {
    if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
        Write-Host "R2 root is not present: $Root"
        return
    }
    $bytes = Get-PrivatePayloadBytes $Root
    Write-Host "R2 root: $Root"
    Write-Host "Private non-hardlinked file payload (junction contents excluded): $([math]::Round($bytes / 1MB, 1)) MiB"
    foreach ($name in @('bin', 'r1', 'tools', 'r1delta')) {
        $path = Join-Path $Root $name
        if (Test-Path -LiteralPath $path) {
            $item = Get-Item -LiteralPath $path -Force
            $type = Get-LinkType $item
            if (-not $type) { $type = 'real directory' }
            Write-Host "  $name : $type -> $($item.Target -join ', ')"
        }
    }
    if (Test-Path -LiteralPath $CoreRoot) {
        $head = @(Invoke-Git $CoreRoot @('rev-parse', 'HEAD'))
        $status = @(Invoke-Git $CoreRoot @('status', '--short', '--untracked-files=all'))
        Write-Host "CORE HEAD: $($head -join ','); worktree: $(if ($status.Count -eq 0) { 'clean' } else { $status -join '; ' })"
        $plan = Get-CoreDirectoryPlan
        Write-Host "Shared CORE directories: $(if ($plan.Shared.Count) { $plan.Shared -join ', ' } else { 'none' })."
    }
}

try {
    if ($Create -or $Reset -or $Stage) {
        $purpose = if ($Create) { 'Create permanent R2-port root' } elseif ($Reset) { 'Reset permanent R2-port root' } else { 'Stage permanent R2-port root' }
        Acquire-MutationLock $purpose
    }
    if ($Create) {
        if (Test-Path -LiteralPath $Root) { throw "R2 root already exists. Use -Reset to recreate only $Root." }
        Assert-R2Idle
        New-R2Root
    } elseif ($Reset) {
        Assert-R2Idle
        if (Test-Path -LiteralPath $Root) {
            $rootItem = Get-Item -LiteralPath $Root -Force
            if (-not $rootItem.PSIsContainer -or ($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Refusing to reset a non-directory or reparse-point root: $Root"
            }
            Remove-TreeEntry $Root
        }
        New-R2Root
    } elseif ($Stage) {
        Stage-R2Root
    } elseif ($Verify) {
        Assert-PinnedCheckout
    } else {
        Show-R2Status
    }
} finally {
    Release-MutationLock
}
