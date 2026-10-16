[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$firmwareRoot = Split-Path -Parent $PSScriptRoot

function Resolve-HostCompiler {
    param(
        [ValidateSet('c', 'c++')]
        [string]$Language = 'c++'
    )

    $candidates = if ($Language -eq 'c') {
        @('gcc', 'clang')
    } else {
        @('g++', 'clang++')
    }
    foreach ($candidate in $candidates) {
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

        [ValidateSet('c', 'c++')]
        [string]$Language = 'c++',

        [Parameter(Mandatory = $true)]
        [string]$Compiler
    )

    $languageStandard = if ($Language -eq 'c') { '-std=c11' } else { '-std=c++17' }
    $outputDirectory = Join-Path ([System.IO.Path]::GetTempPath()) (
        'sprout-host-' + [System.Guid]::NewGuid().ToString('N').Substring(0, 8)
    )
    New-Item -ItemType Directory -Path $outputDirectory | Out-Null
    $executable = Join-Path $outputDirectory ($Name + '.exe')

    try {
        $arguments = @(
            $languageStandard
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

$compiler = Resolve-HostCompiler -Language 'c++'
$cCompiler = Resolve-HostCompiler -Language 'c'

$contentFilterRoot = Join-Path $firmwareRoot 'components/content_filter'

$errorCodeRoot = Join-Path $firmwareRoot 'components/error_code'
Invoke-HostTest `
    -Name 'error_code_contract' `
    -Sources @(
        (Join-Path $errorCodeRoot 'src/error_code.c')
        (Join-Path $errorCodeRoot 'test/test_error_code.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $errorCodeRoot 'include')
        (Join-Path $errorCodeRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$versionInfoRoot = Join-Path $firmwareRoot 'components/version_info'
Invoke-HostTest `
    -Name 'version_info_contract' `
    -Sources @(
        (Join-Path $versionInfoRoot 'src/version_info.c')
        (Join-Path $versionInfoRoot 'test/stubs/esp_app_desc.c')
        (Join-Path $versionInfoRoot 'test/test_version_info.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $versionInfoRoot 'include')
        (Join-Path $versionInfoRoot 'test/stubs')
    ) `
    -AdditionalArguments @(
        '-DCONFIG_IDF_TARGET=\"esp32s3\"'
        '-DCONFIG_FIRMWARE_SCHEMA_VERSION=\"1.0.0\"'
        '-DCONFIG_FIRMWARE_PROTOCOL_VERSION=\"1.0.0\"'
    ) `
    -Compiler $compiler

$deviceCapabilitiesRoot = Join-Path $firmwareRoot 'components/device_capabilities'
Invoke-HostTest `
    -Name 'device_capabilities_contract' `
    -Sources @(
        (Join-Path $deviceCapabilitiesRoot 'src/device_capabilities.c')
        (Join-Path $deviceCapabilitiesRoot 'test/test_device_capabilities.c')
    ) `
    -IncludeDirectories @(
        (Join-Path $deviceCapabilitiesRoot 'include')
        (Join-Path $deviceCapabilitiesRoot 'test/stubs')
    ) `
    -AdditionalArguments @(
        '-DCONFIG_CAPABILITY_AUDIO_INPUT=1'
        '-DCONFIG_CAPABILITY_AUDIO_OUTPUT=1'
        '-DCONFIG_CAPABILITY_WIFI=1'
    ) `
    -Language 'c' `
    -Compiler $cCompiler

$provisioningPayloadRoot = Join-Path $firmwareRoot 'components/provisioning_payload'
Invoke-HostTest `
    -Name 'provisioning_payload_qr' `
    -Sources @(
        (Join-Path $provisioningPayloadRoot 'src/provisioning_payload.c')
        (Join-Path $provisioningPayloadRoot 'test/test_provisioning_payload.c')
    ) `
    -IncludeDirectories @(
        (Join-Path $provisioningPayloadRoot 'include')
        (Join-Path $provisioningPayloadRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Language 'c' `
    -Compiler $cCompiler

$moduleRegistryRoot = Join-Path $firmwareRoot 'components/module_registry'
Invoke-HostTest `
    -Name 'module_registry_lifecycle' `
    -Sources @(
        (Join-Path $moduleRegistryRoot 'src/module_registry.c')
        (Join-Path $moduleRegistryRoot 'test/test_module_registry_lifecycle.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $moduleRegistryRoot 'include')
        (Join-Path $moduleRegistryRoot 'test/stubs')
    ) `
    -AdditionalArguments @(
        '-DCONFIG_MODULE_REGISTRY_CAPACITY=16'
    ) `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'module_registry_capacity' `
    -Sources @(
        (Join-Path $moduleRegistryRoot 'src/module_registry.c')
        (Join-Path $moduleRegistryRoot 'test/test_module_registry_capacity.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $moduleRegistryRoot 'include')
        (Join-Path $moduleRegistryRoot 'test/stubs')
    ) `
    -AdditionalArguments @(
        '-DCONFIG_MODULE_REGISTRY_CAPACITY=2'
    ) `
    -Compiler $compiler

$errorRecoveryRoot = Join-Path $firmwareRoot 'components/error_recovery'
Invoke-HostTest `
    -Name 'error_recovery_transitions' `
    -Sources @(
        (Join-Path $errorRecoveryRoot 'src/error_recovery.c')
        (Join-Path $errorRecoveryRoot 'test/stubs/error_recovery_host_stubs.c')
        (Join-Path $errorRecoveryRoot 'test/test_error_recovery.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $errorRecoveryRoot 'include')
        (Join-Path $errorRecoveryRoot 'test/stubs')
    ) `
    -AdditionalArguments @(
        '-DCONFIG_ERROR_RECOVERY_RESTART_DELAY_MS=2000'
    ) `
    -Compiler $compiler

$cloudAuthRoot = Join-Path $firmwareRoot 'components/cloud_auth'
Invoke-HostTest `
    -Name 'cloud_auth_state_machine' `
    -Sources @(
        (Join-Path $cloudAuthRoot 'src/cloud_auth.c')
        (Join-Path $cloudAuthRoot 'test/stubs/cloud_auth_deps.c')
        (Join-Path $cloudAuthRoot 'test/test_cloud_auth.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $cloudAuthRoot 'include')
        (Join-Path $cloudAuthRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$wakeFeedbackRoot = Join-Path $firmwareRoot 'components/wake_feedback'
Invoke-HostTest `
    -Name 'wake_feedback_fanout' `
    -Sources @(
        (Join-Path $wakeFeedbackRoot 'src/wake_feedback.c')
        (Join-Path $wakeFeedbackRoot 'test/stubs/wake_feedback_deps.c')
        (Join-Path $wakeFeedbackRoot 'test/test_wake_feedback.c')
    ) `
    -IncludeDirectories @(
        (Join-Path $wakeFeedbackRoot 'include')
        (Join-Path $wakeFeedbackRoot 'test/stubs')
    ) `
    -AdditionalArguments @(
        '-DCONFIG_FEATURE_VOICE_WAKE=1'
        '-DCONFIG_FEATURE_PROMPT_TONE=1'
        '-DCONFIG_FEATURE_LED_INDICATOR=1'
        '-DCONFIG_FEATURE_DIAGNOSTIC_REPORTER=1'
    ) `
    -Language 'c' `
    -Compiler $cCompiler

$childPromptProfileRoot = Join-Path $firmwareRoot 'components/child_prompt_profile'
Invoke-HostTest `
    -Name 'child_prompt_profile_state' `
    -Sources @(
        (Join-Path $childPromptProfileRoot 'src/child_prompt_profile.c')
        (Join-Path $childPromptProfileRoot 'test/stubs/child_prompt_profile_deps.c')
        (Join-Path $childPromptProfileRoot 'test/test_child_prompt_profile.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $childPromptProfileRoot 'include')
        (Join-Path $childPromptProfileRoot 'test/stubs')
    ) `
    -AdditionalArguments @(
        '-DCONFIG_FEATURE_PARENT_POLICY=1'
    ) `
    -Compiler $compiler

$systemCoreRoot = Join-Path $firmwareRoot 'components/system_core'
Invoke-HostTest `
    -Name 'system_core_event_loop' `
    -Sources @(
        (Join-Path $systemCoreRoot 'src/system_core.c')
        (Join-Path $systemCoreRoot 'test/stubs/system_core_deps.c')
        (Join-Path $systemCoreRoot 'test/test_system_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $systemCoreRoot 'include')
        (Join-Path $systemCoreRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'system_core_creation_failure' `
    -Sources @(
        (Join-Path $systemCoreRoot 'src/system_core.c')
        (Join-Path $systemCoreRoot 'test/stubs/system_core_deps.c')
        (Join-Path $systemCoreRoot 'test/test_system_core_failure.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $systemCoreRoot 'include')
        (Join-Path $systemCoreRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$offlineFallbackRoot = Join-Path $firmwareRoot 'components/offline_fallback'
Invoke-HostTest `
    -Name 'offline_fallback_state' `
    -Sources @(
        (Join-Path $offlineFallbackRoot 'src/offline_fallback.c')
        (Join-Path $offlineFallbackRoot 'test/stubs/offline_fallback_deps.c')
        (Join-Path $offlineFallbackRoot 'test/test_offline_fallback.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $offlineFallbackRoot 'include')
        (Join-Path $offlineFallbackRoot 'test/stubs')
    ) `
    -AdditionalArguments @(
        '-DCONFIG_FEATURE_TIME_SYNC=1'
        '-DCONFIG_OFFLINE_FALLBACK_GRACE_SECONDS=15'
    ) `
    -Compiler $compiler

$networkQualityRoot = Join-Path $firmwareRoot 'components/network_quality'
Invoke-HostTest `
    -Name 'network_quality_classification' `
    -Sources @(
        (Join-Path $networkQualityRoot 'src/network_quality.c')
        (Join-Path $networkQualityRoot 'test/stubs/network_quality_deps.c')
        (Join-Path $networkQualityRoot 'test/test_network_quality.c')
    ) `
    -IncludeDirectories @(
        (Join-Path $networkQualityRoot 'include')
        (Join-Path $networkQualityRoot 'test/stubs')
    ) `
    -AdditionalArguments @(
        '-DCONFIG_NETWORK_QUALITY_POOR_RSSI=-80'
        '-DCONFIG_NETWORK_QUALITY_GOOD_RSSI=-65'
        '-DCONFIG_NETWORK_QUALITY_EXCELLENT_RSSI=-50'
    ) `
    -Language 'c' `
    -Compiler $cCompiler

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

$deviceMqttRoot = Join-Path $firmwareRoot 'components/device_mqtt'
Invoke-HostTest `
    -Name 'device_mqtt_core' `
    -Sources @(
        (Join-Path $deviceMqttRoot 'src/device_mqtt_core.c')
        (Join-Path $deviceMqttRoot 'test/test_device_mqtt_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $deviceMqttRoot 'include')
        (Join-Path $deviceMqttRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'device_mqtt_transport_ownership' `
    -Sources @(
        (Join-Path $deviceMqttRoot 'src/device_mqtt_core.c')
        (Join-Path $deviceMqttRoot 'src/device_mqtt_transport.c')
        (Join-Path $deviceMqttRoot 'test/test_device_mqtt_transport_ownership.c')
    ) `
    -IncludeDirectories @(
        (Join-Path $deviceMqttRoot 'include')
        (Join-Path $deviceMqttRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Language 'c' `
    -Compiler $cCompiler

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

$cjsonRoot = Join-Path $firmwareRoot 'managed_components/espressif__cjson'
if (-not (Test-Path -LiteralPath (Join-Path $cjsonRoot 'cJSON/cJSON.c'))) {
    throw (
        'The voice_session_token host test requires ' +
        "'pio run -e esp32-s3-n16r8 -t compiledb' to resolve ESP-IDF " +
        'components before it runs.'
    )
}
$voiceSessionTokenInclude = @(
    (Join-Path $voiceSessionRoot 'include')
    (Join-Path $cjsonRoot 'cJSON')
)
Invoke-HostTest `
    -Name 'voice_session_token' `
    -Sources @(
        (Join-Path $voiceSessionRoot 'src/voice_session_token.c')
        (Join-Path $voiceSessionRoot 'test/test_voice_session_token.cpp')
        (Join-Path $cjsonRoot 'cJSON/cJSON.c')
    ) `
    -IncludeDirectories $voiceSessionTokenInclude `
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

$visionSyncRoot = Join-Path $firmwareRoot 'components/vision_sync'
Invoke-HostTest `
    -Name 'vision_sync_core' `
    -Sources @(
        (Join-Path $visionSyncRoot 'src/vision_sync_core.c')
        (Join-Path $visionSyncRoot 'test/test_vision_sync_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $visionSyncRoot 'include')
        (Join-Path $visionSyncRoot 'src')
        (Join-Path $visionSyncRoot 'test/stubs')
        (Join-Path $cameraVisionRoot 'include')
        (Join-Path $firmwareRoot 'components/camera_capture/include')
        (Join-Path $firmwareRoot 'components/module_registry/include')
        (Join-Path $cameraVisionRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
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

$uiTextSyncRoot = Join-Path $firmwareRoot 'components/ui_text_sync'
Invoke-HostTest `
    -Name 'ui_text_sync_core' `
    -Sources @(
        (Join-Path $uiTextSyncRoot 'src/ui_text_sync_core.c')
        (Join-Path $uiTextSyncRoot 'test/test_ui_text_sync_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $uiTextRoot 'include')
        (Join-Path $uiTextRoot 'test/stubs')
        (Join-Path $uiTextSyncRoot 'src')
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
        (Join-Path $bluetoothAudioRoot 'src/bluetooth_audio_sync_core.c')
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

Invoke-HostTest `
    -Name 'bluetooth_audio_sync_core' `
    -Sources @(
        (Join-Path $bluetoothAudioRoot 'src/bluetooth_core.c')
        (Join-Path $bluetoothAudioRoot 'src/bluetooth_audio_sync_core.c')
        (Join-Path $bluetoothAudioRoot 'test/test_bluetooth_audio_sync_core.cpp')
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
        (Join-Path $learningVisualizationRoot 'src/learning_visualization_sync_core.c')
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

Invoke-HostTest `
    -Name 'learning_visualization_sync_core' `
    -Sources @(
        (Join-Path $learningVisualizationRoot 'src/learning_core.c')
        (Join-Path $learningVisualizationRoot 'src/learning_visualization_sync_core.c')
        (Join-Path $learningVisualizationRoot 'test/test_learning_visualization_sync_core.cpp')
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
        (Join-Path $imagePrivacyRoot 'src/image_privacy_sync_core.c')
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

Invoke-HostTest `
    -Name 'image_privacy_sync_core' `
    -Sources @(
        (Join-Path $imagePrivacyRoot 'src/image_core.c')
        (Join-Path $imagePrivacyRoot 'src/image_privacy_sync_core.c')
        (Join-Path $imagePrivacyRoot 'test/test_image_privacy_sync_core.cpp')
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

$displayPolicySyncRoot = Join-Path $firmwareRoot 'components/display_policy_sync'
Invoke-HostTest `
    -Name 'display_policy_sync_core' `
    -Sources @(
        (Join-Path $displayPolicyRoot 'src/display_policy_core.c')
        (Join-Path $displayPolicySyncRoot 'src/display_policy_sync_core.c')
        (Join-Path $displayPolicySyncRoot 'test/test_display_policy_sync_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $displayPolicyRoot 'include')
        (Join-Path $displayPolicyRoot 'src')
        (Join-Path $displayPolicyRoot 'test/stubs')
        (Join-Path $displayPolicySyncRoot 'src')
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

$touchReportRoot = Join-Path $firmwareRoot 'components/touch_report'
Invoke-HostTest `
    -Name 'touch_report_core' `
    -Sources @(
        (Join-Path $touchInputRoot 'src/touch_input_core.c')
        (Join-Path $touchReportRoot 'src/touch_report_core.c')
        (Join-Path $touchReportRoot 'test/test_touch_report_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $touchInputRoot 'include')
        (Join-Path $touchInputRoot 'src')
        (Join-Path $touchInputRoot 'test/stubs')
        (Join-Path $touchReportRoot 'src')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$displayReportRoot = Join-Path $firmwareRoot 'components/display_report'
Invoke-HostTest `
    -Name 'display_report_core' `
    -Sources @(
        (Join-Path $displayPolicyRoot 'src/display_policy_core.c')
        (Join-Path $displayReportRoot 'src/display_report_core.c')
        (Join-Path $displayReportRoot 'test/test_display_report_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $displayPolicyRoot 'include')
        (Join-Path $displayPolicyRoot 'src')
        (Join-Path $displayPolicyRoot 'test/stubs')
        (Join-Path $displayReportRoot 'src')
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

$touchInputSyncRoot = Join-Path $firmwareRoot 'components/touch_input_sync'
Invoke-HostTest `
    -Name 'touch_input_sync_core' `
    -Sources @(
        (Join-Path $touchInputRoot 'src/touch_input_core.c')
        (Join-Path $touchInputSyncRoot 'src/touch_input_sync_core.c')
        (Join-Path $touchInputSyncRoot 'test/test_touch_input_sync_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $touchInputRoot 'include')
        (Join-Path $touchInputRoot 'src')
        (Join-Path $touchInputRoot 'test/stubs')
        (Join-Path $touchInputSyncRoot 'src')
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

$cellularSyncRoot = Join-Path $firmwareRoot 'components/cellular_sync'
Invoke-HostTest `
    -Name 'cellular_sync_core' `
    -Sources @(
        (Join-Path $cellularRoot 'src/cellular_core.c')
        (Join-Path $cellularSyncRoot 'src/cellular_sync_core.c')
        (Join-Path $cellularSyncRoot 'test/test_cellular_sync_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $cellularSyncRoot 'include')
        (Join-Path $cellularSyncRoot 'src')
        (Join-Path $cellularRoot 'include')
        (Join-Path $cellularRoot 'src')
        (Join-Path $cellularRoot 'test/stubs')
        (Join-Path $firmwareRoot 'components/module_registry/include')
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

$motionPolicySyncRoot = Join-Path $firmwareRoot 'components/motion_policy_sync'
Invoke-HostTest `
    -Name 'motion_policy_sync_core' `
    -Sources @(
        (Join-Path $motionRoot 'src/motion_core.c')
        (Join-Path $motionPolicySyncRoot 'src/motion_policy_sync_core.c')
        (Join-Path $motionPolicySyncRoot 'test/test_motion_policy_sync_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $motionRoot 'include')
        (Join-Path $motionRoot 'src')
        (Join-Path $motionRoot 'test/stubs')
        (Join-Path $motionPolicySyncRoot 'src')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$motionReportRoot = Join-Path $firmwareRoot 'components/motion_report'
Invoke-HostTest `
    -Name 'motion_report_core' `
    -Sources @(
        (Join-Path $motionRoot 'src/motion_core.c')
        (Join-Path $motionReportRoot 'src/motion_report_core.c')
        (Join-Path $motionReportRoot 'test/test_motion_report_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $motionRoot 'include')
        (Join-Path $motionRoot 'src')
        (Join-Path $motionRoot 'test/stubs')
        (Join-Path $motionReportRoot 'src')
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

$formFactorReportRoot = Join-Path $firmwareRoot 'components/form_factor_report'
Invoke-HostTest `
    -Name 'form_factor_report_core' `
    -Sources @(
        (Join-Path $formFactorRoot 'src/form_factor_core.c')
        (Join-Path $formFactorReportRoot 'src/form_factor_report_core.c')
        (Join-Path $formFactorReportRoot 'test/stubs/device_capabilities.c')
        (Join-Path $formFactorReportRoot 'test/test_form_factor_report_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $formFactorRoot 'include')
        (Join-Path $formFactorRoot 'src')
        (Join-Path $formFactorRoot 'test/stubs')
        (Join-Path $formFactorReportRoot 'src')
        (Join-Path $formFactorReportRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$connectivityReportRoot = Join-Path $firmwareRoot 'components/connectivity_report'
Invoke-HostTest `
    -Name 'connectivity_report_core' `
    -Sources @(
        (Join-Path $connectivityReportRoot 'src/connectivity_report_core.c')
        (Join-Path $connectivityReportRoot 'test/test_connectivity_report_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $connectivityReportRoot 'src')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$connectivityPolicySyncRoot = Join-Path $firmwareRoot 'components/connectivity_policy_sync'
Invoke-HostTest `
    -Name 'connectivity_policy_sync_core' `
    -Sources @(
        (Join-Path $connectivityPolicySyncRoot 'src/connectivity_policy_sync_core.c')
        (Join-Path $connectivityPolicySyncRoot 'test/test_connectivity_policy_sync_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $connectivityPolicySyncRoot 'src')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$powerReportRoot = Join-Path $firmwareRoot 'components/power_report'
$batteryPowerRoot = Join-Path $firmwareRoot 'components/battery_power'
Invoke-HostTest `
    -Name 'power_report_core' `
    -Sources @(
        (Join-Path $batteryPowerRoot 'src/battery_core.c')
        (Join-Path $powerReportRoot 'src/power_report_core.c')
        (Join-Path $powerReportRoot 'test/test_power_report_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $batteryPowerRoot 'include')
        (Join-Path $batteryPowerRoot 'src')
        (Join-Path $batteryPowerRoot 'test/stubs')
        (Join-Path $powerReportRoot 'src')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$powerPolicySyncRoot = Join-Path $firmwareRoot 'components/power_policy_sync'
Invoke-HostTest `
    -Name 'power_policy_sync_core' `
    -Sources @(
        (Join-Path $batteryPowerRoot 'src/battery_core.c')
        (Join-Path $powerPolicySyncRoot 'src/power_policy_sync_core.c')
        (Join-Path $powerPolicySyncRoot 'test/test_power_policy_sync_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $batteryPowerRoot 'include')
        (Join-Path $batteryPowerRoot 'src')
        (Join-Path $batteryPowerRoot 'test/stubs')
        (Join-Path $powerPolicySyncRoot 'src')
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
        (Join-Path $videoSessionRoot 'src/video_session_sync_core.c')
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

Invoke-HostTest `
    -Name 'video_session_sync_core' `
    -Sources @(
        (Join-Path $videoSessionRoot 'src/video_core.c')
        (Join-Path $videoSessionRoot 'src/video_session_sync_core.c')
        (Join-Path $videoSessionRoot 'test/test_video_session_sync_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $videoSessionRoot 'include')
        (Join-Path $videoSessionRoot 'src')
        (Join-Path $videoSessionRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$videoCompanionReportRoot = Join-Path $firmwareRoot 'components/video_companion_report'
Invoke-HostTest `
    -Name 'video_companion_report_core' `
    -Sources @(
        (Join-Path $videoSessionRoot 'src/video_core.c')
        (Join-Path $videoCompanionReportRoot 'src/video_companion_report_core.c')
        (Join-Path $videoCompanionReportRoot 'test/test_video_companion_report_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $videoSessionRoot 'include')
        (Join-Path $videoSessionRoot 'src')
        (Join-Path $videoSessionRoot 'test/stubs')
        (Join-Path $videoCompanionReportRoot 'src')
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

$wearableSyncRoot = Join-Path $firmwareRoot 'components/wearable_sync'
Invoke-HostTest `
    -Name 'wearable_sync_core' `
    -Sources @(
        (Join-Path $wearableRoot 'src/wearable_core.c')
        (Join-Path $wearableSyncRoot 'src/wearable_sync_core.c')
        (Join-Path $wearableSyncRoot 'test/test_wearable_sync_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $wearableSyncRoot 'include')
        (Join-Path $wearableSyncRoot 'src')
        (Join-Path $wearableRoot 'include')
        (Join-Path $wearableRoot 'src')
        (Join-Path $wearableRoot 'test/stubs')
        (Join-Path $firmwareRoot 'components/module_registry/include')
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

$screenReportRoot = Join-Path $firmwareRoot 'components/screen_report'
Invoke-HostTest `
    -Name 'screen_report_core' `
    -Sources @(
        (Join-Path $screenSurfaceRoot 'src/screen_core.c')
        (Join-Path $screenReportRoot 'src/screen_report_core.c')
        (Join-Path $screenReportRoot 'test/test_screen_report_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $screenSurfaceRoot 'include')
        (Join-Path $screenSurfaceRoot 'src')
        (Join-Path $screenSurfaceRoot 'test/stubs')
        (Join-Path $screenReportRoot 'src')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$screenPolicySyncRoot = Join-Path $firmwareRoot 'components/screen_policy_sync'
Invoke-HostTest `
    -Name 'screen_policy_sync_core' `
    -Sources @(
        (Join-Path $screenSurfaceRoot 'src/screen_core.c')
        (Join-Path $screenPolicySyncRoot 'src/screen_policy_sync_core.c')
        (Join-Path $screenPolicySyncRoot 'test/test_screen_policy_sync_core.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $screenSurfaceRoot 'include')
        (Join-Path $screenSurfaceRoot 'src')
        (Join-Path $screenSurfaceRoot 'test/stubs')
        (Join-Path $screenPolicySyncRoot 'src')
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

$volumeControlRoot = Join-Path $firmwareRoot 'components/volume_control'
$networkManagerRoot = Join-Path $firmwareRoot 'components/network_manager'
Invoke-HostTest `
    -Name 'network_manager_station' `
    -Sources @(
        (Join-Path $networkManagerRoot 'src/network_manager.c')
        (Join-Path $networkManagerRoot 'test/stubs/network_manager_deps.c')
        (Join-Path $networkManagerRoot 'test/test_network_manager.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $networkManagerRoot 'include')
        (Join-Path $networkManagerRoot 'test/stubs')
    ) `
    -AdditionalArguments @() `
    -Compiler $compiler

$factoryResetRoot = Join-Path $firmwareRoot 'components/factory_reset'
Invoke-HostTest `
    -Name 'factory_reset_guard' `
    -Sources @(
        (Join-Path $factoryResetRoot 'src/factory_reset.c')
        (Join-Path $factoryResetRoot 'test/stubs/factory_reset_deps.c')
        (Join-Path $factoryResetRoot 'test/test_factory_reset.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $factoryResetRoot 'include')
        (Join-Path $factoryResetRoot 'test/stubs')
    ) `
    -AdditionalArguments @(
        '-DCONFIG_FEATURE_CONFIG_STORE=1'
        '-DCONFIG_FACTORY_RESET_PENDING_TIMEOUT_SECONDS=10'
        '-DCONFIG_FACTORY_RESET_NVS_PARTITION=\"nvs\"'
    ) `
    -Compiler $compiler

$ledIndicatorRoot = Join-Path $firmwareRoot 'components/led_indicator'
Invoke-HostTest `
    -Name 'led_indicator_state' `
    -Sources @(
        (Join-Path $ledIndicatorRoot 'src/led_indicator.c')
        (Join-Path $ledIndicatorRoot 'test/stubs/led_indicator_deps.c')
        (Join-Path $ledIndicatorRoot 'test/test_led_indicator.c')
    ) `
    -IncludeDirectories @(
        (Join-Path $ledIndicatorRoot 'include')
        (Join-Path $ledIndicatorRoot 'test/stubs')
    ) `
    -AdditionalArguments @(
        '-DCONFIG_FEATURE_LED_INDICATOR=1'
        '-DCONFIG_LED_INDICATOR_GPIO=38'
        '-DCONFIG_LED_INDICATOR_ACTIVE_LOW=0'
        '-DCONFIG_LED_INDICATOR_PWM_FREQUENCY_HZ=1000'
        '-DCONFIG_LED_INDICATOR_BRIGHTNESS_PERCENT=50'
        '-DCONFIG_LED_INDICATOR_TASK_STACK_SIZE=3072'
        '-DCONFIG_LED_INDICATOR_TASK_PRIORITY=2'
        '-DCONFIG_LED_INDICATOR_POLL_INTERVAL_MS=20'
    ) `
    -Language 'c' `
    -Compiler $cCompiler

$promptToneRoot = Join-Path $firmwareRoot 'components/prompt_tone'
Invoke-HostTest `
    -Name 'prompt_tone_cues' `
    -Sources @(
        (Join-Path $promptToneRoot 'src/prompt_tone.c')
        (Join-Path $promptToneRoot 'test/stubs/prompt_tone_deps.c')
        (Join-Path $promptToneRoot 'test/test_prompt_tone.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $promptToneRoot 'include')
        (Join-Path $promptToneRoot 'test/stubs')
        (Join-Path $firmwareRoot 'components/audio_codec/include')
    ) `
    -AdditionalArguments @(
        '-DCONFIG_FEATURE_PLAYBACK_QUEUE=0'
        '-DCONFIG_FEATURE_AUDIO_OUTPUT=0'
    ) `
    -Compiler $compiler

$audioCodecRoot = Join-Path $firmwareRoot 'components/audio_codec'
Invoke-HostTest `
    -Name 'audio_codec_profile' `
    -Sources @(
        (Join-Path $audioCodecRoot 'src/audio_codec.c')
        (Join-Path $audioCodecRoot 'test/stubs/audio_codec_deps.c')
        (Join-Path $audioCodecRoot 'test/test_audio_codec_profile.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $audioCodecRoot 'include')
        (Join-Path $audioCodecRoot 'test/stubs')
    ) `
    -AdditionalArguments @(
        '-DCONFIG_AUDIO_CODEC_BITRATE=24000'
        '-DCONFIG_AUDIO_CODEC_COMPLEXITY=3'
        '-DCONFIG_AUDIO_CODEC_ENABLE_DTX=0'
    ) `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'audio_codec_runtime' `
    -Sources @(
        (Join-Path $audioCodecRoot 'src/audio_codec.c')
        (Join-Path $audioCodecRoot 'test/stubs/audio_codec_deps.c')
        (Join-Path $audioCodecRoot 'test/test_audio_codec_runtime.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $audioCodecRoot 'include')
        (Join-Path $audioCodecRoot 'test/stubs')
    ) `
    -AdditionalArguments @(
        '-DCONFIG_AUDIO_CODEC_BITRATE=24000'
        '-DCONFIG_AUDIO_CODEC_COMPLEXITY=3'
        '-DCONFIG_AUDIO_CODEC_ENABLE_DTX=1'
    ) `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'volume_control_runtime' `
    -Sources @(
        (Join-Path $volumeControlRoot 'src/volume_control.c')
        (Join-Path $volumeControlRoot 'test/stubs/config_store.c')
        (Join-Path $volumeControlRoot 'test/test_volume_control_runtime.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $volumeControlRoot 'include')
        (Join-Path $volumeControlRoot 'test/stubs')
    ) `
    -AdditionalArguments @(
        '-DCONFIG_FEATURE_CONFIG_STORE=1'
        '-DCONFIG_VOLUME_CONTROL_DEFAULT_PERCENT=40'
        '-DCONFIG_VOLUME_CONTROL_MAX_PERCENT=70'
    ) `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'volume_control_persistence' `
    -Sources @(
        (Join-Path $volumeControlRoot 'src/volume_control.c')
        (Join-Path $volumeControlRoot 'test/stubs/config_store.c')
        (Join-Path $volumeControlRoot 'test/test_volume_control_persistence.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $volumeControlRoot 'include')
        (Join-Path $volumeControlRoot 'test/stubs')
    ) `
    -AdditionalArguments @(
        '-DCONFIG_FEATURE_CONFIG_STORE=1'
        '-DCONFIG_VOLUME_CONTROL_DEFAULT_PERCENT=40'
        '-DCONFIG_VOLUME_CONTROL_MAX_PERCENT=70'
    ) `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'volume_control_clamp' `
    -Sources @(
        (Join-Path $volumeControlRoot 'src/volume_control.c')
        (Join-Path $volumeControlRoot 'test/stubs/config_store.c')
        (Join-Path $volumeControlRoot 'test/test_volume_control_clamp.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $volumeControlRoot 'include')
        (Join-Path $volumeControlRoot 'test/stubs')
    ) `
    -AdditionalArguments @(
        '-DCONFIG_FEATURE_CONFIG_STORE=1'
        '-DCONFIG_VOLUME_CONTROL_DEFAULT_PERCENT=40'
        '-DCONFIG_VOLUME_CONTROL_MAX_PERCENT=70'
    ) `
    -Compiler $compiler

Invoke-HostTest `
    -Name 'volume_control_fallback' `
    -Sources @(
        (Join-Path $volumeControlRoot 'src/volume_control.c')
        (Join-Path $volumeControlRoot 'test/stubs/config_store.c')
        (Join-Path $volumeControlRoot 'test/test_volume_control_fallback.cpp')
    ) `
    -IncludeDirectories @(
        (Join-Path $volumeControlRoot 'include')
        (Join-Path $volumeControlRoot 'test/stubs')
    ) `
    -AdditionalArguments @(
        '-DCONFIG_FEATURE_CONFIG_STORE=1'
        '-DCONFIG_VOLUME_CONTROL_DEFAULT_PERCENT=40'
        '-DCONFIG_VOLUME_CONTROL_MAX_PERCENT=70'
    ) `
    -Compiler $compiler

Write-Host 'Host verification passed.'
