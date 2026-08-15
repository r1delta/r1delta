param(
    [Parameter(Mandatory = $true)]
    [string]$LauncherPath
)

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
}
