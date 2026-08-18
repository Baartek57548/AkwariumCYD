[CmdletBinding()]
param(
    [string]$ExpectedVersion,
    [string]$OutputDirectory,
    [string]$InnoCompiler,
    [string]$VCRedist,
    [switch]$SkipFlutterBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'Home Control for Windows must be built on Windows.'
}

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$applicationRoot = Join-Path $repositoryRoot 'apps\home_control'
$pubspecPath = Join-Path $applicationRoot 'pubspec.yaml'
$pubspec = Get-Content -Raw -LiteralPath $pubspecPath
$versionMatch = [regex]::Match(
    $pubspec,
    '(?m)^version:\s*(?<version>(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*))\+(?<build>(?:0|[1-9]\d*))\s*$'
)
if (-not $versionMatch.Success) {
    throw 'apps/home_control/pubspec.yaml must contain a canonical X.Y.Z+N version.'
}

$version = $versionMatch.Groups['version'].Value
$buildNumber = $versionMatch.Groups['build'].Value
if ($ExpectedVersion -and $ExpectedVersion -ne $version) {
    throw "Expected Home Control $ExpectedVersion, but pubspec declares $version."
}

$versionParts = $version.Split('.')
foreach ($component in @($versionParts) + @($buildNumber)) {
    [uint32]$numericComponent = 0
    if (
        -not [uint32]::TryParse($component, [ref]$numericComponent) -or
        $numericComponent -gt 65535
    ) {
        throw "Windows PE version components must be integers from 0 through 65535; got '$component'."
    }
}

if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $repositoryRoot 'artifacts\home-control-windows'
}
$resolvedOutput = [System.IO.Path]::GetFullPath($OutputDirectory)
[System.IO.Directory]::CreateDirectory($resolvedOutput) | Out-Null

function Invoke-CheckedCommand {
    param(
        [Parameter(Mandatory)]
        [string]$Executable,
        [Parameter(Mandatory)]
        [string[]]$Arguments,
        [Parameter(Mandatory)]
        [string]$WorkingDirectory
    )

    Push-Location -LiteralPath $WorkingDirectory
    try {
        & $Executable @Arguments
        if ($LASTEXITCODE -ne 0) {
            throw "$Executable failed with exit code $LASTEXITCODE."
        }
    }
    finally {
        Pop-Location
    }
}

if (-not $SkipFlutterBuild) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $atlSearchRoots = @()
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        $atlInstallations = @(& $vswhere `
            -products * `
            -requires `
            Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            Microsoft.VisualStudio.Component.VC.ATL `
            -property installationPath)
        $atlSearchRoots = @($atlInstallations | Where-Object { $_ })
    }
    if ($atlSearchRoots.Count -eq 0) {
        $atlSearchRoots = @(
            (Join-Path $env:ProgramFiles 'Microsoft Visual Studio'),
            (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio')
        )
    }
    $atlHeader = $atlSearchRoots |
        ForEach-Object {
            $toolsRoot = Join-Path $_ 'VC\Tools\MSVC'
            if (Test-Path -LiteralPath $toolsRoot -PathType Container) {
                Get-ChildItem `
                    -LiteralPath $toolsRoot `
                    -Recurse `
                    -Filter 'atlstr.h' `
                    -File `
                    -ErrorAction SilentlyContinue
            }
        } |
        Where-Object { $_.FullName -match '\\atlmfc\\include\\atlstr\.h$' } |
        Select-Object -First 1
    if (-not $atlHeader) {
        throw 'Visual Studio C++ ATL is required by flutter_secure_storage_windows but is not installed.'
    }
    $flutter = Get-Command flutter -ErrorAction Stop
    Invoke-CheckedCommand -Executable $flutter.Source -Arguments @('pub', 'get', '--enforce-lockfile') -WorkingDirectory $applicationRoot
    Invoke-CheckedCommand -Executable $flutter.Source -Arguments @('build', 'windows', '--release') -WorkingDirectory $applicationRoot
}

$bundleDirectory = Join-Path $applicationRoot 'build\windows\x64\runner\Release'
$requiredBundleFiles = @(
    (Join-Path $bundleDirectory 'HomeControl.exe'),
    (Join-Path $bundleDirectory 'flutter_windows.dll'),
    (Join-Path $bundleDirectory 'data\flutter_assets\AssetManifest.bin')
)
foreach ($requiredFile in $requiredBundleFiles) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Windows release bundle is incomplete: $requiredFile"
    }
}

$versionInfo = (Get-Item -LiteralPath $requiredBundleFiles[0]).VersionInfo
$fullVersion = "$version+$buildNumber"
$numericVersion = "$version.$buildNumber"
$productVersion = $versionInfo.ProductVersion.Trim()
if ($productVersion -ne $fullVersion) {
    throw "HomeControl.exe ProductVersion must be $fullVersion; got '$productVersion'."
}
if (
    $versionInfo.FileMajorPart -ne [int]$versionParts[0] -or
    $versionInfo.FileMinorPart -ne [int]$versionParts[1] -or
    $versionInfo.FileBuildPart -ne [int]$versionParts[2] -or
    $versionInfo.FilePrivatePart -ne [int]$buildNumber
) {
    throw "HomeControl.exe numeric FileVersion must be $numericVersion."
}

if (-not $InnoCompiler) {
    $discoveredCompiler = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($discoveredCompiler) {
        $InnoCompiler = $discoveredCompiler.Source
    }
    else {
        $compilerCandidates = @(
            (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
            (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe')
        )
        foreach ($candidate in $compilerCandidates) {
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                $InnoCompiler = $candidate
                break
            }
        }
    }
}
if (-not $InnoCompiler -or -not (Test-Path -LiteralPath $InnoCompiler -PathType Leaf)) {
    throw 'Inno Setup 6 compiler was not found. Install it or pass -InnoCompiler.'
}

if (-not $VCRedist) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        $installations = & $vswhere -products * -property installationPath
        $candidates = foreach ($installation in $installations) {
            $redistRoot = Join-Path $installation 'VC\Redist\MSVC'
            if (Test-Path -LiteralPath $redistRoot -PathType Container) {
                Get-ChildItem -LiteralPath $redistRoot -Recurse -Filter 'vc_redist.x64.exe' -File
            }
        }
        $VCRedist = $candidates |
            Sort-Object -Property FullName -Descending |
            Select-Object -First 1 -ExpandProperty FullName
    }
}
if (-not $VCRedist -or -not (Test-Path -LiteralPath $VCRedist -PathType Leaf)) {
    throw 'Microsoft Visual C++ x64 Redistributable was not found. Pass -VCRedist.'
}
$redistSignature = Get-AuthenticodeSignature -LiteralPath $VCRedist
if (
    $redistSignature.Status -ne [System.Management.Automation.SignatureStatus]::Valid -or
    -not $redistSignature.SignerCertificate -or
    $redistSignature.SignerCertificate.Subject -notmatch '(?:^|,\s*)O=Microsoft Corporation(?:,|$)'
) {
    throw "Microsoft Visual C++ Redistributable has an invalid or unexpected Authenticode signature: $VCRedist"
}
$redistVersionInfo = (Get-Item -LiteralPath $VCRedist).VersionInfo
$redistVersionParts = @(
    $redistVersionInfo.FileMajorPart,
    $redistVersionInfo.FileMinorPart,
    $redistVersionInfo.FileBuildPart,
    $redistVersionInfo.FilePrivatePart
)
if (
    $redistVersionParts[0] -ne 14 -or
    @($redistVersionParts | Where-Object { $_ -lt 0 }).Count -ne 0
) {
    throw "Microsoft Visual C++ Redistributable must expose a valid 14.x numeric FileVersion: $VCRedist"
}
$redistVersion = $redistVersionParts -join '.'

$outputBaseName = "Home-Control-$version-Windows-x64-Setup"
$installerScript = Join-Path $applicationRoot 'windows\installer\HomeControl.iss'
$compilerArguments = @(
    "/DAppVersion=$version",
    "/DAppBuildNumber=$buildNumber",
    "/DSourceDir=$bundleDirectory",
    "/DOutputDir=$resolvedOutput",
    "/DOutputBaseFilename=$outputBaseName",
    "/DVCRedistPath=$VCRedist",
    "/DVCRedistVersion=$redistVersion",
    $installerScript
)
Invoke-CheckedCommand -Executable $InnoCompiler -Arguments $compilerArguments -WorkingDirectory $applicationRoot

$installerPath = Join-Path $resolvedOutput "$outputBaseName.exe"
if (-not (Test-Path -LiteralPath $installerPath -PathType Leaf)) {
    throw "Inno Setup did not create $installerPath."
}
if ((Get-Item -LiteralPath $installerPath).Length -le 0) {
    throw "Windows installer is empty: $installerPath"
}

$installerVersionInfo = (Get-Item -LiteralPath $installerPath).VersionInfo
$installerProductVersion = $installerVersionInfo.ProductVersion.Trim()
$installerFileVersion = $installerVersionInfo.FileVersion.Trim()
if (
    $installerProductVersion -ne $numericVersion -or
    $installerFileVersion -ne $numericVersion
) {
    throw "Installer ProductVersion and FileVersion must be $numericVersion; got '$installerProductVersion' and '$installerFileVersion'."
}

Write-Output $installerPath
