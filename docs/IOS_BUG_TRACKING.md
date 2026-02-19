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
| **BUG-I002** | UIDocumentPickerViewController file selection unresponsive | 🔴 Open |
| **BUG-I003** | Crash in QIOSTapRecognizer / showEditMenu after file drop | 🟡 Investigating |
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

**Affected files:**
- `source/Main.cpp` — `IOSInboxWatcher` class, wiring in `main()`
- `source/ui/launcher/Launcher.cpp` — Inbox path added to cleanup in `performBatchImport()`

**Verification:**  
Dragging `.snbx` into the simulator now imports the notebook correctly. PDF documents inside the notebook open and render properly.

---

## Open Bugs

### BUG-I002: UIDocumentPickerViewController File Selection Unresponsive
**Status:** 🔴 Open  
**Priority:** High  
**Category:** File Import (Native Picker)  
**Affected:** PDF import (single-select), SNBX import (multi-select)

**Symptom:**  
The native iOS file picker (`UIDocumentPickerViewController`) presents correctly, but:
- **PDF picker (single-select):** Tapping a PDF file does nothing. No delegate callback fires. No error.
- **SNBX picker (multi-select):** Multi-select checkmark UI appears, but tapping the "Open" button does nothing. No delegate callback fires.
- Cancelling the picker DOES fire the `documentPickerWasCancelled:` delegate.

**Analysis:**  
`UIDocumentPickerViewController` is a remote view controller that runs in a separate process (`DocumentManagerUICore.Service`). File selection results are delivered via XPC.

Early attempts used `QEventLoop` to block the main thread until the delegate fired — this caused XPC results to never arrive. The code was refactored to a fully asynchronous `std::function` callback pattern, which fixed the cancel delegate. However, file selection (the "positive" result path) still does not trigger `documentPicker:didPickDocumentsAtURLs:`.

This may be related to:
- Qt's touch event interception interfering with the remote view controller's selection gestures
- The picker's view hierarchy being presented from a Qt-managed `UIViewController`
- Simulator-specific limitations with the document picker's remote UI

**Workaround:**  
Use the Inbox watcher (BUG-I001 fix) — users can import files by dragging them into the app or using iOS "Open With" / Files app sharing, bypassing the in-app picker entirely.

**Affected files:**
- `source/ios/PdfPickerIOS.mm`
- `source/ios/SnbxPickerIOS.mm`

---

### BUG-I003: Crash in QIOSTapRecognizer / showEditMenu After File Drop
**Status:** 🟡 Investigating  
**Priority:** Medium  
**Category:** UI / Text Input  
**Crash Type:** EXC_BAD_ACCESS (SIGSEGV) at address 0x18

**Symptom:**  
After dragging a file into the simulator (which triggers the Inbox import), the app crashes ~2.5 minutes after launch. The crash is NOT immediate — it happens during normal interaction after the import.

**Crash Stack (Thread 0, main thread):**
```
0  QPlatformWindow::window() const + 12
1  showEditMenu(UIView*, QPoint) + 105          (qiostextinputoverlay.mm:139)
2  [QIOSTapRecognizer touchesEnded:withEvent:]   (qiostextinputoverlay.mm:982)
3  _dispatch_call_block_and_release
...
```

**Analysis:**  
`QPlatformWindow::window()` dereferences `this + 0x8` (offset 0x18 from a null-like base pointer of 0x10, stored in `rax`). This means a `QPlatformWindow*` that was once valid has been destroyed, but the `QIOSTapRecognizer` still holds a stale reference.

The tap recognizer's `touchesEnded:withEvent:` dispatches a block asynchronously (via `dispatch_async` on the main queue) that calls `showEditMenu()`. By the time the block executes, the window it references may have been destroyed.

This is likely a **Qt bug in the iOS text input overlay** — the asynchronous block captures a view/window reference that becomes invalid. This could be triggered by:
- Window destruction during or shortly after the file import flow
- A race between the Inbox watcher's import processing and Qt's internal window management

**Possible mitigations:**
- File a Qt bug report for `qiostextinputoverlay.mm` use-after-free
- Investigate if SpeedyNote's import flow destroys/recreates windows in a way that races with the tap recognizer
- As a workaround, consider adding a null check guard via Qt source patch if reproducible

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
