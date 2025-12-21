# Phase 1 Detailed Subplan: New Data Structures

> **Purpose:** Break down Phase 1 into manageable tasks with clear dependencies.
> **Goal:** Create Page, Document, DocumentViewport with proper modularity.

---

## Directory Structure (New)

```
source/
├── core/                    ← NEW: Core document classes
│   ├── Page.h / Page.cpp
│   ├── Document.h / Document.cpp
│   └── DocumentViewport.h / DocumentViewport.cpp
│
├── layers/                  ← NEW: Layer system
│   └── VectorLayer.h / VectorLayer.cpp
│
├── objects/                 ← NEW: Inserted objects
│   ├── InsertedObject.h / InsertedObject.cpp
│   └── ImageObject.h / ImageObject.cpp
│
├── strokes/                 ← NEW: Stroke data (extracted from VectorCanvas)
│   ├── VectorStroke.h       (move from VectorCanvas.h)
│   └── StrokePoint.h        (move from VectorCanvas.h)
│
└── (existing files stay for now)
```

---

## Phase 1.1: Create Page Class (Broken Down)

### Task 1.1.1: Create Directory Structure ✅ COMPLETE
- [x] Create `source/core/` directory
- [x] Create `source/layers/` directory
- [x] Create `source/objects/` directory
- [x] Create `source/strokes/` directory
- [x] Update CMakeLists.txt to include new directories

**Dependencies:** None
**Estimated size:** ~10 lines CMake changes
**Completed:** Added 4 directories + CMakeLists.txt sections with placeholders

---

### Task 1.1.2: Extract Stroke Types to Separate Files ✅ COMPLETE
Move `StrokePoint` and `VectorStroke` from `VectorCanvas.h` to their own files.

**Files created:**
- `source/strokes/StrokePoint.h` ✅
- `source/strokes/VectorStroke.h` ✅

**Changes made:**
- Extracted `StrokePoint` struct with full documentation
- Extracted `VectorStroke` struct with full documentation  
- Updated `VectorCanvas.h` to `#include` the new files
- All functionality preserved (toJson, fromJson, containsPoint, updateBoundingBox, distanceToSegment)
- Added Doxygen-style documentation comments

**Dependencies:** None
**Actual size:** ~160 lines total (with added documentation)
**Note:** Header-only implementation (no .cpp files needed)

---

### Task 1.1.3: Create VectorLayer Class ✅ COMPLETE
A single layer containing strokes. Based on VectorCanvas but:
- No widget (just data + rendering helper)
- No input handling (that's Viewport's job)
- Has: name, visibility, opacity, locked state

**File created:** `source/layers/VectorLayer.h` ✅ (header-only)

**Features implemented:**
- Layer properties: id, name, visible, opacity, locked
- Stroke management: addStroke (copy & move), removeStroke, strokes(), clear()
- Hit testing: strokesAtPoint(), boundingBox()
- Rendering: render(), static renderStroke() helper
- Serialization: toJson(), fromJson()
- Full Doxygen documentation

**Dependencies:** Task 1.1.2 (StrokePoint, VectorStroke)
**Actual size:** ~280 lines (with documentation)
**Reuses:** Rendering logic from VectorCanvas::renderStroke() as static helper

---

### Task 1.1.4: Create InsertedObject Base Class ✅ COMPLETE
Abstract base for all insertable objects.

**Files created:**
- `source/objects/InsertedObject.h` ✅
- `source/objects/InsertedObject.cpp` ✅

**Features implemented:**
- Common properties: id, position, size, zOrder, locked, visible, rotation
- Pure virtual: render(), type()
- Virtual with base impl: toJson(), loadFromJson(), containsPoint()
- Helpers: boundingRect(), setBoundingRect(), center(), moveBy()
- Factory method: fromJson() (creates subclasses by type)
- Full Doxygen documentation

**Dependencies:** None
**Actual size:** ~150 lines header + ~80 lines cpp
**Note:** InsertedObject.cpp includes ImageObject.h - won't compile until 1.1.5 done

---

### Task 1.1.5: Create ImageObject Class ✅ COMPLETE
First concrete InsertedObject type.

**Files created:**
- `source/objects/ImageObject.h` ✅
- `source/objects/ImageObject.cpp` ✅

**Features implemented:**
- Image properties: imagePath, imageHash, maintainAspectRatio, originalAspectRatio
- Rendering: render() with zoom and rotation support
- Loading: loadImage(), setPixmap(), unloadImage()
- Aspect ratio: resizeToWidth(), resizeToHeight()
- Deduplication: calculateHash() (SHA-256)
- Path handling: fullPath() (resolves relative paths)
- Serialization: toJson(), loadFromJson()
- Full Doxygen documentation

**Dependencies:** Task 1.1.4 (InsertedObject)
**Actual size:** ~130 lines header + ~160 lines cpp
**Reuses:** Concepts from PictureWindow (simplified)

---

### Task 1.1.6: Create Page Class (Finally!) ✅ COMPLETE
Now Page is just a coordinator, not a monster.

**Files created:**
- `source/core/Page.h` ✅
- `source/core/Page.cpp` ✅

**Features implemented:**
- Identity: pageIndex, size, modified
- Background: 5 types (None, PDF, Custom, Grid, Lines) with colors/spacing
- Layer management: activeLayer(), addLayer(), removeLayer(), moveLayer()
- Object management: addObject(), removeObject(), objectAtPoint(), objectById()
- Rendering: render(), renderBackground() with zoom support
- Serialization: toJson(), fromJson(), loadImages()
- Factory: createDefault(), createForPdf()
- Utility: hasContent(), clearContent(), contentBoundingRect()
- Full Doxygen documentation

**Dependencies:** Tasks 1.1.3, 1.1.4, 1.1.5
**Actual size:** ~260 lines header + ~380 lines cpp
**Note:** Page is a pure data coordinator - Viewport handles caching.

---

### Task 1.1.7: Unit Tests
Simple tests to verify serialization round-trip.

**File:** `tests/test_page.cpp` (or inline test function)

```cpp
void testPageSerialization() {
    // Create page with layers and objects
    auto page = Page::createDefault(QSizeF(800, 600));
    page->addLayer("Layer 2");
    page->activeLayer()->addStroke(testStroke);
    
    auto img = std::make_unique<ImageObject>();
    img->position = QPointF(100, 100);
    page->addObject(std::move(img));
    
    // Serialize
    QJsonObject json = page->toJson();
    
    // Deserialize
    auto restored = Page::fromJson(json);
    
    // Verify
    assert(restored->vectorLayers.size() == 2);
    assert(restored->objects.size() == 1);
}
```

**Dependencies:** All above tasks
**Estimated size:** ~50 lines

---

## Task Summary Table

| Task | Description | Dependencies | Est. Lines | Status |
|------|-------------|--------------|------------|--------|
| 1.1.1 | Directory structure | None | 10 | [✅] |
| 1.1.2 | Extract StrokePoint/VectorStroke | None | 120 | [✅] |
| 1.1.3 | Create VectorLayer | 1.1.2 | 200 | [✅] |
| 1.1.4 | Create InsertedObject base | None | 80 | [✅] |
| 1.1.5 | Create ImageObject | 1.1.4 | 100 | [✅] |
| 1.1.6 | Create Page class | 1.1.3, 1.1.4, 1.1.5 | 250 | [✅] |
| 1.1.7 | Unit tests | All above | 50 | [ ] |
| **TOTAL** | | | **~810** | |

---

## Execution Order

```
1.1.1 (directories)
    ↓
┌───┴───┐
1.1.2   1.1.4
(strokes) (InsertedObject)
    ↓       ↓
1.1.3   1.1.5
(VectorLayer) (ImageObject)
    ↓       ↓
    └───┬───┘
        ↓
      1.1.6
      (Page)
        ↓
      1.1.7
      (tests)
```

Tasks 1.1.2 and 1.1.4 can be done in **parallel**.
Tasks 1.1.3 and 1.1.5 can be done in **parallel**.

---

## Notes for Implementation

1. **Don't break VectorCanvas yet** - New files are additions, not replacements
2. **Include guards** - Use `#pragma once` for simplicity
3. **Smart pointers** - Use `std::unique_ptr` for ownership
4. **Qt parent-child** - NOT used for data classes (only for widgets)
5. **Edgeless canvas ready** - Page doesn't assume fixed size; coordinates can be negative

---

## After Phase 1.1

Once Page is complete:
- [ ] Phase 1.2: Create Document class (owns Pages, PDF, metadata)
- [ ] Phase 1.3: Create DocumentViewport (renders Document, handles input)
- [ ] Phase 1.4: Integration tests

---

*Subplan created for SpeedyNote viewport migration*
