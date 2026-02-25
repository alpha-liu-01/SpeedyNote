#include "PagePanelListView.h"
#include "../PageThumbnailModel.h"

#include <QMouseEvent>
#include <QDragMoveEvent>
#include <QScrollBar>
#include <QDrag>
#include <QMimeData>
#include <QPainter>
#include <QDebug>

// Debug flag for touch scrolling diagnostics (set to 1 to enable)


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
    
    // Configure kinetic scroll timer
    m_kineticTimer.setInterval(KINETIC_TICK_MS);
    connect(&m_kineticTimer, &QTimer::timeout,
            this, &PagePanelListView::onKineticScrollTick);
    
    // Setup manual touch scrolling (no QScroller - it causes conflicts)
    setupTouchScrolling();
    
#if SPEEDYNOTE_DEBUG
    // Monitor scroll position changes
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        qDebug() << "[SCROLL] pos:" << value 
                 << "touchScrolling:" << m_touchScrolling
                 << "kinetic:" << m_kineticTimer.isActive();
    });
#endif
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
    // Manual touch scrolling is implemented in the mouse event handlers.
    // We don't use QScroller because it conflicts with QListView's native handling.
}

// ============================================================================
// Event Handling
// ============================================================================

bool PagePanelListView::viewportEvent(QEvent* event)
{
#if SPEEDYNOTE_DEBUG
    // Log touch-related events
    if (event->type() == QEvent::TouchBegin || 
        event->type() == QEvent::TouchUpdate ||
        event->type() == QEvent::TouchEnd || 
        event->type() == QEvent::TouchCancel) {
        QString typeStr;
        switch (event->type()) {
            case QEvent::TouchBegin: typeStr = "TouchBegin"; break;
            case QEvent::TouchUpdate: typeStr = "TouchUpdate"; break;
            case QEvent::TouchEnd: typeStr = "TouchEnd"; break;
            case QEvent::TouchCancel: typeStr = "TouchCancel"; break;
            default: typeStr = "Unknown"; break;
        }
        qDebug() << "[VIEWPORT] event:" << typeStr << "scrollPos:" << verticalScrollBar()->value();
    }
#endif

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
        m_touchScrolling = false;
        
        // Detect touch vs stylus/mouse via event source
        m_isTouchInput = (event->source() != Qt::MouseEventNotSynthesized);
        
        if (m_isTouchInput) {
            // Stop any ongoing kinetic scroll
            stopKineticScroll();
            
            // Store scroll position at touch start for manual scrolling
            m_touchScrollStartPos = verticalScrollBar()->value();
            
            // Initialize velocity tracking
            m_velocityTimer.start();
            m_lastVelocity = 0.0;
            
            // Start long-press timer for drag-and-drop
            bool canDrag = m_pressedIndex.isValid() && 
                           m_pressedIndex.data(PageThumbnailModel::CanDragRole).toBool();
            if (canDrag) {
                m_longPressTimer.start();
            }
            
#if SPEEDYNOTE_DEBUG
            qDebug() << "[PRESS] pos:" << event->pos() 
                     << "isTouch: true"
                     << "scrollPos:" << m_touchScrollStartPos;
#endif
            
            // Accept event - we handle touch scrolling manually
            event->accept();
            return;
        }
        
#if SPEEDYNOTE_DEBUG
        qDebug() << "[PRESS] pos:" << event->pos() 
                 << "isTouch: false (mouse/stylus)"
                 << "scrollPos:" << verticalScrollBar()->value();
#endif
    }
    
    // Mouse/stylus: let QListView handle normally
    QListView::mousePressEvent(event);
}

void PagePanelListView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_longPressTimer.stop();
        
        bool wasTouchInput = m_isTouchInput;
        bool wasScrolling = m_touchScrolling;
        
#if SPEEDYNOTE_DEBUG
        qDebug() << "[RELEASE] pos:" << event->pos() 
                 << "wasTouch:" << wasTouchInput
                 << "wasScrolling:" << wasScrolling
                 << "scrollPos:" << verticalScrollBar()->value();
#endif
        
        if (m_longPressTriggered) {
            m_longPressTriggered = false;
            m_isTouchInput = false;
            m_touchScrolling = false;
            
            // Re-enable auto-scroll
            setAutoScroll(true);
            
            event->accept();
            return;
        }
        
        if (wasTouchInput) {
            m_isTouchInput = false;
            m_touchScrolling = false;
            
            // If this was a tap (not a scroll), emit clicked signal manually
            if (!wasScrolling) {
                QModelIndex index = indexAt(event->pos());
                if (index.isValid() && index == m_pressedIndex) {
                    // Emit the clicked signal that PagePanel listens to
                    emit clicked(index);
                }
            } else {
                // Use the velocity that was calculated during dragging
#if SPEEDYNOTE_DEBUG
                qDebug() << "[RELEASE] velocity:" << m_lastVelocity << "px/ms";
#endif
                
                // Start kinetic scroll if velocity is significant
                if (qAbs(m_lastVelocity) > KINETIC_MIN_VELOCITY) {
                    startKineticScroll(m_lastVelocity);
                }
            }
            
            event->accept();
            return;
        }
        
        m_isTouchInput = false;
        m_touchScrolling = false;
    }
    
    // Mouse/stylus: let QListView handle normally
    QListView::mouseReleaseEvent(event);
}

void PagePanelListView::mouseMoveEvent(QMouseEvent* event)
{
    if (m_isTouchInput) {
        // Check if moved enough to count as scrolling (not a tap)
        QPoint delta = event->pos() - m_pressPos;
        if (!m_touchScrolling && delta.manhattanLength() > LONG_PRESS_MOVE_THRESHOLD) {
            m_touchScrolling = true;
            // Cancel long-press timer if active (user is scrolling, not trying to drag)
            if (m_longPressTimer.isActive()) {
                m_longPressTimer.stop();
            }
        }
        
        // During long-press drag, don't scroll
        if (m_longPressTriggered) {
            event->accept();
            return;
        }
        
        int currentY = event->pos().y();
        
        // Manual touch scrolling: scroll by the Y delta from press position
        int deltaY = currentY - m_pressPos.y();
        int newScrollPos = m_touchScrollStartPos - deltaY;  // Subtract because scroll up = finger down
        int oldScrollPos = verticalScrollBar()->value();
        verticalScrollBar()->setValue(newScrollPos);
        
        // Calculate velocity from actual scroll change (more accurate than finger position)
        // Velocity = scroll position change per millisecond
        // Use exponential smoothing to reduce noise
        int scrollDelta = newScrollPos - oldScrollPos;
        
        // Get time since last move and restart timer for next frame
        qint64 frameTime = m_velocityTimer.restart();
        
        if (frameTime > 0 && scrollDelta != 0) {
            // Instantaneous velocity (scroll delta / time)
            qreal instantVelocity = static_cast<qreal>(scrollDelta) / static_cast<qreal>(frameTime);
            
            // Exponential smoothing: blend with previous velocity
            // Higher alpha = more responsive but noisier, lower = smoother but laggier
            constexpr qreal alpha = 0.4;
            m_lastVelocity = alpha * instantVelocity + (1.0 - alpha) * m_lastVelocity;
        } else if (frameTime > 50) {
            // If no movement for a while, decay velocity
            m_lastVelocity *= 0.5;
        }
        
#if SPEEDYNOTE_DEBUG
        static int moveCount = 0;
        if (++moveCount % 10 == 0) {
            qDebug() << "[MOVE] pos:" << event->pos() 
                     << "deltaY:" << deltaY
                     << "scrollPos:" << newScrollPos
                     << "velocity:" << m_lastVelocity;
        }
#endif
        
        event->accept();
        return;
    }
    
    // Cancel long-press for mouse/stylus too
    if (m_longPressTimer.isActive()) {
        QPoint delta = event->pos() - m_pressPos;
        if (delta.manhattanLength() > LONG_PRESS_MOVE_THRESHOLD) {
            m_longPressTimer.stop();
        }
    }
    
    // Mouse/stylus: let QListView handle normally
    QListView::mouseMoveEvent(event);
}

// ============================================================================
// Drag Auto-Scroll
// ============================================================================

void PagePanelListView::dragMoveEvent(QDragMoveEvent* event)
{
    QListView::dragMoveEvent(event);
    
    // Auto-scroll when dragging near edges
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    int y = event->position().toPoint().y();
#else
    int y = event->pos().y();
#endif
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
        // Disable QListView's auto-scroll during drag (we implement our own in dragMoveEvent)
        setAutoScroll(false);
        
        // Select the item
        setCurrentIndex(m_pressedIndex);
        
        // Notify PagePanel to start drag operation
        emit dragRequested(m_pressedIndex);
    }
}

// ============================================================================
// Kinetic Scrolling
// ============================================================================

void PagePanelListView::startKineticScroll(qreal velocity)
{
    // Cap velocity to prevent excessive scrolling
    m_kineticVelocity = qBound(-KINETIC_MAX_VELOCITY, velocity, KINETIC_MAX_VELOCITY);
    m_kineticTimer.start();
    
#if SPEEDYNOTE_DEBUG
    qDebug() << "[KINETIC] start, velocity:" << m_kineticVelocity << "px/ms (input:" << velocity << ")";
#endif
}

void PagePanelListView::stopKineticScroll()
{
    if (m_kineticTimer.isActive()) {
        m_kineticTimer.stop();
        m_kineticVelocity = 0.0;
        
#if SPEEDYNOTE_DEBUG
        qDebug() << "[KINETIC] stop";
#endif
    }
}

void PagePanelListView::onKineticScrollTick()
{
    // Apply velocity to scroll position
    int scrollDelta = static_cast<int>(m_kineticVelocity * KINETIC_TICK_MS);
    int currentPos = verticalScrollBar()->value();
    int newPos = currentPos + scrollDelta;
    
    // Clamp to valid range
    int minPos = verticalScrollBar()->minimum();
    int maxPos = verticalScrollBar()->maximum();
    newPos = qBound(minPos, newPos, maxPos);
    
    verticalScrollBar()->setValue(newPos);
    
    // Apply deceleration
    m_kineticVelocity *= KINETIC_DECELERATION;
    
    // Stop if velocity is too low or we hit the bounds
    bool hitBounds = (newPos == minPos || newPos == maxPos) && scrollDelta != 0;
    if (qAbs(m_kineticVelocity) < KINETIC_MIN_VELOCITY || hitBounds) {
        stopKineticScroll();
    }
}
