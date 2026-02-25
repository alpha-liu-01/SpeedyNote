param(
    [switch]$arm64,      # Build for ARM64 Windows (Snapdragon)
    [switch]$old,        # Build for older x86_64 CPUs (SSE3/SSSE3)
    [switch]$legacy,     # Alias for -old
    [switch]$qt5,        # Build with Qt5 instead of Qt6
    [switch]$win32,      # Build for 32-bit x86 Windows (requires -qt5)
    [switch]$debug,      # Enable verbose debug output (qDebug)
    [switch]$norun       # Don't run the application after building (for CI/remote builds)
)

# ✅ Validate flags
if ($win32 -and -not $qt5) {
    Write-Host "❌ -win32 requires -qt5 (Qt6 does not support 32-bit Windows)" -ForegroundColor Red
    exit 1
}

# ✅ Determine architecture and set appropriate toolchain
if ($arm64) {
    $toolchain = "clangarm64"
    $archName = "ARM64"
    $archColor = "Magenta"
    Write-Host "🚀 Building for ARM64 Windows (Snapdragon)" -ForegroundColor $archColor
} elseif ($win32) {
    $toolchain = "mingw32"
    $archName = "x86 (32-bit)"
    $archColor = "Yellow"
    Write-Host "🚀 Building for 32-bit x86 Windows (Qt5)" -ForegroundColor $archColor
} else {
    $toolchain = "clang64"
    $archName = "x86_64"
    $archColor = "Cyan"
    Write-Host "🚀 Building for x86_64 Windows" -ForegroundColor $archColor
}

if ($qt5) {
    Write-Host "   Qt version: Qt5" -ForegroundColor Gray
} else {
    Write-Host "   Qt version: Qt6" -ForegroundColor Gray
}

if ($win32) {
    # ✅ Standalone Qt 5.15.2 SDK (MSYS2 does not offer 32-bit packages)
    $qtSdkPath = "C:\Qt\5.15.2\mingw81_32"
    $mingwToolsPath = "C:\Qt\Tools\mingw810_32"

    if (-not (Test-Path "$qtSdkPath\bin")) {
        Write-Host "❌ Qt 5.15.2 mingw81_32 not found at: $qtSdkPath" -ForegroundColor Red
        Write-Host "   Install Qt 5.15.2 with the MinGW 8.1 32-bit component via Qt Maintenance Tool" -ForegroundColor Yellow
        exit 1
    }
    if (-not (Test-Path "$mingwToolsPath\bin\gcc.exe")) {
        Write-Host "❌ MinGW 8.1 32-bit tools not found at: $mingwToolsPath" -ForegroundColor Red
        Write-Host "   Install MinGW 8.1 32-bit from Qt Maintenance Tool (Developer and Designer Tools)" -ForegroundColor Yellow
        exit 1
    }

    Write-Host "✅ Found Qt 5.15.2 at: $qtSdkPath" -ForegroundColor Green
    Write-Host "✅ Found MinGW 8.1 at: $mingwToolsPath" -ForegroundColor Green

    $toolchainPath = $mingwToolsPath
    $qtBinPath = "$qtSdkPath\bin"

    # Find cmake (Qt Tools bundle or system PATH)
    $cmakeExe = $null
    foreach ($p in @("C:\Qt\Tools\CMake_64\bin\cmake.exe", "C:\Qt\Tools\cmake\bin\cmake.exe")) {
        if (Test-Path $p) { $cmakeExe = $p; break }
    }
    if (-not $cmakeExe) {
        $cmakeCmd = Get-Command cmake.exe -ErrorAction SilentlyContinue
        if ($cmakeCmd) { $cmakeExe = $cmakeCmd.Source }
    }
    if (-not $cmakeExe) {
        Write-Host "❌ cmake not found. Install CMake or add Qt Tools CMake to PATH." -ForegroundColor Red
        exit 1
    }
    Write-Host "   CMake: $cmakeExe" -ForegroundColor Gray
} else {
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
    $qtBinPath = "$toolchainPath\bin"
    $cmakeExe = "$toolchainPath\bin\cmake.exe"
}

# Clean and recreate build folder
if (Test-Path ".\build" -PathType Container) {
    # Kill any running speedynote instances that might lock files
    $noteAppProcesses = Get-Process -Name "speedynote" -ErrorAction SilentlyContinue
    if ($noteAppProcesses) {
        Write-Host "Stopping running speedynote instances..." -ForegroundColor Yellow
        $noteAppProcesses | Stop-Process -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 500
    }
    
    # Try to remove the build folder
    Write-Host "Cleaning build folder..." -ForegroundColor Gray
    Remove-Item -Path ".\build" -Recurse -Force -ErrorAction SilentlyContinue
    
    # If it still exists, try again with a delay
    if (Test-Path ".\build" -PathType Container) {
        Start-Sleep -Seconds 1
        Remove-Item -Path ".\build" -Recurse -Force -ErrorAction SilentlyContinue
    }
    
    # If it STILL exists, at minimum delete CMake cache files to avoid stale config
    if (Test-Path ".\build" -PathType Container) {
        Write-Host "⚠️  Could not fully clean build folder - cleaning CMake cache..." -ForegroundColor Yellow
        # These files MUST be deleted for a clean CMake configuration
        Remove-Item -Path ".\build\CMakeCache.txt" -Force -ErrorAction SilentlyContinue
        Remove-Item -Path ".\build\CMakeFiles" -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -Path ".\build\cmake_install.cmake" -Force -ErrorAction SilentlyContinue
        Remove-Item -Path ".\build\Makefile" -Force -ErrorAction SilentlyContinue
        Remove-Item -Path ".\build\.cmake" -Recurse -Force -ErrorAction SilentlyContinue
        
        # Verify CMake cache is gone
        if (Test-Path ".\build\CMakeCache.txt") {
            Write-Host "❌ FATAL: Cannot delete CMakeCache.txt - please close any programs using the build folder" -ForegroundColor Red
            Write-Host "   Try: Close File Explorer, IDE, or restart the computer" -ForegroundColor Yellow
            exit 1
        }
        Write-Host "   CMake cache cleaned, continuing with partial rebuild..." -ForegroundColor Yellow
    } else {
        New-Item -ItemType Directory -Path ".\build" | Out-Null
    }
} else {
    New-Item -ItemType Directory -Path ".\build" | Out-Null
}

# ✅ Compile .ts → .qm files
$lreleaseExe = $null
if ($qt5) {
    if (Test-Path "$qtBinPath\lrelease.exe") {
        $lreleaseExe = "$qtBinPath\lrelease.exe"
    }
} else {
    if (Test-Path "$qtBinPath\lrelease-qt6.exe") {
        $lreleaseExe = "$qtBinPath\lrelease-qt6.exe"
    } elseif (Test-Path "$qtBinPath\lrelease.exe") {
        $lreleaseExe = "$qtBinPath\lrelease.exe"
    }
}

if ($lreleaseExe) {
    Write-Host "Compiling translation files using $lreleaseExe..." -ForegroundColor Cyan
    & $lreleaseExe ./resources/translations/app_zh.ts ./resources/translations/app_fr.ts ./resources/translations/app_es.ts
    Copy-Item -Path ".\resources\translations\*.qm" -Destination ".\build" -Force
} else {
    $qtVer = if ($qt5) { "qt5" } else { "qt6" }
    Write-Host "⚠️  Warning: lrelease not found in $qtBinPath" -ForegroundColor Yellow
    Write-Host "   Skipping translation compilation. Install $qtVer-tools if needed." -ForegroundColor Yellow
}

cd .\build

# ✅ Set PATH to use the selected toolchain (critical for DLLs and compiler detection)
if ($win32) {
    $env:PATH = "$qtBinPath;$toolchainPath\bin;$env:PATH"
} else {
    $env:PATH = "$toolchainPath\bin;$env:PATH"
}

# ✅ Detect compiler (Clang preferred, GCC fallback for mingw32)
if (Test-Path "$toolchainPath\bin\clang.exe") {
    $cCompiler = "$toolchainPath/bin/clang.exe"
    $cxxCompiler = "$toolchainPath/bin/clang++.exe"
    Write-Host "   Compiler: Clang" -ForegroundColor Gray
} else {
    $cCompiler = "$toolchainPath/bin/gcc.exe"
    $cxxCompiler = "$toolchainPath/bin/g++.exe"
    Write-Host "   Compiler: GCC" -ForegroundColor Gray
}

# ✅ Prepare CMake configuration
$cmakeArgs = @(
    "-G", "MinGW Makefiles",
    "-DCMAKE_C_COMPILER=$cCompiler",
    "-DCMAKE_CXX_COMPILER=$cxxCompiler",
    "-DCMAKE_MAKE_PROGRAM=$toolchainPath/bin/mingw32-make.exe",
    "-DCMAKE_BUILD_TYPE=Release"
)

if ($qt5) {
    $cmakeArgs += "-DUSE_QT5=ON"
}
if ($win32) {
    $qtPathCmake = $qtSdkPath -replace '\\', '/'
    $cmakeArgs += "-DQT_PATH=$qtPathCmake"
}

# Architecture-specific configuration
if ($arm64) {
    # ARM64 build - set processor type
    $cmakeArgs += "-DCMAKE_SYSTEM_PROCESSOR=arm64"
    Write-Host "Target: ARM64 (Cortex-A75/Snapdragon optimized)" -ForegroundColor $archColor
} elseif ($win32) {
    Write-Host "Target: x86 (32-bit) Windows" -ForegroundColor $archColor
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

if ($debug) {
    $cmakeArgs += "-DENABLE_DEBUG_OUTPUT=ON"
    Write-Host "Debug Output: ENABLED" -ForegroundColor Yellow
} else {
    $cmakeArgs += "-DENABLE_DEBUG_OUTPUT=OFF"
    Write-Host "Debug Output: DISABLED" -ForegroundColor Gray
}

# ✅ Configure and build
& $cmakeExe @cmakeArgs ..

# Determine number of parallel jobs based on architecture
# ARM64 devices often have limited memory/thermal headroom, so use half the cores
$cpuCount = [Environment]::ProcessorCount
if ($arm64) {
    $jobs = [Math]::Max(1, [Math]::Floor($cpuCount / 2))
    Write-Host "Using $jobs parallel jobs (ARM64: half of $cpuCount cores)" -ForegroundColor Gray
} else {
    $jobs = $cpuCount
    Write-Host "Using $jobs parallel jobs ($archName`: all $cpuCount cores)" -ForegroundColor Gray
}

& $cmakeExe --build . --config Release --parallel $jobs

# ✅ Deploy Qt runtime
if ($qt5) {
    & "$qtBinPath\windeployqt.exe" "speedynote.exe"
} else {
    & "$qtBinPath\windeployqt6.exe" "speedynote.exe"
}

$copiedCount = 0
if ($win32) {
    # ✅ Standalone Qt SDK: windeployqt handles Qt DLLs; detect remaining deps recursively
    Write-Host "Detecting and copying required DLLs (recursive)..." -ForegroundColor Cyan

    $objdumpExe = "$toolchainPath\bin\objdump.exe"
    if (-not (Test-Path $objdumpExe)) {
        Write-Host "❌ objdump not found at $objdumpExe" -ForegroundColor Red
        exit 1
    }

    $win32SearchPaths = @("$qtSdkPath\bin", "$toolchainPath\bin")
    $win32SystemPrefixes = @("C:\Windows", "C:\WINDOWS", "$env:SystemRoot", "$env:windir") |
        Where-Object { $_ } | ForEach-Object { $_.ToLower() }
    $win32Visited = @{}

    function Resolve-Win32Deps {
        param([string]$BinaryPath)

        $imports = & $objdumpExe -p $BinaryPath 2>$null |
            Select-String "DLL Name:\s+(\S+)" |
            ForEach-Object { $_.Matches[0].Groups[1].Value }

        foreach ($dllName in $imports) {
            $lowerName = $dllName.ToLower()
            if ($win32Visited.ContainsKey($lowerName)) { continue }
            $win32Visited[$lowerName] = $true

            # Already in build dir (e.g. copied by windeployqt)
            if (Test-Path ".\$dllName") {
                Resolve-Win32Deps -BinaryPath ".\$dllName"
                continue
            }

            # Search in Qt SDK and MinGW toolchain
            $srcPath = $null
            foreach ($dir in $win32SearchPaths) {
                $candidate = "$dir\$dllName"
                if (Test-Path $candidate) { $srcPath = $candidate; break }
            }
            if (-not $srcPath) { continue }

            # Skip Windows system DLLs
            $lowerSrc = $srcPath.ToLower()
            $isSystem = $false
            foreach ($sp in $win32SystemPrefixes) {
                if ($lowerSrc.StartsWith($sp)) { $isSystem = $true; break }
            }
            if ($isSystem) { continue }

            Copy-Item -Path $srcPath -Destination ".\$dllName" -Force
            $script:copiedCount++
            Write-Host "   Copied $dllName" -ForegroundColor Gray

            Resolve-Win32Deps -BinaryPath ".\$dllName"
        }
    }

    Resolve-Win32Deps -BinaryPath ".\speedynote.exe"
    Write-Host "✅ Copied $copiedCount DLL(s) from Qt SDK / MinGW toolchain" -ForegroundColor Green
} else {
# ✅ Copy required DLLs automatically using ntldd (recursive dependency detection)
Write-Host "Detecting and copying required DLLs..." -ForegroundColor Cyan

$sourceDir = "$toolchainPath\bin"
$ntlddExe = "$toolchainPath\bin\ntldd.exe"

# Windows system directories to exclude (DLLs from these are provided by Windows)
$systemPaths = @(
    "C:\Windows",
    "C:\WINDOWS", 
    "$env:SystemRoot",
    "$env:windir"
) | Where-Object { $_ } | ForEach-Object { $_.ToLower() }

function Test-SystemDll {
    param([string]$dllPath)
    $lowerPath = $dllPath.ToLower()
    foreach ($sysPath in $systemPaths) {
        if ($lowerPath.StartsWith($sysPath)) { return $true }
    }
    # Also exclude "not found" entries
    if ($lowerPath -match "not found") { return $true }
    return $false
}

if (Test-Path $ntlddExe) {
    Write-Host "Using ntldd for automatic dependency detection..." -ForegroundColor Gray
    
    # Get all dependencies recursively using ntldd
    $ntlddOutput = & $ntlddExe -R "speedynote.exe" 2>$null
    
    # Debug: Show how many lines ntldd returned
    $ntlddLineCount = ($ntlddOutput | Measure-Object).Count
    Write-Host "   ntldd found $ntlddLineCount dependency entries" -ForegroundColor Gray
    
    # Parse ntldd output: "dllname.dll => /path/to/dll (0xaddress)" or "dllname.dll => not found"
    # ntldd may output MSYS2 paths (/clangarm64/...) or Windows paths (C:/msys64/...)
    $dllsToCopy = @{}
    $skippedSystem = 0
    $skippedNotFound = 0
    foreach ($line in $ntlddOutput) {
        if ($line -match '^\s*(\S+\.dll)\s+=>\s+(.+?)\s*(\(0x|$)') {
            $dllName = $Matches[1]
            $dllPath = $Matches[2].Trim()
            
            # Skip system DLLs and "not found" entries
            if (-not (Test-SystemDll $dllPath)) {
                # Convert MSYS2 paths to Windows paths if needed
                if ($dllPath.StartsWith("/")) {
                    # MSYS2 format: /clangarm64/bin/foo.dll or /clang64/bin/foo.dll
                    $dllPath = $dllPath -replace "^/$toolchain", $toolchainPath
                    $dllPath = $dllPath -replace "/", "\"
                } elseif ($dllPath -match "^[A-Za-z]:/") {
                    # Already Windows format with forward slashes: C:/msys64/clangarm64/bin/foo.dll
                    $dllPath = $dllPath -replace "/", "\"
                }
                # If it's already a Windows path with backslashes, use as-is
                
                if (Test-Path $dllPath) {
                    if (-not $dllsToCopy.ContainsKey($dllName)) {
                        $dllsToCopy[$dllName] = $dllPath
                    }
                } else {
                    $skippedNotFound++
                }
            } else {
                $skippedSystem++
            }
        }
    }
    
    Write-Host "   Skipped $skippedSystem system DLLs, $skippedNotFound not found" -ForegroundColor Gray
    Write-Host "Found $($dllsToCopy.Count) dependencies to copy" -ForegroundColor Gray
    
    # Copy all detected DLLs
    foreach ($dll in $dllsToCopy.GetEnumerator()) {
        $destPath = $dll.Key
        if (-not (Test-Path $destPath)) {
            Copy-Item -Path $dll.Value -Destination $destPath -Force
            $copiedCount++
        }
    }
    
} else {
    Write-Host "ntldd not found, using MSYS2 bash + ldd for dependency detection..." -ForegroundColor Yellow
    
    # Fallback: Use MSYS2 bash to run ldd
    $bashExe = "$msys2Root\usr\bin\bash.exe"
    if (Test-Path $bashExe) {
        $lddScript = @"
export PATH="/$toolchain/bin:`$PATH"
ldd speedynote.exe 2>/dev/null | grep "/$toolchain/" | awk '{print `$3}'
"@
        $lddOutput = & $bashExe -lc $lddScript 2>$null
        
        foreach ($dllPath in $lddOutput) {
            if ($dllPath -and $dllPath.Trim()) {
                # Convert MSYS2 path to Windows path
                $winPath = $dllPath -replace "^/$toolchain", $toolchainPath
                $winPath = $winPath -replace "/", "\"
                $dllName = Split-Path -Leaf $winPath
                
                if ((Test-Path $winPath) -and (-not (Test-Path $dllName))) {
                    Copy-Item -Path $winPath -Destination $dllName -Force
                    $copiedCount++
                }
            }
        }
    } else {
        Write-Host "⚠️  Neither ntldd nor MSYS2 bash found. Please install ntldd:" -ForegroundColor Red
        Write-Host "   pacman -S mingw-w64-clang-aarch64-ntldd (ARM64)" -ForegroundColor Yellow
        Write-Host "   pacman -S mingw-w64-clang-x86_64-ntldd (x64)" -ForegroundColor Yellow
        exit 1
    }
}

Write-Host "✅ Copied $copiedCount DLL(s) from $toolchain" -ForegroundColor Green
Write-Host "   Note: MuPDF is dynamically linked (libmupdf.dll)" -ForegroundColor Gray
} # end MSYS2 DLL detection

# Poppler removed - SpeedyNote uses MuPDF exclusively for PDF rendering and export

Write-Host ""
Write-Host "✅ Build complete!" -ForegroundColor Green
Write-Host "   PDF rendering: MuPDF" -ForegroundColor Cyan
Write-Host "   PDF export: MuPDF" -ForegroundColor Cyan
Write-Host ""

# ✅ Clean up build artifacts (not needed for packaging)
Write-Host "Cleaning up build artifacts..." -ForegroundColor Gray
$cleanupItems = @(
    ".qt",                    # Qt internal cache
    "speedynote_autogen",        # CMake Qt autogen (MOC/UIC/RCC)
    "CMakeFiles",             # CMake internal files
    "CMakeCache.txt",         # CMake cache
    "cmake_install.cmake",    # CMake install script
    "Makefile",               # Generated Makefile
    "compile_commands.json",  # Clang compilation database
    "qrc_*.cpp",              # Generated resource files
    "*.o",                    # Object files
    "*.obj"                   # Object files (MSVC style)
)

$cleanedCount = 0
foreach ($pattern in $cleanupItems) {
    $items = Get-ChildItem -Path $pattern -ErrorAction SilentlyContinue -Force
    foreach ($item in $items) {
        if ($item.PSIsContainer) {
            Remove-Item -Path $item.FullName -Recurse -Force -ErrorAction SilentlyContinue
        } else {
            Remove-Item -Path $item.FullName -Force -ErrorAction SilentlyContinue
        }
        $cleanedCount++
    }
}
Write-Host "   Removed $cleanedCount build artifact(s)" -ForegroundColor Gray

Write-Host ""
Write-Host "📦 Build folder is ready for packaging with Inno Setup" -ForegroundColor Green
Write-Host ""

cd ../

# ✅ Run the application (unless -norun flag is set)
if (-not $norun) {
    Write-Host "Launching application..." -ForegroundColor Cyan
    & .\build\speedynote.exe
}