# MainWindow Cleanup Subplan

**Document Version:** 1.1  
**Date:** January 3, 2026  
**Status:** Phase 1 & 2 Complete, Phase 3 Ready  
**Prerequisites:** Q&A completed in `MAINWINDOW_CLEANUP_QA.md`

---

## Overview

MainWindow.cpp (~9,700 lines) is the last major piece of legacy code in SpeedyNote. This subplan details how to clean it up systematically, making it maintainable and ready for new features.

### Goals
1. ✅ Delete dead code (~2,800 lines saved!)
2. ✅ Delete dial system entirely (simpler than extraction)
3. Simplify toolbar (prepare for subtoolbar system)
4. Clean up remaining code structure

### Results So Far
| Metric | Before | Current | Target |
|--------|--------|---------|--------|
| MainWindow.cpp lines | 9,722 | 6,910 | ~5,500 |
| MainWindow.h lines | 887 | 812 | ~700 |
| Dead code blocks | ~1,500 | 0 | 0 |
| Dial system | Complex 7-mode | Deleted | N/A |

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

**Line count results:**
- MainWindow.cpp: 8,186 → 6,910 (**-1,276 lines total in Phase 2**)
- MainWindow.h: 887 → 812 (**-75 lines**)
- CMakeLists.txt: 518 → 497 (**-21 lines**)
- Deleted files: 7 files in source/input/ and source/ui/

---

## Phase 3: Simplify Toolbar

**Goal:** Remove 2-row responsive layout, prepare for subtoolbar system.  
**Estimated savings:** ~400 lines  
**Risk:** Medium (visual layout changes)

### MW3.1: Delete 2-Row Responsive Layout Code

**Files:** `source/MainWindow.cpp`

**Delete:**
- `createSingleRowLayout()` method
- `createTwoRowLayout()` method (if exists)
- `adjustToolbarLayout()` resize handling
- All row/column tracking variables

**Replace with:**
```cpp
void MainWindow::setupToolbar() {
    // Simple single-row layout
    m_toolbarLayout = new QHBoxLayout(m_toolbar);
    m_toolbarLayout->setSpacing(4);
    m_toolbarLayout->setContentsMargins(8, 4, 8, 4);
    
    // Add only essential buttons
    m_toolbarLayout->addWidget(m_penButton);
    m_toolbarLayout->addWidget(m_markerButton);
    m_toolbarLayout->addWidget(m_eraserButton);
    m_toolbarLayout->addWidget(m_lassoButton);
    m_toolbarLayout->addWidget(m_objectSelectButton);
    m_toolbarLayout->addStretch();
    m_toolbarLayout->addWidget(m_undoButton);
    m_toolbarLayout->addWidget(m_redoButton);
    m_toolbarLayout->addWidget(m_fullscreenButton);
}
```

---

### MW3.2: Remove Button State Tracking Complexity

**Current:** Complex tracking of which buttons are visible, which row they're on.

**Delete:**
- `m_toolbarRowStates`
- `m_buttonVisibility` maps
- `updateToolbarButtonStates()` method

**Simplify to:**
- Buttons are either in toolbar or not
- No dynamic repositioning

---

### MW3.3: Remove Obsolete Toolbar Buttons

**Delete widgets that are moving to subtoolbars:**
- Color buttons (except maybe one "current color" indicator)
- Thickness slider from toolbar
- Zoom slider from toolbar

**Keep as stubs for subtoolbar implementation:**
- Color selection logic (just not in toolbar)
- Thickness logic
- Zoom logic

---

### MW3.4: Stub Subtoolbar Infrastructure

**Create placeholder for future:**
```cpp
// source/ui/SubToolbar.h (stub)
class SubToolbar : public QWidget {
    Q_OBJECT
public:
    enum Type { Pen, Marker, Eraser, Lasso, ObjectSelect };
    explicit SubToolbar(Type type, QWidget *parent = nullptr);
    void showAt(const QPoint &position);
    void hide();
};
```

This is just a placeholder - full implementation is a future task.

---

### MW3.5: Verification

- [ ] `./compile.sh` succeeds
- [ ] Toolbar displays correctly (single row)
- [ ] Tool buttons work
- [ ] No layout errors on resize

---

## Phase 4: Clean Up Remaining Code

**Goal:** Improve code quality and structure.  
**Estimated savings:** ~200 lines (mostly simplification)  
**Risk:** Low

### MW4.1: Simplify Save Button Logic

**Current:** Complex conditional logic for different save scenarios.

**Replace with:**
```cpp
void MainWindow::onSaveButtonClicked() {
    saveDocument();  // Same as Ctrl+S
}
```

**Delete:**
- Old save path detection logic
- Canvas-based save logic
- Temporary file handling for old save

---

### MW4.2: Clean Up updateTheme()

**Current:** 300+ line function updating 50+ widgets individually.

**Improve to:**
- Use stylesheets more effectively
- Group widgets by type
- Consider theme signal subscriptions (future)

**Example cleanup:**
```cpp
void MainWindow::updateTheme() {
    // Get theme colors once
    QColor bg = m_darkMode ? QColor(30, 30, 30) : QColor(245, 245, 245);
    QColor fg = m_darkMode ? QColor(255, 255, 255) : QColor(0, 0, 0);
    
    // Apply base stylesheet to all buttons at once
    QString buttonStyle = QString(
        "QPushButton { background: %1; color: %2; }"
    ).arg(bg.name(), fg.name());
    
    for (QPushButton *btn : m_toolButtons) {
        btn->setStyleSheet(buttonStyle);
    }
    
    // ... grouped updates instead of 50 individual calls
}
```

---

### MW4.3: Remove Commented-Out Code

Search and remove:
```cpp
// Old code that was commented out
/* Unused blocks */
// TODO: delete this
```

If code is valuable for reference, note it in documentation instead.

---

### MW4.4: Organize Method Order

Reorder methods by category:
1. Constructor / Destructor
2. Setup methods (setupUi, setupToolbar, setupSignals)
3. Tool handling
4. Document operations
5. UI state (theme, layout)
6. Event handlers

---

### MW4.5: Final Verification

- [ ] `./compile.sh` succeeds
- [ ] All keyboard shortcuts work
- [ ] Tool switching works
- [ ] Save/load works
- [ ] Tab switching works
- [ ] Theme switching works
- [ ] No memory leaks (basic test)

---

## Phase 5: Documentation Update

### MW5.1: Update MAINWINDOW_ANALYSIS.md

Mark sections as complete:
- Dead code: ✅ Deleted
- Dial system: ✅ Extracted to module
- Toolbar: ✅ Simplified

---

### MW5.2: ~~Create source/input/README.md~~ ✅ N/A

**No longer needed** - dial system was deleted entirely instead of extracted.

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

### Phase 3: Simplify Toolbar
- [ ] MW3.1: Delete 2-row layout
- [ ] MW3.2: Remove state tracking
- [ ] MW3.3: Remove obsolete buttons
- [ ] MW3.4: Stub subtoolbar
- [ ] MW3.5: Verify layout

### Phase 4: Clean Up
- [ ] MW4.1: Simplify save logic
- [ ] MW4.2: Clean updateTheme()
- [ ] MW4.3: Remove comments
- [ ] MW4.4: Organize methods
- [ ] MW4.5: Final verification

### Phase 5: Documentation
- [ ] MW5.1: Update analysis doc
- [x] MW5.2: ~~Create input module README~~ (N/A - dial system deleted)

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

### Kept (for future use)

| File | Status |
|------|--------|
| `source/SDLControllerManager.cpp` | Kept for gamepad support |
| `source/SDLControllerManager.h` | Kept for gamepad support |

### Future Work (Phase 3+)

| File | Action |
|------|--------|
| `source/ui/SubToolbar.h` | NEW (stub for subtoolbar system) |

