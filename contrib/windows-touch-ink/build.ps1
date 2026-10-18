# SPDX-License-Identifier: GPL-3.0-or-later
param(
    [Parameter(Mandatory = $true)][string]$QtRoot,
    [Parameter(Mandatory = $true)][string]$Compiler,
    [Parameter(Mandatory = $true)][string]$EditionDirectory,
    [string]$BuildDirectory = (Join-Path $PSScriptRoot 'build'),
    [string]$NativeSourceRoot = (Join-Path $PSScriptRoot '..\..\source'),
    [string]$QtVersion = '6.11.2',
    [switch]$RunTests
)
$ErrorActionPreference = 'Stop'
$QtRoot = (Resolve-Path -LiteralPath $QtRoot).Path
$Compiler = (Resolve-Path -LiteralPath $Compiler).Path
$EditionDirectory = (Resolve-Path -LiteralPath $EditionDirectory).Path
$NativeSourceRoot = (Resolve-Path -LiteralPath $NativeSourceRoot).Path
foreach ($name in 'speedynote.exe','Qt6Core.dll') {
    if (!(Test-Path -LiteralPath (Join-Path $EditionDirectory $name))) {
        throw "Use a separate copy of the complete SpeedyNote 1.6.3 installation (missing $name)."
    }
}
$runtimeVersion = (Get-Item -LiteralPath (Join-Path $EditionDirectory 'Qt6Core.dll')).VersionInfo.FileVersion
if (!$runtimeVersion.StartsWith($QtVersion + '.')) {
    throw "Runtime Qt $runtimeVersion does not match QtVersion $QtVersion."
}
$includeRoot = Join-Path $QtRoot 'include\qt6'
if (!(Test-Path -LiteralPath $includeRoot)) { $includeRoot = Join-Path $QtRoot 'include' }
$moc = @('share\qt6\bin\moc.exe','bin\moc.exe','libexec\moc.exe') |
    ForEach-Object { Join-Path $QtRoot $_ } | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (!$moc) { throw 'Qt moc.exe not found in QtRoot.' }
$null = New-Item -ItemType Directory -Force -Path $BuildDirectory
$BuildDirectory = (Resolve-Path -LiteralPath $BuildDirectory).Path
$staging = Join-Path $BuildDirectory 'sources'
$null = New-Item -ItemType Directory -Force -Path $staging,(Join-Path $BuildDirectory 'test-results'),(Join-Path $EditionDirectory 'generic')
Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'src') -File | Copy-Item -Destination $staging -Force
foreach ($name in 'input-tests.cpp','curve-tests.cpp') {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "tests\$name") -Destination $staging -Force
}
$includes = @("-I$includeRoot", "-I$includeRoot\QtCore", "-I$includeRoot\QtGui", "-I$includeRoot\QtWidgets",
    "-I$includeRoot\QtCore\$QtVersion", "-I$includeRoot\QtGui\$QtVersion", "-I$NativeSourceRoot")
$oldPath = $env:PATH
$oldPlatform = $env:QT_QPA_PLATFORM
$oldPlugins = $env:QT_QPA_GENERIC_PLUGINS
$oldPluginPath = $env:QT_PLUGIN_PATH
$env:PATH = "$EditionDirectory;$oldPath"
$env:QT_PLUGIN_PATH = $EditionDirectory
Push-Location -LiteralPath $staging
try {
    & $moc touchink.cpp -o touchink.moc
    if ($LASTEXITCODE) { throw 'Plugin moc failed' }
    $plugin = Join-Path $BuildDirectory 'speedynote_touchink.dll'
    & $Compiler -std=c++20 -O2 -DQT_NO_DEBUG -shared -static-libgcc -static-libstdc++ @includes `
        touchink.cpp "-L$QtRoot\lib" -lQt6Widgets -lQt6Gui -lQt6Core -ladvapi32 -o $plugin
    if ($LASTEXITCODE) { throw 'Plugin build failed' }
    $update = Join-Path $EditionDirectory 'touch-ink-update.dll'
    Copy-Item -LiteralPath $plugin -Destination $update -Force
    try {
        Copy-Item -LiteralPath $update -Destination (Join-Path $EditionDirectory 'generic\speedynote_touchink.dll') -Force
        Remove-Item -LiteralPath $update
    } catch {
        Write-Output 'Plugin update prepared; close edition windows and launch again to install it.'
    }
    & $Compiler -std=c++17 -O2 -mwindows -municode -static `
        '-finput-charset=UTF-8' '-fexec-charset=UTF-8' launcher.cpp -o (Join-Path $EditionDirectory 'touch-ink-launcher.exe')
    if ($LASTEXITCODE) { throw 'Launcher build failed (close an existing launcher and retry).' }
    if ($RunTests) {
        if (Test-Path -LiteralPath $update) { throw 'Close edition windows and rerun to test the newly built plugin.' }
        & $moc input-tests.cpp -o input-tests.moc
        if ($LASTEXITCODE) { throw 'Input test moc failed' }
        $inputTest = Join-Path $BuildDirectory 'input-tests.exe'
        & $Compiler -std=c++20 -O2 -DQT_NO_DEBUG -static-libgcc -static-libstdc++ @includes `
            input-tests.cpp "-L$QtRoot\lib" -lQt6Widgets -lQt6Gui -lQt6Core -ladvapi32 -o $inputTest
        if ($LASTEXITCODE) { throw 'Input test build failed' }
        $env:QT_QPA_PLATFORM = 'offscreen'
        $env:QT_QPA_GENERIC_PLUGINS = 'speedynotetouchink'
        & $inputTest --enabled
        if ($LASTEXITCODE) { throw 'Touch input tests failed' }
        Remove-Item Env:QT_QPA_GENERIC_PLUGINS
        & $inputTest --disabled
        if ($LASTEXITCODE) { throw 'Baseline input test failed' }
        $curveTest = Join-Path $BuildDirectory 'curve-tests.exe'
        & $Compiler -std=c++20 -O2 -DQT_NO_DEBUG -static-libgcc -static-libstdc++ @includes `
            curve-tests.cpp "-L$QtRoot\lib" -lQt6Gui -lQt6Core -o $curveTest
        if ($LASTEXITCODE) { throw 'Curve test build failed' }
        & $curveTest
        if ($LASTEXITCODE) { throw 'Curve regression checks failed; see build/test-results.' }
    }
    Write-Output "TouchInk built in $EditionDirectory. Start touch-ink-launcher.exe."
} finally {
    Pop-Location
    $env:PATH = $oldPath
    $env:QT_QPA_PLATFORM = $oldPlatform
    $env:QT_QPA_GENERIC_PLUGINS = $oldPlugins
    $env:QT_PLUGIN_PATH = $oldPluginPath
}
