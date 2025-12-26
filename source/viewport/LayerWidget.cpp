// ============================================================================
// LayerWidget - Implementation
// ============================================================================
// Part of the SpeedyNote viewport reconstruction (Phase 2)
// 
// CRITICAL: This widget is now OPAQUE and blits the background cache.
// This breaks the Qt transparency compositing chain and prevents
// BackgroundWidget from repainting during strokes.
// ============================================================================

#include "LayerWidget.h"
#include <QPainter>
#include <QPaintEvent>
#include <QDebug>
#include <QElapsedTimer>

LayerWidget::LayerWidget(QWidget* parent)
    : QWidget(parent)
{
    // OPAQUE widget - breaks Qt transparency compositing chain!
    // This is the key to preventing BackgroundWidget from repainting during strokes.
    setAttribute(Qt::WA_OpaquePaintEvent, true);
}

void LayerWidget::setVectorLayer(VectorLayer* layer)
{
    if (m_layer == layer) {
        return;
    }
    
    m_layer = layer;
    update();
}

void LayerWidget::setPageSize(const QSizeF& size)
{
    if (m_pageSize == size) {
        return;
    }
    
    m_pageSize = size;
    // NOTE: VectorLayer stroke cache is no longer used - BackgroundWidget manages caching
    update();
}

void LayerWidget::setZoom(qreal zoom)
{
    if (qFuzzyCompare(m_zoom, zoom)) {
        return;
    }
    
    m_zoom = zoom;
    // NOTE: VectorLayer stroke cache is no longer used - BackgroundWidget manages caching
    update();
}

void LayerWidget::setActive(bool active)
{
    if (m_isActive == active) {
        return;
    }
    
    m_isActive = active;
    
    // If deactivating, clear current stroke
    if (!active) {
        m_currentStroke = nullptr;
    }
    
    update();
}

void LayerWidget::setCurrentStroke(const VectorStroke* stroke)
{
    m_currentStroke = stroke;
    // Don't update here - caller should call update() when ready
}

void LayerWidget::setBackgroundCache(const QPixmap* cache)
{
    m_backgroundCache = cache;
    // Don't update - this is called as part of setup
}

void LayerWidget::invalidateStrokeCache()
{
    if (m_layer) {
        m_layer->invalidateStrokeCache();
    }
    update();
}

void LayerWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    
    static int paintCount = 0;
    static qint64 totalBgBlitTime = 0;
    static qint64 totalStrokeCacheTime = 0;
    static qint64 totalCurrentStrokeTime = 0;
    static int timingCount = 0;
    
    QElapsedTimer timer;
    
    ++paintCount;
    
    QPainter painter(this);
    
    // STEP 1: Blit the background cache (contains PDF + ALL committed strokes)
    // CRITICAL: Background cache now includes active layer strokes!
    // This avoids blitting a separate transparent stroke cache (6ms+ overhead).
    timer.start();
    if (m_backgroundCache && !m_backgroundCache->isNull()) {
        painter.drawPixmap(0, 0, *m_backgroundCache);
    } else {
        // Fallback: fill with white if no cache available
        painter.fillRect(rect(), Qt::white);
    }
    qint64 bgBlitTime = timer.nsecsElapsed();
    
    // Early exit if no layer or layer is hidden
    if (!m_layer || !m_layer->visible) {
        return;
    }
    
    painter.setRenderHint(QPainter::Antialiasing, true);
    
    // STEP 2: SKIP stroke cache blit - committed strokes are in background cache!
    // This eliminates the 6ms+ transparent pixmap alpha-blend overhead.
    qint64 strokeCacheTime = 0;  // No longer used
    
    // STEP 3: If this is the active layer and we have a current stroke, render it
    // This is the ONLY thing we render on top of the background cache.
    qint64 currentStrokeTime = 0;
    if (m_isActive && m_currentStroke && !m_currentStroke->points.isEmpty()) {
        timer.restart();
        // The current stroke is in page coordinates
        // We need to scale it by zoom to render at correct size
        painter.save();
        painter.scale(m_zoom, m_zoom);
        
        // Use VectorLayer's static stroke rendering method
        VectorLayer::renderStroke(painter, *m_currentStroke);
        
        painter.restore();
        currentStrokeTime = timer.nsecsElapsed();
    }
    
    // Accumulate timing data
    totalBgBlitTime += bgBlitTime;
    totalStrokeCacheTime += strokeCacheTime;
    totalCurrentStrokeTime += currentStrokeTime;
    ++timingCount;
    
    // Log every 100th paint with timing averages
    if (paintCount % 100 == 1) {
        qDebug() << "LayerWidget::paintEvent #" << paintCount 
                 << "layer:" << (m_layer ? m_layer->name : "null")
                 << "active:" << m_isActive
                 << "hasBgCache:" << (m_backgroundCache != nullptr)
                 << "bgCacheSize:" << (m_backgroundCache ? m_backgroundCache->size() : QSize());
        
        if (timingCount > 0) {
            qDebug() << "  TIMING AVG (us): bgBlit=" << (totalBgBlitTime / timingCount / 1000)
                     << "strokeCache=" << (totalStrokeCacheTime / timingCount / 1000)
                     << "currentStroke=" << (totalCurrentStrokeTime / timingCount / 1000)
                     << "TOTAL=" << ((totalBgBlitTime + totalStrokeCacheTime + totalCurrentStrokeTime) / timingCount / 1000);
        }
        
        // Reset accumulators after reporting
        totalBgBlitTime = 0;
        totalStrokeCacheTime = 0;
        totalCurrentStrokeTime = 0;
        timingCount = 0;
    }
}

