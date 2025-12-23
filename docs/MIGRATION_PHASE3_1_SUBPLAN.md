# Phase 3.1: Disconnect InkCanvas and Replace Tab System

## Overview

Phase 3.1 removes all InkCanvas dependencies from MainWindow and replaces the custom tab system (`tabList` + `canvasStack`) with `QTabWidget` via `TabManager`.

**Goal:** MainWindow compiles without InkCanvas. Many features will be broken/stubbed.

**Reference Files:**
- `source/MainWindow_OLD.cpp` - Original MainWindow before changes
- `source/MainWindow_OLD.h` - Original header before changes

---

## Success Criteria

- [ ] MainWindow.cpp compiles without errors
- [ ] MainWindow.h has no InkCanvas includes or members
- [ ] VectorCanvas and its buttons are removed
- [ ] App launches (even if most features don't work)
- [ ] No runtime crashes on basic operations (open/close app)

---

## Task Breakdown

### Task 3.1.1: Replace Tab System with QTabWidget (~200 lines)

**Goal:** Replace `tabList` (QListWidget) + `canvasStack` (QStackedWidget) with `QTabWidget` managed by `TabManager`.

**Files:** `MainWindow.h`, `MainWindow.cpp`

**Remove:**
```cpp
// MainWindow.h - REMOVE these members:
QListWidget *tabList;
QStackedWidget *canvasStack;
QMap<InkCanvas*, int> pageMap;
```

**Add:**
```cpp
// MainWindow.h - ADD these members:
#include "ui/TabManager.h"
TabManager* m_tabManager = nullptr;
```

**Changes in MainWindow.cpp:**
1. Remove `tabList` creation (~lines 898-910)
2. Remove `canvasStack` creation (~lines 141, 1306-1313)
3. Remove `pageMap` usage (search for `pageMap[`)
4. Initialize `m_tabManager` with a new `QTabWidget`
5. Update `switchTab()` to use TabManager

**Verification:** App compiles, tab bar shows (empty)

---

### Task 3.1.2: Remove addNewTab() InkCanvas Code (~150 lines)

**Goal:** Replace InkCanvas creation with stub/placeholder.

**Files:** `MainWindow.cpp`

**Current code (lines ~3896-4160):**
- Creates `InkCanvas *newCanvas = new InkCanvas(this);`
- Connects 12+ signals
- Sets up touch gestures, pageMap, etc.

**New code:**
```cpp
void MainWindow::addNewTab() {
    // TODO: Phase 3.2 - Create DocumentViewport tab
    qDebug() << "addNewTab(): Not implemented yet (Phase 3.2)";
}
```

**Also stub:**
- `removeTabAt()` - just remove from TabManager
- `switchTab()` - delegate to TabManager
- `findTabWithNotebookId()` - stub

**Verification:** App compiles, addNewTab does nothing

---

### Task 3.1.3: Remove VectorCanvas and Buttons (~100 lines) ✅ COMPLETE

**Goal:** Remove VectorCanvas overlay and its toolbar buttons.

**Files:** `MainWindow.h`, `MainWindow.cpp`, `CMakeLists.txt`

**Remove from MainWindow.h:**
```cpp
// Remove these members:
QPushButton *vectorPenButton;
QPushButton *vectorEraserButton;
QPushButton *vectorUndoButton;
```

**Remove from MainWindow.cpp:**
- VectorCanvas #include
- Button creation and connections
- `setVectorPenTool()`, `setVectorEraserTool()`, `vectorUndo()` methods

**Remove from CMakeLists.txt:**
```cmake
# Remove from SOURCES:
source/VectorCanvas.cpp
```

**Verification:** App compiles without VectorCanvas

---

### Task 3.1.4: Create currentViewport() (~300 lines)

**Goal:** Replace 200+ `currentCanvas()` calls with `currentViewport()`.

**Files:** `MainWindow.h`, `MainWindow.cpp`

**Add to MainWindow.h:**
```cpp
// Forward declaration
class DocumentViewport;

// New accessor
DocumentViewport* currentViewport() const;
```

**Add to MainWindow.cpp:**
```cpp
DocumentViewport* MainWindow::currentViewport() const {
    if (m_tabManager) {
        return m_tabManager->currentViewport();
    }
    return nullptr;
}
```

**Replace pattern:**
```cpp
// OLD:
if (InkCanvas* canvas = currentCanvas()) {
    canvas->doSomething();
}

// NEW (stubbed):
if (DocumentViewport* vp = currentViewport()) {
    // TODO: vp->doSomething();
}
// OR just comment out the body
```

**High-priority replacements:**
- Tool setters: `setTool()`, `setPenColor()`, `setPenThickness()`
- View getters: `getZoom()`, basic state queries

**Verification:** All `currentCanvas()` calls replaced or removed

---

### Task 3.1.5: Remove/Stub Signal Handlers (~150 lines)

**Goal:** Remove or stub InkCanvas signal handlers.

**Files:** `MainWindow.cpp`

**Handlers to stub (keep signature, empty body):**
```cpp
void MainWindow::handleTouchZoomChange(int zoom) {
    // TODO: Phase 3.3 - Connect to DocumentViewport
}

void MainWindow::handleTouchPanChange(int panX, int panY) {
    // TODO: Phase 3.3 - Connect to DocumentViewport
}

void MainWindow::handleTouchGestureEnd() {
    // TODO: Phase 3.3
}

void MainWindow::onAutoScrollRequested(int direction) {
    // REMOVED: Not needed with new continuous scrolling
}

void MainWindow::onEarlySaveRequested() {
    // TODO: Phase 3.5 - File operations
}
```

**Handlers to remove entirely:**
- `handleTouchPanningChanged()` - InkCanvas specific
- `showRopeSelectionMenu()` - Lasso tool (Phase 2B)

**Verification:** No undefined reference errors

---

### Task 3.1.6: Remove Page Navigation Methods (~200 lines)

**Goal:** Remove/stub methods that depend on old page model.

**Files:** `MainWindow.cpp`

**Methods to stub:**
```cpp
void MainWindow::switchPage(int page) {
    // TODO: Phase 3.3.4 - Use viewport->scrollToPage()
    Q_UNUSED(page);
}

void MainWindow::switchPageWithDirection(int page, int direction) {
    // TODO: Phase 3.3.4
    Q_UNUSED(page);
    Q_UNUSED(direction);
}

void MainWindow::goToPreviousPage() {
    // TODO: Phase 3.3.4
}

void MainWindow::goToNextPage() {
    // TODO: Phase 3.3.4
}

int MainWindow::getCurrentPageForCanvas(InkCanvas* canvas) {
    // REMOVED: Use viewport->currentPage() instead
    Q_UNUSED(canvas);
    return 0;
}
```

**Methods to remove entirely:**
- `saveCurrentPageConcurrent()` - combined canvas specific
- `onAutoScrollRequested()` - not needed with continuous scroll

**Verification:** App compiles, page navigation buttons do nothing

---

### Task 3.1.7: Remove InkCanvas Includes and Members (~50 lines)

**Goal:** Clean removal of InkCanvas from MainWindow.

**Files:** `MainWindow.h`, `MainWindow.cpp`

**Remove from MainWindow.h:**
```cpp
// Remove:
#include "InkCanvas.h"
InkCanvas *canvas;  // if exists as member

// Remove method declarations:
InkCanvas* currentCanvas();
int getCurrentPageForCanvas(InkCanvas* canvas);
```

**Remove from MainWindow.cpp:**
```cpp
// Remove:
#include "InkCanvas.h"

// Remove currentCanvas() implementation
```

**Verification:** No InkCanvas references in MainWindow

---

### Task 3.1.8: Disable ControlPanelDialog (~30 lines)

**Goal:** Temporarily disable ControlPanelDialog (depends on InkCanvas).

**Files:** `MainWindow.cpp`, `ControlPanelDialog.h`

**Option A - Stub the call:**
```cpp
void MainWindow::openControlPanel() {
    QMessageBox::information(this, tr("Control Panel"), 
        tr("Control Panel is being redesigned. Coming soon!"));
    // TODO: Phase 4.6 - Reconnect ControlPanelDialog
}
```

**Option B - Keep dialog but pass nullptr:**
- Modify ControlPanelDialog to handle nullptr canvas
- Skip canvas-dependent tabs

**Recommended:** Option A (simpler)

**Verification:** Control Panel button shows message, no crash

---

### Task 3.1.9: Remove Markdown/Highlight Handlers (~50 lines)

**Goal:** Stub markdown note handlers (Phase 4 feature).

**Files:** `MainWindow.cpp`

**Methods to stub:**
```cpp
void MainWindow::onMarkdownNotesUpdated() {
    // TODO: Phase 4.5 - Markdown notes
}

void MainWindow::onHighlightDoubleClicked(const QString& highlightId) {
    // TODO: Phase 4.5
    Q_UNUSED(highlightId);
}

void MainWindow::loadMarkdownNotesForCurrentPage() {
    // TODO: Phase 4.5
}
```

**Verification:** No crashes when features are accessed

---

## Estimated Changes Summary

| Task | Files | Lines Changed | Priority |
|------|-------|---------------|----------|
| 3.1.1 | MainWindow.h/.cpp | ~200 | HIGH |
| 3.1.2 | MainWindow.cpp | ~150 | HIGH |
| 3.1.3 | MainWindow.h/.cpp, CMakeLists | ~100 | HIGH |
| 3.1.4 | MainWindow.h/.cpp | ~300 | HIGH |
| 3.1.5 | MainWindow.cpp | ~150 | MEDIUM |
| 3.1.6 | MainWindow.cpp | ~200 | MEDIUM |
| 3.1.7 | MainWindow.h/.cpp | ~50 | HIGH |
| 3.1.8 | MainWindow.cpp | ~30 | LOW |
| 3.1.9 | MainWindow.cpp | ~50 | LOW |
| **TOTAL** | | **~1230** | |

---

## Order of Implementation

1. **3.1.3** - Remove VectorCanvas first (isolated change)
2. **3.1.1** - Replace tab system
3. **3.1.2** - Remove addNewTab InkCanvas code
4. **3.1.7** - Remove InkCanvas includes/members
5. **3.1.4** - Create currentViewport(), replace currentCanvas()
6. **3.1.6** - Remove page navigation methods
7. **3.1.5** - Stub signal handlers
8. **3.1.8** - Disable ControlPanelDialog
9. **3.1.9** - Stub markdown handlers

---

## After Phase 3.1

The app will:
- ✅ Compile without InkCanvas or VectorCanvas
- ✅ Launch and show main window
- ✅ Have TabManager ready for DocumentViewport tabs
- ❌ Not create any documents/tabs yet (Phase 3.2)
- ❌ Have many buttons that do nothing (Phase 3.3+)
- ❌ Not save/load anything (Phase 3.5)

---

## Reference: Methods to Keep (reconnect later)

These methods should be kept but stubbed - they'll be reconnected in Phase 3.3+:

**Tool methods:**
- `setPenTool()`, `setMarkerTool()`, `setEraserTool()`
- `onPenColorChanged()`, `onThicknessChanged()`

**Zoom methods:**
- `updateZoom()`, `onZoomSliderChanged()`
- `handleTouchZoomChange()`

**File methods:**
- `saveNotebook()`, `openSpnPackage()`, `openPdfFile()`

**Sidebar methods:**
- `toggleOutlineSidebar()`, `toggleBookmarksSidebar()`
- `loadPdfOutline()`

---

*Document created for SpeedyNote Phase 3.1 migration*
