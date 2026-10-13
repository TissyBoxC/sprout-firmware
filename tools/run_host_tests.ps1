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

$contentFilterRoot = Join-Path $firmwareRoot 'components/content_filter'

$securityPostureRoot = Join-Path $firmwareRoot 'components/security_posture'
Invoke-HostTest `
    -Name 'security_posture_core' `
    -Sources @(
        (Join-Path $securityPostureRoot 'src/security_posture_core.c')
        (Join-Path $securityPostureRoot 'test/test_security_posture_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $securityPostureRoot 'include')
        (Join-Path $securityPostureRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

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

$deviceMessageRoot = Join-Path $firmwareRoot 'components/device_message'
Invoke-HostTest `
    -Name 'device_message_core' `
    -Sources @(
        (Join-Path $deviceMessageRoot 'src/device_message_core.c')
        (Join-Path $deviceMessageRoot 'test/test_device_message_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $deviceMessageRoot 'include')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'device_message_runtime' `
    -Sources @(
        (Join-Path $deviceMessageRoot 'src/device_message_core.c')
        (Join-Path $deviceMessageRoot 'src/device_message.c')
        (Join-Path $deviceMessageRoot 'test/stubs/device_capabilities_stub.c')
        (Join-Path $contentFilterRoot 'src/content_filter_core.c')
        (Join-Path $contentFilterRoot 'src/content_filter.c')
        (Join-Path $deviceMessageRoot 'test/test_device_message_runtime.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $deviceMessageRoot 'include')
        (Join-Path $deviceMessageRoot 'src')
        (Join-Path $deviceMessageRoot 'test/stubs')
        (Join-Path $contentFilterRoot 'include')
        (Join-Path $contentFilterRoot 'src')
        (Join-Path $contentFilterRoot 'test/stubs')
    ) `
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

Invoke-HostTest `
    -Name 'voice_duplex_core' `
    -Sources @(
        (Join-Path $voiceSessionRoot 'src/voice_duplex_core.c')
        (Join-Path $voiceSessionRoot 'test/test_voice_duplex_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $voiceSessionRoot 'src')
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

Invoke-HostTest `
    -Name 'privacy_guard_production' `
    -Sources @(
        (Join-Path $privacyGuardRoot 'src/privacy_guard_core.c')
        (Join-Path $privacyGuardRoot 'src/privacy_guard.c')
        (Join-Path $privacyGuardRoot 'test/stubs/config_store.c')
        (Join-Path $privacyGuardRoot 'test/test_privacy_guard_production.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $privacyGuardRoot 'include')
        (Join-Path $privacyGuardRoot 'src')
        (Join-Path $privacyGuardRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$cameraCaptureRoot = Join-Path $firmwareRoot 'components/camera_capture'
Invoke-HostTest `
    -Name 'camera_capture_core' `
    -Sources @(
        (Join-Path $cameraCaptureRoot 'src/camera_capture_core.c')
        (Join-Path $cameraCaptureRoot 'test/test_camera_capture_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $cameraCaptureRoot 'include')
        (Join-Path $cameraCaptureRoot 'src')
        (Join-Path $cameraCaptureRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'camera_capture_runtime' `
    -Sources @(
        (Join-Path $cameraCaptureRoot 'src/camera_capture_core.c')
        (Join-Path $cameraCaptureRoot 'src/camera_capture.c')
        (Join-Path $privacyGuardRoot 'src/privacy_guard_core.c')
        (Join-Path $privacyGuardRoot 'src/privacy_guard.c')
        (Join-Path $cameraCaptureRoot 'test/stubs/config_store.c')
        (Join-Path $cameraCaptureRoot 'test/test_camera_capture_runtime.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $cameraCaptureRoot 'include')
        (Join-Path $cameraCaptureRoot 'src')
        (Join-Path $cameraCaptureRoot 'test/stubs')
        (Join-Path $privacyGuardRoot 'include')
        (Join-Path $privacyGuardRoot 'src')
    ) `
    -AdditionalArguments @('-DCONFIG_FEATURE_PRIVACY_GUARD=1') `
    -Compiler $compiler

$cameraVisionRoot = Join-Path $firmwareRoot 'components/camera_vision'
Invoke-HostTest `
    -Name 'camera_vision_core' `
    -Sources @(
        (Join-Path $cameraVisionRoot 'src/camera_vision_core.c')
        (Join-Path $cameraVisionRoot 'test/test_camera_vision_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $cameraVisionRoot 'include')
        (Join-Path $cameraVisionRoot 'src')
        (Join-Path $cameraVisionRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'camera_vision_runtime' `
    -Sources @(
        (Join-Path $cameraVisionRoot 'src/camera_vision_core.c')
        (Join-Path $cameraVisionRoot 'src/camera_vision.c')
        (Join-Path $privacyGuardRoot 'src/privacy_guard_core.c')
        (Join-Path $privacyGuardRoot 'src/privacy_guard.c')
        (Join-Path $cameraVisionRoot 'test/stubs/config_store.c')
        (Join-Path $cameraVisionRoot 'test/test_camera_vision_runtime.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $cameraVisionRoot 'include')
        (Join-Path $cameraVisionRoot 'src')
        (Join-Path $cameraVisionRoot 'test/stubs')
        (Join-Path $privacyGuardRoot 'include')
        (Join-Path $privacyGuardRoot 'src')
    ) `
    -AdditionalArguments @('-DCONFIG_FEATURE_PRIVACY_GUARD=1') `
    -Compiler $compiler

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

Invoke-HostTest `
    -Name 'conversation_context_runtime' `
    -Sources @(
        (Join-Path $conversationContextRoot 'src/conversation_context_ring.c')
        (Join-Path $conversationContextRoot 'src/conversation_context.c')
        (Join-Path $privacyGuardRoot 'src/privacy_guard_core.c')
        (Join-Path $privacyGuardRoot 'src/privacy_guard.c')
        (Join-Path $privacyGuardRoot 'test/stubs/config_store.c')
        (Join-Path $conversationContextRoot 'test/test_conversation_context_runtime.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $conversationContextRoot 'include')
        (Join-Path $conversationContextRoot 'src')
        (Join-Path $conversationContextRoot 'test/stubs')
        (Join-Path $privacyGuardRoot 'include')
        (Join-Path $privacyGuardRoot 'src')
        (Join-Path $privacyGuardRoot 'test/stubs')
    ) `
    -AdditionalArguments @('-DCONFIG_FEATURE_PRIVACY_GUARD=1') `
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

$uiTextRoot = Join-Path $firmwareRoot 'components/ui_text'
Invoke-HostTest `
    -Name 'ui_text_store' `
    -Sources @(
        (Join-Path $uiTextRoot 'src/ui_text_store.c')
        (Join-Path $uiTextRoot 'test/test_ui_text_store.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $uiTextRoot 'include')
        (Join-Path $uiTextRoot 'src')
        (Join-Path $uiTextRoot 'test/stubs')
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

$favoritesRoot = Join-Path $firmwareRoot 'components/favorites'
Invoke-HostTest `
    -Name 'favorites_core' `
    -Sources @(
        (Join-Path $favoritesRoot 'src/favorites_core.c')
        (Join-Path $favoritesRoot 'test/test_favorites_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $favoritesRoot 'include')
        (Join-Path $favoritesRoot 'src')
        (Join-Path $contentLibraryRoot 'include')
        (Join-Path $favoritesRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'favorites_sync_core' `
    -Sources @(
        (Join-Path $favoritesRoot 'src/favorites_core.c')
        (Join-Path $favoritesRoot 'src/favorites_sync_core.c')
        (Join-Path $favoritesRoot 'test/test_favorites_sync_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $favoritesRoot 'include')
        (Join-Path $favoritesRoot 'src')
        (Join-Path $contentLibraryRoot 'include')
        (Join-Path $favoritesRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$playbackHistoryRoot = Join-Path $firmwareRoot 'components/playback_history'
Invoke-HostTest `
    -Name 'playback_history_core' `
    -Sources @(
        (Join-Path $playbackHistoryRoot 'src/playback_history_core.c')
        (Join-Path $playbackHistoryRoot 'test/test_playback_history_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $playbackHistoryRoot 'include')
        (Join-Path $playbackHistoryRoot 'src')
        (Join-Path $contentLibraryRoot 'include')
        (Join-Path $playbackHistoryRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$themePackageRoot = Join-Path $firmwareRoot 'components/theme_package'
Invoke-HostTest `
    -Name 'theme_package_core' `
    -Sources @(
        (Join-Path $themePackageRoot 'src/theme_package_core.c')
        (Join-Path $themePackageRoot 'test/test_theme_package_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $themePackageRoot 'include')
        (Join-Path $themePackageRoot 'src')
        (Join-Path $contentLibraryRoot 'include')
        (Join-Path $themePackageRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'theme_package_sync_core' `
    -Sources @(
        (Join-Path $themePackageRoot 'src/theme_package_core.c')
        (Join-Path $themePackageRoot 'src/theme_package_sync_core.c')
        (Join-Path $themePackageRoot 'test/test_theme_package_sync_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $themePackageRoot 'include')
        (Join-Path $themePackageRoot 'src')
        (Join-Path $contentLibraryRoot 'include')
        (Join-Path $themePackageRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$growthRecordRoot = Join-Path $firmwareRoot 'components/growth_record'
Invoke-HostTest `
    -Name 'growth_record_core' `
    -Sources @(
        (Join-Path $growthRecordRoot 'src/growth_record_core.c')
        (Join-Path $growthRecordRoot 'test/test_growth_record_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $growthRecordRoot 'include')
        (Join-Path $growthRecordRoot 'src')
        (Join-Path $growthRecordRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$habitReminderRoot = Join-Path $firmwareRoot 'components/habit_reminder'
Invoke-HostTest `
    -Name 'habit_reminder_core' `
    -Sources @(
        (Join-Path $habitReminderRoot 'src/habit_reminder_core.c')
        (Join-Path $habitReminderRoot 'test/test_habit_reminder_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $habitReminderRoot 'include')
        (Join-Path $habitReminderRoot 'src')
        (Join-Path $habitReminderRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$alarmReminderRoot = Join-Path $firmwareRoot 'components/alarm_reminder'
Invoke-HostTest `
    -Name 'alarm_reminder_core' `
    -Sources @(
        (Join-Path $alarmReminderRoot 'src/alarm_reminder_core.c')
        (Join-Path $alarmReminderRoot 'test/test_alarm_reminder_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $alarmReminderRoot 'include')
        (Join-Path $alarmReminderRoot 'src')
        (Join-Path $alarmReminderRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'alarm_reminder_sync_core' `
    -Sources @(
        (Join-Path $alarmReminderRoot 'src/alarm_reminder_core.c')
        (Join-Path $alarmReminderRoot 'src/alarm_reminder_sync_core.c')
        (Join-Path $alarmReminderRoot 'test/test_alarm_reminder_sync_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $alarmReminderRoot 'include')
        (Join-Path $alarmReminderRoot 'src')
        (Join-Path $alarmReminderRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$bluetoothAudioRoot = Join-Path $firmwareRoot 'components/bluetooth_audio'
Invoke-HostTest `
    -Name 'bluetooth_core' `
    -Sources @(
        (Join-Path $bluetoothAudioRoot 'src/bluetooth_core.c')
        (Join-Path $bluetoothAudioRoot 'test/test_bluetooth_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $bluetoothAudioRoot 'include')
        (Join-Path $bluetoothAudioRoot 'src')
        (Join-Path $bluetoothAudioRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'bluetooth_audio_runtime' `
    -Sources @(
        (Join-Path $bluetoothAudioRoot 'src/bluetooth_core.c')
        (Join-Path $bluetoothAudioRoot 'src/bluetooth_audio.c')
        (Join-Path $bluetoothAudioRoot 'test/stubs/config_store.c')
        (Join-Path $bluetoothAudioRoot 'test/test_bluetooth_runtime.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $bluetoothAudioRoot 'include')
        (Join-Path $bluetoothAudioRoot 'src')
        (Join-Path $bluetoothAudioRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$learningVisualizationRoot = Join-Path $firmwareRoot 'components/learning_visualization'
Invoke-HostTest `
    -Name 'learning_core' `
    -Sources @(
        (Join-Path $learningVisualizationRoot 'src/learning_core.c')
        (Join-Path $learningVisualizationRoot 'test/test_learning_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $learningVisualizationRoot 'include')
        (Join-Path $learningVisualizationRoot 'src')
        (Join-Path $learningVisualizationRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'learning_visualization_runtime' `
    -Sources @(
        (Join-Path $learningVisualizationRoot 'src/learning_core.c')
        (Join-Path $learningVisualizationRoot 'src/learning_visualization.c')
        (Join-Path $learningVisualizationRoot 'test/stubs/config_store.c')
        (Join-Path $learningVisualizationRoot 'test/test_learning_runtime.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $learningVisualizationRoot 'include')
        (Join-Path $learningVisualizationRoot 'src')
        (Join-Path $learningVisualizationRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$imagePrivacyRoot = Join-Path $firmwareRoot 'components/image_privacy'
Invoke-HostTest `
    -Name 'image_core' `
    -Sources @(
        (Join-Path $imagePrivacyRoot 'src/image_core.c')
        (Join-Path $imagePrivacyRoot 'test/test_image_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $imagePrivacyRoot 'include')
        (Join-Path $imagePrivacyRoot 'src')
        (Join-Path $imagePrivacyRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'image_privacy_runtime' `
    -Sources @(
        (Join-Path $imagePrivacyRoot 'src/image_core.c')
        (Join-Path $imagePrivacyRoot 'src/image_privacy.c')
        (Join-Path $imagePrivacyRoot 'test/stubs/config_store.c')
        (Join-Path $imagePrivacyRoot 'test/test_image_runtime.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $imagePrivacyRoot 'include')
        (Join-Path $imagePrivacyRoot 'src')
        (Join-Path $imagePrivacyRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$companionshipRoot = Join-Path $firmwareRoot 'components/companionship'
Invoke-HostTest `
    -Name 'companionship_core' `
    -Sources @(
        (Join-Path $companionshipRoot 'src/companionship_core.c')
        (Join-Path $companionshipRoot 'test/test_companionship_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $companionshipRoot 'include')
        (Join-Path $companionshipRoot 'src')
        (Join-Path $companionshipRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$longTermMemoryRoot = Join-Path $firmwareRoot 'components/long_term_memory'
Invoke-HostTest `
    -Name 'long_term_memory_core' `
    -Sources @(
        (Join-Path $longTermMemoryRoot 'src/long_term_memory_core.c')
        (Join-Path $longTermMemoryRoot 'test/test_long_term_memory_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $longTermMemoryRoot 'include')
        (Join-Path $longTermMemoryRoot 'src')
        (Join-Path $longTermMemoryRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'long_term_memory_runtime' `
    -Sources @(
        (Join-Path $longTermMemoryRoot 'src/long_term_memory_core.c')
        (Join-Path $longTermMemoryRoot 'src/long_term_memory.c')
        (Join-Path $longTermMemoryRoot 'test/stubs/config_store.c')
        (Join-Path $longTermMemoryRoot 'test/test_long_term_memory_runtime.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $longTermMemoryRoot 'include')
        (Join-Path $longTermMemoryRoot 'src')
        (Join-Path $longTermMemoryRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$englishCourseRoot = Join-Path $firmwareRoot 'components/english_course'
Invoke-HostTest `
    -Name 'english_course_core' `
    -Sources @(
        (Join-Path $englishCourseRoot 'src/english_course_core.c')
        (Join-Path $englishCourseRoot 'test/test_english_course_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $englishCourseRoot 'include')
        (Join-Path $englishCourseRoot 'src')
        (Join-Path $englishCourseRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'english_course_sync_core' `
    -Sources @(
        (Join-Path $englishCourseRoot 'src/english_course_core.c')
        (Join-Path $englishCourseRoot 'src/english_course_sync_core.c')
        (Join-Path $englishCourseRoot 'test/test_english_course_sync_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $englishCourseRoot 'include')
        (Join-Path $englishCourseRoot 'src')
        (Join-Path $englishCourseRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'english_course_runtime' `
    -Sources @(
        (Join-Path $englishCourseRoot 'src/english_course_core.c')
        (Join-Path $englishCourseRoot 'src/english_course_sync_core.c')
        (Join-Path $englishCourseRoot 'src/english_course.c')
        (Join-Path $englishCourseRoot 'test/stubs/config_store.c')
        (Join-Path $englishCourseRoot 'test/test_english_course_runtime.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $englishCourseRoot 'include')
        (Join-Path $englishCourseRoot 'src')
        (Join-Path $englishCourseRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$playbackRequestRoot = Join-Path $firmwareRoot 'components/playback_request'
Invoke-HostTest `
    -Name 'playback_request_core' `
    -Sources @(
        (Join-Path $playbackRequestRoot 'src/playback_request_core.c')
        (Join-Path $playbackRequestRoot 'test/test_playback_request_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $playbackRequestRoot 'include')
        (Join-Path $playbackRequestRoot 'src')
        (Join-Path $playbackRequestRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'playback_request_runtime' `
    -Sources @(
        (Join-Path $playbackRequestRoot 'src/playback_request_core.c')
        (Join-Path $playbackRequestRoot 'src/playback_request_sync_core.c')
        (Join-Path $playbackRequestRoot 'src/playback_request.c')
        (Join-Path $playbackRequestRoot 'test/stubs/config_store.c')
        (Join-Path $playbackRequestRoot 'test/test_playback_request_runtime.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $playbackRequestRoot 'include')
        (Join-Path $playbackRequestRoot 'src')
        (Join-Path $playbackRequestRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'playback_request_sync_core' `
    -Sources @(
        (Join-Path $playbackRequestRoot 'src/playback_request_core.c')
        (Join-Path $playbackRequestRoot 'src/playback_request_sync_core.c')
        (Join-Path $playbackRequestRoot 'test/test_playback_request_sync_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $playbackRequestRoot 'include')
        (Join-Path $playbackRequestRoot 'src')
        (Join-Path $playbackRequestRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$localCommandRoot = Join-Path $firmwareRoot 'components/local_command'
Invoke-HostTest `
    -Name 'local_command_core' `
    -Sources @(
        (Join-Path $localCommandRoot 'src/local_command_core.c')
        (Join-Path $localCommandRoot 'test/test_local_command_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $localCommandRoot 'include')
        (Join-Path $localCommandRoot 'src')
        (Join-Path $localCommandRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'local_command_runtime' `
    -Sources @(
        (Join-Path $localCommandRoot 'src/local_command_core.c')
        (Join-Path $localCommandRoot 'src/local_command_sync_core.c')
        (Join-Path $localCommandRoot 'src/local_command.c')
        (Join-Path $localCommandRoot 'test/stubs/config_store.c')
        (Join-Path $localCommandRoot 'test/test_local_command_runtime.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $localCommandRoot 'include')
        (Join-Path $localCommandRoot 'src')
        (Join-Path $localCommandRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'local_command_sync_core' `
    -Sources @(
        (Join-Path $localCommandRoot 'src/local_command_core.c')
        (Join-Path $localCommandRoot 'src/local_command_sync_core.c')
        (Join-Path $localCommandRoot 'test/test_local_command_sync_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $localCommandRoot 'include')
        (Join-Path $localCommandRoot 'src')
        (Join-Path $localCommandRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$offlineContentRoot = Join-Path $firmwareRoot 'components/offline_content'
Invoke-HostTest `
    -Name 'offline_content_core' `
    -Sources @(
        (Join-Path $offlineContentRoot 'src/offline_content_core.c')
        (Join-Path $offlineContentRoot 'test/test_offline_content_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $offlineContentRoot 'include')
        (Join-Path $offlineContentRoot 'src')
        (Join-Path $offlineContentRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'offline_content_runtime' `
    -Sources @(
        (Join-Path $offlineContentRoot 'src/offline_content_core.c')
        (Join-Path $offlineContentRoot 'src/offline_content.c')
        (Join-Path $offlineContentRoot 'test/stubs/config_store.c')
        (Join-Path $offlineContentRoot 'test/test_offline_content_runtime.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $offlineContentRoot 'include')
        (Join-Path $offlineContentRoot 'src')
        (Join-Path $offlineContentRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$offlinePolicyRoot = Join-Path $firmwareRoot 'components/offline_policy'
Invoke-HostTest `
    -Name 'offline_policy_core' `
    -Sources @(
        (Join-Path $offlinePolicyRoot 'src/offline_policy_core.c')
        (Join-Path $offlinePolicyRoot 'test/test_offline_policy_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $offlinePolicyRoot 'include')
        (Join-Path $offlinePolicyRoot 'src')
        (Join-Path $offlinePolicyRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'offline_policy_runtime' `
    -Sources @(
        (Join-Path $offlinePolicyRoot 'src/offline_policy_core.c')
        (Join-Path $offlinePolicyRoot 'src/offline_policy.c')
        (Join-Path $offlinePolicyRoot 'test/stubs/config_store.c')
        (Join-Path $offlinePolicyRoot 'test/test_offline_policy_runtime.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $offlinePolicyRoot 'include')
        (Join-Path $offlinePolicyRoot 'src')
        (Join-Path $offlinePolicyRoot 'test/stubs')
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

$temperatureMonitorRoot = Join-Path $firmwareRoot 'components/temperature_monitor'
Invoke-HostTest `
    -Name 'temperature_monitor_core' `
    -Sources @(
        (Join-Path $temperatureMonitorRoot 'src/temperature_monitor_core.c')
        (Join-Path $temperatureMonitorRoot 'test/test_temperature_monitor_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $temperatureMonitorRoot 'src')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$parentControlRoot = Join-Path $firmwareRoot 'components/parent_control_runtime'
$displayPolicyRoot = Join-Path $firmwareRoot 'components/display_policy'
Invoke-HostTest `
    -Name 'display_policy_core' `
    -Sources @(
        (Join-Path $displayPolicyRoot 'src/display_policy_core.c')
        (Join-Path $displayPolicyRoot 'test/test_display_policy_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $displayPolicyRoot 'include')
        (Join-Path $displayPolicyRoot 'src')
        (Join-Path $displayPolicyRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'display_policy_runtime' `
    -Sources @(
        (Join-Path $displayPolicyRoot 'src/display_policy_core.c')
        (Join-Path $displayPolicyRoot 'src/display_policy.c')
        (Join-Path $displayPolicyRoot 'test/stubs/config_store.c')
        (Join-Path $displayPolicyRoot 'test/test_display_policy_runtime.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $displayPolicyRoot 'include')
        (Join-Path $displayPolicyRoot 'src')
        (Join-Path $displayPolicyRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$touchInputRoot = Join-Path $firmwareRoot 'components/touch_input'
Invoke-HostTest `
    -Name 'touch_input_core' `
    -Sources @(
        (Join-Path $touchInputRoot 'src/touch_input_core.c')
        (Join-Path $touchInputRoot 'test/test_touch_input_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $touchInputRoot 'include')
        (Join-Path $touchInputRoot 'src')
        (Join-Path $touchInputRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'touch_input_runtime' `
    -Sources @(
        (Join-Path $touchInputRoot 'src/touch_input_core.c')
        (Join-Path $touchInputRoot 'src/touch_input.c')
        (Join-Path $touchInputRoot 'test/stubs/config_store.c')
        (Join-Path $touchInputRoot 'test/test_touch_input_runtime.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $touchInputRoot 'include')
        (Join-Path $touchInputRoot 'src')
        (Join-Path $touchInputRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$cellularRoot = Join-Path $firmwareRoot 'components/cellular_4g'
Invoke-HostTest `
    -Name 'cellular_core' `
    -Sources @(
        (Join-Path $cellularRoot 'src/cellular_core.c')
        (Join-Path $cellularRoot 'test/test_cellular_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $cellularRoot 'include')
        (Join-Path $cellularRoot 'src')
        (Join-Path $cellularRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'cellular_runtime' `
    -Sources @(
        (Join-Path $cellularRoot 'src/cellular_core.c')
        (Join-Path $cellularRoot 'src/cellular_4g.c')
        (Join-Path $cellularRoot 'test/stubs/config_store.c')
        (Join-Path $cellularRoot 'test/test_cellular_runtime.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $cellularRoot 'include')
        (Join-Path $cellularRoot 'src')
        (Join-Path $cellularRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$batteryRoot = Join-Path $firmwareRoot 'components/battery_power'
Invoke-HostTest `
    -Name 'battery_core' `
    -Sources @(
        (Join-Path $batteryRoot 'src/battery_core.c')
        (Join-Path $batteryRoot 'test/test_battery_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $batteryRoot 'include')
        (Join-Path $batteryRoot 'src')
        (Join-Path $batteryRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'battery_runtime' `
    -Sources @(
        (Join-Path $batteryRoot 'src/battery_core.c')
        (Join-Path $batteryRoot 'src/battery_power.c')
        (Join-Path $batteryRoot 'test/stubs/config_store.c')
        (Join-Path $batteryRoot 'test/test_battery_runtime.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $batteryRoot 'include')
        (Join-Path $batteryRoot 'src')
        (Join-Path $batteryRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$motionRoot = Join-Path $firmwareRoot 'components/motion_control'
Invoke-HostTest `
    -Name 'motion_core' `
    -Sources @(
        (Join-Path $motionRoot 'src/motion_core.c')
        (Join-Path $motionRoot 'test/test_motion_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $motionRoot 'include')
        (Join-Path $motionRoot 'src')
        (Join-Path $motionRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'motion_runtime' `
    -Sources @(
        (Join-Path $motionRoot 'src/motion_core.c')
        (Join-Path $motionRoot 'src/motion_control.c')
        (Join-Path $motionRoot 'test/stubs/config_store.c')
        (Join-Path $motionRoot 'test/test_motion_runtime.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $motionRoot 'include')
        (Join-Path $motionRoot 'src')
        (Join-Path $motionRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$formFactorRoot = Join-Path $firmwareRoot 'components/form_factor_profile'
Invoke-HostTest `
    -Name 'form_factor_core' `
    -Sources @(
        (Join-Path $formFactorRoot 'src/form_factor_core.c')
        (Join-Path $formFactorRoot 'test/test_form_factor_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $formFactorRoot 'include')
        (Join-Path $formFactorRoot 'src')
        (Join-Path $formFactorRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'form_factor_runtime' `
    -Sources @(
        (Join-Path $formFactorRoot 'src/form_factor_core.c')
        (Join-Path $formFactorRoot 'src/form_factor_profile.c')
        (Join-Path $formFactorRoot 'test/stubs/config_store.c')
        (Join-Path $formFactorRoot 'test/test_form_factor_runtime.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $formFactorRoot 'include')
        (Join-Path $formFactorRoot 'src')
        (Join-Path $formFactorRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$videoSessionRoot = Join-Path $firmwareRoot 'components/video_session'
Invoke-HostTest `
    -Name 'video_core' `
    -Sources @(
        (Join-Path $videoSessionRoot 'src/video_core.c')
        (Join-Path $videoSessionRoot 'test/test_video_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $videoSessionRoot 'include')
        (Join-Path $videoSessionRoot 'src')
        (Join-Path $videoSessionRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'video_session_runtime' `
    -Sources @(
        (Join-Path $videoSessionRoot 'src/video_core.c')
        (Join-Path $videoSessionRoot 'src/video_session.c')
        (Join-Path $videoSessionRoot 'test/stubs/config_store.c')
        (Join-Path $videoSessionRoot 'test/test_video_runtime.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $videoSessionRoot 'include')
        (Join-Path $videoSessionRoot 'src')
        (Join-Path $videoSessionRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$wearableRoot = Join-Path $firmwareRoot 'components/wearable_location'
Invoke-HostTest `
    -Name 'wearable_core' `
    -Sources @(
        (Join-Path $wearableRoot 'src/wearable_core.c')
        (Join-Path $wearableRoot 'test/test_wearable_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $wearableRoot 'include')
        (Join-Path $wearableRoot 'src')
        (Join-Path $wearableRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'wearable_location_runtime' `
    -Sources @(
        (Join-Path $wearableRoot 'src/wearable_core.c')
        (Join-Path $wearableRoot 'src/wearable_location.c')
        (Join-Path $wearableRoot 'test/stubs/config_store.c')
        (Join-Path $wearableRoot 'test/test_wearable_runtime.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $wearableRoot 'include')
        (Join-Path $wearableRoot 'src')
        (Join-Path $wearableRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$familyRoot = Join-Path $firmwareRoot 'components/family_linkage'
Invoke-HostTest `
    -Name 'family_core' `
    -Sources @(
        (Join-Path $familyRoot 'src/family_core.c')
        (Join-Path $familyRoot 'test/test_family_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $familyRoot 'include')
        (Join-Path $familyRoot 'src')
        (Join-Path $familyRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'family_linkage_runtime' `
    -Sources @(
        (Join-Path $familyRoot 'src/family_core.c')
        (Join-Path $familyRoot 'src/family_linkage.c')
        (Join-Path $familyRoot 'test/stubs/config_store.c')
        (Join-Path $familyRoot 'test/test_family_runtime.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $familyRoot 'include')
        (Join-Path $familyRoot 'src')
        (Join-Path $familyRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$screenSurfaceRoot = Join-Path $firmwareRoot 'components/screen_surface'
Invoke-HostTest `
    -Name 'screen_core' `
    -Sources @(
        (Join-Path $screenSurfaceRoot 'src/screen_core.c')
        (Join-Path $screenSurfaceRoot 'test/test_screen_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $screenSurfaceRoot 'include')
        (Join-Path $screenSurfaceRoot 'src')
        (Join-Path $screenSurfaceRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'screen_surface_runtime' `
    -Sources @(
        (Join-Path $screenSurfaceRoot 'src/screen_core.c')
        (Join-Path $screenSurfaceRoot 'src/screen_surface.c')
        (Join-Path $screenSurfaceRoot 'test/stubs/config_store.c')
        (Join-Path $screenSurfaceRoot 'test/test_screen_runtime.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $screenSurfaceRoot 'include')
        (Join-Path $screenSurfaceRoot 'src')
        (Join-Path $screenSurfaceRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'screen_surface_moderation' `
    -Sources @(
        (Join-Path $screenSurfaceRoot 'src/screen_core.c')
        (Join-Path $screenSurfaceRoot 'src/screen_surface.c')
        (Join-Path $screenSurfaceRoot 'test/stubs/config_store.c')
        (Join-Path $contentFilterRoot 'src/content_filter_core.c')
        (Join-Path $contentFilterRoot 'src/content_filter.c')
        (Join-Path $screenSurfaceRoot 'test/test_screen_moderation.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $screenSurfaceRoot 'include')
        (Join-Path $screenSurfaceRoot 'src')
        (Join-Path $screenSurfaceRoot 'test/stubs')
        (Join-Path $contentFilterRoot 'include')
        (Join-Path $contentFilterRoot 'src')
        (Join-Path $contentFilterRoot 'test/stubs')
    ) `
    -AdditionalArguments @('-DCONFIG_FEATURE_CONTENT_FILTER=1') `
    -Compiler $compiler

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
