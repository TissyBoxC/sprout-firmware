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

    $pythonEnvironment = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\platformio.exe'
    if (Test-Path -LiteralPath $pythonEnvironment) {
        return $pythonEnvironment
    }

    throw 'PlatformIO CLI was not found.'
}

$platformIoCommand = Resolve-PlatformIoCommand
$environments = @(
    'esp32-s3-n16r8',
    'esp32-s3-n16r8-minimal'
)

foreach ($environment in $environments) {
    Write-Host "Building $environment"
    # CI compilers are run serially because parallel Xtensa GCC builds have
    # intermittently crashed inside ESP-IDF LCD translation units.
    & $platformIoCommand run -e $environment -j 1
    if ($LASTEXITCODE -ne 0) {
        throw "PlatformIO build failed for $environment."
    }
}

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
