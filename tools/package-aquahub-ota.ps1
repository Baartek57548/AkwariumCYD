[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$FirmwarePath,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9A-Za-z][0-9A-Za-z.+-]{0,31}$')]
    [string]$Version,

    [ValidateRange(0, 2147483647)]
    [int]$SecurityVersion = 1,

    [ValidateLength(0, 383)]
    [string]$Notes = 'Stable AquaHub ESP32-P4 release.',

    [ValidateLength(0, 48)]
    [string]$ReleaseId = '',

    [string]$OutputDirectory = 'artifacts/aquahub-ota',

    [switch]$Mandatory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$source = Get-Item -LiteralPath $FirmwarePath
if ($source.Extension -ne '.bin') {
    throw 'The OTA firmware must be a .bin file.'
}
if ($source.Length -le 0 -or $source.Length -gt 5MB) {
    throw 'The OTA firmware size must be between 1 byte and 5 MiB.'
}

$effectiveReleaseId = if ([string]::IsNullOrWhiteSpace($ReleaseId)) {
    "stable-$Version"
} else {
    $ReleaseId
}
if ($effectiveReleaseId.Length -gt 48) {
    throw 'The release identifier cannot exceed 48 characters.'
}

$destinationDirectory = [System.IO.Path]::GetFullPath(
    (Join-Path (Get-Location) $OutputDirectory)
)
[System.IO.Directory]::CreateDirectory($destinationDirectory) | Out-Null
$fileName = "aquahub-p4-$Version.bin"
$destinationFirmware = Join-Path $destinationDirectory $fileName
Copy-Item -LiteralPath $source.FullName -Destination $destinationFirmware -Force

$hash = (Get-FileHash -LiteralPath $destinationFirmware -Algorithm SHA256).Hash.ToUpperInvariant()
$size = (Get-Item -LiteralPath $destinationFirmware).Length
$manifest = [ordered]@{
    target = 'aquahub-p4'
    release_id = $effectiveReleaseId
    version = $Version
    file = $fileName
    size = $size
    sha256 = $hash
    security_version = $SecurityVersion
    mandatory = [bool]$Mandatory
    notes = $Notes
}
$manifestJson = $manifest | ConvertTo-Json -Depth 3
$manifestPath = Join-Path $destinationDirectory 'manifest.json'
$utf8WithoutBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText(
    $manifestPath,
    $manifestJson + [Environment]::NewLine,
    $utf8WithoutBom
)

$verification = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($verification.sha256 -ne $hash -or $verification.size -ne $size) {
    throw 'The final OTA manifest verification failed.'
}

Write-Host "OTA package ready: $destinationDirectory"
Write-Host "Firmware: $fileName ($size bytes)"
Write-Host "SHA-256: $hash"
