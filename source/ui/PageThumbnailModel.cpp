#include "PageThumbnailModel.h"
#include "ThumbnailRenderer.h"
#include "../core/Document.h"
#include "../core/Page.h"

#include <QMimeData>
#include <QByteArray>
#include <QDataStream>

// ============================================================================
// Constructor / Destructor
// ============================================================================

PageThumbnailModel::PageThumbnailModel(QObject* parent)
    : QAbstractListModel(parent)
    , m_renderer(new ThumbnailRenderer(this))
{
    // Connect renderer signals
    connect(m_renderer, &ThumbnailRenderer::thumbnailReady,
            this, &PageThumbnailModel::onThumbnailRendered);
}

PageThumbnailModel::~PageThumbnailModel()
{
}

// ============================================================================
// QAbstractListModel Interface
// ============================================================================

int PageThumbnailModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;  // No children for list model
    }
    
    if (!m_document) {
        return 0;
    }
    
    return m_document->pageCount();
}

QVariant PageThumbnailModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || !m_document) {
        return QVariant();
    }
    
    const int pageIndex = index.row();
    if (pageIndex < 0 || pageIndex >= m_document->pageCount()) {
        return QVariant();
    }
    
    switch (role) {
        case Qt::DisplayRole:
            // Return page number (1-based) as display text
            return QString::number(pageIndex + 1);
            
        case PageIndexRole:
            return pageIndex;
            
        case ThumbnailRole:
            return QVariant::fromValue(thumbnailForPage(pageIndex));
            
        case IsCurrentPageRole:
            return (pageIndex == m_currentPageIndex);
            
        case IsPdfPageRole:
            return isPdfPage(pageIndex);
            
        case CanDragRole:
            return canDragPage(pageIndex);
            
        default:
            return QVariant();
    }
}

Qt::ItemFlags PageThumbnailModel::flags(const QModelIndex& index) const
{
    Qt::ItemFlags defaultFlags = QAbstractListModel::flags(index);
    
    if (!index.isValid() || !m_document) {
        return defaultFlags | Qt::ItemIsDropEnabled;
    }
    
    const int pageIndex = index.row();
    
    // All items are selectable and enabled
    Qt::ItemFlags itemFlags = defaultFlags | Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    
    // Only non-PDF pages can be dragged
    if (canDragPage(pageIndex)) {
        itemFlags |= Qt::ItemIsDragEnabled;
    }
    
    return itemFlags;
}

QHash<int, QByteArray> PageThumbnailModel::roleNames() const
{
    QHash<int, QByteArray> roles = QAbstractListModel::roleNames();
    roles[PageIndexRole] = "pageIndex";
    roles[ThumbnailRole] = "thumbnail";
    roles[IsCurrentPageRole] = "isCurrentPage";
    roles[IsPdfPageRole] = "isPdfPage";
    roles[CanDragRole] = "canDrag";
    return roles;
}

// ============================================================================
// Drag-and-Drop Support
// ============================================================================

Qt::DropActions PageThumbnailModel::supportedDropActions() const
{
    return Qt::MoveAction;
}

QStringList PageThumbnailModel::mimeTypes() const
{
    QStringList types;
    types << MIME_TYPE;
    return types;
}

QMimeData* PageThumbnailModel::mimeData(const QModelIndexList& indexes) const
{
    if (indexes.isEmpty()) {
        return nullptr;
    }
    
    // Only use the first index (single selection)
    const QModelIndex& index = indexes.first();
    if (!index.isValid()) {
        return nullptr;
    }
    
    const int pageIndex = index.row();
    
    // Don't allow dragging PDF pages
    if (!canDragPage(pageIndex)) {
        return nullptr;
    }
    
    // Encode the page index
    QMimeData* mimeData = new QMimeData();
    QByteArray encodedData;
    QDataStream stream(&encodedData, QIODevice::WriteOnly);
    stream << pageIndex;
    mimeData->setData(MIME_TYPE, encodedData);
    
    return mimeData;
}

bool PageThumbnailModel::canDropMimeData(const QMimeData* data, Qt::DropAction action,
                                          int row, int column, const QModelIndex& parent) const
{
    Q_UNUSED(column);
    Q_UNUSED(parent);
    
    if (!data || !data->hasFormat(MIME_TYPE)) {
        return false;
    }
    
    if (action != Qt::MoveAction) {
        return false;
    }
    
    // Can drop anywhere in the list
    if (row < 0 || !m_document) {
        return false;
    }
    
    return row <= m_document->pageCount();
}

bool PageThumbnailModel::dropMimeData(const QMimeData* data, Qt::DropAction action,
                                       int row, int column, const QModelIndex& parent)
{
    Q_UNUSED(column);
    Q_UNUSED(parent);
    
    if (!canDropMimeData(data, action, row, column, parent)) {
        return false;
    }
    
    // Decode the source page index
    QByteArray encodedData = data->data(MIME_TYPE);
    QDataStream stream(&encodedData, QIODevice::ReadOnly);
    int sourceIndex;
    stream >> sourceIndex;
    
    // Calculate target index
    int targetIndex = row;
    
    // If dropping after the source, adjust for the removal
    if (targetIndex > sourceIndex) {
        targetIndex--;
    }
    
    // Don't do anything if dropping in the same position
    if (sourceIndex == targetIndex) {
        return false;
    }
    
    // Emit signal for the move (let the caller handle the actual move)
    emit pageDropped(sourceIndex, targetIndex);
    
    return true;
}

// ============================================================================
// Document Binding
// ============================================================================

void PageThumbnailModel::setDocument(Document* doc)
{
    if (m_document == doc) {
        return;
    }
    
    beginResetModel();
    
    // Cancel any pending thumbnail requests for old document
    m_renderer->cancelAll();
    
    m_document = doc;
    m_currentPageIndex = 0;
    m_thumbnailCache.clear();
    m_pendingThumbnails.clear();
    
    endResetModel();
}

void PageThumbnailModel::setCurrentPageIndex(int index)
{
    if (!m_document || index < 0 || index >= m_document->pageCount()) {
        return;
    }
    
    if (m_currentPageIndex != index) {
        const int oldIndex = m_currentPageIndex;
        m_currentPageIndex = index;
        
        // Emit dataChanged for the old and new current pages
        if (oldIndex >= 0 && oldIndex < m_document->pageCount()) {
            const QModelIndex oldModelIndex = createIndex(oldIndex, 0);
            emit dataChanged(oldModelIndex, oldModelIndex, {IsCurrentPageRole});
        }
        
        const QModelIndex newModelIndex = createIndex(index, 0);
        emit dataChanged(newModelIndex, newModelIndex, {IsCurrentPageRole});
    }
}

// ============================================================================
// Thumbnail Management
// ============================================================================

void PageThumbnailModel::setThumbnailWidth(int width)
{
    if (m_thumbnailWidth != width && width > 0) {
        m_thumbnailWidth = width;
        
        // Cancel pending requests (they're for the old size)
        m_renderer->cancelAll();
        
        // Invalidate all thumbnails since size changed
        invalidateAllThumbnails();
    }
}

void PageThumbnailModel::setDevicePixelRatio(qreal dpr)
{
    if (!qFuzzyCompare(m_devicePixelRatio, dpr) && dpr > 0) {
        m_devicePixelRatio = dpr;
        
        // Cancel pending requests (they're for the old DPR)
        m_renderer->cancelAll();
        
        // Invalidate all thumbnails since DPR changed
        invalidateAllThumbnails();
    }
}

QPixmap PageThumbnailModel::thumbnailForPage(int pageIndex) const
{
    // Return cached thumbnail if available
    if (m_thumbnailCache.contains(pageIndex)) {
        return m_thumbnailCache.value(pageIndex);
    }
    
    // Request thumbnail if not already pending
    requestThumbnail(pageIndex);
    
    // Return null pixmap - delegate will show placeholder
    return QPixmap();
}

void PageThumbnailModel::invalidateThumbnail(int pageIndex)
{
    bool hadCache = m_thumbnailCache.contains(pageIndex);
    bool wasPending = m_pendingThumbnails.contains(pageIndex);
    
    if (hadCache || wasPending) {
        m_thumbnailCache.remove(pageIndex);
        m_pendingThumbnails.remove(pageIndex);
        
        // Notify view that the thumbnail data changed
        if (m_document && pageIndex >= 0 && pageIndex < m_document->pageCount()) {
            const QModelIndex modelIndex = createIndex(pageIndex, 0);
            emit dataChanged(modelIndex, modelIndex, {ThumbnailRole});
        }
    }
}

void PageThumbnailModel::invalidateAllThumbnails()
{
    // Cancel all pending renders
    m_renderer->cancelAll();
    
    m_thumbnailCache.clear();
    m_pendingThumbnails.clear();
    
    // Notify view that all data changed
    if (m_document && m_document->pageCount() > 0) {
        emit dataChanged(createIndex(0, 0), 
                         createIndex(m_document->pageCount() - 1, 0),
                         {ThumbnailRole});
    }
}

// ============================================================================
// Slots
// ============================================================================

void PageThumbnailModel::onPageCountChanged()
{
    // Reset the model when page count changes
    // This is simpler than tracking individual inserts/removes
    beginResetModel();
    
    // Cancel pending renders (indices may have changed)
    m_renderer->cancelAll();
    
    // Clear cache since page indices may have changed
    m_thumbnailCache.clear();
    m_pendingThumbnails.clear();
    
    // Clamp current page index
    if (m_document && m_currentPageIndex >= m_document->pageCount()) {
        m_currentPageIndex = qMax(0, m_document->pageCount() - 1);
    }
    
    endResetModel();
}

void PageThumbnailModel::onPageContentChanged(int pageIndex)
{
    invalidateThumbnail(pageIndex);
}

void PageThumbnailModel::onThumbnailRendered(int pageIndex, QPixmap thumbnail)
{
    // Remove from pending set
    m_pendingThumbnails.remove(pageIndex);
    
    // Validate page index is still valid
    if (!m_document || pageIndex < 0 || pageIndex >= m_document->pageCount()) {
        return;
    }
    
    // Cache the thumbnail
    m_thumbnailCache[pageIndex] = thumbnail;
    
    // Notify view that the thumbnail is ready
    const QModelIndex modelIndex = createIndex(pageIndex, 0);
    emit dataChanged(modelIndex, modelIndex, {ThumbnailRole});
    
    // Emit thumbnailReady signal for external listeners
    emit thumbnailReady(pageIndex);
}

// ============================================================================
// Thumbnail Request Methods
// ============================================================================

void PageThumbnailModel::requestThumbnail(int pageIndex) const
{
    // Don't request if already cached or pending
    if (m_thumbnailCache.contains(pageIndex)) {
        return;
    }
    
    if (m_pendingThumbnails.contains(pageIndex)) {
        return;
    }
    
    // Validate
    if (!m_document || pageIndex < 0 || pageIndex >= m_document->pageCount()) {
        return;
    }
    
    if (m_thumbnailWidth <= 0) {
        return;
    }
    
    // Mark as pending and request from renderer
    m_pendingThumbnails.insert(pageIndex);
    m_renderer->requestThumbnail(m_document, pageIndex, m_thumbnailWidth, m_devicePixelRatio);
}

void PageThumbnailModel::requestVisibleThumbnails(int firstVisible, int lastVisible)
{
    if (!m_document) {
        return;
    }
    
    const int pageCount = m_document->pageCount();
    
    // Clamp to valid range
    firstVisible = qMax(0, firstVisible);
    lastVisible = qMin(lastVisible, pageCount - 1);
    
    // Request thumbnails for visible range plus a small buffer
    const int buffer = 2;  // Pre-fetch 2 pages before/after
    int startIndex = qMax(0, firstVisible - buffer);
    int endIndex = qMin(pageCount - 1, lastVisible + buffer);
    
    for (int i = startIndex; i <= endIndex; ++i) {
        requestThumbnail(i);
    }
}

// ============================================================================
// Private Helpers
// ============================================================================

bool PageThumbnailModel::isPdfPage(int pageIndex) const
{
    if (!m_document || pageIndex < 0 || pageIndex >= m_document->pageCount()) {
        return false;
    }
    
    const Page* p = m_document->page(pageIndex);
    if (!p) {
        return false;
    }
    
    return p->backgroundType == Page::BackgroundType::PDF && p->pdfPageNumber >= 0;
}

bool PageThumbnailModel::canDragPage(int pageIndex) const
{
    if (!m_document) {
        return false;
    }
    
    // In a PDF document, only inserted (non-PDF) pages can be dragged
    if (m_document->hasPdfReference()) {
        return !isPdfPage(pageIndex);
    }
    
    // In a non-PDF document, all pages can be dragged
    return true;
}

