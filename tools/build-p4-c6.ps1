[CmdletBinding()]
param(
    [ValidateSet("all", "c6", "p4")]
    [string]$Target = "all",

    [string]$IdfPath = $env:IDF_PATH,

    [string]$IdfToolsPath = $env:IDF_TOOLS_PATH,

    [switch]$Flash,

    [string]$Port,

    [switch]$Monitor
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($IdfPath)) {
    throw "Podaj -IdfPath albo aktywuj środowisko ESP-IDF 5.4.x."
}

$resolvedIdfPath = (Resolve-Path -LiteralPath $IdfPath).Path
$exportScript = Join-Path $resolvedIdfPath "export.ps1"
if (-not (Test-Path -LiteralPath $exportScript -PathType Leaf)) {
    throw "Nie znaleziono export.ps1 w katalogu ESP-IDF: $resolvedIdfPath"
}

if ([string]::IsNullOrWhiteSpace($IdfToolsPath)) {
    $toolCandidates = @(
        "C:\Espressif\tools",
        (Join-Path $env:USERPROFILE ".espressif")
    )
    $IdfToolsPath = $toolCandidates |
        Where-Object { Test-Path -LiteralPath $_ -PathType Container } |
        Select-Object -First 1
}
if ([string]::IsNullOrWhiteSpace($IdfToolsPath) -or
    -not (Test-Path -LiteralPath $IdfToolsPath -PathType Container)) {
    throw "Podaj poprawny katalog narzędzi przez -IdfToolsPath."
}
$env:IDF_TOOLS_PATH = (Resolve-Path -LiteralPath $IdfToolsPath).Path

$exportMessages = . $exportScript
$exportMessages | Out-Null
$idfVersion = (& idf.py --version 2>&1 | Out-String).Trim()
if ($LASTEXITCODE -ne 0) {
    throw "Nie można uruchomić idf.py."
}
if ($idfVersion -notmatch "^ESP-IDF v5\.4(\.|$)") {
    throw "Wymagany jest ESP-IDF 5.4.x, wykryto: $idfVersion"
}

if ($Flash -and [string]::IsNullOrWhiteSpace($Port)) {
    throw "Wgrywanie wymaga jawnego parametru -Port, np. COM7."
}
if ($Flash -and $Target -eq "all") {
    throw "Wgrywaj pojedynczy układ: wybierz -Target c6 albo -Target p4."
}
if ($Monitor -and -not $Flash) {
    throw "Parametr -Monitor wymaga również -Flash."
}

$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$projects = switch ($Target) {
    "c6" { @("firmware\esp32c6_gateway") }
    "p4" { @("firmware\esp32p4_hmi") }
    default {
        @(
            "firmware\esp32c6_gateway",
            "firmware\esp32p4_hmi"
        )
    }
}

foreach ($relativeProject in $projects) {
    $projectPath = Join-Path $repositoryRoot $relativeProject
    if (-not (Test-Path -LiteralPath $projectPath -PathType Container)) {
        throw "Brak projektu ESP-IDF: $projectPath"
    }

    Push-Location -LiteralPath $projectPath
    try {
        & idf.py build
        if ($LASTEXITCODE -ne 0) {
            throw "Kompilacja nie powiodła się: $relativeProject"
        }

        if ($Flash) {
            $flashArguments = @("-p", $Port, "flash")
            if ($Monitor) {
                $flashArguments += "monitor"
            }
            & idf.py @flashArguments
            if ($LASTEXITCODE -ne 0) {
                throw "Wgrywanie nie powiodło się: $relativeProject"
            }
        }
    }
    finally {
        Pop-Location
    }
}

Write-Host "Gotowe: $Target ($idfVersion)."
