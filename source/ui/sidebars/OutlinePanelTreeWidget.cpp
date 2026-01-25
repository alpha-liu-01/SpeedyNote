#include "OutlinePanelTreeWidget.h"

#include <QMouseEvent>
#include <QScrollBar>

// ============================================================================
// Constructor
// ============================================================================

OutlinePanelTreeWidget::OutlinePanelTreeWidget(QWidget* parent)
    : QTreeWidget(parent)
{
    // Configure kinetic scroll timer
    m_kineticTimer.setInterval(KINETIC_TICK_MS);
    connect(&m_kineticTimer, &QTimer::timeout,
            this, &OutlinePanelTreeWidget::onKineticScrollTick);
}

// ============================================================================
// Mouse Event Handlers
// ============================================================================

void OutlinePanelTreeWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressPos = event->pos();
        m_touchScrolling = false;
        m_isTouchInput = (event->source() != Qt::MouseEventNotSynthesized);
        
        if (m_isTouchInput) {
            // Stop any ongoing kinetic scroll
            stopKineticScroll();
            
            // Store scroll position at touch start for manual scrolling
            m_touchScrollStartPos = verticalScrollBar()->value();
            
            // Initialize velocity tracking
            m_velocityTimer.start();
            m_lastVelocity = 0.0;
            
            event->accept();
            return;
        }
    }
    
    // Mouse/stylus: let QTreeWidget handle normally
    QTreeWidget::mousePressEvent(event);
}

void OutlinePanelTreeWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_isTouchInput) {
        // Check if we've moved enough to start scrolling
        QPoint delta = event->pos() - m_pressPos;
        if (!m_touchScrolling && delta.manhattanLength() > SCROLL_THRESHOLD) {
            m_touchScrolling = true;
        }
        
        if (m_touchScrolling) {
            int currentY = event->pos().y();
            
            // Manual touch scrolling: scroll by the Y delta from press position
            int deltaY = currentY - m_pressPos.y();
            int newScrollPos = m_touchScrollStartPos - deltaY;
            int oldScrollPos = verticalScrollBar()->value();
            verticalScrollBar()->setValue(newScrollPos);
            
            // Calculate velocity from actual scroll change
            int scrollDelta = newScrollPos - oldScrollPos;
            
            // Get time since last move and restart timer for next frame
            qint64 frameTime = m_velocityTimer.restart();
            
            if (frameTime > 0 && scrollDelta != 0) {
                qreal instantVelocity = static_cast<qreal>(scrollDelta) / static_cast<qreal>(frameTime);
                
                // Exponential smoothing
                constexpr qreal alpha = 0.4;
                m_lastVelocity = alpha * instantVelocity + (1.0 - alpha) * m_lastVelocity;
            } else if (frameTime > 50) {
                // If no movement for a while, decay velocity
                m_lastVelocity *= 0.5;
            }
        }
        
        event->accept();
        return;
    }
    
    // Mouse/stylus: let QTreeWidget handle normally
    QTreeWidget::mouseMoveEvent(event);
}

void OutlinePanelTreeWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        bool wasTouchInput = m_isTouchInput;
        bool wasScrolling = m_touchScrolling;
        
        m_isTouchInput = false;
        m_touchScrolling = false;
        
        if (wasTouchInput) {
            // If this was a tap (not a scroll), handle the action
            if (!wasScrolling) {
                QTreeWidgetItem* item = itemAt(event->pos());
                if (item) {
                    // Check if tap is on the expand/collapse indicator
                    // The indicator is in the indentation area (left of the item text)
                    int itemDepth = 0;
                    QTreeWidgetItem* parent = item->parent();
                    while (parent) {
                        itemDepth++;
                        parent = parent->parent();
                    }
                    
                    // Calculate the expand indicator region
                    // indentation() is the width per level, indicator is at the deepest level
                    int indicatorRight = indentation() * (itemDepth + 1);
                    int indicatorLeft = indentation() * itemDepth;
                    int clickX = event->pos().x();
                    
                    // Check if this item has children (can be expanded/collapsed)
                    bool hasChildren = item->childCount() > 0;
                    
                    if (hasChildren && clickX >= indicatorLeft && clickX < indicatorRight) {
                        // Toggle expand/collapse
                        item->setExpanded(!item->isExpanded());
                    } else {
                        // Regular item click - navigate to outline entry
                        emit itemClicked(item, 0);
                    }
                }
            } else {
                // Use the velocity that was calculated during dragging
                if (qAbs(m_lastVelocity) > KINETIC_MIN_VELOCITY) {
                    startKineticScroll(m_lastVelocity);
                }
            }
            event->accept();
            return;
        }
    }
    
    QTreeWidget::mouseReleaseEvent(event);
}

// ============================================================================
// Kinetic Scrolling
// ============================================================================

void OutlinePanelTreeWidget::startKineticScroll(qreal velocity)
{
    // Cap velocity to prevent excessive scrolling
    m_kineticVelocity = qBound(-KINETIC_MAX_VELOCITY, velocity, KINETIC_MAX_VELOCITY);
    m_kineticTimer.start();
}

void OutlinePanelTreeWidget::stopKineticScroll()
{
    if (m_kineticTimer.isActive()) {
        m_kineticTimer.stop();
        m_kineticVelocity = 0.0;
    }
}

void OutlinePanelTreeWidget::onKineticScrollTick()
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
