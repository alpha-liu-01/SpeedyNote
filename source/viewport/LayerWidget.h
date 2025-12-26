// ============================================================================
// LayerWidget - Renders the ACTIVE layer's strokes (OPAQUE widget)
// ============================================================================
// Part of the SpeedyNote viewport reconstruction (Phase 2)
// 
// CRITICAL CHANGE: LayerWidget is now OPAQUE (not transparent).
// 
// The problem with transparent LayerWidget:
// - Qt's transparency compositing forces parent (BackgroundWidget) to repaint
// - This caused PDF backgrounds to re-render during every stroke update
// - Performance was terrible with PDF loaded
//
// The solution (OPAQUE LayerWidget):
// 1. LayerWidget is opaque - Qt doesn't composite with parent
// 2. BackgroundWidget's cache contains: background + PDF + grid + INACTIVE layers
// 3. LayerWidget blits the background cache first, then renders active layer strokes
// 4. During strokes, ONLY LayerWidget repaints - BackgroundWidget stays untouched
// 5. PDF performance is completely decoupled from stroke performance!
//
// Each LayerWidget only updates when:
// - It's the active layer and a stroke is being drawn
// - Zoom level changes
// ============================================================================

#pragma once

#include "../layers/VectorLayer.h"
#include "../strokes/VectorStroke.h"
#include <QWidget>
#include <QPixmap>

class LayerWidget : public QWidget {
    Q_OBJECT
    
public:
    explicit LayerWidget(QWidget* parent = nullptr);
    ~LayerWidget() override = default;
    
    // ===== Configuration =====
    
    /**
     * @brief Set the VectorLayer to render.
     * @param layer Pointer to the VectorLayer (not owned).
     */
    void setVectorLayer(VectorLayer* layer);
    
    /**
     * @brief Get the current VectorLayer.
     */
    VectorLayer* vectorLayer() const { return m_layer; }
    
    /**
     * @brief Set the page size for coordinate reference.
     * @param size Page size in document units.
     */
    void setPageSize(const QSizeF& size);
    
    /**
     * @brief Set the current zoom level.
     * @param zoom Zoom level (1.0 = 100%).
     */
    void setZoom(qreal zoom);
    
    /**
     * @brief Get the current zoom level.
     */
    qreal zoom() const { return m_zoom; }
    
    /**
     * @brief Set the background cache to blit before rendering strokes.
     * @param cache Pointer to BackgroundWidget's cache (not owned).
     * 
     * This is the key to decoupling stroke performance from PDF!
     * LayerWidget blits this pre-rendered cache instead of being transparent.
     */
    void setBackgroundCache(const QPixmap* cache);
    
    // ===== Active Layer Handling =====
    
    /**
     * @brief Set whether this layer is the active (drawing) layer.
     * @param active True if this layer should handle current stroke.
     */
    void setActive(bool active);
    
    /**
     * @brief Check if this layer is currently active.
     */
    bool isActive() const { return m_isActive; }
    
    /**
     * @brief Set the current in-progress stroke to render.
     * @param stroke Pointer to the current stroke, or nullptr when not drawing.
     * 
     * Only used when this layer is active.
     * Call update() after setting to trigger repaint.
     */
    void setCurrentStroke(const VectorStroke* stroke);
    
    /**
     * @brief Get the current stroke being rendered.
     */
    const VectorStroke* currentStroke() const { return m_currentStroke; }
    
    // ===== Cache Management =====
    
    /**
     * @brief Invalidate the VectorLayer's stroke cache.
     * 
     * Call when strokes are added/removed/modified.
     */
    void invalidateStrokeCache();
    
protected:
    void paintEvent(QPaintEvent* event) override;
    
private:
    VectorLayer* m_layer = nullptr;
    QSizeF m_pageSize;
    qreal m_zoom = 1.0;
    
    bool m_isActive = false;
    const VectorStroke* m_currentStroke = nullptr;
    const QPixmap* m_backgroundCache = nullptr;  // From BackgroundWidget
};

