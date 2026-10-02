param(
    [ValidatePattern('^COM[0-9]+$')]
    [string]$Port = 'COM7'
)

$maxRetries = 6
for ($i = 1; $i -le $maxRetries; $i++) {
    Write-Host "========================================================" -ForegroundColor Cyan
    Write-Host " [Proba $i/$maxRetries] Trwa laczenie z ESP32 ($Port, ST7789)..." -ForegroundColor Yellow
    Write-Host " PRZYTRZYMAJ PRZYCISK 'BOOT' NA PLYTCE CYD!" -ForegroundColor Magenta
    Write-Host "========================================================" -ForegroundColor Cyan
    
    pio run --project-dir "$PSScriptRoot\.." -e esp32dev-st7789 -t upload --upload-port $Port
    if ($LASTEXITCODE -eq 0) {
        Write-Host "========================================================" -ForegroundColor Green
        Write-Host " SUKCES: Oprogramowanie wgrane pomyslnie na plytke CYD! " -ForegroundColor Green
        Write-Host "========================================================" -ForegroundColor Green
        exit 0
    }
    Start-Sleep -Seconds 2
}
Write-Host "Nie udalo sie nawiazac polaczenia w zadanym czasie." -ForegroundColor Red
exit 1
