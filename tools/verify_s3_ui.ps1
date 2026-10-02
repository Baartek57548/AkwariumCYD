[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$uiRepo=[System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$uiPreview=Join-Path $uiRepo 'firmware/esp32s3_ui/preview'
$uiGallery=Join-Path $uiRepo 'docs/ui/przekroj'
Push-Location $uiPreview
try {
    & python -m platformio run
    if($LASTEXITCODE -ne 0){throw 'Native LVGL build failed.'}
    New-Item -ItemType Directory -Path 'output',$uiGallery -Force | Out-Null
    foreach($uiCase in @('ux-v11','edge-v9','animation-v11','navigation-v9','header-v9','settings-v9','lifecycle-v11','system-v9','plan-v9','history-v11','feeding-v11','additional-dose-v1','manual-feeder-v1','palette-v9','themes-v2','system-refinement-v9','refinement-v11','light-solar-v12','soak','swipe-soak','premium-main')) {
        $uiOut=if($uiCase -eq 'details'){'output/details'}else{'output/final'}
        & './.pio/build/lvgl_preview/program.exe' --script "$uiCase.txt" --out $uiOut |
            Tee-Object -FilePath "output/$uiCase.log"
        if($LASTEXITCODE -ne 0){throw "UI scenario $uiCase failed."}
    }
    foreach($uiVariant in 1..3) {
        & './.pio/build/lvgl_preview/program.exe' --explore $uiVariant --out "output/exploration-$uiVariant"
        if($LASTEXITCODE -ne 0){throw "Visual exploration $uiVariant failed."}
    }
    Add-Type -AssemblyName System.Drawing
    $uiSources=@('output/final','output/details')
    foreach($uiSource in $uiSources) {
        foreach($uiBmp in Get-ChildItem -LiteralPath $uiSource -Filter '*.bmp' -File) {
            if($uiBmp.BaseName -eq 'failure'){continue}
            $uiRaster=[System.Drawing.Image]::FromFile($uiBmp.FullName)
            try {$uiRaster.Save((Join-Path $uiGallery "$($uiBmp.BaseName).png"),[System.Drawing.Imaging.ImageFormat]::Png)}
            finally {$uiRaster.Dispose()}
        }
    }
    foreach($uiVariant in 1..3) {
        $uiRaster=[System.Drawing.Image]::FromFile((Join-Path $uiPreview "output/exploration-$uiVariant/home.bmp"))
        try {$uiRaster.Save((Join-Path $uiGallery "exploration-$uiVariant.png"),[System.Drawing.Imaging.ImageFormat]::Png)}
        finally {$uiRaster.Dispose()}
    }
    Write-Output "Verified LVGL images exported to $uiGallery"
} finally {Pop-Location}
