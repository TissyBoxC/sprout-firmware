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
            'device_runtime_reporter'
            'device_provisioning'
            'offline_fallback'
            'network_quality'
            'prompt_tone'
            'playback_queue'
            'volume_control'
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
