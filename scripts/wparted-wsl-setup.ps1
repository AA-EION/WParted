# WParted — WSL2 Engine Setup & Distro Provisioning Script
# Copyright (C) 2026 WParted Contributors
# Licensed under GNU GPLv2+

[CmdletBinding()]
param(
    [string]$DistroName = "wparted-wsl-engine",
    [string]$InstallPath = "$env:LOCALAPPDATA\WParted\wsl-engine",
    [string]$RootfsTar = "wparted-wsl-engine.tar.gz"
)

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "  WParted WSL2 Linux Filesystem Bridge Engine Setup      " -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

# Check WSL2 status
Write-Host "[*] Checking WSL2 status..." -ForegroundColor Yellow
$wslStatus = wsl.exe --status 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Warning "WSL is not enabled or needs initialization. Running 'wsl --install --no-distribution'..."
    Start-Process -FilePath "wsl.exe" -ArgumentList "--install --no-distribution" -Wait -Verb RunAs
}

# Check if distro already imported
$installedDistros = wsl.exe -l -q 2>&1 | Out-String
if ($installedDistros -match $DistroName) {
    Write-Host "[+] WParted WSL helper distro '$DistroName' is already installed." -ForegroundColor Green
    exit 0
}

# Create installation directory
if (-not (Test-Path $InstallPath)) {
    New-Item -ItemType Directory -Path $InstallPath -Force | Out-Null
}

# Check for rootfs tarball or generate lightweight Alpine Linux rootfs
if (-not (Test-Path $RootfsTar)) {
    Write-Host "[*] Downloading official lightweight Alpine rootfs for WParted engine..." -ForegroundColor Yellow
    $alpineUrl = "https://dl-cdn.alpinelinux.org/alpine/v3.20/releases/x86_64/alpine-minirootfs-3.20.0-x86_64.tar.gz"
    Invoke-WebRequest -Uri $alpineUrl -OutFile $RootfsTar -UseBasicParsing
}

Write-Host "[*] Importing '$DistroName' into WSL2..." -ForegroundColor Yellow
wsl.exe --import $DistroName $InstallPath $RootfsTar --version 2

if ($LASTEXITCODE -eq 0) {
    Write-Host "[*] Installing required Linux storage management utilities inside distro..." -ForegroundColor Yellow
    # Install e2fsprogs, btrfs-progs, xfsprogs, f2fs-tools, dosfstools, parted, smartmontools, cryptsetup, lvm2
    $installCmd = "apk update && apk add --no-cache e2fsprogs btrfs-progs xfsprogs f2fs-tools dosfstools parted smartmontools cryptsetup lvm2 util-linux"
    wsl.exe -d $DistroName -u root --exec sh -c $installCmd

    Write-Host "[+] WParted WSL2 engine provisioned and ready!" -ForegroundColor Green
} else {
    Write-Error "Failed to import WSL distro."
    exit 1
}
