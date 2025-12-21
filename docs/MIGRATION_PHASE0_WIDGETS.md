# Phase 0.3: Page-Related Widgets in MainWindow

> **Purpose:** Document all UI widgets connected to the page/document system.
> **Generated:** Phase 0.3 of viewport architecture migration
> **Total widgets mapped:** 25+ widgets across 6 categories

---

## 1. PAGE NAVIGATION WIDGETS

### 1.1 Core Navigation Controls

| Widget | Type | Declaration | Purpose | Occurrences |
|--------|------|-------------|---------|-------------|
| `pageInput` | `QSpinBox*` | Line 320 | Page number input (1-based) | 76 |
| `prevPageButton` | `QPushButton*` | Line 321 | Go to previous page | 5 |
| `nextPageButton` | `QPushButton*` | Line 322 | Go to next page | 5 |
| `jumpToPageButton` | `QPushButton*` | Line 591 | Show jump-to-page dialog | 3 |
| `deletePageButton` | `QPushButton*` | Line 533 | Clear current page content | 5 |

### 1.2 Signal/Slot Connections

```cpp
// [WIDGET:PAGE-NAV] Navigation button connections
connect(prevPageButton, &QPushButton::clicked, this, &MainWindow::goToPreviousPage);
connect(nextPageButton, &QPushButton::clicked, this, &MainWindow::goToNextPage);
connect(pageInput, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onPageInputChanged);
connect(jumpToPageButton, &QPushButton::clicked, this, &MainWindow::showJumpToPageDialog);
connect(deletePageButton, &QPushButton::clicked, this, &MainWindow::deleteCurrentPage);
```

### 1.3 Handler Methods

| Method | Purpose | Canvas Interaction |
|--------|---------|-------------------|
| `goToPreviousPage()` | Navigate backward | `switchPageWithDirection(newPage, -1)` |
| `goToNextPage()` | Navigate forward | `switchPageWithDirection(newPage, 1)` |
| `onPageInputChanged()` | Handle spinbox change | `switchPageWithDirection()` or `switchPage()` |
| `showJumpToPageDialog()` | Jump to specific page | `switchPage()` + `pageInput->setValue()` |
| `deleteCurrentPage()` | Clear page content | `canvas->clearCurrentPage()` |

### 1.4 Migration Notes
- **pageInput** must stay synced with viewport's current page
- **Direction tracking** (-1, 0, 1) is for scroll position after page switch
- New architecture: Viewport emits `pageChanged(int)` signal → update pageInput

---

## 2. PAN/ZOOM WIDGETS

### 2.1 Zoom Controls

| Widget | Type | Purpose | Occurrences |
|--------|------|---------|-------------|
| `zoomSlider` | `QSlider*` | Main zoom control (50-200%) | 119 |
| `zoomButton` | `QPushButton*` | Toggle zoom popup | - |
| `zoomInput` | `QLineEdit*` | Manual zoom input | - |
| `zoom50Button` | `QPushButton*` | Set zoom to 50% | - |
| `dezoomButton` | `QPushButton*` | Set zoom to 100% | - |
| `zoom200Button` | `QPushButton*` | Set zoom to 200% | - |

### 2.2 Pan Controls

| Widget | Type | Purpose | Occurrences |
|--------|------|---------|-------------|
| `panXSlider` | `QScrollBar*` | Horizontal pan | 119 |
| `panYSlider` | `QScrollBar*` | Vertical pan | 119 |

### 2.3 Signal/Slot Connections

```cpp
// [WIDGET:ZOOM] Zoom control connections
connect(zoomSlider, &QSlider::valueChanged, this, &MainWindow::onZoomSliderChanged);
connect(zoom50Button, &QPushButton::clicked, [this]() { zoomSlider->setValue(50/dpr); });
connect(dezoomButton, &QPushButton::clicked, [this]() { zoomSlider->setValue(100/dpr); });
connect(zoom200Button, &QPushButton::clicked, [this]() { zoomSlider->setValue(200/dpr); });

// [WIDGET:PAN] Pan control connections
connect(panXSlider, &QScrollBar::valueChanged, this, &MainWindow::updatePanX);
connect(panYSlider, &QScrollBar::valueChanged, this, &MainWindow::updatePanY);
```

### 2.4 Handler Methods

| Method | Purpose | Canvas Interaction |
|--------|---------|-------------------|
| `updateZoom()` | Apply zoom to canvas | `canvas->setZoom()`, `updatePanRange()` |
| `onZoomSliderChanged()` | Handle slider change | `updateZoom()` |
| `updatePanRange()` | Calculate pan limits | `canvas->getZoom()`, `canvas->getCanvasSize()` |
| `updatePanX()` | Apply X pan | `canvas->setPanX()`, `canvas->setLastPanX()` |
| `updatePanY()` | Apply Y pan | `canvas->setPanY()`, `canvas->setLastPanY()` |

### 2.5 Touch Gesture Handlers

```cpp
// [WIDGET:TOUCH] Touch gesture signal handlers
connect(canvas, &InkCanvas::zoomChanged, this, &MainWindow::handleTouchZoomChange);
connect(canvas, &InkCanvas::panChanged, this, &MainWindow::handleTouchPanChange);
connect(canvas, &InkCanvas::touchGestureEnded, this, &MainWindow::handleTouchGestureEnd);
```

| Method | Purpose | Widget Interaction |
|--------|---------|-------------------|
| `handleTouchZoomChange()` | Sync zoom from touch | `zoomSlider->setValue()`, `updatePanRange()` |
| `handleTouchPanChange()` | Sync pan from touch | `panXSlider->setValue()`, `panYSlider->setValue()` |
| `handleTouchGestureEnd()` | Hide scrollbars | `panXSlider->setVisible(false)` |

### 2.6 Migration Notes
- **Pan range calculation** depends on canvas size and zoom
- **Autoscroll threshold** triggers page switch at pan limits
- New architecture: Viewport handles zoom/pan internally, emits signals to sync UI

---

## 3. PDF OUTLINE SIDEBAR

### 3.1 Widgets

| Widget | Type | Purpose | Occurrences |
|--------|------|---------|-------------|
| `outlineSidebar` | `QWidget*` | Container for outline | 130 |
| `outlineTree` | `QTreeWidget*` | PDF bookmarks/TOC | 130 |
| `toggleOutlineButton` | `QPushButton*` | Toggle sidebar visibility | - |

### 3.2 Signal/Slot Connections

```cpp
// [WIDGET:OUTLINE] Outline sidebar connections
connect(outlineTree, &QTreeWidget::itemClicked, this, &MainWindow::onOutlineItemClicked);
connect(toggleOutlineButton, &QPushButton::clicked, this, &MainWindow::toggleOutlineSidebar);
```

### 3.3 Handler Methods

| Method | Purpose | Canvas Interaction |
|--------|---------|-------------------|
| `toggleOutlineSidebar()` | Show/hide outline | None |
| `loadPdfOutline()` | Populate tree from PDF | `getPdfDocument()` |
| `onOutlineItemClicked()` | Navigate to page | `switchPage()`, `pageInput->setValue()` |
| `updateOutlineSelection()` | Highlight current page | None |

### 3.4 Migration Notes
- Outline data comes from PDF → stays in Document class
- Navigation still goes through page switching → use Viewport.goToPage()
- New architecture: Document owns PDF, Viewport handles navigation

---

## 4. BOOKMARKS SIDEBAR

### 4.1 Widgets

| Widget | Type | Purpose | Occurrences |
|--------|------|---------|-------------|
| `bookmarksSidebar` | `QWidget*` | Container for bookmarks | 130 |
| `bookmarkList` | `QListWidget*` | User bookmark list | - |
| `toggleBookmarksButton` | `QPushButton*` | Toggle sidebar | - |
| `toggleBookmarkButton` | `QPushButton*` | Add/remove bookmark | - |

### 4.2 Data Storage

```cpp
// [WIDGET:BOOKMARKS] Bookmark data
QMap<int, QString> bookmarks;  // Page number → bookmark title
```

### 4.3 Signal/Slot Connections

```cpp
// [WIDGET:BOOKMARKS] Bookmark connections
connect(toggleBookmarksButton, &QPushButton::clicked, this, &MainWindow::toggleBookmarksSidebar);
connect(toggleBookmarkButton, &QPushButton::clicked, this, &MainWindow::toggleCurrentPageBookmark);
```

### 4.4 Handler Methods

| Method | Purpose | Canvas Interaction |
|--------|---------|-------------------|
| `toggleBookmarksSidebar()` | Show/hide bookmarks | None |
| `toggleCurrentPageBookmark()` | Add/remove bookmark | `canvas->setBookmarks()` |
| `updateBookmarkButtonState()` | Update button icon | `getCurrentPageForCanvas()` |
| `loadBookmarks()` | Populate bookmark list | None |
| `populateBookmarksList()` | Fill list widget | None |

### 4.5 Migration Notes
- Bookmarks stored in notebook metadata (JSON)
- Button state depends on current page → Viewport.currentPage()
- New architecture: Document stores bookmarks, Viewport navigates

---

## 5. MARKDOWN NOTES SIDEBAR

### 5.1 Widgets

| Widget | Type | Purpose |
|--------|------|---------|
| `markdownNotesSidebar` | `MarkdownNotesSidebar*` | Sidebar for notes |
| `toggleMarkdownNotesButton` | `QPushButton*` | Toggle sidebar |

### 5.2 Signal/Slot Connections

```cpp
// [WIDGET:MARKDOWN] Markdown notes connections
connect(markdownNotesSidebar, &MarkdownNotesSidebar::noteContentChanged, ...);
connect(markdownNotesSidebar, &MarkdownNotesSidebar::noteDeleted, ...);
connect(markdownNotesSidebar, &MarkdownNotesSidebar::highlightLinkClicked, ...);
connect(canvas, &InkCanvas::markdownNotesUpdated, this, &MainWindow::onMarkdownNotesUpdated);
connect(canvas, &InkCanvas::highlightDoubleClicked, this, &MainWindow::onHighlightDoubleClicked);
```

### 5.3 Handler Methods

| Method | Purpose | Canvas Interaction |
|--------|---------|-------------------|
| `loadMarkdownNotesForCurrentPage()` | Load notes for page | `canvas->getMarkdownNotesForPages()` |
| `onMarkdownNotesUpdated()` | Refresh sidebar | Re-load notes |
| `onMarkdownNoteContentChanged()` | Save note changes | `canvas->updateMarkdownNote()` |
| `onMarkdownNoteDeleted()` | Remove note | `canvas->removeMarkdownNote()` |
| `onHighlightDoubleClicked()` | Open linked note | Navigation + expand note |

### 5.4 Migration Notes
- Notes are per-page → stored in Page or Document
- Combined canvas shows notes for 2 pages → new architecture handles naturally
- Highlight links require page+coordinate → Viewport handles navigation

---

## 6. TAB/CANVAS MANAGEMENT

### 6.1 Data Structures

```cpp
// [WIDGET:TABS] Tab management data
QMap<InkCanvas*, int> pageMap;  // Canvas → current page number
QTabWidget* (or custom tab bar)
```

### 6.2 Key Methods

| Method | Purpose |
|--------|---------|
| `currentCanvas()` | Get active InkCanvas (200+ calls!) |
| `getCurrentPageForCanvas()` | Get page number for canvas |
| `addNewTab()` | Create new notebook tab |
| `switchTab()` | Switch between tabs |
| `findTabWithNotebookId()` | Find tab by notebook ID |

### 6.3 Migration Notes
- `currentCanvas()` → `currentViewport()` or `currentDocument()`
- `pageMap` → Viewport stores its own current page
- Tab management stays in MainWindow

---

## 7. AUTOSCROLL SYSTEM

### 7.1 Signal/Slot

```cpp
// [WIDGET:AUTOSCROLL] Auto page switching at scroll limits
connect(canvas, &InkCanvas::autoScrollRequested, this, &MainWindow::onAutoScrollRequested);
connect(canvas, &InkCanvas::earlySaveRequested, this, &MainWindow::onEarlySaveRequested);
```

### 7.2 Handler Methods

| Method | Purpose |
|--------|---------|
| `onAutoScrollRequested(direction)` | Switch page at scroll threshold |
| `onEarlySaveRequested()` | Save before crossing threshold |

### 7.3 How It Works (Current)
1. User scrolls panYSlider past threshold
2. InkCanvas detects threshold crossing via `checkAutoscrollThreshold()`
3. Emits `autoScrollRequested(+1)` or `autoScrollRequested(-1)`
4. MainWindow calls `switchPageWithDirection()`
5. Pan position is set to opposite end after page load

### 7.4 Migration Notes
- New architecture: Viewport handles continuous scrolling natively
- No threshold detection needed - pages render as they come into view
- `autoScrollRequested` signal becomes unnecessary

---

## 8. WIDGET DEPENDENCY SUMMARY

```
┌─────────────────────────────────────────────────────────────────────┐
│                          MainWindow                                  │
│                                                                      │
│  ┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐   │
│  │  Page Navigation │  │   Pan/Zoom       │  │   Sidebars       │   │
│  │  ────────────────│  │  ────────────────│  │  ────────────────│   │
│  │  pageInput       │  │  zoomSlider      │  │  outlineTree     │   │
│  │  prevPageButton  │  │  panXSlider      │  │  bookmarkList    │   │
│  │  nextPageButton  │  │  panYSlider      │  │  markdownSidebar │   │
│  │  jumpToPageBtn   │  │  zoom buttons    │  │                  │   │
│  └────────┬─────────┘  └────────┬─────────┘  └────────┬─────────┘   │
│           │                     │                     │              │
│           ▼                     ▼                     ▼              │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │                      InkCanvas (current)                      │   │
│  │  - switchPage(), goToPreviousPage(), goToNextPage()          │   │
│  │  - setZoom(), setPanX(), setPanY()                           │   │
│  │  - getMarkdownNotesForPages(), getPdfDocument()              │   │
│  └──────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │                   DocumentViewport (NEW)                      │   │
│  │  - goToPage(), setZoom(), setPan()                           │   │
│  │  - emits: pageChanged, zoomChanged, panChanged               │   │
│  │  - owns: Document reference                                   │   │
│  └──────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 9. MIGRATION PRIORITY

### HIGH (Core functionality)
- `pageInput`, `prevPageButton`, `nextPageButton` → connect to Viewport
- `zoomSlider`, `panXSlider`, `panYSlider` → connect to Viewport
- `pageMap` → remove, Viewport tracks own page

### MEDIUM (Important features)
- `outlineTree` → connect to Viewport for navigation
- `bookmarkList` → connect to Document for data
- Touch gesture handlers → connect to Viewport

### LOW (Can use adapters initially)
- `markdownNotesSidebar` → reads from Document
- Autoscroll system → may become unnecessary

---

## 10. TOTAL WIDGET COUNTS

| Category | Widget Count | Signal Connections | Handler Methods |
|----------|-------------|-------------------|-----------------|
| Page Navigation | 5 | 5 | 5 |
| Pan/Zoom | 8 | 6 | 8 |
| PDF Outline | 3 | 2 | 4 |
| Bookmarks | 4 | 2 | 5 |
| Markdown Notes | 2 | 5 | 5 |
| Tab Management | - | - | 5 |
| **TOTAL** | **22+** | **20** | **32** |

---

## 11. NEXT STEPS

Phase 0 is now COMPLETE:
- [✅] Phase 0.1: MainWindow ↔ InkCanvas interactions mapped
- [✅] Phase 0.2: Combined canvas code locations mapped  
- [✅] Phase 0.3: Page-related widgets documented

Ready for Phase 1: Create new data structures (Page, Document, DocumentViewport)

---

*Document generated for SpeedyNote viewport migration project*
