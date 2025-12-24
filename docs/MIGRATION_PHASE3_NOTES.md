# Phase 3 Migration Notes

> **Purpose:** Track decisions and current state during Phase 3 implementation.
> **Last Updated:** Dec 23, 2024

---

## Current State Summary

### Runtime State (Dec 23, 2024)

**App launches successfully!** Current behavior:

| Feature | Status | Notes |
|---------|--------|-------|
| App Launch | ✅ Works | No crash on startup (after fixing `updatePanRange`, `updateDialDisplay`) |
| Toolbar | ✅ Visible | Old toolbar displays correctly |
| Old Tab Bar | ⚠️ Visible but empty | `tabBarContainer` shows only add tab button (tabList removed) |
| New Tab Bar (QTabWidget) | ✅ Works | Below toolbar, creates DocumentViewport tabs correctly |
| Add Tab Button | ✅ Works | Creates new tabs with DocumentViewport |
| DocumentViewport | ✅ Works | Same behavior as `--test-viewport` window |
| Color Buttons | ❌ Crash | Calls `currentCanvas()->setPenColor()` on null |
| PDF/Bookmark Sidebars | ✅ Toggle works | Empty content (expected) |
| MagicDial Display | ✅ Correct | Shows "No Canvas" text |

**UI Layout Issue:** Two tab bars visible - old `tabBarContainer` (top) and new `m_tabWidget` (below toolbar). Should consolidate to single tab bar.

### Completed Tasks

| Task | Status | Notes |
|------|--------|-------|
| 3.0.1 DocumentManager | ✅ Complete | `source/core/DocumentManager.h/.cpp` |
| 3.0.2 TabManager | ✅ Complete | `source/ui/TabManager.h/.cpp` |
| 3.0.3 LayerPanel | ✅ Complete | `source/ui/LayerPanel.h/.cpp` |
| 3.0.4 Command line flag | ✅ REMOVED | `--use-new-viewport` removed in 3.1.1 |
| 3.1.3 Remove VectorCanvas | ✅ Complete | Removed from MainWindow, NOT from CMakeLists yet |
| 3.1.1 Replace Tab System | ✅ Complete | tabList, canvasStack, pageMap removed; m_tabWidget + TabManager in place |
| 3.1.2 addNewTab stubbed | ✅ Complete | Old InkCanvas code wrapped in comment block |
| 3.1.6 Page Nav stubbed | ✅ Complete | switchPage, switchPageWithDirection stubbed |
| LauncherWindow | ⏸️ Disconnected | Commented out from CMakeLists.txt, sharedLauncher refs commented |
| Touch/Palm Rejection | ⏸️ Stubbed | onStylusProximityEnter, restoreTouchGestureMode stubbed |
| updateTabSizes | ⏸️ Stubbed | QTabWidget handles its own sizing |
| updateTheme tabList | ⏸️ Stubbed | Will use m_tabWidget styling in Phase 3.3 |
| updatePanRange | ⏸️ Stubbed | DocumentViewport handles own pan/zoom |
| updateDialDisplay | ⏸️ Protected | Returns early if no currentCanvas() |

### Reference Files

- `source/MainWindow_OLD.cpp` - Original MainWindow before Phase 3
- `source/MainWindow_OLD.h` - Original header before Phase 3

---

## Key Decisions

### Architecture

1. **Tab System:** Completely replace `tabList` (QListWidget) + `canvasStack` (QStackedWidget) + `pageMap` with `QTabWidget` via `TabManager`

2. **TabManager Receives QTabWidget:** MainWindow creates QTabWidget, passes pointer to TabManager. TabManager does NOT own QTabWidget - just manages tab operations. (CORRECTED: Previous note was wrong)

3. **LayerPanel ↔ Page:** LayerPanel talks DIRECTLY to Page, NOT through DocumentViewport

4. **Tab Bar Position:** Tab bar stays at the very top of MainWindow (already in place), with back button and add tab button on each side

5. **Ownership:**
   - DocumentManager OWNS → Document
   - MainWindow OWNS → QTabWidget (creates it)
   - TabManager RECEIVES → QTabWidget pointer (does NOT own)
   - TabManager OWNS → DocumentViewport widgets (creates them)
   - DocumentViewport REFERENCES → Document (pointer, not ownership)
   - LayerPanel REFERENCES → Page (pointer, updated when page/tab changes)

### File Format

1. **Initial Format:** JSON file containing:
   - Document metadata (per MIGRATION_PHASE1_2_SUBPLAN.md lines 230-253)
   - Strokes from each VectorLayer on each Page

2. **Future Format:** QDataStream-based package (`.snx` extension) containing:
   - Base JSON metadata
   - Inserted pictures, text, and other embedded objects

3. **Extension:** `.snx` (SpeedyNote eXtended) - NOT compatible with old `.spn`

4. **Old .spn Files:** NOT supported - breaking change is acceptable

### Feature Flags

- `--use-new-viewport` flag exists but is currently unused ("useless")
- Can be ignored for now; may be removed or repurposed later

### Backwards Compatibility

- No concern about existing users
- Clean break from old format

---

## Phase 3.1 Task Order

Per `MIGRATION_PHASE3_1_SUBPLAN.md`:

1. ~~3.1.3 - Remove VectorCanvas~~ ✅ COMPLETE
2. **3.1.1 - Replace tab system** ← NEXT
3. 3.1.2 - Remove addNewTab InkCanvas code
4. 3.1.7 - Remove InkCanvas includes/members
5. 3.1.4 - Create currentViewport(), replace currentCanvas()
6. 3.1.6 - Remove page navigation methods
7. 3.1.5 - Stub signal handlers
8. 3.1.8 - Disable ControlPanelDialog
9. 3.1.9 - Stub markdown handlers

**Goal:** MainWindow compiles without InkCanvas. Many features will be broken/stubbed.

---

## Known Issues (Phase 3.1)

### 1. Dual Tab Bars
- **Issue:** Two tab bars visible - old `tabBarContainer` at top, new `m_tabWidget` below toolbar
- **Solution:** Move `m_tabWidget` into `tabBarContainer`, or replace tabBarContainer entirely
- **Priority:** Medium - cosmetic but confusing

### 2. Color Button Crash
- **Issue:** Clicking color buttons (red, blue, etc.) crashes - calls `currentCanvas()->setPenColor()` on null
- **Location:** `setupUi()` lambda at ~line 456+ (connect statements)
- **Solution:** Add null checks to all button lambdas, or stub them
- **Priority:** High - prevents testing

### 3. Tool Button Crash (likely)
- **Issue:** Pen/Marker/Eraser buttons likely crash similarly
- **Solution:** Same as color buttons

### 4. 176 `currentCanvas()` Calls
- **Issue:** Many functions still call `currentCanvas()` without null checks
- **Solution:** Phase 3.1.4 - Replace with `currentViewport()` or add null guards

---

## Current MainWindow State

### Members to Remove (Phase 3.1.1)

```cpp
QListWidget *tabList;          // Line 593 - Replace with QTabWidget
QStackedWidget *canvasStack;   // Line 594 - Remove
QMap<InkCanvas*, int> pageMap; // Line 573 - Remove (Viewport tracks own page)
```

### Members to Add (Phase 3.1.1)

```cpp
#include "ui/TabManager.h"
TabManager* m_tabManager = nullptr;
```

### Already Removed (Phase 3.1.3)

- `vectorPenButton`, `vectorEraserButton`, `vectorUndoButton` - Removed from header
- `setVectorPenTool()`, `setVectorEraserTool()`, `vectorUndo()` - Removed from implementation
- VectorCanvas still in CMakeLists.txt (InkCanvas dependency)

---

## Execution Strategy

1. **Batch related tasks** when they don't break compilation independently
2. **Compile verification** after groups of changes (not after every single change)
3. **User runs compile commands manually**
4. **Keep stubbed methods** for features to be reconnected in Phase 3.2+

---

## Notes for Implementation

### VectorCanvas in CMakeLists.txt

VectorCanvas.cpp is still in CMakeLists.txt because InkCanvas depends on it. Once InkCanvas is disconnected (end of Phase 3.1), both can be removed from the build.

### SpnPackageManager

**Can be DISCONNECTED now.** SpnPackageManager handles old `.spn` format which we're abandoning.
- The ability to save/load `.snx` packages will be implemented LATER
- For now, documents are in-memory only (no persistence)
- This also breaks the InkCanvas dependency via BackgroundStyle

### Launch Sequence (Phase 3 - Simplified)

Main.cpp → MainWindow (DIRECTLY)
- **Skip LauncherWindow entirely** during Phase 3
- Main.cpp will be significantly simplified (drop most old code)
- LauncherWindow will be relinked LATER (after core is working)

### Files Directly Under `/source`

**ASSUME NONE OF THESE EXIST** except Main.cpp and MainWindow:
- LauncherWindow.cpp/.h - NOT LINKED during Phase 3
- SpnPackageManager.cpp/.h - NOT LINKED
- InkCanvas.cpp/.h - Goal: disconnect from MainWindow
- VectorCanvas.cpp/.h - Already removed from MainWindow
- ControlPanelDialog.cpp/.h - NOT LINKED
- SDLControllerManager.cpp/.h - NOT LINKED
- SimpleAudio.cpp/.h - NOT LINKED
- PictureWindow*.cpp/.h - NOT LINKED
- MarkdownNotes*.cpp/.h - NOT LINKED
- RecentNotebooksManager.cpp/.h - NOT LINKED
- And ALL other auxiliary files - NOT LINKED

**Only subfolders of /source contain reusable new logic:**
- `/source/core/` - Document, Page, DocumentViewport, DocumentManager
- `/source/ui/` - TabManager, LayerPanel
- `/source/layers/` - VectorLayer
- `/source/strokes/` - VectorStroke, StrokePoint
- `/source/objects/` - InsertedObject hierarchy
- `/source/pdf/` - PDF handling (if needed)

### Disconnection Strategy

**Go straight to MainWindow InkCanvas disconnection.**
Let dependent files break - they'll be disconnected from the build anyway.
Don't waste time fixing SpnPackageManager, LauncherWindow, etc.

### Debug Overlay

The debug overlay in DocumentViewport (lines 716-763) will be MOVED to MainWindow later.
Currently shows: document info, zoom, pan, tool, undo/redo state, paint rate.
This is temporary test code from Phase 2.

### Initial Document (Phase 3)

Before LauncherWindow is relinked, MainWindow should start with a blank document.
**Use DocumentManager to create the document** - maintain proper structure for consistency.
Don't copy test code from DocumentViewportTests - keep architecture clean.

---

## Implementation Decisions

### Main.cpp Changes
- **Comment out** LauncherWindow code (don't delete - may need reference)
- Go directly to MainWindow
- Remove `useNewViewport` parameter from MainWindow constructor

### CMakeLists.txt Changes
- **Comment out** files as we disconnect them (not delete)
- This makes it easy to see what was removed and recover if needed

### MainWindow Constructor
- Remove `bool useNewViewport` parameter entirely
- Always use new DocumentViewport architecture
- Create blank Document via DocumentManager on startup

### ControlPanelDialog

`ControlPanelDialog` depends on InkCanvas. Will be disabled/stubbed in Phase 3.1.8.

---

## Questions Resolved

1. ✅ Phase 3.0 files exist and are complete
2. ✅ VectorCanvas removed from MainWindow (not CMakeLists yet)
3. ✅ TabManager replaces entire old tab system
4. ✅ TabManager RECEIVES QTabWidget (MainWindow creates it) - CORRECTED
5. ✅ Phase 3.1 goal: compile without InkCanvas, features stubbed
6. ✅ New format is JSON → .snx package
7. ✅ --use-new-viewport flag: REMOVE entirely (always use new architecture)
8. ✅ No backwards compatibility concerns
9. ✅ Batch related tasks, manual compile verification
10. ✅ MainWindow_OLD files exist as reference
11. ✅ Skip LauncherWindow entirely during Phase 3
12. ✅ Main.cpp: comment out LauncherWindow code, go directly to MainWindow
13. ✅ Initial document: blank Document via DocumentManager (not test document)
14. ✅ CMakeLists.txt: comment out files as we go (not delete)
15. ✅ All files under /source except Main/MainWindow: assume don't exist

---

*Notes file for SpeedyNote Phase 3 migration*

