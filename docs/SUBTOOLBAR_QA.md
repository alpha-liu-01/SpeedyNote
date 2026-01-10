# Subtoolbar Implementation Q&A

**Document Version:** 1.5  
**Date:** January 9, 2026  
**Status:** ✅ COMPLETE  
**Purpose:** Clarify requirements before implementing subtoolbars (Phase D.2-D.3)

---

## Background

From previous discussions:
- Subtoolbars are **always visible** on the left of DocumentViewport
- **Vertical/upright design**, floating over the viewport
- **One subtoolbar per tool** - switching tools swaps the subtoolbar
- Each tool has different customization needs

Existing plan mentions:
- PenSubToolbar, MarkerSubToolbar, EraserSubToolbar
- ObjectSelectSubToolbar (for LinkObject modes)
- LassoSubToolbar, HighlighterSubToolbar

---

## Section 1: Common Subtoolbar Framework

### Q1.1: Base Class Design
Should subtoolbars share a common base class?
- A) Yes, abstract `SubToolbar` base class with virtual methods
- B) Yes, but just a `QWidget` subclass with shared styling
- C) No base class, just follow a naming convention
- D) Use a template/factory pattern
- E) Other: ___

**Answer:**
Option A. 
---

### Q1.2: Shared Attributes
What attributes should ALL subtoolbars share? (Check all that apply)
- [x ] Fixed width (specify: ___ px)
- [ ] Semi-transparent background
- [x ] Rounded corners
- [x ] Shadow/border
- [ ] Drag handle to reposition
- [ ] Collapse/expand toggle
- [ ] Tool icon at top
- [x ] Other: ___

**Answer:**

---

### Q1.3: Subtoolbar Container
How should the subtoolbar be managed in the layout?
- A) Direct child of DocumentViewport (floating with absolute positioning)
- B) Child of MainWindow, positioned over viewport
- C) Managed by a `SubToolbarContainer` widget that handles swapping
- D) `QStackedWidget` containing all subtoolbars
- E) Other: ___

**Answer:**
From what I can see, Option C seems nice. What's the benifits and the downsides of Option C? 

**Claude's Analysis - Option C (SubToolbarContainer):**

✅ **Benefits:**
- **Single point of control** - One widget manages all subtoolbar swapping logic
- **Easier positioning** - Container handles position, individual subtoolbars don't need to know
- **Clean transitions** - Can add fade/slide animations between subtoolbars in one place
- **Memory management** - Container can lazy-load subtoolbars (create on first use) or keep all in memory
- **Consistent styling** - Container provides the frame/shadow, subtoolbars just provide content

❌ **Downsides:**
- **One more class** - Slightly more abstraction
- **Signal routing** - Signals from subtoolbars may need to pass through container (minor)

**Recommendation:** Option C is the cleanest approach. The container would:
1. Receive `toolChanged(ToolType)` signal from MainWindow
2. Swap to appropriate subtoolbar (or hide if tool has none)
3. Handle positioning relative to DocumentViewport
4. Provide the visual frame (rounded corners, shadow)

---

### Q1.4: Position & Docking
Exact position of subtoolbar:
- A) Fixed left edge, vertically centered
- B) Fixed left edge, top-aligned
- C) Slight offset from left edge (specify margin: ___ px)
- D) User-draggable but snaps to left/right edge
- E) Other: ___

**Answer:**
Option E. It should be positioned a bit to the right of the left edge of the DocumentViewport, and float on it. This is just the positioning. 
---

## Section 2: Per-Tool Subtoolbar Contents

### Q2.1: Pen Subtoolbar
What should the Pen subtoolbar contain? (Check and specify)
- [X ] Thickness presets (how many? _3__)
- [ ] Thickness slider (range: ___ to ___)
- [X ] Color presets (how many? _3__)
- [X ] Color picker button (opens dialog)
- [ ] Pen type toggle (ballpoint/fountain/etc.)
- [ ] Straight line mode toggle
- [ ] Other: ___

**Answer:**
It works like this. The top 3 buttons are of a type of buttons that:
1. A circle with the color filled in it
2. Click an unselected button will switch to this color
3. Click a selected button will open the color setting dialog.
4. Changing the color in the colot setting dialog will change the button color fill. 
-------------------------A GAP----------------------------------------
The bottom 3 buttons are thickness presets. There is a similar circle display for each button, but within the button, it will display a number that shows the current thickness (or if it's possible, a demonstration of the current thickness)

The workflow is about the same as color buttons, but the settings dialog contains a pair of connected slider and spinbox, instead of the Qt color dialog we used earlier (even in previous versions). 

Remember these type of buttons. They will defined and reused in a lot of subtoolbars. 
---

### Q2.2: Marker Subtoolbar
What should the Marker subtoolbar contain?
- [x ] Thickness presets (how many? _3__)
- [ ] Thickness slider
- [x ] Color presets (how many? _3__)
- [ ] Color picker button
- [ ] Opacity slider
- [ ] Other: ___

Should Marker and Pen share the same subtoolbar, or be separate?
- A) Same subtoolbar (they're similar)
- B) Separate subtoolbars (cleaner separation)

**Answer:**
They actually look identical. I don't know if it's a better idea to separate them into 2 or merge them... 
But the pen color / thickness presets NEVER share with the marker color /  thickness presets. 

**Claude's Recommendation: Separate subtoolbars (Option B)**

Rationale:
1. **Different state** - Pen and Marker have separate presets, so they need separate storage anyway
2. **Future-proofing** - Marker might get opacity slider later, Pen might get pen-type toggle
3. **Simpler code** - Each subtoolbar manages its own state, no "if pen then X else Y" logic
4. **Same visual design** - Both use the same custom button widgets, just different instances

Implementation approach:
- `PenSubToolbar` and `MarkerSubToolbar` are separate classes
- Both inherit from `SubToolbar` base class
- Both use the same `ColorPresetButton` and `ThicknessPresetButton` widgets
- Each has its own 3+3 preset instances with separate QSettings keys

This keeps code simple while allowing future divergence.

---

### Q2.3: Eraser Subtoolbar
What should the Eraser subtoolbar contain?
- [ ] Size presets (how many? ___)
- [ ] Size slider
- [ ] Mode toggle: Stroke eraser (erase whole stroke)
- [ ] Mode toggle: Pixel eraser (erase partial stroke)
- [ ] Mode toggle: Object eraser (erase whole object)
- [ ] Clear page button
- [x ] Other: ___

**Answer:**
I don't think we need a eraser subtoolbar now... The current implementation is strictly stroke-based and doesn't need much customization at all... So when the user selects the eraser tool, no tool bar will be displayed. 
---

### Q2.4: Highlighter Subtoolbar
What should the Highlighter (text selection) subtoolbar contain?
- [ ] Color presets for highlight strokes
- [x ] Auto-highlight toggle (current keyboard shortcut)
- [ ] Copy text button
- [ ] Search in document button
- [ ] Other: ___

**Answer:**
The copy text feature will be connected to the copy button in the action bar. 
I don't think the whole document is indexed for search... But this notices me. Is it actually really possible to implement a global text search feature that doesn't give the CPU or disk too much burden? 
For searching the highlighted text, the search UI should be located on the right sidebar (currently for markdown notes, but it can also search for LinkObjects with no markdown connections)

**Claude's Analysis - Global PDF Text Search:**

Yes, it's feasible! Two approaches:

**Option 1: On-demand search (simpler)**
- When user searches, iterate through pages calling `PdfProvider::textBoxes(pageIndex)`
- Poppler caches parsed data, so repeated searches are fast
- CPU spike only during search, not during normal use
- Works well for documents <500 pages

**Option 2: Background indexing (more complex)**
- Build search index on document load (background thread)
- Store index in memory or document bundle
- Instant search results
- More memory usage, slower initial load

**Recommendation:** Start with Option 1. PDF text search is fast enough for typical academic/note-taking documents. If users report slowness on 1000+ page documents, add indexing later.

For now, this is **out of scope** for subtoolbar implementation. Can be added to sidebar in a future phase.

---

### Q2.5: Lasso Subtoolbar
What should the Lasso subtoolbar contain?
- [ ] Cut button
- [ ] Copy button
- [ ] Delete button
- [ ] Duplicate button
- [ ] Group/ungroup buttons
- [ ] Transform options (rotate, flip)
- [ ] Other: ___

**Answer:**
I don't think we need a lasso subtoolbar either. 
---

### Q2.6: ObjectSelect Subtoolbar
What should the ObjectSelect subtoolbar contain?

**Insert Mode:**
- [x ] Image insert mode button
- [x ] Link insert mode button

**Action Mode:**
- [x ] Select mode button
- [x ] Create mode button

**LinkObject Slots (when LinkObject selected):**
- [x ] Slot 1 button (shows state: empty/position/url/markdown)
- [x ] Slot 2 button
- [x ] Slot 3 button

**Object Operations:**
- [x ] Delete button
- [ ] Duplicate button
- [x ] Move to front/back (z-order)
- [x ] Change layer affinity
- [ ] Other: ___

**Answer:**
Just like what we planned before. The slot buttons are of a separate button type. 
---

### Q2.7: Other Tools
Are there other tools that need subtoolbars?
- [x ] Shape tool (future) - what shapes?
- [ ] Text tool - font, size, color?
- [ ] Other: ___

**Answer:**
The only shape we've implemented is straight line... So for now, it would just be an always toggled straight line button, and the exact same color buttons from the pen subtoolbar, because the shapes share the same color palette as the pen tool... 

**Claude's Clarification Question:**
Currently, straight line mode is a toggle that works WITH Pen/Marker (not a separate tool). If Shape becomes a separate tool:
- Should it have its OWN subtoolbar (with straight line toggle + color presets)?
- Or should Pen subtoolbar just get a "straight line mode" toggle added?
- Or keep current behavior (shape is a mode, not a tool)?

**Answer:**

**Claude's Recommendation:**

Given that:
1. Straight line mode currently works as a toggle with Pen/Marker
2. Shape shares Pen's color palette
3. You said "always toggled straight line button" for shape subtoolbar

**Simplest approach:** Keep it as a toggle on Pen subtoolbar. No separate "Shape" tool for now.
- Pen subtoolbar gets: [Color1] [Color2] [Color3] --- [Thick1] [Thick2] [Thick3] --- [📏 Straight Line Toggle]
- When future shapes are added (rectangle, circle), THEN create separate Shape tool + subtoolbar

Does this match your vision?
Sure, so this means we are removing the shape button on the tool bar. 
---

## Section 3: Visual Design

### Q3.1: Button Size
What size should subtoolbar buttons be?
- A) Same as toolbar (36×36 logical)
- B) Smaller (24×24 or 28×28)
- C) Larger (44×44 for touch)
- D) Variable (icons vs. sliders have different heights)
- E) Other: ___

**Answer:**
Option A. But the buttons should be fully round (maybe 36x36 with an 18 border radius?)
---

### Q3.2: Grouping
Should subtoolbar items be visually grouped?
- A) No grouping, just vertical list
- B) Separator lines between groups
- C) Labeled sections (e.g., "Size", "Color")
- D) Expandable/collapsible sections
- E) Other: ___

**Answer:**
Option B
---

### Q3.3: Preset Customization
How should users customize presets (colors, thicknesses)?
- A) Long-press opens color picker / slider
- B) Right-click context menu
- C) Dedicated "edit preset" button
- D) Settings dialog only
- E) Other: ___

**Answer:**
I already described this earlier. 
---

### Q3.4: Current Value Indicator
How should the currently active preset be shown?
- A) Border/outline highlight
- B) Background color change
- C) Checkmark overlay
- D) Size change (slightly larger)
- E) Other: ___

**Answer:**
Inside the button. I described this earlier.  So the buttons aren't really buttons any more. 
---

## Section 4: Interaction

### Q4.1: Preset Selection
When user taps a preset:
- A) Apply immediately, no feedback
- B) Apply immediately, brief visual feedback (pulse/flash)
- C) Apply and show toast/tooltip
- D) Other: ___

**Answer:**
The button's border will light up (likely with a bright color). 
---

### Q4.2: Slider Behavior
For thickness/size sliders:
- A) Continuous update while dragging
- B) Update on release only
- C) User preference (settings toggle)
- D) Other: ___

**Answer:**
Update on release only. Option B. 
---

### Q4.3: Color Picker
When opening color picker:
- A) Qt's standard `QColorDialog`
- B) Custom compact color wheel
- C) Preset palette + custom color option
- D) Other: ___

**Answer:**
Option A. I think I used this on a button on mainwindow before. 
---

### Q4.4: Subtoolbar Visibility During Gestures
When user is drawing/erasing, should subtoolbar:
- A) Stay fully visible
- B) Fade to semi-transparent
- C) Auto-hide, reappear when idle
- D) User preference
- E) Other: ___

**Answer:**
My initial plan is stay fully visible. But if it's not hard to make it fade to semitransparent, this is a good choice too. 
---

## Section 5: Technical Implementation

### Q5.1: State Persistence
Should subtoolbar settings persist across sessions?
- A) Yes, all presets saved globally
- B) Yes, but per-document
- C) No, reset to defaults each session
- D) Some settings global, some per-document
- E) Other: ___

**Answer:**
All presets saved globally (for persistence), but each tab should have its own presets stored in memory and the presets will switch on switching to another tab.  
---

### Q5.2: Preset Storage Format
If presets persist, where/how?
- A) QSettings (app config)
- B) JSON file in app config directory
- C) Part of document bundle (per-document)
- D) Database
- E) Other: ___

**Answer:**
Option A. 
---

### Q5.3: Signal/Slot Architecture
How should subtoolbars communicate changes?
- A) Direct signals to DocumentViewport
- B) Through MainWindow relay
- C) Central ToolManager/SettingsManager
- D) Property bindings
- E) Other: ___

**Answer:**
Uh... I don't have much ideas for this... Can you please explain which one makes the most sense for a clear architecture and efficiency? 

**Claude's Analysis - Signal/Slot Options:**

| Option | Pros | Cons |
|--------|------|------|
| **A) Direct to DocumentViewport** | Simple, fast | Tight coupling, subtoolbar needs viewport reference |
| **B) Through MainWindow** | MainWindow already connects everything | Extra hop, MainWindow gets bigger |
| **C) Central ToolManager** | Clean separation, testable | New class to create and maintain |
| **D) Property bindings** | Automatic sync | Qt property bindings can be fragile |

**Recommendation: Option A (Direct signals) with a twist**

Since subtoolbars change tool *settings* (not tool *type*), they should emit signals that DocumentViewport connects to:

```cpp
// In PenSubToolbar:
signals:
    void penColorChanged(QColor color);
    void penThicknessChanged(qreal thickness);

// In MainWindow (during setup):
connect(m_penSubToolbar, &PenSubToolbar::penColorChanged,
        m_viewport, &DocumentViewport::setPenColor);
```

This way:
- Subtoolbars don't need to know about DocumentViewport (loose coupling)
- MainWindow wires connections once during setup (existing pattern)
- DocumentViewport just receives setting changes (clean API)

This is essentially "A with MainWindow doing the wiring" - simple and matches existing architecture.

---

### Q5.4: Undo Integration
Should preset changes be undoable?
- A) No, tool settings are not document operations
- B) Yes, as part of document undo stack
- C) Separate "preferences undo"
- D) Other: ___

**Answer:**
Option A. No. 
---

## Section 6: Priority & Phasing

### Q6.1: Implementation Order
Which subtoolbar should be implemented first?
- A) Pen (most commonly used)
- B) ObjectSelect (needed for LinkObject completion)
- C) Eraser (simple, good for testing framework)
- D) Other: ___

**Answer:**
None of them. I think we should define a few custom button-like widgets first... The other ones would be relatively straightforward. 
---

### Q6.2: MVP Scope
For the first implementation, what's the minimum viable subtoolbar?
- A) Just mode buttons (no presets)
- B) Mode buttons + 3 presets each
- C) Full design from the start
- D) Other: ___

**Answer:**
Somewhere between B and C. I want all the subtoolbars to be complete, except for the ones that don't even have their features complete. 
---

### Q6.3: Action Bar Relationship
The plans mention an "Action Bar" for context-sensitive actions (copy/paste/delete). Relationship to subtoolbars?
- A) Separate widget, appears on selection
- B) Part of each subtoolbar (tool-specific actions)
- C) Both - subtoolbar has tool settings, action bar has operations
- D) Defer action bar, subtoolbar handles everything
- E) Other: ___

**Answer:**
Action bar is separate from the subtoolbars, but the tool actions (and some other global actions that are not related to any tool). For example, when the user is using the highlighter tool, the way he selects some text and click copy using the action bar is not related to whether he has the auto highlight mode on. 
---

## Section 7: Edge Cases

### Q7.1: No Tool Selected
What shows when no document is open or no tool is active?
- A) Empty/placeholder subtoolbar
- B) Hide subtoolbar entirely
- C) Show disabled state
- D) N/A - always have a tool selected
- E) Other: ___

**Answer:**
It's actually impossible that no tool is selected. 
---

### Q7.2: Tool Without Subtoolbar
If a tool has no customization options, should it:
- A) Show empty subtoolbar (maintains consistent layout)
- B) Hide subtoolbar
- C) Show generic "no options" message
- D) Every tool will have something
- E) Other: ___

**Answer:**
Option B. Hide subtoolbar
---

### Q7.3: Viewport Overlap
If subtoolbar overlaps important content, should user be able to:
- A) Drag to opposite side
- B) Collapse to minimal state
- C) Toggle visibility
- D) No accommodation - user can pan document
- E) Other: ___

**Answer:**
Option D for now. 
---

## Summary

### Key Decisions

| Decision | Choice |
|----------|--------|
| Base class | Abstract `SubToolbar` with virtual methods |
| Container | `SubToolbarContainer` manages swapping |
| Position | 24px from left edge, vertically centered per-subtoolbar |
| Button size | 36×36, fully round (18px border radius) |
| Layout | Single column, no labels, tooltips on hover |
| Grouping | Separator lines between groups |
| Signal architecture | Direct signals, MainWindow wires connections |
| Persistence | QSettings (global), per-tab overrides in memory |
| Selection persistence | Per-tab in memory only (not saved to disk) |
| Preset changes | NOT undoable |
| Tools without subtoolbar | Hide subtoolbar entirely |
| Action Bar | Deferred (keyboard shortcuts only for now) |
| Straight line toggle | Stays in TOOLBAR (Shape button → Straight Line toggle) |
| Marker/Highlighter colors | Shared (editing one affects the other) |

### Subtoolbar Matrix

| Tool | Has Subtoolbar | Button Count | Contents |
|------|---------------|--------------|----------|
| Pen | ✅ | 6 | 3 color presets, 3 thickness presets |
| Marker | ✅ | 6 | 3 color presets (shared w/ Highlighter), 3 thickness presets |
| Eraser | ❌ | 0 | None needed |
| Highlighter | ✅ | 4 | 3 color presets (shared w/ Marker), 1 auto-highlight toggle |
| Lasso | ❌ | 0 | None needed |
| ObjectSelect | ✅ | 5 | 2 mode toggles, 3 slot buttons |

**Note:** Straight line toggle stays in TOOLBAR (not subtoolbar). Shape button → Straight Line toggle.

### Custom Widget Types Needed

1. **ColorPresetButton** - Circle with color fill, click unselected=select, click selected=edit
2. **ThicknessPresetButton** - Diagonal line scaled to thickness, click to select/edit
3. **LinkSlotButton** - Shows slot state (empty/position/url/markdown), long-press=delete
4. **ToggleButton** - Simple on/off toggle (auto-highlight, straight line)
5. **ModeToggleButton** - Single button that toggles between 2 states (Insert: Image↔Link, Action: Select↔Create)

**Removed:** ActionButton - deferred to Action Bar phase

### Files to Create

**Phase 1: Custom Widgets**
- `source/ui/widgets/ColorPresetButton.h/cpp` - Color circle, select/edit behavior
- `source/ui/widgets/ThicknessPresetButton.h/cpp` - Diagonal line preview (no number), select/edit
- `source/ui/widgets/ToggleButton.h/cpp` - On/off toggle (auto-highlight)
- `source/ui/widgets/ModeToggleButton.h/cpp` - Two-state toggle (Image↔Link, Select↔Create)
- `source/ui/widgets/LinkSlotButton.h/cpp` - Slot state icons, long-press delete

**Phase 2: Framework**
- `source/ui/subtoolbars/SubToolbar.h/cpp` (base class)
- `source/ui/subtoolbars/SubToolbarContainer.h/cpp` (manages swapping, per-subtoolbar centering)

**Phase 3: Tool Subtoolbars**
- `source/ui/subtoolbars/PenSubToolbar.h/cpp` - 3 color + 3 thickness (6 buttons)
- `source/ui/subtoolbars/MarkerSubToolbar.h/cpp` - 3 color (shared) + 3 thickness (6 buttons)
- `source/ui/subtoolbars/HighlighterSubToolbar.h/cpp` - 3 color (shared) + auto-highlight toggle (4 buttons)
- `source/ui/subtoolbars/ObjectSelectSubToolbar.h/cpp` - 2 mode toggles + 3 slot buttons (5 buttons)

**Toolbar Change (separate task)**
- Modify `Toolbar.cpp` - Change Shape button → Straight Line toggle

### Implementation Order
1. Custom button widgets (foundation) - ColorPresetButton, ThicknessPresetButton
2. SubToolbar base class + SubToolbarContainer
3. PenSubToolbar (tests color + thickness)
4. MarkerSubToolbar (tests shared color presets)
5. ToggleButton + HighlighterSubToolbar
6. ModeToggleButton + LinkSlotButton widgets
7. ObjectSelectSubToolbar (completes LinkObject UI)
8. Toolbar modification (Shape → Straight Line toggle)

---

## Section 8: Custom Button Widgets (Follow-up Questions)

Based on your emphasis that "buttons carry more definitions than subtoolbars," here are detailed questions:

### Q8.1: ColorPresetButton Appearance
When the color preset button is NOT selected, how should it look?
- A) Just the filled circle, no border
- B) Filled circle with thin neutral border
- C) Filled circle with subtle shadow
- D) Other: ___

Option B


When SELECTED:
- A) Bright colored border (what color? accent color / white / black?)
- B) Glow effect
- C) Thicker border
- D) Other: ___

**Answer:**
Option A. White in dark mode, and black in bright mode. 
---

### Q8.2: ThicknessPresetButton Display
The thickness preset shows a number. Should it also show:
- A) Just the number (e.g., "2.0")
- B) Number + small line preview at that thickness
- C) A dot/circle scaled to represent the thickness
- D) Other: ___

What's the range of thicknesses to support? (min: ___, max: ___) I don't think the range of thickness is limited here. You may refer to the document viewport for the limits. 

**Answer:**
A line scaled to represent the thickness. 
---

### Q8.3: ThicknessPresetButton Edit Dialog
You mentioned "slider + spinbox". Should this be:
- A) Inline popup (appears next to button)
- B) Modal dialog
- C) Dropdown panel (like a menu)
- D) Other: ___

**Answer:**
Option B, since the color buttons also pop up a window. 
---

### Q8.4: LinkSlotButton States
The slot button shows state. Visual representation for each:
- **Empty:** ___Plus sign icon. We can reuse the add tab button. 
- **Position link:** ___ 
- **URL link:** ___  I'll find an icon for positon link and an icon  for url link. 
- **Markdown link:** ___ Reuse the markdown icon. 

Should empty slots look "inviting" (dashed border, + icon)?

**Answer:**

---

### Q8.5: Button Press Feedback
When user presses (not releases) a button, should there be:
- A) Scale down slightly (standard button feel)
- B) Darken/lighten
- C) Border change
- D) No press feedback, only release feedback
- E) Other: ___

**Answer:**
Option B, just like the buttons on the tool bar. You may reference those. 
---

### Q8.6: Color Preset Defaults
What should the default 3 colors be for:
- **Pen:** _ff0000__, _0000ff__, _000000__
- **Marker:** _ffaaaa__, _ffff00__, _aaaaff__
- **Highlighter:** _Same as marker. They share the same slots. Editing the highlighter button will also change the marker button settings__, though I don't know if the actual button objects are shared. 

**Answer:**

**Claude's Clarification - Highlighter/Marker Shared Presets:**

This is an important architectural decision! Two implementation approaches:

**Option A: Shared Button Instances**
- `MarkerSubToolbar` and `HighlighterSubToolbar` literally use the same `ColorPresetButton` objects
- Pro: Changes instantly sync
- Con: Complex ownership, both subtoolbars need access to same widgets

**Option B: Shared Data, Separate Buttons**
- Each subtoolbar has its own button instances
- Both read/write to the same QSettings keys (e.g., `marker/color1`)
- When user edits in one subtoolbar, the other subtoolbar's buttons update on next show
- Pro: Simpler ownership
- Con: Need to refresh buttons when subtoolbar becomes visible

**Recommendation:** Option B (shared data). Simpler and matches how QSettings already works. Add a `refreshFromSettings()` method that subtoolbars call when they become visible.

---

### Q8.7: Thickness Preset Defaults
What should the default 3 thicknesses be for:
- **Pen:** _2.0__, __5.0_, _10.0__
- **Marker:** _8.0__, _16.0__, _32.0__

**Answer:**
I'm going to try these first. 
---

### Q8.8: Per-Tab Preset State
You mentioned "each tab should have its own presets in memory." Clarify:
- A) Tab remembers WHICH preset (1/2/3) is selected, not the preset values
- B) Tab remembers preset values too (overrides global until tab closes)
- C) Tab remembers both selection AND modified values
- D) Other: ___

**Answer:**
Option B. Also for global value override, save the preset values on the current tab when the whole application closes. 

**Claude's Clarification - Tab State Management:**

So the flow would be:

1. **App starts:** Load presets from QSettings → all tabs start with same values
2. **User edits preset in Tab 1:** Tab 1's in-memory presets change, other tabs unaffected
3. **User switches to Tab 2:** Tab 2 still has its own (original) presets
4. **User switches back to Tab 1:** Tab 1's modified presets are restored
5. **App closes:** Save CURRENT tab's presets to QSettings (becomes new global default)

**Question:** What about which preset is SELECTED (1/2/3)? Does that also persist per-tab?
- Example: Tab 1 has pen color preset 2 selected (blue), Tab 2 has preset 1 selected (red)
- Switching tabs should restore the selection too?

**Answer:**
Switching tabs should restore the selection to match the one selected on that tab before. But this doesn't need to be saved to disk. 
---

## Section 9: Final Clarifications

### Q9.1: Highlighter Subtoolbar Contents
You said Highlighter only needs auto-highlight toggle. But Highlighter also uses colors for the highlight strokes (same as Marker). Should Highlighter subtoolbar have:
- A) Just auto-highlight toggle (color is inherited from Marker presets)
- B) Auto-highlight toggle + color presets (shared with Marker)
- C) Other: ___

**Answer:**
Option B. The color presets is for the case when the user wants to highlight in another color. In the case of Option A, he needs to switch to marker (to see the marker subtoolbar), make the color change and switch back to highlighter, which is not ideal. 
---

### Q9.2: ObjectSelect Subtoolbar Layout
The ObjectSelect subtoolbar has many items:
- Insert mode (Image/Link)
- Action mode (Select/Create)  
- Slot buttons (1/2/3) - only shown when LinkObject selected
- Object operations (Delete, Z-order, Layer affinity)

Should these be:
- A) All visible at once (long subtoolbar)
- B) Grouped with separators (as you mentioned)
- C) Some hidden until LinkObject is selected
- D) Other: ___

Visual layout suggestion:
```
[Image][Link]     ← Insert mode (radio)  // Since there are only 2 options, so they can be combined into ONE button. A click will switch mode. 
[Select][Create]  ← Action mode (radio)  // Since there are only 2 options, so they can be combined into ONE button. A click will switch mode. 
─────────────────
[Slot1][Slot2][Slot3]  ← Only when LinkObject selected   // They should be positioned vertically. The subtoolbar is STRICTLY ONE COLUMN FOR ALL. 
─────────────────
[🗑️ Delete] // The delete trigger is on long press of a slot (then a confirmation popup), not a separate button. 
[⬆️ Front][⬇️ Back] // Actually the zorder/affinity adjustments belong to the subtoolbar, but the action bar, because they are "object" controls, not "tool" controls. So none of these belong here. So there are 5 buttons in total.
[📄 Layer]
```

Does this look right?

**Answer:**


---

### Q9.3: SubToolbar Width
You checked "fixed width" but didn't specify. Given 36×36 buttons:
- Single column of buttons: ~44px wide (36 + padding)
- Two buttons side-by-side: ~80px wide
- With labels: ~100-120px

What width feels right?

**Answer:**
Single column. No labels. There may be tool tips on hover though. 
---

### Q9.4: Subtoolbar Offset from Edge
You said "a bit to the right of the left edge." Specific offset?
- A) 8px
- B) 12px
- C) 16px
- D) Other: _~24px logic__ (to create a floating effect)

And offset from top (below toolbar)?
- A) Align with viewport top edge
- B) 8-16px below toolbar
- C) Other: _Centered. The height of the center of the subtoolbar should be the same as the center of the documentviewport height. __

**Answer:**
Option C. 
---

### Q9.5: Action Bar Scope (Brief)
You mentioned Action Bar is separate from subtoolbars. For now:
- A) Defer Action Bar entirely, keyboard shortcuts only
- B) Implement minimal Action Bar alongside subtoolbars
- C) Other: ___

**Answer:**
Option A. Defer Action bar entirely, since there work independently. 
---

### Q9.6: ThicknessPresetButton Line Preview
You said "a line scaled to represent thickness." Should it be:
- A) Horizontal line (─)
- B) Vertical line (│)
- C) Diagonal line (╱)
- D) A filled circle/dot at that thickness

And should the number still be shown alongside the preview?
Diagonal line. 
**Answer:**

**Follow-up:** Should the number still be shown alongside the diagonal line preview?
- A) Yes, number + line preview
- B) No, just line preview
- C) Other: ___

**Answer:**
Option B. Just line preview. 
---

## Section 10: Final Widget Clarifications

### Q10.1: Mode Toggle Button Appearance
You said Insert mode (Image/Link) and Action mode (Select/Create) should each be ONE toggle button. 

What should the button display?
- A) Icon only (changes based on current mode)
- B) Icon + text label
- C) Two-state icon (like a switch visual)
- D) Other: _Icon only. __

Example for Insert mode button:
- When in Image mode: shows 🖼️ icon
- When in Link mode: shows 🔗 icon
- Click toggles between them

Is this correct? Yes, just like this. 

**Answer:**

---

### Q10.2: Slot Button Long-Press Delete
You said delete is triggered by long-press on a slot. Clarify:
- Can you long-press an EMPTY slot? (probably no, since there's nothing to delete)
- Long-press shows confirmation popup, then deletes slot content (not the whole LinkObject)?
- Or long-press deletes the entire LinkObject?

**Answer:**
Long press an empty slot should do nothing. 

**Follow-up:** When long-pressing a FILLED slot:
- A) Delete just the slot content (Position/URL/Markdown link) - slot becomes empty again
- B) Delete the entire LinkObject
- C) Other: ___

**Answer:**
Option A. Deleting the entire LinkObject needs to use the delete button or the action bar. 
---

### Q10.3: Highlighter Color Presets
Highlighter subtoolbar has color presets shared with Marker. So:
- Marker subtoolbar: [Color1][Color2][Color3] + [Thick1][Thick2][Thick3]
- Highlighter subtoolbar: [Color1][Color2][Color3] + [Auto-highlight toggle]

Both share the SAME 3 color values (editing in one affects the other)?

**Answer:**
Yes, exactly. 
---

### Q10.4: Subtoolbar Vertical Centering
You said subtoolbar is vertically centered on viewport. Since subtoolbar height varies by tool:
- Pen: 7 items (3 color + 3 thickness + 1 toggle) + separators ≈ 280px
- ObjectSelect: 5 items ≈ 200px
- Highlighter: 4 items ≈ 160px

Should each subtoolbar be centered based on ITS height, or all aligned to a common center point?

**Answer:**
I think the better idea is to move the toggle back to the tool bar (like changing the shape button back to the straight line toggle). The toggle will be shared between the pen and the marker for now. So pen and marker have 6 buttons, object select has 5 and highlighter has 4. 

Each toolbar should be centered based on its height. 
---

### Q10.5: Toolbar Shape Button Removal
You confirmed removing the shape button from toolbar. Current toolbar has:
```
[Pen][Marker][Eraser][Lasso][Object][Text][Undo][Redo]
```

After removal, should we also:
- A) Remove the straight line keyboard shortcut (keep it only in Pen subtoolbar)
- B) Keep keyboard shortcut, it just toggles the mode
- C) Other: ___

**Answer:**
I answered this. Change the shape button back to a straight line toggle for now. 
---

