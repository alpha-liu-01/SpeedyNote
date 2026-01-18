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
}

void TimelineListView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressPos = event->pos();
        m_pressedIndex = indexAt(event->pos());
        m_longPressTriggered = false;
        
        // Only start timer if we pressed on a valid item
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
        
        // If long-press was triggered, don't process as a click
        if (m_longPressTriggered) {
            m_longPressTriggered = false;
            event->accept();
            return;
        }
    }
    
    // Call base class to handle normal release behavior
    QListView::mouseReleaseEvent(event);
}

void TimelineListView::mouseMoveEvent(QMouseEvent* event)
{
    // Cancel long-press if moved too far from initial press position
    if (m_longPressTimer.isActive()) {
        QPoint delta = event->pos() - m_pressPos;
        if (delta.manhattanLength() > LONG_PRESS_MOVE_THRESHOLD) {
            m_longPressTimer.stop();
        }
    }
    
    // Call base class to handle normal move behavior (scrolling, etc.)
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
