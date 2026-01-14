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
- Add `FORMAT_VERSION = 1` constant
- Write `.snb_marker` file on bundle save (empty file, just for identification)
- Add `"format_version": 1` to document.json
- Add version check on load (warn if newer version)

**Estimated:** ~30 LOC
**Status:** Pending

---

### Task P.1.2: Delete SpnPackageManager
**Files to delete:** `source/SpnPackageManager.h`, `source/SpnPackageManager.cpp`
**Files to update:** 
- `CMakeLists.txt` (remove from build)
- Any files that `#include "SpnPackageManager.h"`

**Steps:**
1. `grep -r "SpnPackageManager" source/` to find all references
2. Remove/comment out usages
3. Delete files
4. Update CMakeLists.txt
5. Verify build

**Estimated:** ~50 LOC changes
**Status:** Pending

---

### Task P.1.3: Delete RecentNotebooksManager
**Files to delete:** `source/RecentNotebooksManager.h`, `source/RecentNotebooksManager.cpp`
**Files to update:**
- `CMakeLists.txt`
- `source/MainWindow.cpp` (remove usage)
- `source/LauncherWindow.cpp` (will be deleted anyway)

**Steps:**
1. `grep -r "RecentNotebooksManager" source/` to find all references
2. Remove/comment out usages (temporary - NotebookLibrary will replace)
3. Delete files
4. Update CMakeLists.txt
5. Verify build

**Estimated:** ~30 LOC changes
**Status:** Pending

---

### Task P.1.4: Delete PdfOpenDialog
**Files to delete:** `source/PdfOpenDialog.h`, `source/PdfOpenDialog.cpp`
**Files to update:**
- `CMakeLists.txt`
- Any files that reference PdfOpenDialog

**Note:** The PDF opening feature will be remade in Phase P.4 with a simpler flow.

**Estimated:** ~20 LOC changes
**Status:** Pending

---

### Task P.1.5: Delete LauncherWindow
**Files to delete:** `source/LauncherWindow.h`, `source/LauncherWindow.cpp`
**Files to update:**
- `CMakeLists.txt`
- `source/Main.cpp` (update to show MainWindow directly for now)

**Note:** Extract reusable utilities first:
- `findExistingMainWindow()` → move to MainWindow as static
- `preserveWindowState()` → move to utility file or MainWindow
- `isDarkMode()` / `loadThemedIcon()` → already exists elsewhere or move to utility

**Estimated:** ~50 LOC changes
**Status:** 🔄 In Progress (utilities extracted, files ready for deletion)

---

### Task P.1.6: Remove remaining .spn references
**Search:** `grep -r "\.spn" source/` and `grep -r "spn" source/`

**Changes made:**
- `source/MainWindow.h`: Updated comment (line 167) from `.spn` to PDF description
- `source/core/Document.h`: Updated docstring from `.spn` to `.snb`
- `source/core/DocumentManager.h/cpp`: Updated comments removing SpnPackageManager references
- `source/Main.cpp`: All `.spn` references are inside commented-out code blocks (safe)
- `source/MainWindow.cpp`: All `.spn` references are removal comments (safe)
- `source/ControlPanelDialog.cpp`: **Disconnected - will be remade** (not modified further)

**Remaining (in disconnected/commented code):**
- `Main.cpp`: Commented block (lines 417-449) with old LauncherWindow code
- `ControlPanelDialog.cpp`: Disconnected, queued for remake

**Estimated:** ~100 LOC changes
**Status:** ✅ Complete (active code cleaned, disconnected code left for remake)

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

**Implementation:**
- Created `NotebookInfo` struct with all planned fields
- Added `isValid()` helper method to check if bundlePath is set
- Added `displayName()` helper method that falls back to folder name if `name` is empty
- Includes proper documentation comments

**Estimated:** ~40 LOC
**Status:** ✅ Complete

---

### Task P.2.2: Create NotebookLibrary class skeleton
**Files:** `source/core/NotebookLibrary.h`, `source/core/NotebookLibrary.cpp`

**Implementation:**
- Added `NotebookLibrary` class as singleton (`instance()` pattern)
- Grouped methods: Recent, Starred, Search, Thumbnails, Persistence
- Added private `findNotebook()` helper for lookups
- Constructor sets up paths using `QStandardPaths::AppDataLocation`
- All method stubs created with `TODO` comments for later phases
- Added to `CMakeLists.txt` under `CORE_SOURCES`

**Private members:**
- `m_libraryFilePath`: Path to `notebook_library.json`
- `m_thumbnailCachePath`: Path to `thumbnail_cache/` directory
- `m_notebooks`: List of all tracked notebooks
- `m_starredFolderOrder`: Ordered list of folder names

**Estimated:** ~100 LOC header, ~50 LOC skeleton
**Status:** ✅ Complete

---

### Task P.2.3: Implement JSON persistence
**File:** `source/core/NotebookLibrary.cpp`

**Implementation:**
- `save()`: Serializes `m_notebooks` and `m_starredFolderOrder` to JSON
- `load()`: Parses JSON and validates each notebook path exists
  - Checks for `.snb_marker` or `document.json` to confirm valid bundle
  - Removes stale entries automatically
- `scheduleSave()`: Starts/restarts debounce timer (1 second)
- `markDirty()`: Emits `libraryChanged()` and schedules save
- Constructor calls `load()` and sets up timer connection
- Version field for future compatibility

**Added to header:**
- `QTimer m_saveTimer` for debounced auto-save
- `scheduleSave()` and `markDirty()` private methods
- `SAVE_DEBOUNCE_MS = 1000` and `LIBRARY_VERSION = 1` constants

**Estimated:** ~150 LOC
**Status:** ✅ Complete

---

### Task P.2.4: Implement recent notebooks logic
**File:** `source/core/NotebookLibrary.cpp`

**Implementation:**
- `recentNotebooks()`: Returns copy sorted by `lastAccessed` descending using `std::sort`
- `addToRecent()`:
  - If exists: updates `lastAccessed` and refreshes `lastModified` from filesystem
  - If new: reads `document.json` to extract `name`, `notebook_id`, `mode`, `pdf_path`
  - Sets `isEdgeless` based on mode == "edgeless"
  - Sets `isPdfBased` and `pdfFileName` if pdf_path is present
  - Gets `lastModified` from filesystem (more accurate than JSON)
- `removeFromRecent()`: Removes entry by path
- `updateLastAccessed()`: Updates timestamp only

**Added:** `#include <algorithm>` for std::sort

**Estimated:** ~80 LOC
**Status:** ✅ Complete

---

### Task P.2.5: Implement starred notebooks logic
**File:** `source/core/NotebookLibrary.cpp`

**Implementation:**
- `starredNotebooks()`: Returns starred notebooks grouped by folder
  - Folders appear in `m_starredFolderOrder` order
  - Unfiled notebooks (empty `starredFolder`) appear last
- `setStarred(bundlePath, starred)`:
  - Toggles star status
  - Clears folder assignment when unstarring
- `setStarredFolder(bundlePath, folder)`:
  - Auto-stars notebook if assigning to a folder
  - Validates folder exists before assignment
  - Empty folder = unfiled
- `createStarredFolder(name)`:
  - Appends new folder to order list
  - Validates name is non-empty and unique
- `deleteStarredFolder(name)`:
  - Moves all notebooks in folder to unfiled
  - Removes folder from order list
- `reorderStarredFolder(name, newIndex)`:
  - Moves folder to new position (clamped to valid range)
  - Supports drag-drop reordering

**Estimated:** ~100 LOC
**Status:** ✅ Complete

---

### Task P.2.6: Implement search
**File:** `source/core/NotebookLibrary.cpp`

**Implementation:**
- Case-insensitive search using `QString::toLower()`
- Matches against `displayName()` and `pdfFileName` (if PDF-based)
- Scoring system:
  - Score 2: Exact match (name or PDF filename equals query)
  - Score 1: Contains match (name or PDF filename contains query)
- Results sorted by:
  1. Score descending (exact matches first)
  2. `lastAccessed` descending (more recent first for same score)
- Early return for empty query

**Estimated:** ~50 LOC
**Status:** ✅ Complete

---

### Task P.2.7: Implement thumbnail caching
**File:** `source/core/NotebookLibrary.cpp`

**Implementation:**
- Cache location: `QStandardPaths::CacheLocation + "/thumbnails/"`
- Filename: `{documentId}.png` (using notebook's unique ID)
- `thumbnailPathFor(bundlePath)`:
  - Looks up notebook by path to get documentId
  - Returns path only if file exists, otherwise empty string
- `saveThumbnail(bundlePath, thumbnail)`:
  - Saves pixmap as PNG using documentId
  - Emits `thumbnailUpdated` signal for UI refresh
  - Triggers `cleanupThumbnailCache()` after save
- `invalidateThumbnail(bundlePath)`:
  - Deletes cached file if it exists
  - Emits `thumbnailUpdated` signal
- `cleanupThumbnailCache()`:
  - LRU eviction when total cache size exceeds 200 MiB
  - Sorts files by `lastModified` (oldest first)
  - Deletes oldest until under limit

**Added to header:**
- `MAX_CACHE_SIZE_BYTES = 200 * 1024 * 1024` constant
- `cleanupThumbnailCache()` private method

**Estimated:** ~80 LOC
**Status:** ✅ Complete

---

### Task P.2.8: Integrate with DocumentManager
**File:** `source/core/DocumentManager.cpp`

**Implementation:**
- Added `#include "NotebookLibrary.h"`
- On .snb bundle load (`loadDocument`): `NotebookLibrary::instance()->addToRecent(path)`
- On document save (`doSave`): `NotebookLibrary::instance()->addToRecent(bundlePath)`
  - Uses `addToRecent` instead of `updateLastAccessed` to also refresh metadata
- On document close: Added comment noting MainWindow should handle thumbnail saving
  - MainWindow connects to `documentClosed(doc)` signal
  - Calls `NotebookLibrary::instance()->saveThumbnail(path, thumbnail)`
  - (Thumbnail saving implemented in Phase P.4 MainWindow integration)

**Note:** Thumbnail saving requires PagePanel access, which only MainWindow has.
The `documentClosed` signal is already emitted at the right time for this.

**Estimated:** ~30 LOC
**Status:** ✅ Complete

---

## Phase P.3: Launcher UI

Build the new launcher from scratch.

### Task P.3.1: Create Launcher class skeleton
**Files:** `source/ui/launcher/Launcher.h`, `source/ui/launcher/Launcher.cpp`

**Implementation:**
- Created `source/ui/launcher/` directory
- Header includes all signals, UI component pointers, and View enum
- Signals: `notebookSelected`, `createNewEdgeless`, `createNewPaged`, `openPdfRequested`, `openNotebookRequested`
- View switching: `Timeline`, `Starred`, `Search` views via `QStackedWidget`
- Animation support:
  - `showWithAnimation()` / `hideWithAnimation()` with fade effect
  - `Q_PROPERTY(fadeOpacity)` for `QPropertyAnimation`
- Keyboard handling:
  - `Escape` hides launcher
  - `Ctrl+H` toggles launcher (as per Q&A)
- Setup methods stubbed with TODO comments for later tasks
- Added to `CMakeLists.txt` under `UI_SOURCES`

**Estimated:** ~100 LOC
**Status:** ✅ Complete

---

### Task P.3.2: Create navigation sidebar
**Files:** 
- `source/ui/launcher/LauncherNavButton.h/cpp` (new widget)
- `source/ui/launcher/Launcher.cpp` (updated)

**LauncherNavButton widget:**
- Pill-shaped button: 44px height × 132px width (expanded)
- Compact mode: 44x44 circle, icon only (for portrait)
- Icon on left (20px), text on right
- Checkable for view selection (accent color when checked)
- Follows `ActionBarButton` styling patterns
- `setIconName()`: Uses `{name}.png` / `{name}_reversed.png` convention
- `setCompact(bool)`: Switches between pill and circle
- Colors: Google blue accent (#1a73e8 light, #8ab4f8 dark)

**Navigation buttons:**
- Return: `back` icon, non-checkable, hides launcher
- Timeline: `timeline` icon, checkable
- Starred: `star` icon, checkable
- Search: `zoom` icon (existing), checkable

**`setNavigationCompact(bool)`:** 
- Switches all nav buttons between expanded/compact mode
- Updates sidebar width accordingly

**QSS files:**
- `resources/styles/launcher.qss` (light mode)
- `resources/styles/launcher_dark.qss` (dark mode)

**Estimated:** ~250 LOC (widget + integration)
**Status:** ✅ Complete

---

### Task P.3.3: Create Timeline view
**Files:**
- `source/ui/launcher/TimelineModel.h/cpp` (~180 LOC)
- `source/ui/launcher/TimelineDelegate.h/cpp` (~280 LOC)
- `source/ui/launcher/Launcher.cpp` (updated `setupTimeline()`)

**Implementation:**

**TimelineModel (QAbstractListModel):**
- Fetches data from `NotebookLibrary::recentNotebooks()`
- Groups notebooks by time period using `sectionForDate()`:
  - Today, Yesterday, This Week, This Month, Last Month
  - Individual months for current year (e.g., "January")
  - Year for previous years (e.g., "2025", "2024")
- Builds flat display list with section headers interleaved
- Custom roles: `NotebookInfoRole`, `IsSectionHeaderRole`, `SectionNameRole`,
  `BundlePathRole`, `ThumbnailPathRole`, `LastModifiedRole`, etc.
- Auto-reloads on `NotebookLibrary::libraryChanged` signal

**TimelineDelegate (QStyledItemDelegate):**
- Two rendering modes: section headers and notebook cards
- **Section headers:** Bold text with underline (32px height)
- **Notebook cards:** (80px height)
  - Thumbnail on left (60px width, rounded corners)
  - Name (bold), date, and type indicator
  - Star indicator (★) if starred
  - Hover/selected background states
  - Subtle shadow in light mode
- Thumbnail display per Q&A (C+D hybrid):
  - Taller than card: top-align crop
  - Shorter than card: letterbox
  - Standard aspect: no modification
- Type indicators with colors:
  - PDF Annotation (red)
  - Edgeless Canvas (green)
  - Paged Notebook (blue)
- Date formatting: relative for recent, absolute for older

**Touch scrolling:**
- `QScroller::grabGesture()` on list viewport
- Configured overshoot and drag sensitivity

**Click handling:**
- Section headers are ignored
- Notebook cards emit `notebookSelected(bundlePath)`

**Estimated:** ~250 LOC (actual: ~460 LOC)
**Status:** ✅ Complete

---

### Task P.3.4: Create NotebookCard widget
**Files:** `source/ui/launcher/NotebookCard.h/cpp`

**Implementation:**
- **Fixed size:** 120×160 pixels for consistent grid layout
- **Thumbnail area:** 104×100 pixels with 8px corner radius
  - C+D hybrid display: top-crop for tall images, letterbox for short
  - Placeholder icon (📄) when thumbnail not available
- **Name label:** Bold, elided if too long
- **Type indicator:** Colored text (PDF=red, Edgeless=green, Paged=blue)
- **Star indicator:** Yellow ★ in top-right of thumbnail if starred

**Interaction:**
- Tap → `clicked()` signal
- Long-press (500ms) → `longPressed()` signal for context menu
- Movement cancels long-press (10px threshold)
- Hover effect (lighter background)
- Press effect (darker background)
- Selected state with accent border

**Visual:**
- Rounded corners (12px)
- Subtle shadow in light mode
- Dark mode support with appropriate colors

**Pattern:** Follows `ActionBarButton` widget patterns for mouse events and dark mode detection.

**Estimated:** ~150 LOC (actual: ~290 LOC)
**Status:** ✅ Complete

---

### Task P.3.5: Create Starred view
**Files:** `source/ui/launcher/StarredView.h/cpp`

**Implementation:**

**StarredView (main widget):**
- Scrollable container with touch-friendly QScroller
- Builds folder sections from `NotebookLibrary::starredNotebooks()` and `starredFolders()`
- Groups notebooks by `starredFolder` property
- "Unfiled" section for starred notebooks without a folder
- Empty state message when no starred notebooks
- Persists collapsed/expanded state per folder

**FolderHeader (collapsible section header):**
- 44px height, bold folder name with chevron (▶/▼)
- Click to toggle collapse/expand
- Long-press (500ms) triggers `longPressed()` for context menu
- Hover and press visual feedback
- Dark mode support

**Notebook Grid:**
- Uses `QGridLayout` with `NotebookCard` widgets
- 3+ columns based on estimated width
- Cards emit `clicked()` and `longPressed()` signals
- Responsive spacing (12px between cards)

**Signals:**
- `notebookClicked(bundlePath)` → forwarded to `Launcher::notebookSelected`
- `notebookLongPressed(bundlePath)` → for context menu (TODO)
- `folderLongPressed(folderName)` → for folder rename/delete (TODO)

**Future enhancements (deferred):**
- Drag-drop reordering within folders
- Drag to move between folders
- FlowLayout for true responsive grid

**Estimated:** ~300 LOC (actual: ~380 LOC)
**Status:** ✅ Complete

---

### Task P.3.6: Create Search view
**Files:** `source/ui/launcher/SearchView.h/cpp`

**Implementation:**

**Search Bar (styled like MarkdownNotesSidebar):**
- `QLineEdit` with placeholder "Search notebooks..."
- Clear button enabled (built-in)
- Search button (44×44, zoom icon)
- Clear button (44×44, ×) - shown when input has text
- 44px height for touch-friendly targets

**Real-time Search:**
- 300ms debounce timer for typing
- Enter key triggers immediate search
- Escape key clears search
- Uses `NotebookLibrary::search(query)`
- Scope: notebook names + PDF filenames (per Q&A)

**Results Display:**
- Grid of `NotebookCard` widgets (4 columns)
- 12px spacing between cards
- Touch-friendly scrolling with QScroller
- Status label shows result count

**Empty States:**
- Initial: "Type to search notebooks by name or PDF filename"
- No results: "No notebooks match your search."

**Signals:**
- `notebookClicked(bundlePath)` → `Launcher::notebookSelected`
- `notebookLongPressed(bundlePath)` → context menu (TODO)

**Auto-focus:** When switching to Search view, input is focused

**Estimated:** ~150 LOC (actual: ~270 LOC)
**Status:** ✅ Complete

---

### Task P.3.7: Create FAB (Floating Action Button)
**Files:** `source/ui/launcher/FloatingActionButton.h/cpp`

**Implementation:**

**Main Button (56×56):**
- Round button with Google Blue (#1a73e8) background
- White "+" icon that rotates to "×" when expanded
- Hover and pressed states
- Positioned in bottom-right corner (24px margin)
- Tooltip: "Create new notebook"

**Action Buttons (48×48 each):**
| Order | Icon | Tooltip | Signal |
|-------|------|---------|--------|
| 1 (bottom) | `edgeless` | "New Edgeless Canvas" | `createEdgeless()` |
| 2 | `paged` | "New Paged Notebook" | `createPaged()` |
| 3 | `pdf` | "Open PDF for Annotation" | `openPdf()` |
| 4 (top) | `folder` | "Open Notebook (.snb)" | `openNotebook()` |

**Animation:**
- `QParallelAnimationGroup` with two properties:
  - `expandProgress` (0→1): Animates action button positions upward
  - `rotation` (0→45°): Rotates + to × on main button
- Duration: 200ms with OutCubic easing
- Action buttons fade in/out with expand progress

**Behavior:**
- Click main button → toggle expand/collapse
- Click action button → emit signal + collapse
- Click outside (event filter on parent) → collapse
- Resize parent → reposition FAB

**Connected to Launcher signals:**
- `createEdgeless()` → `Launcher::createNewEdgeless`
- `createPaged()` → `Launcher::createNewPaged`
- `openPdf()` → `Launcher::openPdfRequested`
- `openNotebook()` → `Launcher::openNotebookRequested`

**Estimated:** ~200 LOC (actual: ~310 LOC)
**Status:** ✅ Complete

---

### Task P.3.8: Implement gestures and context menus
**Files:** `source/ui/launcher/Launcher.cpp` (updated)

**Gestures (already implemented in NotebookCard):**
- ✅ Tap: Open notebook
- ✅ Long-press: Triggers context menu signal

**Context Menus (new):**

**Notebook Context Menu:**
| Action | Description |
|--------|-------------|
| ☆ Star / ★ Unstar | Toggle starred status |
| Move to Folder | Submenu with: Unfiled, existing folders, + New Folder |
| 🗑 Delete | Confirm dialog, removes from library and deletes from disk |

**Folder Context Menu:**
| Action | Description |
|--------|-------------|
| ✏ Rename | Input dialog, moves notebooks to new folder name |
| 🗑 Delete Folder | Confirm dialog, notebooks become unfiled |

**Context Menu Triggers:**
- Timeline: Right-click via `customContextMenuRequested`
- Starred view: `notebookLongPressed` / `folderLongPressed` signals
- Search view: `notebookLongPressed` signal

**Helper Methods Added:**
- `showNotebookContextMenu(bundlePath, globalPos)`
- `showFolderContextMenu(folderName, globalPos)`
- `deleteNotebook(bundlePath)` - with confirmation
- `toggleNotebookStar(bundlePath)`

**Swipe Gestures (deferred to polish phase):**
- Swipe left: Delete button
- Swipe right: Toggle star

**Estimated:** ~100 LOC (actual: ~160 LOC)
**Status:** ✅ Complete

---

### Task P.3.9: Implement full context menu
**File:** `source/ui/launcher/Launcher.cpp` (extended from P.3.8)

**Complete Menu Items:**
| Action | Icon | Implementation |
|--------|------|----------------|
| Star / Unstar | ☆/★ | `toggleNotebookStar()` |
| Move to Folder | → | Submenu: Unfiled, folders, + New Folder |
| Rename | ✏ | `renameNotebook()` - input dialog, rename .snb folder |
| Duplicate | 📋 | `duplicateNotebook()` - recursive copy with "(Copy)" suffix |
| Show in File Manager | 📂 | `showInFileManager()` - platform-specific |
| Delete | 🗑 | `deleteNotebook()` - confirm, remove + delete |

**New Helper Methods (~110 LOC):**

**`renameNotebook(bundlePath)`:**
- Extract current name from path
- Show input dialog with current name
- Sanitize new name (replace `/` and `\` with `_`)
- Check if target exists
- Rename directory with `QDir::rename()`
- Update NotebookLibrary (remove old, add new)

**`duplicateNotebook(bundlePath)`:**
- Generate unique name with "(Copy)" or "(Copy N)" suffix
- Create destination directory
- Recursively copy all files/subdirectories with `QDirIterator`
- Add new notebook to library

**`showInFileManager(bundlePath)`:**
- Windows: `explorer /select,<path>`
- macOS: `open -R <path>`
- Linux: `QDesktopServices::openUrl()` on parent folder

**Estimated:** ~80 LOC (actual: ~110 LOC for new methods)
**Status:** ✅ Complete

---

### Task P.3.10: Apply styling
**Files:** `resources/styles/launcher.qss`, `resources/styles/launcher_dark.qss`

**Updated to match actual widget object names and added missing styles:**

**Styled Elements:**
| Widget | Object Name | Style Features |
|--------|-------------|----------------|
| Main window | `Launcher` | Background color |
| Nav sidebar | `#LauncherNavSidebar` | Background, border |
| Nav separator | `#LauncherNavSeparator` | Line color |
| Content views | `#TimelineView`, `#StarredViewWidget`, `#SearchViewWidget` | Background |
| Timeline list | `#TimelineList` | Transparent background |
| Search input | `#SearchInput` | Pill-shaped (22px radius), border, focus state |
| Search buttons | `#SearchButton`, `#ClearButton` | Round buttons, hover/pressed states |
| Status label | `#StatusLabel` | Muted text color |
| Empty label | `#EmptyLabel` | Centered, muted, with padding |
| Scroll areas | `#SearchScrollArea`, `#StarredScrollArea` | Transparent, no border |
| Scroll bars | `QScrollBar:vertical` | Slim (8px), rounded handle, touch-friendly |
| Context menus | `QMenu` | Rounded corners, padding, item states |
| Message boxes | `QMessageBox` | Background, label colors |
| Input dialogs | `QInputDialog` | Styled input fields |
| Tooltips | `QToolTip` | Dark background, rounded |

**Notes:**
- `LauncherNavButton` uses custom QPainter rendering, not QSS
- `TimelineDelegate` uses custom QPainter rendering, not QSS
- `NotebookCard` uses custom QPainter rendering, not QSS
- `FloatingActionButton` uses inline stylesheets for its buttons

**Touch-friendly targets:**
- 44px minimum height for interactive elements
- 40px minimum scrollbar handle height

**Estimated:** ~150 LOC (actual: ~430 LOC total across both files)
**Status:** ✅ Complete

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
**Status:** Complete

**Implementation Notes:**
- Added `#include "ui/launcher/Launcher.h"`
- Fixed SDL_QUIT macro (was self-referencing)
- If file argument provided → go directly to MainWindow, use `openFileInNewTab(path)`
- If no file → show Launcher and connect all signals:
  - `notebookSelected` → `MainWindow::openFileInNewTab(bundlePath)` (routes through DocumentManager)
  - `createNewEdgeless` → `MainWindow::addNewEdgelessTab()`
  - `createNewPaged` → `MainWindow::addNewTab()`
  - `openPdfRequested` → `MainWindow::showOpenPdfDialog()` (new public method)
  - `openNotebookRequested` → `MainWindow::loadFolderDocument()`
- Uses `MainWindow::findExistingMainWindow()` and `preserveWindowState()` for smooth transitions
- All file operations route through `DocumentManager::loadDocument()` for proper ownership and state management
- Updated `openFileInNewTab()` to handle all file types (PDFs, .snb bundles, .snx/.json)

---

### Task P.4.2: Add MainWindow interface methods
**File:** `source/MainWindow.h`, `source/MainWindow.cpp`
**New methods:**
```cpp
// Document operations (already existed, renamed/documented)
void openFileInNewTab(const QString& filePath);  // Opens PDF, .snb, .snx
void showOpenPdfDialog();                         // Shows file dialog for PDF
void loadFolderDocument();                        // Shows folder dialog for .snb

// Tab creation (already existed)
void addNewTab();           // Same as createNewPaged()
void addNewEdgelessTab();   // Same as createNewEdgeless()

// Tab operations (NEW)
bool hasOpenDocuments() const;
bool switchToDocument(const QString& bundlePath);

// Window operations (NEW)
void bringToFront();

// Already existed
static MainWindow* findExistingMainWindow();
void preserveWindowState(QWidget* source, bool isExisting);
```

**Estimated:** ~100 LOC
**Status:** Complete

**Implementation Notes:**
- `hasOpenDocuments()`: Returns `m_tabManager->tabCount() > 0`
- `switchToDocument(path)`: Iterates tabs, compares normalized paths, switches if found
- `bringToFront()`: Calls `show()`, `raise()`, `activateWindow()`
- Updated Main.cpp to use `bringToFront()` and `switchToDocument()` to prevent duplicates

---

### Task P.4.3: Update "+" button dropdown
**File:** `source/MainWindow.cpp`, `source/MainWindow.h`, `source/ui/NavigationBar.h`
**Changes:**
- Replace current "+" behavior with dropdown menu:
  - New Edgeless Canvas (Ctrl+Shift+N)
  - New Paged Notebook (Ctrl+N)
  - ───────────────
  - Open PDF... (Ctrl+Shift+O)
  - Open Notebook... (Ctrl+Shift+L)

**Estimated:** ~50 LOC
**Status:** Complete

**Implementation Notes:**
- Added `showAddMenu()` method to MainWindow
- Added `addButton()` getter to NavigationBar for menu positioning
- Changed `addClicked` signal handler to call `showAddMenu()` instead of `addNewTab()`
- Menu appears directly below the "+" button
- Added Ctrl+N global shortcut for New Paged Notebook
- Menu shows keyboard shortcuts for all actions

---

### Task P.4.4: Add Ctrl+H shortcut
**File:** `source/MainWindow.cpp`, `source/MainWindow.h`
**Changes:**
- Add `QShortcut` for Ctrl+H → toggle launcher
- Add `QShortcut` for Escape → go to launcher (when no dialogs open)
- Implement `toggleLauncher()` method
- Connect `launcherClicked` signal from NavigationBar

**Estimated:** ~30 LOC
**Status:** Complete

**Implementation Notes:**
- `toggleLauncher()` finds Launcher via `QApplication::topLevelWidgets()` using `inherits("Launcher")`
- **Window state transfer**: Copies geometry (position/size) and state (maximized/fullscreen) between windows
- If launcher visible: transfers geometry to MainWindow, hides launcher, shows MainWindow
- If launcher hidden: transfers geometry to launcher, hides MainWindow, shows launcher
- This creates seamless "same window" experience when toggling
- Ctrl+H has `Qt::ApplicationShortcut` context
- Escape has `Qt::WindowShortcut` context and checks `QApplication::activeModalWidget()` before toggling
- NavigationBar launcher button now connected to `toggleLauncher()`

---

### Task P.4.5: Implement smooth transition
**File:** `source/ui/launcher/Launcher.cpp`, `source/MainWindow.cpp`, `source/Main.cpp`
**Changes:**
- Fix window flash (proper show/hide ordering)
- Add fade animation (optional polish):
  - Launcher fades out (opacity 1→0)
  - MainWindow fades in (opacity 0→1)
  - Use QPropertyAnimation on windowOpacity

**Estimated:** ~50 LOC
**Status:** Complete

**Implementation Notes:**
- `MainWindow::toggleLauncher()` now uses 150ms fade animations with `QEasingCurve::OutCubic`
- Destination window is shown first (at opacity 0), source is hidden immediately, then destination fades in
- Window opacity is reset to 1.0 after transitions for clean state
- `MainWindow::bringToFront()` detects if window was hidden and fades in accordingly
- `Main.cpp` lambdas now call `launcher->hideWithAnimation()` for consistent fade behavior
- Launcher already had `showWithAnimation()` and `hideWithAnimation()` from Phase P.3

---

### Task P.4.6: Save thumbnail on document close
**File:** `source/MainWindow.cpp`, `source/ui/PagePanel.h/cpp`
**Changes:**
- On tab close or document save:
  - Get page-0 thumbnail from PagePanel cache (if available)
  - Fall back to `renderPage0Thumbnail()` for synchronous rendering
  - Call `NotebookLibrary::instance()->saveThumbnail(path, pixmap)`
- Added `PagePanel::thumbnailForPage(int)` method
- Added `MainWindow::renderPage0Thumbnail(Document*)` helper

**Estimated:** ~30 LOC
**Status:** Complete

**Implementation Notes:**
- Thumbnail saving occurs in two places:
  1. Tab close: in `tabCloseRequested` handler, before document deletion
  2. Document save: in `saveDocument()` after successful save
- Fixed memory safety issue: PagePanel's document reference is cleared BEFORE document deletion
  - This ensures `ThumbnailRenderer::cancelAll()` blocks until all async renders complete
  - Prevents use-after-free on the Document pointer
- `renderPage0Thumbnail()` uses same rendering logic as `ThumbnailRenderer::renderThumbnailSync()`
- Save As also calls `NotebookLibrary::addToRecent()` to register new documents

---

### Task P.4.7: Remove Ctrl+O shortcut
**File:** `source/MainWindow.cpp`
**Changes:**
- Remove the obsolete Ctrl+O shortcut
- Update help/documentation if any

**Estimated:** ~10 LOC
**Status:** Complete

**Implementation Notes:**
- Removed `QShortcut* loadShortcut` for `QKeySequence::Open` (Ctrl+O)
- Added comment explaining file opening is now handled by:
  - Launcher (recent notebooks, starred, search)
  - "+" menu → Open PDF... (Ctrl+Shift+O)
  - "+" menu → Open Notebook... (Ctrl+Shift+L)
- `loadDocument()` function retained for potential legacy use

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
| P.1 | P.1.1 | Add .snb_marker + version | 30 | Pending |
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

