#include "ThumbnailRenderer.h"
#include "../core/Document.h"
#include "../core/Page.h"
#include "../layers/VectorLayer.h"

#include <QPainter>
#include <QtConcurrent>
#include <QDebug>

ThumbnailRenderer::ThumbnailRenderer(QObject* parent)
    : QObject(parent)
{
}

ThumbnailRenderer::~ThumbnailRenderer()
{
    m_shuttingDown = true;
    cancelAll();
}

void ThumbnailRenderer::requestThumbnail(Document* doc, int pageIndex, int width, qreal dpr)
{
    if (!doc || pageIndex < 0 || pageIndex >= doc->pageCount() || width <= 0) {
        return;
    }
    
    QMutexLocker locker(&m_mutex);
    
    // Check if already pending or active
    if (m_activePages.contains(pageIndex)) {
        return;  // Already rendering
    }
    
    for (const ThumbnailSnapshot& pending : m_pendingTasks) {
        if (pending.pageIndex == pageIndex) {
            return;  // Already queued
        }
    }
    
    locker.unlock();
    
    // Create snapshot on main thread (thread-safe copy of page data)
    // This MUST happen before we start the async task
    ThumbnailSnapshot snapshot = createSnapshot(doc, pageIndex, width, dpr);
    if (!snapshot.valid) {
        return;  // Failed to create snapshot (page unavailable)
    }
    
    locker.relock();
    
    // Double-check after snapshot creation (in case another request came in)
    if (m_activePages.contains(pageIndex)) {
        return;
    }
    
    // Add to pending queue
    m_pendingTasks.append(std::move(snapshot));
    
    locker.unlock();
    
    // Try to start rendering
    startNextTask();
}

void ThumbnailRenderer::cancelAll()
{
    QMutexLocker locker(&m_mutex);
    
    // Clear pending tasks
    m_pendingTasks.clear();
    
    // Cancel active watchers
    for (QFutureWatcher<QPair<int, QPixmap>>* watcher : m_activeWatchers) {
        watcher->cancel();
        watcher->waitForFinished();
        delete watcher;
    }
    m_activeWatchers.clear();
    m_activePages.clear();
}

bool ThumbnailRenderer::isPending(int pageIndex) const
{
    QMutexLocker locker(&m_mutex);
    
    if (m_activePages.contains(pageIndex)) {
        return true;
    }
    
    for (const ThumbnailSnapshot& pending : m_pendingTasks) {
        if (pending.pageIndex == pageIndex) {
            return true;
        }
    }
    
    return false;
}

void ThumbnailRenderer::setMaxConcurrentRenders(int max)
{
    QMutexLocker locker(&m_mutex);
    m_maxConcurrent = qMax(1, max);
}

void ThumbnailRenderer::startNextTask()
{
    QMutexLocker locker(&m_mutex);
    
    // Check if we can start more renders
    while (m_activeWatchers.size() < m_maxConcurrent && !m_pendingTasks.isEmpty()) {
        ThumbnailSnapshot snapshot = std::move(m_pendingTasks.takeFirst());
        
        // Mark as active
        int pageIndex = snapshot.pageIndex;
        m_activePages.insert(pageIndex);
        
        // Create future watcher
        auto* watcher = new QFutureWatcher<QPair<int, QPixmap>>(this);
        connect(watcher, &QFutureWatcher<QPair<int, QPixmap>>::finished,
                this, &ThumbnailRenderer::onRenderFinished);
        
        m_activeWatchers.append(watcher);
        
        // Move snapshot into lambda - background thread owns it now
        // No live Document/Page access from background thread!
        QFuture<QPair<int, QPixmap>> future = QtConcurrent::run([snapshot = std::move(snapshot)]() {
            QPixmap result = renderFromSnapshot(snapshot);
            return qMakePair(snapshot.pageIndex, result);
        });
        
        watcher->setFuture(future);
    }
}

void ThumbnailRenderer::onRenderFinished()
{
    if (m_shuttingDown) {
        return;
    }
    
    // QFutureWatcher is a template without Q_OBJECT, so use static_cast
    auto* watcher = static_cast<QFutureWatcher<QPair<int, QPixmap>>*>(sender());
    if (!watcher) {
        return;
    }
    
    // Get result before we lock the mutex
    QPair<int, QPixmap> result;
    bool wasCancelled = watcher->isCanceled();
    if (!wasCancelled) {
        result = watcher->result();
    }
    
    {
        QMutexLocker locker(&m_mutex);
        
        // Remove from active
        m_activeWatchers.removeOne(watcher);
        if (!wasCancelled) {
            m_activePages.remove(result.first);
        }
    }
    
    // Clean up watcher
    watcher->deleteLater();
    
    // Emit result if not cancelled
    if (!wasCancelled && !result.second.isNull()) {
        emit thumbnailReady(result.first, result.second);
    }
    
    // Try to start next task
    startNextTask();
}

ThumbnailRenderer::ThumbnailSnapshot ThumbnailRenderer::createSnapshot(
    Document* doc, int pageIndex, int width, qreal dpr)
{
    ThumbnailSnapshot snapshot;
    snapshot.pageIndex = pageIndex;
    snapshot.width = width;
    snapshot.dpr = dpr;
    
    // Get page size from metadata (doesn't trigger lazy load)
    QSizeF pageSize = doc->pageSizeAt(pageIndex);
    if (pageSize.isEmpty()) {
        pageSize = QSizeF(612, 792);  // Default US Letter
    }
    snapshot.pageSize = pageSize;
    
    // Try to get the page (may trigger lazy load)
    // This is safe because we're on the main thread
    Page* page = doc->page(pageIndex);
    if (!page) {
        // Page not available - return invalid snapshot
        return snapshot;
    }
    
    // Copy background settings
    snapshot.backgroundType = page->backgroundType;
    snapshot.backgroundColor = page->backgroundColor;
    snapshot.gridColor = page->gridColor;
    snapshot.gridSpacing = page->gridSpacing;
    snapshot.lineSpacing = page->lineSpacing;
    
    // Calculate thumbnail dimensions for PDF rendering
    qreal aspectRatio = pageSize.height() / pageSize.width();
    int thumbnailWidth = width;
    int thumbnailHeight = static_cast<int>(width * aspectRatio);
    
    // Pre-render PDF background on main thread if needed
    if (doc->isPdfLoaded() && page->pdfPageNumber >= 0) {
        qreal pdfDpi = (thumbnailWidth * dpr) / (pageSize.width() / 72.0);
        pdfDpi = qMin(pdfDpi, 150.0);  // Cap at 150 DPI for thumbnails
        
        QImage pdfImage = doc->renderPdfPageToImage(page->pdfPageNumber, pdfDpi);
        if (!pdfImage.isNull()) {
            snapshot.pdfBackground = QPixmap::fromImage(pdfImage);
        }
    }
    
    // Deep copy stroke data from all layers
    for (int layerIdx = 0; layerIdx < page->layerCount(); ++layerIdx) {
        VectorLayer* layer = page->layer(layerIdx);
        if (layer) {
            LayerSnapshot layerSnap;
            layerSnap.visible = layer->visible;
            layerSnap.opacity = layer->opacity;
            // Deep copy strokes - Qt's implicit sharing is thread-safe for reads
            layerSnap.strokes = layer->strokes();
            snapshot.layers.append(std::move(layerSnap));
        }
    }
    
    // Pre-render objects to a pixmap on main thread
    // Objects may contain QPixmap data that isn't safe to share across threads
    if (page->objectCount() > 0 && pageSize.width() > 0 && pageSize.height() > 0) {
        // Calculate physical size for the objects layer
        int physicalWidth = static_cast<int>(thumbnailWidth * dpr);
        int physicalHeight = static_cast<int>(thumbnailHeight * dpr);
        
        if (physicalWidth > 0 && physicalHeight > 0) {
            snapshot.objectsLayer = QPixmap(physicalWidth, physicalHeight);
            
            if (!snapshot.objectsLayer.isNull()) {
                snapshot.hasObjects = true;
                snapshot.objectsLayer.setDevicePixelRatio(dpr);
                snapshot.objectsLayer.fill(Qt::transparent);
                
                QPainter objPainter(&snapshot.objectsLayer);
                if (objPainter.isActive()) {
                    objPainter.setRenderHint(QPainter::Antialiasing, true);
                    objPainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
                    
                    // Scale to fit page into thumbnail
                    qreal scaleX = static_cast<qreal>(thumbnailWidth) / pageSize.width();
                    qreal scaleY = static_cast<qreal>(thumbnailHeight) / pageSize.height();
                    qreal scale = qMin(scaleX, scaleY);
                    objPainter.scale(scale, scale);
                    
                    // Render all objects
                    page->renderObjects(objPainter, 1.0);
                    objPainter.end();
                }
            }
        }
    }
    
    snapshot.valid = true;
    return snapshot;
}

QPixmap ThumbnailRenderer::renderFromSnapshot(const ThumbnailSnapshot& snapshot)
{
    if (!snapshot.valid) {
        return QPixmap();
    }
    
    int width = snapshot.width;
    qreal dpr = snapshot.dpr;
    QSizeF pageSize = snapshot.pageSize;
    
    // Safety check: prevent division by zero
    if (pageSize.width() <= 0 || pageSize.height() <= 0) {
        return QPixmap();
    }
    
    // Calculate thumbnail dimensions
    qreal aspectRatio = pageSize.height() / pageSize.width();
    int thumbnailWidth = width;
    int thumbnailHeight = static_cast<int>(width * aspectRatio);
    
    // Calculate physical size for high DPI
    int physicalWidth = static_cast<int>(thumbnailWidth * dpr);
    int physicalHeight = static_cast<int>(thumbnailHeight * dpr);
    
    // Safety check: ensure valid dimensions
    if (physicalWidth <= 0 || physicalHeight <= 0) {
        return QPixmap();
    }
    
    // Create pixmap
    QPixmap thumbnail(physicalWidth, physicalHeight);
    if (thumbnail.isNull()) {
        // Pixmap creation failed (e.g., out of memory)
        return QPixmap();
    }
    thumbnail.setDevicePixelRatio(dpr);
    thumbnail.fill(Qt::white);  // Default background
    
    QPainter painter(&thumbnail);
    if (!painter.isActive()) {
        // Painter failed to initialize
        return QPixmap();
    }
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    
    // Calculate scale factor
    qreal scaleX = static_cast<qreal>(thumbnailWidth) / pageSize.width();
    qreal scaleY = static_cast<qreal>(thumbnailHeight) / pageSize.height();
    qreal scale = qMin(scaleX, scaleY);
    
    painter.scale(scale, scale);
    
    // 1. Render background
    QRectF pageRect(0, 0, pageSize.width(), pageSize.height());
    
    if (!snapshot.pdfBackground.isNull()) {
        // PDF background - draw scaled to fit
        painter.drawPixmap(pageRect.toRect(), snapshot.pdfBackground);
    } else {
        // Use Page's static helper for background pattern
        Page::renderBackgroundPattern(
            painter,
            pageRect,
            snapshot.backgroundColor,
            snapshot.backgroundType,
            snapshot.gridColor,
            snapshot.gridSpacing,
            snapshot.lineSpacing
        );
    }
    
    // 2. Render vector layers from snapshot (thread-safe - all data is local)
    for (const LayerSnapshot& layerSnap : snapshot.layers) {
        if (!layerSnap.visible) {
            continue;
        }
        
        painter.save();
        if (layerSnap.opacity < 1.0) {
            painter.setOpacity(layerSnap.opacity);
        }
        
        // Render each stroke using VectorLayer's static method
        for (const VectorStroke& stroke : layerSnap.strokes) {
            VectorLayer::renderStroke(painter, stroke);
        }
        
        painter.restore();
    }
    
    // 3. Render pre-rendered objects layer
    if (snapshot.hasObjects && !snapshot.objectsLayer.isNull()) {
        // Reset transform to draw the pre-rendered pixmap at 1:1
        painter.resetTransform();
        painter.drawPixmap(0, 0, snapshot.objectsLayer);
    }
    
    painter.end();
    return thumbnail;
}

