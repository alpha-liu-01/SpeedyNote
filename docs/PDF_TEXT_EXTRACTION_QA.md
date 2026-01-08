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

---

## Phase B Questions: Highlight Strokes

Phase A (Text Selection) is complete. Before implementing Phase B, I need clarification on stroke creation.

---

### Question L: When is the highlight stroke created?

After user selects text, what triggers stroke creation?

- [ ] **Auto on release** - Mouse/stylus release creates stroke immediately
- [ ] **Explicit confirmation** - User presses Enter, clicks button, or uses context menu
- [ ] **Subtoolbar toggle** - Auto if toggle ON, show menu if toggle OFF (as described in Q&A)
- [ ] **Keyboard shortcut** - e.g., press H to highlight current selection

**Note:** Subtoolbar doesn't exist yet. For Phase B, should we:
1. Use auto-on-release as temporary behavior?
2. Add a keyboard shortcut?
3. Wait for subtoolbar implementation?

**Your answer:**
Add a keyboard shortcut. With this toggle on, we create the stroke. With it off, the behavior stays the same as the end of phase 1. 
---

### Question M: Stroke shape for highlights

How should highlight strokes be shaped?

**Option A: One rectangle per line**
```
┌─────────────────────────┐  ← Line 1 rect
│ Selected text on line 1 │
└─────────────────────────┘
┌───────────────┐            ← Line 2 rect  
│ Line 2 text   │
└───────────────┘
```

**Option B: One merged stroke covering all**
```
┌─────────────────────────┐
│ Selected text on line 1 │
│ Line 2 text             │
└─────────────────────────┘
```

**Option C: Follow text boxes exactly (jagged edges)**
```
┌─────────────────────────┐
│ Selected text on line 1 │
└────────────────┬────────┘
                 └───────────┐
                 │ Line 2    │
                 └───────────┘
```

**Your preference:**
Option A. But it should definitely be possible to start the stroke halfway in a row. For "per line", do you mean filling the whole row, or we can adjust the start or the end? Also... What if text is tilted from the beginning? How do we handle this case, or we may give up on supporting this case?
---

### Question N: Stroke type and properties

Should highlight strokes:

**Type:**
- [x ] Use existing `VectorStroke` class with marker-like properties
- [ ] Create new `HighlightStroke` subclass
- [ ] Use `VectorStroke` but with a special flag/property

**Properties:**
- **Color:** Use `m_highlighterColor` (configurable per-tool)?
- **Opacity:** Same as markers (50%)?
- **Thickness:** Should the rectangle height match text line height, or be configurable?
- **Pressure:** Ignore pressure (uniform appearance)?

**Your preferences:**
Regular vector stroke. Use `m_highlighterColor` and the same opacity. Thickness should match the text line height ( but slightly more than that ). Definitely ignore pressure. Pay attention to the fact that multiple layers exist. Only create the stroke on the current layer. 
---

### Question O: Stroke integration with VectorLayer

Highlight strokes should:
- [x ] Go into the page's existing `VectorLayer` (mixed with pen strokes)
- [ ] Go into a separate highlight-only layer
- [ ] Use affinity system (drawn to layer based on current affinity setting)

**Note:** If using existing VectorLayer + affinity, highlights behave like any other stroke for undo/redo/serialization.

**Your preference:**
"Affinity" is designed for inserted objects, and NEVER strokes. Don't mix them up. Highlights behave like any other stroke is correct behavior. 
---

### Question P: Selection state after highlight creation

After creating a highlight stroke:
- [x ] **Clear selection** - User must re-select for next highlight
- [ ] **Keep selection** - User can change color/re-highlight
- [ ] **Auto-advance** - Selection cleared, tool ready for next selection

**Your preference:**
Just let the user undo (like a normal stroke) and highlight again. No special UX needed here. 
---

### Question Q: Highlight overlap behavior

If user highlights text that's already highlighted (same or different color):
- [x ] **Stack** - New highlight drawn on top (colors blend)
- [ ] **Replace** - Delete old highlight, create new one
- [ ] **Ignore** - Don't create duplicate highlights
- [ ] **Allow both** - Treat as independent strokes

**Your preference:**
For minimum architectural changes, it's the vanilla behavior when the user draws one marker stroke on top of another one. No special logic needed here.
---

### Question R: Stroke data structure

For Phase C (LinkObject), we might need to store the extracted text with the stroke. Should we:

**Option A: Store nothing** - Stroke is just a visual rectangle
```cpp
// VectorStroke only has points/color/width
```

**Option B: Store text in stroke metadata**
```cpp
struct VectorStroke {
    // existing fields...
    QString extractedText;  // Only for highlights
};
```

**Option C: Store reference to future LinkObject**
```cpp
struct VectorStroke {
    // existing fields...
    QString linkedObjectId;  // Empty for regular strokes
};
```

**Option D: Defer to Phase C** - Keep stroke simple for now

**Your preference:**

The strokes themselves NEVER contain extra data. In Phase C, we will create that LinkObject ALONGSIDE with the highlighter stroke, and that LinkObject can hold the selected text and more information. So it's Option A and D?

---

## AI Feedback on Phase B Answers

### On L: Keyboard Shortcut Clarification Needed

You said "Add a keyboard shortcut. With this toggle on, we create the stroke."

I'm not 100% sure what you mean. Two possible interpretations:

**Interpretation 1: Toggle mode + auto-create**
- Press `H` to toggle "auto-highlight mode"
- When ON: releasing mouse after selection → auto-create stroke
- When OFF: selection only, no stroke created

**Interpretation 2: Explicit shortcut to create**
- Select text (no stroke yet)
- Press `H` to convert current selection to stroke
- No toggle, just an action

**Which did you mean?**

Also, what keyboard shortcut? `H` is intuitive but might conflict with other shortcuts.

---

### On M: Mid-row Selection - We Already Support This!

Yes, "per line" means we can adjust start and end positions. The `highlightRects` from Phase A already stores **per-line rectangles with correct start/end positions**:

```
Line 1: [x_start ──────────────────── x_end]  ← Full width
Line 2: [x_start ─────── x_end]                ← Starts mid-line
```

The Phase A `updateSelectedTextAndRects()` computes these rectangles based on character-level positions. Each rectangle in `highlightRects` is:
- X: from first selected char to last selected char on that line
- Y: from line top to line bottom
- Height: matches the text line height

So **no additional work needed** for mid-row precision - we already have the rectangles!

---

### On M: Tilted/Rotated Text

This is a good edge case to consider. Poppler's `QRectF` bounding boxes are **always axis-aligned** (horizontal/vertical edges), even for rotated text.

For rotated text, the bounding box looks like:

```
Rotated text "Hello":    Poppler's bounding box:
    H                    ┌───────┐
     e                   │       │
      l                  │       │  ← Axis-aligned rect
       l                 │       │     covering rotated text
        o                └───────┘
```

**Options for tilted text:**

1. **Use axis-aligned boxes anyway** (simplest)
   - Highlight rectangle won't align perfectly with text
   - But still covers the text area
   - 99% of PDFs have horizontal text, so this is fine for most cases

2. **Skip rotated text** (detect via box aspect ratio / text direction)
   - If text appears rotated, don't allow selection
   - Cleaner UX but limits functionality

3. **Future: Rotated stroke support** (complex)
   - Store rotation angle in stroke
   - Requires VectorStroke changes

**My recommendation:** Option 1 for Phase B. Most PDFs have horizontal text. We can revisit if users report issues with rotated PDFs.

**Do you agree?**
I agree with Option 1. 
---

### On N: Thickness "Slightly More Than Line Height"

You said thickness should match text line height "but slightly more than that."

**Implementation question:** How much more?

- **Option A:** Line height × 1.1 (10% padding)
- **Option B:** Line height + fixed padding (e.g., +2 PDF points)
- **Option C:** Use the bounding box height directly (already includes some padding from Poppler)

The `highlightRects` from Phase A use the exact bounding box from Poppler's text extraction. These boxes typically include a small amount of padding around the text.

**My recommendation:** Use the bounding box height directly (Option C). It already has natural padding and matches what the user sees during selection.

**Do you agree?**
I agree with Option C. 
---

### On N: Current Layer Clarification

You mentioned "Only create the stroke on the current layer."

**Quick check:** In DocumentViewport, we have:
- `m_currentLayerIndex` - the active layer for new strokes
- `addStrokeToCurrentLayer()` - adds stroke to active layer

So highlights will use the same mechanism as pen/marker strokes. ✓

---

### On R: Option A + D Confirmed

Yes, correct interpretation! Strokes are pure visual data. The semantic meaning (extracted text, links) lives in LinkObject which is created separately in Phase C.

---

## Phase B Agreed Points

| Topic | Decision |
|-------|----------|
| **Stroke creation trigger** | Keyboard shortcut (details TBD) |
| **Stroke shape** | One rectangle per line, respecting character-level start/end positions |
| **Tilted text** | Use axis-aligned boxes (Poppler default), revisit if needed |
| **Stroke type** | Regular `VectorStroke` (no subclass, no special flags) |
| **Stroke color** | Use `m_highlighterColor` (per-tool setting) |
| **Stroke opacity** | Same as markers (50% alpha) |
| **Stroke thickness** | Match text line height (use bounding box height from Phase A) |
| **Pressure sensitivity** | Ignored (uniform appearance) |
| **Layer integration** | Goes into current `VectorLayer` via `m_currentLayerIndex` |
| **Selection after create** | Clear selection |
| **Overlap behavior** | Stack like normal markers (no special logic) |
| **Stroke data** | Pure visual - no extracted text stored in stroke |
| **Undo/redo** | Standard stroke undo behavior |

---

## Remaining Questions

### Question S: Keyboard Shortcut Key

What key should trigger highlight creation?

- [ ] `H` - intuitive for "Highlight"
- [ ] `Enter` - standard "confirm" action
- [ ] `Space` - easy to press with stylus hand
- [x ] Other: _How about ctrl+h? This hard-coded keyboard shortcut will be removed when we reconnect the keyboard shortcut customizations in the control panel. ctrl+h is good enough before subtoolbars are implemented. SpeedyNote is meant for tablets anyway, so this is not a problem.______________

**Also:** Should this be a toggle (persistent mode) or one-shot (press each time)?

**Your answer:**
Toggle. The interface should be a toggle as well, and so will the button on the subtoolbar be. 

---

### Question T: Toggle State Indication (Before Subtoolbar)

Since subtoolbar doesn't exist yet, how should the user know if auto-highlight is ON or OFF?

- [ ] **Cursor change** - Different cursor when toggle is ON (e.g., highlighter icon)
- [ ] **Status bar message** - "Auto-highlight: ON/OFF" 
- [ ] **Toast notification** - Brief popup when toggling
- [ ] **Selection overlay color** - Blue when OFF, yellow when ON
- [x] **No indication** - Power users will remember (temporary solution)
- [ ] Other: _______________

**Your preference:**
No indication for now. Subtoolbar toggle button will provide indication when implemented.

---

## Phase B Final Agreed Points

| Topic | Decision |
|-------|----------|
| **Stroke creation trigger** | `Ctrl+H` toggle for auto-highlight mode |
| **Toggle behavior** | ON: release mouse → auto-create stroke; OFF: selection only |
| **Stroke shape** | One rectangle per line, respecting character-level start/end positions |
| **Tilted text** | Use axis-aligned boxes (Poppler default), revisit if needed |
| **Stroke type** | Regular `VectorStroke` (no subclass, no special flags) |
| **Stroke color** | Use `m_highlighterColor` (per-tool setting) |
| **Stroke opacity** | Same as markers (50% alpha) |
| **Stroke thickness** | Use bounding box height directly (natural padding from Poppler) |
| **Pressure sensitivity** | Ignored (uniform appearance) |
| **Layer integration** | Goes into current `VectorLayer` via `m_currentLayerIndex` |
| **Selection after create** | Clear selection |
| **Overlap behavior** | Stack like normal markers (no special logic) |
| **Stroke data** | Pure visual - no extracted text stored in stroke |
| **Undo/redo** | Standard stroke undo behavior |
| **Toggle indication** | None for now (subtoolbar toggle button will provide indication later) |

---

## Phase B Implementation Plan

**Scope:** Create highlight strokes from text selection. No LinkObject yet.

**Prerequisites:** Phase A complete (text selection working)

---

### Task B.1: Add Auto-Highlight Toggle State

**Location:** `DocumentViewport.h`

```cpp
// In private members section
bool m_autoHighlightEnabled = false;  ///< When true, releasing selection auto-creates stroke

// Public method for external control (future subtoolbar)
public:
    void setAutoHighlightEnabled(bool enabled);
    bool isAutoHighlightEnabled() const { return m_autoHighlightEnabled; }
    
signals:
    void autoHighlightEnabledChanged(bool enabled);
```

**Estimated lines:** ~15

---

### Task B.2: Add Ctrl+H Keyboard Shortcut

**Location:** `DocumentViewport.cpp` (keyPressEvent) or `MainWindow.cpp`

```cpp
// In keyPressEvent or via QShortcut
if (event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_H) {
    setAutoHighlightEnabled(!m_autoHighlightEnabled);
    // Optional: qDebug() << "Auto-highlight:" << m_autoHighlightEnabled;
}
```

**Note:** Check MainWindow for existing Ctrl+H bindings to avoid conflicts.

**Estimated lines:** ~20

---

### Task B.3: Implement createHighlightStrokes()

**Location:** `DocumentViewport.cpp`

```cpp
/**
 * @brief Creates marker strokes from current text selection.
 * 
 * Converts each rectangle in m_textSelection.highlightRects to a VectorStroke
 * and adds it to the current layer.
 * 
 * @return List of created stroke IDs for potential undo grouping.
 */
QVector<QString> DocumentViewport::createHighlightStrokes();
```

**Implementation outline:**
1. Check `m_textSelection.isValid()` and `!m_textSelection.highlightRects.isEmpty()`
2. Get target page and layer
3. For each rect in `highlightRects`:
   - Convert from PDF coords to page coords (× PDF_TO_PAGE_SCALE)
   - Create `VectorStroke` as rectangle path (4 corner points, closed)
   - Set stroke properties:
     - Color: `m_highlighterColor`
     - Width: rect height (already correct from bounding box)
     - Tool type: `ToolType::Marker` (for correct blending)
     - Pressure: uniform (1.0)
   - Add to current layer via existing stroke addition methods
   - Record stroke ID for undo
4. Invalidate stroke cache for affected page
5. Return list of stroke IDs

**Key consideration:** Marker strokes typically use a polyline with width. For rectangles, we have two options:
- **Option A:** Single horizontal stroke (start to end of rect, width = rect height)
- **Option B:** Four-point rectangle path

Option A is simpler and matches how markers work. The stroke goes through the center of the text line horizontally.

**Estimated lines:** ~80

---

### Task B.4: Modify handlePointerRelease_Highlighter()

**Location:** `DocumentViewport.cpp`

```cpp
void DocumentViewport::handlePointerRelease_Highlighter(const PointerEvent& pe)
{
    Q_UNUSED(pe);
    
    if (!m_textSelection.isSelecting) {
        return;
    }
    
    m_textSelection.isSelecting = false;
    
    // Finalize selection
    if (m_textSelection.isValid()) {
        finalizeTextSelection();
        
        // NEW: Auto-create strokes if toggle is ON
        if (m_autoHighlightEnabled) {
            createHighlightStrokes();
            m_textSelection.clear();  // Clear selection after stroke creation
        }
    }
    
    update();
}
```

**Estimated lines:** ~10 (modification)

---

### Task B.5: Integrate with Undo System

**Location:** `DocumentViewport.cpp`

The existing stroke addition methods should already integrate with undo. Verify:
- `addStroke()` or equivalent pushes to undo stack
- Each highlight rect = one stroke = one undo step (or group them?)

**Question resolved earlier:** Separate undo steps (not grouped). Each stroke can be undone individually.

**Estimated lines:** ~5 (verification, may need minor adjustments)

---

### Task B.6: VectorStroke Rectangle Creation Helper

**Location:** `DocumentViewport.cpp` (or utility function)

```cpp
/**
 * @brief Creates a marker-style stroke for a highlight rectangle.
 * 
 * @param rect Rectangle in page coordinates
 * @param color Highlight color
 * @return VectorStroke configured as horizontal marker through rect center
 */
VectorStroke DocumentViewport::createHighlightStroke(const QRectF& rect, const QColor& color);
```

**Implementation:**
```cpp
VectorStroke stroke;
stroke.setId(QUuid::createUuid().toString());
stroke.setColor(color);
stroke.setWidth(rect.height());  // Stroke width = text line height
stroke.setToolType(ToolType::Marker);

// Single horizontal line through center of rect
QVector<QPointF> points;
points.append(QPointF(rect.left(), rect.center().y()));   // Start
points.append(QPointF(rect.right(), rect.center().y()));  // End

// Uniform pressure
QVector<qreal> pressures;
pressures.append(1.0);
pressures.append(1.0);

stroke.setPoints(points);
stroke.setPressures(pressures);

return stroke;
```

**Estimated lines:** ~30

---

### Task B.7: Test and Verify

Manual testing checklist:
- [ ] Ctrl+H toggles auto-highlight mode
- [ ] With toggle OFF: selection works as before (Phase A)
- [ ] With toggle ON: releasing mouse creates strokes
- [ ] Strokes appear on current layer
- [ ] Strokes have correct color (m_highlighterColor)
- [ ] Strokes have correct width (text line height)
- [ ] Strokes render with marker blending (semi-transparent)
- [ ] Multi-line selection creates multiple strokes
- [ ] Character-level precision respected (partial lines)
- [ ] Undo removes strokes one at a time
- [ ] Selection clears after stroke creation
- [ ] Strokes persist after save/load

---

### Task Summary

| # | Task | Est. Lines | Dependencies |
|---|------|------------|--------------|
| B.1 | Add auto-highlight toggle state | 15 | None |
| B.2 | Add Ctrl+H keyboard shortcut | 20 | B.1 |
| B.3 | Implement createHighlightStrokes() | 80 | B.1, B.6 |
| B.4 | Modify handlePointerRelease_Highlighter() | 10 | B.1, B.3 |
| B.5 | Integrate with undo system | 5 | B.3 |
| B.6 | VectorStroke rectangle creation helper | 30 | None |
| B.7 | Test and verify | - | All |
| **Total** | | **~160** | |

---

### Implementation Order

1. **B.1** → Add toggle state (foundation)
2. **B.6** → Create stroke helper (can test independently)
3. **B.3** → Implement createHighlightStrokes()
4. **B.4** → Wire up auto-create on release
5. **B.2** → Add Ctrl+H shortcut
6. **B.5** → Verify undo integration
7. **B.7** → Full testing

---

### Code References Needed

Before implementation, review these existing methods:
- How strokes are added to layers: `VectorLayer::addStroke()` or similar
- How undo is handled for strokes
- Existing stroke creation patterns in pen/marker handlers
- `m_currentLayerIndex` usage

---

**Ready to proceed with Phase B implementation?**

---

## Phase B Code Review

Performed after completing B.1 through B.6.

### Issues Found and Fixed

| Issue | Severity | Fix |
|-------|----------|-----|
| Debug output in release builds | Minor | Wrapped in `#ifdef QT_DEBUG` |
| Degenerate rectangle handling | Edge case | Skip rects with width/height < 0.1 |
| Emit documentModified unconditionally | Minor | Only emit if strokes were created |

### Verified Safe

| Aspect | Status |
|--------|--------|
| **Memory safety** | ✅ All objects stack-allocated or Qt-managed |
| **Document switching** | ✅ `setDocument()` clears text selection (from Phase A) |
| **Tool switching** | ✅ Switching away from Highlighter clears selection |
| **Undo integration** | ✅ Uses standard `pushUndoAction()` for each stroke |
| **Thread safety** | ✅ All operations on main Qt thread |
| **Eraser compatibility** | ✅ Highlight strokes are regular VectorStrokes |

### Code Quality Notes

1. **Return value of `createHighlightStrokes()`**: Returns list of stroke IDs but currently unused. Kept for potential future use (e.g., grouping for LinkObject in Phase C).

2. **Toggle state persistence**: `m_autoHighlightEnabled` is NOT reset on document change - intentional, as it's a user preference rather than document state.

3. **Coordinate conversion**: Uses `PDF_TO_PAGE_SCALE` constant consistently (same as Phase A).

### Test Results

Manual testing confirmed:
- ✅ Toggle works via Ctrl+H
- ✅ Strokes created on correct layer
- ✅ Undo removes strokes individually
- ✅ Eraser can delete highlight strokes
- ✅ Selection cleared after stroke creation
- ✅ Document marked dirty after highlight

---

## Phase C Questions: LinkObject Integration

Phase B (Highlight Strokes) is complete. Before implementing Phase C, I need clarification on LinkObject design.

### Understanding from Earlier Q&A

From our earlier discussions, I understand:
- LinkObject is a **visible InsertedObject** (like ImageObject)
- It's **general-purpose** - can link to viewport location, website, markdown notes
- It's **independent from strokes** - erasing a highlight stroke doesn't delete the LinkObject
- It appears as a **semi-transparent double quote icon**
- Has a **description/title field** for search
- When created from highlight, stores the **extracted text**

---

### Question U: LinkObject Core Properties

What properties should LinkObject store?

**Proposed properties (based on InsertedObject base + specific):**

```cpp
class LinkObject : public InsertedObject {
    // Inherited: id, position, size, zOrder, locked, visible, rotation, layerAffinity
    
    // Link target type
    enum class TargetType {
        None,           // No target (just annotation)
        PageLocation,   // Link to specific page + position
        Url,            // External URL
        Markdown        // Link to markdown note (by ID)
    };
    TargetType targetType = TargetType::None;
    
    // Target data (depending on type)
    int targetPageIndex = -1;           // For PageLocation
    QPointF targetPosition;             // For PageLocation
    QString targetUrl;                  // For Url
    QString targetMarkdownId;           // For Markdown
    
    // Content
    QString title;                      // Short label (displayed near icon?)
    QString description;                // Longer text (for search, extracted text)
    QColor color;                       // Tint color for the icon
    
    // Source info (for highlights)
    int sourcePageIndex = -1;           // PDF page where text was extracted
    QString sourceText;                 // Original extracted text (redundant with description?)
};
```

**Questions:**
1. Is `description` the same as extracted text, or separate?
2. Should `sourceText` and `description` be merged into one field?
3. Should there be a `sourceType` to indicate how the LinkObject was created (highlight, manual, markdown)?

**Your preferences:**
A1: For the LinkObjects created alongside with a highlight, the descriptions will be the same as extracted text. For LinkObjects created other ways, no. 
A2: Yes. Just like what I said in A1. For further description on some highlighted text, the user should reach out to the markdown text annotation linked to this LinkObject. 
A3: `markdown` definitely should not be a separate type, but an attribute. I think a LinkObject would support a lot of features at the same time, it can link to a website, and link to a markdown note, and link to another position. 

I got an idea about the UX

---
Img Insert

*LinkInsert* (toggled)

---

🖋



➕

---

+ // to be taken

+ // to be taken

+ // to be taken
---
This is the default behavior. By default, there are 3 buttons on the subtoolbar for the ObjectInsert tool. The pen is for adding one on the DocumentViewport, The mouse cursor is for selection, the plus button is for adding links. The user may add one of a few types, like a markdown note, a position shortcut, or a web link, or something else (that will be implemented later). For each LinkObject, there will be 3 general purpose slots, for the user to link stuff to. 

The UX is like this. The user select the ObjectSelect tool on the main toolbar - (default to) or select LinkInsert mode - (since the default is select, the user will need to manually select add) the pen icon and click the place he wants on the canvas to create a new LinkObject. 

Object Creation is complete, by default it already has the attributes that all InsertedObject have. The user will need to click "add" to add a link to it. (now there will be a menu of options on which type of link he wants, markdown , web link, another position, etc. We can deal with this later). After the link is created, it will take up one of the 3 "to be taken" slot of this link. The link can be of any type, it can be 2 markdowns and a position, or one of each kind. 

For accessing the link later, (since the default option is LinkInsert - select), all the user needs to do is click on tool ObjectInsert, and immediately click on the LinkObject somewhere on a page, or an edgeless canvas. That's only 2 steps. After the LinkObject is selected, the subtoolbar shows all 3 (or as many as the user created) links and the user can access any. 

What do you think about this idea? This whole thing can be accelerated even more if the action combinations are connected to other things, like gestures? 

It should really reuse the logic how other things get inserted, like an image, which also has an "selected" state. I can't just let the user click on the LinkObject and immediately bring it somewhere else, because it goes against how other InsertedObjects work.


---

### Question V: LinkObject Visual Appearance

You mentioned "semi-transparent double quote icon". More details needed:

**Icon design:**
- [x ] Fixed icon (e.g., `"` quote marks)
- [ ] Icon varies by target type (quote for text, chain for URL, page for location)
- [ ] Custom icon per LinkObject

**Size:**
- [x ] Fixed size (e.g., 24x24 pixels at 100% zoom)
- [ ] Size based on content (title length)
- [ ] Configurable per object

**Position relative to highlight:**
- [x ] At start of highlight (left edge)
- [ ] At end of highlight (right edge)
- [ ] Centered on first line
- [x ] User-draggable anywhere

**Visibility:**
- [x ] Always visible (like ImageObject), *but only if the user makes the layer visible (affinity support)*
- [ ] Only visible when hovering near the highlight
- [ ] Only visible in a special "link view" mode

**Your preferences:**
Just like what I selected. 
---

### Question W: LinkObject Creation Flow

When user creates a highlight (with auto-highlight ON), how is LinkObject created?

**Option A: Always create LinkObject with highlight**
```
Select text → Release → Create stroke(s) + Create LinkObject
```
- Every highlight has an associated LinkObject
- Simpler logic

**Option B: Separate toggle for LinkObject**
```
Toggle 1: Auto-highlight (creates strokes)
Toggle 2: Auto-link (creates LinkObject)
```
- User controls whether to create just visual highlight or also semantic link
- More flexible but more complex UI

**Option C: Post-creation option**
```
Select text → Release → Create stroke(s) only
Then: User right-clicks highlight area → "Add Link" → Creates LinkObject
```
- LinkObject is optional, added later
- Doesn't require knowing which stroke "belongs" to which link

**Your preference:**
Option A. Always auto create (as long as the highlight mode is on., or it still behaves like the end of Phase A). Otherwise the user will use the regular marker if he doesn't want the LinkObject. 
---

### Question X: LinkObject Interaction

What happens when user interacts with a LinkObject?

**On click:**
- [ ] Navigate to target (if TargetType is PageLocation/Url/Markdown)
- [ ] Open context menu (navigate, edit, delete)
- [ ] Select the object (like ImageObject)

**On double-click:**
- [ ] Edit target/description
- [ ] Navigate to target
- [ ] Show properties dialog

**Tool requirements:**
- [ ] Clickable with any tool (like hyperlinks in PDF viewers)
- [x ] Only clickable with ObjectSelect tool
- [ ] Only clickable with a new "Link" tool

**Your preferences:**
I think I answered the question clearly in Question U. Link appears on the subtoolbar. When we don't have a toolbar, we can use ctrl+8 ctrl+9 ctrl+0 shortcuts for now. We can't default single or double click behavior, because a LinkObject can have various types of things linked to it, unless the user gives it an order, the first one can be triggered by double click, the second one can be triggered by something else, etc. This can be convenient, but we need to implement this later. 
---

### Question Y: LinkObject and Markdown Integration

You mentioned markdown notes will reference LinkObjects. Clarify:

**Connection direction:**
- [ ] Markdown note stores LinkObject ID → looks up LinkObject for position
- [x ] LinkObject stores markdown note ID → links to note
- [ ] Bidirectional references

**When markdown note is created:**
1. User selects text → creates highlight + LinkObject
2. Then what?
   - [ ] Automatic: Opens markdown editor with extracted text
   - [ ] Manual: User later creates note and links it to existing LinkObject
   - [ ] Context menu: Right-click LinkObject → "Create Note"

**Your preferences:**
For connection direction, LinkObject should store the markdown note id. Since there is already a "loading ondemand" system for InsertedObjects, the moment markdown notes load FOLLOWS when the InsertedObjects load. This means that, when a LinkObject is deleted, the deletion will cascade and the markdown note(s) linked to the LinkObject will be deleted. 
When a markdown note is deleted, the LinkObject is more or less unaffected, unless the user tries to access (or the application is trying to automatically load) the markdown note again, and the file is not there and the LinkObject will delete this reference. Does this make sense to you? If not, you may correct me. 


For markdown creation, he can create a markdown note by adding a "link to a markdown note", which takes up one of the 3 slots.  


---

### Question Z: LinkObject Layer Affinity

Where should LinkObject render in the layer stack?

- [ ] **Above all strokes** (always visible, even under highlight strokes)
- [ ] **Same affinity as highlight stroke** (if applicable)
- [ ] **Default affinity like ImageObject** (activeLayer - 1)
- [ ] **Special "overlay" layer** that's always on top

**Your preference:**
Same affinity as the CURRENT LAYER. DocumentViewport shouldn't really know what type of InsertedObject it is. You may reference the affinity of the inserted images. It's always right below the currently selected vector layer and right above the vector layer below. 
---

### Question AA: Phase C Scope

Given the complexity of LinkObject, what should Phase C cover?

**Option A: Full LinkObject implementation**
- Create LinkObject class
- Integrate with highlight creation
- Implement all target types
- Markdown integration

**Option B: Minimal LinkObject (MVP)**
- Create LinkObject class (basic properties only)
- Integrate with highlight creation (stores extracted text)
- No navigation (TargetType::None only)
- Markdown integration deferred

**Option C: LinkObject class only**
- Create LinkObject class and rendering
- No integration with highlighter yet
- Test insertion/selection/manipulation via debug menu
- Integration in separate phase

**Your preference:**

I'm going for Option A. This really is the LAST puzzle of the architectural migration (and improvement) of SpeedyNote. After this, it's all UI/UX, and smaller stuff. 

---

## AI Analysis & Follow-up Questions

### Understanding the 3-Slot Architecture

Your UX design is creative! Let me make sure I understand correctly:

**LinkObject has 3 "link slots"**, each can hold a different type of link:

```cpp
struct LinkSlot {
    enum class Type { Empty, Markdown, Url, Position };
    Type type = Type::Empty;
    
    // Data (only one is used based on type)
    QString markdownNoteId;     // For Markdown
    QString url;                // For Url
    int targetPageIndex;        // For Position
    QPointF targetPosition;     // For Position
};

class LinkObject : public InsertedObject {
    // ... inherited properties ...
    
    QString description;        // Extracted text (for highlights) or user-entered
    QColor iconColor;          // Tint color for the icon
    
    LinkSlot slots[3];         // The 3 link slots
};
```

**Questions about slots:**

### Question AB: Slot Limit

Is 3 a hard limit, or should it be configurable?

- [ ] **Hard limit of 3** - Simple, predictable UI
- [ ] **Configurable per-document** - More flexible
- [ ] **Unlimited slots** - Maximum flexibility, but complex UI

**My suggestion:** Start with hard limit of 3 for Phase C. This keeps the subtoolbar UI simple and predictable. Can be expanded later if needed.

**Your preference:**

---

### Question AC: Two Creation Flows

I see TWO ways to create a LinkObject:

**Flow 1: From Highlight (Automatic)**
```
Highlighter tool active + auto-highlight ON
→ Select PDF text → Release mouse
→ Creates stroke(s) + LinkObject with description = extracted text
→ LinkObject positioned at start of first highlight rect
```

**Flow 2: Manual via ObjectSelect Tool**
```
ObjectSelect tool active + LinkInsert mode
→ Click pen icon on subtoolbar
→ Click position on canvas
→ Creates empty LinkObject (no description, no slots filled)
```

**Is this understanding correct?**

**Follow-up:** For Flow 1, should the LinkObject:
- [ ] Have all 3 slots empty (user adds links later)
- [ ] Have slot 0 pre-filled with something (e.g., position of the highlight itself?)
- [ ] User chooses during creation

**Your answer:**

---

### Question AD: Cascade Delete Confirmation

You said: "when a LinkObject is deleted, the deletion will cascade and the markdown note(s) linked to the LinkObject will be deleted."

**This is significant behavior!** Want to confirm:

1. **Deleting a LinkObject** → All markdown notes in its slots are **permanently deleted**?
2. **Is there a confirmation dialog?** "This will delete X linked notes. Continue?"
3. **What about undo?** Undo delete LinkObject → restores the markdown notes too?

**Alternative approach (softer):**
- Deleting LinkObject only removes the *reference* to markdown notes
- Markdown notes become "orphaned" but still exist
- User can find orphaned notes via markdown panel/search

**Which behavior do you want?**
- [ ] **Hard cascade** - Delete LinkObject = delete notes (with confirmation)
- [ ] **Soft cascade** - Delete LinkObject = orphan notes (recoverable)

**Your preference:**

---

### Question AE: Subtoolbar Dependency

The UX you described depends heavily on the **subtoolbar**, which doesn't exist yet.

**For Phase C, should we:**

1. **Implement subtoolbar first** → Then LinkObject integration
2. **Use temporary keyboard shortcuts** → Ctrl+8/9/0 for slots (you mentioned this)
3. **Use context menu** → Right-click LinkObject → menu shows slots

**My suggestion:** Start with option 2+3 (shortcuts + context menu). This lets us:
- Build and test LinkObject without waiting for subtoolbar
- Verify the slot architecture works
- Add subtoolbar later as pure UI layer

**Your preference:**

---

### Question AF: LinkObject Icon Rendering

You said: Fixed icon (24x24 at 100% zoom), semi-transparent double quote.

**Implementation questions:**

1. **Icon source:** 
   - [ ] Embedded PNG/SVG resource
   - [ ] Qt-drawn shapes (QPainter)
   - [ ] Font glyph (e.g., FontAwesome quote icon)

2. **Visual states:**
   - Normal: Semi-transparent icon
   - Hovered: Slightly more opaque?
   - Selected: Selection box around it (like ImageObject)?
   - Has links: Any indicator (dots under icon for filled slots)?

**Your preferences:**

---

### Question AG: Highlighter Tool vs ObjectSelect Tool

Clarifying tool relationships:

| Tool | Can Create LinkObject? | Can Select LinkObject? | Can Access Slots? |
|------|------------------------|------------------------|-------------------|
| **Highlighter** | Yes (from highlight) | No? | No? |
| **ObjectSelect** | Yes (manual) | Yes | Yes (subtoolbar) |
| **Pen/Marker/Eraser** | No | No | No |

**Is this correct?**

If Highlighter can't select LinkObjects, how does user access the slots of a highlight-created LinkObject?
- Switch to ObjectSelect tool?
- Auto-switch after creation?

**Your answer:**

---

### Phase C Sub-phases Suggestion

Given the scope (Full LinkObject + all link types + markdown integration), I suggest breaking Phase C into sub-phases:

**C.1: LinkObject Class Foundation**
- Create `LinkObject` class with 3-slot architecture
- Implement `render()` (draw icon)
- Implement `toJson()` / `loadFromJson()` serialization
- Register in `InsertedObject::fromJson()` factory
- Test via direct insertion (debug code)
- ~150 lines

**C.2: LinkObject Selection & Manipulation**
- Hit testing (click on 24x24 icon)
- Selection works like ImageObject
- Move/delete via ObjectSelect tool
- Undo/redo support
- ~100 lines

**C.3: Highlighter Integration**
- Modify `createHighlightStrokes()` to also create LinkObject
- Position at start of first highlight rect
- Set description to extracted text
- ~50 lines

**C.4: Link Slot Functionality**
- Implement slot types (Markdown, Url, Position)
- Keyboard shortcuts (Ctrl+8/9/0) or context menu to access slots
- Navigation for Position type
- URL opening for Url type
- ~150 lines

**C.5: Markdown Integration**
- Create/link markdown notes
- Cascade delete behavior
- Markdown panel integration (if exists)
- ~200 lines

**Total: ~650 lines across 5 sub-phases**

**Do you agree with this breakdown?**

---

## Phase C Agreed Points (So Far)

| Topic | Decision |
|-------|----------|
| **LinkObject properties** | 3 link slots + description + iconColor |
| **Description field** | Same as extracted text for highlights, user-entered otherwise |
| **Link types** | Multiple per LinkObject: Markdown, Url, Position in slots |
| **Visual** | Fixed 24x24 icon (double quote), semi-transparent, user-draggable |
| **Initial position** | Start of highlight (left edge) when created from highlight |
| **Visibility** | Always visible, respects layer affinity |
| **Creation from highlight** | Always auto-create when highlight mode ON |
| **Interaction** | Only via ObjectSelect tool; slots shown on subtoolbar/shortcuts |
| **Markdown connection** | LinkObject stores markdown ID; cascade delete (TBD exact behavior) |
| **Layer affinity** | Same as current layer (activeLayer - 1) |
| **Scope** | Full implementation (Option A) |

---

## References

- Old implementation: `source/InkCanvas.cpp` lines 5820-5935
- PdfProvider interface: `source/pdf/PdfProvider.h`
- PopplerPdfProvider: `source/pdf/PopplerPdfProvider.h/.cpp`
- DocumentViewport: `source/core/DocumentViewport.h/.cpp`
- Toolbar: `source/ui/Toolbar.cpp`
- Phase 2B Subplan: `docs/MIGRATION_PHASE2B_SUBPLAN.md`

