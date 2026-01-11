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

### Task 1.1: Extend PdfOutlineItem ✅ COMPLETE

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

### Task 1.2: Update PopplerPdfProvider ✅ COMPLETE

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

### Task 2.1: OutlinePanel Class ✅ COMPLETE

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

### Task 2.2: Custom Item Delegate ✅ COMPLETE

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

### Task 3.1: Add OutlinePanel to Container ✅ COMPLETE

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

### Task 4.1: Signal Connections ✅ COMPLETE

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

### Task 4.2: Page Change Tracking ✅ COMPLETE

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

### Task 4.3: Document/Tab Change Handling ✅ COMPLETE

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

### Task 4.4: DocumentViewport Position Scrolling ✅ COMPLETE

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

### Task 5.1: Theme Support ✅ COMPLETE (Already Implemented)

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

### Task 5.2: Expand/Collapse Arrows ✅ COMPLETE

**File:** `source/ui/sidebars/OutlinePanel.cpp`

**Uses existing arrow icons via QSS:**
- `:/resources/icons/right_arrow.png` (collapsed)
- `:/resources/icons/down_arrow.png` (expanded)
- `_reversed.png` variants for dark mode

**Implementation:** Arrow icons are set in `updateTheme()` stylesheet, not in delegate paint.

**Estimated:** ~20 lines (in delegate paint)

---

### Task 5.3: Tooltip for Long Titles ✅ COMPLETE (Already Implemented)

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

**Status:** Implemented in Task 2.1 (line 104 of OutlinePanel.cpp)

---

## Bug Fixes (Post-Implementation)

### Fix 5.1: Mouse Clicks Not Working / Treated as Touch Gestures ✅
**Status:** FIXED

**Problem:** Mouse inputs were being mistreated as touch inputs:
- Mouse clicks on entries didn't navigate
- Hold and drag worked with mouse (which shouldn't happen)
- Stylus inputs triggered gesture navigation

**Root Cause:** `QScroller::grabGesture()` was using `LeftMouseButtonGesture` which intercepts ALL left mouse button events (including actual mouse clicks) and treats them as touch gestures. This prevented normal click handling.

**Fix:** Changed from `LeftMouseButtonGesture` to `TouchGesture`:
```cpp
// Before (bug):
QScroller::grabGesture(m_tree->viewport(), QScroller::LeftMouseButtonGesture);

// After (fixed):
QScroller::grabGesture(m_tree->viewport(), QScroller::TouchGesture);
```

**Correct Behavior:**
- Mouse clicks work normally (single click navigates)
- Touch scrolling still works with kinetic scrolling
- Stylus clicks work like mouse (no gesture interference)
- Touch drag enables kinetic scrolling

**Files Fixed:**
- `source/ui/sidebars/OutlinePanel.cpp`

---

### Fix 5.2: Hover Effects Not Working (Mouse/Stylus) ✅
**Status:** FIXED

**Problems:**
1. Stylus hovering didn't work (should show hover effect)
2. Stylus dragging acted as hovering
3. Mouse hovering didn't work, except after using mouse wheel

**Root Cause:** Mouse tracking was not enabled on the `QTreeWidget` or its viewport. By default, Qt widgets only receive mouse move events when a button is pressed. For hover effects to work, mouse tracking must be enabled so the widget receives move events even without button press.

**Fix:** Added mouse tracking and hover attributes:
```cpp
// Enable mouse tracking for proper hover effects (mouse, stylus)
m_tree->setMouseTracking(true);
m_tree->viewport()->setMouseTracking(true);
m_tree->setAttribute(Qt::WA_Hover, true);
m_tree->viewport()->setAttribute(Qt::WA_Hover, true);
```

**Why both widget AND viewport:**
- `QTreeWidget` is a subclass of `QAbstractItemView` which uses a viewport widget for actual content display
- Mouse events go to the viewport, so it needs tracking enabled
- Setting on both ensures consistent behavior

**Correct Behavior:**
- Mouse hover shows highlight on items
- Stylus proximity/hover shows highlight on items
- Dragging no longer triggers hover (proper event separation)

**Files Fixed:**
- `source/ui/sidebars/OutlinePanel.cpp`

---

### Fix 5.3: OutlinePanel Gray Block When No PDF Loaded ✅
**Status:** FIXED

**Problem:** When no PDF was loaded (or PDF without outline), a large gray block appeared on the left sidebar, blocking the layer panel. The OutlinePanel should be invisible when not added to a tab.

**Root Cause:** The `OutlinePanel` was created as a child of `LeftSidebarContainer` but not added to any tab initially. In Qt, child widgets are visible by default when their parent is visible. Since the panel wasn't in a tab but was still a visible child widget, it rendered as a gray block overlapping other content.

**Fix:** Explicitly hide/show the OutlinePanel:
```cpp
// In setupUi():
m_outlinePanel = new OutlinePanel(this);
m_outlinePanel->hide();  // Hide until added to tab

// In showOutlineTab():
if (show && m_outlineTabIndex == -1) {
    m_outlinePanel->show();  // Show when adding to tab
    m_outlineTabIndex = insertTab(0, m_outlinePanel, tr("Outline"));
    // ...
} else if (!show && m_outlineTabIndex != -1) {
    removeTab(m_outlineTabIndex);
    m_outlinePanel->hide();  // Hide when removing from tab
    // ...
}
```

**Correct Behavior:**
- Non-PDF document → No gray block, only Layers tab visible
- PDF without outline → No gray block, only Layers tab visible
- PDF with outline → Outline tab appears and is visible

**Files Fixed:**
- `source/ui/sidebars/LeftSidebarContainer.cpp`

---

### Fix 5.4: Outline Navigation Position Off by ~3/4 Page + Not Centered ✅
**Status:** FIXED

**Problems:**
1. Clicking outline items positioned ~3/4 page lower than intended
2. Pages were not horizontally centered after navigation

**Root Causes:**

1. **Y position offset**: Poppler's `LinkDestination::left()` and `top()` return PDF coordinates in **points** (e.g., 0-792 for US Letter), NOT normalized 0-1 values. The code was treating raw point values as normalized, causing huge offsets. For example, if `dest->top()` returned 700 (near top of a 792pt page), the code treated it as normalized 700.0, resulting in `(1.0 - 700) * pageHeight` = massive negative offset.

2. **X centering**: `scrollToPositionOnPage()` didn't call `recenterHorizontally()` like `scrollToPage()` does. PDF outlines typically don't specify X positions (they're -1), so pages should use normal horizontal centering.

**Fixes:**

1. **PopplerPdfProvider::convertOutlineItem()** - Now properly normalizes coordinates:
```cpp
// Get page size to normalize coordinates
QSizeF pageSizePoints = pageSize(item.targetPage);

if (dest->isChangeLeft() && pageSizePoints.width() > 0) {
    posX = dest->left() / pageSizePoints.width();  // Normalize to 0-1
}
if (dest->isChangeTop() && pageSizePoints.height() > 0) {
    // PDF Y=0 at bottom, our Y=0 at top, so invert
    posY = 1.0 - (dest->top() / pageSizePoints.height());
}
```

2. **DocumentViewport::scrollToPositionOnPage()** - Updated to:
   - Use pre-normalized Y coordinates directly (no double inversion)
   - Position target near top of viewport (not centered) for natural reading flow
   - Call `recenterHorizontally()` for proper page centering

**Coordinate System Summary:**
- **Poppler's top()**: Returns ALREADY NORMALIZED coordinates (0-1 range), NOT PDF points!
  - `top() = 0.0` → top of page
  - `top() = 0.5` → middle of page  
  - `top() = 1.0` → bottom of page
- **PdfOutlineItem**: Normalized 0-1, Y=0 at top, Y=1 at bottom
- **DocumentViewport**: Y increases downward, coordinates in logical pixels

**Fix 5.4b: Y Position Off by Exactly 1 Page (Double Inversion Bug)**

After the initial fix, navigation was still exactly 1 page off. Root cause: I incorrectly assumed Poppler returned PDF-style coordinates (Y=0 at bottom) and added an inversion. But Poppler's `LinkDestination::top()` already returns screen-style coordinates (Y=0 at top, like Qt), so the inversion caused positions to be flipped (top→bottom, bottom→top).

**Fix:** Removed the `1.0 - ...` inversion from the Y normalization.

**Fix 5.4c: Positions Always at Top of Page (Wrong Normalization)**

After Fix 5.4b, all positions went to the top of the page even though the debug showed correct raw `top()` values like `0.119` (12% down).

**Root Cause:** Poppler's `left()` and `top()` return **ALREADY NORMALIZED** coordinates (0-1), not PDF point coordinates! The debug output showed:
```
top: 0.119156 pageSize: 637.822 → normalized: 0.000186816
```
Dividing `0.119 / 637.822` resulted in a tiny value (0.0001), placing everything at the top.

**Fix:** Don't divide by page size - use Poppler's values directly:
```cpp
// Before (wrong - dividing already-normalized values):
posY = dest->top() / pageSizePoints.height();

// After (correct - values are already 0-1):
posY = dest->top();
```

---

### Note: PDF Outline Position Accuracy (Not a Bug)

**Observation:** Some PDFs have outline entries with imprecise or identical positions:
- Multiple items on the same page may share the same `top()` value
- Position values may cluster in one region (e.g., all < 0.5)
- Items that visually appear at different positions may have the same stored position

**Example from testing:**
```
"3.6　总结" page: 149 raw top: 0.206954
"3.7　习题" page: 149 raw top: 0.206954  // Same position!
```

**Cause:** This is a **PDF creation issue**, not a SpeedyNote bug. Many PDF creators:
- Set outline destinations to "near the heading" rather than exact positions
- Use the same position for all items on a page
- Only specify page number without position data

**Our implementation correctly uses whatever position data the PDF provides.**

**Correct Behavior:**
- Clicking outline item goes to correct position on page
- Target position appears near top of viewport (natural reading position)
- Pages remain horizontally centered

**Files Fixed:**
- `source/pdf/PopplerPdfProvider.cpp`
- `source/core/DocumentViewport.cpp`

---

## Post-Implementation Code Review

### CR-OP-1: Expansion State Not Cleared on Document Switch ✅ FIXED

**Problem:** When switching from PDF A (with outline) to PDF B (with outline), the expansion state from PDF A incorrectly persisted. This could cause items in PDF B to be expanded based on paths from PDF A (if titles happened to match).

**Root Cause:** `setOutline()` only cleared expansion state if `restoreState()` was called (when `m_expandedItems` was non-empty). But when switching documents directly without calling `clearOutline()` first, the old state remained and the `restoreState()` call was skipped because we want default expansion, not the old document's state.

**Fix:** Clear `m_expandedItems` at the start of `setOutline()`:
```cpp
void OutlinePanel::setOutline(const QVector<PdfOutlineItem>& outline) {
    m_outline = outline;
    m_tree->clear();
    m_lastHighlightedPage = -1;
    m_expandedItems.clear();  // Clear previous document's state
    // ...
}
```

**Files Fixed:**
- `source/ui/sidebars/OutlinePanel.cpp`

---

### CR-OP-2: Redundant Include ✅ FIXED

**Problem:** `PdfProvider.h` was included in both `OutlinePanel.h` and `OutlinePanel.cpp`.

**Fix:** Removed redundant include from `.cpp` file with comment explaining it's already included via header.

**Files Fixed:**
- `source/ui/sidebars/OutlinePanel.cpp`

---

### CR-OP-3: getItemPath() Duplicate Title Handling (Documented)

**Status:** DOCUMENTED (Minor edge case, not fixed)

**Issue:** If two outline items have the same title at the same level, `getItemPath()` returns the same path for both. When restoring expansion state, both items would be expanded.

**Impact:** Minimal - PDF outlines rarely have duplicate titles at the same level.

**Potential Future Fix:** Include index in path (e.g., `"0:Chapter 1/1:Section 1"`).

---

### CR-OP-4: findItemForPage() Performance (Documented)

**Status:** DOCUMENTED (Acceptable for current use)

**Issue:** `findItemForPage()` iterates through ALL tree items (O(n)) to find the best match for the current page.

**Impact:** Acceptable - only called when page changes, not on every frame. For a 500-item outline, this is ~500 comparisons per page change, which is negligible.

**Potential Future Optimization:** Build a sorted list of (page, item) pairs for binary search (O(log n)).

---

### Code Review Summary

| Issue | Severity | Status |
|-------|----------|--------|
| CR-OP-1: Expansion state persistence | Medium | ✅ FIXED |
| CR-OP-2: Redundant include | Low | ✅ FIXED |
| CR-OP-3: Duplicate title handling | Low | Documented |
| CR-OP-4: findItemForPage O(n) | Low | Documented |

**Memory Safety:** ✅ No issues found
- All tree items owned by QTreeWidget or parent items
- Delegate owned by OutlinePanel (parent)
- Proper null checks in place

**Memory Leaks:** ✅ No issues found
- Qt parent-child ownership handles all allocations
- State cleared when outline changes

**Crashes:** ✅ No issues found
- Null checks in place for pointers
- Bounds checking on page indices

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

