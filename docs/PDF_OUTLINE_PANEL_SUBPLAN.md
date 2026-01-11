# PDF Outline Panel - Implementation Subplan

**Document Version:** 1.0  
**Date:** January 10, 2026  
**Status:** 🔵 READY TO IMPLEMENT  
**Prerequisites:** Phase D complete (Subtoolbars, Action Bars)  
**References:** `docs/PDF_OUTLINE_PANEL_QA.md` for design decisions

---

## Overview

Implement a PDF Outline Panel that displays the table of contents from PDF documents, allowing users to navigate by clicking entries and highlighting the current section as they scroll.

### Final Design Summary

| Aspect | Decision |
|--------|----------|
| Widget | QTreeWidget with custom item delegate |
| Navigation | Jump to exact position (page + position within page) |
| Page display | Right-aligned with leader dots |
| Item height | 36px logical (touch-friendly) |
| Touch | Single tap text = navigate, tap arrow = expand/collapse |
| Tab order | `[Outline] [Layers]` |
| Visibility | Only show tab when PDF has outline |

---

## Phase 1: Data Structure Extensions

### Task 1.1: Extend PdfOutlineItem

**File:** `source/pdf/PdfProvider.h`

**Changes:**
```cpp
struct PdfOutlineItem {
    QString title;                          // Display title
    int targetPage = -1;                    // Target page (0-based), -1 if none
    QPointF targetPosition = QPointF(-1, -1); // NEW: normalized position (0,0)-(1,1)
    qreal targetZoom = -1;                  // NEW: suggested zoom, -1 = unchanged
    bool isOpen = false;                    // Whether item is expanded by default
    QVector<PdfOutlineItem> children;       // Child items
};
```

**Estimated:** ~10 lines changed

---

### Task 1.2: Update PopplerPdfProvider

**File:** `source/pdf/PopplerPdfProvider.cpp`

**Changes:**
- Update `outline()` method to extract position data from Poppler's `LinkDestination`
- Poppler provides: `destination->left()`, `destination->top()`, `destination->zoom()`

**Implementation:**
```cpp
PdfOutlineItem convertOutlineItem(const Poppler::OutlineItem& popplerItem) {
    PdfOutlineItem item;
    item.title = popplerItem.name();
    item.isOpen = popplerItem.isOpen();
    
    if (auto dest = popplerItem.destination()) {
        item.targetPage = dest->pageNumber() - 1;  // Convert to 0-based
        
        // Extract position if available
        if (dest->isChangeLeft() || dest->isChangeTop()) {
            item.targetPosition = QPointF(
                dest->isChangeLeft() ? dest->left() : -1,
                dest->isChangeTop() ? dest->top() : -1
            );
        }
        
        // Extract zoom if available
        if (dest->isChangeZoom()) {
            item.targetZoom = dest->zoom();
        }
    }
    
    // Recursively process children
    for (const auto& child : popplerItem.children()) {
        item.children.append(convertOutlineItem(child));
    }
    
    return item;
}
```

**Estimated:** ~50 lines changed

---

## Phase 2: OutlinePanel Widget

### Task 2.1: OutlinePanel Class

**Files:** `source/ui/sidebars/OutlinePanel.h`, `source/ui/sidebars/OutlinePanel.cpp`

**Class Structure:**
```cpp
class OutlinePanel : public QWidget {
    Q_OBJECT
    
public:
    explicit OutlinePanel(QWidget* parent = nullptr);
    
    // Load outline data
    void setOutline(const QVector<PdfOutlineItem>& outline);
    void clearOutline();
    
    // Highlight current section based on page
    void highlightPage(int pageIndex);
    
    // State management for multi-tab
    void saveState();
    void restoreState();
    
signals:
    // Emitted when user clicks an outline item
    void navigationRequested(int pageIndex, QPointF position);
    
private slots:
    void onItemClicked(QTreeWidgetItem* item, int column);
    void onItemExpanded(QTreeWidgetItem* item);
    void onItemCollapsed(QTreeWidgetItem* item);
    
private:
    void setupUi();
    void populateTree(const QVector<PdfOutlineItem>& items, QTreeWidgetItem* parent = nullptr);
    QTreeWidgetItem* findItemForPage(int pageIndex);
    
    QTreeWidget* m_tree = nullptr;
    QVector<PdfOutlineItem> m_outline;  // Cached for state restoration
    
    // State per document (session only)
    QSet<QString> m_expandedItems;  // Track expanded items by path
    int m_lastHighlightedPage = -1;
};
```

**Features:**
- QTreeWidget with custom styling
- Touch-friendly (36px row height, kinetic scrolling)
- Stores outline data for state restoration
- Tracks expanded items per session

**Estimated:** ~150 lines

---

### Task 2.2: Custom Item Delegate

**Files:** `source/ui/sidebars/OutlineItemDelegate.h`, `source/ui/sidebars/OutlineItemDelegate.cpp`

**Purpose:** Draw outline items with right-aligned page numbers and leader dots.

**Class Structure:**
```cpp
class OutlineItemDelegate : public QStyledItemDelegate {
    Q_OBJECT
    
public:
    explicit OutlineItemDelegate(QObject* parent = nullptr);
    
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
               
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;
                   
private:
    static constexpr int ROW_HEIGHT = 36;
    static constexpr int PAGE_NUMBER_WIDTH = 40;
    static constexpr int PADDING = 8;
};
```

**Paint logic:**
1. Draw selection/hover background
2. Draw expand/collapse arrow (if has children)
3. Draw title text (left-aligned, elided if needed)
4. Draw leader dots (stretch between title and page number)
5. Draw page number (right-aligned)

**Estimated:** ~120 lines

---

### Task 2.3: Touch Support

**File:** `source/ui/sidebars/OutlinePanel.cpp`

**Implementation:**
```cpp
void OutlinePanel::setupUi() {
    m_tree = new QTreeWidget(this);
    
    // Enable kinetic scrolling for touch
    QScroller::grabGesture(m_tree->viewport(), QScroller::LeftMouseButtonGesture);
    m_tree->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_tree->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    
    // Touch-friendly settings
    m_tree->setIndentation(20);  // Space for expand arrows
    m_tree->setRootIsDecorated(true);
    m_tree->setHeaderHidden(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    
    // Custom delegate for styling
    m_tree->setItemDelegate(new OutlineItemDelegate(this));
    
    // ... layout setup
}
```

**Estimated:** Included in Task 2.1

---

## Phase 3: LeftSidebarContainer Integration

### Task 3.1: Add OutlinePanel to Container

**Files:** `source/ui/sidebars/LeftSidebarContainer.h`, `source/ui/sidebars/LeftSidebarContainer.cpp`

**Changes to Header:**
```cpp
class OutlinePanel;  // Forward declaration

class LeftSidebarContainer : public QTabWidget {
    // ...
public:
    OutlinePanel* outlinePanel() const { return m_outlinePanel; }
    
    // Dynamic tab management
    void showOutlineTab(bool show);
    bool hasOutlineTab() const;
    
private:
    LayerPanel* m_layerPanel = nullptr;
    OutlinePanel* m_outlinePanel = nullptr;
    int m_outlineTabIndex = -1;  // -1 = not added
};
```

**Changes to Implementation:**
```cpp
void LeftSidebarContainer::setupUi() {
    setTabPosition(QTabWidget::West);
    setDocumentMode(true);
    
    // Create panels (but don't add Outline tab yet)
    m_outlinePanel = new OutlinePanel(this);
    m_layerPanel = new LayerPanel(this);
    
    // Only add Layers tab initially
    addTab(m_layerPanel, tr("Layers"));
}

void LeftSidebarContainer::showOutlineTab(bool show) {
    if (show && m_outlineTabIndex == -1) {
        // Insert at position 0 (before Layers)
        m_outlineTabIndex = insertTab(0, m_outlinePanel, tr("Outline"));
        setCurrentIndex(0);  // Switch to Outline tab
    } else if (!show && m_outlineTabIndex != -1) {
        removeTab(m_outlineTabIndex);
        m_outlineTabIndex = -1;
    }
}
```

**Estimated:** ~50 lines

---

## Phase 4: MainWindow Integration

### Task 4.1: Signal Connections

**File:** `source/MainWindow.cpp`

**New connections needed:**
```cpp
void MainWindow::setupOutlinePanelConnections() {
    OutlinePanel* outlinePanel = m_leftSidebar->outlinePanel();
    
    // Navigation: OutlinePanel → DocumentViewport
    connect(outlinePanel, &OutlinePanel::navigationRequested,
            this, [this](int pageIndex, QPointF position) {
        if (DocumentViewport* vp = currentViewport()) {
            vp->scrollToPage(pageIndex);
            // TODO: If position is valid, scroll to position within page
            if (position.x() >= 0 || position.y() >= 0) {
                vp->scrollToPositionOnPage(pageIndex, position);
            }
        }
    });
}
```

**Estimated:** ~30 lines

---

### Task 4.2: Page Change Tracking

**File:** `source/MainWindow.cpp`

**Connect viewport page changes to outline highlighting:**
```cpp
// In connectViewportScrollSignals() or similar:
void MainWindow::connectOutlineSignals(DocumentViewport* viewport) {
    // Disconnect previous
    if (m_outlinePageConn) {
        disconnect(m_outlinePageConn);
    }
    
    // Connect new viewport
    OutlinePanel* outlinePanel = m_leftSidebar->outlinePanel();
    m_outlinePageConn = connect(viewport, &DocumentViewport::currentPageChanged,
                                 outlinePanel, &OutlinePanel::highlightPage);
    
    // Sync current state
    outlinePanel->highlightPage(viewport->currentPageIndex());
}
```

**Estimated:** ~20 lines

---

### Task 4.3: Document/Tab Change Handling

**File:** `source/MainWindow.cpp`

**When document changes or tab switches:**
```cpp
void MainWindow::updateOutlinePanelForDocument(Document* doc) {
    OutlinePanel* outlinePanel = m_leftSidebar->outlinePanel();
    
    if (!doc || !doc->hasPdf()) {
        // No PDF - hide outline tab
        m_leftSidebar->showOutlineTab(false);
        outlinePanel->clearOutline();
        return;
    }
    
    PdfProvider* pdf = doc->pdfProvider();
    if (!pdf || !pdf->hasOutline()) {
        // PDF without outline - hide tab
        m_leftSidebar->showOutlineTab(false);
        outlinePanel->clearOutline();
        return;
    }
    
    // PDF with outline - show tab and load data
    QVector<PdfOutlineItem> outline = pdf->outline();
    outlinePanel->setOutline(outline);
    m_leftSidebar->showOutlineTab(true);
}
```

**Call this from:**
- `onCurrentViewportChanged()` (tab switch)
- Document load handlers

**Estimated:** ~40 lines

---

### Task 4.4: DocumentViewport Position Scrolling

**File:** `source/core/DocumentViewport.h`, `source/core/DocumentViewport.cpp`

**New method needed:**
```cpp
// In DocumentViewport.h:
void scrollToPositionOnPage(int pageIndex, QPointF normalizedPosition);

// In DocumentViewport.cpp:
void DocumentViewport::scrollToPositionOnPage(int pageIndex, QPointF normalizedPosition) {
    if (!m_document || pageIndex < 0 || pageIndex >= m_document->pageCount()) {
        return;
    }
    
    // Get page size
    QSizeF pageSize = m_document->pageSizeAt(pageIndex);
    
    // Convert normalized position to document coordinates
    QPointF pagePos = pagePosition(pageIndex);
    qreal targetY = pagePos.y();
    
    if (normalizedPosition.y() >= 0) {
        // PDF coordinates: (0,0) is bottom-left, we need top-left
        targetY += (1.0 - normalizedPosition.y()) * pageSize.height();
    }
    
    qreal targetX = pagePos.x();
    if (normalizedPosition.x() >= 0) {
        targetX += normalizedPosition.x() * pageSize.width();
    }
    
    // Scroll to position (centered in viewport)
    QPointF newPan(
        targetX - width() / (2.0 * m_zoomLevel),
        targetY - height() / (2.0 * m_zoomLevel)
    );
    
    setPanOffset(newPan);
}
```

**Estimated:** ~30 lines

---

## Phase 5: Styling & Polish

### Task 5.1: Theme Support

**File:** `source/ui/sidebars/OutlinePanel.cpp`

**Apply consistent styling:**
```cpp
void OutlinePanel::updateTheme(bool darkMode) {
    QString bgColor = darkMode ? "#2D2D2D" : "#F5F5F5";
    QString textColor = darkMode ? "#E0E0E0" : "#333333";
    QString selectedBg = darkMode ? "#3D3D3D" : "#E0E0E0";
    QString hoverBg = darkMode ? "#353535" : "#EAEAEA";
    
    m_tree->setStyleSheet(QString(R"(
        QTreeWidget {
            background-color: %1;
            color: %2;
            border: none;
            outline: none;
        }
        QTreeWidget::item {
            height: 36px;
            padding: 4px;
        }
        QTreeWidget::item:selected {
            background-color: %3;
        }
        QTreeWidget::item:hover:!selected {
            background-color: %4;
        }
    )").arg(bgColor, textColor, selectedBg, hoverBg));
}
```

**Estimated:** ~40 lines

---

### Task 5.2: Expand/Collapse Arrows

**File:** `source/ui/sidebars/OutlineItemDelegate.cpp`

**Use provided arrow icons:**
- `:/resources/icons/arrow_down.png` (expanded)
- `:/resources/icons/arrow_right.png` (collapsed) - or rotate down arrow
- `_reversed.png` variants for dark mode

**Estimated:** ~20 lines (in delegate paint)

---

### Task 5.3: Tooltip for Long Titles

**File:** `source/ui/sidebars/OutlinePanel.cpp`

**When populating tree:**
```cpp
void OutlinePanel::populateTree(...) {
    // ...
    QTreeWidgetItem* item = new QTreeWidgetItem();
    item->setText(0, outlineItem.title);
    item->setToolTip(0, outlineItem.title);  // Full title on hover
    item->setData(0, Qt::UserRole, outlineItem.targetPage);
    item->setData(0, Qt::UserRole + 1, outlineItem.targetPosition);
    // ...
}
```

**Estimated:** Included in Task 2.1

---

## Phase 6: Testing & Future

### Task 6.1: Testing Checklist

- [ ] Outline loads correctly for PDF with TOC
- [ ] Outline tab hidden for non-PDF documents
- [ ] Outline tab hidden for PDF without TOC
- [ ] Click item → navigates to correct page
- [ ] Click item → scrolls to position (if available)
- [ ] Re-click selected item → re-scrolls
- [ ] Scroll document → outline highlights current section
- [ ] Expand/collapse works via arrow tap
- [ ] Touch scrolling (kinetic) works
- [ ] Dark/light mode styling correct
- [ ] Tab switching preserves outline state
- [ ] Multi-document tabs work correctly
- [ ] Long titles show tooltip
- [ ] Page numbers display correctly with leader dots

### Task 6.2: Future Enhancements (Not in Initial Scope)

- [ ] Search/filter box at top of panel
- [ ] Context menu (long-press): Copy title, Expand all
- [ ] Keyboard navigation (arrow keys)
- [ ] "Collapse all" / "Expand all" buttons

---

## File Summary

### New Files
```
source/ui/sidebars/
├── OutlinePanel.h          (~50 lines)
├── OutlinePanel.cpp        (~200 lines)
├── OutlineItemDelegate.h   (~30 lines)
└── OutlineItemDelegate.cpp (~120 lines)
```

### Modified Files
```
source/pdf/PdfProvider.h           (~10 lines changed)
source/pdf/PopplerPdfProvider.cpp  (~50 lines changed)
source/ui/sidebars/LeftSidebarContainer.h   (~15 lines added)
source/ui/sidebars/LeftSidebarContainer.cpp (~40 lines added)
source/MainWindow.h                (~10 lines added)
source/MainWindow.cpp              (~80 lines added)
source/core/DocumentViewport.h     (~5 lines added)
source/core/DocumentViewport.cpp   (~30 lines added)
CMakeLists.txt                     (~4 lines added)
```

### Total Estimated
- **New code:** ~400 lines
- **Modified code:** ~240 lines
- **Total:** ~640 lines

---

## Implementation Order

1. **Phase 1** - Data structure extensions (PdfOutlineItem, PopplerPdfProvider)
2. **Phase 2** - OutlinePanel widget and delegate
3. **Phase 3** - LeftSidebarContainer integration
4. **Phase 4** - MainWindow signal connections
5. **Phase 5** - Styling and polish
6. **Phase 6** - Testing

---

## Dependencies

- `source/pdf/PdfProvider.h` - Already exists
- `source/pdf/PopplerPdfProvider.cpp` - Already exists
- `source/ui/sidebars/LeftSidebarContainer.h/cpp` - Already exists
- `source/core/DocumentViewport.h/cpp` - Already exists
- Qt components: QTreeWidget, QStyledItemDelegate, QScroller

