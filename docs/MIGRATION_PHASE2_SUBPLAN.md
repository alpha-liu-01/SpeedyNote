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

---

## Prerequisites

- [x] Phase 1.1: Page, VectorLayer, VectorStroke classes
- [x] Phase 1.2: Document class
- [x] Phase 1.3: DocumentViewport with input routing
- [x] VectorCanvas: Working drawing prototype (reference implementation)

---

## Architecture Comparison

### VectorCanvas (Current - Single Page Overlay)
```
VectorCanvas
├── strokes: QVector<VectorStroke>      // Owns strokes directly
├── currentStroke: VectorStroke         // In-progress stroke
├── undoStack/redoStack: QStack         // Single undo stack
├── strokeCache: QPixmap                // Single cache
└── Renders to self (QWidget)
```

### DocumentViewport (Target - Multi-Page)
```
DocumentViewport
├── m_document: Document*
│   └── pages: vector<Page>
│       └── vectorLayers: vector<VectorLayer>
│           └── strokes: vector<VectorStroke>   // Strokes live here
├── m_currentStroke: VectorStroke               // In-progress stroke
├── m_pageUndoStacks: map<int, UndoStack>       // Per-page undo
├── m_currentStrokeCache: QPixmap               // For incremental rendering
└── Renders pages via Page::render() + VectorLayer cache
```

---

## Task Breakdown

### Task 2.1: Tool State Management (~100 lines) ✅ COMPLETE

**Files:** `source/core/DocumentViewport.h`, `source/core/DocumentViewport.cpp`

**Goal:** Add tool state to DocumentViewport so MainWindow can set pen/eraser mode.

**Implemented:**
- Added `#include "ToolType.h"` for tool enum
- Added `setCurrentTool()`, `currentTool()` for tool selection
- Added `setPenColor()`, `penColor()` for pen color
- Added `setPenThickness()`, `penThickness()` for pen size
- Added `setEraserSize()`, `eraserSize()` for eraser radius
- Added `toolChanged(ToolType)` signal
- All setters have bounds checking and change detection

**Add to DocumentViewport.h:**
```cpp
// Tool state (from ToolType.h)
#include "ToolType.h"

// In class:
public:
    // Tool management
    void setCurrentTool(ToolType tool);
    ToolType currentTool() const { return m_currentTool; }
    
    void setPenColor(const QColor& color);
    QColor penColor() const { return m_penColor; }
    
    void setPenThickness(qreal thickness);
    qreal penThickness() const { return m_penThickness; }
    
    void setEraserSize(qreal size);
    qreal eraserSize() const { return m_eraserSize; }

signals:
    void toolChanged(ToolType tool);

private:
    ToolType m_currentTool = ToolType::Pen;
    QColor m_penColor = Qt::black;
    qreal m_penThickness = 5.0;
    qreal m_eraserSize = 20.0;
```

**Implementation:**
- Simple setters that store state and emit signals
- `setCurrentTool()` also updates eraser cursor visibility

**Deliverable:** MainWindow can set tool state on DocumentViewport

---

### Task 2.2: Stroke Creation (~250 lines) ✅ COMPLETE

**Files:** `source/core/DocumentViewport.h`, `source/core/DocumentViewport.cpp`

**Goal:** Implement actual stroke drawing in DocumentViewport.

**Implemented:**
- Added `m_currentStroke`, `m_isDrawing`, `MIN_DISTANCE_SQ` members
- Added `startStroke()`, `continueStroke()`, `finishStroke()`, `addPointToStroke()` methods
- Point decimation (1.5px threshold) to reduce points by 50-70%
- Pressure peak preservation when skipping points
- Updated `handlePointerPress/Move/Release` to call stroke methods
- Added in-progress stroke rendering in `paintEvent()` using `VectorLayer::renderStroke()`
- Strokes are stored in `Page->activeLayer()` on completion
- **ZOOM-AWARE CACHING**: VectorLayer stroke cache now includes zoom level
  - Cache built at `pageSize * zoom * dpr` physical pixels
  - Sharp rendering at any zoom level without aliasing
  - Auto-rebuilds when zoom changes (lazy invalidation)

**Key code to migrate from VectorCanvas:**
1. `addPoint()` with point decimation
2. `finishStroke()` to complete and store stroke
3. Stroke initialization on press

**Add to DocumentViewport.h:**
```cpp
private:
    // Current stroke being drawn
    VectorStroke m_currentStroke;
    bool m_isDrawing = false;
    int m_lastRenderedPointIndex = 0;
    
    // Point decimation threshold (squared distance)
    static constexpr qreal MIN_DISTANCE_SQ = 1.5 * 1.5;  // 1.5 pixels
    
    // Drawing methods
    void startStroke(const PointerEvent& pe);
    void continueStroke(const PointerEvent& pe);
    void finishStroke();
    void addPointToStroke(const QPointF& pagePos, qreal pressure);
```

**Implementation (migrate from VectorCanvas):**

```cpp
void DocumentViewport::startStroke(const PointerEvent& pe) {
    if (!m_document || !pe.pageHit.valid()) return;
    if (m_currentTool != ToolType::Pen && m_currentTool != ToolType::VectorPen) return;
    
    m_isDrawing = true;
    m_activeDrawingPage = pe.pageHit.pageIndex;
    
    // Initialize new stroke
    m_currentStroke = VectorStroke();
    m_currentStroke.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_currentStroke.color = m_penColor;
    m_currentStroke.baseThickness = m_penThickness;
    
    // Reset incremental rendering
    m_lastRenderedPointIndex = 0;
    resetCurrentStrokeCache();
    
    // Add first point
    addPointToStroke(pe.pageHit.pagePoint, pe.pressure);
}

void DocumentViewport::addPointToStroke(const QPointF& pagePos, qreal pressure) {
    // Point decimation - skip points too close together
    if (!m_currentStroke.points.isEmpty()) {
        const QPointF& lastPos = m_currentStroke.points.last().pos;
        qreal dx = pagePos.x() - lastPos.x();
        qreal dy = pagePos.y() - lastPos.y();
        qreal distSq = dx * dx + dy * dy;
        
        if (distSq < MIN_DISTANCE_SQ) {
            // Update pressure if higher (preserve peaks)
            if (pressure > m_currentStroke.points.last().pressure) {
                m_currentStroke.points.last().pressure = pressure;
            }
            return;
        }
    }
    
    StrokePoint pt;
    pt.pos = pagePos;
    pt.pressure = qBound(0.1, pressure, 1.0);
    m_currentStroke.points.append(pt);
    
    // Request repaint for dirty region
    updateStrokeDirtyRegion();
}

void DocumentViewport::finishStroke() {
    if (m_currentStroke.points.isEmpty()) {
        m_isDrawing = false;
        return;
    }
    
    // Finalize stroke
    m_currentStroke.updateBoundingBox();
    
    // Add to page's active layer
    Page* page = m_document->page(m_activeDrawingPage);
    if (page && page->activeLayer()) {
        page->activeLayer()->addStroke(m_currentStroke);
        
        // Push to undo stack
        pushUndoAction(m_activeDrawingPage, UndoAction::AddStroke, m_currentStroke);
    }
    
    // Clear state
    m_currentStroke = VectorStroke();
    m_isDrawing = false;
    m_lastRenderedPointIndex = 0;
    
    emit documentModified();
    update();
}
```

**Update handlePointerPress/Move/Release:**
```cpp
void DocumentViewport::handlePointerPress(const PointerEvent& pe) {
    // ... existing code ...
    
    if (m_currentTool == ToolType::Pen || m_currentTool == ToolType::VectorPen) {
        startStroke(pe);
    } else if (m_currentTool == ToolType::Eraser || m_currentTool == ToolType::VectorEraser) {
        eraseAt(pe);
    }
}

void DocumentViewport::handlePointerMove(const PointerEvent& pe) {
    // ... existing code ...
    
    if (m_isDrawing && (m_currentTool == ToolType::Pen || m_currentTool == ToolType::VectorPen)) {
        // Continue stroke on active page (don't switch pages mid-stroke)
        QPointF pagePos = getPageLocalPos(pe, m_activeDrawingPage);
        continueStroke(pagePos, pe.pressure);
    } else if (m_currentTool == ToolType::Eraser || m_currentTool == ToolType::VectorEraser) {
        eraseAt(pe);
    }
}

void DocumentViewport::handlePointerRelease(const PointerEvent& pe) {
    if (m_isDrawing) {
        finishStroke();
    }
    // ... existing code ...
}
```

**Deliverable:** Can draw pressure-sensitive strokes on any page

---

### Task 2.3: Incremental Stroke Rendering (~150 lines) ✅ COMPLETE

**Files:** `source/core/DocumentViewport.h`, `source/core/DocumentViewport.cpp`

**Goal:** Render in-progress stroke efficiently (not redrawing entire stroke each frame).

**Implemented:**
- Added `m_currentStrokeCache` QPixmap for caching rendered segments
- Added `m_lastRenderedPointIndex` to track progress
- Added `m_cacheZoom`, `m_cachePan` to detect transform changes
- `resetCurrentStrokeCache()` - creates viewport-sized transparent cache
- `renderCurrentStrokeIncremental()` - renders only NEW segments to cache
- Cache auto-invalidates on resize, pan, or zoom changes during drawing
- End cap always rendered fresh (follows current position)
- Integrated into `paintEvent()` and `startStroke()`/`finishStroke()`

**Add to DocumentViewport.h:**
```cpp
private:
    // Incremental rendering cache for current stroke
    QPixmap m_currentStrokeCache;
    
    void resetCurrentStrokeCache();
    void renderCurrentStrokeIncremental(QPainter& painter);
    void updateStrokeDirtyRegion();
```

**Implementation (migrate from VectorCanvas):**

```cpp
void DocumentViewport::resetCurrentStrokeCache() {
    qreal dpr = devicePixelRatioF();
    QSize physicalSize = size() * dpr;
    
    m_currentStrokeCache = QPixmap(physicalSize);
    m_currentStrokeCache.setDevicePixelRatio(dpr);
    m_currentStrokeCache.fill(Qt::transparent);
    m_lastRenderedPointIndex = 0;
}

void DocumentViewport::renderCurrentStrokeIncremental(QPainter& painter) {
    const int n = m_currentStroke.points.size();
    if (n < 1 || m_activeDrawingPage < 0) return;
    
    // Ensure cache is valid
    QSize expectedSize = size() * devicePixelRatioF();
    if (m_currentStrokeCache.isNull() || m_currentStrokeCache.size() != expectedSize) {
        resetCurrentStrokeCache();
    }
    
    // Render new segments to cache
    if (n > m_lastRenderedPointIndex && n >= 2) {
        QPainter cachePainter(&m_currentStrokeCache);
        cachePainter.setRenderHint(QPainter::Antialiasing, true);
        
        // Transform to page coordinates in viewport space
        QPointF pagePos = pagePosition(m_activeDrawingPage);
        cachePainter.translate(-m_panOffset * m_zoomLevel);
        cachePainter.scale(m_zoomLevel, m_zoomLevel);
        cachePainter.translate(pagePos);
        
        QPen pen(m_currentStroke.color, 1.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        
        int startIdx = qMax(1, m_lastRenderedPointIndex);
        for (int i = startIdx; i < n; ++i) {
            const auto& p0 = m_currentStroke.points[i - 1];
            const auto& p1 = m_currentStroke.points[i];
            
            qreal avgPressure = (p0.pressure + p1.pressure) / 2.0;
            qreal width = qMax(m_currentStroke.baseThickness * avgPressure, 1.0);
            
            pen.setWidthF(width);
            cachePainter.setPen(pen);
            cachePainter.drawLine(p0.pos, p1.pos);
        }
        
        // Start cap
        if (m_lastRenderedPointIndex == 0 && n >= 1) {
            qreal r = qMax(m_currentStroke.baseThickness * m_currentStroke.points[0].pressure, 1.0) / 2.0;
            cachePainter.setPen(Qt::NoPen);
            cachePainter.setBrush(m_currentStroke.color);
            cachePainter.drawEllipse(m_currentStroke.points[0].pos, r, r);
        }
        
        m_lastRenderedPointIndex = n;
    }
    
    // Blit cached current stroke
    painter.drawPixmap(0, 0, m_currentStrokeCache);
    
    // Draw end cap (always updating)
    if (n >= 1) {
        // Transform for end cap
        painter.save();
        QPointF pagePos = pagePosition(m_activeDrawingPage);
        painter.translate(-m_panOffset * m_zoomLevel);
        painter.scale(m_zoomLevel, m_zoomLevel);
        painter.translate(pagePos);
        
        qreal r = qMax(m_currentStroke.baseThickness * m_currentStroke.points[n-1].pressure, 1.0) / 2.0;
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_currentStroke.color);
        painter.drawEllipse(m_currentStroke.points[n-1].pos, r, r);
        painter.restore();
    }
}
```

**Update paintEvent:**
```cpp
void DocumentViewport::paintEvent(QPaintEvent* event) {
    // ... existing page rendering ...
    
    // Draw current stroke being drawn (on top of everything)
    if (m_isDrawing && !m_currentStroke.points.isEmpty()) {
        renderCurrentStrokeIncremental(painter);
    }
    
    // ... debug overlay ...
}
```

**Deliverable:** Smooth drawing at 360Hz without performance degradation

---

### Task 2.4: Eraser Tool (~150 lines) ✅ COMPLETE

**Files:** `source/core/DocumentViewport.h`, `source/core/DocumentViewport.cpp`

**Goal:** Implement stroke-based eraser using VectorLayer's existing hit detection.

**Implemented:**
- Added `eraseAt(const PointerEvent& pe)` - finds and removes strokes at pointer position
- Added `drawEraserCursor(QPainter& painter)` - draws dashed circle at cursor position
- Uses `VectorLayer::strokesAtPoint()` for hit detection with `m_eraserSize` tolerance
- Eraser cursor visible during hover (mouse tracking enabled)
- Stroke cache auto-invalidated on removal
- Undo integration prepared (commented out, Task 2.5)

**Add to DocumentViewport.h:**
```cpp
private:
    void eraseAt(const PointerEvent& pe);
    void drawEraserCursor(QPainter& painter);
```

**Implementation:**

```cpp
void DocumentViewport::eraseAt(const PointerEvent& pe) {
    if (!m_document || !pe.pageHit.valid()) return;
    
    Page* page = m_document->page(pe.pageHit.pageIndex);
    if (!page) return;
    
    VectorLayer* layer = page->activeLayer();
    if (!layer) return;
    
    // Find strokes at eraser position
    QVector<QString> hitIds = layer->strokesAtPoint(pe.pageHit.pagePoint, m_eraserSize);
    
    if (hitIds.isEmpty()) return;
    
    // Collect strokes for undo before removing
    QVector<VectorStroke> removedStrokes;
    for (const QString& id : hitIds) {
        for (const VectorStroke& s : layer->strokes()) {
            if (s.id == id) {
                removedStrokes.append(s);
                break;
            }
        }
    }
    
    // Remove strokes
    for (const QString& id : hitIds) {
        layer->removeStroke(id);
    }
    
    // Push undo action
    if (removedStrokes.size() == 1) {
        pushUndoAction(pe.pageHit.pageIndex, UndoAction::RemoveStroke, removedStrokes[0]);
    } else if (removedStrokes.size() > 1) {
        pushUndoAction(pe.pageHit.pageIndex, UndoAction::RemoveMultiple, removedStrokes);
    }
    
    emit documentModified();
    update();
}

void DocumentViewport::drawEraserCursor(QPainter& painter) {
    if (m_currentTool != ToolType::Eraser && m_currentTool != ToolType::VectorEraser) {
        return;
    }
    
    // Draw eraser circle at last pointer position
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QPen(Qt::gray, 1, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    
    qreal screenRadius = m_eraserSize * m_zoomLevel;
    painter.drawEllipse(m_lastPointerPos, screenRadius, screenRadius);
}
```

**Update paintEvent:**
```cpp
void DocumentViewport::paintEvent(QPaintEvent* event) {
    // ... existing rendering ...
    
    // Draw eraser cursor
    if (underMouse()) {
        drawEraserCursor(painter);
    }
}
```

**Deliverable:** Can erase strokes by touching them with eraser

**Hardware Eraser Fix (Critical):**

Hardware eraser (flipping stylus to eraser end) had multiple issues:

1. **Eraser cursor didn't move when pressed**: `eraseAt()` only called `update()` when strokes were actually removed. When moving over empty space, no repaint happened.
   - **Fix:** Added explicit cursor area updates in both `handlePointerPress` and `handlePointerMove`, regardless of whether strokes were removed.

2. **Hardware eraser detection inconsistent**: Some tablet drivers don't report `pointerType() == Eraser` on every event - only some events have it.
   - **Fix:** If ANY event in a stroke sequence has `pe.isEraser`, set `m_hardwareEraserActive = true`. Only reset on Release. This mirrors the `InkCanvas` pattern.

3. **Alternative eraser detection**: Added fallback detection using device name (some tablets have "eraser" in device name).

**Key code pattern (from InkCanvas):**
```cpp
// In handlePointerMove:
if (pe.isEraser && !m_hardwareEraserActive) {
    m_hardwareEraserActive = true;  // Upgrade to eraser mid-stroke
}

// Always update cursor area when erasing:
qreal eraserRadius = m_eraserSize * m_zoomLevel + 5;
QRectF oldRect(oldPos.x() - eraserRadius, ...);
QRectF newRect(pe.viewportPos.x() - eraserRadius, ...);
update(oldRect.united(newRect).toRect());
```

---

### Task 2.5: Per-Page Undo/Redo System (~200 lines)

**Files:** `source/core/DocumentViewport.h`, `source/core/DocumentViewport.cpp`

**Goal:** Implement undo/redo that works per-page.

**Add to DocumentViewport.h:**
```cpp
// Undo action types
struct UndoAction {
    enum Type { AddStroke, RemoveStroke, RemoveMultiple };
    Type type;
    int pageIndex;
    VectorStroke stroke;              // For single stroke
    QVector<VectorStroke> strokes;    // For multiple strokes
};

// In class:
public:
    void undo();
    void redo();
    bool canUndo() const;
    bool canRedo() const;

signals:
    void undoAvailableChanged(bool available);
    void redoAvailableChanged(bool available);

private:
    // Per-page undo stacks
    QMap<int, QStack<UndoAction>> m_undoStacks;
    QMap<int, QStack<UndoAction>> m_redoStacks;
    static const int MAX_UNDO_PER_PAGE = 50;
    
    void pushUndoAction(int pageIndex, UndoAction::Type type, const VectorStroke& stroke);
    void pushUndoAction(int pageIndex, UndoAction::Type type, const QVector<VectorStroke>& strokes);
    void clearRedoStack(int pageIndex);
```

**Implementation:**

```cpp
void DocumentViewport::pushUndoAction(int pageIndex, UndoAction::Type type, const VectorStroke& stroke) {
    UndoAction action;
    action.type = type;
    action.pageIndex = pageIndex;
    action.stroke = stroke;
    
    m_undoStacks[pageIndex].push(action);
    
    // Limit stack size
    while (m_undoStacks[pageIndex].size() > MAX_UNDO_PER_PAGE) {
        m_undoStacks[pageIndex].removeFirst();
    }
    
    clearRedoStack(pageIndex);
    emit undoAvailableChanged(canUndo());
}

void DocumentViewport::undo() {
    // Undo on current page
    int pageIdx = m_currentPageIndex;
    
    if (!m_undoStacks.contains(pageIdx) || m_undoStacks[pageIdx].isEmpty()) {
        return;
    }
    
    Page* page = m_document ? m_document->page(pageIdx) : nullptr;
    if (!page) return;
    
    VectorLayer* layer = page->activeLayer();
    if (!layer) return;
    
    UndoAction action = m_undoStacks[pageIdx].pop();
    
    switch (action.type) {
        case UndoAction::AddStroke:
            // Remove the added stroke
            layer->removeStroke(action.stroke.id);
            break;
            
        case UndoAction::RemoveStroke:
            // Re-add the removed stroke
            layer->addStroke(action.stroke);
            break;
            
        case UndoAction::RemoveMultiple:
            // Re-add all removed strokes
            for (const auto& s : action.strokes) {
                layer->addStroke(s);
            }
            break;
    }
    
    m_redoStacks[pageIdx].push(action);
    
    emit undoAvailableChanged(canUndo());
    emit redoAvailableChanged(canRedo());
    emit documentModified();
    update();
}

void DocumentViewport::redo() {
    int pageIdx = m_currentPageIndex;
    
    if (!m_redoStacks.contains(pageIdx) || m_redoStacks[pageIdx].isEmpty()) {
        return;
    }
    
    Page* page = m_document ? m_document->page(pageIdx) : nullptr;
    if (!page) return;
    
    VectorLayer* layer = page->activeLayer();
    if (!layer) return;
    
    UndoAction action = m_redoStacks[pageIdx].pop();
    
    switch (action.type) {
        case UndoAction::AddStroke:
            // Re-add the stroke
            layer->addStroke(action.stroke);
            break;
            
        case UndoAction::RemoveStroke:
            // Remove the stroke again
            layer->removeStroke(action.stroke.id);
            break;
            
        case UndoAction::RemoveMultiple:
            // Remove all strokes again
            for (const auto& s : action.strokes) {
                layer->removeStroke(s.id);
            }
            break;
    }
    
    m_undoStacks[pageIdx].push(action);
    
    emit undoAvailableChanged(canUndo());
    emit redoAvailableChanged(canRedo());
    emit documentModified();
    update();
}

bool DocumentViewport::canUndo() const {
    return m_undoStacks.contains(m_currentPageIndex) && 
           !m_undoStacks[m_currentPageIndex].isEmpty();
}

bool DocumentViewport::canRedo() const {
    return m_redoStacks.contains(m_currentPageIndex) && 
           !m_redoStacks[m_currentPageIndex].isEmpty();
}
```

**Add keyboard handling:**
```cpp
void DocumentViewport::keyPressEvent(QKeyEvent* event) {
    if (event->matches(QKeySequence::Undo)) {
        undo();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Redo)) {
        redo();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}
```

**Deliverable:** Ctrl+Z / Ctrl+Y works per-page

---

### Task 2.6: Benchmark Integration (~50 lines)

**Files:** `source/core/DocumentViewport.h`, `source/core/DocumentViewport.cpp`

**Goal:** Add paint rate measurement for performance monitoring.

**Add to DocumentViewport.h:**
```cpp
public:
    void startBenchmark();
    void stopBenchmark();
    int getPaintRate() const;

private:
    bool m_benchmarking = false;
    QElapsedTimer m_benchmarkTimer;
    mutable std::deque<qint64> m_paintTimestamps;
```

**Implementation (copy from VectorCanvas):**
```cpp
void DocumentViewport::startBenchmark() {
    m_benchmarking = true;
    m_paintTimestamps.clear();
    m_benchmarkTimer.start();
}

void DocumentViewport::stopBenchmark() {
    m_benchmarking = false;
}

int DocumentViewport::getPaintRate() const {
    if (!m_benchmarking) return 0;
    
    qint64 now = m_benchmarkTimer.elapsed();
    
    // Remove timestamps older than 1 second
    while (!m_paintTimestamps.empty() && now - m_paintTimestamps.front() > 1000) {
        m_paintTimestamps.pop_front();
    }
    
    return static_cast<int>(m_paintTimestamps.size());
}
```

**Update paintEvent:**
```cpp
void DocumentViewport::paintEvent(QPaintEvent* event) {
    if (m_benchmarking) {
        m_paintTimestamps.push_back(m_benchmarkTimer.elapsed());
    }
    // ... rest of painting ...
}
```

**Deliverable:** Can measure paint refresh rate

---

### Task 2.7: Drawing Tests (~100 lines)

**Files:** `source/core/DocumentViewportTests.h`

**Goal:** Add tests for drawing functionality.

**Tests to add:**
```cpp
static bool testStrokeCreation() {
    // Create viewport with document
    // Simulate pointer press/move/release
    // Verify stroke was added to page's layer
    // Verify stroke has correct points and properties
}

static bool testPointDecimation() {
    // Add many close points
    // Verify decimation reduced point count
    // Verify pressure peaks were preserved
}

static bool testEraser() {
    // Add strokes to layer
    // Simulate eraser at stroke position
    // Verify stroke was removed
}

static bool testUndo() {
    // Draw stroke
    // Undo
    // Verify stroke removed from layer
    // Redo
    // Verify stroke restored
}

static bool testPerPageUndo() {
    // Draw on page 0
    // Switch to page 1
    // Draw on page 1
    // Undo should only affect page 1
    // Switch to page 0
    // Undo should affect page 0
}
```

**Update --test-viewport to include drawing:**
- Enable drawing in test viewport
- Add instructions for manual testing

**Deliverable:** Automated and visual tests pass

---

## Task Summary

### Phase 2A: Core Drawing (Migration from VectorCanvas)

| Task | Description | Est. Lines | Dependencies | Status |
|------|-------------|------------|--------------|--------|
| 2.1 | Tool State Management | 100 | 1.3 | ✅ |
| 2.2 | Stroke Creation | 250 | 2.1 | ✅ |
| 2.3 | Incremental Stroke Rendering | 150 | 2.2 | ✅ |
| 2.4 | Eraser Tool | 150 | 2.1 | ✅ |
| 2.5 | Per-Page Undo/Redo | 200 | 2.2, 2.4 | [ ] |
| 2.6 | Benchmark Integration | 50 | 2.3 | [ ] |
| 2.7 | Drawing Tests | 100 | All above | [ ] |

**Phase 2A Total:** ~1000 lines

### Phase 2B: Additional Tools (New Implementation)

| Task | Description | Est. Lines | Dependencies | Status |
|------|-------------|------------|--------------|--------|
| 2.8 | Marker Tool | ~150 | 2.7 | [ ] |
| 2.9 | Straight Line Mode | ~200 | 2.7 | [ ] |
| 2.10 | Lasso Selection Tool | ~400 | 2.7 | [ ] |
| 2.11 | Highlighter Tool | ~100 | 2.8 | [ ] |

**Phase 2B Total:** ~850 lines (can be deferred)

**Notes on Phase 2B:**
- **Marker (2.8):** Semi-transparent strokes, blending mode
- **Straight Line (2.9):** Draw mode modifier, snap to line from press to release
- **Lasso (2.10):** Selection tool, needs selection state management, copy/paste/delete
- **Highlighter (2.11):** Like marker but with specific highlighting blend mode

Phase 2B tasks are **optional for initial release** - core pen/eraser is sufficient for MVP.

---

## Execution Order

### Phase 2A (Core - Required)
```
2.1 (Tool State) ✅
    ↓
┌───┴───┐
2.2     2.4
(Stroke) (Eraser)
    ↓       
2.3     
(Incremental)
    ↓
    └───┬───┘
        ↓
      2.5
    (Undo/Redo)
        ↓
      2.6
   (Benchmark)
        ↓
      2.7
     (Tests)
```

Tasks 2.2 and 2.4 can be done in parallel after 2.1.

### Phase 2B (Additional Tools - Can be deferred)
```
      2.7 (Tests Complete)
          ↓
    ┌─────┼─────┐
   2.8   2.9   2.10
(Marker)(Line)(Lasso)
    ↓
   2.11
(Highlighter)
```

Phase 2B tasks are independent and can be done in any order after 2.7.

---

## Code Migration Checklist

| VectorCanvas Code | Migrate To | Status |
|-------------------|------------|--------|
| `addPoint()` with decimation | `addPointToStroke()` | [x] |
| `finishStroke()` | `finishStroke()` | [x] |
| `eraseAt()` | `eraseAt()` | [x] |
| `renderCurrentStrokeIncremental()` | Same name | [x] |
| `resetCurrentStrokeCache()` | Same name | [x] |
| `UndoAction` struct | `PageUndoAction` (renamed) | [x] |
| `undo()` / `redo()` | Per-page versions | [x] |
| `startBenchmark()` / `getPaintRate()` | Same | [x] |
| Tool enum | Use `ToolType.h` | [x] |
| Pen color/thickness | Same properties | [x] |

---

## Success Criteria

### Phase 2A Complete When:

1. [ ] Can draw strokes with pressure on any page
2. [ ] Point decimation reduces points by 50%+ with no visible quality loss
3. [ ] Drawing maintains 60+ FPS (360Hz input on capable hardware)
4. [ ] Can erase strokes by touching them
5. [ ] Ctrl+Z / Ctrl+Y works per-page
6. [ ] Strokes persist in Page→VectorLayer→strokes
7. [ ] All tests pass

### Phase 2B Complete When (Optional):

8. [ ] Marker tool draws semi-transparent strokes
9. [ ] Straight line mode snaps pen strokes to lines
10. [ ] Lasso tool can select, move, and delete strokes
11. [ ] Highlighter tool works with proper blend mode

---

## Notes

### VectorCanvas Status
**DO NOT UPDATE VectorCanvas** - It's a test implementation being deleted in Phase 5.
- Keep it working as-is for the transition period
- Keep `VectorPen`/`VectorEraser` enum names for backward compatibility
- DocumentViewport is the replacement - all new code goes there
- Phase 5 will remove VectorCanvas and rename enum values

### ToolType.h Location
Currently at `source/ToolType.h` - will move to `source/core/ToolType.h` in Phase 5 cleanup.
For now, keep it where it is to avoid breaking InkCanvas/MainWindow.

### Performance Targets (Same as VectorCanvas)
- Paint at 60+ FPS when idle
- Handle 360Hz input without lag
- CPU usage < 50% on Celeron N4000 during drawing

### What's NOT in Phase 2A
- Touch gesture drawing (Phase 4)
- Partial stroke eraser (future consideration)
- Multi-layer editing UI (future)

### What's in Phase 2B (Optional)
- Marker tool (semi-transparent strokes)
- Highlighter tool
- Straight line mode
- Lasso selection tool

Phase 2A creates **basic but complete drawing capability** using the new architecture.
Phase 2B adds **enhanced tools** that can be deferred to post-MVP.
