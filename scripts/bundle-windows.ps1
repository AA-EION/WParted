# WParted — Windows Standalone Packaging Script
# Copyright (C) 2026 WParted Contributors
# Licensed under GNU GPLv2+

[CmdletBinding()]
param(
    [string]$BuildDir = "build",
    [string]$OutputDir = "dist\WParted-portable",
    [string]$Msys2Root = "C:\msys64\ucrt64"
)

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "  WParted Windows Standalone Distribution Bundler        " -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

# Ensure output directory exists
if (Test-Path $OutputDir) {
    Remove-Item -Path $OutputDir -Recurse -Force
}
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
New-Item -ItemType Directory -Path "$OutputDir\share\glib-2.0\schemas" -Force | Out-Null
New-Item -ItemType Directory -Path "$OutputDir\share\icons" -Force | Out-Null
New-Item -ItemType Directory -Path "$OutputDir\scripts" -Force | Out-Null

# 1. Copy Compiled Executables
$binaries = @(
    "wparted.exe",
    "wparted-apfs-mount.exe",
    "wparted-boot.exe",
    "wparted_tests.exe"
)

foreach ($bin in $binaries) {
    $src = Join-Path $BuildDir $bin
    if (Test-Path $src) {
        Write-Host "[+] Bundling binary: $bin" -ForegroundColor Green
        Copy-Item -Path $src -Destination $OutputDir -Force
    } else {
        Write-Warning "Binary not found in build directory: $src"
    }
}

# 2. Copy Helper Scripts and Setup
Copy-Item -Path "scripts\wparted-wsl-setup.ps1" -Destination "$OutputDir\scripts" -Force
if (Test-Path "resources\wparted.ico") {
    Copy-Item -Path "resources\wparted.ico" -Destination $OutputDir -Force
}
if (Test-Path "README.md") {
    Copy-Item -Path "README.md" -Destination $OutputDir -Force
}
if (Test-Path "COPYING") {
    Copy-Item -Path "COPYING" -Destination "$OutputDir\LICENSE.txt" -Force
}

# 3. Copy MSYS2 / UCRT64 GTKmm and Runtime DLL Dependencies if available
if (Test-Path $Msys2Root) {
    Write-Host "[*] Collecting runtime DLLs from MSYS2 UCRT64..." -ForegroundColor Yellow
    $dllPatterns = @(
        "libgtkmm*.dll",
        "libgtk-3*.dll",
        "libgdkmm*.dll",
        "libgdk-3*.dll",
        "libglibmm*.dll",
        "libglib-2.0*.dll",
        "libgobject-2.0*.dll",
        "libgio-2.0*.dll",
        "libcairo*.dll",
        "libpango*.dll",
        "libharfbuzz*.dll",
        "libepoxy*.dll",
        "libfontconfig*.dll",
        "libfreetype*.dll",
        "libpixman*.dll",
        "libpng*.dll",
        "libintl*.dll",
        "libwinpthread*.dll",
        "zlib*.dll"
    )

    foreach ($pat in $dllPatterns) {
        Get-ChildItem -Path "$Msys2Root\bin" -Filter $pat -ErrorAction SilentlyContinue | ForEach-Object {
            Copy-Item -Path $_.FullName -Destination $OutputDir -Force
        }
    }

    # Compile and copy GSettings schemas
    if (Test-Path "$Msys2Root\share\glib-2.0\schemas") {
        Copy-Item -Path "$Msys2Root\share\glib-2.0\schemas\*" -Destination "$OutputDir\share\glib-2.0\schemas" -Force
        if (Test-Path "$Msys2Root\bin\glib-compile-schemas.exe") {
            & "$Msys2Root\bin\glib-compile-schemas.exe" "$OutputDir\share\glib-2.0\schemas"
        }
    }

    # Copy Icons & Themes
    if (Test-Path "$Msys2Root\share\icons\Adwaita") {
        Copy-Item -Path "$Msys2Root\share\icons\Adwaita" -Destination "$OutputDir\share\icons" -Recurse -Force
    }
}

# 4. Copy Project Icons
if (Test-Path "data\icons") {
    Copy-Item -Path "data\icons" -Destination "$OutputDir\share\icons\wparted" -Recurse -Force
}

# 5. Generate Portable ZIP Archive
$version = "1.0.0"
$zipPath = "dist\WParted-v$version-win64-portable.zip"
Write-Host "[*] Creating portable ZIP archive: $zipPath..." -ForegroundColor Yellow
Compress-Archive -Path "$OutputDir\*" -DestinationPath $zipPath -Force

# Generate SHA256 Hash
$hash = Get-FileHash -Path $zipPath -Algorithm SHA256
"$($hash.Hash)  WParted-v$version-win64-portable.zip" | Out-File -FilePath "dist\SHA256SUMS.txt" -Encoding ascii

Write-Host "[+] Packaging complete!" -ForegroundColor Green
Write-Host "    Portable Zip: $zipPath"
Write-Host "    SHA256:       $($hash.Hash)"
