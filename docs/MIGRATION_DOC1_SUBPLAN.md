# doc-1: Document Loading Integration Subplan

> **Purpose:** Complete document loading/saving integration with DocumentViewport
> **Created:** Dec 24, 2024
> **Status:** 🔄 IN PROGRESS (Phase 1.0 complete)

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
| `Ctrl+Shift+I` | Insert Page | Inserts new page after current page |

---

## Task Breakdown

### Phase 1: Core Save/Load Infrastructure

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

### Phase 2: PDF Loading Integration

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

#### 2.2 Verify PDF Rendering
**Goal:** Confirm PDF pages render correctly in DocumentViewport

**Test cases:**
- Single page PDF
- Multi-page PDF
- PDF with different page sizes
- Zooming and scrolling work correctly

#### 2.3 Implement Zoom Debounce (Optimization)
**Goal:** Prevent unnecessary re-renders during rapid zoom operations

**Problem:** Ctrl+wheel zoom fires many events in quick succession. Current behavior:
- Each zoom level change invalidates PDF cache
- Re-renders all visible pages at new DPI
- Wastes CPU on intermediate zoom levels user doesn't care about

**Solution:** Add debounce timer to `DocumentViewport`:
```cpp
// In DocumentViewport.h:
QTimer m_zoomDebounceTimer;

// In constructor:
m_zoomDebounceTimer.setSingleShot(true);
m_zoomDebounceTimer.setInterval(250);  // 250ms settle time
connect(&m_zoomDebounceTimer, &QTimer::timeout, this, [this]() {
    invalidatePdfCache();
    preloadPdfCache();
    update();
});

// In setZoomLevel():
// Don't invalidate immediately - just mark that zoom changed
m_zoomDebounceTimer.start();  // Restarts timer on each zoom event
```

**Files to modify:**
- `source/core/DocumentViewport.h` - Add timer member
- `source/core/DocumentViewport.cpp` - Implement debounce logic

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

### Phase 1: Core Save/Load
- [x] Can add page to document (Ctrl+Shift+A) - prerequisite ✅
- [x] Can save multi-page document to JSON file (Ctrl+S) ✅
- [x] Can load document from JSON file (Ctrl+O) ✅
- [ ] Strokes and layers preserved on save/load (needs testing)

### Phase 2: PDF Loading
- [ ] Can open PDF and view in DocumentViewport (Ctrl+Shift+O)
- [ ] PDF pages render correctly
- [ ] Multi-page PDF works

### Phase 3: Insert Page
- [ ] Can insert page after current (Ctrl+Shift+I)
- [ ] Insert works correctly for non-PDF documents
- [ ] Insert behavior defined for PDF documents

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

### Fix 3: PDF Performance Degradation (Page Background Caching)

**Problem:** Rapid strokes are delayed when a PDF is loaded, but not on Grid/Lines pages.

**Root Cause Analysis:**

The old architecture had TWO separate widgets:
- `InkCanvas`: Rendered background (PDF/grid/lines) - only repainted when background changed
- `VectorCanvas`: Transparent overlay for strokes - repainted during drawing

The new `DocumentViewport` merged everything into ONE widget. During each `paintEvent()`:
```
Current (slow for PDF):
  paintEvent() → renderPage()
    → fillRect(backgroundColor)     ← Re-done every paint
    → drawPixmap(pdfFromCache)      ← Re-blitted every paint (overhead!)
    → drawLines(grid)               ← Re-drawn every paint
    → layer->renderWithZoomCache()  ← Stroke cache (fast)
```

Even though the PDF is cached, calling `drawPixmap()` for the full page 360 times/second has overhead.
Grid/Lines are lightweight vector ops, so they don't show the same slowdown.

**Solution: Add Page Background Composite Cache**

Pre-render the entire page background (color + PDF + grid/lines) to a single pixmap:

```
Desired (fast for all background types):
  paintEvent() → renderPage()
    → drawPixmap(cachedPageBackground)  ← Single blit (fast!)
    → layer->renderWithZoomCache()      ← Stroke cache (fast)
```

**Implementation Plan:**

1. **Add to DocumentViewport.h:**
   ```cpp
   // Page background composite cache
   struct PageBackgroundCache {
       int pageIndex = -1;
       qreal zoom = 0;
       qreal dpr = 0;
       QPixmap pixmap;
       bool isValid(int idx, qreal z, qreal d) const {
           return pageIndex == idx && qFuzzyCompare(zoom, z) && qFuzzyCompare(dpr, d);
       }
   };
   QVector<PageBackgroundCache> m_pageBackgroundCaches;  // Per visible page
   int m_backgroundCacheCapacity = 4;  // Same as PDF cache
   ```

2. **Add helper methods:**
   ```cpp
   QPixmap getCachedPageBackground(int pageIndex);
   void invalidateBackgroundCaches();
   void invalidateBackgroundCache(int pageIndex);
   ```

3. **Modify renderPage():**
   ```cpp
   void renderPage(QPainter& painter, Page* page, int pageIndex) {
       // 1. Get or build background cache
       QPixmap bgCache = getCachedPageBackground(pageIndex);
       if (!bgCache.isNull()) {
           painter.drawPixmap(0, 0, bgCache);
       }
       
       // 2. Render strokes on top (already cached)
       for (VectorLayer* layer : page->layers()) {
           layer->renderWithZoomCache(painter, ...);
       }
       
       // 3. Render objects, border, etc.
   }
   ```

4. **getCachedPageBackground() builds cache if needed:**
   ```cpp
   QPixmap getCachedPageBackground(int pageIndex) {
       // Check existing cache
       for (auto& cache : m_pageBackgroundCaches) {
           if (cache.isValid(pageIndex, m_zoomLevel, devicePixelRatioF())) {
               return cache.pixmap;
           }
       }
       
       // Build new cache: render background + PDF + grid to pixmap
       QPixmap bg = renderPageBackgroundToPixmap(pageIndex);
       
       // Store in cache (FIFO eviction if full)
       // ...
       
       return bg;
   }
   ```

5. **Invalidate cache when:**
   - `setZoomLevel()` changes zoom
   - `setDocument()` changes document
   - Page background settings change

**Expected Result:**
- PDF pages: Same performance as Grid/Lines pages
- During rapid strokes: Only 2 blits per page (background + strokes)
- Background only re-rendered when zoom/settings change

**Files to modify:**
- `source/core/DocumentViewport.h` - Add cache structures
- `source/core/DocumentViewport.cpp` - Implement caching logic

---

---

*Subplan for doc-1 task in SpeedyNote Phase 3 migration*

