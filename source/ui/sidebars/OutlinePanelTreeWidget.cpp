#include "OutlinePanelTreeWidget.h"

#include <QMouseEvent>
#include <QScrollBar>

OutlinePanelTreeWidget::OutlinePanelTreeWidget(QWidget* parent)
    : QTreeWidget(parent)
{
}

void OutlinePanelTreeWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressPos = event->pos();
        m_touchScrolling = false;
        m_isTouchInput = (event->source() != Qt::MouseEventNotSynthesized);
        
        if (m_isTouchInput) {
            // Store scroll position at touch start for manual scrolling
            m_touchScrollStartPos = verticalScrollBar()->value();
            event->accept();
            return;
        }
    }
    
    // Mouse/stylus: let QTreeWidget handle normally
    QTreeWidget::mousePressEvent(event);
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
            }
            event->accept();
            return;
        }
    }
    
    QTreeWidget::mouseReleaseEvent(event);
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
            // Manual touch scrolling: scroll by the Y delta from press position
            int deltaY = event->pos().y() - m_pressPos.y();
            int newScrollPos = m_touchScrollStartPos - deltaY;
            verticalScrollBar()->setValue(newScrollPos);
        }
        
        event->accept();
        return;
    }
    
    // Mouse/stylus: let QTreeWidget handle normally
    QTreeWidget::mouseMoveEvent(event);
}
