#include "ThumbnailRenderer.h"
#include "../core/DarkModeUtils.h"
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
        return;
    }
    
    for (const ThumbnailRequest& req : m_pendingRequests) {
        if (req.pageIndex == pageIndex) {
            return;
        }
    }
    
    // Drop oldest requests if queue is full
    while (m_pendingRequests.size() >= MAX_PENDING_REQUESTS) {
        m_pendingRequests.removeFirst();
    }
    
    m_pendingRequests.append({doc, pageIndex, width, dpr, m_pdfDarkMode});
    
    locker.unlock();
    
    startNextTask();
}

void ThumbnailRenderer::setPdfDarkMode(bool enabled)
{
    m_pdfDarkMode = enabled;
}

void ThumbnailRenderer::cancelAll()
{
    QMutexLocker locker(&m_mutex);
    
    // Clear lightweight pending requests (trivially cheap)
    m_pendingRequests.clear();
    
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
    
    for (const ThumbnailRequest& req : m_pendingRequests) {
        if (req.pageIndex == pageIndex) {
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
    
    while (m_activeWatchers.size() < m_maxConcurrent && !m_pendingRequests.isEmpty()) {
        ThumbnailRequest req = m_pendingRequests.takeFirst();
        
        // Unlock while creating the heavy snapshot on the main thread
        locker.unlock();
        
        bool wasLoaded = req.doc->isPageLoaded(req.pageIndex);
        
        ThumbnailSnapshot snapshot = createSnapshot(
            req.doc, req.pageIndex, req.width, req.dpr,
            req.pdfDarkMode);
        
        // Evict pages loaded only for thumbnail rendering to prevent
        // m_loadedPages from growing unboundedly during fast panel scrolling
        if (!wasLoaded && req.doc->isLazyLoadEnabled()
            && req.doc->isPageLoaded(req.pageIndex)) {
            req.doc->evictPage(req.pageIndex);
        }
        
        locker.relock();
        
        if (!snapshot.valid) {
            continue;
        }
        
        int pageIndex = snapshot.pageIndex;
        m_activePages.insert(pageIndex);
        
        auto* watcher = new QFutureWatcher<QPair<int, QPixmap>>(this);
        connect(watcher, &QFutureWatcher<QPair<int, QPixmap>>::finished,
                this, &ThumbnailRenderer::onRenderFinished);
        
        m_activeWatchers.append(watcher);
        
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
    
    // Delete watcher immediately to free the QFuture's result QPixmap.
    // Safe per Qt docs: "always safe to delete a QObject from within a slot
    // connected to that object's own signal." The watcher has already been
    // removed from m_activeWatchers and the result extracted above.
    delete watcher;
    
    // Emit result if not cancelled
    if (!wasCancelled && !result.second.isNull()) {
        emit thumbnailReady(result.first, result.second);
    }
    
    // Try to start next task
    startNextTask();
}

ThumbnailRenderer::ThumbnailSnapshot ThumbnailRenderer::createSnapshot(
    Document* doc, int pageIndex, int width, qreal dpr, bool pdfDarkMode)
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
        return snapshot;
    }
    
    // Copy background settings
    snapshot.backgroundType = page->backgroundType;
    snapshot.backgroundColor = page->backgroundColor;
    snapshot.gridColor = page->gridColor;
    snapshot.gridSpacing = page->gridSpacing;
    snapshot.lineSpacing = page->lineSpacing;
    
    // Calculate thumbnail dimensions
    qreal aspectRatio = pageSize.height() / pageSize.width();
    int thumbnailWidth = width;
    int thumbnailHeight = static_cast<int>(width * aspectRatio);
    
    // Store PDF info for deferred rendering in the worker thread.
    // MuPdfProvider::renderPageToImage() is already mutex-protected,
    // so calling it from a worker is safe and keeps the main thread responsive.
    if (doc->isPdfLoaded() && page->pdfPageNumber >= 0) {
        snapshot.doc = doc;
        snapshot.pdfPageNumber = page->pdfPageNumber;
        snapshot.pdfDarkMode = pdfDarkMode;
        qreal pdfDpi = (thumbnailWidth * dpr) / (pageSize.width() / 72.0);
        snapshot.pdfDpi = qMin(pdfDpi, 96.0);
    }
    
    // Deep copy stroke data from all layers
    for (int layerIdx = 0; layerIdx < page->layerCount(); ++layerIdx) {
        VectorLayer* layer = page->layer(layerIdx);
        if (layer) {
            LayerSnapshot layerSnap;
            layerSnap.visible = layer->visible;
            layerSnap.opacity = layer->opacity;
            layerSnap.strokes = layer->strokes();
            snapshot.layers.append(std::move(layerSnap));
        }
    }
    
    // Pre-render objects to a pixmap on main thread
    // Objects may contain QPixmap data that isn't safe to copy across threads
    if (page->objectCount() > 0 && pageSize.width() > 0 && pageSize.height() > 0) {
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
                    
                    qreal scaleX = static_cast<qreal>(thumbnailWidth) / pageSize.width();
                    qreal scaleY = static_cast<qreal>(thumbnailHeight) / pageSize.height();
                    qreal scale = qMin(scaleX, scaleY);
                    objPainter.scale(scale, scale);
                    
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
    
    if (pageSize.width() <= 0 || pageSize.height() <= 0) {
        return QPixmap();
    }
    
    // Calculate thumbnail dimensions
    qreal aspectRatio = pageSize.height() / pageSize.width();
    int thumbnailWidth = width;
    int thumbnailHeight = static_cast<int>(width * aspectRatio);
    
    int physicalWidth = static_cast<int>(thumbnailWidth * dpr);
    int physicalHeight = static_cast<int>(thumbnailHeight * dpr);
    
    if (physicalWidth <= 0 || physicalHeight <= 0) {
        return QPixmap();
    }
    
    // Render PDF background in the worker thread (deferred from createSnapshot)
    QPixmap pdfBackground;
    if (snapshot.pdfPageNumber >= 0 && snapshot.doc && snapshot.pdfDpi > 0) {
        QImage pdfImage = snapshot.doc->renderPdfPageToImage(snapshot.pdfPageNumber, snapshot.pdfDpi);
        if (!pdfImage.isNull()) {
            if (snapshot.pdfDarkMode) {
                QVector<QRect> imgRegions = snapshot.doc->pdfImageRegions(
                    snapshot.pdfPageNumber, snapshot.pdfDpi);
                DarkModeUtils::invertImageLightness(pdfImage, imgRegions);
            }
            pdfBackground = QPixmap::fromImage(pdfImage);
        }
        snapshot.doc->trimPdfStore();
    }
    
    QPixmap thumbnail(physicalWidth, physicalHeight);
    if (thumbnail.isNull()) {
        return QPixmap();
    }
    thumbnail.setDevicePixelRatio(dpr);
    thumbnail.fill(Qt::white);
    
    QPainter painter(&thumbnail);
    if (!painter.isActive()) {
        return QPixmap();
    }
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    
    qreal scaleX = static_cast<qreal>(thumbnailWidth) / pageSize.width();
    qreal scaleY = static_cast<qreal>(thumbnailHeight) / pageSize.height();
    qreal scale = qMin(scaleX, scaleY);
    
    painter.scale(scale, scale);
    
    // 1. Render background
    QRectF pageRect(0, 0, pageSize.width(), pageSize.height());
    
    if (!pdfBackground.isNull()) {
        painter.drawPixmap(pageRect.toRect(), pdfBackground);
    } else {
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
        
        for (const VectorStroke& stroke : layerSnap.strokes) {
            VectorLayer::renderStroke(painter, stroke);
        }
        
        painter.restore();
    }
    
    // 3. Render pre-rendered objects layer
    if (snapshot.hasObjects && !snapshot.objectsLayer.isNull()) {
        painter.resetTransform();
        painter.drawPixmap(0, 0, snapshot.objectsLayer);
    }
    
    painter.end();
    return thumbnail;
}

