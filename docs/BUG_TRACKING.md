# SpeedyNote Bug Tracking

## Overview

This document tracks bugs, regressions, and polish issues discovered during and after the migration. Each bug is assigned a unique ID for reference in commit messages and code comments.

**Format:** `BUG-{CATEGORY}-{NUMBER}` (e.g., `BUG-VP-001` for viewport bugs)

**Last Updated:** Jan 14, 2026 (BUG-AB-001 fixed)

---

## Categories

| Prefix | Category | Description |
|--------|----------|-------------|
| **VP** | Viewport | DocumentViewport rendering, pan/zoom, coordinate transforms |
| **DRW** | Drawing | Pen, Marker, Eraser stroke behavior |
| **LSO** | Lasso | Lasso selection, transforms, clipboard |
| **HL** | Highlighter | Highlighter tool, PDF text selection |
| **SL** | Straight Line | Straight line mode |
| **OBJ** | Objects | ImageObject, LinkObject, ObjectSelect tool |
| **LYR** | Layers | Layer panel, layer operations |
| **PG** | Pages | Page panel, page navigation, add/delete |
| **PDF** | PDF | PDF loading, rendering, outline, caching |
| **FILE** | File I/O | Save/load, bundle format, dirty tracking |
| **TAB** | Tabs | Tab management, switching, close behavior |
| **TB** | Toolbar | Main toolbar, tool buttons |
| **STB** | Subtoolbar | Tool-specific subtoolbars |
| **AB** | Action Bar | Context action bar |
| **SB** | Sidebar | Left sidebar container, panel switching |
| **TCH** | Touch | Touch gestures, tablet input |
| **MD** | Markdown | Markdown notes integration |
| **PERF** | Performance | Lag, memory, CPU issues |
| **UI** | UI/UX | Visual glitches, layout issues |
| **MISC** | Miscellaneous | Other issues |

---

## Priority Levels

| Level | Description | Response |
|-------|-------------|----------|
| 🔴 **P0** | Critical | Data loss, crash, unusable feature - fix immediately |
| 🟠 **P1** | High | Major feature broken, poor UX - fix soon |
| 🟡 **P2** | Medium | Minor feature issue, workaround exists |
| 🟢 **P3** | Low | Polish, cosmetic, edge case |

---

## Bug Status

| Status | Description |
|--------|-------------|
| 🆕 **NEW** | Reported, not yet investigated |
| 🔍 **INVESTIGATING** | Root cause being analyzed |
| 🔧 **IN PROGRESS** | Fix being implemented |
| ✅ **FIXED** | Fix complete and verified |
| ⏸️ **DEFERRED** | Won't fix now, tracked for later |
| ❌ **WONTFIX** | By design, not a bug, or not worth fixing |

---

## Active Bugs

### Viewport (VP)

*No active bugs*

---

### Touch/Tablet (TCH)

*No active bugs*

---

<!-- Template:
#### BUG-VP-XXX: [Title]
**Priority:** 🟡 P2 | **Status:** 🆕 NEW

**Symptom:** 
[What the user sees/experiences]

**Steps to Reproduce:**
1. Step one
2. Step two
3. Step three

**Expected:** [What should happen]
**Actual:** [What actually happens]

**Root Cause:** 
[Technical explanation - fill in during investigation]

**Fix:**
[Solution description]

**Files Modified:**
- `source/core/DocumentViewport.cpp`

**Verified:** [ ] Tested and working
-->

---

### Drawing (DRW)

---

### Lasso (LSO)

---

### Highlighter (HL)

---

### Straight Line (SL)

---

### Objects (OBJ)

---

### Layers (LYR)

---

### Pages (PG)

---

### PDF (PDF)

---

### File I/O (FILE)

---

### Tabs (TAB)

---

### Toolbar (TB)

---

### Subtoolbar (STB)

---

### Action Bar (AB)

---

### Sidebar (SB)

---

### Touch/Tablet (TCH)

---

### Markdown (MD)

---

### Performance (PERF)

---

### UI/UX (UI)

---

### Miscellaneous (MISC)

---

## Fixed Bugs Archive

<!-- Move fixed bugs here with their full details for reference -->

### Recently Fixed

#### BUG-AB-001: Action bars mispositioned when PagePanelActionBar visible
**Priority:** 🟠 P1 | **Status:** ✅ FIXED

**Symptom:** 
The page panel action bar was "stopping" other action bars from moving to the correct location (right edge of DocumentViewport). Action bars had a tendency to stick somewhere in the viewport, or outside it entirely. Additionally, context action bars (Lasso, ObjectSelect) wouldn't appear correctly when PagePanelActionBar was NOT open.

**Steps to Reproduce:**
1. Open a paged document (not edgeless)
2. Open the left sidebar and select the Pages tab
3. PagePanelActionBar appears
4. Create a lasso selection or select an object
5. The context action bar (LassoActionBar/ObjectSelectActionBar) appears in wrong position
6. OR: Without PagePanel open, context action bars don't appear or appear offscreen

**Expected:** Both bars should be arranged in 2-column layout, right-aligned 24px from viewport edge; single action bars should also position correctly
**Actual:** Action bars stuck in middle of viewport or outside it

**Root Cause:** 
Four issues in `ActionBarContainer.cpp`:

1. **`animateShow()` ignored 2-column layout:** The animation code only used `m_currentActionBar` dimensions, completely ignoring `PagePanelActionBar` when calculating positions. This was legacy code from before 2-column support was added.

2. **Stale viewport rect:** When `setPagePanelVisible()` was called, it used cached `m_viewportRect` which could be empty or outdated. `MainWindow::updatePagePanelActionBarVisibility()` didn't call `updateActionBarPosition()` after changing visibility.

3. **`isVisible()` returns false when container is hidden (THE REAL BUG):** In `updateSize()` and `updatePosition()`, the code checked `m_currentActionBar->isVisible()` to determine if a context bar should be shown. However, `QWidget::isVisible()` returns false if ANY ancestor is hidden. When showing a context action bar:
   - `showActionBar()` calls `m_currentActionBar->show()` on the child
   - Then calls `updateSize()` BEFORE showing the container
   - `isVisible()` returns false because container is still hidden
   - Container size is set to 0x0!
   - This is why it worked with PagePanel open (container already visible) but not when closed.

4. **No fresh rect for context bars:** When context action bars were shown, `showActionBar()` used `m_viewportRect` which might be empty.

**Fix:**
1. Rewrote `animateShow()` to calculate total width/height considering both PagePanelActionBar and context action bar (2-column or single-column layout)
2. Added `updateActionBarPosition()` call in `MainWindow::updatePagePanelActionBarVisibility()` after visibility change
3. Animation now triggers `updatePosition()` on completion to properly position child action bars
4. Added `positionUpdateRequested` signal to `ActionBarContainer` - emitted when container is about to become visible
5. Connected signal to `MainWindow::updateActionBarPosition()` to ensure fresh viewport rect before showing
6. Added fallback to query parent widget rect when `m_viewportRect` is empty
7. **Critical fix:** Changed `updateSize()`, `updatePosition()`, and `setPagePanelVisible()` to use `m_currentActionBar != nullptr` instead of `m_currentActionBar->isVisible()`. This checks intent-to-show rather than actual visibility, which depends on ancestor state.

**Files Modified:**
- `source/ui/actionbars/ActionBarContainer.h` (added positionUpdateRequested signal)
- `source/ui/actionbars/ActionBarContainer.cpp` (animateShow, showActionBar, updatePosition - parent fallback)
- `source/MainWindow.cpp` (updatePagePanelActionBarVisibility, connect positionUpdateRequested)

**Verified:** [x] Single action bar positions correctly
**Verified:** [x] 2-column layout positions both bars correctly
**Verified:** [x] Animation works for both layouts
**Verified:** [x] Context bars appear correctly without PagePanel open
**Verified:** [x] Action bars reposition on window maximize

---

#### BUG-UI-001: Subtoolbars/action bars don't reposition on window maximize
**Priority:** 🟢 P3 | **Status:** ✅ FIXED

**Symptom:** 
When maximizing the window, subtoolbars and action bars (especially PagePanelActionBar) didn't reposition correctly. They only updated on gradual resizing.

**Root Cause:** 
1. The event filter comparison used `m_viewportStack->parentWidget()` instead of comparing directly with `m_canvasContainer` (the object the filter was installed on)
2. The `MainWindow::resizeEvent()` was empty and didn't trigger position updates

**Fix:**
1. Changed event filter to compare `obj == m_canvasContainer` directly
2. Added `updateSubToolbarPosition()` and `updateActionBarPosition()` calls in `resizeEvent()`

**Files Modified:**
- `source/MainWindow.cpp` (eventFilter, resizeEvent)

**Verified:** [x] Subtoolbars reposition on maximize
**Verified:** [x] Action bars reposition on maximize

---

#### BUG-UI-002: Touch scroll in sidebar panels triggers unintended page navigation
**Priority:** 🟡 P2 | **Status:** ✅ FIXED

**Symptom:** 
When scrolling the PagePanel or OutlinePanel with touch, the scroll gesture was also being detected as a tap/click on an item, causing unintended page navigation while trying to scroll.

**Root Cause:** 
Qt's `QScroller::TouchGesture` enables kinetic scrolling for touch, but the `clicked` signal is still emitted when the touch ends on an item, even if the user was scrolling. The click handler didn't distinguish between a genuine tap and a scroll-release.

**Fix:**
In both `PagePanel::onItemClicked()` and `OutlinePanel::onItemClicked()`, check the QScroller's state before processing the click:
- If scroller state is `Dragging` or `Scrolling` → ignore the click
- If scroller state is `Inactive` or `Pressed` → process the click normally

```cpp
QScroller* scroller = QScroller::scroller(viewport);
if (scroller) {
    QScroller::State state = scroller->state();
    if (state == QScroller::Dragging || state == QScroller::Scrolling) {
        return;  // Ignore click during scroll
    }
}
```

**Files Modified:**
- `source/ui/sidebars/PagePanel.cpp` (onItemClicked)
- `source/ui/sidebars/OutlinePanel.cpp` (onItemClicked)

**Verified:** [ ] Touch scroll doesn't trigger page navigation
**Verified:** [ ] Deliberate tap still works for page selection

---

#### BUG-TCH-001: Touch gesture mode button fails to switch modes
**Priority:** 🟠 P1 | **Status:** ✅ FIXED

**Symptom:** 
The touch gesture mode button on the toolbar appeared to cycle through states (debug message confirmed mode changes), but actual touch behavior was always stuck at Full gestures mode.

**Steps to Reproduce:**
1. Open SpeedyNote
2. Click the touch gesture button (hand icon) to cycle modes
3. Observe debug output: "Toolbar: Touch gesture mode changed to 0/1/2"
4. Try touch gestures - they always work as Full mode regardless of button state

**Expected:** Touch gestures should be disabled (mode 0), Y-axis only (mode 1), or full (mode 2)
**Actual:** Touch gestures always behaved as Full mode

**Root Cause:** 
In `MainWindow.cpp` line 971-975, the `touchGestureModeChanged` signal handler only logged the mode but **never called `setTouchGestureMode()`** to actually apply it. There was a TODO comment indicating this was never completed:
```cpp
connect(m_toolbar, &Toolbar::touchGestureModeChanged, this, [this](int mode) {
    qDebug() << "Toolbar: Touch gesture mode changed to" << mode;
    // TODO: Connect to TouchGestureHandler when ready  ← NEVER DONE!
});
```

**Fix:**
1. Updated the signal handler to convert the int mode to `TouchGestureMode` enum and call `setTouchGestureMode()`
2. Updated `setTouchGestureMode()` to sync the toolbar button state (for settings load on startup)

**Files Modified:**
- `source/MainWindow.cpp` (lines 971-982, 3001-3018)

**Verified:** [x] Signal handler now calls setTouchGestureMode()
**Verified:** [x] Toolbar button syncs when mode loaded from settings

---

## Statistics

| Category | New | In Progress | Fixed | Total |
|----------|-----|-------------|-------|-------|
| Viewport | 0 | 0 | 0 | 0 |
| Drawing | 0 | 0 | 0 | 0 |
| Lasso | 0 | 0 | 0 | 0 |
| Highlighter | 0 | 0 | 0 | 0 |
| Objects | 0 | 0 | 0 | 0 |
| Layers | 0 | 0 | 0 | 0 |
| Pages | 0 | 0 | 0 | 0 |
| PDF | 0 | 0 | 0 | 0 |
| File I/O | 0 | 0 | 0 | 0 |
| Tabs | 0 | 0 | 0 | 0 |
| Toolbar | 0 | 0 | 0 | 0 |
| Subtoolbar | 0 | 0 | 0 | 0 |
| Action Bar | 0 | 0 | 1 | 1 |
| Sidebar | 0 | 0 | 0 | 0 |
| Touch | 0 | 0 | 1 | 1 |
| Markdown | 0 | 0 | 0 | 0 |
| Performance | 0 | 0 | 0 | 0 |
| UI/UX | 0 | 0 | 2 | 2 |
| **TOTAL** | **0** | **0** | **4** | **4** |

---

## Notes

### Commit Message Format
```
Fix BUG-VP-001: [Short description]

[Longer explanation if needed]
```

### Code Comment Format
```cpp
// BUG-VP-001 FIX: [Explanation of what this code fixes]
```

### Related Documents
- See `*_QA.md` files for feature-specific test cases
- See `*_SUBPLAN.md` files for implementation details
- Code review fixes are documented as `CR-*` in subplan files

