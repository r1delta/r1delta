$ErrorActionPreference = 'Stop'

$source = 'C:\Users\r3muxd\tmp\r2port\physgen\r2patch_reader.h'
$destination = Join-Path $PSScriptRoot '..\engine\r2port\patch\r2patch_reader.h'
if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
    throw "Physics patch reader source is missing: $source"
}

$origin = "// Origin: $source, first-party R1Delta patch reader supplied with physgen.`r`n"
$sourceBytes = [System.IO.File]::ReadAllBytes($source)
$sourceText = [System.Text.Encoding]::UTF8.GetString($sourceBytes)
if (-not $sourceText.StartsWith('// Origin:')) {
    $sourceText = $origin + $sourceText
}
$destinationDirectory = Split-Path -Parent $destination
New-Item -ItemType Directory -Force -Path $destinationDirectory | Out-Null
[System.IO.File]::WriteAllText($destination, $sourceText, [System.Text.UTF8Encoding]::new($false))
