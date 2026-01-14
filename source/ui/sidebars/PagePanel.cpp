#include "PagePanel.h"
#include "../PageThumbnailModel.h"
#include "../PageThumbnailDelegate.h"
#include "../../core/Document.h"

#include <QListView>
#include <QVBoxLayout>
#include <QScrollBar>
#include <QScroller>
#include <QTimer>
#include <QResizeEvent>

// ============================================================================
// Constructor / Destructor
// ============================================================================

PagePanel::PagePanel(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
    setupConnections();
}

PagePanel::~PagePanel()
{
    // Children are parented, will be deleted automatically
}

// ============================================================================
// Setup
// ============================================================================

void PagePanel::setupUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Create model
    m_model = new PageThumbnailModel(this);
    
    // Create delegate
    m_delegate = new PageThumbnailDelegate(this);
    
    // Create list view
    m_listView = new QListView(this);
    configureListView();
    
    // Set model and delegate
    m_listView->setModel(m_model);
    m_listView->setItemDelegate(m_delegate);
    
    layout->addWidget(m_listView);
    
    // Create invalidation timer
    m_invalidationTimer = new QTimer(this);
    m_invalidationTimer->setSingleShot(true);
    m_invalidationTimer->setInterval(INVALIDATION_DELAY_MS);
    
    // Apply initial theme
    applyTheme();
}

void PagePanel::configureListView()
{
    // Basic configuration
    m_listView->setViewMode(QListView::ListMode);
    m_listView->setFlow(QListView::TopToBottom);
    m_listView->setWrapping(false);
    m_listView->setResizeMode(QListView::Adjust);
    // DEBUG: Disabled batched mode - was possibly causing scroll jumps
    // m_listView->setLayoutMode(QListView::Batched);
    // m_listView->setBatchSize(10);
    m_listView->setLayoutMode(QListView::SinglePass);
    
    // Selection
    m_listView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_listView->setSelectionBehavior(QAbstractItemView::SelectRows);
    
    // Scrolling
    m_listView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_listView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    
    // Drag and drop
    m_listView->setDragEnabled(true);
    m_listView->setAcceptDrops(true);
    m_listView->setDropIndicatorShown(true);
    m_listView->setDragDropMode(QAbstractItemView::InternalMove);
    m_listView->setDefaultDropAction(Qt::MoveAction);
    
    // Appearance
    m_listView->setFrameShape(QFrame::NoFrame);
    m_listView->setSpacing(0);
    m_listView->setUniformItemSizes(false);  // Items may have different heights
    
    // Enable mouse tracking for hover effects
    m_listView->setMouseTracking(true);
    m_listView->viewport()->setMouseTracking(true);
    m_listView->setAttribute(Qt::WA_Hover, true);
    m_listView->viewport()->setAttribute(Qt::WA_Hover, true);
    
    // Setup touch scrolling
    setupTouchScrolling();
}

void PagePanel::setupTouchScrolling()
{
    // Enable kinetic scrolling for touch only (not mouse)
    QScroller::grabGesture(m_listView->viewport(), QScroller::TouchGesture);
    
    // Configure scroller
    QScroller* scroller = QScroller::scroller(m_listView->viewport());
    if (scroller) {
        QScrollerProperties props = scroller->scrollerProperties();
        props.setScrollMetric(QScrollerProperties::DecelerationFactor, 0.3);
        props.setScrollMetric(QScrollerProperties::OvershootDragResistanceFactor, 0.5);
        props.setScrollMetric(QScrollerProperties::SnapTime, 0.3);
        scroller->setScrollerProperties(props);
    }
}

void PagePanel::setupConnections()
{
    // Item click
    connect(m_listView, &QListView::clicked, this, &PagePanel::onItemClicked);
    
    // Page dropped from model
    connect(m_model, &PageThumbnailModel::pageDropped, 
            this, &PagePanel::onModelPageDropped);
    
    // Invalidation timer
    connect(m_invalidationTimer, &QTimer::timeout, 
            this, &PagePanel::performPendingInvalidation);
    
}

// ============================================================================
// Document Binding
// ============================================================================

void PagePanel::setDocument(Document* doc)
{
    if (m_document == doc) {
        return;
    }
    
    m_document = doc;
    m_currentPageIndex = 0;
    
    // Update model
    m_model->setDocument(doc);
    m_model->setCurrentPageIndex(0);
    
    // Update thumbnail width based on current size
    updateThumbnailWidth();
    
    // Clear pending invalidations
    m_pendingInvalidations.clear();
    m_needsFullRefresh = false;
}

// ============================================================================
// Current Page
// ============================================================================

void PagePanel::setCurrentPageIndex(int index)
{
    if (m_currentPageIndex != index && m_document) {
        m_currentPageIndex = index;
        m_model->setCurrentPageIndex(index);
    }
}

void PagePanel::onCurrentPageChanged(int pageIndex)
{
    int previousPage = m_currentPageIndex;
    setCurrentPageIndex(pageIndex);
    
    // Only auto-scroll if the page change is significant (not just minor viewport scroll)
    // and if the new current page is not already visible in the list view
    if (isVisible() && previousPage != pageIndex) {
        // Check if the current page item is already visible
        QModelIndex index = m_model->index(pageIndex, 0);
        if (index.isValid()) {
            QRect itemRect = m_listView->visualRect(index);
            QRect viewRect = m_listView->viewport()->rect();
            
            // Only scroll if item is completely outside visible area
            if (!viewRect.intersects(itemRect)) {
                scrollToCurrentPage();
            }
        }
    }
}

void PagePanel::scrollToCurrentPage()
{
    if (!m_document || m_currentPageIndex < 0) {
        return;
    }
    
    QModelIndex index = m_model->index(m_currentPageIndex, 0);
    if (index.isValid()) {
        m_listView->scrollTo(index, QAbstractItemView::EnsureVisible);
    }
}

// ============================================================================
// Scroll Position State
// ============================================================================

int PagePanel::scrollPosition() const
{
    return m_listView->verticalScrollBar()->value();
}

void PagePanel::setScrollPosition(int pos)
{
    m_listView->verticalScrollBar()->setValue(pos);
}

void PagePanel::saveTabState(int tabIndex)
{
    m_tabScrollPositions[tabIndex] = scrollPosition();
}

void PagePanel::restoreTabState(int tabIndex)
{
    if (m_tabScrollPositions.contains(tabIndex)) {
        setScrollPosition(m_tabScrollPositions.value(tabIndex));
    } else {
        // New tab - scroll to current page
        scrollToCurrentPage();
    }
}

void PagePanel::clearTabState(int tabIndex)
{
    m_tabScrollPositions.remove(tabIndex);
}

// ============================================================================
// Theme
// ============================================================================

void PagePanel::setDarkMode(bool dark)
{
    if (m_darkMode != dark) {
        m_darkMode = dark;
        m_delegate->setDarkMode(dark);
        applyTheme();
        m_listView->viewport()->update();
    }
}

void PagePanel::applyTheme()
{
    QString bgColor = m_darkMode ? "#2D2D2D" : "#F5F5F5";
    
    m_listView->setStyleSheet(QString(R"(
        QListView {
            background-color: %1;
            border: none;
            outline: none;
        }
        QListView::item {
            border: none;
            padding: 0px;
        }
        QListView::item:selected {
            background-color: transparent;
        }
    )").arg(bgColor));
}

// ============================================================================
// Thumbnail Access
// ============================================================================

QPixmap PagePanel::thumbnailForPage(int pageIndex) const
{
    if (!m_model) {
        return QPixmap();
    }
    return m_model->thumbnailForPage(pageIndex);
}

// ============================================================================
// Thumbnail Invalidation
// ============================================================================

void PagePanel::invalidateThumbnail(int pageIndex)
{
    // Optimization: If panel is not visible, just mark for refresh when it becomes visible.
    // This avoids clearing cached thumbnails unnecessarily while the user is editing
    // on another sidebar tab, and prevents any rendering work until the panel is shown.
    if (!isVisible()) {
        m_pendingInvalidations.insert(pageIndex);
        // Don't start the timer - we'll handle it in showEvent
        return;
    }
    
    m_pendingInvalidations.insert(pageIndex);
    
    // Start debounce timer if not already running
    if (!m_invalidationTimer->isActive()) {
        m_invalidationTimer->start();
    }
}

void PagePanel::invalidateAllThumbnails()
{
    m_needsFullRefresh = true;
    m_pendingInvalidations.clear();
    
    // Start debounce timer if not already running
    if (!m_invalidationTimer->isActive()) {
        m_invalidationTimer->start();
    }
}

void PagePanel::performPendingInvalidation()
{
    if (m_needsFullRefresh) {
        m_model->invalidateAllThumbnails();
        m_needsFullRefresh = false;
    } else {
        for (int pageIndex : m_pendingInvalidations) {
            m_model->invalidateThumbnail(pageIndex);
        }
    }
    
    m_pendingInvalidations.clear();
}

// ============================================================================
// Page Count Change
// ============================================================================

void PagePanel::onPageCountChanged()
{
    m_model->onPageCountChanged();
    
    // Update thumbnail width in case layout changed
    updateThumbnailWidth();
}

// ============================================================================
// Private Slots
// ============================================================================

void PagePanel::onItemClicked(const QModelIndex& index)
{
    if (!index.isValid()) {
        return;
    }
    
    // BUG-UI-002 FIX: Ignore clicks when QScroller is actively scrolling
    // This prevents touch scroll gestures from being detected as clicks
    QScroller* scroller = QScroller::scroller(m_listView->viewport());
    if (scroller) {
        QScroller::State state = scroller->state();
        if (state == QScroller::Dragging || state == QScroller::Scrolling) {
            return;  // Ignore click during scroll
        }
    }
    
    int pageIndex = index.data(PageThumbnailModel::PageIndexRole).toInt();
    emit pageClicked(pageIndex);
}

void PagePanel::onModelPageDropped(int fromIndex, int toIndex)
{
    // Forward the signal
    emit pageDropped(fromIndex, toIndex);
}

// ============================================================================
// Thumbnail Width
// ============================================================================

void PagePanel::updateThumbnailWidth()
{
    // Calculate thumbnail width based on panel width
    int availableWidth = width() - THUMBNAIL_PADDING * 2;
    int thumbnailWidth = qMax(MIN_THUMBNAIL_WIDTH, availableWidth);
    
    // Get device pixel ratio from screen
    qreal dpr = devicePixelRatioF();
    
    // Update model and delegate
    m_model->setThumbnailWidth(thumbnailWidth);
    m_model->setDevicePixelRatio(dpr);
    m_delegate->setThumbnailWidth(thumbnailWidth);
}

// ============================================================================
// Event Handlers
// ============================================================================

// Override resizeEvent to update thumbnail width
void PagePanel::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateThumbnailWidth();
}

// Override showEvent to handle refresh when becoming visible
void PagePanel::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    
    // Process any pending invalidations that accumulated while hidden
    // This is more efficient than clearing cache while hidden (thumbnails stay cached)
    if (m_needsFullRefresh) {
        m_model->invalidateAllThumbnails();
        m_needsFullRefresh = false;
        m_pendingInvalidations.clear();  // Full refresh supersedes individual invalidations
    } else if (!m_pendingInvalidations.isEmpty()) {
        // Invalidate only the pages that were modified while hidden
        for (int pageIndex : m_pendingInvalidations) {
            m_model->invalidateThumbnail(pageIndex);
        }
        m_pendingInvalidations.clear();
    }
    
    // Only scroll to current page on initial show, not every show
    // The user's scroll position should be preserved
    // scrollToCurrentPage();  // Disabled - was causing scroll jumps
}

