[CmdletBinding()]
param(
    # Read-only source of the R2 map port commits (a worktree/clone of the r1delta repo). Never written.
    [string] $R2PortRepo = 'C:\Users\r3muxd\tmp\r1delta-r2port',
    [string] $R2PortBranch = 'feat/r2-map-port',
    # Extra patches (git format-patch / git am mbox files) applied on top, in order (e.g. a Version30 patch).
    [string[]] $Patch = @(),
    [switch] $NoMerge,       # skip the fetch+merge (rebuild + restage only)
    [switch] $NoBuild,       # skip the build
    [string] $RootName = 'r2-combo',
    [switch] $Flag,
    [switch] $Drive
)
# One-shot refresh of the combined tree: pull the newest r2port commits (read-only fetch), merge them into the
# combo branch, apply extra patches, rebuild, restage S:\game\<RootName> (-Refresh: caches kept) and print the hashes.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$code = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
function G([string[]] $a) {
    $prev = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    $o = & git -C $code @a 2>&1
    $rc = $LASTEXITCODE; $ErrorActionPreference = $prev
    if ($rc -ne 0) { throw "git $($a -join ' ') failed ($rc): $($o -join "`n")" }
    return $o
}
if (@(G @('status', '--porcelain', '--untracked-files=no')).Count -ne 0) { throw 'combined tree has uncommitted tracked changes: commit them first' }
if (-not $NoMerge) {
    G @('fetch', '--no-tags', $R2PortRepo, "+${R2PortBranch}:refs/remotes/r2port/feat") | Out-Null
    $new = @(G @('rev-parse', 'r2port/feat'))[0]
    & git -C $code merge-base --is-ancestor $new HEAD
    $merged = ($LASTEXITCODE -eq 0)
    if ($merged) { Write-Host "[refresh] r2port $($new.Substring(0,7)) already merged" }
    else {
        $prev = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
        $o = & git -C $code merge --no-ff -m "Merge feat/r2-map-port ($($new.Substring(0,7))) into the combined tree" r2port/feat 2>&1
        $rc = $LASTEXITCODE; $ErrorActionPreference = $prev
        if ($rc -ne 0) {
            $c = @(G @('diff', '--name-only', '--diff-filter=U'))
            throw "merge conflict in: $($c -join ', '). Resolve, commit, then re-run with -NoMerge.`n$($o -join "`n")"
        }
        Write-Host "[refresh] merged r2port $($new.Substring(0,7))"
    }
}
foreach ($p in $Patch) {
    $prev = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    $o = & git -C $code am --3way --keep-cr $p 2>&1
    $rc = $LASTEXITCODE; $ErrorActionPreference = $prev
    if ($rc -ne 0) { & git -C $code am --abort 2>$null; throw "patch $p failed: $($o -join "`n")" }
    Write-Host "[refresh] applied $p"
}
if (-not $NoBuild) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'build-combo.ps1')
    if ($LASTEXITCODE -ne 0) { throw 'build failed (see .build\combo-build.log)' }
    if (Select-String -LiteralPath (Join-Path $code '.build\combo-build.log') -Pattern ' error ' -Quiet) { throw 'build log contains errors' }
}
$args2 = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'stage-combo.ps1'), '-RootName', $RootName, '-Refresh')
if ($Flag) { $args2 += '-Flag' }
if ($Drive) { $args2 += '-Drive' }
& powershell @args2
if ($LASTEXITCODE -ne 0) { throw 'stage failed' }
Write-Host ("[refresh] HEAD " + (@(G @('rev-parse', '--short', 'HEAD'))[0]) + "  r2port " + (@(G @('rev-parse', '--short', 'r2port/feat'))[0]))
