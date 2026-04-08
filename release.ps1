$ErrorActionPreference = "Stop"

param(
    [string]$BuildDir = "build",
    [string]$Configuration = "Release"
)

Write-Host "[1/2] Configuring top-level CMake project..." -ForegroundColor Cyan
cmake -S . -B $BuildDir
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed."
}

Write-Host "[2/2] Building publish target..." -ForegroundColor Cyan
cmake --build $BuildDir --config $Configuration --target publish
if ($LASTEXITCODE -ne 0) {
    throw "CMake publish build failed."
}

Write-Host ""
Write-Host "Release finished." -ForegroundColor Green
Write-Host "Bundle directory: $BuildDir\\release"
Write-Host "Zip package: $BuildDir\\MemAttrib-$Configuration.zip"
