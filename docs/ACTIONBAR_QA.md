# Action Bar Q&A Document

**Document Version:** 1.1  
**Date:** January 10, 2026  
**Status:** ✅ COMPLETE → See `ACTIONBAR_SUBPLAN.md` for implementation plan

---

## Overview

The Action Bar is a context-sensitive floating UI that provides quick access to common editing operations for tablet users who don't have keyboard shortcuts available. It appears when relevant (e.g., when a selection exists) and disappears after an action is performed.

### Background: Keyboard Shortcuts Without UI Equivalents

Currently, the following operations are only accessible via keyboard and have no UI buttons:

| Shortcut | Operation | Context |
|----------|-----------|---------|
| Ctrl+C | Copy | Lasso selection, Object selection, Text selection |
| Ctrl+X | Cut | Lasso selection |
| Ctrl+V | Paste | Lasso tool, ObjectSelect tool |
| Delete/Backspace | Delete | Lasso selection, Object selection |
| Escape | Cancel | Lasso selection, Text selection |

The Action Bar will expose these operations for tablet/touch users.

---

## Section A: Core Functionality

### Q1: What actions should the Action Bar contain?

**Primary Actions (always relevant):**
- [x] Copy
- [x] Cut  
- [x] Paste
- [x] Delete

**Secondary Actions (context-dependent):**
- [ ] Select All (if applicable)
- [ ] Deselect / Cancel

**Advanced Actions (for objects only):**
- [x] Bring Forward
- [x] Send Backward
- [x] Bring to Front
- [x] Send to Back

**Questions:**
1. Should all primary actions always be visible, or only those that are currently possible?
   - For example: "Paste" when clipboard is empty
   
2. Should we include advanced layer ordering actions, or keep those keyboard-only?

3. Should we include tool-specific actions beyond clipboard operations?

**YOUR ANSWER:**
A1: Only those are current available. 
A2: Include the layer ordering actions in ObjectInsert mode. 
A3: Yes, but for now there aren't many. 

---

### Q2: When should the Action Bar appear?

**Option A: Selection-triggered**
- Appears automatically when a selection exists (lasso, object, or text)
- Disappears when selection is cleared

**Option B: User-triggered**
- User taps a "..." button or long-presses to show Action Bar
- Manual dismiss

**Option C: Hybrid**
- Appears automatically on selection
- User can dismiss, and it won't reappear until next selection

**Questions:**
1. Which appearance trigger model do you prefer?
2. Should there be a delay before appearing (to avoid flicker during brief selections)?

**YOUR ANSWER:**

Option C. It should be hybrid. For example, when a lasso action finishes and the region enter the selected state, the action bar pops up. When the user clicks on an InsertedObject, the action bar pops up. 

In the case that the user wants to paste a picture onto the page, I don't know if it's possible (and viable performance wise) to make it detect that there is a picture in the clipboard, and make the action bar appear. If not, we may need a manual trigger for the action bar. 
---

### Q3: When should the Action Bar disappear?

**Auto-dismiss scenarios:**
- [x ] After any action is performed (Copy, Delete, etc.)
- [x ] When selection is cleared
- [x ] When tool changes
- [x ] When document/page changes
- [ ] After a timeout (e.g., 5 seconds of inactivity)
- [ ] When user taps outside the Action Bar

**Questions:**
1. Should it disappear immediately after an action, or stay visible until selection changes?
2. For "Copy" action, the selection remains - should the bar stay?

**YOUR ANSWER:**
A1: Stay visible until selection changes. When the InsertedObject or the pdf text is no longer selected, the action bar can then disappear. 

---

## Section B: Visual Design

### Q4: Where should the Action Bar be positioned?

**Option A: Near Selection**
- Floats above/below the selection bounding box
- Moves with the selection if dragged
- Pro: Contextually close to what user is working on
- Con: May obscure content, complex positioning logic

**Option B: Fixed Position**
- Fixed location (e.g., top-center, bottom-center)
- Pro: Predictable, simpler implementation
- Con: May require user to look away from selection

**Option C: Near Subtoolbar**
- Appears below or integrated with the subtoolbar on the left side
- Pro: Consistent with existing UI pattern
- Con: May be far from selection

**YOUR ANSWER:**
Option B. It would be on the right side of the DocumentViewport, floating on it (though not belonged to the DocumentViewport) JUST LIKE THE SUBTOOLBARS, and SYMMETRICAL TO THE SUBTOOLBARS.

---

### Q5: What should the Action Bar look like?

**Shape options:**
- [ ] Horizontal bar (buttons in a row)
- [x ] Vertical bar (buttons in a column)
- [ ] Pill-shaped floating bar
- [ ] Grid (2x2 or similar)

**Size options:**
- Same button size as subtoolbar (36×36) 
- Larger for easier touch targeting (44×44 or 48×48)
- Compact (24×24) to minimize obstruction

**Style:**
- Should it match the subtoolbar styling (rounded, shadow, theme-aware)?
- Should buttons have text labels, icons only, or both?

**YOUR ANSWER:**
Same button size and same style as the subtoolbars. No text labels. But do include tooltips just like the subtoolbars. 

---

### Q6: Should the Action Bar have a visual relationship with the subtoolbar?

From `plans.txt`: "Should integrate visually with subtoolbar"

**Options:**
- [x ] Completely independent floating bar
- [x ] Same styling but separate position
- [ ] Appears as an extension of the subtoolbar (below it)
- [ ] Shares the subtoolbar container (appears as additional buttons when selection exists)

**YOUR ANSWER:**
They should be completely independent, though the styling is the same.

---

## Section C: Context-Specific Behavior

### Q7: How should the Action Bar behave differently per tool?

**Lasso Tool:**
- Copy, Cut, Paste, Delete
- Selection transform is handled elsewhere (handles on selection)

**ObjectSelect Tool:**
- Copy, Paste, Delete
- Should layer ordering (Forward/Backward) be included? Yes. 

**Highlighter Tool (Text Selection):**
- Copy only (text is copied to clipboard) Yes.
- No Cut/Delete (can't delete PDF text)

**Questions:**
1. Should the Action Bar content change based on current tool? Yes. There are only 3 tools supported, and the lasso tool doesn't even have a subtoolbar. So they really are independent. There may be future tools, like shape, and future inserted objects, like a text box. The action bars need to be flexible for supporting future features. 
2. Or should unavailable actions be hidden vs. disabled?

Hidden. 
There will be a fixed number of buttons on each actionbar (like lassoactionbar, objectinsertactionbar, defaultactionbar, clipboardactionbar, etc), but an actionbar should not show another actionbar's buttons. 
**YOUR ANSWER:**


---

### Q8: Paste behavior when nothing is selected

**Scenario:** User has previously copied something. No current selection exists.

**Options:**
- [x ] Show Action Bar with only "Paste" button (user can paste anywhere)
- [ ] Don't show Action Bar - require user to create selection first
- [ ] Show a minimal "Paste" indicator near cursor/last touch position

**YOUR ANSWER:**
This is what the clipboardactionbar does. There is only 1 button on it, which is paste. 

---

## Section D: Implementation Details

### Q9: How should the Action Bar interact with existing undo/redo?

**Current situation:**
- Undo/Redo buttons exist in the main Toolbar
- Ctrl+Z / Ctrl+Y work in DocumentViewport

**Options:**
- [x ] Don't include Undo/Redo in Action Bar (they're in Toolbar)
- [ ] Include them as secondary actions in Action Bar
- [ ] Replace/supplement Toolbar buttons when Action Bar is visible

**YOUR ANSWER:**


---

### Q10: Touch vs. Mouse behavior

**Questions:**
1. Should the Action Bar appear for mouse users too, or keyboard users are expected to use shortcuts?
2. Should there be a setting to enable/disable the Action Bar?

**YOUR ANSWER:**
1. Yes. 
2. No. 

---

### Q11: Animation and transitions

**Options:**
- [ ] No animation (instant appear/disappear)
- [ ] Fade in/out
- [ ] Slide in from edge
- [ ] Scale up from selection point

**Duration:** 150ms / 200ms / 250ms?

**YOUR ANSWER:**
I don't know how a simple animation will hurt the performance. SpeedyNote targets very low end hardware, like the Intel Atom Z3735E. If the animation is efficient enough, slide in from edge looks nice. Otherwise, no animation. 

---

## Section E: Edge Cases

### Q12: Multiple selections

**Scenario:** User has multiple objects selected in ObjectSelect mode.

**Questions:**
1. Should all actions apply to all selected objects?
2. Any special handling needed?

**YOUR ANSWER:**
I don't think selecting multiple objects is possible architecturally. We may handle this case in the future... 

---

### Q13: Clipboard compatibility

**Current clipboard situation:**
- Lasso: Internal clipboard (strokes stored in memory)
- ObjectSelect: Internal clipboard (objects stored in memory)
- Text: System clipboard (QString)

**Questions:**
1. Should Copy from Action Bar also copy to system clipboard (for cross-app paste)?
2. Should Paste check both internal and system clipboard?

**YOUR ANSWER:**
1. The only thing that can be copied out of SpeedyNote is PDF text (which is just plain text). Everything else don't support copy in SpeedyNote and paste outside speedynote. 
2. The only 2 things that can be pasted into SpeedyNote are: plain text (for markdown integration later), and pictures. 
Internal clipboard has higher priority than system clipboard. 

---

### Q14: Empty clipboard state

**Scenario:** User hasn't copied anything yet.

**Options:**
- [ ] Show Paste button but disabled (grayed out)
- [x ] Hide Paste button entirely
- [x ] Don't show Action Bar at all if only action would be Paste

**YOUR ANSWER:**


---

## Section F: Additional Questions

### Q15: Accessibility

**Questions:**
1. Should Action Bar buttons have tooltips?
2. Should there be keyboard navigation within the Action Bar (Tab/Enter)?
3. Any special considerations for accessibility?

**YOUR ANSWER:**

1. Yes. 
2. No. 
3. No for now. 
---

### Q16: Integration with existing features

**Questions:**
1. How should Action Bar interact with the debug overlay?
2. Should Action Bar state be saved/restored with tabs?
3. Any interaction with touch gesture modes?

**YOUR ANSWER:**
1. I don't think the action bar ineeds to interact with debug overlay.
2. Yes. 
3. No. The 3-state button for touch gesture modes is on the tool bar. 

---

### Q17: Future extensibility

**Questions:**
1. Should the Action Bar be extensible for future actions?
2. Plugin/extension support considerations?
3. User customization (add/remove buttons)?

**YOUR ANSWER:**
1. Yes. 
2. There will be future features, but I'm not sure if more InsertedObject types can be supported BY USING A PLUGIN. Krita is also a Qt app, which has a very wide range of extension support. I don't know if this concept can be brought here. The current architecture is much more flexible than the old inkcanvas+mainwindow in theory. 
3. No for now. 

---

## Summary of Required Decisions

Before implementation, we need your decisions on:

1. **Actions:** What buttons should be included?
2. **Trigger:** When does it appear?
3. **Dismiss:** When does it disappear?
4. **Position:** Where on screen?
5. **Style:** Visual design specifications
6. **Context:** Tool-specific behavior
7. **Edge cases:** Clipboard, multiple selection, etc.

Please fill in your answers above, and I'll create a detailed implementation plan based on your requirements.
I've already answered all of them before. 
---

## Notes for Implementation

### Existing keyboard shortcuts to expose:

```cpp
// Lasso Tool (DocumentViewport.cpp ~1933-1965)
Ctrl+C → copySelection()
Ctrl+X → cutSelection()
Ctrl+V → pasteSelection()
Delete/Backspace → deleteSelection()
Escape → cancelSelectionTransform()

// ObjectSelect Tool (DocumentViewport.cpp ~1977-2070)
Ctrl+C → copySelectedObjects()
Ctrl+V → pasteForObjectSelect()
Delete/Backspace → deleteSelectedObjects()
Alt+] → increaseSelectedAffinity()
Alt+[ → decreaseSelectedAffinity()
Alt+\ → sendSelectedToBackground()
Ctrl+] → bringSelectedForward()
Ctrl+Shift+] → bringSelectedToFront()
Ctrl+[ → sendSelectedBackward()
Ctrl+Shift+[ → sendSelectedToBack()

// Highlighter Tool (DocumentViewport.cpp ~2129-2155)
Ctrl+C → copySelectedTextToClipboard()
Escape → clear text selection
```

### Relevant signals in DocumentViewport:

```cpp
void lassoSelectionChanged(bool hasSelection);  // May need to add
void objectSelectionChanged();  // Already exists
void textSelectionChanged(bool hasSelection);  // May need to add
```

---

## Section G: Developer Suggestions & Clarifications

### S1: Clipboard Detection Performance (Re: Q2)

**Your concern:** Detecting clipboard content for paste-only Action Bar may be expensive.

**Suggestion:** 

Qt's `QClipboard` provides signals that fire when clipboard content changes:
```cpp
QClipboard* clipboard = QApplication::clipboard();
connect(clipboard, &QClipboard::dataChanged, this, &ActionBarContainer::onClipboardChanged);
```

This is **event-driven**, not polling, so it's very efficient. The approach:
1. On `dataChanged`, check if clipboard has image or text
2. Cache the result: `m_clipboardHasImage`, `m_clipboardHasText`
3. When deciding whether to show paste-only Action Bar, just check the cached flags

**Performance impact:** Minimal - one signal connection, clipboard is only checked when it changes.

**Recommendation:** Use clipboard signal detection. No manual trigger needed.

**YOUR RESPONSE:**
Good, I can accept that. 

---

### S2: Animation Performance (Re: Q11)

**Your concern:** Performance on Intel Atom Z3735E.

**Analysis:**
- QPropertyAnimation with opacity or position is GPU-accelerated in Qt
- A simple slide animation (changing x position over 150ms) is essentially free
- The subtoolbar already uses QGraphicsDropShadowEffect which is more expensive than animation

**Recommendation:** 
1. Use simple CSS/position animation (slide from right edge)
2. Duration: 150ms (fast enough to feel snappy)
3. If issues arise on Atom, can add a `QSettings` flag to disable

**Implementation:**
```cpp
// Simple slide-in: start at x + 50, animate to final x
QPropertyAnimation* anim = new QPropertyAnimation(this, "pos");
anim->setDuration(150);
anim->setStartValue(QPoint(finalX + 50, finalY));
anim->setEndValue(QPoint(finalX, finalY));
anim->setEasingCurve(QEasingCurve::OutCubic);
anim->start(QAbstractAnimation::DeleteWhenStopped);
```

**YOUR RESPONSE:**
Good, I can accept this. 

---

### S3: Multiple Object Selection (Re: Q12)

**Clarification:** Looking at the code, `m_selectedObjects` is a `QList<InsertedObject*>`, which CAN hold multiple objects. The current UI doesn't expose multi-select (e.g., Shift+click), but the architecture supports it.

**For Action Bar:** 
- Actions should apply to ALL selected objects (Copy all, Delete all, etc.)
- Layer ordering with multiple objects is trickier (order relative to each other?)

**Recommendation:** For now, assume single selection. The Action Bar code should use `selectedObjects()` (returns list) rather than assuming single item, for future compatibility.

**YOUR RESPONSE:**
Sure, this makes sense. But please plan the multiselect for the future. We may need to change this in a future task. 

---

### S4: Action Bar Types Architecture (Re: Q7)

**Your vision:** Separate action bar types (LassoActionBar, ObjectSelectActionBar, ClipboardActionBar, etc.)

**Two approaches:**

**Option A: Single ActionBar class with dynamic content**
- One `ActionBar` class
- Changes buttons based on context
- Simpler to manage positioning
- Buttons shown/hidden dynamically

**Option B: Separate ActionBar subclasses**
- `LassoActionBar`, `ObjectSelectActionBar`, `TextSelectionActionBar`, `ClipboardActionBar`
- Each has fixed buttons
- `ActionBarContainer` swaps between them (like SubToolbarContainer)

**Recommendation:** Option B (separate classes) aligns with how subtoolbars work and is more extensible. However, they could share a common base class for styling.

**Architecture:**
```
ActionBar (base class - same as SubToolbar styling)
├── LassoActionBar        [Copy, Cut, Paste, Delete]
├── ObjectSelectActionBar [Copy, Paste, Delete, Forward, Backward, ToFront, ToBack]
├── TextSelectionActionBar [Copy only]
└── ClipboardActionBar    [Paste only - when clipboard has content but no selection]
```

**YOUR RESPONSE:**
Good, Option B it is. 

---

### S5: Positioning Details (Re: Q4)

**Your answer:** Right side, symmetrical to subtoolbar.

**Current subtoolbar position:**
- 24px from left edge of viewport
- Vertically centered

**Proposed Action Bar position:**
- 24px from **right** edge of viewport
- Vertically centered

**Question:** Should the Action Bar also be 24px from right edge, or a different offset?

**Additional consideration:** The vertical scrollbar is on the right side. Should the Action Bar be:
- [ ] Left of the scrollbar (may overlap with scrollbar)
- [ ] Right of the scrollbar (outside viewport area)
- [ ] Scrollbar hidden when Action Bar is visible

**YOUR RESPONSE:**
The vertical scrollbar is actually ON THE LEFT SIDE ON PURPOSE. So no worries on this. 

---

### S6: Per-Tab State (Re: Q16.2)

**Your answer:** Yes, Action Bar state should be saved/restored with tabs.

**Clarification needed:** What state exactly?

Candidates:
- [x ] Visibility (was Action Bar shown/hidden?)
- [ ] Position (if user can drag it)
- [ ] Nothing - Action Bar is purely reactive to selection state

**My understanding:** The Action Bar appears/disappears based on selection. When switching tabs:
1. The new tab's selection state determines Action Bar visibility. Yes. 
2. No additional state needs to be saved. See the case in my response. 

**Is this correct, or did you have something else in mind?**

**YOUR RESPONSE:**
For example, when the user has a picture in the clipboard, but he is using the pen tool on one tab, and ObjectSelection on another, the action bar should NEVER show up (unless he clicks on a markdown note, which will be reconnected later), and he switches to another tab in ObjectSelect tool, the clipboard action bar should show up. 

---

### S7: Manual Trigger for ClipboardActionBar

**Scenario:** User wants to paste an image from system clipboard, but there's no selection (so no automatic Action Bar).

**Options for triggering ClipboardActionBar:**
1. **Long-press on empty canvas** - Shows ClipboardActionBar at touch position
2. **Right-click context menu** - Traditional but less touch-friendly
3. **Dedicated button in Toolbar** - Always accessible
4. **Automatic** - If clipboard has image, show ClipboardActionBar when user taps on empty canvas

**Question:** Which trigger mechanism do you prefer for the paste-only scenario?

**YOUR RESPONSE:**
Option 4. Automatic. 

---

### S8: Default Action Bar

**Your mention:** "defaultactionbar" in Q7 response.

**Question:** What is the "default" action bar? When would it appear?

Possible interpretations:
1. A fallback when no specific action bar applies
2. A minimal bar with just Paste (same as ClipboardActionBar)
3. Something else?

**YOUR RESPONSE:**
Oh sorry, I was referring to the clipboardactionbar (since I figured out it wasn't really default). The action bar with only a paste button goes by clipboardactionbar from now on... 

---

## Section H: Summary of Confirmed Design

**Status:** ✅ ALL DECISIONS CONFIRMED

Based on your answers, here's the confirmed design:

### Action Bar Types

| Type | Trigger | Tool Context | Buttons |
|------|---------|--------------|---------|
| LassoActionBar | Lasso selection completed | Lasso tool | Copy, Cut, Paste, Delete |
| ObjectSelectActionBar | Object(s) selected | ObjectSelect tool | Copy, Paste, Delete, Forward, Backward, ToFront, ToBack |
| TextSelectionActionBar | PDF text selected | Highlighter tool | Copy |
| ClipboardActionBar | Clipboard has image, no selection, auto-detect | ObjectSelect tool only | Paste |

### Visual Specifications

| Property | Value |
|----------|-------|
| Position | Right side of viewport, 24px from edge, vertically centered |
| Button size | 36×36 (same as subtoolbar) |
| Style | Same as subtoolbar (rounded, shadow, theme-aware) |
| Labels | Icons only, with tooltips |
| Animation | Slide in from right (150ms) with OutCubic easing |

### Behavior

| Aspect | Behavior |
|--------|----------|
| Appear | When selection exists OR (clipboard has image AND ObjectSelect tool active) |
| Disappear | When selection cleared, tool changes, or page/document changes |
| After action | Stay visible until selection changes |
| Unavailable actions | Hidden (not disabled) |
| Per-tab | Re-evaluate visibility based on new tab's tool and selection state |

### Architecture

```
ActionBar (base class - styling, animation, positioning)
├── LassoActionBar        [Copy, Cut, Paste, Delete]
├── ObjectSelectActionBar [Copy, Paste, Delete, Forward, Backward, ToFront, ToBack]
├── TextSelectionActionBar [Copy]
└── ClipboardActionBar    [Paste]

ActionBarContainer (manages swapping, positioning on right side)
```

### Key Implementation Notes

1. **Clipboard Detection:** Use `QClipboard::dataChanged` signal (event-driven, efficient)
2. **ClipboardActionBar:** Only shows in ObjectSelect tool when clipboard has image and no object is selected
3. **Scrollbar:** No conflict - vertical scrollbar is on the LEFT side
4. **Multi-select:** Use `selectedObjects()` list API for future compatibility
5. **Tab switching:** Re-evaluate action bar visibility based on new tab's tool + selection state

### Resolved Decisions

- [x] Scrollbar interaction (S5) → No conflict, scrollbar is on left
- [x] Per-tab state details (S6) → Reactive to tool + selection, no saved state needed
- [x] ClipboardActionBar trigger (S7) → Automatic when clipboard has image + ObjectSelect tool
- [x] Default action bar (S8) → No "default", was referring to ClipboardActionBar

---

## Section I: Additional Clarification

### ClipboardActionBar Tool Restriction

**Clarification needed:** You mentioned ClipboardActionBar should only show in ObjectSelect tool. 

The Lasso tool also has Paste (Ctrl+V → `pasteSelection()`), but this pastes **strokes** from the internal clipboard, not images from system clipboard.

**Current understanding:**
- **Lasso Paste:** Pastes strokes from internal `m_clipboard` (SpeedyNote-internal)
- **ObjectSelect Paste:** Pastes objects, including images from system clipboard

**Confirmed:** ClipboardActionBar (for system clipboard images) only appears in ObjectSelect tool.

**Question:** Should LassoActionBar's Paste button also check the internal stroke clipboard, and only show if there are strokes to paste?

**YOUR RESPONSE:**
Yes. 

---

## Q&A Complete

All questions have been answered. See `docs/ACTIONBAR_SUBPLAN.md` for the implementation plan.

