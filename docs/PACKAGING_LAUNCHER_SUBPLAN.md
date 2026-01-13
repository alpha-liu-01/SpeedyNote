# Packaging & Launcher Modernization - Implementation Plan

## Overview

This plan covers:
1. Cleaning up legacy packaging code (.spn format)
2. Creating NotebookLibrary for notebook tracking
3. Building a new touch-first Launcher from scratch
4. Integrating with MainWindow

**Design Document:** See `PACKAGING_LAUNCHER_QA.md` for all design decisions.

---

## Phase P.1: Cleanup (Must Be First)

Remove all legacy code before building new features.

### Task P.1.1: Add .snb_marker and format version
**Files:** `source/core/Document.cpp`, `source/core/Document.h`
**Changes:**
- Add `BUNDLE_FORMAT_VERSION = 1` static constant to `Document` class
- Write `.snb_marker` file on bundle save (empty file, just for identification)
- Add `"bundle_format_version": 1` to document.json manifest
- Add version check on load (warn if newer version)

**Implementation:**
- `Document.h`: Added `static constexpr int BUNDLE_FORMAT_VERSION = 1;` with version history documentation
- `Document::saveBundle()`: Added code to write `.snb_marker` file at bundle root
- `Document::toJson()`: Added `bundle_format_version` field to manifest
- `Document::loadBundle()`: Added check to warn if bundle version is newer than current

**Estimated:** ~30 LOC
**Status:** ✅ Complete

---

### Task P.1.2: Delete SpnPackageManager
**Files to delete:** `source/SpnPackageManager.h`, `source/SpnPackageManager.cpp`
**Files to update:** 
- `CMakeLists.txt` (remove from build)
- Any files that `#include "SpnPackageManager.h"`

**Estimated:** ~50 LOC changes
**Status:** ✅ Complete (User manually deleted files, build verified)

---

### Task P.1.3: Delete RecentNotebooksManager
**Files to delete:** `source/RecentNotebooksManager.h`, `source/RecentNotebooksManager.cpp`
**Files to update:**
- `CMakeLists.txt`
- `source/MainWindow.cpp` (remove usage)
- `source/LauncherWindow.cpp` (will be deleted anyway)

**Estimated:** ~30 LOC changes
**Status:** ✅ Complete (User manually deleted files, build verified)

---

### Task P.1.4: Delete PdfOpenDialog
**Files to delete:** `source/PdfOpenDialog.h`, `source/PdfOpenDialog.cpp`
**Files to update:**
- `CMakeLists.txt`
- Any files that reference PdfOpenDialog

**Note:** The PDF opening feature will be remade in Phase P.4 with a simpler flow.

**Estimated:** ~20 LOC changes
**Status:** ✅ Complete (User manually deleted files, build verified)

---

### Task P.1.5: Delete LauncherWindow
**Files to delete:** `source/LauncherWindow.h`, `source/LauncherWindow.cpp`
**Files to update:**
- `CMakeLists.txt`
- `source/Main.cpp` (update to show MainWindow directly for now)

**Extracted utilities:**
- `findExistingMainWindow()` → Moved to `MainWindow` as static method
- `preserveWindowState()` → Moved to `MainWindow` as public method
- `isDarkMode()` → Already exists in `MainWindow::isDarkMode()`
- `loadThemedIcon()` → Not needed; widgets use `setIconName()` which handles theming

**Estimated:** ~50 LOC changes
**Status:** 🔄 In Progress (utilities extracted, files ready for deletion)

---

### Task P.1.6: Remove remaining .spn references
**Search:** `grep -r "\.spn" source/` and `grep -r "spn" source/`
**Files likely affected:**
- `source/MainWindow.cpp` (openSpnPackage, etc.)
- `source/DocumentConverter.cpp` (if any)
- Any file dialogs with .spn filters

**Estimated:** ~100 LOC changes
**Status:** Pending

---

### Task P.1.7: Verify clean build
**Steps:**
1. `cmake --build build --clean-first`
2. Fix any remaining compile errors
3. Run application, verify basic functionality works

**Estimated:** Variable (debugging)
**Status:** Pending

---

## Phase P.2: NotebookLibrary

Create the new library management system.

### Task P.2.1: Create NotebookInfo struct
**File:** `source/core/NotebookLibrary.h`
**Contents:**
```cpp
struct NotebookInfo {
    QString bundlePath;
    QString name;
    QString documentId;      // From document.json
    QDateTime lastModified;
    QDateTime lastAccessed;
    bool isStarred = false;
    QString starredFolder;   // Empty = unfiled
    bool isPdfBased = false;
    bool isEdgeless = false;
    QString pdfFileName;     // For search, if PDF-based
};
```

**Estimated:** ~40 LOC
**Status:** Pending

---

### Task P.2.2: Create NotebookLibrary class skeleton
**File:** `source/core/NotebookLibrary.h`, `source/core/NotebookLibrary.cpp`
**Interface:**
```cpp
class NotebookLibrary : public QObject {
    Q_OBJECT
public:
    static NotebookLibrary* instance();
    
    // Recent management
    QList<NotebookInfo> recentNotebooks() const;
    void addToRecent(const QString& bundlePath);
    void removeFromRecent(const QString& bundlePath);
    void updateLastAccessed(const QString& bundlePath);
    
    // Starred management
    QList<NotebookInfo> starredNotebooks() const;
    void setStarred(const QString& bundlePath, bool starred);
    void setStarredFolder(const QString& bundlePath, const QString& folder);
    QStringList starredFolders() const;
    void createStarredFolder(const QString& name);
    void deleteStarredFolder(const QString& name);
    void reorderStarredFolder(const QString& name, int newIndex);
    
    // Search
    QList<NotebookInfo> search(const QString& query) const;
    
    // Thumbnails
    QString thumbnailPathFor(const QString& bundlePath) const;
    void saveThumbnail(const QString& bundlePath, const QPixmap& thumbnail);
    void invalidateThumbnail(const QString& bundlePath);
    
    // Persistence
    void save();
    void load();
    
signals:
    void libraryChanged();
    void thumbnailUpdated(const QString& bundlePath);
    
private:
    NotebookLibrary(QObject* parent = nullptr);
    QString m_libraryFilePath;
    QList<NotebookInfo> m_notebooks;
    QStringList m_starredFolderOrder;
};
```

**Estimated:** ~100 LOC header, ~50 LOC skeleton
**Status:** Pending

---

### Task P.2.3: Implement JSON persistence
**File:** `source/core/NotebookLibrary.cpp`
**Features:**
- Save to `QStandardPaths::AppDataLocation + "/library.json"`
- Load on construction
- Auto-save on changes (debounced)
- Validate paths exist on load (remove stale entries)

**JSON Structure:**
```json
{
  "version": 1,
  "notebooks": [
    {
      "path": "/path/to/notebook.snb",
      "name": "My Notebook",
      "documentId": "abc123",
      "lastModified": "2026-01-13T10:00:00Z",
      "lastAccessed": "2026-01-13T12:00:00Z",
      "isStarred": true,
      "starredFolder": "Work",
      "isPdfBased": false,
      "isEdgeless": true,
      "pdfFileName": ""
    }
  ],
  "starredFolders": ["Work", "Personal", "Archive"]
}
```

**Estimated:** ~150 LOC
**Status:** Pending

---

### Task P.2.4: Implement recent notebooks logic
**File:** `source/core/NotebookLibrary.cpp`
**Features:**
- `addToRecent()`: Add/update entry, set lastAccessed to now
- `removeFromRecent()`: Remove entry (keeps starred status)
- `recentNotebooks()`: Return sorted by lastAccessed (newest first)
- No limit on recent count (infinite history)
- Extract metadata from bundle on add (name, isPdfBased, isEdgeless)

**Estimated:** ~80 LOC
**Status:** Pending

---

### Task P.2.5: Implement starred notebooks logic
**File:** `source/core/NotebookLibrary.cpp`
**Features:**
- `setStarred()`: Toggle star status
- `setStarredFolder()`: Assign to folder (or empty for unfiled)
- `starredNotebooks()`: Return starred entries, grouped by folder
- Folder CRUD operations
- Reordering within folders (drag-drop support)

**Estimated:** ~100 LOC
**Status:** Pending

---

### Task P.2.6: Implement search
**File:** `source/core/NotebookLibrary.cpp`
**Features:**
- Case-insensitive search
- Match notebook name OR pdfFileName
- Return matching entries sorted by relevance (exact match first, then contains)

**Estimated:** ~50 LOC
**Status:** Pending

---

### Task P.2.7: Implement thumbnail caching
**File:** `source/core/NotebookLibrary.cpp`
**Features:**
- Cache location: `QStandardPaths::CacheLocation + "/thumbnails/"`
- Filename: `{documentId}.png`
- `saveThumbnail()`: Save pixmap to cache
- `thumbnailPathFor()`: Return cache path (or empty if not cached)
- `invalidateThumbnail()`: Delete cached file
- LRU cleanup: Delete oldest when cache exceeds 200 MiB

**Estimated:** ~80 LOC
**Status:** Pending

---

### Task P.2.8: Integrate with DocumentManager
**File:** `source/core/DocumentManager.cpp`
**Changes:**
- On document open: `NotebookLibrary::instance()->addToRecent(path)`
- On document save: `NotebookLibrary::instance()->updateLastAccessed(path)`
- On document close: Copy page-0 thumbnail to library cache

**Estimated:** ~30 LOC
**Status:** Pending

---

## Phase P.3: Launcher UI

Build the new launcher from scratch.

### Task P.3.1: Create Launcher class skeleton
**Files:** `source/ui/launcher/Launcher.h`, `source/ui/launcher/Launcher.cpp`
**Structure:**
```cpp
class Launcher : public QMainWindow {
    Q_OBJECT
public:
    explicit Launcher(QWidget* parent = nullptr);
    
    void showWithAnimation();
    void hideWithAnimation();
    
signals:
    void notebookSelected(const QString& bundlePath);
    void createNewEdgeless();
    void createNewPaged();
    void openPdfRequested();
    void openNotebookRequested();
    
private:
    void setupUi();
    void setupTimeline();
    void setupStarred();
    void setupSearch();
    void setupFAB();
    
    QStackedWidget* m_contentStack;
    // ... widgets
};
```

**Estimated:** ~100 LOC
**Status:** Pending

---

### Task P.3.2: Create navigation sidebar
**File:** `source/ui/launcher/Launcher.cpp`
**Features:**
- Vertical list of views: Timeline, Starred, Search
- "Return" button at top (if MainWindow exists)
- Touch-friendly sizing (60px height items)
- Highlight current view

**Estimated:** ~80 LOC
**Status:** Pending

---

### Task P.3.3: Create Timeline view
**File:** `source/ui/launcher/TimelineView.h`, `source/ui/launcher/TimelineView.cpp`
**Features:**
- Scrollable list grouped by time period
- Section headers: "Today", "Yesterday", "This Week", etc.
- Collapsible year groups for old entries
- NotebookCard widgets in grid layout
- Touch scrolling with QScroller

**Estimated:** ~250 LOC
**Status:** Pending

---

### Task P.3.4: Create NotebookCard widget
**File:** `source/ui/launcher/NotebookCard.h`, `source/ui/launcher/NotebookCard.cpp`
**Features:**
- Fixed width, fixed height
- Thumbnail display (C+D hybrid: top-crop for tall, letterbox for short)
- Name label (elided if too long)
- Type indicator icon (PDF/Edgeless/Paged)
- Star indicator
- Tap → open notebook
- Long-press → context menu

**Estimated:** ~150 LOC
**Status:** Pending

---

### Task P.3.5: Create Starred view
**File:** `source/ui/launcher/StarredView.h`, `source/ui/launcher/StarredView.cpp`
**Features:**
- iOS homescreen-style layout
- Folders as expandable groups
- Drag-drop reordering within folders
- Drag to folder to move
- Long-press folder to rename/delete
- "Unfiled" section for starred without folder

**Estimated:** ~300 LOC
**Status:** Pending

---

### Task P.3.6: Create Search view
**File:** `source/ui/launcher/SearchView.h`, `source/ui/launcher/SearchView.cpp`
**Features:**
- Search input at top
- Results grid below
- Real-time search as user types (debounced)
- "No results" message
- Keyboard-friendly (Enter to search, Escape to clear)

**Estimated:** ~150 LOC
**Status:** Pending

---

### Task P.3.7: Create FAB (Floating Action Button)
**File:** `source/ui/launcher/FloatingActionButton.h`, `source/ui/launcher/FloatingActionButton.cpp`
**Features:**
- Round button in bottom-right corner
- "+" icon, rotates to "×" when expanded
- Unfolds upward with 4 action buttons:
  1. New Edgeless
  2. New Paged
  3. From PDF
  4. Open .snb
- Icons with tooltips
- Animation on expand/collapse
- Click outside to collapse

**Estimated:** ~200 LOC
**Status:** Pending

---

### Task P.3.8: Implement gestures
**File:** Multiple launcher files
**Features:**
- Tap: Open notebook (already in NotebookCard)
- Long-press: Context menu (already in NotebookCard)
- Swipe left: Show delete button (with confirmation dialog)
- Swipe right: Toggle star

**Note:** Implement tap/long-press first. Swipes can be polish.

**Estimated:** ~100 LOC
**Status:** Pending

---

### Task P.3.9: Implement context menu
**File:** `source/ui/launcher/NotebookCard.cpp`
**Menu items:**
- Star / Unstar
- Move to folder... (submenu with folder list)
- Rename
- Duplicate
- Delete (with confirmation)
- Show in file manager

**Estimated:** ~80 LOC
**Status:** Pending

---

### Task P.3.10: Apply styling
**Files:** `resources/styles/launcher.qss`, `resources/styles/launcher_dark.qss`
**Features:**
- Consistent with existing SpeedyNote style
- Touch-friendly sizing
- Dark mode support
- FAB styling with shadows

**Estimated:** ~150 LOC
**Status:** Pending

---

## Phase P.4: MainWindow Integration

Connect the new launcher to the application.

### Task P.4.1: Update Main.cpp
**File:** `source/Main.cpp`
**Changes:**
- Show Launcher on startup (instead of MainWindow)
- Create MainWindow when notebook is opened
- Handle Launcher ↔ MainWindow transitions

**Estimated:** ~50 LOC
**Status:** Pending

---

### Task P.4.2: Add MainWindow interface methods
**File:** `source/MainWindow.h`, `source/MainWindow.cpp`
**New methods:**
```cpp
// Document operations
void openDocument(const QString& bundlePath);
void openPdf(const QString& pdfPath);  // Simplified flow
void createNewEdgeless();
void createNewPaged();

// Tab operations  
bool hasOpenDocuments() const;
bool switchToDocument(const QString& bundlePath);

// Window operations
void bringToFront();
static MainWindow* findExisting();
```

**Estimated:** ~100 LOC
**Status:** Pending

---

### Task P.4.3: Update "+" button dropdown
**File:** `source/MainWindow.cpp` or `source/ui/TabBar.cpp`
**Changes:**
- Replace current "+" behavior with dropdown menu:
  - New Edgeless Canvas (Ctrl+Shift+N)
  - New Paged Notebook (Ctrl+N)
  - ───────────────
  - Open PDF... (Ctrl+Shift+O)
  - Open Notebook... (Ctrl+Shift+L)

**Estimated:** ~50 LOC
**Status:** Pending

---

### Task P.4.4: Add Ctrl+H shortcut
**File:** `source/MainWindow.cpp`
**Changes:**
- Add `QShortcut` for Ctrl+H → toggle launcher
- Add `QShortcut` for Escape → go to launcher (when no dialogs open)
- Implement `toggleLauncher()` method

**Estimated:** ~30 LOC
**Status:** Pending

---

### Task P.4.5: Implement smooth transition
**File:** `source/ui/launcher/Launcher.cpp`, `source/MainWindow.cpp`
**Changes:**
- Fix window flash (proper show/hide ordering)
- Add fade animation (optional polish):
  - Launcher fades out (opacity 1→0)
  - MainWindow fades in (opacity 0→1)
  - Use QPropertyAnimation on windowOpacity

**Estimated:** ~50 LOC
**Status:** Pending

---

### Task P.4.6: Save thumbnail on document close
**File:** `source/MainWindow.cpp` or `source/core/DocumentManager.cpp`
**Changes:**
- On tab close or document save:
  - Get page-0 thumbnail from ThumbnailRenderer (if available)
  - Call `NotebookLibrary::instance()->saveThumbnail(path, pixmap)`

**Estimated:** ~30 LOC
**Status:** Pending

---

### Task P.4.7: Remove Ctrl+O shortcut
**File:** `source/MainWindow.cpp`
**Changes:**
- Remove the obsolete Ctrl+O shortcut
- Update help/documentation if any

**Estimated:** ~10 LOC
**Status:** Pending

---

## Phase P.5: File Manager Integration (Optional)

Low priority, can be done later.

### Task P.5.1: Windows context menu
**Files:** `installer/windows_context_menu.reg` or installer script
**Features:**
- Add "Open in SpeedyNote" to folder context menu
- Filter in app: only accept .snb folders

**Estimated:** ~20 LOC + installer changes
**Status:** Optional

---

### Task P.5.2: Linux .desktop file
**Files:** `installer/speedynote.desktop`
**Features:**
- MimeType for directory
- Actions for opening folders

**Estimated:** ~20 LOC
**Status:** Optional

---

## Summary Table

| Phase | Task | Description | Est. LOC | Status |
|-------|------|-------------|----------|--------|
| P.1 | P.1.1 | Add .snb_marker + version | 30 | ✅ Complete |
| P.1 | P.1.2 | Delete SpnPackageManager | 50 | Pending |
| P.1 | P.1.3 | Delete RecentNotebooksManager | 30 | Pending |
| P.1 | P.1.4 | Delete PdfOpenDialog | 20 | Pending |
| P.1 | P.1.5 | Delete LauncherWindow | 50 | Pending |
| P.1 | P.1.6 | Remove .spn references | 100 | Pending |
| P.1 | P.1.7 | Verify clean build | - | Pending |
| P.2 | P.2.1 | NotebookInfo struct | 40 | Pending |
| P.2 | P.2.2 | NotebookLibrary skeleton | 150 | Pending |
| P.2 | P.2.3 | JSON persistence | 150 | Pending |
| P.2 | P.2.4 | Recent notebooks logic | 80 | Pending |
| P.2 | P.2.5 | Starred notebooks logic | 100 | Pending |
| P.2 | P.2.6 | Search | 50 | Pending |
| P.2 | P.2.7 | Thumbnail caching | 80 | Pending |
| P.2 | P.2.8 | DocumentManager integration | 30 | Pending |
| P.3 | P.3.1 | Launcher skeleton | 100 | Pending |
| P.3 | P.3.2 | Navigation sidebar | 80 | Pending |
| P.3 | P.3.3 | Timeline view | 250 | Pending |
| P.3 | P.3.4 | NotebookCard widget | 150 | Pending |
| P.3 | P.3.5 | Starred view | 300 | Pending |
| P.3 | P.3.6 | Search view | 150 | Pending |
| P.3 | P.3.7 | FAB | 200 | Pending |
| P.3 | P.3.8 | Gestures | 100 | Pending |
| P.3 | P.3.9 | Context menu | 80 | Pending |
| P.3 | P.3.10 | Styling | 150 | Pending |
| P.4 | P.4.1 | Update Main.cpp | 50 | Pending |
| P.4 | P.4.2 | MainWindow interface | 100 | Pending |
| P.4 | P.4.3 | + button dropdown | 50 | Pending |
| P.4 | P.4.4 | Ctrl+H shortcut | 30 | Pending |
| P.4 | P.4.5 | Smooth transition | 50 | Pending |
| P.4 | P.4.6 | Thumbnail on close | 30 | Pending |
| P.4 | P.4.7 | Remove Ctrl+O | 10 | Pending |
| P.5 | P.5.1 | Windows context menu | 20 | Optional |
| P.5 | P.5.2 | Linux .desktop | 20 | Optional |

**Total Estimated:** ~2,700 LOC (excluding optional P.5)

---

## Implementation Order

```
Phase P.1: Cleanup
├── P.1.1: .snb_marker + version
├── P.1.2: Delete SpnPackageManager
├── P.1.3: Delete RecentNotebooksManager
├── P.1.4: Delete PdfOpenDialog
├── P.1.5: Delete LauncherWindow
├── P.1.6: Remove .spn references
└── P.1.7: Verify clean build

Phase P.2: NotebookLibrary
├── P.2.1: NotebookInfo struct
├── P.2.2: NotebookLibrary skeleton
├── P.2.3: JSON persistence
├── P.2.4: Recent notebooks logic
├── P.2.5: Starred notebooks logic
├── P.2.6: Search
├── P.2.7: Thumbnail caching
└── P.2.8: DocumentManager integration

Phase P.3: Launcher UI
├── P.3.1: Launcher skeleton
├── P.3.2: Navigation sidebar
├── P.3.3: Timeline view
├── P.3.4: NotebookCard widget
├── P.3.5: Starred view
├── P.3.6: Search view
├── P.3.7: FAB
├── P.3.8: Gestures (tap/long-press first)
├── P.3.9: Context menu
└── P.3.10: Styling

Phase P.4: MainWindow Integration
├── P.4.1: Update Main.cpp
├── P.4.2: MainWindow interface
├── P.4.3: + button dropdown
├── P.4.4: Ctrl+H shortcut
├── P.4.5: Smooth transition
├── P.4.6: Thumbnail on close
└── P.4.7: Remove Ctrl+O

Phase P.5: File Manager Integration (Optional)
├── P.5.1: Windows context menu
└── P.5.2: Linux .desktop
```

---

## Success Criteria

### Phase P.1 Complete When:
- [ ] All legacy files deleted
- [ ] No .spn references remain
- [ ] App builds and runs
- [ ] Can open existing .snb documents via MainWindow

### Phase P.2 Complete When:
- [ ] NotebookLibrary tracks recent/starred
- [ ] Data persists across app restarts
- [ ] Thumbnails cached to disk
- [ ] Search returns correct results

### Phase P.3 Complete When:
- [ ] Launcher displays timeline of notebooks
- [ ] Starred view with folders works
- [ ] FAB creates new notebooks
- [ ] Touch gestures work
- [ ] Looks good in light and dark mode

### Phase P.4 Complete When:
- [ ] Launcher ↔ MainWindow transition is smooth
- [ ] All shortcuts work
- [ ] + button dropdown works
- [ ] Thumbnails saved on document close

### Overall Complete When:
- [ ] User can manage all notebooks from Launcher
- [ ] No legacy code remains
- [ ] Touch-first design works well
- [ ] All design decisions from QA doc implemented

---

*Document created: 2026-01-13*
*Based on: PACKAGING_LAUNCHER_QA.md*

