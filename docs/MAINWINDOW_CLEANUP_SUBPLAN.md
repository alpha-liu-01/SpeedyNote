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

### MW1.3: Delete Prev/Next Page Buttons

Prev/next buttons are already hidden. Only the spinbox is kept.

**Files:** `source/MainWindow.cpp`, `source/MainWindow.h`

**Delete:**
1. `prevPageButton`, `nextPageButton` declarations
2. Button creation in constructor
3. `switchPageWithDirection()` method (complex direction tracking)
4. Related signal connections
5. Style updates in `updateTheme()`

**Keep:**
- `pageInput` spinbox (stub for future reconnection)
- `onPageInputChanged()` slot (stub it if needed)

**Search patterns:**
```
prevPageButton
nextPageButton
switchPageWithDirection
```

**Verification:**
- [ ] `./compile.sh` succeeds
- [ ] Page spinbox still visible (even if non-functional)

---

### MW1.4: Delete Old InkCanvas References

Any remaining references to the old InkCanvas system.

**Files:** `source/MainWindow.cpp`

**Search for and remove:**
```
InkCanvas::
m_inkCanvas
inkCanvas
canvasWidget
```

**Note:** Be careful to distinguish between:
- Old InkCanvas (delete)
- New DocumentViewport (keep)

**Verification:**
- [ ] `./compile.sh` succeeds
- [ ] No InkCanvas headers included

---

### MW1.5: Delete Unused Method Stubs

Methods that are stubbed and will never be implemented.

**Files:** `source/MainWindow.cpp`, `source/MainWindow.h`

**Review and delete if completely unused:**
- Empty slot implementations with just `// TODO: stub`
- Methods that only reference deleted features

**Verification:**
- [ ] `./compile.sh` succeeds
- [ ] Check for any signal-slot disconnections

---

## Phase 2: Extract Dial Controller Module

**Goal:** Create modular dial system that can be excluded on Android builds.  
**Estimated savings:** ~800 lines moved out of MainWindow  
**Risk:** Medium (requires careful signal/slot restructuring)

### MW2.1: Create DialController Directory Structure

**Create:**
```
source/input/
├── DialController.h
├── DialController.cpp
├── DialModeToolbar.h
├── DialModeToolbar.cpp
├── MouseDialHandler.h
├── MouseDialHandler.cpp
├── CMakeLists.txt
```

**CMakeLists.txt content:**
```cmake
# source/input/CMakeLists.txt

option(ENABLE_DIAL_CONTROLLER "Enable MagicDial support (desktop only)" ON)

if(ANDROID)
    set(ENABLE_DIAL_CONTROLLER OFF CACHE BOOL "" FORCE)
endif()

if(ENABLE_DIAL_CONTROLLER)
    target_sources(SpeedyNote PRIVATE
        DialController.cpp
        DialModeToolbar.cpp
        MouseDialHandler.cpp
    )
    target_compile_definitions(SpeedyNote PRIVATE ENABLE_DIAL_CONTROLLER)
endif()
```

---

### MW2.2: Extract MagicDial Widget

The OLED-style circular controller widget.

**From MainWindow, extract:**
- Dial widget creation and styling
- `handleDialInput()` - main dial rotation handler
- `handleToolSelection()` - tool selection mode
- `handleDialZoom()` - zoom mode
- `handleDialPanScroll()` - pan/scroll mode
- `handleDialThickness()` - thickness mode
- `handlePresetSelection()` - preset selection mode
- `changeDialMode()` - mode switching
- Dial mode enum and state

**Into `DialController.cpp`:**
```cpp
class DialController : public QWidget {
    Q_OBJECT
public:
    explicit DialController(QWidget *parent = nullptr);
    
    // Mode management
    void setMode(DialMode mode);
    DialMode currentMode() const;
    
signals:
    // Emit actions that MainWindow connects to
    void toolChangeRequested(ToolType tool);
    void zoomChangeRequested(int delta);
    void panScrollRequested(int delta);
    void thicknessChangeRequested(int value);
    void colorPresetSelected(int index);
    
private slots:
    void handleDialInput(int angle);
    
private:
    // All dial state and widgets
};
```

---

### MW2.3: Extract Dial Mode Toolbar

The vertical strip with mode selection buttons.

**From MainWindow, extract:**
- Mode toolbar widget and buttons
- Mode button styling
- Mode switching logic

**Into `DialModeToolbar.cpp`:**
```cpp
class DialModeToolbar : public QWidget {
    Q_OBJECT
public:
    explicit DialModeToolbar(DialController *controller, QWidget *parent = nullptr);
    
signals:
    void modeChangeRequested(DialMode mode);
    
private:
    QPushButton *m_toolButton;
    QPushButton *m_zoomButton;
    QPushButton *m_panButton;
    QPushButton *m_thicknessButton;
    QPushButton *m_presetButton;
};
```

---

### MW2.4: Extract Mouse Dial Handler

Mouse button + scroll wheel → dial control.

**From MainWindow, extract:**
- `handleMouseWheelDial()` method
- Mouse dial state tracking
- Related event handling

**Into `MouseDialHandler.cpp`:**
```cpp
class MouseDialHandler : public QObject {
    Q_OBJECT
public:
    explicit MouseDialHandler(DialController *controller, QObject *parent = nullptr);
    
    bool handleWheelEvent(QWheelEvent *event);
    bool handleMousePress(QMouseEvent *event);
    bool handleMouseRelease(QMouseEvent *event);
    
private:
    DialController *m_dialController;
    bool m_leftHeld = false;
    bool m_rightHeld = false;
};
```

---

### MW2.5: Move SDLControllerManager into Dial Module

**Current:** `source/SDLControllerManager.cpp/h`  
**Move to:** `source/input/SDLControllerHandler.cpp/h`

**Changes:**
- Rename to `SDLControllerHandler` for consistency
- Connect to `DialController` instead of directly to MainWindow
- Include in conditional compilation

**Update CMakeLists.txt:**
```cmake
if(ENABLE_DIAL_CONTROLLER)
    find_package(SDL2 QUIET)
    if(SDL2_FOUND)
        target_sources(SpeedyNote PRIVATE SDLControllerHandler.cpp)
        target_link_libraries(SpeedyNote PRIVATE SDL2::SDL2)
    endif()
endif()
```

---

### MW2.6: Update MainWindow to Use DialController

**MainWindow.cpp changes:**
```cpp
#ifdef ENABLE_DIAL_CONTROLLER
#include "input/DialController.h"
#include "input/DialModeToolbar.h"
#include "input/MouseDialHandler.h"
#endif

// In constructor:
#ifdef ENABLE_DIAL_CONTROLLER
    m_dialController = new DialController(this);
    m_dialModeToolbar = new DialModeToolbar(m_dialController, this);
    m_mouseDialHandler = new MouseDialHandler(m_dialController, this);
    
    connect(m_dialController, &DialController::toolChangeRequested,
            this, &MainWindow::changeTool);
    connect(m_dialController, &DialController::zoomChangeRequested,
            this, &MainWindow::handleZoomDelta);
    // ... other connections
#endif

// In wheelEvent:
#ifdef ENABLE_DIAL_CONTROLLER
    if (m_mouseDialHandler->handleWheelEvent(event)) {
        return;
    }
#endif
```

---

### MW2.7: Verification

**Compile tests:**
- [ ] `./compile.sh` succeeds with `ENABLE_DIAL_CONTROLLER=ON`
- [ ] `./compile.sh` succeeds with `ENABLE_DIAL_CONTROLLER=OFF`
- [ ] `cmake -DANDROID=ON` excludes dial code

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
- [ ] MW1.2: Delete background selection
- [ ] MW1.3: Delete prev/next page buttons
- [ ] MW1.4: Delete InkCanvas references
- [ ] MW1.5: Delete unused stubs
- [ ] Compile and test

### Phase 2: Extract Dial Controller
- [ ] MW2.1: Create directory structure
- [ ] MW2.2: Extract MagicDial widget
- [ ] MW2.3: Extract Dial Mode Toolbar
- [ ] MW2.4: Extract Mouse Dial Handler
- [ ] MW2.5: Move SDLControllerManager
- [ ] MW2.6: Update MainWindow
- [ ] MW2.7: Verify both compile modes

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

