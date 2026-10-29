$ErrorActionPreference = 'Stop'

$source = 'C:\msys64\home\r3muxd\revpk-mutated\lzham_alpha'
$destination = Join-Path $PSScriptRoot '..\engine\r2port\lzham'
$includeFiles = @(
    'lzham_dynamic_lib.h',
    'lzham_static_lib.h',
    'lzham.h',
    'lzham_exports.inc',
    'zlib.h'
)
$decompFiles = @(
    'lzham_assert.cpp', 'lzham_assert.h',
    'lzham_checksum.cpp', 'lzham_checksum.h',
    'lzham_config.h', 'lzham_core.h', 'lzham_decomp.h', 'lzham_helpers.h',
    'lzham_huffman_codes.cpp', 'lzham_huffman_codes.h',
    'lzham_lzdecompbase.cpp', 'lzham_lzdecompbase.h', 'lzham_lzdecomp.cpp',
    'lzham_math.h', 'lzham_mem.cpp', 'lzham_mem.h',
    'lzham_platform.cpp', 'lzham_platform.h',
    'lzham_polar_codes.cpp', 'lzham_polar_codes.h',
    'lzham_prefix_coding.cpp', 'lzham_prefix_coding.h',
    'lzham_symbol_codec.cpp', 'lzham_symbol_codec.h',
    'lzham_timer.cpp', 'lzham_timer.h',
    'lzham_traits.h', 'lzham_types.h', 'lzham_utils.h',
    'lzham_vector.cpp', 'lzham_vector.h'
)
$originHeader = "// Origin: LZHAM Alpha 8, copied from $source. Copyright Richard Geldreich, Jr.; MIT license in ../LICENSE.txt.`r`n"

New-Item -ItemType Directory -Force -Path (Join-Path $destination 'include') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $destination 'lzhamdecomp') | Out-Null
foreach ($file in $includeFiles) {
    $sourcePath = Join-Path (Join-Path $source 'include') $file
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        throw "LZHAM source file is missing: $sourcePath"
    }
    $bytes = [System.IO.File]::ReadAllBytes($sourcePath)
    $header = [System.Text.Encoding]::UTF8.GetBytes($originHeader)
    $combined = [byte[]]::new($header.Length + $bytes.Length)
    [System.Buffer]::BlockCopy($header, 0, $combined, 0, $header.Length)
    [System.Buffer]::BlockCopy($bytes, 0, $combined, $header.Length, $bytes.Length)
    [System.IO.File]::WriteAllBytes((Join-Path (Join-Path $destination 'include') $file), $combined)
}
foreach ($file in $decompFiles) {
    $sourcePath = Join-Path (Join-Path $source 'lzhamdecomp') $file
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        throw "LZHAM source file is missing: $sourcePath"
    }
    $bytes = [System.IO.File]::ReadAllBytes($sourcePath)
    $fileOriginHeader = $originHeader
    if ($file -eq 'lzham_core.h') {
        $text = [System.Text.Encoding]::UTF8.GetString($bytes)
        $oldInclude = '#include <memory.h>'
        if (-not $text.Contains($oldInclude)) {
            throw "Expected LZHAM memory.h include was not found in $sourcePath"
        }
        $text = $text.Replace($oldInclude, '#include <string.h>')
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($text)
        $fileOriginHeader += "// Local change: use string.h because the project include path shadows memory.h with engine/memory/memory.h.`r`n"
    }
    $header = [System.Text.Encoding]::UTF8.GetBytes($fileOriginHeader)
    $combined = [byte[]]::new($header.Length + $bytes.Length)
    [System.Buffer]::BlockCopy($header, 0, $combined, 0, $header.Length)
    [System.Buffer]::BlockCopy($bytes, 0, $combined, $header.Length, $bytes.Length)
    [System.IO.File]::WriteAllBytes((Join-Path (Join-Path $destination 'lzhamdecomp') $file), $combined)
}
Copy-Item -LiteralPath (Join-Path $source 'license.txt') -Destination (Join-Path $destination 'LICENSE.txt') -Force
