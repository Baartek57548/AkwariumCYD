[CmdletBinding()]
param(
    [string]$HubHost = "aquahub.local",
    [ValidateRange(1, 65535)]
    [int]$HttpsPort = 8443,
    [Parameter(Mandatory = $true)]
    [string]$ExpectedSha256,
    [string]$OpenSslCommand = "openssl"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$expected = ($ExpectedSha256 -replace "[^0-9A-Fa-f]", "").ToUpperInvariant()
if ($expected -notmatch "^[0-9A-F]{64}$") {
    throw "ExpectedSha256 musi zawierać dokładnie 64 cyfry szesnastkowe."
}
if ($HubHost -notmatch "^[A-Za-z0-9.-]+$") {
    throw "HubHost zawiera niedozwolone znaki."
}

$openssl = Get-Command -Name $OpenSslCommand -ErrorAction Stop
$endpoint = "${HubHost}:$HttpsPort"
$transcript = & $openssl.Source s_client `
    -connect $endpoint `
    -servername $HubHost `
    -showcerts 2>$null | Out-String
if ($LASTEXITCODE -ne 0) {
    throw "Nie udało się pobrać certyfikatu TLS z $endpoint."
}

$certificateMatch = [regex]::Match(
    $transcript,
    "-----BEGIN CERTIFICATE-----[\s\S]+?-----END CERTIFICATE-----"
)
if (-not $certificateMatch.Success) {
    throw "AquaHub nie zwrócił certyfikatu PEM."
}

$temporaryCertificate = New-TemporaryFile
try {
    Set-Content -LiteralPath $temporaryCertificate.FullName `
        -Value $certificateMatch.Value `
        -Encoding ascii `
        -NoNewline
    $fingerprintOutput = & $openssl.Source x509 `
        -in $temporaryCertificate.FullName `
        -noout `
        -fingerprint `
        -sha256 | Out-String
    if ($LASTEXITCODE -ne 0) {
        throw "OpenSSL nie zdołał obliczyć odcisku certyfikatu."
    }
    $actual = ($fingerprintOutput -replace "^[^=]*=", "" `
        -replace "[^0-9A-Fa-f]", "").ToUpperInvariant()
    if ($actual -ne $expected) {
        throw "Odcisk certyfikatu nie zgadza się z panelem. Oczekiwano $expected, odebrano $actual."
    }

    $repositoryRoot = Split-Path -Parent $PSScriptRoot
    $targetDirectory = Join-Path $repositoryRoot "firmware\esp32c6_gateway\main"
    $targetPath = Join-Path $targetDirectory "aquahub.pem"
    $resolvedTargetDirectory = (Resolve-Path -LiteralPath $targetDirectory).Path
    $resolvedRepository = (Resolve-Path -LiteralPath $repositoryRoot).Path
    if (-not $resolvedTargetDirectory.StartsWith(
            $resolvedRepository,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Docelowy katalog certyfikatu leży poza repozytorium."
    }
    Copy-Item -LiteralPath $temporaryCertificate.FullName `
        -Destination $targetPath `
        -Force
    Write-Host "Zweryfikowany certyfikat zapisano w $targetPath"
    Write-Host "W menuconfig włącz AQUACYD_MQTT_EMBED_HUB_CERTIFICATE przed kompilacją C6."
}
finally {
    Remove-Item -LiteralPath $temporaryCertificate.FullName -Force -ErrorAction SilentlyContinue
}
