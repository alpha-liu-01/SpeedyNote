// ============================================================================
// PageWidget - Container for one page's rendering widgets
// ============================================================================
// Part of the SpeedyNote viewport reconstruction (Phase 3)
// 
// PageWidget is a QWidget that contains:
// - BackgroundWidget (bottom, opaque)
// - LayerWidgets (stacked on top, transparent, one per VectorLayer)
//
// PageWidget manages:
// - Creating/destroying LayerWidgets when layers change
// - Routing current stroke to the active LayerWidget
// - Positioning child widgets to fill the page area
// - Forwarding zoom/PDF updates to children
//
// DocumentViewport creates one PageWidget per visible page.
// ============================================================================

#pragma once

#include "../core/Page.h"
#include "../strokes/VectorStroke.h"
#include "BackgroundWidget.h"
#include "LayerWidget.h"
#include <QWidget>
#include <QVector>

class PageWidget : public QWidget {
    Q_OBJECT
    
public:
    explicit PageWidget(QWidget* parent = nullptr);
    ~PageWidget() override;
    
    // ===== Page Configuration =====
    
    /**
     * @brief Set the page to render.
     * @param page Pointer to the Page (not owned).
     */
    void setPage(Page* page);
    
    /**
     * @brief Get the current page.
     */
    Page* page() const { return m_page; }
    
    /**
     * @brief Set the PDF pixmap for PDF-backed pages.
     * @param pdfPixmap The rendered PDF page pixmap.
     */
    void setPdfPixmap(const QPixmap& pdfPixmap);
    
    /**
     * @brief Set the current zoom level.
     * @param zoom Zoom level (1.0 = 100%).
     */
    void setZoom(qreal zoom);
    
    /**
     * @brief Get the current zoom level.
     */
    qreal zoom() const { return m_zoom; }
    
    // ===== Layer Management =====
    
    /**
     * @brief Synchronize LayerWidgets with Page's VectorLayers.
     * 
     * Creates/destroys LayerWidgets to match the current layer count.
     * Call after layers are added/removed from the Page.
     */
    void syncLayers();
    
    /**
     * @brief Set the active layer index for stroke routing.
     * @param index Index into the Page's layer list.
     */
    void setActiveLayerIndex(int index);
    
    /**
     * @brief Get the current active layer index.
     */
    int activeLayerIndex() const { return m_activeLayerIndex; }
    
    /**
     * @brief Get the LayerWidget for a specific layer.
     * @param index Layer index.
     * @return LayerWidget pointer, or nullptr if out of range.
     */
    LayerWidget* layerWidget(int index) const;
    
    /**
     * @brief Get the active LayerWidget.
     * @return LayerWidget for the active layer, or nullptr if none.
     */
    LayerWidget* activeLayerWidget() const;
    
    // ===== Stroke Drawing =====
    
    /**
     * @brief Begin a new stroke.
     * @param stroke The initial stroke data.
     */
    void beginStroke(const VectorStroke& stroke);
    
    /**
     * @brief Update the current stroke with new data.
     * @param stroke The updated stroke data.
     * 
     * Triggers repaint of the active LayerWidget only.
     */
    void updateStroke(const VectorStroke& stroke);
    
    /**
     * @brief End the current stroke.
     * 
     * Commits the stroke to the active layer and clears current stroke state.
     */
    void endStroke();
    
    /**
     * @brief Cancel the current stroke without committing.
     */
    void cancelStroke();
    
    /**
     * @brief Check if a stroke is currently in progress.
     */
    bool isDrawing() const { return m_isDrawing; }
    
    /**
     * @brief Get the current in-progress stroke.
     */
    const VectorStroke& currentStroke() const { return m_currentStroke; }
    
    // ===== Cache Management =====
    
    /**
     * @brief Invalidate the background cache.
     */
    void invalidateBackgroundCache();
    
    /**
     * @brief Invalidate all caches (background + all layer stroke caches).
     */
    void invalidateAllCaches();
    
signals:
    /**
     * @brief Emitted when a stroke is completed and committed.
     * @param layerIndex The layer the stroke was added to.
     * @param stroke The completed stroke.
     */
    void strokeCompleted(int layerIndex, const VectorStroke& stroke);
    
protected:
    void resizeEvent(QResizeEvent* event) override;
    
private:
    // Data
    Page* m_page = nullptr;
    qreal m_zoom = 1.0;
    
    // Child widgets
    BackgroundWidget* m_backgroundWidget = nullptr;
    QVector<LayerWidget*> m_layerWidgets;
    
    // Active layer
    int m_activeLayerIndex = 0;
    
    // Current stroke
    VectorStroke m_currentStroke;
    bool m_isDrawing = false;
    
    /**
     * @brief Create LayerWidgets for all layers in the page.
     */
    void createLayerWidgets();
    
    /**
     * @brief Destroy all LayerWidgets.
     */
    void destroyLayerWidgets();
    
    /**
     * @brief Update geometry of all child widgets to fill this widget.
     */
    void updateChildGeometry();
    
    /**
     * @brief Update which LayerWidget is marked as active.
     */
    void updateActiveLayer();
    
    /**
     * @brief Rebuild background cache if needed.
     * 
     * Since BackgroundWidget is hidden, we must manually trigger cache rebuilds.
     */
    void rebuildBackgroundCacheIfNeeded();
};

