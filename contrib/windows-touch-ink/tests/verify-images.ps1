# SPDX-License-Identifier: GPL-3.0-or-later
param(
    [Parameter(Mandatory = $true)][string]$EditionDirectory,
    [Parameter(Mandatory = $true)][string]$QtRoot,
    [Parameter(Mandatory = $true)][string]$Compiler,
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '..\integration-build'),
    [switch]$BuildOnly, [switch]$RunOnly, [switch]$Edgeless,
    [switch]$Reopen, [switch]$ProbeOnly, [switch]$Page, [switch]$Portable
)
$ErrorActionPreference = 'Stop'
$editionDir = (Resolve-Path -LiteralPath $EditionDirectory).Path
$qtRoot = (Resolve-Path -LiteralPath $QtRoot).Path
$compilerPath = (Resolve-Path -LiteralPath $Compiler).Path
$sourceDir = Join-Path $PSScriptRoot '..\src'
$null = New-Item -ItemType Directory -Path $BuildDirectory -Force
$BuildDirectory = (Resolve-Path -LiteralPath $BuildDirectory).Path
$testAppDir = Join-Path $BuildDirectory 'runtime'
$testSourceDir = Join-Path $BuildDirectory 'sources'
$includeRoot = Join-Path $qtRoot 'include\qt6'
if (!(Test-Path -LiteralPath $includeRoot)) { $includeRoot = Join-Path $qtRoot 'include' }
$moc = @('share\qt6\bin\moc.exe','bin\moc.exe','libexec\moc.exe') |
    ForEach-Object { Join-Path $qtRoot $_ } | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (!$moc) { throw 'Qt moc.exe not found in QtRoot.' }
$environmentNames = @('PATH','QT_QPA_PLATFORM','QT_QPA_GENERIC_PLUGINS','QT_FORCE_STDERR_LOGGING',
    'QT_PLUGIN_PATH','SPEEDYNOTE_IMAGE_PAGE','SPEEDYNOTE_IMAGE_EDGELESS','SPEEDYNOTE_IMAGE_REOPEN','SPEEDYNOTE_IMAGE_PROBE_OUTPUT')
$savedEnvironment = @{}
foreach ($name in $environmentNames) { $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
try {
if (!$RunOnly) {
    New-Item -ItemType Directory -Path $testAppDir,$testSourceDir -Force | Out-Null
    if (!$ProbeOnly) {
        Get-ChildItem -LiteralPath $editionDir -File | Where-Object {
            $_.Name -eq 'speedynote.exe' -or $_.Name -eq 'qt.conf' -or $_.Extension -in '.dll','.qm'
        } | Copy-Item -Destination $testAppDir -Force
        foreach ($name in 'platforms','imageformats','iconengines','styles','tls','networkinformation') {
            $runtimePath = Join-Path $editionDir $name
            if (Test-Path -LiteralPath $runtimePath) { Copy-Item -LiteralPath $runtimePath -Destination $testAppDir -Recurse -Force }
        }
        $null = New-Item -ItemType Directory -Path (Join-Path $testAppDir 'generic') -Force
    }
    $includes = @("-I$includeRoot", "-I$includeRoot\QtCore", "-I$includeRoot\QtGui", "-I$includeRoot\QtWidgets")
    $adapterText = [IO.File]::ReadAllText((Join-Path $sourceDir 'touchink.cpp'))
    $adapterText = $adapterText.Replace('L"Software\\SpeedyNoteTouchInk\\Profile"', 'L"Software\\SpeedyNoteTouchInk\\ImageIntegrationTestProfile"')
    $adapterText = $adapterText.Replace('QStringLiteral("SpeedyNote-TouchInk")', 'QStringLiteral("SpeedyNote-TouchInk-ImageTest")')
    $adapterText = $adapterText.Replace('"SpeedyNote_TouchInk______"', '"SpeedyNote_TouchInkTest__"')
    if (!$adapterText.Contains('ImageIntegrationTestProfile')) { throw 'Test profile substitution failed' }
    [IO.File]::WriteAllText((Join-Path $testSourceDir 'touchink.cpp'), $adapterText, [Text.UTF8Encoding]::new($false))
    foreach ($name in 'rounding.h','imageinsert.h','launcherimages.h','imagedocument.h','touchink.json') {
        Copy-Item -LiteralPath (Join-Path $sourceDir $name) -Destination $testSourceDir -Force
    }
    foreach ($name in 'image-probe.cpp','image-probe.json') {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $testSourceDir -Force
    }
    $oldPath = $env:PATH
    $env:PATH = "$editionDir;$oldPath"
    Push-Location -LiteralPath $testSourceDir
    try {
        if (!$ProbeOnly) {
        & $moc touchink.cpp -o touchink.moc
        if ($LASTEXITCODE) { throw 'Test adapter moc failed' }
        & $compilerPath -std=c++20 -O2 -DQT_NO_DEBUG -shared -static-libgcc -static-libstdc++ @includes `
            touchink.cpp "-L$qtRoot\lib" -lQt6Widgets -lQt6Gui -lQt6Core -ladvapi32 -o "$testAppDir\generic\speedynote_touchink.dll"
        if ($LASTEXITCODE) { throw 'Test adapter build failed' }
        }
        & $moc image-probe.cpp -o image-probe.moc
        if ($LASTEXITCODE) { throw 'Image probe moc failed' }
        & $compilerPath -std=c++20 -O2 -DQT_NO_DEBUG -shared -static-libgcc -static-libstdc++ @includes `
            image-probe.cpp "-L$qtRoot\lib" -lQt6Widgets -lQt6Gui -lQt6Core -o "$testAppDir\generic\speedynote_image_probe.dll"
        if ($LASTEXITCODE) { throw 'Image probe build failed' }
    } finally { Pop-Location; $env:PATH = $oldPath }
}
if ($BuildOnly) { Write-Output 'Isolated image integration fixture built.'; return }
$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_QPA_GENERIC_PLUGINS = 'speedynotetouchink,speedynoteimageprobe'
$env:QT_FORCE_STDERR_LOGGING = '1'
$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
$env:QT_PLUGIN_PATH = $testAppDir
$label = if ($Page) { 'page' } elseif ($Edgeless) { 'edgeless' } else { 'paged' }
if ($Page) { $env:SPEEDYNOTE_IMAGE_PAGE = '1' } else { Remove-Item Env:SPEEDYNOTE_IMAGE_PAGE -ErrorAction SilentlyContinue }
if ($Edgeless) { $env:SPEEDYNOTE_IMAGE_EDGELESS = '1' } else { Remove-Item Env:SPEEDYNOTE_IMAGE_EDGELESS -ErrorAction SilentlyContinue }
$outputPrefix = Join-Path $BuildDirectory "images-$label"
$fileArgs = @()
$hiddenAsset = $null
$originalAsset = $null
if ($Reopen -or $Portable) {
    $savedReport = Get-Content -LiteralPath "$outputPrefix.json" -Raw | ConvertFrom-Json
    if (!(Test-Path -LiteralPath $savedReport.bundlePath)) { throw 'Saved test notebook missing' }
    $reopenPath = $savedReport.bundlePath
    if ($Portable) {
        if (!$Page) { throw 'Portable check requires -Page' }
        $sourceBundle = (Resolve-Path -LiteralPath $reopenPath).Path
        if (!$sourceBundle.Contains('\SpeedyNote-TouchInk-ImageTest\image-notebooks\')) {
            throw 'Refusing to alter a notebook outside the isolated image test namespace'
        }
        $reopenPath = Join-Path $BuildDirectory ('portable-image-page-' + [guid]::NewGuid().ToString() + '.snb')
        Copy-Item -LiteralPath $sourceBundle -Destination $reopenPath -Recurse
        $originalAsset = Join-Path $sourceBundle 'image.pdf'
        $hiddenAsset = $originalAsset + '.test-hidden'
        Rename-Item -LiteralPath $originalAsset -NewName 'image.pdf.test-hidden'
        $outputPrefix += '-portable'
    } else { $outputPrefix += '-reopened' }
    $fileArgs = @('"' + $reopenPath + '"')
    $env:SPEEDYNOTE_IMAGE_REOPEN = '1'
} else { Remove-Item Env:SPEEDYNOTE_IMAGE_REOPEN -ErrorAction SilentlyContinue }
$env:SPEEDYNOTE_IMAGE_PROBE_OUTPUT = $outputPrefix
$launchParams = @{FilePath = (Join-Path $testAppDir 'speedynote.exe'); WorkingDirectory = $testAppDir;
    WindowStyle = 'Hidden'; PassThru = $true; RedirectStandardOutput = "$outputPrefix-stdout.log";
    RedirectStandardError = "$outputPrefix-stderr.log"}
if ($fileArgs.Count) { $launchParams.ArgumentList = $fileArgs }
$testProcess = Start-Process @launchParams
if (!$testProcess.WaitForExit(45000)) {
    $testProcess.Kill()
    throw 'Isolated image integration fixture timed out'
}
$report = Get-Content -LiteralPath "$outputPrefix.json" -Raw | ConvertFrom-Json
$report | ConvertTo-Json -Depth 6
if (!$report.passed) { throw 'Isolated image integration fixture failed' }
} finally {
    if ($hiddenAsset -and (Test-Path -LiteralPath $hiddenAsset)) {
        Rename-Item -LiteralPath $hiddenAsset -NewName 'image.pdf'
    }
    foreach ($name in $environmentNames) {
        [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process')
    }
}
