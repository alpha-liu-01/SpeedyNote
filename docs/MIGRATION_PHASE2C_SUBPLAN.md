# Phase 2C: Two-Column Layout Enhancement Subplan

## Overview

This phase enhances the existing two-column layout support with:
- Auto-layout mode that switches between 1 and 2 columns based on viewport width
- Dynamic PDF cache capacity based on visible pages
- Improved preload strategy for 2-column mode
- Immediate cache eviction when capacity decreases

**Prerequisites:** Phase 2B (Lasso Tool) complete

---

## Design Decisions (Confirmed)

| Topic | Decision |
|-------|----------|
| **Default mode** | 1-column only (no auto-switching) |
| **Toggle shortcut** | Ctrl+2 toggles between "1-column only" and "auto 1/2 column" |
| **Auto-layout check** | In auto mode: 2-column when `viewport_width >= 2 * page_width + gap` |
| **Persistence** | Session only (resets to 1-column only on restart) |
| **Cache capacity (1-col)** | `visible_pages + 3` (minimum 4) |
| **Cache capacity (2-col)** | `visible_pages + 6` (minimum 4) |
| **Preload (1-col)** | ±1 pages beyond visible (2 pages total) |
| **Preload (2-col)** | ±2 pages beyond visible (4 pages = 1 row each direction) |
| **Capacity decrease** | Immediately evict FURTHEST entries |
| **DPI cap** | 300 DPI remains (not a concern - 2-column won't be enabled at high zoom) |

---

## Current Implementation Status

### Already Working ✅

1. **Layout modes** - `SingleColumn` and `TwoColumn` enum defined
2. **setLayoutMode()** - Switches between modes, invalidates layout cache
3. **Layout calculations** - `pagePosition()`, `totalContentSize()` work for both modes
4. **PDF cache** - Smart eviction (evicts furthest page when full)
5. **Async PDF preloading** - Debounced, background thread rendering

### Needs Enhancement

| Area | Current | Goal |
|------|---------|------|
| Cache Capacity | Fixed: 6 (1-col) / 12 (2-col) | Dynamic: `visible + 3` / `visible + 6` |
| Layout Toggle | None | Ctrl+2 toggles auto mode |
| Default Mode | N/A | 1-column only |
| Auto Logic | None | Check width vs 2 pages + gap |
| Preload Strategy | ±1 pages (both) | ±1 (1-col) / ±2 (2-col) |
| Capacity Decrease | Removes first | Evict FURTHEST entries |

---

## Task Breakdown

### Task 2C.1: Add Auto-Layout State (~30 lines) ✅ COMPLETE

**Files:** `source/core/DocumentViewport.h`, `source/core/DocumentViewport.cpp`

**New members:**
```cpp
// In DocumentViewport.h private section:
bool m_autoLayoutEnabled = false;  // Default: 1-column only mode

// In DocumentViewport.h public section:
void setAutoLayoutEnabled(bool enabled);
bool autoLayoutEnabled() const { return m_autoLayoutEnabled; }
```

**Implementation:**
```cpp
void DocumentViewport::setAutoLayoutEnabled(bool enabled) {
    if (m_autoLayoutEnabled == enabled) return;
    
    m_autoLayoutEnabled = enabled;
    
    if (enabled) {
        // Immediately check if layout should change
        checkAutoLayout();
    } else {
        // When disabling auto mode, revert to single column
        setLayoutMode(LayoutMode::SingleColumn);
    }
}
```

**Deliverable:** Auto-layout flag can be toggled programmatically

---

### Task 2C.2: Implement Auto-Layout Check (~50 lines) ✅ COMPLETE

**Files:** `source/core/DocumentViewport.cpp`

**New private method:**
```cpp
void checkAutoLayout();
```

**Implementation:**
```cpp
void DocumentViewport::checkAutoLayout() {
    // Only check if auto mode is enabled
    if (!m_autoLayoutEnabled) return;
    
    // Skip for edgeless documents (no pages)
    if (!m_document || m_document->isEdgeless()) return;
    
    // Skip if no pages
    if (m_document->pageCount() == 0) return;
    
    // Get typical page width from first page
    const Page* page = m_document->page(0);
    if (!page) return;
    
    // Calculate required width for 2-column layout (in viewport pixels)
    qreal pageWidth = page->size.width() * m_zoomLevel;
    qreal gapWidth = m_pageGap * m_zoomLevel;
    qreal requiredWidth = 2 * pageWidth + gapWidth;
    
    // Determine target layout mode
    LayoutMode targetMode = (width() >= requiredWidth) 
        ? LayoutMode::TwoColumn 
        : LayoutMode::SingleColumn;
    
    // Only switch if different (avoids redundant invalidation)
    if (targetMode != m_layoutMode) {
        setLayoutMode(targetMode);
    }
}
```

**Call sites:**
- `resizeEvent()` - after resize handling
- `onGestureTimeout()` - after zoom settles (not during deferred zoom)
- `setAutoLayoutEnabled(true)` - when auto mode is enabled

**Deliverable:** Layout automatically switches based on viewport width

---

### Task 2C.3: Dynamic PDF Cache Capacity (~60 lines) ✅ COMPLETE

**Files:** `source/core/DocumentViewport.h`, `source/core/DocumentViewport.cpp`

**New private method:**
```cpp
void evictFurthestCacheEntries();
```

**Modify existing `updatePdfCacheCapacity()`:**

```cpp
void DocumentViewport::updatePdfCacheCapacity() {
    // Calculate visible page count
    QVector<int> visible = visiblePages();
    int visibleCount = visible.size();
    
    // Buffer: 3 pages for 1-column (1 above + 2 below or vice versa)
    //         6 pages for 2-column (1 row above + 1 row below = 4, plus margin)
    int buffer = (m_layoutMode == LayoutMode::TwoColumn) ? 6 : 3;
    
    // New capacity with minimum of 4
    int newCapacity = qMax(4, visibleCount + buffer);
    
    // Only update if changed
    if (m_pdfCacheCapacity != newCapacity) {
        m_pdfCacheCapacity = newCapacity;
        
        // Immediately evict if over new capacity
        QMutexLocker locker(&m_pdfCacheMutex);
        evictFurthestCacheEntries();
    }
}

void DocumentViewport::evictFurthestCacheEntries() {
    // Must be called with m_pdfCacheMutex locked
    
    // Get reference page for distance calculation
    int centerPage = m_currentPageIndex;
    
    // Evict furthest entries until within capacity
    while (m_pdfCache.size() > m_pdfCacheCapacity) {
        int evictIdx = 0;
        int maxDistance = -1;
        
        for (int i = 0; i < m_pdfCache.size(); ++i) {
            int dist = qAbs(m_pdfCache[i].pageIndex - centerPage);
            if (dist > maxDistance) {
                maxDistance = dist;
                evictIdx = i;
            }
        }
        
        qDebug() << "PDF cache evict: page" << m_pdfCache[evictIdx].pageIndex 
                 << "distance" << maxDistance << "new size" << (m_pdfCache.size() - 1);
        m_pdfCache.removeAt(evictIdx);
    }
}
```

**Call sites (existing, already call `updatePdfCacheCapacity()`):**
- Constructor
- `setLayoutMode()`

**Additional call sites (new):**
- After scroll settles (capacity may change with visible page count)
- After zoom settles

**Deliverable:** Cache capacity adjusts dynamically, immediate eviction on decrease

---

### Task 2C.4: Enhanced Preload for Two-Column (~20 lines) ✅ COMPLETE

**Files:** `source/core/DocumentViewport.cpp`

**Modify `doAsyncPdfPreload()`:**

```cpp
void DocumentViewport::doAsyncPdfPreload() {
    // ... existing validation code ...
    
    QVector<int> visible = visiblePages();
    if (visible.isEmpty()) return;
    
    int first = visible.first();
    int last = visible.last();
    
    // Pre-load buffer depends on layout mode:
    // - Single column: ±1 page (above and below)
    // - Two column: ±2 pages (1 row above + 1 row below = 4 pages)
    int preloadBuffer = (m_layoutMode == LayoutMode::TwoColumn) ? 2 : 1;
    
    int preloadStart = qMax(0, first - preloadBuffer);
    int preloadEnd = qMin(m_document->pageCount() - 1, last + preloadBuffer);
    
    // ... rest of existing preload logic ...
}
```

**Deliverable:** 2-column mode preloads 4 pages (1 row each direction)

---

### Task 2C.5: MainWindow Integration (~30 lines) ✅ COMPLETE

**Files:** `source/ui/MainWindow.h`, `source/ui/MainWindow.cpp`

**Add shortcut:**
```cpp
// In MainWindow constructor or setupShortcuts():
QShortcut* toggleAutoLayoutShortcut = new QShortcut(QKeySequence("Ctrl+2"), this);
connect(toggleAutoLayoutShortcut, &QShortcut::activated, this, &MainWindow::toggleAutoLayout);
```

**New slot:**
```cpp
void MainWindow::toggleAutoLayout() {
    DocumentViewport* viewport = currentViewport();
    if (!viewport) return;
    
    Document* doc = viewport->document();
    if (!doc || doc->isEdgeless()) {
        // Auto layout only applies to paged documents
        return;
    }
    
    bool newState = !viewport->autoLayoutEnabled();
    viewport->setAutoLayoutEnabled(newState);
    
    // Show status feedback
    QString msg = newState 
        ? tr("Auto layout enabled (1/2 columns)")
        : tr("Single column layout");
    statusBar()->showMessage(msg, 2000);
}
```

**Deliverable:** Ctrl+2 toggles auto-layout mode with status feedback

---

### Task 2C.6: Integration and Testing ✅ COMPLETE

**Test cases:**

1. **Default behavior:**
   - Launch app → should be in 1-column mode
   - Load PDF document → should stay in 1-column mode

2. **Toggle shortcut:**
   - Press Ctrl+2 → status shows "Auto layout enabled"
   - Press Ctrl+2 again → status shows "Single column layout"

3. **Auto-layout switching:**
   - Enable auto mode on wide viewport → should switch to 2-column
   - Resize window narrower → should switch back to 1-column
   - Zoom in significantly → should switch to 1-column
   - Zoom out → should switch to 2-column

4. **Cache capacity:**
   - In 1-column with 2 visible pages → capacity should be 5 (2+3)
   - Switch to 2-column with 4 visible pages → capacity should be 10 (4+6)
   - Zoom in (fewer visible) → capacity decreases, furthest evicted

5. **Preload verification:**
   - In 1-column → check debug output shows ±1 page preload
   - In 2-column → check debug output shows ±2 page preload

6. **Edge cases:**
   - Edgeless document → Ctrl+2 should do nothing
   - Single-page document → should stay 1-column regardless
   - Session restart → should revert to 1-column only mode

---

## File Changes Summary

| File | Changes |
|------|---------|
| `DocumentViewport.h` | Add `m_autoLayoutEnabled`, `setAutoLayoutEnabled()`, `autoLayoutEnabled()`, declare `checkAutoLayout()`, `evictFurthestCacheEntries()`, `recenterHorizontally()` |
| `DocumentViewport.cpp` | Implement new methods, modify `updatePdfCacheCapacity()`, modify `doAsyncPdfPreload()`, add `checkAutoLayout()` calls, add `recenterHorizontally()` for layout-aware centering |
| `MainWindow.h` | Declare `toggleAutoLayout()` slot |
| `MainWindow.cpp` | Add Ctrl+2 shortcut, implement `toggleAutoLayout()` |

---

## Content Centering Fix

When switching between 1-column and 2-column layouts, the content width changes significantly:
- **1-column**: Content width = 1 page width
- **2-column**: Content width = 2 page widths + gap

The original `centerViewportContent()` in MainWindow was only called once when a tab is created.
To fix this, `recenterHorizontally()` was added to DocumentViewport and is called from `setLayoutMode()`.

**Implementation:**
```cpp
void DocumentViewport::recenterHorizontally() {
    if (!m_document || m_document->isEdgeless()) return;
    
    qreal viewportWidth = width() / m_zoomLevel;
    QSizeF contentSize = totalContentSize();  // Width changes with layout mode
    
    if (contentSize.width() < viewportWidth) {
        qreal centeringOffset = (viewportWidth - contentSize.width()) / 2.0;
        m_panOffset.setX(-centeringOffset);
        emit panChanged(m_panOffset);
    }
}
```

This ensures content is always centered regardless of layout mode changes.

---

## Two-Column Layout Bug Fixes

### Fix 2C.F1: Zoom-in Not Triggering Layout Switch

**Problem:** Zooming in too much didn't quit 2-column layout in auto mode.

**Root Cause:** `checkAutoLayout()` was only called in `onGestureTimeout()`, but zoom level is applied in `endZoomGesture()`. For non-deferred zoom via `zoomAtPoint()`, it was never called.

**Fix:** 
- Added `checkAutoLayout()` call at end of `endZoomGesture()` 
- Added `checkAutoLayout()` call in `zoomAtPoint()` for non-deferred zoom
- Removed redundant call from `onGestureTimeout()` (now handled by `endZoomGesture()`)

### Fix 2C.F2: Vertical Offset Not Scaled on Layout Switch

**Problem:** When switching from 1-column to 2-column (or vice versa), user was taken to a different page because vertical positions change dramatically.

**Root Cause:** `setLayoutMode()` only recentered horizontally, not adjusted Y offset.

**Fix:** In `setLayoutMode()`:
1. Before switching: Record current page and its Y position
2. After switching: Get new Y position of same page  
3. Adjust `m_panOffset.y()` by the delta to keep same page visible

### Fix 2C.F3: Current Page Detection in Two-Column Mode

**Problem:** In 2-column mode, current page was always an odd page (1, 3, 5...) because viewport center falls in the gap between columns.

**Root Cause:** `updateCurrentPageIndex()` used `pageAtPoint(viewCenter)` which returns -1 when center is in a gap, then fell back to `visible.first()` (always left column).

**Fix:** Improved fallback logic for 2-column mode:
- When center is in gap, find visible page whose center is closest to viewport center
- Uses Euclidean distance for accurate 2D proximity check
- Works correctly regardless of which column the user is viewing

---

## Implementation Order

1. **2C.1** - Add auto-layout state (foundation)
2. **2C.2** - Implement auto-layout check logic
3. **2C.3** - Dynamic cache capacity with immediate eviction
4. **2C.4** - Enhanced preload for two-column
5. **2C.5** - MainWindow shortcut integration
6. **2C.6** - Integration testing

---

## Success Criteria

Phase 2C is complete when:

1. ✅ Default mode is 1-column only (no auto-switching)
2. ✅ Ctrl+2 toggles between "1-column only" and "auto 1/2 column" modes
3. ✅ In auto mode, layout switches based on `viewport_width >= 2 * page_width + gap`
4. ✅ PDF cache capacity dynamically adjusts to `visible_pages + buffer`
5. ✅ Cache immediately evicts furthest entries when capacity decreases
6. ✅ 2-column mode preloads 4 pages (1 row above + 1 row below)
7. ✅ Layout mode resets to 1-column only on session restart
8. ✅ Edgeless documents ignore Ctrl+2 shortcut

---

## Future Enhancements (Not in this phase)

- **Settings persistence** - Save preferred mode per document or globally
- **UI toggle** - Add button/menu item for layout mode
- **Customizable shortcuts** - Make Ctrl+2 configurable
- **Touch gesture** - Two-finger pinch to toggle layout
- **Horizontal layout** - Side-by-side scrolling mode

---

## Notes

### Performance Considerations

- `checkAutoLayout()` is lightweight (simple width comparison)
- Cache eviction uses O(n) scan but cache size is typically < 20 entries
- Layout change invalidates PDF cache anyway (positions change)

### Memory Impact

- Dynamic capacity prevents over-allocation at high zoom
- Immediate eviction prevents memory bloat during layout switches
- Minimum capacity of 4 ensures basic preload always works

### Interaction with Existing Features

- Deferred zoom rendering: Auto-layout check happens AFTER zoom settles
- PDF preloading: Already debounced, works with new capacity
- Stroke cache: Unaffected (per-layer, not layout-dependent)

---

## Code Review Fixes (2C.CR)

### CR-2C.1: Removed Unused Variable
**Issue:** `contentCenterX` was declared in `updateCurrentPageIndex()` but never used (dead code from earlier design).
**Fix:** Removed the unused variable declaration.

### CR-2C.2: Thread Safety in `updatePdfCacheCapacity()`
**Issue:** `m_pdfCacheCapacity` was updated outside the mutex lock, creating a brief window where other threads could see inconsistent state between capacity and actual cache size.
**Fix:** Moved mutex acquisition to BEFORE updating `m_pdfCacheCapacity`, ensuring the capacity update and cache eviction happen atomically.

```cpp
// Before (race condition possible):
if (m_pdfCacheCapacity != newCapacity) {
    m_pdfCacheCapacity = newCapacity;  // Not protected!
    QMutexLocker locker(&m_pdfCacheMutex);
    evictFurthestCacheEntries();
}

// After (thread-safe):
QMutexLocker locker(&m_pdfCacheMutex);
if (m_pdfCacheCapacity != newCapacity) {
    m_pdfCacheCapacity = newCapacity;  // Protected
    evictFurthestCacheEntries();
}
```

### CR-2C.3: Not Fixed (Acceptable)
**Issue:** Fallback loop in `updateCurrentPageIndex()` iterates through all pages when none are visible (O(n)).
**Decision:** Left as-is. This case is rare (only happens when viewport is completely outside document bounds) and document size is typically manageable. The overhead is negligible.
