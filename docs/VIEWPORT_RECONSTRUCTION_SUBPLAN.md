# DocumentViewport Reconstruction Subplan

> **Purpose:** Fix the fundamental rendering architecture flaw in DocumentViewport
> **Created:** Dec 26, 2024
> **Status:** 🔄 IN PROGRESS (Phase 0 complete, Phase 1-3 complete)

---

## Problem Statement

### Current Architecture (Flawed)
```
DocumentViewport (ONE QWidget)
└── paintEvent() renders EVERYTHING to ONE surface:
    ├── Background (color + PDF + grid)    ← repaints every frame
    ├── VectorLayer 1 strokes              ← repaints every frame
    ├── VectorLayer 2 strokes              ← repaints every frame
    └── Border                             ← repaints every frame
```

**Problem:** When drawing ONE stroke on Layer 2, the ENTIRE widget repaints - including the background and other layers that haven't changed.

### Correct Architecture (Goal)
```
DocumentViewport (container)
└── PageWidget (per visible page)
    ├── BackgroundWidget (opaque)          ← only repaints on zoom/settings change
    ├── LayerWidget 0 (transparent)        ← only repaints when Layer 0 changes
    ├── LayerWidget 1 (transparent)        ← only repaints when Layer 1 changes
    └── LayerWidget 2 (transparent)        ← only repaints when Layer 2 changes
```

**Solution:** Each layer is a separate QWidget. Qt only repaints widgets marked dirty. During stroke drawing, only the active LayerWidget updates.

---

## Architecture Overview

### Widget Hierarchy
```
DocumentViewport (QWidget)
│
├── PageWidget 0 (QWidget - for page 0)
│   ├── BackgroundWidget (QWidget - opaque)
│   │   └── Renders: background color + PDF/grid/lines
│   │
│   ├── LayerWidget 0 (QWidget - transparent)
│   │   └── Renders: VectorLayer 0 strokes (+ current stroke if active)
│   │
│   ├── LayerWidget 1 (QWidget - transparent)
│   │   └── Renders: VectorLayer 1 strokes (+ current stroke if active)
│   │
│   └── LayerWidget N...
│
├── PageWidget 1 (for page 1)
│   └── ...
│
└── (more PageWidgets for visible pages)
```

### Component Responsibilities

| Component | Responsibilities |
|-----------|------------------|
| **DocumentViewport** | Pan/zoom, PageWidget lifecycle, input routing, PDF cache |
| **PageWidget** | Layer widget management, active layer tracking, stroke routing |
| **BackgroundWidget** | Render background (color + PDF + grid), cache management |
| **LayerWidget** | Render VectorLayer strokes, handle current stroke if active |

### Data Model (Unchanged)
```
Document
├── Page 0
│   ├── VectorLayer 0 (with stroke cache)
│   ├── VectorLayer 1 (with stroke cache)
│   └── background settings
├── Page 1
└── ...
```

**Key point:** Document, Page, VectorLayer remain unchanged. Only the rendering widgets change.

---

## Design Decisions

### D1: Current Stroke Handling
**Decision:** Active LayerWidget handles the current stroke (Option A)

The active LayerWidget renders both:
1. Its VectorLayer's committed strokes (from stroke cache)
2. The current in-progress stroke (if this layer is active)

When active layer changes, the previous LayerWidget stops rendering current stroke, new one starts.

**Rationale:** Conceptually cleaner - the current stroke belongs to the layer it will be committed to.

### D2: PageWidget as QWidget
**Decision:** PageWidget is a QWidget (parent container)

- PageWidget positions and manages its child widgets
- DocumentViewport only deals with PageWidgets, not internal layer widgets
- Clean encapsulation

### D3: Page Lifecycle
**Decision:** Create/destroy PageWidgets dynamically

- When page scrolls into view → create PageWidget
- When page scrolls out of view → destroy PageWidget
- Simple approach; widget creation is cheap compared to PDF rendering

### D4: Background Cache Location
**Decision:** BackgroundWidget owns and manages the background cache

- The cache is a rendering concern, belongs in the widget
- BackgroundWidget stores: `QPixmap m_cache`, `qreal m_cacheZoom`, `qreal m_cacheDpr`
- Rebuilt when zoom changes or background settings change

### D5: Input Routing
**Decision:** DocumentViewport routes to PageWidget, PageWidget routes to active layer

```
Pointer Event
    → DocumentViewport::handlePointerEvent()
        → Find PageWidget at pointer position
        → PageWidget::handlePointerEvent()
            → Update current stroke
            → activeLayerWidget->update()
```

---

## File Structure

### New Files
```
source/viewport/
├── PageWidget.h
├── PageWidget.cpp
├── BackgroundWidget.h
├── BackgroundWidget.cpp
├── LayerWidget.h
└── LayerWidget.cpp
```

### Modified Files
```
source/core/DocumentViewport.h   - Major changes
source/core/DocumentViewport.cpp - Major changes
CMakeLists.txt                   - Add new files
```

### Unchanged Files
```
source/core/Document.h/.cpp      - No changes
source/core/Page.h/.cpp          - No changes
source/layers/VectorLayer.h      - No changes (stroke cache stays here)
source/ui/LayerPanel.cpp         - No changes (only signal handlers change)
```

---

## Phase Breakdown

### Phase 0: Preparation ✅ COMPLETE
- [x] Create `source/viewport/` directory
- [x] Add new files to CMakeLists.txt
- [x] Document current DocumentViewport interface for reference

**Created files:**
```
source/viewport/
├── BackgroundWidget.h    (98 lines)
├── BackgroundWidget.cpp  (154 lines)
├── LayerWidget.h         (107 lines)
├── LayerWidget.cpp       (99 lines)
├── PageWidget.h          (164 lines)
└── PageWidget.cpp        (221 lines)
```

**CMakeLists.txt update:**
```cmake
set(VIEWPORT_SOURCES
    source/viewport/BackgroundWidget.cpp
    source/viewport/LayerWidget.cpp
    source/viewport/PageWidget.cpp
)
```

**Current DocumentViewport Interface (for reference):**
```cpp
// Key methods to KEEP (will be modified to use PageWidgets):
void setDocument(Document* doc);
void setZoomLevel(qreal zoom);
void setPanOffset(QPointF offset);
void scrollToPage(int pageIndex);
QPointF viewportToDocument(QPointF viewportPt) const;
PageHit viewportToPage(QPointF viewportPt) const;
int pageAtPoint(QPointF documentPt) const;
QVector<int> visiblePages() const;

// Key methods to REMOVE (replaced by PageWidget):
void renderPage(QPainter& painter, Page* page, int pageIndex);
void renderCurrentStrokeIncremental(QPainter& painter);

// Key methods to MODIFY (route to PageWidget):
void handlePointerPress(const PointerEvent& pe);
void handlePointerMove(const PointerEvent& pe);
void handlePointerRelease(const PointerEvent& pe);

// Signals to KEEP:
void zoomChanged(qreal zoom);
void panChanged(QPointF offset);
void currentPageChanged(int pageIndex);
void documentModified();
```

### Phase 1: BackgroundWidget ✅ COMPLETE
**Goal:** Widget that renders page background (color + PDF + grid/lines)

**Files:** `BackgroundWidget.h`, `BackgroundWidget.cpp`

**Interface:**
```cpp
class BackgroundWidget : public QWidget {
    Q_OBJECT
public:
    explicit BackgroundWidget(QWidget* parent = nullptr);
    
    void setPage(Page* page);
    void setPdfPixmap(const QPixmap& pdfPixmap);  // From DocumentViewport's PDF cache
    void setZoom(qreal zoom);
    
    void invalidateCache();  // Force rebuild on next paint
    
protected:
    void paintEvent(QPaintEvent* event) override;
    
private:
    Page* m_page = nullptr;
    QPixmap m_pdfPixmap;
    qreal m_zoom = 1.0;
    
    // Background cache
    QPixmap m_cache;
    qreal m_cacheZoom = 0;
    qreal m_cacheDpr = 0;
    bool m_cacheDirty = true;
    
    void rebuildCache();
};
```

**Key behaviors:**
- `paintEvent()` blits `m_cache` (single drawPixmap)
- Cache rebuilt only when zoom/dpr/settings change
- Receives PDF pixmap from parent (PageWidget gets it from DocumentViewport)

### Phase 2: LayerWidget ✅ COMPLETE
**Goal:** Widget that renders one VectorLayer's strokes

**Files:** `LayerWidget.h`, `LayerWidget.cpp`

**Interface:**
```cpp
class LayerWidget : public QWidget {
    Q_OBJECT
public:
    explicit LayerWidget(QWidget* parent = nullptr);
    
    void setVectorLayer(VectorLayer* layer);
    void setPageSize(const QSizeF& size);
    void setZoom(qreal zoom);
    
    // Active layer handling
    void setActive(bool active);
    void setCurrentStroke(const VectorStroke* stroke);  // During drawing
    
    void invalidateStrokeCache();  // Trigger VectorLayer cache rebuild
    
protected:
    void paintEvent(QPaintEvent* event) override;
    
private:
    VectorLayer* m_layer = nullptr;
    QSizeF m_pageSize;
    qreal m_zoom = 1.0;
    
    bool m_isActive = false;
    const VectorStroke* m_currentStroke = nullptr;
};
```

**Key behaviors:**
- Transparent background (`setAttribute(Qt::WA_TranslucentBackground)`)
- `paintEvent()` calls `m_layer->renderWithZoomCache()` (uses VectorLayer's existing cache)
- If active, also renders `m_currentStroke`
- Only this widget updates during stroke drawing

### Phase 3: PageWidget ✅ COMPLETE
**Goal:** Container that manages BackgroundWidget + LayerWidgets for one page

**Files:** `PageWidget.h`, `PageWidget.cpp`

**Interface:**
```cpp
class PageWidget : public QWidget {
    Q_OBJECT
public:
    explicit PageWidget(QWidget* parent = nullptr);
    
    void setPage(Page* page);
    void setPdfPixmap(const QPixmap& pdfPixmap);
    void setZoom(qreal zoom);
    
    // Layer management
    void syncLayers();  // Rebuild LayerWidgets to match Page's layers
    void setActiveLayerIndex(int index);
    
    // Input handling
    void beginStroke(const VectorStroke& stroke);
    void updateStroke(const VectorStroke& stroke);
    void endStroke();
    
    // Cache invalidation
    void invalidateBackgroundCache();
    void invalidateAllCaches();
    
signals:
    void strokeCompleted(int layerIndex, const VectorStroke& stroke);
    
private:
    Page* m_page = nullptr;
    qreal m_zoom = 1.0;
    
    BackgroundWidget* m_backgroundWidget = nullptr;
    QVector<LayerWidget*> m_layerWidgets;
    int m_activeLayerIndex = 0;
    
    VectorStroke m_currentStroke;  // In-progress stroke
    
    void createLayerWidgets();
    void destroyLayerWidgets();
    void updateLayerWidgetGeometry();
};
```

**Key behaviors:**
- Creates/destroys LayerWidgets when layers added/removed from Page
- Routes current stroke to active LayerWidget
- Positions all child widgets to fill the page area

### Phase 4: DocumentViewport Modification ✅ COMPLETE
**Goal:** Replace single-widget rendering with PageWidget management

**Testing Shortcut Added:**
- Press `T` to toggle between OLD (direct render) and NEW (PageWidgets) architecture
- Debug overlay shows current architecture: `Arch: OLD` or `Arch: NEW`
- Console outputs toggle status via qDebug

---

## CRITICAL FIX: Qt Transparency Compositing Issue

### The Problem Discovered

Initial testing showed BackgroundWidget and LayerWidget paint counts were **IDENTICAL**:
```
BackgroundWidget::paintEvent # 101
LayerWidget::paintEvent # 101
```

**Root Cause:** Qt's transparency compositing model.

When LayerWidget had `WA_TranslucentBackground`:
1. `activeLW->update()` is called
2. Qt sees LayerWidget is transparent
3. Qt needs what's "behind" for compositing
4. Qt marks BackgroundWidget as needing repaint
5. BackgroundWidget repaints (**including PDF blit!**)
6. LayerWidget repaints
7. Qt composites them

**This is fundamental to Qt's widget model - cannot be bypassed with stacked transparent widgets.**

### The Solution: Bake + Blit Architecture

**NEW Architecture:**
```
PageWidget
├── BackgroundWidget (OPAQUE)
│   └── Caches: Background + PDF + Grid + ALL INACTIVE layer strokes
│
└── LayerWidget (OPAQUE - only ONE for active layer)
    └── paintEvent:
        1. Blit BackgroundWidget's cache (single drawPixmap)
        2. Render active layer strokes
        3. Render current stroke
```

**Key Changes:**
1. **BackgroundWidget** now renders INACTIVE layer strokes into its cache
2. **LayerWidget** is now **OPAQUE** (not transparent)
3. **LayerWidget** blits the background cache before rendering strokes
4. **Only ONE LayerWidget exists** (for the active layer)
5. When active layer changes, BackgroundWidget rebuilds cache

**Performance:**
- During strokes: Only LayerWidget updates
- BackgroundWidget does NOT repaint
- PDF is NEVER re-blitted during strokes
- Stroke performance is completely decoupled from PDF!

---

**How to Test:**
1. Launch application
2. Load JSON with PDF
3. Press `T` to switch to NEW architecture
4. Rapid-fire draw strokes
5. Check console: BackgroundWidget should NOT repaint during strokes
6. Press `B` to benchmark, compare paint rates between OLD and NEW

**Changes to DocumentViewport:**

1. **Remove:** `renderPage()` method (replaced by PageWidget)
2. **Add:** PageWidget management
   ```cpp
   QVector<PageWidget*> m_pageWidgets;
   QHash<int, PageWidget*> m_pageIndexToWidget;
   
   void syncPageWidgets();  // Create/destroy PageWidgets for visible pages
   PageWidget* pageWidgetAt(int pageIndex);
   ```

3. **Modify:** `paintEvent()` 
   - No longer renders pages directly
   - Only renders viewport background (gray area between pages)
   - PageWidgets render themselves

4. **Modify:** Input handling
   - Route to correct PageWidget based on position
   - PageWidget handles stroke state

5. **Keep:** PDF cache (`m_pdfCache`) - provides pixmaps to PageWidgets

**Interface changes:**
```cpp
// Remove these from DocumentViewport:
- void renderPage(QPainter& painter, Page* page, int pageIndex);
- void renderCurrentStrokeIncremental(QPainter& painter);

// Add these to DocumentViewport:
+ void syncPageWidgets();
+ PageWidget* pageWidgetForPage(int pageIndex);
+ void updatePageWidgetPositions();  // Called on pan/zoom
```

### Phase 5: Integration & Wiring
**Goal:** Connect everything together

1. **LayerPanel signals:**
   ```cpp
   // In MainWindow or DocumentViewport:
   connect(layerPanel, &LayerPanel::layerAdded, [](int index) {
       currentPageWidget->syncLayers();
   });
   connect(layerPanel, &LayerPanel::layerRemoved, [](int index) {
       currentPageWidget->syncLayers();
   });
   connect(layerPanel, &LayerPanel::activeLayerChanged, [](int index) {
       currentPageWidget->setActiveLayerIndex(index);
   });
   ```

2. **Zoom handling:**
   ```cpp
   void DocumentViewport::setZoomLevel(qreal zoom) {
       m_zoomLevel = zoom;
       invalidatePdfCache();
       for (PageWidget* pw : m_pageWidgets) {
           pw->setZoom(zoom);
           pw->invalidateBackgroundCache();
       }
       updatePageWidgetPositions();
   }
   ```

3. **Page navigation:**
   - On scroll, check which pages are visible
   - Create PageWidgets for newly visible pages
   - Destroy PageWidgets for pages that scrolled out

### Phase 6: Testing & Cleanup
**Goal:** Verify everything works correctly

**Test cases:**
- [ ] Single page document, single layer - draw strokes
- [ ] Multi-page document - scroll through pages
- [ ] Multi-layer document - switch layers, draw on each
- [ ] PDF document - verify background renders correctly
- [ ] Grid/Lines background - verify render correctly
- [ ] Zoom in/out - caches rebuild correctly
- [ ] Add layer via LayerPanel - LayerWidget created
- [ ] Remove layer via LayerPanel - LayerWidget destroyed
- [ ] Performance test - rapid strokes should be smooth with PDF loaded

**Cleanup:**
- [ ] Remove old rendering code from DocumentViewport
- [ ] Remove unused includes
- [ ] Update documentation

---

## Migration Strategy

### Approach: Parallel Implementation
1. Build new widget classes alongside existing code
2. Add a toggle to switch between old and new rendering
3. Test new implementation thoroughly
4. Remove old code once verified

```cpp
// Temporary toggle during development
bool DocumentViewport::useNewArchitecture() const {
    return m_useNewArchitecture;  // Toggle for testing
}

void DocumentViewport::paintEvent(QPaintEvent* event) {
    if (useNewArchitecture()) {
        // New: PageWidgets render themselves
        // Just draw viewport background
    } else {
        // Old: existing renderPage() code
    }
}
```

### Rollback Plan
If issues arise, the old code remains available via toggle. No destructive changes until new architecture is verified.

---

## Performance Expectations

### Before (Current)
```
Rapid strokes on PDF page:
  paintEvent() called at 360Hz
    → fillRect(background)         ~0.1ms
    → drawPixmap(PDF, full page)   ~2-5ms  ← THE PROBLEM
    → drawLines(grid)              ~0.5ms
    → renderStrokes(all layers)    ~1ms (cached)
  Total per frame: ~4-7ms
  Result: LAGGY
```

### After (New Architecture)
```
Rapid strokes on PDF page:
  Only active LayerWidget::paintEvent() called at 360Hz
    → drawPixmap(stroke cache)     ~0.1ms
    → drawCurrentStroke            ~0.1ms
  Total per frame: ~0.2ms
  
  BackgroundWidget: NOT repainted (no update() called)
  Other LayerWidgets: NOT repainted (no update() called)
  Result: SMOOTH
```

---

## Open Questions

### Q1: Object Rendering
InsertedObjects (images, shapes) are currently rendered in `renderPage()`. Where should they go?

**Options:**
- A) Separate ObjectWidget per object
- B) Objects rendered in a dedicated ObjectsLayerWidget
- C) Objects rendered in LayerWidgets (mixed with strokes)

**Decision:** Defer for now. Architecture allows adding ObjectWidget later.

### Q2: Page Border
Currently rendered in `renderPage()`. Where should it go?

**Options:**
- A) Part of BackgroundWidget (conceptually the page "frame")
- B) Separate thin overlay widget
- C) Part of PageWidget's paintEvent (PageWidget paints border around children)

**Recommendation:** Option A - include in BackgroundWidget as part of the static background.

### Q3: Eraser Cursor
Currently rendered in DocumentViewport. Keep it there or move to PageWidget?

**Recommendation:** Keep in DocumentViewport - it's a viewport-level UI element, not page-specific.

---

## Success Criteria

1. **Performance:** Rapid strokes on PDF pages are as smooth as on blank pages
2. **Correctness:** All existing functionality preserved
3. **Architecture:** Clean separation of concerns, testable components
4. **Maintainability:** Code is clear and well-documented

---

## Timeline Estimate

| Phase | Estimated Effort |
|-------|------------------|
| Phase 0: Preparation | 0.5 day |
| Phase 1: BackgroundWidget | 1 day |
| Phase 2: LayerWidget | 1 day |
| Phase 3: PageWidget | 1.5 days |
| Phase 4: DocumentViewport Modification | 2 days |
| Phase 5: Integration | 1 day |
| Phase 6: Testing & Cleanup | 1 day |
| **Total** | **~8 days** |

---

## Notes

- Document, Page, VectorLayer, DocumentManager remain unchanged
- LayerPanel remains unchanged (only signal connections change)
- VectorLayer's stroke cache is reused (no changes needed)
- PDF cache stays in DocumentViewport (provides pixmaps to PageWidgets)

---

*Subplan for DocumentViewport reconstruction in SpeedyNote Phase 3 migration*

