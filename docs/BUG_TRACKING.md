# SpeedyNote Bug Tracking

## Overview

This document tracks bugs, regressions, and polish issues discovered during and after the migration. Each bug is assigned a unique ID for reference in commit messages and code comments.

**Format:** `BUG-{CATEGORY}-{NUMBER}` (e.g., `BUG-VP-001` for viewport bugs)

**Last Updated:** Jan 14, 2026

---

## Categories

| Prefix | Category | Description |
|--------|----------|-------------|
| **VP** | Viewport | DocumentViewport rendering, pan/zoom, coordinate transforms |
| **DRW** | Drawing | Pen, Marker, Eraser stroke behavior |
| **LSO** | Lasso | Lasso selection, transforms, clipboard |
| **HL** | Highlighter | Highlighter tool, PDF text selection |
| **SL** | Straight Line | Straight line mode |
| **OBJ** | Objects | ImageObject, LinkObject, ObjectSelect tool |
| **LYR** | Layers | Layer panel, layer operations |
| **PG** | Pages | Page panel, page navigation, add/delete |
| **PDF** | PDF | PDF loading, rendering, outline, caching |
| **FILE** | File I/O | Save/load, bundle format, dirty tracking |
| **TAB** | Tabs | Tab management, switching, close behavior |
| **TB** | Toolbar | Main toolbar, tool buttons |
| **STB** | Subtoolbar | Tool-specific subtoolbars |
| **AB** | Action Bar | Context action bar |
| **SB** | Sidebar | Left sidebar container, panel switching |
| **TCH** | Touch | Touch gestures, tablet input |
| **MD** | Markdown | Markdown notes integration |
| **PERF** | Performance | Lag, memory, CPU issues |
| **UI** | UI/UX | Visual glitches, layout issues |
| **MISC** | Miscellaneous | Other issues |

---

## Priority Levels

| Level | Description | Response |
|-------|-------------|----------|
| 🔴 **P0** | Critical | Data loss, crash, unusable feature - fix immediately |
| 🟠 **P1** | High | Major feature broken, poor UX - fix soon |
| 🟡 **P2** | Medium | Minor feature issue, workaround exists |
| 🟢 **P3** | Low | Polish, cosmetic, edge case |

---

## Bug Status

| Status | Description |
|--------|-------------|
| 🆕 **NEW** | Reported, not yet investigated |
| 🔍 **INVESTIGATING** | Root cause being analyzed |
| 🔧 **IN PROGRESS** | Fix being implemented |
| ✅ **FIXED** | Fix complete and verified |
| ⏸️ **DEFERRED** | Won't fix now, tracked for later |
| ❌ **WONTFIX** | By design, not a bug, or not worth fixing |

---

## Active Bugs

### Viewport (VP)

<!-- Template:
#### BUG-VP-XXX: [Title]
**Priority:** 🟡 P2 | **Status:** 🆕 NEW

**Symptom:** 
[What the user sees/experiences]

**Steps to Reproduce:**
1. Step one
2. Step two
3. Step three

**Expected:** [What should happen]
**Actual:** [What actually happens]

**Root Cause:** 
[Technical explanation - fill in during investigation]

**Fix:**
[Solution description]

**Files Modified:**
- `source/core/DocumentViewport.cpp`

**Verified:** [ ] Tested and working
-->

---

### Drawing (DRW)

---

### Lasso (LSO)

---

### Highlighter (HL)

---

### Straight Line (SL)

---

### Objects (OBJ)

---

### Layers (LYR)

---

### Pages (PG)

---

### PDF (PDF)

---

### File I/O (FILE)

---

### Tabs (TAB)

---

### Toolbar (TB)

---

### Subtoolbar (STB)

---

### Action Bar (AB)

---

### Sidebar (SB)

---

### Touch/Tablet (TCH)

---

### Markdown (MD)

---

### Performance (PERF)

---

### UI/UX (UI)

---

### Miscellaneous (MISC)

---

## Fixed Bugs Archive

<!-- Move fixed bugs here with their full details for reference -->

### Recently Fixed

---

## Statistics

| Category | New | In Progress | Fixed | Total |
|----------|-----|-------------|-------|-------|
| Viewport | 0 | 0 | 0 | 0 |
| Drawing | 0 | 0 | 0 | 0 |
| Lasso | 0 | 0 | 0 | 0 |
| Highlighter | 0 | 0 | 0 | 0 |
| Objects | 0 | 0 | 0 | 0 |
| Layers | 0 | 0 | 0 | 0 |
| Pages | 0 | 0 | 0 | 0 |
| PDF | 0 | 0 | 0 | 0 |
| File I/O | 0 | 0 | 0 | 0 |
| Tabs | 0 | 0 | 0 | 0 |
| Toolbar | 0 | 0 | 0 | 0 |
| Subtoolbar | 0 | 0 | 0 | 0 |
| Action Bar | 0 | 0 | 0 | 0 |
| Sidebar | 0 | 0 | 0 | 0 |
| Touch | 0 | 0 | 0 | 0 |
| Markdown | 0 | 0 | 0 | 0 |
| Performance | 0 | 0 | 0 | 0 |
| UI/UX | 0 | 0 | 0 | 0 |
| **TOTAL** | **0** | **0** | **0** | **0** |

---

## Notes

### Commit Message Format
```
Fix BUG-VP-001: [Short description]

[Longer explanation if needed]
```

### Code Comment Format
```cpp
// BUG-VP-001 FIX: [Explanation of what this code fixes]
```

### Related Documents
- See `*_QA.md` files for feature-specific test cases
- See `*_SUBPLAN.md` files for implementation details
- Code review fixes are documented as `CR-*` in subplan files

