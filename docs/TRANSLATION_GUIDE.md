# SpeedyNote Translation Contribution Guide

Thank you for your interest in translating SpeedyNote! This guide explains how to add or improve a translation starting from a fork of the source code.

SpeedyNote uses Qt's Linguist toolchain: strings in the C++ source are marked with `tr()`, extracted into `.ts` XML files, translated using Qt Linguist, and compiled into binary `.qm` files that the app loads at runtime.

---

## Language support status

| Language | Code | Status |
|---|---|---|
| English | `en` | Default — no translation file needed |
| Chinese Simplified | `zh` | Fully supported |
| Spanish | `es` | Partially translated — help welcome |
| French | `fr` | Partially translated — help welcome |

Any other language not listed above is not yet started.

---

## Prerequisites

You need the Qt Linguist tools installed on your machine. These are the same tools used to build SpeedyNote.

**Windows (MSYS2 clang64):**
```
pacman -S mingw-w64-clang-x86_64-qt6-tools
```

**Linux (Debian/Ubuntu):**
```
sudo apt install qt6-tools-dev-tools
# or for Qt5 builds:
sudo apt install qttools5-dev-tools
```

**macOS (Homebrew):**
```
brew install qt
```

The tools you will use are:
- `lupdate` — scans the C++ source and updates `.ts` files with new/changed strings
- `linguist` — GUI editor for filling in translations
- `lrelease` — compiles a `.ts` file into the binary `.qm` format

---

## Step 1 — Fork and clone

Fork the repository on GitHub, then clone your fork:

```
git clone https://github.com/<your-username>/SpeedyNote.git
cd SpeedyNote
```

---

## Step 2 — Create a new `.ts` file for your language

Translation source files live in `resources/translations/` and follow the naming pattern `app_<lang>.ts`, where `<lang>` is an [ISO 639-1](https://en.wikipedia.org/wiki/List_of_ISO_639-1_codes) two-letter language code (e.g. `de` for German, `ja` for Japanese, `ko` for Korean).

If you are **starting a new language**, copy an existing file as a starting point and clear the translations:

```
cp resources/translations/app_zh.ts resources/translations/app_de.ts
```

Open the new file in a text editor and change the `language` attribute on the `<TS>` tag to match your locale (e.g. `de_DE`):

```xml
<TS version="2.1" language="de_DE">
```

Then clear every `<translation>` element (leave the tags but make the content empty) and change all `type="unfinished"` attributes to stay as `type="unfinished"`. You do not need to do this manually — the next step regenerates the file cleanly from the source.

If you are **improving an existing translation** (Spanish or French), skip this step and go directly to Step 3.

---

## Step 3 — Update the `.ts` file from source

Run the appropriate translation helper script from the repository root. These scripts call `lupdate` to scan the C++ source tree and insert any new or changed strings into every `.ts` file, then strip obsolete entries.

**Windows (PowerShell):**
```powershell
.\translate.ps1
```

**Linux / macOS (bash):**
```bash
bash translate.sh
```

Both scripts auto-discover every `app_*.ts` file in `resources/translations/`, so your new file is picked up automatically.

After the script finishes, Qt Linguist opens with the default file (`app_zh.ts`). Close it — you will open your own file in the next step.

Alternatively, you can run `lupdate` directly:

```bash
# Linux/macOS
/usr/lib/qt6/bin/lupdate source/ -ts resources/translations/app_de.ts

# Windows (MSYS2 clang64)
C:\msys64\clang64\bin\lupdate.exe source/ -ts resources/translations/app_de.ts
```

---

## Step 4 — Translate strings in Qt Linguist

Open your `.ts` file in Qt Linguist:

```bash
# Linux/macOS
/usr/lib/qt6/bin/linguist resources/translations/app_de.ts

# Windows (MSYS2 clang64)
C:\msys64\clang64\bin\linguist.exe resources/translations/app_de.ts
```

In Linguist:

- Strings marked **Unfinished** (yellow question mark) need a translation.
- Enter the translation in the bottom text field and press **Ctrl+Return** to mark it done.
- Strings with **no translation type** (green tick) are already accepted.
- Use **View → Sort by → Source text** to work through strings alphabetically.
- Save frequently with **Ctrl+S**.

Do not worry about translating every single string in one go. Partial translations are useful — untranslated strings fall back to English automatically.

---

## Step 5 — Compile the `.ts` file to a `.qm` binary

Once you are done translating, compile the `.ts` to a `.qm` binary:

```bash
# Linux/macOS
/usr/lib/qt6/bin/lrelease resources/translations/app_de.ts \
    -qm resources/translations/app_de.qm

# Windows (MSYS2 clang64)
C:\msys64\clang64\bin\lrelease.exe resources/translations/app_de.ts `
    -qm resources/translations/app_de.qm
```

The `.qm` file is a compiled binary. Both the `.ts` source and the `.qm` binary should be committed to the repository — the `.ts` for future editing and the `.qm` so the file can be embedded in the app.

---

## Step 6 — Register the `.qm` file in `resources.qrc`

Open `resources.qrc` (in the repository root) and add an entry for your new language inside the `<qresource>` block, alongside the existing translation entries:

```xml
<file>resources/translations/app_de.qm</file>
```

The full block looks like this after the addition:

```xml
<file>resources/translations/app_zh.qm</file>
<file>resources/translations/app_es.qm</file>
<file>resources/translations/app_fr.qm</file>
<file>resources/translations/app_de.qm</file>   <!-- your new entry -->
```

This embeds the translation into the application binary so it works without any external files.

---

## Step 7 — Add the language to the in-app selector

Open `source/ControlPanelDialog.cpp` and find the `createLanguageTab()` function (around line 1742). Add your language to the `languageCombo` alongside the existing entries:

```cpp
languageCombo->addItem(tr("English"),                    "en");
languageCombo->addItem(tr("Español (Spanish)"),          "es");
languageCombo->addItem(tr("Français (French)"),          "fr");
languageCombo->addItem(tr("中文 (Chinese Simplified)"),  "zh");
languageCombo->addItem(tr("Deutsch (German)"),           "de"); // your new entry
```

Use the native name of the language in parentheses after the English name — this helps users identify their language even if the UI is in a language they cannot read.

---

## Step 8 — Verify the translation loads correctly

Build the project normally (see the relevant build guide in `docs/build_docs/`) and launch SpeedyNote. Go to **Settings → Language**, disable "Use System Language", select your language from the dropdown, and restart the app. All translated strings should appear in your language.

If a string still shows in English it means it either has no translation yet (acceptable) or the `.qm` file was not rebuilt after your last edit — re-run `lrelease` and rebuild.

---

## Step 9 — Submit a pull request

Commit all of the following files:

| File | Why |
|---|---|
| `resources/translations/app_<lang>.ts` | Translation source (required for future editing) |
| `resources/translations/app_<lang>.qm` | Compiled binary (required for the app to embed) |
| `resources.qrc` | New `<file>` entry for the `.qm` |
| `source/ControlPanelDialog.cpp` | New entry in the language selector combo |

Open a pull request against the `main` branch. In the PR description, mention:

- Which language you are adding or improving
- Approximately what percentage of strings are translated
- Your preferred contact method if the maintainer has questions

---

## Tips

**Keeping your translation up to date**

When new strings are added to the source code in future versions, re-run the translation script (Step 3) to pull them into your `.ts` file. New strings will appear in Linguist with an "Unfinished" marker.

**Context information**

Some strings in Linguist show a "Comment" field — these are hints left by the developer to explain where or how a string is used. Use them to choose the right phrasing.

**Plurals**

Qt Linguist supports plural forms. If you see a string with `%n` in it, Linguist will ask for both singular and plural translations. Fill in both according to your language's grammar rules.

**Right-to-left languages**

Qt handles RTL layout mirroring automatically for languages like Arabic (`ar`) and Hebrew (`he`) when the locale is set correctly. No extra code changes are needed.

**Testing without a full build**

You can place the compiled `app_<lang>.qm` file next to the SpeedyNote binary and it will be loaded from there before the embedded QRC copy. This lets you iterate without rebuilding the whole application.
