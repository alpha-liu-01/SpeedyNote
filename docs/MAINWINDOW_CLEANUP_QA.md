# MainWindow Cleanup Q&A

**Purpose:** Clarify requirements before creating the MainWindow deconstruction subplan.  
**Status:** Awaiting Answers

---

## 1. Toolbar Simplification

### Q1.1: Page Navigation Spinbox
The page navigation spinbox (`pageInput`) and prev/next buttons - should these be:
- (a) Deleted entirely (users navigate via dial, scroll, or keyboard)
- (b) Kept but simplified (just the spinbox, no direction tracking)
- (c) Hidden by default but available in settings

**Answer:** Delete prev/next buttons (already hidden anyway). Keep the spinbox - will be reconnected to DocumentViewport page navigation in the future. 

---

### Q1.2: Responsive 2-Row Layout
The 2-row responsive layout - should we:
- (a) Delete it entirely and use a single fixed row
- (b) Keep responsive behavior but rewrite cleaner
- (c) Replace with a different approach (e.g., overflow menu for less-used buttons)

**Answer:** Delete entirely. Most buttons will move to floating **subtoolbars** (GoodNotes-style). When user clicks a tool (e.g., pen), a subtoolbar appears with customizable thickness/color presets. Main toolbar will be minimal and never overflow. 

---

### Q1.3: Overflow Menu
The overflow menu button (hamburger menu with "Import/Clear Document", "Export PDF", zoom controls, etc.) - is this:
- (a) Staying as the home for less-used actions
- (b) Being replaced with keyboard shortcuts only
- (c) Being redesigned

**Answer:** Redesigned - these actions will move to subtoolbars (GoodNotes-style). The overflow menu concept stays but implemented as context-sensitive floating panels. 

---

## 2. Dial UI

### Q2.1: Magic Dial
The magic dial (the OLED-style circular controller) - is this:
- (a) Staying exactly as-is (it works, just needs file extraction)
- (b) Staying but being simplified
- (c) Being redesigned or removed

**Answer:** Staying with cleanup. Works mostly as-is, some features need tweaks. **Key requirement:** Must be modular enough to be completely unlinked at compile time. For Android builds, MagicDial will be excluded entirely (doesn't make sense on mobile/touch platforms). 

---

### Q2.2: Dial Mode Toolbar
The dial mode toolbar (the vertical strip on the right with dial mode buttons) - same options as above?

**Answer:** Same as MagicDial - staying but must be modular. Will be excluded from Android builds along with MagicDial. Extract to same module so they can be conditionally compiled together. 

---

## 3. Sidebars

### Q3.1: PDF Outline Sidebar
PDF Outline sidebar - is this:
- (a) Staying (for PDF documents)
- (b) Being removed (outline info shown differently)
- (c) Staying but needs reimplementation for DocumentViewport

**Answer:** Option C - Staying but needs reimplementation. The `PdfProvider` abstraction layer exists (`source/pdf/PdfProvider.h`, `PopplerPdfProvider.cpp`) for cross-platform PDF support. However, many PDF features (text extraction, highlighter tool) aren't integrated into Document/DocumentViewport yet. PDF outline extraction will be **delayed** until those features are implemented. 

---

### Q3.2: Bookmarks Sidebar
Bookmarks sidebar - same options as above?

**Answer:** Option C - Staying but needs reimplementation. Changes needed due to:
1. New persistence model (how bookmarks are stored in Document)
2. New page switching model (continuous scroll in DocumentViewport vs old page-by-page)

**Delayed** until Document persistence and viewport navigation are finalized. 

---

### Q3.3: Layer Panel
Layer Panel seems fully working. Any changes needed?

**Answer:** All features are in place and working. Only **style refresh** needed to match the new UI aesthetic. No functional changes required. 

---

### Q3.4: Markdown Notes Sidebar
Markdown Notes sidebar - is this:
- (a) Staying and needs proper implementation
- (b) Being removed (feature cut)
- (c) Deferred to later phase

**Answer:** Staying but **significantly redesigned**. Notes will connect to a new **InsertedLink** object type (subclass of `InsertedObject`) instead of highlighted text. InsertedLink objects can point to:
- Another coordinate on the DocumentViewport
- External resources
- Other linkable targets

**Delayed** until `LinkObject` (or `InsertedLink`) implementation is complete. This represents a major architectural change from text-anchored to spatially-anchored notes. 

---

## 4. Controller Support

### Q4.1: SDL Controller (Gamepad)
The SDL controller (gamepad) support - is this:
- (a) Staying as-is
- (b) Being simplified
- (c) Being removed

**Answer:** Staying but needs **major rework**:
1. Designed to work primarily with MagicDial - should be bundled with it
2. Needs ability to be **unlinked for mobile builds** (same as MagicDial)
3. Code is unnecessarily complex - split between MainWindow (signal handlers, button→action mapping) and SDLControllerManager.cpp
4. Should consolidate all controller logic into a unified input handler module

**Architecture:** Bundle with DialController module, share conditional compilation flags. 

---

### Q4.2: Mouse Dial System
The mouse dial system (hold mouse buttons + scroll wheel = dial control) - same options?

**Answer:** Staying with minimal changes. Mouse dial is a **direct mapping** to MagicDial functionality (different input method, same output). Needs:
1. Ability to be **unlinked and separated** (same as MagicDial)
2. No functional changes needed

**Architecture:** Part of DialController module, same conditional compilation as MagicDial and SDL controller. 

---

## 5. Stubbed Features

### Q5.1: PDF Text Selection
PDF text selection button - is this:
- (a) Being implemented soon
- (b) Being deferred indefinitely
- (c) Being removed

**Answer:** **Deferred** - related to Q3.1 (PDF features). Will be reimplemented after `ToolType::Highlighter` is working for PDF text extraction. Part of the broader PDF feature roadmap:
1. PdfProvider integration → 2. Text extraction → 3. Highlighter tool → 4. Text selection UI

Keep the button (hidden/stubbed) for now; implementation comes later. 

---

### Q5.2: Background Selection Button
Background selection button (`selectBackground`) - is this:
- (a) Being implemented to set page/tile background
- (b) Being replaced with a different UI
- (c) Being removed (backgrounds set via settings only)

**Answer:** **Remove entirely**. This feature was dropped long ago and the button has been hidden for a long time. Safe to delete:
- `backgroundButton` widget
- `selectBackground()` method
- Any related signal connections 

---

## 6. Export/Save Features

### Q6.1: PDF Export
PDF export - when reimplemented, should it:
- (a) Be a separate `PdfExporter` class called from MainWindow
- (b) Be a menu action that opens an export dialog
- (c) Something else

**Answer:** **Full rewrite needed** - current pdftk-based approach is unsatisfactory:
- pdftk is Java-based, single-threaded, poor performance
- No code will be directly reused
- Conceptual logic (identify annotated pages → render overlays → merge) may inspire new implementation
- New implementation will use **PdfProvider abstraction** for cross-platform/mobile support
- Native PDF writing (QPdfWriter or similar) instead of shelling out to external process

**Action now:** Delete all old export code (`#if 0` blocks). Clean slate for future implementation. 

---

### Q6.2: Save Button
The save button - currently stubbed. Will saving be:
- (a) Auto-save only (like now with edgeless temp bundles)
- (b) Manual save via Ctrl+S / save button
- (c) Both

**Answer:** **Simple direct mapping**. The new save button will just trigger what Ctrl+S does now (`saveDocument()` via DocumentManager). 

Old logic was messy (save current page vs save temp notebook vs various conditions). New approach:
- Save button click → `saveDocument()` 
- Same as Ctrl+S keyboard shortcut
- No complex conditional logic needed 

---

## 7. Window State

### Q7.1: Fullscreen Toggle
The fullscreen toggle that hides control bar + tab bar - is this:
- (a) Staying as-is (works well)
- (b) Being simplified
- (c) Being replaced with a different distraction-free mode

**Answer:** There are actually **two different "fullscreen" features**:

**1. Toolbar Fullscreen Button** (`fullscreenButton`):
- Toggles between windowed ↔ OS fullscreen mode
- Logic works fine, **keep as-is**
- Will move to new toolbar module when toolbar is separated

**2. MagicDial "Fullscreen"** (in PanAndPageScroll mode):
- Currently hides toolbar + tab bar (distraction-free mode)
- **Needs redesign** - UI structure has changed (now has sidebars, layer panel, dial toolbar, etc.)
- Toggle will be **relocated out of MagicDial** to a more appropriate place
- Should hide/show all non-essential UI elements based on new layout 

---

## 8. Future UI Direction

### Q8.1: New UI Design
Is there a planned new UI/UX design, or are we cleaning up to make the current design maintainable?

**Answer:** Styles will be refreshed. Two **new UI elements** connected to MainWindow:

**1. Subtoolbar** (discussed in Q1.2):
- GoodNotes-style floating panel
- Shows tool-specific options (thickness, color presets)
- Toggled by clicking tool buttons

**2. Unified Option Menu** (new):
- Touch-friendly context menu for tablet PCs (can't rely on keyboard shortcuts)
- Shows available actions **after an operation completes** (e.g., after lasso selection → shows copy, paste, delete, etc.)
- Solves discoverability problem - users don't need to know shortcuts
- **Reusable** across many features (lasso, object selection, text selection, etc.)
- Example flow: Lasso select → menu appears → user taps "Copy" or "Delete" 

---

### Q8.2: Touch-Friendly UI
Touch-friendly UI - is the current button size (36x36) appropriate, or should buttons be larger/different for pen/touch devices?

**Answer:** Current size is **already appropriate**. 36×36 logical pixels:
- At 192 DPI (2x scaling): 72×72 physical pixels
- Tablet PCs often have even higher DPI, so buttons scale up further
- Already designed with touch/pen input in mind
- No changes needed 

---

## Summary of Decisions

| Topic | Decision | Action Now |
|-------|----------|------------|
| Page Navigation | Delete prev/next, keep spinbox | Delete buttons, stub spinbox |
| Responsive Layout | Delete entirely | Delete 2-row layout code |
| Overflow Menu | Redesign as subtoolbars | Keep stub, redesign later |
| Magic Dial | Keep, make modular | Extract to DialController, add compile flag |
| Dial Toolbar | Keep, make modular | Bundle with DialController |
| PDF Outline | Deferred | Keep stub, wait for PdfProvider integration |
| Bookmarks | Deferred | Keep stub, wait for Document persistence |
| Layer Panel | Working | Style refresh only |
| Markdown Notes | Redesigned | Keep stub, wait for InsertedLink object |
| SDL Controller | Keep, major rework | Bundle with DialController, add compile flag |
| Mouse Dial | Keep | Bundle with DialController |
| PDF Text Select | Deferred | Keep stub, wait for Highlighter tool |
| Background Button | **DELETE** | Remove button + method entirely |
| PDF Export | Full rewrite | **DELETE all #if 0 code** |
| Save System | Simplify | Just map to saveDocument() |
| Fullscreen (OS) | Keep | Moves with toolbar |
| Fullscreen (UI hide) | Redesign | Remove from MagicDial, redesign later |
| New: Subtoolbar | New feature | Implement after cleanup |
| New: Option Menu | New feature | Implement after cleanup |
| Button Size | No change | Already touch-friendly |

---

## Ready for Subplan Creation

All questions answered. Next step: Create `MAINWINDOW_CLEANUP_SUBPLAN.md` with:
1. Phase 1: Delete dead code (#if 0 blocks, background button, old export)
2. Phase 2: Extract DialController module (with compile flags)
3. Phase 3: Simplify toolbar (delete 2-row layout, prep for subtoolbar)
4. Phase 4: Clean up remaining MainWindow code

