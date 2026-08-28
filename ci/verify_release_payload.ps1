[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $CorePath,

    [string] $BuildPath,

    [string] $PayloadPath,

    [switch] $PackagedPayload
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-Condition {
    param(
        [bool] $Condition,
        [string] $Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

function Get-NormalizedFullPath {
    param([string] $Path)

    return [System.IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
}


function Assert-CoreRuntime {
    param([string] $Root)

    $coreRoot = Get-NormalizedFullPath $Root
    Assert-Condition (Test-Path -LiteralPath $coreRoot -PathType Container) "CORE path does not exist: $coreRoot"

    $nexonPath = Join-Path $coreRoot 'bin_nexon'
    $baseBinPath = Join-Path $coreRoot 'bin'
    $manifestPath = Join-Path $nexonPath 'SHA256SUMS'
    Assert-Condition (Test-Path -LiteralPath $nexonPath -PathType Container) "Missing CORE bin_nexon directory."
    Assert-Condition (Test-Path -LiteralPath $baseBinPath -PathType Container) "Missing CORE bin directory."
    Assert-Condition (Test-Path -LiteralPath $manifestPath -PathType Leaf) "Missing bin_nexon/SHA256SUMS."

    $expectedNexonFiles = @(
        'datacache.dll',
        'filesystem_stdio.dll',
        'GFSDK_SSAO.win64.dll',
        'GFSDK_TXAA.win64.dll',
        'inputsystem.dll',
        'launcher.dll',
        'localize.dll',
        'materialsystem_dx11.dll',
        'studiorender.dll',
        'vguimatsurface.dll',
        'vphysics.dll'
    )
    $forbiddenNexonFiles = @(
        'BugTrap-x64.dll',
        'GBClient.dll',
        'msvcp100.dll',
        'msvcr100.dll',
        'server_local.dll',
        'vgui2.dll'
    )
    $expectedManifestEntries = @(
        '../bin/server.dll',
        '../bin/gbclient.dll',
        'datacache.dll',
        'filesystem_stdio.dll',
        'GFSDK_SSAO.win64.dll',
        'GFSDK_TXAA.win64.dll',
        'inputsystem.dll',
        'launcher.dll',
        'localize.dll',
        'materialsystem_dx11.dll',
        'studiorender.dll',
        'vguimatsurface.dll',
        'vphysics.dll'
    )

    $actualNexonFiles = @(
        Get-ChildItem -LiteralPath $nexonPath -File |
            Where-Object Name -ne 'SHA256SUMS' |
            ForEach-Object Name
    )
    $unexpectedDirectories = @(Get-ChildItem -LiteralPath $nexonPath -Directory)
    Assert-Condition ($unexpectedDirectories.Count -eq 0) "bin_nexon must not contain subdirectories."
    Assert-Condition ($actualNexonFiles.Count -eq $expectedNexonFiles.Count) (
        "bin_nexon contains $($actualNexonFiles.Count) runtime files; expected $($expectedNexonFiles.Count)."
    )

    foreach ($fileName in $expectedNexonFiles) {
        Assert-Condition ($actualNexonFiles -icontains $fileName) "bin_nexon is missing $fileName."
    }
    foreach ($fileName in $forbiddenNexonFiles) {
        Assert-Condition (-not ($actualNexonFiles -icontains $fileName)) (
            "bin_nexon contains forbidden duplicate/runtime file $fileName."
        )
    }

    $manifest = @{}
    $lineNumber = 0
    foreach ($line in Get-Content -LiteralPath $manifestPath) {
        ++$lineNumber
        if ([string]::IsNullOrWhiteSpace($line)) {
            continue
        }
        if ($line -notmatch '^([0-9A-Fa-f]{64})  (.+)$') {
            throw "Malformed SHA256SUMS line ${lineNumber}: $line"
        }

        $relativePath = $Matches[2].Replace('\', '/')
        Assert-Condition (-not $manifest.ContainsKey($relativePath)) "Duplicate SHA256SUMS entry: $relativePath"
        $manifest[$relativePath] = $Matches[1].ToUpperInvariant()
    }

    Assert-Condition ($manifest.Count -eq $expectedManifestEntries.Count) (
        "SHA256SUMS contains $($manifest.Count) entries; expected $($expectedManifestEntries.Count)."
    )

    $corePrefix = $coreRoot + [System.IO.Path]::DirectorySeparatorChar
    foreach ($relativePath in $expectedManifestEntries) {
        Assert-Condition ($manifest.ContainsKey($relativePath)) "SHA256SUMS is missing $relativePath."

        $nativeRelativePath = $relativePath.Replace('/', [System.IO.Path]::DirectorySeparatorChar)
        $targetPath = Get-NormalizedFullPath (Join-Path $nexonPath $nativeRelativePath)
        Assert-Condition (
            $targetPath.StartsWith($corePrefix, [System.StringComparison]::OrdinalIgnoreCase)
        ) "SHA256SUMS entry escapes CORE: $relativePath"
        Assert-Condition (Test-Path -LiteralPath $targetPath -PathType Leaf) (
            "SHA256SUMS target is missing: $relativePath"
        )

        $actualHash = (Get-FileHash -LiteralPath $targetPath -Algorithm SHA256).Hash
        Assert-Condition ($actualHash -eq $manifest[$relativePath]) (
            "SHA-256 mismatch for $relativePath. Expected $($manifest[$relativePath]), got $actualHash."
        )
    }

    $baseHashes = @{}
    foreach ($file in Get-ChildItem -LiteralPath $baseBinPath -File -Filter '*.dll') {
        $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        if (-not $baseHashes.ContainsKey($hash)) {
            $baseHashes[$hash] = $file.FullName
        }
    }
    foreach ($file in Get-ChildItem -LiteralPath $nexonPath -File -Filter '*.dll') {
        $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        Assert-Condition (-not $baseHashes.ContainsKey($hash)) (
            "bin_nexon duplicates an identical CORE bin DLL: $($file.Name) == $($baseHashes[$hash])"
        )
    }

}

function Find-DumpBin {
    $command = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $roots = @(
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio'),
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio')
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Container) }

    foreach ($root in $roots) {
        $candidate = Get-ChildItem -LiteralPath $root -Recurse -File -Filter dumpbin.exe -ErrorAction SilentlyContinue |
            Where-Object FullName -Match '\\bin\\Hostx64\\x64\\dumpbin\.exe$' |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($candidate) {
            return $candidate.FullName
        }
    }

    throw 'Could not locate dumpbin.exe for release dependency validation.'
}

function Assert-PeRuntimeImports {
    param([string[]] $Roots)

    $dumpbin = Find-DumpBin
    $files = @{}
    foreach ($root in $Roots) {
        if (-not $root -or -not (Test-Path -LiteralPath $root -PathType Container)) {
            continue
        }
        foreach ($file in Get-ChildItem -LiteralPath $root -Recurse -File |
            Where-Object Extension -In @('.dll', '.exe')) {
            $files[$file.FullName] = $file
        }
    }

    $runtimePattern =
        '(?i)\b(?:MSVCP\d+(?:_[A-Z0-9]+)?|MSVCR\d+|VCRUNTIME\d+(?:_[A-Z0-9]+)?|CONCRT\d+|VCOMP\d+|UCRTBASE(?:D)?|API-MS-WIN-CRT-[A-Z0-9-]+)\.DLL\b'
    $debugRuntimePattern =
        '^(?:MSVCP\d+D(?:_[A-Z0-9]+)?|MSVCR\d+D|VCRUNTIME\d+(?:_[A-Z0-9]+)?D|CONCRT\d+D|VCOMP\d+D|UCRTBASED)\.DLL$'
    $supportedRuntimePattern =
        '^(?:MSVCP100|MSVCR100|MSVCP140(?:_[A-Z0-9]+)?|VCRUNTIME140(?:_[A-Z0-9]+)?|CONCRT140|VCOMP140|UCRTBASE|API-MS-WIN-CRT-[A-Z0-9-]+)\.DLL$'

    foreach ($file in $files.Values | Sort-Object FullName) {
        $output = & $dumpbin /nologo /imports $file.FullName 2>&1 | Out-String
        Assert-Condition ($LASTEXITCODE -eq 0) "dumpbin could not inspect $($file.FullName)."

        $imports = @(
            [System.Text.RegularExpressions.Regex]::Matches($output, $runtimePattern) |
                ForEach-Object { $_.Value.ToUpperInvariant() } |
                Sort-Object -Unique
        )
        foreach ($import in $imports) {
            Assert-Condition ($import -notmatch $debugRuntimePattern) (
                "$($file.FullName) imports non-redistributable debug CRT $import."
            )
            Assert-Condition ($import -match $supportedRuntimePattern) (
                "$($file.FullName) imports unsupported CRT family $import. Update the launcher prerequisite catalog first."
            )
        }
    }
}

function Assert-BuildOutputs {
    param([string] $Root)

    $buildRoot = Get-NormalizedFullPath $Root
    Assert-Condition (Test-Path -LiteralPath $buildRoot -PathType Container) "Build output path does not exist: $buildRoot"

    $requiredFiles = @(
        'CoherentUIGT.dll',
        'materialsystem_nodx.dll',
        'R1Delta_DS.exe',
        'r1delta.exe',
        'r1delta.exe.config',
        'tier0.dll',
        'Titanfall.exe',
        'vaudio_speex.dll'
    )
    foreach ($fileName in $requiredFiles) {
        Assert-Condition (Test-Path -LiteralPath (Join-Path $buildRoot $fileName) -PathType Leaf) (
            "Clean Release build is missing $fileName."
        )
    }

    $stubRoot = Join-Path $buildRoot 'r1o_stubs'
    Assert-Condition (Test-Path -LiteralPath $stubRoot -PathType Container) (
        "Clean Release build is missing the isolated R1O graphics stub directory."
    )
    $requiredStubFiles = @(
        'd3d11.dll',
        'GFSDK_SSAO.win64.dll',
        'GFSDK_TXAA.win64.dll'
    )
    $actualStubFiles = @(Get-ChildItem -LiteralPath $stubRoot -File)
    $stubDirectories = @(Get-ChildItem -LiteralPath $stubRoot -Directory)
    Assert-Condition ($stubDirectories.Count -eq 0) "r1o_stubs must not contain subdirectories."
    Assert-Condition ($actualStubFiles.Count -eq $requiredStubFiles.Count) (
        "r1o_stubs contains $($actualStubFiles.Count) files; expected $($requiredStubFiles.Count)."
    )
    foreach ($fileName in $requiredStubFiles) {
        Assert-Condition (
            Test-Path -LiteralPath (Join-Path $stubRoot $fileName) -PathType Leaf
        ) "Clean Release build r1o_stubs is missing $fileName."
        Assert-Condition (
            -not (Test-Path -LiteralPath (Join-Path $buildRoot $fileName) -PathType Leaf)
        ) "$fileName must remain isolated under r1o_stubs, not the build root."
    }

    $forbiddenNames = @(
        Get-ChildItem -LiteralPath $buildRoot -Recurse -File |
            Where-Object {
                (
                    $_.Extension -in @('.dll', '.exe') -and
                    $_.Name -match '(?i)(?:^|[._-])debug(?:[._-]|$)|\.(?:locked|pre(?:_|\.))'
                ) -or
                $_.Name -ieq 'minhook.x64d.dll'
            }
    )
    $forbiddenNameList = ($forbiddenNames | ForEach-Object FullName) -join ', '
    Assert-Condition ($forbiddenNames.Count -eq 0) (
        "Clean Release build contains debug/backup artifacts: $forbiddenNameList"
    )
}

function Assert-Payload {
    param([string] $Root)

    $payloadRoot = Get-NormalizedFullPath $Root
    Assert-Condition (Test-Path -LiteralPath $payloadRoot -PathType Container) "Payload path does not exist: $payloadRoot"

    $requiredRootFiles = @('R1Delta_DS.exe', 'r1delta.exe', 'r1delta.exe.config', 'Titanfall.exe')
    if (-not $PackagedPayload) {
        $requiredRootFiles += 'R1Delta.nuspec'
    }
    foreach ($fileName in $requiredRootFiles) {
        Assert-Condition (Test-Path -LiteralPath (Join-Path $payloadRoot $fileName) -PathType Leaf) (
            "Release payload is missing $fileName."
        )
    }

    $curlPath = Join-Path (Join-Path (Join-Path $payloadRoot 'tools') 'curl') 'curl.exe'
    Assert-Condition (Test-Path -LiteralPath $curlPath -PathType Leaf) (
        "Release payload is missing exact bundled downloader path tools/curl/curl.exe."
    )
    $curlSha256 = (Get-FileHash -LiteralPath $curlPath -Algorithm SHA256).Hash.ToLowerInvariant()
    Assert-Condition (
        $curlSha256 -eq '589c8e4d297b4831c82adf0261fc1ca57ce59d663b91b4106d2ee7dff3972648'
    ) "Release payload contains an unexpected tools/curl/curl.exe binary: $curlSha256"
    $curlRoot = Split-Path -Parent $curlPath
    foreach ($relativeNoticePath in @(
        'LICENSE.static-curl.txt',
        'licenses\curl',
        'licenses\openssl'
    )) {
        $noticePath = Join-Path $curlRoot $relativeNoticePath
        Assert-Condition (Test-Path -LiteralPath $noticePath -PathType Leaf) (
            "Release payload is missing bundled curl notice $relativeNoticePath."
        )
    }
    $aria2Path = Join-Path (Join-Path (Join-Path $payloadRoot 'tools') 'aria2') 'aria2c.exe'
    Assert-Condition (-not (Test-Path -LiteralPath $aria2Path)) (
        "Release payload still contains the retired tools/aria2/aria2c.exe downloader."
    )

    $deltaRoot = Join-Path $payloadRoot 'r1delta'
    Assert-CoreRuntime $deltaRoot
    foreach ($fileName in @('CoherentUIGT.dll', 'materialsystem_nodx.dll', 'tier0.dll', 'vaudio_speex.dll')) {
        Assert-Condition (
            Test-Path -LiteralPath (Join-Path (Join-Path $deltaRoot 'bin_delta') $fileName) -PathType Leaf
        ) "Release payload bin_delta is missing $fileName."
    }

    $binDeltaRoot = Join-Path $deltaRoot 'bin_delta'
    $stubRoot = Join-Path $binDeltaRoot 'r1o_stubs'
    $requiredStubFiles = @(
        'd3d11.dll',
        'GFSDK_SSAO.win64.dll',
        'GFSDK_TXAA.win64.dll'
    )
    Assert-Condition (Test-Path -LiteralPath $stubRoot -PathType Container) (
        "Release payload is missing bin_delta/r1o_stubs."
    )
    $actualStubFiles = @(Get-ChildItem -LiteralPath $stubRoot -File)
    $stubDirectories = @(Get-ChildItem -LiteralPath $stubRoot -Directory)
    Assert-Condition ($stubDirectories.Count -eq 0) "Payload r1o_stubs must not contain subdirectories."
    Assert-Condition ($actualStubFiles.Count -eq $requiredStubFiles.Count) (
        "Payload r1o_stubs contains $($actualStubFiles.Count) files; expected $($requiredStubFiles.Count)."
    )
    foreach ($fileName in $requiredStubFiles) {
        Assert-Condition (
            Test-Path -LiteralPath (Join-Path $stubRoot $fileName) -PathType Leaf
        ) "Release payload r1o_stubs is missing $fileName."
        Assert-Condition (
            -not (Test-Path -LiteralPath (Join-Path $binDeltaRoot $fileName) -PathType Leaf)
        ) "Release payload must not place fake-dedi stub $fileName in bin_delta's root."
    }

    $forbiddenArtifacts = @(
        Get-ChildItem -LiteralPath $payloadRoot -Recurse -File |
            Where-Object {
                $_.Extension -in @('.exp', '.i64', '.lib', '.pdb') -or
                (
                    $_.Extension -in @('.dll', '.exe') -and
                    $_.Name -match '(?i)\.(?:locked|pre(?:_|\.))'
                )
            }
    )
    $forbiddenArtifactList = ($forbiddenArtifacts | ForEach-Object FullName) -join ', '
    Assert-Condition ($forbiddenArtifacts.Count -eq 0) (
        "Release payload contains developer/backup artifacts: $forbiddenArtifactList"
    )
}

$resolvedCorePath = Get-NormalizedFullPath $CorePath
Assert-CoreRuntime $resolvedCorePath

$scanRoots = @(
    (Join-Path $resolvedCorePath 'bin'),
    (Join-Path $resolvedCorePath 'bin_nexon')
)

if ($BuildPath) {
    Assert-BuildOutputs $BuildPath
    $scanRoots += Get-NormalizedFullPath $BuildPath
}

if ($PayloadPath) {
    Assert-Payload $PayloadPath
    $scanRoots += Get-NormalizedFullPath $PayloadPath
}

Assert-PeRuntimeImports $scanRoots
Write-Host 'Release payload, Nexon runtime hashes, and native CRT imports are valid.'
