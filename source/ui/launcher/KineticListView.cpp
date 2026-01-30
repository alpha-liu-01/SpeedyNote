#include "KineticListView.h"
#include "KineticScrollHelper.h"

#include <QMouseEvent>
#include <QScrollBar>

KineticListView::KineticListView(QWidget* parent)
    : QListView(parent)
{
    // Configure long-press timer
    m_longPressTimer.setSingleShot(true);
    m_longPressTimer.setInterval(LONG_PRESS_MS);
    connect(&m_longPressTimer, &QTimer::timeout,
            this, &KineticListView::onLongPressTimeout);
    
    // Setup kinetic scroll helper
    m_kineticHelper = new KineticScrollHelper(verticalScrollBar(), this);
}

void KineticListView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        // Stop any ongoing kinetic scroll and start tracking
        m_kineticHelper->startTracking();
        
        m_pressPos = event->pos();
        m_pressedIndex = indexAtPosition(event->pos());
        m_longPressTriggered = false;
        m_touchScrolling = false;
        m_scrollStartValue = verticalScrollBar()->value();
        
        // For touch input, don't let base class handle press yet
        // We'll decide on release whether it was a tap or scroll
        if (KineticScrollHelper::isTouchInput(event)) {
            // Only start long-press timer if we pressed on a valid item
            if (m_pressedIndex.isValid()) {
                m_longPressTimer.start();
            }
            event->accept();
            return;
        }
        
        // For mouse input, also start long-press timer
        if (m_pressedIndex.isValid()) {
            m_longPressTimer.start();
        }
    }
    else if (event->button() == Qt::RightButton) {
        // Right-click triggers context menu
        QModelIndex index = indexAtPosition(event->pos());
        if (index.isValid()) {
            QPoint globalPos = viewport()->mapToGlobal(event->pos());
            handleRightClick(index, globalPos);
        }
        event->accept();
        return;
    }
    
    // Call base class to handle normal click behavior (mouse only)
    QListView::mousePressEvent(event);
}

void KineticListView::mouseReleaseEvent(QMouseEvent* event)
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
        
        // Handle touch input specially
        if (KineticScrollHelper::isTouchInput(event)) {
            if (wasScrolling) {
                // Start kinetic scroll if velocity is high enough
                m_kineticHelper->finishTracking();
                event->accept();
                return;
            }
            
            // It was a tap (no scroll) - handle the tap
            if (m_pressedIndex.isValid()) {
                handleItemTap(m_pressedIndex, event->pos());
            }
            event->accept();
            return;
        }
        
        // For mouse input, verify release is on same item
        if (m_pressedIndex.isValid()) {
            QModelIndex releaseIndex = indexAtPosition(event->pos());
            if (releaseIndex == m_pressedIndex) {
                handleItemTap(m_pressedIndex, event->pos());
            }
        }
        
        // We've handled the left button release - don't let base class
        // emit clicked() on the selected item when clicking empty space
        event->accept();
        return;
    }
    
    // Call base class only for other mouse buttons
    QListView::mouseReleaseEvent(event);
}

void KineticListView::mouseMoveEvent(QMouseEvent* event)
{
    if ((event->buttons() & Qt::LeftButton)) {
        QPoint delta = event->pos() - m_pressPos;
        
        // Cancel long-press if moved beyond threshold
        if (delta.manhattanLength() > LONG_PRESS_MOVE_THRESHOLD) {
            if (m_longPressTimer.isActive()) {
                m_longPressTimer.stop();
            }
            
            // Start touch scrolling for touch input
            if (KineticScrollHelper::isTouchInput(event) && !m_touchScrolling) {
                m_touchScrolling = true;
            }
        }
        
        // Handle touch scrolling
        if (m_touchScrolling && KineticScrollHelper::isTouchInput(event)) {
            // Apply scroll
            int newValue = m_scrollStartValue - delta.y();
            int oldValue = verticalScrollBar()->value();
            verticalScrollBar()->setValue(newValue);
            
            // Update velocity tracking
            int scrollDelta = verticalScrollBar()->value() - oldValue;
            m_kineticHelper->updateVelocity(scrollDelta);
            
            event->accept();
            return;
        }
    }
    
    // Call base class to handle normal move behavior
    QListView::mouseMoveEvent(event);
}

void KineticListView::onLongPressTimeout()
{
    m_longPressTriggered = true;
    
    if (m_pressedIndex.isValid()) {
        QPoint globalPos = viewport()->mapToGlobal(m_pressPos);
        handleLongPress(m_pressedIndex, globalPos);
    }
    
    // Clear selection state to prevent accidental clicks after menu closes
    clearSelection();
}

void KineticListView::handleItemTap(const QModelIndex& index, const QPoint& pos)
{
    Q_UNUSED(pos)
    // Default: just emit clicked signal
    if (index.isValid()) {
        emit clicked(index);
    }
}

void KineticListView::handleRightClick(const QModelIndex& index, const QPoint& globalPos)
{
    // Default: same as long press
    handleLongPress(index, globalPos);
}

void KineticListView::handleLongPress(const QModelIndex& index, const QPoint& globalPos)
{
    // Default: emit itemLongPressed signal
    if (index.isValid()) {
        emit itemLongPressed(index, globalPos);
    }
}

QModelIndex KineticListView::indexAtPosition(const QPoint& pos) const
{
    // Default: just use standard indexAt
    return indexAt(pos);
}
