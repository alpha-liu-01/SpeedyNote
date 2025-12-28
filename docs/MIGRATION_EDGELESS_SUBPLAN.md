# Edgeless Mode Implementation Subplan

> **Purpose:** Implement infinite canvas support with tiled architecture
> **Created:** Dec 27, 2024
> **Status:** ✅ DESIGN COMPLETE - Ready for implementation
> **Parent:** MIGRATION_DOC1_SUBPLAN.md (Phase 4)

---

## Overview

Edgeless mode provides an infinite canvas for freeform note-taking. Unlike paged mode (vertical stack of fixed-size pages), edgeless mode allows drawing anywhere on a 2D plane that extends infinitely in all directions.

---

## Current State Analysis

### What Exists (Implemented Early, Never Tested)

| Component | Status | Notes |
|-----------|--------|-------|
| `Document::Mode::Edgeless` | ✅ Enum exists | Used in Document creation |
| `Document::createNew(..., Mode::Edgeless)` | ✅ Factory exists | Creates single 4096×4096 page |
| `Document::isEdgeless()` | ✅ Helper exists | Returns `mode == Mode::Edgeless` |
| `Document::edgelessPage()` | ✅ Accessor exists | Returns single page (index 0) |
| Pan offset clamping disabled | ✅ Working | Allows infinite pan in edgeless |
| `Ctrl+Shift+N` shortcut | ❌ Not connected | Needs implementation |

### What's Broken / Missing

| Issue | Severity | Description |
|-------|----------|-------------|
| **Single giant page** | 🔴 CRITICAL | 4096×4096 page doesn't scale |
| **Stroke cache memory** | 🔴 CRITICAL | Cache at page size = 64MB+ at zoom |
| **No tile system** | 🔴 CRITICAL | No sparse 2D storage |
| **No on-demand loading** | 🟡 MEDIUM | All strokes loaded at once |
| **No stroke culling** | 🟡 MEDIUM | Renders all strokes even if off-screen |
| **Serialization** | 🟡 MEDIUM | Doesn't support tile format |
| **Shortcut not connected** | 🟢 LOW | Easy fix |

---

## Architecture Decisions (Agreed)

### 1. Tiled Canvas Architecture

```
Edgeless Canvas = Sparse 2D Grid of Tiles

Each tile:
- Is a standard Page object
- Has fixed size: 1024×1024 pixels
- Created on-demand when user draws
- Removed when empty (optional optimization)

Storage: std::map<std::pair<int,int>, std::unique_ptr<Page>>

Coordinates can be negative (infinite in all directions):
  (-1,-1)  (0,-1)  (1,-1)
  (-1, 0)  (0, 0)  (1, 0)  ← Origin tile
  (-1, 1)  (0, 1)  (1, 1)
```

### 2. Fixed Tile Size

| Property | Value | Rationale |
|----------|-------|-----------|
| Tile size | 1024×1024 | Simple math, reasonable cache size |
| Customizable | No | Changing would break existing documents |
| Cache per tile | ~4MB (zoom 1.0, DPR 1.0) | Bounded and manageable |

### 3. Stroke Storage

| Rule | Description |
|------|-------------|
| **Home tile** | Stroke stored in tile where first point lands |
| **Cross-tile strokes** | Rendered fully (extend beyond tile bounds) |
| **Rendering** | Render visible tiles + 1-tile margin |
| **No clipping** | Strokes render completely, painter clips to viewport |

### 4. Viewport Zoom Limits

**Goal:** At most 4 tiles (2×2 = 2048×2048) visible at once.

**Formula:**
```cpp
qreal minZoom = qMax(
    static_cast<qreal>(width()) / 2048.0,
    static_cast<qreal>(height()) / 2048.0
);
minZoom = qMax(minZoom, 0.1);  // Absolute floor at 10%
```

**Key:** Use Qt's logical pixels (`width()`, `height()`), which are already DPI-adjusted.

| Display | Scaling | Logical Size | Min Zoom |
|---------|---------|--------------|----------|
| 1920×1080 | 100% | 1920×1080 | 94% |
| 1920×1080 | 125% | 1536×864 | 75% |
| 2560×1440 | 150% | 1707×960 | 83% |
| 2048×1536 | 200% | 1024×768 | 50% |
| 3840×2160 | 100% | 3840×2160 | 188% (can't zoom out) |

**Edge case (4K@100%):** Accepted. Users with 4K at 100% scaling are rare, and they still get a usable canvas area. No special handling needed for MVP.

### 5. Serialization Format

```json
{
  "mode": "edgeless",
  "tile_size": 1024,
  "origin_offset": [0, 0],
  "tiles": {
    "0,0": {
      "layers": [...],
      "activeLayerIndex": 0
    },
    "1,0": { ... },
    "-1,2": { ... }
  }
}
```

### 6. Tile Boundary Grid (Debug)

- Optional grid lines at tile boundaries
- Enabled by default during development
- Will be customizable or removed later

---

## Implementation Phases

### Phase E1: Core Tile Infrastructure ✅ COMPLETE

**Goal:** Replace single giant page with sparse tile map.

#### E1.1: Add Tile Storage to Document

**File:** `source/core/Document.h`

```cpp
// New members for edgeless mode
// Uses std::map (not QMap) because unique_ptr is move-only
using TileCoord = std::pair<int,int>;
std::map<TileCoord, std::unique_ptr<Page>> m_tiles;
static constexpr int EDGELESS_TILE_SIZE = 1024;

// New methods
Page* getTile(int tx, int ty) const;
Page* getOrCreateTile(int tx, int ty);
TileCoord tileCoordForPoint(QPointF docPt) const;
QVector<TileCoord> tilesInRect(QRectF docRect) const;
void removeTileIfEmpty(int tx, int ty);
int tileCount() const;
QVector<TileCoord> allTileCoords() const;
```

#### E1.2: Implement Tile Methods

**File:** `source/core/Document.cpp`

```cpp
QPair<int,int> Document::tileCoordForPoint(QPointF docPt) const {
    int tx = static_cast<int>(std::floor(docPt.x() / EDGELESS_TILE_SIZE));
    int ty = static_cast<int>(std::floor(docPt.y() / EDGELESS_TILE_SIZE));
    return {tx, ty};
}

Page* Document::getOrCreateTile(int tx, int ty) {
    QPair<int,int> coord(tx, ty);
    
    if (!m_tiles.contains(coord)) {
        auto tile = std::make_unique<Page>();
        tile->size = QSizeF(EDGELESS_TILE_SIZE, EDGELESS_TILE_SIZE);
        tile->backgroundType = defaultBackgroundType;
        tile->backgroundColor = defaultBackgroundColor;
        tile->gridColor = defaultGridColor;
        tile->gridSpacing = defaultGridSpacing;
        m_tiles[coord] = std::move(tile);
        markModified();
    }
    
    return m_tiles[coord].get();
}

Page* Document::getTile(int tx, int ty) const {
    QPair<int,int> coord(tx, ty);
    auto it = m_tiles.find(coord);
    return (it != m_tiles.end()) ? it->get() : nullptr;
}
```

#### E1.3: Update createNew() for Edgeless

**File:** `source/core/Document.cpp`

```cpp
std::unique_ptr<Document> Document::createNew(const QString& docName, Mode docMode) {
    auto doc = std::make_unique<Document>();
    doc->name = docName;
    doc->mode = docMode;
    
    if (docMode == Mode::Edgeless) {
        // Don't create any tiles - they're created on-demand
        // Just ensure the tile map is empty (default state)
    } else {
        doc->ensureMinimumPages();
    }
    
    return doc;
}
```

#### E1.4: Update edgelessPage() to Return Origin Tile

```cpp
Page* Document::edgelessPage() {
    if (mode != Mode::Edgeless) return nullptr;
    
    // For compatibility, return origin tile (0,0)
    // Creates it if doesn't exist
    return getOrCreateTile(0, 0);
}
```

---

### Phase E2: Viewport Rendering for Edgeless ✅ COMPLETE

**Goal:** Render tiles correctly with cross-tile stroke support.

#### E2.1: Add Tile Rendering Path

**File:** `source/core/DocumentViewport.cpp`

```cpp
void DocumentViewport::paintEvent(QPaintEvent* event) {
    // ... existing code ...
    
    if (m_document->isEdgeless()) {
        renderEdgelessMode(painter);
    } else {
        // ... existing paged rendering ...
    }
}

void DocumentViewport::renderEdgelessMode(QPainter& painter) {
    // Get visible rect in document coordinates
    QRectF viewRect = visibleRect();
    
    // Expand by 1 tile margin for cross-tile strokes
    int margin = Document::EDGELESS_TILE_SIZE;
    QRectF expandedRect = viewRect.adjusted(-margin, -margin, margin, margin);
    
    // Get tiles to render
    QVector<QPair<int,int>> tilesToRender = m_document->tilesInRect(expandedRect);
    
    // Apply view transform
    painter.save();
    painter.translate(-m_panOffset.x() * m_zoomLevel, -m_panOffset.y() * m_zoomLevel);
    painter.scale(m_zoomLevel, m_zoomLevel);
    
    // Render each tile
    for (const auto& coord : tilesToRender) {
        Page* tile = m_document->getTile(coord.first, coord.second);
        if (!tile) continue;
        
        // Calculate tile origin in document coordinates
        QPointF tileOrigin(coord.first * Document::EDGELESS_TILE_SIZE,
                          coord.second * Document::EDGELESS_TILE_SIZE);
        
        painter.save();
        painter.translate(tileOrigin);
        renderTile(painter, tile, coord);
        painter.restore();
    }
    
    // Draw tile boundary grid (debug)
    if (m_showTileBoundaries) {
        drawTileBoundaries(painter, viewRect);
    }
    
    painter.restore();
}
```

#### E2.2: Implement renderTile()

```cpp
void DocumentViewport::renderTile(QPainter& painter, Page* tile, QPair<int,int> coord) {
    QSizeF tileSize = tile->size;
    QRectF tileRect(0, 0, tileSize.width(), tileSize.height());
    
    // 1. Fill background
    painter.fillRect(tileRect, tile->backgroundColor);
    
    // 2. Draw background pattern (Grid/Lines)
    // ... same as renderPage() ...
    
    // 3. Render vector layers (strokes may extend beyond tile bounds - OK!)
    qreal dpr = devicePixelRatioF();
    for (int i = 0; i < tile->layerCount(); ++i) {
        VectorLayer* layer = tile->layer(i);
        if (layer && layer->visible) {
            layer->renderWithZoomCache(painter, tileSize, m_zoomLevel, dpr);
        }
    }
}
```

#### E2.3: Implement drawTileBoundaries() (Debug)

```cpp
void DocumentViewport::drawTileBoundaries(QPainter& painter, QRectF viewRect) {
    int tileSize = Document::EDGELESS_TILE_SIZE;
    
    // Calculate visible tile range
    int minTx = static_cast<int>(std::floor(viewRect.left() / tileSize));
    int maxTx = static_cast<int>(std::ceil(viewRect.right() / tileSize));
    int minTy = static_cast<int>(std::floor(viewRect.top() / tileSize));
    int maxTy = static_cast<int>(std::ceil(viewRect.bottom() / tileSize));
    
    painter.setPen(QPen(QColor(100, 100, 100, 100), 1.0 / m_zoomLevel, Qt::DashLine));
    
    // Vertical lines
    for (int tx = minTx; tx <= maxTx; ++tx) {
        qreal x = tx * tileSize;
        painter.drawLine(QPointF(x, viewRect.top()), QPointF(x, viewRect.bottom()));
    }
    
    // Horizontal lines
    for (int ty = minTy; ty <= maxTy; ++ty) {
        qreal y = ty * tileSize;
        painter.drawLine(QPointF(viewRect.left(), y), QPointF(viewRect.right(), y));
    }
}
```

#### E2.4: Implement Dynamic Min Zoom

```cpp
// In DocumentViewport.h
qreal minZoomForEdgeless() const;

// In DocumentViewport.cpp
qreal DocumentViewport::minZoomForEdgeless() const {
    // Goal: at most 4 tiles (2x2 = 2048x2048) visible
    constexpr qreal maxVisibleSize = 2.0 * Document::EDGELESS_TILE_SIZE;  // 2048
    
    // Use logical pixels (Qt handles DPI automatically)
    qreal minZoomX = static_cast<qreal>(width()) / maxVisibleSize;
    qreal minZoomY = static_cast<qreal>(height()) / maxVisibleSize;
    
    // Take the larger (more restrictive) value, with 10% floor
    return qMax(qMax(minZoomX, minZoomY), 0.1);
}

// Update setZoomLevel() to use dynamic min zoom
void DocumentViewport::setZoomLevel(qreal zoom) {
    // Apply mode-specific minimum zoom
    qreal minZ = (m_document && m_document->isEdgeless()) 
                 ? minZoomForEdgeless() 
                 : MIN_ZOOM;
    
    zoom = qBound(minZ, zoom, MAX_ZOOM);
    
    // ... rest of existing code ...
}

// Also update endZoomGesture() to respect dynamic min zoom
void DocumentViewport::endZoomGesture() {
    // ... existing code ...
    
    qreal minZ = (m_document && m_document->isEdgeless()) 
                 ? minZoomForEdgeless() 
                 : MIN_ZOOM;
    qreal finalZoom = qBound(minZ, m_zoomGesture.targetZoom, MAX_ZOOM);
    
    // ... rest of existing code ...
}
```

---

### Phase E3: Stroke Drawing in Edgeless ✅ COMPLETE (with Stroke Splitting)

**Goal:** Strokes are added to correct tile(s), split at tile boundaries for cache efficiency.

#### E3.1: Stroke Splitting Strategy

When a stroke crosses tile boundaries:
1. Split into separate segments (one per tile)
2. Each segment stored in its home tile
3. Segments share a point at boundaries for visual continuity
4. Each segment gets a unique stroke ID (for undo to work correctly)

**Benefits:**
- ✅ Stroke cache works per-tile (bounded memory)
- ✅ Cross-tile strokes render correctly (each tile renders its part)
- ✅ Undo removes all segments as one action

**Trade-offs:**
- Slight overdraw at boundaries (acceptable per user feedback)

#### E3.2: finishStrokeEdgeless() Implementation

```cpp
void DocumentViewport::finishStrokeEdgeless() {
    // Walk through all points, group by tile
    struct TileSegment { TileCoord coord; QVector<StrokePoint> points; };
    QVector<TileSegment> segments;
    
    TileSegment current;
    current.coord = tileCoordForPoint(m_currentStroke.points.first().pos);
    current.points.append(m_currentStroke.points.first());
    
    for (int i = 1; i < m_currentStroke.points.size(); ++i) {
        TileCoord ptTile = tileCoordForPoint(pt.pos);
        
        if (ptTile != current.coord) {
            // Tile boundary crossed!
            current.points.append(pt);  // Include for overlap
            segments.append(current);
            
            // Start new segment (with overlap point)
            current.coord = ptTile;
            current.points.clear();
            current.points.append(pt);
        } else {
            current.points.append(pt);
        }
    }
    segments.append(current);  // Last segment
    
    // Add each segment to its tile with unique ID
    for (const TileSegment& seg : segments) {
        Page* tile = getOrCreateTile(seg.coord);
        VectorStroke localStroke = m_currentStroke;  // Copy base properties
        localStroke.id = QUuid::createUuid().toString();  // New ID per segment
        // ... convert to tile-local coords and add ...
    }
}
```

---

### Phase E4: Eraser in Edgeless

**Goal:** Eraser works across tile boundaries.

#### E4.1: Update eraseAt() for Edgeless

```cpp
void DocumentViewport::eraseAt(const PointerEvent& pe) {
    if (!m_document) return;
    
    if (m_document->isEdgeless()) {
        eraseAtEdgeless(pe.viewportPos);
    } else {
        // ... existing paged code ...
    }
}

void DocumentViewport::eraseAtEdgeless(QPointF viewportPos) {
    QPointF docPt = viewportToDocument(viewportPos);
    
    // Check home tile + neighboring tiles (for cross-tile strokes)
    QPair<int,int> centerTile = m_document->tileCoordForPoint(docPt);
    
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            int tx = centerTile.first + dx;
            int ty = centerTile.second + dy;
            
            Page* tile = m_document->getTile(tx, ty);
            if (!tile) continue;
            
            VectorLayer* layer = tile->activeLayer();
            if (!layer || layer->locked) continue;
            
            // Convert doc point to tile-local
            QPointF tileOrigin(tx * Document::EDGELESS_TILE_SIZE,
                               ty * Document::EDGELESS_TILE_SIZE);
            QPointF localPt = docPt - tileOrigin;
            
            // Find and erase strokes
            QVector<QString> hitIds = layer->strokesAtPoint(localPt, m_eraserSize);
            // ... erase logic ...
        }
    }
}
```

---

### Phase E5: Serialization

**Goal:** Save/load tiled edgeless documents.

#### E5.1: Update toFullJson() for Edgeless

```cpp
QJsonObject Document::toFullJson() const {
    QJsonObject obj = toJson();
    
    if (mode == Mode::Edgeless) {
        obj["tile_size"] = EDGELESS_TILE_SIZE;
        
        QJsonObject tilesObj;
        for (auto it = m_tiles.begin(); it != m_tiles.end(); ++it) {
            QString key = QString("%1,%2").arg(it.key().first).arg(it.key().second);
            tilesObj[key] = it.value()->toJson();
        }
        obj["tiles"] = tilesObj;
    } else {
        obj["pages"] = pagesToJson();
    }
    
    return obj;
}
```

#### E5.2: Update fromFullJson() for Edgeless

```cpp
std::unique_ptr<Document> Document::fromFullJson(const QJsonObject& obj) {
    auto doc = fromJson(obj);
    if (!doc) return nullptr;
    
    if (doc->mode == Mode::Edgeless) {
        // Load tiles
        QJsonObject tilesObj = obj["tiles"].toObject();
        for (auto it = tilesObj.begin(); it != tilesObj.end(); ++it) {
            // Parse "x,y" key
            QStringList parts = it.key().split(',');
            if (parts.size() != 2) continue;
            
            int tx = parts[0].toInt();
            int ty = parts[1].toInt();
            
            auto tile = Page::fromJson(it.value().toObject());
            if (tile) {
                doc->m_tiles[{tx, ty}] = std::move(tile);
            }
        }
    } else {
        // ... existing paged loading ...
    }
    
    return doc;
}
```

---

### Phase E6: Undo/Redo for Edgeless

**Goal:** Global undo stack for seamless edgeless experience.

#### E6.1: Global Undo Stack

**File:** `source/core/DocumentViewport.h`

```cpp
// Edgeless undo action (stores tile coordinate for applying undo)
struct EdgelessUndoAction {
    QPair<int,int> tileCoord;      // Which tile this action affects
    PageUndoAction::Type type;      // AddStroke, RemoveStroke, RemoveMultiple
    VectorStroke stroke;            // For single stroke actions
    QVector<VectorStroke> strokes;  // For multi-stroke actions
};

// Global stacks for edgeless mode
QStack<EdgelessUndoAction> m_edgelessUndoStack;
QStack<EdgelessUndoAction> m_edgelessRedoStack;
static constexpr int MAX_UNDO_GLOBAL = 100;  // Memory bound
```

#### E6.2: Implement undoEdgeless() and redoEdgeless()

```cpp
void DocumentViewport::undoEdgeless() {
    if (m_edgelessUndoStack.isEmpty()) return;
    
    EdgelessUndoAction action = m_edgelessUndoStack.pop();
    
    // Get the tile this action affects
    Page* tile = m_document->getTile(action.tileCoord.first, action.tileCoord.second);
    if (!tile) return;  // Tile was removed? Skip.
    
    VectorLayer* layer = tile->activeLayer();
    if (!layer) return;
    
    switch (action.type) {
        case PageUndoAction::AddStroke:
            layer->removeStroke(action.stroke.id);
            break;
        case PageUndoAction::RemoveStroke:
            layer->addStroke(action.stroke);
            break;
        case PageUndoAction::RemoveMultiple:
            for (const auto& s : action.strokes) {
                layer->addStroke(s);
            }
            break;
    }
    
    m_edgelessRedoStack.push(action);
    trimEdgelessUndoStack();
    
    emit undoAvailableChanged(canUndo());
    emit redoAvailableChanged(canRedo());
    emit documentModified();
    update();
}

void DocumentViewport::trimEdgelessUndoStack() {
    while (m_edgelessUndoStack.size() > MAX_UNDO_GLOBAL) {
        m_edgelessUndoStack.remove(0);  // Remove oldest
    }
}
```

#### E6.3: Update canUndo() and canRedo()

```cpp
bool DocumentViewport::canUndo() const {
    if (m_document && m_document->isEdgeless()) {
        return !m_edgelessUndoStack.isEmpty();
    }
    // Existing per-page logic for paged mode
    return m_undoStacks.contains(m_currentPageIndex) && 
           !m_undoStacks[m_currentPageIndex].isEmpty();
}
```

---

### Phase E7: Connect Shortcut & Test ✅ COMPLETE (with Fixes)

**Goal:** Basic edgeless mode working end-to-end.

#### E7.1: Bug Fixes During Testing

| Issue | Problem | Fix |
|-------|---------|-----|
| **Pan not working** | `clampPanOffset()` reset pan to (0,0) when `pageCount()==0` (true for edgeless since it uses tiles) | Move edgeless check before `pageCount()` check |
| **Initially blank** | Viewport only rendered existing tiles, so empty canvas was gray | `renderEdgelessMode` now draws default background for ALL visible tile coordinates first |
| **Stroke clipping** | Strokes only appeared in starting tile | Implemented stroke splitting at tile boundaries (see E3.2) |

#### E7.2: Scroll/Pan Controls

For edgeless mode:
- **Mouse wheel**: Vertical pan (same as paged mode)
- **Shift + Mouse wheel**: Horizontal pan (same as paged mode)
- **Future**: Smooth pan with viewport frame caching (like zoom)

#### E7.1: Connect Ctrl+Shift+N

```cpp
// MainWindow.cpp
void MainWindow::newEdgelessDocument() {
    auto doc = Document::createNew("Untitled", Document::Mode::Edgeless);
    m_documentManager->addDocument(std::move(doc));
    // ... create tab ...
}
```

#### E7.2: Test Checklist

- [ ] Ctrl+Shift+N creates edgeless document
- [ ] Drawing creates tiles on-demand
- [ ] Strokes crossing tile boundaries render correctly
- [ ] Pan works infinitely in all directions
- [ ] Zoom limits enforced (max 4 tiles visible)
- [ ] Save/load preserves tiles and strokes
- [ ] Eraser works across tile boundaries
- [ ] Undo/redo works
- [ ] Tile boundary grid visible (debug)

---

## Files to Modify

| File | Changes |
|------|---------|
| `source/core/Document.h` | Add tile storage, tile methods |
| `source/core/Document.cpp` | Implement tile methods, update serialization |
| `source/core/DocumentViewport.h` | Add edgeless rendering methods, undo types |
| `source/core/DocumentViewport.cpp` | Implement edgeless rendering, stroke handling, eraser |
| `source/MainWindow.cpp` | Connect Ctrl+Shift+N shortcut |
| `source/MainWindow.h` | Declare newEdgelessDocument() |

---

## Decisions (All Resolved)

| Question | Decision | Rationale |
|----------|----------|-----------|
| Eraser across tiles | Search home tile + 8 neighbors | Catches cross-tile strokes |
| Undo scope | **Global stack** for edgeless | User mental model: "undo last action anywhere" |
| Origin point | (0,0) = top-left of tile (0,0) | Simple, matches document coordinates |
| Min zoom calculation | `qMax(width, height) / 2048` | Ensures ≤4 tiles visible, uses logical pixels |

### Why Global Undo for Edgeless (Not Per-Tile)

Paged mode uses per-page undo because each page is independent. In edgeless:

1. **Canvas is seamless** - user doesn't think in terms of tiles
2. **User expectation:** "Undo my last stroke" regardless of where they panned
3. **Problem with per-tile:** Draw in (0,0), pan to (2,2), press undo → nothing happens (confusing!)
4. **Memory:** Bounded with `MAX_UNDO_GLOBAL = 100`

```cpp
// Edgeless uses different undo path
void DocumentViewport::undo() {
    if (m_document->isEdgeless()) {
        undoEdgeless();  // Global stack
    } else {
        undoPaged();     // Per-page stack (existing)
    }
}
```

---

## Success Criteria

### MVP (Phase E1-E7)

- [ ] Edgeless document creates successfully
- [ ] Tiles created on-demand when drawing
- [ ] No memory explosion (cache bounded per tile)
- [ ] Cross-tile strokes render correctly
- [ ] Save/load works with tile format
- [ ] Basic undo/redo works

### Future Enhancements (Not in MVP)

- [ ] On-demand tile loading from disk
- [ ] Spatial indexing for large stroke counts
- [ ] Tile boundary grid customization
- [ ] Minimap for navigation
- [ ] Stroke culling within tiles

---

*Edgeless mode subplan for SpeedyNote - Dec 27, 2024*

