[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$firmwareRoot = Split-Path -Parent $PSScriptRoot

function Resolve-PlatformIoCommand {
    $platformIo = Get-Command platformio -ErrorAction SilentlyContinue
    if ($null -ne $platformIo) {
        return $platformIo.Source
    }

    if (-not [string]::IsNullOrWhiteSpace($env:PLATFORMIO_CORE_DIR)) {
        $configuredEnvironment = Join-Path `
            $env:PLATFORMIO_CORE_DIR `
            'penv/Scripts/pio.exe'
        if (Test-Path -LiteralPath $configuredEnvironment) {
            return $configuredEnvironment
        }
    }

    $pythonEnvironment = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\platformio.exe'
    if (Test-Path -LiteralPath $pythonEnvironment) {
        return $pythonEnvironment
    }

    throw 'PlatformIO CLI was not found.'
}

function Invoke-PlatformIoBuild {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectPath,

        [Parameter(Mandatory = $true)]
        [string[]]$Environments,

        [Parameter(Mandatory = $true)]
        [string]$PlatformIoCommand
    )

    foreach ($environment in $Environments) {
        Write-Host "Building $environment in $ProjectPath"
        # CI compilers are run serially because parallel Xtensa GCC builds have
        # intermittently crashed inside ESP-IDF LCD translation units.
        & $PlatformIoCommand run --project-dir $ProjectPath -e $environment -j 1
        if ($LASTEXITCODE -ne 0) {
            throw "PlatformIO build failed for $environment in $ProjectPath."
        }
    }
}

function Remove-OptionalModule {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectPath,

        [Parameter(Mandatory = $true)]
        [string]$ModuleName
    )

    $moduleMacro = $ModuleName.ToUpperInvariant()
    $componentPath = Join-Path $ProjectPath "components/$ModuleName"
    if (Test-Path -LiteralPath $componentPath) {
        Remove-Item -LiteralPath $componentPath -Recurse -Force
    }

    $compositionPath = Join-Path $ProjectPath 'src/main.c'
    $composition = Get-Content -LiteralPath $compositionPath -Raw
    $includePattern = '(?ms)^#if CONFIG_FEATURE_{0}\r?\n#include "{1}\.h"\r?\n#endif\r?\n' -f
        $moduleMacro,
        $ModuleName
    $registrationPattern =
        '(?ms)^#if CONFIG_FEATURE_{0}[ \t]*\r?\n(?:(?!#endif).)*?module_registry_add\(\s*{1}_module_descriptor\(\)\s*\)\s*\)\s*;[ \t]*\r?\n#endif\r?\n' -f
        $moduleMacro,
        $ModuleName
    $composition = $composition -replace $includePattern, ''
    $composition = $composition -replace $registrationPattern, ''
    [System.IO.File]::WriteAllText(
        $compositionPath,
        $composition,
        [System.Text.UTF8Encoding]::new($false)
    )

    $applicationCMakePath = Join-Path $ProjectPath 'src/CMakeLists.txt'
    $applicationCMake = Get-Content -LiteralPath $applicationCMakePath -Raw
    $dependencyPattern =
        '(?ms)\r?\nif\(CONFIG_FEATURE_{0}\)\r?\n\s*list\(APPEND app_requires "{1}"\)\r?\nendif\(\)\r?\n' -f
        $moduleMacro,
        $ModuleName
    $applicationCMake = $applicationCMake -replace `
        $dependencyPattern, `
        [Environment]::NewLine
    [System.IO.File]::WriteAllText(
        $applicationCMakePath,
        $applicationCMake,
        [System.Text.UTF8Encoding]::new($false)
    )
}

function Assert-NoResidualRemovedDependency {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectPath,

        [Parameter(Mandatory = $true)]
        [string[]]$RemovedModules
    )

    $removed = [System.Collections.Generic.HashSet[string]]::new(
        [string[]]$RemovedModules,
        [System.StringComparer]::Ordinal
    )
    $componentsRoot = Join-Path $ProjectPath 'components'
    if (-not (Test-Path -LiteralPath $componentsRoot)) {
        return
    }

    $residualDependencies = New-Object System.Collections.Generic.List[string]
    Get-ChildItem -LiteralPath $componentsRoot -Directory | ForEach-Object {
        $component = $_.Name
        $cmakePath = Join-Path $_.FullName 'CMakeLists.txt'
        if (-not (Test-Path -LiteralPath $cmakePath)) {
            return
        }

        # Collect every hard dependency source: list(APPEND <...>requires ...)
        # variables and each REQUIRES / PRIV_REQUIRES keyword block inside the
        # register call. Optional edges may dangle because
        # idf_component_optional_requires is a no-op for an absent component.
        $source = Get-Content -LiteralPath $cmakePath -Raw
        $dependencyText = New-Object System.Text.StringBuilder
        foreach ($listMatch in [regex]::Matches(
            $source,
            '(?ms)list\(APPEND\s+\w*requires\s+(?<deps>.*?)\)'
        )) {
            [void]$dependencyText.AppendLine($listMatch.Groups['deps'].Value)
        }
        $registerMatch = [regex]::Match(
            $source,
            '(?ms)idf_component_register\((?<body>.*?)\)'
        )
        if ($registerMatch.Success) {
            $body = $registerMatch.Groups['body'].Value
            # Walk the register block line by line: a REQUIRES / PRIV_REQUIRES
            # keyword starts a dependency list that continues until the next
            # keyword or the end of the block. A single regex with a
            # lookahead can match the keyword itself and capture nothing, so
            # the line walk keeps every component name.
            $collecting = $false
            foreach ($line in ($body -split "\r?\n")) {
                if ($line -match '^\s*(?:REQUIRES|PRIV_REQUIRES)\b') {
                    $collecting = $true
                    $remainder = $line -replace '^\s*(?:REQUIRES|PRIV_REQUIRES)\b', ''
                    [void]$dependencyText.AppendLine($remainder)
                    continue
                }
                if ($line -match '^\s*(?:SRCS|INCLUDE_DIRS|PRIV_INCLUDE_DIRS|WHOLE_ARCHIVE|LDFRAGMENTS)\b') {
                    $collecting = $false
                    continue
                }
                if ($collecting) {
                    [void]$dependencyText.AppendLine($line)
                }
            }
        }
        foreach ($token in [regex]::Matches(
            $dependencyText.ToString(),
            '\b([a-z][a-z0-9_]*)\b'
        )) {
            $dependency = $token.Groups[1].Value
            if ($removed.Contains($dependency)) {
                $residualDependencies.Add("$component -> $dependency")
            }
        }
    }

    if ($residualDependencies.Count -gt 0) {
        $details = ($residualDependencies | Sort-Object) -join '; '
        throw "Remaining components still hard-require removed modules: $details"
    }
}

function Test-OptionalModuleRemoval {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectPath,

        [Parameter(Mandatory = $true)]
        [string]$PlatformIoCommand
    )

    # The *_sync companions and connectivity_report are Kconfig-gated behind the
    # modules below, but ESP-IDF still evaluates every component directory it
    # can see and resolves each component's REQUIRES list. A companion that is
    # left in place therefore keeps a hard edge to a deleted component and
    # fails at the configure step, so it has to leave the graph with the module
    # it hard-requires.
    $removableModules = @(
        'ui_text_sync'
        'vision_sync'
        'theme_package_sync'
        'english_course_sync'
        'playback_request_sync'
        'playback_history_sync'
        'local_command_sync'
        'favorites_sync'
        'connectivity_report'
        'ui_text'
        'content_filter'
        'camera_vision'
        'camera_capture'
        'theme_package'
        'companionship'
        'growth_record'
        'habit_reminder'
        'long_term_memory_sync'
        'long_term_memory'
        'english_course'
        'playback_request'
        'local_command'
        'offline_content'
        'favorites'
        'playback_history'
        'content_package_manager'
        'content_downloader'
        'content_library'
        'voice_intercom'
        'care_view'
        'voice_session'
        'privacy_guard'
        'conversation_context'
        'child_prompt_profile'
        'transport_security'
        'factory_reset'
        'led_indicator'
        'button_input'
        'wake_feedback'
        'voice_wake'
        'diagnostic_reporter'
        'device_message'
        'device_mqtt'
        'device_runtime_reporter'
        'provisioning_reporter'
        'parent_control_runtime'
        'usage_ledger'
        'parent_policy'
        'device_provisioning'
        'offline_fallback'
        'network_quality'
        'prompt_tone'
        'audio_output'
        'playback_queue'
        'volume_control'
        'audio_input'
        'audio_pipeline'
        'audio_codec'
    )

    # Keep the copied project path short. ESP-IDF generates deeply nested
    # object paths, and Windows fails with "command line is too long" when the
    # verification copy contains a 32-character GUID.
    $temporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) (
        'sp' + [System.Guid]::NewGuid().ToString('N').Substring(0, 8)
    )

    try {
        New-Item -ItemType Directory -Path $temporaryRoot | Out-Null
        Get-ChildItem -Force -LiteralPath $ProjectPath |
            Where-Object { $_.Name -notin @('.git', '.pio', 'bgen') } |
            Copy-Item -Destination $temporaryRoot -Recurse
        # Remove dependents before the modules they require, so the remaining
        # CMake graph never references a component that is already gone.
        foreach ($moduleName in $removableModules) {
            Remove-OptionalModule `
                -ProjectPath $temporaryRoot `
                -ModuleName $moduleName
        }
        Assert-NoResidualRemovedDependency `
            -ProjectPath $temporaryRoot `
            -RemovedModules $removableModules
        Invoke-PlatformIoBuild `
            -ProjectPath $temporaryRoot `
            -Environments @('esp32-s3-n16r8') `
            -PlatformIoCommand $PlatformIoCommand
    } finally {
        if (Test-Path -LiteralPath $temporaryRoot) {
            Remove-Item -LiteralPath $temporaryRoot -Recurse -Force
        }
    }
}

$platformIoCommand = Resolve-PlatformIoCommand
# The default and minimal profiles always build end to end. The production
# profile also builds whenever a Secure Boot signing key is available (CI
# generates a throwaway one), which exercises the real hardened configuration,
# the signing steps, and the fail-closed key guard.
$environments = @(
    'esp32-s3-n16r8'
    'esp32-s3-n16r8-minimal'
)

# Resolve the same key the production pre-script uses so CI and local runs stay
# consistent. Without a key the production build is skipped and covered by the
# config contract checks below instead.
$productionSigningKey = $env:SPROUT_SECURE_BOOT_SIGNING_KEY
if ([string]::IsNullOrWhiteSpace($productionSigningKey)) {
    $productionSigningKey = Join-Path $firmwareRoot 'secure_boot_signing_key.pem'
}
if (Test-Path -LiteralPath $productionSigningKey -PathType Leaf) {
    $env:SPROUT_SECURE_BOOT_SIGNING_KEY = (Resolve-Path -LiteralPath $productionSigningKey).Path
    $environments += 'esp32-s3-n16r8-production'
} else {
    Write-Host 'No Secure Boot signing key found; skipping the production build.'
}

Invoke-PlatformIoBuild `
    -ProjectPath $firmwareRoot `
    -Environments $environments `
    -PlatformIoCommand $platformIoCommand

Write-Host 'Verifying recovery diagnostic contract.'
$diagnosticHeader = Get-Content -Raw -LiteralPath (
    Join-Path $firmwareRoot 'components/diagnostic_reporter/include/diagnostic_reporter.h'
)
$diagnosticSource = Get-Content -Raw -LiteralPath (
    Join-Path $firmwareRoot 'components/diagnostic_reporter/src/diagnostic_reporter_state.cpp'
)
$diagnosticStateHeader = Get-Content -Raw -LiteralPath (
    Join-Path $firmwareRoot 'components/diagnostic_reporter/src/diagnostic_reporter_state.h'
)
$voiceWakeSource = Get-Content -Raw -LiteralPath (
    Join-Path $firmwareRoot 'components/voice_wake/src/voice_wake.c'
)
$factoryResetSource = Get-Content -Raw -LiteralPath (
    Join-Path $firmwareRoot 'components/factory_reset/src/factory_reset.c'
)
$ledIndicatorSource = Get-Content -Raw -LiteralPath (
    Join-Path $firmwareRoot 'components/led_indicator/src/led_indicator.c'
)
$runtimeSource = Get-Content -Raw -LiteralPath (
    Join-Path $firmwareRoot 'components/device_runtime_reporter/src/device_runtime_reporter.cpp'
)
$recoveryHeader = Get-Content -Raw -LiteralPath (
    Join-Path $firmwareRoot 'components/error_recovery/include/error_recovery.h'
)
$recoverySource = Get-Content -Raw -LiteralPath (
    Join-Path $firmwareRoot 'components/error_recovery/src/error_recovery.c'
)
$registryHeader = Get-Content -Raw -LiteralPath (
    Join-Path $firmwareRoot 'components/module_registry/include/module_registry.h'
)
$audioInputHeader = Get-Content -Raw -LiteralPath (
    Join-Path $firmwareRoot 'components/audio_input/include/audio_input.h'
)
$audioInputSource = Get-Content -Raw -LiteralPath (
    Join-Path $firmwareRoot 'components/audio_input/src/audio_input.c'
)
$audioPipelineHeader = Get-Content -Raw -LiteralPath (
    Join-Path $firmwareRoot 'components/audio_pipeline/include/audio_pipeline.h'
)
$audioOutputHeader = Get-Content -Raw -LiteralPath (
    Join-Path $firmwareRoot 'components/audio_output/include/audio_output.h'
)
$audioOutputSource = Get-Content -Raw -LiteralPath (
    Join-Path $firmwareRoot 'components/audio_output/src/audio_output.c'
)
$playbackQueueHeader = Get-Content -Raw -LiteralPath (
    Join-Path $firmwareRoot 'components/playback_queue/include/playback_queue.h'
)
$securityPostureHeader = Get-Content -Raw -LiteralPath (
    Join-Path $firmwareRoot 'components/security_posture/include/security_posture.h'
)
$securityPostureSource = Get-Content -Raw -LiteralPath (
    Join-Path $firmwareRoot 'components/security_posture/src/security_posture.c'
)
$mainSource = Get-Content -Raw -LiteralPath (
    Join-Path $firmwareRoot 'src/main.c'
)

$contractChecks = @(
    @{
        Name = 'config_store blob size error contract'
        Text = (Get-Content -Raw -LiteralPath (
            Join-Path $firmwareRoot 'components/config_store/src/config_store.c'
        ))
        Pattern = 'ESP_ERR_NVS_INVALID_LENGTH[\s\S]*return ESP_ERR_INVALID_SIZE;'
    },
    @{
        Name = 'diagnostic reporter consumes the config_store size error'
        Text = (Get-Content -Raw -LiteralPath (
            Join-Path $firmwareRoot 'components/diagnostic_reporter/src/diagnostic_reporter.cpp'
        ))
        Pattern = 'result == ESP_ERR_INVALID_SIZE'
    },
    @{
        Name = 'diagnostic state layout version'
        Text = $diagnosticStateHeader
        Pattern = '#define DIAGNOSTIC_REPORTER_STATE_VERSION 6u'
    },
    @{
        Name = 'v5 diagnostic state migration'
        Text = $diagnosticSource
        Pattern = 'diagnostic_reporter_migrate_v5'
    },
    @{
        Name = 'bounded crash capacity'
        Text = $diagnosticHeader
        Pattern = '#define DIAGNOSTIC_REPORTER_CRASH_EVENT_CAPACITY 8'
    },
    @{
        Name = 'crash cause contract validation'
        Text = $diagnosticSource
        Pattern = 'diagnostic_reporter_crash_cause_is_valid'
    },
    @{
        Name = 'crash fault kind contract validation'
        Text = $diagnosticSource
        Pattern = 'diagnostic_reporter_crash_fault_kind_is_valid'
    },
    @{
        Name = 'crash event serialization'
        Text = $runtimeSource
        Pattern = 'diagnostics_object\["crash_events"\]'
    },
    @{
        Name = 'crash reset classification'
        Text = (Get-Content -Raw -LiteralPath (
            Join-Path $firmwareRoot 'components/diagnostic_reporter/src/diagnostic_reporter.cpp'
        ))
        Pattern = 'diagnostic_reporter_crash_from_reset'
    },
    @{
        Name = 'v3 diagnostic state migration'
        Text = $diagnosticSource
        Pattern = 'diagnostic_reporter_migrate_v3'
    },
    @{
        Name = 'v4 diagnostic state migration'
        Text = $diagnosticSource
        Pattern = 'diagnostic_reporter_migrate_v4'
    },
    @{
        Name = 'detail code contract validation'
        Text = $diagnosticSource
        Pattern = 'diagnostic_reporter_detail_code_is_valid'
    },
    @{
        Name = 'wake rejection producer'
        Text = $voiceWakeSource
        Pattern = 'diagnostic_reporter_record_interaction\(\s+"wake_rejected"'
    },
    @{
        Name = 'wake rejected rate limit'
        Text = $voiceWakeSource
        Pattern = 'VOICE_WAKE_REJECTION_EVENT_INTERVAL_MS'
    },
    @{
        Name = 'stable wake detail code'
        Text = $voiceWakeSource
        Pattern = 'wake_%lu_confidence_%04lu'
    },
    @{
        Name = 'factory reset requested producer'
        Text = $factoryResetSource
        Pattern = 'factory_reset_report_event\(\s*"factory_reset_requested"'
    },
    @{
        Name = 'factory reset cancelled producer'
        Text = $factoryResetSource
        Pattern = 'factory_reset_report_event\(\s*"factory_reset_cancelled"'
    },
    @{
        Name = 'factory reset completed producer'
        Text = $factoryResetSource
        Pattern = '"factory_reset_completed"'
    },
    @{
        Name = 'factory reset failed producer'
        Text = $factoryResetSource
        Pattern = '"factory_reset_failed"'
    },
    @{
        Name = 'wake detected producer'
        Text = (Get-Content -Raw -LiteralPath (
            Join-Path $firmwareRoot 'components/wake_feedback/src/wake_feedback.c'
        ))
        Pattern = 'diagnostic_reporter_record_interaction\(\s+"wake_detected"'
    },
    @{
        Name = 'indicator state producer'
        Text = $ledIndicatorSource
        Pattern = 'diagnostic_reporter_record_interaction\(\s+"indicator_state"'
    },
    @{
        Name = 'bounded recovery capacity'
        Text = $diagnosticHeader
        Pattern = '#define DIAGNOSTIC_REPORTER_RECOVERY_EVENT_CAPACITY 8'
    },
    @{
        Name = 'bounded recovery transition queue'
        Text = $recoveryHeader
        Pattern = '#define ERROR_RECOVERY_RECOVERY_EVENT_CAPACITY 8'
    },
    @{
        Name = 'shared transition ordering'
        Text = $recoverySource
        Pattern = 'transition_count'
    },
    @{
        Name = 'recovery event type'
        Text = $runtimeSource
        Pattern = 'item\["event_type"\] = "module_recovered";'
    },
    @{
        Name = 'recovery event module name'
        Text = $runtimeSource
        Pattern = 'item\["module_name"\] = event->module_name;'
    },
    @{
        Name = 'recovery acknowledgement'
        Text = $diagnosticSource
        Pattern = 'recovery_event_count =\s+retained_recovery_count;'
    },
    @{
        Name = 'interaction acknowledgement'
        Text = $diagnosticSource
        Pattern = 'interaction_event_count = retained_interaction_count;'
    },
    @{
        Name = 'factory reset ack uses the same command id'
        Text = $runtimeSource
        Pattern = 'device_runtime_mark_factory_reset_completed\([\s\S]*command->id'
    },
    @{
        Name = 'factory reset ack retry survives restart'
        Text = $runtimeSource
        Pattern = 'RTC_NOINIT_ATTR[\s\S]*device_runtime_factory_reset_completed_command_id'
    },
    @{
        Name = 'interaction events stay bounded'
        Text = $diagnosticHeader
        Pattern = '#define DIAGNOSTIC_REPORTER_INTERACTION_EVENT_CAPACITY 16'
    },
    @{
        Name = 'interaction detail code validation'
        Text = $diagnosticSource
        Pattern = 'diagnostic_reporter_detail_code_is_valid'
    },
    @{
        Name = 'interaction event type validation'
        Text = $diagnosticSource
        Pattern = 'diagnostic_reporter_interaction_type_is_valid'
    },
    @{
        Name = 'recovery callback registration'
        Text = $recoverySource
        Pattern = 'module_registry_set_recovery_handler\(error_recovery_module_recovered\);'
    },
    @{
        Name = 'ordered transition observation'
        Text = $diagnosticSource
        Pattern = 'last_recovery_transition_count'
    },
    @{
        Name = 'explicit single-module retry API'
        Text = $registryHeader
        Pattern = 'module_registry_initialize_module'
    },
    @{
        Name = 'audio input stream lifecycle API'
        Text = $audioInputHeader
        Pattern = 'audio_input_start\(\s+uint32_t stream_id'
    },
    @{
        Name = 'audio input echo and gain diagnostics'
        Text = $audioInputHeader
        Pattern = 'aec_convergence_q10'
    },
    @{
        Name = 'audio input gain diagnostic'
        Text = $audioInputHeader
        Pattern = 'agc_gain_q8'
    },
    @{
        Name = 'audio input scalar quality metrics'
        Text = $audioInputHeader
        Pattern = 'audio_input_start_with_metrics'
    },
    @{
        Name = 'audio input microphone quality level'
        Text = $audioInputHeader
        Pattern = 'microphone_level_q15'
    },
    @{
        Name = 'audio input encodes opus frames'
        Text = $audioInputSource
        Pattern = 'audio_codec_encode_frame\('
    },
    @{
        Name = 'audio input capture starts only on start'
        Text = $audioInputSource
        Pattern = 'audio_pipeline_capture_acquire\(\s+AUDIO_PIPELINE_CAPTURE_OWNER_AUDIO_INPUT'
    },
    @{
        Name = 'audio input capture stops and clears reference on stop'
        Text = $audioInputSource
        Pattern = 'audio_pipeline_capture_release\(\s+AUDIO_PIPELINE_CAPTURE_OWNER_AUDIO_INPUT'
    },
    @{
        Name = 'audio pipeline has a stable capture owner contract'
        Text = $audioPipelineHeader
        Pattern = 'AUDIO_PIPELINE_CAPTURE_OWNER_VOICE_WAKE[\s\S]*AUDIO_PIPELINE_CAPTURE_OWNER_AUDIO_INPUT'
    },
    @{
        Name = 'audio pipeline rejects ownerless frame reads'
        Text = $audioPipelineHeader
        Pattern = 'audio_pipeline_capture_frame\(\s+audio_pipeline_capture_owner_t owner'
    },
    @{
        Name = 'audio pipeline locks owner validation with I2S reads'
        Text = (Get-Content -Raw -LiteralPath (
            Join-Path $firmwareRoot 'components/audio_pipeline/src/audio_pipeline.c'
        ))
        Pattern = 'audio_pipeline_capture_owner != owner'
    },
    @{
        Name = 'voice wake uses the capture owner lease'
        Text = (Get-Content -Raw -LiteralPath (
            Join-Path $firmwareRoot 'components/voice_wake/src/voice_wake.c'
        ))
        Pattern = 'audio_pipeline_capture_acquire\(\s+AUDIO_PIPELINE_CAPTURE_OWNER_VOICE_WAKE'
    },
    @{
        Name = 'audio input clears the reference sink on stop'
        Text = $audioInputSource
        Pattern = 'audio_pipeline_set_reference_sink\(NULL, NULL\)'
    },
    @{
        Name = 'audio pipeline reference sink API'
        Text = $audioPipelineHeader
        Pattern = 'audio_pipeline_set_reference_sink\('
    },
    @{
        Name = 'voice session full-duplex core'
        Text = (Get-Content -Raw -LiteralPath (
            Join-Path $firmwareRoot 'components/voice_session/Kconfig'
        ))
        Pattern = 'FEATURE_VOICE_DUPLEX'
    },
    @{
        Name = 'voice session idle timeout configuration'
        Text = (Get-Content -Raw -LiteralPath (
            Join-Path $firmwareRoot 'components/voice_session/Kconfig'
        ))
        Pattern = 'VOICE_SESSION_IDLE_TIMEOUT_MS'
    },
    @{
        Name = 'voice session barge-in protocol frame'
        Text = (Get-Content -Raw -LiteralPath (
            Join-Path $firmwareRoot 'components/voice_session/src/voice_session.c'
        ))
        Pattern = 'voice_session_build_barge_in'
    },
    @{
        Name = 'voice session quality protocol frame'
        Text = (Get-Content -Raw -LiteralPath (
            Join-Path $firmwareRoot 'components/voice_session/src/voice_session.c'
        ))
        Pattern = 'voice_session_build_quality'
    },
    @{
        Name = 'audio output priority lanes'
        Text = $audioOutputHeader
        Pattern = 'AUDIO_OUTPUT_PRIORITY_SAFETY'
    },
    @{
        Name = 'audio output blocking frame submission'
        Text = $audioOutputHeader
        Pattern = 'audio_output_submit_blocking\('
    },
    @{
        Name = 'audio output priority discard'
        Text = $audioOutputHeader
        Pattern = 'audio_output_discard_priority\('
    },
    @{
        Name = 'audio output saturating mix'
        Text = $audioOutputSource
        Pattern = 'audio_output_saturate_add\('
    },
    @{
        Name = 'audio output safety bypasses mute'
        Text = $audioOutputSource
        Pattern = 'CONFIG_AUDIO_OUTPUT_SAFETY_MIN_PERCENT'
    },
    @{
        Name = 'audio output feeds the reference sink'
        Text = $audioOutputSource
        Pattern = 'audio_pipeline_play_frame\('
    },
    @{
        Name = 'playback queue routes through mixer'
        Text = (Get-Content -Raw -LiteralPath (
            Join-Path $firmwareRoot 'components/playback_queue/src/playback_queue.c'
        ))
        Pattern = 'audio_output_submit_blocking\('
    },
    @{
        Name = 'playback queue exposes shutdown'
        Text = $playbackQueueHeader
        Pattern = 'playback_queue_shutdown\('
    },
    @{
        Name = 'prompt tone mixes with active conversation'
        Text = (Get-Content -Raw -LiteralPath (
            Join-Path $firmwareRoot 'components/prompt_tone/src/prompt_tone.c'
        ))
        Pattern = 'prompt_tone_play_mixed\('
    }
    @{
        Name = 'production security posture gate exists'
        Text = $securityPostureHeader
        Pattern = 'security_posture_guard'
    }
    @{
        Name = 'security posture refuses insecure boot'
        Text = $securityPostureSource
        Pattern = 'SECURITY_POSTURE_VERDICT_INSECURE_BOOT'
    }
    @{
        Name = 'security posture refuses unencrypted flash'
        Text = $securityPostureSource
        Pattern = 'SECURITY_POSTURE_VERDICT_UNENCRYPTED_FLASH'
    }
    @{
        Name = 'security posture halts instead of continuing'
        Text = $securityPostureSource
        Pattern = 'security_posture_halt\(\)'
    }
    @{
        Name = 'security posture runs first in the composition root'
        Text = $mainSource
        Pattern = 'module_registry_add\(security_posture_module_descriptor\(\)\)\);[\s\S]*module_registry_add\(system_core_module_descriptor\(\)\)\)'
    }
    @{
        Name = 'production build gate defaults closed'
        Text = (Get-Content -Raw -LiteralPath (
            Join-Path $firmwareRoot 'components/security_posture/Kconfig'
        ))
        Pattern = 'config SPROUT_PRODUCTION_BUILD[\s\S]*default n'
    }
    @{
        Name = 'english course cache lives in external RAM'
        Text = (Get-Content -Raw -LiteralPath (
            Join-Path $firmwareRoot 'components/english_course/src/english_course.c'
        ))
        Pattern = 'MALLOC_CAP_SPIRAM'
    }
    @{
        Name = 'temperature monitor links its sensor driver'
        Text = (Get-Content -Raw -LiteralPath (
            Join-Path $firmwareRoot 'components/temperature_monitor/CMakeLists.txt'
        ))
        Pattern = 'PRIV_REQUIRES[\s\S]*esp_driver_tsens'
    }
    @{
        Name = 'temperature monitor compiles its classifier'
        Text = (Get-Content -Raw -LiteralPath (
            Join-Path $firmwareRoot 'components/temperature_monitor/CMakeLists.txt'
        ))
        Pattern = 'list\(APPEND temperature_monitor_sources "src/temperature_monitor_core\.c"\)'
    }
    @{
        Name = 'production profile turns on the runtime gate'
        Text = (Get-Content -Raw -LiteralPath (
            Join-Path $firmwareRoot 'sdkconfig.production.defaults'
        ))
        Pattern = 'CONFIG_SPROUT_PRODUCTION_BUILD=y'
    }
    @{
        Name = 'production profile enables Secure Boot and Flash Encryption'
        Text = (Get-Content -Raw -LiteralPath (
            Join-Path $firmwareRoot 'sdkconfig.production.defaults'
        ))
        Pattern = 'CONFIG_SECURE_BOOT=y[\s\S]*CONFIG_SECURE_FLASH_ENC_ENABLED=y'
    }
    @{
        Name = 'production profile derives NVS keys from an HMAC eFuse block'
        Text = (Get-Content -Raw -LiteralPath (
            Join-Path $firmwareRoot 'sdkconfig.production.defaults'
        ))
        Pattern = 'CONFIG_NVS_SEC_KEY_PROTECT_USING_HMAC=y[\s\S]*CONFIG_NVS_SEC_HMAC_EFUSE_KEY_ID=[0-5]'
    }
    @{
        Name = 'production environment layers the hardening overlay'
        Text = (Get-Content -Raw -LiteralPath (
            Join-Path $firmwareRoot 'platformio.ini'
        ))
        Pattern = 'env:esp32-s3-n16r8-production[\s\S]*sdkconfig\.production\.defaults'
    }
    @{
        Name = 'production environment fails closed without a signing key'
        Text = (Get-Content -Raw -LiteralPath (
            Join-Path $firmwareRoot 'platformio.ini'
        ))
        Pattern = 'env:esp32-s3-n16r8-production[\s\S]*verify_production_signing\.py'
    }
)

$rawNvsSizeConsumers = New-Object System.Collections.Generic.List[string]
foreach ($extension in @('*.c', '*.cpp', '*.h')) {
    Get-ChildItem `
        -LiteralPath (Join-Path $firmwareRoot 'components') `
        -Recurse `
        -File `
        -Filter $extension |
        Where-Object {
            $_.FullName -notmatch '[\\/]config_store[\\/]' -and
            $_.FullName -notmatch '[\\/]test[\\/]' -and
            (Get-Content -Raw -LiteralPath $_.FullName) -match
                'ESP_ERR_NVS_INVALID_LENGTH'
        } |
        ForEach-Object { $rawNvsSizeConsumers.Add($_.FullName) }
}
if ($rawNvsSizeConsumers.Count -gt 0) {
    throw (
        'Components above config_store must not consume raw NVS size errors: ' +
        ($rawNvsSizeConsumers -join '; ')
    )
}

foreach ($contractCheck in $contractChecks) {
    if ($contractCheck.Text -notmatch $contractCheck.Pattern) {
        throw "Recovery diagnostic contract check failed: $($contractCheck.Name)."
    }
}

Test-OptionalModuleRemoval `
    -ProjectPath $firmwareRoot `
    -PlatformIoCommand $platformIoCommand

$trackedBuildOutput = git -C $firmwareRoot ls-files -- `
    'bgen/*' `
    'sdkconfig.esp32-s3-n16r8' `
    'AGENTS.md' `
    'docs/*'
if ($LASTEXITCODE -ne 0) {
    throw 'Unable to inspect tracked firmware files.'
}
if ($trackedBuildOutput) {
    throw "Generated or local-only files are tracked: $trackedBuildOutput"
}

Write-Host 'Removable module verification passed.'
