# Builds the double-clickable R1DeltaBisect.exe GUI tool.
$ErrorActionPreference = 'Stop'
$msbuild = 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'
$root = Split-Path -Parent $PSScriptRoot
& $msbuild (Join-Path $root 'bisect_gui\R1DeltaBisect.csproj') /p:Configuration=Release /p:Platform=AnyCPU /v:minimal
if ($LASTEXITCODE -ne 0) { throw "bisect GUI build failed" }
$exe = Join-Path $root 'bisect_gui\bin\Release\R1DeltaBisect.exe'
Write-Output "BUILT: $exe"
Write-Output "Double-click R1DeltaBisect.exe to pick any build v2.1.2..latest or bisect a load-crash."
