# Phase 0.2: Combined Canvas Code Locations

> **Purpose:** Map all combined canvas special-case code that will be eliminated by the new architecture.
> **Generated:** Phase 0.2 of viewport architecture migration
> **Total occurrences:** ~150 references across InkCanvas.cpp + ~20 in MainWindow.cpp

---

## 1. DETECTION PATTERN (Repeated 11+ times)

The same detection logic is **copy-pasted** throughout the codebase:

```cpp
// [COMBINED-CANVAS:DETECTION] - This pattern appears 11+ times!
bool isCombinedCanvas = false;
int singlePageHeight = buffer.height();

if (!backgroundImage.isNull() && buffer.height() >= backgroundImage.height() * 1.8) {
    isCombinedCanvas = true;
    singlePageHeight = backgroundImage.height() / 2;
} else if (buffer.height() > 1400) { // Fallback heuristic for tall buffers
    isCombinedCanvas = true;
    singlePageHeight = buffer.height() / 2;
}
```

### Why This Is Bad:
1. **Magic numbers:** 1.8, 1400, 2000 scattered everywhere
2. **Code duplication:** Same logic repeated 11+ times
3. **Fragile heuristics:** "height > 1400" can break on different DPI/screen sizes
4. **No single source of truth:** If detection logic changes, must update everywhere

---

## 2. LOCATIONS IN InkCanvas.cpp (11 blocks)

### 2.1 saveToFile() - Lines ~2515-2560
**Purpose:** Split combined canvas into two halves and save separately
```cpp
// [COMBINED-CANVAS:SAVE] Splits buffer into top/bottom halves
if (isCombinedCanvas) {
    QPixmap currentPageBuffer = buffer.copy(0, 0, bufferWidth, singlePageHeight);
    QPixmap nextPageBuffer = buffer.copy(0, singlePageHeight, bufferWidth, singlePageHeight);
    // Save both halves to separate files
}
```
**New architecture:** Document.savePages() handles this naturally

---

### 2.2 loadPage() - Lines ~2684-2689
**Purpose:** Skip buffer resize if combined canvas
```cpp
// [COMBINED-CANVAS:LOAD] Don't resize buffer in combined mode
bool isCombinedCanvas = (buffer.height() >= backgroundImage.height() * 1.8);
if (!isCombinedCanvas && backgroundImage.size() != buffer.size()) {
    // Resize buffer
}
```
**New architecture:** Each Page has its own fixed-size buffer

---

### 2.3 clearCurrentPage() - Lines ~2889-2894
**Purpose:** Only clear top half in combined mode
```cpp
// [COMBINED-CANVAS:CLEAR] Clear only upper half
if (isCombinedCanvasMode()) {
    int singlePageHeight = getSinglePageHeight();
    painter.fillRect(0, 0, buffer.width(), singlePageHeight, Qt::transparent);
}
```
**New architecture:** Page.clear() clears entire page

---

### 2.4 loadPdfTextBoxes() - Lines ~4488-4530
**Purpose:** Load text boxes for both pages, use negative cache key
```cpp
// [COMBINED-CANVAS:PDF-TEXT] Different cache key and loading strategy
int cacheKey = isCombinedCanvas ? -(pageNumber + 1) : pageNumber;
if (isCombinedCanvas) {
    loadPdfTextBoxesForCombinedCanvas(pageNumber, singlePageHeight, newEntry);
} else {
    loadPdfTextBoxesForSinglePage(pageNumber, newEntry);
}
```
**New architecture:** Each Page loads its own text boxes

---

### 2.5 mapWidgetToPdfCoordinates() - Lines ~4675-4695
**Purpose:** Adjust Y coordinate for bottom-half text
```cpp
// [COMBINED-CANVAS:COORD-MAP] Y offset for bottom page
if (isCombinedCanvas && currentTextPageNumberSecond >= 0) {
    if (adjustedPoint.y() >= singlePageHeight) {
        targetPageNumber = currentTextPageNumberSecond;
        finalAdjustedPoint.setY(adjustedPoint.y() - singlePageHeight);
    }
}
```
**New architecture:** Viewport handles coordinate transforms per-page

---

### 2.6 mapPdfToWidgetCoordinates() - Lines ~4727-4755
**Purpose:** Add Y offset when mapping from bottom page
```cpp
// [COMBINED-CANVAS:COORD-MAP] Add offset for bottom page
if (isCombinedCanvas && pageNumber != -1) {
    if (pageNumber > basePage && currentTextPageNumberSecond >= 0) {
        yOffset = singlePageHeight; // Shift down to bottom half
    }
}
```
**New architecture:** Viewport handles coordinate transforms per-page

---

### 2.7 updatePdfTextSelection() - Lines ~4832-4840
**Purpose:** Handle selection across two pages
```cpp
// [COMBINED-CANVAS:SELECTION] Different single page height calculation
if (!backgroundImage.isNull() && buffer.height() >= backgroundImage.height() * 1.8) {
    isCombinedCanvas = true;
    singlePageHeight = backgroundImage.height(); // BUG? Different from others
}
```
**New architecture:** Selection is per-page, viewport handles multi-page selection

---

### 2.8 getTextBoxesInSelection() - Lines ~5034-5105
**Purpose:** Complex per-page coordinate handling for selection
```cpp
// [COMBINED-CANVAS:SELECTION] Most complex combined canvas code
if (!isCombinedCanvas) {
    // Simple single-page logic
} else {
    // Complex: determine which page(s) selection spans
    // Handle top half vs bottom half coordinates
    // Scale coordinates differently per page
}
```
**New architecture:** Viewport iterates visible pages, asks each Page for selection

---

### 2.9 loadCombinedWindowsForPage() - Lines ~6554-6585
**Purpose:** Load picture windows for both pages, offset Y for bottom half
```cpp
// [COMBINED-CANVAS:WINDOWS] Load and merge windows from two pages
if (isCombinedCanvas) {
    QList<PictureWindow*> topHalfPictureWindows = loadPictureWindowsForPage(pageNumber);
    QList<PictureWindow*> bottomHalfPictureWindows = loadPictureWindowsForPage(pageNumber + 1);
    // Offset bottom half windows by singlePageHeight
}
```
**New architecture:** Each Page owns its windows, no merging needed

---

### 2.10 saveCombinedWindowsForPage() - Lines ~6598-6650
**Purpose:** Sort windows into top/bottom halves and save separately
```cpp
// [COMBINED-CANVAS:WINDOWS] Split windows by Y position
if (isCombinedCanvas) {
    for (PictureWindow* window : allPictureWindows) {
        if (window->y() < singlePageHeight) {
            topHalfPictureWindows.append(window);
        } else {
            bottomHalfPictureWindows.append(window);
            // Adjust Y coordinate before saving
        }
    }
}
```
**New architecture:** Each Page saves its own windows directly

---

### 2.11 checkAutoscrollThreshold() - Lines ~7654-7665
**Purpose:** Detect page switch threshold
```cpp
// [COMBINED-CANVAS:AUTOSCROLL] Calculate threshold for page switching
if (!isCombinedCanvas || singlePageHeight == 0) {
    return; // No autoscroll for single-page mode
}
// Calculate forward/backward thresholds based on singlePageHeight
```
**New architecture:** Viewport knows page boundaries directly

---

### 2.12 getAutoscrollThreshold() - Lines ~7765-7777
**Purpose:** Return threshold for MainWindow
```cpp
// [COMBINED-CANVAS:AUTOSCROLL] Return threshold or 0
if (!isCombinedCanvas || singlePageHeight == 0) {
    return 0;
}
return singlePageHeight - backwardOffset;
```
**New architecture:** Viewport returns page boundary positions

---

## 3. LOCATIONS IN MainWindow.cpp (~20 occurrences)

### 3.1 saveCurrentPageConcurrent() - Lines ~2129-2196
**Purpose:** Split buffer and save both halves concurrently
```cpp
// [COMBINED-CANVAS:MAINWINDOW-SAVE] MainWindow knows about buffer splitting!
bool isCombinedCanvas = false;
if (!backgroundImage.isNull() && bufferCopy.height() >= backgroundImage.height() * 1.8) {
    isCombinedCanvas = true;
} else if (bufferCopy.height() > 2000) {
    isCombinedCanvas = true;
}

int singlePageHeight = isCombinedCanvas ? backgroundImage.height() / 2 : bufferCopy.height();

if (isCombinedCanvas) {
    topPagePixmap = bufferCopy.copy(0, 0, bufferWidth, singlePageHeight);
    bottomPagePixmap = bufferCopy.copy(0, singlePageHeight, bufferWidth, singlePageHeight);
}
```
**CRITICAL:** MainWindow should NOT know about internal canvas layout!

---

### 3.2 switchPage() - Lines ~1897-1912
**Purpose:** Calculate pan position after page switch
```cpp
// [COMBINED-CANVAS:MAINWINDOW-PAN] Uses autoscroll threshold
int threshold = canvas->getAutoscrollThreshold();
// Calculate backward switch offset
```
**New architecture:** Viewport handles pan position per-page

---

### 3.3 onAutoScrollRequested() - Lines ~8915-8920
**Purpose:** Detect combined canvas for auto-scroll handling
```cpp
// [COMBINED-CANVAS:MAINWINDOW-AUTOSCROLL] Duplicates detection logic!
if (!backgroundImage.isNull() && buffer.height() >= backgroundImage.height() * 1.8) {
    isCombinedCanvas = true;
} else if (buffer.height() > 1400) {
    isCombinedCanvas = true;
}
```
**New architecture:** Viewport emits page change signals directly

---

## 4. HELPER METHODS (Combined Canvas Specific)

These methods exist ONLY because of the combined canvas hack:

| Method | Lines | Purpose |
|--------|-------|---------|
| `isCombinedCanvasMode()` | 6699-6710 | Detect if combined mode |
| `getSinglePageHeight()` | 6712-6720 | Calculate single page height |
| `getPageNumberForCanvasY()` | 6722-6735 | Determine page from Y coordinate |
| `loadCombinedWindowsForPage()` | 6548-6588 | Load windows for both pages |
| `saveCombinedWindowsForPage()` | 6591-6690 | Save windows by Y position |
| `loadPdfTextBoxesForCombinedCanvas()` | 4599-4660 | Load text boxes for both pages |

**All of these can be deleted** in the new architecture.

---

## 5. DATA FLOW PROBLEMS

### Current (Broken) Flow:
```
MainWindow calls switchPage(N)
    → InkCanvas.loadPage(N) creates double-height buffer
    → Buffer contains page N (top) + page N+1 (bottom)
    → Every operation must check: "Am I in top or bottom half?"
    → Save must split buffer back into two files
```

### New (Clean) Flow:
```
MainWindow calls viewport.goToPage(N)
    → Viewport scrolls to show page N
    → Page N and N+1 are separate objects
    → Each Page handles its own operations
    → Each Page saves to its own file
```

---

## 6. SUMMARY STATISTICS

| Category | Count | Files |
|----------|-------|-------|
| Detection blocks (`bool isCombinedCanvas = false`) | 11 | InkCanvas.cpp |
| Detection blocks in MainWindow | 2 | MainWindow.cpp |
| `if (isCombinedCanvas)` branches | 6 | InkCanvas.cpp |
| `if (!isCombinedCanvas)` branches | 1 | InkCanvas.cpp |
| Magic number `1.8` | 13 | Both |
| Magic number `1400` | 11 | InkCanvas.cpp |
| Magic number `2000` | 1 | MainWindow.cpp |
| Helper methods to delete | 6 | InkCanvas.cpp |

**Total lines of combined-canvas code to eliminate:** ~500-700 lines

---

## 7. MIGRATION IMPACT

### Files Affected:
- `InkCanvas.cpp`: Major cleanup (~500 lines removed)
- `MainWindow.cpp`: Remove buffer-splitting code (~70 lines removed)
- `InkCanvas.h`: Remove helper method declarations

### New Architecture Eliminates:
- All `isCombinedCanvas` checks
- All `singlePageHeight` calculations
- All Y-coordinate offset adjustments
- All buffer splitting/merging
- All "which page am I in?" logic

### Replaced By:
- `Document` owns list of `Page` objects
- `DocumentViewport` renders visible pages
- Each `Page` is self-contained
- Coordinate transforms handled by viewport

---

## 8. NEXT STEPS

After Phase 0.2 (this document):
- [ ] Phase 0.3: Document page-related widgets in MainWindow
- [ ] Phase 1.1: Create Page class (no combined canvas complexity!)

---

*Document generated for SpeedyNote viewport migration project*
