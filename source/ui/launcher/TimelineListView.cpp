#include "TimelineListView.h"

#include <QMouseEvent>
#include <QScrollBar>

TimelineListView::TimelineListView(QWidget* parent)
    : QListView(parent)
{
    // Configure long-press timer
    m_longPressTimer.setSingleShot(true);
    m_longPressTimer.setInterval(LONG_PRESS_MS);
    connect(&m_longPressTimer, &QTimer::timeout,
            this, &TimelineListView::onLongPressTimeout);
    
    // Configure kinetic scroll timer
    m_kineticTimer.setInterval(KINETIC_TICK_MS);
    connect(&m_kineticTimer, &QTimer::timeout,
            this, &TimelineListView::onKineticScrollTick);
}

bool TimelineListView::isTouchInput(QMouseEvent* event) const
{
    // Touch events are synthesized to mouse events with this source
    return (event->source() != Qt::MouseEventNotSynthesized);
}

void TimelineListView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        // Stop any ongoing kinetic scroll
        stopKineticScroll();
        
        m_pressPos = event->pos();
        m_pressedIndex = indexAt(event->pos());
        m_longPressTriggered = false;
        m_touchScrolling = false;
        m_scrollStartValue = verticalScrollBar()->value();
        
        // Initialize velocity tracking
        m_velocityTimer.start();
        m_lastVelocity = 0.0;
        
        // Only start long-press timer if we pressed on a valid item
        if (m_pressedIndex.isValid()) {
            m_longPressTimer.start();
        }
    }
    
    // Call base class to handle normal click behavior
    QListView::mousePressEvent(event);
}

void TimelineListView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_longPressTimer.stop();
        bool wasScrolling = m_touchScrolling;
        m_touchScrolling = false;
        
        // If long-press was triggered, don't process as a click
        if (m_longPressTriggered) {
            m_longPressTriggered = false;
            event->accept();
            return;
        }
        
        // If was scrolling, start kinetic scroll and don't trigger click
        if (wasScrolling && isTouchInput(event)) {
            if (qAbs(m_lastVelocity) > KINETIC_MIN_VELOCITY) {
                startKineticScroll(m_lastVelocity);
            }
            event->accept();
            return;
        }
    }
    
    // Call base class to handle normal release behavior
    QListView::mouseReleaseEvent(event);
}

void TimelineListView::mouseMoveEvent(QMouseEvent* event)
{
    if ((event->buttons() & Qt::LeftButton)) {
        QPoint delta = event->pos() - m_pressPos;
        
        // Cancel long-press if moved beyond threshold
        if (delta.manhattanLength() > LONG_PRESS_MOVE_THRESHOLD) {
            if (m_longPressTimer.isActive()) {
                m_longPressTimer.stop();
            }
            
            // Start touch scrolling for touch input
            if (isTouchInput(event) && !m_touchScrolling) {
                m_touchScrolling = true;
            }
        }
        
        // Handle touch scrolling
        if (m_touchScrolling && isTouchInput(event)) {
            // Apply scroll
            int newValue = m_scrollStartValue - delta.y();
            verticalScrollBar()->setValue(newValue);
            
            // Calculate velocity for kinetic scrolling
            qint64 frameTime = m_velocityTimer.restart();
            if (frameTime > 0) {
                // Track actual scroll change
                static int lastScrollValue = m_scrollStartValue;
                int scrollChange = verticalScrollBar()->value() - lastScrollValue;
                lastScrollValue = verticalScrollBar()->value();
                
                qreal instantVelocity = static_cast<qreal>(scrollChange) / frameTime;
                
                // Exponential smoothing for stable velocity
                constexpr qreal alpha = 0.4;
                m_lastVelocity = alpha * instantVelocity + (1.0 - alpha) * m_lastVelocity;
                
                // Decay velocity if no movement for a while
                if (frameTime > 50 && qAbs(scrollChange) < 1) {
                    m_lastVelocity *= 0.5;
                }
            }
            
            event->accept();
            return;
        }
    }
    
    // Call base class to handle normal move behavior
    QListView::mouseMoveEvent(event);
}

void TimelineListView::onLongPressTimeout()
{
    m_longPressTriggered = true;
    
    // Emit signal with the pressed index and global position
    if (m_pressedIndex.isValid()) {
        QPoint globalPos = viewport()->mapToGlobal(m_pressPos);
        emit longPressed(m_pressedIndex, globalPos);
    }
    
    // Clear selection state to prevent accidental clicks after menu closes
    clearSelection();
}

void TimelineListView::startKineticScroll(qreal velocity)
{
    // Cap velocity to prevent extreme scroll distances
    m_kineticVelocity = qBound(-KINETIC_MAX_VELOCITY, velocity, KINETIC_MAX_VELOCITY);
    m_kineticTimer.start();
}

void TimelineListView::stopKineticScroll()
{
    m_kineticTimer.stop();
    m_kineticVelocity = 0.0;
}

void TimelineListView::onKineticScrollTick()
{
    // Apply velocity to scroll position
    int delta = static_cast<int>(m_kineticVelocity * KINETIC_TICK_MS);
    int currentValue = verticalScrollBar()->value();
    int newValue = currentValue + delta;
    
    // Clamp to valid range
    int minValue = verticalScrollBar()->minimum();
    int maxValue = verticalScrollBar()->maximum();
    newValue = qBound(minValue, newValue, maxValue);
    
    verticalScrollBar()->setValue(newValue);
    
    // Apply deceleration
    m_kineticVelocity *= KINETIC_DECELERATION;
    
    // Stop if velocity is too low or hit bounds
    if (qAbs(m_kineticVelocity) < KINETIC_MIN_VELOCITY ||
        newValue == minValue || newValue == maxValue) {
        stopKineticScroll();
    }
}
