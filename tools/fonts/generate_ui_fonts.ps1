[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$fontRelativePath = 'assets/fonts/inter/Inter-Variable.ttf'
$fontSourcePath = Join-Path $repoRoot $fontRelativePath
$fontExpectedHash = '29160A80FF49DDCAB2C97711247E08B1FAB27A484A329CE8B813D820DC559031'

if (-not (Test-Path -LiteralPath $fontSourcePath -PathType Leaf)) {
    throw "Bundled Inter source not found: $fontSourcePath"
}
if ((Get-FileHash -LiteralPath $fontSourcePath -Algorithm SHA256).Hash -ne $fontExpectedHash) {
    throw 'Inter source checksum differs from the pinned upstream font.'
}

$npmCommand = Get-Command npm.cmd -ErrorAction SilentlyContinue
if ($null -eq $npmCommand) {
    $nodeInstallPath = 'C:/Program Files/nodejs'
    if (-not (Test-Path -LiteralPath (Join-Path $nodeInstallPath 'npm.cmd') -PathType Leaf)) {
        throw 'Node.js/npm must be installed before generating LVGL font assets.'
    }
    $env:Path = "$nodeInstallPath;$env:Path"
    $npmCommand = Get-Command npm.cmd -ErrorAction Stop
}

$fontCodepoints = @(0x20..0x7E) + @(
    0xB0, 0xB5, 0xD3, 0xD7, 0xF3,
    0x104, 0x105, 0x106, 0x107, 0x118, 0x119,
    0x141, 0x142, 0x143, 0x144, 0x15A, 0x15B,
    0x179, 0x17A, 0x17B, 0x17C,
    0x2013, 0x2022, 0x2026, 0x2190, 0x2192
)
$fontRanges = '0x20-0x7E,0xB0,0xB5,0xD3,0xD7,0xF3,0x104-0x107,0x118-0x119,0x141-0x144,0x15A-0x15B,0x179-0x17C,0x2013,0x2022,0x2026,0x2190,0x2192'
$fontOutputDirectory = Join-Path $repoRoot 'firmware/esp32s3_ui/src/fonts'
New-Item -ItemType Directory -Force -Path $fontOutputDirectory | Out-Null

# Relative paths keep generated command headers independent of the workstation.
Push-Location $repoRoot
try {
    foreach ($fontSize in @(12, 14, 16, 20, 24, 44)) {
        $fontName = "ui_font_$fontSize"
        $fontOutputPath = "firmware/esp32s3_ui/src/fonts/$fontName.c"
        $converterArguments = @(
            'exec', '--yes', '--package=lv_font_conv@1.5.3', '--', 'lv_font_conv',
            '--size', "$fontSize", '--bpp', '4', '--format', 'lvgl',
            '--font', $fontRelativePath, '--range', $fontRanges,
            '--no-compress', '--no-prefilter', '--force-fast-kern-format',
            '--lv-include', 'lvgl.h', '--lv-font-name', $fontName,
            '--output', $fontOutputPath
        )
        & $npmCommand.Source @converterArguments
        if ($LASTEXITCODE -ne 0) {
            throw "lv_font_conv failed for $fontName (exit $LASTEXITCODE)."
        }

        $generatedFont = Get-Content -LiteralPath $fontOutputPath -Raw -Encoding UTF8
        foreach ($fontCodepoint in $fontCodepoints) {
            $glyphMarker = 'U+{0:X4}' -f $fontCodepoint
            if (-not $generatedFont.Contains($glyphMarker)) {
                throw "$fontName is missing requested glyph $glyphMarker."
            }
        }
        if (-not $generatedFont.Contains("const lv_font_t $fontName")) {
            throw "Missing public LVGL font descriptor: $fontName"
        }
        Write-Output "$fontName : $($fontCodepoints.Count) glyphs verified."
    }
} finally {
    Pop-Location
}
