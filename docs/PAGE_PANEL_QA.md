# Page Panel Q&A Document

**Document Version:** 1.0  
**Date:** January 11, 2026  
**Status:** 🟡 DESIGN PHASE  
**Purpose:** Clarify design decisions for the Page Panel feature

---

## Overview

The Page Panel is a new tab in the left sidebar that displays page thumbnails for quick navigation and page management. It complements the existing Layers tab and Outline tab (for PDFs).

---

## Section 1: Display & Layout

### Q1.1: What should the thumbnail size be?

**Options:**
- A) Fixed size (e.g., 120px width, height varies by aspect ratio)
- B) Fill available width (responsive to sidebar width)
- C) User-configurable size

**Considerations:**
- Touch-friendly (minimum tap target)
- Readable page numbers
- Performance (larger = more rendering work)
- Sidebar is typically 200-300px wide

**My Recommendation:** Option B with minimum size constraint. Thumbnail width = sidebar width - padding (maybe 16px each side), with minimum 100px width.

**Answer:**
I agree with Option B. 
---

### Q1.2: Should we support 1-column and 2-column thumbnail layouts?

**Options:**
- A) Always 1-column (larger thumbnails, more scrolling)
- B) Always 2-column (smaller thumbnails, less scrolling)
- C) User toggle between 1/2 columns
- D) Auto-switch based on sidebar width

**Considerations:**
- 2-column useful for documents with many pages
- 1-column better for seeing page content
- User mentioned supporting both

**My Recommendation:** Option C - toggle button at top of panel. Default to 1-column.

**Answer:**
Option A. Always 1 column. 
---

### Q1.3: How should the current page be visually indicated?

**Options:**
- A) Border highlight (thick colored border)
- B) Background highlight (subtle background color)
- C) Both border and background
- D) Selection indicator on side (like a vertical bar)

**Considerations:**
- Must be visible at a glance
- Should work in both light and dark themes
- Not too distracting

**My Recommendation:** Option A - thick accent-colored border (same color as app accent, ~3px).

**Answer:**
Option A. 
---

### Q1.4: Should page numbers be displayed on thumbnails?

**Options:**
- A) Always show page number
- B) Show on hover only
- C) Show below thumbnail (not overlaid)
- D) User preference

**Considerations:**
- Overlaid numbers may obscure content
- Numbers below take extra vertical space
- Touch devices can't hover

**My Recommendation:** Option C - page number below each thumbnail (small text, like "Page 5").

**Answer:**
I agree with Option C. 
---

### Q1.5: What about edgeless canvas documents?

**Options:**
- A) Hide Page Panel tab entirely for edgeless
- B) Show disabled/empty state with message
- C) Show tile grid instead of pages

**Considerations:**
- Edgeless has no pages, only infinite canvas with tiles
- User explicitly mentioned not supporting edgeless
- Consistent with Outline tab behavior (hide when not applicable)

**My Recommendation:** Option A - hide the tab entirely for edgeless documents, like we do with Outline tab for non-PDF documents.

**Answer:**
Option A. 
---

## Section 2: Thumbnail Generation & Performance

### Q2.1: When should thumbnails be generated?

**Options:**
- A) All at once when document opens (may cause lag)
- B) Lazy load as user scrolls (may show blank initially)
- C) Generate visible + buffer pages (like PDF cache)
- D) Background thread generation for all pages

**Considerations:**
- Intel Z3735E is very slow
- Large documents (100+ pages) could be problematic
- User expects to see thumbnails quickly

**My Recommendation:** Option C - generate visible thumbnails + small buffer (±2 pages), similar to how we handle PDF page caching. Show placeholder for unloaded thumbnails.

**Answer:**
I agree with your choice. I wonder if you are referring to the full resolution PDF cache. 
It's like this. There is a fullsize PDF cache (that the Document uses), which contains the pages on the viewport and a few next to the visible area. The page panel will load this (but never request rendering fullsize PDF pages), and when the user jumps to a page that's not in the fullsize PDF cache, it would request rendering the low resolution thumbnails, and when the fullsize PDF cache is ready, the thumbnails will be replaced? 
---

### Q2.2: What resolution should thumbnails be rendered at?

**Options:**
- A) Fixed DPI (e.g., 72 DPI regardless of display)
- B) Match device DPI (high DPI displays get sharper thumbnails)
- C) Lower than device DPI (e.g., 50% of device DPI for performance)

**Considerations:**
- Higher DPI = more rendering time and memory
- Low DPI thumbnails may look blurry on high-DPI displays
- Thumbnails are small, so lower DPI may be acceptable

**My Recommendation:** Option C - use lower DPI for thumbnails (maybe 72 DPI or 50% of screen DPI). The small size hides most quality loss.

**Answer:**
Definitely LOWER, but not a fixed number. You think this way, if the thumbnail widget is 120px wide, assuming the aspect ratio is 5:3, the thumbnail will be 200x120 px logical, and the device pixel ratio is 2 (96x2=192 dpi), the PDF needs to be rendered to 400x240px absolute, which can then determine the thumbnail rendering DPI. 
In short, the PDF rendering DPI is adaptive so that it will always be pixel-to-pixel on the thumbnail. 
---

### Q2.3: Should thumbnails be cached to disk?

**Options:**
- A) Memory cache only (regenerate on app restart)
- B) Disk cache inside .snb bundle
- C) Disk cache in app temp folder
- D) No caching (always regenerate)

**Considerations:**
- Disk cache speeds up reopening documents
- Cache invalidation complexity (when page content changes)
- .snb bundle size increase
- Temp folder cache doesn't persist

**My Recommendation:** Option A - memory cache only, regenerate on app restart. Keeps it simple and avoids cache invalidation issues. We can add disk caching later if needed.

**Answer:**
I agree with Option A. 
---

### Q2.4: How should thumbnail cache invalidation work?

When a page's content changes (strokes added, objects inserted), when should the thumbnail update?

**Options:**
- A) Immediate update (may cause lag during drawing)
- B) Debounced update (wait 500ms after last change)
- C) Update only when panel becomes visible
- D) Manual refresh button

**Considerations:**
- Immediate updates during drawing could cause stuttering
- User expects thumbnails to eventually reflect changes
- Panel may not be visible during drawing

**My Recommendation:** Option B + C combined - debounced update (500ms) when panel is visible, OR update when panel becomes visible if changes occurred while hidden.

**Answer:**
I agree with your recommendation. 
---

### Q2.5: What should be shown while thumbnail is loading?

**Options:**
- A) Blank rectangle with page number
- B) Loading spinner
- C) Low-quality placeholder (solid color based on page background)
- D) Skeleton animation

**Considerations:**
- Should indicate that content is coming
- Not too distracting
- Simple to implement

**My Recommendation:** Option C - show page background color (or gray for PDF pages) as placeholder, with page number visible. No spinner needed for fast loads.

**Answer:**
I agree with Option C. 
---

## Section 3: Navigation

### Q3.1: What happens when user clicks a thumbnail?

**Options:**
- A) Navigate to page, scroll to top
- B) Navigate to page, maintain relative scroll position
- C) Navigate and center page in viewport
- D) Navigate with smooth scroll animation

**Considerations:**
- Consistency with Outline panel (scrollToPage)
- User expectation

**My Recommendation:** Option A - use existing `scrollToPage()` which scrolls to show page at top of viewport.

**Answer:**
I agree with Option A. 
---

### Q3.2: Should the Page Panel auto-scroll to show current page?

**Options:**
- A) Always auto-scroll to current page
- B) Auto-scroll only when page changes significantly (e.g., > 3 pages away)
- C) Never auto-scroll (user maintains scroll position)
- D) Auto-scroll with user override (stop if user manually scrolls)

**Considerations:**
- User may be browsing thumbnails while viewing a different page
- Jarring if panel keeps jumping around
- Helpful for finding current position in large documents

**My Recommendation:** Option B - auto-scroll only when current page is not visible in the panel, and only after navigation (not during regular scrolling).

**Answer:**
I agree with your recommendation.
---

### Q3.3: Should double-click do something different from single-click?

**Options:**
- A) Single-click navigates, double-click does nothing special
- B) Single-click selects, double-click navigates
- C) Single-click navigates, double-click opens page in new tab
- D) Single-click navigates, double-click enters page rename/edit mode

**Considerations:**
- Simplicity
- Consistency with Outline panel (single-click navigates)
- Touch devices don't have double-click

**My Recommendation:** Option A - keep it simple. Single-click navigates, double-click is just two navigations.

**Answer:**
I agree with your recommendation.
---

## Section 4: Page Management Actions

### Q4.1: What page management actions should be available?

**Potential actions:**
- [x ] Add page (after current or at end?)
- [x ] Delete page
- [ ] Duplicate page
- [ ] Move page up/down (reorder)
- [ ] Drag-and-drop reorder
- [ ] Copy page
- [ ] Paste page
And there is an insert page after the current page feature. 

**Note:** User mentioned "Drag to reorder pages" might not be implemented in DocumentViewport. Need to verify what's supported.

**Questions:**
1. Which actions should be available?
2. For PDF documents, which actions are allowed? (Can't delete/reorder pages in a PDF?)

**Answer:**
1. I already answered it by ticking on the options. 
2. Deleting an INSERTED page (that doesn't contain PDF background) is allowed. Reordering pags in a PDF is not. 
---

### Q4.2: How should page actions be accessed?

**Options:**
- A) Buttons at top/bottom of Page Panel
- B) Context menu (right-click / long-press)
- C) Action bar when page is selected
- D) All of the above

**Considerations:**
- User mentioned wanting a PagePanelActionBar
- Consistency with other action bars (ObjectSelect, Lasso, etc.)
- Touch-friendly access

**My Recommendation:** Option D - buttons at top for common actions (Add), context menu for all actions, and action bar for the currently selected/highlighted page.

**Answer:**
I think you TOTALLY forgot the fact that `source/ui/actionbars/ActionBar.cpp` exists. There will be another type of ActionBar when the page panel is toggled. It would contain the keyboard shortcuts for the pages, as well as a page navigation system that follows the same style as the other buttons on the action bars. 
It should be something like this:

page up

page number display (round but spinbox-like), but needs to be SPINABLE BY TOUCH INPUTS, LIKE THE iPhone alarm clock wheel. This is also a custom widget. 

page down (both page up and page downs are already connected to mainwindow)
---
add page

insert page

delete page
---

### Q4.3: Should there be a "multi-select" mode for batch operations?

**Options:**
- A) No multi-select (operations on single pages only)
- B) Ctrl/Cmd+click for multi-select
- C) Shift+click for range select
- D) Both B and C
- E) Checkbox mode for touch devices

**Considerations:**
- Batch delete could be useful
- Complicates UI significantly
- Touch devices can't easily Ctrl+click

**My Recommendation:** Option A for initial implementation - single page operations only. Can add multi-select as future enhancement.

**Answer:**
I agree with Option A. 
---

### Q4.4: What confirmation should be required for delete?

**Options:**
- A) No confirmation (instant delete, rely on undo)
- B) Always confirm with dialog
- C) Confirm only if page has content
- D) Confirm with undo toast (like Gmail)

**Considerations:**
- Undo is available for page operations
- Accidental deletes could lose work
- Confirmation dialogs are annoying for intentional deletes

**My Recommendation:** Option D - show toast "Page deleted" with Undo button, auto-dismiss after 5 seconds. No blocking dialog.

**Answer:**
I agree with your choice. 
---

### Q4.5: Where should new pages be inserted?

When user clicks "Add Page":

**Options:**
- A) Always at end of document
- B) After current page
- C) After selected thumbnail (if different from current)
- D) Ask user (dialog with options)

**Considerations:**
- Consistency with existing Ctrl+Shift+A behavior
- User expectation

**My Recommendation:** Option B - insert after current page (consistent with keyboard shortcut behavior).

**Answer:**
I agree with Option B. 
---

### Q4.6: Can pages be reordered via drag-and-drop?

**Questions:**
1. Is page reordering implemented in Document/DocumentViewport?
2. If not, should we implement it now or defer?
3. How complex would drag-and-drop implementation be?

**Considerations:**
- Drag-and-drop is intuitive but complex to implement well
- Would need to update Document structure, undo/redo, etc.
- Alternative: Move Up/Down buttons

**DISCOVERED:** `Document::movePage(int from, int to)` **ALREADY EXISTS!** The core functionality is implemented and tested. We just need UI to expose it.

**My Recommendation:** Start with Move Up/Down buttons (simple). Drag-and-drop can be added as future enhancement.

**Answer:**
We actually never tested the movePage function before... So I'm not sure if it works properly. 
But drag and drop will be very nice UX. Page documents have in-page undo stacks, so I don't think undo behavior gets affected here. 
Can you give me some recommendation on the UX for this? 
---

## Section 5: Action Bar Integration

### Q5.1: Should there be a PagePanelActionBar?

**Options:**
- A) Yes, dedicated action bar for page operations
- B) No, use existing UI (buttons, context menu)
- C) Reuse existing action bars if applicable

**Considerations:**
- User explicitly mentioned PagePanelActionBar
- Consistency with other action bars
- What actions would it contain?

**Potential PagePanelActionBar actions:**
- Delete Page
- Duplicate Page  
- Move Up
- Move Down
- (Add Page is better as a persistent button)

**My Recommendation:** Option A - create PagePanelActionBar with: Delete, Duplicate, Move Up, Move Down. Show when a page thumbnail is selected/tapped.

**Answer:**
I explained this earlier. 
---

### Q5.2: When should PagePanelActionBar appear?

**Options:**
- A) When user clicks/taps a thumbnail
- B) When user long-presses a thumbnail
- C) When a page is "selected" (separate from current page)
- D) Always visible at bottom of panel

**Considerations:**
- Click already navigates - would action bar appear too?
- Need clear distinction between "navigate to page" and "manage page"
- Touch vs mouse UX

**My Recommendation:** Need clarification - if click navigates, how do we select for management? Options:
- Long-press to select (shows action bar)
- Click navigates AND shows action bar for that page
- Separate selection mode

**Answer:**
As long as the page panel is toggled, the page panel action bar always exists. But remember, the action bar actually lies in the right side of the DocumentViewport (position wise), and not on the page panel itself. 
---

### Q5.3: Where should PagePanelActionBar be positioned?

**Options:**
- A) Inside Page Panel (bottom of panel)
- B) Same position as other action bars (right side of viewport)
- C) Floating near selected thumbnail

**Considerations:**
- Other action bars are on right side of viewport
- But Page Panel is on left side
- User's hand position when using touch

**My Recommendation:** Option A - inside Page Panel at bottom, since the user's attention is already on the left side when using the panel.

**Answer:**
Option B. The UX makes total sense. The user uses the left hand (touch input to control (slide across) the page thumbnails on the page panel, and uses the right hand (stylus input) to control the page panel action bar. )
---

## Section 6: Keyboard Shortcuts

### Q6.1: Which existing shortcuts should integrate with Page Panel?

**DISCOVERED - Existing shortcuts:**
- `Ctrl+Shift+A` - Add page at END of document (`addPageToDocument()`)
- `Ctrl+Shift+I` - Insert page AFTER current page (`insertPageInDocument()`)
- `Ctrl+Shift+D` - Delete current page (`deletePageInDocument()`)

**Existing Document methods:**
- `Document::addPage()` - add at end
- `Document::insertPage(int index)` - insert at position
- `Document::removePage(int index)` - remove page (can't remove last)
- `Document::movePage(int from, int to)` - reorder pages

**Questions:**
1. Should shortcuts work even when Page Panel is not visible? (Currently yes)
2. Should Page Panel get focus to receive additional shortcuts (arrow keys)?
3. Should we add shortcut for Move Up/Down?

**Answer:**
You ignored the switchPage on mainwindow, and the switch fowards/backwards  (connected to mouse side buttons) . I showed you earlier. 
---

### Q6.2: Should arrow keys navigate thumbnails when panel has focus?

**Options:**
- A) Yes, Up/Down moves selection
- B) No, arrow keys do nothing
- C) Arrow keys scroll the panel (not change selection)

**Considerations:**
- Keyboard accessibility
- Consistency with other list/tree widgets

**My Recommendation:** Option A - arrow keys move selection when panel has focus.

**Answer:**
They are already mapped to the mouse side buttons. Don't just hard code the arrow key controls here. They will be reconnected to the keyboard shortcut hub (and become customizable) after the control panel is reconnected. 
---

## Section 7: PDF Documents

### Q7.1: What page operations are allowed for PDF documents?

**Options:**
- A) All operations (add, delete, reorder) - modify the annotation layer, not PDF
- B) No modifications - PDF pages are immutable
- C) Some operations (add blank pages, but can't delete PDF pages)

**Considerations:**
- Our PDF handling adds an annotation layer on top
- Can we insert blank pages between PDF pages? Yes. We tested this. 
- Can we "delete" a PDF page (hide it)? No. 

**My Recommendation:** Need technical clarification on what's currently supported.

**Answer:**
You may refer to the Phase 1.2 Document and Phase 1.3 DocumentViewport subplan for more details. 
---

### Q7.2: Should PDF page thumbnails show the PDF content or our rendered version?

**Options:**
- A) PDF content only (from PdfProvider)
- B) Our rendered version (PDF + strokes + objects)
- C) PDF content with strokes overlay indicator

**Considerations:**
- Rendering our full version is slower
- But user expects to see their annotations
- PDF-only thumbnails miss user's work

**My Recommendation:** Option B - show full rendered version including strokes and objects. User expects thumbnails to show their current work.

**Answer:**
I don't know decreasing the thumbnail dpi will exponentially decrease the rendering speed. If 48 dpi really only takes 1/16 the time to render than 192 dpi for both PDF and the strokes (all layers, but likely not that many layers) (the numbers are just examples), Option B makes sense. 
---

## Section 8: State Management

### Q8.1: Should Page Panel have per-tab state?

**Options:**
- A) Yes, remember scroll position and layout per tab
- B) No, shared state across all tabs
- C) Remember scroll position only, layout is global

**Considerations:**
- Consistency with Outline panel behavior
- User expectation when switching tabs

**My Recommendation:** Option C - scroll position per tab, layout toggle is global (QSettings).

**Answer:**
I agree with your choice. 
---

### Q8.2: Should panel state persist across app restarts?

**What to persist:**
- [ ] Column layout (1 or 2 columns)
- [ ] Scroll position (probably not useful)
- [ ] Panel visibility (handled by sidebar container)

**Considerations:**
- Layout preference is user preference
- Scroll position changes frequently, less useful to persist

**My Recommendation:** Persist column layout to QSettings. Don't persist scroll position.

**Answer:**
There shouldn't really be a 2 column option for the page panel (though 2 column layout exists on the DocumentViewport)
---

## Section 9: Visual Design

### Q9.1: Should thumbnails have rounded corners?

**Options:**
- A) Yes, consistent with app aesthetic
- B) No, sharp corners for document-like appearance
- C) Slight rounding (2-4px radius)

**My Recommendation:** Option C - slight rounding to match other UI elements.

**Answer:**
I agree with Option C. 
---

### Q9.2: Should there be a shadow or border around thumbnails?

**Options:**
- A) Drop shadow (depth effect)
- B) Thin border (1px neutral)
- C) No decoration (clean look)
- D) Border on hover/select only

**Considerations:**
- Visual separation between thumbnails
- Performance (shadows are slightly heavier)

**My Recommendation:** Option B - thin neutral border always, thicker accent border for current page.

**Answer:**
I agree with your recommendation. 
---

### Q9.3: What spacing should be between thumbnails?

**Options:**
- A) Tight spacing (4-8px)
- B) Medium spacing (12-16px)
- C) Generous spacing (20-24px)

**Considerations:**
- Touch targets need some separation
- More spacing = less content visible

**My Recommendation:** Option B - 12px spacing vertically.

**Answer:**
I agree with Option B. 
---

## Section 10: Implementation Questions

### Q10.1: Should we create a new custom widget or use QListWidget?

**Options:**
- A) Custom widget with QScrollArea + manual layout
- B) QListWidget with custom delegate
- C) QListView with custom model + delegate

**Considerations:**
- QListWidget is simpler but less flexible
- Custom widget gives full control over rendering
- QListView + model is most Qt-idiomatic for this use case

**My Recommendation:** Option B - QListWidget with custom delegate (QStyledItemDelegate) like we did for OutlinePanel. Simpler than full MVC but flexible enough.

**Answer:**
I don't know if performance is going to be an issue for Option B. For a ~3000-page document, is Option B going to cause performance issues... (while the same document only has ~100 or so pdf outline items, so that one is fine). 
---

### Q10.2: How should thumbnail rendering be threaded?

**Options:**
- A) Main thread (may cause UI lag)
- B) Dedicated background thread
- C) Qt Concurrent / thread pool
- D) Same approach as PDF cache (async rendering)

**Considerations:**
- Heavy rendering on main thread causes stutter
- Need to update UI when render completes
- Our PDF cache already does async rendering

**My Recommendation:** Option D - reuse/extend our existing async rendering approach from PDF cache.

**Answer:**
I agree with Option D. Definitely async. 
---

## Summary Questions

Before I create the implementation plan, please confirm:

1. **Column layout:** Toggle between 1/2 columns?
2. **Thumbnail generation:** Lazy load with placeholders?
3. **Page actions:** What operations are supported? (Add, Delete, Move, Duplicate?)
4. **Action bar:** Dedicated PagePanelActionBar inside panel?
5. **PDF pages:** What operations allowed on PDF documents?
6. **Drag-and-drop reorder:** Implement now or defer?
7. **Selection model:** Click navigates + shows action bar, or separate selection?

---

## Additional Concerns Raised

**Performance on Intel Z3735E:**
- Thumbnail rendering must be lightweight
- Consider capping thumbnail resolution
- Lazy loading is essential

**Action bar shortcuts:**
- Need to verify existing Ctrl+Shift+A, Ctrl+Shift+I behavior
- Delete page shortcut?

**Edgeless not supported:**
- Hide Page Panel tab for edgeless documents
- Same pattern as Outline panel

**Touchscreen support**
- The Page Panel needs to support touchscreen and mouse inputs, just like the PDF outline panel. 

---



## Section 11: Additional Questions (Added by AI)

### Q11.1: PagePanelActionBar - Spinnable Page Number Widget

You mentioned an iPhone alarm clock-style wheel for page navigation. Let me clarify:

**Visual concept:**
```
     ┌───────────┐
     │    12     │  ← dimmed, smaller
     │    13     │  ← dimmed, smaller
     │  [ 14 ]   │  ← current, highlighted, larger
     │    15     │  ← dimmed, smaller
     │    16     │  ← dimmed, smaller
     └───────────┘
```
Also the container is round (not rectangular) like every other button on the action bar. This may be another custom widget like the color buttons on the subtoolbars. 

**Questions:**
1. How many pages visible in the wheel at once? (3? 5?) Answer: 3. 
2. Should swiping/scrolling be velocity-based (flick to scroll fast)? Yes, with inertia scrolling. 
3. Should it snap to whole page numbers or allow smooth scrolling? Allow smooth scrolling and snap to whole page number when it slows down. If you find this consumes too much performance you may change the plan. 
4. On selection, does it navigate immediately or require confirmation? Navigate immediately. 

**My Recommendation:** 
- Show 3-5 pages in wheel
- Velocity-based scrolling with snap-to-page
- Navigate immediately on selection (no confirm button)
- Consider using QML for smoother animation, or custom paint with physics  (For QML, will this immediately make SpeedyNote really heavy? I doubt if it will work well with the rest of the application... )

**Answer:**

---

### Q11.2: PagePanelActionBar Visibility

You said the action bar appears when Page Panel is toggled. Questions:

1. Does it REPLACE the existing action bar (Object/Lasso selection bar)?
2. Or does it appear in addition to / alongside other action bars?
3. If user has a lasso selection AND Page Panel open, which action bar shows?

**My Recommendation:** 
- Context priority: Selection action bar > Page Panel action bar
- If user has active selection, show selection action bar
- Otherwise, show Page Panel action bar when panel is visible

**Answer:**
I think the better way is to modify the `ActionBarContainer.cpp` to make it support 2 action bars (if necessary). When no other action bar is present, the pagepanelactionbar takes the place just like any other 1 column action bar. 
When there is another action bar being active (imagine this use case, the user copies an InsertedObject, and keeps it selected, and he opens the page panel to navigate to another page, and then paste the InsertedObject), the PagePanelActionBar needs to be positioned to the left of the other action bar and form another column. Apart from the PagePanelActionBar, there should always be one action bar on display. 
---

### Q11.3: Page Panel Tab Behavior in Sidebar

Currently the left sidebar has tabs (Layers, Outline for PDF). Questions:

1. Is Page Panel a new tab alongside these? Yes. 
2. What's the tab order? (Outline → Pages → Layers?) Yes, just like what you described. 
3. Should Page Panel tab be hidden for edgeless (like Outline is hidden for non-PDF)? Yes. 

**My Recommendation:**
- New tab in sidebar: Outline | Pages | Layers
- Hide for edgeless documents
- Default to Pages tab for new documents (unless PDF with outline)

**Answer:**

---

### Q11.4: Thumbnail Content - What Exactly Gets Rendered?

For each page thumbnail, confirm what's included:

- [x] Page background (color/grid/lines/image)
- [?] PDF content (if PDF page)
- [?] All vector layers (or just visible layers?)
- [?] Inserted objects (images, links)
- [ ] Selection highlights (probably not)
- [ ] Current stroke being drawn (probably not)

**My Recommendation:**
- Include: background, PDF, ALL layers (regardless of visibility toggle), objects
- Exclude: selection state, in-progress strokes

**Answer:**
I agree with this. 
---

### Q11.5: Existing Page Navigation - What Already Exists?

I should understand what's already implemented. From your mention:

| Feature | Location | Description |
|---------|----------|-------------|
| `switchPage(int)` | MainWindow | Navigate to specific page |
| Forward/Backward | Mouse side buttons | Navigate ±1 page |
| Page spinbox | Toolbar area? | Direct page number input |
| Ctrl+Shift+A | Shortcut | Add page at end |
| Ctrl+Shift+I | Shortcut | Insert page after current |
| Ctrl+Shift+D | Shortcut | Delete current page |

**Questions:**
1. Is this list complete? The page spinbox is already REMOVED. 
2. Are there Page Up/Page Down keyboard shortcuts currently? I don't think so. But the mouse side button shortcuts are already connected. 
3. Does the existing page spinbox work the way you want, or does it need replacement? The existing page spinbox is stubbed and mostly deleted. It should no longer exist and be replaced with the new widget (the iPhone alarm-like one). 
It should be connected to 

```cpp

void MainWindow::switchPage(int pageIndex) {
    // Phase S4: Main page switching function - everything goes through here
    // pageIndex is 0-based internally
    DocumentViewport* vp = currentViewport();
    if (!vp) return;
    
    vp->scrollToPage(pageIndex);
    // pageInput update is handled by currentPageChanged signal connection
}
```
and 
```cpp
void MainWindow::goToPreviousPage() {
    // Phase S4: Thin wrapper - go to previous page (0-based)
    DocumentViewport* vp = currentViewport();
    if (!vp) return;
    switchPage(vp->currentPageIndex() - 1);
}

void MainWindow::goToNextPage() {
    // Phase S4: Thin wrapper - go to next page (0-based)
    DocumentViewport* vp = currentViewport();
    if (!vp) return;
    switchPage(vp->currentPageIndex() + 1);
}
```
these. 

**Answer:**

---

### Q11.6: Document::movePage() Testing

You mentioned `movePage()` was never tested. Before implementing drag-and-drop, we should verify:

1. Does `movePage(from, to)` work correctly?
2. Does it properly update page indices for bookmarks?
3. Does it handle edge cases (move to same position, move first/last page)?
4. Is there undo support for page moves?

**My Recommendation:** Add unit tests for movePage() before building drag-and-drop UI.

**Answer:**
I agree with your recommendation. 
---

### Q11.7: Touch Scrolling in Page Panel

You mentioned the panel needs touch support. Specific questions:

1. Does Qt's QListView/QScrollArea support touch scrolling out of the box on Linux?
2. Do we need custom touch handling like DocumentViewport?
3. Should there be kinetic/momentum scrolling?

**My Recommendation:** Test Qt's built-in touch scrolling first. If inadequate, may need custom QScroller setup.

**Answer:**
Sure. The touch support featured in the PDF Outline Panel is good enough. It should be good enough here. 
---

*Please answer the questions above, and feel free to add any additional requirements or constraints I may have missed!*

## Additional responses from your suggestions:

### Q2.1 

I agree with your choice. Low DPI thumbnails are guaranteed to be pixel-to-pixel (since we calculate the DPI based on the page size and widget size), so the quality is already as good as it gets for the limited widget size. 

### Q4.6

Up/Down buttons will not be present in the final version. It will be testing only. So you may not plan on designing any UI elements for this, but connecting it to keyboard shortcuts for testing. 

### Q10.1

I agree with Option C (QListView + QAbstractListModel + custom delegate).

---

## Section 12: Final Clarifications (Added by AI)

### Q12.1: ActionBarContainer 2-Column Layout Details

You mentioned the PagePanelActionBar should form a second column when another action bar is active. Let me confirm the exact layout:

**Scenario 1: Only Page Panel open (no selection)**
```
                    ┌─────────────┐
                    │ [Page Up]   │
                    │ [Wheel 14]  │  ← PagePanelActionBar (single column, right side)
                    │ [Page Down] │
                    ├─────────────┤
                    │ [Add]       │
                    │ [Insert]    │
                    │ [Delete]    │
                    └─────────────┘
```

**Scenario 2: Page Panel open + Object selected**
```
        ┌─────────────┐  ┌─────────────┐
        │ [Page Up]   │  │ [Copy]      │
        │ [Wheel 14]  │  │ [Paste]     │  ← Two columns side by side
        │ [Page Down] │  │ [Delete]    │
        ├─────────────┤  │ [...]       │
        │ [Add]       │  └─────────────┘
        │ [Insert]    │
        │ [Delete]    │
        └─────────────┘
           Left            Right
      (PagePanel)    (Selection/Object)
```

**Questions:**
1. Is this layout correct? PagePanelActionBar always on LEFT when 2 columns?
2. What's the gap between the two columns? Same as internal button spacing?
3. When Page Panel tab is closed, does PagePanelActionBar disappear (leaving only selection bar)?

**Answer:**

---

### Q12.2: PagePanelActionBar - When Exactly Is It Visible?

I want to confirm the visibility rules:

| Condition | PagePanelActionBar Visible? |
|-----------|----------------------------|
| Page Panel tab is selected/open | Yes |
| Page Panel tab is NOT selected (e.g., Layers tab) | ? |
| Edgeless document (no Page Panel tab) | No |
| Switching tabs (Document tabs, not sidebar) | ? |

**My assumption:** PagePanelActionBar is visible **only when the Page Panel tab in the sidebar is the active tab**, regardless of what's happening in the viewport.

**Answer:**

---

### Q12.3: PageWheelPicker - Size and Touch Target

For the iPhone-style wheel picker in a round container:

**Visual mockup:**
```
      ┌─────────────┐
      │      13     │  ← small, dimmed (20% opacity?)
      │    [ 14 ]   │  ← full size, bright, centered
      │      15     │  ← small, dimmed
      └─────────────┘
         ~48x48px?
```

**Questions:**
1. What diameter for the round container? 
2. Should the wheel picker be the SAME width as other action bar buttons, or can it be taller/different? 
3. Numbers only, or also show "Page" prefix? (e.g., "14" vs "Page 14") No prefix. 

**My Recommendation:**
- Same width as other buttons (36px) but allow taller height (~72px) to fit 3 numbers... 
- Numbers only (space is tight)
- Font size: center = 14px bold, above/below = 10px light

**Answer:**
Your recommendation (36x72px with border radius of 18px) is interesting. So it will be a hotdog-shaped widget, instead of being 100% round. It still fits in though, which is nice. 
I agree with your other choices as well. 
---

### Q12.4: Delete Page Confirmation Toast

From Q4.4, you agreed with Option D (undo toast like Gmail). Questions:

1. Where should the toast appear? (Bottom center? Bottom right? Near action bar?)
2. How long before auto-dismiss? (5 seconds as suggested?)
3. Should the toast be the same component used elsewhere, or new?

**My Recommendation:**
- Bottom center of viewport (not blocking sidebar or action bars)
- 5 second auto-dismiss with Undo button
- Create reusable `UndoToast` widget if one doesn't exist, or reuse existing notification system

**Answer:**
I have a good idea. Make the delete button in the pagepanelactionbar a custom widget, so that it will turn itself into the toast after clicking on it. 
---

### Q12.5: Drag-and-Drop - Visual Feedback During Drag

You approved drag-and-drop reorder. Specific visual questions:

1. **Drag initiation indicator:** Should there be a "drag handle" icon on each thumbnail, or just long-press anywhere?
2. **Drop indicator:** Horizontal line between thumbnails, or gap that opens up?
3. **Scroll speed:** When dragging near edges, how fast should auto-scroll be?

**My Recommendation:**
- No drag handle (keep UI clean) - long-press anywhere on thumbnail initiates drag
- Gap opens up between thumbnails (more intuitive than line)
- Auto-scroll: start slow (2 thumbnails/sec), accelerate the closer to edge

**Answer:**
I agree with your recommendations.

---

### Q12.6: Delete Button → Toast Transformation (Clarification)

You suggested the delete button transforms into a toast after clicking. Let me clarify:

**Visual concept:**
```
BEFORE CLICK:              AFTER CLICK (toast state):
┌─────────────┐            ┌─────────────────────────────┐
│ [Delete 🗑]  │    →→→    │ Page deleted [Undo]         │
└─────────────┘            └─────────────────────────────┘
                           (expands width? same position?)
```

**Questions:**
1. Does the button expand in-place (within action bar), or does it float/pop out? No expansion needed. 
2. What happens to other action bar buttons during toast state? (Hidden? Pushed aside?) Nothing happens. 
3. After 5 seconds or Undo click, does it shrink back to normal delete button? Yes. 
4. If user clicks another action while toast is showing, what happens? I agree with your recommendation. 

**My Recommendation:**
- Button expands in-place within the action bar column
- Other buttons remain visible (toast just takes more width temporarily)
- Shrinks back after timeout or action
- Any other action dismisses the toast (confirms delete)

**Answer:**
The buttons never show any text. It just changes the icon from the bin to an undo icon.
---

### Q12.7: PDF Document - Drag-and-Drop Restrictions

We established PDF pages can't be deleted or reordered. For drag-and-drop:

**Scenario:** User opens a PDF, inserts a blank page between page 5 and 6. Can they drag that inserted page?

**Options:**
- A) No drag-and-drop at all in PDF documents
- B) Only inserted (non-PDF) pages can be dragged
- C) Inserted pages can be dragged, but only between PDF pages (can't reorder PDF pages)

**My Recommendation:** Option B - only inserted pages show drag behavior. PDF pages show no drag affordance.

**Answer:**
I agree with Option B. 
---

### Q12.8: Action Bar Icons - Do They Exist?

For PagePanelActionBar, we need icons:

| Button | Icon Needed | Exists? |
|--------|-------------|---------|
| Page Up | Arrow up or chevron up | ? |
| Page Down | Arrow down or chevron down | ? |
| Add Page | Plus icon | ? |
| Insert Page | Plus with line? Insert icon? | ? |
| Delete Page | Trash/bin icon | ? |

**Questions:**
1. Do these icons already exist in `resources/icons/`?
2. If not, should I plan to create placeholders or use text labels initially?
3. Should icons be the same style as toolbar icons?

**Answer:**
1 and 2. You can assume that they exist for now. Pay attention to the light/dark color switch. You may take a look at how other action bars handle this. I'll replace the file names with the actual ones later. 
3. No. They should be the same as OTHER action bar icons. There are multiple action bars that share the same general architecture. 
---

## REMINDER: Unanswered Questions

**Q12.1** and **Q12.2** about ActionBarContainer layout are still unanswered:

### Q12.1 (Still needs answer):
1. Is the 2-column layout correct? PagePanelActionBar always on LEFT?  Yes, both are correct. 
2. What's the gap between the two columns?  24px logical. 
3. When Page Panel tab is closed, does PagePanelActionBar disappear? Yes. 

### Q12.2 (Still needs answer):
When exactly is PagePanelActionBar visible?
- Only when Page Panel sidebar tab is selected? Yes
- Or whenever a paged document is open (regardless of sidebar tab)?  No. 