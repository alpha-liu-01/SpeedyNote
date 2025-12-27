# doc-1: Document Loading Integration Subplan

> **Purpose:** Complete document loading/saving integration with DocumentViewport
> **Created:** Dec 24, 2024
> **Status:** 🔄 IN PROGRESS (Phase 1, 2, 3 complete, ready for Phase 3B/4)

---

## Overview

This subplan covers the integration of Document loading, saving, and PDF handling into MainWindow with DocumentViewport. The goal is to establish a working document persistence system before MainWindow modularization.

---

## Temporary Keyboard Shortcuts

These shortcuts are temporary until the toolbar is migrated. They will be removed or made customizable later.

| Shortcut | Action | Notes |
|----------|--------|-------|
| `Ctrl+S` | Save JSON | Opens file dialog, user picks location |
| `Ctrl+O` | Load JSON | Opens file dialog, user picks file |
| `Ctrl+Shift+O` | Open PDF | Opens file dialog, creates PDF-backed document |
| `Ctrl+Shift+N` | New Edgeless | Creates new edgeless document in new tab |
| `Ctrl+Shift+A` | Add Page | Appends new page at end of document |
| `Ctrl+Shift+I` | Insert Page | ✅ Inserts new page after current page |

---

## Task Breakdown

### Phase 1: Core Save/Load Infrastructure ✅ COMPLETE

#### 1.0 Implement Add Page (Prerequisite) ✅ COMPLETE
**Goal:** Add new page at end of document so we can test multi-page save/load

**Rationale:** Without this, we only have 1 page and can't properly test multi-page serialization.

**Implementation:**
- Added `MainWindow::addPageToDocument()` 
- Connected via `QShortcut` with `Qt::ApplicationShortcut` context
- Works correctly across multiple tabs

**Code flow:**
```
User presses Ctrl+Shift+A
  → MainWindow::addPageToDocument()
    → Get Document from current viewport
    → Document::addPage()
    → Viewport::update()
    → Mark tab as modified
```

#### 1.1 Implement Save Document Flow ✅ COMPLETE
**Goal:** Save current document to JSON file via file dialog

**Implementation:**
- Added `MainWindow::saveDocument()` method
- Uses `QFileDialog::getSaveFileName()` for location selection
- Serializes using `Document::toFullJson()`
- Writes indented JSON for readability
- Updates tab title and clears modified flag on success
- Shows error dialogs on failure

**Code flow:**
```
User presses Ctrl+S
  → MainWindow::saveDocument()
    → Get Document from viewport
    → Show QFileDialog::getSaveFileName()
    → Document::toFullJson()
    → QJsonDocument → Write to file
    → doc->clearModified()
    → Update tab title
```

#### 1.2 Implement Load Document Flow ✅ COMPLETE
**Goal:** Load document from JSON file via file dialog

**Implementation:**
- Added `MainWindow::loadDocument()` method
- Uses `QFileDialog::getOpenFileName()` for file selection
- Parses JSON with error handling
- Deserializes using `Document::fromFullJson()`
- Creates new tab via `TabManager::createTab()`
- Attempts PDF reload if document has PDF reference
- Centers viewport content after loading

**Code flow:**
```
User presses Ctrl+O
  → MainWindow::loadDocument()
    → Show QFileDialog::getOpenFileName()
    → Read file → QJsonDocument::fromJson()
    → Document::fromFullJson()
    → TabManager::createTab(doc, title)
    → Attempt PDF reload
    → centerViewportContent()
```

**Note:** Document ownership properly handled by `DocumentManager`.

#### 1.3 Connect Keyboard Shortcuts ✅ COMPLETE
**Goal:** Wire up Ctrl+S, Ctrl+O, and Ctrl+Shift+A to handlers

**Implementation:** All shortcuts use `QShortcut` with `Qt::ApplicationShortcut` context:
- `QKeySequence::Save` (Ctrl+S) → `saveDocument()`
- `QKeySequence::Open` (Ctrl+O) → `loadDocument()`
- `Qt::CTRL | Qt::SHIFT | Qt::Key_A` → `addPageToDocument()`

---

### Phase 2: PDF Loading Integration ✅ COMPLETE

#### 2.1 Implement Open PDF Flow ✅ COMPLETE
**Goal:** Load PDF file and create PDF-backed document

┌─────────────────────────────────────────────────────────────────┐
│                     PDF LOADING ARCHITECTURE                     │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────┐     ┌──────────────────┐     ┌─────────────────┐
│   MainWindow    │────▶│ DocumentManager  │────▶│    Document     │
│                 │     │                  │     │                 │
│ openPdfDocument │     │ loadDocument()   │     │ loadPdf()       │
│ (Ctrl+Shift+O)  │     │ - owns Document  │     │ createForPdf()  │
└─────────────────┘     │ - tracks path    │     │                 │
                        │ - recent docs    │     │ m_pdfProvider   │
                        └──────────────────┘     └────────┬────────┘
                                                          │
                                                          ▼
┌─────────────────────────────────────────────────────────────────┐
│                     PDF PROVIDER LAYER                           │
├─────────────────────────────────────────────────────────────────┤
│  PdfProvider (interface)     PopplerPdfProvider (implementation) │
│  ├─ pageCount()             ├─ Poppler::Document::load()         │
│  ├─ pageSize(i)             │   [FAST: only parses metadata]     │
│  ├─ renderPageToImage()     ├─ renderToImage(dpi)                │
│  ├─ outline()               │   [EXPENSIVE: renders on demand]   │
│  └─ textBoxes(i)            └─ getPage(i)->renderToImage()       │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│                   PDF RENDERING (Lazy, On-Demand)                │
├─────────────────────────────────────────────────────────────────┤
│  DocumentViewport                                                │
│  ├─ paintEvent()                                                 │
│  │   └─ renderPage() → getCachedPdfPage(pageIndex, dpi)         │
│  │                                                               │
│  ├─ m_pdfCache: QVector<PdfCacheEntry>  (capacity: 4-8)         │
│  │   ├─ getCachedPdfPage() - returns cached or renders new      │
│  │   ├─ preloadPdfCache()  - pre-renders ±1 adjacent pages      │
│  │   └─ invalidatePdfCache() - clears on zoom/doc change        │
│  │                                                               │
│  └─ Cache Strategy (SMART EVICTION):                              │
│      ┌──────────────────────────────────────────────────┐       │
│      │ Page 3 │ Page 4 │ Page 5 │ Page 6 │ Page 7 │ P8  │       │
│      │(preload)│(visible)│(visible)│(preload)│ async │async│       │
│      └──────────────────────────────────────────────────┘       │
│               ▲                                                  │
│    evicted: page FURTHEST from current (not FIFO!)              │
└─────────────────────────────────────────────────────────────────┘

PERFORMANCE CHARACTERISTICS:
─────────────────────────────
• PDF Load:     O(1) - metadata parsing only
• First Paint:  O(visible pages) - typically 1-2 pages
• Scroll:       O(1) if cache hit, async background render if miss
• Zoom:         O(visible pages) - cache invalidated, re-render
• Memory:       Bounded by cache capacity (6-12 pages)
• Scroll Back:  O(1) - smart eviction keeps nearby pages cached

OPTIMIZATION DECISIONS:
───────────────────────
1. **Async Preloading ✅ IMPLEMENTED**
   - Uses `QtConcurrent::run()` with thread-local `PopplerPdfProvider`
   - Debounced (150ms) - only fires after scroll stops
   - Each thread creates own PDF instance for thread safety
   - `QFutureWatcher` triggers `update()` when render completes
   - Result: NO main thread blocking during scroll

2. **Smart Eviction ✅ IMPLEMENTED**
   - Problem: FIFO eviction evicted pages we were about to need
   - Solution: Evict page FURTHEST from current view (distance-based)
   - Result: Scroll forward → back has NO cache misses

3. **Zoom Debounce Timer (TODO)**
   - Problem: Ctrl+wheel zoom fires many rapid events
   - Solution: Delay cache invalidation until zoom "settles"
   - Implementation:
     ```
     onZoomChanged():
       → Cancel existing debounce timer
       → Start new timer (e.g., 200-300ms)
       → On timeout: invalidatePdfCache() + re-render visible pages
     ```
   - Benefit: Avoids rendering intermediate zoom levels

4. **Cache Size ✅ INCREASED**
   - Before: 4 pages (single column), 8 pages (two column)
   - After: 6 pages (single column), 12 pages (two column)
   - Future: Configurable via Control Panel (after reconnection)

5. **Never Block UI ✅ ACHIEVED**
   - PDF opens immediately (metadata only)
   - First visible page renders synchronously (unavoidable)
   - Adjacent pages preload in background via async
   - Scroll back has cache hits (smart eviction keeps nearby pages)

**Requirements:**
- Open OS file dialog for PDF selection
- Create Document using `Document::createForPdf()`
- Use PdfProvider interface (not PopplerPdfProvider directly)
- Create new tab with DocumentViewport
- Set document on viewport
- PDF pages should render in viewport

**Files to modify:**
- `source/MainWindow.cpp` - Add PDF open handler
- `source/MainWindow.h` - Declare method

**Code flow:**
```
User presses Ctrl+Shift+O
  → MainWindow::openPdfDocument()
    → Show QFileDialog::getOpenFileName() with PDF filter
    → m_documentManager->loadDocument(path)  [handles .pdf extension]
      → Document::createForPdf(baseName, path)
        → Document::loadPdf(path)
          → PdfProvider::create(path)
            → PopplerPdfProvider (parses metadata only - FAST)
        → Document::createPagesForPdf() (creates Page objects, no rendering)
      → Takes ownership, adds to recent
    → m_tabManager->createTab(doc, title)
    → centerViewportContent()
    → [First paintEvent triggers getCachedPdfPage() for visible pages]
```

**Implementation Notes:**
- `openPdfDocument()` added to MainWindow (Ctrl+Shift+O)
- Legacy `loadPdf()` stub now redirects to `openPdfDocument()`
- Uses `DocumentManager::loadDocument()` for proper ownership
- Error dialog shown if PDF fails to load

#### 2.2 Verify PDF Rendering ✅ COMPLETE
**Goal:** Confirm PDF pages render correctly in DocumentViewport

**Test cases verified:**
- ✅ Single page PDF
- ✅ Multi-page PDF (tested with 3600+ pages)
- ✅ PDF with different page sizes
- ✅ Zooming and scrolling work correctly
- ✅ Rapid stroke drawing on PDF pages (performance fixed)

#### 2.3 Deferred Zoom Rendering 🔄 PLANNED
**Status:** Design complete, ready for implementation.

**Problem:** Current zoom implementation re-renders PDF at new DPI on every zoom event.
- Ctrl+wheel: ~5 events/second → 5 expensive PDF re-renders
- Pinch gesture: ~120 events/second → 120 expensive PDF re-renders

**Solution:** Defer rendering until gesture ends. Scale cached frame during gesture.

---

### Deferred Zoom Rendering - Detailed Design

#### Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                    UNIFIED ZOOM GESTURE SYSTEM                   │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  Input Sources (all feed into same state machine):              │
│  ├─ Ctrl+Wheel (discrete, ~5 Hz)                                │
│  ├─ Touch Pinch (continuous, ~120 Hz)                           │
│  └─ Trackpad Pinch (continuous, ~60 Hz)                         │
│                                                                  │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │                    ZoomGestureState                         ││
│  │  ├─ isActive: bool                                          ││
│  │  ├─ startZoom: qreal          (zoom level when started)     ││
│  │  ├─ targetZoom: qreal         (accumulates zoom changes)    ││
│  │  ├─ centerPoint: QPointF      (zoom center in viewport)     ││
│  │  └─ cachedFrame: QPixmap      (viewport snapshot)           ││
│  └─────────────────────────────────────────────────────────────┘│
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

#### State Machine

```
                    ┌──────────────────┐
                    │      IDLE        │
                    │  (normal render) │
                    └────────┬─────────┘
                             │
         ┌───────────────────┼───────────────────┐
         │                   │                   │
    Ctrl+Wheel         Pinch Start         Trackpad Pinch
         │                   │                   │
         ▼                   ▼                   ▼
    ┌─────────────────────────────────────────────────┐
    │                  ZOOM GESTURE                    │
    │  • Capture cachedFrame on first event           │
    │  • Update targetZoom on each event              │
    │  • paintEvent: scale cachedFrame (fast!)        │
    │  • NO PDF re-rendering during gesture           │
    └─────────────────────────┬───────────────────────┘
                              │
         ┌────────────────────┼────────────────────┐
         │                    │                    │
    Ctrl Release         Pinch End          Timeout (fallback)
    (if in gesture)                         (300ms no events)
         │                    │                    │
         ▼                    ▼                    ▼
    ┌─────────────────────────────────────────────────┐
    │                  GESTURE END                     │
    │  • m_zoomLevel = targetZoom                     │
    │  • invalidatePdfCache()                         │
    │  • Clear cachedFrame                            │
    │  • Trigger full re-render at new DPI            │
    │  • Return to IDLE                               │
    └─────────────────────────────────────────────────┘
```

#### Implementation Details

**1. New Members in DocumentViewport.h:**

```cpp
// ===== Deferred Zoom Gesture State =====
struct ZoomGestureState {
    bool isActive = false;           ///< True during zoom gesture
    qreal startZoom = 1.0;           ///< Zoom level when gesture started
    qreal targetZoom = 1.0;          ///< Target zoom (accumulates changes)
    QPointF centerPoint;             ///< Zoom center in viewport coords
    QPixmap cachedFrame;             ///< Viewport snapshot for fast scaling
    QPointF startPan;                ///< Pan offset when gesture started
    
    void reset() {
        isActive = false;
        cachedFrame = QPixmap();
    }
};

ZoomGestureState m_zoomGesture;
QTimer* m_zoomGestureTimeoutTimer = nullptr;  ///< Fallback gesture end detection
static constexpr int ZOOM_GESTURE_TIMEOUT_MS = 300;
```

**2. Gesture Start (any source):**

```cpp
void DocumentViewport::beginZoomGesture(QPointF centerPoint)
{
    if (m_zoomGesture.isActive) return;  // Already in gesture
    
    m_zoomGesture.isActive = true;
    m_zoomGesture.startZoom = m_zoomLevel;
    m_zoomGesture.targetZoom = m_zoomLevel;
    m_zoomGesture.centerPoint = centerPoint;
    m_zoomGesture.startPan = m_panOffset;
    
    // Capture current viewport as cached frame
    m_zoomGesture.cachedFrame = grab();
    
    // Start timeout timer (fallback for gesture end detection)
    m_zoomGestureTimeoutTimer->start(ZOOM_GESTURE_TIMEOUT_MS);
}
```

**3. During Gesture:**

```cpp
void DocumentViewport::updateZoomGesture(qreal scaleFactor, QPointF centerPoint)
{
    if (!m_zoomGesture.isActive) {
        beginZoomGesture(centerPoint);
    }
    
    // Accumulate zoom (multiplicative for smooth feel)
    m_zoomGesture.targetZoom *= scaleFactor;
    m_zoomGesture.targetZoom = qBound(MIN_ZOOM, m_zoomGesture.targetZoom, MAX_ZOOM);
    m_zoomGesture.centerPoint = centerPoint;
    
    // Restart timeout timer
    m_zoomGestureTimeoutTimer->start(ZOOM_GESTURE_TIMEOUT_MS);
    
    // Trigger repaint (will use cached frame scaling)
    update();
}
```

**4. Modified paintEvent (fast path during gesture):**

```cpp
void DocumentViewport::paintEvent(QPaintEvent* event)
{
    // FAST PATH: During zoom gesture, just scale the cached frame
    if (m_zoomGesture.isActive && !m_zoomGesture.cachedFrame.isNull()) {
        QPainter painter(this);
        
        // Calculate scale factor
        qreal scale = m_zoomGesture.targetZoom / m_zoomGesture.startZoom;
        
        // Calculate transform to zoom at center point
        QPointF center = m_zoomGesture.centerPoint;
        
        painter.translate(center);
        painter.scale(scale, scale);
        painter.translate(-center);
        
        // Draw scaled cached frame (fast GPU operation, may be blurry)
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawPixmap(0, 0, m_zoomGesture.cachedFrame);
        
        return;  // Skip expensive page rendering
    }
    
    // NORMAL PATH: Full rendering (existing code)
    // ... existing paintEvent code ...
}
```

**5. Gesture End:**

```cpp
void DocumentViewport::endZoomGesture()
{
    if (!m_zoomGesture.isActive) return;
    
    // Stop timeout timer
    m_zoomGestureTimeoutTimer->stop();
    
    // Apply final zoom level
    qreal finalZoom = m_zoomGesture.targetZoom;
    
    // Calculate new pan offset to keep center point fixed
    qreal scale = finalZoom / m_zoomGesture.startZoom;
    QPointF center = m_zoomGesture.centerPoint;
    QPointF docPtAtCenter = center / m_zoomGesture.startZoom + m_zoomGesture.startPan;
    m_panOffset = docPtAtCenter - center / finalZoom;
    
    // Clear gesture state BEFORE setting zoom (to avoid recursion)
    m_zoomGesture.reset();
    
    // Now set the zoom level (will invalidate PDF cache and trigger re-render)
    setZoomLevel(finalZoom);
    
    // Clamp pan and emit signals
    clampPanOffset();
    emit panChanged(m_panOffset);
}
```

**6. Input Event Handlers:**

```cpp
// Ctrl+Wheel
void DocumentViewport::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        qreal zoomDelta = event->angleDelta().y() / 120.0;
        qreal scaleFactor = qPow(1.1, zoomDelta);
        
        updateZoomGesture(scaleFactor, event->position());
        
        event->accept();
        return;
    }
    // ... existing scroll handling ...
}

// Ctrl key release
void DocumentViewport::keyReleaseEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Control && m_zoomGesture.isActive) {
        endZoomGesture();
        event->accept();
        return;
    }
    QWidget::keyReleaseEvent(event);
}

// Touch pinch (future)
void DocumentViewport::handlePinchGesture(qreal scaleFactor, QPointF center)
{
    updateZoomGesture(scaleFactor, center);
}

void DocumentViewport::handlePinchEnd()
{
    endZoomGesture();
}
```

**7. Timeout Fallback:**

```cpp
// Constructor:
m_zoomGestureTimeoutTimer = new QTimer(this);
m_zoomGestureTimeoutTimer->setSingleShot(true);
connect(m_zoomGestureTimeoutTimer, &QTimer::timeout, 
        this, &DocumentViewport::endZoomGesture);
```

#### Performance Characteristics

| Metric | Before | After |
|--------|--------|-------|
| Ctrl+wheel (5 clicks) | 5 PDF re-renders | 1 re-render at end |
| Pinch gesture (2 sec @ 120Hz) | 240 PDF re-renders | 1 re-render at end |
| Frame rate during gesture | Variable (depends on PDF) | Consistent 60+ FPS |
| Visual quality during gesture | Sharp | May be blurry |
| Visual quality after gesture | Sharp | Sharp |

#### Edge Cases

1. **Zoom gesture abandoned** (e.g., Ctrl pressed, but no wheel events):
   - Timeout timer fires after 300ms, calls `endZoomGesture()`
   - If `targetZoom == startZoom`, no actual change occurs

2. **Window loses focus during gesture**:
   - `focusOutEvent` should call `endZoomGesture()` as safety measure

3. **Document changed during gesture**:
   - `setDocument()` should call `endZoomGesture()` first

4. **Very large zoom change** (e.g., 100% → 1000%):
   - Cached frame will be very pixelated
   - This is acceptable per design decision (prioritize frame rate)

#### Files to Modify

| File | Changes |
|------|---------|
| `source/core/DocumentViewport.h` | Add `ZoomGestureState`, timer member |
| `source/core/DocumentViewport.cpp` | Implement gesture state machine, modify `paintEvent`, `wheelEvent`, add `keyReleaseEvent` |

---

### Phase 3: Insert Page (PDF-Aware)

> **Note:** Add Page was moved to Phase 1.0 as a prerequisite for Save/Load testing.
> Insert Page is more complex because it must handle PDF-backed documents correctly.

#### 3.1 Implement Insert Page
**Goal:** Insert new page after current page

**Complexity with PDF:**
- For non-PDF documents: Simply insert a blank page
- For PDF documents: Must decide what background the new page gets
  - Option A: Insert blank page (no PDF background)
  - Option B: Duplicate current page's PDF background
  - Option C: Block insertion (PDF pages are fixed)
  
  → **Decision needed during implementation**

**Requirements:**
- Get current page index from viewport
- Determine document type (PDF vs non-PDF)
- Call `Document::insertPage(currentIndex + 1)` with appropriate settings
- Trigger viewport repaint
- Optionally scroll to new page

**Code flow:**
```
User presses Ctrl+Shift+I
  → MainWindow::insertPageInDocument()
    → Get current page index from viewport
    → Get Document from viewport
    → Check if document has PDF
    → Document::insertPage(currentIndex + 1, appropriateSettings)
    → Viewport::update()
```

#### 3.2 Connect Keyboard Shortcut
**Goal:** Wire up Ctrl+Shift+I

---

### Phase 3B: Delete Page (PDF-Aware)

> **Note:** Delete Page has similar PDF complexity to Insert Page.
> For PDF-backed documents, we must decide what happens when a user deletes a page.

#### 3B.1 Implement Delete Page in Document
**Goal:** Add proper delete page support to Document class

**Complexity with PDF:**
- For non-PDF documents: Simply remove the page
- For PDF documents: Must decide behavior
  - Option A: Remove the page entirely (PDF page becomes inaccessible)
  - Option B: Clear annotations only (keep PDF background)
  - Option C: Block deletion (PDF pages are fixed)
  
  → **Decision needed during implementation**

**Note:** `Document::removePage()` exists but may need PDF-aware logic.

**Requirements:**
- Check if document has PDF
- Handle "cannot remove last page" case
- Decide behavior for PDF pages
- Emit appropriate signals for UI update

**Files to modify:**
- `source/core/Document.cpp` - Add PDF-aware delete logic if needed
- `source/MainWindow.cpp` - Connect to UI (may reuse existing deleteCurrentPage)

#### 3B.2 Test Delete Page
**Test cases:**
- Delete page in non-PDF document
- Delete page in PDF document
- Attempt to delete the last remaining page (should fail)
- Undo after delete (if undo is connected)

---

### Phase 4: Edgeless Mode

#### 4.1 Implement New Edgeless Document
**Goal:** Create new edgeless document in new tab

**Requirements:**
- Create Document using `Document::createNew(name, Mode::Edgeless)`
- Create new tab with DocumentViewport
- Set document on viewport
- Verify edgeless page renders correctly

**Code flow:**
```
User presses Ctrl+Shift+N
  → MainWindow::newEdgelessDocument()
    → Document::createNew("Untitled", Document::Mode::Edgeless)
    → Create new tab (via TabManager)
    → Set document on viewport
```

#### 4.2 Test Edgeless Behavior
**Goal:** Verify edgeless mode works correctly

**Test cases:**
- Edgeless page has large size (4096x4096 per Document.cpp)
- Drawing works across the large canvas
- Pan/zoom works correctly
- Save/load preserves edgeless mode
- Strokes are saved and loaded correctly

---

### Phase 5: LayerPanel Integration

#### 5.1 Add LayerPanel to MainWindow
**Goal:** Show LayerPanel in UI

**Requirements:**
- Create LayerPanel instance
- Add to appropriate sidebar/container
- Connect to current page

**Files to modify:**
- `source/MainWindow.cpp` - Create and place LayerPanel
- `source/MainWindow.h` - Declare LayerPanel member

#### 5.2 Connect LayerPanel to Page
**Goal:** LayerPanel updates when page changes

**Requirements:**
- When tab changes → update LayerPanel's page
- When page changes within document → update LayerPanel
- LayerPanel signals trigger viewport repaint

**Connections:**
```
TabManager::currentViewportChanged
  → Get new viewport's current page
  → LayerPanel::setCurrentPage(page)

DocumentViewport::currentPageChanged
  → Get new page
  → LayerPanel::setCurrentPage(page)

LayerPanel::layerVisibilityChanged
  → Viewport::update()

LayerPanel::activeLayerChanged
  → (Document handles this internally)
```

#### 5.3 Test Multi-Layer Editing
**Goal:** Verify multiple layers work in GUI

**Test cases:**
- Add new layer via LayerPanel
- Switch between layers, draw on each
- Toggle layer visibility
- Reorder layers
- Save/load preserves layer structure and content
- Delete layer (not last one)

---

## Reference Files

| File | Purpose |
|------|---------|
| `source/core/DocumentTests.h` | Document serialization tests (all pass) |
| `source/core/DocumentViewportTests.h` | Viewport integration tests (all pass) |
| `source/core/Document.cpp` | Document implementation with toFullJson/fromFullJson |
| `source/core/Page.cpp` | Page implementation with layer management |
| `source/ui/LayerPanel.cpp` | LayerPanel implementation (ready to integrate) |
| `source/pdf/PdfProvider.h` | PDF interface (use this, not Poppler directly) |

---

## Success Criteria

### Phase 1: Core Save/Load ✅ COMPLETE
- [x] Can add page to document (Ctrl+Shift+A) ✅
- [x] Can save multi-page document to JSON file (Ctrl+S) ✅
- [x] Can load document from JSON file (Ctrl+O) ✅
- [x] Strokes and layers preserved on save/load ✅

### Phase 2: PDF Loading ✅ COMPLETE
- [x] Can open PDF and view in DocumentViewport (Ctrl+Shift+O) ✅
- [x] PDF pages render correctly ✅
- [x] Multi-page PDF works (tested with 3600+ page document) ✅
- [x] Async PDF preloading (non-blocking) ✅
- [x] Smart cache eviction (distance-based) ✅
- [x] PDF performance matches Grid/Lines pages ✅
- [x] Memory bounded via cache eviction ✅

### Phase 3: Insert Page ✅ COMPLETE
- [x] Can insert page after current (Ctrl+Shift+I) ✅
- [x] Insert works correctly for non-PDF documents ✅
- [x] Insert behavior defined for PDF documents ✅ (blank page, no PDF background)
- [x] Page centering preserved after insert ✅
- [x] Undo stacks cleared for affected pages ✅

---

## Phase 3 Analysis: Insert Page Performance (Dec 27, 2024)

### Key Architectural Insight: Two Separate Indexes

| Field | Purpose | Affected by Insert? |
|-------|---------|---------------------|
| `Page::pageIndex` | Position in document (metadata) | Yes - but just an integer |
| `Page::pdfPageNumber` | Which PDF page provides background | **NO!** |

When inserting a blank page at position 5 in a 3600-page document:
- Pages 5-3599 shift to positions 6-3600 in the `m_pages` vector
- But their `pdfPageNumber` values **stay unchanged**!
- Page that was at index 5 with `pdfPageNumber=5` is now at index 6, but still renders PDF page 5

### Performance Analysis

**O(n) Operations (all fast):**
1. **Vector insertion**: ~3600 pointer moves (~28KB memory shift) - microseconds
2. **Page::pageIndex updates**: Optional - this field is redundant with vector position
3. **Page layout cache invalidation**: Must call `invalidatePageLayoutCache()` - O(n) rebuild on next paint

**UNAFFECTED by insert:**
- **PDF Cache**: Keyed by `pdfPageNumber`, not document position
  ```cpp
  QPixmap pdfPixmap = getCachedPdfPage(page->pdfPageNumber, dpi);
  ```
- **Stroke caches**: Per-layer, not dependent on page position

### ⚠️ Problem: Undo Stacks Keyed by Page Index

```cpp
QMap<int, QStack<PageUndoAction>> m_undoStacks;  // Keyed by document page index!
```

If we insert at page 5, the undo history for pages 5+ is now at the **wrong key**!
- Page that WAS at index 5 is now at index 6
- But its undo history is still at key 5
- If kept, undo on "page 5" would apply to the NEW blank page, not the original!

**Solution: Clear undo/redo for pages >= insertIndex**

```cpp
// After inserting at insertIndex:
for (auto it = m_undoStacks.begin(); it != m_undoStacks.end(); ) {
    if (it.key() >= insertIndex) {
        it = m_undoStacks.erase(it);
    } else {
        ++it;
    }
}
// Same for m_redoStacks
```

**Rationale:**
- Users typically work front-to-back; pages before insert are likely "done"
- Preserves undo for pages 0 to insertIndex-1
- Simple O(k) where k = pages after insert point
- Correct behavior: no stale undo applied to wrong pages

### Insert Page Behavior (Non-PDF vs PDF)

| Document Type | Insert Behavior |
|---------------|-----------------|
| **Non-PDF** | Insert blank page with document defaults |
| **PDF-backed** | Insert blank page (no PDF background) - useful for notes |

For PDF documents, inserted pages have `backgroundType = None` (or Grid/Lines per defaults), not `backgroundType = PDF`. The PDF page numbering is fixed at document creation.

### Implementation Plan

1. **MainWindow::insertPageInDocument()** (Ctrl+Shift+I)
   - Get current page index from viewport
   - Call `Document::insertPage(currentIndex + 1)`
   - Call `viewport->notifyDocumentStructureChanged()`

2. **DocumentViewport::clearUndoStacksFrom(int pageIndex)**
   - New method to clear undo/redo for pages >= pageIndex
   - Called from MainWindow after insert

3. **Document::insertPage()** - Already exists and works correctly

4. **Viewport notification** - Already have `notifyDocumentStructureChanged()`

### Files to Modify

| File | Changes |
|------|---------|
| `source/MainWindow.cpp` | Add `insertPageInDocument()` handler |
| `source/MainWindow.h` | Declare method |
| `source/core/DocumentViewport.cpp` | Add `clearUndoStacksFrom(int)` |
| `source/core/DocumentViewport.h` | Declare method |

### Delete Page (Phase 3B) - Same Pattern

When deleting page at index `deleteIndex`:
1. Clear undo for pages >= deleteIndex (same logic)
2. Call `Document::removePage(deleteIndex)`
3. Call `viewport->notifyDocumentStructureChanged()`

The undo stack clearing is the same for both insert and delete - we just clear from the affected index onward.

### Phase 3 Fixes During Testing (Dec 27, 2024)

#### Fix 1: Page Centering Lost After Insert

**Problem:** After `Ctrl+Shift+I`, pages stuck to the left edge instead of centering.

**Root Cause:** `insertPageInDocument()` called `scrollToPage()` which set pan to 
the page's left edge, not centered.

**Fix:** Removed `scrollToPage()` call, matching `addPageToDocument()` behavior.
The viewport now preserves the current pan position after insert.

#### Fix 2: Incremental Stroke Cache Memory Leak

**Problem:** Editing any page caused ~33MB (at 4K) memory increase that was never freed.

**Root Cause:** `m_currentStrokeCache` (viewport-sized QPixmap for incremental stroke
rendering) was allocated during drawing but never released in `finishStroke()`.

**Fix:** Added `m_currentStrokeCache = QPixmap();` at the end of `finishStroke()` to
release the cache. It's lazily reallocated on the next stroke start.

See `docs/MEMORY_LEAK_FIX_SUBPLAN.md` for full details.

### Phase 3B: Delete Page
- [ ] Delete page works for non-PDF documents
- [ ] Delete behavior defined for PDF documents
- [ ] Cannot delete last remaining page

### Phase 4: Edgeless Mode
- [ ] Can create edgeless document (Ctrl+Shift+N)
- [ ] Edgeless mode works correctly (large canvas, drawing, save/load)

### Phase 5: LayerPanel
- [ ] LayerPanel shows in UI
- [ ] Multi-layer editing works
- [ ] Layer changes trigger viewport repaint
- [ ] All existing DocumentTests still pass

---

## Notes

- Save/Load flow must be **modular and clear** - many future features depend on it
- Keyboard shortcuts are **temporary** - will be removed/customized later
- Use `PdfProvider` interface, not `PopplerPdfProvider` directly
- Tab creation should go through `TabManager`
- Document ownership: `DocumentManager` owns documents

---

## Keyboard Shortcut Architecture

### Decision: Distributed Shortcuts (No Hub Needed)

Qt's event propagation naturally supports shortcuts at different levels:

```
Key Press Event
    ↓
DocumentViewport (focused widget)
    ↓ (if not handled, propagates up)
MainWindow
```

### Separation Rule

| Scope | Handler | Examples |
|-------|---------|----------|
| **Page/Viewport** | DocumentViewport | P/E (tools), Ctrl+Z/Y (undo/redo), B (benchmark) |
| **Document** | MainWindow | Ctrl+S (save), Ctrl+Shift+A (add page) |
| **Application** | MainWindow | Ctrl+O (open), Ctrl+Shift+O (open PDF) |

**Principle:** Shortcuts live where the action happens.

### Implementation

MainWindow uses `QShortcut` with `Qt::ApplicationShortcut` context for guaranteed behavior:

```cpp
QShortcut* saveShortcut = new QShortcut(QKeySequence::Save, this);
saveShortcut->setContext(Qt::ApplicationShortcut);
connect(saveShortcut, &QShortcut::activated, this, &MainWindow::saveDocument);
```

This ensures the shortcut works regardless of which widget has focus.

---

## Background Settings Architecture

### Sources of Truth (Priority Order)

| Level | Purpose | Used When |
|-------|---------|-----------|
| **QSettings** | User's global preference | MainWindow loads and applies to new Documents |
| **Document.defaultXxx** | Document-level defaults | `createDefaultPage()` uses these for new pages |
| **Page properties** | Actual page settings | Rendering and serialization |
| **JSON fallbacks** | Recovery for incomplete files | Only if field is missing in JSON |

### Flow for New Pages ✅ IMPLEMENTED
```
MainWindow::addNewTab()
  → m_documentManager->createDocument() (creates doc with first page)
  → loadDefaultBackgroundSettings() from QSettings
  → Apply to doc->defaultBackgroundType, etc. (for future pages)
  → Apply to first page (already created)
  → TabManager::createTab()
```

**Note:** Since `Document::createNew()` already creates the first page, we apply
settings to both the document defaults (for future `addPage()` calls) AND to
the existing first page.

### JSON Serialization Behavior

**Grid/line settings are always saved**, even when `backgroundType` is `None`:
```json
"default_background": {
    "type": "none",
    "grid_color": "#ffc8c8c8",
    "grid_spacing": 20,
    "line_spacing": 24,
    ...
}
```

**Why this is correct:**
1. Settings are preserved for when user switches to Grid/Lines mode later
2. They don't affect rendering when type is "none" (Page::renderBackground skips them)
3. Loading correctly shows blank page when type is "none"

### Default Values

| Property | Document Default | Page Fallback |
|----------|------------------|---------------|
| backgroundType | `None` | `None` (0) |
| backgroundColor | `Qt::white` | `#ffffffff` |
| gridColor | `QColor(200,200,200)` | `#ffc8c8c8` |
| gridSpacing | `20` | `20` |
| lineSpacing | `24` | `24` |
| pageSize | `816 x 1056` | `816 x 1056` |

### QSettings Migration (doc-1 fix)

**Problem:** Old `BackgroundStyle` enum had different values than new `Page::BackgroundType`:

| Value | Old BackgroundStyle | New Page::BackgroundType |
|-------|---------------------|--------------------------|
| 0 | None | None |
| 1 | Grid | PDF |
| 2 | Lines | Custom |
| 3 | - | Grid |
| 4 | - | Lines |

This caused QSettings to load stale values incorrectly (e.g., old Grid=1 → new PDF=1).

**Fix:** Changed QSettings key from `"defaultBackgroundStyle"` to `"defaultBgType"`:
- Old key is removed on first run
- New key uses correct `Page::BackgroundType` values
- Added enum range validation (0-4)

### Grid Color in QSettings (doc-1 fix)

**Problem:** Grid color was hardcoded, not loaded from QSettings. Also, JSON used `HexArgb` format (`#ffc8c8c8`) which looked like pink when read as RGB.

**Fix:**
1. Added `defaultGridColor` to QSettings (key: `"defaultGridColor"`)
2. Updated function signatures:
   - `saveDefaultBackgroundSettings(style, bgColor, gridColor, density)`
   - `loadDefaultBackgroundSettings(style, bgColor, gridColor, density)`
3. Applied gridColor in `addNewTab()` to both Document defaults and first Page
4. Changed JSON serialization to use `HexRgb` format (`#c8c8c8`) for grid color

**QSettings Keys (for Control Panel reconnection):**
| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `defaultBgType` | int | 3 (Grid) | Page::BackgroundType enum value |
| `defaultBackgroundColor` | QString | "#FFFFFF" | Background color (HexRgb) |
| `defaultGridColor` | QString | "#C8C8C8" | Grid/line color (HexRgb) |
| `defaultBackgroundDensity` | int | 30 | Grid/line spacing in pixels |

---

## DocumentManager Integration (doc-1 fix)

### Problem
Save/Load in MainWindow used a hacky `static std::vector<std::unique_ptr<Document>>` for document ownership. This led to:
1. Memory management confusion (documents lived forever until app close)
2. No proper tracking of document state (modified, path)
3. Duplicated code (DocumentManager already had proper save/load)

### Fix
Refactored `MainWindow::saveDocument()` and `MainWindow::loadDocument()` to use `DocumentManager`:

**Save flow:**
```
MainWindow::saveDocument()
  → Check for existing path via m_documentManager->documentPath(doc)
  → Show QFileDialog for save location
  → m_documentManager->saveDocumentAs(doc, path)
    → Serializes JSON
    → Writes to file with proper error handling
    → Updates document state (clearModified)
    → Adds to recent documents
  → Update tab title
```

**Load flow:**
```
MainWindow::loadDocument()
  → Show QFileDialog for file selection
  → m_documentManager->loadDocument(filePath)
    → Reads and parses JSON
    → Deserializes Document
    → Takes ownership
    → Attempts PDF reload if referenced
    → Adds to recent documents
  → m_tabManager->createTab(doc, title)
  → centerViewportContent()
```

### DocumentManager File Format Support
Added `.json` support to `DocumentManager::loadDocument()`:
- Previously only supported `.snx` and `.pdf`
- Now supports `.json` (same internal format as `.snx`)
- Future: `.snx` will be QDataStream package with embedded binaries

### Benefits
1. **Single source of truth** for document ownership
2. **Proper lifecycle management** - documents cleaned up in destructor
3. **State tracking** - modified flags, file paths, recent documents
4. **Signal emission** - `documentSaved`, `documentLoaded` for UI updates
5. **No memory leaks** - unique_ptr ownership transferred to DocumentManager

---

## Bug Fixes (Phase 2 Testing)

### Fix 1: Crash on Application Close (Signal-During-Destruction)

**Problem:** Application crashes when closing with a document loaded.

**Root Cause (from stack trace):**
```
MainWindow::~MainWindow()
  → TabManager::~TabManager()
    → DocumentViewport destroyed
      → Signal: currentViewportChanged() emitted
        → Slot: MainWindow::updateDialDisplay() called
          → QPixmap::load() CRASH (MainWindow already partially destroyed)
```

When Qt deletes TabManager's children (DocumentViewport), signals are emitted to 
slots in MainWindow which is already being destroyed.

**Fix:** Disconnect TabManager signals in MainWindow destructor BEFORE Qt deletes children.

```cpp
MainWindow::~MainWindow() {
    // Disconnect TabManager signals before children are deleted
    if (m_tabManager) {
        disconnect(m_tabManager, nullptr, this, nullptr);
    }
    
    // ... rest of cleanup
}
```

Also add defensive null check in `updateDialDisplay()` for robustness.

---

### Fix 2: Ctrl+S Always Shows File Dialog

**Problem:** Pressing Ctrl+S on an already-saved document still shows the file dialog.

**Expected behavior:**
- **New document (no path):** Show "Save As" dialog
- **Existing document (has path):** Save directly, no dialog

**Fix:** Check if document has existing path before showing dialog.

```cpp
void MainWindow::saveDocument() {
    // ...
    QString existingPath = m_documentManager->documentPath(doc);
    
    if (!existingPath.isEmpty()) {
        // Already saved - save in-place
        if (!m_documentManager->saveDocument(doc)) {
            QMessageBox::critical(...);
        }
        // Update UI (clear modified flag)
        return;
    }
    
    // New document - show Save As dialog
    // ... existing dialog code
}
```

---

### ~~Fix 3: PDF Performance Degradation (Page Background Caching)~~ ❌ OBSOLETE

> **Status:** This section was an incorrect hypothesis. See below for actual root causes.

**Original Hypothesis (WRONG):**
Thought that `drawPixmap()` for PDF pages 360 times/second caused overhead.

**Actual Root Causes (found via investigation):**

1. **O(n²) page layout calculations** 
   - `pagePosition(i)` was O(i), called in loops
   - For 3600-page document: 1.6 million operations per stroke point!
   - **Fix:** Added `m_pageYCache` for O(1) lookup, binary search for `pageAtPoint()`

2. **PDF cache thrashing on stroke release**
   - `preloadPdfCache()` was called after every stroke finished
   - This triggered cache rebuilds during rapid drawing
   - **Fix:** Moved `preloadPdfCache()` to scroll events only, added 150ms debounce

3. **FIFO cache eviction**
   - When scrolling back, pages we just passed were evicted
   - Caused cache misses for pages we were about to need
   - **Fix:** Smart eviction - evict page FURTHEST from current view

4. **Synchronous PDF preloading**
   - Blocking main thread during scroll
   - **Fix:** Async preloading with `QtConcurrent` and thread-local PDF providers

**Actual Fixes Applied:**
- See `docs/PDF_PERFORMANCE_INVESTIGATION.md` for full details
- O(1) page position cache + binary search
- Async PDF preloading with debounce
- Distance-based smart eviction
- Cache capacity increased (6 single column, 12 two column)

**Result:** PDF performance is now equivalent to Grid/Lines pages.

---

---

## Code Review: DocumentViewport Issues (Dec 27, 2024)

After completing Phase 2, a comprehensive review of `DocumentViewport.cpp` identified the following issues.

### ✅ ALL ISSUES FIXED (Dec 27, 2024)

---

### ✅ FIXED: Thread Safety Bug in Async PDF Preload

**Problem:** `QPixmap::fromImage()` was called inside a background thread (`QtConcurrent::run`).
Qt documentation states QPixmap must only be created on the main thread.

**Fix Applied:**
1. Changed `QFutureWatcher<void>` to `QFutureWatcher<QImage>`
2. Background thread now returns `QImage` (thread-safe)
3. `QPixmap::fromImage()` is called in the `finished` handler (main thread)
4. Cache updates happen on main thread inside mutex

```cpp
// Background thread: returns QImage (safe)
QFuture<QImage> future = QtConcurrent::run([pdfPageNum, dpi, pdfPath]() -> QImage {
    PopplerPdfProvider threadPdf(pdfPath);
    return threadPdf.renderPageToImage(pdfPageNum, dpi);
});

// Finished handler (main thread): converts to QPixmap (safe)
connect(watcher, &QFutureWatcher<QImage>::finished, this, [...]() {
    QPixmap pixmap = QPixmap::fromImage(watcher->result());
    // ... add to cache under mutex ...
});
```

---

### ✅ FIXED: Inefficient Undo Stack Trimming

**Problem:** Used O(n) stack→list→stack conversion with unnecessary allocations.

**Fix Applied:** Simplified to use `QStack::remove(0)` directly (QStack inherits from QVector):

```cpp
while (stack.size() > MAX_UNDO_PER_PAGE) {
    stack.remove(0);  // Remove oldest entry at bottom
}
```

---

### ✅ FIXED: m_cachedDpi Data Race

**Problem:** `m_cachedDpi` was written in background thread under mutex but could be read unsafely.

**Fix Applied:** As a side effect of the QPixmap fix, all cache mutations now happen on the 
main thread (in the finished handler), eliminating the data race. All accesses are now either:
- Single-threaded (main thread only), or
- Protected by mutex

---

### ✅ FIXED: Debug Code and Style Issues

**Cleaned up:**
- Removed empty lines after opening braces
- Removed commented-out debug code (`painter.fillRect`, `return; DO NOTHING ELSE`)
- Removed stray `qDebug()` comments
- Fixed double empty lines

---

### ✅ FIXED: Duplicate pagePosition() Call

**Problem:** `pagePosition(pageIdx)` was called twice for partial updates.

**Fix Applied:** Moved the call before the `isPartialUpdate` check so it's only called once:

```cpp
QPointF pos = pagePosition(pageIdx);  // Call once, reuse

if (isPartialUpdate) {
    QRectF pageRectInViewport = QRectF(
        (pos.x() - m_panOffset.x()) * m_zoomLevel,
        // ...
    );
    // ...
}

painter.translate(pos);  // Reuse
```

---

### Summary Table

| Priority | Issue | Status |
|----------|-------|--------|
| 🔴 CRITICAL | QPixmap in background thread | ✅ FIXED |
| 🟡 MEDIUM | Inefficient trimUndoStack | ✅ FIXED |
| 🟡 MEDIUM | m_cachedDpi data race | ✅ FIXED |
| 🟢 MINOR | Debug code cleanup | ✅ FIXED |
| 🟢 MINOR | Duplicate pagePosition call | ✅ FIXED |

---

## Zoom Optimization: Deferred Rendering (Dec 27, 2024)

### ✅ IMPLEMENTED: Deferred Zoom Rendering

**Problem:** Rapid zoom operations (Ctrl+wheel, future touch pinch) triggered expensive 
PDF re-renders on every event, causing lag and dropped frames.

**Solution:** Deferred zoom rendering with cached viewport snapshot.

### Design

**During Gesture:**
1. On first zoom event, capture viewport snapshot via `grab()`
2. Scale the cached snapshot during gesture (may be blurry, but fast)
3. No PDF/stroke rendering during gesture → guaranteed 60+ FPS

**On Gesture End:**
1. Detected via Ctrl key release OR timeout (300ms fallback)
2. Apply final zoom/pan transformation
3. Invalidate PDF cache (DPI changed)
4. Trigger full re-render at correct DPI
5. Preload PDF cache for new zoom level

### Public API (for future gesture modules)

```cpp
// These can be called by any input source (Ctrl+wheel, touch, trackpad, SDL controller)
void beginZoomGesture(QPointF centerPoint);     // Capture snapshot
void updateZoomGesture(qreal scaleFactor, QPointF centerPoint);  // Scale display
void endZoomGesture();                          // Apply final zoom
bool isZoomGestureActive() const;               // Query state
```

### Implementation Details

**New members in DocumentViewport:**
```cpp
struct ZoomGestureState {
    bool isActive = false;           // True during gesture
    qreal startZoom = 1.0;           // Zoom when gesture started
    qreal targetZoom = 1.0;          // Accumulated target zoom
    QPointF centerPoint;             // Zoom center (viewport coords)
    QPixmap cachedFrame;             // Viewport snapshot
    QPointF startPan;                // Pan offset when started
};
ZoomGestureState m_zoomGesture;
QTimer* m_zoomGestureTimeoutTimer;   // Fallback for gesture end
```

**paintEvent fast path:**
```cpp
if (m_zoomGesture.isActive && !m_zoomGesture.cachedFrame.isNull()) {
    qreal relativeScale = m_zoomGesture.targetZoom / m_zoomGesture.startZoom;
    painter.fillRect(rect(), QColor(64, 64, 64));
    // Scale cached frame around zoom center
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);  // Speed > quality
    painter.drawPixmap(...scaled...);
    return;  // Skip expensive rendering
}
```

**Gesture end triggers:**
1. `keyReleaseEvent(Qt::Key_Control)` → `endZoomGesture()`
2. Timeout timer (3s) → `endZoomGesture()` (fallback)
3. `focusOutEvent()` → `endZoomGesture()` (window loses focus)
4. `resizeEvent()` → `endZoomGesture()` (cached frame size invalid)
5. `setDocument()` → reset gesture (cached frame from old document)

### Edge Cases Handled

| Scenario | Handling |
|----------|----------|
| Window loses focus during gesture | `focusOutEvent` ends gesture |
| Window resized during gesture | `resizeEvent` ends gesture (frame size mismatch) |
| Document changed during gesture | `setDocument` resets gesture state |
| Division by zero (startZoom=0) | Guard in paintEvent skips fast path |
| Destructor during gesture | Explicit timer stop and frame cleanup |

### Integration with Future Gesture Module

Touch/trackpad gestures will be implemented in a separate module that calls:
```cpp
// Touch gesture module (future)
void onPinchUpdate(qreal scale, QPointF center) {
    documentViewport->updateZoomGesture(scale / lastScale, center);
}

void onPinchEnd() {
    documentViewport->endZoomGesture();
}
```

No changes to DocumentViewport needed for touch support - just call the API.

---

*Subplan for doc-1 task in SpeedyNote Phase 3 migration*

