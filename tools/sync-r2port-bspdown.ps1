$ErrorActionPreference = 'Stop'

$source = 'C:\Users\r3muxd\tmp\r2port\bspdown'
$destination = Join-Path $PSScriptRoot '..\engine\r2port\bspdown'

# Compiled into tier0.dll (Dll1.vcxproj). Each gets an origin comment line.
$sourceFiles = @(
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
    'shadow.cpp'
)
# Vendored for the command-line tool and its checks; not part of Dll1.vcxproj.
# main.cpp is the bspdown.exe driver (build_msvc.bat), check_bspdown.py (with its frame_check.py helper) verifies the output, README.txt lists the rules.
$toolSourceFiles = @('main.cpp')
$verbatimFiles = @('bcdec.h', 'check_bspdown.py', 'frame_check.py', 'README.txt', 'build_msvc.bat')
$originHeader = "// Origin: $source. Bundled in r1delta under GPL-3.0.`r`n"

New-Item -ItemType Directory -Force -Path $destination | Out-Null
foreach ($file in $sourceFiles + $toolSourceFiles + $verbatimFiles) {
    $sourcePath = Join-Path $source $file
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        throw "Converter source file is missing: $sourcePath"
    }

    $bytes = [System.IO.File]::ReadAllBytes($sourcePath)
    if ($verbatimFiles -notcontains $file) {
        $header = [System.Text.Encoding]::UTF8.GetBytes($originHeader)
        $combined = [byte[]]::new($header.Length + $bytes.Length)
        [System.Buffer]::BlockCopy($header, 0, $combined, 0, $header.Length)
        [System.Buffer]::BlockCopy($bytes, 0, $combined, $header.Length, $bytes.Length)
        $bytes = $combined
    }
    [System.IO.File]::WriteAllBytes((Join-Path $destination $file), $bytes)
}
