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
        '(?ms)^#if CONFIG_FEATURE_{0}\r?\n\s*ESP_ERROR_CHECK\(module_registry_add\({1}_module_descriptor\(\)\)\);\r?\n#endif\r?\n' -f
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

function Test-OptionalModuleRemoval {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectPath,

        [Parameter(Mandatory = $true)]
        [string]$PlatformIoCommand
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
        foreach ($moduleName in @(
            'ui_text'
            'diagnostic_reporter'
            'device_runtime_reporter'
            'parent_policy'
            'device_provisioning'
            'offline_fallback'
            'network_quality'
            'prompt_tone'
            'playback_queue'
            'volume_control'
            'audio_input'
            'audio_pipeline'
            'audio_codec'
            'cloud_auth'
            'time_sync'
        )) {
            Remove-OptionalModule `
                -ProjectPath $temporaryRoot `
                -ModuleName $moduleName
        }
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
$environments = @(
    'esp32-s3-n16r8',
    'esp32-s3-n16r8-minimal'
)

Invoke-PlatformIoBuild `
    -ProjectPath $firmwareRoot `
    -Environments $environments `
    -PlatformIoCommand $platformIoCommand

Write-Host 'Verifying recovery diagnostic contract.'
$diagnosticHeader = Get-Content -Raw -LiteralPath (
    Join-Path $firmwareRoot 'components/diagnostic_reporter/include/diagnostic_reporter.h'
)
$diagnosticSource = Get-Content -Raw -LiteralPath (
    Join-Path $firmwareRoot 'components/diagnostic_reporter/src/diagnostic_reporter.cpp'
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

$contractChecks = @(
    @{
        Name = 'diagnostic state layout version'
        Text = $diagnosticSource
        Pattern = '#define DIAGNOSTIC_REPORTER_STATE_VERSION 3u'
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
        Name = 'audio input encodes opus frames'
        Text = $audioInputSource
        Pattern = 'audio_codec_encode_frame\('
    },
    @{
        Name = 'audio input capture starts only on start'
        Text = $audioInputSource
        Pattern = 'audio_pipeline_set_capturing\(true\)'
    },
    @{
        Name = 'audio input capture stops and clears reference on stop'
        Text = $audioInputSource
        Pattern = 'audio_pipeline_set_capturing\(false\)'
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
    }
)

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
