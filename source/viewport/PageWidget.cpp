// ============================================================================
// PageWidget - Implementation
// ============================================================================
// Part of the SpeedyNote viewport reconstruction (Phase 3)
// 
// ARCHITECTURE CHANGE:
// - BackgroundWidget now renders: background + PDF + grid + ALL INACTIVE layers
// - Only ONE LayerWidget exists (for active layer) - it's OPAQUE
// - LayerWidget blits the background cache, then renders active layer strokes
// - This completely decouples stroke performance from PDF rendering!
// ============================================================================

#include "PageWidget.h"
#include <QResizeEvent>
#include <QDebug>

PageWidget::PageWidget(QWidget* parent)
    : QWidget(parent)
{
    // PageWidget itself doesn't paint - its children do
    setAttribute(Qt::WA_TranslucentBackground, true);
    
    // Create background widget for CACHE MANAGEMENT ONLY
    // It is NOT shown - LayerWidget blits its cache directly
    // This avoids Qt's optimization of not painting covered widgets
    m_backgroundWidget = new BackgroundWidget(this);
    m_backgroundWidget->hide();  // HIDDEN - only used for cache!
}

PageWidget::~PageWidget()
{
    destroyLayerWidgets();
    // m_backgroundWidget is deleted by Qt parent-child relationship
}

void PageWidget::setPage(Page* page)
{
    if (m_page == page) {
        return;
    }
    
    m_page = page;
    
    // Update background widget (cache manager)
    m_backgroundWidget->setPage(page);
    m_backgroundWidget->setActiveLayerIndex(m_activeLayerIndex);
    
    // Recreate layer widget (just one for active layer)
    destroyLayerWidgets();
    if (page) {
        createLayerWidgets();
    }
    
    updateChildGeometry();
    
    // Force cache rebuild if we have valid size
    rebuildBackgroundCacheIfNeeded();
}

void PageWidget::setPdfPixmap(const QPixmap& pdfPixmap)
{
    m_backgroundWidget->setPdfPixmap(pdfPixmap);
    
    // Force cache rebuild with new PDF
    rebuildBackgroundCacheIfNeeded();
    
    // Update active LayerWidget's background cache reference
    if (!m_layerWidgets.isEmpty()) {
        m_layerWidgets[0]->setBackgroundCache(&m_backgroundWidget->cache());
    }
}

void PageWidget::setZoom(qreal zoom)
{
    if (qFuzzyCompare(m_zoom, zoom)) {
        return;
    }
    
    m_zoom = zoom;
    
    // Update background widget zoom
    m_backgroundWidget->setZoom(zoom);
    
    // Rebuild cache at new zoom
    rebuildBackgroundCacheIfNeeded();
    
    // Update layer widgets
    for (LayerWidget* lw : m_layerWidgets) {
        lw->setZoom(zoom);
        lw->setBackgroundCache(&m_backgroundWidget->cache());
    }
}

void PageWidget::syncLayers()
{
    if (!m_page) {
        destroyLayerWidgets();
        return;
    }
    
    // We only need ONE LayerWidget for the active layer
    // Inactive layers are baked into BackgroundWidget's cache
    
    if (m_layerWidgets.isEmpty()) {
        createLayerWidgets();
    } else {
        // Update the active layer widget to point to current active layer
        VectorLayer* activeLayer = m_page->layer(m_activeLayerIndex);
        if (activeLayer && !m_layerWidgets.isEmpty()) {
            m_layerWidgets[0]->setVectorLayer(activeLayer);
            m_layerWidgets[0]->setBackgroundCache(&m_backgroundWidget->cache());
        }
    }
}

void PageWidget::setActiveLayerIndex(int index)
{
    if (m_activeLayerIndex == index) {
        return;
    }
    
    int oldIndex = m_activeLayerIndex;
    m_activeLayerIndex = index;
    
    qDebug() << "PageWidget::setActiveLayerIndex:" << oldIndex << "->" << index;
    
    // Tell BackgroundWidget which layer to EXCLUDE from its cache
    m_backgroundWidget->setActiveLayerIndex(index);
    
    // Rebuild background cache (now includes old active layer, excludes new active layer)
    m_backgroundWidget->invalidateCache();
    rebuildBackgroundCacheIfNeeded();
    
    // Update LayerWidget to point to the new active layer
    if (m_page && !m_layerWidgets.isEmpty()) {
        VectorLayer* activeLayer = m_page->layer(index);
        if (activeLayer) {
            m_layerWidgets[0]->setVectorLayer(activeLayer);
            m_layerWidgets[0]->setActive(true);
            m_layerWidgets[0]->update();
        }
    }
}

LayerWidget* PageWidget::layerWidget(int index) const
{
    // We only have ONE LayerWidget for the active layer
    if (index == m_activeLayerIndex && !m_layerWidgets.isEmpty()) {
        return m_layerWidgets[0];
    }
    return nullptr;
}

LayerWidget* PageWidget::activeLayerWidget() const
{
    return m_layerWidgets.isEmpty() ? nullptr : m_layerWidgets[0];
}

void PageWidget::beginStroke(const VectorStroke& stroke)
{
    m_currentStroke = stroke;
    m_isDrawing = true;
    
    qDebug() << "PageWidget::beginStroke - activeLayerIndex:" << m_activeLayerIndex;
    
    // Set current stroke on active layer widget
    LayerWidget* activeLW = activeLayerWidget();
    if (activeLW) {
        activeLW->setCurrentStroke(&m_currentStroke);
        activeLW->update();
        qDebug() << "PageWidget::beginStroke - LayerWidget updated";
    } else {
        qDebug() << "PageWidget::beginStroke - NO active LayerWidget!";
    }
}

void PageWidget::updateStroke(const VectorStroke& stroke)
{
    if (!m_isDrawing) {
        return;
    }
    
    m_currentStroke = stroke;
    
    static int updateCount = 0;
    if (++updateCount % 50 == 1) {  // Log every 50th update
        qDebug() << "PageWidget::updateStroke #" << updateCount 
                 << "points:" << m_currentStroke.points.size();
    }
    
    // Only update the active layer widget
    LayerWidget* activeLW = activeLayerWidget();
    if (activeLW) {
        // Stroke pointer is already set, just trigger repaint
        activeLW->update();
    }
}

void PageWidget::endStroke()
{
    if (!m_isDrawing) {
        return;
    }
    
    qDebug() << "PageWidget::endStroke - points:" << m_currentStroke.points.size();
    
    // Commit stroke to the active layer
    if (m_page && m_currentStroke.points.size() >= 2) {
        VectorLayer* layer = m_page->layer(m_activeLayerIndex);
        if (layer) {
            layer->addStroke(m_currentStroke);
            emit strokeCompleted(m_activeLayerIndex, m_currentStroke);
            qDebug() << "PageWidget::endStroke - Stroke committed to layer" << m_activeLayerIndex;
            
            // CRITICAL FIX: Incrementally append the new stroke to background cache!
            // This is O(1) - only renders the new stroke onto existing cache.
            // Much faster than full rebuild (O(n)) or transparent stroke cache blit (6ms+).
            m_backgroundWidget->appendStrokeToCache(m_currentStroke);
            
            // Update LayerWidget's cache reference (in case cache was reallocated)
            if (!m_layerWidgets.isEmpty()) {
                m_layerWidgets[0]->setBackgroundCache(&m_backgroundWidget->cache());
            }
        }
    }
    
    // Clear current stroke state
    LayerWidget* activeLW = activeLayerWidget();
    if (activeLW) {
        activeLW->setCurrentStroke(nullptr);
        // NOTE: Do NOT invalidate stroke cache here!
        // VectorLayer::addStroke() does incremental cache update (O(1))
        // Invalidating would cause full rebuild (O(n)) - defeating the optimization
        activeLW->update();  // Just trigger repaint to show committed stroke
    }
    
    m_currentStroke = VectorStroke();
    m_isDrawing = false;
}

void PageWidget::cancelStroke()
{
    if (!m_isDrawing) {
        return;
    }
    
    // Clear without committing
    LayerWidget* activeLW = activeLayerWidget();
    if (activeLW) {
        activeLW->setCurrentStroke(nullptr);
        activeLW->update();
    }
    
    m_currentStroke = VectorStroke();
    m_isDrawing = false;
}

void PageWidget::invalidateBackgroundCache()
{
    m_backgroundWidget->invalidateCache();
    
    // Rebuild immediately (since BackgroundWidget is hidden)
    rebuildBackgroundCacheIfNeeded();
}

void PageWidget::invalidateAllCaches()
{
    invalidateBackgroundCache();
    
    for (LayerWidget* lw : m_layerWidgets) {
        lw->invalidateStrokeCache();
    }
}

void PageWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateChildGeometry();
    
    // Rebuild cache at new size
    rebuildBackgroundCacheIfNeeded();
}

void PageWidget::createLayerWidgets()
{
    if (!m_page) {
        return;
    }
    
    // Create just ONE LayerWidget for the active layer
    // Inactive layers are rendered into BackgroundWidget's cache
    
    VectorLayer* activeLayer = m_page->layer(m_activeLayerIndex);
    if (activeLayer) {
        LayerWidget* lw = new LayerWidget(this);
        lw->setVectorLayer(activeLayer);
        lw->setPageSize(m_page->size);
        lw->setZoom(m_zoom);
        lw->setActive(true);
        lw->setBackgroundCache(&m_backgroundWidget->cache());
        lw->show();
        lw->raise();  // Above background
        
        m_layerWidgets.append(lw);
    }
    
    updateChildGeometry();
}

void PageWidget::destroyLayerWidgets()
{
    for (LayerWidget* lw : m_layerWidgets) {
        delete lw;
    }
    m_layerWidgets.clear();
}

void PageWidget::updateChildGeometry()
{
    QRect fullRect(0, 0, width(), height());
    
    // All children fill the entire widget
    m_backgroundWidget->setGeometry(fullRect);
    
    for (LayerWidget* lw : m_layerWidgets) {
        lw->setGeometry(fullRect);
    }
}

void PageWidget::updateActiveLayer()
{
    // This is now handled by setActiveLayerIndex()
    // Kept for API compatibility
    if (!m_layerWidgets.isEmpty()) {
        m_layerWidgets[0]->setActive(true);
        
        // If drawing, update with current stroke
        if (m_isDrawing) {
            m_layerWidgets[0]->setCurrentStroke(&m_currentStroke);
            m_layerWidgets[0]->update();
        }
    }
}

void PageWidget::rebuildBackgroundCacheIfNeeded()
{
    if (!m_page) {
        return;
    }
    
    QSize targetSize = size();
    if (targetSize.isEmpty()) {
        return;  // No valid size yet
    }
    
    // Get DPR from this widget (or LayerWidget if available)
    qreal dpr = devicePixelRatioF();
    
    // Ensure cache is valid at current size
    m_backgroundWidget->ensureCacheValid(targetSize, dpr);
    
    // Update LayerWidget's cache reference
    if (!m_layerWidgets.isEmpty()) {
        m_layerWidgets[0]->setBackgroundCache(&m_backgroundWidget->cache());
        m_layerWidgets[0]->update();
    }
}
