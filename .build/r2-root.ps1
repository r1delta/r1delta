[CmdletBinding()]
param(
    # r2dev is the golden template/staging reference. The four agent roots are the only playable test roots.
    # The user's play root (r2) is deliberately not accepted anywhere in this script.
    [Alias('Root')]
    [string] $RootName,

    [ValidateSet('r2dev')]
    [string] $From = 'r2dev',

    [switch] $Create,
    [switch] $Stage,
    [switch] $Verify,
    [switch] $Status,
    [switch] $List,
    [switch] $Remove,
    [switch] $Reset,
    [switch] $Repair
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$CodeRoot = Split-Path -Parent $PSScriptRoot
$GameRoot = 'S:\game'
$PhysicalGameRoot = 'C:\Users\r3muxd\newr1root3\game'
$SourceLaunchRoot = Join-Path $GameRoot 'mp'
$SourceCoreRoot = Join-Path $GameRoot 'r1delta'
$UserPlayCoreRoot = Join-Path (Join-Path $GameRoot 'r2') 'r1delta'
$TemplateName = $From
$AgentRootNames = @('r2-provider', 'r2-texlib', 'r2-shader', 'r2-lead')
$TemplateRoot = Join-Path $GameRoot $TemplateName
$TemplateCoreRoot = Join-Path $TemplateRoot 'r1delta'
$Pin = '316993bd0142e706ea5386943530f2b8d2c711dd'
$BuildRoot = Join-Path $PSScriptRoot 'release'
$VerifyScript = Join-Path $CodeRoot 'ci\verify_release_payload.ps1'
$LockDirectory = 'C:\Users\r3muxd\tmp\r2port\locks'
$GraphSourceRoot = 'C:\Users\r3muxd\tmp\r2port\ainconv\out'
$MinimumFreeBytes = [long] 3GB
$ExcludedExtensions = [string[]] @('.i64', '.idb')
# Existing files in these directories are NTFS hardlinks to the pristine template CORE. The directories themselves are real.
$LargeCoreDirectories = @('bin', 'bin_nexon', 'materials', 'models', 'sound', 'vgui', 'cfg', 'config', 'admin')
# The template may expose only these as junctions to the identical source-CORE directory (resolved read-only).
$ReadOnlySharedCoreDirectories = @('bin_nexon', 'vgui')
$AlwaysCheckoutDirectories = @('scripts', 'resource', 'maps')
$RequiredRealDirectories = @('bin', 'materials', 'models', 'shaders', 'maps', 'scripts', 'resource', 'sound', 'cfg', 'config', 'admin')
$WriteProbeDirectories = @('bin', 'bin_delta', 'materials', 'models', 'shaders', 'maps', 'scripts', 'resource', 'sound', 'cfg', 'config', 'admin', 'r2port', 'r2port\cache', 'r2port_cache')
$LocalExcludePatterns = @(
    # Intentional per-root runtime additions are untracked and stay out of the pinned clone's status.
    '/bin/',
    '/bin_delta/',
    '/bin_nexon/',
    '/materials/',
    '/models/',
    '/sound/',
    '/vgui/',
    '/cfg/',
    '/config/',
    '/admin/',
    '/shaders/',
    '/r2port/',
    '/r2port_cache/',
    '/maps/graphs/mp_glitch.ain',
    '/maps/graphs/mp_lf_stacks.ain',
    '/r2port-loose-materials.manifest.json'
)
$SampleBlobPaths = @(
    'GameInfo.txt',
    'playlists.txt',
    'scripts/vscripts/_mapspawn.nut',
    'scripts/vscripts/mp/_control_panel.nut'
)
$GraphNames = @('mp_glitch.ain', 'mp_lf_stacks.ain')
$StubNames = @('d3d11.dll', 'GFSDK_SSAO.win64.dll', 'GFSDK_TXAA.win64.dll')
$RootExecutables = @('Titanfall.exe', 'R1Delta_DS.exe', 'r1delta.exe')
$RootJunctions = @('bin', 'r1', 'tools')
$script:RootLock = $null

$selectedModes = @($Create, $Stage, $Verify, $Status, $List, $Remove, $Reset, $Repair | Where-Object { $_ }).Count
if ($selectedModes -ne 1) {
    throw 'Select exactly one of -Create, -Stage, -Verify, -Status, -List, -Remove, -Reset, or -Repair.'
}

$Root = $null
$CoreRoot = $null
if (-not $List) {
    if ([string]::IsNullOrWhiteSpace($RootName)) {
        throw "Specify -Root with one of: $($AgentRootNames -join ', '), or the template $TemplateName (-Stage/-Verify/-Status only)."
    }
    if ($RootName -ieq 'r2') {
        throw "-Root r2 is the user's play root and is off limits to this script."
    }
    $canonicalName = @(@($AgentRootNames) + @($TemplateName) | Where-Object { $_ -ieq $RootName } | Select-Object -First 1)
    if ($canonicalName.Count -eq 0) {
        throw "Unknown root '$RootName'. Supported roots: $($AgentRootNames -join ', '); template: $TemplateName."
    }
    $RootName = [string] $canonicalName[0]
    if ($RootName -eq $TemplateName -and ($Create -or $Remove -or $Reset -or $Repair)) {
        throw "$TemplateName is the golden template; -Create, -Remove, -Reset, and -Repair only operate on agent roots ($($AgentRootNames -join ', '))."
    }
    if ($PSBoundParameters.ContainsKey('From') -and -not ($Create -or $Reset)) {
        throw '-From is only meaningful with -Create or -Reset.'
    }
    $Root = Join-Path $GameRoot $RootName
    $CoreRoot = Join-Path $Root 'r1delta'
}
$IsTemplate = ($RootName -eq $TemplateName)

if (-not ('R2Native' -as [type])) {
    Add-Type -Language CSharp -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;

public sealed class R2FileIdentity
{
    public string Id;
    public uint Links;
    public long Length;
}

public sealed class R2TreeResult
{
    public int Created;
    public int Existing;
    public int Excluded;
}

public sealed class R2TreeCompare
{
    public List<string> Missing = new List<string>();
    public List<string> Different = new List<string>();
    public int Matched;
}

public sealed class R2TreeScan
{
    public List<string> ReparsePoints = new List<string>();
    public List<string> ExcludedFiles = new List<string>();
    public List<string> SharedFiles = new List<string>();
    public long PrivateBytes;
    public long SharedBytes;
    public int Files;
}

public static class R2Native
{
    private const uint FILE_SHARE_ALL = 7;
    private const uint OPEN_EXISTING = 3;
    private const uint FILE_FLAG_BACKUP_SEMANTICS = 0x02000000;
    private const uint FILE_FLAG_OPEN_REPARSE_POINT = 0x00200000;
    private static readonly IntPtr InvalidHandle = new IntPtr(-1);

    [StructLayout(LayoutKind.Sequential)]
    private struct BY_HANDLE_FILE_INFORMATION
    {
        public uint FileAttributes;
        public uint CreationTimeLow;
        public uint CreationTimeHigh;
        public uint LastAccessTimeLow;
        public uint LastAccessTimeHigh;
        public uint LastWriteTimeLow;
        public uint LastWriteTimeHigh;
        public uint VolumeSerialNumber;
        public uint FileSizeHigh;
        public uint FileSizeLow;
        public uint NumberOfLinks;
        public uint FileIndexHigh;
        public uint FileIndexLow;
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CreateHardLinkW(string lpFileName, string lpExistingFileName, IntPtr lpSecurityAttributes);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr CreateFileW(string lpFileName, uint dwDesiredAccess, uint dwShareMode, IntPtr lpSecurityAttributes, uint dwCreationDisposition, uint dwFlagsAndAttributes, IntPtr hTemplateFile);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetFileInformationByHandle(IntPtr hFile, out BY_HANDLE_FILE_INFORMATION lpFileInformation);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CloseHandle(IntPtr hObject);

    public static bool IsExcluded(string name, string[] excluded)
    {
        if (excluded == null) { return false; }
        for (int index = 0; index < excluded.Length; index++)
        {
            if (name.EndsWith(excluded[index], StringComparison.OrdinalIgnoreCase)) { return true; }
        }
        return false;
    }

    // Opens the entry itself (never follows a reparse point) and returns volume/file-index identity plus link count.
    public static R2FileIdentity GetIdentity(string path)
    {
        IntPtr handle = CreateFileW(path, 0, FILE_SHARE_ALL, IntPtr.Zero, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, IntPtr.Zero);
        if (handle == InvalidHandle)
        {
            throw new Win32Exception(Marshal.GetLastWin32Error(), "Cannot open '" + path + "'");
        }
        try
        {
            BY_HANDLE_FILE_INFORMATION info;
            if (!GetFileInformationByHandle(handle, out info))
            {
                throw new Win32Exception(Marshal.GetLastWin32Error(), "Cannot query '" + path + "'");
            }
            R2FileIdentity identity = new R2FileIdentity();
            identity.Id = info.VolumeSerialNumber.ToString("x8") + ":" + info.FileIndexHigh.ToString("x8") + info.FileIndexLow.ToString("x8");
            identity.Links = info.NumberOfLinks;
            identity.Length = ((long)info.FileSizeHigh << 32) | (long)info.FileSizeLow;
            return identity;
        }
        finally
        {
            CloseHandle(handle);
        }
    }

    private static bool PathExists(string path)
    {
        return File.Exists(path) || Directory.Exists(path);
    }

    private static bool IsReparsePoint(FileSystemInfo entry)
    {
        return (entry.Attributes & FileAttributes.ReparsePoint) != 0;
    }

    // Creates destination as a hardlink of source. Returns false (and leaves the file alone) when destination already exists.
    public static bool LinkFile(string source, string destination)
    {
        if (PathExists(destination)) { return false; }
        if (!CreateHardLinkW(destination, source, IntPtr.Zero))
        {
            throw new Win32Exception(Marshal.GetLastWin32Error(), "Cannot hardlink '" + source + "' -> '" + destination + "'");
        }
        return true;
    }

    public static R2TreeResult LinkTree(string source, string destination, string[] excluded)
    {
        R2TreeResult result = new R2TreeResult();
        TransferInto(new DirectoryInfo(source), destination, excluded, true, result);
        return result;
    }

    public static R2TreeResult CopyTree(string source, string destination, string[] excluded)
    {
        R2TreeResult result = new R2TreeResult();
        TransferInto(new DirectoryInfo(source), destination, excluded, false, result);
        return result;
    }

    private static void TransferInto(DirectoryInfo source, string destination, string[] excluded, bool link, R2TreeResult result)
    {
        if (File.Exists(destination))
        {
            throw new IOException("Destination is a file, expected a directory: " + destination);
        }
        if (Directory.Exists(destination))
        {
            if (IsReparsePoint(new DirectoryInfo(destination)))
            {
                throw new IOException("Refusing to write through a reparse point: " + destination);
            }
        }
        else
        {
            Directory.CreateDirectory(destination);
        }
        foreach (FileSystemInfo entry in source.EnumerateFileSystemInfos())
        {
            if (IsReparsePoint(entry))
            {
                throw new IOException("Refusing to follow a reparse point in the source tree: " + entry.FullName);
            }
            string target = Path.Combine(destination, entry.Name);
            DirectoryInfo child = entry as DirectoryInfo;
            if (child != null)
            {
                TransferInto(child, target, excluded, link, result);
                continue;
            }
            if (IsExcluded(entry.Name, excluded))
            {
                result.Excluded++;
                continue;
            }
            if (PathExists(target))
            {
                result.Existing++;
                continue;
            }
            if (link)
            {
                LinkFile(entry.FullName, target);
            }
            else
            {
                File.Copy(entry.FullName, target, false);
            }
            result.Created++;
        }
    }

    // 0 = same file (hardlink identity), 1 = destination missing, 2 = destination is a different file.
    public static int CompareFile(string source, string destination)
    {
        if (!File.Exists(destination)) { return 1; }
        return GetIdentity(source).Id == GetIdentity(destination).Id ? 0 : 2;
    }

    public static R2TreeCompare CompareTree(string source, string destination, string[] excluded)
    {
        R2TreeCompare result = new R2TreeCompare();
        CompareInto(new DirectoryInfo(source), destination, excluded, result);
        return result;
    }

    private static void CompareInto(DirectoryInfo source, string destination, string[] excluded, R2TreeCompare result)
    {
        foreach (FileSystemInfo entry in source.EnumerateFileSystemInfos())
        {
            if (IsReparsePoint(entry))
            {
                throw new IOException("Reparse point in the source tree: " + entry.FullName);
            }
            string target = Path.Combine(destination, entry.Name);
            DirectoryInfo child = entry as DirectoryInfo;
            if (child != null)
            {
                if (!Directory.Exists(target)) { result.Missing.Add(target); continue; }
                CompareInto(child, target, excluded, result);
                continue;
            }
            if (IsExcluded(entry.Name, excluded)) { continue; }
            switch (CompareFile(entry.FullName, target))
            {
                case 0: result.Matched++; break;
                case 1: result.Missing.Add(target); break;
                default: result.Different.Add(target); break;
            }
        }
    }

    // Walks a tree without descending into reparse points.
    public static R2TreeScan Scan(string root, string[] excluded)
    {
        R2TreeScan scan = new R2TreeScan();
        ScanInto(new DirectoryInfo(root), excluded, scan);
        return scan;
    }

    private static void ScanInto(DirectoryInfo directory, string[] excluded, R2TreeScan scan)
    {
        foreach (FileSystemInfo entry in directory.EnumerateFileSystemInfos())
        {
            if (IsReparsePoint(entry))
            {
                scan.ReparsePoints.Add(entry.FullName);
                continue;
            }
            DirectoryInfo child = entry as DirectoryInfo;
            if (child != null)
            {
                ScanInto(child, excluded, scan);
                continue;
            }
            R2FileIdentity identity;
            try
            {
                identity = GetIdentity(entry.FullName);
            }
            catch (Win32Exception ex)
            {
                if (ex.NativeErrorCode == 2 || ex.NativeErrorCode == 3) { continue; }
                throw;
            }
            scan.Files++;
            if (IsExcluded(entry.Name, excluded)) { scan.ExcludedFiles.Add(entry.FullName); }
            if (identity.Links > 1)
            {
                scan.SharedFiles.Add(entry.FullName);
                scan.SharedBytes += identity.Length;
            }
            else
            {
                scan.PrivateBytes += identity.Length;
            }
        }
    }

    private const uint DELETE_ACCESS = 0x00010000;
    private const int FileDispositionInfoEx = 21;
    private const uint FILE_DISPOSITION_FLAG_DELETE = 0x00000001;
    private const uint FILE_DISPOSITION_FLAG_IGNORE_READONLY_ATTRIBUTE = 0x00000010;

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetFileInformationByHandle(IntPtr hFile, int fileInformationClass, ref uint lpFileInformation, uint dwBufferSize);

    // Returns 0 on success, otherwise the Win32 error. Opens the directory entry itself (no reparse following)
    // and marks it for deletion with IGNORE_READONLY_ATTRIBUTE; the read-only attribute is never modified.
    private static int DeleteIgnoringReadOnly(string path)
    {
        IntPtr handle = CreateFileW(path, DELETE_ACCESS, FILE_SHARE_ALL, IntPtr.Zero, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, IntPtr.Zero);
        if (handle == InvalidHandle) { return Marshal.GetLastWin32Error(); }
        try
        {
            uint flags = FILE_DISPOSITION_FLAG_DELETE | FILE_DISPOSITION_FLAG_IGNORE_READONLY_ATTRIBUTE;
            if (!SetFileInformationByHandle(handle, FileDispositionInfoEx, ref flags, 4)) { return Marshal.GetLastWin32Error(); }
            return 0;
        }
        finally
        {
            CloseHandle(handle);
        }
    }

    // Deletes a tree. Reparse points (junctions/symlinks) are unlinked, never followed. Hardlinked files are only unlinked.
    public static void RemoveTree(string path)
    {
        FileSystemInfo item;
        if (Directory.Exists(path)) { item = new DirectoryInfo(path); }
        else if (File.Exists(path)) { item = new FileInfo(path); }
        else { return; }
        RemoveEntry(item);
    }

    private static void RemoveEntry(FileSystemInfo item)
    {
        DirectoryInfo directory = item as DirectoryInfo;
        if (directory != null)
        {
            if (!IsReparsePoint(directory))
            {
                foreach (FileSystemInfo child in directory.GetFileSystemInfos())
                {
                    RemoveEntry(child);
                }
            }
            Directory.Delete(directory.FullName, false);
            return;
        }
        FileInfo file = (FileInfo)item;
        if ((file.Attributes & FileAttributes.ReadOnly) != 0)
        {
            // Unlink just this directory entry without touching the (possibly shared) file's attributes or content.
            int deleteError = DeleteIgnoringReadOnly(file.FullName);
            if (deleteError == 0) { return; }
            if (GetIdentity(file.FullName).Links > 1)
            {
                throw new IOException("Cannot unlink read-only shared hardlinked file '" + file.FullName + "': FileDispositionInfoEx with IGNORE_READONLY_ATTRIBUTE failed with Win32 error " + deleteError + " (requires Windows 10 1809+ on NTFS). Refusing to clear the read-only attribute because that would change the shared file.");
            }
            FileAttributes cleared = file.Attributes & ~FileAttributes.ReadOnly;
            if (cleared == 0) { cleared = FileAttributes.Normal; }
            file.Attributes = cleared;
        }
        file.Delete();
    }
}
'@
}

function Invoke-Git([string] $Directory, [string[]] $GitArgs) {
    $previousErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $output = & git -c core.quotepath=false -C $Directory @GitArgs 2>&1
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    if ($exitCode -ne 0) {
        throw "git -C '$Directory' $($GitArgs -join ' ') failed ($exitCode): $($output -join [Environment]::NewLine)"
    }
    return @($output | ForEach-Object { [string] $_ })
}

function Get-LinkType($Item) {
    $property = $Item.PSObject.Properties['LinkType']
    if ($null -eq $property) { return '' }
    return [string] $property.Value
}

function Test-EntryExists([string] $Path) {
    return $null -ne (Get-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue)
}

function Assert-RealDirectory([string] $Path) {
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if (-not $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Expected a real directory at $Path."
    }
}

function Assert-Junction([string] $Path, [string] $Target) {
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0 -or (Get-LinkType $item) -ne 'Junction') {
        throw "Expected a junction at $Path."
    }
    $targetProperty = $item.PSObject.Properties['Target']
    if ($null -eq $targetProperty -or @($targetProperty.Value).Count -eq 0) { throw "Junction target cannot be read: $Path" }
    $actualTarget = [IO.Path]::GetFullPath([string] @($targetProperty.Value)[0]).TrimEnd('\')
    $expectedTarget = [IO.Path]::GetFullPath($Target).TrimEnd('\')
    if (-not $actualTarget.Equals($expectedTarget, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Junction $Path targets '$actualTarget', expected '$expectedTarget'."
    }
}

function New-DirectoryJunction([string] $Path, [string] $Target) {
    if (Test-EntryExists $Path) { throw "Refusing to replace an existing path with a junction: $Path" }
    New-Item -ItemType Junction -Path $Path -Target $Target | Out-Null
}

function Get-CoreRootFor([string] $Name) {
    return Join-Path (Join-Path $GameRoot $Name) 'r1delta'
}

# Resolves a CORE big directory to a real directory path. Agent roots must have real directories. The golden template may
# expose bin_nexon/vgui as junctions to the identical directory of the source CORE; only those exact junctions are resolved to
# their targets (read-only use: hardlink source and hashing), anything else reparse-based is rejected.
function Get-RealCoreDirectory([string] $Core, [string] $Name) {
    $path = Join-Path $Core $Name
    $item = Get-Item -LiteralPath $path -Force -ErrorAction Stop
    if (-not $item.PSIsContainer) { throw "Expected a directory at $path." }
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) { return $path }
    if ($Core -ne $TemplateCoreRoot -or $ReadOnlySharedCoreDirectories -notcontains $Name -or (Get-LinkType $item) -ne 'Junction') {
        throw "Unexpected reparse point at ${path}; only the $TemplateName template may junction $($ReadOnlySharedCoreDirectories -join '/') to the source CORE."
    }
    $targetProperty = $item.PSObject.Properties['Target']
    if ($null -eq $targetProperty -or @($targetProperty.Value).Count -eq 0) { throw "Junction target cannot be read: $path" }
    $target = [IO.Path]::GetFullPath([string] @($targetProperty.Value)[0]).TrimEnd('\')
    $allowed = @(
        [IO.Path]::GetFullPath((Join-Path $SourceCoreRoot $Name)).TrimEnd('\'),
        [IO.Path]::GetFullPath((Join-Path (Join-Path $PhysicalGameRoot 'r1delta') $Name)).TrimEnd('\')
    )
    if ($allowed -notcontains $target) { throw "Template junction $path targets '$target', expected one of: $($allowed -join ', ')." }
    if (-not (Test-Path -LiteralPath $target -PathType Container)) { throw "Template junction target is missing: $target" }
    return $target
}

# ---------------------------------------------------------------------------
# Per-root serialization (separate roots never contend) and running-process guard
# ---------------------------------------------------------------------------

function Enter-RootLock([string] $Name) {
    New-Item -ItemType Directory -Path $LockDirectory -Force | Out-Null
    $lockPath = Join-Path $LockDirectory "$Name.lock"
    $stream = $null
    try {
        $stream = [IO.FileStream]::new($lockPath, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None, 4096, [IO.FileOptions]::DeleteOnClose)
        $bytes = [Text.UTF8Encoding]::new($false).GetBytes("r2-root.ps1 pid $PID started $([DateTimeOffset]::UtcNow.ToString('o'))" + [Environment]::NewLine)
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush()
    } catch [IO.IOException] {
        if ($null -ne $stream) { $stream.Dispose() }
        throw "Another r2-root.ps1 operation is already modifying $Name (lock '$lockPath'): $($_.Exception.Message)"
    }
    $script:RootLock = $stream
}

function Exit-RootLock {
    if ($null -ne $script:RootLock) {
        $script:RootLock.Dispose()
        $script:RootLock = $null
    }
}

function Get-AllProcesses {
    return @(Get-CimInstance Win32_Process -ErrorAction Stop)
}

function Get-RootProcesses([string] $Name, $Processes) {
    $prefixes = @(
        ([IO.Path]::GetFullPath((Join-Path $GameRoot $Name)).TrimEnd('\') + '\'),
        ([IO.Path]::GetFullPath((Join-Path $PhysicalGameRoot $Name)).TrimEnd('\') + '\')
    )
    return @($Processes | Where-Object {
        if (-not $_.ExecutablePath) { return $false }
        $path = [IO.Path]::GetFullPath($_.ExecutablePath)
        foreach ($prefix in $prefixes) {
            if ($path.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { return $true }
        }
        return $false
    })
}

function Assert-R2Idle {
    $running = @(Get-RootProcesses $RootName (Get-AllProcesses))
    if ($running.Count -ne 0) {
        $listing = ($running | ForEach-Object { "pid=$($_.ProcessId) path=$($_.ExecutablePath)" }) -join '; '
        throw "Refusing to modify $Root while processes from that root are running: $listing"
    }
}

function Assert-FreeSpace {
    $free = [IO.DriveInfo]::new('C:\').AvailableFreeSpace
    if ($free -lt $MinimumFreeBytes) {
        throw ('C: has {0:N2} GiB free; at least {1:N0} GiB is required before creating an agent root.' -f ($free / 1GB), ($MinimumFreeBytes / 1GB))
    }
}

# ---------------------------------------------------------------------------
# Template, pinned checkout, and sampled blob checks
# ---------------------------------------------------------------------------

function Get-RootStageNames {
    $names = [System.Collections.Generic.List[string]]::new()
    foreach ($name in $RootExecutables) { $names.Add($name) }
    $dedicatedDll = Join-Path $BuildRoot 'dedicated.dll'
    if (Test-Path -LiteralPath $dedicatedDll -PathType Leaf) { $names.Add('dedicated.dll') }
    return @($names.ToArray())
}

function Get-TrackedSamples([string] $Core) {
    $samples = [System.Collections.Generic.List[object]]::new()
    foreach ($name in $LargeCoreDirectories) {
        $entries = [System.Collections.Generic.List[object]]::new()
        foreach ($line in @(Invoke-Git $Core @('ls-tree', '-r', $Pin, '--', $name))) {
            if ($line -match '^\d+ blob ([0-9a-f]{40})\t(.+)$') {
                $entries.Add([pscustomobject]@{ Path = $Matches[2]; Blob = $Matches[1] })
            }
        }
        if ($entries.Count -eq 0) { continue }
        $indexes = @(0, [int][math]::Floor(($entries.Count - 1) / 2), ($entries.Count - 1)) | Select-Object -Unique
        foreach ($index in $indexes) { $samples.Add($entries[$index]) }
    }
    foreach ($relativePath in $SampleBlobPaths) {
        $blob = @(Invoke-Git $Core @('rev-parse', '--verify', "${Pin}:$relativePath"))
        if ($blob.Count -ne 1) { throw "Could not resolve pinned blob for $relativePath." }
        $samples.Add([pscustomobject]@{ Path = $relativePath; Blob = $blob[0] })
    }
    return @($samples.ToArray())
}

# Raw-hash sampled tracked files and compare against the pinned blobs. Catches accidental writes through a hardlink.
function Assert-TrackedSamples([string] $Core, [string] $Label) {
    $samples = @(Get-TrackedSamples $Core)
    $paths = @($samples | ForEach-Object {
        $parts = $_.Path.Split('/', 2)
        if ($parts.Count -eq 2 -and $LargeCoreDirectories -contains $parts[0]) {
            Join-Path (Get-RealCoreDirectory $Core $parts[0]) ($parts[1].Replace('/', '\'))
        } else {
            Join-Path $Core ($_.Path.Replace('/', '\'))
        }
    })
    $actual = @(Invoke-Git $Core (@('hash-object', '--no-filters', '--') + $paths))
    if ($actual.Count -ne $samples.Count) { throw "$Label`: hash-object returned $($actual.Count) hashes for $($samples.Count) sampled files." }
    $mismatches = [System.Collections.Generic.List[string]]::new()
    for ($index = 0; $index -lt $samples.Count; $index++) {
        if ($actual[$index] -ne $samples[$index].Blob) { $mismatches.Add($samples[$index].Path) }
    }
    if ($mismatches.Count -ne 0) {
        throw "$Label`: sampled file(s) no longer match the pinned Git blobs (write through a hardlink or local edit?): $($mismatches -join ', ')"
    }
    return $samples.Count
}

function Assert-PinnedClone([string] $Core, [string] $Label) {
    Assert-RealDirectory $Core
    Assert-RealDirectory (Join-Path $Core '.git')
    $head = @(Invoke-Git $Core @('rev-parse', 'HEAD'))
    if ($head.Count -ne 1 -or $head[0] -ne $Pin) { throw "$Label CORE HEAD is '$($head -join ',')', expected pinned commit $Pin." }
    $autocrlf = @(Invoke-Git $Core @('config', '--local', '--get', 'core.autocrlf'))
    if ($autocrlf.Count -ne 1 -or $autocrlf[0] -ne 'false') {
        throw "$Label CORE clone core.autocrlf is '$($autocrlf -join ',')', expected false to preserve the pinned raw bytes."
    }
    $commonDir = @(Invoke-Git $Core @('rev-parse', '--git-common-dir'))
    $commonPath = [IO.Path]::GetFullPath((Join-Path $Core $commonDir[0]))
    $corePrefix = [IO.Path]::GetFullPath($Core).TrimEnd('\') + '\'
    if (-not $commonPath.StartsWith($corePrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label CORE is not an independent clone; git common directory resolves outside ${Core}: $commonPath"
    }
    $dirty = @(Invoke-Git $Core @('status', '--porcelain', '--untracked-files=no'))
    if ($Core -eq $TemplateCoreRoot) {
        # Git may report the template's read-only source junctions as type changes; their content is covered by the blob samples.
        $junctioned = @($ReadOnlySharedCoreDirectories | Where-Object {
            $item = Get-Item -LiteralPath (Join-Path $Core $_) -Force -ErrorAction SilentlyContinue
            $null -ne $item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
        })
        $dirty = @($dirty | Where-Object {
            $entry = $_.Substring(3).Trim('"')
            $hidden = $false
            foreach ($name in $junctioned) { if ($entry -eq $name -or $entry.StartsWith("$name/")) { $hidden = $true } }
            -not $hidden
        })
    }
    if ($dirty.Count -ne 0) { throw "$Label CORE has tracked changes: $($dirty -join '; ')" }
}

# The template is the hardlink source; it must be an unmodified pinned checkout or every agent root inherits the damage.
function Assert-TemplatePristine {
    if (-not (Test-Path -LiteralPath $TemplateRoot -PathType Container)) { throw "Template root does not exist: $TemplateRoot" }
    Assert-RealDirectory $TemplateRoot
    $templateProcesses = @(Get-RootProcesses $TemplateName (Get-AllProcesses))
    if ($templateProcesses.Count -ne 0) { throw "Template $TemplateName is in use by $($templateProcesses.Count) process(es); stop them before cloning or verifying agent roots." }
    Assert-PinnedClone $TemplateCoreRoot "Template $TemplateName"
    foreach ($name in $LargeCoreDirectories) {
        $directory = Get-RealCoreDirectory $TemplateCoreRoot $name
        if ($null -eq $directory) { throw "Template CORE directory is missing: $name" }
        $scan = [R2Native]::Scan($directory, $ExcludedExtensions)
        if ($scan.ReparsePoints.Count -ne 0) { throw "Template directory $directory contains reparse points: $(@($scan.ReparsePoints) -join ', ')" }
    }
    [void] (Assert-TrackedSamples $TemplateCoreRoot "Template $TemplateName")
    $binDelta = Join-Path $TemplateCoreRoot 'bin_delta'
    Assert-RealDirectory $binDelta
    # Agent roots copy bin_delta from the template, so the template must already be staged.
    $stubRoot = Join-Path $binDelta 'r1o_stubs'
    $required = @('CoherentUIGT.dll', 'materialsystem_nodx.dll', 'tier0.dll', 'vaudio_speex.dll') | ForEach-Object { Join-Path $binDelta $_ }
    $required += @($StubNames | ForEach-Object { Join-Path $stubRoot $_ })
    foreach ($path in $required) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Template $TemplateName bin_delta payload is incomplete (missing $path). Run -Stage -Root $TemplateName first."
        }
    }
}

function Add-LocalExclude([string] $Core, [string[]] $Patterns) {
    $excludePath = Join-Path $Core '.git\info\exclude'
    $existing = @()
    if (Test-Path -LiteralPath $excludePath -PathType Leaf) {
        $existing = [IO.File]::ReadAllLines($excludePath)
    }
    $toAppend = @($Patterns | Where-Object { $existing -notcontains $_ })
    if ($toAppend.Count -eq 0) { return }
    $text = [Environment]::NewLine + ($toAppend -join [Environment]::NewLine) + [Environment]::NewLine
    [IO.File]::AppendAllText($excludePath, $text, [Text.UTF8Encoding]::new($false))
}

# ---------------------------------------------------------------------------
# Agent root construction (shared by -Create, -Reset, and -Repair; never overwrites an existing file)
# ---------------------------------------------------------------------------

# Background test mode marker (docs/r2port.md): an empty r1delta_background.flag next to Titanfall.exe makes every
# launch from the root a background launch (off-screen, silent, no focus, no message boxes), whatever script starts it.
# Agent roots only; never the template and never the user's own r2 root.
function Ensure-BackgroundFlag {
    if ($AgentRootNames -notcontains $RootName -or $RootName -eq $TemplateName) { return }
    $flag = Join-Path $Root 'r1delta_background.flag'
    if (-not (Test-Path -LiteralPath $flag -PathType Leaf)) { New-Item -ItemType File -Path $flag | Out-Null }
}

function Install-RootSkeleton {
    $stageNames = @(Get-RootStageNames)
    $linked = 0
    foreach ($file in Get-ChildItem -LiteralPath $SourceLaunchRoot -File -Force) {
        if ($stageNames -contains $file.Name) { continue }
        if ($ExcludedExtensions -contains $file.Extension.ToLowerInvariant()) { continue }
        if ([R2Native]::LinkFile($file.FullName, (Join-Path $Root $file.Name))) { $linked++ }
    }
    foreach ($name in $RootJunctions) {
        $path = Join-Path $Root $name
        if (-not (Test-EntryExists $path)) { New-DirectoryJunction $path (Join-Path $GameRoot $name) }
    }
    foreach ($name in @('crash', 'crashes')) {
        $path = Join-Path $Root $name
        if (-not (Test-EntryExists $path)) { New-Item -ItemType Directory -Path $path | Out-Null }
    }
    Ensure-BackgroundFlag
    return $linked
}

function Install-CoreClone {
    # Independent clone of the already-verified pinned template (not of the user's CORE).
    Invoke-Git $Root @('clone', '--no-checkout', '--shared', $TemplateCoreRoot, $CoreRoot) | Out-Null
    Invoke-Git $CoreRoot @('config', '--local', 'core.autocrlf', 'false') | Out-Null
    Invoke-Git $CoreRoot @('sparse-checkout', 'init', '--cone') | Out-Null
    Invoke-Git $CoreRoot (@('sparse-checkout', 'set', '--cone') + @($AlwaysCheckoutDirectories)) | Out-Null
    $available = $true
    try { Invoke-Git $CoreRoot @('rev-parse', '--verify', "${Pin}^{commit}") | Out-Null } catch { $available = $false }
    if (-not $available) { Invoke-Git $CoreRoot @('fetch', '--no-tags', $TemplateCoreRoot, $Pin) | Out-Null }
    Invoke-Git $CoreRoot @('checkout', '--detach', $Pin) | Out-Null
}

function Install-CoreContent {
    $linked = 0
    foreach ($name in $LargeCoreDirectories) {
        $source = Get-RealCoreDirectory $TemplateCoreRoot $name
        $destination = Join-Path $CoreRoot $name
        if (Test-EntryExists $destination) { Assert-RealDirectory $destination }
        $result = [R2Native]::LinkTree($source, $destination, $ExcludedExtensions)
        $linked += $result.Created
    }
    foreach ($name in $RequiredRealDirectories) {
        $destination = Join-Path $CoreRoot $name
        if (-not (Test-EntryExists $destination)) { New-Item -ItemType Directory -Path $destination | Out-Null }
        Assert-RealDirectory $destination
    }

    # bin_delta is staged into, so it is always a private copy and never a hardlink.
    $binDeltaSource = Join-Path $TemplateCoreRoot 'bin_delta'
    if (-not (Test-Path -LiteralPath $binDeltaSource -PathType Container)) { throw "Missing template bin_delta: $binDeltaSource" }
    $binDeltaDestination = Join-Path $CoreRoot 'bin_delta'
    if (Test-EntryExists $binDeltaDestination) { Assert-RealDirectory $binDeltaDestination }
    [void] [R2Native]::CopyTree($binDeltaSource, $binDeltaDestination, $ExcludedExtensions)

    # Test-only AI graphs stay private copies.
    $graphDirectory = Join-Path $CoreRoot 'maps\graphs'
    if (Test-EntryExists $graphDirectory) { Assert-RealDirectory $graphDirectory } else { New-Item -ItemType Directory -Path $graphDirectory | Out-Null }
    foreach ($name in $GraphNames) {
        $source = Join-Path $GraphSourceRoot $name
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing test graph: $source" }
        $destination = Join-Path $graphDirectory $name
        if (-not (Test-EntryExists $destination)) { Copy-Item -LiteralPath $source -Destination $destination }
    }
    Add-LocalExclude $CoreRoot $LocalExcludePatterns
    return $linked
}

function Get-RootPayloadReport {
    $total = [R2Native]::Scan($Root, $ExcludedExtensions)
    $git = [R2Native]::Scan((Join-Path $CoreRoot '.git'), $ExcludedExtensions)
    $binDelta = [R2Native]::Scan((Join-Path $CoreRoot 'bin_delta'), $ExcludedExtensions)
    return [pscustomobject]@{
        PrivateMiB = [math]::Round($total.PrivateBytes / 1MB, 1)
        GitMiB = [math]::Round($git.PrivateBytes / 1MB, 1)
        BinDeltaMiB = [math]::Round($binDelta.PrivateBytes / 1MB, 1)
        SharedMiB = [math]::Round($total.SharedBytes / 1MB, 1)
        SharedFiles = $total.SharedFiles.Count
    }
}

function New-AgentRoot {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    Assert-FreeSpace
    if (-not (Test-Path -LiteralPath $SourceLaunchRoot -PathType Container)) { throw "Missing source path: $SourceLaunchRoot" }
    Assert-TemplatePristine

    New-Item -ItemType Directory -Path $Root | Out-Null
    try {
        $launchLinks = Install-RootSkeleton
        Install-CoreClone
        $contentLinks = Install-CoreContent
        Invoke-RootVerification -AfterCreate | Out-Null
    } catch {
        if (Test-Path -LiteralPath $Root) { [R2Native]::RemoveTree($Root) }
        throw
    }
    $timer.Stop()
    $report = Get-RootPayloadReport
    Write-Host "Created $Root from $TemplateName at CORE pin $Pin in $([math]::Round($timer.Elapsed.TotalSeconds, 1)) s."
    Write-Host "Private payload: $($report.PrivateMiB) MiB (.git $($report.GitMiB) MiB, bin_delta $($report.BinDeltaMiB) MiB); hardlinked $($report.SharedFiles) files / $($report.SharedMiB) MiB shared with the template and launch root."
    Write-Host "Hardlinks created: $launchLinks launch files, $contentLinks CORE files. Run -Stage -Root $RootName to install the Release executables and bin_delta payload."
}

function Remove-AgentRoot {
    $fullRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\')
    if ((Split-Path -Parent $fullRoot) -ne [IO.Path]::GetFullPath($GameRoot).TrimEnd('\') -or $AgentRootNames -notcontains (Split-Path -Leaf $fullRoot)) {
        throw "Refusing to remove '$fullRoot': not a managed agent root."
    }
    if (-not (Test-EntryExists $Root)) {
        Write-Host "Agent root is not present: $Root"
        return
    }
    $rootItem = Get-Item -LiteralPath $Root -Force
    if (-not $rootItem.PSIsContainer -or ($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Refusing to remove a non-directory or reparse-point root: $Root"
    }
    Assert-R2Idle
    $guarded = @(
        (Join-Path $GameRoot 'bin'), (Join-Path $GameRoot 'r1'), (Join-Path $GameRoot 'tools'),
        $SourceCoreRoot, $SourceLaunchRoot, $TemplateCoreRoot
    ) | Where-Object { Test-Path -LiteralPath $_ }
    $timer = [Diagnostics.Stopwatch]::StartNew()
    [R2Native]::RemoveTree($Root)
    $timer.Stop()
    foreach ($path in $guarded) {
        if (-not (Test-Path -LiteralPath $path)) { throw "CRITICAL: $path disappeared while removing $Root." }
    }
    if (Test-EntryExists $Root) { throw "Removal of $Root did not complete." }
    Write-Host "Removed $Root (junctions unlinked without following them; hardlinked files only unlinked) in $([math]::Round($timer.Elapsed.TotalSeconds, 1)) s."
}

function Repair-AgentRoot {
    Assert-R2Idle
    Assert-RealDirectory $Root
    Assert-PinnedClone $CoreRoot $RootName
    Assert-TemplatePristine
    $launchLinks = Install-RootSkeleton
    $contentLinks = Install-CoreContent
    Invoke-RootVerification -AfterCreate | Out-Null
    Write-Host "Repaired ${Root}: added $launchLinks missing launch hardlinks and $contentLinks missing CORE hardlinks. Existing files were not overwritten; run -Verify to confirm staging."
}

# ---------------------------------------------------------------------------
# Verification
# ---------------------------------------------------------------------------

function Test-RootWritablePaths {
    $rootPrefix = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    $otherCoreTrees = @($SourceCoreRoot, $UserPlayCoreRoot)
    foreach ($name in (@($TemplateName) + @($AgentRootNames))) {
        if ($name -ne $RootName) { $otherCoreTrees += (Get-CoreRootFor $name) }
    }
    foreach ($relativePath in $WriteProbeDirectories) {
        $createdDirectories = [System.Collections.Generic.List[string]]::new()
        $directory = $CoreRoot
        foreach ($part in $relativePath.Split('\')) {
            $directory = Join-Path $directory $part
            if (-not (Test-Path -LiteralPath $directory -PathType Container)) {
                New-Item -ItemType Directory -Path $directory | Out-Null
                $createdDirectories.Add($directory)
            }
            Assert-RealDirectory $directory
        }

        $markerName = ".r2root-writecheck-$([Guid]::NewGuid().ToString('N')).tmp"
        $marker = Join-Path $directory $markerName
        try {
            foreach ($otherCore in $otherCoreTrees) {
                if (Test-Path -LiteralPath (Join-Path (Join-Path $otherCore $relativePath) $markerName)) {
                    throw "Write-test marker unexpectedly exists outside ${RootName}: $otherCore"
                }
            }
            [IO.File]::WriteAllText($marker, [Guid]::NewGuid().ToString('N'))
            if (-not (Test-Path -LiteralPath $marker -PathType Leaf)) { throw "Write test did not create $marker." }
            $writtenPath = [IO.Path]::GetFullPath($marker)
            if (-not $writtenPath.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
                throw "Write test escaped ${RootName}: $writtenPath"
            }
            foreach ($otherCore in $otherCoreTrees) {
                if (Test-Path -LiteralPath (Join-Path (Join-Path $otherCore $relativePath) $markerName)) {
                    throw "Write test reached another CORE tree: $otherCore"
                }
            }
        } finally {
            if (Test-Path -LiteralPath $marker) { Remove-Item -LiteralPath $marker -Force }
            for ($index = $createdDirectories.Count - 1; $index -ge 0; $index--) {
                $createdDirectory = $createdDirectories[$index]
                if ((Get-ChildItem -LiteralPath $createdDirectory -Force | Measure-Object).Count -eq 0) {
                    [IO.Directory]::Delete($createdDirectory, $false)
                }
            }
        }
    }
    Write-Host "Verified isolated temporary writes in $($WriteProbeDirectories.Count) $RootName paths."
}

function Format-PathList($Paths) {
    $items = @($Paths)
    $shown = @($items | Select-Object -First 5)
    $suffix = if ($items.Count -gt $shown.Count) { " ... (+$($items.Count - $shown.Count) more)" } else { '' }
    return ($shown -join ', ') + $suffix
}

function Invoke-RootVerification([switch] $AfterCreate) {
    if (-not (Test-Path -LiteralPath $Root -PathType Container)) { throw "Launch root does not exist: $Root" }
    Assert-RealDirectory $Root
    foreach ($name in $RootJunctions) { Assert-Junction (Join-Path $Root $name) (Join-Path $GameRoot $name) }
    if ($IsTemplate) {
        $templateProcesses = @(Get-RootProcesses $TemplateName (Get-AllProcesses))
        if ($templateProcesses.Count -ne 0) { throw "Template $TemplateName is in use by $($templateProcesses.Count) process(es); stop them before verifying it." }
    }
    foreach ($name in @('crash', 'crashes')) { Assert-RealDirectory (Join-Path $Root $name) }
    Assert-PinnedClone $CoreRoot $RootName
    foreach ($name in $AlwaysCheckoutDirectories) { Assert-RealDirectory (Join-Path $CoreRoot $name) }
    foreach ($name in $RequiredRealDirectories) { Assert-RealDirectory (Join-Path $CoreRoot $name) }
    foreach ($name in $LargeCoreDirectories) { [void] (Get-RealCoreDirectory $CoreRoot $name) }

# Whole-root scan: only expected root junctions and template-only shared-content junctions may be reparse points.
    $scan = [R2Native]::Scan($Root, $ExcludedExtensions)
    $expectedJunctions = @($RootJunctions | ForEach-Object { Join-Path $Root $_ })
    if ($IsTemplate) { $expectedJunctions += @($ReadOnlySharedCoreDirectories | ForEach-Object { Join-Path $CoreRoot $_ }) }
    $unexpected = @($scan.ReparsePoints | Where-Object { $expectedJunctions -notcontains $_ })
    if ($unexpected.Count -ne 0) { throw "Unexpected reparse points inside ${Root}: $(Format-PathList $unexpected)" }
    if ($scan.ExcludedFiles.Count -ne 0) { throw "IDA database files must not be copied into ${Root}: $(Format-PathList $scan.ExcludedFiles)" }

    $hardlinkedCoreFiles = 0
    if (-not $IsTemplate) {
        Assert-TemplatePristine
        foreach ($name in $LargeCoreDirectories) {
            $compare = [R2Native]::CompareTree((Get-RealCoreDirectory $TemplateCoreRoot $name), (Join-Path $CoreRoot $name), $ExcludedExtensions)
            if ($compare.Different.Count -ne 0) {
                throw "CORE $name has files that are not hardlinks of the $TemplateName template: $(Format-PathList $compare.Different)"
            }
            if ($compare.Missing.Count -ne 0) {
                throw "CORE $name is missing template entries: $(Format-PathList $compare.Missing). Run -Repair -Root $RootName."
            }
            $hardlinkedCoreFiles += $compare.Matched
        }
        $skipNames = @($RootExecutables) + @('dedicated.dll')
        foreach ($file in Get-ChildItem -LiteralPath $SourceLaunchRoot -File -Force) {
            if ($skipNames -contains $file.Name) { continue }
            if ($ExcludedExtensions -contains $file.Extension.ToLowerInvariant()) { continue }
            $state = [R2Native]::CompareFile($file.FullName, (Join-Path $Root $file.Name))
            if ($state -eq 1) { throw "Launch root is missing hardlink $($file.Name). Run -Repair -Root $RootName." }
            if ($state -eq 2) { throw "Launch file $($file.Name) is not a hardlink of $SourceLaunchRoot." }
        }
    }

    foreach ($name in $GraphNames) {
        $source = Join-Path $GraphSourceRoot $name
        $target = Join-Path $CoreRoot "maps\graphs\$name"
        if (-not (Test-Path -LiteralPath $target -PathType Leaf)) { throw "Missing loose test graph: $target" }
        if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash) {
            throw "Loose graph differs from its generated source: $name"
        }
    }
    if (Test-Path -LiteralPath (Join-Path $CoreRoot 'scripts\vscripts\movement')) {
        throw 'Pinned CORE unexpectedly contains scripts/vscripts/movement.'
    }
    $sampleCount = Assert-TrackedSamples $CoreRoot $RootName

    $binDelta = Join-Path $CoreRoot 'bin_delta'
    Assert-RealDirectory $binDelta
    foreach ($name in @('CoherentUIGT.dll', 'materialsystem_nodx.dll', 'tier0.dll', 'vaudio_speex.dll')) {
        if (-not (Test-Path -LiteralPath (Join-Path $binDelta $name) -PathType Leaf)) {
            throw "CORE bin_delta is missing $name. Run -Stage after creating the root."
        }
    }
    $stubRoot = Join-Path $binDelta 'r1o_stubs'
    Assert-RealDirectory $stubRoot
    foreach ($name in $StubNames) {
        if (-not (Test-Path -LiteralPath (Join-Path $stubRoot $name) -PathType Leaf)) { throw "CORE r1o_stubs is missing $name." }
    }
    $binDeltaScan = [R2Native]::Scan($binDelta, $ExcludedExtensions)
    if ($binDeltaScan.SharedFiles.Count -ne 0) {
        throw "bin_delta must be a private copy but contains shared hardlinked files: $(Format-PathList $binDeltaScan.SharedFiles)"
    }

    if (-not $AfterCreate) {
        foreach ($name in @('Titanfall.exe', 'R1Delta_DS.exe', 'r1delta.exe', 'r1delta.exe.config')) {
            if (-not (Test-Path -LiteralPath (Join-Path $Root $name) -PathType Leaf)) { throw "Launch root is missing $name. Run -Stage after creating the root." }
        }
        foreach ($name in $RootExecutables) {
            if ([R2Native]::GetIdentity((Join-Path $Root $name)).Links -ne 1) {
                throw "Staged executable $name must be a private copy but is hardlinked."
            }
        }
        $running = @(Get-RootProcesses $RootName (Get-AllProcesses))
        if ($running.Count -eq 0) {
            Test-RootWritablePaths
        } else {
            Write-Host "Skipped write probes: $($running.Count) process(es) are running from $RootName."
        }
    }

    $untracked = @(Invoke-Git $CoreRoot @('ls-files', '--others', '--exclude-standard'))
    if (-not $AfterCreate) {
        Write-Host "Verified ${Root}: independent CORE clone at $Pin (tracked tree clean, core.autocrlf=false); $sampleCount sampled blobs match the pin; no IDA databases; only the root junctions are reparse points."
        if (-not $IsTemplate) {
            Write-Host "Hardlink identities match $TemplateName for $hardlinkedCoreFiles CORE files; launch files match $SourceLaunchRoot; bin_delta and staged executables are private."
        }
        Write-Host "Root-local untracked files (allowed loose additions): $($untracked.Count)."
        $report = Get-RootPayloadReport
        Write-Host "Private payload: $($report.PrivateMiB) MiB (.git $($report.GitMiB) MiB, bin_delta $($report.BinDeltaMiB) MiB)."
    }
    return $true
}

# ---------------------------------------------------------------------------
# Staging (root-scoped; release-payload verification is unchanged)
# ---------------------------------------------------------------------------

function Copy-FileAtomically([string] $Source, [string] $DestinationDirectory) {
    $destination = Join-Path $DestinationDirectory (Split-Path -Leaf $Source)
    $backup = "$destination.pre-r2port-backup"
    if (Test-Path -LiteralPath $destination) {
        if (-not (Test-Path -LiteralPath $destination -PathType Leaf)) { throw "Cannot stage over a non-file target: $destination" }
        if (Test-Path -LiteralPath $backup) {
            if (-not (Test-Path -LiteralPath $backup -PathType Leaf)) { throw "Existing pre-stage backup is not a file: $backup" }
        } else {
            $backupTemporary = "$backup.$([Guid]::NewGuid().ToString('N')).r1dstaging"
            try {
                Copy-Item -LiteralPath $destination -Destination $backupTemporary
                Move-Item -LiteralPath $backupTemporary -Destination $backup
            } finally {
                if (Test-Path -LiteralPath $backupTemporary -PathType Leaf) { Remove-Item -LiteralPath $backupTemporary -Force }
            }
        }
    }
    # Copy to a private temporary name, then move over the directory entry: never writes through a hardlink.
    $temporary = "$destination.$([Guid]::NewGuid().ToString('N')).r1dstaging"
    try {
        Copy-Item -LiteralPath $Source -Destination $temporary -Force
        if (Test-Path -LiteralPath $destination -PathType Leaf) {
            $existingItem = Get-Item -LiteralPath $destination -Force
            if ($existingItem.IsReadOnly -or [R2Native]::GetIdentity($destination).Links -gt 1) {
                # Unlink only this directory entry (never the shared inode, its content, or its attributes), then move the private copy in.
                [R2Native]::RemoveTree($destination)
            }
        }
        Move-Item -LiteralPath $temporary -Destination $destination -Force
    } finally {
        if (Test-Path -LiteralPath $temporary -PathType Leaf) {
            Remove-Item -LiteralPath $temporary -Force
        }
    }
}

function New-ReleaseVerificationMirror([string] $Destination) {
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    $sourcePrefix = [IO.Path]::GetFullPath($BuildRoot).TrimEnd('\') + '\'
    foreach ($item in Get-ChildItem -LiteralPath $BuildRoot -Recurse -Force) {
        $relativePath = $item.FullName.Substring($sourcePrefix.Length)
        if ($item.PSIsContainer) {
            New-Item -ItemType Directory -Path (Join-Path $Destination $relativePath) -Force | Out-Null
            continue
        }
        if (-not $relativePath.Contains('\') -and $StubNames -contains $item.Name) { continue }
        $target = Join-Path $Destination $relativePath
        $targetParent = Split-Path -Parent $target
        if (-not (Test-Path -LiteralPath $targetParent -PathType Container)) {
            New-Item -ItemType Directory -Path $targetParent -Force | Out-Null
        }
        New-Item -ItemType HardLink -Path $target -Target $item.FullName | Out-Null
    }
    $verificationStubRoot = Join-Path $Destination 'r1o_stubs'
    New-Item -ItemType Directory -Path $verificationStubRoot -Force | Out-Null
    foreach ($name in $StubNames) {
        $rootStub = Join-Path $BuildRoot $name
        if (Test-Path -LiteralPath $rootStub -PathType Leaf) {
            Copy-Item -LiteralPath $rootStub -Destination (Join-Path $verificationStubRoot $name) -Force
        }
    }
}

function Stage-R2Root {
    Assert-R2Idle
    Ensure-BackgroundFlag
    Assert-RealDirectory $Root
    Assert-RealDirectory $CoreRoot
    foreach ($name in @('bin', 'bin_nexon')) { [void] (Get-RealCoreDirectory $CoreRoot $name) }
    if (Test-EntryExists (Join-Path $CoreRoot 'bin_delta')) { Assert-RealDirectory (Join-Path $CoreRoot 'bin_delta') }
    foreach ($path in @($VerifyScript, (Join-Path $BuildRoot 'Titanfall.exe'), (Join-Path $BuildRoot 'R1Delta_DS.exe'), (Join-Path $BuildRoot 'r1delta.exe'))) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required staging input is missing: $path" }
    }
    $verifyCore = Join-Path $env:TEMP ("r2port-core-verify-" + [Guid]::NewGuid().ToString('N'))
    $verifyBuild = Join-Path $env:TEMP ("r2port-build-verify-" + [Guid]::NewGuid().ToString('N'))
    try {
        foreach ($directory in @('bin', 'bin_nexon')) {
            New-Item -ItemType Directory -Path (Join-Path $verifyCore $directory) -Force | Out-Null
        }
        foreach ($directory in @('bin', 'bin_nexon')) {
            $sourceDirectory = Join-Path $CoreRoot $directory
            $targetDirectory = Join-Path $verifyCore $directory
            Get-ChildItem -LiteralPath $sourceDirectory -File |
                Where-Object { $_.Extension -in @('.dll', '.exe') } |
                ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $targetDirectory $_.Name) }
        }
        Copy-Item -LiteralPath (Join-Path (Join-Path $CoreRoot 'bin_nexon') 'SHA256SUMS') -Destination (Join-Path (Join-Path $verifyCore 'bin_nexon') 'SHA256SUMS')
        New-ReleaseVerificationMirror $verifyBuild
        & $VerifyScript -CorePath $verifyCore -BuildPath $verifyBuild
    } finally {
        if (Test-Path -LiteralPath $verifyCore) { Remove-Item -LiteralPath $verifyCore -Recurse -Force }
        if (Test-Path -LiteralPath $verifyBuild) { Remove-Item -LiteralPath $verifyBuild -Recurse -Force }
    }

    $binDelta = Join-Path $CoreRoot 'bin_delta'
    New-Item -ItemType Directory -Path $binDelta -Force | Out-Null
    foreach ($file in Get-ChildItem -LiteralPath $BuildRoot -File | Where-Object { $_.Extension -in @('.dll', '.png') }) {
        if ($StubNames -contains $file.Name) { continue }
        Copy-FileAtomically $file.FullName $binDelta
    }
    $stubSource = Join-Path $BuildRoot 'r1o_stubs'
    $stubDestination = Join-Path $binDelta 'r1o_stubs'
    New-Item -ItemType Directory -Path $stubDestination -Force | Out-Null
    foreach ($name in $StubNames) {
        $source = Join-Path $stubSource $name
        $rootStub = Join-Path $BuildRoot $name
        if (Test-Path -LiteralPath $rootStub -PathType Leaf) { $source = $rootStub }
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing R1O graphics stub: $source" }
        Copy-FileAtomically $source $stubDestination
    }
    foreach ($name in (Get-RootStageNames)) {
        $source = Join-Path $BuildRoot $name
        if (Test-Path -LiteralPath $source -PathType Leaf) { Copy-FileAtomically $source $Root }
    }
    Write-Host "Staged Release DLL/PNG payload into $binDelta and Release executables into $Root by copy-then-move; original targets are preserved once as .pre-r2port-backup."
}

# ---------------------------------------------------------------------------
# Reporting
# ---------------------------------------------------------------------------

function Get-RootSummary([string] $Name, $Processes) {
    $path = Join-Path $GameRoot $Name
    $core = Get-CoreRootFor $Name
    $summary = [ordered]@{
        Root = $Name
        Role = if ($Name -eq $TemplateName) { 'template' } else { 'agent' }
        Present = $false
        Running = @(Get-RootProcesses $Name $Processes).Count
        CreatedUtc = ''
        PrivateMiB = ''
        SharedMiB = ''
        Head = ''
        Tracked = ''
    }
    if (-not (Test-EntryExists $path)) { return [pscustomobject]$summary }
    $item = Get-Item -LiteralPath $path -Force
    $summary.CreatedUtc = $item.CreationTimeUtc.ToString('o')
    if (-not $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        $summary.Present = $true
        $summary.Tracked = 'INVALID (not a real directory)'
        return [pscustomobject]$summary
    }
    $summary.Present = $true
    $scan = [R2Native]::Scan($path, $ExcludedExtensions)
    $summary.PrivateMiB = [math]::Round($scan.PrivateBytes / 1MB, 1)
    $summary.SharedMiB = [math]::Round($scan.SharedBytes / 1MB, 1)
    if (Test-Path -LiteralPath (Join-Path $core '.git')) {
        try {
            $head = @(Invoke-Git $core @('rev-parse', 'HEAD'))
            $summary.Head = if ($head[0] -eq $Pin) { 'pinned' } else { $head[0].Substring(0, 9) }
            $dirty = @(Invoke-Git $core @('status', '--porcelain', '--untracked-files=no'))
            $summary.Tracked = if ($dirty.Count -eq 0) { 'clean' } else { "$($dirty.Count) changed" }
        } catch {
            $summary.Tracked = 'git error'
        }
    }
    return [pscustomobject]$summary
}

function Show-RootList {
    $processes = Get-AllProcesses
    $rows = @(@($TemplateName) + @($AgentRootNames) | ForEach-Object { Get-RootSummary $_ $processes })
    ($rows | Format-Table -AutoSize | Out-String -Width 200).TrimEnd() | Write-Host
    Write-Host "Roots live under $GameRoot; '$TemplateName' is the golden template (stage/verify/status only). The user's r2 root is never touched."
}

function Show-R2Status {
    if (-not (Test-EntryExists $Root)) {
        Write-Host "Root is not present: $Root"
        return
    }
    $summary = Get-RootSummary $RootName (Get-AllProcesses)
    Write-Host "Root: $Root ($($summary.Role)); created UTC: $($summary.CreatedUtc); running processes: $($summary.Running)"
    Write-Host "Private non-hardlinked payload (junction contents excluded): $($summary.PrivateMiB) MiB; hardlink-shared: $($summary.SharedMiB) MiB"
    foreach ($name in @('bin', 'r1', 'tools', 'r1delta')) {
        $path = Join-Path $Root $name
        if (Test-EntryExists $path) {
            $item = Get-Item -LiteralPath $path -Force
            $type = Get-LinkType $item
            if (-not $type) { $type = 'real directory' }
            Write-Host "  $name : $type -> $($item.Target -join ', ')"
        }
    }
    if (Test-Path -LiteralPath (Join-Path $CoreRoot '.git')) {
        Write-Host "CORE HEAD: $($summary.Head); tracked files: $($summary.Tracked)"
        Write-Host "CORE directories hardlinked from ${TemplateName}: $(if ($RootName -eq $TemplateName) { 'n/a (template)' } else { $LargeCoreDirectories -join ', ' })."
    }
}

# ---------------------------------------------------------------------------
# Dispatch
# ---------------------------------------------------------------------------

try {
    if ($List) {
        Show-RootList
    } else {
        if ($Create -or $Stage -or $Remove -or $Reset -or $Repair) { Enter-RootLock $RootName }
        if ($Create) {
            if (Test-EntryExists $Root) { throw "Agent root already exists: $Root. Use -Reset to recreate only that root." }
            Assert-R2Idle
            New-AgentRoot
        } elseif ($Reset) {
            Assert-FreeSpace
            Remove-AgentRoot
            New-AgentRoot
        } elseif ($Remove) {
            Remove-AgentRoot
        } elseif ($Repair) {
            Repair-AgentRoot
        } elseif ($Stage) {
            Stage-R2Root
        } elseif ($Verify) {
            [void] (Invoke-RootVerification)
        } else {
            Show-R2Status
        }
    }
} finally {
    Exit-RootLock
}
