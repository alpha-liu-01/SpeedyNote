# Edgeless Mode Implementation Subplan

> **Purpose:** Implement infinite canvas support with tiled architecture
> **Created:** Dec 27, 2024
> **Status:** 🔄 IN PROGRESS - Core drawing works, persistence & undo pending
> **Parent:** MIGRATION_DOC1_SUBPLAN.md (Phase 4)

---

## Overview

Edgeless mode provides an infinite canvas for freeform note-taking. Unlike paged mode (vertical stack of fixed-size pages), edgeless mode allows drawing anywhere on a 2D plane that extends infinitely in all directions.

---

## Implementation Progress (Dec 27, 2024)

### ✅ What's Working Now

| Feature | Status | Notes |
|---------|--------|-------|
| **Tile infrastructure** | ✅ Complete | Sparse `std::map<TileCoord, Page>` storage |
| **Tile creation on-demand** | ✅ Complete | Tiles created when user draws |
| **Stroke drawing** | ✅ Complete | Points in document coords, stored in tiles |
| **Stroke splitting at boundaries** | ✅ Complete | Strokes split at tile edges, overlap point for continuity |
| **Pan/scroll** | ✅ Complete | Mouse wheel vertical, Shift+wheel horizontal |
| **Background rendering** | ✅ Complete | Default bg shown for all visible tiles (even empty) |
| **Debug grid overlay** | ✅ Complete | Tile boundaries visible for testing |
| **Min zoom limit** | ✅ Complete | Prevents zooming out too far (≤9 tiles visible) |
| **Ctrl+Shift+N shortcut** | ✅ Complete | Creates new edgeless document |

### ❌ Known Issues (Expected at This Stage)

| Issue | Reason | Resolution |
|-------|--------|------------|
| **Undo doesn't work** | Phase E6 not implemented | Implement after persistence |
| **Memory grows with tiles** | No tile cache eviction | Implement with persistence (E5) |
| **Can't save/load** | Phase E5 not implemented | Next priority |

### 🔧 Bugs Fixed During Testing

| Bug | Root Cause | Fix |
|-----|------------|-----|
| **Pan not working** | `clampPanOffset()` reset pan when `pageCount()==0` | Check `isEdgeless()` before `pageCount()` |
| **Initially blank canvas** | Only rendered existing tiles | Render background for ALL visible tile coords |
| **Strokes clipped at tile boundary** | Stored in first tile only | Implemented stroke splitting |
| **Rendering 16+ tiles** | 1-tile (1024px) margin for backgrounds | Reduced to 100px for strokes, 0 for backgrounds |

---

## Deferred Decisions

### Tile Memory Management

**Decision:** Delay stroke cache eviction until persistence (E5) is implemented.

**Rationale:**
- Stroke cache eviction makes sense paired with dynamic disk loading/unloading
- Without persistence, evicted caches would just need to be regenerated
- Option B chosen: Full tile unloading to disk (not just cache eviction)

**Current behavior:**
- Tiles stay in memory once created
- Stroke caches (~4MB each) accumulate as user draws on more tiles
- Acceptable for MVP testing; will be bounded after E5

### Undo/Redo

**Decision:** Implement undo (E6) after persistence (E5).

**Rationale:**
- Undo needs to store tile coordinates with each action
- Makes more sense to complete the tile system first
- Global undo stack confirmed (not per-tile)

---

## Original State Analysis (Historical)

### What Existed Before Tiled Architecture

| Component | Status | Notes |
|-----------|--------|-------|
| `Document::Mode::Edgeless` | ✅ Enum existed | Used in Document creation |
| `Document::createNew(..., Mode::Edgeless)` | ⚠️ Broken | Created single 4096×4096 page (doesn't scale) |
| `Document::isEdgeless()` | ✅ Helper existed | Returns `mode == Mode::Edgeless` |
| `Document::edgelessPage()` | ⚠️ Broken | Returned single giant page |
| Pan offset clamping disabled | ✅ Working | Allows infinite pan in edgeless |

### Issues Fixed by Tiled Architecture

| Issue | Severity | Resolution |
|-------|----------|------------|
| **Single giant page** | 🔴 CRITICAL | ✅ Now uses sparse tile grid |
| **Stroke cache memory** | 🔴 CRITICAL | ✅ Cache per tile (~4MB), not per canvas |
| **No tile system** | 🔴 CRITICAL | ✅ `std::map<TileCoord, Page>` implemented |
| **No stroke culling** | 🟡 MEDIUM | ✅ Only visible tiles rendered |
| **Serialization** | 🟡 MEDIUM | 🔄 Phase E5 pending |
| **Shortcut not connected** | 🟢 LOW | ✅ Connected |

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

### 4. Viewport Tile Coverage & Zoom Limits

**Tile Coverage Analysis:**

With 1024×1024 tiles on a viewport of size W×H:
- Horizontally: `ceil(W/1024)` tiles, +1 if straddling boundary = up to 3 tiles
- Vertically: `ceil(H/1024)` tiles, +1 if straddling boundary = up to 3 tiles

| Viewport | Best Case (aligned) | Worst Case (straddling) |
|----------|---------------------|-------------------------|
| 1920×1080 | 2×2 = 4 tiles | 3×3 = **9 tiles** |
| 1280×720 | 2×1 = 2 tiles | 3×2 = 6 tiles |
| 2560×1440 | 3×2 = 6 tiles | 4×3 = 12 tiles |

**Min Zoom Formula:**
```cpp
// Allow ~2048 logical pixels of document per dimension
// At worst case, this means up to 9 tiles visible
qreal minZoom = qMax(width(), height()) / 2048.0;
minZoom = qMax(minZoom, 0.1);  // Absolute floor at 10%
```

**Memory Impact:**
- 9 tiles × ~4MB each = ~36MB stroke cache at zoom 1.0, DPR 1.0
- At 2× zoom: ~16MB per tile → ~144MB (zoomed caches are larger)
- Caches are invalidated on zoom change, so only one zoom level's worth is in memory

| Display | Scaling | Logical Size | Min Zoom | Max Tiles |
|---------|---------|--------------|----------|-----------|
| 1920×1080 | 100% | 1920×1080 | 94% | 9 |
| 1920×1080 | 125% | 1536×864 | 75% | 6 |
| 2560×1440 | 150% | 1707×960 | 83% | 9 |
| 2048×1536 | 200% | 1024×768 | 50% | 4 |
| 3840×2160 | 100% | 3840×2160 | 188% | 12 (can't zoom out much) |

**Edge case (4K@100%):** Accepted. Users with 4K at 100% scaling are rare. They get more tiles visible but less zoom-out range.

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

#### E2.1: Two-Pass Rendering Strategy

**File:** `source/core/DocumentViewport.cpp`

With stroke splitting, we use optimized two-pass rendering:

```cpp
void DocumentViewport::renderEdgelessMode(QPainter& painter) {
    QRectF viewRect = visibleRect();
    
    // ========== OPTIMIZED TILE SELECTION ==========
    // With stroke splitting, cross-tile strokes are stored as separate segments.
    // Small margin (100px) handles thick strokes extending beyond tile boundary.
    constexpr int STROKE_MARGIN = 100;
    QRectF strokeRect = viewRect.adjusted(-STROKE_MARGIN, -STROKE_MARGIN, 
                                          STROKE_MARGIN, STROKE_MARGIN);
    
    // Backgrounds: only visible tiles (4-9 for 1920x1080)
    QVector<TileCoord> visibleTiles = m_document->tilesInRect(viewRect);
    
    // Strokes: visible + small margin (for thick stroke edges)
    QVector<TileCoord> strokeTiles = m_document->tilesInRect(strokeRect);
    
    // PASS 1: Backgrounds for visible tiles only (including empty tiles)
    for (const auto& coord : visibleTiles) {
        // Render background color + grid/lines
    }
    
    // PASS 2: Strokes for visible tiles + margin (only existing tiles)
    for (const auto& coord : strokeTiles) {
        Page* tile = m_document->getTile(coord);
        if (!tile) continue;  // Skip empty tiles
        // Render vector layers
    }
    
    // Debug overlay: tile boundaries
    if (m_showTileBoundaries) {
        drawTileBoundaries(painter, viewRect);
    }
}
```

**Performance:**
- Before (1-tile margin): 16+ tiles rendered for backgrounds
- After (optimized): Only 4-9 tiles for backgrounds, 4-9 for strokes

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

### Phase E5: Serialization 🔄 IN PROGRESS

**Goal:** Save/load tiled edgeless documents with O(1) tile access.

#### E5.0a: TEMPORARY - Directory-Based Loading

**Current State (Dec 27, 2024):**

The `.snb` format is currently a **directory** (folder), not a single file:
```
MyDocument.snb/
├── document.json    # Manifest with metadata + tile index
└── tiles/
    ├── 0,0.json     # Individual tile files
    ├── 1,0.json
    └── ...
```

**Why a folder?**
- Enables O(1) tile access (read/write single tile file)
- Easier development/debugging (view tiles in text editor)
- Defers compression/packaging decision

**TEMPORARY Workaround:**
- `Ctrl+Shift+L` opens a **folder picker** to select `.snb` directory
- Regular `Ctrl+O` only works for `.json`/`.snx` (single files)
- This is documented as temporary until unified packaging is implemented

**Future Options:**
1. **Zip package**: Package `.snb/` into `.snb.zip`, rename to `.snb`
2. **SQLite**: Store tiles as blobs in SQLite database (single file)
3. **QDataStream**: Custom binary format (like old `.spn`)
4. **Keep folder**: Some apps (macOS .app, .pages) use folder bundles

**Decision:** Defer packaging until core edgeless features are stable.

#### E5.0: Design - Multi-File Document Bundle

**Problem with single JSON file:**
- All tiles in one file → O(n) parsing to open document
- Loading 1000 tiles just to view 9 → wasteful
- Modifying one tile requires rewriting entire file

**Solution: Document Bundle Format**

```
MyDocument.snb/                    # Directory (SpeedyNote Bundle)
├── document.json                  # Metadata ONLY (no tile content)
│   {
│     "id": "uuid",
│     "name": "My Canvas",
│     "mode": "edgeless",
│     "tile_size": 1024,
│     "default_background": {...},
│     "tile_index": ["0,0", "1,0", "-1,2"]  # Existing tile coords
│   }
└── tiles/
    ├── 0,0.json                   # Individual tile files
    ├── 1,0.json                   # Each ~10-100KB typically
    └── -1,2.json
```

**Complexity Analysis:**

| Operation | Old (single file) | New (bundle) |
|-----------|-------------------|--------------|
| Open document | O(n) parse all tiles | O(1) parse manifest |
| Load visible tiles | Already loaded | O(k) where k = visible tiles |
| Save after edit | O(n) rewrite all | O(1) write dirty tile only |
| Memory (1000 tiles) | All 1000 in RAM | Only visible ~9 in RAM |

#### E5.1: Document Bundle Path Management

**New members in Document.h:**

```cpp
class Document {
    // ... existing ...
    
    // Bundle path management
    QString m_bundlePath;              // Path to .snb directory
    QSet<TileCoord> m_tileIndex;       // All tile coords that exist on disk
    QSet<TileCoord> m_dirtyTiles;      // Tiles modified since last save
    
    // Tile loading
    bool m_lazyLoadEnabled = false;    // True after loading from disk
    
public:
    // Bundle operations
    void setBundlePath(const QString& path);
    QString bundlePath() const;
    
    // Tile persistence
    bool saveTile(TileCoord coord);
    bool loadTileFromDisk(TileCoord coord);
    void markTileDirty(TileCoord coord);
    bool isTileDirty(TileCoord coord) const;
    
    // Tile eviction (for memory management)
    void evictTile(TileCoord coord);        // Save if dirty, remove from memory
    bool isTileLoaded(TileCoord coord) const;
    bool tileExistsOnDisk(TileCoord coord) const;
};
```

#### E5.2: Lazy Tile Loading

**Modified getTile() - Load on demand:**

```cpp
Page* Document::getTile(int tx, int ty) const {
    TileCoord coord(tx, ty);
    
    // 1. Check if already in memory
    auto it = m_tiles.find(coord);
    if (it != m_tiles.end()) {
        return it->second.get();
    }
    
    // 2. If lazy loading enabled, try to load from disk
    if (m_lazyLoadEnabled && m_tileIndex.contains(coord)) {
        // const_cast needed because loading modifies m_tiles
        Document* mutableThis = const_cast<Document*>(this);
        if (mutableThis->loadTileFromDisk(coord)) {
            return m_tiles.at(coord).get();
        }
    }
    
    // 3. Tile doesn't exist
    return nullptr;
}
```

#### E5.3: Tile File Operations

```cpp
bool Document::saveTile(TileCoord coord) {
    auto it = m_tiles.find(coord);
    if (it == m_tiles.end()) return false;
    
    QString tilePath = m_bundlePath + "/tiles/" + 
                       QString("%1,%2.json").arg(coord.first).arg(coord.second);
    
    QFile file(tilePath);
    if (!file.open(QIODevice::WriteOnly)) return false;
    
    QJsonDocument doc(it->second->toJson());
    file.write(doc.toJson(QJsonDocument::Compact));
    
    m_dirtyTiles.remove(coord);
    m_tileIndex.insert(coord);
    return true;
}

bool Document::loadTileFromDisk(TileCoord coord) {
    QString tilePath = m_bundlePath + "/tiles/" + 
                       QString("%1,%2.json").arg(coord.first).arg(coord.second);
    
    QFile file(tilePath);
    if (!file.open(QIODevice::ReadOnly)) return false;
    
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    auto tile = Page::fromJson(doc.object());
    if (!tile) return false;
    
    m_tiles[coord] = std::move(tile);
    return true;
}

void Document::evictTile(TileCoord coord) {
    // Save if dirty
    if (m_dirtyTiles.contains(coord)) {
        saveTile(coord);
    }
    
    // Remove from memory (keeps coord in m_tileIndex)
    m_tiles.erase(coord);
}
```

#### E5.4: Bundle Save/Load

```cpp
// Save entire document bundle
bool Document::saveBundle(const QString& path) {
    m_bundlePath = path;
    
    // Create directory structure
    QDir().mkpath(path + "/tiles");
    
    // Save manifest (document.json)
    QJsonObject manifest = toJson();  // Metadata only
    
    // Build tile index from both memory and disk
    QJsonArray tileIndexArray;
    for (const auto& coord : m_tileIndex) {
        tileIndexArray.append(QString("%1,%2").arg(coord.first).arg(coord.second));
    }
    for (const auto& pair : m_tiles) {
        QString key = QString("%1,%2").arg(pair.first.first).arg(pair.first.second);
        if (!m_tileIndex.contains(pair.first)) {
            tileIndexArray.append(key);
        }
    }
    manifest["tile_index"] = tileIndexArray;
    
    // Write manifest
    QFile manifestFile(path + "/document.json");
    if (!manifestFile.open(QIODevice::WriteOnly)) return false;
    manifestFile.write(QJsonDocument(manifest).toJson());
    
    // Save all dirty tiles
    for (const auto& coord : m_dirtyTiles) {
        saveTile(coord);
    }
    
    // Save tiles that are in memory but not yet on disk
    for (const auto& pair : m_tiles) {
        if (!m_tileIndex.contains(pair.first)) {
            saveTile(pair.first);
        }
    }
    
    m_dirtyTiles.clear();
    return true;
}

// Load document bundle (manifest only, tiles lazy-loaded)
static std::unique_ptr<Document> Document::loadBundle(const QString& path) {
    QFile manifestFile(path + "/document.json");
    if (!manifestFile.open(QIODevice::ReadOnly)) return nullptr;
    
    QJsonDocument jsonDoc = QJsonDocument::fromJson(manifestFile.readAll());
    auto doc = Document::fromJson(jsonDoc.object());
    if (!doc) return nullptr;
    
    doc->m_bundlePath = path;
    doc->m_lazyLoadEnabled = true;
    
    // Parse tile index (no actual tile loading!)
    QJsonArray tileIndexArray = jsonDoc.object()["tile_index"].toArray();
    for (const auto& val : tileIndexArray) {
        QStringList parts = val.toString().split(',');
        if (parts.size() == 2) {
            int tx = parts[0].toInt();
            int ty = parts[1].toInt();
            doc->m_tileIndex.insert({tx, ty});
        }
    }
    
    return doc;  // Tiles loaded on-demand when getTile() is called
}
```

#### E5.5: Integration with DocumentViewport

**Tile eviction during pan:**

```cpp
void DocumentViewport::evictDistantTiles() {
    if (!m_document || !m_document->isEdgeless()) return;
    
    QRectF viewRect = visibleRect();
    constexpr int KEEP_MARGIN = 2;  // Keep tiles within 2 tiles of viewport
    
    QVector<TileCoord> toEvict;
    for (const auto& coord : m_document->allLoadedTileCoords()) {
        QRectF tileRect(coord.first * TILE_SIZE, coord.second * TILE_SIZE,
                        TILE_SIZE, TILE_SIZE);
        
        // Check if tile is far from viewport
        QRectF keepRect = viewRect.adjusted(
            -KEEP_MARGIN * TILE_SIZE, -KEEP_MARGIN * TILE_SIZE,
            KEEP_MARGIN * TILE_SIZE, KEEP_MARGIN * TILE_SIZE);
        
        if (!keepRect.intersects(tileRect)) {
            toEvict.append(coord);
        }
    }
    
    for (const auto& coord : toEvict) {
        m_document->evictTile(coord);
    }
}
```

#### E5.6: File Extension Decision

| Extension | Type | Use Case |
|-----------|------|----------|
| `.snb` | Directory | Edgeless documents (bundle) |
| `.json` | Single file | Paged documents (current format) |

**Future consideration:** Could use `.snb` for both, with paged docs having `pages/` instead of `tiles/`.

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
| Min zoom calculation | `qMax(width, height) / 2048` | Ensures ≤9 tiles visible (worst case), uses logical pixels |
| Rendering margin | 100px for strokes, 0 for backgrounds | Stroke splitting eliminates need for large margin |
| Tile count at 1920×1080 | 4-9 (depending on pan alignment) | Acceptable for memory (~36MB stroke cache) |

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

- [x] Edgeless document creates successfully ✅
- [x] Tiles created on-demand when drawing ✅
- [x] Cross-tile strokes render correctly ✅ (stroke splitting)
- [x] Pan/scroll works infinitely ✅
- [x] Zoom limits enforced (≤9 tiles visible) ✅
- [ ] Save/load works with tile format (E5 pending)
- [ ] Basic undo/redo works (E6 pending)
- [ ] Eraser works across tiles (E4 pending)

### Memory Management (Deferred to E5)

- [ ] Tile stroke cache eviction for distant tiles
- [ ] Full tile unloading to disk (Option B chosen)
- [ ] On-demand tile loading from disk

### Future Enhancements (Post-MVP)

- [ ] Spatial indexing for large stroke counts
- [ ] Tile boundary grid customization
- [ ] Minimap for navigation
- [ ] Stroke culling within tiles

---

## Next Steps

1. **Phase E5: Serialization** - Save/load tile-based documents
2. **Phase E4: Eraser** - Erase strokes across tile boundaries
3. **Phase E6: Undo/Redo** - Global undo stack for edgeless

---

*Edgeless mode subplan for SpeedyNote - Dec 27, 2024*

