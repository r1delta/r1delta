# Superseded for permanent R2-port tests: no unstage is needed; use .build\r2-root.ps1 -Reset only to recreate S:\game\r2.
$ErrorActionPreference = 'Stop'
# Restores every runtime file that stage-r2port.ps1 replaced, from its '.pre-r2port-backup' copy.
# Files that did not exist before staging (no backup) are left alone.
$runtimeRoot = 'S:\game'
$blockedNames = @('Titanfall', 'r1delta', 'R1Delta_DS', 'cdb')
$running = @(Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -in $blockedNames })
if ($running.Count -ne 0) {
  $listing = ($running | ForEach-Object { "$($_.ProcessName) (pid $($_.Id))" }) -join ', '
  throw "Refusing to unstage while a game or debugger process is running: $listing."
}

$restored = 0
$roots = @(
  (Join-Path $runtimeRoot 'r1delta\bin_delta'),
  (Join-Path $runtimeRoot 'r1delta\bin_delta\r1o_stubs'),
  $runtimeRoot
)
foreach ($root in $roots) {
  if (-not (Test-Path -LiteralPath $root)) { continue }
  Get-ChildItem -LiteralPath $root -File -Filter '*.pre-r2port-backup' | ForEach-Object {
    $target = $_.FullName.Substring(0, $_.FullName.Length - '.pre-r2port-backup'.Length)
    Copy-Item -LiteralPath $_.FullName -Destination "$target.r1dunstaging" -Force
    Move-Item -LiteralPath "$target.r1dunstaging" -Destination $target -Force
    Remove-Item -LiteralPath $_.FullName -Force
    $restored++
  }
}
Write-Host "Restored $restored runtime files from pre-r2port backups."
