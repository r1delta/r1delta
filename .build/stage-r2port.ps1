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
& (Join-Path $code 'ci\verify_release_payload.ps1') -CorePath 'S:\game\r1delta' -BuildPath $build
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
