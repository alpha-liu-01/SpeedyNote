# SpeedyNote Bug Tracking

## Overview

This document tracks bugs, regressions, and polish issues discovered during and after the migration. Each bug is assigned a unique ID for reference in commit messages and code comments.

**Format:** `BUG-{CATEGORY}-{NUMBER}` (e.g., `BUG-VP-001` for viewport bugs)

**Last Updated:** Jan 14, 2026 (BUG-AB-001 fixed)

---

## Categories

| Prefix | Category | Description |
|--------|----------|-------------|
| **VP** | Viewport | DocumentViewport rendering, pan/zoom, coordinate transforms |
| **DRW** | Drawing | Pen, Marker, Eraser stroke behavior |
| **LSO** | Lasso | Lasso selection, transforms, clipboard |
| **HL** | Highlighter | Highlighter tool, PDF text selection |
| **SL** | Straight Line | Straight line mode |
| **OBJ** | Objects | ImageObject, LinkObject, ObjectSelect tool |
| **LYR** | Layers | Layer panel, layer operations |
| **PG** | Pages | Page panel, page navigation, add/delete |
| **PDF** | PDF | PDF loading, rendering, outline, caching |
| **FILE** | File I/O | Save/load, bundle format, dirty tracking |
| **TAB** | Tabs | Tab management, switching, close behavior |
| **TB** | Toolbar | Main toolbar, tool buttons |
| **STB** | Subtoolbar | Tool-specific subtoolbars |
| **AB** | Action Bar | Context action bar |
| **SB** | Sidebar | Left sidebar container, panel switching |
| **TCH** | Touch | Touch gestures, tablet input |
| **MD** | Markdown | Markdown notes integration |
| **PERF** | Performance | Lag, memory, CPU issues |
| **UI** | UI/UX | Visual glitches, layout issues |
| **MISC** | Miscellaneous | Other issues |

---

## Priority Levels

| Level | Description | Response |
|-------|-------------|----------|
| 🔴 **P0** | Critical | Data loss, crash, unusable feature - fix immediately |
| 🟠 **P1** | High | Major feature broken, poor UX - fix soon |
| 🟡 **P2** | Medium | Minor feature issue, workaround exists |
| 🟢 **P3** | Low | Polish, cosmetic, edge case |

---

## Bug Status

| Status | Description |
|--------|-------------|
| 🆕 **NEW** | Reported, not yet investigated |
| 🔍 **INVESTIGATING** | Root cause being analyzed |
| 🔧 **IN PROGRESS** | Fix being implemented |
| ✅ **FIXED** | Fix complete and verified |
| ⏸️ **DEFERRED** | Won't fix now, tracked for later |
| ❌ **WONTFIX** | By design, not a bug, or not worth fixing |

---

## Active Bugs

### Viewport (VP)

*No active bugs*

---

### Touch/Tablet (TCH)

*No active bugs*

---

<!-- Template:
#### BUG-VP-XXX: [Title]
**Priority:** 🟡 P2 | **Status:** 🆕 NEW

**Symptom:** 
[What the user sees/experiences]

**Steps to Reproduce:**
1. Step one
2. Step two
3. Step three

**Expected:** [What should happen]
**Actual:** [What actually happens]

**Root Cause:** 
[Technical explanation - fill in during investigation]

**Fix:**
[Solution description]

**Files Modified:**
- `source/core/DocumentViewport.cpp`

**Verified:** [ ] Tested and working
-->

---

### Drawing (DRW)

---

### Lasso (LSO)

---

### Highlighter (HL)

---

### Straight Line (SL)

---

### Objects (OBJ)

---

### Layers (LYR)

---

### Pages (PG)

#### BUG-PG-001: PDF background pages can be deleted via Page Panel
**Priority:** 🔴 P0 | **Status:** ✅ FIXED

**Symptom:** 
Pages with PDF backgrounds could be deleted through the Page Panel action bar's delete button, even though PDF pages should be protected from deletion (use external tools to modify PDFs).

**Steps to Reproduce:**
1. Open a PDF document in SpeedyNote
2. Open the left sidebar, select Pages tab
3. Navigate to any PDF page
4. Click the delete button on the PagePanelActionBar
5. Wait for 5-second timeout (or don't click undo)
6. PDF page is deleted

**Expected:** PDF background pages should be protected from deletion
**Actual:** PDF pages were deleted, corrupting the annotation workflow

**Root Cause:** 
The `deletePageClicked` handler in `MainWindow::setupPagePanelActionBar()` (line 3676) was missing the PDF page protection check. It only checked for "last page" but not for `Page::BackgroundType::PDF`.

The standalone `deletePageInDocument()` method (line 2297) had the check, but the PagePanelActionBar handler bypassed it by directly calling `doc->removePage()`.

**Fix:**
Added PDF page check to the `deletePageClicked` handler:
```cpp
// BUG-PG-001 FIX: Can't delete PDF background pages
Page* page = doc->page(m_pendingDeletePageIndex);
if (page && page->backgroundType == Page::BackgroundType::PDF) {
    qDebug() << "Page Panel: Cannot delete PDF page" << m_pendingDeletePageIndex;
    m_pendingDeletePageIndex = -1;
    m_pagePanelActionBar->resetDeleteButton();
    return;
}
```

**Files Modified:**
- `source/MainWindow.cpp` (deletePageClicked handler, ~line 3685)

**Verified:** [ ] PDF pages cannot be deleted via Page Panel
**Verified:** [ ] Inserted blank pages CAN still be deleted
**Verified:** [ ] Last page protection still works

---

#### BUG-PG-002: Page delete undo button doesn't work
**Priority:** 🟠 P1 | **Status:** ✅ FIXED

**Symptom:** 
When clicking the delete button on the PagePanelActionBar, the page was immediately deleted. The 5-second undo window was useless because clicking "Undo" did nothing - the page was already gone.

**Steps to Reproduce:**
1. Open a document with multiple pages (not PDF)
2. Open Pages tab in left sidebar
3. Click delete button on PagePanelActionBar
4. Button transforms to "Undo" state
5. Click "Undo" within 5 seconds
6. Page is NOT restored (it was already deleted)

**Expected:** Clicking "Undo" within 5 seconds should cancel the deletion
**Actual:** Page was already deleted on first click, undo did nothing

**Root Cause:** 
The `deletePageClicked` handler immediately called `doc->removePage()` instead of waiting for the 5-second confirmation timer to expire. The design intention was:
1. First click → Mark for deletion (soft delete)
2. 5 sec timeout → Actually delete (hard delete)
3. Undo click → Cancel the pending delete

But the implementation was:
1. First click → **Immediately delete** ❌
2. 5 sec timeout → Just clear state
3. Undo click → Nothing to undo

**Fix:**
Restructured the three handlers:

1. **`deletePageClicked`**: Only stores `m_pendingDeletePageIndex`, doesn't delete
2. **`deleteConfirmed`**: Actually performs `doc->removePage()` and updates UI
3. **`undoDeleteClicked`**: Clears `m_pendingDeletePageIndex` to cancel

Added validation in `deleteConfirmed` to handle edge cases:
- Page index still valid?
- Page is still not a PDF page?
- Document still has >1 page?

**Files Modified:**
- `source/MainWindow.cpp` (deletePageClicked, deleteConfirmed, undoDeleteClicked handlers)

**Verified:** [ ] First click marks page for deletion but doesn't delete
**Verified:** [ ] Clicking Undo cancels the pending delete
**Verified:** [ ] Waiting 5 seconds actually deletes the page
**Verified:** [ ] PDF/last-page protections still work

---

#### BUG-PG-004: Default page size inconsistent and not configurable
**Priority:** 🟡 P2 | **Status:** ✅ FIXED

**Symptom:** 
1. Default page size was inconsistent across the codebase:
   - `Document.h`: 816×1056 (US Letter at 96 DPI) ✓
   - `Page::Page()`: Uninitialized (0×0 or garbage) ✗
   - `Page::fromJson()`: 800×600 fallback ✗
2. Users could not configure default page size for new documents
3. Users who prefer ISO paper sizes (A4, A3, etc.) had no way to change the default

**Root Cause:** 
The `Page` class constructor didn't initialize the `size` member, and `Page::fromJson()` used an arbitrary 800×600 fallback instead of being consistent with `Document::defaultPageSize`.

**Fix:**

**Part 1: Fixed inconsistencies**
1. `Page::Page()` now initializes `size` to 816×1056 (US Letter at 96 DPI)
2. `Page::fromJson()` now uses 816×1056 as fallback (matches Document default)

**Part 2: Added configurable page size presets**
Added a "Paper Size" dropdown to the Settings → Page tab with common presets:

| Preset | Size (mm) | Size (px @ 96 DPI) |
|--------|-----------|-------------------|
| A3 | 297 × 420 | 1123 × 1587 |
| B4 | 250 × 353 | 945 × 1334 |
| A4 | 210 × 297 | 794 × 1123 |
| B5 | 176 × 250 | 665 × 945 |
| A5 | 148 × 210 | 559 × 794 |
| US Letter | 8.5 × 11 in | 816 × 1056 |
| US Legal | 8.5 × 14 in | 816 × 1344 |
| US Tabloid | 11 × 17 in | 1056 × 1632 |

Settings are stored in QSettings:
- `page/width` - Page width in pixels
- `page/height` - Page height in pixels

**Important:** Page size setting only affects **newly created documents**. It does not change existing documents or currently open documents.

**Files Modified:**
- `source/core/Page.cpp` (constructor, fromJson fallback)
- `source/ControlPanelDialog.h` (added pageSizeCombo, pageSizeDimLabel)
- `source/ControlPanelDialog.cpp` (page size UI in Background tab)
- `source/MainWindow.cpp` (addNewTab applies page size from settings)

**Verified:** [ ] Page::Page() initializes size to 816×1056
**Verified:** [ ] Page::fromJson() uses 816×1056 as fallback
**Verified:** [ ] Settings dialog shows page size presets
**Verified:** [ ] New documents use selected page size
**Verified:** [ ] Existing documents are not affected

---

#### BUG-PG-003: Page thumbnails display with wrong aspect ratio
**Priority:** 🟡 P2 | **Status:** ✅ FIXED

**Symptom:** 
All page thumbnails in the PagePanel displayed with a fixed US Letter aspect ratio (1.294), causing pages with different sizes (A4, custom, etc.) to appear stretched or squashed.

**Steps to Reproduce:**
1. Open a PDF document with A4 pages (aspect ratio ~1.414)
2. Open the Pages tab in the left sidebar
3. Thumbnails appear squashed (too short for their width)

**Expected:** Thumbnails should display at the same aspect ratio as the actual page
**Actual:** All thumbnails used fixed US Letter ratio regardless of actual page size

**Root Cause:** 
The `PageThumbnailDelegate` used a hardcoded `m_pageAspectRatio = 1.294` for all items in both `sizeHint()` and `paint()`. While the `ThumbnailRenderer` correctly rendered each thumbnail with the actual page's aspect ratio, the delegate then forced all items into the same fixed-height rectangle, causing distortion.

| Component | Aspect Ratio Used |
|-----------|-------------------|
| ThumbnailRenderer | ✅ Actual page ratio |
| PageThumbnailDelegate | ❌ Fixed 1.294 (US Letter) |

**Fix:**
1. **Added `PageAspectRatioRole`** to `PageThumbnailModel`:
   - Returns `pageSize.height() / pageSize.width()` from document metadata
   - Available immediately (doesn't wait for thumbnail to render)

2. **Updated `PageThumbnailDelegate::sizeHint()`**:
   - Queries `PageAspectRatioRole` from model for each item
   - Uses actual ratio if available, falls back to default

3. **Updated `PageThumbnailDelegate::paint()`**:
   - Queries `PageAspectRatioRole` to calculate correct `thumbRect` height
   - Thumbnail pixmap now fits perfectly without stretching

**Files Modified:**
- `source/ui/PageThumbnailModel.h` (added PageAspectRatioRole enum)
- `source/ui/PageThumbnailModel.cpp` (data() returns aspect ratio, roleNames() updated)
- `source/ui/PageThumbnailDelegate.cpp` (sizeHint() and paint() use per-page ratio)

**Verified:** [ ] A4 pages display correctly (taller than Letter)
**Verified:** [ ] US Letter pages display correctly
**Verified:** [ ] Mixed page sizes in same document each display correctly
**Verified:** [ ] Placeholder (before thumbnail loads) has correct height

---

### PDF (PDF)

---

### File I/O (FILE)

---

### Tabs (TAB)

---

### Toolbar (TB)

---

### Subtoolbar (STB)

---

### Action Bar (AB)

---

### Sidebar (SB)

---

### Touch/Tablet (TCH)

---

### Markdown (MD)

---

### Performance (PERF)

---

### UI/UX (UI)

---

### Miscellaneous (MISC)

---

## Fixed Bugs Archive

<!-- Move fixed bugs here with their full details for reference -->

### Recently Fixed

#### BUG-AB-001: Action bars mispositioned when PagePanelActionBar visible
**Priority:** 🟠 P1 | **Status:** ✅ FIXED

**Symptom:** 
The page panel action bar was "stopping" other action bars from moving to the correct location (right edge of DocumentViewport). Action bars had a tendency to stick somewhere in the viewport, or outside it entirely. Additionally, context action bars (Lasso, ObjectSelect) wouldn't appear correctly when PagePanelActionBar was NOT open.

**Steps to Reproduce:**
1. Open a paged document (not edgeless)
2. Open the left sidebar and select the Pages tab
3. PagePanelActionBar appears
4. Create a lasso selection or select an object
5. The context action bar (LassoActionBar/ObjectSelectActionBar) appears in wrong position
6. OR: Without PagePanel open, context action bars don't appear or appear offscreen

**Expected:** Both bars should be arranged in 2-column layout, right-aligned 24px from viewport edge; single action bars should also position correctly
**Actual:** Action bars stuck in middle of viewport or outside it

**Root Cause:** 
Four issues in `ActionBarContainer.cpp`:

1. **`animateShow()` ignored 2-column layout:** The animation code only used `m_currentActionBar` dimensions, completely ignoring `PagePanelActionBar` when calculating positions. This was legacy code from before 2-column support was added.

2. **Stale viewport rect:** When `setPagePanelVisible()` was called, it used cached `m_viewportRect` which could be empty or outdated. `MainWindow::updatePagePanelActionBarVisibility()` didn't call `updateActionBarPosition()` after changing visibility.

3. **`isVisible()` returns false when container is hidden (THE REAL BUG):** In `updateSize()` and `updatePosition()`, the code checked `m_currentActionBar->isVisible()` to determine if a context bar should be shown. However, `QWidget::isVisible()` returns false if ANY ancestor is hidden. When showing a context action bar:
   - `showActionBar()` calls `m_currentActionBar->show()` on the child
   - Then calls `updateSize()` BEFORE showing the container
   - `isVisible()` returns false because container is still hidden
   - Container size is set to 0x0!
   - This is why it worked with PagePanel open (container already visible) but not when closed.

4. **No fresh rect for context bars:** When context action bars were shown, `showActionBar()` used `m_viewportRect` which might be empty.

**Fix:**
1. Rewrote `animateShow()` to calculate total width/height considering both PagePanelActionBar and context action bar (2-column or single-column layout)
2. Added `updateActionBarPosition()` call in `MainWindow::updatePagePanelActionBarVisibility()` after visibility change
3. Animation now triggers `updatePosition()` on completion to properly position child action bars
4. Added `positionUpdateRequested` signal to `ActionBarContainer` - emitted when container is about to become visible
5. Connected signal to `MainWindow::updateActionBarPosition()` to ensure fresh viewport rect before showing
6. Added fallback to query parent widget rect when `m_viewportRect` is empty
7. **Critical fix:** Changed `updateSize()`, `updatePosition()`, and `setPagePanelVisible()` to use `m_currentActionBar != nullptr` instead of `m_currentActionBar->isVisible()`. This checks intent-to-show rather than actual visibility, which depends on ancestor state.

**Files Modified:**
- `source/ui/actionbars/ActionBarContainer.h` (added positionUpdateRequested signal)
- `source/ui/actionbars/ActionBarContainer.cpp` (animateShow, showActionBar, updatePosition - parent fallback)
- `source/MainWindow.cpp` (updatePagePanelActionBarVisibility, connect positionUpdateRequested)

**Verified:** [x] Single action bar positions correctly
**Verified:** [x] 2-column layout positions both bars correctly
**Verified:** [x] Animation works for both layouts
**Verified:** [x] Context bars appear correctly without PagePanel open
**Verified:** [x] Action bars reposition on window maximize

---

#### BUG-UI-001: Subtoolbars/action bars don't reposition on window maximize
**Priority:** 🟢 P3 | **Status:** ✅ FIXED

**Symptom:** 
When maximizing the window, subtoolbars and action bars (especially PagePanelActionBar) didn't reposition correctly. They only updated on gradual resizing.

**Root Cause:** 
1. The event filter comparison used `m_viewportStack->parentWidget()` instead of comparing directly with `m_canvasContainer` (the object the filter was installed on)
2. The `MainWindow::resizeEvent()` was empty and didn't trigger position updates

**Fix:**
1. Changed event filter to compare `obj == m_canvasContainer` directly
2. Added `updateSubToolbarPosition()` and `updateActionBarPosition()` calls in `resizeEvent()`

**Files Modified:**
- `source/MainWindow.cpp` (eventFilter, resizeEvent)

**Verified:** [x] Subtoolbars reposition on maximize
**Verified:** [x] Action bars reposition on maximize

---

#### BUG-UI-002: Touch scroll in sidebar panels triggers unintended page navigation
**Priority:** 🟡 P2 | **Status:** ✅ FIXED

**Symptom:** 
When scrolling the PagePanel or OutlinePanel with touch, the scroll gesture was also being detected as a tap/click on an item, causing unintended page navigation while trying to scroll.

**Root Cause:** 
Qt's `QScroller::TouchGesture` enables kinetic scrolling for touch, but the `clicked` signal is still emitted when the touch ends on an item, even if the user was scrolling. The click handler didn't distinguish between a genuine tap and a scroll-release.

**Fix:**
In both `PagePanel::onItemClicked()` and `OutlinePanel::onItemClicked()`, check the QScroller's state before processing the click:
- If scroller state is `Dragging` or `Scrolling` → ignore the click
- If scroller state is `Inactive` or `Pressed` → process the click normally

```cpp
QScroller* scroller = QScroller::scroller(viewport);
if (scroller) {
    QScroller::State state = scroller->state();
    if (state == QScroller::Dragging || state == QScroller::Scrolling) {
        return;  // Ignore click during scroll
    }
}
```

**Files Modified:**
- `source/ui/sidebars/PagePanel.cpp` (onItemClicked)
- `source/ui/sidebars/OutlinePanel.cpp` (onItemClicked)

**Verified:** [ ] Touch scroll doesn't trigger page navigation
**Verified:** [ ] Deliberate tap still works for page selection

---

#### BUG-UI-003: Launcher FAB creates extra tab (default tab already exists)
**Priority:** 🟡 P2 | **Status:** ✅ FIXED

**Symptom:** 
When clicking any FAB button in the Launcher (New Edgeless, New Paged, Open PDF, Open Notebook), MainWindow opens with TWO tabs: an unwanted empty paged document tab plus the requested tab.

**Root Cause:** 
`MainWindow` constructor (line 1033) automatically called `addNewTab()` at the end of initialization. When the Launcher then called methods like `addNewEdgelessTab()`, it added a second tab.

**Fix:**
Removed the automatic `addNewTab()` call from the MainWindow constructor. The Launcher and command-line handling explicitly create the appropriate tabs:
- Launcher FAB → calls `addNewTab()`, `addNewEdgelessTab()`, `showOpenPdfDialog()`, or `loadFolderDocument()`
- File argument → calls `openFileInNewTab()`

```cpp
// BEFORE (bug):
QDir().mkpath(tempDir);
addNewTab();  // This created an unwanted default tab
setupSingleInstanceServer();

// AFTER (fixed):
QDir().mkpath(tempDir);
// NOTE: Do NOT call addNewTab() here!
// Launcher and command-line explicitly create tabs.
setupSingleInstanceServer();
```

**Files Modified:**
- `source/MainWindow.cpp` (constructor, removed addNewTab() call)

**Verified:** [ ] FAB "New Edgeless" creates single edgeless tab
**Verified:** [ ] FAB "New Paged" creates single paged tab
**Verified:** [ ] Opening notebook from Timeline creates single tab
**Verified:** [ ] Command-line file argument creates single tab

---

#### BUG-TCH-001: Touch gesture mode button fails to switch modes
**Priority:** 🟠 P1 | **Status:** ✅ FIXED

**Symptom:** 
The touch gesture mode button on the toolbar appeared to cycle through states (debug message confirmed mode changes), but actual touch behavior was always stuck at Full gestures mode.

**Steps to Reproduce:**
1. Open SpeedyNote
2. Click the touch gesture button (hand icon) to cycle modes
3. Observe debug output: "Toolbar: Touch gesture mode changed to 0/1/2"
4. Try touch gestures - they always work as Full mode regardless of button state

**Expected:** Touch gestures should be disabled (mode 0), Y-axis only (mode 1), or full (mode 2)
**Actual:** Touch gestures always behaved as Full mode

**Root Cause:** 
In `MainWindow.cpp` line 971-975, the `touchGestureModeChanged` signal handler only logged the mode but **never called `setTouchGestureMode()`** to actually apply it. There was a TODO comment indicating this was never completed:
```cpp
connect(m_toolbar, &Toolbar::touchGestureModeChanged, this, [this](int mode) {
    qDebug() << "Toolbar: Touch gesture mode changed to" << mode;
    // TODO: Connect to TouchGestureHandler when ready  ← NEVER DONE!
});
```

**Fix:**
1. Updated the signal handler to convert the int mode to `TouchGestureMode` enum and call `setTouchGestureMode()`
2. Updated `setTouchGestureMode()` to sync the toolbar button state (for settings load on startup)

**Files Modified:**
- `source/MainWindow.cpp` (lines 971-982, 3001-3018)

**Verified:** [x] Signal handler now calls setTouchGestureMode()
**Verified:** [x] Toolbar button syncs when mode loaded from settings

---

## Statistics

| Category | New | In Progress | Fixed | Total |
|----------|-----|-------------|-------|-------|
| Viewport | 0 | 0 | 0 | 0 |
| Drawing | 0 | 0 | 0 | 0 |
| Lasso | 0 | 0 | 0 | 0 |
| Highlighter | 0 | 0 | 0 | 0 |
| Objects | 0 | 0 | 0 | 0 |
| Layers | 0 | 0 | 0 | 0 |
| Pages | 0 | 0 | 4 | 4 |
| PDF | 0 | 0 | 0 | 0 |
| File I/O | 0 | 0 | 0 | 0 |
| Tabs | 0 | 0 | 0 | 0 |
| Toolbar | 0 | 0 | 0 | 0 |
| Subtoolbar | 0 | 0 | 0 | 0 |
| Action Bar | 0 | 0 | 1 | 1 |
| Sidebar | 0 | 0 | 0 | 0 |
| Touch | 0 | 0 | 1 | 1 |
| Markdown | 0 | 0 | 0 | 0 |
| Performance | 0 | 0 | 0 | 0 |
| UI/UX | 0 | 0 | 3 | 3 |
| **TOTAL** | **0** | **0** | **9** | **9** |

---

## Notes

### Commit Message Format
```
Fix BUG-VP-001: [Short description]

[Longer explanation if needed]
```

### Code Comment Format
```cpp
// BUG-VP-001 FIX: [Explanation of what this code fixes]
```

### Related Documents
- See `*_QA.md` files for feature-specific test cases
- See `*_SUBPLAN.md` files for implementation details
- Code review fixes are documented as `CR-*` in subplan files

