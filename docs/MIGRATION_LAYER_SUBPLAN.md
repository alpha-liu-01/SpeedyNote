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

### Phase 5.3: Select/Tick UI ⬜ NOT STARTED

**Goal:** Add checkbox selection to layers for batch operations.

**Design:** Each layer item shows:
```
[✓] 👁 Layer Name
```
- Checkbox for selection (left)
- Eye icon for visibility (click to toggle)
- Layer name (double-click to edit)

**Tasks:**
1. Add checkbox to layer items
2. Track selected layers (can be multiple)
3. Add "Select All" / "Select None" options
4. Selection state is UI-only (not saved)
5. Update button states based on selection (merge needs 2+)

**Files to modify:**
- `source/ui/LayerPanel.h` - Add selection tracking
- `source/ui/LayerPanel.cpp` - Add checkboxes, selection logic

**Test cases:**
- [ ] Checkboxes appear on each layer
- [ ] Can select multiple layers
- [ ] Selection persists while panel is open
- [ ] Selection cleared when page changes

---

### Phase 5.4: Merge Layers ⬜ NOT STARTED

**Goal:** Merge selected layers into one.

**Behavior:**
1. User selects 2+ layers via checkboxes
2. Clicks "Merge" button
3. All strokes combined into bottom-most selected layer
4. Other selected layers removed
5. Layer order preserved for unselected layers

**Tasks:**
1. Add "Merge" button to LayerPanel
2. Implement merge logic:
   - Collect all strokes from selected layers
   - Add to target layer (lowest index among selected)
   - Remove other selected layers
3. Emit signals for undo integration
4. Refresh layer list

**Files to modify:**
- `source/ui/LayerPanel.h` - Add merge button, signal
- `source/ui/LayerPanel.cpp` - Implement merge logic
- `source/core/Page.h` - Add mergeLayers() helper if needed

**Test cases:**
- [ ] Merge button disabled with 0-1 selected
- [ ] Merge combines strokes correctly
- [ ] Layer order preserved
- [ ] Undo restores original layers (Phase 5.7)

---

### Phase 5.5: Duplicate Layer ⬜ NOT STARTED

**Goal:** Copy a layer with all its strokes.

**Tasks:**
1. Add "Duplicate" button to LayerPanel
2. Implement duplicate:
   - Deep copy all strokes (new UUIDs)
   - Create new layer with name "LayerName Copy"
   - Insert above original
3. Select the new layer

**Files to modify:**
- `source/ui/LayerPanel.h` - Add duplicate button
- `source/ui/LayerPanel.cpp` - Implement duplicate logic
- `source/layers/VectorLayer.h` - Add clone() method if needed

**Test cases:**
- [ ] Duplicate creates new layer above original
- [ ] All strokes copied with new IDs
- [ ] New layer is selected
- [ ] Original layer unchanged

---

### Phase 5.6: Edgeless Layer Manifest ⬜ NOT STARTED

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

#### Phase 5.6.1: LayerDefinition Struct & Document Members

**Add to `Document.h`:**
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

#### Phase 5.6.2: Manifest Save/Load

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

#### Phase 5.6.3: Tile Save Format (Strokes Only)

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

#### Phase 5.6.4: Tile Load with Manifest Reconstruction

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

#### Phase 5.6.5: Remove Origin Tile Special Handling

**Delete from `DocumentViewport::evictDistantTiles()`:**
```cpp
// DELETE THIS:
// Phase 5.1: Never evict the origin tile (0,0) - it's the LayerPanel representative
if (coord.first == 0 && coord.second == 0) {
    continue;
}
```

**Delete from `Document.cpp`:**
- Remove `syncTileLayerStructure()` method entirely
- Remove calls to `syncTileLayerStructure()` in `getOrCreateTile()` and `loadTileFromDisk()`

---

#### Phase 5.6.6: Update getOrCreateTile for Manifest Layers

**Update `Document::getOrCreateTile()`:**
```cpp
// When creating new tile, use manifest layer structure
if (isEdgeless()) {
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

---

#### Phase 5.6.7: LayerPanel Connection Update

**Option A: Virtual Page Adapter (Recommended)**
Create a "virtual page" that wraps Document's layer manifest:

```cpp
// In Document.h:
class EdgelessLayerAdapter {
public:
    EdgelessLayerAdapter(Document* doc) : m_doc(doc) {}
    
    int layerCount() const { return m_doc->edgelessLayerCount(); }
    VectorLayer* layer(int index);  // Returns temporary VectorLayer
    // ... other Page-like methods
    
private:
    Document* m_doc;
    std::vector<std::unique_ptr<VectorLayer>> m_tempLayers;  // For LayerPanel
};
```

**Option B: Modify LayerPanel (Alternative)**
Add `setEdgelessDocument(Document*)` to LayerPanel and have it read from `m_edgelessLayers` directly.

---

#### Phase 5.6.8: Simplify MainWindow Layer Logic

**Replace ~145 lines with:**
```cpp
// Visibility change → just update manifest
connect(m_layerPanel, &LayerPanel::layerVisibilityChanged, this, [this](int idx, bool vis) {
    if (auto* vp = currentViewport()) {
        if (auto* doc = vp->document(); doc && doc->isEdgeless()) {
            doc->setEdgelessLayerVisible(idx, vis);
            vp->update();  // Repaint - loaded tiles already have correct VectorLayers
        }
    }
});

// Layer add → just update manifest
connect(m_layerPanel, &LayerPanel::layerAdded, this, [this](int idx) {
    // LayerPanel already called doc->addEdgelessLayer() via adapter
    if (auto* vp = currentViewport()) {
        emit vp->documentModified();
        vp->update();
    }
});

// Similar simplification for remove/move/rename
```

**Net result:** ~145 lines → ~30 lines

---

#### Phase 5.6.9: Migration & Backward Compatibility

**Version detection in `loadBundle()`:**
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

- [ ] Layer operations are O(1) disk writes
- [ ] No tile iteration for layer metadata changes
- [ ] Origin tile can be evicted like any other tile
- [ ] MainWindow layer logic reduced to ~30 lines
- [ ] Old .snb files migrate transparently
- [ ] All layer features work: visibility, rename, add, remove, reorder
- [ ] Active layer selection is global and persistent

---

### Phase 5.7: Multi-Layer Undo/Redo ⬜ NOT STARTED

**Goal:** Ensure undo/redo works correctly with multiple layers.

**Current state:**
- Stroke undo already tracks layer index
- Layer operations (add/remove/merge) not undoable

**Tasks:**
1. Verify stroke undo uses correct layer
2. Add layer operation undo actions:
   - `AddLayerAction` - stores layer data for undo
   - `RemoveLayerAction` - stores removed layer for redo
   - `MergeLayersAction` - stores original layers for undo
   - `DuplicateLayerAction` - stores new layer id for undo
3. Integrate with existing undo stack

**Undo action types:**
```cpp
enum class LayerUndoType {
    AddLayer,
    RemoveLayer,
    MergeLayers,
    DuplicateLayer,
    RenameLayer,
    ReorderLayer
};

struct LayerUndoAction {
    LayerUndoType type;
    int layerIndex;
    VectorLayer savedLayer;  // For restore
    // ... additional fields per type
};
```

**Files to modify:**
- `source/core/DocumentViewport.h` - Add layer undo types
- `source/core/DocumentViewport.cpp` - Implement layer undo/redo
- `source/ui/LayerPanel.cpp` - Push undo actions for operations

**Test cases:**
- [ ] Ctrl+Z after add layer removes it
- [ ] Ctrl+Z after remove layer restores it
- [ ] Ctrl+Z after merge restores original layers
- [ ] Ctrl+Z after duplicate removes the copy
- [ ] Redo works for all layer operations

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
| 5.6 | 5.1, 5.2 | ⬜ **Next** |
| 5.5 | 5.6 (benefits from manifest) | ⬜ Pending |
| 5.3 | 5.6 (benefits from manifest) | ⬜ Pending |
| 5.4 | 5.3 (needs selection UI) | ⬜ Pending |
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

