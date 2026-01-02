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

### O1.3: Paged Mode Interleaved Rendering ✅

**Goal:** Render objects interleaved with layers based on affinity.
**Status:** COMPLETE

#### O1.3.1: Update Page::render() (if exists) or renderPage() ✅
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
- [x] Modify `renderPage()` to call `page->renderObjectsWithAffinity(painter, zoom, -1)` after background
- [x] Modify layer loop to call `page->renderObjectsWithAffinity(painter, zoom, layerIdx)` after each layer
- [x] Remove old `page->renderObjects()` call (replaced by interleaved calls)

---

### O1.4: Edgeless Multi-Pass Rendering ✅

**Goal:** Implement multi-pass rendering for correct cross-tile object display.
**Status:** COMPLETE

#### O1.4.1: Refactor renderEdgelessMode ✅
**File:** `source/core/DocumentViewport.cpp`

**Tasks:**
- [x] Background rendering kept inline (already well-organized)
- [x] Extract stroke rendering into helper: `renderTileLayerStrokes(int layerIdx)`
- [x] Create `renderEdgelessObjectsWithAffinity(int affinity)` method

**Implementation Notes:**
- `renderTileLayerStrokes()` renders a single layer's strokes from a tile
- `renderEdgelessObjectsWithAffinity()` renders objects from all tiles at document coordinates
- Removed object rendering from `renderTileStrokes()` (now handled by multi-pass)

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
- [x] Implement the multi-pass structure
- [x] Ensure correct painter state management (save/restore)

#### O1.4.3: Implement renderEdgelessObjectsWithAffinity ✅
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
- [x] Implement the method
- [x] Uses tile coordinates × tileSize for document position (no separate helper needed)
- [x] Uses `allTiles` parameter (from tilesInRect) rather than separate accessor

**Actual Implementation:**
```cpp
void DocumentViewport::renderEdgelessObjectsWithAffinity(
    QPainter& painter, int affinity, const QVector<Document::TileCoord>& allTiles)
{
    // Iterate all loaded tiles and render objects with matching affinity
    for (const auto& coord : allTiles) {
        Page* tile = m_document->getTile(coord.first, coord.second);
        if (!tile) continue;
        
        auto it = tile->objectsByAffinity.find(affinity);
        if (it == tile->objectsByAffinity.end() || it->second.empty()) continue;
        
        QPointF tileOrigin(coord.first * tileSize, coord.second * tileSize);
        
        // Sort and render objects at document coordinates
        std::vector<InsertedObject*> objs = it->second;
        std::sort(objs.begin(), objs.end(), [](auto* a, auto* b) {
            return a->zOrder < b->zOrder;
        });
        
        for (InsertedObject* obj : objs) {
            if (!obj->visible) continue;
            QPointF docPos = tileOrigin + obj->position;
            QRectF objRect(docPos, obj->size);
            if (!objRect.intersects(viewRect.adjusted(-200, -200, 200, 200))) continue;
            
            painter.save();
            painter.translate(docPos);
            obj->render(painter, 1.0);
            painter.restore();
        }
    }
}
```

---

### O1.5: Extended Tile Loading Margin

**Goal:** Load extra tiles to capture objects that extend into viewport.

#### O1.5.1: Track Maximum Object Extent ✅
**File:** `source/core/Document.h/.cpp`

**Tasks:**
- [x] Add `mutable int m_maxObjectExtent = 0` member (mutable for lazy loading)
- [x] Add `void updateMaxObjectExtent(const InsertedObject* obj)` - updates if obj is larger
- [x] Add `void recalculateMaxObjectExtent()` - full scan after object removal
- [x] Add `int maxObjectExtent() const` accessor
- [x] Update `loadTileFromDisk()` to update extent when loading tiles with objects
- [x] Update `loadPagesFromJson()` to update extent when loading pages with objects

**Implementation Notes:**
- `m_maxObjectExtent` is mutable because it's updated in `loadTileFromDisk()` which is const
- Object extent updated automatically when tiles/pages are loaded
- For object insertion (Phase O2), caller should call `updateMaxObjectExtent()`
- For object removal, caller should call `recalculateMaxObjectExtent()` if needed

#### O1.5.2: Expand Tile Loading Range ✅
**File:** `source/core/DocumentViewport.cpp`

**Tasks:**
- [x] In `renderEdgelessMode()`, calculate `objectMargin = m_document->maxObjectExtent()`
- [x] Use `totalMargin = qMax(STROKE_MARGIN, objectMargin)` for tile loading
- [x] Existing background filtering already handles margin tiles (backgrounds not rendered for margin tiles)

**Implementation Notes:**
- Changed `strokeRect` calculation to use `totalMargin` instead of just `STROKE_MARGIN`
- Margin tiles (outside visible rect) are loaded for objects/strokes but backgrounds are not rendered
- This ensures objects extending from margin tiles into the viewport are fully rendered

---

### O1.6: Unified Bundle Format

**Goal:** Both paged and edgeless use .snb bundle with assets folder.

#### O1.6.1: Create Assets Directory Structure ✅
**File:** `source/core/Document.cpp`, `source/core/Document.h`

**Tasks:**
- [x] In `saveBundle()`, create `assets/images/` directory
- [x] Add helper: `QString assetsImagePath() const` in Document.h

**Implementation Notes:**
- Directory created in `saveBundle()` alongside `tiles/` directory
- Helper returns empty string if bundle path not set (handles unsaved documents)

#### O1.6.2: Update ImageObject Path Resolution ✅
**File:** `source/objects/ImageObject.h/.cpp`

**Tasks:**
- [x] Modify `fullPath()` to resolve against `bundlePath/assets/images/`
- [x] Add `saveToAssets(const QString& bundlePath)` method:
  - Calculates hash if not already set
  - Saves to `assets/images/{hash16}.png` if not exists
  - Updates `imagePath` to just filename
  - Returns true if already exists (deduplication)

**Implementation Notes:**
- `fullPath()` still handles absolute paths for legacy compatibility
- Hash uses first 16 characters of SHA-256 (64-bit collision resistance)
- PNG format used for lossless storage

#### O1.6.3: Update Image Loading ✅
**File:** `source/core/Page.h`

**Tasks:**
- [x] Update `loadImages()` docstring to clarify basePath is bundle path
- [x] Relative path resolution works via `ImageObject::fullPath()` (O1.6.2)

**Implementation Notes:**
- No code change needed in `loadImages()` itself
- Path resolution encapsulated in `ImageObject::fullPath()` which adds `/assets/images/`
- Callers should pass bundle path (e.g., `/path/to/notebook.snb`)
- Images are loaded on-demand when `loadImages()` is called (lazy loading)

---

### O1.7: Paged Mode Lazy Loading

**Goal:** Implement lazy loading for paged mode pages.

#### O1.7.1: Add Lazy Loading Members ✅
**File:** `source/core/Document.h/.cpp`

```cpp
// Paged mode lazy loading
QStringList m_pageOrder;                              // Ordered page UUIDs
std::map<QString, QSizeF> m_pageMetadata;             // Sizes for layout
mutable std::map<QString, std::unique_ptr<Page>> m_loadedPages;
mutable std::set<QString> m_dirtyPages;
std::set<QString> m_deletedPages;                     // For cleanup on save
```

**Tasks:**
- [x] Add all member variables to Document.h
- [x] Add accessors: `isPageLoaded()`, `pageUuidAt()`, `pageSizeAt()`
- [x] Implement accessors with legacy m_pages fallback

**Implementation Notes:**
- Accessors support both legacy mode (m_pages) and lazy loading mode (m_pageOrder)
- Legacy mode detected by `m_pageOrder.isEmpty()`
- `pageSizeAt()` checks metadata first, falls back to loading page

#### O1.7.2: Implement page() with Lazy Loading ✅
**File:** `source/core/Document.h/.cpp`

**Tasks:**
- [x] Modify `page(int index)` to check `m_loadedPages`, load on demand
- [x] Implement `loadPageFromDisk(int index)` - loads from pages/{uuid}.json
- [x] Implement `savePage(int index)` - saves to pages/{uuid}.json
- [x] Implement `evictPage(int index)` - saves if dirty, then removes from memory
- [x] Implement `markPageDirty(int index)` and `isPageDirty(int index)`

**Implementation Notes:**
- `page()` checks for lazy loading mode via `!m_pageOrder.isEmpty()`
- Falls back to legacy `m_pages` when `m_pageOrder` is empty
- `loadPageFromDisk()` updates `m_maxObjectExtent` for loaded objects
- `savePage()` also updates `m_pageMetadata` with current page size

#### O1.7.3: Update Page Insert/Remove/Move ✅
**File:** `source/core/Document.h/.cpp`

**Tasks:**
- [x] Update `pageCount()` to return `m_pageOrder.size()` in lazy mode
- [x] Update `addPage()` to generate UUID, update `m_pageOrder`
- [x] Update `insertPage()` to use UUID, update `m_pageOrder`
- [x] Update `removePage()` to track in `m_deletedPages`
- [x] Update `movePage()` to just reorder `m_pageOrder`

**Implementation Notes:**
- All methods check `m_pageOrder.isEmpty()` for legacy vs lazy mode
- `addPage()` / `insertPage()` generate UUID, add to metadata, mark dirty
- `removePage()` adds UUID to `m_deletedPages` for cleanup on save
- `movePage()` only reorders `m_pageOrder` - no file operations needed

#### O1.7.4: Update Bundle Save/Load for Paged Mode ✅
**File:** `source/core/Document.cpp`

**Tasks:**
- [x] `saveBundle()`: Write pages to `pages/{uuid}.json`, include `page_order` and `page_metadata` in manifest
- [x] `loadBundle()`: Parse `page_order`, `page_metadata`, enable lazy loading
- [x] Delete files for pages in `m_deletedPages`

**Implementation Notes:**

**saveBundle() Changes:**
- Mode-specific directory creation: `tiles/` for edgeless, `pages/` for paged
- Legacy conversion: If `m_pageOrder` is empty but `m_pages` has content, converts to UUID-based format
- Writes `page_order` array and `page_metadata` object to manifest
- Copies evicted pages when saving to new location (similar to tile handling)
- Saves only dirty pages (or all if saving to new location)
- Deletes page files for pages in `m_deletedPages`

**loadBundle() Changes:**
- Mode-specific parsing: tile_index for edgeless, page_order for paged
- Parses `page_order` array into `m_pageOrder`
- Parses `page_metadata` into `m_pageMetadata` (with A4 default fallback)
- Does NOT load page contents - pages loaded on-demand via `page()` accessor

**Manifest Structure for Paged Bundle:**
```json
{
  "format_version": "1.0",
  "mode": "paged",
  "page_order": ["uuid1", "uuid2", "uuid3"],
  "page_metadata": {
    "uuid1": { "width": 595.0, "height": 842.0 },
    "uuid2": { "width": 595.0, "height": 842.0 }
  }
}
```

#### O1.7.5: Update DocumentViewport Page Access ✅
**File:** `source/core/DocumentViewport.cpp`

**Tasks:**
- [x] In `preloadStrokeCaches()`, page loading is now triggered via `page()` call for visible ±1 pages
- [x] Add page eviction for pages far from visible (visible ±2 buffer)
- [x] Update `ensurePageLayoutCache()` to use `pageSizeAt()` instead of `page(i)->size`

**Implementation Notes:**

**preloadStrokeCaches() Changes:**
- Added early return for edgeless mode (uses tile-based loading instead)
- When lazy loading is enabled: evict entire pages outside keep range via `evictPage()`
- When legacy mode: only evict stroke caches, keep pages in memory
- Calling `page()` for pages in preload range automatically triggers lazy loading

**ensurePageLayoutCache() Changes:**
- Replaced `page()->size` with `pageSizeAt()` for both single-column and two-column layouts
- This is critical for lazy loading: layout can now be calculated from manifest metadata alone
- No pages are loaded during layout cache building, only metadata is accessed

**Memory Management Pattern:**
```
Visible pages:     [5, 6, 7]
Preload range:     [4, 5, 6, 7, 8]   (visible ±1, loaded)
Keep range:        [3, 4, 5, 6, 7, 8, 9]   (visible ±2, not evicted)
Eviction range:    [0, 1, 2] and [10, 11, ...]  (evicted to disk)
```

#### O1.7.6: Route Paged Mode Through Bundle Save ✅
**Files:** `source/core/DocumentManager.cpp`, `source/MainWindow.cpp`

**Tasks:**
- [x] Remove old single-file JSON save path
- [x] Route all documents through `saveBundle()`
- [x] Update file dialogs to use `.snb` extension

**Implementation Notes:**

**DocumentManager.cpp - doSave() Changes:**
- Removed the separate paged document JSON save path (`toFullJson()` branch)
- ALL documents (paged and edgeless) now go through `saveBundle()`
- This enables lazy loading, asset storage, and consistent save/load for all document types

**MainWindow.cpp - Save Dialog Changes:**
- Save dialog now uses `.snb` extension for ALL documents (not just edgeless)
- Filter simplified to: `"SpeedyNote Bundle (*.snb);;All Files (*)"`

**MainWindow.cpp - Open Dialog Changes:**
- Filter updated to prioritize `.snb` bundles: `"SpeedyNote Files (*.snb *.json *.snx);;..."`
- Legacy `.json` and `.snx` files still loadable for backward compatibility

**Backward Compatibility:**
- Loading: Old `.json` files can still be loaded (DocumentManager.loadDocument handles this)
- Saving: When an old `.json` file is resaved, it becomes a `.snb` bundle

**Bundle Loading (Ctrl+Shift+L):**
- Renamed `loadEdgelessDocument()` → `loadFolderDocument()` to reflect its purpose
- Function now handles BOTH paged and edgeless `.snb` bundles
- Detects mode from manifest and applies appropriate setup:
  - Edgeless: Centers on origin (pan offset)
  - Paged: Goes to first page (centered)
- Dialog title: "Open SpeedyNote Bundle (.snb folder)"
- Debug message correctly identifies document type

**Why "loadFolderDocument":**
- `.snb` bundles are currently directories (not single files)
- `QFileDialog::getOpenFileName()` can't select directories
- This function uses `QFileDialog::getExistingDirectory()` instead
- Name distinguishes from future `loadDocument()` when `.snb` becomes a single file

---

### O1.8: Code Review & Bug Fixes ✅

**Goal:** Review Phase O1 code for issues before proceeding.
**Status:** COMPLETE

#### Issues Found and Fixed

**Issue 1: Assets Not Copied on "Save As" (CRITICAL - Fixed)**
- **File:** `source/core/Document.cpp` - `saveBundle()`
- **Problem:** When saving to a new location (`savingToNewLocation`), we copied evicted tiles/pages but NOT the `assets/images/` folder. This would cause all image references to break.
- **Fix:** Added a new block after checking `savingToNewLocation` that copies all files from `oldBundlePath/assets/images/` to `path/assets/images/`.
- **Impact:** Without fix, "Save As" would lose all inserted images.

**Issue 2: Page::render() Bypasses Affinity System (Documented)**
- **File:** `source/core/Page.h`, `source/core/Page.cpp`
- **Problem:** The `Page::render()` method renders objects AFTER all layers, not interleaved based on affinity.
- **Impact:** Low - this method is only used for export/preview, not live rendering. DocumentViewport::renderPage() uses the correct interleaved approach.
- **Fix:** Added `@deprecated` documentation to clarify the limitation.

**Issue 3: No Issues Found (Verified Correct)**
- `Page::fromJson()` correctly calls `rebuildAffinityMap()` ✅
- `Page::clearContent()` correctly clears `objectsByAffinity` ✅
- `preloadStrokeCaches()` correctly handles lazy vs legacy mode ✅
- Lazy loading mutable members are correctly declared ✅
- Object grouping by affinity is maintained in addObject/removeObject ✅

#### Performance Notes

**Noted but not fixed (acceptable for current use cases):**
- `preloadStrokeCaches()` iterates all pages when evicting. Could be optimized for very large documents (500+ pages) by tracking the previous keep range.
- Object sorting in `renderObjectsWithAffinity()` creates a copy of the vector. For pages with many objects (100+), consider pre-sorting in the affinity map.

---

### O1.9: Testing & Verification

**Tasks:**
- [ ] Test: Create edgeless doc with objects, verify cross-tile rendering
- [ ] Test: Create paged doc with objects, verify layer-interleaved rendering
- [ ] Test: Save/load bundle with images, verify assets folder
- [ ] Test: **"Save As" to new location with images - verify images copied**
- [ ] Test: Lazy loading - load 100+ page doc, verify memory usage
- [ ] Test: Object affinity -1, 0, 1 render in correct order

---

## Phase O2: Interaction Layer (MVP)

**Goal:** Enable users to insert, select, and manipulate objects.

**Prerequisites from O1:**
- `layerAffinity` property on InsertedObject ✅
- `objectsByAffinity` map in Page with `addObject()`/`removeObject()` ✅
- `renderObjectsWithAffinity()` for correct rendering order ✅
- `updateMaxObjectExtent()` / `recalculateMaxObjectExtent()` in Document ✅
- `saveToAssets()` in ImageObject for hash-based storage ✅
- `markPageDirty()` for lazy loading persistence ✅
- `markTileDirty()` for edgeless persistence ✅

**Helper Methods to Implement (O2.0):**
- `viewportCenterInDocument()` - returns center of visible viewport in document coords
- `pushObjectInsertUndo()` / `pushObjectDeleteUndo()` / `pushObjectMoveUndo()` - undo helpers
- Object-specific undo handling in `undo()` / `redo()` methods

---

### O2.1: Object Select Tool

**Goal:** Add dedicated tool for selecting objects.

#### O2.1.1: Add ToolType::ObjectSelect ✅
**File:** `source/core/ToolType.h` (NOT DocumentViewport.h - ToolType is in separate file)

**Tasks:**
- [x] Add `ObjectSelect` to `ToolType` enum in `ToolType.h`

**File:** `source/core/DocumentViewport.h`

**Tasks:**
- [x] Add member: `QList<InsertedObject*> m_selectedObjects`
- [x] Add member: `InsertedObject* m_hoveredObject = nullptr`

**Implementation Notes:**
- `InsertedObject` is available via transitive include: `DocumentViewport.h` → `Page.h` → `InsertedObject.h`
- Members added in "Object Selection (Phase O2)" section after lasso selection members
- Both are non-owning pointers (objects owned by `Page::objects`)

#### O2.1.2: Implement Object Hit Testing ✅
**File:** `source/core/DocumentViewport.cpp`

```cpp
InsertedObject* DocumentViewport::objectAtPoint(const QPointF& docPoint) const
{
    if (m_document->isEdgeless()) {
        // Check all loaded tiles (use allLoadedTileCoords, not loadedTiles which doesn't exist)
        for (const auto& coord : m_document->allLoadedTileCoords()) {
            Page* tile = m_document->getTile(coord.first, coord.second);
            if (!tile) continue;
            
            // Convert document coords to tile-local coords
            QPointF tileLocal = docPoint - QPointF(
                coord.first * Document::EDGELESS_TILE_SIZE,
                coord.second * Document::EDGELESS_TILE_SIZE
            );
            
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
- [x] Implement `objectAtPoint()` as shown above
- [x] Declaration added to `DocumentViewport.h` (const method)
- [x] Tile-local conversion inline (no separate helper needed)

**Implementation Notes:**
- Method is `const` since it only reads data
- Edgeless: iterates all loaded tiles via `allLoadedTileCoords()`
- Paged: uses existing `pageAtPoint()` + `pagePosition()` for coordinate conversion
- Delegates to `Page::objectAtPoint()` which handles z-order (topmost first)

#### O2.1.3: Handle ObjectSelect Tool Input ✅
**File:** `source/core/DocumentViewport.cpp`

**Tasks:**
- [x] In `handlePointerPress()`, if ObjectSelect tool:
  - Hit test for object via `objectAtPoint()`
  - If Shift held, toggle selection; else replace selection
  - If no object hit and no Shift, deselect all
  - Start drag if clicking on selected object(s)
- [x] In `handlePointerMove()`, update hover state, handle drag if active
- [x] In `handlePointerRelease()`, finalize drag, mark page/tile dirty

**Implementation Notes:**
- Added three handler methods: `handlePointerPress_ObjectSelect()`, `handlePointerMove_ObjectSelect()`, `handlePointerRelease_ObjectSelect()`
- Added `clearObjectSelection()` helper method
- Added member variables: `m_isDraggingObjects`, `m_objectDragStartViewport`, `m_objectDragStartDoc`
- Drag moves all selected objects by the same delta
- Hover state tracked in `m_hoveredObject` (for visual feedback in O2.1.4)
- Undo entry creation marked as TODO for O2.7

#### O2.1.4: Selection Visual Feedback ✅
**File:** `source/core/DocumentViewport.cpp`

**Tasks:**
- [x] In render loop, after objects, draw selection boxes for `m_selectedObjects`
- [x] Draw resize handles at corners (for single selection - same style as lasso handles)
- [x] Draw hover highlight for `m_hoveredObject`

**Implementation Notes:**
- Added `renderObjectSelection(QPainter&)` method
- Called from both paged and edgeless render paths (after lasso selection)
- Hover: Light blue semi-transparent fill + 2px outline (visible when not selected)
- Selection boxes: Marching ants dashed line (black over white for visibility)
- Single selection: 8 scale handles (squares) + 1 rotation handle (circle with line)
- Handle sizes use same constants as lasso: `HANDLE_VISUAL_SIZE`, `ROTATE_HANDLE_OFFSET`

---

### O2.2: Object Selection API ✅

**Goal:** Implement selection management methods.

#### O2.2.1: Selection Methods
**File:** `source/core/DocumentViewport.cpp`

**Tasks:**
- [x] Implement `selectObject(obj, addToSelection)` - replaces or adds to selection
- [x] Implement `deselectObject(obj)` - removes single object from selection
- [x] Implement `deselectAllObjects()` - clears selection
- [x] Implement `selectedObjects()` getter - inline, returns `const QList<InsertedObject*>&`
- [x] Implement `hasSelectedObjects()` getter - inline, returns `!m_selectedObjects.isEmpty()`
- [x] Emit `objectSelectionChanged` signal when selection changes

**Implementation Notes:**
- All methods emit `objectSelectionChanged()` signal only when selection actually changes
- `selectObject()` with `addToSelection=false` replaces selection
- `selectObject()` with `addToSelection=true` adds to existing selection
- Updated `handlePointerPress_ObjectSelect()` to use the API methods
- Updated `clearObjectSelection()` to emit signal

---

### O2.3: Object Movement

**Goal:** Allow dragging selected objects.

#### O2.3.1: Drag State Tracking ✅
**File:** `source/core/DocumentViewport.h`

**Tasks:**
- [x] Add `bool m_isDraggingObjects = false` (added in O2.1.3)
- [x] Add `QPointF m_objectDragStartViewport` (added in O2.1.3)
- [x] Add `QPointF m_objectDragStartDoc` (added in O2.1.3)
- [x] Add `QMap<QString, QPointF> m_objectOriginalPositions` (for undo - added now)

**Implementation Notes:**
- Most drag state was added during O2.1.3 when implementing pointer handlers
- `m_objectOriginalPositions` maps object ID → original position at drag start
- This is used to create proper undo entries when drag completes

#### O2.3.2: Implement Drag Logic ✅
**File:** `source/core/DocumentViewport.cpp`

**Tasks:**
- [x] On pointer press on selected object: start drag, store original positions in `m_objectOriginalPositions`
- [x] On pointer move: calculate delta, move objects (inline, moveSelectedObjects deferred to O2.3.3)
- [x] On pointer release: check if moved, mark page dirty, clear original positions
- [x] TODO placeholder for undo entry creation (O2.7)

**Implementation Notes:**
- `handlePointerPress_ObjectSelect`: Stores `obj->id → obj->position` for all selected objects
- `handlePointerRelease_ObjectSelect`: Checks if any object actually moved before marking dirty
- Original positions cleared after drag ends (ready for next drag)

#### O2.3.3: Implement moveSelectedObjects ✅
**File:** `source/core/DocumentViewport.cpp`

```cpp
void DocumentViewport::moveSelectedObjects(const QPointF& delta)
{
    if (m_selectedObjects.isEmpty() || delta.isNull()) return;
    
    for (InsertedObject* obj : m_selectedObjects) {
        if (obj) obj->position += delta;
    }
    
    // Note: Dirty marking done on drag release (O2.3.2)
    // Tile boundary crossing handled in O2.3.4
    
    update();
}
```

**Tasks:**
- [x] Implement the method with null checks
- [x] Declaration added to header
- [x] Updated `handlePointerMove_ObjectSelect` to call this method
- [x] Dirty marking deferred to drag release (avoids marking on every micro-movement)
- [ ] Handle page/tile boundary crossing (O2.3.4)

**Implementation Notes:**
- Method is public for potential external use (e.g., keyboard nudge)
- Returns early if no selection or zero delta
- `update()` called to refresh viewport
- Dirty marking happens in `handlePointerRelease_ObjectSelect` (O2.3.2)

#### O2.3.4: Handle Tile Boundary Crossing (Edgeless) ✅
**File:** `source/core/DocumentViewport.cpp`

When an object moves across tile boundaries in edgeless mode:
1. Remove object from old tile
2. Add object to new tile (with updated tile-local coordinates)
3. Mark both tiles dirty

**Tasks:**
- [x] Detect when object moves to different tile (using `tileCoordForPoint()`)
- [x] Added `Page::extractObject(id)` - returns ownership instead of destroying
- [x] Added `relocateObjectsToCorrectTiles()` - handles relocation logic
- [x] Recalculate tile-local position based on new tile origin
- [x] Mark both old and new tiles dirty
- [x] Updated `handlePointerRelease_ObjectSelect` to call relocation

**Implementation Notes:**
- `Page::extractObject(QString id)` returns `std::unique_ptr<InsertedObject>` for transfer
- `relocateObjectsToCorrectTiles()` iterates selected objects, finds their current tile,
  calculates target tile from document position, and moves if different
- Object pointers in `m_selectedObjects` remain valid (same address after move)
- Both source and destination tiles marked dirty for persistence

---

### O2.4: Clipboard Paste (MVP)

**Goal:** Paste images from clipboard as objects with tool-aware behavior.

**Architecture Decision:** Paste behavior is tool-dependent:
| Tool | Ctrl+V Behavior |
|------|-----------------|
| Lasso | Paste strokes from internal clipboard (existing `pasteSelection()`) |
| ObjectSelect | 1. System clipboard image → `insertImageFromClipboard()` |
|              | 2. Internal object clipboard → `pasteObjects()` (O2.6) |
| Pen/Marker/etc. | No paste action |

#### O2.4.1: Make Paste Handler Tool-Aware ✅
**File:** `source/core/DocumentViewport.cpp`

**Tasks:**
- [x] Modify Ctrl+V handler in `keyPressEvent()` to check current tool:
  - If `m_currentTool == ToolType::Lasso`: call existing `pasteSelection()` (already in Lasso block)
  - If `m_currentTool == ToolType::ObjectSelect`: call new `pasteForObjectSelect()`
  - Otherwise: paste is not handled (falls through)

**Implementation Notes:**
- Added new keyboard handling block for ObjectSelect tool after the existing Lasso block
- Added `pasteForObjectSelect()` declaration to header in "Object Selection API" section
- Added stub implementation that logs and does nothing (actual logic in O2.4.2)
- Lasso paste was already tool-aware (inside `if (m_currentTool == ToolType::Lasso)` block)
- TODO placeholders added for Copy (O2.6) and Delete (O2.5) in ObjectSelect block

#### O2.4.2: Implement pasteForObjectSelect ✅
**File:** `source/core/DocumentViewport.cpp`

```cpp
void DocumentViewport::pasteForObjectSelect()
{
    // Priority 1: System clipboard has image → insert as ImageObject
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard && clipboard->mimeData() && clipboard->mimeData()->hasImage()) {
        insertImageFromClipboard();
        return;
    }
    
    // Priority 2: Internal object clipboard (O2.6 will add m_objectClipboard)
    // TODO O2.6: Check m_objectClipboard and call pasteObjects()
    
    // If neither, do nothing (no fallback to lasso paste)
}
```

**Tasks:**
- [x] Implement `pasteForObjectSelect()` as entry point for ObjectSelect paste
- [x] Call `insertImageFromClipboard()` if system clipboard has image
- [x] Add placeholder for `pasteObjects()` (to be implemented in O2.6)

**Implementation Notes:**
- Added includes: `QClipboard`, `QGuiApplication`, `QMimeData`
- Uses `QGuiApplication::clipboard()` (works with Qt Quick/QML too)
- Null checks on clipboard and mimeData for safety
- Added `insertImageFromClipboard()` declaration and stub (O2.4.3)
- Commented placeholder for O2.6's `pasteObjects()`

#### O2.4.3: Implement insertImageFromClipboard ✅
**File:** `source/core/DocumentViewport.cpp`

**Tasks:**
- [x] Implement `insertImageFromClipboard()` (note: rawPtr saved before move!)
- [x] Add helper `viewportCenterInDocument()` - returns viewport center in document coords
- [x] Image saved to assets via `saveToAssets()` for persistence
- [x] Verify `markTileDirty()` and `markPageDirty()` are called
- [x] Handle paged mode: convert center position to page-local coordinates
- [x] Emit `documentModified()` signal
- [x] TODO placeholder for undo (O2.7)

**Implementation Notes:**

1. **viewportCenterInDocument()** added as helper:
   ```cpp
   QPointF viewportCenterInDocument() const {
       QPointF viewportCenter(width() / 2.0, height() / 2.0);
       return viewportToDocument(viewportCenter);
   }
   ```

2. **insertImageFromClipboard()** flow:
   - Get image from clipboard via `QGuiApplication::clipboard()->image()`
   - Create `ImageObject` with `setPixmap(QPixmap::fromImage(image))`
   - Position at center of viewport, offset by half size to center the image
   - Set `layerAffinity = -1` (below all strokes, default for test papers)
   - **Critical**: Save raw pointer before `std::move` invalidates the unique_ptr
   - Edgeless: find tile via `tileCoordForPoint()`, convert to tile-local coords
   - Paged: convert position to page-local coords (subtract page origin)
   - Call `updateMaxObjectExtent()` for extended tile loading margin
   - Save to assets folder if bundle path exists
   - Select the new object
   - Emit `documentModified()` and `update()`

3. **Paged mode fix**: Original plan didn't account for page-local coordinates.
   Added: `imgObj->position = imgObj->position - pageOrigin;`

---

### O2.5: Object Deletion

**Goal:** Delete selected objects with Delete key.

#### O2.5.1: Handle Delete Key ✅
**File:** `source/core/DocumentViewport.cpp`

**Tasks:**
- [x] In `keyPressEvent()`, if Delete pressed and `hasSelectedObjects()`:
  - Call `deleteSelectedObjects()`

**Implementation Notes:**
- Added Delete key (and Backspace) handler in ObjectSelect tool keyboard block
- Added `deleteSelectedObjects()` declaration in header
- Added stub implementation with TODO comments for O2.5.2
- Both `Qt::Key_Delete` and `Qt::Key_Backspace` trigger deletion (common UX pattern)

#### O2.5.2: Implement deleteSelectedObjects ✅
**File:** `source/core/DocumentViewport.cpp`

**Tasks:**
- [x] For each selected object:
  - Find containing page/tile
  - Create undo entry (TODO O2.7 placeholder)
  - Call `page->removeObject(obj->id)`
  - Mark page/tile dirty
- [x] Call `m_document->recalculateMaxObjectExtent()` (removed object might have been largest)
- [x] Clear selection and nullify `m_hoveredObject`
- [x] Emit `objectSelectionChanged()` and `documentModified()` signals
- [x] Update viewport

**Implementation Notes:**

**Edgeless mode:**
- Iterates all loaded tiles via `allLoadedTileCoords()`
- Finds tile containing each object via `tile->objectById()`
- Removes object and marks tile dirty

**Paged mode:**
- First checks current page (most common case)
- Falls back to searching all pages if not found on current page
- Handles edge case where objects might be selected across pages

**Safety:**
- Clears selection AFTER removal (pointers become invalid)
- Sets `m_hoveredObject = nullptr` (may have pointed to deleted object)
- Only emits `documentModified()` if objects were actually deleted

---

### O2.6: Object Copy/Paste

**Goal:** Copy selected objects, paste duplicates via internal clipboard.

**Integration with O2.4:** `pasteForObjectSelect()` checks system clipboard for images first,
then falls back to `pasteObjects()` for internal object clipboard.

#### O2.6.1: Internal Object Clipboard ✅
**File:** `source/core/DocumentViewport.h`

**Tasks:**
- [x] Add `QList<QJsonObject> m_objectClipboard` (serialized objects)

**Implementation Notes:**
- Added after `m_objectOriginalPositions` in Object Selection section
- Each entry is a complete JSON representation of an InsertedObject
- Separate from system clipboard (for internal object copy/paste only)

#### O2.6.2: Implement copySelectedObjects ✅
**File:** `source/core/DocumentViewport.cpp`

**Tasks:**
- [x] Serialize each selected object to JSON via `obj->toJson()`
- [x] Store in `m_objectClipboard`
- [x] Wire Ctrl+C to call this when ObjectSelect tool is active

**Implementation Notes:**
- Declaration added to `DocumentViewport.h` after `deleteSelectedObjects()`
- Implementation clears clipboard, then appends each selected object's JSON
- Ctrl+C wired in `keyPressEvent()` before paste handling (Copy before Paste order)
- Only copies if `hasSelectedObjects()` returns true

#### O2.6.3: Implement pasteObjects ✅
**File:** `source/core/DocumentViewport.cpp`

**Tasks:**
- [x] Called from `pasteForObjectSelect()` when no system clipboard image
- [x] If `m_objectClipboard` not empty:
  - Deserialize each object via `InsertedObject::fromJson()`
  - Assign new UUIDs
  - Offset position (+20, +20) from original
  - Add to current page/tile
  - ~~Create undo entries~~ (TODO O2.7)
  - Select pasted objects

**Implementation Notes:**
- Declaration added to `DocumentViewport.h` after `copySelectedObjects()`
- For ImageObjects, calls `loadImage()` to load pixmap from assets folder
- Uses same tile/page logic as `insertImageFromClipboard()`
- Stores raw pointers before std::move for selection tracking
- Updated `pasteForObjectSelect()` to call `pasteObjects()` as Priority 2

---

### O2.7: Undo/Redo Integration

**Goal:** Make object operations undoable.

**Note:** The existing undo structs (`PageUndoAction`, `EdgelessUndoAction`) are stroke-specific.
We need to extend them or create parallel object undo types.

#### O2.7.1: Extend Undo Action Types
**File:** `source/core/DocumentViewport.h`

**Current struct (stroke-only):**
```cpp
struct PageUndoAction {
    enum Type { 
        AddStroke, RemoveStroke, RemoveMultiple, TransformSelection
    };
    // ... stroke-specific fields
};
```

**Extended struct (add object support):**
```cpp
struct PageUndoAction {
    enum Type { 
        // Stroke types (existing)
        AddStroke, RemoveStroke, RemoveMultiple, TransformSelection,
        // Object types (new)
        ObjectInsert, ObjectDelete, ObjectMove, ObjectAffinityChange
    };
    
    Type type;
    int pageIndex;
    int layerIndex = 0;
    
    // Stroke fields (existing)
    VectorStroke stroke;
    QVector<VectorStroke> strokes;
    QVector<VectorStroke> removedStrokes;
    QVector<VectorStroke> addedStrokes;
    
    // Object fields (new)
    QJsonObject objectData;           ///< Serialized object for restore
    QString objectId;                 ///< Object ID for lookup
    QPointF objectOldPosition;        ///< For move undo
    QPointF objectNewPosition;        ///< For move redo
    int objectOldAffinity = -1;       ///< For affinity change undo
    int objectNewAffinity = -1;       ///< For affinity change redo
};
```

**Tasks:**
- [x] Add object action types to `PageUndoAction::Type` enum
- [x] Add object-specific fields to `PageUndoAction`
- [x] Add corresponding fields to `EdgelessUndoAction` (include tile coord)

**Implementation Notes:**
- Added 4 new object action types: `ObjectInsert`, `ObjectDelete`, `ObjectMove`, `ObjectAffinityChange`
- `PageUndoAction` fields: `objectData`, `objectId`, `objectOldPosition`, `objectNewPosition`, `objectOldAffinity`, `objectNewAffinity`
- `EdgelessUndoAction` adds tile coordinates: `objectTileCoord`, `objectOldTile`, `objectNewTile` for cross-tile moves

#### O2.7.2: Paged Mode Object Undo ✅
**File:** `source/core/DocumentViewport.cpp`

**Tasks:**
- [x] Implement `pushObjectInsertUndo(InsertedObject* obj)` - stores serialized object
- [x] Implement `pushObjectDeleteUndo(InsertedObject* obj)` - stores serialized object
- [x] Implement `pushObjectMoveUndo(InsertedObject* obj, QPointF oldPos)` - stores positions
- [x] In `undo()`: handle ObjectInsert by removing, ObjectDelete by recreating, ObjectMove by restoring position
- [x] In `redo()`: inverse operations

**Implementation Notes:**
- Added 3 push helper functions: `pushObjectInsertUndo()`, `pushObjectDeleteUndo()`, `pushObjectMoveUndo()`
- All helpers support both paged (per-page stack) and edgeless (global stack) modes
- Refactored `undo()`/`redo()` to distinguish stroke vs object actions
- Object actions work directly with Page, not VectorLayer
- For ImageObject undo/redo, loads image from assets via `loadImage()`
- ObjectAffinityChange also implemented (uses `page->updateObjectAffinity()`)

#### O2.7.3: Edgeless Mode Object Undo ✅
**File:** `source/core/DocumentViewport.cpp`

**Tasks:**
- [x] Same as paged mode but use `EdgelessUndoAction` with tile coordinate
- [x] Store `tileCoord` so object can be added back to correct tile on undo
- [x] Handle cross-tile moves (object moved to different tile)

**Implementation Notes:**
- Extended `undoEdgeless()` and `redoEdgeless()` with object action handling
- Uses `objectTileCoord` for same-tile operations (insert/delete/affinity)
- Cross-tile moves use `objectOldTile`/`objectNewTile` with `extractObject()` for transfer
- Properly handles tile creation/removal (`getOrCreateTile`, `removeTileIfEmpty`)
- For ImageObject, loads image from assets via `loadImage()` on restore

---

### O2.8: zOrder Keyboard Shortcuts

**Goal:** Implement Ctrl+[ and Ctrl+] for z-order changes.

#### O2.8.1: Implement zOrder Methods ✅
**File:** `source/core/DocumentViewport.cpp`

**Tasks:**
- [x] `bringSelectedToFront()` - set zOrder = max + 1 in affinity group
- [x] `sendSelectedToBack()` - set zOrder = min - 1 in affinity group
- [x] `bringSelectedForward()` - swap with next higher zOrder
- [x] `sendSelectedBackward()` - swap with next lower zOrder

**Implementation Notes:**
- All methods work for both paged and edgeless modes
- zOrder changes are scoped to objects with the same layerAffinity
- `bringToFront`/`sendToBack`: set to max+1 or min-1 respectively
- `bringForward`/`sendBackward`: swap zOrder with adjacent object
- Calls `page->rebuildAffinityMap()` after changes to update rendering order
- Marks page/tile dirty after modification

#### O2.8.2: Wire Up Shortcuts ✅
**File:** `source/core/DocumentViewport.cpp`

**Tasks:**
- [x] In `keyPressEvent()`:
  - Ctrl+] → `bringSelectedForward()`
  - Ctrl+[ → `sendSelectedBackward()`
  - Ctrl+Shift+] → `bringSelectedToFront()`
  - Ctrl+Shift+[ → `sendSelectedToBack()`

**Implementation Notes:**
- Added in ObjectSelect tool section of keyPressEvent()
- Only active when `hasSelectedObjects()` returns true
- Uses `Qt::Key_BracketRight` (]) and `Qt::Key_BracketLeft` ([)

---

### O2.9: Temporary MainWindow Connection

**Goal:** Connect existing button to object tool.

**Context:** The `insertPictureButton` already exists in MainWindow but is currently stubbed.
It has a click handler that does nothing and `updatePictureButtonState()` that sets `isEnabled = false`.

#### O2.9.1: Update insertPictureButton ✅
**File:** `source/MainWindow.cpp`

**Tasks:**
- [x] Change click handler to toggle ObjectSelect tool
- [x] Update tooltip: "Object Select Tool (O)"
- [x] Update `updatePictureButtonState()` to track if ObjectSelect tool is active
- [x] Add keyboard shortcut 'O' for ObjectSelect tool

**Implementation Notes:**
- Click handler now calls `vp->setCurrentTool(ToolType::ObjectSelect)`
- Tooltip changed to "Object Select Tool (O)"
- `updateToolButtonStates()` now resets/sets insertPictureButton like other tool buttons
- `updatePictureButtonState()` checks `vp->currentTool() == ToolType::ObjectSelect`
- Added `Qt::Key_O` case in `DocumentViewport::keyPressEvent()` for keyboard shortcut

#### O2.9.2: Add Ctrl+V Handling in MainWindow ✅
**File:** `source/MainWindow.cpp`

**Tasks:**
- [x] Ensure Ctrl+V reaches DocumentViewport (check if already works)
- [x] DocumentViewport's keyPressEvent checks clipboard for image
- [x] If image present: call `insertImageFromClipboard()`
- [x] If no image: fall back to lasso paste (existing behavior)

**Verification Notes:**
- Ctrl+V already reaches DocumentViewport - MainWindow has no paste shortcut intercepting it
- Tool-aware paste implemented in O2.4:
  - **Lasso tool**: `pasteSelection()` (pastes strokes from lasso clipboard)
  - **ObjectSelect tool**: `pasteForObjectSelect()` which:
    1. Checks system clipboard for image → `insertImageFromClipboard()`
    2. Checks internal object clipboard → `pasteObjects()`
- This tool-aware design is superior to a global "if image, insert" approach
- No additional changes needed - functionality complete from O2.4

---

### O2.BF: Bug Fixes During Testing

#### O2.BF.1: File URL Paste Support (Windows File Explorer) ✅

**Problem:** When copying a PNG file from Windows File Explorer, the clipboard contains file URLs (`hasUrls = true`), not raw image data (`hasImage = false`). Our original code only checked `hasImage()`.

**Solution:** Updated `pasteForObjectSelect()` to also check `hasUrls()`:

```cpp
// Priority 1: System clipboard has raw image data
if (mimeData->hasImage()) { ... }

// Priority 2: File URLs (e.g., copied from File Explorer)
if (mimeData->hasUrls()) {
    for (const QUrl& url : mimeData->urls()) {
        if (url.isLocalFile()) {
            QString filePath = url.toLocalFile();
            // Check if it's an image file (.png, .jpg, .jpeg, .bmp, .gif, .webp)
            insertImageFromFile(filePath);
            return;
        }
    }
}
```

**Files Changed:**
- `source/core/DocumentViewport.h`: Added `insertImageFromFile(const QString& filePath)` declaration
- `source/core/DocumentViewport.cpp`: Updated `pasteForObjectSelect()`, added `insertImageFromFile()` implementation

---

#### O2.BF.2: Save Unsaved Images Before Document Save ✅

**Problem:** When pasting an image into a NEW unsaved document, `m_document->bundlePath()` is empty, so `saveToAssets()` was skipped. The image existed in memory (`cachedPixmap`) but `imagePath` stayed empty, resulting in images not being persisted.

**Solution:** Added `Document::saveUnsavedImages()` called at the start of `saveBundle()`:

```cpp
int Document::saveUnsavedImages(const QString& bundlePath)
{
    // Iterate all pages/tiles
    // For each ImageObject with empty imagePath but valid isLoaded():
    //   Call saveToAssets(bundlePath)
}

bool Document::saveBundle(const QString& path)
{
    // Create assets/images directory
    QDir().mkpath(path + "/assets/images");
    
    // Phase O2: Save unsaved images BEFORE saving page JSON
    saveUnsavedImages(path);
    
    // ... rest of save logic ...
}
```

**Files Changed:**
- `source/core/Document.h`: Added `saveUnsavedImages()` declaration
- `source/core/Document.cpp`: Implemented `saveUnsavedImages()`, called it in `saveBundle()`

---

#### O2.BF.3: Load Images After Loading Pages/Tiles ✅

**Problem:** After `Page::fromJson()`, `loadImages()` was never called. `ImageObject::loadFromJson()` only sets `imagePath` but doesn't load the actual pixmap. The image files existed in `assets/images/` but weren't loaded into memory.

**Solution:** Added `page->loadImages(m_bundlePath)` calls after every `Page::fromJson()`:

1. **`loadPageFromDisk()`** (lazy paged mode)
2. **`loadPagesFromJson()`** (legacy paged mode)
3. **`loadTileFromDisk()`** (edgeless mode - manifest reconstruction)
4. **`loadTileFromDisk()`** (edgeless mode - legacy format)

```cpp
auto page = Page::fromJson(jsonDoc.object());
// ... error handling ...

// Phase O2: Load image objects from assets folder
int imagesLoaded = page->loadImages(m_bundlePath);

// ... rest of loading logic ...
```

**Files Changed:**
- `source/core/Document.cpp`: Added `loadImages()` calls in 4 locations
- `source/core/Page.cpp`: Added debug output to `loadImages()`

---

#### O2.BF.4: Object Rendered at 2× Distance in Edgeless Mode ✅

**Problem:** In edgeless mode, objects appeared at approximately twice the distance from the origin as expected, and the selection box was misaligned with the actual image. Moving the selection box caused the image to move "more" than expected.

**Symptoms:**
- Object appears much further from origin than expected
- Selection box appears at different position than the actual image
- Dragging feels like image moves 2× the mouse movement
- Issue only occurs in edgeless mode (paged mode works correctly)

**Root Cause:** The object's `position` was being applied **twice**:

1. In `renderEdgelessObjectsWithAffinity()`:
```cpp
painter.translate(docPos);  // docPos = tileOrigin + obj->position
obj->render(painter, 1.0);
```

2. In `ImageObject::render()`:
```cpp
QRectF targetRect(
    position.x() * zoom,  // obj->position applied AGAIN!
    position.y() * zoom,
    ...
);
```

Result: Object rendered at `tileOrigin + obj->position + obj->position` = `tileOrigin + 2×position`

**Solution:** In edgeless rendering, only translate to tile origin, not to the full document position:
```cpp
// FIXED: Only translate to tile origin
// render() will internally add obj->position
painter.save();
painter.translate(tileOrigin);  // NOT tileOrigin + obj->position
obj->render(painter, 1.0);
painter.restore();
```

Also fixed selection box to correctly add tile origin for coordinate conversion.

**Files Changed:**
- `source/core/DocumentViewport.cpp`: 
  - Fixed `renderEdgelessObjectsWithAffinity()` to only translate to tileOrigin
  - Fixed `renderObjectSelection()` to add tile origin when converting coords

---

#### O2.BF.5: Objects Not Serialized in Edgeless Tile Save ✅

**Problem:** When saving an edgeless canvas, inserted objects were not being saved to the tile JSON files. Objects existed in memory but were lost on save/reload.

**Symptoms:**
- Insert image in edgeless mode works
- Save document successfully
- Reload document - image is gone
- No error messages

**Root Cause:** In `Document::saveTile()`, the edgeless mode serialization only saved:
- `layers` array (layer ID + strokes)
- `coord_x` and `coord_y`

It did NOT save the `objects` array, even though `loadTileFromDisk()` was correctly looking for and loading objects.

**Before (broken):**
```cpp
if (isEdgeless()) {
    // ... layers serialization ...
    tileObj["layers"] = layersArray;
    tileObj["coord_x"] = coord.first;
    tileObj["coord_y"] = coord.second;
}
```

**After (fixed):**
```cpp
if (isEdgeless()) {
    // ... layers serialization ...
    tileObj["layers"] = layersArray;
    
    // Phase O2: Save objects to tile (BF.5)
    if (!tile->objects.empty()) {
        QJsonArray objectsArray;
        for (const auto& obj : tile->objects) {
            objectsArray.append(obj->toJson());
        }
        tileObj["objects"] = objectsArray;
    }
    
    tileObj["coord_x"] = coord.first;
    tileObj["coord_y"] = coord.second;
}
```

**Files Changed:**
- `source/core/Document.cpp`: Added object serialization to `saveTile()` for edgeless mode

---

#### O2.BF.6: Object Insert Undo Not Working ✅

**Problem:** After pasting an image to the canvas, pressing Ctrl+Z did not undo the insertion. The image remained on screen despite multiple undo attempts.

**Symptoms:**
- Paste image successfully with Ctrl+V
- Press Ctrl+Z - nothing happens
- Image remains on canvas
- Debug output shows `undo(): Called` but no actual undo action

**Root Cause:** Both `insertImageFromClipboard()` and `insertImageFromFile()` were **not calling** `pushObjectInsertUndo()`. The call was left as a TODO comment:
```cpp
// 7. Create undo entry
// TODO O2.7: pushObjectInsertUndo(rawPtr);
```

The undo stack never received the insert action, so there was nothing to undo.

**Fix:**
1. Track the tile coordinate where the object is inserted (for edgeless mode)
2. Call `pushObjectInsertUndo(rawPtr, m_currentPageIndex, insertedTileCoord)`
3. Added `deselectObjectById()` helper to safely remove objects from selection before deleting
4. Updated both `undoEdgeless()` and paged `undo()` to deselect the object before removing it

**After (fixed) - insertImageFromFile():**
```cpp
// Track tile coord for undo (edgeless mode)
Document::TileCoord insertedTileCoord = {0, 0};

// 4. Add to appropriate page/tile
if (m_document->isEdgeless()) {
    auto coord = m_document->tileCoordForPoint(imgObj->position);
    // ... add to tile ...
    insertedTileCoord = coord;  // Save for undo
} else {
    // ... add to page ...
}

// ... other steps ...

// 7. Create undo entry (BF.6)
pushObjectInsertUndo(rawPtr, m_currentPageIndex, insertedTileCoord);
```

**After (fixed) - undoEdgeless() ObjectInsert case:**
```cpp
case PageUndoAction::ObjectInsert:
    // Undo insert = remove the object (BF.6)
    deselectObjectById(action.objectId);  // Prevent dangling pointer
    Page* tile = m_document->getTile(...);
    if (tile) {
        tile->removeObject(action.objectId);
        // ...
    }
    break;
```

**Files Changed:**
- `source/core/DocumentViewport.cpp`: 
  - Fixed `insertImageFromClipboard()` to call `pushObjectInsertUndo()`
  - Fixed `insertImageFromFile()` to call `pushObjectInsertUndo()`
  - Added `deselectObjectById()` helper function
  - Updated `undoEdgeless()` ObjectInsert case to deselect before removing
  - Updated paged `undo()` ObjectInsert case to deselect before removing
- `source/core/DocumentViewport.h`: Added `deselectObjectById()` declaration

---

#### O2.BF.7: Object Redo Not Working for Unsaved Documents ✅

**Problem:** After undoing an image insertion with Ctrl+Z, pressing Ctrl+Y (redo) did not restore the image. This was specific to unsaved documents where images only existed in memory.

**Symptoms:**
- Paste image successfully with Ctrl+V
- Press Ctrl+Z - image disappears (undo works)
- Press Ctrl+Y - image does NOT reappear (redo fails)
- Only affects unsaved documents (no `imagePath` set)

**Root Cause:** The undo/redo system serializes objects to JSON via `toJson()` and restores them via `fromJson()`. However, `ImageObject::toJson()` only stored `imagePath` and `imageHash`, NOT the actual pixel data. For unsaved documents:
1. `imagePath` is empty (not saved to disk yet)
2. `cachedPixmap` exists in memory
3. `toJson()` stores `imagePath = ""`
4. On redo, `loadFromJson()` sets `imagePath = ""`, no pixmap loaded
5. `loadImage()` fails because `imagePath.isEmpty()` returns `true`
6. Object is added but has no image data - invisible/broken

**Memory Safety Concern:** The fix must not cause memory leaks or dangling pointers. By embedding image data in JSON (owned by the action struct), the data lifecycle is tied to the undo/redo stack, which is properly managed.

**Fix:** Embed image data as base64 in JSON when `imagePath` is empty:

**ImageObject::toJson() - After:**
```cpp
QJsonObject ImageObject::toJson() const
{
    QJsonObject obj = InsertedObject::toJson();
    
    obj["imagePath"] = imagePath;
    obj["imageHash"] = imageHash;
    // ... other properties ...
    
    // BF.7: If imagePath is empty but we have a cached pixmap (unsaved document),
    // embed the image data as base64 so undo/redo works correctly
    if (imagePath.isEmpty() && !cachedPixmap.isNull()) {
        QByteArray imageData;
        QBuffer buffer(&imageData);
        buffer.open(QIODevice::WriteOnly);
        cachedPixmap.save(&buffer, "PNG");
        obj["embeddedImageData"] = QString::fromLatin1(imageData.toBase64());
    }
    
    return obj;
}
```

**ImageObject::loadFromJson() - After:**
```cpp
void ImageObject::loadFromJson(const QJsonObject& obj)
{
    InsertedObject::loadFromJson(obj);
    
    imagePath = obj["imagePath"].toString();
    // ... other properties ...
    
    // BF.7: Check for embedded image data (unsaved document case)
    if (obj.contains("embeddedImageData")) {
        QString base64Data = obj["embeddedImageData"].toString();
        QByteArray imageData = QByteArray::fromBase64(base64Data.toLatin1());
        QPixmap pixmap;
        if (pixmap.loadFromData(imageData, "PNG")) {
            cachedPixmap = pixmap;
            if (size.isEmpty() && !cachedPixmap.isNull()) {
                size = cachedPixmap.size();
            }
        }
    }
}
```

**Design Notes:**
- Embedding only occurs when `imagePath` is empty (unsaved case)
- Once document is saved, `imagePath` is set and no embedding needed
- Embedded data is temporary - only needed for undo/redo during session
- `loadImage()` safely ignores empty `imagePath` without clearing `cachedPixmap`

**Files Changed:**
- `source/objects/ImageObject.cpp`: 
  - Updated `toJson()` to embed image data when `imagePath` is empty
  - Updated `loadFromJson()` to decode embedded image data

---

#### O2.BF.8: Object Move Undo Not Working ✅

**Problem:** After inserting images and moving them, the undo/redo sequence did not restore the images to their moved positions. Instead, redo would restore images to their original insertion positions, ignoring all subsequent moves.

**Symptoms:**
- Insert image at position A
- Move image to position B
- Save document (position B saved correctly)
- Undo multiple times
- Redo → image appears at position A (original insert), NOT position B (moved)
- After save/reload → images at correct position B

**Root Cause:** The `pushObjectMoveUndo()` call was **commented out as a TODO** in `handlePointerRelease_ObjectSelect()`:
```cpp
// TODO O2.7: Create undo entry for move using m_objectOriginalPositions
// pushObjectMoveUndo(m_objectOriginalPositions);
```

When objects were moved, no undo entry was created. The undo stack only had the original `ObjectInsert` actions with the insertion positions. On redo, the insert action restored the object at its insertion position, not the moved position.

**Fix:** Implemented the object move undo in `handlePointerRelease_ObjectSelect()`:

```cpp
// O2.7/BF.8: Create undo entry for each moved object
for (InsertedObject* obj : m_selectedObjects) {
    if (!obj) continue;
    
    auto it = m_objectOriginalPositions.find(obj->id);
    if (it == m_objectOriginalPositions.end()) continue;
    
    QPointF oldPos = it.value();
    
    // Only create undo if position actually changed
    if (oldPos != obj->position) {
        // For edgeless mode, track tile coordinates
        Document::TileCoord oldTile = {0, 0};
        Document::TileCoord newTile = {0, 0};
        
        if (m_document->isEdgeless()) {
            // Find current tile containing object
            for (const auto& coord : m_document->allLoadedTileCoords()) {
                Page* tile = m_document->getTile(coord.first, coord.second);
                if (tile && tile->objectById(obj->id)) {
                    newTile = coord;
                    break;
                }
            }
            oldTile = newTile;  // Cross-tile tracking deferred
        }
        
        pushObjectMoveUndo(obj, oldPos, m_currentPageIndex, oldTile, newTile);
    }
}
```

**Key Implementation Details:**
- `m_objectOriginalPositions` is a `QMap<QString, QPointF>` keyed by object ID
- Original positions are stored when drag starts in `handlePointerPress_ObjectSelect()`
- Only creates undo entry if position actually changed (avoids spurious entries from clicks)
- For edgeless mode, tile coordinates tracked for cross-tile moves
- For paged mode, tile coordinates not used (set to {0,0})

**Undo/Redo Sequence Now Works Correctly:**
1. Insert image at A → pushes `ObjectInsert` (position A)
2. Move to B → pushes `ObjectMove` (oldPos=A, newPos=B)
3. Ctrl+Z (undo move) → restores position A
4. Ctrl+Z (undo insert) → removes image
5. Ctrl+Y (redo insert) → adds image at position A
6. Ctrl+Y (redo move) → moves image to position B ✓

**Files Changed:**
- `source/core/DocumentViewport.cpp`:
  - Implemented object move undo in `handlePointerRelease_ObjectSelect()`
  - Added debug output to `pushObjectMoveUndo()` for testing
- `source/objects/InsertedObject.cpp`:
  - Added debug output to `loadFromJson()` for testing

**Debug Output (kept for future testing):**
```
pushObjectMoveUndo: obj "xxx" oldPos = QPointF(259.5,-17) newPos = QPointF(24,32)
InsertedObject::loadFromJson: loaded position = QPointF(24,32) size = QSizeF(297, 550) ...
```

---

#### O2.BF.9: zOrder Shortcuts Not Working (Keyboard Layout Issue) ✅

**Problem:** Pressing Ctrl+Shift+] to bring objects to front did nothing. The zOrder methods were implemented correctly but never called. Debug output showed the key press was detected but the shortcut matching failed.

**Symptoms:**
- Select object, press Ctrl+Shift+] - nothing happens
- `bringSelectedToFront()` never called
- Debug shows: `key = 125 ctrl = true shift = true BracketRight = Qt::Key_BracketRight`
- Key code 125 doesn't match `Qt::Key_BracketRight` (which is 93)

**Root Cause:** Qt reports the **character produced**, not the physical key pressed. On a US keyboard:
- `]` (bracket right) has key code 93 (`Qt::Key_BracketRight`)
- `}` (brace right) has key code 125 (`Qt::Key_BraceRight`)

When pressing `Ctrl+Shift+]`:
1. Physical key is `]`
2. Shift modifier changes the character to `}`
3. Qt reports key = 125 (`Qt::Key_BraceRight`), NOT key = 93
4. Our code checked for `Qt::Key_BracketRight` (93) → no match

Same issue for `[` (91) vs `{` (123).

**Fix:** Check for BOTH the bracket keys (without shift) AND brace keys (with shift):

```cpp
// Note: On most keyboards, Shift+[ = { and Shift+] = }
// Qt reports the character produced, not the physical key

// Ctrl+] = bring forward, Ctrl+Shift+] (which produces }) = bring to front
if (ctrl && (event->key() == Qt::Key_BracketRight || event->key() == Qt::Key_BraceRight)) {
    if (shift || event->key() == Qt::Key_BraceRight) {
        bringSelectedToFront();   // Ctrl+Shift+] or Ctrl+}
    } else {
        bringSelectedForward();   // Ctrl+]
    }
    event->accept();
    return;
}

// Ctrl+[ = send backward, Ctrl+Shift+[ (which produces {) = send to back  
if (ctrl && (event->key() == Qt::Key_BracketLeft || event->key() == Qt::Key_BraceLeft)) {
    if (shift || event->key() == Qt::Key_BraceLeft) {
        sendSelectedToBack();     // Ctrl+Shift+[ or Ctrl+{
    } else {
        sendSelectedBackward();   // Ctrl+[
    }
    event->accept();
    return;
}
```

**Key Code Reference:**
| Physical Key | Key Code | Qt Constant |
|--------------|----------|-------------|
| `[` | 91 | `Qt::Key_BracketLeft` |
| `]` | 93 | `Qt::Key_BracketRight` |
| `{` (Shift+[) | 123 | `Qt::Key_BraceLeft` |
| `}` (Shift+]) | 125 | `Qt::Key_BraceRight` |

**Files Changed:**
- `source/core/DocumentViewport.cpp`: Updated zOrder shortcut handling to check for both bracket and brace keys

---

### O2.10: Testing & Verification

**Tasks:**
- [ ] Test: Paste image from clipboard, appears at center
- [ ] Test: Click to select object, shows selection box
- [ ] Test: Shift+click adds to selection
- [ ] Test: Drag selected objects, moves them
- [ ] Test: Delete key removes selected objects
- [ ] Test: Ctrl+C then Ctrl+V duplicates object
- [ ] Test: Ctrl+Z undoes insert (image disappears)
- [ ] Test: Ctrl+Y redoes insert (image reappears) - **unsaved document**
- [ ] Test: Ctrl+Y redoes insert (image reappears) - **saved document**
- [ ] Test: Ctrl+Z/Y for object move (insert → move → undo all → redo all)
- [ ] Test: zOrder shortcuts work correctly
- [ ] Test: zOrder undo/redo (future)
- [ ] Test: Object affinity change undo/redo (future)
- [ ] Test: Object resize undo/redo (future, when resize implemented)
- [ ] Test: Save and reload - inserted images persist (assets folder)
- [ ] Test: Edgeless - object crossing tile boundary (drag to new tile)
- [ ] Test: Paged lazy loading - objects on evicted pages persist

---

## Phase O2.C: Code Cleanup - Abstraction Enforcement ✅

**Goal:** Ensure DocumentViewport works with the abstract `InsertedObject` interface, not concrete types like `ImageObject`. This prepares the system for future object types (TextBox, Shape, Link, etc.).

**Status:** COMPLETE

---

### O2.C.1: Current Abstraction Violations

**Analysis:** DocumentViewport currently has **5 places** that check `obj->type() == "image"` and cast to `ImageObject*`:

| Location | Purpose | Current Code |
|----------|---------|--------------|
| `pasteObjects()` | Load image from assets after paste | `if (obj->type() == "image") { ImageObject* imgObj = static_cast<...>; imgObj->loadImage(...); }` |
| `undo()` (paged) ObjectDelete | Load image when restoring | Same pattern |
| `undo()` (paged) ObjectInsert redo | Load image when redoing insert | Same pattern |
| `undoEdgeless()` ObjectDelete | Load image when restoring | Same pattern |
| `redoEdgeless()` ObjectInsert | Load image when redoing insert | Same pattern |

**Additionally**, 2 places call `saveToAssets()` after insertion:
- `insertImageFromClipboard()` - calls `imgObj->saveToAssets()`
- `insertImageFromFile()` - calls `imgObj->saveToAssets()`

**Problem:** When we add `TextBoxObject`, `ShapeObject`, or `LinkObject`, DocumentViewport would need type-specific handling for each. This violates the Open-Closed Principle.

---

### O2.C.2: Solution - Add Virtual Asset Methods ✅

**Added to `InsertedObject` base class:**

```cpp
// ===== Virtual Asset Management Methods =====

/**
 * @brief Load external assets (e.g., images) from the bundle.
 * @param bundlePath Path to the .snb bundle directory.
 * @return True if successful or no assets to load.
 * 
 * Default implementation does nothing (for objects without external assets).
 * ImageObject overrides to load the pixmap from assets/images/.
 */
virtual bool loadAssets(const QString& bundlePath) { 
    Q_UNUSED(bundlePath);
    return true; 
}

/**
 * @brief Save external assets (e.g., images) to the bundle.
 * @param bundlePath Path to the .snb bundle directory.
 * @return True if successful or no assets to save.
 * 
 * Default implementation does nothing (for objects without external assets).
 * ImageObject overrides to save the pixmap to assets/images/.
 */
virtual bool saveAssets(const QString& bundlePath) { 
    Q_UNUSED(bundlePath);
    return true; 
}

/**
 * @brief Check if this object's assets are loaded and ready to render.
 * @return True if ready (or no assets needed).
 * 
 * Default returns true. ImageObject returns !cachedPixmap.isNull().
 */
virtual bool isLoaded() const { return true; }
```

**ImageObject overrides:**

```cpp
bool ImageObject::loadAssets(const QString& bundlePath) override {
    return loadImage(bundlePath);
}

bool ImageObject::saveAssets(const QString& bundlePath) override {
    return saveToAssets(bundlePath);
}

bool ImageObject::isLoaded() const override {
    return !cachedPixmap.isNull();
}
```

---

### O2.C.3: Refactor DocumentViewport ✅

**Before (type-specific):**
```cpp
// 5 places like this:
if (obj->type() == "image" && !m_document->bundlePath().isEmpty()) {
    ImageObject* imgObj = static_cast<ImageObject*>(obj.get());
    imgObj->loadImage(m_document->bundlePath());
}
```

**After (generic):**
```cpp
// Single line, works for ANY object type:
obj->loadAssets(m_document->bundlePath());
```

**Changes needed in DocumentViewport.cpp:**

| Location | Change |
|----------|--------|
| `pasteObjects()` | Replace type check + cast with `obj->loadAssets(bundlePath)` |
| `undo()` ObjectDelete | Replace with `obj->loadAssets(bundlePath)` |
| `undo()` ObjectInsert redo | Replace with `obj->loadAssets(bundlePath)` |
| `undoEdgeless()` ObjectDelete | Replace with `obj->loadAssets(bundlePath)` |
| `redoEdgeless()` ObjectInsert | Replace with `obj->loadAssets(bundlePath)` |
| `insertImageFromClipboard()` | Replace cast+saveToAssets with `rawPtr->saveAssets(bundlePath)` |
| `insertImageFromFile()` | Replace cast+saveToAssets with `rawPtr->saveAssets(bundlePath)` |

---

### O2.C.4: What SHOULD Remain Type-Specific (Verified ✅)

These are **correctly** type-specific and were NOT abstracted:

1. **`insertImageFromClipboard()`** - Creates `ImageObject` directly. This is the entry point for image insertion and MUST know the type.

2. **`insertImageFromFile()`** - Creates `ImageObject` directly. Same rationale.

3. **Future entry points:**
   - `insertTextBox()` - will create `TextBoxObject`
   - `insertShape()` - will create `ShapeObject`
   - `insertLink()` - will create `LinkObject`

The key principle: **Entry points know the type, everything else works with `InsertedObject*`**.

---

### O2.C.5: Implementation Summary ✅

| Task | Status | Files Changed |
|------|--------|---------------|
| Add virtual methods to InsertedObject.h | ✅ | InsertedObject.h |
| Add overrides to ImageObject | ✅ | ImageObject.h/.cpp |
| Refactor 5 type checks in DocumentViewport | ✅ | DocumentViewport.cpp |
| Refactor 2 saveAssets calls | ✅ | DocumentViewport.cpp |
| Test undo/redo/paste still work | ⏳ Pending | - |

**Changes Made:**
1. **InsertedObject.h**: Added `loadAssets()`, `saveAssets()`, `isAssetLoaded()` virtual methods with default no-op implementations
2. **ImageObject.h**: Added override declarations for the 3 virtual methods
3. **ImageObject.cpp**: Added implementations that delegate to existing `loadImage()` and `saveToAssets()`
4. **DocumentViewport.cpp**: Replaced 5 type-specific `loadImage()` calls and 2 `saveToAssets()` calls with generic `loadAssets()`/`saveAssets()` calls

---

### O2.C.6: Future Object Types Enabled ✅

After this cleanup, adding new object types requires:

1. Create new class (e.g., `TextBoxObject : public InsertedObject`)
2. Implement `render()`, `type()`, `toJson()`, `loadFromJson()`
3. If has external assets: override `loadAssets()`, `saveAssets()`, `isLoaded()`
4. Register type in `InsertedObject::fromJson()` factory
5. Add entry point method (e.g., `insertTextBox()`) to DocumentViewport

**NO changes needed to:**
- Undo/redo system
- Copy/paste system
- Selection/movement system
- zOrder system
- Rendering system

---

## Phase O3: Enhanced Features (Testing Required)

**Goal:** Implement features needed for comprehensive testing of the object system.

**Priority Items (Needed for Testing):**
- **O3.1: Object Resize** - Test scaling, aspect ratio, undo/redo
- **O3.6: Object Properties Panel** - Test/change affinity, position, size

---

### O3.1: Object Resize

**Goal:** Enable users to resize selected objects via drag handles.

**Status:** Implementation required for testing.

**Prerequisites:**
- Selection box with 8 handles already renders ✅ (from O2.1.4)
- `HandleHit` enum exists ✅ (reuse from lasso transform)
- Object undo/redo infrastructure exists ✅ (from O2.7)

---

#### O3.1.1: Handle Hit Testing ✅

**Goal:** Detect which resize handle (if any) is under the pointer.

**Status:** COMPLETE

**File:** `source/core/DocumentViewport.cpp`

```cpp
// Reuse existing HandleHit enum from lasso transform
HandleHit DocumentViewport::objectHandleAtPoint(const QPointF& viewportPos) const
{
    if (m_selectedObjects.size() != 1) return HandleHit::None;
    
    InsertedObject* obj = m_selectedObjects.first();
    if (!obj) return HandleHit::None;
    
    // Convert object bounds to viewport coordinates
    QRectF objRect = objectBoundsInViewport(obj);
    
    // Calculate handle positions (same as in renderObjectSelection)
    QPointF handles[8] = {
        objRect.topLeft(),                                    // TopLeft
        QPointF(objRect.center().x(), objRect.top()),         // Top
        objRect.topRight(),                                   // TopRight
        QPointF(objRect.left(), objRect.center().y()),        // Left
        QPointF(objRect.right(), objRect.center().y()),       // Right
        objRect.bottomLeft(),                                 // BottomLeft
        QPointF(objRect.center().x(), objRect.bottom()),      // Bottom
        objRect.bottomRight()                                 // BottomRight
    };
    
    // Rotation handle position
    QPointF rotatePos(objRect.center().x(), objRect.top() - ROTATE_HANDLE_OFFSET);
    
    // Check rotation handle first (has priority)
    if (QLineF(viewportPos, rotatePos).length() <= HANDLE_VISUAL_SIZE) {
        return HandleHit::Rotate;
    }
    
    // Check resize handles
    static const HandleHit handleTypes[8] = {
        HandleHit::TopLeft, HandleHit::Top, HandleHit::TopRight,
        HandleHit::Left, HandleHit::Right,
        HandleHit::BottomLeft, HandleHit::Bottom, HandleHit::BottomRight
    };
    
    for (int i = 0; i < 8; ++i) {
        if (QLineF(viewportPos, handles[i]).length() <= HANDLE_VISUAL_SIZE) {
            return handleTypes[i];
        }
    }
    
    // Check if inside bounding box (for move - but this is handled by object drag)
    return HandleHit::None;
}
```

**Tasks:**
- [x] Add `objectHandleAtPoint(QPointF)` method declaration to header
- [x] Add helper `objectBoundsInViewport(InsertedObject*)` - converts object rect to viewport coords
- [x] Implement handle hit testing as shown above

**Implementation Notes:**
- `objectBoundsInViewport()` searches for the object in tiles/pages to get correct document position
- `objectHandleAtPoint()` uses `HANDLE_HIT_SIZE` (20px) for touch-friendly hit testing
- Reuses existing `HandleHit` enum from lasso transform
- Rotation handle checked first (has priority when overlapping)

---

#### O3.1.2: Resize Drag State ✅

**Goal:** Track resize operation in progress.

**Status:** COMPLETE

**File:** `source/core/DocumentViewport.h`

```cpp
// Add to Object Selection section (after m_objectOriginalPositions)

// ===== Object Resize State (Phase O3.1) =====
bool m_isResizingObject = false;          ///< Currently dragging a resize handle
HandleHit m_objectResizeHandle = HandleHit::None;  ///< Which handle is being dragged
QPointF m_resizeStartViewport;            ///< Viewport pos where resize started
QSizeF m_resizeOriginalSize;              ///< Object size before resize
QPointF m_resizeOriginalPosition;         ///< Object position before resize (for anchoring)
```

**Tasks:**
- [x] Add state variables to `DocumentViewport.h`

**Implementation Notes:**
- Added 5 state variables after `m_objectClipboard` section
- Each variable has documentation explaining its purpose
- `m_objectResizeHandle` uses existing `HandleHit` enum
- `m_resizeOriginalPosition` needed because corner handles change both position and size

---

#### O3.1.3: Handle Resize in Pointer Events ✅

**Goal:** Start/update/finish resize when dragging handles.

**Status:** COMPLETE

**File:** `source/core/DocumentViewport.cpp`

**In `handlePointerPress_ObjectSelect()`:**
```cpp
// BEFORE checking if clicking on object to start drag:
if (m_selectedObjects.size() == 1) {
    HandleHit handle = objectHandleAtPoint(event->pos());
    if (handle != HandleHit::None && handle != HandleHit::Inside) {
        // Start resize operation
        m_isResizingObject = true;
        m_objectResizeHandle = handle;
        m_resizeStartViewport = event->pos();
        m_resizeOriginalSize = m_selectedObjects.first()->size;
        m_resizeOriginalPosition = m_selectedObjects.first()->position;
        return;  // Don't start object drag
    }
}
```

**In `handlePointerMove_ObjectSelect()`:**
```cpp
if (m_isResizingObject) {
    // Calculate new size based on handle being dragged
    updateObjectResize(event->pos());
    update();
    return;
}
```

**In `handlePointerRelease_ObjectSelect()`:**
```cpp
if (m_isResizingObject) {
    // Finalize resize
    InsertedObject* obj = m_selectedObjects.first();
    if (obj && (obj->size != m_resizeOriginalSize || obj->position != m_resizeOriginalPosition)) {
        // Create undo entry for resize
        pushObjectResizeUndo(obj, m_resizeOriginalPosition, m_resizeOriginalSize);
        
        // Mark dirty
        if (m_document->isEdgeless()) {
            // May need to relocate to different tile if position changed
            relocateObjectsToCorrectTiles();
        } else {
            m_document->markPageDirty(m_currentPageIndex);
        }
    }
    
    m_isResizingObject = false;
    m_objectResizeHandle = HandleHit::None;
    emit documentModified();
    return;
}
```

**Tasks:**
- [x] Modify `handlePointerPress_ObjectSelect()` to detect handle clicks
- [x] Modify `handlePointerMove_ObjectSelect()` to call `updateObjectResize()`
- [x] Modify `handlePointerRelease_ObjectSelect()` to finalize resize

**Implementation Notes:**
- Resize check happens FIRST in press handler (before object drag check)
- Added `m_pointerActive = true` when starting resize for consistency
- Release handler checks if size/position actually changed before marking dirty
- Undo entry creation is a TODO placeholder (O3.1.5)
- `updateObjectResize()` declaration added as stub, full logic in O3.1.4 ✅

---

#### O3.1.4: Implement updateObjectResize() ✅

**Goal:** Calculate and apply new object size based on drag.

**Status:** COMPLETE

**File:** `source/core/DocumentViewport.cpp`

```cpp
void DocumentViewport::updateObjectResize(const QPointF& currentViewport)
{
    if (m_selectedObjects.size() != 1) return;
    InsertedObject* obj = m_selectedObjects.first();
    if (!obj) return;
    
    // Convert positions to document coordinates
    QPointF startDoc = viewportToDocument(m_resizeStartViewport);
    QPointF currentDoc = viewportToDocument(currentViewport);
    QPointF delta = currentDoc - startDoc;
    
    // Original bounds
    QRectF originalRect(m_resizeOriginalPosition, m_resizeOriginalSize);
    QRectF newRect = originalRect;
    
    // Apply delta based on which handle is being dragged
    switch (m_objectResizeHandle) {
        case HandleHit::TopLeft:
            newRect.setTopLeft(originalRect.topLeft() + delta);
            break;
        case HandleHit::Top:
            newRect.setTop(originalRect.top() + delta.y());
            break;
        case HandleHit::TopRight:
            newRect.setTopRight(originalRect.topRight() + delta);
            break;
        case HandleHit::Left:
            newRect.setLeft(originalRect.left() + delta.x());
            break;
        case HandleHit::Right:
            newRect.setRight(originalRect.right() + delta.x());
            break;
        case HandleHit::BottomLeft:
            newRect.setBottomLeft(originalRect.bottomLeft() + delta);
            break;
        case HandleHit::Bottom:
            newRect.setBottom(originalRect.bottom() + delta.y());
            break;
        case HandleHit::BottomRight:
            newRect.setBottomRight(originalRect.bottomRight() + delta);
            break;
        case HandleHit::Rotate:
            // Rotation not implemented yet
            return;
        default:
            return;
    }
    
    // Normalize rect (handle inverted dimensions)
    newRect = newRect.normalized();
    
    // Enforce minimum size
    const qreal MIN_SIZE = 10.0;
    if (newRect.width() < MIN_SIZE) newRect.setWidth(MIN_SIZE);
    if (newRect.height() < MIN_SIZE) newRect.setHeight(MIN_SIZE);
    
    // TODO: Aspect ratio lock (Shift key or ImageObject::maintainAspectRatio)
    
    // Apply to object
    obj->position = newRect.topLeft();
    obj->size = newRect.size();
}
```

**Tasks:**
- [x] Add `updateObjectResize(QPointF)` declaration to header (done in O3.1.3)
- [x] Implement the method as shown
- [ ] Add aspect ratio lock support (check Shift key or `maintainAspectRatio`) - deferred

**Implementation Notes:**
- Declaration was added in O3.1.3 with stub, now replaced with full implementation
- Uses `QRectF::normalized()` to handle cases where user drags past opposite edge
- Minimum size of 10.0 document units enforced
- Aspect ratio lock is marked as TODO for future enhancement

---

#### O3.1.5: Resize Undo/Redo ✅

**Goal:** Make resize operations undoable.

**Status:** COMPLETE

**File:** `source/core/DocumentViewport.h`

```cpp
// Added to PageUndoAction::Type enum:
ObjectResize        ///< An object was resized (undo = restore old pos+size)

// Added to PageUndoAction:
QSizeF objectOldSize;               ///< For ObjectResize: size before resize
QSizeF objectNewSize;               ///< For ObjectResize: size after resize

// Added to EdgelessUndoAction:
QSizeF objectOldSize;                 ///< For ObjectResize: size before resize
QSizeF objectNewSize;                 ///< For ObjectResize: size after resize
```

**File:** `source/core/DocumentViewport.cpp`

```cpp
void DocumentViewport::pushObjectResizeUndo(InsertedObject* obj, 
                                            const QPointF& oldPos, 
                                            const QSizeF& oldSize)
{
    if (!obj) return;
    
    if (m_document->isEdgeless()) {
        EdgelessUndoAction action;
        action.type = PageUndoAction::ObjectResize;
        action.objectId = obj->id;
        action.objectData = obj->toJson();
        action.objectOldPosition = oldPos;
        action.objectNewPosition = obj->position;
        action.objectOldSize = oldSize;
        action.objectNewSize = obj->size;
        // Find tile
        for (const auto& coord : m_document->allLoadedTileCoords()) {
            Page* tile = m_document->getTile(coord.first, coord.second);
            if (tile && tile->objectById(obj->id)) {
                action.objectTileCoord = coord;
                break;
            }
        }
        m_edgelessUndoStack.push(action);
        m_edgelessRedoStack.clear();
    } else {
        PageUndoAction action;
        action.type = PageUndoAction::ObjectResize;
        action.pageIndex = m_currentPageIndex;
        action.objectId = obj->id;
        action.objectData = obj->toJson();
        action.objectOldPosition = oldPos;
        action.objectNewPosition = obj->position;
        action.objectOldSize = oldSize;
        action.objectNewSize = obj->size;
        m_undoStacks[m_currentPageIndex].push(action);
        m_redoStacks[m_currentPageIndex].clear();
    }
}
```

**Tasks:**
- [x] Add `ObjectResize` to `PageUndoAction::Type` enum
- [x] Add `objectOldSize` and `objectNewSize` to both undo structs
- [x] Implement `pushObjectResizeUndo()` helper
- [x] Add `ObjectResize` case to `undo()` and `redo()` methods
- [x] Enable `pushObjectResizeUndo()` call in `handlePointerRelease_ObjectSelect()`

**Implementation Notes:**
- Added clear comment blocks to each `ObjectResize` case in undo/redo for maintainability
- Both paged and edgeless modes are supported with separate code paths
- Resize can change both position (dragging corners) and size, so both are stored
- Debug output included for troubleshooting (`qDebug()` statements)
- Full object snapshot (`objectData`) stored for safety

---

#### O3.1.6: Cursor Feedback

**Goal:** Show appropriate resize cursor when hovering over handles.

**File:** `source/core/DocumentViewport.cpp`

```cpp
// In handlePointerMove_ObjectSelect() when not dragging:
if (m_selectedObjects.size() == 1 && !m_isDraggingObjects && !m_isResizingObject) {
    HandleHit handle = objectHandleAtPoint(event->pos());
    switch (handle) {
        case HandleHit::TopLeft:
        case HandleHit::BottomRight:
            setCursor(Qt::SizeFDiagCursor);
            break;
        case HandleHit::TopRight:
        case HandleHit::BottomLeft:
            setCursor(Qt::SizeBDiagCursor);
            break;
        case HandleHit::Top:
        case HandleHit::Bottom:
            setCursor(Qt::SizeVerCursor);
            break;
        case HandleHit::Left:
        case HandleHit::Right:
            setCursor(Qt::SizeHorCursor);
            break;
        case HandleHit::Rotate:
            // TODO: custom rotation cursor
            setCursor(Qt::CrossCursor);
            break;
        default:
            setCursor(Qt::ArrowCursor);
            break;
    }
}
```

**Tasks:**
- [ ] Add cursor updates based on hovered handle
- [ ] Reset cursor when leaving handle area

---

#### O3.1.7: Testing Checklist (Resize)

- [ ] Resize from each of 8 handles works
- [ ] Resize undo/redo works
- [ ] Resize in edgeless mode works (including tile relocation)
- [ ] Resize in paged mode works
- [ ] Minimum size enforced
- [ ] Cursor changes on hover
- [ ] Aspect ratio lock works (optional)

---

### O3.1.8: Object Rotation

**Goal:** Enable rotation of objects using the rotation handle.

**Status:** PLANNED

**Estimated Time:** 2-3 hours

#### Existing Infrastructure (Already Implemented)

| Component | Location | Status |
|-----------|----------|--------|
| `rotation` property | `InsertedObject.h` | ✅ Exists |
| Serialization | `InsertedObject.cpp` | ✅ `toJson()`/`loadFromJson()` |
| Rendering | `ImageObject::render()` | ✅ Uses `painter.rotate(rotation)` |
| Handle detection | `objectHandleAtPoint()` | ✅ Returns `HandleHit::Rotate` |
| Handle drawing | `renderObjectSelection()` | ✅ Draws circle above object |

#### O3.1.8.1: Rotation Drag Logic ✅

**Status:** COMPLETE

**File:** `source/core/DocumentViewport.cpp`

**In `updateObjectResize()`, replaced the `HandleHit::Rotate` case:**
```cpp
case HandleHit::Rotate: {
    // Calculate angle from object center to current pointer position
    QPointF objectCenter = m_resizeOriginalPosition + 
                           QPointF(m_resizeOriginalSize.width() / 2, 
                                   m_resizeOriginalSize.height() / 2);
    
    // Angle from center to current pointer (in document coords)
    // atan2 returns radians, with 0 pointing right (+X), positive going counterclockwise
    // We add 90° because the rotation handle starts above the object (at 12 o'clock)
    qreal angle = qRadiansToDegrees(
        qAtan2(currentDoc.y() - objectCenter.y(), 
               currentDoc.x() - objectCenter.x())
    ) + 90.0;
    
    // Normalize to 0-360 range
    while (angle < 0) angle += 360.0;
    while (angle >= 360) angle -= 360.0;
    
    // Snap to 15° increments by default
    // TODO O3.1.8.1: Check Shift key for free rotation (no snap)
    angle = qRound(angle / 15.0) * 15.0;
    
    obj->rotation = angle;
    return;  // Don't apply resize logic below
}
```

**Tasks:**
- [x] Implement rotation angle calculation
- [x] Add snap to 15° increments
- [ ] Optional: Free rotation when Shift held (deferred)

**Implementation Notes:**
- Uses `qAtan2` for angle calculation from object center to pointer
- Adds 90° offset because rotation handle is at 12 o'clock position
- Normalizes angle to 0-360 range
- Snaps to 15° increments (0°, 15°, 30°, 45°, etc.)
- Returns early to skip resize rect logic
- Debug output included for testing

---

#### O3.1.8.2: Rotation State Variables ✅

**Status:** COMPLETE

**File:** `source/core/DocumentViewport.h`

```cpp
// Added to private members (after m_resizeOriginalPosition):
/**
 * @brief Object rotation before resize/rotate started (Phase O3.1.8.2).
 * 
 * Stored when user starts dragging any handle, used for rotation undo.
 */
qreal m_resizeOriginalRotation = 0.0;
```

**File:** `source/core/DocumentViewport.cpp`

**In `handlePointerPress_ObjectSelect()`:**
```cpp
// When starting resize, also store original rotation:
m_resizeOriginalRotation = m_selectedObjects.first()->rotation;  // Phase O3.1.8.2
```

**Tasks:**
- [x] Add `m_resizeOriginalRotation` member
- [x] Store original rotation when starting resize operation

**Implementation Notes:**
- Member initialized to 0.0 by default
- Stored alongside size and position when any resize/rotate handle is grabbed
- Will be used by O3.1.8.3 for undo/redo

---

#### O3.1.8.3: Rotation Undo/Redo ✅

**Status:** COMPLETE

**File:** `source/core/DocumentViewport.h`

```cpp
// Added to PageUndoAction:
// ===== Object rotation fields (Phase O3.1.8.3) =====
qreal objectOldRotation = 0.0;      ///< For ObjectResize: rotation before (degrees)
qreal objectNewRotation = 0.0;      ///< For ObjectResize: rotation after (degrees)

// Added to EdgelessUndoAction:
// ===== Object rotation fields (Phase O3.1.8.3) =====
qreal objectOldRotation = 0.0;        ///< For ObjectResize: rotation before (degrees)
qreal objectNewRotation = 0.0;        ///< For ObjectResize: rotation after (degrees)

// Extended pushObjectResizeUndo() signature:
void pushObjectResizeUndo(InsertedObject* obj, const QPointF& oldPos, 
                          const QSizeF& oldSize, qreal oldRotation = 0.0);
```

**Implementation Choice:** Extended `ObjectResize` to include rotation (Option B), since both are "transform" operations. This avoids adding a new action type and keeps the undo system simpler.

**Tasks:**
- [x] Add `objectOldRotation` and `objectNewRotation` to both undo structs
- [x] Extend `pushObjectResizeUndo()` to include rotation parameter
- [x] Update call site to pass `m_resizeOriginalRotation`
- [x] Update condition to check for rotation changes
- [x] Update all 4 undo/redo cases (edgeless undo/redo, paged undo/redo)

**Implementation Notes:**
- Rotation parameter has default value of 0.0 for backward compatibility
- Condition now checks `obj->rotation != m_resizeOriginalRotation` in addition to size/position
- All 4 undo/redo cases now restore/apply rotation alongside position and size
- Debug output updated to include rotation value

---

#### O3.1.8.4: Rotation Rendering Considerations ✅

**Status:** VERIFIED (existing code already handles rotation correctly)

**Note:** `ImageObject::render()` already handles rotation correctly:
```cpp
if (rotation != 0.0) {
    painter.translate(position + QPointF(size.width() / 2, size.height() / 2));
    painter.rotate(rotation);
    painter.translate(-size.width() / 2, -size.height() / 2);
    // Draw at origin
}
```

**Selection box for rotated objects:**
- Current selection box assumes axis-aligned bounds
- For simplicity, keep selection box axis-aligned (don't rotate it)
- Alternative: Rotate the selection box to match object (more complex)

**Hit testing for rotated objects:**
- Current hit testing uses axis-aligned bounds
- For MVP: Keep axis-aligned hit testing (may feel slightly off for rotated objects)
- Future: Transform pointer position into object-local coords for accurate hit testing

---

#### O3.1.8.BF.1: Memory Safety Fix - setDocument() ✅

**Issue Found:** `setDocument()` did not clear `m_selectedObjects` and `m_hoveredObject` when changing documents. This could lead to dangling pointers if the user had objects selected and then opened a different document.

**Fix Applied:** Added selection cleanup at the beginning of `setDocument()`:
```cpp
// Clear object selection (pointers refer to old document's objects)
// Must be done BEFORE changing m_document to avoid dangling pointer access
bool hadSelection = !m_selectedObjects.isEmpty();
m_selectedObjects.clear();
m_hoveredObject = nullptr;
m_isDraggingObjects = false;
m_isResizingObject = false;

// ... later after document change ...
if (hadSelection) {
    emit objectSelectionChanged();
}
```

**Abstraction Principle Verification:**
- ✅ All rotation code accesses `obj->rotation` through the `InsertedObject` base class
- ✅ No type-specific casting (e.g., `dynamic_cast<ImageObject*>`) in rotation logic
- ✅ The only `ImageObject` references in `DocumentViewport.cpp` are in factory methods (`insertImageFromClipboard`, `insertImageFromFile`) which legitimately need to create specific object types

---

#### O3.1.8.5: Testing Checklist (Rotation)

- [ ] Rotation handle drag rotates object
- [ ] Rotation snaps to 15° increments
- [ ] Rotation undo/redo works
- [ ] Rotated objects render correctly
- [ ] Rotation persists after save/load
- [ ] Rotation cursor shows on hover (CrossCursor or custom)

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

### O3.6: Object Properties Panel

**Goal:** Provide UI to view and edit object properties, especially layer affinity for testing.

**Status:** Implementation required for affinity testing.

**Design Decision:** Use a floating widget (QDockWidget or custom QWidget) that appears when objects are selected. Lightweight approach - no full property panel infrastructure.

---

#### O3.6.1: Properties Widget UI Design

**Goal:** Create a minimal widget showing object properties.

**Mockup:**
```
┌─────────────────────────────────────┐
│ Object Properties              [×] │
├─────────────────────────────────────┤
│ Type: Image                         │
│                                     │
│ Position                            │
│   X: [_____123.5___] px            │
│   Y: [_____456.0___] px            │
│                                     │
│ Size                                │
│   W: [_____300_____] px            │
│   H: [_____200_____] px            │
│   [ ] Lock aspect ratio            │
│                                     │
│ Layer                               │
│   Affinity: [▼ Below all strokes ] │
│     • Below all strokes (-1)       │
│     • Above Layer 0                │
│     • Above Layer 1                │
│     • Above Layer 2                │
│                                     │
│ Stacking                            │
│   zOrder: [____0____]              │
│   [↑ Front] [↓ Back]               │
│                                     │
│ [ ] Locked                          │
│ [ ] Visible                         │
└─────────────────────────────────────┘
```

**Key Features:**
- Shows when single object selected
- Hides when no selection or multiple selection (future: show "Multiple objects selected")
- Editable fields update object immediately
- Changes trigger undo entries

---

#### O3.6.2: Create ObjectPropertiesWidget Class

**New File:** `source/widgets/ObjectPropertiesWidget.h`

```cpp
#pragma once

#include <QWidget>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>

class InsertedObject;
class DocumentViewport;

/**
 * @brief Widget for viewing/editing selected object properties.
 * 
 * Phase O3.6: Provides UI for testing layer affinity and other properties.
 */
class ObjectPropertiesWidget : public QWidget
{
    Q_OBJECT
    
public:
    explicit ObjectPropertiesWidget(QWidget* parent = nullptr);
    
    /**
     * @brief Set the viewport this widget interacts with.
     */
    void setViewport(DocumentViewport* viewport);
    
    /**
     * @brief Update display for selected object.
     * @param obj The selected object (nullptr if no selection)
     */
    void setObject(InsertedObject* obj);
    
signals:
    /**
     * @brief Emitted when user changes a property value.
     */
    void propertyChanged();
    
private slots:
    void onPositionChanged();
    void onSizeChanged();
    void onAffinityChanged(int index);
    void onZOrderChanged(int value);
    void onLockedChanged(bool locked);
    void onVisibleChanged(bool visible);
    void onBringToFront();
    void onSendToBack();
    
private:
    void setupUI();
    void updateFromObject();
    void blockSignalsForUpdate(bool block);
    
    DocumentViewport* m_viewport = nullptr;
    InsertedObject* m_currentObject = nullptr;
    
    // UI elements
    QLabel* m_typeLabel;
    QDoubleSpinBox* m_posXSpinBox;
    QDoubleSpinBox* m_posYSpinBox;
    QDoubleSpinBox* m_widthSpinBox;
    QDoubleSpinBox* m_heightSpinBox;
    QCheckBox* m_aspectLockCheckBox;
    QComboBox* m_affinityComboBox;
    QSpinBox* m_zOrderSpinBox;
    QPushButton* m_bringToFrontBtn;
    QPushButton* m_sendToBackBtn;
    QCheckBox* m_lockedCheckBox;
    QCheckBox* m_visibleCheckBox;
    
    bool m_updatingFromObject = false;  // Prevent feedback loops
};
```

**Tasks:**
- [ ] Create `source/widgets/ObjectPropertiesWidget.h`
- [ ] Create `source/widgets/ObjectPropertiesWidget.cpp`
- [ ] Add to CMakeLists.txt

---

#### O3.6.3: Implement ObjectPropertiesWidget

**File:** `source/widgets/ObjectPropertiesWidget.cpp`

**Key Implementation Points:**

1. **setupUI()** - Create layout with spin boxes, combo box, checkboxes
2. **setObject()** - Store reference, update UI
3. **updateFromObject()** - Populate fields from object properties
4. **onAffinityChanged()** - Critical for testing:

```cpp
void ObjectPropertiesWidget::onAffinityChanged(int index)
{
    if (m_updatingFromObject || !m_currentObject || !m_viewport) return;
    
    // Combo box items: -1, 0, 1, 2, 3, ...
    int newAffinity = index - 1;  // Index 0 = "Below all" = -1
    
    if (newAffinity != m_currentObject->getLayerAffinity()) {
        // Get the page containing this object
        Page* page = nullptr;
        if (m_viewport->document()->isEdgeless()) {
            // Find tile containing object
            for (const auto& coord : m_viewport->document()->allLoadedTileCoords()) {
                Page* tile = m_viewport->document()->getTile(coord.first, coord.second);
                if (tile && tile->objectById(m_currentObject->id)) {
                    page = tile;
                    break;
                }
            }
        } else {
            page = m_viewport->document()->page(m_viewport->currentPageIndex());
        }
        
        if (page) {
            // Create undo entry before changing
            // pushObjectAffinityUndo(m_currentObject, oldAffinity);
            
            // Update affinity via Page method (rebuilds affinity map)
            page->updateObjectAffinity(m_currentObject->id, newAffinity);
            
            // Mark dirty and trigger repaint
            emit propertyChanged();
        }
    }
}
```

**Tasks:**
- [ ] Implement `setupUI()` with proper layout
- [ ] Implement `setObject()` and `updateFromObject()`
- [ ] Implement property change handlers
- [ ] Connect signals to slots

---

#### O3.6.4: Integrate with MainWindow

**Goal:** Show properties widget in a dock or side panel.

**File:** `source/MainWindow.cpp`

**Option A: QDockWidget (Recommended)**
```cpp
// In MainWindow constructor or setupUI:
m_objectPropertiesWidget = new ObjectPropertiesWidget(this);
m_objectPropertiesDock = new QDockWidget(tr("Object Properties"), this);
m_objectPropertiesDock->setWidget(m_objectPropertiesWidget);
m_objectPropertiesDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
addDockWidget(Qt::RightDockWidgetArea, m_objectPropertiesDock);
m_objectPropertiesDock->hide();  // Hidden by default
```

**Option B: Floating Window**
```cpp
// Create as tool window
m_objectPropertiesWidget = new ObjectPropertiesWidget(nullptr);
m_objectPropertiesWidget->setWindowFlags(Qt::Tool);
m_objectPropertiesWidget->setWindowTitle(tr("Object Properties"));
```

**Tasks:**
- [ ] Add `ObjectPropertiesWidget*` member to MainWindow
- [ ] Create and add dock widget in MainWindow constructor
- [ ] Add menu item: View → Object Properties (toggle)

---

#### O3.6.5: Connect to Selection Changes

**Goal:** Update properties widget when selection changes.

**File:** `source/MainWindow.cpp`

```cpp
// Connect to DocumentViewport's selection signal
connect(viewport, &DocumentViewport::objectSelectionChanged,
        this, &MainWindow::onObjectSelectionChanged);

void MainWindow::onObjectSelectionChanged()
{
    DocumentViewport* vp = currentViewport();
    if (!vp) return;
    
    const auto& selected = vp->selectedObjects();
    
    if (selected.size() == 1) {
        m_objectPropertiesWidget->setObject(selected.first());
        m_objectPropertiesDock->show();
    } else if (selected.isEmpty()) {
        m_objectPropertiesWidget->setObject(nullptr);
        // Optionally hide: m_objectPropertiesDock->hide();
    } else {
        // Multiple selection - show "Multiple objects" or disable editing
        m_objectPropertiesWidget->setObject(nullptr);
    }
}
```

**Tasks:**
- [ ] Add `onObjectSelectionChanged()` slot to MainWindow
- [ ] Connect signal to slot for each tab's viewport
- [ ] Update on tab change

---

#### O3.6.6: Affinity Combo Box Population

**Goal:** Dynamically populate affinity options based on document layers.

```cpp
void ObjectPropertiesWidget::populateAffinityComboBox()
{
    m_affinityComboBox->clear();
    
    // Always available: below all strokes
    m_affinityComboBox->addItem(tr("Below all strokes"), -1);
    
    // Add one entry per layer
    int layerCount = 4;  // TODO: get from document
    if (m_viewport && m_viewport->document()) {
        layerCount = m_viewport->document()->isEdgeless() 
            ? m_viewport->document()->edgelessLayerCount()
            : m_viewport->document()->page(0)->layers.size();
    }
    
    for (int i = 0; i < layerCount; ++i) {
        m_affinityComboBox->addItem(tr("Above Layer %1").arg(i), i);
    }
}
```

**Tasks:**
- [ ] Implement dynamic layer enumeration
- [ ] Call `populateAffinityComboBox()` when object changes

---

#### O3.6.7: Undo/Redo for Property Changes

**Goal:** Make property edits undoable.

**File:** `source/core/DocumentViewport.h`

```cpp
// Already exists: ObjectAffinityChange in PageUndoAction::Type
// Need to add: ObjectPropertyChange for position/size/etc via properties panel
```

**Implementation Notes:**
- Position/size changes via properties panel should create ObjectMove/ObjectResize undo entries
- Affinity changes use existing ObjectAffinityChange type
- zOrder changes: could use existing zOrder methods or add ObjectZOrderChange type

**Tasks:**
- [ ] Wire property changes to appropriate undo helpers
- [ ] Test undo/redo of affinity changes
- [ ] Test undo/redo of position/size changes from panel

---

#### O3.6.8: Testing Checklist

- [ ] Panel appears when single object selected
- [ ] Panel hides/updates when selection cleared
- [ ] Position fields update object position
- [ ] Size fields update object size
- [ ] **Affinity dropdown changes layer affinity** (critical for testing)
- [ ] Object renders in correct layer order after affinity change
- [ ] zOrder changes work
- [ ] Locked checkbox prevents editing
- [ ] Visible checkbox toggles rendering
- [ ] Changes are undoable

---

## Task Summary

| Phase | Tasks | Priority | Status | Est. Effort |
|-------|-------|----------|--------|-------------|
| O1.1 | Layer affinity property | P0 | ✅ | 1 hour |
| O1.2 | Object grouping by affinity | P0 | ✅ | 2 hours |
| O1.3 | Paged mode interleaved rendering | P0 | ✅ | 2 hours |
| O1.4 | Edgeless multi-pass rendering | P0 | ✅ | 4 hours |
| O1.5 | Extended tile loading margin | P0 | ✅ | 1 hour |
| O1.6 | Unified bundle format | P0 | ✅ | 3 hours |
| O1.7 | Paged mode lazy loading | P0 | ✅ | 6 hours |
| O1.8 | Testing Phase 1 | P0 | ✅ | 2 hours |
| **Phase 1 Total** | | | ✅ | **~21 hours** |
| O2.1 | Object select tool | P1 | ✅ | 3 hours |
| O2.2 | Selection API | P1 | ✅ | 1 hour |
| O2.3 | Object movement | P1 | ✅ | 2 hours |
| O2.4 | Clipboard paste | P1 | ✅ | 2 hours |
| O2.5 | Object deletion | P1 | ✅ | 1 hour |
| O2.6 | Object copy/paste | P1 | ✅ | 2 hours |
| O2.7 | Undo/redo integration | P1 | ✅ | 3 hours |
| O2.8 | zOrder shortcuts | P1 | ✅ | 1 hour |
| O2.9 | MainWindow connection | P1 | ✅ | 1 hour |
| O2.BF | Bug fixes during testing | P1 | ✅ | 4 hours |
| O2.C | Code cleanup (abstraction) | P1 | ✅ | 1 hour |
| **Phase 2 Total** | | | ✅ | **~21 hours** |
| **O3.1** | **Object Resize** | P1 | 🔲 | 3 hours |
| **O3.1.8** | **Object Rotation** | P1 | 🔲 | 2-3 hours |
| **O3.6** | **Object Properties Panel** | P1 | 🔲 | 4 hours |
| O3.2-O3.5 | Deferred features | P2 | - | TBD |
| **Phase 3 (Testing)** | | | 🔲 | **~7-8 hours** |

**Total Estimated: ~51-52 hours for Complete MVP (Phase 1 + Phase 2 + O3.1 + O3.1.8 + O3.6)**

---

## Execution Order

```
O1.1 → O1.2 → O1.3 ─┬─→ O1.6 → O1.7 → O1.8
                    │
O1.4 → O1.5 ────────┘
                    
O2.1 → O2.2 → O2.3 → O2.4 → O2.5 → O2.6 → O2.7 → O2.8 → O2.9 → O2.10

O3.1.1 → O3.1.2 → O3.1.3 → O3.1.4 → O3.1.5 → O3.1.6 → O3.1.7 → O3.1.8 → O3.6
```

Phase 1 tasks can be parallelized:
- O1.1-O1.3 (paged rendering) independent from O1.4-O1.5 (edgeless rendering)
- O1.6-O1.7 (bundle format) depends on both

Phase 2 is mostly sequential (each builds on previous).

Phase 3 (O3.1.x): Resize and rotation are sequential, rotation reuses resize infrastructure.

---

*Implementation begins with O1.1: Layer Affinity Property*
