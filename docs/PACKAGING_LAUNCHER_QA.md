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
- Should we add a `.snb_marker` file to help identify bundles?  Yes. 
- Should `document.json` include a format version for future compatibility?  Yes. 
- Any other metadata to add to `document.json`? For now, no. 

**Answer:** ✅ Yes to .snb_marker and format version. No additional metadata for now.

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
Since we are going to keep a much longer history (instead of only 25~50 before), I'm not sure if Option 1 is still good enough. 

**Claude's Response:**
You're right to be concerned. For a longer history (hundreds of entries), I recommend **Option 2: JSON file**.

Here's why:
- QSettings on Linux stores in `~/.config/SpeedyNote/SpeedyNote.conf` (INI format) - gets unwieldy with large lists
- QSettings on Windows uses Registry - not ideal for large structured data
- JSON file in `QStandardPaths::AppDataLocation` gives you:
  - Human-readable for debugging
  - Easy to structure (nested objects for notebooks, folders, etc.)
  - Can store complex data like starred folder hierarchies
  - Easy backup/sync (single file)

**Proposed structure:**
```json
{
  "version": 1,
  "recent": [
    {"path": "/path/to/notebook.snb", "lastAccessed": "2026-01-13T10:30:00Z", "lastModified": "2026-01-12T15:00:00Z"},
    ...
  ],
  "starred": {
    "folders": [
      {"name": "Work", "items": ["path1.snb", "path2.snb"]},
      {"name": "Personal", "items": ["path3.snb"]}
    ],
    "unfiled": ["path4.snb", "path5.snb"]
  },
  "thumbnailCache": "~/.cache/SpeedyNote/thumbnails/"
}
```

**Final Answer:** JSON file in app data folder.

---

### Q1.3: What to do with legacy .spn files?

**Options:**
1. **No migration** - Clean break, users must re-annotate PDFs
2. **One-time conversion** - Offer to convert .spn to .snb on first open
3. **Read-only support** - Can open .spn but saves as .snb

**Recommendation:** Option 1 (clean break) since the architectures are fundamentally incompatible.

**Answer:** [PENDING]
No migration. InkCanvas is GONE by now. 

**Answer:** ✅ Clean break. No migration support.

---

### Q1.4: Files to delete during cleanup

| File | Reason |
|------|--------|
| `SpnPackageManager.h/cpp` | Obsolete .spn format |
| `RecentNotebooksManager.h/cpp` | Will rebuild from scratch |
| Any .spn references in other files | Legacy cleanup |

**Answer:** [PENDING - confirm list]
PdfOpenDialog.h / PdfOpenDialog.cpp. This is for directly opening a PDF with SpeedyNote. It also has a lot to do with the spn packaging. I wonder if this feature will be remade and added back. 

**Claude's Response:**
Looking at `PdfOpenDialog.cpp`, it handles:
1. Choosing where to save annotations (create new .spn or link to existing)
2. PDF relinking when file moves
3. Notebook creation with PDF background

**Recommendation:**
- **Delete PdfOpenDialog** - it's tightly coupled to .spn format
- **Remake the feature** in Phase P.3/P.4 with simpler flow:
  1. User opens PDF → Creates new .snb bundle with PDF reference
  2. Bundle stores PDF path + hash (already implemented in PDF relink feature)
  3. No complex "link to existing notebook" flow needed initially

The new flow would be:
1. Click "From PDF" in launcher (or Ctrl+Shift+O)
2. File picker opens
3. New .snb bundle created automatically in temp location
4. User can "Save As" to choose permanent location

**Files to delete (confirmed):**
| File | Reason |
|------|--------|
| `SpnPackageManager.h/cpp` | Obsolete .spn format |
| `RecentNotebooksManager.h/cpp` | Rebuilding from scratch |
| `PdfOpenDialog.h/cpp` | Coupled to .spn, will remake |
| `LauncherWindow.h/cpp` | Will rebuild from scratch (see Q below) |

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
| **Starred** | Pinned/favorite notebooks | ✅ Grid of cards |  // Also, I think the starred notebooks view needs a drag and drop and store in folder feature. 
| **New** | Create new notebook (edgeless/paged/from PDF) | ✅ Large buttons |
| **Search** | Find notebooks by name/content | ✅ On-screen keyboard |

**Questions:**
- Should "Open PDF" and "Open Notes" be merged into a single "Import/Open" action?
- Should "Return" be a button instead of a tab (since it's an action, not a view)?
- Is the timeline view sorted by last modified or last accessed?

**Answer:** [PENDING]
A1: No. By design, open PDF should be of the same group as "add new tab / add new edgeless tab", which means the user is annotating this PDF for the first time. Open notes is meant for opening notes that are already saved. 
A2: Yes. The old design is inferior. 
A3: Maybe both?

**Answer:** ✅ Keep Open PDF separate from Open Notes. Return as button. Show both modified and accessed (grouped by accessed, sorted by modified within groups).

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
A1: No. Designing a "recycle bin" without actually removing it doesn't sound like a good idea... 
A2: Yes. 
A3: I haven't thought aboout this before. Can you please give me some suggestions? 

**Claude's Response to A3 (handling very old notebooks):**

Here are some options:

**Option A: No archiving (simplest)**
- Keep all notebooks in timeline forever
- Older months collapse into year groups (e.g., "2024", "2023")
- User can manually delete if they want
- **Pros:** Simple, no data loss risk
- **Cons:** Timeline gets long over years

**Option B: Soft archive after X months**
- After 6-12 months of no access, notebooks move to "Archived" section
- Still searchable, just not in main timeline
- Opening an archived notebook moves it back to timeline
- **Pros:** Cleaner timeline
- **Cons:** Extra UI complexity

**Option C: Limit timeline entries, keep starred forever**
- Timeline shows last N notebooks (e.g., 100-200)
- Starred notebooks never expire
- Older notebooks still accessible via Search
- **Pros:** Bounded memory/storage for timeline data
- **Cons:** User might lose track of old unstarred notebooks

**Recommendation:** Option A (no archiving) with smart grouping:
```
Timeline:
├── Today (3)
├── Yesterday (1)
├── This Week (5)
├── This Month (12)
├── December 2025 (8)
├── November 2025 (3)
├── ▼ 2024 (collapsed, 45 notebooks)
│   ├── December (5)
│   └── ...
└── ▼ 2023 (collapsed, 20 notebooks)
```

This keeps it simple while staying manageable for long-term users.

**Final Answer:** Option A with collapsible year groups.

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
| Tap notebook | Open notebook | Yes
| Long-press notebook | Show context menu (star, delete, duplicate, rename) | Yes
| Swipe left on notebook | Quick delete (with undo) | Yes
| Swipe right on notebook | Quick star/unstar | Yes
| Pull down | Refresh list | (I think the list is always fresh when we enter the launcher? Correct me if I'm wrong. )
| Pinch | Zoom grid (more/fewer columns) | I don't think this is necessary? 

**Questions:**
- Which gestures should be implemented?
- Should swipe-to-delete require confirmation or just show undo toast? There should be a confirmation. Deleting a whole notebook is not a quick decision. 
- How to handle pinch zoom on notebooks grid? I don't see the point of it... If we are going to support it, do we rerender the thumbnail again at higher resolution? This gets complex very quickly. 

**Claude's Response:**

**Pull-down refresh:** You're correct - the list should be fresh when entering the launcher. The only case where refresh might be useful is if the user leaves the launcher open while files change externally (e.g., sync from cloud). We can skip this for now and add later if needed.

**Pinch zoom:** Agreed, this adds complexity without much benefit. Skip it.

**Final gesture list:**
| Gesture | Action | Priority |
|---------|--------|----------|
| Tap | Open notebook | Must have |
| Long-press | Context menu | Must have |
| Swipe left | Delete (with confirmation dialog) | Nice to have |
| Swipe right | Star/unstar | Nice to have |

**Implementation note:** Swipe gestures on touch require `QScroller` and careful handling. We can implement tap/long-press first, then add swipes in a polish phase.

**Answer:** Implement tap + long-press first. Swipes can be Phase P.3.5 (polish).

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
There is actually a default background style shared between edgeless and paged notebooks. We can follow this (and it will become customizable in the control panel that we are going to reconnect later). 
So blank edgeless, pdf annotation, paged notebook. Background style override sounds like a good idea, but we may defer that after we reconnect/redesign the control panel.
Also, the UI design should be more like a round plus button (with a similar style as the ones on the ) at the bottom right, click on it, and it will unfold 4 round buttons (for each type of notebook, and opening spn from file manager). This design is inspired from Squid, a fairly popular note-taking application. 
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
- Which shortcuts should open the Launcher instead of acting directly? None
- Should `Ctrl+O` show a unified "Open" dialog or go to Launcher? ctrl+o is OBSOLETE. This was for the single file json notebook during the development stage. This type of notebook is no longer supported. We are dropping support for this type in the final product, since pages store differently now. `ctrl+shift+l` somehow becomes the only way of loading an existing notebook. `ctrl+shift+o` opens a pdf in the temporary tab, but it shouldn't be allowed when a pdf is already loaded. You may need to check this if it's not handled correctly. 
- Should there be a shortcut to toggle Launcher visibility? Yes. 

**Answer:** [PENDING]
All of the shortcuts should ALL act directly without going to the launcher. The launcher provides extra UI for some of these features, like creating/opening different types of tabs/notebooks.  

**Answer:** ✅ Shortcuts act directly. Remove Ctrl+O. Add launcher toggle shortcut.

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
Clicking the add button and automatically brings to launcher and automatically unfolds the add button in the launcher on the bottom right sounds really strange to me. So Option A. But the return to launcher is the very first button on the navigation bar, so don't include that in the dropdown menu again. 

**Answer:** ✅ Option A (dropdown menu) without "Go to Launcher" entry.

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
Option 1 makes the most sense. But I don't know if the transition can be smoothed out with some animations. The previous switching process looks a bit crude (with a fairly significant screen flash). 

**Answer:** ✅ Option 1 (modal). Will add smooth transition animation (see Q6.2).

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
We already have this kind of rendered thumbnails, which are on the page panel. I wonder if we can just select one of the page panel renderings and put it on the launcher window... Just keep the thumbnail size consistent with the ones on the page panel (which have a fixed width and an adaptive height, and guaranteed to be rendered 1:1 with the correct DPI). 

Regeneate on close makes sense. But I didn't quite get your idea of "regenerating on demand". Can you please explain how this would work? 

Each rendering is about ~500 KiB. For the cached renders in RAM, it would be a ~10-20 pages max. For persistent storage, several hundred should be good. Please correct me if I was wrong. 

**Claude's Response:**

**Reusing PagePanel thumbnails:** Excellent idea! The `ThumbnailRenderer` class already handles this. We can:
1. On document close/save → Copy `ThumbnailRenderer`'s page 0 result to persistent cache
2. Store in `~/.cache/SpeedyNote/thumbnails/{document_id}.png`
3. Launcher loads from persistent cache, never re-renders

**"On demand" explanation:** I meant if a cached thumbnail is missing (e.g., first launch, cache cleared), generate it only when the notebook card becomes visible in the launcher's scroll view. This prevents regenerating 200 thumbnails at once. But since we're copying from PagePanel on close, this should rarely happen.

**Cache size math:**
- You're right: ~500 KiB per thumbnail (at PagePanel resolution)
- RAM: 10-20 thumbnails = 5-10 MiB (managed by launcher's visible area)
- Disk: 500 thumbnails = ~250 MiB max
- We can add LRU eviction: delete oldest when cache exceeds 200 MiB

**Final Answer:**
1. Copy PagePanel page-0 thumbnail to persistent cache on document close
2. Launcher loads from disk cache (no re-rendering needed)
3. Disk cache limit: 200 MiB with LRU eviction
4. RAM: Only load visible thumbnails (handled by scroll view)

But this brings another problem... The thumbnails have consistent width, but not height. Do we keep the inconsistent height in the launcher, or we just cut them to be the same height? Zooming in/out is not an option, because this will break the pixel to pixel display clarity. 

**Claude's Response:**

Good catch! Here are the options:

**Option A: Variable height cards (Pinterest-style "masonry" layout)**
```
┌──────┐ ┌──────┐ ┌──────┐
│      │ │      │ │      │
│ tall │ │short │ │      │
│      │ │      │ │medium│
│      │ └──────┘ │      │
└──────┘ ┌──────┐ └──────┘
         │      │
         │ tall │
         ...
```
- Pros: No cropping, full preview visible, pixel-perfect
- Cons: Irregular layout, harder to scan visually, complex layout code

**Option B: Fixed height with center-crop**
```
┌──────┐ ┌──────┐ ┌──────┐
│ crop │ │      │ │ crop │
│center│ │ fits │ │center│
│      │ │      │ │      │
└──────┘ └──────┘ └──────┘
```
- Pros: Clean grid, easy to scan
- Cons: Tall pages lose top/bottom, but center content usually most relevant

**Option C: Fixed height with letterboxing**
```
┌──────┐ ┌──────┐ ┌──────┐
│▓▓▓▓▓▓│ │      │ │▓▓▓▓▓▓│
│ img  │ │ img  │ │ img  │
│▓▓▓▓▓▓│ │      │ │▓▓▓▓▓▓│
└──────┘ └──────┘ └──────┘
```
- Pros: No cropping, clean grid
- Cons: Wasted space, looks uneven

**Option D: Fixed height with top-align crop**
```
┌──────┐ ┌──────┐ ┌──────┐
│ top  │ │      │ │ top  │
│ kept │ │ fits │ │ kept │
│cropped│ │      │ │cropped│
└──────┘ └──────┘ └──────┘
```
- Pros: Title/header usually at top, so most useful content preserved
- Cons: Still loses bottom content

**Recommendation:** Option D (top-align crop) with a fixed card height.

Rationale:
- Most documents have titles/headers at top
- Grid layout is cleaner for visual scanning
- The crop happens at display time (CSS/Qt), not on the cached image
- Cached image stays full resolution, only display crops

**Implementation:**
```cpp
// In launcher card painting
QRect cardRect = ...; // Fixed height card
QPixmap thumb = loadThumbnail(path);

// Calculate source rect (top portion of thumbnail)
int sourceHeight = qMin(thumb.height(), cardRect.height() * thumb.width() / cardRect.width());
QRect sourceRect(0, 0, thumb.width(), sourceHeight);

// Draw cropped portion
painter.drawPixmap(cardRect, thumb, sourceRect);
```

**Answer:** ✅ Combination of C+D:
- Pages taller than card: top-align crop (keep top, crop bottom)
- Pages shorter than card: letterbox (show full, add bars)
- Standard aspect ratios: no cropping, no bars

This minimizes visual artifacts while keeping a clean grid layout.

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
I agree with your recommendation

**Answer:** ✅ Clean interface with direct method calls.

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
I agree with Option 2. 

**Answer:** ✅ New NotebookLibrary class.

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

### Q4.4: Should LauncherWindow be rebuilt from scratch?

**User's Question:** I'm not sure if the original LauncherWindow still has any use at all now... Is it a better idea to just remove it and start from scratch, or there are enough useful code for us to reuse some of it?

**Claude's Analysis:**

Looking at the current `LauncherWindow.cpp` (1447 lines), here's what's reusable vs obsolete:

| Component | Status | Notes |
|-----------|--------|-------|
| `setupUi()` | ❌ Obsolete | Sidebar tabs design is changing |
| `QSplitter` layout | ❌ Obsolete | New design is different (FAB, timeline) |
| `populateRecentGrid()` | ❌ Obsolete | Uses old RecentNotebooksManager |
| `createNotebookButton()` | ⚠️ Partial | Thumbnail display logic could be adapted |
| `cropBlankMargins()` | ✅ Reusable | Useful image processing utility |
| `openNotebook()` | ❌ Obsolete | Uses InkCanvas |
| `findExistingMainWindow()` | ✅ Reusable | Good pattern for window management |
| `preserveWindowState()` | ✅ Reusable | Nice UX feature |
| Dark mode detection | ✅ Reusable | `isDarkMode()`, `loadThemedIcon()` |
| Touch scrolling setup | ✅ Reusable | `QScroller::grabGesture()` pattern |
| Pixmap cache | ⚠️ Partial | Pattern is good, implementation needs update |

**Recommendation: Rebuild from scratch**, but keep these utilities:
1. `findExistingMainWindow()` 
2. `preserveWindowState()`
3. `isDarkMode()` / `loadThemedIcon()`
4. `cropBlankMargins()`
5. QScroller setup pattern

**Why rebuild:**
- 80%+ of the code is obsolete (InkCanvas, .spn, old design)
- New design is fundamentally different (FAB, timeline, folders)
- Cleaner to start fresh than untangle dependencies
- Can reference old code for patterns without inheriting debt

**Practical approach:**
1. Create `source/ui/launcher/` folder for new launcher
2. Create `NewLauncher.h/cpp` (or just `Launcher.h/cpp`)
3. Delete old `LauncherWindow.h/cpp` after new one works
4. Extract reusable utilities into separate files if needed

**Final Answer:** Rebuild from scratch. Delete old LauncherWindow.

---

---

## Section 6: Follow-up Questions

### Q6.1: Starred notebooks folder structure

You mentioned "drag and drop and store in folder feature" for starred notebooks.

**Questions:**
1. Should folders be nestable (folders within folders)?
2. Can a notebook be in multiple folders, or only one?
3. Should there be a limit on folder depth?
4. Should folders have icons/colors?

**Recommendation:**
- Single level of folders (no nesting) - simpler UI
- Notebook can only be in one folder OR "unfiled"
- Folders can have custom names, maybe colors
- Drag-and-drop to reorder within folder

**Answer:** [PENDING]
I agree with your design. You may treat the starred view (with the folders) like a pre-iOS 6 iPhone homescreen, and the folder works in a similar way. 

**Answer:** ✅ Single-level folders, iOS-style homescreen layout. Notebooks can be in one folder or unfiled.

---

### Q6.2: Launcher ↔ MainWindow transition animation

You mentioned the transition looks "crude with a fairly significant screen flash."

**Options:**
1. **Fade crossfade** - Launcher fades out, MainWindow fades in
2. **Slide transition** - Launcher slides out, MainWindow slides in
3. **Morph transition** - Clicked notebook card expands into MainWindow
4. **No animation, just fix flashing** - Ensure windows are properly sized/positioned before showing

**Implementation complexity:**
- Option 4 is simplest (just ensure proper show/hide ordering)
- Options 1-2 need QPropertyAnimation on window opacity/position
- Option 3 is complex (need to track card position, animate transform)

**Recommendation:** Start with Option 4 (fix flashing), add Option 1 (fade) as polish.

**Answer:** ✅ Start with fixing flash, add fade crossfade as polish.

---

### Q6.3: FAB (Floating Action Button) design details

You want a Squid-style FAB that unfolds. Questions:

1. **Position:** Bottom-right corner, or should it be configurable? Make it fixed for now. 
2. **Unfold direction:** Up, or fan out in arc? Up. 
3. **Labels:** Should unfolded buttons have text labels, or just icons with tooltips? Icons with tooltips. 
4. **Animation:** Rotation of main + button while unfolding? This would be nice to have, but not a must. 

**Proposed design:**
```
                              ┌─────────────┐
                              │ Open .snb   │ ←──── Unfolds upward
                              └─────────────┘
                              ┌─────────────┐
                              │  From PDF   │
                              └─────────────┘
                              ┌─────────────┐
                              │   Paged     │
                              └─────────────┘
                              ┌─────────────┐
                              │  Edgeless   │
                              └─────────────┘
                                   (+)       ←──── Main FAB (rotates to ×)
```

**Answer:** ✅ Fixed bottom-right, unfolds upward, icons with tooltips, rotation nice-to-have.

---

### Q6.4: Search functionality scope

What should search cover?

| Scope | Difficulty | Notes |
|-------|------------|-------|
| Notebook names only | Easy | String matching on NotebookInfo.name |
| + PDF filenames | Easy | Store original PDF name in metadata |
| + Markdown note content | Medium | Need to scan .md files in bundle |
| + Full text (all annotations) | Hard | Need indexing, significant I/O |

**Recommendation:** Start with notebook names + PDF filenames. Add markdown search later if needed.

**Answer:** ✅ Notebook names + PDF filenames. Markdown search can be added later.

---

### Q6.5: Keyboard shortcut for Launcher toggle

You confirmed there should be a shortcut. What should it be?

**Options:**
| Shortcut | Notes |
|----------|-------|
| `Ctrl+L` | Conflicts with some text editors, but available in SpeedyNote |
| `Ctrl+H` | "Home" connotation, common for launcher |
| `Ctrl+Shift+H` | Less likely to conflict |
| `F1` | Traditionally help, but could repurpose |
| `Escape` | When in MainWindow, could go to launcher? |

**Recommendation:** `Ctrl+H` for "Home" (launcher), with `Escape` as secondary when no dialogs are open.

**Answer:** ✅ Ctrl+H for launcher toggle, Escape as secondary.

---

## Decisions Summary

| Question | Decision | Status |
|----------|----------|--------|
| Q1.1: .snb_marker + version | Yes to both | ✅ Decided |
| Q1.2: App metadata storage | JSON file | ✅ Decided |
| Q1.3: Legacy .spn | Clean break, no migration | ✅ Decided |
| Q1.4: Files to delete | SpnPackageManager, RecentNotebooksManager, PdfOpenDialog, LauncherWindow | ✅ Decided |
| Q2.1: Views | Timeline + Starred + Search + FAB | ✅ Decided |
| Q2.2: Timeline grouping | Collapsible by time period + year | ✅ Decided |
| Q2.3: Gestures | Tap + long-press first, swipes later | ✅ Decided |
| Q2.4: Notebook types | Edgeless, Paged, From PDF, Open .snb | ✅ Decided |
| Q3.1: Shortcuts | Keep direct action, add Ctrl+O removal | ✅ Decided |
| Q3.2: + button | Dropdown menu (Option A) | ✅ Decided |
| Q3.3: Modal launcher | Yes, with smooth transition | ✅ Decided |
| Q4.1: Thumbnails | Reuse PagePanel, copy on close | ✅ Decided |
| Q4.2: Launcher↔MainWindow | Direct method calls | ✅ Decided |
| Q4.3: NotebookLibrary | Yes, new class | ✅ Decided |
| Q4.4: LauncherWindow | Rebuild from scratch | ✅ Decided |
| Q4.1b: Thumbnail height | C+D hybrid (crop tall, letterbox short) | ✅ Decided |
| Q6.1: Starred folders | iOS-style homescreen, single-level | ✅ Decided |
| Q6.2: Transition animation | Fix flash first, fade later | ✅ Decided |
| Q6.3: FAB design | Bottom-right, unfold up, icons+tooltips | ✅ Decided |
| Q6.4: Search scope | Names + PDF filenames | ✅ Decided |
| Q6.5: Launcher shortcut | Ctrl+H, Escape secondary | ✅ Decided |

---

*Document created: 2026-01-13*
*Last updated: 2026-01-13*
*Status: Mostly decided, a few follow-ups pending*

