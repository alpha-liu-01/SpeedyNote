// ============================================================================
// BackgroundWidget - Renders page background + INACTIVE layer strokes
// ============================================================================
// Part of the SpeedyNote viewport reconstruction (Phase 1)
// 
// CRITICAL INSIGHT: Qt's transparency compositing means that when a 
// transparent LayerWidget updates, Qt MUST repaint the opaque parent first.
// This caused PDF backgrounds to repaint during every stroke.
//
// SOLUTION: BackgroundWidget now renders:
// - Background color fill
// - PDF page content (if PDF-backed page)
// - Grid lines (if grid background)
// - Ruled lines (if lined background)
// - Page border
// - **ALL INACTIVE layer strokes** (baked into cache)
//
// The active layer is rendered separately by LayerWidget.
// LayerWidget is made OPAQUE and blits this cache, then draws active strokes.
// This completely decouples active layer performance from PDF rendering.
//
// Cache is rebuilt when:
// - Zoom level changes
// - Device pixel ratio changes
// - Background settings change
// - PDF pixmap is updated
// - Active layer changes (inactive strokes need re-baking)
// - A stroke is committed (inactive layer changed)
// ============================================================================

#pragma once

#include "../core/Page.h"
#include "../strokes/VectorStroke.h"
#include <QWidget>
#include <QPixmap>

class BackgroundWidget : public QWidget {
    Q_OBJECT
    
public:
    explicit BackgroundWidget(QWidget* parent = nullptr);
    ~BackgroundWidget() override = default;
    
    // ===== Configuration =====
    
    /**
     * @brief Set the page to render background for.
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
     * 
     * Called by PageWidget when PDF cache is updated.
     * Invalidates cache if pixmap differs.
     */
    void setPdfPixmap(const QPixmap& pdfPixmap);
    
    /**
     * @brief Set the current zoom level.
     * @param zoom Zoom level (1.0 = 100%).
     * 
     * Invalidates cache if zoom changes.
     */
    void setZoom(qreal zoom);
    
    /**
     * @brief Get the current zoom level.
     */
    qreal zoom() const { return m_zoom; }
    
    /**
     * @brief Set the index of the active layer (to EXCLUDE from background cache).
     * @param index The active layer index (-1 means include all layers).
     * 
     * Inactive layers are baked into the background cache.
     * The active layer is rendered separately by LayerWidget.
     */
    void setActiveLayerIndex(int index);
    
    /**
     * @brief Get the active layer index.
     */
    int activeLayerIndex() const { return m_activeLayerIndex; }
    
    // ===== Cache Management =====
    
    /**
     * @brief Force cache invalidation.
     * 
     * Call when background settings change or inactive layer strokes change.
     */
    void invalidateCache();
    
    /**
     * @brief Check if cache is currently valid.
     */
    bool isCacheValid() const;
    
    /**
     * @brief Get the cached composite image.
     * 
     * Used by LayerWidget to blit background before rendering active strokes.
     */
    const QPixmap& cache() const { return m_cache; }
    
    /**
     * @brief Force rebuild the cache immediately.
     * @param targetSize The size to render the cache at (widget size).
     * @param dpr Device pixel ratio.
     * 
     * Call this manually since BackgroundWidget is hidden and paintEvent won't run.
     */
    void ensureCacheValid(const QSize& targetSize, qreal dpr);
    
    /**
     * @brief Append a single stroke to the existing cache (O(1) operation).
     * @param stroke The stroke to render onto the cache.
     * 
     * CRITICAL OPTIMIZATION: Instead of rebuilding the entire cache when a stroke
     * is committed, this renders just the new stroke onto the existing cache.
     * This changes stroke commit from O(n) to O(1).
     */
    void appendStrokeToCache(const VectorStroke& stroke);

protected:
    void paintEvent(QPaintEvent* event) override;
    
private:
    // Data
    Page* m_page = nullptr;
    QPixmap m_pdfPixmap;
    qreal m_zoom = 1.0;
    int m_activeLayerIndex = 0;  // Layer to EXCLUDE from background cache
    
    // Cache
    QPixmap m_cache;
    qreal m_cacheZoom = 0;
    qreal m_cacheDpr = 0;
    bool m_cacheDirty = true;
    
    /**
     * @brief Rebuild the background cache.
     * 
     * Renders background color + PDF/grid/lines + border + INACTIVE layer strokes.
     */
    void rebuildCache();
};

