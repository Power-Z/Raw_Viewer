[CmdletBinding()]
param(
    [string]$Version = "v0.3.0-preview.1",
    [string]$BuildDirectory = "build/windows-msvc-release",
    [string]$OutputDirectory = "artifacts",
    [string]$QtRoot = $env:RAWVIEWER_QT_ROOT,
    [string]$LibRawRoot = $env:RAWVIEWER_LIBRAW_ROOT,
    [switch]$SkipSmokeTest
)

$ErrorActionPreference = "Stop"
$repositoryRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot ".."))
if ([string]::IsNullOrWhiteSpace($QtRoot)) {
    $QtRoot = "E:\Qt\6.8.3\msvc2022_64"
}
if ([string]::IsNullOrWhiteSpace($LibRawRoot)) {
    $LibRawRoot = "E:\LibRaw\0.22.2\LibRaw-0.22.2"
}

$buildRoot = if ([System.IO.Path]::IsPathRooted($BuildDirectory)) {
    [System.IO.Path]::GetFullPath($BuildDirectory)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $BuildDirectory))
}
$outputRoot = if ([System.IO.Path]::IsPathRooted($OutputDirectory)) {
    [System.IO.Path]::GetFullPath($OutputDirectory)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $OutputDirectory))
}
$packageName = "RawViewer-$Version-windows-x64"
$packageRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $outputRoot $packageName))
$expectedPrefix = $outputRoot.TrimEnd('\', '/') +
    [System.IO.Path]::DirectorySeparatorChar
if (-not $packageRoot.StartsWith(
        $expectedPrefix,
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Package directory escaped the selected output directory: $packageRoot"
}

$application = Join-Path $buildRoot "apps/raw-viewer/Release/RawViewer.exe"
$winDeployQt = Join-Path $QtRoot "bin/windeployqt.exe"
$libRawDll = Join-Path $LibRawRoot "bin/libraw.dll"
foreach ($required in @($application, $winDeployQt, $libRawDll)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required release input was not found: $required"
    }
}

New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
if (Test-Path -LiteralPath $packageRoot) {
    Remove-Item -LiteralPath $packageRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $packageRoot | Out-Null
Copy-Item -LiteralPath $application -Destination $packageRoot
Copy-Item -LiteralPath $libRawDll -Destination $packageRoot
Copy-Item -LiteralPath (Join-Path $repositoryRoot "README.md") -Destination $packageRoot
Copy-Item -LiteralPath (Join-Path $repositoryRoot "THIRD_PARTY_NOTICES.md") -Destination $packageRoot

& $winDeployQt `
    --release `
    --compiler-runtime `
    --no-translations `
    --dir $packageRoot `
    (Join-Path $packageRoot "RawViewer.exe")
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE"
}

$vsWhere = Join-Path ${env:ProgramFiles(x86)} `
    "Microsoft Visual Studio/Installer/vswhere.exe"
if (-not (Test-Path -LiteralPath $vsWhere -PathType Leaf)) {
    throw "vswhere.exe was not found; the MSVC runtime cannot be packaged."
}
$visualStudioRoot = (& $vsWhere `
    -latest `
    -products "*" `
    -requires Microsoft.VisualStudio.Component.VC.Redist.14.Latest `
    -property installationPath).Trim()
if ([string]::IsNullOrWhiteSpace($visualStudioRoot)) {
    throw "A Visual Studio installation with the VC143 redistributable was not found."
}
$redistRoot = Join-Path $visualStudioRoot "VC/Redist/MSVC"
$crtDirectory = Get-ChildItem -LiteralPath $redistRoot -Directory |
    ForEach-Object {
        $candidate = Join-Path $_.FullName "x64/Microsoft.VC143.CRT"
        if (Test-Path -LiteralPath $candidate -PathType Container) {
            Get-Item -LiteralPath $candidate
        }
    } |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
if (-not $crtDirectory) {
    throw "The app-local Microsoft.VC143.CRT directory was not found."
}
Get-ChildItem -LiteralPath $crtDirectory.FullName -Filter "*.dll" -File |
    Copy-Item -Destination $packageRoot

$licenseRoot = Join-Path $packageRoot "licenses"
$libRawLicenseRoot = Join-Path $licenseRoot "LibRaw-0.22.2"
$qtLicenseRoot = Join-Path $licenseRoot "Qt-6.8.3"
New-Item -ItemType Directory -Path $libRawLicenseRoot -Force | Out-Null
New-Item -ItemType Directory -Path $qtLicenseRoot -Force | Out-Null
foreach ($license in @("COPYRIGHT", "LICENSE.LGPL", "LICENSE.CDDL")) {
    Copy-Item -LiteralPath (Join-Path $LibRawRoot $license) `
        -Destination $libRawLicenseRoot
}

$qtLicenseUrl =
    "https://code.qt.io/cgit/qt/qtbase.git/plain/LICENSES/LGPL-3.0-only.txt?h=v6.8.3"
$qtExceptionUrl =
    "https://code.qt.io/cgit/qt/qtbase.git/plain/LICENSES/Qt-GPL-exception-1.0.txt?h=v6.8.3"
Invoke-WebRequest -Uri $qtLicenseUrl `
    -OutFile (Join-Path $qtLicenseRoot "LGPL-3.0-only.txt")
Invoke-WebRequest -Uri $qtExceptionUrl `
    -OutFile (Join-Path $qtLicenseRoot "Qt-GPL-exception-1.0.txt")

$commit = (& git -C $repositoryRoot rev-parse HEAD).Trim()
$releaseInfo = @"
Raw Viewer $Version
Platform: Windows 10/11 x64
Commit: $commit
Compiler: Microsoft Visual Studio 2022
Qt: 6.8.3 (dynamic linkage)
LibRaw: 0.22.2 official Win64 package (dynamic linkage)
MSVC Runtime: Visual C++ 2022 x64 app-local runtime

Run RawViewer.exe. No installation is required.
This preview build is unsigned. Windows may show a SmartScreen warning.
Qt source for this release is available from https://code.qt.io/cgit/qt/qtbase.git/tag/?h=v6.8.3
LibRaw source is available from https://www.libraw.org/download
See THIRD_PARTY_NOTICES.md and licenses/ for dependency terms.
"@
[System.IO.File]::WriteAllText(
    (Join-Path $packageRoot "RELEASE-INFO.txt"),
    $releaseInfo,
    [System.Text.UTF8Encoding]::new($false))

if (-not $SkipSmokeTest) {
    $process = Start-Process `
        -FilePath (Join-Path $packageRoot "RawViewer.exe") `
        -WorkingDirectory $packageRoot `
        -PassThru
    try {
        $null = $process.WaitForInputIdle(10000)
        Start-Sleep -Milliseconds 750
        $process.Refresh()
        if ($process.HasExited -or -not $process.Responding) {
            throw "Packaged RawViewer.exe did not remain responsive."
        }
    } finally {
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id
            Wait-Process -Id $process.Id -ErrorAction SilentlyContinue
        }
    }
}

$zipPath = Join-Path $outputRoot "$packageName.zip"
if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}
Compress-Archive -LiteralPath $packageRoot -DestinationPath $zipPath `
    -CompressionLevel Optimal
$hash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash
$checksumPath = "$zipPath.sha256"
[System.IO.File]::WriteAllText(
    $checksumPath,
    "$hash  $([System.IO.Path]::GetFileName($zipPath))`n",
    [System.Text.UTF8Encoding]::new($false))

[pscustomobject]@{
    Package = $zipPath
    Checksum = $checksumPath
    SHA256 = $hash
    SizeMiB = [math]::Round((Get-Item -LiteralPath $zipPath).Length / 1MB, 2)
} | Format-List
