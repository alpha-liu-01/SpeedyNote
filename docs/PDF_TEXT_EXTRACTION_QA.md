# PDF Text Extraction Feature - Q&A Document

## Overview

This document captures the design decisions for implementing PDF text box extraction in DocumentViewport. The feature allows users to select and highlight text from PDF documents in paged mode.

**Scope:** Text extraction and selection UI only. Persistence and markdown note connections are deferred to a later phase.

---

## Questions

### Q1: What specifically "worked very poorly" in the old InkCanvas implementation?

Looking at the old code (`InkCanvas.cpp` lines 5820-5935), I see it:
- Used Poppler's text box extraction
- Grouped text boxes by row with Y-coordinate tolerance
- Drew highlights as continuous rectangles per row

**What were the pain points?**
- [ ] Performance issues (slow extraction, laggy selection)?
- [ ] Inaccurate text box positions (highlighting wrong areas)?
- [ ] Poor text selection UX (hard to select what you want)?
- [ ] Visual glitches (highlights rendering incorrectly)?
- [x ] Other: _________________

**Your answer:**
The only real problem I had with the PDF text extraction in InkCanvas was, it was pretty hard to stop the selection halfway in a row of text. Other than that, it was more like InkCanvas itself being ridiculous, rather than how text extraction works. 
---

### Q2: What is the expected user interaction flow?

**Option A: Tool-based selection**
1. User clicks "Text Select" tool in toolbar
2. Click and drag to select text region
3. Selected text boxes are highlighted
4. Right-click or toolbar button to create highlight

**Option B: Mode-based selection (like PDF readers)**
1. Highlighter tool is active
2. Click and drag over text to immediately highlight
3. No intermediate "selection" state

**Option C: Hybrid**
1. Text Select tool shows selection
2. Highlighter tool immediately highlights (using text boxes under the stroke)

**Which approach do you prefer?**

**Your answer:**
Option A. Tool-based. You may reference the `source/ui/Toolbar.cpp` and the `m_textButton` is its toggle. 
---

### Q3: How should text selection work mechanically?

**Selection triggers:**
- [x] Click and drag **text-flow selection** (like Notepad/Word - NOT rectangle)
- [ ] Click and drag freeform path (lasso-style)
- [x] Double-click for word selection
- [x] Triple-click for line/paragraph selection
- [ ] Ctrl+A for entire page

**Selection behavior:**
- [x] Snap to word boundaries (default, but allow character precision when needed)
- [x] Allow character-level precision (can start/end mid-word)
- [ ] Auto-expand to full text boxes

**Your preferences:**
Follow the options I selected. I don't know how hard it is to make double-click for word selection, triple-click for line/paragraph selection, and character level precision possible. The previous implementation didn't have that. But they are definitely nice and useful.

**⚠️ CLARIFICATION (added after initial implementation):**
The selection should work like a **text editor** (Notepad, Word), NOT like a rectangle selection tool:
- Click to place cursor at a character position
- Drag to extend selection character-by-character in reading order
- Selection flows left→right, then line-by-line (top→bottom)
- Can start mid-word on one line and end mid-word on another line
- Visual: highlighted text flows across lines, not a rectangle overlay

This is different from "rectangle selection" which selects all text inside a geometric area.

---

### Q4: When should text boxes be extracted from the PDF?

**Option A: On-demand (when user activates text tool)**
- Pros: Lower memory usage, faster initial load
- Cons: Slight delay when switching to text tool

**Option B: Pre-load when PDF page is rendered**
- Pros: Instant text selection
- Cons: Higher memory usage, slower page rendering

**Option C: Lazy load with caching**
- Pros: Best of both worlds
- Cons: More complex implementation

**Your preference:**
Option A. Memory usage is VERY important. SpeedyNote is supposed to target Intel Atom Tablet PCs with 2 GiB of RAM. 
---

### Q5: Where should text box data be stored in the architecture?

Looking at the current architecture:

```
Document
└── Page
    ├── VectorLayer (strokes)
    ├── InsertedObject (images)
    └── ??? (text boxes from PDF)
```

**Option A: Store in Page**
```cpp
class Page {
    QVector<PdfTextBox> m_pdfTextBoxes;  // Cached from PDF
};
```

**Option B: Store in DocumentViewport (view-only cache)**
```cpp
class DocumentViewport {
    QMap<int, QVector<PdfTextBox>> m_textBoxCache;  // pageIndex -> boxes
};
```

**Option C: Query PdfProvider on demand (no storage)**
```cpp
// Just call document->pdfProvider()->textBoxes(pageIndex) when needed
```

**Your preference:**
For now, we may choose Option C. I don't think it's a good idea to "store" the text boxes, or having a separate highlighter from the regular marker. When it comes to persistence, we will then implement an insertedobject called LinkObject, which may link to other places on the viewport, or other things, like plain text. Then when text is "marked" it would just use the normal marker to draw a stroke here, and create a LinkObject, and let other things, like a markdown text annotation to the LinkObject. 

Correct me if I was wrong. What do you think? 
---

### Q6: How should highlighted regions be represented?

The old code had a `TextHighlight` struct. For the new architecture:

**Option A: Store as list of text box indices**
```cpp
struct TextHighlight {
    int pageIndex;
    QVector<int> textBoxIndices;  // Indices into page's text boxes
    QColor color;
};
```

**Option B: Store as bounding rectangles (PDF coordinates)**
```cpp
struct TextHighlight {
    int pageIndex;
    QVector<QRectF> rects;  // In PDF coordinate space
    QColor color;
};
```

**Option C: Store as text content + position**
```cpp
struct TextHighlight {
    int pageIndex;
    QString selectedText;
    QVector<QRectF> rects;
    QColor color;
};
```

**Your preference:**
LinkObject for position and text reference (to be implemented) and a normal stroke of highlighter. 
---

### Q7: Should this be a new tool type or a mode?

Current tools in DocumentViewport:
- `ToolType::Pen`
- `ToolType::Marker`
- `ToolType::Eraser`
- `ToolType::Lasso`

**Option A: Add new tool types**
```cpp
enum class ToolType {
    // existing...
    TextSelect,    // Select text (no highlight yet)
    Highlighter,   // Highlight text immediately
};
```

**Option B: Highlighter only (selection is implicit)**
```cpp
enum class ToolType {
    // existing...
    Highlighter,   // Selects + highlights in one action
};
```

**Your preference:**
We are going to use a single tool type for now. Since subtoolbars are planned, the user may choose to let it immediately draw highlight strokes to the stroke layer, or select the text boxes only. We stick to the Highlighter name for now, though we need to implement text box extraction first. 
---

### Q8: How should the Highlighter tool interact with non-PDF pages?

If user has Highlighter tool active and draws on a page without PDF background:
- [x ] **Disable tool** - Tool only works on PDF pages, greyed out otherwise
- [ ] **Draw marker stroke** - Fall back to Marker tool behavior
- [ ] **Show warning** - Toast/message saying "No PDF on this page"
- [ ] **Other:** _________________

**Your preference:**
Disable tool.
---

### Q9: Visual feedback during text selection

**While selecting:**
- [x ] Blue/colored overlay on selected text boxes (like browsers)
- [ ] Dashed outline around selection region
- [ ] Inverted colors on selected text
- [ ] Other: _________________

**After highlighting:**
- [x ] Semi-transparent colored rectangle (current approach)
- [ ] Underline style
- [ ] Background + underline combination
- [ ] Other: _________________

**Your preferences:**
---

### Q10: Performance considerations

The old implementation had issues. For the new one:

**Text box extraction:**
- Poppler's `textList()` can be slow on text-heavy pages
- Should we limit extraction to visible viewport area?
- Should we use background thread for extraction?

**Rendering highlights:**
- Should highlights be rendered into the PDF cache (like old code)?
- Or rendered as a separate overlay layer?

**Your thoughts:**
Definitely limit the extraction to visible viewport area. It's clear that what pages are currently in the viewport. 
For rendering highlights, they are strokes after all, so they belong to the stroke cache, not highlight cache. You don't need to treat it separately. Since they are strokes (and an additional inserted object), the undo/redo behavior will stay more or less the same. Undo and redo are getting so bloated now... 
---

---

## AI Feedback & Clarifications

### On Q1 + Q3: Character-Level Precision Already Supported!

Your pain point was "hard to stop selection halfway in a row" - that IS character-level precision. Good news: **the infrastructure already exists!**

Looking at `PdfProvider.h`, `PdfTextBox` already has:
```cpp
struct PdfTextBox {
    QString text;
    QRectF boundingBox;
    QVector<QRectF> charBoundingBoxes;  // ← Character-level boxes!
};
```

The Poppler implementation extracts character bounding boxes. So we CAN implement:
- **Word-level selection** (default, snap to word boundaries)
- **Character-level precision** (when user drags to mid-word positions)

**Recommendation:** Implement both. Start selection at word boundaries, but if the drag position is clearly mid-word, use character boxes to clip. This gives the best UX.

---

### On Q5/Q6: LinkObject Architecture - Clarification Needed

I like this architecture! Using regular marker strokes + LinkObject is elegant. But I need clarification:

**Question A: How are stroke and LinkObject connected?**

Option 1: LinkObject stores stroke ID
```cpp
class LinkObject : public InsertedObject {
    QString linkedStrokeId;  // UUID of the marker stroke
    QString extractedText;
    // ...
};
```

Option 2: Stroke stores LinkObject ID  
```cpp
// In VectorStroke
QString linkedObjectId;  // Optional, empty for regular strokes
```

Option 3: Both reference each other (bidirectional)

Option 4: No direct link - they just happen to overlap spatially

**Which approach?**
They are not linked. If other things are trying to search for this highlight, it would be searching for the LinkObject (with its position and the extracted text from the selected text), not the highlight itself. 
---

**Question B: What happens on delete?**

If user:
1. **Deletes the marker stroke** → Should LinkObject be auto-deleted too?
2. **Deletes the LinkObject** → Should the stroke remain (becomes regular marker)?
3. **Uses Eraser on the stroke** → Same as #1?

Erasing a stroke or creating a LinkObject just deletes themselves. One will not affect the other. 
---

**Question C: Undo behavior for highlight creation?**

When user creates a highlight, they create:
- 1 marker stroke
- 1 LinkObject

Should undo:
- Remove both in one step (single compound action)?
- Remove them separately (two undos needed)?

Remove them separately for a clearer structure. 
---

### On Q7: Tool Naming vs Existing UI

I see in `Toolbar.cpp`:
- `m_textButton` exists with tooltip "Text Tool (T)"
- `setCurrentTool()` has a `ToolType::Highlighter` case (falls back to marker)

**Question D: Is `m_textButton` ("Text Tool") the same as Highlighter?**

Or are these separate concepts:
- **Text Tool** = Select text only (for copy/paste)
- **Highlighter** = Select text AND create highlight

If separate, we need two buttons. If same, we should rename to avoid confusion.

---

### On Q3: Double-click / Triple-click

**Feasibility assessment:**

| Feature | Difficulty | Notes |
|---------|------------|-------|
| Click-drag rectangle | Easy | Standard implementation |
| Double-click for word | Medium | Need to detect double-click, find word at point |
| Triple-click for line | Medium | Need to group text boxes by Y-coordinate (existing row logic) |
| Character precision | Easy | Already have `charBoundingBoxes` from Poppler |

All are doable. The double/triple click detection is standard Qt (`QMouseEvent::type() == QEvent::MouseButtonDblClick`).

---

### On Q10: Rendering Highlights as Strokes

You said highlights are strokes, use stroke cache. I agree, but one subtlety:

**Marker strokes are semi-transparent (50% alpha).** When drawn over PDF text, they blend correctly. But if we cache the stroke separately and composite, the visual result is the same. ✓

The existing marker rendering in DocumentViewport handles this correctly (draws opaque to cache, applies alpha on blit). So no changes needed.

---

## Additional Questions

### Question E: Text Selection State Storage

While selecting (before confirming highlight), where is the selection stored?

```cpp
// In DocumentViewport - temporary selection state
struct TextSelection {
    int pageIndex;
    QVector<PdfTextBox> selectedBoxes;  // Or indices?
    QRectF selectionRect;               // The drag rectangle
};

TextSelection m_currentTextSelection;
```

Is this acceptable? (Cleared when tool changes or selection confirmed)

Yes it is.
---

### Question F: Confirming a Highlight

After user selects text, how do they confirm it becomes a highlight?

- [ ] **Release mouse/stylus** → Immediate highlight
- [ ] **Press Enter / click button** → Explicit confirmation
- [ ] **Right-click context menu** → "Create Highlight" option

The tool-based flow (Option A in Q2) suggests there's a step between selection and highlight. What triggers the highlight creation?

I mentioned subtoolbars (which doesn't exist yet) before. For each tool, the subtoolbar will show a few of the options on this tool. For pen, it would be color and thickness. For highlighter, one of the things appear in the subtoolbar of this tool will be "highlight toggle". If the user leaves this toggle on, every selected text will be highlighted. Otherwise, they will not, and the user will be offered with a few choices, like copy or cancel.
---

### Question G: Multiple Highlight Colors

Should users be able to choose highlight color?
- [ ] Fixed yellow (like textbook highlighters)
- [ ] Use current marker color
- [x ] Dedicated highlight color picker

Color should also be customizable for the highlighter. Document the interface, and I'll connect it to UI elements later on. 
Pen, marker, and text highlighter has its own customizable color options. 

---

## AI Follow-up Questions (Round 2)

### Question D Still Unanswered: Text Tool vs Highlighter

In `Toolbar.cpp`, there's `m_textButton` with tooltip "Text Tool (T)". 

**Is this button the Highlighter tool?** If yes, should we:
- Rename tooltip to "Highlighter (T)" or "Text Highlighter (T)"?
- Keep "Text Tool" name but it does highlighting?

Just want to confirm the naming before implementation.
Calling the tool type Highlighter is not a problem. I think they can coexist. Having highlighter and marker on the same tool bar can be a bit confusing, so you can keep the tool tip, as it's mapped to tooltype highlighter anyway. 
---

### Question H: LinkObject Without Stroke?

Based on your subtoolbar toggle design:
- **Toggle ON**: Select text → auto-create stroke + LinkObject
- **Toggle OFF**: Select text → show options (copy, highlight, cancel)

If user chooses "copy" when toggle is OFF, they get text copied but no stroke/LinkObject. ✓

**But can a user ever create a LinkObject WITHOUT creating a stroke?**

Use case: User wants to reference PDF text in a markdown note, but doesn't want a visible highlight on the page.

Options:
- [ ] **Not supported** - LinkObject always comes with a stroke
- [x ] **Supported** - Add "Create Link Only" option in the menu
- [ ] **Deferred** - Implement later if needed

Sorry for not providing more details on this. LinkObjects are used by highlights and a lot of other things. The user may just create one to bring him to another place on the DocumentViewport, or to a website, or to show a markdown notes. It's general purpose. 

For the case you described, he can just create a markdown note, and the "markdownnotesmanager" or something that hasn't been migrated to the new architecture yet would then position a LinkObject somewhere on the page based on the user's preference. It works with scanned PDF with no text boxes. 
---

### Question I: "Orphaned" LinkObject UX Concern

You said stroke and LinkObject are independent - erasing one doesn't affect the other.

**Scenario:** User creates highlight (stroke + LinkObject), then erases the stroke. The LinkObject remains but is now invisible on the page.

Is this intentional? Concerns:
1. User might not know there's still a LinkObject at that position
2. If markdown note references this LinkObject, clicking the link goes to... an invisible object?
3. How does user delete an "orphaned" LinkObject? (No visual indicator)

**Possible solutions:**
- [ ] **Accept it** - Power users can manage via some list view
- [ ] **Show indicator** - Faint outline or icon for LinkObjects without associated strokes
- [ ] **Auto-delete** - When stroke is erased, prompt "Also delete associated link?"
- [ ] **Don't allow** - LinkObject must have a visible stroke

**Your thoughts?**

LinkObjects is an InsertedObject. It will be VISIBLE on the page (by affinity and zorder, so the user can hide it with a stroke layer), and the user can interact with it, just like an ImageObject (which is another type of InsertedObject). 

---

### Question J: How Does "Search" Find LinkObject?

You mentioned: "other things searching for this highlight would search for the LinkObject"

Since stroke and LinkObject are not directly linked, how does the search work?

1. **By position overlap?** - Check if click/search point is within LinkObject bounds
2. **By text match?** - Search for LinkObjects containing specific text
3. **Via markdown sidebar?** - Only accessible through the linked markdown note

This affects whether we need spatial indexing for LinkObjects.

Just like the previous markdown note search UX, the user will provide a page range, and it will iterate through the page jsons within the page range, and search for the "description" or "title" (name TBD) field of the inserted LinkObjects. When a highlight is created, the highlighted text will be extracted to the same field, alongside with other information, like color, or something else. 
---

### Suggestion: Simplify Initial Implementation

Given the complexity of LinkObject + subtoolbar toggle + independent undo, I suggest we **phase the implementation**:

**Phase A: Text Selection Only (This Sprint)**
- Implement `ToolType::Highlighter` in DocumentViewport
- Implement text box extraction and selection UI
- Click-drag to select text, show blue overlay
- Copy selected text to clipboard
- NO stroke creation, NO LinkObject yet

**Phase B: Highlight Strokes (Next Sprint)**
- Add stroke creation from selected text boxes
- Convert selection bounds to marker strokes
- Basic highlighting without LinkObject

**Phase C: LinkObject Integration (Later)**
- Implement LinkObject class
- Connect highlights to markdown annotations
- Implement subtoolbar toggle

This way we can:
1. Validate text selection UX first
2. Ship usable copy-text feature early
3. Iterate on LinkObject design separately

**Do you agree with this phased approach?**

I agree with this. We need to get text selection working first. 

---

### Question K: LinkObject Visual Appearance (For Future Reference)

You clarified LinkObject is a **visible** InsertedObject. For Phase C planning, what does it look like?

- [x] **Small icon** (e.g., link chain icon, annotation pin)
- [ ] **Underline/border** around the linked area
- [ ] **Colored badge/tag** with optional label
- [ ] **Configurable** - user chooses appearance
- [ ] **TBD** - design later

**Answer:** Semi-transparent double quote icon (representing "references"). This doesn't block Phase A.

---

## Final Agreed Points

| Topic | Decision |
|-------|----------|
| **Pain points with old impl** | Hard to stop selection mid-row → need character-level precision |
| **User interaction flow** | Tool-based (`m_textButton` → `ToolType::Highlighter`) |
| **Tool naming** | Keep "Text Tool (T)" tooltip, maps to `ToolType::Highlighter` internally |
| **Selection mechanics** | **Text-flow selection** (like Notepad/Word, NOT rectangle) + double-click word + triple-click line + character precision |
| **Text box loading** | On-demand when tool active (memory-conscious for 2GB devices) |
| **Text box storage** | Query `PdfProvider::textBoxes()` on demand, no persistent cache |
| **Temp selection state** | Store in `DocumentViewport` as `TextSelection` struct |
| **Highlight representation** | Marker stroke (visual) + LinkObject (semantic), independent entities |
| **LinkObject nature** | Visible InsertedObject, general-purpose (links to viewport/website/markdown) |
| **LinkObject search** | By description/title field containing extracted text |
| **Stroke ↔ LinkObject link** | None - spatially overlapping but independent |
| **Delete behavior** | Independent - erasing stroke doesn't affect LinkObject, vice versa |
| **Undo behavior** | Separate actions for stroke and LinkObject |
| **Highlight creation trigger** | Subtoolbar toggle: ON=auto-highlight, OFF=show menu (copy/highlight/cancel) |
| **Highlight color** | Dedicated color picker (separate from Pen and Marker) |
| **Non-PDF pages** | Disable Highlighter tool |
| **Visual feedback** | Blue overlay during selection, semi-transparent stroke after |
| **Performance** | Limit text extraction to visible pages |

---

## Phase A Implementation Plan

**Scope:** Text selection and copy only. No strokes, no LinkObject.

### Tasks

| # | Task | Est. Lines | Notes |
|---|------|------------|-------|
| A.1 | Add `ToolType::Highlighter` to enum | 5 | Already exists, verify it's wired up |
| A.2 | Connect `m_textButton` → `ToolType::Highlighter` | 10 | In Toolbar.cpp |
| A.3 | Add `TextSelection` struct to DocumentViewport | 30 | Temp state for selection |
| A.4 | Implement `handlePointerPress_Highlighter()` | 40 | Start selection, extract text boxes |
| A.5 | Implement `handlePointerMove_Highlighter()` | 60 | Update selection, hit-test text boxes |
| A.6 | Implement `handlePointerRelease_Highlighter()` | 30 | Finalize selection |
| A.7 | Implement text box hit-testing | 80 | Word-level + character-level precision |
| A.8 | Implement double-click word selection | 40 | Find word at point |
| A.9 | Implement triple-click line selection | 40 | Group by Y-coordinate |
| A.10 | Render selection overlay (blue) | 50 | In paintEvent |
| A.11 | Copy selected text to clipboard | 20 | Ctrl+C or context menu |
| A.12 | Disable tool on non-PDF pages | 20 | Check page background type |
| **Total** | | **~425** | |

### Data Structures

```cpp
// In DocumentViewport.h

struct TextSelection {
    int pageIndex = -1;
    QVector<PdfTextBox> selectedBoxes;
    QString selectedText;           // Combined text for clipboard
    QRectF selectionRect;           // Drag rectangle in page coords
    bool isActive = false;
    
    void clear() {
        pageIndex = -1;
        selectedBoxes.clear();
        selectedText.clear();
        selectionRect = QRectF();
        isActive = false;
    }
};

// Tool state
QColor m_highlighterColor = QColor(255, 255, 0, 128);  // Yellow, 50% alpha
TextSelection m_textSelection;
QVector<PdfTextBox> m_currentPageTextBoxes;  // Cached for active page
int m_textBoxCachePageIndex = -1;            // Which page is cached
```

### Key Methods

```cpp
// Text box management
void loadTextBoxesForPage(int pageIndex);
void clearTextBoxCache();

// Selection
void startTextSelection(const QPointF& pagePos, int pageIndex);
void updateTextSelection(const QPointF& pagePos);
void finalizeTextSelection();
void selectWordAtPoint(const QPointF& pagePos, int pageIndex);
void selectLineAtPoint(const QPointF& pagePos, int pageIndex);

// Hit testing
QVector<PdfTextBox> textBoxesInRect(const QRectF& rect) const;
int textBoxAtPoint(const QPointF& point) const;
QRectF clipTextBoxToSelection(const PdfTextBox& box, const QRectF& selectionRect) const;

// Clipboard
void copySelectedTextToClipboard();

// Rendering
void renderTextSelectionOverlay(QPainter& painter);
```

---

**Ready to proceed with Phase A implementation?**

## Agreed Points

*Will be filled after clarifications above.*

| Topic | Decision |
|-------|----------|
| Pain points with old impl | Hard to stop selection mid-row → need character precision |
| User interaction flow | Tool-based (Text Tool button) |
| Selection mechanics | **Text-flow selection** (like Notepad/Word) + double-click word + triple-click line + character precision |
| Text box loading strategy | On-demand (memory-conscious for 2GB devices) |
| Data storage location | Query PdfProvider on demand, no storage |
| Highlight representation | **PENDING** - LinkObject + marker stroke (need clarification) |
| Tool type design | Single `Highlighter` tool type |
| Non-PDF page behavior | Disable tool |
| Visual feedback | Blue overlay during selection, semi-transparent rect after |
| Performance approach | Limit to visible viewport, strokes use stroke cache |

---

## Implementation Notes

*Technical details and code snippets will be added here as design solidifies.*

### Existing Infrastructure

**PdfProvider API** (`source/pdf/PdfProvider.h`):
```cpp
virtual QVector<PdfTextBox> textBoxes(int pageIndex) const = 0;
```

**PdfTextBox struct**:
```cpp
struct PdfTextBox {
    QString text;
    QRectF boundingBox;              // Word-level box (PDF points)
    QVector<QRectF> charBoundingBoxes;  // Character-level boxes
};
```

**Toolbar** (`source/ui/Toolbar.cpp`):
- `m_textButton` exists, emits `textClicked()` signal
- `ToolType::Highlighter` exists but not fully connected

---

## References

- Old implementation: `source/InkCanvas.cpp` lines 5820-5935
- PdfProvider interface: `source/pdf/PdfProvider.h`
- PopplerPdfProvider: `source/pdf/PopplerPdfProvider.h/.cpp`
- DocumentViewport: `source/core/DocumentViewport.h/.cpp`
- Toolbar: `source/ui/Toolbar.cpp`
- Phase 2B Subplan: `docs/MIGRATION_PHASE2B_SUBPLAN.md`

