#include "StarredListView.h"
#include "StarredModel.h"
#include "NotebookCardDelegate.h"

StarredListView::StarredListView(QWidget* parent)
    : KineticListView(parent)
{
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
    
    // Disable Qt's native selection highlight - delegate handles selection drawing
    // This prevents rectangular selection from showing around rounded cards
    setStyleSheet("QListView::item:selected { background: transparent; }"
                  "QListView::item:selected:active { background: transparent; }");
    
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

bool StarredListView::isOnMenuButton(const QModelIndex& index, const QPoint& pos) const
{
    if (!index.isValid() || isFolderHeader(index)) {
        return false;  // Only notebook cards have menu buttons
    }
    
    QRect itemRect = visualRect(index);
    QRect menuRect = NotebookCardDelegate::menuButtonRect(itemRect);
    
    // Add some padding for easier clicking
    constexpr int HIT_PADDING = 8;
    menuRect.adjust(-HIT_PADDING, -HIT_PADDING, HIT_PADDING, HIT_PADDING);
    
    return menuRect.contains(pos);
}

void StarredListView::handleItemTap(const QModelIndex& index, const QPoint& pos)
{
    if (!index.isValid()) return;
    
    if (isFolderHeader(index)) {
        // Folder header: toggle collapsed state
        QString folderName = folderNameForIndex(index);
        if (!folderName.isEmpty()) {
            if (m_starredModel) {
                m_starredModel->toggleFolder(folderName);
            }
            emit folderClicked(folderName);
        }
    } else {
        // Notebook card: check if tap was on menu button
        QString bundlePath = bundlePathForIndex(index);
        if (!bundlePath.isEmpty()) {
            if (isOnMenuButton(index, pos)) {
                QPoint globalPos = viewport()->mapToGlobal(pos);
                emit notebookMenuRequested(bundlePath, globalPos);
            } else {
                emit notebookClicked(bundlePath);
            }
        }
    }
}

void StarredListView::handleRightClick(const QModelIndex& index, const QPoint& globalPos)
{
    if (!index.isValid()) return;
    
    if (isFolderHeader(index)) {
        QString folderName = folderNameForIndex(index);
        if (!folderName.isEmpty()) {
            emit folderLongPressed(folderName, globalPos);
        }
    } else {
        QString bundlePath = bundlePathForIndex(index);
        if (!bundlePath.isEmpty()) {
            emit notebookMenuRequested(bundlePath, globalPos);
        }
    }
}

void StarredListView::handleLongPress(const QModelIndex& index, const QPoint& globalPos)
{
    if (!index.isValid()) return;
    
    if (isFolderHeader(index)) {
        QString folderName = folderNameForIndex(index);
        if (!folderName.isEmpty()) {
            emit folderLongPressed(folderName, globalPos);
        }
    } else {
        QString bundlePath = bundlePathForIndex(index);
        if (!bundlePath.isEmpty()) {
            // Long-press emits notebookLongPressed for batch select mode (L-007)
            emit notebookLongPressed(bundlePath, globalPos);
        }
    }
}
