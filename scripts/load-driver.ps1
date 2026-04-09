$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$releaseDir = Split-Path -Parent $scriptDir
$driverPath = Join-Path $releaseDir "MemAttribDriver.sys"
$serviceName = "MemAttrib"

if (-not (Test-Path $driverPath)) {
    throw "Driver file not found: $driverPath"
}

$existing = sc.exe query $serviceName 2>$null
if ($LASTEXITCODE -ne 0) {
    Write-Host "[1/3] Creating driver service..." -ForegroundColor Cyan
    Start-Process -FilePath "sc.exe" `
        -ArgumentList "create $serviceName type= kernel start= demand binPath= `"$driverPath`"" `
        -Verb RunAs `
        -Wait
} else {
    Write-Host "[1/3] Driver service already exists." -ForegroundColor Yellow
}

Write-Host "[2/3] Starting driver service..." -ForegroundColor Cyan
Start-Process -FilePath "sc.exe" `
    -ArgumentList "start $serviceName" `
    -Verb RunAs `
    -Wait

Write-Host "[3/3] Querying driver service status..." -ForegroundColor Cyan
sc.exe query $serviceName

Write-Host ""
Write-Host "If STATE is RUNNING, the driver is loaded." -ForegroundColor Green
