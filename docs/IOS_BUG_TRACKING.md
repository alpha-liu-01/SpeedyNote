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
