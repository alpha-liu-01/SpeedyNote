#include "SearchListView.h"
#include "SearchModel.h"
#include "NotebookCardDelegate.h"

SearchListView::SearchListView(QWidget* parent)
    : KineticListView(parent)
{
    // Configure view for grid-like display
    setViewMode(QListView::IconMode);
    setFlow(QListView::LeftToRight);
    setWrapping(true);
    setResizeMode(QListView::Adjust);
    setSpacing(12);  // Match GRID_SPACING from original SearchView
    setUniformItemSizes(true);
    
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

QString SearchListView::bundlePathForIndex(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return QString();
    }
    return index.data(SearchModel::BundlePathRole).toString();
}

bool SearchListView::isOnMenuButton(const QModelIndex& index, const QPoint& pos) const
{
    if (!index.isValid()) {
        return false;
    }
    
    QRect itemRect = visualRect(index);
    QRect menuRect = NotebookCardDelegate::menuButtonRect(itemRect);
    
    // Add some padding for easier clicking
    constexpr int HIT_PADDING = 8;
    menuRect.adjust(-HIT_PADDING, -HIT_PADDING, HIT_PADDING, HIT_PADDING);
    
    return menuRect.contains(pos);
}

void SearchListView::handleItemTap(const QModelIndex& index, const QPoint& pos)
{
    if (!index.isValid()) return;
    
    QString bundlePath = bundlePathForIndex(index);
    if (!bundlePath.isEmpty()) {
        // Check if tap was on menu button
        if (isOnMenuButton(index, pos)) {
            QPoint globalPos = viewport()->mapToGlobal(pos);
            emit notebookMenuRequested(bundlePath, globalPos);
        } else {
            emit notebookClicked(bundlePath);
        }
    }
}

void SearchListView::handleRightClick(const QModelIndex& index, const QPoint& globalPos)
{
    if (!index.isValid()) return;
    
    QString bundlePath = bundlePathForIndex(index);
    if (!bundlePath.isEmpty()) {
        emit notebookMenuRequested(bundlePath, globalPos);
    }
}

void SearchListView::handleLongPress(const QModelIndex& index, const QPoint& globalPos)
{
    // In SearchView, long-press shows context menu (no batch select)
    handleRightClick(index, globalPos);
}
