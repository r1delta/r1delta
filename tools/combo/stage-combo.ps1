[CmdletBinding()]
param(
    [string] $RootName = 'r2-combo',
    [switch] $Reset,        # remove the root (junctions unlinked, never followed) and recreate it
    [switch] $Refresh,      # keep the root and its caches; re-stage binaries + CORE overlay only
    [switch] $Flag,         # add r1delta_background.flag (test copies only; the user's launch has none)
    [switch] $Drive         # TEST ONLY: input-injection fixture in this root's pilot.nut (console +forward does nothing in a background window)
)
# Stages the combined (R2 map port + slide/grapple movement) test root. Templates: S:\game\r2-lead (round 19 files,
# hardlinked, never written), S:\game\r2-shader (cfg), S:\game\r1delta (user CORE: movement scripts + credits, read only),
# the combined build (.build\release). Nothing outside $Root is modified.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Game = 'S:\game'
$Root = Join-Path $Game $RootName
$Core = Join-Path $Root 'r1delta'
$Lead = Join-Path $Game 'r2-lead'
$LeadCore = Join-Path $Lead 'r1delta'
$ShaderCore = Join-Path $Game 'r2-shader\r1delta'
$UserCore = Join-Path $Game 'r1delta'
$TemplateCore = 'S:\game\r2dev\r1delta'   # pristine pinned CORE content (r2-lead is missing 138 stock materials)
$CodeRoot = 'C:\Users\r3muxd\tmp\r1delta-combined'
$Build = Join-Path $CodeRoot '.build\release'
$Handover = Join-Path $CodeRoot '.build\handover'
$AssetStage = 'S:\game\.mp-slide-model-assets-stage-2026'
$BaseCommit = '0dff84a3'   # CORE commit the movement script edits were made against (316993b descends from it)
$StubNames = @('d3d11.dll', 'GFSDK_SSAO.win64.dll', 'GFSDK_TXAA.win64.dll')
$RootExes = @('r1delta.exe', 'Titanfall.exe', 'R1Delta_DS.exe', 'r1delta.exe.config')
$LinkDirs = @('bin', 'bin_nexon', 'materials', 'models', 'sound', 'vgui', 'cfg', 'config', 'admin')
$MovementScripts = @('scripts/game_sounds_manifest.txt', 'scripts/vscripts/_autobhop.nut', 'scripts/vscripts/_cl_mapspawn.nut',
    'scripts/vscripts/_mapspawn.nut', 'scripts/vscripts/_remote_functions.nut', 'scripts/vscripts/ui/menu_credits.nut')

if ($RootName -in @('r2', 'r1delta', 'mp', 'sp', 'r2-lead', 'r2-shader', 'r2-provider', 'r2-texlib', 'r2dev', 'r2-lead2')) { throw "Refusing root name $RootName" }

Add-Type -Language CSharp -TypeDefinition @'
using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
public static class ComboNative
{
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CreateHardLinkW(string lpFileName, string lpExistingFileName, IntPtr lpSecurityAttributes);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr CreateFileW(string f, uint access, uint share, IntPtr sec, uint disp, uint flags, IntPtr tmpl);
    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetFileInformationByHandle(IntPtr h, int cls, ref uint info, uint size);
    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CloseHandle(IntPtr h);
    private static bool Rp(FileSystemInfo e) { return (e.Attributes & FileAttributes.ReparsePoint) != 0; }

    public static int LinkTree(string source, string destination)
    {
        int n = 0;
        Directory.CreateDirectory(destination);
        foreach (FileSystemInfo e in new DirectoryInfo(source).EnumerateFileSystemInfos())
        {
            if (Rp(e)) throw new IOException("reparse point in source tree: " + e.FullName);
            string t = Path.Combine(destination, e.Name);
            DirectoryInfo d = e as DirectoryInfo;
            if (d != null) { n += LinkTree(d.FullName, t); continue; }
            if (e.Name.EndsWith(".i64", StringComparison.OrdinalIgnoreCase) || e.Name.EndsWith(".idb", StringComparison.OrdinalIgnoreCase)) continue;
            if (File.Exists(t)) continue;
            if (!CreateHardLinkW(t, e.FullName, IntPtr.Zero)) throw new Win32Exception(Marshal.GetLastWin32Error(), "hardlink " + e.FullName);
            n++;
        }
        return n;
    }
    public static void LinkFile(string source, string destination)
    {
        if (!CreateHardLinkW(destination, source, IntPtr.Zero)) throw new Win32Exception(Marshal.GetLastWin32Error(), "hardlink " + source);
    }
    // Unlinks one directory entry (read-only ok) without touching the possibly shared inode.
    private static int Unlink(string path)
    {
        IntPtr h = CreateFileW(path, 0x00010000, 7, IntPtr.Zero, 3, 0x02000000 | 0x00200000, IntPtr.Zero);
        if (h == new IntPtr(-1)) return Marshal.GetLastWin32Error();
        try { uint flags = 0x1 | 0x10; return SetFileInformationByHandle(h, 21, ref flags, 4) ? 0 : Marshal.GetLastWin32Error(); }
        finally { CloseHandle(h); }
    }
    public static void RemoveTree(string path)
    {
        FileSystemInfo item = Directory.Exists(path) ? (FileSystemInfo)new DirectoryInfo(path) : (File.Exists(path) ? (FileSystemInfo)new FileInfo(path) : null);
        if (item == null) return;
        Remove(item);
    }
    private static void Remove(FileSystemInfo item)
    {
        DirectoryInfo d = item as DirectoryInfo;
        if (d != null)
        {
            if (!Rp(d)) foreach (FileSystemInfo c in d.GetFileSystemInfos()) Remove(c);
            Directory.Delete(d.FullName, false);
            return;
        }
        int err = Unlink(item.FullName);
        if (err != 0) throw new Win32Exception(err, "unlink " + item.FullName);
    }
    public static void Replace(string source, string destination)
    {
        if (File.Exists(destination)) { int err = Unlink(destination); if (err != 0) throw new Win32Exception(err, "unlink " + destination); }
        Directory.CreateDirectory(Path.GetDirectoryName(destination));
        File.Copy(source, destination, false);
    }
}
'@

function Say([string] $m) { Write-Host ("[combo] " + $m) }
function Sha([string] $p) { (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant() }
function GitC([string] $dir, [string[]] $a) {
    $prev = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    $o = & git -c core.quotepath=false -C $dir @a 2>&1
    $code = $LASTEXITCODE; $ErrorActionPreference = $prev
    $LASTEXITCODE = $code
    if ($LASTEXITCODE -ne 0) { throw "git -C $dir $($a -join ' ') failed: $($o -join "`n")" }
    return $o
}
function Assert-Idle {
    $p = @(Get-CimInstance Win32_Process | Where-Object { $_.ExecutablePath -and $_.ExecutablePath.StartsWith($Root + '\', [StringComparison]::OrdinalIgnoreCase) })
    if ($p.Count) { throw "Processes still running from $Root : $(($p | ForEach-Object { $_.ProcessId }) -join ',')" }
}

$free = (Get-PSDrive C).Free
if ($free -lt 6GB) { throw "C: free space $([math]::Round($free/1GB,1)) GB < 6 GB" }

# ---------------------------------------------------------------- create / reset
$exists = Test-Path -LiteralPath $Root
if ($exists -and $Reset) {
    Assert-Idle
    Say "removing $Root"
    [ComboNative]::RemoveTree($Root)
    $exists = $false
}
if ($exists -and -not $Refresh) { throw "$Root exists; use -Reset (recreate) or -Refresh (restage binaries + overlay)" }

if (-not $exists) {
    Say "creating $Root from $Lead"
    New-Item -ItemType Directory -Path $Root | Out-Null
    foreach ($f in Get-ChildItem -LiteralPath $Lead -File -Force) {
        if ($f.Name -like '*.pre-r2port-backup' -or $f.Name -eq 'r1delta_background.flag' -or $RootExes -contains $f.Name) { continue }
        [ComboNative]::LinkFile($f.FullName, (Join-Path $Root $f.Name))
    }
    foreach ($j in @('bin', 'r1', 'tools')) { New-Item -ItemType Junction -Path (Join-Path $Root $j) -Target (Join-Path $Game $j) | Out-Null }
    foreach ($d in @('crash', 'crashes')) { New-Item -ItemType Directory -Path (Join-Path $Root $d) | Out-Null }

    New-Item -ItemType Directory -Path $Core | Out-Null
    foreach ($d in $LinkDirs) {
        $n = [ComboNative]::LinkTree((Join-Path $TemplateCore $d), (Join-Path $Core $d))
        Say "hardlinked $d ($n files)"
    }
    foreach ($f in Get-ChildItem -LiteralPath $LeadCore -File -Force) { Copy-Item -LiteralPath $f.FullName -Destination (Join-Path $Core $f.Name) }
    # Private copies (written to): git metadata, scripts, maps, resource, shaders, bin_delta, r2port runtime + caches.
    foreach ($d in @('.git', 'scripts', 'resource', 'maps', 'shaders', 'r2port', 'r2port_cache')) {
        $s = Join-Path $LeadCore $d
        if (Test-Path -LiteralPath $s) { Copy-Item -LiteralPath $s -Destination (Join-Path $Core $d) -Recurse }
    }
    foreach ($junk in @('r2port\fs-trace.txt', 'r2port\postfx_stats.txt')) { Remove-Item -LiteralPath (Join-Path $Core $junk) -Force -ErrorAction SilentlyContinue }
    New-Item -ItemType Directory -Path (Join-Path $Core 'bin_delta') | Out-Null
    foreach ($f in Get-ChildItem -LiteralPath (Join-Path $LeadCore 'bin_delta') -Recurse -File -Force) {
        if ($f.Name -like '*.pre-r2port-backup' -or $f.Name -like '*.pre39-backup') { continue }
        $rel = $f.FullName.Substring((Join-Path $LeadCore 'bin_delta').Length + 1)
        $dst = Join-Path (Join-Path $Core 'bin_delta') $rel
        New-Item -ItemType Directory -Path (Split-Path -Parent $dst) -Force | Out-Null
        Copy-Item -LiteralPath $f.FullName -Destination $dst
    }
    foreach ($n in @('r2port_maps.cfg', 'r1_maps.cfg')) {
        $dst = Join-Path $Core "cfg\$n"
        if (-not (Test-Path -LiteralPath $dst)) { Copy-Item -LiteralPath (Join-Path $ShaderCore "cfg\$n") -Destination $dst }
    }
} else {
    Assert-Idle
    Say "refreshing $Root"
}

# ---------------------------------------------------------------- binaries
Say "staging build $Build"
$bd = Join-Path $Core 'bin_delta'
$stubDir = Join-Path $bd 'r1o_stubs'
New-Item -ItemType Directory -Path $stubDir -Force | Out-Null
foreach ($f in Get-ChildItem -LiteralPath $Build -File | Where-Object { $_.Extension -in @('.dll', '.png') -and $StubNames -notcontains $_.Name }) {
    [ComboNative]::Replace($f.FullName, (Join-Path $bd $f.Name))
}
foreach ($n in @('tier0.pdb', 'tier0.lib', 'tier0.exp')) {
    if (Test-Path -LiteralPath (Join-Path $Build $n)) { [ComboNative]::Replace((Join-Path $Build $n), (Join-Path $bd $n)) }
}
foreach ($n in $StubNames) { [ComboNative]::Replace((Join-Path $Build $n), (Join-Path $stubDir $n)) }
foreach ($n in $RootExes) { [ComboNative]::Replace((Join-Path $Build $n), (Join-Path $Root $n)) }
foreach ($n in @('r1delta.pdb', 'Titanfall.pdb', 'R1Delta_DS.pdb')) {
    if (Test-Path -LiteralPath (Join-Path $Build $n)) { [ComboNative]::Replace((Join-Path $Build $n), (Join-Path $Root $n)) }
}
$flagPath = Join-Path $Root 'r1delta_background.flag'
if ($Flag) { if (-not (Test-Path -LiteralPath $flagPath)) { New-Item -ItemType File -Path $flagPath | Out-Null } }
else { Remove-Item -LiteralPath $flagPath -Force -ErrorAction SilentlyContinue }

# ---------------------------------------------------------------- CORE overlay (movement scripts, credits, sounds, assets)
Say 'applying CORE movement overlay from the user CORE (read only)'
$state = Join-Path $Root '.combo-overlay.json'
if (Test-Path -LiteralPath $state) {
    # Refresh: restore the overlaid tracked scripts from the pinned commit first so the patch re-applies cleanly.
    GitC $Core (@('checkout', '--') + @($MovementScripts | Where-Object { $_ -notlike '*_autobhop.nut' })) | Out-Null
    GitC $Core @('checkout', '--', 'scripts/vscripts/menu/_lobby.nut') | Out-Null   # drop the withdrawn DLC lobby patch from earlier stagings
    Remove-Item -LiteralPath (Join-Path $Core 'scripts\vscripts\_autobhop.nut') -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath (Join-Path $Core 'scripts\vscripts\movement') -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath (Join-Path $Core 'sound\r1delta\movement') -Recurse -Force -ErrorAction SilentlyContinue
    # Every later step (movement patch, auto-enable, v3.0 browser, DynWeapons2, FD overlay) re-applies on a clean pinned tree: restore all tracked
    # files and drop untracked files under scripts/ (all of them are regenerated below; the FD patch resets files an earlier patch also touched).
    GitC $Core @('checkout', '--', '.') | Out-Null
    GitC $Core @('clean', '-fdq', '--', 'scripts') | Out-Null
}
$patch = Join-Path $Root '.combo-core-overlay.patch'
$prevEap = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
$diff = & git -C $UserCore -c core.quotepath=false -c core.safecrlf=false diff --binary $BaseCommit -- @MovementScripts 2>$null
$ErrorActionPreference = $prevEap
if ($LASTEXITCODE -ne 0 -or -not $diff) { throw 'could not diff the user CORE movement scripts' }
[IO.File]::WriteAllText($patch, (($diff -join "`n") + "`n"), [Text.UTF8Encoding]::new($false))
GitC $Core @('apply', '--ignore-whitespace', '--whitespace=nowarn', $patch) | Out-Null
foreach ($pair in @(@('scripts\vscripts\movement', 'scripts\vscripts\movement'), @('sound\r1delta\movement', 'sound\r1delta\movement'))) {
    $s = Join-Path $UserCore $pair[0]; $d = Join-Path $Core $pair[1]
    New-Item -ItemType Directory -Path $d -Force | Out-Null
    foreach ($f in Get-ChildItem -LiteralPath $s -Recurse -File) {
        $rel = $f.FullName.Substring($s.Length + 1)
        [ComboNative]::Replace($f.FullName, (Join-Path $d $rel))
    }
}
[ComboNative]::Replace((Join-Path $UserCore 'scripts\r1_delta_slide_sounds.txt'), (Join-Path $Core 'scripts\r1_delta_slide_sounds.txt'))
# Handover CORE patches that the user CORE does not carry yet (applied only when not already present).
foreach ($p in @('core-slide-body.patch', 'core-grapple-credit.patch')) {
    $pp = Join-Path $Handover $p
    $prev = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    & git -C $Core apply --ignore-whitespace --whitespace=nowarn -R --check $pp 2>$null
    $already = ($LASTEXITCODE -eq 0)
    $ErrorActionPreference = $prev
    if ($already) { Say "$p already present"; continue }
    GitC $Core @('apply', '--ignore-whitespace', '--whitespace=nowarn', $pp) | Out-Null
    Say "applied $p"
}

$manifest = Get-Content -LiteralPath (Join-Path $Handover 'slide-assets-manifest.json') -Raw | ConvertFrom-Json
$na = 0
# Escrow 2: NO slide model asset is staged (the 36 replacement pilot models of models/humans/** and models/anim/pilot_slide*.mdl: the replacement
# MDLs render the pilots invisible; the native slide pose retarget that used them is off). Every manifest file is DELETED from this root's CORE copy
# when present (an earlier staging or the CORE itself may carry it), so -Refresh / -Flag can never leave one behind.
foreach ($a in $manifest.files) {
    $dst = Join-Path $Core ($a.path.Replace('/', '\'))
    if (Test-Path -LiteralPath $dst) { Remove-Item -LiteralPath $dst -Force; $na++ }
}
Say "$na slide model assets removed from the root (none staged)"
if ($Drive) {
    $pn = Join-Path $Core 'scripts\vscripts\movement\pilot.nut'
    $s = [IO.File]::ReadAllText($pn)
    $a = 'Move_SetCheckpointMask(MOVE_CHECKPOINT_ALL);\n"'
    $b = 'Move_SetCheckpointMask(MOVE_CHECKPOINT_ALL);\nif (Move_GetPhase() == MOVE_SETUP && (Move_GetCustomButtons() & 2) != 0) Move_SetForwardSpeed(450.0);\n"'
    $c = 'Move_InputSetButtons( ::PilotMovementGrappleHeld ? 1 : 0 )'
    $d = 'Move_InputSetButtons( ( ::PilotMovementGrappleHeld ? 1 : 0 ) | ( ( "MVDrive" in getroottable() && ::MVDrive ) ? 2 : 0 ) )'
    if (-not $s.Contains($a) -or -not $s.Contains($c)) { throw 'pilot.nut drive anchors missing' }
    [IO.File]::WriteAllText($pn, $s.Replace($a, $b).Replace($c, $d), [Text.UTF8Encoding]::new($false))
    Copy-Item -LiteralPath (Join-Path $CodeRoot 'tools\combo\combo_test.nut') -Destination (Join-Path $Core 'scripts\vscripts\movement\combo_test.nut') -Force
    Say 'TEST FIXTURE (-Drive) installed: re-run -Refresh without -Drive before handing the root to anyone'
}

# ---- slide/grapple on by default (playtest): tools\combo\core-pilot-auto-enable.patch opts every pilot in on converted R2 maps
# (cvar delta_pilot_movement_auto_r2, default 1) and on stock maps when delta_pilot_movement_auto_r1 is 1 (default 0).
$autoPatch = Join-Path $CodeRoot 'tools\combo\core-pilot-auto-enable.patch'
GitC $Core @('apply', '--ignore-whitespace', '--whitespace=nowarn', $autoPatch) | Out-Null
Say 'applied core-pilot-auto-enable.patch'
# Grapple disabled for the playtest (CORE-only, reversible): the client never sends the grapple button while delta_pilot_grapple_enabled is 0
# (native cvar, default 0, replicated: a dedicated server decides), so the native grapple program stays in phase 0 and takes no weapon lock.
# Slide is untouched. delta_pilot_grapple_enabled 1 re-enables it.
$pnG = Join-Path $Core 'scripts\vscripts\movement\pilot.nut'
$sG = [IO.File]::ReadAllText($pnG)
$aG = '::PilotMovementGrappleHeld ? 1 : 0'
if (-not $sG.Contains($aG)) { throw 'pilot.nut grapple gate anchor missing' }
$tmpG = Join-Path $env:TEMP ('combo-pilot-' + [Guid]::NewGuid().ToString('N') + '.nut')
[IO.File]::WriteAllText($tmpG, $sG.Replace($aG, '( ::PilotMovementGrappleHeld && GetConVarInt( "delta_pilot_grapple_enabled" ) != 0 ) ? 1 : 0'), [Text.UTF8Encoding]::new($false))
[ComboNative]::Replace($tmpG, $pnG); Remove-Item $tmpG
Say 'grapple gated by delta_pilot_grapple_enabled (default 0)'
# 3P slide body animation disabled for the playtest (stuck slide animation after the slide ends): both per-frame writers of Movement_SetSlideActivity
# (client slide_body.nut presentation loop, server pilot_server.nut) are skipped while delta_pilot_slide_body is 0 (native cvar, default 0).
# NOT passed as (0,false): session 0 makes the native side clear the player's whole presentation record every frame, which also wipes the viewmodel
# tilt / FOV features set by the presentation script. Slide movement, slide audio and the viewmodel tilt are untouched.
foreach ($fn in @('slide_body.nut', 'pilot_server.nut')) {
    $pnB = Join-Path $Core ('scripts\vscripts\movement\' + $fn)
    $sB = [IO.File]::ReadAllText($pnB)
    $aB = 'Movement_SetSlideActivity( player, session, sliding )'
    if (-not $sB.Contains($aB)) { throw "$fn slide body gate anchor missing" }
    $tmpB = Join-Path $env:TEMP ('combo-body-' + [Guid]::NewGuid().ToString('N') + '.nut')
    [IO.File]::WriteAllText($tmpB, $sB.Replace($aB, 'if ( GetConVarInt( "delta_pilot_slide_body" ) != 0 ) Movement_SetSlideActivity( player, session, sliding )'), [Text.UTF8Encoding]::new($false))
    [ComboNative]::Replace($tmpB, $pnB); Remove-Item $tmpB
}
Say 'slide body animation switch delta_pilot_slide_body wired (native default 1)'
# Version30: server browser compares release tags without the leading v (min server version v3.0.0); applied once (reverse check).
$v30Core = Join-Path $CodeRoot 'tools\combo\core-v30-serverbrowser.patch'
$prev = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
& git -C $Core apply --ignore-whitespace --whitespace=nowarn -R --check $v30Core 2>$null
$v30Done = ($LASTEXITCODE -eq 0)
$ErrorActionPreference = $prev
if (-not $v30Done) { GitC $Core @('apply', '--ignore-whitespace', '--whitespace=nowarn', $v30Core) | Out-Null; Say 'applied core-v30-serverbrowser.patch' }
# DLC ownership (DlcLobby): lobby map re-selection when a player without the DLC joins (countdown untouched), DLC-aware ordered/random/voted
# map picks, and the stale `serverDLCMapGroups 15` autoexec line removed (the server computes it); applied once (reverse check).
$dlcCore = Join-Path $CodeRoot 'tools\combo\core-dlc-lobby.patch'
$prev = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
& git -C $Core apply --ignore-whitespace --whitespace=nowarn -R --check $dlcCore 2>$null
$dlcDone = ($LASTEXITCODE -eq 0)
$ErrorActionPreference = $prev
if (-not $dlcDone) { GitC $Core @('apply', '--ignore-whitespace', '--whitespace=nowarn', $dlcCore) | Out-Null; Say 'applied core-dlc-lobby.patch' }
# DynWeapons2: the dynamic weapon manifest CORE patches (renamed weapon definitions, pdata names, scripts/weapon_manifest.txt). They go together
# with the dynamic weapon code; applied once each (reverse check), in order.
foreach ($dw in @(Get-ChildItem -LiteralPath (Join-Path $CodeRoot 'tools\combo\dynweapons') -Filter '*.patch' | Sort-Object Name)) {
    $prev = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    & git -C $Core apply --ignore-whitespace --whitespace=nowarn -R --check $dw.FullName 2>$null
    $dwDone = ($LASTEXITCODE -eq 0)
    $ErrorActionPreference = $prev
    if (-not $dwDone) { GitC $Core @('apply', '--ignore-whitespace', '--whitespace=nowarn', $dw.FullName) | Out-Null; Say "applied dynweapons $($dw.Name)" }
}
# R2 map sounds (AudioPort CORE-0001): the LAST precache_file of the manifest is the generated, in-memory scripts/r2port_sounds.txt.
# Idempotent; the manifest is re-restored from the pin by -Refresh above, so this runs every staging.
$mf = Join-Path $Core 'scripts\game_sounds_manifest.txt'
$mt = [IO.File]::ReadAllText($mf)
if ($mt -notmatch 'scripts/r2port_sounds\.txt') {
    $i = $mt.LastIndexOf('}')
    if ($i -lt 0) { throw 'game_sounds_manifest.txt has no closing brace' }
    $mt = $mt.Substring(0, $i) + "`t`"precache_file`"`t`t`"scripts/r2port_sounds.txt`"`n" + $mt.Substring($i)
    $tmpm = Join-Path $env:TEMP ('combo-mf-' + [Guid]::NewGuid().ToString('N') + '.txt')
    [IO.File]::WriteAllText($tmpm, $mt, [Text.UTF8Encoding]::new($false))
    [ComboNative]::Replace($tmpm, $mf); Remove-Item $tmpm
    Say 'appended scripts/r2port_sounds.txt to game_sounds_manifest.txt'
}
# Default grapple keys for playtesters (r2-combo only; private copies of two cfg files, never written through a hardlink):
#  - config_default_pc.cfg: MOUSE4 (only used for a profile without saved settings, so it can never clobber a player's own bind);
#  - autoexec_delta.cfg: H (runs every launch; neither the shipped defaults nor the user's profile use H).
function Set-ComboCfgBlock([string] $path, [string] $block) {
    $t = [IO.File]::ReadAllText($path)
    $t = [regex]::Replace($t, '(?s)\r?\n+// ---- r2-combo playtest defaults.*$', '')
    $t = $t.TrimEnd() + "`n`n" + $block
    $tmp = Join-Path $env:TEMP ('combo-cfg-' + [Guid]::NewGuid().ToString('N') + '.cfg')
    [IO.File]::WriteAllText($tmp, $t, [Text.UTF8Encoding]::new($false))
    [ComboNative]::Replace($tmp, $path); Remove-Item $tmp
}
Set-ComboCfgBlock (Join-Path $Core 'cfg\autoexec_delta.cfg') ("// ---- r2-combo playtest defaults (this root only)`n// Slide is enabled automatically on converted R2 maps (delta_pilot_movement_auto_r2 1; stock maps: delta_pilot_movement_auto_r1 1). The grapple is disabled for this playtest: delta_pilot_grapple_enabled 1 re-enables it (+scriptCommand3).`n// Slide tuning (SP friction 1, MP ground acceleration 300 instead of 50). Set here so an archived profile value cannot win.`ndelta_sp_slide_friction_scale 1`ndelta_sp_slide_ground_acceleration 300`n")
Set-ComboCfgBlock (Join-Path $Core 'cfg\config_default_pc.cfg') ("// ---- r2-combo playtest defaults (this root only)`n// Fresh profiles: grapple on MOUSE4 (+scriptCommand3), also on H via autoexec_delta.cfg.`nbind `"MOUSE4`" `"+scriptCommand3`"`n")

# ---- Frontier Defense overlay (playtest only; GENERATED from the player's own Titanfall 2 data, never committed).
# New files (adapter, generated map scripts, minimap materials) are copied (unlink + copy) into this root only. CORE files that the
# pinned CORE already tracks (_consts.nut, _burncards.nut, cl_*.nut, ...) are NOT replaced by the package copies (those come from an
# older CORE base and drop the Turbo Titan constants): they are reset to the pin and FdPlayers' core-scripts.patch is applied instead.
$FdOverlay = 'C:\Users\r3muxd\tmp\r2port\maps\fd\package\overlay\r1delta'
$FdPatch = 'C:\Users\r3muxd\tmp\r2port\maps\fd\players\core-scripts.patch'
if (Test-Path -LiteralPath $FdOverlay) {
    $nfd = 0; $tracked = @()
    foreach ($f in Get-ChildItem -LiteralPath $FdOverlay -Recurse -File) {
        $rel = $f.FullName.Substring($FdOverlay.Length + 1)
        $prev = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
        & git -C $Core ls-files --error-unmatch ($rel.Replace('\', '/')) *> $null
        $isTracked = ($LASTEXITCODE -eq 0)
        $ErrorActionPreference = $prev
        if ($isTracked) { $tracked += $rel.Replace('\', '/'); continue }
        [ComboNative]::Replace($f.FullName, (Join-Path $Core $rel)); $nfd++
    }
    if ($tracked.Count) { GitC $Core (@('checkout', '--') + $tracked) | Out-Null }
    GitC $Core @('apply', '--ignore-whitespace', '--whitespace=nowarn', $FdPatch) | Out-Null
    Say "$nfd new Frontier Defense overlay files staged; $($tracked.Count) tracked CORE files patched via core-scripts.patch"
}

# FD overlay vs DynWeapons2: the FD step above resets _consts.nut, client\cl_main_hud.nut and mp\_coobj_towerdefense.nut to the pin and applies FdPlayers'
# core-scripts.patch, which drops DynWeapons2's renames in those three files (stale mp_weapon_mega1..5 ids: SCRIPT ERROR invalid constant
# [eDamageSourceId.mp_weapon_mega3] and a failed include of mp/_COOBJ_towerdefense on every coop map load). Re-apply the DynWeapons2 hunks of just those
# three files on top of the FD result (git apply --include, in patch order; the hunks of the other files are already applied).
if (Test-Path -LiteralPath $FdOverlay) {
    $fdInc = @('--include=scripts/vscripts/_consts.nut', '--include=scripts/vscripts/client/cl_main_hud.nut', '--include=scripts/vscripts/mp/_coobj_towerdefense.nut')
    foreach ($dw in @(Get-ChildItem -LiteralPath (Join-Path $CodeRoot 'tools\combo\dynweapons') -Filter '*.patch' | Sort-Object Name)) {
        GitC $Core (@('apply', '--ignore-whitespace', '--whitespace=nowarn') + $fdInc + @($dw.FullName)) | Out-Null
    }
    Say 'DynWeapons2 hunks re-applied on the FD-patched _consts.nut / cl_main_hud.nut / _coobj_towerdefense.nut'
}

# ---- LobbyMaps CORE (R2 maps in the picker/lobby/vote/browser, playlists, group 4/5 ownership): patch applies on top of the FD overlay (it was
# made against the staged CORE), once (reverse check); the 11 localization files are replaced whole (unlink + copy).
$lm = Join-Path $CodeRoot 'tools\combo\lobbymaps'
$lmPatch = Join-Path $lm 'lobbymaps-core.patch'
if (Test-Path -LiteralPath $lmPatch) {
    $prev = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    & git -C $Core apply --ignore-whitespace --whitespace=nowarn -R --check $lmPatch 2>$null
    $lmDone = ($LASTEXITCODE -eq 0)
    $ErrorActionPreference = $prev
    if (-not $lmDone) { GitC $Core @('apply', '--ignore-whitespace', '--whitespace=nowarn', $lmPatch) | Out-Null; Say 'applied lobbymaps-core.patch' }
    foreach ($f in Get-ChildItem -LiteralPath (Join-Path $lm 'resource') -Filter 'delta_*.txt') { [ComboNative]::Replace($f.FullName, (Join-Path $Core ('resource\' + $f.Name))) }
    Say 'LobbyMaps localization files staged'
    # Playtest: no converted R2 map in CTF (playlist side; the picker/vote/server guard side is GetTitanfall2MapModes without "ctf" in the patch).
    # The R2 entries LobbyMaps appended to the "ctf" playlist are map24.. / gamemode24.. of its maps block: drop them (idempotent).
    $plf = Join-Path $Core 'playlists.txt'
    $plt = [IO.File]::ReadAllText($plf)
    $mc = [regex]::Match($plt, '(?m)^"ctf"[ \t]*\r?$')
    if (-not $mc.Success) { throw 'playlists.txt has no ctf playlist' }
    $mn = [regex]::Match($plt.Substring($mc.Index + $mc.Length), '(?m)^"(?!vars"|maps"|ctf")\w+"[ \t]*\r?$')
    $blockEnd = if ($mn.Success) { $mc.Index + $mc.Length + $mn.Index } else { $plt.Length }
    $block = $plt.Substring($mc.Index, $blockEnd - $mc.Index)
    $block2 = [regex]::Replace($block, '(?m)^"(?:map|gamemode)(\d+)"\t"[^"\r\n]*"[ \t]*\r?\n', { param($x) if ([int]$x.Groups[1].Value -ge 24) { '' } else { $x.Value } })
    if ($block2 -ne $block) {
        $tmpP = Join-Path $env:TEMP ('combo-pl-' + [Guid]::NewGuid().ToString('N') + '.txt')
        [IO.File]::WriteAllText($tmpP, $plt.Substring(0, $mc.Index) + $block2 + $plt.Substring($blockEnd), [Text.UTF8Encoding]::new($false))
        [ComboNative]::Replace($tmpP, $plf); Remove-Item $tmpP
    }
    $left = @([regex]::Matches($block2, '(?m)^"(?:map|gamemode)(\d+)"') | Where-Object { [int]$_.Groups[1].Value -ge 24 }).Count
    if ($left -ne 0) { throw "ctf playlist still has $left entries >= 24" }
    $nrem = @([regex]::Matches($block, '(?m)^"(?:map|gamemode)(\d+)"') | Where-Object { [int]$_.Groups[1].Value -ge 24 }).Count
    Say "ctf playlist: removed $nrem R2 entries"
}
# ---- MinimapZoom CORE (fullscreen map window for the converted R2 maps; script-only), applied once (reverse check).
$mzPatch = Join-Path $CodeRoot 'tools\combo\minimapzoom\core-minimap.patch'
if (Test-Path -LiteralPath $mzPatch) {
    $prev = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    & git -C $Core apply --ignore-whitespace --whitespace=nowarn -R --check $mzPatch 2>$null
    $mzDone = ($LASTEXITCODE -eq 0)
    $ErrorActionPreference = $prev
    if (-not $mzDone) { GitC $Core @('apply', '--ignore-whitespace', '--whitespace=nowarn', $mzPatch) | Out-Null; Say 'applied core-minimap.patch' }
}

# ---------------------------------------------------------------- report
$report = [ordered]@{
    root = $Root
    codeHead = @(GitC $CodeRoot @('rev-parse', 'HEAD'))[0]
    r2portMerged = @(GitC $CodeRoot @('rev-parse', 'r2port/feat'))[0]
    coreBase = @(GitC $Core @('rev-parse', 'HEAD'))[0]
    userCoreHead = @(GitC $UserCore @('rev-parse', 'HEAD'))[0]
    stagedUtc = (Get-Date).ToUniversalTime().ToString('o')
    files = [ordered]@{}
}
foreach ($rel in @('r1delta.exe', 'Titanfall.exe', 'R1Delta_DS.exe', 'r1delta\bin_delta\tier0.dll', 'r1delta\bin_delta\materialsystem_nodx.dll',
        'r1delta\scripts\vscripts\movement\pilot.nut', 'r1delta\scripts\vscripts\movement\pilot_client.nut', 'r1delta\scripts\vscripts\movement\pilot_server.nut',
        'r1delta\scripts\vscripts\movement\slide_body.nut', 'r1delta\scripts\vscripts\movement\grapple.nut', 'r1delta\scripts\vscripts\ui\menu_credits.nut',
        'r1delta\scripts\r1_delta_slide_sounds.txt', 'r1delta\scripts\game_sounds_manifest.txt')) {
    $report.files[$rel] = Sha (Join-Path $Root $rel)
}
$json = $report | ConvertTo-Json -Depth 4
[IO.File]::WriteAllText($state, $json, [Text.UTF8Encoding]::new($false))
Say 'done'
$json
