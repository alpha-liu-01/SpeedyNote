# Toolbar Extraction Subplan

**Document Version:** 1.0  
**Date:** January 4, 2026  
**Status:** 🔄 IN PROGRESS  
**Prerequisites:** Q&A complete (see TOOLBAR_EXTRACTION_QA.md)

---

## Overview

Extract toolbar-related UI from MainWindow (~6900 lines) into modular components. This creates a clean separation between app chrome (navigation, tabs, tools) and document content (DocumentViewport).

### Target Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│ NavigationBar [←][📑][🔖][📄] filename.spn [📝][💾][⛶][👆][⋮] │
├─────────────────────────────────────────────────────────────────┤
│ TabBar        [Tab1] [Tab2] [Active Tab (gray)]                 │
├─────────────────────────────────────────────────────────────────┤
│ Toolbar       [🖊️][🖍️][⌫][◯][📝][T][↩️][↪️]                    │
├──────┬──────────────────────────────────────────────────────────┤
│ Sub  │                                                          │
│ tool │              DocumentViewport                            │
│ bar  │                                                          │
└──────┴──────────────────────────────────────────────────────────┘
```

### Color Scheme
- **Accent color:** NavigationBar, TabBar, inactive tabs
- **Gray (theme-aware):** Active tab, Toolbar, DocumentViewport background

---

## Phase A: NavigationBar

### Task A.1: Create NavigationBar Class
**Status:** ⬜ Not Started

**Files to create:**
- `source/ui/NavigationBar.h`
- `source/ui/NavigationBar.cpp`

**Class structure:**
```cpp
class NavigationBar : public QWidget {
    Q_OBJECT
public:
    explicit NavigationBar(QWidget *parent = nullptr);
    
    void setFilename(const QString &filename);
    void updateTheme(bool darkMode, const QColor &accentColor);
    
signals:
    // Left side - layout toggles
    void launcherToggled();
    void pdfOutlineToggled();
    void bookmarksToggled();
    void layerPanelToggled();
    
    // Right side - actions
    void markdownNotesToggled();
    void saveClicked();
    void fullscreenToggled();
    void touchGestureModeChanged(int mode); // 3-state
    void menuRequested();
    
private:
    // Left buttons
    QPushButton *launcherButton;
    QPushButton *outlineButton;
    QPushButton *bookmarksButton;
    QPushButton *layerPanelButton;
    
    // Center
    QLabel *filenameLabel;
    
    // Right buttons
    QPushButton *markdownNotesButton;
    QPushButton *saveButton;
    QPushButton *fullscreenButton;
    QPushButton *touchGestureButton; // 3-state toggle
    QPushButton *menuButton;
};
```

**Checklist:**
- [ ] Create header with class definition
- [ ] Create cpp with constructor and button setup
- [ ] Implement `setFilename()` with elision for long names
- [ ] Implement `updateTheme()` for accent color
- [ ] Add to CMakeLists.txt

---

### Task A.2: Create Button Icons
**Status:** ⬜ Not Started

**Icons needed (light + dark variants):**
- [ ] `launcher.svg` / `launcher_dark.svg` (back/home arrow)
- [ ] `outline.svg` / `outline_dark.svg` (PDF outline)
- [ ] `bookmarks.svg` / `bookmarks_dark.svg`
- [ ] `layers.svg` / `layers_dark.svg`
- [ ] `markdown.svg` / `markdown_dark.svg`
- [ ] `save.svg` / `save_dark.svg`
- [ ] `fullscreen.svg` / `fullscreen_dark.svg`
- [ ] `touch_gesture_0.svg` (off)
- [ ] `touch_gesture_1.svg` (mode 1)
- [ ] `touch_gesture_2.svg` (mode 2)
- [ ] `menu.svg` / `menu_dark.svg` (three dots)

**Note:** Check existing icons in `resources/icons/` - many may already exist.

---

### Task A.3: Integrate NavigationBar into MainWindow
**Status:** ⬜ Not Started

**Changes to MainWindow:**
- [ ] Add `#include "ui/NavigationBar.h"`
- [ ] Create `NavigationBar *navigationBar` member
- [ ] Add to main layout (top position)
- [ ] Connect signals to existing MainWindow slots
- [ ] Remove old navigation-related buttons from MainWindow

**Signal connections:**
```cpp
connect(navigationBar, &NavigationBar::launcherToggled, 
        this, &MainWindow::toggleLauncher);
connect(navigationBar, &NavigationBar::saveClicked,
        this, &MainWindow::saveDocument);
// ... etc
```

---

### Task A.4: Verify NavigationBar
**Status:** ⬜ Not Started

**Verification checklist:**
- [ ] All buttons visible and clickable
- [ ] Filename displays correctly
- [ ] Theme changes apply (dark/light, accent color)
- [ ] All signals trigger correct MainWindow actions
- [ ] Layout fits within 768px minimum width
- [ ] Compile with no warnings

---

## Phase B: Toolbar

### Task B.1: Create Toolbar Class
**Status:** ⬜ Not Started

**Files to create:**
- `source/ui/Toolbar.h`
- `source/ui/Toolbar.cpp`

**Class structure:**
```cpp
class Toolbar : public QWidget {
    Q_OBJECT
public:
    explicit Toolbar(QWidget *parent = nullptr);
    
    void setCurrentTool(ToolType tool);
    void updateTheme(bool darkMode);
    
signals:
    void toolSelected(ToolType tool);
    void undoClicked();
    void redoClicked();
    
private:
    QButtonGroup *toolGroup; // Exclusive selection
    
    QPushButton *penButton;
    QPushButton *markerButton;
    QPushButton *eraserButton;
    QPushButton *lassoButton;
    QPushButton *objectSelectionButton;
    QPushButton *textButton;
    
    QPushButton *undoButton;  // Not in group (instant action)
    QPushButton *redoButton;  // Not in group (instant action)
    
    ToolType currentTool;
};
```

**Checklist:**
- [ ] Create header with class definition
- [ ] Create cpp with constructor and button setup
- [ ] Use QButtonGroup for exclusive tool selection
- [ ] Undo/Redo as separate instant-action buttons
- [ ] Implement `setCurrentTool()` for external sync
- [ ] Implement `updateTheme()` for gray background
- [ ] Add to CMakeLists.txt

---

### Task B.2: Tool Button Icons
**Status:** ⬜ Not Started

**Icons needed (light + dark variants):**
- [ ] `pen.svg` / `pen_dark.svg`
- [ ] `marker.svg` / `marker_dark.svg`
- [ ] `eraser.svg` / `eraser_dark.svg`
- [ ] `lasso.svg` / `lasso_dark.svg`
- [ ] `object_selection.svg` / `object_selection_dark.svg`
- [ ] `text.svg` / `text_dark.svg`
- [ ] `undo.svg` / `undo_dark.svg`
- [ ] `redo.svg` / `redo_dark.svg`

**Note:** Most should already exist in `resources/icons/`.

---

### Task B.3: Integrate Toolbar into MainWindow
**Status:** ⬜ Not Started

**Changes to MainWindow:**
- [ ] Add `#include "ui/Toolbar.h"`
- [ ] Create `Toolbar *toolbar` member
- [ ] Add to main layout (below TabBar)
- [ ] Connect `toolSelected` to DocumentViewport tool switching
- [ ] Connect `undoClicked`/`redoClicked` to undo/redo logic
- [ ] Remove old tool buttons from MainWindow

**Signal connections:**
```cpp
connect(toolbar, &Toolbar::toolSelected,
        this, &MainWindow::onToolSelected);
connect(toolbar, &Toolbar::undoClicked,
        this, &MainWindow::undo);
connect(toolbar, &Toolbar::redoClicked,
        this, &MainWindow::redo);
```

---

### Task B.4: Verify Toolbar
**Status:** ⬜ Not Started

**Verification checklist:**
- [ ] All tool buttons visible and clickable
- [ ] Only one tool selected at a time
- [ ] Tool selection updates DocumentViewport
- [ ] Undo/Redo work without changing selected tool
- [ ] Theme changes apply (gray background)
- [ ] Layout fits within 768px
- [ ] Compile with no warnings

---

## Phase C: TabBar (Optional)

### Task C.1: Convert to QTabBar
**Status:** ⬜ Not Started

**Current:** QTabWidget (manages tabs + content)  
**Target:** QTabBar (tabs only) + QStackedWidget or manual viewport management

**Changes:**
- [ ] Replace QTabWidget with QTabBar
- [ ] Manage DocumentViewport visibility separately
- [ ] Connect `QTabBar::currentChanged` to viewport switching
- [ ] Preserve tab close buttons
- [ ] Preserve tab styling (accent for inactive, gray for active)

**Note:** This phase is lower priority. Can be deferred if Phase A+B take longer than expected.

---

### Task C.2: Extract TabBar to Separate File (Optional)
**Status:** ⬜ Not Started

**Files to create:**
- `source/ui/TabBar.h`
- `source/ui/TabBar.cpp`

**Only if C.1 is successful and there's benefit to extraction.**

---

## Phase D: Subtoolbars (Deferred)

### Task D.1: Create SubToolbar Base Class
**Status:** ⬜ Not Started (Deferred)

**Files to create:**
- `source/ui/subtoolbars/SubToolbar.h`
- `source/ui/subtoolbars/SubToolbar.cpp`

**Base class for all subtoolbars:**
```cpp
class SubToolbar : public QWidget {
    Q_OBJECT
public:
    explicit SubToolbar(QWidget *parent = nullptr);
    virtual void updateTheme(bool darkMode) = 0;
};
```

---

### Task D.2: PenSubToolbar
**Status:** ⬜ Not Started (Deferred)

**Files:** `source/ui/subtoolbars/PenSubToolbar.h/cpp`

**Contents:**
- 3 customizable color preset buttons
- 3 customizable thickness preset buttons
- Color picker access
- Thickness slider/input

---

### Task D.3: MarkerSubToolbar
**Status:** ⬜ Not Started (Deferred)

**Files:** `source/ui/subtoolbars/MarkerSubToolbar.h/cpp`

**Contents:** Similar to PenSubToolbar (color + thickness presets)

---

### Task D.4: EraserSubToolbar
**Status:** ⬜ Not Started (Deferred)

**Files:** `source/ui/subtoolbars/EraserSubToolbar.h/cpp`

**Contents:**
- Eraser size presets
- Eraser mode (stroke vs area)

---

### Task D.5: ObjectSelectionSubToolbar
**Status:** ⬜ Not Started (Deferred)

**Files:** `source/ui/subtoolbars/ObjectSelectionSubToolbar.h/cpp`

**Contents:**
- Object affinity controls
- Z-order controls (bring forward, send back)
- Alignment tools (future)

---

### Task D.6: Other Subtoolbars
**Status:** ⬜ Not Started (Deferred)

- LassoSubToolbar (cut/copy/delete after selection)
- TextSubToolbar (font, size, color)

---

## Phase E: Cleanup

### Task E.1: Remove Old Toolbar Code from MainWindow
**Status:** ⬜ Not Started

After all phases verified:
- [ ] Delete old button creation code
- [ ] Delete old layout code (single/double row)
- [ ] Delete old style application code
- [ ] Delete unused member variables
- [ ] Update any remaining signal connections

---

### Task E.2: Create External QSS
**Status:** ⬜ Not Started

**Files to create:**
- `resources/styles/toolbar.qss` (or similar)

**Contents:**
```css
/* Navigation bar */
NavigationBar { ... }
NavigationBar QPushButton { ... }

/* Toolbar */
Toolbar { ... }
Toolbar QPushButton { ... }
Toolbar QPushButton:checked { ... }

/* Subtoolbars */
SubToolbar { ... }
```

---

### Task E.3: Final Verification
**Status:** ⬜ Not Started

- [ ] Full app test on desktop
- [ ] Theme switching works
- [ ] All tools functional
- [ ] All navigation actions work
- [ ] Tab switching works
- [ ] No MainWindow toolbar remnants
- [ ] Code compiles with no warnings
- [ ] Line count significantly reduced

---

## Progress Summary

| Phase | Description | Status | Tasks |
|-------|-------------|--------|-------|
| A | NavigationBar | ⬜ Not Started | 0/4 |
| B | Toolbar | ⬜ Not Started | 0/4 |
| C | TabBar | ⬜ Not Started | 0/2 |
| D | Subtoolbars | ⬜ Deferred | 0/6 |
| E | Cleanup | ⬜ Not Started | 0/3 |

**Overall:** 0/19 tasks complete

---

## Reference

### Files to Backup Before Starting
- `source/MainWindow.cpp`
- `source/MainWindow.h`

### Related Documents
- `docs/TOOLBAR_EXTRACTION_QA.md` - Design decisions
- `docs/MAINWINDOW_CLEANUP_SUBPLAN.md` - Parent cleanup plan
- `docs/MAINWINDOW_CLEANUP_QA.md` - Earlier Q&A

### Existing Code References
Check MainWindow.cpp for:
- Button creation: search for `new QPushButton`
- Tool switching: search for `changeTool`
- Layout code: search for `createSingleRowLayout`, `createDoubleRowLayout`
- Style code: search for `createButtonStyle`, `setStyleSheet`

