param(
    [Parameter(Mandatory = $true)]
    [string]$LauncherPath
)

# Redirect the launcher log before any launcher code can write it (the log path is resolved once per process).
$logDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ('r1delta-launcher-log-' + [Guid]::NewGuid().ToString('N'))
$env:R1DELTA_LAUNCHER_LOG_DIR = $logDirectory
$ErrorActionPreference = 'Stop'
$binding = [System.Reflection.BindingFlags]'Public,NonPublic,Static,Instance'
$assembly = [System.Reflection.Assembly]::LoadFrom((Resolve-Path -LiteralPath $LauncherPath))
$manager = $assembly.GetType('R1Delta.TitanfallManager', $true)
$setupWindow = $assembly.GetType('launcher_ex.SetupWindow', $true)
$statusInterface = $assembly.GetType('R1Delta.IInstallProgressStatus', $true)
$registryHelper = $assembly.GetType('R1Delta.RegistryHelper', $true)
$resolve = $manager.GetMethod('ResolveGameRoot', $binding)
$hasSidecars = $manager.GetMethod('HasDownloadSidecars', $binding)
$deleteArtifacts = $manager.GetMethod('DeleteDownloadArtifacts', $binding)
$hasOwnership = $manager.GetMethod('HasValidManagedInstallOwnership', $binding)
$ensureOwnership = $manager.GetMethod('TryEnsureManagedInstallOwnership', $binding)
$removeOwnership = $manager.GetMethod('TryRemoveManagedInstallOwnership', $binding)
$hasLegacyProof = $manager.GetMethod('HasLegacyCompletedInstallProof', $binding)
$cleanupAuthorized = $manager.GetMethod('IsManagedCleanupAuthorized', $binding)
$acquireLease = $manager.GetMethod('TryAcquireInstallOperationLease', $binding)
$claimOneTimeWarning = $registryHelper.GetMethod('TryClaimOneTimeWarning', $binding)
$isRootOfVolume = $manager.GetMethod('IsRootOfVolume', $binding)
$driveRootRefusal = $manager.GetMethod('TryGetDriveRootRefusal', $binding)
$describeMissingLaunchFiles = $manager.GetMethod('TryDescribeMissingLaunchFiles', $binding)
$fileList = $assembly.GetType('launcher_ex.TitanfallFileList', $true).GetField('s_fileList', $binding).GetValue($null)
$launchFailure = $assembly.GetType('launcher_ex.LaunchFailureMessage', $true)
$launcherLog = $assembly.GetType('launcher_ex.LauncherLog', $true)
$startupDiagnostics = $assembly.GetType('launcher_ex.StartupDiagnostics', $true)
$progressText = $assembly.GetType('launcher_ex.InstallProgressText', $true)
$statusType = $assembly.GetType('R1Delta.InstallProgressStatus', $true)
$phaseType = $assembly.GetType('R1Delta.InstallProgressPhase', $true)
$transferPhaseType = $assembly.GetType('DownloadTransferPhase', $true)
$backendType = $assembly.GetType('DownloadBackend', $true)
$buildWarningMutexName = $registryHelper.GetMethod('BuildPrerequisiteWarningClaimMutexName', $binding)
$warningClaimVersion = [int]$registryHelper.GetField(
    'PrerequisiteWarningClaimVersion',
    $binding).GetRawConstantValue()

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "FAILED: $Message"
    }
    Write-Host "PASS: $Message"
}

function Get-Resolution([string]$Path) {
    return $resolve.Invoke($null, @($Path))
}

function Get-Property($Object, [string]$Name) {
    return $Object.GetType().GetProperty($Name, $binding).GetValue($Object)
}

$root = Join-Path ([System.IO.Path]::GetTempPath()) ('r1delta-installer-smoke-' + [Guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Path $root | Out-Null
    $testSid = 'S-1-5-21-123-456-789-1001'
    $warningMutexName = [string]$buildWarningMutexName.Invoke(
        $null,
        [object[]]@($testSid))
    Assert-True (
        $warningMutexName -eq ('Global\R1Delta.PrerequisiteWarningClaim.' + $testSid)
    ) 'prerequisite-warning mutex is global and scoped to the current user SID'

    $claimState = [System.Collections.Generic.List[int]]::new()
    $claimState.Add(0)
    $readClaim = [Func[int]] { return $claimState[0] }
    $writeClaim = [Action[int]] { param([int]$version) $claimState[0] = $version }
    $claimArgs = [object[]]@($warningClaimVersion, $readClaim, $writeClaim)
    Assert-True ([bool]$claimOneTimeWarning.Invoke($null, $claimArgs)) 'first prerequisite-warning claim succeeds'
    Assert-True ($claimState[0] -eq $warningClaimVersion) 'prerequisite-warning claim writes the current version marker'
    Assert-True (-not [bool]$claimOneTimeWarning.Invoke($null, $claimArgs)) 'subsequent prerequisite-warning claim is suppressed'

    $unpersistedClaimState = [System.Collections.Generic.List[int]]::new()
    $unpersistedClaimState.Add(0)
    $readUnpersistedClaim = [Func[int]] { return $unpersistedClaimState[0] }
    $ignoreClaimWrite = [Action[int]] { param([int]$version) }
    $unpersistedClaimArgs = [object[]]@(
        $warningClaimVersion,
        $readUnpersistedClaim,
        $ignoreClaimWrite)
    Assert-True (-not [bool]$claimOneTimeWarning.Invoke($null, $unpersistedClaimArgs)) 'unpersisted prerequisite-warning claim is not displayed'

    $empty = Get-Resolution ''
    Assert-True (-not (Get-Property $empty 'IsUsableDestination')) 'empty selection is rejected'

    $freshPath = Join-Path $root 'fresh'
    $fresh = Get-Resolution $freshPath
    Assert-True (Get-Property $fresh 'IsUsableDestination') 'markerless fresh destination is usable'
    Assert-True (-not (Get-Property $fresh 'Succeeded')) 'markerless fresh destination is not mistaken for an existing install'
    Assert-True ((Get-Property $fresh 'ResolvedRoot') -eq [System.IO.Path]::GetFullPath($freshPath)) 'fresh destination remains the exact selected root'

    New-Item -ItemType Directory -Path $freshPath | Out-Null
    Set-Content -LiteralPath (Join-Path $freshPath 'partial.bin.curl.partial') -Value 'partial'
    $partial = Get-Resolution $freshPath
    Assert-True ((Get-Property $partial 'IsUsableDestination') -and -not (Get-Property $partial 'Succeeded')) 'markerless partial destination remains recoverable'

    $exactPath = Join-Path $root 'exact'
    New-Item -ItemType Directory -Path (Join-Path $exactPath 'vpk') -Force | Out-Null
    New-Item -ItemType File -Path (Join-Path $exactPath 'vpk/client_mp_common.bsp.pak000_000.vpk') | Out-Null
    $exact = Get-Resolution $exactPath
    Assert-True ((Get-Property $exact 'Succeeded') -and (Get-Property $exact 'ResolvedRoot') -eq [System.IO.Path]::GetFullPath($exactPath)) 'exact existing root is selected'

    $parentPath = Join-Path $root 'parent'
    $childPath = Join-Path $parentPath 'r1delta'
    New-Item -ItemType Directory -Path (Join-Path $childPath 'vpk') -Force | Out-Null
    New-Item -ItemType File -Path (Join-Path $childPath 'vpk/client_mp_common.bsp.pak000_000.vpk') | Out-Null
    $child = Get-Resolution $parentPath
    Assert-True ((Get-Property $child 'Succeeded') -and (Get-Property $child 'ResolvedRoot') -eq [System.IO.Path]::GetFullPath($childPath)) 'direct r1delta child is normalized and selected'

    New-Item -ItemType Directory -Path (Join-Path $parentPath 'vpk') -Force | Out-Null
    New-Item -ItemType File -Path (Join-Path $parentPath 'vpk/client_mp_common.bsp.pak000_000.vpk') | Out-Null
    $ambiguous = Get-Resolution $parentPath
    Assert-True (-not (Get-Property $ambiguous 'IsUsableDestination')) 'ambiguous parent and child roots are rejected'

    # --- Drive roots: content installed to D:\ later lost bin\x64_retail and launcher.dll failed with 126 ---
    $rootArgs = [object[]]@('C:\', $null)
    Assert-True ([bool]$isRootOfVolume.Invoke($null, $rootArgs)) 'a drive root is recognised as a volume root'
    Assert-True ($rootArgs[1] -eq 'C:\') 'volume root reports the drive root itself'
    Assert-True (-not [bool]$isRootOfVolume.Invoke($null, [object[]]@('C:\Games\Titanfall\', $null))) 'a trailing separator does not make a normal folder look like a volume root'
    Assert-True (-not [bool]$isRootOfVolume.Invoke($null, [object[]]@('C:\Games\Titanfall', $null))) 'a folder below a drive root is a valid destination'
    Assert-True (-not [bool]$isRootOfVolume.Invoke($null, [object[]]@('', $null))) 'empty path is not a volume root'
    Assert-True ([bool]$isRootOfVolume.Invoke($null, [object[]]@('\\server\share\', $null))) 'a share root is recognised as a volume root'

    $freshRootArgs = [object[]]@('D:\', $false, $null)
    Assert-True ([bool]$driveRootRefusal.Invoke($null, $freshRootArgs)) 'a fresh install into a drive root is refused'
    Assert-True ($freshRootArgs[2] -like '*root of a drive ("D:\")*' -and $freshRootArgs[2] -like '*D:\Titanfall*') 'the refusal names the drive and suggests a folder on it'
    Assert-True (-not [bool]$driveRootRefusal.Invoke($null, [object[]]@('D:\', $true, $null))) 'a drive root that already holds an install is not refused'
    Assert-True (-not [bool]$driveRootRefusal.Invoke($null, [object[]]@('D:\Titanfall', $false, $null))) 'a fresh install into a folder is not refused'

    # A real drive root without touching the user's drives: map a scratch folder to an unused letter.
    $letter = 'Z', 'Y', 'X', 'W', 'V', 'U', 'T' | Where-Object { -not (Test-Path -LiteralPath ($_ + ':\')) } | Select-Object -First 1
    Assert-True ($null -ne $letter) 'an unused drive letter is available for the drive-root checks'
    $driveBacking = Join-Path $root 'drive-backing'
    New-Item -ItemType Directory -Path $driveBacking | Out-Null
    & subst.exe "${letter}:" $driveBacking
    if ($LASTEXITCODE -ne 0) { throw "subst ${letter}: failed" }
    try {
        $driveRoot = "${letter}:\"
        $emptyRoot = Get-Resolution $driveRoot
        Assert-True ((Get-Property $emptyRoot 'IsUsableDestination') -and -not (Get-Property $emptyRoot 'Succeeded')) 'an empty drive root resolves as a usable destination that is not an install'
        Assert-True ([bool]$driveRootRefusal.Invoke($null, [object[]]@($driveRoot, [bool](Get-Property $emptyRoot 'Succeeded'), $null))) 'setup refuses the empty drive root as a new install'

        # An install that already lives at the root keeps launching: it resolves as an existing install.
        New-Item -ItemType Directory -Path (Join-Path $driveRoot 'vpk') | Out-Null
        New-Item -ItemType File -Path (Join-Path $driveRoot 'vpk/client_mp_common.bsp.pak000_000.vpk') | Out-Null
        foreach ($entry in $fileList) {
            if ($entry.Item2 -like 'bin/x64_retail/*') {
                $libraryPath = Join-Path $driveRoot $entry.Item2
                New-Item -ItemType Directory -Path (Split-Path -Parent $libraryPath) -Force | Out-Null
                New-Item -ItemType File -Path $libraryPath -Force | Out-Null
            }
        }
        $rootInstall = Get-Resolution $driveRoot
        Assert-True (Get-Property $rootInstall 'Succeeded') 'a valid install at a drive root resolves as an existing install'
        Assert-True ((Get-Property $rootInstall 'ResolvedRoot') -eq $driveRoot) 'the existing root install keeps the drive root as its game root'
        Assert-True (-not [bool]$driveRootRefusal.Invoke($null, [object[]]@($driveRoot, [bool](Get-Property $rootInstall 'Succeeded'), $null))) 'the existing root install is not refused'
        $launchArgs = [object[]]@($driveRoot, $null)
        Assert-True (-not [bool]$describeMissingLaunchFiles.Invoke($null, $launchArgs)) 'a complete root install has every file launcher.dll needs'

        # The field failure: bin\x64_retail\vstdlib.dll is gone. The install is detected as damaged, still resolves
        # as an install (so repair can run in place), and is not refused.
        Remove-Item -LiteralPath (Join-Path $driveRoot 'bin/x64_retail/vstdlib.dll')
        $damagedArgs = [object[]]@($driveRoot, $null)
        Assert-True ([bool]$describeMissingLaunchFiles.Invoke($null, $damagedArgs)) 'a root install without vstdlib.dll is detected as damaged'
        Assert-True ($damagedArgs[1] -eq 'bin\x64_retail\vstdlib.dll') 'the damage report names the missing library'
        $damagedRoot = Get-Resolution $driveRoot
        Assert-True (Get-Property $damagedRoot 'Succeeded') 'a damaged root install still resolves as an install'
        Assert-True (-not [bool]$driveRootRefusal.Invoke($null, [object[]]@($driveRoot, [bool](Get-Property $damagedRoot 'Succeeded'), $null))) 'a damaged root install can be repaired in place'

        Remove-Item -LiteralPath (Join-Path $driveRoot 'vpk/client_mp_common.bsp.pak000_000.vpk')
        $noMarkerArgs = [object[]]@($driveRoot, $null)
        Assert-True ([bool]$describeMissingLaunchFiles.Invoke($null, $noMarkerArgs) -and $noMarkerArgs[1] -like '*client_mp_common.bsp.pak000_000.vpk*') 'a missing core VPK is reported as damage'
    }
    finally {
        & subst.exe "${letter}:" /d | Out-Null
    }

    # --- A launch that Windows application control blocks must say what was blocked ---
    $blockedPath = 'C:\Users\pilot\AppData\Local\R1Delta\app-3.0.5\Titanfall.exe'
    Assert-True ([bool]$launchFailure.GetMethod('IsApplicationControlBlock', $binding).Invoke($null, [object[]]@(4551))) 'error 4551 is an application control block'
    Assert-True (-not [bool]$launchFailure.GetMethod('IsApplicationControlBlock', $binding).Invoke($null, [object[]]@(2))) 'error 2 is not an application control block'
    $blockedText = [string]$launchFailure.GetMethod('Describe', $binding).Invoke($null, [object[]]@($blockedPath, 4551, 'An Application Control policy has blocked this file.'))
    Assert-True ($blockedText -like "*$blockedPath*") 'the blocked-file message names the file'
    Assert-True ($blockedText -like '*An Application Control policy has blocked this file.*' -and $blockedText -like '*error 4551*') 'the blocked-file message carries the Windows text and code'
    Assert-True ($blockedText -like '*Smart App Control*' -and $blockedText -like '*Windows Defender Application Control*') 'the blocked-file message explains which policies block files'
    $realException = [System.ComponentModel.Win32Exception]::new(4551)
    Write-Host "INFO: this Windows describes error 4551 as: $($realException.Message)"
    $startFailure = [string]$launchFailure.GetMethod('DescribeProcessStartFailure', $binding).Invoke($null, [object[]]@($blockedPath, $realException))
    Assert-True ($startFailure -like "*$blockedPath*" -and $startFailure -like '*Smart App Control*') 'a Process.Start failure with error 4551 is explained as a policy block'
    $missingText = [string]$launchFailure.GetMethod('Describe', $binding).Invoke($null, [object[]]@($blockedPath, 2, 'The system cannot find the file specified.'))
    Assert-True ($missingText -like "*$blockedPath*" -and $missingText -like '*is missing*') 'a missing game executable is named'
    Assert-True ($missingText -notlike '*Smart App Control*') 'a missing file is not blamed on application control'

    # --- Every failure leaves a log file that the dialog names ---
    $logPath = [string]$launcherLog.GetProperty('Path', $binding).GetValue($null)
    Assert-True ($logPath -eq (Join-Path $logDirectory 'launcher.log')) 'the log file lives in the configured directory'
    $launcherLog.GetMethod('Write', $binding, $null, [Type[]]@([string]), $null).Invoke($null, [object[]]@('integration test entry'))
    $thrown = [System.InvalidOperationException]::new('integration test exception')
    $launcherLog.GetMethod('Write', $binding, $null, [Type[]]@([string], [Exception]), $null).Invoke($null, [object[]]@('integration test failure', $thrown))
    $logText = Get-Content -LiteralPath $logPath -Raw
    Assert-True ($logText -like '*integration test entry*') 'a plain entry is written to the log'
    Assert-True ($logText -like '*integration test failure*InvalidOperationException*integration test exception*') 'an exception is written to the log with its type and message'
    Assert-True ($logText -like '*launcher session:*os=*clr=*') 'the log starts with the environment of the session'
    $fatalText = [string]$startupDiagnostics.GetMethod('BuildFatalMessage', $binding).Invoke($null, [object[]]@('the launcher failed during startup', $thrown, $null, $logPath))
    Assert-True ($fatalText -like "*$logPath*" -and $fatalText -like '*integration test exception*' -and $fatalText -like '*the launcher failed during startup*') 'the fatal dialog names what failed and where the log is'

    # --- Progress wording: the checking phases must say what they are doing ---
    function New-Status([string]$Phase, [string]$Transfer = 'Preparing', [string]$Backend = $null, [int]$Pass = 0, [string]$Detail = $null) {
        $status = [Activator]::CreateInstance($statusType)
        $statusType.GetProperty('Phase').SetValue($status, [Enum]::Parse($phaseType, $Phase))
        $statusType.GetProperty('TransferPhase').SetValue($status, [Enum]::Parse($transferPhaseType, $Transfer))
        if ($Backend) { $statusType.GetProperty('Backend').SetValue($status, [Enum]::Parse($backendType, $Backend)) }
        $statusType.GetProperty('VerificationPass').SetValue($status, $Pass)
        $statusType.GetProperty('MaxVerificationPasses').SetValue($status, 3)
        $statusType.GetProperty('Detail').SetValue($status, $Detail)
        return $status
    }
    function Get-ProgressText($Status, [long]$Bytes, [long]$Total, [double]$Rate) {
        return [string]$progressText.GetMethod('Compose', $binding).Invoke($null, [object[]]@($Status, $Bytes, $Total, $Rate, $null))
    }
    $gib = 1GB
    $verifying = Get-ProgressText (New-Status 'Verification' -Pass 1 -Detail 'File 2 of 190: englishclient_mp_common.bsp.pak000_040.vpk') (5 * $gib) (12 * $gib) (200MB)
    Assert-True ($verifying -like 'Verifying downloaded files (pass 1/3): 41.7% (5.0 GB / 12.0 GB)*') 'verification shows its own percentage and byte counts'
    Assert-True ($verifying -like '*File 2 of 190: englishclient_mp_common.bsp.pak000_040.vpk*') 'verification names the file being checked'
    Assert-True ($verifying -like '*Checking speed: 200.0 MB/s | ETA: 35s*') 'verification shows how fast files are read and when it will finish'
    $downloading = Get-ProgressText (New-Status 'Download' 'Downloading' 'Curl' -Detail '187 of 190 files complete, 118.4 MB left to download') (11.88 * $gib) (12 * $gib) 0
    Assert-True ($downloading -like 'Downloading with curl*99.0%*' -or $downloading -like 'Downloading with curl*98.*%*') 'the download phase keeps its heading and percentage'
    Assert-True ($downloading -like '*187 of 190 files complete, 118.4 MB left to download*') 'the download phase says how much is really left while the bar waits at 99%'
    Assert-True ($downloading -like '*Speed: 0 B/s | ETA: --:--*') 'a stalled transfer still reports its speed honestly'
    $preflight = Get-ProgressText (New-Status 'Preflight' -Detail 'File 7 of 190: bin/x64_retail/engine.dll') (3 * $gib) (12 * $gib) (150MB)
    Assert-True ($preflight -like 'Checking existing files: 25.0%*' -and $preflight -like '*File 7 of 190: bin/x64_retail/engine.dll*' -and $preflight -like '*Checking speed:*') 'checking existing files reports progress, file and read speed'
    $complete = Get-ProgressText (New-Status 'Complete') (12 * $gib) (12 * $gib) 0
    Assert-True ($complete -like 'Setup complete: 100.0%*' -and $complete -notlike '*speed*') 'completion shows 100% without a rate'

    Assert-True ($statusInterface.IsAssignableFrom($setupWindow)) 'SetupWindow consumes structured install status phases'
    $leasePath = Join-Path $root 'leased'
    $leaseArgs = [object[]]@([string]$leasePath, $null, $null)
    Assert-True ([bool]$acquireLease.Invoke($null, $leaseArgs)) 'first install-operation lease is acquired'
    $heldLease = [IDisposable]$leaseArgs[1]
    $lockPath = Join-Path $leasePath '.r1delta-install-operation.lock'
    Assert-True (Test-Path -LiteralPath $leasePath -PathType Container) 'install-operation lease creates the selected root'
    Assert-True (Test-Path -LiteralPath $lockPath -PathType Leaf) 'install-operation lease is rooted inside the shared install directory'
    try {
        $sameLeaseArgs = [object[]]@(
            [string]($leasePath + [System.IO.Path]::DirectorySeparatorChar),
            $null,
            $null)
        Assert-True (-not [bool]$acquireLease.Invoke($null, $sameLeaseArgs)) 'normalized same-root operation is excluded while leased'
        Assert-True (-not [string]::IsNullOrEmpty([string]$sameLeaseArgs[2])) 'same-root lease denial reports a concrete error'

        $otherLeaseArgs = [object[]]@([string](Join-Path $root 'other-leased'), $null, $null)
        Assert-True ([bool]$acquireLease.Invoke($null, $otherLeaseArgs)) 'independent install roots can be leased concurrently'
        ([IDisposable]$otherLeaseArgs[1]).Dispose()
    }
    finally {
        $heldLease.Dispose()
    }
    Assert-True (-not (Test-Path -LiteralPath $lockPath)) 'root-local install-operation lock is removed after disposal'

    $reacquireArgs = [object[]]@([string]$leasePath, $null, $null)
    Assert-True ([bool]$acquireLease.Invoke($null, $reacquireArgs)) 'install-operation lease is reacquired after disposal'
    ([IDisposable]$reacquireArgs[1]).Dispose()


    $artifact = Join-Path $root 'artifact.bin'
    @('', '.aria2', '.part', '.curl.partial') | ForEach-Object {
        Set-Content -LiteralPath ($artifact + $_) -Value $_
    }
    Assert-True ([bool]$hasSidecars.Invoke($null, [object[]]@([string]$artifact))) 'download sidecar detector sees resume artifacts'
    $deleted = [int]$deleteArtifacts.Invoke($null, [object[]]@([string]$artifact))
    Assert-True ($deleted -eq 4) 'download artifact cleanup removes final, aria2, part, and curl sidecars'
    Assert-True (-not [bool]$hasSidecars.Invoke($null, [object[]]@([string]$artifact))) 'download artifact cleanup leaves no sidecars'
    Assert-True (-not (Test-Path -LiteralPath $artifact)) 'download artifact cleanup removes the final file'

    $managedPath = Join-Path $root 'managed'
    $ensureArgs = [object[]]@([string]$managedPath, $null)
    Assert-True ([bool]$ensureOwnership.Invoke($null, $ensureArgs)) 'managed ownership marker is created for a confirmed fresh destination'
    Assert-True ([string]::IsNullOrEmpty([string]$ensureArgs[1])) 'managed ownership creation reports no error'
    Assert-True ([bool]$hasOwnership.Invoke($null, [object[]]@([string]$managedPath))) 'created managed ownership marker validates'
    $markerPath = Join-Path $managedPath '.r1delta-managed-install'
    $expectedMarkerBytes = [System.Text.Encoding]::UTF8.GetBytes("R1DELTA_MANAGED_INSTALL_V1`r`n")
    Assert-True ([System.Linq.Enumerable]::SequenceEqual[byte](
        [System.IO.File]::ReadAllBytes($markerPath),
        $expectedMarkerBytes)) 'managed ownership marker has exact versioned bytes'

    $ensureAgainArgs = [object[]]@([string]$managedPath, $null)
    Assert-True ([bool]$ensureOwnership.Invoke($null, $ensureAgainArgs)) 'managed ownership creation is idempotent'

    $malformedPath = Join-Path $root 'malformed'
    New-Item -ItemType Directory -Path $malformedPath | Out-Null
    Set-Content -LiteralPath (Join-Path $malformedPath '.r1delta-managed-install') -Value 'not-r1delta'
    Assert-True (-not [bool]$hasOwnership.Invoke($null, [object[]]@([string]$malformedPath))) 'malformed ownership marker is rejected'
    $malformedEnsureArgs = [object[]]@([string]$malformedPath, $null)
    Assert-True (-not [bool]$ensureOwnership.Invoke($null, $malformedEnsureArgs)) 'malformed ownership marker is not overwritten'
    $malformedRemoveArgs = [object[]]@([string]$malformedPath, $null)
    Assert-True (-not [bool]$removeOwnership.Invoke($null, $malformedRemoveArgs)) 'malformed ownership marker is not removed'
    Assert-True (Test-Path -LiteralPath (Join-Path $malformedPath '.r1delta-managed-install')) 'malformed ownership marker remains for manual recovery'

    $legacyPath = Join-Path $root 'legacy'
    New-Item -ItemType Directory -Path (Join-Path $legacyPath 'vpk') -Force | Out-Null
    New-Item -ItemType File -Path (Join-Path $legacyPath 'vpk/client_mp_delta_common.bsp.pak000_000.vpk') | Out-Null
    Assert-True ([bool]$hasLegacyProof.Invoke($null, [object[]]@([string]$legacyPath))) 'legacy completed delta VPK authorizes legacy cleanup'
    Assert-True (-not [bool]$cleanupAuthorized.Invoke($null, [object[]]@($false, $false))) 'manifest files and sidecars alone do not authorize cleanup'
    Assert-True ([bool]$cleanupAuthorized.Invoke($null, [object[]]@($true, $false))) 'valid managed marker authorizes cleanup'
    Assert-True ([bool]$cleanupAuthorized.Invoke($null, [object[]]@($false, $true))) 'legacy completed delta VPK authorizes cleanup'

    $ownershipRacePath = Join-Path $root 'ownership-race'
    $legacyRaceVpk = Join-Path $ownershipRacePath 'vpk/client_mp_delta_common.bsp.pak000_000.vpk'
    New-Item -ItemType Directory -Path (Split-Path -Parent $legacyRaceVpk) -Force | Out-Null
    New-Item -ItemType File -Path $legacyRaceVpk | Out-Null
    Assert-True ([bool]$hasLegacyProof.Invoke($null, [object[]]@([string]$ownershipRacePath))) 'legacy ownership is visible before an uninstall confirmation gap'
    $raceEnsureArgs = [object[]]@([string]$ownershipRacePath, $null)
    Assert-True ([bool]$ensureOwnership.Invoke($null, $raceEnsureArgs)) 'concurrent setup can replace legacy proof with managed ownership'
    Remove-Item -LiteralPath $legacyRaceVpk -Force
    $raceLeaseArgs = [object[]]@([string]$ownershipRacePath, $null, $null)
    Assert-True ([bool]$acquireLease.Invoke($null, $raceLeaseArgs)) 'uninstall acquires the shared-root lease after confirmation'
    try {
        $refreshedManaged = [bool]$hasOwnership.Invoke($null, [object[]]@([string]$ownershipRacePath))
        $refreshedLegacy = [bool]$hasLegacyProof.Invoke($null, [object[]]@([string]$ownershipRacePath))
        Assert-True ($refreshedManaged -and -not $refreshedLegacy) 'ownership proofs are refreshed after the prompt gap'
        Assert-True ([bool]$cleanupAuthorized.Invoke($null, [object[]]@($refreshedManaged, $refreshedLegacy))) 'refreshed managed ownership authorizes cleanup and marker removal'
    }
    finally {
        ([IDisposable]$raceLeaseArgs[1]).Dispose()
    }

    $removeArgs = [object[]]@([string]$managedPath, $null)
    Assert-True ([bool]$removeOwnership.Invoke($null, $removeArgs)) 'valid managed ownership marker can be removed after successful cleanup'
    Assert-True (-not (Test-Path -LiteralPath $markerPath)) 'managed ownership marker removal is observable'

    Write-Host 'All installer integration smoke tests passed.'
}
finally {
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $logDirectory -Recurse -Force -ErrorAction SilentlyContinue
}
