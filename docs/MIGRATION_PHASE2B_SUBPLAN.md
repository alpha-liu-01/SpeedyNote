# Phase 2B: Additional Drawing Tools - Subplan

## Overview

Phase 2B implements three additional drawing tools that were deferred during the initial DocumentViewport migration:

1. **Marker Tool** - Fixed opacity, fixed thickness drawing tool
2. **Straight Line Mode** - Constrained line drawing (works with Pen/Marker)
3. **Lasso Selection Tool** - Freeform stroke selection with transform operations

**Deferred:** Highlighter Tool (2.11) - requires PDF text box integration

## Current Progress

| Tool | Status |
|------|--------|
| 2.8 Marker Tool | ✅ **COMPLETE** |
| 2.9 Straight Line Mode | ✅ **COMPLETE** |
| 2.10 Lasso Selection Tool | ⏳ Pending |

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

#### 2.8.3 Stroke Rendering with Opacity (~30 lines) ✅ COMPLETE
**File:** `source/core/DocumentViewport.cpp`

**Issue Found:** Incremental stroke rendering drew each segment with semi-transparent color, causing overlapping segments at joints to compound alpha (in-progress strokes appeared ~75% opaque while finished strokes were 50%).

**Fix Applied:** Modified `renderCurrentStrokeIncremental()` to:
1. Detect semi-transparent strokes (alpha < 255)
2. Draw with FULL OPACITY to the cache (prevents alpha compounding)
3. Apply the stroke's opacity only when blitting the cache to viewport

```cpp
// Key fix: Draw opaque to cache, apply alpha on final blit
if (hasSemiTransparency) {
    drawColor.setAlpha(255);  // Draw opaque to cache
}
// ... draw segments ...
if (hasSemiTransparency) {
    painter.setOpacity(strokeAlpha / 255.0);  // Apply alpha on blit
}
painter.drawPixmap(0, 0, m_currentStrokeCache);
```

#### 2.8.4 MainWindow Integration (~50 lines) ✅ ALREADY COMPLETE
**File:** `source/MainWindow.cpp`

**Pre-existing:** The marker button was already connected during the original Phase 2 tool infrastructure setup:
- `markerToolButton` created and styled (line 717-720)
- Connected to `MainWindow::setMarkerTool()` (line 721)
- `setMarkerTool()` calls `vp->setCurrentTool(ToolType::Marker)` (line 1918-1921)
- Button state updates in `updateToolButtonSelection()` (line 1963-1965)

**Future interface for marker color picker:**
```cpp
// void MainWindow::onMarkerColorChanged(const QColor& color) {
//     if (auto* vp = currentViewport()) {
//         vp->setMarkerColor(color);
//     }
// }
```

### Test Cases
- [x] Marker strokes render with 50% opacity
- [x] Marker thickness is consistent (no pressure variation)
- [x] Marker color is separate from pen color
- [x] Works in both paged and edgeless modes
- [x] Marker strokes saved/loaded correctly (alpha preserved)

### 2.8 Status: ✅ COMPLETE
All marker tool tasks have been implemented. The marker now has:
- Fixed 50% opacity (consistent during drawing and after completion)
- Fixed 8.0 thickness (no pressure variation)
- Separate color from pen (#E6FF6E default, customizable via `setMarkerColor()`)
- Works in both paged and edgeless modes

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

#### 2.9.1 Mode Toggle (~20 lines) ✅ COMPLETE
**File:** `source/core/DocumentViewport.h/.cpp`

**Implemented:**
- `void setStraightLineMode(bool enabled)` - setter with signal emission
- `bool straightLineMode() const` - inline getter
- Signal `straightLineModeChanged(bool enabled)` - for UI sync
- Member variables:
  - `m_straightLineMode` - toggle state
  - `m_isDrawingStraightLine` - drawing state
  - `m_straightLineStart` / `m_straightLinePreviewEnd` - coordinates
  - `m_straightLinePageIndex` - for paged mode

#### 2.9.2 Modified Stroke Creation (~60 lines) ✅ COMPLETE
**File:** `source/core/DocumentViewport.cpp`

**Implemented in `handlePointerPress()`:**
- Intercepts Pen/Marker tools when `m_straightLineMode` is enabled
- Records start point (document coords for edgeless, page coords for paged)
- Sets `m_isDrawingStraightLine = true`

**Implemented in `handlePointerMove()`:**
- Updates `m_straightLinePreviewEnd` while drawing
- Handles coordinate extrapolation when pointer moves off original page

**Implemented in `handlePointerRelease()`:**
- Gets final end point
- Calls `createStraightLineStroke()` to create the actual stroke
- Clears straight line state

#### 2.9.3 Straight Line Stroke Creation (~50 lines) ✅ COMPLETE
**File:** `source/core/DocumentViewport.cpp`

**Implemented `createStraightLineStroke()`:**
- Uses Marker or Pen color/thickness based on current tool
- Creates stroke with two points (pressure = 1.0)
- **Paged mode:** Adds directly to page's active layer with undo support
- **Edgeless mode:**
  - If line is within one tile: adds directly
  - If line crosses tiles: samples points along the line (~10px spacing), splits at tile boundaries using same algorithm as freehand strokes
  - Full undo support (all segments as one atomic action)

#### 2.9.4 Preview Line Rendering (~40 lines) ✅ COMPLETE
**File:** `source/core/DocumentViewport.cpp`

**Implemented:** Added straight line preview rendering in two locations:
1. **Paged mode paint section** (after current stroke rendering)
   - Converts page-local coordinates to viewport coordinates
2. **Edgeless mode paint section** (`renderEdgelessMode()`)
   - Converts document coordinates to viewport coordinates

**Features:**
- Uses Marker or Pen color/thickness based on current tool
- Proper antialiasing and round caps
- Scales thickness by zoom level for consistent visual appearance

#### 2.9.5 MainWindow Integration (~30 lines) ✅ COMPLETE
**File:** `source/MainWindow.cpp`

**Implemented:**
1. **Button click handler** (replaced stub at line 747):
   - Toggles `vp->straightLineMode()` on current viewport
   - Updates button visual state ("selected" property)

2. **Viewport change sync** (added to `currentViewportChanged` handler):
   - Syncs button state when switching tabs
   - Updates visual appearance to match new viewport's straight line mode

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
- [x] Toggle straight line mode on/off
- [x] Preview line shows while dragging
- [x] Final stroke matches preview
- [x] Works with Pen tool
- [x] Works with Marker tool (correct color/opacity)
- [x] Works in paged mode
- [x] Works in edgeless mode (tile splitting)
- [x] Undo/redo works for straight line strokes

### 2.9 Status: ✅ COMPLETE
All straight line mode tasks have been implemented:
- Toggle via `straightLineToggleButton` in MainWindow
- Works with both Pen and Marker tools
- Preview line shows while dragging
- Proper tile splitting for edgeless mode
- Full undo/redo support

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

#### 2.10.1 Lasso Path Drawing (~50 lines) ✅ COMPLETE
**File:** `source/core/DocumentViewport.cpp`

**Implemented:**
1. **Data structures** in `DocumentViewport.h`:
   - `LassoSelection` struct with strokes, indices, transform state
   - `m_lassoPath` (QPolygonF) for the drawing path
   - `m_isDrawingLasso` flag

2. **Handler methods:**
   - `handlePointerPress_Lasso()` - starts lasso path, clears existing selection
   - `handlePointerMove_Lasso()` - adds points with decimation
   - `handlePointerRelease_Lasso()` - finalizes path (selection in 2.10.2)
   - `clearLassoSelection()` - clears all lasso state

3. **Integration:**
   - Added `ToolType::Lasso` handling in `handlePointerPress/Move/Release`

4. **Rendering:**
   - Lasso path preview with dashed blue line and light fill
   - Works in both paged and edgeless modes

#### 2.10.2 Stroke Hit Detection (~60 lines) ✅ COMPLETE
**File:** `source/core/DocumentViewport.cpp`

**Implemented:**
1. **`finalizeLassoSelection()`** - Finds strokes inside lasso path:
   - **Paged mode:** Checks strokes on active layer of source page
   - **Edgeless mode:** Iterates all loaded tiles, transforms stroke points from tile-local to document coordinates for hit testing
   - Stores selected strokes, original indices, source location info
   - Calculates bounding box and transform origin

2. **`strokeIntersectsLasso()`** - Checks if any stroke point is inside lasso polygon using Qt's `containsPoint()` with OddEvenFill

3. **`calculateSelectionBoundingBox()`** - Computes unified bounding rect of all selected strokes

#### 2.10.3 Selection Rendering (~80 lines) ✅ COMPLETE
**File:** `source/core/DocumentViewport.cpp`

**Implemented:**
1. **`renderLassoSelection()`** - Renders selected strokes with transforms:
   - Applies current transform (offset, scale, rotation) via `buildSelectionTransform()`
   - Transforms each stroke's points before rendering
   - Handles coordinate conversion for both paged and edgeless modes
   - Calls `drawSelectionBoundingBox()` for visual feedback

2. **`drawSelectionBoundingBox()`** - Draws dashed bounding box:
   - Transforms bounding box corners with current selection transform
   - Converts to viewport coordinates
   - Draws black/white dashed lines for contrast
   - Static dash offset (animation hook available for future marching ants)

3. **`buildSelectionTransform()`** - Creates transform matrix:
   - Translates to transform origin (center)
   - Applies rotation
   - Applies scale
   - Translates back and applies offset

4. **Integration:**
   - Called from `paintEvent()` (paged mode) after lasso path rendering
   - Called from `renderEdgelessMode()` (edgeless mode) after lasso path rendering

#### 2.10.4 Transform Handles (~60 lines) ✅ COMPLETE
**File:** `source/core/DocumentViewport.cpp`

**Implemented with touch-friendly design:**

1. **`HandleHit` enum** - Defines all handle types:
   - 8 scale handles: TopLeft, Top, TopRight, Left, Right, BottomLeft, Bottom, BottomRight
   - Rotate handle above top center
   - Inside (for move when clicking inside bounding box)

2. **Handle size constants** (touch-friendly):
   - `HANDLE_VISUAL_SIZE = 8.0` pixels (visual appearance)
   - `HANDLE_HIT_SIZE = 20.0` pixels (touch-friendly hit area)
   - `ROTATE_HANDLE_OFFSET = 25.0` pixels (rotation handle distance)

3. **`getHandlePositions()`** - Returns 9 positions (8 scale + 1 rotate)
   - Positions in document/page coordinates
   - Rotation handle offset scales with zoom for consistent visual

4. **`drawSelectionHandles()`**:
   - Draws 8 white square handles with black border (scale)
   - Draws rotation handle (circle) with connecting line
   - Small rotation indicator arrow inside circle
   - Works in both paged and edgeless modes

5. **`hitTestSelectionHandles()`**:
   - Uses larger hit area (20px) for touch-friendly interaction
   - Tests rotation handle first (highest priority)
   - Tests corner handles before edge handles
   - Tests bounding box interior last (for move)
   - Returns HandleHit enum for use in transform operations

#### 2.10.5 Transform Operations (~80 lines) ✅ COMPLETE
**File:** `source/core/DocumentViewport.cpp`

**Implemented:**

1. **Transform state members:**
   - `m_isTransformingSelection` - transform in progress flag
   - `m_transformHandle` - which handle is being dragged
   - `m_transformStartPos/DocPos` - starting positions
   - `m_transformStartBounds/Rotation/ScaleX/ScaleY/Offset` - initial state

2. **`startSelectionTransform()`** (~25 lines):
   - Stores initial transform state for delta calculations
   - Works in both viewport and document coordinates

3. **`updateSelectionTransform()`** (~45 lines):
   - **Move (Inside):** Offset by delta in document coordinates
   - **Rotate:** Calculates angle from transform origin in viewport space
   - **Scale:** Delegates to `updateScaleFromHandle()`

4. **`updateScaleFromHandle()`** (~75 lines):
   - Handles all 8 scale handles
   - Applies inverse rotation to get local coordinates
   - Calculates scale factors relative to transform origin
   - Clamps scale to 0.1-10.0 range

5. **`finalizeSelectionTransform()`** (~10 lines):
   - Clears transform state
   - Leaves visual transform applied (actual stroke modification in 2.10.6)

6. **Handler integration:**
   - `handlePointerPress_Lasso` - starts transform on handle hit
   - `handlePointerMove_Lasso` - updates transform during drag
   - `handlePointerRelease_Lasso` - finalizes transform

#### 2.10.6 Apply/Cancel Transform (~40 lines) ✅ COMPLETE
**File:** `source/core/DocumentViewport.cpp`

**Implemented:**

1. **`transformStrokePoints()`** (~5 lines):
   - Static helper to apply a QTransform to all points in a stroke
   - Updates bounding box after transformation

2. **`applySelectionTransform()`** (~100 lines):
   - **Paged mode:**
     - Removes original strokes by ID from source layer
     - Adds transformed strokes with new UUIDs
     - Invalidates stroke cache
   - **Edgeless mode:**
     - Removes original strokes from all loaded tiles by ID
     - Adds transformed strokes to appropriate tile based on bounding box center
     - Converts to tile-local coordinates
     - Marks tiles dirty

3. **`cancelSelectionTransform()`** (~3 lines):
   - Simply clears selection without applying
   - Original strokes remain untouched

4. **Handler integration:**
   - `handlePointerPress_Lasso` detects non-identity transforms
   - Calls `applySelectionTransform()` when clicking outside selection
   - Falls back to `clearLassoSelection()` if no transform applied

**Transform detection:**
```cpp
bool hasTransform = !qFuzzyIsNull(offset.x/y) ||
                    !qFuzzyCompare(scaleX/Y, 1.0) ||
                    !qFuzzyIsNull(rotation);
```

#### 2.10.7 Clipboard Operations (~50 lines) ✅ COMPLETE
**File:** `source/core/DocumentViewport.cpp`

**Implemented:**

1. **`StrokeClipboard` struct:**
   - `QVector<VectorStroke> strokes` - copied strokes (pre-transformed)
   - `bool hasContent` - clipboard state flag
   - `void clear()` - clears clipboard

2. **`copySelection()`** (~20 lines):
   - Applies current transform to selection before copying
   - Stores transformed strokes in clipboard with new UUIDs
   - Sets `hasContent = true`

3. **`cutSelection()`** (~10 lines):
   - Calls `copySelection()` then `deleteSelection()`

4. **`pasteSelection()`** (~85 lines):
   - Calculates clipboard bounding box
   - Centers pasted content at viewport center
   - **Paged mode:** Adds strokes to current page's active layer
   - **Edgeless mode:** Places strokes in appropriate tiles based on position
   - All pasted strokes get new UUIDs
   - Emits `documentModified()`

5. **`deleteSelection()`** (~55 lines):
   - **Paged mode:** Removes strokes by ID from source layer
   - **Edgeless mode:** Iterates all loaded tiles, removes matching strokes
   - Invalidates affected caches, marks tiles dirty
   - Clears selection and emits `documentModified()`

6. **`hasClipboardContent()`** - inline getter for clipboard state

#### 2.10.8 Keyboard Shortcuts (~30 lines) ✅ COMPLETE
**File:** `source/core/DocumentViewport.cpp`

**Implemented:** Added lasso shortcuts to `keyPressEvent()`:

| Shortcut | Action |
|----------|--------|
| Ctrl+C | `copySelection()` |
| Ctrl+X | `cutSelection()` |
| Ctrl+V | `pasteSelection()` |
| Delete / Backspace | `deleteSelection()` (if selection valid) |
| Escape | `cancelSelectionTransform()` (cancels drawing or clears selection) |

All shortcuts only active when `m_currentTool == ToolType::Lasso`.

#### 2.10.9 MainWindow Integration (~30 lines) ✅ COMPLETE
**File:** `source/MainWindow.cpp`, `source/core/DocumentViewport.cpp`

**Implemented:**

1. **`ropeToolButton` click handler** - Toggles between Lasso and Pen tools
2. **`updateToolButtonStates()`** - Added Lasso case to reset/select rope button
3. **`updateRopeToolButtonState()`** - Updated to properly reflect viewport state
4. **`setCurrentTool()`** - Added logic to:
   - Disable straight line mode when entering Lasso (like Eraser)
   - Apply/clear lasso selection when switching away from Lasso tool

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
| 2.8.1 Marker Tool State | [x] COMPLETE |
| 2.8.2 Marker Stroke Creation | [x] COMPLETE |
| 2.8.3 Marker Rendering | [x] COMPLETE |
| 2.8.4 Marker MainWindow Integration | [x] PRE-EXISTING |
| 2.9.1 Straight Line Toggle | [x] COMPLETE |
| 2.9.2 Straight Line Stroke Creation | [x] COMPLETE |
| 2.9.3 Straight Line for Edgeless | [x] COMPLETE |
| 2.9.4 Straight Line Preview | [x] COMPLETE |
| 2.9.5 Straight Line MainWindow Integration | [x] COMPLETE |
| 2.10.1 Lasso Path Drawing | [x] |
| 2.10.2 Stroke Hit Detection | [x] |
| 2.10.3 Selection Rendering | [x] |
| 2.10.4 Transform Handles | [x] |
| 2.10.5 Transform Operations | [x] |
| 2.10.6 Apply/Cancel Transform | [x] |
| 2.10.7 Clipboard Operations | [x] |
| 2.10.8 Keyboard Shortcuts | [x] |
| 2.10.9 Lasso MainWindow Integration | [x] |

---

## Code Review Fixes

### CR-2B-1: Eraser + Straight Line Mode Conflict
**Issue:** User could enable straight line mode while on Eraser tool, or switch to Eraser while in straight line mode, creating an invalid state.

**Fix:** 
- In `setCurrentTool()`: Auto-disable straight line mode when switching to Eraser
- In `setStraightLineMode()`: Auto-switch to Pen tool when enabling while on Eraser

**Files Modified:**
- `source/core/DocumentViewport.cpp`

### CR-2B-2: Button State Sync for Keyboard Shortcuts
**Issue:** When user changed tools via keyboard shortcuts (e.g., 'E' for eraser) in the viewport, the straight line button and tool buttons in MainWindow weren't updated.

**Fix:** 
- Added `m_toolChangedConn` and `m_straightLineModeConn` connections in `connectViewportScrollSignals()`
- When viewport emits `toolChanged` or `straightLineModeChanged`, MainWindow updates button states
- Added cleanup in destructor

**Files Modified:**
- `source/MainWindow.h` (new connection members)
- `source/MainWindow.cpp` (connections, cleanup)

### CR-2B-3: Straight Line Button Sync in updateToolButtonStates()
**Issue:** `updateToolButtonStates()` didn't sync the straight line toggle button.

**Fix:** Added straight line button state sync at end of `updateToolButtonStates()`.

**Files Modified:**
- `source/MainWindow.cpp`

### CR-2B-4: Lasso Selection Lost Immediately After Drawing
**Issue:** In paged mode, after completing a lasso selection path (releasing stylus/mouse), the selection immediately disappeared instead of entering the selected state for transformations.

**Root Cause:** In `finalizeLassoSelection()`:
1. `m_lassoSelection.sourcePageIndex` was set during `handlePointerPress_Lasso()` (line 3004)
2. `m_lassoSelection.clear()` was called at the start of `finalizeLassoSelection()`, which reset `sourcePageIndex` to -1
3. The paged mode check `if (sourcePageIndex < 0)` then returned early, never processing any strokes

**Fix:** Save `sourcePageIndex` before calling `clear()`, then restore it after:
```cpp
// BUG FIX: Save sourcePageIndex BEFORE clearing selection
int savedSourcePageIndex = m_lassoSelection.sourcePageIndex;
m_lassoSelection.clear();
m_lassoSelection.sourcePageIndex = savedSourcePageIndex;
```

**Files Modified:**
- `source/core/DocumentViewport.cpp`

### CR-2B-6: Transform Offset Applied in Wrong Coordinate Space
**Issue:** When moving a rotated/scaled selection, movement was applied BEFORE the rotation/scale transform, causing counter-intuitive behavior:
- Moving "up" on a 180° rotated selection moved it "down" on screen
- Scaled selections had proportionally scaled movement sensitivity

**Root Cause:** In Qt's `QTransform` composition, operations are applied in **reverse order** (last added = first applied to point). The original code had:
```cpp
t.translate(origin.x(), origin.y());      // Line 1
t.rotate(rotation);                        // Line 2
t.scale(scaleX, scaleY);                   // Line 3
t.translate(-origin.x(), -origin.y());    // Line 4
t.translate(offset.x(), offset.y());      // Line 5 - WRONG POSITION
```
This applied the offset FIRST (Line 5 is last, so applied first), meaning the offset got rotated/scaled along with the content.

**Fix:** Move offset to first line so it's applied LAST (after rotation/scale):
```cpp
t.translate(offset.x(), offset.y());      // Applied 5th (last) - CORRECT
t.translate(origin.x(), origin.y());      // Applied 4th
t.rotate(rotation);                        // Applied 3rd
t.scale(scaleX, scaleY);                  // Applied 2nd
t.translate(-origin.x(), -origin.y());   // Applied 1st
```

**Files Modified:**
- `source/core/DocumentViewport.cpp`

### CR-2B-7: Original Strokes Visible During Selection Transform
**Issue:** When transforming (scale/rotate) a lasso selection, the original strokes remained visible on the page, only disappearing after the transform was applied. This created visual duplication.

**Root Cause:** Layer rendering used cached pixmaps that included all strokes. During selection, the transformed copies were rendered on top, but the originals in the cache were still visible.

**Fix:** Added stroke exclusion during layer rendering:
1. Added `getSelectedIds()` helper to `LassoSelection` struct
2. Added `renderExcluding()` method to `VectorLayer` that renders strokes while skipping specified IDs
3. Modified paged mode rendering to use `renderExcluding()` for the source layer when there's an active selection
4. Modified edgeless `renderTileStrokes()` to use `renderExcluding()` for the active layer when there's a selection

**Files Modified:**
- `source/core/DocumentViewport.h` (added `getSelectedIds()`)
- `source/core/DocumentViewport.cpp` (modified paged and edgeless rendering)
- `source/layers/VectorLayer.h` (added `renderExcluding()`)

### CR-2B-8: Consecutive Transforms Referenced Original Position
**Issue:** When performing multiple transform operations in a row (e.g., move then scale), the second operation referenced the original position instead of the position after the first operation. This caused:
- Scale after move: scaling jumped back to original location
- Rotate after scale: rotation used wrong center

**Root Cause:** `startSelectionTransform()` stored `m_transformStartBounds = m_lassoSelection.boundingBox`, but `boundingBox` was always the ORIGINAL bounds from when the selection was first created. It was never updated to reflect applied transforms.

Similarly, `transformOrigin` stayed at the original center, even after the selection had been moved.

**Fix:** (Updated by CR-2B-9) "Bake in" ONLY the offset before starting a new transform:
```cpp
// Only offset is safe to bake - rotation/scale must remain cumulative
if (!m_lassoSelection.offset.isNull()) {
    m_lassoSelection.boundingBox.translate(offset);
    m_lassoSelection.transformOrigin += offset;
    for (stroke) translate points by offset;
    
    // Reset offset only - rotation and scale remain
    m_lassoSelection.offset = QPointF(0, 0);
}
```

**Files Modified:**
- `source/core/DocumentViewport.cpp`

### CR-2B-9: Rotation Lost After Baking Transform
**Issue:** After rotating a selection and then performing another operation (like scale), the bounding box snapped to axis-aligned, losing the rotation. Scaling then happened along X/Y axes instead of along the rotated axes.

Example: After rotating 45° counterclockwise, stretching "taller" should stretch along the 135° direction, not straight up.

**Root Cause:** The original CR-2B-8 fix baked in ALL transforms including rotation:
```cpp
QRectF newBox = transformedCorners.boundingRect();  // Always axis-aligned!
m_lassoSelection.rotation = 0;  // Rotation lost!
```

`boundingRect()` returns an axis-aligned rectangle, so the tilted orientation was lost.

**Fix:** Only bake in the OFFSET (pure translation), preserve rotation and scale as cumulative values. This ensures the rotated coordinate system is preserved for subsequent operations.

**Files Modified:**
- `source/core/DocumentViewport.cpp`

**Files Modified:**
- `source/core/DocumentViewport.cpp`

### CR-2B-5: Transform Handles Couldn't Be Grabbed
**Issue:** After completing a lasso selection, the transform handles were visible but couldn't be grabbed by mouse or stylus. Attempting to drag a handle would instead clear the selection and start a new lasso path.

**Root Cause:** In `handlePointerMove()` and `handlePointerRelease()`, the lasso handlers were only called when `m_isDrawingLasso` was true:
```cpp
// BUG: Only checked m_isDrawingLasso
if (m_isDrawingLasso) {
    handlePointerMove_Lasso(pe);
    return;
}
```

When the user clicks a handle:
1. `handlePointerPress_Lasso()` correctly detects the handle hit
2. `startSelectionTransform()` sets `m_isTransformingSelection = true`
3. But `m_isDrawingLasso = false` (finished drawing the path)
4. Move events never reach `handlePointerMove_Lasso()` → transform updates ignored
5. Release events never reach `handlePointerRelease_Lasso()` → transform never finalized

**Fix:** Added `m_isTransformingSelection` check to both handlers:
```cpp
// FIX: Check both drawing and transforming states
if (m_isDrawingLasso || m_isTransformingSelection) {
    handlePointerMove_Lasso(pe);
    return;
}
```

**Files Modified:**
- `source/core/DocumentViewport.cpp`
