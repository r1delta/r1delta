# Shared by ci/new_content_manifest.ps1 (generator) and ci/verify_release_payload.ps1 (validator).
#
# r1delta/content_manifest.txt tells the game which script, menu and native files R1Delta shipped,
# so the script-error telemetry can tell a hand-modified install from a stock one
# (engine/net/script_modding.h parses the same format):
#
#   r1delta-content-manifest 1
#   S <sha256> <path>   scripts/vscripts/** and resource/**/*.res|*.menu
#   N <sha256> <path>   *.dll under bin, bin_nexon and bin_delta
#
# Paths are relative to the r1delta directory, lowercase, '/' separated; hashes are lowercase hex.

Set-StrictMode -Version Latest

$script:ContentManifestHeader = 'r1delta-content-manifest 1'
$script:ContentManifestFileName = 'content_manifest.txt'

function Get-ContentManifestRelativePath {
    param([string] $Root, [string] $FullName)

    $prefix = $Root.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    if (-not $FullName.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "File is outside the content root: $FullName"
    }
    return $FullName.Substring($prefix.Length).Replace('\', '/').ToLowerInvariant()
}

# The files the manifest covers, as @{ Kind; Path; FullName }, sorted by kind then ordinal path.
function Get-ContentManifestFiles {
    param([Parameter(Mandatory = $true)] [string] $Root)

    $rootPath = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    $entries = New-Object System.Collections.Generic.List[object]

    $scriptsRoot = Join-Path $rootPath 'scripts\vscripts'
    if (-not (Test-Path -LiteralPath $scriptsRoot -PathType Container)) {
        throw "Content root has no scripts/vscripts directory: $rootPath"
    }
    foreach ($file in Get-ChildItem -LiteralPath $scriptsRoot -Recurse -File) {
        $entries.Add(@{ Kind = 'S'; Path = (Get-ContentManifestRelativePath $rootPath $file.FullName); FullName = $file.FullName })
    }

    $resourceRoot = Join-Path $rootPath 'resource'
    if (Test-Path -LiteralPath $resourceRoot -PathType Container) {
        foreach ($file in Get-ChildItem -LiteralPath $resourceRoot -Recurse -File |
                Where-Object { $_.Extension -in @('.res', '.menu') }) {
            $entries.Add(@{ Kind = 'S'; Path = (Get-ContentManifestRelativePath $rootPath $file.FullName); FullName = $file.FullName })
        }
    }

    foreach ($directory in @('bin', 'bin_nexon', 'bin_delta')) {
        $moduleRoot = Join-Path $rootPath $directory
        if (-not (Test-Path -LiteralPath $moduleRoot -PathType Container)) {
            continue
        }
        # Not -Filter '*.dll': a three-letter filter also matches longer extensions through 8.3 names.
        foreach ($file in Get-ChildItem -LiteralPath $moduleRoot -Recurse -File |
                Where-Object { $_.Extension -ieq '.dll' }) {
            $entries.Add(@{ Kind = 'N'; Path = (Get-ContentManifestRelativePath $rootPath $file.FullName); FullName = $file.FullName })
        }
    }

    $seen = @{}
    foreach ($entry in $entries) {
        $key = $entry.Kind + ' ' + $entry.Path
        if ($seen.ContainsKey($key)) {
            throw "Two files differ only by case: $($entry.Path)"
        }
        $seen[$key] = $true
    }

    return @($entries | Sort-Object -Property @{ Expression = { $_.Kind } }, @{ Expression = { $_.Path } })
}

function Get-ContentManifestHash {
    param([string] $FullName)

    return (Get-FileHash -LiteralPath $FullName -Algorithm SHA256).Hash.ToLowerInvariant()
}

# Parses and validates a manifest file; returns @{ S = @{ path = hash }; N = @{ path = hash } }.
function Read-ContentManifest {
    param([Parameter(Mandatory = $true)] [string] $Path)

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
        throw 'content manifest must not start with a UTF-8 byte order mark.'
    }
    $lines = [System.Text.Encoding]::UTF8.GetString($bytes).Split("`n")

    $result = @{ S = @{}; N = @{} }
    $lineNumber = 0
    $sawHeader = $false
    foreach ($rawLine in $lines) {
        ++$lineNumber
        $line = $rawLine.TrimEnd("`r")
        if ($line.Length -eq 0) {
            continue
        }
        if (-not $sawHeader) {
            if ($line -ne $script:ContentManifestHeader) {
                throw "content manifest header must be '$($script:ContentManifestHeader)'."
            }
            $sawHeader = $true
            continue
        }
        if ($line -cnotmatch '^([SN]) ([0-9a-f]{64}) (.+)$') {
            throw "Malformed content manifest line ${lineNumber}: $line"
        }
        $kind = $Matches[1]
        $hash = $Matches[2]
        $entryPath = $Matches[3]
        if ($entryPath -cmatch '[A-Z\\:]' -or $entryPath.StartsWith('/') -or ($entryPath -split '/') -contains '..' -or ($entryPath -split '/') -contains '.' -or ($entryPath -split '/') -contains '') {
            throw "Content manifest path is not a clean lowercase relative path (line ${lineNumber}): $entryPath"
        }
        if ($result[$kind].ContainsKey($entryPath)) {
            throw "Duplicate content manifest entry: $entryPath"
        }
        $result[$kind][$entryPath] = $hash
    }
    if (-not $sawHeader) {
        throw 'content manifest is empty.'
    }
    return $result
}

function Format-ContentManifest {
    param([Parameter(Mandatory = $true)] [object[]] $Entries)

    $builder = New-Object System.Text.StringBuilder
    [void] $builder.Append($script:ContentManifestHeader).Append("`n")
    foreach ($entry in $Entries) {
        [void] $builder.Append($entry.Kind).Append(' ').Append($entry.Hash).Append(' ').Append($entry.Path).Append("`n")
    }
    return $builder.ToString()
}
