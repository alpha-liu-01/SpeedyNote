#!/bin/bash

# Qt6 tools paths
QT6_BIN="/usr/lib/qt6/bin"

# Remove and recreate build directory
rm -rf build
mkdir -p build

# Update translation source files (ensure the .ts files exist already)
"$QT6_BIN/lupdate" source/ -ts ./resources/translations/app_fr.ts ./resources/translations/app_zh.ts ./resources/translations/app_es.ts

# Remove obsolete entries from translation files
ts_files=(
    "./resources/translations/app_fr.ts"
    "./resources/translations/app_zh.ts"
    "./resources/translations/app_es.ts"
)

for ts_file in "${ts_files[@]}"; do
    temp_file="${ts_file}.tmp"
    "$QT6_BIN/lconvert" -no-obsolete -o "$temp_file" "$ts_file"
    mv -f "$temp_file" "$ts_file"
done

# Launch linguist for Chinese translation file
"$QT6_BIN/linguist" resources/translations/app_zh.ts
# "$QT6_BIN/linguist" resources/translations/app_fr.ts
# "$QT6_BIN/linguist" resources/translations/app_es.ts