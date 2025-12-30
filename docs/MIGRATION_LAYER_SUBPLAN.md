# Phase 5: LayerPanel Integration Subplan

> **Purpose:** Multi-layer support with LayerPanel UI integration
> **Created:** Dec 28, 2024
> **Status:** 🔄 IN PROGRESS (Phase 5.1, 5.2 Complete → Phase 5.6 Next)

---

## Overview

This subplan covers the integration of multi-layer support into SpeedyNote. The data structures for layers already exist (`VectorLayer`, `Page.vectorLayers`), and a `LayerPanel` widget exists but is not connected to the UI.

### Current State

**Already Implemented:**
- `VectorLayer` class with UUID id, name, visible, opacity, locked, strokes
- `Page` with `std::vector<std::unique_ptr<VectorLayer>>` and `activeLayerIndex`
- `LayerPanel` widget with add/remove/move up/move down/visibility toggle
- Serialization (layers saved per page/tile with all properties)
- Layer rendering with caching (`VectorLayer::renderWithZoomCache`)

**Not Yet Implemented:**
- ~~LayerPanel not placed in MainWindow~~ ✅ Phase 5.1
- ~~LayerPanel not connected to DocumentViewport~~ ✅ Phase 5.1
- ~~Layer rename (double-click to edit)~~ ✅ Phase 5.2
- Edgeless global layer manifest ← **Phase 5.6 (NEXT)**
- Duplicate layer ← Phase 5.5 (after 5.6)
- Select/tick UI for batch operations ← Phase 5.3 (after 5.6)
- Merge layers ← Phase 5.4 (after 5.3)
- Multi-layer undo integration ← Phase 5.7 (after all operations)

---

## Architecture Decisions

### Edgeless Layer Scope: Global Structure, Per-Tile Storage

For edgeless mode, layers work as follows:

```
document.json (manifest):
├── layers: [
│     {id: "uuid1", name: "Sketch", visible: true},
│     {id: "uuid2", name: "Ink", visible: true}
│   ]
├── active_layer_index: 1
└── tile_index: ["0,0", "1,0", "-1,0", ...]

tiles/0,0.json:
├── layers: [
│     {id: "uuid1", strokes: [...]},  ← has content
│     {id: "uuid2", strokes: [...]}   ← has content
│   ]

tiles/1,0.json:
├── layers: [
│     {id: "uuid1", strokes: [...]}   ← has content
│   ]                                  ← uuid2 omitted (no content)
```

**Key behaviors:**
- Add/remove/rename/reorder layer → 1 disk write (manifest only)
- Tiles only store layers with content
- When loading tile, empty layers reconstructed from manifest
- Active layer selection is global for drawing (affects all tiles)
- `activeLayerIndex` stored per-tile for serialization compatibility

### Active Layer in Edgeless Mode

When user selects a layer in LayerPanel:
1. `DocumentViewport::m_edgelessActiveLayerIndex` is updated (new member)
2. All drawing operations use this global index
3. Per-tile `activeLayerIndex` is updated when tile is modified

### Page Change with Different Layer Counts

When switching pages (paged mode):
- If new page has fewer layers than `activeLayerIndex`, clamp to `pageLayerCount - 1`
- Emit `activeLayerChanged` signal so LayerPanel updates

---

## UI Layout

```
┌─────────────────────────────────────────────────────────────┐
│  Menu Bar                                                    │
├─────────────────────────────────────────────────────────────┤
│  Tool Bar                                                    │
├──────────┬──────────────────────────────────────────────────┤
│          │                                                   │
│  Left    │                                                   │
│  Sidebar │              DocumentViewport                     │
│  (Tools) │                                                   │
│          │                                                   │
├──────────┤                                                   │
│          │                                                   │
│  Layer   │                                                   │
│  Panel   │                                                   │
│          │                                                   │
└──────────┴──────────────────────────────────────────────────┘
```

LayerPanel is placed **below** the left sidebar, sharing the vertical space.

---

## Phase Breakdown

### Phase 5.1: LayerPanel Integration ✅ COMPLETE

**Goal:** Connect existing LayerPanel to MainWindow and DocumentViewport.

**Tasks:**
1. ✅ Add `LayerPanel* m_layerPanel` member to MainWindow
2. ✅ Create LayerPanel in MainWindow constructor  
3. ✅ Place below left sidebar (adjust layout)
   - Created `m_leftSideContainer` (QWidget with QVBoxLayout)
   - Top: `leftSidebarsWidget` with outline + bookmarks sidebars (stretch=1)
   - Bottom: `m_layerPanel` (fixed 250px width, 180-300px height)

4. ✅ Connect `TabManager::currentViewportChanged` → update LayerPanel's page
   - Added `updateLayerPanelForViewport()` helper method
   - For edgeless: uses origin tile (0,0) as representative page
   - For paged: uses `doc->page(currentPageIndex())`

5. ✅ Connect `DocumentViewport::currentPageChanged` → update LayerPanel's page
   - Added `m_layerPanelPageConn` connection member
   - Properly disconnects when switching viewports

6. ✅ Connect `LayerPanel::layerVisibilityChanged` → `viewport->update()`
7. ✅ Connect `LayerPanel::activeLayerChanged` → update drawing target
   - Edgeless: calls `vp->setEdgelessActiveLayerIndex()`
   - Paged: `Page::activeLayerIndex` already updated by LayerPanel

8. ✅ Connect `LayerPanel::layerAdded/Removed/Moved` → mark document modified
   - Emits `vp->documentModified()` signal
   - Triggers viewport repaint

9. ✅ Handle page change: clamp `activeLayerIndex` if needed
   - In `currentPageChanged` handler, clamps to `layerCount - 1`

10. ✅ Add `m_edgelessActiveLayerIndex` to DocumentViewport (already existed!)
    - Added `setEdgelessActiveLayerIndex()` setter
    - Added `edgelessActiveLayerIndex()` getter

**Files to modify:**
- `source/MainWindow.h` - Add m_layerPanel member
- `source/MainWindow.cpp` - Create, place, connect LayerPanel
- `source/core/DocumentViewport.h` - Add m_edgelessActiveLayerIndex
- `source/core/DocumentViewport.cpp` - Use correct layer for edgeless drawing

**Test cases:**
- [ ] LayerPanel appears below left sidebar
- [ ] Switching tabs updates LayerPanel
- [ ] Scrolling to new page updates LayerPanel (paged mode)
- [ ] Add layer works, strokes go to new layer
- [ ] Toggle visibility hides/shows layer strokes
- [ ] Drawing goes to selected layer
- [ ] Move up/down changes render order

**Code Review Fixes (Dec 29, 2024):**

| ID | Issue | Fix |
|----|-------|-----|
| CR-L1 | Unused variable `tileLayerCount` in `syncTileLayerStructure` | Removed dead code |
| CR-L2 | Contradictory comment about ID sync | Clarified: IDs synced only when creating new layers, not for existing layers |
| CR-L3 | Inefficient `getOrCreateTile()` on already-loaded tiles | Changed to `getTile()` since `allLoadedTileCoords()` returns loaded tiles |
| CR-L4 | Layer changes not saved in edgeless mode | Added `markTileDirty()` calls for all layer operations (visibility, add, remove, move, rename) |

**Additional Fixes for Edgeless Mode:**

| Issue | Cause | Fix |
|-------|-------|-----|
| LayerPanel locks up when scrolling far | Origin tile (0,0) was being evicted | Never evict origin tile in `evictDistantTiles()` |
| Layer order inconsistent across tiles | Tiles loaded from disk had old structure | Added `syncTileLayerStructure()` called on tile load/create |

---

### Phase 5.2: Layer Rename ✅ COMPLETE

**Goal:** Double-click layer name to edit inline.

**Tasks:**
1. ✅ Enable `QListWidget` item editing on double-click
   - Added `Qt::ItemIsEditable` flag to items in `createLayerItem()`
2. ✅ Connect `itemChanged` signal to update `VectorLayer::name`
   - Added `onItemChanged()` slot that strips visibility prefix and updates name
3. ✅ Emit signal for document modified
   - Added `layerRenamed(int index, const QString& newName)` signal
   - Connected in MainWindow to emit `documentModified()`
4. ✅ Ensure serialization saves updated name
   - VectorLayer::name is already serialized - no changes needed
5. ✅ Sync rename to all tiles in edgeless mode
   - Connected `layerRenamed` signal to sync name across all loaded tiles

**Files modified:**
- `source/ui/LayerPanel.h` - Added `layerRenamed` signal, `onItemChanged` slot, `m_notVisibleIcon` member
- `source/ui/LayerPanel.cpp` - Implemented rename handling, icon-based visibility
- `source/MainWindow.cpp` - Connected signal for edgeless sync and document modified

**Visibility Display:**
- Visible layers: just layer name (no icon)
- Hidden layers: "notvisible" icon + layer name
- Uses themed icon (`notvisible.png` / `notvisible_reversed.png`) for dark/light modes

**Test cases:**
- [ ] Double-click layer opens inline editor
- [ ] Enter commits new name
- [ ] Escape cancels edit
- [ ] Name persists after save/load
- [ ] In edgeless mode, rename syncs to all tiles
- [ ] Hidden layers show notvisible icon
- [ ] Visible layers show no icon

---

### Phase 5.3: Select/Tick UI ✅ COMPLETE

**Goal:** Add checkbox selection to layers for batch operations.

**Design:** Each layer item shows:
```
[✓] [eye/empty] Layer Name
```
- Checkbox for selection (left, ~20px)
- Eye icon for hidden layers (20-50px click area for visibility toggle)
- Layer name (double-click to edit)

**Tasks:**
1. ✅ Add checkbox to layer items (`Qt::ItemIsUserCheckable`)
2. ✅ Track selected layers via `selectedLayerIndices()` / `selectedLayerCount()`
3. ✅ Add "All" / "None" / "Merge" buttons
4. ✅ Selection state is UI-only (checkboxes reset on `refreshLayerList()`)
5. ✅ Merge button enabled only when 2+ layers checked

**Files modified:**
- `source/ui/LayerPanel.h` - Added selection API, signals, merge button
- `source/ui/LayerPanel.cpp` - Added checkboxes, selection logic, button handlers

**New API:**
```cpp
// Selection queries
QVector<int> selectedLayerIndices() const;
int selectedLayerCount() const;
void selectAllLayers();
void deselectAllLayers();

// Signals
void selectionChanged(QVector<int> selectedIndices);
void layersMerged(int targetIndex, QVector<int> mergedIndices);
```

**Test cases:**
- [x] Checkboxes appear on each layer
- [x] Can select multiple layers
- [x] Selection persists while panel is open
- [x] Selection cleared when page changes (via `refreshLayerList()`)

---

### Phase 5.4: Merge Layers ✅ COMPLETE

**Goal:** Merge selected layers into one.

**Behavior:**
1. User selects 2+ layers via checkboxes
2. Clicks "Merge" button
3. All strokes combined into bottom-most selected layer
4. Other selected layers removed
5. Layer order preserved for unselected layers

**Implementation:**

1. **Page::mergeLayers(targetIndex, sourceIndices)** - `source/core/Page.cpp`
   - Validates indices, ensures at least one layer remains
   - Collects strokes from source layers into target layer
   - Removes source layers in reverse order (highest index first)
   - Adjusts activeLayerIndex if needed

2. **Document::mergeEdgelessLayers(targetIndex, sourceIndices)** - `source/core/Document.cpp`
   - Same logic but operates on all loaded tiles
   - For each tile: moves strokes to target layer, clears source layers
   - Then removes source layers from manifest and all tiles
   - Marks tiles dirty and manifest dirty

3. **LayerPanel::onMergeClicked()** - `source/ui/LayerPanel.cpp`
   - Gets selected indices, sorts them to find lowest as target
   - Calls Page::mergeLayers() or Document::mergeEdgelessLayers()
   - Refreshes layer list
   - Emits layersMerged signal

**Files modified:**
- `source/core/Document.h` - Added mergeEdgelessLayers() declaration
- `source/core/Document.cpp` - Implemented mergeEdgelessLayers()
- `source/core/Page.h` - Added mergeLayers() declaration  
- `source/core/Page.cpp` - Implemented mergeLayers()
- `source/ui/LayerPanel.cpp` - Implemented merge logic in onMergeClicked()

**Bug Fix - CR-L10: Eraser fails on merged strokes**

**Problem:** After merging layers, the eraser could not erase strokes that existed before the merge (but could erase strokes drawn after the merge). After save/reload, everything worked.

**Root Cause:** `DocumentViewport` has its own `m_edgelessActiveLayerIndex` that was NOT synchronized after the merge:
1. `Document::mergeEdgelessLayers()` removes source layers and adjusts `Document::m_edgelessActiveLayerIndex`
2. `refreshLayerList()` is called with `m_updatingList = true`, which suppresses `activeLayerChanged` signal
3. `DocumentViewport::m_edgelessActiveLayerIndex` remains at the OLD value (e.g., 1)
4. The eraser checks `if (m_edgelessActiveLayerIndex >= tile->layerCount()) continue;`
5. Since layer 1 was removed, index 1 is now out of bounds → all tiles skipped → no strokes found!

**Fix:** In `LayerPanel::onMergeClicked()`, explicitly emit `activeLayerChanged(targetIndex)` after the merge to sync the viewport's active layer index. The target layer (lowest index among merged) becomes the new active layer.

**Test cases:**
- [x] Merge button disabled with 0-1 selected (enforced in updateButtonStates)
- [x] Merge combines strokes correctly
- [x] Layer order preserved (removes from highest to lowest index)
- [x] Eraser works on merged strokes (CR-L10 fix)
- [ ] Undo restores original layers (Phase 5.7)

---

### Phase 5.5: Duplicate Layer ✅ COMPLETE

**Goal:** Copy a layer with all its strokes.

**Implementation:**

1. **Document::duplicateEdgelessLayer(index)** - `source/core/Document.cpp`
   - Creates a new `LayerDefinition` with name "OriginalName Copy"
   - Generates new UUID for the layer
   - Inserts at `index + 1` (above original)
   - For each loaded tile: creates a new VectorLayer with deep-copied strokes (new UUIDs)
   - Moves the new layer to the correct position
   - Marks tiles dirty and manifest dirty

2. **Page::duplicateLayer(index)** - `source/core/Page.cpp`
   - Creates a new `VectorLayer` with name "OriginalName Copy"
   - Deep copies all strokes with new UUIDs
   - Inserts at `index + 1` (above original)
   - Adjusts active layer index if needed

3. **LayerPanel::onDuplicateClicked()** - `source/ui/LayerPanel.cpp`
   - Gets the currently selected layer
   - Routes to Page::duplicateLayer() or Document::duplicateEdgelessLayer()
   - Refreshes layer list
   - Emits `activeLayerChanged(newIndex)` to sync viewport
   - Emits `layerDuplicated(originalIndex, newIndex)` for MainWindow

4. **MainWindow connections**
   - Connects to `layersMerged` and `layerDuplicated` signals
   - Emits `documentModified()` and calls `vp->update()`

**Files modified:**
- `source/core/Document.h` - Added duplicateEdgelessLayer() declaration
- `source/core/Document.cpp` - Implemented duplicateEdgelessLayer()
- `source/core/Page.h` - Added duplicateLayer() declaration
- `source/core/Page.cpp` - Implemented duplicateLayer()
- `source/ui/LayerPanel.h` - Added duplicate button, slot, and signal
- `source/ui/LayerPanel.cpp` - Added button UI and onDuplicateClicked()
- `source/MainWindow.cpp` - Connected layersMerged and layerDuplicated signals

**Test cases:**
- [x] Duplicate creates new layer above original
- [x] All strokes copied with new IDs (new UUIDs generated)
- [x] New layer is selected (activeLayerChanged emitted)
- [x] Original layer unchanged (deep copy, not move)

---

### Phase 5.6: Edgeless Layer Manifest ✅ COMPLETE

**Goal:** Store layer definitions in edgeless document manifest, enabling O(1) layer operations and removing the origin tile special handling.

---

#### Current Architecture Problems

| Problem | Impact |
|---------|--------|
| Origin tile (0,0) cannot be evicted | Memory waste, crashes if user scrolls far |
| Layer ops iterate ALL loaded tiles | O(n) disk writes for add/remove/rename |
| Evicted tiles have stale layer data | Inconsistent state after layer changes |
| MainWindow has ~145 lines sync logic | Violates "thin hub" principle |
| Layer definitions duplicated per-tile | Wasteful storage, sync complexity |

---

#### New Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│  document.json (MANIFEST - Single Source of Truth)                  │
│  ├── layers: [                                                      │
│  │     {id:"uuid1", name:"Sketch", visible:true, opacity:1, locked:false},│
│  │     {id:"uuid2", name:"Ink", visible:true, opacity:1, locked:false}   │
│  │   ]                                                              │
│  ├── active_layer_index: 0                                          │
│  └── tile_index: ["0,0", "1,0", "-1,0", ...]                       │
├─────────────────────────────────────────────────────────────────────┤
│  tiles/0,0.json (STROKES ONLY)                                      │
│  └── layers: [                                                      │
│        {id:"uuid1", strokes:[...]},  ← Only id + strokes            │
│        {id:"uuid2", strokes:[...]}   ← No name/visible/opacity      │
│      ]                                                              │
├─────────────────────────────────────────────────────────────────────┤
│  tiles/1,0.json                                                     │
│  └── layers: [                                                      │
│        {id:"uuid1", strokes:[...]}   ← uuid2 omitted (empty)        │
│      ]                                                              │
└─────────────────────────────────────────────────────────────────────┘
```

**Key behaviors:**
- Add/remove/rename/reorder/visibility → 1 disk write (manifest only)
- Tiles only store layers that have strokes
- When loading tile: reconstruct VectorLayers from manifest + strokes from tile
- No special "origin tile" needed - all tiles equal
- Layer operations are O(1), not O(loaded_tiles)

---

#### Phase 5.6.1: LayerDefinition Struct & Document Members ✅ COMPLETE

**Added to `Document.h`:**
```cpp
/**
 * @brief Layer metadata for edgeless mode manifest.
 * Strokes are stored per-tile, but layer definitions (name, visibility, etc.)
 * are stored once in the manifest.
 */
struct LayerDefinition {
    QString id;
    QString name;
    bool visible = true;
    qreal opacity = 1.0;
    bool locked = false;
    
    QJsonObject toJson() const;
    static LayerDefinition fromJson(const QJsonObject& obj);
};

// In Document class:
private:
    // Edgeless layer manifest (Phase 5.6)
    std::vector<LayerDefinition> m_edgelessLayers;
    int m_edgelessActiveLayerIndex = 0;
    bool m_edgelessManifestDirty = false;  // Track if manifest needs saving

public:
    // Edgeless layer manifest API
    int edgelessLayerCount() const;
    const LayerDefinition* edgelessLayerDef(int index) const;
    QString edgelessLayerId(int index) const;
    
    int addEdgelessLayer(const QString& name);
    bool removeEdgelessLayer(int index);
    bool moveEdgelessLayer(int from, int to);
    void setEdgelessLayerVisible(int index, bool visible);
    void setEdgelessLayerName(int index, const QString& name);
    void setEdgelessLayerOpacity(int index, qreal opacity);
    void setEdgelessLayerLocked(int index, bool locked);
    
    int edgelessActiveLayerIndex() const;
    void setEdgelessActiveLayerIndex(int index);
    
    // Signal when manifest changes (for UI updates)
    bool isEdgelessManifestDirty() const { return m_edgelessManifestDirty; }
```

**Implementation notes:**
- Layer operations modify `m_edgelessLayers` and set `m_edgelessManifestDirty = true`
- No tile iteration needed for layer metadata changes
- Loaded tiles are NOT updated in memory (lazy - will sync on next load)

---

#### Phase 5.6.2: Manifest Save/Load ✅ COMPLETE

**Update `Document::saveBundle()`:**
```cpp
// In manifest building:
QJsonArray layersArray;
for (const auto& layerDef : m_edgelessLayers) {
    layersArray.append(layerDef.toJson());
}
manifest["layers"] = layersArray;
manifest["active_layer_index"] = m_edgelessActiveLayerIndex;

// Clear manifest dirty flag after save
m_edgelessManifestDirty = false;
```

**Update `Document::loadBundle()`:**
```cpp
// Parse layer definitions from manifest
QJsonArray layersArray = obj["layers"].toArray();
m_edgelessLayers.clear();
for (const auto& val : layersArray) {
    m_edgelessLayers.push_back(LayerDefinition::fromJson(val.toObject()));
}

// Ensure at least one layer exists
if (m_edgelessLayers.empty()) {
    LayerDefinition defaultLayer;
    defaultLayer.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    defaultLayer.name = "Layer 1";
    m_edgelessLayers.push_back(defaultLayer);
}

m_edgelessActiveLayerIndex = obj["active_layer_index"].toInt(0);
```

**Update `Document::createNew()` for edgeless:**
```cpp
if (docMode == Mode::Edgeless) {
    // Create default layer in manifest
    LayerDefinition layer;
    layer.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    layer.name = "Layer 1";
    doc->m_edgelessLayers.push_back(layer);
}
```

---

#### Phase 5.6.3: Tile Save Format (Strokes Only) ✅ COMPLETE

**Update `Document::saveTile()`:**
```cpp
// For edgeless mode, use compact format (id + strokes only)
if (isEdgeless()) {
    QJsonObject tileObj;
    QJsonArray layersArray;
    
    Page* tile = it->second.get();
    for (int i = 0; i < tile->layerCount(); ++i) {
        VectorLayer* layer = tile->layer(i);
        if (layer && !layer->isEmpty()) {
            QJsonObject layerObj;
            layerObj["id"] = layer->id;
            
            QJsonArray strokesArray;
            for (const auto& stroke : layer->strokes()) {
                strokesArray.append(stroke.toJson());
            }
            layerObj["strokes"] = strokesArray;
            
            layersArray.append(layerObj);
        }
    }
    tileObj["layers"] = layersArray;
    // ... write tileObj
}
```

**Key change:** Don't save `name`, `visible`, `opacity`, `locked` per-layer in tiles. Only `id` + `strokes`.

---

#### Phase 5.6.4: Tile Load with Manifest Reconstruction ✅ COMPLETE

**Update `Document::loadTileFromDisk()`:**
```cpp
// For edgeless mode, reconstruct full layers from manifest
if (isEdgeless()) {
    QJsonArray tileLayersArray = jsonDoc["layers"].toArray();
    
    // Build map of layerId → strokes from tile
    std::map<QString, QVector<VectorStroke>> strokesByLayerId;
    for (const auto& val : tileLayersArray) {
        QJsonObject layerObj = val.toObject();
        QString layerId = layerObj["id"].toString();
        QVector<VectorStroke> strokes;
        for (const auto& strokeVal : layerObj["strokes"].toArray()) {
            strokes.append(VectorStroke::fromJson(strokeVal.toObject()));
        }
        strokesByLayerId[layerId] = strokes;
    }
    
    // Reconstruct full VectorLayers from manifest
    tile->vectorLayers.clear();
    for (const auto& layerDef : m_edgelessLayers) {
        auto layer = std::make_unique<VectorLayer>(layerDef.name);
        layer->id = layerDef.id;
        layer->visible = layerDef.visible;
        layer->opacity = layerDef.opacity;
        layer->locked = layerDef.locked;
        
        // Add strokes if this tile has any for this layer
        auto it = strokesByLayerId.find(layerDef.id);
        if (it != strokesByLayerId.end()) {
            for (const auto& stroke : it->second) {
                layer->addStroke(stroke);
            }
        }
        
        tile->vectorLayers.push_back(std::move(layer));
    }
    
    tile->activeLayerIndex = m_edgelessActiveLayerIndex;
}
```

---

#### Phase 5.6.5: Remove Origin Tile Special Handling ✅ COMPLETE

**Deleted from `DocumentViewport::evictDistantTiles()`:**
- Removed the `if (coord.first == 0 && coord.second == 0) { continue; }` check
- Origin tile can now be evicted like any other tile

**Deleted from `Document.cpp`:**
- ✅ Removed `syncTileLayerStructure()` method entirely (~55 lines)
- ✅ Removed calls to `syncTileLayerStructure()` in `getTile()` and `getOrCreateTile()`

**Deleted from `Document.h`:**
- ✅ Removed `syncTileLayerStructure(Page* tile) const` declaration

**Note:** New tiles created in `getOrCreateTile()` still get a default single layer from the `Page` constructor. Phase 5.6.6 will update this to initialize new tiles with the manifest layer structure.

---

#### Phase 5.6.6: Update getOrCreateTile for Manifest Layers ✅ COMPLETE

**Updated `Document::getOrCreateTile()`:**
- Added layer initialization from manifest BEFORE tile is inserted into `m_tiles`
- For edgeless mode with non-empty manifest:
  - Clears default layer from `Page` constructor
  - Creates `VectorLayer` for each `LayerDefinition` in manifest
  - Copies id, name, visible, opacity, locked from manifest
  - Sets `activeLayerIndex` from manifest

```cpp
// Phase 5.6.6: Initialize tile layer structure from manifest
if (isEdgeless() && !m_edgelessLayers.empty()) {
    tile->vectorLayers.clear();
    for (const auto& layerDef : m_edgelessLayers) {
        auto layer = std::make_unique<VectorLayer>(layerDef.name);
        layer->id = layerDef.id;
        layer->visible = layerDef.visible;
        layer->opacity = layerDef.opacity;
        layer->locked = layerDef.locked;
        tile->vectorLayers.push_back(std::move(layer));
    }
    tile->activeLayerIndex = m_edgelessActiveLayerIndex;
}
```

**Key behavior:** All new tiles now have the correct layer structure from creation, ensuring consistency across the entire edgeless canvas.

---

#### Phase 5.6.7: LayerPanel Connection Update ✅ COMPLETE

**Implemented: Option A/B Hybrid - Direct Document Integration**

Instead of a separate adapter class, modified LayerPanel to support dual modes:

**New API in LayerPanel:**
- `setCurrentPage(Page*)` - paged mode (existing, updated)
- `setEdgelessDocument(Document*)` - edgeless mode (new)
- `isEdgelessMode()` - check current mode

**Private abstraction helpers:**
```cpp
// Layer access (reads from Page or Document manifest)
int getLayerCount() const;
QString getLayerName(int index) const;
bool getLayerVisible(int index) const;
bool getLayerLocked(int index) const;
int getActiveLayerIndex() const;

// Layer modification (writes to Page or Document manifest)
void setLayerVisible(int index, bool visible);
void setLayerName(int index, const QString& name);
void setActiveLayerIndex(int index);
int addLayer(const QString& name);
bool removeLayer(int index);
bool moveLayer(int from, int to);
```

**Key changes:**
- All slot handlers now use abstracted helpers
- When in edgeless mode, operations go directly to `Document::m_edgelessLayers`
- No temporary VectorLayer objects needed
- Clean separation: LayerPanel doesn't know about tiles

---

#### Phase 5.6.8: Simplify MainWindow Layer Logic ✅ COMPLETE

**Document methods now sync to loaded tiles:**
Updated all `Document::setEdgeless*()` and `Document::addEdgelessLayer/removeEdgelessLayer/moveEdgelessLayer` methods to automatically sync changes to all loaded tiles. This eliminates the need for MainWindow to iterate tiles.

**Simplified signal handlers (~138 lines → ~55 lines):**
```cpp
// Visibility change → just repaint (Document already synced tiles)
connect(m_layerPanel, &LayerPanel::layerVisibilityChanged, this, [this](int, bool) {
    if (auto* vp = currentViewport()) vp->update();
});

// Active layer → sync to viewport for edgeless drawing target
connect(m_layerPanel, &LayerPanel::activeLayerChanged, this, [this](int idx) {
    if (auto* vp = currentViewport()) {
        if (auto* doc = vp->document(); doc && doc->isEdgeless()) {
            vp->setEdgelessActiveLayerIndex(idx);
        }
    }
});

// Structural changes → mark modified and repaint
connect(m_layerPanel, &LayerPanel::layerAdded, this, [this](int) {
    if (auto* vp = currentViewport()) { emit vp->documentModified(); vp->update(); }
});
// Similar for layerRemoved, layerMoved, layerRenamed
```

**Updated `updateLayerPanelForViewport()`:**
- For edgeless: calls `m_layerPanel->setEdgelessDocument(doc)` 
- For paged: calls `m_layerPanel->setCurrentPage(page)` (unchanged)
- No more origin tile (0,0) references

---

#### Phase 5.6.9: Migration & Backward Compatibility ⏭️ SKIPPED

> **Note:** Skipped because the old tile-based layer format was never released.
> If needed in the future, implement version detection and migration from tile layer data.

**Original plan (not implemented):**
```cpp
// Check manifest version
int manifestVersion = obj["format_version"].toString("1.0").split('.')[0].toInt();

if (manifestVersion < 2 || !obj.contains("layers")) {
    // Old format: layers stored per-tile
    // Migration: read layer structure from first available tile
    migrateLayersFromTileToManifest();
}
```

**Migration helper:**
```cpp
void Document::migrateLayersFromTileToManifest() {
    // Load any existing tile to get layer structure
    for (const auto& coord : m_tileIndex) {
        if (loadTileFromDisk(coord)) {
            Page* tile = m_tiles[coord].get();
            for (int i = 0; i < tile->layerCount(); ++i) {
                VectorLayer* layer = tile->layer(i);
                LayerDefinition def;
                def.id = layer->id;
                def.name = layer->name;
                def.visible = layer->visible;
                def.opacity = layer->opacity;
                def.locked = layer->locked;
                m_edgelessLayers.push_back(def);
            }
            m_edgelessActiveLayerIndex = tile->activeLayerIndex;
            break;  // Only need one tile for structure
        }
    }
    
    // Ensure at least one layer
    if (m_edgelessLayers.empty()) {
        LayerDefinition def;
        def.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        def.name = "Layer 1";
        m_edgelessLayers.push_back(def);
    }
    
    m_edgelessManifestDirty = true;  // Save new format on next save
}
```

---

#### Files to Modify

| File | Changes |
|------|---------|
| `Document.h` | Add `LayerDefinition`, `m_edgelessLayers`, layer API methods |
| `Document.cpp` | Implement layer API, update saveBundle/loadBundle/saveTile/loadTile, add migration |
| `DocumentViewport.cpp` | Remove origin tile special handling, update drawing to use manifest layer index |
| `MainWindow.cpp` | Simplify layer signal handlers (~145 lines → ~30 lines) |
| `LayerPanel.h/cpp` | Add `setEdgelessDocument()` or adapter pattern |

---

#### Test Cases

| Test | Expected |
|------|----------|
| Add layer in edgeless | 1 disk write (manifest), no tile writes |
| Rename layer | 1 disk write (manifest), loaded tiles unaffected |
| Toggle visibility | Manifest updated, all loaded tiles respect new visibility |
| Evict all tiles, reload | Layers reconstructed correctly from manifest |
| Open old .snb (no manifest layers) | Migration creates manifest from first tile |
| 100 loaded tiles, add layer | O(1) operation, not O(100) |
| Origin tile (0,0) eviction | Should evict normally (no longer special) |
| Save bundle after layer ops | Manifest has new structure, tiles unchanged |

---

#### Success Criteria

- [x] Layer operations are O(1) disk writes
- [x] No tile iteration for layer metadata changes
- [x] Origin tile can be evicted like any other tile
- [x] MainWindow layer logic reduced to ~55 lines (was 138)
- [ ] ~~Old .snb files migrate transparently~~ (skipped - never released)
- [x] All layer features work: visibility, rename, add, remove, reorder
- [x] Active layer selection is global and persistent

---

#### Phase 5.6 Code Review (Dec 29, 2024)

| ID | Issue | Severity | Fix |
|----|-------|----------|-----|
| CR-L5 | Property setters marked ALL tiles dirty, even if layer didn't exist | Minor (Perf) | Only mark tile dirty if layer was actually updated |
| CR-L6 | `edgelessLayerCount()` comment claimed "always >= 1" but code didn't enforce | Minor (Doc) | Clarified comment: "createNew() and loadBundle() ensure >= 1" |
| CR-L7 | `setEdgelessActiveLayerIndex()` could set index=-1 if layers empty | Minor (Edge) | Added early return for empty layers, use `qBound()` for clamping |
| CR-L8 | LayerPanel could have dangling pointer when Document deleted | Medium (Crash) | Clear LayerPanel's document BEFORE calling `closeDocument()` |
| CR-L9 | `setEdgelessActiveLayerIndex()` marked all tiles dirty unnecessarily | Minor (Perf) | Removed dirty marking - activeLayerIndex is in manifest, not per-tile |

**All issues fixed in this review.**

---

### Phase 5.7: Multi-Layer Undo/Redo 🟡 PARTIAL

**Goal:** Ensure undo/redo works correctly with multiple layers.

---

#### Analysis of Current State

**✅ ALREADY WORKING: Stroke-level undo with layer awareness**

The existing undo system correctly handles strokes across layers:

1. **Stroke tracking includes layer index:**
   ```cpp
   // In EdgelessUndoAction (DocumentViewport.h):
   int layerIndex = 0;  ///< Which layer was affected
   ```

2. **When drawing/erasing, layer index is captured:**
   ```cpp
   // finishStrokeEdgeless() and eraseAtEdgeless():
   undoAction.layerIndex = m_edgelessActiveLayerIndex;
   ```

3. **Undo/redo restores to correct layer:**
   ```cpp
   // undoEdgeless() and redoEdgeless():
   while (tile->layerCount() <= action.layerIndex) {
       tile->addLayer(QString("Layer %1").arg(tile->layerCount() + 1));
   }
   VectorLayer* layer = tile->layer(action.layerIndex);
   ```

4. **Layer auto-creation on undo:** If a layer was deleted, undoing a stroke that was on that layer will auto-recreate the layer structure up to the needed index.

**Test cases for existing functionality:**
- [x] Draw on layer 1, undo → stroke removed from layer 1
- [x] Draw on layer 2, undo → stroke removed from layer 2
- [x] Draw on multiple layers, undo each → correct layer affected
- [x] Delete layer, undo stroke that was on it → layer auto-recreated

---

**⬜ NOT IMPLEMENTED: Layer structural operations**

The following layer operations are **NOT undoable**:

| Operation | Undo Behavior Needed | Complexity |
|-----------|---------------------|------------|
| Add Layer | Remove the added layer | Low |
| Remove Layer | Restore layer with ALL strokes | High - need to save all strokes |
| Merge Layers | Restore original separate layers | High - need to save source layers |
| Duplicate Layer | Remove the copy | Low |
| Rename Layer | Restore old name | Low |
| Reorder Layer | Restore old position | Low |

**Why layer undo is complex:**
- Layer operations affect the manifest (edgeless) or page structure
- Remove/Merge need to save entire VectorLayer data for restoration
- Need a new undo action type separate from stroke undo
- Need to coordinate between LayerPanel (UI) and DocumentViewport (undo stacks)

---

#### Recommended Approach (If Implementing)

**Option A: Minimal - Low priority operations only**
- Skip Remove/Merge undo (these are destructive, users expect "are you sure?")
- Implement Add/Duplicate undo (just remove the layer)
- Implement Rename/Reorder undo (just store old values)

**Option B: Full implementation**
Would require significant changes:
1. New `LayerUndoAction` struct with layer data
2. Deep-copy VectorLayer for Remove/Merge
3. Coordinate manifest vs tile layer data in edgeless mode

---

#### Decision: DEFER

Given that:
1. Stroke undo already works correctly across layers
2. Layer operations are infrequent compared to strokes
3. Full layer undo requires significant additional complexity
4. Users can avoid data loss by:
   - Not removing layers that have content
   - Saving before merge (merge is explicitly requested)

**Recommendation:** Mark Phase 5.7 as "DEFERRED" and move to Phase 5.8 (Polish & Testing).

If layer undo is needed later, implement Option A first (low-complexity operations only).

---

**Current test cases:**
- [x] Stroke undo uses correct layer
- [x] Stroke redo uses correct layer  
- [x] Undo after layer deletion auto-recreates layer
- [ ] ~~Ctrl+Z after add layer removes it~~ (DEFERRED)
- [ ] ~~Ctrl+Z after remove layer restores it~~ (DEFERRED)
- [ ] ~~Ctrl+Z after merge restores original layers~~ (DEFERRED)
- [ ] ~~Ctrl+Z after duplicate removes the copy~~ (DEFERRED)

---

#### Merge/Duplicate Layer Code Review (Dec 29, 2024)

| ID | Issue | Severity | Fix |
|----|-------|----------|-----|
| CR-L10 | Eraser failed on merged strokes | High (Bug) | `onMergeClicked()` now emits `activeLayerChanged(targetIndex)` to sync viewport's active layer index after merge. The viewport's `m_edgelessActiveLayerIndex` was stale, pointing to a removed layer. |
| CR-L12 | Active layer index not adjusted after removing lower-indexed layers | Medium (Bug) | In `removeEdgelessLayer()` and `mergeEdgelessLayers()`, now properly decrements `m_edgelessActiveLayerIndex` when removing layers below it. Previously only clamped to max size. |
| CR-L13 | Merge/delete layer caused data loss for evicted tiles | Critical (Data Loss) | Added `loadAllEvictedTiles()` helper that loads all tiles from disk before destructive layer operations. This ensures strokes on removed/merged layers are properly handled on ALL tiles, not just the ones currently in memory. May be slow with many evicted tiles, but ensures data consistency. |
| CR-L14 | Empty tiles not deleted after layer removal | Medium (Storage) | After removing layers in `removeEdgelessLayer()` and `mergeEdgelessLayers()`, now calls `removeTileIfEmpty()` for each tile. Tiles that become completely empty (no strokes on any layer) are removed from memory and marked for deletion on next save. |

**All issues fixed.**

**Files modified:**
- `source/core/Document.h` - Added `loadAllEvictedTiles()` declaration, updated docs
- `source/core/Document.cpp` - Implemented `loadAllEvictedTiles()`, fixed active layer adjustment, added calls before remove/merge, added empty tile cleanup after layer removal
- `source/ui/LayerPanel.cpp` - Already fixed CR-L10 (emit `activeLayerChanged` after merge)

---

### Phase 5.8: Polish & Testing ⬜ NOT STARTED

**Goal:** Final testing and edge case handling.

**Tasks:**
1. Test all layer operations in paged mode
2. Test all layer operations in edgeless mode
3. Test with large documents (many pages/tiles)
4. Test save/load preserves all layer data
5. Performance testing with many layers
6. Code review for memory leaks, crashes

**Test cases:**
- [ ] 10+ layers on single page
- [ ] Different layer counts on different pages
- [ ] Edgeless with 100+ tiles and 5 layers
- [ ] Rapid layer switching during drawing
- [ ] Layer operations during active stroke (edge case)

---

## Success Criteria

### Phase 5.1 Complete When:
- [ ] LayerPanel visible in UI
- [ ] Layer selection changes drawing target
- [ ] Layer visibility affects rendering
- [ ] Tab/page changes update LayerPanel

### Phase 5.2 Complete When:
- [ ] Double-click renames layer
- [ ] Name persists in save/load

### Phase 5.3 Complete When:
- [ ] Checkboxes allow multi-selection
- [ ] Selection enables/disables merge button

### Phase 5.4 Complete When:
- [ ] Merge combines selected layers
- [ ] Strokes preserved correctly

### Phase 5.5 Complete When:
- [ ] Duplicate creates copy with new IDs
- [ ] Original unchanged

### Phase 5.6 Complete When:
- [ ] Edgeless uses manifest for layer definitions
- [ ] Add layer = 1 disk write
- [ ] Tiles lazy-update layer structure

### Phase 5.7 Complete When:
- [ ] All layer operations undoable
- [ ] Redo works correctly

### Phase 5.8 Complete When:
- [ ] All test cases pass
- [ ] No memory leaks or crashes
- [ ] Code review complete

---

## Dependencies

| Phase | Depends On | Status |
|-------|------------|--------|
| 5.1 | None | ✅ Complete |
| 5.2 | 5.1 | ✅ Complete |
| 5.6 | 5.1, 5.2 | ✅ Complete |
| 5.3 | 5.6 (benefits from manifest) | ✅ Complete |
| 5.4 | 5.3 (needs selection UI) | ⬜ **Next** |
| 5.5 | 5.6 (benefits from manifest) | ⬜ Pending |
| 5.7 | 5.3-5.6 (needs operations) | ⬜ Pending |
| 5.8 | 5.1-5.7 (final testing) | ⬜ Pending |

**Revised order:** 5.1 ✅ → 5.2 ✅ → **5.6** → 5.5 → 5.3 → 5.4 → 5.7 → 5.8

**Rationale for revised order:**
- Phase 5.6 fundamentally changes edgeless layer architecture (manifest-based)
- Implementing 5.3/5.4 now would require refactoring after 5.6
- Doing 5.6 first provides:
  - O(1) layer operations (no tile iteration)
  - Clean separation of concerns (Document owns layer structure)
  - Simpler MainWindow code
  - No origin tile special handling
- 5.5 (Duplicate) benefits from manifest because duplicating a layer = 1 manifest write

---

## Notes

- Opacity and Lock features deferred to future phase
- Drag-and-drop reordering deferred (using buttons for now)
- Layer blend modes not in scope
- Phase 5.6 includes migration for old .snb files without manifest layers

---

*Subplan for Phase 5 LayerPanel Integration - Dec 28, 2024*

