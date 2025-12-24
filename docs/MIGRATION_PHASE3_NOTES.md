# Phase 3 Migration Notes

> **Purpose:** Track decisions and current state during Phase 3 implementation.
> **Last Updated:** Dec 23, 2024

---

## Current State Summary

### Completed Tasks

| Task | Status | Notes |
|------|--------|-------|
| 3.0.1 DocumentManager | ✅ Complete | `source/core/DocumentManager.h/.cpp` |
| 3.0.2 TabManager | ✅ Complete | `source/ui/TabManager.h/.cpp` |
| 3.0.3 LayerPanel | ✅ Complete | `source/ui/LayerPanel.h/.cpp` |
| 3.0.4 Command line flag | ✅ Complete | `--use-new-viewport` (currently unused) |
| 3.1.3 Remove VectorCanvas | ✅ Complete | Removed from MainWindow, NOT from CMakeLists yet |

### Reference Files

- `source/MainWindow_OLD.cpp` - Original MainWindow before Phase 3
- `source/MainWindow_OLD.h` - Original header before Phase 3

---

## Key Decisions

### Architecture

1. **Tab System:** Completely replace `tabList` (QListWidget) + `canvasStack` (QStackedWidget) + `pageMap` with `QTabWidget` via `TabManager`

2. **TabManager Creates QTabWidget:** TabManager is responsible for creating the QTabWidget, not MainWindow

3. **LayerPanel ↔ Page:** LayerPanel talks DIRECTLY to Page, NOT through DocumentViewport

4. **Ownership:**
   - DocumentManager OWNS → Document
   - TabManager CREATES → DocumentViewport
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

`SpnPackageManager.cpp` includes `InkCanvas.h` for `BackgroundStyle` enum. This dependency needs to be broken - either:
- Move `BackgroundStyle` to a separate header, OR
- Update SpnPackageManager to use new `BackgroundType` from Page.h

### ControlPanelDialog

`ControlPanelDialog` depends on InkCanvas. Will be disabled/stubbed in Phase 3.1.8.

---

## Questions Resolved

1. ✅ Phase 3.0 files exist and are complete
2. ✅ VectorCanvas removed from MainWindow (not CMakeLists yet)
3. ✅ TabManager replaces entire old tab system
4. ✅ TabManager creates QTabWidget
5. ✅ Phase 3.1 goal: compile without InkCanvas, features stubbed
6. ✅ New format is JSON → .snx package
7. ✅ --use-new-viewport flag is unused for now
8. ✅ No backwards compatibility concerns
9. ✅ Batch related tasks, manual compile verification
10. ✅ MainWindow_OLD files exist as reference

---

*Notes file for SpeedyNote Phase 3 migration*

