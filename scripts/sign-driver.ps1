param(
    [Parameter(Mandatory = $true)]
    [string]$DriverPath,

    [Parameter(Mandatory = $true)]
    [string]$CertificateOutputPath,

    [Parameter(Mandatory = $true)]
    [string]$SignToolPath,

    [Parameter(Mandatory = $true)]
    [string]$CertificateThumbprint
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $DriverPath)) {
    throw "Driver file not found: $DriverPath"
}

if (-not (Test-Path $SignToolPath)) {
    throw "signtool.exe not found: $SignToolPath"
}

$normalizedThumbprint = ($CertificateThumbprint -replace "\s", "").ToUpperInvariant()

function Get-CertificateByThumbprint {
    param(
        [Parameter(Mandatory = $true)]
        [string]$StoreLocationName,

        [Parameter(Mandatory = $true)]
        [string]$Thumbprint
    )

    $store = New-Object System.Security.Cryptography.X509Certificates.X509Store("My", $StoreLocationName)
    $store.Open([System.Security.Cryptography.X509Certificates.OpenFlags]::ReadOnly)

    try {
        return $store.Certificates.Find(
            [System.Security.Cryptography.X509Certificates.X509FindType]::FindByThumbprint,
            $Thumbprint,
            $false
        ) | Select-Object -First 1
    }
    finally {
        $store.Close()
    }
}

$cert = Get-CertificateByThumbprint -StoreLocationName "CurrentUser" -Thumbprint $normalizedThumbprint
if (-not $cert) {
    $cert = Get-CertificateByThumbprint -StoreLocationName "LocalMachine" -Thumbprint $normalizedThumbprint
}

if (-not $cert) {
    throw "Signing certificate not found in CurrentUser\\My or LocalMachine\\My: $normalizedThumbprint"
}

$certDir = Split-Path -Parent $CertificateOutputPath
if (-not (Test-Path $certDir)) {
    New-Item -ItemType Directory -Path $certDir | Out-Null
}

[System.IO.File]::WriteAllBytes(
    $CertificateOutputPath,
    $cert.Export([System.Security.Cryptography.X509Certificates.X509ContentType]::Cert)
)

Write-Host "Signing driver with certificate $($cert.Subject) [$($cert.Thumbprint)]" -ForegroundColor Cyan
& $SignToolPath sign /fd SHA256 /sha1 $cert.Thumbprint /v $DriverPath
if ($LASTEXITCODE -ne 0) {
    throw "signtool sign failed with exit code $LASTEXITCODE"
}

Write-Host "Driver signing completed." -ForegroundColor Green
