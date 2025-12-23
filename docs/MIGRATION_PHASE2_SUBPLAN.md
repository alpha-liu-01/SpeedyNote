# Phase 2: Drawing Implementation Subplan

## Overview

Phase 2 migrates the working drawing logic from `VectorCanvas` (test overlay) to `DocumentViewport` + `Page` + `VectorLayer` (new architecture).

**Key insight:** This is a **migration**, not a rewrite. VectorCanvas already has:
- Pressure-sensitive stroke creation with point decimation
- Stroke-based eraser with hit detection
- Undo/redo with 50-action stack
- Incremental rendering for performance
- High-DPI support
- Benchmark system

We're moving this proven code to work with the new multi-page, multi-layer architecture.

**Status: Phase 2A COMPLETE** ✅

---

## Prerequisites

- [x] Phase 1.1: Page, VectorLayer, VectorStroke classes
- [x] Phase 1.2: Document class
- [x] Phase 1.3: DocumentViewport with input routing
- [x] VectorCanvas: Working drawing prototype (reference implementation - FROZEN)

---

## Architecture Comparison

### VectorCanvas (Reference - FROZEN, Do Not Modify)
```
VectorCanvas
├── strokes: QVector<VectorStroke>      // Owns strokes directly
├── currentStroke: VectorStroke         // In-progress stroke
├── undoStack/redoStack: QStack         // Single undo stack
├── strokeCache: QPixmap                // Single cache
└── Renders to self (QWidget)
```

### DocumentViewport (Production Implementation)
```
DocumentViewport
├── m_document: Document*
│   └── pages: vector<Page>
│       └── vectorLayers: vector<VectorLayer>
│           └── strokes: vector<VectorStroke>   // Strokes live here
├── m_currentStroke: VectorStroke               // In-progress stroke
├── m_undoStacks: QMap<int, QStack<PageUndoAction>>  // Per-page undo
├── m_redoStacks: QMap<int, QStack<PageUndoAction>>  // Per-page redo
├── m_currentStrokeCache: QPixmap               // For incremental rendering
└── Renders pages via Page::render() + VectorLayer zoom-aware cache
```

---

## Task Breakdown

### Task 2.1: Tool State Management ✅ COMPLETE

**Files:** `source/core/DocumentViewport.h`, `source/core/DocumentViewport.cpp`

**Goal:** Add tool state to DocumentViewport so MainWindow can set pen/eraser mode.

**Implemented Members:**
```cpp
// Private members
ToolType m_currentTool = ToolType::Pen;   // Current drawing tool
QColor m_penColor = Qt::black;            // Pen color for drawing
qreal m_penThickness = 5.0;               // Pen thickness in document units
qreal m_eraserSize = 20.0;                // Eraser radius in document units

// Public methods
void setCurrentTool(ToolType tool);
ToolType currentTool() const;
void setPenColor(const QColor& color);
QColor penColor() const;
void setPenThickness(qreal thickness);
qreal penThickness() const;
void setEraserSize(qreal size);
qreal eraserSize() const;

// Signals
void toolChanged(ToolType tool);
```

**Key Implementation Details:**
- `setCurrentTool()` triggers repaint and emits `toolChanged()`
- Debug overlay shows current tool name and hardware eraser status
- Keyboard shortcuts: `P` for Pen, `E` for Eraser (in test viewport)

**Deliverable:** MainWindow can set tool state on DocumentViewport ✅

---

### Task 2.2: Stroke Creation ✅ COMPLETE

**Files:** `source/core/DocumentViewport.h`, `source/core/DocumentViewport.cpp`

**Goal:** Implement actual stroke drawing in DocumentViewport.

**Implemented Members:**
```cpp
// Private members
VectorStroke m_currentStroke;             // Stroke currently being drawn
bool m_isDrawing = false;                 // True while actively drawing
static constexpr qreal MIN_DISTANCE_SQ = 1.5 * 1.5;  // Point decimation threshold

// Private methods
void startStroke(const PointerEvent& pe);
void continueStroke(const PointerEvent& pe);
void finishStroke();
void addPointToStroke(const QPointF& pagePos, qreal pressure);
```

**Key Implementation Details:**

1. **Point Decimation:** Points closer than 1.5px are skipped (reduces point count by 50-70%)
2. **Pressure Peak Preservation:** When skipping points, pressure is updated if higher
3. **Zoom-Aware Caching:** VectorLayer stroke cache includes zoom level
   - Cache built at `pageSize * zoom * dpr` physical pixels
   - Sharp rendering at any zoom level without aliasing
   - Auto-rebuilds when zoom changes (lazy invalidation via `renderWithZoomCache()`)

**Stroke Creation Flow:**
```
handlePointerPress() → startStroke()
    ├── Initialize m_currentStroke (id, color, thickness)
    ├── resetCurrentStrokeCache()
    └── addPointToStroke()

handlePointerMove() → continueStroke()
    └── addPointToStroke() (with decimation)
        └── update(dirtyRect) (targeted repaint)

handlePointerRelease() → finishStroke()
    ├── m_currentStroke.updateBoundingBox()
    ├── page->activeLayer()->addStroke()
    ├── pushUndoAction(AddStroke)
    └── emit documentModified()
```

**Deliverable:** Can draw pressure-sensitive strokes on any page ✅

---

### Task 2.3: Incremental Stroke Rendering ✅ COMPLETE

**Files:** `source/core/DocumentViewport.h`, `source/core/DocumentViewport.cpp`

**Goal:** Render in-progress stroke efficiently (not redrawing entire stroke each frame).

**Implemented Members:**
```cpp
// Private members
QPixmap m_currentStrokeCache;             // Cache for in-progress stroke segments
int m_lastRenderedPointIndex = 0;         // Index of last point rendered to cache
qreal m_cacheZoom = 1.0;                  // Zoom level when cache was built
QPointF m_cachePan;                       // Pan offset when cache was built

// Private methods
void resetCurrentStrokeCache();
void renderCurrentStrokeIncremental(QPainter& painter);
```

**Key Implementation Details:**

1. **Cache Creation:** `resetCurrentStrokeCache()` creates viewport-sized transparent pixmap
2. **Incremental Rendering:** Only NEW segments are rendered to cache (since last call)
3. **Transform Handling:** Cache is built in viewport coordinates with proper pan/zoom transform
4. **End Cap:** Always rendered fresh (follows current position)
5. **Cache Invalidation:** Auto-invalidates on resize, pan changes, or zoom changes during drawing

**Rendering Algorithm:**
```cpp
// In paintEvent() after page rendering:
if (m_isDrawing && !m_currentStroke.points.isEmpty()) {
    renderCurrentStrokeIncremental(painter);
}

// renderCurrentStrokeIncremental():
1. Validate cache size (recreate if viewport resized)
2. Render new segments [m_lastRenderedPointIndex, n) to cache
3. Blit cache to painter
4. Draw end cap at current position
```

**Performance Results:**
- Maintains 360Hz input on capable hardware
- CPU usage ~18% on Celeron N4000 during drawing

**Deliverable:** Smooth drawing at 360Hz without performance degradation ✅

---

### Task 2.4: Eraser Tool ✅ COMPLETE

**Files:** `source/core/DocumentViewport.h`, `source/core/DocumentViewport.cpp`

**Goal:** Implement stroke-based eraser using VectorLayer's existing hit detection.

**Implemented Members:**
```cpp
// Private members
bool m_hardwareEraserActive = false;      // True when stylus eraser end is being used

// Private methods
void eraseAt(const PointerEvent& pe);
void drawEraserCursor(QPainter& painter);
```

**Key Implementation Details:**

1. **Hit Detection:** Uses `VectorLayer::strokesAtPoint(pagePoint, m_eraserSize)`
2. **Stroke Removal:** `layer->removeStroke(id)` (auto-invalidates stroke cache)
3. **Undo Integration:** Removed strokes are captured for undo before deletion
4. **Cursor Rendering:** Dashed gray circle at `m_lastPointerPos`, scaled by zoom

**Erasing Flow:**
```cpp
// In handlePointerPress/Move:
bool isErasing = m_hardwareEraserActive || m_currentTool == ToolType::Eraser;
if (isErasing) {
    eraseAt(pe);
    // Always update cursor area (even if no strokes removed)
    update(cursorRect);
}
```

**Hardware Eraser Fix (Critical):**

Hardware eraser (flipping stylus to eraser end) had multiple issues that were fixed:

1. **Eraser cursor didn't move when pressed:**
   - **Root cause:** `eraseAt()` only called `update()` when strokes were removed
   - **Fix:** Added explicit cursor area updates in both `handlePointerPress` and `handlePointerMove`, regardless of whether strokes were removed

2. **Hardware eraser detection inconsistent:**
   - **Root cause:** Some tablet drivers don't report `pointerType() == Eraser` on every event
   - **Fix:** If ANY event in a stroke sequence has `pe.isEraser`, set `m_hardwareEraserActive = true`. Only reset on Release.
   ```cpp
   // In handlePointerMove:
   if (pe.isEraser && !m_hardwareEraserActive) {
       m_hardwareEraserActive = true;  // Upgrade to eraser mid-stroke
   }
   ```

3. **Alternative eraser detection:**
   - Added fallback detection using device name (some tablets have "eraser" in device name)
   ```cpp
   // In tabletToPointerEvent:
   if (!pe.isEraser && device->name().contains("eraser", Qt::CaseInsensitive)) {
       pe.isEraser = true;
   }
   ```

**Eraser Cursor Update Pattern:**
```cpp
// Always update cursor region for smooth visual tracking:
qreal eraserRadius = m_eraserSize * m_zoomLevel + 5;
QRectF oldRect(oldPos.x() - eraserRadius, oldPos.y() - eraserRadius, ...);
QRectF newRect(pe.viewportPos.x() - eraserRadius, pe.viewportPos.y() - eraserRadius, ...);
update(oldRect.united(newRect).toRect());
```

**Deliverable:** Can erase strokes by touching them with eraser ✅

---

### Task 2.5: Per-Page Undo/Redo System ✅ COMPLETE

**Files:** `source/core/DocumentViewport.h`, `source/core/DocumentViewport.cpp`

**Goal:** Implement undo/redo that works per-page.

**Implemented Members:**
```cpp
// Undo action struct (named PageUndoAction to avoid conflict with VectorCanvas::UndoAction)
struct PageUndoAction {
    enum Type { AddStroke, RemoveStroke, RemoveMultiple };
    Type type;
    int pageIndex;                      // The page this action occurred on
    VectorStroke stroke;                // For AddStroke and RemoveStroke
    QVector<VectorStroke> strokes;      // For RemoveMultiple
};

// Private members
QMap<int, QStack<PageUndoAction>> m_undoStacks;  // Per-page undo stacks
QMap<int, QStack<PageUndoAction>> m_redoStacks;  // Per-page redo stacks
static const int MAX_UNDO_PER_PAGE = 50;         // Max undo actions per page

// Public methods
void undo();
void redo();
bool canUndo() const;
bool canRedo() const;

// Signals
void undoAvailableChanged(bool available);
void redoAvailableChanged(bool available);

// Private helper methods
void pushUndoAction(int pageIndex, PageUndoAction::Type type, const VectorStroke& stroke);
void pushUndoAction(int pageIndex, PageUndoAction::Type type, const QVector<VectorStroke>& strokes);
void clearRedoStack(int pageIndex);
void trimUndoStack(int pageIndex);
```

**Key Implementation Details:**

1. **Per-Page Stacks:** Each page has its own undo/redo stack
2. **Stack Size Limit:** `trimUndoStack()` removes oldest actions when > MAX_UNDO_PER_PAGE
3. **Redo Clearing:** `clearRedoStack()` called when new actions occur
4. **Signal Emission:** `undoAvailableChanged` and `redoAvailableChanged` emitted on state changes

**Undo/Redo Logic:**
```cpp
void DocumentViewport::undo() {
    int pageIdx = m_currentPageIndex;
    if (!canUndo()) return;
    
    PageUndoAction action = m_undoStacks[pageIdx].pop();
    VectorLayer* layer = page->activeLayer();
    
    switch (action.type) {
        case PageUndoAction::AddStroke:
            layer->removeStroke(action.stroke.id);  // Undo add = remove
            break;
        case PageUndoAction::RemoveStroke:
            layer->addStroke(action.stroke);        // Undo remove = add back
            break;
        case PageUndoAction::RemoveMultiple:
            for (const auto& s : action.strokes) layer->addStroke(s);
            break;
    }
    
    m_redoStacks[pageIdx].push(action);
    // ... emit signals, update
}
```

**Keyboard Shortcuts:**
- `Ctrl+Z` → `undo()`
- `Ctrl+Y` → `redo()`

**Debug Display:** Shows `Undo: Y/N | Redo: Y/N` in overlay

**Deliverable:** Ctrl+Z / Ctrl+Y works per-page ✅

---

### Task 2.6: Benchmark Integration ✅ COMPLETE

**Files:** `source/core/DocumentViewport.h`, `source/core/DocumentViewport.cpp`

**Goal:** Add paint rate measurement for performance monitoring.

**Implemented Members:**
```cpp
// Private members
bool m_benchmarking = false;                      // Whether benchmarking is active
QElapsedTimer m_benchmarkTimer;                   // Timer for measuring intervals
mutable std::deque<qint64> m_paintTimestamps;     // Timestamps of recent paints
QTimer m_benchmarkDisplayTimer;                   // Timer for periodic display updates

// Public methods
void startBenchmark();
void stopBenchmark();
int getPaintRate() const;
bool isBenchmarking() const { return m_benchmarking; }
```

**Key Implementation Details:**

1. **Rate Calculation:** `getPaintRate()` counts timestamps within last 1 second
2. **Display Update Timer:** `m_benchmarkDisplayTimer` triggers periodic `update()` of debug overlay region
   - Without this, dirty region optimization would prevent benchmark display updates
   - Timer interval: 1000ms for debug overlay region only
3. **Integration:** `paintEvent()` records timestamps when benchmarking

**Benchmark Activation:**
- Keyboard shortcut: `B` to toggle benchmark
- Debug display: `Paint Rate: XXX Hz` or `Paint Rate: OFF (press B)`

**Performance Results Achieved:**
| Metric | VectorCanvas | DocumentViewport |
|--------|--------------|------------------|
| Max paint rate | 360 Hz | 360 Hz |
| CPU usage (N4000) | ~34% | ~18% |
| Point decimation | ~50-70% | ~50-70% |

**Deliverable:** Can measure paint refresh rate ✅

---

### Task 2.7: Drawing Tests (Manual Testing Complete)

**Status:** Manual testing via `--test-viewport` flag completed.

**Test Results Verified:**
1. ✅ Can draw strokes with pressure on any page
2. ✅ Point decimation reduces points with no visible quality loss
3. ✅ Drawing maintains 360Hz input (CPU ~18% on N4000)
4. ✅ Can erase strokes by touching them
5. ✅ Hardware eraser detection works reliably
6. ✅ Ctrl+Z / Ctrl+Y works per-page
7. ✅ Strokes persist in Page→VectorLayer→strokes
8. ✅ Zoom-aware caching prevents aliasing at all zoom levels

**Test Viewport Features:**
- 5 test pages with different backgrounds
- Debug overlay with tool state, undo/redo status, benchmark
- Keyboard shortcuts: P (Pen), E (Eraser), B (Benchmark), Ctrl+Z/Y (Undo/Redo)
- Ctrl+Wheel for zoom testing

---

## Task Summary

### Phase 2A: Core Drawing (Migration from VectorCanvas) ✅ COMPLETE

| Task | Description | Est. Lines | Dependencies | Status |
|------|-------------|------------|--------------|--------|
| 2.1 | Tool State Management | 100 | 1.3 | ✅ |
| 2.2 | Stroke Creation | 250 | 2.1 | ✅ |
| 2.3 | Incremental Stroke Rendering | 150 | 2.2 | ✅ |
| 2.4 | Eraser Tool | 150 | 2.1 | ✅ |
| 2.5 | Per-Page Undo/Redo | 200 | 2.2, 2.4 | ✅ |
| 2.6 | Benchmark Integration | 50 | 2.3 | ✅ |
| 2.7 | Drawing Tests | 100 | All above | ✅ (Manual) |

**Phase 2A Total:** ~1000 lines (actual implementation)

### Phase 2B: Additional Tools (Deferred to Post-MVP)

| Task | Description | Est. Lines | Dependencies | Status |
|------|-------------|------------|--------------|--------|
| 2.8 | Marker Tool | ~150 | 2.7 | [ ] Deferred |
| 2.9 | Straight Line Mode | ~200 | 2.7 | [ ] Deferred |
| 2.10 | Lasso Selection Tool | ~400 | 2.7 | [ ] Deferred |
| 2.11 | Highlighter Tool | ~100 | 2.8 | [ ] Deferred |

**Phase 2B Notes:**
- **Marker (2.8):** Semi-transparent strokes with blending mode
- **Straight Line (2.9):** Draw mode modifier, snap from press to release
- **Lasso (2.10):** Selection tool with copy/paste/delete/transform
- **Highlighter (2.11):** Like marker with highlighting blend mode

**Decision:** Phase 2B deferred until after Phase 3 (MainWindow Integration). The MainWindow UI makes testing these tools easier.

---

## Performance Optimizations Applied

All optimizations from VectorCanvas have been migrated:

| Optimization | Description | Impact |
|--------------|-------------|--------|
| **Stroke Caching** | `VectorLayer::strokeCache` rendered at zoom-aware resolution | Eliminates aliasing at any zoom |
| **Incremental Rendering** | Only new stroke segments rendered to `m_currentStrokeCache` | 360Hz sustained |
| **Filled Polygon** | Strokes rendered as filled `QPolygonF` with `Qt::WindingFill` | Better GPU utilization |
| **Point Decimation** | Points < 1.5px apart skipped (with pressure peak preservation) | 50-70% fewer points |
| **Dirty Region Updates** | `update(QRect)` for targeted repaints instead of full widget | CPU ~18% vs ~45% |

**Dirty Region Implementation:**
```cpp
// In addPointToStroke():
QRectF dirtyRect = calculateStrokeDirtyRegion(lastPoint, newPoint, thickness);
update(dirtyRect.toRect().adjusted(-2, -2, 2, 2));

// In eraseAt():
qreal eraserRadius = m_eraserSize * m_zoomLevel + 10;
QRectF dirtyRect(vpPos.x() - eraserRadius, vpPos.y() - eraserRadius, ...);
update(dirtyRect.toRect());
```

---

## Code Migration Checklist

| VectorCanvas Code | Migrated To | Status |
|-------------------|-------------|--------|
| `addPoint()` with decimation | `addPointToStroke()` | ✅ |
| `finishStroke()` | `finishStroke()` | ✅ |
| `eraseAt()` | `eraseAt(const PointerEvent&)` | ✅ |
| `renderCurrentStrokeIncremental()` | Same name | ✅ |
| `resetCurrentStrokeCache()` | Same name | ✅ |
| `UndoAction` struct | `PageUndoAction` (renamed) | ✅ |
| `undo()` / `redo()` | Per-page versions | ✅ |
| `startBenchmark()` / `getPaintRate()` | Same | ✅ |
| Tool enum | `source/core/ToolType.h` | ✅ |
| Pen color/thickness | Same properties | ✅ |
| `VectorPen`/`VectorEraser` | Removed (simplified to `Pen`/`Eraser`) | ✅ |

---

## Success Criteria

### Phase 2A Complete ✅

1. ✅ Can draw strokes with pressure on any page
2. ✅ Point decimation reduces points by 50%+ with no visible quality loss
3. ✅ Drawing maintains 360Hz input (CPU ~18% on N4000)
4. ✅ Can erase strokes by touching them
5. ✅ Hardware eraser (stylus eraser end) works reliably
6. ✅ Ctrl+Z / Ctrl+Y works per-page
7. ✅ Strokes persist in Page→VectorLayer→strokes
8. ✅ Zoom-aware caching eliminates aliasing at all zoom levels

### Phase 2B (Deferred)

8. [ ] Marker tool draws semi-transparent strokes
9. [ ] Straight line mode snaps pen strokes to lines
10. [ ] Lasso tool can select, move, and delete strokes
11. [ ] Highlighter tool works with proper blend mode

---

## Key Files Reference

### Core Implementation
- `source/core/DocumentViewport.h` - Class declaration (~850 lines)
- `source/core/DocumentViewport.cpp` - Implementation (~2200 lines)
- `source/core/ToolType.h` - Tool enum (simplified: Pen, Marker, Eraser, Highlighter, Lasso)

### Supporting Classes
- `source/core/Page.h/.cpp` - Page with vector layers
- `source/layers/VectorLayer.h/.cpp` - Vector layer with stroke cache
- `source/strokes/VectorStroke.h/.cpp` - Stroke data structure

### Reference (Frozen)
- `source/VectorCanvas.h/.cpp` - Original prototype (DO NOT MODIFY)

---

## Notes

### VectorCanvas Status
**FROZEN - DO NOT UPDATE**
- Serves as reference implementation only
- Will be removed in Phase 5 cleanup
- DocumentViewport is the production replacement

### ToolType.h
**Moved to `source/core/ToolType.h`**
- Simplified enum: `Pen`, `Marker`, `Eraser`, `Highlighter`, `Lasso`
- Removed `VectorPen`/`VectorEraser` (obsolete)
- InkCanvas.h updated to `#include "core/ToolType.h"`
- MainWindow.h/.cpp updated similarly

### Performance Targets Achieved
| Target | VectorCanvas | DocumentViewport |
|--------|--------------|------------------|
| Paint rate | 360 Hz | 360 Hz ✅ |
| CPU (N4000) | ~34% | ~18% ✅ |
| Zoom aliasing | Yes | No ✅ |

### What's NOT in Phase 2
- Touch gesture drawing (Phase 4)
- Partial stroke eraser (future consideration)
- Multi-layer editing UI (future)
- MainWindow integration (Phase 3)

### Next Steps: Phase 3
Phase 3 will integrate DocumentViewport into MainWindow:
- Replace InkCanvas tabs with DocumentViewport
- Connect toolbar buttons to tool state
- Connect save/load to Document serialization
- Connect scrollbars and zoom controls
