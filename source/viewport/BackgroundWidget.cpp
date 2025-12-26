// ============================================================================
// BackgroundWidget - Implementation
// ============================================================================
// Part of the SpeedyNote viewport reconstruction (Phase 1)
// 
// CRITICAL: This widget now renders INACTIVE layer strokes into its cache.
// This allows LayerWidget to be OPAQUE and blit this cache, avoiding Qt's
// transparency compositing which was forcing PDF repaints during strokes.
// ============================================================================

#include "BackgroundWidget.h"
#include "../layers/VectorLayer.h"
#include <QPainter>
#include <QPaintEvent>
#include <QDebug>

BackgroundWidget::BackgroundWidget(QWidget* parent)
    : QWidget(parent)
{
    // Opaque widget - no transparency needed
    setAttribute(Qt::WA_OpaquePaintEvent, true);
}

void BackgroundWidget::setPage(Page* page)
{
    if (m_page == page) {
        return;
    }
    
    m_page = page;
    invalidateCache();
    update();
}

void BackgroundWidget::setPdfPixmap(const QPixmap& pdfPixmap)
{
    // Check if pixmap actually changed
    // (comparing pixmaps directly is expensive, so just invalidate)
    m_pdfPixmap = pdfPixmap;
    invalidateCache();
    update();
}

void BackgroundWidget::setZoom(qreal zoom)
{
    if (qFuzzyCompare(m_zoom, zoom)) {
        return;
    }
    
    m_zoom = zoom;
    invalidateCache();
    update();
}

void BackgroundWidget::setActiveLayerIndex(int index)
{
    if (m_activeLayerIndex == index) {
        return;
    }
    
    m_activeLayerIndex = index;
    invalidateCache();
    // Don't call update() here - let PageWidget control when to repaint
}

void BackgroundWidget::invalidateCache()
{
    m_cacheDirty = true;
}

bool BackgroundWidget::isCacheValid() const
{
    if (m_cacheDirty) {
        return false;
    }
    
    qreal currentDpr = devicePixelRatioF();
    return !m_cache.isNull() &&
           qFuzzyCompare(m_cacheZoom, m_zoom) &&
           qFuzzyCompare(m_cacheDpr, currentDpr);
}

void BackgroundWidget::ensureCacheValid(const QSize& targetSize, qreal dpr)
{
    // Check if cache needs rebuild
    bool needsRebuild = m_cacheDirty || 
                        m_cache.isNull() ||
                        !qFuzzyCompare(m_cacheZoom, m_zoom) ||
                        !qFuzzyCompare(m_cacheDpr, dpr);
    
    // Also check size (cache size should match target)
    QSize expectedPhysicalSize(static_cast<int>(targetSize.width() * dpr),
                                static_cast<int>(targetSize.height() * dpr));
    if (m_cache.size() != expectedPhysicalSize) {
        needsRebuild = true;
    }
    
    if (!needsRebuild) {
        return;  // Cache is valid
    }
    
    // Temporarily set our size for rebuildCache to use
    QSize oldSize = size();
    resize(targetSize);
    
    qDebug() << "BackgroundWidget::ensureCacheValid - rebuilding cache at size:" << targetSize;
    rebuildCache();
    
    // Restore size (though it doesn't matter since we're hidden)
    resize(oldSize);
}

void BackgroundWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    
    static int paintCount = 0;
    if (++paintCount % 100 == 1) {  // Log every 100th paint
        qDebug() << "BackgroundWidget::paintEvent #" << paintCount 
                 << "size:" << size() << "visible:" << isVisible();
    }
    
    QPainter painter(this);
    
    if (!m_page) {
        // No page - fill with dark gray
        painter.fillRect(rect(), QColor(64, 64, 64));
        return;
    }
    
    // Ensure cache is valid
    if (!isCacheValid()) {
        qDebug() << "BackgroundWidget: Rebuilding cache...";
        rebuildCache();
    }
    
    // Blit the cached background (single draw call)
    if (!m_cache.isNull()) {
        painter.drawPixmap(0, 0, m_cache);
    }
}

void BackgroundWidget::rebuildCache()
{
    if (!m_page) {
        m_cache = QPixmap();
        m_cacheDirty = false;
        return;
    }
    
    qreal dpr = devicePixelRatioF();
    QSize physicalSize(static_cast<int>(width() * dpr),
                       static_cast<int>(height() * dpr));
    
    if (physicalSize.isEmpty()) {
        m_cache = QPixmap();
        m_cacheDirty = false;
        return;
    }
    
    qDebug() << "BackgroundWidget: Rebuilding cache with activeLayer=" << m_activeLayerIndex;
    
    // Create cache from OPAQUE QImage (Format_RGB32 = no alpha channel)
    // This is critical for performance: blitting an opaque pixmap is 2-3x faster
    // than blitting one with alpha, because no alpha blending is needed.
    QImage cacheImage(physicalSize, QImage::Format_RGB32);
    cacheImage.setDevicePixelRatio(dpr);
    
    QPainter cachePainter(&cacheImage);
    cachePainter.setRenderHint(QPainter::Antialiasing, true);
    
    QSizeF pageSize = m_page->size;
    QRectF pageRect(0, 0, width(), height());
    
    // 1. Fill background color (makes the image fully opaque)
    cachePainter.fillRect(pageRect, m_page->backgroundColor);
    
    // 2. Render background based on type
    switch (m_page->backgroundType) {
        case Page::BackgroundType::None:
            // Just background color (already filled)
            break;
            
        case Page::BackgroundType::PDF:
            if (!m_pdfPixmap.isNull()) {
                cachePainter.drawPixmap(pageRect.toRect(), m_pdfPixmap);
            }
            break;
            
        case Page::BackgroundType::Custom:
            if (!m_page->customBackground.isNull()) {
                cachePainter.drawPixmap(pageRect.toRect(), m_page->customBackground);
            }
            break;
            
        case Page::BackgroundType::Grid:
            {
                cachePainter.setPen(QPen(m_page->gridColor, 1.0));
                qreal spacing = m_page->gridSpacing * m_zoom;
                
                // Vertical lines
                for (qreal x = spacing; x < width(); x += spacing) {
                    cachePainter.drawLine(QPointF(x, 0), QPointF(x, height()));
                }
                
                // Horizontal lines
                for (qreal y = spacing; y < height(); y += spacing) {
                    cachePainter.drawLine(QPointF(0, y), QPointF(width(), y));
                }
            }
            break;
            
        case Page::BackgroundType::Lines:
            {
                cachePainter.setPen(QPen(m_page->gridColor, 1.0));
                qreal spacing = m_page->lineSpacing * m_zoom;
                
                // Horizontal lines only
                for (qreal y = spacing; y < height(); y += spacing) {
                    cachePainter.drawLine(QPointF(0, y), QPointF(width(), y));
                }
            }
            break;
    }
    
    // 3. Draw page border
    cachePainter.setPen(QPen(QColor(180, 180, 180), 1.0));
    cachePainter.drawRect(pageRect.adjusted(0.5, 0.5, -0.5, -0.5));
    
    // 4. Render ALL layer strokes (INCLUDING the active layer's COMMITTED strokes)
    // CRITICAL FIX: By including active layer strokes here, LayerWidget only needs
    // to render the current IN-PROGRESS stroke. This avoids blitting a large 
    // transparent stroke cache (which causes 6ms+ alpha blending overhead).
    int layerCount = m_page->layerCount();
    for (int i = 0; i < layerCount; ++i) {
        VectorLayer* layer = m_page->layer(i);
        if (layer && layer->visible && !layer->strokes().isEmpty()) {
            // Apply zoom transform for stroke rendering
            cachePainter.save();
            cachePainter.scale(m_zoom, m_zoom);
            
            // Render strokes with opacity
            if (layer->opacity < 1.0) {
                cachePainter.setOpacity(layer->opacity);
            }
            layer->render(cachePainter);
            
            cachePainter.restore();
        }
    }
    
    // End painting before converting to pixmap
    cachePainter.end();
    
    // Convert opaque QImage to QPixmap for fast blitting
    m_cache = QPixmap::fromImage(cacheImage);
    m_cache.setDevicePixelRatio(dpr);
    
    m_cacheZoom = m_zoom;
    m_cacheDpr = dpr;
    m_cacheDirty = false;
}

void BackgroundWidget::appendStrokeToCache(const VectorStroke& stroke)
{
    // Can only append if cache is valid
    if (m_cache.isNull() || m_cacheDirty) {
        qDebug() << "BackgroundWidget::appendStrokeToCache - cache invalid, cannot append";
        return;
    }
    
    // Render just this stroke onto the existing cache
    QPainter cachePainter(&m_cache);
    cachePainter.setRenderHint(QPainter::Antialiasing, true);
    
    // Apply zoom transform (strokes are in page coordinates)
    cachePainter.scale(m_zoom, m_zoom);
    
    // Get layer for opacity
    if (m_page) {
        VectorLayer* layer = m_page->layer(m_activeLayerIndex);
        if (layer && layer->opacity < 1.0) {
            cachePainter.setOpacity(layer->opacity);
        }
    }
    
    // Render the single stroke
    VectorLayer::renderStroke(cachePainter, stroke);
    
    qDebug() << "BackgroundWidget::appendStrokeToCache - rendered 1 stroke (O(1))";
}

