# Phase 1.3: DocumentViewport Subplan

## Overview

DocumentViewport is a QWidget that:
- Displays pages from a Document with pan/zoom
- Routes input to the correct page
- Manages caching for smooth scrolling
- Communicates with MainWindow via signals

**Replaces:** InkCanvas (view/input portions) + VectorCanvas overlay
**Does NOT replace:** Document/Page data structures (already created)

---

## Design Decisions (Confirmed)

| Topic | Decision |
|-------|----------|
| **Instances** | One DocumentViewport per tab |
| **Layout modes** | SingleColumn, TwoColumn initially (extensible) |
| **Layout switching** | Manual toggle, not auto |
| **Page gaps** | Fixed value (configurable later) |
| **Scrolling** | Continuous (not snap-to-page) |
| **Pan system** | Unified across pages (like Ocular/Word) |
| **Input routing** | Routes to whichever page pen touches |
| **Edgeless pan** | No boundary, infinite canvas |
| **Home position** | Exists, accessible via MainWindow UI |
| **Zoom on resize** | Keep zoom level same |
| **PDF cache** | 2 pages (single col) / 4 pages (two col) |
| **DPI caching** | Single DPI, flush on zoom change |
| **Stroke cache** | Pre-load for nearby pages |

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                      DocumentViewport                            │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │ View State                                               │    │
│  │  - panOffset (QPointF)     // Scroll position            │    │
│  │  - zoomLevel (qreal)       // Current zoom               │    │
│  │  - currentPageIndex (int)  // For status display         │    │
│  └─────────────────────────────────────────────────────────┘    │
│                                                                  │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │ Layout Engine                                            │    │
│  │  - layoutMode (enum)                                     │    │
│  │  - pageGap (int)                                         │    │
│  │  - pagePosition(index) → QPointF                         │    │
│  │  - pageAtPoint(viewportPt) → int                         │    │
│  │  - totalContentSize() → QSizeF                           │    │
│  └─────────────────────────────────────────────────────────┘    │
│                                                                  │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │ Cache Manager                                            │    │
│  │  - pdfCache (map: pageIndex → QPixmap at current DPI)    │    │
│  │  - visibleRange (first, last visible page)               │    │
│  │  - cacheRange (visible + buffer pages)                   │    │
│  │  - preloadStrokeCaches()                                 │    │
│  └─────────────────────────────────────────────────────────┘    │
│                                                                  │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │ Input Router                                             │    │
│  │  - viewportToDocument(QPointF) → (pageIndex, QPointF)    │    │
│  │  - documentToViewport(pageIndex, QPointF) → QPointF      │    │
│  │  - activeDrawingPage (int, or -1)                        │    │
│  └─────────────────────────────────────────────────────────┘    │
│                                                                  │
│  Document* m_document;  // Reference, not owned                  │
└─────────────────────────────────────────────────────────────────┘
```

---

## Task Breakdown

### Task 1.3.1: Core Skeleton (~150 lines) ✅ COMPLETE

**Files:** `source/core/DocumentViewport.h`, `source/core/DocumentViewport.cpp`

**Implemented:**
- Constructor with mouse/tablet tracking enabled
- `setDocument()` / `document()` - sets document reference, resets view state
- View state: `zoomLevel`, `panOffset`, `currentPageIndex` with getters/setters
- Layout: `layoutMode`, `pageGap` with getters/setters
- Signals: `zoomChanged`, `panChanged`, `currentPageChanged`, `documentModified`, scroll fractions
- Slots: `setZoomLevel`, `setPanOffset`, `scrollToPage`, `scrollBy`, `zoomToFit`, `zoomToWidth`, `scrollToHome`
- Placeholder `paintEvent()` showing document info
- Stub implementations for input events and coordinate transforms (marked with TODO)

Create the basic class structure:

```cpp
class DocumentViewport : public QWidget {
    Q_OBJECT
    
public:
    explicit DocumentViewport(QWidget* parent = nullptr);
    ~DocumentViewport();
    
    // Document management
    void setDocument(Document* doc);
    Document* document() const;
    
    // View state getters
    qreal zoomLevel() const;
    QPointF panOffset() const;
    int currentPageIndex() const;
    
signals:
    void zoomChanged(qreal zoom);
    void panChanged(QPointF offset);
    void currentPageChanged(int pageIndex);
    void documentModified();
    
protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    
private:
    Document* m_document = nullptr;
    qreal m_zoomLevel = 1.0;
    QPointF m_panOffset;
    int m_currentPageIndex = 0;
};
```

**Deliverable:** Compiles, can be instantiated, shows blank widget

---

### Task 1.3.2: Layout Engine (~200 lines) ✅ COMPLETE

**Files:** `source/core/DocumentViewport.h`, `source/core/DocumentViewport.cpp`

**Implemented:**
- `pagePosition(int pageIndex)` - calculates top-left of page in document coords
- `pageRect(int pageIndex)` - returns full rect including position and size
- `totalContentSize()` - bounding box of all pages
- `pageAtPoint(QPointF documentPt)` - finds which page contains a point
- `visiblePages()` - returns list of pages intersecting viewport
- `visibleRect()` - returns visible area in document coordinates
- Layout support for SingleColumn and TwoColumn modes
- Edgeless document support (single page, no clamping)
- Updated `clampPanOffset()` to use `totalContentSize()`
- Updated `updateCurrentPageIndex()` to use layout engine
- Updated `scrollToPage()` to use `pagePosition()`
- Debug rendering showing page outlines with numbers

Implement page positioning:

```cpp
enum class LayoutMode {
    SingleColumn,
    TwoColumn
};

// In DocumentViewport:
LayoutMode m_layoutMode = LayoutMode::SingleColumn;
int m_pageGap = 20;  // pixels between pages

QPointF pagePosition(int pageIndex) const;      // Top-left of page in document coords
QRectF pageRect(int pageIndex) const;           // Full rect including page size
QSizeF totalContentSize() const;                // Bounding box of all pages
int pageAtPoint(QPointF documentPt) const;      // Which page contains this point?
QVector<int> visiblePages() const;              // Pages intersecting viewport
```

**Layout calculations:**

```
SingleColumn:
  Page 0: (0, 0)
  Page 1: (0, page0.height + gap)
  Page 2: (0, page0.height + page1.height + 2*gap)
  ...

TwoColumn:
  Page 0: (0, 0)
  Page 1: (page0.width + gap, 0)
  Page 2: (0, max(page0.height, page1.height) + gap)
  Page 3: (page2.width + gap, max(page0.height, page1.height) + gap)
  ...
```

**Deliverable:** Can calculate positions, `visiblePages()` works

---

### Task 1.3.3: Basic Rendering (~250 lines) ✅ COMPLETE

**Files:** `source/core/DocumentViewport.h`, `source/core/DocumentViewport.cpp`

**Implemented:**
- `renderPage()` helper method for rendering a single page
- Page background rendering (solid color, grid, lines, custom image)
- PDF page rendering (live render, cache comes in Task 1.3.6)
- `effectivePdfDpi()` to calculate zoom-scaled DPI for PDF rendering
- Calls `Page::render()` with zoom=1.0 (painter handles zoom)
- Page border rendering for visual separation
- Debug overlay toggle (`m_showDebugOverlay`)
- Only renders visible pages for performance

Implement `paintEvent()`:

```cpp
void DocumentViewport::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Apply view transform
    painter.translate(-m_panOffset * m_zoomLevel);
    painter.scale(m_zoomLevel, m_zoomLevel);
    
    // Get visible pages
    QVector<int> visible = visiblePages();
    
    for (int pageIdx : visible) {
        Page* page = m_document->page(pageIdx);
        if (!page) continue;
        
        QPointF pos = pagePosition(pageIdx);
        painter.save();
        painter.translate(pos);
        
        // Render page background
        renderPageBackground(painter, page, pageIdx);
        
        // Render page content (strokes, objects)
        page->render(painter, m_document, m_zoomLevel);
        
        painter.restore();
    }
}
```

**Sub-tasks:**
- Render page backgrounds (solid color, grid, lines)
- Render PDF backgrounds (from cache or live)
- Render vector layers via `Page::render()`
- Render inserted objects

**Deliverable:** Can see pages rendered with strokes

---

### Task 1.3.4: Pan & Zoom (~200 lines) ✅ COMPLETE

**Files:** `source/core/DocumentViewport.h`, `source/core/DocumentViewport.cpp`

**Implemented:**
- `zoomToFit()` - fits current page in viewport with 5% margin
- `zoomToWidth()` - fits page width to viewport with 5% margin
- `wheelEvent()` - scroll with wheel, zoom with Ctrl+wheel
- `viewportCenter()` - returns viewport center in document coords
- `zoomAtPoint()` - zoom towards cursor (keeps point stationary)
- Scroll with Shift+wheel for horizontal scrolling
- Supports both mouse wheel and touchpad pixel deltas
- Multiplicative zoom (10% per wheel step) for consistent feel

Implement view manipulation:

```cpp
public slots:
    void setZoomLevel(qreal zoom);
    void setPanOffset(QPointF offset);
    void zoomToFit();
    void zoomToWidth();
    void scrollToPage(int pageIndex);
    void scrollBy(QPointF delta);
    
private:
    void clampPanOffset();  // Keep within content bounds
    void updateCurrentPageFromPan();  // Determine which page is "current"
    QPointF viewportCenter() const;  // Center point in document coords
```

**Pan bounds:**
- Minimum: Can scroll to show first page
- Maximum: Can scroll to show last page
- Allow some overscroll margin (e.g., 50% of viewport)

**Current page detection:**
- The page with most visible area, OR
- The page containing viewport center

**Deliverable:** Can pan/zoom programmatically, signals emit

---

### Task 1.3.5: Coordinate Transform (~100 lines) ✅ COMPLETE

**Files:** `source/core/DocumentViewport.h`, `source/core/DocumentViewport.cpp`

**Implemented:**
- `viewportToDocument()` - viewport pixel → document coordinate
- `documentToViewport()` - document coordinate → viewport pixel
- `viewportToPage()` - viewport pixel → page-local (via PageHit)
- `pageToViewport()` - page-local → viewport pixel
- `pageToDocument()` - page-local → document coordinate
- `documentToPage()` - document coordinate → page-local (via PageHit)
- Uses existing `pageAtPoint()` and `pagePosition()` from layout engine
- No DPR code needed (Qt handles logical coordinates automatically)

**Also fixed:** `effectivePdfDpi()` now includes device pixel ratio for high DPI screens.

Implement coordinate conversion:

```cpp
// Viewport pixel → Document coordinate
QPointF viewportToDocument(QPointF viewportPt) const {
    return viewportPt / m_zoomLevel + m_panOffset;
}

// Document coordinate → Viewport pixel
QPointF documentToViewport(QPointF docPt) const {
    return (docPt - m_panOffset) * m_zoomLevel;
}

// Viewport pixel → (pageIndex, page-local coordinate)
struct PageHit {
    int pageIndex = -1;
    QPointF pagePoint;
    bool valid() const { return pageIndex >= 0; }
};
PageHit viewportToPage(QPointF viewportPt) const;

// Page-local coordinate → Viewport pixel
QPointF pageToViewport(int pageIndex, QPointF pagePt) const;
```

**Deliverable:** Coordinate transforms work correctly at any zoom

---

### Task 1.3.6: PDF Cache (~200 lines) ✅ COMPLETE

**Files:** `source/core/DocumentViewport.h`, `source/core/DocumentViewport.cpp`

**Implemented:**
- `PdfCacheEntry` struct for cached PDF page data
- `getCachedPdfPage()` - returns cached pixmap or renders and caches
- `preloadPdfCache()` - pre-loads ±1 pages around visible area
- `invalidatePdfCache()` - clears all cached pages (on zoom/document change)
- `invalidatePdfCachePage()` - invalidates single page
- `updatePdfCacheCapacity()` - adjusts capacity based on layout mode
- Cache capacity: 4 for single column, 8 for two column
- Auto-invalidation on zoom change (DPI changed)
- Auto-invalidation on document change
- Updated `renderPage()` to use cache instead of live rendering

Implement PDF page caching:

```cpp
struct PdfCacheEntry {
    int pageIndex;
    qreal dpi;
    QPixmap pixmap;
};

class PdfCache {
public:
    void setCapacity(int pages);  // Default: 2 for single col, 4 for two col
    
    QPixmap getPage(Document* doc, int pageIndex, qreal dpi);
    void preload(Document* doc, int centerPage, qreal dpi);
    void invalidate();  // Clear all (on zoom change)
    void invalidatePage(int pageIndex);  // Single page changed
    
private:
    QList<PdfCacheEntry> m_cache;
    int m_capacity = 2;
    
    void renderPageAsync(Document* doc, int pageIndex, qreal dpi);
};
```

**Cache strategy:**
1. On scroll: Check if pages near viewport are cached
2. If not cached: Render synchronously (blocking) OR show placeholder
3. Pre-load: After scroll settles, render ±1 pages in background
4. On zoom: Clear cache, re-render at new DPI

**Deliverable:** PDF pages render from cache, pre-loading works

---

### Task 1.3.7: Stroke Cache Pre-loading (~100 lines) ✅ COMPLETE

**Files:** `source/core/DocumentViewport.cpp`, `source/layers/VectorLayer.h`, `source/core/Page.h`, `source/core/Page.cpp`

**Implemented:**
- Added stroke cache to `VectorLayer`:
  - `ensureStrokeCacheValid(size, dpr)` - builds/validates cache
  - `invalidateStrokeCache()` - marks cache dirty
  - `renderWithCache(painter, size, dpr)` - renders using cache
  - Auto-invalidation on addStroke/removeStroke/clear
  - High DPI support via device pixel ratio
- Added `preloadStrokeCaches()` to DocumentViewport
- Added `renderObjects()` to Page (for separate object rendering)
- Updated `renderPage()` to use cached layer rendering
- Cache pre-loads ±1 pages around visible area

Pre-generate stroke caches for nearby pages:

```cpp
void DocumentViewport::preloadStrokeCaches() {
    if (!m_document) return;
    
    QVector<int> visible = visiblePages();
    int first = visible.isEmpty() ? 0 : visible.first();
    int last = visible.isEmpty() ? 0 : visible.last();
    
    // Pre-load ±1 pages beyond visible
    int preloadStart = qMax(0, first - 1);
    int preloadEnd = qMin(m_document->pageCount() - 1, last + 1);
    
    for (int i = preloadStart; i <= preloadEnd; ++i) {
        Page* page = m_document->page(i);
        if (!page) continue;
        
        // Trigger stroke cache generation for all layers
        for (int layerIdx = 0; layerIdx < page->layerCount(); ++layerIdx) {
            VectorLayer* layer = page->layer(layerIdx);
            if (layer) {
                layer->ensureStrokeCacheValid(m_zoomLevel);
            }
        }
    }
}
```

**Note:** May need to add `ensureStrokeCacheValid(zoom)` to VectorLayer

**Deliverable:** Nearby pages have stroke caches ready

---

### Task 1.3.8: Input Routing Basics (~150 lines) ✅ COMPLETE

**Files:** `source/core/DocumentViewport.h`, `source/core/DocumentViewport.cpp`

**Implemented:**
- `PointerEvent` struct - unified input abstraction for mouse/tablet/touch
- `GestureState` struct - stub for multi-touch gestures (Phase 2+)
- `mouseToPointerEvent()` / `tabletToPointerEvent()` - event converters
- `handlePointerEvent()` - main routing dispatcher
- `handlePointerPress()` / `handlePointerMove()` / `handlePointerRelease()` - handlers
- Eraser button detection via `QPointingDevice::PointerType::Eraser`
- Barrel button support via `event->buttons()`
- Active drawing page tracking (m_activeDrawingPage)
- Pointer source tracking (avoids tablet/mouse duplicate events)
- Debug output for testing (qDebug)
- Cache pre-loading on pointer release

Handle mouse/tablet events and route to pages:

```cpp
protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void tabletEvent(QTabletEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    
private:
    int m_activeDrawingPage = -1;  // Page currently receiving strokes
    
    void handlePointerPress(QPointF viewportPos, qreal pressure);
    void handlePointerMove(QPointF viewportPos, qreal pressure);
    void handlePointerRelease(QPointF viewportPos);
```

**Routing logic:**
1. Convert viewport point to document coordinates
2. Find which page contains the point
3. Convert to page-local coordinates
4. Forward to that page's active layer

**Note:** Full drawing implementation comes in Phase 2. This task sets up the routing infrastructure.

**Deliverable:** Input events reach the correct page

---

### Task 1.3.9: Resize Handling (~50 lines) ✅ COMPLETE

**Files:** `source/core/DocumentViewport.cpp`

**Implemented:**
- Keeps same document point centered after resize
- Handles first resize (no old size) gracefully
- Clamps pan offset to new bounds
- Updates current page index
- Emits panChanged and scroll fraction signals
- Works correctly for window resize and screen rotation

Handle viewport resize:

```cpp
void DocumentViewport::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    
    // Keep the same document point at viewport center
    QPointF centerBefore = viewportToDocument(
        QPointF(event->oldSize().width() / 2.0, event->oldSize().height() / 2.0)
    );
    
    // After resize, adjust pan so same point is at center
    QPointF centerAfter = QPointF(width() / 2.0, height() / 2.0);
    m_panOffset = centerBefore - centerAfter / m_zoomLevel;
    
    clampPanOffset();
    updateVisiblePages();
    update();
}
```

**Deliverable:** Content stays centered on resize/rotation

---

### Task 1.3.10: Signals & Scroll Sync (~100 lines) ✅ COMPLETE

**Files:** `source/core/DocumentViewport.cpp`

**Implemented:**
- `emitScrollFractions()` - calculates and emits scroll fractions (0.0 to 1.0)
- `setHorizontalScrollFraction()` - sets pan X from external scrollbar
- `setVerticalScrollFraction()` - sets pan Y from external scrollbar
- Handles edge cases (content smaller than viewport, empty document)
- Updates current page index on vertical scroll
- Emits panChanged signal when scroll position changes

Signals for MainWindow integration:

```cpp
signals:
    // View state changes
    void zoomChanged(qreal zoom);
    void panChanged(QPointF offset);
    void currentPageChanged(int pageIndex);
    
    // Content changes (for save indicator)
    void documentModified();
    
    // Scroll position for external sliders (0.0 to 1.0)
    void horizontalScrollChanged(qreal fraction);
    void verticalScrollChanged(qreal fraction);
    
public slots:
    // For external scroll bars
    void setHorizontalScrollFraction(qreal fraction);
    void setVerticalScrollFraction(qreal fraction);
```

**Scroll fraction calculation:**
```cpp
qreal verticalFraction() const {
    qreal contentHeight = totalContentSize().height();
    qreal viewportHeight = height() / m_zoomLevel;
    qreal scrollableHeight = contentHeight - viewportHeight;
    if (scrollableHeight <= 0) return 0;
    return m_panOffset.y() / scrollableHeight;
}
```

**Deliverable:** External sliders can sync with viewport

---

### Task 1.3.11: Test Harness (~100 lines) ✅ COMPLETE

**Files:** `source/core/DocumentViewportTests.h`, `source/Main.cpp`

**Implemented:**
- `--test-viewport` command line flag
- Separate test file (`DocumentViewportTests.h`) following existing pattern
- **9 Unit Tests:**
  - `testViewportCreation` - basic creation and document assignment
  - `testZoomBounds` - zoom min/max clamping
  - `testLayoutEngine` - SingleColumn and TwoColumn positioning
  - `testCoordinateTransforms` - viewport ↔ document transforms
  - `testPageHitDetection` - page hit detection and gaps
  - `testVisiblePages` - visible page calculation
  - `testScrollFractions` - scroll fraction setters/getters
  - `testPdfCache` - cache management (no crash)
  - `testPointerEvents` - PointerEvent and GestureState structs
- **Visual Test:**
  - 5-page document with colored strokes
  - Variable backgrounds (plain/grid/lines)
  - Wavy, diagonal, and spiral strokes
  - Interactive pan/zoom/click testing

Create standalone test:

```cpp
// In Main.cpp
if (args.contains("--test-viewport")) {
    auto doc = Document::createNew("Test Document");
    
    // Add some test pages with content
    for (int i = 0; i < 5; ++i) {
        Page* page = (i == 0) ? doc->page(0) : doc->addPage();
        VectorStroke stroke;
        stroke.color = QColor::fromHsv(i * 60, 200, 200);
        stroke.baseThickness = 3.0;
        // Add wavy line
        for (int j = 0; j <= 50; ++j) {
            qreal t = j / 50.0;
            stroke.points.append({
                QPointF(50 + t * 700, 100 + qSin(t * 6.28 * 3) * 50),
                0.5 + 0.5 * t
            });
        }
        stroke.updateBoundingBox();
        page->activeLayer()->addStroke(stroke);
    }
    
    DocumentViewport* viewport = new DocumentViewport();
    viewport->setDocument(doc.get());
    viewport->resize(800, 600);
    viewport->show();
    
    return app.exec();
}
```

**Deliverable:** Can run `NoteApp --test-viewport` to see pages

---

## Task Summary

| Task | Description | Est. Lines | Dependencies |
|------|-------------|------------|--------------|
| 1.3.1 | Core Skeleton | 150 | None |
| 1.3.2 | Layout Engine | 200 | 1.3.1 |
| 1.3.3 | Basic Rendering | 250 | 1.3.2 |
| 1.3.4 | Pan & Zoom | 200 | 1.3.3 |
| 1.3.5 | Coordinate Transform | 100 | 1.3.2 |
| 1.3.6 | PDF Cache | 200 | 1.3.3 |
| 1.3.7 | Stroke Cache Preload | 100 | 1.3.3 |
| 1.3.8 | Input Routing Basics | 150 | 1.3.5 |
| 1.3.9 | Resize Handling | 50 | 1.3.4 |
| 1.3.10 | Signals & Scroll Sync | 100 | 1.3.4 |
| 1.3.11 | Test Harness | 100 | All above |

**Total estimated:** ~1600 lines

---

## File Structure After Phase 1.3

```
source/
├── core/
│   ├── Document.h / Document.cpp       ✅ (Phase 1.2)
│   ├── DocumentTests.h                 ✅ (Phase 1.2)
│   ├── Page.h / Page.cpp               ✅ (Phase 1.1)
│   ├── PageTests.h                     ✅ (Phase 1.1)
│   ├── DocumentViewport.h              ← NEW
│   ├── DocumentViewport.cpp            ← NEW
│   └── PdfCache.h                      ← NEW (optional, can be inline)
├── layers/
│   └── VectorLayer.h                   ✅ (Phase 1.1)
├── strokes/
│   ├── StrokePoint.h                   ✅ (Phase 1.1)
│   └── VectorStroke.h                  ✅ (Phase 1.1)
├── objects/
│   ├── InsertedObject.h / .cpp         ✅ (Phase 1.1)
│   └── ImageObject.h / .cpp            ✅ (Phase 1.1)
└── pdf/
    ├── PdfProvider.h                   ✅ (Phase 1.2)
    └── PopplerPdfProvider.h / .cpp     ✅ (Phase 1.2)
```

---

## Success Criteria

Phase 1.3 is complete when:

1. ✅ `--test-viewport` shows multi-page document with strokes
2. ✅ Pan and zoom work via keyboard/code
3. ✅ Pages render with correct gaps
4. ✅ PDF pages render from cache
5. ✅ Coordinate transforms work at any zoom
6. ✅ Signals emit for zoom/pan/page changes
7. ✅ Resize maintains view position

---

## Notes for Implementation

### Performance Targets (Clovertrail Atom)
- Paint at 60+ FPS when idle
- Smooth pan/scroll (no visible lag)
- Zoom transition < 200ms
- Page switch < 100ms (cached)

### What's NOT in Phase 1.3
- Touch gesture handling (Phase 2 or 4)
- Actual drawing/erasing (Phase 2)
- MainWindow integration (Phase 3)
- Tool switching (Phase 4)
- Undo/redo (Phase 2)

Phase 1.3 creates the **rendering and view infrastructure**. Drawing comes in Phase 2.

---

## Important Notes for MainWindow Integration (Phase 3)

### Content Centering (Pan X = 0 Semantics)

**Pan X = 0 means document x=0 is at viewport x=0 (left edge).**

When content is narrower than the viewport, it appears left-aligned by default. To center content:

**Solution (implemented in Phase 3.3):** MainWindow sets an initial negative pan X when creating new tabs.

```cpp
// In MainWindow::centerViewportContent():
qreal centeringOffset = (viewportWidth - contentSize.width()) / 2.0;
viewport->setPanOffset(QPointF(-centeringOffset, currentPan.y()));
```

**Why this approach:**
1. **Preserves layout flexibility** - Pan semantics unchanged, horizontal scrolling still works
2. **One-time calculation** - Not recalculated on every zoom/resize (avoids complexity)
3. **DocumentViewport unchanged** - All centering logic is in MainWindow
4. **Debug overlay shows truth** - Negative pan X values are correct and expected

**DO NOT** add a separate rendering offset in DocumentViewport - this breaks zoom behavior and complicates coordinate transforms.
