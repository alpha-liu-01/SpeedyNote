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
| **BUG-A008** | Stylus Eraser Not Working | ✅ Fixed (JNI tool type detection) |
| **BUG-A009** | CJK Font Rendering Incorrect | ✅ Fixed (locale-aware font fallback) |

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
/data/data/org.speedynote.app/files/pdfs/           # Direct PDF imports via SAF
/data/data/org.speedynote.app/files/notebooks/embedded/  # PDFs from .snbx packages
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
   - Handles both PDF storage locations:
     - `/files/pdfs/` - PDFs imported directly via SAF file picker
     - `/files/notebooks/embedded/` - PDFs extracted from .snbx packages (Phase 2)
   - Never touches user's original files outside sandbox

**Files for storage management:**
- `android/app-resources/src/org/speedynote/app/PdfFileHelper.java`
  - `getFileSize()` - gets file size for deduplication
  - `copyUriToLocal()` - implements deduplication logic
  
- `source/ui/launcher/Launcher.cpp` (wrapped with `#ifdef Q_OS_ANDROID`)
  - `findImportedPdfPath()` - finds PDF in sandbox from document.json (checks both `/pdfs/` and `/embedded/`)
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
**Status:** ✅ Fixed (v7)  
**Priority:** Medium  
**Category:** Touch Input / Gestures
**Platform:** Android only

**Description:**  
Pinch-to-zoom gestures only trigger about 1 in 5 times. Once triggered, the zoom works correctly until released.

**Root Cause (Original - Partially Fixed):**  
The `TouchGestureHandler::handleTouchEvent()` only handled 1-finger and 3-finger cases in the `TouchBegin` event. On Android, when two fingers touch nearly simultaneously, the system may send a `TouchBegin` event with 2 points directly.

**Root Cause (v2 - Improved):**  
The original fix improved reliability from ~30% to ~70%, but three additional issues remained:

1. **Released points counted as active**: `points.size()` includes ALL touch points, including those with state `Released`. When a finger lifts, the point is still in the event but with `Released` state. The code was counting released points as active, causing:
   - Pinch update trying to use a released point's position
   - State transitions not triggering correctly (2→1 finger transition missed)

2. **No 2→1 finger transition handling**: When one finger lifts during a pinch, the code had no path to transition back to pan with the remaining finger. The gesture would get stuck.

3. **Inconsistent state flags**: When 2-finger `TouchBegin` happened, `m_panActive` wasn't explicitly cleared, potentially leaving conflicting state.

**Root Cause (v3 - Improved Further):**  
The v2 fix improved reliability to ~60%, but open beta testing revealed ~40% failure rate persisted. The issue: **Android may not include all active touch points in every event**.

When two fingers touch nearly simultaneously on Android:
1. First finger → `TouchBegin` with 1 point → pan starts
2. Second finger arrives, but `TouchUpdate` may only contain the newly pressed point (second finger) and not the stationary first finger
3. Per-event counting sees only 1 active point, missing the second finger entirely

**Root Cause (v4 - Fixed stale state issues):**  
Spurious `TouchCancel` events were generated when input focus changed (e.g., stylus hovering to toolbar), and when returning from launcher. These caused corrupted tracking state. Fixed with:
- Defensive reset of `m_trackedTouchIds` on `TouchBegin`
- `DocumentViewport::hideEvent()` to clear gesture state when viewport is hidden

**Root Cause (v5 - Fixed post-sleep/wake behavior):**  
After device sleep/wake, Android sends `TouchUpdate` events with **only one point** (the finger that moved), even when two fingers are down. The v3/v4 fixes tracked the correct finger count, but pinch **updates** still required `activePoints.size() >= 2` to calculate the zoom. Since only one point was in the event, updates were skipped entirely:

```
// Log when broken (after sleep/wake):
beginZoomGesture STARTED
endZoomGesture           // NO updates in between!

// Log when working:
beginZoomGesture STARTED
Pinch update #N          // Updates happen
updateZoomGesture scale: X.XX
endZoomGesture
```

**Fix Applied (v5):**
Cache last-known positions for each touch point ID. When an update event only contains 1 point, use the cached position for the other finger:

```cpp
// Member variable
QHash<int, QPointF> m_lastTouchPositions;

// In handleTouchEvent - track positions:
for (const auto& pt : points) {
    if (pt.state() == QEventPoint::Pressed) {
        m_trackedTouchIds.insert(pt.id());
        m_lastTouchPositions.insert(pt.id(), pt.position());
    } else if (pt.state() == QEventPoint::Released) {
        m_trackedTouchIds.remove(pt.id());
        m_lastTouchPositions.remove(pt.id());
    } else {
        // Updated or Stationary - cache latest position
        m_lastTouchPositions.insert(pt.id(), pt.position());
    }
}

// In pinch update - use cached positions when event is incomplete:
if (m_activeTouchPoints == 2 && m_pinchActive) {
    QPointF pos1, pos2;
    // Get from event...
    // If only 1 position in event, get other from m_lastTouchPositions
}
```

Key improvements (v5):
- **Position caching**: `QHash<int, QPointF> m_lastTouchPositions` stores last known position for each finger
- **Fallback lookup**: When event has only 1 point, retrieve other position from cache
- **No waiting required**: Pinch updates proceed immediately using cached data instead of being skipped

**Files Modified:**
- `source/core/TouchGestureHandler.h`
  - Added `#include <QHash>`
  - Added `QHash<int, QPointF> m_lastTouchPositions` member

- `source/core/TouchGestureHandler.cpp`
  - Updated ID tracking to also cache positions
  - Rewrote pinch update block to use cached positions as fallback
  - Clear `m_lastTouchPositions` alongside `m_trackedTouchIds` in all reset paths

**Desktop Impact:** None - desktop platforms deliver complete touch data. The fix improves robustness without changing behavior.

**Root Cause (v6 - Fixed stale ID and position issues):**  
After sleep/wake, two issues persisted:

1. **Single finger causing zoom**: When one finger lifted, Android sometimes didn't send a `Released` event. The ID stayed in `m_trackedTouchIds`, so `m_activeTouchPoints` was still 2 even with only 1 finger down. This caused single-finger movements to trigger zoom calculations.

2. **Stale cached positions**: The `m_lastTouchPositions` cache held the old position of the "phantom" finger that had already lifted. Zoom calculations used this stale position, causing erratic zoom behavior.

Log evidence showing the problem:
```
[TouchGestureHandler] Pinch update # 3441 activePoints: 1 cachedPositions: 2
```
This shows the current event only had 1 active point, but the cache still had 2 positions - one of which was stale.

**Fix Applied (v6):**
Detect and remove stale touch IDs that Android "forgot" to send `Released` for:

```cpp
// New member variable
QHash<int, int> m_touchIdMissingCount;  // Count consecutive events where ID was missing
static constexpr int MAX_MISSING_COUNT = 3;  // Remove after 3 consecutive misses

// In handleTouchEvent:
QSet<int> idsSeenThisEvent;
for (const auto& pt : points) {
    idsSeenThisEvent.insert(pt.id());
    // ... existing tracking logic ...
    m_touchIdMissingCount.remove(pt.id());  // Reset counter if seen
}

// Detect stale IDs
for (int trackedId : m_trackedTouchIds) {
    if (!idsSeenThisEvent.contains(trackedId)) {
        m_touchIdMissingCount[trackedId]++;
        if (m_touchIdMissingCount[trackedId] >= MAX_MISSING_COUNT) {
            // Remove stale ID from tracking and cache
            m_trackedTouchIds.remove(trackedId);
            m_lastTouchPositions.remove(trackedId);
        }
    }
}

m_activeTouchPoints = m_trackedTouchIds.size();  // Now accurate
```

Key improvements (v6):
- **Stale detection**: Track how many consecutive events each ID was missing from
- **Automatic cleanup**: After 3 consecutive misses, assume finger lifted and remove ID
- **Cache sync**: Stale positions removed alongside stale IDs
- **Proper transitions**: When finger count drops from 2→1, pinch ends and pan starts correctly

**Files Modified:**
- `source/core/TouchGestureHandler.h`
  - Added `QHash<int, int> m_touchIdMissingCount` member
  - Added `MAX_MISSING_COUNT = 3` constant

- `source/core/TouchGestureHandler.cpp`
  - Track which IDs are seen in each event
  - Increment missing count for IDs not seen
  - Remove stale IDs after 3 consecutive misses
  - Clear `m_touchIdMissingCount` in all reset paths

**Result (v6):**
- No more "phantom finger" causing single-finger zoom
- Stale cached positions automatically cleaned up
- Proper 2→1 finger transition when one finger lifts without `Released` event
- Reliable pinch-to-zoom even after sleep/wake cycles

**Root Cause (v7 - Fixed premature stale detection):**
The v6 event-count-based stale detection was too aggressive. When a second finger touches for pinch but stays **stationary**, Android only sends data for the moving first finger. The stationary finger was removed as "stale" after just 3 events (~30ms):

```
[TouchGestureHandler] Starting pinch (1→2 transition) trackedIds: 2
[DocumentViewport] beginZoomGesture STARTED
[TouchGestureHandler] Removing stale touch ID 2 (missing for 3 consecutive events)  // 30ms later!
[DocumentViewport] endZoomGesture
```

This caused pinch gestures to immediately fail when the second finger was stationary.

**Fix Applied (v7):**
Switched from event-count-based to **time-based** stale detection:

```cpp
// Old (v6) - too aggressive:
QHash<int, int> m_touchIdMissingCount;
static constexpr int MAX_MISSING_COUNT = 3;  // Removed after 3 events!

// New (v7) - time-based:
QHash<int, qint64> m_touchIdLastSeenTime;  // Timestamp when last seen
static constexpr qint64 STALE_TIMEOUT_MS = 500;  // Removed after 500ms

// In handleTouchEvent:
qint64 currentTime = QDateTime::currentMSecsSinceEpoch();

for (const auto& pt : points) {
    // Update timestamp for seen points
    m_touchIdLastSeenTime.insert(pt.id(), currentTime);
}

// Check for stale IDs
for (int trackedId : m_trackedTouchIds) {
    qint64 elapsed = currentTime - m_touchIdLastSeenTime.value(trackedId, 0);
    if (elapsed > STALE_TIMEOUT_MS) {
        // Remove stale ID
    }
}
```

Key improvements (v7):
- **Time-based detection**: 500ms timeout instead of 3 events
- **Stationary fingers preserved**: A finger not moving won't be removed until 500ms with no data
- **Still catches phantom fingers**: After sleep/wake, if a finger truly lifted without Released event, it gets cleaned up after 500ms

**Files Modified:**
- `source/core/TouchGestureHandler.h`
  - Changed `QHash<int, int> m_touchIdMissingCount` to `QHash<int, qint64> m_touchIdLastSeenTime`
  - Changed `MAX_MISSING_COUNT = 3` to `STALE_TIMEOUT_MS = 500`

- `source/core/TouchGestureHandler.cpp`
  - Added `#include <QDateTime>`
  - Use `QDateTime::currentMSecsSinceEpoch()` for timestamps
  - Time-based stale check instead of event-count-based

**Result (v7):**
- Stationary second finger during pinch is no longer prematurely removed
- Pinch gestures work even when one finger is stationary
- Phantom fingers still cleaned up after 500ms timeout

**Root Cause (v7b - Fixed rapid start/end after tab switch):**
After `hideEvent` (tab switch), Android could send spurious `Released` events for old touch IDs. This immediately dropped `m_activeTouchPoints` to 1, triggering the 2→1 transition that ended the pinch:

```
19:39:16.027: Starting pinch trackedIds: 2
19:39:16.070: endZoomGesture  // 43ms later, NO stale removal - immediate transition!
19:39:16.118: Starting pinch
19:39:16.179: endZoomGesture  // Another immediate end
```

The rapid start/end cycle happened because each new pinch attempt was immediately cancelled by the 2→1 transition.

**Fix Applied (v7b):**
Made pinch mode "sticky" - don't immediately transition to pan when finger count drops to 1:

```cpp
// 2→1 finger transition block
if (m_activeTouchPoints == 1 && !m_panActive && !activePoints.isEmpty()) {
    // BUG-A005 v7b: If pinch is active, DON'T immediately transition to pan.
    // After hideEvent (tab switch), Android may send spurious Released events
    // that temporarily drop finger count to 1.
    if (m_pinchActive) {
        // Don't end pinch, just skip this update
        // Pinch will resume if second finger comes back
        event->accept();
        return true;
    }
    // ... start pan only if NOT coming from pinch ...
}
```

Additionally, when the stale timeout (500ms) legitimately removes a phantom finger during pinch, we now properly end the pinch and allow pan transition:

```cpp
// After stale removal
if (staleRemovalDuringPinch && m_activeTouchPoints == 1 && m_pinchActive) {
    endTouchPinch();  // Now confident the finger is truly gone
    // Pan transition happens in 2→1 block since m_pinchActive is now false
}
```

**Result (v7b):**
- Pinch is "sticky" - transient 1-finger states don't immediately end the pinch
- After tab switch, pinch gestures remain stable
- Stale timeout (500ms) still properly cleans up phantom fingers
- When stale removal happens, proper pinch→pan transition occurs
- Still unreliable after certain tab switches

---

### BUG-A005 v8: Complete Redesign - Gallery Style Gestures

**Root Cause (Final):**
After extensive debugging, we identified that the fundamental problem is the **1→2 finger transition**. No matter how much caching or stale detection we added, the core design required tracking transitions between finger counts - which Android's unreliable touch event reporting made impossible to do reliably.

**Solution: Complete Architecture Redesign**

Replaced the old gesture system with a smartphone gallery-style approach:

| Fingers | Old Design | New Design |
|---------|------------|------------|
| 1 finger | Pan (navigation) | Tool mode (drawing/eraser) - passed through |
| 2 fingers | Zoom only | Pan + Zoom simultaneously |
| 3+ fingers | Tap detection | Same (tap detection) |

**Key Principles:**
1. **No transitions to track** - 1 finger always = tool, 2 fingers always = pan+zoom
2. **No position caching** - Only process when BOTH fingers have valid data
3. **Simultaneous pan+zoom** - Like Google Maps/Photos app
4. **Simple state machine** - Just `Idle ↔ GestureActive`

**Files Changed:**
- `source/core/TouchGestureHandler.h` - Complete rewrite
- `source/core/TouchGestureHandler.cpp` - Complete rewrite  
- `source/core/TouchGestureHandlerLegacy.h/cpp` - Old code preserved for reference

**New Code (simplified):**
```cpp
// Only process 2-finger gestures when we have BOTH points
if (fingerCount == 2) {
    QPointF p1 = activePoints[0]->position();
    QPointF p2 = activePoints[1]->position();
    QPointF centroid = (p1 + p2) / 2.0;
    qreal distance = QLineF(p1, p2).length();
    
    if (!m_gestureActive) {
        // Start both pan AND zoom
        m_viewport->beginPanGesture();
        m_viewport->beginZoomGesture(centroid);
        m_gestureActive = true;
    } else {
        // Update both simultaneously
        QPointF panDelta = centroid - m_lastCentroid;
        qreal scale = distance / m_lastDistance;
        m_viewport->updatePanGesture(panDelta);
        m_viewport->updateZoomGesture(scale, centroid);
    }
}

// 1 finger - pass through to tool handling
if (fingerCount == 1) {
    if (m_gestureActive) {
        endGesture(true);  // End with inertia
    }
    return false;  // Let viewport handle as drawing/eraser input
}
```

**Result:**
- No transition tracking = no transition bugs
- Simple and robust gesture detection
- Standard UX (matches Google Maps, Photos, Samsung Notes)
- Pending user testing...

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

### BUG-A008: Stylus Eraser Not Working
**Status:** ✅ Fixed  
**Priority:** Medium  
**Category:** Input / Stylus
**Platform:** Android only

**Description:**  
The eraser end of the stylus (e.g., S-Pen) doesn't erase - it draws like the pen tip instead.

**Root Cause:**  
Qt on Android doesn't properly translate Android's `MotionEvent.TOOL_TYPE_ERASER` to `QPointingDevice::PointerType::Eraser`. The `QTabletEvent::pointerType()` returns `Pen` even when using the eraser end of the stylus.

**Symptoms (BEFORE fix):**
- Using eraser tip draws strokes instead of erasing
- Eraser works correctly on desktop (Linux, Windows)
- Pen tip works correctly on Android

**Fix Applied:**

**Layer 1: Java-side tool type detection**
In `SpeedyNoteActivity.java`, detect eraser tool type in `dispatchTouchEvent()`:

```java
private static volatile boolean sEraserToolActive = false;

@Override
public boolean dispatchTouchEvent(MotionEvent event) {
    // Check tool type before Qt processes the event
    boolean hasEraser = false;
    for (int i = 0; i < event.getPointerCount(); i++) {
        if (event.getToolType(i) == MotionEvent.TOOL_TYPE_ERASER) {
            hasEraser = true;
            break;
        }
    }
    sEraserToolActive = hasEraser;
    // ... rest of method
}

public static boolean isEraserToolActive() {
    return sEraserToolActive;
}
```

**Layer 2: C++ JNI query**
In `DocumentViewport::tabletToPointerEvent()`, query Android's tool type if Qt's detection fails:

```cpp
#ifdef Q_OS_ANDROID
    if (!pe.isEraser) {
        QJniObject result = QJniObject::callStaticMethod<jboolean>(
            "org/speedynote/app/SpeedyNoteActivity",
            "isEraserToolActive", "()Z");
        if (result.isValid()) {
            pe.isEraser = static_cast<bool>(result.object<jboolean>());
        }
    }
#endif
```

**Files Modified:**
- `android/app-resources/src/org/speedynote/app/SpeedyNoteActivity.java`
  - Added `sEraserToolActive` volatile boolean
  - Added eraser detection in `dispatchTouchEvent()`
  - Added `isEraserToolActive()` static method for JNI
  
- `source/core/DocumentViewport.cpp`
  - Added `#include <QJniObject>` for Android
  - Added JNI query in `tabletToPointerEvent()` as fallback

**Desktop Impact:** None - all changes are wrapped in `#ifdef Q_OS_ANDROID`.

**Result:**
- ✅ Stylus eraser tip now erases correctly on Android
- ✅ Pen tip still draws correctly
- ✅ Tool type detected before Qt processes the event (no timing issues)

---

### BUG-A009: CJK Font Rendering Incorrect
**Status:** ✅ Fixed  
**Priority:** Medium  
**Category:** UI / Fonts / Localization
**Platform:** Android only

**Description:**  
Chinese characters display incorrectly - showing a mix of Simplified Chinese, Traditional Chinese, and Japanese Kanji variants instead of consistent glyphs.

**Root Cause:**  
Qt on Android doesn't properly use Android's locale-aware font fallback. When a character isn't in the primary font (Roboto), Qt picks glyphs from various CJK fonts without considering the system locale, resulting in mixed glyph variants.

**Symptoms (BEFORE fix):**
- Chinese text has inconsistent appearance
- Some characters appear as Japanese Kanji variants
- Mixed Simplified/Traditional Chinese glyphs in the same text

**Fix Applied:**

Added `applyAndroidFonts()` function in `Main.cpp` that:
1. Detects system locale (zh_CN, zh_TW, ja_JP, etc.)
2. Sets up appropriate CJK font fallback chain based on locale
3. Uses Noto Sans CJK variants with locale-specific priority

```cpp
static void applyAndroidFonts(QApplication& app)
{
    QString locale = QLocale::system().name();
    QFont font("Roboto", 14);
    
    if (locale.startsWith("zh_CN") || locale.startsWith("zh_Hans")) {
        // Simplified Chinese - prioritize SC variant
        font.setFamilies({"Roboto", "Noto Sans CJK SC", "Noto Sans SC", ...});
    } else if (locale.startsWith("zh_TW") || locale.startsWith("zh_HK")) {
        // Traditional Chinese - prioritize TC variant
        font.setFamilies({"Roboto", "Noto Sans CJK TC", "Noto Sans TC", ...});
    } else if (locale.startsWith("ja")) {
        // Japanese - prioritize JP variant
        font.setFamilies({"Roboto", "Noto Sans CJK JP", ...});
    }
    // ... etc
    
    app.setFont(font);
}
```

**Files Modified:**
- `source/Main.cpp`
  - Added `applyAndroidFonts()` with locale-aware CJK font fallback
  - Called `applyAndroidFonts(app)` after `applyAndroidPalette(app)`

**Desktop Impact:** None - all changes are wrapped in `#ifdef Q_OS_ANDROID`.

**Result:**
- ✅ Chinese characters display consistently based on system locale
- ✅ Simplified Chinese devices show SC glyphs
- ✅ Traditional Chinese devices show TC glyphs
- ✅ Japanese devices show JP glyphs

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


