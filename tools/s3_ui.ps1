[CmdletBinding()]
param(
    [ValidateSet('build','upload')][string]$Action='build',
    [string]$Port='COM10'
)
$ErrorActionPreference='Stop'
$uiRepoRoot=[System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$uiPackagesPath=Join-Path $uiRepoRoot '.pio/packages-s3'
$uiOldPackages=$env:PLATFORMIO_PACKAGES_DIR
$uiOldEncoding=$env:PYTHONIOENCODING
$uiOldUtf8=$env:PYTHONUTF8
Push-Location $uiRepoRoot
try {
    # pioarduino's package cleanup cannot remove an Arduino 2.x package belonging
    # to the CYD build when the S3 dependencies have their own package directory.
    $env:PLATFORMIO_PACKAGES_DIR=$uiPackagesPath
    $env:PYTHONIOENCODING='utf-8'
    $env:PYTHONUTF8='1'
    $uiArguments=@('-m','platformio','run','-e','esp32s3_waveshare_ui')
    if($Action -eq 'upload') {$uiArguments+=@('-t','upload','--upload-port',$Port)}
    & python @uiArguments
    if($LASTEXITCODE -ne 0){throw "ESP32-S3 $Action failed (exit $LASTEXITCODE)."}
} finally {
    $env:PLATFORMIO_PACKAGES_DIR=$uiOldPackages
    $env:PYTHONIOENCODING=$uiOldEncoding
    $env:PYTHONUTF8=$uiOldUtf8
    Pop-Location
}
