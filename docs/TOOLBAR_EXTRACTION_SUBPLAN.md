# Toolbar Extraction Subplan

**Document Version:** 1.0  
**Date:** January 4, 2026  
**Status:** 🔄 IN PROGRESS  
**Prerequisites:** Q&A complete (see TOOLBAR_EXTRACTION_QA.md)

---

## Overview

Extract toolbar-related UI from MainWindow (~6900 lines) into modular components. This creates a clean separation between app chrome (navigation, tabs, tools) and document content (DocumentViewport).

**Phase order:** Define button types first (Phase 0), then build bars using those types.

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

## Phase 0: Define Button Types

### Task 0.1: Create ToolbarButtons Module
**Status:** ✅ Complete

**Files to create:**
- `source/ui/ToolbarButtons.h`
- `source/ui/ToolbarButtons.cpp`

**Button types to define:**

#### ActionButton
- **Behavior:** Click triggers instant action, no persistent state
- **States:** idle, hover, pressed
- **Examples:** Save, Undo, Redo, Menu, Launcher (back)

#### ToggleButton  
- **Behavior:** Click toggles on/off state
- **States:** off, off+hover, on, on+hover, pressed
- **Examples:** Bookmarks, Outline, Layers, Fullscreen, Markdown Notes

#### ThreeStateButton
- **Behavior:** Click cycles through 3 states
- **States:** state1, state2 (red shade), state3, hover variants, pressed
- **Examples:** Touch gesture mode (off / y-axis only / on)
- **Reference:** Current touch gesture button behavior in MainWindow

#### ToolButton
- **Behavior:** Exclusive selection (radio group), opens associated subtoolbar
- **States:** Same as ToggleButton (visually identical)
- **Examples:** Pen, Marker, Eraser, Lasso, Object Selection, Text
- **Note:** Uses QButtonGroup for exclusive selection

**Common properties (all button types):**
- Size: 36×36 logical pixels
- Icon support (light/dark variants)
- Theme-aware styling
- Same appearance whether on NavigationBar or Toolbar

---

### Task 0.2: Define Button QSS Styling
**Status:** ✅ Complete

**Files created:**
- `resources/styles/buttons.qss` - Light mode styles
- `resources/styles/buttons_dark.qss` - Dark mode styles
- Added to `resources.qrc`

**Added to ToolbarButtons module:**
- `ButtonStyles::applyToWidget(widget, darkMode)` - Apply styles to parent widget
- `ButtonStyles::getStylesheet(darkMode)` - Get stylesheet string

**QSS uses objectName selectors:**
```css
QPushButton#ActionButton { ... }
QPushButton#ToggleButton:checked { ... }
QPushButton#ThreeStateButton[state="1"] { ... }  /* Red shade */
QPushButton#ToolButton { ... }
```

**Theme colors:**
- Light mode: dark highlights (`rgba(0,0,0,N)`)
- Dark mode: white highlights (`rgba(255,255,255,N)`)
- State 1 (3-state): red shade (`rgba(255,100,100,N)`)

---

### Task 0.3: Implement Button Classes
**Status:** ⬜ Not Started

**Class structure:**

```cpp
// source/ui/ToolbarButtons.h

// Base class (optional, or just use QPushButton directly)
class ToolbarButton : public QPushButton {
    Q_OBJECT
public:
    explicit ToolbarButton(QWidget *parent = nullptr);
    void setDarkMode(bool dark);
    void setIcon(const QString &lightIcon, const QString &darkIcon);
protected:
    QString lightIconPath;
    QString darkIconPath;
    bool darkMode = false;
};

class ActionButton : public ToolbarButton {
    Q_OBJECT
public:
    explicit ActionButton(QWidget *parent = nullptr);
    // No additional state - just styled differently
};

class ToggleButton : public ToolbarButton {
    Q_OBJECT
public:
    explicit ToggleButton(QWidget *parent = nullptr);
    // Uses setCheckable(true)
};

class ThreeStateButton : public ToolbarButton {
    Q_OBJECT
    Q_PROPERTY(int state READ state WRITE setState NOTIFY stateChanged)
public:
    explicit ThreeStateButton(QWidget *parent = nullptr);
    
    int state() const;
    void setState(int state);
    
signals:
    void stateChanged(int newState);
    
protected:
    void mousePressEvent(QMouseEvent *event) override;
    
private:
    int m_state = 0; // 0, 1, or 2
    QString stateIcons[3];
};

class ToolButton : public ToggleButton {
    Q_OBJECT
public:
    explicit ToolButton(QWidget *parent = nullptr);
    // Same as ToggleButton, but semantically for tools
    // Used with QButtonGroup for exclusive selection
};
```

**Checklist:**
- [ ] Create header with class definitions
- [ ] Implement ToolbarButton base (icon handling, dark mode)
- [ ] Implement ActionButton (minimal, just applies style class)
- [ ] Implement ToggleButton (setCheckable, style class)
- [ ] Implement ThreeStateButton (state cycling, Q_PROPERTY for QSS)
- [ ] Implement ToolButton (same as Toggle, semantic distinction)
- [ ] Add to CMakeLists.txt

---

### Task 0.4: Test Button Types
**Status:** ⬜ Not Started

**Verification:**
- [ ] Create simple test widget with one of each button type
- [ ] Verify hover states work
- [ ] Verify toggle on/off visually distinct
- [ ] Verify 3-state cycles correctly with visual feedback
- [ ] Verify dark mode icon switching
- [ ] Verify 36×36 sizing

---

## Phase A: NavigationBar

### Task A.1: Create NavigationBar Class
**Status:** ⬜ Not Started

**Files to create:**
- `source/ui/NavigationBar.h`
- `source/ui/NavigationBar.cpp`

**Prerequisites:** Phase 0 complete (button types defined)

**Class structure:**
```cpp
#include "ui/ToolbarButtons.h"

class NavigationBar : public QWidget {
    Q_OBJECT
public:
    explicit NavigationBar(QWidget *parent = nullptr);
    
    void setFilename(const QString &filename);
    void updateTheme(bool darkMode, const QColor &accentColor);
    
signals:
    // Left side - layout toggles
    void launcherClicked();
    void pdfOutlineToggled(bool checked);
    void bookmarksToggled(bool checked);
    void layerPanelToggled(bool checked);
    
    // Right side - actions
    void markdownNotesToggled(bool checked);
    void saveClicked();
    void fullscreenToggled(bool checked);
    void touchGestureModeChanged(int mode); // 0, 1, or 2
    void menuRequested();
    
private:
    // Left buttons (using Phase 0 button types)
    ActionButton *launcherButton;      // Back to launcher
    ToggleButton *outlineButton;
    ToggleButton *bookmarksButton;
    ToggleButton *layerPanelButton;
    
    // Center
    QLabel *filenameLabel;
    
    // Right buttons
    ToggleButton *markdownNotesButton;
    ActionButton *saveButton;
    ToggleButton *fullscreenButton;
    ThreeStateButton *touchGestureButton;
    ActionButton *menuButton;
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

**Prerequisites:** Phase 0 complete (button types defined)

**Class structure:**
```cpp
#include "ui/ToolbarButtons.h"

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
    QButtonGroup *toolGroup; // Exclusive selection for ToolButtons
    
    // Tool buttons (using Phase 0 button types)
    ToolButton *penButton;
    ToolButton *markerButton;
    ToolButton *eraserButton;
    ToolButton *lassoButton;
    ToolButton *objectSelectionButton;
    ToolButton *textButton;
    
    // Action buttons (instant, not in group)
    ActionButton *undoButton;
    ActionButton *redoButton;
    
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
| 0 | Button Types | 🔄 In Progress | 2/4 |
| A | NavigationBar | ⬜ Not Started | 0/4 |
| B | Toolbar | ⬜ Not Started | 0/4 |
| C | TabBar | ⬜ Not Started | 0/2 |
| D | Subtoolbars | ⬜ Deferred | 0/6 |
| E | Cleanup | ⬜ Not Started | 0/3 |

**Overall:** 2/23 tasks complete

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



