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
    
    for (const RenderTask& task : m_pendingTasks) {
        if (task.pageIndex == pageIndex) {
            return;  // Already queued
        }
    }
    
    // Add to pending queue
    RenderTask task;
    task.doc = doc;
    task.pageIndex = pageIndex;
    task.width = width;
    task.dpr = dpr;
    m_pendingTasks.append(task);
    
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
    
    for (const RenderTask& task : m_pendingTasks) {
        if (task.pageIndex == pageIndex) {
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
        RenderTask task = m_pendingTasks.takeFirst();
        
        // Mark as active
        m_activePages.insert(task.pageIndex);
        
        // Create future watcher
        auto* watcher = new QFutureWatcher<QPair<int, QPixmap>>(this);
        connect(watcher, &QFutureWatcher<QPair<int, QPixmap>>::finished,
                this, &ThumbnailRenderer::onRenderFinished);
        
        m_activeWatchers.append(watcher);
        
        // Start async render
        QFuture<QPair<int, QPixmap>> future = QtConcurrent::run([task]() {
            QPixmap result = renderThumbnailSync(task);
            return qMakePair(task.pageIndex, result);
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

QPixmap ThumbnailRenderer::renderThumbnailSync(const RenderTask& task)
{
    Document* doc = task.doc;
    int pageIndex = task.pageIndex;
    int width = task.width;
    qreal dpr = task.dpr;
    
    // Validate document still has this page
    if (!doc || pageIndex < 0 || pageIndex >= doc->pageCount()) {
        return QPixmap();
    }
    
    // Get page size from metadata (doesn't trigger lazy load)
    QSizeF pageSize = doc->pageSizeAt(pageIndex);
    if (pageSize.isEmpty()) {
        pageSize = QSizeF(612, 792);  // Default US Letter
    }
    
    // Calculate thumbnail dimensions
    qreal aspectRatio = pageSize.height() / pageSize.width();
    int thumbnailWidth = width;
    int thumbnailHeight = static_cast<int>(width * aspectRatio);
    
    // Calculate physical size for high DPI
    int physicalWidth = static_cast<int>(thumbnailWidth * dpr);
    int physicalHeight = static_cast<int>(thumbnailHeight * dpr);
    
    // Create pixmap
    QPixmap thumbnail(physicalWidth, physicalHeight);
    thumbnail.setDevicePixelRatio(dpr);
    thumbnail.fill(Qt::white);  // Default background
    
    QPainter painter(&thumbnail);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    
    // Calculate scale factor: how to fit page into thumbnail
    qreal scaleX = static_cast<qreal>(thumbnailWidth) / pageSize.width();
    qreal scaleY = static_cast<qreal>(thumbnailHeight) / pageSize.height();
    qreal scale = qMin(scaleX, scaleY);
    
    painter.scale(scale, scale);
    
    // Try to get the page (may trigger lazy load)
    Page* page = doc->page(pageIndex);
    if (!page) {
        // Page not available - return white placeholder
        painter.end();
        return thumbnail;
    }
    
    // 1. Render page background
    QPixmap pdfBackground;
    if (doc->isPdfLoaded() && page->pdfPageNumber >= 0) {
        // Calculate DPI for PDF rendering
        // We want the PDF to be rendered at the thumbnail's target resolution
        qreal pdfDpi = (thumbnailWidth * dpr) / (pageSize.width() / 72.0);
        pdfDpi = qMin(pdfDpi, 150.0);  // Cap at 150 DPI for thumbnails
        
        QImage pdfImage = doc->renderPdfPageToImage(page->pdfPageNumber, pdfDpi);
        if (!pdfImage.isNull()) {
            pdfBackground = QPixmap::fromImage(pdfImage);
        }
    }
    
    // Render background (solid color, grid, lines, or PDF)
    page->renderBackground(painter, pdfBackground.isNull() ? nullptr : &pdfBackground, 1.0);
    
    // 2. Render vector layers
    for (int layerIdx = 0; layerIdx < page->layerCount(); ++layerIdx) {
        VectorLayer* layer = page->layer(layerIdx);
        if (layer && layer->visible) {
            layer->render(painter);
        }
    }
    
    // 3. Render inserted objects
    page->renderObjects(painter, 1.0);
    
    painter.end();
    return thumbnail;
}

