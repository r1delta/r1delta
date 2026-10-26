$ErrorActionPreference = 'Stop'

$source = 'C:\Users\r3muxd\tmp\r2port\r2tex'
$destination = Join-Path $PSScriptRoot '..\engine\r2port\r2tex'
$files = @('r2tex.cpp', 'r2tex.hpp', 'rtech_decode.inc', 'bcdec.h', 'THIRD_PARTY_NOTICES.txt')
$originHeader = "// Origin: $source. R2 texture conversion library bundled with r1delta under GPL-3.0.`r`n// Local adaptation: family cache size is configurable with a 256 MiB default.`r`n"
$utf8 = [System.Text.UTF8Encoding]::new($false)

New-Item -ItemType Directory -Force -Path $destination | Out-Null
foreach ($file in $files) {
    $sourcePath = Join-Path $source $file
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        throw "TexLib source file is missing: $sourcePath"
    }

    $bytes = [System.IO.File]::ReadAllBytes($sourcePath)
    if ($file -eq 'r2tex.cpp' -or $file -eq 'r2tex.hpp') {
        $text = [System.Text.Encoding]::UTF8.GetString($bytes)
        if ($file -eq 'r2tex.cpp') {
            if (-not $text.Contains('#include <algorithm>')) {
                throw "Expected standard algorithm include was not found in $sourcePath"
            }
            if (-not $text.Contains('#include <atomic>')) {
                $text = $text.Replace('#include <algorithm>', "#include <algorithm>`r`n#include <atomic>")
            }

            $oldLimit = 'constexpr std::size_t kFamilyCacheLimit = 768ull * 1024ull * 1024ull;'
            $newLimit = "constexpr std::size_t kMaximumFamilyCacheLimit = 768ull * 1024ull * 1024ull;`r`nstd::atomic<std::size_t> gFamilyCacheLimit{256ull * 1024ull * 1024ull};"
            if (-not $text.Contains($oldLimit)) {
                throw "Expected family cache limit was not found in $sourcePath"
            }
            $text = $text.Replace($oldLimit, $newLimit)

            $oldCheck = 'while (familyBytes > kFamilyCacheLimit && lru.size() > 1) {'
            $newCheck = 'while (familyBytes > gFamilyCacheLimit.load(std::memory_order_relaxed) && lru.size() > 1) {'
            if (-not $text.Contains($oldCheck)) {
                throw "Expected family cache enforcement was not found in $sourcePath"
            }
            $text = $text.Replace($oldCheck, $newCheck)

            $openSignature = 'bool Open(const std::filesystem::path& installDir, const std::filesystem::path& cacheDir, std::string* error) {'
            $cacheSetter = @'
void SetFamilyCacheLimitMiB(std::uint32_t megabytes) noexcept {
    const auto maximumMiB = static_cast<std::uint32_t>(kMaximumFamilyCacheLimit / (1024ull * 1024ull));
    const auto boundedMiB = std::min(megabytes, maximumMiB);
    gFamilyCacheLimit.store(static_cast<std::size_t>(boundedMiB) * 1024ull * 1024ull,
                            std::memory_order_relaxed);
}

'@
            if (-not $text.Contains($openSignature)) {
                throw "Expected Open definition was not found in $sourcePath"
            }
            $text = $text.Replace($openSignature, $cacheSetter + $openSignature)
        }
        else {
            $convertSignature = 'bool ConvertTexture(std::uint64_t guid,'
            $setterDeclaration = 'void SetFamilyCacheLimitMiB(std::uint32_t megabytes) noexcept;' + "`r`n`r`n"
            if (-not $text.Contains($convertSignature)) {
                throw "Expected texture conversion declaration was not found in $sourcePath"
            }
            $text = $text.Replace($convertSignature, $setterDeclaration + $convertSignature)
        }

        if (-not $text.StartsWith('// Origin:')) {
            $text = $originHeader + $text
        }
        $bytes = $utf8.GetBytes($text)
    }
    [System.IO.File]::WriteAllBytes((Join-Path $destination $file), $bytes)
}
