# MainWindow Cleanup Subplan

**Document Version:** 1.2  
**Date:** January 5, 2026  
**Status:** Phase 1, 2, & 3 Complete, Phase 4 Ready  
**Prerequisites:** Q&A completed in `MAINWINDOW_CLEANUP_QA.md`  
**Related:** `TOOLBAR_EXTRACTION_SUBPLAN.md` (Phase 3 details)

---

## Overview

MainWindow.cpp (~9,700 lines) is the last major piece of legacy code in SpeedyNote. This subplan details how to clean it up systematically, making it maintainable and ready for new features.

### Goals
1. ✅ Delete dead code (~2,800 lines saved!)
2. ✅ Delete dial system entirely (simpler than extraction)
3. ✅ Extract toolbar components (NavigationBar, Toolbar, TabBar)
4. Clean up remaining code structure

### Results So Far
| Metric | Before | Current | Target |
|--------|--------|---------|--------|
| MainWindow.cpp lines | 9,722 | 6,633 | ~5,500 |
| MainWindow.h lines | 887 | 836 | ~700 |
| Dead code blocks | ~1,500 | 0 | 0 |
| Dial system | Complex 7-mode | Deleted | N/A |
| Toolbar | Monolithic | Extracted | ✅ |

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

### Phase 1: Delete Dead Code
- [x] MW1.1: Delete #if 0 export blocks ✅ (1,314 lines deleted - 9,722 → 8,408)
- [x] MW1.2: Delete background selection ✅ (13 lines deleted - 8,408 → 8,395)
- [x] MW1.3: Delete prev/next page buttons ✅ (16 lines deleted - 8,395 → 8,379)
- [x] MW1.4: Delete InkCanvas references ✅ (19 lines deleted - 8,379 → 8,360)
- [x] MW1.5: Delete unused stubs ✅ (174 lines deleted - 8,360 → 8,186)
- [x] Compile and test ✅

### Phase 2: Delete Dial System ✅ COMPLETED
- [x] MW2.1: Create directory structure ✅ (then deleted)
- [x] MW2.2: Delete dial mode system from MainWindow ✅ (**-1,190 lines**)
- [x] MW2.3: Delete button hold mappings ✅ (**-50 lines**)
- [x] MW2.4: Delete skeleton files ✅ (7 files deleted)
- [x] MW2.5: Update CMakeLists.txt ✅ (**-21 lines**)
- [x] MW2.6: SDLControllerManager kept for future gamepad support
- [x] MW2.7: Verification ✅ (compiles successfully)

### Phase 3: Toolbar Extraction ✅ COMPLETED
- [x] MW3.1: Create NavigationBar ✅
- [x] MW3.2: Create Toolbar ✅
- [x] MW3.3: Create TabBar ✅
- [x] MW3.4: Comment out old buttons ✅
- [x] MW3.5: Verify components ✅

### Phase 4: Clean Up Remaining Code
- [ ] MW4.1: Delete commented button code
- [ ] MW4.2: Decide on Category 3 items
- [ ] MW4.3: Remove layout functions
- [ ] MW4.4: Clean updateTheme()
- [ ] MW4.5: Final verification

### Phase 5: Documentation
- [ ] MW5.1: Update analysis doc
- [x] MW5.2: ~~Create input module README~~ (N/A - dial system deleted)
- [x] MW5.3: Update TOOLBAR_EXTRACTION_SUBPLAN.md ✅

---

## Appendix: File Changes Summary

### Phase 1 & 2 Completed Changes

| File | Action | Lines Changed |
|------|--------|---------------|
| `source/MainWindow.cpp` | Major cleanup | 9,722 → 6,910 (**-2,812 lines**) |
| `source/MainWindow.h` | Remove unused declarations | 887 → 812 (**-75 lines**) |
| `CMakeLists.txt` | Remove dial options | 518 → 497 (**-21 lines**) |
| `source/input/DialTypes.h` | DELETED | - |
| `source/input/DialController.h` | DELETED | - |
| `source/input/DialController.cpp` | DELETED | - |
| `source/input/MouseDialHandler.h` | DELETED | - |
| `source/input/MouseDialHandler.cpp` | DELETED | - |
| `source/ui/DialModeToolbar.h` | DELETED | - |
| `source/ui/DialModeToolbar.cpp` | DELETED | - |

### Phase 3 Completed Changes

| File | Action | Lines |
|------|--------|-------|
| `source/MainWindow.cpp` | Toolbar extraction + cleanup | 6,910 → 6,633 (**-277 lines**) |
| `source/MainWindow.h` | Add new component members | 812 → 836 (+24 lines) |
| `source/ui/NavigationBar.h` | NEW | 85 lines |
| `source/ui/NavigationBar.cpp` | NEW | 202 lines |
| `source/ui/Toolbar.h` | NEW | 97 lines |
| `source/ui/Toolbar.cpp` | NEW | 218 lines |
| `source/ui/TabBar.h` | NEW | 60 lines |
| `source/ui/TabBar.cpp` | NEW | 70 lines |
| `source/ui/StyleLoader.h` | NEW | 58 lines |
| `source/ui/StyleLoader.cpp` | NEW | 56 lines |
| `source/ui/ToolbarButtons.h` | NEW | 150 lines |
| `source/ui/ToolbarButtons.cpp` | NEW | 180 lines |
| `resources/styles/tabs.qss` | NEW | 88 lines |
| `resources/styles/tabs_dark.qss` | NEW | 88 lines |
| `resources/styles/buttons.qss` | NEW | ~50 lines |
| `resources/styles/buttons_dark.qss` | NEW | ~50 lines |

### Kept (for future use)

| File | Status |
|------|--------|
| `source/SDLControllerManager.cpp` | Kept for gamepad support |
| `source/SDLControllerManager.h` | Kept for gamepad support |

### Future Work (Phase 4+)

| File | Action |
|------|--------|
| `source/ui/SubToolbar.h` | Future: subtoolbar system |
| `source/ui/PenSubToolbar.h` | Future: pen color/size selection |



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



## Next Step