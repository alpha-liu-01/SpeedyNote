#include "TimelineListView.h"
#include "TimelineModel.h"
#include "NotebookCardDelegate.h"

TimelineListView::TimelineListView(QWidget* parent)
    : KineticListView(parent)
{
    // Configure view for mixed content (section headers + notebook cards grid)
    // Use IconMode for grid layout of notebook cards.
    // Section headers return a wide sizeHint so they span their own row.
    setViewMode(QListView::IconMode);
    setFlow(QListView::LeftToRight);
    setWrapping(true);
    setResizeMode(QListView::Adjust);
    setSpacing(12);  // Match GRID_SPACING
    setUniformItemSizes(false);  // Different sizes for headers vs cards
    
    // Visual settings
    setSelectionMode(QAbstractItemView::SingleSelection);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setFrameShape(QFrame::NoFrame);
    
    // Disable Qt's native selection highlight - delegate handles selection drawing
    // This prevents rectangular selection from showing around rounded cards
    setStyleSheet("QListView::item:selected { background: transparent; }"
                  "QListView::item:selected:active { background: transparent; }");
    
    // Enable mouse tracking for hover effects
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
}

bool TimelineListView::isSectionHeader(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return false;
    }
    return index.data(TimelineModel::IsSectionHeaderRole).toBool();
}

bool TimelineListView::isOnMenuButton(const QModelIndex& index, const QPoint& pos) const
{
    if (!index.isValid() || isSectionHeader(index)) {
        return false;  // Only notebook cards have menu buttons
    }
    
    // In IconMode, visualRect returns the correct card rect
    QRect itemRect = visualRect(index);
    QRect menuRect = NotebookCardDelegate::menuButtonRect(itemRect);
    
    // Add some padding for easier clicking
    constexpr int HIT_PADDING = 8;
    menuRect.adjust(-HIT_PADDING, -HIT_PADDING, HIT_PADDING, HIT_PADDING);
    
    return menuRect.contains(pos);
}

void TimelineListView::handleItemTap(const QModelIndex& index, const QPoint& pos)
{
    if (!index.isValid()) return;
    
    // Section headers: just emit clicked
    if (isSectionHeader(index)) {
        emit clicked(index);
        return;
    }
    
    // Notebook card: check if tap was on menu button
    if (isOnMenuButton(index, pos)) {
        QPoint globalPos = viewport()->mapToGlobal(pos);
        emit menuRequested(index, globalPos);
    } else {
        emit clicked(index);
    }
}

void TimelineListView::handleRightClick(const QModelIndex& index, const QPoint& globalPos)
{
    if (!index.isValid() || isSectionHeader(index)) {
        return;
    }
    emit menuRequested(index, globalPos);
}

void TimelineListView::handleLongPress(const QModelIndex& index, const QPoint& globalPos)
{
    if (!index.isValid() || isSectionHeader(index)) {
        return;
    }
    // Long-press emits longPressed for batch select mode (L-007)
    emit longPressed(index, globalPos);
}

