# Object Insertion System - Subplan

## Overview

This document covers the implementation of object insertion on pages and tiles (edgeless canvases). The goal is to allow users to insert, manipulate, and manage various objects (images, links, etc.) on their documents.

---

## Current State Analysis

### What Already Exists (Phase 1.1)

The data model and basic infrastructure were created in Phase 1.1.4-1.1.5:

#### 1. InsertedObject Base Class ✅
**File:** `source/objects/InsertedObject.h/.cpp`

| Feature | Status | Notes |
|---------|--------|-------|
| Common properties | ✅ | id, position, size, zOrder, locked, visible, rotation |
| Pure virtual render() | ✅ | Subclasses implement rendering |
| Pure virtual type() | ✅ | Returns type string ("image", etc.) |
| toJson() / loadFromJson() | ✅ | Serialization with base properties |
| containsPoint() | ✅ | Default bounding rect hit test |
| boundingRect() / center() / moveBy() | ✅ | Geometry helpers |
| Factory fromJson() | ✅ | Creates subclass from JSON type field |

#### 2. ImageObject Subclass ✅
**File:** `source/objects/ImageObject.h/.cpp`

| Feature | Status | Notes |
|---------|--------|-------|
| Image properties | ✅ | imagePath, imageHash, maintainAspectRatio, originalAspectRatio |
| render() | ✅ | Draws pixmap with zoom and rotation |
| loadImage() / unloadImage() | ✅ | Disk I/O with path resolution |
| setPixmap() | ✅ | Set from clipboard/memory |
| calculateHash() | ✅ | SHA-256 for deduplication |
| resizeToWidth() / resizeToHeight() | ✅ | Aspect-ratio-aware resize |
| Serialization | ✅ | Saves/loads all image properties |

#### 3. Page Object Storage ✅
**File:** `source/core/Page.h/.cpp`

| Feature | Status | Notes |
|---------|--------|-------|
| Object container | ✅ | `std::vector<std::unique_ptr<InsertedObject>> objects` |
| addObject() | ✅ | Add with ownership transfer |
| removeObject() | ✅ | Remove by ID |
| objectAtPoint() | ✅ | Hit test in z-order (topmost first) |
| objectById() | ✅ | Find by UUID |
| sortObjectsByZOrder() | ✅ | Sort objects by stacking order |
| renderObjects() | ✅ | Render all visible objects |
| Serialization | ✅ | Saves/loads objects array in JSON |

#### 4. Viewport Integration (Partial) ✅
**File:** `source/core/DocumentViewport.cpp`

| Feature | Status | Notes |
|---------|--------|-------|
| Paged mode rendering | ✅ | `renderPage()` calls `page->renderObjects()` |
| Edgeless mode rendering | ✅ | `renderTileStrokes()` calls `tile->renderObjects()` |
| Object insertion UI | ❌ | Not implemented |
| Object selection | ❌ | Not implemented |
| Object manipulation | ❌ | Not implemented |

---

### What Does NOT Exist

#### 1. Object Insertion Mechanism ❌
**No way to add objects to pages/tiles:**
- No file dialog to select images
- No clipboard paste handling for images
- No drag & drop support
- No programmatic insertion from MainWindow

#### 2. Object Selection Tool ❌
**No way to select inserted objects:**
- No `ToolType::ObjectSelect` or similar
- No visual selection feedback (bounding box, handles)
- No hit testing in DocumentViewport input handlers
- No selected object state tracking

#### 3. Object Manipulation ❌
**No way to modify objects after insertion:**
- No move/drag functionality
- No resize with handles
- No rotation UI
- No delete (Delete key, context menu)
- No z-order manipulation (bring to front, send to back)
- No lock/unlock UI

#### 4. Object-Specific Features ❌
**ImageObject specifics not exposed:**
- No aspect ratio lock toggle UI
- No image replacement
- No image cropping
- No image export

#### 5. Undo/Redo Integration ❌
**Object operations not undoable:**
- No undo for insert/delete
- No undo for move/resize/rotate
- No undo for z-order changes

#### 6. MainWindow Integration ❌
**No UI elements for objects:**
- No "Insert Image" menu/button
- No object properties panel
- No context menu for objects
- No keyboard shortcuts for object operations

---

### Architecture Notes

1. **Tiles ARE Pages:** In edgeless mode, tiles are stored as `std::unique_ptr<Page>` in `m_tiles`. This means all Page object methods work on tiles automatically.

2. **Objects render BELOW strokes by default:** The primary use case is pasting a document image (e.g., test paper) and writing on top of it. Strokes must appear above objects by default.

3. **Layer Affinity:** Objects can optionally be "raised" to render above specific stroke layers, providing flexible z-ordering.

4. **No "Link" object type yet:** The plans mention "link inserts" that navigate to another location. This would be a new `LinkObject` subclass of `InsertedObject`.

5. **Rendering uses zoom parameter:** Both Page and ImageObject rendering accept a zoom parameter, allowing proper scaling.

---

## Architectural Decisions

### AD-1: Layer Affinity and Multi-Pass Rendering

**Design Goal:** Objects render BELOW strokes by default (test paper use case), but can be "raised" above specific layers.

#### Layer Affinity Property

Add to `InsertedObject`:

```cpp
int layerAffinity = -1;  // -1 = below all strokes (default)
                         //  0 = above Layer 0, below Layer 1
                         //  1 = above Layer 1, below Layer 2
                         // ... etc
                         // MAX_INT = always on top
```

| layerAffinity | Behavior |
|---------------|----------|
| -1 (default) | Renders below ALL strokes (test paper use case) |
| 0 | Renders above Layer 0, below Layer 1 |
| 1 | Renders above Layer 1, below Layer 2 |
| N | Renders above Layer N, below Layer N+1 |
| MAX_INT | Always on top of all strokes |

#### Object Grouping by Affinity (Core Requirement)

Objects MUST be pre-grouped by affinity for O(1) lookup during rendering. This is NOT an optimization—it's fundamental to how objects are stored and accessed.

Add to `Page`:

```cpp
// Primary storage (owns the objects)
std::vector<std::unique_ptr<InsertedObject>> objects;

// Grouped views by affinity (non-owning pointers, updated on add/remove/affinity change)
std::map<int, std::vector<InsertedObject*>> objectsByAffinity;

// Methods to maintain grouping
void addObject(std::unique_ptr<InsertedObject> obj);      // Updates both containers
void removeObject(const QString& id);                      // Updates both containers
void updateObjectAffinity(const QString& id, int newAffinity);  // Re-groups object
```

---

### AD-2: Multi-Pass Rendering for Edgeless Mode

**Problem:** Objects are stored in a single tile (the tile containing their top-left position), but large objects may extend across tile boundaries. Additionally, objects must interleave correctly with stroke layers based on their affinity.

**Example of Cross-Tile Problem:**
```
Object in tile (0,1) at position (900, 500) with size (300, 200)
- Object extends from x=900 to x=1200 (200px into tile (1,1))
- Tile size is 1024px

Per-tile rendering would clip the object at tile boundaries.
```

**Solution: Multi-Pass Rendering with Layer Interleaving**

```cpp
void DocumentViewport::renderEdgelessMode(QPainter& painter)
{
    // ===== PASS A: All visible tiles' BACKGROUNDS =====
    for (each visible tile) {
        renderTileBackground(painter, tile);  // Color/PDF only, no strokes
    }
    
    // ===== PASS B: Objects with layerAffinity = -1 (default) =====
    // These render BELOW all strokes
    renderObjectsWithAffinity(painter, -1);
    
    // ===== INTERLEAVED LAYER PASSES =====
    for (int layerIdx = 0; layerIdx < maxLayers; layerIdx++) {
        // Pass C, E, G, ...: Stroke layer N from all visible tiles
        for (each visible tile) {
            tile->renderLayerStrokes(painter, layerIdx, zoom);
        }
        
        // Pass D, F, H, ...: Objects with layerAffinity = N
        renderObjectsWithAffinity(painter, layerIdx);
    }
}

void DocumentViewport::renderObjectsWithAffinity(QPainter& painter, int affinity)
{
    // Iterate ALL loaded tiles (includes margin tiles)
    for (each loaded tile) {
        // O(1) lookup via pre-grouped map
        auto it = tile->objectsByAffinity.find(affinity);
        if (it == tile->objectsByAffinity.end()) continue;
        
        for (InsertedObject* obj : it->second) {
            if (!obj->visible) continue;
            
            // Convert tile-local position to document coordinates
            QPointF docPos = tileToDocument(obj->position, tileCoord);
            
            // Visibility check
            QRectF objRect(docPos, obj->size);
            if (!objRect.intersects(visibleDocRect)) continue;
            
            // Render at document coordinates (can extend across tiles)
            painter.save();
            painter.translate(docPos);
            obj->render(painter, zoom);
            painter.restore();
        }
    }
}
```

**Render Order Visualization:**

```
┌─────────────────────────────────────────────────────────────────┐
│ Pass A: All visible tiles' BACKGROUNDS                          │
│         (color/PDF background only, no strokes, no objects)     │
├─────────────────────────────────────────────────────────────────┤
│ Pass B: Objects with layerAffinity = -1 (default)               │
│         → "Below all strokes" - e.g., pasted test paper image   │
├─────────────────────────────────────────────────────────────────┤
│ Pass C: All visible tiles' Layer 0 strokes                      │
├─────────────────────────────────────────────────────────────────┤
│ Pass D: Objects with layerAffinity = 0                          │
│         → "Above Layer 0, below Layer 1"                        │
├─────────────────────────────────────────────────────────────────┤
│ Pass E: All visible tiles' Layer 1 strokes                      │
├─────────────────────────────────────────────────────────────────┤
│ Pass F: Objects with layerAffinity = 1                          │
│         → "Above Layer 1, below Layer 2"                        │
├─────────────────────────────────────────────────────────────────┤
│ ...continue for all layers...                                   │
└─────────────────────────────────────────────────────────────────┘
```

---

### AD-3: Extended Tile Loading Margin

To ensure objects that extend into the visible area are rendered, tiles beyond the viewport must be loaded:

```
Viewport visible area: tiles (2,2) to (4,4)
Max object extent: 500px (largest object dimension in document)
Margin needed: ceil(500 / 1024) = 1 tile

       0    1    2    3    4    5
    ┌────┬────┬────┬────┬────┬────┐
  0 │    │    │    │    │    │    │
    ├────┼────┼────┼────┼────┼────┤
  1 │    │░░░░│░░░░│░░░░│░░░░│░░░░│  ░░░░ = margin tiles (loaded for objects)
    ├────┼────┼────┼────┼────┼────┤
  2 │    │░░░░│████│████│████│░░░░│  ████ = viewport tiles (loaded normally)
    ├────┼────┼────┼────┼────┼────┤
  3 │    │░░░░│████│████│████│░░░░│
    ├────┼────┼────┼────┼────┼────┤
  4 │    │░░░░│████│████│████│░░░░│
    ├────┼────┼────┼────┼────┼────┤
  5 │    │░░░░│░░░░│░░░░│░░░░│░░░░│
    └────┴────┴────┴────┴────┴────┘
```

---

### AD-4: Paged Mode Rendering

Paged mode is simpler because pages don't overlap. Layer affinity still applies:

```cpp
void Page::render(QPainter& painter, qreal zoom)
{
    renderBackground(painter, zoom);
    
    // Objects below all strokes
    renderObjectsWithAffinity(painter, zoom, -1);
    
    // Interleaved layers and objects
    for (int layerIdx = 0; layerIdx < layers.size(); layerIdx++) {
        layers[layerIdx]->render(painter, zoom);
        renderObjectsWithAffinity(painter, zoom, layerIdx);
    }
}
```

---

### Implementation Requirements Summary

| Component | Change Needed | Priority |
|-----------|---------------|----------|
| `InsertedObject` | Add `int layerAffinity = -1` property | **P0 - Core** |
| `InsertedObject` | Add affinity to `toJson()`/`loadFromJson()` | **P0 - Core** |
| `Page` | Add `std::map<int, std::vector<InsertedObject*>> objectsByAffinity` | **P0 - Core** |
| `Page` | Update `addObject()` to maintain grouping | **P0 - Core** |
| `Page` | Update `removeObject()` to maintain grouping | **P0 - Core** |
| `Page` | Add `updateObjectAffinity()` method | **P0 - Core** |
| `Page` | Add `renderObjectsWithAffinity()` method | **P0 - Core** |
| `Page::render()` | Implement interleaved layer/object rendering | **P0 - Core** |
| `Document` | Track `m_maxObjectExtent` (updated on object add/resize) | **P0 - Core** |
| `DocumentViewport` | Expand tile loading margin for objects | **P0 - Core** |
| `DocumentViewport` | Implement multi-pass edgeless rendering | **P0 - Core** |
| `renderTileStrokes()` | Remove `tile->renderObjects()` call | **P0 - Core** |

**Object Storage:** Unchanged. Objects remain stored in their "home" tile using tile-local coordinates. Only the rendering approach and grouping changes.

---

## What Needs to Be Built

### Phase 1: Core Rendering Architecture (P0)

**Must be completed FIRST** - these changes define how objects fundamentally load and render:

1. **Layer Affinity property** - Add `layerAffinity` to `InsertedObject`, update serialization
2. **Object grouping by affinity** - Add `objectsByAffinity` map to `Page`, maintain on add/remove
3. **Paged mode interleaved rendering** - Update `Page::render()` to interleave objects with layers
4. **Edgeless multi-pass rendering** - Implement pass-based rendering in `DocumentViewport`
5. **Extended tile loading margin** - Track `m_maxObjectExtent`, expand loading range

### Phase 2: Interaction Layer (MVP)

After rendering architecture is solid:

1. **Insert Image mechanism** (file dialog, clipboard paste)
2. **Object selection tool** (click to select, visual feedback)
3. **Basic manipulation** (move, resize, delete)
4. **Undo/redo integration**
5. **MainWindow integration** (button, shortcuts)

### Phase 3: Enhanced Features (Post-MVP)

1. **LinkObject** - Navigate to page/location
2. **Advanced manipulation** - Rotation UI, z-order/affinity controls
3. **Object properties panel**
4. **Drag & drop support**
5. **Image cropping/editing**

---

## Implementation Dependencies

```
Phase 1 (Core Architecture)
    │
    ├── InsertedObject.layerAffinity
    │       │
    │       └── Page.objectsByAffinity
    │               │
    │               ├── Page::render() interleaving
    │               │
    │               └── DocumentViewport multi-pass
    │                       │
    │                       └── Extended tile loading margin
    │
    ▼
Phase 2 (Interaction Layer)
    │
    ├── Object insertion ──► File dialog, clipboard
    │       │
    │       └── Object selection ──► New tool type, hit testing
    │               │
    │               └── Object manipulation ──► Move, resize, delete
    │                       │
    │                       └── Undo/redo integration
    │
    ▼
Phase 3 (Enhanced Features)
    │
    └── MainWindow UI, LinkObject, advanced manipulation, etc.
```

---

## Architectural Decisions (continued)

### AD-5: Unified Bundle Format with Asset Storage

**Current State Analysis:**

#### Edgeless Mode (.snb bundle) - Current Structure
```
notebook.snb/
├── document.json        (metadata + tile_index + layers)
└── tiles/
    ├── 0,0.json         (strokes + objects for tile)
    ├── 1,0.json
    └── ...
```
- Objects serialized INTO each tile's JSON
- `ImageObject.imagePath` stores absolute or relative path
- No dedicated asset storage
- Tile lazy loading works via `m_tileIndex` + `loadTileFromDisk()`

#### Paged Mode (.json file) - Current Structure
```
notebook.json            (single file with everything)
```
- All pages inline in `pages` array
- All strokes and objects embedded
- Cannot support external asset files
- Must load entire document into memory

**Problem:** 
1. No consistent asset storage for images/files
2. Paged mode cannot reference external files
3. When duplicating images, each copy stores full path (no deduplication)
4. Can't package as zip/archive without asset folder

---

### Proposed Unified Bundle Structure

Both paged and edgeless modes will use the `.snb` folder format:

```
notebook.snb/
├── document.json            (manifest: metadata + mode-specific index)
│
├── tiles/                   (EDGELESS MODE ONLY)
│   ├── 0,0.json             (tile with strokes + object references)
│   ├── 1,0.json
│   └── ...
│
├── pages/                   (PAGED MODE ONLY - NEW)
│   ├── {uuid-1}.json        (page with strokes + object references)
│   ├── {uuid-2}.json        (UUID-based for easy insert/remove/reorder)
│   └── ...
│
└── assets/                  (BOTH MODES - NEW)
    └── images/
        ├── a1b2c3d4.png     (hash-based filename for deduplication)
        ├── e5f6g7h8.jpg
        └── ...
```

---

### Asset Storage Strategy

#### 1. Hash-Based Naming (Deduplication)

When an image is inserted:
```cpp
// 1. Calculate SHA-256 hash of image data
QString hash = ImageObject::calculateHash(pixmap);  // e.g., "a1b2c3d4e5f6..."

// 2. Truncate to reasonable length (first 16 chars)
QString filename = hash.left(16) + ".png";

// 3. Check if already exists in assets/images/
QString assetPath = bundlePath + "/assets/images/" + filename;
if (!QFile::exists(assetPath)) {
    // Save image to assets folder
    pixmap.save(assetPath, "PNG");
}

// 4. Store relative path in ImageObject
imageObject->imagePath = filename;  // Just the filename, not full path
```

**Benefits:**
- Same image used multiple times → same file (deduplication)
- Safe for archiving (no absolute paths)
- Hash collision: effectively impossible with SHA-256

#### 2. Path Resolution

`ImageObject::fullPath()` changes:
```cpp
QString ImageObject::fullPath(const QString& bundlePath) const
{
    if (imagePath.isEmpty()) return QString();
    
    // New: resolve against assets/images/ subdirectory
    return bundlePath + "/assets/images/" + imagePath;
}
```

#### 3. Image Loading Flow

```
Insert Image → calculateHash() → check assets/images/{hash}.png
     │                                  │
     │                         exists?──┼──► use existing file
     │                                  │
     │                         no ──────┼──► save to assets/images/
     │                                  │
     └────────────► set imagePath = "{hash}.png"
```

---

### AD-6: Paged Mode Lazy Loading Architecture

Pages will use the same lazy loading pattern as edgeless tiles.

#### Bundle Structure

```
notebook.snb/
├── document.json                (manifest with page_order)
├── pages/
│   ├── {uuid-1}.json           (page content)
│   ├── {uuid-2}.json
│   └── ...
└── assets/
    └── images/
        └── ...
```

**document.json manifest:**
```json
{
  "mode": "paged",
  "page_order": ["uuid-1", "uuid-2", "uuid-3"],  // Ordered list of page IDs
  "page_metadata": {
    "uuid-1": { "width": 816, "height": 1056 },  // Minimal info for layout
    "uuid-2": { "width": 816, "height": 1056 },
    ...
  },
  ...
}
```

**Why UUIDs instead of index-based filenames (`0.json`, `1.json`):**
- Insert/remove/reorder pages → only update `page_order` array
- No file renaming required
- Consistent with tile coordinate-based approach

---

### Current vs. New Architecture Comparison

| Aspect | Current Paged | New Paged (Lazy) | Edgeless (Reference) |
|--------|---------------|------------------|----------------------|
| Storage | `vector<unique_ptr<Page>> m_pages` | `map<QString, unique_ptr<Page>> m_pages` | `map<TileCoord, unique_ptr<Page>> m_tiles` |
| Index | N/A (all in memory) | `QStringList m_pageOrder` | `set<TileCoord> m_tileIndex` |
| Count | `m_pages.size()` | `m_pageOrder.size()` | `m_tileIndex.size()` |
| Access | `m_pages[index].get()` | Load on demand | Load on demand |
| Eviction | None | `evictPage(uuid)` | `evictTile(coord)` |
| Dirty tracking | `Page::modified` | `set<QString> m_dirtyPages` | `set<TileCoord> m_dirtyTiles` |

---

### Code Changes Required

#### 1. Document.h - New Member Variables

```cpp
// ===== Paged Mode Lazy Loading =====
QStringList m_pageOrder;                              // Ordered page UUIDs
std::map<QString, QSizeF> m_pageMetadata;             // Page sizes for layout cache
mutable std::map<QString, std::unique_ptr<Page>> m_loadedPages;  // Loaded pages
mutable std::set<QString> m_dirtyPages;               // Modified since last save

// Compatibility: m_pages becomes unused in new bundle format
// Keep for temporary backward compat during migration
```

#### 2. Document.h - API Changes

```cpp
// CURRENT: Page* page(int index);
// NEW:
Page* page(int index);                    // Returns loaded or loads on demand
bool isPageLoaded(int index) const;       // Check if page is in memory
bool loadPageFromDisk(int index);         // Explicit load
void evictPage(int index);                // Save if dirty, remove from memory
bool savePage(int index);                 // Save single page to disk

// Page order/metadata (needed for layout before loading)
QSizeF pageSizeAt(int index) const;       // Get size without loading page
QString pageUuidAt(int index) const;      // Get UUID for page index
```

#### 3. Document.cpp - page() Implementation Change

```cpp
// CURRENT:
Page* Document::page(int index) {
    if (index < 0 || index >= static_cast<int>(m_pages.size())) {
        return nullptr;
    }
    return m_pages[index].get();
}

// NEW:
Page* Document::page(int index) {
    if (index < 0 || index >= static_cast<int>(m_pageOrder.size())) {
        return nullptr;
    }
    
    QString uuid = m_pageOrder[index];
    
    // Check if already loaded
    auto it = m_loadedPages.find(uuid);
    if (it != m_loadedPages.end()) {
        return it->second.get();
    }
    
    // Load on demand
    if (!loadPageFromDisk(index)) {
        return nullptr;
    }
    
    return m_loadedPages[uuid].get();
}
```

#### 4. Document.cpp - New Page Persistence Methods

```cpp
bool Document::loadPageFromDisk(int index) {
    if (m_bundlePath.isEmpty()) return false;
    
    QString uuid = m_pageOrder[index];
    QString pagePath = m_bundlePath + "/pages/" + uuid + ".json";
    
    QFile file(pagePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Cannot load page:" << pagePath;
        return false;
    }
    
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    auto page = Page::fromJson(doc.object());
    if (!page) return false;
    
    // Load images for this page
    page->loadImages(m_bundlePath + "/assets/images");
    
    m_loadedPages[uuid] = std::move(page);
    return true;
}

bool Document::savePage(int index) {
    QString uuid = m_pageOrder[index];
    auto it = m_loadedPages.find(uuid);
    if (it == m_loadedPages.end()) return false;  // Not loaded
    
    QString pagePath = m_bundlePath + "/pages/" + uuid + ".json";
    QDir().mkpath(m_bundlePath + "/pages");
    
    QFile file(pagePath);
    if (!file.open(QIODevice::WriteOnly)) return false;
    
    QJsonDocument doc(it->second->toJson());
    file.write(doc.toJson(QJsonDocument::Compact));
    
    m_dirtyPages.erase(uuid);
    return true;
}

void Document::evictPage(int index) {
    QString uuid = m_pageOrder[index];
    auto it = m_loadedPages.find(uuid);
    if (it == m_loadedPages.end()) return;
    
    // Save if dirty
    if (m_dirtyPages.count(uuid) > 0) {
        savePage(index);
    }
    
    m_loadedPages.erase(it);
}
```

#### 5. Document.cpp - Insert/Remove/Move Changes

```cpp
Page* Document::insertPage(int index) {
    // Generate new UUID
    QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    
    // Insert into order list
    m_pageOrder.insert(index, uuid);
    
    // Create page in memory
    auto newPage = createDefaultPage();
    Page* ptr = newPage.get();
    m_loadedPages[uuid] = std::move(newPage);
    
    // Store metadata
    m_pageMetadata[uuid] = ptr->size;
    
    // Mark dirty
    m_dirtyPages.insert(uuid);
    markModified();
    
    return ptr;
}

bool Document::removePage(int index) {
    if (m_pageOrder.size() <= 1) return false;
    
    QString uuid = m_pageOrder[index];
    
    // Remove from order
    m_pageOrder.removeAt(index);
    
    // Evict from memory
    m_loadedPages.erase(uuid);
    m_dirtyPages.erase(uuid);
    m_pageMetadata.erase(uuid);
    
    // Mark for deletion on save (like m_deletedTiles)
    m_deletedPages.insert(uuid);
    
    markModified();
    return true;
}

bool Document::movePage(int from, int to) {
    // Just reorder - no file changes needed!
    QString uuid = m_pageOrder[from];
    m_pageOrder.removeAt(from);
    m_pageOrder.insert(to, uuid);
    markModified();
    return true;
}
```

#### 6. DocumentViewport - Preload/Evict Integration

The viewport ALREADY has the right pattern:

```cpp
void DocumentViewport::preloadNearbyPages() {
    QVector<int> visible = visiblePages();
    // ... existing preload logic ...
    
    // NEW: Also trigger page loading
    for (int i = preloadStart; i <= preloadEnd; ++i) {
        if (!m_document->isPageLoaded(i)) {
            m_document->loadPageFromDisk(i);  // Or queue for async load
        }
    }
    
    // NEW: Evict far pages (similar to stroke cache eviction)
    for (int i = 0; i < pageCount; ++i) {
        if (i < keepStart || i > keepEnd) {
            m_document->evictPage(i);
        }
    }
}
```

#### 7. Page Layout Cache Consideration

**Problem:** `ensurePageLayoutCache()` needs page sizes for ALL pages to calculate positions.

**Solution:** Store minimal metadata in manifest:
```json
"page_metadata": {
    "uuid-1": { "width": 816, "height": 1056 }
}
```

Use `pageSizeAt(index)` instead of `page(index)->size` for layout:
```cpp
QSizeF Document::pageSizeAt(int index) const {
    QString uuid = m_pageOrder[index];
    return m_pageMetadata[uuid];  // O(1), no page load
}
```

---

### Summary of Files to Modify

| File | Changes |
|------|---------|
| `Document.h` | Add lazy loading members, modify page API |
| `Document.cpp` | Implement lazy loading methods, modify save/load bundle |
| `DocumentViewport.cpp` | Add page eviction to preload logic |
| `Page.h` | No changes |
| `Page.cpp` | No changes |
| `DocumentManager.cpp` | Route paged mode through bundle save |

### Scope Assessment

| Change Category | Estimated Effort |
|-----------------|------------------|
| Document storage refactor | Medium-High |
| page() lazy loading | Medium |
| Insert/remove/move with UUIDs | Medium |
| Page layout cache (metadata) | Low |
| DocumentViewport eviction | Low |
| Bundle save/load for paged | Medium |
| Asset folder integration | Low |

**Total:** This is a significant refactor (~300-500 lines changed), but follows the well-tested edgeless tile pattern.

---

### AD-5 Implementation Requirements

| Component | Change | Priority |
|-----------|--------|----------|
| `Document::saveBundle()` | Create `assets/images/` directory | **P0** |
| `Document::saveBundle()` | Save paged mode to `pages/` directory | **P0** |
| `Document::loadBundle()` | Load pages from `pages/` directory (lazy) | **P0** |
| `DocumentManager::doSave()` | Route ALL modes through bundle format | **P0** |
| `ImageObject::fullPath()` | Resolve against `bundlePath/assets/images/` | **P0** |
| `ImageObject` (new) | `saveToAssets(bundlePath)` method | **P0** |
| `Page::loadImages()` | Pass bundle path for resolution | **P0** |
| Tile JSON | Objects store just filename in `imagePath` | **P0** |
| Page JSON | Objects store just filename in `imagePath` | **P0** |

**Note:** The old single-file `.json` format for paged notebooks will no longer be supported. All documents use the unified `.snb` bundle format.

---

### Asset Types (Future Extension)

The `assets/` folder structure allows for future asset types:
```
assets/
├── images/          (ImageObject)
├── audio/           (future: AudioObject)
├── videos/          (future: VideoObject)
├── attachments/     (future: FileAttachmentObject)
└── thumbnails/      (future: page previews)
```

---

## Agreed Design Decisions (Pre-Planning)

### DD-1: Phase Scope
- **Phase 1**: Core rendering architecture (P0) - MUST complete first
- **Phase 2**: Interaction layer (MVP) - Object insertion and manipulation
- **Phase 3**: Enhanced features - Minimal scope, future expansion

### DD-2: PDF Document Handling
**Decision:** Option A - Create Page objects upfront, lazy-load strokes/objects.

Memory analysis for 3000-page PDF:
- Page shell: ~200-500 bytes each (metadata only)
- 3000 pages × 500 bytes = ~1.5 MB - negligible
- Strokes/objects: Only loaded for visible ±2 pages

This matches current behavior with lazy loading infrastructure.

### DD-3: Object Insertion (MVP)
**Decision:** Clipboard paste only for MVP.

- **Ctrl+V with image in clipboard** → Insert at cursor/center position
- **Leave interfaces for future UI:**
  - File dialog ("Insert Image" menu)
  - Drag & drop from file explorer
  - "Browse existing pictures" dialog
- **Temporary UI:** Connect to existing `insertPictureButton` in MainWindow

### DD-4: Existing Picture Reuse
**Decision:** Hash-based deduplication handles this automatically.

- Multiple objects can reference same `{hash}.png` in `assets/images/`
- "Insert existing picture" = create new `ImageObject` with same `imagePath`
- Zero file duplication, zero extra memory for shared images

### DD-5: Assets Folder Migration
**Decision:** Add `assets/` folder as non-breaking change.

- Existing edgeless bundles: Add `assets/` on next save
- No migration of existing imagePaths needed until object is modified

### DD-6: zOrder Scope
**Decision:** zOrder only affects ordering within the same affinity level.

```
Layer 0 strokes
├── Object A (affinity=0, zOrder=1)
├── Object B (affinity=0, zOrder=2)  ← B renders on top of A
Layer 1 strokes
├── Object C (affinity=1, zOrder=1)  ← C always on top of A and B
```

### DD-7: Affinity/zOrder UX

**Affinity (which layer objects render with):**
- "Attach to Layer" model - object "belongs to" a layer
- Show objects in layer panel under their affiliated layer
- Move between layers via drag or right-click menu

**zOrder (within same affinity):**
| Command | Shortcut | Effect |
|---------|----------|--------|
| Bring to Front | Ctrl+Shift+] | Top of current affinity group |
| Send to Back | Ctrl+Shift+[ | Bottom of current affinity group |
| Bring Forward | Ctrl+] | One step up |
| Send Backward | Ctrl+[ | One step down |

### DD-8: Object Selection Tool
**Decision:** Dedicated "Object Select" tool.

- New tool type in `ToolType` enum
- Only active when this tool is selected
- No hit-testing during drawing/erasing (performance)
- Click on object → select it
- Click on empty → deselect

**Temporary UI:** Use existing `insertPictureButton` to toggle Object Select tool.

### DD-9: Layer Panel Integration (Future)
Objects shown in layer panel:
```
Layer 0 (strokes)
├── [Object A]  ← objects with affinity=0
├── [Object B]
Layer 1 (strokes)
├── [Object C]  ← affinity=1
Layer 2 (strokes)
```

This will be implemented when tearing apart MainWindow (future phase).

### DD-10: Undo/Redo for Objects
**Decision:** Keep consistent with existing undo systems.

| Mode | Stack Type | Rationale |
|------|------------|-----------|
| **Paged** | Per-page stacks | Same as stroke undo, object actions go in `m_undoStacks[pageIdx]` |
| **Edgeless** | Global stack | Same as edgeless stroke undo (Phase E6), one action = one undo |

Object actions that generate undo entries:
- Insert object
- Delete object
- Move object
- Resize object
- Change layerAffinity
- Change zOrder

### DD-11: Object Copy/Paste
**Decision:** Yes, same behavior as lasso selection.

| Action | Shortcut | Behavior |
|--------|----------|----------|
| Copy | Ctrl+C | Copy selected object(s) to internal clipboard |
| Paste | Ctrl+V | Paste at offset position (like lasso paste) |
| Cut | Ctrl+X | Copy + Delete |
| Delete | Delete | Remove selected object(s) |
| Duplicate | Ctrl+D | Copy + immediate paste at offset (optional) |

Internal clipboard (not system clipboard) - consistent with lasso selection behavior.

### DD-12: Multi-Object Selection
**Decision:** Option B - Support multi-select from MVP.

| Interaction | Behavior |
|-------------|----------|
| Click object | Select it, deselect others |
| Shift+click | Add to / toggle in selection |
| Click empty | Deselect all |
| Drag rectangle | Select all objects in rectangle (future UI) |

**Document interface for future UI:**
```cpp
// Selection API (to be connected to UI later)
void DocumentViewport::selectObject(InsertedObject* obj, bool addToSelection = false);
void DocumentViewport::deselectObject(InsertedObject* obj);
void DocumentViewport::deselectAllObjects();
void DocumentViewport::selectObjectsInRect(const QRectF& rect);
QList<InsertedObject*> DocumentViewport::selectedObjects() const;
```

---

## Interface Documentation (For Future UI Connections)

### Object Insertion Interfaces
```cpp
// To be connected to MainWindow UI later
void DocumentViewport::insertImageFromFile(const QString& filePath);
void DocumentViewport::insertImageFromClipboard();  // MVP: Implement this
void DocumentViewport::insertImageFromDialog();     // Future: File picker
void DocumentViewport::showExistingPictureDialog(); // Future: Browse existing
```

### Object Selection Interfaces
```cpp
// Tool switching
void DocumentViewport::setToolType(ToolType type);  // Existing, add ObjectSelect
enum ToolType { ..., ObjectSelect };                // New tool type

// Selection state
void DocumentViewport::selectObject(InsertedObject* obj, bool addToSelection);
void DocumentViewport::deselectAllObjects();
QList<InsertedObject*> DocumentViewport::selectedObjects() const;
bool DocumentViewport::hasSelectedObjects() const;
```

### Object Manipulation Interfaces
```cpp
// Move/resize (triggered by mouse drag on selection)
void DocumentViewport::moveSelectedObjects(const QPointF& delta);
void DocumentViewport::resizeSelectedObject(const QSizeF& newSize);

// Clipboard operations
void DocumentViewport::copySelectedObjects();
void DocumentViewport::pasteObjects();
void DocumentViewport::deleteSelectedObjects();

// Layer affinity (to be connected to layer panel)
void DocumentViewport::setSelectedObjectAffinity(int layerIndex);
int DocumentViewport::selectedObjectAffinity() const;

// zOrder (keyboard shortcuts)
void DocumentViewport::bringSelectedToFront();   // Ctrl+Shift+]
void DocumentViewport::sendSelectedToBack();     // Ctrl+Shift+[
void DocumentViewport::bringSelectedForward();   // Ctrl+]
void DocumentViewport::sendSelectedBackward();   // Ctrl+[
```

### Signals for UI Updates
```cpp
// Emitted when selection changes (for UI to update)
void objectSelectionChanged(const QList<InsertedObject*>& selected);

// Emitted when object properties change (for property panel)
void selectedObjectPropertiesChanged();
```

---

# Implementation Plan

## Phase O1: Core Architecture

**Goal:** Establish the rendering and persistence foundation that all object features depend on.

---

### O1.1: Layer Affinity Property

**Goal:** Add `layerAffinity` to InsertedObject for layer-relative rendering.

#### O1.1.1: Add layerAffinity to InsertedObject
**File:** `source/objects/InsertedObject.h`

```cpp
// Add to InsertedObject class
int layerAffinity = -1;  // -1 = below all strokes (default)
```

**Tasks:**
- [ ] Add `int layerAffinity = -1` member variable
- [ ] Add getter/setter: `int getLayerAffinity() const`, `void setLayerAffinity(int)`
- [ ] Document the semantics in header comments

#### O1.1.2: Update Serialization
**File:** `source/objects/InsertedObject.cpp`

**Tasks:**
- [ ] Add `layerAffinity` to `toJson()`
- [ ] Add `layerAffinity` to `loadFromJson()` with default -1

#### O1.1.3: Update ImageObject
**File:** `source/objects/ImageObject.cpp`

**Tasks:**
- [ ] Ensure ImageObject inherits layerAffinity behavior correctly
- [ ] Verify render() doesn't need changes (affinity is handled at Page level)

---

### O1.2: Object Grouping by Affinity

**Goal:** Pre-group objects by affinity for O(1) lookup during rendering.

#### O1.2.1: Add Affinity Map to Page
**File:** `source/core/Page.h`

```cpp
// Add to Page class
std::map<int, std::vector<InsertedObject*>> objectsByAffinity;
```

**Tasks:**
- [ ] Add `objectsByAffinity` member (non-owning pointers)
- [ ] Declare helper: `void rebuildAffinityMap()`
- [ ] Declare: `void renderObjectsWithAffinity(QPainter&, qreal zoom, int affinity)`

#### O1.2.2: Maintain Affinity Map
**File:** `source/core/Page.cpp`

**Tasks:**
- [ ] Implement `rebuildAffinityMap()` - iterate objects, group by affinity
- [ ] Update `addObject()` - add to affinity map
- [ ] Update `removeObject()` - remove from affinity map
- [ ] Add `updateObjectAffinity(QString id, int newAffinity)` - re-group object
- [ ] Call `rebuildAffinityMap()` in `Page::fromJson()` after loading objects

#### O1.2.3: Implement renderObjectsWithAffinity
**File:** `source/core/Page.cpp`

```cpp
void Page::renderObjectsWithAffinity(QPainter& painter, qreal zoom, int affinity)
{
    auto it = objectsByAffinity.find(affinity);
    if (it == objectsByAffinity.end()) return;
    
    // Sort by zOrder within this affinity group
    auto& objs = it->second;
    std::sort(objs.begin(), objs.end(), 
        [](auto* a, auto* b) { return a->zOrder < b->zOrder; });
    
    for (InsertedObject* obj : objs) {
        if (obj->visible) {
            obj->render(painter, zoom);
        }
    }
}
```

**Tasks:**
- [ ] Implement the method as shown above
- [ ] Ensure zOrder sorting within affinity group

---

### O1.3: Paged Mode Interleaved Rendering

**Goal:** Render objects interleaved with layers based on affinity.

#### O1.3.1: Update Page::render() (if exists) or renderPage()
**File:** `source/core/DocumentViewport.cpp` (renderPage function)

**Current flow:**
1. Background
2. All layers
3. All objects

**New flow:**
1. Background
2. Objects with affinity = -1 (below all)
3. Layer 0 → Objects with affinity = 0
4. Layer 1 → Objects with affinity = 1
5. ... continue for all layers

**Tasks:**
- [ ] Modify `renderPage()` to call `page->renderObjectsWithAffinity(painter, zoom, -1)` after background
- [ ] Modify layer loop to call `page->renderObjectsWithAffinity(painter, zoom, layerIdx)` after each layer
- [ ] Remove old `page->renderObjects()` call (replaced by interleaved calls)

---

### O1.4: Edgeless Multi-Pass Rendering

**Goal:** Implement multi-pass rendering for correct cross-tile object display.

#### O1.4.1: Refactor renderEdgelessMode
**File:** `source/core/DocumentViewport.cpp`

**Tasks:**
- [ ] Extract background rendering into helper: `renderTileBackground()`
- [ ] Extract stroke rendering into helper: `renderTileLayerStrokes(int layerIdx)`
- [ ] Create `renderEdgelessObjectsWithAffinity(int affinity)` method

#### O1.4.2: Implement Multi-Pass Structure
**File:** `source/core/DocumentViewport.cpp`

```cpp
void DocumentViewport::renderEdgelessMode(QPainter& painter)
{
    // ... setup code ...
    
    // PASS A: All visible tiles' backgrounds
    for (auto& coord : visibleTiles) {
        renderTileBackground(painter, coord);
    }
    
    // PASS B: Objects with affinity = -1 (below all strokes)
    renderEdgelessObjectsWithAffinity(painter, -1);
    
    // INTERLEAVED PASSES: For each layer
    int maxLayers = m_document->edgelessLayerCount();
    for (int layerIdx = 0; layerIdx < maxLayers; ++layerIdx) {
        // Render this layer's strokes from all visible tiles
        for (auto& coord : visibleTiles) {
            renderTileLayerStrokes(painter, coord, layerIdx);
        }
        
        // Render objects affiliated with this layer
        renderEdgelessObjectsWithAffinity(painter, layerIdx);
    }
}
```

**Tasks:**
- [ ] Implement the multi-pass structure
- [ ] Ensure correct painter state management (save/restore)

#### O1.4.3: Implement renderEdgelessObjectsWithAffinity
**File:** `source/core/DocumentViewport.cpp`

```cpp
void DocumentViewport::renderEdgelessObjectsWithAffinity(QPainter& painter, int affinity)
{
    // Iterate ALL loaded tiles (includes margin)
    for (auto& [coord, tile] : m_document->loadedTiles()) {
        auto it = tile->objectsByAffinity.find(affinity);
        if (it == tile->objectsByAffinity.end()) continue;
        
        for (InsertedObject* obj : it->second) {
            if (!obj->visible) continue;
            
            // Convert tile-local to document coordinates
            QPointF docPos = tileToDocument(obj->position, coord);
            QRectF objRect(docPos, obj->size);
            
            // Skip if not visible
            if (!objRect.intersects(visibleDocRect)) continue;
            
            painter.save();
            painter.translate(docPos);
            obj->render(painter, m_zoomLevel);
            painter.restore();
        }
    }
}
```

**Tasks:**
- [ ] Implement the method
- [ ] Add `tileToDocument()` helper if not exists
- [ ] Add `loadedTiles()` accessor to Document if not exists

---

### O1.5: Extended Tile Loading Margin

**Goal:** Load extra tiles to capture objects that extend into viewport.

#### O1.5.1: Track Maximum Object Extent
**File:** `source/core/Document.h/.cpp`

**Tasks:**
- [ ] Add `int m_maxObjectExtent = 0` member
- [ ] Add `void updateMaxObjectExtent(const InsertedObject* obj)`
- [ ] Call from tile's `addObject()` when object added
- [ ] Add `int maxObjectExtent() const` accessor

#### O1.5.2: Expand Tile Loading Range
**File:** `source/core/DocumentViewport.cpp`

**Tasks:**
- [ ] In tile loading logic, add margin: `ceil(m_document->maxObjectExtent() / TILE_SIZE)`
- [ ] Ensure margin tiles are loaded but backgrounds not rendered (stroke margin already exists)

---

### O1.6: Unified Bundle Format

**Goal:** Both paged and edgeless use .snb bundle with assets folder.

#### O1.6.1: Create Assets Directory Structure
**File:** `source/core/Document.cpp`

**Tasks:**
- [ ] In `saveBundle()`, create `assets/images/` directory
- [ ] Add helper: `QString assetsImagePath() const { return m_bundlePath + "/assets/images"; }`

#### O1.6.2: Update ImageObject Path Resolution
**File:** `source/objects/ImageObject.cpp`

**Tasks:**
- [ ] Modify `fullPath()` to resolve against `bundlePath/assets/images/`
- [ ] Add `saveToAssets(const QString& bundlePath)` method:
  - Calculate hash
  - Save to `assets/images/{hash16}.png` if not exists
  - Update `imagePath` to just filename

#### O1.6.3: Update Image Loading
**File:** `source/core/Page.cpp`

**Tasks:**
- [ ] Update `loadImages(basePath)` to pass `basePath + "/assets/images"`
- [ ] Ensure relative path resolution works

---

### O1.7: Paged Mode Lazy Loading

**Goal:** Implement lazy loading for paged mode pages.

#### O1.7.1: Add Lazy Loading Members
**File:** `source/core/Document.h`

```cpp
// Paged mode lazy loading
QStringList m_pageOrder;                              // Ordered page UUIDs
std::map<QString, QSizeF> m_pageMetadata;             // Sizes for layout
mutable std::map<QString, std::unique_ptr<Page>> m_loadedPages;
mutable std::set<QString> m_dirtyPages;
std::set<QString> m_deletedPages;                     // For cleanup on save
```

**Tasks:**
- [ ] Add all member variables
- [ ] Add accessors: `isPageLoaded()`, `pageUuidAt()`, `pageSizeAt()`

#### O1.7.2: Implement page() with Lazy Loading
**File:** `source/core/Document.cpp`

**Tasks:**
- [ ] Modify `page(int index)` to check `m_loadedPages`, load on demand
- [ ] Implement `loadPageFromDisk(int index)`
- [ ] Implement `savePage(int index)`
- [ ] Implement `evictPage(int index)`

#### O1.7.3: Update Page Insert/Remove/Move
**File:** `source/core/Document.cpp`

**Tasks:**
- [ ] Update `insertPage()` to use UUID, update `m_pageOrder`
- [ ] Update `removePage()` to track in `m_deletedPages`
- [ ] Update `movePage()` to just reorder `m_pageOrder`
- [ ] Ensure `pageCount()` returns `m_pageOrder.size()`

#### O1.7.4: Update Bundle Save/Load for Paged Mode
**File:** `source/core/Document.cpp`

**Tasks:**
- [ ] `saveBundle()`: Write pages to `pages/{uuid}.json`, include `page_order` and `page_metadata` in manifest
- [ ] `loadBundle()`: Parse `page_order`, `page_metadata`, enable lazy loading
- [ ] Delete files for pages in `m_deletedPages`

#### O1.7.5: Update DocumentViewport Page Access
**File:** `source/core/DocumentViewport.cpp`

**Tasks:**
- [ ] In `preloadNearbyPages()`, trigger page loading for visible ±2
- [ ] Add page eviction for pages far from visible
- [ ] Update `ensurePageLayoutCache()` to use `pageSizeAt()` instead of `page(i)->size`

#### O1.7.6: Route Paged Mode Through Bundle Save
**File:** `source/core/DocumentManager.cpp`

**Tasks:**
- [ ] Remove old single-file JSON save path
- [ ] Route all documents through `saveBundle()`
- [ ] Update file dialogs to use `.snb` extension

---

### O1.8: Testing & Verification

**Tasks:**
- [ ] Test: Create edgeless doc with objects, verify cross-tile rendering
- [ ] Test: Create paged doc with objects, verify layer-interleaved rendering
- [ ] Test: Save/load bundle with images, verify assets folder
- [ ] Test: Lazy loading - load 100+ page doc, verify memory usage
- [ ] Test: Object affinity -1, 0, 1 render in correct order

---

## Phase O2: Interaction Layer (MVP)

**Goal:** Enable users to insert, select, and manipulate objects.

---

### O2.1: Object Select Tool

**Goal:** Add dedicated tool for selecting objects.

#### O2.1.1: Add ToolType::ObjectSelect
**File:** `source/core/DocumentViewport.h`

**Tasks:**
- [ ] Add `ObjectSelect` to `ToolType` enum
- [ ] Add member: `QList<InsertedObject*> m_selectedObjects`
- [ ] Add member: `InsertedObject* m_hoveredObject = nullptr`

#### O2.1.2: Implement Object Hit Testing
**File:** `source/core/DocumentViewport.cpp`

```cpp
InsertedObject* DocumentViewport::objectAtPoint(const QPointF& docPoint)
{
    if (m_document->isEdgeless()) {
        // Check loaded tiles
        for (auto& [coord, tile] : m_document->loadedTiles()) {
            QPointF tileLocal = documentToTile(docPoint, coord);
            if (auto* obj = tile->objectAtPoint(tileLocal)) {
                return obj;
            }
        }
    } else {
        // Check page at point
        int pageIdx = pageAtPoint(docPoint);
        if (Page* page = m_document->page(pageIdx)) {
            QPointF pageLocal = docPoint - pagePosition(pageIdx);
            return page->objectAtPoint(pageLocal);
        }
    }
    return nullptr;
}
```

**Tasks:**
- [ ] Implement `objectAtPoint()` as shown
- [ ] Add `documentToTile()` helper if needed

#### O2.1.3: Handle ObjectSelect Tool Input
**File:** `source/core/DocumentViewport.cpp`

**Tasks:**
- [ ] In `handlePointerPress()`, if ObjectSelect tool:
  - Hit test for object
  - If Shift held, add to selection; else replace selection
  - If no object hit, deselect all
- [ ] In `handlePointerMove()`, update hover state, handle drag if selected
- [ ] In `handlePointerRelease()`, finalize move if dragging

#### O2.1.4: Selection Visual Feedback
**File:** `source/core/DocumentViewport.cpp`

**Tasks:**
- [ ] In render loop, after objects, draw selection boxes for `m_selectedObjects`
- [ ] Draw resize handles at corners (future: for resize)
- [ ] Draw hover highlight for `m_hoveredObject`

---

### O2.2: Object Selection API

**Goal:** Implement selection management methods.

#### O2.2.1: Selection Methods
**File:** `source/core/DocumentViewport.cpp`

**Tasks:**
- [ ] Implement `selectObject(obj, addToSelection)`
- [ ] Implement `deselectObject(obj)`
- [ ] Implement `deselectAllObjects()`
- [ ] Implement `selectedObjects()` getter
- [ ] Implement `hasSelectedObjects()` getter
- [ ] Emit `objectSelectionChanged` signal when selection changes

---

### O2.3: Object Movement

**Goal:** Allow dragging selected objects.

#### O2.3.1: Drag State Tracking
**File:** `source/core/DocumentViewport.h`

**Tasks:**
- [ ] Add `bool m_isDraggingObject = false`
- [ ] Add `QPointF m_objectDragStart`
- [ ] Add `QMap<QString, QPointF> m_objectOriginalPositions` (for undo)

#### O2.3.2: Implement Drag Logic
**File:** `source/core/DocumentViewport.cpp`

**Tasks:**
- [ ] On pointer press on selected object: start drag, store original positions
- [ ] On pointer move: calculate delta, call `moveSelectedObjects(delta)`
- [ ] On pointer release: create undo entry, finalize positions

#### O2.3.3: Implement moveSelectedObjects
**File:** `source/core/DocumentViewport.cpp`

```cpp
void DocumentViewport::moveSelectedObjects(const QPointF& delta)
{
    for (InsertedObject* obj : m_selectedObjects) {
        obj->position += delta;
    }
    update();
}
```

**Tasks:**
- [ ] Implement the method
- [ ] Handle page/tile boundary crossing (edgeless: move to new tile if needed)

---

### O2.4: Clipboard Paste (MVP)

**Goal:** Paste images from clipboard as objects.

#### O2.4.1: Detect Image in Clipboard
**File:** `source/core/DocumentViewport.cpp`

**Tasks:**
- [ ] In keyboard handler for Ctrl+V:
  - Check if clipboard has image (`QApplication::clipboard()->mimeData()->hasImage()`)
  - If yes, call `insertImageFromClipboard()`
  - If no, fall back to existing paste behavior (lasso paste)

#### O2.4.2: Implement insertImageFromClipboard
**File:** `source/core/DocumentViewport.cpp`

```cpp
void DocumentViewport::insertImageFromClipboard()
{
    QClipboard* clipboard = QApplication::clipboard();
    QImage image = clipboard->image();
    if (image.isNull()) return;
    
    // Create ImageObject
    auto imgObj = std::make_unique<ImageObject>();
    imgObj->setPixmap(QPixmap::fromImage(image));
    imgObj->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    
    // Position at viewport center
    QPointF center = viewportCenterInDocument();
    imgObj->position = center - QPointF(imgObj->size.width()/2, imgObj->size.height()/2);
    
    // Default affinity: -1 (below all strokes)
    imgObj->layerAffinity = -1;
    
    // Add to appropriate page/tile
    if (m_document->isEdgeless()) {
        auto coord = m_document->tileCoordForPoint(imgObj->position);
        Page* tile = m_document->getOrCreateTile(coord.first, coord.second);
        // Convert to tile-local coordinates
        imgObj->position = documentToTile(imgObj->position, coord);
        tile->addObject(std::move(imgObj));
    } else {
        Page* page = m_document->page(m_currentPageIndex);
        page->addObject(std::move(imgObj));
    }
    
    // Save image to assets
    // ... hash-based save logic ...
    
    // Create undo entry
    pushObjectInsertUndo(imgObj.get());
    
    // Select the new object
    deselectAllObjects();
    selectObject(imgObj.get(), false);
    
    update();
}
```

**Tasks:**
- [ ] Implement the method as outlined
- [ ] Add helper `viewportCenterInDocument()`
- [ ] Save image to assets folder with hash
- [ ] Create undo entry for insert

---

### O2.5: Object Deletion

**Goal:** Delete selected objects with Delete key.

#### O2.5.1: Handle Delete Key
**File:** `source/core/DocumentViewport.cpp`

**Tasks:**
- [ ] In `keyPressEvent()`, if Delete pressed and `hasSelectedObjects()`:
  - Call `deleteSelectedObjects()`

#### O2.5.2: Implement deleteSelectedObjects
**File:** `source/core/DocumentViewport.cpp`

**Tasks:**
- [ ] For each selected object:
  - Find containing page/tile
  - Create undo entry (store object data for restore)
  - Call `page->removeObject(obj->id)`
- [ ] Clear selection
- [ ] Update viewport

---

### O2.6: Object Copy/Paste

**Goal:** Copy selected objects, paste duplicates.

#### O2.6.1: Internal Object Clipboard
**File:** `source/core/DocumentViewport.h`

**Tasks:**
- [ ] Add `QList<QJsonObject> m_objectClipboard` (serialized objects)

#### O2.6.2: Implement copySelectedObjects
**File:** `source/core/DocumentViewport.cpp`

**Tasks:**
- [ ] Serialize each selected object to JSON
- [ ] Store in `m_objectClipboard`

#### O2.6.3: Implement pasteObjects
**File:** `source/core/DocumentViewport.cpp`

**Tasks:**
- [ ] If `m_objectClipboard` not empty:
  - Deserialize each object
  - Assign new UUIDs
  - Offset position (e.g., +20, +20)
  - Add to current page/tile
  - Create undo entries
  - Select pasted objects

---

### O2.7: Undo/Redo Integration

**Goal:** Make object operations undoable.

#### O2.7.1: Object Undo Action Types
**File:** `source/core/DocumentViewport.h`

**Tasks:**
- [ ] Add to undo action enum: `ObjectInsert`, `ObjectDelete`, `ObjectMove`, `ObjectResize`, `ObjectAffinityChange`
- [ ] Add object-specific undo data structure

#### O2.7.2: Paged Mode Object Undo
**File:** `source/core/DocumentViewport.cpp`

**Tasks:**
- [ ] Object actions push to page's undo stack (same as strokes)
- [ ] Implement undo/redo handlers for each object action type

#### O2.7.3: Edgeless Mode Object Undo
**File:** `source/core/DocumentViewport.cpp`

**Tasks:**
- [ ] Object actions push to global edgeless undo stack
- [ ] Store tile coordinate with action for proper restore

---

### O2.8: zOrder Keyboard Shortcuts

**Goal:** Implement Ctrl+[ and Ctrl+] for z-order changes.

#### O2.8.1: Implement zOrder Methods
**File:** `source/core/DocumentViewport.cpp`

**Tasks:**
- [ ] `bringSelectedToFront()` - set zOrder = max + 1 in affinity group
- [ ] `sendSelectedToBack()` - set zOrder = min - 1 in affinity group
- [ ] `bringSelectedForward()` - swap with next higher zOrder
- [ ] `sendSelectedBackward()` - swap with next lower zOrder

#### O2.8.2: Wire Up Shortcuts
**File:** `source/core/DocumentViewport.cpp`

**Tasks:**
- [ ] In `keyPressEvent()`:
  - Ctrl+] → `bringSelectedForward()`
  - Ctrl+[ → `sendSelectedBackward()`
  - Ctrl+Shift+] → `bringSelectedToFront()`
  - Ctrl+Shift+[ → `sendSelectedToBack()`

---

### O2.9: Temporary MainWindow Connection

**Goal:** Connect existing button to object tool.

#### O2.9.1: Update insertPictureButton
**File:** `source/MainWindow.cpp`

**Tasks:**
- [ ] Change button to toggle ObjectSelect tool (not insert)
- [ ] Update tooltip: "Object Select Tool"
- [ ] When clicked, call `viewport->setToolType(ToolType::ObjectSelect)`

#### O2.9.2: Add Ctrl+V Handling in MainWindow
**File:** `source/MainWindow.cpp`

**Tasks:**
- [ ] Ensure Ctrl+V reaches DocumentViewport
- [ ] DocumentViewport checks for image → `insertImageFromClipboard()`

---

### O2.10: Testing & Verification

**Tasks:**
- [ ] Test: Paste image from clipboard, appears at center
- [ ] Test: Click to select object, shows selection box
- [ ] Test: Shift+click adds to selection
- [ ] Test: Drag selected objects, moves them
- [ ] Test: Delete key removes selected objects
- [ ] Test: Ctrl+C then Ctrl+V duplicates object
- [ ] Test: Ctrl+Z undoes insert/delete/move
- [ ] Test: zOrder shortcuts work correctly

---

## Phase O3: Enhanced Features (Minimal)

**Goal:** Document future enhancements, implement only critical items.

---

### O3.1: Object Resize (Deferred)

**Status:** Interface documented, implementation deferred.

**When needed:**
- [ ] Add resize handles to selection box
- [ ] Track resize drag state
- [ ] Implement `resizeSelectedObject(newSize)`
- [ ] Create undo entry for resize

---

### O3.2: LinkObject (Deferred)

**Status:** Documented as future object type.

**When needed:**
- [ ] Create `LinkObject` subclass of `InsertedObject`
- [ ] Properties: `targetType` (page/coordinate), `targetPageIndex`, `targetPosition`
- [ ] Render as clickable icon/text
- [ ] On click, navigate viewport to target

---

### O3.3: File Dialog Insert (Deferred)

**Status:** Interface documented at `insertImageFromDialog()`.

**When needed:**
- [ ] Show file dialog for image selection
- [ ] Load image, create ImageObject
- [ ] Use same insertion flow as clipboard

---

### O3.4: Drag & Drop (Deferred)

**Status:** Interface documented.

**When needed:**
- [ ] Handle `dragEnterEvent()` for image files
- [ ] Handle `dropEvent()` to insert at drop position

---

### O3.5: Layer Panel Integration (Deferred)

**Status:** Documented in DD-9.

**When needed:**
- [ ] Show objects in layer panel under affiliated layer
- [ ] Allow drag to change affinity
- [ ] Right-click menu for "Move to Layer X"

---

### O3.6: Object Properties Panel (Deferred)

**Status:** Documented as future UI.

**When needed:**
- [ ] Show panel when object selected
- [ ] Display/edit: position, size, rotation, affinity, locked
- [ ] ImageObject: show path, aspect ratio lock

---

## Task Summary

| Phase | Tasks | Priority | Est. Effort |
|-------|-------|----------|-------------|
| O1.1 | Layer affinity property | P0 | 1 hour |
| O1.2 | Object grouping by affinity | P0 | 2 hours |
| O1.3 | Paged mode interleaved rendering | P0 | 2 hours |
| O1.4 | Edgeless multi-pass rendering | P0 | 4 hours |
| O1.5 | Extended tile loading margin | P0 | 1 hour |
| O1.6 | Unified bundle format | P0 | 3 hours |
| O1.7 | Paged mode lazy loading | P0 | 6 hours |
| O1.8 | Testing Phase 1 | P0 | 2 hours |
| **Phase 1 Total** | | | **~21 hours** |
| O2.1 | Object select tool | P1 | 3 hours |
| O2.2 | Selection API | P1 | 1 hour |
| O2.3 | Object movement | P1 | 2 hours |
| O2.4 | Clipboard paste | P1 | 2 hours |
| O2.5 | Object deletion | P1 | 1 hour |
| O2.6 | Object copy/paste | P1 | 2 hours |
| O2.7 | Undo/redo integration | P1 | 3 hours |
| O2.8 | zOrder shortcuts | P1 | 1 hour |
| O2.9 | MainWindow connection | P1 | 1 hour |
| O2.10 | Testing Phase 2 | P1 | 2 hours |
| **Phase 2 Total** | | | **~18 hours** |
| O3.x | Deferred features | P2 | TBD |

**Total Estimated: ~39 hours for MVP (Phase 1 + Phase 2)**

---

## Execution Order

```
O1.1 → O1.2 → O1.3 ─┬─→ O1.6 → O1.7 → O1.8
                    │
O1.4 → O1.5 ────────┘
                    
O2.1 → O2.2 → O2.3 → O2.4 → O2.5 → O2.6 → O2.7 → O2.8 → O2.9 → O2.10
```

Phase 1 tasks can be parallelized:
- O1.1-O1.3 (paged rendering) independent from O1.4-O1.5 (edgeless rendering)
- O1.6-O1.7 (bundle format) depends on both

Phase 2 is mostly sequential (each builds on previous).

---

*Implementation begins with O1.1: Layer Affinity Property*
