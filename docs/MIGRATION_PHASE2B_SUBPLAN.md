# Phase 2B: Additional Drawing Tools - Subplan

## Overview

Phase 2B implements three additional drawing tools that were deferred during the initial DocumentViewport migration:

1. **Marker Tool** - Fixed opacity, fixed thickness drawing tool
2. **Straight Line Mode** - Constrained line drawing (works with Pen/Marker)
3. **Lasso Selection Tool** - Freeform stroke selection with transform operations

**Deferred:** Highlighter Tool (2.11) - requires PDF text box integration

## Architecture Principles

1. **Modular Design**: Each tool is self-contained with clear interfaces
2. **Shared Logic**: Marker and Straight Line reuse pen stroke infrastructure
3. **Dual Mode Compatible**: All tools work in both paged and edgeless modes
4. **Touch-Ready Interfaces**: Leave hooks for future touch/gesture controls
5. **Efficient Rendering**: Minimize CPU usage, especially for lasso animations

---

## 2.8 Marker Tool (~150 lines)

### Design

The Marker is a simplified drawing tool with:
- **Fixed opacity**: 50% (0.5 alpha) - no pressure variation
- **Fixed thickness**: No pressure sensitivity, consistent width
- **Separate color**: Independent from pen color, default #E6FF6E (yellow-green)
- **Blending**: Standard alpha blending (overlap shows darker areas)

### Data Model

Markers are stored as regular `VectorStroke` objects. The stroke's color includes the alpha channel, so no new fields are needed in the stroke structure.

```cpp
// In DocumentViewport (tool state)
QColor m_markerColor = QColor(0xE6, 0xFF, 0x6E, 128);  // #E6FF6E at 50% opacity
qreal m_markerThickness = 8.0;  // Wider than default pen (3.0)
```

### Implementation Tasks

#### 2.8.1 Tool State Extension (~30 lines)
**File:** `source/core/DocumentViewport.h/.cpp`

```cpp
// New members
QColor m_markerColor;
qreal m_markerThickness;

// New methods
void setMarkerColor(const QColor& color);
QColor markerColor() const;
void setMarkerThickness(qreal thickness);
qreal markerThickness() const;
```

#### 2.8.2 Stroke Creation for Marker (~40 lines)
**File:** `source/core/DocumentViewport.cpp`

Modify `handlePointerPress()` and `handlePointerMove()` to use marker settings when `m_currentTool == ToolType::Marker`:

```cpp
// In handlePointerPress / stroke creation:
if (m_currentTool == ToolType::Marker) {
    // Use marker color (includes alpha) and fixed thickness
    m_currentStrokeColor = m_markerColor;
    m_currentStrokeThickness = m_markerThickness;
    // Ignore pressure for thickness
} else if (m_currentTool == ToolType::Pen) {
    m_currentStrokeColor = m_penColor;
    m_currentStrokeThickness = m_penThickness * pressure;
}
```

#### 2.8.3 Stroke Rendering with Opacity (~30 lines)
**File:** `source/core/DocumentViewport.cpp`

The existing stroke rendering already supports alpha in colors. Verify that:
- `VectorStroke::render()` uses the stroke's color alpha correctly
- QPainter composition mode is appropriate (SourceOver is default, should work)

#### 2.8.4 MainWindow Integration (~50 lines)
**File:** `source/MainWindow.cpp`

- Connect existing Marker button to `currentViewport()->setCurrentTool(ToolType::Marker)`
- Document interface for future marker color picker:

```cpp
// Future: Connect marker color picker
// void MainWindow::onMarkerColorChanged(const QColor& color) {
//     if (auto* vp = currentViewport()) {
//         vp->setMarkerColor(color);
//     }
// }
```

### Test Cases
- [ ] Marker strokes render with 50% opacity
- [ ] Marker thickness is consistent (no pressure variation)
- [ ] Marker color is separate from pen color
- [ ] Works in both paged and edgeless modes
- [ ] Marker strokes saved/loaded correctly (alpha preserved)

---

## 2.9 Straight Line Mode (~200 lines)

### Design

Straight Line Mode is a **toggle** that modifies how Pen and Marker strokes are created:
- When enabled, strokes are constrained to straight lines (point A to point B)
- Works with both Pen and Marker tools
- Shows preview line while dragging
- Free angle (no snapping) - architecture allows future ruler/angle snap feature

### State

```cpp
// In DocumentViewport
bool m_straightLineMode = false;
QPointF m_straightLineStart;      // Start point (document coords)
QPointF m_straightLinePreviewEnd; // Current end point for preview
```

### Implementation Tasks

#### 2.9.1 Mode Toggle (~20 lines)
**File:** `source/core/DocumentViewport.h/.cpp`

```cpp
void setStraightLineMode(bool enabled);
bool straightLineMode() const { return m_straightLineMode; }

// Signal for UI sync
signals:
    void straightLineModeChanged(bool enabled);
```

#### 2.9.2 Modified Stroke Creation (~60 lines)
**File:** `source/core/DocumentViewport.cpp`

When straight line mode is active:

```cpp
void DocumentViewport::handlePointerPress(const PointerEvent& event) {
    if (m_straightLineMode && (m_currentTool == ToolType::Pen || 
                                m_currentTool == ToolType::Marker)) {
        // Record start point, don't create stroke yet
        m_straightLineStart = event.pageHit.pagePoint;  // or document coords for edgeless
        m_straightLinePreviewEnd = m_straightLineStart;
        m_isDrawingStraightLine = true;
        return;
    }
    // ... existing pen/marker handling
}

void DocumentViewport::handlePointerMove(const PointerEvent& event) {
    if (m_isDrawingStraightLine) {
        // Update preview end point
        m_straightLinePreviewEnd = event.pageHit.pagePoint;
        update();  // Trigger repaint for preview
        return;
    }
    // ... existing handling
}

void DocumentViewport::handlePointerRelease(const PointerEvent& event) {
    if (m_isDrawingStraightLine) {
        // Create the actual stroke from start to end
        createStraightLineStroke(m_straightLineStart, event.pageHit.pagePoint);
        m_isDrawingStraightLine = false;
        update();
        return;
    }
    // ... existing handling
}
```

#### 2.9.3 Straight Line Stroke Creation (~50 lines)
**File:** `source/core/DocumentViewport.cpp`

```cpp
void DocumentViewport::createStraightLineStroke(QPointF start, QPointF end) {
    // Determine color and thickness based on current tool
    QColor color;
    qreal thickness;
    if (m_currentTool == ToolType::Marker) {
        color = m_markerColor;
        thickness = m_markerThickness;
    } else {
        color = m_penColor;
        thickness = m_penThickness;  // No pressure for straight lines
    }
    
    // Create stroke with just two points (start and end)
    VectorStroke stroke;
    stroke.setColor(color);
    stroke.setThickness(thickness);
    stroke.addPoint(StrokePoint(start.x(), start.y(), 1.0));
    stroke.addPoint(StrokePoint(end.x(), end.y(), 1.0));
    stroke.finalize();
    
    // Add to page/tile (reuse existing logic)
    // For edgeless: handle tile splitting if line crosses boundaries
    if (m_document->isEdgeless()) {
        addEdgelessStroke(stroke);  // Existing method handles splitting
    } else {
        addPagedStroke(stroke);
    }
}
```

#### 2.9.4 Preview Line Rendering (~40 lines)
**File:** `source/core/DocumentViewport.cpp`

In `paintEvent()`, after rendering strokes:

```cpp
// Draw straight line preview
if (m_isDrawingStraightLine) {
    painter.save();
    
    // Transform to viewport coordinates
    QPointF vpStart = documentToViewport(m_straightLineStart);
    QPointF vpEnd = documentToViewport(m_straightLinePreviewEnd);
    
    // Use current tool's color and thickness
    QColor previewColor = (m_currentTool == ToolType::Marker) 
                          ? m_markerColor : m_penColor;
    qreal previewThickness = (m_currentTool == ToolType::Marker)
                             ? m_markerThickness : m_penThickness;
    
    QPen pen(previewColor, previewThickness * m_zoomLevel, 
             Qt::SolidLine, Qt::RoundCap);
    painter.setPen(pen);
    painter.drawLine(vpStart, vpEnd);
    
    painter.restore();
}
```

#### 2.9.5 MainWindow Integration (~30 lines)
**File:** `source/MainWindow.cpp`

Connect existing line tool button:

```cpp
connect(straightLineButton, &QPushButton::toggled, this, [this](bool checked) {
    if (auto* vp = currentViewport()) {
        vp->setStraightLineMode(checked);
    }
});

// Sync button state when viewport changes
connect(m_tabManager, &TabManager::currentViewportChanged, this, [this](DocumentViewport* vp) {
    if (vp && straightLineButton) {
        straightLineButton->setChecked(vp->straightLineMode());
    }
});
```

### Future Extension Points

```cpp
// For future ruler/angle snapping feature:
// enum class LineSnapMode { None, Angle15, Angle45, Ruler };
// LineSnapMode m_lineSnapMode = LineSnapMode::None;
// qreal m_rulerAngle = 0;  // For ruler feature
//
// QPointF snapLineEndpoint(QPointF start, QPointF rawEnd) {
//     if (m_lineSnapMode == LineSnapMode::None) return rawEnd;
//     // ... snapping logic
// }
```

### Test Cases
- [ ] Toggle straight line mode on/off
- [ ] Preview line shows while dragging
- [ ] Final stroke matches preview
- [ ] Works with Pen tool
- [ ] Works with Marker tool (correct color/opacity)
- [ ] Works in paged mode
- [ ] Works in edgeless mode (tile splitting)
- [ ] Undo/redo works for straight line strokes

---

## 2.10 Lasso Selection Tool (~400 lines)

### Design

The Lasso tool allows freeform selection of strokes with transform operations:
- **Selection**: Draw freeform path to select strokes
- **Scope**: Active layer only, entire strokes (not partial)
- **Operations**: Move, Scale, Rotate, Delete, Copy, Cut, Paste
- **Visual**: Marching ants (or static dashed line if performance issue)
- **Handles**: 8 scale handles + rotation handle

### Data Model

```cpp
// In DocumentViewport
struct LassoSelection {
    QVector<VectorStroke> selectedStrokes;  // Copies of selected strokes
    QVector<int> originalIndices;            // Indices in the layer (for removal)
    int sourcePageIndex = -1;                // Source page (paged mode)
    Document::TileCoord sourceTileCoord;     // Source tile (edgeless mode)
    int sourceLayerIndex = 0;
    
    QRectF boundingBox;                      // Selection bounding box
    QPointF transformOrigin;                 // Center for rotate/scale
    qreal rotation = 0;                      // Current rotation angle
    qreal scaleX = 1.0, scaleY = 1.0;        // Current scale factors
    QPointF offset;                          // Move offset
    
    bool isValid() const { return !selectedStrokes.isEmpty(); }
    void clear() { selectedStrokes.clear(); originalIndices.clear(); /* ... */ }
};

LassoSelection m_lassoSelection;
QPolygonF m_lassoPath;           // The selection path being drawn
bool m_isDrawingLasso = false;
```

### Implementation Tasks

#### 2.10.1 Lasso Path Drawing (~50 lines)
**File:** `source/core/DocumentViewport.cpp`

```cpp
void DocumentViewport::handlePointerPress_Lasso(const PointerEvent& event) {
    // If clicking on existing selection, start transform
    if (m_lassoSelection.isValid()) {
        HandleHit hit = hitTestSelectionHandles(event.viewportPos);
        if (hit != HandleHit::None) {
            startSelectionTransform(hit, event.viewportPos);
            return;
        }
        if (m_lassoSelection.boundingBox.contains(event.pageHit.pagePoint)) {
            startSelectionMove(event.viewportPos);
            return;
        }
        // Click outside - clear selection
        clearLassoSelection();
    }
    
    // Start new lasso path
    m_lassoPath.clear();
    m_lassoPath << event.pageHit.pagePoint;
    m_isDrawingLasso = true;
}

void DocumentViewport::handlePointerMove_Lasso(const PointerEvent& event) {
    if (m_isDrawingLasso) {
        m_lassoPath << event.pageHit.pagePoint;
        update();
    } else if (m_isTransformingSelection) {
        updateSelectionTransform(event.viewportPos);
    }
}

void DocumentViewport::handlePointerRelease_Lasso(const PointerEvent& event) {
    if (m_isDrawingLasso) {
        m_lassoPath << event.pageHit.pagePoint;
        finalizeLassoSelection();
        m_isDrawingLasso = false;
    } else if (m_isTransformingSelection) {
        finalizeSelectionTransform();
    }
}
```

#### 2.10.2 Stroke Hit Detection (~60 lines)
**File:** `source/core/DocumentViewport.cpp`

```cpp
void DocumentViewport::finalizeLassoSelection() {
    m_lassoSelection.clear();
    
    // Get strokes from active layer
    VectorLayer* layer = getActiveLayer();  // Helper to get layer for current mode
    if (!layer) return;
    
    const auto& strokes = layer->strokes();
    
    for (int i = 0; i < strokes.size(); ++i) {
        const VectorStroke& stroke = strokes[i];
        
        // Check if stroke intersects with lasso path
        if (strokeIntersectsLasso(stroke, m_lassoPath)) {
            m_lassoSelection.selectedStrokes.append(stroke);
            m_lassoSelection.originalIndices.append(i);
        }
    }
    
    if (m_lassoSelection.isValid()) {
        // Calculate bounding box
        m_lassoSelection.boundingBox = calculateSelectionBoundingBox();
        m_lassoSelection.transformOrigin = m_lassoSelection.boundingBox.center();
        
        // Store source location
        if (m_document->isEdgeless()) {
            m_lassoSelection.sourceTileCoord = currentTileCoord();
        } else {
            m_lassoSelection.sourcePageIndex = m_currentPageIndex;
        }
        m_lassoSelection.sourceLayerIndex = getActiveLayerIndex();
    }
    
    m_lassoPath.clear();
    update();
}

bool DocumentViewport::strokeIntersectsLasso(const VectorStroke& stroke, 
                                              const QPolygonF& lasso) {
    // Check if any point of the stroke is inside the lasso polygon
    for (const auto& point : stroke.points()) {
        if (lasso.containsPoint(QPointF(point.x, point.y), Qt::OddEvenFill)) {
            return true;
        }
    }
    return false;
}
```

#### 2.10.3 Selection Rendering (~80 lines)
**File:** `source/core/DocumentViewport.cpp`

```cpp
void DocumentViewport::renderLassoSelection(QPainter& painter) {
    // Draw lasso path while drawing
    if (m_isDrawingLasso && m_lassoPath.size() > 1) {
        painter.save();
        QPen lassoPen(Qt::blue, 1, Qt::DashLine);
        painter.setPen(lassoPen);
        painter.setBrush(QColor(0, 0, 255, 30));  // Light blue fill
        
        QPolygonF vpPath;
        for (const QPointF& pt : m_lassoPath) {
            vpPath << documentToViewport(pt);
        }
        painter.drawPolygon(vpPath);
        painter.restore();
    }
    
    // Draw selection with transforms applied
    if (m_lassoSelection.isValid()) {
        painter.save();
        
        // Apply current transform
        QTransform transform = buildSelectionTransform();
        
        // Draw selected strokes with transform
        for (const VectorStroke& stroke : m_lassoSelection.selectedStrokes) {
            VectorStroke transformedStroke = stroke.transformed(transform);
            transformedStroke.render(painter, m_zoomLevel, -m_panOffset);
        }
        
        // Draw bounding box (marching ants or dashed)
        drawSelectionBoundingBox(painter, transform);
        
        // Draw handles
        drawSelectionHandles(painter, transform);
        
        painter.restore();
    }
}

void DocumentViewport::drawSelectionBoundingBox(QPainter& painter, 
                                                 const QTransform& transform) {
    QRectF box = m_lassoSelection.boundingBox;
    QPolygonF corners;
    corners << box.topLeft() << box.topRight() 
            << box.bottomRight() << box.bottomLeft();
    corners = transform.map(corners);
    
    // Convert to viewport
    QPolygonF vpCorners;
    for (const QPointF& pt : corners) {
        vpCorners << documentToViewport(pt);
    }
    
    // Marching ants effect (or static dashed line)
    static int dashOffset = 0;
    QPen pen(Qt::black, 1, Qt::DashLine);
    pen.setDashOffset(dashOffset);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(vpCorners);
    
    // Animate marching ants (called from timer if enabled)
    // dashOffset = (dashOffset + 1) % 16;
}
```

#### 2.10.4 Transform Handles (~60 lines)
**File:** `source/core/DocumentViewport.cpp`

```cpp
enum class HandleHit {
    None,
    TopLeft, Top, TopRight,
    Left, Right,
    BottomLeft, Bottom, BottomRight,
    Rotate,  // Above top center
    Inside   // For move
};

void DocumentViewport::drawSelectionHandles(QPainter& painter, 
                                            const QTransform& transform) {
    QRectF box = m_lassoSelection.boundingBox;
    
    // 8 scale handles at corners and edge midpoints
    QVector<QPointF> handlePositions = {
        box.topLeft(), 
        QPointF(box.center().x(), box.top()),
        box.topRight(),
        QPointF(box.left(), box.center().y()),
        QPointF(box.right(), box.center().y()),
        box.bottomLeft(),
        QPointF(box.center().x(), box.bottom()),
        box.bottomRight()
    };
    
    // Rotation handle above top center
    QPointF rotateHandle(box.center().x(), box.top() - 20 / m_zoomLevel);
    
    painter.setPen(Qt::black);
    painter.setBrush(Qt::white);
    
    qreal handleSize = 8;  // pixels
    for (const QPointF& pos : handlePositions) {
        QPointF transformed = transform.map(pos);
        QPointF vp = documentToViewport(transformed);
        painter.drawRect(QRectF(vp.x() - handleSize/2, vp.y() - handleSize/2,
                                handleSize, handleSize));
    }
    
    // Rotation handle (circle)
    QPointF rotateVp = documentToViewport(transform.map(rotateHandle));
    painter.drawEllipse(rotateVp, handleSize/2, handleSize/2);
    
    // Line from top center to rotation handle
    QPointF topCenterVp = documentToViewport(
        transform.map(QPointF(box.center().x(), box.top())));
    painter.drawLine(topCenterVp, rotateVp);
}

HandleHit DocumentViewport::hitTestSelectionHandles(QPointF viewportPos) {
    // Test each handle position (reverse order of drawing for correct z-order)
    // Return HandleHit enum indicating which handle was hit
    // ... implementation
}
```

#### 2.10.5 Transform Operations (~80 lines)
**File:** `source/core/DocumentViewport.cpp`

```cpp
void DocumentViewport::startSelectionTransform(HandleHit handle, QPointF startPos) {
    m_transformHandle = handle;
    m_transformStartPos = startPos;
    m_transformStartBounds = m_lassoSelection.boundingBox;
    m_isTransformingSelection = true;
}

void DocumentViewport::updateSelectionTransform(QPointF currentPos) {
    QPointF delta = currentPos - m_transformStartPos;
    
    switch (m_transformHandle) {
        case HandleHit::Inside:
            // Move
            m_lassoSelection.offset = viewportToDocument(currentPos) - 
                                      viewportToDocument(m_transformStartPos);
            break;
            
        case HandleHit::Rotate: {
            // Rotate around center
            QPointF center = documentToViewport(m_lassoSelection.transformOrigin);
            qreal startAngle = std::atan2(m_transformStartPos.y() - center.y(),
                                          m_transformStartPos.x() - center.x());
            qreal currentAngle = std::atan2(currentPos.y() - center.y(),
                                            currentPos.x() - center.x());
            m_lassoSelection.rotation = (currentAngle - startAngle) * 180 / M_PI;
            break;
        }
            
        case HandleHit::BottomRight:
        case HandleHit::TopLeft:
        // ... other scale handles
            // Calculate scale factors based on handle position
            updateScaleFromHandle(m_transformHandle, currentPos);
            break;
    }
    
    update();
}

QTransform DocumentViewport::buildSelectionTransform() {
    QTransform t;
    QPointF origin = m_lassoSelection.transformOrigin;
    
    // Order: translate to origin, scale, rotate, translate back, apply offset
    t.translate(origin.x(), origin.y());
    t.rotate(m_lassoSelection.rotation);
    t.scale(m_lassoSelection.scaleX, m_lassoSelection.scaleY);
    t.translate(-origin.x(), -origin.y());
    t.translate(m_lassoSelection.offset.x(), m_lassoSelection.offset.y());
    
    return t;
}
```

#### 2.10.6 Apply/Cancel Transform (~40 lines)
**File:** `source/core/DocumentViewport.cpp`

```cpp
void DocumentViewport::finalizeSelectionTransform() {
    m_isTransformingSelection = false;
    // Transform is applied visually; actual stroke modification happens on:
    // - Click elsewhere (apply and clear)
    // - Paste (apply to new location)
    // - Delete (remove originals)
}

void DocumentViewport::applySelectionTransform() {
    if (!m_lassoSelection.isValid()) return;
    
    QTransform transform = buildSelectionTransform();
    
    // Remove original strokes from source layer
    VectorLayer* sourceLayer = getLayerAt(m_lassoSelection.sourcePageIndex,
                                          m_lassoSelection.sourceLayerIndex);
    // Remove in reverse order to maintain indices
    for (int i = m_lassoSelection.originalIndices.size() - 1; i >= 0; --i) {
        sourceLayer->removeStrokeAt(m_lassoSelection.originalIndices[i]);
    }
    
    // Add transformed strokes back
    for (const VectorStroke& stroke : m_lassoSelection.selectedStrokes) {
        VectorStroke transformed = stroke.transformed(transform);
        sourceLayer->addStroke(transformed);
    }
    
    // Push undo action
    // ...
    
    clearLassoSelection();
}
```

#### 2.10.7 Clipboard Operations (~50 lines)
**File:** `source/core/DocumentViewport.cpp`

```cpp
void DocumentViewport::copySelection() {
    if (!m_lassoSelection.isValid()) return;
    
    // Store in clipboard (internal clipboard, not system)
    m_clipboard.strokes = m_lassoSelection.selectedStrokes;
    m_clipboard.transform = buildSelectionTransform();
    m_clipboard.hasContent = true;
    
    // Apply transform to clipboard strokes
    for (VectorStroke& stroke : m_clipboard.strokes) {
        stroke = stroke.transformed(m_clipboard.transform);
    }
}

void DocumentViewport::cutSelection() {
    copySelection();
    deleteSelection();
}

void DocumentViewport::pasteSelection() {
    if (!m_clipboard.hasContent) return;
    
    // Paste at current view center (or last click position)
    QPointF pasteCenter = viewportToDocument(QPointF(width()/2, height()/2));
    QPointF clipboardCenter = calculateBoundingBox(m_clipboard.strokes).center();
    QPointF offset = pasteCenter - clipboardCenter;
    
    // Add strokes to current active layer
    VectorLayer* layer = getActiveLayer();
    for (const VectorStroke& stroke : m_clipboard.strokes) {
        VectorStroke translated = stroke.translated(offset);
        layer->addStroke(translated);
        
        // For edgeless: handle tile placement
        if (m_document->isEdgeless()) {
            // Split stroke across tiles if needed
        }
    }
    
    // Create new selection from pasted strokes
    // ...
    
    update();
    emit documentModified();
}

void DocumentViewport::deleteSelection() {
    if (!m_lassoSelection.isValid()) return;
    
    // Remove original strokes
    // ... (similar to applySelectionTransform but without adding back)
    
    // Push undo action
    // ...
    
    clearLassoSelection();
    update();
    emit documentModified();
}
```

#### 2.10.8 Keyboard Shortcuts (~30 lines)
**File:** `source/core/DocumentViewport.cpp`

```cpp
void DocumentViewport::keyPressEvent(QKeyEvent* event) {
    if (m_currentTool == ToolType::Lasso) {
        if (event->matches(QKeySequence::Copy)) {
            copySelection();
            return;
        }
        if (event->matches(QKeySequence::Cut)) {
            cutSelection();
            return;
        }
        if (event->matches(QKeySequence::Paste)) {
            pasteSelection();
            return;
        }
        if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
            deleteSelection();
            return;
        }
        if (event->key() == Qt::Key_Escape) {
            clearLassoSelection();
            update();
            return;
        }
    }
    
    // ... existing key handling
}
```

#### 2.10.9 MainWindow Integration (~30 lines)
**File:** `source/MainWindow.cpp`

```cpp
// Connect Lasso button
connect(lassoButton, &QPushButton::clicked, this, [this]() {
    if (auto* vp = currentViewport()) {
        vp->setCurrentTool(ToolType::Lasso);
    }
});

// Update tool button states
void MainWindow::updateToolButtonStates() {
    ToolType tool = currentViewport() ? currentViewport()->currentTool() : ToolType::Pen;
    lassoButton->setChecked(tool == ToolType::Lasso);
    // ... other buttons
}
```

### VectorStroke Extensions Required

```cpp
// In source/strokes/VectorStroke.h

// Transform all points by the given transform matrix
VectorStroke transformed(const QTransform& transform) const;

// Translate all points by offset
VectorStroke translated(const QPointF& offset) const;
```

### Future Extension Points

```cpp
// For future touch interface:
// - Two-finger rotate/scale gesture on selection
// - Long-press to show context menu (copy/cut/delete)
// - Pinch to scale selection
//
// Interface hooks:
// void handleTouchSelectionGesture(const GestureState& gesture);
// void showSelectionContextMenu(QPointF position);
```

### Test Cases
- [ ] Draw lasso path to select strokes
- [ ] Only strokes on active layer are selected
- [ ] Entire strokes selected (not partial)
- [ ] Bounding box and handles displayed correctly
- [ ] Move selection by dragging inside
- [ ] Scale selection using corner handles
- [ ] Rotate selection using rotation handle
- [ ] Ctrl+C copies selection
- [ ] Ctrl+X cuts selection
- [ ] Ctrl+V pastes at center of view
- [ ] Delete/Backspace removes selected strokes
- [ ] Escape clears selection
- [ ] Undo/redo works for all operations
- [ ] Works in paged mode
- [ ] Works in edgeless mode (cross-tile paste)

---

## Implementation Order

Recommended order for minimal dependencies:

1. **2.8 Marker Tool** (simplest, builds on existing pen)
   - 2.8.1 Tool state
   - 2.8.2 Stroke creation
   - 2.8.3 Rendering verification
   - 2.8.4 MainWindow integration

2. **2.9 Straight Line Mode** (builds on marker/pen)
   - 2.9.1 Mode toggle
   - 2.9.2-3 Modified stroke creation
   - 2.9.4 Preview rendering
   - 2.9.5 MainWindow integration

3. **2.10 Lasso Selection Tool** (most complex, independent)
   - 2.10.1 Lasso path drawing
   - 2.10.2 Stroke hit detection
   - 2.10.3 Selection rendering
   - 2.10.4 Transform handles
   - 2.10.5-6 Transform operations
   - 2.10.7 Clipboard operations
   - 2.10.8-9 Keyboard shortcuts & MainWindow

---

## Estimated Lines of Code

| Component | Estimated Lines |
|-----------|-----------------|
| 2.8 Marker Tool | ~150 |
| 2.9 Straight Line Mode | ~200 |
| 2.10 Lasso Selection Tool | ~400 |
| VectorStroke extensions | ~50 |
| **Total** | **~800 lines** |

---

## Dependencies

- **DocumentViewport**: All tools implemented here
- **VectorStroke**: Needs `transformed()` and `translated()` methods for Lasso
- **MainWindow**: Button connections (mostly already exist)
- **No new files needed**: All code goes in existing files

---

## Status

| Task | Status |
|------|--------|
| 2.8.1 Marker Tool State | [ ] |
| 2.8.2 Marker Stroke Creation | [ ] |
| 2.8.3 Marker Rendering | [ ] |
| 2.8.4 Marker MainWindow Integration | [ ] |
| 2.9.1 Straight Line Toggle | [ ] |
| 2.9.2 Straight Line Stroke Creation | [ ] |
| 2.9.3 Straight Line for Edgeless | [ ] |
| 2.9.4 Straight Line Preview | [ ] |
| 2.9.5 Straight Line MainWindow Integration | [ ] |
| 2.10.1 Lasso Path Drawing | [ ] |
| 2.10.2 Stroke Hit Detection | [ ] |
| 2.10.3 Selection Rendering | [ ] |
| 2.10.4 Transform Handles | [ ] |
| 2.10.5 Transform Operations | [ ] |
| 2.10.6 Apply/Cancel Transform | [ ] |
| 2.10.7 Clipboard Operations | [ ] |
| 2.10.8 Keyboard Shortcuts | [ ] |
| 2.10.9 Lasso MainWindow Integration | [ ] |
