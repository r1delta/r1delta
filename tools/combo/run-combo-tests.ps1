[CmdletBinding()]
param([string[]] $Only)
# Builds and runs the CI native regression tests (tests\*.vcxproj list from .github\workflows\msbuild.yml) plus the r2port
# tool tests of the combined tree. Output: .build\combo-tests.log
$ErrorActionPreference = 'Continue'
$code = 'C:\Users\r3muxd\tmp\r1delta-combined'
$out = Join-Path $code '.build\combo-tests'
$log = Join-Path $code '.build\combo-tests.log'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$yml = Get-Content (Join-Path $code '.github\workflows\msbuild.yml') -Raw
$names = [regex]::Matches($yml, "(\w+_tests) = 'tests/\1\.vcxproj'") | ForEach-Object { $_.Groups[1].Value }
if ($Only) { $names = $names | Where-Object { $Only -contains $_ } }
$vc = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
$ms = 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'
$props = Join-Path $code '.build\local-deps.props'
"== tests $(Get-Date -Format o)" | Set-Content $log
$fail = @()
foreach ($n in $names) {
    $od = Join-Path $out "$n\"
    New-Item -ItemType Directory -Force -Path $od | Out-Null
    $cmd = "call `"$vc`" >nul && `"$ms`" tests\$n.vcxproj /m:4 /t:Rebuild /p:CI=1 /p:VcpkgEnabled=false /p:ForceImportAfterCppProps=$props /p:Configuration=Release /p:Platform=x64 /p:OutDir=$od /verbosity:minimal /nologo"
    $o = cmd /c $cmd 2>&1
    if ($LASTEXITCODE -ne 0) { "BUILD FAIL $n`n$($o | Select-Object -Last 15 | Out-String)" | Add-Content $log; $fail += "$n(build)"; continue }
    $exe = Join-Path $od "$n.exe"
    $r = & $exe 2>&1
    $code2 = $LASTEXITCODE
    "TEST $n exit=$code2`n$($r | Select-Object -Last 6 | Out-String)" | Add-Content $log
    if ($code2 -ne 0) { $fail += "$n(run)" }
}
foreach ($c in @('run_material_names_tests.cmd', 'run_entity_rewrite_tests.cmd')) {
    if ($Only) { continue }
    $o = cmd /c "`"$code\tools\r2port\$c`"" 2>&1
    "R2PORT $c exit=$LASTEXITCODE`n$($o | Select-Object -Last 8 | Out-String)" | Add-Content $log
    if ($LASTEXITCODE -ne 0) { $fail += $c }
}
"FAILED: $($fail -join ', ')" | Add-Content $log
Get-Content $log | Select-Object -Last 60
