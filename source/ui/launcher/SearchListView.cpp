#include "SearchListView.h"
#include "SearchModel.h"
#include "NotebookCardDelegate.h"

SearchListView::SearchListView(QWidget* parent)
    : KineticListView(parent)
{
    // Configure view for grid-like display with mixed item sizes
    // L-009: Must use non-uniform sizes so section headers and folders span full width
    setViewMode(QListView::IconMode);
    setFlow(QListView::LeftToRight);
    setWrapping(true);
    setResizeMode(QListView::Adjust);
    setSpacing(12);  // Match GRID_SPACING from original SearchView
    setUniformItemSizes(false);  // Required for mixed section headers + cards
    
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
    
    // Check item type (L-009)
    int itemType = index.data(SearchModel::ItemTypeRole).toInt();
    
    switch (itemType) {
        case SearchModel::SectionHeaderItem:
            // Section headers are not clickable
            return;
            
        case SearchModel::FolderResultItem: {
            // Folder item - emit folder clicked signal
            QString folderName = index.data(SearchModel::FolderNameRole).toString();
            if (!folderName.isEmpty()) {
                emit folderClicked(folderName);
            }
            break;
        }
        
        case SearchModel::NotebookResultItem: {
            // Notebook item
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
            break;
        }
    }
}

void SearchListView::handleRightClick(const QModelIndex& index, const QPoint& globalPos)
{
    if (!index.isValid()) return;
    
    // Only notebooks have context menus (L-009)
    int itemType = index.data(SearchModel::ItemTypeRole).toInt();
    if (itemType != SearchModel::NotebookResultItem) {
        return;  // No context menu for section headers or folder results
    }
    
    QString bundlePath = bundlePathForIndex(index);
    if (!bundlePath.isEmpty()) {
        emit notebookMenuRequested(bundlePath, globalPos);
    }
}

void SearchListView::handleLongPress(const QModelIndex& index, const QPoint& globalPos)
{
    if (!index.isValid()) return;
    
    int itemType = index.data(SearchModel::ItemTypeRole).toInt();
    
    switch (itemType) {
        case SearchModel::SectionHeaderItem:
            // Section headers - ignore long press
            return;
            
        case SearchModel::FolderResultItem: {
            // Folder - long press navigates to folder
            QString folderName = index.data(SearchModel::FolderNameRole).toString();
            if (!folderName.isEmpty()) {
                emit folderClicked(folderName);
            }
            return;
        }
        
        case SearchModel::NotebookResultItem:
            // Notebook - long press shows context menu
            handleRightClick(index, globalPos);
            return;
    }
}
