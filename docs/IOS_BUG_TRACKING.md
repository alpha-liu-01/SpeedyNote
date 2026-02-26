# SpeedyNote iPadOS - Bug Tracking

## Build Information

| Property | Value |
|----------|-------|
| Build Type | Debug (Simulator) |
| Qt Version | 6.9.3 |
| Target | iOS 17.0+ |
| Architecture | x86_64 (Simulator), arm64 (Device) |
| PDF Backend | MuPDF (patched HarfBuzz) |
| Test Device | iPad Pro M4 (real), iPad Simulator on MacBookPro15,4 (x86_64) |

## Bug Summary

| Bug | Description | Status |
|-----|-------------|--------|
| **BUG-I001** | QFileOpenEvent not delivered on iOS | ✅ Fixed (Inbox directory watcher) |
| **BUG-I001b** | Inbox PDF not opened when only Launcher visible | ✅ Fixed (auto-create MainWindow) |
| **BUG-I001c** | Inbox PDF stored with temporary path, lost on relaunch | ✅ Fixed (copy to pdfs/ first) |
| **BUG-I002** | UIDocumentPickerViewController file selection unresponsive | ✅ Fixed (dedicated UIWindow) |
| **BUG-I003** | Crash in QIOSTapRecognizer / showEditMenu | ✅ Fixed (remove gesture recognizer at runtime) |
| **BUG-I003b** | Crash in processTouchEvent on notebook deletion | 🟡 Qt bug (low priority) |
| **BUG-I004** | PDF export files not created / share sheet no-op | 🟡 Investigating |
| **BUG-I005** | File picker fails on real jailbroken device | ✅ Fixed (entitlements plist) |
| **BUG-I006** | Virtual keyboard pops up on all UI components | ✅ Fixed (FocusIn event filter) |
| **BUG-I007** | Pinch-to-zoom unreliable | ✅ Fixed (native touch tracking + per-finger TouchBegin) |

---

## Fixed Bugs

### BUG-I001: QFileOpenEvent Not Delivered on iOS
**Status:** 🟢 Fixed  
**Priority:** Critical  
**Category:** File Import  
**Root Cause:** Qt's iOS platform plugin does not generate `QFileOpenEvent`. It routes incoming file URLs through `QPlatformServices::openDocument()`, which is unimplemented on iOS.

**Symptom:**  
When dragging an `.snbx` file onto the simulator app, the file appeared in `Documents/Inbox/` but nothing was imported. The console printed:
```
This plugin does not support QPlatformServices::openDocument() for 'file:///...Documents/Inbox/filename.snbx'.
```

**Analysis:**  
On macOS, Qt's Cocoa plugin converts incoming file URLs into `QFileOpenEvent` via `QWindowSystemInterface::handleFileOpenEvent()`. The iOS plugin does NOT do this — it instead calls `QPlatformServices::openUrl()` → `openDocument()`, which fails silently (aside from the console message).

However, iOS correctly copies the incoming file to the app's `Documents/Inbox/` directory before delivering the URL. The file is there; Qt just doesn't tell the app about it.

**Fix:**  
Replaced the `QFileOpenEvent`-based `FileOpenEventFilter` (which only works on macOS) with an `IOSInboxWatcher` class on iOS:

- Uses `QFileSystemWatcher` to monitor `Documents/Inbox/`
- On directory change, waits 400ms (for iOS to finish writing), then scans for new files
- Routes `.snbx` files to `Launcher::importFiles()`
- Routes `.pdf` files to `MainWindow::openFileInNewTab()`
- Cleans up processed files from Inbox
- Also scans Inbox on app launch (800ms delay) for files that arrived while app was closed

The macOS `FileOpenEventFilter` is preserved for macOS builds where `QFileOpenEvent` works correctly.

This is **production-essential** — `Documents/Inbox/` is the standard iOS mechanism for receiving files via "Open With", AirDrop, drag-and-drop from Split View, Mail attachments, and other apps' share sheets.

**Affected files:**
- `source/Main.cpp` — `IOSInboxWatcher` class, wiring in `main()`
- `source/ui/launcher/Launcher.cpp` — Inbox path added to cleanup in `performBatchImport()`

**Verification:**  
Dragging `.snbx` into the simulator now imports the notebook correctly. PDF documents inside the notebook open and render properly.

---

### BUG-I001b: Inbox PDF Not Opened When Only Launcher Is Visible
**Status:** 🟢 Fixed  
**Priority:** High  
**Category:** File Import  
**Root Cause:** `IOSInboxWatcher::processInbox()` found the PDF in Inbox but could not open it because no `MainWindow` existed yet (only the Launcher was showing). The code checked `m_mainWindow` (null) and `MainWindow::findExistingMainWindow()` (returns nullptr), then silently deleted the PDF from Inbox.

**Symptom:**  
Opening a PDF from the Files app into SpeedyNote (via "Open With") showed the Launcher but no PDF document tab. Console showed:
```
This plugin does not support QPlatformServices::openDocument() for 'file:///...Documents/Inbox/document.pdf'.
[InboxWatcher] found: /...Documents/Inbox/document.pdf
```
The InboxWatcher detected the file but discarded it because there was no MainWindow to open it in.

**Fix:**  
Added `getOrCreateMainWindow()` helper to `IOSInboxWatcher` that mirrors the pattern from `connectLauncherSignals()`. When a PDF or `.snb` arrives in Inbox and no MainWindow exists:
1. Creates a new `MainWindow`
2. Calls `preserveWindowState()` to inherit geometry from the Launcher
3. Hides the Launcher with animation
4. Opens the file in the new MainWindow

**Note on "Preview opens instead when dragging":** This is a simulator limitation, not an app bug. Dragging a file from macOS Finder to the iOS Simulator goes through macOS's drag-and-drop system, which routes to the macOS default handler (Preview for PDFs). On a real iPad, "Open With" from Files.app and drag-and-drop from Split View both work through iOS's proper file routing, which does respect the app's `CFBundleDocumentTypes` in Info.plist (both `.snbx` and `.pdf` are registered).

**Affected files:**
- `source/Main.cpp` — `IOSInboxWatcher::getOrCreateMainWindow()`

---

### BUG-I001c: Inbox PDF Stored With Temporary Path, Lost on Relaunch
**Status:** 🟢 Fixed  
**Priority:** High  
**Category:** File Import  
**Root Cause:** The InboxWatcher passed the `Documents/Inbox/` path directly to `openFileInNewTab()`. SpeedyNote saved a reference to this temporary path. The Inbox file was then deleted (by the watcher's cleanup and/or iOS itself). On relaunch, the PDF was not found.

**Symptom:**
```
loadBundle: PDF not found at absolute path: "/.../Documents/Inbox/The Cosmic Perspective..." 
or relative path: "../../../../../../Documents/Inbox/The Cosmic Perspective..."
```
SNBX export also failed to include the PDF because the source file was already gone.

**Fix:**  
Added `copyPdfToPermanentStorage()` to `IOSInboxWatcher`. Before opening a PDF from Inbox:
1. Copies it to `AppData/pdfs/` (the same permanent directory used by `PdfPickerIOS`)
2. Handles filename collisions with a counter suffix
3. Passes the permanent path to `openFileInNewTab()`
4. Only then deletes the Inbox copy

**Affected files:**
- `source/Main.cpp` — `IOSInboxWatcher::copyPdfToPermanentStorage()`

---

### BUG-I002: UIDocumentPickerViewController File Selection Fixed
**Status:** 🟢 Fixed  
**Priority:** Critical  
**Category:** File Import (Native Picker)  
**Root Cause:** Qt's `QIOSViewController` intercepts all touch events on its UIView hierarchy. When the `UIDocumentPickerViewController` (a remote view controller) was presented from Qt's root view controller, Qt's touch handling prevented file selection gestures from reaching the picker's remote UI.

**Symptom:**  
- **PDF picker (single-select):** Tapping a PDF file did nothing.
- **SNBX picker (multi-select):** Checkmarks appeared but "Open" button was unresponsive.
- Cancel always worked (it's in the navigation bar, outside the remote content area).

**Fix:**  
Rewrote both `PdfPickerIOS.mm` and `SnbxPickerIOS.mm` to present the document picker from a **dedicated UIWindow** completely independent of Qt's view hierarchy:

1. Create a new `UIWindow` at `UIWindowLevelAlert + 1` (above all Qt windows)
2. Set a minimal root `UIViewController` with a clear background
3. Make it key and visible
4. Present the `UIDocumentPickerViewController` from this root VC
5. In delegate callbacks, hide and release the dedicated window, then invoke the completion

This ensures Qt has zero involvement in the picker's touch event chain. The delegate (`SNPdfPickerDelegate` / `SNSnbxPickerDelegate`) holds a strong reference to the window to keep it alive.

**Verification:**  
File picker now works correctly for files in user-accessible locations:
- "On My iPad" root folder — PDF and SNBX selection works
- iCloud Drive — expected to work on real devices
- Third-party file providers (Google Drive, etc.) — expected to work

**Note on simulator limitations:**  
Files in other apps' sandboxes (e.g., `preview/inbox/*.pdf`) may appear in the picker on the simulator but cannot be selected. This is expected iOS sandboxing behavior — the document picker can only grant access to files the user legitimately owns. On a real iPad, these phantom entries from other apps' sandboxes would not be visible. The simulator exposes the macOS file system in misleading ways.

**Affected files:**
- `source/ios/PdfPickerIOS.mm` — dedicated UIWindow for PDF picker
- `source/ios/SnbxPickerIOS.mm` — dedicated UIWindow for SNBX picker

---

### BUG-I003: Crash in QIOSTapRecognizer / showEditMenu
**Status:** 🟢 Fixed (workaround)  
**Priority:** Medium  
**Category:** UI / Text Input  
**Crash Type:** EXC_BAD_ACCESS (SIGSEGV) at address 0x18  
**Root Cause:** Qt bug — use-after-free in `qiostextinputoverlay.mm`

**Symptom:**  
The app crashes during normal interaction, typically after a window transition (file import, picker dismissal, etc.). Observed twice with identical crash stacks. The crash is NOT immediate — it happens seconds to minutes after the triggering event.

**Crash Stack (Thread 0, main thread):**
```
0  QPlatformWindow::window() const + 12
1  showEditMenu(UIView*, QPoint) + 105          (qiostextinputoverlay.mm:139)
2  [QIOSTapRecognizer touchesEnded:withEvent:]   (qiostextinputoverlay.mm:982)
3  _dispatch_call_block_and_release
```

**Analysis:**  
Qt's iOS text input overlay installs a `QIOSTapRecognizer` (a `UIGestureRecognizer` subclass) on the root UIView. When the user taps the screen, the recognizer's `touchesEnded:withEvent:` dispatches a block asynchronously via `dispatch_async` to show the iOS edit menu (copy/paste). 

The bug: the block captures a `UIView*` / `QPlatformWindow*` reference. If the associated window is destroyed between the touch event and the block execution (which is common during window transitions like Launcher → MainWindow, or after dismissing a modal `UIDocumentPickerViewController`), the block dereferences a stale pointer (`0x10 + 0x8 = 0x18`), causing SIGSEGV.

This is a **Qt framework bug** — the asynchronous block should check for window validity before accessing `QPlatformWindow::window()`.

**Confirmed by:**
- Two separate crash reports with identical stacks, both KERN_INVALID_ADDRESS at 0x18
- Both triggered by window transitions (file drop import, picker cancellation)
- `rax = 0x10` in both cases (null-like QPlatformWindow pointer)

**Fix (workaround):**  
Since SpeedyNote is a stylus drawing app and does not need the iOS copy/paste edit menu, the `QIOSTapRecognizer` is removed entirely at runtime:

- `IOSPlatformHelper::disableEditMenuOverlay()` uses Objective-C runtime introspection to find the `QIOSTapRecognizer` class via `NSClassFromString`
- Recursively traverses all UIViews in all UIWindowScenes
- Removes any gesture recognizers matching the class
- Called via `QTimer::singleShot(0, ...)` after the first widget is shown, so Qt has finished setting up its UIViews

This eliminates both the crash and the unwanted edit menu popup on tap.

**Affected files:**
- `source/ios/IOSPlatformHelper.h` — declared `disableEditMenuOverlay()`
- `source/ios/IOSPlatformHelper.mm` — implemented with ObjC runtime introspection
- `source/Main.cpp` — called after widget show in both code paths

---

### BUG-I005: File Picker Fails on Real Jailbroken Device
**Status:** Fixed  
**Priority:** Critical  
**Category:** File Import (Native Picker)  
**Platform:** Real device only (works on Simulator)

**Symptom:**  
The `UIDocumentPickerViewController` opens but selecting a file does nothing. Tapping a PDF or SNBX file has no effect. "Open in SpeedyNote" from other apps also fails. The same code works perfectly on the iOS Simulator.

**Root Cause:**  
The `UIDocumentPickerViewController` relies on XPC communication with file provider extensions. On a real device, XPC services verify the calling app's entitlements. Ad-hoc signing with bare `ldid -S` strips all entitlements, leaving the app without:

1. `application-identifier` -- required for XPC to identify the caller and grant file access
2. `platform-application` -- required for system-level trust on jailbroken/TrollStore devices

Without these, the file provider extension refuses to communicate with the app, so file selection callbacks are never triggered.

**Fix:**  
Created `ios/entitlements.plist` with the required entitlements and updated `ios/build-device.sh` to sign with them:

```bash
ldid -S"${ENTITLEMENTS}" "${APP_PATH}/speedynote"
```

**Affected files:**
- `ios/entitlements.plist` -- new file with `platform-application` and `application-identifier`
- `ios/build-device.sh` -- updated ad-hoc signing step to use entitlements plist

---

### BUG-I006: Virtual Keyboard Pops Up on All UI Components
**Status:** Fixed  
**Priority:** Medium  
**Category:** UI / Input  
**Platform:** Real device only (not visible on Simulator)

**Symptom:**  
Pressing almost any UI component (buttons, toolbars, canvas, etc.) causes the iOS virtual keyboard to pop up. The keyboard should only appear for actual text input fields.

**Root Cause:**  
Qt's iOS platform plugin creates a `UITextInput`-conforming responder for every focused `QWidget`. When any widget receives focus, iOS sees a text input responder and shows the virtual keyboard -- even for buttons, sliders, and the drawing canvas.

**Fix:**  
Installed an application-level `QEvent::FocusIn` event filter (`IOSKeyboardFilter`) that intercepts focus events before the keyboard appears. For each focused widget, it checks whether the widget is a text input type (`QLineEdit`, `QTextEdit`, or `QPlainTextEdit`). If not, it sets `Qt::WA_InputMethodEnabled` to `false`, preventing iOS from showing the keyboard.

**Affected files:**
- `source/ios/IOSPlatformHelper.h` -- declared `installKeyboardFilter()`
- `source/ios/IOSPlatformHelper.mm` -- `IOSKeyboardFilter` class and installation
- `source/Main.cpp` -- called during iOS app initialization

---

### BUG-I007: Pinch-to-Zoom Unreliable
**Status:** Fixed  
**Priority:** High  
**Category:** Touch Input / Gestures  
**Platform:** iPad (real device)

**Description:**  
Two-finger pinch-to-zoom gestures only trigger sporadically. The gesture stays in single-finger pan mode even when two fingers are clearly on the screen. Zoom-in (spreading fingers apart) is slightly more reliable than zoom-out (pinching together). Stale touch inputs are common. Behavior is nearly identical to the pre-fix Android version (BUG-A005).

**Root Cause (two issues):**

**1. Native touch tracker not receiving events:**  
The initial fix attempted to use a `UIGestureRecognizer` subclass attached to the key window's root view. This recognizer was not reliably receiving touch events because Qt's iOS view hierarchy and its own gesture recognizers can interfere with event delivery to additional recognizers attached to ancestor views.

**2. iOS sends per-finger `TouchBegin` events:**  
Unlike Android (where additional fingers arrive as `QEvent::TouchUpdate`), Qt on iOS sends a separate `QEvent::TouchBegin` for each new finger. The gesture handler's "nuclear reset" on every `TouchBegin` was designed for Android's model where `TouchBegin` means a genuinely new gesture. On iOS, the second finger's `TouchBegin` triggered the nuclear reset, which:
- Cleared all tracked touch IDs (forgetting the first finger)
- Ended the active pan gesture
- Restarted as a single-finger pan with only the second finger

The pinch transition never occurred because the handler only ever saw one finger at a time.

**Fix (two layers):**

**Layer 1: `sendEvent:` swizzle (replaces gesture recognizer)**  
Replaced the `UIGestureRecognizer` with a method swizzle on `UIApplication.sendEvent:`. This is the iOS equivalent of Android's `Activity.dispatchTouchEvent()` -- it intercepts every touch event at the application level, before any view, gesture recognizer, or Qt code processes it. This guarantees accurate native finger count and position data.

The swizzle:
- Filters for `UIEventTypeTouches` events
- Iterates `[event allTouches]` to count active touches (phase = Began/Moved/Stationary)
- Records positions of the first two active touches in screen coordinates
- Stores a timestamp for freshness verification
- Forwards to the original `sendEvent:` implementation

Since the swizzle operates on `UIApplication` (not a view), it can be installed at app startup before any window exists.

**Layer 2: iOS per-finger `TouchBegin` interception**  
Added an iOS-specific check before the nuclear reset in `TouchGestureHandler::handleTouchEvent()`. When a `TouchBegin` arrives while a gesture is already active (`m_panActive` or `m_pinchActive`), the code queries the native touch count:
- If native reports 2+ fingers with fresh data (< 100ms), this `TouchBegin` is a second finger joining, not a new gesture
- The new touch ID is added to tracking without clearing existing IDs
- The handler transitions directly to a 2-finger gesture using native positions via `handleTwoFingerGestureNative()`
- The nuclear reset is skipped entirely

If native reports 0-1 fingers, the `TouchBegin` is treated as a genuinely new gesture and the nuclear reset proceeds as before.

**Coordinate conversion:**  
Native positions from `locationInView:nil` are in screen points (logical pixels), which map directly to Qt's coordinate system via `QWidget::mapFromGlobal()`. No device-pixel-ratio conversion is needed (unlike Android where native positions are in physical pixels).

**Files added:**
- `source/ios/IOSTouchTracker.h` -- C++ header declaring native touch query functions
- `source/ios/IOSTouchTracker.mm` -- `sendEvent:` swizzle implementation and C++ bridge

**Files modified:**
- `source/core/TouchGestureHandler.h` -- extended `handleTwoFingerGestureNative()` to iOS
- `source/core/TouchGestureHandler.cpp` -- iOS per-finger `TouchBegin` handling, extended all native verification blocks from `#ifdef Q_OS_ANDROID` to `#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)`
- `source/Main.cpp` -- `IOSTouchTracker::install()` called at app initialization
- `CMakeLists.txt` -- added `IOSTouchTracker.mm` to iOS platform sources

**Desktop/Android impact:** None. All iOS-specific code is wrapped in `#ifdef Q_OS_IOS`. The shared verification logic in `TouchGestureHandler` was already present for Android; iOS was added alongside it.

**Relation to BUG-A005:**  
This is the iOS counterpart of Android's BUG-A005 (Pinch-to-Zoom Unreliable). Both platforms suffer from Qt's touch event layer losing track of fingers, but the specific failure modes differ:

| Aspect | Android | iOS |
|--------|---------|-----|
| Native tracking | JNI via `SpeedyNoteActivity.dispatchTouchEvent()` | `sendEvent:` swizzle on `UIApplication` |
| Second finger delivery | `TouchUpdate` with partial point data | Separate `TouchBegin` per finger |
| Core fix | Position caching + stale ID detection | Skip nuclear reset when second finger joins |
| Coordinate format | Physical pixels (needs DPR conversion) | Screen points (direct `mapFromGlobal`) |

---

## Open Bugs

### BUG-I003b: Crash in processTouchEvent on Notebook Deletion
**Status:** 🟡 Qt bug (low priority)  
**Priority:** Low  
**Category:** UI / Touch Handling  
**Crash Type:** EXC_BAD_ACCESS (SIGSEGV) at address 0x2f

**Symptom:**  
Deleting a notebook from the Launcher causes a crash. The notebook IS successfully deleted. The crash happens in Qt's touch event processing after the widget/window is destroyed.

**Crash Stack (Thread 0, main thread):**
```
0  QHashPrivate::Span<...SynthesizedMouseData>::hasNode() + 20
1  QHashPrivate::iterator<...SynthesizedMouseData>::isUnused() + 78
2  QHashPrivate::iterator<...SynthesizedMouseData>::operator++() + 100
3  QHash<QWindow*, ...SynthesizedMouseData>::const_iterator::operator++() + 25
4  QGuiApplicationPrivate::processTouchEvent() + 1293
```

**Analysis:**  
Qt maintains an internal `QHash<QWindow*, SynthesizedMouseData>` for touch-to-mouse synthesis. When a `QWindow` is destroyed (from deleting the notebook), the hash entry becomes stale. On the next touch event, Qt iterates the hash and dereferences the freed `QWindow*` pointer.

This is a **Qt framework bug** — the hash should clean up entries when windows are destroyed. Different from BUG-I003 (which was in the text input overlay), this is in the core touch event processing.

**Mitigation:** Low priority since the delete operation succeeds. The crash only occurs after the action is complete.

---

### BUG-I004: PDF Export / Share Sheet Not Producing Output
**Status:** 🟡 Investigating  
**Priority:** High  
**Category:** File Export  

**Symptom:**  
PDF export appears to succeed from the UI's perspective (the share sheet presents via `UIActivityViewController`), but:
- No file is actually created or exported to an accessible location
- The same `QPlatformServices::openDocument()` error was observed during one export attempt

**Analysis:**  
The `UIActivityViewController` should handle the actual sharing/saving of the file. If the source file path passed to it does not exist or is inaccessible, the share sheet may appear but have nothing to share. Need to verify:
1. The PDF is actually written to disk before the share sheet is presented
2. The file URL passed to `UIActivityViewController` is a valid `file://` URL
3. The share sheet's completion handler reports success or failure

**Affected files:**
- `source/ios/IOSShareHelper.mm`
- PDF export pipeline (creates the PDF file before sharing)
