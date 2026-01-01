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

2. **Objects render ABOVE strokes:** Currently `renderObjects()` is called after layer rendering, so objects always appear on top of all strokes. There's no interleaving or z-order between strokes and objects.

3. **No "Link" object type yet:** The plans mention "link inserts" that navigate to another location. This would be a new `LinkObject` subclass of `InsertedObject`.

4. **Rendering uses zoom parameter:** Both Page and ImageObject rendering accept a zoom parameter, allowing proper scaling.

---

## Architectural Decisions

### AD-1: Cross-Tile Object Rendering in Edgeless Mode

**Problem:** Objects are stored in a single tile (the tile containing their top-left position), but large objects may extend across tile boundaries. The current rendering approach renders each tile independently, causing objects to be clipped when adjacent tiles render their backgrounds/strokes on top.

**Example of the Problem:**
```
Object in tile (0,1) at position (900, 500) with size (300, 200)
- Object extends from x=900 to x=1200 (200px into tile (1,1))
- Tile size is 1024px

Current render order:
1. Render tile (0,1): strokes + object (object extends to x=1200) ✓
2. Render tile (1,1): background + strokes ← OVERWRITES object's extended portion!

Result: Object's right 200px is covered by tile (1,1)'s background.
```

**Solution: Two-Pass Rendering**

Separate object rendering into a dedicated pass AFTER all tile backgrounds and strokes:

```cpp
void DocumentViewport::renderEdgelessMode(QPainter& painter)
{
    // ===== PASS 1: All tile backgrounds and strokes =====
    for (each visible tile) {
        renderTileBackgroundAndStrokes(painter, tile);  // NO objects in this pass
    }
    
    // ===== PASS 2: All objects from loaded tiles =====
    for (each LOADED tile) {  // includes margin tiles beyond viewport
        for (each object in tile->objects) {
            // Convert object position from tile-local to document coordinates
            QPointF docPos = tileToDocument(object->position, tileCoord);
            
            // Check if object intersects visible viewport
            QRectF objRect(docPos, object->size);
            if (objRect.intersects(visibleDocRect)) {
                // Render at document coordinates
                painter.save();
                painter.translate(docPos);
                object->render(painter, zoom);
                painter.restore();
            }
        }
    }
}
```

**Extended Tile Loading Margin:**

To ensure objects that extend into the visible area are rendered, we must load tiles beyond the viewport:

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

**Implementation Requirements:**

| Component | Change Needed |
|-----------|---------------|
| `Document` | Track `m_maxObjectExtent` (updated when objects added/resized) |
| Tile loading | Expand loading margin by `ceil(m_maxObjectExtent / TILE_SIZE)` |
| `renderEdgelessMode()` | Split into two passes: (1) strokes, (2) objects |
| `renderTileStrokes()` | Remove `tile->renderObjects()` call |
| New method | `renderEdgelessObjects()` to iterate loaded tiles and render objects |

**Paged Mode:** No changes needed. Pages don't overlap, so objects render correctly within each page.

**Object Storage:** Unchanged. Objects remain stored in their "home" tile using tile-local coordinates. Only the rendering approach changes.

---

## What Needs to Be Built

### Minimum Viable Product (MVP)

For basic image insertion support:

1. **Insert Image mechanism** (file dialog, clipboard paste)
2. **Object selection tool** (click to select, visual feedback)
3. **Basic manipulation** (move, resize, delete)
4. **Undo/redo integration**
5. **MainWindow integration** (button, shortcuts)

### Enhanced Features (Post-MVP)

1. **LinkObject** - Navigate to page/location
2. **Advanced manipulation** - Rotation UI, z-order controls
3. **Object properties panel**
4. **Drag & drop support**
5. **Image cropping/editing**

---

## Implementation Dependencies

| Component | Depends On |
|-----------|------------|
| Object insertion | File dialog, clipboard handling |
| Object selection | New tool type, hit testing |
| Object manipulation | Selection (must select before manipulate) |
| Undo/redo | Existing undo system extension |
| MainWindow UI | All of the above |

---

*This document will be expanded with detailed implementation tasks after discussion.*
