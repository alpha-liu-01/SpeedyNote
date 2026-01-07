# MainWindow Cleanup Subplan

**Document Version:** 1.5  
**Date:** January 6, 2026  
**Status:** ✅ PHASE 1-7 COMPLETE - MainWindow cleanup finished!  
**Prerequisites:** Q&A completed in `MAINWINDOW_CLEANUP_QA.md`  
**Related:** `TOOLBAR_EXTRACTION_SUBPLAN.md` (Phase 3 details)

---

## Overview

MainWindow.cpp was the last major piece of legacy code in SpeedyNote. This cleanup reduced it from ~9,700 lines to ~2,970 lines - a **69% reduction**!

### Goals - ALL ACHIEVED ✅
1. ✅ Delete dead code (~2,800 lines saved!)
2. ✅ Delete dial system entirely (simpler than extraction)
3. ✅ Extract toolbar components (NavigationBar, Toolbar, TabBar)
4. ✅ MW5.1-5.8: Delete controlBar, zoomSlider, PDF functions, toggles, presets, .spn, autoScroll, rework pan sliders
5. ✅ Phase 7: Deep cleanup (dial remnants, bookmarks, outlines, mappings, stubs)
6. ⬜ Phase 6: Extract input handling to hub classes (Future - optional)

### Final Results
| Metric | Before | After | Reduction |
|--------|--------|-------|-----------|
| MainWindow.cpp lines | 9,722 | **2,970** | **-6,752 (69%)** |
| MainWindow.h lines | 887 | **381** | **-506 (57%)** |
| Dead code blocks | ~1,500 | 0 | ✅ |
| Dial system | Complex 7-mode | Deleted | ✅ |
| Toolbar | Monolithic | Extracted | ✅ |
| Old controlBar | Present | Deleted | ✅ |
| Pan sliders | Buggy positioning | Keyboard-aware auto-hide | ✅ |

### New Components Created (Phase 3)
| Component | File | Purpose |
|-----------|------|---------|
| NavigationBar | `source/ui/NavigationBar.h/cpp` | Top bar: launcher, save, fullscreen, etc. |
| Toolbar | `source/ui/Toolbar.h/cpp` | Tool selection: pen, marker, eraser, etc. |
| TabBar | `source/ui/TabBar.h/cpp` | Custom tab bar with theming |
| StyleLoader | `source/ui/StyleLoader.h/cpp` | QSS loading with placeholders |
| ToolbarButtons | `source/ui/ToolbarButtons.h/cpp` | Reusable button types |

---

## Phase 1: Delete Dead Code

**Goal:** Remove all code that will never be used again.  
**Estimated savings:** ~1,500 lines  
**Risk:** Low (all code is already disabled or hidden)

### MW1.1: Delete `#if 0` Export Code Blocks

The old PDF export logic using pdftk is completely deprecated. A full rewrite is planned.

**Files:** `source/MainWindow.cpp`

**Delete these `#if 0` blocks:**

| Location | Description | Approx Lines |
|----------|-------------|--------------|
| ~2400-2700 | `exportPdfWithAnnotations()` pdftk-based export | ~300 |
| ~2700-2900 | `exportSimplePdf()` fallback export | ~200 |
| ~2900-3100 | `exportPdfInternal()` helper | ~200 |
| ~3100-3300 | `exportPdfWithQPdfWriter()` | ~200 |
| ~3300-3500 | `filterAndAdjustOutline()` outline manipulation | ~150 |

**Implementation:**
```cpp
// Search for: #if 0
// Delete entire blocks from #if 0 to matching #endif
// Verify no active code references deleted functions
```

**Verification:**
- [ ] `./compile.sh` succeeds
- [ ] No undefined references
- [ ] Application runs normally

---

### MW1.2: Delete Background Selection Feature

Feature was dropped long ago, button has been hidden.

**Files:** `source/MainWindow.cpp`, `source/MainWindow.h`

**Delete:**
1. `backgroundButton` declaration in `.h`
2. `backgroundButton` creation/setup in constructor
3. `selectBackground()` method definition (~10-20 lines)
4. Any signal connections to `backgroundButton`
5. Any style updates for `backgroundButton` in `updateTheme()`

**Search patterns:**
```
backgroundButton
selectBackground
```

**Verification:**
- [ ] `./compile.sh` succeeds
- [ ] No references to `backgroundButton`

---

### MW1.3: Delete Prev/Next Page Buttons ✅ COMPLETED

Prev/next buttons are already hidden. Only the spinbox is kept.

**Files:** `source/MainWindow.cpp`, `source/MainWindow.h`

**Deleted:**
1. ✅ `prevPageButton`, `nextPageButton` declarations
2. ✅ Button creation in constructor
3. ✅ Related signal connections
4. ✅ setVisible(false) calls
5. ✅ Style updates in `updateTheme()`

**Kept (still in use by keyboard shortcuts, dial controls):**
- ✅ `pageInput` spinbox
- ✅ `onPageInputChanged()` slot
- ✅ `switchPageWithDirection()` - already stubbed, called from 10+ places
- ✅ `goToPreviousPage()`, `goToNextPage()` - used by keyboard shortcuts

**Verification:**
- [x] `./compile.sh` succeeds
- [x] Page spinbox still visible

---

### MW1.4: Delete Old InkCanvas References ✅ COMPLETED

Any remaining references to the old InkCanvas system.

**Files:** `source/MainWindow.cpp`, `source/MainWindow.h`

**Deleted (truly unused):**
1. ✅ `handleEdgeProximity(InkCanvas*, QPoint&)` - no callers
2. ✅ `applyDefaultBackgroundToCanvas(InkCanvas*)` - no callers
3. ✅ `showLastAccessedPageDialog(InkCanvas*)` - no callers

**Kept as stubs (still called from legacy code paths):**
- `currentCanvas()` - returns nullptr, called from many places
- `getCurrentPageForCanvas(InkCanvas*)` - returns 0, called from InkCanvas.cpp + MainWindow
- `ensureTabHasUniqueSaveFolder(InkCanvas*)` - returns true, called from tab close logic

**Note:** Full InkCanvas removal requires migrating all callers first.

**Verification:**
- [x] `./compile.sh` succeeds

---

### MW1.5: Delete Unused Method Stubs ✅ COMPLETED

User manually deleted many stubs. Fixed compilation by:

**Deleted (user manually removed):**
- `saveCanvas()`, `saveCurrentPageConcurrent()`
- `exportAnnotatedPdf()`, `clearPdf()`, `handleSmartPdfButton()`
- `applyZoom()`, `findTabWithNotebookId()`
- `handleTouchZoomChange()`, `handleTouchPanChange()`, `handleTouchGestureEnd()`, `handleTouchPanningChanged()`
- `showRopeSelectionMenu()`, `onMarkdownNotesUpdated()`
- `onMarkdownNoteContentChanged()`, `onMarkdownNoteDeleted()`, `onHighlightLinkClicked()`, `onHighlightDoubleClicked()`
- `loadMarkdownNotesForCurrentPage()`, `toggleCurrentPageBookmark()`
- `onEarlySaveRequested()`

**Kept as stubs (still referenced):**
- `switchPage()`, `switchPageWithDirection()`, `saveCurrentPage()`
- `enableStylusButtonMode()`, `disableStylusButtonMode()`
- `saveBookmarks()`, `openPdfFile()`, `switchToExistingNotebook()`

**Disconnected signals (commented out):**
- PDF buttons: `handleSmartPdfButton`, `clearPdf`, `exportAnnotatedPdf`
- Markdown sidebar: `noteContentChanged`, `noteDeleted`, `highlightLinkClicked`
- Bookmark toggle button

**Verification:**
- [x] `./compile.sh` succeeds

---

## Phase 2: Delete Dial System ✅ COMPLETED

**Goal:** Remove entire dial system - it's overengineered and unnecessary.  
**Actual savings:** ~1,240 lines from MainWindow + all dial files deleted  
**Risk:** Low (removing unused complexity)

### Rationale (from Q&A discussion):
- The QDial widget and 7-mode system was overengineered
- Most functionality works fine via mouse/touch/keyboard
- Surface Dial hardware users are a tiny minority
- Fresh minimal implementation beats migrating messy code
- The dial display doesn't belong in a dial anymore - it can be a simple status widget later

### MW2.1: Create Directory Structure ✅ COMPLETED (then deleted)

Initially created skeleton files, then decided to delete everything:
```
source/input/           # ENTIRE FOLDER DELETED
├── DialTypes.h         # DELETED
├── DialController.h    # DELETED
├── DialController.cpp  # DELETED
├── MouseDialHandler.h  # DELETED
├── MouseDialHandler.cpp # DELETED

source/ui/
├── DialModeToolbar.h   # DELETED
├── DialModeToolbar.cpp # DELETED
```

---

### MW2.2: Delete Dial Mode System from MainWindow ✅ COMPLETED

**DELETED from MainWindow.cpp (~1,190 lines):**

1. **Mode handler functions (14 functions):**
   - `handleDialInput()`, `onDialReleased()`
   - `handleDialZoom()`, `onZoomReleased()`
   - `handleDialThickness()`, `onThicknessReleased()`
   - `handleToolSelection()`, `onToolReleased()`
   - `handlePresetSelection()`, `onPresetReleased()`
   - `handleDialPanScroll()`, `onPanScrollReleased()`
   - `changeDialMode()` (mode switching with connect/disconnect mess)

2. **QDial widget and mode toolbar:**
   - `pageDial` (QDial widget)
   - Mode selection buttons and container
   - All dial styling code
   - `dialContainer` and related drag handling

3. **Mouse dial system:**
   - `handleMouseWheelDial()`
   - `startMouseDialMode()`, `stopMouseDialMode()`
   - Mouse button combination tracking

4. **Removed from header (~65 lines)**

---

### MW2.3: Delete Button Hold Mappings ✅ COMPLETED

Hold mappings only served dial mode purposes. Removed:

**MainWindow.cpp:**
- `getHoldMapping()` function
- Hold mapping code from `saveButtonMappings()`
- Hold mapping code from `loadButtonMappings()`
- Dial mode migration from `migrateOldButtonMappings()`

**MainWindow.h:**
- `getHoldMapping()` declaration
- `buttonHoldMapping` member variable

---

### MW2.4: Delete Skeleton Files ✅ COMPLETED

All dial-related files deleted:
- `source/input/` folder entirely (DialTypes.h, DialController.h/cpp, MouseDialHandler.h/cpp)
- `source/ui/DialModeToolbar.h/cpp`

---

### MW2.5: Update CMakeLists.txt ✅ COMPLETED

Removed dial controller conditional compilation (~20 lines):
- `option(ENABLE_DIAL_CONTROLLER ...)` 
- `DIAL_SOURCES` variable
- Android conditional logic for dial

---

### MW2.6: SDLControllerManager Decision ✅ KEPT

**Decision:** Keep SDLControllerManager for future gamepad support.
- Dial mode switching will be removed from it later
- Gamepad buttons can directly trigger actions without dial modes
- This is a separate cleanup task (not blocking)

---

### MW2.7: Verification ✅ COMPLETED

**Compile and test:**
- [x] `./compile.sh` succeeds
- [x] Application builds without dial code

**Line count results (Phase 2):**
- MainWindow.cpp: 8,186 → 6,910 (**-1,276 lines**)
- MainWindow.h: 887 → 812 (**-75 lines**)
- CMakeLists.txt: 518 → 497 (**-21 lines**)
- Deleted files: 7 files in source/input/ and source/ui/

**Line count results (after Phase 3):**
- MainWindow.cpp: 6,910 → 6,633 (**-277 lines**)
- MainWindow.h: 812 → 836 (+24 lines for new component members)
- New UI components: ~1,000 lines (modular, maintainable)

---

## Phase 3: Toolbar Extraction ✅ COMPLETED

**Goal:** Extract toolbar components from MainWindow into reusable classes.  
**Actual savings:** ~280 lines from MainWindow + new modular components  
**Risk:** Medium (visual layout changes) - Successfully completed

**See:** `TOOLBAR_EXTRACTION_SUBPLAN.md` for full implementation details.

### MW3.1: Create NavigationBar ✅ COMPLETED

**Created:** `source/ui/NavigationBar.h/cpp`

**Features:**
- Launcher button (stub)
- Left sidebar toggle
- Save button
- Add button (stub)
- Filename display (updates on tab switch)
- Fullscreen toggle
- Share button (stub)
- Right sidebar toggle
- Menu button (overflow menu)

**Theming:** `updateTheme(bool darkMode, QColor accentColor)`

---

### MW3.2: Create Toolbar ✅ COMPLETED

**Created:** `source/ui/Toolbar.h/cpp`

**Features:**
- Tool buttons (exclusive): Pen, Marker, Eraser, Shape, Lasso, ObjectInsert, Text
- Action buttons: Undo, Redo
- Mode button: Touch Gesture (3-state)

**Theming:** `updateTheme(bool darkMode)`

---

### MW3.3: Create TabBar ✅ COMPLETED

**Created:** `source/ui/TabBar.h/cpp`

**Features:**
- Inherits from QTabBar
- Self-configuring (expanding, movable, closable, scroll buttons, elide)
- Close button on left (applied before first tab creation)
- Theme-aware styling via QSS

**Theming:** `updateTheme(bool darkMode, QColor accentColor)`

---

### MW3.4: Comment Out Old Buttons ✅ COMPLETED

**Commented out from controlBar (functionality moved to new components):**
- `toggleTabBarButton` → NavigationBar::filenameClicked
- `toggleMarkdownNotesButton` → NavigationBar::rightSidebarToggled
- `touchGesturesButton` → Toolbar::m_touchGestureButton
- `pdfTextSelectButton` → Review
- `saveButton` → NavigationBar::saveClicked
- `penToolButton`, `markerToolButton`, `eraserToolButton` → Toolbar
- `straightLineToggleButton`, `ropeToolButton` → Toolbar::m_shapeButton
- `insertPictureButton` → Review
- `fullscreenButton` → NavigationBar::fullscreenToggled

**Kept in controlBar (color buttons - for future PenSubToolbar):**
- `redButton`, `blueButton`, `yellowButton`, `greenButton`
- `blackButton`, `whiteButton`, `customColorButton`

**Kept in controlBar (needs review):**
- `toggleBookmarkButton`, `pageInput`, `deletePageButton`
- `overflowMenuButton` (redundant with NavigationBar?)
- `benchmarkButton`, `benchmarkLabel` (dev tools)

---

### MW3.5: Verification ✅ COMPLETED

- [x] `./compile.sh` succeeds
- [x] NavigationBar displays correctly
- [x] Toolbar tool selection works
- [x] TabBar displays and themes correctly
- [x] Tab switching works
- [x] Filename updates on tab switch

---

## Phase 4: Clean Up Remaining Code

**Goal:** Delete commented code, decide on remaining controlBar items.  
**Estimated savings:** ~300 lines (commented code deletion)  
**Risk:** Low

### MW4.1: Delete Commented Button Code

**Status:** ⬜ Not Started

The following were commented out in Phase 3 and can now be deleted:

**Button declarations in MainWindow.h:**
```cpp
// DELETE these declarations:
QPushButton *toggleTabBarButton;
QPushButton *toggleMarkdownNotesButton;
QPushButton *touchGesturesButton;
QPushButton *pdfTextSelectButton;
QPushButton *saveButton;
QPushButton *penToolButton;
QPushButton *markerToolButton;
QPushButton *eraserToolButton;
QPushButton *straightLineToggleButton;
QPushButton *ropeToolButton;
QPushButton *insertPictureButton;
QPushButton *fullscreenButton;
```

**Button creation/connection code in MainWindow.cpp:**
- Search for commented `// penToolButton = new QPushButton` blocks
- Search for commented `// controlLayout->addWidget(penToolButton)` lines
- Delete all commented-out button code

---

### MW4.2: Decide on Category 3 Items

**Status:** ⬜ Not Started

These items are still active in controlBar and need decisions:

| Item | Options | Decision |
|------|---------|----------|
| `toggleBookmarkButton` | Keep (bookmark toggle) or move to sidebar | ? |
| `pageInput` | Keep (page navigation spinbox) | Keep |
| `overflowMenuButton` | Remove (NavigationBar has menu) | Remove? |
| `deletePageButton` | Keep or move to page context menu | ? |
| `benchmarkButton/Label` | Remove (dev-only) or hide | ? |

---

### MW4.3: Remove Layout Functions

**Status:** ⬜ Not Started

**Delete these functions (no longer needed):**
- `createSingleRowLayout()` - mostly commented out
- `createTwoRowLayout()` - if exists
- Layout tracking variables

**Keep:**
- Basic controlBar setup (for color buttons)

---

### MW4.4: Clean Up updateTheme()

**Status:** ⬜ Not Started

**Current:** Still updates many individual widgets.

**Now simpler because:**
- NavigationBar has `updateTheme()`
- Toolbar has `updateTheme()`
- TabBar has `updateTheme()`

**Remaining in MainWindow::updateTheme():**
- Call component `updateTheme()` methods ✅ (already done)
- controlBar styling (for color buttons)
- Floating sidebar tabs styling
- Sidebar styling

---

### MW4.5: Final Verification

- [ ] `./compile.sh` succeeds
- [ ] All keyboard shortcuts work
- [ ] Tool switching works (via new Toolbar)
- [ ] Save/load works (via NavigationBar)
- [ ] Tab switching works (via TabBar)
- [ ] Theme switching works
- [ ] Color buttons still work

---

## Phase 5: Documentation Update

### MW5.1: Update MAINWINDOW_ANALYSIS.md

Mark sections as complete:
- Dead code: ✅ Deleted
- Dial system: ✅ Deleted entirely
- Toolbar: ✅ Extracted to NavigationBar, Toolbar, TabBar

---

### MW5.2: ~~Create source/input/README.md~~ ✅ N/A

**No longer needed** - dial system was deleted entirely instead of extracted.

---

### MW5.3: Update TOOLBAR_EXTRACTION_SUBPLAN.md ✅ COMPLETED

Full documentation of toolbar extraction in separate document.

---

## Future Work (Not This Subplan)

These items are identified but NOT part of this cleanup:

| Feature | Dependency | Status |
|---------|------------|--------|
| Subtoolbar system | After cleanup | Design phase |
| Unified Option Menu | After subtoolbar | Design phase |
| PDF Outline sidebar | PdfProvider integration | Blocked |
| Bookmarks sidebar | Document persistence | Blocked |
| Markdown Notes | InsertedLink object | Blocked |
| PDF Export rewrite | PdfProvider | Planned |
| Distraction-free mode | UI layout finalized | Planned |

---

## Summary Checklist

### Phase 1: Delete Dead Code ✅ COMPLETED
- [x] MW1.1: Delete #if 0 export blocks ✅ (1,314 lines deleted)
- [x] MW1.2: Delete background selection ✅
- [x] MW1.3: Delete prev/next page buttons ✅
- [x] MW1.4: Delete InkCanvas references ✅
- [x] MW1.5: Delete unused stubs ✅

### Phase 2: Delete Dial System ✅ COMPLETED
- [x] MW2.1-MW2.7: All dial code deleted ✅ (**-1,240 lines + 7 files**)

### Phase 3: Toolbar Extraction ✅ COMPLETED
- [x] MW3.1: Create NavigationBar ✅
- [x] MW3.2: Create Toolbar ✅
- [x] MW3.3: Create TabBar ✅
- [x] MW3.4-MW3.5: Verify components ✅

### Phase 4: Sidebar Extraction ✅ COMPLETED
- [x] Create LeftSidebarContainer ✅
- [x] Move LayerPanel to sidebars/ ✅
- [x] Remove floating toggle buttons ✅

### Phase 5: Comprehensive Cleanup ✅ COMPLETED
- [x] MW5.1: Delete controlBar ✅
- [x] MW5.2: Delete zoomSlider ✅
- [x] MW5.3: Delete PDF functions from MainWindow ✅
- [x] MW5.4: Delete old toggle panel functions ✅
- [x] MW5.5: Delete colorPreset code ✅
- [x] MW5.6: Delete old .spn format code ✅
- [x] MW5.7: Delete autoScroll code ✅
- [x] MW5.8: Rework pan sliders (keyboard-aware auto-hide) ✅

### Phase 7: Deep Cleanup ✅ COMPLETED
- [x] MW7.1-MW7.10: All deep cleanup tasks ✅ (**-2,930 lines**)

### Phase 6: Input Hub Extraction (Future - Optional)
- [ ] MW6.1: Create KeyboardShortcutHub
- [ ] MW6.2: Create MouseShortcutHub  
- [ ] MW6.3: Create ControllerShortcutHub

---

## Appendix: File Changes Summary

### Final Results (All Phases)

| File | Before | After | Change |
|------|--------|-------|--------|
| `source/MainWindow.cpp` | 9,722 | **2,970** | **-6,752 (69%)** |
| `source/MainWindow.h` | 887 | **381** | **-506 (57%)** |
| `CMakeLists.txt` | 518 | ~500 | -18 |

### Deleted Files (Phase 2)

| File | Reason |
|------|--------|
| `source/input/DialTypes.h` | Dial system deleted |
| `source/input/DialController.h/cpp` | Dial system deleted |
| `source/input/MouseDialHandler.h/cpp` | Dial system deleted |
| `source/ui/DialModeToolbar.h/cpp` | Dial system deleted |

### New Components Created (Phase 3-4)

| File | Purpose | Lines |
|------|---------|-------|
| `source/ui/NavigationBar.h/cpp` | Top bar UI | ~290 |
| `source/ui/Toolbar.h/cpp` | Tool selection | ~315 |
| `source/ui/TabBar.h/cpp` | Custom tab bar | ~130 |
| `source/ui/StyleLoader.h/cpp` | QSS loading | ~115 |
| `source/ui/ToolbarButtons.h/cpp` | Button types | ~330 |
| `source/ui/sidebars/LeftSidebarContainer.h/cpp` | Sidebar container | ~150 |
| `resources/styles/tabs.qss` | Tab styling | ~90 |
| `resources/styles/tabs_dark.qss` | Dark tab styling | ~90 |

### Kept (for future use)

| File | Status |
|------|--------|
| `source/SDLControllerManager.h/cpp` | Kept for gamepad support |
| `source/InkCanvas.h/cpp` | Legacy (separate removal task) |

### Future Work

| Task | Description |
|------|-------------|
| Input Hub Extraction | Create KeyboardShortcutHub, MouseShortcutHub, ControllerShortcutHub |
| Subtoolbar System | PenSubToolbar, EraserSubToolbar, etc. |
| InkCanvas Removal | Remove legacy InkCanvas from build |



## Phase 4.5: Sidebar Extraction

**See:** `SIDEBAR_EXTRACTION_SUBPLAN.md` for full implementation details.

**Summary:**
- Create `source/ui/sidebars/` folder
- Create `LeftSidebarContainer` (QTabWidget-based)
- Move `LayerPanel` into sidebars folder
- Remove floating toggle buttons (`toggleOutlineButton`, `toggleBookmarksButton`, `toggleLayerPanelButton`)
- Connect NavigationBar's left sidebar toggle to container

**Deferred:**
- OutlinePanel, BookmarksPanel connections (not implemented yet)
- PagePanel (doesn't exist yet)
- Panel visibility memory via QSettings 



## Next Steps (MainWindow Cleanup Complete!)

### ✅ Completed:
- MainWindow reduced from **9,722 to 2,970 lines (69% reduction)**
- Page switching connected to DocumentViewport
- Pan sliders reworked with keyboard-aware auto-hide
- All obsolete code deleted

### Future Options:
1. **Subtoolbar System** - Implement PenSubToolbar, EraserSubToolbar, etc.
2. **PDF Features** - PDF outline in LeftSidebarContainer, PDF text selection tool
3. **Input Hub Extraction** - Optional: move shortcuts to separate hub classes
4. **InsertedObject Types** - More object types for the canvas
5. **InkCanvas Removal** - Remove legacy InkCanvas.cpp from build

### Recommended Next:
The MainWindow is now clean and maintainable. The next logical step depends on priorities:
- **For UI polish:** Subtoolbar system
- **For PDF workflow:** PDF outline + text selection
- **For extensibility:** InsertedObject types 



### Answers: 

0. Option 3 it is. Page switching has a high priority. 
1. It does support paged documents and edgeless documents. Paged documents has 2 layouts, 1 column and 2 column, from here

```cpp
// ===== Layout =====

void DocumentViewport::setLayoutMode(LayoutMode mode)
{
    if (m_layoutMode == mode) {
        return;
    }
    
    // Before switching: get the page currently at viewport center
    int currentPage = m_currentPageIndex;
    qreal oldPageY = 0;
    if (m_document && !m_document->isEdgeless() && currentPage >= 0) {
        oldPageY = pagePosition(currentPage).y();
    }
    
    LayoutMode oldMode = m_layoutMode;
    m_layoutMode = mode;
    
    // Invalidate layout cache for new layout mode
    invalidatePageLayoutCache();
    
    // After switching: adjust vertical offset to keep same page visible
    if (m_document && !m_document->isEdgeless() && currentPage >= 0) {
        qreal newPageY = pagePosition(currentPage).y();
        
        // Adjust pan offset to compensate for page position change
        // Keep the same relative position within the viewport
        qreal yDelta = newPageY - oldPageY;
        m_panOffset.setY(m_panOffset.y() + yDelta);
        
        qDebug() << "Layout switch:" << (oldMode == LayoutMode::SingleColumn ? "1-col" : "2-col")
                 << "->" << (mode == LayoutMode::SingleColumn ? "1-col" : "2-col")
                 << "page" << currentPage << "yDelta" << yDelta;
    }
    
    // Update PDF cache capacity for new layout (Task 1.3.6)
    updatePdfCacheCapacity();
    
    // Recenter content horizontally for new layout width
    recenterHorizontally();
    
    // Recalculate layout and repaint
    clampPanOffset();
    update();
    emitScrollFractions();
}
```

you can see that how 1 column and 2 column layout switch back and forth. And this function
```cpp
// ===== Layout Engine (Task 1.3.2) =====

QPointF DocumentViewport::pagePosition(int pageIndex) const
{
    if (!m_document || pageIndex < 0 || pageIndex >= m_document->pageCount()) {
        return QPointF(0, 0);
    }
    
    // For edgeless documents, there's only one page at origin
    if (m_document->isEdgeless()) {
        return QPointF(0, 0);
    }
    
    // Ensure cache is valid - O(n) rebuild only when dirty
    ensurePageLayoutCache();
    
    // O(1) lookup from cache
    qreal y = (pageIndex < m_pageYCache.size()) ? m_pageYCache[pageIndex] : 0;
    
    switch (m_layoutMode) {
        case LayoutMode::SingleColumn:
            // X is always 0 for single column
            return QPointF(0, y);
        
        case LayoutMode::TwoColumn: {
            // Y comes from cache, just need to calculate X for right column
            int col = pageIndex % 2;
            qreal x = 0;
            
            if (col == 1) {
                // Right column - offset by left page width + gap
                int leftIdx = (pageIndex / 2) * 2;
                const Page* leftPage = m_document->page(leftIdx);
                if (leftPage) {
                    x = leftPage->size.width() + m_pageGap;
                }
            }
            
            return QPointF(x, y);
        }
    }
    
    return QPointF(0, 0);
}
```
is supposed to find the position of a page. So page switching should be VERY straightfoward. But you need to read the documents in phase 1.3 and think twice before we make the plan. 


2. The Page Switching UI (apart from touch gestures and keyboard shortcuts) only exists in page panel, and I haven't decided on the style yet. But visual changes should be easy after we sort all the logic out. 

3. They are equally as important. 

### Extra details

- There should be one and only one switch page function in mainwindow. All other UI components in mainwindow should only be accessing this. 

- None of the old page related logic works. InkCanvas is permanently dead. 


### More Answers

A6: Apart from what the user can SEE, everything else should stay 0-based for consistency, including the values stored in mainwindow. I don't know if this makes sense. What do you think? 
A7: We don't need this any more. This was built for magicdial, which was removed. 
A8: I don't even think goToNextPage needs to exist now... It only served a few shortcut buttons that we already removed...

---

## Phase S4: Page Switching Implementation ✅ COMPLETED

**Date:** January 5, 2026

### What Was Done

Implemented page switching with ~25 lines of code:

1. **`switchPage(int pageIndex)`** - Main function (0-based)
   - Calls `vp->scrollToPage(pageIndex)`
   - pageInput update handled by signal

2. **`switchPageWithDirection(int pageIndex, int direction)`**
   - Now just calls `switchPage()` (direction was for magicdial)

3. **`goToPreviousPage()` / `goToNextPage()`**
   - Thin wrappers: `switchPage(currentPageIndex ± 1)`

4. **`onPageInputChanged(int newPage)`**
   - Simplified: `switchPage(newPage - 1)` (convert 1-based to 0-based)

5. **Connected `currentPageChanged` signal to update `pageInput`**
   - pageInput now auto-updates when page changes (scroll, touch, keyboard, etc.)

### Code Changes

```cpp
void MainWindow::switchPage(int pageIndex) {
    DocumentViewport* vp = currentViewport();
    if (!vp) return;
    vp->scrollToPage(pageIndex);  // 0-based
}

void MainWindow::goToNextPage() {
    if (auto* vp = currentViewport())
        switchPage(vp->currentPageIndex() + 1);
}

void MainWindow::goToPreviousPage() {
    if (auto* vp = currentViewport())
        switchPage(vp->currentPageIndex() - 1);
}
```

### Verification

- [x] Build successful
- [ ] Manual test: pageInput spinbox changes page
- [ ] Manual test: Mouse back/forward buttons work
- [ ] Manual test: Gamepad controller navigation works
- [ ] Manual test: Page scrolling updates pageInput 


## Answers for further cleanup

1. The issues is on the pan X slider. When the tab bar folds (by clicking the title on navigation bar), everything is fine. But when tab bar appears, (upon launching the application or adjusting any other layouts), the pan X slider is about 1 tab bar height lower than where it's supposed to be. Folding and unfolding the tab bar fixes this problem, but if I change the layout again (by resizing the window or opening a side bar), the pan X slider misposition happens again. 

2. We need a new folder `controls` in the `source` folder, and it should contain a keyboardshortcuthub, a mouseshortcuthub, and a controllershortcuthub? After we recover the keyboard shortcut settings from the control panel `ControlPanelDialog.cpp`, we can change the hard coded defaults to editable ones. 

3. Fix (1) first, because it's obvious, then 


(2) delete the functions that don't make sense any more, like `onZoomSliderChanged` (because the zoom slider doesn't exist any more), 

(3) any interactions with PDF (like `getPdfDocument`, or anything related to the PDF outline. I find it insane that the PDF loads the PDF outline itself before). 

(4) Then all toggle functions for old panels (since we already replaced them with sidebar containers, except for the markdown container on the right). 

(5) Then bookmark related functions. 

(6) Then everything about color presets (we don't have hard coded color presets any more.)

(7) Everything related to the old spn format packages. New packaging TBD (`.snb` is temporarily a loose folder, and we will finalize the packaging after we finalize everything about persistence)

(8) Other useless logic, like `onAutoScrollRequested`.

## Questions: 

1. Does `setupSingleInstanceServer()` sound like a good and efficient idea to stay single instance? This needs Qt network module.

2. Is my plan above (in Answer 3) reasonable? 


## More answers about further cleanup

1. From what I can see, MainWindow line 5063 `updateScrollbarPositions()`. There may be more. 

2. Sorry for making this mistake. What I meant was "'MainWindow loads the PDF outline' was ridiculous". 
Now it's (supposed to be) handled by DocumentViewport
```cpp
QVector<PdfOutlineItem> Document::pdfOutline() const
{
    if (!isPdfLoaded()) {
        return QVector<PdfOutlineItem>();
    }
    return m_pdfProvider->outline();
}
```
And it fetches PDF Outline from the pdf provider abstraction layer, and then it fetches the actual data from one of the PDF providers (for now there is only Poppler). 

3. Previously, it was both (since the PDF pages HAD to be tied to pages 1 to 1). But now, it is NEITHER, and how bookmarks should work now is TBD. 
The page index and the PDF page index ARE SEPARATE AND CAN BE DIFFERENT, because it allows empty pages to be inserted between 2 PDF pages. PDF outline should follow PDF page index obviously, but bookmarks... we may need to record both. 

4. The `controlBar` IS the old tool bar, and it needs to be removed. THE NEW UI NO LONGER NEEDS THE `controlBar` ANY MORE. It used to hold the old buttons. 

5. Making the 3 hubs independent makes the most sense. 

6. Since the pan slider positioning is also a temporary hack after we connected the new DocumentViewport, the way they arer positioned need to be modified completely. 

(1). When no keyboard is detected - HIDE COMPLETELY (I don't know if this is possible)
(2). When a keyboard is detected - Position it like it does now, right below the tool bar (not tab bar) (for the pan X slider), and to the right of the left side container (for the pan Y slider). They should float on the top side and the left side of the DocumentViewport. 

7. Delete all controlBar related code. 

8. You already answered this question (but you forgot). `setupSingleInstanceServer()` is supposedly good and we don't need to worry about that now. 

9. You can make a detailed plan on this cleanup process, and we can follow the plan later. I'm going to ask you for help and do some work manually myself.

---

## Phase 5: Comprehensive MainWindow Cleanup

**Goal:** Remove obsolete code, rework pan sliders, and prepare for input hub extraction.  
**Estimated savings:** ~1,000-1,500 lines  
**Current MainWindow.cpp:** 6,396 lines  
**Target:** ~5,000 lines

### Scope Summary (from grep analysis)

| Category | Matches | Action |
|----------|---------|--------|
| controlBar | 14 | DELETE |
| zoomSlider | 21 | DELETE |
| getPdfDocument/pdfOutline | 6 | DELETE |
| colorPreset | 21 | DELETE |
| .spn format | 8 | DELETE |
| autoScroll | 2 | DELETE |
| toggleOutline/Bookmark/Layer (old) | 30 | DELETE |
| panXSlider/panYSlider | 47 | REWORK |
| bookmark | 130 | DEFER (TBD) |
| keyboard events | 18 | EXTRACT (later) |
| SDL/controller/gamepad | 82 | EXTRACT (later) |

---

### MW5.1: Delete controlBar

**Priority:** HIGH  
**Matches:** 14  
**Risk:** Low

The `controlBar` was the old toolbar container. It's been replaced by NavigationBar and Toolbar.

**Tasks:**
- [ ] Find controlBar declaration in MainWindow.h
- [ ] Delete controlBar creation code
- [ ] Delete controlBar layout code
- [ ] Delete controlBar styling code
- [ ] Delete controlBar show/hide logic
- [ ] Remove controlBar member variable
- [ ] Verify build

**Search pattern:** `controlBar`

---

### MW5.2: Delete zoomSlider

**Priority:** HIGH  
**Matches:** 21  
**Risk:** Low

The zoom slider was in the old toolbar. Zoom is now controlled differently.

**Tasks:**
- [ ] Find zoomSlider declaration in MainWindow.h
- [ ] Delete zoomSlider creation code
- [ ] Delete `onZoomSliderChanged()` function
- [ ] Delete zoomSlider signal connections
- [ ] Delete zoomSlider styling
- [ ] Remove zoomSlider member variable
- [ ] Verify build

**Search pattern:** `zoomSlider|onZoomSliderChanged`

---

### MW5.3: Delete PDF-related functions from MainWindow

**Priority:** HIGH  
**Matches:** 6  
**Risk:** Low

PDF outline and document access is now handled by Document/DocumentViewport.

**Tasks:**
- [ ] Delete `getPdfDocument()` if it exists
- [ ] Delete any PDF outline loading code in MainWindow
- [ ] Ensure Document::pdfOutline() is the only path
- [ ] Verify build

**Search pattern:** `getPdfDocument|pdfOutline|PdfOutline`

---

### MW5.4: Delete old toggle panel functions

**Priority:** HIGH  
**Matches:** 30  
**Risk:** Low

Old floating sidebar buttons have been replaced by LeftSidebarContainer.

**Tasks:**
- [ ] Delete `toggleOutlinePanel()` function
- [ ] Delete `toggleBookmarksPanel()` function (if separate from bookmark data)
- [ ] Delete `toggleLayerPanel()` function (now handled by sidebar container)
- [ ] Delete any positioning code for old floating buttons
- [ ] Verify build

**Search pattern:** `toggleOutline|toggleBookmark|toggleLayer`

---

### MW5.5: Delete colorPreset code

**Priority:** MEDIUM  
**Matches:** 21  
**Risk:** Low

Hard-coded color presets are no longer used. Colors are now handled differently.

**Tasks:**
- [ ] Find colorPreset declarations
- [ ] Delete colorPreset array/list
- [ ] Delete colorPreset loading/saving
- [ ] Delete colorPreset button creation
- [ ] Delete colorPreset UI logic
- [ ] Verify build

**Search pattern:** `colorPreset|ColorPreset`

---

### MW5.6: Delete old .spn format code

**Priority:** MEDIUM  
**Matches:** 8  
**Risk:** Low

Old spn format is deprecated. New packaging (.snb as loose folder) is TBD.

**Tasks:**
- [ ] Find .spn format references
- [ ] Delete old packaging/unpackaging code
- [ ] Keep any code that might be useful for migration (comment clearly)
- [ ] Verify build

**Search pattern:** `\.spn|spnFormat`

---

### MW5.7: Delete autoScroll code

**Priority:** LOW  
**Matches:** 2  
**Risk:** Low

Auto-scroll was a legacy feature.

**Tasks:**
- [ ] Delete `onAutoScrollRequested()` function
- [ ] Delete any autoScroll signal connections
- [ ] Verify build

**Search pattern:** `onAutoScrollRequested|autoScroll`

---

### MW5.8: Rework Pan Sliders ✅ COMPLETED

**Priority:** HIGH  
**Status:** ✅ Complete

Pan sliders completely reworked with keyboard-aware auto-hide.

**Implementation:**
1. **Keyboard detection:** Uses `QInputDevice::devices()` (Qt 6.4+) to detect physical keyboards
2. **Auto-hide:** 3-second timer hides sliders after inactivity
3. **Show on activity:** Scrollbars appear when viewport scrolls or user hovers
4. **Positioning:**
   - Pan X: Top of viewport area (below Toolbar in layout)
   - Pan Y: Left side (after LeftSidebarContainer if visible)

**New Functions Added:**
- `hasPhysicalKeyboard()` - Detect keyboard via QInputDevice
- `showScrollbars()` - Show and reset hide timer
- `hideScrollbars()` - Hide sliders

**Tasks Completed:**
- [x] Add keyboard detection logic
- [x] Modify `updateScrollbarPositions()` to use proper references
- [x] Add hide/show logic based on keyboard presence
- [x] Connect scroll signals to show scrollbars
- [x] Auto-hide timer (3 seconds)
- [x] Verify build

---

### MW5.9: Bookmark Code (DEFERRED)

**Priority:** DEFERRED  
**Matches:** 130  
**Risk:** HIGH

**Reason for deferral:** Bookmarks need architectural decisions first.
- Page index ≠ PDF page index now (empty pages can be inserted)
- PDF outline → follows PDF page index
- Bookmarks → need to record BOTH page index and PDF page index

**Decision needed:** How should bookmarks work with the new page model?

---

## Phase 6: Input Hub Extraction (Future)

**Goal:** Extract keyboard, mouse, and controller input handling into separate hub classes.  
**Estimated savings:** ~500-800 lines  
**Dependencies:** Phase 5 should be mostly complete

### Folder Structure

```
source/
└── controls/
    ├── KeyboardShortcutHub.h
    ├── KeyboardShortcutHub.cpp
    ├── MouseShortcutHub.h
    ├── MouseShortcutHub.cpp
    ├── ControllerShortcutHub.h
    └── ControllerShortcutHub.cpp
```

### MW6.1: Create KeyboardShortcutHub

**Matches in MainWindow:** 18 (keyPressEvent, keyReleaseEvent, QKeySequence)

**Responsibilities:**
- Handle all keyboard shortcuts
- Load/save shortcut configurations from QSettings
- Emit signals for actions (e.g., `undoRequested()`, `redoRequested()`)
- Support customizable shortcuts (from ControlPanelDialog)

**Tasks:**
- [ ] Create `source/controls/` folder
- [ ] Create KeyboardShortcutHub class
- [ ] Move keyPressEvent logic to hub
- [ ] Move keyReleaseEvent logic to hub
- [ ] Define shortcut configuration format
- [ ] Connect hub signals to MainWindow slots
- [ ] Update CMakeLists.txt
- [ ] Verify build

---

### MW6.2: Create MouseShortcutHub

**Responsibilities:**
- Handle mouse button mappings (e.g., side buttons for page navigation)
- Support customizable mouse actions
- Emit signals for actions

**Tasks:**
- [ ] Create MouseShortcutHub class
- [ ] Move mouse button handling logic
- [ ] Define mouse action configuration format
- [ ] Connect hub signals to MainWindow slots
- [ ] Update CMakeLists.txt
- [ ] Verify build

---

### MW6.3: Create ControllerShortcutHub

**Matches in MainWindow:** 82 (SDL, controller, gamepad)

**Responsibilities:**
- Handle SDL controller/gamepad input
- Map controller buttons to actions
- Support customizable controller mappings
- Emit signals for actions

**Tasks:**
- [ ] Create ControllerShortcutHub class
- [ ] Move SDL controller initialization
- [ ] Move controller event handling
- [ ] Define controller mapping configuration format
- [ ] Connect hub signals to MainWindow slots
- [ ] Update CMakeLists.txt
- [ ] Verify build

---

## Execution Order

**Recommended order for Phase 5:**

1. **MW5.1** - Delete controlBar (foundational, unblocks other deletions)
2. **MW5.2** - Delete zoomSlider (related to old toolbar)
3. **MW5.4** - Delete old toggle panel functions (already replaced)
4. **MW5.3** - Delete PDF functions from MainWindow
5. **MW5.5** - Delete colorPreset code
6. **MW5.6** - Delete old .spn format code
7. **MW5.7** - Delete autoScroll code
8. **MW5.8** - Rework pan sliders (do last, as it involves rework not just deletion)

**MW5.9 (bookmarks):** Defer until architectural decision made.

**Phase 6:** Start after Phase 5 is complete and tested.

---

## Notes

- Each task should be followed by a build verification
- Manual testing is recommended after major deletions
- Some code may have unexpected dependencies - proceed carefully
- Keep backup of MainWindow.cpp before starting (git commit)

---

## Phase 7: Deep Cleanup ✅ COMPLETED

**Status:** ✅ All tasks completed (manually + automated)  
**Result:** MainWindow reduced from ~5,900 to ~2,970 lines  
**Savings:** ~2,930 lines (50% reduction in this phase alone!)

---

### MW7.1: Clean Up "REMOVED" Comments ✅ COMPLETED

Verbose comments cleaned from MainWindow.h.

---

### MW7.2: Delete ALL Dial Functionality ✅ COMPLETED

All dial-related code deleted:
- `dialDisplay`, `dialColorPreview`, `dialIconView` - deleted
- `toggleDial()`, `positionDialContainer()`, `initializeDialSound()`, `updateDialDisplay()` - deleted
- All calls to `updateDialDisplay()` - deleted
- Dial styling and signal connections - deleted

---

### MW7.3: Delete Old PDF Functions ✅ COMPLETED

Old PDF workflow functions deleted. **Kept:** `openPdfDocument()` (Ctrl+Shift+O)

---

### MW7.4: Delete Bookmark Implementation ✅ COMPLETED

Bookmark sidebar and related functions deleted. Will be reimplemented later with proper page index handling.

---

### MW7.5: Delete Old Outline Sidebar ✅ COMPLETED

Outline sidebar deleted. Will be reimplemented in LeftSidebarContainer.

---

### MW7.6: Delete Old Keyboard/Controller Mapping System ✅ COMPLETED

Old mapping system deleted. **Kept:**
- `SDLControllerManager` - for future gamepad support
- `keyPressEvent()` override - cleaned up
- New `QShortcut`-based shortcuts (Phase doc-1)

---

### MW7.7: Delete Stub Functions ✅ COMPLETED

Obsolete stub functions deleted:
- `switchPageWithDirection()` - replaced with direct `switchPage()` calls
- Stylus button mode functions - removed

---

### MW7.8: Delete overflowMenuButton ✅ COMPLETED

`overflowMenuButton` deleted. **Kept:** `overflowMenu` (connected to NavigationBar's menu button)

---

### MW7.9: Clean Up InkCanvas References ✅ COMPLETED

InkCanvas references cleaned from MainWindow.h. InkCanvas.cpp still in build (separate removal task).

---

### MW7.10: Delete Remaining Obsolete UI Elements ✅ COMPLETED

Obsolete UI declarations cleaned up.

---

### Summary Checklist

| Task | Description | Status |
|------|-------------|--------|
| MW7.1 | Delete "REMOVED" comments | ✅ |
| MW7.2 | Delete ALL dial code | ✅ |
| MW7.3 | Delete old PDF functions | ✅ |
| MW7.4 | Delete bookmark implementation | ✅ |
| MW7.5 | Delete outline sidebar | ✅ |
| MW7.6 | Delete old mapping system | ✅ |
| MW7.7 | Delete stub functions | ✅ |
| MW7.8 | Delete overflowMenuButton | ✅ |
| MW7.9 | Clean InkCanvas refs | ✅ |
| MW7.10 | Delete remaining obsolete UI | ✅ |

---

---

## Appendix: Code to KEEP (Reference)

> **Note:** This section documents code that was intentionally preserved during cleanup.

### Keep: New QShortcut-based Shortcuts (Phase doc-1)

These use the new pattern and should NOT be deleted:

```cpp
// Phase doc-1: Application-wide keyboard shortcuts
// Using QShortcut with ApplicationShortcut context
QShortcut* saveShortcut = new QShortcut(QKeySequence::Save, this);
QShortcut* loadShortcut = new QShortcut(QKeySequence::Open, this);
QShortcut* addPageShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A), this);
QShortcut* insertPageShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I), this);
QShortcut* deletePageShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D), this);
QShortcut* newEdgelessShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N), this);
QShortcut* loadBundleShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L), this);
QShortcut* openPdfShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O), this);
QShortcut* debugOverlayShortcut = new QShortcut(QKeySequence(Qt::Key_F12), this);
QShortcut* autoLayoutShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_2), this);
```

### Keep: SDLControllerManager

The controller manager itself is needed - only the mapping system is deleted:

```cpp
SDLControllerManager *controllerManager = nullptr;
QThread *controllerThread = nullptr;
```

### Keep: keyPressEvent() Override

The override stays, but old code inside may need cleanup:

```cpp
void keyPressEvent(QKeyEvent *event) override;
```

### Keep: openPdfDocument()

New PDF opening function (Ctrl+Shift+O):

```cpp
void openPdfDocument();
```