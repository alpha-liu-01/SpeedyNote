# SpeedyNote Android - Bug Tracking

## Build Information

| Property | Value |
|----------|-------|
| Build Type | Release |
| Qt Version | 6.7.2 |
| Target API | 34 |
| Min API | 26 |
| ABI | arm64-v8a |
| PDF Backend | MuPDF |
| Test Device | Samsung Galaxy Tab S6 Lite 2024 (Android 16 / API 36) |

---

## Critical Bugs (App Crashes / Data Loss)

### BUG-A001: Settings Panel Crash (Qt Framework Bug)
**Status:** 🟢 Fixed  
**Priority:** Critical  
**Category:** UI / Settings  
**Root Cause:** Qt 6.7.2 Android bug in keyboard handling

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
**Status:** 🔴 Open  
**Priority:** Critical  
**Category:** File I/O

**Description:**  
Documents are never saved to .snb folder bundles on Android.

**Steps to Reproduce:**  
1. Create new document or open existing
2. Draw strokes / make changes
3. Attempt to save
4. Close and reopen → Changes lost

**Expected:** Document should save to internal storage  
**Actual:** Save appears to fail silently or doesn't persist

**Possible Causes:**
- Android Scoped Storage restrictions (API 30+)
- Missing storage permissions in AndroidManifest
- Incorrect file paths for Android
- Bundle path not set correctly

**Notes:**  
_User to provide: Where is it trying to save? Any error dialogs?_

---

### BUG-A003: PDF Loading Fails
**Status:** 🔴 Open  
**Priority:** Critical  
**Category:** PDF / MuPDF

**Description:**  
PDF files cannot be loaded. App shows "relink" dialog, but relinking also fails.

**Steps to Reproduce:**  
1. Open a PDF file
2. → Shows "PDF not found, relink?" dialog
3. Try to relink → Fails again

**Expected:** PDF should load and render  
**Actual:** PDF loading fails, relink fails

**Possible Causes:**
- MuPDF integration issue
- File path encoding (Android content:// URIs vs file:// paths)
- MuPDF cannot open file due to permissions
- MuPdfProvider not correctly implemented

**Notes:**  
_User to provide: Is this opening from file manager or from within app?_

---

## Major Bugs (Functionality Broken)

### BUG-A004: Extreme Stroke Lag
**Status:** 🟠 Open  
**Priority:** High  
**Category:** Performance / Input

**Description:**  
Drawing strokes has huge lag. Sometimes strokes only appear after lifting the stylus.

**Symptoms:**
- Visible delay between stylus movement and stroke rendering
- Strokes may not appear until stylus is lifted
- Drawing feels unresponsive

**Possible Causes:**
1. **Event compression** - Qt compresses high-frequency touch events by default
2. **Software rendering fallback** - Not using GPU acceleration
3. **Main thread blocking** - Rendering blocking input processing
4. **Missing unbuffered dispatch** - Not requesting high-rate stylus input

**Fixes to Try:**

1. **Disable event compression** (in Main.cpp):
```cpp
#ifdef Q_OS_ANDROID
QCoreApplication::setAttribute(Qt::AA_CompressHighFrequencyEvents, false);
QCoreApplication::setAttribute(Qt::AA_SynthesizeMouseForUnhandledTabletEvents, false);
#endif
```

2. **Force GPU rendering** (environment variable or in code):
```cpp
qputenv("QSG_RHI_BACKEND", "opengl");
qputenv("QSG_RENDER_LOOP", "threaded");
```

3. **Request unbuffered dispatch** (requires Java/JNI):
```java
// In Activity or View
view.requestUnbufferedDispatch(motionEvent);
```

4. **Use threaded render loop** - Already using Qt Widgets, so this may help

**Notes:**  
- High-rate stylus input (240Hz) requires API 31+ and `requestUnbufferedDispatch()`
- Current community port was limited to 60Hz - this may be the same issue

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


