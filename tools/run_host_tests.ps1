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

Write-Host 'Host verification passed.'
