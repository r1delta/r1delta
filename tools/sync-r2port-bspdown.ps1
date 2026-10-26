$ErrorActionPreference = 'Stop'

$source = 'C:\Users\r3muxd\tmp\r2port\bspdown'
$destination = Join-Path $PSScriptRoot '..\engine\r2port\bspdown'
$files = @(
    'bspdown.hpp',
    'internal.hpp',
    'bspdown.cpp',
    'tricoll.cpp',
    'collision.cpp',
    'sprp.cpp',
    'meshclip.cpp',
    'tangents.cpp',
    'physics.cpp',
    'cubemaps.cpp',
    'bcdec.h'
)
$originHeader = "// Origin: $source. Bundled in r1delta under GPL-3.0.`r`n"

New-Item -ItemType Directory -Force -Path $destination | Out-Null
foreach ($file in $files) {
    $sourcePath = Join-Path $source $file
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        throw "Converter source file is missing: $sourcePath"
    }

    $bytes = [System.IO.File]::ReadAllBytes($sourcePath)
    if ($file -ne 'bcdec.h') {
        $header = [System.Text.Encoding]::UTF8.GetBytes($originHeader)
        $combined = [byte[]]::new($header.Length + $bytes.Length)
        [System.Buffer]::BlockCopy($header, 0, $combined, 0, $header.Length)
        [System.Buffer]::BlockCopy($bytes, 0, $combined, $header.Length, $bytes.Length)
        $bytes = $combined
    }
    [System.IO.File]::WriteAllBytes((Join-Path $destination $file), $bytes)
}
