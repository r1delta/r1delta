[CmdletBinding()]
param(
    # An r1delta content directory: the payload's r1delta folder in CI, or a local game root's r1delta folder.
    [Parameter(Mandatory = $true)]
    [string] $RootPath,

    # Defaults to <RootPath>/content_manifest.txt, where the game looks for it.
    [string] $OutputPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'content_manifest_lib.ps1')

$root = [System.IO.Path]::GetFullPath($RootPath).TrimEnd('\', '/')
if (-not $OutputPath) {
    $OutputPath = Join-Path $root $script:ContentManifestFileName
}

$entries = foreach ($file in Get-ContentManifestFiles -Root $root) {
    @{ Kind = $file.Kind; Path = $file.Path; Hash = (Get-ContentManifestHash $file.FullName) }
}
$entries = @($entries)
if (@($entries | Where-Object { $_.Kind -eq 'S' }).Count -eq 0) {
    throw "No script content found under $root."
}

# UTF-8 without a byte order mark, LF line endings: the game parses the bytes as they are.
[System.IO.File]::WriteAllText($OutputPath, (Format-ContentManifest $entries), (New-Object System.Text.UTF8Encoding($false)))

$scriptCount = @($entries | Where-Object { $_.Kind -eq 'S' }).Count
$moduleCount = @($entries | Where-Object { $_.Kind -eq 'N' }).Count
Write-Host "Wrote $OutputPath ($scriptCount content files, $moduleCount modules)."
