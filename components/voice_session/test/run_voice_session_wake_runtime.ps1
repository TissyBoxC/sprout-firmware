[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Compiler,

    [Parameter(Mandatory = $true)]
    [string]$CCompiler,

    [Parameter(Mandatory = $true)]
    [string]$FirmwareRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$componentRoot = Split-Path -Parent $PSScriptRoot
$cjsonRoot = Join-Path $FirmwareRoot 'managed_components/espressif__cjson'
if (-not (Test-Path -LiteralPath (Join-Path $cjsonRoot 'cJSON/cJSON.c'))) {
    throw 'The voice session wake runtime test requires the resolved cJSON component.'
}

$outputDirectory = Join-Path ([System.IO.Path]::GetTempPath()) (
    'sprout-voice-session-wake-' + [System.Guid]::NewGuid().ToString('N').Substring(0, 8)
)
New-Item -ItemType Directory -Path $outputDirectory | Out-Null
$executable = Join-Path $outputDirectory 'voice_session_wake_runtime.exe'

try {
    $includeDirectories = @(
        (Join-Path $componentRoot 'include')
        (Join-Path $componentRoot 'src')
        (Join-Path $componentRoot 'test/stubs')
        (Join-Path $FirmwareRoot 'components/voice_wake/include')
        (Join-Path $FirmwareRoot 'components/voice_wake/test/stubs')
        (Join-Path $FirmwareRoot 'components/audio_codec/include')
        (Join-Path $cjsonRoot 'cJSON')
    )
    $includeArguments = @()
    foreach ($includeDirectory in $includeDirectories) {
        $includeArguments += '-I' + $includeDirectory
    }
    $commonArguments = @('-std=c++17', '-Wall', '-Wextra', '-Werror') +
        $includeArguments + @('-DVOICE_SESSION_HOST_TESTS=1')

    $objects = @()
    $cSources = @(
        (Join-Path $componentRoot 'test/stubs/voice_session_runtime_deps.c')
    )
    foreach ($source in $cSources) {
        $object = Join-Path $outputDirectory (
            [System.IO.Path]::GetFileNameWithoutExtension($source) + '.o'
        )
        & $CCompiler '-std=c11' '-Wall' '-Wextra' '-Werror' @includeArguments `
            '-include' (Join-Path $componentRoot 'test/stubs/voice_session_host_config.h') `
            '-DVOICE_SESSION_WAKE_HOST_TESTS=1' `
            '-c' $source '-o' $object
        if ($LASTEXITCODE -ne 0) {
            throw "voice_session_wake_runtime failed to compile $source."
        }
        $objects += $object
    }

    $runtimeSources = @(
        (Join-Path $componentRoot 'src/voice_session_protocol.c')
        (Join-Path $componentRoot 'src/voice_session_token.c')
        (Join-Path $componentRoot 'src/voice_session_wake_observer.c')
        (Join-Path $componentRoot 'src/voice_session.c')
    )
    foreach ($source in $runtimeSources) {
        $object = Join-Path $outputDirectory (
            'runtime_' + [System.IO.Path]::GetFileNameWithoutExtension($source) + '.o'
        )
        & $CCompiler '-std=c11' '-Wall' '-Wextra' '-Werror' @includeArguments `
            '-include' (Join-Path $componentRoot 'test/stubs/voice_session_host_config.h') `
            '-DVOICE_SESSION_WAKE_HOST_TESTS=1' `
            '-c' $source '-o' $object
        if ($LASTEXITCODE -ne 0) {
            throw "voice_session_wake_runtime failed to compile $source."
        }
        $objects += $object
    }

    $cjsonObject = Join-Path $outputDirectory 'cJSON.o'
    & $CCompiler '-std=c11' '-Wall' '-Wextra' '-Werror' @includeArguments `
        '-c' (Join-Path $cjsonRoot 'cJSON/cJSON.c') '-o' $cjsonObject
    if ($LASTEXITCODE -ne 0) {
        throw 'voice_session_wake_runtime failed to compile cJSON.'
    }
    $objects += $cjsonObject

    $testSource = Join-Path $componentRoot 'test/test_voice_session_wake_runtime.cpp'
    $testObject = Join-Path $outputDirectory 'test_voice_session_wake_runtime.o'
    & $Compiler @commonArguments '-c' $testSource '-o' $testObject
    if ($LASTEXITCODE -ne 0) {
        throw "voice_session_wake_runtime failed to compile $testSource."
    }
    $objects += $testObject

    Write-Host 'Building host test voice_session_wake_runtime'
    & $Compiler @commonArguments @objects '-o' $executable
    if ($LASTEXITCODE -ne 0) {
        throw 'voice_session_wake_runtime failed to link.'
    }
    Write-Host 'Running host test voice_session_wake_runtime'
    & $executable
    if ($LASTEXITCODE -ne 0) {
        throw 'voice_session_wake_runtime failed.'
    }
} finally {
    if (Test-Path -LiteralPath $outputDirectory) {
        Remove-Item -LiteralPath $outputDirectory -Recurse -Force
    }
}
