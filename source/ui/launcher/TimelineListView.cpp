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

void TimelineListView::setTimelineModel(TimelineModel* model)
{
    m_timelineModel = model;
    setModel(model);
}

// -----------------------------------------------------------------------------
// Batch Select Mode (L-007)
// -----------------------------------------------------------------------------

void TimelineListView::enterSelectMode(const QString& firstSelection)
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
    if (m_timelineModel) {
        m_timelineModel->setSelectMode(true);
        m_timelineModel->setSelectedBundlePaths(m_selectedBundlePaths);
    }
    
    emit selectModeChanged(true);
    emit batchSelectionChanged(m_selectedBundlePaths.size());
}

void TimelineListView::exitSelectMode()
{
    if (!m_selectMode) {
        return;  // Not in select mode
    }
    
    m_selectMode = false;
    m_selectedBundlePaths.clear();
    
    // Sync with model for delegate painting
    if (m_timelineModel) {
        m_timelineModel->setSelectMode(false);
    }
    
    emit selectModeChanged(false);
    emit batchSelectionChanged(0);
}

void TimelineListView::toggleSelection(const QString& bundlePath)
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
    if (m_timelineModel) {
        m_timelineModel->setSelectedBundlePaths(m_selectedBundlePaths);
    }
    
    emit batchSelectionChanged(m_selectedBundlePaths.size());
}

void TimelineListView::selectAll()
{
    if (!m_selectMode || !m_timelineModel) {
        return;
    }
    
    // Iterate through all items and select notebook cards (not section headers)
    int rowCount = m_timelineModel->rowCount();
    for (int i = 0; i < rowCount; ++i) {
        QModelIndex index = m_timelineModel->index(i, 0);
        if (!isSectionHeader(index)) {
            QString bundlePath = bundlePathForIndex(index);
            if (!bundlePath.isEmpty()) {
                m_selectedBundlePaths.insert(bundlePath);
            }
        }
    }
    
    // Sync with model for delegate painting
    m_timelineModel->setSelectedBundlePaths(m_selectedBundlePaths);
    
    emit batchSelectionChanged(m_selectedBundlePaths.size());
}

void TimelineListView::deselectAll()
{
    if (!m_selectMode) {
        return;
    }
    
    m_selectedBundlePaths.clear();
    
    // Sync with model for delegate painting
    if (m_timelineModel) {
        m_timelineModel->setSelectedBundlePaths(m_selectedBundlePaths);
    }
    
    emit batchSelectionChanged(0);
}

QStringList TimelineListView::selectedBundlePaths() const
{
    return QStringList(m_selectedBundlePaths.begin(), m_selectedBundlePaths.end());
}

bool TimelineListView::isSelected(const QString& bundlePath) const
{
    return m_selectedBundlePaths.contains(bundlePath);
}

// -----------------------------------------------------------------------------
// Private Helpers
// -----------------------------------------------------------------------------

bool TimelineListView::isSectionHeader(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return false;
    }
    return index.data(TimelineModel::IsSectionHeaderRole).toBool();
}

QString TimelineListView::bundlePathForIndex(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return QString();
    }
    return index.data(TimelineModel::BundlePathRole).toString();
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

// -----------------------------------------------------------------------------
// Event Handlers
// -----------------------------------------------------------------------------

void TimelineListView::handleItemTap(const QModelIndex& index, const QPoint& pos)
{
    if (!index.isValid()) return;
    
    // Section headers: just emit clicked (same in normal and select mode)
    if (isSectionHeader(index)) {
        emit clicked(index);
        return;
    }
    
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
                emit menuRequested(index, globalPos);
            } else {
                emit clicked(index);
            }
        }
    }
}

void TimelineListView::handleRightClick(const QModelIndex& index, const QPoint& globalPos)
{
    if (!index.isValid() || isSectionHeader(index)) {
        return;
    }
    
    // In select mode, right-click does nothing (3-dot menu is hidden)
    if (m_selectMode) {
        return;
    }
    
    emit menuRequested(index, globalPos);
}

void TimelineListView::handleLongPress(const QModelIndex& index, const QPoint& globalPos)
{
    if (!index.isValid() || isSectionHeader(index)) {
        return;
    }
    
    QString bundlePath = bundlePathForIndex(index);
    if (!bundlePath.isEmpty()) {
        if (m_selectMode) {
            // Already in select mode: long-press toggles selection
            toggleSelection(bundlePath);
        } else {
            // Not in select mode: emit signal to enter select mode
            emit longPressed(index, globalPos);
        }
    }
}

