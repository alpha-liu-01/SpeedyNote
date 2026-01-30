#include "LauncherScrollArea.h"

#include <QMouseEvent>
#include <QScrollBar>

LauncherScrollArea::LauncherScrollArea(QWidget* parent)
    : QScrollArea(parent)
{
    // Setup kinetic scroll timer
    m_kineticTimer.setInterval(KINETIC_TICK_MS);
    connect(&m_kineticTimer, &QTimer::timeout,
            this, &LauncherScrollArea::onKineticScrollTick);
}

bool LauncherScrollArea::isTouchInput(QMouseEvent* event) const
{
    // Touch events are synthesized to mouse events with this source
    return (event->source() != Qt::MouseEventNotSynthesized);
}

void LauncherScrollArea::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && isTouchInput(event)) {
        // Stop any ongoing kinetic scroll
        stopKineticScroll();
        
        // Record starting position for potential scroll
        m_touchScrollStartPos = event->pos();
        m_scrollStartValue = verticalScrollBar()->value();
        m_touchScrolling = false;
        
        // Initialize velocity tracking
        m_velocityTimer.start();
        m_lastVelocity = 0.0;
        
        event->accept();
        return;
    }
    
    QScrollArea::mousePressEvent(event);
}

void LauncherScrollArea::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && isTouchInput(event)) {
        bool wasScrolling = m_touchScrolling;
        m_touchScrolling = false;
        
        if (wasScrolling) {
            // Start kinetic scroll if velocity is high enough
            if (qAbs(m_lastVelocity) > KINETIC_MIN_VELOCITY) {
                startKineticScroll(m_lastVelocity);
            }
            event->accept();
            return;
        }
        // If not scrolling, let child widgets handle the release (tap)
    }
    
    QScrollArea::mouseReleaseEvent(event);
}

void LauncherScrollArea::mouseMoveEvent(QMouseEvent* event)
{
    if ((event->buttons() & Qt::LeftButton) && isTouchInput(event)) {
        QPoint delta = event->pos() - m_touchScrollStartPos;
        
        // Start scrolling if moved beyond threshold
        if (!m_touchScrolling && qAbs(delta.y()) > SCROLL_THRESHOLD) {
            m_touchScrolling = true;
        }
        
        if (m_touchScrolling) {
            // Calculate scroll delta and apply
            int scrollDelta = m_scrollStartValue - (verticalScrollBar()->value());
            int newValue = m_scrollStartValue - delta.y();
            verticalScrollBar()->setValue(newValue);
            
            // Calculate velocity for kinetic scrolling
            qint64 frameTime = m_velocityTimer.restart();
            if (frameTime > 0) {
                int actualDelta = verticalScrollBar()->value() - (m_scrollStartValue - delta.y() + scrollDelta);
                // We want velocity in terms of how much the content moved
                // Negative delta.y means finger moved up, scroll down (positive velocity)
                qreal instantVelocity = static_cast<qreal>(-delta.y() + m_scrollStartValue - verticalScrollBar()->value()) / frameTime;
                
                // Actually, simpler: track the actual scroll change
                static int lastScrollValue = m_scrollStartValue;
                int scrollChange = verticalScrollBar()->value() - lastScrollValue;
                lastScrollValue = verticalScrollBar()->value();
                
                instantVelocity = static_cast<qreal>(scrollChange) / frameTime;
                
                // Exponential smoothing for stable velocity
                constexpr qreal alpha = 0.4;
                m_lastVelocity = alpha * instantVelocity + (1.0 - alpha) * m_lastVelocity;
                
                // Decay velocity if no movement for a while
                if (frameTime > 50 && qAbs(scrollChange) < 1) {
                    m_lastVelocity *= 0.5;
                }
            }
            
            // Update start position for next move calculation
            m_touchScrollStartPos = event->pos();
            m_scrollStartValue = verticalScrollBar()->value();
            
            event->accept();
            return;
        }
    }
    
    QScrollArea::mouseMoveEvent(event);
}

void LauncherScrollArea::startKineticScroll(qreal velocity)
{
    // Cap velocity to prevent extreme scroll distances
    m_kineticVelocity = qBound(-KINETIC_MAX_VELOCITY, velocity, KINETIC_MAX_VELOCITY);
    m_kineticTimer.start();
}

void LauncherScrollArea::stopKineticScroll()
{
    m_kineticTimer.stop();
    m_kineticVelocity = 0.0;
}

void LauncherScrollArea::onKineticScrollTick()
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
