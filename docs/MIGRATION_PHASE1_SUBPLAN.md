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

### Task 1.1.2: Extract Stroke Types to Separate Files
Move `StrokePoint` and `VectorStroke` from `VectorCanvas.h` to their own files.

**Files to create:**
- `source/strokes/StrokePoint.h`
- `source/strokes/VectorStroke.h`

**Content (from VectorCanvas.h lines 17-117):**
```cpp
// StrokePoint.h
struct StrokePoint {
    QPointF pos;
    qreal pressure;
    QJsonObject toJson() const;
    static StrokePoint fromJson(const QJsonObject& obj);
};

// VectorStroke.h  
struct VectorStroke {
    QString id;
    QVector<StrokePoint> points;
    QColor color;
    qreal baseThickness;
    QRectF boundingBox;
    
    void updateBoundingBox();
    bool containsPoint(const QPointF& point, qreal tolerance) const;
    QJsonObject toJson() const;
    static VectorStroke fromJson(const QJsonObject& obj);
};
```

**Dependencies:** None
**Estimated size:** ~120 lines total (extraction, no new logic)
**Note:** VectorCanvas.h will #include these instead of defining them inline

---

### Task 1.1.3: Create VectorLayer Class
A single layer containing strokes. Based on VectorCanvas but:
- No widget (just data + rendering helper)
- No input handling (that's Viewport's job)
- Has: name, visibility, opacity, locked state

**File:** `source/layers/VectorLayer.h` and `.cpp`

```cpp
class VectorLayer {
public:
    QString name = "Layer 1";
    bool visible = true;
    qreal opacity = 1.0;
    bool locked = false;
    
    // Stroke management
    void addStroke(const VectorStroke& stroke);
    void removeStroke(const QString& id);
    const QVector<VectorStroke>& strokes() const;
    
    // Rendering (no caching - Page handles that)
    void render(QPainter& painter) const;
    
    // Hit testing (for eraser)
    QVector<QString> strokesAtPoint(QPointF pt, qreal tolerance) const;
    
    // Serialization
    QJsonObject toJson() const;
    static VectorLayer fromJson(const QJsonObject& obj);
    
private:
    QVector<VectorStroke> m_strokes;
};
```

**Dependencies:** Task 1.1.2 (StrokePoint, VectorStroke)
**Estimated size:** ~200 lines
**Reuses:** Rendering logic from VectorCanvas::renderStroke()

---

### Task 1.1.4: Create InsertedObject Base Class
Abstract base for all insertable objects.

**File:** `source/objects/InsertedObject.h` and `.cpp`

```cpp
class InsertedObject {
public:
    QString id;           // UUID
    QPointF position;     // Top-left position on page
    QSizeF size;          // Bounding size
    int zOrder = 0;       // Stacking order (higher = on top)
    bool locked = false;
    bool visible = true;
    
    virtual ~InsertedObject() = default;
    
    // Pure virtual - subclasses implement
    virtual void render(QPainter& painter, qreal zoom) const = 0;
    virtual QString type() const = 0;  // "image", "text", etc.
    virtual QJsonObject toJson() const;
    virtual bool containsPoint(QPointF pt) const;
    
    // Factory method for deserialization
    static std::unique_ptr<InsertedObject> fromJson(const QJsonObject& obj);
    
    // Common helpers
    QRectF boundingRect() const { return QRectF(position, size); }
};
```

**Dependencies:** None
**Estimated size:** ~80 lines

---

### Task 1.1.5: Create ImageObject Class
First concrete InsertedObject type.

**File:** `source/objects/ImageObject.h` and `.cpp`

```cpp
class ImageObject : public InsertedObject {
public:
    QString imagePath;     // Path to image file (relative to notebook)
    QString imageHash;     // For deduplication
    QPixmap cachedPixmap;  // Cached for rendering
    
    void render(QPainter& painter, qreal zoom) const override;
    QString type() const override { return "image"; }
    QJsonObject toJson() const override;
    
    // Load image from path
    bool loadImage(const QString& basePath);
    
    static std::unique_ptr<ImageObject> fromJson(const QJsonObject& obj);
};
```

**Dependencies:** Task 1.1.4 (InsertedObject)
**Estimated size:** ~100 lines
**Reuses:** Logic from PictureWindow (simplified)

---

### Task 1.1.6: Create Page Class (Finally!)
Now Page is just a coordinator, not a monster.

**File:** `source/core/Page.h` and `.cpp`

```cpp
class Page {
public:
    // Identity
    int pageIndex = 0;
    QSizeF size;          // Page dimensions
    bool modified = false;
    
    // Background
    enum class BackgroundType { None, PDF, Custom, Grid, Lines };
    BackgroundType backgroundType = BackgroundType::None;
    int pdfPageNumber = -1;           // If BackgroundType::PDF
    QPixmap customBackground;          // If BackgroundType::Custom
    QColor backgroundColor = Qt::white;
    int gridDensity = 20;
    
    // Layers (ordered, index 0 = bottom)
    QVector<std::unique_ptr<VectorLayer>> vectorLayers;
    int activeLayerIndex = 0;
    
    // Inserted objects
    QVector<std::unique_ptr<InsertedObject>> objects;
    
    // Layer management
    VectorLayer* activeLayer();
    void addLayer(const QString& name = "New Layer");
    void removeLayer(int index);
    void moveLayer(int from, int to);
    
    // Object management
    void addObject(std::unique_ptr<InsertedObject> obj);
    void removeObject(const QString& id);
    InsertedObject* objectAtPoint(QPointF pt);
    
    // Rendering (for export/preview - Viewport handles live rendering)
    void render(QPainter& painter, const QPixmap* pdfBackground = nullptr) const;
    
    // Serialization
    QJsonObject toJson() const;
    static std::unique_ptr<Page> fromJson(const QJsonObject& obj);
    
    // Factory
    static std::unique_ptr<Page> createDefault(QSizeF size);
};
```

**Dependencies:** Tasks 1.1.3, 1.1.4, 1.1.5
**Estimated size:** ~250 lines
**Note:** Page is a data container + coordinator. No rendering cache (Viewport does that).

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
| 1.1.2 | Extract StrokePoint/VectorStroke | None | 120 | [ ] |
| 1.1.3 | Create VectorLayer | 1.1.2 | 200 | [ ] |
| 1.1.4 | Create InsertedObject base | None | 80 | [ ] |
| 1.1.5 | Create ImageObject | 1.1.4 | 100 | [ ] |
| 1.1.6 | Create Page class | 1.1.3, 1.1.4, 1.1.5 | 250 | [ ] |
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
