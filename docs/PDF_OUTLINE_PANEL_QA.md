# PDF Outline Panel - Design Q&A

**Document Version:** 1.0  
**Date:** January 10, 2026  
**Status:** 🟡 DESIGN PHASE

---

## Overview

The PDF Outline Panel displays the table of contents (TOC/outline) extracted from PDF documents. It allows users to navigate the document by clicking outline entries and highlights the current section as the user scrolls.

### Existing Infrastructure

| Component | Status | Notes |
|-----------|--------|-------|
| `PdfProvider::outline()` | ✅ Available | Returns `QVector<PdfOutlineItem>` with hierarchical structure |
| `PdfProvider::hasOutline()` | ✅ Available | Quick check if PDF has TOC |
| `LeftSidebarContainer` | ✅ Available | QTabWidget with tabs on left, currently holds LayerPanel |
| `DocumentViewport::currentPageChanged` | ✅ Available | Signal for tracking current page |

### Data Structure (from PdfProvider.h)

```cpp
struct PdfOutlineItem {
    QString title;                          // Display title
    int targetPage = -1;                    // Target page (0-based), -1 if none
    bool isOpen = false;                    // Whether item is expanded by default
    QVector<PdfOutlineItem> children;       // Child items (nested hierarchy)
};
```

---

## Section 1: Basic Functionality

### Q1.1: When should the outline panel be visible?

**Options:**
- A) Always visible as a tab (even for non-PDF documents)
- B) Tab only appears when a PDF-based document is loaded
- C) Tab always exists, but shows "No outline available" message when appropriate

**My Recommendation:** Option B - The Outline tab should only appear when viewing a PDF document with an outline. This keeps the UI clean for non-PDF documents.

**Your Answer:**
I agree with your recommendation. Option B. 
---

### Q1.2: What should happen for PDFs without an outline?

Some PDFs don't have a built-in table of contents.

**Options:**
- A) Show "No outline available" message in the panel
- B) Generate a simple page list fallback (Page 1, Page 2, etc.) - like the old implementation
- C) Hide the entire Outline tab if no outline exists
- D) Option A with a "Show page list" toggle button

**My Recommendation:** Option D - Show a helpful message by default, but provide an option to view a page list for navigation.

**Your Answer:**
Option C. Hide the entire outline tab. The page navigation tab is an entirely different thing that will be implemented right after we finish the pdf outline panel. 
---

### Q1.3: Tree widget or custom widget?

**Options:**
- A) `QTreeWidget` - standard Qt widget, fast to implement
- B) `QTreeView` + custom model - more flexible, better for large outlines
- C) Custom widget with styled items

**My Recommendation:** Option A (`QTreeWidget`) - Good enough for typical PDF outlines (rarely more than a few hundred items), faster to implement, and matches the pattern used elsewhere in the app.

**Your Answer:**
So... what are the downsides of using QTreeWidget? Is it performance or flexibility? 

**Claude's Response:**
Good question! Here's the comparison:

| Aspect | QTreeWidget | QTreeView + Model |
|--------|-------------|-------------------|
| **Performance** | Fine for ~1000s items, each item is a QObject | Better for 10,000+ items with lazy loading |
| **Memory** | Higher (stores data in items) | Lower (data stays in model) |
| **Flexibility** | Limited customization | Full control over data/view separation |
| **Implementation** | ~50% less code | More boilerplate |
| **Updates** | Direct item manipulation | Model signals required |

**For PDF outlines:** QTreeWidget is the right choice because:
1. Outlines rarely exceed a few hundred items (even huge technical docs)
2. We need simple, direct manipulation (expand/collapse, select)
3. Touch events work identically for both
4. Implementation time is significantly lower

**Verdict:** Stick with QTreeWidget. If we ever encounter a PDF with 50,000+ outline items, we can refactor then (unlikely).

---

## Section 2: Navigation Behavior

### Q2.1: What happens when user clicks an outline item?

**Options:**
- A) Jump immediately to the target page (scroll to top of page)
- B) Animate/smooth scroll to the target page
- C) Jump to page and attempt to scroll to the specific position (if PDF provides it)

**Note:** `PdfOutlineItem` only contains page number, not specific position within page.

**My Recommendation:** Option A - Simple jump to page top. Smooth scrolling can be disorienting for large jumps. The current `switchPage()` mechanism already works well.

**Your Answer:**
Option A? But I wonder if "jump immediately to the target position" is an option here, and do you think this is a good choice?
Option B and C are definitely rejected.  

**Claude's Response:**
Great question! Let me investigate what Poppler actually provides:

Looking at the old code (`MainWindow_OLD.cpp:8483-8486`):
```cpp
auto destination = outlineItem.destination();
if (destination) {
    pageNumber = destination->pageNumber();
}
```

Poppler's `LinkDestination` actually provides MORE than just page number:
- `pageNumber()` - target page
- `left()`, `top()` - position on page (normalized 0.0-1.0)
- `zoom()` - suggested zoom level

**Current `PdfOutlineItem` struct only stores `targetPage`**, but we could extend it:

```cpp
struct PdfOutlineItem {
    QString title;
    int targetPage = -1;
    QPointF targetPosition;  // NEW: normalized position (0,0)-(1,1), (-1,-1) if none
    qreal targetZoom = -1;   // NEW: suggested zoom, -1 if none
    bool isOpen = false;
    QVector<PdfOutlineItem> children;
};
```

**My recommendation:** Yes, extend the struct to include position data. Then:
- If position is available → scroll to that exact position on the page
- If only page number → scroll to top of page (current behavior)

This provides better navigation for PDFs where the author specified exact positions (common in technical documents).

**Do you want me to extend `PdfOutlineItem` to include position data?**
Sure! This would be very helpful.
---

### Q2.2: How should current section highlighting work?

When user scrolls the document manually, the outline should highlight which section they're currently viewing.

**Options:**
- A) Highlight the item whose target page ≤ current page (floor match) - like old implementation
- B) Highlight the item closest to current page (round match)
- C) Highlight multiple items if user is between sections
- D) No automatic highlighting - only manual selection

**My Recommendation:** Option A - Floor match (highest page ≤ current) provides intuitive "you're in this section" feedback, and matches the old implementation that worked well.

**Your Answer:**
Option A. 
---

### Q2.3: Should clicking an already-selected outline item do anything?

**Options:**
- A) Do nothing (already selected)
- B) Re-scroll to page top (in case user has scrolled within the page)
- C) Toggle expand/collapse if item has children

**My Recommendation:** Option B - Re-scrolling is useful when user has scrolled within a long section and wants to return to the start.

**Your Answer:**
Option B. The expand/collapse should have its own button. 
---

## Section 3: Expand/Collapse Behavior

### Q3.1: Default expansion state on load?

**Options:**
- A) Respect PDF's `isOpen` field for each item
- B) Always expand first level only
- C) Always expand all levels
- D) Always start collapsed

**My Recommendation:** Option A with fallback to B - Use PDF's preference if available, otherwise expand only the first level. This respects the document author's intent while providing a reasonable default.

**Your Answer:**
I agree with your recommendation. 
---

### Q3.2: Should expansion state persist?

**Options:**
- A) No persistence - reset on each open
- B) Per-document persistence (saved in .snb bundle)
- C) Session persistence only (lost on app close)

**My Recommendation:** Option C - Session-only persistence is the simplest. Most users don't care about persisting outline state across sessions.

**Your Answer:**
I agree with your recommendation. 
---

### Q3.3: Auto-expand to show current section?

When scrolling changes the highlighted section:

**Options:**
- A) Auto-expand parent items to make current section visible
- B) Never auto-expand (user controls expansion)
- C) Only auto-expand if panel is visible

**My Recommendation:** Option C - Auto-expand is helpful, but only when the panel is visible to avoid unnecessary work.

**Your Answer:**
I agree. Option C makes the most sense. 
---

## Section 4: Visual Design

### Q4.1: Icon usage in the tree?

**Options:**
- A) No icons (text only)
- B) Folder icons for items with children, document icon for leaves
- C) Custom icons based on item type (if detectable)
- D) Page number badges/indicators

**My Recommendation:** Option A or D - Text-only is clean and works well. Alternatively, page number indicators could be helpful for "find page X" use case.

**Your Answer:**
Option D.
---

### Q4.2: Should page numbers be displayed?

**Options:**
- A) No page numbers shown
- B) Page numbers on the right side of each item (e.g., "Chapter 1 .......... 15")
- C) Page numbers in parentheses after title (e.g., "Chapter 1 (p.15)")
- D) Page numbers only shown on hover

**My Recommendation:** Option C - Compact and always visible, helpful for large documents.

**Your Answer:**
Option B seems to be the best. 

**Claude's Clarification:**
You chose Option D for Q4.1 (page number badges/indicators as icons) and Option B here (page numbers on right side).

These are actually the same concept! Let me clarify what Option B looks like:

```
├─ Chapter 1 ............................ 1
│  ├─ Section 1.1 ...................... 3
│  └─ Section 1.2 ..................... 12
├─ Chapter 2 ........................... 45
```

The dots are leader characters that guide the eye from title to page number. This is classic TOC styling.

**Implementation:** We'll use a custom item delegate to draw:
1. Title text (left-aligned, truncated with "..." if needed)
2. Leader dots (stretch to fill)
3. Page number (right-aligned)

This is more elegant than parentheses `(p.15)` and matches printed book TOCs.

**Confirmed: Option B with right-aligned page numbers and leader dots.**

---

### Q4.3: Long title handling?

Some outline items have very long titles.

**Options:**
- A) Wrap text (multi-line items)
- B) Elide with "..." (single line, truncated)
- C) Horizontal scroll for long items
- D) Tooltip shows full text, display truncated

**My Recommendation:** Option D - Truncated display keeps the tree compact, tooltip reveals full text when needed.

**Your Answer:**
I agree with Option D. 
---

### Q4.4: Dark/light mode support?

**Options:**
- A) Use system palette (automatic)
- B) Custom styling matching other panels
- C) Match LeftSidebarContainer styling

**My Recommendation:** Option A/C - Use system palette primarily, matching the existing LayerPanel and sidebar styling.

**Your Answer:**
All panels match the system palette, so the 3 options are really the same. They should not follow the color theme (on the navigation bar), but the default bright/dark theme like what the leftsidebarcontainer does now. 
---

## Section 5: Tab Management

### Q5.1: Tab label?

**Options:**
- A) "Outline"
- B) "Contents"
- C) "TOC"
- D) Icon only (📑 or similar)

**My Recommendation:** Option A "Outline" - Clear and matches common terminology.

**Your Answer:**
Option A. 
---

### Q5.2: Tab order?

Current: `[Layers]`
With Outline: 

**Options:**
- A) `[Layers] [Outline]` - Layers first
- B) `[Outline] [Layers]` - Outline first (most commonly used for navigation)
- C) User configurable tab order

**My Recommendation:** Option B - Outline is probably used more frequently for PDF navigation, so it should be easily accessible.

**Your Answer:**
I agree with Option B. 
---

### Q5.3: Remember last active tab per document?

**Options:**
- A) Always open to Layers tab
- B) Always open to Outline tab (if PDF with outline)
- C) Remember per-document which tab was last active
- D) Global preference

**My Recommendation:** Option B - For PDF documents with outline, default to showing Outline tab since it's most useful for navigation. Non-PDF documents show Layers.

**Your Answer:**
Actually I agree with neither. The default should be the page preview/navigation tab that we are about to implement. But for now, option B works. 
---

## Section 6: Performance & Edge Cases

### Q6.1: Very large outlines?

Some technical PDFs have thousands of outline items.

**Options:**
- A) No special handling (load everything)
- B) Lazy loading of children (load on expand)
- C) Virtual scrolling / pagination

**My Recommendation:** Option A initially - `QTreeWidget` handles thousands of items reasonably well. We can optimize later if needed. The outline is typically small compared to the actual PDF content.

**Your Answer:**
Option A is fine for now. 
---

### Q6.2: When to load the outline?

**Options:**
- A) Immediately when document opens
- B) When sidebar becomes visible
- C) When Outline tab is selected
- D) On first scroll/navigation

**My Recommendation:** Option A - Outline data is small (just text and page numbers), so loading immediately ensures instant response when user opens the panel.

**Your Answer:**
Option A.
---

### Q6.3: Multi-tab document behavior?

When multiple documents are open in tabs:

**Options:**
- A) Outline updates immediately when switching tabs
- B) Outline is cached per document, switches with tab
- C) Only update outline when sidebar is visible

**My Recommendation:** Option B - Each document maintains its own outline state (expansion, selection), and switching tabs restores that state.

**Your Answer:**
I agree with Option B.
---

## Section 7: Integration Details

### Q7.1: How to access PDF provider from the panel?

The panel needs to get outline data somehow:

**Options:**
- A) Pass `PdfProvider*` directly to OutlinePanel
- B) Pass `Document*` and let panel get provider via `document->pdfProvider()`
- C) MainWindow/TabManager mediates - signals/slots only, no direct access
- D) Panel requests data via signal, MainWindow provides via slot

**My Recommendation:** Option C or D - Keep the panel decoupled from document internals. MainWindow already has access to everything needed.

**Your Answer:**
I agree with your recommendation for a cleaner architecture. 
---

### Q7.2: Signal for navigation?

**Options:**
- A) `OutlinePanel` emits `pageRequested(int)` signal
- B) Direct call to `DocumentViewport::scrollToPage()`
- C) Emit signal with full `PdfOutlineItem` data

**My Recommendation:** Option A - Simple signal with page number, MainWindow connects it to viewport navigation.

**Your Answer:**
I agree with Option A. 
---

### Q7.3: How should MainWindow know when to update outline selection?

**Options:**
- A) Connect `DocumentViewport::currentPageChanged` → `OutlinePanel::highlightPage`
- B) OutlinePanel polls current page on timer
- C) MainWindow tracks page changes and updates panel

**My Recommendation:** Option A - Direct signal connection is clean and efficient.

**Your Answer:**
I agree with Option A. 
---

## Section 8: Search/Filter (Future Enhancement)

### Q8.1: Should the outline have a search/filter box?

**Options:**
- A) Yes, with a search field at the top
- B) No, keep it simple
- C) Add later as enhancement

**My Recommendation:** Option C - Not needed for initial implementation. Can be added later if users request it.

**Your Answer:**
I agree with Option C. But please add the search field into the plan. 
---

## Summary of Questions

Please answer the questions above. For each, you can:
- Accept my recommendation ✅
- Choose a different option and explain why
- Suggest a new option I didn't consider
- Ask for clarification

After your answers, I'll create an implementation plan based on the confirmed design.


## Extra Points Raised

### E1: Touch Gesture Support

> SpeedyNote is designed for tablet PCs, so touch gesture navigation must be supported from the beginning. Do not limit the navigation to a scroll bar.

**Claude's Response:**
Absolutely! Here's how we'll ensure touch-friendly navigation:

1. **QTreeWidget touch support:** Qt's QTreeWidget already supports:
   - Touch scrolling (flick/swipe to scroll)
   - Touch tap (same as click - selects and navigates)
   - Touch long-press (can be used for context menu if needed)

2. **Touch-specific considerations:**
   - **Item height:** Ensure items are tall enough for finger tapping (~44px minimum, matching iOS guidelines)
   - **Expand/collapse:** The expand arrow should be large enough to tap easily
   - **No hover states:** Touch devices don't have hover, so don't rely on hover for information
   - **Kinetic scrolling:** Qt enables this by default for touch

3. **Implementation:** We'll set:
   ```cpp
   QScroller::grabGesture(outlineTree, QScroller::LeftMouseButtonGesture);
   outlineTree->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
   ```

**No scrollbar-only navigation - touch gestures will be first-class citizens.**

---

## Additional Questions

### Q9.1: Minimum item height for touch?

**Options:**
- A) Default Qt height (~20-25px) - compact, harder to tap
- B) 36px - matches subtoolbar buttons
- C) 44px - iOS Human Interface Guidelines recommendation
- D) Adaptive - larger on touch devices, smaller with mouse

**My Recommendation:** Option C (44px) - Consistent, always works for both touch and mouse.

**Your Answer:**
36px (logical) is good enough. 
---

### Q9.2: Tap vs double-tap for navigation?

**Options:**
- A) Single tap navigates to page
- B) Double tap navigates (single tap only selects)
- C) Single tap on text navigates, single tap on arrow expands/collapses

**My Recommendation:** Option C - Matches standard tree behavior. Tapping the expand arrow should not navigate.

**Your Answer:**
I agree with Option C. For the arrow icons, I already have the up arrow and the down arrow icons with normal and reversed palette ready. 
---

### Q9.3: Context menu (long-press)?

**Options:**
- A) No context menu
- B) Long-press shows "Copy title", "Copy page number" options
- C) Long-press shows "Expand all children", "Collapse all children"

**My Recommendation:** Option A for initial implementation - keep it simple. Can add later.

**Your Answer:**
Option A for now. 
---

### Q9.4: Confirm the clarifications

Please confirm my responses above:
1. **QTreeWidget** - Agree it's the right choice?
2. **Position navigation** - Should we extend `PdfOutlineItem` to include position data?
3. **Page number display** - Confirm Option B with leader dots style?

**Your Answer:** 
Yes. All are correct.

---

## Final Design Summary

All questions answered! Here's the confirmed design:

### Core Behavior
| Decision | Choice |
|----------|--------|
| Panel visibility | Only show when PDF with outline is loaded |
| No outline fallback | Hide the Outline tab entirely |
| Widget type | QTreeWidget |
| Navigation | Jump to target position (extend PdfOutlineItem) |
| Section highlighting | Floor match (highest page ≤ current) |
| Re-click behavior | Re-scroll to target position |

### Expand/Collapse
| Decision | Choice |
|----------|--------|
| Default state | Respect PDF's `isOpen`, fallback to first-level expanded |
| Persistence | Session only |
| Auto-expand | Only when panel is visible |

### Visual Design
| Decision | Choice |
|----------|--------|
| Page numbers | Right-aligned with leader dots |
| Long titles | Truncated with tooltip |
| Theme | Match system palette (dark/light) |
| Item height | 36px (logical) |

### Tab Management
| Decision | Choice |
|----------|--------|
| Tab label | "Outline" |
| Tab order | `[Outline] [Layers]` |
| Default tab | Outline (for PDF with outline) |

### Integration
| Decision | Choice |
|----------|--------|
| Data access | MainWindow mediates via signals/slots |
| Navigation signal | `pageRequested(int, QPointF)` with position |
| Page tracking | Connect `currentPageChanged` → `highlightPage` |

### Touch Support
| Decision | Choice |
|----------|--------|
| Tap behavior | Text = navigate, Arrow = expand/collapse |
| Scrolling | Kinetic scrolling via QScroller |
| Context menu | None (for now) |

### Future Enhancements
- Search/filter box (planned in implementation)
- Context menu with copy/expand options

### Data Structure Changes
Extend `PdfOutlineItem` in `PdfProvider.h`:
```cpp
struct PdfOutlineItem {
    QString title;
    int targetPage = -1;
    QPointF targetPosition = QPointF(-1, -1);  // NEW: normalized (0,0)-(1,1)
    qreal targetZoom = -1;                      // NEW: suggested zoom, -1 = unchanged
    bool isOpen = false;
    QVector<PdfOutlineItem> children;
};
```

---

## Ready for Implementation?

If everything looks correct, I'll create the implementation plan (similar to SUBTOOLBAR_SUBPLAN.md).

**Your confirmation:** 

Everything confirmed.