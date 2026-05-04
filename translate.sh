#!/bin/bash

# Qt6 tools paths
QT6_BIN="/usr/lib/qt6/bin"

# Remove and recreate build directory
rm -rf build
mkdir -p build

# Auto-discover all translation files in resources/translations/
ts_files=( resources/translations/app_*.ts )

if [ ${#ts_files[@]} -eq 0 ] || [ ! -f "${ts_files[0]}" ]; then
    echo "No .ts files found in resources/translations/. Create one first (see docs/TRANSLATION_GUIDE.md)."
    exit 1
fi

# Update translation source files with new/changed strings from source/
"$QT6_BIN/lupdate" source/ -ts "${ts_files[@]}"

# Remove obsolete entries (strings removed from source code)
for ts_file in "${ts_files[@]}"; do
    temp_file="${ts_file}.tmp"
    "$QT6_BIN/lconvert" -no-obsolete -o "$temp_file" "$ts_file"
    mv -f "$temp_file" "$ts_file"
done

# Optional: auto-translate Spanish/French (and any other languages) with an LLM,
# using the English source + Chinese translation as references.
#
#   pip install -r scripts/requirements.txt
#   export GEMINI_API_KEY="<your key from https://aistudio.google.com/app/apikey>"
#   python3 scripts/translate_auto.py --lang es
#   python3 scripts/translate_auto.py --lang fr
#
# See docs/TRANSLATION_GUIDE.md for details and other provider options.

# Open Qt Linguist for each .ts file that has unfinished translations.
# By default only app_zh.ts is opened; uncomment or add lines for other languages.
"$QT6_BIN/linguist" resources/translations/app_zh.ts
# "$QT6_BIN/linguist" resources/translations/app_es.ts
# "$QT6_BIN/linguist" resources/translations/app_fr.ts
# Add your language here, e.g.:
# "$QT6_BIN/linguist" resources/translations/app_de.ts
