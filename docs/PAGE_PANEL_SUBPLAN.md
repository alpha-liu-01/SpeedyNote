# Page Panel Implementation Subplan

**Document Version:** 1.0  
**Date:** January 11, 2026  
**Status:** 🔵 READY TO IMPLEMENT  
**Prerequisites:** PDF Outline Panel complete, ActionBar framework complete  
**References:** `docs/PAGE_PANEL_QA.md` for design decisions

---

## Overview

The Page Panel is a new tab in the left sidebar that displays page thumbnails for quick navigation and page management in paged documents. It includes a dedicated action bar for page operations.

### Key Components

| Component | Description |
|-----------|-------------|
| **PagePanel** | QListView-based thumbnail display with lazy loading |
| **PageThumbnailModel** | QAbstractListModel for page data |
| **PageThumbnailDelegate** | Custom delegate for thumbnail rendering |
| **PagePanelActionBar** | Action bar with page navigation and management |
| **PageWheelPicker** | iPhone-style wheel for page number selection |
| **UndoDeleteButton** | Delete button that transforms to undo state |

### Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              MainWindow                                      │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  ┌──────────────────────┐                    ┌────────────────────────────┐ │
│  │   Left Sidebar       │                    │     DocumentViewport       │ │
│  │   (SidebarContainer) │                    │                            │ │
│  │                      │                    │                            │ │
│  │  ┌────────────────┐  │                    │                            │ │
│  │  │ [Outline]      │  │                    │                            │ │
│  │  │ [Pages]←active │  │                    │                            │ │
│  │  │ [Layers]       │  │                    │                            │ │
│  │  └────────────────┘  │                    │                            │ │
│  │                      │                    │     ┌──────────────────┐   │ │
│  │  ┌────────────────┐  │                    │     │ ActionBar        │   │ │
│  │  │  PagePanel     │  │                    │     │ Container        │   │ │
│  │  │  (QListView)   │  │                    │     │                  │   │ │
│  │  │                │  │                    │     │ ┌──────┐ ┌─────┐│   │ │
│  │  │  ┌──────────┐  │  │                    │     │ │Page  │ │Other││   │ │
│  │  │  │ Page 1   │  │  │                    │     │ │Panel │ │Action│   │ │
│  │  │  │ [thumb]  │  │  │    signals         │     │ │Action│ │Bar  ││   │ │
│  │  │  └──────────┘  │  │ ←──────────────────│     │ │Bar   │ │     ││   │ │
│  │  │  ┌──────────┐  │  │                    │     │ │      │ │     ││   │ │
│  │  │  │ Page 2   │◄─┼──┼─ current page      │     │ └──────┘ └─────┘│   │ │
│  │  │  │ [thumb]  │  │  │   highlight        │     │  Left     Right │   │ │
│  │  │  └──────────┘  │  │                    │     │  (24px gap)     │   │ │
│  │  │  ┌──────────┐  │  │                    │     └──────────────────┘   │ │
│  │  │  │ Page 3   │  │  │                    │                            │ │
│  │  │  │ [thumb]  │  │  │                    │                            │ │
│  │  │  └──────────┘  │  │                    │                            │ │
│  │  │      ...       │  │                    │                            │ │
│  │  └────────────────┘  │                    └────────────────────────────┘ │
│  └──────────────────────┘                                                    │
└─────────────────────────────────────────────────────────────────────────────┘
```

### PagePanelActionBar Layout

```
┌─────────────────┐
│   [Page Up ↑]   │
├─────────────────┤
│   ┌─────────┐   │
│   │   13    │   │  ← dimmed, small
│   │ [ 14 ]  │   │  ← current, bold   PageWheelPicker
│   │   15    │   │  ← dimmed, small   (36×72px "hotdog")
│   └─────────┘   │
├─────────────────┤
│  [Page Down ↓]  │
├═════════════════┤  ← separator
│   [Add Page]    │
├─────────────────┤
│  [Insert Page]  │
├─────────────────┤
│  [Delete Page]  │  ← transforms to [Undo] for 5 sec
└─────────────────┘
```

---

## Phase 0: Prerequisites & Testing

### Task 0.1: Test Document::movePage() (~50 lines)

**File:** `source/core/DocumentTests.h`

Before implementing drag-and-drop, verify `movePage()` works correctly.

**Tests to add:**
```cpp
void testMovePage() {
    // Create document with 5 pages
    auto doc = Document::createNew("Test");
    for (int i = 0; i < 4; ++i) doc->addPage();
    
    // Test: Move page 0 to position 2
    doc->movePage(0, 2);
    // Verify order
    
    // Test: Move page 4 to position 1
    doc->movePage(4, 1);
    // Verify order
    
    // Test: Move to same position (no-op)
    doc->movePage(2, 2);
    // Verify unchanged
    
    // Test: Edge cases
    doc->movePage(0, 4);  // First to last
    doc->movePage(4, 0);  // Last to first
    
    // Test: Bookmarks are preserved
    doc->setBookmark(1, "Test Bookmark");  // But I'm hesitating if the bookmark feature will be kept, since we already have LinkObjects... You may come up with something else for testing here.
    doc->movePage(1, 3);
    // Verify bookmark follows page
}
```

**Deliverable:** All movePage() tests pass

---

## Phase 1: Core Widgets

### Task 1.1: PageWheelPicker Widget (~300 lines)

**Files:** `source/ui/widgets/PageWheelPicker.h`, `source/ui/widgets/PageWheelPicker.cpp`

**Description:** iPhone-style wheel picker for page number selection.

```cpp
class PageWheelPicker : public QWidget {
    Q_OBJECT
    Q_PROPERTY(int currentPage READ currentPage WRITE setCurrentPage NOTIFY currentPageChanged)
    Q_PROPERTY(int pageCount READ pageCount WRITE setPageCount NOTIFY pageCountChanged)
    
public:
    explicit PageWheelPicker(QWidget* parent = nullptr);
    
    int currentPage() const;      // 0-based
    void setCurrentPage(int page);
    
    int pageCount() const;
    void setPageCount(int count);
    
    void setDarkMode(bool dark);
    
signals:
    void currentPageChanged(int page);  // Emitted during scroll AND on snap
    void pageCountChanged(int count);
    
protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    
private:
    int m_currentPage = 0;
    int m_pageCount = 1;
    
    // Scroll state
    qreal m_scrollOffset = 0.0;     // Fractional page offset during drag
    qreal m_velocity = 0.0;         // For inertia
    QPointF m_lastPos;
    QElapsedTimer m_velocityTimer;
    
    // Animation
    QPropertyAnimation* m_snapAnimation = nullptr;
    QTimer* m_inertiaTimer = nullptr;
    
    bool m_darkMode = false;
    
    void startInertia();
    void stopInertia();
    void snapToPage();
    void updateFromOffset();
    
    static constexpr int VISIBLE_PAGES = 3;
    static constexpr qreal DECELERATION = 0.95;
    static constexpr qreal SNAP_THRESHOLD = 0.1;
};
```

**Visual specs:**
- Size: 36×72 px (border-radius: 18px - "hotdog" shape)
- Center number: 14px bold, full opacity
- Adjacent numbers: 10px light, 40% opacity
- Background: theme-aware (same as action bar buttons)

**Behavior:**
1. Drag up/down to scroll through pages
2. Release with velocity → inertia scroll with deceleration
3. When velocity < threshold → snap to nearest whole page
4. Emit `currentPageChanged` during scroll AND on final snap
5. Mouse wheel also scrolls

**Estimated:** ~300 lines

---

### Task 1.2: UndoDeleteButton Widget (~150 lines)

**Files:** `source/ui/widgets/UndoDeleteButton.h`, `source/ui/widgets/UndoDeleteButton.cpp`

**Description:** Delete button that transforms to undo state after click.

```cpp
class UndoDeleteButton : public QWidget {
    Q_OBJECT
    
public:
    explicit UndoDeleteButton(QWidget* parent = nullptr);
    
    void setDarkMode(bool dark);
    
    // Call when an external action should confirm the delete
    void confirmDelete();
    
signals:
    void deleteRequested();       // Emitted on first click
    void deleteConfirmed();       // Emitted after timeout or confirmDelete()
    void undoRequested();         // Emitted if undo clicked within timeout
    
protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    
private:
    enum class State { Normal, UndoPending };
    State m_state = State::Normal;
    
    QTimer* m_confirmTimer = nullptr;
    bool m_darkMode = false;
    bool m_pressed = false;
    
    QIcon m_deleteIcon;
    QIcon m_undoIcon;
    
    void startUndoTimer();
    void onTimerExpired();
    void resetToNormal();
    
    static constexpr int UNDO_TIMEOUT_MS = 5000;
};
```

**Behavior:**
1. Normal state: Shows delete/trash icon
2. Click → enters UndoPending state, shows undo icon, starts 5-sec timer
3. In UndoPending:
   - Click → emit `undoRequested()`, return to Normal
   - Timer expires → emit `deleteConfirmed()`, return to Normal
   - `confirmDelete()` called → emit `deleteConfirmed()`, return to Normal

**Estimated:** ~150 lines

---

## Phase 2: Page Panel Core

### Task 2.1: PageThumbnailModel (~200 lines)

**Files:** `source/ui/PageThumbnailModel.h`, `source/ui/PageThumbnailModel.cpp`

**Description:** QAbstractListModel providing page data for QListView.

```cpp
class PageThumbnailModel : public QAbstractListModel {
    Q_OBJECT
    
public:
    enum Roles {
        PageIndexRole = Qt::UserRole + 1,
        ThumbnailRole,
        IsCurrentPageRole,
        IsPdfPageRole,
        CanDragRole
    };
    
    explicit PageThumbnailModel(QObject* parent = nullptr);
    
    // Model interface
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    
    // Drag-and-drop support
    Qt::DropActions supportedDropActions() const override;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    bool canDropMimeData(const QMimeData* data, Qt::DropAction action,
                         int row, int column, const QModelIndex& parent) const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action,
                      int row, int column, const QModelIndex& parent) override;
    
    // Document binding
    void setDocument(Document* doc);
    Document* document() const;
    
    void setCurrentPageIndex(int index);
    int currentPageIndex() const;
    
    // Thumbnail management
    QPixmap thumbnailForPage(int pageIndex) const;
    void invalidateThumbnail(int pageIndex);
    void invalidateAllThumbnails();
    
signals:
    void pageDropped(int fromIndex, int toIndex);
    void thumbnailReady(int pageIndex);
    
public slots:
    void onPageCountChanged();
    void onPageContentChanged(int pageIndex);
    
private:
    Document* m_document = nullptr;
    int m_currentPageIndex = 0;
    
    // Thumbnail cache
    mutable QHash<int, QPixmap> m_thumbnailCache;
    mutable QSet<int> m_pendingThumbnails;
    
    void requestThumbnail(int pageIndex) const;
    QPixmap renderThumbnail(int pageIndex) const;
    
    int m_thumbnailWidth = 0;  // Set based on view width
};
```

**Key features:**
- Provides page index, thumbnail pixmap, current/PDF/draggable state
- Drag-and-drop via MIME data (internal move only)
- Lazy thumbnail generation with async rendering
- Cache invalidation on content change

**Estimated:** ~200 lines

---

### Task 2.2: PageThumbnailDelegate (~250 lines)

**Files:** `source/ui/PageThumbnailDelegate.h`, `source/ui/PageThumbnailDelegate.cpp`

**Description:** Custom delegate for rendering page thumbnails in QListView.

```cpp
class PageThumbnailDelegate : public QStyledItemDelegate {
    Q_OBJECT
    
public:
    explicit PageThumbnailDelegate(QObject* parent = nullptr);
    
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;
    
    void setThumbnailWidth(int width);
    int thumbnailWidth() const;
    
    void setDarkMode(bool dark);
    
private:
    int m_thumbnailWidth = 150;
    bool m_darkMode = false;
    
    // Visual constants
    static constexpr int VERTICAL_SPACING = 12;
    static constexpr int BORDER_RADIUS = 4;
    static constexpr int BORDER_WIDTH_NORMAL = 1;
    static constexpr int BORDER_WIDTH_CURRENT = 3;
    static constexpr int PAGE_NUMBER_HEIGHT = 20;
};
```

**Rendering:**
1. Draw thumbnail pixmap (or placeholder if loading)
2. Draw border (thin neutral for normal, thick accent for current)
3. Draw page number below ("Page N")
4. Slight corner rounding (4px radius)

**Placeholder rendering:**
- Page background color (or gray for PDF)
- Page number visible
- Optional loading indicator

**Estimated:** ~250 lines

---

### Task 2.3: PagePanel Widget (~350 lines)

**Files:** `source/ui/PagePanel.h`, `source/ui/PagePanel.cpp`

**Description:** Main page panel widget containing the thumbnail list.

```cpp
class PagePanel : public QWidget {
    Q_OBJECT
    
public:
    explicit PagePanel(QWidget* parent = nullptr);
    
    void setDocument(Document* doc);
    Document* document() const;
    
    void setCurrentPageIndex(int index);
    int currentPageIndex() const;
    
    // Scroll position state (per-tab)
    int scrollPosition() const;
    void setScrollPosition(int pos);
    
    void setDarkMode(bool dark);
    
    // Invalidation
    void invalidateThumbnail(int pageIndex);
    void invalidateAllThumbnails();
    
signals:
    void pageClicked(int pageIndex);
    void pageDropped(int fromIndex, int toIndex);
    
public slots:
    void onCurrentPageChanged(int pageIndex);
    void scrollToCurrentPage();
    
private slots:
    void onItemClicked(const QModelIndex& index);
    void onThumbnailReady(int pageIndex);
    void updateThumbnailWidth();
    void onContentChanged();
    
private:
    QListView* m_listView;
    PageThumbnailModel* m_model;
    PageThumbnailDelegate* m_delegate;
    
    Document* m_document = nullptr;
    int m_currentPageIndex = 0;
    
    // Debounced invalidation
    QTimer* m_invalidationTimer = nullptr;
    QSet<int> m_pendingInvalidations;
    
    void setupUI();
    void setupConnections();
    void configureListView();
    
    // Touch scrolling (like OutlinePanel)
    void setupTouchScrolling();
};
```

**Features:**
- QListView with custom model and delegate
- Touch-friendly scrolling (QScroller)
- Auto-scroll to current page when not visible
- Debounced thumbnail invalidation (500ms)
- Drag-and-drop reorder support
- Width-responsive thumbnail sizing

**Estimated:** ~350 lines

---

## Phase 3: Thumbnail Rendering

### Task 3.1: Async Thumbnail Renderer (~200 lines)

**Files:** `source/ui/ThumbnailRenderer.h`, `source/ui/ThumbnailRenderer.cpp`

**Description:** Async thumbnail generation using Qt Concurrent.

```cpp
class ThumbnailRenderer : public QObject {
    Q_OBJECT
    
public:
    explicit ThumbnailRenderer(QObject* parent = nullptr);
    
    // Request thumbnail (returns immediately, emits ready signal later)
    void requestThumbnail(Document* doc, int pageIndex, int width, qreal dpr);
    
    // Cancel pending requests (e.g., when document changes)
    void cancelAll();
    
signals:
    void thumbnailReady(int pageIndex, QPixmap thumbnail);
    
private:
    struct RenderTask {
        Document* doc;
        int pageIndex;
        int width;
        qreal dpr;
    };
    
    QSet<int> m_pendingPages;
    
    QPixmap renderThumbnailSync(const RenderTask& task);
};
```

**Rendering process:**
1. Calculate thumbnail height from page aspect ratio
2. Calculate render DPI: `dpi = (width * dpr) / (pageWidth / 72.0)`
3. Render page background
4. If PDF page: render PDF at calculated DPI (or downscale from cache if available)
5. Render all vector layers
6. Render inserted objects
7. Return scaled pixmap

**Performance notes:**
- Use QtConcurrent::run for background rendering
- Limit concurrent renders (2-4 max)
- Cancel outdated requests when scrolling fast

**Estimated:** ~200 lines

---

### Task 3.2: Integrate Renderer with Model (~100 lines)

**Files:** Modify `source/ui/PageThumbnailModel.cpp`

**Description:** Connect ThumbnailRenderer to PageThumbnailModel.

```cpp
// In PageThumbnailModel:
void PageThumbnailModel::requestThumbnail(int pageIndex) const {
    if (m_pendingThumbnails.contains(pageIndex)) return;
    if (m_thumbnailCache.contains(pageIndex)) return;
    
    m_pendingThumbnails.insert(pageIndex);
    m_renderer->requestThumbnail(m_document, pageIndex, 
                                  m_thumbnailWidth, devicePixelRatioF());
}

void PageThumbnailModel::onThumbnailReady(int pageIndex, QPixmap thumbnail) {
    m_pendingThumbnails.remove(pageIndex);
    m_thumbnailCache[pageIndex] = thumbnail;
    
    QModelIndex idx = index(pageIndex);
    emit dataChanged(idx, idx, {ThumbnailRole});
}
```

**Estimated:** ~100 lines

---

## Phase 4: Action Bar

### Task 4.1: PagePanelActionBar (~250 lines)

**Files:** `source/ui/actionbars/PagePanelActionBar.h`, `source/ui/actionbars/PagePanelActionBar.cpp`

**Description:** Action bar for page navigation and management.

```cpp
class PagePanelActionBar : public ActionBar {  // Assuming ActionBar base exists
    Q_OBJECT
    
public:
    explicit PagePanelActionBar(QWidget* parent = nullptr);
    
    void setCurrentPage(int page);    // 0-based
    void setPageCount(int count);
    
    void setDarkMode(bool dark) override;
    
signals:
    void pageUpClicked();
    void pageDownClicked();
    void pageSelected(int page);      // From wheel picker
    void addPageClicked();
    void insertPageClicked();
    void deletePageClicked();
    void undoDeleteClicked();
    
private slots:
    void onWheelPageChanged(int page);
    void onDeleteRequested();
    void onDeleteConfirmed();
    void onUndoRequested();
    
private:
    ActionBarButton* m_pageUpButton;
    PageWheelPicker* m_wheelPicker;
    ActionBarButton* m_pageDownButton;
    ActionBarButton* m_addPageButton;
    ActionBarButton* m_insertPageButton;
    UndoDeleteButton* m_deleteButton;
    
    int m_pendingDeletePage = -1;  // Page to delete after timeout
    
    void setupUI();
    void setupConnections();
};
```

**Button icons (placeholder names):**
- Page Up: `pageUp.png` / `pageUp_reversed.png`
- Page Down: `pageDown.png` / `pageDown_reversed.png`
- Add Page: `addPage.png` / `addPage_reversed.png`
- Insert Page: `insertPage.png` / `insertPage_reversed.png`
- Delete Page: `deletePage.png` / `deletePage_reversed.png`
- Undo: `undo.png` / `undo_reversed.png`

**Estimated:** ~250 lines

---

### Task 4.2: ActionBarContainer 2-Column Support (~100 lines)

**Files:** Modify `source/ui/actionbars/ActionBarContainer.h`, `source/ui/actionbars/ActionBarContainer.cpp`

**Description:** Enable 2-column layout when PagePanelActionBar + another bar are both active.

```cpp
// Add to ActionBarContainer:

void ActionBarContainer::setPagePanelActionBar(PagePanelActionBar* bar) {
    m_pagePanelBar = bar;
    updateLayout();
}

void ActionBarContainer::updateLayout() {
    // Count visible bars
    int visibleCount = 0;
    if (m_pagePanelBar && m_pagePanelBar->isVisible()) visibleCount++;
    if (m_currentActionBar && m_currentActionBar->isVisible()) visibleCount++;
    
    if (visibleCount == 2) {
        // 2-column layout: PagePanel on left, other on right
        // Gap: 24px
        m_pagePanelBar->move(rightEdge - totalWidth, centeredY);
        m_currentActionBar->move(rightEdge - actionBarWidth, centeredY);
    } else if (visibleCount == 1) {
        // Single column on right
        ActionBar* visible = m_pagePanelBar->isVisible() ? m_pagePanelBar : m_currentActionBar;
        visible->move(rightEdge - barWidth, centeredY);
    }
}
```

**Layout specs:**
- 2-column gap: 24px logical
- PagePanelActionBar always on LEFT when 2 columns
- Both vertically centered

**Estimated:** ~100 lines

---

## Phase 5: Sidebar Integration

### Task 5.1: Add Page Panel Tab (~80 lines)

**Files:** Modify `source/MainWindow.cpp`, `source/MainWindow.h`

**Description:** Add Page Panel as new tab in left sidebar.

```cpp
// In MainWindow::setupSidebar() or similar:

// Create page panel
m_pagePanel = new PagePanel(this);

// Add to sidebar tab widget (after Outline, before Layers)
m_sidebarTabs->insertTab(1, m_pagePanel, tr("Pages"));

// Hide for edgeless documents
connect(m_tabManager, &TabManager::currentViewportChanged, this, [this](DocumentViewport* vp) {
    if (vp && vp->document()) {
        bool isEdgeless = vp->document()->isEdgeless();
        m_sidebarTabs->setTabVisible(1, !isEdgeless);  // Pages tab index
    }
});
```

**Tab order:** Outline (0) → Pages (1) → Layers (2)

**Estimated:** ~80 lines

---

### Task 5.2: Connect Page Panel Signals (~100 lines)

**Files:** Modify `source/MainWindow.cpp`

**Description:** Wire up PagePanel ↔ DocumentViewport ↔ PagePanelActionBar.

```cpp
void MainWindow::setupPagePanelConnections() {
    // PagePanel → Viewport
    connect(m_pagePanel, &PagePanel::pageClicked, this, [this](int page) {
        switchPage(page);
    });
    
    // Viewport → PagePanel
    // (Connected per-viewport in connectViewportScrollSignals)
    
    // PagePanel → PagePanelActionBar
    // Drag-and-drop
    connect(m_pagePanel, &PagePanel::pageDropped, this, [this](int from, int to) {
        if (auto* vp = currentViewport()) {
            vp->document()->movePage(from, to);
            vp->update();
            // TODO: Mark document modified
        }
    });
}

void MainWindow::connectViewportScrollSignals(DocumentViewport* vp) {
    // ... existing connections ...
    
    // Page Panel sync
    m_pagePanelConn = connect(vp, &DocumentViewport::currentPageChanged, 
                               m_pagePanel, &PagePanel::onCurrentPageChanged);
}
```

**Estimated:** ~100 lines

---

### Task 5.3: Connect PagePanelActionBar (~120 lines)

**Files:** Modify `source/MainWindow.cpp`

**Description:** Wire up PagePanelActionBar signals.

```cpp
void MainWindow::setupPagePanelActionBar() {
    m_pagePanelActionBar = new PagePanelActionBar(this);
    m_actionBarContainer->setPagePanelActionBar(m_pagePanelActionBar);
    
    // Navigation
    connect(m_pagePanelActionBar, &PagePanelActionBar::pageUpClicked,
            this, &MainWindow::goToPreviousPage);
    connect(m_pagePanelActionBar, &PagePanelActionBar::pageDownClicked,
            this, &MainWindow::goToNextPage);
    connect(m_pagePanelActionBar, &PagePanelActionBar::pageSelected,
            this, &MainWindow::switchPage);
    
    // Page management
    connect(m_pagePanelActionBar, &PagePanelActionBar::addPageClicked,
            this, &MainWindow::addPageToDocument);
    connect(m_pagePanelActionBar, &PagePanelActionBar::insertPageClicked,
            this, &MainWindow::insertPageInDocument);
    connect(m_pagePanelActionBar, &PagePanelActionBar::deletePageClicked, this, [this]() {
        // Store page index for potential undo
        m_pendingDeletePageIndex = currentViewport()->currentPageIndex();
    });
    connect(m_pagePanelActionBar, &PagePanelActionBar::deletePageClicked,
            this, &MainWindow::deletePageInDocument);
    connect(m_pagePanelActionBar, &PagePanelActionBar::undoDeleteClicked, this, [this]() {
        // TODO: Implement undo delete (need to store deleted page)
    });
    
    // Visibility: Only when Pages tab is selected
    connect(m_sidebarTabs, &QTabWidget::currentChanged, this, [this](int index) {
        bool isPagesTab = (index == 1);  // Pages tab index
        m_pagePanelActionBar->setVisible(isPagesTab);
        m_actionBarContainer->updateLayout();
    });
}
```

**Estimated:** ~120 lines

---

### Task 5.4: Action Bar Visibility Logic (~50 lines)

**Files:** Modify `source/MainWindow.cpp`

**Description:** Handle PagePanelActionBar visibility with sidebar tab switching.

```cpp
void MainWindow::updatePagePanelActionBarVisibility() {
    if (!m_pagePanelActionBar) return;
    
    bool shouldShow = false;
    
    // Check: Is Pages tab selected?
    if (m_sidebarTabs->currentIndex() == 1) {  // Pages tab
        // Check: Is it a paged document?
        if (auto* vp = currentViewport()) {
            if (vp->document() && !vp->document()->isEdgeless()) {
                shouldShow = true;
            }
        }
    }
    
    m_pagePanelActionBar->setVisible(shouldShow);
    m_actionBarContainer->updateLayout();
}
```

**Estimated:** ~50 lines

---

## Phase 6: Drag-and-Drop

### Task 6.1: Drag Initiation (~100 lines)

**Files:** Modify `source/ui/PagePanel.cpp`

**Description:** Long-press to initiate drag.

```cpp
// In PagePanel, handle long-press:

void PagePanel::setupDragDrop() {
    m_listView->setDragEnabled(true);
    m_listView->setAcceptDrops(true);
    m_listView->setDropIndicatorShown(true);
    m_listView->setDragDropMode(QAbstractItemView::InternalMove);
    
    // Long-press timer for touch
    m_longPressTimer = new QTimer(this);
    m_longPressTimer->setSingleShot(true);
    m_longPressTimer->setInterval(400);  // 400ms for touch
    
    connect(m_longPressTimer, &QTimer::timeout, this, [this]() {
        if (m_pressedIndex.isValid()) {
            startDrag(m_pressedIndex);
        }
    });
}

bool PagePanel::eventFilter(QObject* obj, QEvent* event) {
    if (obj == m_listView->viewport()) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(event);
            m_pressedIndex = m_listView->indexAt(me->pos());
            m_pressPos = me->pos();
            
            // Check if page can be dragged
            if (m_pressedIndex.isValid() && 
                m_pressedIndex.data(PageThumbnailModel::CanDragRole).toBool()) {
                m_longPressTimer->start();
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            m_longPressTimer->stop();
        } else if (event->type() == QEvent::MouseMove) {
            auto* me = static_cast<QMouseEvent*>(event);
            if ((me->pos() - m_pressPos).manhattanLength() > 10) {
                m_longPressTimer->stop();  // Cancel if moved too much
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}
```

**Estimated:** ~100 lines

---

### Task 6.2: Drop Indicator & Visual Feedback (~80 lines)

**Files:** Modify `source/ui/PageThumbnailDelegate.cpp`

**Description:** Custom drop indicator (gap opening up).

```cpp
void PageThumbnailDelegate::paint(QPainter* painter, 
                                   const QStyleOptionViewItem& option,
                                   const QModelIndex& index) const {
    // ... existing thumbnail painting ...
    
    // Draw drop indicator (gap)
    if (option.state & QStyle::State_DropIndicator) {
        // Draw horizontal line or expanded gap
        painter->setPen(QPen(accentColor, 2));
        painter->drawLine(option.rect.topLeft(), option.rect.topRight());
    }
}
```

**Estimated:** ~80 lines

---

### Task 6.3: Auto-scroll During Drag (~50 lines)

**Files:** Modify `source/ui/PagePanel.cpp`

**Description:** Auto-scroll when dragging near edges.

```cpp
void PagePanel::setupAutoScroll() {
    m_autoScrollTimer = new QTimer(this);
    m_autoScrollTimer->setInterval(50);  // 20 FPS
    
    connect(m_autoScrollTimer, &QTimer::timeout, this, [this]() {
        if (!m_isDragging) {
            m_autoScrollTimer->stop();
            return;
        }
        
        QPoint pos = m_listView->viewport()->mapFromGlobal(QCursor::pos());
        int viewHeight = m_listView->viewport()->height();
        int scrollSpeed = 0;
        
        if (pos.y() < 50) {
            // Near top - scroll up
            scrollSpeed = -qMin(10, (50 - pos.y()) / 5);
        } else if (pos.y() > viewHeight - 50) {
            // Near bottom - scroll down
            scrollSpeed = qMin(10, (pos.y() - (viewHeight - 50)) / 5);
        }
        
        if (scrollSpeed != 0) {
            m_listView->verticalScrollBar()->setValue(
                m_listView->verticalScrollBar()->value() + scrollSpeed);
        }
    });
}
```

**Estimated:** ~50 lines

---

## Phase 7: Polish & Integration

### Task 7.1: Dark Mode Support (~50 lines)

**Files:** Modify `source/MainWindow.cpp`

**Description:** Propagate dark mode to all Page Panel components.

```cpp
void MainWindow::updateTheme() {
    bool darkMode = isDarkMode();
    
    // ... existing theme updates ...
    
    // Page Panel components
    if (m_pagePanel) {
        m_pagePanel->setDarkMode(darkMode);
    }
    if (m_pagePanelActionBar) {
        m_pagePanelActionBar->setDarkMode(darkMode);
    }
}
```

**Estimated:** ~50 lines

---

### Task 7.2: Per-Tab State (~60 lines)

**Files:** Modify `source/ui/PagePanel.cpp`

**Description:** Save/restore scroll position per tab.

```cpp
void PagePanel::saveTabState(int tabIndex) {
    m_tabScrollPositions[tabIndex] = m_listView->verticalScrollBar()->value();
}

void PagePanel::restoreTabState(int tabIndex) {
    if (m_tabScrollPositions.contains(tabIndex)) {
        m_listView->verticalScrollBar()->setValue(m_tabScrollPositions[tabIndex]);
    } else {
        // New tab - scroll to current page
        scrollToCurrentPage();
    }
}

void PagePanel::clearTabState(int tabIndex) {
    m_tabScrollPositions.remove(tabIndex);
}
```

**Estimated:** ~60 lines

---

### Task 7.3: Cache Invalidation (~80 lines)

**Files:** Modify `source/ui/PagePanel.cpp`, `source/ui/PageThumbnailModel.cpp`

**Description:** Debounced thumbnail invalidation.

```cpp
void PagePanel::onContentChanged() {
    // Called when strokes change, objects added, etc.
    if (!m_invalidationTimer->isActive()) {
        m_invalidationTimer->start(500);  // 500ms debounce
    }
}

void PagePanel::performInvalidation() {
    if (isVisible()) {
        // Invalidate visible + buffer pages
        int firstVisible = /* calculate */;
        int lastVisible = /* calculate */;
        
        for (int i = firstVisible - 2; i <= lastVisible + 2; ++i) {
            m_model->invalidateThumbnail(i);
        }
    } else {
        // Mark all as needing refresh when panel becomes visible
        m_needsFullRefresh = true;
    }
}

void PagePanel::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    
    if (m_needsFullRefresh) {
        m_model->invalidateAllThumbnails();
        m_needsFullRefresh = false;
    }
}
```

**Estimated:** ~80 lines

---

## File Structure

```
source/ui/
├── widgets/
│   ├── PageWheelPicker.h           ← NEW (~300 lines)
│   ├── PageWheelPicker.cpp
│   ├── UndoDeleteButton.h          ← NEW (~150 lines)
│   └── UndoDeleteButton.cpp
├── actionbars/
│   ├── ActionBarContainer.h        ← MODIFY (+100 lines)
│   ├── ActionBarContainer.cpp
│   ├── PagePanelActionBar.h        ← NEW (~250 lines)
│   └── PagePanelActionBar.cpp
├── PagePanel.h                     ← NEW (~350 lines)
├── PagePanel.cpp
├── PageThumbnailModel.h            ← NEW (~200 lines)
├── PageThumbnailModel.cpp
├── PageThumbnailDelegate.h         ← NEW (~250 lines)
├── PageThumbnailDelegate.cpp
├── ThumbnailRenderer.h             ← NEW (~200 lines)
└── ThumbnailRenderer.cpp

source/core/
└── DocumentTests.h                 ← MODIFY (+50 lines)

source/
└── MainWindow.cpp                  ← MODIFY (+400 lines)
```

---

## Task Summary

| Phase | Task | Description | Est. Lines |
|-------|------|-------------|------------|
| **0** | 0.1 | Test movePage() | ~50 |
| **1** | 1.1 | PageWheelPicker widget | ~300 |
| | 1.2 | UndoDeleteButton widget | ~150 |
| **2** | 2.1 | PageThumbnailModel | ~200 |
| | 2.2 | PageThumbnailDelegate | ~250 |
| | 2.3 | PagePanel widget | ~350 |
| **3** | 3.1 | ThumbnailRenderer | ~200 |
| | 3.2 | Integrate renderer | ~100 |
| **4** | 4.1 | PagePanelActionBar | ~250 |
| | 4.2 | ActionBarContainer 2-col | ~100 |
| **5** | 5.1 | Add sidebar tab | ~80 |
| | 5.2 | Connect PagePanel signals | ~100 |
| | 5.3 | Connect ActionBar signals | ~120 |
| | 5.4 | Visibility logic | ~50 |
| **6** | 6.1 | Drag initiation | ~100 |
| | 6.2 | Drop indicator | ~80 |
| | 6.3 | Auto-scroll | ~50 |
| **7** | 7.1 | Dark mode | ~50 |
| | 7.2 | Per-tab state | ~60 |
| | 7.3 | Cache invalidation | ~80 |
| **Total** | | | **~2720 lines** |

---

## Implementation Order

```
Phase 0: Prerequisites
    └── Task 0.1: Test movePage()
         ↓
Phase 1: Core Widgets (can be parallel)
    ├── Task 1.1: PageWheelPicker
    └── Task 1.2: UndoDeleteButton
         ↓
Phase 2: Page Panel Core
    ├── Task 2.1: PageThumbnailModel
    ├── Task 2.2: PageThumbnailDelegate
    └── Task 2.3: PagePanel
         ↓
Phase 3: Thumbnail Rendering
    ├── Task 3.1: ThumbnailRenderer
    └── Task 3.2: Integrate with model
         ↓
Phase 4: Action Bar
    ├── Task 4.1: PagePanelActionBar
    └── Task 4.2: ActionBarContainer 2-column
         ↓
Phase 5: Sidebar Integration
    ├── Task 5.1: Add tab
    ├── Task 5.2: Connect PagePanel
    ├── Task 5.3: Connect ActionBar
    └── Task 5.4: Visibility logic
         ↓
Phase 6: Drag-and-Drop
    ├── Task 6.1: Drag initiation
    ├── Task 6.2: Drop indicator
    └── Task 6.3: Auto-scroll
         ↓
Phase 7: Polish
    ├── Task 7.1: Dark mode
    ├── Task 7.2: Per-tab state
    └── Task 7.3: Cache invalidation
```

---

## Success Criteria

Phase complete when:

1. ✅ Page Panel tab appears in sidebar (hidden for edgeless)
2. ✅ Thumbnails render asynchronously with lazy loading
3. ✅ Click thumbnail → navigates to page
4. ✅ Current page has accent border highlight
5. ✅ PageWheelPicker scrolls with inertia and snaps
6. ✅ Page Up/Down buttons work
7. ✅ Add/Insert/Delete page buttons work
8. ✅ Delete button transforms to undo state for 5 seconds
9. ✅ Drag-and-drop reorder works (long-press to initiate)
10. ✅ PDF pages cannot be dragged (only inserted pages)
11. ✅ 2-column action bar layout works when both bars visible
12. ✅ Dark/light mode switching works
13. ✅ Per-tab scroll position preserved
14. ✅ Thumbnail cache invalidates on content change

---

## Dependencies

- `source/ui/actionbars/ActionBar.h` - Base class for action bars
- `source/ui/actionbars/ActionBarContainer.h` - Container for positioning
- `source/core/Document.h` - `movePage()`, `pageCount()`, etc.
- `source/core/DocumentViewport.h` - `currentPageIndex()`, `scrollToPage()`
- `source/ui/SidebarContainer.h` (or equivalent) - Tab widget for sidebar

---

## Notes

### Performance Considerations

- **Intel Z3735E target:** Keep thumbnail rendering lightweight
- Limit concurrent async renders to 2-4
- Use lower DPI for thumbnails (adaptive to widget size)
- Cancel outdated render requests when scrolling fast

### PDF Document Restrictions

- Cannot delete PDF background pages
- Cannot reorder PDF pages via drag-and-drop
- CAN insert blank pages between PDF pages
- CAN drag inserted (non-PDF) pages

### Future Enhancements

- Multi-select for batch operations
- Duplicate page functionality
- Page templates
- Thumbnail preview on hover

---

## Bug Fixes

### BF-1: Page Panel Scroll Bouncing Back (Fixed)

**Symptom:** When scrolling the PagePanel thumbnail list to view pages far from the current page (e.g., scrolling from page 8 to page 300), the list would immediately bounce back to the current page area.

**Root Causes Identified:**

1. **Aggressive auto-scroll in `onCurrentPageChanged()`**: Every time the viewport's current page changed (which happens during viewport scrolling), the PagePanel would auto-scroll to show that page, overriding user's manual scroll position.

2. **Auto-scroll in `showEvent()`**: Every time the panel became visible, it would scroll to the current page, which interfered with tab switching and panel toggling.

3. **Batched layout mode**: `QListView::Batched` layout mode was causing layout recalculations that could reset scroll position during batched item layout passes.

**Fixes Applied:**

1. **Smart auto-scroll logic** (`PagePanel::onCurrentPageChanged()`):
   ```cpp
   // Only auto-scroll if the current page is completely OFF-SCREEN
   QRect itemRect = m_listView->visualRect(index);
   QRect viewRect = m_listView->viewport()->rect();
   if (!viewRect.intersects(itemRect)) {
       scrollToCurrentPage();  // Page was off-screen, scroll to it
   }
   // If page is already visible (even partially), don't scroll
   ```

2. **Disabled auto-scroll in `showEvent()`**: The panel no longer forces a scroll-to-current-page when becoming visible. User's scroll position is preserved.

3. **Changed to SinglePass layout mode**:
   ```cpp
   // Was: m_listView->setLayoutMode(QListView::Batched);
   m_listView->setLayoutMode(QListView::SinglePass);
   ```

**Performance Notes:**

The change from `Batched` to `SinglePass` layout mode does NOT significantly impact performance because:

- **Item sizes are constant**: `PageThumbnailDelegate::sizeHint()` returns fixed dimensions based on `thumbnailWidth × aspectRatio`, not on actual thumbnail content
- **Thumbnails load lazily**: Async rendering is unaffected by layout mode
- **No content-dependent layout**: Unlike text-based lists where content affects height, thumbnail items have uniform calculated sizes

For a 3651-page PDF document, scrolling performance remains smooth after this fix.

**Files Modified:**
- `source/ui/PagePanel.cpp`
- `source/ui/PageThumbnailModel.cpp` (debug output only)

---

*Subplan created for SpeedyNote Page Panel implementation*

