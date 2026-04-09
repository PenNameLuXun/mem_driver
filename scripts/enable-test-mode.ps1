$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$releaseDir = Split-Path -Parent $scriptDir
$certPath = Join-Path $releaseDir "MemAttribTest.cer"

if (-not (Test-Path $certPath)) {
    throw "Certificate file not found: $certPath"
}

Write-Host "[1/3] Enabling test-signing mode..." -ForegroundColor Cyan
Start-Process -FilePath "$env:SystemRoot\System32\bcdedit.exe" `
    -ArgumentList "/set testsigning on" `
    -Verb RunAs `
    -Wait

Write-Host "[2/3] Importing certificate into Root store..." -ForegroundColor Cyan
Start-Process -FilePath "certutil.exe" `
    -ArgumentList "-addstore Root `"$certPath`"" `
    -Verb RunAs `
    -Wait

Write-Host "[3/3] Importing certificate into TrustedPublisher store..." -ForegroundColor Cyan
Start-Process -FilePath "certutil.exe" `
    -ArgumentList "-addstore TrustedPublisher `"$certPath`"" `
    -Verb RunAs `
    -Wait

Write-Host ""
Write-Host "Test mode preparation finished." -ForegroundColor Green
Write-Host "A reboot is required before starting the driver."
