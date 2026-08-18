[CmdletBinding()]
param(
    [string]$DestinationDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'Inno Setup can only be installed on Windows.'
}

$innoVersion = '6.7.3'
$downloadUri = "https://github.com/jrsoftware/issrc/releases/download/is-6_7_3/innosetup-$innoVersion.exe"
$expectedSha256 = '9c73c3bae7ed48d44112a0f48e66742c00090bdb5bef71d9d3c056c66e97b732'

if (-not $DestinationDirectory) {
    $baseDirectory = if ($env:RUNNER_TEMP) {
        $env:RUNNER_TEMP
    }
    else {
        [System.IO.Path]::GetTempPath()
    }
    $DestinationDirectory = Join-Path $baseDirectory "Inno Setup $innoVersion"
}

$resolvedDestination = [System.IO.Path]::GetFullPath($DestinationDirectory)
$compiler = Join-Path $resolvedDestination 'ISCC.exe'
$versionMarker = Join-Path $resolvedDestination '.home-control-inno-version'
if (
    (Test-Path -LiteralPath $compiler -PathType Leaf) -and
    (Test-Path -LiteralPath $versionMarker -PathType Leaf) -and
    ((Get-Content -Raw -LiteralPath $versionMarker).Trim() -eq $innoVersion)
) {
    Write-Output $compiler
    return
}

$temporaryDirectory = Join-Path (
    [System.IO.Path]::GetTempPath()
) "home-control-inno-$([Guid]::NewGuid().ToString('N'))"
[System.IO.Directory]::CreateDirectory($temporaryDirectory) | Out-Null
$installer = Join-Path $temporaryDirectory "innosetup-$innoVersion.exe"

try {
    Invoke-WebRequest -Uri $downloadUri -OutFile $installer -UseBasicParsing
    $actualSha256 = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualSha256 -ne $expectedSha256) {
        throw "Inno Setup SHA-256 mismatch: expected $expectedSha256, got $actualSha256."
    }

    [System.IO.Directory]::CreateDirectory($resolvedDestination) | Out-Null
    $process = Start-Process `
        -FilePath $installer `
        -ArgumentList @(
            '/VERYSILENT',
            '/SUPPRESSMSGBOXES',
            '/NORESTART',
            '/SP-',
            '/CURRENTUSER',
            "/DIR=`"$resolvedDestination`""
        ) `
        -WindowStyle Hidden `
        -Wait `
        -PassThru
    if ($process.ExitCode -ne 0) {
        throw "Inno Setup installer failed with exit code $($process.ExitCode)."
    }
    if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) {
        throw "Inno Setup compiler was not installed at $compiler."
    }

    Set-Content -LiteralPath $versionMarker -Value $innoVersion -Encoding ascii -NoNewline
    Write-Output $compiler
}
finally {
    if (Test-Path -LiteralPath $temporaryDirectory -PathType Container) {
        [System.IO.Directory]::Delete($temporaryDirectory, $true)
    }
}
