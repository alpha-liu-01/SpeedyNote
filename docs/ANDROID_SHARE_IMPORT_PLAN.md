# Android Share & Import Feature Plan

## Overview

Implement a modular share/import system for SpeedyNote on Android, enabling users to:
1. Export notebooks as compressed packages and share via Android's native share sheet
2. Import shared notebook packages from other devices/users
3. Relink missing PDFs in imported notebooks

---

## Feature Components

### Component 1: Share/Export (Share Button)

**Goal:** Compress `.snb` folder and share via Android's native share intent.

**Implementation Steps:**
1. Compress `.snb` folder to ZIP format
2. Use `FileProvider` to create a shareable URI
3. Launch Android's share sheet (ACTION_SEND)
4. Clean up temp files after sharing

### Component 2: Import

**Goal:** Pick a shared notebook package and import it to app-private storage.

**Implementation Steps:**
1. Use SAF to pick a `.zip`/`.snbx` file
2. Copy to temp location while permission is active
3. Extract to `notebooks/` directory
4. Handle naming conflicts

### Component 3: PDF Relinking on Android

**Goal:** Enable `PdfRelinkDialog` to work with Android's SAF.

**Implementation Steps:**
1. Replace `QFileDialog` with custom SAF picker (like PDF import)
2. Copy selected PDF to sandbox while permission is active
3. Update document's PDF path

---

## Open Questions

### Q1: Export File Format

**Options:**

| Option | Extension | Pros | Cons |
|--------|-----------|------|------|
| A | `.snb.zip` | Obvious it's a ZIP | Double extension looks odd |
| B | `.snbx` | Clean, unique | Need to register MIME type |
| C | `.zip` | Universal | No association with SpeedyNote |

**Recommendation:** Option B (`.snbx`) - creates a unique file type that Android can associate with SpeedyNote, enabling "Open with SpeedyNote" functionality.

**Your preference?**
Option B makes sense. 
---

### Q2: Include PDF in Export?

**Options:**

| Option | Description | Pros | Cons |
|--------|-------------|------|------|
| A | Never include PDF | Small files, fast share | Recipient needs same PDF |
| B | Always include PDF | Complete package | Very large files (10-100+ MB) |
| C | Optional (user choice) | Flexible | More complex UI |
| D | Include if < threshold | Automatic, practical | Magic number problem |

**Recommendation:** Option C with a simple toggle in the share dialog: "Include PDF (adds X MB)"

**Your preference?**
Option C makes sense. 
---

### Q3: Duplicate Notebook Handling

When importing a notebook that already exists (same name):

| Option | Behavior |
|--------|----------|
| A | Auto-rename: `Notebook`, `Notebook (1)`, `Notebook (2)` |
| B | Ask user: Overwrite / Rename / Cancel |
| C | Always overwrite silently |

**Recommendation:** Option A - auto-rename is safest and requires no user interaction.

**Your preference?**
I agree with Option A. 
---

### Q4: Share Button Visibility

Should the share button be:

| Option | Behavior |
|--------|----------|
| A | Always visible, disabled on desktop (shows "Android only" tooltip) |
| B | Only visible on Android builds |
| C | Visible everywhere, shows "Coming Soon" on desktop |

Current implementation uses Option C.

**Your preference?**
OK, here's the problem... If we never allow the desktop version to import or export the `.snbx` format, the mobile users will be isolated. So for PC to mobile and mobile to PC, we are probably going to need to share the zip pack / unpack code, for sharing purposes. The primary way to store the notes would still be loose folders. 

This also makes changes to Q5. We need a 5th button on the launcher `FloatingActionButton.cpp`, for importing zip packs. For PCs, we just unzip it in the original folder and import it like normal (if exported with pdf, with PDF references). But since the PDF paths are absolute in our current version of `document.json` inside the snb, we may need to ADD a relative path field (and when it's loaded, it checks the absolute and relative path, and if it's found on one, we may update the other. This is a cross-platform change). If neither is found, it will then fall back to the pdf relink dialog. 

On Android, it would follow the same "copy to the sandbox" routine, which is modular. 

For the sharing feature, it's about the same. The PC version just pops a file explorer dialog and save the snbx to somewhere on the disk. On android, it would then launch the share interface. But please pay attention to error handling. It's very likely that a failed export/import will cause disk space leaks. 

On mobile, the traditional import folder notebook button can just be stubbed or even hidden, since it's not really a choice. 
---

### Q5: Import Entry Points

Where should "Import Notebook" be accessible?

| Location | Current Behavior | Proposed |
|----------|------------------|----------|
| Launcher "Open" button | Opens file dialog | Android: Show menu with "Open" + "Import" |
| Add Tab menu | New/Open options | Android: Add "Import Shared Notebook" |
| Share sheet intent | N/A | Handle `ACTION_VIEW` for `.snbx` files |

**Recommendation:** All three locations for maximum discoverability.

**Your preference?**
I agree with your choice. 
---

### Q6: Progress Indication

For large notebooks (many pages, large PDF):

| Option | Description |
|--------|-------------|
| A | No progress (simple, may look frozen) |
| B | Indeterminate spinner |
| C | Progress bar with percentage |

**Recommendation:** Option B for share, Option C for import (extraction can report progress).

**Your preference?**
I agree with your choice. 
---

## Technical Implementation Details

### Files to Create

```
android/app-resources/src/org/speedynote/app/
├── ShareHelper.java          # ZIP compression + share intent
├── ImportHelper.java         # SAF picker + extraction
└── FileProviderPaths.xml     # For FileProvider configuration

source/
├── sharing/
│   ├── NotebookExporter.h/.cpp   # Qt-side ZIP creation
│   └── NotebookImporter.h/.cpp   # Qt-side extraction
```

### Files to Modify

```
source/ui/NavigationBar.cpp       # Connect share button (Android only)
source/ui/launcher/Launcher.cpp   # Add import option
source/MainWindow.cpp             # Handle import from tab menu
source/pdf/PdfRelinkDialog.cpp    # Android SAF support
android/app-resources/AndroidManifest.xml  # FileProvider, intent filters
```

### Dependencies

| Dependency | Purpose | Status |
|------------|---------|--------|
| Qt's `QZipReader`/`QZipWriter` | ZIP handling | ✅ Available in Qt 6 |
| Android `FileProvider` | Share files securely | Need to add |
| Android `DocumentsContract` | SAF handling | Already using |

---

## Implementation Order

1. **Phase 1: Export/Share** (can be tested immediately)
   - Create `NotebookExporter` class
   - Create `ShareHelper.java`
   - Configure `FileProvider`
   - Connect share button

2. **Phase 2: Import** (needs Phase 1 for testing)
   - Create `NotebookImporter` class
   - Create `ImportHelper.java`
   - Add import UI entry points
   - Handle naming conflicts

3. **Phase 3: PDF Relink on Android**
   - Refactor `PdfRelinkDialog` for Android SAF
   - Reuse PDF copy logic from `PdfFileHelper`

4. **Phase 4: Intent Handling** (optional enhancement)
   - Register `.snbx` MIME type
   - Handle `ACTION_VIEW` intent
   - Enable "Open with SpeedyNote"

---

## Risk Assessment

| Risk | Impact | Mitigation |
|------|--------|------------|
| Large file sharing fails | High | Warn user, offer "without PDF" option |
| ZIP extraction fails mid-way | Medium | Extract to temp, move on success |
| FileProvider misconfiguration | High | Test thoroughly on multiple Android versions |
| Memory issues with large PDFs | Medium | Stream compression, don't load entire file |

---

## Estimated Effort

| Phase | Complexity | Estimated Time |
|-------|------------|----------------|
| Phase 1: Export/Share | Medium | 2-3 hours |
| Phase 2: Import | Medium | 2-3 hours |
| Phase 3: PDF Relink | Low | 30 min (reuses PdfFileHelper) |
| Phase 4: Intent Handling | Low | 1 hour |

**Total:** ~6-8 hours

---

## Notes

- All changes should be wrapped in `#ifdef Q_OS_ANDROID` to avoid affecting desktop builds
- Reuse existing patterns from PDF import (`PdfFileHelper.java`)
- Consider adding this to `ANDROID_BUG_TRACKING.md` as a feature (FEAT-A001?)

---

## Finalized Decisions

| Question | Decision |
|----------|----------|
| Q1: File extension | `.snbx` |
| Q2: Include PDF | Optional (user choice with toggle) |
| Q3: Duplicate handling | Auto-rename: `Notebook (1)`, `Notebook (2)` |
| Q4: Share button | Cross-platform: Android=share sheet, Desktop=file dialog |
| Q5: Import entry points | All three (Launcher, Add Tab menu, Intent) |
| Q6: Progress indication | Spinner for export, Progress bar for import |

---

## Dual PDF Path System (Cross-Platform)

### Concept

Store **both** absolute and relative paths in `document.json`. When loading:
1. Try absolute path → if found, update relative path
2. Try relative path → if found, update absolute path  
3. If neither found → show PDF relink dialog

### document.json Schema (v2)

```json
{
    "bundle_format_version": 2,
    "pdf_path": "/home/alpha/Documents/thesis.pdf",
    "pdf_relative_path": "../embedded/thesis.pdf",
    "pdf_hash": "sha256:...",
    "pdf_size": 17835715
}
```

### Path Resolution Logic

```
┌─────────────────────────────────────────────────────────────┐
│                    PDF Path Resolution                       │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  1. Check pdf_path (absolute)                               │
│     ├─ EXISTS → Load PDF, update pdf_relative_path          │
│     └─ NOT FOUND → Continue to step 2                       │
│                                                              │
│  2. Check pdf_relative_path (relative to document.json)     │
│     ├─ EXISTS → Load PDF, update pdf_path to new absolute   │
│     └─ NOT FOUND → Continue to step 3                       │
│                                                              │
│  3. Show PdfRelinkDialog                                    │
│     ├─ User selects PDF → Update both paths                 │
│     └─ User cancels → Continue without PDF                  │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

### Use Cases

| Scenario | Absolute Path | Relative Path | Resolution |
|----------|---------------|---------------|------------|
| Normal usage | ✅ Valid | ✅ Valid | Load from absolute |
| Moved notebook locally | ❌ Invalid | ✅ Valid | Load from relative, fix absolute |
| Imported from .snbx | ❌ Invalid (foreign) | ✅ Valid (`../embedded/...`) | Load from relative, set absolute |
| PDF deleted | ❌ Invalid | ❌ Invalid | Relink dialog |

---

## ZIP Structure (.snbx)

```
MyNotebook.snbx (ZIP file)
│
├── MyNotebook.snb/              # Original .snb folder
│   ├── document.json
│   └── pages/
│       ├── page_0.json
│       ├── page_1.json
│       └── ...
│
└── embedded/                    # Optional: included PDF
    └── thesis.pdf
```

**Notes:**
- `pdf_relative_path` in exported `document.json`: `../embedded/thesis.pdf`
- `pdf_path` preserved as-is (will be invalid on recipient device)

---

## Desktop Share/Export Behavior

### Analysis: Native Share vs File Dialog

| Platform | Native Share API | Recommendation |
|----------|------------------|----------------|
| Windows 10/11 | `DataTransferManager` (UWP) | Complex, requires COM. Use file dialog. |
| Linux | None (desktop-dependent) | File dialog only |
| macOS | `NSSharingService` | Possible but complex. Use file dialog. |
| **Android** | `ACTION_SEND` Intent | ✅ Native share sheet |

**Decision:** 
- **Desktop:** Share button opens "Save As" file dialog for `.snbx`
- **Android:** Share button opens native share sheet

This is simpler, more reliable, and still achieves cross-platform compatibility.

---

## Launcher Button Layout (Final)

| # | Button | Desktop | Android |
|---|--------|---------|---------|
| 1 | New Paged Notebook | ✅ | ✅ |
| 2 | New Edgeless Notebook | ✅ | ✅ |
| 3 | Open PDF | ✅ | ✅ |
| 4 | Open Notebook (.snb folder) | ✅ | ❌ Hidden |
| 5 | **Import Package (.snbx)** | ✅ | ✅ |

---

## Architecture Discussion

### Q13: Where Should .snbx Handling Live?

**User's observation:** `DocumentManager` is the high-level orchestrator. `.snbx` handling should be there, not in `Document.cpp`.

**Analysis of current architecture:**

```
┌─────────────────────────────────────────────────────────────────┐
│                        DocumentManager                           │
│  (Orchestrator - owns documents, manages lifecycle)              │
├─────────────────────────────────────────────────────────────────┤
│  loadDocument(path)                                              │
│  ├── .pdf → Document::createForPdf()                            │
│  ├── .snb → Document::loadBundle()                              │
│  └── .snbx → ??? (NEW)                                          │
│                                                                  │
│  saveDocument(doc) → Document::saveBundle()                     │
│  exportPackage(doc) → ??? (NEW)                                 │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                          Document                                │
│  (Data model - pages, strokes, PDF info)                        │
├─────────────────────────────────────────────────────────────────┤
│  loadBundle(path) → reads document.json, loads pages            │
│  saveBundle(path) → writes document.json, saves pages           │
│  pdfPath, pdfHash, pdfSize                                      │
│  pdfRelativePath → NEW field                                    │
└─────────────────────────────────────────────────────────────────┘
```

**Recommended Split:**

| Component | Responsibility | Location |
|-----------|----------------|----------|
| `.snbx` extraction | Unzip to temp, return .snb path | `DocumentManager` or `NotebookImporter` |
| `.snbx` creation | ZIP .snb + optional PDF | `DocumentManager` or `NotebookExporter` |
| Dual path resolution | Try absolute, try relative, update both | `Document::loadBundle()` |
| PDF relink trigger | If both paths fail, show dialog | `DocumentManager::loadDocument()` |

**Proposed Flow:**

```
DocumentManager::loadDocument(path)
│
├── If ".snbx":
│   ├── NotebookImporter::extractPackage(path, destDir)
│   │   └── Returns extracted .snb folder path
│   └── Continue as .snb below ↓
│
├── If ".snb" or directory:
│   ├── Document::loadBundle(snbPath)
│   │   ├── Parse document.json
│   │   ├── Try pdf_path (absolute) → if exists, update pdf_relative_path
│   │   ├── Try pdf_relative_path (relative) → if exists, update pdf_path
│   │   └── If neither exists, set doc.needsPdfRelink = true
│   │
│   ├── If doc.needsPdfRelink:
│   │   ├── Show PdfRelinkDialog
│   │   └── Update both paths on success
│   │
│   └── Return document
│
└── If ".pdf":
    └── Document::createForPdf(...)
```

**Export Flow:**

```
DocumentManager::exportPackage(doc, destPath, includePdf)
│
├── NotebookExporter::createPackage(doc, destPath, includePdf)
│   ├── Create ZIP file
│   ├── Add .snb folder contents
│   ├── If includePdf and PDF exists:
│   │   ├── Copy PDF to embedded/
│   │   └── Update pdf_relative_path in document.json copy
│   └── Close ZIP
│
└── Return success/failure
```

**Answer:** Yes, you're correct! `.snbx` handling is a **preprocessing/postprocessing step** that belongs in `DocumentManager`. The dual path resolution stays in `Document::loadBundle()` since it's reading/writing the manifest.

---

## Updated Technical Plan

### Files to Create

```
source/sharing/
├── NotebookExporter.h/.cpp     # ZIP creation + PDF embedding
└── NotebookImporter.h/.cpp     # ZIP extraction

android/app-resources/src/org/speedynote/app/
├── ShareHelper.java            # Share intent + FileProvider
└── ImportHelper.java           # SAF picker for .snbx
```

### Files to Modify

```
source/core/DocumentManager.cpp # Add .snbx support to loadDocument(), add exportPackage()
source/core/DocumentManager.h   # Add exportPackage() declaration
source/core/Document.cpp        # Dual path resolution in loadBundle()
source/core/Document.h          # Add pdf_relative_path field, needsPdfRelink flag
source/ui/NavigationBar.cpp     # Connect share button (platform-specific)
source/ui/launcher/Launcher.cpp # Add import button
source/ui/launcher/FloatingActionButton.cpp  # 5th button
source/pdf/PdfRelinkDialog.cpp  # Android SAF support
android/app-resources/AndroidManifest.xml    # FileProvider, intent filters
```

### bundle_format_version

- Current: `1`
- After this feature: `2` (adds `pdf_relative_path`)
- Backward compatible: v1 documents work, just won't have relative path

---

## Remaining Questions

### Q11: Verify PDF Hash on Import?

When importing from `.snbx`, should we verify the embedded PDF matches `pdf_hash`?

| Option | Behavior |
|--------|----------|
| A | Always verify (security) |
| B | Never verify (speed) |
| C | Verify only if hash exists |

**Recommendation:** Option C - verify if hash exists, skip silently if not.

**Your preference?**
I agree with Option C. 
---

### Q12: What Happens to Old `pdf_path` on Import?

When importing on a new device, the old `pdf_path` is invalid. Should we:

| Option | Behavior |
|--------|----------|
| A | Keep the old path (for reference/debugging) |
| B | Clear it (set to empty string) |
| C | Replace with new absolute path immediately |

**Recommendation:** Option C - replace with new absolute path after extraction.

**Your preference?**
I agree with Option C. 
---



## Updated Effort Estimate

| Phase | Complexity | Estimated Time |
|-------|------------|----------------|
| Phase 0: Dual path system in Document | Medium | 1-2 hours |
| Phase 1: Export/Share (cross-platform) | Medium | 3-4 hours |
| Phase 2: Import (cross-platform) | Medium | 3-4 hours |
| Phase 3: PDF Relink on Android | Low | 30 min (reuses PdfFileHelper.java) |
| Phase 4: Intent Handling | Low | 1 hour |

**Total:** ~10-12 hours

---

# Implementation Plan

## Phase 0: Dual PDF Path System (Foundation)

**Goal:** Add `pdf_relative_path` field and path resolution logic to Document.

**Estimated Time:** 1-2 hours

### Step 0.1: Update Document.h

Add new member variables:

```cpp
// In Document class (private section)
QString m_pdfRelativePath;  // Relative path to PDF (from document.json location)
bool m_needsPdfRelink = false;  // True if PDF not found at either path
```

Add public accessors:

```cpp
// PDF path accessors
QString pdfRelativePath() const;
void setPdfRelativePath(const QString& path);
bool needsPdfRelink() const;
void clearNeedsPdfRelink();
```

### Step 0.2: Update Document.cpp - loadBundle()

Modify the PDF path loading logic:

```cpp
// In Document::loadBundle(), after reading document.json:

// Read both paths
m_pdfPath = root["pdf_path"].toString();
m_pdfRelativePath = root["pdf_relative_path"].toString();

// Path resolution logic
QString bundleDir = QFileInfo(manifestPath).absolutePath();
bool absoluteExists = !m_pdfPath.isEmpty() && QFile::exists(m_pdfPath);
bool relativeExists = false;
QString resolvedRelativePath;

if (!m_pdfRelativePath.isEmpty()) {
    resolvedRelativePath = QDir(bundleDir).absoluteFilePath(m_pdfRelativePath);
    relativeExists = QFile::exists(resolvedRelativePath);
}

if (absoluteExists) {
    // Absolute path works - update relative path
    QDir bundleDir(QFileInfo(manifestPath).absolutePath());
    m_pdfRelativePath = bundleDir.relativeFilePath(m_pdfPath);
    // Note: Will be saved on next saveBundle()
} else if (relativeExists) {
    // Relative path works - update absolute path
    m_pdfPath = resolvedRelativePath;
    // Note: Will be saved on next saveBundle()
} else if (!m_pdfPath.isEmpty() || !m_pdfRelativePath.isEmpty()) {
    // Neither path works but PDF was expected
    m_needsPdfRelink = true;
}
```

### Step 0.3: Update Document.cpp - saveBundle()

Add `pdf_relative_path` to saved JSON:

```cpp
// In Document::saveBundle(), when writing document.json:
if (!m_pdfPath.isEmpty()) {
    root["pdf_path"] = m_pdfPath;
    
    // Calculate relative path from document.json location
    QDir bundleDir(bundlePath);
    QString relativePath = bundleDir.relativeFilePath(m_pdfPath);
    root["pdf_relative_path"] = relativePath;
}
```

### Step 0.4: Update bundle_format_version

```cpp
root["bundle_format_version"] = 2;  // Was 1
```

### Step 0.5: Test Phase 0

1. Create notebook with PDF → Save → Check document.json has both paths
2. Move .snb folder → Reopen → PDF loads via relative path
3. Move PDF file → Reopen → Shows relink dialog (needsPdfRelink = true)

---

## Phase 1: Export/Share

**Goal:** Create .snbx packages and share via platform-appropriate method.

**Estimated Time:** 3-4 hours

### Step 1.1: Create NotebookExporter class

**File:** `source/sharing/NotebookExporter.h`

```cpp
#pragma once

#include <QString>
#include <QObject>

class Document;

class NotebookExporter : public QObject {
    Q_OBJECT
public:
    struct ExportOptions {
        bool includePdf = false;
        QString destPath;  // Full path including .snbx extension
    };
    
    struct ExportResult {
        bool success = false;
        QString errorMessage;
        QString exportedPath;
        qint64 fileSize = 0;
    };
    
    static ExportResult exportPackage(Document* doc, const ExportOptions& options);
    
    // Get estimated size before export (for UI)
    static qint64 estimatePdfSize(Document* doc);
    
signals:
    void progressChanged(int percent);
};
```

**File:** `source/sharing/NotebookExporter.cpp`

Implementation using Qt's private ZIP classes or QuaZip:

1. Create temp directory
2. Copy .snb contents to temp
3. If includePdf: copy PDF to `embedded/` folder
4. Modify document.json copy to set `pdf_relative_path = "../embedded/filename.pdf"`
5. Create ZIP from temp directory
6. Clean up temp directory
7. Return result

### Step 1.2: Add CMake configuration

**File:** `CMakeLists.txt`

```cmake
# Add sharing module (uses miniz for ZIP, MIT license)
set(SHARING_SOURCES
    source/sharing/NotebookExporter.cpp
    source/sharing/NotebookImporter.cpp
    thirdparty/miniz/miniz.c
)

# Include miniz headers
include_directories(${CMAKE_SOURCE_DIR}/thirdparty/miniz)
```

> **Implementation Note (2026-01-16):** We use miniz (https://github.com/richgel999/miniz)
> instead of Qt's private `QZipWriter` API. Qt6::GuiPrivate requires private headers
> that are not available on all platforms (e.g., Linux desktop packages don't ship them).
> miniz is a public domain single-header C library that provides cross-platform ZIP support.

### Step 1.3: Create ShareHelper.java (Android only)

**File:** `android/app-resources/src/org/speedynote/app/ShareHelper.java`

```java
public class ShareHelper {
    public static void shareFile(Activity activity, String filePath, String mimeType) {
        File file = new File(filePath);
        Uri uri = FileProvider.getUriForFile(activity, 
            "org.speedynote.app.fileprovider", file);
        
        Intent shareIntent = new Intent(Intent.ACTION_SEND);
        shareIntent.setType(mimeType);
        shareIntent.putExtra(Intent.EXTRA_STREAM, uri);
        shareIntent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        
        activity.startActivity(Intent.createChooser(shareIntent, "Share Notebook"));
    }
}
```

### Step 1.4: Configure FileProvider

**File:** `android/app-resources/res/xml/file_paths.xml`

```xml
<?xml version="1.0" encoding="utf-8"?>
<paths>
    <files-path name="exports" path="exports/" />
    <cache-path name="temp" path="temp/" />
</paths>
```

**File:** `android/app-resources/AndroidManifest.xml` (add inside <application>)

```xml
<provider
    android:name="androidx.core.content.FileProvider"
    android:authorities="org.speedynote.app.fileprovider"
    android:exported="false"
    android:grantUriPermissions="true">
    <meta-data
        android:name="android.support.FILE_PROVIDER_PATHS"
        android:resource="@xml/file_paths" />
</provider>
```

### Step 1.5: Create Export Dialog

**File:** `source/sharing/ExportDialog.h/.cpp`

Simple dialog with:
- Checkbox: "Include PDF (adds X MB)"
- Progress bar (indeterminate)
- Export/Cancel buttons

### Step 1.6: Connect Share Button in NavigationBar

**File:** `source/ui/NavigationBar.cpp`

```cpp
connect(m_shareButton, &ActionButton::clicked, this, [this]() {
#ifdef Q_OS_ANDROID
    // Show export dialog, then call ShareHelper via JNI
    emit exportRequested();
#else
    // Show "Save As" dialog for .snbx file
    emit exportRequested();
#endif
});
```

**File:** `source/MainWindow.cpp`

Add slot to handle export:

```cpp
void MainWindow::onExportRequested() {
    Document* doc = currentDocument();
    if (!doc) return;
    
    ExportDialog dialog(doc, this);
    if (dialog.exec() != QDialog::Accepted) return;
    
#ifdef Q_OS_ANDROID
    // Export to cache, then share
    QString cachePath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QString exportPath = cachePath + "/exports/" + doc->name + ".snbx";
    
    auto result = NotebookExporter::exportPackage(doc, {dialog.includePdf(), exportPath});
    if (result.success) {
        // Call ShareHelper.shareFile via JNI
        QJniObject::callStaticMethod<void>(
            "org/speedynote/app/ShareHelper",
            "shareFile",
            "(Landroid/app/Activity;Ljava/lang/String;Ljava/lang/String;)V",
            QNativeInterface::QAndroidApplication::context(),
            QJniObject::fromString(exportPath).object<jstring>(),
            QJniObject::fromString("application/octet-stream").object<jstring>()
        );
    }
#else
    // Desktop: Show save dialog
    QString destPath = QFileDialog::getSaveFileName(this, 
        tr("Export Notebook"), 
        QDir::homePath() + "/" + doc->name + ".snbx",
        tr("SpeedyNote Package (*.snbx)"));
    
    if (!destPath.isEmpty()) {
        auto result = NotebookExporter::exportPackage(doc, {dialog.includePdf(), destPath});
        if (!result.success) {
            QMessageBox::warning(this, tr("Export Failed"), result.errorMessage);
        }
    }
#endif
}
```

### Step 1.7: Test Phase 1

1. Export notebook without PDF → Check .snbx is valid ZIP with .snb inside
2. Export notebook with PDF → Check embedded/ folder contains PDF
3. Android: Share button opens share sheet
4. Desktop: Save dialog creates .snbx file

---

## Phase 2: Import

**Goal:** Import .snbx packages from file picker or share intent.

**Estimated Time:** 3-4 hours

### Step 2.1: Create NotebookImporter class

**File:** `source/sharing/NotebookImporter.h`

```cpp
#pragma once

#include <QString>
#include <QObject>

class NotebookImporter : public QObject {
    Q_OBJECT
public:
    struct ImportResult {
        bool success = false;
        QString errorMessage;
        QString extractedSnbPath;  // Path to extracted .snb folder
        QString embeddedPdfPath;   // Path to extracted PDF (if any)
    };
    
    // Extract .snbx to destination directory
    // Returns path to the .snb folder inside
    static ImportResult importPackage(const QString& snbxPath, const QString& destDir);
    
    // Generate unique name if duplicate exists
    static QString resolveNameConflict(const QString& baseName, const QString& destDir);
    
signals:
    void progressChanged(int percent, const QString& status);
};
```

**File:** `source/sharing/NotebookImporter.cpp`

1. Open ZIP file
2. Find .snb folder inside
3. Check for name conflicts → auto-rename if needed
4. Extract .snb to destDir
5. If embedded/ folder exists, extract PDF
6. Return paths

### Step 2.2: Create ImportHelper.java (Android only)

**File:** `android/app-resources/src/org/speedynote/app/ImportHelper.java`

Similar to PdfFileHelper:
- Opens SAF file picker for .snbx files
- Copies selected file to app-private storage
- Calls back to C++ with local path

### Step 2.3: Update DocumentManager

**File:** `source/core/DocumentManager.cpp`

Add to `loadDocument()`:

```cpp
// Handle .snbx packages
if (suffix == "snbx") {
    // Determine destination
#ifdef Q_OS_ANDROID
    QString destDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/notebooks";
#else
    // On desktop, extract next to the .snbx file
    QString destDir = fileInfo.absolutePath();
#endif
    
    auto importResult = NotebookImporter::importPackage(path, destDir);
    if (!importResult.success) {
        qWarning() << "DocumentManager: Failed to import .snbx:" << importResult.errorMessage;
        return nullptr;
    }
    
    // Now load the extracted .snb
    return loadDocument(importResult.extractedSnbPath);
}
```

### Step 2.4: Add Import Button to Launcher

**File:** `source/ui/launcher/FloatingActionButton.cpp`

Add 5th button:
- Icon: import or package icon
- Text: "Import Package"
- Action: emit `importPackageRequested()`

Hide "Open Notebook" button on Android:

```cpp
#ifdef Q_OS_ANDROID
    m_openNotebookButton->setVisible(false);
#endif
```

### Step 2.5: Connect Import Signal

**File:** `source/ui/launcher/Launcher.cpp`

```cpp
connect(m_fab, &FloatingActionButton::importPackageRequested, this, [this]() {
#ifdef Q_OS_ANDROID
    // Use ImportHelper to pick .snbx file
    pickSnbxFileAndroid();
#else
    // Use QFileDialog
    QString path = QFileDialog::getOpenFileName(this,
        tr("Import Notebook Package"),
        QDir::homePath(),
        tr("SpeedyNote Package (*.snbx)"));
    
    if (!path.isEmpty()) {
        emit notebookSelected(path);  // DocumentManager handles .snbx
    }
#endif
});
```

### Step 2.6: Test Phase 2

1. Desktop: Import .snbx → Extracted .snb folder appears, notebook opens
2. Desktop: Import .snbx with duplicate name → Auto-renamed to "Notebook (1)"
3. Android: Import .snbx → Copied to sandbox, extracted, opens
4. Import with embedded PDF → PDF loads via relative path

---

## Phase 3: PDF Relink on Android

**Goal:** Make PdfRelinkDialog work with Android SAF.

**Estimated Time:** 30 minutes

> **Simplification Note (2026-01-16):** Originally planned to create a separate 
> `PdfRelinkHelper.java`, but we can **reuse the existing `PdfFileHelper.java`** 
> from BUG-A003. The SAF picker + copy-to-sandbox logic is identical - only the
> callback target differs. This reduces code duplication and implementation time.

### Step 3.1: Add Android JNI Integration to PdfRelinkDialog

**File:** `source/pdf/PdfRelinkDialog.cpp`

Add the same JNI pattern used in `MainWindow.cpp` for PDF picking:

```cpp
#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QEventLoop>
#include <jni.h>

namespace {
    static QString s_relinkPdfPath;
    static bool s_relinkCancelled = false;
    static QEventLoop* s_relinkLoop = nullptr;
}

// JNI callbacks - reuse PdfFileHelper's native methods
extern "C" JNIEXPORT void JNICALL
Java_org_speedynote_app_PdfFileHelper_onPdfFilePickedForRelink(JNIEnv *env, jclass, jstring path)
{
    const char* pathChars = env->GetStringUTFChars(path, nullptr);
    s_relinkPdfPath = QString::fromUtf8(pathChars);
    env->ReleaseStringUTFChars(path, pathChars);
    s_relinkCancelled = false;
    if (s_relinkLoop) s_relinkLoop->quit();
}

extern "C" JNIEXPORT void JNICALL
Java_org_speedynote_app_PdfFileHelper_onPdfPickCancelledForRelink(JNIEnv*, jclass)
{
    s_relinkPdfPath.clear();
    s_relinkCancelled = true;
    if (s_relinkLoop) s_relinkLoop->quit();
}

QString PdfRelinkDialog::pickPdfFileAndroid()
{
    s_relinkPdfPath.clear();
    s_relinkCancelled = false;
    
    QString destDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/pdfs";
    QDir().mkpath(destDir);
    
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    QJniObject::callStaticMethod<void>(
        "org/speedynote/app/PdfFileHelper",
        "pickPdfFile",
        "(Landroid/app/Activity;Ljava/lang/String;)V",
        activity.object<jobject>(),
        QJniObject::fromString(destDir).object<jstring>()
    );
    
    QEventLoop loop;
    s_relinkLoop = &loop;
    loop.exec();
    s_relinkLoop = nullptr;
    
    return s_relinkCancelled ? QString() : s_relinkPdfPath;
}
#endif
```

**Wait - Issue Identified:** The JNI callback function names are tied to the Java native method declarations. We can't have two different C++ functions for the same Java method.

**Revised Approach:** Use a **request context flag** to distinguish callers, OR simply call the existing `MainWindow::pickPdfFileAndroid()` if it's accessible.

**Simplest Solution:** Make the PDF picker a shared utility that both `MainWindow` and `PdfRelinkDialog` can use:

1. Move `pickPdfFileAndroid()` and JNI callbacks to a shared location (e.g., `PdfPickerAndroid.cpp`)
2. Both `MainWindow` and `PdfRelinkDialog` call this shared function
3. Return the picked path, caller handles the result

### Step 3.2: Update PdfRelinkDialog for Android

**File:** `source/pdf/PdfRelinkDialog.cpp`

```cpp
void PdfRelinkDialog::onLocatePdf() {
    QString newPath;
    
#ifdef Q_OS_ANDROID
    // Use shared Android PDF picker (reuses PdfFileHelper.java)
    newPath = pickPdfFileAndroid();  // Shared utility function
#else
    // Existing QFileDialog code
    newPath = QFileDialog::getOpenFileName(this, 
        tr("Locate PDF"), 
        QDir::homePath(), 
        tr("PDF Files (*.pdf)"));
#endif

    if (!newPath.isEmpty()) {
        // Update document with new PDF path
        m_document->relinkPdf(newPath);
        accept();  // Close dialog on success
    }
}
```

### Step 3.3: Test Phase 3

1. Import `.snbx` without embedded PDF → Verify relink dialog appears
2. Tap "Locate PDF" → SAF picker opens
3. Select PDF → File copied to sandbox, document loads with PDF
4. Verify PDF path saved correctly in `document.json`

---

## Phase 4: Intent Handling (Optional Enhancement)

**Goal:** Handle "Open with SpeedyNote" for .snbx files.

**Estimated Time:** 1 hour

### Step 4.1: Register MIME type

**File:** `android/app-resources/AndroidManifest.xml`

```xml
<activity android:name="org.speedynote.app.SpeedyNoteActivity" ...>
    <intent-filter>
        <action android:name="android.intent.action.VIEW" />
        <category android:name="android.intent.category.DEFAULT" />
        <data android:mimeType="application/octet-stream" />
        <data android:pathPattern=".*\\.snbx" />
    </intent-filter>
</activity>
```

### Step 4.2: Handle Intent in SpeedyNoteActivity

**File:** `android/app-resources/src/org/speedynote/app/SpeedyNoteActivity.java`

```java
@Override
public void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);
    sInstance = this;
    
    // Check for incoming intent
    Intent intent = getIntent();
    if (Intent.ACTION_VIEW.equals(intent.getAction())) {
        Uri uri = intent.getData();
        if (uri != null) {
            // Copy to local and notify C++
            handleIncomingFile(uri);
        }
    }
}
```

### Step 4.3: Test Phase 4

1. Receive .snbx via Bluetooth/email
2. Tap file → "Open with SpeedyNote" option appears
3. SpeedyNote opens with imported notebook

---

## Implementation Checklist

### Phase 0: Dual Path System
- [ ] Add `pdf_relative_path` to Document.h
- [ ] Add `needsPdfRelink` flag to Document.h
- [ ] Update Document::loadBundle() with path resolution
- [ ] Update Document::saveBundle() to write relative path
- [ ] Bump bundle_format_version to 2
- [ ] Test: Move notebook, PDF still loads
- [ ] Test: Delete PDF, relink dialog appears

### Phase 1: Export/Share
- [ ] Create NotebookExporter class
- [ ] Create ExportDialog
- [ ] Create ShareHelper.java
- [ ] Configure FileProvider
- [ ] Connect share button in NavigationBar
- [ ] Add export handler in MainWindow
- [ ] Test: Export without PDF
- [ ] Test: Export with PDF
- [ ] Test: Android share sheet

### Phase 2: Import
- [ ] Create NotebookImporter class
- [ ] Create ImportHelper.java
- [ ] Update DocumentManager::loadDocument() for .snbx
- [ ] Add import button to FloatingActionButton
- [ ] Hide "Open Notebook" on Android
- [ ] Connect import signals
- [ ] Test: Import on desktop
- [ ] Test: Import on Android
- [ ] Test: Duplicate name handling

### Phase 3: PDF Relink on Android (Simplified)
- [ ] Create shared Android PDF picker utility (or reuse existing)
- [ ] Update PdfRelinkDialog for Android SAF
- [ ] Test: Relink PDF on Android (import .snbx without PDF, locate via SAF)

### Phase 4: Intent Handling
- [ ] Register .snbx MIME type
- [ ] Handle ACTION_VIEW intent
- [ ] Test: Open .snbx from file manager

However, I noticed a very significant problem. 

Did you notice `source/core/DocumentManager.cpp/.h`? Do you think the double path update / snbx handling /  etc should happen there instead of inside Document.cpp? MainWindow just calls `DocumentManager`'s `loadDocument`. The `loadDocument` may need a few more helper functions to make it work. This .snbx handling feature should be very high level and handled there, right? 

