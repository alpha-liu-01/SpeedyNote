# SpeedyNote Android - Bug Tracking

## Build Information

| Property | Value |
|----------|-------|
| Build Type | Release |
| Qt Version | 6.9.3 ✅ (6.10.x has OpenGL deadlock, 6.7.x has keyboard crash) |
| Target API | 35 |
| Min API | 26 |
| ABI | arm64-v8a |
| NDK Version | r27 (27.2.12479018) |
| PDF Backend | MuPDF |
| Test Device | Samsung Galaxy Tab S6 Lite 2024 (Android 16 / API 36) |

## Bug Summary

| Bug | Description | Status |
|-----|-------------|--------|
| **BUG-A001** | Settings Panel Crash | ✅ Fixed (Qt 6.9.3) |
| **BUG-A002** | Document Never Saves | ✅ Fixed (app-private storage) |
| **BUG-A003** | PDF Loading Fails | ✅ Fixed (Java SAF handler + storage management) |
| **BUG-A004** | Extreme Stroke Lag | ✅ Fixed (Qt 6.9.3 + 240Hz unbuffered) |
| **BUG-A005** | Pinch-to-Zoom Unreliable | ✅ Fixed (2-finger TouchBegin handler) |
| **BUG-A006** | PDF Page Switch Crash | ✅ Fixed (multi-layer fix) |
| **BUG-A007** | Dark Mode Not Syncing | ✅ Fixed (JNI + Fusion style) |

**All critical bugs resolved!** 🎉

---

## Critical Bugs (App Crashes / Data Loss)

### BUG-A001: Settings Panel Crash (Qt Framework Bug)
**Status:** 🟢 Fixed (by Qt 6.9.3 upgrade)  
**Priority:** Critical  
**Category:** UI / Settings  
**Root Cause:** Qt 6.7.x Android bug in keyboard handling - resolved by upgrading to Qt 6.9.3

**Description:**  
Applying settings in the control panel causes immediate crash.

**Crash Log:**
```
java.lang.NullPointerException: Attempt to write to field 
'boolean org.qtproject.qt.android.QtEditText.m_optionsChanged' 
on a null object reference in method 
'void org.qtproject.qt.android.QtInputDelegate.lambda$resetSoftwareKeyboard$0$...()'
```

**Analysis:**  
This is a **Qt framework bug**, not a SpeedyNote bug. The crash occurs in:
- `QtInputDelegate.java:167` - `resetSoftwareKeyboard()` 
- Tries to access `QtEditText.m_optionsChanged` but `QtEditText` is null

**Trigger:**  
Any action that causes Qt to reset/hide the software keyboard while the text input widget is being destroyed or has lost focus. Likely triggered by:
- Closing dialogs with text inputs (QLineEdit, QSpinBox)
- Settings panel uses spin boxes / text fields that trigger keyboard reset on apply

**Workarounds to Try:**
1. **Upgrade Qt** - Check if Qt 6.8+ fixes this
2. **Avoid text inputs** - Use sliders/buttons instead of spinboxes on Android
3. **Defer dialog close** - `QTimer::singleShot(0, ...)` before closing dialog
4. **Hide keyboard manually** - Call `QGuiApplication::inputMethod()->hide()` before closing

**Related Qt Bug Reports:**
- Likely related to QTBUG-* (needs research)

**Steps to Reproduce:**  
1. Open SpeedyNote
2. Open settings/control panel
3. Change any setting (especially ones with text input)
4. Apply → Crash

**Fix Applied:**  
Added `done()` override to dialogs with text inputs to hide the virtual keyboard before closing:
- `ControlPanelDialog::done()` - hides keyboard via `QGuiApplication::inputMethod()->hide()`
- `ThicknessEditDialog::done()` - same fix

Files modified:
- `source/ControlPanelDialog.cpp` / `.h`
- `source/ui/widgets/ThicknessPresetButton.cpp` / `.h`

---

### BUG-A002: Document Never Saves
**Status:** ✅ Fixed  
**Priority:** Critical  
**Category:** File I/O

**Description:**  
Documents are never saved to .snb folder bundles on Android.

**Root Cause Analysis:**  
The error message showed:
```
content://com.android.externalstorage.documents/document/primary%3ADocuments%2FUntitled.snb
```

- `QFileDialog::getSaveFileName()` on Android returns `content://` URIs
- `.snb` bundles are **directories** requiring `QDir().mkpath()` operations
- Android's SAF (Storage Access Framework) only supports single-file access via `content://`
- **Result:** `QDir().mkpath(content://...)` fails → save fails

**Fix Applied:**  
On Android, save/load documents to **app-private storage** instead of SAF:
- Save path: `QStandardPaths::AppDataLocation/notebooks/*.snb`
- Uses `QInputDialog` for document naming instead of `QFileDialog`
- Open shows list of saved documents from app-private storage

**Files Modified:**
- `source/MainWindow.cpp`
  - `saveDocument()` - uses app-private storage on Android
  - `loadDocument()` - shows list dialog on Android
  - `loadFolderDocument()` - redirects to `loadDocument()` on Android

---

### BUG-A003: PDF Loading Fails
**Status:** ✅ Fixed  
**Priority:** Critical  
**Category:** PDF / MuPDF / Permissions

**Description:**  
PDF files cannot be loaded. Error: "Failed to read PDF file. Android may have denied access to this file."

**Root Cause Analysis:**  
1. Qt's `QFileDialog` returns `content://` URIs from Android's file picker
2. The SAF (Storage Access Framework) permission is **only valid inside the Activity result callback**
3. By the time our C++ code tries to use it, the permission context is lost
4. MuPDF (C library) cannot read `content://` URIs anyway

**Why Previous Attempts Failed:**
- JNI call to `ContentResolver.openInputStream()` failed because permission expired
- Qt processes the Intent result internally and discards the permission context
- Our code runs after Qt's callback, so permission is gone

**Solution Implemented:**  
Use a **custom Java Activity** that handles the file picker result and copies the file **while permission is still valid**:

```
Qt's QFileDialog (broken):
  Intent result → Qt processes → Returns URI string → Our code → ❌ Permission gone

Our solution (works):
  Intent result → Our Java callback → Copy file NOW → Return local path → ✅
```

**Files Added:**
- `android/app-resources/src/org/speedynote/app/PdfFileHelper.java`
  - Opens file picker with proper Intent flags
  - Copies file to local storage in `onActivityResult` callback
  - Calls back to C++ with local file path

- `android/app-resources/src/org/speedynote/app/SpeedyNoteActivity.java`
  - Custom Activity extending QtActivity
  - Forwards `onActivityResult` to `PdfFileHelper`

**Files Modified:**
- `android/app-resources/AndroidManifest.xml`
  - Changed activity class to `SpeedyNoteActivity`

- `source/MainWindow.cpp`
  - Added JNI callbacks: `onPdfFilePicked()`, `onPdfPickCancelled()`
  - Added `pickPdfFileAndroid()` - calls Java and waits for result
  - Updated `openPdfDocument()` - uses custom picker on Android

- `CMakeLists.txt`
  - Updated target SDK to 35

**Imported PDFs are stored at:**
```
/data/data/org.speedynote.app/files/pdfs/
```

**Storage Management:** ✅ Implemented & Tested

1. **PDF Deduplication:**
   - Before copying a PDF, checks if file with same name and size exists
   - If duplicate found, reuses existing file (no copy needed)
   - If name exists but size differs, generates unique name (e.g., `file_1.pdf`)

2. **Cleanup on Document Delete:**
   - When deleting a document from the Launcher
   - Checks if document has an imported PDF in the sandbox
   - Deletes the imported PDF along with the document bundle
   - Only deletes PDFs in `/files/pdfs/` - never touches user's original files

**Files for storage management:**
- `android/app-resources/src/org/speedynote/app/PdfFileHelper.java`
  - `getFileSize()` - gets file size for deduplication
  - `copyUriToLocal()` - implements deduplication logic
  
- `source/ui/launcher/Launcher.cpp` (wrapped with `#ifdef Q_OS_ANDROID`)
  - `findImportedPdfPath()` - finds PDF in sandbox from document.json
  - `deleteNotebook()` - deletes imported PDF with document

---

## Major Bugs (Functionality Broken)

### BUG-A004: Extreme Stroke Lag
**Status:** ✅ Fixed  
**Priority:** High  
**Category:** Performance / Input

**Description:**  
Drawing strokes has huge lag. Sometimes strokes only appear after lifting the stylus.

**Root Causes:**
1. Qt 6.7.x had internal input handling issues → Fixed by Qt 6.9.3
2. Android batches touch events at 60Hz by default → Fixed by `requestUnbufferedDispatch()`

**Symptoms (BEFORE fix):**
- Visible delay between stylus movement and stroke rendering
- Strokes may not appear until stylus is lifted
- Drawing feels unresponsive
- Stylus input capped at 60Hz despite 240Hz hardware

**Fixes Applied:**

**Fix 1: Qt Upgrade (major lag)**
- Upgraded Qt from 6.7.2 → 6.9.3
- Resolved the extreme lag where strokes wouldn't appear until stylus lifted

**Fix 2: Unbuffered Dispatch (60Hz → 240Hz)**
- Added `dispatchTouchEvent()` override in `SpeedyNoteActivity.java`
- Calls `requestUnbufferedDispatch()` on API 31+ (Android 12+)
- Tells Android to deliver events at hardware rate instead of batching

```java
// SpeedyNoteActivity.java
@Override
public boolean dispatchTouchEvent(MotionEvent event) {
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
        View contentView = findViewById(android.R.id.content);
        if (contentView != null) {
            contentView.requestUnbufferedDispatch(event);
        }
    }
    return super.dispatchTouchEvent(event);
}
```

**Files Modified:**
- `android/app-resources/src/org/speedynote/app/SpeedyNoteActivity.java`
  - Added `dispatchTouchEvent()` override with unbuffered dispatch

**Result:**
- Strokes appear immediately during drawing
- Full hardware stylus poll rate (up to 240Hz on supported devices)
- Smooth, responsive drawing experience

---

### BUG-A005: Pinch-to-Zoom Unreliable
**Status:** ✅ Fixed  
**Priority:** Medium  
**Category:** Touch Input / Gestures
**Platform:** Android only

**Description:**  
Pinch-to-zoom gestures only trigger about 1 in 5 times. Once triggered, the zoom works correctly until released.

**Root Cause:**  
The `TouchGestureHandler::handleTouchEvent()` only handled 1-finger and 3-finger cases in the `TouchBegin` event. On Android, when two fingers touch nearly simultaneously, the system may send a `TouchBegin` event with 2 points directly (instead of a 1-finger TouchBegin followed by a 2-finger TouchUpdate).

This left the pinch gesture uninitialized, causing subsequent `TouchUpdate` events to fail to start the zoom properly.

**Symptoms (BEFORE fix):**
- Pinch-to-zoom gesture often doesn't trigger
- Need to retry pinch gesture multiple times
- Once triggered, zoom works correctly until fingers are lifted

**Fix Applied:**
Added handling for 2-finger `TouchBegin` events in `TouchGestureHandler.cpp`:

```cpp
// TouchBegin handler - added 2-finger case
} else if (points.size() == 2) {
    // Two fingers touch simultaneously - start pinch directly
    // Common on Android where both fingers can arrive in same event
    if (m_mode == TouchGestureMode::YAxisOnly) {
        event->accept();
        return true;
    }
    
    const auto& p1 = points[0];
    const auto& p2 = points[1];
    
    QPointF pos1 = p1.position();
    QPointF pos2 = p2.position();
    QPointF centroid = (pos1 + pos2) / 2.0;
    qreal distance = QLineF(pos1, pos2).length();
    
    if (distance < 1.0) distance = 1.0;
    
    m_pinchActive = true;
    m_pinchStartZoom = m_viewport->zoomLevel();
    m_pinchStartDistance = distance;
    m_pinchCentroid = centroid;
    
    m_viewport->beginZoomGesture(centroid);
    
    event->accept();
    return true;
}
```

**Files Modified:**
- `source/core/TouchGestureHandler.cpp`
  - Added 2-finger `TouchBegin` handler to initialize pinch gesture directly

**Desktop Impact:** None - the fix is in shared code but desktop platforms typically don't send 2-finger TouchBegin events (they use a 1-finger then update pattern). The fix is additive and doesn't change existing behavior.

**Result:**
- Pinch-to-zoom now triggers reliably on first attempt
- Gesture system properly handles simultaneous 2-finger touch

---

### BUG-A006: PDF Page Switch Crash
**Status:** ✅ Fixed  
**Priority:** High  
**Category:** Stability / Threading
**Platform:** Primarily Android (ARM64)

**Description:**  
App crashes when switching pages in PDF documents, particularly during rapid page navigation using PageWheelPicker.

**Root Causes (Multiple):**

1. **Orphaned background renders**: `invalidatePdfCache()` didn't cancel active `QtConcurrent` render threads, leaving them running with stale PDF paths

2. **Signal flooding**: `PageWheelPicker` emitted `currentPageChanged` during every frame of scroll animation (60+ times/second), overwhelming the PDF system

3. **MuPDF thread-safety**: MuPDF's `fz_context` was not protected by mutex, allowing potential concurrent access corruption

4. **ARM64 alignment**: Direct `memcpy` on potentially unaligned MuPDF buffers caused SIGBUS on ARM64

**Symptoms (BEFORE fix):**
- Crashes when using PageWheelPicker to scroll pages
- SIGBUS/WINDOW DIED errors in logcat
- More frequent with rapid page switching
- Only on Android, not desktop

**Multi-Layer Fix Applied:**

**Layer 1: Cancel stale background renders**
```cpp
void DocumentViewport::invalidatePdfCache() {
    for (QFutureWatcher<QImage>* watcher : m_activePdfWatchers) {
        watcher->cancel();
    }
    m_activePdfWatchers.clear();
    // ...
}
```

**Layer 2: Debounce PageWheelPicker signals**
```cpp
void PageWheelPicker::updateFromOffset() {
    m_currentPage = qBound(...);  // Update display only
    // Do NOT emit signal - wait for snap to finish
}

void PageWheelPicker::onSnapFinished() {
    if (newPage != m_lastEmittedPage) {
        m_lastEmittedPage = newPage;
        emit currentPageChanged(m_currentPage);  // Only emit when stopped
    }
}
```

**Layer 3: MuPDF thread-safety**
```cpp
// MuPdfProvider.h
mutable fz_context* m_ctx;  // Mutable for const methods
mutable QMutex m_mutex;     // Thread protection

// MuPdfProvider.cpp
QImage MuPdfProvider::renderPageToImage(...) const {
    QMutexLocker locker(&m_mutex);  // Serialize all MuPDF calls
    // ...
}
```

**Layer 4: ARM64 safety checks**
```cpp
// Validate before memory operations
if (!samples || stride < width * 4) {
    fz_throw(m_ctx, FZ_ERROR_GENERIC, "Invalid pixmap data");
}
// Use memmove for alignment safety
memmove(dst, src, width * 4);
```

**Files Modified:**
- `source/core/DocumentViewport.cpp` - Cancel active watchers, check cancellation in callback
- `source/ui/widgets/PageWheelPicker.cpp/.h` - Debounce signal, add `m_lastEmittedPage`
- `source/pdf/MuPdfProvider.cpp/.h` - Mutex, mutable, safety checks, memmove

**Desktop Impact:** Also improves desktop stability with better thread safety.

**Result:**
- ✅ No more crashes during PDF page navigation
- ✅ PageWheelPicker scrolling is crash-free
- ✅ Background renders properly cancelled on page change
- ✅ MuPDF operations are thread-safe
- ✅ ARM64 memory alignment issues prevented

---

### BUG-A007: Dark Mode Not Syncing with Android System
**Status:** ✅ Fixed  
**Priority:** Medium  
**Category:** Theming / UI
**Platform:** Android only

**Description:**  
SpeedyNote's UI elements don't follow Android's system light/dark mode setting. When switching to light mode in Android settings, most UI elements remain dark, but some (like control panel tabs, text boxes) become light while their text stays light-colored, causing very low contrast and unreadable text.

**Root Causes:**

1. **No Android theme initialization**: `Main.cpp` had explicit palette setup for Windows but nothing for Android. Qt uses its default "android" style which doesn't consistently follow system theme.

2. **`isDarkMode()` detection fails on Android**: The function falls back to checking `QPalette::Window` color lightness, but Qt doesn't automatically sync its palette with Android's system theme, so this returns a fixed value regardless of system setting.

3. **Mixed native vs Qt widget styling**: Native Android dialogs follow system theme, but Qt widgets use Qt's internal palette, causing visual inconsistency.

**Symptoms (BEFORE fix):**
- Tab bars have light background but light text (invisible)
- Text boxes have wrong background/text color combinations
- Control panel tabs unreadable in light mode
- Inconsistent appearance between native and Qt widgets

**Fix Applied:**

**Layer 1: JNI Dark Mode Detection**
Added `isDarkMode()` static method to `SpeedyNoteActivity.java`:

```java
public static boolean isDarkMode() {
    Configuration config = sInstance.getResources().getConfiguration();
    int nightMode = config.uiMode & Configuration.UI_MODE_NIGHT_MASK;
    return (nightMode == Configuration.UI_MODE_NIGHT_YES);
}
```

**Layer 2: Android Palette Initialization**
Added `applyAndroidPalette()` in `Main.cpp`:
- Uses Fusion style (cross-platform, respects palette colors)
- Queries system dark mode via JNI
- Applies matching dark or light palette

```cpp
static void applyAndroidPalette(QApplication& app) {
    app.setStyle("Fusion");  // Respects palette colors
    
    if (isAndroidDarkMode()) {
        // Apply dark palette (same as Windows)
        QPalette darkPalette;
        // ... set all color roles ...
        app.setPalette(darkPalette);
    } else {
        // Apply light palette
        QPalette lightPalette;
        // ... set all color roles ...
        app.setPalette(lightPalette);
    }
}
```

**Layer 3: Update isDarkMode() for Android**
Modified `MainWindow::isDarkMode()` to call JNI on Android:

```cpp
bool MainWindow::isDarkMode() {
#ifdef Q_OS_WIN
    // Windows registry detection
#elif defined(Q_OS_ANDROID)
    // JNI call to SpeedyNoteActivity.isDarkMode()
    QJniObject result = QJniObject::callStaticMethod<jboolean>(
        "org/speedynote/app/SpeedyNoteActivity",
        "isDarkMode", "()Z");
    return result.isValid() && result.object<jboolean>();
#else
    // Linux palette detection
#endif
}
```

**Files Modified:**
- `android/app-resources/src/org/speedynote/app/SpeedyNoteActivity.java`
  - Added `sInstance` singleton for JNI access
  - Added `isDarkMode()` static method querying `Configuration.UI_MODE_NIGHT_MASK`
  
- `source/Main.cpp`
  - Added `isAndroidDarkMode()` JNI helper
  - Added `applyAndroidPalette()` with full dark/light palette definitions
  - Called `applyAndroidPalette()` at startup
  
- `source/MainWindow.cpp`
  - Added Android branch to `isDarkMode()` using JNI call

**Desktop Impact:** None - Windows and Linux behavior unchanged. All changes are wrapped in `#ifdef Q_OS_ANDROID`.

**Result:**
- ✅ All Qt widgets follow Android system theme
- ✅ Text is readable in both light and dark modes
- ✅ Control panel tabs have proper contrast
- ✅ Consistent appearance across all UI elements
- ✅ Theme is applied at app startup

---

## Minor Bugs

_(None reported yet)_

---

## Needs Investigation

### INV-A001: Android File Access Model
**Status:** 🔵 Investigation  

Android 10+ (API 30+) uses Scoped Storage which restricts file access:
- Apps can only access their own private directories freely
- External storage requires MediaStore or Storage Access Framework
- Direct file paths to external storage may not work

**Questions:**
- Where should SpeedyNote save .snb bundles on Android?
- How should PDF opening work? (content:// URI from file picker?)
- Do we need to implement SAF for "Open" and "Save As"?

---

### INV-A002: MuPDF Rendering Quality
**Status:** 🔵 Investigation  

Need to verify:
- Is MuPDF rendering PDFs correctly when it does work?
- Are the right libraries linked (libmupdf.a, libmupdf-third.a)?
- Is there a runtime initialization needed?

---

### INV-A003: Qt Touch/Stylus Input on Android
**Status:** 🔵 Investigation  

Need to verify:
- Is `QTabletEvent` being received or just `QTouchEvent`?
- Is pressure sensitivity working?
- Is palm rejection working?

---

## Root Cause Analysis

### 🚨 CRITICAL: Storage Permissions Are Wrong for API 36

**Current AndroidManifest permissions:**
```xml
<uses-permission android:name="android.permission.READ_EXTERNAL_STORAGE" android:maxSdkVersion="32"/>
<uses-permission android:name="android.permission.WRITE_EXTERNAL_STORAGE" android:maxSdkVersion="29"/>
```

**Problem:** Test device is **API 36** (Android 16), but:
- `READ_EXTERNAL_STORAGE` is capped at API 32 → **Not granted**
- `WRITE_EXTERNAL_STORAGE` is capped at API 29 → **Not granted**

**This explains:**
- ❌ BUG-A002 (Document never saves)
- ❌ BUG-A003 (PDF loading fails)

**Solutions for API 33+:**

| Approach | Description | Invasiveness |
|----------|-------------|--------------|
| **A. Scoped Storage (Recommended)** | Save to app-private dirs (`getFilesDir()`), use SAF for user files | Medium |
| **B. Media Permissions** | `READ_MEDIA_*` for media files (not documents) | N/A for docs |
| **C. MANAGE_EXTERNAL_STORAGE** | Full storage access (requires Play Store justification) | High |

**Recommended Fix:**
1. Save `.snb` bundles to app-private directory by default
2. Use Storage Access Framework (SAF) for "Open" and "Save As"
3. Handle `content://` URIs from file pickers instead of `file://` paths

---

## Environment Checklist

### Permissions (AndroidManifest.xml)
- [x] `READ_EXTERNAL_STORAGE` - Present but maxSdkVersion="32" ⚠️
- [x] `WRITE_EXTERNAL_STORAGE` - Present but maxSdkVersion="29" ⚠️
- [ ] `MANAGE_EXTERNAL_STORAGE` - NOT present (would need Play Store justification)
- [x] `INTERNET` - Present
- [x] `HIGH_SAMPLING_RATE_SENSORS` - Present (for stylus)

### Runtime Features
- [ ] Storage permission requested at runtime (moot - permission not granted on API 36)
- [x] Hardware acceleration enabled (`android:hardwareAccelerated="true"`)
- [ ] OpenGL ES working - needs verification
- [ ] Native libraries loading correctly - needs verification

---

## Testing Checklist

### Core Functionality
- [ ] Create new paged notebook
- [ ] Create new edgeless notebook
- [ ] Draw with stylus
- [ ] Draw with finger
- [ ] Eraser tool
- [ ] Color picker
- [ ] Stroke width
- [ ] Undo/Redo
- [ ] Save notebook
- [ ] Load notebook
- [ ] Open PDF
- [ ] Navigate PDF pages
- [ ] Zoom and pan

### Settings
- [ ] Open settings panel
- [ ] Change pen color
- [ ] Change background
- [ ] Apply settings (CRASH)
- [ ] Settings persist after restart

### File Operations
- [ ] Save new document
- [ ] Save existing document
- [ ] Open .snb bundle
- [ ] Open PDF from file picker
- [ ] Recent files

---

## Notes for Debugging

### Getting Logs from Device
```bash
# Clear logcat and capture SpeedyNote logs
adb logcat -c
adb logcat | grep -E "(SpeedyNote|Qt|Fatal|Error)"

# Or save to file
adb logcat > android_logs.txt
```

### Checking APK Contents
```bash
# List native libraries in APK
unzip -l SpeedyNote.apk | grep "\.so"

# Check if MuPDF symbols are included
adb shell "run-as org.speedynote.app ls /data/data/org.speedynote.app/lib/"
```

---

## Version History

| Date | Version | Notes |
|------|---------|-------|
| 2026-01-16 | 0.1-alpha | Initial Android build, multiple critical bugs |


