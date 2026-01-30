#include "StarredListView.h"
#include "StarredModel.h"
#include "KineticScrollHelper.h"

#include <QMouseEvent>
#include <QScrollBar>

StarredListView::StarredListView(QWidget* parent)
    : QListView(parent)
{
    // Configure long-press timer
    m_longPressTimer.setSingleShot(true);
    m_longPressTimer.setInterval(LONG_PRESS_MS);
    connect(&m_longPressTimer, &QTimer::timeout,
            this, &StarredListView::onLongPressTimeout);
    
    // Setup kinetic scroll helper
    m_kineticHelper = new KineticScrollHelper(verticalScrollBar(), this);
    
    // Configure view for mixed content (folder headers + notebook cards grid)
    // Use IconMode for grid layout of notebook cards.
    // Folder headers return a wide sizeHint (viewport width) so they span their own row.
    setViewMode(QListView::IconMode);
    setFlow(QListView::LeftToRight);
    setWrapping(true);
    setResizeMode(QListView::Adjust);
    setSpacing(12);  // Match GRID_SPACING from original StarredView
    setUniformItemSizes(false);  // Different sizes for headers vs cards
    
    // Visual settings
    setSelectionMode(QAbstractItemView::SingleSelection);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setFrameShape(QFrame::NoFrame);
    
    // Enable mouse tracking for hover effects
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
}

void StarredListView::setStarredModel(StarredModel* model)
{
    m_starredModel = model;
    setModel(model);
}

bool StarredListView::isFolderHeader(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return false;
    }
    int itemType = index.data(StarredModel::ItemTypeRole).toInt();
    return itemType == StarredModel::FolderHeaderItem;
}

QString StarredListView::folderNameForIndex(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return QString();
    }
    return index.data(StarredModel::FolderNameRole).toString();
}

QString StarredListView::bundlePathForIndex(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return QString();
    }
    return index.data(StarredModel::BundlePathRole).toString();
}

void StarredListView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        // Stop any ongoing kinetic scroll and start tracking
        m_kineticHelper->startTracking();
        
        m_pressPos = event->pos();
        m_pressedIndex = indexAt(event->pos());
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
        
        // Only start long-press timer if we pressed on a valid item
        if (m_pressedIndex.isValid()) {
            m_longPressTimer.start();
        }
    }
    else if (event->button() == Qt::RightButton) {
        // Right-click triggers context menu (same as long-press)
        QModelIndex index = indexAt(event->pos());
        if (index.isValid()) {
            QPoint globalPos = viewport()->mapToGlobal(event->pos());
            
            if (isFolderHeader(index)) {
                QString folderName = folderNameForIndex(index);
                if (!folderName.isEmpty()) {
                    emit folderLongPressed(folderName, globalPos);
                }
            } else {
                QString bundlePath = bundlePathForIndex(index);
                if (!bundlePath.isEmpty()) {
                    emit notebookLongPressed(bundlePath, globalPos);
                }
            }
        }
        event->accept();
        return;
    }
    
    // Call base class to handle normal click behavior (mouse only)
    QListView::mousePressEvent(event);
}

void StarredListView::mouseReleaseEvent(QMouseEvent* event)
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
            
            // It was a tap (no scroll) - handle based on item type
            if (m_pressedIndex.isValid()) {
                if (isFolderHeader(m_pressedIndex)) {
                    QString folderName = folderNameForIndex(m_pressedIndex);
                    if (!folderName.isEmpty()) {
                        // Toggle folder collapsed state
                        if (m_starredModel) {
                            m_starredModel->toggleFolder(folderName);
                        }
                        emit folderClicked(folderName);
                    }
                } else {
                    QString bundlePath = bundlePathForIndex(m_pressedIndex);
                    if (!bundlePath.isEmpty()) {
                        emit notebookClicked(bundlePath);
                    }
                }
            }
            event->accept();
            return;
        }
        
        // For mouse input, handle click on release
        if (m_pressedIndex.isValid() && indexAt(event->pos()) == m_pressedIndex) {
            if (isFolderHeader(m_pressedIndex)) {
                QString folderName = folderNameForIndex(m_pressedIndex);
                if (!folderName.isEmpty()) {
                    // Toggle folder collapsed state
                    if (m_starredModel) {
                        m_starredModel->toggleFolder(folderName);
                    }
                    emit folderClicked(folderName);
                }
            } else {
                QString bundlePath = bundlePathForIndex(m_pressedIndex);
                if (!bundlePath.isEmpty()) {
                    emit notebookClicked(bundlePath);
                }
            }
        }
    }
    
    // Call base class to handle normal release behavior
    QListView::mouseReleaseEvent(event);
}

void StarredListView::mouseMoveEvent(QMouseEvent* event)
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

void StarredListView::onLongPressTimeout()
{
    m_longPressTriggered = true;
    
    // Emit appropriate signal based on item type
    if (m_pressedIndex.isValid()) {
        QPoint globalPos = viewport()->mapToGlobal(m_pressPos);
        
        if (isFolderHeader(m_pressedIndex)) {
            QString folderName = folderNameForIndex(m_pressedIndex);
            if (!folderName.isEmpty()) {
                emit folderLongPressed(folderName, globalPos);
            }
        } else {
            QString bundlePath = bundlePathForIndex(m_pressedIndex);
            if (!bundlePath.isEmpty()) {
                emit notebookLongPressed(bundlePath, globalPos);
            }
        }
    }
    
    // Clear selection state to prevent accidental clicks after menu closes
    clearSelection();
}
