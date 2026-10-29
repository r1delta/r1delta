# Superseded for per-root tests: use .build\r2-root.ps1 -Stage -Root r2-<agent>; never stage into the user's S:\game\r2 play root.
$ErrorActionPreference = 'Stop'
$code = 'C:\Users\r3muxd\tmp\r1delta-r2port'
$build = Join-Path $code '.build\release'
$stubs = Join-Path $build 'r1o_stubs'
$runtimeRoot = 'S:\game'
$runtimeDelta = Join-Path $runtimeRoot 'r1delta\bin_delta'

$blockedNames = @('Titanfall', 'r1delta', 'R1Delta_DS', 'cdb')
$running = @(Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -in $blockedNames })
if ($running.Count -ne 0) {
  $listing = ($running | ForEach-Object {
    $started = try { $_.StartTime.ToString('HH:mm:ss') } catch { 'unknown' }
    "$($_.ProcessName) (pid $($_.Id), started $started)"
  }) -join ', '
  throw "Refusing to stage while a game or debugger process is running: $listing. Stop only processes you started, then re-run."
}

New-Item -ItemType Directory -Path $stubs -Force | Out-Null
foreach ($name in @('d3d11.dll','GFSDK_SSAO.win64.dll','GFSDK_TXAA.win64.dll')) {
  $path = Join-Path $build $name
  if (Test-Path -LiteralPath $path) { Move-Item -LiteralPath $path -Destination $stubs -Force }
}
# CORE may contain non-runtime analysis databases. Keep the shared verifier unchanged
# by passing it a temporary mirror of the runtime files only.
$verifyCore = Join-Path $env:TEMP ("r2port-core-verify-" + [Guid]::NewGuid().ToString('N'))
try {
  foreach ($directory in @('bin', 'bin_nexon')) {
    New-Item -ItemType Directory -Path (Join-Path $verifyCore $directory) -Force | Out-Null
  }
  foreach ($directory in @('bin', 'bin_nexon')) {
    $sourceDirectory = Join-Path 'S:\game\r1delta' $directory
    $targetDirectory = Join-Path $verifyCore $directory
    Get-ChildItem -LiteralPath $sourceDirectory -File |
      Where-Object { $_.Extension -in @('.dll', '.exe') } |
      ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $targetDirectory $_.Name)
      }
  }
  Copy-Item -LiteralPath 'S:\game\r1delta\bin_nexon\SHA256SUMS' -Destination (Join-Path $verifyCore 'bin_nexon\SHA256SUMS')
  & (Join-Path $code 'ci\verify_release_payload.ps1') -CorePath $verifyCore -BuildPath $build
} finally {
  if (Test-Path -LiteralPath $verifyCore) { Remove-Item -LiteralPath $verifyCore -Recurse -Force }
}
New-Item -ItemType Directory -Path $runtimeDelta -Force | Out-Null

function Copy-RuntimeFile([string] $source, [string] $destinationDirectory) {
  $destination = Join-Path $destinationDirectory (Split-Path -Leaf $source)
  $backup = "$destination.pre-r2port-backup"
  if ((Test-Path -LiteralPath $destination) -and -not (Test-Path -LiteralPath $backup)) {
    Copy-Item -LiteralPath $destination -Destination $backup
  }
  $staging = "$destination.r1dstaging"
  Copy-Item -LiteralPath $source -Destination $staging -Force
  Move-Item -LiteralPath $staging -Destination $destination -Force
}

Get-ChildItem -LiteralPath $build -File -Filter '*.dll' | ForEach-Object { Copy-RuntimeFile $_.FullName $runtimeDelta }
Get-ChildItem -LiteralPath $build -File -Filter '*.png' | ForEach-Object { Copy-RuntimeFile $_.FullName $runtimeDelta }
$stubDestination = Join-Path $runtimeDelta 'r1o_stubs'
New-Item -ItemType Directory -Path $stubDestination -Force | Out-Null
Get-ChildItem -LiteralPath $stubs -File | ForEach-Object { Copy-RuntimeFile $_.FullName $stubDestination }
foreach ($name in @('Titanfall.exe','R1Delta_DS.exe')) {
  Copy-RuntimeFile (Join-Path $build $name) $runtimeRoot
}
Write-Host 'Candidate runtime staged from the R2-port worktree with pre-r2port backups.'
