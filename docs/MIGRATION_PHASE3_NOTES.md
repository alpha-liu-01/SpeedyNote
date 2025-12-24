# Phase 3 Migration Notes

> **Purpose:** Track decisions and current state during Phase 3 implementation.
> **Last Updated:** Dec 24, 2024
> 
> **Progress:** Phase 3.0-3.2 ✅ COMPLETE | Phase 3.3 🔄 IN PROGRESS (~60%)

---

## Current State Summary

### Runtime State (Dec 23, 2024)

**App launches successfully!** Current behavior:

| Feature | Status | Notes |
|---------|--------|-------|
| App Launch | ✅ Works | No crash on startup (after fixing `updatePanRange`, `updateDialDisplay`) |
| Toolbar | ✅ Visible | Old toolbar displays correctly |
| Tab Bar | ✅ Works | Single `m_tabWidget` tab bar with corner buttons (back + add tab) |
| Add Tab Button | ✅ Works | Creates new tabs with DocumentViewport |
| DocumentViewport | ✅ Works | Same behavior as `--test-viewport` window |
| Color Buttons | ✅ Fixed | Null checks added to prevent crashes |
| Tool Buttons | ✅ Fixed | Null checks added to prevent crashes |
| PDF/Bookmark Sidebars | ✅ Toggle works | Empty content (expected) |
| MagicDial Display | ✅ Correct | Shows "No Canvas" text |

**Tab Bar Consolidation:** Old `tabBarContainer` hidden, buttons moved to `m_tabWidget` corner widgets.

### Completed Tasks

| Task | Status | Notes |
|------|--------|-------|
| 3.0.1 DocumentManager | ✅ Complete | `source/core/DocumentManager.h/.cpp` |
| 3.0.2 TabManager | ✅ Complete | `source/ui/TabManager.h/.cpp` |
| 3.0.3 LayerPanel | ✅ Complete | `source/ui/LayerPanel.h/.cpp` |
| 3.0.4 Command line flag | ✅ REMOVED | `--use-new-viewport` removed in 3.1.1 |
| 3.1.3 Remove VectorCanvas | ✅ Complete | Removed from MainWindow, NOT from CMakeLists yet |
| 3.1.1 Replace Tab System | ✅ Complete | tabList, canvasStack, pageMap removed; m_tabWidget + TabManager in place |
| 3.1.2 addNewTab stubbed | ✅ Complete | Old InkCanvas code wrapped in comment block |
| 3.1.6 Page Nav stubbed | ✅ Complete | switchPage, switchPageWithDirection stubbed |
| LauncherWindow | ⏸️ Disconnected | Commented out from CMakeLists.txt, sharedLauncher refs commented |
| ControlPanelDialog | ⏸️ Disconnected | Commented out from CMakeLists.txt (source/ControlPanelDialog.cpp) |
| Touch/Palm Rejection | ⏸️ Stubbed | onStylusProximityEnter, restoreTouchGestureMode stubbed |
| updateTabSizes | ⏸️ Stubbed | QTabWidget handles its own sizing |
| updateTheme tabList | ⏸️ Stubbed | Will use m_tabWidget styling in Phase 3.3 |
| updatePanRange | ⏸️ Stubbed | DocumentViewport handles own pan/zoom |
| updateDialDisplay | ⏸️ Protected | Returns early if no currentCanvas() |
| Button Handlers | ✅ Fixed | Null checks added to color/tool/thickness buttons |
| Tab Bar Consolidation | ✅ Complete | tabBarContainer hidden, buttons as m_tabWidget corner widgets |
| 3.1.8 ControlPanelDialog | ✅ Disabled | Replaced with QMessageBox, include commented out |
| 3.1.8b InkCanvas Cleanup | ✅ Complete | All InkCanvas calls wrapped in #if 0 blocks or stubbed |
| 3.1.9 Markdown Handlers | ✅ Complete | All markdown/highlight handlers stubbed |

### Type Migrations (Phase 3.1.8)

| Old Type | New Type | Location |
|----------|----------|----------|
| `BackgroundStyle` (from InkCanvas.h) | `Page::BackgroundType` (from Page.h) | `saveDefaultBackgroundSettings()`, `loadDefaultBackgroundSettings()` |
| `vp->setZoom()` | `vp->setZoomLevel()` | `updateZoom()` |
| `viewport->currentPage()` | `viewport->currentPageIndex()` | `toggleOutlineSidebar()` |

**Note:** `Page::BackgroundType` has additional values (`PDF`, `Custom`) not in the old `BackgroundStyle`. The QSettings key remains `defaultBackgroundStyle` but now stores `Page::BackgroundType` integer values.

### Disabled Functions (Phase 3.1.8)

The following functions have been stubbed/disabled using `#if 0` blocks:
- `exportAnnotatedPdf()` - PDF export will be reimplemented for DocumentViewport
- `exportCanvasOnlyNotebook()` - Canvas export disabled
- `exportAnnotatedPdfFullRender()` - Full render export disabled  
- `createAnnotatedPagesPdf()` - PDF creation disabled
- `mergePdfWithPdftk()` - PDF merge disabled
- `enableStylusButtonMode()` - Stylus modes will use DocumentViewport
- `disableStylusButtonMode()` - Stylus modes disabled
- `onPdfTextSelectionCleared()` - Text selection disabled
- `openPdfFile()` - PDF file association disabled
- `openSpnPackage()` - .spn format being replaced with .snx
- `createNewSpnPackage()` - .spn creation disabled
- `keyPressEvent/keyReleaseEvent` - Ctrl tracking stubbed
- `toggleControlBar` - Canvas size management stubbed
- `getPdfDocument()` - Returns nullptr (will use DocumentViewport)
- `eventFilter()` - InkCanvas event handling replaced with DocumentViewport

### Stubbed Markdown Handlers (Phase 3.1.9)

The following markdown/highlight handlers are stubbed (will be reimplemented in Phase 3.4):
- `onMarkdownNotesUpdated()` - Was triggered by InkCanvas signal
- `onMarkdownNoteContentChanged()` - Note content sync
- `onMarkdownNoteDeleted()` - Note deletion
- `onHighlightLinkClicked()` - Navigate to highlight
- `onHighlightDoubleClicked()` - Edit highlight note
- `loadMarkdownNotesForCurrentPage()` - Loads notes for current page

### Reference Files

- `source/MainWindow_OLD.cpp` - Original MainWindow before Phase 3
- `source/MainWindow_OLD.h` - Original header before Phase 3

---

## Key Decisions

### Architecture

1. **Tab System:** Completely replace `tabList` (QListWidget) + `canvasStack` (QStackedWidget) + `pageMap` with `QTabWidget` via `TabManager`

2. **TabManager Receives QTabWidget:** MainWindow creates QTabWidget, passes pointer to TabManager. TabManager does NOT own QTabWidget - just manages tab operations. (CORRECTED: Previous note was wrong)

3. **LayerPanel ↔ Page:** LayerPanel talks DIRECTLY to Page, NOT through DocumentViewport

4. **Tab Bar Position:** Tab bar stays at the very top of MainWindow (already in place), with back button and add tab button on each side

5. **Ownership:**
   - DocumentManager OWNS → Document
   - MainWindow OWNS → QTabWidget (creates it)
   - TabManager RECEIVES → QTabWidget pointer (does NOT own)
   - TabManager OWNS → DocumentViewport widgets (creates them)
   - DocumentViewport REFERENCES → Document (pointer, not ownership)
   - LayerPanel REFERENCES → Page (pointer, updated when page/tab changes)

### File Format

1. **Initial Format:** JSON file containing:
   - Document metadata (per MIGRATION_PHASE1_2_SUBPLAN.md lines 230-253)
   - Strokes from each VectorLayer on each Page

2. **Future Format:** QDataStream-based package (`.snx` extension) containing:
   - Base JSON metadata
   - Inserted pictures, text, and other embedded objects

3. **Extension:** `.snx` (SpeedyNote eXtended) - NOT compatible with old `.spn`

4. **Old .spn Files:** NOT supported - breaking change is acceptable

### Feature Flags

- `--use-new-viewport` flag exists but is currently unused ("useless")
- Can be ignored for now; may be removed or repurposed later

### Backwards Compatibility

- No concern about existing users
- Clean break from old format

---

## Phase 3.3: Pan/Zoom/Scroll Control System

### Key Principle: No Inter-Control Dependencies

**CRITICAL:** All UI controls (sliders, buttons, dial, etc.) must call DocumentViewport methods directly. Controls must NEVER rely on each other.

```
┌─────────────────────────────────────────────────────────────┐
│                    CONTROL PATTERN                          │
│                                                             │
│  UI Widget ──────────► DocumentViewport.setXXX()            │
│      ▲                                                      │
│      │                                                      │
│  DocumentViewport.xxxChanged() signal ──────────────────────┘
│                                                             │
│  NO control should call another control's method!           │
│  All controls read/write to DocumentViewport directly.      │
└─────────────────────────────────────────────────────────────┘
```

### DocumentViewport Pan/Scroll Interface

| Method | Purpose | Units |
|--------|---------|-------|
| `setPanOffset(QPointF)` | Set pan position directly | Document pixels |
| `scrollBy(QPointF delta)` | Scroll by delta amount | Document pixels |
| `setHorizontalScrollFraction(qreal)` | Set horizontal scroll | 0.0-1.0 fraction |
| `setVerticalScrollFraction(qreal)` | Set vertical scroll | 0.0-1.0 fraction |
| `setZoomLevel(qreal)` | Set zoom level | 1.0 = 100% |
| `scrollToPage(int)` | Jump to specific page | Page index |

| Signal | Purpose | Value |
|--------|---------|-------|
| `panChanged(QPointF)` | Pan offset changed | Document pixels |
| `horizontalScrollChanged(qreal)` | H-scroll fraction changed | 0.0-1.0 |
| `verticalScrollChanged(qreal)` | V-scroll fraction changed | 0.0-1.0 |
| `zoomChanged(qreal)` | Zoom level changed | 1.0 = 100% |
| `currentPageChanged(int)` | Visible page changed | Page index |

### Connection Flow (Mouse Wheel / Touch / Sliders)

```
Mouse Wheel                          Scroll Bar Slider
     │                                      │
     ▼                                      ▼
scrollBy(delta)                 setVerticalScrollFraction(frac)
     │                                      │
     └──────────► m_panOffset ◄─────────────┘
                       │
                       ▼
              clampPanOffset()
                       │
                       ▼
            emitScrollFractions()
                       │
                       ▼
            verticalScrollChanged(fraction)
                       │
                       ▼
              (MainWindow updates slider position)
```

### Slider Precision

For a 1000-page document (~1,000,000px content):
- Slider range 0-100: each step = 10,000px = 10 pages ❌
- Slider range 0-1000: each step = 1,000px = 1 page ❌
- Slider range 0-10000+: each step = ~100px ✅

**Decision:** Use high-resolution sliders (0-10000 or higher), convert to fraction:
```cpp
// Slider → DocumentViewport
qreal fraction = slider->value() / 10000.0;
viewport->setVerticalScrollFraction(fraction);

// DocumentViewport → Slider (via signal)
connect(viewport, &DocumentViewport::verticalScrollChanged, [slider](qreal fraction) {
    slider->blockSignals(true);
    slider->setValue(qRound(fraction * 10000));
    slider->blockSignals(false);
});
```

### Pan Overshoot (Intentional Feature)

DocumentViewport allows 50% viewport overscroll at document edges:

```cpp
// In clampPanOffset():
qreal overscrollY = viewHeight * 0.5;  // 50% of viewport
qreal minY = -overscrollY;
qreal maxY = contentHeight - viewHeight + overscrollY;
```

**Example (700px viewport, 5360px content):**
- `minY = -350` (can scroll 350px before first page)
- `maxY = 5010` (can scroll 350px past last page)
- Total pan range: -350 to 5010 ✅

### Scroll Input Types

| Input | Method | Precision | Use Case |
|-------|--------|-----------|----------|
| Mouse wheel | `scrollBy()` | Document pixels | Fine scrolling |
| Touch gesture | `scrollBy()` | Document pixels | Smooth scrolling (Phase 4) |
| Scroll bar slider | `setVerticalScrollFraction()` | Fraction 0-1 | Rough navigation |
| Page navigation | `scrollToPage()` | Page index | Jump to page |

### Touch Gesture Scrolling (Phase 4)

Touch gestures will use `scrollBy()` directly with touch deltas:
- High frequency updates (60-120Hz)
- Momentum/inertia after finger lift
- Same `m_panOffset` state as other inputs

### DocumentViewport Content Centering

**IMPLEMENTED (Task 3.3.3):** Content is centered horizontally via initial negative pan X.

**Problem:** When content (pages) is narrower than the viewport, it appears left-aligned because pan X = 0 means document x=0 is at viewport x=0 (left edge).

**Solution:** MainWindow sets an initial negative pan X value when creating new tabs.

```cpp
// In MainWindow::centerViewportContent(int tabIndex):
qreal centeringOffset = (viewportWidth - contentSize.width()) / 2.0;
viewport->setPanOffset(QPointF(-centeringOffset, currentPan.y()));
```

**Key design decisions:**
1. **One-time calculation** - Only done when tab is created, not on every resize/zoom
2. **MainWindow's responsibility** - DocumentViewport is NOT modified
3. **Negative pan X** - The debug overlay correctly shows negative pan X values
4. **User can pan freely** - After initial centering, user scrolls as normal
5. **Layout flexibility preserved** - Pan X = 0 still means "left edge of content"

**Why NOT Option D (rendering offset in DocumentViewport):**
- Breaks zoom behavior (offset must be recalculated at each zoom level)
- Complicates coordinate transforms
- Makes horizontal scrolling layouts impossible
- Violates the "pan offset is the source of truth" principle

### Old Pan Sliders - Detailed Analysis

**Widget Type:** `QScrollBar` (not QSlider)

**Declarations:**
```cpp
// MainWindow.h:606-607
QScrollBar *panXSlider;
QScrollBar *panYSlider;
```

**Positioning:**
- Both are children of `canvasContainer` (overlay on top of content)
- `panXSlider`: Positioned at **TOP** of container (horizontal scrollbar)
- `panYSlider`: Positioned at **LEFT** of container (vertical scrollbar)
- Both have 3px margin and 15px corner offset to avoid overlap

```cpp
// updateScrollbarPositions():
panXSlider->setGeometry(
    cornerOffset + margin,  // Leave space at left corner
    margin,                 // At top
    containerWidth - cornerOffset - margin*2,
    scrollbarHeight
);

panYSlider->setGeometry(
    margin,                 // At left
    cornerOffset + margin,  // Leave space at top corner  
    scrollbarWidth,
    containerHeight - cornerOffset - margin*2
);
```

**Styling (shared):**
```css
QScrollBar {
    background: rgba(200, 200, 200, 80);
    border: none;
}
QScrollBar::handle {
    background: rgba(100, 100, 100, 150);
    border-radius: 2px;
    min-height: 120px;
    min-width: 120px;
}
/* Scroll buttons hidden, page areas transparent */
```

**Dimensions:**
- `panXSlider`: Fixed height 16px
- `panYSlider`: Fixed width 16px

**Current State (Phase 3.1):**
```cpp
// panYSlider PERMANENTLY HIDDEN (hack):
panYSlider->setVisible(false);
panYSlider->setEnabled(false);

// Auto-hide timer only shows panXSlider:
scrollbarHideTimer->connect([this]() {
    panXSlider->setVisible(false);
    // panYSlider stays hidden permanently
});
```

**Auto-Hide Behavior:**
- 200ms timer after mouse leaves scrollbar
- Enter event stops timer (keeps visible)
- Leave event starts timer

**Current Stubbed Handlers:**
```cpp
void MainWindow::updatePanX(int value) {
    Q_UNUSED(value);  // Stubbed
}
void MainWindow::updatePanY(int value) {
    Q_UNUSED(value);  // Stubbed
}
void MainWindow::updatePanRange() {
    // Sets both to range(0,0) and hides them
}
```

**Phase 3.3 Changes Required:**

1. **Unhide panYSlider:**
   - Remove `panYSlider->setVisible(false)` and `setEnabled(false)` from setup
   - Add panYSlider to auto-hide timer alongside panXSlider

2. **Connect to DocumentViewport:**
   ```cpp
   // Slider → Viewport
   connect(panXSlider, &QScrollBar::valueChanged, [this](int value) {
       if (auto* vp = currentViewport()) {
           vp->setHorizontalScrollFraction(value / 10000.0);
       }
   });
   connect(panYSlider, &QScrollBar::valueChanged, [this](int value) {
       if (auto* vp = currentViewport()) {
           vp->setVerticalScrollFraction(value / 10000.0);
       }
   });
   
   // Viewport → Slider (in connectViewportSignals)
   connect(viewport, &DocumentViewport::horizontalScrollChanged, [this](qreal frac) {
       panXSlider->blockSignals(true);
       panXSlider->setValue(qRound(frac * 10000));
       panXSlider->blockSignals(false);
   });
   connect(viewport, &DocumentViewport::verticalScrollChanged, [this](qreal frac) {
       panYSlider->blockSignals(true);
       panYSlider->setValue(qRound(frac * 10000));
       panYSlider->blockSignals(false);
   });
   ```

3. **`updatePanRange()` is OBSOLETE:**

   The old InkCanvas required recalculating pan range on every zoom change:
   - At 100% zoom, if page fit viewport: pan range = 0
   - At 200% zoom: pan range = 50% of page size
   
   DocumentViewport eliminates this entirely:
   ```cpp
   // In emitScrollFractions():
   qreal viewportHeight = height() / m_zoomLevel;  // Zoom-aware!
   qreal scrollableHeight = contentSize.height() - viewportHeight;
   vFraction = m_panOffset.y() / scrollableHeight;  // Always 0.0-1.0
   ```
   
   **The fraction 0.0-1.0 is zoom-independent.** DocumentViewport handles zoom internally.
   
   ```cpp
   void MainWindow::updatePanRange() {
       // OBSOLETE - can be removed or kept as no-op
       // Sliders use fixed range 0-10000, connected to scroll fractions
       // DocumentViewport handles all zoom-aware calculations internally
   }
   ```
   
   **Slider setup (done ONCE at init, never updated):**
   ```cpp
   panXSlider->setRange(0, 10000);
   panYSlider->setRange(0, 10000);
   ```

4. **Event Filter on DocumentViewport:**
   - DocumentViewport must have MainWindow's eventFilter installed for wheel events
   - Done in `connectViewportScrollSignals()` when viewport changes

5. **Slider Visibility (Phase 3.3 Status):**
   - **Currently:** Sliders are ALWAYS VISIBLE (simplified for now)
   - **Deferred to Phase 3.4:** Auto-hide behavior with timer
   - The auto-hide logic exists but is disabled - signals/timer not connected
   - Position: panXSlider at top (below tab bar), panYSlider at left

6. **Memory Safety (Phase 3.3 Cleanup):**
   - Viewport scroll connections stored as member variables (`m_hScrollConn`, `m_vScrollConn`)
   - `QPointer<DocumentViewport> m_connectedViewport` - auto-nulls if viewport deleted
   - Connections properly disconnected in destructor
   - Division-by-zero guard for zoomLevel
   - No static local variables (proper object lifetime management)

7. **Mouse Wheel Event Routing (Phase 3.3 Fix):**
   - **Previous (broken):** MainWindow's eventFilter intercepted wheel events on DocumentViewport
     and routed them through sliders, which clamped to 0-10000 (no overshoot support)
   - **Fixed:** eventFilter now returns `false` for wheel events on DocumentViewport,
     allowing DocumentViewport::wheelEvent() to handle scrolling natively
   - DocumentViewport's wheelEvent uses scrollBy() → setPanOffset() → clampPanOffset()
     which properly supports 50% viewport overshoot at document edges
   - Sliders are updated via scroll fraction signals (horizontalScrollChanged/verticalScrollChanged)
   - This ensures consistent pan overshoot behavior for wheel, trackpad, AND touch (future)

### Why This Architecture is Better

| Aspect | Old InkCanvas | New DocumentViewport |
|--------|---------------|----------------------|
| Pan range calculation | MainWindow's job | DocumentViewport's job |
| Zoom affects pan range? | Yes, must recalculate | No, fraction is zoom-independent |
| `updatePanRange()` calls | On every zoom change | Never (obsolete) |
| Slider range | Dynamic (changes with zoom) | Fixed (0-10000) |
| Pan overshoot | Unknown/inconsistent | 50% of viewport (consistent) |

**Key insight:** The scroll fraction (0.0-1.0) is an **abstraction layer** that hides zoom complexity from MainWindow. At any zoom level:
- Fraction 0.0 = top of document
- Fraction 1.0 = bottom of document
- DocumentViewport converts fraction ↔ pan offset internally

---

## Phase 3.1 Task Order

Per `MIGRATION_PHASE3_1_SUBPLAN.md`:

1. ~~3.1.3 - Remove VectorCanvas~~ ✅ COMPLETE
2. ~~3.1.1 - Replace tab system~~ ✅ COMPLETE (QTabWidget + TabManager)
3. ~~3.1.2 - Remove addNewTab InkCanvas code~~ ✅ COMPLETE (wrapped in comment block)
4. ~~3.1.6 - Remove page navigation methods~~ ✅ COMPLETE (stubbed)
5. ~~3.1.4 - Create currentViewport(), replace currentCanvas()~~ ✅ COMPLETE (186→115 calls)
6. ~~3.1.5 - Stub signal handlers~~ ✅ COMPLETE (115→101 calls)
7. ~~3.1.7 - Remove InkCanvas includes/members~~ ✅ COMPLETE (include commented, forward decl added)
8. ~~3.1.8 - Disable ControlPanelDialog~~ ✅ COMPLETE
   - Shows "Coming soon" message for ControlPanelDialog
   - Fixed missing type definitions: `TouchGestureMode`, `ToolType`, `QElapsedTimer`
   - Added local `TouchGestureMode` enum (extracted from InkCanvas.h) with guards
   - Added `#include "core/ToolType.h"` and `#include <QElapsedTimer>` and `#include <QColorDialog>`
   - Stubbed all InkCanvas-dependent functions: save, export, PDF, pan, zoom
   - Replaced ~30 `currentCanvas()` calls with `currentViewport()` where needed
9. ~~3.1.9 - Stub markdown handlers~~ ✅ COMPLETE
   - Stubbed: loadBookmarks, saveBookmarks, toggleCurrentPageBookmark
   - Stubbed: onMarkdownNoteContentChanged, onMarkdownNoteDeleted
   - Stubbed: onHighlightLinkClicked, onHighlightDoubleClicked
   - Stubbed: loadMarkdownNotesForCurrentPage
   - Updated page navigation functions to use currentViewport()

**Phase 3.1 COMPLETE** - App should compile without InkCanvas method calls causing errors.

**Goal:** MainWindow compiles without InkCanvas. Many features will be broken/stubbed.

**Current Status:** App launches and runs without crashes. Basic tab/viewport functionality works.

---

## Known Issues (Phase 3.1)

### ~~1. Dual Tab Bars~~ ✅ FIXED
- **Solution:** Buttons moved to `m_tabWidget` corner widgets, `tabBarContainer` hidden

### ~~2. Color Button Crash~~ ✅ FIXED
- **Solution:** Null checks added to all color button lambdas

### ~~3. Tool Button Crash~~ ✅ FIXED
- **Solution:** Null checks already present or added

### ~~4. `currentCanvas()` Calls~~ ✅ MOSTLY FIXED
- **Status:** Reduced from 186 to 115 calls (38% reduction)
- **Updated:** Tool setters, color buttons, benchmark, thickness slider now use `currentViewport()`
- **Stubbed:** Straight line, rope tool, picture insertion, PDF text selection
- **Remaining:** 115 calls in dormant code paths (PDF, save/export, dial, markdown)

---

## Partial Implementations (Phase 3.1.4)

### Tool/Color Connection - Current vs Target

The tool and color button handlers now use `currentViewport()` but with a **simplified pattern** that defers the full "global state" architecture to Phase 3.3.

#### Current Implementation (Phase 3.1)

```
┌─────────────────┐    ┌──────────────────┐    ┌───────────────────┐
│  Color Button   │───▶│  MainWindow      │───▶│  DocumentViewport │
│  (clicked)      │    │  currentViewport()│    │  setPenColor()    │
└─────────────────┘    └──────────────────┘    └───────────────────┘
                              │
                              ▼
                       TabManager::currentViewport()
```

**Code path:**
```cpp
// Button lambda in setupUi()
connect(redButton, &QPushButton::clicked, [this]() { 
    if (DocumentViewport* vp = currentViewport()) {
        vp->setPenColor(getPaletteColor("red")); 
    }
});

// currentViewport() in MainWindow
DocumentViewport* MainWindow::currentViewport() const {
    return m_tabManager ? m_tabManager->currentViewport() : nullptr;
}
```

**Characteristics:**
- ✅ Uses correct component chain (MainWindow → TabManager → DocumentViewport)
- ✅ Works without crashes
- ❌ No global state stored in MainWindow
- ❌ No tab-switch handler to reapply state

#### Target Architecture (Phase 3.3)

```
┌─────────────────┐    ┌──────────────────────────────┐    ┌───────────────────┐
│  Color Button   │───▶│  MainWindow                  │───▶│  DocumentViewport │
│  (clicked)      │    │  1. Store in m_penColor      │    │  setPenColor()    │
└─────────────────┘    │  2. Apply to currentViewport │    └───────────────────┘
                       └──────────────────────────────┘
                                      │
                       ┌──────────────▼──────────────┐
                       │  onViewportChanged()        │
                       │  - Apply m_penColor         │
                       │  - Apply m_penThickness     │
                       │  - Apply m_currentTool      │
                       │  - Apply m_eraserSize       │
                       └─────────────────────────────┘
```

**Target code (from MIGRATION_PHASE3_SUBPLAN.md):**
```cpp
// MainWindow members for global state
ToolType m_currentTool = ToolType::Pen;
QColor m_penColor = Qt::black;
qreal m_penThickness = 5.0;
qreal m_eraserSize = 20.0;

// Color change handler
void MainWindow::onPenColorChanged(const QColor& color) {
    m_penColor = color;  // Store globally
    if (auto* vp = m_tabManager->currentViewport()) {
        vp->setPenColor(color);  // Apply to current
    }
}

// Tab switch handler
void MainWindow::onViewportChanged(DocumentViewport* viewport) {
    if (viewport) {
        viewport->setCurrentTool(m_currentTool);
        viewport->setPenColor(m_penColor);
        viewport->setPenThickness(m_penThickness);
        viewport->setEraserSize(m_eraserSize);
    }
}
```

### Comparison Table

| Aspect | Current (3.1) | Target (3.3) |
|--------|---------------|--------------|
| Color stored in | DocumentViewport only | MainWindow + DocumentViewport |
| Tool stored in | DocumentViewport only | MainWindow + DocumentViewport |
| Tab switch handling | No state sync | Reapply global state |
| Multi-tab consistency | Each tab independent | All tabs share tool state |

### What Works Now

| Feature | Status | Notes |
|---------|--------|-------|
| Color buttons (6) | ✅ Working | Red, blue, yellow, green, black, white |
| Custom color button | ✅ Working | Color picker + apply |
| Tool buttons | ✅ Working | Pen, marker (→pen), eraser |
| Thickness slider | ✅ Working | Updates viewport thickness |
| Benchmark | ✅ Working | Uses viewport's getPaintRate() |

### What's Deferred to Phase 3.3

- [ ] Global tool state members (`m_currentTool`, `m_penColor`, etc.)
- [ ] `onViewportChanged()` tab switch handler
- [ ] Unified tool/color change methods
- [ ] Tab-consistent tool state (switching tabs maintains tool)

---

## Current MainWindow State

### Members to Remove (Phase 3.1.1)

```cpp
QListWidget *tabList;          // Line 593 - Replace with QTabWidget
QStackedWidget *canvasStack;   // Line 594 - Remove
QMap<InkCanvas*, int> pageMap; // Line 573 - Remove (Viewport tracks own page)
```

### Members to Add (Phase 3.1.1)

```cpp
#include "ui/TabManager.h"
TabManager* m_tabManager = nullptr;
```

### Already Removed (Phase 3.1.3)

- `vectorPenButton`, `vectorEraserButton`, `vectorUndoButton` - Removed from header
- `setVectorPenTool()`, `setVectorEraserTool()`, `vectorUndo()` - Removed from implementation
- VectorCanvas still in CMakeLists.txt (InkCanvas dependency)

---

## Execution Strategy

1. **Batch related tasks** when they don't break compilation independently
2. **Compile verification** after groups of changes (not after every single change)
3. **User runs compile commands manually**
4. **Keep stubbed methods** for features to be reconnected in Phase 3.2+

---

## Notes for Implementation

### VectorCanvas in CMakeLists.txt

VectorCanvas.cpp is still in CMakeLists.txt because InkCanvas depends on it. Once InkCanvas is disconnected (end of Phase 3.1), both can be removed from the build.

### SpnPackageManager

**Can be DISCONNECTED now.** SpnPackageManager handles old `.spn` format which we're abandoning.
- The ability to save/load `.snx` packages will be implemented LATER
- For now, documents are in-memory only (no persistence)
- This also breaks the InkCanvas dependency via BackgroundStyle

### Launch Sequence (Phase 3 - Simplified)

Main.cpp → MainWindow (DIRECTLY)
- **Skip LauncherWindow entirely** during Phase 3
- Main.cpp will be significantly simplified (drop most old code)
- LauncherWindow will be relinked LATER (after core is working)

### Files Directly Under `/source`

**ASSUME NONE OF THESE EXIST** except Main.cpp and MainWindow:
- LauncherWindow.cpp/.h - NOT LINKED during Phase 3
- SpnPackageManager.cpp/.h - NOT LINKED
- InkCanvas.cpp/.h - Goal: disconnect from MainWindow
- VectorCanvas.cpp/.h - Already removed from MainWindow
- ControlPanelDialog.cpp/.h - NOT LINKED
- SDLControllerManager.cpp/.h - NOT LINKED
- SimpleAudio.cpp/.h - NOT LINKED
- PictureWindow*.cpp/.h - NOT LINKED
- MarkdownNotes*.cpp/.h - NOT LINKED
- RecentNotebooksManager.cpp/.h - NOT LINKED
- And ALL other auxiliary files - NOT LINKED

**Only subfolders of /source contain reusable new logic:**
- `/source/core/` - Document, Page, DocumentViewport, DocumentManager
- `/source/ui/` - TabManager, LayerPanel
- `/source/layers/` - VectorLayer
- `/source/strokes/` - VectorStroke, StrokePoint
- `/source/objects/` - InsertedObject hierarchy
- `/source/pdf/` - PDF handling (if needed)

### Disconnection Strategy

**Go straight to MainWindow InkCanvas disconnection.**
Let dependent files break - they'll be disconnected from the build anyway.
Don't waste time fixing SpnPackageManager, LauncherWindow, etc.

### Debug Overlay

The debug overlay in DocumentViewport (lines 716-763) will be MOVED to MainWindow later.
Currently shows: document info, zoom, pan, tool, undo/redo state, paint rate.
This is temporary test code from Phase 2.

### Initial Document (Phase 3)

Before LauncherWindow is relinked, MainWindow should start with a blank document.
**Use DocumentManager to create the document** - maintain proper structure for consistency.
Don't copy test code from DocumentViewportTests - keep architecture clean.

---

## Implementation Decisions

### Main.cpp Changes
- **Comment out** LauncherWindow code (don't delete - may need reference)
- Go directly to MainWindow
- Remove `useNewViewport` parameter from MainWindow constructor

### CMakeLists.txt Changes
- **Comment out** files as we disconnect them (not delete)
- This makes it easy to see what was removed and recover if needed

### MainWindow Constructor
- Remove `bool useNewViewport` parameter entirely
- Always use new DocumentViewport architecture
- Create blank Document via DocumentManager on startup

### ControlPanelDialog

`ControlPanelDialog` depends on InkCanvas. Will be disabled/stubbed in Phase 3.1.8.

---

## Questions Resolved

1. ✅ Phase 3.0 files exist and are complete
2. ✅ VectorCanvas removed from MainWindow (not CMakeLists yet)
3. ✅ TabManager replaces entire old tab system
4. ✅ TabManager RECEIVES QTabWidget (MainWindow creates it) - CORRECTED
5. ✅ Phase 3.1 goal: compile without InkCanvas, features stubbed
6. ✅ New format is JSON → .snx package
7. ✅ --use-new-viewport flag: REMOVE entirely (always use new architecture)
8. ✅ No backwards compatibility concerns
9. ✅ Batch related tasks, manual compile verification
10. ✅ MainWindow_OLD files exist as reference
11. ✅ Skip LauncherWindow entirely during Phase 3
12. ✅ Main.cpp: comment out LauncherWindow code, go directly to MainWindow
13. ✅ Initial document: blank Document via DocumentManager (not test document)
14. ✅ CMakeLists.txt: comment out files as we go (not delete)
15. ✅ All files under /source except Main/MainWindow: assume don't exist

---

*Notes file for SpeedyNote Phase 3 migration*

