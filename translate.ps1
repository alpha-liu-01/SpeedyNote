rm -r build
mkdir build

# ✅ Update translation source files (ensure the .ts files exist already)
$lupdate = if (Test-Path "C:\msys64\clang64\bin\lupdate-qt6.exe") { "C:\msys64\clang64\bin\lupdate-qt6.exe" } else { "C:\msys64\clang64\bin\lupdate.exe" }
& $lupdate source/ -ts ./resources/translations/app_fr.ts ./resources/translations/app_zh.ts ./resources/translations/app_es.ts

# ✅ Remove obsolete entries (strings from deleted files/code)
# lconvert -no-obsolete rewrites the file without obsolete entries
$lconvert = if (Test-Path "C:\msys64\clang64\bin\lconvert-qt6.exe") { "C:\msys64\clang64\bin\lconvert-qt6.exe" } else { "C:\msys64\clang64\bin\lconvert.exe" }
$tsFiles = @(
    "./resources/translations/app_fr.ts",
    "./resources/translations/app_zh.ts", 
    "./resources/translations/app_es.ts"
)

foreach ($ts in $tsFiles) {
    $temp = "$ts.tmp"
    & $lconvert -no-obsolete -o $temp $ts
    Move-Item -Force $temp $ts
}

$linguist = if (Test-Path "C:\msys64\clang64\bin\linguist-qt6.exe") { "C:\msys64\clang64\bin\linguist-qt6.exe" } else { "C:\msys64\clang64\bin\linguist.exe" }
& $linguist resources/translations/app_zh.ts
# & "C:\msys64\clang64\bin\linguist-qt6.exe" resources/translations/app_fr.ts
# & "C:\msys64\clang64\bin\linguist-qt6.exe" resources/translations/app_es.ts
