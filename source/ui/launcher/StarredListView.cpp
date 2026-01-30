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

// -----------------------------------------------------------------------------
// Batch Select Mode (L-007)
// -----------------------------------------------------------------------------

void StarredListView::enterSelectMode(const QString& firstSelection)
{
    if (m_selectMode) {
        return;  // Already in select mode
    }
    
    m_selectMode = true;
    m_selectedBundlePaths.clear();
    
    // Add the first selection
    if (!firstSelection.isEmpty()) {
        m_selectedBundlePaths.insert(firstSelection);
    }
    
    // Sync with model for delegate painting
    if (m_starredModel) {
        m_starredModel->setSelectMode(true);
        m_starredModel->setSelectedBundlePaths(m_selectedBundlePaths);
    }
    
    emit selectModeChanged(true);
    emit batchSelectionChanged(m_selectedBundlePaths.size());
}

void StarredListView::exitSelectMode()
{
    if (!m_selectMode) {
        return;  // Not in select mode
    }
    
    m_selectMode = false;
    m_selectedBundlePaths.clear();
    
    // Sync with model for delegate painting
    if (m_starredModel) {
        m_starredModel->setSelectMode(false);
    }
    
    emit selectModeChanged(false);
    emit batchSelectionChanged(0);
}

void StarredListView::toggleSelection(const QString& bundlePath)
{
    if (!m_selectMode || bundlePath.isEmpty()) {
        return;
    }
    
    if (m_selectedBundlePaths.contains(bundlePath)) {
        m_selectedBundlePaths.remove(bundlePath);
    } else {
        m_selectedBundlePaths.insert(bundlePath);
    }
    
    // Sync with model for delegate painting
    if (m_starredModel) {
        m_starredModel->setSelectedBundlePaths(m_selectedBundlePaths);
    }
    
    emit batchSelectionChanged(m_selectedBundlePaths.size());
}

void StarredListView::selectAll()
{
    if (!m_selectMode || !m_starredModel) {
        return;
    }
    
    // Iterate through all items and select notebook cards (not folder headers)
    int rowCount = m_starredModel->rowCount();
    for (int i = 0; i < rowCount; ++i) {
        QModelIndex index = m_starredModel->index(i, 0);
        if (!isFolderHeader(index)) {
            QString bundlePath = bundlePathForIndex(index);
            if (!bundlePath.isEmpty()) {
                m_selectedBundlePaths.insert(bundlePath);
            }
        }
    }
    
    // Sync with model for delegate painting
    m_starredModel->setSelectedBundlePaths(m_selectedBundlePaths);
    
    emit batchSelectionChanged(m_selectedBundlePaths.size());
}

void StarredListView::deselectAll()
{
    if (!m_selectMode) {
        return;
    }
    
    m_selectedBundlePaths.clear();
    
    // Sync with model for delegate painting
    if (m_starredModel) {
        m_starredModel->setSelectedBundlePaths(m_selectedBundlePaths);
    }
    
    emit batchSelectionChanged(0);
}

QStringList StarredListView::selectedBundlePaths() const
{
    return QStringList(m_selectedBundlePaths.begin(), m_selectedBundlePaths.end());
}

bool StarredListView::isSelected(const QString& bundlePath) const
{
    return m_selectedBundlePaths.contains(bundlePath);
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
        // Folder header: toggle collapsed state (same in normal and select mode)
        QString folderName = folderNameForIndex(index);
        if (!folderName.isEmpty()) {
            if (m_starredModel) {
                m_starredModel->toggleFolder(folderName);
            }
            emit folderClicked(folderName);
        }
    } else {
        // Notebook card
        QString bundlePath = bundlePathForIndex(index);
        if (!bundlePath.isEmpty()) {
            if (m_selectMode) {
                // In select mode: tap toggles selection
                toggleSelection(bundlePath);
            } else {
                // Normal mode: check if tap was on menu button
                if (isOnMenuButton(index, pos)) {
                    QPoint globalPos = viewport()->mapToGlobal(pos);
                    emit notebookMenuRequested(bundlePath, globalPos);
                } else {
                    emit notebookClicked(bundlePath);
                }
            }
        }
    }
}

void StarredListView::handleRightClick(const QModelIndex& index, const QPoint& globalPos)
{
    if (!index.isValid()) return;
    
    // In select mode, right-click does nothing (3-dot menu is hidden)
    // Bulk actions are accessed via the header overflow menu
    if (m_selectMode) {
        return;
    }
    
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
        // Folder header: context menu (same in normal and select mode)
        QString folderName = folderNameForIndex(index);
        if (!folderName.isEmpty()) {
            emit folderLongPressed(folderName, globalPos);
        }
    } else {
        QString bundlePath = bundlePathForIndex(index);
        if (!bundlePath.isEmpty()) {
            if (m_selectMode) {
                // Already in select mode: long-press toggles selection
                toggleSelection(bundlePath);
            } else {
                // Not in select mode: emit signal to enter select mode
                emit notebookLongPressed(bundlePath, globalPos);
            }
        }
    }
}
