[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$firmwareRoot = Split-Path -Parent $PSScriptRoot

function Resolve-HostCompiler {
    foreach ($candidate in @('g++', 'clang++')) {
        $command = Get-Command $candidate -ErrorAction SilentlyContinue
        if ($null -ne $command) {
            return $command.Source
        }
    }
    throw 'No host C++ compiler (g++ or clang++) was found.'
}

function Invoke-HostTest {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,

        [Parameter(Mandatory = $true)]
        [string[]]$Sources,

        [string[]]$IncludeDirectories = @(),

        # Optional arguments and an empty include list are legitimate here;
        # binding an empty array to a mandatory parameter fails under pwsh 7.
        [string[]]$AdditionalArguments = @(),

        [Parameter(Mandatory = $true)]
        [string]$Compiler
    )

    $outputDirectory = Join-Path ([System.IO.Path]::GetTempPath()) (
        'sprout-host-' + [System.Guid]::NewGuid().ToString('N').Substring(0, 8)
    )
    New-Item -ItemType Directory -Path $outputDirectory | Out-Null
    $executable = Join-Path $outputDirectory ($Name + '.exe')

    try {
        $arguments = @(
            '-std=c++17'
            '-Wall'
            '-Wextra'
            '-Werror'
        )
        foreach ($includeDirectory in $IncludeDirectories) {
            $arguments += '-I' + $includeDirectory
        }
        $arguments += $AdditionalArguments
        $arguments += $Sources
        $arguments += @('-o', $executable)

        Write-Host "Building host test $Name"
        & $Compiler @arguments
        if ($LASTEXITCODE -ne 0) {
            throw "Host test $Name failed to compile."
        }

        Write-Host "Running host test $Name"
        & $executable
        if ($LASTEXITCODE -ne 0) {
            throw "Host test $Name failed."
        }
    } finally {
        if (Test-Path -LiteralPath $outputDirectory) {
            Remove-Item -LiteralPath $outputDirectory -Recurse -Force
        }
    }
}

$compiler = Resolve-HostCompiler

$otaValidateRoot = Join-Path $firmwareRoot 'components/ota_validate'
Invoke-HostTest `
    -Name 'ota_manifest' `
    -Sources @(
        (Join-Path $otaValidateRoot 'src/ota_manifest.c')
        (Join-Path $otaValidateRoot 'test/test_ota_manifest.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $otaValidateRoot 'include')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$otaDownloadRoot = Join-Path $firmwareRoot 'components/ota_download'
Invoke-HostTest `
    -Name 'ota_download_core' `
    -Sources @(
        (Join-Path $otaDownloadRoot 'src/ota_download_core.c')
        (Join-Path $otaValidateRoot 'src/ota_manifest.c')
        (Join-Path $otaDownloadRoot 'test/test_ota_download_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $otaDownloadRoot 'include')
        (Join-Path $otaValidateRoot 'include')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$otaManagerRoot = Join-Path $firmwareRoot 'components/ota_manager'
Invoke-HostTest `
    -Name 'ota_manager_event_sequence' `
    -Sources @(
        (Join-Path $otaManagerRoot 'src/ota_manager_event_sequence.c')
        (Join-Path $otaManagerRoot 'test/test_ota_manager_event_sequence.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $otaManagerRoot 'include')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'ota_manager_state' `
    -Sources @(
        (Join-Path $otaManagerRoot 'src/ota_manager_state.c')
        (Join-Path $otaManagerRoot 'test/test_ota_manager_state.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $otaManagerRoot 'include')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$otaRollbackRoot = Join-Path $firmwareRoot 'components/ota_rollback'
Invoke-HostTest `
    -Name 'ota_rollback_policy' `
    -Sources @(
        (Join-Path $otaRollbackRoot 'src/ota_rollback_policy.c')
        (Join-Path $otaValidateRoot 'src/ota_manifest.c')
        (Join-Path $otaRollbackRoot 'test/test_ota_rollback_policy.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $otaRollbackRoot 'include')
        (Join-Path $otaValidateRoot 'include')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$diagnosticRoot = Join-Path $firmwareRoot 'components/diagnostic_reporter'
Invoke-HostTest `
    -Name 'diagnostic_reporter_state' `
    -Sources @(
        (Join-Path $diagnosticRoot 'src/diagnostic_reporter_state.cpp')
        (Join-Path $diagnosticRoot 'test/test_diagnostic_reporter_state.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $diagnosticRoot 'include')
        (Join-Path $diagnosticRoot 'src')
        (Join-Path $diagnosticRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$runtimeRoot = Join-Path $firmwareRoot 'components/device_runtime_reporter'
Invoke-HostTest `
    -Name 'device_runtime_command_contract' `
    -Sources @(
        (Join-Path $runtimeRoot 'test/test_device_runtime_command_contract.cpp')
    ) `
    -IncludeDirectories @() `
    -AdditionalArguments @() `
    -Compiler $compiler

$provisioningRoot = Join-Path $firmwareRoot 'components/provisioning_reporter'
Invoke-HostTest `
    -Name 'provisioning_reporter_state' `
    -Sources @(
        (Join-Path $provisioningRoot 'src/provisioning_reporter_state.cpp')
        (Join-Path $provisioningRoot 'test/test_provisioning_reporter_state.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $provisioningRoot 'include')
        (Join-Path $provisioningRoot 'src')
        (Join-Path $provisioningRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$voiceSessionRoot = Join-Path $firmwareRoot 'components/voice_session'
Invoke-HostTest `
    -Name 'voice_session_protocol' `
    -Sources @(
        (Join-Path $voiceSessionRoot 'src/voice_session_protocol.c')
        (Join-Path $voiceSessionRoot 'test/test_voice_session_protocol.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $voiceSessionRoot 'include')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$transportSecurityRoot = Join-Path $firmwareRoot 'components/transport_security'
Invoke-HostTest `
    -Name 'transport_security' `
    -Sources @(
        (Join-Path $transportSecurityRoot 'src/transport_security.c')
        (Join-Path $transportSecurityRoot 'src/transport_security_core.c')
        (Join-Path $transportSecurityRoot 'test/test_transport_security.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $transportSecurityRoot 'include')
        (Join-Path $transportSecurityRoot 'src')
        (Join-Path $transportSecurityRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$privacyGuardRoot = Join-Path $firmwareRoot 'components/privacy_guard'
Invoke-HostTest `
    -Name 'privacy_guard_core' `
    -Sources @(
        (Join-Path $privacyGuardRoot 'src/privacy_guard_core.c')
        (Join-Path $privacyGuardRoot 'test/test_privacy_guard_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $privacyGuardRoot 'include')
        (Join-Path $privacyGuardRoot 'src')
        (Join-Path $privacyGuardRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'privacy_guard_runtime' `
    -Sources @(
        (Join-Path $privacyGuardRoot 'src/privacy_guard_core.c')
        (Join-Path $privacyGuardRoot 'src/privacy_guard.c')
        (Join-Path $privacyGuardRoot 'test/stubs/config_store.c')
        (Join-Path $privacyGuardRoot 'test/test_privacy_guard_runtime.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $privacyGuardRoot 'include')
        (Join-Path $privacyGuardRoot 'src')
        (Join-Path $privacyGuardRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$contentFilterRoot = Join-Path $firmwareRoot 'components/content_filter'
Invoke-HostTest `
    -Name 'content_filter' `
    -Sources @(
        (Join-Path $contentFilterRoot 'src/content_filter_core.c')
        (Join-Path $contentFilterRoot 'src/content_filter.c')
        (Join-Path $contentFilterRoot 'test/test_content_filter.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $contentFilterRoot 'include')
        (Join-Path $contentFilterRoot 'src')
        (Join-Path $contentFilterRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$conversationContextRoot = Join-Path $firmwareRoot 'components/conversation_context'
Invoke-HostTest `
    -Name 'conversation_context_ring' `
    -Sources @(
        (Join-Path $conversationContextRoot 'src/conversation_context_ring.c')
        (Join-Path $conversationContextRoot 'test/test_conversation_context_ring.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $conversationContextRoot 'include')
        (Join-Path $conversationContextRoot 'src')
        (Join-Path $conversationContextRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$contentLibraryRoot = Join-Path $firmwareRoot 'components/content_library'
Invoke-HostTest `
    -Name 'content_library_index' `
    -Sources @(
        (Join-Path $contentLibraryRoot 'src/content_library_index.c')
        (Join-Path $contentLibraryRoot 'test/test_content_library_index.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $contentLibraryRoot 'include')
        (Join-Path $contentLibraryRoot 'src')
        (Join-Path $contentLibraryRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$contentDownloaderRoot = Join-Path $firmwareRoot 'components/content_downloader'
Invoke-HostTest `
    -Name 'content_download_state' `
    -Sources @(
        (Join-Path $contentDownloaderRoot 'src/content_download_state.c')
        (Join-Path $contentDownloaderRoot 'test/test_content_download_state.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $contentDownloaderRoot 'include')
        (Join-Path $contentDownloaderRoot 'src')
        (Join-Path $contentDownloaderRoot 'test/stubs')
        (Join-Path $contentLibraryRoot 'include')
        (Join-Path $firmwareRoot 'components/audio_codec/include')
        (Join-Path $firmwareRoot 'components/playback_queue/include')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$contentPackageRoot = Join-Path $firmwareRoot 'components/content_package_manager'
Invoke-HostTest `
    -Name 'content_package_state' `
    -Sources @(
        (Join-Path $contentPackageRoot 'src/content_package_state.c')
        (Join-Path $contentPackageRoot 'test/test_content_package_state.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $contentPackageRoot 'include')
        (Join-Path $contentPackageRoot 'src')
        (Join-Path $contentPackageRoot 'test/stubs')
        (Join-Path $contentLibraryRoot 'include')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'content_playback_chunker' `
    -Sources @(
        (Join-Path $contentDownloaderRoot 'src/content_playback_chunker.c')
        (Join-Path $contentDownloaderRoot 'test/test_content_playback_chunker.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $contentDownloaderRoot 'include')
        (Join-Path $contentDownloaderRoot 'src')
        (Join-Path $contentDownloaderRoot 'test/stubs')
        (Join-Path $contentLibraryRoot 'include')
        (Join-Path $firmwareRoot 'components/audio_codec/include')
        (Join-Path $firmwareRoot 'components/playback_queue/include')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$timeSyncRoot = Join-Path $firmwareRoot 'components/time_sync'
Invoke-HostTest `
    -Name 'time_sync_timezone_boundaries' `
    -Sources @(
        (Join-Path $timeSyncRoot 'src/time_sync_pure.c')
        (Join-Path $timeSyncRoot 'test/test_timezone_boundaries.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $timeSyncRoot 'include')
        (Join-Path $timeSyncRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$usageLedgerRoot = Join-Path $firmwareRoot 'components/usage_ledger'
Invoke-HostTest `
    -Name 'usage_ledger_state' `
    -Sources @(
        (Join-Path $usageLedgerRoot 'src/usage_ledger_core.c')
        (Join-Path $usageLedgerRoot 'test/test_usage_ledger.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $usageLedgerRoot 'include')
        (Join-Path $usageLedgerRoot 'src')
        (Join-Path $usageLedgerRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$parentControlRoot = Join-Path $firmwareRoot 'components/parent_control_runtime'
Invoke-HostTest `
    -Name 'parent_control_policy_evaluator' `
    -Sources @(
        (Join-Path $parentControlRoot 'src/parent_control_decision.c')
        (Join-Path $parentControlRoot 'test/test_policy_evaluator.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $parentControlRoot 'include')
        (Join-Path $parentControlRoot 'src')
        (Join-Path $firmwareRoot 'components/parent_policy/include')
        (Join-Path $firmwareRoot 'components/usage_ledger/include')
        (Join-Path $parentControlRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'parent_control_consumption_gate' `
    -Sources @(
        (Join-Path $parentControlRoot 'src/parent_control_decision.c')
        (Join-Path $parentControlRoot 'test/test_consumption_gate.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $parentControlRoot 'include')
        (Join-Path $parentControlRoot 'src')
        (Join-Path $firmwareRoot 'components/parent_policy/include')
        (Join-Path $firmwareRoot 'components/usage_ledger/include')
        (Join-Path $parentControlRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

Write-Host 'Host verification passed.'
