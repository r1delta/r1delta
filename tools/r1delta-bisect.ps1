#requires -Version 5.1
<#
.SYNOPSIS
  R1Delta version launcher / bisection tool.

.DESCRIPTION
  Downloads any released R1Delta build (v2.1.2 .. latest) as a self-contained
  nupkg extraction and runs it without touching the installed game directory.
  The only shared state is the player profile under
  "%USERPROFILE%\Saved Games\Respawn\R1Delta" (plus the legacy
  Documents\Respawn\R1Delta copy), which is backed up before launching an old
  build and restored afterwards because profiles are not compatible across
  versions.

  Bisect mode performs a binary search between a known-good and a known-bad
  version for issues such as "game crashes on load", launching each candidate
  and classifying it by whether the game stays alive for the probe window.

.EXAMPLE
  .\r1delta-bisect.ps1 -Command List
  .\r1delta-bisect.ps1 -Command Install -Version v2.5.0
  .\r1delta-bisect.ps1 -Command Launch -Version v2.5.0
  .\r1delta-bisect.ps1 -Command Bisect -Good v2.1.2 -Bad latest
  .\r1delta-bisect.ps1 -Command Bisect -Good v2.1.2 -Bad v2.13.14 -Server -ProbeSeconds 90
  .\r1delta-bisect.ps1 -Command RestoreProfile
#>
[CmdletBinding()]
param(
    [ValidateSet('List', 'Install', 'Launch', 'Bisect', 'RestoreProfile')]
    [string]$Command = 'List',
    [string]$Version,
    [string]$Good = 'v2.1.2',
    [string]$Bad = 'latest',
    [switch]$Server,
    [int]$ProbeSeconds = 60,
    [string]$CacheDir = (Join-Path ([System.IO.Path]::GetTempPath()) 'r1delta-bisect'),
    [string]$Port = '5555',
    [switch]$KeepProfile,
    [string]$ExtraArgs = ''
)

$ErrorActionPreference = 'Stop'
$repo = 'r1delta/r1delta'
$minVersion = [version]'2.1.2'

function Write-Step([string]$Message) { Write-Host "==> $Message" -ForegroundColor Cyan }
function Write-Ok([string]$Message) { Write-Host "    $Message" -ForegroundColor Green }
function Write-Warn([string]$Message) { Write-Host "    $Message" -ForegroundColor Yellow }

function Get-GitHubVersions {
    # Prefer sources that are not subject to the GitHub API rate limit
    # (60 req/hr per IP). The asset URL for a full package is deterministic
    # once the tag is known.
    $tags = @()
    try {
        $raw = & git ls-remote --tags "https://github.com/$repo.git" 2>$null
        foreach ($line in $raw) {
            if ($line -match 'refs/tags/(v\d+\.\d+\.\d+)$') { $tags += $Matches[1] }
        }
    } catch { }
    if ($tags.Count -eq 0) {
        try {
            $tags = @()
            $re = [regex]'/r1delta/r1delta/releases/tag/(v\d+\.\d+\.\d+)'
            for ($page = 1; $page -le 6; $page++) {
                $html = (Invoke-WebRequest -UseBasicParsing -Uri "https://github.com/$repo/releases?page=$page" -Headers @{ 'User-Agent' = 'curl/8.4.0' }).Content
                $any = $false
                foreach ($m in $re.Matches($html)) { $tags += $m.Groups[1].Value; $any = $true }
                if (-not $any) { break }
            }
        } catch { }
    }
    if ($tags.Count -eq 0) {
        try {
            $page = 1
            do {
                $url = "https://api.github.com/repos/$repo/releases?per_page=100&page=$page"
                $releases = Invoke-RestMethod -Uri $url -Headers @{ 'User-Agent' = 'curl/8.4.0' } -ErrorAction Stop
                foreach ($r in $releases) {
                    if ($r.tag_name -match '^v\d+\.\d+\.\d+$') { $tags += $r.tag_name }
                }
                $page++
            } while ($releases.Count -eq 100)
        } catch { }
    }

    $versions = @()
    foreach ($tag in ($tags | Sort-Object -Unique)) {
        $ver = [version]($tag.Substring(1))
        if ($ver -lt $minVersion) { continue }
        $name = "R1Delta-$ver-full.nupkg"
        $versions += [pscustomobject]@{
            Version = $ver
            Tag     = $tag
            Name    = $name
            Url     = "https://github.com/$repo/releases/download/$tag/$name"
        }
    }
    return ($versions | Sort-Object Version -Unique)
}

function Resolve-Version([string]$Tag, $Versions) {
    if ($Tag -eq 'latest') { return ($Versions | Select-Object -Last 1) }
    $match = $Versions | Where-Object { $_.Tag -eq $Tag }
    if (-not $match) { throw "Version '$Tag' not found or has no full package. Run -Command List." }
    return $match
}

function Get-PackagePath($Info) {
    $dest = Join-Path $CacheDir ($Info.Version.ToString())
    $nupkg = Join-Path $dest "$($Info.Name)"
    $extract = Join-Path $dest 'extract'
    if (-not (Test-Path (Join-Path $extract 'lib\net462\Titanfall.exe'))) {
        New-Item -ItemType Directory -Force -Path $dest | Out-Null
        if (-not (Test-Path $nupkg)) {
            Write-Step "Downloading $($Info.Name) ..."
            $attempt = 0
            while ($true) {
                $attempt++
                $part = "$nupkg.part"
                try {
                    Invoke-WebRequest -Uri $Info.Url -OutFile $part -UseBasicParsing
                    if (-not (Test-ZipFile $part)) {
                        throw "downloaded file is not a valid ZIP (truncated download)"
                    }
                    Move-Item -Force $part $nupkg
                    break
                }
                catch {
                    $is404 = $_.Exception.Response -and [int]$_.Exception.Response.StatusCode -eq 404
                    if ($is404) {
                        throw "No full package exists for $($Info.Tag) ($($Info.Name) not found)."
                    }
                    if ($attempt -ge 3) { throw }
                    Write-Warn "Download attempt $attempt failed ($($_.Exception.Message)); retrying ..."
                    Remove-Item -Force $part -ErrorAction SilentlyContinue
                }
            }
        }
        Write-Step "Extracting $($Info.Name) ..."
        if (Test-Path $extract) { Remove-Item -Recurse -Force $extract }
        try {
            Expand-Archive -Path $nupkg -DestinationPath $extract
        }
        catch {
            Write-Warn "Cached package is corrupt; re-downloading ..."
            Remove-Item -Force $nupkg -ErrorAction SilentlyContinue
            Invoke-WebRequest -Uri $Info.Url -OutFile $nupkg -UseBasicParsing
            if (-not (Test-ZipFile $nupkg)) { throw "Re-download of $($Info.Tag) is not a valid ZIP." }
            Expand-Archive -Path $nupkg -DestinationPath $extract
        }
        Write-Ok "Extracted to $extract\lib\net462"
    }
    return (Join-Path $extract 'lib\net462')
}

function Test-ZipFile([string]$Path) {
    try {
        $fs = [System.IO.File]::OpenRead($Path)
        try {
            if ($fs.Length -lt 4) { return $false }
            $magic = New-Object byte[] 4
            [void]$fs.Read($magic, 0, 4)
            return $magic[0] -eq 0x50 -and $magic[1] -eq 0x4B -and
                ($magic[2] -eq 0x03 -or $magic[2] -eq 0x05 -or $magic[2] -eq 0x07)
        }
        finally { $fs.Dispose() }
    }
    catch { return $false }
}

function Get-ProfilePaths {
    $paths = @()
    $saved = Join-Path $env:USERPROFILE 'Saved Games\Respawn\R1Delta'
    $legacy = Join-Path $env:USERPROFILE 'Documents\Respawn\R1Delta'
    if (Test-Path $saved) { $paths += $saved }
    if (Test-Path $legacy) { $paths += $legacy }
    return $paths
}

function Backup-Profile {
    if ($KeepProfile) { return }
    $backupRoot = Join-Path $CacheDir 'profile-backup'
    New-Item -ItemType Directory -Force -Path $backupRoot | Out-Null
    foreach ($p in (Get-ProfilePaths)) {
        $name = Split-Path $p -Leaf
        $target = Join-Path $backupRoot "$name-$(Get-Date -Format 'yyyyMMdd_HHmmss')"
        Write-Step "Backing up profile $p -> $target"
        robocopy $p $target /MIR /NFL /NDL /NJH /NJS | Out-Null
    }
}

function Restore-Profile {
    if ($KeepProfile) { return }
    $backupRoot = Join-Path $CacheDir 'profile-backup'
    if (-not (Test-Path $backupRoot)) { Write-Warn "No profile backup found."; return }
    foreach ($dir in (Get-ChildItem $backupRoot -Directory)) {
        $name = $dir.Name -replace '-.*$', ''
        $live = Join-Path $env:USERPROFILE "Saved Games\Respawn\$name"
        if (Test-Path $live) {
            Write-Step "Restoring profile $($dir.FullName) -> $live"
            robocopy $dir.FullName $live /MIR /NFL /NDL /NJH /NJS | Out-Null
        }
    }
}

function Get-ClientArgs($GameDir) {
    return @('-novid', '-dev', '-windowed', '-noborder', '-game', $GameDir)
}

function Get-ServerArgs($GameDir) {
    return @('-console', '-dev', '-novid', '-port', $Port, '+hostport', $Port, '+map', 'mp_airbase', '-game', $GameDir)
}

function Get-CacheGameProcesses {
    Get-Process -ErrorAction SilentlyContinue | Where-Object {
        try { $_.Path -and $_.Path.StartsWith($CacheDir, [System.StringComparison]::OrdinalIgnoreCase) } catch { $false }
    }
}

function Stop-CacheGameProcesses {
    Get-CacheGameProcesses | ForEach-Object {
        try { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue } catch { }
    }
    Start-Sleep -Seconds 2
}

function Test-CandidateAlive($Info, $GameDir, [int]$Seconds) {
    $exe = if ($Server) { Join-Path $GameDir 'R1Delta_DS.exe' } else { Join-Path $GameDir 'Titanfall.exe' }
    $args = if ($Server) { Get-ServerArgs $GameDir } else { Get-ClientArgs $GameDir }
    if ($ExtraArgs) { $args += $ExtraArgs.Split(' ', [System.StringSplitOptions]::RemoveEmptyEntries) }

    Write-Step "Probing $($Info.Tag) ($($Info.Version)) ..."
    # The launched exe may be a stub that chains to the real game process, so
    # the started process exiting is not a liveness signal. Kill leftovers from
    # previous candidates first (only builds under our cache dir).
    Stop-CacheGameProcesses
    $p = Start-Process -FilePath $exe -ArgumentList $args -WorkingDirectory $GameDir -PassThru
    try {
        if (-not ('R1DeltaBisect.Native' -as [type])) {
            Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
namespace R1DeltaBisect {
    public static class Native {
        [StructLayout(LayoutKind.Sequential)]
        public struct COPYDATASTRUCT { public IntPtr dwData; public int cbData; public IntPtr lpData; }
        public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        public static extern IntPtr FindWindow(string lpClassName, string lpWindowName);
        [DllImport("user32.dll", SetLastError = true)]
        public static extern IntPtr SendMessageTimeout(IntPtr hWnd, uint msg, IntPtr wParam, ref COPYDATASTRUCT lParam, uint flags, uint timeout, out IntPtr result);
        [DllImport("user32.dll")]
        public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        public static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);
        [DllImport("user32.dll")]
        public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
        public static IntPtr FindEngineErrorDialog() {
            IntPtr found = IntPtr.Zero;
            EnumWindows(delegate(IntPtr h, IntPtr l) {
                StringBuilder sb = new StringBuilder(256);
                GetWindowText(h, sb, sb.Capacity);
                if (sb.ToString().IndexOf("Engine Error", StringComparison.OrdinalIgnoreCase) >= 0) { found = h; return false; }
                return true;
            }, IntPtr.Zero);
            return found;
        }
        public const uint WM_COPYDATA = 0x004A;
        public const uint WM_CLOSE = 0x0010;
        public const uint SMTO_ABORTIFHUNG = 0x0002;
    }
}
'@
        }

        if ($Server) {
            $waited = 0
            while ($waited -lt $Seconds) {
                if (Get-CacheGameProcesses) { break }
                Start-Sleep -Seconds 1
                $waited++
            }
            if ($waited -ge $Seconds) {
                Write-Warn "$($Info.Tag) showed no server process within $Seconds s -> BAD"
                return $false
            }
            Start-Sleep -Seconds $Seconds
            if (-not (Get-CacheGameProcesses)) {
                Write-Warn "$($Info.Tag) server exited -> BAD"
                return $false
            }
            Write-Ok "$($Info.Tag) server stayed alive -> GOOD"
            return $true
        }

        $hwnd = [IntPtr]::Zero
        $waited = 0
        while ($waited -lt $Seconds) {
            $hwnd = [R1DeltaBisect.Native]::FindWindow('Respawn001', $null)
            if ($hwnd -ne [IntPtr]::Zero) { break }
            Start-Sleep -Seconds 1
            $waited++
        }
        if ($hwnd -eq [IntPtr]::Zero) {
            Write-Warn "$($Info.Tag) showed no Respawn001 window within $Seconds s -> BAD (did not finish loading)"
            return $false
        }

        Write-Host "    Respawn001 window appeared; waiting 15s for the engine to initialize ..."
        Start-Sleep -Seconds 15

        $cmd = 'playlist private_match'
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($cmd)
        $mem = [System.Runtime.InteropServices.Marshal]::AllocHGlobal($bytes.Length + 1)
        [System.Runtime.InteropServices.Marshal]::Copy($bytes, 0, $mem, $bytes.Length)
        [System.Runtime.InteropServices.Marshal]::WriteByte($mem, $bytes.Length, 0)
        $cds = New-Object R1DeltaBisect.Native+COPYDATASTRUCT
        $cds.dwData = [IntPtr]::Zero
        $cds.cbData = $bytes.Length + 1
        $cds.lpData = $mem
        try {
            $result = [IntPtr]::Zero
            $ret = [R1DeltaBisect.Native]::SendMessageTimeout($hwnd, [R1DeltaBisect.Native]::WM_COPYDATA, [IntPtr]::Zero, [ref]$cds, [R1DeltaBisect.Native]::SMTO_ABORTIFHUNG, 5000, [ref]$result)
            if ($ret -eq [IntPtr]::Zero) {
                Write-Warn "$($Info.Tag) did not process playlist private_match within 5s -> BAD (hung)"
                return $false
            }
        }
        finally {
            [System.Runtime.InteropServices.Marshal]::FreeHGlobal($mem)
        }
        Write-Ok "$($Info.Tag) accepted playlist private_match"

        $cmd = 'map mp_lobby'
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($cmd)
        $mem = [System.Runtime.InteropServices.Marshal]::AllocHGlobal($bytes.Length + 1)
        [System.Runtime.InteropServices.Marshal]::Copy($bytes, 0, $mem, $bytes.Length)
        [System.Runtime.InteropServices.Marshal]::WriteByte($mem, $bytes.Length, 0)
        $cds = New-Object R1DeltaBisect.Native+COPYDATASTRUCT
        $cds.dwData = [IntPtr]::Zero
        $cds.cbData = $bytes.Length + 1
        $cds.lpData = $mem
        try {
            $result = [IntPtr]::Zero
            $ret = [R1DeltaBisect.Native]::SendMessageTimeout($hwnd, [R1DeltaBisect.Native]::WM_COPYDATA, [IntPtr]::Zero, [ref]$cds, [R1DeltaBisect.Native]::SMTO_ABORTIFHUNG, 5000, [ref]$result)
            if ($ret -eq [IntPtr]::Zero) {
                Write-Warn "$($Info.Tag) did not process map mp_lobby within 5s -> BAD (hung)"
                return $false
            }
        }
        finally {
            [System.Runtime.InteropServices.Marshal]::FreeHGlobal($mem)
        }
        Write-Ok "$($Info.Tag) accepted map mp_lobby"

        Write-Host "    commands accepted; waiting 15s for the map to settle ..."
        Start-Sleep -Seconds 15

        $cmd = 'error_test'
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($cmd)
        $mem = [System.Runtime.InteropServices.Marshal]::AllocHGlobal($bytes.Length + 1)
        [System.Runtime.InteropServices.Marshal]::Copy($bytes, 0, $mem, $bytes.Length)
        [System.Runtime.InteropServices.Marshal]::WriteByte($mem, $bytes.Length, 0)
        $cds = New-Object R1DeltaBisect.Native+COPYDATASTRUCT
        $cds.dwData = [IntPtr]::Zero
        $cds.cbData = $bytes.Length + 1
        $cds.lpData = $mem
        try {
            $result = [IntPtr]::Zero
            $ret = [R1DeltaBisect.Native]::SendMessageTimeout($hwnd, [R1DeltaBisect.Native]::WM_COPYDATA, [IntPtr]::Zero, [ref]$cds, [R1DeltaBisect.Native]::SMTO_ABORTIFHUNG, 5000, [ref]$result)
            if ($ret -eq [IntPtr]::Zero) {
                Write-Warn "$($Info.Tag) did not process error_test within 5s -> BAD (hung)"
                return $false
            }
        }
        finally {
            [System.Runtime.InteropServices.Marshal]::FreeHGlobal($mem)
        }

        Write-Ok "$($Info.Tag) accepted error_test; waiting for the Engine Error dialog ..."
        $waited = 0
        $dlg = [IntPtr]::Zero
        while ($waited -lt 15) {
            $dlg = [R1DeltaBisect.Native]::FindEngineErrorDialog()
            if ($dlg -ne [IntPtr]::Zero) { break }
            Start-Sleep -Seconds 1
            $waited++
        }
        if ($dlg -eq [IntPtr]::Zero) {
            Write-Warn "$($Info.Tag) no Engine Error dialog within 15s -> BAD"
            return $false
        }
        [void][R1DeltaBisect.Native]::PostMessage($dlg, [R1DeltaBisect.Native]::WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero)
        Write-Ok "$($Info.Tag) Engine Error dialog appeared -> GOOD"
        return $true
    }
    finally {
        Stop-CacheGameProcesses
    }
}

function Show-List($Versions) {
    Write-Step "Available builds ($($Versions.Count)):"
    $Versions | ForEach-Object {
        Write-Host ("  {0,-10} {1}" -f $_.Tag, $_.Name)
    }
}

function Start-Bisect($Versions) {
    $good = Resolve-Version $Good $Versions
    $bad = Resolve-Version $Bad $Versions
    if ($good.Version -ge $bad.Version) { throw "Good ($Good) must be older than Bad ($Bad)." }

    Backup-Profile
    try {
        $lo = $good.Version
        $hi = $bad.Version
        $loTag = $good.Tag
        $hiTag = $bad.Tag

        Write-Step "Bisecting [$loTag .. $hiTag] (probe $ProbeSeconds s, server=$Server)"
        while ($true) {
            $candidates = $Versions | Where-Object { $_.Version -gt $lo -and $_.Version -lt $hi }
            if ($candidates.Count -eq 0) { break }
            $mid = $candidates[[int][Math]::Floor(($candidates.Count - 1) / 2)]

            try {
                $gameDir = Get-PackagePath $mid
            }
            catch {
                Write-Warn "  $($mid.Tag) unavailable ($($_.Exception.Message)); excluding it from the search"
                $versions = $versions | Where-Object { $_.Tag -ne $mid.Tag }
                continue
            }
            if (Test-CandidateAlive $mid $gameDir $ProbeSeconds) {
                $lo = $mid.Version; $loTag = $mid.Tag
                Write-Ok "  -> new good bound: $loTag"
            }
            else {
                $hi = $mid.Version; $hiTag = $mid.Tag
                Write-Warn "  -> new bad bound: $hiTag"
            }
        }

        Write-Host ""
        Write-Step "RESULT: first bad version is $hiTag (last good: $loTag)"
        Write-Host "  Reproduce with:  .\r1delta-bisect.ps1 -Command Launch -Version $hiTag"
        Write-Host "  Confirm good with: .\r1delta-bisect.ps1 -Command Launch -Version $loTag"
    }
    finally {
        Restore-Profile
    }
}

# ---------------------------------------------------------------------------
$versions = Get-GitHubVersions
if ($versions.Count -eq 0) { throw 'No released versions with full packages found.' }

switch ($Command) {
    'List' { Show-List $versions }

    'Install' {
        if (-not $Version) { throw 'Install requires -Version.' }
        $info = Resolve-Version $Version $versions
        $gameDir = Get-PackagePath $info
        Write-Ok "Ready. Run manually with:  $gameDir\r1delta.exe"
        Write-Ok "Automated launch:  .\r1delta-bisect.ps1 -Command Launch -Version $Version"
    }

    'Launch' {
        if (-not $Version) { throw 'Launch requires -Version.' }
        $info = Resolve-Version $Version $versions
        $gameDir = Get-PackagePath $info
        Backup-Profile
        try {
            $exe = if ($Server) { Join-Path $gameDir 'R1Delta_DS.exe' } else { Join-Path $gameDir 'Titanfall.exe' }
            $args = if ($Server) { Get-ServerArgs $gameDir } else { Get-ClientArgs $gameDir }
            if ($ExtraArgs) { $args += $ExtraArgs.Split(' ', [System.StringSplitOptions]::RemoveEmptyEntries) }
            Write-Step "Launching $($info.Tag): $exe $($args -join ' ')"
            Start-Process -FilePath $exe -ArgumentList $args -WorkingDirectory $gameDir
            Write-Ok "Launched. The profile will be restored when you run -Command RestoreProfile."
        }
        catch {
            Restore-Profile
            throw
        }
    }

    'Bisect' { Start-Bisect $versions }

    'RestoreProfile' { Restore-Profile }
}
