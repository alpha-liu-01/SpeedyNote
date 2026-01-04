# Toolbar Extraction Q&A

**Document Version:** 1.0  
**Date:** January 3, 2026  
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

---

### Q1.2: Subtoolbar Files
Where should subtoolbar files go?
- A) Same folder as toolbar (`source/ui/SubToolbar.h/cpp`)
- B) Subfolder (`source/ui/toolbar/SubToolbar.h/cpp`)
- C) One file per subtoolbar type (`PenSubToolbar.h`, `EraserSubToolbar.h`, etc.)
- D) Other: ___

**Answer:**

---

## Section 2: Current Toolbar Features

### Q2.1: Tool Buttons
Which tool buttons should the new toolbar include? (Check all that apply)
- [ ] Pen
- [ ] Marker/Highlighter
- [ ] Eraser
- [ ] Lasso selection
- [ ] Object/Rectangle selection
- [ ] Text tool
- [ ] Image insert
- [ ] Shape tools
- [ ] Other: ___

**Answer:**

---

### Q2.2: Action Buttons
Which action buttons should be in the main toolbar? (vs. menu/subtoolbar)
- [ ] Undo
- [ ] Redo
- [ ] Save
- [ ] Fullscreen toggle
- [ ] Zoom in/out
- [ ] Page navigation
- [ ] Layer panel toggle
- [ ] Settings/menu button
- [ ] Other: ___

**Answer:**

---

### Q2.3: Widgets in Toolbar
Which widgets (non-buttons) should remain in main toolbar?
- [ ] Zoom slider
- [ ] Thickness slider
- [ ] Page number spinbox
- [ ] Color indicator/button
- [ ] None - all widgets go to subtoolbars
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

---

## Section 3: Connected Features

### Q3.1: Already Connected to New Architecture
Which features are already connected to DocumentViewport/new logic?
(This helps me know what the new toolbar should connect to)
- [ ] Tool switching (pen, marker, eraser, etc.)
- [ ] Color selection
- [ ] Thickness adjustment
- [ ] Undo/redo
- [ ] Zoom control
- [ ] Page navigation
- [ ] Layer operations
- [ ] Other: ___

**Answer:**

---

### Q3.2: Still Using Legacy Code
Which toolbar features still go through InkCanvas/legacy code?
(These will need stubs or temporary connections)

**Answer:**

---

## Section 4: Subtoolbar Behavior

### Q4.1: Subtoolbar Trigger
When should subtoolbars appear?
- A) On tool button click (tap pen → pen subtoolbar shows)
- B) On tool button long-press (tap = select tool, long-press = subtoolbar)
- C) On secondary button next to each tool (pen button + arrow button)
- D) Always visible when tool selected (docked below main toolbar)
- E) Other: ___

**Answer:**

---

### Q4.2: Subtoolbar Position
Where should subtoolbars appear?
- A) Floating near the tool button that triggered it
- B) Fixed position below main toolbar
- C) Fixed position at bottom of screen
- D) Floating, user can drag to reposition
- E) Other: ___

**Answer:**

---

### Q4.3: Subtoolbar Dismiss
How should subtoolbars be dismissed?
- A) Tap outside subtoolbar
- B) Tap the tool button again
- C) Auto-hide after X seconds
- D) Only when selecting different tool
- E) Multiple of above: ___

**Answer:**

---

### Q4.4: Multiple Subtoolbars
Can multiple subtoolbars be open at once?
- A) No, only one at a time
- B) Yes, but they stack/tile
- C) Yes, free floating

**Answer:**

---

## Section 5: Styling & Theming

### Q5.1: Theme Updates
How should the new toolbar handle theme changes?
- A) Receive signal from MainWindow (simple, current approach)
- B) Subscribe to a central ThemeManager (cleaner, more work)
- C) Detect system theme directly
- D) Other: ___

**Answer:**

---

### Q5.2: Styling Approach
How should toolbar styling be handled?
- A) Inline stylesheets (current approach)
- B) External QSS file
- C) Programmatic palette changes
- D) Mix (base from QSS, dynamic parts inline)

**Answer:**

---

### Q5.3: Visual Style
Should the new toolbar match the current visual style exactly, or is this a chance to refresh?
- A) Match exactly (safer migration)
- B) Minor refresh (cleaner, but same feel)
- C) Major refresh (new design)

**Answer:**

---

## Section 6: Layout & Responsiveness

### Q6.1: Toolbar Position
Where should the toolbar be positioned?
- A) Top of window (current)
- B) Bottom of window
- C) User-configurable (top or bottom)
- D) Floating/draggable

**Answer:**

---

### Q6.2: Responsive Layout
Should the toolbar adapt to window width?
- A) No, fixed single row (overflow handled by scroll or menu)
- B) Yes, wrap to 2 rows if needed (like current)
- C) Yes, but simpler (hide less-used buttons, show in overflow menu)
- D) Other: ___

**Answer:**

---

### Q6.3: Mobile/Tablet Considerations
Any special requirements for touch/tablet use?
- A) Same as desktop
- B) Larger touch targets
- C) Different layout entirely
- D) Other: ___

**Answer:**

---

## Section 7: Related Components

### Q7.1: Tab Bar
Is the tab bar also being extracted?
- A) Yes, extract to separate file too
- B) No, stays in MainWindow for now
- C) Later phase

**Answer:**

---

### Q7.2: Status Bar / Bottom Bar
Is there a status bar or bottom bar to consider?
- A) No status bar
- B) Yes, but staying in MainWindow
- C) Yes, extract alongside toolbar

**Answer:**

---

### Q7.3: Sidebar Toggles
How should sidebar toggle buttons be handled?
(Layer panel, bookmarks, outline, markdown notes)
- A) In main toolbar
- B) In a separate "view" menu/button
- C) Edge buttons (click edge of screen)
- D) Other: ___

**Answer:**

---

## Section 8: Migration Strategy

### Q8.1: Verification Approach
You suggested positioning new toolbar on top of old to compare. Specifics:
- A) Literally overlay (new on top of old, toggle visibility)
- B) Side by side (temporarily wider window)
- C) Replace old, quick A/B test via git
- D) Other: ___

**Answer:**

---

### Q8.2: Incremental vs. All-at-once
Should we migrate all toolbar features at once, or incrementally?
- A) All at once (cleaner cut)
- B) Incrementally (one tool/feature at a time)
- C) Two phases: buttons first, then subtoolbars

**Answer:**

---

### Q8.3: Fallback Plan
If new toolbar has issues, what's the rollback plan?
- A) Git revert
- B) Keep old code behind flag temporarily
- C) Other: ___

**Answer:**

---

## Summary

Once all questions are answered, I'll update the MAINWINDOW_CLEANUP_SUBPLAN.md with a revised Phase 3 plan.

---

## Answers Log

(I'll record your answers here as we go)


