param(
    [ValidateSet("configure", "build", "test", "run")]
    [string]$Action = "build",
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$ApplicationArguments
)

$ErrorActionPreference = "Stop"
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$cmake = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

if (-not (Test-Path -LiteralPath $cmake)) {
    $cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
    if (-not $cmakeCommand) {
        throw "CMake 3.21 or newer was not found."
    }
    $cmake = $cmakeCommand.Source
}
$ctest = Join-Path (Split-Path -Parent $cmake) "ctest.exe"

if (-not $env:RAWVIEWER_QT_ROOT) {
    $env:RAWVIEWER_QT_ROOT = "E:\Qt\6.8.3\msvc2022_64"
}
if (-not $env:RAWVIEWER_LIBRAW_ROOT) {
    $env:RAWVIEWER_LIBRAW_ROOT = "E:\LibRaw\0.22.2\LibRaw-0.22.2"
}
$env:PATH = "$env:RAWVIEWER_QT_ROOT\bin;$env:RAWVIEWER_LIBRAW_ROOT\bin;$env:PATH"

$preset = if ($Configuration -eq "Release") {
    "windows-msvc-release"
} else {
    "windows-msvc-debug"
}
$binaryDirectory = Join-Path $repositoryRoot "build\$preset"

function Invoke-CMake {
    param([string[]]$Arguments)
    & $cmake @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "CMake failed with exit code $LASTEXITCODE."
    }
}

function Invoke-CTest {
    & $ctest --preset $preset
    if ($LASTEXITCODE -ne 0) {
        throw "CTest failed with exit code $LASTEXITCODE."
    }
}

Push-Location $repositoryRoot
try {
    if ($Action -in @("configure", "build", "test", "run")) {
        Invoke-CMake @("--preset", $preset)
    }
    if ($Action -in @("build", "test", "run")) {
        Invoke-CMake @("--build", "--preset", $preset, "--", "/m:1")
    }
    if ($Action -eq "test") {
        Invoke-CTest
    }
    if ($Action -eq "run") {
        $executable = Join-Path $binaryDirectory "apps\raw-viewer\$Configuration\RawViewer.exe"
        & $executable @ApplicationArguments
    }
}
finally {
    Pop-Location
}
