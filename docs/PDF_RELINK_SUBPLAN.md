# PDF Relink Feature Implementation Plan

## Overview

This document outlines the implementation plan for the PDF relink feature, which allows users to relocate a missing PDF file or continue without it when opening a notebook that references an external PDF.

## Design Decisions

### D.1: PDF Verification Strategy

**Decision**: Use SHA-256 hash of the first 1MB of the file + file size.

**Rationale**:
- Full file hash is too slow for large PDFs (100+ MB could take seconds)
- First 1MB captures the PDF header, metadata, and early content
- File size provides an additional quick sanity check
- Combined, these provide high confidence that it's the same file

**Storage**: In `document.json`:
```json
{
  "pdf_path": "/path/to/file.pdf",
  "pdf_hash": "sha256:abc123...",
  "pdf_size": 12345678
}
```

### D.2: Hash Mismatch Behavior

**Decision**: Option C - Ask if they want to "update the reference" (treat new PDF as authoritative).

**Flow**:
1. User selects a PDF file
2. Compute hash and compare with stored hash
3. If mismatch, show dialog:
   - "The selected PDF appears to be different from the original."
   - "Original: document.pdf (12.3 MB)"
   - "Selected: other_document.pdf (15.1 MB)"
   - Buttons: [Use This PDF] [Choose Different File] [Cancel]
4. If "Use This PDF": Update the stored hash and path, mark document modified
5. If "Choose Different File": Return to file picker
6. If "Cancel": Return to relink dialog

### D.3: Dialog Trigger Timing

**Decision**: Option B - Non-blocking, after document opens.

**Flow**:
1. Document opens normally (with empty PDF backgrounds)
2. A banner/notification appears at the top of the viewport:
   - "PDF file not found: document.pdf [Locate PDF] [Dismiss]"
3. User can interact with their annotations while deciding
4. Clicking "Locate PDF" opens the modal `PdfRelinkDialog`
5. Clicking "Dismiss" hides the banner (can access via menu later)

**Benefits**:
- User sees their annotations immediately (reassurance)
- User can decide at their own pace
- Less disruptive than a blocking dialog on open

### D.4: Dialog Modality

**Decision**: Modal to MainWindow (not application-modal).

**Rationale**: User can still access other tabs/windows if needed.

---

## Current State Analysis

### Existing Infrastructure

| Component | Status | Notes |
|-----------|--------|-------|
| `Document::m_pdfPath` | ✅ Exists | Stores path in JSON |
| `Document::loadPdf()` | ✅ Exists | Loads PDF, stores path even on failure |
| `Document::relinkPdf()` | ✅ Exists | Updates path and marks modified |
| `Document::pdfFileExists()` | ✅ Exists | Checks if file exists at path |
| `Document::hasPdfReference()` | ✅ Exists | Checks if path is non-empty |
| `Document::isPdfLoaded()` | ✅ Exists | Checks if provider is valid |
| `PdfRelinkDialog` | ✅ Exists | Basic UI, needs hash verification |

### Missing Components

| Component | Status | Notes |
|-----------|--------|-------|
| `Document::m_pdfHash` | ❌ Missing | SHA-256 hash storage |
| `Document::m_pdfSize` | ❌ Missing | File size storage |
| `Document::computePdfHash()` | ❌ Missing | Hash computation |
| `Document::verifyPdfHash()` | ❌ Missing | Hash verification |
| `PdfMismatchDialog` | ❌ Missing | Hash mismatch warning dialog |
| Missing PDF banner | ❌ Missing | Non-blocking notification |
| Integration in MainWindow | ❌ Missing | Trigger banner on document load |

---

## Implementation Phases

### Phase R.1: PDF Hash Storage & Computation

Add hash storage and computation to `Document`.

#### Task R.1.1: Add Hash Members to Document ✅

**File**: `source/core/Document.h`

**Changes**:
- Add `QString m_pdfHash` member (format: "sha256:...")
- Add `qint64 m_pdfSize` member

**Estimated Lines**: ~5

**Status**: Completed. Added members at line 1116-1117.

#### Task R.1.2: Serialize Hash in JSON ✅

**File**: `source/core/Document.cpp`

**Changes**:
- In `toJson()`: Add `"pdf_hash"` and `"pdf_size"` fields
- In `fromJson()`: Read `"pdf_hash"` and `"pdf_size"` fields

**Estimated Lines**: ~10

**Status**: Completed. Updated `toJson()` and `fromJson()` methods.

#### Task R.1.3: Implement Hash Computation ✅

**File**: `source/core/Document.cpp`

**Changes**:
- Add `QString Document::computePdfHash(const QString& path)` static method
  - Open file, read first 1MB
  - Compute SHA-256 hash
  - Return "sha256:{hex}" or empty string on error
- Add `qint64 Document::getPdfFileSize(const QString& path)` static method

**Estimated Lines**: ~30

**Status**: Completed. Added static methods and accessor methods (`pdfHash()`, `pdfSize()`) to Document.

#### Task R.1.4: Store Hash on PDF Load ✅

**File**: `source/core/Document.cpp`

**Changes**:
- In `loadPdf()`: After successful load, compute and store hash if not already set
- In `relinkPdf()`: Always compute and store new hash (it's a new reference)

**Estimated Lines**: ~15

**Status**: Completed. Hash is computed on first load and always updated on relink.

#### Task R.1.5: Implement Hash Verification ✅

**File**: `source/core/Document.cpp`

**Changes**:
- Add `bool Document::verifyPdfHash(const QString& path) const`
  - If no stored hash: return true (legacy document, can't verify)
  - Compute hash of given path, compare with stored
  - Return true if matches

**Estimated Lines**: ~15

**Status**: Completed. Added `verifyPdfHash()` method declaration and implementation.

**Phase R.1 Total**: ~75 lines

---

### Phase R.2: Hash Mismatch Dialog

Create a dialog for when the user selects a PDF that doesn't match the stored hash.

#### Task R.2.1: Create PdfMismatchDialog ✅

**New Files**: 
- `source/pdf/PdfMismatchDialog.h`
- `source/pdf/PdfMismatchDialog.cpp`

**Design**:
```
┌─────────────────────────────────────────────────────┐
│ ⚠️  Different PDF Detected                          │
├─────────────────────────────────────────────────────┤
│                                                     │
│ The selected PDF appears to be different from the   │
│ one originally used with this notebook.             │
│                                                     │
│ Original:  document.pdf (12.3 MB)                   │
│ Selected:  other_doc.pdf (15.1 MB)                  │
│                                                     │
│ Using a different PDF may cause annotations to      │
│ appear in the wrong positions.                      │
│                                                     │
│ ┌─────────────────┐ ┌──────────────────┐ ┌────────┐│
│ │ Use This PDF    │ │ Choose Different │ │ Cancel ││
│ └─────────────────┘ └──────────────────┘ └────────┘│
└─────────────────────────────────────────────────────┘
```

**Result Enum**:
```cpp
enum class Result {
    UseThisPdf,      // Accept the different PDF
    ChooseDifferent, // Go back to file picker
    Cancel           // Abort relink entirely
};
```

**Estimated Lines**: ~120

**Status**: Completed. Created dialog with warning icon, file comparison, and three action buttons.
Added to CMakeLists.txt under PDF_SOURCES.

**Phase R.2 Total**: ~120 lines

---

### Phase R.3: Missing PDF Banner

Create a non-blocking notification banner for missing PDFs.

#### Task R.3.1: Create MissingPdfBanner Widget ✅

**New Files**:
- `source/ui/banners/MissingPdfBanner.h`
- `source/ui/banners/MissingPdfBanner.cpp`

**Design**:
```
┌──────────────────────────────────────────────────────────────────┐
│ ⚠️ PDF file not found: document.pdf    [Locate PDF] [Dismiss]   │
└──────────────────────────────────────────────────────────────────┘
```

**Features**:
- Appears at top of `DocumentViewport`
- Yellow/orange warning background (#FFF3CD)
- "Locate PDF" button (orange) triggers `locatePdfClicked()` signal
- "Dismiss" button hides banner with animation
- Animated slide-in/slide-out using QPropertyAnimation

**Signals**:
- `locatePdfClicked()`
- `dismissed()`

**Estimated Lines**: ~80

**Status**: Completed. Created banner widget with slide animation and Bootstrap-style warning colors.
Added to CMakeLists.txt under UI_SOURCES.

#### Task R.3.2: Add Banner Container to DocumentViewport ✅

**File**: `source/core/DocumentViewport.h`, `source/core/DocumentViewport.cpp`

**Changes**:
- Add `MissingPdfBanner* m_missingPdfBanner` member
- Add `showMissingPdfBanner(const QString& pdfName)` method
- Add `hideMissingPdfBanner()` method
- Add `requestPdfRelink()` signal (emitted when user clicks "Locate PDF")
- Position banner at top of viewport, above content
- Update banner width on viewport resize

**Estimated Lines**: ~40

**Status**: Completed. Added banner member, show/hide methods, signal, and resize handling.

**Phase R.3 Total**: ~120 lines

---

### Phase R.4: Integration & Flow

Wire everything together in MainWindow.

#### Task R.4.1: Update PdfRelinkDialog with Hash Verification ✅

**File**: `source/pdf/PdfRelinkDialog.h`, `source/pdf/PdfRelinkDialog.cpp`

**Changes**:
- Add constructor parameters for stored hash and size
- Add `verifyAndConfirmPdf()` private method
- After file selection, verify hash using `Document::computePdfHash()`
- If mismatch, show `PdfMismatchDialog`
- Handle result:
  - `UseThisPdf` → Accept and close dialog
  - `ChooseDifferent` → Loop back to file picker
  - `Cancel` → Abort entirely

**Estimated Lines**: ~50

**Status**: Completed. Added hash verification with mismatch dialog integration.

#### Task R.4.2: Integrate Banner into Document Loading ✅

**File**: `source/MainWindow.h`, `source/MainWindow.cpp`

**Changes**:
- Added `m_pdfRelinkConn` connection member variable
- Added include for `pdf/PdfRelinkDialog.h`
- In `connectViewportScrollSignals()`:
  - Disconnect old `m_pdfRelinkConn`
  - Connect `viewport->requestPdfRelink` to handler that:
    - Opens `PdfRelinkDialog` with hash verification parameters
    - On `RelinkPdf`: calls `doc->relinkPdf()`, hides banner, refreshes viewport
    - On `ContinueWithoutPdf`: hides banner
    - On `Cancel`: keeps banner visible
  - Check if `doc->hasPdfReference() && !doc->isPdfLoaded()`
  - If true, show `MissingPdfBanner` with PDF filename
  - Otherwise, ensure banner is hidden

**Estimated Lines**: ~60

**Status**: Completed. Integrated PDF missing detection and relink flow.

#### Task R.4.3: Add Menu Option for Relink ✅

**File**: `source/MainWindow.h`, `source/MainWindow.cpp`

**Changes**:
- Added `QAction* m_relinkPdfAction` member variable
- Added "Relink PDF..." action to `overflowMenu`
- Initially disabled, enabled when document has PDF reference
- Opens `PdfRelinkDialog` with hash verification
- Handles relink success (hide banner, refresh viewport)
- Updated `connectViewportScrollSignals()` to enable/disable action on tab switch

**Estimated Lines**: ~20

**Status**: Completed. Menu action added with proper enable/disable logic.

**Phase R.4 Total**: ~130 lines

---

## Summary Table

| Phase | Description | Est. Lines | Dependencies |
|-------|-------------|------------|--------------|
| R.1 | PDF Hash Storage & Computation | ~75 | None |
| R.2 | Hash Mismatch Dialog | ~120 | R.1 |
| R.3 | Missing PDF Banner | ~120 | None |
| R.4 | Integration & Flow | ~130 | R.1, R.2, R.3 |
| **Total** | | **~445** | |

---

## Implementation Order

1. **R.1.1** → **R.1.2** → **R.1.3** → **R.1.4** → **R.1.5** (Hash infrastructure)
2. **R.2.1** (Mismatch dialog)
3. **R.3.1** → **R.3.2** (Banner)
4. **R.4.1** → **R.4.2** → **R.4.3** (Integration)

---

## Success Criteria

### Functional Requirements

- [ ] Documents with missing PDFs open successfully (degraded mode)
- [ ] Banner appears for documents with missing PDFs
- [ ] User can locate and relink a PDF via banner or menu
- [ ] PDF backgrounds render after successful relink
- [ ] Hash is computed and stored on first PDF load
- [ ] Hash mismatch warning appears when selecting different PDF
- [ ] User can accept different PDF (updates stored hash)
- [ ] Document is marked modified after relink

### Non-Functional Requirements

- [ ] Hash computation completes in <100ms for typical PDFs
- [ ] Banner animation is smooth (60fps)
- [ ] Dialog is modal to MainWindow, not application
- [ ] Works correctly with dark mode

---

## Edge Cases

| Case | Expected Behavior |
|------|-------------------|
| Legacy document (no hash stored) | Accept any PDF without warning |
| PDF found, hash matches | Load normally, no dialog/banner |
| PDF found, hash mismatch | Unusual but could happen if user replaced file; treat as "found" |
| PDF not found, user cancels | Keep banner visible, document usable |
| PDF not found, user dismisses | Hide banner, can access via menu |
| Relink to same file (after moving back) | Hash matches, accept silently |
| Very large PDF (1GB+) | Hash computation still fast (only reads 1MB) |
| Corrupted PDF selected | `loadPdf()` fails, show error, return to dialog |

---

## Files to Create/Modify

### New Files
- `source/pdf/PdfMismatchDialog.h`
- `source/pdf/PdfMismatchDialog.cpp`
- `source/ui/banners/MissingPdfBanner.h`
- `source/ui/banners/MissingPdfBanner.cpp`

### Modified Files
- `source/core/Document.h` (add hash members and methods)
- `source/core/Document.cpp` (implement hash logic, update serialization)
- `source/pdf/PdfRelinkDialog.h` (add hash verification)
- `source/pdf/PdfRelinkDialog.cpp` (integrate hash check)
- `source/core/DocumentViewport.h` (add banner support)
- `source/core/DocumentViewport.cpp` (implement banner display)
- `source/MainWindow.cpp` (integrate relink flow)
- `CMakeLists.txt` (add new source files)

---

## Phase R.5: Code Review & Fixes ✅

### CR.R.1: Animation state race condition in MissingPdfBanner ✅

**Issue**: In `hideAnimated()`, a `SingleShotConnection` is connected to hide the banner. If `showAnimated()` is called before the animation finishes, the stale connection could unexpectedly hide the banner.

**Fix**: Added `disconnect(m_animation, &QPropertyAnimation::finished, nullptr, nullptr)` at the start of both `showAnimated()` and `hideAnimated()` to clear any pending connections.

### CR.R.2: Redundant animation restarts ✅

**Issue**: Calling `showMissingPdfBanner()` multiple times (e.g., on rapid tab switches) would restart the animation unnecessarily.

**Fix**: Added `if (!m_missingPdfBanner->isVisible())` check before calling `showAnimated()`.

### CR.R.3: Redundant hide animation ✅

**Issue**: `hideMissingPdfBanner()` could start a hide animation even when the banner was already hidden.

**Fix**: Added `&& m_missingPdfBanner->isVisible()` check before calling `hideAnimated()`.

### Items Verified (No Issues Found)

- **Memory management**: All widgets use Qt parent-child ownership, no manual deletion needed
- **JSON serialization**: Handles missing keys gracefully, only saves non-empty values
- **Hash computation**: File is properly closed, handles read errors
- **Signal connections**: Qt handles disconnection when objects are destroyed
- **Thread safety**: All operations run on the main thread (no threading issues)

