param(
    [switch]$arm64,      # Build for ARM64 Windows (Snapdragon)
    [switch]$old,        # Build for older x86_64 CPUs (SSE3/SSSE3)
    [switch]$legacy      # Alias for -old
)

# ✅ Determine architecture and set appropriate toolchain
if ($arm64) {
    $toolchain = "clangarm64"
    $archName = "ARM64"
    $archColor = "Magenta"
    Write-Host "🚀 Building for ARM64 Windows (Snapdragon)" -ForegroundColor $archColor
} else {
    $toolchain = "clang64"
    $archName = "x86_64"
    $archColor = "Cyan"
    Write-Host "🚀 Building for x86_64 Windows" -ForegroundColor $archColor
}

# ✅ Detect MSYS2 installation path
# Check if MSYS2 path is set by environment variable (GitHub Actions)
$possiblePaths = @()
if ($env:MSYS) {
    $possiblePaths += $env:MSYS
}
$possiblePaths += @(
    "C:\msys64",
    "$env:RUNNER_TEMP\..\msys64",
    "D:\a\_temp\msys64",
    "$env:SystemDrive\msys64"
)

$msys2Root = $null
foreach ($path in $possiblePaths) {
    if (Test-Path "$path\$toolchain\bin") {
        $msys2Root = $path
        Write-Host "✅ Found MSYS2 at: $msys2Root" -ForegroundColor Green
        break
    }
}

if (-not $msys2Root) {
    Write-Host "❌ Could not find MSYS2 installation with $toolchain toolchain. Checked:" -ForegroundColor Red
    foreach ($path in $possiblePaths) {
        Write-Host "  - $path\$toolchain\bin" -ForegroundColor Yellow
    }
    Write-Host "Please ensure MSYS2 is installed with the $toolchain environment" -ForegroundColor Red
    exit 1
}

$toolchainPath = "$msys2Root\$toolchain"

if (Test-Path ".\build" -PathType Container) {
    rm -r build
    mkdir build
} else {
    mkdir build
}

# ✅ Compile .ts → .qm files
$lreleaseExe = "$toolchainPath\bin\lrelease-qt6.exe"
if (Test-Path $lreleaseExe) {
    Write-Host "Compiling translation files..." -ForegroundColor Cyan
    & $lreleaseExe ./resources/translations/app_zh.ts ./resources/translations/app_fr.ts ./resources/translations/app_es.ts
    Copy-Item -Path ".\resources\translations\*.qm" -Destination ".\build" -Force
} else {
    Write-Host "⚠️  Warning: lrelease-qt6.exe not found at $lreleaseExe" -ForegroundColor Yellow
    Write-Host "   Skipping translation compilation. Install mingw-w64-$toolchain-qt6-tools if needed." -ForegroundColor Yellow
}

cd .\build

# ✅ Set PATH to use the selected toolchain (critical for DLLs and compiler detection)
$env:PATH = "$toolchainPath\bin;$env:PATH"

# ✅ Prepare CMake configuration
$cmakeArgs = @(
    "-G", "MinGW Makefiles",
    "-DCMAKE_C_COMPILER=$toolchainPath/bin/clang.exe",
    "-DCMAKE_CXX_COMPILER=$toolchainPath/bin/clang++.exe",
    "-DCMAKE_MAKE_PROGRAM=$toolchainPath/bin/mingw32-make.exe",
    "-DCMAKE_BUILD_TYPE=Release"
)

# Architecture-specific configuration
if ($arm64) {
    # ARM64 build - set processor type
    $cmakeArgs += "-DCMAKE_SYSTEM_PROCESSOR=arm64"
    Write-Host "Target: ARM64 (Cortex-A75/Snapdragon optimized)" -ForegroundColor $archColor
} else {
    # x86_64 build - determine CPU architecture target
    $cpuArch = "modern"
    if ($old -or $legacy) {
        $cpuArch = "old"
        Write-Host "Target: Older x86_64 CPUs (SSE3/SSSE3 compatible - Core 2 Duo era)" -ForegroundColor Yellow
    } else {
        Write-Host "Target: Modern x86_64 CPUs (SSE4.2 compatible - Core i series)" -ForegroundColor Green
    }
    $cmakeArgs += "-DCPU_ARCH=$cpuArch"
}

# ✅ Configure and build
& "$toolchainPath\bin\cmake.exe" @cmakeArgs ..
& "$toolchainPath\bin\cmake.exe" --build . --config Release -- -j16

# ✅ Deploy Qt runtime
& "$toolchainPath\bin\windeployqt6.exe" "NoteApp.exe"

# ✅ Copy required DLLs (for Poppler and SDL2 dependencies)
Write-Host "Copying required DLLs from $toolchainPath\bin..." -ForegroundColor Cyan

$requiredDlls = @(
    "libdeflate.dll", "libiconv-2.dll", "libnettle-8.dll", "libreadline8.dll", "Qt6Core.dll", "libdouble-conversion.dll", "libicudt78.dll", "libnghttp2-14.dll", "librhash.dll", "Qt6Gui.dll", "libexpat-1.dll", "libicuin78.dll", "libnghttp3-9.dll", "libsharpyuv-0.dll", "Qt6Network.dll", "libffi-8.dll", "libicuio78.dll", "libngtcp2-16.dll", "libssh2-1.dll", "Qt6Widgets.dll", "libfontconfig-1.dll", "libicutest78.dll", "libngtcp2_crypto_gnutls-8.dll", "libssl-3.dll", "libssl-3-x64.dll", "SDL2.dll", "libformw6.dll", "libicutu78.dll", "libngtcp2_crypto_ossl-0.dll", "libsystre-0.dll", "libLTO.dll", "libfreetype-6.dll", "libicuuc78.dll", "libngtcp2_crypto_ossl.dll", "libtasn1-6.dll", "libLerc.dll", "libgif-7.dll", "libidn2-0.dll", "libnspr4.dll", "libtermcap-0.dll", "libRemarks.dll", "libgio-2.0-0.dll", "libintl-8.dll", "libomp.dll", "libtiff-6.dll", "libarchive-13.dll", "libgirepository-2.0-0.dll", "libjbig-0.dll", "libopenjp2-7.dll", "libtiffxx-6.dll", "libasprintf-0.dll", "libglib-2.0-0.dll", "libjpeg-8.dll", "libopenjpip-7.dll", "libtre-5.dll", "libb2-1.dll", "libgmodule-2.0-0.dll", "libjsoncpp-26.dll", "libp11-kit-0.dll", "libturbojpeg.dll", "libbrotlicommon.dll", "libgmp-10.dll", "liblcms2-2.dll", "libpanelw6.dll", "libunistring-5.dll", "libbrotlidec.dll", "libgmpxx-4.dll", "liblcms2_fast_float-2.dll", "libpcre2-16-0.dll", "libunwind.dll", "libbrotlienc.dll", "libgnutls-30.dll", "liblldb.dll", "libpcre2-32-0.dll", "libuv-1.dll", "libbz2-1.dll", "libgnutls-openssl-27.dll", "liblz4.dll", "libpcre2-8-0.dll", "libwebp-7.dll", "libc++.dll", "libgnutlsxx-30.dll", "liblzma-5.dll", "libpcre2-posix-3.dll", "libwebpdecoder-3.dll", "libcairo-2.dll", "libgobject-2.0-0.dll", "liblzo2-2.dll", "libpixman-1-0.dll", "libwebpdemux-2.dll", "libcairo-gobject-2.dll", "libgraphite2.dll", "libmd4c-html.dll", "libpkgconf-7.dll", "libwebpmux-3.dll", "libcairo-script-interpreter-2.dll", "libgthread-2.0-0.dll", "libmd4c.dll", "libplc4.dll", "libwinpthread-1.dll", "libcares-2.dll", "libharfbuzz-0.dll", "libmenuw6.dll", "libplds4.dll", "libzstd.dll", "libcharset-1.dll", "libharfbuzz-gobject-0.dll", "libmpdec++-4.dll", "libpng16-16.dll", "nss3.dll", "libcrypto-3.dll", "libcrypto-3-x64.dll", "libharfbuzz-subset-0.dll", "libmpdec-4.dll", "nssutil3.dll", "libcurl-4.dll", "libhistory8.dll", "libncurses++w6.dll", "libpoppler-qt6-3.dll", "smime3.dll", "libdbus-1-3.dll", "libhogweed-6.dll", "libncursesw6.dll", "libpsl-5.dll", "zlib1.dll"
)

$sourceDir = "$toolchainPath\bin"
$copiedCount = 0

# ✅ Copy libpoppler-*.dll dynamically (handles any version like 152, 153, etc.)
$popplerDlls = Get-ChildItem -Path "$sourceDir\libpoppler-*.dll" -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '^libpoppler-\d{3}\.dll$' }
foreach ($popplerDll in $popplerDlls) {
    $destPath = $popplerDll.Name
    if (-not (Test-Path $destPath)) {
        Copy-Item -Path $popplerDll.FullName -Destination $destPath -Force
        $copiedCount++
        Write-Host "  Found and copied: $($popplerDll.Name)" -ForegroundColor Gray
    }
}

# ✅ Copy other required DLLs
$missingDlls = @()
foreach ($dll in $requiredDlls) {
    $sourcePath = Join-Path $sourceDir $dll
    $destPath = $dll  # We're already in the build directory

    if (Test-Path $sourcePath) {
        if (-not (Test-Path $destPath)) {
            Copy-Item -Path $sourcePath -Destination $destPath -Force
            $copiedCount++
        }
    } else {
        $missingDlls += $dll
    }
}

Write-Host "Copied $copiedCount DLL(s) from $toolchain\bin" -ForegroundColor Green

# ✅ Warn about missing DLLs
if ($missingDlls.Count -gt 0) {
    Write-Host "⚠️  Warning: The following DLLs from the required list were not found and could not be copied:" -ForegroundColor Yellow
    foreach ($missingDll in $missingDlls) {
        Write-Host "  - $missingDll" -ForegroundColor Red
    }
    Write-Host "   This may indicate version changes or missing dependencies. Please verify the DLL names." -ForegroundColor Yellow
}

# Copy share folder
Copy-Item -Path "$toolchainPath\share\poppler" -Destination "..\build\share\poppler" -Recurse -Force

# ✅ Copy pdftk for optimized PDF export (if available in MSYS2)
$pdftkExe = "$toolchainPath\bin\pdftk.exe"

if (Test-Path $pdftkExe) {
    Copy-Item -Path $pdftkExe -Destination "pdftk.exe" -Force
    Write-Host "✅ Copied pdftk.exe from MSYS2 for optimized PDF export" -ForegroundColor Green
} else {
    Write-Host "ℹ️  pdftk.exe not found in MSYS2 - checking system PATH..." -ForegroundColor Cyan
    
    # Check if pdftk is available in system PATH (e.g., installed from pdftk.com)
    try {
        $pdftkCheck = Get-Command pdftk -ErrorAction Stop
        Write-Host "✅ Found pdftk in system PATH: $($pdftkCheck.Source)" -ForegroundColor Green
    } catch {
        Write-Host "⚠️  pdftk not found - PDF export will use slower fallback method" -ForegroundColor Yellow
        Write-Host "   Option 1: pacman -S mingw-w64-$toolchain-pdftk" -ForegroundColor Yellow
        Write-Host "   Option 2: Download from https://www.pdflabs.com/tools/pdftk-the-pdf-toolkit/" -ForegroundColor Yellow
    }
}

./NoteApp.exe
cd ../