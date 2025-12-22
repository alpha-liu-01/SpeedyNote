#pragma once

// ============================================================================
// DocumentViewport - The main canvas widget for displaying documents
// ============================================================================
// Part of the new SpeedyNote document architecture (Phase 1.3)
//
// DocumentViewport is a QWidget that:
// - Displays pages from a Document with pan/zoom
// - Routes input to the correct page
// - Manages caching for smooth scrolling
// - Communicates with MainWindow via signals
//
// Replaces: InkCanvas (view/input portions)
// ============================================================================

#include "Document.h"
#include "Page.h"

#include <QWidget>
#include <QPointF>
#include <QSizeF>
#include <QRectF>
#include <QVector>

// Forward declarations
class QPaintEvent;
class QResizeEvent;
class QMouseEvent;
class QTabletEvent;
class QWheelEvent;

/**
 * @brief Layout mode for page arrangement.
 */
enum class LayoutMode {
    SingleColumn,   ///< Vertical scroll, 1 page wide (default)
    TwoColumn       ///< Vertical scroll, 2 pages side-by-side
    // Future: Grid, Horizontal, etc.
};

/**
 * @brief Result of viewport-to-page coordinate conversion.
 */
struct PageHit {
    int pageIndex = -1;     ///< Page index, or -1 if no page hit
    QPointF pagePoint;      ///< Point in page-local coordinates
    
    bool valid() const { return pageIndex >= 0; }
};

/**
 * @brief The main canvas widget for displaying and interacting with documents.
 * 
 * DocumentViewport handles:
 * - Rendering pages with backgrounds, PDF content, strokes, and objects
 * - Pan and zoom transforms
 * - Routing input events to the correct page
 * - Managing caches for smooth scrolling
 * 
 * One DocumentViewport instance per tab (each tab has its own view state).
 */
class DocumentViewport : public QWidget {
    Q_OBJECT
    
public:
    // ===== Constructor & Destructor =====
    
    /**
     * @brief Construct a new DocumentViewport.
     * @param parent Parent widget (typically the tab container).
     */
    explicit DocumentViewport(QWidget* parent = nullptr);
    
    /**
     * @brief Destructor.
     */
    ~DocumentViewport() override;
    
    // ===== Document Management =====
    
    /**
     * @brief Set the document to display.
     * @param doc Pointer to the document (not owned, must outlive viewport).
     * 
     * Resets view state (pan, zoom) and triggers repaint.
     * Pass nullptr to clear the document.
     */
    void setDocument(Document* doc);
    
    /**
     * @brief Get the currently displayed document.
     * @return Pointer to the document, or nullptr if none set.
     */
    Document* document() const { return m_document; }
    
    // ===== View State Getters =====
    
    /**
     * @brief Get the current zoom level.
     * @return Zoom level (1.0 = 100%, 2.0 = 200%, etc.)
     */
    qreal zoomLevel() const { return m_zoomLevel; }
    
    /**
     * @brief Get the current pan offset.
     * @return Pan offset in document coordinates.
     */
    QPointF panOffset() const { return m_panOffset; }
    
    /**
     * @brief Get the index of the "current" page (most visible or centered).
     * @return 0-based page index.
     */
    int currentPageIndex() const { return m_currentPageIndex; }
    
    // ===== Layout =====
    
    /**
     * @brief Get the current layout mode.
     */
    LayoutMode layoutMode() const { return m_layoutMode; }
    
    /**
     * @brief Set the layout mode.
     * @param mode New layout mode.
     */
    void setLayoutMode(LayoutMode mode);
    
    /**
     * @brief Get the gap between pages in pixels.
     */
    int pageGap() const { return m_pageGap; }
    
    /**
     * @brief Set the gap between pages.
     * @param gap Gap in pixels.
     */
    void setPageGap(int gap);
    
    // ===== Layout Engine (Task 1.3.2) =====
    
    /**
     * @brief Get the position of a page in document coordinates.
     * @param pageIndex 0-based page index.
     * @return Top-left corner of the page in document coordinates.
     */
    QPointF pagePosition(int pageIndex) const;
    
    /**
     * @brief Get the full rectangle of a page in document coordinates.
     * @param pageIndex 0-based page index.
     * @return Rectangle including position and size.
     */
    QRectF pageRect(int pageIndex) const;
    
    /**
     * @brief Get the total size of all pages (bounding box).
     * @return Size of the content area containing all pages.
     */
    QSizeF totalContentSize() const;
    
    /**
     * @brief Find which page contains a point in document coordinates.
     * @param documentPt Point in document coordinates.
     * @return Page index, or -1 if point is not on any page.
     */
    int pageAtPoint(QPointF documentPt) const;
    
    /**
     * @brief Get the list of pages currently visible in the viewport.
     * @return Vector of page indices that intersect the viewport.
     */
    QVector<int> visiblePages() const;
    
    /**
     * @brief Get the visible rectangle in document coordinates.
     * @return The area of the document currently visible in the viewport.
     */
    QRectF visibleRect() const;
    
    // ===== View State Setters (Slots) =====
    
public slots:
    /**
     * @brief Set the zoom level.
     * @param zoom New zoom level (clamped to valid range).
     */
    void setZoomLevel(qreal zoom);
    
    /**
     * @brief Set the pan offset.
     * @param offset New pan offset in document coordinates.
     */
    void setPanOffset(QPointF offset);
    
    /**
     * @brief Scroll to make a specific page visible.
     * @param pageIndex 0-based page index.
     */
    void scrollToPage(int pageIndex);
    
    /**
     * @brief Scroll by a delta amount.
     * @param delta Scroll delta in document coordinates.
     */
    void scrollBy(QPointF delta);
    
    /**
     * @brief Zoom to fit the entire document in the viewport.
     */
    void zoomToFit();
    
    /**
     * @brief Zoom to fit the page width in the viewport.
     */
    void zoomToWidth();
    
    /**
     * @brief Scroll to the home position (origin).
     */
    void scrollToHome();
    
    /**
     * @brief Set horizontal scroll position as a fraction (0.0 to 1.0).
     * @param fraction Scroll fraction.
     */
    void setHorizontalScrollFraction(qreal fraction);
    
    /**
     * @brief Set vertical scroll position as a fraction (0.0 to 1.0).
     * @param fraction Scroll fraction.
     */
    void setVerticalScrollFraction(qreal fraction);
    
signals:
    // ===== View State Signals =====
    
    /**
     * @brief Emitted when the zoom level changes.
     * @param zoom New zoom level.
     */
    void zoomChanged(qreal zoom);
    
    /**
     * @brief Emitted when the pan offset changes.
     * @param offset New pan offset.
     */
    void panChanged(QPointF offset);
    
    /**
     * @brief Emitted when the current page changes.
     * @param pageIndex New current page index.
     */
    void currentPageChanged(int pageIndex);
    
    /**
     * @brief Emitted when the document is modified.
     */
    void documentModified();
    
    /**
     * @brief Emitted when horizontal scroll position changes.
     * @param fraction Scroll position as fraction (0.0 to 1.0).
     */
    void horizontalScrollChanged(qreal fraction);
    
    /**
     * @brief Emitted when vertical scroll position changes.
     * @param fraction Scroll position as fraction (0.0 to 1.0).
     */
    void verticalScrollChanged(qreal fraction);
    
protected:
    // ===== Qt Event Overrides =====
    
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void tabletEvent(QTabletEvent* event) override;
    
private:
    // ===== Document Reference =====
    Document* m_document = nullptr;
    
    // ===== View State =====
    qreal m_zoomLevel = 1.0;
    QPointF m_panOffset;
    int m_currentPageIndex = 0;
    
    // ===== Layout =====
    LayoutMode m_layoutMode = LayoutMode::SingleColumn;
    int m_pageGap = 20;  // Pixels between pages
    
    // ===== Zoom Limits =====
    static constexpr qreal MIN_ZOOM = 0.1;   // 10%
    static constexpr qreal MAX_ZOOM = 10.0;  // 1000%
    
    // ===== Private Methods =====
    
    /**
     * @brief Clamp pan offset to valid bounds.
     */
    void clampPanOffset();
    
    /**
     * @brief Update the current page index based on pan position.
     */
    void updateCurrentPageIndex();
    
    /**
     * @brief Emit scroll fraction signals.
     */
    void emitScrollFractions();
    
    // ===== Pan & Zoom Helpers (Task 1.3.4) =====
    
    /**
     * @brief Get the viewport center point in document coordinates.
     * @return Center of viewport in document space.
     */
    QPointF viewportCenter() const;
    
    /**
     * @brief Zoom at a specific point, keeping that point stationary.
     * @param newZoom The new zoom level.
     * @param viewportPt The point in viewport coordinates to keep fixed.
     * 
     * Used for zoom-towards-cursor behavior with mouse wheel.
     */
    void zoomAtPoint(qreal newZoom, QPointF viewportPt);
    
    // ===== Rendering Helpers (Task 1.3.3) =====
    
    /**
     * @brief Render a single page (background + content).
     * @param painter The QPainter to render to.
     * @param page The page to render.
     * @param pageIndex The page index (for PDF pages).
     * 
     * Assumes painter is already translated to page position.
     * Handles solid color, grid, lines, and PDF backgrounds.
     */
    void renderPage(QPainter& painter, Page* page, int pageIndex);
    
    /**
     * @brief Get the effective DPI for rendering PDF at current zoom.
     * @return DPI value scaled by zoom level.
     */
    qreal effectivePdfDpi() const;
    
    /**
     * @brief Whether to show debug overlay.
     */
    bool m_showDebugOverlay = true;
};
