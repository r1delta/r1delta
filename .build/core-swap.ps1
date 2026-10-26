# Superseded for per-root tests: each agent has an independent pinned CORE; use .build\r2-root.ps1 -Create/-Reset -Root r2-<agent>.
# Do not use this legacy swap flow against the user's S:\game\r2 play root.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Apply', 'Revert', 'Verify')]
    [string]$Action
)

$ErrorActionPreference = 'Stop'
$coreRoot = 'S:\game\r1delta'
$pinCommit = '316993bd0142e706ea5386943530f2b8d2c711dd'
$swapRoot = 'C:\Users\r3muxd\tmp\r2port\core-swap'
$backupRoot = Join-Path $swapRoot 'backup'
$activePath = Join-Path $swapRoot 'active.json'
$lockPath = 'C:\Users\r3muxd\tmp\r2port\GAME.lock'
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)

function Invoke-CoreGit([string[]]$GitArgs) {
    $output = & git -c core.quotepath=false -C $coreRoot @GitArgs 2>&1
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "git -C '$coreRoot' $($GitArgs -join ' ') failed ($exitCode): $($output -join [Environment]::NewLine)"
    }
    return @($output | ForEach-Object { [string]$_ })
}

function Test-LockOwner {
    if (-not (Test-Path -LiteralPath $lockPath -PathType Leaf)) {
        throw "GAME.lock is required for CORE swapping: $lockPath"
    }
    $lock = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json
    if ($lock.owner -ne 'ProviderHook') {
        throw "GAME.lock belongs to '$($lock.owner)', not ProviderHook."
    }
}

function Assert-NoGameProcesses {
    $names = @('Titanfall.exe', 'r1delta.exe', 'R1Delta_DS.exe', 'cdb.exe')
    $running = @(Get-CimInstance Win32_Process | Where-Object { $names -contains $_.Name })
    if ($running.Count) {
        $items = $running | ForEach-Object { "$($_.Name) pid=$($_.ProcessId) parent=$($_.ParentProcessId) started=$($_.CreationDate)" }
        throw "CORE swap requires no game/debugger processes: $($items -join '; ')"
    }
}

function Get-FileSha256([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return '' }
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Expected a file, found a non-file path: $Path" }
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-StatusLines {
    return @(Invoke-CoreGit @('status', '--porcelain', '--untracked-files=all', '--ignore-submodules=all'))
}

function Get-ObjectId([string]$Path) {
    $result = @(Invoke-CoreGit @('hash-object', '--no-filters', $Path))
    if ($result.Count -ne 1) { throw "Unexpected git hash-object result for $Path" }
    return $result[0].Trim()
}

function Write-Manifest($Manifest, [string]$Path) {
    $json = ConvertTo-Json -InputObject $Manifest -Depth 10
    [IO.File]::WriteAllText($Path, $json + [Environment]::NewLine, $utf8NoBom)
}

function Read-ActiveManifest {
    if (-not (Test-Path -LiteralPath $activePath -PathType Leaf)) { return $null }
    $manifestPath = [IO.File]::ReadAllText($activePath).Trim()
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "Active CORE swap manifest is missing: $manifestPath"
    }
    return [pscustomobject]@{ path = $manifestPath; data = (Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json) }
}

function Assert-RelativePath([string]$Path) {
    $normalized = $Path.Replace('\', '/')
    if ([string]::IsNullOrWhiteSpace($normalized) -or $normalized.StartsWith('/') -or ($normalized -split '/') -contains '..' -or $normalized.Contains('"')) {
        throw "Unsafe CORE path: $Path"
    }
    return $normalized
}

function Resolve-CorePath([string]$Path) {
    $normalized = Assert-RelativePath $Path
    $target = [IO.Path]::GetFullPath((Join-Path $coreRoot ($normalized.Replace('/', '\'))))
    $rootPrefix = [IO.Path]::GetFullPath($coreRoot).TrimEnd('\') + '\'
    if (-not $target.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "CORE path escapes the game directory: $Path"
    }
    return $target
}

function Get-SelectedPaths {
    $excluded = '^(maps|materials|models|r2port_cache)(/|$)'
    $diffPaths = Invoke-CoreGit @('diff', '--name-only', '--no-renames', $pinCommit, '--')
    $selected = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($path in $diffPaths) {
        $normalized = Assert-RelativePath $path
        if ($normalized -match $excluded) { continue }
        if ($normalized -eq 'playlists.txt' -or $normalized.StartsWith('resource/') -or $normalized.StartsWith('scripts/')) {
            [void]$selected.Add($normalized)
        } else {
            throw "Unclassified tracked CORE change; refusing to swap it: $normalized"
        }
    }
    $untracked = Invoke-CoreGit @('ls-files', '--others', '--exclude-standard', '--', 'scripts', 'resource')
    foreach ($path in $untracked) {
        $normalized = Assert-RelativePath $path
        if ($normalized.StartsWith('scripts/') -or $normalized.StartsWith('resource/')) {
            [void]$selected.Add($normalized)
        }
    }
    return @($selected | Sort-Object)
}

function Get-PinTree {
    $lines = Invoke-CoreGit @('ls-tree', '-r', $pinCommit)
    $objects = @{}
    foreach ($line in $lines) {
        $parts = $line -split "`t", 2
        if ($parts.Count -ne 2) { throw "Could not parse git ls-tree entry: $line" }
        $metadata = $parts[0] -split ' '
        if ($metadata.Count -lt 3 -or $metadata[1] -ne 'blob') { continue }
        $objects[$parts[1]] = $metadata[2]
    }
    return $objects
}

function Get-PinBlob([string]$ObjectId, [string]$Destination, [string]$WorkDirectory) {
    New-Item -ItemType Directory -Path (Split-Path -Parent $Destination) -Force | Out-Null
    $inputPath = Join-Path $WorkDirectory ($ObjectId + '.input')
    $batchPath = Join-Path $WorkDirectory ($ObjectId + '.batch')
    [IO.File]::WriteAllText($inputPath, $ObjectId + [Environment]::NewLine, [Text.Encoding]::ASCII)
    $command = 'git -C "' + $coreRoot + '" cat-file --batch < "' + $inputPath + '" > "' + $batchPath + '"'
    & $env:ComSpec /d /c $command
    if ($LASTEXITCODE -ne 0) { throw "git cat-file --batch failed with exit code $LASTEXITCODE for $ObjectId" }

    $source = $null
    $destinationStream = $null
    try {
        $source = [IO.File]::OpenRead($batchPath)
        $headerBytes = [System.Collections.Generic.List[byte]]::new()
        while ($true) {
            $value = $source.ReadByte()
            if ($value -lt 0) { throw "Missing git cat-file header for $ObjectId" }
            if ($value -eq 10) { break }
            if ($value -ne 13) { $headerBytes.Add([byte]$value) }
        }
        $header = [Text.Encoding]::ASCII.GetString($headerBytes.ToArray())
        $fields = $header -split ' '
        if ($fields.Count -ne 3 -or $fields[0] -ne $ObjectId -or $fields[1] -ne 'blob') {
            throw "Unexpected git cat-file header for $ObjectId`: $header"
        }
        $remaining = [long]::Parse($fields[2], [Globalization.CultureInfo]::InvariantCulture)
        $destinationStream = [IO.File]::Create($Destination)
        $buffer = New-Object byte[] 65536
        while ($remaining -gt 0) {
            $count = [int][Math]::Min($buffer.Length, $remaining)
            $read = $source.Read($buffer, 0, $count)
            if ($read -le 0) { throw "Truncated git cat-file payload for $ObjectId" }
            $destinationStream.Write($buffer, 0, $read)
            $remaining -= $read
        }
        $separator = $source.ReadByte()
        if ($separator -ne 10) { throw "Missing git cat-file payload separator for $ObjectId" }
    } finally {
        if ($destinationStream) { $destinationStream.Dispose() }
        if ($source) { $source.Dispose() }
    }
    Remove-Item -LiteralPath $inputPath, $batchPath -Force
}

function Apply-CorePin {
    Test-LockOwner
    Assert-NoGameProcesses
    if (Test-Path -LiteralPath $activePath) { throw "A CORE swap is already active: $activePath" }
    $rootOutput = @(Invoke-CoreGit @('rev-parse', '--show-toplevel'))
    if ($rootOutput.Count -ne 1) { throw 'Could not read CORE repository root.' }
    $root = ([string]$rootOutput[0]).Replace('/', '\').TrimEnd([char[]]@('\', '/'))
    $prefix = @(Invoke-CoreGit @('rev-parse', '--show-prefix'))
    if ($prefix.Count -gt 0 -and -not [string]::IsNullOrEmpty($prefix[0])) {
        throw "S:\game\r1delta is not the CORE repository root: $root"
    }
    foreach ($forbidden in @(
        'C:\Users\r3muxd\tmp\r1delta-release-20261002',
        'C:\Users\r3muxd\tmp\r1delta-r2port'
    )) {
        if ($root.Equals($forbidden, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to swap CORE content in protected code worktree: $root"
        }
    }
    Write-Host "CORE repository root resolved as $root"
    [void](Invoke-CoreGit @('cat-file', '-e', "$pinCommit^{commit}"))

    $paths = @(Get-SelectedPaths)
    if (-not $paths.Count) { throw 'No classified CORE paths differ from the pin.' }
    $tree = Get-PinTree
    $stamp = [DateTime]::UtcNow.ToString("yyyyMMdd'T'HHmmssfff'Z'")
    $backupDirectory = Join-Path $backupRoot $stamp
    $extractDirectory = Join-Path $backupDirectory 'pin-content'
    New-Item -ItemType Directory -Path $backupDirectory -Force | Out-Null
    New-Item -ItemType Directory -Path $extractDirectory -Force | Out-Null

    $entries = @()
    foreach ($path in $paths) {
        $target = Resolve-CorePath $path
        $beforePresent = Test-Path -LiteralPath $target -PathType Leaf
        if ((Test-Path -LiteralPath $target) -and -not $beforePresent) { throw "CORE target is not a file: $target" }
        $beforeHash = if ($beforePresent) { Get-FileSha256 $target } else { '' }
        $backupPath = ''
        if ($beforePresent) {
            $backupPath = Join-Path $backupDirectory ($path.Replace('/', '\'))
            New-Item -ItemType Directory -Path (Split-Path -Parent $backupPath) -Force | Out-Null
            [IO.File]::WriteAllBytes($backupPath, [IO.File]::ReadAllBytes($target))
            if ((Get-FileSha256 $backupPath) -ne $beforeHash) { throw "Backup hash mismatch for $path" }
        }

        $afterPresent = $tree.ContainsKey($path)
        $afterHash = ''
        $blobId = ''
        if ($afterPresent) {
            $pinFile = Join-Path $extractDirectory ($path.Replace('/', '\'))
            $blobId = $tree[$path]
            Get-PinBlob $blobId $pinFile $extractDirectory
            $afterHash = Get-FileSha256 $pinFile
            if ((Get-ObjectId $pinFile) -ne $blobId) { throw "Pin Git blob mismatch for $path" }
        }
        $entries += [pscustomobject]@{
            path = $path
            action = if ($afterPresent) { 'write' } else { 'delete' }
            beforePresent = $beforePresent
            beforeSha256 = $beforeHash
            backupPath = $backupPath
            afterPresent = $afterPresent
            afterSha256 = $afterHash
            pinBlob = $blobId
        }
    }

    $manifestPath = Join-Path $backupDirectory 'manifest.json'
    $manifest = [pscustomobject]@{
        schema = 1
        pinCommit = $pinCommit
        runtimeCore = $coreRoot
        backupDirectory = $backupDirectory
        appliedAtUtc = $stamp
        state = 'prepared'
        statusBefore = [string[]](Get-StatusLines)
        entries = $entries
    }
    Write-Manifest $manifest $manifestPath
    [IO.File]::WriteAllText($activePath, $manifestPath + [Environment]::NewLine, $utf8NoBom)

    foreach ($entry in $entries) {
        $target = Resolve-CorePath $entry.path
        if ($entry.afterPresent) {
            $pinFile = Join-Path $extractDirectory ($entry.path.Replace('/', '\'))
            New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
            [IO.File]::WriteAllBytes($target, [IO.File]::ReadAllBytes($pinFile))
            if ((Get-FileSha256 $target) -ne $entry.afterSha256 -or (Get-ObjectId $target) -ne $entry.pinBlob) {
                throw "Applied pin content verification failed for $($entry.path)"
            }
        } elseif (Test-Path -LiteralPath $target) {
            Remove-Item -LiteralPath $target -Force
        }
    }
    $manifest.state = 'applied'
    Write-Manifest $manifest $manifestPath
    Remove-Item -LiteralPath $extractDirectory -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host "Applied CORE pin $pinCommit to $($entries.Count) classified files. Backup: $backupDirectory"
}

function Revert-CorePin {
    Test-LockOwner
    Assert-NoGameProcesses
    $active = Read-ActiveManifest
    if (-not $active) { Write-Host 'No active CORE swap.'; return }
    $manifest = $active.data
    foreach ($entry in $manifest.entries) {
        $target = Resolve-CorePath $entry.path
        if ($entry.beforePresent) {
            if (-not (Test-Path -LiteralPath $entry.backupPath -PathType Leaf)) { throw "Missing CORE backup; preserving swap state: $($entry.backupPath)" }
            New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
            [IO.File]::WriteAllBytes($target, [IO.File]::ReadAllBytes($entry.backupPath))
            if ((Get-FileSha256 $target) -ne $entry.beforeSha256) { throw "CORE restore hash mismatch; backup retained: $($entry.path)" }
        } else {
            if (Test-Path -LiteralPath $target -PathType Leaf) { Remove-Item -LiteralPath $target -Force }
            if (Test-Path -LiteralPath $target) { throw "CORE restore expected absence but path remains: $($entry.path)" }
        }
    }
    $actualStatus = [string[]](Get-StatusLines)
    $expectedStatus = [string[]]@($manifest.statusBefore)
    if ([string]::Join("`n", $actualStatus) -cne [string]::Join("`n", $expectedStatus)) {
        throw "CORE restore status mismatch; backups retained at $($manifest.backupDirectory). Expected [$($expectedStatus -join ' | ')], got [$($actualStatus -join ' | ')]"
    }
    $manifest.state = 'reverted'
    Write-Manifest $manifest $active.path
    Remove-Item -LiteralPath $activePath -Force
    Write-Host "Restored CORE files and verified SHA-256 plus git status. Backup retained: $($manifest.backupDirectory)"
}

function Verify-CorePin {
    $active = Read-ActiveManifest
    if (-not $active) { Write-Host 'No active CORE swap.'; return }
    $manifest = $active.data
    foreach ($entry in $manifest.entries) {
        $target = Resolve-CorePath $entry.path
        if ($entry.afterPresent) {
            if ((Get-FileSha256 $target) -ne $entry.afterSha256 -or (Get-ObjectId $target) -ne $entry.pinBlob) {
                throw "Active CORE pin verification failed for $($entry.path)"
            }
        } elseif (Test-Path -LiteralPath $target) {
            throw "Active CORE pin expected deleted path: $($entry.path)"
        }
    }
    Write-Host "Active CORE pin $($manifest.pinCommit) verified for $($manifest.entries.Count) files. State: $($manifest.state)"
}

switch ($Action) {
    'Apply' { Apply-CorePin }
    'Revert' { Revert-CorePin }
    'Verify' { Verify-CorePin }
}
