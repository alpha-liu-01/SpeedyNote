#include "PagePanelListView.h"
#include "../PageThumbnailModel.h"

#include <QMouseEvent>
#include <QDragMoveEvent>
#include <QScroller>
#include <QScrollerProperties>
#include <QScrollBar>
#include <QDrag>
#include <QMimeData>
#include <QPainter>

// ============================================================================
// Constructor
// ============================================================================

PagePanelListView::PagePanelListView(QWidget* parent)
    : QListView(parent)
{
    // Configure long-press timer
    m_longPressTimer.setSingleShot(true);
    m_longPressTimer.setInterval(LONG_PRESS_MS);
    connect(&m_longPressTimer, &QTimer::timeout,
            this, &PagePanelListView::onLongPressTimeout);
    
    // Setup touch scrolling (QScroller)
    setupTouchScrolling();
}

void PagePanelListView::beginDrag(Qt::DropActions supportedActions)
{
    // Public wrapper for protected startDrag
    startDrag(supportedActions);
}

// ============================================================================
// Touch Scrolling Setup
// ============================================================================

void PagePanelListView::setupTouchScrolling()
{
    // Enable kinetic scrolling for touch only (not mouse)
    QScroller::grabGesture(viewport(), QScroller::TouchGesture);
    m_scrollerGrabbed = true;
    
    // Configure scroller properties
    QScroller* scroller = QScroller::scroller(viewport());
    if (scroller) {
        QScrollerProperties props = scroller->scrollerProperties();
        props.setScrollMetric(QScrollerProperties::DecelerationFactor, 0.3);
        props.setScrollMetric(QScrollerProperties::OvershootDragResistanceFactor, 0.5);
        props.setScrollMetric(QScrollerProperties::SnapTime, 0.3);
        scroller->setScrollerProperties(props);
    }
}

// ============================================================================
// Event Handling
// ============================================================================

bool PagePanelListView::viewportEvent(QEvent* event)
{
    // Fallback cleanup for touch end/cancel (main cleanup is in mouseReleaseEvent)
    // This handles edge cases like interrupted touches that don't generate mouse release
    if (event->type() == QEvent::TouchEnd || event->type() == QEvent::TouchCancel) {
        m_longPressTimer.stop();
    }
    
    return QListView::viewportEvent(event);
}

// ============================================================================
// Mouse Event Handlers
// ============================================================================

void PagePanelListView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressPos = event->pos();
        m_pressedIndex = indexAt(event->pos());
        m_longPressTriggered = false;
        
        // Detect touch vs stylus/mouse via event source
        // Touch events are synthesized by the system, stylus/mouse are not
        m_isTouchInput = (event->source() == Qt::MouseEventSynthesizedBySystem);
        
        if (m_isTouchInput) {
            // Touch: check if item can be dragged and start long-press timer
            bool canDrag = m_pressedIndex.isValid() && 
                           m_pressedIndex.data(PageThumbnailModel::CanDragRole).toBool();
            if (canDrag) {
                m_longPressTimer.start();
            }
        }
        // Stylus/mouse: let QListView handle immediate drag
    }
    
    QListView::mousePressEvent(event);
}

void PagePanelListView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_longPressTimer.stop();
        
        if (m_longPressTriggered) {
            m_longPressTriggered = false;
            m_isTouchInput = false;
            
            // Re-enable auto-scroll
            setAutoScroll(true);
            
            // Re-enable scroller
            if (!m_scrollerGrabbed) {
                regrabScroller();
            }
            
            event->accept();
            return;
        }
        
        // Re-enable scroller if we disabled it
        if (!m_scrollerGrabbed) {
            regrabScroller();
        }
    }
    
    m_isTouchInput = false;
    QListView::mouseReleaseEvent(event);
}

void PagePanelListView::mouseMoveEvent(QMouseEvent* event)
{
    // Cancel long-press timer if moved too far (user is scrolling/dragging)
    if (m_longPressTimer.isActive()) {
        QPoint delta = event->pos() - m_pressPos;
        if (delta.manhattanLength() > LONG_PRESS_MOVE_THRESHOLD) {
            m_longPressTimer.stop();
            // For touch: movement without long-press = scrolling (handled by QScroller)
        }
    }
    
    // During long-press drag, don't let QListView process mouse moves (prevents scroll)
    if (m_longPressTriggered && m_isTouchInput) {
        event->accept();
        return;
    }
    
    QListView::mouseMoveEvent(event);
}

// ============================================================================
// Drag Auto-Scroll
// ============================================================================

void PagePanelListView::dragMoveEvent(QDragMoveEvent* event)
{
    QListView::dragMoveEvent(event);
    
    // Auto-scroll when dragging near edges
    int y = event->position().toPoint().y();
    int viewHeight = viewport()->height();
    
    if (y < AUTO_SCROLL_MARGIN) {
        // Near top - scroll up
        int speed = qMax(1, qMin(AUTO_SCROLL_MAX_SPEED, (AUTO_SCROLL_MARGIN - y) / 3));
        verticalScrollBar()->setValue(verticalScrollBar()->value() - speed);
    } else if (y > viewHeight - AUTO_SCROLL_MARGIN) {
        // Near bottom - scroll down
        int speed = qMax(1, qMin(AUTO_SCROLL_MAX_SPEED, (y - (viewHeight - AUTO_SCROLL_MARGIN)) / 3));
        verticalScrollBar()->setValue(verticalScrollBar()->value() + speed);
    }
}

void PagePanelListView::startDrag(Qt::DropActions supportedActions)
{
    // For touch input, block immediate drag - wait for long-press
    if (m_isTouchInput && !m_longPressTriggered) {
        return;
    }
    
    // Get selected indexes, fallback to pressed index
    QModelIndexList indexes = selectedIndexes();
    if (indexes.isEmpty()) {
        if (m_pressedIndex.isValid()) {
            setCurrentIndex(m_pressedIndex);
            indexes = selectedIndexes();
        }
        if (indexes.isEmpty()) {
            return;
        }
    }
    
    QModelIndex index = indexes.first();
    QMimeData* mimeData = model()->mimeData(indexes);
    if (!mimeData) {
        return;
    }
    
    // Create drag object (Qt takes ownership of mimeData)
    QDrag* drag = new QDrag(this);
    drag->setMimeData(mimeData);
    
    // Create drag pixmap at LOGICAL size (not physical) to avoid DPR scaling issues
    QRect itemRect = visualRect(index);
    if (itemRect.isEmpty()) {
        // Item not visible, can't create proper drag pixmap
        // Don't use Qt's default (has DPR scaling issues)
        delete drag;
        return;
    }
    
    QPixmap pixmap(itemRect.size());
    pixmap.fill(Qt::transparent);
    
    // Render the item into pixmap
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    
    QStyleOptionViewItem option;
    option.initFrom(this);
    option.rect = QRect(QPoint(0, 0), itemRect.size());
    option.state |= QStyle::State_Selected;
    option.decorationSize = itemRect.size();
    
    itemDelegate()->paint(&painter, option, index);
    painter.end();
    
    drag->setPixmap(pixmap);
    drag->setHotSpot(QPoint(itemRect.width() / 2, itemRect.height() / 2));
    
    // Execute drag (Qt deletes QDrag when operation completes)
    drag->exec(supportedActions, Qt::MoveAction);
}

// ============================================================================
// Long-Press Handling
// ============================================================================

void PagePanelListView::onLongPressTimeout()
{
    m_longPressTriggered = true;
    
    if (m_pressedIndex.isValid()) {
        // Ungrab scroller now that we're starting drag
        ungrabScroller();
        
        // Disable QListView's auto-scroll during drag (we implement our own in dragMoveEvent)
        setAutoScroll(false);
        
        // Select the item (it might not be selected if QScroller was handling the touch)
        setCurrentIndex(m_pressedIndex);
        
        // Notify PagePanel to start drag operation
        emit dragRequested(m_pressedIndex);
    }
}

// ============================================================================
// QScroller Management
// ============================================================================

void PagePanelListView::ungrabScroller()
{
    if (m_scrollerGrabbed) {
        QScroller::ungrabGesture(viewport());
        m_scrollerGrabbed = false;
    }
}

void PagePanelListView::regrabScroller()
{
    if (!m_scrollerGrabbed) {
        QScroller::grabGesture(viewport(), QScroller::TouchGesture);
        m_scrollerGrabbed = true;
    }
}
