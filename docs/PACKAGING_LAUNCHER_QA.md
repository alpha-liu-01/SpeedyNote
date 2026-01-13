# Packaging & Launcher Modernization - Q&A Document

## Overview

This document captures design decisions for:
1. Finalizing the .snb bundle format as the sole packaging solution
2. Modernizing the Launcher Window as the primary entry point
3. Reorganizing keyboard shortcuts and file operations
4. Touch-first UI design

---

## Section 1: Bundle Format & Cleanup

### Q1.1: What is the final .snb bundle structure?

**Current structure:**
```
notebook.snb/
├── document.json          # Manifest (metadata, page count, mode)
├── pages/                 # Paged document content
│   ├── page_0.json
│   ├── page_1.json
│   └── ...
├── tiles/                 # Edgeless document content
│   ├── tile_0_0.json
│   └── ...
├── assets/
│   ├── images/            # Inserted images
│   │   └── {uuid}.png
│   └── notes/             # Markdown notes
│       └── {uuid}.md
└── .snb_marker            # (Optional) Identifies this as SpeedyNote bundle
```

**Questions:**
- Should we add a `.snb_marker` file to help identify bundles?
- Should `document.json` include a format version for future compatibility?
- Any other metadata to add to `document.json`?

**Answer:** [PENDING]

---

### Q1.2: Where should app-level metadata be stored?

App-level metadata includes:
- Recent notebooks list
- Starred notebooks list  
- Last accessed page per notebook
- Thumbnail cache location

**Options:**
1. **QSettings** (current approach for RecentNotebooksManager)
   - Pros: Cross-platform, built into Qt
   - Cons: Limited structure, hard to inspect

2. **JSON file in app data folder**
   - Pros: Human-readable, easy to debug
   - Cons: Need to handle file I/O

3. **SQLite database**
   - Pros: Fast queries, ACID transactions
   - Cons: Heavier dependency, overkill for this use case

**Recommendation:** Keep QSettings for simple key-value (recent paths, starred paths), but store thumbnails in a cache folder.

**Answer:** [PENDING]

---

### Q1.3: What to do with legacy .spn files?

**Options:**
1. **No migration** - Clean break, users must re-annotate PDFs
2. **One-time conversion** - Offer to convert .spn to .snb on first open
3. **Read-only support** - Can open .spn but saves as .snb

**Recommendation:** Option 1 (clean break) since the architectures are fundamentally incompatible.

**Answer:** [PENDING]

---

### Q1.4: Files to delete during cleanup

| File | Reason |
|------|--------|
| `SpnPackageManager.h/cpp` | Obsolete .spn format |
| `RecentNotebooksManager.h/cpp` | Will rebuild from scratch |
| Any .spn references in other files | Legacy cleanup |

**Answer:** [PENDING - confirm list]

---

## Section 2: Launcher Window Design

### Q2.1: What views should the Launcher have?

**Current sidebar tabs:**
1. Return (to previous document)
2. New (create notebook)
3. Open PDF
4. Open Notes
5. Recent
6. Starred

**Proposed redesign:**

| View | Description | Touch-Friendly? |
|------|-------------|-----------------|
| **Timeline** | Chronological list of all notebooks | ✅ Swipe to scroll |
| **Starred** | Pinned/favorite notebooks | ✅ Grid of cards |
| **New** | Create new notebook (edgeless/paged/from PDF) | ✅ Large buttons |
| **Search** | Find notebooks by name/content | ✅ On-screen keyboard |

**Questions:**
- Should "Open PDF" and "Open Notes" be merged into a single "Import/Open" action?
- Should "Return" be a button instead of a tab (since it's an action, not a view)?
- Is the timeline view sorted by last modified or last accessed?

**Answer:** [PENDING]

---

### Q2.2: Timeline view design

The timeline should show notebooks grouped by time period.

**Proposed groupings:**
- Today
- Yesterday
- This Week
- This Month
- Older (by month: January 2026, December 2025, etc.)

**Each notebook card shows:**
- Thumbnail (first page preview)
- Name
- Last modified date/time
- Type indicator (PDF-based / Edgeless / Paged)
- Star indicator (if starred)

**Questions:**
- Should the timeline include deleted notebooks (with "restore" option)?
- Should we track "last accessed" separately from "last modified"?
- What happens to very old notebooks (archive after X months)?

**Answer:** [PENDING]

---

### Q2.3: Touch-first interaction patterns

**Core principles:**
1. All actions reachable with one hand (thumb zone)
2. Large touch targets (minimum 44x44 dp)
3. Swipe gestures for common actions
4. No hover-dependent UI
5. Context menus via long-press instead of right-click

**Proposed gestures:**

| Gesture | Action |
|---------|--------|
| Tap notebook | Open notebook |
| Long-press notebook | Show context menu (star, delete, duplicate, rename) |
| Swipe left on notebook | Quick delete (with undo) |
| Swipe right on notebook | Quick star/unstar |
| Pull down | Refresh list |
| Pinch | Zoom grid (more/fewer columns) |

**Questions:**
- Which gestures should be implemented?
- Should swipe-to-delete require confirmation or just show undo toast?
- How to handle pinch zoom on notebooks grid?

**Answer:** [PENDING]

---

### Q2.4: New notebook creation flow

**Current:** Single "Create New Notebook" button creates edgeless canvas.

**Proposed options for "New" view:**

```
┌─────────────────────────────────────────┐
│           Create New Notebook           │
├─────────────────────────────────────────┤
│  ┌───────────┐  ┌───────────┐          │
│  │           │  │           │          │
│  │  Blank    │  │  From     │          │
│  │  Canvas   │  │   PDF     │          │
│  │           │  │           │          │
│  └───────────┘  └───────────┘          │
│                                         │
│  ┌───────────┐  ┌───────────┐          │
│  │           │  │           │          │
│  │  Paged    │  │  From     │          │
│  │ Notebook  │  │ Template  │          │
│  │           │  │           │          │
│  └───────────┘  └───────────┘          │
└─────────────────────────────────────────┘
```

**Questions:**
- What types of notebooks can be created?
  - Blank edgeless canvas
  - PDF annotation (import PDF)
  - Paged notebook (with background options: blank, grid, lines)
  - From template (future feature?)
- Should background style (grid/lines/blank) be chosen at creation or later?

**Answer:** [PENDING]

---

## Section 3: Keyboard Shortcuts & File Operations

### Q3.1: Current keyboard shortcuts to reorganize

| Shortcut | Current Action | Location |
|----------|---------------|----------|
| `Ctrl+N` | New tab (current type) | MainWindow |
| `Ctrl+Shift+N` | New edgeless tab | MainWindow |
| `Ctrl+O` | Open file | MainWindow |
| `Ctrl+Shift+O` | Open PDF | MainWindow |
| `Ctrl+Shift+L` | Open document (.snb) | MainWindow |
| `Ctrl+S` | Save | MainWindow |
| `Ctrl+Shift+S` | Save As | MainWindow |
| `Ctrl+W` | Close tab | MainWindow |
| `Ctrl+Tab` | Next tab | MainWindow |
| `Ctrl+Shift+Tab` | Previous tab | MainWindow |

**Questions:**
- Which shortcuts should open the Launcher instead of acting directly?
- Should `Ctrl+O` show a unified "Open" dialog or go to Launcher?
- Should there be a shortcut to toggle Launcher visibility?

**Answer:** [PENDING]

---

### Q3.2: "Add Tab" button consolidation

The current "+" button on the tab bar needs to handle multiple creation types.

**Option A: Dropdown menu on + button**
```
[+] ▼
├── New Edgeless Canvas     (Ctrl+Shift+N)
├── New Paged Notebook      (Ctrl+N)
├── ─────────────────────
├── Open PDF...             (Ctrl+Shift+O)
├── Open Notebook...        (Ctrl+Shift+L)
└── ─────────────────────
    Go to Launcher          (Ctrl+L)
```

**Option B: + button opens Launcher's "New" view**
- Simpler UI
- Consistent with touch-first approach
- All creation happens in one place

**Option C: Quick action + long-press for menu**
- Tap + → Creates default type (edgeless?)
- Long-press + → Shows full menu

**Recommendation:** Option B (Launcher as central hub) for consistency.

**Answer:** [PENDING]

---

### Q3.3: Should Launcher be modal or coexist with MainWindow?

**Options:**

1. **Modal Launcher** (current behavior)
   - Launcher hides MainWindow when shown
   - User must choose a notebook before continuing
   - Simpler state management

2. **Coexisting Windows**
   - Launcher and MainWindow can both be visible
   - Can drag-drop between them
   - More complex but more flexible

3. **Integrated Launcher** (like VS Code's welcome tab)
   - Launcher is a special "tab" within MainWindow
   - No separate window
   - Seamless transition

**Recommendation:** Option 1 (modal) for simplicity, especially on touch devices.

**Answer:** [PENDING]

---

## Section 4: Technical Architecture

### Q4.1: How should thumbnails be generated and cached?

**Generation:**
- Render first page of document at reduced resolution
- For PDF-based: Use existing PDF rendering
- For edgeless: Render visible portion at origin
- For paged: Render page 0

**Caching:**
- Store in app's cache directory (`QStandardPaths::CacheLocation`)
- Filename: `{document_id}_thumb.png`
- Invalidate when document is modified

**Questions:**
- What resolution for thumbnails? (e.g., 300x400 px)
- When to regenerate? (on close? on save? on demand?)
- Maximum cache size before cleanup?

**Answer:** [PENDING]

---

### Q4.2: How does Launcher communicate with MainWindow?

**Current approach:** Launcher creates/finds MainWindow and calls methods directly.

**Proposed approach:** Keep direct method calls but clean up the interface:

```cpp
// MainWindow public interface for Launcher
class MainWindow {
public:
    // Document operations
    void openDocument(const QString& bundlePath);
    void openPdf(const QString& pdfPath);
    void createNewEdgeless();
    void createNewPaged();
    
    // Tab operations  
    bool hasOpenDocuments() const;
    bool switchToDocument(const QString& bundlePath);
    
    // Window operations
    void bringToFront();
};
```

**Answer:** [PENDING]

---

### Q4.3: What replaces RecentNotebooksManager?

**Options:**

1. **Merge into DocumentManager**
   - Recent/starred tracking becomes part of DocumentManager
   - Single source of truth for all document state

2. **New NotebookLibrary class**
   - Dedicated class for library management
   - Handles recent, starred, search, thumbnails
   - Cleaner separation of concerns

3. **LauncherWindow handles it internally**
   - Simplest approach
   - Launcher owns its own data

**Recommendation:** Option 2 (NotebookLibrary) for clean architecture.

```cpp
class NotebookLibrary : public QObject {
public:
    // Recent/Starred management
    QList<NotebookInfo> recentNotebooks() const;
    QList<NotebookInfo> starredNotebooks() const;
    void addToRecent(const QString& bundlePath);
    void toggleStarred(const QString& bundlePath);
    void removeFromRecent(const QString& bundlePath);
    
    // Search
    QList<NotebookInfo> search(const QString& query) const;
    
    // Thumbnails
    QPixmap thumbnailFor(const QString& bundlePath) const;
    void invalidateThumbnail(const QString& bundlePath);
    
signals:
    void libraryChanged();
    void thumbnailReady(const QString& bundlePath);
};

struct NotebookInfo {
    QString bundlePath;
    QString name;
    QDateTime lastModified;
    QDateTime lastAccessed;
    bool isStarred;
    bool isPdfBased;
    bool isEdgeless;
};
```

**Answer:** [PENDING]

---

## Section 5: Implementation Order

### Proposed phases:

**Phase P.1: Cleanup (must be first)**
- Delete SpnPackageManager
- Delete RecentNotebooksManager  
- Remove all .spn references from codebase
- Update MainWindow to remove legacy code paths

**Phase P.2: NotebookLibrary (foundation)**
- Create NotebookLibrary class
- Implement recent/starred tracking with QSettings
- Implement thumbnail generation and caching
- Add search functionality

**Phase P.3: Launcher Modernization**
- Remove InkCanvas dependencies
- Implement new UI design (timeline, touch-first)
- Connect to NotebookLibrary
- Implement gestures and interactions

**Phase P.4: MainWindow Integration**
- Update "+" button behavior
- Reconnect keyboard shortcuts
- Clean up document opening flow
- Ensure Launcher ↔ MainWindow communication works

**Phase P.5: File Manager Integration (optional)**
- Windows context menu
- Linux .desktop file

---

## Open Questions Summary

Please answer these to proceed with implementation:

1. **Q1.1:** Should we add a `.snb_marker` file? Format version in document.json?
2. **Q1.3:** Legacy .spn handling: clean break or migration?
3. **Q2.1:** Launcher views - merge Open PDF/Open Notes? Return as button?
4. **Q2.2:** Timeline sorting - by modified or accessed?
5. **Q2.3:** Which gestures to implement?
6. **Q2.4:** What notebook types can be created?
7. **Q3.2:** + button behavior: dropdown, launcher, or quick action?
8. **Q3.3:** Launcher modal or coexisting?
9. **Q4.1:** Thumbnail resolution and regeneration timing?
10. **Q4.3:** Use NotebookLibrary class or merge into DocumentManager?

---

*Document created: 2026-01-13*
*Status: Awaiting answers*

