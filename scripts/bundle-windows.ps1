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
        "libgdk_pixbuf*.dll",
        "libglibmm*.dll",
        "libglib-2.0*.dll",
        "libgiomm*.dll",
        "libgio-2.0*.dll",
        "libgobject-2.0*.dll",
        "libgmodule-2.0*.dll",
        "libatkmm*.dll",
        "libatk*.dll",
        "libcairomm*.dll",
        "libcairo*.dll",
        "libpangomm*.dll",
        "libpango*.dll",
        "libsigc*.dll",
        "libstdc++*.dll",
        "libgcc_s*.dll",
        "libwinpthread*.dll",
        "libharfbuzz*.dll",
        "libepoxy*.dll",
        "libfontconfig*.dll",
        "libfreetype*.dll",
        "libfribidi*.dll",
        "libgraphite2*.dll",
        "libbrotli*.dll",
        "libbz2*.dll",
        "libdatrie*.dll",
        "libdeflate*.dll",
        "libexpat*.dll",
        "libffi*.dll",
        "libiconv*.dll",
        "libintl*.dll",
        "libjbig*.dll",
        "libjpeg*.dll",
        "libLerc*.dll",
        "liblzma*.dll",
        "libpcre2*.dll",
        "libpixman*.dll",
        "libpng*.dll",
        "libsharpyuv*.dll",
        "libthai*.dll",
        "libtiff*.dll",
        "libwebp*.dll",
        "libzstd*.dll",
        "librsvg*.dll",
        "libxml2*.dll",
        "zlib*.dll"
    )

    foreach ($pat in $dllPatterns) {
        Get-ChildItem -Path "$Msys2Root\bin" -Filter $pat -ErrorAction SilentlyContinue | ForEach-Object {
            Copy-Item -Path $_.FullName -Destination $OutputDir -Force
        }
    }

    # Copy gdk-pixbuf loaders before dependency resolution so loader plugins are resolved
    if (Test-Path "$Msys2Root\lib\gdk-pixbuf-2.0") {
        New-Item -ItemType Directory -Path "$OutputDir\lib" -Force | Out-Null
        Copy-Item -Path "$Msys2Root\lib\gdk-pixbuf-2.0" -Destination "$OutputDir\lib" -Recurse -Force
    }

    # Dynamic dependency resolution to ensure no transitive DLLs are missed (including plugins)
    $objdump = Join-Path "$Msys2Root\bin" "objdump.exe"
    if (Test-Path $objdump) {
        $resolved = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
        $queue = [System.Collections.Generic.Queue[string]]::new()
        Get-ChildItem -Path $OutputDir -Filter "*.exe" -Recurse | ForEach-Object { $queue.Enqueue($_.FullName) }
        Get-ChildItem -Path $OutputDir -Filter "*.dll" -Recurse | ForEach-Object { $queue.Enqueue($_.FullName) }

        while ($queue.Count -gt 0) {
            $item = $queue.Dequeue()
            $imports = & $objdump -p $item 2>$null | Select-String 'DLL Name: (.*\.dll)' | ForEach-Object { $_.Matches.Groups[1].Value }
            foreach ($dll in $imports) {
                if (-not $resolved.Contains($dll)) {
                    $resolved.Add($dll) | Out-Null
                    $srcDll = Join-Path "$Msys2Root\bin" $dll
                    $destDll = Join-Path $OutputDir $dll
                    if ((Test-Path $srcDll) -and (-not (Test-Path $destDll))) {
                        Copy-Item -Path $srcDll -Destination $OutputDir -Force
                        $queue.Enqueue($destDll)
                    }
                }
            }
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
