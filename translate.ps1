rm -r build
mkdir build

# Resolve Qt tool paths
$lupdate  = if (Test-Path "C:\msys64\clang64\bin\lupdate-qt6.exe")  { "C:\msys64\clang64\bin\lupdate-qt6.exe"  } else { "C:\msys64\clang64\bin\lupdate.exe"  }
$lconvert = if (Test-Path "C:\msys64\clang64\bin\lconvert-qt6.exe") { "C:\msys64\clang64\bin\lconvert-qt6.exe" } else { "C:\msys64\clang64\bin\lconvert.exe" }
$linguist = if (Test-Path "C:\msys64\clang64\bin\linguist-qt6.exe") { "C:\msys64\clang64\bin\linguist-qt6.exe" } else { "C:\msys64\clang64\bin\linguist.exe"  }

# Auto-discover all translation files in resources/translations/
$tsFiles = Get-ChildItem -Path ".\resources\translations\app_*.ts" | Select-Object -ExpandProperty FullName

if ($tsFiles.Count -eq 0) {
    Write-Host "No .ts files found in resources/translations/. Create one first (see docs/TRANSLATION_GUIDE.md)."
    exit 1
}

# Update translation source files with new/changed strings from source/
& $lupdate source/ -ts $tsFiles

# Remove obsolete entries (strings removed from source code)
foreach ($ts in $tsFiles) {
    $temp = "$ts.tmp"
    & $lconvert -no-obsolete -o $temp $ts
    Move-Item -Force $temp $ts
}

# Open Qt Linguist for each .ts file that has unfinished translations.
# By default only app_zh.ts is opened; uncomment or add lines for other languages.
& $linguist resources/translations/app_zh.ts
# & $linguist resources/translations/app_es.ts
# & $linguist resources/translations/app_fr.ts
# Add your language here, e.g.:
# & $linguist resources/translations/app_de.ts
