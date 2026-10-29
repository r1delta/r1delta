# Local mirror of the CI native regression gate.
#
# The CI workflow builds and runs these projects itself; this script runs the same set on a
# workstation with MSBuild 18 Community and the local dependency props, so a release can be
# gated before it is pushed. Keep the lists in step with `.github/workflows/msbuild.yml`
# ("Build native regression tests" / "Run native regression tests").
param(
    [string]$RepoRoot = (Split-Path -Parent $PSScriptRoot),
    [string]$BuildPath = '',
    [string[]]$Only = @(),
    [switch]$NoRebuild,
    [switch]$SkipLauncherTests
)

$ErrorActionPreference = 'Stop'

if (-not $BuildPath) {
    $BuildPath = Join-Path $RepoRoot '.build'
}

$msbuild = 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path -LiteralPath $msbuild)) {
    throw "MSBuild not found at $msbuild"
}

$projects = [ordered]@{
    addonlist_state_tests                     = 'tests/addonlist_state_tests.vcxproj'
    compression_stream_tests                  = 'tests/compression_stream_tests.vcxproj'
    r1o_vpk_tests                             = 'tests/r1o_vpk_tests.vcxproj'
    vpk_async_precache_tests                  = 'tests/vpk_async_precache_tests.vcxproj'
    script_variant_abi_tests                  = 'tests/script_variant_abi_tests.vcxproj'
    vsdk_memalloc_tests                       = 'tests/vsdk_memalloc_tests.vcxproj'
    persistentdata_codec_tests                = 'tests/persistentdata_codec_tests.vcxproj'
    crash_report_minidump_tests               = 'tests/crash_report_minidump_tests.vcxproj'
    client_studio_header_guard_tests          = 'tests/client_studio_header_guard_tests.vcxproj'
    client_brush_material_guard_tests         = 'tests/client_brush_material_guard_tests.vcxproj'
    cbuf_initialization_guard_tests           = 'tests/cbuf_initialization_guard_tests.vcxproj'
    materialsystem_dx11_vtf_scratch_tests     = 'tests/materialsystem_dx11_vtf_scratch_tests.vcxproj'
    materialsystem_dx11_txaa_lifetime_tests   = 'tests/materialsystem_dx11_txaa_lifetime_tests.vcxproj'
    materialsystem_dx11_render_thread_tests   = 'tests/materialsystem_dx11_render_thread_tests.vcxproj'
    hudwarp_convar_tests                      = 'tests/hudwarp_convar_tests.vcxproj'
    materialsystem_dx11_input_layout_cache_tests = 'tests/materialsystem_dx11_input_layout_cache_tests.vcxproj'
    vphysics_parallel_guard_tests             = 'tests/vphysics_parallel_guard_tests.vcxproj'
    client_port_override_tests                = 'tests/client_port_override_tests.vcxproj'
    ffa_targeting_tests                       = 'tests/ffa_targeting_tests.vcxproj'
    p2p_codec_tests                           = 'tests/p2p_codec_tests.vcxproj'
    region_cull_tests                         = 'tests/region_cull_tests.vcxproj'
}

$selected = @($projects.GetEnumerator() | Where-Object {
    $Only.Count -eq 0 -or $Only -contains $_.Key
})
if ($selected.Count -eq 0) {
    throw "No test projects matched: $($Only -join ', ')"
}

$failed = @()
foreach ($entry in $selected) {
    $outDir = Join-Path $BuildPath "tests/$($entry.Key)"
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null

    $arguments = @(
        (Join-Path $RepoRoot $entry.Value),
        '/m', '/restore', "/t:$(if ($NoRebuild) { 'Build' } else { 'Rebuild' })",
        '/p:CI=1', '/p:Configuration=Release', '/p:Platform=x64',
        "/p:OutDir=$outDir/", '/v:minimal', '/nologo'
    )
    & $msbuild @arguments | Out-Null
    if ($LASTEXITCODE -ne 0) {
        $failed += "$($entry.Key) (build exit $LASTEXITCODE)"
        continue
    }

    $executable = Join-Path $outDir "$($entry.Key).exe"
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
        $failed += "$($entry.Key) (missing executable)"
        continue
    }

    $output = & $executable 2>&1
    $exit = $LASTEXITCODE
    $summary = ($output | Select-Object -Last 1)
    if ($exit -ne 0) {
        $failed += "$($entry.Key) (exit $exit)"
        Write-Host "FAIL  $($entry.Key)"
        $output | ForEach-Object { Write-Host "      $_" }
    }
    else {
        Write-Host "ok    $($entry.Key): $summary"
    }
}

if (-not $SkipLauncherTests) {
    $launcherPath = Join-Path $BuildPath 'release/r1delta.exe'
    if (Test-Path -LiteralPath $launcherPath) {
        foreach ($script in @(
            'tests/launcher_installer_integration_tests.ps1',
            'tests/fastdownload_httpclient_idle_tests.ps1',
            'tests/fastdownload_process_cleanup_tests.ps1'
        )) {
            & pwsh -NoProfile -ExecutionPolicy Bypass -File (Join-Path $RepoRoot $script) -LauncherPath $launcherPath
            if ($LASTEXITCODE -ne 0) {
                $failed += "$script (exit $LASTEXITCODE)"
            }
            else {
                Write-Host "ok    $script"
            }
        }
    }
    else {
        Write-Host "skip  launcher integration tests: $launcherPath not built"
    }
}

if ($failed.Count -ne 0) {
    Write-Host "FAIL: $($failed -join '; ')"
    exit 1
}
Write-Host "PASS: $($selected.Count) native test projects"
