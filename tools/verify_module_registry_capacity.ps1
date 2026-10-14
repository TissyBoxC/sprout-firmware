[CmdletBinding()]
param(
    [string]$FirmwareRoot = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($FirmwareRoot)) {
    $FirmwareRoot = Split-Path -Parent $PSScriptRoot
}

# The composition root registers one module per enabled feature macro. A build
# whose static capacity is smaller than the number of enabled registrations
# aborts at boot: module_registry_add returns ESP_ERR_NO_MEM and the
# ESP_ERROR_CHECK in main.c propagates it before startup completes.
#
# The strongest profile-independent invariant is
#   capacity >= (number of registration guards in main.c)
# because no product profile can ever enable more registrations than exist.
# This also holds in CI, where the gitignored per-environment sdkconfig files
# are absent and only the tracked *.defaults overlays exist.

function Get-RegisteredFeatureMacros {
    param(
        [Parameter(Mandatory = $true)]
        [string]$CompositionPath
    )

    $macros = New-Object System.Collections.Generic.List[string]
    $pendingGuard = $null

    foreach ($line in Get-Content -LiteralPath $CompositionPath) {
        $trimmed = $line.Trim()
        if ($trimmed -match '^#if\s+CONFIG_FEATURE_([A-Z0-9_]+)\s*$') {
            $pendingGuard = $Matches[1]
            continue
        }
        if ($null -ne $pendingGuard -and $trimmed -match 'module_registry_add\(') {
            $macros.Add($pendingGuard)
            $pendingGuard = $null
            continue
        }
        if ($trimmed -match '^#endif') {
            $pendingGuard = $null
        }
    }

    return $macros
}

function Get-CapacityFromFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match '^CONFIG_MODULE_REGISTRY_CAPACITY=(\d+)\s*$') {
            return [int]$Matches[1]
        }
    }
    return $null
}

function Get-CapacityFromKconfig {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $inCapacity = $false
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match '^config\s+MODULE_REGISTRY_CAPACITY\s*$') {
            $inCapacity = $true
            continue
        }
        if ($inCapacity) {
            if ($line -match '^\s*default\s+(\d+)\s*$') {
                return [int]$Matches[1]
            }
            if ($line -match '^config\s+') {
                break
            }
        }
    }
    return $null
}

$compositionPath = Join-Path $FirmwareRoot 'src/main.c'
$registeredMacros = Get-RegisteredFeatureMacros -CompositionPath $compositionPath
$registrationCount = $registeredMacros.Count
if ($registrationCount -eq 0) {
    throw "No module registrations were found in $compositionPath."
}

$failures = New-Object System.Collections.Generic.List[string]

$defaultsPath = Join-Path $FirmwareRoot 'sdkconfig.defaults'
$defaultsCapacity = Get-CapacityFromFile -Path $defaultsPath
if ($null -eq $defaultsCapacity) {
    throw 'sdkconfig.defaults does not define CONFIG_MODULE_REGISTRY_CAPACITY.'
}
'{0,-6} {1,-24} capacity={2,-4} registrations={3}' -f `
    $(if ($defaultsCapacity -ge $registrationCount) { 'ok' } else { 'FAIL' }), `
    'sdkconfig.defaults', $defaultsCapacity, $registrationCount
if ($defaultsCapacity -lt $registrationCount) {
    $failures.Add(
        "sdkconfig.defaults capacity $defaultsCapacity < $registrationCount registrations"
    )
}

$kconfigPath = Join-Path $FirmwareRoot 'components/module_registry/Kconfig'
$kconfigCapacity = Get-CapacityFromKconfig -Path $kconfigPath
if ($null -eq $kconfigCapacity) {
    throw 'module_registry/Kconfig does not define a default capacity.'
}
'{0,-6} {1,-24} capacity={2,-4} registrations={3}' -f `
    $(if ($kconfigCapacity -ge $registrationCount) { 'ok' } else { 'FAIL' }), `
    'Kconfig default', $kconfigCapacity, $registrationCount
if ($kconfigCapacity -lt $registrationCount) {
    $failures.Add(
        "Kconfig default capacity $kconfigCapacity < $registrationCount registrations"
    )
}

if ($failures.Count -gt 0) {
    throw "module_registry capacity is too small:`n$($failures -join "`n")"
}

Write-Host 'module_registry capacity verification passed.'
