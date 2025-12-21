# Phase 0.1: MainWindow ↔ InkCanvas Interaction Map

> **Purpose:** Document all interactions between MainWindow and InkCanvas for the viewport migration.
> **Generated:** Phase 0.1 of viewport architecture migration
> **Total interactions:** ~318 method calls, 18 signal/slot connections

---

## 1. SIGNAL/SLOT CONNECTIONS (18 total)

These are the reactive bindings that need equivalent signals in DocumentViewport.

| Signal | Slot/Lambda | Purpose | Priority |
|--------|-------------|---------|----------|
| `InkCanvas::zoomChanged` | `MainWindow::handleTouchZoomChange` | Sync zoom UI when touch gesture changes zoom | HIGH |
| `InkCanvas::panChanged` | `MainWindow::handleTouchPanChange` | Sync pan sliders when touch gesture changes pan | HIGH |
| `InkCanvas::touchGestureEnded` | `MainWindow::handleTouchGestureEnd` | Cleanup after touch gesture | MEDIUM |
| `InkCanvas::touchPanningChanged` | `MainWindow::handleTouchPanningChanged` | Performance optimization during touch pan | MEDIUM |
| `InkCanvas::ropeSelectionCompleted` | `MainWindow::showRopeSelectionMenu` | Show context menu after lasso selection | MEDIUM |
| `InkCanvas::pdfLinkClicked` | Lambda → `switchPage()` | Navigate when PDF internal link clicked | HIGH |
| `InkCanvas::pdfLoaded` | Lambda → refresh outline | Update PDF outline sidebar | HIGH |
| `InkCanvas::autoScrollRequested` | `MainWindow::onAutoScrollRequested` | Auto page switch at scroll threshold | HIGH |
| `InkCanvas::earlySaveRequested` | `MainWindow::onEarlySaveRequested` | Proactive save before page switch | MEDIUM |
| `InkCanvas::markdownNotesUpdated` | `MainWindow::onMarkdownNotesUpdated` | Refresh markdown sidebar | LOW |
| `InkCanvas::highlightDoubleClicked` | `MainWindow::onHighlightDoubleClicked` | Open/create markdown note from highlight | LOW |
| `InkCanvas::pdfTextSelectionCleared` | `MainWindow::onPdfTextSelectionCleared` | UI state update | LOW |

---

## 2. SETTERS (87 total) - MainWindow → InkCanvas

### 2.1 Tool & Drawing State
```cpp
// [MIGRATION:TOOL] Tool selection - must route to correct Page/Layer
canvas->setTool(ToolType::Pen)           // 6 occurrences
canvas->setTool(ToolType::Marker)        // 2 occurrences
canvas->setTool(ToolType::Eraser)        // 2 occurrences
canvas->setTool(ToolType::VectorPen)     // 2 occurrences
canvas->setTool(ToolType::VectorEraser)  // 1 occurrence
canvas->setPenColor(QColor)              // 10 occurrences
canvas->setPenThickness(qreal)           // 2 occurrences
canvas->setStraightLineMode(bool)        // 5 occurrences
canvas->setRopeToolMode(bool)            // 6 occurrences
canvas->setPictureSelectionMode(bool)    // 1 occurrence
canvas->setPdfTextSelectionEnabled(bool) // 5 occurrences
```

### 2.2 View State (Pan/Zoom)
```cpp
// [MIGRATION:VIEW] These become DocumentViewport methods
canvas->setZoom(int)                     // 4 occurrences
canvas->setPanX(int)                     // 2 occurrences
canvas->setPanY(int)                     // 2 occurrences
canvas->setLastZoomLevel(int)            // 3 occurrences - per-tab state
canvas->setLastPanX(int)                 // 5 occurrences - per-tab state
canvas->setLastPanY(int)                 // 5 occurrences - per-tab state
```

### 2.3 Document/Page State
```cpp
// [MIGRATION:DOCUMENT] These become Document methods
canvas->setSaveFolder(QString)           // 8 occurrences
canvas->setLastActivePage(int)           // 2 occurrences
canvas->setLastAccessedPage(int)         // 3 occurrences
canvas->setEdited(bool)                  // 1 occurrence
canvas->setBookmarks(QStringList)        // 1 occurrence
canvas->setBackground(QString, int)      // 1 occurrence
canvas->setBackgroundStyle(BackgroundStyle) // 1 occurrence
canvas->setBackgroundColor(QColor)       // 1 occurrence
canvas->setBackgroundDensity(int)        // 1 occurrence
canvas->setPDFRenderDPI(int)             // 1 occurrence
```

### 2.4 Touch/Gesture State
```cpp
// [MIGRATION:INPUT] Input handling configuration
canvas->setTouchGestureMode(TouchGestureMode) // 4 occurrences
canvas->setCtrlKeyPhysicallyPressed(bool)     // 2 occurrences
```

### 2.5 Widget Size
```cpp
// [MIGRATION:VIEW] Widget sizing
canvas->setMaximumSize(QSize)            // 2 occurrences
```

---

## 3. GETTERS (100 total) - InkCanvas → MainWindow

### 3.1 Tool & Drawing State Queries
```cpp
// [MIGRATION:TOOL] Tool state queries
canvas->getCurrentTool()                 // 6 occurrences
canvas->getPenColor()                    // 6 occurrences
canvas->getPenThickness()                // 4 occurrences
canvas->isStraightLineMode()             // 4 occurrences
canvas->isRopeToolMode()                 // 4 occurrences
canvas->isPictureSelectionMode()         // 1 occurrence
canvas->isPdfTextSelectionEnabled()      // 3 occurrences
canvas->hasSelectedPdfText()             // 1 occurrence
```

### 3.2 View State Queries
```cpp
// [MIGRATION:VIEW] View state - becomes DocumentViewport methods
canvas->getZoom()                        // 4 occurrences
canvas->getCanvasSize()                  // 3 occurrences
canvas->getLastZoomLevel()               // 2 occurrences
canvas->getLastPanX()                    // 2 occurrences
canvas->getLastPanY()                    // 2 occurrences
canvas->getAutoscrollThreshold()         // 3 occurrences
```

### 3.3 Document State Queries
```cpp
// [MIGRATION:DOCUMENT] Document queries - becomes Document methods
canvas->isEdited()                       // 6 occurrences
canvas->getSaveFolder()                  // 7 occurrences
canvas->getDisplayPath()                 // 3 occurrences
canvas->getNotebookId()                  // 4 occurrences
canvas->getLastActivePage()              // 1 occurrence
canvas->getBuffer()                      // 1 occurrence - direct pixmap access!
canvas->getBackgroundImage()             // 1 occurrence
canvas->getAllMarkdownNotes()            // 1 occurrence
```

### 3.4 PDF State Queries
```cpp
// [MIGRATION:DOCUMENT] PDF-specific - part of Document
canvas->isPdfLoadedFunc()                // 10 occurrences
canvas->getTotalPdfPages()               // 6 occurrences
canvas->getPdfDocument()                 // 4 occurrences - direct Poppler access!
canvas->getPdfPath()                     // 5 occurrences
```

### 3.5 Touch/Gesture State Queries
```cpp
// [MIGRATION:INPUT] Touch state
canvas->getTouchGestureMode()            // 1 occurrence
canvas->isTouchPanningActive()           // 1 occurrence
```

### 3.6 Sub-component Access
```cpp
// [MIGRATION:COMPONENTS] Direct component access - needs careful handling
canvas->getVectorCanvas()                // 4 occurrences
canvas->getPictureManager()              // 4 occurrences
canvas->findHighlightById(QString)       // 1 occurrence
```

---

## 4. ACTION METHODS (26 total)

### 4.1 Page Loading/Saving
```cpp
// [MIGRATION:PAGE] Core page operations - becomes Document/Page methods
canvas->loadPage(int)                    // 2 occurrences
canvas->loadPdfPage(int)                 // 4 occurrences
canvas->saveToFile(int)                  // 4 occurrences
canvas->saveCombinedWindowsForPage(int)  // 3 occurrences
canvas->loadNotebookMetadata()           // 2 occurrences
canvas->saveBackgroundMetadata()         // 1 occurrence
canvas->insertPageIntoCache(int, QPixmap) // 1 occurrence
```

### 4.2 PDF Operations
```cpp
// [MIGRATION:DOCUMENT] PDF loading
canvas->loadPdf(QString)                 // 4 occurrences
canvas->loadPdfPreviewAsync(int)         // 1 occurrence
canvas->clearPdf()                       // 1 occurrence
canvas->clearPdfCache()                  // 1 occurrence
canvas->handleMissingPdf(QWidget*)       // 3 occurrences
```

### 4.3 Content Operations
```cpp
// [MIGRATION:PAGE] Content clearing
canvas->clearCurrentPage()               // 1 occurrence
canvas->clearInProgressLasso()           // 1 occurrence
canvas->clearPdfTextSelection()          // 1 occurrence
```

### 4.4 Tool Adjustments
```cpp
// [MIGRATION:TOOL] Tool state changes
canvas->adjustAllToolThicknesses(qreal)  // 1 occurrence - zoom-dependent thickness
```

### 4.5 Deferred Operations
```cpp
// [MIGRATION:SAVE] Save/sync operations
canvas->flushPendingMetadataSave()       // 1 occurrence
canvas->flushPendingSpnSync()            // 1 occurrence
```

---

## 5. TAB/CANVAS MANAGEMENT

### 5.1 Canvas Lookup
```cpp
// [MIGRATION:TABS] Tab management - stays in MainWindow
pageMap[canvas] = pageNumber             // Multiple occurrences
currentCanvas()                          // ~200+ occurrences (returns active InkCanvas*)
```

### 5.2 Canvas Lifecycle
```cpp
// [MIGRATION:TABS] Canvas creation/destruction
new InkCanvas(parent)                    // In addNewTab()
delete canvas                            // On tab close
disconnect(canvas, nullptr, this, nullptr) // Cleanup
```

---

## 6. PRIORITY CLASSIFICATION FOR MIGRATION

### HIGH PRIORITY (Core functionality, migrate first)
- Page navigation: `loadPage`, `loadPdfPage`, `switchPage`
- View state: `setZoom`, `setPanX`, `setPanY`, zoom/pan signals
- Document state: `isEdited`, `saveToFile`, `getSaveFolder`
- PDF loading: `loadPdf`, `isPdfLoadedFunc`, `getTotalPdfPages`

### MEDIUM PRIORITY (Important features)
- Tool state: `setTool`, `getCurrentTool`, color/thickness
- Touch gestures: All touch-related signals and methods
- Lasso/selection: `setRopeToolMode`, `ropeSelectionCompleted`
- Autoscroll: `autoScrollRequested`, `getAutoscrollThreshold`

### LOW PRIORITY (Can work with adapters initially)
- Markdown notes: `markdownNotesUpdated`, `getAllMarkdownNotes`
- Highlights: `highlightDoubleClicked`, `findHighlightById`
- Picture windows: `getPictureManager`
- Background customization: `setBackgroundStyle`, etc.

---

## 7. DANGEROUS PATTERNS (Need special attention)

### 7.1 Direct Buffer Access
```cpp
canvas->getBuffer()  // Returns raw QPixmap - tight coupling!
```
**Risk:** New architecture should not expose raw buffers.

### 7.2 Direct Poppler Access
```cpp
canvas->getPdfDocument()  // Returns Poppler::Document* 
```
**Risk:** PDF document should be owned by Document class, not exposed.

### 7.3 Combined Canvas Logic in MainWindow
```cpp
// Found in saveCurrentPageConcurrent() - MainWindow knows about combined canvas!
int singlePageHeight = buffer.height() / 2;
QPixmap topPagePixmap = buffer.copy(0, 0, bufferWidth, singlePageHeight);
```
**Risk:** MainWindow should NOT know about internal canvas layout.

---

## 8. NEXT STEPS

After Phase 0.1 (this document):
- [ ] Phase 0.2: Map all `isCombinedCanvas` locations in InkCanvas.cpp
- [ ] Phase 0.3: Document page-related widgets in MainWindow

---

*Document generated for SpeedyNote viewport migration project*
