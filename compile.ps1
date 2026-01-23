param(
    [switch]$arm64,      # Build for ARM64 Windows (Snapdragon)
    [switch]$old,        # Build for older x86_64 CPUs (SSE3/SSSE3)
    [switch]$legacy,     # Alias for -old
    [switch]$debug,      # Enable verbose debug output (qDebug)
    [switch]$norun       # Don't run the application after building (for CI/remote builds)
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

# Clean and recreate build folder
if (Test-Path ".\build" -PathType Container) {
    # Kill any running NoteApp instances that might lock files
    $noteAppProcesses = Get-Process -Name "NoteApp" -ErrorAction SilentlyContinue
    if ($noteAppProcesses) {
        Write-Host "Stopping running NoteApp instances..." -ForegroundColor Yellow
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

if ($debug) {
    $cmakeArgs += "-DENABLE_DEBUG_OUTPUT=ON"
    Write-Host "Debug Output: ENABLED" -ForegroundColor Yellow
} else {
    $cmakeArgs += "-DENABLE_DEBUG_OUTPUT=OFF"
    Write-Host "Debug Output: DISABLED" -ForegroundColor Gray
}

# ✅ Configure and build
& "$toolchainPath\bin\cmake.exe" @cmakeArgs ..

# Determine number of parallel jobs based on architecture
# ARM64 devices often have limited memory/thermal headroom, so use half the cores
$cpuCount = [Environment]::ProcessorCount
if ($arm64) {
    $jobs = [Math]::Max(1, [Math]::Floor($cpuCount / 2))
    Write-Host "Using $jobs parallel jobs (ARM64: half of $cpuCount cores)" -ForegroundColor Gray
} else {
    $jobs = $cpuCount
    Write-Host "Using $jobs parallel jobs (x64: all $cpuCount cores)" -ForegroundColor Gray
}

& "$toolchainPath\bin\cmake.exe" --build . --config Release --parallel $jobs

# ✅ Deploy Qt runtime
& "$toolchainPath\bin\windeployqt6.exe" "NoteApp.exe"

# ✅ Copy required DLLs automatically using ntldd (recursive dependency detection)
Write-Host "Detecting and copying required DLLs..." -ForegroundColor Cyan

$sourceDir = "$toolchainPath\bin"
$copiedCount = 0
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
    $ntlddOutput = & $ntlddExe -R "NoteApp.exe" 2>$null
    
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
ldd NoteApp.exe 2>/dev/null | grep "/$toolchain/" | awk '{print `$3}'
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

# ✅ Also copy any versioned libpoppler DLLs that might have been missed
$popplerDlls = Get-ChildItem -Path "$sourceDir\libpoppler-*.dll" -ErrorAction SilentlyContinue | 
    Where-Object { $_.Name -match '^libpoppler-\d+\.dll$' }
foreach ($popplerDll in $popplerDlls) {
    if (-not (Test-Path $popplerDll.Name)) {
        Copy-Item -Path $popplerDll.FullName -Destination $popplerDll.Name -Force
        $copiedCount++
        Write-Host "  Also copied: $($popplerDll.Name)" -ForegroundColor Gray
    }
}

Write-Host "✅ Copied $copiedCount DLL(s) from $toolchain" -ForegroundColor Green
Write-Host "   Note: MuPDF for PDF export is statically linked (if enabled in CMake)" -ForegroundColor Gray

# ✅ Copy Poppler data files (fonts, etc.)
Copy-Item -Path "$toolchainPath\share\poppler" -Destination "..\build\share\poppler" -Recurse -Force

Write-Host ""
Write-Host "✅ Build complete!" -ForegroundColor Green
Write-Host "   PDF rendering: Poppler" -ForegroundColor Cyan
Write-Host "   PDF export: MuPDF (statically linked)" -ForegroundColor Cyan
Write-Host ""

# ✅ Clean up build artifacts (not needed for packaging)
Write-Host "Cleaning up build artifacts..." -ForegroundColor Gray
$cleanupItems = @(
    ".qt",                    # Qt internal cache
    "NoteApp_autogen",        # CMake Qt autogen (MOC/UIC/RCC)
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
    & .\build\NoteApp.exe
}