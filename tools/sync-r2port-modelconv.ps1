$ErrorActionPreference = 'Stop'

$source = 'C:\Users\r3muxd\tmp\r2port\modelconv'
$destination = Join-Path $PSScriptRoot '..\engine\r2port\modelconv'
$files = @('modelconv.hpp', 'modelconv.cpp')
$originHeader = "// Origin: $source. R2 static model converter bundled in r1delta under GPL-3.0.`r`n"

New-Item -ItemType Directory -Force -Path $destination | Out-Null
foreach ($file in $files) {
    $sourcePath = Join-Path $source $file
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        throw "Converter source file is missing: $sourcePath"
    }

    $bytes = [System.IO.File]::ReadAllBytes($sourcePath)
    $header = [System.Text.Encoding]::UTF8.GetBytes($originHeader)
    $combined = [byte[]]::new($header.Length + $bytes.Length)
    [System.Buffer]::BlockCopy($header, 0, $combined, 0, $header.Length)
    [System.Buffer]::BlockCopy($bytes, 0, $combined, $header.Length, $bytes.Length)
    [System.IO.File]::WriteAllBytes((Join-Path $destination $file), $combined)
}
