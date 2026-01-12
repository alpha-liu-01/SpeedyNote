#include "PagePanel.h"
#include "PageThumbnailModel.h"
#include "PageThumbnailDelegate.h"
#include "../core/Document.h"

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
    
    // DEBUG: Track scroll value changes
    connect(m_listView->verticalScrollBar(), &QScrollBar::valueChanged, 
            this, [](int value) {
        static int lastValue = -1;
        // Only log significant changes to avoid spam
        if (qAbs(value - lastValue) > 100) {
            qDebug() << "PagePanel scroll changed:" << lastValue << "->" << value;
            lastValue = value;
        }
    });
}

// ============================================================================
// Document Binding
// ============================================================================

void PagePanel::setDocument(Document* doc)
{
    if (m_document == doc) {
        return;
    }
    
    qDebug() << "PagePanel::setDocument() - changing document, scroll will reset";
    
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
    qDebug() << "PagePanel::onCurrentPageChanged:" << pageIndex 
             << "visible:" << isVisible()
             << "current scroll:" << scrollPosition();
    
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
                qDebug() << "PagePanel: Auto-scrolling to page" << pageIndex << "(was off-screen)";
                scrollToCurrentPage();
            } else {
                qDebug() << "PagePanel: Page" << pageIndex << "already visible, not scrolling";
            }
        }
    }
}

void PagePanel::scrollToCurrentPage()
{
    if (!m_document || m_currentPageIndex < 0) {
        return;
    }
    
    qDebug() << "PagePanel::scrollToCurrentPage() called, page:" << m_currentPageIndex;
    
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
    int pos = scrollPosition();
    qDebug() << "PagePanel::saveTabState(" << tabIndex << ") - saving scroll position:" << pos;
    m_tabScrollPositions[tabIndex] = pos;
}

void PagePanel::restoreTabState(int tabIndex)
{
    qDebug() << "PagePanel::restoreTabState(" << tabIndex << ") - current scroll:" << scrollPosition();
    
    if (m_tabScrollPositions.contains(tabIndex)) {
        int savedPos = m_tabScrollPositions.value(tabIndex);
        qDebug() << "  -> restoring saved position:" << savedPos;
        setScrollPosition(savedPos);
    } else {
        // New tab - scroll to current page
        qDebug() << "  -> no saved position, scrolling to current page";
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
// Thumbnail Invalidation
// ============================================================================

void PagePanel::invalidateThumbnail(int pageIndex)
{
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
    
    qDebug() << "PagePanel::showEvent() - needsFullRefresh:" << m_needsFullRefresh;
    
    // If we need a full refresh, do it now
    if (m_needsFullRefresh) {
        m_model->invalidateAllThumbnails();
        m_needsFullRefresh = false;
    }
    
    // Only scroll to current page on initial show, not every show
    // The user's scroll position should be preserved
    // scrollToCurrentPage();  // Disabled - was causing scroll jumps
}

