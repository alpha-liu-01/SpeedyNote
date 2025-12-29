# Phase 5: LayerPanel Integration Subplan

> **Purpose:** Multi-layer support with LayerPanel UI integration
> **Created:** Dec 28, 2024
> **Status:** 🔄 NOT STARTED

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
- LayerPanel not placed in MainWindow
- LayerPanel not connected to DocumentViewport
- Layer rename (double-click to edit)
- Select/tick UI for batch operations
- Merge layers
- Duplicate layer
- Edgeless global layer manifest
- Multi-layer undo integration

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

### Phase 5.1: LayerPanel Integration ⬜ NOT STARTED

**Goal:** Connect existing LayerPanel to MainWindow and DocumentViewport.

**Tasks:**
1. Add `LayerPanel* m_layerPanel` member to MainWindow
2. Create LayerPanel in MainWindow constructor
3. Place below left sidebar (adjust layout)
4. Connect `TabManager::currentViewportChanged` → update LayerPanel's page
5. Connect `DocumentViewport::currentPageChanged` → update LayerPanel's page
6. Connect `LayerPanel::layerVisibilityChanged` → `viewport->update()`
7. Connect `LayerPanel::activeLayerChanged` → update drawing target
8. Connect `LayerPanel::layerAdded/Removed/Moved` → mark document modified
9. Handle page change: clamp `activeLayerIndex` if needed
10. Add `m_edgelessActiveLayerIndex` to DocumentViewport for edgeless mode

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

---

### Phase 5.2: Layer Rename ⬜ NOT STARTED

**Goal:** Double-click layer name to edit inline.

**Tasks:**
1. Enable `QListWidget` item editing on double-click
2. Connect `itemChanged` signal to update `VectorLayer::name`
3. Emit signal for document modified
4. Ensure serialization saves updated name

**Files to modify:**
- `source/ui/LayerPanel.cpp` - Add double-click edit support

**Test cases:**
- [ ] Double-click layer opens inline editor
- [ ] Enter commits new name
- [ ] Escape cancels edit
- [ ] Name persists after save/load

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

**Goal:** Store layer definitions in edgeless document manifest.

**Current:** Each tile stores full layer info (id, name, visible, etc.)
**New:** Manifest stores layer definitions; tiles only store strokes.

**Tasks:**
1. Add `layerDefinitions` to Document for edgeless mode
2. Update `Document::saveBundle()`:
   - Write layer definitions to `document.json`
   - Tiles only save layers with strokes
3. Update `Document::loadBundle()`:
   - Load layer definitions from manifest
   - When loading tile, create empty layers from manifest
4. Update add/remove/rename layer:
   - Modify manifest
   - Don't touch tiles (lazy update)
5. Sync active layer globally in DocumentViewport

**Files to modify:**
- `source/core/Document.h` - Add layerDefinitions for edgeless
- `source/core/Document.cpp` - Update save/load bundle
- `source/core/DocumentViewport.cpp` - Global active layer for edgeless

**Manifest format:**
```json
{
  "format": "speedynote_edgeless",
  "version": 1,
  "layers": [
    {"id": "uuid1", "name": "Sketch", "visible": true, "opacity": 1.0, "locked": false},
    {"id": "uuid2", "name": "Ink", "visible": true, "opacity": 1.0, "locked": false}
  ],
  "active_layer_index": 0,
  "tile_index": ["0,0", "1,0"]
}
```

**Tile format (updated):**
```json
{
  "coord": [0, 0],
  "layers": {
    "uuid1": {"strokes": [...]},
    "uuid2": {"strokes": [...]}
  }
}
```

**Test cases:**
- [ ] Add layer = 1 disk write (manifest only)
- [ ] Tiles without layer content don't store that layer
- [ ] Loading reconstructs empty layers from manifest
- [ ] Layer visibility synced across all tiles
- [ ] Active layer selection applies globally

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

| Phase | Depends On |
|-------|------------|
| 5.1 | None (can start immediately) |
| 5.2 | 5.1 (needs LayerPanel in UI) |
| 5.3 | 5.1 (needs LayerPanel in UI) |
| 5.4 | 5.3 (needs selection UI) |
| 5.5 | 5.1 (needs LayerPanel in UI) |
| 5.6 | 5.1 (needs basic layer ops working) |
| 5.7 | 5.1-5.5 (needs all operations implemented) |
| 5.8 | 5.1-5.7 (final testing) |

**Recommended order:** 5.1 → 5.2 → 5.5 → 5.3 → 5.4 → 5.6 → 5.7 → 5.8

---

## Notes

- Opacity and Lock features deferred to future phase
- Drag-and-drop reordering deferred (using buttons for now)
- Layer blend modes not in scope

---

*Subplan for Phase 5 LayerPanel Integration - Dec 28, 2024*

