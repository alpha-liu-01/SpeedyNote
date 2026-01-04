# Toolbar Extraction Q&A

**Document Version:** 1.1  
**Date:** January 4, 2026  
**Status:** ✅ COMPLETE  
**Purpose:** Clarify requirements before extracting toolbar from MainWindow

---

## Section 1: File Structure

### Q1.1: File Location
Where should the new toolbar files live?
- A) `source/ui/MainToolbar.h/cpp`
- B) `source/ui/Toolbar.h/cpp`
- C) `source/ui/toolbar/Toolbar.h/cpp` (with subtoolbars in same folder)
- D) Other: ___

**Answer:**
From what I can see, there should be 2 tool bars on top, one navigation bar, positioned at the very top, and a tool bar positioned right below the tab layout. 

--------
Navigation bar  - for toggling other layouts (like bookmarks, pdf outline, layer panel, etc) going back to launcher, adding tabs, adding bookmarks, displaying the file name, etc
--------
tab layout  - for holding the tabs
--------
tool bar  - for holding tools, like pen, marker, eraser, object insertion, etc. The color should be the same as the tabs to pretend to be part of a tab.
--------
DocumentViewport


But my question is, is it possible to separate the tab layout from the DocumentViewport with no architectural changes?
---

### Q1.2: Subtoolbar Files
Where should subtoolbar files go?
- A) Same folder as toolbar (`source/ui/SubToolbar.h/cpp`)
- B) Subfolder (`source/ui/toolbar/SubToolbar.h/cpp`)
- C) One file per subtoolbar type (`PenSubToolbar.h`, `EraserSubToolbar.h`, etc.)
- D) Other: ___

**Answer:**
Option C. But I haven't decided on where to put them yet. From what I can see, the best way is to put the subtoolbar in the left of the documentviewport (while floating on it). Making all tools share the same subtoolbar object doesn't sound like a wise choice, since each one can contain very different things. 
---

## Section 2: Current Toolbar Features

### Q2.1: Tool Buttons
Which tool buttons should the new toolbar include? (Check all that apply)
- [x ] Pen
- [x ] Marker/Highlighter
- [x ] Eraser
- [x ] Lasso selection
- [x ] Object/Rectangle selection
- [x ] Text tool
- [ ] Image insert (objectinsertion and object selection are the same tool)
- [ ] Shape tools  (doesn't exist yet, but may be implemented in the future)
- [x ] Other: ___Undo and redo buttons, since they are tab specific. 

**Answer:**

---

### Q2.2: Action Buttons
Which action buttons should be in the main toolbar? (vs. menu/subtoolbar) (Navigation bar, actually)
- [ ] Undo
- [ ] Redo
- [x ] Save
- [x ] Fullscreen toggle
- [x ] Zoom in/out
- [ ] Page navigation  (I'll put page navigation on a SEPARATE sidebar widget, so the spinbox is obsolete now, but the toggle is on the navigation bar, that's for sure)
- [x ] Layer panel toggle
- [x ] Settings/menu button (Settings should be hidden in the three dot menu on the far right of the navigation bar)
- [x ] Other: Launcher toggle. There is a launcher (LauncherWindow.cpp) that will be reconnected (or maybe redesigned) in the future, so this is a "back" button that's positioned on the very left of the navigation bar. 

**Answer:**
The button on the navigation bar are splitted into 3 parts. The ones that are aligned to the left are layout toggles, like launcher, pdfoutline, bookmarks, etc. There is a file name display in the center. The buttons aligned to the right are save, fullscreen toggle, menu etc. But here is an exception. Since the markdown notes layout is opened from the right, the toggle for markdown notes should be positioned on the right side of the navigation bar as well.
---

### Q2.3: Widgets in Toolbar
Which widgets (non-buttons) should remain in main toolbar?
- [ ] Zoom slider
- [ ] Thickness slider
- [ ] Page number spinbox
- [ ] Color indicator/button
- [x ] None - all widgets go to subtoolbars
- [ ] Other: ___

**Answer:**

---

### Q2.4: Color Buttons
Currently there are 6 color quick-access buttons. What should happen to them?
- A) Keep in main toolbar
- B) Move to pen/marker subtoolbar only
- C) Keep one "current color" indicator, rest in subtoolbar
- D) Remove entirely (subtoolbar only)

**Answer:**
Option B. The main toolbar only contains the tools. In the subtoolbars, the color and thickness presets will be replaced with customizable ones (like the only one with a hex display on the old main toolbar). There should also be 3 customizable thickness presets as well. 
---

## Section 3: Connected Features

### Q3.1: Already Connected to New Architecture
Which features are already connected to DocumentViewport/new logic?
(This helps me know what the new toolbar should connect to)
- [x ] Tool switching (pen, marker, eraser, etc.)(actually a few more, straight line toggle, lasso tool, objectinsertion)
- [x ] Color selection
- [x ] Thickness adjustment  (this no longer exists as we removed magicdial, but we can still access zoom adjustments.)
- [x ] Undo/redo (keyboard shortcuts only for now)
- [x ] Zoom control (keyboard shortcuts only for now)
- [x ] Page navigation  (wheel, gestures, keyboard shortcuts), but NO direct jump to a page number yet. 
- [x ] Layer operations (separate panel LayerPanel)
- [x ] Other: ___A lot of file or tab operations, like load single file, load package, create new tab (paged or edgeless), remove tab. But be careful with file loading. The speedynote packaging is NOT finalized, and it's currently snb loose folders. As soon as we determine all the details on persistence, we can THEN implement notebook packaging. 

Also there are some completely new features, like moving an object to have a different affinity or a different zorder. These are keyboard shortcuts only, but they will be migrated to the objectinsertion subtoolbar. 

**Answer:**

---

### Q3.2: Still Using Legacy Code
Which toolbar features still go through InkCanvas/legacy code?
(These will need stubs or temporary connections)

**Answer:**
I don't think there are any since we already unlinked inkcanvas from mainwindow. 
---

## Section 4: Subtoolbar Behavior

### Q4.1: Subtoolbar Trigger
When should subtoolbars appear?
- A) On tool button click (tap pen → pen subtoolbar shows)
- B) On tool button long-press (tap = select tool, long-press = subtoolbar)
- C) On secondary button next to each tool (pen button + arrow button)
- D) Always visible when tool selected (docked below main toolbar) (but in the left of the documentviewport, floating on it. The subtoolbars is an upright design.)
- E) Other: _D__

**Answer:**

---

### Q4.2: Subtoolbar Position
Where should subtoolbars appear?
- A) Floating near the tool button that triggered it
- B) Fixed position below main toolbar
- C) Fixed position at bottom of screen
- D) Floating, user can drag to reposition
- E) Other: _D__

**Answer:**
I already answered it in Q4.1.
---


### Q4.3: Subtoolbar Dismiss
How should subtoolbars be dismissed?
- A) Tap outside subtoolbar
- B) Tap the tool button again
- C) Auto-hide after X seconds
- D) Only when selecting different tool
- E) Multiple of above: ___

**Answer:**
You can assume that the subtoolbars will NEVER be dismissed. There is always one and only one of them showing up. 
---

### Q4.4: Multiple Subtoolbars
Can multiple subtoolbars be open at once?
- A) No, only one at a time
- B) Yes, but they stack/tile
- C) Yes, free floating

**Answer:**
A
---

## Section 5: Styling & Theming

### Q5.1: Theme Updates
How should the new toolbar handle theme changes?
- A) Receive signal from MainWindow (simple, current approach)
- B) Subscribe to a central ThemeManager (cleaner, more work)
- C) Detect system theme directly
- D) Other: _D__

**Answer:**
It's more like a mix between B and C. Just like the old version, it loads the theme color (and whether dark mode is on) from the system, and the theme manager may override the defaults. 
---

### Q5.2: Styling Approach
How should toolbar styling be handled?
- A) Inline stylesheets (current approach)
- B) External QSS file
- C) Programmatic palette changes
- D) Mix (base from QSS, dynamic parts inline)

**Answer:** 
External QSS makes more sense (?) Since there are a lot of things that need styles, like regular buttons, toggle buttons, a 3-state toggle button (touch gesture button) (Oh sorry I forgot to mention this button before. It should be positioned on the navigation bar), buttons on the navigation bar and buttons on the toolbar. Styles for the all 3 tool bars, and many subtoolbars. A lot of the styles can be shared, unlike the buttonStyle would be applied on EVERY BUTTON, which was ridiculous. 
---

### Q5.3: Visual Style
Should the new toolbar match the current visual style exactly, or is this a chance to refresh?
- A) Match exactly (safer migration)
- B) Minor refresh (cleaner, but same feel)
- C) Major refresh (new design)

**Answer:**
The parts that will follow the accent color will be the navigation bar, the tab bar and all tabs except for the current one. The parts that will be light gray (or dark gray depending on the bright/dark theme) will be the currently selected tab, the tool bar, and the DocumentViewport background outside pages (this will not appear in edgeless mode). The light/dark icon set change will be kept in the new version. The final product would look something like a KDE Plasma version of GoodNotes 5, since this is an app meant for low power tablet PCs but built with desktop app tech stack.
---

## Section 6: Layout & Responsiveness

### Q6.1: Toolbar Position
Where should the toolbar be positioned?
- A) Top of window (current)
- B) Bottom of window
- C) User-configurable (top or bottom)
- D) Floating/draggable

**Answer:**
We already determined this. Option A. 
---

### Q6.2: Responsive Layout
Should the toolbar adapt to window width?
- A) No, fixed single row (overflow handled by scroll or menu)
- B) Yes, wrap to 2 rows if needed (like current)
- C) Yes, but simpler (hide less-used buttons, show in overflow menu)
- D) Other: ___

**Answer:**
A. With the new design, we can determine that the everything on the navigation bar and the tool bar will fit within 768 pixels, which is the minimum amount of pixels that a tablet PC can have on its short side. 
---

### Q6.3: Mobile/Tablet Considerations
Any special requirements for touch/tablet use?
- A) Same as desktop
- B) Larger touch targets
- C) Different layout entirely
- D) Other: ___

**Answer:**
The same. The layout is already designed for mobile use (though I don't know if calling tablet PCs "mobile" is correct, but this app will land on android when I sort out another pdf provider other than poppler soon.)
---

## Section 7: Related Components

### Q7.1: Tab Bar
Is the tab bar also being extracted?
- A) Yes, extract to separate file too
- B) No, stays in MainWindow for now
- C) Later phase

**Answer:**
This is a hard choice. Since you mentioned that the tab bar will be changed to a `QTabBar` + `QStackedWidget` architecture, I'm not sure the extra change, and all the actions going on upon switching to another tab, I'm not sure if it makes sense architecturally to extract the tab bar. But from my preference, I would like the tab bar to be extracted as well. 
---

### Q7.2: Status Bar / Bottom Bar
Is there a status bar or bottom bar to consider?
- A) No status bar
- B) Yes, but staying in MainWindow
- C) Yes, extract alongside toolbar

**Answer:**
Status bar / bottom bar (especially permanently enabled ones) are prohibited, because this will result in false inputs, as the user's arm and/or wrist will cover it. 
---

### Q7.3: Sidebar Toggles
How should sidebar toggle buttons be handled?
(Layer panel, bookmarks, outline, markdown notes)
- A) In main toolbar
- B) In a separate "view" menu/button
- C) Edge buttons (click edge of screen)
- D) Other: ___

**Answer:**
I already answered this question. Navigation bar is designed for this. 
---

## Section 8: Migration Strategy

### Q8.1: Verification Approach
You suggested positioning new toolbar on top of old to compare. Specifics:
- A) Literally overlay (new on top of old, toggle visibility)
- B) Side by side (temporarily wider window)
- C) Replace old, quick A/B test via git
- D) Other: ___

**Answer:**
With so many widgets added, the best way would still be removing the old entirely and replace it with the new layouts. I'll make a backup of the current state of mainwindow for reference though. 
---

### Q8.2: Incremental vs. All-at-once
Should we migrate all toolbar features at once, or incrementally?
- A) All at once (cleaner cut)
- B) Incrementally (one tool/feature at a time)
- C) Two phases: buttons first, then subtoolbars

**Answer:**
We definitely delay the subtoolbars for later. It should be close to Option C, but likely with more than 2 phases. 
---

### Q8.3: Fallback Plan
If new toolbar has issues, what's the rollback plan?
- A) Git revert
- B) Keep old code behind flag temporarily
- C) Other: ___

**Answer:**
We won't ship the products before we finish the redesign. I can git revert or recover the file backup other ways. But since these really only connect to core logic (which is more or less complete), it's really unlikely that a visual redesign will "NOT WORK". 
---

## Summary

### Final Layout
```
┌─────────────────────────────────────────────────────────────────┐
│ NavigationBar [←][📑][🔖][📄] filename.spn [📝][💾][⛶][👆][⋮] │
├─────────────────────────────────────────────────────────────────┤
│ TabBar        [Tab1] [Tab2] [Active Tab (gray)]                 │
├─────────────────────────────────────────────────────────────────┤
│ Toolbar       [🖊️][🖍️][⌫][◯][📝][T][↩️][↪️]                    │
├──────┬──────────────────────────────────────────────────────────┤
│ Sub  │                                                          │
│ tool │              DocumentViewport                            │
│ bar  │                                                          │
│      │                                                          │
└──────┴──────────────────────────────────────────────────────────┘
```

### Key Decisions
1. **Three bars:** NavigationBar (app actions) → TabBar → Toolbar (document tools)
2. **Subtoolbar:** Always visible on left, vertical, one per tool, never dismissed
3. **No responsive layout:** Everything fits 768px minimum
4. **External QSS:** Shared styles, not inline per-widget
5. **No zoom buttons:** Vector canvas + adaptive PDF DPI = gesture-only zoom
6. **No bottom bar:** Tablet ergonomics (arm/wrist coverage)
7. **Multi-phase migration:** NavBar → Toolbar → TabBar → Subtoolbars

### Files to Create
- `source/ui/NavigationBar.h/cpp`
- `source/ui/Toolbar.h/cpp`
- `source/ui/subtoolbars/PenSubToolbar.h/cpp` (deferred)
- `source/ui/subtoolbars/EraserSubToolbar.h/cpp` (deferred)
- `source/ui/subtoolbars/*.h/cpp` (deferred)
- `resources/styles/toolbar.qss` (or similar)

### Next Step
Update MAINWINDOW_CLEANUP_SUBPLAN.md with revised Phase 3 plan based on these decisions.

---

## Answers Log

### Q1.1: File Location
**Answer:** Three-tier layout:
1. **Navigation Bar** (top) - layout toggles (bookmarks, outline, layers), back to launcher, add tabs, bookmarks, filename display
2. **Tab Layout** - document tabs
3. **Toolbar** - tools (pen, marker, eraser, etc.), styled to match tab color as visual extension

**Technical note:** Separating tabs from DocumentViewport is possible using `QTabBar` + `QStackedWidget` instead of `QTabWidget`. This decouples tab UI from viewport management.

**File structure:** 
- `source/ui/NavigationBar.h/cpp`
- `source/ui/Toolbar.h/cpp`
- (Tab bar will be part of MainWindow initially, or extracted later)

---

### Q1.2: Subtoolbar Files
**Answer:** Option C - one file per subtoolbar type. Position: floating on left side of DocumentViewport.

**File structure:**
- `source/ui/subtoolbars/PenSubToolbar.h/cpp`
- `source/ui/subtoolbars/EraserSubToolbar.h/cpp`
- `source/ui/subtoolbars/ShapeSubToolbar.h/cpp`
- etc.

---

### Q2.1: Tool Buttons
**Answer:** Toolbar contains:
- Pen, Marker/Highlighter, Eraser, Lasso selection, Object selection, Text tool
- Undo/Redo (document-specific, so in toolbar not navigation bar)
- NOT image insert (part of object selection)
- NOT shape tools (future feature)

---

### Q2.2: Action Buttons (Navigation Bar)
**Answer:** Navigation bar has 3-part layout:
- **Left (layout toggles):** Launcher (back), PDF outline, Bookmarks, Layer panel
- **Center:** Filename display
- **Right (actions):** Markdown notes toggle*, Save, Fullscreen, Menu (⋮)

*Markdown notes toggle on right because its panel opens from the right side.

Page navigation will be a separate sidebar widget; toggle for it is on navigation bar.

**Zoom buttons:** NOT needed. The new vector-based canvas + adaptive PDF DPI system renders sharply at any zoom level. Users zoom via:
- Pinch gestures (touch)
- Ctrl+scroll wheel (desktop)
- Keyboard shortcuts if desired

No explicit zoom controls in UI - this is a deliberate architectural improvement over the old picture-based canvas which had aliasing at non-integer zoom levels.

---

### Q2.3: Widgets in Toolbar
**Answer:** None - all widgets (zoom slider, thickness slider, page spinbox, color indicator) go to subtoolbars. Main toolbar stays clean with just tool buttons.

---

### Q2.4: Color Buttons
**Answer:** Option B - move to pen/marker subtoolbar. Replace fixed 6 colors with customizable presets (3 color slots + 3 thickness slots), each user-configurable like the current hex-display button.

---

### Q3.1: Already Connected to New Architecture
**Answer:** Everything is already connected to DocumentViewport/new logic:
- **Tools:** Pen, marker, eraser, straight line toggle, lasso, object insertion
- **Properties:** Color selection, thickness adjustment
- **Operations:** Undo/redo, zoom (keyboard shortcuts)
- **Navigation:** Page wheel/gestures/shortcuts (no direct page jump yet)
- **Panels:** Layer operations (LayerPanel)
- **Files:** Load single/package, create/remove tabs

**New features (keyboard only, will migrate to subtoolbars):**
- Object affinity changes → object insertion subtoolbar
- Object z-order changes → object insertion subtoolbar

**Note:** Packaging format (snb) not finalized; currently loose folders. Toolbar should not assume specific persistence format.

---

### Q3.2: Still Using Legacy Code
**Answer:** None. InkCanvas is fully unlinked from MainWindow. This means:
- Clean toolbar extraction with no legacy bridges
- All signals connect directly to new architecture
- No compatibility stubs needed

---

### Q4.1 & Q4.2: Subtoolbar Trigger & Position
**Answer:** Option D - Always visible when tool selected. Position: floating on left side of DocumentViewport, vertical/upright design.

When user switches tools, the subtoolbar swaps to match the current tool. No trigger action needed - it's always there.

---

### Q4.3: Subtoolbar Dismiss
**Answer:** Never dismissed. There is always exactly one subtoolbar visible. Switching tools = switching subtoolbar.

---

### Q4.4: Multiple Subtoolbars
**Answer:** A - No, only one at a time. The current tool determines which subtoolbar is shown.

**Note on Undo/Redo:** These are instant action buttons, not selectable tools. Clicking them does not change the current tool or subtoolbar - they execute immediately and the previous tool remains active.

---

### Q5.1: Theme Updates
**Answer:** Mix of B+C. System provides defaults (dark mode, accent color), ThemeManager can override. Respects OS preferences while allowing app customization.

---

### Q5.2: Styling Approach
**Answer:** External QSS file. Many elements need styling:
- Regular buttons, toggle buttons, 3-state toggle (touch gesture button on nav bar)
- Navigation bar, toolbar, subtoolbar buttons
- All 3 bars + subtoolbars

Shared CSS classes (`.nav-button`, `.toolbar-button`, `.toggle-active`) instead of inline stylesheets per widget.

**New item for Navigation Bar:** 3-state touch gesture toggle button.

---

### Q5.3: Visual Style
**Answer:** Clear visual hierarchy:
- **Accent color:** Navigation bar, tab bar, non-selected tabs
- **Gray (light/dark based on theme):** Selected tab, toolbar, DocumentViewport background outside pages (paged mode only)
- Light/dark icon sets based on theme

Selected tab is gray to visually blend with toolbar below = "this is the content area."

**Target aesthetic:** "KDE Plasma version of GoodNotes 5" - native Qt feel, modern/clean, optimized for low-power tablet PCs using desktop tech stack.

---

### Q6.1: Toolbar Position
**Answer:** A - Top of window. Already determined in layout discussion (Nav bar → Tab bar → Toolbar → DocumentViewport).

---

### Q6.2: Responsive Layout
**Answer:** A - No responsive wrapping. Fixed single row for both navigation bar and toolbar.

**Rationale:** Everything fits within 768px, which is the minimum width for tablet short edge. No 2-row fallback needed = simpler code.

---

### Q6.3: Mobile/Tablet Considerations
**Answer:** Same as desktop. Layout is already touch-optimized:
- 36×36 logical button size (72×72 at 192 DPI)
- No hover-dependent interactions
- Subtoolbar always visible
- Zoom/pan via gestures

**Future:** Android port planned when alternative PDF provider (non-Poppler) is ready.

---

### Q7.1: Tab Bar
**Answer:** Prefer extraction, but uncertain due to complexity of tab switching logic.

**Recommendation:** Phased approach:
- **Phase A:** Extract NavigationBar + Toolbar + Subtoolbars first
- **Phase B:** Extract TabBar after Phase A is stable

TabBar (using `QTabBar` not `QTabWidget`) is just the visual tabs. Tab switching logic and viewport management can remain in MainWindow, connected via `QTabBar::currentChanged` signal.

---

### Q7.2: Status Bar / Bottom Bar
**Answer:** A - No status bar. Bottom bars are prohibited on tablet apps because user's arm/wrist covers the bottom of screen, causing false inputs.

---

### Q7.3: Sidebar Toggles
**Answer:** Navigation bar handles all sidebar toggles (layer panel, bookmarks, outline, markdown notes). Already covered in Q2.2.

---

### Q8.1: Verification Approach
**Answer:** D - Clean replace. Remove old toolbar entirely, replace with new layouts. Backup MainWindow.cpp for reference.

No overlay/side-by-side comparison - too many structural changes make that approach confusing.

---

### Q8.2: Incremental vs. All-at-once
**Answer:** Multi-phase (more than 2):
- **Phase A:** NavigationBar (layout toggles, filename, save, fullscreen, menu)
- **Phase B:** Toolbar (tool buttons, undo/redo)
- **Phase C:** TabBar (optional, QTabBar extraction)
- **Phase D+:** Subtoolbars (deferred, each independent)

Subtoolbars explicitly delayed for later phases.

---

### Q8.3: Fallback Plan
**Answer:** A - Git revert (or file backup recovery).

Low risk since this is UI wiring to already-complete core logic. Not shipping until redesign complete. Functional failures unlikely - only cosmetic bugs possible.

---


