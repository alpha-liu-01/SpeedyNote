# Phase A: PDF Text Selection - Implementation Subplan

## Overview

Implement text selection from PDF documents in DocumentViewport. This is Phase A of the PDF text extraction feature - selection and copy only, no strokes or LinkObjects.

**Prerequisites:** PDF document loaded with `BackgroundType::PDF` pages

**References:**
- Design decisions: `docs/PDF_TEXT_EXTRACTION_QA.md`
- PdfProvider API: `source/pdf/PdfProvider.h`
- Poppler implementation: `source/pdf/PopplerPdfProvider.cpp`

---

## Current State Analysis

| Component | Status | Notes |
|-----------|--------|-------|
| `ToolType::Highlighter` | ✅ Exists | In `source/core/ToolType.h` line 20 |
| `m_textButton` in Toolbar | ✅ Exists | Emits `textClicked()` signal |
| `textClicked` handler | ⚠️ Stub | Just logs debug message in MainWindow.cpp:746 |
| `PdfProvider::textBoxes()` | ✅ Implemented | Returns `QVector<PdfTextBox>` with char boxes |
| Highlighter in DocumentViewport | ❌ Not implemented | No handler code exists |

---

## Task Breakdown

### A.1: Wire Up Toolbar to ToolType::Highlighter (~15 lines)

**Files:** `source/ui/Toolbar.cpp`, `source/MainWindow.cpp`

**Current:** `m_textButton` emits `textClicked()` signal, MainWindow has stub handler.

**Change:**
1. In `Toolbar::connectSignals()`, change `m_textButton` to emit `toolSelected(ToolType::Highlighter)`
2. In `Toolbar::setCurrentTool()`, add proper `Highlighter` case (currently falls back to marker)
3. Remove stub handler in MainWindow.cpp

```cpp
// Toolbar.cpp - connectSignals()
connect(m_textButton, &QPushButton::clicked, this, [this]() {
    emit toolSelected(ToolType::Highlighter);
});

// Toolbar.cpp - setCurrentTool()
case ToolType::Highlighter:
    m_textButton->setChecked(true);
    break;
```

**Test:** Click Text button → viewport receives `ToolType::Highlighter`

---

### A.2: Add TextSelection Struct and State (~40 lines)

**File:** `source/core/DocumentViewport.h`

**Add near other tool state (after LassoSelection):**

```cpp
// ============================================================================
// Text Selection State (Highlighter Tool)
// ============================================================================

/**
 * @brief Temporary state for text selection from PDF.
 * 
 * Active when ToolType::Highlighter is selected and user is selecting text.
 * Cleared when tool changes or selection is finalized.
 */
struct TextSelection {
    int pageIndex = -1;                     ///< Page being selected from (-1 = none)
    QVector<PdfTextBox> selectedBoxes;      ///< Text boxes intersecting selection
    QString selectedText;                   ///< Combined text for clipboard
    QRectF selectionStartPoint;             ///< Initial click point (page coords)
    QRectF selectionRect;                   ///< Current drag rectangle (page coords)
    bool isSelecting = false;               ///< Currently dragging?
    
    bool isValid() const { return pageIndex >= 0 && !selectedBoxes.isEmpty(); }
    
    void clear() {
        pageIndex = -1;
        selectedBoxes.clear();
        selectedText.clear();
        selectionStartPoint = QPointF();
        selectionRect = QRectF();
        isSelecting = false;
    }
};

// Members in DocumentViewport:
TextSelection m_textSelection;

// Text box cache (loaded on-demand for current page)
QVector<PdfTextBox> m_textBoxCache;
int m_textBoxCachePageIndex = -1;

// Highlighter tool settings
QColor m_highlighterColor = QColor(255, 255, 0, 128);  // Yellow, 50% alpha
```

---

### A.3: Add Highlighter Tool Methods (~20 lines declarations)

**File:** `source/core/DocumentViewport.h`

**Add in private section:**

```cpp
// ============================================================================
// Highlighter Tool Methods
// ============================================================================

// Pointer handlers
void handlePointerPress_Highlighter(const PointerEvent& pe);
void handlePointerMove_Highlighter(const PointerEvent& pe);
void handlePointerRelease_Highlighter(const PointerEvent& pe);

// Text box management
void loadTextBoxesForPage(int pageIndex);
void clearTextBoxCache();
bool isHighlighterEnabled() const;  // False if page has no PDF

// Selection operations
void updateTextSelection(const QPointF& pagePos);
void finalizeTextSelection();
void selectWordAtPoint(const QPointF& pagePos, int pageIndex);
void selectLineAtPoint(const QPointF& pagePos, int pageIndex);

// Hit testing
QVector<int> textBoxIndicesInRect(const QRectF& rect) const;
int textBoxIndexAtPoint(const QPointF& point) const;

// Clipboard
void copySelectedTextToClipboard();

// Rendering
void renderTextSelectionOverlay(QPainter& painter, int pageIndex);
```

**Add signals:**

```cpp
signals:
    void textSelected(const QString& text);  // Emitted when selection finalized
```

---

### A.4: Implement loadTextBoxesForPage() (~30 lines)

**File:** `source/core/DocumentViewport.cpp`

```cpp
void DocumentViewport::loadTextBoxesForPage(int pageIndex)
{
    // Already cached?
    if (pageIndex == m_textBoxCachePageIndex && !m_textBoxCache.isEmpty()) {
        return;
    }
    
    m_textBoxCache.clear();
    m_textBoxCachePageIndex = -1;
    
    if (!m_document || pageIndex < 0 || pageIndex >= m_document->pageCount()) {
        return;
    }
    
    // Check if page has PDF background
    Page* page = m_document->page(pageIndex);
    if (!page || page->backgroundType() != BackgroundType::PDF) {
        return;
    }
    
    // Get PDF provider
    const PdfProvider* pdf = m_document->pdfProvider();
    if (!pdf || !pdf->supportsTextExtraction()) {
        return;
    }
    
    // Get PDF page index (may differ from document page index)
    int pdfPageIndex = page->pdfPageNumber();
    if (pdfPageIndex < 0) {
        pdfPageIndex = pageIndex;  // Fallback: assume 1:1 mapping
    }
    
    // Load text boxes
    m_textBoxCache = pdf->textBoxes(pdfPageIndex);
    m_textBoxCachePageIndex = pageIndex;
    
    qDebug() << "Loaded" << m_textBoxCache.size() << "text boxes for page" << pageIndex;
}

void DocumentViewport::clearTextBoxCache()
{
    m_textBoxCache.clear();
    m_textBoxCachePageIndex = -1;
}

bool DocumentViewport::isHighlighterEnabled() const
{
    if (!m_document) return false;
    
    // Check if current page has PDF
    Page* page = m_document->page(m_currentPageIndex);
    return page && page->backgroundType() == BackgroundType::PDF;
}
```

---

### A.5: Implement Pointer Handlers (~120 lines)

**File:** `source/core/DocumentViewport.cpp`

```cpp
void DocumentViewport::handlePointerPress_Highlighter(const PointerEvent& pe)
{
    // Check if highlighter is enabled on this page
    PageHit hit = viewportToPage(pe.viewportPos);
    if (!hit.valid()) {
        m_textSelection.clear();
        return;
    }
    
    Page* page = m_document->page(hit.pageIndex);
    if (!page || page->backgroundType != Page::BackgroundType::PDF) {
        // Not a PDF page - highlighter disabled
        m_textSelection.clear();
        return;
    }
    
    // Load text boxes for this page if not cached
    loadTextBoxesForPage(hit.pageIndex);
    
    // Check for double-click (word selection)
    // Qt provides this via mouseDoubleClickEvent, but we handle via timing
    static QElapsedTimer lastClickTimer;
    static QPointF lastClickPos;
    static int clickCount = 0;
    
    const qreal doubleClickDistance = 5.0;  // pixels
    const int doubleClickTime = 400;  // ms
    
    if (lastClickTimer.isValid() && 
        lastClickTimer.elapsed() < doubleClickTime &&
        QLineF(lastClickPos, pe.viewportPos).length() < doubleClickDistance) {
        clickCount++;
    } else {
        clickCount = 1;
    }
    lastClickTimer.restart();
    lastClickPos = pe.viewportPos;
    
    if (clickCount == 2) {
        // Double-click: select word
        selectWordAtPoint(hit.pagePoint, hit.pageIndex);
        return;
    } else if (clickCount >= 3) {
        // Triple-click: select line
        selectLineAtPoint(hit.pagePoint, hit.pageIndex);
        clickCount = 0;  // Reset
        return;
    }
    
    // Single click: start drag selection
    m_textSelection.clear();
    m_textSelection.pageIndex = hit.pageIndex;
    m_textSelection.selectionStartPoint = hit.pagePoint;
    m_textSelection.selectionRect = QRectF(hit.pagePoint, QSizeF(0, 0));
    m_textSelection.isSelecting = true;
    
    update();
}

void DocumentViewport::handlePointerMove_Highlighter(const PointerEvent& pe)
{
    if (!m_textSelection.isSelecting) {
        return;
    }
    
    PageHit hit = viewportToPage(pe.viewportPos);
    if (!hit.valid() || hit.pageIndex != m_textSelection.pageIndex) {
        // Moved off the page - clamp to page bounds
        // For now, just ignore moves outside the page
        return;
    }
    
    // Update selection rectangle
    QPointF start = m_textSelection.selectionStartPoint;
    QPointF current = hit.pagePoint;
    
    m_textSelection.selectionRect = QRectF(
        qMin(start.x(), current.x()),
        qMin(start.y(), current.y()),
        qAbs(current.x() - start.x()),
        qAbs(current.y() - start.y())
    );
    
    // Find text boxes intersecting selection
    updateTextSelection(hit.pagePoint);
    
    update();
}

void DocumentViewport::handlePointerRelease_Highlighter(const PointerEvent& pe)
{
    if (!m_textSelection.isSelecting) {
        return;
    }
    
    m_textSelection.isSelecting = false;
    
    // Finalize selection
    if (m_textSelection.isValid()) {
        finalizeTextSelection();
    }
    
    update();
}
```

---

### A.6: Implement Selection Logic (~100 lines)

**File:** `source/core/DocumentViewport.cpp`

```cpp
void DocumentViewport::updateTextSelection(const QPointF& pagePos)
{
    m_textSelection.selectedBoxes.clear();
    m_textSelection.selectedText.clear();
    
    if (m_textBoxCache.isEmpty()) {
        return;
    }
    
    QRectF selRect = m_textSelection.selectionRect;
    
    // Convert selection rect from page coords to PDF coords
    // Page uses 96 DPI, PDF uses 72 DPI
    const qreal scale = 72.0 / 96.0;
    QRectF pdfSelRect(
        selRect.x() * scale,
        selRect.y() * scale,
        selRect.width() * scale,
        selRect.height() * scale
    );
    
    // Find intersecting text boxes
    QStringList selectedTexts;
    
    for (const PdfTextBox& box : m_textBoxCache) {
        if (box.boundingBox.intersects(pdfSelRect)) {
            // Check for partial selection using character boxes
            if (!box.charBoundingBoxes.isEmpty()) {
                // Character-level precision
                QString partialText;
                QVector<QRectF> partialBoxes;
                
                for (int i = 0; i < box.charBoundingBoxes.size() && i < box.text.length(); ++i) {
                    if (box.charBoundingBoxes[i].intersects(pdfSelRect)) {
                        partialText += box.text[i];
                        partialBoxes.append(box.charBoundingBoxes[i]);
                    }
                }
                
                if (!partialText.isEmpty()) {
                    PdfTextBox partialBox;
                    partialBox.text = partialText;
                    // Compute bounding box of selected chars
                    QRectF combinedRect;
                    for (const QRectF& r : partialBoxes) {
                        combinedRect = combinedRect.united(r);
                    }
                    partialBox.boundingBox = combinedRect;
                    partialBox.charBoundingBoxes = partialBoxes;
                    
                    m_textSelection.selectedBoxes.append(partialBox);
                    selectedTexts.append(partialText);
                }
            } else {
                // Word-level only
                m_textSelection.selectedBoxes.append(box);
                selectedTexts.append(box.text);
            }
        }
    }
    
    // Combine selected text with spaces
    m_textSelection.selectedText = selectedTexts.join(" ");
}

void DocumentViewport::finalizeTextSelection()
{
    if (!m_textSelection.isValid()) {
        return;
    }
    
    // Emit signal for UI feedback
    emit textSelected(m_textSelection.selectedText);
    
    qDebug() << "Text selected:" << m_textSelection.selectedText.left(50) 
             << (m_textSelection.selectedText.length() > 50 ? "..." : "");
}

void DocumentViewport::selectWordAtPoint(const QPointF& pagePos, int pageIndex)
{
    loadTextBoxesForPage(pageIndex);
    
    // Convert to PDF coords
    const qreal scale = 72.0 / 96.0;
    QPointF pdfPos = pagePos * scale;
    
    // Find text box containing point
    for (const PdfTextBox& box : m_textBoxCache) {
        if (box.boundingBox.contains(pdfPos)) {
            m_textSelection.clear();
            m_textSelection.pageIndex = pageIndex;
            m_textSelection.selectedBoxes.append(box);
            m_textSelection.selectedText = box.text;
            m_textSelection.selectionRect = box.boundingBox;
            
            finalizeTextSelection();
            update();
            return;
        }
    }
}

void DocumentViewport::selectLineAtPoint(const QPointF& pagePos, int pageIndex)
{
    loadTextBoxesForPage(pageIndex);
    
    // Convert to PDF coords
    const qreal scale = 72.0 / 96.0;
    QPointF pdfPos = pagePos * scale;
    
    // Find text box containing point, then select all boxes on same line
    const qreal lineThreshold = 5.0;  // PDF points
    
    for (const PdfTextBox& box : m_textBoxCache) {
        if (box.boundingBox.contains(pdfPos)) {
            qreal targetY = box.boundingBox.center().y();
            
            m_textSelection.clear();
            m_textSelection.pageIndex = pageIndex;
            
            QStringList lineTexts;
            QRectF lineRect;
            
            // Collect all boxes on same line (similar Y)
            for (const PdfTextBox& lineBox : m_textBoxCache) {
                qreal boxY = lineBox.boundingBox.center().y();
                if (qAbs(boxY - targetY) <= lineThreshold) {
                    m_textSelection.selectedBoxes.append(lineBox);
                    lineTexts.append(lineBox.text);
                    lineRect = lineRect.united(lineBox.boundingBox);
                }
            }
            
            // Sort by X position for correct text order
            // (simplified - assumes left-to-right text)
            m_textSelection.selectedText = lineTexts.join(" ");
            m_textSelection.selectionRect = lineRect;
            
            finalizeTextSelection();
            update();
            return;
        }
    }
}
```

---

### A.7: Integrate Handlers into Main Routing (~20 lines) ✅ COMPLETED

**File:** `source/core/DocumentViewport.cpp`

**In `handlePointerPress()` (after ObjectSelect):**
```cpp
} else if (m_currentTool == ToolType::Highlighter) {
    // Phase A: Text selection / highlighter tool
    handlePointerPress_Highlighter(pe);
}
```

**In `handlePointerMove()` (after ObjectSelect):**
```cpp
// Phase A: Highlighter tool - update text selection
if (m_currentTool == ToolType::Highlighter && m_textSelection.isSelecting) {
    handlePointerMove_Highlighter(pe);
    return;
}
```

**In `handlePointerRelease()` (after ObjectSelect):**
```cpp
// Phase A: Highlighter tool - finalize text selection
if (m_currentTool == ToolType::Highlighter) {
    handlePointerRelease_Highlighter(pe);
    return;
}
```

**In `setCurrentTool()` (after Lasso cleanup):**
```cpp
// Phase A: Clear text selection when switching away from Highlighter
if (previousTool == ToolType::Highlighter && tool != ToolType::Highlighter) {
    m_textSelection.clear();
    clearTextBoxCache();
}
```

---

### A.8: Implement Selection Rendering (~60 lines) ✅ COMPLETED

**File:** `source/core/DocumentViewport.cpp`

**Implementation:** Added `renderTextSelectionOverlay()` after `selectLineAtPoint()`:
```cpp
void DocumentViewport::renderTextSelectionOverlay(QPainter& painter, int pageIndex)
{
    // Only render if there's an active or finalized selection
    if (!m_textSelection.isValid() && !m_textSelection.isSelecting) {
        return;
    }
    
    // Only render on the page being selected
    if (m_textSelection.pageIndex != pageIndex) {
        return;
    }
    
    painter.save();
    
    // Scale factor: PDF coords (72 DPI) to page coords (96 DPI)
    const qreal scale = 96.0 / 72.0;
    
    // Selection color (Windows selection blue with transparency)
    QColor selectionColor(0, 120, 215, 100);
    painter.setBrush(selectionColor);
    painter.setPen(Qt::NoPen);
    
    // Draw drag rectangle while actively selecting (in page coords)
    if (m_textSelection.isSelecting) {
        painter.drawRect(m_textSelection.selectionRect);
    }
    
    // Draw selected text boxes (converted from PDF coords to page coords)
    for (const PdfTextBox& box : m_textSelection.selectedBoxes) {
        QRectF pageRect(
            box.boundingBox.x() * scale,
            box.boundingBox.y() * scale,
            box.boundingBox.width() * scale,
            box.boundingBox.height() * scale
        );
        painter.drawRect(pageRect);
    }
    
    painter.restore();
}
```

**Call from `renderPage()`** (added as step 5, before page border):
```cpp
// 5. Render text selection overlay (Phase A: Highlighter tool)
if (m_currentTool == ToolType::Highlighter) {
    renderTextSelectionOverlay(painter, pageIndex);
}
```

---

### A.9: Implement Copy to Clipboard (~25 lines) ✅ COMPLETED

**File:** `source/core/DocumentViewport.cpp`

**In `keyPressEvent()` (after ObjectSelect block, before tool switching):**
```cpp
// Phase A: Highlighter tool keyboard shortcuts
if (m_currentTool == ToolType::Highlighter) {
    // Copy (Ctrl+C) - copy selected text to clipboard
    if (event->matches(QKeySequence::Copy)) {
        copySelectedTextToClipboard();
        event->accept();
        return;
    }
    
    // Cancel selection (Escape)
    if (event->key() == Qt::Key_Escape) {
        if (m_textSelection.isValid() || m_textSelection.isSelecting) {
            m_textSelection.clear();
            update();
            event->accept();
            return;
        }
    }
}
```

**`copySelectedTextToClipboard()` implementation:**
```cpp
void DocumentViewport::copySelectedTextToClipboard()
{
    if (!m_textSelection.isValid() || m_textSelection.selectedText.isEmpty()) {
        qDebug() << "copySelectedTextToClipboard: No text selected";
        return;
    }
    
    QClipboard* clipboard = QGuiApplication::clipboard();
    clipboard->setText(m_textSelection.selectedText);
    
    qDebug() << "Copied to clipboard:" << m_textSelection.selectedText.left(50)
             << (m_textSelection.selectedText.length() > 50 ? "..." : "");
}
```

```cpp
void DocumentViewport::copySelectedTextToClipboard()
{
    if (!m_textSelection.isValid()) {
        return;
    }
    
    QClipboard* clipboard = QGuiApplication::clipboard();
    clipboard->setText(m_textSelection.selectedText);
    
    qDebug() << "Copied to clipboard:" << m_textSelection.selectedText.left(50)
             << (m_textSelection.selectedText.length() > 50 ? "..." : "");
    
    // Optional: Clear selection after copy
    // m_textSelection.clear();
    // update();
}
```

**Add include at top:**
```cpp
#include <QClipboard>
#include <QGuiApplication>
```

---

### A.10: Disable Tool on Non-PDF Pages (~15 lines) ✅ COMPLETE

**File:** `source/core/DocumentViewport.cpp`, `source/core/DocumentViewport.h`

**Implementation:**

1. **Added `updateHighlighterCursor()` declaration to header:**
```cpp
/**
 * @brief Update cursor based on Highlighter tool availability.
 * Sets IBeamCursor on PDF pages, ForbiddenCursor on non-PDF pages,
 * and restores ArrowCursor when Highlighter is not active.
 */
void updateHighlighterCursor();
```

2. **Implemented `updateHighlighterCursor()`:**
```cpp
void DocumentViewport::updateHighlighterCursor()
{
    if (m_currentTool != ToolType::Highlighter) {
        // Not in Highlighter mode - restore default cursor
        setCursor(Qt::ArrowCursor);
        return;
    }
    
    if (isHighlighterEnabled()) {
        setCursor(Qt::IBeamCursor);  // Text selection cursor for PDF pages
    } else {
        setCursor(Qt::ForbiddenCursor);  // Not available on non-PDF pages
    }
}
```

3. **Called from `setCurrentTool()`** - updates cursor when switching to/from Highlighter

4. **Called from `updateCurrentPageIndex()`** - updates cursor when scrolling to different pages while Highlighter is active

---

## Task Summary

| Task | Description | Est. Lines | Status |
|------|-------------|------------|--------|
| A.1 | Wire Toolbar → ToolType::Highlighter | 15 | [x] COMPLETE |
| A.2 | Add TextSelection struct and state | 40 | [x] COMPLETE |
| A.3 | Add method declarations | 20 | [x] COMPLETE |
| A.4 | Implement loadTextBoxesForPage() | 30 | [x] COMPLETE |
| A.5 | Implement pointer handlers | 120 | [x] COMPLETE |
| A.6 | Implement selection logic | 100 | [x] COMPLETE |
| A.7 | Integrate into main routing | 20 | [x] COMPLETE |
| A.8 | Implement selection rendering | 60 | [x] COMPLETE |
| A.9 | Implement copy to clipboard | 25 | [x] COMPLETE |
| A.10 | Disable on non-PDF pages | 15 | [x] COMPLETE |
| **Total** | | **~445** | ✅ **PHASE A COMPLETE** |

---

## Test Cases

- [ ] Click Text button → tool changes to Highlighter
- [ ] Click on non-PDF page → cursor shows forbidden, no selection possible
- [ ] Click on PDF page → cursor shows I-beam
- [ ] Click and drag → blue selection rectangle follows
- [ ] Release drag → text boxes inside are highlighted blue
- [ ] Double-click on word → word is selected
- [ ] Triple-click on line → entire line is selected
- [ ] Ctrl+C with selection → text copied to clipboard
- [ ] Escape → selection cleared
- [ ] Switch to another tool → selection cleared
- [ ] Character-level precision: drag to middle of word → partial word selected

---

## Dependencies

- `PdfProvider::textBoxes()` - ✅ Already implemented
- `Page::backgroundType()` - ✅ Already exists
- `Page::pdfPageNumber()` - ✅ Already exists
- `Document::pdfProvider()` - ✅ Already exists

---

## Future Work (Phase B)

After Phase A is validated:
- Convert selection to marker strokes
- Add highlight color picker to subtoolbar
- Snap strokes to text box boundaries for clean highlighting

---

## ⚠️ REVISION: Text-Flow Selection Model (Not Rectangle)

**Date:** After A.8 implementation

**Issue:** The initial implementation used **rectangle-based selection** (like image editing), but the expected behavior is **text-flow selection** (like Notepad/Word).

### Wrong Behavior (Rectangle-Based):
- User drags a rectangle on the page
- All text boxes **geometrically inside** the rectangle are selected
- Selection is spatial - you select an area, not text

### Correct Behavior (Text-Flow):
- User clicks at a **character position** to place cursor
- Drag extends selection **character-by-character in reading order**
- Selection flows left→right, then top→bottom (like selecting text in a document)
- Can start/end mid-word, mid-line

### What Needs to Change

#### 1. TextSelection Struct (A.2 revision)

**Old (rectangle-based):**
```cpp
struct TextSelection {
    int pageIndex = -1;
    QVector<PdfTextBox> selectedBoxes;
    QString selectedText;
    QPointF selectionStartPoint;      // Click position
    QRectF selectionRect;             // Drag rectangle
    bool isSelecting = false;
};
```

**New (text-flow):**
```cpp
struct TextSelection {
    int pageIndex = -1;
    
    // Start position (anchor) - where selection began
    int startBoxIndex = -1;           // Index into m_textBoxCache
    int startCharIndex = -1;          // Character index within that box
    
    // End position (cursor) - where selection ends
    int endBoxIndex = -1;
    int endCharIndex = -1;
    
    // Computed from start/end
    QString selectedText;
    QVector<QRectF> highlightRects;   // Per-line highlight rectangles
    
    bool isSelecting = false;
    
    bool isValid() const { 
        return pageIndex >= 0 && startBoxIndex >= 0 && endBoxIndex >= 0; 
    }
    
    void clear() {
        pageIndex = -1;
        startBoxIndex = startCharIndex = -1;
        endBoxIndex = endCharIndex = -1;
        selectedText.clear();
        highlightRects.clear();
        isSelecting = false;
    }
};
```

#### 2. Hit Testing (A.6 revision)

**Old:** `textBoxIndicesInRect(rect)` - find boxes inside rectangle

**New:** `findCharacterAtPoint(pagePos)` - returns `(boxIndex, charIndex)`
```cpp
struct CharacterPosition {
    int boxIndex = -1;
    int charIndex = -1;
    bool isValid() const { return boxIndex >= 0 && charIndex >= 0; }
};

CharacterPosition findCharacterAtPoint(const QPointF& pdfPos) const;
```

#### 3. Selection Logic (A.5/A.6 revision)

**handlePointerPress_Highlighter:**
```cpp
// Find character at click position
CharacterPosition pos = findCharacterAtPoint(pdfPos);
if (pos.isValid()) {
    m_textSelection.startBoxIndex = pos.boxIndex;
    m_textSelection.startCharIndex = pos.charIndex;
    m_textSelection.endBoxIndex = pos.boxIndex;
    m_textSelection.endCharIndex = pos.charIndex;
    m_textSelection.isSelecting = true;
}
```

**handlePointerMove_Highlighter:**
```cpp
// Update end position
CharacterPosition pos = findCharacterAtPoint(pdfPos);
if (pos.isValid()) {
    m_textSelection.endBoxIndex = pos.boxIndex;
    m_textSelection.endCharIndex = pos.charIndex;
    updateSelectedTextAndRects();  // Compute text & highlight rects
}
```

**updateSelectedTextAndRects():**
```cpp
// Given start and end positions, compute:
// 1. selectedText - all characters from start to end in reading order
// 2. highlightRects - rectangles to draw (one per line segment)

// Handle forward/backward selection (end can be before start)
int fromBox, fromChar, toBox, toChar;
if (startBoxIndex < endBoxIndex || 
    (startBoxIndex == endBoxIndex && startCharIndex <= endCharIndex)) {
    // Forward selection
    fromBox = startBoxIndex; fromChar = startCharIndex;
    toBox = endBoxIndex; toChar = endCharIndex;
} else {
    // Backward selection
    fromBox = endBoxIndex; fromChar = endCharIndex;
    toBox = startBoxIndex; toChar = startCharIndex;
}

// Iterate through boxes from fromBox to toBox
// For each box, determine which characters are selected
// Build selectedText and highlightRects
```

#### 4. Rendering (A.8 revision)

**Old:** Draw rectangle + full text box rects

**New:** Draw `highlightRects` (computed highlight regions per line)
```cpp
void DocumentViewport::renderTextSelectionOverlay(QPainter& painter, int pageIndex)
{
    // ... validation ...
    
    // Draw computed highlight rectangles (already in page coords)
    for (const QRectF& rect : m_textSelection.highlightRects) {
        painter.drawRect(rect);
    }
}
```

### What Stays the Same

| Task | Status | Notes |
|------|--------|-------|
| A.1: Toolbar wiring | ✅ Keep | No change needed |
| A.4: loadTextBoxesForPage() | ✅ Keep | No change needed |
| A.7: Main routing | ✅ Keep | No change needed |
| A.9: Copy to clipboard | ✅ Keep | Uses `selectedText` (same) |
| A.10: Disable on non-PDF | ✅ Keep | No change needed |
| Double-click word | ⚠️ Minor | Set start/end to word boundaries |
| Triple-click line | ⚠️ Minor | Set start/end to line boundaries |

### Revised Task List

| Task | Description | Status |
|------|-------------|--------|
| A.2-rev | Update TextSelection struct for text-flow model | ✅ DONE |
| A.5-rev | Update pointer handlers for character positions | ✅ DONE |
| A.6-rev | Implement `findCharacterAtPoint()` and `updateSelectedTextAndRects()` | ✅ DONE |
| A.8-rev | Update rendering to use `highlightRects` | ✅ DONE |

### Key Algorithm: Text-Flow Selection

Given text boxes in reading order:
```
Box 0: "Hello" (chars 0-4)
Box 1: "world," (chars 0-5)
Box 2: "this" (chars 0-3)
Box 3: "is" (chars 0-1)
Box 4: "a" (chars 0)
Box 5: "test." (chars 0-4)
```

If user selects from Box 1, char 2 to Box 4, char 0:
- Selected: "rld, this is a"
- Highlight rects: 
  - Line 1: rect covering "rld," in box 1
  - Line 2: rect covering "this is a" (boxes 2-4)

The highlight rectangles are computed by:
1. For each line (group of boxes with similar Y), find the leftmost selected char and rightmost selected char
2. Create a rectangle from left edge to right edge, spanning line height

---

## Phase A Code Review (Pre-Phase B)

**Date:** Post Phase A completion

Before proceeding to Phase B, the code was reviewed for potential issues.

### Issues Found and Fixed

| Issue | Description | Fix |
|-------|-------------|-----|
| **Missing cleanup on document change** | `setDocument()` didn't clear text selection - could cause stale data or crashes when switching documents | Added `m_textSelection.clear()` and `clearTextBoxCache()` in `setDocument()` |
| **Empty text box handling** | If `box.text` is empty, `box.text.length() - 1` returns -1, causing invalid indices | Added `box.text.isEmpty()` checks before accessing character indices |
| **Duplicate scale constants** | `72.0 / 96.0` and `96.0 / 72.0` appeared multiple times | Added `PAGE_TO_PDF_SCALE` and `PDF_TO_PAGE_SCALE` constants |
| **Static timer clarity** | Static `QElapsedTimer` for double/triple click not obviously safe | Added clarifying comment (code was actually correct - `isValid()` handles uninitialized case) |

### Code Quality Improvements

1. **Added constants for coordinate scaling:**
```cpp
static constexpr qreal PDF_TO_PAGE_SCALE = 96.0 / 72.0;
static constexpr qreal PAGE_TO_PDF_SCALE = 72.0 / 96.0;
```

2. **Safety checks for empty text boxes:**
```cpp
if (box.text.isEmpty()) {
    continue;  // Skip empty boxes
}
int maxCharIdx = box.text.length() - 1;  // Safe now
```

3. **Document change cleanup:**
```cpp
// In setDocument():
m_textSelection.clear();
clearTextBoxCache();
```

### Items Reviewed but Not Changed

| Item | Reason |
|------|--------|
| `selectLineAtPoint()` double iteration | Acceptable for rare triple-click event; text boxes are typically small |
| Static timer initialization | Code is correct (uses `isValid()` check); added comment for clarity |
| `m_highlighterColor` unused | Intentionally left for Phase B (highlight strokes) |

### Memory Safety Verified

- ✅ No raw pointers to heap objects
- ✅ `QVector` handles memory automatically
- ✅ Text box cache cleared on document change
- ✅ Bounds checking on all array accesses

---

## Performance Optimization (Post-A.10)

**Issue:** High CPU usage during text selection (2 threads on AMD R9, unusable on Intel Atom)

### Root Cause Analysis

The original code processed EVERY mouse move event at input device rate (100-360Hz):

```
Mouse move → viewportToPage() → findCharacterAtPoint() [O(n)] 
          → updateSelectedTextAndRects() [string concat] → update() [full repaint]
```

**Key insight:** Most mouse moves don't change the selected character. The cursor moves within the same character's bounding box many times before crossing to the next character.

### Optimizations Applied

| Optimization | Before | After | Reduction |
|--------------|--------|-------|-----------|
| **Early-out on unchanged position** | Always rebuild | Skip if same char | ~90-99% of moves |
| **Conditional `update()`** | Every move | Only on change | ~90-99% repaints |
| **Spatial locality in hit-test** | Full O(n) scan | Check last box first | ~95% hit on first try |

### Code Changes

**1. Position change detection (handlePointerMove_Highlighter):**
```cpp
bool positionChanged = (charPos.boxIndex != m_textSelection.endBoxIndex ||
                        charPos.charIndex != m_textSelection.endCharIndex);
if (positionChanged) {
    // ... update selection and repaint ...
}
// No work if position unchanged
```

**2. Spatial locality cache (findCharacterAtPoint):**
```cpp
// Check last hit box and neighbors first
if (m_lastHitBoxIndex >= 0) {
    if (checkBox(m_lastHitBoxIndex)) return result;      // Same box
    if (checkBox(m_lastHitBoxIndex + 1)) return result;  // Next box
    if (checkBox(m_lastHitBoxIndex - 1)) return result;  // Prev box
}
// Fallback to full scan only if locality miss
```

### Performance Characteristics

| Scenario | Operations/sec | CPU Impact |
|----------|---------------|------------|
| Cursor stationary in character | 0 updates | ~0% |
| Slow drag (1 char/100ms) | ~10 updates/sec | Low |
| Fast drag (360Hz input) | Character rate, not input rate | Moderate |
| Jump to distant text | 1 full scan, then locality | Brief spike |

### Future Optimizations (if needed)

1. **R-tree spatial index** for pages with 1000+ text boxes
2. **Deferred update batching** using a short timer (16ms)
3. **Incremental string building** instead of full rebuild

---

## A.11: PDF Internal Link Clicking

**Date:** Phase D.1 implementation

**Goal:** Make PDF internal links (TOC, cross-references) and external URLs clickable in Highlighter tool.

### Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Cursor feedback | Hand cursor on hover | Standard UX pattern |
| Click priority | Links over text selection | Expected PDF reader behavior |
| External URLs | Open immediately in browser | Productivity app, not security sandbox |
| Visual indicator | Cursor only | PDFs already have link underlines |

### Implementation

#### A.11.1: Add Link Cache (DocumentViewport.h)

```cpp
// Link cache (loaded on-demand for current page, similar to text box cache)
QVector<PdfLink> m_linkCache;
int m_linkCachePageIndex = -1;
```

#### A.11.2: Add Link Methods (DocumentViewport.h)

```cpp
// PDF Link support (Phase D.1)
void loadLinksForPage(int pageIndex);
void clearLinkCache();
const PdfLink* findLinkAtPoint(const QPointF& pagePos, int pageIndex);
void activatePdfLink(const PdfLink& link);
void updateLinkCursor(const QPointF& viewportPos);
```

#### A.11.3: Implement Link Loading (DocumentViewport.cpp)

```cpp
void DocumentViewport::loadLinksForPage(int pageIndex)
{
    if (pageIndex == m_linkCachePageIndex && !m_linkCache.isEmpty()) {
        return;  // Already cached
    }
    
    m_linkCache.clear();
    m_linkCachePageIndex = -1;
    
    if (!m_document || pageIndex < 0 || pageIndex >= m_document->pageCount()) {
        return;
    }
    
    Page* page = m_document->page(pageIndex);
    if (!page || page->backgroundType != Page::BackgroundType::PDF) {
        return;
    }
    
    const PdfProvider* pdf = m_document->pdfProvider();
    if (!pdf || !pdf->supportsLinks()) {
        return;
    }
    
    int pdfPageIndex = page->pdfPageNumber;  // member variable, not function
    if (pdfPageIndex < 0) pdfPageIndex = pageIndex;
    
    m_linkCache = pdf->links(pdfPageIndex);
    m_linkCachePageIndex = pageIndex;
}
```

#### A.11.4: Implement Link Hit Testing (DocumentViewport.cpp)

```cpp
const PdfLink* DocumentViewport::findLinkAtPoint(const QPointF& pagePos, int pageIndex)
{
    loadLinksForPage(pageIndex);
    
    if (m_linkCache.isEmpty()) return nullptr;
    
    Page* page = m_document->page(pageIndex);
    if (!page) return nullptr;
    
    // Link areas are normalized (0-1), convert pagePos to normalized coords
    QSizeF pageSize = page->size;
    qreal normX = pagePos.x() / pageSize.width();
    qreal normY = pagePos.y() / pageSize.height();
    QPointF normPos(normX, normY);
    
    for (const PdfLink& link : m_linkCache) {
        if (link.area.contains(normPos)) {
            return &link;
        }
    }
    return nullptr;
}
```

#### A.11.5: Implement Link Activation (DocumentViewport.cpp)

```cpp
void DocumentViewport::activatePdfLink(const PdfLink& link)
{
    switch (link.type) {
        case PdfLinkType::Goto:
            if (link.targetPage >= 0 && link.targetPage < m_document->pageCount()) {
                scrollToPage(link.targetPage);  // existing method
            }
            break;
        case PdfLinkType::Uri:
            if (!link.uri.isEmpty()) {
                QDesktopServices::openUrl(QUrl(link.uri));
            }
            break;
        default:
            break;
    }
}
```

#### A.11.6: Modify handlePointerPress_Highlighter

Insert link check BEFORE text selection starts:

```cpp
void DocumentViewport::handlePointerPress_Highlighter(const PointerEvent& pe)
{
    PageHit hit = viewportToPage(pe.viewportPos);
    if (!hit.valid()) { ... }
    
    // NEW: Check for PDF link click (priority over text selection)
    const PdfLink* link = findLinkAtPoint(hit.pagePoint, hit.pageIndex);
    if (link && link->type != PdfLinkType::None) {
        activatePdfLink(*link);
        return;  // Don't start text selection
    }
    
    // ... existing text selection code ...
}
```

#### A.11.7: Add Cursor Feedback on Hover

In `handlePointerMove()`, update cursor when in Highlighter mode:

```cpp
void DocumentViewport::updateLinkCursor(const QPointF& viewportPos)
{
    if (m_currentTool != ToolType::Highlighter) return;
    
    PageHit hit = viewportToPage(viewportPos);
    if (!hit.valid()) {
        setCursor(Qt::ArrowCursor);
        return;
    }
    
    Page* page = m_document->page(hit.pageIndex);
    if (!page || page->backgroundType != Page::BackgroundType::PDF) {
        setCursor(Qt::ForbiddenCursor);
        return;
    }
    
    // Check if hovering over a link
    const PdfLink* link = findLinkAtPoint(hit.pagePoint, hit.pageIndex);
    if (link && link->type != PdfLinkType::None) {
        setCursor(Qt::PointingHandCursor);
    } else {
        setCursor(Qt::IBeamCursor);  // Text selection cursor
    }
}
```

### Task Checklist

| Task | Description | Status |
|------|-------------|--------|
| A.11.1 | Add link cache members | ✅ DONE |
| A.11.2 | Add link method declarations | ✅ DONE |
| A.11.3 | Implement loadLinksForPage() | ✅ DONE |
| A.11.4 | Implement findLinkAtPoint() | ✅ DONE |
| A.11.5 | Implement activatePdfLink() | ✅ DONE |
| A.11.6 | Modify handlePointerPress_Highlighter for link priority | ✅ DONE |
| A.11.7 | Add cursor feedback in handlePointerMove | ✅ DONE |
| A.11.8 | Clear link cache in setDocument() and tool switching | ✅ DONE |
| A.11.9 | Fix cursor stuck issues (m_pointerActive management) | ✅ DONE |
| A.11.10 | Code review and optimization | ✅ DONE |

### A.11.9: Cursor Management Fixes

**Issues fixed:**
1. Cursor stuck after page jump/pan - `updateHighlighterCursor()` used stale `m_lastPointerPos`
2. Cursor stuck after text selection - `m_pointerActive` not reset in Highlighter release handler
3. Cursor stuck after link click - `m_pointerActive` not reset after navigation

**Solution:**
- Use `QCursor::pos()` via `mapFromGlobal()` for current mouse position in `updateHighlighterCursor()`
- Reset `m_pointerActive = false` in all Highlighter handler exit paths
- Call `updateHighlighterCursor()` after state changes

### A.11.10: Code Review - Optimizations Applied

| Issue | Fix |
|-------|-----|
| Redundant page lookup in `updateLinkCursor()` | Removed null check since `viewportToPage` validates page |
| Verbose debug output | Removed "Loaded N links/text boxes" logs that run on every page |
| Inconsistent cache validation | Made link cache check consistent with text box cache (check index >= 0) |
| Minor: temporary `QPointF` in loop | Inline `QPointF(normX, normY)` construction in `findLinkAtPoint()` |

