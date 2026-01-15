# SpeedyNote Bug Tracking

## Overview

This document tracks bugs, regressions, and polish issues discovered during and after the migration. Each bug is assigned a unique ID for reference in commit messages and code comments.

**Format:** `BUG-{CATEGORY}-{NUMBER}` (e.g., `BUG-VP-001` for viewport bugs)

**Last Updated:** Jan 15, 2026 (Optimized first-time save of PDF documents)

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

#### BUG-VP-002: Page navigation causes horizontal shift when sidebar is open
**Priority:** 🟡 P2 | **Status:** ✅ FIXED

**Symptom:** 
When navigating to a different page via the PagePanel or OutlinePanel (with sidebar open), the page would shift horizontally. After closing the sidebar, the page would be significantly off-center, with a large blank area on the left and part of the page outside the window.

**Steps to Reproduce:**
1. Open a paged document at default zoom
2. Note initial Pan X (e.g., -5, properly centered)
3. Open left sidebar (Pages tab)
4. Note Pan X changes (e.g., 120, adjusted for narrower viewport)
5. Click a different page in the PagePanel
6. Pan X resets to 0 (the bug!)
7. Close sidebar
8. Pan X is now ~125 instead of -5, page is shifted right

**Expected:** Page should remain centered after navigation and sidebar toggle
**Actual:** Page shifted significantly to the right after closing sidebar

**Root Cause:** 
Two issues combined:

**Issue 1: `scrollToPage()` reset Pan X to 0**
```cpp
QPointF pos = pagePosition(pageIndex);  // Returns X=0, Y=pagePosition
pos.setY(pos.y() - 10);
setPanOffset(pos);  // Sets BOTH X=0 and Y — wipes out X centering!
```

**Issue 2: `recenterHorizontally()` condition failed with narrow viewport**
When sidebar is open, the viewport is narrower. If content width > narrow viewport width at default zoom, the condition `contentSize.width() < viewportWidth` fails, and `recenterHorizontally()` does nothing. Pan X stays at 0.

Then when sidebar closes:
- `resizeEvent()` tries to "preserve center point" from Pan X = 0
- Result: Pan X becomes positive (page shifted right)

**Fix:**
Two-part fix:

**Part 1: Only modify Y in `scrollToPage()`**
```cpp
// Before: setPanOffset(pos) — sets both X and Y
// After: Only modify Y, preserve X centering
m_panOffset.setY(pos.y() - 10);
recenterHorizontally();
clampPanOffset();
emit panChanged(m_panOffset);
```

**Part 2: Recenter on resize when content is narrower than viewport**
Added to `resizeEvent()`:
```cpp
// Re-center horizontally if content is narrower than viewport
QSizeF contentSize = totalContentSize();
qreal viewportWidth = width() / m_zoomLevel;
if (contentSize.width() < viewportWidth) {
    recenterHorizontally();
}
```

This ensures:
- Page switch preserves X pan (only Y changes)
- Sidebar close → `resizeEvent` recenters for wider viewport
- Zoomed in (content > viewport) → preserves user's horizontal pan

**Files Modified:**
- `source/core/DocumentViewport.cpp` (`scrollToPage()`, `resizeEvent()`)

**Verified:** [x] Page switch with sidebar open preserves X centering
**Verified:** [x] Sidebar close recenters page correctly
**Verified:** [x] Zoomed-in documents preserve horizontal pan on resize

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

#### BUG-TAB-001: Renamed document opens in new tab / breaks existing tab
**Priority:** 🟠 P1 | **Status:** ✅ FIXED

**Symptom:** 
When a document is renamed in the Launcher while still open in MainWindow:
1. The existing tab becomes broken (can't load pages - old path no longer exists)
2. Reopening the renamed document creates a second tab instead of switching to existing

**Steps to Reproduce:**
1. Open a notebook from Launcher Timeline → opens in MainWindow tab
2. Return to Launcher (Escape or Ctrl+H)
3. Rename the document in the Timeline (long press → rename)
4. Switch back to MainWindow
5. **Existing tab is broken:** "Cannot load page: file not found..." errors

**Root Cause:** 
When the folder is renamed on disk, the Document object's internal `m_bundlePath` still points to the old location. Lazy loading then fails because it looks for pages at the old path.

**Fix:**
Two-part solution:

**Part 1: Close tab before renaming**
The folder cannot be safely renamed while the document is open. The Launcher now closes the tab (with save prompt if needed) before renaming:

```cpp
// In Launcher::renameNotebook(), BEFORE renaming:
QString docId = Document::peekBundleId(bundlePath);
if (!docId.isEmpty()) {
    MainWindow* mainWindow = MainWindow::findExistingMainWindow();
    if (mainWindow && mainWindow->closeDocumentById(docId)) {
        // Document was saved and closed, proceed with rename
    }
}
```

**Part 2: Duplicate detection on open**
Before loading a document, check if it's already open (by UUID):

```cpp
// In openFileInNewTab(), before loading:
QString docId = Document::peekBundleId(filePath);
if (!docId.isEmpty()) {
    for (int i = 0; i < m_tabManager->tabCount(); ++i) {
        Document* existingDoc = m_tabManager->documentAt(i);
        if (existingDoc && existingDoc->id == docId) {
            m_tabBar->setCurrentIndex(i);  // Switch to existing tab
            m_documentManager->setDocumentPath(existingDoc, filePath);
            return;
        }
    }
}
```

**New Methods:**
- `Document::peekBundleId(path)` - Lightweight manifest read to get document UUID (reads `notebook_id` key)
- `DocumentManager::setDocumentPath(doc, path)` - Update tracked path for existing doc
- `MainWindow::closeDocumentById(docId)` - Save and close document by UUID

**Bug during implementation:** Initially `peekBundleId()` read from `"id"` key, but the manifest uses `"notebook_id"`. Fixed to read `"notebook_id"` first, falling back to `"id"` for legacy compatibility.

**Files Modified:**
- `source/core/Document.h` (added `peekBundleId()` declaration)
- `source/core/Document.cpp` (added `peekBundleId()` implementation - reads `notebook_id`)
- `source/core/DocumentManager.h` (added `setDocumentPath()` declaration)
- `source/core/DocumentManager.cpp` (added `setDocumentPath()` implementation)
- `source/MainWindow.h` (added `closeDocumentById()` declaration)
- `source/MainWindow.cpp` (added `closeDocumentById()`, duplicate detection)
- `source/ui/launcher/Launcher.cpp` (added Document.h include, call `closeDocumentById()` before rename)

**Verified:** [x] Renaming closes and saves the open document first
**Verified:** [x] Reopening renamed document opens fresh (no stale paths)
**Verified:** [ ] User can cancel rename if they don't want to save
**Verified:** [x] Duplicate detection prevents opening same doc twice

---

### Toolbar (TB)

---

### Subtoolbar (STB)

#### BUG-STB-001: Subtoolbar color presets not persisting after restart
**Priority:** 🟠 P1 | **Status:** ✅ FIXED

**Symptom:** 
Custom color presets in the Pen, Marker, and Highlighter subtoolbars would reset to defaults after closing and reopening the application. However, new tabs created within the same session would correctly show the custom presets.

**Steps to Reproduce:**
1. Open SpeedyNote, select Pen tool
2. Long-press a color preset, change it to a custom color (e.g., orange)
3. Close SpeedyNote
4. Reopen SpeedyNote, open a document from Launcher
5. Color preset shows default (red/blue/black) instead of custom (orange)

**Expected:** Custom color presets should persist across app restarts
**Actual:** Color presets reset to defaults after restart

**Root Cause:** 
Inconsistent `QSettings` constructor usage across the codebase:

| Location | Constructor Used | Settings File |
|----------|-----------------|---------------|
| MainWindow, ControlPanelDialog | `QSettings("SpeedyNote", "App")` | ✅ SpeedyNote/App |
| PenSubToolbar, MarkerSubToolbar, etc. | `QSettings()` (default) | ❌ Empty/unknown |

The default `QSettings()` constructor relies on `QCoreApplication::organizationName()` and `applicationName()` - but these were **never set** in `Main.cpp`! So the subtoolbars were writing to and reading from a **different settings location** than the rest of the application.

**Fix:**
Added organization and application name setup in `Main.cpp` right after creating `QApplication`:

```cpp
app.setOrganizationName("SpeedyNote");
app.setApplicationName("App");
```

This ensures that `QSettings()` (default constructor) now uses the same settings location as `QSettings("SpeedyNote", "App")`.

**Files Modified:**
- `source/Main.cpp` (added setOrganizationName/setApplicationName)

**Verified:** [ ] Custom pen color presets persist after restart
**Verified:** [ ] Custom marker color presets persist after restart  
**Verified:** [ ] Custom thickness presets persist after restart
**Verified:** [ ] Other settings (theme, language, etc.) still work

---

#### BUG-STB-002: ObjectSelect mode toggle mismatch when switching tabs
**Priority:** 🟡 P2 | **Status:** ✅ FIXED

**Symptom:** 
When switching between tabs with ObjectSelect tool active, the insert mode and action mode toggle buttons would show incorrect state. The visual icon wouldn't match the actual mode being used, causing confusion.

**Steps to Reproduce:**
1. Open Tab 1, select ObjectSelect tool, set Insert Mode to "Link"
2. Open Tab 2, select ObjectSelect tool (defaults to "Image")
3. Switch back to Tab 1
4. Toggle button shows "Image" but viewport is in "Link" mode (mismatch)

**Expected:** Toggle buttons should reflect the current viewport's actual mode
**Actual:** Toggle buttons showed stale state from per-tab subtoolbar storage

**Root Cause:** 
Conflicting sources of truth for object modes:

| Step | Action | Result |
|------|--------|--------|
| 1 | Tab switch triggers `connectViewportScrollSignals()` | Syncs mode FROM viewport TO subtoolbar ✅ |
| 2 | Then `restoreTabState()` is called | OVERWRITES subtoolbar with saved tab state ❌ |
| 3 | Then `applyAllSubToolbarValuesToViewport()` | Pushes wrong state TO viewport ❌ |

The viewport stores its own `m_objectInsertMode` and `m_objectActionMode` (per-document state). But `ObjectSelectSubToolbar::restoreTabState()` was saving/restoring modes as per-tab subtoolbar state, creating a conflict.

**Fix:**
Removed mode save/restore from `ObjectSelectSubToolbar`. The viewport is now the sole source of truth for object modes:

```cpp
void ObjectSelectSubToolbar::restoreTabState(int tabIndex)
{
    // BUG-STB-002 FIX: Do NOT restore modes here.
    // Viewport is source of truth, synced via setInsertModeState()/setActionModeState()
    Q_UNUSED(tabIndex);
}
```

The subtoolbar now only receives mode updates from:
- `setInsertModeState()` / `setActionModeState()` - called when tab switches
- Keyboard shortcuts (Ctrl+< / Ctrl+>) via viewport signals

**Files Modified:**
- `source/ui/subtoolbars/ObjectSelectSubToolbar.cpp` (removed mode save/restore)
- `source/ui/subtoolbars/ObjectSelectSubToolbar.h` (removed TabState struct)

**Verified:** [ ] Switching tabs correctly updates mode toggles
**Verified:** [ ] Mode changes via toggle button work correctly
**Verified:** [ ] Mode changes via keyboard shortcuts sync to toggle

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

#### BUG-PERF-001: First-time save of PDF documents extremely slow
**Priority:** 🟠 P1 | **Status:** ✅ FIXED

**Symptom:** 
Saving a PDF document for the first time (loading a PDF and saving as .snb) was extremely slow on weaker devices. A 3000+ page PDF could take minutes to save initially, but subsequent saves were nearly instant.

**Root Cause:**
During first-time save, the legacy-to-UUID migration marked ALL pages as dirty:

```cpp
// Before fix
for (auto& page : m_pages) {
    m_dirtyPages.insert(uuid);  // ALL 3000+ pages marked dirty!
}
```

This caused 3000+ individual page JSON files to be written, even though most pages had no user content (just PDF background reference).

**Fix:**
Implemented "pristine PDF page" optimization:

1. **Track PDF page indices in manifest** (`m_pagePdfIndex` map):
   - `page_metadata` now includes `pdf_page` for PDF pages
   - Enables on-demand page synthesis without individual files

2. **Skip saving pristine PDF pages** in `saveBundle()`:
   - Pages with `backgroundType == PDF` and `hasContent() == false` are not written to disk
   - Their PDF page index is stored in manifest metadata instead

3. **Synthesize on load** in `loadPageFromDisk()`:
   - If page file doesn't exist but `pdf_page` metadata exists, create page in memory
   - Uses size from manifest + PDF page index + document defaults

**Performance Impact:**
- **Before:** First save writes N page files (O(N) file operations)
- **After:** First save writes only pages with content + 1 manifest (O(k+1) where k = edited pages)
- For 3000-page PDF with 5 edited pages: **3000 files → 6 files** (99.8% reduction!)

**Files Changed:**
- `source/core/Document.h`: Added `m_pagePdfIndex` map
- `source/core/Document.cpp`: Modified `saveBundle()`, `loadBundle()`, `loadPageFromDisk()`

**Backward Compatibility:**
- Old bundles with individual page files still work normally
- Files are loaded as usual; shrinking only happens on re-save

**Verified:** [ ] First save of large PDF is fast; subsequent loads/edits work correctly

---

### UI/UX (UI)

#### BUG-UI-004: NavigationBar filename not updated after Save As
**Priority:** 🟡 P2 | **Status:** ✅ FIXED

**Symptom:** 
After saving a document with "Save As" and choosing a new filename, the NavigationBar still displayed the old name (e.g., "Untitled") while the tab title correctly showed the new name.

**Steps to Reproduce:**
1. Create a new paged document (shows "Untitled" in NavigationBar)
2. Draw something
3. Press Ctrl+S to save
4. Choose a new filename like "MyNotes.snb"
5. NavigationBar still shows "Untitled"

**Expected:** NavigationBar should show "MyNotes" after saving
**Actual:** NavigationBar showed "Untitled"

**Root Cause:** 
The `saveDocument()` method updated the tab title via `m_tabManager->setTabTitle()` but didn't update the NavigationBar via `m_navigationBar->setFilename()`. The NavigationBar was only updated on tab switch.

| Location | Tab Title | NavigationBar |
|----------|-----------|---------------|
| Tab switch | N/A | ✅ Updated |
| Save As | ✅ Updated | ❌ NOT updated |
| Tab close with save | ✅ Updated | ❌ NOT updated |

**Fix:**
Added `m_navigationBar->setFilename()` calls after all `setTabTitle()` updates:

1. In `saveDocument()` after Save As dialog (line ~2145)
2. In `tabCloseRequested` handler after save (line ~400)

**Files Modified:**
- `source/MainWindow.cpp` (two locations)

**Verified:** [ ] Save As updates NavigationBar filename
**Verified:** [ ] Tab close with save updates NavigationBar filename

---

#### BUG-UI-005: Page Panel action bar not shown when switching back from edgeless tab
**Priority:** 🟡 P2 | **Status:** ✅ FIXED

**Symptom:** 
When switching from a paged document tab to an edgeless document tab and back, the Page Panel action bar fails to reappear even though the Page Panel is visible and the document is paged.

**Steps to Reproduce:**
1. Open a paged document (Page Panel action bar shows correctly)
2. Switch to an edgeless document tab (action bar hides, sidebar switches to Layers tab)
3. Switch back to the paged document tab
4. Page Panel shows, but action bar does NOT appear

**Expected:** Action bar should reappear when switching back to paged document
**Actual:** Action bar stays hidden

**Root Cause:** 
Race condition in `TabManager::onCurrentChanged()`. The `currentViewportChanged` signal was emitted BEFORE the `m_viewportStack` index was updated. When `updatePagePanelActionBarVisibility()` called `currentViewport()`, it returned the **old** viewport (edgeless document) instead of the new one (paged document).

Signal connection order in constructor:
```cpp
// Connection 1: emits currentViewportChanged (runs first)
connect(m_tabBar, &QTabBar::currentChanged, this, &TabManager::onCurrentChanged);
// Connection 2: sets stack index (runs second - TOO LATE!)
connect(m_tabBar, &QTabBar::currentChanged, m_viewportStack, &QStackedWidget::setCurrentIndex);
```

**Fix:**
In `TabManager::onCurrentChanged()`, explicitly sync the viewport stack BEFORE emitting the signal:

```cpp
void TabManager::onCurrentChanged(int index)
{
    // CRITICAL: Sync viewport stack BEFORE emitting signal
    if (m_viewportStack && index >= 0 && index < m_viewportStack->count()) {
        m_viewportStack->setCurrentIndex(index);
    }
    
    DocumentViewport* viewport = ...;
    emit currentViewportChanged(viewport);
}
```

Also removed the redundant second connection since `onCurrentChanged` now handles it.

**Files Modified:**
- `source/ui/TabManager.cpp`

**Verified:** [x] Switching between paged and edgeless tabs correctly shows/hides action bar

---

#### BUG-UI-006: Control Panel settings override PDF backgrounds
**Priority:** 🔴 P1 | **Status:** ✅ FIXED

**Symptom:** 
When opening the Control Panel and clicking Apply or OK (even without changing any background settings), PDF backgrounds are overwritten with the grid/lines/none setting from the Control Panel.

**Steps to Reproduce:**
1. Open a PDF document in SpeedyNote
2. Open Control Panel (Settings)
3. Change any setting (e.g., theme, language) or just click "Apply"
4. PDF background disappears, replaced by grid/lines/none

**Expected:** PDF backgrounds should be preserved when applying Control Panel settings
**Actual:** All pages unconditionally have their `backgroundType` overwritten

**Root Cause:** 
In `MainWindow::applyBackgroundSettings()`, the function unconditionally set `page->backgroundType = type` for ALL pages, including pages with `Page::BackgroundType::PDF`:

```cpp
for (int i = 0; i < doc->pageCount(); ++i) {
    Page* page = doc->page(i);
    if (page) {
        page->backgroundType = type;  // ← Overwrites PDF backgrounds!
        // ...
    }
}
```

When `ControlPanelDialog::applyChanges()` calls `mainWindowRef->applyBackgroundSettings()`, it passes the current combo box value (Grid/Lines/None), which replaces the PDF background type.

**Fix:**
Added a guard in `MainWindow::applyBackgroundSettings()` to skip pages with PDF backgrounds:

```cpp
for (int i = 0; i < doc->pageCount(); ++i) {
    Page* page = doc->page(i);
    if (page) {
        // Preserve PDF backgrounds - only apply settings to non-PDF pages
        if (page->backgroundType != Page::BackgroundType::PDF) {
            page->backgroundType = type;
        }
        // Colors and spacing are still applied (for overlays, etc.)
        page->backgroundColor = bgColor;
        // ...
    }
}
```

Same logic applied to edgeless tiles for consistency.

**Files Modified:**
- `source/MainWindow.cpp` - Added PDF background check in `applyBackgroundSettings()`

**Verified:** [ ] PDF document backgrounds preserved after Control Panel changes
**Verified:** [ ] Non-PDF pages still get background settings applied
**Verified:** [ ] Newly added pages in PDF document follow document defaults

---

### Miscellaneous (MISC)

#### BUG-MISC-001: Dead code returnToLauncher() shows placeholder message
**Priority:** 🟢 P3 | **Status:** ✅ FIXED

**Symptom:** 
The `returnToLauncher()` method existed in `MainWindow` but only showed a placeholder message: "Launcher is being redesigned. This feature will return soon!"

**Root Cause:** 
During the launcher redesign (Phase P.4), the old `returnToLauncher()` was temporarily disabled with a placeholder. The proper replacement `toggleLauncher()` was implemented later but the old method was never removed.

| Method | Status | Connected To |
|--------|--------|--------------|
| `toggleLauncher()` | ✅ Active | NavigationBar button, Ctrl+H, Escape key |
| `returnToLauncher()` | ❌ Dead code | Nothing (only in OLD files) |

**Fix:**
Removed the obsolete `returnToLauncher()` declaration and implementation:
- `MainWindow.h` - Removed declaration
- `MainWindow.cpp` - Removed implementation, added comment pointing to `toggleLauncher()`

**Files Modified:**
- `source/MainWindow.h` (removed declaration)
- `source/MainWindow.cpp` (removed implementation)

**Verified:** [ ] Launcher button still works (uses toggleLauncher)
**Verified:** [ ] Ctrl+H still toggles launcher
**Verified:** [ ] Escape key still toggles launcher

---

#### CLEANUP-MISC-002: Removed legacy .snx/.json loading code
**Priority:** 🟢 P3 | **Status:** ✅ FIXED

**Background:** 
SpeedyNote originally planned two file formats: `.snx` (a proposed packaged format) and raw `.json` files. However, the unified `.snb` folder-based bundle format was implemented instead, making the `.snx` and standalone `.json` loaders dead code.

**What Was Removed:**
- `DocumentManager.cpp` lines 189-236: Complete `.snx`/`.json` file loading code path that read raw JSON files and called `Document::fromFullJson()`
- File dialog filter in `MainWindow.cpp` no longer offers "Legacy JSON (*.json *.snx)" option

**What Was Updated:**
- `DocumentManager.h` comments: Updated to mention `.snb bundles` instead of `.snx format`
- `MainWindow.cpp` comments: Updated to reflect that all documents use `.snb` bundles
- `Main.cpp` comments: Updated file format references
- File open dialog now shows: `"SpeedyNote Files (*.snb *.pdf);;SpeedyNote Bundle (*.snb);;PDF Documents (*.pdf);;All Files (*)"`

**What Was Kept:**
- `Document::toFullJson()` / `Document::fromFullJson()` - Still used internally for `.snb` bundle's `document.json` manifest
- `Document.cpp` legacy format version comment - Still relevant for bundle versioning

**Files Modified:**
- `source/core/DocumentManager.cpp` (removed ~50 lines of legacy loader)
- `source/core/DocumentManager.h` (updated comments)
- `source/MainWindow.cpp` (updated file dialog filter and comments)
- `source/Main.cpp` (updated comments)

---

#### BUG-MISC-003: Backtick key auto-repeat causes debug spam
**Priority:** 🟢 P3 | **Status:** ✅ FIXED

**Symptom:** 
When holding the backtick (`) key for deferred vertical panning, the console would be flooded with debug messages like:
```
keyPressEvent: currentTool = 0 (ObjectSelect= 5 )
```
These messages repeated rapidly until the key was released.

**Root Cause:** 
Two issues combined to cause this:

1. **Auto-repeat events not consumed**: The backtick handler only caught the initial press (`!event->isAutoRepeat()`), but auto-repeat events fell through to the rest of `keyPressEvent()`.

2. **Unconditional debug statements**: Debug prints at lines 2077-2078 ran for every key event that wasn't the initial backtick press, causing spam.

**Fix:**
1. Changed backtick handling to consume ALL backtick events (initial + auto-repeat), only setting `m_backtickHeld = true` on initial press
2. Removed unnecessary debug statements from `keyPressEvent()`

```cpp
// Before: Only caught initial press, auto-repeat fell through
if (event->key() == Qt::Key_QuoteLeft && !event->isAutoRepeat()) {
    m_backtickHeld = true;
    event->accept();
    return;
}

// After: Catches all backtick events
if (event->key() == Qt::Key_QuoteLeft) {
    if (!event->isAutoRepeat()) {
        m_backtickHeld = true;
    }
    event->accept();
    return;  // Always return to prevent fallthrough
}
```

**Files Modified:**
- `source/core/DocumentViewport.cpp` (fixed backtick handling, removed debug prints)

---

#### BUG-VP-001: Undo operates on wrong page in two-column mode
**Priority:** 🟡 P2 | **Status:** ✅ FIXED

**Symptom:** 
In two-column layout mode, when the user alternates between editing the left and right pages, undo/redo would operate on the wrong page. This happened because the "current page" was determined by viewport center, not by which page the user was actually editing.

**Steps to Reproduce:**
1. Open a multi-page document
2. Press Ctrl+2 to enable auto two-column layout (on wide viewport)
3. Draw on the left page
4. Draw on the right page
5. Draw on the left page again
6. Press Ctrl+Z to undo
7. **Expected:** Undo on left page (last edited)
8. **Actual:** Undo on right page (at viewport center)

**Root Cause:** 
`m_currentPageIndex` was only updated by `updateCurrentPageIndex()`, which uses the viewport center to determine the current page. In two-column mode, both pages are equally visible, and the center often falls in the gap between them, causing incorrect page detection.

**Fix:**
Added logic in `handlePointerPress()` to update `m_currentPageIndex` when the user touches a page with any editing tool. This ensures the current page matches user intent:

```cpp
// In handlePointerPress(), after determining m_activeDrawingPage:
if (!m_document->isEdgeless() && pe.pageHit.valid()) {
    int touchedPage = pe.pageHit.pageIndex;
    if (touchedPage != m_currentPageIndex) {
        m_currentPageIndex = touchedPage;
        emit currentPageChanged(m_currentPageIndex);
        emit undoAvailableChanged(canUndo());
        emit redoAvailableChanged(canRedo());
    }
}
```

**Design Rationale:**
- Touch/click intent is clearer than viewport center for determining "current" page
- Works for all tools (Pen, Eraser, Lasso, ObjectSelect, Highlighter)
- Minimal overhead: single comparison + potential signal emit
- Edgeless mode excluded (only has one logical "page")

**Files Modified:**
- `source/core/DocumentViewport.cpp` (added touch-sets-current-page logic)

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

### BUG-STB-003: Highlighter color buttons don't affect highlighter color ✅ FIXED

**Date Identified:** 2026-01-15
**Severity:** High (Feature broken)
**Category:** Subtoolbar

**Symptom:** Clicking color preset buttons on the HighlighterSubToolbar had no effect on the highlighter color. The highlighter tool always used the default yellow color.

**Root Cause:** The `HighlighterSubToolbar::highlighterColorChanged` signal was connected to `vp->setMarkerColor()` in MainWindow, but the Highlighter tool uses its own separate `m_highlighterColor` member variable in DocumentViewport. There was no `setHighlighterColor()` method.

**Fix:**
1. Added `setHighlighterColor(const QColor& color)` method to `DocumentViewport`
2. Updated MainWindow to connect `highlighterColorChanged` to `setHighlighterColor()` instead of `setMarkerColor()`
3. Updated `applyAllSubToolbarValuesToViewport()` to also apply highlighter color

**Files Modified:**
- `source/core/DocumentViewport.h` - Added `setHighlighterColor()` and `highlighterColor()` methods
- `source/core/DocumentViewport.cpp` - Implemented `setHighlighterColor()`
- `source/MainWindow.cpp` - Fixed signal connection and `applyAllSubToolbarValuesToViewport()`

**Verified:** [x] Clicking highlighter color buttons now changes the highlighter stroke color

---

### BUG-STB-004: Marker/Highlighter color presets not syncing when switching tools ✅ FIXED

**Date Identified:** 2026-01-15
**Severity:** Medium (Inconsistent UI)
**Category:** Subtoolbar

**Symptom:** When changing a color preset on the HighlighterSubToolbar and then switching to the Marker tool, the MarkerSubToolbar's color buttons still showed the old colors. The presets are designed to be shared, but the in-memory button colors weren't syncing.

**Root Cause:** Both subtoolbars share the same QSettings keys for colors (`marker/color1-3`), but when one subtoolbar modifies a color and saves to QSettings, the other subtoolbar's in-memory ColorPresetButton values are not updated.

**Fix:**
1. Added `syncSharedState()` virtual method to `SubToolbar` base class (default empty implementation)
2. Added `syncSharedColorsFromSettings()` method to both `MarkerSubToolbar` and `HighlighterSubToolbar` to reload shared colors from QSettings
3. Override `syncSharedState()` in both subtoolbars to call `syncSharedColorsFromSettings()`
4. Modified `SubToolbarContainer::showForTool()` to call `syncSharedState()` on the incoming subtoolbar

**Files Modified:**
- `source/ui/subtoolbars/SubToolbar.h` - Added virtual `syncSharedState()` method
- `source/ui/subtoolbars/MarkerSubToolbar.h` - Added method declarations
- `source/ui/subtoolbars/MarkerSubToolbar.cpp` - Implemented methods
- `source/ui/subtoolbars/HighlighterSubToolbar.h` - Added method declarations
- `source/ui/subtoolbars/HighlighterSubToolbar.cpp` - Implemented methods
- `source/ui/subtoolbars/SubToolbarContainer.cpp` - Call `syncSharedState()` in `showForTool()`

**Verified:** [x] Switching between Marker and Highlighter now shows synchronized color presets

---

### BUG-STB-005: Auto-highlight toggle state inconsistent when switching tabs ✅ FIXED

**Date Identified:** 2026-01-15
**Severity:** Medium (Confusing UX)
**Category:** Subtoolbar

**Symptom:** The auto-highlight toggle button on the HighlighterSubToolbar would randomly flip states when switching between tabs. If tab 1 had it ON and tab 2 had it OFF, switching back and forth would cause the states to swap or change unpredictably.

**Root Cause:** **Duplicate state storage** - the auto-highlight state was stored in two places:
1. `DocumentViewport::m_autoHighlightEnabled` - the TRUE per-viewport state (source of truth)
2. `HighlighterSubToolbar::TabState::autoHighlightEnabled` - a STALE duplicate

When switching tabs, two conflicting operations occurred:
1. `connectViewportScrollSignals()` synced the subtoolbar to the viewport's CORRECT state
2. `onTabChanged()` → `restoreTabState()` OVERWROTE with the subtoolbar's STALE duplicate state

Since the handlers ran in sequence, the subtoolbar's stale state won, causing incorrect toggle states.

**Fix:** Removed `autoHighlightEnabled` from `HighlighterSubToolbar::TabState`. The viewport is now the single source of truth, and the subtoolbar only syncs from the viewport via `setAutoHighlightState()`.

This follows the same pattern used for `ObjectSelectSubToolbar` modes (BUG-STB-002).

**Files Modified:**
- `source/ui/subtoolbars/HighlighterSubToolbar.h` - Removed `autoHighlightEnabled` from `TabState` struct
- `source/ui/subtoolbars/HighlighterSubToolbar.cpp` - Removed save/restore of auto-highlight state

**Verified:** [x] Auto-highlight toggle state is now consistent per-tab after switching

---

### CLEANUP-MISC-003: Redundant format_version in document manifest ✅ FIXED

**Date Identified:** 2026-01-15
**Severity:** Low (Confusing/Legacy cruft)
**Category:** Miscellaneous

**Symptom:** The `document.json` manifest contained two version fields:
- `bundle_format_version: 1` (integer)
- `format_version: "2.0"` (string)

This was confusing as they seemed redundant and had inconsistent values.

**Root Cause:** `format_version` was a legacy field intended for the never-implemented `.snx` single-file format. When the `.snb` bundle format was implemented, a new `bundle_format_version` integer was added for forward compatibility checks. The old `format_version` string was left in for "backward compatibility" but served no purpose.

**Fix:** Removed the `format_version` field entirely:
1. Removed `formatVersion` member variable from `Document.h`
2. Removed writing `format_version` to manifest in `toJson()`
3. Old files with `format_version` are still loadable (the field is simply ignored)
4. Updated `DocumentTests.h` to remove the obsolete test

The `bundle_format_version` integer is now the single source of truth for format versioning.

**Files Modified:**
- `source/core/Document.h` - Removed `formatVersion` member
- `source/core/Document.cpp` - Removed write/read of `format_version`
- `source/core/DocumentTests.h` - Removed obsolete test

**Verified:** [x] New documents only have `bundle_format_version` in manifest

---

## Statistics

| Category | New | In Progress | Fixed | Total |
|----------|-----|-------------|-------|-------|
| Viewport | 0 | 0 | 2 | 2 |
| Drawing | 0 | 0 | 0 | 0 |
| Lasso | 0 | 0 | 0 | 0 |
| Highlighter | 0 | 0 | 0 | 0 |
| Objects | 0 | 0 | 0 | 0 |
| Layers | 0 | 0 | 0 | 0 |
| Pages | 0 | 0 | 4 | 4 |
| PDF | 0 | 0 | 0 | 0 |
| File I/O | 0 | 0 | 0 | 0 |
| Tabs | 0 | 0 | 1 | 1 |
| Toolbar | 0 | 0 | 0 | 0 |
| Subtoolbar | 0 | 0 | 5 | 5 |
| Action Bar | 0 | 0 | 1 | 1 |
| Sidebar | 0 | 0 | 0 | 0 |
| Touch | 0 | 0 | 1 | 1 |
| Markdown | 0 | 0 | 0 | 0 |
| Performance | 0 | 0 | 1 | 1 |
| UI/UX | 0 | 0 | 4 | 4 |
| Miscellaneous | 0 | 0 | 4 | 4 |
| **TOTAL** | **0** | **0** | **23** | **23** |

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

