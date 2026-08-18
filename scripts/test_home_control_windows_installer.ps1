[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$InstallerPath,
    [string]$InstallDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'The Home Control Windows installer smoke test requires Windows.'
}

$resolvedInstaller = (Resolve-Path -LiteralPath $InstallerPath -ErrorAction Stop).Path
if (-not (Test-Path -LiteralPath $resolvedInstaller -PathType Leaf)) {
    throw "Windows Setup is absent: $resolvedInstaller"
}
if (-not $InstallDirectory) {
    $InstallDirectory = Join-Path (
        [System.IO.Path]::GetTempPath()
    ) "Home Control smoke $([Guid]::NewGuid().ToString('N'))"
}
$resolvedInstallDirectory = [System.IO.Path]::GetFullPath($InstallDirectory)
if (Test-Path -LiteralPath $resolvedInstallDirectory) {
    throw "Smoke-test install directory already exists: $resolvedInstallDirectory"
}

function Invoke-CheckedHiddenProcess {
    param(
        [Parameter(Mandatory)]
        [string]$Executable,
        [Parameter(Mandatory)]
        [string[]]$Arguments,
        [Parameter(Mandatory)]
        [string]$Operation
    )

    $process = Start-Process `
        -FilePath $Executable `
        -ArgumentList $Arguments `
        -WindowStyle Hidden `
        -Wait `
        -PassThru
    if ($process.ExitCode -ne 0) {
        throw "$Operation failed with exit code $($process.ExitCode)."
    }
}

$application = Join-Path $resolvedInstallDirectory 'HomeControl.exe'
$uninstaller = Join-Path $resolvedInstallDirectory 'unins000.exe'
$installationIsPresent = $false
$applicationProcess = $null
try {
    Invoke-CheckedHiddenProcess `
        -Executable $resolvedInstaller `
        -Arguments @(
            '/VERYSILENT',
            '/SUPPRESSMSGBOXES',
            '/NORESTART',
            '/SP-',
            "/DIR=`"$resolvedInstallDirectory`""
        ) `
        -Operation 'Setup'
    $installationIsPresent = $true

    if (-not (Test-Path -LiteralPath $application -PathType Leaf)) {
        throw 'Installed HomeControl.exe is absent.'
    }
    if (-not (Test-Path -LiteralPath $uninstaller -PathType Leaf)) {
        throw 'Windows uninstaller is absent.'
    }

    $applicationProcess = Start-Process `
        -FilePath $application `
        -WorkingDirectory $resolvedInstallDirectory `
        -WindowStyle Hidden `
        -PassThru
    Start-Sleep -Seconds 5
    $applicationProcess.Refresh()
    if ($applicationProcess.HasExited) {
        throw "Installed HomeControl.exe exited during startup with code $($applicationProcess.ExitCode)."
    }
    Stop-Process -Id $applicationProcess.Id -Force
    Wait-Process -Id $applicationProcess.Id -ErrorAction SilentlyContinue
    $applicationProcess = $null

    Invoke-CheckedHiddenProcess `
        -Executable $uninstaller `
        -Arguments @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART') `
        -Operation 'Uninstaller'
    $installationIsPresent = $false

    if (Test-Path -LiteralPath $application) {
        throw 'HomeControl.exe remained after uninstall.'
    }
    if (Test-Path -LiteralPath $uninstaller) {
        throw 'Windows uninstaller remained after uninstall.'
    }

    Write-Output "Windows installer smoke test passed: $resolvedInstaller"
}
finally {
    if ($applicationProcess -and -not $applicationProcess.HasExited) {
        Stop-Process -Id $applicationProcess.Id -Force -ErrorAction SilentlyContinue
        Wait-Process -Id $applicationProcess.Id -ErrorAction SilentlyContinue
    }
    if ($installationIsPresent -and (Test-Path -LiteralPath $uninstaller -PathType Leaf)) {
        try {
            Invoke-CheckedHiddenProcess `
                -Executable $uninstaller `
                -Arguments @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART') `
                -Operation 'Cleanup uninstaller'
        }
        catch {
            Write-Warning "Smoke-test cleanup failed: $($_.Exception.Message)"
        }
    }
}
