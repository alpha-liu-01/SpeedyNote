# MainWindow Cleanup Subplan

**Document Version:** 1.0  
**Date:** January 3, 2026  
**Status:** Ready for Implementation  
**Prerequisites:** Q&A completed in `MAINWINDOW_CLEANUP_QA.md`

---

## Overview

MainWindow.cpp (~9,700 lines) is the last major piece of legacy code in SpeedyNote. This subplan details how to clean it up systematically, making it maintainable and ready for new features.

### Goals
1. Delete dead code (~1,500+ lines saved)
2. Extract dial-related code to modular `DialController` (with Android compile exclusion)
3. Simplify toolbar (prepare for subtoolbar system)
4. Clean up remaining code structure

### Expected Results
| Metric | Before | After |
|--------|--------|-------|
| MainWindow.cpp lines | ~9,700 | ~5,500 |
| Dead code blocks | ~1,500 | 0 |
| Conditional compilation | None | Android dial exclusion |
| Module structure | Monolithic | Modular dial system |

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

## Phase 2: Simplify Dial System (Revised Q3 Approach)

**Goal:** Remove complex dial logic, keep only essential display widget.  
**Estimated savings:** ~800 lines DELETED from MainWindow (not just moved)  
**Risk:** Low (removing unused complexity)

### Rationale (from Q&A discussion):
- The QDial widget and 7-mode system is overengineered
- Most functionality works fine via mouse/touch/keyboard
- The dial display is useful feedback UI - worth keeping
- Surface Dial hardware users are a tiny minority
- Fresh minimal implementation beats migrating messy code

### MW2.1: Create Directory Structure ✅ COMPLETED

**Created skeleton files** (will be simplified/removed based on new approach):
```
source/input/
├── DialTypes.h           (keep - shared enum, simplify to fewer modes)
├── DialController.h      (simplify - minimal input handler only)
├── DialController.cpp
├── MouseDialHandler.h    (DELETE - not needed in simplified approach)
├── MouseDialHandler.cpp  (DELETE)

source/ui/
├── DialModeToolbar.h     (DELETE - mode switching removed)
├── DialModeToolbar.cpp   (DELETE)
├── DialDisplay.h         (NEW - keep the display widget)
├── DialDisplay.cpp       (NEW)
```

---

### MW2.2: Delete Dial Mode System from MainWindow ✅ COMPLETED

**DELETED from MainWindow.cpp (~1,190 lines!):**

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

**KEPT as stubs (for moc compatibility):**
- `toggleDial()`, `positionDialContainer()`, `initializeDialSound()` - empty stubs
- `updateDialButtonState()`, `updateFastForwardButtonState()` - empty stubs
- `wheelEvent()` - forwards to base class

**KEPT:**
- `dialDisplay` widget (QLabel) - still used for status display
- `updateDialDisplay()` - simplified to just show page info

**Line count:**
- MainWindow.cpp: 8,186 → 6,996 (**-1,190 lines!**)
- MainWindow.h: 887 → 822 (**-65 lines**)

---

### MW2.3: Create Simple DialDisplay Widget

**New `source/ui/DialDisplay.h/cpp`:**

A simple floating display widget showing context-sensitive feedback.
No input handling - just visual feedback.

```cpp
class DialDisplay : public QWidget {
    Q_OBJECT
public:
    explicit DialDisplay(QWidget *parent = nullptr);
    
    // Display different contexts
    void showZoomLevel(int percent);
    void showToolInfo(const QString &toolName);
    void showPageInfo(int current, int total);
    void showThickness(int value);
    void showMessage(const QString &text);
    
    void setDarkMode(bool dark);
    
private:
    QLabel *m_label;
    // OLED-style circular display (keep the nice visual)
};
```

**Use cases for DialDisplay:**
- Show current zoom level during zoom gestures
- Show tool name on tool switch
- Show page number during page navigation
- Show thickness value during thickness adjustment
- General status messages

---

### MW2.4: Simplify DialTypes.h

**Reduce from 7 modes to minimal set:**

```cpp
// OLD (delete):
enum DialMode {
    None,
    PageSwitching,
    ZoomControl,
    ThicknessControl,
    ToolSwitching,
    PresetSelection,
    PanAndPageScroll
};

// NEW (if any dial input support kept):
// May not even need this - DialDisplay just shows context
```

---

### MW2.5: Delete Skeleton Files

**Delete the files we created in MW2.1 that are no longer needed:**
- `source/input/DialController.h/cpp` (unless keeping minimal hardware dial support)
- `source/input/MouseDialHandler.h/cpp`
- `source/ui/DialModeToolbar.h/cpp`

**Keep:**
- `source/input/DialTypes.h` (simplify or delete if not needed)
- Create `source/ui/DialDisplay.h/cpp` instead

---

### MW2.6: Update CMakeLists.txt

Remove dial controller conditional compilation (no longer needed):

```cmake
# DELETE:
option(ENABLE_DIAL_CONTROLLER ...)
set(DIAL_SOURCES ...)

# ADD:
# DialDisplay is always included (it's just a display widget)
set(UI_SOURCES
    ...
    source/ui/DialDisplay.cpp
)
```

---

### MW2.7: Decide on SDLControllerManager

**Options:**
A) **Keep as-is** - It's for gamepad support, separate from dial
B) **Simplify** - Remove dial-related mappings, keep only gamepad actions
C) **Delete entirely** - If gamepad support not needed

**Recommendation:** Option B - Keep SDLControllerManager for gamepad, 
but remove any dial mode switching it does. Gamepad buttons can directly 
trigger actions (zoom, tool switch) without going through dial modes

---

### MW2.8: Verification

**Compile and test:**
- [ ] `./compile.sh` succeeds
- [ ] Application runs without dial code
- [ ] DialDisplay shows appropriate feedback during operations

**Functional tests:**
- [ ] MagicDial rotates and changes tools
- [ ] Mode switching works
- [ ] Mouse dial (hold button + scroll) works
- [ ] SDL controller (if connected) works

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

### MW5.2: Create source/input/README.md

```markdown
# Input Module

Dial controller system for SpeedyNote. Excluded on Android builds.

## Components
- `DialController` - MagicDial widget and rotation handling
- `DialModeToolbar` - Mode selection buttons
- `MouseDialHandler` - Mouse button + scroll wheel input
- `SDLControllerHandler` - Gamepad support (optional, requires SDL2)

## Compile Flags
- `ENABLE_DIAL_CONTROLLER` - ON by default, OFF on Android
```

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

### Phase 2: Simplify Dial System (Revised - Q3 Approach)
- [x] MW2.1: Create directory structure ✅ (will be simplified)
- [x] MW2.2: Delete dial mode system from MainWindow ✅ (**-1,190 lines!**)
- [ ] MW2.3: Create simple DialDisplay widget
- [ ] MW2.4: Simplify/delete DialTypes.h
- [ ] MW2.5: Delete unused skeleton files
- [ ] MW2.6: Update CMakeLists.txt
- [ ] MW2.7: Decide on SDLControllerManager
- [ ] MW2.8: Verification

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
- [ ] MW5.2: Create input module README

---

## Appendix: File Changes Summary

| File | Action |
|------|--------|
| `source/MainWindow.cpp` | Major cleanup, ~4,200 lines removed/moved |
| `source/MainWindow.h` | Remove unused declarations |
| `source/SDLControllerManager.cpp` | Move to input/ module |
| `source/SDLControllerManager.h` | Move to input/ module |
| `source/input/DialController.cpp` | NEW |
| `source/input/DialController.h` | NEW |
| `source/input/DialModeToolbar.cpp` | NEW |
| `source/input/DialModeToolbar.h` | NEW |
| `source/input/MouseDialHandler.cpp` | NEW |
| `source/input/MouseDialHandler.h` | NEW |
| `source/input/SDLControllerHandler.cpp` | NEW (renamed) |
| `source/input/SDLControllerHandler.h` | NEW (renamed) |
| `source/input/CMakeLists.txt` | NEW |
| `source/ui/SubToolbar.h` | NEW (stub only) |
| `CMakeLists.txt` | Add input/ subdirectory |

