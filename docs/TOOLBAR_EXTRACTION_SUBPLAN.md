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
**Status:** ✅ Complete (merged with Task 0.1)

Button classes were implemented in Task 0.1. All classes verified working by Task 0.4 unit tests.

**Implemented classes:**
- `ToolbarButton` - Base class: 36×36 size, `setThemedIcon()`, `setDarkMode()`
- `ActionButton` - Not checkable, instant action
- `ToggleButton` - Checkable, on/off state
- `ThreeStateButton` - Cycles 0→1→2, Q_PROPERTY for QSS `[state="N"]`
- `ToolButton` - Same as ToggleButton, for use with QButtonGroup

---

### Task 0.4: Test Button Types
**Status:** ✅ Complete

**Files created:**
- `source/ui/ToolbarButtonTests.h/cpp` - Unit tests using QTest
- `source/ui/ToolbarButtonTestWidget.h/cpp` - Visual test widget

**Unit tests (all passing):**
- [x] testActionButton - Not checkable, correct objectName, 36×36 size
- [x] testToggleButton - Checkable, toggle behavior
- [x] testThreeStateButton - State cycling 0→1→2→0, bounds clamping
- [x] testToolButton - Checkable, correct objectName
- [x] testToolButtonGroup - Exclusive selection with QButtonGroup
- [x] testIconLoading - Icon loading, dark mode switching
- [x] testButtonStyles - QSS loading for light/dark themes

**Run commands:**
```bash
./build/NoteApp --test-buttons        # Unit tests
./build/NoteApp --test-buttons-visual # Visual test widget
```

**Visual test widget features:**
- All 4 button types displayed
- Dark mode toggle
- Status labels showing state changes
- Tool buttons in exclusive group

---

## Phase A: NavigationBar

### Task A.1: Create NavigationBar Class
**Status:** ✅ Complete

**Files to create:**
- `source/ui/NavigationBar.h`
- `source/ui/NavigationBar.cpp`

**Prerequisites:** Phase 0 complete (button types defined)

**Layout:**
```
┌──────────────────────────────────────────────────────────────────────────┐
│ [←][📁][💾][+]          document_name.snb          [⛶][📤][📝][⋮] │
│  │   │   │  │                  │                     │   │   │  │       │
│  │   │   │  └─ Add (stub)      │                     │   │   │  └─ Menu │
│  │   │   └─ Save               │                     │   │   └─ Right   │
│  │   └─ Left Sidebar Toggle    │                     │   │      Sidebar │
│  └─ Back to Launcher           └─ Filename           │   └─ Share(stub)│
│                                   (click=toggle tabs)└─ Fullscreen     │
└──────────────────────────────────────────────────────────────────────────┘
```

**Class structure:**
```cpp
#include "ui/ToolbarButtons.h"
#include <QLabel>

class NavigationBar : public QWidget {
    Q_OBJECT
public:
    explicit NavigationBar(QWidget *parent = nullptr);
    
    void setFilename(const QString &filename);
    void updateTheme(bool darkMode, const QColor &accentColor);
    
signals:
    // Left side
    void launcherClicked();
    void leftSidebarToggled(bool checked);
    void saveClicked();
    void addClicked();  // Stubbed - will show menu in future
    
    // Center
    void filenameClicked();  // Toggle tab container visibility
    
    // Right side
    void fullscreenToggled(bool checked);
    void shareClicked();  // Stubbed - placeholder
    void rightSidebarToggled(bool checked);  // Markdown notes
    void menuRequested();
    
private:
    // Left buttons
    ActionButton *launcherButton;
    ToggleButton *leftSidebarButton;
    ActionButton *saveButton;
    ActionButton *addButton;  // Stubbed
    
    // Center
    QPushButton *filenameButton;  // Clickable label
    
    // Right buttons
    ToggleButton *fullscreenButton;
    ActionButton *shareButton;  // Stubbed
    ToggleButton *rightSidebarButton;
    ActionButton *menuButton;
    
    // State
    bool m_darkMode = false;
    QColor m_accentColor;
};
```

**Checklist:**
- [x] Create header with class definition
- [x] Create cpp with constructor and button setup
- [x] Implement `setFilename()` with elision for long names
- [x] Implement `updateTheme()` for accent color (#2277CC default)
- [x] Make filename clickable (emits filenameClicked)
- [x] Add to CMakeLists.txt

---

### Task A.2: Check/Create Button Icons
**Status:** ✅ Complete

**Icons needed (check existing in `resources/icons/` first):**

| Button | Icon Base Name | Exists? |
|--------|---------------|---------|
| Back to Launcher | `folder` or new `back` | recent.png ✓ |
| Left Sidebar Toggle | `outline` or new `sidebar` | leftsidebar.png ✓ |
| Save | `save` | save.png ✓ |
| Add | `addtab` | addtab.png ✓ |
| Fullscreen | `fullscreen` | fullscreen.png ✓ |
| Share | new `share` or `export` | export.png ✓ |
| Right Sidebar Toggle | `markdown` | rightsidebar.png ✓ |
| Menu | `menu` | menu.png ✓ |

**Note:** Most icons already exist. May need to verify they look appropriate for nav bar context. I manually corrected the file names. Since the reversed icons always have a "_reversed" suffix, and it's handled by buttons themselves, so no worries here. 

---

### Task A.3: Integrate NavigationBar into MainWindow
**Status:** ✅ Complete

**Changes to MainWindow:**
- [x] Add `#include "ui/NavigationBar.h"`
- [x] Create `NavigationBar *m_navigationBar` member
- [x] Add to main layout (top position, above controlBar)
- [x] Connect signals to existing MainWindow slots/stubs
- [ ] Remove old navigation-related buttons from MainWindow (deferred to Phase E)

**Signal connections:**
```cpp
// Left side
connect(navigationBar, &NavigationBar::launcherClicked, 
        this, &MainWindow::showLauncher);  // Or stub, since we don't have the new left sidebar yet.
connect(navigationBar, &NavigationBar::leftSidebarToggled,
        this, &MainWindow::toggleLeftSidebar);  // Stub initially
connect(navigationBar, &NavigationBar::saveClicked,
        this, &MainWindow::saveDocument);
connect(navigationBar, &NavigationBar::addClicked,
        this, &MainWindow::showAddMenu);  // Stub

// Center
connect(navigationBar, &NavigationBar::filenameClicked,
        this, &MainWindow::toggleTabBarVisibility);  // Stub

// Right side
connect(navigationBar, &NavigationBar::fullscreenToggled,
        this, &MainWindow::setFullscreen);
connect(navigationBar, &NavigationBar::shareClicked,
        []() { /* Stub - do nothing */ });
connect(navigationBar, &NavigationBar::rightSidebarToggled,
        this, &MainWindow::toggleMarkdownNotes);  // Or stub
connect(navigationBar, &NavigationBar::menuRequested,
        this, &MainWindow::showOverflowMenu);
```

**Note:** Many slots will be stubs initially. Core functionality (save, fullscreen) should work.

---

### Task A.4: Verify NavigationBar
**Status:** ✅ Complete

**Verification checklist:**
- [x] All buttons visible and clickable
- [x] Filename displays correctly
- [x] Theme changes apply (dark/light, accent color)
- [x] All signals trigger correct MainWindow actions
- [x] Layout fits within 768px minimum width
- [x] Compile with no warnings
- [x] Memory: No leak - NavigationBar has MainWindow as parent (auto-cleanup)

**Known issue (deferred):**
- Filename doesn't refresh when tab title changes - will be fixed when tab layout is redesigned

---

## Phase B: Toolbar

### Task B.1: Create Toolbar Class
**Status:** ✅ Complete

**Files to create:**
- `source/ui/Toolbar.h`
- `source/ui/Toolbar.cpp`

**Prerequisites:** Phase 0 complete (button types defined)

**Layout (center-aligned):**
```
┌─────────────────────────────────────────────────────────────────────────────────┐
│        [🖊️][🖍️][⌫][📐][🔗][📷][T]    ─gap─    [↩️][↪️]  [👆]                 │
│          │   │   │  │  │  │  │                  │   │    │                     │
│          │   │   │  │  │  │  └─ Text            │   │    └─ Touch Gesture      │
│          │   │   │  │  │  └─ Object Insert      │   └─ Redo                    │
│          │   │   │  │  └─ Lasso (rope)          └─ Undo                        │
│          │   │   │  └─ Shape (→straight line)                                  │
│          │   │   └─ Eraser                                                     │
│          │   └─ Marker                                                         │
│          └─ Pen                                                                │
└─────────────────────────────────────────────────────────────────────────────────┘
         [------ Tool Buttons (exclusive) ------]      [Actions] [Tab-specific]
```

**Class structure:**
```cpp
#include "ui/ToolbarButtons.h"

class Toolbar : public QWidget {
    Q_OBJECT
public:
    explicit Toolbar(QWidget *parent = nullptr);
    
    void setCurrentTool(ToolType tool);
    void setTouchGestureMode(int mode);  // 0, 1, or 2
    void updateTheme(bool darkMode);
    
signals:
    void toolSelected(ToolType tool);
    void shapeClicked();  // For now → straight line mode
    void undoClicked();
    void redoClicked();
    void touchGestureModeChanged(int mode);
    
private:
    QButtonGroup *toolGroup; // Exclusive selection for ToolButtons
    
    // Tool buttons (exclusive selection, center-aligned)
    ToolButton *penButton;
    ToolButton *markerButton;
    ToolButton *eraserButton;
    ToolButton *shapeButton;           // NEW - currently → straight line
    ToolButton *lassoButton;           // rope.png
    ToolButton *objectInsertButton;    // objectinsert.png
    ToolButton *textButton;
    
    // Action buttons (after gap)
    ActionButton *undoButton;
    ActionButton *redoButton;
    
    // Tab-specific settings
    ThreeStateButton *touchGestureButton;  // Off / Y-axis / Full
    
    ToolType currentTool;
    bool m_darkMode = false;
};
```

**Notes:**
- Shape button uses `shape.png`, connects to straight line functionality for now
- Future: Shape will have subtoolbar with shape options (line, rectangle, etc.)
- All tool buttons center-aligned with slight gap before Undo/Redo
- **Known Issue (deferred):** Shape button enables straight line mode but cannot be untoggled. Will be resolved when subtoolbars are implemented - the Shape subtoolbar will include a "freehand/none" option, or clicking another tool (Pen/Marker/Eraser) will automatically disable straight line mode.

**Checklist:**
- [x] Create header with class definition
- [x] Create cpp with constructor and button setup
- [x] Use QButtonGroup for exclusive tool selection
- [x] Undo/Redo as separate instant-action buttons
- [x] Implement `setCurrentTool()` for external sync
- [x] Implement `updateTheme()` for gray background
- [x] Add to CMakeLists.txt

---

### Task B.2: Tool Button Icons
**Status:** ✅ Complete (all icons exist)

**Icons verified (all have `_reversed` variants):**
| Button | Icon | Exists |
|--------|------|--------|
| Pen | `pen.png` | ✓ |
| Marker | `marker.png` | ✓ |
| Eraser | `eraser.png` | ✓ |
| Shape | `shape.png` | ✓ |
| Lasso | `rope.png` | ✓ |
| Object Insert | `objectinsert.png` | ✓ |
| Text | `text.png` | ✓ |
| Undo | `undo.png` | ✓ |
| Redo | `redo.png` | ✓ |
| Touch Gesture | `hand.png` | ✓ |

---

### Task B.3: Integrate Toolbar into MainWindow
**Status:** ✅ Complete

**Changes to MainWindow:**
- [x] Add `#include "ui/Toolbar.h"`
- [x] Create `Toolbar *m_toolbar` member
- [x] Add to main layout (after NavigationBar)
- [x] Connect `toolSelected` to DocumentViewport tool switching
- [x] Connect `undoClicked`/`redoClicked` to undo/redo logic
- [x] Update theme in `updateTheme()`
- [ ] Remove old tool buttons from MainWindow (deferred to Phase E cleanup)

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

## Phase C: TabBar

### Architecture Analysis

**Current State:**
```
┌─────────────────────────────────────────────────────────────────────┐
│ MainWindow                                                           │
│   mainLayout (QVBoxLayout)                                          │
│   ├── NavigationBar                                                  │
│   ├── Toolbar                                                        │
│   ├── controlBar (old toolbar - to be removed)                       │
│   └── contentWidget                                                  │
│       contentLayout (QHBoxLayout)                                    │
│       ├── leftSideContainer (sidebars)                               │
│       ├── layerPanel                                                 │
│       ├── canvasContainer                                            │
│       │   └── m_tabWidget (QTabWidget) ← TAB BAR IS HERE            │
│       │       ├── [Tab bar at top of tab widget]                     │
│       │       │   ├── Corner: openRecentNotebooksButton (left)       │
│       │       │   └── Corner: addTabButton (right)                   │
│       │       └── [Stacked DocumentViewports]                        │
│       └── markdownNotesSidebar                                       │
└─────────────────────────────────────────────────────────────────────┘
```

**Problem:** QTabWidget combines tab bar + content, making it impossible to position the tab bar between NavigationBar/Toolbar and content area (as shown in the target architecture diagrams).

**Target Layout (confirmed):**
```
┌─────────────────────────────────────────────────────────────────────┐
│ MainWindow                                                           │
│   mainLayout (QVBoxLayout)                                          │
│   ├── NavigationBar         ← Global actions, accent background     │
│   ├── TabBar (QTabBar)      ← Document tabs, accent background      │
│   ├── Toolbar               ← Tool buttons, gray background         │
│   └── contentWidget                                                  │
│       contentLayout (QHBoxLayout)                                    │
│       ├── leftSideContainer                                          │
│       ├── layerPanel                                                 │
│       ├── canvasContainer                                            │
│       │   └── viewportStack (QStackedWidget) ← VIEWPORTS ONLY       │
│       └── markdownNotesSidebar                                       │
└─────────────────────────────────────────────────────────────────────┘
```

**TabManager Current State:**
- Wraps QTabWidget (doesn't own it)
- Creates/owns DocumentViewport instances (stored in m_viewports)
- Tracks base titles and modified flags separately
- Emits: `currentViewportChanged`, `tabCloseRequested`, `tabCloseAttempted`

### Migration Options

**Option A: Keep QTabWidget, Just Restyle**
- Minimal code changes
- Tab bar remains inside canvas area
- ❌ Doesn't match target architecture

**Option B: Separate QTabBar + QStackedWidget (Recommended)**
- Create standalone QTabBar in main layout
- Use QStackedWidget in canvasContainer for viewports
- Modify TabManager to manage both separately
- ✅ Matches target architecture
- ✅ Tab bar position is flexible

**Option C: Remove TabManager, Direct Management**
- MainWindow directly manages QTabBar + viewports
- Simplifies architecture
- ❌ Increases MainWindow complexity (opposite of our goal)

### Recommended Approach (Option B)

**TabManager Changes:**
```cpp
class TabManager : public QObject {
    // Remove: QTabWidget* m_tabWidget
    // Add:
    QTabBar* m_tabBar;              // Tab strip (not owned)
    QStackedWidget* m_viewportStack; // Viewport container (not owned)
    
    // Existing members stay:
    QVector<DocumentViewport*> m_viewports;  // Owned
    QVector<QString> m_baseTitles;
    QVector<bool> m_modifiedFlags;
};
```

**MainWindow Changes:**
1. Create `QTabBar* m_tabBar` (in main layout after NavigationBar or after Toolbar)
2. Create `QStackedWidget* m_viewportStack` (in canvasContainer)
3. Update TabManager constructor: `TabManager(QTabBar*, QStackedWidget*, QObject*)`
4. Move corner widgets (launcher, add) to NavigationBar (already there!)
5. Remove old `m_tabWidget`

**Signal Connections:**
```cpp
// TabManager internally connects:
connect(m_tabBar, &QTabBar::currentChanged, m_viewportStack, &QStackedWidget::setCurrentIndex);
connect(m_tabBar, &QTabBar::currentChanged, this, &TabManager::onCurrentChanged);
connect(m_tabBar, &QTabBar::tabCloseRequested, this, &TabManager::onTabCloseRequested);
```

### Task C.1: Convert to QTabBar

---

#### Task C.1.1: Create New Widgets
**Status:** ✅ Complete

Create the replacement widgets in MainWindow:
- [x] Add `QTabBar* m_tabBar` member to MainWindow.h
- [x] Add `QStackedWidget* m_viewportStack` member to MainWindow.h
- [x] Create `m_tabBar` in setupUi (after NavigationBar, before Toolbar)
- [x] Create `m_viewportStack` in canvasContainer (created but hidden, will be added to layout in C.1.5)
- [x] Add m_tabBar to main layout (m_viewportStack stays hidden until C.1.5)
- [x] Basic theme styling for m_tabBar in updateTheme()
- [x] Verify compiles successfully

---

#### Task C.1.2: Refactor TabManager
**Status:** ✅ Complete

Update TabManager to use separate QTabBar + QStackedWidget:
- [x] Change constructor: `TabManager(QTabBar*, QStackedWidget*, QObject*)`
- [x] Replace `m_tabWidget` with `m_tabBar` and `m_viewportStack`
- [x] Update `createTab()`: add tab to QTabBar, add viewport to QStackedWidget
- [x] Update `closeTab()`: remove from both QTabBar and QStackedWidget
- [x] Update `currentViewport()`: use `m_viewportStack->currentIndex()`
- [x] Update `viewportAt()`: use `m_viewports` vector (unchanged)
- [x] Connect `QTabBar::currentChanged` → `QStackedWidget::setCurrentIndex`
- [x] Connect `QTabBar::tabCloseRequested` → `onTabCloseRequested`
- [x] Update title/modified methods to use `m_tabBar->setTabText()`
- [x] Update MainWindow to create TabManager with new parameters
- [x] Hide old m_tabWidget, use m_viewportStack in canvasLayout
- [x] Verify: compiles successfully

---

#### Task C.1.3: Apply Tab Styling
**Status:** ✅ Complete

Style the QTabBar to match design:
- [x] Tab bar background: accent color
- [x] Inactive tabs: washed out accent (darkened/desaturated for better contrast)
- [x] Active/selected tab: gray (`#f0f0f0` light / `#2d2d2d` dark)
- [x] Tab text color: theme-aware (black/white)
- [x] Set min/max tab width: 80px-200px
- [x] Enable scroll buttons: already enabled in constructor
- [x] Close button styling with `cross.png` icon
- [x] Scroll button styling with `left_arrow.png`/`right_arrow.png` icons
- [x] Hover states for tabs and buttons
- [x] Theme-aware icons (normal/_reversed variants)

---

#### Task C.1.4: Custom Close Buttons
**Status:** ✅ Complete (via QSS approach)

~~Implement close buttons on LEFT side of tab title:~~
- ~~Disable default close buttons: `m_tabBar->setTabsClosable(false)`~~
- ~~Create helper function: `createTabCloseButton(int index)`~~
- ~~Use `m_tabBar->setTabButton(index, QTabBar::LeftSide, closeButton)`~~

**Actual Implementation (simpler):**
- [x] Used QSS `subcontrol-position: left;` in `tabs.qss` / `tabs_dark.qss`
- [x] Applied minimal stylesheet in constructor BEFORE first tab is created
- [x] Close button icons theme-aware via `{{CLOSE_ICON}}` placeholder
- [x] Hover styling in QSS

**Note:** The original plan called for custom `setTabButton()` approach, but QSS `subcontrol-position` works correctly and is simpler to maintain.

---

#### Task C.1.5: Cleanup Old QTabWidget
**Status:** ✅ Complete

**Removed 43+ references to `m_tabWidget`**

**Constructor cleanup:**
- [x] Removed `m_tabWidget = new QTabWidget(this);` and configuration

**setupUi() cleanup:**
- [x] Removed `m_tabWidget` configuration
- [x] Removed `connect(m_tabWidget, &QTabWidget::currentChanged, ...)`
- [x] Removed corner widget setup: `openRecentNotebooksButton`, `addTabButton` (now in NavigationBar)
- [x] Updated `toggleTabBarButton` to use `m_tabBar`
- [x] Removed `m_tabWidget->setSizePolicy()` and `m_tabWidget->hide()`
- [x] Updated NavigationBar filename click handler to use `m_tabBar`

**switchTab() and tab switching:**
- [x] Updated `switchTab()` to use `m_tabManager->tabCount()`
- [x] Updated all `setCurrentIndex()` calls to use `m_tabBar->setCurrentIndex()`

**Fullscreen toggle:**
- [x] Updated `toggleControlBar()` to use `m_tabBar`

**updateTheme() cleanup:**
- [x] Removed old `m_tabWidget` styling block

**Other references:**
- [x] Updated container calculations (`eventFilter`, `updateScrollbarPositions`) to use `m_viewportStack`

**Header cleanup:**
- [x] Removed `QTabWidget *m_tabWidget = nullptr;`
- [x] Removed `#include <QTabWidget>` (no longer needed)
- [x] Removed `QPushButton *openRecentNotebooksButton;` (now in NavigationBar)
- [x] Removed `QPushButton *addTabButton;` (now in NavigationBar)

---

#### Task C.1.6: NavigationBar Filename Sync
**Status:** ✅ Complete

Update NavigationBar to show current document's filename:
- [x] In `currentViewportChanged` handler, call `m_navigationBar->setFilename()`
- [x] Get filename from `viewport->document()->displayName()` (returns name or "Untitled")
- [ ] Test: filename updates when switching tabs (manual verification needed)
- [ ] Test: filename shows correctly for new/loaded documents (manual verification needed)

**Implementation:**
```cpp
// In currentViewportChanged handler:
if (m_navigationBar) {
    QString filename = tr("Untitled");
    if (vp && vp->document()) {
        filename = vp->document()->displayName();
    }
    m_navigationBar->setFilename(filename);
}
```

---

#### Task C.1.7: Verification
**Status:** ✅ Complete

Full testing of tab functionality:
- [x] Tab switching works (clicks, keyboard)
- [x] Tab closing works (close button)
- [x] Tab titles display correctly
- [x] Styling matches in light and dark mode
- [x] NavigationBar filename updates on tab switch
- [x] Close button on left for all tabs (including first)
- [x] TabBar theming works correctly (accent color, hover, selected)
- [ ] No crashes on edge cases (close last tab, rapid switching)

### Decisions

1. **Tab bar position:** NavigationBar → TabBar → Toolbar ✅
   - Matches architecture diagrams
   - Tab bar sits directly below navigation, above tools

2. **Hide tab bar with single tab?** No ✅
   - Tab bar always visible regardless of tab count
   - Consistent UI, no layout shifts

3. **Priority:** Phase C is NOT low priority ✅
   - Should be tackled while architecture is fresh

4. **Tab overflow handling:**
   - QTabWidget handles scrolling automatically
   - QTabBar needs explicit scroll button configuration (use `setUsesScrollButtons(true)`)

---

### Task C.2: Extract TabBar to Separate File
**Status:** ✅ Complete

**Files created:**
- `source/ui/TabBar.h`
- `source/ui/TabBar.cpp`

**Implementation:**
- `TabBar` inherits from `QTabBar`
- Constructor configures: `setExpanding(false)`, `setMovable(true)`, `setTabsClosable(true)`, `setUsesScrollButtons(true)`, `setElideMode(Qt::ElideRight)`
- `applyInitialStyle()` sets close button position before any tabs are created (fixes first-tab bug)
- `updateTheme(bool darkMode, const QColor &accentColor)` handles all color calculations and StyleLoader calls

**Changes to MainWindow:**
- Header: `#include "ui/TabBar.h"` instead of `<QTabBar>`
- Header: `TabBar *m_tabBar` instead of `QTabBar *m_tabBar`
- Constructor: `m_tabBar = new TabBar(this)` (single line, no configuration needed)
- `updateTheme()`: Simplified to just `m_tabBar->updateTheme(darkMode, accentColor)`

**CMakeLists.txt:**
- Added `source/ui/TabBar.cpp` to NoteApp sources

**TabManager:** No changes needed - accepts `QTabBar*` base pointer, works with `TabBar*` polymorphically.

---

## Phase D: Subtoolbars (Deferred)

**Status:** Explicitly deferred until MainWindow cleanup is complete.

**Rationale:** Subtoolbars will be designed after:
1. MainWindow cleanup removes legacy toolbar code
2. Color button migration strategy is decided
3. Overall architecture is cleaner

**Includes:**
- PenSubToolbar (size, color palette)
- MarkerSubToolbar (size, color palette, opacity)
- EraserSubToolbar (size, mode)
- ShapeSubToolbar (line, rectangle, circle, etc.)
- LassoSubToolbar (cut/copy/delete)
- TextSubToolbar (font, size, color)

---

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

### Old controlBar Status

The old `controlBar` is now largely **redundant**:

**Functionality moved to new components:**
- Navigation (launcher, save, fullscreen, overflow) → NavigationBar
- Tool selection (pen, marker, eraser, lasso, etc.) → Toolbar
- Tab bar → TabBar

**Remaining in old controlBar (18 references):**
- **Color buttons:** redButton, blueButton, yellowButton, greenButton, blackButton, whiteButton, customColorButton
- toggleTabBarButton (functionality duplicated in NavigationBar sidebar toggle?)
- Some layout code (createSingleRowLayout, createDoubleRowLayout)

**Color Buttons Decision:**
The color buttons need to be migrated. Options for future:
1. **ColorPalette subtoolbar** - appears when pen/marker selected (Phase D)
2. **Move to left sidebar** - color palette panel
3. **Keep in controlBar temporarily** - until subtoolbar system is implemented

For now, color buttons remain in old controlBar. They will be addressed when either:
- Phase D (Subtoolbars) is implemented
- Or during MainWindow cleanup if a simpler solution is chosen

---

### Task E.1: Remove Old Toolbar Code from MainWindow
**Status:** ⬜ Not Started

After all phases verified:
- [ ] Delete old button creation code (tool buttons already handled by Toolbar)
- [ ] Delete old layout code (single/double row)
- [ ] Delete old style application code
- [ ] Delete unused member variables
- [ ] Update any remaining signal connections
- [ ] Decide on color button migration strategy

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
| 0 | Button Types | ✅ Complete | 4/4 |
| A | NavigationBar | ✅ Complete | 4/4 |
| B | Toolbar | 🔄 In Progress | 3/4 |
| C | TabBar | ✅ Complete | 8/8 |
| D | Subtoolbars | ⬜ Deferred | 0/6 |
| E | Cleanup | ⬜ Not Started | 0/3 |

**Overall:** 19/29 tasks complete (excluding deferred Phase D)

**Phase C subtask status:**
- C.1.1: ✅ Create New Widgets
- C.1.2: ✅ Refactor TabManager
- C.1.3: ✅ Apply Tab Styling
- C.1.4: ✅ Custom Close Buttons (via QSS)
- C.1.5: ✅ Cleanup Old QTabWidget (43+ refs removed)
- C.1.6: ✅ NavigationBar Filename Sync
- C.1.7: ✅ Verification (tab bar works perfectly)
- C.2: ✅ Extract TabBar to Separate File

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



## Design Notes

### UI Hierarchy
| Layer | Scope | Location |
|-------|-------|----------|
| NavigationBar | Global/App-wide | Top of window |
| Toolbar | Tab/Document-specific | Below tab bar |
| Subtoolbars | Tool-specific | Left side, floating |

### Left Sidebar Container
Contains internal switches for:
- PDF Outline
- Bookmarks  
- Page Preview / Jump to Page
- Layer Panel

One toggle on NavigationBar shows/hides the entire container.

### Default Accent Color
`#2277CC` - can be customized via Control Panel in future. 

---

## Reference: Theme Interface

### QSettings Keys (Organization: "SpeedyNote", App: "App")

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `useCustomAccentColor` | bool | `false` | If true, use `customAccentColor`; otherwise use system highlight |
| `customAccentColor` | QString | `"#0078D4"` | User's custom accent color (hex format) |
| `useBrighterPalette` | bool | `false` | Whether to use brighter palette variant |

### MainWindow Theme Methods

```cpp
// Get current accent color (returns custom or system highlight)
QColor getAccentColor() const;

// Set custom accent color (saves to QSettings, triggers updateTheme if enabled)
void setCustomAccentColor(const QColor &color);

// Enable/disable custom accent color (triggers updateTheme, saves to QSettings)
void setUseCustomAccentColor(bool use);

// Check if dark mode is active
bool isDarkMode() const;

// Apply theme to all components
void updateTheme();

// Save/load theme settings to/from QSettings
void saveThemeSettings();
void loadThemeSettings();
```

### Component Theme Update Pattern

```cpp
void MainWindow::updateTheme() {
    QColor accentColor = getAccentColor();
    bool darkMode = isDarkMode();
    
    // NavigationBar: accent background + icon theme
    if (m_navigationBar) {
        m_navigationBar->updateTheme(darkMode, accentColor);
    }
    
    // Toolbar: gray background + icon theme
    if (m_toolbar) {
        m_toolbar->updateTheme(darkMode);
    }
    
    // ... other components
}
```

### NavigationBar Theme Interface

```cpp
// Updates background color and icon themes
void NavigationBar::updateTheme(bool darkMode, const QColor &accentColor);
```

**Implementation:** Uses `QPalette` with `setAutoFillBackground(true)` for reliable background coloring.

### Toolbar Theme Interface

```cpp
// Updates background (gray) and icon themes
void Toolbar::updateTheme(bool darkMode);
```

**Background:** Light mode = `#f0f0f0`, Dark mode = `#2d2d2d`

### Load Order

1. `MainWindow()` constructor calls `setupUi()` - creates NavigationBar/Toolbar with defaults
2. `MainWindow()` calls `loadUserSettings()` → `loadThemeSettings()` → `updateTheme()`
3. Theme is applied to all components with loaded QSettings values

**Note:** Components are created with hardcoded defaults, then immediately updated with user settings. This is intentional to avoid blocking the constructor on settings load.

### Styling Troubleshooting / Fixes

#### NavigationBar Background Color Issue (Fixed)

**Problem:** NavigationBar was not displaying the custom accent color loaded from QSettings. Instead, it showed the KDE Plasma default system color (`#2a2e32`) while the TabBar correctly displayed the custom accent (`#343a46`).

**Root Cause:** Using `QPalette` to set the NavigationBar background color did not work reliably. Despite setting `setAutoFillBackground(true)` and `QPalette::Window`, the palette was being overridden by the desktop environment's theme.

**Solution:** Use `setStyleSheet()` with `setObjectName()` to apply the background color:

```cpp
void NavigationBar::updateTheme(bool darkMode, const QColor &accentColor) {
    // ... icon updates ...
    
    // Use setObjectName + stylesheet for reliable background color
    setObjectName("NavigationBar");
    setStyleSheet(QString("#NavigationBar { background-color: %1; }").arg(accentColor.name()));
}
```

**Why This Works:**
- `setStyleSheet()` with an ID selector (`#ObjectName`) has higher specificity than palette-based styling
- It bypasses the Qt platform theme's palette overrides
- The TabBar was already using stylesheets (via `QTabBar::tab { ... }`), which is why it worked correctly

**Lesson Learned:** When setting background colors on custom widgets that need to match other styled components (like QTabBar), prefer `setStyleSheet()` over `QPalette` for consistency and reliability across different desktop environments.

#### QTabBar Close Button Position Issue (Fixed)

**Problem:** The first tab created on application launch had its close button on the RIGHT side, while tabs created later had close buttons correctly positioned on the LEFT.

**Root Cause:** The sequence of operations was:
1. `m_tabBar` is created with `setTabsClosable(true)` (default close button on right)
2. First tab is created via `addNewTab()` inside `setupUi()`
3. `updateTheme()` is called AFTER `setupUi()`, applying `subcontrol-position: left;`

Qt creates the close button widget when a tab is added. If no stylesheet specifying `subcontrol-position` exists at that moment, Qt uses the default right-side position. Applying the stylesheet later does NOT reposition existing close buttons.

**Solution:** Apply a minimal stylesheet with close button positioning BEFORE the first tab is created:

```cpp
// In constructor, immediately after m_tabBar configuration:
m_tabBar->setStyleSheet(R"(
    QTabBar::close-button {
        subcontrol-position: left;
    }
)");
```

This ensures ALL tabs (including the first) have close buttons positioned on the left. The full styling (colors, hover effects, etc.) is applied later in `updateTheme()` and will extend/override this initial stylesheet.

**Lesson Learned:** For QTabBar styling that affects widget positioning (like `subcontrol-position`), the stylesheet must be applied BEFORE any tabs are added. Styling properties that only affect appearance (colors, padding) can be applied later.


### StyleLoader - External QSS with Placeholders

**Files created:**
- `source/ui/StyleLoader.h/cpp` - Helper class for loading QSS with placeholder substitution
- `resources/styles/tabs.qss` - Light mode tab styling
- `resources/styles/tabs_dark.qss` - Dark mode tab styling

**Placeholder syntax:** `{{PLACEHOLDER_NAME}}`

**Available placeholders for tabs:**
| Placeholder | Description | Example Value |
|-------------|-------------|---------------|
| `{{TAB_BAR_BG}}` | Tab bar background (accent) | `#343a46` |
| `{{TAB_BG}}` | Inactive tab background (washed) | `#2a2d33` |
| `{{TAB_TEXT}}` | Tab text color | `#ffffff` |
| `{{TAB_SELECTED_BG}}` | Selected tab background (system) | `#2d2d2d` |
| `{{TAB_HOVER_BG}}` | Tab hover background | `#3a3d44` |
| `{{CLOSE_ICON}}` | Close button icon filename | `cross_reversed.png` |
| `{{RIGHT_ARROW}}` | Right scroll arrow icon | `right_arrow_reversed.png` |
| `{{LEFT_ARROW}}` | Left scroll arrow icon | `left_arrow_reversed.png` |

**Usage in MainWindow:**
```cpp
#include "ui/StyleLoader.h"

// In updateTheme():
QString tabStylesheet = StyleLoader::loadTabStylesheet(
    darkMode,
    accentColor,    // Tab bar background
    washedColor,    // Inactive tab background
    textColor,      // Text color
    selectedBg,     // Selected tab background
    hoverColor      // Hover background
);
m_tabBar->setStyleSheet(tabStylesheet);
```

**Benefits:**
- Styling separated from code (easier to modify)
- Consistent placeholder pattern for all QSS files
- Light/dark mode variants in separate files
- Dynamic colors calculated at runtime, structural styling in QSS

---

### Tab Bar Styling (Confirmed)

| Element | Light Mode | Dark Mode |
|---------|------------|-----------|
| Tab bar background | Accent color | Accent color |
| Inactive tabs | Washed out accent (lighter/desaturated) | Washed out accent |
| Active/selected tab | Light gray (`#f0f0f0`) | Dark gray (`#2d2d2d`) |
| Tab text | Black on light, White on dark | Context-dependent |
| Close button icon | `cross.png` / `cross_reversed.png` | Theme-aware |
| Close button position | LEFT of tab title | (custom, not Qt default) |

**Tab Size Constraints:**
- Minimum width: ~80px (enough for short filename + close button)
- Maximum width: ~200px (prevents single tab from taking entire bar)
- Elide long names in the middle (preserve extension)

**Overflow Handling:**
- Yes, overflow menu on far right is possible! QTabBar supports `setUsesScrollButtons(true)` for scroll arrows, but a custom overflow dropdown would require subclassing QTabBar or adding a separate button
- Recommendation: Start with scroll buttons, consider overflow menu as enhancement

**Close Button Implementation:**
- QTabBar's default close button is on the RIGHT side
- To put it on the LEFT, we need `QTabBar::setTabButton(index, QTabBar::LeftSide, closeButton)`
- This means creating custom close buttons per tab (not a new button type, just ActionButton with cross icon, 16x16 size)

### Tab Switch Actions (Analysis)

**Question:** Upon switching tabs, what actions need to happen?

**Current Implementation (`MainWindow::switchTab` + `TabManager::currentViewportChanged`):**

Already implemented:
- ✅ `connectViewportScrollSignals(vp)` - scroll sync
- ✅ `updateDialDisplay()` - dial UI sync (now mostly removed)
- ✅ `updateLayerPanelForViewport(vp)` - layer panel sync
- ✅ `m_debugOverlay->setViewport(vp)` - debug overlay sync

Stubbed/TODO (in `switchTab`):
- ⏳ Update page spinbox from viewport
- ⏳ Update zoom slider from viewport
- ⏳ `updateColorButtonStates()` - color button highlights
- ⏳ `updateStraightLineButtonState()` - straight line toggle
- ⏳ `updateRopeToolButtonState()` - lasso tool toggle
- ⏳ `updateToolButtonStates()` - pen/marker/eraser selection
- ⏳ `updateThicknessSliderForCurrentTool()` - thickness UI

**Recommendation:** Most of these are already stubbed and ready for connection. With the new Toolbar:
- `Toolbar::setCurrentTool()` can be called on tab switch to sync tool buttons
- Tool state should be per-document (stored in Document or DocumentViewport)
- For now: Plan to connect in Phase C, but defer full per-tab tool state until later

### NavigationBar Filename Update (Task for Phase C)

**Current state:** `m_navigationBar->setFilename(tr("Untitled"))` only called once at startup.

**Required:** Update filename when:
1. Tab switches → show current document's filename
2. Document saved/renamed → update displayed name
3. Document modified → could show asterisk (but TabManager already handles this for tabs)

**Implementation in Phase C:**
```cpp
// In switchTab or currentViewportChanged handler:
if (m_navigationBar && viewport && viewport->document()) {
    QString filename = viewport->document()->filename();
    if (filename.isEmpty()) filename = tr("Untitled");
    m_navigationBar->setFilename(filename);
}
```

This should be added to Task C.1 checklist.